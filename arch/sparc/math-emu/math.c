/*
 * arch/sparc/math-emu/math.c
 *
 * Copyright (C) 1998 Peter Maydell (pmaydell@chiark.greenend.org.uk)
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 * This is a good place to start if you're trying to understand the
 * emulation code, because it's pretty simple. What we do is
 * essentially analyse the instruction to work out what the operation
 * is and which registers are involved. We then execute the appropriate
 * FXXXX function. [The floating point queue introduces a minor wrinkle;
 * see below...]
 * The fxxxxx.c files each emulate a single insn. They look relatively
 * simple because the complexity is hidden away in an unholy tangle
 * of preprocessor macros.
 *
 * The first layer of macros is single.h, double.h, quad.h. Generally
 * these files define macros for working with floating point numbers
 * of the three IEEE formats. FP_ADD_D(R,A,B) is for adding doubles,
 * for instance. These macros are usually defined as calls to more
 * generic macros (in this case _FP_ADD(D,2,R,X,Y) where the number
 * of machine words required to store the given IEEE format is passed
 * as a parameter. [double.h and co check the number of bits in a word
 * and define FP_ADD_D & co appropriately].
 * The generic macros are defined in op-common.h. This is where all
 * the grotty stuff like handling NaNs is coded. To handle the possible
 * word sizes macros in op-common.h use macros like _FP_FRAC_SLL_##wc()
 * where wc is the 'number of machine words' parameter (here 2).
 * These are defined in the third layer of macros: op-1.h, op-2.h
 * and op-4.h. These handle operations on floating point numbers composed
 * of 1,2 and 4 machine words respectively. [For example, on sparc64
 * doubles are one machine word so macros in double.h eventually use
 * constructs in op-1.h, but on sparc32 they use op-2.h definitions.]
 * soft-fp.h is on the same level as op-common.h, and defines some
 * macros which are independent of both word size and FP format.
 * Finally, sfp-machine.h is the machine dependent part of the
 * code: it defines the word size and what type a word is. It also
 * defines how _FP_MUL_MEAT_t() maps to _FP_MUL_MEAT_n_* : op-n.h
 * provide several possible flavours of multiply algorithm, most
 * of which require that you supply some form of asm or C primitive to
 * do the actual multiply. (such asm primitives should be defined
 * in sfp-machine.h too). udivmodti4.c is the same sort of thing.
 *
 * There may be some errors here because I'm working from a
 * SPARC architecture manual V9, and what I really want is V8...
 * Also, the insns which can generate exceptions seem to be a
 * greater subset of the FPops than for V9 (for example, FCMPED
 * has to be emulated on V8). So I think I'm going to have
 * to emulate them all just to be on the safe side...
 *
 * Emulation routines originate from soft-fp package, which is
 * part of glibc and has appropriate copyrights in it (allegedly).
 *
 * NB: on sparc int == long == 4 bytes, long long == 8 bytes.
 * Most bits of the kernel seem to go for long rather than int,
 * so we follow that practice...
 */

/* TODO:
 * fpsave() saves the FP queue but fpload() doesn't reload it.
 * Therefore when we context switch or change FPU ownership
 * we have to check to see if the queue had anything in it and
 * emulate it if it did. This is going to be a pain.
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/uaccess.h>

#include "sfp-util.h"
#include <math-emu/soft-fp.h>
#include <math-emu/single.h>
#include <math-emu/double.h>
#include <math-emu/quad.h>

#define FLOATFUNC(x) extern int x(void *,void *,void *)

/* The Vn labels indicate what version of the SPARC architecture gas thinks
 * each insn is. This is from the binutils source :->
 */
