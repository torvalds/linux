/*
 * Copyright (c) 2013, 2014 Kenneth MacKay. All rights reserved.
 * Copyright (c) 2019 Vitaly Chikunov <vt@altlinux.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *  * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/swab.h>
#include <linux/fips.h>
#include <crypto/ecdh.h>
#include <crypto/rng.h>
#include <asm/unaligned.h>
#include <linux/ratelimit.h>

#include "ecc.h"
#include "ecc_curve_defs.h"

typedef struct {
	u64 m_low;
	u64 m_high;
} uint128_t;

const struct ecc_curve *ecc_get_curve(unsigned int curve_id)
{
	switch (curve_id) {
	/* In FIPS mode only allow P256 and higher */
	case ECC_CURVE_NIST_P192:
		return fips_enabled ? NULL : &nist_p192;
	case ECC_CURVE_NIST_P256:
		return &nist_p256;
	case ECC_CURVE_NIST_P384:
		return &nist_p384;
	default:
		return NULL;
	}
}
EXPORT_SYMBOL(ecc_get_curve);

static u64 *ecc_alloc_digits_space(unsigned int ndigits)
{
	size_t len = ndigits * sizeof(u64);

	if (!len)
		return NULL;

	return kmalloc(len, GFP_KERNEL);
}

static void ecc_free_digits_space(u64 *space)
{
	kfree_sensitive(space);
}

static struct ecc_point *ecc_alloc_point(unsigned int ndigits)
{
	struct ecc_point *p = kmalloc(sizeof(*p), GFP_KERNEL);

	if (!p)
		return NULL;

	p->x = ecc_alloc_digits_space(ndigits);
	if (!p->x)
		goto err_alloc_x;

	p->y = ecc_alloc_digits_space(ndigits);
	if (!p->y)
		goto err_alloc_y;

	p->ndigits = ndigits;

	return p;

err_alloc_y:
	ecc_free_digits_space(p->x);
err_alloc_x:
	kfree(p);
	return NULL;
}

static void ecc_free_point(struct ecc_point *p)
{
	if (!p)
		return;

	kfree_sensitive(p->x);
	kfree_sensitive(p->y);
	kfree_sensitive(p);
}

static void vli_clear(u64 *vli, unsigned int ndigits)
{
	int i;

	for (i = 0; i < ndigits; i++)
		vli[i] = 0;
}

/* Returns true if vli == 0, false otherwise. */
bool vli_is_zero(const u64 *vli, unsigned int ndigits)
{
	int i;

	for (i = 0; i < ndigits; i++) {
		if (vli[i])
			return false;
	}

	return true;
}
EXPORT_SYMBOL(vli_is_zero);

/* Returns nonzero if bit bit of vli is set. */
static u64 vli_test_bit(const u64 *vli, unsigned int bit)
{
	return (vli[bit / 64] & ((u64)1 << (bit % 64)));
}

static bool vli_is_negative(const u64 *vli, unsigned int ndigits)
{
	return vli_test_bit(vli, ndigits * 64 - 1);
}

/* Counts the number of 64-bit "digits" in vli. */
static unsigned int vli_num_digits(const u64 *vli, unsigned int ndigits)
{
	int i;

	/* Search from the end until we find a non-zero digit.
	 * We do it in reverse because we expect that most digits will
	 * be nonzero.
	 */
	for (i = ndigits - 1; i >= 0 && vli[i] == 0; i--);

	return (i + 1);
}

/* Counts the number of bits required for vli. */
static unsigned int vli_num_bits(const u64 *vli, unsigned int ndigits)
{
	unsigned int i, num_digits;
	u64 digit;

	num_digits = vli_num_digits(vli, ndigits);
	if (num_digits == 0)
		return 0;

	digit = vli[num_digits - 1];
	for (i = 0; digit; i++)
		digit >>= 1;

	return ((num_digits - 1) * 64 + i);
}

/* Set dest from unaligned bit string src. */
void vli_from_be64(u64 *dest, const void *src, unsigned int ndigits)
{
	int i;
	const u64 *from = src;

	for (i = 0; i < ndigits; i++)
		dest[i] = get_unaligned_be64(&from[ndigits - 1 - i]);
}
EXPORT_SYMBOL(vli_from_be64);

void vli_from_le64(u64 *dest, const void *src, unsigned int ndigits)
{
	int i;
	const u64 *from = src;

	for (i = 0; i < ndigits; i++)
		dest[i] = get_unaligned_le64(&from[i]);
}
EXPORT_SYMBOL(vli_from_le64);

/* Sets dest = src. */
static void vli_set(u64 *dest, const u64 *src, unsigned int ndigits)
{
	int i;

	for (i = 0; i < ndigits; i++)
		dest[i] = src[i];
}

/* Returns sign of left - right. */
int vli_cmp(const u64 *left, const u64 *right, unsigned int ndigits)
{
	int i;

	for (i = ndigits - 1; i >= 0; i--) {
		if (left[i] > right[i])
			return 1;
		else if (left[i] < right[i])
			return -1;
	}

	return 0;
}
EXPORT_SYMBOL(vli_cmp);

/* Computes result = in << c, returning carry. Can modify in place
 * (if result == in). 0 < shift < 64.
 */
static u64 vli_lshift(u64 *result, const u64 *in, unsigned int shift,
		      unsigned int ndigits)
{
	u64 carry = 0;
	int i;

	for (i = 0; i < ndigits; i++) {
		u64 temp = in[i];

		result[i] = (temp << shift) | carry;
		carry = temp >> (64 - shift);
	}

	return carry;
}

/* Computes vli = vli >> 1. */
static void vli_rshift1(u64 *vli, unsigned int ndigits)
{
	u64 *end = vli;
	u64 carry = 0;

	vli += ndigits;

	while (vli-- > end) {
		u64 temp = *vli;
		*vli = (temp >> 1) | carry;
		carry = temp << 63;
	}
}

/* Computes result = left + right, returning carry. Can modify in place. */
static u64 vli_add(u64 *result, const u64 *left, const u64 *right,
		   unsigned int ndigits)
{
	u64 carry = 0;
	int i;

	for (i = 0; i < ndigits; i++) {
		u64 sum;

		sum = left[i] + right[i] + carry;
		if (sum != left[i])
			carry = (sum < left[i]);

		result[i] = sum;
	}

	return carry;
}

/* Computes result = left + right, returning carry. Can modify in place. */
static u64 vli_uadd(u64 *result, const u64 *left, u64 right,
		    unsigned int ndigits)
{
	u64 carry = right;
	int i;

	for (i = 0; i < ndigits; i++) {
		u64 sum;

		sum = left[i] + carry;
		if (sum != left[i])
			carry = (sum < left[i]);
		else
			carry = !!carry;

		result[i] = sum;
	}

	return carry;
}

