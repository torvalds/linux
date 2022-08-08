// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/ptrace.h>

#include <linux/uaccess.h>

#include "sfp-util.h"
#include <math-emu/soft-fp.h>
#include <math-emu/single.h>
#include <math-emu/double.h>

#define	OPC_PAL		0x00
#define OPC_INTA	0x10
#define OPC_INTL	0x11
#define OPC_INTS	0x12
#define OPC_INTM	0x13
#define OPC_FLTC	0x14
#define OPC_FLTV	0x15
#define OPC_FLTI	0x16
#define OPC_FLTL	0x17
#define OPC_MISC	0x18
#define	OPC_JSR		0x1a

#define FOP_SRC_S	0
#define FOP_SRC_T	2
#define FOP_SRC_Q	3

#define FOP_FNC_ADDx	0
#define FOP_FNC_CVTQL	0
#define FOP_FNC_SUBx	1
#define FOP_FNC_MULx	2
#define FOP_FNC_DIVx	3
#define FOP_FNC_CMPxUN	4
#define FOP_FNC_CMPxEQ	5
#define FOP_FNC_CMPxLT	6
#define FOP_FNC_CMPxLE	7
#define FOP_FNC_SQRTx	11
#define FOP_FNC_CVTxS	12
#define FOP_FNC_CVTxT	14
#define FOP_FNC_CVTxQ	15

#define MISC_TRAPB	0x0000
#define MISC_EXCB	0x0400

extern unsigned long alpha_read_fp_reg (unsigned long reg);
extern void alpha_write_fp_reg (unsigned long reg, unsigned long val);
extern unsigned long alpha_read_fp_reg_s (unsigned long reg);
extern void alpha_write_fp_reg_s (unsigned long reg, unsigned long val);


#ifdef MODULE

MODULE_DESCRIPTION("FP Software completion module");
MODULE_LICENSE("GPL v2");

extern long (*alpha_fp_emul_imprecise)(struct pt_regs *, unsigned long);
extern long (*alpha_fp_emul) (unsigned long pc);

static long (*save_emul_imprecise)(struct pt_regs *, unsigned long);
static long (*save_emul) (unsigned long pc);

long do_alpha_fp_emul_imprecise(struct pt_regs *, unsigned long);
long do_alpha_fp_emul(unsigned long);

static int alpha_fp_emul_init_module(void)
{
	save_emul_imprecise = alpha_fp_emul_imprecise;
	save_emul = alpha_fp_emul;
	alpha_fp_emul_imprecise = do_alpha_fp_emul_imprecise;
	alpha_fp_emul = do_alpha_fp_emul;
	return 0;
}
module_init(alpha_fp_emul_init_module);

static void alpha_fp_emul_cleanup_module(void)
{
	alpha_fp_emul_imprecise = save_emul_imprecise;
	alpha_fp_emul = save_emul;
}
module_exit(alpha_fp_emul_cleanup_module);

#undef  alpha_fp_emul_imprecise
#define alpha_fp_emul_imprecise		do_alpha_fp_emul_imprecise
#undef  alpha_fp_emul
#define alpha_fp_emul			do_alpha_fp_emul

#endif /* MODULE */


/*
 * Emulate the floating point instruction at address PC.  Returns -1 if the
 * instruction to be emulated is illegal (such as with the opDEC trap), else
 * the SI_CODE for a SIGFPE signal, else 0 if everything's ok.
 *
 * Notice that the kernel does not and cannot use FP regs.  This is good
 * because it means that instead of saving/restoring all fp regs, we simply
 * stick the result of the operation into the appropriate register.
 */
