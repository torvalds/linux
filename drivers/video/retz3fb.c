/*
 * Linux/drivers/video/retz3fb.c -- RetinaZ3 frame buffer device
 *
 *    Copyright (C) 1997 Jes Sorensen
 *
 * This file is based on the CyberVision64 frame buffer device and
 * the generic Cirrus Logic driver.
 *
 * cyberfb.c: Copyright (C) 1996 Martin Apel,
 *                               Geert Uytterhoeven
 * clgen.c:   Copyright (C) 1996 Frank Neumann
 *
 * History:
 *   - 22 Jan 97: Initial work
 *   - 14 Feb 97: Screen initialization works somewhat, still only
 *                8-bit packed pixel is supported.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/zorro.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/io.h>

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>

#include "retz3fb.h"

/* #define DEBUG if(1) */
#define DEBUG if(0)

/*
 * Reserve space for one pattern line.
 *
 * For the time being we only support 4MB boards!
 */

#define PAT_MEM_SIZE 16*3
#define PAT_MEM_OFF  (4*1024*1024 - PAT_MEM_SIZE)

struct retz3fb_par {
	int xres;
	int yres;
	int xres_vir;
	int yres_vir;
	int xoffset;
	int yoffset;
	int bpp;

	struct fb_bitfield red;
	struct fb_bitfield green;
	struct fb_bitfield blue;
	struct fb_bitfield transp;

	int pixclock;
	int left_margin;	/* time from sync to picture	*/
	int right_margin;	/* time from picture to sync	*/
	int upper_margin;	/* time from sync to picture	*/
	int lower_margin;
	int hsync_len;	/* length of horizontal sync	*/
	int vsync_len;	/* length of vertical sync	*/
	int vmode;

	int accel;
};

struct display_data {
	long h_total;		/* Horizontal Total */
	long h_sstart;		/* Horizontal Sync Start */
	long h_sstop;		/* Horizontal Sync Stop */
	long h_bstart;		/* Horizontal Blank Start */
	long h_bstop;		/* Horizontal Blank Stop */
	long h_dispend;		/* Horizontal Display End */
	long v_total;		/* Vertical Total */
	long v_sstart;		/* Vertical Sync Start */
	long v_sstop;		/* Vertical Sync Stop */
	long v_bstart;		/* Vertical Blank Start */
	long v_bstop;		/* Vertical Blank Stop */
	long v_dispend;		/* Horizontal Display End */
};

struct retz3_fb_info {
	struct fb_info info;
	unsigned char *base;
	unsigned char *fbmem;
	unsigned long fbsize;
	volatile unsigned char *regs;
	unsigned long physfbmem;
	unsigned long physregs;
	int current_par_valid; /* set to 0 by memset */
	int blitbusy;
	struct display disp;
	struct retz3fb_par current_par;
	unsigned char color_table [256][3];
};


static char fontname[40] __initdata = { 0 };

#define retz3info(info) ((struct retz3_fb_info *)(info))
#define fbinfo(info) ((struct fb_info *)(info))


/*
 *    Frame Buffer Name
 */

static char retz3fb_name[16] = "RetinaZ3";


/*
 * A small info on how to convert XFree86 timing values into fb
 * timings - by Frank Neumann:
 *
An XFree86 mode line consists of the following fields:
 "800x600"     50      800  856  976 1040    600  637  643  666
 < name >     DCF       HR  SH1  SH2  HFL     VR  SV1  SV2  VFL

The fields in the fb_var_screeninfo structure are:
        unsigned long pixclock;         * pixel clock in ps (pico seconds) *
        unsigned long left_margin;      * time from sync to picture    *
        unsigned long right_margin;     * time from picture to sync    *
        unsigned long upper_margin;     * time from sync to picture    *
        unsigned long lower_margin;
        unsigned long hsync_len;        * length of horizontal sync    *
        unsigned long vsync_len;        * length of vertical sync      *

1) Pixelclock:
   xfree: in MHz
   fb: In Picoseconds (ps)

   pixclock = 1000000 / DCF

2) horizontal timings:
   left_margin = HFL - SH2
   right_margin = SH1 - HR
   hsync_len = SH2 - SH1

3) vertical timings:
   upper_margin = VFL - SV2
   lower_margin = SV1 - VR
   vsync_len = SV2 - SV1

Good examples for VESA timings can be found in the XFree86 source tree,
under "programs/Xserver/hw/xfree86/doc/modeDB.txt".
*/

/*
 *    Predefined Video Modes
 */

static struct {
    const char *name;
    struct fb_var_screeninfo var;
} retz3fb_predefined[] __initdata = {
    /*
     * NB: it is very important to adjust the pixel-clock to the color-depth.
     */

    {
	"640x480", {		/* 640x480, 8 bpp */
	    640, 480, 640, 480, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCEL_NONE, 39722, 48, 16, 33, 10, 96, 2,
	    FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,FB_VMODE_NONINTERLACED
	}
    },
    /*
     ModeLine "800x600" 36 800 824 896 1024 600 601 603 625
	      < name > DCF HR  SH1 SH2  HFL VR  SV1 SV2 VFL
     */
    {
	"800x600", {		/* 800x600, 8 bpp */
	    800, 600, 800, 600, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 27778, 64, 24, 22, 1, 120, 2,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    },
    {
	"800x600-60", {		/* 800x600, 8 bpp */
	    800, 600, 800, 600, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 25000, 88, 40, 23, 1, 128, 4,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    },
    {
	"800x600-70", {		/* 800x600, 8 bpp */
	    800, 600, 800, 600, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 22272, 40, 24, 15, 9, 144, 12,
	    FB_SYNC_COMP_HIGH_ACT, FB_VMODE_NONINTERLACED
	}
    },
    /*
      ModeLine "1024x768i" 45 1024 1064 1224 1264 768 777 785 817 interlace
	       < name >   DCF HR  SH1  SH2  HFL  VR  SV1 SV2 VFL
     */
    {
	"1024x768i", {		/* 1024x768, 8 bpp, interlaced */
	    1024, 768, 1024, 768, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 22222, 40, 40, 32, 9, 160, 8,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_INTERLACED
	}
    },
    {
	"1024x768", {
	    1024, 768, 1024, 768, 0, 0, 8, 0, 
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0}, 
	    0, 0, -1, -1, FB_ACCEL_NONE, 12500, 92, 112, 31, 2, 204, 4,
	    FB_SYNC_HOR_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	 }
    },
    {
	"640x480-16", {		/* 640x480, 16 bpp */
	    640, 480, 640, 480, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, 0, 38461/2, 28, 32, 12, 10, 96, 2,
	    FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,FB_VMODE_NONINTERLACED
	}
    },
    {
	"640x480-24", {		/* 640x480, 24 bpp */
	    640, 480, 640, 480, 0, 0, 24, 0,
	    {8, 8, 8}, {8, 8, 8}, {8, 8, 8}, {0, 0, 0},
	    0, 0, -1, -1, 0, 38461/3, 28, 32, 12, 10, 96, 2,
	    FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,FB_VMODE_NONINTERLACED
	}
    },
};


#define NUM_TOTAL_MODES    ARRAY_SIZE(retz3fb_predefined)

static struct fb_var_screeninfo retz3fb_default;

static int z3fb_inverse = 0;
static int z3fb_mode __initdata = 0;


/*
 *    Interface used by the world
 */

int retz3fb_setup(char *options);

static int retz3fb_get_fix(struct fb_fix_screeninfo *fix, int con,
			   struct fb_info *info);
static int retz3fb_get_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info);
static int retz3fb_set_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info);
static int retz3fb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			    struct fb_info *info);
static int retz3fb_setcolreg(unsigned int regno, unsigned int red,
			     unsigned int green, unsigned int blue,
			     unsigned int transp, struct fb_info *info);
