/*
 * Save/restore floating point context for signal handlers.
 *
 * Copyright (C) 1999, 2000  Kaz Kojima & Niibe Yutaka
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * FIXME! These routines can be optimized in big endian case.
 */
#include <linux/sched.h>
#include <linux/signal.h>
#include <asm/processor.h>
#include <asm/io.h>
#include <asm/fpu.h>

/* The PR (precision) bit in the FP Status Register must be clear when
 * an frchg instruction is executed, otherwise the instruction is undefined.
 * Executing frchg with PR set causes a trap on some SH4 implementations.
 */

#define FPSCR_RCHG 0x00000000


/*
 * Save FPU registers onto task structure.
 * Assume called with FPU enabled (SR.FD=0).
 */
void
save_fpu(struct task_struct *tsk, struct pt_regs *regs)
{
	unsigned long dummy;

	clear_tsk_thread_flag(tsk, TIF_USEDFPU);
	enable_fpu();
	asm volatile("sts.l	fpul, @-%0\n\t"
		     "sts.l	fpscr, @-%0\n\t"
		     "fmov.s	fr15, @-%0\n\t"
		     "fmov.s	fr14, @-%0\n\t"
		     "fmov.s	fr13, @-%0\n\t"
		     "fmov.s	fr12, @-%0\n\t"
		     "fmov.s	fr11, @-%0\n\t"
		     "fmov.s	fr10, @-%0\n\t"
		     "fmov.s	fr9, @-%0\n\t"
		     "fmov.s	fr8, @-%0\n\t"
		     "fmov.s	fr7, @-%0\n\t"
		     "fmov.s	fr6, @-%0\n\t"
		     "fmov.s	fr5, @-%0\n\t"
		     "fmov.s	fr4, @-%0\n\t"
		     "fmov.s	fr3, @-%0\n\t"
		     "fmov.s	fr2, @-%0\n\t"
		     "fmov.s	fr1, @-%0\n\t"
		     "fmov.s	fr0, @-%0\n\t"
		     "lds	%3, fpscr\n\t"
		     : "=r" (dummy)
		     : "0" ((char *)(&tsk->thread.fpu.hard.status)),
		       "r" (FPSCR_RCHG),
		       "r" (FPSCR_INIT)
		     : "memory");

	disable_fpu();
	release_fpu(regs);
}

static void
restore_fpu(struct task_struct *tsk)
{
	unsigned long dummy;

	enable_fpu();
	asm volatile("fmov.s	@%0+, fr0\n\t"
		     "fmov.s	@%0+, fr1\n\t"
		     "fmov.s	@%0+, fr2\n\t"
		     "fmov.s	@%0+, fr3\n\t"
		     "fmov.s	@%0+, fr4\n\t"
		     "fmov.s	@%0+, fr5\n\t"
		     "fmov.s	@%0+, fr6\n\t"
		     "fmov.s	@%0+, fr7\n\t"
		     "fmov.s	@%0+, fr8\n\t"
		     "fmov.s	@%0+, fr9\n\t"
		     "fmov.s	@%0+, fr10\n\t"
		     "fmov.s	@%0+, fr11\n\t"
		     "fmov.s	@%0+, fr12\n\t"
		     "fmov.s	@%0+, fr13\n\t"
		     "fmov.s	@%0+, fr14\n\t"
		     "fmov.s	@%0+, fr15\n\t"
		     "lds.l	@%0+, fpscr\n\t"
		     "lds.l	@%0+, fpul\n\t"
		     : "=r" (dummy)
		     : "0" (&tsk->thread.fpu), "r" (FPSCR_RCHG)
		     : "memory");
	disable_fpu();
}

/*
 * Load the FPU with signalling NANS.  This bit pattern we're using
 * has the property that no matter wether considered as single or as
 * double precission represents signaling NANS.
 */

static void
fpu_init(void)
{
	enable_fpu();
	asm volatile("lds	%0, fpul\n\t"
		     "fsts	fpul, fr0\n\t"
		     "fsts	fpul, fr1\n\t"
		     "fsts	fpul, fr2\n\t"
		     "fsts	fpul, fr3\n\t"
		     "fsts	fpul, fr4\n\t"
		     "fsts	fpul, fr5\n\t"
		     "fsts	fpul, fr6\n\t"
		     "fsts	fpul, fr7\n\t"
		     "fsts	fpul, fr8\n\t"
		     "fsts	fpul, fr9\n\t"
		     "fsts	fpul, fr10\n\t"
		     "fsts	fpul, fr11\n\t"
		     "fsts	fpul, fr12\n\t"
		     "fsts	fpul, fr13\n\t"
		     "fsts	fpul, fr14\n\t"
		     "fsts	fpul, fr15\n\t"
		     "lds	%2, fpscr\n\t"
		     : /* no output */
		     : "r" (0), "r" (FPSCR_RCHG), "r" (FPSCR_INIT));
	disable_fpu();
}

