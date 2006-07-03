/*
 * Copyright (C) 1999  Eddie C. Dost  (ecd@atecom.com)
 */

#include <linux/types.h>
#include <linux/sched.h>

#include <asm/uaccess.h>
#include <asm/reg.h>

#include "sfp-machine.h"
#include "double.h"

#define FLOATFUNC(x)	extern int x(void *, void *, void *, void *)

FLOATFUNC(fadd);
FLOATFUNC(fadds);
FLOATFUNC(fdiv);
FLOATFUNC(fdivs);
FLOATFUNC(fmul);
FLOATFUNC(fmuls);
FLOATFUNC(fsub);
FLOATFUNC(fsubs);

FLOATFUNC(fmadd);
FLOATFUNC(fmadds);
FLOATFUNC(fmsub);
FLOATFUNC(fmsubs);
FLOATFUNC(fnmadd);
FLOATFUNC(fnmadds);
FLOATFUNC(fnmsub);
FLOATFUNC(fnmsubs);

FLOATFUNC(fctiw);
FLOATFUNC(fctiwz);
FLOATFUNC(frsp);

FLOATFUNC(fcmpo);
FLOATFUNC(fcmpu);

FLOATFUNC(mcrfs);
FLOATFUNC(mffs);
FLOATFUNC(mtfsb0);
FLOATFUNC(mtfsb1);
FLOATFUNC(mtfsf);
FLOATFUNC(mtfsfi);

FLOATFUNC(lfd);
FLOATFUNC(lfs);

FLOATFUNC(stfd);
FLOATFUNC(stfs);
FLOATFUNC(stfiwx);

FLOATFUNC(fabs);
FLOATFUNC(fmr);
FLOATFUNC(fnabs);
FLOATFUNC(fneg);

/* Optional */
FLOATFUNC(fres);
FLOATFUNC(frsqrte);
FLOATFUNC(fsel);
FLOATFUNC(fsqrt);
FLOATFUNC(fsqrts);


#define OP31		0x1f		/*   31 */
#define LFS		0x30		/*   48 */
#define LFSU		0x31		/*   49 */
#define LFD		0x32		/*   50 */
#define LFDU		0x33		/*   51 */
#define STFS		0x34		/*   52 */
#define STFSU		0x35		/*   53 */
#define STFD		0x36		/*   54 */
#define STFDU		0x37		/*   55 */
#define OP59		0x3b		/*   59 */
#define OP63		0x3f		/*   63 */

/* Opcode 31: */
/* X-Form: */
#define LFSX		0x217		/*  535 */
#define LFSUX		0x237		/*  567 */
#define LFDX		0x257		/*  599 */
#define LFDUX		0x277		/*  631 */
#define STFSX		0x297		/*  663 */
#define STFSUX		0x2b7		/*  695 */
#define STFDX		0x2d7		/*  727 */
#define STFDUX		0x2f7		/*  759 */
#define STFIWX		0x3d7		/*  983 */

/* Opcode 59: */
/* A-Form: */
#define FDIVS		0x012		/*   18 */
#define FSUBS		0x014		/*   20 */
#define FADDS		0x015		/*   21 */
#define FSQRTS		0x016		/*   22 */
#define FRES		0x018		/*   24 */
#define FMULS		0x019		/*   25 */
#define FMSUBS		0x01c		/*   28 */
#define FMADDS		0x01d		/*   29 */
#define FNMSUBS		0x01e		/*   30 */
#define FNMADDS		0x01f		/*   31 */

/* Opcode 63: */
/* A-Form: */
#define FDIV		0x012		/*   18 */
#define FSUB		0x014		/*   20 */
#define FADD		0x015		/*   21 */
#define FSQRT		0x016		/*   22 */
#define FSEL		0x017		/*   23 */
#define FMUL		0x019		/*   25 */
#define FRSQRTE		0x01a		/*   26 */
#define FMSUB		0x01c		/*   28 */
#define FMADD		0x01d		/*   29 */
#define FNMSUB		0x01e		/*   30 */
#define FNMADD		0x01f		/*   31 */

/* X-Form: */
#define FCMPU		0x000		/*    0	*/
#define FRSP		0x00c		/*   12 */
#define FCTIW		0x00e		/*   14 */
#define FCTIWZ		0x00f		/*   15 */
#define FCMPO		0x020		/*   32 */
#define MTFSB1		0x026		/*   38 */
#define FNEG		0x028		/*   40 */
#define MCRFS		0x040		/*   64 */
#define MTFSB0		0x046		/*   70 */
#define FMR		0x048		/*   72 */
#define MTFSFI		0x086		/*  134 */
#define FNABS		0x088		/*  136 */
#define FABS		0x108		/*  264 */
#define MFFS		0x247		/*  583 */
#define MTFSF		0x2c7		/*  711 */