static int retz3fb_blank(int blank, struct fb_info *info);


/*
 *    Interface to the low level console driver
 */

int retz3fb_init(void);
static int z3fb_switch(int con, struct fb_info *info);
static int z3fb_updatevar(int con, struct fb_info *info);


/*
 *    Text console acceleration
 */

#ifdef FBCON_HAS_CFB8
static struct display_switch fbcon_retz3_8;
#endif


/*
 *    Accelerated Functions used by the low level console driver
 */

static void retz3_bitblt(struct display *p,
			 unsigned short curx, unsigned short cury, unsigned
			 short destx, unsigned short desty, unsigned short
			 width, unsigned short height, unsigned short cmd,
			 unsigned short mask);

/*
 *   Hardware Specific Routines
 */

static int retz3_encode_fix(struct fb_info *info,
			    struct fb_fix_screeninfo *fix,
			    struct retz3fb_par *par);
static int retz3_decode_var(struct fb_var_screeninfo *var,
			    struct retz3fb_par *par);
static int retz3_encode_var(struct fb_var_screeninfo *var,
			    struct retz3fb_par *par);
static int retz3_getcolreg(unsigned int regno, unsigned int *red,
			   unsigned int *green, unsigned int *blue,
			   unsigned int *transp, struct fb_info *info);

/*
 *    Internal routines
 */

static void retz3fb_get_par(struct fb_info *info, struct retz3fb_par *par);
static void retz3fb_set_par(struct fb_info *info, struct retz3fb_par *par);
static int do_fb_set_var(struct fb_info *info,
			 struct fb_var_screeninfo *var, int isactive);
static void retz3fb_set_disp(int con, struct fb_info *info);
static int get_video_mode(const char *name);


/* -------------------- Hardware specific routines ------------------------- */

static unsigned short find_fq(unsigned int freq)
{
	unsigned long f;
	long tmp;
	long prev = 0x7fffffff;
	long n2, n1 = 3;
	unsigned long m;
	unsigned short res = 0;

	if (freq <= 31250000)
		n2 = 3;
	else if (freq <= 62500000)
		n2 = 2;
	else if (freq <= 125000000)
		n2 = 1;
	else if (freq <= 250000000)
		n2 = 0;
	else
		return 0;


	do {
		f = freq >> (10 - n2);

		m = (f * n1) / (14318180/1024);

		if (m > 129)
			break;

		tmp =  (((m * 14318180) >> n2) / n1) - freq;
		if (tmp < 0)
			tmp = -tmp;

		if (tmp < prev) {
			prev = tmp;
			res = (((n2 << 5) | (n1-2)) << 8) | (m-2);
		}

	} while ( (++n1) <= 21);

	return res;
}


static int retz3_set_video(struct fb_info *info,
			   struct fb_var_screeninfo *var,
			   struct retz3fb_par *par)
{
	volatile unsigned char *regs = retz3info(info)->regs;
	unsigned int freq;

	int xres, hfront, hsync, hback;
	int yres, vfront, vsync, vback;
	unsigned char tmp;
	unsigned short best_freq;
	struct display_data data;

	short clocksel = 0; /* Apparantly this is always zero */

	int bpp = var->bits_per_pixel;

	/*
	 * XXX
	 */
	if (bpp == 24)
		return 0;

	if ((bpp != 8) && (bpp != 16) && (bpp != 24))
		return -EFAULT;

	par->xoffset = 0;
	par->yoffset = 0;

	xres   = var->xres * bpp / 4;
	hfront = var->right_margin * bpp / 4;
	hsync  = var->hsync_len * bpp / 4;
	hback  = var->left_margin * bpp / 4;

	if (var->vmode & FB_VMODE_DOUBLE)
	{
		yres = var->yres * 2;
		vfront = var->lower_margin * 2;
		vsync  = var->vsync_len * 2;
		vback  = var->upper_margin * 2;
	}
	else if (var->vmode & FB_VMODE_INTERLACED)
	{
		yres   = (var->yres + 1) / 2;
		vfront = (var->lower_margin + 1) / 2;
		vsync  = (var->vsync_len + 1) / 2;
		vback  = (var->upper_margin + 1) / 2;
	}
	else
	{
		yres   = var->yres; /* -1 ? */
		vfront = var->lower_margin;
		vsync  = var->vsync_len;
		vback  = var->upper_margin;
	}

	data.h_total	= (hback / 8) + (xres / 8)
			+ (hfront / 8) + (hsync / 8) - 1 /* + 1 */;
	data.h_dispend	= ((xres + bpp - 1)/ 8) - 1;
	data.h_bstart	= xres / 8 - 1 /* + 1 */;

	data.h_bstop	= data.h_total+1 + 2 + 1;
	data.h_sstart	= (xres / 8) + (hfront / 8) + 1;
	data.h_sstop	= (xres / 8) + (hfront / 8) + (hsync / 8) + 1;

	data.v_total	= yres + vfront + vsync + vback - 1;

	data.v_dispend	= yres - 1;
	data.v_bstart	= yres - 1;

	data.v_bstop	= data.v_total;
	data.v_sstart	= yres + vfront - 1 - 2;
	data.v_sstop	= yres + vfront + vsync - 1;

#if 0 /* testing */

