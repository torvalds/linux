/*************************************************************************/ /*!
@File
@Title          Global 3D types for use by IMG APIs
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Defines 3D types for use by IMG APIs
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#ifndef IMG_3DTYPES_H
#define IMG_3DTYPES_H

#include <powervr/buffer_attribs.h>
#include "img_types.h"
#include "img_defs.h"

/**
 * Comparison functions
 * This comparison function is defined as:
 * A {CmpFunc} B
 * A is a reference value, e.g., incoming depth etc.
 * B is the sample value, e.g., value in depth buffer.
 */
typedef enum _IMG_COMPFUNC_
{
	IMG_COMPFUNC_NEVER,			/**< The comparison never succeeds */
	IMG_COMPFUNC_LESS,			/**< The comparison is a less-than operation */
	IMG_COMPFUNC_EQUAL,			/**< The comparison is an equal-to operation */
	IMG_COMPFUNC_LESS_EQUAL,	/**< The comparison is a less-than or equal-to
									 operation */
	IMG_COMPFUNC_GREATER,		/**< The comparison is a greater-than operation
								*/
	IMG_COMPFUNC_NOT_EQUAL,		/**< The comparison is a no-equal-to operation
								*/
	IMG_COMPFUNC_GREATER_EQUAL,	/**< The comparison is a greater-than or
									 equal-to operation */
	IMG_COMPFUNC_ALWAYS,		/**< The comparison always succeeds */
} IMG_COMPFUNC;

/**
 * Stencil op functions
 */
typedef enum _IMG_STENCILOP_
{
	IMG_STENCILOP_KEEP,		/**< Keep original value */
	IMG_STENCILOP_ZERO,		/**< Set stencil to 0 */
	IMG_STENCILOP_REPLACE,	/**< Replace stencil entry */
	IMG_STENCILOP_INCR_SAT,	/**< Increment stencil entry, clamping to max */
	IMG_STENCILOP_DECR_SAT,	/**< Decrement stencil entry, clamping to zero */
	IMG_STENCILOP_INVERT,	/**< Invert bits in stencil entry */
	IMG_STENCILOP_INCR,		/**< Increment stencil entry,
								 wrapping if necessary */
	IMG_STENCILOP_DECR,		/**< Decrement stencil entry,
								 wrapping if necessary */
} IMG_STENCILOP;

/**
 * Alpha blending allows colours and textures on one surface
 * to be blended with transparency onto another surface.
 * These definitions apply to both source and destination blending
 * states
 */
typedef enum _IMG_BLEND_
{
	IMG_BLEND_ZERO = 0,        /**< Blend factor is (0,0,0,0) */
	IMG_BLEND_ONE,             /**< Blend factor is (1,1,1,1) */
	IMG_BLEND_SRC_COLOUR,      /**< Blend factor is the source colour */
	IMG_BLEND_INV_SRC_COLOUR,  /**< Blend factor is the inverted source colour
									(i.e. 1-src_col) */
	IMG_BLEND_SRC_ALPHA,       /**< Blend factor is the source alpha */
	IMG_BLEND_INV_SRC_ALPHA,   /**< Blend factor is the inverted source alpha
									(i.e. 1-src_alpha) */
	IMG_BLEND_DEST_ALPHA,      /**< Blend factor is the destination alpha */
	IMG_BLEND_INV_DEST_ALPHA,  /**< Blend factor is the inverted destination
									alpha */
	IMG_BLEND_DEST_COLOUR,     /**< Blend factor is the destination colour */
	IMG_BLEND_INV_DEST_COLOUR, /**< Blend factor is the inverted destination
									colour */
	IMG_BLEND_SRC_ALPHASAT,    /**< Blend factor is the alpha saturation (the
									minimum of (Src alpha,
									1 - destination alpha)) */
	IMG_BLEND_BLEND_FACTOR,    /**< Blend factor is a constant */
	IMG_BLEND_INVBLEND_FACTOR, /**< Blend factor is a constant (inverted)*/
	IMG_BLEND_SRC1_COLOUR,     /**< Blend factor is the colour outputted from
									the pixel shader */
	IMG_BLEND_INV_SRC1_COLOUR, /**< Blend factor is the inverted colour
									outputted from the pixel shader */
	IMG_BLEND_SRC1_ALPHA,      /**< Blend factor is the alpha outputted from
									the pixel shader */
	IMG_BLEND_INV_SRC1_ALPHA   /**< Blend factor is the inverted alpha
									outputted from the pixel shader */
} IMG_BLEND;

/**
 * The arithmetic operation to perform when blending
 */
typedef enum _IMG_BLENDOP_
{
	IMG_BLENDOP_ADD = 0,      /**< Result = (Source + Destination) */
	IMG_BLENDOP_SUBTRACT,     /**< Result = (Source - Destination) */
	IMG_BLENDOP_REV_SUBTRACT, /**< Result = (Destination - Source) */
	IMG_BLENDOP_MIN,          /**< Result = min (Source, Destination) */
	IMG_BLENDOP_MAX           /**< Result = max (Source, Destination) */
} IMG_BLENDOP;

/**
 * Logical operation to perform when logic ops are enabled
 */
