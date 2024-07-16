/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_EXTABLE_H
#define _ASM_IA64_EXTABLE_H

#define ARCH_HAS_RELATIVE_EXTABLE

struct exception_table_entry {
	int insn;	/* location-relative address of insn this fixup is for */
	int fixup;	/* location-relative continuation addr.; if bit 2 is set, r9 is set to 0 */
};

#endif