	printk("HBS: %i\n", data.h_bstart);
	printk("HSS: %i\n", data.h_sstart);
	printk("HSE: %i\n", data.h_sstop);
	printk("HBE: %i\n", data.h_bstop);
	printk("HT: %i\n", data.h_total);

	printk("hsync: %i\n", hsync);
	printk("hfront: %i\n", hfront);
	printk("hback: %i\n", hback);

	printk("VBS: %i\n", data.v_bstart);
	printk("VSS: %i\n", data.v_sstart);
	printk("VSE: %i\n", data.v_sstop);
	printk("VBE: %i\n", data.v_bstop);
	printk("VT: %i\n", data.v_total);

	printk("vsync: %i\n", vsync);
	printk("vfront: %i\n", vfront);
	printk("vback: %i\n", vback);
#endif

	if (data.v_total >= 1024)
		printk(KERN_ERR "MAYDAY: v_total >= 1024; bailing out!\n");

	reg_w(regs, GREG_MISC_OUTPUT_W, 0xe3 | ((clocksel & 3) * 0x04));
	reg_w(regs, GREG_FEATURE_CONTROL_W, 0x00);

	seq_w(regs, SEQ_RESET, 0x00);
	seq_w(regs, SEQ_RESET, 0x03);	/* reset sequencer logic */

	/*
	 * CLOCKING_MODE bits:
	 * 2: This one is only set for certain text-modes, wonder if
	 *    it may be for EGA-lines? (it was referred to as CLKDIV2)
	 * (The CL drivers sets it to 0x21 with the comment:
	 *  FullBandwidth (video off) and 8/9 dot clock)
	 */
	seq_w(regs, SEQ_CLOCKING_MODE, 0x01 | 0x00 /* 0x08 */);

	seq_w(regs, SEQ_MAP_MASK, 0x0f);        /* enable writing to plane 0-3 */
	seq_w(regs, SEQ_CHAR_MAP_SELECT, 0x00); /* doesn't matter in gfx-mode */
	seq_w(regs, SEQ_MEMORY_MODE, 0x06); /* CL driver says 0x0e for 256 col mode*/
	seq_w(regs, SEQ_RESET, 0x01);
	seq_w(regs, SEQ_RESET, 0x03);

	seq_w(regs, SEQ_EXTENDED_ENABLE, 0x05);

	seq_w(regs, SEQ_CURSOR_CONTROL, 0x00);	/* disable cursor */
	seq_w(regs, SEQ_PRIM_HOST_OFF_HI, 0x00);
	seq_w(regs, SEQ_PRIM_HOST_OFF_HI, 0x00);
	seq_w(regs, SEQ_LINEAR_0, 0x4a);
	seq_w(regs, SEQ_LINEAR_1, 0x00);

	seq_w(regs, SEQ_SEC_HOST_OFF_HI, 0x00);
	seq_w(regs, SEQ_SEC_HOST_OFF_LO, 0x00);
	seq_w(regs, SEQ_EXTENDED_MEM_ENA, 0x3 | 0x4 | 0x10 | 0x40);

	/*
	 * The lower 4 bits (0-3) are used to set the font-width for
	 * text-mode - DON'T try to set this for gfx-mode.
	 */
	seq_w(regs, SEQ_EXT_CLOCK_MODE, 0x10);
	seq_w(regs, SEQ_EXT_VIDEO_ADDR, 0x03);

	/*
	 * Extended Pixel Control:
	 * bit 0:   text-mode=0, gfx-mode=1 (Graphics Byte ?)
	 * bit 1: (Packed/Nibble Pixel Format ?)
	 * bit 4-5: depth, 0=1-8bpp, 1=9-16bpp, 2=17-24bpp
	 */
	seq_w(regs, SEQ_EXT_PIXEL_CNTL, 0x01 | (((bpp / 8) - 1) << 4));

	seq_w(regs, SEQ_BUS_WIDTH_FEEDB, 0x04);
	seq_w(regs, SEQ_COLOR_EXP_WFG, 0x01);
	seq_w(regs, SEQ_COLOR_EXP_WBG, 0x00);
	seq_w(regs, SEQ_EXT_RW_CONTROL, 0x00);
	seq_w(regs, SEQ_MISC_FEATURE_SEL, (0x51 | (clocksel & 8)));
	seq_w(regs, SEQ_COLOR_KEY_CNTL, 0x40);
	seq_w(regs, SEQ_COLOR_KEY_MATCH0, 0x00);
	seq_w(regs, SEQ_COLOR_KEY_MATCH1, 0x00);
	seq_w(regs, SEQ_COLOR_KEY_MATCH2, 0x00);
	seq_w(regs, SEQ_CRC_CONTROL, 0x00);
	seq_w(regs, SEQ_PERF_SELECT, 0x10);
	seq_w(regs, SEQ_ACM_APERTURE_1, 0x00);
	seq_w(regs, SEQ_ACM_APERTURE_2, 0x30);
	seq_w(regs, SEQ_ACM_APERTURE_3, 0x00);
	seq_w(regs, SEQ_MEMORY_MAP_CNTL, 0x03);


	/* unlock register CRT0..CRT7 */
	crt_w(regs, CRT_END_VER_RETR, (data.v_sstop & 0x0f) | 0x20);

	/* Zuerst zu schreibende Werte nur per printk ausgeben */
	DEBUG printk("CRT_HOR_TOTAL: %ld\n", data.h_total);
	crt_w(regs, CRT_HOR_TOTAL, data.h_total & 0xff);

	DEBUG printk("CRT_HOR_DISP_ENA_END: %ld\n", data.h_dispend);
	crt_w(regs, CRT_HOR_DISP_ENA_END, (data.h_dispend) & 0xff);

	DEBUG printk("CRT_START_HOR_BLANK: %ld\n", data.h_bstart);
	crt_w(regs, CRT_START_HOR_BLANK, data.h_bstart & 0xff);

	DEBUG printk("CRT_END_HOR_BLANK: 128+%ld\n", data.h_bstop % 32);
	crt_w(regs, CRT_END_HOR_BLANK,  0x80 | (data.h_bstop & 0x1f));

	DEBUG printk("CRT_START_HOR_RETR: %ld\n", data.h_sstart);
	crt_w(regs, CRT_START_HOR_RETR, data.h_sstart & 0xff);

	tmp = (data.h_sstop & 0x1f);
	if (data.h_bstop & 0x20)
		tmp |= 0x80;
	DEBUG printk("CRT_END_HOR_RETR: %d\n", tmp);
	crt_w(regs, CRT_END_HOR_RETR, tmp);

