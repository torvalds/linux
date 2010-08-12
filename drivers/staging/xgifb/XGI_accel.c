/*
 * XGI 300/630/730/540/315/550/650/740 frame buffer driver
 * for Linux kernels 2.4.x and 2.5.x
 *
 * 2D acceleration part
 *
 * Based on the X driver's XGI300_accel.c which is
 *     Copyright Xavier Ducoin <x.ducoin@lectra.com>
 *     Copyright 2002 by Thomas Winischhofer, Vienna, Austria
 * and XGI310_accel.c which is
 *     Copyright 2002 by Thomas Winischhofer, Vienna, Austria
 *
 * Author: Thomas Winischhofer <thomas@winischhofer.net>
 *			(see http://www.winischhofer.net/
 *			for more information and updates)
 */

//#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/selection.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vt_kern.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/agp_backend.h>

#include <linux/types.h>
#include <asm/io.h>

#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

#include "vgatypes.h"
#include "vb_struct.h"
#include "XGIfb.h"
#include "XGI_accel.h"

static const int XGIALUConv[] =
{
    0x00,       /* dest = 0;            0,      GXclear,        0 */
    0x88,       /* dest &= src;         DSa,    GXand,          0x1 */
    0x44,       /* dest = src & ~dest;  SDna,   GXandReverse,   0x2 */
    0xCC,       /* dest = src;          S,      GXcopy,         0x3 */
    0x22,       /* dest &= ~src;        DSna,   GXandInverted,  0x4 */
    0xAA,       /* dest = dest;         D,      GXnoop,         0x5 */
    0x66,       /* dest = ^src;         DSx,    GXxor,          0x6 */
    0xEE,       /* dest |= src;         DSo,    GXor,           0x7 */
    0x11,       /* dest = ~src & ~dest; DSon,   GXnor,          0x8 */
    0x99,       /* dest ^= ~src ;       DSxn,   GXequiv,        0x9 */
    0x55,       /* dest = ~dest;        Dn,     GXInvert,       0xA */
    0xDD,       /* dest = src|~dest ;   SDno,   GXorReverse,    0xB */
    0x33,       /* dest = ~src;         Sn,     GXcopyInverted, 0xC */
    0xBB,       /* dest |= ~src;        DSno,   GXorInverted,   0xD */
    0x77,       /* dest = ~src|~dest;   DSan,   GXnand,         0xE */
    0xFF,       /* dest = 0xFF;         1,      GXset,          0xF */
};
/* same ROP but with Pattern as Source */
static const int XGIPatALUConv[] =
{
    0x00,       /* dest = 0;            0,      GXclear,        0 */
    0xA0,       /* dest &= src;         DPa,    GXand,          0x1 */
    0x50,       /* dest = src & ~dest;  PDna,   GXandReverse,   0x2 */
    0xF0,       /* dest = src;          P,      GXcopy,         0x3 */
    0x0A,       /* dest &= ~src;        DPna,   GXandInverted,  0x4 */
    0xAA,       /* dest = dest;         D,      GXnoop,         0x5 */
    0x5A,       /* dest = ^src;         DPx,    GXxor,          0x6 */
    0xFA,       /* dest |= src;         DPo,    GXor,           0x7 */
    0x05,       /* dest = ~src & ~dest; DPon,   GXnor,          0x8 */
    0xA5,       /* dest ^= ~src ;       DPxn,   GXequiv,        0x9 */
    0x55,       /* dest = ~dest;        Dn,     GXInvert,       0xA */
    0xF5,       /* dest = src|~dest ;   PDno,   GXorReverse,    0xB */
    0x0F,       /* dest = ~src;         Pn,     GXcopyInverted, 0xC */
    0xAF,       /* dest |= ~src;        DPno,   GXorInverted,   0xD */
    0x5F,       /* dest = ~src|~dest;   DPan,   GXnand,         0xE */
    0xFF,       /* dest = 0xFF;         1,      GXset,          0xF */
};

static const unsigned char myrops[] = {
   	3, 10, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3
   };

/* 300 series */
static void
XGI310Sync(void)
{
	XGI310Idle
}

/* 310/325 series ------------------------------------------------ */

static void
XGI310SetupForScreenToScreenCopy(int xdir, int ydir, int rop,
                                unsigned int planemask, int trans_color)
{
	XGI310SetupDSTColorDepth(xgi_video_info.DstColor);
	XGI310SetupSRCPitch(xgi_video_info.video_linelength)
	XGI310SetupDSTRect(xgi_video_info.video_linelength, 0xFFF)
	if (trans_color != -1) {
		XGI310SetupROP(0x0A)
		XGI310SetupSRCTrans(trans_color)
		XGI310SetupCMDFlag(TRANSPARENT_BITBLT)
	} else {
	        XGI310SetupROP(XGIALUConv[rop])
		/* Set command - not needed, both 0 */
		/* XGISetupCMDFlag(BITBLT | SRCVIDEO) */
	}
	XGI310SetupCMDFlag(xgi_video_info.XGI310_AccelDepth)
	/* TW: The 310/325 series is smart enough to know the direction */
}

