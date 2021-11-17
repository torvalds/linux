// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2019 Andes Technology Corporation
#include <linux/uaccess.h>

#include <asm/sfp-machine.h>
#include <math-emu/soft-fp.h>
#include <math-emu/single.h>

void fs2ui(void *ft, void *fa)
{
	unsigned int r;

	FP_DECL_S(A);
	FP_DECL_EX;

	FP_UNPACK_SP(A, fa);

	if (A_c == FP_CLS_INF) {
		*(unsigned int *)ft = (A_s == 0) ? 0xffffffff : 0x00000000;
		__FPU_FPCSR |= FP_EX_INVALID;
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)ft = 0xffffffff;
		__FPU_FPCSR |= FP_EX_INVALID;
	} else {
		FP_TO_INT_ROUND_S(r, A, 32, 0);
		__FPU_FPCSR |= FP_CUR_EXCEPTIONS;
		*(unsigned int *)ft = r;
	}
}
