/*
 *  SGI GBE frame buffer driver
 *
 *  Copyright (C) 1999 Silicon Graphics, Inc. - Jeffrey Newquist
 *  Copyright (C) 2002 Vivien Chappelier <vivien.chappelier@linux-mips.org>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>

#ifdef CONFIG_X86
#include <asm/mtrr.h>
#endif
#ifdef CONFIG_MIPS
#include <asm/addrspace.h>
#endif
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/tlbflush.h>

#include <video/gbe.h>

static struct sgi_gbe *gbe;

struct gbefb_par {
	struct fb_var_screeninfo var;
	struct gbe_timing_info timing;
	int valid;
};

#ifdef CONFIG_SGI_IP32
#define GBE_BASE	0x16000000 /* SGI O2 */
#endif

#ifdef CONFIG_X86_VISWS
#define GBE_BASE	0xd0000000 /* SGI Visual Workstation */
#endif

/* macro for fastest write-though access to the framebuffer */
#ifdef CONFIG_MIPS
#ifdef CONFIG_CPU_R10000
#define pgprot_fb(_prot) (((_prot) & (~_CACHE_MASK)) | _CACHE_UNCACHED_ACCELERATED)
#else
#define pgprot_fb(_prot) (((_prot) & (~_CACHE_MASK)) | _CACHE_CACHABLE_NO_WA)
#endif
#endif
#ifdef CONFIG_X86
#define pgprot_fb(_prot) ((_prot) | _PAGE_PCD)
#endif

/*
 *  RAM we reserve for the frame buffer. This defines the maximum screen
 *  size
 */
#if CONFIG_FB_GBE_MEM > 8
#error GBE Framebuffer cannot use more than 8MB of memory
#endif

#define TILE_SHIFT 16
#define TILE_SIZE (1 << TILE_SHIFT)
#define TILE_MASK (TILE_SIZE - 1)

static unsigned int gbe_mem_size = CONFIG_FB_GBE_MEM * 1024*1024;
static void *gbe_mem;
static dma_addr_t gbe_dma_addr;
static unsigned long gbe_mem_phys;

static struct {
	uint16_t *cpu;
	dma_addr_t dma;
} gbe_tiles;

static int gbe_revision;

static int ypan, ywrap;

static uint32_t pseudo_palette[16];
static uint32_t gbe_cmap[256];
static int gbe_turned_on; /* 0 turned off, 1 turned on */

static char *mode_option __initdata = NULL;

/* default CRT mode */
static struct fb_var_screeninfo default_var_CRT __initdata = {
	/* 640x480, 60 Hz, Non-Interlaced (25.175 MHz dotclock) */
	.xres		= 640,
	.yres		= 480,
	.xres_virtual	= 640,
	.yres_virtual	= 480,
	.xoffset	= 0,
	.yoffset	= 0,
	.bits_per_pixel	= 8,
	.grayscale	= 0,
	.red		= { 0, 8, 0 },
	.green		= { 0, 8, 0 },
	.blue		= { 0, 8, 0 },
	.transp		= { 0, 0, 0 },
	.nonstd		= 0,
	.activate	= 0,
	.height		= -1,
	.width		= -1,
	.accel_flags	= 0,
	.pixclock	= 39722,	/* picoseconds */
	.left_margin	= 48,
	.right_margin	= 16,
	.upper_margin	= 33,
	.lower_margin	= 10,
	.hsync_len	= 96,
	.vsync_len	= 2,
	.sync		= 0,
	.vmode		= FB_VMODE_NONINTERLACED,
};

/* default LCD mode */
static struct fb_var_screeninfo default_var_LCD __initdata = {
	/* 1600x1024, 8 bpp */
	.xres		= 1600,
	.yres		= 1024,
	.xres_virtual	= 1600,
	.yres_virtual	= 1024,
	.xoffset	= 0,
	.yoffset	= 0,
	.bits_per_pixel	= 8,
	.grayscale	= 0,
	.red		= { 0, 8, 0 },
	.green		= { 0, 8, 0 },
	.blue		= { 0, 8, 0 },
	.transp		= { 0, 0, 0 },
	.nonstd		= 0,
	.activate	= 0,
	.height		= -1,
	.width		= -1,
	.accel_flags	= 0,
	.pixclock	= 9353,
	.left_margin	= 20,
	.right_margin	= 30,
	.upper_margin	= 37,
	.lower_margin	= 3,
	.hsync_len	= 20,
	.vsync_len	= 3,
	.sync		= 0,
	.vmode		= FB_VMODE_NONINTERLACED
};

