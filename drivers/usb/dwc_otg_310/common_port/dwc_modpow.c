/* Bignum routines adapted from PUTTY sources.  PuTTY copyright notice follows.
 *
 * PuTTY is copyright 1997-2007 Simon Tatham.
 *
 * Portions copyright Robert de Bath, Joris van Rantwijk, Delian
 * Delchev, Andreas Schultz, Jeroen Massar, Wez Furlong, Nicolas Barry,
 * Justin Bradford, Ben Harris, Malcolm Smith, Ahmad Khalifa, Markus
 * Kuhn, and CORE SDI S.A.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#ifdef DWC_CRYPTOLIB

#ifndef CONFIG_MACH_IPMATE

#include "dwc_modpow.h"

#define BIGNUM_INT_MASK  0xFFFFFFFFUL
#define BIGNUM_TOP_BIT   0x80000000UL
#define BIGNUM_INT_BITS  32

static void *snmalloc(void *mem_ctx, size_t n, size_t size)
{
	void *p;
	size *= n;
	if (size == 0)
		size = 1;
	p = dwc_alloc(mem_ctx, size);
	return p;
}

#define snewn(ctx, n, type) ((type *)snmalloc((ctx), (n), sizeof(type)))
#define sfree dwc_free

/*
 * Usage notes:
 *  * Do not call the DIVMOD_WORD macro with expressions such as array
 *    subscripts, as some implementations object to this (see below).
 *  * Note that none of the division methods below will cope if the
 *    quotient won't fit into BIGNUM_INT_BITS. Callers should be careful
 *    to avoid this case.
 *    If this condition occurs, in the case of the x86 DIV instruction,
 *    an overflow exception will occur, which (according to a correspondent)
 *    will manifest on Windows as something like
 *      0xC0000095: Integer overflow
 *    The C variant won't give the right answer, either.
 */

#define MUL_WORD(w1, w2) ((BignumDblInt)w1 * w2)

#if defined __GNUC__ && defined __i386__
#define DIVMOD_WORD(q, r, hi, lo, w) \
    __asm__("div %2" : \
	    "=d" (r), "=a" (q) : \
	    "r" (w), "d" (hi), "a" (lo))
#else
#define DIVMOD_WORD(q, r, hi, lo, w) do { \
    BignumDblInt n = (((BignumDblInt)hi) << BIGNUM_INT_BITS) | lo; \
    q = n / w; \
    r = n % w; \
} while (0)
#endif

#define BIGNUM_INT_BYTES (BIGNUM_INT_BITS / 8)

#define BIGNUM_INTERNAL

static Bignum newbn(void *mem_ctx, int length)
{
	Bignum b = snewn(mem_ctx, length + 1, BignumInt);
	/* if (!b) */
	/* abort(); */                 /* FIXME */
	DWC_MEMSET(b, 0, (length + 1) * sizeof(*b));
	b[0] = length;
	return b;
}

void freebn(void *mem_ctx, Bignum b)
{
	/*
	 * Burn the evidence, just in case.
	 */
	DWC_MEMSET(b, 0, sizeof(b[0]) * (b[0] + 1));
	sfree(mem_ctx, b);
}

/*
 * Compute c = a * b.
 * Input is in the first len words of a and b.
 * Result is returned in the first 2*len words of c.
 */
static void internal_mul(BignumInt *a, BignumInt *b, BignumInt *c, int len)
{
	int i, j;
	BignumDblInt t;

	for (j = 0; j < 2 * len; j++)
		c[j] = 0;

	for (i = len - 1; i >= 0; i--) {
		t = 0;
		for (j = len - 1; j >= 0; j--) {
			t += MUL_WORD(a[i], (BignumDblInt) b[j]);
			t += (BignumDblInt) c[i + j + 1];
			c[i + j + 1] = (BignumInt) t;
			t = t >> BIGNUM_INT_BITS;
		}
		c[i] = (BignumInt) t;
	}
}

