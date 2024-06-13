/*
 * arch/sh/math-emu/math.c
 *
 * Copyright (C) 2006 Takashi YOSHII <takasi-y@ops.dti.ne.jp>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/sched/signal.h>
#include <linux/signal.h>
#include <linux/perf_event.h>

#include <linux/uaccess.h>
#include <asm/processor.h>
#include <asm/io.h>

#include "sfp-util.h"
#include <math-emu/soft-fp.h>
#include <math-emu/single.h>
#include <math-emu/double.h>

#define	FPUL		(fregs->fpul)
#define FPSCR		(fregs->fpscr)
#define FPSCR_RM	(FPSCR&3)
#define FPSCR_DN	((FPSCR>>18)&1)
#define FPSCR_PR	((FPSCR>>19)&1)
#define FPSCR_SZ	((FPSCR>>20)&1)
#define FPSCR_FR	((FPSCR>>21)&1)
#define FPSCR_MASK	0x003fffffUL

#define BANK(n)	(n^(FPSCR_FR?16:0))
#define FR	((unsigned long*)(fregs->fp_regs))
#define FR0	(FR[BANK(0)])
#define FRn	(FR[BANK(n)])
#define FRm	(FR[BANK(m)])
#define DR	((unsigned long long*)(fregs->fp_regs))
#define DRn	(DR[BANK(n)/2])
#define DRm	(DR[BANK(m)/2])

#define XREG(n)	(n^16)
#define XFn	(FR[BANK(XREG(n))])
#define XFm	(FR[BANK(XREG(m))])
#define XDn	(DR[BANK(XREG(n))/2])
#define XDm	(DR[BANK(XREG(m))/2])

#define R0	(regs->regs[0])
#define Rn	(regs->regs[n])
#define Rm	(regs->regs[m])

#define MWRITE(d,a)	({if(put_user(d, (typeof (d) __user *)a)) return -EFAULT;})
#define MREAD(d,a)	({if(get_user(d, (typeof (d) __user *)a)) return -EFAULT;})

#define PACK_S(r,f)	FP_PACK_SP(&r,f)
#define UNPACK_S(f,r)	FP_UNPACK_SP(f,&r)
#define PACK_D(r,f) \
	{u32 t[2]; FP_PACK_DP(t,f); ((u32*)&r)[0]=t[1]; ((u32*)&r)[1]=t[0];}
#define UNPACK_D(f,r) \
	{u32 t[2]; t[0]=((u32*)&r)[1]; t[1]=((u32*)&r)[0]; FP_UNPACK_DP(f,t);}

// 2 args instructions.
#define BOTH_PRmn(op,x) \
	FP_DECL_EX; if(FPSCR_PR) op(D,x,DRm,DRn); else op(S,x,FRm,FRn);

#define CMP_X(SZ,R,M,N) do{ \
	FP_DECL_##SZ(Fm); FP_DECL_##SZ(Fn); \
	UNPACK_##SZ(Fm, M); UNPACK_##SZ(Fn, N); \
	FP_CMP_##SZ(R, Fn, Fm, 2); }while(0)
#define EQ_X(SZ,R,M,N) do{ \
	FP_DECL_##SZ(Fm); FP_DECL_##SZ(Fn); \
	UNPACK_##SZ(Fm, M); UNPACK_##SZ(Fn, N); \
	FP_CMP_EQ_##SZ(R, Fn, Fm); }while(0)
#define CMP(OP) ({ int r; BOTH_PRmn(OP##_X,r); r; })

static int
fcmp_gt(struct sh_fpu_soft_struct *fregs, struct pt_regs *regs, int m, int n)
{
	if (CMP(CMP) > 0)
		regs->sr |= 1;
	else
		regs->sr &= ~1;

	return 0;
}

static int
fcmp_eq(struct sh_fpu_soft_struct *fregs, struct pt_regs *regs, int m, int n)
{
	if (CMP(CMP /*EQ*/) == 0)
		regs->sr |= 1;
	else
		regs->sr &= ~1;
	return 0;
}

