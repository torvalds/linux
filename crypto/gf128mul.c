/* gf128mul.c - GF(2^128) multiplication functions
 *
 * Copyright (c) 2003, Dr Brian Gladman, Worcester, UK.
 * Copyright (c) 2006, Rik Snel <rsnel@cube.dyndns.org>
 *
 * Based on Dr Brian Gladman's (GPL'd) work published at
 * http://gladman.plushost.co.uk/oldsite/cryptography_technology/index.php
 * See the original copyright notice below.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/*
 ---------------------------------------------------------------------------
 Copyright (c) 2003, Dr Brian Gladman, Worcester, UK.   All rights reserved.

 LICENSE TERMS

 The free distribution and use of this software in both source and binary
 form is allowed (with or without changes) provided that:

   1. distributions of this source code include the above copyright
      notice, this list of conditions and the following disclaimer;

   2. distributions in binary form include the above copyright
      notice, this list of conditions and the following disclaimer
      in the documentation and/or other associated materials;

   3. the copyright holder's name is not used to endorse products
      built using this software without specific written permission.

 ALTERNATIVELY, provided that this notice is retained in full, this product
 may be distributed under the terms of the GNU General Public License (GPL),
 in which case the provisions of the GPL apply INSTEAD OF those given above.

 DISCLAIMER

 This software is provided 'as is' with no explicit or implied warranties
 in respect of its properties, including, but not limited to, correctness
 and/or fitness for purpose.
 ---------------------------------------------------------------------------
 Issue 31/01/2006

 This file provides fast multiplication in GF(2^128) as required by several
 cryptographic authentication modes
*/

#include <crypto/gf128mul.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#define gf128mul_dat(q) { \
	q(0x00), q(0x01), q(0x02), q(0x03), q(0x04), q(0x05), q(0x06), q(0x07),\
	q(0x08), q(0x09), q(0x0a), q(0x0b), q(0x0c), q(0x0d), q(0x0e), q(0x0f),\
	q(0x10), q(0x11), q(0x12), q(0x13), q(0x14), q(0x15), q(0x16), q(0x17),\
	q(0x18), q(0x19), q(0x1a), q(0x1b), q(0x1c), q(0x1d), q(0x1e), q(0x1f),\
	q(0x20), q(0x21), q(0x22), q(0x23), q(0x24), q(0x25), q(0x26), q(0x27),\
	q(0x28), q(0x29), q(0x2a), q(0x2b), q(0x2c), q(0x2d), q(0x2e), q(0x2f),\
	q(0x30), q(0x31), q(0x32), q(0x33), q(0x34), q(0x35), q(0x36), q(0x37),\
	q(0x38), q(0x39), q(0x3a), q(0x3b), q(0x3c), q(0x3d), q(0x3e), q(0x3f),\
	q(0x40), q(0x41), q(0x42), q(0x43), q(0x44), q(0x45), q(0x46), q(0x47),\
	q(0x48), q(0x49), q(0x4a), q(0x4b), q(0x4c), q(0x4d), q(0x4e), q(0x4f),\
	q(0x50), q(0x51), q(0x52), q(0x53), q(0x54), q(0x55), q(0x56), q(0x57),\
	q(0x58), q(0x59), q(0x5a), q(0x5b), q(0x5c), q(0x5d), q(0x5e), q(0x5f),\
	q(0x60), q(0x61), q(0x62), q(0x63), q(0x64), q(0x65), q(0x66), q(0x67),\
	q(0x68), q(0x69), q(0x6a), q(0x6b), q(0x6c), q(0x6d), q(0x6e), q(0x6f),\
	q(0x70), q(0x71), q(0x72), q(0x73), q(0x74), q(0x75), q(0x76), q(0x77),\
	q(0x78), q(0x79), q(0x7a), q(0x7b), q(0x7c), q(0x7d), q(0x7e), q(0x7f),\
	q(0x80), q(0x81), q(0x82), q(0x83), q(0x84), q(0x85), q(0x86), q(0x87),\
	q(0x88), q(0x89), q(0x8a), q(0x8b), q(0x8c), q(0x8d), q(0x8e), q(0x8f),\
	q(0x90), q(0x91), q(0x92), q(0x93), q(0x94), q(0x95), q(0x96), q(0x97),\
	q(0x98), q(0x99), q(0x9a), q(0x9b), q(0x9c), q(0x9d), q(0x9e), q(0x9f),\
	q(0xa0), q(0xa1), q(0xa2), q(0xa3), q(0xa4), q(0xa5), q(0xa6), q(0xa7),\
	q(0xa8), q(0xa9), q(0xaa), q(0xab), q(0xac), q(0xad), q(0xae), q(0xaf),\
	q(0xb0), q(0xb1), q(0xb2), q(0xb3), q(0xb4), q(0xb5), q(0xb6), q(0xb7),\
	q(0xb8), q(0xb9), q(0xba), q(0xbb), q(0xbc), q(0xbd), q(0xbe), q(0xbf),\
	q(0xc0), q(0xc1), q(0xc2), q(0xc3), q(0xc4), q(0xc5), q(0xc6), q(0xc7),\
	q(0xc8), q(0xc9), q(0xca), q(0xcb), q(0xcc), q(0xcd), q(0xce), q(0xcf),\
	q(0xd0), q(0xd1), q(0xd2), q(0xd3), q(0xd4), q(0xd5), q(0xd6), q(0xd7),\
	q(0xd8), q(0xd9), q(0xda), q(0xdb), q(0xdc), q(0xdd), q(0xde), q(0xdf),\
	q(0xe0), q(0xe1), q(0xe2), q(0xe3), q(0xe4), q(0xe5), q(0xe6), q(0xe7),\
	q(0xe8), q(0xe9), q(0xea), q(0xeb), q(0xec), q(0xed), q(0xee), q(0xef),\
	q(0xf0), q(0xf1), q(0xf2), q(0xf3), q(0xf4), q(0xf5), q(0xf6), q(0xf7),\
	q(0xf8), q(0xf9), q(0xfa), q(0xfb), q(0xfc), q(0xfd), q(0xfe), q(0xff) \
}

