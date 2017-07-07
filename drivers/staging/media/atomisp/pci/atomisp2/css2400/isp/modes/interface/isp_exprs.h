#ifndef ISP2401
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#else
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
#endif

#ifndef _COMMON_ISP_EXPRS_H_
#define _COMMON_ISP_EXPRS_H_

/* Binary independent pre-processor expressions */

#include "sh_css_defs.h"
#include "isp_const.h"

#ifdef __HOST
#error "isp_exprs.h: Do not include on HOST, contains ISP specific defines"
#endif

#ifndef __ISP
#if defined(MODE)
#define MODE aap
#error "isp_exprs.h: is mode independent, but MODE is set"
#endif
#if defined(VARIABLE_RESOLUTION)
#define VARIABLE_RESOLUTION noot
#error "isp_exprs.h: is mode independent, but VARIABLE_RESOLUTION is set"
#endif
#if defined(DECI_FACTOR_LOG2)
#define DECI_FACTOR_LOG2 mies
#error "isp_exprs.h: is mode independent, but DECI_FACTOR_LOG2 is set"
#endif
#endif

#define LOG_VECTOR_STEP        _ISP_LOG_VECTOR_STEP(MODE)
/* should be even and multiple of vf downscaling */
#define ISP_OUTPUT_CHUNK_LOG_FACTOR (MAX_VF_LOG_DOWNSCALE<=1 ? LOG_VECTOR_STEP : \
					umax(VF_LOG_DOWNSCALE, LOG_VECTOR_STEP))

#define CEIL_DIV_CHUNKS(n,c)    ((c) == 1 ? (n) \
		  		          : CEIL_SHIFT(CEIL_DIV((n), (c)), ISP_OUTPUT_CHUNK_LOG_FACTOR)<<ISP_OUTPUT_CHUNK_LOG_FACTOR)


#define ISP_VARIABLE_INPUT     (ISP_INPUT == IA_CSS_BINARY_INPUT_VARIABLE)

/* Binary independent versions, see isp_defs.h for binary dependent ones */
#ifndef __ISP 
#define IMAGEFORMAT_IS_RAW(fmt)			((fmt) == IA_CSS_FRAME_FORMAT_RAW)

#define IMAGEFORMAT_IS_RAW_INTERLEAVED(fmt) 	((fmt) == IA_CSS_FRAME_FORMAT_RAW)

#define IMAGEFORMAT_IS_RGB(fmt) 		((fmt) == IA_CSS_FRAME_FORMAT_RGBA888 || (fmt) == IA_CSS_FRAME_FORMAT_PLANAR_RGB888 || \
						 (fmt) == IA_CSS_FRAME_FORMAT_RGB565)

#define IMAGEFORMAT_IS_RGB_INTERLEAVED(fmt) 	((fmt) == IA_CSS_FRAME_FORMAT_RGBA888 || (fmt) == IA_CSS_FRAME_FORMAT_RGB565)

#define IMAGEFORMAT_UV_INTERLEAVED(fmt) 	((fmt) == IA_CSS_FRAME_FORMAT_NV11    || \
						 (fmt) == IA_CSS_FRAME_FORMAT_NV12    || (fmt) == IA_CSS_FRAME_FORMAT_NV21 || \
						 (fmt) == IA_CSS_FRAME_FORMAT_NV16    || (fmt) == IA_CSS_FRAME_FORMAT_NV61 || \
						 (fmt) == IA_CSS_FRAME_FORMAT_UYVY    || (fmt) == IA_CSS_FRAME_FORMAT_YUYV || \
						 (fmt) == IA_CSS_FRAME_FORMAT_NV12_16 || (fmt) == IA_CSS_FRAME_FORMAT_NV12_TILEY)

#define IMAGEFORMAT_YUV_INTERLEAVED(fmt)	((fmt) == IA_CSS_FRAME_FORMAT_UYVY    || (fmt) == IA_CSS_FRAME_FORMAT_YUYV)

#define IMAGEFORMAT_INTERLEAVED(fmt)		(IMAGEFORMAT_UV_INTERLEAVED(fmt) || IMAGEFORMAT_IS_RGB_INTERLEAVED(fmt))

#define IMAGEFORMAT_SUB_SAMPL_420(fmt)		((fmt) == IA_CSS_FRAME_FORMAT_YUV420 || (fmt) == IA_CSS_FRAME_FORMAT_YV12 || \
						 (fmt) == IA_CSS_FRAME_FORMAT_NV12   || (fmt) == IA_CSS_FRAME_FORMAT_NV21 || \
						 (fmt) == IA_CSS_FRAME_FORMAT_NV12_16 || (fmt) == IA_CSS_FRAME_FORMAT_NV12TILEY)