#define ARITH_X(SZ,OP,M,N) do{ \
	FP_DECL_##SZ(Fm); FP_DECL_##SZ(Fn); FP_DECL_##SZ(Fr); \
	UNPACK_##SZ(Fm, M); UNPACK_##SZ(Fn, N); \
	FP_##OP##_##SZ(Fr, Fn, Fm); \
	PACK_##SZ(N, Fr); }while(0)

static int
fadd(struct sh_fpu_soft_struct *fregs, struct pt_regs *regs, int m, int n)
{
	BOTH_PRmn(ARITH_X, ADD);
	return 0;
}

static int
fsub(struct sh_fpu_soft_struct *fregs, struct pt_regs *regs, int m, int n)
{
	BOTH_PRmn(ARITH_X, SUB);
	return 0;
}

static int
fmul(struct sh_fpu_soft_struct *fregs, struct pt_regs *regs, int m, int n)
{
	BOTH_PRmn(ARITH_X, MUL);
	return 0;
}

static int
fdiv(struct sh_fpu_soft_struct *fregs, struct pt_regs *regs, int m, int n)
{
	BOTH_PRmn(ARITH_X, DIV);
	return 0;
}

static int
fmac(struct sh_fpu_soft_struct *fregs, struct pt_regs *regs, int m, int n)
{
	FP_DECL_EX;
	FP_DECL_S(Fr);
	FP_DECL_S(Ft);
	FP_DECL_S(F0);
	FP_DECL_S(Fm);
	FP_DECL_S(Fn);
	UNPACK_S(F0, FR0);
	UNPACK_S(Fm, FRm);
	UNPACK_S(Fn, FRn);
	FP_MUL_S(Ft, Fm, F0);
	FP_ADD_S(Fr, Fn, Ft);
	PACK_S(FRn, Fr);
	return 0;
}

// to process fmov's extension (odd n for DR access XD).
#define FMOV_EXT(x) if(x&1) x+=16-1

static int
fmov_idx_reg(struct sh_fpu_soft_struct *fregs, struct pt_regs *regs, int m,
	     int n)
{
	if (FPSCR_SZ) {
		FMOV_EXT(n);
		MREAD(FRn, Rm + R0 + 4);
		n++;
		MREAD(FRn, Rm + R0);
	} else {
		MREAD(FRn, Rm + R0);
	}

	return 0;
}

static int
fmov_mem_reg(struct sh_fpu_soft_struct *fregs, struct pt_regs *regs, int m,
	     int n)
{
	if (FPSCR_SZ) {
		FMOV_EXT(n);
		MREAD(FRn, Rm + 4);
		n++;
		MREAD(FRn, Rm);
	} else {
		MREAD(FRn, Rm);
	}

	return 0;
}

static int
fmov_inc_reg(struct sh_fpu_soft_struct *fregs, struct pt_regs *regs, int m,
	     int n)
{
	if (FPSCR_SZ) {
		FMOV_EXT(n);
		MREAD(FRn, Rm + 4);
		n++;
		MREAD(FRn, Rm);
		Rm += 8;
	} else {
		MREAD(FRn, Rm);
		Rm += 4;
	}

	return 0;
}

static int
fmov_reg_idx(struct sh_fpu_soft_struct *fregs, struct pt_regs *regs, int m,
	     int n)
{
	if (FPSCR_SZ) {
		FMOV_EXT(m);
		MWRITE(FRm, Rn + R0 + 4);
		m++;
		MWRITE(FRm, Rn + R0);
	} else {
		MWRITE(FRm, Rn + R0);
	}

	return 0;
}

static int
fmov_reg_mem(struct sh_fpu_soft_struct *fregs, struct pt_regs *regs, int m,
	     int n)
{
	if (FPSCR_SZ) {
		FMOV_EXT(m);
		MWRITE(FRm, Rn + 4);
		m++;
		MWRITE(FRm, Rn);
	} else {
		MWRITE(FRm, Rn);
	}

	return 0;
}

