/*
 * Geode GX video processor device.
 *
 *   Copyright (C) 2006 Arcom Control Systems Ltd.
 *
 *   Portions from AMD's original 2.4 driver:
 *     Copyright (C) 2004 Advanced Micro Devices, Inc.
 *
 *   This program is free software; you can redistribute it and/or modify it
 *   under the terms of the GNU General Public License as published by the
 *   Free Software Foundation; either version 2 of the License, or (at your
 *   option) any later version.
 */
#include <linux/fb.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/msr.h>
#include <asm/geode.h>

#include "geodefb.h"
#include "video_gx.h"


/*
 * Tables of register settings for various DOTCLKs.
 */
struct gx_pll_entry {
	long pixclock; /* ps */
	u32 sys_rstpll_bits;
	u32 dotpll_value;
};

#define POSTDIV3 ((u32)MSR_GLCP_SYS_RSTPLL_DOTPOSTDIV3)
#define PREMULT2 ((u32)MSR_GLCP_SYS_RSTPLL_DOTPREMULT2)
#define PREDIV2  ((u32)MSR_GLCP_SYS_RSTPLL_DOTPOSTDIV3)

static const struct gx_pll_entry gx_pll_table_48MHz[] = {
	{ 40123, POSTDIV3,	    0x00000BF2 },	/*  24.9230 */
	{ 39721, 0,		    0x00000037 },	/*  25.1750 */
	{ 35308, POSTDIV3|PREMULT2, 0x00000B1A },	/*  28.3220 */
	{ 31746, POSTDIV3,	    0x000002D2 },	/*  31.5000 */
	{ 27777, POSTDIV3|PREMULT2, 0x00000FE2 },	/*  36.0000 */
	{ 26666, POSTDIV3,	    0x0000057A },	/*  37.5000 */
	{ 25000, POSTDIV3,	    0x0000030A },	/*  40.0000 */
	{ 22271, 0,		    0x00000063 },	/*  44.9000 */
	{ 20202, 0,		    0x0000054B },	/*  49.5000 */
	{ 20000, 0,		    0x0000026E },	/*  50.0000 */
	{ 19860, PREMULT2,	    0x00000037 },	/*  50.3500 */
	{ 18518, POSTDIV3|PREMULT2, 0x00000B0D },	/*  54.0000 */
	{ 17777, 0,		    0x00000577 },	/*  56.2500 */
	{ 17733, 0,		    0x000007F7 },	/*  56.3916 */
	{ 17653, 0,		    0x0000057B },	/*  56.6444 */
	{ 16949, PREMULT2,	    0x00000707 },	/*  59.0000 */
	{ 15873, POSTDIV3|PREMULT2, 0x00000B39 },	/*  63.0000 */
	{ 15384, POSTDIV3|PREMULT2, 0x00000B45 },	/*  65.0000 */
	{ 14814, POSTDIV3|PREMULT2, 0x00000FC1 },	/*  67.5000 */
	{ 14124, POSTDIV3,	    0x00000561 },	/*  70.8000 */
	{ 13888, POSTDIV3,	    0x000007E1 },	/*  72.0000 */
	{ 13426, PREMULT2,	    0x00000F4A },	/*  74.4810 */
	{ 13333, 0,		    0x00000052 },	/*  75.0000 */
	{ 12698, 0,		    0x00000056 },	/*  78.7500 */
	{ 12500, POSTDIV3|PREMULT2, 0x00000709 },	/*  80.0000 */
	{ 11135, PREMULT2,	    0x00000262 },	/*  89.8000 */
	{ 10582, 0,		    0x000002D2 },	/*  94.5000 */
	{ 10101, PREMULT2,	    0x00000B4A },	/*  99.0000 */
	{ 10000, PREMULT2,	    0x00000036 },	/* 100.0000 */
	{  9259, 0,		    0x000007E2 },	/* 108.0000 */
	{  8888, 0,		    0x000007F6 },	/* 112.5000 */
	{  7692, POSTDIV3|PREMULT2, 0x00000FB0 },	/* 130.0000 */
	{  7407, POSTDIV3|PREMULT2, 0x00000B50 },	/* 135.0000 */
	{  6349, 0,		    0x00000055 },	/* 157.5000 */
	{  6172, 0,		    0x000009C1 },	/* 162.0000 */
	{  5787, PREMULT2,	    0x0000002D },	/* 172.798  */
	{  5698, 0,		    0x000002C1 },	/* 175.5000 */
	{  5291, 0,		    0x000002D1 },	/* 189.0000 */
	{  4938, 0,		    0x00000551 },	/* 202.5000 */
	{  4357, 0,		    0x0000057D },	/* 229.5000 */
};

