/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef _ASM_S390_KPROBES_H
#define _ASM_S390_KPROBES_H
/*
 *  Kernel Probes (KProbes)
 *
 * Copyright IBM Corp. 2002, 2006
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
#include <asm-generic/kprobes.h>

#define BREAKPOINT_INSTRUCTION	0x0002

#define FIXUP_PSW_NORMAL	0x08
#define FIXUP_BRANCH_NOT_TAKEN	0x04
#define FIXUP_RETURN_REGISTER	0x02
#define FIXUP_NOT_REQUIRED	0x01

int probe_is_prohibited_opcode(u16 *insn);
int probe_get_fixup_type(u16 *insn);
int probe_is_insn_relative_long(u16 *insn);

#ifdef CONFIG_KPROBES
#include <linux/ptrace.h>
#include <linux/percpu.h>
#include <linux/sched/task_stack.h>

#define __ARCH_WANT_KPROBES_INSN_SLOT

struct pt_regs;
struct kprobe;

typedef u16 kprobe_opcode_t;

/* Maximum instruction size is 3 (16bit) halfwords: */
#define MAX_INSN_SIZE		0x0003
#define MAX_STACK_SIZE		64
#define MIN_STACK_SIZE(ADDR) (((MAX_STACK_SIZE) < \
	(((unsigned long)task_stack_page(current)) + THREAD_SIZE - (ADDR))) \
	? (MAX_STACK_SIZE) \
	: (((unsigned long)task_stack_page(current)) + THREAD_SIZE - (ADDR)))

#define kretprobe_blacklist_size 0

/* Architecture specific copy of original instruction */
struct arch_specific_insn {
	/* copy of original instruction */
	kprobe_opcode_t *insn;
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
};

void arch_remove_kprobe(struct kprobe *p);

int kprobe_fault_handler(struct pt_regs *regs, int trapnr);
int kprobe_exceptions_notify(struct notifier_block *self,
	unsigned long val, void *data);

#define flush_insn_slot(p)	do { } while (0)

#endif /* CONFIG_KPROBES */
#endif	/* _ASM_S390_KPROBES_H */