#define IMAGEFORMAT_SUB_SAMPL_422(fmt)		((fmt) == IA_CSS_FRAME_FORMAT_YUV422 || (fmt) == IA_CSS_FRAME_FORMAT_YV16 || \
						 (fmt) == IA_CSS_FRAME_FORMAT_NV16   || (fmt) == IA_CSS_FRAME_FORMAT_NV61)

#define IMAGEFORMAT_SUB_SAMPL_444(fmt) 		((fmt) == IA_CSS_FRAME_FORMAT_YUV444)

#define IMAGEFORMAT_UV_SWAPPED(fmt)		((fmt) == IA_CSS_FRAME_FORMAT_NV21 || (fmt) == IA_CSS_FRAME_FORMAT_NV61)

#define IMAGEFORMAT_IS_RGBA(fmt)		((fmt) == IA_CSS_FRAME_FORMAT_RGBA888)

#define IMAGEFORMAT_IS_NV11(fmt)		((fmt) == IA_CSS_FRAME_FORMAT_NV11)

#define IMAGEFORMAT_IS_16BIT(fmt)               ((fmt) == IA_CSS_FRAME_FORMAT_YUV420_16 || (fmt) == IA_CSS_FRAME_FORMAT_NV12_16 || (fmt) == IA_CSS_FRAME_FORMAT_YUV422_16)

#endif


/******** GDCAC settings *******/
#define GDCAC_BPP			ISP_VEC_ELEMBITS  /* We use 14 bits per pixel component for the GDCAC mode */
#define GDC_INPUT_BLOCK_WIDTH		2 /* Two vectors are needed */
#define GDC_OUTPUT_BLOCK_WIDTH		1 /* One vector is produced */

#if ISP_VEC_NELEMS == 16
/* For 16*16 output block, the distortion fits in 13.312 lines __ALWAYS__ */
#define GDC_INPUT_BLOCK_HEIGHT		14
#elif ISP_VEC_NELEMS == 64
/* For 64*64 output block, the distortion fits in 47.    lines __ALWAYS__ */
#define GDC_INPUT_BLOCK_HEIGHT		48
#endif
/*******************************/


#define ENABLE_HUP ((isp_input_width  - isp_envelope_width)  < isp_output_width)
#define ENABLE_VUP ((isp_input_height - isp_envelope_height) < isp_output_height)

#define ISP_INPUT_WIDTH  (ENABLE_DS | ENABLE_HUP ? isp_input_width  : ISP_INTERNAL_WIDTH)
#define ISP_INPUT_HEIGHT (ENABLE_DS | ENABLE_VUP ? isp_input_height : isp_internal_height)

#define DECI_FACTOR_LOG2 (ISP_FIXED_S3A_DECI_LOG ? ISP_FIXED_S3A_DECI_LOG : isp_deci_log_factor)

#define ISP_S3ATBL_WIDTH \
  _ISP_S3ATBL_ISP_WIDTH(_ISP_S3A_ELEMS_ISP_WIDTH((ENABLE_HUP ? ISP_INTERNAL_WIDTH : ISP_INPUT_WIDTH), ISP_LEFT_CROPPING), \
    DECI_FACTOR_LOG2)
#define S3ATBL_WIDTH_BYTES   (sizeof(struct ia_css_3a_output) * ISP_S3ATBL_WIDTH)
#define S3ATBL_WIDTH_SHORTS  (S3ATBL_WIDTH_BYTES / sizeof(short))

/* should be even?? */
#define ISP_UV_OUTPUT_CHUNK_VECS   	CEIL_DIV(ISP_OUTPUT_CHUNK_VECS, 2)


#if defined(__ISP) || defined(INIT_VARS)

#define ISP_USE_IF	(ISP_INPUT == IA_CSS_BINARY_INPUT_MEMORY ? 0 : \
	       	         ISP_INPUT == IA_CSS_BINARY_INPUT_SENSOR ? 1 : \
	                 isp_online)

#define ISP_DVS_ENVELOPE_WIDTH  0
#define ISP_DVS_ENVELOPE_HEIGHT 0

#define _ISP_INPUT_WIDTH_VECS	_ISP_VECS(ISP_INPUT_WIDTH)

#if !defined(__ISP) || (VARIABLE_RESOLUTION && !__HOST)
#define ISP_INPUT_WIDTH_VECS	isp_vectors_per_input_line
#else
#define ISP_INPUT_WIDTH_VECS	_ISP_INPUT_WIDTH_VECS
#endif

#if !defined(__ISP) || VARIABLE_RESOLUTION
#define ISP_INTERNAL_WIDTH_VECS		isp_vectors_per_line
#else
#define ISP_INTERNAL_WIDTH_VECS		_ISP_INTERNAL_WIDTH_VECS
#endif

