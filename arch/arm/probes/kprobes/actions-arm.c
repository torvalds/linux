// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/probes/kprobes/actions-arm.c
 *
 * Copyright (C) 2006, 2007 Motorola Inc.
 */

/*
 * We do not have hardware single-stepping on ARM, This
 * effort is further complicated by the ARM not having a
 * "next PC" register.  Instructions that change the PC
 * can't be safely single-stepped in a MP environment, so
 * we have a lot of work to do:
 *
 * In the prepare phase:
 *   *) If it is an instruction that does anything
 *      with the CPU mode, we reject it for a kprobe.
 *      (This is out of laziness rather than need.  The
 *      instructions could be simulated.)
 *
 *   *) Otherwise, decode the instruction rewriting its
 *      registers to take fixed, ordered registers and
 *      setting a handler for it to run the instruction.
 *
 * In the execution phase by an instruction's handler:
 *
 *   *) If the PC is written to by the instruction, the
 *      instruction must be fully simulated in software.
 *
 *   *) Otherwise, a modified form of the instruction is
 *      directly executed.  Its handler calls the
 *      instruction in insn[0].  In insn[1] is a
 *      "mov pc, lr" to return.
 *
 *      Before calling, load up the reordered registers
 *      from the original instruction's registers.  If one
 *      of the original input registers is the PC, compute
 *      and adjust the appropriate input register.
 *
 *	After call completes, copy the output registers to
 *      the original instruction's original registers.
 *
 * We don't use a real breakpoint instruction since that
 * would have us in the kernel go from SVC mode to SVC
 * mode losing the link register.  Instead we use an
 * undefined instruction.  To simplify processing, the
 * undefined instruction used for kprobes must be reserved
 * exclusively for kprobes use.
 *
 * TODO: ifdef out some instruction decoding based on architecture.
 */

#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>

#include "../decode-arm.h"
#include "core.h"
#include "checkers.h"

#if  __LINUX_ARM_ARCH__ >= 6
#define BLX(reg)	"blx	"reg"		\n\t"
#else
#define BLX(reg)	"mov	lr, pc		\n\t"	\
			"mov	pc, "reg"	\n\t"
#endif

static void __kprobes
emulate_ldrdstrd(probes_opcode_t insn,
	struct arch_probes_insn *asi, struct pt_regs *regs)
{
	unsigned long pc = regs->ARM_pc + 4;
	int rt = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;

	register unsigned long rtv asm("r0") = regs->uregs[rt];
	register unsigned long rt2v asm("r1") = regs->uregs[rt+1];
	register unsigned long rnv asm("r2") = (rn == 15) ? pc
							  : regs->uregs[rn];
	register unsigned long rmv asm("r3") = regs->uregs[rm];

	__asm__ __volatile__ (
		BLX("%[fn]")
		: "=r" (rtv), "=r" (rt2v), "=r" (rnv)
		: "0" (rtv), "1" (rt2v), "2" (rnv), "r" (rmv),
		  [fn] "r" (asi->insn_fn)
		: "lr", "memory", "cc"
	);

	regs->uregs[rt] = rtv;
	regs->uregs[rt+1] = rt2v;
	if (is_writeback(insn))
		regs->uregs[rn] = rnv;
}

static void __kprobes
emulate_ldr(probes_opcode_t insn,
	struct arch_probes_insn *asi, struct pt_regs *regs)
{
	unsigned long pc = regs->ARM_pc + 4;
	int rt = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;

	register unsigned long rtv asm("r0");
	register unsigned long rnv asm("r2") = (rn == 15) ? pc
							  : regs->uregs[rn];
	register unsigned long rmv asm("r3") = regs->uregs[rm];

	__asm__ __volatile__ (
		BLX("%[fn]")
		: "=r" (rtv), "=r" (rnv)
		: "1" (rnv), "r" (rmv), [fn] "r" (asi->insn_fn)
		: "lr", "memory", "cc"
	);

	if (rt == 15)
		load_write_pc(rtv, regs);
	else
		regs->uregs[rt] = rtv;

	if (is_writeback(insn))
		regs->uregs[rn] = rnv;
}

static void __kprobes
emulate_str(probes_opcode_t insn,
	struct arch_probes_insn *asi, struct pt_regs *regs)
{
	unsigned long rtpc = regs->ARM_pc - 4 + str_pc_offset;
	unsigned long rnpc = regs->ARM_pc + 4;
	int rt = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;

	register unsigned long rtv asm("r0") = (rt == 15) ? rtpc
							  : regs->uregs[rt];
	register unsigned long rnv asm("r2") = (rn == 15) ? rnpc
							  : regs->uregs[rn];
	register unsigned long rmv asm("r3") = regs->uregs[rm];

