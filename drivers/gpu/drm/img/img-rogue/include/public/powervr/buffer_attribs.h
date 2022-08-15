/*************************************************************************/ /*!
@File
@Title          3D types for use by IMG APIs
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        MIT

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/ /**************************************************************************/
#ifndef POWERVR_BUFFER_ATTRIBS_H
#define POWERVR_BUFFER_ATTRIBS_H

/*!
 * Memory layouts
 * Defines how pixels are laid out within a surface.
 */
typedef enum
{
	IMG_MEMLAYOUT_STRIDED,       /**< Resource is strided, one row at a time */
	IMG_MEMLAYOUT_TWIDDLED,      /**< Resource is 2D twiddled to match HW */
	IMG_MEMLAYOUT_3DTWIDDLED,    /**< Resource is 3D twiddled, classic style */
	IMG_MEMLAYOUT_TILED,         /**< Resource is tiled, tiling config specified elsewhere. */
	IMG_MEMLAYOUT_PAGETILED,     /**< Resource is pagetiled */
	IMG_MEMLAYOUT_INVNTWIDDLED,  /**< Resource is 2D twiddled !N style */
} IMG_MEMLAYOUT;

/*!
 * Rotation types
 */
typedef enum
{
	IMG_ROTATION_0DEG = 0,
	IMG_ROTATION_90DEG = 1,
	IMG_ROTATION_180DEG = 2,
	IMG_ROTATION_270DEG = 3,
	IMG_ROTATION_FLIP_Y = 4,

	IMG_ROTATION_BAD = 255,
} IMG_ROTATION;

/*!
 * Alpha types.
 */
typedef enum
{
	IMG_COLOURSPACE_FORMAT_UNKNOWN                 =  0x0UL << 16,
	IMG_COLOURSPACE_FORMAT_LINEAR                  =  0x1UL << 16,
	IMG_COLOURSPACE_FORMAT_SRGB                    =  0x2UL << 16,
	IMG_COLOURSPACE_FORMAT_SCRGB                   =  0x3UL << 16,
	IMG_COLOURSPACE_FORMAT_SCRGB_LINEAR            =  0x4UL << 16,
	IMG_COLOURSPACE_FORMAT_DISPLAY_P3_LINEAR       =  0x5UL << 16,
	IMG_COLOURSPACE_FORMAT_DISPLAY_P3              =  0x6UL << 16,
	IMG_COLOURSPACE_FORMAT_BT2020_PQ               =  0x7UL << 16,
	IMG_COLOURSPACE_FORMAT_BT2020_LINEAR           =  0x8UL << 16,
	IMG_COLOURSPACE_FORMAT_DISPLAY_P3_PASSTHROUGH  =  0x9UL << 16,
	IMG_COLOURSPACE_FORMAT_MASK                    =  0xFUL << 16,
} IMG_COLOURSPACE_FORMAT;

/*!
 * Determines if FB Compression is Lossy
 */
#define IS_FBCDC_LOSSY(mode)			((mode == IMG_FB_COMPRESSION_DIRECT_LOSSY50_8x8) ? IMG_TRUE : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY50_16x4) ? IMG_TRUE : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY50_32x2) ? IMG_TRUE : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY25_8x8)  ? IMG_TRUE : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY25_16x4) ? IMG_TRUE : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY25_32x2) ? IMG_TRUE : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY37_8x8)  ? IMG_TRUE : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY37_16x4) ? IMG_TRUE : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY37_32x2) ? IMG_TRUE : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY75_8x8)  ? IMG_TRUE : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY75_16x4) ? IMG_TRUE : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY75_32x2) ? IMG_TRUE : IMG_FALSE)

/*!
 * Determines if FB Compression is Packed
 */
#define IS_FBCDC_PACKED(mode)			((mode == IMG_FB_COMPRESSION_DIRECT_PACKED_8x8) ? IMG_TRUE : IMG_FALSE)

/*!
 * Returns type of FB Compression
 */
#define GET_FBCDC_BLOCK_TYPE(mode)		((mode == IMG_FB_COMPRESSION_DIRECT_LOSSY50_8x8) ? IMG_FB_COMPRESSION_DIRECT_8x8  : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY50_16x4) ? IMG_FB_COMPRESSION_DIRECT_16x4 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY50_32x2) ? IMG_FB_COMPRESSION_DIRECT_32x2 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_PACKED_8x8)   ? IMG_FB_COMPRESSION_DIRECT_8x8  : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY25_8x8)  ? IMG_FB_COMPRESSION_DIRECT_8x8  : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY25_16x4) ? IMG_FB_COMPRESSION_DIRECT_16x4 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY25_32x2) ? IMG_FB_COMPRESSION_DIRECT_32x2 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY37_8x8)  ? IMG_FB_COMPRESSION_DIRECT_8x8  : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY37_16x4) ? IMG_FB_COMPRESSION_DIRECT_16x4 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY37_32x2) ? IMG_FB_COMPRESSION_DIRECT_32x2 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY75_8x8)  ? IMG_FB_COMPRESSION_DIRECT_8x8  : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY75_16x4) ? IMG_FB_COMPRESSION_DIRECT_16x4 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY75_32x2) ? IMG_FB_COMPRESSION_DIRECT_32x2 : mode)

/*!
 * Adds Packing compression setting to mode if viable
 */
