/* Geode LX framebuffer driver
 *
 * Copyright (C) 2006-2007, Advanced Micro Devices,Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <asm/geode.h>

#include "lxfb.h"

/* TODO
 * Support panel scaling
 * Add acceleration
 * Add support for interlacing (TV out)
 * Support compression
 */

/* This is the complete list of PLL frequencies that we can set -
 * we will choose the closest match to the incoming clock.
 * freq is the frequency of the dotclock * 1000 (for example,
 * 24823 = 24.983 Mhz).
 * pllval is the corresponding PLL value
*/

static const struct {
  unsigned int pllval;
  unsigned int freq;
} pll_table[] = {
  { 0x000031AC, 24923 },
  { 0x0000215D, 25175 },
  { 0x00001087, 27000 },
  { 0x0000216C, 28322 },
  { 0x0000218D, 28560 },
  { 0x000010C9, 31200 },
  { 0x00003147, 31500 },
  { 0x000010A7, 33032 },
  { 0x00002159, 35112 },
  { 0x00004249, 35500 },
  { 0x00000057, 36000 },
  { 0x0000219A, 37889 },
  { 0x00002158, 39168 },
  { 0x00000045, 40000 },
  { 0x00000089, 43163 },
  { 0x000010E7, 44900 },
  { 0x00002136, 45720 },
  { 0x00003207, 49500 },
  { 0x00002187, 50000 },
  { 0x00004286, 56250 },
  { 0x000010E5, 60065 },
  { 0x00004214, 65000 },
  { 0x00001105, 68179 },
  { 0x000031E4, 74250 },
  { 0x00003183, 75000 },
  { 0x00004284, 78750 },
  { 0x00001104, 81600 },
  { 0x00006363, 94500 },
  { 0x00005303, 97520 },
  { 0x00002183, 100187 },
  { 0x00002122, 101420 },
  { 0x00001081, 108000 },
  { 0x00006201, 113310 },
  { 0x00000041, 119650 },
  { 0x000041A1, 129600 },
  { 0x00002182, 133500 },
  { 0x000041B1, 135000 },
  { 0x00000051, 144000 },
  { 0x000041E1, 148500 },
  { 0x000062D1, 157500 },
  { 0x000031A1, 162000 },
  { 0x00000061, 169203 },
  { 0x00004231, 172800 },
  { 0x00002151, 175500 },
  { 0x000052E1, 189000 },
  { 0x00000071, 192000 },
  { 0x00003201, 198000 },
  { 0x00004291, 202500 },
  { 0x00001101, 204750 },
  { 0x00007481, 218250 },
  { 0x00004170, 229500 },
  { 0x00006210, 234000 },
  { 0x00003140, 251182 },
  { 0x00006250, 261000 },
  { 0x000041C0, 278400 },
  { 0x00005220, 280640 },
  { 0x00000050, 288000 },
  { 0x000041E0, 297000 },
  { 0x00002130, 320207 }
};


static void lx_set_dotpll(u32 pllval)
{
	u32 dotpll_lo, dotpll_hi;
	int i;

	rdmsr(MSR_GLCP_DOTPLL, dotpll_lo, dotpll_hi);

	if ((dotpll_lo & GLCP_DOTPLL_LOCK) && (dotpll_hi == pllval))
		return;

	dotpll_hi = pllval;
	dotpll_lo &= ~(GLCP_DOTPLL_BYPASS | GLCP_DOTPLL_HALFPIX);
	dotpll_lo |= GLCP_DOTPLL_RESET;

	wrmsr(MSR_GLCP_DOTPLL, dotpll_lo, dotpll_hi);

	/* Wait 100us for the PLL to lock */

	udelay(100);

	/* Now, loop for the lock bit */

	for (i = 0; i < 1000; i++) {
		rdmsr(MSR_GLCP_DOTPLL, dotpll_lo, dotpll_hi);
		if (dotpll_lo & GLCP_DOTPLL_LOCK)
			break;
	}

	/* Clear the reset bit */

	dotpll_lo &= ~GLCP_DOTPLL_RESET;
	wrmsr(MSR_GLCP_DOTPLL, dotpll_lo, dotpll_hi);
}

