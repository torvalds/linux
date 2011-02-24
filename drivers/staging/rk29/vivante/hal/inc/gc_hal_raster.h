/****************************************************************************
*
*    Copyright (C) 2005 - 2011 by Vivante Corp.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/




#ifndef __gc_hal_raster_h_
#define __gc_hal_raster_h_

#include "gc_hal_enum.h"
#include "gc_hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************\
****************************** Object Declarations *****************************
\******************************************************************************/

typedef struct _gcoBRUSH *				gcoBRUSH;
typedef struct _gcoBRUSH_CACHE *  		gcoBRUSH_CACHE;

/******************************************************************************\
******************************** gcoBRUSH Object *******************************
\******************************************************************************/

/* Create a new solid color gcoBRUSH object. */
gceSTATUS
gcoBRUSH_ConstructSingleColor(
	IN gcoHAL Hal,
	IN gctUINT32 ColorConvert,
	IN gctUINT32 Color,
	IN gctUINT64 Mask,
	gcoBRUSH * Brush
	);

/* Create a new monochrome gcoBRUSH object. */
gceSTATUS
gcoBRUSH_ConstructMonochrome(
	IN gcoHAL Hal,
	IN gctUINT32 OriginX,
	IN gctUINT32 OriginY,
	IN gctUINT32 ColorConvert,
	IN gctUINT32 FgColor,
	IN gctUINT32 BgColor,
	IN gctUINT64 Bits,
	IN gctUINT64 Mask,
	gcoBRUSH * Brush
	);

/* Create a color gcoBRUSH object. */
gceSTATUS
gcoBRUSH_ConstructColor(
	IN gcoHAL Hal,
	IN gctUINT32 OriginX,
	IN gctUINT32 OriginY,
	IN gctPOINTER Address,
	IN gceSURF_FORMAT Format,
	IN gctUINT64 Mask,
	gcoBRUSH * Brush
	);

/* Destroy an gcoBRUSH object. */
gceSTATUS
gcoBRUSH_Destroy(
	IN gcoBRUSH Brush
	);

/******************************************************************************\
******************************** gcoSURF Object *******************************
\******************************************************************************/

/* Set cipping rectangle. */
gceSTATUS
gcoSURF_SetClipping(
	IN gcoSURF Surface
	);

/* Clear one or more rectangular areas. */
gceSTATUS
gcoSURF_Clear2D(
	IN gcoSURF DestSurface,
	IN gctUINT32 RectCount,
	IN gcsRECT_PTR DestRect,
	IN gctUINT32 LoColor,
	IN gctUINT32 HiColor
	);

/* Draw one or more Bresenham lines. */
gceSTATUS
gcoSURF_Line(
	IN gcoSURF Surface,
	IN gctUINT32 LineCount,
	IN gcsRECT_PTR Position,
	IN gcoBRUSH Brush,
	IN gctUINT8 FgRop,
	IN gctUINT8 BgRop
	);

/* Generic rectangular blit. */
gceSTATUS
gcoSURF_Blit(
	IN OPTIONAL gcoSURF SrcSurface,
	IN gcoSURF DestSurface,
	IN gctUINT32 RectCount,
	IN OPTIONAL gcsRECT_PTR SrcRect,
	IN gcsRECT_PTR DestRect,
	IN OPTIONAL gcoBRUSH Brush,
	IN gctUINT8 FgRop,
	IN gctUINT8 BgRop,
	IN OPTIONAL gceSURF_TRANSPARENCY Transparency,
	IN OPTIONAL gctUINT32 TransparencyColor,
	IN OPTIONAL gctPOINTER Mask,
	IN OPTIONAL gceSURF_MONOPACK MaskPack
	);

/* Monochrome blit. */
gceSTATUS
gcoSURF_MonoBlit(
	IN gcoSURF DestSurface,
	IN gctPOINTER Source,
	IN gceSURF_MONOPACK SourcePack,
	IN gcsPOINT_PTR SourceSize,
	IN gcsPOINT_PTR SourceOrigin,
	IN gcsRECT_PTR DestRect,
	IN OPTIONAL gcoBRUSH Brush,
	IN gctUINT8 FgRop,
	IN gctUINT8 BgRop,
	IN gctBOOL ColorConvert,
	IN gctUINT8 MonoTransparency,
	IN gceSURF_TRANSPARENCY Transparency,
	IN gctUINT32 FgColor,
	IN gctUINT32 BgColor
	);

