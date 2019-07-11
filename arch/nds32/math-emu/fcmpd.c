// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2018 Andes Technology Corporation
#include <asm/sfp-machine.h>
#include <math-emu/soft-fp.h>
#include <math-emu/double.h>
int fcmpd(void *ft, void *fa, void *fb, int cmpop)
{
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_EX;
	long cmp;

	FP_UNPACK_DP(A, fa);
	FP_UNPACK_DP(B, fb);

	FP_CMP_D(cmp, A, B, SF_CUN);
	cmp += 2;
	if (cmp == SF_CGT)
		*(long *)ft = 0;
	else
		*(long *)ft = (cmp & cmpop) ? 1 : 0;

	return 0;
}
