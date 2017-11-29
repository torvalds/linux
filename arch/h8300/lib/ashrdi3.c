// SPDX-License-Identifier: GPL-2.0
#include "libgcc.h"

DWtype __ashrdi3(DWtype u, word_type b)
{
	const DWunion uu = {.ll = u};
	const word_type bm = (sizeof (Wtype) * BITS_PER_UNIT) - b;
	DWunion w;

	if (b == 0)
		return u;

	if (bm <= 0) {
		/* w.s.high = 1..1 or 0..0 */
		w.s.high = uu.s.high >> (sizeof (Wtype) * BITS_PER_UNIT - 1);
		w.s.low = uu.s.high >> -bm;
	} else {
		const UWtype carries = (UWtype) uu.s.high << bm;

		w.s.high = uu.s.high >> b;
		w.s.low = ((UWtype) uu.s.low >> b) | carries;
	}

	return w.ll;
}
