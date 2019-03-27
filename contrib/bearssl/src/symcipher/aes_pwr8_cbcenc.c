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
br_aes_pwr8_cbcenc_init(br_aes_pwr8_cbcenc_keys *ctx,
	const void *key, size_t len)
{
	ctx->vtable = &br_aes_pwr8_cbcenc_vtable;
	ctx->num_rounds = br_aes_pwr8_keysched(ctx->skey.skni, key, len);
}

static void
cbcenc_128(const unsigned char *sk,
	const unsigned char *iv, unsigned char *buf, size_t len)
{
	long cc;

#if BR_POWER8_LE
	static const uint32_t idx2be[] = {
		0x03020100, 0x07060504, 0x0B0A0908, 0x0F0E0D0C
	};
#endif

	cc = 0;
	asm volatile (

		/*
		 * Load subkeys into v0..v10
		 */
		lxvw4x(32, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(33, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(34, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(35, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(36, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(37, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(38, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(39, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(40, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(41, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(42, %[cc], %[sk])

#if BR_POWER8_LE
		/*
		 * v15 = constant for byteswapping words
		 */
		lxvw4x(47, 0, %[idx2be])
#endif
		/*
		 * Load IV into v16.
		 */
		lxvw4x(48, 0, %[iv])
#if BR_POWER8_LE
		vperm(16, 16, 16, 15)
#endif

		mtctr(%[num_blocks])
	label(loop)
		/*
		 * Load next plaintext word and XOR with current IV.
		 */
		lxvw4x(49, 0, %[buf])
#if BR_POWER8_LE
		vperm(17, 17, 17, 15)
#endif
		vxor(16, 16, 17)

		/*
		 * Encrypt the block.
		 */
		vxor(16, 16, 0)
		vcipher(16, 16, 1)
		vcipher(16, 16, 2)
		vcipher(16, 16, 3)
		vcipher(16, 16, 4)
		vcipher(16, 16, 5)
		vcipher(16, 16, 6)
		vcipher(16, 16, 7)
		vcipher(16, 16, 8)
		vcipher(16, 16, 9)
		vcipherlast(16, 16, 10)

		/*
		 * Store back result (with byteswap)
		 */
#if BR_POWER8_LE
		vperm(17, 16, 16, 15)
		stxvw4x(49, 0, %[buf])
#else
		stxvw4x(48, 0, %[buf])
#endif
		addi(%[buf], %[buf], 16)

		bdnz(loop)

: [cc] "+b" (cc), [buf] "+b" (buf)
: [sk] "b" (sk), [iv] "b" (iv), [num_blocks] "b" (len >> 4)
#if BR_POWER8_LE
	, [idx2be] "b" (idx2be)
#endif
: "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9",
  "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19",
  "ctr", "memory"
	);
}

static void
cbcenc_192(const unsigned char *sk,
	const unsigned char *iv, unsigned char *buf, size_t len)
{
	long cc;

#if BR_POWER8_LE
	static const uint32_t idx2be[] = {
		0x03020100, 0x07060504, 0x0B0A0908, 0x0F0E0D0C
	};
#endif

	cc = 0;
	asm volatile (

		/*
		 * Load subkeys into v0..v12
		 */
		lxvw4x(32, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(33, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(34, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(35, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(36, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(37, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(38, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(39, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(40, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(41, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(42, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(43, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(44, %[cc], %[sk])

#if BR_POWER8_LE
		/*
		 * v15 = constant for byteswapping words
		 */
		lxvw4x(47, 0, %[idx2be])
#endif
		/*
		 * Load IV into v16.
		 */
		lxvw4x(48, 0, %[iv])
#if BR_POWER8_LE
		vperm(16, 16, 16, 15)
#endif

		mtctr(%[num_blocks])
	label(loop)
		/*
		 * Load next plaintext word and XOR with current IV.
		 */
		lxvw4x(49, 0, %[buf])
#if BR_POWER8_LE
		vperm(17, 17, 17, 15)
#endif
		vxor(16, 16, 17)

		/*
		 * Encrypt the block.
		 */
		vxor(16, 16, 0)
		vcipher(16, 16, 1)
		vcipher(16, 16, 2)
		vcipher(16, 16, 3)
		vcipher(16, 16, 4)
		vcipher(16, 16, 5)
		vcipher(16, 16, 6)
		vcipher(16, 16, 7)
		vcipher(16, 16, 8)
		vcipher(16, 16, 9)
		vcipher(16, 16, 10)
		vcipher(16, 16, 11)
		vcipherlast(16, 16, 12)

		/*
		 * Store back result (with byteswap)
		 */
#if BR_POWER8_LE
		vperm(17, 16, 16, 15)
		stxvw4x(49, 0, %[buf])
#else
		stxvw4x(48, 0, %[buf])
#endif
		addi(%[buf], %[buf], 16)

		bdnz(loop)

: [cc] "+b" (cc), [buf] "+b" (buf)
: [sk] "b" (sk), [iv] "b" (iv), [num_blocks] "b" (len >> 4)
#if BR_POWER8_LE
	, [idx2be] "b" (idx2be)
#endif
: "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9",
  "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19",
  "ctr", "memory"
	);
}

static void
cbcenc_256(const unsigned char *sk,
	const unsigned char *iv, unsigned char *buf, size_t len)
{
	long cc;

#if BR_POWER8_LE
	static const uint32_t idx2be[] = {
		0x03020100, 0x07060504, 0x0B0A0908, 0x0F0E0D0C
	};
#endif

	cc = 0;
	asm volatile (

		/*
		 * Load subkeys into v0..v14
		 */
		lxvw4x(32, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(33, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(34, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(35, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(36, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(37, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(38, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(39, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(40, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(41, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(42, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(43, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(44, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(45, %[cc], %[sk])
		addi(%[cc], %[cc], 16)
		lxvw4x(46, %[cc], %[sk])

#if BR_POWER8_LE
		/*
		 * v15 = constant for byteswapping words
		 */
		lxvw4x(47, 0, %[idx2be])
#endif
		/*
		 * Load IV into v16.
		 */
		lxvw4x(48, 0, %[iv])
#if BR_POWER8_LE
		vperm(16, 16, 16, 15)
#endif

		mtctr(%[num_blocks])
	label(loop)
		/*
		 * Load next plaintext word and XOR with current IV.
		 */
		lxvw4x(49, 0, %[buf])
#if BR_POWER8_LE
		vperm(17, 17, 17, 15)
#endif
		vxor(16, 16, 17)

		/*
		 * Encrypt the block.
		 */
		vxor(16, 16, 0)
		vcipher(16, 16, 1)
		vcipher(16, 16, 2)
		vcipher(16, 16, 3)
		vcipher(16, 16, 4)
		vcipher(16, 16, 5)
		vcipher(16, 16, 6)
		vcipher(16, 16, 7)
		vcipher(16, 16, 8)
		vcipher(16, 16, 9)
		vcipher(16, 16, 10)
		vcipher(16, 16, 11)
		vcipher(16, 16, 12)
		vcipher(16, 16, 13)
		vcipherlast(16, 16, 14)

		/*
		 * Store back result (with byteswap)
		 */
#if BR_POWER8_LE
		vperm(17, 16, 16, 15)
		stxvw4x(49, 0, %[buf])
#else
		stxvw4x(48, 0, %[buf])
#endif
		addi(%[buf], %[buf], 16)

		bdnz(loop)

: [cc] "+b" (cc), [buf] "+b" (buf)
: [sk] "b" (sk), [iv] "b" (iv), [num_blocks] "b" (len >> 4)
#if BR_POWER8_LE
	, [idx2be] "b" (idx2be)
#endif
: "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9",
  "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19",
  "ctr", "memory"
	);
}

/* see bearssl_block.h */
void
br_aes_pwr8_cbcenc_run(const br_aes_pwr8_cbcenc_keys *ctx,
	void *iv, void *data, size_t len)
{
	if (len > 0) {
		switch (ctx->num_rounds) {
		case 10:
			cbcenc_128(ctx->skey.skni, iv, data, len);
			break;
		case 12:
			cbcenc_192(ctx->skey.skni, iv, data, len);
			break;
		default:
			cbcenc_256(ctx->skey.skni, iv, data, len);
			break;
		}
		memcpy(iv, (unsigned char *)data + (len - 16), 16);
	}
}

/* see bearssl_block.h */
const br_block_cbcenc_class br_aes_pwr8_cbcenc_vtable = {
	sizeof(br_aes_pwr8_cbcenc_keys),
	16,
	4,
	(void (*)(const br_block_cbcenc_class **, const void *, size_t))
		&br_aes_pwr8_cbcenc_init,
	(void (*)(const br_block_cbcenc_class *const *, void *, void *, size_t))
		&br_aes_pwr8_cbcenc_run
};

/* see bearssl_block.h */
const br_block_cbcenc_class *
br_aes_pwr8_cbcenc_get_vtable(void)
{
	return br_aes_pwr8_supported() ? &br_aes_pwr8_cbcenc_vtable : NULL;
}

#else

/* see bearssl_block.h */
const br_block_cbcenc_class *
br_aes_pwr8_cbcenc_get_vtable(void)
{
	return NULL;
}

#endif
