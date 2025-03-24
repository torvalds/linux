/*
 * BRIEF MODULE DESCRIPTION
 *	Au1200 LCD Driver.
 *
 * Copyright 2004-2005 AMD
 * Author: AMD
 *
 * Based on:
 * linux/drivers/video/skeletonfb.c -- Skeleton for a frame buffer device
 *  Created 28 Dec 1997 by Geert Uytterhoeven
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED	  ``AS	IS'' AND   ANY	EXPRESS OR IMPLIED
 *  WARRANTIES,	  INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO	EVENT  SHALL   THE AUTHOR  BE	 LIABLE FOR ANY	  DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED	  TO, PROCUREMENT OF  SUBSTITUTE GOODS	OR SERVICES; LOSS OF
 *  USE, DATA,	OR PROFITS; OR	BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN	 CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ctype.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1200fb.h>	/* platform_data */
#include "au1200fb.h"

#define DRIVER_NAME "au1200fb"
#define DRIVER_DESC "LCD controller driver for AU1200 processors"

#define DEBUG 0

#define print_err(f, arg...) printk(KERN_ERR DRIVER_NAME ": " f "\n", ## arg)
#define print_warn(f, arg...) printk(KERN_WARNING DRIVER_NAME ": " f "\n", ## arg)
#define print_info(f, arg...) printk(KERN_INFO DRIVER_NAME ": " f "\n", ## arg)

#if DEBUG
#define print_dbg(f, arg...) printk(KERN_DEBUG __FILE__ ": " f "\n", ## arg)
#else
#define print_dbg(f, arg...) do {} while (0)
#endif


#define AU1200_LCD_FB_IOCTL 0x46FF

#define AU1200_LCD_SET_SCREEN 1
#define AU1200_LCD_GET_SCREEN 2
#define AU1200_LCD_SET_WINDOW 3
#define AU1200_LCD_GET_WINDOW 4
#define AU1200_LCD_SET_PANEL  5
#define AU1200_LCD_GET_PANEL  6

#define SCREEN_SIZE		    (1<< 1)
#define SCREEN_BACKCOLOR    (1<< 2)
#define SCREEN_BRIGHTNESS   (1<< 3)
#define SCREEN_COLORKEY     (1<< 4)
#define SCREEN_MASK         (1<< 5)

struct au1200_lcd_global_regs_t {
	unsigned int flags;
	unsigned int xsize;
	unsigned int ysize;
	unsigned int backcolor;
	unsigned int brightness;
	unsigned int colorkey;
	unsigned int mask;
	unsigned int panel_choice;
	char panel_desc[80];

};

#define WIN_POSITION            (1<< 0)
#define WIN_ALPHA_COLOR         (1<< 1)
#define WIN_ALPHA_MODE          (1<< 2)
#define WIN_PRIORITY            (1<< 3)
#define WIN_CHANNEL             (1<< 4)
#define WIN_BUFFER_FORMAT       (1<< 5)
#define WIN_COLOR_ORDER         (1<< 6)
#define WIN_PIXEL_ORDER         (1<< 7)
#define WIN_SIZE                (1<< 8)
#define WIN_COLORKEY_MODE       (1<< 9)
#define WIN_DOUBLE_BUFFER_MODE  (1<< 10)
#define WIN_RAM_ARRAY_MODE      (1<< 11)
#define WIN_BUFFER_SCALE        (1<< 12)
#define WIN_ENABLE	            (1<< 13)

struct au1200_lcd_window_regs_t {
	unsigned int flags;
	unsigned int xpos;
	unsigned int ypos;
	unsigned int alpha_color;
	unsigned int alpha_mode;
	unsigned int priority;
	unsigned int channel;
	unsigned int buffer_format;
	unsigned int color_order;
	unsigned int pixel_order;
	unsigned int xsize;
	unsigned int ysize;
	unsigned int colorkey_mode;
	unsigned int double_buffer_mode;
	unsigned int ram_array_mode;
	unsigned int xscale;
	unsigned int yscale;
	unsigned int enable;
};


struct au1200_lcd_iodata_t {
	unsigned int subcmd;
	struct au1200_lcd_global_regs_t global;
	struct au1200_lcd_window_regs_t window;
};

#if defined(__BIG_ENDIAN)
#define LCD_CONTROL_DEFAULT_PO LCD_CONTROL_PO_11
#else
#define LCD_CONTROL_DEFAULT_PO LCD_CONTROL_PO_00
#endif
#define LCD_CONTROL_DEFAULT_SBPPF LCD_CONTROL_SBPPF_565

/* Private, per-framebuffer management information (independent of the panel itself) */
struct au1200fb_device {
	struct fb_info *fb_info;		/* FB driver info record */
	struct au1200fb_platdata *pd;
	struct device *dev;

	int					plane;
	unsigned char* 		fb_mem;		/* FrameBuffer memory map */
	unsigned int		fb_len;
	dma_addr_t    		fb_phys;
};

/********************************************************************/

/* LCD controller restrictions */
#define AU1200_LCD_MAX_XRES	1280
#define AU1200_LCD_MAX_YRES	1024
#define AU1200_LCD_MAX_BPP	32
#define AU1200_LCD_MAX_CLK	96000000 /* fixme: this needs to go away ? */
#define AU1200_LCD_NBR_PALETTE_ENTRIES 256

/* Default number of visible screen buffer to allocate */
#define AU1200FB_NBR_VIDEO_BUFFERS 1

/* Default maximum number of fb devices to create */
#define MAX_DEVICE_COUNT	4

/* Default window configuration entry to use (see windows[]) */
#define DEFAULT_WINDOW_INDEX	2

/********************************************************************/

static struct fb_info *_au1200fb_infos[MAX_DEVICE_COUNT];
static struct au1200_lcd *lcd = (struct au1200_lcd *) AU1200_LCD_ADDR;
static int device_count = MAX_DEVICE_COUNT;
static int window_index = DEFAULT_WINDOW_INDEX;	/* default is zero */
static int panel_index = 2; /* default is zero */
static struct window_settings *win;
static struct panel_settings *panel;
static int noblanking = 1;
static int nohwcursor = 0;

struct window_settings {
	unsigned char name[64];
	uint32 mode_backcolor;
	uint32 mode_colorkey;
	uint32 mode_colorkeymsk;
	struct {
		int xres;
		int yres;
		int xpos;
		int ypos;
		uint32 mode_winctrl1; /* winctrl1[FRM,CCO,PO,PIPE] */
		uint32 mode_winenable;
	} w[4];
};

#if defined(__BIG_ENDIAN)
#define LCD_WINCTRL1_PO_16BPP LCD_WINCTRL1_PO_00
#else
#define LCD_WINCTRL1_PO_16BPP LCD_WINCTRL1_PO_01
#endif

/*
 * Default window configurations
 */
static struct window_settings windows[] = {
	{ /* Index 0 */
		"0-FS gfx, 1-video, 2-ovly gfx, 3-ovly gfx",
		/* mode_backcolor	*/ 0x006600ff,
		/* mode_colorkey,msk*/ 0, 0,
		{
			{
			/* xres, yres, xpos, ypos */ 0, 0, 0, 0,
			/* mode_winctrl1 */ LCD_WINCTRL1_FRM_16BPP565 |
				LCD_WINCTRL1_PO_16BPP,
			/* mode_winenable*/ LCD_WINENABLE_WEN0,
			},
			{
			/* xres, yres, xpos, ypos */ 100, 100, 100, 100,
			/* mode_winctrl1 */ LCD_WINCTRL1_FRM_16BPP565 |
				LCD_WINCTRL1_PO_16BPP |
				LCD_WINCTRL1_PIPE,
			/* mode_winenable*/ LCD_WINENABLE_WEN1,
			},
			{
			/* xres, yres, xpos, ypos */ 0, 0, 0, 0,
			/* mode_winctrl1 */ LCD_WINCTRL1_FRM_16BPP565 |
				LCD_WINCTRL1_PO_16BPP,
			/* mode_winenable*/ 0,
			},
			{
			/* xres, yres, xpos, ypos */ 0, 0, 0, 0,
			/* mode_winctrl1 */ LCD_WINCTRL1_FRM_16BPP565 |
				LCD_WINCTRL1_PO_16BPP |
				LCD_WINCTRL1_PIPE,
			/* mode_winenable*/ 0,
			},
		},
	},

