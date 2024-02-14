/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/include/asm/kprobes.h
 *
 * Copyright (C) 2006, 2007 Motorola Inc.
 */

#ifndef _ARM_KPROBES_H
#define _ARM_KPROBES_H

#include <asm-generic/kprobes.h>

#ifdef CONFIG_KPROBES
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/notifier.h>

#define __ARCH_WANT_KPROBES_INSN_SLOT
#define MAX_INSN_SIZE			2

#define flush_insn_slot(p)		do { } while (0)
#define kretprobe_blacklist_size	0

typedef u32 kprobe_opcode_t;
struct kprobe;
#include <asm/probes.h>

#define	arch_specific_insn	arch_probes_insn

struct prev_kprobe {
	struct kprobe *kp;
	unsigned int status;
};

/* per-cpu kprobe control block */
struct kprobe_ctlblk {
	unsigned int kprobe_status;
	struct prev_kprobe prev_kprobe;
};

void arch_remove_kprobe(struct kprobe *);
int kprobe_fault_handler(struct pt_regs *regs, unsigned int fsr);

/* optinsn template addresses */
extern __visible kprobe_opcode_t optprobe_template_entry[];
extern __visible kprobe_opcode_t optprobe_template_val[];
extern __visible kprobe_opcode_t optprobe_template_call[];
extern __visible kprobe_opcode_t optprobe_template_end[];
extern __visible kprobe_opcode_t optprobe_template_sub_sp[];
extern __visible kprobe_opcode_t optprobe_template_add_sp[];
extern __visible kprobe_opcode_t optprobe_template_restore_begin[];
extern __visible kprobe_opcode_t optprobe_template_restore_orig_insn[];
extern __visible kprobe_opcode_t optprobe_template_restore_end[];

#define MAX_OPTIMIZED_LENGTH	4
#define MAX_OPTINSN_SIZE				\
	((unsigned long)optprobe_template_end -	\
	 (unsigned long)optprobe_template_entry)
#define RELATIVEJUMP_SIZE	4

struct arch_optimized_insn {
	/*
	 * copy of the original instructions.
	 * Different from x86, ARM kprobe_opcode_t is u32.
	 */
#define MAX_COPIED_INSN	DIV_ROUND_UP(RELATIVEJUMP_SIZE, sizeof(kprobe_opcode_t))
	kprobe_opcode_t copied_insn[MAX_COPIED_INSN];
	/* detour code buffer */
	kprobe_opcode_t *insn;
	/*
	 * We always copy one instruction on ARM,
	 * so size will always be 4, and unlike x86, there is no
	 * need for a size field.
	 */
};

#endif /* CONFIG_KPROBES */
#endif /* _ARM_KPROBES_H */
