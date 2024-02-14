/* $Id: g364fb.c,v 1.3 1998/08/28 22:43:00 tsbogend Exp $
 *
 * linux/drivers/video/g364fb.c -- Mips Magnum frame buffer device
 *
 * (C) 1998 Thomas Bogendoerfer
 *
 *  This driver is based on tgafb.c
 *
 *	Copyright (C) 1997 Geert Uytterhoeven 
 *	Copyright (C) 1995  Jay Estabrook
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/jazz.h>

/* 
 * Various defines for the G364
 */
#define G364_MEM_BASE   0xe4400000
#define G364_PORT_BASE  0xe4000000
#define ID_REG 		0xe4000000	/* Read only */
#define BOOT_REG 	0xe4080000
#define TIMING_REG 	0xe4080108	/* to 0x080170 - DON'T TOUCH! */
#define DISPLAY_REG 	0xe4080118
#define VDISPLAY_REG 	0xe4080150
#define MASK_REG 	0xe4080200
#define CTLA_REG 	0xe4080300
#define CURS_TOGGLE 	0x800000
#define BIT_PER_PIX	0x700000	/* bits 22 to 20 of Control A */
#define DELAY_SAMPLE    0x080000
#define PORT_INTER	0x040000
#define PIX_PIPE_DEL	0x030000	/* bits 17 and 16 of Control A */
#define PIX_PIPE_DEL2	0x008000	/* same as above - don't ask me why */
#define TR_CYCLE_TOG	0x004000
#define VRAM_ADR_INC	0x003000	/* bits 13 and 12 of Control A */
#define BLANK_OFF	0x000800
#define FORCE_BLANK	0x000400
#define BLK_FUN_SWTCH	0x000200
#define BLANK_IO	0x000100
#define BLANK_LEVEL	0x000080
#define A_VID_FORM	0x000040
#define D_SYNC_FORM	0x000020
#define FRAME_FLY_PAT	0x000010
#define OP_MODE		0x000008
#define INTL_STAND	0x000004
#define SCRN_FORM	0x000002
#define ENABLE_VTG	0x000001
#define TOP_REG 	0xe4080400
#define CURS_PAL_REG 	0xe4080508	/* to 0x080518 */
#define CHKSUM_REG 	0xe4080600	/* to 0x080610 - unused */
#define CURS_POS_REG 	0xe4080638
#define CLR_PAL_REG 	0xe4080800	/* to 0x080ff8 */
#define CURS_PAT_REG 	0xe4081000	/* to 0x081ff8 */
#define MON_ID_REG 	0xe4100000	/* unused */
#define RESET_REG 	0xe4180000	/* Write only */

static struct fb_info fb_info;

static struct fb_fix_screeninfo fb_fix __initdata = {
	.id 		= "G364 8plane",
	.smem_start 	= 0x40000000,	/* physical address */
	.type 		= FB_TYPE_PACKED_PIXELS,
	.visual 	= FB_VISUAL_PSEUDOCOLOR,
	.ypanstep 	= 1,
	.accel 		= FB_ACCEL_NONE,
};

static struct fb_var_screeninfo fb_var __initdata = {
	.bits_per_pixel = 8,
	.red 		= { 0, 8, 0 },
      	.green 		= { 0, 8, 0 },
      	.blue		= { 0, 8, 0 },
      	.activate	= FB_ACTIVATE_NOW,
      	.height		= -1,
      	.width		= -1,
      	.pixclock	= 39722,
      	.left_margin	= 40,
      	.right_margin	= 24,
      	.upper_margin	= 32,
      	.lower_margin	= 11,
      	.hsync_len 	= 96,
      	.vsync_len 	= 2,
      	.vmode		= FB_VMODE_NONINTERLACED,
};

/*
 *  Interface used by the world
 */
int g364fb_init(void);

static int g364fb_pan_display(struct fb_var_screeninfo *var,
			      struct fb_info *info);
static int g364fb_setcolreg(u_int regno, u_int red, u_int green,
			    u_int blue, u_int transp,
			    struct fb_info *info);