	__asm__ __volatile__ (
		BLX("%[fn]")
		: "=r" (rnv)
		: "r" (rtv), "0" (rnv), "r" (rmv), [fn] "r" (asi->insn_fn)
		: "lr", "memory", "cc"
	);

	if (is_writeback(insn))
		regs->uregs[rn] = rnv;
}

static void __kprobes
emulate_rd12rn16rm0rs8_rwflags(probes_opcode_t insn,
	struct arch_probes_insn *asi, struct pt_regs *regs)
{
	unsigned long pc = regs->ARM_pc + 4;
	int rd = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;
	int rs = (insn >> 8) & 0xf;

	register unsigned long rdv asm("r0") = regs->uregs[rd];
	register unsigned long rnv asm("r2") = (rn == 15) ? pc
							  : regs->uregs[rn];
	register unsigned long rmv asm("r3") = (rm == 15) ? pc
							  : regs->uregs[rm];
	register unsigned long rsv asm("r1") = regs->uregs[rs];
	unsigned long cpsr = regs->ARM_cpsr;

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[cpsr]	\n\t"
		BLX("%[fn]")
		"mrs	%[cpsr], cpsr		\n\t"
		: "=r" (rdv), [cpsr] "=r" (cpsr)
		: "0" (rdv), "r" (rnv), "r" (rmv), "r" (rsv),
		  "1" (cpsr), [fn] "r" (asi->insn_fn)
		: "lr", "memory", "cc"
	);

	if (rd == 15)
		alu_write_pc(rdv, regs);
	else
		regs->uregs[rd] = rdv;
	regs->ARM_cpsr = (regs->ARM_cpsr & ~APSR_MASK) | (cpsr & APSR_MASK);
}

static void __kprobes
emulate_rd12rn16rm0_rwflags_nopc(probes_opcode_t insn,
	struct arch_probes_insn *asi, struct pt_regs *regs)
{
	int rd = (insn >> 12) & 0xf;
	int rn = (insn >> 16) & 0xf;
	int rm = insn & 0xf;

	register unsigned long rdv asm("r0") = regs->uregs[rd];
	register unsigned long rnv asm("r2") = regs->uregs[rn];
	register unsigned long rmv asm("r3") = regs->uregs[rm];
	unsigned long cpsr = regs->ARM_cpsr;

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[cpsr]	\n\t"
		BLX("%[fn]")
		"mrs	%[cpsr], cpsr		\n\t"
		: "=r" (rdv), [cpsr] "=r" (cpsr)
		: "0" (rdv), "r" (rnv), "r" (rmv),
		  "1" (cpsr), [fn] "r" (asi->insn_fn)
		: "lr", "memory", "cc"
	);

	regs->uregs[rd] = rdv;
	regs->ARM_cpsr = (regs->ARM_cpsr & ~APSR_MASK) | (cpsr & APSR_MASK);
}

static void __kprobes
emulate_rd16rn12rm0rs8_rwflags_nopc(probes_opcode_t insn,
	struct arch_probes_insn *asi,
	struct pt_regs *regs)
{
	int rd = (insn >> 16) & 0xf;
	int rn = (insn >> 12) & 0xf;
	int rm = insn & 0xf;
	int rs = (insn >> 8) & 0xf;

	register unsigned long rdv asm("r2") = regs->uregs[rd];
	register unsigned long rnv asm("r0") = regs->uregs[rn];
	register unsigned long rmv asm("r3") = regs->uregs[rm];
	register unsigned long rsv asm("r1") = regs->uregs[rs];
	unsigned long cpsr = regs->ARM_cpsr;

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[cpsr]	\n\t"
		BLX("%[fn]")
		"mrs	%[cpsr], cpsr		\n\t"
		: "=r" (rdv), [cpsr] "=r" (cpsr)
		: "0" (rdv), "r" (rnv), "r" (rmv), "r" (rsv),
		  "1" (cpsr), [fn] "r" (asi->insn_fn)
		: "lr", "memory", "cc"
	);

	regs->uregs[rd] = rdv;
	regs->ARM_cpsr = (regs->ARM_cpsr & ~APSR_MASK) | (cpsr & APSR_MASK);
}

static void __kprobes
emulate_rd12rm0_noflags_nopc(probes_opcode_t insn,
	struct arch_probes_insn *asi, struct pt_regs *regs)
{
	int rd = (insn >> 12) & 0xf;
	int rm = insn & 0xf;

	register unsigned long rdv asm("r0") = regs->uregs[rd];
	register unsigned long rmv asm("r3") = regs->uregs[rm];

	__asm__ __volatile__ (
		BLX("%[fn]")
		: "=r" (rdv)
		: "0" (rdv), "r" (rmv), [fn] "r" (asi->insn_fn)
		: "lr", "memory", "cc"
	);

	regs->uregs[rd] = rdv;
}

