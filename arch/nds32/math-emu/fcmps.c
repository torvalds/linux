// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2018 Andes Technology Corporation
#include <asm/sfp-machine.h>
#include <math-emu/soft-fp.h>
#include <math-emu/single.h>
int fcmps(void *ft, void *fa, void *fb, int cmpop)
{
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;
	long cmp;

	FP_UNPACK_SP(A, fa);
	FP_UNPACK_SP(B, fb);

	FP_CMP_S(cmp, A, B, SF_CUN);
	cmp += 2;
	if (cmp == SF_CGT)
		*(int *)ft = 0x0;
	else
		*(int *)ft = (cmp & cmpop) ? 0x1 : 0x0;

	return 0;
}
