#ifndef _ASM_S390_KPROBES_H
#define _ASM_S390_KPROBES_H
/*
 *  Kernel Probes (KProbes)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2002, 2006
 *
 * 2002-Oct	Created by Vamsi Krishna S <vamsi_krishna@in.ibm.com> Kernel
 *		Probes initial implementation ( includes suggestions from
 *		Rusty Russell).
 * 2004-Nov	Modified for PPC64 by Ananth N Mavinakayanahalli
 *		<ananth@in.ibm.com>
 * 2005-Dec	Used as a template for s390 by Mike Grundy
 *		<grundym@us.ibm.com>
 */
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/percpu.h>

struct pt_regs;
struct kprobe;

typedef u16 kprobe_opcode_t;
#define BREAKPOINT_INSTRUCTION	0x0002

/* Maximum instruction size is 3 (16bit) halfwords: */
#define MAX_INSN_SIZE		0x0003
#define MAX_STACK_SIZE		64
#define MIN_STACK_SIZE(ADDR) (((MAX_STACK_SIZE) < \
	(((unsigned long)current_thread_info()) + THREAD_SIZE - (ADDR))) \
	? (MAX_STACK_SIZE) \
	: (((unsigned long)current_thread_info()) + THREAD_SIZE - (ADDR)))

#define kretprobe_blacklist_size 0

#define KPROBE_SWAP_INST	0x10

#define FIXUP_PSW_NORMAL	0x08
#define FIXUP_BRANCH_NOT_TAKEN	0x04
#define FIXUP_RETURN_REGISTER	0x02
#define FIXUP_NOT_REQUIRED	0x01

/* Architecture specific copy of original instruction */
struct arch_specific_insn {
	/* copy of original instruction */
	kprobe_opcode_t insn[MAX_INSN_SIZE];
};

struct prev_kprobe {
	struct kprobe *kp;
	unsigned long status;
};

/* per-cpu kprobe control block */
struct kprobe_ctlblk {
	unsigned long kprobe_status;
	unsigned long kprobe_saved_imask;
	unsigned long kprobe_saved_ctl[3];
	struct prev_kprobe prev_kprobe;
	struct pt_regs jprobe_saved_regs;
	kprobe_opcode_t jprobes_stack[MAX_STACK_SIZE];
};

void arch_remove_kprobe(struct kprobe *p);
void kretprobe_trampoline(void);

int kprobe_fault_handler(struct pt_regs *regs, int trapnr);
int kprobe_exceptions_notify(struct notifier_block *self,
	unsigned long val, void *data);

#define flush_insn_slot(p)	do { } while (0)

#endif	/* _ASM_S390_KPROBES_H */
