/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ASM_CSKY_KPROBES_H
#define __ASM_CSKY_KPROBES_H

#include <asm-generic/kprobes.h>

#ifdef CONFIG_KPROBES
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/percpu.h>

#define __ARCH_WANT_KPROBES_INSN_SLOT
#define MAX_INSN_SIZE			1

#define flush_insn_slot(p)		do { } while (0)
#define kretprobe_blacklist_size	0

#include <asm/probes.h>

struct prev_kprobe {
	struct kprobe *kp;
	unsigned int status;
};

/* Single step context for kprobe */
struct kprobe_step_ctx {
	unsigned long ss_pending;
	unsigned long match_addr;
};

/* per-cpu kprobe control block */
struct kprobe_ctlblk {
	unsigned int kprobe_status;
	unsigned long saved_sr;
	struct prev_kprobe prev_kprobe;
	struct kprobe_step_ctx ss_ctx;
};

void arch_remove_kprobe(struct kprobe *p);
int kprobe_fault_handler(struct pt_regs *regs, unsigned int trapnr);
int kprobe_breakpoint_handler(struct pt_regs *regs);
int kprobe_single_step_handler(struct pt_regs *regs);
void __kretprobe_trampoline(void);
void __kprobes *trampoline_probe_handler(struct pt_regs *regs);

#endif /* CONFIG_KPROBES */
#endif /* __ASM_CSKY_KPROBES_H */
