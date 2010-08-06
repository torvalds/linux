/*
 * XGI 300/630/730/540/315/550/650/740 frame buffer driver
 * for Linux kernels 2.4.x and 2.5.x
 *
 * 2D acceleration part
 *
 * Based on the X driver's XGI300_accel.h which is
 *     Copyright Xavier Ducoin <x.ducoin@lectra.com>
 *     Copyright 2002 by Thomas Winischhofer, Vienna, Austria
 * and XGI310_accel.h which is
 *     Copyright 2002 by Thomas Winischhofer, Vienna, Austria
 *
 * Author:   Thomas Winischhofer <thomas@winischhofer.net>:
 *			(see http://www.winischhofer.net/
 *			for more information and updates)
 */

#ifndef _XGIFB_ACCEL_H
#define _XGIFB_ACCEL_H

/* Guard accelerator accesses with spin_lock_irqsave? Works well without. */
#undef XGIFB_USE_SPINLOCKS

#ifdef XGIFB_USE_SPINLOCKS
#include <linux/spinlock.h>
#define CRITBEGIN  spin_lock_irqsave(&xgi_video_info.lockaccel), critflags);
#define CRITEND	   spin_unlock_irqrestore(&xgi_video_info.lockaccel), critflags);
#define CRITFLAGS  unsigned long critflags;
#else
#define CRITBEGIN
#define CRITEND
#define CRITFLAGS
#endif

/* Definitions for the XGI engine communication. */

#define PATREGSIZE      384  /* Pattern register size. 384 bytes @ 0x8300 */
#define BR(x)   (0x8200 | (x) << 2)
#define PBR(x)  (0x8300 | (x) << 2)

/* XGI300 engine commands */
#define BITBLT                  0x00000000  /* Blit */
#define COLOREXP                0x00000001  /* Color expand */
#define ENCOLOREXP              0x00000002  /* Enhanced color expand */
#define MULTIPLE_SCANLINE       0x00000003  /* ? */
#define LINE                    0x00000004  /* Draw line */
#define TRAPAZOID_FILL          0x00000005  /* Fill trapezoid */
#define TRANSPARENT_BITBLT      0x00000006  /* Transparent Blit */

/* Additional engine commands for 310/325 */
#define ALPHA_BLEND		0x00000007  /* Alpha blend ? */
#define A3D_FUNCTION		0x00000008  /* 3D command ? */
#define	CLEAR_Z_BUFFER		0x00000009  /* ? */
#define GRADIENT_FILL		0x0000000A  /* Gradient fill */
#define STRETCH_BITBLT		0x0000000B  /* Stretched Blit */

/* source select */
#define SRCVIDEO                0x00000000  /* source is video RAM */
#define SRCSYSTEM               0x00000010  /* source is system memory */
#define SRCCPUBLITBUF           SRCSYSTEM   /* source is CPU-driven BitBuffer (for color expand) */
#define SRCAGP                  0x00000020  /* source is AGP memory (?) */

/* Pattern flags */
#define PATFG                   0x00000000  /* foreground color */
#define PATPATREG               0x00000040  /* pattern in pattern buffer (0x8300) */
#define PATMONO                 0x00000080  /* mono pattern */

/* blitting direction (300 series only) */
#define X_INC                   0x00010000
#define X_DEC                   0x00000000
#define Y_INC                   0x00020000
#define Y_DEC                   0x00000000

/* Clipping flags */
#define NOCLIP                  0x00000000
#define NOMERGECLIP             0x04000000
#define CLIPENABLE              0x00040000
#define CLIPWITHOUTMERGE        0x04040000

/* Transparency */
#define OPAQUE                  0x00000000
#define TRANSPARENT             0x00100000

/* ? */
#define DSTAGP                  0x02000000
#define DSTVIDEO                0x02000000

/* Line */
#define LINE_STYLE              0x00800000
#define NO_RESET_COUNTER        0x00400000
#define NO_LAST_PIXEL           0x00200000

/* Subfunctions for Color/Enhanced Color Expansion (310/325 only) */
#define COLOR_TO_MONO		0x00100000
#define AA_TEXT			0x00200000

/* Some general registers for 310/325 series */
#define SRC_ADDR		0x8200
#define SRC_PITCH		0x8204
#define AGP_BASE		0x8206 /* color-depth dependent value */
#define SRC_Y			0x8208
#define SRC_X			0x820A
#define DST_Y			0x820C
#define DST_X			0x820E
#define DST_ADDR		0x8210
#define DST_PITCH		0x8214
#define DST_HEIGHT		0x8216
#define RECT_WIDTH		0x8218
#define RECT_HEIGHT		0x821A
#define PAT_FGCOLOR		0x821C
#define PAT_BGCOLOR		0x8220
#define SRC_FGCOLOR		0x8224
#define SRC_BGCOLOR		0x8228
#define MONO_MASK		0x822C
#define LEFT_CLIP		0x8234
#define TOP_CLIP		0x8236
#define RIGHT_CLIP		0x8238
#define BOTTOM_CLIP		0x823A
#define COMMAND_READY		0x823C
#define FIRE_TRIGGER      	0x8240

