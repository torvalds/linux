/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * arch/i386/boot/video-vga.c
 *
 * Common all-VGA modes
 */

#include "boot.h"
#include "video.h"

static struct mode_info vga_modes[] = {
	{ VIDEO_80x25,  80, 25, 0 },
	{ VIDEO_8POINT, 80, 50, 0 },
	{ VIDEO_80x43,  80, 43, 0 },
	{ VIDEO_80x28,  80, 28, 0 },
	{ VIDEO_80x30,  80, 30, 0 },
	{ VIDEO_80x34,  80, 34, 0 },
	{ VIDEO_80x60,  80, 60, 0 },
};

static struct mode_info ega_modes[] = {
	{ VIDEO_80x25,  80, 25, 0 },
	{ VIDEO_8POINT, 80, 43, 0 },
};

static struct mode_info cga_modes[] = {
	{ VIDEO_80x25,  80, 25, 0 },
};

__videocard video_vga;

/* Set basic 80x25 mode */
static u8 vga_set_basic_mode(void)
{
	u16 ax;
	u8 rows;
	u8 mode;

#ifdef CONFIG_VIDEO_400_HACK
	if (adapter >= ADAPTER_VGA) {
		asm volatile(INT10
			     : : "a" (0x1202), "b" (0x0030)
			     : "ecx", "edx", "esi", "edi");
	}
#endif

	ax = 0x0f00;
	asm volatile(INT10
		     : "+a" (ax)
		     : : "ebx", "ecx", "edx", "esi", "edi");

	mode = (u8)ax;

	set_fs(0);
	rows = rdfs8(0x484);	/* rows minus one */

#ifndef CONFIG_VIDEO_400_HACK
	if ((ax == 0x5003 || ax == 0x5007) &&
	    (rows == 0 || rows == 24))
		return mode;
#endif

	if (mode != 3 && mode != 7)
		mode = 3;

	/* Set the mode */
	ax = mode;
	asm volatile(INT10
		     : "+a" (ax)
		     : : "ebx", "ecx", "edx", "esi", "edi");
	do_restore = 1;
	return mode;
}

static void vga_set_8font(void)
{
	/* Set 8x8 font - 80x43 on EGA, 80x50 on VGA */

	/* Set 8x8 font */
	asm volatile(INT10 : : "a" (0x1112), "b" (0));

	/* Use alternate print screen */
	asm volatile(INT10 : : "a" (0x1200), "b" (0x20));

	/* Turn off cursor emulation */
	asm volatile(INT10 : : "a" (0x1201), "b" (0x34));

	/* Cursor is scan lines 6-7 */
	asm volatile(INT10 : : "a" (0x0100), "c" (0x0607));
}

static void vga_set_14font(void)
{
	/* Set 9x14 font - 80x28 on VGA */

	/* Set 9x14 font */
	asm volatile(INT10 : : "a" (0x1111), "b" (0));

	/* Turn off cursor emulation */
	asm volatile(INT10 : : "a" (0x1201), "b" (0x34));

	/* Cursor is scan lines 11-12 */
	asm volatile(INT10 : : "a" (0x0100), "c" (0x0b0c));
}

static void vga_set_80x43(void)
{
	/* Set 80x43 mode on VGA (not EGA) */

	/* Set 350 scans */
	asm volatile(INT10 : : "a" (0x1201), "b" (0x30));

	/* Reset video mode */
	asm volatile(INT10 : : "a" (0x0003));

	vga_set_8font();
}

/* I/O address of the VGA CRTC */
u16 vga_crtc(void)
{
	return (inb(0x3cc) & 1) ? 0x3d4 : 0x3b4;
}

static void vga_set_480_scanlines(int end)
{
	u16 crtc;
	u8  csel;

	crtc = vga_crtc();

	out_idx(0x0c, crtc, 0x11); /* Vertical sync end, unlock CR0-7 */
	out_idx(0x0b, crtc, 0x06); /* Vertical total */
	out_idx(0x3e, crtc, 0x07); /* Vertical overflow */
	out_idx(0xea, crtc, 0x10); /* Vertical sync start */
	out_idx(end, crtc, 0x12); /* Vertical display end */
	out_idx(0xe7, crtc, 0x15); /* Vertical blank start */
	out_idx(0x04, crtc, 0x16); /* Vertical blank end */
	csel = inb(0x3cc);
	csel &= 0x0d;
	csel |= 0xe2;
	outb(csel, 0x3cc);
}

static void vga_set_80x30(void)
{
	vga_set_480_scanlines(0xdf);
}

static void vga_set_80x34(void)
{
	vga_set_14font();
	vga_set_480_scanlines(0xdb);
}

static void vga_set_80x60(void)
{
	vga_set_8font();
	vga_set_480_scanlines(0xdf);
}

static int vga_set_mode(struct mode_info *mode)
{
	/* Set the basic mode */
	vga_set_basic_mode();

	/* Override a possibly broken BIOS */
	force_x = mode->x;
	force_y = mode->y;

	switch (mode->mode) {
	case VIDEO_80x25:
		break;
	case VIDEO_8POINT:
		vga_set_8font();
		break;
	case VIDEO_80x43:
		vga_set_80x43();
		break;
	case VIDEO_80x28:
		vga_set_14font();
		break;
	case VIDEO_80x30:
		vga_set_80x30();
		break;
	case VIDEO_80x34:
		vga_set_80x34();
		break;
	case VIDEO_80x60:
		vga_set_80x60();
		break;
	}

	return 0;
}

/*
 * Note: this probe includes basic information required by all
 * systems.  It should be executed first, by making sure
 * video-vga.c is listed first in the Makefile.
 */
static int vga_probe(void)
{
	u16 ega_bx;

	static const char *card_name[] = {
		"CGA/MDA/HGC", "EGA", "VGA"
	};
	static struct mode_info *mode_lists[] = {
		cga_modes,
		ega_modes,
		vga_modes,
	};
	static int mode_count[] = {
		sizeof(cga_modes)/sizeof(struct mode_info),
		sizeof(ega_modes)/sizeof(struct mode_info),
		sizeof(vga_modes)/sizeof(struct mode_info),
	};
	u8 vga_flag;

	asm(INT10
	    : "=b" (ega_bx)
	    : "a" (0x1200), "b" (0x10) /* Check EGA/VGA */
	    : "ecx", "edx", "esi", "edi");

#ifndef _WAKEUP
	boot_params.screen_info.orig_video_ega_bx = ega_bx;
#endif

	/* If we have MDA/CGA/HGC then BL will be unchanged at 0x10 */
	if ((u8)ega_bx != 0x10) {
		/* EGA/VGA */
		asm(INT10
		    : "=a" (vga_flag)
		    : "a" (0x1a00)
		    : "ebx", "ecx", "edx", "esi", "edi");

		if (vga_flag == 0x1a) {
			adapter = ADAPTER_VGA;
#ifndef _WAKEUP
			boot_params.screen_info.orig_video_isVGA = 1;
#endif
		} else {
			adapter = ADAPTER_EGA;
		}
	} else {
		adapter = ADAPTER_CGA;
	}

	video_vga.modes = mode_lists[adapter];
	video_vga.card_name = card_name[adapter];
	return mode_count[adapter];
}

__videocard video_vga =
{
	.card_name	= "VGA",
	.probe		= vga_probe,
	.set_mode	= vga_set_mode,
};