/* default modedb mode */
/* 640x480, 60 Hz, Non-Interlaced (25.172 MHz dotclock) */
static struct fb_videomode default_mode_CRT __initdata = {
	.refresh	= 60,
	.xres		= 640,
	.yres		= 480,
	.pixclock	= 39722,
	.left_margin	= 48,
	.right_margin	= 16,
	.upper_margin	= 33,
	.lower_margin	= 10,
	.hsync_len	= 96,
	.vsync_len	= 2,
	.sync		= 0,
	.vmode		= FB_VMODE_NONINTERLACED,
};
/* 1600x1024 SGI flatpanel 1600sw */
static struct fb_videomode default_mode_LCD __initdata = {
	/* 1600x1024, 8 bpp */
	.xres		= 1600,
	.yres		= 1024,
	.pixclock	= 9353,
	.left_margin	= 20,
	.right_margin	= 30,
	.upper_margin	= 37,
	.lower_margin	= 3,
	.hsync_len	= 20,
	.vsync_len	= 3,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static struct fb_videomode *default_mode __initdata = &default_mode_CRT;
static struct fb_var_screeninfo *default_var __initdata = &default_var_CRT;

static int flat_panel_enabled = 0;

static void gbe_reset(void)
{
	/* Turn on dotclock PLL */
	gbe->ctrlstat = 0x300aa000;
}


/*
 * Function:	gbe_turn_off
 * Parameters:	(None)
 * Description:	This should turn off the monitor and gbe.  This is used
 *              when switching between the serial console and the graphics
 *              console.
 */

static void gbe_turn_off(void)
{
	int i;
	unsigned int val, x, y, vpixen_off;

	gbe_turned_on = 0;

	/* check if pixel counter is on */
	val = gbe->vt_xy;
	if (GET_GBE_FIELD(VT_XY, FREEZE, val) == 1)
		return;

	/* turn off DMA */
	val = gbe->ovr_control;
	SET_GBE_FIELD(OVR_CONTROL, OVR_DMA_ENABLE, val, 0);
	gbe->ovr_control = val;
	udelay(1000);
	val = gbe->frm_control;
	SET_GBE_FIELD(FRM_CONTROL, FRM_DMA_ENABLE, val, 0);
	gbe->frm_control = val;
	udelay(1000);
	val = gbe->did_control;
	SET_GBE_FIELD(DID_CONTROL, DID_DMA_ENABLE, val, 0);
	gbe->did_control = val;
	udelay(1000);

	/* We have to wait through two vertical retrace periods before
	 * the pixel DMA is turned off for sure. */
	for (i = 0; i < 10000; i++) {
		val = gbe->frm_inhwctrl;
		if (GET_GBE_FIELD(FRM_INHWCTRL, FRM_DMA_ENABLE, val)) {
			udelay(10);
		} else {
			val = gbe->ovr_inhwctrl;
			if (GET_GBE_FIELD(OVR_INHWCTRL, OVR_DMA_ENABLE, val)) {
				udelay(10);
			} else {
				val = gbe->did_inhwctrl;
				if (GET_GBE_FIELD(DID_INHWCTRL, DID_DMA_ENABLE, val)) {
					udelay(10);
				} else
					break;
			}
		}
	}
	if (i == 10000)
		printk(KERN_ERR "gbefb: turn off DMA timed out\n");

	/* wait for vpixen_off */
	val = gbe->vt_vpixen;
	vpixen_off = GET_GBE_FIELD(VT_VPIXEN, VPIXEN_OFF, val);

	for (i = 0; i < 100000; i++) {
		val = gbe->vt_xy;
		x = GET_GBE_FIELD(VT_XY, X, val);
		y = GET_GBE_FIELD(VT_XY, Y, val);
		if (y < vpixen_off)
			break;
		udelay(1);
	}
	if (i == 100000)
		printk(KERN_ERR
		       "gbefb: wait for vpixen_off timed out\n");
	for (i = 0; i < 10000; i++) {
		val = gbe->vt_xy;
		x = GET_GBE_FIELD(VT_XY, X, val);
		y = GET_GBE_FIELD(VT_XY, Y, val);
		if (y > vpixen_off)
			break;
		udelay(1);
	}
	if (i == 10000)
		printk(KERN_ERR "gbefb: wait for vpixen_off timed out\n");

	/* turn off pixel counter */
	val = 0;
	SET_GBE_FIELD(VT_XY, FREEZE, val, 1);
	gbe->vt_xy = val;
	udelay(10000);
	for (i = 0; i < 10000; i++) {
		val = gbe->vt_xy;
		if (GET_GBE_FIELD(VT_XY, FREEZE, val) != 1)
			udelay(10);
		else
			break;
	}
	if (i == 10000)
		printk(KERN_ERR "gbefb: turn off pixel clock timed out\n");

	/* turn off dot clock */
	val = gbe->dotclock;
	SET_GBE_FIELD(DOTCLK, RUN, val, 0);
	gbe->dotclock = val;
	udelay(10000);
	for (i = 0; i < 10000; i++) {
		val = gbe->dotclock;
		if (GET_GBE_FIELD(DOTCLK, RUN, val))
			udelay(10);
		else
			break;
	}
	if (i == 10000)
		printk(KERN_ERR "gbefb: turn off dotclock timed out\n");

	/* reset the frame DMA FIFO */
	val = gbe->frm_size_tile;
	SET_GBE_FIELD(FRM_SIZE_TILE, FRM_FIFO_RESET, val, 1);
	gbe->frm_size_tile = val;
	SET_GBE_FIELD(FRM_SIZE_TILE, FRM_FIFO_RESET, val, 0);
	gbe->frm_size_tile = val;
}

static void gbe_turn_on(void)
{
	unsigned int val, i;

	/*
	 * Check if pixel counter is off, for unknown reason this
	 * code hangs Visual Workstations
	 */
	if (gbe_revision < 2) {
		val = gbe->vt_xy;
		if (GET_GBE_FIELD(VT_XY, FREEZE, val) == 0)
			return;
	}

	/* turn on dot clock */
	val = gbe->dotclock;
	SET_GBE_FIELD(DOTCLK, RUN, val, 1);
	gbe->dotclock = val;
	udelay(10000);
	for (i = 0; i < 10000; i++) {
		val = gbe->dotclock;
		if (GET_GBE_FIELD(DOTCLK, RUN, val) != 1)
			udelay(10);
		else
			break;
	}
	if (i == 10000)
		printk(KERN_ERR "gbefb: turn on dotclock timed out\n");

	/* turn on pixel counter */
	val = 0;
	SET_GBE_FIELD(VT_XY, FREEZE, val, 0);
	gbe->vt_xy = val;
	udelay(10000);
	for (i = 0; i < 10000; i++) {
		val = gbe->vt_xy;
		if (GET_GBE_FIELD(VT_XY, FREEZE, val))
			udelay(10);
		else
			break;
	}
	if (i == 10000)
		printk(KERN_ERR "gbefb: turn on pixel clock timed out\n");

	/* turn on DMA */
	val = gbe->frm_control;
	SET_GBE_FIELD(FRM_CONTROL, FRM_DMA_ENABLE, val, 1);
	gbe->frm_control = val;
	udelay(1000);
	for (i = 0; i < 10000; i++) {
		val = gbe->frm_inhwctrl;
		if (GET_GBE_FIELD(FRM_INHWCTRL, FRM_DMA_ENABLE, val) != 1)
			udelay(10);
		else
			break;
	}
	if (i == 10000)
		printk(KERN_ERR "gbefb: turn on DMA timed out\n");

	gbe_turned_on = 1;
}

static void gbe_loadcmap(void)
{
	int i, j;

	for (i = 0; i < 256; i++) {
		for (j = 0; j < 1000 && gbe->cm_fifo >= 63; j++)
			udelay(10);
		if (j == 1000)
			printk(KERN_ERR "gbefb: cmap FIFO timeout\n");

		gbe->cmap[i] = gbe_cmap[i];
	}
}

/*
 *  Blank the display.
 */
static int gbefb_blank(int blank, struct fb_info *info)
{
	/* 0 unblank, 1 blank, 2 no vsync, 3 no hsync, 4 off */
	switch (blank) {
	case FB_BLANK_UNBLANK:		/* unblank */
		gbe_turn_on();
		gbe_loadcmap();
		break;

	case FB_BLANK_NORMAL:		/* blank */
		gbe_turn_off();
		break;

	default:
		/* Nothing */
		break;
	}
	return 0;
}

/*
 *  Setup flatpanel related registers.
 */
static void gbefb_setup_flatpanel(struct gbe_timing_info *timing)
{
	int fp_wid, fp_hgt, fp_vbs, fp_vbe;
	u32 outputVal = 0;

	SET_GBE_FIELD(VT_FLAGS, HDRV_INVERT, outputVal,
		(timing->flags & FB_SYNC_HOR_HIGH_ACT) ? 0 : 1);
	SET_GBE_FIELD(VT_FLAGS, VDRV_INVERT, outputVal,
		(timing->flags & FB_SYNC_VERT_HIGH_ACT) ? 0 : 1);
	gbe->vt_flags = outputVal;

	/* Turn on the flat panel */
	fp_wid = 1600;
	fp_hgt = 1024;
	fp_vbs = 0;
	fp_vbe = 1600;
	timing->pll_m = 4;
	timing->pll_n = 1;
	timing->pll_p = 0;

	outputVal = 0;
	SET_GBE_FIELD(FP_DE, ON, outputVal, fp_vbs);
	SET_GBE_FIELD(FP_DE, OFF, outputVal, fp_vbe);
	gbe->fp_de = outputVal;
	outputVal = 0;
	SET_GBE_FIELD(FP_HDRV, OFF, outputVal, fp_wid);
	gbe->fp_hdrv = outputVal;
	outputVal = 0;
	SET_GBE_FIELD(FP_VDRV, ON, outputVal, 1);
	SET_GBE_FIELD(FP_VDRV, OFF, outputVal, fp_hgt + 1);
	gbe->fp_vdrv = outputVal;
}

struct gbe_pll_info {
	int clock_rate;
	int fvco_min;
	int fvco_max;
};

static struct gbe_pll_info gbe_pll_table[2] = {
	{ 20, 80, 220 },
	{ 27, 80, 220 },
};

static int compute_gbe_timing(struct fb_var_screeninfo *var,
			      struct gbe_timing_info *timing)
{
	int pll_m, pll_n, pll_p, error, best_m, best_n, best_p, best_error;
	int pixclock;
	struct gbe_pll_info *gbe_pll;

