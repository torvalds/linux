/* 
 * Cryptographic API.
 *
 * DES & Triple DES EDE Cipher Algorithms.
 *
 * Originally released as descore by Dana L. How <how@isl.stanford.edu>.
 * Modified by Raimar Falke <rf13@inf.tu-dresden.de> for the Linux-Kernel.
 * Derived from Cryptoapi and Nettle implementations, adapted for in-place
 * scatterlist interface.  Changed LGPL to GPL per section 3 of the LGPL.
 *
 * Copyright (c) 1992 Dana L. How.
 * Copyright (c) Raimar Falke <rf13@inf.tu-dresden.de> 
 * Copyright (c) Gisle Sælensminde <gisle@ii.uib.no>
 * Copyright (C) 2001 Niels Möller.
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <asm/scatterlist.h>
#include <linux/crypto.h>

#define DES_KEY_SIZE		8
#define DES_EXPKEY_WORDS	32
#define DES_BLOCK_SIZE		8

#define DES3_EDE_KEY_SIZE	(3 * DES_KEY_SIZE)
#define DES3_EDE_EXPKEY_WORDS	(3 * DES_EXPKEY_WORDS)
#define DES3_EDE_BLOCK_SIZE	DES_BLOCK_SIZE

#define ROR(d,c,o)	((d) = (d) >> (c) | (d) << (o))

struct des_ctx {
	u8 iv[DES_BLOCK_SIZE];
	u32 expkey[DES_EXPKEY_WORDS];
};

struct des3_ede_ctx {
	u8 iv[DES_BLOCK_SIZE];
	u32 expkey[DES3_EDE_EXPKEY_WORDS];
};

static const u32 des_keymap[] = {
	0x02080008, 0x02082000, 0x00002008, 0x00000000,
	0x02002000, 0x00080008, 0x02080000, 0x02082008,
	0x00000008, 0x02000000, 0x00082000, 0x00002008,
	0x00082008, 0x02002008, 0x02000008, 0x02080000,
	0x00002000, 0x00082008, 0x00080008, 0x02002000,
	0x02082008, 0x02000008, 0x00000000, 0x00082000,
	0x02000000, 0x00080000, 0x02002008, 0x02080008,
	0x00080000, 0x00002000, 0x02082000, 0x00000008,
	0x00080000, 0x00002000, 0x02000008, 0x02082008,
	0x00002008, 0x02000000, 0x00000000, 0x00082000,
	0x02080008, 0x02002008, 0x02002000, 0x00080008,
	0x02082000, 0x00000008, 0x00080008, 0x02002000,
	0x02082008, 0x00080000, 0x02080000, 0x02000008,
	0x00082000, 0x00002008, 0x02002008, 0x02080000,
	0x00000008, 0x02082000, 0x00082008, 0x00000000,
	0x02000000, 0x02080008, 0x00002000, 0x00082008,

	0x08000004, 0x00020004, 0x00000000, 0x08020200,
	0x00020004, 0x00000200, 0x08000204, 0x00020000,
	0x00000204, 0x08020204, 0x00020200, 0x08000000,
	0x08000200, 0x08000004, 0x08020000, 0x00020204,
	0x00020000, 0x08000204, 0x08020004, 0x00000000,
	0x00000200, 0x00000004, 0x08020200, 0x08020004,
	0x08020204, 0x08020000, 0x08000000, 0x00000204,
	0x00000004, 0x00020200, 0x00020204, 0x08000200,
	0x00000204, 0x08000000, 0x08000200, 0x00020204,
	0x08020200, 0x00020004, 0x00000000, 0x08000200,
	0x08000000, 0x00000200, 0x08020004, 0x00020000,
	0x00020004, 0x08020204, 0x00020200, 0x00000004,
	0x08020204, 0x00020200, 0x00020000, 0x08000204,
	0x08000004, 0x08020000, 0x00020204, 0x00000000,
	0x00000200, 0x08000004, 0x08000204, 0x08020200,
	0x08020000, 0x00000204, 0x00000004, 0x08020004,

	0x80040100, 0x01000100, 0x80000000, 0x81040100,
	0x00000000, 0x01040000, 0x81000100, 0x80040000,
	0x01040100, 0x81000000, 0x01000000, 0x80000100,
	0x81000000, 0x80040100, 0x00040000, 0x01000000,
	0x81040000, 0x00040100, 0x00000100, 0x80000000,
	0x00040100, 0x81000100, 0x01040000, 0x00000100,
	0x80000100, 0x00000000, 0x80040000, 0x01040100,
	0x01000100, 0x81040000, 0x81040100, 0x00040000,
	0x81040000, 0x80000100, 0x00040000, 0x81000000,
	0x00040100, 0x01000100, 0x80000000, 0x01040000,
	0x81000100, 0x00000000, 0x00000100, 0x80040000,
	0x00000000, 0x81040000, 0x01040100, 0x00000100,
	0x01000000, 0x81040100, 0x80040100, 0x00040000,
	0x81040100, 0x80000000, 0x01000100, 0x80040100,
	0x80040000, 0x00040100, 0x01040000, 0x81000100,
	0x80000100, 0x01000000, 0x81000000, 0x01040100,

	0x04010801, 0x00000000, 0x00010800, 0x04010000,
	0x04000001, 0x00000801, 0x04000800, 0x00010800,
	0x00000800, 0x04010001, 0x00000001, 0x04000800,
	0x00010001, 0x04010800, 0x04010000, 0x00000001,
	0x00010000, 0x04000801, 0x04010001, 0x00000800,
	0x00010801, 0x04000000, 0x00000000, 0x00010001,
	0x04000801, 0x00010801, 0x04010800, 0x04000001,
	0x04000000, 0x00010000, 0x00000801, 0x04010801,
	0x00010001, 0x04010800, 0x04000800, 0x00010801,
	0x04010801, 0x00010001, 0x04000001, 0x00000000,
	0x04000000, 0x00000801, 0x00010000, 0x04010001,
	0x00000800, 0x04000000, 0x00010801, 0x04000801,
	0x04010800, 0x00000800, 0x00000000, 0x04000001,
	0x00000001, 0x04010801, 0x00010800, 0x04010000,
	0x04010001, 0x00010000, 0x00000801, 0x04000800,
	0x04000801, 0x00000001, 0x04010000, 0x00010800,

	0x00000400, 0x00000020, 0x00100020, 0x40100000,
	0x40100420, 0x40000400, 0x00000420, 0x00000000,
	0x00100000, 0x40100020, 0x40000020, 0x00100400,
	0x40000000, 0x00100420, 0x00100400, 0x40000020,
	0x40100020, 0x00000400, 0x40000400, 0x40100420,
	0x00000000, 0x00100020, 0x40100000, 0x00000420,
	0x40100400, 0x40000420, 0x00100420, 0x40000000,
	0x40000420, 0x40100400, 0x00000020, 0x00100000,
	0x40000420, 0x00100400, 0x40100400, 0x40000020,
	0x00000400, 0x00000020, 0x00100000, 0x40100400,
	0x40100020, 0x40000420, 0x00000420, 0x00000000,
	0x00000020, 0x40100000, 0x40000000, 0x00100020,
	0x00000000, 0x40100020, 0x00100020, 0x00000420,
	0x40000020, 0x00000400, 0x40100420, 0x00100000,
	0x00100420, 0x40000000, 0x40000400, 0x40100420,
	0x40100000, 0x00100420, 0x00100400, 0x40000400,

	0x00800000, 0x00001000, 0x00000040, 0x00801042,
	0x00801002, 0x00800040, 0x00001042, 0x00801000,
	0x00001000, 0x00000002, 0x00800002, 0x00001040,
	0x00800042, 0x00801002, 0x00801040, 0x00000000,
	0x00001040, 0x00800000, 0x00001002, 0x00000042,
	0x00800040, 0x00001042, 0x00000000, 0x00800002,
	0x00000002, 0x00800042, 0x00801042, 0x00001002,
	0x00801000, 0x00000040, 0x00000042, 0x00801040,
	0x00801040, 0x00800042, 0x00001002, 0x00801000,
	0x00001000, 0x00000002, 0x00800002, 0x00800040,
	0x00800000, 0x00001040, 0x00801042, 0x00000000,
	0x00001042, 0x00800000, 0x00000040, 0x00001002,
	0x00800042, 0x00000040, 0x00000000, 0x00801042,
	0x00801002, 0x00801040, 0x00000042, 0x00001000,
	0x00001040, 0x00801002, 0x00800040, 0x00000042,
	0x00000002, 0x00001042, 0x00801000, 0x00800002,

	0x10400000, 0x00404010, 0x00000010, 0x10400010,
	0x10004000, 0x00400000, 0x10400010, 0x00004010,
	0x00400010, 0x00004000, 0x00404000, 0x10000000,
	0x10404010, 0x10000010, 0x10000000, 0x10404000,
	0x00000000, 0x10004000, 0x00404010, 0x00000010,
	0x10000010, 0x10404010, 0x00004000, 0x10400000,
	0x10404000, 0x00400010, 0x10004010, 0x00404000,
	0x00004010, 0x00000000, 0x00400000, 0x10004010,
	0x00404010, 0x00000010, 0x10000000, 0x00004000,
	0x10000010, 0x10004000, 0x00404000, 0x10400010,
	0x00000000, 0x00404010, 0x00004010, 0x10404000,
	0x10004000, 0x00400000, 0x10404010, 0x10000000,
	0x10004010, 0x10400000, 0x00400000, 0x10404010,
	0x00004000, 0x00400010, 0x10400010, 0x00004010,
	0x00400010, 0x00000000, 0x10404000, 0x10000010,
	0x10400000, 0x10004010, 0x00000010, 0x00404000,

	0x00208080, 0x00008000, 0x20200000, 0x20208080,
	0x00200000, 0x20008080, 0x20008000, 0x20200000,
	0x20008080, 0x00208080, 0x00208000, 0x20000080,
	0x20200080, 0x00200000, 0x00000000, 0x20008000,
	0x00008000, 0x20000000, 0x00200080, 0x00008080,
	0x20208080, 0x00208000, 0x20000080, 0x00200080,
	0x20000000, 0x00000080, 0x00008080, 0x20208000,
	0x00000080, 0x20200080, 0x20208000, 0x00000000,
	0x00000000, 0x20208080, 0x00200080, 0x20008000,
	0x00208080, 0x00008000, 0x20000080, 0x00200080,
	0x20208000, 0x00000080, 0x00008080, 0x20200000,
	0x20008080, 0x20000000, 0x20200000, 0x00208000,
	0x20208080, 0x00008080, 0x00208000, 0x20200080,
	0x00200000, 0x20000080, 0x20008000, 0x00000000,
	0x00008000, 0x00200000, 0x20200080, 0x00208080,
	0x20000000, 0x20208000, 0x00000080, 0x20008080,
};

static const u8 rotors[] = {
	34, 13,  5, 46, 47, 18, 32, 41, 11, 53, 33, 20,
	14, 36, 30, 24, 49,  2, 15, 37, 42, 50,  0, 21,
	38, 48,  6, 26, 39,  4, 52, 25, 12, 27, 31, 40,
	1, 17, 28, 29, 23, 51, 35,  7,  3, 22,  9, 43,

	41, 20, 12, 53, 54, 25, 39, 48, 18, 31, 40, 27,
	21, 43, 37,  0,  1,  9, 22, 44, 49,  2,  7, 28,
	45, 55, 13, 33, 46, 11,  6, 32, 19, 34, 38, 47,
	8, 24, 35, 36, 30,  3, 42, 14, 10, 29, 16, 50,

	55, 34, 26, 38, 11, 39, 53,  5, 32, 45, 54, 41,
	35,  2, 51, 14, 15, 23, 36,  3,  8, 16, 21, 42,
	6, 12, 27, 47, 31, 25, 20, 46, 33, 48, 52,  4,
	22,  7, 49, 50, 44, 17,  1, 28, 24, 43, 30,  9,

	12, 48, 40, 52, 25, 53, 38, 19, 46,  6, 11, 55,
	49, 16, 10, 28, 29, 37, 50, 17, 22, 30, 35,  1,
	20, 26, 41,  4, 45, 39, 34, 31, 47,  5, 13, 18,
	36, 21,  8,  9,  3,  0, 15, 42,  7,  2, 44, 23,

	26,  5, 54, 13, 39, 38, 52, 33, 31, 20, 25, 12,
	8, 30, 24, 42, 43, 51,  9,  0, 36, 44, 49, 15,
	34, 40, 55, 18,  6, 53, 48, 45,  4, 19, 27, 32,
	50, 35, 22, 23, 17, 14, 29,  1, 21, 16,  3, 37,

	40, 19, 11, 27, 53, 52, 13, 47, 45, 34, 39, 26,
	22, 44,  7,  1,  2, 10, 23, 14, 50,  3,  8, 29,
	48, 54, 12, 32, 20, 38,  5,  6, 18, 33, 41, 46,
	9, 49, 36, 37,  0, 28, 43, 15, 35, 30, 17, 51,

	54, 33, 25, 41, 38, 13, 27,  4,  6, 48, 53, 40,
	36,  3, 21, 15, 16, 24, 37, 28,  9, 17, 22, 43,
	5, 11, 26, 46, 34, 52, 19, 20, 32, 47, 55, 31,
	23,  8, 50, 51, 14, 42,  2, 29, 49, 44,  0, 10,

	11, 47, 39, 55, 52, 27, 41, 18, 20,  5, 38, 54,
	50, 17, 35, 29, 30,  7, 51, 42, 23,  0, 36,  2,
	19, 25, 40, 31, 48, 13, 33, 34, 46,  4, 12, 45,
	37, 22,  9, 10, 28,  1, 16, 43,  8,  3, 14, 24,

	18, 54, 46,  5,  6, 34, 48, 25, 27, 12, 45,  4,
	2, 24, 42, 36, 37, 14,  3, 49, 30,  7, 43,  9,
	26, 32, 47, 38, 55, 20, 40, 41, 53, 11, 19, 52,
	44, 29, 16, 17, 35,  8, 23, 50, 15, 10, 21,  0,

	32, 11, 31, 19, 20, 48,  5, 39, 41, 26,  6, 18,
	16,  7,  1, 50, 51, 28, 17,  8, 44, 21,  2, 23,
	40, 46,  4, 52, 12, 34, 54, 55, 38, 25, 33, 13,
	3, 43, 30,  0, 49, 22, 37,  9, 29, 24, 35, 14,

	46, 25, 45, 33, 34,  5, 19, 53, 55, 40, 20, 32,
	30, 21, 15,  9, 10, 42,  0, 22,  3, 35, 16, 37,
	54, 31, 18, 13, 26, 48, 11, 12, 52, 39, 47, 27,
	17,  2, 44, 14,  8, 36, 51, 23, 43,  7, 49, 28,

	31, 39,  6, 47, 48, 19, 33, 38, 12, 54, 34, 46,
	44, 35, 29, 23, 24,  1, 14, 36, 17, 49, 30, 51,
	11, 45, 32, 27, 40,  5, 25, 26, 13, 53,  4, 41,
	0, 16,  3, 28, 22, 50, 10, 37,  2, 21,  8, 42,

	45, 53, 20,  4,  5, 33, 47, 52, 26, 11, 48, 31,
	3, 49, 43, 37,  7, 15, 28, 50,  0,  8, 44, 10,
	25,  6, 46, 41, 54, 19, 39, 40, 27, 38, 18, 55,
	14, 30, 17, 42, 36,  9, 24, 51, 16, 35, 22,  1,

	6, 38, 34, 18, 19, 47,  4, 13, 40, 25,  5, 45,
	17,  8,  2, 51, 21, 29, 42,  9, 14, 22,  3, 24,
	39, 20, 31, 55, 11, 33, 53, 54, 41, 52, 32, 12,
	28, 44,  0,  1, 50, 23,  7, 10, 30, 49, 36, 15,

	20, 52, 48, 32, 33,  4, 18, 27, 54, 39, 19,  6,
	0, 22, 16, 10, 35, 43,  1, 23, 28, 36, 17,  7,
	53, 34, 45, 12, 25, 47, 38, 11, 55, 13, 46, 26,
	42,  3, 14, 15,  9, 37, 21, 24, 44,  8, 50, 29,

	27,  6, 55, 39, 40, 11, 25, 34,  4, 46, 26, 13,
	7, 29, 23, 17, 42, 50,  8, 30, 35, 43, 24, 14,
	31, 41, 52, 19, 32, 54, 45, 18,  5, 20, 53, 33,
	49, 10, 21, 22, 16, 44, 28,  0, 51, 15,  2, 36,
};

static const u8 parity[] = {
	8,1,0,8,0,8,8,0,0,8,8,0,8,0,2,8,0,8,8,0,8,0,0,8,8,0,0,8,0,8,8,3,
	0,8,8,0,8,0,0,8,8,0,0,8,0,8,8,0,8,0,0,8,0,8,8,0,0,8,8,0,8,0,0,8,
	0,8,8,0,8,0,0,8,8,0,0,8,0,8,8,0,8,0,0,8,0,8,8,0,0,8,8,0,8,0,0,8,
	8,0,0,8,0,8,8,0,0,8,8,0,8,0,0,8,0,8,8,0,8,0,0,8,8,0,0,8,0,8,8,0,
	0,8,8,0,8,0,0,8,8,0,0,8,0,8,8,0,8,0,0,8,0,8,8,0,0,8,8,0,8,0,0,8,
	8,0,0,8,0,8,8,0,0,8,8,0,8,0,0,8,0,8,8,0,8,0,0,8,8,0,0,8,0,8,8,0,
	8,0,0,8,0,8,8,0,0,8,8,0,8,0,0,8,0,8,8,0,8,0,0,8,8,0,0,8,0,8,8,0,
	4,8,8,0,8,0,0,8,8,0,0,8,0,8,8,0,8,5,0,8,0,8,8,0,0,8,8,0,8,0,6,8,
};


static void des_small_fips_encrypt(u32 *expkey, u8 *dst, const u8 *src)
{
	u32 x, y, z;
	
	x  = src[7];
	x <<= 8;
	x |= src[6];
	x <<= 8;
	x |= src[5];
	x <<= 8;
	x |= src[4];
	y  = src[3];
	y <<= 8;
	y |= src[2];
	y <<= 8;
	y |= src[1];
	y <<= 8;
	y |= src[0];
	z  = ((x >> 004) ^ y) & 0x0F0F0F0FL;
	x ^= z << 004;
	y ^= z;
	z  = ((y >> 020) ^ x) & 0x0000FFFFL;
	y ^= z << 020;
	x ^= z;
	z  = ((x >> 002) ^ y) & 0x33333333L;
	x ^= z << 002;
	y ^= z;
	z  = ((y >> 010) ^ x) & 0x00FF00FFL;
	y ^= z << 010;
	x ^= z;
	x  = x >> 1 | x << 31;
	z  = (x ^ y) & 0x55555555L;
	y ^= z;
	x ^= z;
	y  = y >> 1 | y << 31;
	z  = expkey[0];
	z ^= y;
	x ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[1];
	z ^= y;
	z  = z << 4 | z >> 28;
	x ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[2];
	z ^= x;
	y ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[3];
	z ^= x;
	z  = z << 4 | z >> 28;
	y ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[4];
	z ^= y;
	x ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[5];
	z ^= y;
	z  = z << 4 | z >> 28;
	x ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[6];
	z ^= x;
	y ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[7];
	z ^= x;
	z  = z << 4 | z >> 28;
	y ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[8];
	z ^= y;
	x ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[9];
	z ^= y;
	z  = z << 4 | z >> 28;
	x ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[10];
	z ^= x;
	y ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[11];
	z ^= x;
	z  = z << 4 | z >> 28;
	y ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[12];
	z ^= y;
	x ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[13];
	z ^= y;
	z  = z << 4 | z >> 28;
	x ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[14];
	z ^= x;
	y ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[15];
	z ^= x;
	z  = z << 4 | z >> 28;
	y ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[16];
	z ^= y;
	x ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[17];
	z ^= y;
	z  = z << 4 | z >> 28;
	x ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[18];
	z ^= x;
	y ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[19];
	z ^= x;
	z  = z << 4 | z >> 28;
	y ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[20];
	z ^= y;
	x ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[21];
	z ^= y;
	z  = z << 4 | z >> 28;
	x ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[22];
	z ^= x;
	y ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[23];
	z ^= x;
	z  = z << 4 | z >> 28;
	y ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[24];
	z ^= y;
	x ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[25];
	z ^= y;
	z  = z << 4 | z >> 28;
	x ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[26];
	z ^= x;
	y ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[27];
	z ^= x;
	z  = z << 4 | z >> 28;
	y ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[28];
	z ^= y;
	x ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[29];
	z ^= y;
	z  = z << 4 | z >> 28;
	x ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[30];
	z ^= x;
	y ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[31];
	z ^= x;
	z  = z << 4 | z >> 28;
	y ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	x  = x << 1 | x >> 31;
	z  = (x ^ y) & 0x55555555L;
	y ^= z;
	x ^= z;
	y  = y << 1 | y >> 31;
	z  = ((x >> 010) ^ y) & 0x00FF00FFL;
	x ^= z << 010;
	y ^= z;
	z  = ((y >> 002) ^ x) & 0x33333333L;
	y ^= z << 002;
	x ^= z;
	z  = ((x >> 020) ^ y) & 0x0000FFFFL;
	x ^= z << 020;
	y ^= z;
	z  = ((y >> 004) ^ x) & 0x0F0F0F0FL;
	y ^= z << 004;
	x ^= z;
	dst[0] = x;
	x >>= 8;
	dst[1] = x;
	x >>= 8;
	dst[2] = x;
	x >>= 8;
	dst[3] = x;
	dst[4] = y;
	y >>= 8;
	dst[5] = y;
	y >>= 8;
	dst[6] = y;
	y >>= 8;
	dst[7] = y;
}

static void des_small_fips_decrypt(u32 *expkey, u8 *dst, const u8 *src)
{
	u32 x, y, z;
	
	x  = src[7];
	x <<= 8;
	x |= src[6];
	x <<= 8;
	x |= src[5];
	x <<= 8;
	x |= src[4];
	y  = src[3];
	y <<= 8;
	y |= src[2];
	y <<= 8;
	y |= src[1];
	y <<= 8;
	y |= src[0];
	z  = ((x >> 004) ^ y) & 0x0F0F0F0FL;
	x ^= z << 004;
	y ^= z;
	z  = ((y >> 020) ^ x) & 0x0000FFFFL;
	y ^= z << 020;
	x ^= z;
	z  = ((x >> 002) ^ y) & 0x33333333L;
	x ^= z << 002;
	y ^= z;
	z  = ((y >> 010) ^ x) & 0x00FF00FFL;
	y ^= z << 010;
	x ^= z;
	x  = x >> 1 | x << 31;
	z  = (x ^ y) & 0x55555555L;
	y ^= z;
	x ^= z;
	y  = y >> 1 | y << 31;
	z  = expkey[31];
	z ^= y;
	z  = z << 4 | z >> 28;
	x ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[30];
	z ^= y;
	x ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[29];
	z ^= x;
	z  = z << 4 | z >> 28;
	y ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[28];
	z ^= x;
	y ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[27];
	z ^= y;
	z  = z << 4 | z >> 28;
	x ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[26];
	z ^= y;
	x ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[25];
	z ^= x;
	z  = z << 4 | z >> 28;
	y ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[24];
	z ^= x;
	y ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[23];
	z ^= y;
	z  = z << 4 | z >> 28;
	x ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[22];
	z ^= y;
	x ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[21];
	z ^= x;
	z  = z << 4 | z >> 28;
	y ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[20];
	z ^= x;
	y ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[19];
	z ^= y;
	z  = z << 4 | z >> 28;
	x ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[18];
	z ^= y;
	x ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[17];
	z ^= x;
	z  = z << 4 | z >> 28;
	y ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[16];
	z ^= x;
	y ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[15];
	z ^= y;
	z  = z << 4 | z >> 28;
	x ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[14];
	z ^= y;
	x ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[13];
	z ^= x;
	z  = z << 4 | z >> 28;
	y ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[12];
	z ^= x;
	y ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[11];
	z ^= y;
	z  = z << 4 | z >> 28;
	x ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[10];
	z ^= y;
	x ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[9];
	z ^= x;
	z  = z << 4 | z >> 28;
	y ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[8];
	z ^= x;
	y ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[7];
	z ^= y;
	z  = z << 4 | z >> 28;
	x ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[6];
	z ^= y;
	x ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[5];
	z ^= x;
	z  = z << 4 | z >> 28;
	y ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[4];
	z ^= x;
	y ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[3];
	z ^= y;
	z  = z << 4 | z >> 28;
	x ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[2];
	z ^= y;
	x ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	x ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	z  = expkey[1];
	z ^= x;
	z  = z << 4 | z >> 28;
	y ^= * (u32 *) ((u8 *) (des_keymap + 448) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 384) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 320) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 256) + (0xFC & z));
	z  = expkey[0];
	z ^= x;
	y ^= * (u32 *) ((u8 *) (des_keymap + 192) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 128) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) (des_keymap + 64) + (0xFC & z));
	z >>= 8;
	y ^= * (u32 *) ((u8 *) des_keymap + (0xFC & z));
	x  = x << 1 | x >> 31;
	z  = (x ^ y) & 0x55555555L;
	y ^= z;
	x ^= z;
	y  = y << 1 | y >> 31;
	z  = ((x >> 010) ^ y) & 0x00FF00FFL;
	x ^= z << 010;
	y ^= z;
	z  = ((y >> 002) ^ x) & 0x33333333L;
	y ^= z << 002;
	x ^= z;
	z  = ((x >> 020) ^ y) & 0x0000FFFFL;
	x ^= z << 020;
	y ^= z;
	z  = ((y >> 004) ^ x) & 0x0F0F0F0FL;
	y ^= z << 004;
	x ^= z;
	dst[0] = x;
	x >>= 8;
	dst[1] = x;
	x >>= 8;
	dst[2] = x;
	x >>= 8;
	dst[3] = x;
	dst[4] = y;
	y >>= 8;
	dst[5] = y;
	y >>= 8;
	dst[6] = y;
	y >>= 8;
	dst[7] = y;
}

/*
 * RFC2451: Weak key checks SHOULD be performed.
 */