static void internal_add_shifted(BignumInt *number, unsigned n, int shift)
{
	int word = 1 + (shift / BIGNUM_INT_BITS);
	int bshift = shift % BIGNUM_INT_BITS;
	BignumDblInt addend;

	addend = (BignumDblInt) n << bshift;

	while (addend) {
		addend += number[word];
		number[word] = (BignumInt) addend & BIGNUM_INT_MASK;
		addend >>= BIGNUM_INT_BITS;
		word++;
	}
}

/*
 * Compute a = a % m.
 * Input in first alen words of a and first mlen words of m.
 * Output in first alen words of a
 * (of which first alen-mlen words will be zero).
 * The MSW of m MUST have its high bit set.
 * Quotient is accumulated in the `quotient' array, which is a Bignum
 * rather than the internal bigendian format. Quotient parts are shifted
 * left by `qshift' before adding into quot.
 */
static void internal_mod(BignumInt *a, int alen,
			 BignumInt *m, int mlen, BignumInt *quot, int qshift)
{
	BignumInt m0, m1;
	unsigned int h;
	int i, k;

	m0 = m[0];
	if (mlen > 1)
		m1 = m[1];
	else
		m1 = 0;

	for (i = 0; i <= alen - mlen; i++) {
		BignumDblInt t;
		unsigned int q, r, c, ai1;

		if (i == 0) {
			h = 0;
		} else {
			h = a[i - 1];
			a[i - 1] = 0;
		}

		if (i == alen - 1)
			ai1 = 0;
		else
			ai1 = a[i + 1];

		/* Find q = h:a[i] / m0 */
		if (h >= m0) {
			/*
			 * Special case.
			 *
			 * To illustrate it, suppose a BignumInt is 8 bits, and
			 * we are dividing (say) A1:23:45:67 by A1:B2:C3. Then
			 * our initial division will be 0xA123 / 0xA1, which
			 * will give a quotient of 0x100 and a divide overflow.
			 * However, the invariants in this division algorithm
			 * are not violated, since the full number A1:23:... is
			 * _less_ than the quotient prefix A1:B2:... and so the
			 * following correction loop would have sorted it out.
			 *
			 * In this situation we set q to be the largest
			 * quotient we _can_ stomach (0xFF, of course).
			 */
			q = BIGNUM_INT_MASK;
		} else {
			/* Macro doesn't want an array subscript expression passed
			 * into it (see definition), so use a temporary. */
			BignumInt tmplo = a[i];
			DIVMOD_WORD(q, r, h, tmplo, m0);

			/* Refine our estimate of q by looking at
			   h:a[i]:a[i+1] / m0:m1 */
			t = MUL_WORD(m1, q);
			if (t > ((BignumDblInt) r << BIGNUM_INT_BITS) + ai1) {
				q--;
				t -= m1;
				r = (r + m0) & BIGNUM_INT_MASK;	/* overflow? */
				if (r >= (BignumDblInt) m0 &&
				    t >
				    ((BignumDblInt) r << BIGNUM_INT_BITS) + ai1)
					q--;
			}
		}

		/* Subtract q * m from a[i...] */
		c = 0;
		for (k = mlen - 1; k >= 0; k--) {
			t = MUL_WORD(q, m[k]);
			t += c;
			c = (unsigned)(t >> BIGNUM_INT_BITS);
			if ((BignumInt) t > a[i + k])
				c++;
			a[i + k] -= (BignumInt) t;
		}

		/* Add back m in case of borrow */
		if (c != h) {
			t = 0;
			for (k = mlen - 1; k >= 0; k--) {
				t += m[k];
				t += a[i + k];
				a[i + k] = (BignumInt) t;
				t = t >> BIGNUM_INT_BITS;
			}
			q--;
		}
		if (quot)
			internal_add_shifted(quot, q,
					     qshift + BIGNUM_INT_BITS * (alen -
									 mlen -
									 i));
	}
}

