/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_UPROBES_H
#define _ASM_UPROBES_H
/*
 * User-space Probes (UProbes) for sparc
 *
 * Copyright (C) 2013 Oracle, Inc.
 *
 * Authors:
 *     Jose E. Marchesi <jose.marchesi@oracle.com>
 *	Eric Saint Etienne <eric.saint.etienne@oracle.com>
 */

typedef u32 uprobe_opcode_t;

#define MAX_UINSN_BYTES		4
#define UPROBE_XOL_SLOT_BYTES	(MAX_UINSN_BYTES * 2)

#define UPROBE_SWBP_INSN_SIZE	4
#define UPROBE_SWBP_INSN	0x91d02073 /* ta 0x73 */
#define UPROBE_STP_INSN		0x91d02074 /* ta 0x74 */

#define ANNUL_BIT (1 << 29)

struct arch_uprobe {
	union {
		u8  insn[MAX_UINSN_BYTES];
		u32 ixol;
	};
};

struct arch_uprobe_task {
	u64 saved_tpc;
	u64 saved_tnpc;
};

struct task_struct;
struct notifier_block;

extern int  arch_uprobe_analyze_insn(struct arch_uprobe *aup, struct mm_struct *mm, unsigned long addr);
extern int  arch_uprobe_pre_xol(struct arch_uprobe *aup, struct pt_regs *regs);
extern int  arch_uprobe_post_xol(struct arch_uprobe *aup, struct pt_regs *regs);
extern bool arch_uprobe_xol_was_trapped(struct task_struct *tsk);
extern int  arch_uprobe_exception_notify(struct notifier_block *self, unsigned long val, void *data);
extern void arch_uprobe_abort_xol(struct arch_uprobe *aup, struct pt_regs *regs);

#endif	/* _ASM_UPROBES_H */