	if (gbe_revision < 2)
		gbe_pll = &gbe_pll_table[0];
	else
		gbe_pll = &gbe_pll_table[1];

	/* Determine valid resolution and timing
	 * GBE crystal runs at 20Mhz or 27Mhz
	 * pll_m, pll_n, pll_p define the following frequencies
	 * fvco = pll_m * 20Mhz / pll_n
	 * fout = fvco / (2**pll_p) */
	best_error = 1000000000;
	best_n = best_m = best_p = 0;
	for (pll_p = 0; pll_p < 4; pll_p++)
		for (pll_m = 1; pll_m < 256; pll_m++)
			for (pll_n = 1; pll_n < 64; pll_n++) {
				pixclock = (1000000 / gbe_pll->clock_rate) *
						(pll_n << pll_p) / pll_m;

				error = var->pixclock - pixclock;

				if (error < 0)
					error = -error;

				if (error < best_error &&
				    pll_m / pll_n >
				    gbe_pll->fvco_min / gbe_pll->clock_rate &&
 				    pll_m / pll_n <
				    gbe_pll->fvco_max / gbe_pll->clock_rate) {
					best_error = error;
					best_m = pll_m;
					best_n = pll_n;
					best_p = pll_p;
				}
			}

	if (!best_n || !best_m)
		return -EINVAL;	/* Resolution to high */

	pixclock = (1000000 / gbe_pll->clock_rate) *
		(best_n << best_p) / best_m;