static const struct gx_pll_entry gx_pll_table_14MHz[] = {
	{ 39721, 0, 0x00000037 },	/*  25.1750 */
	{ 35308, 0, 0x00000B7B },	/*  28.3220 */
	{ 31746, 0, 0x000004D3 },	/*  31.5000 */
	{ 27777, 0, 0x00000BE3 },	/*  36.0000 */
	{ 26666, 0, 0x0000074F },	/*  37.5000 */
	{ 25000, 0, 0x0000050B },	/*  40.0000 */
	{ 22271, 0, 0x00000063 },	/*  44.9000 */
	{ 20202, 0, 0x0000054B },	/*  49.5000 */
	{ 20000, 0, 0x0000026E },	/*  50.0000 */
	{ 19860, 0, 0x000007C3 },	/*  50.3500 */
	{ 18518, 0, 0x000007E3 },	/*  54.0000 */
	{ 17777, 0, 0x00000577 },	/*  56.2500 */
	{ 17733, 0, 0x000002FB },	/*  56.3916 */
	{ 17653, 0, 0x0000057B },	/*  56.6444 */
	{ 16949, 0, 0x0000058B },	/*  59.0000 */
	{ 15873, 0, 0x0000095E },	/*  63.0000 */
	{ 15384, 0, 0x0000096A },	/*  65.0000 */
	{ 14814, 0, 0x00000BC2 },	/*  67.5000 */
	{ 14124, 0, 0x0000098A },	/*  70.8000 */
	{ 13888, 0, 0x00000BE2 },	/*  72.0000 */
	{ 13333, 0, 0x00000052 },	/*  75.0000 */
	{ 12698, 0, 0x00000056 },	/*  78.7500 */
	{ 12500, 0, 0x0000050A },	/*  80.0000 */
	{ 11135, 0, 0x0000078E },	/*  89.8000 */
	{ 10582, 0, 0x000002D2 },	/*  94.5000 */
	{ 10101, 0, 0x000011F6 },	/*  99.0000 */
	{ 10000, 0, 0x0000054E },	/* 100.0000 */
	{  9259, 0, 0x000007E2 },	/* 108.0000 */
	{  8888, 0, 0x000002FA },	/* 112.5000 */
	{  7692, 0, 0x00000BB1 },	/* 130.0000 */
	{  7407, 0, 0x00000975 },	/* 135.0000 */
	{  6349, 0, 0x00000055 },	/* 157.5000 */
	{  6172, 0, 0x000009C1 },	/* 162.0000 */
	{  5698, 0, 0x000002C1 },	/* 175.5000 */
	{  5291, 0, 0x00000539 },	/* 189.0000 */
	{  4938, 0, 0x00000551 },	/* 202.5000 */
	{  4357, 0, 0x0000057D },	/* 229.5000 */
};