/*
 * Compute p % mod.
 * The most significant word of mod MUST be non-zero.
 * We assume that the result array is the same size as the mod array.
 * We optionally write out a quotient if `quotient' is non-NULL.
 * We can avoid writing out the result if `result' is NULL.
 */
void bigdivmod(void *mem_ctx, Bignum p, Bignum mod, Bignum result,
	       Bignum quotient)
{
	BignumInt *n, *m;
	int mshift;
	int plen, mlen, i, j;

	/* Allocate m of size mlen, copy mod to m */
	/* We use big endian internally */
	mlen = mod[0];
	m = snewn(mem_ctx, mlen, BignumInt);
	/* if (!m) */
	/* abort(); */                /* FIXME */
	for (j = 0; j < mlen; j++)
		m[j] = mod[mod[0] - j];

	/* Shift m left to make msb bit set */
	for (mshift = 0; mshift < BIGNUM_INT_BITS - 1; mshift++)
		if ((m[0] << mshift) & BIGNUM_TOP_BIT)
			break;
	if (mshift) {
		for (i = 0; i < mlen - 1; i++)
			m[i] =
			    (m[i] << mshift) | (m[i + 1] >>
						(BIGNUM_INT_BITS - mshift));
		m[mlen - 1] = m[mlen - 1] << mshift;
	}

	plen = p[0];
	/* Ensure plen > mlen */
	if (plen <= mlen)
		plen = mlen + 1;

	/* Allocate n of size plen, copy p to n */
	n = snewn(mem_ctx, plen, BignumInt);
	/* if (!n) */
	/* abort(); */                /* FIXME */
	for (j = 0; j < plen; j++)
		n[j] = 0;
	for (j = 1; j <= (int)p[0]; j++)
		n[plen - j] = p[j];

	/* Main computation */
	internal_mod(n, plen, m, mlen, quotient, mshift);

	/* Fixup result in case the modulus was shifted */
	if (mshift) {
		for (i = plen - mlen - 1; i < plen - 1; i++)
			n[i] =
			    (n[i] << mshift) | (n[i + 1] >>
						(BIGNUM_INT_BITS - mshift));
		n[plen - 1] = n[plen - 1] << mshift;
		internal_mod(n, plen, m, mlen, quotient, 0);
		for (i = plen - 1; i >= plen - mlen; i--)
			n[i] =
			    (n[i] >> mshift) | (n[i - 1] <<
						(BIGNUM_INT_BITS - mshift));
	}

	/* Copy result to buffer */
	if (result) {
		for (i = 1; i <= (int)result[0]; i++) {
			int j = plen - i;
			result[i] = j >= 0 ? n[j] : 0;
		}
	}

	/* Free temporary arrays */
	for (i = 0; i < mlen; i++)
		m[i] = 0;
	sfree(mem_ctx, m);
	for (i = 0; i < plen; i++)
		n[i] = 0;
	sfree(mem_ctx, n);
}

/*
 * Simple remainder.
 */
Bignum bigmod(void *mem_ctx, Bignum a, Bignum b)
{
	Bignum r = newbn(mem_ctx, b[0]);
	bigdivmod(mem_ctx, a, b, r, NULL);
	return r;
}

/*
 * Compute (base ^ exp) % mod.
 */
