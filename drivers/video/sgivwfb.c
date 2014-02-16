/*
 *  linux/drivers/video/sgivwfb.c -- SGI DBE frame buffer device
 *
 *	Copyright (C) 1999 Silicon Graphics, Inc.
 *      Jeffrey Newquist, newquist@engr.sgi.som
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/mtrr.h>
#include <asm/visws/sgivw.h>

#define INCLUDE_TIMING_TABLE_DATA
#define DBE_REG_BASE par->regs
#include <video/sgivw.h>

struct sgivw_par {
	struct asregs *regs;
	u32 cmap_fifo;
	u_long timing_num;
};

#define FLATPANEL_SGI_1600SW	5

/*
 *  RAM we reserve for the frame buffer. This defines the maximum screen
 *  size
 *
 *  The default can be overridden if the driver is compiled as a module
 */

static int ypan = 0;
static int ywrap = 0;

static int flatpanel_id = -1;

static struct fb_fix_screeninfo sgivwfb_fix = {
	.id		= "SGI Vis WS FB",
	.type		= FB_TYPE_PACKED_PIXELS,
        .visual		= FB_VISUAL_PSEUDOCOLOR,
	.mmio_start	= DBE_REG_PHYS,
	.mmio_len	= DBE_REG_SIZE,
        .accel		= FB_ACCEL_NONE,
	.line_length	= 640,
};

static struct fb_var_screeninfo sgivwfb_var = {
	/* 640x480, 8 bpp */
	.xres		= 640,
	.yres		= 480,
	.xres_virtual	= 640,
	.yres_virtual	= 480,
	.bits_per_pixel	= 8,
	.red		= { 0, 8, 0 },
	.green		= { 0, 8, 0 },
	.blue		= { 0, 8, 0 },
	.height		= -1,
	.width		= -1,
	.pixclock	= 20000,
	.left_margin	= 64,
	.right_margin	= 64,
	.upper_margin	= 32,
	.lower_margin	= 32,
	.hsync_len	= 64,
	.vsync_len	= 2,
	.vmode		= FB_VMODE_NONINTERLACED
};

static struct fb_var_screeninfo sgivwfb_var1600sw = {
	/* 1600x1024, 8 bpp */
	.xres		= 1600,
	.yres		= 1024,
	.xres_virtual	= 1600,
	.yres_virtual	= 1024,
	.bits_per_pixel	= 8,
	.red		= { 0, 8, 0 },
	.green		= { 0, 8, 0 },
	.blue		= { 0, 8, 0 },
	.height		= -1,
	.width		= -1,
	.pixclock	= 9353,
	.left_margin	= 20,
	.right_margin	= 30,
	.upper_margin	= 37,
	.lower_margin	= 3,
	.hsync_len	= 20,
	.vsync_len	= 3,
	.vmode		= FB_VMODE_NONINTERLACED
};

/*
 *  Interface used by the world
 */
int sgivwfb_init(void);

static int sgivwfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info);
static int sgivwfb_set_par(struct fb_info *info);
static int sgivwfb_setcolreg(u_int regno, u_int red, u_int green,
			     u_int blue, u_int transp,
			     struct fb_info *info);
static int sgivwfb_mmap(struct fb_info *info,
			struct vm_area_struct *vma);

static struct fb_ops sgivwfb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= sgivwfb_check_var,
	.fb_set_par	= sgivwfb_set_par,
	.fb_setcolreg	= sgivwfb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_mmap	= sgivwfb_mmap,
};

/*
 *  Internal routines
 */
static unsigned long bytes_per_pixel(int bpp)
{
	switch (bpp) {
		case 8:
			return 1;
		case 16:
			return 2;
		case 32:
			return 4;
		default:
			printk(KERN_INFO "sgivwfb: unsupported bpp %d\n", bpp);
			return 0;
	}
}

static unsigned long get_line_length(int xres_virtual, int bpp)
{
	return (xres_virtual * bytes_per_pixel(bpp));
}