static int
fmov_reg_dec(struct sh_fpu_soft_struct *fregs, struct pt_regs *regs, int m,
	     int n)
{
	if (FPSCR_SZ) {
		FMOV_EXT(m);
		Rn -= 8;
		MWRITE(FRm, Rn + 4);
		m++;
		MWRITE(FRm, Rn);
	} else {
		Rn -= 4;
		MWRITE(FRm, Rn);
	}

	return 0;
}

static int
fmov_reg_reg(struct sh_fpu_soft_struct *fregs, struct pt_regs *regs, int m,
	     int n)
{
	if (FPSCR_SZ) {
		FMOV_EXT(m);
		FMOV_EXT(n);
		DRn = DRm;
	} else {
		FRn = FRm;
	}

	return 0;
}

static int
fnop_mn(struct sh_fpu_soft_struct *fregs, struct pt_regs *regs, int m, int n)
{
	return -EINVAL;
}

// 1 arg instructions.
#define NOTYETn(i) static int i(struct sh_fpu_soft_struct *fregs, int n) \
	{ printk( #i " not yet done.\n"); return 0; }

NOTYETn(ftrv)
NOTYETn(fsqrt)
NOTYETn(fipr)
NOTYETn(fsca)
NOTYETn(fsrra)

#define EMU_FLOAT_X(SZ,N) do { \
	FP_DECL_##SZ(Fn); \
	FP_FROM_INT_##SZ(Fn, FPUL, 32, int); \
	PACK_##SZ(N, Fn); }while(0)
static int ffloat(struct sh_fpu_soft_struct *fregs, int n)
{
	FP_DECL_EX;

	if (FPSCR_PR)
		EMU_FLOAT_X(D, DRn);
	else
		EMU_FLOAT_X(S, FRn);

	return 0;
}

#define EMU_FTRC_X(SZ,N) do { \
	FP_DECL_##SZ(Fn); \
	UNPACK_##SZ(Fn, N); \
	FP_TO_INT_##SZ(FPUL, Fn, 32, 1); }while(0)
static int ftrc(struct sh_fpu_soft_struct *fregs, int n)
{
	FP_DECL_EX;

	if (FPSCR_PR)
		EMU_FTRC_X(D, DRn);
	else
		EMU_FTRC_X(S, FRn);

	return 0;
}

static int fcnvsd(struct sh_fpu_soft_struct *fregs, int n)
{
	FP_DECL_EX;
	FP_DECL_S(Fn);
	FP_DECL_D(Fr);
	UNPACK_S(Fn, FPUL);
	FP_CONV(D, S, 2, 1, Fr, Fn);
	PACK_D(DRn, Fr);
	return 0;
}

static int fcnvds(struct sh_fpu_soft_struct *fregs, int n)
{
	FP_DECL_EX;
	FP_DECL_D(Fn);
	FP_DECL_S(Fr);
	UNPACK_D(Fn, DRn);
	FP_CONV(S, D, 1, 2, Fr, Fn);
	PACK_S(FPUL, Fr);
	return 0;
}

static int fxchg(struct sh_fpu_soft_struct *fregs, int flag)
{
	FPSCR ^= flag;
	return 0;
}

static int fsts(struct sh_fpu_soft_struct *fregs, int n)
{
	FRn = FPUL;
	return 0;
}

static int flds(struct sh_fpu_soft_struct *fregs, int n)
{
	FPUL = FRn;
	return 0;
}

static int fneg(struct sh_fpu_soft_struct *fregs, int n)
{
	FRn ^= (1 << (_FP_W_TYPE_SIZE - 1));
	return 0;
}

static int fabs(struct sh_fpu_soft_struct *fregs, int n)
{
	FRn &= ~(1 << (_FP_W_TYPE_SIZE - 1));
	return 0;
}

static int fld0(struct sh_fpu_soft_struct *fregs, int n)
{
	FRn = 0;
	return 0;
}

static int fld1(struct sh_fpu_soft_struct *fregs, int n)
{
	FRn = (_FP_EXPBIAS_S << (_FP_FRACBITS_S - 1));
	return 0;
}

static int fnop_n(struct sh_fpu_soft_struct *fregs, int n)
{
	return -EINVAL;
}

/// Instruction decoders.

static int id_fxfd(struct sh_fpu_soft_struct *, int);
static int id_fnxd(struct sh_fpu_soft_struct *, struct pt_regs *, int, int);

static int (*fnxd[])(struct sh_fpu_soft_struct *, int) = {
	fsts, flds, ffloat, ftrc, fneg, fabs, fsqrt, fsrra,
	fld0, fld1, fcnvsd, fcnvds, fnop_n, fnop_n, fipr, id_fxfd
};

static int (*fnmx[])(struct sh_fpu_soft_struct *, struct pt_regs *, int, int) = {
	fadd, fsub, fmul, fdiv, fcmp_eq, fcmp_gt, fmov_idx_reg, fmov_reg_idx,
	fmov_mem_reg, fmov_inc_reg, fmov_reg_mem, fmov_reg_dec,
	fmov_reg_reg, id_fnxd, fmac, fnop_mn};

static int id_fxfd(struct sh_fpu_soft_struct *fregs, int x)
{
	const int flag[] = { FPSCR_SZ, FPSCR_PR, FPSCR_FR, 0 };
	switch (x & 3) {
	case 3:
		fxchg(fregs, flag[x >> 2]);
		break;
	case 1:
		ftrv(fregs, x - 1);
		break;
	default:
		fsca(fregs, x);
	}
	return 0;
}

static int
id_fnxd(struct sh_fpu_soft_struct *fregs, struct pt_regs *regs, int x, int n)
{
	return (fnxd[x])(fregs, n);
}

static int
id_fnmx(struct sh_fpu_soft_struct *fregs, struct pt_regs *regs, u16 code)
{
	int n = (code >> 8) & 0xf, m = (code >> 4) & 0xf, x = code & 0xf;
	return (fnmx[x])(fregs, regs, m, n);
}

static int
id_sys(struct sh_fpu_soft_struct *fregs, struct pt_regs *regs, u16 code)
{
	int n = ((code >> 8) & 0xf);
	unsigned long *reg = (code & 0x0010) ? &FPUL : &FPSCR;

	switch (code & 0xf0ff) {
	case 0x005a:
	case 0x006a:
		Rn = *reg;
		break;
	case 0x405a:
	case 0x406a:
		*reg = Rn;
		break;
	case 0x4052:
	case 0x4062:
		Rn -= 4;
		MWRITE(*reg, Rn);
		break;
	case 0x4056:
	case 0x4066:
		MREAD(*reg, Rn);
		Rn += 4;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int fpu_emulate(u16 code, struct sh_fpu_soft_struct *fregs, struct pt_regs *regs)
{
	if ((code & 0xf000) == 0xf000)
		return id_fnmx(fregs, regs, code);
	else
		return id_sys(fregs, regs, code);
}

/**
 * fpu_init - Initialize FPU registers
 * @fpu: Pointer to software emulated FPU registers.
 */
static void fpu_init(struct sh_fpu_soft_struct *fpu)
{
	int i;

	fpu->fpscr = FPSCR_INIT;
	fpu->fpul = 0;

	for (i = 0; i < 16; i++) {
		fpu->fp_regs[i] = 0;
		fpu->xfp_regs[i]= 0;
	}
}

/**
 * do_fpu_inst - Handle reserved instructions for FPU emulation
 * @inst: instruction code.
 * @regs: registers on stack.
 */
int do_fpu_inst(unsigned short inst, struct pt_regs *regs)
{
	struct task_struct *tsk = current;
	struct sh_fpu_soft_struct *fpu = &(tsk->thread.xstate->softfpu);

	perf_sw_event(PERF_COUNT_SW_EMULATION_FAULTS, 1, regs, 0);

	if (!(task_thread_info(tsk)->status & TS_USEDFPU)) {
		/* initialize once. */
		fpu_init(fpu);
		task_thread_info(tsk)->status |= TS_USEDFPU;
	}

	return fpu_emulate(inst, fpu, regs);
}