Bignum dwc_modpow(void *mem_ctx, Bignum base_in, Bignum exp, Bignum mod)
{
	BignumInt *a, *b, *n, *m;
	int mshift;
	int mlen, i, j;
	Bignum base, result;

	/*
	 * The most significant word of mod needs to be non-zero. It
	 * should already be, but let's make sure.
	 */
	/* assert(mod[mod[0]] != 0); */

	/*
	 * Make sure the base is smaller than the modulus, by reducing
	 * it modulo the modulus if not.
	 */
	base = bigmod(mem_ctx, base_in, mod);

	/* Allocate m of size mlen, copy mod to m */
	/* We use big endian internally */
	mlen = mod[0];
	m = snewn(mem_ctx, mlen, BignumInt);
	/* if (!m) */
	/* abort(); */                /* FIXME */
	for (j = 0; j < mlen; j++)
		m[j] = mod[mod[0] - j];

	/* Shift m left to make msb bit set */
	for (mshift = 0; mshift < BIGNUM_INT_BITS - 1; mshift++)
		if ((m[0] << mshift) & BIGNUM_TOP_BIT)
			break;
	if (mshift) {
		for (i = 0; i < mlen - 1; i++)
			m[i] =
			    (m[i] << mshift) | (m[i + 1] >>
						(BIGNUM_INT_BITS - mshift));
		m[mlen - 1] = m[mlen - 1] << mshift;
	}

	/* Allocate n of size mlen, copy base to n */
	n = snewn(mem_ctx, mlen, BignumInt);
	/* if (!n) */
	/* abort(); */                /* FIXME */
	i = mlen - base[0];
	for (j = 0; j < i; j++)
		n[j] = 0;
	for (j = 0; j < base[0]; j++)
		n[i + j] = base[base[0] - j];

	/* Allocate a and b of size 2*mlen. Set a = 1 */
	a = snewn(mem_ctx, 2 * mlen, BignumInt);
	/* if (!a) */
	/* abort(); */                /* FIXME */
	b = snewn(mem_ctx, 2 * mlen, BignumInt);
	/* if (!b) */
	/* abort(); */                /* FIXME */
	for (i = 0; i < 2 * mlen; i++)
		a[i] = 0;
	a[2 * mlen - 1] = 1;

	/* Skip leading zero bits of exp. */
	i = 0;
	j = BIGNUM_INT_BITS - 1;
	while (i < exp[0] && (exp[exp[0] - i] & (1 << j)) == 0) {
		j--;
		if (j < 0) {
			i++;
			j = BIGNUM_INT_BITS - 1;
		}
	}

	/* Main computation */
	while (i < exp[0]) {
		while (j >= 0) {
			internal_mul(a + mlen, a + mlen, b, mlen);
			internal_mod(b, mlen * 2, m, mlen, NULL, 0);
			if ((exp[exp[0] - i] & (1 << j)) != 0) {
				internal_mul(b + mlen, n, a, mlen);
				internal_mod(a, mlen * 2, m, mlen, NULL, 0);
			} else {
				BignumInt *t;
				t = a;
				a = b;
				b = t;
			}
			j--;
		}
		i++;
		j = BIGNUM_INT_BITS - 1;
	}

	/* Fixup result in case the modulus was shifted */
	if (mshift) {
		for (i = mlen - 1; i < 2 * mlen - 1; i++)
			a[i] =
			    (a[i] << mshift) | (a[i + 1] >>
						(BIGNUM_INT_BITS - mshift));
		a[2 * mlen - 1] = a[2 * mlen - 1] << mshift;
		internal_mod(a, mlen * 2, m, mlen, NULL, 0);
		for (i = 2 * mlen - 1; i >= mlen; i--)
			a[i] =
			    (a[i] >> mshift) | (a[i - 1] <<
						(BIGNUM_INT_BITS - mshift));
	}

	/* Copy result to buffer */
	result = newbn(mem_ctx, mod[0]);
	for (i = 0; i < mlen; i++)
		result[result[0] - i] = a[i + mlen];
	while (result[0] > 1 && result[result[0]] == 0)
		result[0]--;

	/* Free temporary arrays */
	for (i = 0; i < 2 * mlen; i++)
		a[i] = 0;
	sfree(mem_ctx, a);
	for (i = 0; i < 2 * mlen; i++)
		b[i] = 0;
	sfree(mem_ctx, b);
	for (i = 0; i < mlen; i++)
		m[i] = 0;
	sfree(mem_ctx, m);
	for (i = 0; i < mlen; i++)
		n[i] = 0;
	sfree(mem_ctx, n);

	freebn(mem_ctx, base);

	return result;
}

#ifdef UNITTEST