/*
 * Function:	dbe_TurnOffDma
 * Parameters:	(None)
 * Description:	This should turn off the monitor and dbe.  This is used
 *              when switching between the serial console and the graphics
 *              console.
 */

static void dbe_TurnOffDma(struct sgivw_par *par)
{
	unsigned int readVal;
	int i;

	// Check to see if things are already turned off:
	// 1) Check to see if dbe is not using the internal dotclock.
	// 2) Check to see if the xy counter in dbe is already off.

	DBE_GETREG(ctrlstat, readVal);
	if (GET_DBE_FIELD(CTRLSTAT, PCLKSEL, readVal) < 2)
		return;

	DBE_GETREG(vt_xy, readVal);
	if (GET_DBE_FIELD(VT_XY, VT_FREEZE, readVal) == 1)
		return;

	// Otherwise, turn off dbe

	DBE_GETREG(ovr_control, readVal);
	SET_DBE_FIELD(OVR_CONTROL, OVR_DMA_ENABLE, readVal, 0);
	DBE_SETREG(ovr_control, readVal);
	udelay(1000);
	DBE_GETREG(frm_control, readVal);
	SET_DBE_FIELD(FRM_CONTROL, FRM_DMA_ENABLE, readVal, 0);
	DBE_SETREG(frm_control, readVal);
	udelay(1000);
	DBE_GETREG(did_control, readVal);
	SET_DBE_FIELD(DID_CONTROL, DID_DMA_ENABLE, readVal, 0);
	DBE_SETREG(did_control, readVal);
	udelay(1000);

	// XXX HACK:
	//
	//    This was necessary for GBE--we had to wait through two
	//    vertical retrace periods before the pixel DMA was
	//    turned off for sure.  I've left this in for now, in
	//    case dbe needs it.

	for (i = 0; i < 10000; i++) {
		DBE_GETREG(frm_inhwctrl, readVal);
		if (GET_DBE_FIELD(FRM_INHWCTRL, FRM_DMA_ENABLE, readVal) ==
		    0)
			udelay(10);
		else {
			DBE_GETREG(ovr_inhwctrl, readVal);
			if (GET_DBE_FIELD
			    (OVR_INHWCTRL, OVR_DMA_ENABLE, readVal) == 0)
				udelay(10);
			else {
				DBE_GETREG(did_inhwctrl, readVal);
				if (GET_DBE_FIELD
				    (DID_INHWCTRL, DID_DMA_ENABLE,
				     readVal) == 0)
					udelay(10);
				else
					break;
			}
		}
	}
}

/*
 *  Set the User Defined Part of the Display. Again if par use it to get
 *  real video mode.
 */
static int sgivwfb_check_var(struct fb_var_screeninfo *var, 
			     struct fb_info *info)
{
	struct sgivw_par *par = (struct sgivw_par *)info->par;
	struct dbe_timing_info *timing;
	u_long line_length;
	u_long min_mode;
	int req_dot;
	int test_mode;

	/*
	 *  FB_VMODE_CONUPDATE and FB_VMODE_SMOOTH_XPAN are equal!
	 *  as FB_VMODE_SMOOTH_XPAN is only used internally
	 */

	if (var->vmode & FB_VMODE_CONUPDATE) {
		var->vmode |= FB_VMODE_YWRAP;
		var->xoffset = info->var.xoffset;
		var->yoffset = info->var.yoffset;
	}

	/* XXX FIXME - forcing var's */
	var->xoffset = 0;
	var->yoffset = 0;

	/* Limit bpp to 8, 16, and 32 */
	if (var->bits_per_pixel <= 8)
		var->bits_per_pixel = 8;
	else if (var->bits_per_pixel <= 16)
		var->bits_per_pixel = 16;
	else if (var->bits_per_pixel <= 32)
		var->bits_per_pixel = 32;
	else
		return -EINVAL;

	var->grayscale = 0;	/* No grayscale for now */