/* Set the clock based on the frequency specified by the current mode */

static void lx_set_clock(struct fb_info *info)
{
	unsigned int diff, min, best = 0;
	unsigned int freq, i;

	freq = (unsigned int) (0x3b9aca00 / info->var.pixclock);

	min = abs(pll_table[0].freq - freq);

	for (i = 0; i < ARRAY_SIZE(pll_table); i++) {
		diff = abs(pll_table[i].freq - freq);
		if (diff < min) {
			min = diff;
			best = i;
		}
	}

	lx_set_dotpll(pll_table[best].pllval & 0x7FFF);
}

static void lx_graphics_disable(struct fb_info *info)
{
	struct lxfb_par *par = info->par;
	unsigned int val, gcfg;

	/* Note:  This assumes that the video is in a quitet state */

	writel(0, par->df_regs + DF_ALPHA_CONTROL_1);
	writel(0, par->df_regs + DF_ALPHA_CONTROL_1 + 32);
	writel(0, par->df_regs + DF_ALPHA_CONTROL_1 + 64);

	/* Turn off the VGA and video enable */
	val = readl (par->dc_regs + DC_GENERAL_CFG) &
		~(DC_GCFG_VGAE | DC_GCFG_VIDE);

	writel(val, par->dc_regs + DC_GENERAL_CFG);

	val = readl(par->df_regs + DF_VIDEO_CFG) & ~DF_VCFG_VID_EN;
	writel(val, par->df_regs + DF_VIDEO_CFG);

	writel( DC_IRQ_MASK | DC_VSYNC_IRQ_MASK |
		DC_IRQ_STATUS | DC_VSYNC_IRQ_STATUS,
		par->dc_regs + DC_IRQ);

	val = readl(par->dc_regs + DC_GENLCK_CTRL) & ~DC_GENLCK_ENABLE;
	writel(val, par->dc_regs + DC_GENLCK_CTRL);

	val = readl(par->dc_regs + DC_COLOR_KEY) & ~DC_CLR_KEY_ENABLE;
	writel(val & ~DC_CLR_KEY_ENABLE, par->dc_regs + DC_COLOR_KEY);

	/* We don't actually blank the panel, due to the long latency
	   involved with bringing it back */

	val = readl(par->df_regs + DF_MISC) | DF_MISC_DAC_PWRDN;
	writel(val, par->df_regs + DF_MISC);

	/* Turn off the display */

	val = readl(par->df_regs + DF_DISPLAY_CFG);
	writel(val & ~(DF_DCFG_CRT_EN | DF_DCFG_HSYNC_EN | DF_DCFG_VSYNC_EN |
		       DF_DCFG_DAC_BL_EN), par->df_regs + DF_DISPLAY_CFG);

	gcfg = readl(par->dc_regs + DC_GENERAL_CFG);
	gcfg &= ~(DC_GCFG_CMPE | DC_GCFG_DECE);
	writel(gcfg, par->dc_regs + DC_GENERAL_CFG);

	/* Turn off the TGEN */
	val = readl(par->dc_regs + DC_DISPLAY_CFG);
	val &= ~DC_DCFG_TGEN;
	writel(val, par->dc_regs + DC_DISPLAY_CFG);

	/* Wait 1000 usecs to ensure that the TGEN is clear */
	udelay(1000);

	/* Turn off the FIFO loader */

	gcfg &= ~DC_GCFG_DFLE;
	writel(gcfg, par->dc_regs + DC_GENERAL_CFG);

	/* Lastly, wait for the GP to go idle */

	do {
		val = readl(par->gp_regs + GP_BLT_STATUS);
	} while ((val & GP_BS_BLT_BUSY) || !(val & GP_BS_CB_EMPTY));
}

