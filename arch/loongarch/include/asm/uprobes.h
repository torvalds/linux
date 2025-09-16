/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_LOONGARCH_UPROBES_H
#define __ASM_LOONGARCH_UPROBES_H

#include <asm/inst.h>

typedef u32 uprobe_opcode_t;

#define MAX_UINSN_BYTES		8
#define UPROBE_XOL_SLOT_BYTES	MAX_UINSN_BYTES

#define UPROBE_SWBP_INSN	__emit_break(BRK_UPROBE_BP)
#define UPROBE_SWBP_INSN_SIZE	LOONGARCH_INSN_SIZE

#define UPROBE_XOLBP_INSN	__emit_break(BRK_UPROBE_XOLBP)

struct arch_uprobe {
	u32	insn[2];
	u32	ixol[2];
	bool	simulate;
};

struct arch_uprobe_task {
	unsigned long saved_trap_nr;
};

#ifdef CONFIG_UPROBES
bool uprobe_breakpoint_handler(struct pt_regs *regs);
bool uprobe_singlestep_handler(struct pt_regs *regs);
#else /* !CONFIG_UPROBES */
static inline bool uprobe_breakpoint_handler(struct pt_regs *regs) { return false; }
static inline bool uprobe_singlestep_handler(struct pt_regs *regs) { return false; }
#endif /* CONFIG_UPROBES */

#endif /* __ASM_LOONGARCH_UPROBES_H */
