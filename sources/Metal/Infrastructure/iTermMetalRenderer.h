#import <Foundation/Foundation.h>

#import "iTermMetalBufferPool.h"
#import "iTermMetalDebugInfo.h"
#import "iTermMetalFrameData.h"
#import "iTermPreciseTimer.h"
#import "iTermShaderTypes.h"
#import "iTermTexturePool.h"
#import <MetalKit/MetalKit.h>
#import <simd/simd.h>

NS_ASSUME_NONNULL_BEGIN

// Maybe I could increase this in the future but it's easier to reason about issues during development when it's 1.
// This is exposed because it's used to set the capacity of mixed-size buffer pools.
extern const NSInteger iTermMetalDriverMaximumNumberOfFramesInFlight;

@class iTermImageWrapper;
@class iTermMetalRendererTransientState;

NS_CLASS_AVAILABLE(10_11, NA)
@interface iTermRenderConfiguration : NSObject
@property (nonatomic, readonly) vector_uint2 viewportSize;
@property (nonatomic, readonly) vector_uint2 viewportSizeExcludingLegacyScrollbars;
@property (nonatomic, readonly) CGFloat scale;
@property (nonatomic, readonly) BOOL hasBackgroundImage;
@property (nonatomic, readonly) CGFloat rightExtraPixels;

// NOT flipped. top means top, bottom means bottom.
@property (nonatomic, readonly) NSEdgeInsets extraMargins;
@property (nonatomic, readonly) CGFloat maximumExtendedDynamicRangeColorComponentValue;
@property (nonatomic, readonly) NSColorSpace *colorSpace;

- (instancetype)init NS_UNAVAILABLE;
- (instancetype)initWithViewportSize:(vector_uint2)viewportSize
                legacyScrollbarWidth:(unsigned int)legacyScrollbarWidth
                               scale:(CGFloat)scale
                  hasBackgroundImage:(BOOL)hasBackgroundImage
                        extraMargins:(NSEdgeInsets)extraMargins
maximumExtendedDynamicRangeColorComponentValue:(CGFloat)maximumExtendedDynamicRangeColorComponentValue
                          colorSpace:(NSColorSpace *)colorSpace
                    rightExtraPixels:(CGFloat)rightExtraPixels NS_DESIGNATED_INITIALIZER;
@end

NS_CLASS_AVAILABLE(10_11, NA)
@protocol iTermMetalRenderer<NSObject>
@property (nonatomic, readonly) BOOL rendererDisabled;

- (iTermMetalFrameDataStat)createTransientStateStat;
- (void)drawWithFrameData:(iTermMetalFrameData *)frameData
           transientState:(__kindof iTermMetalRendererTransientState *)transientState;

- (nullable __kindof iTermMetalRendererTransientState *)createTransientStateForConfiguration:(iTermRenderConfiguration *)configuration
                                                                               commandBuffer:(id<MTLCommandBuffer>)commandBuffer;

@end

NS_CLASS_AVAILABLE(10_11, NA)
@interface iTermMetalRendererTransientState : NSObject
@property (nonatomic, strong, readonly) __kindof iTermRenderConfiguration *configuration;
@property (nonatomic, strong) id<MTLBuffer> vertexBuffer;
@property (nonatomic, readonly) iTermMetalBufferPoolContext *poolContext;
@property (nonatomic, weak) iTermMetalDebugInfo *debugInfo;
@property (nonatomic, strong) NSImage *renderedOutputForDebugging;
@property (nonatomic) NSUInteger sequenceNumber;

// These values are in points. Scissor rect will be used to prevent drawing in these regions.
// Use them to prevent drawing over the offscreen command line (top) and the auto composer (bottom).
@property (nonatomic) CGFloat suppressedTopHeight;
@property (nonatomic) CGFloat suppressedBottomHeight;

// You don't generally need to assign to this unless you plan to make more than one draw call.
// You can get a pipeline state from the iTermMetal[Cell]Renderer. See its comments for details.
@property (nonatomic, strong) id<MTLRenderPipelineState> pipelineState;
@property (nonatomic, readonly) BOOL skipRenderer;

- (instancetype)initWithConfiguration:(__kindof iTermRenderConfiguration *)configuration;
- (instancetype)init NS_UNAVAILABLE;

- (void)measureTimeForStat:(int)index ofBlock:(void (^)(void))block;
- (nullable iTermPreciseTimerStats *)stats;
- (int)numberOfStats;
- (NSString *)nameForStat:(int)i;

// Subclasses should override this to provide useful debugging info.
- (void)writeDebugInfoToFolder:(NSURL *)folder NS_REQUIRES_SUPER;

@end

@class iTermMetalBufferPoolContext;

