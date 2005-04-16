/*
 *  arch/s390/math-emu/math.c
 *
 *  S390 version
 *    Copyright (C) 1999-2001 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *
 * 'math.c' emulates IEEE instructions on a S390 processor
 *          that does not have the IEEE fpu (all processors before G5).
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <asm/lowcore.h>

#include "sfp-util.h"
#include <math-emu/soft-fp.h>
#include <math-emu/single.h>
#include <math-emu/double.h>
#include <math-emu/quad.h>

/*
 * I miss a macro to round a floating point number to the
 * nearest integer in the same floating point format.
 */
#define _FP_TO_FPINT_ROUND(fs, wc, X)					\
  do {									\
    switch (X##_c)							\
      {									\
      case FP_CLS_NORMAL:						\
        if (X##_e > _FP_FRACBITS_##fs + _FP_EXPBIAS_##fs)		\
          { /* floating point number has no bits after the dot. */	\
          }								\
        else if (X##_e <= _FP_FRACBITS_##fs + _FP_EXPBIAS_##fs &&	\
                 X##_e > _FP_EXPBIAS_##fs)				\
	  { /* some bits before the dot, some after it. */		\
            _FP_FRAC_SRS_##wc(X, _FP_WFRACBITS_##fs,			\
                              X##_e - _FP_EXPBIAS_##fs			\
                              + _FP_FRACBITS_##fs);			\
	    _FP_ROUND(wc, X);						\
	    _FP_FRAC_SLL_##wc(X, X##_e - _FP_EXPBIAS_##fs		\
                              + _FP_FRACBITS_##fs);			\
          }								\
        else								\
          { /* all bits after the dot. */				\
	    FP_SET_EXCEPTION(FP_EX_INEXACT);				\
            X##_c = FP_CLS_ZERO;					\
	  }								\
        break;								\
      case FP_CLS_NAN:							\
      case FP_CLS_INF:							\
      case FP_CLS_ZERO:							\
        break;								\
      }									\
  } while (0)

#define FP_TO_FPINT_ROUND_S(X)	_FP_TO_FPINT_ROUND(S,1,X)
#define FP_TO_FPINT_ROUND_D(X)	_FP_TO_FPINT_ROUND(D,2,X)
#define FP_TO_FPINT_ROUND_Q(X)	_FP_TO_FPINT_ROUND(Q,4,X)

typedef union {
        long double ld;
        struct {
                __u64 high;
                __u64 low;
        } w;
} mathemu_ldcv;

#ifdef CONFIG_SYSCTL
int sysctl_ieee_emulation_warnings=1;
#endif

#define mathemu_put_user(x, p) \
        do { \
                if (put_user((x),(p))) \
                        return SIGSEGV; \
        } while (0)

#define mathemu_get_user(x, p) \
        do { \
                if (get_user((x),(p))) \
                        return SIGSEGV; \
        } while (0)

#define mathemu_copy_from_user(d, s, n)\
        do { \
                if (copy_from_user((d),(s),(n)) != 0) \
                        return SIGSEGV; \
        } while (0)

#define mathemu_copy_to_user(d, s, n) \
        do { \
                if (copy_to_user((d),(s),(n)) != 0) \
                        return SIGSEGV; \
        } while (0)

static void display_emulation_not_implemented(struct pt_regs *regs, char *instr)
{
        __u16 *location;
        
#ifdef CONFIG_SYSCTL
        if(sysctl_ieee_emulation_warnings)
#endif
        {
                location = (__u16 *)(regs->psw.addr-S390_lowcore.pgm_ilc);
                printk("%s ieee fpu instruction not emulated "
                       "process name: %s pid: %d \n",
                       instr, current->comm, current->pid);
                printk("%s's PSW:    %08lx %08lx\n", instr,
                       (unsigned long) regs->psw.mask,
                       (unsigned long) location);
        }
}

static inline void emu_set_CC (struct pt_regs *regs, int cc)
{
        regs->psw.mask = (regs->psw.mask & 0xFFFFCFFF) | ((cc&3) << 12);
}

/*
 * Set the condition code in the user psw.
 *  0 : Result is zero
 *  1 : Result is less than zero
 *  2 : Result is greater than zero
 *  3 : Result is NaN or INF
 */
static inline void emu_set_CC_cs(struct pt_regs *regs, int class, int sign)
{
        switch (class) {
        case FP_CLS_NORMAL:
        case FP_CLS_INF:
                emu_set_CC(regs, sign ? 1 : 2);
                break;
        case FP_CLS_ZERO:
                emu_set_CC(regs, 0);
                break;
        case FP_CLS_NAN:
                emu_set_CC(regs, 3);
                break;
        }
}

/* Add long double */
static int emu_axbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_Q(QA); FP_DECL_Q(QB); FP_DECL_Q(QR);
        FP_DECL_EX;
	mathemu_ldcv cvt;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        cvt.w.high = current->thread.fp_regs.fprs[rx].ui;
        cvt.w.low = current->thread.fp_regs.fprs[rx+2].ui;
        FP_UNPACK_QP(QA, &cvt.ld);
        cvt.w.high = current->thread.fp_regs.fprs[ry].ui;
        cvt.w.low = current->thread.fp_regs.fprs[ry+2].ui;
        FP_UNPACK_QP(QB, &cvt.ld);
        FP_ADD_Q(QR, QA, QB);
        FP_PACK_QP(&cvt.ld, QR);
        current->thread.fp_regs.fprs[rx].ui = cvt.w.high;
        current->thread.fp_regs.fprs[rx+2].ui = cvt.w.low;
        emu_set_CC_cs(regs, QR_c, QR_s);
        return _fex;
}

/* Add double */
static int emu_adbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_D(DA); FP_DECL_D(DB); FP_DECL_D(DR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[rx].d);
        FP_UNPACK_DP(DB, &current->thread.fp_regs.fprs[ry].d);
        FP_ADD_D(DR, DA, DB);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rx].d, DR);
        emu_set_CC_cs(regs, DR_c, DR_s);
        return _fex;
}

/* Add double */
static int emu_adb (struct pt_regs *regs, int rx, double *val) {
        FP_DECL_D(DA); FP_DECL_D(DB); FP_DECL_D(DR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[rx].d);
        FP_UNPACK_DP(DB, val);
        FP_ADD_D(DR, DA, DB);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rx].d, DR);
        emu_set_CC_cs(regs, DR_c, DR_s);
        return _fex;
}

/* Add float */
static int emu_aebr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_S(SA); FP_DECL_S(SB); FP_DECL_S(SR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[rx].f);
        FP_UNPACK_SP(SB, &current->thread.fp_regs.fprs[ry].f);
        FP_ADD_S(SR, SA, SB);
	FP_PACK_SP(&current->thread.fp_regs.fprs[rx].f, SR);
        emu_set_CC_cs(regs, SR_c, SR_s);
        return _fex;
}

/* Add float */
static int emu_aeb (struct pt_regs *regs, int rx, float *val) {
        FP_DECL_S(SA); FP_DECL_S(SB); FP_DECL_S(SR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[rx].f);
        FP_UNPACK_SP(SB, val);
        FP_ADD_S(SR, SA, SB);
	FP_PACK_SP(&current->thread.fp_regs.fprs[rx].f, SR);
        emu_set_CC_cs(regs, SR_c, SR_s);
        return _fex;
}

/* Compare long double */
static int emu_cxbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_Q(QA); FP_DECL_Q(QB);
	mathemu_ldcv cvt;
        int IR;

        cvt.w.high = current->thread.fp_regs.fprs[rx].ui;
        cvt.w.low = current->thread.fp_regs.fprs[rx+2].ui;
        FP_UNPACK_RAW_QP(QA, &cvt.ld);
        cvt.w.high = current->thread.fp_regs.fprs[ry].ui;
        cvt.w.low = current->thread.fp_regs.fprs[ry+2].ui;
        FP_UNPACK_RAW_QP(QB, &cvt.ld);
        FP_CMP_Q(IR, QA, QB, 3);
        /*
         * IR == -1 if DA < DB, IR == 0 if DA == DB,
         * IR == 1 if DA > DB and IR == 3 if unorderded
         */
        emu_set_CC(regs, (IR == -1) ? 1 : (IR == 1) ? 2 : IR);
        return 0;
}

/* Compare double */
static int emu_cdbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_D(DA); FP_DECL_D(DB);
        int IR;

        FP_UNPACK_RAW_DP(DA, &current->thread.fp_regs.fprs[rx].d);
        FP_UNPACK_RAW_DP(DB, &current->thread.fp_regs.fprs[ry].d);
        FP_CMP_D(IR, DA, DB, 3);
        /*
         * IR == -1 if DA < DB, IR == 0 if DA == DB,
         * IR == 1 if DA > DB and IR == 3 if unorderded
         */
        emu_set_CC(regs, (IR == -1) ? 1 : (IR == 1) ? 2 : IR);
        return 0;
}

/* Compare double */
static int emu_cdb (struct pt_regs *regs, int rx, double *val) {
        FP_DECL_D(DA); FP_DECL_D(DB);
        int IR;

        FP_UNPACK_RAW_DP(DA, &current->thread.fp_regs.fprs[rx].d);
        FP_UNPACK_RAW_DP(DB, val);
        FP_CMP_D(IR, DA, DB, 3);
        /*
         * IR == -1 if DA < DB, IR == 0 if DA == DB,
         * IR == 1 if DA > DB and IR == 3 if unorderded
         */
        emu_set_CC(regs, (IR == -1) ? 1 : (IR == 1) ? 2 : IR);
        return 0;
}

/* Compare float */
static int emu_cebr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_S(SA); FP_DECL_S(SB);
        int IR;

        FP_UNPACK_RAW_SP(SA, &current->thread.fp_regs.fprs[rx].f);
        FP_UNPACK_RAW_SP(SB, &current->thread.fp_regs.fprs[ry].f);
        FP_CMP_S(IR, SA, SB, 3);
        /*
         * IR == -1 if DA < DB, IR == 0 if DA == DB,
         * IR == 1 if DA > DB and IR == 3 if unorderded
         */
        emu_set_CC(regs, (IR == -1) ? 1 : (IR == 1) ? 2 : IR);
        return 0;
}

