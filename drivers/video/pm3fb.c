/*
 *  linux/drivers/video/pm3fb.c -- 3DLabs Permedia3 frame buffer device
 *  
 *  Copyright (C) 2001 Romain Dolbeau <dolbeau@irisa.fr>
 *  Based on code written by:
 *           Sven Luther, <luther@dpt-info.u-strasbg.fr>
 *           Alan Hourihane, <alanh@fairlite.demon.co.uk>
 *           Russell King, <rmk@arm.linux.org.uk>
 *  Based on linux/drivers/video/skeletonfb.c:
 *	Copyright (C) 1997 Geert Uytterhoeven
 *  Based on linux/driver/video/pm2fb.c:
 *      Copyright (C) 1998-1999 Ilario Nardinocchi (nardinoc@CS.UniBO.IT)
 *      Copyright (C) 1999 Jakub Jelinek (jakub@redhat.com)
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 *
 *  $Header: /cvsroot/linux/drivers/video/pm3fb.c,v 1.1 2002/02/25 19:11:06 marcelo Exp $
 *
 *  CHANGELOG:
 *  Mon Feb 11 10:35:48 MET 2002, v 1.4.11B: Cosmetic update.
 *  Wed Jan 23 14:16:59 MET 2002, v 1.4.11: Preliminary 2.5.x support, patch for 2.5.2.
 *  Wed Nov 28 11:08:29 MET 2001, v 1.4.10: potential bug fix for SDRAM-based board, patch for 2.4.16.
 *  Thu Sep 20 10:24:42 MET DST 2001, v 1.4.9: sync bug fix, preliminary flatpanel support, better timings.
 *  Tue Aug 28 10:13:01 MET DST 2001, v 1.4.8: memory timings check, minor bug fixes.
 *  Wed Jul 18 19:06:14 CEST 2001, v 1.4.7: Mode fix (800x600-100, 1024x768-100 changed), using HW panning + accel bug fix.
 *  Mon Jun 25 10:33:56 MET DST 2001, v 1.4.6: Depth 12 fix, chip reset ioctl, moved memory erase ioctl to DEBUG.
 *  Wed Jun 20 11:13:08 MET DST 2001, v 1.4.5: Fixed missing blinking cursor in 8bpp, code cleaning, memory erase IOCTL.
 *  Mon Jun 18 16:00:27 CEST 2001, v 1.4.4: Depth 12 (RGBA 4444) support, code cleaning.
 *  Fri Jun 15 13:53:01 CEST 2001, v 1.4.3: Removed warnings, depth 15 support, add 'depth' option.
 *  Thu Jun 14 10:13:52 MET DST 2001, v 1.4.2: Fixed depth switching bug, preliminary 15bpp (RGB5551) support.
 *  Thu Apr 12 11:16:45 MET DST 2001, v 1.4.1B: Doc updates.
 *  Fri Apr  6 11:12:53 MET DST 2001, v 1.4.1: Configure.help, minor cleanup
 *  Thu Mar 29 10:56:50 MET DST 2001, v 1.4.0: Module & module options support (note: linux patch changed, 2.2.19 added).
 *  Thu Mar 15 15:30:31 MET 2001, v 1.3.2: Fixed mirroring bug on little-endian.
 *  Wed Mar 14 21:25:54 CET 2001, v 1.3.1: Fixed bug in BlockMove (_bmov).
 *  Tue Mar 13 10:53:19 MET 2001, v 1.3.0: Character drawing hardware support (in all width between 1 and 16), fixes.
 *  Thu Mar  8 10:20:16 MET 2001, v 1.2.2: Better J2000 support, "font:" option.
 *  Tue Mar  6 21:25:04 CET 2001, v 1.2.1: Better acceleration support.
 *  Mon Mar  5 21:54:17 CET 2001, v 1.2.0: Partial acceleration support (clear & bmove)
 *  Mon Mar  5 12:52:15 CET 2001, v 1.1.3: Big pan_display fix.
 *  Sun Mar  4 22:21:50 CET 2001, v 1.1.2: (numerous) bug fixes.
 *  Fri Mar  2 15:54:07 CET 2001, v 1.1.1: Might have Appian J2000 support, resource mangement in 2.4
 *  Wed Feb 28 18:21:35 CET 2001, v 1.1.0: Might have multiple boards support (added, but not yest tested)
 *  Tue Feb 27 17:31:12 CET 2001, v 1.0.6: fixes boot-time mode select, add more default mode
 *  Tue Feb 27 14:01:36 CET 2001, v 1.0.5: fixes (1.0.4 was broken for 2.2), cleaning up
 *  Mon Feb 26 23:17:36 CET 2001, v 1.0.4: preliminary 2.4.x support, dropped (useless on pm3) partial product, more OF fix
 *  Mon Feb 26 20:59:05 CET 2001, v 1.0.3: No more shadow register (and wasted memory), endianess fix, use OF-preset resolution by default
 *  Wed Feb 21 22:09:30 CET 2001, v 1.0.2: Code cleaning for future multiboard support, better OF support, bugs fix
 *  Wed Feb 21 19:58:56 CET 2001, v 1.0.1: OpenFirmware support, fixed memory detection, better debug support, code cleaning
 *  Wed Feb 21 14:47:06 CET 2001, v 1.0.0: First working version
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/ctype.h>

#include <video/fbcon.h>
#include <video/fbcon-mfb.h>
#include <video/fbcon-cfb2.h>
#include <video/fbcon-cfb4.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>
#include <video/pm3fb.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#ifdef CONFIG_FB_OF
#include <asm/prom.h>
#endif

/* ************************************* */
/* ***** The various "global" data ***** */
/* ************************************* */

/* those will need a rework for multiple board support */
/* Driver name */
static const char permedia3_name[16] = "Permedia3";

/* the fb_par struct, mandatory */
struct pm3fb_par {
	u32 pixclock;		/* pixclock in KHz */

	u32 width;		/* width of virtual screen */
	u32 height;		/* height of virtual screen */

	u32 hsstart;		/* horiz. sync start */
	u32 hsend;		/* horiz. sync end */
	u32 hbend;		/* horiz. blank end (also gate end) */
	u32 htotal;		/* total width (w/ sync & blank) */

	u32 vsstart;		/* vert. sync start */
	u32 vsend;		/* vert. sync end */
	u32 vbend;		/* vert. blank end */
	u32 vtotal;		/* total height (w/ sync & blank) */

	u32 stride;		/* screen stride */
	u32 base;		/* screen base (xoffset+yoffset) in 128 bits unit */
	/* NOTE : unlike other pm3 stuff above, stored *after* shiftbpp. don't ask */
	u32 depth;		/* screen depth (8, 12, 15, 16 or 32) */
	u32 video;		/* video control (hsync,vsync) */
};

/* memory timings */
struct pm3fb_timings
{
	unsigned long caps;
	unsigned long timings;
	unsigned long control;
	unsigned long refresh;
	unsigned long powerdown;
};
typedef enum pm3fb_timing_result { pm3fb_timing_ok, pm3fb_timing_problem, pm3fb_timing_retry } pm3fb_timing_result;
#define PM3FB_UNKNOWN_TIMING_VALUE ((unsigned long)-1)
#define PM3FB_UNKNOWN_TIMINGS { PM3FB_UNKNOWN_TIMING_VALUE, PM3FB_UNKNOWN_TIMING_VALUE, PM3FB_UNKNOWN_TIMING_VALUE, PM3FB_UNKNOWN_TIMING_VALUE, PM3FB_UNKNOWN_TIMING_VALUE }

/* the fb_info struct, mandatory */
struct pm3fb_info {
	struct fb_info_gen gen;
	unsigned long board_num; /* internal board number */
	unsigned long use_current;
	struct pm3fb_par *current_par;
	struct pci_dev *dev;    /* PCI device */
	unsigned long board_type; /* index in the cardbase */
	unsigned char *fb_base;	/* framebuffer memory base */
	u32 fb_size;		/* framebuffer memory size */
	unsigned char *p_fb;	/* physical address of frame buffer */
	unsigned char *v_fb;	/* virtual address of frame buffer */
	unsigned char *pIOBase;	/* physical address of registers region, must be rg_base or rg_base+PM2_REGS_SIZE depending on the host endianness */
	unsigned char *vIOBase;	/* address of registers after ioremap() */
	struct {
		u8 transp;
		u8 red;
		u8 green;
		u8 blue;
	} palette[256];
	union {
#ifdef FBCON_HAS_CFB16
		u16 cmap12[16]; /* RGBA 4444 */
		u16 cmap15[16]; /* RGBA 5551 */
		u16 cmap16[16]; /* RGBA 5650 */
#endif
#ifdef FBCON_HAS_CFB32
		u32 cmap32[16];
#endif
	} cmap;
	struct pm3fb_timings memt;
};