	{ /* Index 1 */
		"0-FS gfx, 1-video, 2-ovly gfx, 3-ovly gfx",
		/* mode_backcolor	*/ 0x006600ff,
		/* mode_colorkey,msk*/ 0, 0,
		{
			{
			/* xres, yres, xpos, ypos */ 320, 240, 5, 5,
			/* mode_winctrl1 */ LCD_WINCTRL1_FRM_24BPP |
				LCD_WINCTRL1_PO_00,
			/* mode_winenable*/ LCD_WINENABLE_WEN0,
			},
			{
			/* xres, yres, xpos, ypos */ 0, 0, 0, 0,
			/* mode_winctrl1 */ LCD_WINCTRL1_FRM_16BPP565
				| LCD_WINCTRL1_PO_16BPP,
			/* mode_winenable*/ 0,
			},
			{
			/* xres, yres, xpos, ypos */ 100, 100, 0, 0,
			/* mode_winctrl1 */ LCD_WINCTRL1_FRM_16BPP565 |
				LCD_WINCTRL1_PO_16BPP |
				LCD_WINCTRL1_PIPE,
			/* mode_winenable*/ 0/*LCD_WINENABLE_WEN2*/,
			},
			{
			/* xres, yres, xpos, ypos */ 200, 25, 0, 0,
			/* mode_winctrl1 */ LCD_WINCTRL1_FRM_16BPP565 |
				LCD_WINCTRL1_PO_16BPP |
				LCD_WINCTRL1_PIPE,
			/* mode_winenable*/ 0,
			},
		},
	},
	{ /* Index 2 */
		"0-FS gfx, 1-video, 2-ovly gfx, 3-ovly gfx",
		/* mode_backcolor	*/ 0x006600ff,
		/* mode_colorkey,msk*/ 0, 0,
		{
			{
			/* xres, yres, xpos, ypos */ 0, 0, 0, 0,
			/* mode_winctrl1 */ LCD_WINCTRL1_FRM_16BPP565 |
				LCD_WINCTRL1_PO_16BPP,
			/* mode_winenable*/ LCD_WINENABLE_WEN0,
			},
			{
			/* xres, yres, xpos, ypos */ 0, 0, 0, 0,
			/* mode_winctrl1 */ LCD_WINCTRL1_FRM_16BPP565 |
				LCD_WINCTRL1_PO_16BPP,
			/* mode_winenable*/ 0,
			},
			{
			/* xres, yres, xpos, ypos */ 0, 0, 0, 0,
			/* mode_winctrl1 */ LCD_WINCTRL1_FRM_32BPP |
				LCD_WINCTRL1_PO_00|LCD_WINCTRL1_PIPE,
			/* mode_winenable*/ 0/*LCD_WINENABLE_WEN2*/,
			},
			{
			/* xres, yres, xpos, ypos */ 0, 0, 0, 0,
			/* mode_winctrl1 */ LCD_WINCTRL1_FRM_16BPP565 |
				LCD_WINCTRL1_PO_16BPP |
				LCD_WINCTRL1_PIPE,
			/* mode_winenable*/ 0,
			},
		},
	},
	/* Need VGA 640 @ 24bpp, @ 32bpp */
	/* Need VGA 800 @ 24bpp, @ 32bpp */
	/* Need VGA 1024 @ 24bpp, @ 32bpp */
};

/*
 * Controller configurations for various panels.
 */

struct panel_settings
{
	const char name[25];		/* Full name <vendor>_<model> */

	struct 	fb_monspecs monspecs; 	/* FB monitor specs */

	/* panel timings */
	uint32 mode_screen;
	uint32 mode_horztiming;
	uint32 mode_verttiming;
	uint32 mode_clkcontrol;
	uint32 mode_pwmdiv;
	uint32 mode_pwmhi;
	uint32 mode_outmask;
	uint32 mode_fifoctrl;
	uint32 mode_backlight;
	uint32 lcdclk;
#define Xres min_xres
#define Yres min_yres
	u32	min_xres;		/* Minimum horizontal resolution */
	u32	max_xres;		/* Maximum horizontal resolution */
	u32 	min_yres;		/* Minimum vertical resolution */
	u32 	max_yres;		/* Maximum vertical resolution */
};

/********************************************************************/
/* fixme: Maybe a modedb for the CRT ? otherwise panels should be as-is */

