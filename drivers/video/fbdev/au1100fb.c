// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BRIEF MODULE DESCRIPTION
 *	Au1100 LCD Driver.
 *
 * Rewritten for 2.6 by Embedded Alley Solutions
 * 	<source@embeddedalley.com>, based on submissions by
 *  	Karl Lessard <klessard@sunrisetelecom.com>
 *  	<c.pellegrin@exadron.com>
 *
 * PM support added by Rodolfo Giometti <giometti@linux.it>
 * Cursor enable/disable by Rodolfo Giometti <giometti@linux.it>
 *
 * Copyright 2002 MontaVista Software
 * Author: MontaVista Software, Inc.
 *		ppopov@mvista.com or source@mvista.com
 *
 * Copyright 2002 Alchemy Semiconductor
 * Author: Alchemy Semiconductor
 *
 * Based on:
 * linux/drivers/video/skeletonfb.c -- Skeleton for a frame buffer device
 *  Created 28 Dec 1997 by Geert Uytterhoeven
 */

#define pr_fmt(fmt) "au1100fb:" fmt "\n"

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ctype.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#if defined(__BIG_ENDIAN)
#define LCD_CONTROL_DEFAULT_PO LCD_CONTROL_PO_11
#else
#define LCD_CONTROL_DEFAULT_PO LCD_CONTROL_PO_00
#endif
#define LCD_CONTROL_DEFAULT_SBPPF LCD_CONTROL_SBPPF_565

/********************************************************************/

/* LCD controller restrictions */
#define AU1100_LCD_MAX_XRES	800
#define AU1100_LCD_MAX_YRES	600
#define AU1100_LCD_MAX_BPP	16
#define AU1100_LCD_MAX_CLK	48000000
#define AU1100_LCD_NBR_PALETTE_ENTRIES 256

/* Default number of visible screen buffer to allocate */
#define AU1100FB_NBR_VIDEO_BUFFERS 4

/********************************************************************/

struct au1100fb_panel
{
	const char name[25];		/* Full name <vendor>_<model> */

	u32   	control_base;		/* Mode-independent control values */
	u32	clkcontrol_base;	/* Panel pixclock preferences */

	u32	horztiming;
	u32	verttiming;

	u32	xres;		/* Maximum horizontal resolution */
	u32 	yres;		/* Maximum vertical resolution */
	u32 	bpp;		/* Maximum depth supported */
};

struct au1100fb_regs
{
	u32  lcd_control;
	u32  lcd_intstatus;
	u32  lcd_intenable;
	u32  lcd_horztiming;
	u32  lcd_verttiming;
	u32  lcd_clkcontrol;
	u32  lcd_dmaaddr0;
	u32  lcd_dmaaddr1;
	u32  lcd_words;
	u32  lcd_pwmdiv;
	u32  lcd_pwmhi;
	u32  reserved[(0x0400-0x002C)/4];
	u32  lcd_palettebase[256];
};

struct au1100fb_device {

	struct fb_info info;			/* FB driver info record */

	struct au1100fb_panel 	*panel;		/* Panel connected to this device */

	struct au1100fb_regs* 	regs;		/* Registers memory map */
	size_t       		regs_len;
	unsigned int 		regs_phys;

#ifdef CONFIG_PM
	/* stores the register values during suspend */
	struct au1100fb_regs	pm_regs;
#endif

	unsigned char* 		fb_mem;		/* FrameBuffer memory map */
	size_t	      		fb_len;
	dma_addr_t    		fb_phys;
	int			panel_idx;
	struct clk		*lcdclk;
	struct device		*dev;
};

/********************************************************************/

