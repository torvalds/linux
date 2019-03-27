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

#define BR_POWER_ASM_MACROS   1
#include "inner.h"

#if BR_POWER8

/* see bearssl_block.h */
void
br_aes_pwr8_cbcdec_init(br_aes_pwr8_cbcdec_keys *ctx,
	const void *key, size_t len)
{
	ctx->vtable = &br_aes_pwr8_cbcdec_vtable;
	ctx->num_rounds = br_aes_pwr8_keysched(ctx->skey.skni, key, len);
}

static void
cbcdec_128(const unsigned char *sk,
	const unsigned char *iv, unsigned char *buf, size_t num_blocks)
{
	long cc0, cc1, cc2, cc3;

#if BR_POWER8_LE
	static const uint32_t idx2be[] = {
		0x03020100, 0x07060504, 0x0B0A0908, 0x0F0E0D0C
	};
#endif

	cc0 = 0;
	cc1 = 16;
	cc2 = 32;
	cc3 = 48;
	asm volatile (

		/*
		 * Load subkeys into v0..v10
		 */
		lxvw4x(32, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(33, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(34, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(35, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(36, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(37, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(38, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(39, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(40, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(41, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(42, %[cc0], %[sk])
		li(%[cc0], 0)

#if BR_POWER8_LE
		/*
		 * v15 = constant for byteswapping words
		 */
		lxvw4x(47, 0, %[idx2be])
#endif
		/*
		 * Load IV into v24.
		 */
		lxvw4x(56, 0, %[iv])
#if BR_POWER8_LE
		vperm(24, 24, 24, 15)
#endif

		mtctr(%[num_blocks])
	label(loop)
		/*
		 * Load next ciphertext words in v16..v19. Also save them
		 * in v20..v23.
		 */
		lxvw4x(48, %[cc0], %[buf])
		lxvw4x(49, %[cc1], %[buf])
		lxvw4x(50, %[cc2], %[buf])
		lxvw4x(51, %[cc3], %[buf])
#if BR_POWER8_LE
		vperm(16, 16, 16, 15)
		vperm(17, 17, 17, 15)
		vperm(18, 18, 18, 15)
		vperm(19, 19, 19, 15)
#endif
		vand(20, 16, 16)
		vand(21, 17, 17)
		vand(22, 18, 18)
		vand(23, 19, 19)

		/*
		 * Decrypt the blocks.
		 */
		vxor(16, 16, 10)
		vxor(17, 17, 10)
		vxor(18, 18, 10)
		vxor(19, 19, 10)
		vncipher(16, 16, 9)
		vncipher(17, 17, 9)
		vncipher(18, 18, 9)
		vncipher(19, 19, 9)
		vncipher(16, 16, 8)
		vncipher(17, 17, 8)
		vncipher(18, 18, 8)
		vncipher(19, 19, 8)
		vncipher(16, 16, 7)
		vncipher(17, 17, 7)
		vncipher(18, 18, 7)
		vncipher(19, 19, 7)
		vncipher(16, 16, 6)
		vncipher(17, 17, 6)
		vncipher(18, 18, 6)
		vncipher(19, 19, 6)
		vncipher(16, 16, 5)
		vncipher(17, 17, 5)
		vncipher(18, 18, 5)
		vncipher(19, 19, 5)
		vncipher(16, 16, 4)
		vncipher(17, 17, 4)
		vncipher(18, 18, 4)
		vncipher(19, 19, 4)
		vncipher(16, 16, 3)
		vncipher(17, 17, 3)
		vncipher(18, 18, 3)
		vncipher(19, 19, 3)
		vncipher(16, 16, 2)
		vncipher(17, 17, 2)
		vncipher(18, 18, 2)
		vncipher(19, 19, 2)
		vncipher(16, 16, 1)
		vncipher(17, 17, 1)
		vncipher(18, 18, 1)
		vncipher(19, 19, 1)
		vncipherlast(16, 16, 0)
		vncipherlast(17, 17, 0)
		vncipherlast(18, 18, 0)
		vncipherlast(19, 19, 0)

		/*
		 * XOR decrypted blocks with IV / previous block.
		 */
		vxor(16, 16, 24)
		vxor(17, 17, 20)
		vxor(18, 18, 21)
		vxor(19, 19, 22)

		/*
		 * Store back result (with byteswap)
		 */
#if BR_POWER8_LE
		vperm(16, 16, 16, 15)
		vperm(17, 17, 17, 15)
		vperm(18, 18, 18, 15)
		vperm(19, 19, 19, 15)
#endif
		stxvw4x(48, %[cc0], %[buf])
		stxvw4x(49, %[cc1], %[buf])
		stxvw4x(50, %[cc2], %[buf])
		stxvw4x(51, %[cc3], %[buf])

		/*
		 * Fourth encrypted block is IV for next run.
		 */
		vand(24, 23, 23)

		addi(%[buf], %[buf], 64)

		bdnz(loop)

: [cc0] "+b" (cc0), [cc1] "+b" (cc1), [cc2] "+b" (cc2), [cc3] "+b" (cc3),
  [buf] "+b" (buf)
: [sk] "b" (sk), [iv] "b" (iv), [num_blocks] "b" (num_blocks >> 2)
#if BR_POWER8_LE
	, [idx2be] "b" (idx2be)
#endif
: "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9",
  "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19",
  "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29",
  "ctr", "memory"
	);
}

static void
cbcdec_192(const unsigned char *sk,
	const unsigned char *iv, unsigned char *buf, size_t num_blocks)
{
	long cc0, cc1, cc2, cc3;

#if BR_POWER8_LE
	static const uint32_t idx2be[] = {
		0x03020100, 0x07060504, 0x0B0A0908, 0x0F0E0D0C
	};
#endif

	cc0 = 0;
	cc1 = 16;
	cc2 = 32;
	cc3 = 48;
	asm volatile (

		/*
		 * Load subkeys into v0..v12
		 */
		lxvw4x(32, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(33, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(34, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(35, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(36, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(37, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(38, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(39, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(40, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(41, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(42, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(43, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(44, %[cc0], %[sk])
		li(%[cc0], 0)

#if BR_POWER8_LE
		/*
		 * v15 = constant for byteswapping words
		 */
		lxvw4x(47, 0, %[idx2be])
#endif
		/*
		 * Load IV into v24.
		 */
		lxvw4x(56, 0, %[iv])
#if BR_POWER8_LE
		vperm(24, 24, 24, 15)
#endif

		mtctr(%[num_blocks])
	label(loop)
		/*
		 * Load next ciphertext words in v16..v19. Also save them
		 * in v20..v23.
		 */
		lxvw4x(48, %[cc0], %[buf])
		lxvw4x(49, %[cc1], %[buf])
		lxvw4x(50, %[cc2], %[buf])
		lxvw4x(51, %[cc3], %[buf])
#if BR_POWER8_LE
		vperm(16, 16, 16, 15)
		vperm(17, 17, 17, 15)
		vperm(18, 18, 18, 15)
		vperm(19, 19, 19, 15)
#endif
		vand(20, 16, 16)
		vand(21, 17, 17)
		vand(22, 18, 18)
		vand(23, 19, 19)

		/*
		 * Decrypt the blocks.
		 */
		vxor(16, 16, 12)
		vxor(17, 17, 12)
		vxor(18, 18, 12)
		vxor(19, 19, 12)
		vncipher(16, 16, 11)
		vncipher(17, 17, 11)
		vncipher(18, 18, 11)
		vncipher(19, 19, 11)
		vncipher(16, 16, 10)
		vncipher(17, 17, 10)
		vncipher(18, 18, 10)
		vncipher(19, 19, 10)
		vncipher(16, 16, 9)
		vncipher(17, 17, 9)
		vncipher(18, 18, 9)
		vncipher(19, 19, 9)
		vncipher(16, 16, 8)
		vncipher(17, 17, 8)
		vncipher(18, 18, 8)
		vncipher(19, 19, 8)
		vncipher(16, 16, 7)
		vncipher(17, 17, 7)
		vncipher(18, 18, 7)
		vncipher(19, 19, 7)
		vncipher(16, 16, 6)
		vncipher(17, 17, 6)
		vncipher(18, 18, 6)
		vncipher(19, 19, 6)
		vncipher(16, 16, 5)
		vncipher(17, 17, 5)
		vncipher(18, 18, 5)
		vncipher(19, 19, 5)
		vncipher(16, 16, 4)
		vncipher(17, 17, 4)
		vncipher(18, 18, 4)
		vncipher(19, 19, 4)
		vncipher(16, 16, 3)
		vncipher(17, 17, 3)
		vncipher(18, 18, 3)
		vncipher(19, 19, 3)
		vncipher(16, 16, 2)
		vncipher(17, 17, 2)
		vncipher(18, 18, 2)
		vncipher(19, 19, 2)
		vncipher(16, 16, 1)
		vncipher(17, 17, 1)
		vncipher(18, 18, 1)
		vncipher(19, 19, 1)
		vncipherlast(16, 16, 0)
		vncipherlast(17, 17, 0)
		vncipherlast(18, 18, 0)
		vncipherlast(19, 19, 0)

		/*
		 * XOR decrypted blocks with IV / previous block.
		 */
		vxor(16, 16, 24)
		vxor(17, 17, 20)
		vxor(18, 18, 21)
		vxor(19, 19, 22)

		/*
		 * Store back result (with byteswap)
		 */
#if BR_POWER8_LE
		vperm(16, 16, 16, 15)
		vperm(17, 17, 17, 15)
		vperm(18, 18, 18, 15)
		vperm(19, 19, 19, 15)
#endif
		stxvw4x(48, %[cc0], %[buf])
		stxvw4x(49, %[cc1], %[buf])
		stxvw4x(50, %[cc2], %[buf])
		stxvw4x(51, %[cc3], %[buf])

		/*
		 * Fourth encrypted block is IV for next run.
		 */
		vand(24, 23, 23)

		addi(%[buf], %[buf], 64)

		bdnz(loop)

: [cc0] "+b" (cc0), [cc1] "+b" (cc1), [cc2] "+b" (cc2), [cc3] "+b" (cc3),
  [buf] "+b" (buf)
: [sk] "b" (sk), [iv] "b" (iv), [num_blocks] "b" (num_blocks >> 2)
#if BR_POWER8_LE
	, [idx2be] "b" (idx2be)
#endif
: "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9",
  "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19",
  "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29",
  "ctr", "memory"
	);
}

static void
cbcdec_256(const unsigned char *sk,
	const unsigned char *iv, unsigned char *buf, size_t num_blocks)
{
	long cc0, cc1, cc2, cc3;

#if BR_POWER8_LE
	static const uint32_t idx2be[] = {
		0x03020100, 0x07060504, 0x0B0A0908, 0x0F0E0D0C
	};
#endif

	cc0 = 0;
	cc1 = 16;
	cc2 = 32;
	cc3 = 48;
	asm volatile (

		/*
		 * Load subkeys into v0..v14
		 */
		lxvw4x(32, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(33, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(34, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(35, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(36, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(37, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(38, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(39, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(40, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(41, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(42, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(43, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(44, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(45, %[cc0], %[sk])
		addi(%[cc0], %[cc0], 16)
		lxvw4x(46, %[cc0], %[sk])
		li(%[cc0], 0)

#if BR_POWER8_LE
		/*
		 * v15 = constant for byteswapping words
		 */
		lxvw4x(47, 0, %[idx2be])
#endif
		/*
		 * Load IV into v24.
		 */
		lxvw4x(56, 0, %[iv])
#if BR_POWER8_LE
		vperm(24, 24, 24, 15)
#endif

		mtctr(%[num_blocks])
	label(loop)
		/*
		 * Load next ciphertext words in v16..v19. Also save them
		 * in v20..v23.
		 */
		lxvw4x(48, %[cc0], %[buf])
		lxvw4x(49, %[cc1], %[buf])
		lxvw4x(50, %[cc2], %[buf])
		lxvw4x(51, %[cc3], %[buf])
#if BR_POWER8_LE
		vperm(16, 16, 16, 15)
		vperm(17, 17, 17, 15)
		vperm(18, 18, 18, 15)
		vperm(19, 19, 19, 15)
#endif
		vand(20, 16, 16)
		vand(21, 17, 17)
		vand(22, 18, 18)
		vand(23, 19, 19)

		/*
		 * Decrypt the blocks.
		 */
		vxor(16, 16, 14)
		vxor(17, 17, 14)
		vxor(18, 18, 14)
		vxor(19, 19, 14)
		vncipher(16, 16, 13)
		vncipher(17, 17, 13)
		vncipher(18, 18, 13)
		vncipher(19, 19, 13)
		vncipher(16, 16, 12)
		vncipher(17, 17, 12)
		vncipher(18, 18, 12)
		vncipher(19, 19, 12)
		vncipher(16, 16, 11)
		vncipher(17, 17, 11)
		vncipher(18, 18, 11)
		vncipher(19, 19, 11)
		vncipher(16, 16, 10)
		vncipher(17, 17, 10)
		vncipher(18, 18, 10)
		vncipher(19, 19, 10)
		vncipher(16, 16, 9)
		vncipher(17, 17, 9)
		vncipher(18, 18, 9)
		vncipher(19, 19, 9)
		vncipher(16, 16, 8)
		vncipher(17, 17, 8)
		vncipher(18, 18, 8)
		vncipher(19, 19, 8)
		vncipher(16, 16, 7)
		vncipher(17, 17, 7)
		vncipher(18, 18, 7)
		vncipher(19, 19, 7)
		vncipher(16, 16, 6)
		vncipher(17, 17, 6)
		vncipher(18, 18, 6)
		vncipher(19, 19, 6)
		vncipher(16, 16, 5)
		vncipher(17, 17, 5)
		vncipher(18, 18, 5)
		vncipher(19, 19, 5)
		vncipher(16, 16, 4)
		vncipher(17, 17, 4)
		vncipher(18, 18, 4)
		vncipher(19, 19, 4)
		vncipher(16, 16, 3)
		vncipher(17, 17, 3)
		vncipher(18, 18, 3)
		vncipher(19, 19, 3)
		vncipher(16, 16, 2)
		vncipher(17, 17, 2)
		vncipher(18, 18, 2)
		vncipher(19, 19, 2)
		vncipher(16, 16, 1)
		vncipher(17, 17, 1)
		vncipher(18, 18, 1)
		vncipher(19, 19, 1)
		vncipherlast(16, 16, 0)
		vncipherlast(17, 17, 0)
		vncipherlast(18, 18, 0)
		vncipherlast(19, 19, 0)

		/*
		 * XOR decrypted blocks with IV / previous block.
		 */
		vxor(16, 16, 24)
		vxor(17, 17, 20)
		vxor(18, 18, 21)
		vxor(19, 19, 22)

		/*
		 * Store back result (with byteswap)
		 */
#if BR_POWER8_LE
		vperm(16, 16, 16, 15)
		vperm(17, 17, 17, 15)
		vperm(18, 18, 18, 15)
		vperm(19, 19, 19, 15)
#endif
		stxvw4x(48, %[cc0], %[buf])
		stxvw4x(49, %[cc1], %[buf])
		stxvw4x(50, %[cc2], %[buf])
		stxvw4x(51, %[cc3], %[buf])

		/*
		 * Fourth encrypted block is IV for next run.
		 */
		vand(24, 23, 23)

		addi(%[buf], %[buf], 64)

		bdnz(loop)

: [cc0] "+b" (cc0), [cc1] "+b" (cc1), [cc2] "+b" (cc2), [cc3] "+b" (cc3),
  [buf] "+b" (buf)
: [sk] "b" (sk), [iv] "b" (iv), [num_blocks] "b" (num_blocks >> 2)
#if BR_POWER8_LE
	, [idx2be] "b" (idx2be)
#endif
: "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9",
  "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19",
  "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29",
  "ctr", "memory"
	);
}

/* see bearssl_block.h */
void
br_aes_pwr8_cbcdec_run(const br_aes_pwr8_cbcdec_keys *ctx,
	void *iv, void *data, size_t len)
{
	unsigned char nextiv[16];
	unsigned char *buf;

	if (len == 0) {
		return;
	}
	buf = data;
	memcpy(nextiv, buf + len - 16, 16);
	if (len >= 64) {
		size_t num_blocks;
		unsigned char tmp[16];

		num_blocks = (len >> 4) & ~(size_t)3;
		memcpy(tmp, buf + (num_blocks << 4) - 16, 16);
		switch (ctx->num_rounds) {
		case 10:
			cbcdec_128(ctx->skey.skni, iv, buf, num_blocks);
			break;
		case 12:
			cbcdec_192(ctx->skey.skni, iv, buf, num_blocks);
			break;
		default:
			cbcdec_256(ctx->skey.skni, iv, buf, num_blocks);
			break;
		}
		buf += num_blocks << 4;
		len &= 63;
		memcpy(iv, tmp, 16);
	}
	if (len > 0) {
		unsigned char tmp[64];

		memcpy(tmp, buf, len);
		memset(tmp + len, 0, (sizeof tmp) - len);
		switch (ctx->num_rounds) {
		case 10:
			cbcdec_128(ctx->skey.skni, iv, tmp, 4);
			break;
		case 12:
			cbcdec_192(ctx->skey.skni, iv, tmp, 4);
			break;
		default:
			cbcdec_256(ctx->skey.skni, iv, tmp, 4);
			break;
		}
		memcpy(buf, tmp, len);
	}
	memcpy(iv, nextiv, 16);
}

/* see bearssl_block.h */
const br_block_cbcdec_class br_aes_pwr8_cbcdec_vtable = {
	sizeof(br_aes_pwr8_cbcdec_keys),
	16,
	4,
	(void (*)(const br_block_cbcdec_class **, const void *, size_t))
		&br_aes_pwr8_cbcdec_init,
	(void (*)(const br_block_cbcdec_class *const *, void *, void *, size_t))
		&br_aes_pwr8_cbcdec_run
};

/* see bearssl_block.h */
const br_block_cbcdec_class *
br_aes_pwr8_cbcdec_get_vtable(void)
{
	return br_aes_pwr8_supported() ? &br_aes_pwr8_cbcdec_vtable : NULL;
}

#else

/* see bearssl_block.h */
const br_block_cbcdec_class *
br_aes_pwr8_cbcdec_get_vtable(void)
{
	return NULL;
}

#endif