/* Computes result = left - right, returning borrow. Can modify in place. */
u64 vli_sub(u64 *result, const u64 *left, const u64 *right,
		   unsigned int ndigits)
{
	u64 borrow = 0;
	int i;

	for (i = 0; i < ndigits; i++) {
		u64 diff;

		diff = left[i] - right[i] - borrow;
		if (diff != left[i])
			borrow = (diff > left[i]);

		result[i] = diff;
	}

	return borrow;
}
EXPORT_SYMBOL(vli_sub);

/* Computes result = left - right, returning borrow. Can modify in place. */
static u64 vli_usub(u64 *result, const u64 *left, u64 right,
	     unsigned int ndigits)
{
	u64 borrow = right;
	int i;

	for (i = 0; i < ndigits; i++) {
		u64 diff;

		diff = left[i] - borrow;
		if (diff != left[i])
			borrow = (diff > left[i]);

		result[i] = diff;
	}

	return borrow;
}

static uint128_t mul_64_64(u64 left, u64 right)
{
	uint128_t result;
#if defined(CONFIG_ARCH_SUPPORTS_INT128)
	unsigned __int128 m = (unsigned __int128)left * right;

	result.m_low  = m;
	result.m_high = m >> 64;
#else
	u64 a0 = left & 0xffffffffull;
	u64 a1 = left >> 32;
	u64 b0 = right & 0xffffffffull;
	u64 b1 = right >> 32;
	u64 m0 = a0 * b0;
	u64 m1 = a0 * b1;
	u64 m2 = a1 * b0;
	u64 m3 = a1 * b1;

	m2 += (m0 >> 32);
	m2 += m1;

	/* Overflow */
	if (m2 < m1)
		m3 += 0x100000000ull;

	result.m_low = (m0 & 0xffffffffull) | (m2 << 32);
	result.m_high = m3 + (m2 >> 32);
#endif
	return result;
}

static uint128_t add_128_128(uint128_t a, uint128_t b)
{
	uint128_t result;

	result.m_low = a.m_low + b.m_low;
	result.m_high = a.m_high + b.m_high + (result.m_low < a.m_low);

	return result;
}

static void vli_mult(u64 *result, const u64 *left, const u64 *right,
		     unsigned int ndigits)
{
	uint128_t r01 = { 0, 0 };
	u64 r2 = 0;
	unsigned int i, k;

	/* Compute each digit of result in sequence, maintaining the
	 * carries.
	 */
	for (k = 0; k < ndigits * 2 - 1; k++) {
		unsigned int min;

		if (k < ndigits)
			min = 0;
		else
			min = (k + 1) - ndigits;

		for (i = min; i <= k && i < ndigits; i++) {
			uint128_t product;

			product = mul_64_64(left[i], right[k - i]);

			r01 = add_128_128(r01, product);
			r2 += (r01.m_high < product.m_high);
		}

		result[k] = r01.m_low;
		r01.m_low = r01.m_high;
		r01.m_high = r2;
		r2 = 0;
	}

	result[ndigits * 2 - 1] = r01.m_low;
}

/* Compute product = left * right, for a small right value. */
static void vli_umult(u64 *result, const u64 *left, u32 right,
		      unsigned int ndigits)
{
	uint128_t r01 = { 0 };
	unsigned int k;

	for (k = 0; k < ndigits; k++) {
		uint128_t product;

		product = mul_64_64(left[k], right);
		r01 = add_128_128(r01, product);
		/* no carry */
		result[k] = r01.m_low;
		r01.m_low = r01.m_high;
		r01.m_high = 0;
	}
	result[k] = r01.m_low;
	for (++k; k < ndigits * 2; k++)
		result[k] = 0;
}

static void vli_square(u64 *result, const u64 *left, unsigned int ndigits)
{
	uint128_t r01 = { 0, 0 };
	u64 r2 = 0;
	int i, k;

	for (k = 0; k < ndigits * 2 - 1; k++) {
		unsigned int min;

		if (k < ndigits)
			min = 0;
		else
			min = (k + 1) - ndigits;

		for (i = min; i <= k && i <= k - i; i++) {
			uint128_t product;

			product = mul_64_64(left[i], left[k - i]);

			if (i < k - i) {
				r2 += product.m_high >> 63;
				product.m_high = (product.m_high << 1) |
						 (product.m_low >> 63);
				product.m_low <<= 1;
			}

			r01 = add_128_128(r01, product);
			r2 += (r01.m_high < product.m_high);
		}

		result[k] = r01.m_low;
		r01.m_low = r01.m_high;
		r01.m_high = r2;
		r2 = 0;
	}

	result[ndigits * 2 - 1] = r01.m_low;
}

/* Computes result = (left + right) % mod.
 * Assumes that left < mod and right < mod, result != mod.
 */
static void vli_mod_add(u64 *result, const u64 *left, const u64 *right,
			const u64 *mod, unsigned int ndigits)
{
	u64 carry;

	carry = vli_add(result, left, right, ndigits);

	/* result > mod (result = mod + remainder), so subtract mod to
	 * get remainder.
	 */
	if (carry || vli_cmp(result, mod, ndigits) >= 0)
		vli_sub(result, result, mod, ndigits);
}

/* Computes result = (left - right) % mod.
 * Assumes that left < mod and right < mod, result != mod.
 */
static void vli_mod_sub(u64 *result, const u64 *left, const u64 *right,
			const u64 *mod, unsigned int ndigits)
{
	u64 borrow = vli_sub(result, left, right, ndigits);

	/* In this case, p_result == -diff == (max int) - diff.
	 * Since -x % d == d - x, we can get the correct result from
	 * result + mod (with overflow).
	 */
	if (borrow)
		vli_add(result, result, mod, ndigits);
}

/*
 * Computes result = product % mod
 * for special form moduli: p = 2^k-c, for small c (note the minus sign)
 *
 * References:
 * R. Crandall, C. Pomerance. Prime Numbers: A Computational Perspective.
 * 9 Fast Algorithms for Large-Integer Arithmetic. 9.2.3 Moduli of special form
 * Algorithm 9.2.13 (Fast mod operation for special-form moduli).
 */
static void vli_mmod_special(u64 *result, const u64 *product,
			      const u64 *mod, unsigned int ndigits)
{
	u64 c = -mod[0];
	u64 t[ECC_MAX_DIGITS * 2];
	u64 r[ECC_MAX_DIGITS * 2];

	vli_set(r, product, ndigits * 2);
	while (!vli_is_zero(r + ndigits, ndigits)) {
		vli_umult(t, r + ndigits, c, ndigits);
		vli_clear(r + ndigits, ndigits);
		vli_add(r, r, t, ndigits * 2);
	}
	vli_set(t, mod, ndigits);
	vli_clear(t + ndigits, ndigits);
	while (vli_cmp(r, t, ndigits * 2) >= 0)
		vli_sub(r, r, t, ndigits * 2);
	vli_set(result, r, ndigits);
}

