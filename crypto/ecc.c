/*
 * Copyright (c) 2013, Kenneth MacKay
 * All rights reserved.
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

#include <linux/random.h>
#include <linux/slab.h>
#include <linux/swab.h>
#include <linux/fips.h>
#include <crypto/ecdh.h>

#include "ecc.h"
#include "ecc_curve_defs.h"

typedef struct {
	u64 m_low;
	u64 m_high;
} uint128_t;

static inline const struct ecc_curve *ecc_get_curve(unsigned int curve_id)
{
	switch (curve_id) {
	/* In FIPS mode only allow P256 and higher */
	case ECC_CURVE_NIST_P192:
		return fips_enabled ? NULL : &nist_p192;
	case ECC_CURVE_NIST_P256:
		return &nist_p256;
	default:
		return NULL;
	}
}

static u64 *ecc_alloc_digits_space(unsigned int ndigits)
{
	size_t len = ndigits * sizeof(u64);

	if (!len)
		return NULL;

	return kmalloc(len, GFP_KERNEL);
}

static void ecc_free_digits_space(u64 *space)
{
	kzfree(space);
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

	kzfree(p->x);
	kzfree(p->y);
	kzfree(p);
}

static void vli_clear(u64 *vli, unsigned int ndigits)
{
	int i;

	for (i = 0; i < ndigits; i++)
		vli[i] = 0;
}

/* Returns true if vli == 0, false otherwise. */
static bool vli_is_zero(const u64 *vli, unsigned int ndigits)
{
	int i;

	for (i = 0; i < ndigits; i++) {
		if (vli[i])
			return false;
	}

	return true;
}

