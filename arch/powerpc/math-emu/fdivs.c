#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

#include <asm/sfp-machine.h>
#include <math-emu/soft-fp.h>
#include <math-emu/double.h>
#include <math-emu/single.h>

int
fdivs(void *frD, void *frA, void *frB)
{
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(R);
	FP_DECL_EX;

#ifdef DEBUG
	printk("%s: %p %p %p\n", __func__, frD, frA, frB);
#endif

	FP_UNPACK_DP(A, frA);
	FP_UNPACK_DP(B, frB);

#ifdef DEBUG
	printk("A: %ld %lu %lu %ld (%ld)\n", A_s, A_f1, A_f0, A_e, A_c);
	printk("B: %ld %lu %lu %ld (%ld)\n", B_s, B_f1, B_f0, B_e, B_c);
#endif

	if (A_c == FP_CLS_ZERO && B_c == FP_CLS_ZERO) {
		FP_SET_EXCEPTION(EFLAG_VXZDZ);
#ifdef DEBUG
		printk("%s: FPSCR_VXZDZ raised\n", __func__);
#endif
	}
	if (A_c == FP_CLS_INF && B_c == FP_CLS_INF) {
		FP_SET_EXCEPTION(EFLAG_VXIDI);
#ifdef DEBUG
		printk("%s: FPSCR_VXIDI raised\n", __func__);
#endif
	}

	if (B_c == FP_CLS_ZERO && A_c != FP_CLS_ZERO) {
		FP_SET_EXCEPTION(EFLAG_DIVZERO);
		if (__FPU_TRAP_P(EFLAG_DIVZERO))
			return FP_CUR_EXCEPTIONS;
	}

	FP_DIV_D(R, A, B);

#ifdef DEBUG
	printk("D: %ld %lu %lu %ld (%ld)\n", R_s, R_f1, R_f0, R_e, R_c);
#endif

	__FP_PACK_DS(frD, R);

	return FP_CUR_EXCEPTIONS;
}
