/*
 * Copyright (c) 2018 Thomas Pornin <pornin@bolet.org>
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

/* see bearssl_ec.h */
size_t
br_ec_keygen(const br_prng_class **rng_ctx,
	const br_ec_impl *impl, br_ec_private_key *sk,
	void *kbuf, int curve)
{
	const unsigned char *order;
	unsigned char *buf;
	size_t len;
	unsigned mask;

	if (curve < 0 || curve >= 32
		|| ((impl->supported_curves >> curve) & 1) == 0)
	{
		return 0;
	}
	order = impl->order(curve, &len);
	while (len > 0 && *order == 0) {
		order ++;
		len --;
	}
	if (kbuf == NULL || len == 0) {
		return len;
	}
	mask = order[0];
	mask |= (mask >> 1);
	mask |= (mask >> 2);
	mask |= (mask >> 4);

	/*
	 * We generate sequences of random bits of the right size, until
	 * the value is strictly lower than the curve order (we also
	 * check for all-zero values, which are invalid).
	 */
	buf = kbuf;
	for (;;) {
		size_t u;
		unsigned cc, zz;

		(*rng_ctx)->generate(rng_ctx, buf, len);
		buf[0] &= mask;
		cc = 0;
		u = len;
		zz = 0;
		while (u -- > 0) {
			cc = ((unsigned)(buf[u] - order[u] - cc) >> 8) & 1;
			zz |= buf[u];
		}
		if (cc != 0 && zz != 0) {
			break;
		}
	}

	if (sk != NULL) {
		sk->curve = curve;
		sk->x = buf;
		sk->xlen = len;
	}
	return len;
}
