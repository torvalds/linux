/*
 * Geode GX video device
 *
 * Copyright (C) 2006 Arcom Control Systems Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __VIDEO_GX_H__
#define __VIDEO_GX_H__

extern struct geode_vid_ops gx_vid_ops;

/* GX Flatpanel control MSR */
#define GX_VP_MSR_PAD_SELECT           0x2011
#define GX_VP_PAD_SELECT_MASK          0x3FFFFFFF
#define GX_VP_PAD_SELECT_TFT           0x1FFFFFFF

/* Geode GX video processor registers */

#define GX_DCFG		0x0008
#  define GX_DCFG_CRT_EN		0x00000001
#  define GX_DCFG_HSYNC_EN		0x00000002
#  define GX_DCFG_VSYNC_EN		0x00000004
#  define GX_DCFG_DAC_BL_EN		0x00000008
#  define GX_DCFG_CRT_HSYNC_POL		0x00000100
#  define GX_DCFG_CRT_VSYNC_POL		0x00000200
#  define GX_DCFG_CRT_SYNC_SKW_MASK	0x0001C000
#  define GX_DCFG_CRT_SYNC_SKW_DFLT	0x00010000
#  define GX_DCFG_VG_CK			0x00100000
#  define GX_DCFG_GV_GAM		0x00200000
#  define GX_DCFG_DAC_VREF		0x04000000

/* Geode GX MISC video configuration */

#define GX_MISC 0x50
#define GX_MISC_GAM_EN     0x00000001
#define GX_MISC_DAC_PWRDN  0x00000400
#define GX_MISC_A_PWRDN    0x00000800

/* Geode GX flat panel display control registers */

#define GX_FP_PT1 0x0400
#define GX_FP_PT1_VSIZE_MASK 0x7FF0000
#define GX_FP_PT1_VSIZE_SHIFT 16

#define GX_FP_PT2 0x408
#define GX_FP_PT2_VSP (1 << 23)
#define GX_FP_PT2_HSP (1 << 22)

#define GX_FP_PM 0x410
#  define GX_FP_PM_P 0x01000000

#define GX_FP_DFC 0x418

/* Geode GX clock control MSRs */

#define MSR_GLCP_SYS_RSTPLL	0x4c000014
#  define MSR_GLCP_SYS_RSTPLL_DOTPREDIV2	(0x0000000000000002ull)
#  define MSR_GLCP_SYS_RSTPLL_DOTPREMULT2	(0x0000000000000004ull)
#  define MSR_GLCP_SYS_RSTPLL_DOTPOSTDIV3	(0x0000000000000008ull)

#define MSR_GLCP_DOTPLL		0x4c000015
#  define MSR_GLCP_DOTPLL_DOTRESET		(0x0000000000000001ull)
#  define MSR_GLCP_DOTPLL_BYPASS		(0x0000000000008000ull)
#  define MSR_GLCP_DOTPLL_LOCK			(0x0000000002000000ull)

#endif /* !__VIDEO_GX_H__ */