NS_CLASS_AVAILABLE(10_11, NA)
@interface iTermMetalBlending : NSObject
@property (nonatomic) MTLBlendOperation rgbBlendOperation;
@property (nonatomic) MTLBlendOperation alphaBlendOperation;
@property (nonatomic) MTLBlendFactor sourceRGBBlendFactor;
@property (nonatomic) MTLBlendFactor destinationRGBBlendFactor;
@property (nonatomic) MTLBlendFactor sourceAlphaBlendFactor;
@property (nonatomic) MTLBlendFactor destinationAlphaBlendFactor;

// Use this for premultiplied blending.
+ (instancetype)compositeSourceOver;

+ (instancetype)atop;

#if ENABLE_TRANSPARENT_METAL_WINDOWS
+ (instancetype)premultipliedCompositing;
#endif

@end

NS_CLASS_AVAILABLE(10_11, NA)
@interface iTermMetalRenderer : NSObject <iTermMetalDebugInfoFormatter>

@property (nonatomic, readonly) id<MTLDevice> device;
@property (nonatomic, readonly) Class transientStateClass;
@property (nonatomic, copy) NSString *vertexFunctionName;
@property (nonatomic, copy) NSString *fragmentFunctionName;
@property (nonatomic, weak) id<iTermMetalDebugInfoFormatter> formatterDelegate;

// Pool of vertex buffers for quads of iTermVertex.
@property (nonatomic, readonly) iTermMetalBufferPool *verticesPool;

- (nullable instancetype)initWithDevice:(id<MTLDevice>)device;

- (nullable instancetype)initWithDevice:(id<MTLDevice>)device
                     vertexFunctionName:(NSString *)vertexFunctionName
                   fragmentFunctionName:(NSString *)fragmentFunctionName
                               blending:(nullable iTermMetalBlending *)blending
                    transientStateClass:(Class)transientStateClass;

- (instancetype)init NS_UNAVAILABLE;

// Returns the pipeline state based on the current value of `fragmentFunctionName`, which you can
// change whenever you please.
- (id<MTLRenderPipelineState>)pipelineState;

#pragma mark - For subclasses

- (id<MTLBuffer>)newQuadOfSize:(CGSize)size poolContext:(iTermMetalBufferPoolContext *)poolContext;
- (id<MTLBuffer>)newQuadOfSize:(CGSize)size origin:(CGPoint)origin poolContext:(iTermMetalBufferPoolContext *)poolContext;

- (id<MTLBuffer>)newQuadWithFrame:(CGRect)quad  // pixel coordinates on viewport with 0,0 at bottom left
                     textureFrame:(CGRect)textureFrame  // normalized coordinates
                      poolContext:(iTermMetalBufferPoolContext *)poolContext;

- (id<MTLBuffer>)newFlippedQuadWithFrame:(CGRect)quad
                            textureFrame:(CGRect)textureFrame
                             poolContext:(iTermMetalBufferPoolContext *)poolContext;

// Things in Metal are randomly upside down for no good reason. So make it easy to flip them back.
- (id<MTLBuffer>)newFlippedQuadOfSize:(CGSize)size poolContext:(iTermMetalBufferPoolContext *)poolContext;

- (void)drawWithTransientState:(iTermMetalRendererTransientState *)tState
                 renderEncoder:(id <MTLRenderCommandEncoder>)renderEncoder
              numberOfVertices:(NSInteger)numberOfVertices
                  numberOfPIUs:(NSInteger)numberOfPIUs
                 vertexBuffers:(NSDictionary<NSNumber *, id<MTLBuffer>> *)vertexBuffers
               fragmentBuffers:(NSDictionary<NSNumber *, id<MTLBuffer>> *)fragmentBuffers
                      textures:(NSDictionary<NSNumber *, id<MTLTexture>> *)textures;

- (nullable id<MTLTexture>)textureFromImage:(iTermImageWrapper *)image context:(nullable iTermMetalBufferPoolContext *)context colorSpace:(NSColorSpace *)colorSpace;
- (nullable id<MTLTexture>)textureFromImage:(iTermImageWrapper *)image context:(nullable iTermMetalBufferPoolContext *)context pool:(nullable iTermTexturePool *)pool colorSpace:(NSColorSpace *)colorSpace;

- (id<MTLRenderPipelineState>)newPipelineWithBlending:(nullable iTermMetalBlending *)blending
                                       vertexFunction:(id<MTLFunction>)vertexFunction
                                     fragmentFunction:(id<MTLFunction>)fragmentFunction;

- (nullable __kindof iTermMetalRendererTransientState *)createTransientStateForConfiguration:(iTermRenderConfiguration *)configuration
                                                                               commandBuffer:(id<MTLCommandBuffer>)commandBuffer;

@end

int iTermBitsPerSampleForPixelFormat(MTLPixelFormat format);

NS_ASSUME_NONNULL_END
