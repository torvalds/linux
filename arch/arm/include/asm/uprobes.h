/*
 * Copyright (C) 2012 Rabin Vincent <rabin at rab.in>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_UPROBES_H
#define _ASM_UPROBES_H

#include <asm/probes.h>
#include <asm/opcodes.h>

typedef u32 uprobe_opcode_t;

#define MAX_UINSN_BYTES		4
#define UPROBE_XOL_SLOT_BYTES	64

#define UPROBE_SWBP_ARM_INSN	0xe7f001f9
#define UPROBE_SS_ARM_INSN	0xe7f001fa
#define UPROBE_SWBP_INSN	__opcode_to_mem_arm(UPROBE_SWBP_ARM_INSN)
#define UPROBE_SWBP_INSN_SIZE	4

struct arch_uprobe_task {
	u32 backup;
	unsigned long	saved_trap_no;
};

struct arch_uprobe {
	u8 insn[MAX_UINSN_BYTES];
	unsigned long ixol[2];
	uprobe_opcode_t bpinsn;
	bool simulate;
	u32 pcreg;
	void (*prehandler)(struct arch_uprobe *auprobe,
			   struct arch_uprobe_task *autask,
			   struct pt_regs *regs);
	void (*posthandler)(struct arch_uprobe *auprobe,
			    struct arch_uprobe_task *autask,
			    struct pt_regs *regs);
	struct arch_probes_insn asi;
};

#endif
