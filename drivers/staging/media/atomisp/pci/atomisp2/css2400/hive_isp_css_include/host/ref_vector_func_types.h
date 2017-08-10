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

#ifndef __REF_VECTOR_FUNC_TYPES_H_INCLUDED__
#define __REF_VECTOR_FUNC_TYPES_H_INCLUDED__


/*
 * Prerequisites:
 *
 */
#include "mpmath.h"
#include "bbb_config.h"
#include "isp_op1w_types.h"
#include "isp_op2w_types.h"

/* Defines for the Config Unit */
#define MAX_CONFIG_POINTS 5
#define INPUT_OFFSET_FACTOR 10
#define INPUT_SCALE_FACTOR 10
#define OUTPUT_SCALE_FACTOR 10
#define SLOPE_A_RESOLUTION 10
#define CONFIG_UNIT_LUT_SIZE_32 32 /*XCU works for ISP_NWAY = 32 */
#define LXCU_LUT_SIZE      16
#ifdef ISP2401
#define IMAX32_ELEM_SIZE   32
#endif

#define ONE_IN_Q14 (1<<(NUM_BITS-2))
#define Q29_TO_Q15_SHIFT_VAL (NUM_BITS-2)
#define Q28_TO_Q15_SHIFT_VAL (NUM_BITS-3)
#define MAX_ELEM(width_in_bits) ((1<<(width_in_bits))-1)

/* Block matching algorithm related data */
/* NUM_OF_SADS = ((SEARCH_AREA_HEIGHT - REF_BLOCK_HEIGHT)/PIXEL_SHIFT + 1)* \
					((SEARCH_AREA_WIDTH - REF_BLOCK_WIDTH)/PIXEL_SHIFT + 1) */

#define SADS(sw_h,sw_w, ref_h, ref_w, p_sh) (((sw_h - ref_h)/p_sh + 1)*((sw_w - ref_w)/p_sh + 1))
#define SADS_16x16_1	SADS(16, 16, 8, 8, 1)
#define SADS_16x16_2	SADS(16, 16, 8, 8, 2)
#define SADS_14x14_1	SADS(14, 14, 8, 8, 1)
#define SADS_14x14_2	SADS(14, 14, 8, 8, 2)

#define BMA_OUTPUT_MATRIX_DIM(sw_h, ref_h, p_sh)	((sw_h - ref_h)/p_sh + 1)
#define BMA_OUT_16x16_2_32		BMA_OUTPUT_MATRIX_DIM(16, 8, 2)
#define BMA_OUT_14x14_2_32		BMA_OUTPUT_MATRIX_DIM(14, 8, 2)
#define BMA_OUT_16x16_1_32 		BMA_OUTPUT_MATRIX_DIM(16, 8, 1)
#define BMA_OUT_14x14_1_32 		BMA_OUTPUT_MATRIX_DIM(14, 8, 1)
#define BMA_SEARCH_BLOCK_SZ_16 	16
#define BMA_REF_BLOCK_SZ_8    	8
#define PIXEL_SHIFT_2         	2
#define PIXEL_SHIFT_1         	1
#define BMA_SEARCH_WIN_SZ_16  	16
#define BMA_SEARCH_WIN_SZ_14  	14


/*
 * Struct type specification
 */

typedef unsigned short tscalar1w_3bit;       /* tscalar1w in interval [0, 2^3)                       */
typedef short tscalar1w_5bit_signed;         /* tscalar1w in interval [-2^(5-1), 2^(5-1))            */
typedef unsigned short tscalar1w_5bit;       /* tscalar1w in interval [0, 2^5)                       */
typedef short tscalar1w_range1wbit;          /* tscalar1w in interval [-NUM_BITS, NUM_BITS]          */
typedef short tscalar1w_unsigned_range1wbit; /* tscalar1w in interval [0, NUM_BITS]                  */
typedef unsigned short tvector_8bit;		/* 8 bit positive number */
typedef unsigned short tvector_5bit;
typedef unsigned short tvector_4bit;
typedef unsigned short tscalar1w_16bit;
typedef unsigned short tscalar1w_4bit_bma_shift;

typedef struct {
  tvector1w     v0  ;
  tvector1w     v1 ;
} s_1w_2x1_matrix;

