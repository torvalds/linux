// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers/video/geode/video_cs5530.c
 *   -- CS5530 video device
 *
 * Copyright (C) 2005 Arcom Control Systems Ltd.
 *
 * Based on AMD's original 2.4 driver:
 *   Copyright (C) 2004 Advanced Micro Devices, Inc.
 */
#include <linux/fb.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/delay.h>

#include "geodefb.h"
#include "video_cs5530.h"

/*
 * CS5530 PLL table. This maps pixclocks to the appropriate PLL register
 * value.
 */
struct cs5530_pll_entry {
	long pixclock; /* ps */
	u32 pll_value;
};

static const struct cs5530_pll_entry cs5530_pll_table[] = {
	{ 39721, 0x31C45801, }, /*  25.1750 MHz */
	{ 35308, 0x20E36802, }, /*  28.3220 */
	{ 31746, 0x33915801, }, /*  31.5000 */
	{ 27777, 0x31EC4801, }, /*  36.0000 */
	{ 26666, 0x21E22801, }, /*  37.5000 */
	{ 25000, 0x33088801, }, /*  40.0000 */
	{ 22271, 0x33E22801, }, /*  44.9000 */
	{ 20202, 0x336C4801, }, /*  49.5000 */
	{ 20000, 0x23088801, }, /*  50.0000 */
	{ 19860, 0x23088801, }, /*  50.3500 */
	{ 18518, 0x3708A801, }, /*  54.0000 */
	{ 17777, 0x23E36802, }, /*  56.2500 */
	{ 17733, 0x23E36802, }, /*  56.3916 */
	{ 17653, 0x23E36802, }, /*  56.6444 */
	{ 16949, 0x37C45801, }, /*  59.0000 */
	{ 15873, 0x23EC4801, }, /*  63.0000 */
	{ 15384, 0x37911801, }, /*  65.0000 */
	{ 14814, 0x37963803, }, /*  67.5000 */
	{ 14124, 0x37058803, }, /*  70.8000 */
	{ 13888, 0x3710C805, }, /*  72.0000 */
	{ 13333, 0x37E22801, }, /*  75.0000 */
	{ 12698, 0x27915801, }, /*  78.7500 */
	{ 12500, 0x37D8D802, }, /*  80.0000 */
	{ 11135, 0x27588802, }, /*  89.8000 */
	{ 10582, 0x27EC4802, }, /*  94.5000 */
	{ 10101, 0x27AC6803, }, /*  99.0000 */
	{ 10000, 0x27088801, }, /* 100.0000 */
	{  9259, 0x2710C805, }, /* 108.0000 */
	{  8888, 0x27E36802, }, /* 112.5000 */
	{  7692, 0x27C58803, }, /* 130.0000 */
	{  7407, 0x27316803, }, /* 135.0000 */
	{  6349, 0x2F915801, }, /* 157.5000 */
	{  6172, 0x2F08A801, }, /* 162.0000 */
	{  5714, 0x2FB11802, }, /* 175.0000 */
	{  5291, 0x2FEC4802, }, /* 189.0000 */
	{  4950, 0x2F963803, }, /* 202.0000 */
	{  4310, 0x2FB1B802, }, /* 232.0000 */
};

static void cs5530_set_dclk_frequency(struct fb_info *info)
{
	struct geodefb_par *par = info->par;
	int i;
	u32 value;
	long min, diff;

	/* Search the table for the closest pixclock. */
	value = cs5530_pll_table[0].pll_value;
	min = cs5530_pll_table[0].pixclock - info->var.pixclock;
	if (min < 0) min = -min;
	for (i = 1; i < ARRAY_SIZE(cs5530_pll_table); i++) {
		diff = cs5530_pll_table[i].pixclock - info->var.pixclock;
		if (diff < 0L) diff = -diff;
		if (diff < min) {
			min = diff;
			value = cs5530_pll_table[i].pll_value;
		}
	}

	writel(value, par->vid_regs + CS5530_DOT_CLK_CONFIG);
	writel(value | 0x80000100, par->vid_regs + CS5530_DOT_CLK_CONFIG); /* set reset and bypass */
	udelay(500); /* wait for PLL to settle */
	writel(value & 0x7FFFFFFF, par->vid_regs + CS5530_DOT_CLK_CONFIG); /* clear reset */
	writel(value & 0x7FFFFEFF, par->vid_regs + CS5530_DOT_CLK_CONFIG); /* clear bypass */
}

