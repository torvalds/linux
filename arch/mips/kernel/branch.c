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
#include <asm/mips-r2-to-r6-emul.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>

/*
 * Calculate and return exception PC in case of branch delay slot
 * for microMIPS and MIPS16e. It does not clear the ISA mode bit.
 */
int __isa_exception_epc(struct pt_regs *regs)
{
	unsigned short inst;
	long epc = regs->cp0_epc;

	/* Calculate exception PC in branch delay slot. */
	if (__get_user(inst, (u16 __user *) msk_isa16_mode(epc))) {
		/* This should never happen because delay slot was checked. */
		force_sig(SIGSEGV, current);
		return epc;
	}
	if (cpu_has_mips16) {
		union mips16e_instruction inst_mips16e;

		inst_mips16e.full = inst;
		if (inst_mips16e.ri.opcode == MIPS16e_jal_op)
			epc += 4;
		else
			epc += 2;
	} else if (mm_insn_16bit(inst))
		epc += 2;
	else
		epc += 4;

	return epc;
}

/* (microMIPS) Convert 16-bit register encoding to 32-bit register encoding. */
static const unsigned int reg16to32map[8] = {16, 17, 2, 3, 4, 5, 6, 7};

int __mm_isBranchInstr(struct pt_regs *regs, struct mm_decoded_insn dec_insn,
		       unsigned long *contpc)
{
	union mips_instruction insn = (union mips_instruction)dec_insn.insn;
	int bc_false = 0;
	unsigned int fcr31;
	unsigned int bit;

	if (!cpu_has_mmips)
		return 0;

	switch (insn.mm_i_format.opcode) {
	case mm_pool32a_op:
		if ((insn.mm_i_format.simmediate & MM_POOL32A_MINOR_MASK) ==
		    mm_pool32axf_op) {
			switch (insn.mm_i_format.simmediate >>
				MM_POOL32A_MINOR_SHIFT) {
			case mm_jalr_op:
			case mm_jalrhb_op:
			case mm_jalrs_op:
			case mm_jalrshb_op:
				if (insn.mm_i_format.rt != 0)	/* Not mm_jr */
					regs->regs[insn.mm_i_format.rt] =
						regs->cp0_epc +
						dec_insn.pc_inc +
						dec_insn.next_pc_inc;
				*contpc = regs->regs[insn.mm_i_format.rs];
				return 1;
			}
		}
		break;
	case mm_pool32i_op:
		switch (insn.mm_i_format.rt) {
		case mm_bltzals_op:
		case mm_bltzal_op:
			regs->regs[31] = regs->cp0_epc +
				dec_insn.pc_inc +
				dec_insn.next_pc_inc;
			/* Fall through */
		case mm_bltz_op:
			if ((long)regs->regs[insn.mm_i_format.rs] < 0)
				*contpc = regs->cp0_epc +
					dec_insn.pc_inc +
					(insn.mm_i_format.simmediate << 1);
			else
				*contpc = regs->cp0_epc +
					dec_insn.pc_inc +
					dec_insn.next_pc_inc;
			return 1;
		case mm_bgezals_op:
		case mm_bgezal_op:
			regs->regs[31] = regs->cp0_epc +
					dec_insn.pc_inc +
					dec_insn.next_pc_inc;
			/* Fall through */
		case mm_bgez_op:
			if ((long)regs->regs[insn.mm_i_format.rs] >= 0)
				*contpc = regs->cp0_epc +
					dec_insn.pc_inc +
					(insn.mm_i_format.simmediate << 1);
			else
				*contpc = regs->cp0_epc +
					dec_insn.pc_inc +
					dec_insn.next_pc_inc;
			return 1;
		case mm_blez_op:
			if ((long)regs->regs[insn.mm_i_format.rs] <= 0)
				*contpc = regs->cp0_epc +
					dec_insn.pc_inc +
					(insn.mm_i_format.simmediate << 1);
			else
				*contpc = regs->cp0_epc +
					dec_insn.pc_inc +
					dec_insn.next_pc_inc;
			return 1;
		case mm_bgtz_op:
			if ((long)regs->regs[insn.mm_i_format.rs] <= 0)
				*contpc = regs->cp0_epc +
					dec_insn.pc_inc +
					(insn.mm_i_format.simmediate << 1);
			else
				*contpc = regs->cp0_epc +
					dec_insn.pc_inc +
					dec_insn.next_pc_inc;
			return 1;
		case mm_bc2f_op:
		case mm_bc1f_op:
			bc_false = 1;
			/* Fall through */
		case mm_bc2t_op:
		case mm_bc1t_op:
			preempt_disable();
			if (is_fpu_owner())
			        fcr31 = read_32bit_cp1_register(CP1_STATUS);
			else
				fcr31 = current->thread.fpu.fcr31;
			preempt_enable();

			if (bc_false)
				fcr31 = ~fcr31;

			bit = (insn.mm_i_format.rs >> 2);
			bit += (bit != 0);
			bit += 23;
			if (fcr31 & (1 << bit))
				*contpc = regs->cp0_epc +
					dec_insn.pc_inc +
					(insn.mm_i_format.simmediate << 1);
			else
				*contpc = regs->cp0_epc +
					dec_insn.pc_inc + dec_insn.next_pc_inc;
			return 1;
		}
		break;
	case mm_pool16c_op:
		switch (insn.mm_i_format.rt) {
		case mm_jalr16_op:
		case mm_jalrs16_op:
			regs->regs[31] = regs->cp0_epc +
				dec_insn.pc_inc + dec_insn.next_pc_inc;
			/* Fall through */
		case mm_jr16_op:
			*contpc = regs->regs[insn.mm_i_format.rs];
			return 1;
		}
		break;
	case mm_beqz16_op:
		if ((long)regs->regs[reg16to32map[insn.mm_b1_format.rs]] == 0)
			*contpc = regs->cp0_epc +
				dec_insn.pc_inc +
				(insn.mm_b1_format.simmediate << 1);
		else
			*contpc = regs->cp0_epc +
				dec_insn.pc_inc + dec_insn.next_pc_inc;
		return 1;
	case mm_bnez16_op:
		if ((long)regs->regs[reg16to32map[insn.mm_b1_format.rs]] != 0)
			*contpc = regs->cp0_epc +
				dec_insn.pc_inc +
				(insn.mm_b1_format.simmediate << 1);
		else
			*contpc = regs->cp0_epc +
				dec_insn.pc_inc + dec_insn.next_pc_inc;
		return 1;
	case mm_b16_op:
		*contpc = regs->cp0_epc + dec_insn.pc_inc +
			 (insn.mm_b0_format.simmediate << 1);
		return 1;
	case mm_beq32_op:
		if (regs->regs[insn.mm_i_format.rs] ==
		    regs->regs[insn.mm_i_format.rt])
			*contpc = regs->cp0_epc +
				dec_insn.pc_inc +
				(insn.mm_i_format.simmediate << 1);
		else
			*contpc = regs->cp0_epc +
				dec_insn.pc_inc +
				dec_insn.next_pc_inc;
		return 1;
	case mm_bne32_op:
		if (regs->regs[insn.mm_i_format.rs] !=
		    regs->regs[insn.mm_i_format.rt])
			*contpc = regs->cp0_epc +
				dec_insn.pc_inc +
				(insn.mm_i_format.simmediate << 1);
		else
			*contpc = regs->cp0_epc +
				dec_insn.pc_inc + dec_insn.next_pc_inc;
		return 1;
	case mm_jalx32_op:
		regs->regs[31] = regs->cp0_epc +
			dec_insn.pc_inc + dec_insn.next_pc_inc;
		*contpc = regs->cp0_epc + dec_insn.pc_inc;
		*contpc >>= 28;
		*contpc <<= 28;
		*contpc |= (insn.j_format.target << 2);
		return 1;
	case mm_jals32_op:
	case mm_jal32_op:
		regs->regs[31] = regs->cp0_epc +
			dec_insn.pc_inc + dec_insn.next_pc_inc;
		/* Fall through */
	case mm_j32_op:
		*contpc = regs->cp0_epc + dec_insn.pc_inc;
		*contpc >>= 27;
		*contpc <<= 27;
		*contpc |= (insn.j_format.target << 1);
		set_isa16_mode(*contpc);
		return 1;
	}
	return 0;
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

/*
 * Compute return address and emulate branch in MIPS16e mode after an
 * exception only. It does not handle compact branches/jumps and cannot
 * be used in interrupt context. (Compact branches/jumps do not cause
 * exceptions.)
 */
int __MIPS16e_compute_return_epc(struct pt_regs *regs)
{
	u16 __user *addr;
	union mips16e_instruction inst;
	u16 inst2;
	u32 fullinst;
	long epc;

	epc = regs->cp0_epc;

	/* Read the instruction. */
	addr = (u16 __user *)msk_isa16_mode(epc);
	if (__get_user(inst.full, addr)) {
		force_sig(SIGSEGV, current);
		return -EFAULT;
	}

	switch (inst.ri.opcode) {
	case MIPS16e_extend_op:
		regs->cp0_epc += 4;
		return 0;

		/*
		 *  JAL and JALX in MIPS16e mode
		 */
	case MIPS16e_jal_op:
		addr += 1;
		if (__get_user(inst2, addr)) {
			force_sig(SIGSEGV, current);
			return -EFAULT;
		}
		fullinst = ((unsigned)inst.full << 16) | inst2;
		regs->regs[31] = epc + 6;
		epc += 4;
		epc >>= 28;
		epc <<= 28;
		/*
		 * JAL:5 X:1 TARGET[20-16]:5 TARGET[25:21]:5 TARGET[15:0]:16
		 *
		 * ......TARGET[15:0].................TARGET[20:16]...........
		 * ......TARGET[25:21]
		 */
		epc |=
		    ((fullinst & 0xffff) << 2) | ((fullinst & 0x3e00000) >> 3) |
		    ((fullinst & 0x1f0000) << 7);
		if (!inst.jal.x)
			set_isa16_mode(epc);	/* Set ISA mode bit. */
		regs->cp0_epc = epc;
		return 0;

		/*
		 *  J(AL)R(C)
		 */
	case MIPS16e_rr_op:
		if (inst.rr.func == MIPS16e_jr_func) {

			if (inst.rr.ra)
				regs->cp0_epc = regs->regs[31];
			else
				regs->cp0_epc =
				    regs->regs[reg16to32[inst.rr.rx]];

			if (inst.rr.l) {
				if (inst.rr.nd)
					regs->regs[31] = epc + 2;
				else
					regs->regs[31] = epc + 4;
			}
			return 0;
		}
		break;
	}

	/*
	 * All other cases have no branch delay slot and are 16-bits.
	 * Branches do not cause an exception.
	 */
	regs->cp0_epc += 2;

	return 0;
}

/**
 * __compute_return_epc_for_insn - Computes the return address and do emulate
 *				    branch simulation, if required.
 *
 * @regs:	Pointer to pt_regs
 * @insn:	branch instruction to decode
 * @returns:	-EFAULT on error and forces SIGILL, and on success
 *		returns 0 or BRANCH_LIKELY_TAKEN as appropriate after
 *		evaluating the branch.
 *
 * MIPS R6 Compact branches and forbidden slots:
 *	Compact branches do not throw exceptions because they do
 *	not have delay slots. The forbidden slot instruction ($PC+4)
 *	is only executed if the branch was not taken. Otherwise the
 *	forbidden slot is skipped entirely. This means that the
 *	only possible reason to be here because of a MIPS R6 compact
 *	branch instruction is that the forbidden slot has thrown one.
 *	In that case the branch was not taken, so the EPC can be safely
 *	set to EPC + 8.
 */
int __compute_return_epc_for_insn(struct pt_regs *regs,
				   union mips_instruction insn)
{
	unsigned int bit, fcr31, dspcontrol, reg;
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
			if (NO_R6EMU && insn.r_format.func == jr_op)
				goto sigill_r2r6;
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
		case bltzl_op:
			if (NO_R6EMU)
				goto sigill_r2r6;
		case bltz_op:
			if ((long)regs->regs[insn.i_format.rs] < 0) {
				epc = epc + 4 + (insn.i_format.simmediate << 2);
				if (insn.i_format.rt == bltzl_op)
					ret = BRANCH_LIKELY_TAKEN;
			} else
				epc += 8;
			regs->cp0_epc = epc;
			break;

		case bgezl_op:
			if (NO_R6EMU)
				goto sigill_r2r6;
		case bgez_op:
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
			if (NO_R6EMU && (insn.i_format.rs ||
			    insn.i_format.rt == bltzall_op)) {
				ret = -SIGILL;
				break;
			}
			regs->regs[31] = epc + 8;
			/*
			 * OK we are here either because we hit a NAL
			 * instruction or because we are emulating an
			 * old bltzal{,l} one. Lets figure out what the
			 * case really is.
			 */
			if (!insn.i_format.rs) {
				/*
				 * NAL or BLTZAL with rs == 0
				 * Doesn't matter if we are R6 or not. The
				 * result is the same
				 */
				regs->cp0_epc += 4 +
					(insn.i_format.simmediate << 2);
				break;
			}
			/* Now do the real thing for non-R6 BLTZAL{,L} */
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
			if (NO_R6EMU && (insn.i_format.rs ||
			    insn.i_format.rt == bgezall_op)) {
				ret = -SIGILL;
				break;
			}
			regs->regs[31] = epc + 8;
			/*
			 * OK we are here either because we hit a BAL
			 * instruction or because we are emulating an
			 * old bgezal{,l} one. Lets figure out what the
			 * case really is.
			 */
			if (!insn.i_format.rs) {
				/*
				 * BAL or BGEZAL with rs == 0
				 * Doesn't matter if we are R6 or not. The
				 * result is the same
				 */
				regs->cp0_epc += 4 +
					(insn.i_format.simmediate << 2);
				break;
			}
			/* Now do the real thing for non-R6 BGEZAL{,L} */
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
				goto sigill_dsp;

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
	case jalx_op:
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
	case beql_op:
		if (NO_R6EMU)
			goto sigill_r2r6;
	case beq_op:
		if (regs->regs[insn.i_format.rs] ==
		    regs->regs[insn.i_format.rt]) {
			epc = epc + 4 + (insn.i_format.simmediate << 2);
			if (insn.i_format.opcode == beql_op)
				ret = BRANCH_LIKELY_TAKEN;
		} else
			epc += 8;
		regs->cp0_epc = epc;
		break;

	case bnel_op:
		if (NO_R6EMU)
			goto sigill_r2r6;
	case bne_op:
		if (regs->regs[insn.i_format.rs] !=
		    regs->regs[insn.i_format.rt]) {
			epc = epc + 4 + (insn.i_format.simmediate << 2);
			if (insn.i_format.opcode == bnel_op)
				ret = BRANCH_LIKELY_TAKEN;
		} else
			epc += 8;
		regs->cp0_epc = epc;
		break;

	case blezl_op: /* not really i_format */
		if (!insn.i_format.rt && NO_R6EMU)
			goto sigill_r2r6;
	case blez_op:
		/*
		 * Compact branches for R6 for the
		 * blez and blezl opcodes.
		 * BLEZ  | rs = 0 | rt != 0  == BLEZALC
		 * BLEZ  | rs = rt != 0      == BGEZALC
		 * BLEZ  | rs != 0 | rt != 0 == BGEUC
		 * BLEZL | rs = 0 | rt != 0  == BLEZC
		 * BLEZL | rs = rt != 0      == BGEZC
		 * BLEZL | rs != 0 | rt != 0 == BGEC
		 *
		 * For real BLEZ{,L}, rt is always 0.
		 */

		if (cpu_has_mips_r6 && insn.i_format.rt) {
			if ((insn.i_format.opcode == blez_op) &&
			    ((!insn.i_format.rs && insn.i_format.rt) ||
			     (insn.i_format.rs == insn.i_format.rt)))
				regs->regs[31] = epc + 4;
			regs->cp0_epc += 8;
			break;
		}
		/* rt field assumed to be zero */
		if ((long)regs->regs[insn.i_format.rs] <= 0) {
			epc = epc + 4 + (insn.i_format.simmediate << 2);
			if (insn.i_format.opcode == blezl_op)
				ret = BRANCH_LIKELY_TAKEN;
		} else
			epc += 8;
		regs->cp0_epc = epc;
		break;

	case bgtzl_op:
		if (!insn.i_format.rt && NO_R6EMU)
			goto sigill_r2r6;
	case bgtz_op:
		/*
		 * Compact branches for R6 for the
		 * bgtz and bgtzl opcodes.
		 * BGTZ  | rs = 0 | rt != 0  == BGTZALC
		 * BGTZ  | rs = rt != 0      == BLTZALC
		 * BGTZ  | rs != 0 | rt != 0 == BLTUC
		 * BGTZL | rs = 0 | rt != 0  == BGTZC
		 * BGTZL | rs = rt != 0      == BLTZC
		 * BGTZL | rs != 0 | rt != 0 == BLTC
		 *
		 * *ZALC varint for BGTZ &&& rt != 0
		 * For real GTZ{,L}, rt is always 0.
		 */
		if (cpu_has_mips_r6 && insn.i_format.rt) {
			if ((insn.i_format.opcode == blez_op) &&
			    ((!insn.i_format.rs && insn.i_format.rt) ||
			    (insn.i_format.rs == insn.i_format.rt)))
				regs->regs[31] = epc + 4;
			regs->cp0_epc += 8;
			break;
		}

		/* rt field assumed to be zero */
		if ((long)regs->regs[insn.i_format.rs] > 0) {
			epc = epc + 4 + (insn.i_format.simmediate << 2);
			if (insn.i_format.opcode == bgtzl_op)
				ret = BRANCH_LIKELY_TAKEN;
		} else
			epc += 8;
		regs->cp0_epc = epc;
		break;

	/*
	 * And now the FPA/cp1 branch instructions.
	 */
	case cop1_op:
		if (cpu_has_mips_r6 &&
		    ((insn.i_format.rs == bc1eqz_op) ||
		     (insn.i_format.rs == bc1nez_op))) {
			if (!used_math()) { /* First time FPU user */
				ret = init_fpu();
				if (ret && NO_R6EMU) {
					ret = -ret;
					break;
				}
				ret = 0;
				set_used_math();
			}
			lose_fpu(1);    /* Save FPU state for the emulator. */
			reg = insn.i_format.rt;
			bit = 0;
			switch (insn.i_format.rs) {
			case bc1eqz_op:
				/* Test bit 0 */
				if (get_fpr32(&current->thread.fpu.fpr[reg], 0)
				    & 0x1)
					bit = 1;
				break;
			case bc1nez_op:
				/* Test bit 0 */
				if (!(get_fpr32(&current->thread.fpu.fpr[reg], 0)
				      & 0x1))
					bit = 1;
				break;
			}
			own_fpu(1);
			if (bit)
				epc = epc + 4 +
					(insn.i_format.simmediate << 2);
			else
				epc += 8;
			regs->cp0_epc = epc;

			break;
		} else {

			preempt_disable();
			if (is_fpu_owner())
			        fcr31 = read_32bit_cp1_register(CP1_STATUS);
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
					epc = epc + 4 +
						(insn.i_format.simmediate << 2);
					if (insn.i_format.rt == 2)
						ret = BRANCH_LIKELY_TAKEN;
				} else
					epc += 8;
				regs->cp0_epc = epc;
				break;

			case 1: /* bc1t */
			case 3: /* bc1tl */
				if (fcr31 & (1 << bit)) {
					epc = epc + 4 +
						(insn.i_format.simmediate << 2);
					if (insn.i_format.rt == 3)
						ret = BRANCH_LIKELY_TAKEN;
				} else
					epc += 8;
				regs->cp0_epc = epc;
				break;
			}
			break;
		}
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
#else
	case bc6_op:
		/* Only valid for MIPS R6 */
		if (!cpu_has_mips_r6) {
			ret = -SIGILL;
			break;
		}
		regs->cp0_epc += 8;
		break;
	case balc6_op:
		if (!cpu_has_mips_r6) {
			ret = -SIGILL;
			break;
		}
		/* Compact branch: BALC */
		regs->regs[31] = epc + 4;
		epc += 4 + (insn.i_format.simmediate << 2);
		regs->cp0_epc = epc;
		break;
	case beqzcjic_op:
		if (!cpu_has_mips_r6) {
			ret = -SIGILL;
			break;
		}
		/* Compact branch: BEQZC || JIC */
		regs->cp0_epc += 8;
		break;
	case bnezcjialc_op:
		if (!cpu_has_mips_r6) {
			ret = -SIGILL;
			break;
		}
		/* Compact branch: BNEZC || JIALC */
		if (!insn.i_format.rs) {
			/* JIALC: set $31/ra */
			regs->regs[31] = epc + 4;
		}
		regs->cp0_epc += 8;
		break;
#endif
	case cbcond0_op:
	case cbcond1_op:
		/* Only valid for MIPS R6 */
		if (!cpu_has_mips_r6) {
			ret = -SIGILL;
			break;
		}
		/*
		 * Compact branches:
		 * bovc, beqc, beqzalc, bnvc, bnec, bnezlac
		 */
		if (insn.i_format.rt && !insn.i_format.rs)
			regs->regs[31] = epc + 4;
		regs->cp0_epc += 8;
		break;
	}

	return ret;

sigill_dsp:
	pr_info("%s: DSP branch but not DSP ASE - sending SIGILL.\n",
		current->comm);
	force_sig(SIGILL, current);
	return -EFAULT;
sigill_r2r6:
	pr_info("%s: R2 branch but r2-to-r6 emulator is not preset - sending SIGILL.\n",
		current->comm);
	force_sig(SIGILL, current);
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