/* Filter blit. */
gceSTATUS
gcoSURF_FilterBlit(
	IN gcoSURF SrcSurface,
	IN gcoSURF DestSurface,
	IN gcsRECT_PTR SrcRect,
	IN gcsRECT_PTR DestRect,
	IN gcsRECT_PTR DestSubRect
	);

/* Enable alpha blending engine in the hardware and disengage the ROP engine. */
gceSTATUS
gcoSURF_EnableAlphaBlend(
	IN gcoSURF Surface,
	IN gctUINT8 SrcGlobalAlphaValue,
	IN gctUINT8 DstGlobalAlphaValue,
	IN gceSURF_PIXEL_ALPHA_MODE SrcAlphaMode,
	IN gceSURF_PIXEL_ALPHA_MODE DstAlphaMode,
	IN gceSURF_GLOBAL_ALPHA_MODE SrcGlobalAlphaMode,
	IN gceSURF_GLOBAL_ALPHA_MODE DstGlobalAlphaMode,
	IN gceSURF_BLEND_FACTOR_MODE SrcFactorMode,
	IN gceSURF_BLEND_FACTOR_MODE DstFactorMode,
	IN gceSURF_PIXEL_COLOR_MODE SrcColorMode,
	IN gceSURF_PIXEL_COLOR_MODE DstColorMode
	);

/* Disable alpha blending engine in the hardware and engage the ROP engine. */
gceSTATUS
gcoSURF_DisableAlphaBlend(
	IN gcoSURF Surface
	);

/* Copy a rectangular area with format conversion. */
gceSTATUS
gcoSURF_CopyPixels(
	IN gcoSURF Source,
	IN gcoSURF Target,
	IN gctINT SourceX,
	IN gctINT SourceY,
	IN gctINT TargetX,
	IN gctINT TargetY,
	IN gctINT Width,
	IN gctINT Height
	);

/* Read surface pixel. */
gceSTATUS
gcoSURF_ReadPixel(
	IN gcoSURF Surface,
	IN gctPOINTER Memory,
	IN gctINT X,
	IN gctINT Y,
	IN gceSURF_FORMAT Format,
	OUT gctPOINTER PixelValue
	);

/* Write surface pixel. */
gceSTATUS
gcoSURF_WritePixel(
	IN gcoSURF Surface,
	IN gctPOINTER Memory,
	IN gctINT X,
	IN gctINT Y,
	IN gceSURF_FORMAT Format,
	IN gctPOINTER PixelValue
	);

/******************************************************************************\
********************************** gco2D Object *********************************
\******************************************************************************/

/* Construct a new gco2D object. */
gceSTATUS
gco2D_Construct(
	IN gcoHAL Hal,
	OUT gco2D * Hardware
	);

/* Destroy an gco2D object. */
gceSTATUS
gco2D_Destroy(
	IN gco2D Hardware
	);

/* Sets the maximum number of brushes in the brush cache. */
gceSTATUS
gco2D_SetBrushLimit(
	IN gco2D Hardware,
	IN gctUINT MaxCount
	);

/* Flush the brush. */
gceSTATUS
gco2D_FlushBrush(
	IN gco2D Engine,
	IN gcoBRUSH Brush,
	IN gceSURF_FORMAT Format
	);

/* Program the specified solid color brush. */
gceSTATUS
gco2D_LoadSolidBrush(
	IN gco2D Engine,
	IN gceSURF_FORMAT Format,
	IN gctUINT32 ColorConvert,
	IN gctUINT32 Color,
	IN gctUINT64 Mask
	);

/* Configure monochrome source. */
gceSTATUS
gco2D_SetMonochromeSource(
	IN gco2D Engine,
	IN gctBOOL ColorConvert,
	IN gctUINT8 MonoTransparency,
	IN gceSURF_MONOPACK DataPack,
	IN gctBOOL CoordRelative,
	IN gceSURF_TRANSPARENCY Transparency,
	IN gctUINT32 FgColor,
	IN gctUINT32 BgColor
	);