/*
 * Computes result = product % mod
 * for special form moduli: p = 2^{k-1}+c, for small c (note the plus sign)
 * where k-1 does not fit into qword boundary by -1 bit (such as 255).

 * References (loosely based on):
 * A. Menezes, P. van Oorschot, S. Vanstone. Handbook of Applied Cryptography.
 * 14.3.4 Reduction methods for moduli of special form. Algorithm 14.47.
 * URL: http://cacr.uwaterloo.ca/hac/about/chap14.pdf
 *
 * H. Cohen, G. Frey, R. Avanzi, C. Doche, T. Lange, K. Nguyen, F. Vercauteren.
 * Handbook of Elliptic and Hyperelliptic Curve Cryptography.
 * Algorithm 10.25 Fast reduction for special form moduli
 */
static void vli_mmod_special2(u64 *result, const u64 *product,
			       const u64 *mod, unsigned int ndigits)
{
	u64 c2 = mod[0] * 2;
	u64 q[ECC_MAX_DIGITS];
	u64 r[ECC_MAX_DIGITS * 2];
	u64 m[ECC_MAX_DIGITS * 2]; /* expanded mod */
	int carry; /* last bit that doesn't fit into q */
	int i;

	vli_set(m, mod, ndigits);
	vli_clear(m + ndigits, ndigits);

	vli_set(r, product, ndigits);
	/* q and carry are top bits */
	vli_set(q, product + ndigits, ndigits);
	vli_clear(r + ndigits, ndigits);
	carry = vli_is_negative(r, ndigits);
	if (carry)
		r[ndigits - 1] &= (1ull << 63) - 1;
	for (i = 1; carry || !vli_is_zero(q, ndigits); i++) {
		u64 qc[ECC_MAX_DIGITS * 2];

		vli_umult(qc, q, c2, ndigits);
		if (carry)
			vli_uadd(qc, qc, mod[0], ndigits * 2);
		vli_set(q, qc + ndigits, ndigits);
		vli_clear(qc + ndigits, ndigits);
		carry = vli_is_negative(qc, ndigits);
		if (carry)
			qc[ndigits - 1] &= (1ull << 63) - 1;
		if (i & 1)
			vli_sub(r, r, qc, ndigits * 2);
		else
			vli_add(r, r, qc, ndigits * 2);
	}
	while (vli_is_negative(r, ndigits * 2))
		vli_add(r, r, m, ndigits * 2);
	while (vli_cmp(r, m, ndigits * 2) >= 0)
		vli_sub(r, r, m, ndigits * 2);

	vli_set(result, r, ndigits);
}

/*
 * Computes result = product % mod, where product is 2N words long.
 * Reference: Ken MacKay's micro-ecc.
 * Currently only designed to work for curve_p or curve_n.
 */
static void vli_mmod_slow(u64 *result, u64 *product, const u64 *mod,
			  unsigned int ndigits)
{
	u64 mod_m[2 * ECC_MAX_DIGITS];
	u64 tmp[2 * ECC_MAX_DIGITS];
	u64 *v[2] = { tmp, product };
	u64 carry = 0;
	unsigned int i;
	/* Shift mod so its highest set bit is at the maximum position. */
	int shift = (ndigits * 2 * 64) - vli_num_bits(mod, ndigits);
	int word_shift = shift / 64;
	int bit_shift = shift % 64;

	vli_clear(mod_m, word_shift);
	if (bit_shift > 0) {
		for (i = 0; i < ndigits; ++i) {
			mod_m[word_shift + i] = (mod[i] << bit_shift) | carry;
			carry = mod[i] >> (64 - bit_shift);
		}
	} else
		vli_set(mod_m + word_shift, mod, ndigits);

	for (i = 1; shift >= 0; --shift) {
		u64 borrow = 0;
		unsigned int j;

		for (j = 0; j < ndigits * 2; ++j) {
			u64 diff = v[i][j] - mod_m[j] - borrow;

			if (diff != v[i][j])
				borrow = (diff > v[i][j]);
			v[1 - i][j] = diff;
		}
		i = !(i ^ borrow); /* Swap the index if there was no borrow */
		vli_rshift1(mod_m, ndigits);
		mod_m[ndigits - 1] |= mod_m[ndigits] << (64 - 1);
		vli_rshift1(mod_m + ndigits, ndigits);
	}
	vli_set(result, v[i], ndigits);
}

/* Computes result = product % mod using Barrett's reduction with precomputed
 * value mu appended to the mod after ndigits, mu = (2^{2w} / mod) and have
 * length ndigits + 1, where mu * (2^w - 1) should not overflow ndigits
 * boundary.
 *
 * Reference:
 * R. Brent, P. Zimmermann. Modern Computer Arithmetic. 2010.
 * 2.4.1 Barrett's algorithm. Algorithm 2.5.
 */
static void vli_mmod_barrett(u64 *result, u64 *product, const u64 *mod,
			     unsigned int ndigits)
{
	u64 q[ECC_MAX_DIGITS * 2];
	u64 r[ECC_MAX_DIGITS * 2];
	const u64 *mu = mod + ndigits;

	vli_mult(q, product + ndigits, mu, ndigits);
	if (mu[ndigits])
		vli_add(q + ndigits, q + ndigits, product + ndigits, ndigits);
	vli_mult(r, mod, q + ndigits, ndigits);
	vli_sub(r, product, r, ndigits * 2);
	while (!vli_is_zero(r + ndigits, ndigits) ||
	       vli_cmp(r, mod, ndigits) != -1) {
		u64 carry;

		carry = vli_sub(r, r, mod, ndigits);
		vli_usub(r + ndigits, r + ndigits, carry, ndigits);
	}
	vli_set(result, r, ndigits);
}

/* Computes p_result = p_product % curve_p.
 * See algorithm 5 and 6 from
 * http://www.isys.uni-klu.ac.at/PDF/2001-0126-MT.pdf
 */
static void vli_mmod_fast_192(u64 *result, const u64 *product,
			      const u64 *curve_prime, u64 *tmp)
{
	const unsigned int ndigits = 3;
	int carry;

	vli_set(result, product, ndigits);

	vli_set(tmp, &product[3], ndigits);
	carry = vli_add(result, result, tmp, ndigits);

	tmp[0] = 0;
	tmp[1] = product[3];
	tmp[2] = product[4];
	carry += vli_add(result, result, tmp, ndigits);

	tmp[0] = tmp[1] = product[5];
	tmp[2] = 0;
	carry += vli_add(result, result, tmp, ndigits);

	while (carry || vli_cmp(curve_prime, result, ndigits) != 1)
		carry -= vli_sub(result, result, curve_prime, ndigits);
}