/* quadword instructions */
#define FSQRTQ	0x02b		/* v8 */
#define FADDQ	0x043		/* v8 */
#define FSUBQ	0x047		/* v8 */
#define FMULQ	0x04b		/* v8 */
#define FDIVQ	0x04f		/* v8 */
#define FDMULQ	0x06e		/* v8 */
#define FQTOS	0x0c7		/* v8 */
#define FQTOD	0x0cb		/* v8 */
#define FITOQ	0x0cc		/* v8 */
#define FSTOQ	0x0cd		/* v8 */
#define FDTOQ	0x0ce		/* v8 */
#define FQTOI	0x0d3		/* v8 */
#define FCMPQ	0x053		/* v8 */
#define FCMPEQ	0x057		/* v8 */
/* single/double instructions (subnormal): should all work */
#define FSQRTS	0x029		/* v7 */
#define FSQRTD	0x02a		/* v7 */
#define FADDS	0x041		/* v6 */
#define FADDD	0x042		/* v6 */
#define FSUBS	0x045		/* v6 */
#define FSUBD	0x046		/* v6 */
#define FMULS	0x049		/* v6 */
#define FMULD	0x04a		/* v6 */
#define FDIVS	0x04d		/* v6 */
#define FDIVD	0x04e		/* v6 */
#define FSMULD	0x069		/* v6 */
#define FDTOS	0x0c6		/* v6 */
#define FSTOD	0x0c9		/* v6 */
#define FSTOI	0x0d1		/* v6 */
#define FDTOI	0x0d2		/* v6 */
#define FABSS	0x009		/* v6 */
#define FCMPS	0x051		/* v6 */
#define FCMPES	0x055		/* v6 */
#define FCMPD	0x052		/* v6 */
#define FCMPED	0x056		/* v6 */
#define FMOVS	0x001		/* v6 */
#define FNEGS	0x005		/* v6 */
#define FITOS	0x0c4		/* v6 */
#define FITOD	0x0c8		/* v6 */

#define FSR_TEM_SHIFT	23UL
#define FSR_TEM_MASK	(0x1fUL << FSR_TEM_SHIFT)
#define FSR_AEXC_SHIFT	5UL
#define FSR_AEXC_MASK	(0x1fUL << FSR_AEXC_SHIFT)
#define FSR_CEXC_SHIFT	0UL
#define FSR_CEXC_MASK	(0x1fUL << FSR_CEXC_SHIFT)

static int do_one_mathemu(u32 insn, unsigned long *fsr, unsigned long *fregs);

/* Unlike the Sparc64 version (which has a struct fpustate), we
 * pass the taskstruct corresponding to the task which currently owns the
 * FPU. This is partly because we don't have the fpustate struct and
 * partly because the task owning the FPU isn't always current (as is
 * the case for the Sparc64 port). This is probably SMP-related...
 * This function returns 1 if all queued insns were emulated successfully.
 * The test for unimplemented FPop in kernel mode has been moved into
 * kernel/traps.c for simplicity.
 */
int do_mathemu(struct pt_regs *regs, struct task_struct *fpt)
{
	/* regs->pc isn't necessarily the PC at which the offending insn is sitting.
	 * The FPU maintains a queue of FPops which cause traps.
	 * When it hits an instruction that requires that the trapped op succeeded
	 * (usually because it reads a reg. that the trapped op wrote) then it
	 * causes this exception. We need to emulate all the insns on the queue
	 * and then allow the op to proceed.
	 * This code should also handle the case where the trap was precise,
	 * in which case the queue length is zero and regs->pc points at the
	 * single FPop to be emulated. (this case is untested, though :->)
	 * You'll need this case if you want to be able to emulate all FPops
	 * because the FPU either doesn't exist or has been software-disabled.
	 * [The UltraSPARC makes FP a precise trap; this isn't as stupid as it
	 * might sound because the Ultra does funky things with a superscalar
	 * architecture.]
	 */

	/* You wouldn't believe how often I typed 'ftp' when I meant 'fpt' :-> */

	int i;
	int retcode = 0;                               /* assume all succeed */
	unsigned long insn;

#ifdef DEBUG_MATHEMU
	printk("In do_mathemu()... pc is %08lx\n", regs->pc);
	printk("fpqdepth is %ld\n", fpt->thread.fpqdepth);
	for (i = 0; i < fpt->thread.fpqdepth; i++)
		printk("%d: %08lx at %08lx\n", i, fpt->thread.fpqueue[i].insn,
		       (unsigned long)fpt->thread.fpqueue[i].insn_addr);
#endif

	if (fpt->thread.fpqdepth == 0) {                   /* no queue, guilty insn is at regs->pc */
#ifdef DEBUG_MATHEMU
		printk("precise trap at %08lx\n", regs->pc);
#endif
		if (!get_user(insn, (u32 __user *) regs->pc)) {
			retcode = do_one_mathemu(insn, &fpt->thread.fsr, fpt->thread.float_regs);
			if (retcode) {
				/* in this case we need to fix up PC & nPC */
				regs->pc = regs->npc;
				regs->npc += 4;
			}
		}
		return retcode;
	}

	/* Normal case: need to empty the queue... */
	for (i = 0; i < fpt->thread.fpqdepth; i++) {
		retcode = do_one_mathemu(fpt->thread.fpqueue[i].insn, &(fpt->thread.fsr), fpt->thread.float_regs);
		if (!retcode)                               /* insn failed, no point doing any more */
			break;
	}
	/* Now empty the queue and clear the queue_not_empty flag */
	if (retcode)
		fpt->thread.fsr &= ~(0x3000 | FSR_CEXC_MASK);
	else
		fpt->thread.fsr &= ~0x3000;
	fpt->thread.fpqdepth = 0;

	return retcode;
}

