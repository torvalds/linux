#include "libgcc.h"

DWtype __lshrdi3(DWtype u, word_type b)
{
	const DWunion uu = {.ll = u};
	const word_type bm = (sizeof (Wtype) * BITS_PER_UNIT) - b;
	DWunion w;

	if (b == 0)
		return u;

	if (bm <= 0) {
		w.s.high = 0;
		w.s.low = (UWtype) uu.s.high >> -bm;
	} else {
		const UWtype carries = (UWtype) uu.s.high << bm;

		w.s.high = (UWtype) uu.s.high >> b;
		w.s.low = ((UWtype) uu.s.low >> b) | carries;
	}

	return w.ll;
}