/* Computes result = product % curve_prime
 * from http://www.nsa.gov/ia/_files/nist-routines.pdf
 */
static void vli_mmod_fast_256(u64 *result, const u64 *product,
			      const u64 *curve_prime, u64 *tmp)
{
	int carry;
	const unsigned int ndigits = 4;

	/* t */
	vli_set(result, product, ndigits);

	/* s1 */
	tmp[0] = 0;
	tmp[1] = product[5] & 0xffffffff00000000ull;
	tmp[2] = product[6];
	tmp[3] = product[7];
	carry = vli_lshift(tmp, tmp, 1, ndigits);
	carry += vli_add(result, result, tmp, ndigits);

	/* s2 */
	tmp[1] = product[6] << 32;
	tmp[2] = (product[6] >> 32) | (product[7] << 32);
	tmp[3] = product[7] >> 32;
	carry += vli_lshift(tmp, tmp, 1, ndigits);
	carry += vli_add(result, result, tmp, ndigits);

	/* s3 */
	tmp[0] = product[4];
	tmp[1] = product[5] & 0xffffffff;
	tmp[2] = 0;
	tmp[3] = product[7];
	carry += vli_add(result, result, tmp, ndigits);

	/* s4 */
	tmp[0] = (product[4] >> 32) | (product[5] << 32);
	tmp[1] = (product[5] >> 32) | (product[6] & 0xffffffff00000000ull);
	tmp[2] = product[7];
	tmp[3] = (product[6] >> 32) | (product[4] << 32);
	carry += vli_add(result, result, tmp, ndigits);

	/* d1 */
	tmp[0] = (product[5] >> 32) | (product[6] << 32);
	tmp[1] = (product[6] >> 32);
	tmp[2] = 0;
	tmp[3] = (product[4] & 0xffffffff) | (product[5] << 32);
	carry -= vli_sub(result, result, tmp, ndigits);

	/* d2 */
	tmp[0] = product[6];
	tmp[1] = product[7];
	tmp[2] = 0;
	tmp[3] = (product[4] >> 32) | (product[5] & 0xffffffff00000000ull);
	carry -= vli_sub(result, result, tmp, ndigits);

	/* d3 */
	tmp[0] = (product[6] >> 32) | (product[7] << 32);
	tmp[1] = (product[7] >> 32) | (product[4] << 32);
	tmp[2] = (product[4] >> 32) | (product[5] << 32);
	tmp[3] = (product[6] << 32);
	carry -= vli_sub(result, result, tmp, ndigits);

	/* d4 */
	tmp[0] = product[7];
	tmp[1] = product[4] & 0xffffffff00000000ull;
	tmp[2] = product[5];
	tmp[3] = product[6] & 0xffffffff00000000ull;
	carry -= vli_sub(result, result, tmp, ndigits);

	if (carry < 0) {
		do {
			carry += vli_add(result, result, curve_prime, ndigits);
		} while (carry < 0);
	} else {
		while (carry || vli_cmp(curve_prime, result, ndigits) != 1)
			carry -= vli_sub(result, result, curve_prime, ndigits);
	}
}

/* Computes result = product % curve_prime for different curve_primes.
 *
 * Note that curve_primes are distinguished just by heuristic check and
 * not by complete conformance check.
 */
static bool vli_mmod_fast(u64 *result, u64 *product,
			  const u64 *curve_prime, unsigned int ndigits)
{
	u64 tmp[2 * ECC_MAX_DIGITS];

	/* Currently, both NIST primes have -1 in lowest qword. */
	if (curve_prime[0] != -1ull) {
		/* Try to handle Pseudo-Marsenne primes. */
		if (curve_prime[ndigits - 1] == -1ull) {
			vli_mmod_special(result, product, curve_prime,
					 ndigits);
			return true;
		} else if (curve_prime[ndigits - 1] == 1ull << 63 &&
			   curve_prime[ndigits - 2] == 0) {
			vli_mmod_special2(result, product, curve_prime,
					  ndigits);
			return true;
		}
		vli_mmod_barrett(result, product, curve_prime, ndigits);
		return true;
	}

	switch (ndigits) {
	case 3:
		vli_mmod_fast_192(result, product, curve_prime, tmp);
		break;
	case 4:
		vli_mmod_fast_256(result, product, curve_prime, tmp);
		break;
	default:
		pr_err_ratelimited("ecc: unsupported digits size!\n");
		return false;
	}

	return true;
}

/* Computes result = (left * right) % mod.
 * Assumes that mod is big enough curve order.
 */
void vli_mod_mult_slow(u64 *result, const u64 *left, const u64 *right,
		       const u64 *mod, unsigned int ndigits)
{
	u64 product[ECC_MAX_DIGITS * 2];

	vli_mult(product, left, right, ndigits);
	vli_mmod_slow(result, product, mod, ndigits);
}
EXPORT_SYMBOL(vli_mod_mult_slow);

/* Computes result = (left * right) % curve_prime. */
static void vli_mod_mult_fast(u64 *result, const u64 *left, const u64 *right,
			      const u64 *curve_prime, unsigned int ndigits)
{
	u64 product[2 * ECC_MAX_DIGITS];

	vli_mult(product, left, right, ndigits);
	vli_mmod_fast(result, product, curve_prime, ndigits);
}

/* Computes result = left^2 % curve_prime. */
static void vli_mod_square_fast(u64 *result, const u64 *left,
				const u64 *curve_prime, unsigned int ndigits)
{
	u64 product[2 * ECC_MAX_DIGITS];

	vli_square(product, left, ndigits);
	vli_mmod_fast(result, product, curve_prime, ndigits);
}

#define EVEN(vli) (!(vli[0] & 1))
/* Computes result = (1 / p_input) % mod. All VLIs are the same size.
 * See "From Euclid's GCD to Montgomery Multiplication to the Great Divide"
 * https://labs.oracle.com/techrep/2001/smli_tr-2001-95.pdf
 */