/*
 * Given a value i in 0..255 as the byte overflow when a field element
 * in GF(2^128) is multiplied by x^8, the following macro returns the
 * 16-bit value that must be XOR-ed into the low-degree end of the
 * product to reduce it modulo the polynomial x^128 + x^7 + x^2 + x + 1.
 *
 * There are two versions of the macro, and hence two tables: one for
 * the "be" convention where the highest-order bit is the coefficient of
 * the highest-degree polynomial term, and one for the "le" convention
 * where the highest-order bit is the coefficient of the lowest-degree
 * polynomial term.  In both cases the values are stored in CPU byte
 * endianness such that the coefficients are ordered consistently across
 * bytes, i.e. in the "be" table bits 15..0 of the stored value
 * correspond to the coefficients of x^15..x^0, and in the "le" table
 * bits 15..0 correspond to the coefficients of x^0..x^15.
 *
 * Therefore, provided that the appropriate byte endianness conversions
 * are done by the multiplication functions (and these must be in place
 * anyway to support both little endian and big endian CPUs), the "be"
 * table can be used for multiplications of both "bbe" and "ble"
 * elements, and the "le" table can be used for multiplications of both
 * "lle" and "lbe" elements.
 */

#define xda_be(i) ( \
	(i & 0x80 ? 0x4380 : 0) ^ (i & 0x40 ? 0x21c0 : 0) ^ \
	(i & 0x20 ? 0x10e0 : 0) ^ (i & 0x10 ? 0x0870 : 0) ^ \
	(i & 0x08 ? 0x0438 : 0) ^ (i & 0x04 ? 0x021c : 0) ^ \
	(i & 0x02 ? 0x010e : 0) ^ (i & 0x01 ? 0x0087 : 0) \
)

#define xda_le(i) ( \
	(i & 0x80 ? 0xe100 : 0) ^ (i & 0x40 ? 0x7080 : 0) ^ \
	(i & 0x20 ? 0x3840 : 0) ^ (i & 0x10 ? 0x1c20 : 0) ^ \
	(i & 0x08 ? 0x0e10 : 0) ^ (i & 0x04 ? 0x0708 : 0) ^ \
	(i & 0x02 ? 0x0384 : 0) ^ (i & 0x01 ? 0x01c2 : 0) \
)

static const u16 gf128mul_table_le[256] = gf128mul_dat(xda_le);
static const u16 gf128mul_table_be[256] = gf128mul_dat(xda_be);

/*
 * The following functions multiply a field element by x^8 in
 * the polynomial field representation.  They use 64-bit word operations
 * to gain speed but compensate for machine endianness and hence work
 * correctly on both styles of machine.
 */