/* List of panels known to work with the AU1200 LCD controller.
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
static struct panel_settings known_lcd_panels[] =
{
	[0] = { /* QVGA 320x240 H:33.3kHz V:110Hz */
		.name = "QVGA_320x240",
		.monspecs = {
			.modedb = NULL,
			.modedb_len = 0,
			.hfmin = 30000,
			.hfmax = 70000,
			.vfmin = 60,
			.vfmax = 60,
			.dclkmin = 6000000,
			.dclkmax = 28000000,
			.input = FB_DISP_RGB,
		},
		.mode_screen		= LCD_SCREEN_SX_N(320) |
			LCD_SCREEN_SY_N(240),
		.mode_horztiming	= 0x00c4623b,
		.mode_verttiming	= 0x00502814,
		.mode_clkcontrol	= 0x00020002, /* /4=24Mhz */
		.mode_pwmdiv		= 0x00000000,
		.mode_pwmhi		= 0x00000000,
		.mode_outmask	= 0x00FFFFFF,
		.mode_fifoctrl	= 0x2f2f2f2f,
		.mode_backlight	= 0x00000000,
		.lcdclk		= 96,
		320, 320,
		240, 240,
	},

	[1] = { /* VGA 640x480 H:30.3kHz V:58Hz */
		.name = "VGA_640x480",
		.monspecs = {
			.modedb = NULL,
			.modedb_len = 0,
			.hfmin = 30000,
			.hfmax = 70000,
			.vfmin = 60,
			.vfmax = 60,
			.dclkmin = 6000000,
			.dclkmax = 28000000,
			.input = FB_DISP_RGB,
		},
		.mode_screen		= 0x13f9df80,
		.mode_horztiming	= 0x003c5859,
		.mode_verttiming	= 0x00741201,
		.mode_clkcontrol	= 0x00020001, /* /4=24Mhz */
		.mode_pwmdiv		= 0x00000000,
		.mode_pwmhi		= 0x00000000,
		.mode_outmask	= 0x00FFFFFF,
		.mode_fifoctrl	= 0x2f2f2f2f,
		.mode_backlight	= 0x00000000,
		.lcdclk		= 96,
		640, 480,
		640, 480,
	},

	[2] = { /* SVGA 800x600 H:46.1kHz V:69Hz */
		.name = "SVGA_800x600",
		.monspecs = {
			.modedb = NULL,
			.modedb_len = 0,
			.hfmin = 30000,
			.hfmax = 70000,
			.vfmin = 60,
			.vfmax = 60,
			.dclkmin = 6000000,
			.dclkmax = 28000000,
			.input = FB_DISP_RGB,
		},
		.mode_screen		= 0x18fa5780,
		.mode_horztiming	= 0x00dc7e77,
		.mode_verttiming	= 0x00584805,
		.mode_clkcontrol	= 0x00020000, /* /2=48Mhz */
		.mode_pwmdiv		= 0x00000000,
		.mode_pwmhi		= 0x00000000,
		.mode_outmask	= 0x00FFFFFF,
		.mode_fifoctrl	= 0x2f2f2f2f,
		.mode_backlight	= 0x00000000,
		.lcdclk		= 96,
		800, 800,
		600, 600,
	},

	[3] = { /* XVGA 1024x768 H:56.2kHz V:70Hz */
		.name = "XVGA_1024x768",
		.monspecs = {
			.modedb = NULL,
			.modedb_len = 0,
			.hfmin = 30000,
			.hfmax = 70000,
			.vfmin = 60,
			.vfmax = 60,
			.dclkmin = 6000000,
			.dclkmax = 28000000,
			.input = FB_DISP_RGB,
		},
		.mode_screen		= 0x1ffaff80,
		.mode_horztiming	= 0x007d0e57,
		.mode_verttiming	= 0x00740a01,
		.mode_clkcontrol	= 0x000A0000, /* /1 */
		.mode_pwmdiv		= 0x00000000,
		.mode_pwmhi		= 0x00000000,
		.mode_outmask	= 0x00FFFFFF,
		.mode_fifoctrl	= 0x2f2f2f2f,
		.mode_backlight	= 0x00000000,
		.lcdclk		= 72,
		1024, 1024,
		768, 768,
	},

	[4] = { /* XVGA XVGA 1280x1024 H:68.5kHz V:65Hz */
		.name = "XVGA_1280x1024",
		.monspecs = {
			.modedb = NULL,
			.modedb_len = 0,
			.hfmin = 30000,
			.hfmax = 70000,
			.vfmin = 60,
			.vfmax = 60,
			.dclkmin = 6000000,
			.dclkmax = 28000000,
			.input = FB_DISP_RGB,
		},
		.mode_screen		= 0x27fbff80,
		.mode_horztiming	= 0x00cdb2c7,
		.mode_verttiming	= 0x00600002,
		.mode_clkcontrol	= 0x000A0000, /* /1 */
		.mode_pwmdiv		= 0x00000000,
		.mode_pwmhi		= 0x00000000,
		.mode_outmask	= 0x00FFFFFF,
		.mode_fifoctrl	= 0x2f2f2f2f,
		.mode_backlight	= 0x00000000,
		.lcdclk		= 120,
		1280, 1280,
		1024, 1024,
	},

	[5] = { /* Samsung 1024x768 TFT */
		.name = "Samsung_1024x768_TFT",
		.monspecs = {
			.modedb = NULL,
			.modedb_len = 0,
			.hfmin = 30000,
			.hfmax = 70000,
			.vfmin = 60,
			.vfmax = 60,
			.dclkmin = 6000000,
			.dclkmax = 28000000,
			.input = FB_DISP_RGB,
		},
		.mode_screen		= 0x1ffaff80,
		.mode_horztiming	= 0x018cc677,
		.mode_verttiming	= 0x00241217,
		.mode_clkcontrol	= 0x00000000, /* SCB 0x1 /4=24Mhz */
		.mode_pwmdiv		= 0x8000063f, /* SCB 0x0 */
		.mode_pwmhi		= 0x03400000, /* SCB 0x0 */
		.mode_outmask	= 0x00FFFFFF,
		.mode_fifoctrl	= 0x2f2f2f2f,
		.mode_backlight	= 0x00000000,
		.lcdclk		= 96,
		1024, 1024,
		768, 768,
	},

	[6] = { /* Toshiba 640x480 TFT */
		.name = "Toshiba_640x480_TFT",
		.monspecs = {
			.modedb = NULL,
			.modedb_len = 0,
			.hfmin = 30000,
			.hfmax = 70000,
			.vfmin = 60,
			.vfmax = 60,
			.dclkmin = 6000000,
			.dclkmax = 28000000,
			.input = FB_DISP_RGB,
		},
		.mode_screen		= LCD_SCREEN_SX_N(640) |
			LCD_SCREEN_SY_N(480),
		.mode_horztiming	= LCD_HORZTIMING_HPW_N(96) |
			LCD_HORZTIMING_HND1_N(13) | LCD_HORZTIMING_HND2_N(51),
		.mode_verttiming	= LCD_VERTTIMING_VPW_N(2) |
			LCD_VERTTIMING_VND1_N(11) | LCD_VERTTIMING_VND2_N(32),
		.mode_clkcontrol	= 0x00000000, /* /4=24Mhz */
		.mode_pwmdiv		= 0x8000063f,
		.mode_pwmhi		= 0x03400000,
		.mode_outmask	= 0x00fcfcfc,
		.mode_fifoctrl	= 0x2f2f2f2f,
		.mode_backlight	= 0x00000000,
		.lcdclk		= 96,
		640, 480,
		640, 480,
	},

	[7] = { /* Sharp 320x240 TFT */
		.name = "Sharp_320x240_TFT",
		.monspecs = {
			.modedb = NULL,
			.modedb_len = 0,
			.hfmin = 12500,
			.hfmax = 20000,
			.vfmin = 38,
			.vfmax = 81,
			.dclkmin = 4500000,
			.dclkmax = 6800000,
			.input = FB_DISP_RGB,
		},
		.mode_screen		= LCD_SCREEN_SX_N(320) |
			LCD_SCREEN_SY_N(240),
		.mode_horztiming	= LCD_HORZTIMING_HPW_N(60) |
			LCD_HORZTIMING_HND1_N(13) | LCD_HORZTIMING_HND2_N(2),
		.mode_verttiming	= LCD_VERTTIMING_VPW_N(2) |
			LCD_VERTTIMING_VND1_N(2) | LCD_VERTTIMING_VND2_N(5),
		.mode_clkcontrol	= LCD_CLKCONTROL_PCD_N(7), /*16=6Mhz*/
		.mode_pwmdiv		= 0x8000063f,
		.mode_pwmhi		= 0x03400000,
		.mode_outmask	= 0x00fcfcfc,
		.mode_fifoctrl	= 0x2f2f2f2f,
		.mode_backlight	= 0x00000000,
		.lcdclk		= 96, /* 96MHz AUXPLL */
		320, 320,
		240, 240,
	},

	[8] = { /* Toppoly TD070WGCB2 7" 856x480 TFT */
		.name = "Toppoly_TD070WGCB2",
		.monspecs = {
			.modedb = NULL,
			.modedb_len = 0,
			.hfmin = 30000,
			.hfmax = 70000,
			.vfmin = 60,
			.vfmax = 60,
			.dclkmin = 6000000,
			.dclkmax = 28000000,
			.input = FB_DISP_RGB,
		},
		.mode_screen		= LCD_SCREEN_SX_N(856) |
			LCD_SCREEN_SY_N(480),
		.mode_horztiming	= LCD_HORZTIMING_HND2_N(43) |
			LCD_HORZTIMING_HND1_N(43) | LCD_HORZTIMING_HPW_N(114),
		.mode_verttiming	= LCD_VERTTIMING_VND2_N(20) |
			LCD_VERTTIMING_VND1_N(21) | LCD_VERTTIMING_VPW_N(4),
		.mode_clkcontrol	= 0x00020001, /* /4=24Mhz */
		.mode_pwmdiv		= 0x8000063f,
		.mode_pwmhi		= 0x03400000,
		.mode_outmask	= 0x00fcfcfc,
		.mode_fifoctrl	= 0x2f2f2f2f,
		.mode_backlight	= 0x00000000,
		.lcdclk		= 96,
		856, 856,
		480, 480,
	},
	[9] = {
		.name = "DB1300_800x480",
		.monspecs = {
			.modedb = NULL,
			.modedb_len = 0,
			.hfmin = 30000,
			.hfmax = 70000,
			.vfmin = 60,
			.vfmax = 60,
			.dclkmin = 6000000,
			.dclkmax = 28000000,
			.input = FB_DISP_RGB,
		},
		.mode_screen		= LCD_SCREEN_SX_N(800) |
					  LCD_SCREEN_SY_N(480),
		.mode_horztiming	= LCD_HORZTIMING_HPW_N(5) |
					  LCD_HORZTIMING_HND1_N(16) |
					  LCD_HORZTIMING_HND2_N(8),
		.mode_verttiming	= LCD_VERTTIMING_VPW_N(4) |
					  LCD_VERTTIMING_VND1_N(8) |
					  LCD_VERTTIMING_VND2_N(5),
		.mode_clkcontrol	= LCD_CLKCONTROL_PCD_N(1) |
					  LCD_CLKCONTROL_IV |
					  LCD_CLKCONTROL_IH,
		.mode_pwmdiv		= 0x00000000,
		.mode_pwmhi		= 0x00000000,
		.mode_outmask		= 0x00FFFFFF,
		.mode_fifoctrl		= 0x2f2f2f2f,
		.mode_backlight		= 0x00000000,
		.lcdclk			= 96,
		800, 800,
		480, 480,
	},
};

#define NUM_PANELS (ARRAY_SIZE(known_lcd_panels))

/********************************************************************/

static int winbpp (unsigned int winctrl1)
{
	int bits = 0;

	/* how many bits are needed for each pixel format */
	switch (winctrl1 & LCD_WINCTRL1_FRM) {
	case LCD_WINCTRL1_FRM_1BPP:
		bits = 1;
		break;
	case LCD_WINCTRL1_FRM_2BPP:
		bits = 2;
		break;
	case LCD_WINCTRL1_FRM_4BPP:
		bits = 4;
		break;
	case LCD_WINCTRL1_FRM_8BPP:
		bits = 8;
		break;
	case LCD_WINCTRL1_FRM_12BPP:
	case LCD_WINCTRL1_FRM_16BPP655:
	case LCD_WINCTRL1_FRM_16BPP565:
	case LCD_WINCTRL1_FRM_16BPP556:
	case LCD_WINCTRL1_FRM_16BPPI1555:
	case LCD_WINCTRL1_FRM_16BPPI5551:
	case LCD_WINCTRL1_FRM_16BPPA1555:
	case LCD_WINCTRL1_FRM_16BPPA5551:
		bits = 16;
		break;
	case LCD_WINCTRL1_FRM_24BPP:
	case LCD_WINCTRL1_FRM_32BPP:
		bits = 32;
		break;
	}

	return bits;
}