void vli_mod_inv(u64 *result, const u64 *input, const u64 *mod,
			unsigned int ndigits)
{
	u64 a[ECC_MAX_DIGITS], b[ECC_MAX_DIGITS];
	u64 u[ECC_MAX_DIGITS], v[ECC_MAX_DIGITS];
	u64 carry;
	int cmp_result;

	if (vli_is_zero(input, ndigits)) {
		vli_clear(result, ndigits);
		return;
	}

	vli_set(a, input, ndigits);
	vli_set(b, mod, ndigits);
	vli_clear(u, ndigits);
	u[0] = 1;
	vli_clear(v, ndigits);

	while ((cmp_result = vli_cmp(a, b, ndigits)) != 0) {
		carry = 0;

		if (EVEN(a)) {
			vli_rshift1(a, ndigits);

			if (!EVEN(u))
				carry = vli_add(u, u, mod, ndigits);

			vli_rshift1(u, ndigits);
			if (carry)
				u[ndigits - 1] |= 0x8000000000000000ull;
		} else if (EVEN(b)) {
			vli_rshift1(b, ndigits);

			if (!EVEN(v))
				carry = vli_add(v, v, mod, ndigits);

			vli_rshift1(v, ndigits);
			if (carry)
				v[ndigits - 1] |= 0x8000000000000000ull;
		} else if (cmp_result > 0) {
			vli_sub(a, a, b, ndigits);
			vli_rshift1(a, ndigits);

			if (vli_cmp(u, v, ndigits) < 0)
				vli_add(u, u, mod, ndigits);

			vli_sub(u, u, v, ndigits);
			if (!EVEN(u))
				carry = vli_add(u, u, mod, ndigits);

			vli_rshift1(u, ndigits);
			if (carry)
				u[ndigits - 1] |= 0x8000000000000000ull;
		} else {
			vli_sub(b, b, a, ndigits);
			vli_rshift1(b, ndigits);

			if (vli_cmp(v, u, ndigits) < 0)
				vli_add(v, v, mod, ndigits);

			vli_sub(v, v, u, ndigits);
			if (!EVEN(v))
				carry = vli_add(v, v, mod, ndigits);

			vli_rshift1(v, ndigits);
			if (carry)
				v[ndigits - 1] |= 0x8000000000000000ull;
		}
	}

	vli_set(result, u, ndigits);
}
EXPORT_SYMBOL(vli_mod_inv);

/* ------ Point operations ------ */

/* Returns true if p_point is the point at infinity, false otherwise. */
static bool ecc_point_is_zero(const struct ecc_point *point)
{
	return (vli_is_zero(point->x, point->ndigits) &&
		vli_is_zero(point->y, point->ndigits));
}

/* Point multiplication algorithm using Montgomery's ladder with co-Z
 * coordinates. From https://eprint.iacr.org/2011/338.pdf
 */

/* Double in place */
static void ecc_point_double_jacobian(u64 *x1, u64 *y1, u64 *z1,
				      u64 *curve_prime, unsigned int ndigits)
{
	/* t1 = x, t2 = y, t3 = z */
	u64 t4[ECC_MAX_DIGITS];
	u64 t5[ECC_MAX_DIGITS];

	if (vli_is_zero(z1, ndigits))
		return;

	/* t4 = y1^2 */
	vli_mod_square_fast(t4, y1, curve_prime, ndigits);
	/* t5 = x1*y1^2 = A */
	vli_mod_mult_fast(t5, x1, t4, curve_prime, ndigits);
	/* t4 = y1^4 */
	vli_mod_square_fast(t4, t4, curve_prime, ndigits);
	/* t2 = y1*z1 = z3 */
	vli_mod_mult_fast(y1, y1, z1, curve_prime, ndigits);
	/* t3 = z1^2 */
	vli_mod_square_fast(z1, z1, curve_prime, ndigits);

	/* t1 = x1 + z1^2 */
	vli_mod_add(x1, x1, z1, curve_prime, ndigits);
	/* t3 = 2*z1^2 */
	vli_mod_add(z1, z1, z1, curve_prime, ndigits);
	/* t3 = x1 - z1^2 */
	vli_mod_sub(z1, x1, z1, curve_prime, ndigits);
	/* t1 = x1^2 - z1^4 */
	vli_mod_mult_fast(x1, x1, z1, curve_prime, ndigits);

	/* t3 = 2*(x1^2 - z1^4) */
	vli_mod_add(z1, x1, x1, curve_prime, ndigits);
	/* t1 = 3*(x1^2 - z1^4) */
	vli_mod_add(x1, x1, z1, curve_prime, ndigits);
	if (vli_test_bit(x1, 0)) {
		u64 carry = vli_add(x1, x1, curve_prime, ndigits);

		vli_rshift1(x1, ndigits);
		x1[ndigits - 1] |= carry << 63;
	} else {
		vli_rshift1(x1, ndigits);
	}
	/* t1 = 3/2*(x1^2 - z1^4) = B */

	/* t3 = B^2 */
	vli_mod_square_fast(z1, x1, curve_prime, ndigits);
	/* t3 = B^2 - A */
	vli_mod_sub(z1, z1, t5, curve_prime, ndigits);
	/* t3 = B^2 - 2A = x3 */
	vli_mod_sub(z1, z1, t5, curve_prime, ndigits);
	/* t5 = A - x3 */
	vli_mod_sub(t5, t5, z1, curve_prime, ndigits);
	/* t1 = B * (A - x3) */
	vli_mod_mult_fast(x1, x1, t5, curve_prime, ndigits);
	/* t4 = B * (A - x3) - y1^4 = y3 */
	vli_mod_sub(t4, x1, t4, curve_prime, ndigits);

	vli_set(x1, z1, ndigits);
	vli_set(z1, y1, ndigits);
	vli_set(y1, t4, ndigits);
}

/* Modify (x1, y1) => (x1 * z^2, y1 * z^3) */
static void apply_z(u64 *x1, u64 *y1, u64 *z, u64 *curve_prime,
		    unsigned int ndigits)
{
	u64 t1[ECC_MAX_DIGITS];

	vli_mod_square_fast(t1, z, curve_prime, ndigits);    /* z^2 */
	vli_mod_mult_fast(x1, x1, t1, curve_prime, ndigits); /* x1 * z^2 */
	vli_mod_mult_fast(t1, t1, z, curve_prime, ndigits);  /* z^3 */
	vli_mod_mult_fast(y1, y1, t1, curve_prime, ndigits); /* y1 * z^3 */
}

/* P = (x1, y1) => 2P, (x2, y2) => P' */
static void xycz_initial_double(u64 *x1, u64 *y1, u64 *x2, u64 *y2,
				u64 *p_initial_z, u64 *curve_prime,
				unsigned int ndigits)
{
	u64 z[ECC_MAX_DIGITS];

	vli_set(x2, x1, ndigits);
	vli_set(y2, y1, ndigits);

	vli_clear(z, ndigits);
	z[0] = 1;

	if (p_initial_z)
		vli_set(z, p_initial_z, ndigits);

	apply_z(x1, y1, z, curve_prime, ndigits);

	ecc_point_double_jacobian(x1, y1, z, curve_prime, ndigits);

	apply_z(x2, y2, z, curve_prime, ndigits);
}

