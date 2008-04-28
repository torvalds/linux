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
  { 0x000131AC,   6231 },
  { 0x0001215D,   6294 },
  { 0x00011087,   6750 },
  { 0x0001216C,   7081 },
  { 0x0001218D,   7140 },
  { 0x000110C9,   7800 },
  { 0x00013147,   7875 },
  { 0x000110A7,   8258 },
  { 0x00012159,   8778 },
  { 0x00014249,   8875 },
  { 0x00010057,   9000 },
  { 0x0001219A,   9472 },
  { 0x00012158,   9792 },
  { 0x00010045,  10000 },
  { 0x00010089,  10791 },
  { 0x000110E7,  11225 },
  { 0x00012136,  11430 },
  { 0x00013207,  12375 },
  { 0x00012187,  12500 },
  { 0x00014286,  14063 },
  { 0x000110E5,  15016 },
  { 0x00014214,  16250 },
  { 0x00011105,  17045 },
  { 0x000131E4,  18563 },
  { 0x00013183,  18750 },
  { 0x00014284,  19688 },
  { 0x00011104,  20400 },
  { 0x00016363,  23625 },
  { 0x00015303,  24380 },
  { 0x000031AC,  24923 },
  { 0x0000215D,  25175 },
  { 0x00001087,  27000 },
  { 0x0000216C,  28322 },
  { 0x0000218D,  28560 },
  { 0x00010041,  29913 },
  { 0x000010C9,  31200 },
  { 0x00003147,  31500 },
  { 0x000141A1,  32400 },
  { 0x000010A7,  33032 },
  { 0x00012182,  33375 },
  { 0x000141B1,  33750 },
  { 0x00002159,  35112 },
  { 0x00004249,  35500 },
  { 0x00000057,  36000 },
  { 0x000141E1,  37125 },
  { 0x0000219A,  37889 },
  { 0x00002158,  39168 },
  { 0x00000045,  40000 },
  { 0x000131A1,  40500 },
  { 0x00010061,  42301 },
  { 0x00000089,  43163 },
  { 0x00012151,  43875 },
  { 0x000010E7,  44900 },
  { 0x00002136,  45720 },
  { 0x000152E1,  47250 },
  { 0x00010071,  48000 },
  { 0x00003207,  49500 },
  { 0x00002187,  50000 },
  { 0x00014291,  50625 },
  { 0x00011101,  51188 },
  { 0x00017481,  54563 },
  { 0x00004286,  56250 },
  { 0x00014170,  57375 },
  { 0x00016210,  58500 },
  { 0x000010E5,  60065 },
  { 0x00013140,  62796 },
  { 0x00004214,  65000 },
  { 0x00016250,  65250 },
  { 0x00001105,  68179 },
  { 0x000141C0,  69600 },
  { 0x00015220,  70160 },
  { 0x00010050,  72000 },
  { 0x000031E4,  74250 },
  { 0x00003183,  75000 },
  { 0x00004284,  78750 },
  { 0x00012130,  80052 },
  { 0x00001104,  81600 },
  { 0x00006363,  94500 },
  { 0x00005303,  97520 },
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

	freq = (unsigned int) (1000000000 / info->var.pixclock);

	min = abs(pll_table[0].freq - freq);

	for (i = 0; i < ARRAY_SIZE(pll_table); i++) {
		diff = abs(pll_table[i].freq - freq);
		if (diff < min) {
			min = diff;
			best = i;
		}
	}

	lx_set_dotpll(pll_table[best].pllval & 0x00017FFF);
}

static void lx_graphics_disable(struct fb_info *info)
{
	struct lxfb_par *par = info->par;
	unsigned int val, gcfg;

	/* Note:  This assumes that the video is in a quitet state */

	write_vp(par, VP_A1T, 0);
	write_vp(par, VP_A2T, 0);
	write_vp(par, VP_A3T, 0);

	/* Turn off the VGA and video enable */
	val = read_dc(par, DC_GENERAL_CFG) & ~(DC_GENERAL_CFG_VGAE |
			DC_GENERAL_CFG_VIDE);

	write_dc(par, DC_GENERAL_CFG, val);

	val = read_vp(par, VP_VCFG) & ~VP_VCFG_VID_EN;
	write_vp(par, VP_VCFG, val);

	write_dc(par, DC_IRQ, DC_IRQ_MASK | DC_IRQ_VIP_VSYNC_LOSS_IRQ_MASK |
			DC_IRQ_STATUS | DC_IRQ_VIP_VSYNC_IRQ_STATUS);

	val = read_dc(par, DC_GENLK_CTL) & ~DC_GENLK_CTL_GENLK_EN;
	write_dc(par, DC_GENLK_CTL, val);

	val = read_dc(par, DC_CLR_KEY);
	write_dc(par, DC_CLR_KEY, val & ~DC_CLR_KEY_CLR_KEY_EN);

	/* We don't actually blank the panel, due to the long latency
	   involved with bringing it back */

	val = read_vp(par, VP_MISC) | VP_MISC_DACPWRDN;
	write_vp(par, VP_MISC, val);

	/* Turn off the display */

	val = read_vp(par, VP_DCFG);
	write_vp(par, VP_DCFG, val & ~(VP_DCFG_CRT_EN | VP_DCFG_HSYNC_EN |
			VP_DCFG_VSYNC_EN | VP_DCFG_DAC_BL_EN));

	gcfg = read_dc(par, DC_GENERAL_CFG);
	gcfg &= ~(DC_GENERAL_CFG_CMPE | DC_GENERAL_CFG_DECE);
	write_dc(par, DC_GENERAL_CFG, gcfg);

	/* Turn off the TGEN */
	val = read_dc(par, DC_DISPLAY_CFG);
	val &= ~DC_DISPLAY_CFG_TGEN;
	write_dc(par, DC_DISPLAY_CFG, val);

	/* Wait 1000 usecs to ensure that the TGEN is clear */
	udelay(1000);

	/* Turn off the FIFO loader */

	gcfg &= ~DC_GENERAL_CFG_DFLE;
	write_dc(par, DC_GENERAL_CFG, gcfg);

	/* Lastly, wait for the GP to go idle */

	do {
		val = read_gp(par, GP_BLT_STATUS);
	} while ((val & GP_BLT_STATUS_PB) || !(val & GP_BLT_STATUS_CE));
}