/* Compare float */
static int emu_ceb (struct pt_regs *regs, int rx, float *val) {
        FP_DECL_S(SA); FP_DECL_S(SB);
        int IR;

        FP_UNPACK_RAW_SP(SA, &current->thread.fp_regs.fprs[rx].f);
        FP_UNPACK_RAW_SP(SB, val);
        FP_CMP_S(IR, SA, SB, 3);
        /*
         * IR == -1 if DA < DB, IR == 0 if DA == DB,
         * IR == 1 if DA > DB and IR == 3 if unorderded
         */
        emu_set_CC(regs, (IR == -1) ? 1 : (IR == 1) ? 2 : IR);
        return 0;
}

/* Compare and signal long double */
static int emu_kxbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_Q(QA); FP_DECL_Q(QB);
        FP_DECL_EX;
	mathemu_ldcv cvt;
        int IR;

        cvt.w.high = current->thread.fp_regs.fprs[rx].ui;
        cvt.w.low = current->thread.fp_regs.fprs[rx+2].ui;
        FP_UNPACK_RAW_QP(QA, &cvt.ld);
        cvt.w.high = current->thread.fp_regs.fprs[ry].ui;
        cvt.w.low = current->thread.fp_regs.fprs[ry+2].ui;
        FP_UNPACK_QP(QB, &cvt.ld);
        FP_CMP_Q(IR, QA, QB, 3);
        /*
         * IR == -1 if DA < DB, IR == 0 if DA == DB,
         * IR == 1 if DA > DB and IR == 3 if unorderded
         */
        emu_set_CC(regs, (IR == -1) ? 1 : (IR == 1) ? 2 : IR);
        if (IR == 3)
                FP_SET_EXCEPTION (FP_EX_INVALID);
        return _fex;
}

/* Compare and signal double */
static int emu_kdbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_D(DA); FP_DECL_D(DB);
        FP_DECL_EX;
        int IR;

        FP_UNPACK_RAW_DP(DA, &current->thread.fp_regs.fprs[rx].d);
        FP_UNPACK_RAW_DP(DB, &current->thread.fp_regs.fprs[ry].d);
        FP_CMP_D(IR, DA, DB, 3);
        /*
         * IR == -1 if DA < DB, IR == 0 if DA == DB,
         * IR == 1 if DA > DB and IR == 3 if unorderded
         */
        emu_set_CC(regs, (IR == -1) ? 1 : (IR == 1) ? 2 : IR);
        if (IR == 3)
                FP_SET_EXCEPTION (FP_EX_INVALID);
        return _fex;
}

/* Compare and signal double */
static int emu_kdb (struct pt_regs *regs, int rx, double *val) {
        FP_DECL_D(DA); FP_DECL_D(DB);
        FP_DECL_EX;
        int IR;

        FP_UNPACK_RAW_DP(DA, &current->thread.fp_regs.fprs[rx].d);
        FP_UNPACK_RAW_DP(DB, val);
        FP_CMP_D(IR, DA, DB, 3);
        /*
         * IR == -1 if DA < DB, IR == 0 if DA == DB,
         * IR == 1 if DA > DB and IR == 3 if unorderded
         */
        emu_set_CC(regs, (IR == -1) ? 1 : (IR == 1) ? 2 : IR);
        if (IR == 3)
                FP_SET_EXCEPTION (FP_EX_INVALID);
        return _fex;
}

/* Compare and signal float */
static int emu_kebr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_S(SA); FP_DECL_S(SB);
        FP_DECL_EX;
        int IR;

        FP_UNPACK_RAW_SP(SA, &current->thread.fp_regs.fprs[rx].f);
        FP_UNPACK_RAW_SP(SB, &current->thread.fp_regs.fprs[ry].f);
        FP_CMP_S(IR, SA, SB, 3);
        /*
         * IR == -1 if DA < DB, IR == 0 if DA == DB,
         * IR == 1 if DA > DB and IR == 3 if unorderded
         */
        emu_set_CC(regs, (IR == -1) ? 1 : (IR == 1) ? 2 : IR);
        if (IR == 3)
                FP_SET_EXCEPTION (FP_EX_INVALID);
        return _fex;
}

/* Compare and signal float */
static int emu_keb (struct pt_regs *regs, int rx, float *val) {
        FP_DECL_S(SA); FP_DECL_S(SB);
        FP_DECL_EX;
        int IR;

        FP_UNPACK_RAW_SP(SA, &current->thread.fp_regs.fprs[rx].f);
        FP_UNPACK_RAW_SP(SB, val);
        FP_CMP_S(IR, SA, SB, 3);
        /*
         * IR == -1 if DA < DB, IR == 0 if DA == DB,
         * IR == 1 if DA > DB and IR == 3 if unorderded
         */
        emu_set_CC(regs, (IR == -1) ? 1 : (IR == 1) ? 2 : IR);
        if (IR == 3)
                FP_SET_EXCEPTION (FP_EX_INVALID);
        return _fex;
}

/* Convert from fixed long double */
static int emu_cxfbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_Q(QR);
        FP_DECL_EX;
	mathemu_ldcv cvt;
        __s32 si;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        si = regs->gprs[ry];
        FP_FROM_INT_Q(QR, si, 32, int);
        FP_PACK_QP(&cvt.ld, QR);
        current->thread.fp_regs.fprs[rx].ui = cvt.w.high;
        current->thread.fp_regs.fprs[rx+2].ui = cvt.w.low;
        return _fex;
}

/* Convert from fixed double */
static int emu_cdfbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_D(DR);
        FP_DECL_EX;
        __s32 si;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        si = regs->gprs[ry];
        FP_FROM_INT_D(DR, si, 32, int);
        FP_PACK_DP(&current->thread.fp_regs.fprs[rx].d, DR);
        return _fex;
}

/* Convert from fixed float */
static int emu_cefbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_S(SR);
        FP_DECL_EX;
        __s32 si;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        si = regs->gprs[ry];
        FP_FROM_INT_S(SR, si, 32, int);
        FP_PACK_SP(&current->thread.fp_regs.fprs[rx].f, SR);
        return _fex;
}

/* Convert to fixed long double */
static int emu_cfxbr (struct pt_regs *regs, int rx, int ry, int mask) {
        FP_DECL_Q(QA);
        FP_DECL_EX;
	mathemu_ldcv cvt;
        __s32 si;
        int mode;

	if (mask == 0)
		mode = current->thread.fp_regs.fpc & 3;
	else if (mask == 1)
		mode = FP_RND_NEAREST;
	else
		mode = mask - 4;
        cvt.w.high = current->thread.fp_regs.fprs[ry].ui;
        cvt.w.low = current->thread.fp_regs.fprs[ry+2].ui;
        FP_UNPACK_QP(QA, &cvt.ld);
        FP_TO_INT_ROUND_Q(si, QA, 32, 1);
        regs->gprs[rx] = si;
        emu_set_CC_cs(regs, QA_c, QA_s);
        return _fex;
}

/* Convert to fixed double */
static int emu_cfdbr (struct pt_regs *regs, int rx, int ry, int mask) {
        FP_DECL_D(DA);
        FP_DECL_EX;
        __s32 si;
        int mode;

	if (mask == 0)
		mode = current->thread.fp_regs.fpc & 3;
	else if (mask == 1)
		mode = FP_RND_NEAREST;
	else
		mode = mask - 4;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[ry].d);
        FP_TO_INT_ROUND_D(si, DA, 32, 1);
        regs->gprs[rx] = si;
        emu_set_CC_cs(regs, DA_c, DA_s);
        return _fex;
}

/* Convert to fixed float */
static int emu_cfebr (struct pt_regs *regs, int rx, int ry, int mask) {
        FP_DECL_S(SA);
        FP_DECL_EX;
        __s32 si;
        int mode;

	if (mask == 0)
		mode = current->thread.fp_regs.fpc & 3;
	else if (mask == 1)
		mode = FP_RND_NEAREST;
	else
		mode = mask - 4;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[ry].f);
        FP_TO_INT_ROUND_S(si, SA, 32, 1);
        regs->gprs[rx] = si;
        emu_set_CC_cs(regs, SA_c, SA_s);
        return _fex;
}

/* Divide long double */
static int emu_dxbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_Q(QA); FP_DECL_Q(QB); FP_DECL_Q(QR);
        FP_DECL_EX;
	mathemu_ldcv cvt;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        cvt.w.high = current->thread.fp_regs.fprs[rx].ui;
        cvt.w.low = current->thread.fp_regs.fprs[rx+2].ui;
        FP_UNPACK_QP(QA, &cvt.ld);
        cvt.w.high = current->thread.fp_regs.fprs[ry].ui;
        cvt.w.low = current->thread.fp_regs.fprs[ry+2].ui;
        FP_UNPACK_QP(QB, &cvt.ld);
        FP_DIV_Q(QR, QA, QB);
        FP_PACK_QP(&cvt.ld, QR);
        current->thread.fp_regs.fprs[rx].ui = cvt.w.high;
        current->thread.fp_regs.fprs[rx+2].ui = cvt.w.low;
        return _fex;
}

/* Divide double */
static int emu_ddbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_D(DA); FP_DECL_D(DB); FP_DECL_D(DR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[rx].d);
        FP_UNPACK_DP(DB, &current->thread.fp_regs.fprs[ry].d);
        FP_DIV_D(DR, DA, DB);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rx].d, DR);
        return _fex;
}

/* Divide double */
static int emu_ddb (struct pt_regs *regs, int rx, double *val) {
        FP_DECL_D(DA); FP_DECL_D(DB); FP_DECL_D(DR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[rx].d);
        FP_UNPACK_DP(DB, val);
        FP_DIV_D(DR, DA, DB);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rx].d, DR);
        return _fex;
}

/* Divide float */
static int emu_debr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_S(SA); FP_DECL_S(SB); FP_DECL_S(SR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[rx].f);
        FP_UNPACK_SP(SB, &current->thread.fp_regs.fprs[ry].f);
        FP_DIV_S(SR, SA, SB);
	FP_PACK_SP(&current->thread.fp_regs.fprs[rx].f, SR);
        return _fex;
}