static int setkey(u32 *expkey, const u8 *key, unsigned int keylen, u32 *flags)
{
	const u8 *k;
	u8 *b0, *b1;
	u32 n, w;
	u8 bits0[56], bits1[56];

	n  = parity[key[0]]; n <<= 4;
	n |= parity[key[1]]; n <<= 4;
	n |= parity[key[2]]; n <<= 4;
	n |= parity[key[3]]; n <<= 4;
	n |= parity[key[4]]; n <<= 4;
	n |= parity[key[5]]; n <<= 4;
	n |= parity[key[6]]; n <<= 4;
	n |= parity[key[7]];
	w = 0x88888888L;
	
	if ((*flags & CRYPTO_TFM_REQ_WEAK_KEY)
	    && !((n - (w >> 3)) & w)) {  /* 1 in 10^10 keys passes this test */
		if (n < 0x41415151) {
			if (n < 0x31312121) {
				if (n < 0x14141515) {
					/* 01 01 01 01 01 01 01 01 */
					if (n == 0x11111111) goto weak;
					/* 01 1F 01 1F 01 0E 01 0E */
					if (n == 0x13131212) goto weak;
				} else {
					/* 01 E0 01 E0 01 F1 01 F1 */
					if (n == 0x14141515) goto weak;
					/* 01 FE 01 FE 01 FE 01 FE */
					if (n == 0x16161616) goto weak;
				}
			} else {
				if (n < 0x34342525) {
					/* 1F 01 1F 01 0E 01 0E 01 */
					if (n == 0x31312121) goto weak;
					/* 1F 1F 1F 1F 0E 0E 0E 0E (?) */
					if (n == 0x33332222) goto weak;
				} else {
					/* 1F E0 1F E0 0E F1 0E F1 */
					if (n == 0x34342525) goto weak;
					/* 1F FE 1F FE 0E FE 0E FE */
					if (n == 0x36362626) goto weak;
				}
			}
		} else {
			if (n < 0x61616161) {
				if (n < 0x44445555) {
					/* E0 01 E0 01 F1 01 F1 01 */
					if (n == 0x41415151) goto weak;
					/* E0 1F E0 1F F1 0E F1 0E */
					if (n == 0x43435252) goto weak;
				} else {
					/* E0 E0 E0 E0 F1 F1 F1 F1 (?) */
					if (n == 0x44445555) goto weak;
					/* E0 FE E0 FE F1 FE F1 FE */
					if (n == 0x46465656) goto weak;
				}
			} else {
				if (n < 0x64646565) {
					/* FE 01 FE 01 FE 01 FE 01 */
					if (n == 0x61616161) goto weak;
					/* FE 1F FE 1F FE 0E FE 0E */
					if (n == 0x63636262) goto weak;
				} else {
					/* FE E0 FE E0 FE F1 FE F1 */
					if (n == 0x64646565) goto weak;
					/* FE FE FE FE FE FE FE FE */
					if (n == 0x66666666) goto weak;
				}
			}
		}
	
		goto not_weak;
weak:
		*flags |= CRYPTO_TFM_RES_WEAK_KEY;
		return -EINVAL;
	}

not_weak:

	/* explode the bits */
	n = 56;
	b0 = bits0;
	b1 = bits1;
	
	do {
		w = (256 | *key++) << 2;
		do {
			--n;
			b1[n] = 8 & w;
			w >>= 1;
			b0[n] = 4 & w;
		} while ( w >= 16 );
	} while ( n );
	
	/* put the bits in the correct places */
	n = 16;
	k = rotors;
	
	do {
		w   = (b1[k[ 0   ]] | b0[k[ 1   ]]) << 4;
		w  |= (b1[k[ 2   ]] | b0[k[ 3   ]]) << 2;
		w  |=  b1[k[ 4   ]] | b0[k[ 5   ]];
		w <<= 8;
		w  |= (b1[k[ 6   ]] | b0[k[ 7   ]]) << 4;
		w  |= (b1[k[ 8   ]] | b0[k[ 9   ]]) << 2;
		w  |=  b1[k[10   ]] | b0[k[11   ]];
		w <<= 8;
		w  |= (b1[k[12   ]] | b0[k[13   ]]) << 4;
		w  |= (b1[k[14   ]] | b0[k[15   ]]) << 2;
		w  |=  b1[k[16   ]] | b0[k[17   ]];
		w <<= 8;
		w  |= (b1[k[18   ]] | b0[k[19   ]]) << 4;
		w  |= (b1[k[20   ]] | b0[k[21   ]]) << 2;
		w  |=  b1[k[22   ]] | b0[k[23   ]];
		expkey[0] = w;
		
		w   = (b1[k[ 0+24]] | b0[k[ 1+24]]) << 4;
		w  |= (b1[k[ 2+24]] | b0[k[ 3+24]]) << 2;
		w  |=  b1[k[ 4+24]] | b0[k[ 5+24]];
		w <<= 8;
		w  |= (b1[k[ 6+24]] | b0[k[ 7+24]]) << 4;
		w  |= (b1[k[ 8+24]] | b0[k[ 9+24]]) << 2;
		w  |=  b1[k[10+24]] | b0[k[11+24]];
		w <<= 8;
		w  |= (b1[k[12+24]] | b0[k[13+24]]) << 4;
		w  |= (b1[k[14+24]] | b0[k[15+24]]) << 2;
		w  |=  b1[k[16+24]] | b0[k[17+24]];
		w <<= 8;
		w  |= (b1[k[18+24]] | b0[k[19+24]]) << 4;
		w  |= (b1[k[20+24]] | b0[k[21+24]]) << 2;
		w  |=  b1[k[22+24]] | b0[k[23+24]];
		
		ROR(w, 4, 28);      /* could be eliminated */
		expkey[1] = w;

		k += 48;
		expkey += 2;
	} while (--n);

	return 0;
}