typedef enum _IMG_LOGICOP_
{
	IMG_LOGICOP_CLEAR = 0,     /**< Result = 0 */
	IMG_LOGICOP_SET,           /**< Result = -1 */
	IMG_LOGICOP_COPY,          /**< Result = Source */
	IMG_LOGICOP_COPY_INVERTED, /**< Result = ~Source */
	IMG_LOGICOP_NOOP,          /**< Result = Destination */
	IMG_LOGICOP_INVERT,        /**< Result = ~Destination */
	IMG_LOGICOP_AND,           /**< Result = Source & Destination */
	IMG_LOGICOP_NAND,          /**< Result = ~(Source & Destination) */
	IMG_LOGICOP_OR,            /**< Result = Source | Destination */
	IMG_LOGICOP_NOR,           /**< Result = ~(Source | Destination) */
	IMG_LOGICOP_XOR,           /**< Result = Source ^ Destination */
	IMG_LOGICOP_EQUIV,         /**< Result = ~(Source ^ Destination) */
	IMG_LOGICOP_AND_REVERSE,   /**< Result = Source & ~Destination */
	IMG_LOGICOP_AND_INVERTED,  /**< Result = ~Source & Destination */
	IMG_LOGICOP_OR_REVERSE,    /**< Result = Source | ~Destination */
	IMG_LOGICOP_OR_INVERTED    /**< Result = ~Source | Destination */
} IMG_LOGICOP;

/**
 * Type of fog blending supported
 */
typedef enum _IMG_FOGMODE_
{
	IMG_FOGMODE_NONE, /**< No fog blending - fog calculations are
					   *   based on the value output from the vertex phase */
	IMG_FOGMODE_LINEAR, /**< Linear interpolation */
	IMG_FOGMODE_EXP, /**< Exponential */
	IMG_FOGMODE_EXP2, /**< Exponential squaring */
} IMG_FOGMODE;

/**
 * Types of filtering
 */
typedef enum _IMG_FILTER_
{
	IMG_FILTER_DONTCARE,	/**< Any filtering mode is acceptable */
	IMG_FILTER_POINT,		/**< Point filtering */
	IMG_FILTER_LINEAR,		/**< Bi-linear filtering */
	IMG_FILTER_BICUBIC,		/**< Bi-cubic filtering */
} IMG_FILTER;

/**
 * Addressing modes for textures
 */
typedef enum _IMG_ADDRESSMODE_
{
	IMG_ADDRESSMODE_REPEAT,	/**< Texture repeats continuously */
	IMG_ADDRESSMODE_FLIP, /**< Texture flips on odd integer part */
	IMG_ADDRESSMODE_CLAMP, /**< Texture clamped at 0 or 1 */
	IMG_ADDRESSMODE_FLIPCLAMP, /**< Flipped once, then clamp */
	IMG_ADDRESSMODE_CLAMPBORDER,
	IMG_ADDRESSMODE_OGL_CLAMP,
	IMG_ADDRESSMODE_OVG_TILEFILL,
	IMG_ADDRESSMODE_DONTCARE,
} IMG_ADDRESSMODE;

/**
 * Culling based on winding order of triangle.
 */
typedef enum _IMG_CULLMODE_
{
	IMG_CULLMODE_NONE,			/**< Don't cull */
	IMG_CULLMODE_FRONTFACING,	/**< Front facing triangles */
	IMG_CULLMODE_BACKFACING,	/**< Back facing triangles */
} IMG_CULLMODE;

/**
 * Colour for clearing surfaces.
 *  The four elements of the 4 x 32 bit array will map to colour
 *  R,G,B,A components, in order.
 *  For YUV colour space the order is Y,U,V.
 *  For Depth and Stencil formats D maps to R and S maps to G.
 */
typedef union IMG_CLEAR_COLOUR_TAG {
	IMG_UINT32        aui32[4];
	IMG_INT32         ai32[4];
	IMG_FLOAT         af32[4];
} IMG_CLEAR_COLOUR;

static_assert(sizeof(IMG_FLOAT) == sizeof(IMG_INT32), "Size of IMG_FLOAT is not 32 bits.");

/*! ************************************************************************//**
@brief          Specifies the MSAA resolve operation.
*/ /**************************************************************************/
typedef enum _IMG_RESOLVE_OP_
{
	IMG_RESOLVE_BLEND   = 0,          /*!< box filter on the samples */
	IMG_RESOLVE_MIN     = 1,          /*!< minimum of the samples */
	IMG_RESOLVE_MAX     = 2,          /*!< maximum of the samples */
	IMG_RESOLVE_SAMPLE0 = 3,          /*!< choose sample 0 */
	IMG_RESOLVE_SAMPLE1 = 4,          /*!< choose sample 1 */
	IMG_RESOLVE_SAMPLE2 = 5,          /*!< choose sample 2 */
	IMG_RESOLVE_SAMPLE3 = 6,          /*!< choose sample 3 */
	IMG_RESOLVE_SAMPLE4 = 7,          /*!< choose sample 4 */
	IMG_RESOLVE_SAMPLE5 = 8,          /*!< choose sample 5 */
	IMG_RESOLVE_SAMPLE6 = 9,          /*!< choose sample 6 */
	IMG_RESOLVE_SAMPLE7 = 10,         /*!< choose sample 7 */
} IMG_RESOLVE_OP;


#endif /* IMG_3DTYPES_H */
/******************************************************************************
 End of file (img_3dtypes.h)
******************************************************************************/