/* Divide float */
static int emu_deb (struct pt_regs *regs, int rx, float *val) {
        FP_DECL_S(SA); FP_DECL_S(SB); FP_DECL_S(SR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[rx].f);
        FP_UNPACK_SP(SB, val);
        FP_DIV_S(SR, SA, SB);
	FP_PACK_SP(&current->thread.fp_regs.fprs[rx].f, SR);
        return _fex;
}

/* Divide to integer double */
static int emu_didbr (struct pt_regs *regs, int rx, int ry, int mask) {
        display_emulation_not_implemented(regs, "didbr");
        return 0;
}

/* Divide to integer float */
static int emu_diebr (struct pt_regs *regs, int rx, int ry, int mask) {
        display_emulation_not_implemented(regs, "diebr");
        return 0;
}

/* Extract fpc */
static int emu_efpc (struct pt_regs *regs, int rx, int ry) {
        regs->gprs[rx] = current->thread.fp_regs.fpc;
        return 0;
}

/* Load and test long double */
static int emu_ltxbr (struct pt_regs *regs, int rx, int ry) {
        s390_fp_regs *fp_regs = &current->thread.fp_regs;
	mathemu_ldcv cvt;
        FP_DECL_Q(QA);
        FP_DECL_EX;

        cvt.w.high = current->thread.fp_regs.fprs[ry].ui;
        cvt.w.low = current->thread.fp_regs.fprs[ry+2].ui;
        FP_UNPACK_QP(QA, &cvt.ld);
        fp_regs->fprs[rx].ui = fp_regs->fprs[ry].ui;
        fp_regs->fprs[rx+2].ui = fp_regs->fprs[ry+2].ui;
        emu_set_CC_cs(regs, QA_c, QA_s);
        return _fex;
}

/* Load and test double */
static int emu_ltdbr (struct pt_regs *regs, int rx, int ry) {
        s390_fp_regs *fp_regs = &current->thread.fp_regs;
        FP_DECL_D(DA);
        FP_DECL_EX;

        FP_UNPACK_DP(DA, &fp_regs->fprs[ry].d);
        fp_regs->fprs[rx].ui = fp_regs->fprs[ry].ui;
        emu_set_CC_cs(regs, DA_c, DA_s);
        return _fex;
}

/* Load and test double */
static int emu_ltebr (struct pt_regs *regs, int rx, int ry) {
        s390_fp_regs *fp_regs = &current->thread.fp_regs;
        FP_DECL_S(SA);
        FP_DECL_EX;

        FP_UNPACK_SP(SA, &fp_regs->fprs[ry].f);
        fp_regs->fprs[rx].ui = fp_regs->fprs[ry].ui;
        emu_set_CC_cs(regs, SA_c, SA_s);
        return _fex;
}

/* Load complement long double */
static int emu_lcxbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_Q(QA); FP_DECL_Q(QR);
        FP_DECL_EX;
	mathemu_ldcv cvt;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        cvt.w.high = current->thread.fp_regs.fprs[ry].ui;
        cvt.w.low = current->thread.fp_regs.fprs[ry+2].ui;
        FP_UNPACK_QP(QA, &cvt.ld);
	FP_NEG_Q(QR, QA);
        FP_PACK_QP(&cvt.ld, QR);
        current->thread.fp_regs.fprs[rx].ui = cvt.w.high;
        current->thread.fp_regs.fprs[rx+2].ui = cvt.w.low;
        emu_set_CC_cs(regs, QR_c, QR_s);
        return _fex;
}

/* Load complement double */
static int emu_lcdbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_D(DA); FP_DECL_D(DR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[ry].d);
	FP_NEG_D(DR, DA);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rx].d, DR);
        emu_set_CC_cs(regs, DR_c, DR_s);
        return _fex;
}

/* Load complement float */
static int emu_lcebr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_S(SA); FP_DECL_S(SR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[ry].f);
	FP_NEG_S(SR, SA);
	FP_PACK_SP(&current->thread.fp_regs.fprs[rx].f, SR);
        emu_set_CC_cs(regs, SR_c, SR_s);
        return _fex;
}

/* Load floating point integer long double */
static int emu_fixbr (struct pt_regs *regs, int rx, int ry, int mask) {
        s390_fp_regs *fp_regs = &current->thread.fp_regs;
        FP_DECL_Q(QA);
        FP_DECL_EX;
	mathemu_ldcv cvt;
        __s32 si;
        int mode;

	if (mask == 0)
		mode = fp_regs->fpc & 3;
	else if (mask == 1)
		mode = FP_RND_NEAREST;
	else
		mode = mask - 4;
        cvt.w.high = fp_regs->fprs[ry].ui;
        cvt.w.low = fp_regs->fprs[ry+2].ui;
        FP_UNPACK_QP(QA, &cvt.ld);
	FP_TO_FPINT_ROUND_Q(QA);
	FP_PACK_QP(&cvt.ld, QA);
	fp_regs->fprs[rx].ui = cvt.w.high;
	fp_regs->fprs[rx+2].ui = cvt.w.low;
        return _fex;
}

/* Load floating point integer double */
static int emu_fidbr (struct pt_regs *regs, int rx, int ry, int mask) {
	/* FIXME: rounding mode !! */
        s390_fp_regs *fp_regs = &current->thread.fp_regs;
        FP_DECL_D(DA);
        FP_DECL_EX;
        __s32 si;
        int mode;

	if (mask == 0)
		mode = fp_regs->fpc & 3;
	else if (mask == 1)
		mode = FP_RND_NEAREST;
	else
		mode = mask - 4;
        FP_UNPACK_DP(DA, &fp_regs->fprs[ry].d);
	FP_TO_FPINT_ROUND_D(DA);
	FP_PACK_DP(&fp_regs->fprs[rx].d, DA);
        return _fex;
}

/* Load floating point integer float */
static int emu_fiebr (struct pt_regs *regs, int rx, int ry, int mask) {
        s390_fp_regs *fp_regs = &current->thread.fp_regs;
        FP_DECL_S(SA);
        FP_DECL_EX;
        __s32 si;
        int mode;

	if (mask == 0)
		mode = fp_regs->fpc & 3;
	else if (mask == 1)
		mode = FP_RND_NEAREST;
	else
		mode = mask - 4;
        FP_UNPACK_SP(SA, &fp_regs->fprs[ry].f);
	FP_TO_FPINT_ROUND_S(SA);
	FP_PACK_SP(&fp_regs->fprs[rx].f, SA);
        return _fex;
}

/* Load lengthened double to long double */
static int emu_lxdbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_D(DA); FP_DECL_Q(QR);
	FP_DECL_EX;
	mathemu_ldcv cvt;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[ry].d);
	FP_CONV (Q, D, 4, 2, QR, DA);
        FP_PACK_QP(&cvt.ld, QR);
        current->thread.fp_regs.fprs[rx].ui = cvt.w.high;
        current->thread.fp_regs.fprs[rx+2].ui = cvt.w.low;
        return _fex;
}

/* Load lengthened double to long double */
static int emu_lxdb (struct pt_regs *regs, int rx, double *val) {
        FP_DECL_D(DA); FP_DECL_Q(QR);
	FP_DECL_EX;
	mathemu_ldcv cvt;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, val);
	FP_CONV (Q, D, 4, 2, QR, DA);
        FP_PACK_QP(&cvt.ld, QR);
        current->thread.fp_regs.fprs[rx].ui = cvt.w.high;
        current->thread.fp_regs.fprs[rx+2].ui = cvt.w.low;
        return _fex;
}

/* Load lengthened float to long double */
static int emu_lxebr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_S(SA); FP_DECL_Q(QR);
	FP_DECL_EX;
	mathemu_ldcv cvt;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[ry].f);
	FP_CONV (Q, S, 4, 1, QR, SA);
        FP_PACK_QP(&cvt.ld, QR);
        current->thread.fp_regs.fprs[rx].ui = cvt.w.high;
        current->thread.fp_regs.fprs[rx+2].ui = cvt.w.low;
        return _fex;
}

/* Load lengthened float to long double */
static int emu_lxeb (struct pt_regs *regs, int rx, float *val) {
        FP_DECL_S(SA); FP_DECL_Q(QR);
	FP_DECL_EX;
	mathemu_ldcv cvt;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, val);
	FP_CONV (Q, S, 4, 1, QR, SA);
        FP_PACK_QP(&cvt.ld, QR);
        current->thread.fp_regs.fprs[rx].ui = cvt.w.high;
        current->thread.fp_regs.fprs[rx+2].ui = cvt.w.low;
        return _fex;
}

/* Load lengthened float to double */
static int emu_ldebr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_S(SA); FP_DECL_D(DR);
	FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[ry].f);
	FP_CONV (D, S, 2, 1, DR, SA);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rx].d, DR);
        return _fex;
}

/* Load lengthened float to double */
static int emu_ldeb (struct pt_regs *regs, int rx, float *val) {
        FP_DECL_S(SA); FP_DECL_D(DR);
	FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, val);
	FP_CONV (D, S, 2, 1, DR, SA);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rx].d, DR);
        return _fex;
}

/* Load negative long double */
static int emu_lnxbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_Q(QA); FP_DECL_Q(QR);
	FP_DECL_EX;
	mathemu_ldcv cvt;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        cvt.w.high = current->thread.fp_regs.fprs[ry].ui;
        cvt.w.low = current->thread.fp_regs.fprs[ry+2].ui;
        FP_UNPACK_QP(QA, &cvt.ld);
        if (QA_s == 0) {
		FP_NEG_Q(QR, QA);
		FP_PACK_QP(&cvt.ld, QR);
		current->thread.fp_regs.fprs[rx].ui = cvt.w.high;
		current->thread.fp_regs.fprs[rx+2].ui = cvt.w.low;
	} else {
		current->thread.fp_regs.fprs[rx].ui =
			current->thread.fp_regs.fprs[ry].ui;
		current->thread.fp_regs.fprs[rx+2].ui =
			current->thread.fp_regs.fprs[ry+2].ui;
	}
	emu_set_CC_cs(regs, QR_c, QR_s);
        return _fex;
}

