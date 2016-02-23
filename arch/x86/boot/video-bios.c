/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * Standard video BIOS modes
 *
 * We have two options for this; silent and scanned.
 */

#include "boot.h"
#include "video.h"

static __videocard video_bios;

/* Set a conventional BIOS mode */
static int set_bios_mode(u8 mode);

static int bios_set_mode(struct mode_info *mi)
{
	return set_bios_mode(mi->mode - VIDEO_FIRST_BIOS);
}

static int set_bios_mode(u8 mode)
{
	struct biosregs ireg, oreg;
	u8 new_mode;

	initregs(&ireg);
	ireg.al = mode;		/* AH=0x00 Set Video Mode */
	intcall(0x10, &ireg, NULL);

	ireg.ah = 0x0f;		/* Get Current Video Mode */
	intcall(0x10, &ireg, &oreg);

	do_restore = 1;		/* Assume video contents were lost */

	/* Not all BIOSes are clean with the top bit */
	new_mode = oreg.al & 0x7f;

	if (new_mode == mode)
		return 0;	/* Mode change OK */

#ifndef _WAKEUP
	if (new_mode != boot_params.screen_info.orig_video_mode) {
		/* Mode setting failed, but we didn't end up where we
		   started.  That's bad.  Try to revert to the original
		   video mode. */
		ireg.ax = boot_params.screen_info.orig_video_mode;
		intcall(0x10, &ireg, NULL);
	}
#endif
	return -1;
}

static int bios_probe(void)
{
	u8 mode;
#ifdef _WAKEUP
	u8 saved_mode = 0x03;
#else
	u8 saved_mode = boot_params.screen_info.orig_video_mode;
#endif
	u16 crtc;
	struct mode_info *mi;
	int nmodes = 0;

	if (adapter != ADAPTER_EGA && adapter != ADAPTER_VGA)
		return 0;

	set_fs(0);
	crtc = vga_crtc();

	video_bios.modes = GET_HEAP(struct mode_info, 0);

	for (mode = 0x14; mode <= 0x7f; mode++) {
		if (!heap_free(sizeof(struct mode_info)))
			break;

		if (mode_defined(VIDEO_FIRST_BIOS+mode))
			continue;

		if (set_bios_mode(mode))
			continue;

		/* Try to verify that it's a text mode. */

		/* Attribute Controller: make graphics controller disabled */
		if (in_idx(0x3c0, 0x10) & 0x01)
			continue;

		/* Graphics Controller: verify Alpha addressing enabled */
		if (in_idx(0x3ce, 0x06) & 0x01)
			continue;

		/* CRTC cursor location low should be zero(?) */
		if (in_idx(crtc, 0x0f))
			continue;

		mi = GET_HEAP(struct mode_info, 1);
		mi->mode = VIDEO_FIRST_BIOS+mode;
		mi->depth = 0;	/* text */
		mi->x = rdfs16(0x44a);
		mi->y = rdfs8(0x484)+1;
		nmodes++;
	}

	set_bios_mode(saved_mode);

	return nmodes;
}

static __videocard video_bios =
{
	.card_name	= "BIOS",
	.probe		= bios_probe,
	.set_mode	= bios_set_mode,
	.unsafe		= 1,
	.xmode_first	= VIDEO_FIRST_BIOS,
	.xmode_n	= 0x80,
};