#define LCD_CONTROL                (AU1100_LCD_BASE + 0x0)
  #define LCD_CONTROL_SBB_BIT      21
  #define LCD_CONTROL_SBB_MASK     (0x3 << LCD_CONTROL_SBB_BIT)
    #define LCD_CONTROL_SBB_1        (0 << LCD_CONTROL_SBB_BIT)
    #define LCD_CONTROL_SBB_2        (1 << LCD_CONTROL_SBB_BIT)
    #define LCD_CONTROL_SBB_3        (2 << LCD_CONTROL_SBB_BIT)
    #define LCD_CONTROL_SBB_4        (3 << LCD_CONTROL_SBB_BIT)
  #define LCD_CONTROL_SBPPF_BIT    18
  #define LCD_CONTROL_SBPPF_MASK   (0x7 << LCD_CONTROL_SBPPF_BIT)
    #define LCD_CONTROL_SBPPF_655    (0 << LCD_CONTROL_SBPPF_BIT)
    #define LCD_CONTROL_SBPPF_565    (1 << LCD_CONTROL_SBPPF_BIT)
    #define LCD_CONTROL_SBPPF_556    (2 << LCD_CONTROL_SBPPF_BIT)
    #define LCD_CONTROL_SBPPF_1555   (3 << LCD_CONTROL_SBPPF_BIT)
    #define LCD_CONTROL_SBPPF_5551   (4 << LCD_CONTROL_SBPPF_BIT)
  #define LCD_CONTROL_WP           (1<<17)
  #define LCD_CONTROL_WD           (1<<16)
  #define LCD_CONTROL_C            (1<<15)
  #define LCD_CONTROL_SM_BIT       13
  #define LCD_CONTROL_SM_MASK      (0x3 << LCD_CONTROL_SM_BIT)
    #define LCD_CONTROL_SM_0         (0 << LCD_CONTROL_SM_BIT)
    #define LCD_CONTROL_SM_90        (1 << LCD_CONTROL_SM_BIT)
    #define LCD_CONTROL_SM_180       (2 << LCD_CONTROL_SM_BIT)
    #define LCD_CONTROL_SM_270       (3 << LCD_CONTROL_SM_BIT)
  #define LCD_CONTROL_DB           (1<<12)
  #define LCD_CONTROL_CCO          (1<<11)
  #define LCD_CONTROL_DP           (1<<10)
  #define LCD_CONTROL_PO_BIT       8
  #define LCD_CONTROL_PO_MASK      (0x3 << LCD_CONTROL_PO_BIT)
    #define LCD_CONTROL_PO_00        (0 << LCD_CONTROL_PO_BIT)
    #define LCD_CONTROL_PO_01        (1 << LCD_CONTROL_PO_BIT)
    #define LCD_CONTROL_PO_10        (2 << LCD_CONTROL_PO_BIT)
    #define LCD_CONTROL_PO_11        (3 << LCD_CONTROL_PO_BIT)
  #define LCD_CONTROL_MPI          (1<<7)
  #define LCD_CONTROL_PT           (1<<6)
  #define LCD_CONTROL_PC           (1<<5)
  #define LCD_CONTROL_BPP_BIT      1
  #define LCD_CONTROL_BPP_MASK     (0x7 << LCD_CONTROL_BPP_BIT)
    #define LCD_CONTROL_BPP_1        (0 << LCD_CONTROL_BPP_BIT)
    #define LCD_CONTROL_BPP_2        (1 << LCD_CONTROL_BPP_BIT)
    #define LCD_CONTROL_BPP_4        (2 << LCD_CONTROL_BPP_BIT)
    #define LCD_CONTROL_BPP_8        (3 << LCD_CONTROL_BPP_BIT)
    #define LCD_CONTROL_BPP_12       (4 << LCD_CONTROL_BPP_BIT)
    #define LCD_CONTROL_BPP_16       (5 << LCD_CONTROL_BPP_BIT)
  #define LCD_CONTROL_GO           (1<<0)

#define LCD_INTSTATUS              (AU1100_LCD_BASE + 0x4)
#define LCD_INTENABLE              (AU1100_LCD_BASE + 0x8)
  #define LCD_INT_SD               (1<<7)
  #define LCD_INT_OF               (1<<6)
  #define LCD_INT_UF               (1<<5)
  #define LCD_INT_SA               (1<<3)
  #define LCD_INT_SS               (1<<2)
  #define LCD_INT_S1               (1<<1)
  #define LCD_INT_S0               (1<<0)

#define LCD_HORZTIMING             (AU1100_LCD_BASE + 0xC)
  #define LCD_HORZTIMING_HN2_BIT   24
  #define LCD_HORZTIMING_HN2_MASK  (0xFF << LCD_HORZTIMING_HN2_BIT)
  #define LCD_HORZTIMING_HN2_N(N)  ((((N)-1) << LCD_HORZTIMING_HN2_BIT) & LCD_HORZTIMING_HN2_MASK)
  #define LCD_HORZTIMING_HN1_BIT   16
  #define LCD_HORZTIMING_HN1_MASK  (0xFF << LCD_HORZTIMING_HN1_BIT)
  #define LCD_HORZTIMING_HN1_N(N)  ((((N)-1) << LCD_HORZTIMING_HN1_BIT) & LCD_HORZTIMING_HN1_MASK)
  #define LCD_HORZTIMING_HPW_BIT   10
  #define LCD_HORZTIMING_HPW_MASK  (0x3F << LCD_HORZTIMING_HPW_BIT)
  #define LCD_HORZTIMING_HPW_N(N)  ((((N)-1) << LCD_HORZTIMING_HPW_BIT) & LCD_HORZTIMING_HPW_MASK)
  #define LCD_HORZTIMING_PPL_BIT   0
  #define LCD_HORZTIMING_PPL_MASK  (0x3FF << LCD_HORZTIMING_PPL_BIT)
  #define LCD_HORZTIMING_PPL_N(N)  ((((N)-1) << LCD_HORZTIMING_PPL_BIT) & LCD_HORZTIMING_PPL_MASK)