#define _ISP_INTERNAL_HEIGHT	__ISP_INTERNAL_HEIGHT(isp_output_height, ISP_TOP_CROPPING, ISP_DVS_ENVELOPE_HEIGHT)

#define ISP_INTERNAL_HEIGHT	isp_internal_height

#define _ISP_INTERNAL_WIDTH	__ISP_INTERNAL_WIDTH(ISP_OUTPUT_WIDTH, ISP_DVS_ENVELOPE_WIDTH, \
			     			     ISP_LEFT_CROPPING, MODE, ISP_C_SUBSAMPLING, \
						     OUTPUT_NUM_CHUNKS, ISP_PIPELINING)

#define ISP_UV_INTERNAL_WIDTH	(ISP_INTERNAL_WIDTH / 2)
#define ISP_UV_INTERNAL_HEIGHT	(ISP_INTERNAL_HEIGHT / 2)

#define _ISP_INTERNAL_WIDTH_VECS	(_ISP_INTERNAL_WIDTH / ISP_VEC_NELEMS)
#define _ISP_UV_INTERNAL_WIDTH_VECS	CEIL_DIV(ISP_UV_INTERNAL_WIDTH, ISP_VEC_NELEMS)

#define ISP_VF_OUTPUT_WIDTH		_ISP_VF_OUTPUT_WIDTH(ISP_VF_OUTPUT_WIDTH_VECS)
#define ISP_VF_OUTPUT_HEIGHT		_ISP_VF_OUTPUT_HEIGHT(isp_output_height, VF_LOG_DOWNSCALE)

#if defined (__ISP) && !VARIABLE_RESOLUTION
#define ISP_INTERNAL_WIDTH         _ISP_INTERNAL_WIDTH
#define ISP_VF_OUTPUT_WIDTH_VECS   _ISP_VF_OUTPUT_WIDTH_VECS
#else
#define ISP_INTERNAL_WIDTH         (VARIABLE_RESOLUTION ? isp_internal_width : _ISP_INTERNAL_WIDTH)
#define ISP_VF_OUTPUT_WIDTH_VECS   (VARIABLE_RESOLUTION ? isp_vf_output_width_vecs : _ISP_VF_OUTPUT_WIDTH_VECS)
#endif

#if defined(__ISP) && !VARIABLE_RESOLUTION
#define ISP_OUTPUT_WIDTH        ISP_MAX_OUTPUT_WIDTH
#define VF_LOG_DOWNSCALE        MAX_VF_LOG_DOWNSCALE
#else
#define ISP_OUTPUT_WIDTH        isp_output_width
#define VF_LOG_DOWNSCALE        isp_vf_downscale_bits
#endif

#if !defined(__ISP) || VARIABLE_RESOLUTION
#define _ISP_MAX_VF_OUTPUT_WIDTH	__ISP_MAX_VF_OUTPUT_WIDTH(2*SH_CSS_MAX_VF_WIDTH, ISP_LEFT_CROPPING)
#elif defined(MODE) && MODE == IA_CSS_BINARY_MODE_PRIMARY && ISP_OUTPUT_WIDTH > 3328
/* Because of vmem issues, should be fixed later */
#define _ISP_MAX_VF_OUTPUT_WIDTH	(SH_CSS_MAX_VF_WIDTH - 2*ISP_VEC_NELEMS + (ISP_LEFT_CROPPING ? 2 * ISP_VEC_NELEMS : 0))
#else
#define _ISP_MAX_VF_OUTPUT_WIDTH	(ISP_VF_OUTPUT_WIDTH + (ISP_LEFT_CROPPING ? (2 >> VF_LOG_DOWNSCALE) * ISP_VEC_NELEMS : 0))
#endif

#define ISP_MAX_VF_OUTPUT_VECS 		CEIL_DIV(_ISP_MAX_VF_OUTPUT_WIDTH, ISP_VEC_NELEMS)



#define ISP_MIN_STRIPE_WIDTH (ISP_PIPELINING * (1<<_ISP_LOG_VECTOR_STEP(MODE)))

/******* STRIPING-RELATED MACROS *******/
#define NO_STRIPING (ISP_NUM_STRIPES == 1)

#define ISP_OUTPUT_CHUNK_VECS \
	(NO_STRIPING 	? CEIL_DIV_CHUNKS(ISP_OUTPUT_VECS_EXTRA_CROP, OUTPUT_NUM_CHUNKS) \
				: ISP_IO_STRIPE_WIDTH_VECS(ISP_OUTPUT_VECS_EXTRA_CROP, ISP_LEFT_PADDING_VECS, ISP_NUM_STRIPES, ISP_MIN_STRIPE_WIDTH) )

#define VECTORS_PER_LINE \
	(NO_STRIPING 	? ISP_INTERNAL_WIDTH_VECS \
				: ISP_IO_STRIPE_WIDTH_VECS(ISP_INTERNAL_WIDTH_VECS, ISP_LEFT_PADDING_VECS, ISP_NUM_STRIPES, ISP_MIN_STRIPE_WIDTH) )