static int des_setkey(void *ctx, const u8 *key, unsigned int keylen, u32 *flags)
{
	return setkey(((struct des_ctx *)ctx)->expkey, key, keylen, flags);
}

static void des_encrypt(void *ctx, u8 *dst, const u8 *src)
{
	des_small_fips_encrypt(((struct des_ctx *)ctx)->expkey, dst, src);
}

static void des_decrypt(void *ctx, u8 *dst, const u8 *src)
{
	des_small_fips_decrypt(((struct des_ctx *)ctx)->expkey, dst, src);
}

/* 
 * RFC2451:
 *
 *   For DES-EDE3, there is no known need to reject weak or
 *   complementation keys.  Any weakness is obviated by the use of
 *   multiple keys.
 *
 *   However, if the first two or last two independent 64-bit keys are
 *   equal (k1 == k2 or k2 == k3), then the DES3 operation is simply the
 *   same as DES.  Implementers MUST reject keys that exhibit this
 *   property.
 *
 */
static int des3_ede_setkey(void *ctx, const u8 *key,
                           unsigned int keylen, u32 *flags)
{
	unsigned int i, off;
	struct des3_ede_ctx *dctx = ctx;

	if (!(memcmp(key, &key[DES_KEY_SIZE], DES_KEY_SIZE) && 
	    memcmp(&key[DES_KEY_SIZE], &key[DES_KEY_SIZE * 2],
	    					DES_KEY_SIZE))) {

		*flags |= CRYPTO_TFM_RES_BAD_KEY_SCHED;
		return -EINVAL;
	}
	
	for (i = 0, off = 0; i < 3; i++, off += DES_EXPKEY_WORDS,
							key += DES_KEY_SIZE) {
		int ret = setkey(&dctx->expkey[off], key, DES_KEY_SIZE, flags);
		if (ret < 0)
			return ret;
	}	
	return 0;
}

