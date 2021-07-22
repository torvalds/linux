/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef _PixelGen_SysBlock_defs_h
#define _PixelGen_SysBlock_defs_h

/* Parematers and User_Parameters for HSS */
#define _PXG_PPC                       Ppc
#define _PXG_PIXEL_BITS                PixelWidth
#define _PXG_MAX_NOF_SID               MaxNofSids
#define _PXG_DATA_BITS                 DataWidth
#define _PXG_CNT_BITS                  CntWidth
#define _PXG_FIFODEPTH                 FifoDepth
#define _PXG_DBG                       Dbg_device_not_included

/* ID's and Address */
#define _PXG_ADRRESS_ALIGN_REG         4

#define _PXG_COM_ENABLE_REG_IDX        0
#define _PXG_PRBS_RSTVAL_REG0_IDX      1
#define _PXG_PRBS_RSTVAL_REG1_IDX      2
#define _PXG_SYNG_SID_REG_IDX          3
#define _PXG_SYNG_FREE_RUN_REG_IDX     4
#define _PXG_SYNG_PAUSE_REG_IDX        5
#define _PXG_SYNG_NOF_FRAME_REG_IDX    6
#define _PXG_SYNG_NOF_PIXEL_REG_IDX    7
#define _PXG_SYNG_NOF_LINE_REG_IDX     8
#define _PXG_SYNG_HBLANK_CYC_REG_IDX   9
#define _PXG_SYNG_VBLANK_CYC_REG_IDX  10
#define _PXG_SYNG_STAT_HCNT_REG_IDX   11
#define _PXG_SYNG_STAT_VCNT_REG_IDX   12
#define _PXG_SYNG_STAT_FCNT_REG_IDX   13
#define _PXG_SYNG_STAT_DONE_REG_IDX   14
#define _PXG_TPG_MODE_REG_IDX         15
#define _PXG_TPG_HCNT_MASK_REG_IDX    16
#define _PXG_TPG_VCNT_MASK_REG_IDX    17
#define _PXG_TPG_XYCNT_MASK_REG_IDX   18
#define _PXG_TPG_HCNT_DELTA_REG_IDX   19
#define _PXG_TPG_VCNT_DELTA_REG_IDX   20
#define _PXG_TPG_R1_REG_IDX           21
#define _PXG_TPG_G1_REG_IDX           22
#define _PXG_TPG_B1_REG_IDX           23
#define _PXG_TPG_R2_REG_IDX           24
#define _PXG_TPG_G2_REG_IDX           25
#define _PXG_TPG_B2_REG_IDX           26
/* */
#define _PXG_SYNG_PAUSE_CYCLES        0
/* Subblock ID's */
#define _PXG_DISABLE_IDX              0
#define _PXG_PRBS_IDX                 0
#define _PXG_TPG_IDX                  1
#define _PXG_SYNG_IDX                 2
#define _PXG_SMUX_IDX                 3
/* Register Widths */
#define _PXG_COM_ENABLE_REG_WIDTH     2
#define _PXG_COM_SRST_REG_WIDTH       4
#define _PXG_PRBS_RSTVAL_REG0_WIDTH  31
#define _PXG_PRBS_RSTVAL_REG1_WIDTH  31

#define _PXG_SYNG_SID_REG_WIDTH        3

#define _PXG_SYNG_FREE_RUN_REG_WIDTH   1
#define _PXG_SYNG_PAUSE_REG_WIDTH      1
/*
#define _PXG_SYNG_NOF_FRAME_REG_WIDTH  <sync_gen_cnt_width>
#define _PXG_SYNG_NOF_PIXEL_REG_WIDTH  <sync_gen_cnt_width>
#define _PXG_SYNG_NOF_LINE_REG_WIDTH   <sync_gen_cnt_width>
#define _PXG_SYNG_HBLANK_CYC_REG_WIDTH <sync_gen_cnt_width>
#define _PXG_SYNG_VBLANK_CYC_REG_WIDTH <sync_gen_cnt_width>
#define _PXG_SYNG_STAT_HCNT_REG_WIDTH  <sync_gen_cnt_width>
#define _PXG_SYNG_STAT_VCNT_REG_WIDTH  <sync_gen_cnt_width>
#define _PXG_SYNG_STAT_FCNT_REG_WIDTH  <sync_gen_cnt_width>
*/
#define _PXG_SYNG_STAT_DONE_REG_WIDTH  1
#define _PXG_TPG_MODE_REG_WIDTH        2
/*
#define _PXG_TPG_HCNT_MASK_REG_WIDTH   <sync_gen_cnt_width>
#define _PXG_TPG_VCNT_MASK_REG_WIDTH   <sync_gen_cnt_width>
#define _PXG_TPG_XYCNT_MASK_REG_WIDTH  <pixle_width>
*/
#define _PXG_TPG_HCNT_DELTA_REG_WIDTH  4
#define _PXG_TPG_VCNT_DELTA_REG_WIDTH  4
/*
#define _PXG_TPG_R1_REG_WIDTH          <pixle_width>
#define _PXG_TPG_G1_REG_WIDTH          <pixle_width>
#define _PXG_TPG_B1_REG_WIDTH          <pixle_width>
#define _PXG_TPG_R2_REG_WIDTH          <pixle_width>
#define _PXG_TPG_G2_REG_WIDTH          <pixle_width>
#define _PXG_TPG_B2_REG_WIDTH          <pixle_width>
*/
#define _PXG_FIFO_DEPTH                2
/* MISC */
#define _PXG_ENABLE_REG_VAL            1
#define _PXG_PRBS_ENABLE_REG_VAL       1
#define _PXG_TPG_ENABLE_REG_VAL        2
#define _PXG_SYNG_ENABLE_REG_VAL       4
#define _PXG_FIFO_ENABLE_REG_VAL       8
#define _PXG_PXL_BITS                 14
#define _PXG_INVALID_FLAG              0xDEADBEEF
#define _PXG_CAFE_FLAG                 0xCAFEBABE

#endif /* _PixelGen_SysBlock_defs_h */
