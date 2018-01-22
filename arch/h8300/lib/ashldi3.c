// SPDX-License-Identifier: GPL-2.0
#include "libgcc.h"

DWtype
__ashldi3(DWtype u, word_type b)
{
	const DWunion uu = {.ll = u};
	const word_type bm = (sizeof (Wtype) * BITS_PER_UNIT) - b;
	DWunion w;

	if (b == 0)
		return u;

	if (bm <= 0) {
		w.s.low = 0;
		w.s.high = (UWtype) uu.s.low << -bm;
	} else {
		const UWtype carries = (UWtype) uu.s.low >> bm;

		w.s.low = (UWtype) uu.s.low << b;
		w.s.high = ((UWtype) uu.s.high << b) | carries;
	}

	return w.ll;
}
