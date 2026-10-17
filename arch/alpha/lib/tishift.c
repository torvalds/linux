// SPDX-License-Identifier: GPL-2.0
/*
 * 128-bit shift helpers.
 *
 * gcc open-codes a variable 128-bit shift when optimizing for speed, but
 * calls these libgcc routines when optimizing for size.
 */

#include <linux/export.h>
#include <linux/types.h>
#include <asm/asm-prototypes.h>

union ti {
	__int128_t val;
	struct {
		u64 low;
		u64 high;
	};
};

noinstr __int128_t __ashlti3(__int128_t a, int shift)
{
	union ti ti = { .val = a };

	if (!shift)
		return ti.val;
	if (shift < 64) {
		ti.high = (ti.high << shift) | (ti.low >> (64 - shift));
		ti.low <<= shift;
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
		ti.high = (s64)ti.high >> shift;
	} else {
		ti.low = (s64)ti.high >> (shift - 64);
		ti.high = (s64)ti.high >> 63;
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
		ti.high >>= shift;
	} else {
		ti.low = ti.high >> (shift - 64);
		ti.high = 0;
	}
	return ti.val;
}
EXPORT_SYMBOL(__lshrti3);
