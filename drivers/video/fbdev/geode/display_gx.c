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
#include <linux/cs5535.h>

#include "gxfb.h"

unsigned int gx_frame_buffer_size(void)
{
	unsigned int val;

	if (!cs5535_has_vsa2()) {
		uint32_t hi, lo;

		/* The number of pages is (PMAX - PMIN)+1 */
		rdmsr(MSR_GLIU_P2D_RO0, lo, hi);

		/* PMAX */
		val = ((hi & 0xff) << 12) | ((lo & 0xfff00000) >> 20);
		/* PMIN */
		val -= (lo & 0x000fffff);
		val += 1;

		/* The page size is 4k */
		return (val << 12);
	}

	/* FB size can be obtained from the VSA II */
	/* Virtual register class = 0x02 */
	/* VG_MEM_SIZE(512Kb units) = 0x00 */

	outw(VSA_VR_UNLOCK, VSA_VRC_INDEX);
	outw(VSA_VR_MEM_SIZE, VSA_VRC_INDEX);

	val = (unsigned int)(inw(VSA_VRC_DATA)) & 0xFFl;
	return (val << 19);
}

int gx_line_delta(int xres, int bpp)
{
	/* Must be a multiple of 8 bytes. */
	return (xres * (bpp >> 3) + 7) & ~0x7;
}

void gx_set_mode(struct fb_info *info)
{
	struct gxfb_par *par = info->par;
	u32 gcfg, dcfg;
	int hactive, hblankstart, hsyncstart, hsyncend, hblankend, htotal;
	int vactive, vblankstart, vsyncstart, vsyncend, vblankend, vtotal;

	/* Unlock the display controller registers. */
	write_dc(par, DC_UNLOCK, DC_UNLOCK_UNLOCK);

	gcfg = read_dc(par, DC_GENERAL_CFG);
	dcfg = read_dc(par, DC_DISPLAY_CFG);

	/* Disable the timing generator. */
	dcfg &= ~DC_DISPLAY_CFG_TGEN;
	write_dc(par, DC_DISPLAY_CFG, dcfg);

	/* Wait for pending memory requests before disabling the FIFO load. */
	udelay(100);

	/* Disable FIFO load and compression. */
	gcfg &= ~(DC_GENERAL_CFG_DFLE | DC_GENERAL_CFG_CMPE |
			DC_GENERAL_CFG_DECE);
	write_dc(par, DC_GENERAL_CFG, gcfg);

	/* Setup DCLK and its divisor. */
	gx_set_dclk_frequency(info);

	/*
	 * Setup new mode.
	 */

	/* Clear all unused feature bits. */
	gcfg &= DC_GENERAL_CFG_YUVM | DC_GENERAL_CFG_VDSE;
	dcfg = 0;

	/* Set FIFO priority (default 6/5) and enable. */
	/* FIXME: increase fifo priority for 1280x1024 and higher modes? */
	gcfg |= (6 << DC_GENERAL_CFG_DFHPEL_SHIFT) |
		(5 << DC_GENERAL_CFG_DFHPSL_SHIFT) | DC_GENERAL_CFG_DFLE;

	/* Framebuffer start offset. */
	write_dc(par, DC_FB_ST_OFFSET, 0);

	/* Line delta and line buffer length. */
	write_dc(par, DC_GFX_PITCH, info->fix.line_length >> 3);
	write_dc(par, DC_LINE_SIZE,
		((info->var.xres * info->var.bits_per_pixel/8) >> 3) + 2);


	/* Enable graphics and video data and unmask address lines. */
	dcfg |= DC_DISPLAY_CFG_GDEN | DC_DISPLAY_CFG_VDEN |
		DC_DISPLAY_CFG_A20M | DC_DISPLAY_CFG_A18M;

	/* Set pixel format. */
	switch (info->var.bits_per_pixel) {
	case 8:
		dcfg |= DC_DISPLAY_CFG_DISP_MODE_8BPP;
		break;
	case 16:
		dcfg |= DC_DISPLAY_CFG_DISP_MODE_16BPP;
		break;
	case 32:
		dcfg |= DC_DISPLAY_CFG_DISP_MODE_24BPP;
		dcfg |= DC_DISPLAY_CFG_PALB;
		break;
	}

	/* Enable timing generator. */
	dcfg |= DC_DISPLAY_CFG_TGEN;

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

	write_dc(par, DC_H_ACTIVE_TIMING, (hactive - 1)    |
			((htotal - 1) << 16));
	write_dc(par, DC_H_BLANK_TIMING, (hblankstart - 1) |
			((hblankend - 1) << 16));
	write_dc(par, DC_H_SYNC_TIMING, (hsyncstart - 1)   |
			((hsyncend - 1) << 16));

	write_dc(par, DC_V_ACTIVE_TIMING, (vactive - 1)    |
			((vtotal - 1) << 16));
	write_dc(par, DC_V_BLANK_TIMING, (vblankstart - 1) |
			((vblankend - 1) << 16));
	write_dc(par, DC_V_SYNC_TIMING, (vsyncstart - 1)   |
			((vsyncend - 1) << 16));

	/* Write final register values. */
	write_dc(par, DC_DISPLAY_CFG, dcfg);
	write_dc(par, DC_GENERAL_CFG, gcfg);

	gx_configure_display(info);

	/* Relock display controller registers */
	write_dc(par, DC_UNLOCK, DC_UNLOCK_LOCK);
}

void gx_set_hw_palette_reg(struct fb_info *info, unsigned regno,
		unsigned red, unsigned green, unsigned blue)
{
	struct gxfb_par *par = info->par;
	int val;

	/* Hardware palette is in RGB 8-8-8 format. */
	val  = (red   << 8) & 0xff0000;
	val |= (green)      & 0x00ff00;
	val |= (blue  >> 8) & 0x0000ff;

	write_dc(par, DC_PAL_ADDRESS, regno);
	write_dc(par, DC_PAL_DATA, val);
}
