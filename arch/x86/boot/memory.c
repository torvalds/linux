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
 * Memory detection code
 */

#include "boot.h"

#define SMAP	0x534d4150	/* ASCII "SMAP" */

struct e820_ext_entry {
	struct e820entry std;
	u32 ext_flags;
} __attribute__((packed));

static int detect_memory_e820(void)
{
	int count = 0;
	u32 next = 0;
	u32 size, id, edi;
	u8 err;
	struct e820entry *desc = boot_params.e820_map;
	static struct e820_ext_entry buf; /* static so it is zeroed */

	/*
	 * Set this here so that if the BIOS doesn't change this field
	 * but still doesn't change %ecx, we're still okay...
	 */
	buf.ext_flags = 1;

	do {
		size = sizeof buf;

		/* Important: %edx and %esi are clobbered by some BIOSes,
		   so they must be either used for the error output
		   or explicitly marked clobbered.  Given that, assume there
		   is something out there clobbering %ebp and %edi, too. */
		asm("pushl %%ebp; int $0x15; popl %%ebp; setc %0"
		    : "=d" (err), "+b" (next), "=a" (id), "+c" (size),
		      "=D" (edi), "+m" (buf)
		    : "D" (&buf), "d" (SMAP), "a" (0xe820)
		    : "esi");

		/* BIOSes which terminate the chain with CF = 1 as opposed
		   to %ebx = 0 don't always report the SMAP signature on
		   the final, failing, probe. */
		if (err)
			break;

		/* Some BIOSes stop returning SMAP in the middle of
		   the search loop.  We don't know exactly how the BIOS
		   screwed up the map at that point, we might have a
		   partial map, the full map, or complete garbage, so
		   just return failure. */
		if (id != SMAP) {
			count = 0;
			break;
		}

		/* ACPI 3.0 added the extended flags support.  If bit 0
		   in the extended flags is zero, we're supposed to simply
		   ignore the entry -- a backwards incompatible change! */
		if (size > 20 && !(buf.ext_flags & 1))
			continue;

		*desc++ = buf.std;
		count++;
	} while (next && count < ARRAY_SIZE(boot_params.e820_map));

	return boot_params.e820_entries = count;
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
	int err = -1;

	if (detect_memory_e820() > 0)
		err = 0;

	if (!detect_memory_e801())
		err = 0;

	if (!detect_memory_88())
		err = 0;

	return err;
}
