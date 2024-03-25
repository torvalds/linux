// SPDX-License-Identifier: LGPL-2.1+
/*
 * MurmurHash3 was written by Austin Appleby, and is placed in the public
 * domain. The author hereby disclaims copyright to this source code.
 *
 * Adapted by John Wiele (jwiele@redhat.com).
 */

#include "murmurhash3.h"

static inline u64 rotl64(u64 x, s8 r)
{
	return (x << r) | (x >> (64 - r));
}

#define ROTL64(x, y) rotl64(x, y)
static __always_inline u64 getblock64(const u64 *p, int i)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return p[i];
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	return __builtin_bswap64(p[i]);
#else
#error "can't figure out byte order"
#endif
}

static __always_inline void putblock64(u64 *p, int i, u64 value)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	p[i] = value;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	p[i] = __builtin_bswap64(value);
#else
#error "can't figure out byte order"
#endif
}

/* Finalization mix - force all bits of a hash block to avalanche */

static __always_inline u64 fmix64(u64 k)
{
	k ^= k >> 33;
	k *= 0xff51afd7ed558ccdLLU;
	k ^= k >> 33;
	k *= 0xc4ceb9fe1a85ec53LLU;
	k ^= k >> 33;

	return k;
}

void murmurhash3_128(const void *key, const int len, const u32 seed, void *out)
{
	const u8 *data = key;
	const int nblocks = len / 16;

	u64 h1 = seed;
	u64 h2 = seed;

	const u64 c1 = 0x87c37b91114253d5LLU;
	const u64 c2 = 0x4cf5ad432745937fLLU;

	/* body */

	const u64 *blocks = (const u64 *)(data);

	int i;

	for (i = 0; i < nblocks; i++) {
		u64 k1 = getblock64(blocks, i * 2 + 0);
		u64 k2 = getblock64(blocks, i * 2 + 1);

		k1 *= c1;
		k1 = ROTL64(k1, 31);
		k1 *= c2;
		h1 ^= k1;

		h1 = ROTL64(h1, 27);
		h1 += h2;
		h1 = h1 * 5 + 0x52dce729;

		k2 *= c2;
		k2 = ROTL64(k2, 33);
		k2 *= c1;
		h2 ^= k2;

		h2 = ROTL64(h2, 31);
		h2 += h1;
		h2 = h2 * 5 + 0x38495ab5;
	}

	/* tail */

	{
		const u8 *tail = (const u8 *)(data + nblocks * 16);

		u64 k1 = 0;
		u64 k2 = 0;

		switch (len & 15) {
		case 15:
			k2 ^= ((u64)tail[14]) << 48;
			fallthrough;
		case 14:
			k2 ^= ((u64)tail[13]) << 40;
			fallthrough;
		case 13:
			k2 ^= ((u64)tail[12]) << 32;
			fallthrough;
		case 12:
			k2 ^= ((u64)tail[11]) << 24;
			fallthrough;
		case 11:
			k2 ^= ((u64)tail[10]) << 16;
			fallthrough;
		case 10:
			k2 ^= ((u64)tail[9]) << 8;
			fallthrough;
		case 9:
			k2 ^= ((u64)tail[8]) << 0;
			k2 *= c2;
			k2 = ROTL64(k2, 33);
			k2 *= c1;
			h2 ^= k2;
			fallthrough;

		case 8:
			k1 ^= ((u64)tail[7]) << 56;
			fallthrough;
		case 7:
			k1 ^= ((u64)tail[6]) << 48;
			fallthrough;
		case 6:
			k1 ^= ((u64)tail[5]) << 40;
			fallthrough;
		case 5:
			k1 ^= ((u64)tail[4]) << 32;
			fallthrough;
		case 4:
			k1 ^= ((u64)tail[3]) << 24;
			fallthrough;
		case 3:
			k1 ^= ((u64)tail[2]) << 16;
			fallthrough;
		case 2:
			k1 ^= ((u64)tail[1]) << 8;
			fallthrough;
		case 1:
			k1 ^= ((u64)tail[0]) << 0;
			k1 *= c1;
			k1 = ROTL64(k1, 31);
			k1 *= c2;
			h1 ^= k1;
			break;
		default:
			break;
		};
	}
	/* finalization */

	h1 ^= len;
	h2 ^= len;

	h1 += h2;
	h2 += h1;

	h1 = fmix64(h1);
	h2 = fmix64(h2);

	h1 += h2;
	h2 += h1;

	putblock64((u64 *)out, 0, h1);
	putblock64((u64 *)out, 1, h2);
}