static int fbinfo2index (struct fb_info *fb_info)
{
	int i;

	for (i = 0; i < device_count; ++i) {
		if (fb_info == _au1200fb_infos[i])
			return i;
	}
	printk("au1200fb: ERROR: fbinfo2index failed!\n");
	return -1;
}

static int au1200_setlocation (struct au1200fb_device *fbdev, int plane,
	int xpos, int ypos)
{
	uint32 winctrl0, winctrl1, winenable, fb_offset = 0;
	int xsz, ysz;

	/* FIX!!! NOT CHECKING FOR COMPLETE OFFSCREEN YET */

	winctrl0 = lcd->window[plane].winctrl0;
	winctrl1 = lcd->window[plane].winctrl1;
	winctrl0 &= (LCD_WINCTRL0_A | LCD_WINCTRL0_AEN);
	winctrl1 &= ~(LCD_WINCTRL1_SZX | LCD_WINCTRL1_SZY);

	/* Check for off-screen adjustments */
	xsz = win->w[plane].xres;
	ysz = win->w[plane].yres;
	if ((xpos + win->w[plane].xres) > panel->Xres) {
		/* Off-screen to the right */
		xsz = panel->Xres - xpos; /* off by 1 ??? */
		/*printk("off screen right\n");*/
	}

	if ((ypos + win->w[plane].yres) > panel->Yres) {
		/* Off-screen to the bottom */
		ysz = panel->Yres - ypos; /* off by 1 ??? */
		/*printk("off screen bottom\n");*/
	}

	if (xpos < 0) {
		/* Off-screen to the left */
		xsz = win->w[plane].xres + xpos;
		fb_offset += (((0 - xpos) * winbpp(lcd->window[plane].winctrl1))/8);
		xpos = 0;
		/*printk("off screen left\n");*/
	}

	if (ypos < 0) {
		/* Off-screen to the top */
		ysz = win->w[plane].yres + ypos;
		/* fixme: fb_offset += ((0-ypos)*fb_pars[plane].line_length); */
		ypos = 0;
		/*printk("off screen top\n");*/
	}

	/* record settings */
	win->w[plane].xpos = xpos;
	win->w[plane].ypos = ypos;

	xsz -= 1;
	ysz -= 1;
	winctrl0 |= (xpos << 21);
	winctrl0 |= (ypos << 10);
	winctrl1 |= (xsz << 11);
	winctrl1 |= (ysz << 0);

	/* Disable the window while making changes, then restore WINEN */
	winenable = lcd->winenable & (1 << plane);
	wmb(); /* drain writebuffer */
	lcd->winenable &= ~(1 << plane);
	lcd->window[plane].winctrl0 = winctrl0;
	lcd->window[plane].winctrl1 = winctrl1;
	lcd->window[plane].winbuf0 =
	lcd->window[plane].winbuf1 = fbdev->fb_phys;
	lcd->window[plane].winbufctrl = 0; /* select winbuf0 */
	lcd->winenable |= winenable;
	wmb(); /* drain writebuffer */

	return 0;
}

static void au1200_setpanel(struct panel_settings *newpanel,
			    struct au1200fb_platdata *pd)
{
	/*
	 * Perform global setup/init of LCD controller
	 */
	uint32 winenable;

	/* Make sure all windows disabled */
	winenable = lcd->winenable;
	lcd->winenable = 0;
	wmb(); /* drain writebuffer */
	/*
	 * Ensure everything is disabled before reconfiguring
	 */
	if (lcd->screen & LCD_SCREEN_SEN) {
		/* Wait for vertical sync period */
		lcd->intstatus = LCD_INT_SS;
		while ((lcd->intstatus & LCD_INT_SS) == 0)
			;

		lcd->screen &= ~LCD_SCREEN_SEN;	/*disable the controller*/

		do {
			lcd->intstatus = lcd->intstatus; /*clear interrupts*/
			wmb(); /* drain writebuffer */
		/*wait for controller to shut down*/
		} while ((lcd->intstatus & LCD_INT_SD) == 0);

		/* Call shutdown of current panel (if up) */
		/* this must occur last, because if an external clock is driving
		    the controller, the clock cannot be turned off before first
			shutting down the controller.
		 */
		if (pd->panel_shutdown)
			pd->panel_shutdown();
	}

	/* Newpanel == NULL indicates a shutdown operation only */
	if (newpanel == NULL)
		return;

	panel = newpanel;

	printk("Panel(%s), %dx%d\n", panel->name, panel->Xres, panel->Yres);

	/*
	 * Setup clocking if internal LCD clock source (assumes sys_auxpll valid)
	 */
	if (!(panel->mode_clkcontrol & LCD_CLKCONTROL_EXT))
	{
		struct clk *c = clk_get(NULL, "lcd_intclk");
		long r, pc = panel->lcdclk * 1000000;

		if (!IS_ERR(c)) {
			r = clk_round_rate(c, pc);
			if ((pc - r) < (pc / 10)) {	/* 10% slack */
				clk_set_rate(c, r);
				clk_prepare_enable(c);
			}
			clk_put(c);
		}
	}

	/*
	 * Configure panel timings
	 */
	lcd->screen = panel->mode_screen;
	lcd->horztiming = panel->mode_horztiming;
	lcd->verttiming = panel->mode_verttiming;
	lcd->clkcontrol = panel->mode_clkcontrol;
	lcd->pwmdiv = panel->mode_pwmdiv;
	lcd->pwmhi = panel->mode_pwmhi;
	lcd->outmask = panel->mode_outmask;
	lcd->fifoctrl = panel->mode_fifoctrl;
	wmb(); /* drain writebuffer */

	/* fixme: Check window settings to make sure still valid
	 * for new geometry */
#if 0
	au1200_setlocation(fbdev, 0, win->w[0].xpos, win->w[0].ypos);
	au1200_setlocation(fbdev, 1, win->w[1].xpos, win->w[1].ypos);
	au1200_setlocation(fbdev, 2, win->w[2].xpos, win->w[2].ypos);
	au1200_setlocation(fbdev, 3, win->w[3].xpos, win->w[3].ypos);
#endif
	lcd->winenable = winenable;

	/*
	 * Re-enable screen now that it is configured
	 */
	lcd->screen |= LCD_SCREEN_SEN;
	wmb(); /* drain writebuffer */

	/* Call init of panel */
	if (pd->panel_init)
		pd->panel_init();

	/* FIX!!!! not appropriate on panel change!!! Global setup/init */
	lcd->intenable = 0;
	lcd->intstatus = ~0;
	lcd->backcolor = win->mode_backcolor;

	/* Setup Color Key - FIX!!! */
	lcd->colorkey = win->mode_colorkey;
	lcd->colorkeymsk = win->mode_colorkeymsk;

	/* Setup HWCursor - FIX!!! Need to support this eventually */
	lcd->hwc.cursorctrl = 0;
	lcd->hwc.cursorpos = 0;
	lcd->hwc.cursorcolor0 = 0;
	lcd->hwc.cursorcolor1 = 0;
	lcd->hwc.cursorcolor2 = 0;
	lcd->hwc.cursorcolor3 = 0;


#if 0
#define D(X) printk("%25s: %08X\n", #X, X)
	D(lcd->screen);
	D(lcd->horztiming);
	D(lcd->verttiming);
	D(lcd->clkcontrol);
	D(lcd->pwmdiv);
	D(lcd->pwmhi);
	D(lcd->outmask);
	D(lcd->fifoctrl);
	D(lcd->window[0].winctrl0);
	D(lcd->window[0].winctrl1);
	D(lcd->window[0].winctrl2);
	D(lcd->window[0].winbuf0);
	D(lcd->window[0].winbuf1);
	D(lcd->window[0].winbufctrl);
	D(lcd->window[1].winctrl0);
	D(lcd->window[1].winctrl1);
	D(lcd->window[1].winctrl2);
	D(lcd->window[1].winbuf0);
	D(lcd->window[1].winbuf1);
	D(lcd->window[1].winbufctrl);
	D(lcd->window[2].winctrl0);
	D(lcd->window[2].winctrl1);
	D(lcd->window[2].winctrl2);
	D(lcd->window[2].winbuf0);
	D(lcd->window[2].winbuf1);
	D(lcd->window[2].winbufctrl);
	D(lcd->window[3].winctrl0);
	D(lcd->window[3].winctrl1);
	D(lcd->window[3].winctrl2);
	D(lcd->window[3].winbuf0);
	D(lcd->window[3].winbuf1);
	D(lcd->window[3].winbufctrl);
	D(lcd->winenable);
	D(lcd->intenable);
	D(lcd->intstatus);
	D(lcd->backcolor);
	D(lcd->winenable);
	D(lcd->colorkey);
    D(lcd->colorkeymsk);
	D(lcd->hwc.cursorctrl);
	D(lcd->hwc.cursorpos);
	D(lcd->hwc.cursorcolor0);
	D(lcd->hwc.cursorcolor1);
	D(lcd->hwc.cursorcolor2);
	D(lcd->hwc.cursorcolor3);
#endif
}

