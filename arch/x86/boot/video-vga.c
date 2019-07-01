// SPDX-License-Identifier: GPL-2.0-only
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 * ----------------------------------------------------------------------- */

/*
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

static __videocard video_vga;

/* Set basic 80x25 mode */
static u8 vga_set_basic_mode(void)
{
	struct biosregs ireg, oreg;
	u8 mode;

	initregs(&ireg);

	/* Query current mode */
	ireg.ax = 0x0f00;
	intcall(0x10, &ireg, &oreg);
	mode = oreg.al;

	if (mode != 3 && mode != 7)
		mode = 3;

	/* Set the mode */
	ireg.ax = mode;		/* AH=0: set mode */
	intcall(0x10, &ireg, NULL);
	do_restore = 1;
	return mode;
}

static void vga_set_8font(void)
{
	/* Set 8x8 font - 80x43 on EGA, 80x50 on VGA */
	struct biosregs ireg;

	initregs(&ireg);

	/* Set 8x8 font */
	ireg.ax = 0x1112;
	/* ireg.bl = 0; */
	intcall(0x10, &ireg, NULL);

	/* Use alternate print screen */
	ireg.ax = 0x1200;
	ireg.bl = 0x20;
	intcall(0x10, &ireg, NULL);

	/* Turn off cursor emulation */
	ireg.ax = 0x1201;
	ireg.bl = 0x34;
	intcall(0x10, &ireg, NULL);

	/* Cursor is scan lines 6-7 */
	ireg.ax = 0x0100;
	ireg.cx = 0x0607;
	intcall(0x10, &ireg, NULL);
}

static void vga_set_14font(void)
{
	/* Set 9x14 font - 80x28 on VGA */
	struct biosregs ireg;

	initregs(&ireg);

	/* Set 9x14 font */
	ireg.ax = 0x1111;
	/* ireg.bl = 0; */
	intcall(0x10, &ireg, NULL);

	/* Turn off cursor emulation */
	ireg.ax = 0x1201;
	ireg.bl = 0x34;
	intcall(0x10, &ireg, NULL);

	/* Cursor is scan lines 11-12 */
	ireg.ax = 0x0100;
	ireg.cx = 0x0b0c;
	intcall(0x10, &ireg, NULL);
}

static void vga_set_80x43(void)
{
	/* Set 80x43 mode on VGA (not EGA) */
	struct biosregs ireg;

	initregs(&ireg);

	/* Set 350 scans */
	ireg.ax = 0x1201;
	ireg.bl = 0x30;
	intcall(0x10, &ireg, NULL);

	/* Reset video mode */
	ireg.ax = 0x0003;
	intcall(0x10, &ireg, NULL);

	vga_set_8font();
}

/* I/O address of the VGA CRTC */
u16 vga_crtc(void)
{
	return (inb(0x3cc) & 1) ? 0x3d4 : 0x3b4;
}

static void vga_set_480_scanlines(void)
{
	u16 crtc;		/* CRTC base address */
	u8  csel;		/* CRTC miscellaneous output register */

	crtc = vga_crtc();

	out_idx(0x0c, crtc, 0x11); /* Vertical sync end, unlock CR0-7 */
	out_idx(0x0b, crtc, 0x06); /* Vertical total */
	out_idx(0x3e, crtc, 0x07); /* Vertical overflow */
	out_idx(0xea, crtc, 0x10); /* Vertical sync start */
	out_idx(0xdf, crtc, 0x12); /* Vertical display end */
	out_idx(0xe7, crtc, 0x15); /* Vertical blank start */
	out_idx(0x04, crtc, 0x16); /* Vertical blank end */
	csel = inb(0x3cc);
	csel &= 0x0d;
	csel |= 0xe2;
	outb(csel, 0x3c2);
}

static void vga_set_vertical_end(int lines)
{
	u16 crtc;		/* CRTC base address */
	u8  ovfw;		/* CRTC overflow register */
	int end = lines-1;

	crtc = vga_crtc();

	ovfw = 0x3c | ((end >> (8-1)) & 0x02) | ((end >> (9-6)) & 0x40);

	out_idx(ovfw, crtc, 0x07); /* Vertical overflow */
	out_idx(end,  crtc, 0x12); /* Vertical display end */
}

static void vga_set_80x30(void)
{
	vga_set_480_scanlines();
	vga_set_vertical_end(30*16);
}

static void vga_set_80x34(void)
{
	vga_set_480_scanlines();
	vga_set_14font();
	vga_set_vertical_end(34*14);
}

static void vga_set_80x60(void)
{
	vga_set_480_scanlines();
	vga_set_8font();
	vga_set_vertical_end(60*8);
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
	static const char *card_name[] = {
		"CGA/MDA/HGC", "EGA", "VGA"
	};
	static struct mode_info *mode_lists[] = {
		cga_modes,
		ega_modes,
		vga_modes,
	};
	static int mode_count[] = {
		ARRAY_SIZE(cga_modes),
		ARRAY_SIZE(ega_modes),
		ARRAY_SIZE(vga_modes),
	};

	struct biosregs ireg, oreg;

	initregs(&ireg);

	ireg.ax = 0x1200;
	ireg.bl = 0x10;		/* Check EGA/VGA */
	intcall(0x10, &ireg, &oreg);

#ifndef _WAKEUP
	boot_params.screen_info.orig_video_ega_bx = oreg.bx;
#endif

	/* If we have MDA/CGA/HGC then BL will be unchanged at 0x10 */
	if (oreg.bl != 0x10) {
		/* EGA/VGA */
		ireg.ax = 0x1a00;
		intcall(0x10, &ireg, &oreg);

		if (oreg.al == 0x1a) {
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

static __videocard video_vga = {
	.card_name	= "VGA",
	.probe		= vga_probe,
	.set_mode	= vga_set_mode,
};
