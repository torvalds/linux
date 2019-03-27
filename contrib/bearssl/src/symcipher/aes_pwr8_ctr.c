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
br_aes_pwr8_ctr_init(br_aes_pwr8_ctr_keys *ctx,
	const void *key, size_t len)
{
	ctx->vtable = &br_aes_pwr8_ctr_vtable;
	ctx->num_rounds = br_aes_pwr8_keysched(ctx->skey.skni, key, len);
}

static void
ctr_128(const unsigned char *sk, const unsigned char *ivbuf,
	unsigned char *buf, size_t num_blocks)
{
	long cc0, cc1, cc2, cc3;

#if BR_POWER8_LE
	static const uint32_t idx2be[] = {
		0x03020100, 0x07060504, 0x0B0A0908, 0x0F0E0D0C
	};
#endif
	static const uint32_t ctrinc[] = {
		0, 0, 0, 4
	};

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
		 * v28 = increment for IV counter.
		 */
		lxvw4x(60, 0, %[ctrinc])

		/*
		 * Load IV into v16..v19
		 */
		lxvw4x(48, %[cc0], %[ivbuf])
		lxvw4x(49, %[cc1], %[ivbuf])
		lxvw4x(50, %[cc2], %[ivbuf])
		lxvw4x(51, %[cc3], %[ivbuf])
#if BR_POWER8_LE
		vperm(16, 16, 16, 15)
		vperm(17, 17, 17, 15)
		vperm(18, 18, 18, 15)
		vperm(19, 19, 19, 15)
#endif

		mtctr(%[num_blocks])
	label(loop)
		/*
		 * Compute next IV into v24..v27
		 */
		vadduwm(24, 16, 28)
		vadduwm(25, 17, 28)
		vadduwm(26, 18, 28)
		vadduwm(27, 19, 28)

		/*
		 * Load next data blocks. We do this early on but we
		 * won't need them until IV encryption is done.
		 */
		lxvw4x(52, %[cc0], %[buf])
		lxvw4x(53, %[cc1], %[buf])
		lxvw4x(54, %[cc2], %[buf])
		lxvw4x(55, %[cc3], %[buf])

		/*
		 * Encrypt the current IV.
		 */
		vxor(16, 16, 0)
		vxor(17, 17, 0)
		vxor(18, 18, 0)
		vxor(19, 19, 0)
		vcipher(16, 16, 1)
		vcipher(17, 17, 1)
		vcipher(18, 18, 1)
		vcipher(19, 19, 1)
		vcipher(16, 16, 2)
		vcipher(17, 17, 2)
		vcipher(18, 18, 2)
		vcipher(19, 19, 2)
		vcipher(16, 16, 3)
		vcipher(17, 17, 3)
		vcipher(18, 18, 3)
		vcipher(19, 19, 3)
		vcipher(16, 16, 4)
		vcipher(17, 17, 4)
		vcipher(18, 18, 4)
		vcipher(19, 19, 4)
		vcipher(16, 16, 5)
		vcipher(17, 17, 5)
		vcipher(18, 18, 5)
		vcipher(19, 19, 5)
		vcipher(16, 16, 6)
		vcipher(17, 17, 6)
		vcipher(18, 18, 6)
		vcipher(19, 19, 6)
		vcipher(16, 16, 7)
		vcipher(17, 17, 7)
		vcipher(18, 18, 7)
		vcipher(19, 19, 7)
		vcipher(16, 16, 8)
		vcipher(17, 17, 8)
		vcipher(18, 18, 8)
		vcipher(19, 19, 8)
		vcipher(16, 16, 9)
		vcipher(17, 17, 9)
		vcipher(18, 18, 9)
		vcipher(19, 19, 9)
		vcipherlast(16, 16, 10)
		vcipherlast(17, 17, 10)
		vcipherlast(18, 18, 10)
		vcipherlast(19, 19, 10)

#if BR_POWER8_LE
		vperm(16, 16, 16, 15)
		vperm(17, 17, 17, 15)
		vperm(18, 18, 18, 15)
		vperm(19, 19, 19, 15)
#endif

		/*
		 * Load next plaintext word and XOR with encrypted IV.
		 */
		vxor(16, 20, 16)
		vxor(17, 21, 17)
		vxor(18, 22, 18)
		vxor(19, 23, 19)
		stxvw4x(48, %[cc0], %[buf])
		stxvw4x(49, %[cc1], %[buf])
		stxvw4x(50, %[cc2], %[buf])
		stxvw4x(51, %[cc3], %[buf])

		addi(%[buf], %[buf], 64)

		/*
		 * Update IV.
		 */
		vand(16, 24, 24)
		vand(17, 25, 25)
		vand(18, 26, 26)
		vand(19, 27, 27)

		bdnz(loop)

: [cc0] "+b" (cc0), [cc1] "+b" (cc1), [cc2] "+b" (cc2), [cc3] "+b" (cc3),
  [buf] "+b" (buf)
: [sk] "b" (sk), [ivbuf] "b" (ivbuf), [num_blocks] "b" (num_blocks >> 2),
  [ctrinc] "b" (ctrinc)
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
ctr_192(const unsigned char *sk, const unsigned char *ivbuf,
	unsigned char *buf, size_t num_blocks)
{
	long cc0, cc1, cc2, cc3;

#if BR_POWER8_LE
	static const uint32_t idx2be[] = {
		0x03020100, 0x07060504, 0x0B0A0908, 0x0F0E0D0C
	};
#endif
	static const uint32_t ctrinc[] = {
		0, 0, 0, 4
	};

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
		 * v28 = increment for IV counter.
		 */
		lxvw4x(60, 0, %[ctrinc])

		/*
		 * Load IV into v16..v19
		 */
		lxvw4x(48, %[cc0], %[ivbuf])
		lxvw4x(49, %[cc1], %[ivbuf])
		lxvw4x(50, %[cc2], %[ivbuf])
		lxvw4x(51, %[cc3], %[ivbuf])
#if BR_POWER8_LE
		vperm(16, 16, 16, 15)
		vperm(17, 17, 17, 15)
		vperm(18, 18, 18, 15)
		vperm(19, 19, 19, 15)
#endif

		mtctr(%[num_blocks])
	label(loop)
		/*
		 * Compute next IV into v24..v27
		 */
		vadduwm(24, 16, 28)
		vadduwm(25, 17, 28)
		vadduwm(26, 18, 28)
		vadduwm(27, 19, 28)

		/*
		 * Load next data blocks. We do this early on but we
		 * won't need them until IV encryption is done.
		 */
		lxvw4x(52, %[cc0], %[buf])
		lxvw4x(53, %[cc1], %[buf])
		lxvw4x(54, %[cc2], %[buf])
		lxvw4x(55, %[cc3], %[buf])

		/*
		 * Encrypt the current IV.
		 */
		vxor(16, 16, 0)
		vxor(17, 17, 0)
		vxor(18, 18, 0)
		vxor(19, 19, 0)
		vcipher(16, 16, 1)
		vcipher(17, 17, 1)
		vcipher(18, 18, 1)
		vcipher(19, 19, 1)
		vcipher(16, 16, 2)
		vcipher(17, 17, 2)
		vcipher(18, 18, 2)
		vcipher(19, 19, 2)
		vcipher(16, 16, 3)
		vcipher(17, 17, 3)
		vcipher(18, 18, 3)
		vcipher(19, 19, 3)
		vcipher(16, 16, 4)
		vcipher(17, 17, 4)
		vcipher(18, 18, 4)
		vcipher(19, 19, 4)
		vcipher(16, 16, 5)
		vcipher(17, 17, 5)
		vcipher(18, 18, 5)
		vcipher(19, 19, 5)
		vcipher(16, 16, 6)
		vcipher(17, 17, 6)
		vcipher(18, 18, 6)
		vcipher(19, 19, 6)
		vcipher(16, 16, 7)
		vcipher(17, 17, 7)
		vcipher(18, 18, 7)
		vcipher(19, 19, 7)
		vcipher(16, 16, 8)
		vcipher(17, 17, 8)
		vcipher(18, 18, 8)
		vcipher(19, 19, 8)
		vcipher(16, 16, 9)
		vcipher(17, 17, 9)
		vcipher(18, 18, 9)
		vcipher(19, 19, 9)
		vcipher(16, 16, 10)
		vcipher(17, 17, 10)
		vcipher(18, 18, 10)
		vcipher(19, 19, 10)
		vcipher(16, 16, 11)
		vcipher(17, 17, 11)
		vcipher(18, 18, 11)
		vcipher(19, 19, 11)
		vcipherlast(16, 16, 12)
		vcipherlast(17, 17, 12)
		vcipherlast(18, 18, 12)
		vcipherlast(19, 19, 12)

#if BR_POWER8_LE
		vperm(16, 16, 16, 15)
		vperm(17, 17, 17, 15)
		vperm(18, 18, 18, 15)
		vperm(19, 19, 19, 15)
#endif

		/*
		 * Load next plaintext word and XOR with encrypted IV.
		 */
		vxor(16, 20, 16)
		vxor(17, 21, 17)
		vxor(18, 22, 18)
		vxor(19, 23, 19)
		stxvw4x(48, %[cc0], %[buf])
		stxvw4x(49, %[cc1], %[buf])
		stxvw4x(50, %[cc2], %[buf])
		stxvw4x(51, %[cc3], %[buf])

		addi(%[buf], %[buf], 64)

		/*
		 * Update IV.
		 */
		vand(16, 24, 24)
		vand(17, 25, 25)
		vand(18, 26, 26)
		vand(19, 27, 27)

		bdnz(loop)

: [cc0] "+b" (cc0), [cc1] "+b" (cc1), [cc2] "+b" (cc2), [cc3] "+b" (cc3),
  [buf] "+b" (buf)
: [sk] "b" (sk), [ivbuf] "b" (ivbuf), [num_blocks] "b" (num_blocks >> 2),
  [ctrinc] "b" (ctrinc)
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
ctr_256(const unsigned char *sk, const unsigned char *ivbuf,
	unsigned char *buf, size_t num_blocks)
{
	long cc0, cc1, cc2, cc3;

#if BR_POWER8_LE
	static const uint32_t idx2be[] = {
		0x03020100, 0x07060504, 0x0B0A0908, 0x0F0E0D0C
	};
#endif
	static const uint32_t ctrinc[] = {
		0, 0, 0, 4
	};

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
		 * v28 = increment for IV counter.
		 */
		lxvw4x(60, 0, %[ctrinc])

		/*
		 * Load IV into v16..v19
		 */
		lxvw4x(48, %[cc0], %[ivbuf])
		lxvw4x(49, %[cc1], %[ivbuf])
		lxvw4x(50, %[cc2], %[ivbuf])
		lxvw4x(51, %[cc3], %[ivbuf])
#if BR_POWER8_LE
		vperm(16, 16, 16, 15)
		vperm(17, 17, 17, 15)
		vperm(18, 18, 18, 15)
		vperm(19, 19, 19, 15)
#endif

		mtctr(%[num_blocks])
	label(loop)
		/*
		 * Compute next IV into v24..v27
		 */
		vadduwm(24, 16, 28)
		vadduwm(25, 17, 28)
		vadduwm(26, 18, 28)
		vadduwm(27, 19, 28)

		/*
		 * Load next data blocks. We do this early on but we
		 * won't need them until IV encryption is done.
		 */
		lxvw4x(52, %[cc0], %[buf])
		lxvw4x(53, %[cc1], %[buf])
		lxvw4x(54, %[cc2], %[buf])
		lxvw4x(55, %[cc3], %[buf])

		/*
		 * Encrypt the current IV.
		 */
		vxor(16, 16, 0)
		vxor(17, 17, 0)
		vxor(18, 18, 0)
		vxor(19, 19, 0)
		vcipher(16, 16, 1)
		vcipher(17, 17, 1)
		vcipher(18, 18, 1)
		vcipher(19, 19, 1)
		vcipher(16, 16, 2)
		vcipher(17, 17, 2)
		vcipher(18, 18, 2)
		vcipher(19, 19, 2)
		vcipher(16, 16, 3)
		vcipher(17, 17, 3)
		vcipher(18, 18, 3)
		vcipher(19, 19, 3)
		vcipher(16, 16, 4)
		vcipher(17, 17, 4)
		vcipher(18, 18, 4)
		vcipher(19, 19, 4)
		vcipher(16, 16, 5)
		vcipher(17, 17, 5)
		vcipher(18, 18, 5)
		vcipher(19, 19, 5)
		vcipher(16, 16, 6)
		vcipher(17, 17, 6)
		vcipher(18, 18, 6)
		vcipher(19, 19, 6)
		vcipher(16, 16, 7)
		vcipher(17, 17, 7)
		vcipher(18, 18, 7)
		vcipher(19, 19, 7)
		vcipher(16, 16, 8)
		vcipher(17, 17, 8)
		vcipher(18, 18, 8)
		vcipher(19, 19, 8)
		vcipher(16, 16, 9)
		vcipher(17, 17, 9)
		vcipher(18, 18, 9)
		vcipher(19, 19, 9)
		vcipher(16, 16, 10)
		vcipher(17, 17, 10)
		vcipher(18, 18, 10)
		vcipher(19, 19, 10)
		vcipher(16, 16, 11)
		vcipher(17, 17, 11)
		vcipher(18, 18, 11)
		vcipher(19, 19, 11)
		vcipher(16, 16, 12)
		vcipher(17, 17, 12)
		vcipher(18, 18, 12)
		vcipher(19, 19, 12)
		vcipher(16, 16, 13)
		vcipher(17, 17, 13)
		vcipher(18, 18, 13)
		vcipher(19, 19, 13)
		vcipherlast(16, 16, 14)
		vcipherlast(17, 17, 14)
		vcipherlast(18, 18, 14)
		vcipherlast(19, 19, 14)

#if BR_POWER8_LE
		vperm(16, 16, 16, 15)
		vperm(17, 17, 17, 15)
		vperm(18, 18, 18, 15)
		vperm(19, 19, 19, 15)
#endif

		/*
		 * Load next plaintext word and XOR with encrypted IV.
		 */
		vxor(16, 20, 16)
		vxor(17, 21, 17)
		vxor(18, 22, 18)
		vxor(19, 23, 19)
		stxvw4x(48, %[cc0], %[buf])
		stxvw4x(49, %[cc1], %[buf])
		stxvw4x(50, %[cc2], %[buf])
		stxvw4x(51, %[cc3], %[buf])

		addi(%[buf], %[buf], 64)

		/*
		 * Update IV.
		 */
		vand(16, 24, 24)
		vand(17, 25, 25)
		vand(18, 26, 26)
		vand(19, 27, 27)

		bdnz(loop)

: [cc0] "+b" (cc0), [cc1] "+b" (cc1), [cc2] "+b" (cc2), [cc3] "+b" (cc3),
  [buf] "+b" (buf)
: [sk] "b" (sk), [ivbuf] "b" (ivbuf), [num_blocks] "b" (num_blocks >> 2),
  [ctrinc] "b" (ctrinc)
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
uint32_t
br_aes_pwr8_ctr_run(const br_aes_pwr8_ctr_keys *ctx,
	const void *iv, uint32_t cc, void *data, size_t len)
{
	unsigned char *buf;
	unsigned char ivbuf[64];

	buf = data;
	memcpy(ivbuf +  0, iv, 12);
	memcpy(ivbuf + 16, iv, 12);
	memcpy(ivbuf + 32, iv, 12);
	memcpy(ivbuf + 48, iv, 12);
	if (len >= 64) {
		br_enc32be(ivbuf + 12, cc + 0);
		br_enc32be(ivbuf + 28, cc + 1);
		br_enc32be(ivbuf + 44, cc + 2);
		br_enc32be(ivbuf + 60, cc + 3);
		switch (ctx->num_rounds) {
		case 10:
			ctr_128(ctx->skey.skni, ivbuf, buf,
				(len >> 4) & ~(size_t)3);
			break;
		case 12:
			ctr_192(ctx->skey.skni, ivbuf, buf,
				(len >> 4) & ~(size_t)3);
			break;
		default:
			ctr_256(ctx->skey.skni, ivbuf, buf,
				(len >> 4) & ~(size_t)3);
			break;
		}
		cc += (len >> 4) & ~(size_t)3;
		buf += len & ~(size_t)63;
		len &= 63;
	}
	if (len > 0) {
		unsigned char tmp[64];

		memcpy(tmp, buf, len);
		memset(tmp + len, 0, (sizeof tmp) - len);
		br_enc32be(ivbuf + 12, cc + 0);
		br_enc32be(ivbuf + 28, cc + 1);
		br_enc32be(ivbuf + 44, cc + 2);
		br_enc32be(ivbuf + 60, cc + 3);
		switch (ctx->num_rounds) {
		case 10:
			ctr_128(ctx->skey.skni, ivbuf, tmp, 4);
			break;
		case 12:
			ctr_192(ctx->skey.skni, ivbuf, tmp, 4);
			break;
		default:
			ctr_256(ctx->skey.skni, ivbuf, tmp, 4);
			break;
		}
		memcpy(buf, tmp, len);
		cc += (len + 15) >> 4;
	}
	return cc;
}

/* see bearssl_block.h */
const br_block_ctr_class br_aes_pwr8_ctr_vtable = {
	sizeof(br_aes_pwr8_ctr_keys),
	16,
	4,
	(void (*)(const br_block_ctr_class **, const void *, size_t))
		&br_aes_pwr8_ctr_init,
	(uint32_t (*)(const br_block_ctr_class *const *,
		const void *, uint32_t, void *, size_t))
		&br_aes_pwr8_ctr_run
};

/* see bearssl_block.h */
const br_block_ctr_class *
br_aes_pwr8_ctr_get_vtable(void)
{
	return br_aes_pwr8_supported() ? &br_aes_pwr8_ctr_vtable : NULL;
}

#else

/* see bearssl_block.h */
const br_block_ctr_class *
br_aes_pwr8_ctr_get_vtable(void)
{
	return NULL;
}

#endif