/* Input P = (x1, y1, Z), Q = (x2, y2, Z)
 * Output P' = (x1', y1', Z3), P + Q = (x3, y3, Z3)
 * or P => P', Q => P + Q
 */
static void xycz_add(u64 *x1, u64 *y1, u64 *x2, u64 *y2, u64 *curve_prime,
		     unsigned int ndigits)
{
	/* t1 = X1, t2 = Y1, t3 = X2, t4 = Y2 */
	u64 t5[ECC_MAX_DIGITS];

	/* t5 = x2 - x1 */
	vli_mod_sub(t5, x2, x1, curve_prime, ndigits);
	/* t5 = (x2 - x1)^2 = A */
	vli_mod_square_fast(t5, t5, curve_prime, ndigits);
	/* t1 = x1*A = B */
	vli_mod_mult_fast(x1, x1, t5, curve_prime, ndigits);
	/* t3 = x2*A = C */
	vli_mod_mult_fast(x2, x2, t5, curve_prime, ndigits);
	/* t4 = y2 - y1 */
	vli_mod_sub(y2, y2, y1, curve_prime, ndigits);
	/* t5 = (y2 - y1)^2 = D */
	vli_mod_square_fast(t5, y2, curve_prime, ndigits);

	/* t5 = D - B */
	vli_mod_sub(t5, t5, x1, curve_prime, ndigits);
	/* t5 = D - B - C = x3 */
	vli_mod_sub(t5, t5, x2, curve_prime, ndigits);
	/* t3 = C - B */
	vli_mod_sub(x2, x2, x1, curve_prime, ndigits);
	/* t2 = y1*(C - B) */
	vli_mod_mult_fast(y1, y1, x2, curve_prime, ndigits);
	/* t3 = B - x3 */
	vli_mod_sub(x2, x1, t5, curve_prime, ndigits);
	/* t4 = (y2 - y1)*(B - x3) */
	vli_mod_mult_fast(y2, y2, x2, curve_prime, ndigits);
	/* t4 = y3 */
	vli_mod_sub(y2, y2, y1, curve_prime, ndigits);

	vli_set(x2, t5, ndigits);
}

/* Input P = (x1, y1, Z), Q = (x2, y2, Z)
 * Output P + Q = (x3, y3, Z3), P - Q = (x3', y3', Z3)
 * or P => P - Q, Q => P + Q
 */
static void xycz_add_c(u64 *x1, u64 *y1, u64 *x2, u64 *y2, u64 *curve_prime,
		       unsigned int ndigits)
{
	/* t1 = X1, t2 = Y1, t3 = X2, t4 = Y2 */
	u64 t5[ECC_MAX_DIGITS];
	u64 t6[ECC_MAX_DIGITS];
	u64 t7[ECC_MAX_DIGITS];

	/* t5 = x2 - x1 */
	vli_mod_sub(t5, x2, x1, curve_prime, ndigits);
	/* t5 = (x2 - x1)^2 = A */
	vli_mod_square_fast(t5, t5, curve_prime, ndigits);
	/* t1 = x1*A = B */
	vli_mod_mult_fast(x1, x1, t5, curve_prime, ndigits);
	/* t3 = x2*A = C */
	vli_mod_mult_fast(x2, x2, t5, curve_prime, ndigits);
	/* t4 = y2 + y1 */
	vli_mod_add(t5, y2, y1, curve_prime, ndigits);
	/* t4 = y2 - y1 */
	vli_mod_sub(y2, y2, y1, curve_prime, ndigits);

	/* t6 = C - B */
	vli_mod_sub(t6, x2, x1, curve_prime, ndigits);
	/* t2 = y1 * (C - B) */
	vli_mod_mult_fast(y1, y1, t6, curve_prime, ndigits);
	/* t6 = B + C */
	vli_mod_add(t6, x1, x2, curve_prime, ndigits);
	/* t3 = (y2 - y1)^2 */
	vli_mod_square_fast(x2, y2, curve_prime, ndigits);
	/* t3 = x3 */
	vli_mod_sub(x2, x2, t6, curve_prime, ndigits);

	/* t7 = B - x3 */
	vli_mod_sub(t7, x1, x2, curve_prime, ndigits);
	/* t4 = (y2 - y1)*(B - x3) */
	vli_mod_mult_fast(y2, y2, t7, curve_prime, ndigits);
	/* t4 = y3 */
	vli_mod_sub(y2, y2, y1, curve_prime, ndigits);

	/* t7 = (y2 + y1)^2 = F */
	vli_mod_square_fast(t7, t5, curve_prime, ndigits);
	/* t7 = x3' */
	vli_mod_sub(t7, t7, t6, curve_prime, ndigits);
	/* t6 = x3' - B */
	vli_mod_sub(t6, t7, x1, curve_prime, ndigits);
	/* t6 = (y2 + y1)*(x3' - B) */
	vli_mod_mult_fast(t6, t6, t5, curve_prime, ndigits);
	/* t2 = y3' */
	vli_mod_sub(y1, t6, y1, curve_prime, ndigits);

	vli_set(x1, t7, ndigits);
}

static void ecc_point_mult(struct ecc_point *result,
			   const struct ecc_point *point, const u64 *scalar,
			   u64 *initial_z, const struct ecc_curve *curve,
			   unsigned int ndigits)
{
	/* R0 and R1 */
	u64 rx[2][ECC_MAX_DIGITS];
	u64 ry[2][ECC_MAX_DIGITS];
	u64 z[ECC_MAX_DIGITS];
	u64 sk[2][ECC_MAX_DIGITS];
	u64 *curve_prime = curve->p;
	int i, nb;
	int num_bits;
	int carry;

	carry = vli_add(sk[0], scalar, curve->n, ndigits);
	vli_add(sk[1], sk[0], curve->n, ndigits);
	scalar = sk[!carry];
	num_bits = sizeof(u64) * ndigits * 8 + 1;

	vli_set(rx[1], point->x, ndigits);
	vli_set(ry[1], point->y, ndigits);

	xycz_initial_double(rx[1], ry[1], rx[0], ry[0], initial_z, curve_prime,
			    ndigits);

	for (i = num_bits - 2; i > 0; i--) {
		nb = !vli_test_bit(scalar, i);
		xycz_add_c(rx[1 - nb], ry[1 - nb], rx[nb], ry[nb], curve_prime,
			   ndigits);
		xycz_add(rx[nb], ry[nb], rx[1 - nb], ry[1 - nb], curve_prime,
			 ndigits);
	}

	nb = !vli_test_bit(scalar, 0);
	xycz_add_c(rx[1 - nb], ry[1 - nb], rx[nb], ry[nb], curve_prime,
		   ndigits);

	/* Find final 1/Z value. */
	/* X1 - X0 */
	vli_mod_sub(z, rx[1], rx[0], curve_prime, ndigits);
	/* Yb * (X1 - X0) */
	vli_mod_mult_fast(z, z, ry[1 - nb], curve_prime, ndigits);
	/* xP * Yb * (X1 - X0) */
	vli_mod_mult_fast(z, z, point->x, curve_prime, ndigits);

	/* 1 / (xP * Yb * (X1 - X0)) */
	vli_mod_inv(z, z, curve_prime, point->ndigits);

	/* yP / (xP * Yb * (X1 - X0)) */
	vli_mod_mult_fast(z, z, point->y, curve_prime, ndigits);
	/* Xb * yP / (xP * Yb * (X1 - X0)) */
	vli_mod_mult_fast(z, z, rx[1 - nb], curve_prime, ndigits);
	/* End 1/Z calculation */

	xycz_add(rx[nb], ry[nb], rx[1 - nb], ry[1 - nb], curve_prime, ndigits);

	apply_z(rx[0], ry[0], z, curve_prime, ndigits);

	vli_set(result->x, rx[0], ndigits);
	vli_set(result->y, ry[0], ndigits);
}