/* Load negative double */
static int emu_lndbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_D(DA); FP_DECL_D(DR);
	FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[ry].d);
        if (DA_s == 0) {
		FP_NEG_D(DR, DA);
		FP_PACK_DP(&current->thread.fp_regs.fprs[rx].d, DR);
	} else
		current->thread.fp_regs.fprs[rx].ui =
			current->thread.fp_regs.fprs[ry].ui;
	emu_set_CC_cs(regs, DR_c, DR_s);
        return _fex;
}

/* Load negative float */
static int emu_lnebr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_S(SA); FP_DECL_S(SR);
	FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[ry].f);
        if (SA_s == 0) {
		FP_NEG_S(SR, SA);
		FP_PACK_SP(&current->thread.fp_regs.fprs[rx].f, SR);
	} else
		current->thread.fp_regs.fprs[rx].ui =
			current->thread.fp_regs.fprs[ry].ui;
	emu_set_CC_cs(regs, SR_c, SR_s);
        return _fex;
}

/* Load positive long double */
static int emu_lpxbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_Q(QA); FP_DECL_Q(QR);
	FP_DECL_EX;
	mathemu_ldcv cvt;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        cvt.w.high = current->thread.fp_regs.fprs[ry].ui;
        cvt.w.low = current->thread.fp_regs.fprs[ry+2].ui;
        FP_UNPACK_QP(QA, &cvt.ld);
        if (QA_s != 0) {
		FP_NEG_Q(QR, QA);
		FP_PACK_QP(&cvt.ld, QR);
		current->thread.fp_regs.fprs[rx].ui = cvt.w.high;
		current->thread.fp_regs.fprs[rx+2].ui = cvt.w.low;
	} else{
		current->thread.fp_regs.fprs[rx].ui =
			current->thread.fp_regs.fprs[ry].ui;
		current->thread.fp_regs.fprs[rx+2].ui =
			current->thread.fp_regs.fprs[ry+2].ui;
	}
	emu_set_CC_cs(regs, QR_c, QR_s);
        return _fex;
}

/* Load positive double */
static int emu_lpdbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_D(DA); FP_DECL_D(DR);
	FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[ry].d);
        if (DA_s != 0) {
		FP_NEG_D(DR, DA);
		FP_PACK_DP(&current->thread.fp_regs.fprs[rx].d, DR);
	} else
		current->thread.fp_regs.fprs[rx].ui =
			current->thread.fp_regs.fprs[ry].ui;
	emu_set_CC_cs(regs, DR_c, DR_s);
        return _fex;
}

/* Load positive float */
static int emu_lpebr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_S(SA); FP_DECL_S(SR);
	FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[ry].f);
        if (SA_s != 0) {
		FP_NEG_S(SR, SA);
		FP_PACK_SP(&current->thread.fp_regs.fprs[rx].f, SR);
	} else
		current->thread.fp_regs.fprs[rx].ui =
			current->thread.fp_regs.fprs[ry].ui;
	emu_set_CC_cs(regs, SR_c, SR_s);
        return _fex;
}

/* Load rounded long double to double */
static int emu_ldxbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_Q(QA); FP_DECL_D(DR);
	FP_DECL_EX;
	mathemu_ldcv cvt;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        cvt.w.high = current->thread.fp_regs.fprs[ry].ui;
        cvt.w.low = current->thread.fp_regs.fprs[ry+2].ui;
        FP_UNPACK_QP(QA, &cvt.ld);
	FP_CONV (D, Q, 2, 4, DR, QA);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rx].f, DR);
        return _fex;
}

/* Load rounded long double to float */
static int emu_lexbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_Q(QA); FP_DECL_S(SR);
	FP_DECL_EX;
	mathemu_ldcv cvt;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        cvt.w.high = current->thread.fp_regs.fprs[ry].ui;
        cvt.w.low = current->thread.fp_regs.fprs[ry+2].ui;
        FP_UNPACK_QP(QA, &cvt.ld);
	FP_CONV (S, Q, 1, 4, SR, QA);
	FP_PACK_SP(&current->thread.fp_regs.fprs[rx].f, SR);
        return _fex;
}

/* Load rounded double to float */
static int emu_ledbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_D(DA); FP_DECL_S(SR);
	FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[ry].d);
	FP_CONV (S, D, 1, 2, SR, DA);
	FP_PACK_SP(&current->thread.fp_regs.fprs[rx].f, SR);
        return _fex;
}

/* Multiply long double */
static int emu_mxbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_Q(QA); FP_DECL_Q(QB); FP_DECL_Q(QR);
        FP_DECL_EX;
	mathemu_ldcv cvt;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        cvt.w.high = current->thread.fp_regs.fprs[rx].ui;
        cvt.w.low = current->thread.fp_regs.fprs[rx+2].ui;
        FP_UNPACK_QP(QA, &cvt.ld);
        cvt.w.high = current->thread.fp_regs.fprs[ry].ui;
        cvt.w.low = current->thread.fp_regs.fprs[ry+2].ui;
        FP_UNPACK_QP(QB, &cvt.ld);
        FP_MUL_Q(QR, QA, QB);
        FP_PACK_QP(&cvt.ld, QR);
        current->thread.fp_regs.fprs[rx].ui = cvt.w.high;
        current->thread.fp_regs.fprs[rx+2].ui = cvt.w.low;
        return _fex;
}

/* Multiply double */
static int emu_mdbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_D(DA); FP_DECL_D(DB); FP_DECL_D(DR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[rx].d);
        FP_UNPACK_DP(DB, &current->thread.fp_regs.fprs[ry].d);
        FP_MUL_D(DR, DA, DB);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rx].d, DR);
        return _fex;
}

/* Multiply double */
static int emu_mdb (struct pt_regs *regs, int rx, double *val) {
        FP_DECL_D(DA); FP_DECL_D(DB); FP_DECL_D(DR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[rx].d);
        FP_UNPACK_DP(DB, val);
        FP_MUL_D(DR, DA, DB);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rx].d, DR);
        return _fex;
}

/* Multiply double to long double */
static int emu_mxdbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_D(DA); FP_DECL_Q(QA); FP_DECL_Q(QB); FP_DECL_Q(QR);
	FP_DECL_EX;
	mathemu_ldcv cvt;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[rx].d);
	FP_CONV (Q, D, 4, 2, QA, DA);
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[ry].d);
	FP_CONV (Q, D, 4, 2, QB, DA);
        FP_MUL_Q(QR, QA, QB);
        FP_PACK_QP(&cvt.ld, QR);
        current->thread.fp_regs.fprs[rx].ui = cvt.w.high;
        current->thread.fp_regs.fprs[rx+2].ui = cvt.w.low;
        return _fex;
}

/* Multiply double to long double */
static int emu_mxdb (struct pt_regs *regs, int rx, long double *val) {
        FP_DECL_Q(QA); FP_DECL_Q(QB); FP_DECL_Q(QR);
        FP_DECL_EX;
	mathemu_ldcv cvt;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        cvt.w.high = current->thread.fp_regs.fprs[rx].ui;
        cvt.w.low = current->thread.fp_regs.fprs[rx+2].ui;
        FP_UNPACK_QP(QA, &cvt.ld);
        FP_UNPACK_QP(QB, val);
        FP_MUL_Q(QR, QA, QB);
        FP_PACK_QP(&cvt.ld, QR);
        current->thread.fp_regs.fprs[rx].ui = cvt.w.high;
        current->thread.fp_regs.fprs[rx+2].ui = cvt.w.low;
        return _fex;
}

/* Multiply float */
static int emu_meebr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_S(SA); FP_DECL_S(SB); FP_DECL_S(SR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[rx].f);
        FP_UNPACK_SP(SB, &current->thread.fp_regs.fprs[ry].f);
        FP_MUL_S(SR, SA, SB);
	FP_PACK_SP(&current->thread.fp_regs.fprs[rx].f, SR);
        return _fex;
}

/* Multiply float */
static int emu_meeb (struct pt_regs *regs, int rx, float *val) {
        FP_DECL_S(SA); FP_DECL_S(SB); FP_DECL_S(SR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[rx].f);
        FP_UNPACK_SP(SB, val);
        FP_MUL_S(SR, SA, SB);
	FP_PACK_SP(&current->thread.fp_regs.fprs[rx].f, SR);
        return _fex;
}

/* Multiply float to double */
static int emu_mdebr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_S(SA); FP_DECL_D(DA); FP_DECL_D(DB); FP_DECL_D(DR);
	FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[rx].f);
	FP_CONV (D, S, 2, 1, DA, SA);
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[ry].f);
	FP_CONV (D, S, 2, 1, DB, SA);
        FP_MUL_D(DR, DA, DB);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rx].d, DR);
        return _fex;
}

/* Multiply float to double */
static int emu_mdeb (struct pt_regs *regs, int rx, float *val) {
        FP_DECL_S(SA); FP_DECL_D(DA); FP_DECL_D(DB); FP_DECL_D(DR);
	FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[rx].f);
	FP_CONV (D, S, 2, 1, DA, SA);
        FP_UNPACK_SP(SA, val);
	FP_CONV (D, S, 2, 1, DB, SA);
        FP_MUL_D(DR, DA, DB);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rx].d, DR);
        return _fex;
}

/* Multiply and add double */
static int emu_madbr (struct pt_regs *regs, int rx, int ry, int rz) {
        FP_DECL_D(DA); FP_DECL_D(DB); FP_DECL_D(DC); FP_DECL_D(DR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[rx].d);
        FP_UNPACK_DP(DB, &current->thread.fp_regs.fprs[ry].d);
        FP_UNPACK_DP(DC, &current->thread.fp_regs.fprs[rz].d);
        FP_MUL_D(DR, DA, DB);
        FP_ADD_D(DR, DR, DC);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rz].d, DR);
        return _fex;
}

/* Multiply and add double */
static int emu_madb (struct pt_regs *regs, int rx, double *val, int rz) {
        FP_DECL_D(DA); FP_DECL_D(DB); FP_DECL_D(DC); FP_DECL_D(DR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[rx].d);
        FP_UNPACK_DP(DB, val);
        FP_UNPACK_DP(DC, &current->thread.fp_regs.fprs[rz].d);
        FP_MUL_D(DR, DA, DB);
        FP_ADD_D(DR, DR, DC);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rz].d, DR);
        return _fex;
}