/* Configure color source. */
gceSTATUS
gco2D_SetColorSource(
	IN gco2D Engine,
	IN gctUINT32 Address,
	IN gctUINT32 Stride,
	IN gceSURF_FORMAT Format,
	IN gceSURF_ROTATION Rotation,
	IN gctUINT32 SurfaceWidth,
	IN gctBOOL CoordRelative,
	IN gceSURF_TRANSPARENCY Transparency,
	IN gctUINT32 TransparencyColor
	);

/* Configure color source extension for full rotation. */
gceSTATUS
gco2D_SetColorSourceEx(
	IN gco2D Engine,
	IN gctUINT32 Address,
	IN gctUINT32 Stride,
	IN gceSURF_FORMAT Format,
	IN gceSURF_ROTATION Rotation,
	IN gctUINT32 SurfaceWidth,
	IN gctUINT32 SurfaceHeight,
	IN gctBOOL CoordRelative,
	IN gceSURF_TRANSPARENCY Transparency,
	IN gctUINT32 TransparencyColor
	);

/* Configure color source. */
gceSTATUS
gco2D_SetColorSourceAdvanced(
	IN gco2D Engine,
	IN gctUINT32 Address,
	IN gctUINT32 Stride,
	IN gceSURF_FORMAT Format,
	IN gceSURF_ROTATION Rotation,
	IN gctUINT32 SurfaceWidth,
	IN gctUINT32 SurfaceHeight,
	IN gctBOOL CoordRelative
	);

/* Configure masked color source. */
gceSTATUS
gco2D_SetMaskedSource(
	IN gco2D Engine,
	IN gctUINT32 Address,
	IN gctUINT32 Stride,
	IN gceSURF_FORMAT Format,
	IN gctBOOL CoordRelative,
	IN gceSURF_MONOPACK MaskPack
	);

/* Configure masked color source extension for full rotation. */
gceSTATUS
gco2D_SetMaskedSourceEx(
	IN gco2D Engine,
	IN gctUINT32 Address,
	IN gctUINT32 Stride,
	IN gceSURF_FORMAT Format,
	IN gctBOOL CoordRelative,
	IN gceSURF_MONOPACK MaskPack,
	IN gceSURF_ROTATION Rotation,
	IN gctUINT32 SurfaceWidth,
	IN gctUINT32 SurfaceHeight
	);

/* Setup the source rectangle. */
gceSTATUS
gco2D_SetSource(
	IN gco2D Engine,
	IN gcsRECT_PTR SrcRect
	);

/* Set clipping rectangle. */
gceSTATUS
gco2D_SetClipping(
	IN gco2D Engine,
	IN gcsRECT_PTR Rect
	);

/* Configure destination. */
gceSTATUS
gco2D_SetTarget(
	IN gco2D Engine,
	IN gctUINT32 Address,
	IN gctUINT32 Stride,
	IN gceSURF_ROTATION Rotation,
	IN gctUINT32 SurfaceWidth
	);

/* Configure destination extension for full rotation. */
gceSTATUS
gco2D_SetTargetEx(
	IN gco2D Engine,
	IN gctUINT32 Address,
	IN gctUINT32 Stride,
	IN gceSURF_ROTATION Rotation,
	IN gctUINT32 SurfaceWidth,
	IN gctUINT32 SurfaceHeight
	);

/* Calculate and program the stretch factors. */
gceSTATUS
gco2D_SetStretchFactors(
	IN gco2D Engine,
	IN gctUINT32 HorFactor,
	IN gctUINT32 VerFactor
	);

/* Calculate and program the stretch factors based on the rectangles. */
gceSTATUS
gco2D_SetStretchRectFactors(
	IN gco2D Engine,
	IN gcsRECT_PTR SrcRect,
	IN gcsRECT_PTR DestRect
	);

/* Create a new solid color gcoBRUSH object. */
gceSTATUS
gco2D_ConstructSingleColorBrush(
	IN gco2D Engine,
	IN gctUINT32 ColorConvert,
	IN gctUINT32 Color,
	IN gctUINT64 Mask,
	gcoBRUSH * Brush
	);

/* Create a new monochrome gcoBRUSH object. */
gceSTATUS
gco2D_ConstructMonochromeBrush(
	IN gco2D Engine,
	IN gctUINT32 OriginX,
	IN gctUINT32 OriginY,
	IN gctUINT32 ColorConvert,
	IN gctUINT32 FgColor,
	IN gctUINT32 BgColor,
	IN gctUINT64 Bits,
	IN gctUINT64 Mask,
	gcoBRUSH * Brush
	);

