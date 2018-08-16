#ifndef _ASM_POWERPC_KPROBES_H
#define _ASM_POWERPC_KPROBES_H

#include <asm-generic/kprobes.h>

#ifdef __KERNEL__
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
 * Copyright (C) IBM Corporation, 2002, 2004
 *
 * 2002-Oct	Created by Vamsi Krishna S <vamsi_krishna@in.ibm.com> Kernel
 *		Probes initial implementation ( includes suggestions from
 *		Rusty Russell).
 * 2004-Nov	Modified for PPC64 by Ananth N Mavinakayanahalli
 *		<ananth@in.ibm.com>
 */
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/percpu.h>
#include <linux/module.h>
#include <asm/probes.h>
#include <asm/code-patching.h>

#ifdef CONFIG_KPROBES
#define  __ARCH_WANT_KPROBES_INSN_SLOT

struct pt_regs;
struct kprobe;

typedef ppc_opcode_t kprobe_opcode_t;

extern kprobe_opcode_t optinsn_slot;

/* Optinsn template address */
extern kprobe_opcode_t optprobe_template_entry[];
extern kprobe_opcode_t optprobe_template_op_address[];
extern kprobe_opcode_t optprobe_template_call_handler[];
extern kprobe_opcode_t optprobe_template_insn[];
extern kprobe_opcode_t optprobe_template_call_emulate[];
extern kprobe_opcode_t optprobe_template_ret[];
extern kprobe_opcode_t optprobe_template_end[];

/* Fixed instruction size for powerpc */
#define MAX_INSN_SIZE		1
#define MAX_OPTIMIZED_LENGTH	sizeof(kprobe_opcode_t)	/* 4 bytes */
#define MAX_OPTINSN_SIZE	(optprobe_template_end - optprobe_template_entry)
#define RELATIVEJUMP_SIZE	sizeof(kprobe_opcode_t)	/* 4 bytes */

#define flush_insn_slot(p)	do { } while (0)
#define kretprobe_blacklist_size 0

void kretprobe_trampoline(void);
extern void arch_remove_kprobe(struct kprobe *p);

/* Architecture specific copy of original instruction */
struct arch_specific_insn {
	/* copy of original instruction */
	kprobe_opcode_t *insn;
	/*
	 * Set in kprobes code, initially to 0. If the instruction can be
	 * eumulated, this is set to 1, if not, to -1.
	 */
	int boostable;
};

struct prev_kprobe {
	struct kprobe *kp;
	unsigned long status;
	unsigned long saved_msr;
};

/* per-cpu kprobe control block */
struct kprobe_ctlblk {
	unsigned long kprobe_status;
	unsigned long kprobe_saved_msr;
	struct prev_kprobe prev_kprobe;
};

struct arch_optimized_insn {
	kprobe_opcode_t copied_insn[1];
	/* detour buffer */
	kprobe_opcode_t *insn;
};

extern int kprobe_exceptions_notify(struct notifier_block *self,
					unsigned long val, void *data);
extern int kprobe_fault_handler(struct pt_regs *regs, int trapnr);
extern int kprobe_handler(struct pt_regs *regs);
extern int kprobe_post_handler(struct pt_regs *regs);
#else
static inline int kprobe_handler(struct pt_regs *regs) { return 0; }
static inline int kprobe_post_handler(struct pt_regs *regs) { return 0; }
#endif /* CONFIG_KPROBES */
#endif /* __KERNEL__ */
#endif	/* _ASM_POWERPC_KPROBES_H */