long
alpha_fp_emul (unsigned long pc)
{
	FP_DECL_EX;
	FP_DECL_S(SA); FP_DECL_S(SB); FP_DECL_S(SR);
	FP_DECL_D(DA); FP_DECL_D(DB); FP_DECL_D(DR);

	unsigned long fa, fb, fc, func, mode, src;
	unsigned long res, va, vb, vc, swcr, fpcr;
	__u32 insn;
	long si_code;

	get_user(insn, (__u32 __user *)pc);
	fc     = (insn >>  0) & 0x1f;	/* destination register */
	fb     = (insn >> 16) & 0x1f;
	fa     = (insn >> 21) & 0x1f;
	func   = (insn >>  5) & 0xf;
	src    = (insn >>  9) & 0x3;
	mode   = (insn >> 11) & 0x3;
	
	fpcr = rdfpcr();
	swcr = swcr_update_status(current_thread_info()->ieee_state, fpcr);

	if (mode == 3) {
		/* Dynamic -- get rounding mode from fpcr.  */
		mode = (fpcr >> FPCR_DYN_SHIFT) & 3;
	}

	switch (src) {
	case FOP_SRC_S:
		va = alpha_read_fp_reg_s(fa);
		vb = alpha_read_fp_reg_s(fb);
		
		FP_UNPACK_SP(SA, &va);
		FP_UNPACK_SP(SB, &vb);

		switch (func) {
		case FOP_FNC_SUBx:
			FP_SUB_S(SR, SA, SB);
			goto pack_s;

		case FOP_FNC_ADDx:
			FP_ADD_S(SR, SA, SB);
			goto pack_s;

		case FOP_FNC_MULx:
			FP_MUL_S(SR, SA, SB);
			goto pack_s;

		case FOP_FNC_DIVx:
			FP_DIV_S(SR, SA, SB);
			goto pack_s;

		case FOP_FNC_SQRTx:
			FP_SQRT_S(SR, SB);
			goto pack_s;
		}
		goto bad_insn;

	case FOP_SRC_T:
		va = alpha_read_fp_reg(fa);
		vb = alpha_read_fp_reg(fb);

		if ((func & ~3) == FOP_FNC_CMPxUN) {
			FP_UNPACK_RAW_DP(DA, &va);
			FP_UNPACK_RAW_DP(DB, &vb);
			if (!DA_e && !_FP_FRAC_ZEROP_1(DA)) {
				FP_SET_EXCEPTION(FP_EX_DENORM);
				if (FP_DENORM_ZERO)
					_FP_FRAC_SET_1(DA, _FP_ZEROFRAC_1);
			}
			if (!DB_e && !_FP_FRAC_ZEROP_1(DB)) {
				FP_SET_EXCEPTION(FP_EX_DENORM);
				if (FP_DENORM_ZERO)
					_FP_FRAC_SET_1(DB, _FP_ZEROFRAC_1);
			}
			FP_CMP_D(res, DA, DB, 3);
			vc = 0x4000000000000000UL;
			/* CMPTEQ, CMPTUN don't trap on QNaN,
			   while CMPTLT and CMPTLE do */
			if (res == 3
			    && ((func & 3) >= 2
				|| FP_ISSIGNAN_D(DA)
				|| FP_ISSIGNAN_D(DB))) {
				FP_SET_EXCEPTION(FP_EX_INVALID);
			}
			switch (func) {
			case FOP_FNC_CMPxUN: if (res != 3) vc = 0; break;
			case FOP_FNC_CMPxEQ: if (res) vc = 0; break;
			case FOP_FNC_CMPxLT: if (res != -1) vc = 0; break;
			case FOP_FNC_CMPxLE: if ((long)res > 0) vc = 0; break;
			}
			goto done_d;
		}

		FP_UNPACK_DP(DA, &va);
		FP_UNPACK_DP(DB, &vb);

		switch (func) {
		case FOP_FNC_SUBx:
			FP_SUB_D(DR, DA, DB);
			goto pack_d;

		case FOP_FNC_ADDx:
			FP_ADD_D(DR, DA, DB);
			goto pack_d;

		case FOP_FNC_MULx:
			FP_MUL_D(DR, DA, DB);
			goto pack_d;

		case FOP_FNC_DIVx:
			FP_DIV_D(DR, DA, DB);
			goto pack_d;

		case FOP_FNC_SQRTx:
			FP_SQRT_D(DR, DB);
			goto pack_d;

		case FOP_FNC_CVTxS:
			/* It is irritating that DEC encoded CVTST with
			   SRC == T_floating.  It is also interesting that
			   the bit used to tell the two apart is /U... */
			if (insn & 0x2000) {
				FP_CONV(S,D,1,1,SR,DB);
				goto pack_s;
			} else {
				vb = alpha_read_fp_reg_s(fb);
				FP_UNPACK_SP(SB, &vb);
				DR_c = DB_c;
				DR_s = DB_s;
				DR_e = DB_e + (1024 - 128);
				DR_f = SB_f << (52 - 23);
				goto pack_d;
			}

		case FOP_FNC_CVTxQ:
			if (DB_c == FP_CLS_NAN
			    && (_FP_FRAC_HIGH_RAW_D(DB) & _FP_QNANBIT_D)) {
			  /* AAHB Table B-2 says QNaN should not trigger INV */
				vc = 0;
			} else
				FP_TO_INT_ROUND_D(vc, DB, 64, 2);
			goto done_d;
		}
		goto bad_insn;

	case FOP_SRC_Q:
		vb = alpha_read_fp_reg(fb);

		switch (func) {
		case FOP_FNC_CVTQL:
			/* Notice: We can get here only due to an integer
			   overflow.  Such overflows are reported as invalid
			   ops.  We return the result the hw would have
			   computed.  */
			vc = ((vb & 0xc0000000) << 32 |	/* sign and msb */
			      (vb & 0x3fffffff) << 29);	/* rest of the int */
			FP_SET_EXCEPTION (FP_EX_INVALID);
			goto done_d;

		case FOP_FNC_CVTxS:
			FP_FROM_INT_S(SR, ((long)vb), 64, long);
			goto pack_s;

		case FOP_FNC_CVTxT:
			FP_FROM_INT_D(DR, ((long)vb), 64, long);
			goto pack_d;
		}
		goto bad_insn;
	}
	goto bad_insn;

pack_s:
	FP_PACK_SP(&vc, SR);
	if ((_fex & FP_EX_UNDERFLOW) && (swcr & IEEE_MAP_UMZ))
		vc = 0;
	alpha_write_fp_reg_s(fc, vc);
	goto done;

pack_d:
	FP_PACK_DP(&vc, DR);
	if ((_fex & FP_EX_UNDERFLOW) && (swcr & IEEE_MAP_UMZ))
		vc = 0;
done_d:
	alpha_write_fp_reg(fc, vc);
	goto done;

	/*
	 * Take the appropriate action for each possible
	 * floating-point result:
	 *
	 *	- Set the appropriate bits in the FPCR
	 *	- If the specified exception is enabled in the FPCR,
	 *	  return.  The caller (entArith) will dispatch
	 *	  the appropriate signal to the translated program.
	 *
	 * In addition, properly track the exception state in software
	 * as described in the Alpha Architecture Handbook section 4.7.7.3.
	 */
done:
	if (_fex) {
		/* Record exceptions in software control word.  */
		swcr |= (_fex << IEEE_STATUS_TO_EXCSUM_SHIFT);
		current_thread_info()->ieee_state
		  |= (_fex << IEEE_STATUS_TO_EXCSUM_SHIFT);

		/* Update hardware control register.  */
		fpcr &= (~FPCR_MASK | FPCR_DYN_MASK);
		fpcr |= ieee_swcr_to_fpcr(swcr);
		wrfpcr(fpcr);

		/* Do we generate a signal?  */
		_fex = _fex & swcr & IEEE_TRAP_ENABLE_MASK;
		si_code = 0;
		if (_fex) {
			if (_fex & IEEE_TRAP_ENABLE_DNO) si_code = FPE_FLTUND;
			if (_fex & IEEE_TRAP_ENABLE_INE) si_code = FPE_FLTRES;
			if (_fex & IEEE_TRAP_ENABLE_UNF) si_code = FPE_FLTUND;
			if (_fex & IEEE_TRAP_ENABLE_OVF) si_code = FPE_FLTOVF;
			if (_fex & IEEE_TRAP_ENABLE_DZE) si_code = FPE_FLTDIV;
			if (_fex & IEEE_TRAP_ENABLE_INV) si_code = FPE_FLTINV;
		}

		return si_code;
	}

	/* We used to write the destination register here, but DEC FORTRAN
	   requires that the result *always* be written... so we do the write
	   immediately after the operations above.  */

	return 0;

bad_insn:
	printk(KERN_ERR "alpha_fp_emul: Invalid FP insn %#x at %#lx\n",
	       insn, pc);
	return -1;
}