	/* set video timing information */
	if (timing) {
		timing->width = var->xres;
		timing->height = var->yres;
		timing->pll_m = best_m;
		timing->pll_n = best_n;
		timing->pll_p = best_p;
		timing->cfreq = gbe_pll->clock_rate * 1000 * timing->pll_m /
			(timing->pll_n << timing->pll_p);
		timing->htotal = var->left_margin + var->xres +
				var->right_margin + var->hsync_len;
		timing->vtotal = var->upper_margin + var->yres +
				var->lower_margin + var->vsync_len;
		timing->fields_sec = 1000 * timing->cfreq / timing->htotal *
				1000 / timing->vtotal;
		timing->hblank_start = var->xres;
		timing->vblank_start = var->yres;
		timing->hblank_end = timing->htotal;
		timing->hsync_start = var->xres + var->right_margin + 1;
		timing->hsync_end = timing->hsync_start + var->hsync_len;
		timing->vblank_end = timing->vtotal;
		timing->vsync_start = var->yres + var->lower_margin + 1;
		timing->vsync_end = timing->vsync_start + var->vsync_len;
	}

	return pixclock;
}

static void gbe_set_timing_info(struct gbe_timing_info *timing)
{
	int temp;
	unsigned int val;

	/* setup dot clock PLL */
	val = 0;
	SET_GBE_FIELD(DOTCLK, M, val, timing->pll_m - 1);
	SET_GBE_FIELD(DOTCLK, N, val, timing->pll_n - 1);
	SET_GBE_FIELD(DOTCLK, P, val, timing->pll_p);
	SET_GBE_FIELD(DOTCLK, RUN, val, 0);	/* do not start yet */
	gbe->dotclock = val;
	udelay(10000);

	/* setup pixel counter */
	val = 0;
	SET_GBE_FIELD(VT_XYMAX, MAXX, val, timing->htotal);
	SET_GBE_FIELD(VT_XYMAX, MAXY, val, timing->vtotal);
	gbe->vt_xymax = val;

	/* setup video timing signals */
	val = 0;
	SET_GBE_FIELD(VT_VSYNC, VSYNC_ON, val, timing->vsync_start);
	SET_GBE_FIELD(VT_VSYNC, VSYNC_OFF, val, timing->vsync_end);
	gbe->vt_vsync = val;
	val = 0;
	SET_GBE_FIELD(VT_HSYNC, HSYNC_ON, val, timing->hsync_start);
	SET_GBE_FIELD(VT_HSYNC, HSYNC_OFF, val, timing->hsync_end);
	gbe->vt_hsync = val;
	val = 0;
	SET_GBE_FIELD(VT_VBLANK, VBLANK_ON, val, timing->vblank_start);
	SET_GBE_FIELD(VT_VBLANK, VBLANK_OFF, val, timing->vblank_end);
	gbe->vt_vblank = val;
	val = 0;
	SET_GBE_FIELD(VT_HBLANK, HBLANK_ON, val,
		      timing->hblank_start - 5);
	SET_GBE_FIELD(VT_HBLANK, HBLANK_OFF, val,
		      timing->hblank_end - 3);
	gbe->vt_hblank = val;

	/* setup internal timing signals */
	val = 0;
	SET_GBE_FIELD(VT_VCMAP, VCMAP_ON, val, timing->vblank_start);
	SET_GBE_FIELD(VT_VCMAP, VCMAP_OFF, val, timing->vblank_end);
	gbe->vt_vcmap = val;
	val = 0;
	SET_GBE_FIELD(VT_HCMAP, HCMAP_ON, val, timing->hblank_start);
	SET_GBE_FIELD(VT_HCMAP, HCMAP_OFF, val, timing->hblank_end);
	gbe->vt_hcmap = val;

	val = 0;
	temp = timing->vblank_start - timing->vblank_end - 1;
	if (temp > 0)
		temp = -temp;

	if (flat_panel_enabled)
		gbefb_setup_flatpanel(timing);

	SET_GBE_FIELD(DID_START_XY, DID_STARTY, val, (u32) temp);
	if (timing->hblank_end >= 20)
		SET_GBE_FIELD(DID_START_XY, DID_STARTX, val,
			      timing->hblank_end - 20);
	else
		SET_GBE_FIELD(DID_START_XY, DID_STARTX, val,
			      timing->htotal - (20 - timing->hblank_end));
	gbe->did_start_xy = val;

	val = 0;
	SET_GBE_FIELD(CRS_START_XY, CRS_STARTY, val, (u32) (temp + 1));
	if (timing->hblank_end >= GBE_CRS_MAGIC)
		SET_GBE_FIELD(CRS_START_XY, CRS_STARTX, val,
			      timing->hblank_end - GBE_CRS_MAGIC);
	else
		SET_GBE_FIELD(CRS_START_XY, CRS_STARTX, val,
			      timing->htotal - (GBE_CRS_MAGIC -
						timing->hblank_end));
	gbe->crs_start_xy = val;

	val = 0;
	SET_GBE_FIELD(VC_START_XY, VC_STARTY, val, (u32) temp);
	SET_GBE_FIELD(VC_START_XY, VC_STARTX, val, timing->hblank_end - 4);
	gbe->vc_start_xy = val;

	val = 0;
	temp = timing->hblank_end - GBE_PIXEN_MAGIC_ON;
	if (temp < 0)
		temp += timing->htotal;	/* allow blank to wrap around */

	SET_GBE_FIELD(VT_HPIXEN, HPIXEN_ON, val, temp);
	SET_GBE_FIELD(VT_HPIXEN, HPIXEN_OFF, val,
		      ((temp + timing->width -
			GBE_PIXEN_MAGIC_OFF) % timing->htotal));
	gbe->vt_hpixen = val;

	val = 0;
	SET_GBE_FIELD(VT_VPIXEN, VPIXEN_ON, val, timing->vblank_end);
	SET_GBE_FIELD(VT_VPIXEN, VPIXEN_OFF, val, timing->vblank_start);
	gbe->vt_vpixen = val;

	/* turn off sync on green */
	val = 0;
	SET_GBE_FIELD(VT_FLAGS, SYNC_LOW, val, 1);
	gbe->vt_flags = val;
}

/*
 *  Set the hardware according to 'par'.
 */

static int gbefb_set_par(struct fb_info *info)
{
	int i;
	unsigned int val;
	int wholeTilesX, partTilesX, maxPixelsPerTileX;
	int height_pix;
	int xpmax, ypmax;	/* Monitor resolution */
	int bytesPerPixel;	/* Bytes per pixel */
	struct gbefb_par *par = (struct gbefb_par *) info->par;

	compute_gbe_timing(&info->var, &par->timing);

	bytesPerPixel = info->var.bits_per_pixel / 8;
	info->fix.line_length = info->var.xres_virtual * bytesPerPixel;
	xpmax = par->timing.width;
	ypmax = par->timing.height;

	/* turn off GBE */
	gbe_turn_off();

	/* set timing info */
	gbe_set_timing_info(&par->timing);

	/* initialize DIDs */
	val = 0;
	switch (bytesPerPixel) {
	case 1:
		SET_GBE_FIELD(WID, TYP, val, GBE_CMODE_I8);
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		break;
	case 2:
		SET_GBE_FIELD(WID, TYP, val, GBE_CMODE_ARGB5);
		info->fix.visual = FB_VISUAL_TRUECOLOR;
		break;
	case 4:
		SET_GBE_FIELD(WID, TYP, val, GBE_CMODE_RGB8);
		info->fix.visual = FB_VISUAL_TRUECOLOR;
		break;
	}
	SET_GBE_FIELD(WID, BUF, val, GBE_BMODE_BOTH);

	for (i = 0; i < 32; i++)
		gbe->mode_regs[i] = val;

	/* Initialize interrupts */
	gbe->vt_intr01 = 0xffffffff;
	gbe->vt_intr23 = 0xffffffff;

	/* HACK:
	   The GBE hardware uses a tiled memory to screen mapping. Tiles are
	   blocks of 512x128, 256x128 or 128x128 pixels, respectively for 8bit,
	   16bit and 32 bit modes (64 kB). They cover the screen with partial
	   tiles on the right and/or bottom of the screen if needed.
	   For example in 640x480 8 bit mode the mapping is:

	   <-------- 640 ----->
	   <---- 512 ----><128|384 offscreen>
	   ^  ^
	   | 128    [tile 0]        [tile 1]
	   |  v
	   ^
	   4 128    [tile 2]        [tile 3]
	   8  v
	   0  ^
	   128    [tile 4]        [tile 5]
	   |  v
	   |  ^
	   v  96    [tile 6]        [tile 7]
	   32 offscreen

	   Tiles have the advantage that they can be allocated individually in
	   memory. However, this mapping is not linear at all, which is not
	   really convienient. In order to support linear addressing, the GBE
	   DMA hardware is fooled into thinking the screen is only one tile
	   large and but has a greater height, so that the DMA transfer covers
	   the same region.
	   Tiles are still allocated as independent chunks of 64KB of
	   continuous physical memory and remapped so that the kernel sees the
	   framebuffer as a continuous virtual memory. The GBE tile table is
	   set up so that each tile references one of these 64k blocks:

	   GBE -> tile list    framebuffer           TLB   <------------ CPU
	          [ tile 0 ] -> [ 64KB ]  <- [ 16x 4KB page entries ]     ^
	             ...           ...              ...       linear virtual FB
	          [ tile n ] -> [ 64KB ]  <- [ 16x 4KB page entries ]     v


	   The GBE hardware is then told that the buffer is 512*tweaked_height,
	   with tweaked_height = real_width*real_height/pixels_per_tile.
	   Thus the GBE hardware will scan the first tile, filing the first 64k
	   covered region of the screen, and then will proceed to the next
	   tile, until the whole screen is covered.

	   Here is what would happen at 640x480 8bit:

	   normal tiling               linear
	   ^   11111111111111112222    11111111111111111111  ^
	   128 11111111111111112222    11111111111111111111 102 lines
	       11111111111111112222    11111111111111111111  v
	   V   11111111111111112222    11111111222222222222
	       33333333333333334444    22222222222222222222
	       33333333333333334444    22222222222222222222
	       <      512     >        <  256 >               102*640+256 = 64k

	   NOTE: The only mode for which this is not working is 800x600 8bit,
	   as 800*600/512 = 937.5 which is not integer and thus causes
	   flickering.
	   I guess this is not so important as one can use 640x480 8bit or
	   800x600 16bit anyway.
	 */

	/* Tell gbe about the tiles table location */
	/* tile_ptr -> [ tile 1 ] -> FB mem */
	/*             [ tile 2 ] -> FB mem */
	/*               ...                */
	val = 0;
	SET_GBE_FIELD(FRM_CONTROL, FRM_TILE_PTR, val, gbe_tiles.dma >> 9);
	SET_GBE_FIELD(FRM_CONTROL, FRM_DMA_ENABLE, val, 0); /* do not start */
	SET_GBE_FIELD(FRM_CONTROL, FRM_LINEAR, val, 0);
	gbe->frm_control = val;

	maxPixelsPerTileX = 512 / bytesPerPixel;
	wholeTilesX = 1;
	partTilesX = 0;

	/* Initialize the framebuffer */
	val = 0;
	SET_GBE_FIELD(FRM_SIZE_TILE, FRM_WIDTH_TILE, val, wholeTilesX);
	SET_GBE_FIELD(FRM_SIZE_TILE, FRM_RHS, val, partTilesX);

	switch (bytesPerPixel) {
	case 1:
		SET_GBE_FIELD(FRM_SIZE_TILE, FRM_DEPTH, val,
			      GBE_FRM_DEPTH_8);
		break;
	case 2:
		SET_GBE_FIELD(FRM_SIZE_TILE, FRM_DEPTH, val,
			      GBE_FRM_DEPTH_16);
		break;
	case 4:
		SET_GBE_FIELD(FRM_SIZE_TILE, FRM_DEPTH, val,
			      GBE_FRM_DEPTH_32);
		break;
	}
	gbe->frm_size_tile = val;

	/* compute tweaked height */
	height_pix = xpmax * ypmax / maxPixelsPerTileX;

	val = 0;
	SET_GBE_FIELD(FRM_SIZE_PIXEL, FB_HEIGHT_PIX, val, height_pix);
	gbe->frm_size_pixel = val;

	/* turn off DID and overlay DMA */
	gbe->did_control = 0;
	gbe->ovr_width_tile = 0;

	/* Turn off mouse cursor */
	gbe->crs_ctl = 0;

	/* Turn on GBE */
	gbe_turn_on();

	/* Initialize the gamma map */
	udelay(10);
	for (i = 0; i < 256; i++)
		gbe->gmap[i] = (i << 24) | (i << 16) | (i << 8);

	/* Initialize the color map */
	for (i = 0; i < 256; i++)
		gbe_cmap[i] = (i << 8) | (i << 16) | (i << 24);

	gbe_loadcmap();

	return 0;
}

static void gbefb_encode_fix(struct fb_fix_screeninfo *fix,
			     struct fb_var_screeninfo *var)
{
	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id, "SGI GBE");
	fix->smem_start = (unsigned long) gbe_mem;
	fix->smem_len = gbe_mem_size;
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->type_aux = 0;
	fix->accel = FB_ACCEL_NONE;
	switch (var->bits_per_pixel) {
	case 8:
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
		break;
	default:
		fix->visual = FB_VISUAL_TRUECOLOR;
		break;
	}
	fix->ywrapstep = 0;
	fix->xpanstep = 0;
	fix->ypanstep = 0;
	fix->line_length = var->xres_virtual * var->bits_per_pixel / 8;
	fix->mmio_start = GBE_BASE;
	fix->mmio_len = sizeof(struct sgi_gbe);
}