#define FBCDC_MODE_ADD_PACKING(mode)	((mode == IMG_FB_COMPRESSION_DIRECT_8x8) ? IMG_FB_COMPRESSION_DIRECT_PACKED_8x8 : mode)

/*!
 * Removes Packing compression setting from mode
 */
#define FBCDC_MODE_REMOVE_PACKING(mode)	((mode == IMG_FB_COMPRESSION_DIRECT_PACKED_8x8) ? IMG_FB_COMPRESSION_DIRECT_8x8 : mode)

/*!
 * Adds Lossy25 compression setting to mode if viable
 */
#define FBCDC_MODE_ADD_LOSSY25(mode)	((mode == IMG_FB_COMPRESSION_DIRECT_8x8) ? IMG_FB_COMPRESSION_DIRECT_LOSSY25_8x8 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_16x4) ? IMG_FB_COMPRESSION_DIRECT_LOSSY25_16x4 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_32x2) ? IMG_FB_COMPRESSION_DIRECT_LOSSY25_32x2 : mode)

/*!
 * Adds Lossy37 compression setting to mode if viable
 */
#define FBCDC_MODE_ADD_LOSSY37(mode)	((mode == IMG_FB_COMPRESSION_DIRECT_8x8) ? IMG_FB_COMPRESSION_DIRECT_LOSSY37_8x8 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_16x4) ? IMG_FB_COMPRESSION_DIRECT_LOSSY37_16x4 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_32x2) ? IMG_FB_COMPRESSION_DIRECT_LOSSY37_32x2 : mode)

/*!
 * Adds Lossy50 compression setting to mode if viable
 */
#define FBCDC_MODE_ADD_LOSSY50(mode)	((mode == IMG_FB_COMPRESSION_DIRECT_8x8) ? IMG_FB_COMPRESSION_DIRECT_LOSSY50_8x8 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_16x4) ? IMG_FB_COMPRESSION_DIRECT_LOSSY50_16x4 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_32x2) ? IMG_FB_COMPRESSION_DIRECT_LOSSY50_32x2 : mode)

/*!
 * Adds Lossy75 compression setting to mode if viable
 */
#define FBCDC_MODE_ADD_LOSSY75(mode)	((mode == IMG_FB_COMPRESSION_DIRECT_8x8) ? IMG_FB_COMPRESSION_DIRECT_LOSSY75_8x8 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_16x4) ? IMG_FB_COMPRESSION_DIRECT_LOSSY75_16x4 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_32x2) ? IMG_FB_COMPRESSION_DIRECT_LOSSY75_32x2 : mode)

/*!
 * Removes Lossy compression setting from mode
 */
#define FBCDC_MODE_REMOVE_LOSSY(mode)	((mode == IMG_FB_COMPRESSION_DIRECT_LOSSY50_8x8) ? IMG_FB_COMPRESSION_DIRECT_8x8  : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY50_16x4) ? IMG_FB_COMPRESSION_DIRECT_16x4 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY50_32x2) ? IMG_FB_COMPRESSION_DIRECT_32x2 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY25_8x8)  ? IMG_FB_COMPRESSION_DIRECT_8x8  : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY25_16x4) ? IMG_FB_COMPRESSION_DIRECT_16x4 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY25_32x2) ? IMG_FB_COMPRESSION_DIRECT_32x2 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY37_8x8)  ? IMG_FB_COMPRESSION_DIRECT_8x8  : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY37_16x4) ? IMG_FB_COMPRESSION_DIRECT_16x4 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY37_32x2) ? IMG_FB_COMPRESSION_DIRECT_32x2 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY75_8x8)  ? IMG_FB_COMPRESSION_DIRECT_8x8  : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY75_16x4) ? IMG_FB_COMPRESSION_DIRECT_16x4 : \
										(mode == IMG_FB_COMPRESSION_DIRECT_LOSSY75_32x2) ? IMG_FB_COMPRESSION_DIRECT_32x2 : mode)

/*!
 * Types of framebuffer compression
 */
typedef enum
{
	IMG_FB_COMPRESSION_NONE,
	IMG_FB_COMPRESSION_DIRECT_8x8,
	IMG_FB_COMPRESSION_DIRECT_16x4,
	IMG_FB_COMPRESSION_DIRECT_32x2,
	IMG_FB_COMPRESSION_DIRECT_LOSSY25_8x8,
	IMG_FB_COMPRESSION_DIRECT_LOSSY25_16x4,
	IMG_FB_COMPRESSION_DIRECT_LOSSY25_32x2,
	IMG_FB_COMPRESSION_DIRECT_LOSSY75_8x8,
	IMG_FB_COMPRESSION_DIRECT_LOSSY50_8x8,
	IMG_FB_COMPRESSION_DIRECT_LOSSY50_16x4,
	IMG_FB_COMPRESSION_DIRECT_LOSSY50_32x2,
	IMG_FB_COMPRESSION_DIRECT_PACKED_8x8,
	IMG_FB_COMPRESSION_DIRECT_LOSSY75_16x4,
	IMG_FB_COMPRESSION_DIRECT_LOSSY75_32x2,
	IMG_FB_COMPRESSION_DIRECT_LOSSY37_8x8,
	IMG_FB_COMPRESSION_DIRECT_LOSSY37_16x4,
	IMG_FB_COMPRESSION_DIRECT_LOSSY37_32x2,
} IMG_FB_COMPRESSION;


#endif /* POWERVR_BUFFER_ATTRIBS_H */
