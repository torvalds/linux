/*
 * arch/tile/include/asm/kprobes.h
 *
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_KPROBES_H
#define _ASM_TILE_KPROBES_H

#include <asm-generic/kprobes.h>

#ifdef CONFIG_KPROBES

#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/percpu.h>
#include <arch/opcode.h>

#define __ARCH_WANT_KPROBES_INSN_SLOT
#define MAX_INSN_SIZE			2

#define kretprobe_blacklist_size 0

typedef tile_bundle_bits kprobe_opcode_t;

#define flush_insn_slot(p)						\
	flush_icache_range((unsigned long)p->addr,			\
			   (unsigned long)p->addr +			\
			   (MAX_INSN_SIZE * sizeof(kprobe_opcode_t)))

struct kprobe;

/* Architecture specific copy of original instruction. */
struct arch_specific_insn {
	kprobe_opcode_t *insn;
};

struct prev_kprobe {
	struct kprobe *kp;
	unsigned long status;
	unsigned long saved_pc;
};

#define MAX_JPROBES_STACK_SIZE 128
#define MAX_JPROBES_STACK_ADDR \
	(((unsigned long)current_thread_info()) + THREAD_SIZE - 32 \
		- sizeof(struct pt_regs))

#define MIN_JPROBES_STACK_SIZE(ADDR)					\
	((((ADDR) + MAX_JPROBES_STACK_SIZE) > MAX_JPROBES_STACK_ADDR)	\
		? MAX_JPROBES_STACK_ADDR - (ADDR)			\
		: MAX_JPROBES_STACK_SIZE)

/* per-cpu kprobe control block. */
struct kprobe_ctlblk {
	unsigned long kprobe_status;
	unsigned long kprobe_saved_pc;
	unsigned long jprobe_saved_sp;
	struct prev_kprobe prev_kprobe;
	struct pt_regs jprobe_saved_regs;
	char jprobes_stack[MAX_JPROBES_STACK_SIZE];
};

extern tile_bundle_bits breakpoint2_insn;
extern tile_bundle_bits breakpoint_insn;

void arch_remove_kprobe(struct kprobe *);

extern int kprobe_exceptions_notify(struct notifier_block *self,
			     unsigned long val, void *data);

#endif /* CONFIG_KPROBES */
#endif /* _ASM_TILE_KPROBES_H */
