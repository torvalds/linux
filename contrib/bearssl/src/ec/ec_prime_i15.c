/*
 * Copyright (c) 2017 Thomas Pornin <pornin@bolet.org>
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

/*
 * Parameters for supported curves:
 *   - field modulus p
 *   - R^2 mod p (R = 2^(15k) for the smallest k such that R >= p)
 *   - b*R mod p (b is the second curve equation parameter)
 */

static const uint16_t P256_P[] = {
	0x0111,
	0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x003F, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x1000, 0x0000, 0x4000, 0x7FFF,
	0x7FFF, 0x0001
};

static const uint16_t P256_R2[] = {
	0x0111,
	0x0000, 0x6000, 0x0000, 0x0000, 0x0000, 0x0000, 0x7FFC, 0x7FFF,
	0x7FBF, 0x7FFF, 0x7FBF, 0x7FFF, 0x7FFF, 0x7FFF, 0x77FF, 0x7FFF,
	0x4FFF, 0x0000
};

static const uint16_t P256_B[] = {
	0x0111,
	0x770C, 0x5EEF, 0x29C4, 0x3EC4, 0x6273, 0x0486, 0x4543, 0x3993,
	0x3C01, 0x6B56, 0x212E, 0x57EE, 0x4882, 0x204B, 0x7483, 0x3C16,
	0x0187, 0x0000
};

static const uint16_t P384_P[] = {
	0x0199,
	0x7FFF, 0x7FFF, 0x0003, 0x0000, 0x0000, 0x0000, 0x7FC0, 0x7FFF,
	0x7EFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
	0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
	0x7FFF, 0x01FF
};

static const uint16_t P384_R2[] = {
	0x0199,
	0x1000, 0x0000, 0x0000, 0x7FFF, 0x7FFF, 0x0001, 0x0000, 0x0010,
	0x0000, 0x0000, 0x0000, 0x7F00, 0x7FFF, 0x01FF, 0x0000, 0x1000,
	0x0000, 0x2000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000
};

static const uint16_t P384_B[] = {
	0x0199,
	0x7333, 0x2096, 0x70D1, 0x2310, 0x3020, 0x6197, 0x1464, 0x35BB,
	0x70CA, 0x0117, 0x1920, 0x4136, 0x5FC8, 0x5713, 0x4938, 0x7DD2,
	0x4DD2, 0x4A71, 0x0220, 0x683E, 0x2C87, 0x4DB1, 0x7BFF, 0x6C09,
	0x0452, 0x0084
};

static const uint16_t P521_P[] = {
	0x022B,
	0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
	0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
	0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
	0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
	0x7FFF, 0x7FFF, 0x07FF
};

static const uint16_t P521_R2[] = {
	0x022B,
	0x0100, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000
};

static const uint16_t P521_B[] = {
	0x022B,
	0x7002, 0x6A07, 0x751A, 0x228F, 0x71EF, 0x5869, 0x20F4, 0x1EFC,
	0x7357, 0x37E0, 0x4EEC, 0x605E, 0x1652, 0x26F6, 0x31FA, 0x4A8F,
	0x6193, 0x3C2A, 0x3C42, 0x48C7, 0x3489, 0x6771, 0x4C57, 0x5CCD,
	0x2725, 0x545B, 0x503B, 0x5B42, 0x21A0, 0x2534, 0x687E, 0x70E4,
	0x1618, 0x27D7, 0x0465
};

typedef struct {
	const uint16_t *p;
	const uint16_t *b;
	const uint16_t *R2;
	uint16_t p0i;
	size_t point_len;
} curve_params;

static inline const curve_params *
id_to_curve(int curve)
{
	static const curve_params pp[] = {
		{ P256_P, P256_B, P256_R2, 0x0001,  65 },
		{ P384_P, P384_B, P384_R2, 0x0001,  97 },
		{ P521_P, P521_B, P521_R2, 0x0001, 133 }
	};

	return &pp[curve - BR_EC_secp256r1];
}

#define I15_LEN   ((BR_MAX_EC_SIZE + 29) / 15)

/*
 * Type for a point in Jacobian coordinates:
 * -- three values, x, y and z, in Montgomery representation
 * -- affine coordinates are X = x / z^2 and Y = y / z^3
 * -- for the point at infinity, z = 0
 */