#define S_1W_2X1_MATRIX_DEFAULT ((s_1w_2x1_matrix)\
	{ 0, 0 })

typedef struct {
	tvector1w v00;
	tvector1w v01;
} s_1w_1x2_matrix;

#define S_1W_1X2_MATRIX_DEFAULT ((s_1w_1x2_matrix)\
	{ 0, 0 })

typedef struct {
  tvector1w     v00  ;
  tvector1w     v01 ;
  tvector1w     v02 ;
} s_1w_1x3_matrix;

#define S_1W_1X3_MATRIX_DEFAULT ((s_1w_1x3_matrix)\
	{ 0, 0, 0, })

typedef struct {
  tvector1w v00; tvector1w v01; tvector1w v02;
  tvector1w v10; tvector1w v11; tvector1w v12;
} s_1w_2x3_matrix;

#define S_1W_2X3_MATRIX_DEFAULT ((s_1w_2x3_matrix)\
	{ 0, 0, 0, \
	  0, 0, 0 })

typedef struct {
  tvector1w     v00  ; tvector1w     v01 ; tvector1w     v02  ;
  tvector1w     v10  ; tvector1w     v11 ; tvector1w     v12  ;
  tvector1w     v20  ; tvector1w     v21 ; tvector1w     v22  ;
} s_1w_3x3_matrix;

#define S_1W_3X3_MATRIX_DEFAULT ((s_1w_3x3_matrix)\
	{ 0, 0, 0, \
	  0, 0, 0, \
	  0, 0, 0 })

typedef struct {
  tvector1w     v00  ; tvector1w     v01 ; tvector1w     v02  ;
  tvector1w     v10  ; tvector1w     v11 ; tvector1w     v12  ;
  tvector1w     v20  ; tvector1w     v21 ; tvector1w     v22  ;
  tvector1w     v30  ; tvector1w     v31 ; tvector1w     v32  ;
} s_1w_4x3_matrix;

#define S_1W_4X3_MATRIX_DEFAULT ((s_1w_4x3_matrix)\
	{ 0, 0, 0, \
	  0, 0, 0, \
	  0, 0, 0, \
	  0, 0, 0 })

typedef struct {
  tvector1w     v00 ;
  tvector1w     v01 ;
  tvector1w     v02 ;
  tvector1w     v03 ;
  tvector1w     v04 ;
} s_1w_1x5_matrix;

#define S_1W_1X5_MATRIX_DEFAULT ((s_1w_1x5_matrix)\
	{ 0, 0, 0, 0, 0 })

typedef struct {
  tvector1w     v00  ; tvector1w     v01 ; tvector1w     v02  ; tvector1w     v03 ; tvector1w     v04  ;
  tvector1w     v10  ; tvector1w     v11 ; tvector1w     v12  ; tvector1w     v13 ; tvector1w     v14  ;
  tvector1w     v20  ; tvector1w     v21 ; tvector1w     v22  ; tvector1w     v23 ; tvector1w     v24  ;
  tvector1w     v30  ; tvector1w     v31 ; tvector1w     v32  ; tvector1w     v33 ; tvector1w     v34  ;
  tvector1w     v40  ; tvector1w     v41 ; tvector1w     v42  ; tvector1w     v43 ; tvector1w     v44  ;
} s_1w_5x5_matrix;

#define S_1W_5X5_MATRIX_DEFAULT ((s_1w_5x5_matrix)\
	{ 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0 })
#ifndef ISP2401
	
#else

#endif
typedef struct {
	tvector1w v00;
	tvector1w v01;
	tvector1w v02;
	tvector1w v03;
	tvector1w v04;
	tvector1w v05;
	tvector1w v06;
} s_1w_1x7_matrix;

#define S_1W_1X7_MATRIX_DEFAULT ((s_1w_1x7_matrix)\
	{ 0, 0, 0, 0, 0, 0, 0 })

typedef struct {
	tvector1w v00;
	tvector1w v01;
	tvector1w v02;
	tvector1w v03;
	tvector1w v04;
	tvector1w v05;
	tvector1w v06;
	tvector1w v07;
	tvector1w v08;
} s_1w_1x9_matrix;

#define S_1W_1X9_MATRIX_DEFAULT ((s_1w_1x9_matrix)\
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0 })

