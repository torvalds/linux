/*
 * Geode GX display controller.
 *
 *   Copyright (C) 2005 Arcom Control Systems Ltd.
 *
 *   Portions from AMD's original 2.4 driver:
 *     Copyright (C) 2004 Advanced Micro Devices, Inc.
 *
 *   This program is free software; you can redistribute it and/or modify it
 *   under the terms of the GNU General Public License as published by * the
 *   Free Software Foundation; either version 2 of the License, or * (at your
 *   option) any later version.
 */
#include <linux/spinlock.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/div64.h>
#include <asm/delay.h>

#include "geodefb.h"
#include "display_gx.h"

unsigned int gx_frame_buffer_size(void)
{
	unsigned int val;

	/* FB size is reported by a virtual register */
	/* Virtual register class = 0x02 */
	/* VG_MEM_SIZE(512Kb units) = 0x00 */

	outw(0xFC53, 0xAC1C);
	outw(0x0200, 0xAC1C);

	val = (unsigned int)(inw(0xAC1E)) & 0xFFl;
	return (val << 19);
}

int gx_line_delta(int xres, int bpp)
{
	/* Must be a multiple of 8 bytes. */
	return (xres * (bpp >> 3) + 7) & ~0x7;
}

static void gx_set_mode(struct fb_info *info)
{
	struct geodefb_par *par = info->par;
	u32 gcfg, dcfg;
	int hactive, hblankstart, hsyncstart, hsyncend, hblankend, htotal;
	int vactive, vblankstart, vsyncstart, vsyncend, vblankend, vtotal;

	/* Unlock the display controller registers. */
	readl(par->dc_regs + DC_UNLOCK);
	writel(DC_UNLOCK_CODE, par->dc_regs + DC_UNLOCK);

	gcfg = readl(par->dc_regs + DC_GENERAL_CFG);
	dcfg = readl(par->dc_regs + DC_DISPLAY_CFG);

	/* Disable the timing generator. */
	dcfg &= ~(DC_DCFG_TGEN);
	writel(dcfg, par->dc_regs + DC_DISPLAY_CFG);

	/* Wait for pending memory requests before disabling the FIFO load. */
	udelay(100);

	/* Disable FIFO load and compression. */
	gcfg &= ~(DC_GCFG_DFLE | DC_GCFG_CMPE | DC_GCFG_DECE);
	writel(gcfg, par->dc_regs + DC_GENERAL_CFG);

	/* Setup DCLK and its divisor. */
	par->vid_ops->set_dclk(info);

	/*
	 * Setup new mode.
	 */

	/* Clear all unused feature bits. */
	gcfg &= DC_GCFG_YUVM | DC_GCFG_VDSE;
	dcfg = 0;

	/* Set FIFO priority (default 6/5) and enable. */
	/* FIXME: increase fifo priority for 1280x1024 and higher modes? */
	gcfg |= (6 << DC_GCFG_DFHPEL_POS) | (5 << DC_GCFG_DFHPSL_POS) | DC_GCFG_DFLE;

	/* Framebuffer start offset. */
	writel(0, par->dc_regs + DC_FB_ST_OFFSET);

	/* Line delta and line buffer length. */
	writel(info->fix.line_length >> 3, par->dc_regs + DC_GFX_PITCH);
	writel(((info->var.xres * info->var.bits_per_pixel/8) >> 3) + 2,
	       par->dc_regs + DC_LINE_SIZE);

	/* Enable graphics and video data and unmask address lines. */
	dcfg |= DC_DCFG_GDEN | DC_DCFG_VDEN | DC_DCFG_A20M | DC_DCFG_A18M;

	/* Set pixel format. */
	switch (info->var.bits_per_pixel) {
	case 8:
		dcfg |= DC_DCFG_DISP_MODE_8BPP;
		break;
	case 16:
		dcfg |= DC_DCFG_DISP_MODE_16BPP;
		dcfg |= DC_DCFG_16BPP_MODE_565;
		break;
	case 32:
		dcfg |= DC_DCFG_DISP_MODE_24BPP;
		dcfg |= DC_DCFG_PALB;
		break;
	}

	/* Enable timing generator. */
	dcfg |= DC_DCFG_TGEN;

	/* Horizontal and vertical timings. */
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

	writel((hactive - 1)     | ((htotal - 1) << 16),    par->dc_regs + DC_H_ACTIVE_TIMING);
	writel((hblankstart - 1) | ((hblankend - 1) << 16), par->dc_regs + DC_H_BLANK_TIMING);
	writel((hsyncstart - 1)  | ((hsyncend - 1) << 16),  par->dc_regs + DC_H_SYNC_TIMING);

	writel((vactive - 1)     | ((vtotal - 1) << 16),    par->dc_regs + DC_V_ACTIVE_TIMING);
	writel((vblankstart - 1) | ((vblankend - 1) << 16), par->dc_regs + DC_V_BLANK_TIMING);
	writel((vsyncstart - 1)  | ((vsyncend - 1) << 16),  par->dc_regs + DC_V_SYNC_TIMING);

	/* Write final register values. */
	writel(dcfg, par->dc_regs + DC_DISPLAY_CFG);
	writel(gcfg, par->dc_regs + DC_GENERAL_CFG);

	par->vid_ops->configure_display(info);

	/* Relock display controller registers */
	writel(0, par->dc_regs + DC_UNLOCK);
}

static void gx_set_hw_palette_reg(struct fb_info *info, unsigned regno,
				   unsigned red, unsigned green, unsigned blue)
{
	struct geodefb_par *par = info->par;
	int val;

	/* Hardware palette is in RGB 8-8-8 format. */
	val  = (red   << 8) & 0xff0000;
	val |= (green)      & 0x00ff00;
	val |= (blue  >> 8) & 0x0000ff;

	writel(regno, par->dc_regs + DC_PAL_ADDRESS);
	writel(val, par->dc_regs + DC_PAL_DATA);
}

struct geode_dc_ops gx_dc_ops = {
	.set_mode	 = gx_set_mode,
	.set_palette_reg = gx_set_hw_palette_reg,
};