/* Multiply and add float */
static int emu_maebr (struct pt_regs *regs, int rx, int ry, int rz) {
        FP_DECL_S(SA); FP_DECL_S(SB); FP_DECL_S(SC); FP_DECL_S(SR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[rx].f);
        FP_UNPACK_SP(SB, &current->thread.fp_regs.fprs[ry].f);
        FP_UNPACK_SP(SC, &current->thread.fp_regs.fprs[rz].f);
        FP_MUL_S(SR, SA, SB);
        FP_ADD_S(SR, SR, SC);
	FP_PACK_SP(&current->thread.fp_regs.fprs[rz].f, SR);
        return _fex;
}

/* Multiply and add float */
static int emu_maeb (struct pt_regs *regs, int rx, float *val, int rz) {
        FP_DECL_S(SA); FP_DECL_S(SB); FP_DECL_S(SC); FP_DECL_S(SR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[rx].f);
        FP_UNPACK_SP(SB, val);
        FP_UNPACK_SP(SC, &current->thread.fp_regs.fprs[rz].f);
        FP_MUL_S(SR, SA, SB);
        FP_ADD_S(SR, SR, SC);
	FP_PACK_SP(&current->thread.fp_regs.fprs[rz].f, SR);
        return _fex;
}

/* Multiply and subtract double */
static int emu_msdbr (struct pt_regs *regs, int rx, int ry, int rz) {
        FP_DECL_D(DA); FP_DECL_D(DB); FP_DECL_D(DC); FP_DECL_D(DR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[rx].d);
        FP_UNPACK_DP(DB, &current->thread.fp_regs.fprs[ry].d);
        FP_UNPACK_DP(DC, &current->thread.fp_regs.fprs[rz].d);
        FP_MUL_D(DR, DA, DB);
        FP_SUB_D(DR, DR, DC);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rz].d, DR);
        return _fex;
}

/* Multiply and subtract double */
static int emu_msdb (struct pt_regs *regs, int rx, double *val, int rz) {
        FP_DECL_D(DA); FP_DECL_D(DB); FP_DECL_D(DC); FP_DECL_D(DR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[rx].d);
        FP_UNPACK_DP(DB, val);
        FP_UNPACK_DP(DC, &current->thread.fp_regs.fprs[rz].d);
        FP_MUL_D(DR, DA, DB);
        FP_SUB_D(DR, DR, DC);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rz].d, DR);
        return _fex;
}

/* Multiply and subtract float */
static int emu_msebr (struct pt_regs *regs, int rx, int ry, int rz) {
        FP_DECL_S(SA); FP_DECL_S(SB); FP_DECL_S(SC); FP_DECL_S(SR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[rx].f);
        FP_UNPACK_SP(SB, &current->thread.fp_regs.fprs[ry].f);
        FP_UNPACK_SP(SC, &current->thread.fp_regs.fprs[rz].f);
        FP_MUL_S(SR, SA, SB);
        FP_SUB_S(SR, SR, SC);
	FP_PACK_SP(&current->thread.fp_regs.fprs[rz].f, SR);
        return _fex;
}

/* Multiply and subtract float */
static int emu_mseb (struct pt_regs *regs, int rx, float *val, int rz) {
        FP_DECL_S(SA); FP_DECL_S(SB); FP_DECL_S(SC); FP_DECL_S(SR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[rx].f);
        FP_UNPACK_SP(SB, val);
        FP_UNPACK_SP(SC, &current->thread.fp_regs.fprs[rz].f);
        FP_MUL_S(SR, SA, SB);
        FP_SUB_S(SR, SR, SC);
	FP_PACK_SP(&current->thread.fp_regs.fprs[rz].f, SR);
        return _fex;
}

/* Set floating point control word */
static int emu_sfpc (struct pt_regs *regs, int rx, int ry) {
        __u32 temp;

        temp = regs->gprs[rx];
        if ((temp & ~FPC_VALID_MASK) != 0)
		return SIGILL;
	current->thread.fp_regs.fpc = temp;
        return 0;
}

/* Square root long double */
static int emu_sqxbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_Q(QA); FP_DECL_Q(QR);
        FP_DECL_EX;
	mathemu_ldcv cvt;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        cvt.w.high = current->thread.fp_regs.fprs[ry].ui;
        cvt.w.low = current->thread.fp_regs.fprs[ry+2].ui;
        FP_UNPACK_QP(QA, &cvt.ld);
	FP_SQRT_Q(QR, QA);
        FP_PACK_QP(&cvt.ld, QR);
        current->thread.fp_regs.fprs[rx].ui = cvt.w.high;
        current->thread.fp_regs.fprs[rx+2].ui = cvt.w.low;
        emu_set_CC_cs(regs, QR_c, QR_s);
        return _fex;
}

/* Square root double */
static int emu_sqdbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_D(DA); FP_DECL_D(DR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[ry].d);
	FP_SQRT_D(DR, DA);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rx].d, DR);
        emu_set_CC_cs(regs, DR_c, DR_s);
        return _fex;
}

/* Square root double */
static int emu_sqdb (struct pt_regs *regs, int rx, double *val) {
        FP_DECL_D(DA); FP_DECL_D(DR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, val);
	FP_SQRT_D(DR, DA);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rx].d, DR);
        emu_set_CC_cs(regs, DR_c, DR_s);
        return _fex;
}

/* Square root float */
static int emu_sqebr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_S(SA); FP_DECL_S(SR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[ry].f);
	FP_SQRT_S(SR, SA);
	FP_PACK_SP(&current->thread.fp_regs.fprs[rx].f, SR);
        emu_set_CC_cs(regs, SR_c, SR_s);
        return _fex;
}

/* Square root float */
static int emu_sqeb (struct pt_regs *regs, int rx, float *val) {
        FP_DECL_S(SA); FP_DECL_S(SR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, val);
	FP_SQRT_S(SR, SA);
	FP_PACK_SP(&current->thread.fp_regs.fprs[rx].f, SR);
        emu_set_CC_cs(regs, SR_c, SR_s);
        return _fex;
}

/* Subtract long double */
static int emu_sxbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_Q(QA); FP_DECL_Q(QB); FP_DECL_Q(QR);
        FP_DECL_EX;
	mathemu_ldcv cvt;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        cvt.w.high = current->thread.fp_regs.fprs[rx].ui;
        cvt.w.low = current->thread.fp_regs.fprs[rx+2].ui;
        FP_UNPACK_QP(QA, &cvt.ld);
        cvt.w.high = current->thread.fp_regs.fprs[ry].ui;
        cvt.w.low = current->thread.fp_regs.fprs[ry+2].ui;
        FP_UNPACK_QP(QB, &cvt.ld);
        FP_SUB_Q(QR, QA, QB);
        FP_PACK_QP(&cvt.ld, QR);
        current->thread.fp_regs.fprs[rx].ui = cvt.w.high;
        current->thread.fp_regs.fprs[rx+2].ui = cvt.w.low;
        emu_set_CC_cs(regs, QR_c, QR_s);
        return _fex;
}

/* Subtract double */
static int emu_sdbr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_D(DA); FP_DECL_D(DB); FP_DECL_D(DR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[rx].d);
        FP_UNPACK_DP(DB, &current->thread.fp_regs.fprs[ry].d);
        FP_SUB_D(DR, DA, DB);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rx].d, DR);
        emu_set_CC_cs(regs, DR_c, DR_s);
        return _fex;
}

