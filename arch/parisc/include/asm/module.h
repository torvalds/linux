/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PARISC_MODULE_H
#define _ASM_PARISC_MODULE_H

#include <asm-generic/module.h>

/*
 * This file contains the parisc architecture specific module code.
 */

struct unwind_table;

struct mod_arch_specific
{
	unsigned long got_offset, got_count, got_max;
	unsigned long fdesc_offset, fdesc_count, fdesc_max;
	struct {
		unsigned long stub_offset;
		unsigned int stub_entries;
		} *section;
	int unwind_section;
	struct unwind_table *unwind;
};

#endif /* _ASM_PARISC_MODULE_H */
