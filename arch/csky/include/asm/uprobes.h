/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ASM_CSKY_UPROBES_H
#define __ASM_CSKY_UPROBES_H

#include <asm/probes.h>

#define MAX_UINSN_BYTES		4

#define UPROBE_SWBP_INSN	USR_BKPT
#define UPROBE_SWBP_INSN_SIZE	2
#define UPROBE_XOL_SLOT_BYTES	MAX_UINSN_BYTES

typedef u32 uprobe_opcode_t;

struct arch_uprobe_task {
	unsigned long   saved_trap_no;
};

struct arch_uprobe {
	union {
		u8 insn[MAX_UINSN_BYTES];
		u8 ixol[MAX_UINSN_BYTES];
	};
	struct arch_probe_insn api;
	unsigned long insn_size;
	bool simulate;
};

int uprobe_breakpoint_handler(struct pt_regs *regs);
int uprobe_single_step_handler(struct pt_regs *regs);

#endif /* __ASM_CSKY_UPROBES_H */