/* All routines returning an exception to raise should detect
 * such exceptions _before_ rounding to be consistent with
 * the behavior of the hardware in the implemented cases
 * (and thus with the recommendations in the V9 architecture
 * manual).
 *
 * We return 0 if a SIGFPE should be sent, 1 otherwise.
 */
static inline int record_exception(unsigned long *pfsr, int eflag)
{
	unsigned long fsr = *pfsr;
	int would_trap;

	/* Determine if this exception would have generated a trap. */
	would_trap = (fsr & ((long)eflag << FSR_TEM_SHIFT)) != 0UL;

	/* If trapping, we only want to signal one bit. */
	if (would_trap != 0) {
		eflag &= ((fsr & FSR_TEM_MASK) >> FSR_TEM_SHIFT);
		if ((eflag & (eflag - 1)) != 0) {
			if (eflag & FP_EX_INVALID)
				eflag = FP_EX_INVALID;
			else if (eflag & FP_EX_OVERFLOW)
				eflag = FP_EX_OVERFLOW;
			else if (eflag & FP_EX_UNDERFLOW)
				eflag = FP_EX_UNDERFLOW;
			else if (eflag & FP_EX_DIVZERO)
				eflag = FP_EX_DIVZERO;
			else if (eflag & FP_EX_INEXACT)
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
	if (would_trap == 0)
		fsr |= ((long)eflag << FSR_AEXC_SHIFT);

	/* If trapping, indicate fault trap type IEEE. */
	if (would_trap != 0)
		fsr |= (1UL << 14);

	*pfsr = fsr;

	return (would_trap ? 0 : 1);
}

typedef union {
	u32 s;
	u64 d;
	u64 q[2];
} *argp;

static int do_one_mathemu(u32 insn, unsigned long *pfsr, unsigned long *fregs)
{
	/* Emulate the given insn, updating fsr and fregs appropriately. */
	int type = 0;
	/* r is rd, b is rs2 and a is rs1. The *u arg tells
	   whether the argument should be packed/unpacked (0 - do not unpack/pack, 1 - unpack/pack)
	   non-u args tells the size of the argument (0 - no argument, 1 - single, 2 - double, 3 - quad */
#define TYPE(dummy, r, ru, b, bu, a, au) type = (au << 2) | (a << 0) | (bu << 5) | (b << 3) | (ru << 8) | (r << 6)
	int freg;
	argp rs1 = NULL, rs2 = NULL, rd = NULL;
	FP_DECL_EX;
	FP_DECL_S(SA); FP_DECL_S(SB); FP_DECL_S(SR);
	FP_DECL_D(DA); FP_DECL_D(DB); FP_DECL_D(DR);
	FP_DECL_Q(QA); FP_DECL_Q(QB); FP_DECL_Q(QR);
	int IR;
	long fsr;

#ifdef DEBUG_MATHEMU
	printk("In do_mathemu(), emulating %08lx\n", insn);
#endif

	if ((insn & 0xc1f80000) == 0x81a00000)	/* FPOP1 */ {
		switch ((insn >> 5) & 0x1ff) {
		case FSQRTQ: TYPE(3,3,1,3,1,0,0); break;
		case FADDQ:
		case FSUBQ:
		case FMULQ:
		case FDIVQ: TYPE(3,3,1,3,1,3,1); break;
		case FDMULQ: TYPE(3,3,1,2,1,2,1); break;
		case FQTOS: TYPE(3,1,1,3,1,0,0); break;
		case FQTOD: TYPE(3,2,1,3,1,0,0); break;
		case FITOQ: TYPE(3,3,1,1,0,0,0); break;
		case FSTOQ: TYPE(3,3,1,1,1,0,0); break;
		case FDTOQ: TYPE(3,3,1,2,1,0,0); break;
		case FQTOI: TYPE(3,1,0,3,1,0,0); break;
		case FSQRTS: TYPE(2,1,1,1,1,0,0); break;
		case FSQRTD: TYPE(2,2,1,2,1,0,0); break;
		case FADDD:
		case FSUBD:
		case FMULD:
		case FDIVD: TYPE(2,2,1,2,1,2,1); break;
		case FADDS:
		case FSUBS:
		case FMULS:
		case FDIVS: TYPE(2,1,1,1,1,1,1); break;
		case FSMULD: TYPE(2,2,1,1,1,1,1); break;
		case FDTOS: TYPE(2,1,1,2,1,0,0); break;
		case FSTOD: TYPE(2,2,1,1,1,0,0); break;
		case FSTOI: TYPE(2,1,0,1,1,0,0); break;
		case FDTOI: TYPE(2,1,0,2,1,0,0); break;
		case FITOS: TYPE(2,1,1,1,0,0,0); break;
		case FITOD: TYPE(2,2,1,1,0,0,0); break;
		case FMOVS:
		case FABSS:
		case FNEGS: TYPE(2,1,0,1,0,0,0); break;
		default:
#ifdef DEBUG_MATHEMU
			printk("unknown FPop1: %03lx\n",(insn>>5)&0x1ff);
#endif
			break;
		}
	} else if ((insn & 0xc1f80000) == 0x81a80000)	/* FPOP2 */ {
		switch ((insn >> 5) & 0x1ff) {
		case FCMPS: TYPE(3,0,0,1,1,1,1); break;
		case FCMPES: TYPE(3,0,0,1,1,1,1); break;
		case FCMPD: TYPE(3,0,0,2,1,2,1); break;
		case FCMPED: TYPE(3,0,0,2,1,2,1); break;
		case FCMPQ: TYPE(3,0,0,3,1,3,1); break;
		case FCMPEQ: TYPE(3,0,0,3,1,3,1); break;
		default:
#ifdef DEBUG_MATHEMU
			printk("unknown FPop2: %03lx\n",(insn>>5)&0x1ff);
#endif
			break;
		}
	}

	if (!type) {	/* oops, didn't recognise that FPop */
#ifdef DEBUG_MATHEMU
		printk("attempt to emulate unrecognised FPop!\n");
#endif
		return 0;
	}

	/* Decode the registers to be used */
	freg = (*pfsr >> 14) & 0xf;

	*pfsr &= ~0x1c000;				/* clear the traptype bits */
	
	freg = ((insn >> 14) & 0x1f);
	switch (type & 0x3) {				/* is rs1 single, double or quad? */
	case 3:
		if (freg & 3) {				/* quadwords must have bits 4&5 of the */
							/* encoded reg. number set to zero. */
			*pfsr |= (6 << 14);
			return 0;			/* simulate invalid_fp_register exception */
		}
	/* fall through */
	case 2:
		if (freg & 1) {				/* doublewords must have bit 5 zeroed */
			*pfsr |= (6 << 14);
			return 0;
		}
	}
	rs1 = (argp)&fregs[freg];
	switch (type & 0x7) {
	case 7: FP_UNPACK_QP (QA, rs1); break;
	case 6: FP_UNPACK_DP (DA, rs1); break;
	case 5: FP_UNPACK_SP (SA, rs1); break;
	}
	freg = (insn & 0x1f);
	switch ((type >> 3) & 0x3) {			/* same again for rs2 */
	case 3:
		if (freg & 3) {				/* quadwords must have bits 4&5 of the */
							/* encoded reg. number set to zero. */
			*pfsr |= (6 << 14);
			return 0;			/* simulate invalid_fp_register exception */
		}
	/* fall through */
	case 2:
		if (freg & 1) {				/* doublewords must have bit 5 zeroed */
			*pfsr |= (6 << 14);
			return 0;
		}
	}
	rs2 = (argp)&fregs[freg];
	switch ((type >> 3) & 0x7) {
	case 7: FP_UNPACK_QP (QB, rs2); break;
	case 6: FP_UNPACK_DP (DB, rs2); break;
	case 5: FP_UNPACK_SP (SB, rs2); break;
	}
	freg = ((insn >> 25) & 0x1f);
	switch ((type >> 6) & 0x3) {			/* and finally rd. This one's a bit different */
	case 0:						/* dest is fcc. (this must be FCMPQ or FCMPEQ) */
		if (freg) {				/* V8 has only one set of condition codes, so */
							/* anything but 0 in the rd field is an error */
			*pfsr |= (6 << 14);		/* (should probably flag as invalid opcode */
			return 0;			/* but SIGFPE will do :-> ) */
		}
		break;
	case 3:
		if (freg & 3) {				/* quadwords must have bits 4&5 of the */
							/* encoded reg. number set to zero. */
			*pfsr |= (6 << 14);
			return 0;			/* simulate invalid_fp_register exception */
		}
	/* fall through */
	case 2:
		if (freg & 1) {				/* doublewords must have bit 5 zeroed */
			*pfsr |= (6 << 14);
			return 0;
		}
	/* fall through */
	case 1:
		rd = (void *)&fregs[freg];
		break;
	}
#ifdef DEBUG_MATHEMU
	printk("executing insn...\n");
#endif
	/* do the Right Thing */
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
	case FSMULD: FP_CONV (D, S, 2, 1, DA, SA);
		     FP_CONV (D, S, 2, 1, DB, SB);
	case FMULD: FP_MUL_D (DR, DA, DB); break;
	case FDMULQ: FP_CONV (Q, D, 4, 2, QA, DA);
		     FP_CONV (Q, D, 4, 2, QB, DB);
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
	case FMOVS: rd->s = rs2->s; break;
	case FABSS: rd->s = rs2->s & 0x7fffffff; break;
	case FNEGS: rd->s = rs2->s ^ 0x80000000; break;
	/* float to int */
	case FSTOI: FP_TO_INT_S (IR, SB, 32, 1); break;
	case FDTOI: FP_TO_INT_D (IR, DB, 32, 1); break;
	case FQTOI: FP_TO_INT_Q (IR, QB, 32, 1); break;
	/* int to float */
	case FITOS: IR = rs2->s; FP_FROM_INT_S (SR, IR, 32, int); break;
	case FITOD: IR = rs2->s; FP_FROM_INT_D (DR, IR, 32, int); break;
	case FITOQ: IR = rs2->s; FP_FROM_INT_Q (QR, IR, 32, int); break;
	/* float to float */
	case FSTOD: FP_CONV (D, S, 2, 1, DR, SB); break;
	case FSTOQ: FP_CONV (Q, S, 4, 1, QR, SB); break;
	case FDTOQ: FP_CONV (Q, D, 4, 2, QR, DB); break;
	case FDTOS: FP_CONV (S, D, 1, 2, SR, DB); break;
	case FQTOS: FP_CONV (S, Q, 1, 4, SR, QB); break;
	case FQTOD: FP_CONV (D, Q, 2, 4, DR, QB); break;
	/* comparison */
	case FCMPS:
	case FCMPES:
		FP_CMP_S(IR, SB, SA, 3);
		if (IR == 3 &&
		    (((insn >> 5) & 0x1ff) == FCMPES ||
		     FP_ISSIGNAN_S(SA) ||
		     FP_ISSIGNAN_S(SB)))
			FP_SET_EXCEPTION (FP_EX_INVALID);
		break;
	case FCMPD:
	case FCMPED:
		FP_CMP_D(IR, DB, DA, 3);
		if (IR == 3 &&
		    (((insn >> 5) & 0x1ff) == FCMPED ||
		     FP_ISSIGNAN_D(DA) ||
		     FP_ISSIGNAN_D(DB)))
			FP_SET_EXCEPTION (FP_EX_INVALID);
		break;
	case FCMPQ:
	case FCMPEQ:
		FP_CMP_Q(IR, QB, QA, 3);
		if (IR == 3 &&
		    (((insn >> 5) & 0x1ff) == FCMPEQ ||
		     FP_ISSIGNAN_Q(QA) ||
		     FP_ISSIGNAN_Q(QB)))
			FP_SET_EXCEPTION (FP_EX_INVALID);
	}
	if (!FP_INHIBIT_RESULTS) {
		switch ((type >> 6) & 0x7) {
		case 0: fsr = *pfsr;
			if (IR == -1) IR = 2;
			/* fcc is always fcc0 */
			fsr &= ~0xc00; fsr |= (IR << 10); break;
			*pfsr = fsr;
			break;
		case 1: rd->s = IR; break;
		case 5: FP_PACK_SP (rd, SR); break;
		case 6: FP_PACK_DP (rd, DR); break;
		case 7: FP_PACK_QP (rd, QR); break;
		}
	}
	if (_fex == 0)
		return 1;				/* success! */
	return record_exception(pfsr, _fex);
}
