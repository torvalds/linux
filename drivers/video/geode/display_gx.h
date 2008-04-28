/*
 * Geode GX display controller
 *
 * Copyright (C) 2006 Arcom Control Systems Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __DISPLAY_GX_H__
#define __DISPLAY_GX_H__

unsigned int gx_frame_buffer_size(void);
int gx_line_delta(int xres, int bpp);

extern struct geode_dc_ops gx_dc_ops;

/* MSR that tells us if a TFT or CRT is attached */
#define GLD_MSR_CONFIG_DM_FP 0x40

/* Display controller registers */

#define DC_UNLOCK 0x00
#  define DC_UNLOCK_CODE 0x00004758

#define DC_GENERAL_CFG 0x04
#  define DC_GCFG_DFLE	      0x00000001
#  define DC_GCFG_CURE	      0x00000002
#  define DC_GCFG_ICNE	      0x00000004
#  define DC_GCFG_VIDE	      0x00000008
#  define DC_GCFG_CMPE	      0x00000020
#  define DC_GCFG_DECE	      0x00000040
#  define DC_GCFG_VGAE	      0x00000080
#  define DC_GCFG_DFHPSL_MASK 0x00000F00
#  define DC_GCFG_DFHPSL_POS	       8
#  define DC_GCFG_DFHPEL_MASK 0x0000F000
#  define DC_GCFG_DFHPEL_POS	      12
#  define DC_GCFG_STFM	      0x00010000
#  define DC_GCFG_FDTY	      0x00020000
#  define DC_GCFG_VGAFT	      0x00040000
#  define DC_GCFG_VDSE	      0x00080000
#  define DC_GCFG_YUVM	      0x00100000
#  define DC_GCFG_VFSL	      0x00800000
#  define DC_GCFG_SIGE	      0x01000000
#  define DC_GCFG_SGRE	      0x02000000
#  define DC_GCFG_SGFR	      0x04000000
#  define DC_GCFG_CRC_MODE    0x08000000
#  define DC_GCFG_DIAG	      0x10000000
#  define DC_GCFG_CFRW	      0x20000000

#define DC_DISPLAY_CFG 0x08
#  define DC_DCFG_TGEN            0x00000001
#  define DC_DCFG_GDEN            0x00000008
#  define DC_DCFG_VDEN            0x00000010
#  define DC_DCFG_TRUP            0x00000040
#  define DC_DCFG_DISP_MODE_MASK  0x00000300
#  define DC_DCFG_DISP_MODE_8BPP  0x00000000
#  define DC_DCFG_DISP_MODE_16BPP 0x00000100
#  define DC_DCFG_DISP_MODE_24BPP 0x00000200
#  define DC_DCFG_16BPP_MODE_MASK 0x00000c00
#  define DC_DCFG_16BPP_MODE_565  0x00000000
#  define DC_DCFG_16BPP_MODE_555  0x00000100
#  define DC_DCFG_16BPP_MODE_444  0x00000200
#  define DC_DCFG_DCEN            0x00080000
#  define DC_DCFG_PALB            0x02000000
#  define DC_DCFG_FRLK            0x04000000
#  define DC_DCFG_VISL            0x08000000
#  define DC_DCFG_FRSL            0x20000000
#  define DC_DCFG_A18M            0x40000000
#  define DC_DCFG_A20M            0x80000000

#define DC_FB_ST_OFFSET 0x10

#define DC_LINE_SIZE 0x30
#  define DC_LINE_SIZE_FB_LINE_SIZE_MASK  0x000007ff
#  define DC_LINE_SIZE_FB_LINE_SIZE_POS            0
#  define DC_LINE_SIZE_CB_LINE_SIZE_MASK  0x007f0000
#  define DC_LINE_SIZE_CB_LINE_SIZE_POS           16
#  define DC_LINE_SIZE_VID_LINE_SIZE_MASK 0xff000000
#  define DC_LINE_SIZE_VID_LINE_SIZE_POS          24

#define DC_GFX_PITCH 0x34
#  define DC_GFX_PITCH_FB_PITCH_MASK 0x0000ffff
#  define DC_GFX_PITCH_FB_PITCH_POS           0
#  define DC_GFX_PITCH_CB_PITCH_MASK 0xffff0000
#  define DC_GFX_PITCH_CB_PITCH_POS          16

#define DC_H_ACTIVE_TIMING 0x40
#define DC_H_BLANK_TIMING  0x44
#define DC_H_SYNC_TIMING   0x48
#define DC_V_ACTIVE_TIMING 0x50
#define DC_V_BLANK_TIMING  0x54
#define DC_V_SYNC_TIMING   0x58

#define DC_PAL_ADDRESS 0x70
#define DC_PAL_DATA    0x74

#define DC_GLIU0_MEM_OFFSET 0x84
#endif /* !__DISPLAY_GX1_H__ */