	DEBUG printk("CRT_VER_TOTAL: %ld\n", data.v_total & 0xff);
	crt_w(regs, CRT_VER_TOTAL, (data.v_total & 0xff));

	tmp = 0x10;  /* LineCompare bit #9 */
	if (data.v_total & 256)
		tmp |= 0x01;
	if (data.v_dispend & 256)
		tmp |= 0x02;
	if (data.v_sstart & 256)
		tmp |= 0x04;
	if (data.v_bstart & 256)
		tmp |= 0x08;
	if (data.v_total & 512)
		tmp |= 0x20;
	if (data.v_dispend & 512)
		tmp |= 0x40;
	if (data.v_sstart & 512)
		tmp |= 0x80;
	DEBUG printk("CRT_OVERFLOW: %d\n", tmp);
	crt_w(regs, CRT_OVERFLOW, tmp);

	crt_w(regs, CRT_PRESET_ROW_SCAN, 0x00); /* not CL !!! */

	tmp = 0x40; /* LineCompare bit #8 */
	if (data.v_bstart & 512)
		tmp |= 0x20;
	if (var->vmode & FB_VMODE_DOUBLE)
		tmp |= 0x80;
 	DEBUG printk("CRT_MAX_SCAN_LINE: %d\n", tmp);
	crt_w(regs, CRT_MAX_SCAN_LINE, tmp);

	crt_w(regs, CRT_CURSOR_START, 0x00);
	crt_w(regs, CRT_CURSOR_END, 8 & 0x1f); /* font height */

	crt_w(regs, CRT_START_ADDR_HIGH, 0x00);
	crt_w(regs, CRT_START_ADDR_LOW, 0x00);

	crt_w(regs, CRT_CURSOR_LOC_HIGH, 0x00);
	crt_w(regs, CRT_CURSOR_LOC_LOW, 0x00);

 	DEBUG printk("CRT_START_VER_RETR: %ld\n", data.v_sstart & 0xff);
	crt_w(regs, CRT_START_VER_RETR, (data.v_sstart & 0xff));

#if 1
	/* 5 refresh cycles per scanline */
	DEBUG printk("CRT_END_VER_RETR: 64+32+%ld\n", data.v_sstop % 16);
	crt_w(regs, CRT_END_VER_RETR, ((data.v_sstop & 0x0f) | 0x40 | 0x20));
#else
	DEBUG printk("CRT_END_VER_RETR: 128+32+%ld\n", data.v_sstop % 16);
	crt_w(regs, CRT_END_VER_RETR, ((data.v_sstop & 0x0f) | 128 | 32));
#endif
	DEBUG printk("CRT_VER_DISP_ENA_END: %ld\n", data.v_dispend & 0xff);
	crt_w(regs, CRT_VER_DISP_ENA_END, (data.v_dispend & 0xff));

	DEBUG printk("CRT_START_VER_BLANK: %ld\n", data.v_bstart & 0xff);
	crt_w(regs, CRT_START_VER_BLANK, (data.v_bstart & 0xff));

	DEBUG printk("CRT_END_VER_BLANK: %ld\n", data.v_bstop & 0xff);
	crt_w(regs, CRT_END_VER_BLANK, (data.v_bstop & 0xff));

	DEBUG printk("CRT_MODE_CONTROL: 0xe3\n");
	crt_w(regs, CRT_MODE_CONTROL, 0xe3);

	DEBUG printk("CRT_LINE_COMPARE: 0xff\n");
	crt_w(regs, CRT_LINE_COMPARE, 0xff);

	tmp = (var->xres_virtual / 8) * (bpp / 8);
	crt_w(regs, CRT_OFFSET, tmp);

	crt_w(regs, CRT_UNDERLINE_LOC, 0x07); /* probably font-height - 1 */

	tmp = 0x20;			/* Enable extended end bits */
	if (data.h_total & 0x100)
		tmp |= 0x01;
	if ((data.h_dispend) & 0x100)
		tmp |= 0x02;
	if (data.h_bstart & 0x100)
		tmp |= 0x04;
	if (data.h_sstart & 0x100)
		tmp |= 0x08;
	if (var->vmode & FB_VMODE_INTERLACED)
		tmp |= 0x10;
 	DEBUG printk("CRT_EXT_HOR_TIMING1: %d\n", tmp);
	crt_w(regs, CRT_EXT_HOR_TIMING1, tmp);

	tmp = 0x00;
	if (((var->xres_virtual / 8) * (bpp / 8)) & 0x100)
		tmp |= 0x10;
	crt_w(regs, CRT_EXT_START_ADDR, tmp);

	tmp = 0x00;
	if (data.h_total & 0x200)
		tmp |= 0x01;
	if ((data.h_dispend) & 0x200)
		tmp |= 0x02;
	if (data.h_bstart & 0x200)
		tmp |= 0x04;
	if (data.h_sstart & 0x200)
		tmp |= 0x08;
	tmp |= ((data.h_bstop & 0xc0) >> 2);
	tmp |= ((data.h_sstop & 0x60) << 1);
	crt_w(regs, CRT_EXT_HOR_TIMING2, tmp);
 	DEBUG printk("CRT_EXT_HOR_TIMING2: %d\n", tmp);

	tmp = 0x10;			/* Line compare bit 10 */
	if (data.v_total & 0x400)
		tmp |= 0x01;
	if ((data.v_dispend) & 0x400)
		tmp |= 0x02;
	if (data.v_bstart & 0x400)
		tmp |= 0x04;
	if (data.v_sstart & 0x400)
		tmp |= 0x08;
	tmp |= ((data.v_bstop & 0x300) >> 3);
	if (data.v_sstop & 0x10)
		tmp |= 0x80;
	crt_w(regs, CRT_EXT_VER_TIMING, tmp);
 	DEBUG printk("CRT_EXT_VER_TIMING: %d\n", tmp);

	crt_w(regs, CRT_MONITOR_POWER, 0x00);

	/*
	 * Convert from ps to Hz.
	 */
	freq = 2000000000 / var->pixclock;
	freq = freq * 500;

	best_freq = find_fq(freq);
	pll_w(regs, 0x02, best_freq);
	best_freq = find_fq(61000000);
	pll_w(regs, 0x0a, best_freq);
	pll_w(regs, 0x0e, 0x22);