typedef struct {
	tvector1w v00;
	tvector1w v01;
	tvector1w v02;
	tvector1w v03;
} s_1w_1x4_matrix;

#define S_1W_1X4_MATRIX ((s_1w_1x4_matrix)\
	{ 0, 0, 0, 0 })

typedef struct {
	tvector1w v00; tvector1w v01; tvector1w v02; tvector1w v03;
	tvector1w v10; tvector1w v11; tvector1w v12; tvector1w v13;
	tvector1w v20; tvector1w v21; tvector1w v22; tvector1w v23;
	tvector1w v30; tvector1w v31; tvector1w v32; tvector1w v33;
} s_1w_4x4_matrix;

#define S_1W_4X4_MATRIX_DEFAULT ((s_1w_4x4_matrix)\
	{ 0, 0, 0, 0, \
	  0, 0, 0, 0, \
	  0, 0, 0, 0, \
	  0, 0, 0, 0 })

typedef struct {
	tvector1w v00;
	tvector1w v01;
	tvector1w v02;
	tvector1w v03;
	tvector1w v04;
	tvector1w v05;
} s_1w_1x6_matrix;

#define S_1W_1X6_MATRIX_DEFAULT ((s_1w_1x6_matrix)\
	{ 0, 0, 0, 0, 0, 0 })

typedef struct {
	tvector1w v00; tvector1w v01; tvector1w v02; tvector1w v03; tvector1w v04; tvector1w v05;
	tvector1w v10; tvector1w v11; tvector1w v12; tvector1w v13; tvector1w v14; tvector1w v15;
	tvector1w v20; tvector1w v21; tvector1w v22; tvector1w v23; tvector1w v24; tvector1w v25;
	tvector1w v30; tvector1w v31; tvector1w v32; tvector1w v33; tvector1w v34; tvector1w v35;
	tvector1w v40; tvector1w v41; tvector1w v42; tvector1w v43; tvector1w v44; tvector1w v45;
	tvector1w v50; tvector1w v51; tvector1w v52; tvector1w v53; tvector1w v54; tvector1w v55;
} s_1w_6x6_matrix;

#define S_1W_6X6_MATRIX_DEFAULT ((s_1w_6x6_matrix)\
	{ 0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0 })

typedef struct {
	tvector1w v00; tvector1w v01; tvector1w v02; tvector1w v03; tvector1w v04;
	tvector1w v05; tvector1w v06; tvector1w v07; tvector1w v08;
	tvector1w v10; tvector1w v11; tvector1w v12; tvector1w v13; tvector1w v14;
	tvector1w v15; tvector1w v16; tvector1w v17; tvector1w v18;
	tvector1w v20; tvector1w v21; tvector1w v22; tvector1w v23; tvector1w v24;
	tvector1w v25; tvector1w v26; tvector1w v27; tvector1w v28;
	tvector1w v30; tvector1w v31; tvector1w v32; tvector1w v33; tvector1w v34;
	tvector1w v35; tvector1w v36; tvector1w v37; tvector1w v38;
	tvector1w v40; tvector1w v41; tvector1w v42; tvector1w v43; tvector1w v44;
	tvector1w v45; tvector1w v46; tvector1w v47; tvector1w v48;
	tvector1w v50; tvector1w v51; tvector1w v52; tvector1w v53; tvector1w v54;
	tvector1w v55; tvector1w v56; tvector1w v57; tvector1w v58;
	tvector1w v60; tvector1w v61; tvector1w v62; tvector1w v63; tvector1w v64;
	tvector1w v65; tvector1w v66; tvector1w v67; tvector1w v68;
	tvector1w v70; tvector1w v71; tvector1w v72; tvector1w v73; tvector1w v74;
	tvector1w v75; tvector1w v76; tvector1w v77; tvector1w v78;
	tvector1w v80; tvector1w v81; tvector1w v82; tvector1w v83; tvector1w v84;
	tvector1w v85; tvector1w v86; tvector1w v87; tvector1w v88;
} s_1w_9x9_matrix;

#define S_1W_9X9_MATRIX_DEFAULT ((s_1w_9x9_matrix)\
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0, 0, 0, 0 })