#define LCD_VERTTIMING             (AU1100_LCD_BASE + 0x10)
  #define LCD_VERTTIMING_VN2_BIT   24
  #define LCD_VERTTIMING_VN2_MASK  (0xFF << LCD_VERTTIMING_VN2_BIT)
  #define LCD_VERTTIMING_VN2_N(N)  ((((N)-1) << LCD_VERTTIMING_VN2_BIT) & LCD_VERTTIMING_VN2_MASK)
  #define LCD_VERTTIMING_VN1_BIT   16
  #define LCD_VERTTIMING_VN1_MASK  (0xFF << LCD_VERTTIMING_VN1_BIT)
  #define LCD_VERTTIMING_VN1_N(N)  ((((N)-1) << LCD_VERTTIMING_VN1_BIT) & LCD_VERTTIMING_VN1_MASK)
  #define LCD_VERTTIMING_VPW_BIT   10
  #define LCD_VERTTIMING_VPW_MASK  (0x3F << LCD_VERTTIMING_VPW_BIT)
  #define LCD_VERTTIMING_VPW_N(N)  ((((N)-1) << LCD_VERTTIMING_VPW_BIT) & LCD_VERTTIMING_VPW_MASK)
  #define LCD_VERTTIMING_LPP_BIT   0
  #define LCD_VERTTIMING_LPP_MASK  (0x3FF << LCD_VERTTIMING_LPP_BIT)
  #define LCD_VERTTIMING_LPP_N(N)  ((((N)-1) << LCD_VERTTIMING_LPP_BIT) & LCD_VERTTIMING_LPP_MASK)

#define LCD_CLKCONTROL             (AU1100_LCD_BASE + 0x14)
  #define LCD_CLKCONTROL_IB        (1<<18)
  #define LCD_CLKCONTROL_IC        (1<<17)
  #define LCD_CLKCONTROL_IH        (1<<16)
  #define LCD_CLKCONTROL_IV        (1<<15)
  #define LCD_CLKCONTROL_BF_BIT    10
  #define LCD_CLKCONTROL_BF_MASK   (0x1F << LCD_CLKCONTROL_BF_BIT)
  #define LCD_CLKCONTROL_BF_N(N)   ((((N)-1) << LCD_CLKCONTROL_BF_BIT) & LCD_CLKCONTROL_BF_MASK)
  #define LCD_CLKCONTROL_PCD_BIT   0
  #define LCD_CLKCONTROL_PCD_MASK  (0x3FF << LCD_CLKCONTROL_PCD_BIT)
  #define LCD_CLKCONTROL_PCD_N(N)  (((N) << LCD_CLKCONTROL_PCD_BIT) & LCD_CLKCONTROL_PCD_MASK)

#define LCD_DMAADDR0               (AU1100_LCD_BASE + 0x18)
#define LCD_DMAADDR1               (AU1100_LCD_BASE + 0x1C)
  #define LCD_DMA_SA_BIT           5
  #define LCD_DMA_SA_MASK          (0x7FFFFFF << LCD_DMA_SA_BIT)
  #define LCD_DMA_SA_N(N)          ((N) & LCD_DMA_SA_MASK)

#define LCD_WORDS                  (AU1100_LCD_BASE + 0x20)
  #define LCD_WRD_WRDS_BIT         0
  #define LCD_WRD_WRDS_MASK        (0xFFFFFFFF << LCD_WRD_WRDS_BIT)
  #define LCD_WRD_WRDS_N(N)        ((((N)-1) << LCD_WRD_WRDS_BIT) & LCD_WRD_WRDS_MASK)

#define LCD_PWMDIV                 (AU1100_LCD_BASE + 0x24)
  #define LCD_PWMDIV_EN            (1<<12)
  #define LCD_PWMDIV_PWMDIV_BIT    0
  #define LCD_PWMDIV_PWMDIV_MASK   (0xFFF << LCD_PWMDIV_PWMDIV_BIT)
  #define LCD_PWMDIV_PWMDIV_N(N)   ((((N)-1) << LCD_PWMDIV_PWMDIV_BIT) & LCD_PWMDIV_PWMDIV_MASK)

#define LCD_PWMHI                  (AU1100_LCD_BASE + 0x28)
  #define LCD_PWMHI_PWMHI1_BIT     12
  #define LCD_PWMHI_PWMHI1_MASK    (0xFFF << LCD_PWMHI_PWMHI1_BIT)
  #define LCD_PWMHI_PWMHI1_N(N)    (((N) << LCD_PWMHI_PWMHI1_BIT) & LCD_PWMHI_PWMHI1_MASK)
  #define LCD_PWMHI_PWMHI0_BIT     0
  #define LCD_PWMHI_PWMHI0_MASK    (0xFFF << LCD_PWMHI_PWMHI0_BIT)
  #define LCD_PWMHI_PWMHI0_N(N)    (((N) << LCD_PWMHI_PWMHI0_BIT) & LCD_PWMHI_PWMHI0_MASK)