#define PATTERN_REG		0x8300  /* 384 bytes pattern buffer */

/* Line registers */
#define LINE_X0			SRC_Y
#define LINE_X1			DST_Y
#define LINE_Y0			SRC_X
#define LINE_Y1			DST_X
#define LINE_COUNT		RECT_WIDTH
#define LINE_STYLE_PERIOD	RECT_HEIGHT
#define LINE_STYLE_0		MONO_MASK
#define LINE_STYLE_1		0x8230
#define LINE_XN			PATTERN_REG
#define LINE_YN			PATTERN_REG+2

/* Transparent bitblit registers */
#define TRANS_DST_KEY_HIGH	PAT_FGCOLOR
#define TRANS_DST_KEY_LOW	PAT_BGCOLOR
#define TRANS_SRC_KEY_HIGH	SRC_FGCOLOR
#define TRANS_SRC_KEY_LOW	SRC_BGCOLOR

/* Queue */
#define Q_BASE_ADDR		0x85C0  /* Base address of software queue (?) */
#define Q_WRITE_PTR		0x85C4  /* Current write pointer (?) */
#define Q_READ_PTR		0x85C8  /* Current read pointer (?) */
#define Q_STATUS		0x85CC  /* queue status */


#define MMIO_IN8(base, offset) \
	*(volatile u8 *)(((u8*)(base)) + (offset))
#define MMIO_IN16(base, offset) \
	*(volatile u16 *)(void *)(((u8*)(base)) + (offset))
#define MMIO_IN32(base, offset) \
	*(volatile u32 *)(void *)(((u8*)(base)) + (offset))
#define MMIO_OUT8(base, offset, val) \
	*(volatile u8 *)(((u8*)(base)) + (offset)) = (val)
#define MMIO_OUT16(base, offset, val) \
	*(volatile u16 *)(void *)(((u8*)(base)) + (offset)) = (val)
#define MMIO_OUT32(base, offset, val) \
	*(volatile u32 *)(void *)(((u8*)(base)) + (offset)) = (val)



/* ------------- XGI 300 series -------------- */

/* Macros to do useful things with the XGI BitBLT engine */

/* BR(16) (0x8420):

   bit 31 2D engine: 1 is idle,
   bit 30 3D engine: 1 is idle,
   bit 29 Command queue: 1 is empty

   bits 28:24: Current CPU driven BitBlt buffer stage bit[4:0]

   bits 15:0:  Current command queue length

*/

/* TW: BR(16)+2 = 0x8242 */

int     xgiCmdQueLen;

#define XGI300Idle \
  { \
  while( (MMIO_IN16(xgi_video_info.mmio_vbase, BR(16)+2) & 0xE000) != 0xE000){}; \
  while( (MMIO_IN16(xgi_video_info.mmio_vbase, BR(16)+2) & 0xE000) != 0xE000){}; \
  while( (MMIO_IN16(xgi_video_info.mmio_vbase, BR(16)+2) & 0xE000) != 0xE000){}; \
  xgiCmdQueLen=MMIO_IN16(xgi_video_info.mmio_vbase, 0x8240); \
  }
/* TW: (do three times, because 2D engine seems quite unsure about whether or not it's idle) */

#define XGI300SetupSRCBase(base) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(0), base);\
                xgiCmdQueLen --;

#define XGI300SetupSRCPitch(pitch) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT16(xgi_video_info.mmio_vbase, BR(1), pitch);\
                xgiCmdQueLen --;

#define XGI300SetupSRCXY(x,y) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(2), (x)<<16 | (y) );\
                xgiCmdQueLen --;

#define XGI300SetupDSTBase(base) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(4), base);\
                xgiCmdQueLen --;

#define XGI300SetupDSTXY(x,y) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(3), (x)<<16 | (y) );\
                xgiCmdQueLen --;

#define XGI300SetupDSTRect(x,y) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(5), (y)<<16 | (x) );\
                xgiCmdQueLen --;

#define XGI300SetupDSTColorDepth(bpp) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT16(xgi_video_info.mmio_vbase, BR(1)+2, bpp);\
                xgiCmdQueLen --;

#define XGI300SetupRect(w,h) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(6), (h)<<16 | (w) );\
                xgiCmdQueLen --;

#define XGI300SetupPATFG(color) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(7), color);\
                xgiCmdQueLen --;

#define XGI300SetupPATBG(color) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(8), color);\
                xgiCmdQueLen --;

