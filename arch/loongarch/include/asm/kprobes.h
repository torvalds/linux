/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_LOONGARCH_KPROBES_H
#define __ASM_LOONGARCH_KPROBES_H

#include <asm-generic/kprobes.h>

#ifdef CONFIG_KPROBES

#include <asm/inst.h>
#include <asm/cacheflush.h>

#define __ARCH_WANT_KPROBES_INSN_SLOT
#define MAX_INSN_SIZE			2

#define flush_insn_slot(p)						\
do {									\
	if (p->addr)							\
		flush_icache_range((unsigned long)p->addr,		\
			   (unsigned long)p->addr +			\
			   (MAX_INSN_SIZE * sizeof(kprobe_opcode_t)));	\
} while (0)

#define kretprobe_blacklist_size	0

typedef union loongarch_instruction kprobe_opcode_t;

/* Architecture specific copy of original instruction */
struct arch_specific_insn {
	/* copy of the original instruction */
	kprobe_opcode_t *insn;
	/* restore address after simulation */
	unsigned long restore;
};

struct prev_kprobe {
	struct kprobe *kp;
	unsigned int status;
};

/* per-cpu kprobe control block */
struct kprobe_ctlblk {
	unsigned int kprobe_status;
	unsigned long saved_status;
	struct prev_kprobe prev_kprobe;
};

void arch_remove_kprobe(struct kprobe *p);
bool kprobe_fault_handler(struct pt_regs *regs, int trapnr);
bool kprobe_breakpoint_handler(struct pt_regs *regs);
bool kprobe_singlestep_handler(struct pt_regs *regs);

#else /* !CONFIG_KPROBES */

static inline bool kprobe_breakpoint_handler(struct pt_regs *regs) { return false; }
static inline bool kprobe_singlestep_handler(struct pt_regs *regs) { return false; }

#endif /* CONFIG_KPROBES */
#endif /* __ASM_LOONGARCH_KPROBES_H */
