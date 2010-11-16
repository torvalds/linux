#include <linux/module.h>

#include "libgcc.h"

long long __ashrdi3(long long u, word_type b)
{
	DWunion uu, w;
	word_type bm;

	if (b == 0)
		return u;

	uu.ll = u;
	bm = 32 - b;

	if (bm <= 0) {
		/* w.s.high = 1..1 or 0..0 */
		w.s.high =
		    uu.s.high >> 31;
		w.s.low = uu.s.high >> -bm;
	} else {
		const unsigned int carries = (unsigned int) uu.s.high << bm;

		w.s.high = uu.s.high >> b;
		w.s.low = ((unsigned int) uu.s.low >> b) | carries;
	}

	return w.ll;
}

EXPORT_SYMBOL(__ashrdi3);