/*
 *  Set a single color register. The values supplied are already
 *  rounded down to the hardware's capabilities (according to the
 *  entries in the var structure). Return != 0 for invalid regno.
 */

static int gbefb_setcolreg(unsigned regno, unsigned red, unsigned green,
			     unsigned blue, unsigned transp,
			     struct fb_info *info)
{
	int i;

	if (regno > 255)
		return 1;
	red >>= 8;
	green >>= 8;
	blue >>= 8;

	if (info->var.bits_per_pixel <= 8) {
		gbe_cmap[regno] = (red << 24) | (green << 16) | (blue << 8);
		if (gbe_turned_on) {
			/* wait for the color map FIFO to have a free entry */
			for (i = 0; i < 1000 && gbe->cm_fifo >= 63; i++)
				udelay(10);
			if (i == 1000) {
				printk(KERN_ERR "gbefb: cmap FIFO timeout\n");
				return 1;
			}
			gbe->cmap[regno] = gbe_cmap[regno];
		}
	} else if (regno < 16) {
		switch (info->var.bits_per_pixel) {
		case 15:
		case 16:
			red >>= 3;
			green >>= 3;
			blue >>= 3;
			pseudo_palette[regno] =
				(red << info->var.red.offset) |
				(green << info->var.green.offset) |
				(blue << info->var.blue.offset);
			break;
		case 32:
			pseudo_palette[regno] =
				(red << info->var.red.offset) |
				(green << info->var.green.offset) |
				(blue << info->var.blue.offset);
			break;
		}
	}