static void gf128mul_x8_lle(be128 *x)
{
	u64 a = be64_to_cpu(x->a);
	u64 b = be64_to_cpu(x->b);
	u64 _tt = gf128mul_table_le[b & 0xff];

	x->b = cpu_to_be64((b >> 8) | (a << 56));
	x->a = cpu_to_be64((a >> 8) ^ (_tt << 48));
}

static void gf128mul_x8_bbe(be128 *x)
{
	u64 a = be64_to_cpu(x->a);
	u64 b = be64_to_cpu(x->b);
	u64 _tt = gf128mul_table_be[a >> 56];

	x->a = cpu_to_be64((a << 8) | (b >> 56));
	x->b = cpu_to_be64((b << 8) ^ _tt);
}

void gf128mul_x8_ble(le128 *r, const le128 *x)
{
	u64 a = le64_to_cpu(x->a);
	u64 b = le64_to_cpu(x->b);
	u64 _tt = gf128mul_table_be[a >> 56];

	r->a = cpu_to_le64((a << 8) | (b >> 56));
	r->b = cpu_to_le64((b << 8) ^ _tt);
}
EXPORT_SYMBOL(gf128mul_x8_ble);

void gf128mul_lle(be128 *r, const be128 *b)
{
	be128 p[8];
	int i;

	p[0] = *r;
	for (i = 0; i < 7; ++i)
		gf128mul_x_lle(&p[i + 1], &p[i]);

	memset(r, 0, sizeof(*r));
	for (i = 0;;) {
		u8 ch = ((u8 *)b)[15 - i];

		if (ch & 0x80)
			be128_xor(r, r, &p[0]);
		if (ch & 0x40)
			be128_xor(r, r, &p[1]);
		if (ch & 0x20)
			be128_xor(r, r, &p[2]);
		if (ch & 0x10)
			be128_xor(r, r, &p[3]);
		if (ch & 0x08)
			be128_xor(r, r, &p[4]);
		if (ch & 0x04)
			be128_xor(r, r, &p[5]);
		if (ch & 0x02)
			be128_xor(r, r, &p[6]);
		if (ch & 0x01)
			be128_xor(r, r, &p[7]);

		if (++i >= 16)
			break;

		gf128mul_x8_lle(r);
	}
}
EXPORT_SYMBOL(gf128mul_lle);

void gf128mul_bbe(be128 *r, const be128 *b)
{
	be128 p[8];
	int i;

	p[0] = *r;
	for (i = 0; i < 7; ++i)
		gf128mul_x_bbe(&p[i + 1], &p[i]);

	memset(r, 0, sizeof(*r));
	for (i = 0;;) {
		u8 ch = ((u8 *)b)[i];

		if (ch & 0x80)
			be128_xor(r, r, &p[7]);
		if (ch & 0x40)
			be128_xor(r, r, &p[6]);
		if (ch & 0x20)
			be128_xor(r, r, &p[5]);
		if (ch & 0x10)
			be128_xor(r, r, &p[4]);
		if (ch & 0x08)
			be128_xor(r, r, &p[3]);
		if (ch & 0x04)
			be128_xor(r, r, &p[2]);
		if (ch & 0x02)
			be128_xor(r, r, &p[1]);
		if (ch & 0x01)
			be128_xor(r, r, &p[0]);

		if (++i >= 16)
			break;

		gf128mul_x8_bbe(r);
	}
}
EXPORT_SYMBOL(gf128mul_bbe);

/*      This version uses 64k bytes of table space.
    A 16 byte buffer has to be multiplied by a 16 byte key
    value in GF(2^128).  If we consider a GF(2^128) value in
    the buffer's lowest byte, we can construct a table of
    the 256 16 byte values that result from the 256 values
    of this byte.  This requires 4096 bytes. But we also
    need tables for each of the 16 higher bytes in the
    buffer as well, which makes 64 kbytes in total.
*/
/* additional explanation
 * t[0][BYTE] contains g*BYTE
 * t[1][BYTE] contains g*x^8*BYTE
 *  ..
 * t[15][BYTE] contains g*x^120*BYTE */
struct gf128mul_64k *gf128mul_init_64k_bbe(const be128 *g)
{
	struct gf128mul_64k *t;
	int i, j, k;

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		goto out;

	for (i = 0; i < 16; i++) {
		t->t[i] = kzalloc(sizeof(*t->t[i]), GFP_KERNEL);
		if (!t->t[i]) {
			gf128mul_free_64k(t);
			t = NULL;
			goto out;
		}
	}

