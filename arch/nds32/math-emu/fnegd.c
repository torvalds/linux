// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2018 Andes Technology Corporation
#include <linux/uaccess.h>

#include <asm/sfp-machine.h>
#include <math-emu/soft-fp.h>
#include <math-emu/double.h>
void fnegd(void *ft, void *fa)
{
	FP_DECL_D(A);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, fa);

	FP_NEG_D(R, A);

	FP_PACK_DP(ft, R);

	__FPU_FPCSR |= FP_CUR_EXCEPTIONS;
}