#define VECTORS_PER_INPUT_LINE \
	(NO_STRIPING 	? ISP_INPUT_WIDTH_VECS \
				: ISP_IO_STRIPE_WIDTH_VECS(ISP_INPUT_WIDTH_VECS, ISP_LEFT_PADDING_VECS, ISP_NUM_STRIPES, ISP_MIN_STRIPE_WIDTH)+_ISP_EXTRA_PADDING_VECS)


#define ISP_MAX_VF_OUTPUT_STRIPE_VECS \
	(NO_STRIPING 	? ISP_MAX_VF_OUTPUT_VECS \
				: CEIL_MUL(CEIL_DIV(ISP_MAX_VF_OUTPUT_VECS, ISP_NUM_STRIPES), 2))
#define _ISP_VF_OUTPUT_WIDTH_VECS \
	(NO_STRIPING 	? __ISP_VF_OUTPUT_WIDTH_VECS(ISP_OUTPUT_WIDTH, VF_LOG_DOWNSCALE) \
				: __ISP_VF_OUTPUT_WIDTH_VECS(CEIL_DIV(ISP_OUTPUT_WIDTH, ISP_NUM_STRIPES), VF_LOG_DOWNSCALE))

#define ISP_IO_STRIPE_WIDTH_VECS(width, padding, num_stripes, min_stripe) \
	MAX(CEIL_MUL(padding + CEIL_DIV(width-padding, num_stripes) \
		   , 2) \
	  , min_stripe)
////////// INPUT & INTERNAL
/* should be even */
#define INPUT_NUM_CHUNKS	OUTPUT_NUM_CHUNKS

#define INPUT_VECTORS_PER_CHUNK	CEIL_DIV_CHUNKS(VECTORS_PER_INPUT_LINE, INPUT_NUM_CHUNKS)

/* only for ISP code, will be removed: */
#define VECTORS_PER_FULL_LINE         	ISP_INTERNAL_WIDTH_VECS
#define VECTORS_PER_INPUT_FULL_LINE   	ISP_INPUT_WIDTH_VECS

////////// OUTPUT
/* should at least even and also multiple of vf scaling */
#define ISP_OUTPUT_VECS_EXTRA_CROP	CEIL_DIV(ISP_OUTPUT_WIDTH_EXTRA_CROP, ISP_VEC_NELEMS)

/* Output is decoupled from input */
#define ISP_OUTPUT_WIDTH_EXTRA_CROP	CEIL_MUL(CEIL_MUL((ENABLE_DVS_ENVELOPE ? ISP_OUTPUT_WIDTH : ISP_INTERNAL_WIDTH), 2*ISP_VEC_NELEMS), \
		 				ISP_C_SUBSAMPLING * OUTPUT_NUM_CHUNKS *  HIVE_ISP_DDR_WORD_BYTES)

#define ISP_MAX_VF_OUTPUT_CHUNK_VECS \
        (NO_CHUNKING ? ISP_MAX_VF_OUTPUT_STRIPE_VECS \
                                : 2*CEIL_DIV(ISP_MAX_VF_OUTPUT_STRIPE_VECS, 2*OUTPUT_NUM_CHUNKS))

#define OUTPUT_VECTORS_PER_CHUNK	CEIL_DIV_CHUNKS(VECTORS_PER_LINE,OUTPUT_NUM_CHUNKS)

/* should be even?? */
#define OUTPUT_C_VECTORS_PER_CHUNK  	CEIL_DIV(OUTPUT_VECTORS_PER_CHUNK, 2)

#ifndef ISP2401
/**** SCTBL defs *******/
#define ISP_SCTBL_HEIGHT \
	_ISP_SCTBL_HEIGHT(ISP_INPUT_HEIGHT, DECI_FACTOR_LOG2)

#endif
/**** UDS defs *********/
#define UDS_DMACH_STRIDE_B_IN_Y           (( ISP_INTERNAL_WIDTH   /BITS8_ELEMENTS_PER_XMEM_ADDR)*HIVE_ISP_DDR_WORD_BYTES)
#define UDS_DMACH_STRIDE_B_IN_C           (((ISP_INTERNAL_WIDTH/2)/BITS8_ELEMENTS_PER_XMEM_ADDR)*HIVE_ISP_DDR_WORD_BYTES)

#else /* defined(__ISP) || defined(INIT_VARS) */

#define ISP_INTERNAL_WIDTH         isp_internal_width
#define ISP_INTERNAL_HEIGHT        isp_internal_height

#endif /* defined(__ISP) || defined(INIT_VARS) */

#endif /* _COMMON_ISP_EXPRS_H_ */

