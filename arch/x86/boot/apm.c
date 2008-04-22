/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 *   Original APM BIOS checking by Stephen Rothwell, May 1994
 *   (sfr@canb.auug.org.au)
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * Get APM BIOS information
 */

#include "boot.h"

int query_apm_bios(void)
{
	u16 ax, bx, cx, dx, di;
	u32 ebx, esi;
	u8 err;

	/* APM BIOS installation check */
	ax = 0x5300;
	bx = cx = 0;
	asm volatile("pushl %%ebp ; int $0x15 ; popl %%ebp ; setc %0"
		     : "=d" (err), "+a" (ax), "+b" (bx), "+c" (cx)
		     : : "esi", "edi");

	if (err)
		return -1;		/* No APM BIOS */

	if (bx != 0x504d)	/* "PM" signature */
		return -1;

	if (!(cx & 0x02))		/* 32 bits supported? */
		return -1;

	/* Disconnect first, just in case */
	ax = 0x5304;
	bx = 0;
	asm volatile("pushl %%ebp ; int $0x15 ; popl %%ebp"
		     : "+a" (ax), "+b" (bx)
		     : : "ecx", "edx", "esi", "edi");

	/* Paranoia */
	ebx = esi = 0;
	cx = dx = di = 0;

	/* 32-bit connect */
	asm volatile("pushl %%ebp ; int $0x15 ; popl %%ebp ; setc %6"
		     : "=a" (ax), "+b" (ebx), "+c" (cx), "+d" (dx),
		       "+S" (esi), "+D" (di), "=m" (err)
		     : "a" (0x5303));

	boot_params.apm_bios_info.cseg = ax;
	boot_params.apm_bios_info.offset = ebx;
	boot_params.apm_bios_info.cseg_16 = cx;
	boot_params.apm_bios_info.dseg = dx;
	boot_params.apm_bios_info.cseg_len = (u16)esi;
	boot_params.apm_bios_info.cseg_16_len = esi >> 16;
	boot_params.apm_bios_info.dseg_len = di;

	if (err)
		return -1;

	/* Redo the installation check as the 32-bit connect;
	   some BIOSes return different flags this way... */

	ax = 0x5300;
	bx = cx = 0;
	asm volatile("pushl %%ebp ; int $0x15 ; popl %%ebp ; setc %0"
		     : "=d" (err), "+a" (ax), "+b" (bx), "+c" (cx)
		     : : "esi", "edi");

	if (err || bx != 0x504d) {
		/* Failure with 32-bit connect, try to disconect and ignore */
		ax = 0x5304;
		bx = 0;
		asm volatile("pushl %%ebp ; int $0x15 ; popl %%ebp"
			     : "+a" (ax), "+b" (bx)
			     : : "ecx", "edx", "esi", "edi");
		return -1;
	}

	boot_params.apm_bios_info.version = ax;
	boot_params.apm_bios_info.flags = cx;
	return 0;
}

