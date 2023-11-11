/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_MODULE_H
#define _ASM_IA64_MODULE_H

#include <asm-generic/module.h>

/*
 * IA-64-specific support for kernel module loader.
 *
 * Copyright (C) 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

struct elf64_shdr;			/* forward declration */

struct mod_arch_specific {
	/* Used only at module load time. */
	struct elf64_shdr *core_plt;	/* core PLT section */
	struct elf64_shdr *init_plt;	/* init PLT section */
	struct elf64_shdr *got;		/* global offset table */
	struct elf64_shdr *opd;		/* official procedure descriptors */
	struct elf64_shdr *unwind;	/* unwind-table section */
	unsigned long gp;		/* global-pointer for module */
	unsigned int next_got_entry;	/* index of next available got entry */

	/* Used at module run and cleanup time. */
	void *core_unw_table;		/* core unwind-table cookie returned by unwinder */
	void *init_unw_table;		/* init unwind-table cookie returned by unwinder */
	void *opd_addr;			/* symbolize uses .opd to get to actual function */
	unsigned long opd_size;
};

#define ARCH_SHF_SMALL	SHF_IA_64_SHORT

#endif /* _ASM_IA64_MODULE_H */
