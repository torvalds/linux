/*
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include "gcclib.h"

#ifdef CONFIG_ARITHMETIC_OPS_L1
DItype __ashrdi3(DItype u, word_type b)__attribute__((l1_text));
#endif

DItype __ashrdi3(DItype u, word_type b)
{
	DIunion w;
	word_type bm;
	DIunion uu;

	if (b == 0)
		return u;

	uu.ll = u;

	bm = (sizeof(SItype) * BITS_PER_UNIT) - b;
	if (bm <= 0) {
		/* w.s.high = 1..1 or 0..0 */
		w.s.high = uu.s.high >> (sizeof(SItype) * BITS_PER_UNIT - 1);
		w.s.low = uu.s.high >> -bm;
	} else {
		USItype carries = (USItype) uu.s.high << bm;
		w.s.high = uu.s.high >> b;
		w.s.low = ((USItype) uu.s.low >> b) | carries;
	}

	return w.ll;
}
