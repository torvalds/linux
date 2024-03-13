/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_UPROBES_H
#define _ASM_UPROBES_H
/*
 * User-space Probes (UProbes) for x86
 *
 * Copyright (C) IBM Corporation, 2008-2011
 * Authors:
 *	Srikar Dronamraju
 *	Jim Keniston
 */

#include <linux/notifier.h>

typedef u8 uprobe_opcode_t;

#define MAX_UINSN_BYTES			  16
#define UPROBE_XOL_SLOT_BYTES		 128	/* to keep it cache aligned */

#define UPROBE_SWBP_INSN		0xcc
#define UPROBE_SWBP_INSN_SIZE		   1

struct uprobe_xol_ops;

struct arch_uprobe {
	union {
		u8			insn[MAX_UINSN_BYTES];
		u8			ixol[MAX_UINSN_BYTES];
	};

	const struct uprobe_xol_ops	*ops;

	union {
		struct {
			s32	offs;
			u8	ilen;
			u8	opc1;
		}			branch;
		struct {
			u8	fixups;
			u8	ilen;
		} 			defparam;
		struct {
			u8	reg_offset;	/* to the start of pt_regs */
			u8	ilen;
		}			push;
	};
};

struct arch_uprobe_task {
#ifdef CONFIG_X86_64
	unsigned long			saved_scratch_register;
#endif
	unsigned int			saved_trap_nr;
	unsigned int			saved_tf;
};

#endif	/* _ASM_UPROBES_H */