/* Computes R = P + Q mod p */
static void ecc_point_add(const struct ecc_point *result,
		   const struct ecc_point *p, const struct ecc_point *q,
		   const struct ecc_curve *curve)
{
	u64 z[ECC_MAX_DIGITS];
	u64 px[ECC_MAX_DIGITS];
	u64 py[ECC_MAX_DIGITS];
	unsigned int ndigits = curve->g.ndigits;

	vli_set(result->x, q->x, ndigits);
	vli_set(result->y, q->y, ndigits);
	vli_mod_sub(z, result->x, p->x, curve->p, ndigits);
	vli_set(px, p->x, ndigits);
	vli_set(py, p->y, ndigits);
	xycz_add(px, py, result->x, result->y, curve->p, ndigits);
	vli_mod_inv(z, z, curve->p, ndigits);
	apply_z(result->x, result->y, z, curve->p, ndigits);
}

/* Computes R = u1P + u2Q mod p using Shamir's trick.
 * Based on: Kenneth MacKay's micro-ecc (2014).
 */
void ecc_point_mult_shamir(const struct ecc_point *result,
			   const u64 *u1, const struct ecc_point *p,
			   const u64 *u2, const struct ecc_point *q,
			   const struct ecc_curve *curve)
{
	u64 z[ECC_MAX_DIGITS];
	u64 sump[2][ECC_MAX_DIGITS];
	u64 *rx = result->x;
	u64 *ry = result->y;
	unsigned int ndigits = curve->g.ndigits;
	unsigned int num_bits;
	struct ecc_point sum = ECC_POINT_INIT(sump[0], sump[1], ndigits);
	const struct ecc_point *points[4];
	const struct ecc_point *point;
	unsigned int idx;
	int i;

	ecc_point_add(&sum, p, q, curve);
	points[0] = NULL;
	points[1] = p;
	points[2] = q;
	points[3] = &sum;

	num_bits = max(vli_num_bits(u1, ndigits),
		       vli_num_bits(u2, ndigits));
	i = num_bits - 1;
	idx = (!!vli_test_bit(u1, i)) | ((!!vli_test_bit(u2, i)) << 1);
	point = points[idx];

	vli_set(rx, point->x, ndigits);
	vli_set(ry, point->y, ndigits);
	vli_clear(z + 1, ndigits - 1);
	z[0] = 1;

	for (--i; i >= 0; i--) {
		ecc_point_double_jacobian(rx, ry, z, curve->p, ndigits);
		idx = (!!vli_test_bit(u1, i)) | ((!!vli_test_bit(u2, i)) << 1);
		point = points[idx];
		if (point) {
			u64 tx[ECC_MAX_DIGITS];
			u64 ty[ECC_MAX_DIGITS];
			u64 tz[ECC_MAX_DIGITS];

			vli_set(tx, point->x, ndigits);
			vli_set(ty, point->y, ndigits);
			apply_z(tx, ty, z, curve->p, ndigits);
			vli_mod_sub(tz, rx, tx, curve->p, ndigits);
			xycz_add(tx, ty, rx, ry, curve->p, ndigits);
			vli_mod_mult_fast(z, z, tz, curve->p, ndigits);
		}
	}
	vli_mod_inv(z, z, curve->p, ndigits);
	apply_z(rx, ry, z, curve->p, ndigits);
}
EXPORT_SYMBOL(ecc_point_mult_shamir);

static int __ecc_is_key_valid(const struct ecc_curve *curve,
			      const u64 *private_key, unsigned int ndigits)
{
	u64 one[ECC_MAX_DIGITS] = { 1, };
	u64 res[ECC_MAX_DIGITS];

	if (!private_key)
		return -EINVAL;

	if (curve->g.ndigits != ndigits)
		return -EINVAL;

	/* Make sure the private key is in the range [2, n-3]. */
	if (vli_cmp(one, private_key, ndigits) != -1)
		return -EINVAL;
	vli_sub(res, curve->n, one, ndigits);
	vli_sub(res, res, one, ndigits);
	if (vli_cmp(res, private_key, ndigits) != 1)
		return -EINVAL;

	return 0;
}

int ecc_is_key_valid(unsigned int curve_id, unsigned int ndigits,
		     const u64 *private_key, unsigned int private_key_len)
{
	int nbytes;
	const struct ecc_curve *curve = ecc_get_curve(curve_id);

	nbytes = ndigits << ECC_DIGITS_TO_BYTES_SHIFT;

	if (private_key_len != nbytes)
		return -EINVAL;

	return __ecc_is_key_valid(curve, private_key, ndigits);
}
EXPORT_SYMBOL(ecc_is_key_valid);

/*
 * ECC private keys are generated using the method of extra random bits,
 * equivalent to that described in FIPS 186-4, Appendix B.4.1.
 *
 * d = (c mod(n–1)) + 1    where c is a string of random bits, 64 bits longer
 *                         than requested
 * 0 <= c mod(n-1) <= n-2  and implies that
 * 1 <= d <= n-1
 *
 * This method generates a private key uniformly distributed in the range
 * [1, n-1].
 */