typedef struct {
	uint16_t c[3][I15_LEN];
} jacobian;

/*
 * We use a custom interpreter that uses a dozen registers, and
 * only six operations:
 *    MSET(d, a)       copy a into d
 *    MADD(d, a)       d = d+a (modular)
 *    MSUB(d, a)       d = d-a (modular)
 *    MMUL(d, a, b)    d = a*b (Montgomery multiplication)
 *    MINV(d, a, b)    invert d modulo p; a and b are used as scratch registers
 *    MTZ(d)           clear return value if d = 0
 * Destination of MMUL (d) must be distinct from operands (a and b).
 * There is no such constraint for MSUB and MADD.
 *
 * Registers include the operand coordinates, and temporaries.
 */
#define MSET(d, a)      (0x0000 + ((d) << 8) + ((a) << 4))
#define MADD(d, a)      (0x1000 + ((d) << 8) + ((a) << 4))
#define MSUB(d, a)      (0x2000 + ((d) << 8) + ((a) << 4))
#define MMUL(d, a, b)   (0x3000 + ((d) << 8) + ((a) << 4) + (b))
#define MINV(d, a, b)   (0x4000 + ((d) << 8) + ((a) << 4) + (b))
#define MTZ(d)          (0x5000 + ((d) << 8))
#define ENDCODE         0

/*
 * Registers for the input operands.
 */
#define P1x    0
#define P1y    1
#define P1z    2
#define P2x    3
#define P2y    4
#define P2z    5

/*
 * Alternate names for the first input operand.
 */
#define Px     0
#define Py     1
#define Pz     2

/*
 * Temporaries.
 */
#define t1     6
#define t2     7
#define t3     8
#define t4     9
#define t5    10
#define t6    11
#define t7    12

/*
 * Extra scratch registers available when there is no second operand (e.g.
 * for "double" and "affine").
 */
#define t8     3
#define t9     4
#define t10    5

/*
 * Doubling formulas are:
 *
 *   s = 4*x*y^2
 *   m = 3*(x + z^2)*(x - z^2)
 *   x' = m^2 - 2*s
 *   y' = m*(s - x') - 8*y^4
 *   z' = 2*y*z
 *
 * If y = 0 (P has order 2) then this yields infinity (z' = 0), as it
 * should. This case should not happen anyway, because our curves have
 * prime order, and thus do not contain any point of order 2.
 *
 * If P is infinity (z = 0), then again the formulas yield infinity,
 * which is correct. Thus, this code works for all points.
 *
 * Cost: 8 multiplications
 */
static const uint16_t code_double[] = {
	/*
	 * Compute z^2 (in t1).
	 */
	MMUL(t1, Pz, Pz),

	/*
	 * Compute x-z^2 (in t2) and then x+z^2 (in t1).
	 */
	MSET(t2, Px),
	MSUB(t2, t1),
	MADD(t1, Px),

	/*
	 * Compute m = 3*(x+z^2)*(x-z^2) (in t1).
	 */
	MMUL(t3, t1, t2),
	MSET(t1, t3),
	MADD(t1, t3),
	MADD(t1, t3),

	/*
	 * Compute s = 4*x*y^2 (in t2) and 2*y^2 (in t3).
	 */
	MMUL(t3, Py, Py),
	MADD(t3, t3),
	MMUL(t2, Px, t3),
	MADD(t2, t2),

	/*
	 * Compute x' = m^2 - 2*s.
	 */
	MMUL(Px, t1, t1),
	MSUB(Px, t2),
	MSUB(Px, t2),

	/*
	 * Compute z' = 2*y*z.
	 */
	MMUL(t4, Py, Pz),
	MSET(Pz, t4),
	MADD(Pz, t4),

	/*
	 * Compute y' = m*(s - x') - 8*y^4. Note that we already have
	 * 2*y^2 in t3.
	 */
	MSUB(t2, Px),
	MMUL(Py, t1, t2),
	MMUL(t4, t3, t3),
	MSUB(Py, t4),
	MSUB(Py, t4),

	ENDCODE
};

