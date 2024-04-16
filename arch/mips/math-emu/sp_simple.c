// SPDX-License-Identifier: GPL-2.0-only
/* IEEE754 floating point arithmetic
 * single precision
 */
/*
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.
 */

#include "ieee754sp.h"

union ieee754sp ieee754sp_neg(union ieee754sp x)
{
	union ieee754sp y;

	if (ieee754_csr.abs2008) {
		y = x;
		SPSIGN(y) = !SPSIGN(x);
	} else {
		unsigned int oldrm;

		oldrm = ieee754_csr.rm;
		ieee754_csr.rm = FPU_CSR_RD;
		y = ieee754sp_sub(ieee754sp_zero(0), x);
		ieee754_csr.rm = oldrm;
	}
	return y;
}

union ieee754sp ieee754sp_abs(union ieee754sp x)
{
	union ieee754sp y;

	if (ieee754_csr.abs2008) {
		y = x;
		SPSIGN(y) = 0;
	} else {
		unsigned int oldrm;

		oldrm = ieee754_csr.rm;
		ieee754_csr.rm = FPU_CSR_RD;
		if (SPSIGN(x))
			y = ieee754sp_sub(ieee754sp_zero(0), x);
		else
			y = ieee754sp_add(ieee754sp_zero(0), x);
		ieee754_csr.rm = oldrm;
	}
	return y;
}
