// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SiS 300/540/630[S]/730[S],
 * SiS 315[E|PRO]/550/[M]650/651/[M]661[F|M]X/740/[M]741[GX]/330/[M]760[GX],
 * XGI V3XT/V5/V8, Z7
 * frame buffer driver for Linux kernels >= 2.4.14 and >=2.6.3
 *
 * 2D acceleration part
 *
 * Based on the XFree86/X.org driver which is
 *     Copyright (C) 2001-2005 by Thomas Winischhofer, Vienna, Austria
 *
 * Author: Thomas Winischhofer <thomas@winischhofer.net>
 *			(see http://www.winischhofer.net/
 *			for more information and updates)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fb.h>
#include <linux/ioport.h>
#include <linux/types.h>
#include <asm/io.h>

#include "sis.h"
#include "sis_accel.h"

static const u8 sisALUConv[] =
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
static const u8 sisPatALUConv[] =
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

static const int myrops[] = {
   	3, 10, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3
};

/* 300 series ----------------------------------------------------- */
#ifdef CONFIG_FB_SIS_300
static void
SiS300Sync(struct sis_video_info *ivideo)
{
	SiS300Idle
}

static void
SiS300SetupForScreenToScreenCopy(struct sis_video_info *ivideo, int xdir, int ydir,
                                 int rop, int trans_color)
{
	SiS300SetupDSTColorDepth(ivideo->DstColor);
	SiS300SetupSRCPitch(ivideo->video_linelength)
	SiS300SetupDSTRect(ivideo->video_linelength, 0xffff)

	if(trans_color != -1) {
		SiS300SetupROP(0x0A)
		SiS300SetupSRCTrans(trans_color)
		SiS300SetupCMDFlag(TRANSPARENT_BITBLT)
	} else {
	        SiS300SetupROP(sisALUConv[rop])
	}
	if(xdir > 0) {
		SiS300SetupCMDFlag(X_INC)
	}
	if(ydir > 0) {
		SiS300SetupCMDFlag(Y_INC)
	}
}

static void
SiS300SubsequentScreenToScreenCopy(struct sis_video_info *ivideo, int src_x,
				   int src_y, int dst_x, int dst_y, int width, int height)
{
	u32 srcbase = 0, dstbase = 0;

	if(src_y >= 2048) {
		srcbase = ivideo->video_linelength * src_y;
		src_y = 0;
	}
	if(dst_y >= 2048) {
		dstbase = ivideo->video_linelength * dst_y;
		dst_y = 0;
	}

	SiS300SetupSRCBase(srcbase);
	SiS300SetupDSTBase(dstbase);

	if(!(ivideo->CommandReg & X_INC))  {
		src_x += width-1;
		dst_x += width-1;
	}
	if(!(ivideo->CommandReg & Y_INC))  {
		src_y += height-1;
		dst_y += height-1;
	}
	SiS300SetupRect(width, height)
	SiS300SetupSRCXY(src_x, src_y)
	SiS300SetupDSTXY(dst_x, dst_y)
	SiS300DoCMD
}

static void
SiS300SetupForSolidFill(struct sis_video_info *ivideo, u32 color, int rop)
{
	SiS300SetupPATFG(color)
	SiS300SetupDSTRect(ivideo->video_linelength, 0xffff)
	SiS300SetupDSTColorDepth(ivideo->DstColor);
	SiS300SetupROP(sisPatALUConv[rop])
	SiS300SetupCMDFlag(PATFG)
}

static void
SiS300SubsequentSolidFillRect(struct sis_video_info *ivideo, int x, int y, int w, int h)
{
	u32 dstbase = 0;

	if(y >= 2048) {
		dstbase = ivideo->video_linelength * y;
		y = 0;
	}
	SiS300SetupDSTBase(dstbase)
	SiS300SetupDSTXY(x,y)
	SiS300SetupRect(w,h)
	SiS300SetupCMDFlag(X_INC | Y_INC | BITBLT)
	SiS300DoCMD
}
#endif

/* 315/330/340 series ---------------------------------------------- */