#define LCD_PALLETTEBASE                (AU1100_LCD_BASE + 0x400)
  #define LCD_PALLETTE_MONO_MI_BIT      0
  #define LCD_PALLETTE_MONO_MI_MASK     (0xF << LCD_PALLETTE_MONO_MI_BIT)
  #define LCD_PALLETTE_MONO_MI_N(N)     (((N)<< LCD_PALLETTE_MONO_MI_BIT) & LCD_PALLETTE_MONO_MI_MASK)

  #define LCD_PALLETTE_COLOR_RI_BIT     8
  #define LCD_PALLETTE_COLOR_RI_MASK    (0xF << LCD_PALLETTE_COLOR_RI_BIT)
  #define LCD_PALLETTE_COLOR_RI_N(N)    (((N)<< LCD_PALLETTE_COLOR_RI_BIT) & LCD_PALLETTE_COLOR_RI_MASK)
  #define LCD_PALLETTE_COLOR_GI_BIT     4
  #define LCD_PALLETTE_COLOR_GI_MASK    (0xF << LCD_PALLETTE_COLOR_GI_BIT)
  #define LCD_PALLETTE_COLOR_GI_N(N)    (((N)<< LCD_PALLETTE_COLOR_GI_BIT) & LCD_PALLETTE_COLOR_GI_MASK)
  #define LCD_PALLETTE_COLOR_BI_BIT     0
  #define LCD_PALLETTE_COLOR_BI_MASK    (0xF << LCD_PALLETTE_COLOR_BI_BIT)
  #define LCD_PALLETTE_COLOR_BI_N(N)    (((N)<< LCD_PALLETTE_COLOR_BI_BIT) & LCD_PALLETTE_COLOR_BI_MASK)

  #define LCD_PALLETTE_TFT_DC_BIT       0
  #define LCD_PALLETTE_TFT_DC_MASK      (0xFFFF << LCD_PALLETTE_TFT_DC_BIT)
  #define LCD_PALLETTE_TFT_DC_N(N)      (((N)<< LCD_PALLETTE_TFT_DC_BIT) & LCD_PALLETTE_TFT_DC_MASK)

/********************************************************************/

/* List of panels known to work with the AU1100 LCD controller.
 * To add a new panel, enter the same specifications as the
 * Generic_TFT one, and MAKE SURE that it doesn't conflicts
 * with the controller restrictions. Restrictions are:
 *
 * STN color panels: max_bpp <= 12
 * STN mono panels: max_bpp <= 4
 * TFT panels: max_bpp <= 16
 * max_xres <= 800
 * max_yres <= 600
 */
static struct au1100fb_panel known_lcd_panels[] =
{
	/* 800x600x16bpp CRT */
	[0] = {
		.name = "CRT_800x600_16",
		.xres = 800,
		.yres = 600,
		.bpp = 16,
		.control_base =	0x0004886A |
			LCD_CONTROL_DEFAULT_PO | LCD_CONTROL_DEFAULT_SBPPF |
			LCD_CONTROL_BPP_16 | LCD_CONTROL_SBB_4,
		.clkcontrol_base = 0x00020000,
		.horztiming = 0x005aff1f,
		.verttiming = 0x16000e57,
	},
	/* just the standard LCD */
	[1] = {
		.name = "WWPC LCD",
		.xres = 240,
		.yres = 320,
		.bpp = 16,
		.control_base = 0x0006806A,
		.horztiming = 0x0A1010EF,
		.verttiming = 0x0301013F,
		.clkcontrol_base = 0x00018001,
	},
	/* Sharp 320x240 TFT panel */
	[2] = {
		.name = "Sharp_LQ038Q5DR01",
		.xres = 320,
		.yres = 240,
		.bpp = 16,
		.control_base =
		( LCD_CONTROL_SBPPF_565
		| LCD_CONTROL_C
		| LCD_CONTROL_SM_0
			| LCD_CONTROL_DEFAULT_PO
		| LCD_CONTROL_PT
		| LCD_CONTROL_PC
		| LCD_CONTROL_BPP_16 ),
		.horztiming =
		( LCD_HORZTIMING_HN2_N(8)
		| LCD_HORZTIMING_HN1_N(60)
		| LCD_HORZTIMING_HPW_N(12)
		| LCD_HORZTIMING_PPL_N(320) ),
		.verttiming =
		( LCD_VERTTIMING_VN2_N(5)
		| LCD_VERTTIMING_VN1_N(17)
		| LCD_VERTTIMING_VPW_N(1)
		| LCD_VERTTIMING_LPP_N(240) ),
		.clkcontrol_base = LCD_CLKCONTROL_PCD_N(1),
	},

	/* Hitachi SP14Q005 and possibly others */
	[3] = {
		.name = "Hitachi_SP14Qxxx",
		.xres = 320,
		.yres = 240,
		.bpp = 4,
		.control_base =
			( LCD_CONTROL_C
			| LCD_CONTROL_BPP_4 ),
		.horztiming =
			( LCD_HORZTIMING_HN2_N(1)
			| LCD_HORZTIMING_HN1_N(1)
			| LCD_HORZTIMING_HPW_N(1)
			| LCD_HORZTIMING_PPL_N(320) ),
		.verttiming =
			( LCD_VERTTIMING_VN2_N(1)
			| LCD_VERTTIMING_VN1_N(1)
			| LCD_VERTTIMING_VPW_N(1)
			| LCD_VERTTIMING_LPP_N(240) ),
		.clkcontrol_base = LCD_CLKCONTROL_PCD_N(4),
	},

	/* Generic 640x480 TFT panel */
	[4] = {
		.name = "TFT_640x480_16",
		.xres = 640,
		.yres = 480,
		.bpp = 16,
		.control_base = 0x004806a | LCD_CONTROL_DEFAULT_PO,
		.horztiming = 0x3434d67f,
		.verttiming = 0x0e0e39df,
		.clkcontrol_base = LCD_CLKCONTROL_PCD_N(1),
	},

