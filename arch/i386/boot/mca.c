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
 * arch/i386/boot/mca.c
 *
 * Get the MCA system description table
 */

#include "boot.h"

int query_mca(void)
{
	u8 err;
	u16 es, bx, len;

	asm("pushw %%es ; "
	    "int $0x15 ; "
	    "setc %0 ; "
	    "movw %%es, %1 ; "
	    "popw %%es"
	    : "=acd" (err), "=acdSD" (es), "=b" (bx)
	    : "a" (0xc000));

	if (err)
		return -1;	/* No MCA present */

	set_fs(es);
	len = rdfs16(bx);

	if (len > sizeof(boot_params.sys_desc_table))
		len = sizeof(boot_params.sys_desc_table);

	copy_from_fs(&boot_params.sys_desc_table, bx, len);
	return 0;
}