	gfx_w(regs, GFX_SET_RESET, 0x00);
	gfx_w(regs, GFX_ENABLE_SET_RESET, 0x00);
	gfx_w(regs, GFX_COLOR_COMPARE, 0x00);
	gfx_w(regs, GFX_DATA_ROTATE, 0x00);
	gfx_w(regs, GFX_READ_MAP_SELECT, 0x00);
	gfx_w(regs, GFX_GRAPHICS_MODE, 0x00);
	gfx_w(regs, GFX_MISC, 0x05);
	gfx_w(regs, GFX_COLOR_XCARE, 0x0f);
	gfx_w(regs, GFX_BITMASK, 0xff);

	reg_r(regs, ACT_ADDRESS_RESET);
	attr_w(regs, ACT_PALETTE0 , 0x00);
	attr_w(regs, ACT_PALETTE1 , 0x01);
	attr_w(regs, ACT_PALETTE2 , 0x02);
	attr_w(regs, ACT_PALETTE3 , 0x03);
	attr_w(regs, ACT_PALETTE4 , 0x04);
	attr_w(regs, ACT_PALETTE5 , 0x05);
	attr_w(regs, ACT_PALETTE6 , 0x06);
	attr_w(regs, ACT_PALETTE7 , 0x07);
	attr_w(regs, ACT_PALETTE8 , 0x08);
	attr_w(regs, ACT_PALETTE9 , 0x09);
	attr_w(regs, ACT_PALETTE10, 0x0a);
	attr_w(regs, ACT_PALETTE11, 0x0b);
	attr_w(regs, ACT_PALETTE12, 0x0c);
	attr_w(regs, ACT_PALETTE13, 0x0d);
	attr_w(regs, ACT_PALETTE14, 0x0e);
	attr_w(regs, ACT_PALETTE15, 0x0f);
	reg_r(regs, ACT_ADDRESS_RESET);

	attr_w(regs, ACT_ATTR_MODE_CNTL, 0x09); /* 0x01 for CL */

	attr_w(regs, ACT_OVERSCAN_COLOR, 0x00);
	attr_w(regs, ACT_COLOR_PLANE_ENA, 0x0f);
	attr_w(regs, ACT_HOR_PEL_PANNING, 0x00);
	attr_w(regs, ACT_COLOR_SELECT, 0x00);

	reg_r(regs, ACT_ADDRESS_RESET);
	reg_w(regs, ACT_DATA, 0x20);

	reg_w(regs, VDAC_MASK, 0xff);

	/*
	 * Extended palette addressing ???
	 */
	switch (bpp){
	case 8:
		reg_w(regs, 0x83c6, 0x00);
		break;
	case 16:
		reg_w(regs, 0x83c6, 0x60);
		break;
	case 24:
		reg_w(regs, 0x83c6, 0xe0);
		break;
	default:
		printk(KERN_INFO "Illegal color-depth: %i\n", bpp);
	}

	reg_w(regs, VDAC_ADDRESS, 0x00);

	seq_w(regs, SEQ_MAP_MASK, 0x0f );

	return 0;
}


/*
 *    This function should fill in the `fix' structure based on the
 *    values in the `par' structure.
 */

static int retz3_encode_fix(struct fb_info *info,
			    struct fb_fix_screeninfo *fix,
			    struct retz3fb_par *par)
{
	struct retz3_fb_info *zinfo = retz3info(info);

	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id, retz3fb_name);
	fix->smem_start = zinfo->physfbmem;
	fix->smem_len = zinfo->fbsize;
	fix->mmio_start = zinfo->physregs;
	fix->mmio_len = 0x00c00000;

	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->type_aux = 0;
	if (par->bpp == 8)
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
	else
		fix->visual = FB_VISUAL_TRUECOLOR;

	fix->xpanstep = 0;
	fix->ypanstep = 0;
	fix->ywrapstep = 0;
	fix->line_length = 0;

	fix->accel = FB_ACCEL_NCR_77C32BLT;

	return 0;
}


/*
 *    Get the video params out of `var'. If a value doesn't fit, round
 *    it up, if it's too big, return -EINVAL.
 */

static int retz3_decode_var(struct fb_var_screeninfo *var,
			    struct retz3fb_par *par)
{
	par->xres = var->xres;
	par->yres = var->yres;
	par->xres_vir = var->xres_virtual;
	par->yres_vir = var->yres_virtual;
	par->bpp = var->bits_per_pixel;
	par->pixclock = var->pixclock;
	par->vmode = var->vmode;

	par->red = var->red;
	par->green = var->green;
	par->blue = var->blue;
	par->transp = var->transp;

	par->left_margin = var->left_margin;
	par->right_margin = var->right_margin;
	par->upper_margin = var->upper_margin;
	par->lower_margin = var->lower_margin;
	par->hsync_len = var->hsync_len;
	par->vsync_len = var->vsync_len;

	if (var->accel_flags & FB_ACCELF_TEXT)
	    par->accel = FB_ACCELF_TEXT;
	else
	    par->accel = 0;

	return 0;
}


/*
 *    Fill the `var' structure based on the values in `par' and maybe
 *    other values read out of the hardware.
 */

static int retz3_encode_var(struct fb_var_screeninfo *var,
			    struct retz3fb_par *par)
{
	memset(var, 0, sizeof(struct fb_var_screeninfo));
	var->xres = par->xres;
	var->yres = par->yres;
	var->xres_virtual = par->xres_vir;
	var->yres_virtual = par->yres_vir;
	var->xoffset = 0;
	var->yoffset = 0;

	var->bits_per_pixel = par->bpp;
	var->grayscale = 0;

	var->red = par->red;
	var->green = par->green;
	var->blue = par->blue;
	var->transp = par->transp;

	var->nonstd = 0;
	var->activate = 0;

	var->height = -1;
	var->width = -1;

	var->accel_flags = (par->accel && par->bpp == 8) ? FB_ACCELF_TEXT : 0;

	var->pixclock = par->pixclock;

	var->sync = 0;				/* ??? */
	var->left_margin = par->left_margin;
	var->right_margin = par->right_margin;
	var->upper_margin = par->upper_margin;
	var->lower_margin = par->lower_margin;
	var->hsync_len = par->hsync_len;
	var->vsync_len = par->vsync_len;

	var->vmode = par->vmode;
	return 0;
}


/*
 *    Set a single color register. Return != 0 for invalid regno.
 */