	return 0;
}

/*
 *  Check video mode validity, eventually modify var to best match.
 */
static int gbefb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	unsigned int line_length;
	struct gbe_timing_info timing;
	int ret;

	/* Limit bpp to 8, 16, and 32 */
	if (var->bits_per_pixel <= 8)
		var->bits_per_pixel = 8;
	else if (var->bits_per_pixel <= 16)
		var->bits_per_pixel = 16;
	else if (var->bits_per_pixel <= 32)
		var->bits_per_pixel = 32;
	else
		return -EINVAL;

	/* Check the mode can be mapped linearly with the tile table trick. */
	/* This requires width x height x bytes/pixel be a multiple of 512 */
	if ((var->xres * var->yres * var->bits_per_pixel) & 4095)
		return -EINVAL;

	var->grayscale = 0;	/* No grayscale for now */

	ret = compute_gbe_timing(var, &timing);
	var->pixclock = ret;
	if (ret < 0)
		return -EINVAL;

	/* Adjust virtual resolution, if necessary */
	if (var->xres > var->xres_virtual || (!ywrap && !ypan))
		var->xres_virtual = var->xres;
	if (var->yres > var->yres_virtual || (!ywrap && !ypan))
		var->yres_virtual = var->yres;

	if (var->vmode & FB_VMODE_CONUPDATE) {
		var->vmode |= FB_VMODE_YWRAP;
		var->xoffset = info->var.xoffset;
		var->yoffset = info->var.yoffset;
	}

	/* No grayscale for now */
	var->grayscale = 0;

	/* Memory limit */
	line_length = var->xres_virtual * var->bits_per_pixel / 8;
	if (line_length * var->yres_virtual > gbe_mem_size)
		return -ENOMEM;	/* Virtual resolution too high */

	switch (var->bits_per_pixel) {
	case 8:
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 16:		/* RGB 1555 */
		var->red.offset = 10;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 5;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 32:		/* RGB 8888 */
		var->red.offset = 24;
		var->red.length = 8;
		var->green.offset = 16;
		var->green.length = 8;
		var->blue.offset = 8;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 8;
		break;
	}
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;

	var->left_margin = timing.htotal - timing.hsync_end;
	var->right_margin = timing.hsync_start - timing.width;
	var->upper_margin = timing.vtotal - timing.vsync_end;
	var->lower_margin = timing.vsync_start - timing.height;
	var->hsync_len = timing.hsync_end - timing.hsync_start;
	var->vsync_len = timing.vsync_end - timing.vsync_start;

	return 0;
}

