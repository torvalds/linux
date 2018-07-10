/*
 * Copyright (C) 2017 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

/* This AES implementation is derived from tiny-AES128-C code,
 * which was put by its author into public domain:
 *
 * tiny-AES128-C/unlicense.txt, Dec 8, 2014
 * """
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 * """
 */
/* Note that only original tiny-AES128-C code is public domain.
 * The derived code in this file has been expanded to also implement aes192
 * and aes256 and use more efficient word-sized operations in many places,
 * and put under GPLv2 license.
 */
#include "tls.h"

// The lookup-tables are marked const so they can be placed in read-only storage instead of RAM
// The numbers below can be computed dynamically trading ROM for RAM -
// This can be useful in (embedded) bootloader applications, where ROM is often limited.
static const uint8_t sbox[] = {
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
	0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
	0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	0xb7, 0xfd, 0x93, 0x26,	0x36, 0x3f, 0xf7, 0xcc,
	0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
	0x07, 0x12, 0x80, 0xe2,	0xeb, 0x27, 0xb2, 0x75,
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
	0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	0x53, 0xd1, 0x00, 0xed,	0x20, 0xfc, 0xb1, 0x5b,
	0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
	0x45, 0xf9, 0x02, 0x7f,	0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
	0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	0xcd, 0x0c, 0x13, 0xec,	0x5f, 0x97, 0x44, 0x17,
	0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
	0x46, 0xee, 0xb8, 0x14,	0xde, 0x5e, 0x0b, 0xdb,
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
	0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	0xe7, 0xc8, 0x37, 0x6d,	0x8d, 0xd5, 0x4e, 0xa9,
	0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
	0xe8, 0xdd, 0x74, 0x1f,	0x4b, 0xbd, 0x8b, 0x8a,
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
	0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11,	0x69, 0xd9, 0x8e, 0x94,
	0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
	0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static const uint8_t rsbox[] = {
	0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38,
	0xbf, 0x40, 0xa3, 0x9e,	0x81, 0xf3, 0xd7, 0xfb,
	0x7c, 0xe3, 0x39, 0x82,	0x9b, 0x2f, 0xff, 0x87,
	0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
	0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d,
	0xee, 0x4c, 0x95, 0x0b,	0x42, 0xfa, 0xc3, 0x4e,
	0x08, 0x2e, 0xa1, 0x66,	0x28, 0xd9, 0x24, 0xb2,
	0x76, 0x5b, 0xa2, 0x49,	0x6d, 0x8b, 0xd1, 0x25,
	0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16,
	0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
	0x6c, 0x70, 0x48, 0x50,	0xfd, 0xed, 0xb9, 0xda,
	0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
	0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a,
	0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
	0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02,
	0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
	0x3a, 0x91, 0x11, 0x41,	0x4f, 0x67, 0xdc, 0xea,
	0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
	0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85,
	0xe2, 0xf9, 0x37, 0xe8,	0x1c, 0x75, 0xdf, 0x6e,
	0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89,
	0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
	0xfc, 0x56, 0x3e, 0x4b,	0xc6, 0xd2, 0x79, 0x20,
	0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
	0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31,
	0xb1, 0x12, 0x10, 0x59,	0x27, 0x80, 0xec, 0x5f,
	0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d,
	0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
	0xa0, 0xe0, 0x3b, 0x4d,	0xae, 0x2a, 0xf5, 0xb0,
	0xc8, 0xeb, 0xbb, 0x3c,	0x83, 0x53, 0x99, 0x61,
	0x17, 0x2b, 0x04, 0x7e,	0xba, 0x77, 0xd6, 0x26,
	0xe1, 0x69, 0x14, 0x63,	0x55, 0x21, 0x0c, 0x7d,
};

// SubWord() is a function that takes a four-byte input word and
// applies the S-box to each of the four bytes to produce an output word.
static uint32_t Subword(uint32_t x)
{
	return (sbox[(x >> 24)      ] << 24)
	|      (sbox[(x >> 16) & 255] << 16)
	|      (sbox[(x >> 8 ) & 255] << 8 )
	|      (sbox[(x      ) & 255]      );
}

// This function produces Nb(Nr+1) round keys.
// The round keys are used in each round to decrypt the states.
static int KeyExpansion(uint32_t *RoundKey, const void *key, unsigned key_len)
{
	// The round constant word array, Rcon[i], contains the values given by
	// x to th e power (i-1) being powers of x (x is denoted as {02}) in the field GF(2^8).
	// Note that i starts at 2, not 0.
	static const uint8_t Rcon[] = {
		0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
	//..... 0x6c, 0xd8, 0xab, 0x4d, 0x9a, 0x2f, 0x5e, 0xbc, 0x63, 0xc6,...
	// but aes256 only uses values up to 0x36
	};
	int rounds, words_key, words_RoundKey;
	int i, j, k;

	// key_len 16: aes128, rounds 10, words_key 4, words_RoundKey 44
	// key_len 24: aes192, rounds 12, words_key 6, words_RoundKey 52
	// key_len 32: aes256, rounds 14, words_key 8, words_RoundKey 60
	words_key = key_len / 4;
	rounds = 6 + (key_len / 4);
	words_RoundKey = 28 + key_len;

	// The first round key is the key itself.
	for (i = 0; i < words_key; i++)
		RoundKey[i] = get_unaligned_be32((uint32_t*)key + i);
	// i == words_key now

	// All other round keys are found from the previous round keys.
	j = k = 0;
	for (; i < words_RoundKey; i++) {
		uint32_t tempa;

		tempa = RoundKey[i - 1];
		if (j == 0) {
			// RotWord(): rotates the 4 bytes in a word to the left once.
			tempa = (tempa << 8) | (tempa >> 24);
			tempa = Subword(tempa);
			tempa ^= (uint32_t)Rcon[k] << 24;
		} else if (words_key > 6 && j == 4) {
			tempa = Subword(tempa);
		}
		RoundKey[i] = RoundKey[i - words_key] ^ tempa;
		j++;
		if (j == words_key) {
			j = 0;
			k++;
		}
	}
	return rounds;
}

// This function adds the round key to state.
// The round key is added to the state by an XOR function.
static void AddRoundKey(unsigned astate[16], const uint32_t *RoundKeys)
{
	int i;

	for (i = 0; i < 16; i += 4) {
		uint32_t n = *RoundKeys++;
		astate[i + 0] ^= (n >> 24);
		astate[i + 1] ^= (n >> 16) & 255;
		astate[i + 2] ^= (n >> 8) & 255;
		astate[i + 3] ^= n & 255;
	}
}

// The SubBytes Function Substitutes the values in the
// state matrix with values in an S-box.
static void SubBytes(unsigned astate[16])
{
	int i;

	for (i = 0; i < 16; i++)
		astate[i] = sbox[astate[i]];
}

// Our code actually stores "columns" (in aes encryption terminology)
// of state in rows: first 4 elements are "row 0, col 0", "row 1, col 0".
// "row 2, col 0", "row 3, col 0". The fifth element is "row 0, col 1",
// and so on.
#define ASTATE(col,row) astate[(col)*4 + (row)]

// The ShiftRows() function shifts the rows in the state to the left.
// Each row is shifted with different offset.
// Offset = Row number. So the first row is not shifted.
static void ShiftRows(unsigned astate[16])
{
	unsigned v;

	// Rotate first row 1 columns to left
	v = ASTATE(0,1);
	ASTATE(0,1) = ASTATE(1,1);
	ASTATE(1,1) = ASTATE(2,1);
	ASTATE(2,1) = ASTATE(3,1);
	ASTATE(3,1) = v;

	// Rotate second row 2 columns to left
	v = ASTATE(0,2); ASTATE(0,2) = ASTATE(2,2); ASTATE(2,2) = v;
	v = ASTATE(1,2); ASTATE(1,2) = ASTATE(3,2); ASTATE(3,2) = v;

	// Rotate third row 3 columns to left
	v = ASTATE(3,3);
	ASTATE(3,3) = ASTATE(2,3);
	ASTATE(2,3) = ASTATE(1,3);
	ASTATE(1,3) = ASTATE(0,3);
	ASTATE(0,3) = v;
}

// MixColumns function mixes the columns of the state matrix
static void MixColumns(unsigned astate[16])
{
	int i;

	for (i = 0; i < 16; i += 4) {
		unsigned a, b, c, d;
		unsigned x, y, z, t;

		a = astate[i + 0];
		b = astate[i + 1];
		c = astate[i + 2];
		d = astate[i + 3];
		x = (a << 1) ^ b ^ (b << 1) ^ c ^ d;
		y = a ^ (b << 1) ^ c ^ (c << 1) ^ d;
		z = a ^ b ^ (c << 1) ^ d ^ (d << 1);
		t = a ^ (a << 1) ^ b ^ c ^ (d << 1);
		astate[i + 0] = x ^ ((-(int)(x >> 8)) & 0x11b);
		astate[i + 1] = y ^ ((-(int)(y >> 8)) & 0x11b);
		astate[i + 2] = z ^ ((-(int)(z >> 8)) & 0x11b);
		astate[i + 3] = t ^ ((-(int)(t >> 8)) & 0x11b);
	}
}

// The SubBytes Function Substitutes the values in the
// state matrix with values in an S-box.
static void InvSubBytes(unsigned astate[16])
{
	int i;

	for (i = 0; i < 16; i++)
		astate[i] = rsbox[astate[i]];
}

static void InvShiftRows(unsigned astate[16])
{
	unsigned v;

	// Rotate first row 1 columns to right
	v = ASTATE(3,1);
	ASTATE(3,1) = ASTATE(2,1);
	ASTATE(2,1) = ASTATE(1,1);
	ASTATE(1,1) = ASTATE(0,1);
	ASTATE(0,1) = v;

	// Rotate second row 2 columns to right
	v = ASTATE(0,2); ASTATE(0,2) = ASTATE(2,2); ASTATE(2,2) = v;
	v = ASTATE(1,2); ASTATE(1,2) = ASTATE(3,2); ASTATE(3,2) = v;

	// Rotate third row 3 columns to right
	v = ASTATE(0,3);
	ASTATE(0,3) = ASTATE(1,3);
	ASTATE(1,3) = ASTATE(2,3);
	ASTATE(2,3) = ASTATE(3,3);
	ASTATE(3,3) = v;
}

static ALWAYS_INLINE unsigned Multiply(unsigned x)
{
	unsigned y;

	y = x >> 8;
	return (x ^ y ^ (y << 1) ^ (y << 3) ^ (y << 4)) & 255;
}

// MixColumns function mixes the columns of the state matrix.
// The method used to multiply may be difficult to understand for the inexperienced.
// Please use the references to gain more information.
static void InvMixColumns(unsigned astate[16])
{
	int i;

	for (i = 0; i < 16; i += 4) {
		unsigned a, b, c, d;
		unsigned x, y, z, t;

		a = astate[i + 0];
		b = astate[i + 1];
		c = astate[i + 2];
		d = astate[i + 3];
		x = (a << 1) ^ (a << 2) ^ (a << 3) ^ b ^ (b << 1) ^ (b << 3)
		/***/ ^ c ^ (c << 2) ^ (c << 3) ^ d ^ (d << 3);
		y = a ^ (a << 3) ^ (b << 1) ^ (b << 2) ^ (b << 3)
		/***/ ^ c ^ (c << 1) ^ (c << 3) ^ d ^ (d << 2) ^ (d << 3);
		z = a ^ (a << 2) ^ (a << 3) ^ b ^ (b << 3)
		/***/ ^ (c << 1) ^ (c << 2) ^ (c << 3) ^ d ^ (d << 1) ^ (d << 3);
		t = a ^ (a << 1) ^ (a << 3) ^ b ^ (b << 2) ^ (b << 3)
		/***/ ^ c ^ (c << 3) ^ (d << 1) ^ (d << 2) ^ (d << 3);
		astate[i + 0] = Multiply(x);
		astate[i + 1] = Multiply(y);
		astate[i + 2] = Multiply(z);
		astate[i + 3] = Multiply(t);
	}
}

static void aes_encrypt_1(unsigned astate[16], unsigned rounds, const uint32_t *RoundKey)
{
	for (;;) {
		AddRoundKey(astate, RoundKey);
		RoundKey += 4;
		SubBytes(astate);
		ShiftRows(astate);
		if (--rounds == 0)
			break;
		MixColumns(astate);
	}
	AddRoundKey(astate, RoundKey);
}

#if 0 // UNUSED
static void aes_encrypt_one_block(unsigned rounds, const uint32_t *RoundKey, const void *data, void *dst)
{
	unsigned astate[16];
	unsigned i;

	const uint8_t *pt = data;
	uint8_t *ct = dst;

	for (i = 0; i < 16; i++)
		astate[i] = pt[i];
	aes_encrypt_1(astate, rounds, RoundKey);
	for (i = 0; i < 16; i++)
		ct[i] = astate[i];
}
#endif

void aes_cbc_encrypt(const void *key, int klen, void *iv, const void *data, size_t len, void *dst)
{
	uint32_t RoundKey[60];
	uint8_t iv2[16];
	unsigned rounds;

	const uint8_t *pt = data;
	uint8_t *ct = dst;

	memcpy(iv2, iv, 16);
	rounds = KeyExpansion(RoundKey, key, klen);
	while (len > 0) {
		{
			/* almost aes_encrypt_one_block(rounds, RoundKey, pt, ct);
			 * but xor'ing of IV with plaintext[] is combined
			 * with plaintext[] -> astate[]
			 */
			int i;
			unsigned astate[16];
			for (i = 0; i < 16; i++)
				astate[i] = pt[i] ^ iv2[i];
			aes_encrypt_1(astate, rounds, RoundKey);
			for (i = 0; i < 16; i++)
				iv2[i] = ct[i] = astate[i];
		}
		ct += 16;
		pt += 16;
		len -= 16;
	}
}

static void aes_decrypt_1(unsigned astate[16], unsigned rounds, const uint32_t *RoundKey)
{
	RoundKey += rounds * 4;
	AddRoundKey(astate, RoundKey);
	for (;;) {
		InvShiftRows(astate);
		InvSubBytes(astate);
		RoundKey -= 4;
		AddRoundKey(astate, RoundKey);
		if (--rounds == 0)
			break;
		InvMixColumns(astate);
	}
}

#if 0 //UNUSED
static void aes_decrypt_one_block(unsigned rounds, const uint32_t *RoundKey, const void *data, void *dst)
{
	unsigned astate[16];
	unsigned i;

	const uint8_t *ct = data;
	uint8_t *pt = dst;

	for (i = 0; i < 16; i++)
		astate[i] = ct[i];
	aes_decrypt_1(astate, rounds, RoundKey);
	for (i = 0; i < 16; i++)
		pt[i] = astate[i];
}
#endif

void aes_cbc_decrypt(const void *key, int klen, void *iv, const void *data, size_t len, void *dst)
{
	uint32_t RoundKey[60];
	uint8_t iv2[16];
	uint8_t iv3[16];
	unsigned rounds;
	uint8_t *ivbuf;
	uint8_t *ivnext;

	const uint8_t *ct = data;
	uint8_t *pt = dst;

	rounds = KeyExpansion(RoundKey, key, klen);
	ivbuf = memcpy(iv2, iv, 16);
	while (len) {
		ivnext = (ivbuf==iv2) ? iv3 : iv2;
		{
			/* almost aes_decrypt_one_block(rounds, RoundKey, ct, pt)
			 * but xor'ing of ivbuf is combined with astate[] -> plaintext[]
			 */
			int i;
			unsigned astate[16];
			for (i = 0; i < 16; i++)
				ivnext[i] = astate[i] = ct[i];
			aes_decrypt_1(astate, rounds, RoundKey);
			for (i = 0; i < 16; i++)
				pt[i] = astate[i] ^ ivbuf[i];
		}
		ivbuf = ivnext;
		ct += 16;
		pt += 16;
		len -= 16;
	}
}
