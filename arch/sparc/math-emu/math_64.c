/*
 * arch/sparc64/math-emu/math.c
 *
 * Copyright (C) 1997,1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 * Emulation routines originate from soft-fp package, which is part
 * of glibc and has appropriate copyrights in it.
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/perf_event.h>

#include <asm/fpumacro.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>

#include "sfp-util_64.h"
#include <math-emu/soft-fp.h>
#include <math-emu/single.h>
#include <math-emu/double.h>
#include <math-emu/quad.h>

/* QUAD - ftt == 3 */
#define FMOVQ	0x003
#define FNEGQ	0x007
#define FABSQ	0x00b
#define FSQRTQ	0x02b
#define FADDQ	0x043
#define FSUBQ	0x047
#define FMULQ	0x04b
#define FDIVQ	0x04f
#define FDMULQ	0x06e
#define FQTOX	0x083
#define FXTOQ	0x08c
#define FQTOS	0x0c7
#define FQTOD	0x0cb
#define FITOQ	0x0cc
#define FSTOQ	0x0cd
#define FDTOQ	0x0ce
#define FQTOI	0x0d3
/* SUBNORMAL - ftt == 2 */
#define FSQRTS	0x029
#define FSQRTD	0x02a
#define FADDS	0x041
#define FADDD	0x042
#define FSUBS	0x045
#define FSUBD	0x046
#define FMULS	0x049
#define FMULD	0x04a
#define FDIVS	0x04d
#define FDIVD	0x04e
#define FSMULD	0x069
#define FSTOX	0x081
#define FDTOX	0x082
#define FDTOS	0x0c6
#define FSTOD	0x0c9
#define FSTOI	0x0d1
#define FDTOI	0x0d2
#define FXTOS	0x084 /* Only Ultra-III generates this. */
#define FXTOD	0x088 /* Only Ultra-III generates this. */
#if 0	/* Optimized inline in sparc64/kernel/entry.S */
#define FITOS	0x0c4 /* Only Ultra-III generates this. */
#endif
#define FITOD	0x0c8 /* Only Ultra-III generates this. */
/* FPOP2 */
#define FCMPQ	0x053
#define FCMPEQ	0x057
#define FMOVQ0	0x003
#define FMOVQ1	0x043
#define FMOVQ2	0x083
#define FMOVQ3	0x0c3
#define FMOVQI	0x103
#define FMOVQX	0x183
#define FMOVQZ	0x027
#define FMOVQLE	0x047
#define FMOVQLZ 0x067
#define FMOVQNZ	0x0a7
#define FMOVQGZ	0x0c7
#define FMOVQGE 0x0e7

#define FSR_TEM_SHIFT	23UL
#define FSR_TEM_MASK	(0x1fUL << FSR_TEM_SHIFT)
#define FSR_AEXC_SHIFT	5UL
#define FSR_AEXC_MASK	(0x1fUL << FSR_AEXC_SHIFT)
#define FSR_CEXC_SHIFT	0UL
#define FSR_CEXC_MASK	(0x1fUL << FSR_CEXC_SHIFT)

/* All routines returning an exception to raise should detect
 * such exceptions _before_ rounding to be consistent with
 * the behavior of the hardware in the implemented cases
 * (and thus with the recommendations in the V9 architecture
 * manual).
 *
 * We return 0 if a SIGFPE should be sent, 1 otherwise.
 */