	 /* Pb1100 LCDB 640x480 PrimeView TFT panel */
	[5] = {
		.name = "PrimeView_640x480_16",
		.xres = 640,
		.yres = 480,
		.bpp = 16,
		.control_base = 0x0004886a | LCD_CONTROL_DEFAULT_PO,
		.horztiming = 0x0e4bfe7f,
		.verttiming = 0x210805df,
		.clkcontrol_base = 0x00038001,
	},
};

/********************************************************************/

/* Inline helpers */

#define panel_is_dual(panel)  (panel->control_base & LCD_CONTROL_DP)
#define panel_is_active(panel)(panel->control_base & LCD_CONTROL_PT)
#define panel_is_color(panel) (panel->control_base & LCD_CONTROL_PC)
#define panel_swap_rgb(panel) (panel->control_base & LCD_CONTROL_CCO)

#if defined(CONFIG_COMPILE_TEST) && !defined(CONFIG_MIPS)
/* This is only defined to be able to compile this driver on non-mips platforms */
#define KSEG1ADDR(x) (x)
#endif

#define DRIVER_NAME "au1100fb"
#define DRIVER_DESC "LCD controller driver for AU1100 processors"

#define to_au1100fb_device(_info) \
	  (_info ? container_of(_info, struct au1100fb_device, info) : NULL);

/* Bitfields format supported by the controller. Note that the order of formats
 * SHOULD be the same as in the LCD_CONTROL_SBPPF field, so we can retrieve the
 * right pixel format by doing rgb_bitfields[LCD_CONTROL_SBPPF_XXX >> LCD_CONTROL_SBPPF]
 */
struct fb_bitfield rgb_bitfields[][4] =
{
  	/*     Red, 	   Green, 	 Blue, 	     Transp   */
	{ { 10, 6, 0 }, { 5, 5, 0 }, { 0, 5, 0 }, { 0, 0, 0 } },
	{ { 11, 5, 0 }, { 5, 6, 0 }, { 0, 5, 0 }, { 0, 0, 0 } },
	{ { 11, 5, 0 }, { 6, 5, 0 }, { 0, 6, 0 }, { 0, 0, 0 } },
	{ { 10, 5, 0 }, { 5, 5, 0 }, { 0, 5, 0 }, { 15, 1, 0 } },
	{ { 11, 5, 0 }, { 6, 5, 0 }, { 1, 5, 0 }, { 0, 1, 0 } },

	/* The last is used to describe 12bpp format */
	{ { 8, 4, 0 },  { 4, 4, 0 }, { 0, 4, 0 }, { 0, 0, 0 } },
};

/* fb_blank
 * Blank the screen. Depending on the mode, the screen will be
 * activated with the backlight color, or desactivated
 */
static int au1100fb_fb_blank(int blank_mode, struct fb_info *fbi)
{
	struct au1100fb_device *fbdev = to_au1100fb_device(fbi);

	pr_devel("fb_blank %d %p", blank_mode, fbi);

	switch (blank_mode) {

	case VESA_NO_BLANKING:
		/* Turn on panel */
		fbdev->regs->lcd_control |= LCD_CONTROL_GO;
		wmb(); /* drain writebuffer */
		break;

	case VESA_VSYNC_SUSPEND:
	case VESA_HSYNC_SUSPEND:
	case VESA_POWERDOWN:
		/* Turn off panel */
		fbdev->regs->lcd_control &= ~LCD_CONTROL_GO;
		wmb(); /* drain writebuffer */
		break;
	default:
		break;

	}
	return 0;
}

/*
 * Set hardware with var settings. This will enable the controller with a specific
 * mode, normally validated with the fb_check_var method
	 */