static void au1200_setmode(struct au1200fb_device *fbdev)
{
	int plane = fbdev->plane;
	/* Window/plane setup */
	lcd->window[plane].winctrl1 = ( 0
		| LCD_WINCTRL1_PRI_N(plane)
		| win->w[plane].mode_winctrl1 /* FRM,CCO,PO,PIPE */
		) ;

	au1200_setlocation(fbdev, plane, win->w[plane].xpos, win->w[plane].ypos);

	lcd->window[plane].winctrl2 = ( 0
		| LCD_WINCTRL2_CKMODE_00
		| LCD_WINCTRL2_DBM
		| LCD_WINCTRL2_BX_N(fbdev->fb_info->fix.line_length)
		| LCD_WINCTRL2_SCX_1
		| LCD_WINCTRL2_SCY_1
		) ;
	lcd->winenable |= win->w[plane].mode_winenable;
	wmb(); /* drain writebuffer */
}


/* Inline helpers */

/*#define panel_is_dual(panel)  ((panel->mode_screen & LCD_SCREEN_PT) == LCD_SCREEN_PT_010)*/
/*#define panel_is_active(panel)((panel->mode_screen & LCD_SCREEN_PT) == LCD_SCREEN_PT_010)*/

#define panel_is_color(panel) ((panel->mode_screen & LCD_SCREEN_PT) <= LCD_SCREEN_PT_CDSTN)

/* Bitfields format supported by the controller. */
static struct fb_bitfield rgb_bitfields[][4] = {
  	/*     Red, 	   Green, 	 Blue, 	     Transp   */
	[LCD_WINCTRL1_FRM_16BPP655 >> 25] =
		{ { 10, 6, 0 }, { 5, 5, 0 }, { 0, 5, 0 }, { 0, 0, 0 } },

	[LCD_WINCTRL1_FRM_16BPP565 >> 25] =
		{ { 11, 5, 0 }, { 5, 6, 0 }, { 0, 5, 0 }, { 0, 0, 0 } },

	[LCD_WINCTRL1_FRM_16BPP556 >> 25] =
		{ { 11, 5, 0 }, { 6, 5, 0 }, { 0, 6, 0 }, { 0, 0, 0 } },

	[LCD_WINCTRL1_FRM_16BPPI1555 >> 25] =
		{ { 10, 5, 0 }, { 5, 5, 0 }, { 0, 5, 0 }, { 0, 0, 0 } },

	[LCD_WINCTRL1_FRM_16BPPI5551 >> 25] =
		{ { 11, 5, 0 }, { 6, 5, 0 }, { 1, 5, 0 }, { 0, 0, 0 } },

	[LCD_WINCTRL1_FRM_16BPPA1555 >> 25] =
		{ { 10, 5, 0 }, { 5, 5, 0 }, { 0, 5, 0 }, { 15, 1, 0 } },

	[LCD_WINCTRL1_FRM_16BPPA5551 >> 25] =
		{ { 11, 5, 0 }, { 6, 5, 0 }, { 1, 5, 0 }, { 0, 1, 0 } },

	[LCD_WINCTRL1_FRM_24BPP >> 25] =
		{ { 16, 8, 0 }, { 8, 8, 0 }, { 0, 8, 0 }, { 0, 0, 0 } },

	[LCD_WINCTRL1_FRM_32BPP >> 25] =
		{ { 16, 8, 0 }, { 8, 8, 0 }, { 0, 8, 0 }, { 24, 0, 0 } },
};

/*-------------------------------------------------------------------------*/

/* Helpers */

static void au1200fb_update_fbinfo(struct fb_info *fbi)
{
	/* FIX!!!! This also needs to take the window pixel format into account!!! */

	/* Update var-dependent FB info */
	if (panel_is_color(panel)) {
		if (fbi->var.bits_per_pixel <= 8) {
			/* palettized */
			fbi->fix.visual = FB_VISUAL_PSEUDOCOLOR;
			fbi->fix.line_length = fbi->var.xres_virtual /
				(8/fbi->var.bits_per_pixel);
		} else {
			/* non-palettized */
			fbi->fix.visual = FB_VISUAL_TRUECOLOR;
			fbi->fix.line_length = fbi->var.xres_virtual * (fbi->var.bits_per_pixel / 8);
		}
	} else {
		/* mono FIX!!! mono 8 and 4 bits */
		fbi->fix.visual = FB_VISUAL_MONO10;
		fbi->fix.line_length = fbi->var.xres_virtual / 8;
	}

	fbi->screen_size = fbi->fix.line_length * fbi->var.yres_virtual;
	print_dbg("line length: %d\n", fbi->fix.line_length);
	print_dbg("bits_per_pixel: %d\n", fbi->var.bits_per_pixel);
}

/*-------------------------------------------------------------------------*/

/* AU1200 framebuffer driver */

/* fb_check_var
 * Validate var settings with hardware restrictions and modify it if necessary
 */
static int au1200fb_fb_check_var(struct fb_var_screeninfo *var,
	struct fb_info *fbi)
{
	struct au1200fb_device *fbdev = fbi->par;
	u32 pixclock;
	int screen_size, plane;

	if (!var->pixclock)
		return -EINVAL;

	plane = fbdev->plane;

	/* Make sure that the mode respect all LCD controller and
	 * panel restrictions. */
	var->xres = win->w[plane].xres;
	var->yres = win->w[plane].yres;

	/* No need for virtual resolution support */
	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;

	var->bits_per_pixel = winbpp(win->w[plane].mode_winctrl1);

	screen_size = var->xres_virtual * var->yres_virtual;
	if (var->bits_per_pixel > 8) screen_size *= (var->bits_per_pixel / 8);
	else screen_size /= (8/var->bits_per_pixel);

	if (fbdev->fb_len < screen_size)
		return -EINVAL; /* Virtual screen is to big, abort */

	/* FIX!!!! what are the implicaitons of ignoring this for windows ??? */
	/* The max LCD clock is fixed to 48MHz (value of AUX_CLK). The pixel
	 * clock can only be obtain by dividing this value by an even integer.
	 * Fallback to a slower pixel clock if necessary. */
	pixclock = max((u32)(PICOS2KHZ(var->pixclock) * 1000), fbi->monspecs.dclkmin);
	pixclock = min3(pixclock, fbi->monspecs.dclkmax, (u32)AU1200_LCD_MAX_CLK/2);

	if (AU1200_LCD_MAX_CLK % pixclock) {
		int diff = AU1200_LCD_MAX_CLK % pixclock;
		pixclock -= diff;
	}

	var->pixclock = KHZ2PICOS(pixclock/1000);
#if 0
	if (!panel_is_active(panel)) {
		int pcd = AU1200_LCD_MAX_CLK / (pixclock * 2) - 1;

		if (!panel_is_color(panel)
			&& (panel->control_base & LCD_CONTROL_MPI) && (pcd < 3)) {
			/* STN 8bit mono panel support is up to 6MHz pixclock */
			var->pixclock = KHZ2PICOS(6000);
		} else if (!pcd) {
			/* Other STN panel support is up to 12MHz  */
			var->pixclock = KHZ2PICOS(12000);
		}
	}
#endif
	/* Set bitfield accordingly */
	switch (var->bits_per_pixel) {
		case 16:
		{
			/* 16bpp True color.
			 * These must be set to MATCH WINCTRL[FORM] */
			int idx;
			idx = (win->w[0].mode_winctrl1 & LCD_WINCTRL1_FRM) >> 25;
			var->red    = rgb_bitfields[idx][0];
			var->green  = rgb_bitfields[idx][1];
			var->blue   = rgb_bitfields[idx][2];
			var->transp = rgb_bitfields[idx][3];
			break;
		}

		case 32:
		{
			/* 32bpp True color.
			 * These must be set to MATCH WINCTRL[FORM] */
			int idx;
			idx = (win->w[0].mode_winctrl1 & LCD_WINCTRL1_FRM) >> 25;
			var->red    = rgb_bitfields[idx][0];
			var->green  = rgb_bitfields[idx][1];
			var->blue   = rgb_bitfields[idx][2];
			var->transp = rgb_bitfields[idx][3];
			break;
		}
		default:
			print_dbg("Unsupported depth %dbpp", var->bits_per_pixel);
			return -EINVAL;
	}