static int gbefb_mmap(struct fb_info *info,
			struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long addr;
	unsigned long phys_addr, phys_size;
	u16 *tile;

	/* check range */
	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;
	if (offset + size > gbe_mem_size)
		return -EINVAL;

	/* remap using the fastest write-through mode on architecture */
	/* try not polluting the cache when possible */
	pgprot_val(vma->vm_page_prot) =
		pgprot_fb(pgprot_val(vma->vm_page_prot));

	vma->vm_flags |= VM_IO | VM_RESERVED;

	/* look for the starting tile */
	tile = &gbe_tiles.cpu[offset >> TILE_SHIFT];
	addr = vma->vm_start;
	offset &= TILE_MASK;

	/* remap each tile separately */
	do {
		phys_addr = (((unsigned long) (*tile)) << TILE_SHIFT) + offset;
		if ((offset + size) < TILE_SIZE)
			phys_size = size;
		else
			phys_size = TILE_SIZE - offset;

		if (remap_pfn_range(vma, addr, phys_addr >> PAGE_SHIFT,
						phys_size, vma->vm_page_prot))
			return -EAGAIN;

		offset = 0;
		size -= phys_size;
		addr += phys_size;
		tile++;
	} while (size);

	return 0;
}

static struct fb_ops gbefb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= gbefb_check_var,
	.fb_set_par	= gbefb_set_par,
	.fb_setcolreg	= gbefb_setcolreg,
	.fb_mmap	= gbefb_mmap,
	.fb_blank	= gbefb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

/*
 * sysfs
 */

static ssize_t gbefb_show_memsize(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", gbe_mem_size);
}

static DEVICE_ATTR(size, S_IRUGO, gbefb_show_memsize, NULL);

static ssize_t gbefb_show_rev(struct device *device, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", gbe_revision);
}

static DEVICE_ATTR(revision, S_IRUGO, gbefb_show_rev, NULL);

static void __devexit gbefb_remove_sysfs(struct device *dev)
{
	device_remove_file(dev, &dev_attr_size);
	device_remove_file(dev, &dev_attr_revision);
}

static void gbefb_create_sysfs(struct device *dev)
{
	device_create_file(dev, &dev_attr_size);
	device_create_file(dev, &dev_attr_revision);
}

/*
 * Initialization
 */

static int __init gbefb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!strncmp(this_opt, "monitor:", 8)) {
			if (!strncmp(this_opt + 8, "crt", 3)) {
				flat_panel_enabled = 0;
				default_var = &default_var_CRT;
				default_mode = &default_mode_CRT;
			} else if (!strncmp(this_opt + 8, "1600sw", 6) ||
				   !strncmp(this_opt + 8, "lcd", 3)) {
				flat_panel_enabled = 1;
				default_var = &default_var_LCD;
				default_mode = &default_mode_LCD;
			}
		} else if (!strncmp(this_opt, "mem:", 4)) {
			gbe_mem_size = memparse(this_opt + 4, &this_opt);
			if (gbe_mem_size > CONFIG_FB_GBE_MEM * 1024 * 1024)
				gbe_mem_size = CONFIG_FB_GBE_MEM * 1024 * 1024;
			if (gbe_mem_size < TILE_SIZE)
				gbe_mem_size = TILE_SIZE;
		} else
			mode_option = this_opt;
	}
	return 0;
}