/*
 * Addtions formulas are:
 *
 *   u1 = x1 * z2^2
 *   u2 = x2 * z1^2
 *   s1 = y1 * z2^3
 *   s2 = y2 * z1^3
 *   h = u2 - u1
 *   r = s2 - s1
 *   x3 = r^2 - h^3 - 2 * u1 * h^2
 *   y3 = r * (u1 * h^2 - x3) - s1 * h^3
 *   z3 = h * z1 * z2
 *
 * If both P1 and P2 are infinity, then z1 == 0 and z2 == 0, implying that
 * z3 == 0, so the result is correct.
 * If either of P1 or P2 is infinity, but not both, then z3 == 0, which is
 * not correct.
 * h == 0 only if u1 == u2; this happens in two cases:
 * -- if s1 == s2 then P1 and/or P2 is infinity, or P1 == P2
 * -- if s1 != s2 then P1 + P2 == infinity (but neither P1 or P2 is infinity)
 *
 * Thus, the following situations are not handled correctly:
 * -- P1 = 0 and P2 != 0
 * -- P1 != 0 and P2 = 0
 * -- P1 = P2
 * All other cases are properly computed. However, even in "incorrect"
 * situations, the three coordinates still are properly formed field
 * elements.
 *
 * The returned flag is cleared if r == 0. This happens in the following
 * cases:
 * -- Both points are on the same horizontal line (same Y coordinate).
 * -- Both points are infinity.
 * -- One point is infinity and the other is on line Y = 0.
 * The third case cannot happen with our curves (there is no valid point
 * on line Y = 0 since that would be a point of order 2). If the two
 * source points are non-infinity, then remains only the case where the
 * two points are on the same horizontal line.
 *
 * This allows us to detect the "P1 == P2" case, assuming that P1 != 0 and
 * P2 != 0:
 * -- If the returned value is not the point at infinity, then it was properly
 * computed.
 * -- Otherwise, if the returned flag is 1, then P1+P2 = 0, and the result
 * is indeed the point at infinity.
 * -- Otherwise (result is infinity, flag is 0), then P1 = P2 and we should
 * use the 'double' code.
 *
 * Cost: 16 multiplications
 */
static const uint16_t code_add[] = {
	/*
	 * Compute u1 = x1*z2^2 (in t1) and s1 = y1*z2^3 (in t3).
	 */
	MMUL(t3, P2z, P2z),
	MMUL(t1, P1x, t3),
	MMUL(t4, P2z, t3),
	MMUL(t3, P1y, t4),

	/*
	 * Compute u2 = x2*z1^2 (in t2) and s2 = y2*z1^3 (in t4).
	 */
	MMUL(t4, P1z, P1z),
	MMUL(t2, P2x, t4),
	MMUL(t5, P1z, t4),
	MMUL(t4, P2y, t5),

	/*
	 * Compute h = u2 - u1 (in t2) and r = s2 - s1 (in t4).
	 */
	MSUB(t2, t1),
	MSUB(t4, t3),

	/*
	 * Report cases where r = 0 through the returned flag.
	 */
	MTZ(t4),

	/*
	 * Compute u1*h^2 (in t6) and h^3 (in t5).
	 */
	MMUL(t7, t2, t2),
	MMUL(t6, t1, t7),
	MMUL(t5, t7, t2),

	/*
	 * Compute x3 = r^2 - h^3 - 2*u1*h^2.
	 * t1 and t7 can be used as scratch registers.
	 */
	MMUL(P1x, t4, t4),
	MSUB(P1x, t5),
	MSUB(P1x, t6),
	MSUB(P1x, t6),

	/*
	 * Compute y3 = r*(u1*h^2 - x3) - s1*h^3.
	 */
	MSUB(t6, P1x),
	MMUL(P1y, t4, t6),
	MMUL(t1, t5, t3),
	MSUB(P1y, t1),

	/*
	 * Compute z3 = h*z1*z2.
	 */
	MMUL(t1, P1z, P2z),
	MMUL(P1z, t1, t2),

	ENDCODE
};

/*
 * Check that the point is on the curve. This code snippet assumes the
 * following conventions:
 * -- Coordinates x and y have been freshly decoded in P1 (but not
 * converted to Montgomery coordinates yet).
 * -- P2x, P2y and P2z are set to, respectively, R^2, b*R and 1.
 */
