/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef _SH_CSS_PARAMS_DVS_H_
#define _SH_CSS_PARAMS_DVS_H_

#include <math_support.h>
#include <ia_css_types.h>
#include "gdc_global.h" /* gdc_warp_param_mem_t */

#define DVS_ENV_MIN_X (12)
#define DVS_ENV_MIN_Y (12)

#define DVS_BLOCKDIM_X (64)        /* X block height*/
#define DVS_BLOCKDIM_Y_LUMA (64)   /* Y block height*/
#define DVS_BLOCKDIM_Y_CHROMA (32) /* UV height block size is half the Y block height*/

/* ISP2400 */
/* horizontal 64x64 blocks round up to DVS_BLOCKDIM_X, make even */
#define DVS_NUM_BLOCKS_X(X)		(CEIL_MUL(CEIL_DIV((X), DVS_BLOCKDIM_X), 2))

/* ISP2400 */
/* vertical   64x64 blocks round up to DVS_BLOCKDIM_Y */
#define DVS_NUM_BLOCKS_Y(X)		(CEIL_DIV((X), DVS_BLOCKDIM_Y_LUMA))
#define DVS_NUM_BLOCKS_X_CHROMA(X)	(CEIL_DIV((X), DVS_BLOCKDIM_X))
#define DVS_NUM_BLOCKS_Y_CHROMA(X)	(CEIL_DIV((X), DVS_BLOCKDIM_Y_CHROMA))

#define DVS_TABLE_IN_BLOCKDIM_X_LUMA(X)	(DVS_NUM_BLOCKS_X(X) + 1)  /* N blocks have N + 1 set of coords */
#define DVS_TABLE_IN_BLOCKDIM_X_CHROMA(X)   (DVS_NUM_BLOCKS_X_CHROMA(X) + 1)
#define DVS_TABLE_IN_BLOCKDIM_Y_LUMA(X)		(DVS_NUM_BLOCKS_Y(X) + 1)
#define DVS_TABLE_IN_BLOCKDIM_Y_CHROMA(X)	(DVS_NUM_BLOCKS_Y_CHROMA(X) + 1)

#define DVS_ENVELOPE_X(X) (((X) == 0) ? (DVS_ENV_MIN_X) : (X))
#define DVS_ENVELOPE_Y(X) (((X) == 0) ? (DVS_ENV_MIN_Y) : (X))

#define DVS_COORD_FRAC_BITS (10)

/* ISP2400 */
#define DVS_INPUT_BYTES_PER_PIXEL (1)

#define XMEM_ALIGN_LOG2 (5)

#define DVS_6AXIS_COORDS_ELEMS CEIL_MUL(sizeof(gdc_warp_param_mem_t) \
					, HIVE_ISP_DDR_WORD_BYTES)

/* currently we only support two output with the same resolution, output 0 is th default one. */
#define DVS_6AXIS_BYTES(binary) \
	(DVS_6AXIS_COORDS_ELEMS \
	* DVS_NUM_BLOCKS_X((binary)->out_frame_info[0].res.width) \
	* DVS_NUM_BLOCKS_Y((binary)->out_frame_info[0].res.height))

/*
 * ISP2400:
 * Bilinear interpolation (HRT_GDC_BLI_MODE) is the supported method currently.
 * Bicubic interpolation (HRT_GDC_BCI_MODE) is not supported yet */
#define DVS_GDC_INTERP_METHOD HRT_GDC_BLI_MODE

struct ia_css_dvs_6axis_config *
generate_dvs_6axis_table(const struct ia_css_resolution	*frame_res,
			 const struct ia_css_resolution *dvs_offset);

struct ia_css_dvs_6axis_config *
generate_dvs_6axis_table_from_config(struct ia_css_dvs_6axis_config
				     *dvs_config_src);

void
free_dvs_6axis_table(struct ia_css_dvs_6axis_config  **dvs_6axis_config);

void
copy_dvs_6axis_table(struct ia_css_dvs_6axis_config *dvs_config_dst,
		     const struct ia_css_dvs_6axis_config *dvs_config_src);

#endif
