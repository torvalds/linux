// SPDX-License-Identifier: GPL-2.0-only
/* IEEE754 floating point arithmetic
 * double precision: common utilities
 */
/*
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.
 */

#include "ieee754dp.h"

union ieee754dp ieee754dp_neg(union ieee754dp x)
{
	union ieee754dp y;

	if (ieee754_csr.abs2008) {
		y = x;
		DPSIGN(y) = !DPSIGN(x);
	} else {
		unsigned int oldrm;

		oldrm = ieee754_csr.rm;
		ieee754_csr.rm = FPU_CSR_RD;
		y = ieee754dp_sub(ieee754dp_zero(0), x);
		ieee754_csr.rm = oldrm;
	}
	return y;
}

union ieee754dp ieee754dp_abs(union ieee754dp x)
{
	union ieee754dp y;

	if (ieee754_csr.abs2008) {
		y = x;
		DPSIGN(y) = 0;
	} else {
		unsigned int oldrm;

		oldrm = ieee754_csr.rm;
		ieee754_csr.rm = FPU_CSR_RD;
		if (DPSIGN(x))
			y = ieee754dp_sub(ieee754dp_zero(0), x);
		else
			y = ieee754dp_add(ieee754dp_zero(0), x);
		ieee754_csr.rm = oldrm;
	}
	return y;
}
