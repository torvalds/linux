/*
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include "gcclib.h"

#ifdef CONFIG_ARITHMETIC_OPS_L1
DItype __ashldi3(DItype u, word_type b)__attribute__((l1_text));
#endif

DItype __ashldi3(DItype u, word_type b)
{
	DIunion w;
	word_type bm;
	DIunion uu;

	if (b == 0)
		return u;

	uu.ll = u;

	bm = (sizeof(SItype) * BITS_PER_UNIT) - b;
	if (bm <= 0) {
		w.s.low = 0;
		w.s.high = (USItype) uu.s.low << -bm;
	} else {
		USItype carries = (USItype) uu.s.low >> bm;
		w.s.low = (USItype) uu.s.low << b;
		w.s.high = ((USItype) uu.s.high << b) | carries;
	}

	return w.ll;
}