	return 0;
}

/* fb_set_par
 * Set hardware with var settings. This will enable the controller with a
 * specific mode, normally validated with the fb_check_var method
 */
static int au1200fb_fb_set_par(struct fb_info *fbi)
{
	struct au1200fb_device *fbdev = fbi->par;

	au1200fb_update_fbinfo(fbi);
	au1200_setmode(fbdev);

	return 0;
}

/* fb_setcolreg
 * Set color in LCD palette.
 */
static int au1200fb_fb_setcolreg(unsigned regno, unsigned red, unsigned green,
	unsigned blue, unsigned transp, struct fb_info *fbi)
{
	volatile u32 *palette = lcd->palette;
	u32 value;

	if (regno > (AU1200_LCD_NBR_PALETTE_ENTRIES - 1))
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

		palette = (u32*) fbi->pseudo_palette;

		red   >>= (16 - fbi->var.red.length);
		green >>= (16 - fbi->var.green.length);
		blue  >>= (16 - fbi->var.blue.length);

		value = (red   << fbi->var.red.offset) 	|
			(green << fbi->var.green.offset)|
			(blue  << fbi->var.blue.offset);
		value &= 0xFFFF;

	} else if (1 /*FIX!!! panel_is_active(fbdev->panel)*/) {
		/* COLOR TFT PALLETTIZED (use RGB 565) */
		value = (red & 0xF800)|((green >> 5) &
				0x07E0)|((blue >> 11) & 0x001F);
		value &= 0xFFFF;

	} else if (0 /*panel_is_color(fbdev->panel)*/) {
		/* COLOR STN MODE */
		value = 0x1234;
		value &= 0xFFF;
	} else {
		/* MONOCHROME MODE */
		value = (green >> 12) & 0x000F;
		value &= 0xF;
	}

	palette[regno] = value;

	return 0;
}

/* fb_blank
 * Blank the screen. Depending on the mode, the screen will be
 * activated with the backlight color, or desactivated
 */
static int au1200fb_fb_blank(int blank_mode, struct fb_info *fbi)
{
	struct au1200fb_device *fbdev = fbi->par;

	/* Short-circuit screen blanking */
	if (noblanking)
		return 0;

	switch (blank_mode) {

	case FB_BLANK_UNBLANK:
	case FB_BLANK_NORMAL:
		/* printk("turn on panel\n"); */
		au1200_setpanel(panel, fbdev->pd);
		break;
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_POWERDOWN:
		/* printk("turn off panel\n"); */
		au1200_setpanel(NULL, fbdev->pd);
		break;
	default:
		break;

	}

	/* FB_BLANK_NORMAL is a soft blank */
	return (blank_mode == FB_BLANK_NORMAL) ? -EINVAL : 0;
}

/* fb_mmap
 * Map video memory in user space. We don't use the generic fb_mmap
 * method mainly to allow the use of the TLB streaming flag (CCA=6)
 */
static int au1200fb_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct au1200fb_device *fbdev = info->par;

	vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);

	return dma_mmap_coherent(fbdev->dev, vma,
				 fbdev->fb_mem, fbdev->fb_phys, fbdev->fb_len);
}

static void set_global(u_int cmd, struct au1200_lcd_global_regs_t *pdata)
{

	unsigned int hi1, divider;

	/* SCREEN_SIZE: user cannot reset size, must switch panel choice */

	if (pdata->flags & SCREEN_BACKCOLOR)
		lcd->backcolor = pdata->backcolor;

	if (pdata->flags & SCREEN_BRIGHTNESS) {

		// limit brightness pwm duty to >= 30/1600
		if (pdata->brightness < 30) {
			pdata->brightness = 30;
		}
		divider = (lcd->pwmdiv & 0x3FFFF) + 1;
		hi1 = (((pdata->brightness & 0xFF)+1) * divider >> 8);
		lcd->pwmhi &= 0xFFFF;
		lcd->pwmhi |= (hi1 << 16);
	}

	if (pdata->flags & SCREEN_COLORKEY)
		lcd->colorkey = pdata->colorkey;

	if (pdata->flags & SCREEN_MASK)
		lcd->colorkeymsk = pdata->mask;
	wmb(); /* drain writebuffer */
}

static void get_global(u_int cmd, struct au1200_lcd_global_regs_t *pdata)
{
	unsigned int hi1, divider;

	pdata->xsize = ((lcd->screen & LCD_SCREEN_SX) >> 19) + 1;
	pdata->ysize = ((lcd->screen & LCD_SCREEN_SY) >> 8) + 1;

	pdata->backcolor = lcd->backcolor;
	pdata->colorkey = lcd->colorkey;
	pdata->mask = lcd->colorkeymsk;

	// brightness
	hi1 = (lcd->pwmhi >> 16) + 1;
	divider = (lcd->pwmdiv & 0x3FFFF) + 1;
	pdata->brightness = ((hi1 << 8) / divider) - 1;
	wmb(); /* drain writebuffer */
}

static void set_window(unsigned int plane,
	struct au1200_lcd_window_regs_t *pdata)
{
	unsigned int val, bpp;

	/* Window control register 0 */
	if (pdata->flags & WIN_POSITION) {
		val = lcd->window[plane].winctrl0 & ~(LCD_WINCTRL0_OX |
				LCD_WINCTRL0_OY);
		val |= ((pdata->xpos << 21) & LCD_WINCTRL0_OX);
		val |= ((pdata->ypos << 10) & LCD_WINCTRL0_OY);
		lcd->window[plane].winctrl0 = val;
	}
	if (pdata->flags & WIN_ALPHA_COLOR) {
		val = lcd->window[plane].winctrl0 & ~(LCD_WINCTRL0_A);
		val |= ((pdata->alpha_color << 2) & LCD_WINCTRL0_A);
		lcd->window[plane].winctrl0 = val;
	}
	if (pdata->flags & WIN_ALPHA_MODE) {
		val = lcd->window[plane].winctrl0 & ~(LCD_WINCTRL0_AEN);
		val |= ((pdata->alpha_mode << 1) & LCD_WINCTRL0_AEN);
		lcd->window[plane].winctrl0 = val;
	}

	/* Window control register 1 */
	if (pdata->flags & WIN_PRIORITY) {
		val = lcd->window[plane].winctrl1 & ~(LCD_WINCTRL1_PRI);
		val |= ((pdata->priority << 30) & LCD_WINCTRL1_PRI);
		lcd->window[plane].winctrl1 = val;
	}
	if (pdata->flags & WIN_CHANNEL) {
		val = lcd->window[plane].winctrl1 & ~(LCD_WINCTRL1_PIPE);
		val |= ((pdata->channel << 29) & LCD_WINCTRL1_PIPE);
		lcd->window[plane].winctrl1 = val;
	}
	if (pdata->flags & WIN_BUFFER_FORMAT) {
		val = lcd->window[plane].winctrl1 & ~(LCD_WINCTRL1_FRM);
		val |= ((pdata->buffer_format << 25) & LCD_WINCTRL1_FRM);
		lcd->window[plane].winctrl1 = val;
	}
	if (pdata->flags & WIN_COLOR_ORDER) {
		val = lcd->window[plane].winctrl1 & ~(LCD_WINCTRL1_CCO);
		val |= ((pdata->color_order << 24) & LCD_WINCTRL1_CCO);
		lcd->window[plane].winctrl1 = val;
	}
	if (pdata->flags & WIN_PIXEL_ORDER) {
		val = lcd->window[plane].winctrl1 & ~(LCD_WINCTRL1_PO);
		val |= ((pdata->pixel_order << 22) & LCD_WINCTRL1_PO);
		lcd->window[plane].winctrl1 = val;
	}
	if (pdata->flags & WIN_SIZE) {
		val = lcd->window[plane].winctrl1 & ~(LCD_WINCTRL1_SZX |
				LCD_WINCTRL1_SZY);
		val |= (((pdata->xsize << 11) - 1) & LCD_WINCTRL1_SZX);
		val |= (((pdata->ysize) - 1) & LCD_WINCTRL1_SZY);
		lcd->window[plane].winctrl1 = val;
		/* program buffer line width */
		bpp = winbpp(val) / 8;
		val = lcd->window[plane].winctrl2 & ~(LCD_WINCTRL2_BX);
		val |= (((pdata->xsize * bpp) << 8) & LCD_WINCTRL2_BX);
		lcd->window[plane].winctrl2 = val;
	}