static void gx_set_dclk_frequency(struct fb_info *info)
{
	const struct gx_pll_entry *pll_table;
	int pll_table_len;
	int i, best_i;
	long min, diff;
	u64 dotpll, sys_rstpll;
	int timeout = 1000;

	/* Rev. 1 Geode GXs use a 14 MHz reference clock instead of 48 MHz. */
	if (cpu_data(0).x86_mask == 1) {
		pll_table = gx_pll_table_14MHz;
		pll_table_len = ARRAY_SIZE(gx_pll_table_14MHz);
	} else {
		pll_table = gx_pll_table_48MHz;
		pll_table_len = ARRAY_SIZE(gx_pll_table_48MHz);
	}

	/* Search the table for the closest pixclock. */
	best_i = 0;
	min = abs(pll_table[0].pixclock - info->var.pixclock);
	for (i = 1; i < pll_table_len; i++) {
		diff = abs(pll_table[i].pixclock - info->var.pixclock);
		if (diff < min) {
			min = diff;
			best_i = i;
		}
	}

	rdmsrl(MSR_GLCP_SYS_RSTPLL, sys_rstpll);
	rdmsrl(MSR_GLCP_DOTPLL, dotpll);

	/* Program new M, N and P. */
	dotpll &= 0x00000000ffffffffull;
	dotpll |= (u64)pll_table[best_i].dotpll_value << 32;
	dotpll |= MSR_GLCP_DOTPLL_DOTRESET;
	dotpll &= ~MSR_GLCP_DOTPLL_BYPASS;

	wrmsrl(MSR_GLCP_DOTPLL, dotpll);

	/* Program dividers. */
	sys_rstpll &= ~( MSR_GLCP_SYS_RSTPLL_DOTPREDIV2
			 | MSR_GLCP_SYS_RSTPLL_DOTPREMULT2
			 | MSR_GLCP_SYS_RSTPLL_DOTPOSTDIV3 );
	sys_rstpll |= pll_table[best_i].sys_rstpll_bits;

	wrmsrl(MSR_GLCP_SYS_RSTPLL, sys_rstpll);

	/* Clear reset bit to start PLL. */
	dotpll &= ~(MSR_GLCP_DOTPLL_DOTRESET);
	wrmsrl(MSR_GLCP_DOTPLL, dotpll);

	/* Wait for LOCK bit. */
	do {
		rdmsrl(MSR_GLCP_DOTPLL, dotpll);
	} while (timeout-- && !(dotpll & MSR_GLCP_DOTPLL_LOCK));
}

static void
gx_configure_tft(struct fb_info *info)
{
	struct geodefb_par *par = info->par;
	unsigned long val;
	unsigned long fp;

	/* Set up the DF pad select MSR */

	rdmsrl(MSR_GX_MSR_PADSEL, val);
	val &= ~GX_VP_PAD_SELECT_MASK;
	val |= GX_VP_PAD_SELECT_TFT;
	wrmsrl(MSR_GX_MSR_PADSEL, val);

	/* Turn off the panel */

	fp = readl(par->vid_regs + GX_FP_PM);
	fp &= ~GX_FP_PM_P;
	writel(fp, par->vid_regs + GX_FP_PM);

	/* Set timing 1 */

	fp = readl(par->vid_regs + GX_FP_PT1);
	fp &= GX_FP_PT1_VSIZE_MASK;
	fp |= info->var.yres << GX_FP_PT1_VSIZE_SHIFT;
	writel(fp, par->vid_regs + GX_FP_PT1);

	/* Timing 2 */
	/* Set bits that are always on for TFT */

	fp = 0x0F100000;

	/* Configure sync polarity */

	if (!(info->var.sync & FB_SYNC_VERT_HIGH_ACT))
		fp |= GX_FP_PT2_VSP;

	if (!(info->var.sync & FB_SYNC_HOR_HIGH_ACT))
		fp |= GX_FP_PT2_HSP;

	writel(fp, par->vid_regs + GX_FP_PT2);

	/*  Set the dither control */
	writel(0x70, par->vid_regs + GX_FP_DFC);

	/* Enable the FP data and power (in case the BIOS didn't) */

	fp = readl(par->vid_regs + GX_DCFG);
	fp |= GX_DCFG_FP_PWR_EN | GX_DCFG_FP_DATA_EN;
	writel(fp, par->vid_regs + GX_DCFG);

	/* Unblank the panel */

	fp = readl(par->vid_regs + GX_FP_PM);
	fp |= GX_FP_PM_P;
	writel(fp, par->vid_regs + GX_FP_PM);
}