/* Subtract double */
static int emu_sdb (struct pt_regs *regs, int rx, double *val) {
        FP_DECL_D(DA); FP_DECL_D(DB); FP_DECL_D(DR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_DP(DA, &current->thread.fp_regs.fprs[rx].d);
        FP_UNPACK_DP(DB, val);
        FP_SUB_D(DR, DA, DB);
	FP_PACK_DP(&current->thread.fp_regs.fprs[rx].d, DR);
        emu_set_CC_cs(regs, DR_c, DR_s);
        return _fex;
}

/* Subtract float */
static int emu_sebr (struct pt_regs *regs, int rx, int ry) {
        FP_DECL_S(SA); FP_DECL_S(SB); FP_DECL_S(SR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[rx].f);
        FP_UNPACK_SP(SB, &current->thread.fp_regs.fprs[ry].f);
        FP_SUB_S(SR, SA, SB);
	FP_PACK_SP(&current->thread.fp_regs.fprs[rx].f, SR);
        emu_set_CC_cs(regs, SR_c, SR_s);
        return _fex;
}

/* Subtract float */
static int emu_seb (struct pt_regs *regs, int rx, float *val) {
        FP_DECL_S(SA); FP_DECL_S(SB); FP_DECL_S(SR);
        FP_DECL_EX;
        int mode;

	mode = current->thread.fp_regs.fpc & 3;
        FP_UNPACK_SP(SA, &current->thread.fp_regs.fprs[rx].f);
        FP_UNPACK_SP(SB, val);
        FP_SUB_S(SR, SA, SB);
	FP_PACK_SP(&current->thread.fp_regs.fprs[rx].f, SR);
        emu_set_CC_cs(regs, SR_c, SR_s);
        return _fex;
}

/* Test data class long double */
static int emu_tcxb (struct pt_regs *regs, int rx, long val) {
        FP_DECL_Q(QA);
	mathemu_ldcv cvt;
	int bit;

        cvt.w.high = current->thread.fp_regs.fprs[rx].ui;
        cvt.w.low = current->thread.fp_regs.fprs[rx+2].ui;
        FP_UNPACK_RAW_QP(QA, &cvt.ld);
	switch (QA_e) {
	default:
		bit = 8;		/* normalized number */
		break;
	case 0:
		if (_FP_FRAC_ZEROP_4(QA))
			bit = 10;	/* zero */
		else
			bit = 6;	/* denormalized number */
		break;
	case _FP_EXPMAX_Q:
		if (_FP_FRAC_ZEROP_4(QA))
			bit = 4;	/* infinity */
		else if (_FP_FRAC_HIGH_RAW_Q(QA) & _FP_QNANBIT_Q)
			bit = 2;	/* quiet NAN */
		else
			bit = 0;	/* signaling NAN */
		break;
	}
	if (!QA_s)
		bit++;
	emu_set_CC(regs, ((__u32) val >> bit) & 1);
        return 0;
}

/* Test data class double */
static int emu_tcdb (struct pt_regs *regs, int rx, long val) {
        FP_DECL_D(DA);
	int bit;

        FP_UNPACK_RAW_DP(DA, &current->thread.fp_regs.fprs[rx].d);
	switch (DA_e) {
	default:
		bit = 8;		/* normalized number */
		break;
	case 0:
		if (_FP_FRAC_ZEROP_2(DA))
			bit = 10;	/* zero */
		else
			bit = 6;	/* denormalized number */
		break;
	case _FP_EXPMAX_D:
		if (_FP_FRAC_ZEROP_2(DA))
			bit = 4;	/* infinity */
		else if (_FP_FRAC_HIGH_RAW_D(DA) & _FP_QNANBIT_D)
			bit = 2;	/* quiet NAN */
		else
			bit = 0;	/* signaling NAN */
		break;
	}
	if (!DA_s)
		bit++;
	emu_set_CC(regs, ((__u32) val >> bit) & 1);
        return 0;
}

/* Test data class float */
static int emu_tceb (struct pt_regs *regs, int rx, long val) {
        FP_DECL_S(SA);
	int bit;

        FP_UNPACK_RAW_SP(SA, &current->thread.fp_regs.fprs[rx].f);
	switch (SA_e) {
	default:
		bit = 8;		/* normalized number */
		break;
	case 0:
		if (_FP_FRAC_ZEROP_1(SA))
			bit = 10;	/* zero */
		else
			bit = 6;	/* denormalized number */
		break;
	case _FP_EXPMAX_S:
		if (_FP_FRAC_ZEROP_1(SA))
			bit = 4;	/* infinity */
		else if (_FP_FRAC_HIGH_RAW_S(SA) & _FP_QNANBIT_S)
			bit = 2;	/* quiet NAN */
		else
			bit = 0;	/* signaling NAN */
		break;
	}
	if (!SA_s)
		bit++;
	emu_set_CC(regs, ((__u32) val >> bit) & 1);
        return 0;
}

static inline void emu_load_regd(int reg) {
        if ((reg&9) != 0)         /* test if reg in {0,2,4,6} */
                return;
        asm volatile (            /* load reg from fp_regs.fprs[reg] */
                "     bras  1,0f\n"
                "     ld    0,0(%1)\n"
                "0:   ex    %0,0(1)"
                : /* no output */
                : "a" (reg<<4),"a" (&current->thread.fp_regs.fprs[reg].d)
                : "1" );
}

static inline void emu_load_rege(int reg) {
        if ((reg&9) != 0)         /* test if reg in {0,2,4,6} */
                return;
        asm volatile (            /* load reg from fp_regs.fprs[reg] */
                "     bras  1,0f\n"
                "     le    0,0(%1)\n"
                "0:   ex    %0,0(1)"
                : /* no output */
                : "a" (reg<<4), "a" (&current->thread.fp_regs.fprs[reg].f)
                : "1" );
}

static inline void emu_store_regd(int reg) {
        if ((reg&9) != 0)         /* test if reg in {0,2,4,6} */
                return;
        asm volatile (            /* store reg to fp_regs.fprs[reg] */
                "     bras  1,0f\n"
                "     std   0,0(%1)\n"
                "0:   ex    %0,0(1)"
                : /* no output */
                : "a" (reg<<4), "a" (&current->thread.fp_regs.fprs[reg].d)
                : "1" );
}


static inline void emu_store_rege(int reg) {
        if ((reg&9) != 0)         /* test if reg in {0,2,4,6} */
                return;
        asm volatile (            /* store reg to fp_regs.fprs[reg] */
                "     bras  1,0f\n"
                "     ste   0,0(%1)\n"
                "0:   ex    %0,0(1)"
                : /* no output */
                : "a" (reg<<4), "a" (&current->thread.fp_regs.fprs[reg].f)
                : "1" );
}

int math_emu_b3(__u8 *opcode, struct pt_regs * regs) {
        int _fex = 0;
        static const __u8 format_table[256] = {
                [0x00] = 0x03,[0x01] = 0x03,[0x02] = 0x03,[0x03] = 0x03,
		[0x04] = 0x0f,[0x05] = 0x0d,[0x06] = 0x0e,[0x07] = 0x0d,
		[0x08] = 0x03,[0x09] = 0x03,[0x0a] = 0x03,[0x0b] = 0x03,
                [0x0c] = 0x0f,[0x0d] = 0x03,[0x0e] = 0x06,[0x0f] = 0x06,
		[0x10] = 0x02,[0x11] = 0x02,[0x12] = 0x02,[0x13] = 0x02,
		[0x14] = 0x03,[0x15] = 0x02,[0x16] = 0x01,[0x17] = 0x03,
                [0x18] = 0x02,[0x19] = 0x02,[0x1a] = 0x02,[0x1b] = 0x02,
		[0x1c] = 0x02,[0x1d] = 0x02,[0x1e] = 0x05,[0x1f] = 0x05,
		[0x40] = 0x01,[0x41] = 0x01,[0x42] = 0x01,[0x43] = 0x01,
                [0x44] = 0x12,[0x45] = 0x0d,[0x46] = 0x11,[0x47] = 0x04,
		[0x48] = 0x01,[0x49] = 0x01,[0x4a] = 0x01,[0x4b] = 0x01,
		[0x4c] = 0x01,[0x4d] = 0x01,[0x53] = 0x06,[0x57] = 0x06,
                [0x5b] = 0x05,[0x5f] = 0x05,[0x84] = 0x13,[0x8c] = 0x13,
		[0x94] = 0x09,[0x95] = 0x08,[0x96] = 0x07,[0x98] = 0x0c,
		[0x99] = 0x0b,[0x9a] = 0x0a
        };
        static const void *jump_table[256]= {
                [0x00] = emu_lpebr,[0x01] = emu_lnebr,[0x02] = emu_ltebr,
                [0x03] = emu_lcebr,[0x04] = emu_ldebr,[0x05] = emu_lxdbr,
                [0x06] = emu_lxebr,[0x07] = emu_mxdbr,[0x08] = emu_kebr,
                [0x09] = emu_cebr, [0x0a] = emu_aebr, [0x0b] = emu_sebr,
                [0x0c] = emu_mdebr,[0x0d] = emu_debr, [0x0e] = emu_maebr,
                [0x0f] = emu_msebr,[0x10] = emu_lpdbr,[0x11] = emu_lndbr, 
                [0x12] = emu_ltdbr,[0x13] = emu_lcdbr,[0x14] = emu_sqebr,
                [0x15] = emu_sqdbr,[0x16] = emu_sqxbr,[0x17] = emu_meebr,
                [0x18] = emu_kdbr, [0x19] = emu_cdbr, [0x1a] = emu_adbr,
                [0x1b] = emu_sdbr, [0x1c] = emu_mdbr, [0x1d] = emu_ddbr,  
                [0x1e] = emu_madbr,[0x1f] = emu_msdbr,[0x40] = emu_lpxbr,
                [0x41] = emu_lnxbr,[0x42] = emu_ltxbr,[0x43] = emu_lcxbr,
                [0x44] = emu_ledbr,[0x45] = emu_ldxbr,[0x46] = emu_lexbr,
                [0x47] = emu_fixbr,[0x48] = emu_kxbr, [0x49] = emu_cxbr,  
                [0x4a] = emu_axbr, [0x4b] = emu_sxbr, [0x4c] = emu_mxbr,
                [0x4d] = emu_dxbr, [0x53] = emu_diebr,[0x57] = emu_fiebr,
                [0x5b] = emu_didbr,[0x5f] = emu_fidbr,[0x84] = emu_sfpc,
                [0x8c] = emu_efpc, [0x94] = emu_cefbr,[0x95] = emu_cdfbr, 
                [0x96] = emu_cxfbr,[0x98] = emu_cfebr,[0x99] = emu_cfdbr,
                [0x9a] = emu_cfxbr
        };

        switch (format_table[opcode[1]]) {
        case 1: /* RRE format, long double operation */
                if (opcode[3] & 0x22)
			return SIGILL;
                emu_store_regd((opcode[3] >> 4) & 15);
                emu_store_regd(((opcode[3] >> 4) & 15) + 2);
                emu_store_regd(opcode[3] & 15);
                emu_store_regd((opcode[3] & 15) + 2);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *,int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15);
                emu_load_regd((opcode[3] >> 4) & 15);
                emu_load_regd(((opcode[3] >> 4) & 15) + 2);
                emu_load_regd(opcode[3] & 15);
                emu_load_regd((opcode[3] & 15) + 2);
		break;
        case 2: /* RRE format, double operation */
                emu_store_regd((opcode[3] >> 4) & 15);
                emu_store_regd(opcode[3] & 15);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15);
                emu_load_regd((opcode[3] >> 4) & 15);
                emu_load_regd(opcode[3] & 15);
		break;
        case 3: /* RRE format, float operation */
                emu_store_rege((opcode[3] >> 4) & 15);
                emu_store_rege(opcode[3] & 15);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15);
                emu_load_rege((opcode[3] >> 4) & 15);
                emu_load_rege(opcode[3] & 15);
		break;
        case 4: /* RRF format, long double operation */
                if (opcode[3] & 0x22)
			return SIGILL;
                emu_store_regd((opcode[3] >> 4) & 15);
                emu_store_regd(((opcode[3] >> 4) & 15) + 2);
                emu_store_regd(opcode[3] & 15);
                emu_store_regd((opcode[3] & 15) + 2);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15, opcode[2] >> 4);
                emu_load_regd((opcode[3] >> 4) & 15);
                emu_load_regd(((opcode[3] >> 4) & 15) + 2);
                emu_load_regd(opcode[3] & 15);
                emu_load_regd((opcode[3] & 15) + 2);
		break;
        case 5: /* RRF format, double operation */
                emu_store_regd((opcode[2] >> 4) & 15);
                emu_store_regd((opcode[3] >> 4) & 15);
                emu_store_regd(opcode[3] & 15);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15, opcode[2] >> 4);
                emu_load_regd((opcode[2] >> 4) & 15);
                emu_load_regd((opcode[3] >> 4) & 15);
                emu_load_regd(opcode[3] & 15);
		break;
        case 6: /* RRF format, float operation */
                emu_store_rege((opcode[2] >> 4) & 15);
                emu_store_rege((opcode[3] >> 4) & 15);
                emu_store_rege(opcode[3] & 15);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15, opcode[2] >> 4);
                emu_load_rege((opcode[2] >> 4) & 15);
                emu_load_rege((opcode[3] >> 4) & 15);
                emu_load_rege(opcode[3] & 15);
		break;
        case 7: /* RRE format, cxfbr instruction */
                /* call the emulation function */
                if (opcode[3] & 0x20)
			return SIGILL;
                _fex = ((int (*)(struct pt_regs *, int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15);
                emu_load_regd((opcode[3] >> 4) & 15);
                emu_load_regd(((opcode[3] >> 4) & 15) + 2);
		break;
        case 8: /* RRE format, cdfbr instruction */
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15);
                emu_load_regd((opcode[3] >> 4) & 15);
		break;
        case 9: /* RRE format, cefbr instruction */
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15);
                emu_load_rege((opcode[3] >> 4) & 15);
		break;
        case 10: /* RRF format, cfxbr instruction */
                if ((opcode[2] & 128) == 128 || (opcode[2] & 96) == 32)
			/* mask of { 2,3,8-15 } is invalid */
			return SIGILL;
                if (opcode[3] & 2)
			return SIGILL;
                emu_store_regd(opcode[3] & 15);
                emu_store_regd((opcode[3] & 15) + 2);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15, opcode[2] >> 4);
		break;
        case 11: /* RRF format, cfdbr instruction */
                if ((opcode[2] & 128) == 128 || (opcode[2] & 96) == 32)
			/* mask of { 2,3,8-15 } is invalid */
			return SIGILL;
                emu_store_regd(opcode[3] & 15);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15, opcode[2] >> 4);
		break;
        case 12: /* RRF format, cfebr instruction */
                if ((opcode[2] & 128) == 128 || (opcode[2] & 96) == 32)
			/* mask of { 2,3,8-15 } is invalid */
			return SIGILL;
                emu_store_rege(opcode[3] & 15);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15, opcode[2] >> 4);
		break;
        case 13: /* RRE format, ldxbr & mdxbr instruction */
                /* double store but long double load */
                if (opcode[3] & 0x20)
			return SIGILL;
                emu_store_regd((opcode[3] >> 4) & 15);
                emu_store_regd(opcode[3]  & 15);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15);
                emu_load_regd((opcode[3] >> 4) & 15);
                emu_load_regd(((opcode[3] >> 4) & 15) + 2);
		break;
        case 14: /* RRE format, ldxbr & mdxbr instruction */
                /* float store but long double load */
                if (opcode[3] & 0x20)
			return SIGILL;
                emu_store_rege((opcode[3] >> 4) & 15);
                emu_store_rege(opcode[3]  & 15);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15);
                emu_load_regd((opcode[3] >> 4) & 15);
                emu_load_regd(((opcode[3] >> 4) & 15) + 2);
		break;
        case 15: /* RRE format, ldebr & mdebr instruction */
                /* float store but double load */
                emu_store_rege((opcode[3] >> 4) & 15);
                emu_store_rege(opcode[3]  & 15);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15);
                emu_load_regd((opcode[3] >> 4) & 15);
		break;
        case 16: /* RRE format, ldxbr instruction */
                /* long double store but double load */
                if (opcode[3] & 2)
			return SIGILL;
                emu_store_regd(opcode[3] & 15);
                emu_store_regd((opcode[3] & 15) + 2);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15);
                emu_load_regd((opcode[3] >> 4) & 15);
                break;
        case 17: /* RRE format, ldxbr instruction */
                /* long double store but float load */
                if (opcode[3] & 2)
			return SIGILL;
                emu_store_regd(opcode[3] & 15);
                emu_store_regd((opcode[3] & 15) + 2);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15);
                emu_load_rege((opcode[3] >> 4) & 15);
                break;
        case 18: /* RRE format, ledbr instruction */
                /* double store but float load */
                emu_store_regd(opcode[3] & 15);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15);
                emu_load_rege((opcode[3] >> 4) & 15);
                break;
        case 19: /* RRE format, efpc & sfpc instruction */
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, int))
			jump_table[opcode[1]])
                        (regs, opcode[3] >> 4, opcode[3] & 15);
                break;
        default: /* invalid operation */
                return SIGILL;
        }
	if (_fex != 0) {
		current->thread.fp_regs.fpc |= _fex;
		if (current->thread.fp_regs.fpc & (_fex << 8))
			return SIGFPE;
	}
	return 0;
}