static inline int record_exception(struct pt_regs *regs, int eflag)
{
	u64 fsr = current_thread_info()->xfsr[0];
	int would_trap;

	/* Determine if this exception would have generated a trap. */
	would_trap = (fsr & ((long)eflag << FSR_TEM_SHIFT)) != 0UL;

	/* If trapping, we only want to signal one bit. */
	if(would_trap != 0) {
		eflag &= ((fsr & FSR_TEM_MASK) >> FSR_TEM_SHIFT);
		if((eflag & (eflag - 1)) != 0) {
			if(eflag & FP_EX_INVALID)
				eflag = FP_EX_INVALID;
			else if(eflag & FP_EX_OVERFLOW)
				eflag = FP_EX_OVERFLOW;
			else if(eflag & FP_EX_UNDERFLOW)
				eflag = FP_EX_UNDERFLOW;
			else if(eflag & FP_EX_DIVZERO)
				eflag = FP_EX_DIVZERO;
			else if(eflag & FP_EX_INEXACT)
				eflag = FP_EX_INEXACT;
		}
	}

	/* Set CEXC, here is the rule:
	 *
	 *    In general all FPU ops will set one and only one
	 *    bit in the CEXC field, this is always the case
	 *    when the IEEE exception trap is enabled in TEM.
	 */
	fsr &= ~(FSR_CEXC_MASK);
	fsr |= ((long)eflag << FSR_CEXC_SHIFT);

	/* Set the AEXC field, rule is:
	 *
	 *    If a trap would not be generated, the
	 *    CEXC just generated is OR'd into the
	 *    existing value of AEXC.
	 */
	if(would_trap == 0)
		fsr |= ((long)eflag << FSR_AEXC_SHIFT);

	/* If trapping, indicate fault trap type IEEE. */
	if(would_trap != 0)
		fsr |= (1UL << 14);

	current_thread_info()->xfsr[0] = fsr;

	/* If we will not trap, advance the program counter over
	 * the instruction being handled.
	 */
	if(would_trap == 0) {
		regs->tpc = regs->tnpc;
		regs->tnpc += 4;
	}

	return (would_trap ? 0 : 1);
}

typedef union {
	u32 s;
	u64 d;
	u64 q[2];
} *argp;