static void lx_graphics_enable(struct fb_info *info)
{
	struct lxfb_par *par = info->par;
	u32 temp, config;

	/* Set the video request register */
	write_vp(par, VP_VRR, 0);

	/* Set up the polarities */

	config = read_vp(par, VP_DCFG);

	config &= ~(VP_DCFG_CRT_SYNC_SKW | VP_DCFG_PWR_SEQ_DELAY |
			VP_DCFG_CRT_HSYNC_POL | VP_DCFG_CRT_VSYNC_POL);

	config |= (VP_DCFG_CRT_SYNC_SKW_DEFAULT | VP_DCFG_PWR_SEQ_DELAY_DEFAULT
			| VP_DCFG_GV_GAM);

	if (info->var.sync & FB_SYNC_HOR_HIGH_ACT)
		config |= VP_DCFG_CRT_HSYNC_POL;

	if (info->var.sync & FB_SYNC_VERT_HIGH_ACT)
		config |= VP_DCFG_CRT_VSYNC_POL;

	if (par->output & OUTPUT_PANEL) {
		u32 msrlo, msrhi;

		write_fp(par, FP_PT1, 0);
		write_fp(par, FP_PT2, FP_PT2_SCRC);
		write_fp(par, FP_DFC, FP_DFC_BC);

		msrlo = DF_DEFAULT_TFT_PAD_SEL_LOW;
		msrhi = DF_DEFAULT_TFT_PAD_SEL_HIGH;

		wrmsr(MSR_LX_MSR_PADSEL, msrlo, msrhi);
	}

	if (par->output & OUTPUT_CRT) {
		config |= VP_DCFG_CRT_EN | VP_DCFG_HSYNC_EN |
				VP_DCFG_VSYNC_EN | VP_DCFG_DAC_BL_EN;
	}

	write_vp(par, VP_DCFG, config);

	/* Turn the CRT dacs back on */

	if (par->output & OUTPUT_CRT) {
		temp = read_vp(par, VP_MISC);
		temp &= ~(VP_MISC_DACPWRDN | VP_MISC_APWRDN);
		write_vp(par, VP_MISC, temp);
	}

	/* Turn the panel on (if it isn't already) */

	if (par->output & OUTPUT_PANEL) {
		temp = read_fp(par, FP_PM);

		if (!(temp & 0x09))
			write_fp(par, FP_PM, temp | FP_PM_P);
	}
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
	write_dc(par, DC_UNLOCK, DC_UNLOCK_UNLOCK);

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

	write_dc(par, DC_FB_ST_OFFSET, 0);
	write_dc(par, DC_CB_ST_OFFSET, 0);
	write_dc(par, DC_CURS_ST_OFFSET, 0);

	/* FIXME: Add support for interlacing */
	/* FIXME: Add support for scaling */

	val = read_dc(par, DC_GENLK_CTL);
	val &= ~(DC_GENLK_CTL_ALPHA_FLICK_EN | DC_GENLK_CTL_FLICK_EN |
			DC_GENLK_CTL_FLICK_SEL_MASK);

	/* Default scaling params */

	write_dc(par, DC_GFX_SCALE, (0x4000 << 16) | 0x4000);
	write_dc(par, DC_IRQ_FILT_CTL, 0);
	write_dc(par, DC_GENLK_CTL, val);

	/* FIXME:  Support compression */

	if (info->fix.line_length > 4096)
		dv = DC_DV_CTL_DV_LINE_SIZE_8K;
	else if (info->fix.line_length > 2048)
		dv = DC_DV_CTL_DV_LINE_SIZE_4K;
	else if (info->fix.line_length > 1024)
		dv = DC_DV_CTL_DV_LINE_SIZE_2K;
	else
		dv = DC_DV_CTL_DV_LINE_SIZE_1K;

	max = info->fix.line_length * info->var.yres;
	max = (max + 0x3FF) & 0xFFFFFC00;

	write_dc(par, DC_DV_TOP, max | DC_DV_TOP_DV_TOP_EN);

	val = read_dc(par, DC_DV_CTL) & ~DC_DV_CTL_DV_LINE_SIZE;
	write_dc(par, DC_DV_CTL, val | dv);

	size = info->var.xres * (info->var.bits_per_pixel >> 3);

	write_dc(par, DC_GFX_PITCH, info->fix.line_length >> 3);
	write_dc(par, DC_LINE_SIZE, (size + 7) >> 3);

	/* Set default watermark values */

	rdmsrl(MSR_LX_SPARE_MSR, msrval);

	msrval &= ~(DC_SPARE_DISABLE_CFIFO_HGO | DC_SPARE_VFIFO_ARB_SELECT |
		    DC_SPARE_LOAD_WM_LPEN_MASK | DC_SPARE_WM_LPEN_OVRD |
		    DC_SPARE_DISABLE_INIT_VID_PRI | DC_SPARE_DISABLE_VFIFO_WM);
	msrval |= DC_SPARE_DISABLE_VFIFO_WM | DC_SPARE_DISABLE_INIT_VID_PRI;
	wrmsrl(MSR_LX_SPARE_MSR, msrval);

	gcfg = DC_GENERAL_CFG_DFLE;   /* Display fifo enable */
	gcfg |= (0x6 << DC_GENERAL_CFG_DFHPSL_SHIFT) | /* default priority */
			(0xb << DC_GENERAL_CFG_DFHPEL_SHIFT);
	gcfg |= DC_GENERAL_CFG_FDTY;  /* Set the frame dirty mode */

	dcfg  = DC_DISPLAY_CFG_VDEN;  /* Enable video data */
	dcfg |= DC_DISPLAY_CFG_GDEN;  /* Enable graphics */
	dcfg |= DC_DISPLAY_CFG_TGEN;  /* Turn on the timing generator */
	dcfg |= DC_DISPLAY_CFG_TRUP;  /* Update timings immediately */
	dcfg |= DC_DISPLAY_CFG_PALB;  /* Palette bypass in > 8 bpp modes */
	dcfg |= DC_DISPLAY_CFG_VISL;
	dcfg |= DC_DISPLAY_CFG_DCEN;  /* Always center the display */

	/* Set the current BPP mode */

	switch (info->var.bits_per_pixel) {
	case 8:
		dcfg |= DC_DISPLAY_CFG_DISP_MODE_8BPP;
		break;

	case 16:
		dcfg |= DC_DISPLAY_CFG_DISP_MODE_16BPP;
		break;

	case 32:
	case 24:
		dcfg |= DC_DISPLAY_CFG_DISP_MODE_24BPP;
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

	write_dc(par, DC_H_ACTIVE_TIMING, (hactive - 1) | ((htotal - 1) << 16));
	write_dc(par, DC_H_BLANK_TIMING,
			(hblankstart - 1) | ((hblankend - 1) << 16));
	write_dc(par, DC_H_SYNC_TIMING,
			(hsyncstart - 1) | ((hsyncend - 1) << 16));

	write_dc(par, DC_V_ACTIVE_TIMING, (vactive - 1) | ((vtotal - 1) << 16));
	write_dc(par, DC_V_BLANK_TIMING,
			(vblankstart - 1) | ((vblankend - 1) << 16));
	write_dc(par, DC_V_SYNC_TIMING,
			(vsyncstart - 1) | ((vsyncend - 1) << 16));

	write_dc(par, DC_FB_ACTIVE,
			(info->var.xres - 1) << 16 | (info->var.yres - 1));

	/* And re-enable the graphics output */
	lx_graphics_enable(info);

	/* Write the two main configuration registers */
	write_dc(par, DC_DISPLAY_CFG, dcfg);
	write_dc(par, DC_ARB_CFG, 0);
	write_dc(par, DC_GENERAL_CFG, gcfg);

	/* Lock the DC registers */
	write_dc(par, DC_UNLOCK, DC_UNLOCK_LOCK);
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

	write_dc(par, DC_PAL_ADDRESS, regno);
	write_dc(par, DC_PAL_DATA, val);
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

	dcfg = read_vp(par, VP_DCFG);
	dcfg &= ~(VP_DCFG_DAC_BL_EN | VP_DCFG_HSYNC_EN | VP_DCFG_VSYNC_EN);
	if (!blank)
		dcfg |= VP_DCFG_DAC_BL_EN;
	if (hsync)
		dcfg |= VP_DCFG_HSYNC_EN;
	if (vsync)
		dcfg |= VP_DCFG_VSYNC_EN;
	write_vp(par, VP_DCFG, dcfg);

	/* Power on/off flat panel */

	if (par->output & OUTPUT_PANEL) {
		fp_pm = read_fp(par, FP_PM);
		if (blank_mode == FB_BLANK_POWERDOWN)
			fp_pm &= ~FP_PM_P;
		else
			fp_pm |= FP_PM_P;
		write_fp(par, FP_PM, fp_pm);
	}

	return 0;
}