static const uint16_t code_check[] = {

	/* Convert x and y to Montgomery representation. */
	MMUL(t1, P1x, P2x),
	MMUL(t2, P1y, P2x),
	MSET(P1x, t1),
	MSET(P1y, t2),

	/* Compute x^3 in t1. */
	MMUL(t2, P1x, P1x),
	MMUL(t1, P1x, t2),

	/* Subtract 3*x from t1. */
	MSUB(t1, P1x),
	MSUB(t1, P1x),
	MSUB(t1, P1x),

	/* Add b. */
	MADD(t1, P2y),

	/* Compute y^2 in t2. */
	MMUL(t2, P1y, P1y),

	/* Compare y^2 with x^3 - 3*x + b; they must match. */
	MSUB(t1, t2),
	MTZ(t1),

	/* Set z to 1 (in Montgomery representation). */
	MMUL(P1z, P2x, P2z),

	ENDCODE
};

/*
 * Conversion back to affine coordinates. This code snippet assumes that
 * the z coordinate of P2 is set to 1 (not in Montgomery representation).
 */
static const uint16_t code_affine[] = {

	/* Save z*R in t1. */
	MSET(t1, P1z),

	/* Compute z^3 in t2. */
	MMUL(t2, P1z, P1z),
	MMUL(t3, P1z, t2),
	MMUL(t2, t3, P2z),

	/* Invert to (1/z^3) in t2. */
	MINV(t2, t3, t4),

	/* Compute y. */
	MSET(t3, P1y),
	MMUL(P1y, t2, t3),

	/* Compute (1/z^2) in t3. */
	MMUL(t3, t2, t1),

	/* Compute x. */
	MSET(t2, P1x),
	MMUL(P1x, t2, t3),

	ENDCODE
};

static uint32_t
run_code(jacobian *P1, const jacobian *P2,
	const curve_params *cc, const uint16_t *code)
{
	uint32_t r;
	uint16_t t[13][I15_LEN];
	size_t u;

	r = 1;

	/*
	 * Copy the two operands in the dedicated registers.
	 */
	memcpy(t[P1x], P1->c, 3 * I15_LEN * sizeof(uint16_t));
	memcpy(t[P2x], P2->c, 3 * I15_LEN * sizeof(uint16_t));

	/*
	 * Run formulas.
	 */
	for (u = 0;; u ++) {
		unsigned op, d, a, b;

		op = code[u];
		if (op == 0) {
			break;
		}
		d = (op >> 8) & 0x0F;
		a = (op >> 4) & 0x0F;
		b = op & 0x0F;
		op >>= 12;
		switch (op) {
			uint32_t ctl;
			size_t plen;
			unsigned char tp[(BR_MAX_EC_SIZE + 7) >> 3];

		case 0:
			memcpy(t[d], t[a], I15_LEN * sizeof(uint16_t));
			break;
		case 1:
			ctl = br_i15_add(t[d], t[a], 1);
			ctl |= NOT(br_i15_sub(t[d], cc->p, 0));
			br_i15_sub(t[d], cc->p, ctl);
			break;
		case 2:
			br_i15_add(t[d], cc->p, br_i15_sub(t[d], t[a], 1));
			break;
		case 3:
			br_i15_montymul(t[d], t[a], t[b], cc->p, cc->p0i);
			break;
		case 4:
			plen = (cc->p[0] - (cc->p[0] >> 4) + 7) >> 3;
			br_i15_encode(tp, plen, cc->p);
			tp[plen - 1] -= 2;
			br_i15_modpow(t[d], tp, plen,
				cc->p, cc->p0i, t[a], t[b]);
			break;
		default:
			r &= ~br_i15_iszero(t[d]);
			break;
		}
	}

	/*
	 * Copy back result.
	 */
	memcpy(P1->c, t[P1x], 3 * I15_LEN * sizeof(uint16_t));
	return r;
}

static void
set_one(uint16_t *x, const uint16_t *p)
{
	size_t plen;

	plen = (p[0] + 31) >> 4;
	memset(x, 0, plen * sizeof *x);
	x[0] = p[0];
	x[1] = 0x0001;
}

static void
point_zero(jacobian *P, const curve_params *cc)
{
	memset(P, 0, sizeof *P);
	P->c[0][0] = P->c[1][0] = P->c[2][0] = cc->p[0];
}

static inline void
point_double(jacobian *P, const curve_params *cc)
{
	run_code(P, P, cc, code_double);
}