static void* calc_addr(struct pt_regs *regs, int rx, int rb, int disp)
{
        addr_t addr;

        rx &= 15;
        rb &= 15;
        addr = disp & 0xfff;
        addr += (rx != 0) ? regs->gprs[rx] : 0;  /* + index */
        addr += (rb != 0) ? regs->gprs[rb] : 0;  /* + base  */
        return (void*) addr;
}
    
int math_emu_ed(__u8 *opcode, struct pt_regs * regs) {
        int _fex = 0;

        static const __u8 format_table[256] = {
                [0x04] = 0x06,[0x05] = 0x05,[0x06] = 0x07,[0x07] = 0x05,
		[0x08] = 0x02,[0x09] = 0x02,[0x0a] = 0x02,[0x0b] = 0x02,
		[0x0c] = 0x06,[0x0d] = 0x02,[0x0e] = 0x04,[0x0f] = 0x04,
                [0x10] = 0x08,[0x11] = 0x09,[0x12] = 0x0a,[0x14] = 0x02,
		[0x15] = 0x01,[0x17] = 0x02,[0x18] = 0x01,[0x19] = 0x01,
		[0x1a] = 0x01,[0x1b] = 0x01,[0x1c] = 0x01,[0x1d] = 0x01,
                [0x1e] = 0x03,[0x1f] = 0x03,
        };
        static const void *jump_table[]= {
                [0x04] = emu_ldeb,[0x05] = emu_lxdb,[0x06] = emu_lxeb,
                [0x07] = emu_mxdb,[0x08] = emu_keb, [0x09] = emu_ceb,
                [0x0a] = emu_aeb, [0x0b] = emu_seb, [0x0c] = emu_mdeb,
                [0x0d] = emu_deb, [0x0e] = emu_maeb,[0x0f] = emu_mseb,
                [0x10] = emu_tceb,[0x11] = emu_tcdb,[0x12] = emu_tcxb,
                [0x14] = emu_sqeb,[0x15] = emu_sqdb,[0x17] = emu_meeb,
                [0x18] = emu_kdb, [0x19] = emu_cdb, [0x1a] = emu_adb,
                [0x1b] = emu_sdb, [0x1c] = emu_mdb, [0x1d] = emu_ddb,
                [0x1e] = emu_madb,[0x1f] = emu_msdb
        };

        switch (format_table[opcode[5]]) {
        case 1: /* RXE format, double constant */ {
                __u64 *dxb, temp;
                __u32 opc;

                emu_store_regd((opcode[1] >> 4) & 15);
                opc = *((__u32 *) opcode);
                dxb = (__u64 *) calc_addr(regs, opc >> 16, opc >> 12, opc);
                mathemu_copy_from_user(&temp, dxb, 8);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, double *))
			jump_table[opcode[5]])
                        (regs, opcode[1] >> 4, (double *) &temp);
                emu_load_regd((opcode[1] >> 4) & 15);
                break;
        }
        case 2: /* RXE format, float constant */ {
                __u32 *dxb, temp;
                __u32 opc;

                emu_store_rege((opcode[1] >> 4) & 15);
                opc = *((__u32 *) opcode);
                dxb = (__u32 *) calc_addr(regs, opc >> 16, opc >> 12, opc);
                mathemu_get_user(temp, dxb);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, float *))
			jump_table[opcode[5]])
                        (regs, opcode[1] >> 4, (float *) &temp);
                emu_load_rege((opcode[1] >> 4) & 15);
                break;
        }
        case 3: /* RXF format, double constant */ {
                __u64 *dxb, temp;
                __u32 opc;

                emu_store_regd((opcode[1] >> 4) & 15);
                emu_store_regd((opcode[4] >> 4) & 15);
                opc = *((__u32 *) opcode);
                dxb = (__u64 *) calc_addr(regs, opc >> 16, opc >> 12, opc);
                mathemu_copy_from_user(&temp, dxb, 8);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, double *, int))
			jump_table[opcode[5]])
                        (regs, opcode[1] >> 4, (double *) &temp, opcode[4] >> 4);
                emu_load_regd((opcode[1] >> 4) & 15);
                break;
        }
        case 4: /* RXF format, float constant */ {
                __u32 *dxb, temp;
                __u32 opc;

                emu_store_rege((opcode[1] >> 4) & 15);
                emu_store_rege((opcode[4] >> 4) & 15);
                opc = *((__u32 *) opcode);
                dxb = (__u32 *) calc_addr(regs, opc >> 16, opc >> 12, opc);
                mathemu_get_user(temp, dxb);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, float *, int))
			jump_table[opcode[5]])
                        (regs, opcode[1] >> 4, (float *) &temp, opcode[4] >> 4);
                emu_load_rege((opcode[4] >> 4) & 15);
                break;
        }
        case 5: /* RXE format, double constant */
                /* store double and load long double */ 
        {
                __u64 *dxb, temp;
                __u32 opc;
                if ((opcode[1] >> 4) & 0x20)
			return SIGILL;
                emu_store_regd((opcode[1] >> 4) & 15);
                opc = *((__u32 *) opcode);
                dxb = (__u64 *) calc_addr(regs, opc >> 16, opc >> 12, opc);
                mathemu_copy_from_user(&temp, dxb, 8);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, double *))
			jump_table[opcode[5]])
                        (regs, opcode[1] >> 4, (double *) &temp);
                emu_load_regd((opcode[1] >> 4) & 15);
                emu_load_regd(((opcode[1] >> 4) & 15) + 2);
                break;
        }
        case 6: /* RXE format, float constant */
                /* store float and load double */ 
        {
                __u32 *dxb, temp;
                __u32 opc;
                emu_store_rege((opcode[1] >> 4) & 15);
                opc = *((__u32 *) opcode);
                dxb = (__u32 *) calc_addr(regs, opc >> 16, opc >> 12, opc);
                mathemu_get_user(temp, dxb);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, float *))
			jump_table[opcode[5]])
                        (regs, opcode[1] >> 4, (float *) &temp);
                emu_load_regd((opcode[1] >> 4) & 15);
                break;
        }
        case 7: /* RXE format, float constant */
                /* store float and load long double */ 
        {
                __u32 *dxb, temp;
                __u32 opc;
                if ((opcode[1] >> 4) & 0x20)
			return SIGILL;
                emu_store_rege((opcode[1] >> 4) & 15);
                opc = *((__u32 *) opcode);
                dxb = (__u32 *) calc_addr(regs, opc >> 16, opc >> 12, opc);
                mathemu_get_user(temp, dxb);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, float *))
			jump_table[opcode[5]])
                        (regs, opcode[1] >> 4, (float *) &temp);
                emu_load_regd((opcode[1] >> 4) & 15);
                emu_load_regd(((opcode[1] >> 4) & 15) + 2);
                break;
        }
        case 8: /* RXE format, RX address used as int value */ {
                __u64 dxb;
                __u32 opc;

                emu_store_rege((opcode[1] >> 4) & 15);
                opc = *((__u32 *) opcode);
                dxb = (__u64) calc_addr(regs, opc >> 16, opc >> 12, opc);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, long))
			jump_table[opcode[5]])
                        (regs, opcode[1] >> 4, dxb);
                break;
        }
        case 9: /* RXE format, RX address used as int value */ {
                __u64 dxb;
                __u32 opc;

                emu_store_regd((opcode[1] >> 4) & 15);
                opc = *((__u32 *) opcode);
                dxb = (__u64) calc_addr(regs, opc >> 16, opc >> 12, opc);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, long))
			jump_table[opcode[5]])
                        (regs, opcode[1] >> 4, dxb);
                break;
        }
        case 10: /* RXE format, RX address used as int value */ {
                __u64 dxb;
                __u32 opc;

                if ((opcode[1] >> 4) & 2)
			return SIGILL;
                emu_store_regd((opcode[1] >> 4) & 15);
                emu_store_regd(((opcode[1] >> 4) & 15) + 2);
                opc = *((__u32 *) opcode);
                dxb = (__u64) calc_addr(regs, opc >> 16, opc >> 12, opc);
                /* call the emulation function */
                _fex = ((int (*)(struct pt_regs *, int, long))
			jump_table[opcode[5]])
                        (regs, opcode[1] >> 4, dxb);
                break;
        }
        default: /* invalid operation */
                return SIGILL;
        }
	if (_fex != 0) {
		current->thread.fp_regs.fpc |= _fex;
		if (current->thread.fp_regs.fpc & (_fex << 8))
			return SIGFPE;
	}
	return 0;
}