/* Create a color gcoBRUSH object. */
gceSTATUS
gco2D_ConstructColorBrush(
	IN gco2D Engine,
	IN gctUINT32 OriginX,
	IN gctUINT32 OriginY,
	IN gctPOINTER Address,
	IN gceSURF_FORMAT Format,
	IN gctUINT64 Mask,
	gcoBRUSH * Brush
	);

/* Clear one or more rectangular areas. */
gceSTATUS
gco2D_Clear(
	IN gco2D Engine,
	IN gctUINT32 RectCount,
	IN gcsRECT_PTR Rect,
	IN gctUINT32 Color32,
	IN gctUINT8 FgRop,
	IN gctUINT8 BgRop,
	IN gceSURF_FORMAT DestFormat
	);

/* Draw one or more Bresenham lines. */
gceSTATUS
gco2D_Line(
	IN gco2D Engine,
	IN gctUINT32 LineCount,
	IN gcsRECT_PTR Position,
	IN gcoBRUSH Brush,
	IN gctUINT8 FgRop,
	IN gctUINT8 BgRop,
	IN gceSURF_FORMAT DestFormat
	);

/* Draw one or more Bresenham lines based on the 32-bit color. */
gceSTATUS
gco2D_ColorLine(
	IN gco2D Engine,
	IN gctUINT32 LineCount,
	IN gcsRECT_PTR Position,
	IN gctUINT32 Color32,
	IN gctUINT8 FgRop,
	IN gctUINT8 BgRop,
	IN gceSURF_FORMAT DestFormat
	);

/* Generic blit. */
gceSTATUS
gco2D_Blit(
	IN gco2D Engine,
	IN gctUINT32 RectCount,
	IN gcsRECT_PTR Rect,
	IN gctUINT8 FgRop,
	IN gctUINT8 BgRop,
	IN gceSURF_FORMAT DestFormat
	);

/* Batch blit. */
gceSTATUS
gco2D_BatchBlit(
	IN gco2D Engine,
	IN gctUINT32 RectCount,
	IN gcsRECT_PTR SrcRect,
	IN gcsRECT_PTR DestRect,
	IN gctUINT8 FgRop,
	IN gctUINT8 BgRop,
	IN gceSURF_FORMAT DestFormat
	);

/* Stretch blit. */
gceSTATUS
gco2D_StretchBlit(
	IN gco2D Engine,
	IN gctUINT32 RectCount,
	IN gcsRECT_PTR Rect,
	IN gctUINT8 FgRop,
	IN gctUINT8 BgRop,
	IN gceSURF_FORMAT DestFormat
	);

/* Monochrome blit. */
gceSTATUS
gco2D_MonoBlit(
	IN gco2D Engine,
	IN gctPOINTER StreamBits,
	IN gcsPOINT_PTR StreamSize,
	IN gcsRECT_PTR StreamRect,
	IN gceSURF_MONOPACK SrcStreamPack,
	IN gceSURF_MONOPACK DestStreamPack,
	IN gcsRECT_PTR DestRect,
	IN gctUINT32 FgRop,
	IN gctUINT32 BgRop,
	IN gceSURF_FORMAT DestFormat
	);

/* Set kernel size. */
gceSTATUS
gco2D_SetKernelSize(
	IN gco2D Engine,
	IN gctUINT8 HorKernelSize,
	IN gctUINT8 VerKernelSize
	);

/* Set filter type. */
gceSTATUS
gco2D_SetFilterType(
	IN gco2D Engine,
	IN gceFILTER_TYPE FilterType
	);

/* Set the filter kernel by user. */
gceSTATUS
gco2D_SetUserFilterKernel(
	IN gco2D Engine,
	IN gceFILTER_PASS_TYPE PassType,
	IN gctUINT16_PTR KernelArray
	);

/* Select the pass(es) to be done for user defined filter. */
gceSTATUS
gco2D_EnableUserFilterPasses(
	IN gco2D Engine,
	IN gctBOOL HorPass,
	IN gctBOOL VerPass
	);

/* Frees the temporary buffer allocated by filter blit operation. */
gceSTATUS
gco2D_FreeFilterBuffer(
	IN gco2D Engine
	);

