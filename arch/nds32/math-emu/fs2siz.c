// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2019 Andes Technology Corporation
#include <linux/uaccess.h>

#include <asm/sfp-machine.h>
#include <math-emu/soft-fp.h>
#include <math-emu/single.h>

void fs2si_z(void *ft, void *fa)
{
	int r;

	FP_DECL_S(A);
	FP_DECL_EX;

	FP_UNPACK_SP(A, fa);

	if (A_c == FP_CLS_INF) {
		*(int *)ft = (A_s == 0) ? 0x7fffffff : 0x80000000;
		__FPU_FPCSR |= FP_EX_INVALID;
	} else if (A_c == FP_CLS_NAN) {
		*(int *)ft = 0xffffffff;
		__FPU_FPCSR |= FP_EX_INVALID;
	} else {
		FP_TO_INT_S(r, A, 32, 1);
		__FPU_FPCSR |= FP_CUR_EXCEPTIONS;
		*(int *)ft = r;
	}
}
