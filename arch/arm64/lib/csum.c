// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2019-2020 Arm Ltd.

#include <linux/compiler.h>
#include <linux/kasan-checks.h>
#include <linux/kernel.h>

#include <net/checksum.h>

/* Looks dumb, but generates nice-ish code */
static u64 accumulate(u64 sum, u64 data)
{
	__uint128_t tmp = (__uint128_t)sum + data;
	return tmp + (tmp >> 64);
}

unsigned int do_csum(const unsigned char *buff, int len)
{
	unsigned int offset, shift, sum;
	const u64 *ptr;
	u64 data, sum64 = 0;

	offset = (unsigned long)buff & 7;
	/*
	 * This is to all intents and purposes safe, since rounding down cannot
	 * result in a different page or cache line being accessed, and @buff
	 * should absolutely not be pointing to anything read-sensitive. We do,
	 * however, have to be careful not to piss off KASAN, which means using
	 * unchecked reads to accommodate the head and tail, for which we'll
	 * compensate with an explicit check up-front.
	 */
	kasan_check_read(buff, len);
	ptr = (u64 *)(buff - offset);
	len = len + offset - 8;

	/*
	 * Head: zero out any excess leading bytes. Shifting back by the same
	 * amount should be at least as fast as any other way of handling the
	 * odd/even alignment, and means we can ignore it until the very end.
	 */
	shift = offset * 8;
	data = READ_ONCE_NOCHECK(*ptr++);
#ifdef __LITTLE_ENDIAN
	data = (data >> shift) << shift;
#else
	data = (data << shift) >> shift;
#endif

	/*
	 * Body: straightforward aligned loads from here on (the paired loads
	 * underlying the quadword type still only need dword alignment). The
	 * main loop strictly excludes the tail, so the second loop will always
	 * run at least once.
	 */
	while (unlikely(len > 64)) {
		__uint128_t tmp1, tmp2, tmp3, tmp4;

		tmp1 = READ_ONCE_NOCHECK(*(__uint128_t *)ptr);
		tmp2 = READ_ONCE_NOCHECK(*(__uint128_t *)(ptr + 2));
		tmp3 = READ_ONCE_NOCHECK(*(__uint128_t *)(ptr + 4));
		tmp4 = READ_ONCE_NOCHECK(*(__uint128_t *)(ptr + 6));

		len -= 64;
		ptr += 8;

		/* This is the "don't dump the carry flag into a GPR" idiom */
		tmp1 += (tmp1 >> 64) | (tmp1 << 64);
		tmp2 += (tmp2 >> 64) | (tmp2 << 64);
		tmp3 += (tmp3 >> 64) | (tmp3 << 64);
		tmp4 += (tmp4 >> 64) | (tmp4 << 64);
		tmp1 = ((tmp1 >> 64) << 64) | (tmp2 >> 64);
		tmp1 += (tmp1 >> 64) | (tmp1 << 64);
		tmp3 = ((tmp3 >> 64) << 64) | (tmp4 >> 64);
		tmp3 += (tmp3 >> 64) | (tmp3 << 64);
		tmp1 = ((tmp1 >> 64) << 64) | (tmp3 >> 64);
		tmp1 += (tmp1 >> 64) | (tmp1 << 64);
		tmp1 = ((tmp1 >> 64) << 64) | sum64;
		tmp1 += (tmp1 >> 64) | (tmp1 << 64);
		sum64 = tmp1 >> 64;
	}
	while (len > 8) {
		__uint128_t tmp;

		sum64 = accumulate(sum64, data);
		tmp = READ_ONCE_NOCHECK(*(__uint128_t *)ptr);

		len -= 16;
		ptr += 2;

#ifdef __LITTLE_ENDIAN
		data = tmp >> 64;
		sum64 = accumulate(sum64, tmp);
#else
		data = tmp;
		sum64 = accumulate(sum64, tmp >> 64);
#endif
	}
	if (len > 0) {
		sum64 = accumulate(sum64, data);
		data = READ_ONCE_NOCHECK(*ptr);
		len -= 8;
	}
	/*
	 * Tail: zero any over-read bytes similarly to the head, again
	 * preserving odd/even alignment.
	 */
	shift = len * -8;
#ifdef __LITTLE_ENDIAN
	data = (data << shift) >> shift;
#else
	data = (data >> shift) << shift;
#endif
	sum64 = accumulate(sum64, data);

	/* Finally, folding */
	sum64 += (sum64 >> 32) | (sum64 << 32);
	sum = sum64 >> 32;
	sum += (sum >> 16) | (sum << 16);
	if (offset & 1)
		return (u16)swab32(sum);

	return sum >> 16;
}
