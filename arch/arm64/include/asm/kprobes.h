/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm64/include/asm/kprobes.h
 *
 * Copyright (C) 2013 Linaro Limited
 */

#ifndef _ARM_KPROBES_H
#define _ARM_KPROBES_H

#include <asm-generic/kprobes.h>

#ifdef CONFIG_KPROBES
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/percpu.h>

#define __ARCH_WANT_KPROBES_INSN_SLOT
#define MAX_INSN_SIZE			2

#define flush_insn_slot(p)		do { } while (0)
#define kretprobe_blacklist_size	0

#include <asm/probes.h>

struct prev_kprobe {
	struct kprobe *kp;
	unsigned int status;
};

/* per-cpu kprobe control block */
struct kprobe_ctlblk {
	unsigned int kprobe_status;
	unsigned long saved_irqflag;
	struct prev_kprobe prev_kprobe;
};

void arch_remove_kprobe(struct kprobe *);
int kprobe_fault_handler(struct pt_regs *regs, unsigned int fsr);
int kprobe_exceptions_notify(struct notifier_block *self,
			     unsigned long val, void *data);
void __kretprobe_trampoline(void);
void __kprobes *trampoline_probe_handler(struct pt_regs *regs);

#endif /* CONFIG_KPROBES */
#endif /* _ARM_KPROBES_H */