/* regular resolution database*/
static struct {
	char name[16];
	struct pm3fb_par user_mode;
} mode_base[] __initdata = {
	{
		"default-800x600", {
	49500, 800, 600, 16, 96, 256, 1056, 1, 4, 25, 625,
			    800, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_HIGH
			    |
			    PM3VideoControl_VSYNC_ACTIVE_HIGH
			    | PM3VideoControl_PIXELSIZE_8BIT}}, {
		"1024x768-74", {
	78752, 1024, 768, 32, 128, 304, 1328, 1, 4, 38,
			    806, 1024, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_HIGH
			    |
			    PM3VideoControl_VSYNC_ACTIVE_HIGH
			    | PM3VideoControl_PIXELSIZE_8BIT}}, {
		"1024x768-74-32", {
			78752, 1024, 768, 32, 128, 304, 1328, 1, 4, 38,
			806, 1024, 0, 32,
			PM3VideoControl_ENABLE |
			PM3VideoControl_HSYNC_ACTIVE_HIGH
			|
			PM3VideoControl_VSYNC_ACTIVE_HIGH
			| PM3VideoControl_PIXELSIZE_32BIT}},
/* Generated mode : "1600x1024", for the SGI 1600SW flat panel*/
	{
		"SGI1600SW", {
			108000, 1600, 1024, 16, 56, 104, 1704, 3, 6, 32,
			1056, 1600, 0, 8,
			PM3VideoControl_ENABLE|
			PM3VideoControl_HSYNC_ACTIVE_LOW|PM3VideoControl_VSYNC_ACTIVE_LOW|
			PM3VideoControl_PIXELSIZE_32BIT}}, 
/* ##### auto-generated mode, by fbtimings2pm3 */
/* Generated mode : "640x480-60" */
	{
		"640x480-60", {
	25174, 640, 480, 16, 112, 160, 800, 10, 12, 45,
			    525, 640, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_LOW
			    |
			    PM3VideoControl_VSYNC_ACTIVE_LOW
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "640x480-72" */
	{
		"640x480-72", {
	31199, 640, 480, 24, 64, 192, 832, 9, 12, 40, 520,
			    640, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_LOW
			    |
			    PM3VideoControl_VSYNC_ACTIVE_LOW
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "640x480-75" */
	{
		"640x480-75", {
	31499, 640, 480, 16, 80, 200, 840, 1, 4, 20, 500,
			    640, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_LOW
			    |
			    PM3VideoControl_VSYNC_ACTIVE_LOW
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "640x480-90" */
	{
		"640x480-90", {
	39909, 640, 480, 32, 72, 192, 832, 25, 39, 53, 533,
			    640, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_LOW
			    |
			    PM3VideoControl_VSYNC_ACTIVE_LOW
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "640x480-100" */
	{
		"640x480-100", {
	44899, 640, 480, 32, 160, 208, 848, 22, 34, 51,
			    531, 640, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_LOW
			    |
			    PM3VideoControl_VSYNC_ACTIVE_LOW
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "800x600-48-lace" */
/* INTERLACED NOT SUPPORTED
  {"800x600-48-lace", {35999, 800, 600, 80, 208, 264, 1064, 11, 23, 102, 702, 800, 0, 8, PM3VideoControl_ENABLE|PM3VideoControl_HSYNC_ACTIVE_HIGH|PM3VideoControl_VSYNC_ACTIVE_HIGH|PM3VideoControl_PIXELSIZE_8BIT}}, 
   INTERLACED NOT SUPPORTED */
/* Generated mode : "800x600-56" */
	{
		"800x600-56", {
	35999, 800, 600, 24, 96, 224, 1024, 1, 3, 25, 625,
			    800, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_HIGH
			    |
			    PM3VideoControl_VSYNC_ACTIVE_HIGH
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "800x600-60" */
	{
		"800x600-60", {
	40000, 800, 600, 40, 168, 256, 1056, 1, 5, 28, 628,
			    800, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_HIGH
			    |
			    PM3VideoControl_VSYNC_ACTIVE_HIGH
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "800x600-70" */
	{
		"800x600-70", {
	44899, 800, 600, 24, 168, 208, 1008, 9, 21, 36,
			    636, 800, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_HIGH
			    |
			    PM3VideoControl_VSYNC_ACTIVE_LOW
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "800x600-72" */
	{
		"800x600-72", {
	50000, 800, 600, 56, 176, 240, 1040, 37, 43, 66,
			    666, 800, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_HIGH
			    |
			    PM3VideoControl_VSYNC_ACTIVE_HIGH
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "800x600-75" */
	{
		"800x600-75", {
	49497, 800, 600, 16, 96, 256, 1056, 1, 4, 25, 625,
			    800, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_HIGH
			    |
			    PM3VideoControl_VSYNC_ACTIVE_HIGH
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "800x600-90" */
	{
		"800x600-90", {
	56637, 800, 600, 8, 72, 192, 992, 8, 19, 35, 635,
			    800, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_HIGH
			    |
			    PM3VideoControl_VSYNC_ACTIVE_HIGH
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "800x600-100", from /etc/fb.modes */
/* DISABLED, hsstart == 0
	{
		"800x600-100", {
	67499, 800, 600, 0, 64, 280, 1080, 7, 11, 25, 625,
			    800, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_HIGH
			    |
			    PM3VideoControl_VSYNC_ACTIVE_HIGH
			    | PM3VideoControl_PIXELSIZE_8BIT}},
*/
/* Generated mode : "800x600-100", from ??? */
	{
		"800x600-100", {
			69650, 800, 600, 64, 128, 288, 1088, 4, 10, 40, 640, 800, 0, 8,
			PM3VideoControl_ENABLE|PM3VideoControl_HSYNC_ACTIVE_LOW|
			PM3VideoControl_VSYNC_ACTIVE_LOW|PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "1024x768-43-lace" */
/* INTERLACED NOT SUPPORTED
  {"1024x768-43-lace", {44899, 1024, 768, 8, 184, 240, 1264, 1, 9, 49, 817, 1024, 0, 8, PM3VideoControl_ENABLE|PM3VideoControl_HSYNC_ACTIVE_HIGH|PM3VideoControl_VSYNC_ACTIVE_HIGH|PM3VideoControl_PIXELSIZE_8BIT}}, 
   INTERLACED NOT SUPPORTED */
/* Generated mode : "1024x768-60" */
	{
		"1024x768-60", {
	64998, 1024, 768, 24, 160, 320, 1344, 3, 9, 38,
			    806, 1024, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_LOW
			    |
			    PM3VideoControl_VSYNC_ACTIVE_LOW
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "1024x768-70" */
	{
		"1024x768-70", {
	74996, 1024, 768, 24, 160, 304, 1328, 3, 9, 38,
			    806, 1024, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_LOW
			    |
			    PM3VideoControl_VSYNC_ACTIVE_LOW
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "1024x768-72" */
	{
		"1024x768-72", {
	74996, 10224, 768, 24, 160, 264, 10488, 3, 9, 38,
			    806, 10224, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_LOW
			    |
			    PM3VideoControl_VSYNC_ACTIVE_LOW
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "1024x768-75" */
	{
		"1024x768-75", {
	78746, 1024, 768, 16, 112, 288, 1312, 1, 4, 32,
			    800, 1024, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_HIGH
			    |
			    PM3VideoControl_VSYNC_ACTIVE_HIGH
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "1024x768-90" */
	{
		"1024x768-90", {
	100000, 1024, 768, 0, 96, 288, 1312, 21, 36, 77,
			    845, 1024, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_LOW
			    |
			    PM3VideoControl_VSYNC_ACTIVE_LOW
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "1024x768-100", from /etc/fb.modes */
/* DISABLED, vsstart == 0
	{
		"1024x768-100", {
	109998, 1024, 768, 0, 88, 368, 1392, 0, 8, 24, 792,
			    1024, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_LOW
			    |
			    PM3VideoControl_VSYNC_ACTIVE_LOW
			    | PM3VideoControl_PIXELSIZE_8BIT}},
*/
/* Generated mode : "1024x768-100", from ??? */
	{
		"1024x768-100", {
			115500, 1024, 768, 32, 224, 416, 1440, 3, 13, 34, 802, 1024, 0, 8,
			PM3VideoControl_ENABLE|PM3VideoControl_HSYNC_ACTIVE_LOW|
			PM3VideoControl_VSYNC_ACTIVE_LOW|PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "1152x864-43-lace" */
/* INTERLACED NOT SUPPORTED
  {"1152x864-43-lace", {64998, 1152, 864, 72, 200, 264, 1416, 78, 87, 191, 1055, 1152, 0, 8, PM3VideoControl_ENABLE|PM3VideoControl_HSYNC_ACTIVE_HIGH|PM3VideoControl_VSYNC_ACTIVE_HIGH|PM3VideoControl_PIXELSIZE_8BIT}}, 
   INTERLACED NOT SUPPORTED */
/* Generated mode : "1152x864-47-lace" */
/* INTERLACED NOT SUPPORTED
  {"1152x864-47-lace", {64998, 1152, 864, 88, 216, 296, 1448, 30, 39, 83, 947, 1152, 0, 8, PM3VideoControl_ENABLE|PM3VideoControl_HSYNC_ACTIVE_HIGH|PM3VideoControl_VSYNC_ACTIVE_HIGH|PM3VideoControl_PIXELSIZE_8BIT}}, 
   INTERLACED NOT SUPPORTED */
/* Generated mode : "1152x864-60" */
	{
		"1152x864-60", {
	80000, 1152, 864, 64, 176, 304, 1456, 6, 11, 52,
			    916, 1152, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_HIGH
			    |
			    PM3VideoControl_VSYNC_ACTIVE_HIGH
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "1152x864-70" */
	{
		"1152x864-70", {
	100000, 1152, 864, 40, 192, 360, 1512, 13, 24, 81,
			    945, 1152, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_HIGH
			    |
			    PM3VideoControl_VSYNC_ACTIVE_HIGH
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "1152x864-75" */
	{
		"1152x864-75", {
	109998, 1152, 864, 24, 168, 312, 1464, 45, 53, 138,
			    1002, 1152, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_HIGH
			    |
			    PM3VideoControl_VSYNC_ACTIVE_HIGH
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "1152x864-80" */
	{
		"1152x864-80", {
	109998, 1152, 864, 16, 128, 288, 1440, 30, 37, 94,
			    958, 1152, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_HIGH
			    |
			    PM3VideoControl_VSYNC_ACTIVE_HIGH
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "1280x1024-43-lace" */
/* INTERLACED NOT SUPPORTED
  {"1280x1024-43-lace", {80000, 1024, 1024, 80, 160, 320, 1344, 50, 60, 125, 1149, 1024, 0, 8, PM3VideoControl_ENABLE|PM3VideoControl_HSYNC_ACTIVE_HIGH|PM3VideoControl_VSYNC_ACTIVE_HIGH|PM3VideoControl_PIXELSIZE_8BIT}}, 
   INTERLACED NOT SUPPORTED */
/* Generated mode : "1280x1024-47-lace" */
/* INTERLACED NOT SUPPORTED
  {"1280x1024-47-lace", {80000, 1280, 1024, 80, 160, 320, 1600, 1, 11, 29, 1053, 1280, 0, 8, PM3VideoControl_ENABLE|PM3VideoControl_HSYNC_ACTIVE_HIGH|PM3VideoControl_VSYNC_ACTIVE_HIGH|PM3VideoControl_PIXELSIZE_8BIT}}, 
   INTERLACED NOT SUPPORTED */
/* Generated mode : "1280x1024-60" */
	{
		"1280x1024-60", {
	107991, 1280, 1024, 48, 160, 408, 1688, 1, 4, 42,
			    1066, 1280, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_HIGH
			    |
			    PM3VideoControl_VSYNC_ACTIVE_HIGH
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "1280x1024-70" */
	{
		"1280x1024-70", {
	125992, 1280, 1024, 80, 192, 408, 1688, 1, 6, 42,
			    1066, 1280, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_HIGH
			    |
			    PM3VideoControl_VSYNC_ACTIVE_HIGH
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "1280x1024-74" */
	{
		"1280x1024-74", {
	134989, 1280, 1024, 32, 176, 432, 1712, 0, 30, 40,
			    1064, 1280, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_HIGH
			    |
			    PM3VideoControl_VSYNC_ACTIVE_HIGH
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "1280x1024-75" */
	{
		"1280x1024-75", {
	134989, 1280, 1024, 16, 160, 408, 1688, 1, 4, 42,
			    1066, 1280, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_HIGH
			    |
			    PM3VideoControl_VSYNC_ACTIVE_HIGH
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "1600x1200-60" */
	{
		"1600x1200-60", {
	155981, 1600, 1200, 32, 192, 448, 2048, 10, 18, 70,
			    1270, 1600, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_LOW
			    |
			    PM3VideoControl_VSYNC_ACTIVE_LOW
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "1600x1200-66" */
	{
		"1600x1200-66", {
	171998, 1600, 1200, 40, 176, 480, 2080, 3, 6, 53,
			    1253, 1600, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_LOW
			    |
			    PM3VideoControl_VSYNC_ACTIVE_LOW
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* Generated mode : "1600x1200-76" */
	{
		"1600x1200-76", {
	197980, 1600, 1200, 40, 176, 480, 2080, 3, 8, 50,
			    1250, 1600, 0, 8,
			    PM3VideoControl_ENABLE |
			    PM3VideoControl_HSYNC_ACTIVE_LOW
			    |
			    PM3VideoControl_VSYNC_ACTIVE_LOW
			    | PM3VideoControl_PIXELSIZE_8BIT}},
/* ##### end of auto-generated mode */
	{
	"\0",}
};

/* more mandatory stuff (see skeletonfb.c + framebuffer driver HOWTO */
static struct pm3fb_info fb_info[PM3_MAX_BOARD];
static struct pm3fb_par current_par[PM3_MAX_BOARD];
static int current_par_valid[PM3_MAX_BOARD];
/* to allow explicit filtering of board */
short bus[PM3_MAX_BOARD];
short slot[PM3_MAX_BOARD];
short func[PM3_MAX_BOARD];
short disable[PM3_MAX_BOARD];
short noaccel[PM3_MAX_BOARD];
char fontn[PM3_MAX_BOARD][PM3_FONTNAME_SIZE];
short depth[PM3_MAX_BOARD];
short flatpanel[PM3_MAX_BOARD];
static struct display disp[PM3_MAX_BOARD];
static char g_options[PM3_OPTIONS_SIZE] __initdata = "pm3fb,dummy";
short printtimings = 0;
short forcesize[PM3_MAX_BOARD];

/* ********************* */
/* ***** prototype ***** */
/* ********************* */
/* card-specific */
static void pm3fb_j2000_setup(struct pm3fb_info *l_fb_info);
/* permedia3-specific */
static pm3fb_timing_result pm3fb_preserve_memory_timings(struct pm3fb_info *l_fb_info);
static pm3fb_timing_result pm3fb_try_memory_timings(struct pm3fb_info *l_fb_info);
static void pm3fb_write_memory_timings(struct pm3fb_info *l_fb_info);
static unsigned long pm3fb_read_dac_reg(struct pm3fb_info *l_fb_info,
					unsigned long r);
static unsigned long pm3fb_CalculateClock(struct pm3fb_info *l_fb_info, unsigned long reqclock,	/* In kHz units */
					  unsigned long refclock,	/* In kHz units */
					  unsigned char *prescale,	/* ClkPreScale */
					  unsigned char *feedback,	/* ClkFeedBackScale */
					  unsigned char *postscale
					  /* ClkPostScale */ );
static void pm3fb_clear_memory(struct pm3fb_info *l_fb_info, u32 cc);
static void pm3fb_clear_colormap(struct pm3fb_info *l_fb_info, unsigned char r, unsigned char g, unsigned char b);
static void pm3fb_common_init(struct pm3fb_info *l_fb_info);
static int pm3fb_Shiftbpp(struct pm3fb_info *l_fb_info,
			  unsigned long depth, int v);
static int pm3fb_Unshiftbpp(struct pm3fb_info *l_fb_info,
			    unsigned long depth, int v);
static void pm3fb_mapIO(struct pm3fb_info *l_fb_info);
static void pm3fb_unmapIO(struct pm3fb_info *l_fb_info);
#if defined(PM3FB_MASTER_DEBUG) && (PM3FB_MASTER_DEBUG >= 2)
static void pm3fb_show_cur_mode(struct pm3fb_info *l_fb_info);
#endif
static void pm3fb_show_cur_timing(struct pm3fb_info *l_fb_info);
static void pm3fb_write_mode(struct pm3fb_info *l_fb_info);
static void pm3fb_read_mode(struct pm3fb_info *l_fb_info,
			    struct pm3fb_par *curpar);
static unsigned long pm3fb_size_memory(struct pm3fb_info *l_fb_info);
/* accelerated permedia3-specific */
#ifdef PM3FB_USE_ACCEL
static void pm3fb_wait_pm3(struct pm3fb_info *l_fb_info);
static void pm3fb_init_engine(struct pm3fb_info *l_fb_info);
#ifdef FBCON_HAS_CFB32
static void pm3fb_cfb32_clear(struct vc_data *conp,
			      struct display *p,
			      int sy, int sx, int height, int width);
static void pm3fb_cfb32_clear_margins(struct vc_data *conp,
				      struct display *p, int bottom_only);
#endif /* FBCON_HAS_CFB32 */
#ifdef FBCON_HAS_CFB16
static void pm3fb_cfb16_clear(struct vc_data *conp,
			      struct display *p,
			      int sy, int sx, int height, int width);
static void pm3fb_cfb16_clear_margins(struct vc_data *conp,
				      struct display *p, int bottom_only);
#endif /* FBCON_HAS_CFB16 */
#ifdef FBCON_HAS_CFB8
static void pm3fb_cfb8_clear(struct vc_data *conp,
			     struct display *p,
			     int sy, int sx, int height, int width);
static void pm3fb_cfb8_clear_margins(struct vc_data *conp,
				     struct display *p, int bottom_only);
#endif /* FBCON_HAS_CFB8 */
#if defined(FBCON_HAS_CFB8) || defined(FBCON_HAS_CFB16) || defined(FBCON_HAS_CFB32)
static void pm3fb_cfbX_bmove(struct display *p,
			     int sy, int sx,
			     int dy, int dx, int height, int width);
static void pm3fb_cfbX_putc(struct vc_data *conp, struct display *p,
			    int c, int yy, int xx);
static void pm3fb_cfbX_putcs(struct vc_data *conp, struct display *p,
			     const unsigned short *s, int count, int yy,
			     int xx);
static void pm3fb_cfbX_revc(struct display *p, int xx, int yy);
#endif /* FBCON_HAS_CFB8 || FBCON_HAS_CFB16 || FBCON_HAS_CFB32 */
#endif /* PM3FB_USE_ACCEL */
/* pre-init */
static void pm3fb_mode_setup(char *mode, unsigned long board_num);
static void pm3fb_pciid_setup(char *pciid, unsigned long board_num);
static char *pm3fb_boardnum_setup(char *options, unsigned long *bn);
static void pm3fb_real_setup(char *options);
/* fbdev */
static int pm3fb_encode_fix(struct fb_fix_screeninfo *fix,
			    const void *par, struct fb_info_gen *info);
static int pm3fb_decode_var(const struct fb_var_screeninfo *var,
			    void *par, struct fb_info_gen *info);
static void pm3fb_encode_depth(struct fb_var_screeninfo *var, long d);
static int pm3fb_encode_var(struct fb_var_screeninfo *var,
			    const void *par, struct fb_info_gen *info);
static void pm3fb_get_par(void *par, struct fb_info_gen *info);
static void pm3fb_set_par(const void *par, struct fb_info_gen *info);
static void pm3fb_set_color(struct pm3fb_info *l_fb_info,
			    unsigned char regno, unsigned char r,
			    unsigned char g, unsigned char b);
static int pm3fb_getcolreg(unsigned regno, unsigned *red, unsigned *green,
			   unsigned *blue, unsigned *transp,
			   struct fb_info *info);
static int pm3fb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   struct fb_info *info);
static int pm3fb_blank(int blank_mode, struct fb_info_gen *info);
static void pm3fb_set_disp(const void *par, struct display *disp,
			   struct fb_info_gen *info);
static void pm3fb_detect(void);
static int pm3fb_pan_display(const struct fb_var_screeninfo *var,
			     struct fb_info_gen *info);
static int pm3fb_ioctl(struct inode *inode, struct file *file,
                       u_int cmd, u_long arg, int con,
		       struct fb_info *info);


/* the struct that hold them together */
struct fbgen_hwswitch pm3fb_switch = {
	pm3fb_detect, pm3fb_encode_fix, pm3fb_decode_var, pm3fb_encode_var,
	pm3fb_get_par, pm3fb_set_par, pm3fb_getcolreg, 
	pm3fb_pan_display, pm3fb_blank, pm3fb_set_disp
};

static struct fb_ops pm3fb_ops = {
	.owner =	THIS_MODULE,
	.fb_get_fix =	fbgen_get_fix,
	.fb_get_var =	fbgen_get_var,
	.fb_set_var =	fbgen_set_var,
	.fb_get_cmap =	fbgen_get_cmap,
	.fb_set_cmap =	fbgen_set_cmap,
	.fb_setcolreg =	pm3fb_setcolreg,
	.fb_pan_display =fbgen_pan_display,
	.fb_blank =	fbgen_blank,
	.fb_ioctl =	pm3fb_ioctl,
};

#ifdef PM3FB_USE_ACCEL
#ifdef FBCON_HAS_CFB32
static struct display_switch pm3fb_cfb32 = {
	fbcon_cfb32_setup, pm3fb_cfbX_bmove, pm3fb_cfb32_clear,
	pm3fb_cfbX_putc, pm3fb_cfbX_putcs, pm3fb_cfbX_revc,
	NULL /* cursor() */ , NULL /* set_font() */ ,
	pm3fb_cfb32_clear_margins,
	FONTWIDTHRANGE(1, 16)	/* true only if accelerated... */
};
#endif /* FBCON_HAS_CFB32 */
#ifdef FBCON_HAS_CFB16
static struct display_switch pm3fb_cfb16 = {
	fbcon_cfb16_setup, pm3fb_cfbX_bmove, pm3fb_cfb16_clear,
	pm3fb_cfbX_putc, pm3fb_cfbX_putcs, pm3fb_cfbX_revc,
	NULL /* cursor() */ , NULL /* set_font() */ ,
	pm3fb_cfb16_clear_margins,
	FONTWIDTHRANGE(1, 16)	/* true only if accelerated... */
};
#endif /* FBCON_HAS_CFB16 */
#ifdef FBCON_HAS_CFB8
static struct display_switch pm3fb_cfb8 = {
	fbcon_cfb8_setup, pm3fb_cfbX_bmove, pm3fb_cfb8_clear,
	pm3fb_cfbX_putc, pm3fb_cfbX_putcs, pm3fb_cfbX_revc,
	NULL /* cursor() */ , NULL /* set_font() */ ,
	pm3fb_cfb8_clear_margins,
	FONTWIDTHRANGE(1, 16)	/* true only if accelerated... */
};
#endif /* FBCON_HAS_CFB8 */
#endif /* PM3FB_USE_ACCEL */

/* ****************************** */
/* ***** card-specific data ***** */
/* ****************************** */
struct pm3fb_card_timings {
	unsigned long memsize; /* 0 for last value (i.e. default) */
	struct pm3fb_timings memt;
};

static struct pm3fb_card_timings t_FormacProFormance3[] = {
	{ 16, { 0x02e311b8, 0x06100205, 0x08000002, 0x00000079, 0x00000000} },
	{ 0, { 0x02e311b8, 0x06100205, 0x08000002, 0x00000079, 0x00000000} } /* from 16 MB PF3 */
};

static struct pm3fb_card_timings t_AppianJeronimo2000[] = {
	{ 32, { 0x02e311B8, 0x07424905, 0x0c000003, 0x00000061, 0x00000000} },
	{ 0, { 0x02e311B8, 0x07424905, 0x0c000003, 0x00000061, 0x00000000} } /* from 32MB J2000 */
};

static struct pm3fb_card_timings t_3DLabsOxygenVX1[] = {
	{ 32, { 0x30e311b8, 0x08501204, 0x08000002, 0x0000006b, 0x00000000} },
	{ 0, { 0x30e311b8, 0x08501204, 0x08000002, 0x0000006b, 0x00000000} } /* from 32MB VX1 */
};

static struct {
		char cardname[32]; /* recognized card name */
		u16 subvendor; /* subvendor of the card */
		u16 subdevice; /* subdevice of the card */
		u8  func; /* function of the card to which the extra init apply */
		void (*specific_setup)(struct pm3fb_info *l_fb_info); /* card/func specific setup, done before _any_ FB access */
	struct pm3fb_card_timings *c_memt; /* defauls timings for the boards */
} cardbase[] = {
	{ "Unknown Permedia3 board", 0xFFFF, 0xFFFF, 0xFF, NULL, NULL },
	{ "Appian Jeronimo 2000 head 1", 0x1097, 0x3d32, 1, NULL,
	  t_AppianJeronimo2000
	},
	{ "Appian Jeronimo 2000 head 2", 0x1097, 0x3d32, 2, pm3fb_j2000_setup,
	  t_AppianJeronimo2000
	},
	{ "Formac ProFormance 3", PCI_VENDOR_ID_3DLABS, 0x000a, 0, NULL, /* Formac use 3DLabs ID ?!? */
	  t_FormacProFormance3
	},
	{ "3DLabs Permedia3 Create!", PCI_VENDOR_ID_3DLABS, 0x0127, 0, NULL, NULL },
	{ "3DLabs Oxygen VX1 PCI", PCI_VENDOR_ID_3DLABS, 0x0121, 0, NULL,
	  t_3DLabsOxygenVX1
	},
	{ "3DLabs Oxygen VX1 AGP", PCI_VENDOR_ID_3DLABS, 0x0125, 0, NULL, NULL },
	{ "3DLabs Oxygen VX1-16 AGP", PCI_VENDOR_ID_3DLABS, 0x0140, 0, NULL, NULL },
	{ "3DLabs Oxygen VX1-1600SW PCI", PCI_VENDOR_ID_3DLABS, 0x0800, 0, NULL, NULL },
	{ "\0", 0x0, 0x0, 0, NULL, NULL }
};

/* ********************************** */
/* ***** card-specific function ***** */
/* ********************************** */
static void pm3fb_j2000_setup(struct pm3fb_info *l_fb_info)
{       /* the appian j2000 require more initialization of the second head */
	/* l_fb_info must point to the _second_ head of the J2000 */
	
	DTRACE;

	l_fb_info->memt = t_AppianJeronimo2000[0].memt; /* 32 MB, first and only j2000 ? */
	
	pm3fb_write_memory_timings(l_fb_info);
}

/* *************************************** */
/* ***** permedia3-specific function ***** */
/* *************************************** */
static pm3fb_timing_result pm3fb_preserve_memory_timings(struct pm3fb_info *l_fb_info)
{
	l_fb_info->memt.caps = PM3_READ_REG(PM3LocalMemCaps);
	l_fb_info->memt.timings = PM3_READ_REG(PM3LocalMemTimings);
	l_fb_info->memt.control = PM3_READ_REG(PM3LocalMemControl);
	l_fb_info->memt.refresh = PM3_READ_REG(PM3LocalMemRefresh);
	l_fb_info->memt.powerdown = PM3_READ_REG(PM3LocalMemPowerDown);

	if ((l_fb_info->memt.caps == PM3FB_UNKNOWN_TIMING_VALUE) ||
	    (l_fb_info->memt.timings == PM3FB_UNKNOWN_TIMING_VALUE) ||
	    (l_fb_info->memt.control == PM3FB_UNKNOWN_TIMING_VALUE) ||
	    (l_fb_info->memt.refresh == PM3FB_UNKNOWN_TIMING_VALUE) ||
	    (l_fb_info->memt.powerdown == PM3FB_UNKNOWN_TIMING_VALUE))
	{
		printk(KERN_ERR "pm3fb: invalid memory timings in permedia3 board #%ld\n", l_fb_info->board_num);
		return(pm3fb_try_memory_timings(l_fb_info));
	}
	return(pm3fb_timing_ok);
}

static pm3fb_timing_result pm3fb_try_memory_timings(struct pm3fb_info *l_fb_info)
{
	if (cardbase[l_fb_info->board_type].c_memt)
	{
		int i = 0, done = 0;
		while (!done)
		{
			if ((cardbase[l_fb_info->board_type].c_memt[i].memsize == l_fb_info->fb_size)
			    || !(cardbase[l_fb_info->board_type].c_memt[i].memsize))
			{ /* will use the 0-sized timings by default */
				done = 1;
				l_fb_info->memt = cardbase[l_fb_info->board_type].c_memt[i].memt;
				printk(KERN_WARNING  "pm3fb: trying to use predefined memory timings for permedia3 board #%ld (%s, %ld MB)\n",
				       l_fb_info->board_num,
				       cardbase[l_fb_info->board_type].cardname,
				       cardbase[l_fb_info->board_type].c_memt[i].memsize);
				pm3fb_write_memory_timings(l_fb_info);
				return(pm3fb_timing_retry);
			}
			i++;
		}
	} else
		return(pm3fb_timing_problem);
	return(pm3fb_timing_ok);
}

static void pm3fb_write_memory_timings(struct pm3fb_info *l_fb_info)
{
	unsigned char m, n, p;
	unsigned long clockused;
	
	PM3_SLOW_WRITE_REG(PM3LocalMemCaps, l_fb_info->memt.caps);
	PM3_SLOW_WRITE_REG(PM3LocalMemTimings, l_fb_info->memt.timings);
	PM3_SLOW_WRITE_REG(PM3LocalMemControl, l_fb_info->memt.control);
	PM3_SLOW_WRITE_REG(PM3LocalMemRefresh, l_fb_info->memt.refresh);
	PM3_SLOW_WRITE_REG(PM3LocalMemPowerDown, l_fb_info->memt.powerdown);

	clockused =
	    pm3fb_CalculateClock(l_fb_info, 2 * 105000, PM3_REF_CLOCK, &m,
				 &n, &p);

	PM3_WRITE_DAC_REG(PM3RD_KClkPreScale, m);
	PM3_WRITE_DAC_REG(PM3RD_KClkFeedbackScale, n);
	PM3_WRITE_DAC_REG(PM3RD_KClkPostScale, p);
	PM3_WRITE_DAC_REG(PM3RD_KClkControl,
			  PM3RD_KClkControl_STATE_RUN |
			  PM3RD_KClkControl_SOURCE_PLL |
			  PM3RD_KClkControl_ENABLE);
	PM3_WRITE_DAC_REG(PM3RD_MClkControl,
			  PM3RD_MClkControl_STATE_RUN |
			  PM3RD_MClkControl_SOURCE_KCLK |
			  PM3RD_MClkControl_ENABLE);
	PM3_WRITE_DAC_REG(PM3RD_SClkControl,
			  PM3RD_SClkControl_STATE_RUN |
			  PM3RD_SClkControl_SOURCE_PCLK |
			  PM3RD_SClkControl_ENABLE);
}

static unsigned long pm3fb_read_dac_reg(struct pm3fb_info *l_fb_info,
					unsigned long r)
{
	DASSERT((l_fb_info->vIOBase != (unsigned char *) (-1)),
		"l_fb_info->vIOBase mapped in read dac reg\n");
	PM3_SET_INDEX(r);
	mb();
	return (PM3_READ_REG(PM3RD_IndexedData));
}

/* Calculating various clock parameter */
static unsigned long pm3fb_CalculateClock(struct pm3fb_info *l_fb_info, unsigned long reqclock,	/* In kHz units */
					  unsigned long refclock,	/* In kHz units */
					  unsigned char *prescale,	/* ClkPreScale */
					  unsigned char *feedback,	/* ClkFeedBackScale */
					  unsigned char *postscale
					  /* ClkPostScale */ )
{
	int f, pre, post;
	unsigned long freq;
	long freqerr = 1000;
	unsigned long actualclock = 0;

	DTRACE;

	for (f = 1; f < 256; f++) {
		for (pre = 1; pre < 256; pre++) {
			for (post = 0; post < 5; post++) {
				freq =
				    ((2 * refclock * f) /
				     (pre * (1 << post)));
				if ((reqclock > freq - freqerr)
				    && (reqclock < freq + freqerr)) {
					freqerr =
					    (reqclock >
					     freq) ? reqclock -
					    freq : freq - reqclock;
					*feedback = f;
					*prescale = pre;
					*postscale = post;
					actualclock = freq;
				}
			}
		}
	}

	return (actualclock);
}

static int pm3fb_Shiftbpp(struct pm3fb_info *l_fb_info,
			  unsigned long depth, int v)
{
	DTRACE;
	
	switch (depth) {
	case 8:
		return (v >> 4);
	case 12:
	case 15:
	case 16:
		return (v >> 3);
	case 32:
		return (v >> 2);
	}
	DPRINTK(1, "Unsupported depth %ld\n", depth);
	return (0);
}

static int pm3fb_Unshiftbpp(struct pm3fb_info *l_fb_info,
			    unsigned long depth, int v)
{
	DTRACE;

	switch (depth) {
	case 8:
		return (v << 4);
	case 12:	
	case 15:
	case 16:
		return (v << 3);
	case 32:
		return (v << 2);
	}
	DPRINTK(1, "Unsupported depth %ld\n", depth);
	return (0);
}

static void pm3fb_mapIO(struct pm3fb_info *l_fb_info)
{
	DTRACE;

	l_fb_info->vIOBase =
	    ioremap((unsigned long) l_fb_info->pIOBase, PM3_REGS_SIZE);
	l_fb_info->v_fb =
	    ioremap((unsigned long) l_fb_info->p_fb, l_fb_info->fb_size);
	DPRINTK(2, "IO mapping : IOBase %lx / %lx, fb %lx / %lx\n",
		(unsigned long) l_fb_info->pIOBase,
		(unsigned long) l_fb_info->vIOBase,
		(unsigned long) l_fb_info->p_fb,
		(unsigned long) l_fb_info->v_fb);
}

static void pm3fb_unmapIO(struct pm3fb_info *l_fb_info)
{
	DTRACE;

	iounmap(l_fb_info->vIOBase);
	iounmap(l_fb_info->v_fb);
	l_fb_info->vIOBase = (unsigned char *) -1;
	l_fb_info->v_fb = (unsigned char *) -1;
}

#if defined(PM3FB_MASTER_DEBUG) && (PM3FB_MASTER_DEBUG >= 2)
static void pm3fb_show_cur_mode(struct pm3fb_info *l_fb_info)
{
	DPRINTK(2, "PM3Aperture0: 0x%08x\n", PM3_READ_REG(PM3Aperture0));
	DPRINTK(2, "PM3Aperture1: 0x%08x\n", PM3_READ_REG(PM3Aperture1));
	DPRINTK(2, "PM3ByAperture1Mode: 0x%08x\n",
		PM3_READ_REG(PM3ByAperture1Mode));
	DPRINTK(2, "PM3ByAperture2Mode: 0x%08x\n",
		PM3_READ_REG(PM3ByAperture2Mode));
	DPRINTK(2, "PM3ChipConfig: 0x%08x\n", PM3_READ_REG(PM3ChipConfig));
	DPRINTK(2, "PM3FIFODis: 0x%08x\n", PM3_READ_REG(PM3FIFODis));
	DPRINTK(2, "PM3HTotal: 0x%08x\n", PM3_READ_REG(PM3HTotal));
	DPRINTK(2, "PM3HbEnd: 0x%08x\n", PM3_READ_REG(PM3HbEnd));
	DPRINTK(2, "PM3HgEnd: 0x%08x\n", PM3_READ_REG(PM3HgEnd));
	DPRINTK(2, "PM3HsEnd: 0x%08x\n", PM3_READ_REG(PM3HsEnd));
	DPRINTK(2, "PM3HsStart: 0x%08x\n", PM3_READ_REG(PM3HsStart));
	DPRINTK(2, "PM3MemBypassWriteMask: 0x%08x\n",
		PM3_READ_REG(PM3MemBypassWriteMask));
	DPRINTK(2, "PM3RD_IndexControl: 0x%08x\n",
		PM3_READ_REG(PM3RD_IndexControl));
	DPRINTK(2, "PM3ScreenBase: 0x%08x\n", PM3_READ_REG(PM3ScreenBase));
	DPRINTK(2, "PM3ScreenStride: 0x%08x\n",
		PM3_READ_REG(PM3ScreenStride));
	DPRINTK(2, "PM3VClkCtl: 0x%08x\n", PM3_READ_REG(PM3VClkCtl));
	DPRINTK(2, "PM3VTotal: 0x%08x\n", PM3_READ_REG(PM3VTotal));
	DPRINTK(2, "PM3VbEnd: 0x%08x\n", PM3_READ_REG(PM3VbEnd));
	DPRINTK(2, "PM3VideoControl: 0x%08x\n",
		PM3_READ_REG(PM3VideoControl));
	DPRINTK(2, "PM3VsEnd: 0x%08x\n", PM3_READ_REG(PM3VsEnd));
	DPRINTK(2, "PM3VsStart: 0x%08x\n", PM3_READ_REG(PM3VsStart));

	DPRINTK(2, "PM3RD_ColorFormat: %ld\n",
		PM3_READ_DAC_REG(PM3RD_ColorFormat));
	DPRINTK(2, "PM3RD_DACControl: %ld\n",
		PM3_READ_DAC_REG(PM3RD_DACControl));
	DPRINTK(2, "PM3RD_DClk0FeedbackScale: %ld\n",
		PM3_READ_DAC_REG(PM3RD_DClk0FeedbackScale));
	DPRINTK(2, "PM3RD_DClk0PostScale: %ld\n",
		PM3_READ_DAC_REG(PM3RD_DClk0PostScale));
	DPRINTK(2, "PM3RD_DClk0PreScale: %ld\n",
		PM3_READ_DAC_REG(PM3RD_DClk0PreScale));
	DPRINTK(2, "[not set] PM3RD_IndexControl: %ld\n",
		PM3_READ_DAC_REG(PM3RD_IndexControl));
	DPRINTK(2, "PM3RD_MiscControl: %ld\n",
		PM3_READ_DAC_REG(PM3RD_MiscControl));
	DPRINTK(2, "PM3RD_PixelSize: %ld\n",
		PM3_READ_DAC_REG(PM3RD_PixelSize));
	DPRINTK(2, "PM3RD_SyncControl: %ld\n",
		PM3_READ_DAC_REG(PM3RD_SyncControl));
}

#endif /* defined(PM3FB_MASTER_DEBUG) && (PM3FB_MASTER_DEBUG >= 2) */
static void pm3fb_show_cur_timing(struct pm3fb_info *l_fb_info)
{
	u16 subvendor, subdevice;

	if ((!pci_read_config_word
	     (l_fb_info->dev, PCI_SUBSYSTEM_VENDOR_ID, &subvendor))
	    &&
	    (!pci_read_config_word
	     (l_fb_info->dev, PCI_SUBSYSTEM_ID, &subdevice))) {
		/* well, nothing... */
	} else {
		subvendor = subdevice = (u16)-1;
	}

	printk(KERN_INFO "pm3fb: memory timings for board #%ld (subvendor: 0x%hx, subdevice: 0x%hx)\n", l_fb_info->board_num, subvendor, subdevice);
	printk(KERN_INFO " PM3LocalMemCaps: 0x%08x\n",
	       PM3_READ_REG(PM3LocalMemCaps));
	printk(KERN_INFO " PM3LocalMemTimings: 0x%08x\n",
	       PM3_READ_REG(PM3LocalMemTimings));
	printk(KERN_INFO " PM3LocalMemControl: 0x%08x\n",
	       PM3_READ_REG(PM3LocalMemControl));
	printk(KERN_INFO " PM3LocalMemRefresh: 0x%08x\n",
	       PM3_READ_REG(PM3LocalMemRefresh));
	printk(KERN_INFO " PM3LocalMemPowerDown: 0x%08x\n",
	       PM3_READ_REG(PM3LocalMemPowerDown));
}

/* write the mode to registers */
static void pm3fb_write_mode(struct pm3fb_info *l_fb_info)
{
	char tempsync = 0x00, tempmisc = 0x00;
	DTRACE;

	PM3_SLOW_WRITE_REG(PM3MemBypassWriteMask, 0xffffffff);
	PM3_SLOW_WRITE_REG(PM3Aperture0, 0x00000000);
	PM3_SLOW_WRITE_REG(PM3Aperture1, 0x00000000);
	PM3_SLOW_WRITE_REG(PM3FIFODis, 0x00000007);

	PM3_SLOW_WRITE_REG(PM3HTotal,
			   pm3fb_Shiftbpp(l_fb_info,
					  l_fb_info->current_par->depth,
					  l_fb_info->current_par->htotal -
					  1));
	PM3_SLOW_WRITE_REG(PM3HsEnd,
			   pm3fb_Shiftbpp(l_fb_info,
					  l_fb_info->current_par->depth,
					  l_fb_info->current_par->hsend));
	PM3_SLOW_WRITE_REG(PM3HsStart,
			   pm3fb_Shiftbpp(l_fb_info,
					  l_fb_info->current_par->depth,
					  l_fb_info->current_par->
					  hsstart));
	PM3_SLOW_WRITE_REG(PM3HbEnd,
			   pm3fb_Shiftbpp(l_fb_info,
					  l_fb_info->current_par->depth,
					  l_fb_info->current_par->hbend));
	PM3_SLOW_WRITE_REG(PM3HgEnd,
			   pm3fb_Shiftbpp(l_fb_info,
					  l_fb_info->current_par->depth,
					  l_fb_info->current_par->hbend));
	PM3_SLOW_WRITE_REG(PM3ScreenStride,
			   pm3fb_Shiftbpp(l_fb_info,
					  l_fb_info->current_par->depth,
					  l_fb_info->current_par->stride));
	PM3_SLOW_WRITE_REG(PM3VTotal, l_fb_info->current_par->vtotal - 1);
	PM3_SLOW_WRITE_REG(PM3VsEnd, l_fb_info->current_par->vsend - 1);
	PM3_SLOW_WRITE_REG(PM3VsStart,
			   l_fb_info->current_par->vsstart - 1);
	PM3_SLOW_WRITE_REG(PM3VbEnd, l_fb_info->current_par->vbend);

	switch (l_fb_info->current_par->depth) {
	case 8:
		PM3_SLOW_WRITE_REG(PM3ByAperture1Mode,
				   PM3ByApertureMode_PIXELSIZE_8BIT);
		PM3_SLOW_WRITE_REG(PM3ByAperture2Mode,
				   PM3ByApertureMode_PIXELSIZE_8BIT);
		break;

	case 12:
	case 15:
	case 16:
#ifndef __BIG_ENDIAN
		PM3_SLOW_WRITE_REG(PM3ByAperture1Mode,
				   PM3ByApertureMode_PIXELSIZE_16BIT);
		PM3_SLOW_WRITE_REG(PM3ByAperture2Mode,
				   PM3ByApertureMode_PIXELSIZE_16BIT);
#else
		PM3_SLOW_WRITE_REG(PM3ByAperture1Mode,
				   PM3ByApertureMode_PIXELSIZE_16BIT |
				   PM3ByApertureMode_BYTESWAP_BADC);
		PM3_SLOW_WRITE_REG(PM3ByAperture2Mode,
				   PM3ByApertureMode_PIXELSIZE_16BIT |
				   PM3ByApertureMode_BYTESWAP_BADC);
#endif /* ! __BIG_ENDIAN */
		break;

	case 32:
#ifndef __BIG_ENDIAN
		PM3_SLOW_WRITE_REG(PM3ByAperture1Mode,
				   PM3ByApertureMode_PIXELSIZE_32BIT);
		PM3_SLOW_WRITE_REG(PM3ByAperture2Mode,
				   PM3ByApertureMode_PIXELSIZE_32BIT);
#else
		PM3_SLOW_WRITE_REG(PM3ByAperture1Mode,
				   PM3ByApertureMode_PIXELSIZE_32BIT |
				   PM3ByApertureMode_BYTESWAP_DCBA);
		PM3_SLOW_WRITE_REG(PM3ByAperture2Mode,
				   PM3ByApertureMode_PIXELSIZE_32BIT |
				   PM3ByApertureMode_BYTESWAP_DCBA);
#endif /* ! __BIG_ENDIAN */
		break;

	default:
		DPRINTK(1, "Unsupported depth %d\n",
			l_fb_info->current_par->depth);
		break;
	}

	/*
	 * Oxygen VX1 - it appears that setting PM3VideoControl and
	 * then PM3RD_SyncControl to the same SYNC settings undoes
	 * any net change - they seem to xor together.  Only set the
	 * sync options in PM3RD_SyncControl.  --rmk
	 */
	{
		unsigned int video = l_fb_info->current_par->video;

		video &= ~(PM3VideoControl_HSYNC_MASK |
			   PM3VideoControl_VSYNC_MASK);
		video |= PM3VideoControl_HSYNC_ACTIVE_HIGH |
			 PM3VideoControl_VSYNC_ACTIVE_HIGH;
		PM3_SLOW_WRITE_REG(PM3VideoControl, video);
	}
	PM3_SLOW_WRITE_REG(PM3VClkCtl,
			   (PM3_READ_REG(PM3VClkCtl) & 0xFFFFFFFC));
	PM3_SLOW_WRITE_REG(PM3ScreenBase, l_fb_info->current_par->base);
	PM3_SLOW_WRITE_REG(PM3ChipConfig,
			   (PM3_READ_REG(PM3ChipConfig) & 0xFFFFFFFD));

	{
		unsigned char m;	/* ClkPreScale */
		unsigned char n;	/* ClkFeedBackScale */
		unsigned char p;	/* ClkPostScale */
		(void)pm3fb_CalculateClock(l_fb_info, l_fb_info->current_par->pixclock, PM3_REF_CLOCK, &m, &n, &p);

		DPRINTK(2,
			"Pixclock: %d, Pre: %d, Feedback: %d, Post: %d\n",
			l_fb_info->current_par->pixclock, (int) m, (int) n,
			(int) p);

		PM3_WRITE_DAC_REG(PM3RD_DClk0PreScale, m);
		PM3_WRITE_DAC_REG(PM3RD_DClk0FeedbackScale, n);
		PM3_WRITE_DAC_REG(PM3RD_DClk0PostScale, p);
	}
	/*
	   PM3_WRITE_DAC_REG(PM3RD_IndexControl, 0x00);
	 */
	/*
	   PM3_SLOW_WRITE_REG(PM3RD_IndexControl, 0x00);
	 */
	if ((l_fb_info->current_par->video & PM3VideoControl_HSYNC_MASK) ==
	    PM3VideoControl_HSYNC_ACTIVE_HIGH)
		tempsync |= PM3RD_SyncControl_HSYNC_ACTIVE_HIGH;
	if ((l_fb_info->current_par->video & PM3VideoControl_VSYNC_MASK) ==
	    PM3VideoControl_VSYNC_ACTIVE_HIGH)
		tempsync |= PM3RD_SyncControl_VSYNC_ACTIVE_HIGH;
	
	PM3_WRITE_DAC_REG(PM3RD_SyncControl, tempsync);
	DPRINTK(2, "PM3RD_SyncControl: %d\n", tempsync);
	
	if (flatpanel[l_fb_info->board_num])
	{
		PM3_WRITE_DAC_REG(PM3RD_DACControl, PM3RD_DACControl_BLANK_PEDESTAL_ENABLE);
		PM3_WAIT(2);
		PM3_WRITE_REG(PM3VSConfiguration, 0x06);
		PM3_WRITE_REG(0x5a00, 1 << 14); /* black magic... */
		tempmisc = PM3RD_MiscControl_VSB_OUTPUT_ENABLE;
	}
	else
		PM3_WRITE_DAC_REG(PM3RD_DACControl, 0x00);

	switch (l_fb_info->current_par->depth) {
	case 8:
		PM3_WRITE_DAC_REG(PM3RD_PixelSize,
				  PM3RD_PixelSize_8_BIT_PIXELS);
		PM3_WRITE_DAC_REG(PM3RD_ColorFormat,
				  PM3RD_ColorFormat_CI8_COLOR |
				  PM3RD_ColorFormat_COLOR_ORDER_BLUE_LOW);
		tempmisc |= PM3RD_MiscControl_HIGHCOLOR_RES_ENABLE;
		break;
	case 12:
		PM3_WRITE_DAC_REG(PM3RD_PixelSize,
				  PM3RD_PixelSize_16_BIT_PIXELS);
		PM3_WRITE_DAC_REG(PM3RD_ColorFormat,
				  PM3RD_ColorFormat_4444_COLOR |
				  PM3RD_ColorFormat_COLOR_ORDER_BLUE_LOW |
				  PM3RD_ColorFormat_LINEAR_COLOR_EXT_ENABLE);
		tempmisc |= PM3RD_MiscControl_DIRECTCOLOR_ENABLE |
			PM3RD_MiscControl_HIGHCOLOR_RES_ENABLE;
		break;		
	case 15:
		PM3_WRITE_DAC_REG(PM3RD_PixelSize,
				  PM3RD_PixelSize_16_BIT_PIXELS);
		PM3_WRITE_DAC_REG(PM3RD_ColorFormat,
				  PM3RD_ColorFormat_5551_FRONT_COLOR |
				  PM3RD_ColorFormat_COLOR_ORDER_BLUE_LOW |
				  PM3RD_ColorFormat_LINEAR_COLOR_EXT_ENABLE);
		tempmisc |= PM3RD_MiscControl_DIRECTCOLOR_ENABLE |
			PM3RD_MiscControl_HIGHCOLOR_RES_ENABLE;
		break;		
	case 16:
		PM3_WRITE_DAC_REG(PM3RD_PixelSize,
				  PM3RD_PixelSize_16_BIT_PIXELS);
		PM3_WRITE_DAC_REG(PM3RD_ColorFormat,
				  PM3RD_ColorFormat_565_FRONT_COLOR |
				  PM3RD_ColorFormat_COLOR_ORDER_BLUE_LOW |
				  PM3RD_ColorFormat_LINEAR_COLOR_EXT_ENABLE);
		tempmisc |= PM3RD_MiscControl_DIRECTCOLOR_ENABLE |
			PM3RD_MiscControl_HIGHCOLOR_RES_ENABLE;
		break;
	case 32:
		PM3_WRITE_DAC_REG(PM3RD_PixelSize,
				  PM3RD_PixelSize_32_BIT_PIXELS);
		PM3_WRITE_DAC_REG(PM3RD_ColorFormat,
				  PM3RD_ColorFormat_8888_COLOR |
				  PM3RD_ColorFormat_COLOR_ORDER_BLUE_LOW);
		tempmisc |= PM3RD_MiscControl_DIRECTCOLOR_ENABLE |
			PM3RD_MiscControl_HIGHCOLOR_RES_ENABLE;
		break;
	}
	PM3_WRITE_DAC_REG(PM3RD_MiscControl, tempmisc);
	
	PM3_SHOW_CUR_MODE;
}

static void pm3fb_read_mode(struct pm3fb_info *l_fb_info,
			    struct pm3fb_par *curpar)
{
	unsigned long pixsize1, pixsize2, clockused;
	unsigned long pre, feedback, post;

	DTRACE;

	clockused = PM3_READ_REG(PM3VClkCtl);

	switch (clockused) {
	case 3:
		pre = PM3_READ_DAC_REG(PM3RD_DClk3PreScale);
		feedback = PM3_READ_DAC_REG(PM3RD_DClk3FeedbackScale);
		post = PM3_READ_DAC_REG(PM3RD_DClk3PostScale);

		DPRINTK(2,
			"DClk3 parameter: Pre: %ld, Feedback: %ld, Post: %ld ; giving pixclock: %ld\n",
			pre, feedback, post, PM3_SCALE_TO_CLOCK(pre,
								feedback,
								post));
		break;
	case 2:
		pre = PM3_READ_DAC_REG(PM3RD_DClk2PreScale);
		feedback = PM3_READ_DAC_REG(PM3RD_DClk2FeedbackScale);
		post = PM3_READ_DAC_REG(PM3RD_DClk2PostScale);

		DPRINTK(2,
			"DClk2 parameter: Pre: %ld, Feedback: %ld, Post: %ld ; giving pixclock: %ld\n",
			pre, feedback, post, PM3_SCALE_TO_CLOCK(pre,
								feedback,
								post));
		break;
	case 1:
		pre = PM3_READ_DAC_REG(PM3RD_DClk1PreScale);
		feedback = PM3_READ_DAC_REG(PM3RD_DClk1FeedbackScale);
		post = PM3_READ_DAC_REG(PM3RD_DClk1PostScale);

		DPRINTK(2,
			"DClk1 parameter: Pre: %ld, Feedback: %ld, Post: %ld ; giving pixclock: %ld\n",
			pre, feedback, post, PM3_SCALE_TO_CLOCK(pre,
								feedback,
								post));
		break;
	case 0:
		pre = PM3_READ_DAC_REG(PM3RD_DClk0PreScale);
		feedback = PM3_READ_DAC_REG(PM3RD_DClk0FeedbackScale);
		post = PM3_READ_DAC_REG(PM3RD_DClk0PostScale);

		DPRINTK(2,
			"DClk0 parameter: Pre: %ld, Feedback: %ld, Post: %ld ; giving pixclock: %ld\n",
			pre, feedback, post, PM3_SCALE_TO_CLOCK(pre,
								feedback,
								post));
		break;
	default:
		pre = feedback = post = 0;
		DPRINTK(1, "Unknowk D clock used : %ld\n", clockused);
		break;
	}

	curpar->pixclock = PM3_SCALE_TO_CLOCK(pre, feedback, post);

	pixsize1 =
	    PM3ByApertureMode_PIXELSIZE_MASK &
	    (PM3_READ_REG(PM3ByAperture1Mode));
	pixsize2 =
	    PM3ByApertureMode_PIXELSIZE_MASK &
	    (PM3_READ_REG(PM3ByAperture2Mode));

	DASSERT((pixsize1 == pixsize2),
		"pixsize the same in both aperture\n");

	if (pixsize1 & PM3ByApertureMode_PIXELSIZE_32BIT)
		curpar->depth = 32;
	else if (pixsize1 & PM3ByApertureMode_PIXELSIZE_16BIT)
	{
		curpar->depth = 16;
	}
	else
		curpar->depth = 8;

	/* not sure if I need to add one on the next ; it give better result with */
	curpar->htotal =
	    pm3fb_Unshiftbpp(l_fb_info, curpar->depth,
			     1 + PM3_READ_REG(PM3HTotal));
	curpar->hsend =
	    pm3fb_Unshiftbpp(l_fb_info, curpar->depth,
			     PM3_READ_REG(PM3HsEnd));
	curpar->hsstart =
	    pm3fb_Unshiftbpp(l_fb_info, curpar->depth,
			     PM3_READ_REG(PM3HsStart));
	curpar->hbend =
	    pm3fb_Unshiftbpp(l_fb_info, curpar->depth,
			     PM3_READ_REG(PM3HbEnd));

	curpar->stride =
	    pm3fb_Unshiftbpp(l_fb_info, curpar->depth,
			     PM3_READ_REG(PM3ScreenStride));

	curpar->vtotal = 1 + PM3_READ_REG(PM3VTotal);
	curpar->vsend = 1 + PM3_READ_REG(PM3VsEnd);
	curpar->vsstart = 1 + PM3_READ_REG(PM3VsStart);
	curpar->vbend = PM3_READ_REG(PM3VbEnd);

	curpar->video = PM3_READ_REG(PM3VideoControl);

	curpar->base = PM3_READ_REG(PM3ScreenBase);
	curpar->width = curpar->htotal - curpar->hbend; /* make virtual == displayed resolution */
	curpar->height = curpar->vtotal - curpar->vbend;

	DPRINTK(2, "Found : %d * %d, %d Khz, stride is %08x\n",
		curpar->width, curpar->height, curpar->pixclock,
		curpar->stride);
}

static unsigned long pm3fb_size_memory(struct pm3fb_info *l_fb_info)
{
	unsigned long memsize = 0, tempBypass, i, temp1, temp2;
	u16 subvendor, subdevice;
	pm3fb_timing_result ptr;

	DTRACE;

	l_fb_info->fb_size = 64 * 1024 * 1024;	/* pm3 aperture always 64 MB */
	pm3fb_mapIO(l_fb_info);	/* temporary map IO */

	DASSERT((l_fb_info->vIOBase != NULL),
		"IO successfully mapped before mem detect\n");
	DASSERT((l_fb_info->v_fb != NULL),
		"FB successfully mapped before mem detect\n");

	/* card-specific stuff, *before* accessing *any* FB memory */
	if ((!pci_read_config_word
	     (l_fb_info->dev, PCI_SUBSYSTEM_VENDOR_ID, &subvendor))
	    &&
	    (!pci_read_config_word
	     (l_fb_info->dev, PCI_SUBSYSTEM_ID, &subdevice))) {
		i = 0; l_fb_info->board_type = 0;
		while ((cardbase[i].cardname[0]) && !(l_fb_info->board_type)) {
			if ((cardbase[i].subvendor == subvendor) &&
			    (cardbase[i].subdevice == subdevice) &&
			    (cardbase[i].func == PCI_FUNC(l_fb_info->dev->devfn))) {
				DPRINTK(2, "Card #%ld is an %s\n",
					l_fb_info->board_num,
					cardbase[i].cardname);
				if (cardbase[i].specific_setup)
					cardbase[i].specific_setup(l_fb_info);
				l_fb_info->board_type = i;
			}
			i++;
		}
		if (!l_fb_info->board_type) {
			DPRINTK(1, "Card #%ld is an unknown 0x%04x / 0x%04x\n",
				l_fb_info->board_num, subvendor, subdevice);
		}
	} else {
		printk(KERN_ERR "pm3fb: Error: pci_read_config_word failed, board #%ld\n",
		       l_fb_info->board_num);
	}

	if (printtimings)
		pm3fb_show_cur_timing(l_fb_info);
	
	/* card-specific setup is done, we preserve the final
           memory timing for future reference */
	if ((ptr = pm3fb_preserve_memory_timings(l_fb_info)) == pm3fb_timing_problem) { /* memory timings were wrong ! oops.... */
		return(0);
	}
	
	tempBypass = PM3_READ_REG(PM3MemBypassWriteMask);

	DPRINTK(2, "PM3MemBypassWriteMask was: 0x%08lx\n", tempBypass);

	PM3_SLOW_WRITE_REG(PM3MemBypassWriteMask, 0xFFFFFFFF);

	/* pm3 split up memory, replicates, and do a lot of nasty stuff IMHO ;-) */
	for (i = 0; i < 32; i++) {
		fb_writel(i * 0x00345678,
			  (l_fb_info->v_fb + (i * 1048576)));
		mb();
		temp1 = fb_readl((l_fb_info->v_fb + (i * 1048576)));

		/* Let's check for wrapover, write will fail at 16MB boundary */
		if (temp1 == (i * 0x00345678))
			memsize = i;
		else
			break;
	}

	DPRINTK(2, "First detect pass already got %ld MB\n", memsize + 1);

	if (memsize == i) {
		for (i = 0; i < 32; i++) {
			/* Clear first 32MB ; 0 is 0, no need to byteswap */
			writel(0x0000000,
			       (l_fb_info->v_fb + (i * 1048576)));
			mb();
		}

		for (i = 32; i < 64; i++) {
			fb_writel(i * 0x00345678,
				  (l_fb_info->v_fb + (i * 1048576)));
			mb();
			temp1 =
			    fb_readl((l_fb_info->v_fb + (i * 1048576)));
			temp2 =
			    fb_readl((l_fb_info->v_fb +
				      ((i - 32) * 1048576)));
			if ((temp1 == (i * 0x00345678)) && (temp2 == 0))	/* different value, different RAM... */
				memsize = i;
			else
				break;
		}
	}

	DPRINTK(2, "Second detect pass got %ld MB\n", memsize + 1);

	PM3_SLOW_WRITE_REG(PM3MemBypassWriteMask, tempBypass);

	pm3fb_unmapIO(l_fb_info);
	memsize = 1048576 * (memsize + 1);

	DPRINTK(2, "Returning 0x%08lx bytes\n", memsize);

	if (forcesize[l_fb_info->board_num] && ((forcesize[l_fb_info->board_num] * 1048576) != memsize))
	{
		printk(KERN_WARNING "pm3fb: mismatch between probed (%ld MB) and specified (%hd MB) memory size, using SPECIFIED !\n", memsize, forcesize[l_fb_info->board_num]);
		memsize = 1048576 * forcesize[l_fb_info->board_num];
	}
	
	l_fb_info->fb_size = memsize;
	
	if (ptr == pm3fb_timing_retry)
	{
		printk(KERN_WARNING "pm3fb: retrying memory timings check");
		if (pm3fb_try_memory_timings(l_fb_info) == pm3fb_timing_problem)
			return(0);
	}
	
	return (memsize);
}

static void pm3fb_clear_memory(struct pm3fb_info *l_fb_info, u32 cc)
{
	int i;

	DTRACE;

	for (i = 0; i < (l_fb_info->fb_size / sizeof(u32)) ; i++) /* clear entire FB memory to black */
	{
		fb_writel(cc, (l_fb_info->v_fb + (i * sizeof(u32))));
	}
}

static void pm3fb_clear_colormap(struct pm3fb_info *l_fb_info, unsigned char r, unsigned char g, unsigned char b)
{
	int i;

	DTRACE;

	for (i = 0; i < 256 ; i++) /* fill color map with white */
		pm3fb_set_color(l_fb_info, i, r, g, b);

}

/* common initialisation */
static void pm3fb_common_init(struct pm3fb_info *l_fb_info)
{
	DTRACE;

	DPRINTK(2, "Initializing board #%ld @ %lx\n", l_fb_info->board_num,
		(unsigned long) l_fb_info);

	strcpy(l_fb_info->gen.info.modename, permedia3_name);
	disp[l_fb_info->board_num].scrollmode = 0;	/* SCROLL_YNOMOVE; *//* 0 means "let fbcon choose" */
	l_fb_info->gen.parsize = sizeof(struct pm3fb_par);
	l_fb_info->gen.info.changevar = NULL;
	l_fb_info->gen.info.fbops = &pm3fb_ops;
	l_fb_info->gen.info.disp = &(disp[l_fb_info->board_num]);
	if (fontn[l_fb_info->board_num][0])
		strcpy(l_fb_info->gen.info.fontname,
		       fontn[l_fb_info->board_num]);
	l_fb_info->gen.info.switch_con = &fbgen_switch;
	l_fb_info->gen.info.updatevar = &fbgen_update_var;	/* */
	l_fb_info->gen.info.flags = FBINFO_FLAG_DEFAULT;

	pm3fb_mapIO(l_fb_info);

	pm3fb_clear_memory(l_fb_info, 0);
	pm3fb_clear_colormap(l_fb_info, 0, 0, 0);

	(void) fbgen_get_var(&(disp[l_fb_info->board_num]).var, -1,
			     &l_fb_info->gen.info);

	if (depth[l_fb_info->board_num]) /* override mode-defined depth */
	{
		pm3fb_encode_depth(&(disp[l_fb_info->board_num]).var, depth[l_fb_info->board_num]);
		(disp[l_fb_info->board_num]).var.bits_per_pixel = depth2bpp(depth[l_fb_info->board_num]);
	}

	(void) fbgen_do_set_var(&(disp[l_fb_info->board_num]).var, 1,
				&l_fb_info->gen);

	fbgen_set_disp(-1, &l_fb_info->gen);

	do_install_cmap(0, &l_fb_info->gen.info);

	if (register_framebuffer(&l_fb_info->gen.info) < 0) {
		DPRINTK(1, "Couldn't register framebuffer\n");
		return;
	}

	PM3_WRITE_DAC_REG(PM3RD_CursorMode,
			  PM3RD_CursorMode_CURSOR_DISABLE);
	
	PM3_SHOW_CUR_MODE;
	
	pm3fb_write_mode(l_fb_info);
	
	printk("fb%d: %s, using %uK of video memory (%s)\n",
	       l_fb_info->gen.info.node,
	       permedia3_name, (u32) (l_fb_info->fb_size >> 10),
	       cardbase[l_fb_info->board_type].cardname);
}

/* **************************************************** */
/* ***** accelerated permedia3-specific functions ***** */
/* **************************************************** */
#ifdef PM3FB_USE_ACCEL
static void pm3fb_wait_pm3(struct pm3fb_info *l_fb_info)
{
	DTRACE;

	PM3_SLOW_WRITE_REG(PM3FilterMode, PM3FilterModeSync);
	PM3_SLOW_WRITE_REG(PM3Sync, 0);
	mb();
	do {
		while ((PM3_READ_REG(PM3OutFIFOWords)) == 0);
		rmb();
	} while ((PM3_READ_REG(PM3OutputFifo)) != PM3Sync_Tag);
}

static void pm3fb_init_engine(struct pm3fb_info *l_fb_info)
{
	PM3_SLOW_WRITE_REG(PM3FilterMode, PM3FilterModeSync);
	PM3_SLOW_WRITE_REG(PM3StatisticMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3DeltaMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3RasterizerMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3ScissorMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3LineStippleMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3AreaStippleMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3GIDMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3DepthMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3StencilMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3StencilData, 0x0);
	PM3_SLOW_WRITE_REG(PM3ColorDDAMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureCoordMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureIndexMode0, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureIndexMode1, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureReadMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3LUTMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureFilterMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureCompositeMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureApplicationMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureCompositeColorMode1, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureCompositeAlphaMode1, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureCompositeColorMode0, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureCompositeAlphaMode0, 0x0);
	PM3_SLOW_WRITE_REG(PM3FogMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3ChromaTestMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3AlphaTestMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3AntialiasMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3YUVMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3AlphaBlendColorMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3AlphaBlendAlphaMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3DitherMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3LogicalOpMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3RouterMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3Window, 0x0);

	PM3_SLOW_WRITE_REG(PM3Config2D, 0x0);

	PM3_SLOW_WRITE_REG(PM3SpanColorMask, 0xffffffff);

	PM3_SLOW_WRITE_REG(PM3XBias, 0x0);
	PM3_SLOW_WRITE_REG(PM3YBias, 0x0);
	PM3_SLOW_WRITE_REG(PM3DeltaControl, 0x0);

	PM3_SLOW_WRITE_REG(PM3BitMaskPattern, 0xffffffff);

	PM3_SLOW_WRITE_REG(PM3FBDestReadEnables,
			   PM3FBDestReadEnables_E(0xff) |
			   PM3FBDestReadEnables_R(0xff) |
			   PM3FBDestReadEnables_ReferenceAlpha(0xff));
	PM3_SLOW_WRITE_REG(PM3FBDestReadBufferAddr0, 0x0);
	PM3_SLOW_WRITE_REG(PM3FBDestReadBufferOffset0, 0x0);
	PM3_SLOW_WRITE_REG(PM3FBDestReadBufferWidth0,
			   PM3FBDestReadBufferWidth_Width(l_fb_info->
							  current_par->
							  width));

	PM3_SLOW_WRITE_REG(PM3FBDestReadMode,
			   PM3FBDestReadMode_ReadEnable |
			   PM3FBDestReadMode_Enable0);
	PM3_SLOW_WRITE_REG(PM3FBSourceReadBufferAddr, 0x0);
	PM3_SLOW_WRITE_REG(PM3FBSourceReadBufferOffset, 0x0);
	PM3_SLOW_WRITE_REG(PM3FBSourceReadBufferWidth,
			   PM3FBSourceReadBufferWidth_Width(l_fb_info->
							    current_par->
							    width));
	PM3_SLOW_WRITE_REG(PM3FBSourceReadMode,
			   PM3FBSourceReadMode_Blocking |
			   PM3FBSourceReadMode_ReadEnable);

	{
		unsigned long rm = 1;
		switch (l_fb_info->current_par->depth) {
		case 8:
			PM3_SLOW_WRITE_REG(PM3PixelSize,
					   PM3PixelSize_GLOBAL_8BIT);
			break;
		case 12:
		case 15:
		case 16:
			PM3_SLOW_WRITE_REG(PM3PixelSize,
					   PM3PixelSize_GLOBAL_16BIT);
			break;
		case 32:
			PM3_SLOW_WRITE_REG(PM3PixelSize,
					   PM3PixelSize_GLOBAL_32BIT);
			break;
		default:
			DPRINTK(1, "Unsupported depth %d\n",
				l_fb_info->current_par->depth);
			break;
		}
		PM3_SLOW_WRITE_REG(PM3RasterizerMode, rm);
	}

	PM3_SLOW_WRITE_REG(PM3FBSoftwareWriteMask, 0xffffffff);
	PM3_SLOW_WRITE_REG(PM3FBHardwareWriteMask, 0xffffffff);
	PM3_SLOW_WRITE_REG(PM3FBWriteMode,
			   PM3FBWriteMode_WriteEnable |
			   PM3FBWriteMode_OpaqueSpan |
			   PM3FBWriteMode_Enable0);
	PM3_SLOW_WRITE_REG(PM3FBWriteBufferAddr0, 0x0);
	PM3_SLOW_WRITE_REG(PM3FBWriteBufferOffset0, 0x0);
	PM3_SLOW_WRITE_REG(PM3FBWriteBufferWidth0,
			   PM3FBWriteBufferWidth_Width(l_fb_info->
						       current_par->
						       width));

	PM3_SLOW_WRITE_REG(PM3SizeOfFramebuffer, 0x0);
	{
		unsigned long sofb = (8UL * l_fb_info->fb_size) /
			((depth2bpp(l_fb_info->current_par->depth))
			 * l_fb_info->current_par->width);	/* size in lines of FB */
		if (sofb > 4095)
			PM3_SLOW_WRITE_REG(PM3SizeOfFramebuffer, 4095);
		else
			PM3_SLOW_WRITE_REG(PM3SizeOfFramebuffer, sofb);
		
		switch (l_fb_info->current_par->depth) {
		case 8:
			PM3_SLOW_WRITE_REG(PM3DitherMode,
					   (1 << 10) | (2 << 3));
			break;
		case 12:
		case 15:
		case 16:
			PM3_SLOW_WRITE_REG(PM3DitherMode,
					   (1 << 10) | (1 << 3));
			break;
		case 32:
			PM3_SLOW_WRITE_REG(PM3DitherMode,
					   (1 << 10) | (0 << 3));
			break;
		default:
			DPRINTK(1, "Unsupported depth %d\n",
				l_fb_info->current_par->depth);
			break;
		}
	}

	PM3_SLOW_WRITE_REG(PM3dXDom, 0x0);
	PM3_SLOW_WRITE_REG(PM3dXSub, 0x0);
	PM3_SLOW_WRITE_REG(PM3dY, (1 << 16));
	PM3_SLOW_WRITE_REG(PM3StartXDom, 0x0);
	PM3_SLOW_WRITE_REG(PM3StartXSub, 0x0);
	PM3_SLOW_WRITE_REG(PM3StartY, 0x0);
	PM3_SLOW_WRITE_REG(PM3Count, 0x0);
	
/* Disable LocalBuffer. better safe than sorry */
	PM3_SLOW_WRITE_REG(PM3LBDestReadMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3LBDestReadEnables, 0x0);
	PM3_SLOW_WRITE_REG(PM3LBSourceReadMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3LBWriteMode, 0x0);
	
	pm3fb_wait_pm3(l_fb_info);
}

#ifdef FBCON_HAS_CFB32
static void pm3fb_cfb32_clear(struct vc_data *conp,
			      struct display *p,
			      int sy, int sx, int height, int width)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) p->fb_info;
	u32 c;

	DTRACE;

	sx = sx * fontwidth(p);
	width = width * fontwidth(p);
	sy = sy * fontheight(p);
	height = height * fontheight(p);
	c = ((u32 *) p->dispsw_data)[attr_bgcol_ec(p, conp)];

	/* block fills in 32bpp are hard, but in low res (width <= 1600 :-)
	   we can use 16bpp operations, but not if NoWriteMask is on (SDRAM)  */
	if ((l_fb_info->current_par->width > 1600) ||
	    (l_fb_info->memt.caps & PM3LocalMemCaps_NoWriteMask)) {
		PM3_WAIT(4);

		PM3_WRITE_REG(PM3Config2D,
					  PM3Config2D_UseConstantSource |
					  PM3Config2D_ForegroundROPEnable |
					  (PM3Config2D_ForegroundROP(0x3)) |	/* Ox3 is GXcopy */
					  PM3Config2D_FBWriteEnable);

		PM3_WRITE_REG(PM3ForegroundColor, c);

		PM3_WRITE_REG(PM3RectanglePosition,
			      (PM3RectanglePosition_XOffset(sx)) |
			      (PM3RectanglePosition_YOffset(sy)));

		PM3_WRITE_REG(PM3Render2D,
			      PM3Render2D_XPositive |
			      PM3Render2D_YPositive |
			      PM3Render2D_Operation_Normal |
			      PM3Render2D_SpanOperation |
			      (PM3Render2D_Width(width)) |
			      (PM3Render2D_Height(height)));
	} else {
		PM3_WAIT(8);

		PM3_WRITE_REG(PM3FBBlockColor, c);

		PM3_WRITE_REG(PM3PixelSize, PM3PixelSize_GLOBAL_16BIT);

		PM3_WRITE_REG(PM3FBWriteBufferWidth0,
			      PM3FBWriteBufferWidth_Width(l_fb_info->
							  current_par->
							  width << 1));

		PM3_WRITE_REG(PM3Config2D,
					  PM3Config2D_UseConstantSource |
					  PM3Config2D_ForegroundROPEnable |
					  (PM3Config2D_ForegroundROP(0x3)) |	/* Ox3 is GXcopy */
					  PM3Config2D_FBWriteEnable);

		PM3_WRITE_REG(PM3RectanglePosition,
			      (PM3RectanglePosition_XOffset(sx << 1)) |
			      (PM3RectanglePosition_YOffset(sy)));

		PM3_WRITE_REG(PM3Render2D,
			      PM3Render2D_XPositive |
			      PM3Render2D_YPositive |
			      PM3Render2D_Operation_Normal |
			      (PM3Render2D_Width(width << 1)) |
			      (PM3Render2D_Height(height)));

		PM3_WRITE_REG(PM3FBWriteBufferWidth0,
			      PM3FBWriteBufferWidth_Width(l_fb_info->
							  current_par->
							  width));

		PM3_WRITE_REG(PM3PixelSize, PM3PixelSize_GLOBAL_32BIT);
	}

	pm3fb_wait_pm3(l_fb_info);
}

static void pm3fb_cfb32_clear_margins(struct vc_data *conp,
				      struct display *p, int bottom_only)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) p->fb_info;
	int sx, sy;
	u32 c;

	DTRACE;

	sx = conp->vc_cols * fontwidth(p);	/* right margin */
	sy = conp->vc_rows * fontheight(p);	/* bottom margin */
	c = ((u32 *) p->dispsw_data)[attr_bgcol_ec(p, conp)];

	if (!bottom_only) {	/* right margin top->bottom */
		PM3_WAIT(4);

		PM3_WRITE_REG(PM3Config2D,
					  PM3Config2D_UseConstantSource |
					  PM3Config2D_ForegroundROPEnable |
					  (PM3Config2D_ForegroundROP(0x3)) |	/* Ox3 is GXcopy */
					  PM3Config2D_FBWriteEnable);

		PM3_WRITE_REG(PM3ForegroundColor, c);

		PM3_WRITE_REG(PM3RectanglePosition,
			      (PM3RectanglePosition_XOffset
			       (p->var.xoffset +
				sx)) | (PM3RectanglePosition_YOffset(p->
								     var.
								     yoffset)));

		PM3_WRITE_REG(PM3Render2D,
			      PM3Render2D_XPositive |
			      PM3Render2D_YPositive |
			      PM3Render2D_Operation_Normal |
			      PM3Render2D_SpanOperation |
			      (PM3Render2D_Width(p->var.xres - sx)) |
			      (PM3Render2D_Height(p->var.yres)));
	}

	/* bottom margin left -> right */
	PM3_WAIT(4);

	PM3_WRITE_REG(PM3Config2D,
				  PM3Config2D_UseConstantSource |
				  PM3Config2D_ForegroundROPEnable |
				  (PM3Config2D_ForegroundROP(0x3)) |	/* Ox3 is GXcopy */
				  PM3Config2D_FBWriteEnable);

	PM3_WRITE_REG(PM3ForegroundColor, c);

	PM3_WRITE_REG(PM3RectanglePosition,
		      (PM3RectanglePosition_XOffset(p->var.xoffset)) |
		      (PM3RectanglePosition_YOffset(p->var.yoffset + sy)));

	PM3_WRITE_REG(PM3Render2D,
		      PM3Render2D_XPositive |
		      PM3Render2D_YPositive |
		      PM3Render2D_Operation_Normal |
		      PM3Render2D_SpanOperation |
		      (PM3Render2D_Width(p->var.xres)) |
		      (PM3Render2D_Height(p->var.yres - sy)));

	pm3fb_wait_pm3(l_fb_info);
}
#endif /* FBCON_HAS_CFB32 */
#ifdef FBCON_HAS_CFB16
static void pm3fb_cfb16_clear(struct vc_data *conp,
			      struct display *p,
			      int sy, int sx, int height, int width)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) p->fb_info;
	u32 c;

	DTRACE;

	sx = sx * fontwidth(p);
	width = width * fontwidth(p);
	sy = sy * fontheight(p);
	height = height * fontheight(p);
	c = ((u16 *) p->dispsw_data)[attr_bgcol_ec(p, conp)];
	c = c | (c << 16);

	PM3_WAIT(4);

	if (l_fb_info->memt.caps & PM3LocalMemCaps_NoWriteMask)
		PM3_WRITE_REG(PM3ForegroundColor, c);
	else
		PM3_WRITE_REG(PM3FBBlockColor, c);

	PM3_WRITE_REG(PM3Config2D,
				  PM3Config2D_UseConstantSource |
				  PM3Config2D_ForegroundROPEnable |
				  (PM3Config2D_ForegroundROP(0x3)) |	/* Ox3 is GXcopy */
				  PM3Config2D_FBWriteEnable);

	PM3_WRITE_REG(PM3RectanglePosition,
		      (PM3RectanglePosition_XOffset(sx)) |
		      (PM3RectanglePosition_YOffset(sy)));
	
	if (l_fb_info->memt.caps & PM3LocalMemCaps_NoWriteMask)
		PM3_WRITE_REG(PM3Render2D,
			      PM3Render2D_XPositive |
			      PM3Render2D_YPositive |
			      PM3Render2D_Operation_Normal |
			      PM3Render2D_SpanOperation |
			      (PM3Render2D_Width(width)) |
			      (PM3Render2D_Height(height)));
	else
		PM3_WRITE_REG(PM3Render2D,
			      PM3Render2D_XPositive |
			      PM3Render2D_YPositive |
			      PM3Render2D_Operation_Normal |
			      (PM3Render2D_Width(width)) |
			      (PM3Render2D_Height(height)));
	
	pm3fb_wait_pm3(l_fb_info);
}

static void pm3fb_cfb16_clear_margins(struct vc_data *conp,
				      struct display *p, int bottom_only)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) p->fb_info;
	int sx, sy;
	u32 c;

	DTRACE;

	sx = conp->vc_cols * fontwidth(p);	/* right margin */
	sy = conp->vc_rows * fontheight(p);	/* bottom margin */
	c = ((u16 *) p->dispsw_data)[attr_bgcol_ec(p, conp)];
	c = c | (c << 16);

	if (!bottom_only) {	/* right margin top->bottom */
		PM3_WAIT(4);

		PM3_WRITE_REG(PM3Config2D,
					  PM3Config2D_UseConstantSource |
					  PM3Config2D_ForegroundROPEnable |
					  (PM3Config2D_ForegroundROP(0x3)) |	/* Ox3 is GXcopy */
					  PM3Config2D_FBWriteEnable);
		
		if (l_fb_info->memt.caps & PM3LocalMemCaps_NoWriteMask)
			PM3_WRITE_REG(PM3ForegroundColor, c);
		else
			PM3_WRITE_REG(PM3FBBlockColor, c);
		
		PM3_WRITE_REG(PM3RectanglePosition,
			      (PM3RectanglePosition_XOffset
			       (p->var.xoffset +
				sx)) | (PM3RectanglePosition_YOffset(p->
								     var.
								     yoffset)));
		if (l_fb_info->memt.caps & PM3LocalMemCaps_NoWriteMask)
			PM3_WRITE_REG(PM3Render2D,
				      PM3Render2D_XPositive |
				      PM3Render2D_YPositive |
				      PM3Render2D_Operation_Normal |
				      PM3Render2D_SpanOperation |
				      (PM3Render2D_Width(p->var.xres - sx)) |
				      (PM3Render2D_Height(p->var.yres)));
		else
			PM3_WRITE_REG(PM3Render2D,
				      PM3Render2D_XPositive |
				      PM3Render2D_YPositive |
				      PM3Render2D_Operation_Normal |
				      (PM3Render2D_Width(p->var.xres - sx)) |
				      (PM3Render2D_Height(p->var.yres)));
	}
	
	/* bottom margin left -> right */
	PM3_WAIT(4);
	
	PM3_WRITE_REG(PM3Config2D,
		      PM3Config2D_UseConstantSource |
		      PM3Config2D_ForegroundROPEnable |
		      (PM3Config2D_ForegroundROP(0x3)) |	/* Ox3 is GXcopy */
		      PM3Config2D_FBWriteEnable);
	
	if (l_fb_info->memt.caps & PM3LocalMemCaps_NoWriteMask)
		PM3_WRITE_REG(PM3ForegroundColor, c);
	else
		PM3_WRITE_REG(PM3FBBlockColor, c);
	
	
	PM3_WRITE_REG(PM3RectanglePosition,
		      (PM3RectanglePosition_XOffset(p->var.xoffset)) |
		      (PM3RectanglePosition_YOffset(p->var.yoffset + sy)));
	
	if (l_fb_info->memt.caps & PM3LocalMemCaps_NoWriteMask)
		PM3_WRITE_REG(PM3Render2D,
			      PM3Render2D_XPositive |
			      PM3Render2D_YPositive |
			      PM3Render2D_Operation_Normal |
			      PM3Render2D_SpanOperation |
			      (PM3Render2D_Width(p->var.xres)) |
			      (PM3Render2D_Height(p->var.yres - sy)));
	else
		PM3_WRITE_REG(PM3Render2D,
			      PM3Render2D_XPositive |
			      PM3Render2D_YPositive |
			      PM3Render2D_Operation_Normal |
			      (PM3Render2D_Width(p->var.xres)) |
			      (PM3Render2D_Height(p->var.yres - sy)));

	pm3fb_wait_pm3(l_fb_info);
}
#endif /* FBCON_HAS_CFB16 */
#ifdef FBCON_HAS_CFB8
static void pm3fb_cfb8_clear(struct vc_data *conp,
			     struct display *p,
			     int sy, int sx, int height, int width)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) p->fb_info;
	u32 c;

	DTRACE;

	sx = sx * fontwidth(p);
	width = width * fontwidth(p);
	sy = sy * fontheight(p);
	height = height * fontheight(p);

	c = attr_bgcol_ec(p, conp);
	c |= c << 8;
	c |= c << 16;

	PM3_WAIT(4);

	PM3_WRITE_REG(PM3Config2D,
				  PM3Config2D_UseConstantSource |
				  PM3Config2D_ForegroundROPEnable |
				  (PM3Config2D_ForegroundROP(0x3)) |	/* Ox3 is GXcopy */
				  PM3Config2D_FBWriteEnable);

	PM3_WRITE_REG(PM3ForegroundColor, c);

	PM3_WRITE_REG(PM3RectanglePosition,
		      (PM3RectanglePosition_XOffset(sx)) |
		      (PM3RectanglePosition_YOffset(sy)));

	PM3_WRITE_REG(PM3Render2D,
		      PM3Render2D_XPositive |
		      PM3Render2D_YPositive |
		      PM3Render2D_Operation_Normal |
		      PM3Render2D_SpanOperation |
		      (PM3Render2D_Width(width)) |
		      (PM3Render2D_Height(height)));

	pm3fb_wait_pm3(l_fb_info);
}

static void pm3fb_cfb8_clear_margins(struct vc_data *conp,
				     struct display *p, int bottom_only)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) p->fb_info;
	int sx, sy;
	u32 c;

	DTRACE;

	sx = conp->vc_cols * fontwidth(p);	/* right margin */
	sy = conp->vc_rows * fontheight(p);	/* bottom margin */
	c = attr_bgcol_ec(p, conp);
	c |= c << 8;
	c |= c << 16;

	if (!bottom_only) {	/* right margin top->bottom */
		PM3_WAIT(4);

		PM3_WRITE_REG(PM3Config2D,
					  PM3Config2D_UseConstantSource |
					  PM3Config2D_ForegroundROPEnable |
					  (PM3Config2D_ForegroundROP(0x3)) |	/* Ox3 is GXcopy */
					  PM3Config2D_FBWriteEnable);

		PM3_WRITE_REG(PM3ForegroundColor, c);

		PM3_WRITE_REG(PM3RectanglePosition,
			      (PM3RectanglePosition_XOffset
			       (p->var.xoffset +
				sx)) | (PM3RectanglePosition_YOffset(p->
								     var.
								     yoffset)));

		PM3_WRITE_REG(PM3Render2D,
			      PM3Render2D_XPositive |
			      PM3Render2D_YPositive |
			      PM3Render2D_Operation_Normal |
			      PM3Render2D_SpanOperation |
			      (PM3Render2D_Width(p->var.xres - sx)) |
			      (PM3Render2D_Height(p->var.yres)));
	}

	/* bottom margin left -> right */
	PM3_WAIT(4);

	PM3_WRITE_REG(PM3Config2D,
				  PM3Config2D_UseConstantSource |
				  PM3Config2D_ForegroundROPEnable |
				  (PM3Config2D_ForegroundROP(0x3)) |	/* Ox3 is GXcopy */
				  PM3Config2D_FBWriteEnable);

	PM3_WRITE_REG(PM3ForegroundColor, c);

	PM3_WRITE_REG(PM3RectanglePosition,
		      (PM3RectanglePosition_XOffset(p->var.xoffset)) |
		      (PM3RectanglePosition_YOffset(p->var.yoffset + sy)));

	PM3_WRITE_REG(PM3Render2D,
		      PM3Render2D_XPositive |
		      PM3Render2D_YPositive |
		      PM3Render2D_Operation_Normal |
		      PM3Render2D_SpanOperation |
		      (PM3Render2D_Width(p->var.xres)) |
		      (PM3Render2D_Height(p->var.yres - sy)));

	pm3fb_wait_pm3(l_fb_info);
}
#endif /* FBCON_HAS_CFB8 */
#if defined(FBCON_HAS_CFB8) || defined(FBCON_HAS_CFB16) || defined(FBCON_HAS_CFB32)
static void pm3fb_cfbX_bmove(struct display *p,
			     int sy, int sx,
			     int dy, int dx, int height, int width)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) p->fb_info;
	int x_align, o_x, o_y;

	DTRACE;

	sx = sx * fontwidth(p);
	dx = dx * fontwidth(p);
	width = width * fontwidth(p);
	sy = sy * fontheight(p);
	dy = dy * fontheight(p);
	height = height * fontheight(p);

	o_x = sx - dx;		/*(sx > dx ) ? (sx - dx) : (dx - sx); */
	o_y = sy - dy;		/*(sy > dy ) ? (sy - dy) : (dy - sy); */

	x_align = (sx & 0x1f);

	PM3_WAIT(6);

	PM3_WRITE_REG(PM3Config2D,
				  PM3Config2D_UserScissorEnable |
				  PM3Config2D_ForegroundROPEnable |
				  PM3Config2D_Blocking |
				  (PM3Config2D_ForegroundROP(0x3)) |	/* Ox3 is GXcopy */
				  PM3Config2D_FBWriteEnable);

	PM3_WRITE_REG(PM3ScissorMinXY,
		      ((dy & 0x0fff) << 16) | (dx & 0x0fff));
	PM3_WRITE_REG(PM3ScissorMaxXY,
				  (((dy + height) & 0x0fff) << 16) |
				  ((dx + width) & 0x0fff));

	PM3_WRITE_REG(PM3FBSourceReadBufferOffset,
		      PM3FBSourceReadBufferOffset_XOffset(o_x) |
		      PM3FBSourceReadBufferOffset_YOffset(o_y));

	PM3_WRITE_REG(PM3RectanglePosition,
		      (PM3RectanglePosition_XOffset(dx - x_align)) |
		      (PM3RectanglePosition_YOffset(dy)));

	PM3_WRITE_REG(PM3Render2D,
		      ((sx > dx) ? PM3Render2D_XPositive : 0) |
		      ((sy > dy) ? PM3Render2D_YPositive : 0) |
		      PM3Render2D_Operation_Normal |
		      PM3Render2D_SpanOperation |
		      PM3Render2D_FBSourceReadEnable |
		      (PM3Render2D_Width(width + x_align)) |
		      (PM3Render2D_Height(height)));

	pm3fb_wait_pm3(l_fb_info);
}

static void pm3fb_cfbX_putc(struct vc_data *conp, struct display *p,
			    int c, int yy, int xx)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) p->fb_info;
	u8 *cdat, asx = 0, asy = 0, o_x = 0, o_y = 0;
	u32 fgx, bgx, ldat;
	int sx, sy, i;

	DTRACE;

	if (l_fb_info->current_par->depth == 8)
		fgx = attr_fgcol(p, c);
	else if (depth2bpp(l_fb_info->current_par->depth) == 16)
		fgx = ((u16 *) p->dispsw_data)[attr_fgcol(p, c)];
	else
		fgx = ((u32 *) p->dispsw_data)[attr_fgcol(p, c)];

	PM3_COLOR(fgx);

	if (l_fb_info->current_par->depth == 8)
		bgx = attr_bgcol(p, c);
	else if (depth2bpp(l_fb_info->current_par->depth) == 16)
		bgx = ((u16 *) p->dispsw_data)[attr_bgcol(p, c)];
	else
		bgx = ((u32 *) p->dispsw_data)[attr_bgcol(p, c)];

	PM3_COLOR(bgx);

	PM3_WAIT(4);

	PM3_WRITE_REG(PM3Config2D,
				  PM3Config2D_UseConstantSource |
				  PM3Config2D_ForegroundROPEnable |
				  (PM3Config2D_ForegroundROP(0x3)) |	/* Ox3 is GXcopy */
				  PM3Config2D_FBWriteEnable | PM3Config2D_OpaqueSpan);

	PM3_WRITE_REG(PM3ForegroundColor, fgx);
	PM3_WRITE_REG(PM3FillBackgroundColor, bgx);

	/* WARNING : address select X need to specify 8 bits for fontwidth <= 8 */
	/* and 16 bits for fontwidth <= 16 */
	/* same in _putcs, same for Y and fontheight */
	if (fontwidth(p) <= 8)
		asx = 2;
	else if (fontwidth(p) <= 16)
		asx = 3;	/* look OK */
	if (fontheight(p) <= 8)
		asy = 2;
	else if (fontheight(p) <= 16)
		asy = 3;	/* look OK */
	else if (fontheight(p) <= 32)
		asy = 4;	/* look OK */

	sx = xx * fontwidth(p);
	sy = yy * fontheight(p);

	if (fontwidth(p) <= 8)
		o_x = (8 - (sx & 0x7)) & 0x7;
	else if (fontwidth(p) <= 16)
		o_x = (16 - (sx & 0xF)) & 0xF;
	if (fontheight(p) <= 8)
		o_y = (8 - (sy & 0x7)) & 0x7;
	else if (fontheight(p) <= 16)
		o_y = (16 - (sy & 0xF)) & 0xF;
	else if (fontheight(p) <= 32)
		o_y = (32 - (sy & 0x1F)) & 0x1F;

	PM3_WRITE_REG(PM3AreaStippleMode, (o_x << 7) | (o_y << 12) |	/* x_offset, y_offset in pattern */
		      (1 << 18) |	/* BE */
		      1 | (asx << 1) | (asy << 4) |	/* address select x/y */
		      (1 << 20));	/* OpaqueSpan */

	if (fontwidth(p) <= 8) {
		cdat = p->fontdata + (c & p->charmask) * fontheight(p);
	} else {
		cdat =
		    p->fontdata +
		    ((c & p->charmask) * (fontheight(p) << 1));
	}

	PM3_WAIT(2 + fontheight(p));

	for (i = 0; i < fontheight(p); i++) {	/* assume fontheight <= 32 */
		if (fontwidth(p) <= 8) {
			ldat = *cdat++;
		} else {	/* assume fontwidth <= 16 ATM */

			ldat = ((*cdat++) << 8);
			ldat |= *cdat++;
		}
		PM3_WRITE_REG(AreaStipplePattern_indexed(i), ldat);
	}

	PM3_WRITE_REG(PM3RectanglePosition,
		      (PM3RectanglePosition_XOffset(sx)) |
		      (PM3RectanglePosition_YOffset(sy)));

	PM3_WRITE_REG(PM3Render2D,
		      PM3Render2D_AreaStippleEnable |
		      PM3Render2D_XPositive |
		      PM3Render2D_YPositive |
		      PM3Render2D_Operation_Normal |
		      PM3Render2D_SpanOperation |
		      (PM3Render2D_Width(fontwidth(p))) |
		      (PM3Render2D_Height(fontheight(p))));

	pm3fb_wait_pm3(l_fb_info);
}

static void pm3fb_cfbX_putcs(struct vc_data *conp, struct display *p,
			     const unsigned short *s, int count, int yy,
			     int xx)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) p->fb_info;
	u8 *cdat, asx = 0, asy = 0, o_x = 0, o_y = 0;
	u32 fgx, bgx, ldat;
	int sx, sy, i, j;
	u16 sc;

	DTRACE;

	sc = scr_readw(s);
	if (l_fb_info->current_par->depth == 8)
		fgx = attr_fgcol(p, sc);
	else if (depth2bpp(l_fb_info->current_par->depth) == 16)
		fgx = ((u16 *) p->dispsw_data)[attr_fgcol(p, sc)];
	else
		fgx = ((u32 *) p->dispsw_data)[attr_fgcol(p, sc)];
	
	PM3_COLOR(fgx);
	
	if (l_fb_info->current_par->depth == 8)
		bgx = attr_bgcol(p, sc);
	else if (depth2bpp(l_fb_info->current_par->depth) == 16)
		bgx = ((u16 *) p->dispsw_data)[attr_bgcol(p, sc)];
	else
		bgx = ((u32 *) p->dispsw_data)[attr_bgcol(p, sc)];
	
	PM3_COLOR(bgx);

	PM3_WAIT(4);

	PM3_WRITE_REG(PM3Config2D,
				  PM3Config2D_UseConstantSource |
				  PM3Config2D_ForegroundROPEnable |
				  (PM3Config2D_ForegroundROP(0x3)) |	/* Ox3 is GXcopy */
				  PM3Config2D_FBWriteEnable |
				  PM3Config2D_OpaqueSpan);

	PM3_WRITE_REG(PM3ForegroundColor, fgx);
	PM3_WRITE_REG(PM3FillBackgroundColor, bgx);

	/* WARNING : address select X need to specify 8 bits for fontwidth <= 8 */
	/* and 16 bits for fontwidth <= 16 */
	/* same in _putc, same for Y and fontheight */
	if (fontwidth(p) <= 8)
		asx = 2;
	else if (fontwidth(p) <= 16)
		asx = 3;	/* look OK */
	if (fontheight(p) <= 8)
		asy = 2;
	else if (fontheight(p) <= 16)
		asy = 3;	/* look OK */
	else if (fontheight(p) <= 32)
		asy = 4;	/* look OK */

	sy = yy * fontheight(p);

	if (fontheight(p) <= 8)
		o_y = (8 - (sy & 0x7)) & 0x7;
	else if (fontheight(p) <= 16)
		o_y = (16 - (sy & 0xF)) & 0xF;
	else if (fontheight(p) <= 32)
		o_y = (32 - (sy & 0x1F)) & 0x1F;

	for (j = 0; j < count; j++) {
		sc = scr_readw(s + j);
		if (fontwidth(p) <= 8)
			cdat = p->fontdata +
				(sc & p->charmask) * fontheight(p);
		else
			cdat = p->fontdata +
				((sc & p->charmask) * fontheight(p) << 1);
		
		sx = (xx + j) * fontwidth(p);

		if (fontwidth(p) <= 8)
			o_x = (8 - (sx & 0x7)) & 0x7;
		else if (fontwidth(p) <= 16)
			o_x = (16 - (sx & 0xF)) & 0xF;

		PM3_WAIT(3 + fontheight(p));

		PM3_WRITE_REG(PM3AreaStippleMode, (o_x << 7) | (o_y << 12) | /* x_offset, y_offset in pattern */
			      (1 << 18) | /* BE */
			      1 | (asx << 1) | (asy << 4) | /* address select x/y */
			      (1 << 20)); /* OpaqueSpan */

		for (i = 0; i < fontheight(p); i++) { /* assume fontheight <= 32 */
			if (fontwidth(p) <= 8) {
				ldat = *cdat++;
			} else { /* assume fontwidth <= 16 ATM */
				ldat = ((*cdat++) << 8);
				ldat |= *cdat++;
			}
			PM3_WRITE_REG(AreaStipplePattern_indexed(i), ldat);
		}

		PM3_WRITE_REG(PM3RectanglePosition,
			      (PM3RectanglePosition_XOffset(sx)) |
			      (PM3RectanglePosition_YOffset(sy)));

		PM3_WRITE_REG(PM3Render2D,
			      PM3Render2D_AreaStippleEnable |
			      PM3Render2D_XPositive |
			      PM3Render2D_YPositive |
			      PM3Render2D_Operation_Normal |
			      PM3Render2D_SpanOperation |
			      (PM3Render2D_Width(fontwidth(p))) |
			      (PM3Render2D_Height(fontheight(p))));
	}
	pm3fb_wait_pm3(l_fb_info);
}

static void pm3fb_cfbX_revc(struct display *p, int xx, int yy)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) p->fb_info;

	xx = xx * fontwidth(p);
	yy = yy * fontheight(p);

	if (l_fb_info->current_par->depth == 8)
	{
		if (l_fb_info->memt.caps & PM3LocalMemCaps_NoWriteMask)
			PM3_SLOW_WRITE_REG(PM3FBSoftwareWriteMask, 0x0F0F0F0F);
		else
			PM3_SLOW_WRITE_REG(PM3FBHardwareWriteMask, 0x0F0F0F0F);
	}

	PM3_WAIT(3);

	PM3_WRITE_REG(PM3Config2D,
				  PM3Config2D_UseConstantSource |
				  PM3Config2D_ForegroundROPEnable |
				  (PM3Config2D_ForegroundROP(0xa)) |	/* Oxa is GXinvert */
				  PM3Config2D_FBDestReadEnable |
				  PM3Config2D_FBWriteEnable);

	PM3_WRITE_REG(PM3RectanglePosition,
		      (PM3RectanglePosition_XOffset(xx)) |
		      (PM3RectanglePosition_YOffset(yy)));

	PM3_WRITE_REG(PM3Render2D,
		      PM3Render2D_XPositive |
		      PM3Render2D_YPositive |
		      PM3Render2D_Operation_Normal |
		      PM3Render2D_SpanOperation |
		      (PM3Render2D_Width(fontwidth(p))) |
		      (PM3Render2D_Height(fontheight(p))));

	pm3fb_wait_pm3(l_fb_info);

	if (l_fb_info->current_par->depth == 8)
	{
		if (l_fb_info->memt.caps & PM3LocalMemCaps_NoWriteMask)
			PM3_SLOW_WRITE_REG(PM3FBSoftwareWriteMask, 0xFFFFFFFF);
		else
			PM3_SLOW_WRITE_REG(PM3FBHardwareWriteMask, 0xFFFFFFFF);
	}
}

#endif /* FBCON_HAS_CFB8 || FBCON_HAS_CFB16 || FBCON_HAS_CFB32 */
#endif /* PM3FB_USE_ACCEL */
/* *********************************** */
/* ***** pre-init board(s) setup ***** */
/* *********************************** */

static void pm3fb_mode_setup(char *mode, unsigned long board_num)
{
	struct pm3fb_info *l_fb_info = &(fb_info[board_num]);
	struct pm3fb_par *l_fb_par = &(current_par[board_num]);
	unsigned long i = 0;

	current_par_valid[board_num] = 0;

	if (!strncmp(mode, "current", 7)) {
		l_fb_info->use_current = 1;	/* default w/ OpenFirmware */
	} else {
		while ((mode_base[i].name[0])
		       && (!current_par_valid[board_num])) {
			if (!
			    (strncmp
			     (mode, mode_base[i].name,
			      strlen(mode_base[i].name)))) {
				memcpy(l_fb_par, &(mode_base[i].user_mode),
				       sizeof(struct pm3fb_par));
				current_par_valid[board_num] = 1;
				DPRINTK(2, "Mode set to %s\n",
					mode_base[i].name);
			}
			i++;
		}
		DASSERT(current_par_valid[board_num],
			"Valid mode on command line\n");
	}
}

static void pm3fb_pciid_setup(char *pciid, unsigned long board_num)
{
	short l_bus = -1, l_slot = -1, l_func = -1;
	char *next;

	if (pciid) {
		l_bus = simple_strtoul(pciid, &next, 10);
		if (next && (next[0] == ':')) {
			pciid = next + 1;
			l_slot = simple_strtoul(pciid, &next, 10);
			if (next && (next[0] == ':')) {
				pciid = next + 1;
				l_func =
				    simple_strtoul(pciid, (char **) NULL,
						   10);
			}
		}
	} else
		return;

	if ((l_bus >= 0) && (l_slot >= 0) && (l_func >= 0)) {
		bus[board_num] = l_bus;
		slot[board_num] = l_slot;
		func[board_num] = l_func;
		DPRINTK(2, "Board #%ld will be PciId: %hd:%hd:%hd\n",
			board_num, l_bus, l_slot, l_func);
	} else {
		DPRINTK(1, "Invalid PciId: %hd:%hd:%hd for board #%ld\n",
			l_bus, l_slot, l_func, board_num);
	}
}

static void pm3fb_font_setup(char *lf, unsigned long board_num)
{
	unsigned long lfs = strlen(lf);

	if (lfs > (PM3_FONTNAME_SIZE - 1)) {
		DPRINTK(1, "Fontname %s too long\n", lf);
		return;
	}
	strlcpy(fontn[board_num], lf, lfs + 1);
}

static void pm3fb_bootdepth_setup(char *bds, unsigned long board_num)
{
	unsigned long bd = simple_strtoul(bds, (char **) NULL, 10);

	if (!(depth_supported(bd))) {
		printk(KERN_WARNING "pm3fb: ignoring invalid depth %s for board #%ld\n",
		       bds, board_num);
		return;
	}
	depth[board_num] = bd;
}

static void pm3fb_forcesize_setup(char *bds, unsigned long board_num)
{
	unsigned long bd = simple_strtoul(bds, (char **) NULL, 10);

	if (bd > 64) {
		printk(KERN_WARNING "pm3fb: ignoring invalid memory size %s for board #%ld\n",
		       bds, board_num);
		return;
	}
	forcesize[board_num] = bd;
}

static char *pm3fb_boardnum_setup(char *options, unsigned long *bn)
{
	char *next;

	if (!(isdigit(options[0]))) {
		(*bn) = 0;
		return (options);
	}

	(*bn) = simple_strtoul(options, &next, 10);

	if (next && (next[0] == ':') && ((*bn) >= 0)
	    && ((*bn) <= PM3_MAX_BOARD)) {
		DPRINTK(2, "Board_num seen as %ld\n", (*bn));
		return (next + 1);
	} else {
		(*bn) = 0;
		DPRINTK(2, "Board_num default to %ld\n", (*bn));
		return (options);
	}
}

static void pm3fb_real_setup(char *options)
{
	char *next;
	unsigned long i, bn;
	struct pm3fb_info *l_fb_info;

	DTRACE;

	DPRINTK(2, "Options : %s\n", options);

	for (i = 0; i < PM3_MAX_BOARD; i++) {
		l_fb_info = &(fb_info[i]);
		memset(l_fb_info, 0, sizeof(struct pm3fb_info));
		l_fb_info->gen.fbhw = &pm3fb_switch;
		l_fb_info->board_num = i;
		current_par_valid[i] = 0;
		slot[i] = -1;
		func[i] = -1;
		bus[i] = -1;
		disable[i] = 0;
		noaccel[i] = 0;
		fontn[i][0] = '\0';
		depth[i] = 0;
		l_fb_info->current_par = &(current_par[i]);
	}

	/* eat up prefix pm3fb and whatever is used as separator i.e. :,= */
	if (!strncmp(options, "pm3fb", 5)) {
		options += 5;
		while (((*options) == ',') || ((*options) == ':')
		       || ((*options) == '='))
			options++;
	}

	while (options) {
		bn = 0;
		if ((next = strchr(options, ','))) {
			(*next) = '\0';
			next++;
		}

		if (!strncmp(options, "mode:", 5)) {
			options = pm3fb_boardnum_setup(options + 5, &bn);
			DPRINTK(2, "Setting mode for board #%ld\n", bn);
			pm3fb_mode_setup(options, bn);
		} else if (!strncmp(options, "off:", 4)) {
			options = pm3fb_boardnum_setup(options + 4, &bn);
			DPRINTK(2, "Disabling board #%ld\n", bn);
			disable[bn] = 1;
		} else if (!strncmp(options, "off", 3)) {	/* disable everything */
			for (i = 0; i < PM3_MAX_BOARD; i++)
				disable[i] = 1;
		} else if (!strncmp(options, "disable:", 8)) {
			options = pm3fb_boardnum_setup(options + 8, &bn);
			DPRINTK(2, "Disabling board #%ld\n", bn);
			disable[bn] = 1;
		} else if (!strncmp(options, "pciid:", 6)) {
			options = pm3fb_boardnum_setup(options + 6, &bn);
			DPRINTK(2, "Setting PciID for board #%ld\n", bn);
			pm3fb_pciid_setup(options, bn);
		} else if (!strncmp(options, "noaccel:", 8)) {
			options = pm3fb_boardnum_setup(options + 8, &bn);
			noaccel[bn] = 1;
		} else if (!strncmp(options, "font:", 5)) {
			options = pm3fb_boardnum_setup(options + 5, &bn);
			pm3fb_font_setup(options, bn);
		} else if (!strncmp(options, "depth:", 6)) {
			options = pm3fb_boardnum_setup(options + 6, &bn);
			pm3fb_bootdepth_setup(options, bn);
		} else if (!strncmp(options, "printtimings", 12)) {
			printtimings = 1;
		} else if (!strncmp(options, "flatpanel:", 10)) {
			options = pm3fb_boardnum_setup(options + 10, &bn);
			flatpanel[bn] = 1;
		} else if (!strncmp(options, "forcesize:", 10)) {
			options = pm3fb_boardnum_setup(options + 10, &bn);
			pm3fb_forcesize_setup(options, bn);
		}
		options = next;
	}
}

/* ********************************************** */
/* ***** framebuffer API standard functions ***** */
/* ********************************************** */

static int pm3fb_encode_fix(struct fb_fix_screeninfo *fix,
			    const void *par, struct fb_info_gen *info)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) info;
	struct pm3fb_par *p = (struct pm3fb_par *) par;

	DTRACE;

	strcpy(fix->id, permedia3_name);
	fix->smem_start = (unsigned long)l_fb_info->p_fb;
	fix->smem_len = l_fb_info->fb_size;
	fix->mmio_start = (unsigned long)l_fb_info->pIOBase;
	fix->mmio_len = PM3_REGS_SIZE;
#ifdef PM3FB_USE_ACCEL
	if (!(noaccel[l_fb_info->board_num]))
		fix->accel = FB_ACCEL_3DLABS_PERMEDIA3;
	else
#endif /* PM3FB_USE_ACCEL */
		fix->accel = FB_ACCEL_NONE;
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->visual =
	    (p->depth == 8) ? FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_TRUECOLOR;
	if (current_par_valid[l_fb_info->board_num])
		fix->line_length =
			l_fb_info->current_par->width *
			depth2ByPP(l_fb_info->current_par->depth);
	else
		fix->line_length = 0;
	fix->xpanstep = 64 / depth2bpp(p->depth);
	fix->ypanstep = 1;
	fix->ywrapstep = 0;
	return (0);
}

static int pm3fb_decode_var(const struct fb_var_screeninfo *var,
			    void *par, struct fb_info_gen *info)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) info;
	struct pm3fb_par *p = (struct pm3fb_par *) par;
	struct pm3fb_par temp_p;
	u32 xres;

	DTRACE;

	DASSERT((var != NULL), "fb_var_screeninfo* not NULL");
	DASSERT((p != NULL), "pm3fb_par* not NULL");
	DASSERT((l_fb_info != NULL), "pm3fb_info* not NULL");

	memset(&temp_p, 0, sizeof(struct pm3fb_par));
	temp_p.width = (var->xres_virtual + 7) & ~7;
	temp_p.height = var->yres_virtual;

	if (!(depth_supported(var->bits_per_pixel))) /* round unsupported up to a multiple of 8 */
		temp_p.depth = depth2bpp(var->bits_per_pixel);
	else
		temp_p.depth = var->bits_per_pixel;

	temp_p.depth = (temp_p.depth > 32) ? 32 : temp_p.depth; /* max 32 */
	temp_p.depth = (temp_p.depth == 24) ? 32 : temp_p.depth; /* 24 unsupported, round-up to 32 */

	if ((temp_p.depth == 16) && (var->red.length == 5) && (var->green.length == 5) && (var->blue.length == 5))
		temp_p.depth = 15; /* RGBA 5551 is stored as depth 15 */

	if ((temp_p.depth == 16) && (var->red.length == 4) && (var->green.length == 4) && (var->blue.length == 4))
		temp_p.depth = 12; /* RGBA 4444  is stored as depth 12 */


	DPRINTK(2,
		"xres: %d, yres: %d, vxres: %d, vyres: %d ; xoffset:%d, yoffset: %d\n",
		var->xres, var->yres, var->xres_virtual, var->yres_virtual,
		var->xoffset, var->yoffset);

	xres = (var->xres + 31) & ~31;
	if (temp_p.width < xres + var->xoffset)
		temp_p.width = xres + var->xoffset;
	if (temp_p.height < var->yres + var->yoffset)
		temp_p.height = var->yres + var->yoffset;

	if (temp_p.width > 2048) {
		DPRINTK(1, "virtual width not supported: %u\n",
			temp_p.width);
		return (-EINVAL);
	}
	if (var->yres < 200) {
		DPRINTK(1, "height not supported: %u\n", (u32) var->yres);
		return (-EINVAL);
	}
	if (temp_p.height < 200 || temp_p.height > 4095) {
		DPRINTK(1, "virtual height not supported: %u\n",
			temp_p.height);
		return (-EINVAL);
	}
	if (!(depth_supported(temp_p.depth))) {
		DPRINTK(1, "depth not supported: %u\n", temp_p.depth);
		return (-EINVAL);
	}
	if ((temp_p.width * temp_p.height * depth2ByPP(temp_p.depth)) >
	    l_fb_info->fb_size) {
		DPRINTK(1, "no memory for screen (%ux%ux%u)\n",
			temp_p.width, temp_p.height, temp_p.depth);
		return (-EINVAL);
	}

	if ((!var->pixclock) ||
	    (!var->right_margin) ||
	    (!var->hsync_len) ||
	    (!var->left_margin) ||
	    (!var->lower_margin) ||
	    (!var->vsync_len) || (!var->upper_margin)
	    ) {
		unsigned long i = 0, done = 0;
		printk(KERN_WARNING "pm3fb: refusing to use a likely wrong timing\n");

		while ((mode_base[i].user_mode.width) && !done) {
			if ((mode_base[i].user_mode.width == temp_p.width)
			    && (mode_base[i].user_mode.height ==
				temp_p.height)) {
				printk(KERN_NOTICE "pm3fb: using close match %s\n",
				       mode_base[i].name);
				temp_p = mode_base[i].user_mode;
				done = 1;
			}
			i++;
		}
		if (!done)
			return (-EINVAL);
	} else {
		temp_p.pixclock = PICOS2KHZ(var->pixclock);
		if (temp_p.pixclock > PM3_MAX_PIXCLOCK) {
			DPRINTK(1, "pixclock too high (%uKHz)\n",
				temp_p.pixclock);
			return (-EINVAL);
		}

		temp_p.hsstart = var->right_margin;
		temp_p.hsend = var->right_margin + var->hsync_len;
		temp_p.hbend =
		    var->right_margin + var->hsync_len + var->left_margin;
		temp_p.htotal = xres + temp_p.hbend;

		temp_p.vsstart = var->lower_margin;
		temp_p.vsend = var->lower_margin + var->vsync_len;
		temp_p.vbend =
		    var->lower_margin + var->vsync_len + var->upper_margin;
		temp_p.vtotal = var->yres + temp_p.vbend;

		temp_p.stride = temp_p.width;

		DPRINTK(2, "Using %d * %d, %d Khz, stride is %08x\n",
			temp_p.width, temp_p.height, temp_p.pixclock,
			temp_p.stride);

		temp_p.base =
		    pm3fb_Shiftbpp(l_fb_info, temp_p.depth,
				   (var->yoffset * xres) + var->xoffset);

		temp_p.video = 0;

		if (var->sync & FB_SYNC_HOR_HIGH_ACT)
			temp_p.video |= PM3VideoControl_HSYNC_ACTIVE_HIGH;
		else
			temp_p.video |= PM3VideoControl_HSYNC_ACTIVE_LOW;

		if (var->sync & FB_SYNC_VERT_HIGH_ACT)
			temp_p.video |= PM3VideoControl_VSYNC_ACTIVE_HIGH;
		else
			temp_p.video |= PM3VideoControl_VSYNC_ACTIVE_LOW;

		if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
			DPRINTK(1, "Interlaced mode not supported\n\n");
			return (-EINVAL);
		}

		if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE)
			temp_p.video |= PM3VideoControl_LINE_DOUBLE_ON;
		else
			temp_p.video |= PM3VideoControl_LINE_DOUBLE_OFF;

		if (var->activate == FB_ACTIVATE_NOW)
			temp_p.video |= PM3VideoControl_ENABLE;
		else {
			temp_p.video |= PM3VideoControl_DISABLE;
			DPRINTK(2, "PM3Video disabled\n");
		}

		switch (temp_p.depth) {
		case 8:
			temp_p.video |= PM3VideoControl_PIXELSIZE_8BIT;
			break;
		case 12:
		case 15:
		case 16:
			temp_p.video |= PM3VideoControl_PIXELSIZE_16BIT;
			break;
		case 32:
			temp_p.video |= PM3VideoControl_PIXELSIZE_32BIT;
			break;
		default:
			DPRINTK(1, "Unsupported depth\n");
			break;
		}
	}
	(*p) = temp_p;

#ifdef PM3FB_USE_ACCEL
	if (var->accel_flags & FB_ACCELF_TEXT)
		noaccel[l_fb_info->board_num] = 0;
	else
		noaccel[l_fb_info->board_num] = 1;
#endif /* PM3FB_USE_ACCEL */

	return (0);
}

static void pm3fb_encode_depth(struct fb_var_screeninfo *var, long d)
{
	switch (d) {
	case 8:
		var->red.length = var->green.length = var->blue.length = 8;
		var->red.offset = var->green.offset = var->blue.offset = 0;
		var->transp.offset = var->transp.length = 0;
		break;

	case 12:
		var->red.offset = 8;
		var->red.length = 4;
		var->green.offset = 4;
		var->green.length = 4;
		var->blue.offset = 0;
		var->blue.length = 4;
		var->transp.offset = 12;
		var->transp.length = 4;
		break;

	case 15:
		var->red.offset = 10;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 5;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 15;
		var->transp.length = 1;
		break;

	case 16:
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = var->transp.length = 0;
		break;

	case 32:
		var->transp.offset = 24;
		var->red.offset = 16;
		var->green.offset = 8;
		var->blue.offset = 0;
		var->red.length = var->green.length =
			var->blue.length = var->transp.length = 8;
		break;

	default:
		DPRINTK(1, "Unsupported depth %ld\n", d);
		break;
	}
}

static int pm3fb_encode_var(struct fb_var_screeninfo *var,
			    const void *par, struct fb_info_gen *info)
{
	struct pm3fb_par *p = (struct pm3fb_par *) par;
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) info;

	u32 base;

	DTRACE;

	DASSERT((var != NULL), "fb_var_screeninfo* not NULL");
	DASSERT((p != NULL), "pm3fb_par* not NULL");
	DASSERT((info != NULL), "fb_info_gen* not NULL");

	memset(var, 0, sizeof(struct fb_var_screeninfo));

#ifdef PM3FB_USE_ACCEL
	if (!(noaccel[l_fb_info->board_num]))
		var->accel_flags |= FB_ACCELF_TEXT;
#endif /* PM3FB_USE_ACCEL */

	var->xres_virtual = p->width;
	var->yres_virtual = p->height;
	var->xres = p->htotal - p->hbend;
	var->yres = p->vtotal - p->vbend;

	DPRINTK(2, "xres = %d, yres : %d\n", var->xres, var->yres);

	var->right_margin = p->hsstart;
	var->hsync_len = p->hsend - p->hsstart;
	var->left_margin = p->hbend - p->hsend;
	var->lower_margin = p->vsstart;
	var->vsync_len = p->vsend - p->vsstart;
	var->upper_margin = p->vbend - p->vsend;
	var->bits_per_pixel = depth2bpp(p->depth);
	
	pm3fb_encode_depth(var, p->depth);

	base = pm3fb_Unshiftbpp(l_fb_info, p->depth, p->base);

	var->xoffset = base % var->xres;
	var->yoffset = base / var->xres;

	var->height = var->width = -1;

	var->pixclock = KHZ2PICOS(p->pixclock);

	if ((p->video & PM3VideoControl_HSYNC_MASK) ==
	    PM3VideoControl_HSYNC_ACTIVE_HIGH)
		var->sync |= FB_SYNC_HOR_HIGH_ACT;
	if ((p->video & PM3VideoControl_VSYNC_MASK) ==
	    PM3VideoControl_VSYNC_ACTIVE_HIGH)
		var->sync |= FB_SYNC_VERT_HIGH_ACT;
	if (p->video & PM3VideoControl_LINE_DOUBLE_ON)
		var->vmode = FB_VMODE_DOUBLE;

	return (0);
}

static void pm3fb_get_par(void *par, struct fb_info_gen *info)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) info;

	DTRACE;

	if (!current_par_valid[l_fb_info->board_num]) {
		if (l_fb_info->use_current)
			pm3fb_read_mode(l_fb_info, l_fb_info->current_par);
		else
			memcpy(l_fb_info->current_par,
			       &(mode_base[0].user_mode),
			       sizeof(struct pm3fb_par));
		current_par_valid[l_fb_info->board_num] = 1;
	}
	*((struct pm3fb_par *) par) = *(l_fb_info->current_par);
}

static void pm3fb_set_par(const void *par, struct fb_info_gen *info)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) info;

	DTRACE;

	*(l_fb_info->current_par) = *((struct pm3fb_par *) par);
	current_par_valid[l_fb_info->board_num] = 1;

	pm3fb_write_mode(l_fb_info);

#ifdef PM3FB_USE_ACCEL
	pm3fb_init_engine(l_fb_info);
#endif /* PM3FB_USE_ACCEL */
}

static void pm3fb_set_color(struct pm3fb_info *l_fb_info,
			    unsigned char regno, unsigned char r,
			    unsigned char g, unsigned char b)
{
	DTRACE;

	PM3_SLOW_WRITE_REG(PM3RD_PaletteWriteAddress, regno);
	PM3_SLOW_WRITE_REG(PM3RD_PaletteData, r);
	PM3_SLOW_WRITE_REG(PM3RD_PaletteData, g);
	PM3_SLOW_WRITE_REG(PM3RD_PaletteData, b);
}

static int pm3fb_getcolreg(unsigned regno, unsigned *red, unsigned *green,
			   unsigned *blue, unsigned *transp,
			   struct fb_info *info)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) info;

	DTRACE;

	if (regno < 256) {
		*red =
		    l_fb_info->palette[regno].red << 8 | l_fb_info->
		    palette[regno].red;
		*green =
		    l_fb_info->palette[regno].green << 8 | l_fb_info->
		    palette[regno].green;
		*blue =
		    l_fb_info->palette[regno].blue << 8 | l_fb_info->
		    palette[regno].blue;
		*transp =
		    l_fb_info->palette[regno].transp << 8 | l_fb_info->
		    palette[regno].transp;
	}
	return (regno > 255);
}

static int pm3fb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   struct fb_info *info)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) info;

	DTRACE;

	if (regno < 16) {
		switch (l_fb_info->current_par->depth) {
#ifdef FBCON_HAS_CFB8
		case 8:
			break;
#endif
#ifdef FBCON_HAS_CFB16
		case 12:
			l_fb_info->cmap.cmap12[regno] =
				(((u32) red & 0xf000) >> 4) |
				(((u32) green & 0xf000) >> 8) |
				(((u32) blue & 0xf000) >> 12);
			break;

		case 15:
			l_fb_info->cmap.cmap15[regno] =
				(((u32) red & 0xf800) >> 1) |
				(((u32) green & 0xf800) >> 6) |
				(((u32) blue & 0xf800) >> 11);
			break;

		case 16:
			l_fb_info->cmap.cmap16[regno] =
			    ((u32) red & 0xf800) |
			    (((u32) green & 0xfc00) >> 5) |
			    (((u32) blue & 0xf800) >> 11);
			break;
#endif
#ifdef FBCON_HAS_CFB32
		case 32:
			l_fb_info->cmap.cmap32[regno] =
			    (((u32) transp & 0xff00) << 16) |
			    (((u32) red & 0xff00) << 8) |
			    (((u32) green & 0xff00)) |
			    (((u32) blue & 0xff00) >> 8);
			break;
#endif
		default:
			DPRINTK(1, "bad depth %u\n",
				l_fb_info->current_par->depth);
			break;
		}
	}
	if (regno < 256) {
		l_fb_info->palette[regno].red = red >> 8;
		l_fb_info->palette[regno].green = green >> 8;
		l_fb_info->palette[regno].blue = blue >> 8;
		l_fb_info->palette[regno].transp = transp >> 8;
		if (l_fb_info->current_par->depth == 8)
			pm3fb_set_color(l_fb_info, regno, red >> 8,
					green >> 8, blue >> 8);
	}
	return (regno > 255);
}

static int pm3fb_blank(int blank_mode, struct fb_info_gen *info)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) info;
	u32 video;

	DTRACE;

	if (!current_par_valid[l_fb_info->board_num])
		return (1);

	video = l_fb_info->current_par->video;

	/*
	 * Oxygen VX1 - it appears that setting PM3VideoControl and
	 * then PM3RD_SyncControl to the same SYNC settings undoes
	 * any net change - they seem to xor together.  Only set the
	 * sync options in PM3RD_SyncControl.  --rmk
	 */
	video &= ~(PM3VideoControl_HSYNC_MASK |
		   PM3VideoControl_VSYNC_MASK);
	video |= PM3VideoControl_HSYNC_ACTIVE_HIGH |
		 PM3VideoControl_VSYNC_ACTIVE_HIGH;

	if (blank_mode > 0) {
		switch (blank_mode - 1) {

		case VESA_NO_BLANKING:	/* FIXME */
			video = video & ~(PM3VideoControl_ENABLE);
			break;

		case VESA_HSYNC_SUSPEND:
			video = video & ~(PM3VideoControl_HSYNC_MASK |
					  PM3VideoControl_BLANK_ACTIVE_LOW);
			break;
		case VESA_VSYNC_SUSPEND:
			video = video & ~(PM3VideoControl_VSYNC_MASK |
					  PM3VideoControl_BLANK_ACTIVE_LOW);
			break;
		case VESA_POWERDOWN:
			video = video & ~(PM3VideoControl_HSYNC_MASK |
					  PM3VideoControl_VSYNC_MASK |
					  PM3VideoControl_BLANK_ACTIVE_LOW);
			break;
		default:
			DPRINTK(1, "Unsupported blanking %d\n",
				blank_mode);
			return (1);
			break;
		}
	}

	PM3_SLOW_WRITE_REG(PM3VideoControl, video);

	return (0);
}

static void pm3fb_set_disp(const void *par, struct display *disp,
			   struct fb_info_gen *info)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) info;
	struct pm3fb_par *p = (struct pm3fb_par *) par;
	u32 flags;

	DTRACE;

	local_irq_save(flags);
	info->info.screen_base = l_fb_info->v_fb;
	switch (p->depth) {
#ifdef FBCON_HAS_CFB8
	case 8:
#ifdef PM3FB_USE_ACCEL
		if (!(noaccel[l_fb_info->board_num]))
			disp->dispsw = &pm3fb_cfb8;
		else
#endif /* PM3FB_USE_ACCEL */
			disp->dispsw = &fbcon_cfb8;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 12:
#ifdef PM3FB_USE_ACCEL
		if (!(noaccel[l_fb_info->board_num]))
			disp->dispsw = &pm3fb_cfb16;
		else
#endif /* PM3FB_USE_ACCEL */
			disp->dispsw = &fbcon_cfb16;
		disp->dispsw_data = l_fb_info->cmap.cmap12;
		break;
	case 15:
#ifdef PM3FB_USE_ACCEL
		if (!(noaccel[l_fb_info->board_num]))
			disp->dispsw = &pm3fb_cfb16;
		else
#endif /* PM3FB_USE_ACCEL */
			disp->dispsw = &fbcon_cfb16;
		disp->dispsw_data = l_fb_info->cmap.cmap15;
		break;
	case 16:
#ifdef PM3FB_USE_ACCEL
		if (!(noaccel[l_fb_info->board_num]))
			disp->dispsw = &pm3fb_cfb16;
		else
#endif /* PM3FB_USE_ACCEL */
			disp->dispsw = &fbcon_cfb16;
		disp->dispsw_data = l_fb_info->cmap.cmap16;
		break;
#endif
#ifdef FBCON_HAS_CFB32
	case 32:
#ifdef PM3FB_USE_ACCEL
		if (!(noaccel[l_fb_info->board_num]))
			disp->dispsw = &pm3fb_cfb32;
		else
#endif /* PM3FB_USE_ACCEL */
			disp->dispsw = &fbcon_cfb32;
		disp->dispsw_data = l_fb_info->cmap.cmap32;
		break;
#endif /* FBCON_HAS_CFB32 */
	default:
		disp->dispsw = &fbcon_dummy;
		DPRINTK(1, "Invalid depth, using fbcon_dummy\n");
		break;
	}
	local_irq_restore(flags);
}

/* */
static void pm3fb_detect(void)
{
	struct pci_dev *dev_array[PM3_MAX_BOARD];
	struct pci_dev *dev = NULL;
	struct pm3fb_info *l_fb_info = &(fb_info[0]);
	unsigned long i, j, done;

	DTRACE;

	for (i = 0; i < PM3_MAX_BOARD; i++) {
		dev_array[i] = NULL;
		fb_info[i].dev = NULL;
	}

	dev =
	    pci_find_device(PCI_VENDOR_ID_3DLABS,
			    PCI_DEVICE_ID_3DLABS_PERMEDIA3, dev);

	for (i = 0; ((i < PM3_MAX_BOARD) && dev); i++) {
		dev_array[i] = dev;
		dev =
		    pci_find_device(PCI_VENDOR_ID_3DLABS,
				    PCI_DEVICE_ID_3DLABS_PERMEDIA3, dev);
	}

	if (dev) {		/* more than PM3_MAX_BOARD */
		printk(KERN_WARNING "pm3fb: Warning: more than %d boards found\n",
		       PM3_MAX_BOARD);
	}

	if (!dev_array[0]) {	/* not a single board, abort */
		return;
	}

	/* allocate user-defined boards */
	for (i = 0; i < PM3_MAX_BOARD; i++) {
		if ((bus[i] >= 0) && (slot[i] >= 0) && (func[i] >= 0)) {
			for (j = 0; j < PM3_MAX_BOARD; j++) {
				if ((dev_array[j] != NULL) &&
				    (dev_array[j]->bus->number == bus[i])
				    && (PCI_SLOT(dev_array[j]->devfn) ==
					slot[i])
				    && (PCI_FUNC(dev_array[j]->devfn) ==
					func[i])) {
					fb_info[i].dev = dev_array[j];
					dev_array[j] = NULL;
				}
			}
		}
	}
	/* allocate remaining boards */
	for (i = 0; i < PM3_MAX_BOARD; i++) {
		if (fb_info[i].dev == NULL) {
			done = 0;
			for (j = 0; ((j < PM3_MAX_BOARD) && (!done)); j++) {
				if (dev_array[j] != NULL) {
					fb_info[i].dev = dev_array[j];
					dev_array[j] = NULL;
					done = 1;
				}
			}
		}
	}

	/* at that point, all PCI Permedia3 are detected and allocated */
	/* now, initialize... or not */
	for (i = 0; i < PM3_MAX_BOARD; i++) {
		l_fb_info = &(fb_info[i]);
		if ((l_fb_info->dev) && (!disable[i])) {	/* PCI device was found and not disabled by user */
			DPRINTK(2,
				"found @%lx Vendor %lx Device %lx ; base @ : %lx - %lx - %lx - %lx - %lx - %lx, irq %ld\n",
				(unsigned long) l_fb_info->dev,
				(unsigned long) l_fb_info->dev->vendor,
				(unsigned long) l_fb_info->dev->device,
				(unsigned long)
				pci_resource_start(l_fb_info->dev, 0),
				(unsigned long)
				pci_resource_start(l_fb_info->dev, 1),
				(unsigned long)
				pci_resource_start(l_fb_info->dev, 2),
				(unsigned long)
				pci_resource_start(l_fb_info->dev, 3),
				(unsigned long)
				pci_resource_start(l_fb_info->dev, 4),
				(unsigned long)
				pci_resource_start(l_fb_info->dev, 5),
				(unsigned long) l_fb_info->dev->irq);

			l_fb_info->pIOBase =
			    (unsigned char *)
			    pci_resource_start(l_fb_info->dev, 0);
#ifdef __BIG_ENDIAN
			l_fb_info->pIOBase += PM3_REGS_SIZE;
#endif
			l_fb_info->vIOBase = (unsigned char *) -1;
			l_fb_info->p_fb =
			    (unsigned char *)
			    pci_resource_start(l_fb_info->dev, 1);
			l_fb_info->v_fb = (unsigned char *) -1;

				if (!request_mem_region
			    ((unsigned long)l_fb_info->p_fb, 64 * 1024 * 1024, /* request full aperture size */
			     "pm3fb")) {
				printk
				    (KERN_ERR "pm3fb: Error: couldn't request framebuffer memory, board #%ld\n",
				     l_fb_info->board_num);
				continue;
			}
			if (!request_mem_region
			    ((unsigned long)l_fb_info->pIOBase, PM3_REGS_SIZE,
			     "pm3fb I/O regs")) {
				printk
				    (KERN_ERR "pm3fb: Error: couldn't request IObase memory, board #%ld\n",
				     l_fb_info->board_num);
				continue;
			}
			if (forcesize[l_fb_info->board_num])
				l_fb_info->fb_size = forcesize[l_fb_info->board_num];

			l_fb_info->fb_size =
			    pm3fb_size_memory(l_fb_info);
				if (l_fb_info->fb_size) {
				(void) pci_enable_device(l_fb_info->dev);
				pm3fb_common_init(l_fb_info);
			} else
				printk(KERN_ERR "pm3fb: memory problem, not enabling board #%ld\n", l_fb_info->board_num);
		}
	}
}

static int pm3fb_pan_display(const struct fb_var_screeninfo *var,
			     struct fb_info_gen *info)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) info;

	DTRACE;

	if (!current_par_valid[l_fb_info->board_num])
		return -EINVAL;

	l_fb_info->current_par->base =	/* in 128 bits chunk - i.e. AFTER Shiftbpp */
	    pm3fb_Shiftbpp(l_fb_info,
			   l_fb_info->current_par->depth,
			   (var->yoffset * l_fb_info->current_par->width) +
			   var->xoffset);
	PM3_SLOW_WRITE_REG(PM3ScreenBase, l_fb_info->current_par->base);
	return 0;
}

static int pm3fb_ioctl(struct inode *inode, struct file *file,
                       u_int cmd, u_long arg, int con,
		       struct fb_info *info)
{
	struct pm3fb_info *l_fb_info = (struct pm3fb_info *) info;
	u32 cm, i;
#ifdef PM3FB_MASTER_DEBUG
	char cc[3];
#endif /* PM3FB_MASTER_DEBUG */

	switch(cmd)
	{
#ifdef PM3FB_MASTER_DEBUG
	case PM3FBIO_CLEARMEMORY:
		if (copy_from_user(&cm, (void *)arg, sizeof(u32)))
			return(-EFAULT);
		pm3fb_clear_memory(l_fb_info, cm);
		return(0);
		break;

	case PM3FBIO_CLEARCMAP:
		if (copy_from_user(cc, (void*)arg, 3 * sizeof(char)))
			return(-EFAULT);
		pm3fb_clear_colormap(l_fb_info, cc[0], cc[1], cc[2]);
		return(0);
		break;
#endif /* PM3FB_MASTER_DEBUG */

	case PM3FBIO_RESETCHIP:
		cm = 1;
		PM3_SLOW_WRITE_REG(PM3ResetStatus, 1);
		for (i = 0 ; (i < 10000) && cm ; i++)
		{
			PM3_DELAY(10);
			cm = PM3_READ_REG(PM3ResetStatus);
		}
		if (cm)
		{
			printk(KERN_ERR "pm3fb: chip reset failed with status 0x%x\n", cm);
			return(-EIO);
		}
		/* first thing first, reload memory timings */
		pm3fb_write_memory_timings(l_fb_info);
#ifdef PM3FB_USE_ACCEL
		pm3fb_init_engine(l_fb_info);
#endif /* PM3FB_USE_ACCEL */
		pm3fb_write_mode(l_fb_info);
		return(0);
		break;

	default:
		DPRINTK(2, "unknown ioctl: %d (%x)\n", cmd, cmd);
		return(-EINVAL);
	}
}

/* ****************************************** */
/* ***** standard FB API init functions ***** */
/* ****************************************** */

int __init pm3fb_setup(char *options)
{
	long opsi = strlen(options);

	DTRACE;

	memcpy(g_options, options,
	       ((opsi + 1) >
		PM3_OPTIONS_SIZE) ? PM3_OPTIONS_SIZE : (opsi + 1));
	g_options[PM3_OPTIONS_SIZE - 1] = 0;

	return (0);
}

int __init pm3fb_init(void)
{
	DTRACE;

	DPRINTK(2, "This is pm3fb.c, CVS version: $Header: /cvsroot/linux/drivers/video/pm3fb.c,v 1.1 2002/02/25 19:11:06 marcelo Exp $");

	pm3fb_real_setup(g_options);

	pm3fb_detect();

	if (!fb_info[0].dev) {	/* not even one board ??? */
		DPRINTK(1, "No PCI Permedia3 board detected\n");
	}
	return (0);
}

/* ************************* */
/* **** Module support ***** */
/* ************************* */

#ifdef MODULE
MODULE_AUTHOR("Romain Dolbeau");
MODULE_DESCRIPTION("Permedia3 framebuffer device driver");
static char *mode[PM3_MAX_BOARD];
MODULE_PARM(mode,PM3_MAX_BOARD_MODULE_ARRAY_STRING);
MODULE_PARM_DESC(mode,"video mode");
MODULE_PARM(disable,PM3_MAX_BOARD_MODULE_ARRAY_SHORT);
MODULE_PARM_DESC(disable,"disable board");
static short off[PM3_MAX_BOARD];
MODULE_PARM(off,PM3_MAX_BOARD_MODULE_ARRAY_SHORT);
MODULE_PARM_DESC(off,"disable board");
static char *pciid[PM3_MAX_BOARD];
MODULE_PARM(pciid,PM3_MAX_BOARD_MODULE_ARRAY_STRING);
MODULE_PARM_DESC(pciid,"board PCI Id");
MODULE_PARM(noaccel,PM3_MAX_BOARD_MODULE_ARRAY_SHORT);
MODULE_PARM_DESC(noaccel,"disable accel");
static char *font[PM3_MAX_BOARD];
MODULE_PARM(font,PM3_MAX_BOARD_MODULE_ARRAY_STRING);
MODULE_PARM_DESC(font,"choose font");
MODULE_PARM(depth,PM3_MAX_BOARD_MODULE_ARRAY_SHORT);
MODULE_PARM_DESC(depth,"boot-time depth");
MODULE_PARM(printtimings, "h");
MODULE_PARM_DESC(printtimings, "print the memory timings of the card(s)");
MODULE_PARM(forcesize, PM3_MAX_BOARD_MODULE_ARRAY_SHORT);
MODULE_PARM_DESC(forcesize, "force specified memory size");
/*
MODULE_SUPPORTED_DEVICE("Permedia3 PCI boards")
MODULE_GENERIC_TABLE(gtype,name)
MODULE_DEVICE_TABLE(type,name)
*/

void pm3fb_build_options(void)
{
	int i;
	char ts[128];

	strcpy(g_options, "pm3fb");
	for (i = 0; i < PM3_MAX_BOARD ; i++)
	{
		if (mode[i])
		{
			sprintf(ts, ",mode:%d:%s", i, mode[i]);
			strncat(g_options, ts, PM3_OPTIONS_SIZE - strlen(g_options));
		}
		if (disable[i] || off[i])
		{
			sprintf(ts, ",disable:%d:", i);
			strncat(g_options, ts, PM3_OPTIONS_SIZE - strlen(g_options));
		}
		if (pciid[i])
		{
			sprintf(ts, ",pciid:%d:%s", i, pciid[i]);
			strncat(g_options, ts, PM3_OPTIONS_SIZE - strlen(g_options));
		}
		if (noaccel[i])
		{
			sprintf(ts, ",noaccel:%d:", i);
			strncat(g_options, ts, PM3_OPTIONS_SIZE - strlen(g_options));
		}
		if (font[i])
		{
			sprintf(ts, ",font:%d:%s", i, font[i]);
			strncat(g_options, ts, PM3_OPTIONS_SIZE - strlen(g_options));
		}
		if (depth[i])
		{
			sprintf(ts, ",depth:%d:%d", i, depth[i]);
			strncat(g_options, ts, PM3_OPTIONS_SIZE - strlen(g_options));
		}
	}
	g_options[PM3_OPTIONS_SIZE - 1] = '\0';
	DPRINTK(1, "pm3fb use options: %s\n", g_options);
}

int init_module(void)
{
	DTRACE;

	pm3fb_build_options();

	pm3fb_init();

	return (0);
}

void cleanup_module(void)
{
	DTRACE;
	{
		unsigned long i;
		struct pm3fb_info *l_fb_info;
		for (i = 0; i < PM3_MAX_BOARD; i++) {
			l_fb_info = &(fb_info[i]);
			if ((l_fb_info->dev != NULL)
			    && (!(disable[l_fb_info->board_num]))) {
				if (l_fb_info->vIOBase !=
				    (unsigned char *) -1) {
					pm3fb_unmapIO(l_fb_info);
					release_mem_region(l_fb_info->p_fb,
							   l_fb_info->
							   fb_size);
					release_mem_region(l_fb_info->
							   pIOBase,
							   PM3_REGS_SIZE);
				}
				unregister_framebuffer(&l_fb_info->gen.
						       info);
			}
		}
	}
	return;
}
#endif /* MODULE */