static void lx_graphics_enable(struct fb_info *info)
{
	struct lxfb_par *par = info->par;
	u32 temp, config;

	/* Set the video request register */
	writel(0, par->df_regs + DF_VIDEO_REQUEST);

	/* Set up the polarities */

	config = readl(par->df_regs + DF_DISPLAY_CFG);

	config &= ~(DF_DCFG_CRT_SYNC_SKW_MASK | DF_DCFG_PWR_SEQ_DLY_MASK |
		  DF_DCFG_CRT_HSYNC_POL     | DF_DCFG_CRT_VSYNC_POL);

	config |= (DF_DCFG_CRT_SYNC_SKW_INIT | DF_DCFG_PWR_SEQ_DLY_INIT  |
		   DF_DCFG_GV_PAL_BYP);

	if (info->var.sync & FB_SYNC_HOR_HIGH_ACT)
		config |= DF_DCFG_CRT_HSYNC_POL;

	if (info->var.sync & FB_SYNC_VERT_HIGH_ACT)
		config |= DF_DCFG_CRT_VSYNC_POL;

	if (par->output & OUTPUT_PANEL) {
		u32 msrlo, msrhi;

		writel(DF_DEFAULT_TFT_PMTIM1,
		       par->df_regs + DF_PANEL_TIM1);
		writel(DF_DEFAULT_TFT_PMTIM2,
		       par->df_regs + DF_PANEL_TIM2);
		writel(DF_DEFAULT_TFT_DITHCTL,
		       par->df_regs + DF_DITHER_CONTROL);

		msrlo = DF_DEFAULT_TFT_PAD_SEL_LOW;
		msrhi = DF_DEFAULT_TFT_PAD_SEL_HIGH;

		wrmsr(MSR_LX_MSR_PADSEL, msrlo, msrhi);
	}

	if (par->output & OUTPUT_CRT) {
		config |= DF_DCFG_CRT_EN   | DF_DCFG_HSYNC_EN |
			DF_DCFG_VSYNC_EN | DF_DCFG_DAC_BL_EN;
	}

	writel(config, par->df_regs + DF_DISPLAY_CFG);

	/* Turn the CRT dacs back on */

	if (par->output & OUTPUT_CRT) {
		temp = readl(par->df_regs + DF_MISC);
		temp &= ~(DF_MISC_DAC_PWRDN  | DF_MISC_A_PWRDN);
		writel(temp, par->df_regs + DF_MISC);
	}

	/* Turn the panel on (if it isn't already) */

	if (par->output & OUTPUT_PANEL) {
		temp = readl(par->df_regs + DF_FP_PM);

		if (!(temp & 0x09))
			writel(temp | DF_FP_PM_P, par->df_regs + DF_FP_PM);
	}

	temp = readl(par->df_regs + DF_MISC);
	temp = readl(par->df_regs + DF_DISPLAY_CFG);
}

unsigned int lx_framebuffer_size(void)
{
	unsigned int val;

	/* The frame buffer size is reported by a VSM in VSA II */
	/* Virtual Register Class    = 0x02                     */
	/* VG_MEM_SIZE (1MB units)   = 0x00                     */

	outw(0xFC53, 0xAC1C);
	outw(0x0200, 0xAC1C);

	val = (unsigned int)(inw(0xAC1E)) & 0xFE;
	return (val << 20);
}