static int g364fb_blank(int blank, struct fb_info *info);

static const struct fb_ops g364fb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= g364fb_setcolreg,
	.fb_pan_display	= g364fb_pan_display,
	.fb_blank	= g364fb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

/*
 *  Pan or Wrap the Display
 *
 *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
 */
static int g364fb_pan_display(struct fb_var_screeninfo *var, 
			      struct fb_info *info)
{
	if (var->xoffset ||
	    var->yoffset + info->var.yres > info->var.yres_virtual)
		return -EINVAL;

	*(unsigned int *) TOP_REG = var->yoffset * info->var.xres;
	return 0;
}

/*
 *  Blank the display.
 */
static int g364fb_blank(int blank, struct fb_info *info)
{
	if (blank)
		*(unsigned int *) CTLA_REG |= FORCE_BLANK;
	else
		*(unsigned int *) CTLA_REG &= ~FORCE_BLANK;
	return 0;
}

/*
 *  Set a single color register. Return != 0 for invalid regno.
 */
static int g364fb_setcolreg(u_int regno, u_int red, u_int green,
			    u_int blue, u_int transp, struct fb_info *info)
{
	volatile unsigned int *ptr = (volatile unsigned int *) CLR_PAL_REG;

	if (regno > 255)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;

	ptr[regno << 1] = (red << 16) | (green << 8) | blue;

	return 0;
}

/*
 *  Initialisation
 */
int __init g364fb_init(void)
{
	volatile unsigned int *curs_pal_ptr =
	    (volatile unsigned int *) CURS_PAL_REG;
	int mem, i;

	if (fb_get_options("g364fb", NULL))
		return -ENODEV;

	/* TBD: G364 detection */

	/* get the resolution set by ARC console */
	*(volatile unsigned int *) CTLA_REG &= ~ENABLE_VTG;
	fb_var.xres =
	    (*((volatile unsigned int *) DISPLAY_REG) & 0x00ffffff) * 4;
	fb_var.yres =
	    (*((volatile unsigned int *) VDISPLAY_REG) & 0x00ffffff) / 2;
	*(volatile unsigned int *) CTLA_REG |= ENABLE_VTG;

	/* setup cursor */
	curs_pal_ptr[0] |= 0x00ffffff;
	curs_pal_ptr[2] |= 0x00ffffff;
	curs_pal_ptr[4] |= 0x00ffffff;

	/*
	 * first set the whole cursor to transparent
	 */
	for (i = 0; i < 512; i++)
		*(unsigned short *) (CURS_PAT_REG + i * 8) = 0;

	/*
	 * switch the last two lines to cursor palette 3
	 * we assume here, that FONTSIZE_X is 8
	 */
	*(unsigned short *) (CURS_PAT_REG + 14 * 64) = 0xffff;
	*(unsigned short *) (CURS_PAT_REG + 15 * 64) = 0xffff;
	fb_var.xres_virtual = fb_var.xres;
	fb_fix.line_length = fb_var.xres_virtual * fb_var.bits_per_pixel / 8;
	fb_fix.smem_start = 0x40000000;	/* physical address */
	/* get size of video memory; this is special for the JAZZ hardware */
	mem = (r4030_read_reg32(JAZZ_R4030_CONFIG) >> 8) & 3;
	fb_fix.smem_len = (1 << (mem * 2)) * 512 * 1024;
	fb_var.yres_virtual = fb_fix.smem_len / fb_var.xres;

	fb_info.fbops = &g364fb_ops;
	fb_info.screen_base = (char *) G364_MEM_BASE;	/* virtual kernel address */
	fb_info.var = fb_var;
	fb_info.fix = fb_fix;
	fb_info.flags = FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN;

	fb_alloc_cmap(&fb_info.cmap, 255, 0);

	if (register_framebuffer(&fb_info) < 0)
		return -EINVAL;
	return 0;
}

module_init(g364fb_init);
MODULE_LICENSE("GPL");