	/* determine valid resolution and timing */
	for (min_mode = 0; min_mode < ARRAY_SIZE(dbeVTimings); min_mode++) {
		if (dbeVTimings[min_mode].width >= var->xres &&
		    dbeVTimings[min_mode].height >= var->yres)
			break;
	}

	if (min_mode == ARRAY_SIZE(dbeVTimings))
		return -EINVAL;	/* Resolution to high */

	/* XXX FIXME - should try to pick best refresh rate */
	/* for now, pick closest dot-clock within 3MHz */
	req_dot = PICOS2KHZ(var->pixclock);
	printk(KERN_INFO "sgivwfb: requested pixclock=%d ps (%d KHz)\n",
	       var->pixclock, req_dot);
	test_mode = min_mode;
	while (dbeVTimings[min_mode].width == dbeVTimings[test_mode].width) {
		if (dbeVTimings[test_mode].cfreq + 3000 > req_dot)
			break;
		test_mode++;
	}
	if (dbeVTimings[min_mode].width != dbeVTimings[test_mode].width)
		test_mode--;
	min_mode = test_mode;
	timing = &dbeVTimings[min_mode];
	printk(KERN_INFO "sgivwfb: granted dot-clock=%d KHz\n", timing->cfreq);

	/* Adjust virtual resolution, if necessary */
	if (var->xres > var->xres_virtual || (!ywrap && !ypan))
		var->xres_virtual = var->xres;
	if (var->yres > var->yres_virtual || (!ywrap && !ypan))
		var->yres_virtual = var->yres;

	/*
	 *  Memory limit
	 */
	line_length = get_line_length(var->xres_virtual, var->bits_per_pixel);
	if (line_length * var->yres_virtual > sgivwfb_mem_size)
		return -ENOMEM;	/* Virtual resolution to high */

	info->fix.line_length = line_length;

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
	case 16:		/* RGBA 5551 */
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 6;
		var->green.length = 5;
		var->blue.offset = 1;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 32:		/* RGB 8888 */
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 16;
		var->blue.length = 8;
		var->transp.offset = 24;
		var->transp.length = 8;
		break;
	}
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;

	/* set video timing information */
	var->pixclock = KHZ2PICOS(timing->cfreq);
	var->left_margin = timing->htotal - timing->hsync_end;
	var->right_margin = timing->hsync_start - timing->width;
	var->upper_margin = timing->vtotal - timing->vsync_end;
	var->lower_margin = timing->vsync_start - timing->height;
	var->hsync_len = timing->hsync_end - timing->hsync_start;
	var->vsync_len = timing->vsync_end - timing->vsync_start;

	/* Ouch. This breaks the rules but timing_num is only important if you
	* change a video mode */
	par->timing_num = min_mode;

	printk(KERN_INFO "sgivwfb: new video mode xres=%d yres=%d bpp=%d\n",
		var->xres, var->yres, var->bits_per_pixel);
	printk(KERN_INFO "         vxres=%d vyres=%d\n", var->xres_virtual,
		var->yres_virtual);
	return 0;
}

/*
 *  Setup flatpanel related registers.
 */