static void des3_ede_encrypt(void *ctx, u8 *dst, const u8 *src)
{
	struct des3_ede_ctx *dctx = ctx;
	
	des_small_fips_encrypt(dctx->expkey, dst, src);
	des_small_fips_decrypt(&dctx->expkey[DES_EXPKEY_WORDS], dst, dst);
	des_small_fips_encrypt(&dctx->expkey[DES_EXPKEY_WORDS * 2], dst, dst);
}

static void des3_ede_decrypt(void *ctx, u8 *dst, const u8 *src)
{
	struct des3_ede_ctx *dctx = ctx;

	des_small_fips_decrypt(&dctx->expkey[DES_EXPKEY_WORDS * 2], dst, src);
	des_small_fips_encrypt(&dctx->expkey[DES_EXPKEY_WORDS], dst, dst);
	des_small_fips_decrypt(dctx->expkey, dst, dst);
}

static struct crypto_alg des_alg = {
	.cra_name		=	"des",
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct des_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(des_alg.cra_list),
	.cra_u			=	{ .cipher = {
	.cia_min_keysize	=	DES_KEY_SIZE,
	.cia_max_keysize	=	DES_KEY_SIZE,
	.cia_setkey		= 	des_setkey,
	.cia_encrypt		=	des_encrypt,
	.cia_decrypt		=	des_decrypt } }
};

static struct crypto_alg des3_ede_alg = {
	.cra_name		=	"des3_ede",
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES3_EDE_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct des3_ede_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(des3_ede_alg.cra_list),
	.cra_u			=	{ .cipher = {
	.cia_min_keysize	=	DES3_EDE_KEY_SIZE,
	.cia_max_keysize	=	DES3_EDE_KEY_SIZE,
	.cia_setkey	   	= 	des3_ede_setkey,
	.cia_encrypt	 	=	des3_ede_encrypt,
	.cia_decrypt	  	=	des3_ede_decrypt } }
};

MODULE_ALIAS("des3_ede");

static int __init init(void)
{
	int ret = 0;
	
	ret = crypto_register_alg(&des_alg);
	if (ret < 0)
		goto out;

	ret = crypto_register_alg(&des3_ede_alg);
	if (ret < 0)
		crypto_unregister_alg(&des_alg);
out:	
	return ret;
}

static void __exit fini(void)
{
	crypto_unregister_alg(&des3_ede_alg);
	crypto_unregister_alg(&des_alg);
}

module_init(init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DES & Triple DES EDE Cipher Algorithms");