#ifdef CONFIG_FB_SIS_315
static void
SiS310Sync(struct sis_video_info *ivideo)
{
	SiS310Idle
}

static void
SiS310SetupForScreenToScreenCopy(struct sis_video_info *ivideo, int rop, int trans_color)
{
	SiS310SetupDSTColorDepth(ivideo->DstColor);
	SiS310SetupSRCPitch(ivideo->video_linelength)
	SiS310SetupDSTRect(ivideo->video_linelength, 0x0fff)
	if(trans_color != -1) {
		SiS310SetupROP(0x0A)
		SiS310SetupSRCTrans(trans_color)
		SiS310SetupCMDFlag(TRANSPARENT_BITBLT)
	} else {
	        SiS310SetupROP(sisALUConv[rop])
		/* Set command - not needed, both 0 */
		/* SiSSetupCMDFlag(BITBLT | SRCVIDEO) */
	}
	SiS310SetupCMDFlag(ivideo->SiS310_AccelDepth)
	/* The chip is smart enough to know the direction */
}

static void
SiS310SubsequentScreenToScreenCopy(struct sis_video_info *ivideo, int src_x, int src_y,
			 int dst_x, int dst_y, int width, int height)
{
	u32 srcbase = 0, dstbase = 0;
	int mymin = min(src_y, dst_y);
	int mymax = max(src_y, dst_y);

	/* Although the chip knows the direction to use
	 * if the source and destination areas overlap,
	 * that logic fails if we fiddle with the bitmap
	 * addresses. Therefore, we check if the source
	 * and destination blitting areas overlap and
	 * adapt the bitmap addresses synchronously
	 * if the coordinates exceed the valid range.
	 * The areas do not overlap, we do our
	 * normal check.
	 */
	if((mymax - mymin) < height) {
		if((src_y >= 2048) || (dst_y >= 2048)) {
			srcbase = ivideo->video_linelength * mymin;
			dstbase = ivideo->video_linelength * mymin;
			src_y -= mymin;
			dst_y -= mymin;
		}
	} else {
		if(src_y >= 2048) {
			srcbase = ivideo->video_linelength * src_y;
			src_y = 0;
		}
		if(dst_y >= 2048) {
			dstbase = ivideo->video_linelength * dst_y;
			dst_y = 0;
		}
	}

	srcbase += ivideo->video_offset;
	dstbase += ivideo->video_offset;

	SiS310SetupSRCBase(srcbase);
	SiS310SetupDSTBase(dstbase);
	SiS310SetupRect(width, height)
	SiS310SetupSRCXY(src_x, src_y)
	SiS310SetupDSTXY(dst_x, dst_y)
	SiS310DoCMD
}

static void
SiS310SetupForSolidFill(struct sis_video_info *ivideo, u32 color, int rop)
{
	SiS310SetupPATFG(color)
	SiS310SetupDSTRect(ivideo->video_linelength, 0x0fff)
	SiS310SetupDSTColorDepth(ivideo->DstColor);
	SiS310SetupROP(sisPatALUConv[rop])
	SiS310SetupCMDFlag(PATFG | ivideo->SiS310_AccelDepth)
}

static void
SiS310SubsequentSolidFillRect(struct sis_video_info *ivideo, int x, int y, int w, int h)
{
	u32 dstbase = 0;

	if(y >= 2048) {
		dstbase = ivideo->video_linelength * y;
		y = 0;
	}
	dstbase += ivideo->video_offset;
	SiS310SetupDSTBase(dstbase)
	SiS310SetupDSTXY(x,y)
	SiS310SetupRect(w,h)
	SiS310SetupCMDFlag(BITBLT)
	SiS310DoCMD
}
#endif

/* --------------------------------------------------------------------- */

/* The exported routines */

int sisfb_initaccel(struct sis_video_info *ivideo)
{
#ifdef SISFB_USE_SPINLOCKS
	spin_lock_init(&ivideo->lockaccel);
#endif
	return 0;
}