static void sgivwfb_setup_flatpanel(struct sgivw_par *par, struct dbe_timing_info *currentTiming)
{
	int fp_wid, fp_hgt, fp_vbs, fp_vbe;
	u32 outputVal = 0;

	SET_DBE_FIELD(VT_FLAGS, HDRV_INVERT, outputVal, 
		(currentTiming->flags & FB_SYNC_HOR_HIGH_ACT) ? 0 : 1);
	SET_DBE_FIELD(VT_FLAGS, VDRV_INVERT, outputVal, 
		(currentTiming->flags & FB_SYNC_VERT_HIGH_ACT) ? 0 : 1);
	DBE_SETREG(vt_flags, outputVal);

	/* Turn on the flat panel */
	switch (flatpanel_id) {
		case FLATPANEL_SGI_1600SW:
			fp_wid = 1600;
			fp_hgt = 1024;
			fp_vbs = 0;
			fp_vbe = 1600;
			currentTiming->pll_m = 4;
			currentTiming->pll_n = 1;
			currentTiming->pll_p = 0;
			break;
		default:
      			fp_wid = fp_hgt = fp_vbs = fp_vbe = 0xfff;
  	}

	outputVal = 0;
	SET_DBE_FIELD(FP_DE, FP_DE_ON, outputVal, fp_vbs);
	SET_DBE_FIELD(FP_DE, FP_DE_OFF, outputVal, fp_vbe);
	DBE_SETREG(fp_de, outputVal);
	outputVal = 0;
	SET_DBE_FIELD(FP_HDRV, FP_HDRV_OFF, outputVal, fp_wid);
	DBE_SETREG(fp_hdrv, outputVal);
	outputVal = 0;
	SET_DBE_FIELD(FP_VDRV, FP_VDRV_ON, outputVal, 1);
	SET_DBE_FIELD(FP_VDRV, FP_VDRV_OFF, outputVal, fp_hgt + 1);
	DBE_SETREG(fp_vdrv, outputVal);
}

/*
 *  Set the hardware according to 'par'.
 */