void lx_set_mode(struct fb_info *info)
{
	struct lxfb_par *par = info->par;
	u64 msrval;

	unsigned int max, dv, val, size;

	unsigned int gcfg, dcfg;
	int hactive, hblankstart, hsyncstart, hsyncend, hblankend, htotal;
	int vactive, vblankstart, vsyncstart, vsyncend, vblankend, vtotal;

	/* Unlock the DC registers */
	writel(DC_UNLOCK_CODE, par->dc_regs + DC_UNLOCK);

	lx_graphics_disable(info);

	lx_set_clock(info);

	/* Set output mode */

	rdmsrl(MSR_LX_GLD_MSR_CONFIG, msrval);
	msrval &= ~DF_CONFIG_OUTPUT_MASK;

	if (par->output & OUTPUT_PANEL) {
		msrval |= DF_OUTPUT_PANEL;

		if (par->output & OUTPUT_CRT)
			msrval |= DF_SIMULTANEOUS_CRT_AND_FP;
		else
			msrval &= ~DF_SIMULTANEOUS_CRT_AND_FP;
	} else {
		msrval |= DF_OUTPUT_CRT;
	}

	wrmsrl(MSR_LX_GLD_MSR_CONFIG, msrval);

	/* Clear the various buffers */
	/* FIXME:  Adjust for panning here */

	writel(0, par->dc_regs + DC_FB_START);
	writel(0, par->dc_regs + DC_CB_START);
	writel(0, par->dc_regs + DC_CURSOR_START);

	/* FIXME: Add support for interlacing */
	/* FIXME: Add support for scaling */

	val = readl(par->dc_regs + DC_GENLCK_CTRL);
	val &= ~(DC_GC_ALPHA_FLICK_ENABLE |
		 DC_GC_FLICKER_FILTER_ENABLE | DC_GC_FLICKER_FILTER_MASK);

	/* Default scaling params */

	writel((0x4000 << 16) | 0x4000, par->dc_regs + DC_GFX_SCALE);
	writel(0, par->dc_regs + DC_IRQ_FILT_CTL);
	writel(val, par->dc_regs + DC_GENLCK_CTRL);

	/* FIXME:  Support compression */

	if (info->fix.line_length > 4096)
		dv = DC_DV_LINE_SIZE_8192;
	else if (info->fix.line_length > 2048)
		dv = DC_DV_LINE_SIZE_4096;
	else if (info->fix.line_length > 1024)
		dv = DC_DV_LINE_SIZE_2048;
	else
		dv = DC_DV_LINE_SIZE_1024;

	max = info->fix.line_length * info->var.yres;
	max = (max + 0x3FF) & 0xFFFFFC00;

	writel(max | DC_DV_TOP_ENABLE, par->dc_regs + DC_DV_TOP);

	val = readl(par->dc_regs + DC_DV_CTL) & ~DC_DV_LINE_SIZE_MASK;
	writel(val | dv, par->dc_regs + DC_DV_CTL);

	size = info->var.xres * (info->var.bits_per_pixel >> 3);

	writel(info->fix.line_length >> 3, par->dc_regs + DC_GRAPHICS_PITCH);
	writel((size + 7) >> 3, par->dc_regs + DC_LINE_SIZE);

	/* Set default watermark values */

	rdmsrl(MSR_LX_SPARE_MSR, msrval);

	msrval &= ~(DC_SPARE_DISABLE_CFIFO_HGO | DC_SPARE_VFIFO_ARB_SELECT |
		    DC_SPARE_LOAD_WM_LPEN_MASK | DC_SPARE_WM_LPEN_OVRD |
		    DC_SPARE_DISABLE_INIT_VID_PRI | DC_SPARE_DISABLE_VFIFO_WM);
	msrval |= DC_SPARE_DISABLE_VFIFO_WM | DC_SPARE_DISABLE_INIT_VID_PRI;
	wrmsrl(MSR_LX_SPARE_MSR, msrval);

	gcfg = DC_GCFG_DFLE;   /* Display fifo enable */
	gcfg |= 0xB600;         /* Set default priority */
	gcfg |= DC_GCFG_FDTY;  /* Set the frame dirty mode */

	dcfg  = DC_DCFG_VDEN;  /* Enable video data */
	dcfg |= DC_DCFG_GDEN;  /* Enable graphics */
	dcfg |= DC_DCFG_TGEN;  /* Turn on the timing generator */
	dcfg |= DC_DCFG_TRUP;  /* Update timings immediately */
	dcfg |= DC_DCFG_PALB;  /* Palette bypass in > 8 bpp modes */
	dcfg |= DC_DCFG_VISL;
	dcfg |= DC_DCFG_DCEN;  /* Always center the display */

	/* Set the current BPP mode */

	switch (info->var.bits_per_pixel) {
	case 8:
		dcfg |= DC_DCFG_DISP_MODE_8BPP;
		break;

	case 16:
		dcfg |= DC_DCFG_DISP_MODE_16BPP | DC_DCFG_16BPP;
		break;

	case 32:
	case 24:
		dcfg |= DC_DCFG_DISP_MODE_24BPP;
		break;
	}

	/* Now - set up the timings */

	hactive = info->var.xres;
	hblankstart = hactive;
	hsyncstart = hblankstart + info->var.right_margin;
	hsyncend =  hsyncstart + info->var.hsync_len;
	hblankend = hsyncend + info->var.left_margin;
	htotal = hblankend;

	vactive = info->var.yres;
	vblankstart = vactive;
	vsyncstart = vblankstart + info->var.lower_margin;
	vsyncend =  vsyncstart + info->var.vsync_len;
	vblankend = vsyncend + info->var.upper_margin;
	vtotal = vblankend;

	writel((hactive - 1) | ((htotal - 1) << 16),
	       par->dc_regs + DC_H_ACTIVE_TIMING);
	writel((hblankstart - 1) | ((hblankend - 1) << 16),
	       par->dc_regs + DC_H_BLANK_TIMING);
	writel((hsyncstart - 1) | ((hsyncend - 1) << 16),
	       par->dc_regs + DC_H_SYNC_TIMING);

	writel((vactive - 1) | ((vtotal - 1) << 16),
	       par->dc_regs + DC_V_ACTIVE_TIMING);

	writel((vblankstart - 1) | ((vblankend - 1) << 16),
	       par->dc_regs + DC_V_BLANK_TIMING);

	writel((vsyncstart - 1)  | ((vsyncend - 1) << 16),
	       par->dc_regs + DC_V_SYNC_TIMING);

	writel( (info->var.xres - 1) << 16 | (info->var.yres - 1),
		par->dc_regs + DC_FB_ACTIVE);

	/* And re-enable the graphics output */
	lx_graphics_enable(info);

	/* Write the two main configuration registers */
	writel(dcfg, par->dc_regs + DC_DISPLAY_CFG);
	writel(0, par->dc_regs + DC_ARB_CFG);
	writel(gcfg, par->dc_regs + DC_GENERAL_CFG);

	/* Lock the DC registers */
	writel(0, par->dc_regs + DC_UNLOCK);
}

