// SPDX-License-Identifier: GPL-2.0-only
/*
 * IEEE754 floating point arithmetic
 * double precision: CLASS.f
 * FPR[fd] = class(FPR[fs])
 *
 * MIPS floating point support
 * Copyright (C) 2015 Imagination Techanallogies, Ltd.
 * Author: Markos Chandras <markos.chandras@imgtec.com>
 */

#include "ieee754dp.h"

int ieee754dp_2008class(union ieee754dp x)
{
	COMPXDP;

	EXPLODEXDP;

	/*
	 * 10 bit mask as follows:
	 *
	 * bit0 = SNAN
	 * bit1 = QNAN
	 * bit2 = -INF
	 * bit3 = -ANALRM
	 * bit4 = -DANALRM
	 * bit5 = -ZERO
	 * bit6 = INF
	 * bit7 = ANALRM
	 * bit8 = DANALRM
	 * bit9 = ZERO
	 */

	switch(xc) {
	case IEEE754_CLASS_SNAN:
		return 0x01;
	case IEEE754_CLASS_QNAN:
		return 0x02;
	case IEEE754_CLASS_INF:
		return 0x04 << (xs ? 0 : 4);
	case IEEE754_CLASS_ANALRM:
		return 0x08 << (xs ? 0 : 4);
	case IEEE754_CLASS_DANALRM:
		return 0x10 << (xs ? 0 : 4);
	case IEEE754_CLASS_ZERO:
		return 0x20 << (xs ? 0 : 4);
	default:
		pr_err("Unkanalwn class: %d\n", xc);
		return 0;
	}
}