static int sgivwfb_set_par(struct fb_info *info)
{
	struct sgivw_par *par = info->par;
	int i, j, htmp, temp;
	u32 readVal, outputVal;
	int wholeTilesX, maxPixelsPerTileX;
	int frmWrite1, frmWrite2, frmWrite3b;
	struct dbe_timing_info *currentTiming; /* Current Video Timing */
	int xpmax, ypmax;	// Monitor resolution
	int bytesPerPixel;	// Bytes per pixel

	currentTiming = &dbeVTimings[par->timing_num];
	bytesPerPixel = bytes_per_pixel(info->var.bits_per_pixel);
	xpmax = currentTiming->width;
	ypmax = currentTiming->height;

	/* dbe_InitGraphicsBase(); */
	/* Turn on dotclock PLL */
	DBE_SETREG(ctrlstat, 0x20000000);

	dbe_TurnOffDma(par);

	/* dbe_CalculateScreenParams(); */
	maxPixelsPerTileX = 512 / bytesPerPixel;
	wholeTilesX = xpmax / maxPixelsPerTileX;
	if (wholeTilesX * maxPixelsPerTileX < xpmax)
		wholeTilesX++;

	printk(KERN_DEBUG "sgivwfb: pixPerTile=%d wholeTilesX=%d\n",
	       maxPixelsPerTileX, wholeTilesX);

	/* dbe_InitGammaMap(); */
	udelay(10);

	for (i = 0; i < 256; i++) {
		DBE_ISETREG(gmap, i, (i << 24) | (i << 16) | (i << 8));
	}

	/* dbe_TurnOn(); */
	DBE_GETREG(vt_xy, readVal);
	if (GET_DBE_FIELD(VT_XY, VT_FREEZE, readVal) == 1) {
		DBE_SETREG(vt_xy, 0x00000000);
		udelay(1);
	} else
		dbe_TurnOffDma(par);

	/* dbe_Initdbe(); */
	for (i = 0; i < 256; i++) {
		for (j = 0; j < 100; j++) {
			DBE_GETREG(cm_fifo, readVal);
			if (readVal != 0x00000000)
				break;
			else
				udelay(10);
		}

		// DBE_ISETREG(cmap, i, 0x00000000);
		DBE_ISETREG(cmap, i, (i << 8) | (i << 16) | (i << 24));
	}

	/* dbe_InitFramebuffer(); */
	frmWrite1 = 0;
	SET_DBE_FIELD(FRM_SIZE_TILE, FRM_WIDTH_TILE, frmWrite1,
		      wholeTilesX);
	SET_DBE_FIELD(FRM_SIZE_TILE, FRM_RHS, frmWrite1, 0);

	switch (bytesPerPixel) {
	case 1:
		SET_DBE_FIELD(FRM_SIZE_TILE, FRM_DEPTH, frmWrite1,
			      DBE_FRM_DEPTH_8);
		break;
	case 2:
		SET_DBE_FIELD(FRM_SIZE_TILE, FRM_DEPTH, frmWrite1,
			      DBE_FRM_DEPTH_16);
		break;
	case 4:
		SET_DBE_FIELD(FRM_SIZE_TILE, FRM_DEPTH, frmWrite1,
			      DBE_FRM_DEPTH_32);
		break;
	}

	frmWrite2 = 0;
	SET_DBE_FIELD(FRM_SIZE_PIXEL, FB_HEIGHT_PIX, frmWrite2, ypmax);

	// Tell dbe about the framebuffer location and type
	// XXX What format is the FRM_TILE_PTR??  64K aligned address?
	frmWrite3b = 0;
	SET_DBE_FIELD(FRM_CONTROL, FRM_TILE_PTR, frmWrite3b,
		      sgivwfb_mem_phys >> 9);
	SET_DBE_FIELD(FRM_CONTROL, FRM_DMA_ENABLE, frmWrite3b, 1);
	SET_DBE_FIELD(FRM_CONTROL, FRM_LINEAR, frmWrite3b, 1);

	/* Initialize DIDs */

	outputVal = 0;
	switch (bytesPerPixel) {
	case 1:
		SET_DBE_FIELD(WID, TYP, outputVal, DBE_CMODE_I8);
		break;
	case 2:
		SET_DBE_FIELD(WID, TYP, outputVal, DBE_CMODE_RGBA5);
		break;
	case 4:
		SET_DBE_FIELD(WID, TYP, outputVal, DBE_CMODE_RGB8);
		break;
	}
	SET_DBE_FIELD(WID, BUF, outputVal, DBE_BMODE_BOTH);

	for (i = 0; i < 32; i++) {
		DBE_ISETREG(mode_regs, i, outputVal);
	}

	/* dbe_InitTiming(); */
	DBE_SETREG(vt_intr01, 0xffffffff);
	DBE_SETREG(vt_intr23, 0xffffffff);

	DBE_GETREG(dotclock, readVal);
	DBE_SETREG(dotclock, readVal & 0xffff);

	DBE_SETREG(vt_xymax, 0x00000000);
	outputVal = 0;
	SET_DBE_FIELD(VT_VSYNC, VT_VSYNC_ON, outputVal,
		      currentTiming->vsync_start);
	SET_DBE_FIELD(VT_VSYNC, VT_VSYNC_OFF, outputVal,
		      currentTiming->vsync_end);
	DBE_SETREG(vt_vsync, outputVal);
	outputVal = 0;
	SET_DBE_FIELD(VT_HSYNC, VT_HSYNC_ON, outputVal,
		      currentTiming->hsync_start);
	SET_DBE_FIELD(VT_HSYNC, VT_HSYNC_OFF, outputVal,
		      currentTiming->hsync_end);
	DBE_SETREG(vt_hsync, outputVal);
	outputVal = 0;
	SET_DBE_FIELD(VT_VBLANK, VT_VBLANK_ON, outputVal,
		      currentTiming->vblank_start);
	SET_DBE_FIELD(VT_VBLANK, VT_VBLANK_OFF, outputVal,
		      currentTiming->vblank_end);
	DBE_SETREG(vt_vblank, outputVal);
	outputVal = 0;
	SET_DBE_FIELD(VT_HBLANK, VT_HBLANK_ON, outputVal,
		      currentTiming->hblank_start);
	SET_DBE_FIELD(VT_HBLANK, VT_HBLANK_OFF, outputVal,
		      currentTiming->hblank_end - 3);
	DBE_SETREG(vt_hblank, outputVal);
	outputVal = 0;
	SET_DBE_FIELD(VT_VCMAP, VT_VCMAP_ON, outputVal,
		      currentTiming->vblank_start);
	SET_DBE_FIELD(VT_VCMAP, VT_VCMAP_OFF, outputVal,
		      currentTiming->vblank_end);
	DBE_SETREG(vt_vcmap, outputVal);
	outputVal = 0;
	SET_DBE_FIELD(VT_HCMAP, VT_HCMAP_ON, outputVal,
		      currentTiming->hblank_start);
	SET_DBE_FIELD(VT_HCMAP, VT_HCMAP_OFF, outputVal,
		      currentTiming->hblank_end - 3);
	DBE_SETREG(vt_hcmap, outputVal);

	if (flatpanel_id != -1)
		sgivwfb_setup_flatpanel(par, currentTiming);

	outputVal = 0;
	temp = currentTiming->vblank_start - currentTiming->vblank_end - 1;
	if (temp > 0)
		temp = -temp;

	SET_DBE_FIELD(DID_START_XY, DID_STARTY, outputVal, (u32) temp);
	if (currentTiming->hblank_end >= 20)
		SET_DBE_FIELD(DID_START_XY, DID_STARTX, outputVal,
			      currentTiming->hblank_end - 20);
	else
		SET_DBE_FIELD(DID_START_XY, DID_STARTX, outputVal,
			      currentTiming->htotal - (20 -
						       currentTiming->
						       hblank_end));
	DBE_SETREG(did_start_xy, outputVal);

	outputVal = 0;
	SET_DBE_FIELD(CRS_START_XY, CRS_STARTY, outputVal,
		      (u32) (temp + 1));
	if (currentTiming->hblank_end >= DBE_CRS_MAGIC)
		SET_DBE_FIELD(CRS_START_XY, CRS_STARTX, outputVal,
			      currentTiming->hblank_end - DBE_CRS_MAGIC);
	else
		SET_DBE_FIELD(CRS_START_XY, CRS_STARTX, outputVal,
			      currentTiming->htotal - (DBE_CRS_MAGIC -
						       currentTiming->
						       hblank_end));
	DBE_SETREG(crs_start_xy, outputVal);

	outputVal = 0;
	SET_DBE_FIELD(VC_START_XY, VC_STARTY, outputVal, (u32) temp);
	SET_DBE_FIELD(VC_START_XY, VC_STARTX, outputVal,
		      currentTiming->hblank_end - 4);
	DBE_SETREG(vc_start_xy, outputVal);

	DBE_SETREG(frm_size_tile, frmWrite1);
	DBE_SETREG(frm_size_pixel, frmWrite2);

	outputVal = 0;
	SET_DBE_FIELD(DOTCLK, M, outputVal, currentTiming->pll_m - 1);
	SET_DBE_FIELD(DOTCLK, N, outputVal, currentTiming->pll_n - 1);
	SET_DBE_FIELD(DOTCLK, P, outputVal, currentTiming->pll_p);
	SET_DBE_FIELD(DOTCLK, RUN, outputVal, 1);
	DBE_SETREG(dotclock, outputVal);

	udelay(11 * 1000);

	DBE_SETREG(vt_vpixen, 0xffffff);
	DBE_SETREG(vt_hpixen, 0xffffff);

	outputVal = 0;
	SET_DBE_FIELD(VT_XYMAX, VT_MAXX, outputVal, currentTiming->htotal);
	SET_DBE_FIELD(VT_XYMAX, VT_MAXY, outputVal, currentTiming->vtotal);
	DBE_SETREG(vt_xymax, outputVal);

	outputVal = frmWrite1;
	SET_DBE_FIELD(FRM_SIZE_TILE, FRM_FIFO_RESET, outputVal, 1);
	DBE_SETREG(frm_size_tile, outputVal);
	DBE_SETREG(frm_size_tile, frmWrite1);

	outputVal = 0;
	SET_DBE_FIELD(OVR_WIDTH_TILE, OVR_FIFO_RESET, outputVal, 1);
	DBE_SETREG(ovr_width_tile, outputVal);
	DBE_SETREG(ovr_width_tile, 0);

	DBE_SETREG(frm_control, frmWrite3b);
	DBE_SETREG(did_control, 0);

	// Wait for dbe to take frame settings
	for (i = 0; i < 100000; i++) {
		DBE_GETREG(frm_inhwctrl, readVal);
		if (GET_DBE_FIELD(FRM_INHWCTRL, FRM_DMA_ENABLE, readVal) !=
		    0)
			break;
		else
			udelay(1);
	}

	if (i == 100000)
		printk(KERN_INFO
		       "sgivwfb: timeout waiting for frame DMA enable.\n");

	outputVal = 0;
	htmp = currentTiming->hblank_end - 19;
	if (htmp < 0)
		htmp += currentTiming->htotal;	/* allow blank to wrap around */
	SET_DBE_FIELD(VT_HPIXEN, VT_HPIXEN_ON, outputVal, htmp);
	SET_DBE_FIELD(VT_HPIXEN, VT_HPIXEN_OFF, outputVal,
		      ((htmp + currentTiming->width -
			2) % currentTiming->htotal));
	DBE_SETREG(vt_hpixen, outputVal);

	outputVal = 0;
	SET_DBE_FIELD(VT_VPIXEN, VT_VPIXEN_OFF, outputVal,
		      currentTiming->vblank_start);
	SET_DBE_FIELD(VT_VPIXEN, VT_VPIXEN_ON, outputVal,
		      currentTiming->vblank_end);
	DBE_SETREG(vt_vpixen, outputVal);

	// Turn off mouse cursor
	par->regs->crs_ctl = 0;

	// XXX What's this section for??
	DBE_GETREG(ctrlstat, readVal);
	readVal &= 0x02000000;

	if (readVal != 0) {
		DBE_SETREG(ctrlstat, 0x30000000);
	}
	return 0;
}

