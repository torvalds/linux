// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2019 Andes Technology Corporation
#include <linux/uaccess.h>

#include <asm/sfp-machine.h>
#include <math-emu/soft-fp.h>
#include <math-emu/single.h>

void fui2s(void *ft, void *fa)
{
	unsigned int a = *(unsigned int *)fa;

	FP_DECL_S(R);
	FP_DECL_EX;

	FP_FROM_INT_S(R, a, 32, int);

	FP_PACK_SP(ft, R);

	__FPU_FPCSR |= FP_CUR_EXCEPTIONS;

}