static void
XGI310SubsequentScreenToScreenCopy(int src_x, int src_y, int dst_x, int dst_y,
                                int width, int height)
{
	long srcbase, dstbase;
	int mymin, mymax;

	srcbase = dstbase = 0;
	mymin = min(src_y, dst_y);
	mymax = max(src_y, dst_y);

	/* Although the chip knows the direction to use
	 * if the source and destination areas overlap,
	 * that logic fails if we fiddle with the bitmap
	 * addresses. Therefore, we check if the source
	 * and destination blitting areas overlap and
	 * adapt the bitmap addresses synchronously
	 * if the coordinates exceed the valid range.
	 * The the areas do not overlap, we do our
	 * normal check.
	 */
	if((mymax - mymin) < height) {
	   if((src_y >= 2048) || (dst_y >= 2048)) {
	      srcbase = xgi_video_info.video_linelength * mymin;
	      dstbase = xgi_video_info.video_linelength * mymin;
	      src_y -= mymin;
	      dst_y -= mymin;
	   }
	} else {
	   if(src_y >= 2048) {
	      srcbase = xgi_video_info.video_linelength * src_y;
	      src_y = 0;
	   }
	   if(dst_y >= 2048) {
	      dstbase = xgi_video_info.video_linelength * dst_y;
	      dst_y = 0;
	   }
	}

	XGI310SetupSRCBase(srcbase);
	XGI310SetupDSTBase(dstbase);
	XGI310SetupRect(width, height)
	XGI310SetupSRCXY(src_x, src_y)
	XGI310SetupDSTXY(dst_x, dst_y)
	XGI310DoCMD
}

static void
XGI310SetupForSolidFill(int color, int rop, unsigned int planemask)
{
	XGI310SetupPATFG(color)
	XGI310SetupDSTRect(xgi_video_info.video_linelength, 0xFFF)
	XGI310SetupDSTColorDepth(xgi_video_info.DstColor);
	XGI310SetupROP(XGIPatALUConv[rop])
	XGI310SetupCMDFlag(PATFG | xgi_video_info.XGI310_AccelDepth)
}

static void
XGI310SubsequentSolidFillRect(int x, int y, int w, int h)
{
	long dstbase;

	dstbase = 0;
	if(y >= 2048) {
		dstbase = xgi_video_info.video_linelength * y;
		y = 0;
	}
	XGI310SetupDSTBase(dstbase)
	XGI310SetupDSTXY(x,y)
	XGI310SetupRect(w,h)
	XGI310SetupCMDFlag(BITBLT)
	XGI310DoCMD
}

/* --------------------------------------------------------------------- */

/* The exported routines */

int XGIfb_initaccel(void)
{
#ifdef XGIFB_USE_SPINLOCKS
    spin_lock_init(&xgi_video_info.lockaccel);
#endif
    return(0);
}

void XGIfb_syncaccel(void)
{

    XGI310Sync();

}

int fbcon_XGI_sync(struct fb_info *info)
{
    if(!XGIfb_accel) return 0;
    CRITFLAGS

    XGI310Sync();

   CRITEND
   return 0;
}

void fbcon_XGI_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
   int col=0;
   CRITFLAGS


   if(!rect->width || !rect->height)
   	return;

   if(!XGIfb_accel) {
	cfb_fillrect(info, rect);
	return;
   }

   switch(info->var.bits_per_pixel) {
		case 8: col = rect->color;
			break;
		case 16: col = ((u32 *)(info->pseudo_palette))[rect->color];
			 break;
		case 32: col = ((u32 *)(info->pseudo_palette))[rect->color];
			 break;
	}


	   CRITBEGIN
	   XGI310SetupForSolidFill(col, myrops[rect->rop], 0);
	   XGI310SubsequentSolidFillRect(rect->dx, rect->dy, rect->width, rect->height);
	   CRITEND
	   XGI310Sync();


}

void fbcon_XGI_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
   int xdir, ydir;
   CRITFLAGS


   if(!XGIfb_accel) {
   	cfb_copyarea(info, area);
	return;
   }

   if(!area->width || !area->height)
   	return;

   if(area->sx < area->dx) xdir = 0;
   else                    xdir = 1;
   if(area->sy < area->dy) ydir = 0;
   else                    ydir = 1;

      CRITBEGIN
      XGI310SetupForScreenToScreenCopy(xdir, ydir, 3, 0, -1);
      XGI310SubsequentScreenToScreenCopy(area->sx, area->sy, area->dx, area->dy, area->width, area->height);
      CRITEND
      XGI310Sync();

}