/*
 *	Emulate arithmetic ops on denormalized number for some FPU insns.
 */

/* denormalized float * float */
static int denormal_mulf(int hx, int hy)
{
	unsigned int ix, iy;
	unsigned long long m, n;
	int exp, w;

	ix = hx & 0x7fffffff;
	iy = hy & 0x7fffffff;
	if (iy < 0x00800000 || ix == 0)
		return ((hx ^ hy) & 0x80000000);

	exp = (iy & 0x7f800000) >> 23;
	ix &= 0x007fffff;
	iy = (iy & 0x007fffff) | 0x00800000;
	m = (unsigned long long)ix * iy;
	n = m;
	w = -1;
	while (n) { n >>= 1; w++; }

	/* FIXME: use guard bits */
	exp += w - 126 - 46;
	if (exp > 0)
		ix = ((int) (m >> (w - 23)) & 0x007fffff) | (exp << 23);
	else if (exp + 22 >= 0)
		ix = (int) (m >> (w - 22 - exp)) & 0x007fffff;
	else
		ix = 0;

	ix |= (hx ^ hy) & 0x80000000;
	return ix;
}

/* denormalized double * double */
static void mult64(unsigned long long x, unsigned long long y,
		unsigned long long *highp, unsigned long long *lowp)
{
	unsigned long long sub0, sub1, sub2, sub3;
	unsigned long long high, low;

	sub0 = (x >> 32) * (unsigned long) (y >> 32);
	sub1 = (x & 0xffffffffLL) * (unsigned long) (y >> 32);
	sub2 = (x >> 32) * (unsigned long) (y & 0xffffffffLL);
	sub3 = (x & 0xffffffffLL) * (unsigned long) (y & 0xffffffffLL);
	low = sub3;
	high = 0LL;
	sub3 += (sub1 << 32);
	if (low > sub3)
		high++;
	low = sub3;
	sub3 += (sub2 << 32);
	if (low > sub3)
		high++;
	low = sub3;
	high += (sub1 >> 32) + (sub2 >> 32);
	high += sub0;
	*lowp = low;
	*highp = high;
}

static inline long long rshift64(unsigned long long mh,
		unsigned long long ml, int n)
{
	if (n >= 64)
		return mh >> (n - 64);
	return (mh << (64 - n)) | (ml >> n);
}

static long long denormal_muld(long long hx, long long hy)
{
	unsigned long long ix, iy;
	unsigned long long mh, ml, nh, nl;
	int exp, w;

	ix = hx & 0x7fffffffffffffffLL;
	iy = hy & 0x7fffffffffffffffLL;
	if (iy < 0x0010000000000000LL || ix == 0)
		return ((hx ^ hy) & 0x8000000000000000LL);

	exp = (iy & 0x7ff0000000000000LL) >> 52;
	ix &= 0x000fffffffffffffLL;
	iy = (iy & 0x000fffffffffffffLL) | 0x0010000000000000LL;
	mult64(ix, iy, &mh, &ml);
	nh = mh;
	nl = ml;
	w = -1;
	if (nh) {
		while (nh) { nh >>= 1; w++;}
		w += 64;
	} else
		while (nl) { nl >>= 1; w++;}

	/* FIXME: use guard bits */
	exp += w - 1022 - 52 * 2;
	if (exp > 0)
		ix = (rshift64(mh, ml, w - 52) & 0x000fffffffffffffLL)
			| ((long long)exp << 52);
	else if (exp + 51 >= 0)
		ix = rshift64(mh, ml, w - 51 - exp) & 0x000fffffffffffffLL;
	else
		ix = 0;

	ix |= (hx ^ hy) & 0x8000000000000000LL;
	return ix;
}