static void __kprobes
emulate_rdlo12rdhi16rn0rm8_rwflags_nopc(probes_opcode_t insn,
	struct arch_probes_insn *asi,
	struct pt_regs *regs)
{
	int rdlo = (insn >> 12) & 0xf;
	int rdhi = (insn >> 16) & 0xf;
	int rn = insn & 0xf;
	int rm = (insn >> 8) & 0xf;

	register unsigned long rdlov asm("r0") = regs->uregs[rdlo];
	register unsigned long rdhiv asm("r2") = regs->uregs[rdhi];
	register unsigned long rnv asm("r3") = regs->uregs[rn];
	register unsigned long rmv asm("r1") = regs->uregs[rm];
	unsigned long cpsr = regs->ARM_cpsr;

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[cpsr]	\n\t"
		BLX("%[fn]")
		"mrs	%[cpsr], cpsr		\n\t"
		: "=r" (rdlov), "=r" (rdhiv), [cpsr] "=r" (cpsr)
		: "0" (rdlov), "1" (rdhiv), "r" (rnv), "r" (rmv),
		  "2" (cpsr), [fn] "r" (asi->insn_fn)
		: "lr", "memory", "cc"
	);

	regs->uregs[rdlo] = rdlov;
	regs->uregs[rdhi] = rdhiv;
	regs->ARM_cpsr = (regs->ARM_cpsr & ~APSR_MASK) | (cpsr & APSR_MASK);
}

const union decode_action kprobes_arm_actions[NUM_PROBES_ARM_ACTIONS] = {
	[PROBES_PRELOAD_IMM] = {.handler = probes_simulate_nop},
	[PROBES_PRELOAD_REG] = {.handler = probes_simulate_nop},
	[PROBES_BRANCH_IMM] = {.handler = simulate_blx1},
	[PROBES_MRS] = {.handler = simulate_mrs},
	[PROBES_BRANCH_REG] = {.handler = simulate_blx2bx},
	[PROBES_CLZ] = {.handler = emulate_rd12rm0_noflags_nopc},
	[PROBES_SATURATING_ARITHMETIC] = {
		.handler = emulate_rd12rn16rm0_rwflags_nopc},
	[PROBES_MUL1] = {.handler = emulate_rdlo12rdhi16rn0rm8_rwflags_nopc},
	[PROBES_MUL2] = {.handler = emulate_rd16rn12rm0rs8_rwflags_nopc},
	[PROBES_SWP] = {.handler = emulate_rd12rn16rm0_rwflags_nopc},
	[PROBES_LDRSTRD] = {.handler = emulate_ldrdstrd},
	[PROBES_LOAD_EXTRA] = {.handler = emulate_ldr},
	[PROBES_LOAD] = {.handler = emulate_ldr},
	[PROBES_STORE_EXTRA] = {.handler = emulate_str},
	[PROBES_STORE] = {.handler = emulate_str},
	[PROBES_MOV_IP_SP] = {.handler = simulate_mov_ipsp},
	[PROBES_DATA_PROCESSING_REG] = {
		.handler = emulate_rd12rn16rm0rs8_rwflags},
	[PROBES_DATA_PROCESSING_IMM] = {
		.handler = emulate_rd12rn16rm0rs8_rwflags},
	[PROBES_MOV_HALFWORD] = {.handler = emulate_rd12rm0_noflags_nopc},
	[PROBES_SEV] = {.handler = probes_emulate_none},
	[PROBES_WFE] = {.handler = probes_simulate_nop},
	[PROBES_SATURATE] = {.handler = emulate_rd12rn16rm0_rwflags_nopc},
	[PROBES_REV] = {.handler = emulate_rd12rm0_noflags_nopc},
	[PROBES_MMI] = {.handler = emulate_rd12rn16rm0_rwflags_nopc},
	[PROBES_PACK] = {.handler = emulate_rd12rn16rm0_rwflags_nopc},
	[PROBES_EXTEND] = {.handler = emulate_rd12rm0_noflags_nopc},
	[PROBES_EXTEND_ADD] = {.handler = emulate_rd12rn16rm0_rwflags_nopc},
	[PROBES_MUL_ADD_LONG] = {
		.handler = emulate_rdlo12rdhi16rn0rm8_rwflags_nopc},
	[PROBES_MUL_ADD] = {.handler = emulate_rd16rn12rm0rs8_rwflags_nopc},
	[PROBES_BITFIELD] = {.handler = emulate_rd12rm0_noflags_nopc},
	[PROBES_BRANCH] = {.handler = simulate_bbl},
	[PROBES_LDMSTM] = {.decoder = kprobe_decode_ldmstm}
};

const struct decode_checker *kprobes_arm_checkers[] = {arm_stack_checker, arm_regs_checker, NULL};