#define XGI300SetupSRCFG(color) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(9), color);\
                xgiCmdQueLen --;

#define XGI300SetupSRCBG(color) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(10), color);\
                xgiCmdQueLen --;

/* 0x8224 src colorkey high */
/* 0x8228 src colorkey low */
/* 0x821c dest colorkey high */
/* 0x8220 dest colorkey low */
#define XGI300SetupSRCTrans(color) \
                if (xgiCmdQueLen <= 1)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, 0x8224, color);\
		MMIO_OUT32(xgi_video_info.mmio_vbase, 0x8228, color);\
		xgiCmdQueLen -= 2;

#define XGI300SetupDSTTrans(color) \
		if (xgiCmdQueLen <= 1)  XGI300Idle;\
		MMIO_OUT32(xgi_video_info.mmio_vbase, 0x821C, color); \
		MMIO_OUT32(xgi_video_info.mmio_vbase, 0x8220, color); \
                xgiCmdQueLen -= 2;

#define XGI300SetupMONOPAT(p0,p1) \
                if (xgiCmdQueLen <= 1)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(11), p0);\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(12), p1);\
                xgiCmdQueLen -= 2;

#define XGI300SetupClipLT(left,top) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(13), ((left) & 0xFFFF) | (top)<<16 );\
                xgiCmdQueLen--;

#define XGI300SetupClipRB(right,bottom) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(14), ((right) & 0xFFFF) | (bottom)<<16 );\
                xgiCmdQueLen--;

/* General */
#define XGI300SetupROP(rop) \
                xgi_video_info.CommandReg = (rop) << 8;

#define XGI300SetupCMDFlag(flags) \
                xgi_video_info.CommandReg |= (flags);

#define XGI300DoCMD \
                if (xgiCmdQueLen <= 1)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(15), xgi_video_info.CommandReg); \
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(16), 0);\
                xgiCmdQueLen -= 2;

/* Line */
#define XGI300SetupX0Y0(x,y) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(2), (y)<<16 | (x) );\
                xgiCmdQueLen--;

#define XGI300SetupX1Y1(x,y) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(3), (y)<<16 | (x) );\
                xgiCmdQueLen--;

#define XGI300SetupLineCount(c) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT16(xgi_video_info.mmio_vbase, BR(6), c);\
                xgiCmdQueLen--;

#define XGI300SetupStylePeriod(p) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT16(xgi_video_info.mmio_vbase, BR(6)+2, p);\
                xgiCmdQueLen--;

#define XGI300SetupStyleLow(ls) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(11), ls);\
                xgiCmdQueLen--;

#define XGI300SetupStyleHigh(ls) \
                if (xgiCmdQueLen <= 0)  XGI300Idle;\
                MMIO_OUT32(xgi_video_info.mmio_vbase, BR(12), ls);\
                xgiCmdQueLen--;



/* ----------- XGI 310/325 series --------------- */

/* Q_STATUS:
   bit 31 = 1: All engines idle and all queues empty
   bit 30 = 1: Hardware Queue (=HW CQ, 2D queue, 3D queue) empty
   bit 29 = 1: 2D engine is idle
   bit 28 = 1: 3D engine is idle
   bit 27 = 1: HW command queue empty
   bit 26 = 1: 2D queue empty
   bit 25 = 1: 3D queue empty
   bit 24 = 1: SW command queue empty
   bits 23:16: 2D counter 3
   bits 15:8:  2D counter 2
   bits 7:0:   2D counter 1

   Where is the command queue length (current amount of commands the queue
   can accept) on the 310/325 series? (The current implementation is taken
   from 300 series and certainly wrong...)
*/

/* TW: FIXME: xgiCmdQueLen is... where....? */
#define XGI310Idle \
  { \
  while( (MMIO_IN16(xgi_video_info.mmio_vbase, Q_STATUS+2) & 0x8000) != 0x8000){}; \
  while( (MMIO_IN16(xgi_video_info.mmio_vbase, Q_STATUS+2) & 0x8000) != 0x8000){}; \
  xgiCmdQueLen=MMIO_IN16(xgi_video_info.mmio_vbase, Q_STATUS); \
  }

#define XGI310SetupSRCBase(base) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, SRC_ADDR, base);\
      xgiCmdQueLen--;

#define XGI310SetupSRCPitch(pitch) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT16(xgi_video_info.mmio_vbase, SRC_PITCH, pitch);\
      xgiCmdQueLen--;

#define XGI310SetupSRCXY(x,y) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, SRC_Y, (x)<<16 | (y) );\
      xgiCmdQueLen--;

#define XGI310SetupDSTBase(base) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, DST_ADDR, base);\
      xgiCmdQueLen--;

#define XGI310SetupDSTXY(x,y) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, DST_Y, (x)<<16 | (y) );\
      xgiCmdQueLen--;

