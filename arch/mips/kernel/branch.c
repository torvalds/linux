/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 97, 2000, 2001 by Ralf Baechle
 * Copyright (C) 2001 MIPS Technologies, Inc.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/module.h>
#include <asm/branch.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/fpu.h>
#include <asm/fpu_emulator.h>
#include <asm/inst.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>

/*
 * Calculate and return exception PC in case of branch delay
 * slot for microMIPS. It does not clear the ISA mode bit.
 */
int __isa_exception_epc(struct pt_regs *regs)
{
	long epc = regs->cp0_epc;
	unsigned short inst;

	/* Calculate exception PC in branch delay slot. */
	if (__get_user(inst, (u16 __user *) msk_isa16_mode(epc))) {
		/* This should never happen because delay slot was checked. */
		force_sig(SIGSEGV, current);
		return epc;
	}

	if (mm_insn_16bit(inst))
		epc += 2;
	else
		epc += 4;

	return epc;
}

/*
 * Compute return address and emulate branch in microMIPS mode after an
 * exception only. It does not handle compact branches/jumps and cannot
 * be used in interrupt context. (Compact branches/jumps do not cause
 * exceptions.)
 */
int __microMIPS_compute_return_epc(struct pt_regs *regs)
{
	u16 __user *pc16;
	u16 halfword;
	unsigned int word;
	unsigned long contpc;
	struct mm_decoded_insn mminsn = { 0 };

	mminsn.micro_mips_mode = 1;

	/* This load never faults. */
	pc16 = (unsigned short __user *)msk_isa16_mode(regs->cp0_epc);
	__get_user(halfword, pc16);
	pc16++;
	contpc = regs->cp0_epc + 2;
	word = ((unsigned int)halfword << 16);
	mminsn.pc_inc = 2;

	if (!mm_insn_16bit(halfword)) {
		__get_user(halfword, pc16);
		pc16++;
		contpc = regs->cp0_epc + 4;
		mminsn.pc_inc = 4;
		word |= halfword;
	}
	mminsn.insn = word;

	if (get_user(halfword, pc16))
		goto sigsegv;
	mminsn.next_pc_inc = 2;
	word = ((unsigned int)halfword << 16);

	if (!mm_insn_16bit(halfword)) {
		pc16++;
		if (get_user(halfword, pc16))
			goto sigsegv;
		mminsn.next_pc_inc = 4;
		word |= halfword;
	}
	mminsn.next_insn = word;

	mm_isBranchInstr(regs, mminsn, &contpc);

	regs->cp0_epc = contpc;

	return 0;

sigsegv:
	force_sig(SIGSEGV, current);
	return -EFAULT;
}

/**
 * __compute_return_epc_for_insn - Computes the return address and do emulate
 *				    branch simulation, if required.
 *
 * @regs:	Pointer to pt_regs
 * @insn:	branch instruction to decode
 * @returns:	-EFAULT on error and forces SIGBUS, and on success
 *		returns 0 or BRANCH_LIKELY_TAKEN as appropriate after
 *		evaluating the branch.
 */
int __compute_return_epc_for_insn(struct pt_regs *regs,
				   union mips_instruction insn)
{
	unsigned int bit, fcr31, dspcontrol;
	long epc = regs->cp0_epc;
	int ret = 0;