/*
 * Emulate LDR Rx,Ry with Rx or Ry not in {0, 2, 4, 6}
 */
int math_emu_ldr(__u8 *opcode) {
        s390_fp_regs *fp_regs = &current->thread.fp_regs;
        __u16 opc = *((__u16 *) opcode);

        if ((opc & 0x90) == 0) {           /* test if rx in {0,2,4,6} */
                /* we got an exception therfore ry can't be in {0,2,4,6} */
                __asm__ __volatile (       /* load rx from fp_regs.fprs[ry] */
                        "     bras  1,0f\n"
                        "     ld    0,0(%1)\n"
                        "0:   ex    %0,0(1)"
                        : /* no output */
                        : "a" (opc & 0xf0),
                          "a" (&fp_regs->fprs[opc & 0xf].d)
                        : "1" );
        } else if ((opc & 0x9) == 0) {     /* test if ry in {0,2,4,6} */
                __asm__ __volatile (       /* store ry to fp_regs.fprs[rx] */
                        "     bras  1,0f\n"
                        "     std   0,0(%1)\n"
                        "0:   ex    %0,0(1)"
                        : /* no output */
                        : "a" ((opc & 0xf) << 4),
                          "a" (&fp_regs->fprs[(opc & 0xf0)>>4].d)
                        : "1" );
        } else  /* move fp_regs.fprs[ry] to fp_regs.fprs[rx] */
                fp_regs->fprs[(opc & 0xf0) >> 4] = fp_regs->fprs[opc & 0xf];
	return 0;
}

/*
 * Emulate LER Rx,Ry with Rx or Ry not in {0, 2, 4, 6}
 */
int math_emu_ler(__u8 *opcode) {
        s390_fp_regs *fp_regs = &current->thread.fp_regs;
        __u16 opc = *((__u16 *) opcode);

        if ((opc & 0x90) == 0) {           /* test if rx in {0,2,4,6} */
                /* we got an exception therfore ry can't be in {0,2,4,6} */
                __asm__ __volatile (       /* load rx from fp_regs.fprs[ry] */
                        "     bras  1,0f\n"
                        "     le    0,0(%1)\n"
                        "0:   ex    %0,0(1)"
                        : /* no output */
                        : "a" (opc & 0xf0),
                          "a" (&fp_regs->fprs[opc & 0xf].f)
                        : "1" );
        } else if ((opc & 0x9) == 0) {     /* test if ry in {0,2,4,6} */
                __asm__ __volatile (       /* store ry to fp_regs.fprs[rx] */
                        "     bras  1,0f\n"
                        "     ste   0,0(%1)\n"
                        "0:   ex    %0,0(1)"
                        : /* no output */
                        : "a" ((opc & 0xf) << 4),
                          "a" (&fp_regs->fprs[(opc & 0xf0) >> 4].f)
                        : "1" );
        } else  /* move fp_regs.fprs[ry] to fp_regs.fprs[rx] */
                fp_regs->fprs[(opc & 0xf0) >> 4] = fp_regs->fprs[opc & 0xf];
	return 0;
}

/*
 * Emulate LD R,D(X,B) with R not in {0, 2, 4, 6}
 */
int math_emu_ld(__u8 *opcode, struct pt_regs * regs) {
        s390_fp_regs *fp_regs = &current->thread.fp_regs;
        __u32 opc = *((__u32 *) opcode);
        __u64 *dxb;

        dxb = (__u64 *) calc_addr(regs, opc >> 16, opc >> 12, opc);
        mathemu_copy_from_user(&fp_regs->fprs[(opc >> 20) & 0xf].d, dxb, 8);
	return 0;
}

/*
 * Emulate LE R,D(X,B) with R not in {0, 2, 4, 6}
 */
int math_emu_le(__u8 *opcode, struct pt_regs * regs) {
        s390_fp_regs *fp_regs = &current->thread.fp_regs;
        __u32 opc = *((__u32 *) opcode);
        __u32 *mem, *dxb;

        dxb = (__u32 *) calc_addr(regs, opc >> 16, opc >> 12, opc);
        mem = (__u32 *) (&fp_regs->fprs[(opc >> 20) & 0xf].f);
        mathemu_get_user(mem[0], dxb);
	return 0;
}

/*
 * Emulate STD R,D(X,B) with R not in {0, 2, 4, 6}
 */
int math_emu_std(__u8 *opcode, struct pt_regs * regs) {
        s390_fp_regs *fp_regs = &current->thread.fp_regs;
        __u32 opc = *((__u32 *) opcode);
        __u64 *dxb;

        dxb = (__u64 *) calc_addr(regs, opc >> 16, opc >> 12, opc);
        mathemu_copy_to_user(dxb, &fp_regs->fprs[(opc >> 20) & 0xf].d, 8);
	return 0;
}

/*
 * Emulate STE R,D(X,B) with R not in {0, 2, 4, 6}
 */
int math_emu_ste(__u8 *opcode, struct pt_regs * regs) {
        s390_fp_regs *fp_regs = &current->thread.fp_regs;
        __u32 opc = *((__u32 *) opcode);
        __u32 *mem, *dxb;

        dxb = (__u32 *) calc_addr(regs, opc >> 16, opc >> 12, opc);
        mem = (__u32 *) (&fp_regs->fprs[(opc >> 20) & 0xf].f);
        mathemu_put_user(mem[0], dxb);
	return 0;
}

/*
 * Emulate LFPC D(B)
 */
int math_emu_lfpc(__u8 *opcode, struct pt_regs *regs) {
        __u32 opc = *((__u32 *) opcode);
        __u32 *dxb, temp;

        dxb= (__u32 *) calc_addr(regs, 0, opc>>12, opc);
        mathemu_get_user(temp, dxb);
        if ((temp & ~FPC_VALID_MASK) != 0)
		return SIGILL;
	current->thread.fp_regs.fpc = temp;
        return 0;
}

/*
 * Emulate STFPC D(B)
 */
int math_emu_stfpc(__u8 *opcode, struct pt_regs *regs) {
        __u32 opc = *((__u32 *) opcode);
        __u32 *dxb;

        dxb= (__u32 *) calc_addr(regs, 0, opc>>12, opc);
        mathemu_put_user(current->thread.fp_regs.fpc, dxb);
        return 0;
}

/*
 * Emulate SRNM D(B)
 */
int math_emu_srnm(__u8 *opcode, struct pt_regs *regs) {
        __u32 opc = *((__u32 *) opcode);
        __u32 temp;

        temp = calc_addr(regs, 0, opc>>12, opc);
	current->thread.fp_regs.fpc &= ~3;
        current->thread.fp_regs.fpc |= (temp & 3);
        return 0;
}

/* broken compiler ... */
long long
__negdi2 (long long u)
{

  union lll {
    long long ll;
    long s[2];
  };

  union lll w,uu;

  uu.ll = u;

  w.s[1] = -uu.s[1];
  w.s[0] = -uu.s[0] - ((int) w.s[1] != 0);

  return w.ll;
}