static void cs5530_configure_display(struct fb_info *info)
{
	struct geodefb_par *par = info->par;
	u32 dcfg;

	dcfg = readl(par->vid_regs + CS5530_DISPLAY_CONFIG);

	/* Clear bits from existing mode. */
	dcfg &= ~(CS5530_DCFG_CRT_SYNC_SKW_MASK | CS5530_DCFG_PWR_SEQ_DLY_MASK
		  | CS5530_DCFG_CRT_HSYNC_POL   | CS5530_DCFG_CRT_VSYNC_POL
		  | CS5530_DCFG_FP_PWR_EN       | CS5530_DCFG_FP_DATA_EN
		  | CS5530_DCFG_DAC_PWR_EN      | CS5530_DCFG_VSYNC_EN
		  | CS5530_DCFG_HSYNC_EN);

	/* Set default sync skew and power sequence delays.  */
	dcfg |= (CS5530_DCFG_CRT_SYNC_SKW_INIT | CS5530_DCFG_PWR_SEQ_DLY_INIT
		 | CS5530_DCFG_GV_PAL_BYP);

	/* Enable DACs, hsync and vsync for CRTs */
	if (par->enable_crt) {
		dcfg |= CS5530_DCFG_DAC_PWR_EN;
		dcfg |= CS5530_DCFG_HSYNC_EN | CS5530_DCFG_VSYNC_EN;
	}
	/* Enable panel power and data if using a flat panel. */
	if (par->panel_x > 0) {
		dcfg |= CS5530_DCFG_FP_PWR_EN;
		dcfg |= CS5530_DCFG_FP_DATA_EN;
	}

	/* Sync polarities. */
	if (info->var.sync & FB_SYNC_HOR_HIGH_ACT)
		dcfg |= CS5530_DCFG_CRT_HSYNC_POL;
	if (info->var.sync & FB_SYNC_VERT_HIGH_ACT)
		dcfg |= CS5530_DCFG_CRT_VSYNC_POL;

	writel(dcfg, par->vid_regs + CS5530_DISPLAY_CONFIG);
}

static int cs5530_blank_display(struct fb_info *info, int blank_mode)
{
	struct geodefb_par *par = info->par;
	u32 dcfg;
	int blank, hsync, vsync;

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

	dcfg = readl(par->vid_regs + CS5530_DISPLAY_CONFIG);

	dcfg &= ~(CS5530_DCFG_DAC_BL_EN | CS5530_DCFG_DAC_PWR_EN
		  | CS5530_DCFG_HSYNC_EN | CS5530_DCFG_VSYNC_EN
		  | CS5530_DCFG_FP_DATA_EN | CS5530_DCFG_FP_PWR_EN);

	if (par->enable_crt) {
		if (!blank)
			dcfg |= CS5530_DCFG_DAC_BL_EN | CS5530_DCFG_DAC_PWR_EN;
		if (hsync)
			dcfg |= CS5530_DCFG_HSYNC_EN;
		if (vsync)
			dcfg |= CS5530_DCFG_VSYNC_EN;
	}
	if (par->panel_x > 0) {
		if (!blank)
			dcfg |= CS5530_DCFG_FP_DATA_EN;
		if (hsync && vsync)
			dcfg |= CS5530_DCFG_FP_PWR_EN;
	}

	writel(dcfg, par->vid_regs + CS5530_DISPLAY_CONFIG);

	return 0;
}

const struct geode_vid_ops cs5530_vid_ops = {
	.set_dclk          = cs5530_set_dclk_frequency,
	.configure_display = cs5530_configure_display,
	.blank_display     = cs5530_blank_display,
};