/*
 *  Set a single color register. The values supplied are already
 *  rounded down to the hardware's capabilities (according to the
 *  entries in the var structure). Return != 0 for invalid regno.
 */

static int sgivwfb_setcolreg(u_int regno, u_int red, u_int green,
			     u_int blue, u_int transp,
			     struct fb_info *info)
{
	struct sgivw_par *par = (struct sgivw_par *) info->par;

	if (regno > 255)
		return 1;
	red >>= 8;
	green >>= 8;
	blue >>= 8;

	/* wait for the color map FIFO to have a free entry */
	while (par->cmap_fifo == 0)
		par->cmap_fifo = par->regs->cm_fifo;

	par->regs->cmap[regno] = (red << 24) | (green << 16) | (blue << 8);
	par->cmap_fifo--;	/* assume FIFO is filling up */
	return 0;
}

static int sgivwfb_mmap(struct fb_info *info,
			struct vm_area_struct *vma)
{
	int r;

	pgprot_val(vma->vm_page_prot) =
		pgprot_val(vma->vm_page_prot) | _PAGE_PCD;

	r = vm_iomap_memory(vma, sgivwfb_mem_phys, sgivwfb_mem_size);

	printk(KERN_DEBUG "sgivwfb: mmap framebuffer P(%lx)->V(%lx)\n",
		sgivwfb_mem_phys + (vma->vm_pgoff << PAGE_SHIFT), vma->vm_start);