	/* Window control register 2 */
	if (pdata->flags & WIN_COLORKEY_MODE) {
		val = lcd->window[plane].winctrl2 & ~(LCD_WINCTRL2_CKMODE);
		val |= ((pdata->colorkey_mode << 24) & LCD_WINCTRL2_CKMODE);
		lcd->window[plane].winctrl2 = val;
	}
	if (pdata->flags & WIN_DOUBLE_BUFFER_MODE) {
		val = lcd->window[plane].winctrl2 & ~(LCD_WINCTRL2_DBM);
		val |= ((pdata->double_buffer_mode << 23) & LCD_WINCTRL2_DBM);
		lcd->window[plane].winctrl2 = val;
	}
	if (pdata->flags & WIN_RAM_ARRAY_MODE) {
		val = lcd->window[plane].winctrl2 & ~(LCD_WINCTRL2_RAM);
		val |= ((pdata->ram_array_mode << 21) & LCD_WINCTRL2_RAM);
		lcd->window[plane].winctrl2 = val;
	}

	/* Buffer line width programmed with WIN_SIZE */

	if (pdata->flags & WIN_BUFFER_SCALE) {
		val = lcd->window[plane].winctrl2 & ~(LCD_WINCTRL2_SCX |
				LCD_WINCTRL2_SCY);
		val |= ((pdata->xsize << 11) & LCD_WINCTRL2_SCX);
		val |= ((pdata->ysize) & LCD_WINCTRL2_SCY);
		lcd->window[plane].winctrl2 = val;
	}

	if (pdata->flags & WIN_ENABLE) {
		val = lcd->winenable;
		val &= ~(1<<plane);
		val |= (pdata->enable & 1) << plane;
		lcd->winenable = val;
	}
	wmb(); /* drain writebuffer */
}

static void get_window(unsigned int plane,
	struct au1200_lcd_window_regs_t *pdata)
{
	/* Window control register 0 */
	pdata->xpos = (lcd->window[plane].winctrl0 & LCD_WINCTRL0_OX) >> 21;
	pdata->ypos = (lcd->window[plane].winctrl0 & LCD_WINCTRL0_OY) >> 10;
	pdata->alpha_color = (lcd->window[plane].winctrl0 & LCD_WINCTRL0_A) >> 2;
	pdata->alpha_mode = (lcd->window[plane].winctrl0 & LCD_WINCTRL0_AEN) >> 1;

	/* Window control register 1 */
	pdata->priority = (lcd->window[plane].winctrl1& LCD_WINCTRL1_PRI) >> 30;
	pdata->channel = (lcd->window[plane].winctrl1 & LCD_WINCTRL1_PIPE) >> 29;
	pdata->buffer_format = (lcd->window[plane].winctrl1 & LCD_WINCTRL1_FRM) >> 25;
	pdata->color_order = (lcd->window[plane].winctrl1 & LCD_WINCTRL1_CCO) >> 24;
	pdata->pixel_order = (lcd->window[plane].winctrl1 & LCD_WINCTRL1_PO) >> 22;
	pdata->xsize = ((lcd->window[plane].winctrl1 & LCD_WINCTRL1_SZX) >> 11) + 1;
	pdata->ysize = (lcd->window[plane].winctrl1 & LCD_WINCTRL1_SZY) + 1;

	/* Window control register 2 */
	pdata->colorkey_mode = (lcd->window[plane].winctrl2 & LCD_WINCTRL2_CKMODE) >> 24;
	pdata->double_buffer_mode = (lcd->window[plane].winctrl2 & LCD_WINCTRL2_DBM) >> 23;
	pdata->ram_array_mode = (lcd->window[plane].winctrl2 & LCD_WINCTRL2_RAM) >> 21;

	pdata->enable = (lcd->winenable >> plane) & 1;
	wmb(); /* drain writebuffer */
}

static int au1200fb_ioctl(struct fb_info *info, unsigned int cmd,
                          unsigned long arg)
{
	struct au1200fb_device *fbdev = info->par;
	int plane;
	int val;

	plane = fbinfo2index(info);
	print_dbg("au1200fb: ioctl %d on plane %d\n", cmd, plane);

	if (cmd == AU1200_LCD_FB_IOCTL) {
		struct au1200_lcd_iodata_t iodata;

		if (copy_from_user(&iodata, (void __user *) arg, sizeof(iodata)))
			return -EFAULT;

		print_dbg("FB IOCTL called\n");

		switch (iodata.subcmd) {
		case AU1200_LCD_SET_SCREEN:
			print_dbg("AU1200_LCD_SET_SCREEN\n");
			set_global(cmd, &iodata.global);
			break;

		case AU1200_LCD_GET_SCREEN:
			print_dbg("AU1200_LCD_GET_SCREEN\n");
			get_global(cmd, &iodata.global);
			break;

		case AU1200_LCD_SET_WINDOW:
			print_dbg("AU1200_LCD_SET_WINDOW\n");
			set_window(plane, &iodata.window);
			break;

		case AU1200_LCD_GET_WINDOW:
			print_dbg("AU1200_LCD_GET_WINDOW\n");
			get_window(plane, &iodata.window);
			break;

		case AU1200_LCD_SET_PANEL:
			print_dbg("AU1200_LCD_SET_PANEL\n");
			if ((iodata.global.panel_choice >= 0) &&
					(iodata.global.panel_choice <
					 NUM_PANELS))
			{
				struct panel_settings *newpanel;
				panel_index = iodata.global.panel_choice;
				newpanel = &known_lcd_panels[panel_index];
				au1200_setpanel(newpanel, fbdev->pd);
			}
			break;

		case AU1200_LCD_GET_PANEL:
			print_dbg("AU1200_LCD_GET_PANEL\n");
			iodata.global.panel_choice = panel_index;
			break;

		default:
			return -EINVAL;
		}

		val = copy_to_user((void __user *) arg, &iodata, sizeof(iodata));
		if (val) {
			print_dbg("error: could not copy %d bytes\n", val);
			return -EFAULT;
		}
	}

	return 0;
}


static const struct fb_ops au1200fb_fb_ops = {
	.owner		= THIS_MODULE,
	__FB_DEFAULT_DMAMEM_OPS_RDWR,
	.fb_check_var	= au1200fb_fb_check_var,
	.fb_set_par	= au1200fb_fb_set_par,
	.fb_setcolreg	= au1200fb_fb_setcolreg,
	.fb_blank	= au1200fb_fb_blank,
	__FB_DEFAULT_DMAMEM_OPS_DRAW,
	.fb_sync	= NULL,
	.fb_ioctl	= au1200fb_ioctl,
	.fb_mmap	= au1200fb_fb_mmap,
};

/*-------------------------------------------------------------------------*/

static irqreturn_t au1200fb_handle_irq(int irq, void* dev_id)
{
	/* Nothing to do for now, just clear any pending interrupt */
	lcd->intstatus = lcd->intstatus;
	wmb(); /* drain writebuffer */

	return IRQ_HANDLED;
}

/*-------------------------------------------------------------------------*/

/* AU1200 LCD device probe helpers */

static int au1200fb_init_fbinfo(struct au1200fb_device *fbdev)
{
	struct fb_info *fbi = fbdev->fb_info;
	int bpp, ret;

	fbi->fbops = &au1200fb_fb_ops;

	bpp = winbpp(win->w[fbdev->plane].mode_winctrl1);

	/* Copy monitor specs from panel data */
	/* fixme: we're setting up LCD controller windows, so these dont give a
	damn as to what the monitor specs are (the panel itself does, but that
	isn't done here...so maybe need a generic catchall monitor setting??? */
	memcpy(&fbi->monspecs, &panel->monspecs, sizeof(struct fb_monspecs));

	/* We first try the user mode passed in argument. If that failed,
	 * or if no one has been specified, we default to the first mode of the
	 * panel list. Note that after this call, var data will be set */
	if (!fb_find_mode(&fbi->var,
			  fbi,
			  NULL, /* drv_info.opt_mode, */
			  fbi->monspecs.modedb,
			  fbi->monspecs.modedb_len,
			  fbi->monspecs.modedb,
			  bpp)) {

		print_err("Cannot find valid mode for panel %s", panel->name);
		return -EFAULT;
	}

	fbi->pseudo_palette = kcalloc(16, sizeof(u32), GFP_KERNEL);
	if (!fbi->pseudo_palette)
		return -ENOMEM;

	ret = fb_alloc_cmap(&fbi->cmap, AU1200_LCD_NBR_PALETTE_ENTRIES, 0);
	if (ret < 0) {
		print_err("Fail to allocate colormap (%d entries)",
			  AU1200_LCD_NBR_PALETTE_ENTRIES);
		return ret;
	}

	strscpy(fbi->fix.id, "AU1200");
	fbi->fix.smem_start = fbdev->fb_phys;
	fbi->fix.smem_len = fbdev->fb_len;
	fbi->fix.type = FB_TYPE_PACKED_PIXELS;
	fbi->fix.xpanstep = 0;
	fbi->fix.ypanstep = 0;
	fbi->fix.mmio_start = 0;
	fbi->fix.mmio_len = 0;
	fbi->fix.accel = FB_ACCEL_NONE;

	fbi->flags |= FBINFO_VIRTFB;

	fbi->screen_buffer = fbdev->fb_mem;

	au1200fb_update_fbinfo(fbi);

	return 0;
}

