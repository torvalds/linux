// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/uaccess.h>

#include <asm/sfp-machine.h>
#include <math-emu/soft-fp.h>
#include <math-emu/double.h>
#include <math-emu/single.h>

int
frsp(void *frD, void *frB)
{
	FP_DECL_D(B);
	FP_DECL_EX;

#ifdef DEBUG
	printk("%s: D %p, B %p\n", __func__, frD, frB);
#endif

	FP_UNPACK_DP(B, frB);

#ifdef DEBUG
	printk("B: %ld %lu %lu %ld (%ld)\n", B_s, B_f1, B_f0, B_e, B_c);
#endif

	__FP_PACK_DS(frD, B);

	return FP_CUR_EXCEPTIONS;
}