static int retz3fb_setcolreg(unsigned int regno, unsigned int red,
			     unsigned int green, unsigned int blue,
			     unsigned int transp, struct fb_info *info)
{
	struct retz3_fb_info *zinfo = retz3info(info);
	volatile unsigned char *regs = zinfo->regs;

	/* We'll get to this */

	if (regno > 255)
		return 1;

	red >>= 10;
	green >>= 10;
	blue >>= 10;

	zinfo->color_table[regno][0] = red;
	zinfo->color_table[regno][1] = green;
	zinfo->color_table[regno][2] = blue;

	reg_w(regs, VDAC_ADDRESS_W, regno);
	reg_w(regs, VDAC_DATA, red);
	reg_w(regs, VDAC_DATA, green);
	reg_w(regs, VDAC_DATA, blue);

	return 0;
}


/*
 *    Read a single color register and split it into
 *    colors/transparent. Return != 0 for invalid regno.
 */

static int retz3_getcolreg(unsigned int regno, unsigned int *red,
			   unsigned int *green, unsigned int *blue,
			   unsigned int *transp, struct fb_info *info)
{
	struct retz3_fb_info *zinfo = retz3info(info);
	int t;

	if (regno > 255)
		return 1;
	t       = zinfo->color_table[regno][0];
	*red    = (t<<10) | (t<<4) | (t>>2);
	t       = zinfo->color_table[regno][1];
	*green  = (t<<10) | (t<<4) | (t>>2);
	t       = zinfo->color_table[regno][2];
	*blue   = (t<<10) | (t<<4) | (t>>2);
	*transp = 0;
	return 0;
}


static inline void retz3_busy(struct display *p)
{
	struct retz3_fb_info *zinfo = retz3info(p->fb_info);
	volatile unsigned char *acm = zinfo->base + ACM_OFFSET;
	unsigned char blt_status;

	if (zinfo->blitbusy) {
		do{
			blt_status = *((acm) + (ACM_START_STATUS + 2));
		}while ((blt_status & 1) == 0);
		zinfo->blitbusy = 0;
	}
}


static void retz3_bitblt (struct display *p,
			  unsigned short srcx, unsigned short srcy,
			  unsigned short destx, unsigned short desty,
			  unsigned short width, unsigned short height,
			  unsigned short cmd, unsigned short mask)
{
	struct fb_var_screeninfo *var = &p->var;
	struct retz3_fb_info *zinfo = retz3info(p->fb_info);
	volatile unsigned long *acm = (unsigned long *)(zinfo->base + ACM_OFFSET);
	unsigned long *pattern = (unsigned long *)(zinfo->fbmem + PAT_MEM_OFF);

	unsigned short mod;
	unsigned long tmp;
	unsigned long pat, src, dst;

	int i, xres_virtual = var->xres_virtual;
	short bpp = (var->bits_per_pixel & 0xff);

	if (bpp < 8)
		bpp = 8;

	tmp = mask | (mask << 16);

	retz3_busy(p);

	i = 0;
	do{
		*pattern++ = tmp;
	}while(i++ < bpp/4);

	tmp = cmd << 8;
	*(acm + ACM_RASTEROP_ROTATION/4) = tmp;

	mod = 0xc0c2;

	pat = 8 * PAT_MEM_OFF;
	dst = bpp * (destx + desty * xres_virtual);

	/*
	 * Source is not set for clear.
	 */
	if ((cmd != Z3BLTclear) && (cmd != Z3BLTset)) {
		src = bpp * (srcx + srcy * xres_virtual);

		if (destx > srcx) {
			mod &= ~0x8000;
			src += bpp * (width - 1);
			dst += bpp * (width - 1);
			pat += bpp * 2;
		}
		if (desty > srcy) {
			mod &= ~0x4000;
			src += bpp * (height - 1) * xres_virtual;
			dst += bpp * (height - 1) * xres_virtual;
			pat += bpp * 4;
		}

		*(acm + ACM_SOURCE/4) = cpu_to_le32(src);
	}

	*(acm + ACM_PATTERN/4) = cpu_to_le32(pat);

	*(acm + ACM_DESTINATION/4) = cpu_to_le32(dst);

	tmp = mod << 16;
	*(acm + ACM_CONTROL/4) = tmp;

	tmp  = width | (height << 16);

	*(acm + ACM_BITMAP_DIMENSION/4) = cpu_to_le32(tmp);

	*(((volatile unsigned char *)acm) + ACM_START_STATUS) = 0x00;
	*(((volatile unsigned char *)acm) + ACM_START_STATUS) = 0x01;
	zinfo->blitbusy = 1;
}

#if 0
/*
 * Move cursor to x, y
 */
static void retz3_MoveCursor (unsigned short x, unsigned short y)
{
	/* Guess we gotta deal with the cursor at some point */
}
#endif


/*
 *    Fill the hardware's `par' structure.
 */

static void retz3fb_get_par(struct fb_info *info, struct retz3fb_par *par)
{
	struct retz3_fb_info *zinfo = retz3info(info);

	if (zinfo->current_par_valid)
		*par = zinfo->current_par;
	else
		retz3_decode_var(&retz3fb_default, par);
}


static void retz3fb_set_par(struct fb_info *info, struct retz3fb_par *par)
{
	struct retz3_fb_info *zinfo = retz3info(info);

	zinfo->current_par = *par;
	zinfo->current_par_valid = 1;
}


static int do_fb_set_var(struct fb_info *info,
			 struct fb_var_screeninfo *var, int isactive)
{
	int err, activate;
	struct retz3fb_par par;
	struct retz3_fb_info *zinfo = retz3info(info);

	if ((err = retz3_decode_var(var, &par)))
		return err;
	activate = var->activate;

	/* XXX ... what to do about isactive ? */

	if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW && isactive)
		retz3fb_set_par(info, &par);
	retz3_encode_var(var, &par);
	var->activate = activate;

	retz3_set_video(info, var, &zinfo->current_par);

	return 0;
}

/*
 *    Get the Fixed Part of the Display
 */

static int retz3fb_get_fix(struct fb_fix_screeninfo *fix, int con,
			   struct fb_info *info)
{
	struct retz3fb_par par;
	int error = 0;

	if (con == -1)
		retz3fb_get_par(info, &par);
	else
		error = retz3_decode_var(&fb_display[con].var, &par);
	return(error ? error : retz3_encode_fix(info, fix, &par));
}


/*
 *    Get the User Defined Part of the Display
 */

static int retz3fb_get_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info)
{
	struct retz3fb_par par;
	int error = 0;

	if (con == -1) {
		retz3fb_get_par(info, &par);
		error = retz3_encode_var(var, &par);
	} else
		*var = fb_display[con].var;
	return error;
}


