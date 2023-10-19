/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    User-space Probes (UProbes) for s390
 *
 *    Copyright IBM Corp. 2014
 *    Author(s): Jan Willeke,
 */

#ifndef _ASM_UPROBES_H
#define _ASM_UPROBES_H

#include <linux/notifier.h>

typedef u16 uprobe_opcode_t;

#define UPROBE_XOL_SLOT_BYTES	256 /* cache aligned */

#define UPROBE_SWBP_INSN	0x0002
#define UPROBE_SWBP_INSN_SIZE	2

struct arch_uprobe {
	union{
		uprobe_opcode_t insn[3];
		uprobe_opcode_t ixol[3];
	};
	unsigned int saved_per : 1;
	unsigned int saved_int_code;
};

struct arch_uprobe_task {
};

#endif	/* _ASM_UPROBES_H */