#define XGI310SetupDSTRect(x,y) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, DST_PITCH, (y)<<16 | (x) );\
      xgiCmdQueLen--;

#define XGI310SetupDSTColorDepth(bpp) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT16(xgi_video_info.mmio_vbase, AGP_BASE, bpp);\
      xgiCmdQueLen--;

#define XGI310SetupRect(w,h) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, RECT_WIDTH, (h)<<16 | (w) );\
      xgiCmdQueLen--;

#define XGI310SetupPATFG(color) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, PAT_FGCOLOR, color);\
      xgiCmdQueLen--;

#define XGI310SetupPATBG(color) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, PAT_BGCOLOR, color);\
      xgiCmdQueLen--;

#define XGI310SetupSRCFG(color) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, SRC_FGCOLOR, color);\
      xgiCmdQueLen--;

#define XGI310SetupSRCBG(color) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, SRC_BGCOLOR, color);\
      xgiCmdQueLen--;

#define XGI310SetupSRCTrans(color) \
      if (xgiCmdQueLen <= 1)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, TRANS_SRC_KEY_HIGH, color);\
      MMIO_OUT32(xgi_video_info.mmio_vbase, TRANS_SRC_KEY_LOW, color);\
      xgiCmdQueLen -= 2;

#define XGI310SetupDSTTrans(color) \
      if (xgiCmdQueLen <= 1)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, TRANS_DST_KEY_HIGH, color); \
      MMIO_OUT32(xgi_video_info.mmio_vbase, TRANS_DST_KEY_LOW, color); \
      xgiCmdQueLen -= 2;

#define XGI310SetupMONOPAT(p0,p1) \
      if (xgiCmdQueLen <= 1)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, MONO_MASK, p0);\
      MMIO_OUT32(xgi_video_info.mmio_vbase, MONO_MASK+4, p1);\
      xgiCmdQueLen -= 2;

#define XGI310SetupClipLT(left,top) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, LEFT_CLIP, ((left) & 0xFFFF) | (top)<<16 );\
      xgiCmdQueLen--;

#define XGI310SetupClipRB(right,bottom) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, RIGHT_CLIP, ((right) & 0xFFFF) | (bottom)<<16 );\
      xgiCmdQueLen--;

#define XGI310SetupROP(rop) \
      xgi_video_info.CommandReg = (rop) << 8;

#define XGI310SetupCMDFlag(flags) \
      xgi_video_info.CommandReg |= (flags);

#define XGI310DoCMD \
      if (xgiCmdQueLen <= 1)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, COMMAND_READY, xgi_video_info.CommandReg); \
      MMIO_OUT32(xgi_video_info.mmio_vbase, FIRE_TRIGGER, 0); \
      xgiCmdQueLen -= 2;

#define XGI310SetupX0Y0(x,y) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, LINE_X0, (y)<<16 | (x) );\
      xgiCmdQueLen--;

#define XGI310SetupX1Y1(x,y) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, LINE_X1, (y)<<16 | (x) );\
      xgiCmdQueLen--;

#define XGI310SetupLineCount(c) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT16(xgi_video_info.mmio_vbase, LINE_COUNT, c);\
      xgiCmdQueLen--;

#define XGI310SetupStylePeriod(p) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT16(xgi_video_info.mmio_vbase, LINE_STYLE_PERIOD, p);\
      xgiCmdQueLen--;

#define XGI310SetupStyleLow(ls) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, LINE_STYLE_0, ls);\
      xgiCmdQueLen--;

#define XGI310SetupStyleHigh(ls) \
      if (xgiCmdQueLen <= 0)  XGI310Idle;\
      MMIO_OUT32(xgi_video_info.mmio_vbase, LINE_STYLE_1, ls);\
      xgiCmdQueLen--;

int  XGIfb_initaccel(void);
void XGIfb_syncaccel(void);

extern struct video_info xgi_video_info;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,33)
void fbcon_XGI_bmove(struct display *p, int srcy, int srcx, int dsty,
                     int dstx, int height, int width);
void fbcon_XGI_revc(struct display *p, int srcy, int srcx);
void fbcon_XGI_clear8(struct vc_data *conp, struct display *p, int srcy,
                      int srcx, int height, int width);
void fbcon_XGI_clear16(struct vc_data *conp, struct display *p, int srcy,
                       int srcx, int height, int width);
void fbcon_XGI_clear32(struct vc_data *conp, struct display *p, int srcy,
                       int srcx, int height, int width);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,34)
extern int XGIfb_accel;
void fbcon_XGI_fillrect(struct fb_info *info, const struct fb_fillrect *rect);
void fbcon_XGI_copyarea(struct fb_info *info, const struct fb_copyarea *area);
#endif

#endif