static void retz3fb_set_disp(int con, struct fb_info *info)
{
	struct fb_fix_screeninfo fix;
	struct display *display;
	struct retz3_fb_info *zinfo = retz3info(info);

	if (con >= 0)
		display = &fb_display[con];
	else
		display = &zinfo->disp;	/* used during initialization */

	retz3fb_get_fix(&fix, con, info);

	if (con == -1)
		con = 0;

	display->visual = fix.visual;
	display->type = fix.type;
	display->type_aux = fix.type_aux;
	display->ypanstep = fix.ypanstep;
	display->ywrapstep = fix.ywrapstep;
	display->can_soft_blank = 1;
	display->inverse = z3fb_inverse;

	/*
	 * This seems to be about 20% faster.
	 */
	display->scrollmode = SCROLL_YREDRAW;

	switch (display->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
	case 8:
		if (display->var.accel_flags & FB_ACCELF_TEXT) {
		    display->dispsw = &fbcon_retz3_8;
		    retz3_set_video(info, &display->var, &zinfo->current_par);
		} else
		    display->dispsw = &fbcon_cfb8;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 16:
		display->dispsw = &fbcon_cfb16;
		break;
#endif
	default:
		display->dispsw = &fbcon_dummy;
		break;
	}
}


/*
 *    Set the User Defined Part of the Display
 */

static int retz3fb_set_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info)
{
	int err, oldxres, oldyres, oldvxres, oldvyres, oldbpp, oldaccel;
	struct display *display;
	struct retz3_fb_info *zinfo = retz3info(info);

	if (con >= 0)
		display = &fb_display[con];
	else
		display = &zinfo->disp;	/* used during initialization */

	if ((err = do_fb_set_var(info, var, con == info->currcon)))
		return err;
	if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
		oldxres = display->var.xres;
		oldyres = display->var.yres;
		oldvxres = display->var.xres_virtual;
		oldvyres = display->var.yres_virtual;
		oldbpp = display->var.bits_per_pixel;
		oldaccel = display->var.accel_flags;
		display->var = *var;

		if (oldxres != var->xres || oldyres != var->yres ||
		    oldvxres != var->xres_virtual ||
		    oldvyres != var->yres_virtual ||
		    oldbpp != var->bits_per_pixel ||
		    oldaccel != var->accel_flags) {

			struct fb_fix_screeninfo fix;
			retz3fb_get_fix(&fix, con, info);

			display->visual = fix.visual;
			display->type = fix.type;
			display->type_aux = fix.type_aux;
			display->ypanstep = fix.ypanstep;
			display->ywrapstep = fix.ywrapstep;
			display->line_length = fix.line_length;
			display->can_soft_blank = 1;
			display->inverse = z3fb_inverse;
			switch (display->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
			case 8:
				if (var->accel_flags & FB_ACCELF_TEXT) {
					display->dispsw = &fbcon_retz3_8;
				} else
					display->dispsw = &fbcon_cfb8;
				break;
#endif
#ifdef FBCON_HAS_CFB16
			case 16:
				display->dispsw = &fbcon_cfb16;
				break;
#endif
			default:
				display->dispsw = &fbcon_dummy;
				break;
			}
			/*
			 * We still need to find a way to tell the X
			 * server that the video mem has been fiddled with
			 * so it redraws the entire screen when switching
			 * between X and a text console.
			 */
			retz3_set_video(info, var, &zinfo->current_par);

			if (info->changevar)
				(*info->changevar)(con);
		}

		if (oldbpp != var->bits_per_pixel) {
			if ((err = fb_alloc_cmap(&display->cmap, 0, 0)))
				return err;
			do_install_cmap(con, info);
		}
	}
	return 0;
}


/*
 *    Get the Colormap
 */

static int retz3fb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			    struct fb_info *info)
{
	if (con == info->currcon) /* current console? */
		return(fb_get_cmap(cmap, kspc, retz3_getcolreg, info));
	else if (fb_display[con].cmap.len) /* non default colormap? */
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap(fb_default_cmap(1<<fb_display[con].var.bits_per_pixel),
			     cmap, kspc ? 0 : 2);
	return 0;
}

/*
 *    Blank the display.
 */

static int retz3fb_blank(int blank, struct fb_info *info)
{
	struct retz3_fb_info *zinfo = retz3info(info);
	volatile unsigned char *regs = retz3info(info)->regs;
	short i;

	if (blank)
		for (i = 0; i < 256; i++){
			reg_w(regs, VDAC_ADDRESS_W, i);
			reg_w(regs, VDAC_DATA, 0);
			reg_w(regs, VDAC_DATA, 0);
			reg_w(regs, VDAC_DATA, 0);
		}
	else
		for (i = 0; i < 256; i++){
			reg_w(regs, VDAC_ADDRESS_W, i);
			reg_w(regs, VDAC_DATA, zinfo->color_table[i][0]);
			reg_w(regs, VDAC_DATA, zinfo->color_table[i][1]);
			reg_w(regs, VDAC_DATA, zinfo->color_table[i][2]);
		}
	return 0;
}

static struct fb_ops retz3fb_ops = {
	.owner =	THIS_MODULE,
	.fb_get_fix =	retz3fb_get_fix,
	.fb_get_var =	retz3fb_get_var,
	.fb_set_var =	retz3fb_set_var,
	.fb_get_cmap =	retz3fb_get_cmap,
	.fb_set_cmap =	gen_set_cmap,
	.fb_setcolreg =	retz3fb_setcolreg,
	.fb_blank =	retz3fb_blank,
};

int __init retz3fb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;
		if (!strcmp(this_opt, "inverse")) {
			z3fb_inverse = 1;
			fb_invert_cmaps();
		} else if (!strncmp(this_opt, "font:", 5)) {
			strlcpy(fontname, this_opt+5, sizeof(fontname));
		} else
			z3fb_mode = get_video_mode(this_opt);
	}
	return 0;
}


/*
 *    Initialization
 */

