/*
 * Kernel Probes (KProbes)
 *
 * Copyright (C) 2005-2006 Atmel Corporation
 * Copyright (C) IBM Corporation, 2002, 2004
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_KPROBES_H
#define __ASM_AVR32_KPROBES_H

#include <asm-generic/kprobes.h>

#define BREAKPOINT_INSTRUCTION	0xd673	/* breakpoint */

#ifdef CONFIG_KPROBES
#include <linux/types.h>

typedef u16	kprobe_opcode_t;
#define MAX_INSN_SIZE		2
#define MAX_STACK_SIZE		64	/* 32 would probably be OK */

#define kretprobe_blacklist_size 0

#define arch_remove_kprobe(p)	do { } while (0)

/* Architecture specific copy of original instruction */
struct arch_specific_insn {
	kprobe_opcode_t	insn[MAX_INSN_SIZE];
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

extern int kprobe_fault_handler(struct pt_regs *regs, int trapnr);
extern int kprobe_exceptions_notify(struct notifier_block *self,
				    unsigned long val, void *data);

#define flush_insn_slot(p)	do { } while (0)

#endif /* CONFIG_KPROBES */
#endif /* __ASM_AVR32_KPROBES_H */