/* Returns nonzero if bit bit of vli is set. */
static u64 vli_test_bit(const u64 *vli, unsigned int bit)
{
	return (vli[bit / 64] & ((u64)1 << (bit % 64)));
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

/* Sets dest = src. */
static void vli_set(u64 *dest, const u64 *src, unsigned int ndigits)
{
	int i;

	for (i = 0; i < ndigits; i++)
		dest[i] = src[i];
}

/* Returns sign of left - right. */
static int vli_cmp(const u64 *left, const u64 *right, unsigned int ndigits)
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

/* Computes result = left - right, returning borrow. Can modify in place. */
static u64 vli_sub(u64 *result, const u64 *left, const u64 *right,
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

static uint128_t mul_64_64(u64 left, u64 right)
{
	u64 a0 = left & 0xffffffffull;
	u64 a1 = left >> 32;
	u64 b0 = right & 0xffffffffull;
	u64 b1 = right >> 32;
	u64 m0 = a0 * b0;
	u64 m1 = a0 * b1;
	u64 m2 = a1 * b0;
	u64 m3 = a1 * b1;
	uint128_t result;

	m2 += (m0 >> 32);
	m2 += m1;

	/* Overflow */
	if (m2 < m1)
		m3 += 0x100000000ull;

	result.m_low = (m0 & 0xffffffffull) | (m2 << 32);
	result.m_high = m3 + (m2 >> 32);

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

/* Computes result = product % curve_prime
 *  from http://www.nsa.gov/ia/_files/nist-routines.pdf
*/
static bool vli_mmod_fast(u64 *result, u64 *product,
			  const u64 *curve_prime, unsigned int ndigits)
{
	u64 tmp[2 * ndigits];

	switch (ndigits) {
	case 3:
		vli_mmod_fast_192(result, product, curve_prime, tmp);
		break;
	case 4:
		vli_mmod_fast_256(result, product, curve_prime, tmp);
		break;
	default:
		pr_err("unsupports digits size!\n");
		return false;
	}

	return true;
}

/* Computes result = (left * right) % curve_prime. */
static void vli_mod_mult_fast(u64 *result, const u64 *left, const u64 *right,
			      const u64 *curve_prime, unsigned int ndigits)
{
	u64 product[2 * ndigits];

	vli_mult(product, left, right, ndigits);
	vli_mmod_fast(result, product, curve_prime, ndigits);
}

/* Computes result = left^2 % curve_prime. */
static void vli_mod_square_fast(u64 *result, const u64 *left,
				const u64 *curve_prime, unsigned int ndigits)
{
	u64 product[2 * ndigits];

	vli_square(product, left, ndigits);
	vli_mmod_fast(result, product, curve_prime, ndigits);
}

#define EVEN(vli) (!(vli[0] & 1))
/* Computes result = (1 / p_input) % mod. All VLIs are the same size.
 * See "From Euclid's GCD to Montgomery Multiplication to the Great Divide"
 * https://labs.oracle.com/techrep/2001/smli_tr-2001-95.pdf
 */
static void vli_mod_inv(u64 *result, const u64 *input, const u64 *mod,
			unsigned int ndigits)
{
	u64 a[ndigits], b[ndigits];
	u64 u[ndigits], v[ndigits];
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

/* ------ Point operations ------ */

/* Returns true if p_point is the point at infinity, false otherwise. */
static bool ecc_point_is_zero(const struct ecc_point *point)
{
	return (vli_is_zero(point->x, point->ndigits) &&
		vli_is_zero(point->y, point->ndigits));
}

/* Point multiplication algorithm using Montgomery's ladder with co-Z
 * coordinates. From http://eprint.iacr.org/2011/338.pdf
 */

/* Double in place */
static void ecc_point_double_jacobian(u64 *x1, u64 *y1, u64 *z1,
				      u64 *curve_prime, unsigned int ndigits)
{
	/* t1 = x, t2 = y, t3 = z */
	u64 t4[ndigits];
	u64 t5[ndigits];

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
	u64 t1[ndigits];

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
	u64 z[ndigits];

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
	u64 t5[ndigits];

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
	u64 t5[ndigits];
	u64 t6[ndigits];
	u64 t7[ndigits];

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
			   u64 *initial_z, u64 *curve_prime,
			   unsigned int ndigits)
{
	/* R0 and R1 */
	u64 rx[2][ndigits];
	u64 ry[2][ndigits];
	u64 z[ndigits];
	int i, nb;
	int num_bits = vli_num_bits(scalar, ndigits);

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

static inline void ecc_swap_digits(const u64 *in, u64 *out,
				   unsigned int ndigits)
{
	int i;

	for (i = 0; i < ndigits; i++)
		out[i] = __swab64(in[ndigits - 1 - i]);
}

int ecc_is_key_valid(unsigned int curve_id, unsigned int ndigits,
		     const u8 *private_key, unsigned int private_key_len)
{
	int nbytes;
	const struct ecc_curve *curve = ecc_get_curve(curve_id);

	if (!private_key)
		return -EINVAL;

	nbytes = ndigits << ECC_DIGITS_TO_BYTES_SHIFT;

	if (private_key_len != nbytes)
		return -EINVAL;

	if (vli_is_zero((const u64 *)&private_key[0], ndigits))
		return -EINVAL;

	/* Make sure the private key is in the range [1, n-1]. */
	if (vli_cmp(curve->n, (const u64 *)&private_key[0], ndigits) != 1)
		return -EINVAL;

	return 0;
}

int ecdh_make_pub_key(unsigned int curve_id, unsigned int ndigits,
		      const u8 *private_key, unsigned int private_key_len,
		      u8 *public_key, unsigned int public_key_len)
{
	int ret = 0;
	struct ecc_point *pk;
	u64 priv[ndigits];
	unsigned int nbytes;
	const struct ecc_curve *curve = ecc_get_curve(curve_id);

	if (!private_key || !curve) {
		ret = -EINVAL;
		goto out;
	}

	ecc_swap_digits((const u64 *)private_key, priv, ndigits);

	pk = ecc_alloc_point(ndigits);
	if (!pk) {
		ret = -ENOMEM;
		goto out;
	}

	ecc_point_mult(pk, &curve->g, priv, NULL, curve->p, ndigits);
	if (ecc_point_is_zero(pk)) {
		ret = -EAGAIN;
		goto err_free_point;
	}

	nbytes = ndigits << ECC_DIGITS_TO_BYTES_SHIFT;
	ecc_swap_digits(pk->x, (u64 *)public_key, ndigits);
	ecc_swap_digits(pk->y, (u64 *)&public_key[nbytes], ndigits);

err_free_point:
	ecc_free_point(pk);
out:
	return ret;
}

int crypto_ecdh_shared_secret(unsigned int curve_id, unsigned int ndigits,
		       const u8 *private_key, unsigned int private_key_len,
		       const u8 *public_key, unsigned int public_key_len,
		       u8 *secret, unsigned int secret_len)
{
	int ret = 0;
	struct ecc_point *product, *pk;
	u64 priv[ndigits];
	u64 rand_z[ndigits];
	unsigned int nbytes;
	const struct ecc_curve *curve = ecc_get_curve(curve_id);

	if (!private_key || !public_key || !curve) {
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

	product = ecc_alloc_point(ndigits);
	if (!product) {
		ret = -ENOMEM;
		goto err_alloc_product;
	}

	ecc_swap_digits((const u64 *)public_key, pk->x, ndigits);
	ecc_swap_digits((const u64 *)&public_key[nbytes], pk->y, ndigits);
	ecc_swap_digits((const u64 *)private_key, priv, ndigits);

	ecc_point_mult(product, pk, priv, rand_z, curve->p, ndigits);

	ecc_swap_digits(product->x, (u64 *)secret, ndigits);

	if (ecc_point_is_zero(product))
		ret = -EFAULT;

	ecc_free_point(product);
err_alloc_product:
	ecc_free_point(pk);
out:
	return ret;
}