static inline uint32_t
point_add(jacobian *P1, const jacobian *P2, const curve_params *cc)
{
	return run_code(P1, P2, cc, code_add);
}

static void
point_mul(jacobian *P, const unsigned char *x, size_t xlen,
	const curve_params *cc)
{
	/*
	 * We do a simple double-and-add ladder with a 2-bit window
	 * to make only one add every two doublings. We thus first
	 * precompute 2P and 3P in some local buffers.
	 *
	 * We always perform two doublings and one addition; the
	 * addition is with P, 2P and 3P and is done in a temporary
	 * array.
	 *
	 * The addition code cannot handle cases where one of the
	 * operands is infinity, which is the case at the start of the
	 * ladder. We therefore need to maintain a flag that controls
	 * this situation.
	 */
	uint32_t qz;
	jacobian P2, P3, Q, T, U;

	memcpy(&P2, P, sizeof P2);
	point_double(&P2, cc);
	memcpy(&P3, P, sizeof P3);
	point_add(&P3, &P2, cc);

	point_zero(&Q, cc);
	qz = 1;
	while (xlen -- > 0) {
		int k;

		for (k = 6; k >= 0; k -= 2) {
			uint32_t bits;
			uint32_t bnz;

			point_double(&Q, cc);
			point_double(&Q, cc);
			memcpy(&T, P, sizeof T);
			memcpy(&U, &Q, sizeof U);
			bits = (*x >> k) & (uint32_t)3;
			bnz = NEQ(bits, 0);
			CCOPY(EQ(bits, 2), &T, &P2, sizeof T);
			CCOPY(EQ(bits, 3), &T, &P3, sizeof T);
			point_add(&U, &T, cc);
			CCOPY(bnz & qz, &Q, &T, sizeof Q);
			CCOPY(bnz & ~qz, &Q, &U, sizeof Q);
			qz &= ~bnz;
		}
		x ++;
	}
	memcpy(P, &Q, sizeof Q);
}

/*
 * Decode point into Jacobian coordinates. This function does not support
 * the point at infinity. If the point is invalid then this returns 0, but
 * the coordinates are still set to properly formed field elements.
 */
static uint32_t
point_decode(jacobian *P, const void *src, size_t len, const curve_params *cc)
{
	/*
	 * Points must use uncompressed format:
	 * -- first byte is 0x04;
	 * -- coordinates X and Y use unsigned big-endian, with the same
	 *    length as the field modulus.
	 *
	 * We don't support hybrid format (uncompressed, but first byte
	 * has value 0x06 or 0x07, depending on the least significant bit
	 * of Y) because it is rather useless, and explicitly forbidden
	 * by PKIX (RFC 5480, section 2.2).
	 *
	 * We don't support compressed format either, because it is not
	 * much used in practice (there are or were patent-related
	 * concerns about point compression, which explains the lack of
	 * generalised support). Also, point compression support would
	 * need a bit more code.
	 */
	const unsigned char *buf;
	size_t plen, zlen;
	uint32_t r;
	jacobian Q;

	buf = src;
	point_zero(P, cc);
	plen = (cc->p[0] - (cc->p[0] >> 4) + 7) >> 3;
	if (len != 1 + (plen << 1)) {
		return 0;
	}
	r = br_i15_decode_mod(P->c[0], buf + 1, plen, cc->p);
	r &= br_i15_decode_mod(P->c[1], buf + 1 + plen, plen, cc->p);

	/*
	 * Check first byte.
	 */
	r &= EQ(buf[0], 0x04);
	/* obsolete
	r &= EQ(buf[0], 0x04) | (EQ(buf[0] & 0xFE, 0x06)
		& ~(uint32_t)(buf[0] ^ buf[plen << 1]));
	*/

	/*
	 * Convert coordinates and check that the point is valid.
	 */
	zlen = ((cc->p[0] + 31) >> 4) * sizeof(uint16_t);
	memcpy(Q.c[0], cc->R2, zlen);
	memcpy(Q.c[1], cc->b, zlen);
	set_one(Q.c[2], cc->p);
	r &= ~run_code(P, &Q, cc, code_check);
	return r;
}

/*
 * Encode a point. This method assumes that the point is correct and is
 * not the point at infinity. Encoded size is always 1+2*plen, where
 * plen is the field modulus length, in bytes.
 */