static int au1100fb_setmode(struct au1100fb_device *fbdev)
{
	struct fb_info *info;
	u32 words;
	int index;

	if (!fbdev)
		return -EINVAL;

	info = &fbdev->info;

	/* Update var-dependent FB info */
	if (panel_is_active(fbdev->panel) || panel_is_color(fbdev->panel)) {
		if (info->var.bits_per_pixel <= 8) {
			/* palettized */
			info->var.red.offset    = 0;
			info->var.red.length    = info->var.bits_per_pixel;
			info->var.red.msb_right = 0;

			info->var.green.offset  = 0;
			info->var.green.length  = info->var.bits_per_pixel;
			info->var.green.msb_right = 0;

			info->var.blue.offset   = 0;
			info->var.blue.length   = info->var.bits_per_pixel;
			info->var.blue.msb_right = 0;

			info->var.transp.offset = 0;
			info->var.transp.length = 0;
			info->var.transp.msb_right = 0;

			info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
			info->fix.line_length = info->var.xres_virtual /
							(8/info->var.bits_per_pixel);
		} else {
			/* non-palettized */
			index = (fbdev->panel->control_base & LCD_CONTROL_SBPPF_MASK) >> LCD_CONTROL_SBPPF_BIT;
			info->var.red = rgb_bitfields[index][0];
			info->var.green = rgb_bitfields[index][1];
			info->var.blue = rgb_bitfields[index][2];
			info->var.transp = rgb_bitfields[index][3];

			info->fix.visual = FB_VISUAL_TRUECOLOR;
			info->fix.line_length = info->var.xres_virtual << 1; /* depth=16 */
		}
	} else {
		/* mono */
		info->fix.visual = FB_VISUAL_MONO10;
		info->fix.line_length = info->var.xres_virtual / 8;
	}

	info->screen_size = info->fix.line_length * info->var.yres_virtual;
	info->var.rotate = ((fbdev->panel->control_base&LCD_CONTROL_SM_MASK) \
				>> LCD_CONTROL_SM_BIT) * 90;

	/* Determine BPP mode and format */
	fbdev->regs->lcd_control = fbdev->panel->control_base;
	fbdev->regs->lcd_horztiming = fbdev->panel->horztiming;
	fbdev->regs->lcd_verttiming = fbdev->panel->verttiming;
	fbdev->regs->lcd_clkcontrol = fbdev->panel->clkcontrol_base;
	fbdev->regs->lcd_intenable = 0;
	fbdev->regs->lcd_intstatus = 0;
	fbdev->regs->lcd_dmaaddr0 = LCD_DMA_SA_N(fbdev->fb_phys);

	if (panel_is_dual(fbdev->panel)) {
		/* Second panel display seconf half of screen if possible,
		 * otherwise display the same as the first panel */
		if (info->var.yres_virtual >= (info->var.yres << 1)) {
			fbdev->regs->lcd_dmaaddr1 = LCD_DMA_SA_N(fbdev->fb_phys +
							  (info->fix.line_length *
						          (info->var.yres_virtual >> 1)));
		} else {
			fbdev->regs->lcd_dmaaddr1 = LCD_DMA_SA_N(fbdev->fb_phys);
		}
	}

	words = info->fix.line_length / sizeof(u32);
	if (!info->var.rotate || (info->var.rotate == 180)) {
		words *= info->var.yres_virtual;
		if (info->var.rotate /* 180 */) {
			words -= (words % 8); /* should be divisable by 8 */
		}
	}
	fbdev->regs->lcd_words = LCD_WRD_WRDS_N(words);

	fbdev->regs->lcd_pwmdiv = 0;
	fbdev->regs->lcd_pwmhi = 0;

	/* Resume controller */
	fbdev->regs->lcd_control |= LCD_CONTROL_GO;
	mdelay(10);
	au1100fb_fb_blank(VESA_NO_BLANKING, info);

	return 0;
}

/* fb_setcolreg
 * Set color in LCD palette.
 */
static int au1100fb_fb_setcolreg(unsigned regno, unsigned red, unsigned green, unsigned blue,
				 unsigned transp, struct fb_info *fbi)
{
	struct au1100fb_device *fbdev;
	u32 *palette;
	u32 value;

	fbdev = to_au1100fb_device(fbi);
	palette = fbdev->regs->lcd_palettebase;

	if (regno > (AU1100_LCD_NBR_PALETTE_ENTRIES - 1))
		return -EINVAL;

	if (fbi->var.grayscale) {
		/* Convert color to grayscale */
		red = green = blue =
			(19595 * red + 38470 * green + 7471 * blue) >> 16;
	}

	if (fbi->fix.visual == FB_VISUAL_TRUECOLOR) {
		/* Place color in the pseudopalette */
		if (regno > 16)
			return -EINVAL;

		palette = (u32*)fbi->pseudo_palette;

		red   >>= (16 - fbi->var.red.length);
		green >>= (16 - fbi->var.green.length);
		blue  >>= (16 - fbi->var.blue.length);

		value = (red   << fbi->var.red.offset) 	|
			(green << fbi->var.green.offset)|
			(blue  << fbi->var.blue.offset);
		value &= 0xFFFF;

	} else if (panel_is_active(fbdev->panel)) {
		/* COLOR TFT PALLETTIZED (use RGB 565) */
		value = (red & 0xF800)|((green >> 5) & 0x07E0)|((blue >> 11) & 0x001F);
		value &= 0xFFFF;

	} else if (panel_is_color(fbdev->panel)) {
		/* COLOR STN MODE */
		value = (((panel_swap_rgb(fbdev->panel) ? blue : red) >> 12) & 0x000F) |
			((green >> 8) & 0x00F0) |
			(((panel_swap_rgb(fbdev->panel) ? red : blue) >> 4) & 0x0F00);
		value &= 0xFFF;
	} else {
		/* MONOCHROME MODE */
		value = (green >> 12) & 0x000F;
		value &= 0xF;
	}

	palette[regno] = value;

	return 0;
}

/* fb_pan_display
 * Pan display in x and/or y as specified
 */
