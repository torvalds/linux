/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CRYTO_ECC_CURVE_DEFS_H
#define _CRYTO_ECC_CURVE_DEFS_H

/* NIST P-192: a = p - 3 */
static u64 nist_p192_g_x[] = { 0xF4FF0AFD82FF1012ull, 0x7CBF20EB43A18800ull,
				0x188DA80EB03090F6ull };
static u64 nist_p192_g_y[] = { 0x73F977A11E794811ull, 0x631011ED6B24CDD5ull,
				0x07192B95FFC8DA78ull };
static u64 nist_p192_p[] = { 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFEull,
				0xFFFFFFFFFFFFFFFFull };
static u64 nist_p192_n[] = { 0x146BC9B1B4D22831ull, 0xFFFFFFFF99DEF836ull,
				0xFFFFFFFFFFFFFFFFull };
static u64 nist_p192_a[] = { 0xFFFFFFFFFFFFFFFCull, 0xFFFFFFFFFFFFFFFEull,
				0xFFFFFFFFFFFFFFFFull };
static u64 nist_p192_b[] = { 0xFEB8DEECC146B9B1ull, 0x0FA7E9AB72243049ull,
				0x64210519E59C80E7ull };
static struct ecc_curve nist_p192 = {
	.name = "nist_192",
	.nbits = 192,
	.g = {
		.x = nist_p192_g_x,
		.y = nist_p192_g_y,
		.ndigits = 3,
	},
	.p = nist_p192_p,
	.n = nist_p192_n,
	.a = nist_p192_a,
	.b = nist_p192_b
};

/* NIST P-256: a = p - 3 */
static u64 nist_p256_g_x[] = { 0xF4A13945D898C296ull, 0x77037D812DEB33A0ull,
				0xF8BCE6E563A440F2ull, 0x6B17D1F2E12C4247ull };
static u64 nist_p256_g_y[] = { 0xCBB6406837BF51F5ull, 0x2BCE33576B315ECEull,
				0x8EE7EB4A7C0F9E16ull, 0x4FE342E2FE1A7F9Bull };
static u64 nist_p256_p[] = { 0xFFFFFFFFFFFFFFFFull, 0x00000000FFFFFFFFull,
				0x0000000000000000ull, 0xFFFFFFFF00000001ull };
static u64 nist_p256_n[] = { 0xF3B9CAC2FC632551ull, 0xBCE6FAADA7179E84ull,
				0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFF00000000ull };
static u64 nist_p256_a[] = { 0xFFFFFFFFFFFFFFFCull, 0x00000000FFFFFFFFull,
				0x0000000000000000ull, 0xFFFFFFFF00000001ull };
static u64 nist_p256_b[] = { 0x3BCE3C3E27D2604Bull, 0x651D06B0CC53B0F6ull,
				0xB3EBBD55769886BCull, 0x5AC635D8AA3A93E7ull };
static struct ecc_curve nist_p256 = {
	.name = "nist_256",
	.nbits = 256,
	.g = {
		.x = nist_p256_g_x,
		.y = nist_p256_g_y,
		.ndigits = 4,
	},
	.p = nist_p256_p,
	.n = nist_p256_n,
	.a = nist_p256_a,
	.b = nist_p256_b
};

/* NIST P-384 */
static u64 nist_p384_g_x[] = { 0x3A545E3872760AB7ull, 0x5502F25DBF55296Cull,
				0x59F741E082542A38ull, 0x6E1D3B628BA79B98ull,
				0x8Eb1C71EF320AD74ull, 0xAA87CA22BE8B0537ull };
static u64 nist_p384_g_y[] = { 0x7A431D7C90EA0E5Full, 0x0A60B1CE1D7E819Dull,
				0xE9DA3113B5F0B8C0ull, 0xF8F41DBD289A147Cull,
				0x5D9E98BF9292DC29ull, 0x3617DE4A96262C6Full };
static u64 nist_p384_p[] = { 0x00000000FFFFFFFFull, 0xFFFFFFFF00000000ull,
				0xFFFFFFFFFFFFFFFEull, 0xFFFFFFFFFFFFFFFFull,
				0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull };
static u64 nist_p384_n[] = { 0xECEC196ACCC52973ull, 0x581A0DB248B0A77Aull,
				0xC7634D81F4372DDFull, 0xFFFFFFFFFFFFFFFFull,
				0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull };
static u64 nist_p384_a[] = { 0x00000000FFFFFFFCull, 0xFFFFFFFF00000000ull,
				0xFFFFFFFFFFFFFFFEull, 0xFFFFFFFFFFFFFFFFull,
				0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull };
static u64 nist_p384_b[] = { 0x2a85c8edd3ec2aefull, 0xc656398d8a2ed19dull,
				0x0314088f5013875aull, 0x181d9c6efe814112ull,
				0x988e056be3f82d19ull, 0xb3312fa7e23ee7e4ull };
static struct ecc_curve nist_p384 = {
	.name = "nist_384",
	.nbits = 384,
	.g = {
		.x = nist_p384_g_x,
		.y = nist_p384_g_y,
		.ndigits = 6,
	},
	.p = nist_p384_p,
	.n = nist_p384_n,
	.a = nist_p384_a,
	.b = nist_p384_b
};

/* NIST P-521 */
static u64 nist_p521_g_x[] = { 0xf97e7e31c2e5bd66ull, 0x3348b3c1856a429bull,
				0xfe1dc127a2ffa8deull, 0xa14b5e77efe75928ull,
				0xf828af606b4d3dbaull, 0x9c648139053fb521ull,
				0x9e3ecb662395b442ull, 0x858e06b70404e9cdull,
				0xc6ull };
static u64 nist_p521_g_y[] = { 0x88be94769fd16650ull, 0x353c7086a272c240ull,
				0xc550b9013fad0761ull, 0x97ee72995ef42640ull,
				0x17afbd17273e662cull, 0x98f54449579b4468ull,
				0x5c8a5fb42c7d1bd9ull, 0x39296a789a3bc004ull,
				0x118ull };
static u64 nist_p521_p[] = { 0xffffffffffffffffull, 0xffffffffffffffffull,
				0xffffffffffffffffull, 0xffffffffffffffffull,
				0xffffffffffffffffull, 0xffffffffffffffffull,
				0xffffffffffffffffull, 0xffffffffffffffffull,
				0x1ffull };
static u64 nist_p521_n[] = { 0xbb6fb71e91386409ull, 0x3bb5c9b8899c47aeull,
				0x7fcc0148f709a5d0ull, 0x51868783bf2f966bull,
				0xfffffffffffffffaull, 0xffffffffffffffffull,
				0xffffffffffffffffull, 0xffffffffffffffffull,
				0x1ffull };
static u64 nist_p521_a[] = { 0xfffffffffffffffcull, 0xffffffffffffffffull,
				0xffffffffffffffffull, 0xffffffffffffffffull,
				0xffffffffffffffffull, 0xffffffffffffffffull,
				0xffffffffffffffffull, 0xffffffffffffffffull,
				0x1ffull };
static u64 nist_p521_b[] = { 0xef451fd46b503f00ull, 0x3573df883d2c34f1ull,
				0x1652c0bd3bb1bf07ull, 0x56193951ec7e937bull,
				0xb8b489918ef109e1ull, 0xa2da725b99b315f3ull,
				0x929a21a0b68540eeull, 0x953eb9618e1c9a1full,
				0x051ull };
static struct ecc_curve nist_p521 = {
	.name = "nist_521",
	.nbits = 521,
	.g = {
		.x = nist_p521_g_x,
		.y = nist_p521_g_y,
		.ndigits = 9,
	},
	.p = nist_p521_p,
	.n = nist_p521_n,
	.a = nist_p521_a,
	.b = nist_p521_b
};

/* curve25519 */
static u64 curve25519_g_x[] = { 0x0000000000000009, 0x0000000000000000,
				0x0000000000000000, 0x0000000000000000 };
static u64 curve25519_p[] = { 0xffffffffffffffed, 0xffffffffffffffff,
				0xffffffffffffffff, 0x7fffffffffffffff };
static u64 curve25519_a[] = { 0x000000000001DB41, 0x0000000000000000,
				0x0000000000000000, 0x0000000000000000 };
static const struct ecc_curve ecc_25519 = {
	.name = "curve25519",
	.nbits = 255,
	.g = {
		.x = curve25519_g_x,
		.ndigits = 4,
	},
	.p = curve25519_p,
	.a = curve25519_a,
};

#endif