static void
point_encode(void *dst, const jacobian *P, const curve_params *cc)
{
	unsigned char *buf;
	size_t plen;
	jacobian Q, T;

	buf = dst;
	plen = (cc->p[0] - (cc->p[0] >> 4) + 7) >> 3;
	buf[0] = 0x04;
	memcpy(&Q, P, sizeof *P);
	set_one(T.c[2], cc->p);
	run_code(&Q, &T, cc, code_affine);
	br_i15_encode(buf + 1, plen, Q.c[0]);
	br_i15_encode(buf + 1 + plen, plen, Q.c[1]);
}

static const br_ec_curve_def *
id_to_curve_def(int curve)
{
	switch (curve) {
	case BR_EC_secp256r1:
		return &br_secp256r1;
	case BR_EC_secp384r1:
		return &br_secp384r1;
	case BR_EC_secp521r1:
		return &br_secp521r1;
	}
	return NULL;
}

static const unsigned char *
api_generator(int curve, size_t *len)
{
	const br_ec_curve_def *cd;

	cd = id_to_curve_def(curve);
	*len = cd->generator_len;
	return cd->generator;
}

static const unsigned char *
api_order(int curve, size_t *len)
{
	const br_ec_curve_def *cd;

	cd = id_to_curve_def(curve);
	*len = cd->order_len;
	return cd->order;
}

static size_t
api_xoff(int curve, size_t *len)
{
	api_generator(curve, len);
	*len >>= 1;
	return 1;
}

static uint32_t
api_mul(unsigned char *G, size_t Glen,
	const unsigned char *x, size_t xlen, int curve)
{
	uint32_t r;
	const curve_params *cc;
	jacobian P;

	cc = id_to_curve(curve);
	r = point_decode(&P, G, Glen, cc);
	point_mul(&P, x, xlen, cc);
	if (Glen == cc->point_len) {
		point_encode(G, &P, cc);
	}
	return r;
}

static size_t
api_mulgen(unsigned char *R,
	const unsigned char *x, size_t xlen, int curve)
{
	const unsigned char *G;
	size_t Glen;

	G = api_generator(curve, &Glen);
	memcpy(R, G, Glen);
	api_mul(R, Glen, x, xlen, curve);
	return Glen;
}

static uint32_t
api_muladd(unsigned char *A, const unsigned char *B, size_t len,
	const unsigned char *x, size_t xlen,
	const unsigned char *y, size_t ylen, int curve)
{
	uint32_t r, t, z;
	const curve_params *cc;
	jacobian P, Q;

	/*
	 * TODO: see about merging the two ladders. Right now, we do
	 * two independent point multiplications, which is a bit
	 * wasteful of CPU resources (but yields short code).
	 */

	cc = id_to_curve(curve);
	r = point_decode(&P, A, len, cc);
	if (B == NULL) {
		size_t Glen;

		B = api_generator(curve, &Glen);
	}
	r &= point_decode(&Q, B, len, cc);
	point_mul(&P, x, xlen, cc);
	point_mul(&Q, y, ylen, cc);

	/*
	 * We want to compute P+Q. Since the base points A and B are distinct
	 * from infinity, and the multipliers are non-zero and lower than the
	 * curve order, then we know that P and Q are non-infinity. This
	 * leaves two special situations to test for:
	 * -- If P = Q then we must use point_double().
	 * -- If P+Q = 0 then we must report an error.
	 */
	t = point_add(&P, &Q, cc);
	point_double(&Q, cc);
	z = br_i15_iszero(P.c[2]);

	/*
	 * If z is 1 then either P+Q = 0 (t = 1) or P = Q (t = 0). So we
	 * have the following:
	 *
	 *   z = 0, t = 0   return P (normal addition)
	 *   z = 0, t = 1   return P (normal addition)
	 *   z = 1, t = 0   return Q (a 'double' case)
	 *   z = 1, t = 1   report an error (P+Q = 0)
	 */
	CCOPY(z & ~t, &P, &Q, sizeof Q);
	point_encode(A, &P, cc);
	r &= ~(z & t);

	return r;
}

/* see bearssl_ec.h */
const br_ec_impl br_ec_prime_i15 = {
	(uint32_t)0x03800000,
	&api_generator,
	&api_order,
	&api_xoff,
	&api_mul,
	&api_mulgen,
	&api_muladd
};