/* ix - iy where iy: denormal and ix, iy >= 0 */
static int denormal_subf1(unsigned int ix, unsigned int iy)
{
	int frac;
	int exp;

	if (ix < 0x00800000)
		return ix - iy;

	exp = (ix & 0x7f800000) >> 23;
	if (exp - 1 > 31)
		return ix;
	iy >>= exp - 1;
	if (iy == 0)
		return ix;

	frac = (ix & 0x007fffff) | 0x00800000;
	frac -= iy;
	while (frac < 0x00800000) {
		if (--exp == 0)
			return frac;
		frac <<= 1;
	}

	return (exp << 23) | (frac & 0x007fffff);
}

/* ix + iy where iy: denormal and ix, iy >= 0 */
static int denormal_addf1(unsigned int ix, unsigned int iy)
{
	int frac;
	int exp;

	if (ix < 0x00800000)
		return ix + iy;

	exp = (ix & 0x7f800000) >> 23;
	if (exp - 1 > 31)
		return ix;
	iy >>= exp - 1;
	if (iy == 0)
	  return ix;

	frac = (ix & 0x007fffff) | 0x00800000;
	frac += iy;
	if (frac >= 0x01000000) {
		frac >>= 1;
		++exp;
	}

	return (exp << 23) | (frac & 0x007fffff);
}

static int denormal_addf(int hx, int hy)
{
	unsigned int ix, iy;
	int sign;

	if ((hx ^ hy) & 0x80000000) {
		sign = hx & 0x80000000;
		ix = hx & 0x7fffffff;
		iy = hy & 0x7fffffff;
		if (iy < 0x00800000) {
			ix = denormal_subf1(ix, iy);
			if (ix < 0) {
				ix = -ix;
				sign ^= 0x80000000;
			}
		} else {
			ix = denormal_subf1(iy, ix);
			sign ^= 0x80000000;
		}
	} else {
		sign = hx & 0x80000000;
		ix = hx & 0x7fffffff;
		iy = hy & 0x7fffffff;
		if (iy < 0x00800000)
			ix = denormal_addf1(ix, iy);
		else
			ix = denormal_addf1(iy, ix);
	}

	return sign | ix;
}

/* ix - iy where iy: denormal and ix, iy >= 0 */
static long long denormal_subd1(unsigned long long ix, unsigned long long iy)
{
	long long frac;
	int exp;

	if (ix < 0x0010000000000000LL)
		return ix - iy;

	exp = (ix & 0x7ff0000000000000LL) >> 52;
	if (exp - 1 > 63)
		return ix;
	iy >>= exp - 1;
	if (iy == 0)
		return ix;

	frac = (ix & 0x000fffffffffffffLL) | 0x0010000000000000LL;
	frac -= iy;
	while (frac < 0x0010000000000000LL) {
		if (--exp == 0)
			return frac;
		frac <<= 1;
	}

	return ((long long)exp << 52) | (frac & 0x000fffffffffffffLL);
}

/* ix + iy where iy: denormal and ix, iy >= 0 */
static long long denormal_addd1(unsigned long long ix, unsigned long long iy)
{
	long long frac;
	long long exp;

	if (ix < 0x0010000000000000LL)
		return ix + iy;

	exp = (ix & 0x7ff0000000000000LL) >> 52;
	if (exp - 1 > 63)
		return ix;
	iy >>= exp - 1;
	if (iy == 0)
	  return ix;

	frac = (ix & 0x000fffffffffffffLL) | 0x0010000000000000LL;
	frac += iy;
	if (frac >= 0x0020000000000000LL) {
		frac >>= 1;
		++exp;
	}

	return (exp << 52) | (frac & 0x000fffffffffffffLL);
}

static long long denormal_addd(long long hx, long long hy)
{
	unsigned long long ix, iy;
	long long sign;

	if ((hx ^ hy) & 0x8000000000000000LL) {
		sign = hx & 0x8000000000000000LL;
		ix = hx & 0x7fffffffffffffffLL;
		iy = hy & 0x7fffffffffffffffLL;
		if (iy < 0x0010000000000000LL) {
			ix = denormal_subd1(ix, iy);
			if (ix < 0) {
				ix = -ix;
				sign ^= 0x8000000000000000LL;
			}
		} else {
			ix = denormal_subd1(iy, ix);
			sign ^= 0x8000000000000000LL;
		}
	} else {
		sign = hx & 0x8000000000000000LL;
		ix = hx & 0x7fffffffffffffffLL;
		iy = hy & 0x7fffffffffffffffLL;
		if (iy < 0x0010000000000000LL)
			ix = denormal_addd1(ix, iy);
		else
			ix = denormal_addd1(iy, ix);
	}

	return sign | ix;
}

