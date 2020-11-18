/* SPDX-License-Identifier: GPL-2.0 */
/**
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/

#ifndef _COMMON_ISP_CONST_H_
#define _COMMON_ISP_CONST_H_

/*#include "isp.h"*/	/* ISP_VEC_NELEMS */

/* Binary independent constants */

#ifndef NO_HOIST
#  define		NO_HOIST	HIVE_ATTRIBUTE((no_hoist))
#endif

#define NO_HOIST_CSE HIVE_ATTRIBUTE((no_hoist, no_cse))

#define UNION struct /* Union constructors not allowed in C++ */

#define XMEM_WIDTH_BITS              HIVE_ISP_DDR_WORD_BITS
#define XMEM_SHORTS_PER_WORD         (HIVE_ISP_DDR_WORD_BITS / 16)
#define XMEM_INTS_PER_WORD           (HIVE_ISP_DDR_WORD_BITS / 32)
#define XMEM_POW2_BYTES_PER_WORD      HIVE_ISP_DDR_WORD_BYTES

#define BITS8_ELEMENTS_PER_XMEM_ADDR    CEIL_DIV(XMEM_WIDTH_BITS, 8)
#define BITS16_ELEMENTS_PER_XMEM_ADDR    CEIL_DIV(XMEM_WIDTH_BITS, 16)

#if ISP_VEC_NELEMS == 64
#define ISP_NWAY_LOG2  6
#elif ISP_VEC_NELEMS == 32
#define ISP_NWAY_LOG2  5
#elif ISP_VEC_NELEMS == 16
#define ISP_NWAY_LOG2  4
#elif ISP_VEC_NELEMS == 8
#define ISP_NWAY_LOG2  3
#else
#error "isp_const.h ISP_VEC_NELEMS must be one of {8, 16, 32, 64}"
#endif

/* *****************************
 * ISP input/output buffer sizes
 * ****************************/
/* input image */
#define INPUT_BUF_DMA_HEIGHT          2
#define INPUT_BUF_HEIGHT              2 /* double buffer */
#define OUTPUT_BUF_DMA_HEIGHT         2
#define OUTPUT_BUF_HEIGHT             2 /* double buffer */
#define OUTPUT_NUM_TRANSFERS	      4

/* GDC accelerator: Up/Down Scaling */
/* These should be moved to the gdc_defs.h in the device */
#define UDS_SCALING_N                 HRT_GDC_N
/* AB: This should cover the zooming up to 16MP */
#define UDS_MAX_OXDIM                 5000
/* We support maximally 2 planes with different parameters
       - luma and chroma (YUV420) */
#define UDS_MAX_PLANES                2
#define UDS_BLI_BLOCK_HEIGHT          2
#define UDS_BCI_BLOCK_HEIGHT          4
#define UDS_BLI_INTERP_ENVELOPE       1
#define UDS_BCI_INTERP_ENVELOPE       3
#define UDS_MAX_ZOOM_FAC              64
/* Make it always one FPGA vector.
   Four FPGA vectors are required and
   four of them fit in one ASIC vector.*/
#define UDS_MAX_CHUNKS                16

#define ISP_LEFT_PADDING	_ISP_LEFT_CROP_EXTRA(ISP_LEFT_CROPPING)
#define ISP_LEFT_PADDING_VECS	CEIL_DIV(ISP_LEFT_PADDING, ISP_VEC_NELEMS)
/* in case of continuous the croppong of the current binary doesn't matter for the buffer calculation, but the cropping of the sp copy should be used */
#define ISP_LEFT_PADDING_CONT	_ISP_LEFT_CROP_EXTRA(SH_CSS_MAX_LEFT_CROPPING)
#define ISP_LEFT_PADDING_VECS_CONT	CEIL_DIV(ISP_LEFT_PADDING_CONT, ISP_VEC_NELEMS)

#define CEIL_ROUND_DIV_STRIPE(width, stripe, padding) \
	CEIL_MUL(padding + CEIL_DIV(width - padding, stripe), ((ENABLE_RAW_BINNING || ENABLE_FIXED_BAYER_DS) ? 4 : 2))

/* output (Y,U,V) image, 4:2:0 */
#define MAX_VECTORS_PER_LINE \
	CEIL_ROUND_DIV_STRIPE(CEIL_DIV(ISP_MAX_INTERNAL_WIDTH, ISP_VEC_NELEMS), \
			      ISP_NUM_STRIPES, \
			      ISP_LEFT_PADDING_VECS)

/*
 * ITERATOR_VECTOR_INCREMENT' explanation:
 * when striping an even number of iterations, one of the stripes is
 * one iteration wider than the other to account for overlap
 * so the calc for the output buffer vmem size is:
 * ((width[vectors]/num_of_stripes) + 2[vectors])
 */
#define MAX_VECTORS_PER_OUTPUT_LINE \
	CEIL_DIV(CEIL_DIV(ISP_MAX_OUTPUT_WIDTH, ISP_NUM_STRIPES) + ISP_LEFT_PADDING, ISP_VEC_NELEMS)

