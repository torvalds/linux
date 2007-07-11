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
 * arch/i386/boot/memory.c
 *
 * Memory detection code
 */

#include "boot.h"

#define SMAP	0x534d4150	/* ASCII "SMAP" */

static int detect_memory_e820(void)
{
	u32 next = 0;
	u32 size, id;
	u8 err;
	struct e820entry *desc = boot_params.e820_map;

	do {
		size = sizeof(struct e820entry);
		id = SMAP;
		asm("int $0x15; setc %0"
		    : "=am" (err), "+b" (next), "+d" (id), "+c" (size),
		      "=m" (*desc)
		    : "D" (desc), "a" (0xe820));

		if (err || id != SMAP)
			break;

		boot_params.e820_entries++;
		desc++;
	} while (next && boot_params.e820_entries < E820MAX);

	return boot_params.e820_entries;
}

static int detect_memory_e801(void)
{
	u16 ax, bx, cx, dx;
	u8 err;

	bx = cx = dx = 0;
	ax = 0xe801;
	asm("stc; int $0x15; setc %0"
	    : "=m" (err), "+a" (ax), "+b" (bx), "+c" (cx), "+d" (dx));

	if (err)
		return -1;

	/* Do we really need to do this? */
	if (cx || dx) {
		ax = cx;
		bx = dx;
	}

	if (ax > 15*1024)
		return -1;	/* Bogus! */

	/* This ignores memory above 16MB if we have a memory hole
	   there.  If someone actually finds a machine with a memory
	   hole at 16MB and no support for 0E820h they should probably
	   generate a fake e820 map. */
	boot_params.alt_mem_k = (ax == 15*1024) ? (dx << 6)+ax : ax;

	return 0;
}

static int detect_memory_88(void)
{
	u16 ax;
	u8 err;

	ax = 0x8800;
	asm("stc; int $0x15; setc %0" : "=bcdm" (err), "+a" (ax));

	boot_params.screen_info.ext_mem_k = ax;

	return -err;
}

int detect_memory(void)
{
	if (detect_memory_e820() > 0)
		return 0;

	if (!detect_memory_e801())
		return 0;

	return detect_memory_88();
}
