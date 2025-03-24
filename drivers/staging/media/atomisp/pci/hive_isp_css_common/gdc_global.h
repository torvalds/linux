/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __GDC_GLOBAL_H_INCLUDED__
#define __GDC_GLOBAL_H_INCLUDED__

#define IS_GDC_VERSION_2

#include <type_support.h>
#include "gdc_v2_defs.h"

/*
 * Storage addresses for packed data transfer
 */
#define GDC_PARAM_ICX_LEFT_ROUNDED_IDX            0
#define GDC_PARAM_OXDIM_FLOORED_IDX               1
#define GDC_PARAM_OXDIM_LAST_IDX                  2
#define GDC_PARAM_WOIX_LAST_IDX                   3
#define GDC_PARAM_IY_TOPLEFT_IDX                  4
#define GDC_PARAM_CHUNK_CNT_IDX                   5
/*#define GDC_PARAM_ELEMENTS_PER_XMEM_ADDR_IDX    6 */		/* Derived from bpp */
#define GDC_PARAM_BPP_IDX                         6
#define GDC_PARAM_BLOCK_HEIGHT_IDX                7
/*#define GDC_PARAM_DMA_CHANNEL_STRIDE_A_IDX      8*/		/* The DMA stride == the GDC buffer stride */
#define GDC_PARAM_WOIX_IDX                        8
#define GDC_PARAM_DMA_CHANNEL_STRIDE_B_IDX        9
#define GDC_PARAM_DMA_CHANNEL_WIDTH_A_IDX        10
#define GDC_PARAM_DMA_CHANNEL_WIDTH_B_IDX        11
#define GDC_PARAM_VECTORS_PER_LINE_IN_IDX        12
#define GDC_PARAM_VECTORS_PER_LINE_OUT_IDX       13
#define GDC_PARAM_VMEM_IN_DIMY_IDX               14
#define GDC_PARAM_COMMAND_IDX                    15
#define N_GDC_PARAM                              16

/* Because of the packed parameter transfer max(params) == max(fragments) */
#define	N_GDC_FRAGMENTS		N_GDC_PARAM

/* The GDC is capable of higher internal precision than the parameter data structures */
#define HRT_GDC_COORD_SCALE_BITS	6
#define HRT_GDC_COORD_SCALE			BIT(HRT_GDC_COORD_SCALE_BITS)

typedef enum {
	GDC_CH0_ID = 0,
	N_GDC_CHANNEL_ID
} gdc_channel_ID_t;

typedef enum {
	gdc_8_bpp  = 8,
	gdc_10_bpp = 10,
	gdc_12_bpp = 12,
	gdc_14_bpp = 14
} gdc_bits_per_pixel_t;

typedef struct gdc_scale_param_mem_s {
	u16  params[N_GDC_PARAM];
	u16  ipx_start_array[N_GDC_PARAM];
	u16  ibuf_offset[N_GDC_PARAM];
	u16  obuf_offset[N_GDC_PARAM];
} gdc_scale_param_mem_t;

typedef struct gdc_warp_param_mem_s {
	u32      origin_x;
	u32      origin_y;
	u32      in_addr_offset;
	u32      in_block_width;
	u32      in_block_height;
	u32      p0_x;
	u32      p0_y;
	u32      p1_x;
	u32      p1_y;
	u32      p2_x;
	u32      p2_y;
	u32      p3_x;
	u32      p3_y;
	u32      padding[3];
} gdc_warp_param_mem_t;

#endif /* __GDC_GLOBAL_H_INCLUDED__ */