int do_mathemu(struct pt_regs *regs, struct fpustate *f)
{
	unsigned long pc = regs->tpc;
	unsigned long tstate = regs->tstate;
	u32 insn = 0;
	int type = 0;
	/* ftt tells which ftt it may happen in, r is rd, b is rs2 and a is rs1. The *u arg tells
	   whether the argument should be packed/unpacked (0 - do not unpack/pack, 1 - unpack/pack)
	   non-u args tells the size of the argument (0 - no argument, 1 - single, 2 - double, 3 - quad */
#define TYPE(ftt, r, ru, b, bu, a, au) type = (au << 2) | (a << 0) | (bu << 5) | (b << 3) | (ru << 8) | (r << 6) | (ftt << 9)
	int freg;
	static u64 zero[2] = { 0L, 0L };
	int flags;
	FP_DECL_EX;
	FP_DECL_S(SA); FP_DECL_S(SB); FP_DECL_S(SR);
	FP_DECL_D(DA); FP_DECL_D(DB); FP_DECL_D(DR);
	FP_DECL_Q(QA); FP_DECL_Q(QB); FP_DECL_Q(QR);
	int IR;
	long XR, xfsr;

	if (tstate & TSTATE_PRIV)
		die_if_kernel("unfinished/unimplemented FPop from kernel", regs);
	perf_sw_event(PERF_COUNT_SW_EMULATION_FAULTS, 1, regs, 0);
	if (test_thread_flag(TIF_32BIT))
		pc = (u32)pc;
	if (get_user(insn, (u32 __user *) pc) != -EFAULT) {
		if ((insn & 0xc1f80000) == 0x81a00000) /* FPOP1 */ {
			switch ((insn >> 5) & 0x1ff) {
			/* QUAD - ftt == 3 */
			case FMOVQ:
			case FNEGQ:
			case FABSQ: TYPE(3,3,0,3,0,0,0); break;
			case FSQRTQ: TYPE(3,3,1,3,1,0,0); break;
			case FADDQ:
			case FSUBQ:
			case FMULQ:
			case FDIVQ: TYPE(3,3,1,3,1,3,1); break;
			case FDMULQ: TYPE(3,3,1,2,1,2,1); break;
			case FQTOX: TYPE(3,2,0,3,1,0,0); break;
			case FXTOQ: TYPE(3,3,1,2,0,0,0); break;
			case FQTOS: TYPE(3,1,1,3,1,0,0); break;
			case FQTOD: TYPE(3,2,1,3,1,0,0); break;
			case FITOQ: TYPE(3,3,1,1,0,0,0); break;
			case FSTOQ: TYPE(3,3,1,1,1,0,0); break;
			case FDTOQ: TYPE(3,3,1,2,1,0,0); break;
			case FQTOI: TYPE(3,1,0,3,1,0,0); break;

			/* We can get either unimplemented or unfinished
			 * for these cases.  Pre-Niagara systems generate
			 * unfinished fpop for SUBNORMAL cases, and Niagara
			 * always gives unimplemented fpop for fsqrt{s,d}.
			 */
			case FSQRTS: {
				unsigned long x = current_thread_info()->xfsr[0];

				x = (x >> 14) & 0xf;
				TYPE(x,1,1,1,1,0,0);
				break;
			}

			case FSQRTD: {
				unsigned long x = current_thread_info()->xfsr[0];

				x = (x >> 14) & 0xf;
				TYPE(x,2,1,2,1,0,0);
				break;
			}

			/* SUBNORMAL - ftt == 2 */
			case FADDD:
			case FSUBD:
			case FMULD:
			case FDIVD: TYPE(2,2,1,2,1,2,1); break;
			case FADDS:
			case FSUBS:
			case FMULS:
			case FDIVS: TYPE(2,1,1,1,1,1,1); break;
			case FSMULD: TYPE(2,2,1,1,1,1,1); break;
			case FSTOX: TYPE(2,2,0,1,1,0,0); break;
			case FDTOX: TYPE(2,2,0,2,1,0,0); break;
			case FDTOS: TYPE(2,1,1,2,1,0,0); break;
			case FSTOD: TYPE(2,2,1,1,1,0,0); break;
			case FSTOI: TYPE(2,1,0,1,1,0,0); break;
			case FDTOI: TYPE(2,1,0,2,1,0,0); break;

			/* Only Ultra-III generates these */
			case FXTOS: TYPE(2,1,1,2,0,0,0); break;
			case FXTOD: TYPE(2,2,1,2,0,0,0); break;
#if 0			/* Optimized inline in sparc64/kernel/entry.S */
			case FITOS: TYPE(2,1,1,1,0,0,0); break;
#endif
			case FITOD: TYPE(2,2,1,1,0,0,0); break;
			}
		}
		else if ((insn & 0xc1f80000) == 0x81a80000) /* FPOP2 */ {
			IR = 2;
			switch ((insn >> 5) & 0x1ff) {
			case FCMPQ: TYPE(3,0,0,3,1,3,1); break;
			case FCMPEQ: TYPE(3,0,0,3,1,3,1); break;
			/* Now the conditional fmovq support */
			case FMOVQ0:
			case FMOVQ1:
			case FMOVQ2:
			case FMOVQ3:
				/* fmovq %fccX, %fY, %fZ */
				if (!((insn >> 11) & 3))
					XR = current_thread_info()->xfsr[0] >> 10;
				else
					XR = current_thread_info()->xfsr[0] >> (30 + ((insn >> 10) & 0x6));
				XR &= 3;
				IR = 0;
				switch ((insn >> 14) & 0x7) {
				/* case 0: IR = 0; break; */			/* Never */
				case 1: if (XR) IR = 1; break;			/* Not Equal */
				case 2: if (XR == 1 || XR == 2) IR = 1; break;	/* Less or Greater */
				case 3: if (XR & 1) IR = 1; break;		/* Unordered or Less */
				case 4: if (XR == 1) IR = 1; break;		/* Less */
				case 5: if (XR & 2) IR = 1; break;		/* Unordered or Greater */
				case 6: if (XR == 2) IR = 1; break;		/* Greater */
				case 7: if (XR == 3) IR = 1; break;		/* Unordered */
				}
				if ((insn >> 14) & 8)
					IR ^= 1;
				break;
			case FMOVQI:
			case FMOVQX:
				/* fmovq %[ix]cc, %fY, %fZ */
				XR = regs->tstate >> 32;
				if ((insn >> 5) & 0x80)
					XR >>= 4;
				XR &= 0xf;
				IR = 0;
				freg = ((XR >> 2) ^ XR) & 2;
				switch ((insn >> 14) & 0x7) {
				/* case 0: IR = 0; break; */			/* Never */
				case 1: if (XR & 4) IR = 1; break;		/* Equal */
				case 2: if ((XR & 4) || freg) IR = 1; break;	/* Less or Equal */
				case 3: if (freg) IR = 1; break;		/* Less */
				case 4: if (XR & 5) IR = 1; break;		/* Less or Equal Unsigned */
				case 5: if (XR & 1) IR = 1; break;		/* Carry Set */
				case 6: if (XR & 8) IR = 1; break;		/* Negative */
				case 7: if (XR & 2) IR = 1; break;		/* Overflow Set */
				}
				if ((insn >> 14) & 8)
					IR ^= 1;
				break;
			case FMOVQZ:
			case FMOVQLE:
			case FMOVQLZ:
			case FMOVQNZ:
			case FMOVQGZ:
			case FMOVQGE:
				freg = (insn >> 14) & 0x1f;
				if (!freg)
					XR = 0;
				else if (freg < 16)
					XR = regs->u_regs[freg];
				else if (test_thread_flag(TIF_32BIT)) {
					struct reg_window32 __user *win32;
					flushw_user ();
					win32 = (struct reg_window32 __user *)((unsigned long)((u32)regs->u_regs[UREG_FP]));
					get_user(XR, &win32->locals[freg - 16]);
				} else {
					struct reg_window __user *win;
					flushw_user ();
					win = (struct reg_window __user *)(regs->u_regs[UREG_FP] + STACK_BIAS);
					get_user(XR, &win->locals[freg - 16]);
				}
				IR = 0;
				switch ((insn >> 10) & 3) {
				case 1: if (!XR) IR = 1; break;			/* Register Zero */
				case 2: if (XR <= 0) IR = 1; break;		/* Register Less Than or Equal to Zero */
				case 3: if (XR < 0) IR = 1; break;		/* Register Less Than Zero */
				}
				if ((insn >> 10) & 4)
					IR ^= 1;
				break;
			}
			if (IR == 0) {
				/* The fmov test was false. Do a nop instead */
				current_thread_info()->xfsr[0] &= ~(FSR_CEXC_MASK);
				regs->tpc = regs->tnpc;
				regs->tnpc += 4;
				return 1;
			} else if (IR == 1) {
				/* Change the instruction into plain fmovq */
				insn = (insn & 0x3e00001f) | 0x81a00060;
				TYPE(3,3,0,3,0,0,0); 
			}
		}
	}
	if (type) {
		argp rs1 = NULL, rs2 = NULL, rd = NULL;
		
		freg = (current_thread_info()->xfsr[0] >> 14) & 0xf;
		if (freg != (type >> 9))
			goto err;
		current_thread_info()->xfsr[0] &= ~0x1c000;
		freg = ((insn >> 14) & 0x1f);
		switch (type & 0x3) {
		case 3: if (freg & 2) {
				current_thread_info()->xfsr[0] |= (6 << 14) /* invalid_fp_register */;
				goto err;
			}
		case 2: freg = ((freg & 1) << 5) | (freg & 0x1e);
		case 1: rs1 = (argp)&f->regs[freg];
			flags = (freg < 32) ? FPRS_DL : FPRS_DU; 
			if (!(current_thread_info()->fpsaved[0] & flags))
				rs1 = (argp)&zero;
			break;
		}
		switch (type & 0x7) {
		case 7: FP_UNPACK_QP (QA, rs1); break;
		case 6: FP_UNPACK_DP (DA, rs1); break;
		case 5: FP_UNPACK_SP (SA, rs1); break;
		}
		freg = (insn & 0x1f);
		switch ((type >> 3) & 0x3) {
		case 3: if (freg & 2) {
				current_thread_info()->xfsr[0] |= (6 << 14) /* invalid_fp_register */;
				goto err;
			}
		case 2: freg = ((freg & 1) << 5) | (freg & 0x1e);
		case 1: rs2 = (argp)&f->regs[freg];
			flags = (freg < 32) ? FPRS_DL : FPRS_DU; 
			if (!(current_thread_info()->fpsaved[0] & flags))
				rs2 = (argp)&zero;
			break;
		}
		switch ((type >> 3) & 0x7) {
		case 7: FP_UNPACK_QP (QB, rs2); break;
		case 6: FP_UNPACK_DP (DB, rs2); break;
		case 5: FP_UNPACK_SP (SB, rs2); break;
		}
		freg = ((insn >> 25) & 0x1f);
		switch ((type >> 6) & 0x3) {
		case 3: if (freg & 2) {
				current_thread_info()->xfsr[0] |= (6 << 14) /* invalid_fp_register */;
				goto err;
			}
		case 2: freg = ((freg & 1) << 5) | (freg & 0x1e);
		case 1: rd = (argp)&f->regs[freg];
			flags = (freg < 32) ? FPRS_DL : FPRS_DU; 
			if (!(current_thread_info()->fpsaved[0] & FPRS_FEF)) {
				current_thread_info()->fpsaved[0] = FPRS_FEF;
				current_thread_info()->gsr[0] = 0;
			}
			if (!(current_thread_info()->fpsaved[0] & flags)) {
				if (freg < 32)
					memset(f->regs, 0, 32*sizeof(u32));
				else
					memset(f->regs+32, 0, 32*sizeof(u32));
			}
			current_thread_info()->fpsaved[0] |= flags;
			break;
		}
		switch ((insn >> 5) & 0x1ff) {
		/* + */
		case FADDS: FP_ADD_S (SR, SA, SB); break;
		case FADDD: FP_ADD_D (DR, DA, DB); break;
		case FADDQ: FP_ADD_Q (QR, QA, QB); break;
		/* - */
		case FSUBS: FP_SUB_S (SR, SA, SB); break;
		case FSUBD: FP_SUB_D (DR, DA, DB); break;
		case FSUBQ: FP_SUB_Q (QR, QA, QB); break;
		/* * */
		case FMULS: FP_MUL_S (SR, SA, SB); break;
		case FSMULD: FP_CONV (D, S, 1, 1, DA, SA);
			     FP_CONV (D, S, 1, 1, DB, SB);
		case FMULD: FP_MUL_D (DR, DA, DB); break;
		case FDMULQ: FP_CONV (Q, D, 2, 1, QA, DA);
			     FP_CONV (Q, D, 2, 1, QB, DB);
		case FMULQ: FP_MUL_Q (QR, QA, QB); break;
		/* / */
		case FDIVS: FP_DIV_S (SR, SA, SB); break;
		case FDIVD: FP_DIV_D (DR, DA, DB); break;
		case FDIVQ: FP_DIV_Q (QR, QA, QB); break;
		/* sqrt */
		case FSQRTS: FP_SQRT_S (SR, SB); break;
		case FSQRTD: FP_SQRT_D (DR, DB); break;
		case FSQRTQ: FP_SQRT_Q (QR, QB); break;
		/* mov */
		case FMOVQ: rd->q[0] = rs2->q[0]; rd->q[1] = rs2->q[1]; break;
		case FABSQ: rd->q[0] = rs2->q[0] & 0x7fffffffffffffffUL; rd->q[1] = rs2->q[1]; break;
		case FNEGQ: rd->q[0] = rs2->q[0] ^ 0x8000000000000000UL; rd->q[1] = rs2->q[1]; break;
		/* float to int */
		case FSTOI: FP_TO_INT_S (IR, SB, 32, 1); break;
		case FDTOI: FP_TO_INT_D (IR, DB, 32, 1); break;
		case FQTOI: FP_TO_INT_Q (IR, QB, 32, 1); break;
		case FSTOX: FP_TO_INT_S (XR, SB, 64, 1); break;
		case FDTOX: FP_TO_INT_D (XR, DB, 64, 1); break;
		case FQTOX: FP_TO_INT_Q (XR, QB, 64, 1); break;
		/* int to float */
		case FITOQ: IR = rs2->s; FP_FROM_INT_Q (QR, IR, 32, int); break;
		case FXTOQ: XR = rs2->d; FP_FROM_INT_Q (QR, XR, 64, long); break;
		/* Only Ultra-III generates these */
		case FXTOS: XR = rs2->d; FP_FROM_INT_S (SR, XR, 64, long); break;
		case FXTOD: XR = rs2->d; FP_FROM_INT_D (DR, XR, 64, long); break;
#if 0		/* Optimized inline in sparc64/kernel/entry.S */
		case FITOS: IR = rs2->s; FP_FROM_INT_S (SR, IR, 32, int); break;
#endif
		case FITOD: IR = rs2->s; FP_FROM_INT_D (DR, IR, 32, int); break;
		/* float to float */
		case FSTOD: FP_CONV (D, S, 1, 1, DR, SB); break;
		case FSTOQ: FP_CONV (Q, S, 2, 1, QR, SB); break;
		case FDTOQ: FP_CONV (Q, D, 2, 1, QR, DB); break;
		case FDTOS: FP_CONV (S, D, 1, 1, SR, DB); break;
		case FQTOS: FP_CONV (S, Q, 1, 2, SR, QB); break;
		case FQTOD: FP_CONV (D, Q, 1, 2, DR, QB); break;
		/* comparison */
		case FCMPQ:
		case FCMPEQ:
			FP_CMP_Q(XR, QB, QA, 3);
			if (XR == 3 &&
			    (((insn >> 5) & 0x1ff) == FCMPEQ ||
			     FP_ISSIGNAN_Q(QA) ||
			     FP_ISSIGNAN_Q(QB)))
				FP_SET_EXCEPTION (FP_EX_INVALID);
		}
		if (!FP_INHIBIT_RESULTS) {
			switch ((type >> 6) & 0x7) {
			case 0: xfsr = current_thread_info()->xfsr[0];
				if (XR == -1) XR = 2;
				switch (freg & 3) {
				/* fcc0, 1, 2, 3 */
				case 0: xfsr &= ~0xc00; xfsr |= (XR << 10); break;
				case 1: xfsr &= ~0x300000000UL; xfsr |= (XR << 32); break;
				case 2: xfsr &= ~0xc00000000UL; xfsr |= (XR << 34); break;
				case 3: xfsr &= ~0x3000000000UL; xfsr |= (XR << 36); break;
				}
				current_thread_info()->xfsr[0] = xfsr;
				break;
			case 1: rd->s = IR; break;
			case 2: rd->d = XR; break;
			case 5: FP_PACK_SP (rd, SR); break;
			case 6: FP_PACK_DP (rd, DR); break;
			case 7: FP_PACK_QP (rd, QR); break;
			}
		}

		if(_fex != 0)
			return record_exception(regs, _fex);

		/* Success and no exceptions detected. */
		current_thread_info()->xfsr[0] &= ~(FSR_CEXC_MASK);
		regs->tpc = regs->tnpc;
		regs->tnpc += 4;
		return 1;
	}
err:	return 0;
}
