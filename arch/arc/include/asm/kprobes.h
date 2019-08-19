/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef _ARC_KPROBES_H
#define _ARC_KPROBES_H

#include <asm-generic/kprobes.h>

#ifdef CONFIG_KPROBES

typedef u16 kprobe_opcode_t;

#define UNIMP_S_INSTRUCTION 0x79e0
#define TRAP_S_2_INSTRUCTION 0x785e

#define MAX_INSN_SIZE   8
#define MAX_STACK_SIZE  64

struct arch_specific_insn {
	int is_short;
	kprobe_opcode_t *t1_addr, *t2_addr;
	kprobe_opcode_t t1_opcode, t2_opcode;
};

#define flush_insn_slot(p)  do {  } while (0)

#define kretprobe_blacklist_size    0

struct kprobe;

void arch_remove_kprobe(struct kprobe *p);

int kprobe_exceptions_notify(struct notifier_block *self,
			     unsigned long val, void *data);

struct prev_kprobe {
	struct kprobe *kp;
	unsigned long status;
};

struct kprobe_ctlblk {
	unsigned int kprobe_status;
	struct prev_kprobe prev_kprobe;
};

int kprobe_fault_handler(struct pt_regs *regs, unsigned long cause);
void kretprobe_trampoline(void);
void trap_is_kprobe(unsigned long address, struct pt_regs *regs);
#else
#define trap_is_kprobe(address, regs)
#endif /* CONFIG_KPROBES */

#endif /* _ARC_KPROBES_H */