static __u32 dh_p[] = {
	96,
	0xFFFFFFFF,
	0xFFFFFFFF,
	0xA93AD2CA,
	0x4B82D120,
	0xE0FD108E,
	0x43DB5BFC,
	0x74E5AB31,
	0x08E24FA0,
	0xBAD946E2,
	0x770988C0,
	0x7A615D6C,
	0xBBE11757,
	0x177B200C,
	0x521F2B18,
	0x3EC86A64,
	0xD8760273,
	0xD98A0864,
	0xF12FFA06,
	0x1AD2EE6B,
	0xCEE3D226,
	0x4A25619D,
	0x1E8C94E0,
	0xDB0933D7,
	0xABF5AE8C,
	0xA6E1E4C7,
	0xB3970F85,
	0x5D060C7D,
	0x8AEA7157,
	0x58DBEF0A,
	0xECFB8504,
	0xDF1CBA64,
	0xA85521AB,
	0x04507A33,
	0xAD33170D,
	0x8AAAC42D,
	0x15728E5A,
	0x98FA0510,
	0x15D22618,
	0xEA956AE5,
	0x3995497C,
	0x95581718,
	0xDE2BCBF6,
	0x6F4C52C9,
	0xB5C55DF0,
	0xEC07A28F,
	0x9B2783A2,
	0x180E8603,
	0xE39E772C,
	0x2E36CE3B,
	0x32905E46,
	0xCA18217C,
	0xF1746C08,
	0x4ABC9804,
	0x670C354E,
	0x7096966D,
	0x9ED52907,
	0x208552BB,
	0x1C62F356,
	0xDCA3AD96,
	0x83655D23,
	0xFD24CF5F,
	0x69163FA8,
	0x1C55D39A,
	0x98DA4836,
	0xA163BF05,
	0xC2007CB8,
	0xECE45B3D,
	0x49286651,
	0x7C4B1FE6,
	0xAE9F2411,
	0x5A899FA5,
	0xEE386BFB,
	0xF406B7ED,
	0x0BFF5CB6,
	0xA637ED6B,
	0xF44C42E9,
	0x625E7EC6,
	0xE485B576,
	0x6D51C245,
	0x4FE1356D,
	0xF25F1437,
	0x302B0A6D,
	0xCD3A431B,
	0xEF9519B3,
	0x8E3404DD,
	0x514A0879,
	0x3B139B22,
	0x020BBEA6,
	0x8A67CC74,
	0x29024E08,
	0x80DC1CD1,
	0xC4C6628B,
	0x2168C234,
	0xC90FDAA2,
	0xFFFFFFFF,
	0xFFFFFFFF,
};

static __u32 dh_a[] = {
	8,
	0xdf367516,
	0x86459caa,
	0xe2d459a4,
	0xd910dae0,
	0x8a8b5e37,
	0x67ab31c6,
	0xf0b55ea9,
	0x440051d6,
};

static __u32 dh_b[] = {
	8,
	0xded92656,
	0xe07a048a,
	0x6fa452cd,
	0x2df89d30,
	0xc75f1b0f,
	0x8ce3578f,
	0x7980a324,
	0x5daec786,
};

static __u32 dh_g[] = {
	1,
	2,
};

int main(void)
{
	int i;
	__u32 *k;
	k = dwc_modpow(NULL, dh_g, dh_a, dh_p);

	printf("\n\n");
	for (i = 0; i < k[0]; i++) {
		__u32 word32 = k[k[0] - i];
		__u16 l = word32 & 0xffff;
		__u16 m = (word32 & 0xffff0000) >> 16;
		printf("%04x %04x ", m, l);
		if (!((i + 1) % 13))
			printf("\n");
	}
	printf("\n\n");

	if ((k[0] == 0x60) && (k[1] == 0x28e490e5) && (k[0x60] == 0x5a0d3d4e)) {
		printf("PASS\n\n");
	} else {
		printf("FAIL\n\n");
	}

}

#endif /* UNITTEST */

#endif /* CONFIG_MACH_IPMATE */

#endif /*DWC_CRYPTOLIB */