typedef struct {
	tvector1w v00; tvector1w v01; tvector1w v02; tvector1w v03; tvector1w v04;
	tvector1w v05; tvector1w v06;
	tvector1w v10; tvector1w v11; tvector1w v12; tvector1w v13; tvector1w v14;
	tvector1w v15; tvector1w v16;
	tvector1w v20; tvector1w v21; tvector1w v22; tvector1w v23; tvector1w v24;
	tvector1w v25; tvector1w v26;
	tvector1w v30; tvector1w v31; tvector1w v32; tvector1w v33; tvector1w v34;
	tvector1w v35; tvector1w v36;
	tvector1w v40; tvector1w v41; tvector1w v42; tvector1w v43; tvector1w v44;
	tvector1w v45; tvector1w v46;
	tvector1w v50; tvector1w v51; tvector1w v52; tvector1w v53; tvector1w v54;
	tvector1w v55; tvector1w v56;
	tvector1w v60; tvector1w v61; tvector1w v62; tvector1w v63; tvector1w v64;
	tvector1w v65; tvector1w v66;
} s_1w_7x7_matrix;

#define S_1W_7X7_MATRIX_DEFAULT ((s_1w_7x7_matrix)\
	{ 0, 0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0, 0, \
	  0, 0, 0, 0, 0, 0, 0 })

typedef struct {
	tvector1w v0_0;
	tvector1w v0_1;
	tvector1w v0_2;
	tvector1w v0_3;
	tvector1w v0_4;
	tvector1w v0_5;
	tvector1w v0_6;
	tvector1w v0_7;
	tvector1w v0_8;
	tvector1w v0_9;
	tvector1w v0_10;
} s_1w_1x11_matrix;

#define S_1W_1X11_MATRIX_DEFAULT ((s_1w_1x11_matrix)\
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0 })

typedef struct {
	tvector1w x_cord[MAX_CONFIG_POINTS];
	tvector1w slope[MAX_CONFIG_POINTS-1];
	tvector1w y_offset[MAX_CONFIG_POINTS-1];
} ref_config_points;

typedef struct {
	tscalar1w_range1wbit slope_vec[CONFIG_UNIT_LUT_SIZE_32];
	tscalar1w_range1wbit offset_vec[CONFIG_UNIT_LUT_SIZE_32];
	tscalar1w_16bit x_cord_vec[CONFIG_UNIT_LUT_SIZE_32];
	tscalar1w_16bit x_cord_max;
	tscalar1w_5bit exponent;
	tscalar1w_5bit slope_resolution;
} xcu_ref_init_vectors;

typedef struct {
#ifdef ISP2401
	tvector1w elem[IMAX32_ELEM_SIZE];
} imax32_ref_in_vector;

typedef struct {
#endif
	tscalar1w search[BMA_SEARCH_BLOCK_SZ_16][BMA_SEARCH_BLOCK_SZ_16];
} bma_16x16_search_window;

typedef struct {
	tscalar1w ref[BMA_REF_BLOCK_SZ_8][BMA_REF_BLOCK_SZ_8];
} ref_block_8x8;

typedef struct {
	tscalar1w sads[SADS_16x16_1];
} bma_output_16_1;

typedef struct {
	tscalar1w sads[SADS_16x16_2];
} bma_output_16_2;

typedef struct {
	tscalar1w sads[SADS_14x14_2];
} bma_output_14_2;

typedef struct {
	tscalar1w sads[SADS_14x14_1];
} bma_output_14_1;

typedef struct {
	tvector1w spatial_weight_lut[BFA_MAX_KWAY]; /* spatial weight LUT */
	/* range weight LUT, (BFA_RW_LUT_SIZE + 1) numbers of LUT values are compressed in BFA_RW_LUT_SIZE buffer.
	 * range_weight_lut[k] = packed(drop[k], range_weight[k])
	 * where, drop[k] = range_weight[k+1] - range_weight[k]
	 * pack(msb, lsb): two 8bits numbers packed in one 16bits number */
	tvector1w range_weight_lut[BFA_RW_LUT_SIZE];
} bfa_weights;

/* Return type for BFA BBBs */
typedef struct {
	tvector2w sop; /* weighted sum of pixels */
	tvector1w sow; /* sum of weights */
} bfa_7x7_output;
#endif /* __REF_VECTOR_FUNC_TYPES_H_INCLUDED__ */