static int __init gbefb_probe(struct platform_device *p_dev)
{
	int i, ret = 0;
	struct fb_info *info;
	struct gbefb_par *par;
#ifndef MODULE
	char *options = NULL;
#endif

	info = framebuffer_alloc(sizeof(struct gbefb_par), &p_dev->dev);
	if (!info)
		return -ENOMEM;

#ifndef MODULE
	if (fb_get_options("gbefb", &options))
		return -ENODEV;
	gbefb_setup(options);
#endif

	if (!request_mem_region(GBE_BASE, sizeof(struct sgi_gbe), "GBE")) {
		printk(KERN_ERR "gbefb: couldn't reserve mmio region\n");
		ret = -EBUSY;
		goto out_release_framebuffer;
	}

	gbe = (struct sgi_gbe *) ioremap(GBE_BASE, sizeof(struct sgi_gbe));
	if (!gbe) {
		printk(KERN_ERR "gbefb: couldn't map mmio region\n");
		ret = -ENXIO;
		goto out_release_mem_region;
	}
	gbe_revision = gbe->ctrlstat & 15;

	gbe_tiles.cpu =
		dma_alloc_coherent(NULL, GBE_TLB_SIZE * sizeof(uint16_t),
				   &gbe_tiles.dma, GFP_KERNEL);
	if (!gbe_tiles.cpu) {
		printk(KERN_ERR "gbefb: couldn't allocate tiles table\n");
		ret = -ENOMEM;
		goto out_unmap;
	}

	if (gbe_mem_phys) {
		/* memory was allocated at boot time */
		gbe_mem = ioremap_nocache(gbe_mem_phys, gbe_mem_size);
		if (!gbe_mem) {
			printk(KERN_ERR "gbefb: couldn't map framebuffer\n");
			ret = -ENOMEM;
			goto out_tiles_free;
		}

		gbe_dma_addr = 0;
	} else {
		/* try to allocate memory with the classical allocator
		 * this has high chance to fail on low memory machines */
		gbe_mem = dma_alloc_coherent(NULL, gbe_mem_size, &gbe_dma_addr,
					     GFP_KERNEL);
		if (!gbe_mem) {
			printk(KERN_ERR "gbefb: couldn't allocate framebuffer memory\n");
			ret = -ENOMEM;
			goto out_tiles_free;
		}

		gbe_mem_phys = (unsigned long) gbe_dma_addr;
	}

#ifdef CONFIG_X86
	mtrr_add(gbe_mem_phys, gbe_mem_size, MTRR_TYPE_WRCOMB, 1);
#endif

	/* map framebuffer memory into tiles table */
	for (i = 0; i < (gbe_mem_size >> TILE_SHIFT); i++)
		gbe_tiles.cpu[i] = (gbe_mem_phys >> TILE_SHIFT) + i;

	info->fbops = &gbefb_ops;
	info->pseudo_palette = pseudo_palette;
	info->flags = FBINFO_DEFAULT;
	info->screen_base = gbe_mem;
	fb_alloc_cmap(&info->cmap, 256, 0);

	/* reset GBE */
	gbe_reset();

	par = info->par;
	/* turn on default video mode */
	if (fb_find_mode(&par->var, info, mode_option, NULL, 0,
			 default_mode, 8) == 0)
		par->var = *default_var;
	info->var = par->var;
	gbefb_check_var(&par->var, info);
	gbefb_encode_fix(&info->fix, &info->var);

	if (register_framebuffer(info) < 0) {
		printk(KERN_ERR "gbefb: couldn't register framebuffer\n");
		ret = -ENXIO;
		goto out_gbe_unmap;
	}

	platform_set_drvdata(p_dev, info);
	gbefb_create_sysfs(&p_dev->dev);

	printk(KERN_INFO "fb%d: %s rev %d @ 0x%08x using %dkB memory\n",
	       info->node, info->fix.id, gbe_revision, (unsigned) GBE_BASE,
	       gbe_mem_size >> 10);

	return 0;

out_gbe_unmap:
	if (gbe_dma_addr)
		dma_free_coherent(NULL, gbe_mem_size, gbe_mem, gbe_mem_phys);
	else
		iounmap(gbe_mem);
out_tiles_free:
	dma_free_coherent(NULL, GBE_TLB_SIZE * sizeof(uint16_t),
			  (void *)gbe_tiles.cpu, gbe_tiles.dma);
out_unmap:
	iounmap(gbe);
out_release_mem_region:
	release_mem_region(GBE_BASE, sizeof(struct sgi_gbe));
out_release_framebuffer:
	framebuffer_release(info);

	return ret;
}

static int __devexit gbefb_remove(struct platform_device* p_dev)
{
	struct fb_info *info = platform_get_drvdata(p_dev);

	unregister_framebuffer(info);
	gbe_turn_off();
	if (gbe_dma_addr)
		dma_free_coherent(NULL, gbe_mem_size, gbe_mem, gbe_mem_phys);
	else
		iounmap(gbe_mem);
	dma_free_coherent(NULL, GBE_TLB_SIZE * sizeof(uint16_t),
			  (void *)gbe_tiles.cpu, gbe_tiles.dma);
	release_mem_region(GBE_BASE, sizeof(struct sgi_gbe));
	iounmap(gbe);
	gbefb_remove_sysfs(&p_dev->dev);
	framebuffer_release(info);

	return 0;
}

static struct platform_driver gbefb_driver = {
	.probe = gbefb_probe,
	.remove = __devexit_p(gbefb_remove),
	.driver	= {
		.name = "gbefb",
	},
};

static struct platform_device *gbefb_device;

static int __init gbefb_init(void)
{
	int ret = platform_driver_register(&gbefb_driver);
	if (!ret) {
		gbefb_device = platform_device_alloc("gbefb", 0);
		if (gbefb_device) {
			ret = platform_device_add(gbefb_device);
		} else {
			ret = -ENOMEM;
		}
		if (ret) {
			platform_device_put(gbefb_device);
			platform_driver_unregister(&gbefb_driver);
		}
	}
	return ret;
}

static void __exit gbefb_exit(void)
{
	platform_device_unregister(gbefb_device);
	platform_driver_unregister(&gbefb_driver);
}

module_init(gbefb_init);
module_exit(gbefb_exit);

MODULE_LICENSE("GPL");