int __init retz3fb_init(void)
{
	unsigned long board_addr, board_size;
	struct zorro_dev *z = NULL;
	volatile unsigned char *regs;
	struct retz3fb_par par;
	struct retz3_fb_info *zinfo;
	struct fb_info *fb_info;
	short i;
	int res = -ENXIO;

	while ((z = zorro_find_device(ZORRO_PROD_MACROSYSTEMS_RETINA_Z3, z))) {
		board_addr = z->resource.start;
		board_size = z->resource.end-z->resource.start+1;
		if (!request_mem_region(board_addr, 0x0c00000,
			    		"ncr77c32blt")) {
			continue;
		if (!request_mem_region(board_addr+VIDEO_MEM_OFFSET,
			    		0x00400000, "RAM"))
			release_mem_region(board_addr, 0x00c00000);
			continue;
		}
		if (!(zinfo = kmalloc(sizeof(struct retz3_fb_info),
				      GFP_KERNEL)))
			return -ENOMEM;
		memset(zinfo, 0, sizeof(struct retz3_fb_info));

		zinfo->base = ioremap(board_addr, board_size);
		zinfo->regs = zinfo->base;
		zinfo->fbmem = zinfo->base + VIDEO_MEM_OFFSET;
		/* Get memory size - for now we asume it's a 4MB board */
		zinfo->fbsize = 0x00400000; /* 4 MB */
		zinfo->physregs = board_addr;
		zinfo->physfbmem = board_addr + VIDEO_MEM_OFFSET;

		fb_info = fbinfo(zinfo);

		for (i = 0; i < 256; i++){
			for (i = 0; i < 256; i++){
				zinfo->color_table[i][0] = i;
				zinfo->color_table[i][1] = i;
				zinfo->color_table[i][2] = i;
			}
		}

		regs = zinfo->regs;
		/* Disable hardware cursor */
		seq_w(regs, SEQ_CURSOR_Y_INDEX, 0x00);

		retz3fb_setcolreg (255, 56<<8, 100<<8, 160<<8, 0, fb_info);
		retz3fb_setcolreg (254, 0, 0, 0, 0, fb_info);

		strcpy(fb_info->modename, retz3fb_name);
		fb_info->changevar = NULL;
		fb_info->fbops = &retz3fb_ops;
		fb_info->screen_base = zinfo->fbmem;
		fb_info->disp = &zinfo->disp;
		fb_info->currcon = -1;
		fb_info->switch_con = &z3fb_switch;
		fb_info->updatevar = &z3fb_updatevar;
		fb_info->flags = FBINFO_FLAG_DEFAULT;
		strlcpy(fb_info->fontname, fontname, sizeof(fb_info->fontname));

		if (z3fb_mode == -1)
			retz3fb_default = retz3fb_predefined[0].var;

		retz3_decode_var(&retz3fb_default, &par);
		retz3_encode_var(&retz3fb_default, &par);

		do_fb_set_var(fb_info, &retz3fb_default, 0);
		retz3fb_get_var(&zinfo->disp.var, -1, fb_info);

		retz3fb_set_disp(-1, fb_info);

		do_install_cmap(0, fb_info);

		if (register_framebuffer(fb_info) < 0)
			return -EINVAL;

		printk(KERN_INFO "fb%d: %s frame buffer device, using %ldK of "
		       "video memory\n", fb_info->node,
		       fb_info->modename, zinfo->fbsize>>10);

		/* FIXME: This driver cannot be unloaded yet */
		res = 0;
	}
	return res;
}


static int z3fb_switch(int con, struct fb_info *info)
{
	/* Do we have to save the colormap? */
	if (fb_display[info->currcon].cmap.len)
		fb_get_cmap(&fb_display[info->currcon].cmap, 1,
			    retz3_getcolreg, info);

	do_fb_set_var(info, &fb_display[con].var, 1);
	info->currcon = con;
	/* Install new colormap */
	do_install_cmap(con, info);
	return 0;
}


/*
 *    Update the `var' structure (called by fbcon.c)
 *
 *    This call looks only at yoffset and the FB_VMODE_YWRAP flag in `var'.
 *    Since it's called by a kernel driver, no range checking is done.
 */

static int z3fb_updatevar(int con, struct fb_info *info)
{
	return 0;
}

/*
 *    Get a Video Mode
 */

static int __init get_video_mode(const char *name)
{
	short i;

	for (i = 0; i < NUM_TOTAL_MODES; i++)
		if (!strcmp(name, retz3fb_predefined[i].name)){
			retz3fb_default = retz3fb_predefined[i].var;
			return i;
		}
	return -1;
}


#ifdef MODULE
MODULE_LICENSE("GPL");

int init_module(void)
{
	return retz3fb_init();
}
#endif


/*
 *  Text console acceleration
 */

#ifdef FBCON_HAS_CFB8
static void retz3_8_bmove(struct display *p, int sy, int sx,
			  int dy, int dx, int height, int width)
{
	int fontwidth = fontwidth(p);

	sx *= fontwidth;
	dx *= fontwidth;
	width *= fontwidth;

	retz3_bitblt(p,
		     (unsigned short)sx,
		     (unsigned short)(sy*fontheight(p)),
		     (unsigned short)dx,
		     (unsigned short)(dy*fontheight(p)),
		     (unsigned short)width,
		     (unsigned short)(height*fontheight(p)),
		     Z3BLTcopy,
		     0xffff);
}

static void retz3_8_clear(struct vc_data *conp, struct display *p,
			  int sy, int sx, int height, int width)
{
	unsigned short col;
	int fontwidth = fontwidth(p);

	sx *= fontwidth;
	width *= fontwidth;

	col = attr_bgcol_ec(p, conp);
	col &= 0xff;
	col |= (col << 8);

	retz3_bitblt(p,
		     (unsigned short)sx,
		     (unsigned short)(sy*fontheight(p)),
		     (unsigned short)sx,
		     (unsigned short)(sy*fontheight(p)),
		     (unsigned short)width,
		     (unsigned short)(height*fontheight(p)),
		     Z3BLTset,
		     col);
}


static void retz3_putc(struct vc_data *conp, struct display *p, int c,
		       int yy, int xx)
{
	retz3_busy(p);
	fbcon_cfb8_putc(conp, p, c, yy, xx);
}


static void retz3_putcs(struct vc_data *conp, struct display *p,
			const unsigned short *s, int count,
			int yy, int xx)
{
	retz3_busy(p);
	fbcon_cfb8_putcs(conp, p, s, count, yy, xx);
}


static void retz3_revc(struct display *p, int xx, int yy)
{
	retz3_busy(p);
	fbcon_cfb8_revc(p, xx, yy);
}


static void retz3_clear_margins(struct vc_data* conp, struct display* p,
				int bottom_only)
{
	retz3_busy(p);
	fbcon_cfb8_clear_margins(conp, p, bottom_only);
}


static struct display_switch fbcon_retz3_8 = {
	.setup		= fbcon_cfb8_setup,
	.bmove		= retz3_8_bmove,
	.clear		= retz3_8_clear,
	.putc		= retz3_putc,
	.putcs		= retz3_putcs,
	.revc		= retz3_revc,
	.clear_margins	= retz3_clear_margins,
	.fontwidthmask	= FONTWIDTH(8)
};
#endif
