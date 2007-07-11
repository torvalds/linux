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
 * arch/i386/boot/voyager.c
 *
 * Get the Voyager config information
 */

#include "boot.h"

#ifdef CONFIG_X86_VOYAGER

int query_voyager(void)
{
	u8 err;
	u16 es, di;
	/* Abuse the apm_bios_info area for this */
	u8 *data_ptr = (u8 *)&boot_params.apm_bios_info;

	data_ptr[0] = 0xff;	/* Flag on config not found(?) */

	asm("pushw %%es ; "
	    "int $0x15 ; "
	    "setc %0 ; "
	    "movw %%es, %1 ; "
	    "popw %%es"
	    : "=qm" (err), "=rm" (es), "=D" (di)
	    : "a" (0xffc0));

	if (err)
		return -1;	/* Not Voyager */

	set_fs(es);
	copy_from_fs(data_ptr, di, 7);	/* Table is 7 bytes apparently */
	return 0;
}

#endif /* CONFIG_X86_VOYAGER */
