/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "inner.h"

/* see inner.h */
void
br_des_do_IP(uint32_t *xl, uint32_t *xr)
{
	/*
	 * Permutation algorithm is initially from Richard Outerbridge;
	 * implementation here is adapted from Crypto++ "des.cpp" file
	 * (which is in public domain).
	 */
	uint32_t l, r, t;

	l = *xl;
	r = *xr;
	t = ((l >>  4) ^ r) & (uint32_t)0x0F0F0F0F;
	r ^= t;
	l ^= t <<  4;
	t = ((l >> 16) ^ r) & (uint32_t)0x0000FFFF;
	r ^= t;
	l ^= t << 16;
	t = ((r >>  2) ^ l) & (uint32_t)0x33333333;
	l ^= t;
	r ^= t <<  2;
	t = ((r >>  8) ^ l) & (uint32_t)0x00FF00FF;
	l ^= t;
	r ^= t <<  8;
	t = ((l >>  1) ^ r) & (uint32_t)0x55555555;
	r ^= t;
	l ^= t <<  1;
	*xl = l;
	*xr = r;
}

/* see inner.h */
void
br_des_do_invIP(uint32_t *xl, uint32_t *xr)
{
	/*
	 * See br_des_do_IP().
	 */
	uint32_t l, r, t;

	l = *xl;
	r = *xr;
	t = ((l >>  1) ^ r) & 0x55555555;
	r ^= t;
	l ^= t <<  1;
	t = ((r >>  8) ^ l) & 0x00FF00FF;
	l ^= t;
	r ^= t <<  8;
	t = ((r >>  2) ^ l) & 0x33333333;
	l ^= t;
	r ^= t <<  2;
	t = ((l >> 16) ^ r) & 0x0000FFFF;
	r ^= t;
	l ^= t << 16;
	t = ((l >>  4) ^ r) & 0x0F0F0F0F;
	r ^= t;
	l ^= t <<  4;
	*xl = l;
	*xr = r;
}

/* see inner.h */
void
br_des_keysched_unit(uint32_t *skey, const void *key)
{
	uint32_t xl, xr, kl, kr;
	int i;

	xl = br_dec32be(key);
	xr = br_dec32be((const unsigned char *)key + 4);

	/*
	 * Permutation PC-1 is quite similar to the IP permutation.
	 * Definition of IP (in FIPS 46-3 notations) is:
	 *   58 50 42 34 26 18 10 2
	 *   60 52 44 36 28 20 12 4
	 *   62 54 46 38 30 22 14 6
	 *   64 56 48 40 32 24 16 8
	 *   57 49 41 33 25 17  9 1
	 *   59 51 43 35 27 19 11 3
	 *   61 53 45 37 29 21 13 5
	 *   63 55 47 39 31 23 15 7
	 *
	 * Definition of PC-1 is:
	 *   57 49 41 33 25 17  9 1
	 *   58 50 42 34 26 18 10 2
	 *   59 51 43 35 27 19 11 3
	 *   60 52 44 36
	 *   63 55 47 39 31 23 15 7
	 *   62 54 46 38 30 22 14 6
	 *   61 53 45 37 29 21 13 5
	 *   28 20 12  4
	 */
	br_des_do_IP(&xl, &xr);
	kl = ((xr & (uint32_t)0xFF000000) >> 4)
		| ((xl & (uint32_t)0xFF000000) >> 12)
		| ((xr & (uint32_t)0x00FF0000) >> 12)
		| ((xl & (uint32_t)0x00FF0000) >> 20);
	kr = ((xr & (uint32_t)0x000000FF) << 20)
		| ((xl & (uint32_t)0x0000FF00) << 4)
		| ((xr & (uint32_t)0x0000FF00) >> 4)
		| ((xl & (uint32_t)0x000F0000) >> 16);

	/*
	 * For each round, rotate the two 28-bit words kl and kr.
	 * The extraction of the 48-bit subkey (PC-2) is not done yet.
	 */
	for (i = 0; i < 16; i ++) {
		if ((1 << i) & 0x8103) {
			kl = (kl << 1) | (kl >> 27);
			kr = (kr << 1) | (kr >> 27);
		} else {
			kl = (kl << 2) | (kl >> 26);
			kr = (kr << 2) | (kr >> 26);
		}
		kl &= (uint32_t)0x0FFFFFFF;
		kr &= (uint32_t)0x0FFFFFFF;
		skey[(i << 1) + 0] = kl;
		skey[(i << 1) + 1] = kr;
	}
}

/* see inner.h */
void
br_des_rev_skey(uint32_t *skey)
{
	int i;

	for (i = 0; i < 16; i += 2) {
		uint32_t t;

		t = skey[i + 0];
		skey[i + 0] = skey[30 - i];
		skey[30 - i] = t;
		t = skey[i + 1];
		skey[i + 1] = skey[31 - i];
		skey[31 - i] = t;
	}
}