void lx_set_palette_reg(struct fb_info *info, unsigned regno,
			unsigned red, unsigned green, unsigned blue)
{
	struct lxfb_par *par = info->par;
	int val;

	/* Hardware palette is in RGB 8-8-8 format. */

	val  = (red   << 8) & 0xff0000;
	val |= (green)      & 0x00ff00;
	val |= (blue  >> 8) & 0x0000ff;

	writel(regno, par->dc_regs + DC_PAL_ADDRESS);
	writel(val, par->dc_regs + DC_PAL_DATA);
}

int lx_blank_display(struct fb_info *info, int blank_mode)
{
	struct lxfb_par *par = info->par;
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

	dcfg = readl(par->df_regs + DF_DISPLAY_CFG);
	dcfg &= ~(DF_DCFG_DAC_BL_EN
		  | DF_DCFG_HSYNC_EN | DF_DCFG_VSYNC_EN);
	if (!blank)
		dcfg |= DF_DCFG_DAC_BL_EN;
	if (hsync)
		dcfg |= DF_DCFG_HSYNC_EN;
	if (vsync)
		dcfg |= DF_DCFG_VSYNC_EN;
	writel(dcfg, par->df_regs + DF_DISPLAY_CFG);

	/* Power on/off flat panel */

	if (par->output & OUTPUT_PANEL) {
		fp_pm = readl(par->df_regs + DF_FP_PM);
		if (blank_mode == FB_BLANK_POWERDOWN)
			fp_pm &= ~DF_FP_PM_P;
		else
			fp_pm |= DF_FP_PM_P;
		writel(fp_pm, par->df_regs + DF_FP_PM);
	}

	return 0;
}