static void gx_configure_display(struct fb_info *info)
{
	struct geodefb_par *par = info->par;
	u32 dcfg, misc;

	/* Write the display configuration */
	dcfg = readl(par->vid_regs + GX_DCFG);

	/* Disable hsync and vsync */
	dcfg &= ~(GX_DCFG_VSYNC_EN | GX_DCFG_HSYNC_EN);
	writel(dcfg, par->vid_regs + GX_DCFG);

	/* Clear bits from existing mode. */
	dcfg &= ~(GX_DCFG_CRT_SYNC_SKW_MASK
		  | GX_DCFG_CRT_HSYNC_POL   | GX_DCFG_CRT_VSYNC_POL
		  | GX_DCFG_VSYNC_EN        | GX_DCFG_HSYNC_EN);

	/* Set default sync skew.  */
	dcfg |= GX_DCFG_CRT_SYNC_SKW_DFLT;

	/* Enable hsync and vsync. */
	dcfg |= GX_DCFG_HSYNC_EN | GX_DCFG_VSYNC_EN;

	misc = readl(par->vid_regs + GX_MISC);

	/* Disable gamma correction */
	misc |= GX_MISC_GAM_EN;

	if (par->enable_crt) {

		/* Power up the CRT DACs */
		misc &= ~(GX_MISC_A_PWRDN | GX_MISC_DAC_PWRDN);
		writel(misc, par->vid_regs + GX_MISC);

		/* Only change the sync polarities if we are running
		 * in CRT mode.  The FP polarities will be handled in
		 * gxfb_configure_tft */
		if (!(info->var.sync & FB_SYNC_HOR_HIGH_ACT))
			dcfg |= GX_DCFG_CRT_HSYNC_POL;
		if (!(info->var.sync & FB_SYNC_VERT_HIGH_ACT))
			dcfg |= GX_DCFG_CRT_VSYNC_POL;
	} else {
		/* Power down the CRT DACs if in FP mode */
		misc |= (GX_MISC_A_PWRDN | GX_MISC_DAC_PWRDN);
		writel(misc, par->vid_regs + GX_MISC);
	}

	/* Enable the display logic */
	/* Set up the DACS to blank normally */

	dcfg |= GX_DCFG_CRT_EN | GX_DCFG_DAC_BL_EN;

	/* Enable the external DAC VREF? */

	writel(dcfg, par->vid_regs + GX_DCFG);

	/* Set up the flat panel (if it is enabled) */

	if (par->enable_crt == 0)
		gx_configure_tft(info);
}

static int gx_blank_display(struct fb_info *info, int blank_mode)
{
	struct geodefb_par *par = info->par;
	u32 dcfg, fp_pm;
	int blank, hsync, vsync;

	/* CRT power saving modes. */
	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		blank = 0; hsync = 1; vsync = 1;
		break;
	case FB_BLANK_NORMAL:
		blank = 1; hsync = 1; vsync = 1;
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		blank = 1; hsync = 1; vsync = 0;
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		blank = 1; hsync = 0; vsync = 1;
		break;
	case FB_BLANK_POWERDOWN:
		blank = 1; hsync = 0; vsync = 0;
		break;
	default:
		return -EINVAL;
	}
	dcfg = readl(par->vid_regs + GX_DCFG);
	dcfg &= ~(GX_DCFG_DAC_BL_EN
		  | GX_DCFG_HSYNC_EN | GX_DCFG_VSYNC_EN);
	if (!blank)
		dcfg |= GX_DCFG_DAC_BL_EN;
	if (hsync)
		dcfg |= GX_DCFG_HSYNC_EN;
	if (vsync)
		dcfg |= GX_DCFG_VSYNC_EN;
	writel(dcfg, par->vid_regs + GX_DCFG);

	/* Power on/off flat panel. */

	if (par->enable_crt == 0) {
		fp_pm = readl(par->vid_regs + GX_FP_PM);
		if (blank_mode == FB_BLANK_POWERDOWN)
			fp_pm &= ~GX_FP_PM_P;
		else
			fp_pm |= GX_FP_PM_P;
		writel(fp_pm, par->vid_regs + GX_FP_PM);
	}

	return 0;
}

struct geode_vid_ops gx_vid_ops = {
	.set_dclk	   = gx_set_dclk_frequency,
	.configure_display = gx_configure_display,
	.blank_display	   = gx_blank_display,
};