	return r;
}

int __init sgivwfb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!strncmp(this_opt, "monitor:", 8)) {
			if (!strncmp(this_opt + 8, "crt", 3))
				flatpanel_id = -1;
			else if (!strncmp(this_opt + 8, "1600sw", 6))
				flatpanel_id = FLATPANEL_SGI_1600SW;
		}
	}
	return 0;
}

/*
 *  Initialisation
 */
static int sgivwfb_probe(struct platform_device *dev)
{
	struct sgivw_par *par;
	struct fb_info *info;
	char *monitor;

	info = framebuffer_alloc(sizeof(struct sgivw_par) + sizeof(u32) * 16, &dev->dev);
	if (!info)
		return -ENOMEM;
	par = info->par;

	if (!request_mem_region(DBE_REG_PHYS, DBE_REG_SIZE, "sgivwfb")) {
		printk(KERN_ERR "sgivwfb: couldn't reserve mmio region\n");
		framebuffer_release(info);
		return -EBUSY;
	}

	par->regs = (struct asregs *) ioremap_nocache(DBE_REG_PHYS, DBE_REG_SIZE);
	if (!par->regs) {
		printk(KERN_ERR "sgivwfb: couldn't ioremap registers\n");
		goto fail_ioremap_regs;
	}

	mtrr_add(sgivwfb_mem_phys, sgivwfb_mem_size, MTRR_TYPE_WRCOMB, 1);

	sgivwfb_fix.smem_start = sgivwfb_mem_phys;
	sgivwfb_fix.smem_len = sgivwfb_mem_size;
	sgivwfb_fix.ywrapstep = ywrap;
	sgivwfb_fix.ypanstep = ypan;

	info->fix = sgivwfb_fix;

	switch (flatpanel_id) {
		case FLATPANEL_SGI_1600SW:
			info->var = sgivwfb_var1600sw;
			monitor = "SGI 1600SW flatpanel";
			break;
		default:
			info->var = sgivwfb_var;
			monitor = "CRT";
	}

	printk(KERN_INFO "sgivwfb: %s monitor selected\n", monitor);

	info->fbops = &sgivwfb_ops;
	info->pseudo_palette = (void *) (par + 1);
	info->flags = FBINFO_DEFAULT;

	info->screen_base = ioremap_nocache((unsigned long) sgivwfb_mem_phys, sgivwfb_mem_size);
	if (!info->screen_base) {
		printk(KERN_ERR "sgivwfb: couldn't ioremap screen_base\n");
		goto fail_ioremap_fbmem;
	}

	if (fb_alloc_cmap(&info->cmap, 256, 0) < 0)
		goto fail_color_map;

	if (register_framebuffer(info) < 0) {
		printk(KERN_ERR "sgivwfb: couldn't register framebuffer\n");
		goto fail_register_framebuffer;
	}

	platform_set_drvdata(dev, info);

	fb_info(info, "SGI DBE frame buffer device, using %ldK of video memory at %#lx\n",
		sgivwfb_mem_size >> 10, sgivwfb_mem_phys);
	return 0;

fail_register_framebuffer:
	fb_dealloc_cmap(&info->cmap);
fail_color_map:
	iounmap((char *) info->screen_base);
fail_ioremap_fbmem:
	iounmap(par->regs);
fail_ioremap_regs:
	release_mem_region(DBE_REG_PHYS, DBE_REG_SIZE);
	framebuffer_release(info);
	return -ENXIO;
}

