/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/parisc/include/asm/kprobes.h
 *
 * PA-RISC kprobes implementation
 *
 * Copyright (c) 2019 Sven Schnelle <svens@stackframe.org>
 */

#ifndef _PARISC_KPROBES_H
#define _PARISC_KPROBES_H

#ifdef CONFIG_KPROBES

#include <asm-generic/kprobes.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/notifier.h>

#define PARISC_KPROBES_BREAK_INSN	0x3ff801f
#define  __ARCH_WANT_KPROBES_INSN_SLOT
#define MAX_INSN_SIZE 1

typedef u32 kprobe_opcode_t;
struct kprobe;

void arch_remove_kprobe(struct kprobe *p);

#define flush_insn_slot(p) \
	flush_icache_range((unsigned long)&(p)->ainsn.insn[0], \
			   (unsigned long)&(p)->ainsn.insn[0] + \
			   sizeof(kprobe_opcode_t))

#define kretprobe_blacklist_size    0

struct arch_specific_insn {
	kprobe_opcode_t *insn;
};

struct prev_kprobe {
	struct kprobe *kp;
	unsigned long status;
};

struct kprobe_ctlblk {
	unsigned int kprobe_status;
	struct prev_kprobe prev_kprobe;
	unsigned long iaoq[2];
};

int __kprobes parisc_kprobe_break_handler(struct pt_regs *regs);
int __kprobes parisc_kprobe_ss_handler(struct pt_regs *regs);

#endif /* CONFIG_KPROBES */
#endif /* _PARISC_KPROBES_H */
