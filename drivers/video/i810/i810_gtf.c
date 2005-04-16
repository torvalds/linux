/*-*- linux-c -*-
 *  linux/drivers/video/i810_main.h -- Intel 810 Non-discrete Video Timings 
 *                                     (VESA GTF)
 *
 *      Copyright (C) 2001 Antonino Daplas<adaplas@pol.net>
 *      All Rights Reserved      
 *
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */
#include <linux/kernel.h>

#include "i810_regs.h"
#include "i810.h"

/*
 * FIFO and Watermark tables - based almost wholly on i810_wmark.c in 
 * XFree86 v4.03 by Precision Insight.  Slightly modified for integer 
 * operation, instead of float
 */

struct wm_info {
   u32 freq;
   u32  wm;
};

static struct wm_info i810_wm_8_100[] = {
	{ 15, 0x0070c000 },  { 19, 0x0070c000 },  { 25, 0x22003000 },
	{ 28, 0x22003000 },  { 31, 0x22003000 },  { 36, 0x22007000 },
	{ 40, 0x22007000 },  { 45, 0x22007000 },  { 49, 0x22008000 },
	{ 50, 0x22008000 },  { 56, 0x22008000 },  { 65, 0x22008000 },
	{ 75, 0x22008000 },  { 78, 0x22008000 },  { 80, 0x22008000 },
	{ 94, 0x22008000 },  { 96, 0x22107000 },  { 99, 0x22107000 },
	{ 108, 0x22107000 }, { 121, 0x22107000 }, { 128, 0x22107000 },
	{ 132, 0x22109000 }, { 135, 0x22109000 }, { 157, 0x2210b000 },
	{ 162, 0x2210b000 }, { 175, 0x2210b000 }, { 189, 0x2220e000 },
	{ 195, 0x2220e000 }, { 202, 0x2220e000 }, { 204, 0x2220e000 },
	{ 218, 0x2220f000 }, { 229, 0x22210000 }, { 234, 0x22210000 }, 
};

static struct wm_info i810_wm_16_100[] = {
	{ 15, 0x0070c000 },  { 19, 0x0020c000 },  { 25, 0x22006000 },
	{ 28, 0x22006000 },  { 31, 0x22007000 },  { 36, 0x22007000 },
	{ 40, 0x22007000 },  { 45, 0x22007000 },  { 49, 0x22009000 },
	{ 50, 0x22009000 },  { 56, 0x22108000 },  { 65, 0x2210e000 },
	{ 75, 0x2210e000 },  { 78, 0x2210e000 },  { 80, 0x22210000 },
	{ 94, 0x22210000 },  { 96, 0x22210000 },  { 99, 0x22210000 },
	{ 108, 0x22210000 }, { 121, 0x22210000 }, { 128, 0x22210000 },
	{ 132, 0x22314000 }, { 135, 0x22314000 }, { 157, 0x22415000 },
	{ 162, 0x22416000 }, { 175, 0x22416000 }, { 189, 0x22416000 },
	{ 195, 0x22416000 }, { 202, 0x22416000 }, { 204, 0x22416000 },
	{ 218, 0x22416000 }, { 229, 0x22416000 },
};

static struct wm_info i810_wm_24_100[] = {
	{ 15, 0x0020c000 },  { 19, 0x0040c000 },  { 25, 0x22009000 },
	{ 28, 0x22009000 },  { 31, 0x2200a000 },  { 36, 0x2210c000 },
	{ 40, 0x2210c000 },  { 45, 0x2210c000 },  { 49, 0x22111000 },
	{ 50, 0x22111000 },  { 56, 0x22111000 },  { 65, 0x22214000 },
	{ 75, 0x22214000 },  { 78, 0x22215000 },  { 80, 0x22216000 },
	{ 94, 0x22218000 },  { 96, 0x22418000 },  { 99, 0x22418000 },
	{ 108, 0x22418000 }, { 121, 0x22418000 }, { 128, 0x22419000 },
	{ 132, 0x22519000 }, { 135, 0x4441d000 }, { 157, 0x44419000 },
	{ 162, 0x44419000 }, { 175, 0x44419000 }, { 189, 0x44419000 },
	{ 195, 0x44419000 }, { 202, 0x44419000 }, { 204, 0x44419000 },
};