static int sgivwfb_remove(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);

	if (info) {
		struct sgivw_par *par = info->par;

		unregister_framebuffer(info);
		dbe_TurnOffDma(par);
		iounmap(par->regs);
		iounmap(info->screen_base);
		release_mem_region(DBE_REG_PHYS, DBE_REG_SIZE);
		fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}
	return 0;
}

static struct platform_driver sgivwfb_driver = {
	.probe	= sgivwfb_probe,
	.remove	= sgivwfb_remove,
	.driver	= {
		.name	= "sgivwfb",
	},
};

static struct platform_device *sgivwfb_device;

int __init sgivwfb_init(void)
{
	int ret;

#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("sgivwfb", &option))
		return -ENODEV;
	sgivwfb_setup(option);
#endif
	ret = platform_driver_register(&sgivwfb_driver);
	if (!ret) {
		sgivwfb_device = platform_device_alloc("sgivwfb", 0);
		if (sgivwfb_device) {
			ret = platform_device_add(sgivwfb_device);
		} else
			ret = -ENOMEM;
		if (ret) {
			platform_driver_unregister(&sgivwfb_driver);
			platform_device_put(sgivwfb_device);
		}
	}
	return ret;
}

module_init(sgivwfb_init);

#ifdef MODULE
MODULE_LICENSE("GPL");

static void __exit sgivwfb_exit(void)
{
	platform_device_unregister(sgivwfb_device);
	platform_driver_unregister(&sgivwfb_driver);
}

module_exit(sgivwfb_exit);

#endif				/* MODULE */