static int au1100fb_fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *fbi)
{
	struct au1100fb_device *fbdev;
	int dy;

	fbdev = to_au1100fb_device(fbi);

	pr_devel("fb_pan_display %p %p", var, fbi);

	if (!var || !fbdev) {
		return -EINVAL;
	}

	if (var->xoffset - fbi->var.xoffset) {
		/* No support for X panning for now! */
		return -EINVAL;
	}

	pr_devel("fb_pan_display 2 %p %p", var, fbi);
	dy = var->yoffset - fbi->var.yoffset;
	if (dy) {

		u32 dmaaddr;

		pr_devel("Panning screen of %d lines", dy);

		dmaaddr = fbdev->regs->lcd_dmaaddr0;
		dmaaddr += (fbi->fix.line_length * dy);

		/* TODO: Wait for current frame to finished */
		fbdev->regs->lcd_dmaaddr0 = LCD_DMA_SA_N(dmaaddr);

		if (panel_is_dual(fbdev->panel)) {
			dmaaddr = fbdev->regs->lcd_dmaaddr1;
			dmaaddr += (fbi->fix.line_length * dy);
			fbdev->regs->lcd_dmaaddr0 = LCD_DMA_SA_N(dmaaddr);
	}
	}
	pr_devel("fb_pan_display 3 %p %p", var, fbi);

	return 0;
}

/* fb_mmap
 * Map video memory in user space. We don't use the generic fb_mmap method mainly
 * to allow the use of the TLB streaming flag (CCA=6)
 */
static int au1100fb_fb_mmap(struct fb_info *fbi, struct vm_area_struct *vma)
{
	struct au1100fb_device *fbdev = to_au1100fb_device(fbi);

	vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);

#ifndef CONFIG_S390
	/* On s390 pgprot_val() is a function and thus not a lvalue */
	pgprot_val(vma->vm_page_prot) |= (6 << 9); //CCA=6
#endif

	return dma_mmap_coherent(fbdev->dev, vma, fbdev->fb_mem, fbdev->fb_phys,
			fbdev->fb_len);
}

static const struct fb_ops au1100fb_ops = {
	.owner			= THIS_MODULE,
	__FB_DEFAULT_IOMEM_OPS_RDWR,
	.fb_setcolreg		= au1100fb_fb_setcolreg,
	.fb_blank		= au1100fb_fb_blank,
	.fb_pan_display		= au1100fb_fb_pan_display,
	__FB_DEFAULT_IOMEM_OPS_DRAW,
	.fb_mmap		= au1100fb_fb_mmap,
};


/*-------------------------------------------------------------------------*/

static int au1100fb_setup(struct au1100fb_device *fbdev)
{
	char *this_opt, *options;
	int num_panels = ARRAY_SIZE(known_lcd_panels);

	if (num_panels <= 0) {
		pr_err("No LCD panels supported by driver!");
		return -ENODEV;
	}

	if (fb_get_options(DRIVER_NAME, &options))
		return -ENODEV;
	if (!options)
		return -ENODEV;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		/* Panel option */
		if (!strncmp(this_opt, "panel:", 6)) {
			int i;
			this_opt += 6;
			for (i = 0; i < num_panels; i++) {
				if (!strncmp(this_opt, known_lcd_panels[i].name,
					     strlen(this_opt))) {
					fbdev->panel = &known_lcd_panels[i];
					fbdev->panel_idx = i;
					break;
				}
			}
			if (i >= num_panels) {
				pr_warn("Panel '%s' not supported!", this_opt);
				return -ENODEV;
			}
		}
		/* Unsupported option */
		else
			pr_warn("Unsupported option \"%s\"", this_opt);
	}

	pr_info("Panel=%s", fbdev->panel->name);

	return 0;
}

