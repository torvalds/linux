#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

#include "soft-fp.h"
#include "double.h"

int
fmul(void *frD, void *frA, void *frB)
{
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(R);
	int ret = 0;

#ifdef DEBUG
	printk("%s: %p %p %p\n", __func__, frD, frA, frB);
#endif

	__FP_UNPACK_D(A, frA);
	__FP_UNPACK_D(B, frB);

#ifdef DEBUG
	printk("A: %ld %lu %lu %ld (%ld) [%08lx.%08lx %lx]\n",
	       A_s, A_f1, A_f0, A_e, A_c, A_f1, A_f0, A_e + 1023);
	printk("B: %ld %lu %lu %ld (%ld) [%08lx.%08lx %lx]\n",
	       B_s, B_f1, B_f0, B_e, B_c, B_f1, B_f0, B_e + 1023);
#endif

	if ((A_c == FP_CLS_INF && B_c == FP_CLS_ZERO) ||
	    (A_c == FP_CLS_ZERO && B_c == FP_CLS_INF))
		ret |= EFLAG_VXIMZ;

	FP_MUL_D(R, A, B);

#ifdef DEBUG
	printk("D: %ld %lu %lu %ld (%ld) [%08lx.%08lx %lx]\n",
	       R_s, R_f1, R_f0, R_e, R_c, R_f1, R_f0, R_e + 1023);
#endif

	return (ret | __FP_PACK_D(frD, R));
}