/*-------------------------------------------------------------------------*/


static int au1200fb_setup(struct au1200fb_platdata *pd)
{
	char *options = NULL;
	char *this_opt, *endptr;
	int num_panels = ARRAY_SIZE(known_lcd_panels);
	int panel_idx = -1;

	fb_get_options(DRIVER_NAME, &options);

	if (!options)
		goto out;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		/* Panel option - can be panel name,
		 * "bs" for board-switch, or number/index */
		if (!strncmp(this_opt, "panel:", 6)) {
			int i;
			long int li;
			char *endptr;
			this_opt += 6;
			/* First check for index, which allows
			 * to short circuit this mess */
			li = simple_strtol(this_opt, &endptr, 0);
			if (*endptr == '\0')
				panel_idx = (int)li;
			else if (strcmp(this_opt, "bs") == 0)
				panel_idx = pd->panel_index();
			else {
				for (i = 0; i < num_panels; i++) {
					if (!strcmp(this_opt,
						    known_lcd_panels[i].name)) {
						panel_idx = i;
						break;
					}
				}
			}
			if ((panel_idx < 0) || (panel_idx >= num_panels))
				print_warn("Panel %s not supported!", this_opt);
			else
				panel_index = panel_idx;

		} else if (strncmp(this_opt, "nohwcursor", 10) == 0)
			nohwcursor = 1;
		else if (strncmp(this_opt, "devices:", 8) == 0) {
			this_opt += 8;
			device_count = simple_strtol(this_opt, &endptr, 0);
			if ((device_count < 0) ||
			    (device_count > MAX_DEVICE_COUNT))
				device_count = MAX_DEVICE_COUNT;
		} else if (strncmp(this_opt, "wincfg:", 7) == 0) {
			this_opt += 7;
			window_index = simple_strtol(this_opt, &endptr, 0);
			if ((window_index < 0) ||
			    (window_index >= ARRAY_SIZE(windows)))
				window_index = DEFAULT_WINDOW_INDEX;
		} else if (strncmp(this_opt, "off", 3) == 0)
			return 1;
		else
			print_warn("Unsupported option \"%s\"", this_opt);
	}

out:
	return 0;
}

/* AU1200 LCD controller device driver */
static int au1200fb_drv_probe(struct platform_device *dev)
{
	struct au1200fb_device *fbdev;
	struct au1200fb_platdata *pd;
	struct fb_info *fbi = NULL;
	int bpp, plane, ret, irq;

	print_info("" DRIVER_DESC "");

	pd = dev->dev.platform_data;
	if (!pd)
		return -ENODEV;

	/* Setup driver with options */
	if (au1200fb_setup(pd))
		return -ENODEV;

	/* Point to the panel selected */
	panel = &known_lcd_panels[panel_index];
	win = &windows[window_index];

	printk(DRIVER_NAME ": Panel %d %s\n", panel_index, panel->name);
	printk(DRIVER_NAME ": Win %d %s\n", window_index, win->name);

	for (plane = 0; plane < device_count; ++plane) {
		bpp = winbpp(win->w[plane].mode_winctrl1);
		if (win->w[plane].xres == 0)
			win->w[plane].xres = panel->Xres;
		if (win->w[plane].yres == 0)
			win->w[plane].yres = panel->Yres;

		fbi = framebuffer_alloc(sizeof(struct au1200fb_device),
					&dev->dev);
		if (!fbi) {
			ret = -ENOMEM;
			goto failed;
		}

		_au1200fb_infos[plane] = fbi;
		fbdev = fbi->par;
		fbdev->fb_info = fbi;
		fbdev->pd = pd;
		fbdev->dev = &dev->dev;

		fbdev->plane = plane;

		/* Allocate the framebuffer to the maximum screen size */
		fbdev->fb_len = (win->w[plane].xres * win->w[plane].yres * bpp) / 8;

		fbdev->fb_mem = dmam_alloc_attrs(&dev->dev,
				PAGE_ALIGN(fbdev->fb_len),
				&fbdev->fb_phys, GFP_KERNEL, 0);
		if (!fbdev->fb_mem) {
			print_err("fail to allocate framebuffer (size: %dK))",
				  fbdev->fb_len / 1024);
			ret = -ENOMEM;
			goto failed;
		}

		print_dbg("Framebuffer memory map at %p", fbdev->fb_mem);
		print_dbg("phys=0x%08x, size=%dK", fbdev->fb_phys, fbdev->fb_len / 1024);

		/* Init FB data */
		ret = au1200fb_init_fbinfo(fbdev);
		if (ret < 0)
			goto failed;

		/* Register new framebuffer */
		ret = register_framebuffer(fbi);
		if (ret < 0) {
			print_err("cannot register new framebuffer");
			goto failed;
		}

		au1200fb_fb_set_par(fbi);
	}

	/* Now hook interrupt too */
	irq = platform_get_irq(dev, 0);
	if (irq < 0)
		return irq;

	ret = request_irq(irq, au1200fb_handle_irq,
			  IRQF_SHARED, "lcd", (void *)dev);
	if (ret) {
		print_err("fail to request interrupt line %d (err: %d)",
			  irq, ret);
		goto failed;
	}

	platform_set_drvdata(dev, pd);

	/* Kickstart the panel */
	au1200_setpanel(panel, pd);

	return 0;

failed:
	for (plane = 0; plane < device_count; ++plane) {
		fbi = _au1200fb_infos[plane];
		if (!fbi)
			break;

		/* Clean up all probe data */
		unregister_framebuffer(fbi);
		if (fbi->cmap.len != 0)
			fb_dealloc_cmap(&fbi->cmap);
		kfree(fbi->pseudo_palette);

		framebuffer_release(fbi);
		_au1200fb_infos[plane] = NULL;
	}
	return ret;
}

static void au1200fb_drv_remove(struct platform_device *dev)
{
	struct au1200fb_platdata *pd = platform_get_drvdata(dev);
	struct fb_info *fbi;
	int plane;

	/* Turn off the panel */
	au1200_setpanel(NULL, pd);

	for (plane = 0; plane < device_count; ++plane)	{
		fbi = _au1200fb_infos[plane];

		/* Clean up all probe data */
		unregister_framebuffer(fbi);
		if (fbi->cmap.len != 0)
			fb_dealloc_cmap(&fbi->cmap);
		kfree(fbi->pseudo_palette);

		framebuffer_release(fbi);
		_au1200fb_infos[plane] = NULL;
	}

	free_irq(platform_get_irq(dev, 0), (void *)dev);
}

#ifdef CONFIG_PM
static int au1200fb_drv_suspend(struct device *dev)
{
	struct au1200fb_platdata *pd = dev_get_drvdata(dev);
	au1200_setpanel(NULL, pd);

	lcd->outmask = 0;
	wmb(); /* drain writebuffer */

	return 0;
}

static int au1200fb_drv_resume(struct device *dev)
{
	struct au1200fb_platdata *pd = dev_get_drvdata(dev);
	struct fb_info *fbi;
	int i;

	/* Kickstart the panel */
	au1200_setpanel(panel, pd);

	for (i = 0; i < device_count; i++) {
		fbi = _au1200fb_infos[i];
		au1200fb_fb_set_par(fbi);
	}

	return 0;
}

static const struct dev_pm_ops au1200fb_pmops = {
	.suspend	= au1200fb_drv_suspend,
	.resume		= au1200fb_drv_resume,
	.freeze		= au1200fb_drv_suspend,
	.thaw		= au1200fb_drv_resume,
};

#define AU1200FB_PMOPS	(&au1200fb_pmops)

#else
#define AU1200FB_PMOPS	NULL
#endif /* CONFIG_PM */

static struct platform_driver au1200fb_driver = {
	.driver = {
		.name	= "au1200-lcd",
		.pm	= AU1200FB_PMOPS,
	},
	.probe		= au1200fb_drv_probe,
	.remove		= au1200fb_drv_remove,
};
module_platform_driver(au1200fb_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
