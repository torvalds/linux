/*
 * arch/arm/kernel/probes-arm.h
 *
 * Copyright 2013 Linaro Ltd.
 * Written by: David A. Long
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef _ARM_KERNEL_PROBES_ARM_H
#define  _ARM_KERNEL_PROBES_ARM_H

enum probes_arm_action {
	PROBES_EMULATE_NONE,
	PROBES_SIMULATE_NOP,
	PROBES_PRELOAD_IMM,
	PROBES_PRELOAD_REG,
	PROBES_BRANCH_IMM,
	PROBES_BRANCH_REG,
	PROBES_MRS,
	PROBES_CLZ,
	PROBES_SATURATING_ARITHMETIC,
	PROBES_MUL1,
	PROBES_MUL2,
	PROBES_SWP,
	PROBES_LDRSTRD,
	PROBES_LOAD,
	PROBES_STORE,
	PROBES_LOAD_EXTRA,
	PROBES_STORE_EXTRA,
	PROBES_MOV_IP_SP,
	PROBES_DATA_PROCESSING_REG,
	PROBES_DATA_PROCESSING_IMM,
	PROBES_MOV_HALFWORD,
	PROBES_SEV,
	PROBES_WFE,
	PROBES_SATURATE,
	PROBES_REV,
	PROBES_MMI,
	PROBES_PACK,
	PROBES_EXTEND,
	PROBES_EXTEND_ADD,
	PROBES_MUL_ADD_LONG,
	PROBES_MUL_ADD,
	PROBES_BITFIELD,
	PROBES_BRANCH,
	PROBES_LDMSTM,
	NUM_PROBES_ARM_ACTIONS
};

void __kprobes simulate_bbl(probes_opcode_t opcode,
	struct arch_probes_insn *asi, struct pt_regs *regs);
void __kprobes simulate_blx1(probes_opcode_t opcode,
	struct arch_probes_insn *asi, struct pt_regs *regs);
void __kprobes simulate_blx2bx(probes_opcode_t opcode,
	struct arch_probes_insn *asi, struct pt_regs *regs);
void __kprobes simulate_mrs(probes_opcode_t opcode,
	struct arch_probes_insn *asi, struct pt_regs *regs);
void __kprobes simulate_mov_ipsp(probes_opcode_t opcode,
	struct arch_probes_insn *asi, struct pt_regs *regs);

extern const union decode_item probes_decode_arm_table[];

enum probes_insn arm_probes_decode_insn(probes_opcode_t,
		struct arch_probes_insn *, bool emulate,
		const union decode_action *actions);

#endif