int ecc_gen_privkey(unsigned int curve_id, unsigned int ndigits, u64 *privkey)
{
	const struct ecc_curve *curve = ecc_get_curve(curve_id);
	u64 priv[ECC_MAX_DIGITS];
	unsigned int nbytes = ndigits << ECC_DIGITS_TO_BYTES_SHIFT;
	unsigned int nbits = vli_num_bits(curve->n, ndigits);
	int err;

	/* Check that N is included in Table 1 of FIPS 186-4, section 6.1.1 */
	if (nbits < 160 || ndigits > ARRAY_SIZE(priv))
		return -EINVAL;

	/*
	 * FIPS 186-4 recommends that the private key should be obtained from a
	 * RBG with a security strength equal to or greater than the security
	 * strength associated with N.
	 *
	 * The maximum security strength identified by NIST SP800-57pt1r4 for
	 * ECC is 256 (N >= 512).
	 *
	 * This condition is met by the default RNG because it selects a favored
	 * DRBG with a security strength of 256.
	 */
	if (crypto_get_default_rng())
		return -EFAULT;

	err = crypto_rng_get_bytes(crypto_default_rng, (u8 *)priv, nbytes);
	crypto_put_default_rng();
	if (err)
		return err;

	/* Make sure the private key is in the valid range. */
	if (__ecc_is_key_valid(curve, priv, ndigits))
		return -EINVAL;

	ecc_swap_digits(priv, privkey, ndigits);

	return 0;
}
EXPORT_SYMBOL(ecc_gen_privkey);

int ecc_make_pub_key(unsigned int curve_id, unsigned int ndigits,
		     const u64 *private_key, u64 *public_key)
{
	int ret = 0;
	struct ecc_point *pk;
	u64 priv[ECC_MAX_DIGITS];
	const struct ecc_curve *curve = ecc_get_curve(curve_id);

	if (!private_key || !curve || ndigits > ARRAY_SIZE(priv)) {
		ret = -EINVAL;
		goto out;
	}

	ecc_swap_digits(private_key, priv, ndigits);

	pk = ecc_alloc_point(ndigits);
	if (!pk) {
		ret = -ENOMEM;
		goto out;
	}

	ecc_point_mult(pk, &curve->g, priv, NULL, curve, ndigits);

	/* SP800-56A rev 3 5.6.2.1.3 key check */
	if (ecc_is_pubkey_valid_full(curve, pk)) {
		ret = -EAGAIN;
		goto err_free_point;
	}

	ecc_swap_digits(pk->x, public_key, ndigits);
	ecc_swap_digits(pk->y, &public_key[ndigits], ndigits);

err_free_point:
	ecc_free_point(pk);
out:
	return ret;
}
EXPORT_SYMBOL(ecc_make_pub_key);

/* SP800-56A section 5.6.2.3.4 partial verification: ephemeral keys only */
int ecc_is_pubkey_valid_partial(const struct ecc_curve *curve,
				struct ecc_point *pk)
{
	u64 yy[ECC_MAX_DIGITS], xxx[ECC_MAX_DIGITS], w[ECC_MAX_DIGITS];

	if (WARN_ON(pk->ndigits != curve->g.ndigits))
		return -EINVAL;

	/* Check 1: Verify key is not the zero point. */
	if (ecc_point_is_zero(pk))
		return -EINVAL;

	/* Check 2: Verify key is in the range [1, p-1]. */
	if (vli_cmp(curve->p, pk->x, pk->ndigits) != 1)
		return -EINVAL;
	if (vli_cmp(curve->p, pk->y, pk->ndigits) != 1)
		return -EINVAL;

	/* Check 3: Verify that y^2 == (x^3 + a·x + b) mod p */
	vli_mod_square_fast(yy, pk->y, curve->p, pk->ndigits); /* y^2 */
	vli_mod_square_fast(xxx, pk->x, curve->p, pk->ndigits); /* x^2 */
	vli_mod_mult_fast(xxx, xxx, pk->x, curve->p, pk->ndigits); /* x^3 */
	vli_mod_mult_fast(w, curve->a, pk->x, curve->p, pk->ndigits); /* a·x */
	vli_mod_add(w, w, curve->b, curve->p, pk->ndigits); /* a·x + b */
	vli_mod_add(w, w, xxx, curve->p, pk->ndigits); /* x^3 + a·x + b */
	if (vli_cmp(yy, w, pk->ndigits) != 0) /* Equation */
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(ecc_is_pubkey_valid_partial);

/* SP800-56A section 5.6.2.3.3 full verification */
int ecc_is_pubkey_valid_full(const struct ecc_curve *curve,
			     struct ecc_point *pk)
{
	struct ecc_point *nQ;

	/* Checks 1 through 3 */
	int ret = ecc_is_pubkey_valid_partial(curve, pk);

	if (ret)
		return ret;

	/* Check 4: Verify that nQ is the zero point. */
	nQ = ecc_alloc_point(pk->ndigits);
	if (!nQ)
		return -ENOMEM;

	ecc_point_mult(nQ, pk, curve->n, NULL, curve, pk->ndigits);
	if (!ecc_point_is_zero(nQ))
		ret = -EINVAL;

	ecc_free_point(nQ);

	return ret;
}
EXPORT_SYMBOL(ecc_is_pubkey_valid_full);

int crypto_ecdh_shared_secret(unsigned int curve_id, unsigned int ndigits,
			      const u64 *private_key, const u64 *public_key,
			      u64 *secret)
{
	int ret = 0;
	struct ecc_point *product, *pk;
	u64 priv[ECC_MAX_DIGITS];
	u64 rand_z[ECC_MAX_DIGITS];
	unsigned int nbytes;
	const struct ecc_curve *curve = ecc_get_curve(curve_id);

	if (!private_key || !public_key || !curve ||
	    ndigits > ARRAY_SIZE(priv) || ndigits > ARRAY_SIZE(rand_z)) {
		ret = -EINVAL;
		goto out;
	}

	nbytes = ndigits << ECC_DIGITS_TO_BYTES_SHIFT;

	get_random_bytes(rand_z, nbytes);

	pk = ecc_alloc_point(ndigits);
	if (!pk) {
		ret = -ENOMEM;
		goto out;
	}

	ecc_swap_digits(public_key, pk->x, ndigits);
	ecc_swap_digits(&public_key[ndigits], pk->y, ndigits);
	ret = ecc_is_pubkey_valid_partial(curve, pk);
	if (ret)
		goto err_alloc_product;

	ecc_swap_digits(private_key, priv, ndigits);

	product = ecc_alloc_point(ndigits);
	if (!product) {
		ret = -ENOMEM;
		goto err_alloc_product;
	}

	ecc_point_mult(product, pk, priv, rand_z, curve, ndigits);

	if (ecc_point_is_zero(product)) {
		ret = -EFAULT;
		goto err_validity;
	}

	ecc_swap_digits(product->x, secret, ndigits);

err_validity:
	memzero_explicit(priv, sizeof(priv));
	memzero_explicit(rand_z, sizeof(rand_z));
	ecc_free_point(product);
err_alloc_product:
	ecc_free_point(pk);
out:
	return ret;
}
EXPORT_SYMBOL(crypto_ecdh_shared_secret);

MODULE_LICENSE("Dual BSD/GPL");
