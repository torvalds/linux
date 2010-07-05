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
/*
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#include <linux/XGIfb.h>
#else
#include <video/XGIfb.h>
#endif
*/
#include <asm/io.h>

#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>
#endif

#include "osdef.h"
#include "vgatypes.h"
#include "vb_struct.h"
#include "XGIfb.h"
#include "XGI_accel.h"


extern struct     video_info xgi_video_info;
extern int XGIfb_accel;

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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,34)
static const unsigned char myrops[] = {
   	3, 10, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3
   };
#endif

/* 300 series */
#if 0
static void
XGI300Sync(void)
{
	XGI300Idle
}
#endif
static void
XGI310Sync(void)
{
	XGI310Idle
}
#if 0
static void
XGI300SetupForScreenToScreenCopy(int xdir, int ydir, int rop,
                                unsigned int planemask, int trans_color)
{
	XGI300SetupDSTColorDepth(xgi_video_info.DstColor);
	XGI300SetupSRCPitch(xgi_video_info.video_linelength)
	XGI300SetupDSTRect(xgi_video_info.video_linelength, 0xFFF)

	if(trans_color != -1) {
		XGI300SetupROP(0x0A)
		XGI300SetupSRCTrans(trans_color)
		XGI300SetupCMDFlag(TRANSPARENT_BITBLT)
	} else {
	        XGI300SetupROP(XGIALUConv[rop])
	}
	if(xdir > 0) {
		XGI300SetupCMDFlag(X_INC)
	}
	if(ydir > 0) {
		XGI300SetupCMDFlag(Y_INC)
	}
}

static void
XGI300SubsequentScreenToScreenCopy(int src_x, int src_y, int dst_x, int dst_y,
                                int width, int height)
{
	long srcbase, dstbase;

	srcbase = dstbase = 0;
	if (src_y >= 2048) {
		srcbase = xgi_video_info.video_linelength * src_y;
		src_y = 0;
	}
	if (dst_y >= 2048) {
		dstbase = xgi_video_info.video_linelength * dst_y;
		dst_y = 0;
	}

	XGI300SetupSRCBase(srcbase);
	XGI300SetupDSTBase(dstbase);

	if(!(xgi_video_info.CommandReg & X_INC))  {
		src_x += width-1;
		dst_x += width-1;
	}
	if(!(xgi_video_info.CommandReg & Y_INC))  {
		src_y += height-1;
		dst_y += height-1;
	}
	XGI300SetupRect(width, height)
	XGI300SetupSRCXY(src_x, src_y)
	XGI300SetupDSTXY(dst_x, dst_y)
	XGI300DoCMD
}

static void
XGI300SetupForSolidFill(int color, int rop, unsigned int planemask)
{
	XGI300SetupPATFG(color)
	XGI300SetupDSTRect(xgi_video_info.video_linelength, 0xFFF)
	XGI300SetupDSTColorDepth(xgi_video_info.DstColor);
	XGI300SetupROP(XGIPatALUConv[rop])
	XGI300SetupCMDFlag(PATFG)
}

static void
XGI300SubsequentSolidFillRect(int x, int y, int w, int h)
{
	long dstbase;

	dstbase = 0;
	if(y >= 2048) {
		dstbase = xgi_video_info.video_linelength * y;
		y = 0;
	}
	XGI300SetupDSTBase(dstbase)
	XGI300SetupDSTXY(x,y)
	XGI300SetupRect(w,h)
	XGI300SetupCMDFlag(X_INC | Y_INC | BITBLT)
	XGI300DoCMD
}
#endif
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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,34)  /* --- KERNEL 2.5.34 and later --- */

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

#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,33)  /* ------ KERNEL <2.5.34 ------ */

void fbcon_XGI_bmove(struct display *p, int srcy, int srcx,
			    int dsty, int dstx, int height, int width)
{
        int xdir, ydir;
	CRITFLAGS

	if(!xgi_video_info.accel) {
	    switch(xgi_video_info.video_bpp) {
	    case 8:
#ifdef FBCON_HAS_CFB8
	       fbcon_cfb8_bmove(p, srcy, srcx, dsty, dstx, height, width);
#endif
	       break;
	    case 16:
#ifdef FBCON_HAS_CFB16
	       fbcon_cfb16_bmove(p, srcy, srcx, dsty, dstx, height, width);
#endif
	       break;
	    case 32:
#ifdef FBCON_HAS_CFB32
	       fbcon_cfb32_bmove(p, srcy, srcx, dsty, dstx, height, width);
#endif
	       break;
            }
	    return;
	}

	srcx *= fontwidth(p);
	srcy *= fontheight(p);
	dstx *= fontwidth(p);
	dsty *= fontheight(p);
	width *= fontwidth(p);
	height *= fontheight(p);

	if(srcx < dstx) xdir = 0;
	else            xdir = 1;
	if(srcy < dsty) ydir = 0;
	else            ydir = 1;


	   CRITBEGIN
	   XGI310SetupForScreenToScreenCopy(xdir, ydir, 3, 0, -1);
	   XGI310SubsequentScreenToScreenCopy(srcx, srcy, dstx, dsty, width, height);
	   CRITEND
	   XGI310Sync();
#if 0
	   printk(KERN_INFO "XGI_bmove sx %d sy %d dx %d dy %d w %d h %d\n",
		srcx, srcy, dstx, dsty, width, height);
#endif

}