#define AB	2
#define AC	3
#define ABC	4
#define D	5
#define DU	6
#define X	7
#define XA	8
#define XB	9
#define XCR	11
#define XCRB	12
#define XCRI	13
#define XCRL	16
#define XE	14
#define XEU	15
#define XFLB	10

#ifdef CONFIG_MATH_EMULATION
static int
record_exception(struct pt_regs *regs, int eflag)
{
	u32 fpscr;

	fpscr = __FPU_FPSCR;

	if (eflag) {
		fpscr |= FPSCR_FX;
		if (eflag & EFLAG_OVERFLOW)
			fpscr |= FPSCR_OX;
		if (eflag & EFLAG_UNDERFLOW)
			fpscr |= FPSCR_UX;
		if (eflag & EFLAG_DIVZERO)
			fpscr |= FPSCR_ZX;
		if (eflag & EFLAG_INEXACT)
			fpscr |= FPSCR_XX;
		if (eflag & EFLAG_VXSNAN)
			fpscr |= FPSCR_VXSNAN;
		if (eflag & EFLAG_VXISI)
			fpscr |= FPSCR_VXISI;
		if (eflag & EFLAG_VXIDI)
			fpscr |= FPSCR_VXIDI;
		if (eflag & EFLAG_VXZDZ)
			fpscr |= FPSCR_VXZDZ;
		if (eflag & EFLAG_VXIMZ)
			fpscr |= FPSCR_VXIMZ;
		if (eflag & EFLAG_VXVC)
			fpscr |= FPSCR_VXVC;
		if (eflag & EFLAG_VXSOFT)
			fpscr |= FPSCR_VXSOFT;
		if (eflag & EFLAG_VXSQRT)
			fpscr |= FPSCR_VXSQRT;
		if (eflag & EFLAG_VXCVI)
			fpscr |= FPSCR_VXCVI;
	}

	fpscr &= ~(FPSCR_VX);
	if (fpscr & (FPSCR_VXSNAN | FPSCR_VXISI | FPSCR_VXIDI |
		     FPSCR_VXZDZ | FPSCR_VXIMZ | FPSCR_VXVC |
		     FPSCR_VXSOFT | FPSCR_VXSQRT | FPSCR_VXCVI))
		fpscr |= FPSCR_VX;

	fpscr &= ~(FPSCR_FEX);
	if (((fpscr & FPSCR_VX) && (fpscr & FPSCR_VE)) ||
	    ((fpscr & FPSCR_OX) && (fpscr & FPSCR_OE)) ||
	    ((fpscr & FPSCR_UX) && (fpscr & FPSCR_UE)) ||
	    ((fpscr & FPSCR_ZX) && (fpscr & FPSCR_ZE)) ||
	    ((fpscr & FPSCR_XX) && (fpscr & FPSCR_XE)))
		fpscr |= FPSCR_FEX;

	__FPU_FPSCR = fpscr;

	return (fpscr & FPSCR_FEX) ? 1 : 0;
}
#endif /* CONFIG_MATH_EMULATION */