long
alpha_fp_emul_imprecise (struct pt_regs *regs, unsigned long write_mask)
{
	unsigned long trigger_pc = regs->pc - 4;
	unsigned long insn, opcode, rc, si_code = 0;

	/*
	 * Turn off the bits corresponding to registers that are the
	 * target of instructions that set bits in the exception
	 * summary register.  We have some slack doing this because a
	 * register that is the target of a trapping instruction can
	 * be written at most once in the trap shadow.
	 *
	 * Branches, jumps, TRAPBs, EXCBs and calls to PALcode all
	 * bound the trap shadow, so we need not look any further than
	 * up to the first occurrence of such an instruction.
	 */
	while (write_mask) {
		get_user(insn, (__u32 __user *)(trigger_pc));
		opcode = insn >> 26;
		rc = insn & 0x1f;

		switch (opcode) {
		      case OPC_PAL:
		      case OPC_JSR:
		      case 0x30 ... 0x3f:	/* branches */
			goto egress;

		      case OPC_MISC:
			switch (insn & 0xffff) {
			      case MISC_TRAPB:
			      case MISC_EXCB:
				goto egress;

			      default:
				break;
			}
			break;

		      case OPC_INTA:
		      case OPC_INTL:
		      case OPC_INTS:
		      case OPC_INTM:
			write_mask &= ~(1UL << rc);
			break;

		      case OPC_FLTC:
		      case OPC_FLTV:
		      case OPC_FLTI:
		      case OPC_FLTL:
			write_mask &= ~(1UL << (rc + 32));
			break;
		}
		if (!write_mask) {
			/* Re-execute insns in the trap-shadow.  */
			regs->pc = trigger_pc + 4;
			si_code = alpha_fp_emul(trigger_pc);
			goto egress;
		}
		trigger_pc -= 4;
	}

egress:
	return si_code;
}

EXPORT_SYMBOL(__udiv_qrnnd);