/* Must be even due to interlaced bayer input */
#define MAX_VECTORS_PER_INPUT_LINE	CEIL_MUL((CEIL_DIV(ISP_MAX_INPUT_WIDTH, ISP_VEC_NELEMS) + ISP_LEFT_PADDING_VECS), 2)
#define MAX_VECTORS_PER_INPUT_STRIPE	CEIL_ROUND_DIV_STRIPE(MAX_VECTORS_PER_INPUT_LINE, \
							      ISP_NUM_STRIPES, \
							      ISP_LEFT_PADDING_VECS)

/* Add 2 for left croppping */
#define MAX_SP_RAW_COPY_VECTORS_PER_INPUT_LINE	(CEIL_DIV(ISP_MAX_INPUT_WIDTH, ISP_VEC_NELEMS) + 2)

#define MAX_VECTORS_PER_BUF_LINE \
	(MAX_VECTORS_PER_LINE + DUMMY_BUF_VECTORS)
#define MAX_VECTORS_PER_BUF_INPUT_LINE \
	(MAX_VECTORS_PER_INPUT_STRIPE + DUMMY_BUF_VECTORS)
#define MAX_OUTPUT_Y_FRAME_WIDTH \
	(MAX_VECTORS_PER_LINE * ISP_VEC_NELEMS)
#define MAX_OUTPUT_Y_FRAME_SIMDWIDTH \
	MAX_VECTORS_PER_LINE
#define MAX_OUTPUT_C_FRAME_WIDTH \
	(MAX_OUTPUT_Y_FRAME_WIDTH / 2)
#define MAX_OUTPUT_C_FRAME_SIMDWIDTH \
	CEIL_DIV(MAX_OUTPUT_C_FRAME_WIDTH, ISP_VEC_NELEMS)

/* should be even */
#define NO_CHUNKING (OUTPUT_NUM_CHUNKS == 1)

#define MAX_VECTORS_PER_CHUNK \
	(NO_CHUNKING ? MAX_VECTORS_PER_LINE \
				: 2 * CEIL_DIV(MAX_VECTORS_PER_LINE, \
					     2 * OUTPUT_NUM_CHUNKS))

#define MAX_C_VECTORS_PER_CHUNK \
	(MAX_VECTORS_PER_CHUNK / 2)

/* should be even */
#define MAX_VECTORS_PER_OUTPUT_CHUNK \
	(NO_CHUNKING ? MAX_VECTORS_PER_OUTPUT_LINE \
				: 2 * CEIL_DIV(MAX_VECTORS_PER_OUTPUT_LINE, \
					     2 * OUTPUT_NUM_CHUNKS))

#define MAX_C_VECTORS_PER_OUTPUT_CHUNK \
	(MAX_VECTORS_PER_OUTPUT_CHUNK / 2)

/* should be even */
#define MAX_VECTORS_PER_INPUT_CHUNK \
	(INPUT_NUM_CHUNKS == 1 ? MAX_VECTORS_PER_INPUT_STRIPE \
			       : 2 * CEIL_DIV(MAX_VECTORS_PER_INPUT_STRIPE, \
					    2 * OUTPUT_NUM_CHUNKS))

#define DEFAULT_C_SUBSAMPLING      2

/****** DMA buffer properties */

#define RAW_BUF_LINES ((ENABLE_RAW_BINNING || ENABLE_FIXED_BAYER_DS) ? 4 : 2)

#define RAW_BUF_STRIDE \
	(BINARY_ID == SH_CSS_BINARY_ID_POST_ISP ? MAX_VECTORS_PER_INPUT_CHUNK : \
	 ISP_NUM_STRIPES > 1 ? MAX_VECTORS_PER_INPUT_STRIPE + _ISP_EXTRA_PADDING_VECS : \
	 !ENABLE_CONTINUOUS ? MAX_VECTORS_PER_INPUT_LINE : \
	 MAX_VECTORS_PER_INPUT_CHUNK)

/* [isp vmem] table size[vectors] per line per color (GR,R,B,GB),
   multiples of NWAY */
#define ISP2400_SCTBL_VECTORS_PER_LINE_PER_COLOR \
	CEIL_DIV(ISP2400_SH_CSS_MAX_SCTBL_WIDTH_PER_COLOR, ISP_VEC_NELEMS)
#define ISP2401_SCTBL_VECTORS_PER_LINE_PER_COLOR \
	CEIL_DIV(ISP2401_SH_CSS_MAX_SCTBL_WIDTH_PER_COLOR, ISP_VEC_NELEMS)
/* [isp vmem] table size[vectors] per line for 4colors (GR,R,B,GB),
   multiples of NWAY */
#define SCTBL_VECTORS_PER_LINE \
	(SCTBL_VECTORS_PER_LINE_PER_COLOR * IA_CSS_SC_NUM_COLORS)

/*************/

/* Format for fixed primaries */

#define ISP_FIXED_PRIMARY_FORMAT IA_CSS_FRAME_FORMAT_NV12

#endif /* _COMMON_ISP_CONST_H_ */