void sisfb_syncaccel(struct sis_video_info *ivideo)
{
	if(ivideo->sisvga_engine == SIS_300_VGA) {
#ifdef CONFIG_FB_SIS_300
		SiS300Sync(ivideo);
#endif
	} else {
#ifdef CONFIG_FB_SIS_315
		SiS310Sync(ivideo);
#endif
	}
}

int fbcon_sis_sync(struct fb_info *info)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;
	CRITFLAGS

	if((!ivideo->accel) || (!ivideo->engineok))
		return 0;

	CRITBEGIN
	sisfb_syncaccel(ivideo);
	CRITEND

	return 0;
}

void fbcon_sis_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;
	u32 col = 0;
	u32 vxres = info->var.xres_virtual;
	u32 vyres = info->var.yres_virtual;
	int width, height;
	CRITFLAGS

	if(info->state != FBINFO_STATE_RUNNING)
		return;

	if((!ivideo->accel) || (!ivideo->engineok)) {
		cfb_fillrect(info, rect);
		return;
	}

	if(!rect->width || !rect->height || rect->dx >= vxres || rect->dy >= vyres)
		return;

	/* Clipping */
	width = ((rect->dx + rect->width) > vxres) ? (vxres - rect->dx) : rect->width;
	height = ((rect->dy + rect->height) > vyres) ? (vyres - rect->dy) : rect->height;

	switch(info->var.bits_per_pixel) {
	case 8:  col = rect->color;
		 break;
	case 16:
	case 32: col = ((u32 *)(info->pseudo_palette))[rect->color];
		 break;
	}

	if(ivideo->sisvga_engine == SIS_300_VGA) {
#ifdef CONFIG_FB_SIS_300
		CRITBEGIN
		SiS300SetupForSolidFill(ivideo, col, myrops[rect->rop]);
		SiS300SubsequentSolidFillRect(ivideo, rect->dx, rect->dy, width, height);
		CRITEND
#endif
	} else {
#ifdef CONFIG_FB_SIS_315
		CRITBEGIN
		SiS310SetupForSolidFill(ivideo, col, myrops[rect->rop]);
		SiS310SubsequentSolidFillRect(ivideo, rect->dx, rect->dy, width, height);
		CRITEND
#endif
	}

	sisfb_syncaccel(ivideo);
}

void fbcon_sis_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	struct sis_video_info *ivideo = (struct sis_video_info *)info->par;
	u32 vxres = info->var.xres_virtual;
	u32 vyres = info->var.yres_virtual;
	int width = area->width;
	int height = area->height;
	CRITFLAGS

	if(info->state != FBINFO_STATE_RUNNING)
		return;

	if((!ivideo->accel) || (!ivideo->engineok)) {
		cfb_copyarea(info, area);
		return;
	}

	if(!width || !height ||
	   area->sx >= vxres || area->sy >= vyres ||
	   area->dx >= vxres || area->dy >= vyres)
		return;

	/* Clipping */
	if((area->sx + width) > vxres) width = vxres - area->sx;
	if((area->dx + width) > vxres) width = vxres - area->dx;
	if((area->sy + height) > vyres) height = vyres - area->sy;
	if((area->dy + height) > vyres) height = vyres - area->dy;

	if(ivideo->sisvga_engine == SIS_300_VGA) {
#ifdef CONFIG_FB_SIS_300
		int xdir, ydir;

		if(area->sx < area->dx) xdir = 0;
		else                    xdir = 1;
		if(area->sy < area->dy) ydir = 0;
		else                    ydir = 1;

		CRITBEGIN
		SiS300SetupForScreenToScreenCopy(ivideo, xdir, ydir, 3, -1);
		SiS300SubsequentScreenToScreenCopy(ivideo, area->sx, area->sy,
					area->dx, area->dy, width, height);
		CRITEND
#endif
	} else {
#ifdef CONFIG_FB_SIS_315
		CRITBEGIN
		SiS310SetupForScreenToScreenCopy(ivideo, 3, -1);
		SiS310SubsequentScreenToScreenCopy(ivideo, area->sx, area->sy,
					area->dx, area->dy, width, height);
		CRITEND
#endif
	}

	sisfb_syncaccel(ivideo);
}