static struct wm_info i810_wm_8_133[] = {
	{ 15, 0x0070c000 },  { 19, 0x0070c000 },  { 25, 0x22003000 },
	{ 28, 0x22003000 },  { 31, 0x22003000 },  { 36, 0x22007000 },
	{ 40, 0x22007000 },  { 45, 0x22007000 },  { 49, 0x22008000 },
	{ 50, 0x22008000 },  { 56, 0x22008000 },  { 65, 0x22008000 },
	{ 75, 0x22008000 },  { 78, 0x22008000 },  { 80, 0x22008000 },
	{ 94, 0x22008000 },  { 96, 0x22107000 },  { 99, 0x22107000 },
	{ 108, 0x22107000 }, { 121, 0x22107000 }, { 128, 0x22107000 },
	{ 132, 0x22109000 }, { 135, 0x22109000 }, { 157, 0x2210b000 },
	{ 162, 0x2210b000 }, { 175, 0x2210b000 }, { 189, 0x2220e000 },
	{ 195, 0x2220e000 }, { 202, 0x2220e000 }, { 204, 0x2220e000 },
	{ 218, 0x2220f000 }, { 229, 0x22210000 }, { 234, 0x22210000 }, 
};

static struct wm_info i810_wm_16_133[] = {
	{ 15, 0x0020c000 },  { 19, 0x0020c000 },  { 25, 0x22006000 },
	{ 28, 0x22006000 },  { 31, 0x22007000 },  { 36, 0x22007000 },
	{ 40, 0x22007000 },  { 45, 0x22007000 },  { 49, 0x22009000 },
	{ 50, 0x22009000 },  { 56, 0x22108000 },  { 65, 0x2210e000 },
	{ 75, 0x2210e000 },  { 78, 0x2210e000 },  { 80, 0x22210000 },
	{ 94, 0x22210000 },  { 96, 0x22210000 },  { 99, 0x22210000 },
	{ 108, 0x22210000 }, { 121, 0x22210000 }, { 128, 0x22210000 },
	{ 132, 0x22314000 }, { 135, 0x22314000 }, { 157, 0x22415000 },
	{ 162, 0x22416000 }, { 175, 0x22416000 }, { 189, 0x22416000 },
	{ 195, 0x22416000 }, { 202, 0x22416000 }, { 204, 0x22416000 },
	{ 218, 0x22416000 }, { 229, 0x22416000 },
};

static struct wm_info i810_wm_24_133[] = {
	{ 15, 0x0020c000 },  { 19, 0x00408000 },  { 25, 0x22009000 },
	{ 28, 0x22009000 },  { 31, 0x2200a000 },  { 36, 0x2210c000 },
	{ 40, 0x2210c000 },  { 45, 0x2210c000 },  { 49, 0x22111000 },
	{ 50, 0x22111000 },  { 56, 0x22111000 },  { 65, 0x22214000 },
	{ 75, 0x22214000 },  { 78, 0x22215000 },  { 80, 0x22216000 },
	{ 94, 0x22218000 },  { 96, 0x22418000 },  { 99, 0x22418000 },
	{ 108, 0x22418000 }, { 121, 0x22418000 }, { 128, 0x22419000 },
	{ 132, 0x22519000 }, { 135, 0x4441d000 }, { 157, 0x44419000 },
	{ 162, 0x44419000 }, { 175, 0x44419000 }, { 189, 0x44419000 },
	{ 195, 0x44419000 }, { 202, 0x44419000 }, { 204, 0x44419000 },
};

void round_off_xres(u32 *xres) { }
void round_off_yres(u32 *xres, u32 *yres) { }

/**
 * i810fb_encode_registers - encode @var to hardware register values
 * @var: pointer to var structure
 * @par: pointer to hardware par structure
 * 
 * DESCRIPTION: 
 * Timing values in @var will be converted to appropriate
 * register values of @par.  
 */
void i810fb_encode_registers(const struct fb_var_screeninfo *var,
			     struct i810fb_par *par, u32 xres, u32 yres)
{
	int n, blank_s, blank_e;
	u8 __iomem *mmio = par->mmio_start_virtual;
	u8 msr = 0;

	/* Horizontal */
	/* htotal */
	n = ((xres + var->right_margin + var->hsync_len + 
	      var->left_margin) >> 3) - 5;
	par->regs.cr00 =  (u8) n;
	par->regs.cr35 = (u8) ((n >> 8) & 1);
	
	/* xres */
	par->regs.cr01 = (u8) ((xres >> 3) - 1);

	/* hblank */
	blank_e = (xres + var->right_margin + var->hsync_len + 
		   var->left_margin) >> 3;
	blank_e--;
	blank_s = blank_e - 127;
	if (blank_s < (xres >> 3))
		blank_s = xres >> 3;
	par->regs.cr02 = (u8) blank_s;
	par->regs.cr03 = (u8) (blank_e & 0x1F);
	par->regs.cr05 = (u8) ((blank_e & (1 << 5)) << 2);
	par->regs.cr39 = (u8) ((blank_e >> 6) & 1);

