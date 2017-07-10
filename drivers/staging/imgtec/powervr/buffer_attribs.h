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
#ifndef _POWERVR_BUFFER_ATTRIBS_H_
#define _POWERVR_BUFFER_ATTRIBS_H_

/**
 * Memory layouts
 * Defines how pixels are laid out within a surface.
 */
typedef enum
{
	IMG_MEMLAYOUT_STRIDED,       /**< Resource is strided, one row at a time */
	IMG_MEMLAYOUT_TWIDDLED,      /**< Resource is 2D twiddled, classic style */
	IMG_MEMLAYOUT_3DTWIDDLED,    /**< Resource is 3D twiddled, classic style */
	IMG_MEMLAYOUT_TILED,         /**< Resource is tiled, tiling config specified elsewhere. */
	IMG_MEMLAYOUT_PAGETILED,     /**< Resource is pagetiled */
} IMG_MEMLAYOUT;

/**
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

/**
 * Alpha types.
 */
typedef enum
{
	IMG_COLOURSPACE_FORMAT_UNKNOWN   =  0x00000000,  /**< Colourspace Format: Unknown */
	IMG_COLOURSPACE_FORMAT_LINEAR    =  0x00010000,  /**< Colourspace Format: Linear */
	IMG_COLOURSPACE_FORMAT_NONLINEAR =  0x00020000,  /**< Colourspace Format: Non-Linear */
	IMG_COLOURSPACE_FORMAT_MASK      =  0x000F0000,  /**< Colourspace Format Mask */
} IMG_COLOURSPACE_FORMAT;

/**
 * Types of framebuffer compression
 */
typedef enum
{
	IMG_FB_COMPRESSION_NONE,
	IMG_FB_COMPRESSION_DIRECT_8x8,
	IMG_FB_COMPRESSION_DIRECT_16x4,
	IMG_FB_COMPRESSION_DIRECT_32x2,
	IMG_FB_COMPRESSION_INDIRECT_8x8,
	IMG_FB_COMPRESSION_INDIRECT_16x4,
	IMG_FB_COMPRESSION_INDIRECT_4TILE_8x8,
	IMG_FB_COMPRESSION_INDIRECT_4TILE_16x4
} IMG_FB_COMPRESSION;


#endif /* _POWERVR_BUFFER_ATTRIBS_H_ */