static void fbcon_XGI_clear(struct vc_data *conp, struct display *p,
			int srcy, int srcx, int height, int width, int color)
{
	CRITFLAGS

	srcx *= fontwidth(p);
	srcy *= fontheight(p);
	width *= fontwidth(p);
	height *= fontheight(p);


	   CRITBEGIN
	   XGI310SetupForSolidFill(color, 3, 0);
	   XGI310SubsequentSolidFillRect(srcx, srcy, width, height);
	   CRITEND
	   XGI310Sync();

}

void fbcon_XGI_clear8(struct vc_data *conp, struct display *p,
			int srcy, int srcx, int height, int width)
{
	u32 bgx;

	if(!xgi_video_info.accel) {
#ifdef FBCON_HAS_CFB8
	    fbcon_cfb8_clear(conp, p, srcy, srcx, height, width);
#endif
	    return;
	}

	bgx = attr_bgcol_ec(p, conp);
	fbcon_XGI_clear(conp, p, srcy, srcx, height, width, bgx);
}

void fbcon_XGI_clear16(struct vc_data *conp, struct display *p,
			int srcy, int srcx, int height, int width)
{
	u32 bgx;
	if(!xgi_video_info.accel) {
#ifdef FBCON_HAS_CFB16
	    fbcon_cfb16_clear(conp, p, srcy, srcx, height, width);
#endif
	    return;
	}

	bgx = ((u_int16_t*)p->dispsw_data)[attr_bgcol_ec(p, conp)];
	fbcon_XGI_clear(conp, p, srcy, srcx, height, width, bgx);
}

void fbcon_XGI_clear32(struct vc_data *conp, struct display *p,
			int srcy, int srcx, int height, int width)
{
	u32 bgx;

	if(!xgi_video_info.accel) {
#ifdef FBCON_HAS_CFB32
	    fbcon_cfb32_clear(conp, p, srcy, srcx, height, width);
#endif
	    return;
	}

	bgx = ((u_int32_t*)p->dispsw_data)[attr_bgcol_ec(p, conp)];
	fbcon_XGI_clear(conp, p, srcy, srcx, height, width, bgx);
}

void fbcon_XGI_revc(struct display *p, int srcx, int srcy)
{
	CRITFLAGS

	if(!xgi_video_info.accel) {
	    switch(xgi_video_info.video_bpp) {
	    case 16:
#ifdef FBCON_HAS_CFB16
	       fbcon_cfb16_revc(p, srcx, srcy);
#endif
	       break;
	    case 32:
#ifdef FBCON_HAS_CFB32
	       fbcon_cfb32_revc(p, srcx, srcy);
#endif
	       break;
            }
	    return;
	}

	srcx *= fontwidth(p);
	srcy *= fontheight(p);


	   CRITBEGIN
	   XGI310SetupForSolidFill(0, 0x0a, 0);
	   XGI310SubsequentSolidFillRect(srcx, srcy, fontwidth(p), fontheight(p));
	   CRITEND
	   XGI310Sync();

}

#ifdef FBCON_HAS_CFB8
struct display_switch fbcon_XGI8 = {
	setup:			fbcon_cfb8_setup,
	bmove:			fbcon_XGI_bmove,
	clear:			fbcon_XGI_clear8,
	putc:			fbcon_cfb8_putc,
	putcs:			fbcon_cfb8_putcs,
	revc:			fbcon_cfb8_revc,
	clear_margins:		fbcon_cfb8_clear_margins,
	fontwidthmask:		FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif
#ifdef FBCON_HAS_CFB16
struct display_switch fbcon_XGI16 = {
	setup:			fbcon_cfb16_setup,
	bmove:			fbcon_XGI_bmove,
	clear:			fbcon_XGI_clear16,
	putc:			fbcon_cfb16_putc,
	putcs:			fbcon_cfb16_putcs,
	revc:			fbcon_XGI_revc,
	clear_margins:		fbcon_cfb16_clear_margins,
	fontwidthmask:		FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif
#ifdef FBCON_HAS_CFB32
struct display_switch fbcon_XGI32 = {
	setup:			fbcon_cfb32_setup,
	bmove:			fbcon_XGI_bmove,
	clear:			fbcon_XGI_clear32,
	putc:			fbcon_cfb32_putc,
	putcs:			fbcon_cfb32_putcs,
	revc:			fbcon_XGI_revc,
	clear_margins:		fbcon_cfb32_clear_margins,
	fontwidthmask:		FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif

#endif /* KERNEL VERSION */