/* Filter blit. */
gceSTATUS
gco2D_FilterBlit(
	IN gco2D Engine,
	IN gctUINT32 SrcAddress,
	IN gctUINT SrcStride,
	IN gctUINT32 SrcUAddress,
	IN gctUINT SrcUStride,
	IN gctUINT32 SrcVAddress,
	IN gctUINT SrcVStride,
	IN gceSURF_FORMAT SrcFormat,
	IN gceSURF_ROTATION SrcRotation,
	IN gctUINT32 SrcSurfaceWidth,
	IN gcsRECT_PTR SrcRect,
	IN gctUINT32 DestAddress,
	IN gctUINT DestStride,
	IN gceSURF_FORMAT DestFormat,
	IN gceSURF_ROTATION DestRotation,
	IN gctUINT32 DestSurfaceWidth,
	IN gcsRECT_PTR DestRect,
	IN gcsRECT_PTR DestSubRect
	);

/* Filter blit extension for full rotation. */
gceSTATUS
gco2D_FilterBlitEx(
	IN gco2D Engine,
	IN gctUINT32 SrcAddress,
	IN gctUINT SrcStride,
	IN gctUINT32 SrcUAddress,
	IN gctUINT SrcUStride,
	IN gctUINT32 SrcVAddress,
	IN gctUINT SrcVStride,
	IN gceSURF_FORMAT SrcFormat,
	IN gceSURF_ROTATION SrcRotation,
	IN gctUINT32 SrcSurfaceWidth,
	IN gctUINT32 SrcSurfaceHeight,
	IN gcsRECT_PTR SrcRect,
	IN gctUINT32 DestAddress,
	IN gctUINT DestStride,
	IN gceSURF_FORMAT DestFormat,
	IN gceSURF_ROTATION DestRotation,
	IN gctUINT32 DestSurfaceWidth,
	IN gctUINT32 DestSurfaceHeight,
	IN gcsRECT_PTR DestRect,
	IN gcsRECT_PTR DestSubRect
	);

/* Enable alpha blending engine in the hardware and disengage the ROP engine. */
gceSTATUS
gco2D_EnableAlphaBlend(
	IN gco2D Engine,
	IN gctUINT8 SrcGlobalAlphaValue,
	IN gctUINT8 DstGlobalAlphaValue,
	IN gceSURF_PIXEL_ALPHA_MODE SrcAlphaMode,
	IN gceSURF_PIXEL_ALPHA_MODE DstAlphaMode,
	IN gceSURF_GLOBAL_ALPHA_MODE SrcGlobalAlphaMode,
	IN gceSURF_GLOBAL_ALPHA_MODE DstGlobalAlphaMode,
	IN gceSURF_BLEND_FACTOR_MODE SrcFactorMode,
	IN gceSURF_BLEND_FACTOR_MODE DstFactorMode,
	IN gceSURF_PIXEL_COLOR_MODE SrcColorMode,
	IN gceSURF_PIXEL_COLOR_MODE DstColorMode
	);

/* Enable alpha blending engine in the hardware. */
gceSTATUS
gco2D_EnableAlphaBlendAdvanced(
	IN gco2D Engine,
	IN gceSURF_PIXEL_ALPHA_MODE SrcAlphaMode,
	IN gceSURF_PIXEL_ALPHA_MODE DstAlphaMode,
	IN gceSURF_GLOBAL_ALPHA_MODE SrcGlobalAlphaMode,
	IN gceSURF_GLOBAL_ALPHA_MODE DstGlobalAlphaMode,
	IN gceSURF_BLEND_FACTOR_MODE SrcFactorMode,
	IN gceSURF_BLEND_FACTOR_MODE DstFactorMode
	);

/* Enable alpha blending engine with Porter Duff rule. */
gceSTATUS
gco2D_SetPorterDuffBlending(
	IN gco2D Engine,
	IN gce2D_PORTER_DUFF_RULE Rule
	);

/* Disable alpha blending engine in the hardware and engage the ROP engine. */
gceSTATUS
gco2D_DisableAlphaBlend(
	IN gco2D Engine
	);

/* Retrieve the maximum number of 32-bit data chunks for a single DE command. */
gctUINT32
gco2D_GetMaximumDataCount(
	void
	);

/* Retrieve the maximum number of rectangles, that can be passed in a single DE command. */
gctUINT32
gco2D_GetMaximumRectCount(
	void
	);

