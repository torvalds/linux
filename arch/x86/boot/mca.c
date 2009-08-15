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
 * Get the MCA system description table
 */

#include "boot.h"

int query_mca(void)
{
	struct biosregs ireg, oreg;
	u16 len;

	initregs(&ireg);
	ireg.ah = 0xc0;
	intcall(0x15, &ireg, &oreg);

	if (oreg.eflags & X86_EFLAGS_CF)
		return -1;	/* No MCA present */

	set_fs(oreg.es);
	len = rdfs16(oreg.bx);

	if (len > sizeof(boot_params.sys_desc_table))
		len = sizeof(boot_params.sys_desc_table);

	copy_from_fs(&boot_params.sys_desc_table, oreg.bx, len);
	return 0;
}