int
do_mathemu(struct pt_regs *regs)
{
	void *op0 = 0, *op1 = 0, *op2 = 0, *op3 = 0;
	unsigned long pc = regs->nip;
	signed short sdisp;
	u32 insn = 0;
	int idx = 0;
#ifdef CONFIG_MATH_EMULATION
	int (*func)(void *, void *, void *, void *);
	int type = 0;
	int eflag, trap;
#endif

	if (get_user(insn, (u32 *)pc))
		return -EFAULT;

#ifndef CONFIG_MATH_EMULATION
	switch (insn >> 26) {
	case LFD:
		idx = (insn >> 16) & 0x1f;
		sdisp = (insn & 0xffff);
		op0 = (void *)&current->thread.fpr[(insn >> 21) & 0x1f];
		op1 = (void *)((idx ? regs->gpr[idx] : 0) + sdisp);
		lfd(op0, op1, op2, op3);
		break;
	case LFDU:
		idx = (insn >> 16) & 0x1f;
		sdisp = (insn & 0xffff);
		op0 = (void *)&current->thread.fpr[(insn >> 21) & 0x1f];
		op1 = (void *)((idx ? regs->gpr[idx] : 0) + sdisp);
		lfd(op0, op1, op2, op3);
		regs->gpr[idx] = (unsigned long)op1;
		break;
	case STFD:
		idx = (insn >> 16) & 0x1f;
		sdisp = (insn & 0xffff);
		op0 = (void *)&current->thread.fpr[(insn >> 21) & 0x1f];
		op1 = (void *)((idx ? regs->gpr[idx] : 0) + sdisp);
		stfd(op0, op1, op2, op3);
		break;
	case STFDU:
		idx = (insn >> 16) & 0x1f;
		sdisp = (insn & 0xffff);
		op0 = (void *)&current->thread.fpr[(insn >> 21) & 0x1f];
		op1 = (void *)((idx ? regs->gpr[idx] : 0) + sdisp);
		stfd(op0, op1, op2, op3);
		regs->gpr[idx] = (unsigned long)op1;
		break;
	case OP63:
		op0 = (void *)&current->thread.fpr[(insn >> 21) & 0x1f];
		op1 = (void *)&current->thread.fpr[(insn >> 11) & 0x1f];
		fmr(op0, op1, op2, op3);
		break;
	default:
		goto illegal;
	}
#else /* CONFIG_MATH_EMULATION */
	switch (insn >> 26) {
	case LFS:	func = lfs;	type = D;	break;
	case LFSU:	func = lfs;	type = DU;	break;
	case LFD:	func = lfd;	type = D;	break;
	case LFDU:	func = lfd;	type = DU;	break;
	case STFS:	func = stfs;	type = D;	break;
	case STFSU:	func = stfs;	type = DU;	break;
	case STFD:	func = stfd;	type = D;	break;
	case STFDU:	func = stfd;	type = DU;	break;

	case OP31:
		switch ((insn >> 1) & 0x3ff) {
		case LFSX:	func = lfs;	type = XE;	break;
		case LFSUX:	func = lfs;	type = XEU;	break;
		case LFDX:	func = lfd;	type = XE;	break;
		case LFDUX:	func = lfd;	type = XEU;	break;
		case STFSX:	func = stfs;	type = XE;	break;
		case STFSUX:	func = stfs;	type = XEU;	break;
		case STFDX:	func = stfd;	type = XE;	break;
		case STFDUX:	func = stfd;	type = XEU;	break;
		case STFIWX:	func = stfiwx;	type = XE;	break;
		default:
			goto illegal;
		}
		break;

	case OP59:
		switch ((insn >> 1) & 0x1f) {
		case FDIVS:	func = fdivs;	type = AB;	break;
		case FSUBS:	func = fsubs;	type = AB;	break;
		case FADDS:	func = fadds;	type = AB;	break;
		case FSQRTS:	func = fsqrts;	type = AB;	break;
		case FRES:	func = fres;	type = AB;	break;
		case FMULS:	func = fmuls;	type = AC;	break;
		case FMSUBS:	func = fmsubs;	type = ABC;	break;
		case FMADDS:	func = fmadds;	type = ABC;	break;
		case FNMSUBS:	func = fnmsubs;	type = ABC;	break;
		case FNMADDS:	func = fnmadds;	type = ABC;	break;
		default:
			goto illegal;
		}
		break;

	case OP63:
		if (insn & 0x20) {
			switch ((insn >> 1) & 0x1f) {
			case FDIV:	func = fdiv;	type = AB;	break;
			case FSUB:	func = fsub;	type = AB;	break;
			case FADD:	func = fadd;	type = AB;	break;
			case FSQRT:	func = fsqrt;	type = AB;	break;
			case FSEL:	func = fsel;	type = ABC;	break;
			case FMUL:	func = fmul;	type = AC;	break;
			case FRSQRTE:	func = frsqrte;	type = AB;	break;
			case FMSUB:	func = fmsub;	type = ABC;	break;
			case FMADD:	func = fmadd;	type = ABC;	break;
			case FNMSUB:	func = fnmsub;	type = ABC;	break;
			case FNMADD:	func = fnmadd;	type = ABC;	break;
			default:
				goto illegal;
			}
			break;
		}

		switch ((insn >> 1) & 0x3ff) {
		case FCMPU:	func = fcmpu;	type = XCR;	break;
		case FRSP:	func = frsp;	type = XB;	break;
		case FCTIW:	func = fctiw;	type = XB;	break;
		case FCTIWZ:	func = fctiwz;	type = XB;	break;
		case FCMPO:	func = fcmpo;	type = XCR;	break;
		case MTFSB1:	func = mtfsb1;	type = XCRB;	break;
		case FNEG:	func = fneg;	type = XB;	break;
		case MCRFS:	func = mcrfs;	type = XCRL;	break;
		case MTFSB0:	func = mtfsb0;	type = XCRB;	break;
		case FMR:	func = fmr;	type = XB;	break;
		case MTFSFI:	func = mtfsfi;	type = XCRI;	break;
		case FNABS:	func = fnabs;	type = XB;	break;
		case FABS:	func = fabs;	type = XB;	break;
		case MFFS:	func = mffs;	type = X;	break;
		case MTFSF:	func = mtfsf;	type = XFLB;	break;
		default:
			goto illegal;
		}
		break;

	default:
		goto illegal;
	}

	switch (type) {
	case AB:
		op0 = (void *)&current->thread.fpr[(insn >> 21) & 0x1f];
		op1 = (void *)&current->thread.fpr[(insn >> 16) & 0x1f];
		op2 = (void *)&current->thread.fpr[(insn >> 11) & 0x1f];
		break;

	case AC:
		op0 = (void *)&current->thread.fpr[(insn >> 21) & 0x1f];
		op1 = (void *)&current->thread.fpr[(insn >> 16) & 0x1f];
		op2 = (void *)&current->thread.fpr[(insn >>  6) & 0x1f];
		break;

	case ABC:
		op0 = (void *)&current->thread.fpr[(insn >> 21) & 0x1f];
		op1 = (void *)&current->thread.fpr[(insn >> 16) & 0x1f];
		op2 = (void *)&current->thread.fpr[(insn >> 11) & 0x1f];
		op3 = (void *)&current->thread.fpr[(insn >>  6) & 0x1f];
		break;

	case D:
		idx = (insn >> 16) & 0x1f;
		sdisp = (insn & 0xffff);
		op0 = (void *)&current->thread.fpr[(insn >> 21) & 0x1f];
		op1 = (void *)((idx ? regs->gpr[idx] : 0) + sdisp);
		break;

	case DU:
		idx = (insn >> 16) & 0x1f;
		if (!idx)
			goto illegal;

		sdisp = (insn & 0xffff);
		op0 = (void *)&current->thread.fpr[(insn >> 21) & 0x1f];
		op1 = (void *)(regs->gpr[idx] + sdisp);
		break;

	case X:
		op0 = (void *)&current->thread.fpr[(insn >> 21) & 0x1f];
		break;

	case XA:
		op0 = (void *)&current->thread.fpr[(insn >> 21) & 0x1f];
		op1 = (void *)&current->thread.fpr[(insn >> 16) & 0x1f];
		break;

	case XB:
		op0 = (void *)&current->thread.fpr[(insn >> 21) & 0x1f];
		op1 = (void *)&current->thread.fpr[(insn >> 11) & 0x1f];
		break;

	case XE:
		idx = (insn >> 16) & 0x1f;
		if (!idx)
			goto illegal;

		op0 = (void *)&current->thread.fpr[(insn >> 21) & 0x1f];
		op1 = (void *)(regs->gpr[idx] + regs->gpr[(insn >> 11) & 0x1f]);
		break;

	case XEU:
		idx = (insn >> 16) & 0x1f;
		op0 = (void *)&current->thread.fpr[(insn >> 21) & 0x1f];
		op1 = (void *)((idx ? regs->gpr[idx] : 0)
				+ regs->gpr[(insn >> 11) & 0x1f]);
		break;

	case XCR:
		op0 = (void *)&regs->ccr;
		op1 = (void *)((insn >> 23) & 0x7);
		op2 = (void *)&current->thread.fpr[(insn >> 16) & 0x1f];
		op3 = (void *)&current->thread.fpr[(insn >> 11) & 0x1f];
		break;

	case XCRL:
		op0 = (void *)&regs->ccr;
		op1 = (void *)((insn >> 23) & 0x7);
		op2 = (void *)((insn >> 18) & 0x7);
		break;

	case XCRB:
		op0 = (void *)((insn >> 21) & 0x1f);
		break;

	case XCRI:
		op0 = (void *)((insn >> 23) & 0x7);
		op1 = (void *)((insn >> 12) & 0xf);
		break;

	case XFLB:
		op0 = (void *)((insn >> 17) & 0xff);
		op1 = (void *)&current->thread.fpr[(insn >> 11) & 0x1f];
		break;

	default:
		goto illegal;
	}

	eflag = func(op0, op1, op2, op3);

	if (insn & 1) {
		regs->ccr &= ~(0x0f000000);
		regs->ccr |= (__FPU_FPSCR >> 4) & 0x0f000000;
	}

	trap = record_exception(regs, eflag);
	if (trap)
		return 1;

	switch (type) {
	case DU:
	case XEU:
		regs->gpr[idx] = (unsigned long)op1;
		break;

	default:
		break;
	}
#endif /* CONFIG_MATH_EMULATION */

	regs->nip += 4;
	return 0;

illegal:
	return -ENOSYS;
}