	switch (insn.i_format.opcode) {
	/*
	 * jr and jalr are in r_format format.
	 */
	case spec_op:
		switch (insn.r_format.func) {
		case jalr_op:
			regs->regs[insn.r_format.rd] = epc + 8;
			/* Fall through */
		case jr_op:
			regs->cp0_epc = regs->regs[insn.r_format.rs];
			break;
		}
		break;

	/*
	 * This group contains:
	 * bltz_op, bgez_op, bltzl_op, bgezl_op,
	 * bltzal_op, bgezal_op, bltzall_op, bgezall_op.
	 */
	case bcond_op:
		switch (insn.i_format.rt) {
		case bltz_op:
		case bltzl_op:
			if ((long)regs->regs[insn.i_format.rs] < 0) {
				epc = epc + 4 + (insn.i_format.simmediate << 2);
				if (insn.i_format.rt == bltzl_op)
					ret = BRANCH_LIKELY_TAKEN;
			} else
				epc += 8;
			regs->cp0_epc = epc;
			break;

		case bgez_op:
		case bgezl_op:
			if ((long)regs->regs[insn.i_format.rs] >= 0) {
				epc = epc + 4 + (insn.i_format.simmediate << 2);
				if (insn.i_format.rt == bgezl_op)
					ret = BRANCH_LIKELY_TAKEN;
			} else
				epc += 8;
			regs->cp0_epc = epc;
			break;

		case bltzal_op:
		case bltzall_op:
			regs->regs[31] = epc + 8;
			if ((long)regs->regs[insn.i_format.rs] < 0) {
				epc = epc + 4 + (insn.i_format.simmediate << 2);
				if (insn.i_format.rt == bltzall_op)
					ret = BRANCH_LIKELY_TAKEN;
			} else
				epc += 8;
			regs->cp0_epc = epc;
			break;

		case bgezal_op:
		case bgezall_op:
			regs->regs[31] = epc + 8;
			if ((long)regs->regs[insn.i_format.rs] >= 0) {
				epc = epc + 4 + (insn.i_format.simmediate << 2);
				if (insn.i_format.rt == bgezall_op)
					ret = BRANCH_LIKELY_TAKEN;
			} else
				epc += 8;
			regs->cp0_epc = epc;
			break;

		case bposge32_op:
			if (!cpu_has_dsp)
				goto sigill;

			dspcontrol = rddsp(0x01);

			if (dspcontrol >= 32) {
				epc = epc + 4 + (insn.i_format.simmediate << 2);
			} else
				epc += 8;
			regs->cp0_epc = epc;
			break;
		}
		break;

	/*
	 * These are unconditional and in j_format.
	 */
	case jal_op:
		regs->regs[31] = regs->cp0_epc + 8;
	case j_op:
		epc += 4;
		epc >>= 28;
		epc <<= 28;
		epc |= (insn.j_format.target << 2);
		regs->cp0_epc = epc;
		if (insn.i_format.opcode == jalx_op)
			set_isa16_mode(regs->cp0_epc);
		break;

	/*
	 * These are conditional and in i_format.
	 */
	case beq_op:
	case beql_op:
		if (regs->regs[insn.i_format.rs] ==
		    regs->regs[insn.i_format.rt]) {
			epc = epc + 4 + (insn.i_format.simmediate << 2);
			if (insn.i_format.rt == beql_op)
				ret = BRANCH_LIKELY_TAKEN;
		} else
			epc += 8;
		regs->cp0_epc = epc;
		break;

	case bne_op:
	case bnel_op:
		if (regs->regs[insn.i_format.rs] !=
		    regs->regs[insn.i_format.rt]) {
			epc = epc + 4 + (insn.i_format.simmediate << 2);
			if (insn.i_format.rt == bnel_op)
				ret = BRANCH_LIKELY_TAKEN;
		} else
			epc += 8;
		regs->cp0_epc = epc;
		break;

	case blez_op: /* not really i_format */
	case blezl_op:
		/* rt field assumed to be zero */
		if ((long)regs->regs[insn.i_format.rs] <= 0) {
			epc = epc + 4 + (insn.i_format.simmediate << 2);
			if (insn.i_format.rt == bnel_op)
				ret = BRANCH_LIKELY_TAKEN;
		} else
			epc += 8;
		regs->cp0_epc = epc;
		break;

	case bgtz_op:
	case bgtzl_op:
		/* rt field assumed to be zero */
		if ((long)regs->regs[insn.i_format.rs] > 0) {
			epc = epc + 4 + (insn.i_format.simmediate << 2);
			if (insn.i_format.rt == bnel_op)
				ret = BRANCH_LIKELY_TAKEN;
		} else
			epc += 8;
		regs->cp0_epc = epc;
		break;

	/*
	 * And now the FPA/cp1 branch instructions.
	 */
	case cop1_op:
		preempt_disable();
		if (is_fpu_owner())
			asm volatile("cfc1\t%0,$31" : "=r" (fcr31));
		else
			fcr31 = current->thread.fpu.fcr31;
		preempt_enable();

		bit = (insn.i_format.rt >> 2);
		bit += (bit != 0);
		bit += 23;
		switch (insn.i_format.rt & 3) {
		case 0: /* bc1f */
		case 2: /* bc1fl */
			if (~fcr31 & (1 << bit)) {
				epc = epc + 4 + (insn.i_format.simmediate << 2);
				if (insn.i_format.rt == 2)
					ret = BRANCH_LIKELY_TAKEN;
			} else
				epc += 8;
			regs->cp0_epc = epc;
			break;

		case 1: /* bc1t */
		case 3: /* bc1tl */
			if (fcr31 & (1 << bit)) {
				epc = epc + 4 + (insn.i_format.simmediate << 2);
				if (insn.i_format.rt == 3)
					ret = BRANCH_LIKELY_TAKEN;
			} else
				epc += 8;
			regs->cp0_epc = epc;
			break;
		}
		break;
#ifdef CONFIG_CPU_CAVIUM_OCTEON
	case lwc2_op: /* This is bbit0 on Octeon */
		if ((regs->regs[insn.i_format.rs] & (1ull<<insn.i_format.rt))
		     == 0)
			epc = epc + 4 + (insn.i_format.simmediate << 2);
		else
			epc += 8;
		regs->cp0_epc = epc;
		break;
	case ldc2_op: /* This is bbit032 on Octeon */
		if ((regs->regs[insn.i_format.rs] &
		    (1ull<<(insn.i_format.rt+32))) == 0)
			epc = epc + 4 + (insn.i_format.simmediate << 2);
		else
			epc += 8;
		regs->cp0_epc = epc;
		break;
	case swc2_op: /* This is bbit1 on Octeon */
		if (regs->regs[insn.i_format.rs] & (1ull<<insn.i_format.rt))
			epc = epc + 4 + (insn.i_format.simmediate << 2);
		else
			epc += 8;
		regs->cp0_epc = epc;
		break;
	case sdc2_op: /* This is bbit132 on Octeon */
		if (regs->regs[insn.i_format.rs] &
		    (1ull<<(insn.i_format.rt+32)))
			epc = epc + 4 + (insn.i_format.simmediate << 2);
		else
			epc += 8;
		regs->cp0_epc = epc;
		break;
#endif
	}

	return ret;

sigill:
	printk("%s: DSP branch but not DSP ASE - sending SIGBUS.\n", current->comm);
	force_sig(SIGBUS, current);
	return -EFAULT;
}
EXPORT_SYMBOL_GPL(__compute_return_epc_for_insn);

int __compute_return_epc(struct pt_regs *regs)
{
	unsigned int __user *addr;
	long epc;
	union mips_instruction insn;

	epc = regs->cp0_epc;
	if (epc & 3)
		goto unaligned;

	/*
	 * Read the instruction
	 */
	addr = (unsigned int __user *) epc;
	if (__get_user(insn.word, addr)) {
		force_sig(SIGSEGV, current);
		return -EFAULT;
	}

	return __compute_return_epc_for_insn(regs, insn);

unaligned:
	printk("%s: unaligned epc - sending SIGBUS.\n", current->comm);
	force_sig(SIGBUS, current);
	return -EFAULT;

}