/* Returns the pixel alignment of the surface. */
gceSTATUS
gco2D_GetPixelAlignment(
	gceSURF_FORMAT Format,
	gcsPOINT_PTR Alignment
	);

/* Retrieve monochrome stream pack size. */
gceSTATUS
gco2D_GetPackSize(
	IN gceSURF_MONOPACK StreamPack,
	OUT gctUINT32 * PackWidth,
	OUT gctUINT32 * PackHeight
	);

/* Flush the 2D pipeline. */
gceSTATUS
gco2D_Flush(
	IN gco2D Engine
	);

/* Load 256-entry color table for INDEX8 source surfaces. */
gceSTATUS
gco2D_LoadPalette(
	IN gco2D Engine,
	IN gctUINT FirstIndex,
	IN gctUINT IndexCount,
	IN gctPOINTER ColorTable,
	IN gctBOOL ColorConvert
	);

/* Enable/disable 2D BitBlt mirrorring. */
gceSTATUS
gco2D_SetBitBlitMirror(
	IN gco2D Engine,
	IN gctBOOL HorizontalMirror,
	IN gctBOOL VerticalMirror
	);

/* Set the transparency for source, destination and pattern. */
gceSTATUS
gco2D_SetTransparencyAdvanced(
	IN gco2D Engine,
	IN gce2D_TRANSPARENCY SrcTransparency,
	IN gce2D_TRANSPARENCY DstTransparency,
	IN gce2D_TRANSPARENCY PatTransparency
	);

/* Set the source color key. */
gceSTATUS
gco2D_SetSourceColorKeyAdvanced(
	IN gco2D Engine,
	IN gctUINT32 ColorKey
	);

/* Set the source color key range. */
gceSTATUS
gco2D_SetSourceColorKeyRangeAdvanced(
	IN gco2D Engine,
	IN gctUINT32 ColorKeyLow,
	IN gctUINT32 ColorKeyHigh
	);

/* Set the target color key. */
gceSTATUS
gco2D_SetTargetColorKeyAdvanced(
	IN gco2D Engine,
	IN gctUINT32 ColorKey
	);

/* Set the target color key range. */
gceSTATUS
gco2D_SetTargetColorKeyRangeAdvanced(
	IN gco2D Engine,
	IN gctUINT32 ColorKeyLow,
	IN gctUINT32 ColorKeyHigh
	);

/* Set the YUV color space mode. */
gceSTATUS
gco2D_SetYUVColorMode(
	IN gco2D Engine,
	IN gce2D_YUV_COLOR_MODE Mode
	);

/* Setup the source global color value in ARGB8 format. */
gceSTATUS gco2D_SetSourceGlobalColorAdvanced(
	IN gco2D Engine,
	IN gctUINT32 Color32
	);

/* Setup the target global color value in ARGB8 format. */
gceSTATUS gco2D_SetTargetGlobalColorAdvanced(
	IN gco2D Engine,
	IN gctUINT32 Color32
	);

/* Setup the source and target pixel multiply modes. */
gceSTATUS
gco2D_SetPixelMultiplyModeAdvanced(
	IN gco2D Engine,
	IN gce2D_PIXEL_COLOR_MULTIPLY_MODE SrcPremultiplySrcAlpha,
	IN gce2D_PIXEL_COLOR_MULTIPLY_MODE DstPremultiplyDstAlpha,
	IN gce2D_GLOBAL_COLOR_MULTIPLY_MODE SrcPremultiplyGlobalMode,
	IN gce2D_PIXEL_COLOR_MULTIPLY_MODE DstDemultiplyDstAlpha
	);

/* Set the GPU clock cycles after which the idle engine will keep auto-flushing. */
gceSTATUS
gco2D_SetAutoFlushCycles(
	IN gco2D Engine,
	IN gctUINT32 Cycles
	);

/* Read the profile registers available in the 2D engine and sets them in the profile.
   The function will also reset the pixelsRendered counter every time.
*/
gceSTATUS
gco2D_ProfileEngine(
	IN gco2D Engine,
	OPTIONAL gcs2D_PROFILE_PTR Profile
	);

/* Enable or disable 2D dithering. */
gceSTATUS
gco2D_EnableDither(
	IN gco2D Engine,
	IN gctBOOL Enable
	);

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_raster_h_ */