	t->t[0]->t[1] = *g;
	for (j = 1; j <= 64; j <<= 1)
		gf128mul_x_bbe(&t->t[0]->t[j + j], &t->t[0]->t[j]);

	for (i = 0;;) {
		for (j = 2; j < 256; j += j)
			for (k = 1; k < j; ++k)
				be128_xor(&t->t[i]->t[j + k],
					  &t->t[i]->t[j], &t->t[i]->t[k]);

		if (++i >= 16)
			break;

		for (j = 128; j > 0; j >>= 1) {
			t->t[i]->t[j] = t->t[i - 1]->t[j];
			gf128mul_x8_bbe(&t->t[i]->t[j]);
		}
	}

out:
	return t;
}
EXPORT_SYMBOL(gf128mul_init_64k_bbe);

void gf128mul_free_64k(struct gf128mul_64k *t)
{
	int i;

	for (i = 0; i < 16; i++)
		kfree_sensitive(t->t[i]);
	kfree_sensitive(t);
}
EXPORT_SYMBOL(gf128mul_free_64k);

void gf128mul_64k_bbe(be128 *a, const struct gf128mul_64k *t)
{
	u8 *ap = (u8 *)a;
	be128 r[1];
	int i;

	*r = t->t[0]->t[ap[15]];
	for (i = 1; i < 16; ++i)
		be128_xor(r, r, &t->t[i]->t[ap[15 - i]]);
	*a = *r;
}
EXPORT_SYMBOL(gf128mul_64k_bbe);

/*      This version uses 4k bytes of table space.
    A 16 byte buffer has to be multiplied by a 16 byte key
    value in GF(2^128).  If we consider a GF(2^128) value in a
    single byte, we can construct a table of the 256 16 byte
    values that result from the 256 values of this byte.
    This requires 4096 bytes. If we take the highest byte in
    the buffer and use this table to get the result, we then
    have to multiply by x^120 to get the final value. For the
    next highest byte the result has to be multiplied by x^112
    and so on. But we can do this by accumulating the result
    in an accumulator starting with the result for the top
    byte.  We repeatedly multiply the accumulator value by
    x^8 and then add in (i.e. xor) the 16 bytes of the next
    lower byte in the buffer, stopping when we reach the
    lowest byte. This requires a 4096 byte table.
*/
struct gf128mul_4k *gf128mul_init_4k_lle(const be128 *g)
{
	struct gf128mul_4k *t;
	int j, k;

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		goto out;

	t->t[128] = *g;
	for (j = 64; j > 0; j >>= 1)
		gf128mul_x_lle(&t->t[j], &t->t[j+j]);

	for (j = 2; j < 256; j += j)
		for (k = 1; k < j; ++k)
			be128_xor(&t->t[j + k], &t->t[j], &t->t[k]);

out:
	return t;
}
EXPORT_SYMBOL(gf128mul_init_4k_lle);

struct gf128mul_4k *gf128mul_init_4k_bbe(const be128 *g)
{
	struct gf128mul_4k *t;
	int j, k;

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		goto out;

	t->t[1] = *g;
	for (j = 1; j <= 64; j <<= 1)
		gf128mul_x_bbe(&t->t[j + j], &t->t[j]);

	for (j = 2; j < 256; j += j)
		for (k = 1; k < j; ++k)
			be128_xor(&t->t[j + k], &t->t[j], &t->t[k]);

out:
	return t;
}
EXPORT_SYMBOL(gf128mul_init_4k_bbe);

void gf128mul_4k_lle(be128 *a, const struct gf128mul_4k *t)
{
	u8 *ap = (u8 *)a;
	be128 r[1];
	int i = 15;

	*r = t->t[ap[15]];
	while (i--) {
		gf128mul_x8_lle(r);
		be128_xor(r, r, &t->t[ap[i]]);
	}
	*a = *r;
}
EXPORT_SYMBOL(gf128mul_4k_lle);

void gf128mul_4k_bbe(be128 *a, const struct gf128mul_4k *t)
{
	u8 *ap = (u8 *)a;
	be128 r[1];
	int i = 0;

	*r = t->t[ap[0]];
	while (++i < 16) {
		gf128mul_x8_bbe(r);
		be128_xor(r, r, &t->t[ap[i]]);
	}
	*a = *r;
}
EXPORT_SYMBOL(gf128mul_4k_bbe);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Functions for multiplying elements of GF(2^128)");