static int au1100fb_drv_probe(struct platform_device *dev)
{
	struct au1100fb_device *fbdev;
	struct resource *regs_res;
	struct clk *c;

	/* Allocate new device private */
	fbdev = devm_kzalloc(&dev->dev, sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev)
		return -ENOMEM;

	if (au1100fb_setup(fbdev))
		goto failed;

	platform_set_drvdata(dev, (void *)fbdev);
	fbdev->dev = &dev->dev;

	/* Allocate region for our registers and map them */
	regs_res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!regs_res) {
		pr_err("fail to retrieve registers resource");
		return -EFAULT;
	}

	fbdev->info.fix = (struct fb_fix_screeninfo) {
		.mmio_start = regs_res->start,
		.mmio_len = resource_size(regs_res),
		.id = "AU1100 FB",
		.xpanstep = 1,
		.ypanstep = 1,
		.type = FB_TYPE_PACKED_PIXELS,
		.accel = FB_ACCEL_NONE,
	};

	if (!devm_request_mem_region(&dev->dev,
				     fbdev->info.fix.mmio_start,
				     fbdev->info.fix.mmio_len,
				     DRIVER_NAME)) {
		pr_err("fail to lock memory region at 0x%08lx",
			  fbdev->info.fix.mmio_start);
		return -EBUSY;
	}

	fbdev->regs = (struct au1100fb_regs*)KSEG1ADDR(fbdev->info.fix.mmio_start);

	pr_devel("Register memory map at %p", fbdev->regs);
	pr_devel("phys=0x%08x, size=%zu", fbdev->regs_phys, fbdev->regs_len);

	c = clk_get(NULL, "lcd_intclk");
	if (!IS_ERR(c)) {
		fbdev->lcdclk = c;
		clk_set_rate(c, 48000000);
		clk_prepare_enable(c);
	}

	/* Allocate the framebuffer to the maximum screen size * nbr of video buffers */
	fbdev->fb_len = fbdev->panel->xres * fbdev->panel->yres *
		  	(fbdev->panel->bpp >> 3) * AU1100FB_NBR_VIDEO_BUFFERS;

	fbdev->fb_mem = dmam_alloc_coherent(&dev->dev,
					    PAGE_ALIGN(fbdev->fb_len),
					    &fbdev->fb_phys, GFP_KERNEL);
	if (!fbdev->fb_mem) {
		pr_err("fail to allocate framebuffer (size: %zuK))",
			  fbdev->fb_len / 1024);
		return -ENOMEM;
	}

	fbdev->info.fix.smem_start = fbdev->fb_phys;
	fbdev->info.fix.smem_len = fbdev->fb_len;

	pr_devel("Framebuffer memory map at %p", fbdev->fb_mem);
	pr_devel("phys=0x%pad, size=%zuK", &fbdev->fb_phys, fbdev->fb_len / 1024);

	/* load the panel info into the var struct */
	fbdev->info.var = (struct fb_var_screeninfo) {
		.activate = FB_ACTIVATE_NOW,
		.height = -1,
		.width = -1,
		.vmode = FB_VMODE_NONINTERLACED,
		.bits_per_pixel = fbdev->panel->bpp,
		.xres = fbdev->panel->xres,
		.xres_virtual = fbdev->panel->xres,
		.yres = fbdev->panel->yres,
		.yres_virtual = fbdev->panel->yres,
	};

	fbdev->info.screen_base = fbdev->fb_mem;
	fbdev->info.fbops = &au1100fb_ops;

	fbdev->info.pseudo_palette =
		devm_kcalloc(&dev->dev, 16, sizeof(u32), GFP_KERNEL);
	if (!fbdev->info.pseudo_palette)
		return -ENOMEM;

	if (fb_alloc_cmap(&fbdev->info.cmap, AU1100_LCD_NBR_PALETTE_ENTRIES, 0) < 0) {
		pr_err("Fail to allocate colormap (%d entries)",
			   AU1100_LCD_NBR_PALETTE_ENTRIES);
		return -EFAULT;
	}

	/* Set h/w registers */
	au1100fb_setmode(fbdev);

	/* Register new framebuffer */
	if (register_framebuffer(&fbdev->info) < 0) {
		pr_err("cannot register new framebuffer");
		goto failed;
	}

	return 0;

failed:
	if (fbdev->lcdclk) {
		clk_disable_unprepare(fbdev->lcdclk);
		clk_put(fbdev->lcdclk);
	}
	if (fbdev->info.cmap.len != 0) {
		fb_dealloc_cmap(&fbdev->info.cmap);
	}

	return -ENODEV;
}

static void au1100fb_drv_remove(struct platform_device *dev)
{
	struct au1100fb_device *fbdev = NULL;

	fbdev = platform_get_drvdata(dev);

#if !defined(CONFIG_FRAMEBUFFER_CONSOLE) && defined(CONFIG_LOGO)
	au1100fb_fb_blank(VESA_POWERDOWN, &fbdev->info);
#endif
	fbdev->regs->lcd_control &= ~LCD_CONTROL_GO;

	/* Clean up all probe data */
	unregister_framebuffer(&fbdev->info);

	fb_dealloc_cmap(&fbdev->info.cmap);

	if (fbdev->lcdclk) {
		clk_disable_unprepare(fbdev->lcdclk);
		clk_put(fbdev->lcdclk);
	}
}

#ifdef CONFIG_PM
static int au1100fb_drv_suspend(struct platform_device *dev, pm_message_t state)
{
	struct au1100fb_device *fbdev = platform_get_drvdata(dev);

	if (!fbdev)
		return 0;

	/* Blank the LCD */
	au1100fb_fb_blank(VESA_POWERDOWN, &fbdev->info);

	clk_disable(fbdev->lcdclk);

	memcpy(&fbdev->pm_regs, fbdev->regs, sizeof(struct au1100fb_regs));

	return 0;
}

static int au1100fb_drv_resume(struct platform_device *dev)
{
	struct au1100fb_device *fbdev = platform_get_drvdata(dev);
	int ret;

	if (!fbdev)
		return 0;

	memcpy(fbdev->regs, &fbdev->pm_regs, sizeof(struct au1100fb_regs));

	ret = clk_enable(fbdev->lcdclk);
	if (ret)
		return ret;

	/* Unblank the LCD */
	au1100fb_fb_blank(VESA_NO_BLANKING, &fbdev->info);

	return 0;
}
#else
#define au1100fb_drv_suspend NULL
#define au1100fb_drv_resume NULL
#endif

static struct platform_driver au1100fb_driver = {
	.driver = {
		.name		= "au1100-lcd",
	},
	.probe		= au1100fb_drv_probe,
	.remove		= au1100fb_drv_remove,
	.suspend	= au1100fb_drv_suspend,
	.resume		= au1100fb_drv_resume,
};
module_platform_driver(au1100fb_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