	/* hsync */
	par->regs.cr04 = (u8) ((xres + var->right_margin) >> 3);
	par->regs.cr05 |= (u8) (((xres + var->right_margin + 
				  var->hsync_len) >> 3) & 0x1F);
	
       	/* Vertical */
	/* vtotal */
	n = yres + var->lower_margin + var->vsync_len + var->upper_margin - 2;
	par->regs.cr06 = (u8) (n & 0xFF);
	par->regs.cr30 = (u8) ((n >> 8) & 0x0F);

	/* vsync */ 
	n = yres + var->lower_margin;
	par->regs.cr10 = (u8) (n & 0xFF);
	par->regs.cr32 = (u8) ((n >> 8) & 0x0F);
	par->regs.cr11 = i810_readb(CR11, mmio) & ~0x0F;
	par->regs.cr11 |= (u8) ((yres + var->lower_margin + 
				 var->vsync_len) & 0x0F);

	/* yres */
	n = yres - 1;
	par->regs.cr12 = (u8) (n & 0xFF);
	par->regs.cr31 = (u8) ((n >> 8) & 0x0F);
	
	/* vblank */
	blank_e = yres + var->lower_margin + var->vsync_len + 
		var->upper_margin;
	blank_e--;
	blank_s = blank_e - 127;
	if (blank_s < yres)
		blank_s = yres;
	par->regs.cr15 = (u8) (blank_s & 0xFF);
	par->regs.cr33 = (u8) ((blank_s >> 8) & 0x0F);
	par->regs.cr16 = (u8) (blank_e & 0xFF);
	par->regs.cr09 = 0;	

	/* sync polarity */
	if (!(var->sync & FB_SYNC_HOR_HIGH_ACT))
		msr |= 1 << 6;
	if (!(var->sync & FB_SYNC_VERT_HIGH_ACT))
		msr |= 1 << 7;
	par->regs.msr = msr;

	/* interlace */
	if (var->vmode & FB_VMODE_INTERLACED) 
		par->interlace = (1 << 7) | ((u8) (var->yres >> 4));
	else 
		par->interlace = 0;

	if (var->vmode & FB_VMODE_DOUBLE)
		par->regs.cr09 |= 1 << 7;

	/* overlay */
	par->ovract = ((var->xres + var->right_margin + var->hsync_len + 
			var->left_margin - 32) | ((var->xres - 32) << 16));
}	

void i810fb_fill_var_timings(struct fb_var_screeninfo *var) { }

/**
 * i810_get_watermark - gets watermark
 * @var: pointer to fb_var_screeninfo
 * @par: pointer to i810fb_par structure
 *
 * DESCRIPTION:
 * Gets the required watermark based on 
 * pixelclock and RAMBUS frequency.
 * 
 * RETURNS:
 * watermark
 */
u32 i810_get_watermark(const struct fb_var_screeninfo *var,
		       struct i810fb_par *par)
{
	struct wm_info *wmark = NULL;
	u32 i, size = 0, pixclock, wm_best = 0, min, diff;

	if (par->mem_freq == 100) {
		switch (var->bits_per_pixel) { 
		case 8:
			wmark = i810_wm_8_100;
			size = ARRAY_SIZE(i810_wm_8_100);
			break;
		case 16:
			wmark = i810_wm_16_100;
			size = ARRAY_SIZE(i810_wm_16_100);
			break;
		case 24:
		case 32:
			wmark = i810_wm_24_100;
			size = ARRAY_SIZE(i810_wm_24_100);
		}
	} else {
		switch(var->bits_per_pixel) {
		case 8:
			wmark = i810_wm_8_133;
			size = ARRAY_SIZE(i810_wm_8_133);
			break;
		case 16:
			wmark = i810_wm_16_133;
			size = ARRAY_SIZE(i810_wm_16_133);
			break;
		case 24:
		case 32:
			wmark = i810_wm_24_133;
			size = ARRAY_SIZE(i810_wm_24_133);
		}
	}

	pixclock = 1000000/var->pixclock;
	min = ~0;
	for (i = 0; i < size; i++) {
		if (pixclock <= wmark[i].freq) 
			diff = wmark[i].freq - pixclock;
		else 
			diff = pixclock - wmark[i].freq;
		if (diff < min) {
			wm_best = wmark[i].wm;
			min = diff;
		}
	}
	return wm_best;		
}	

