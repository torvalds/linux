// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/types.h>
#include "tishift.h"

union ti {
	__int128_t val;
	struct {
		u64 high;
		u64 low;
	};
};

noinstr __int128_t __ashlti3(__int128_t a, int shift)
{
	union ti ti = { .val = a };

	if (!shift)
		return ti.val;
	if (shift < 64) {
		ti.high = (ti.high << shift) | (ti.low >> (64 - shift));
		ti.low = ti.low << shift;
	} else {
		ti.high = ti.low << (shift - 64);
		ti.low = 0;
	}
	return ti.val;
}
EXPORT_SYMBOL(__ashlti3);

noinstr __int128_t __ashrti3(__int128_t a, int shift)
{
	union ti ti = { .val = a };

	if (!shift)
		return ti.val;
	if (shift < 64) {
		ti.low = (ti.low >> shift) | (ti.high << (64 - shift));
		ti.high = (int64_t)ti.high >> shift;
	} else {
		ti.low = (int64_t)ti.high >> (shift - 64);
		ti.high = (int64_t)ti.high >> 63;
	}
	return ti.val;
}
EXPORT_SYMBOL(__ashrti3);

noinstr __int128_t __lshrti3(__int128_t a, int shift)
{
	union ti ti = { .val = a };

	if (!shift)
		return ti.val;
	if (shift < 64) {
		ti.low = (ti.low >> shift) | (ti.high << (64 - shift));
		ti.high = ti.high >> shift;
	} else {
		ti.low = ti.high >> (shift - 64);
		ti.high = 0;
	}
	return ti.val;
}
EXPORT_SYMBOL(__lshrti3);
