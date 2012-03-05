/*
 * arch/arm/include/asm/kprobes.h
 *
 * Copyright (C) 2006, 2007 Motorola Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef _ARM_KPROBES_H
#define _ARM_KPROBES_H

#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/percpu.h>

#define __ARCH_WANT_KPROBES_INSN_SLOT
#define MAX_INSN_SIZE			2
#define MAX_STACK_SIZE			64	/* 32 would probably be OK */

#define flush_insn_slot(p)		do { } while (0)
#define kretprobe_blacklist_size	0

typedef u32 kprobe_opcode_t;

struct kprobe;
typedef void (kprobe_insn_handler_t)(struct kprobe *, struct pt_regs *);
typedef unsigned long (kprobe_check_cc)(unsigned long);
typedef void (kprobe_insn_singlestep_t)(struct kprobe *, struct pt_regs *);
typedef void (kprobe_insn_fn_t)(void);

/* Architecture specific copy of original instruction. */
struct arch_specific_insn {
	kprobe_opcode_t			*insn;
	kprobe_insn_handler_t		*insn_handler;
	kprobe_check_cc			*insn_check_cc;
	kprobe_insn_singlestep_t	*insn_singlestep;
	kprobe_insn_fn_t		*insn_fn;
};

struct prev_kprobe {
	struct kprobe *kp;
	unsigned int status;
};

/* per-cpu kprobe control block */
struct kprobe_ctlblk {
	unsigned int kprobe_status;
	struct prev_kprobe prev_kprobe;
	struct pt_regs jprobe_saved_regs;
	char jprobes_stack[MAX_STACK_SIZE];
};

void arch_remove_kprobe(struct kprobe *);
int kprobe_fault_handler(struct pt_regs *regs, unsigned int fsr);
int kprobe_exceptions_notify(struct notifier_block *self,
			     unsigned long val, void *data);


#endif /* _ARM_KPROBES_H */
