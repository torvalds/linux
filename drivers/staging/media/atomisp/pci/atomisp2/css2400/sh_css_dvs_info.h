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

#ifndef __SH_CSS_DVS_INFO_H__
#define __SH_CSS_DVS_INFO_H__

#include <math_support.h>

/* horizontal 64x64 blocks round up to DVS_BLOCKDIM_X, make even */
#define DVS_NUM_BLOCKS_X(X)		(CEIL_MUL(CEIL_DIV((X), DVS_BLOCKDIM_X), 2))

/* vertical   64x64 blocks round up to DVS_BLOCKDIM_Y */
#define DVS_NUM_BLOCKS_Y(X)		(CEIL_DIV((X), DVS_BLOCKDIM_Y_LUMA))

/* Bilinear interpolation (HRT_GDC_BLI_MODE) is the supported method currently.
 * Bicubic interpolation (HRT_GDC_BCI_MODE) is not supported yet */
#define DVS_GDC_INTERP_METHOD HRT_GDC_BLI_MODE

#define DVS_INPUT_BYTES_PER_PIXEL (1)

#define DVS_NUM_BLOCKS_X_CHROMA(X)	(CEIL_DIV((X), DVS_BLOCKDIM_X))

#define DVS_NUM_BLOCKS_Y_CHROMA(X)	(CEIL_DIV((X), DVS_BLOCKDIM_Y_CHROMA))

#endif /* __SH_CSS_DVS_INFO_H__ */
