// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2018 Andes Technology Corporation

#include <linux/uaccess.h>
#include <asm/sfp-machine.h>
#include <math-emu/double.h>
#include <math-emu/single.h>
#include <math-emu/soft-fp.h>

void fs2d(void *ft, void *fa)
{
	FP_DECL_S(A);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, fa);

	FP_CONV(D, S, 2, 1, R, A);

	FP_PACK_DP(ft, R);

	__FPU_FPCSR |= FP_CUR_EXCEPTIONS;
}