/**
 *	denormal_to_double - Given denormalized float number,
 *	                     store double float
 *
 *	@fpu: Pointer to sh_fpu_hard structure
 *	@n: Index to FP register
 */
static void
denormal_to_double (struct sh_fpu_hard_struct *fpu, int n)
{
	unsigned long du, dl;
	unsigned long x = fpu->fpul;
	int exp = 1023 - 126;

	if (x != 0 && (x & 0x7f800000) == 0) {
		du = (x & 0x80000000);
		while ((x & 0x00800000) == 0) {
			x <<= 1;
			exp--;
		}
		x &= 0x007fffff;
		du |= (exp << 20) | (x >> 3);
		dl = x << 29;

		fpu->fp_regs[n] = du;
		fpu->fp_regs[n+1] = dl;
	}
}

/**
 *	ieee_fpe_handler - Handle denormalized number exception
 *
 *	@regs: Pointer to register structure
 *
 *	Returns 1 when it's handled (should not cause exception).
 */
static int
ieee_fpe_handler (struct pt_regs *regs)
{
	unsigned short insn = *(unsigned short *) regs->pc;
	unsigned short finsn;
	unsigned long nextpc;
	int nib[4] = {
		(insn >> 12) & 0xf,
		(insn >> 8) & 0xf,
		(insn >> 4) & 0xf,
		insn & 0xf};

	if (nib[0] == 0xb ||
	    (nib[0] == 0x4 && nib[2] == 0x0 && nib[3] == 0xb)) /* bsr & jsr */
		regs->pr = regs->pc + 4;
	if (nib[0] == 0xa || nib[0] == 0xb) { /* bra & bsr */
		nextpc = regs->pc + 4 + ((short) ((insn & 0xfff) << 4) >> 3);
		finsn = *(unsigned short *) (regs->pc + 2);
	} else if (nib[0] == 0x8 && nib[1] == 0xd) { /* bt/s */
		if (regs->sr & 1)
			nextpc = regs->pc + 4 + ((char) (insn & 0xff) << 1);
		else
			nextpc = regs->pc + 4;
		finsn = *(unsigned short *) (regs->pc + 2);
	} else if (nib[0] == 0x8 && nib[1] == 0xf) { /* bf/s */
		if (regs->sr & 1)
			nextpc = regs->pc + 4;
		else
			nextpc = regs->pc + 4 + ((char) (insn & 0xff) << 1);
		finsn = *(unsigned short *) (regs->pc + 2);
	} else if (nib[0] == 0x4 && nib[3] == 0xb &&
		 (nib[2] == 0x0 || nib[2] == 0x2)) { /* jmp & jsr */
		nextpc = regs->regs[nib[1]];
		finsn = *(unsigned short *) (regs->pc + 2);
	} else if (nib[0] == 0x0 && nib[3] == 0x3 &&
		 (nib[2] == 0x0 || nib[2] == 0x2)) { /* braf & bsrf */
		nextpc = regs->pc + 4 + regs->regs[nib[1]];
		finsn = *(unsigned short *) (regs->pc + 2);
	} else if (insn == 0x000b) { /* rts */
		nextpc = regs->pr;
		finsn = *(unsigned short *) (regs->pc + 2);
	} else {
		nextpc = regs->pc + 2;
		finsn = insn;
	}

#define FPSCR_FPU_ERROR (1 << 17)

	if ((finsn & 0xf1ff) == 0xf0ad) { /* fcnvsd */
		struct task_struct *tsk = current;

		if ((tsk->thread.fpu.hard.fpscr & FPSCR_FPU_ERROR)) {
			/* FPU error */
			denormal_to_double (&tsk->thread.fpu.hard,
					    (finsn >> 8) & 0xf);
		} else
			return 0;

		regs->pc = nextpc;
		return 1;
	} else if ((finsn & 0xf00f) == 0xf002) { /* fmul */
		struct task_struct *tsk = current;
		int fpscr;
		int n, m, prec;
		unsigned int hx, hy;

		n = (finsn >> 8) & 0xf;
		m = (finsn >> 4) & 0xf;
		hx = tsk->thread.fpu.hard.fp_regs[n];
		hy = tsk->thread.fpu.hard.fp_regs[m];
		fpscr = tsk->thread.fpu.hard.fpscr;
		prec = fpscr & (1 << 19);

		if ((fpscr & FPSCR_FPU_ERROR)
		     && (prec && ((hx & 0x7fffffff) < 0x00100000
				   || (hy & 0x7fffffff) < 0x00100000))) {
			long long llx, lly;

			/* FPU error because of denormal */
			llx = ((long long) hx << 32)
			       | tsk->thread.fpu.hard.fp_regs[n+1];
			lly = ((long long) hy << 32)
			       | tsk->thread.fpu.hard.fp_regs[m+1];
			if ((hx & 0x7fffffff) >= 0x00100000)
				llx = denormal_muld(lly, llx);
			else
				llx = denormal_muld(llx, lly);
			tsk->thread.fpu.hard.fp_regs[n] = llx >> 32;
			tsk->thread.fpu.hard.fp_regs[n+1] = llx & 0xffffffff;
		} else if ((fpscr & FPSCR_FPU_ERROR)
		     && (!prec && ((hx & 0x7fffffff) < 0x00800000
				   || (hy & 0x7fffffff) < 0x00800000))) {
			/* FPU error because of denormal */
			if ((hx & 0x7fffffff) >= 0x00800000)
				hx = denormal_mulf(hy, hx);
			else
				hx = denormal_mulf(hx, hy);
			tsk->thread.fpu.hard.fp_regs[n] = hx;
		} else
			return 0;

		regs->pc = nextpc;
		return 1;
	} else if ((finsn & 0xf00e) == 0xf000) { /* fadd, fsub */
		struct task_struct *tsk = current;
		int fpscr;
		int n, m, prec;
		unsigned int hx, hy;

		n = (finsn >> 8) & 0xf;
		m = (finsn >> 4) & 0xf;
		hx = tsk->thread.fpu.hard.fp_regs[n];
		hy = tsk->thread.fpu.hard.fp_regs[m];
		fpscr = tsk->thread.fpu.hard.fpscr;
		prec = fpscr & (1 << 19);

		if ((fpscr & FPSCR_FPU_ERROR)
		     && (prec && ((hx & 0x7fffffff) < 0x00100000
				   || (hy & 0x7fffffff) < 0x00100000))) {
			long long llx, lly;

			/* FPU error because of denormal */
			llx = ((long long) hx << 32)
			       | tsk->thread.fpu.hard.fp_regs[n+1];
			lly = ((long long) hy << 32)
			       | tsk->thread.fpu.hard.fp_regs[m+1];
			if ((finsn & 0xf00f) == 0xf000)
				llx = denormal_addd(llx, lly);
			else
				llx = denormal_addd(llx, lly ^ (1LL << 63));
			tsk->thread.fpu.hard.fp_regs[n] = llx >> 32;
			tsk->thread.fpu.hard.fp_regs[n+1] = llx & 0xffffffff;
		} else if ((fpscr & FPSCR_FPU_ERROR)
		     && (!prec && ((hx & 0x7fffffff) < 0x00800000
				   || (hy & 0x7fffffff) < 0x00800000))) {
			/* FPU error because of denormal */
			if ((finsn & 0xf00f) == 0xf000)
				hx = denormal_addf(hx, hy);
			else
				hx = denormal_addf(hx, hy ^ 0x80000000);
			tsk->thread.fpu.hard.fp_regs[n] = hx;
		} else
			return 0;

		regs->pc = nextpc;
		return 1;
	}

	return 0;
}

BUILD_TRAP_HANDLER(fpu_error)
{
	struct task_struct *tsk = current;
	TRAP_HANDLER_DECL;

	save_fpu(tsk, regs);
	if (ieee_fpe_handler(regs)) {
		tsk->thread.fpu.hard.fpscr &=
			~(FPSCR_CAUSE_MASK | FPSCR_FLAG_MASK);
		grab_fpu(regs);
		restore_fpu(tsk);
		set_tsk_thread_flag(tsk, TIF_USEDFPU);
		return;
	}

	force_sig(SIGFPE, tsk);
}

BUILD_TRAP_HANDLER(fpu_state_restore)
{
	struct task_struct *tsk = current;
	TRAP_HANDLER_DECL;

	grab_fpu(regs);
	if (!user_mode(regs)) {
		printk(KERN_ERR "BUG: FPU is used in kernel mode.\n");
		return;
	}

	if (used_math()) {
		/* Using the FPU again.  */
		restore_fpu(tsk);
	} else	{
		/* First time FPU user.  */
		fpu_init();
		set_used_math();
	}
	set_tsk_thread_flag(tsk, TIF_USEDFPU);
}
