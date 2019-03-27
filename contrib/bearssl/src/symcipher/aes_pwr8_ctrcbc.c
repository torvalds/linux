/*
 * Copyright (c) 2018 Thomas Pornin <pornin@bolet.org>
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
const br_block_ctrcbc_class *
br_aes_pwr8_ctrcbc_get_vtable(void)
{
	return br_aes_pwr8_supported() ? &br_aes_pwr8_ctrcbc_vtable : NULL;
}

/* see bearssl_block.h */
void
br_aes_pwr8_ctrcbc_init(br_aes_pwr8_ctrcbc_keys *ctx,
	const void *key, size_t len)
{
	ctx->vtable = &br_aes_pwr8_ctrcbc_vtable;
	ctx->num_rounds = br_aes_pwr8_keysched(ctx->skey.skni, key, len);
}

/*
 * Register conventions for CTR + CBC-MAC:
 *
 *   AES subkeys are in registers 0 to 10/12/14 (depending on keys size)
 *   Register v15 contains the byteswap index register (little-endian only)
 *   Register v16 contains the CTR counter value
 *   Register v17 contains the CBC-MAC current value
 *   Registers v18 to v27 are scratch
 *   Counter increment uses v28, v29 and v30
 *
 * For CTR alone:
 *  
 *   AES subkeys are in registers 0 to 10/12/14 (depending on keys size)
 *   Register v15 contains the byteswap index register (little-endian only)
 *   Registers v16 to v19 contain the CTR counter values (four blocks)
 *   Registers v20 to v27 are scratch
 *   Counter increment uses v28, v29 and v30
 */

#define LOAD_SUBKEYS_128 \
		lxvw4x(32, %[cc], %[sk])   \
		addi(%[cc], %[cc], 16)     \
		lxvw4x(33, %[cc], %[sk])   \
		addi(%[cc], %[cc], 16)     \
		lxvw4x(34, %[cc], %[sk])   \
		addi(%[cc], %[cc], 16)     \
		lxvw4x(35, %[cc], %[sk])   \
		addi(%[cc], %[cc], 16)     \
		lxvw4x(36, %[cc], %[sk])   \
		addi(%[cc], %[cc], 16)     \
		lxvw4x(37, %[cc], %[sk])   \
		addi(%[cc], %[cc], 16)     \
		lxvw4x(38, %[cc], %[sk])   \
		addi(%[cc], %[cc], 16)     \
		lxvw4x(39, %[cc], %[sk])   \
		addi(%[cc], %[cc], 16)     \
		lxvw4x(40, %[cc], %[sk])   \
		addi(%[cc], %[cc], 16)     \
		lxvw4x(41, %[cc], %[sk])   \
		addi(%[cc], %[cc], 16)     \
		lxvw4x(42, %[cc], %[sk])

#define LOAD_SUBKEYS_192 \
		LOAD_SUBKEYS_128 \
		addi(%[cc], %[cc], 16)     \
		lxvw4x(43, %[cc], %[sk])   \
		addi(%[cc], %[cc], 16)     \
		lxvw4x(44, %[cc], %[sk])

#define LOAD_SUBKEYS_256 \
		LOAD_SUBKEYS_192 \
		addi(%[cc], %[cc], 16)     \
		lxvw4x(45, %[cc], %[sk])   \
		addi(%[cc], %[cc], 16)     \
		lxvw4x(46, %[cc], %[sk])

#define BLOCK_ENCRYPT_128(x) \
		vxor(x, x, 0) \
		vcipher(x, x, 1) \
		vcipher(x, x, 2) \
		vcipher(x, x, 3) \
		vcipher(x, x, 4) \
		vcipher(x, x, 5) \
		vcipher(x, x, 6) \
		vcipher(x, x, 7) \
		vcipher(x, x, 8) \
		vcipher(x, x, 9) \
		vcipherlast(x, x, 10)

#define BLOCK_ENCRYPT_192(x) \
		vxor(x, x, 0) \
		vcipher(x, x, 1) \
		vcipher(x, x, 2) \
		vcipher(x, x, 3) \
		vcipher(x, x, 4) \
		vcipher(x, x, 5) \
		vcipher(x, x, 6) \
		vcipher(x, x, 7) \
		vcipher(x, x, 8) \
		vcipher(x, x, 9) \
		vcipher(x, x, 10) \
		vcipher(x, x, 11) \
		vcipherlast(x, x, 12)

#define BLOCK_ENCRYPT_256(x) \
		vxor(x, x, 0) \
		vcipher(x, x, 1) \
		vcipher(x, x, 2) \
		vcipher(x, x, 3) \
		vcipher(x, x, 4) \
		vcipher(x, x, 5) \
		vcipher(x, x, 6) \
		vcipher(x, x, 7) \
		vcipher(x, x, 8) \
		vcipher(x, x, 9) \
		vcipher(x, x, 10) \
		vcipher(x, x, 11) \
		vcipher(x, x, 12) \
		vcipher(x, x, 13) \
		vcipherlast(x, x, 14)

#define BLOCK_ENCRYPT_X2_128(x, y) \
		vxor(x, x, 0) \
		vxor(y, y, 0) \
		vcipher(x, x, 1) \
		vcipher(y, y, 1) \
		vcipher(x, x, 2) \
		vcipher(y, y, 2) \
		vcipher(x, x, 3) \
		vcipher(y, y, 3) \
		vcipher(x, x, 4) \
		vcipher(y, y, 4) \
		vcipher(x, x, 5) \
		vcipher(y, y, 5) \
		vcipher(x, x, 6) \
		vcipher(y, y, 6) \
		vcipher(x, x, 7) \
		vcipher(y, y, 7) \
		vcipher(x, x, 8) \
		vcipher(y, y, 8) \
		vcipher(x, x, 9) \
		vcipher(y, y, 9) \
		vcipherlast(x, x, 10) \
		vcipherlast(y, y, 10)

#define BLOCK_ENCRYPT_X2_192(x, y) \
		vxor(x, x, 0) \
		vxor(y, y, 0) \
		vcipher(x, x, 1) \
		vcipher(y, y, 1) \
		vcipher(x, x, 2) \
		vcipher(y, y, 2) \
		vcipher(x, x, 3) \
		vcipher(y, y, 3) \
		vcipher(x, x, 4) \
		vcipher(y, y, 4) \
		vcipher(x, x, 5) \
		vcipher(y, y, 5) \
		vcipher(x, x, 6) \
		vcipher(y, y, 6) \
		vcipher(x, x, 7) \
		vcipher(y, y, 7) \
		vcipher(x, x, 8) \
		vcipher(y, y, 8) \
		vcipher(x, x, 9) \
		vcipher(y, y, 9) \
		vcipher(x, x, 10) \
		vcipher(y, y, 10) \
		vcipher(x, x, 11) \
		vcipher(y, y, 11) \
		vcipherlast(x, x, 12) \
		vcipherlast(y, y, 12)

#define BLOCK_ENCRYPT_X2_256(x, y) \
		vxor(x, x, 0) \
		vxor(y, y, 0) \
		vcipher(x, x, 1) \
		vcipher(y, y, 1) \
		vcipher(x, x, 2) \
		vcipher(y, y, 2) \
		vcipher(x, x, 3) \
		vcipher(y, y, 3) \
		vcipher(x, x, 4) \
		vcipher(y, y, 4) \
		vcipher(x, x, 5) \
		vcipher(y, y, 5) \
		vcipher(x, x, 6) \
		vcipher(y, y, 6) \
		vcipher(x, x, 7) \
		vcipher(y, y, 7) \
		vcipher(x, x, 8) \
		vcipher(y, y, 8) \
		vcipher(x, x, 9) \
		vcipher(y, y, 9) \
		vcipher(x, x, 10) \
		vcipher(y, y, 10) \
		vcipher(x, x, 11) \
		vcipher(y, y, 11) \
		vcipher(x, x, 12) \
		vcipher(y, y, 12) \
		vcipher(x, x, 13) \
		vcipher(y, y, 13) \
		vcipherlast(x, x, 14) \
		vcipherlast(y, y, 14)

#define BLOCK_ENCRYPT_X4_128(x0, x1, x2, x3) \
		vxor(x0, x0, 0) \
		vxor(x1, x1, 0) \
		vxor(x2, x2, 0) \
		vxor(x3, x3, 0) \
		vcipher(x0, x0, 1) \
		vcipher(x1, x1, 1) \
		vcipher(x2, x2, 1) \
		vcipher(x3, x3, 1) \
		vcipher(x0, x0, 2) \
		vcipher(x1, x1, 2) \
		vcipher(x2, x2, 2) \
		vcipher(x3, x3, 2) \
		vcipher(x0, x0, 3) \
		vcipher(x1, x1, 3) \
		vcipher(x2, x2, 3) \
		vcipher(x3, x3, 3) \
		vcipher(x0, x0, 4) \
		vcipher(x1, x1, 4) \
		vcipher(x2, x2, 4) \
		vcipher(x3, x3, 4) \
		vcipher(x0, x0, 5) \
		vcipher(x1, x1, 5) \
		vcipher(x2, x2, 5) \
		vcipher(x3, x3, 5) \
		vcipher(x0, x0, 6) \
		vcipher(x1, x1, 6) \
		vcipher(x2, x2, 6) \
		vcipher(x3, x3, 6) \
		vcipher(x0, x0, 7) \
		vcipher(x1, x1, 7) \
		vcipher(x2, x2, 7) \
		vcipher(x3, x3, 7) \
		vcipher(x0, x0, 8) \
		vcipher(x1, x1, 8) \
		vcipher(x2, x2, 8) \
		vcipher(x3, x3, 8) \
		vcipher(x0, x0, 9) \
		vcipher(x1, x1, 9) \
		vcipher(x2, x2, 9) \
		vcipher(x3, x3, 9) \
		vcipherlast(x0, x0, 10) \
		vcipherlast(x1, x1, 10) \
		vcipherlast(x2, x2, 10) \
		vcipherlast(x3, x3, 10)

#define BLOCK_ENCRYPT_X4_192(x0, x1, x2, x3) \
		vxor(x0, x0, 0) \
		vxor(x1, x1, 0) \
		vxor(x2, x2, 0) \
		vxor(x3, x3, 0) \
		vcipher(x0, x0, 1) \
		vcipher(x1, x1, 1) \
		vcipher(x2, x2, 1) \
		vcipher(x3, x3, 1) \
		vcipher(x0, x0, 2) \
		vcipher(x1, x1, 2) \
		vcipher(x2, x2, 2) \
		vcipher(x3, x3, 2) \
		vcipher(x0, x0, 3) \
		vcipher(x1, x1, 3) \
		vcipher(x2, x2, 3) \
		vcipher(x3, x3, 3) \
		vcipher(x0, x0, 4) \
		vcipher(x1, x1, 4) \
		vcipher(x2, x2, 4) \
		vcipher(x3, x3, 4) \
		vcipher(x0, x0, 5) \
		vcipher(x1, x1, 5) \
		vcipher(x2, x2, 5) \
		vcipher(x3, x3, 5) \
		vcipher(x0, x0, 6) \
		vcipher(x1, x1, 6) \
		vcipher(x2, x2, 6) \
		vcipher(x3, x3, 6) \
		vcipher(x0, x0, 7) \
		vcipher(x1, x1, 7) \
		vcipher(x2, x2, 7) \
		vcipher(x3, x3, 7) \
		vcipher(x0, x0, 8) \
		vcipher(x1, x1, 8) \
		vcipher(x2, x2, 8) \
		vcipher(x3, x3, 8) \
		vcipher(x0, x0, 9) \
		vcipher(x1, x1, 9) \
		vcipher(x2, x2, 9) \
		vcipher(x3, x3, 9) \
		vcipher(x0, x0, 10) \
		vcipher(x1, x1, 10) \
		vcipher(x2, x2, 10) \
		vcipher(x3, x3, 10) \
		vcipher(x0, x0, 11) \
		vcipher(x1, x1, 11) \
		vcipher(x2, x2, 11) \
		vcipher(x3, x3, 11) \
		vcipherlast(x0, x0, 12) \
		vcipherlast(x1, x1, 12) \
		vcipherlast(x2, x2, 12) \
		vcipherlast(x3, x3, 12)

#define BLOCK_ENCRYPT_X4_256(x0, x1, x2, x3) \
		vxor(x0, x0, 0) \
		vxor(x1, x1, 0) \
		vxor(x2, x2, 0) \
		vxor(x3, x3, 0) \
		vcipher(x0, x0, 1) \
		vcipher(x1, x1, 1) \
		vcipher(x2, x2, 1) \
		vcipher(x3, x3, 1) \
		vcipher(x0, x0, 2) \
		vcipher(x1, x1, 2) \
		vcipher(x2, x2, 2) \
		vcipher(x3, x3, 2) \
		vcipher(x0, x0, 3) \
		vcipher(x1, x1, 3) \
		vcipher(x2, x2, 3) \
		vcipher(x3, x3, 3) \
		vcipher(x0, x0, 4) \
		vcipher(x1, x1, 4) \
		vcipher(x2, x2, 4) \
		vcipher(x3, x3, 4) \
		vcipher(x0, x0, 5) \
		vcipher(x1, x1, 5) \
		vcipher(x2, x2, 5) \
		vcipher(x3, x3, 5) \
		vcipher(x0, x0, 6) \
		vcipher(x1, x1, 6) \
		vcipher(x2, x2, 6) \
		vcipher(x3, x3, 6) \
		vcipher(x0, x0, 7) \
		vcipher(x1, x1, 7) \
		vcipher(x2, x2, 7) \
		vcipher(x3, x3, 7) \
		vcipher(x0, x0, 8) \
		vcipher(x1, x1, 8) \
		vcipher(x2, x2, 8) \
		vcipher(x3, x3, 8) \
		vcipher(x0, x0, 9) \
		vcipher(x1, x1, 9) \
		vcipher(x2, x2, 9) \
		vcipher(x3, x3, 9) \
		vcipher(x0, x0, 10) \
		vcipher(x1, x1, 10) \
		vcipher(x2, x2, 10) \
		vcipher(x3, x3, 10) \
		vcipher(x0, x0, 11) \
		vcipher(x1, x1, 11) \
		vcipher(x2, x2, 11) \
		vcipher(x3, x3, 11) \
		vcipher(x0, x0, 12) \
		vcipher(x1, x1, 12) \
		vcipher(x2, x2, 12) \
		vcipher(x3, x3, 12) \
		vcipher(x0, x0, 13) \
		vcipher(x1, x1, 13) \
		vcipher(x2, x2, 13) \
		vcipher(x3, x3, 13) \
		vcipherlast(x0, x0, 14) \
		vcipherlast(x1, x1, 14) \
		vcipherlast(x2, x2, 14) \
		vcipherlast(x3, x3, 14)

#if BR_POWER8_LE
static const uint32_t idx2be[] = {
	0x03020100, 0x07060504, 0x0B0A0908, 0x0F0E0D0C
};
#define BYTESWAP_INIT     lxvw4x(47, 0, %[idx2be])
#define BYTESWAP(x)       vperm(x, x, x, 15)
#define BYTESWAPX(d, s)   vperm(d, s, s, 15)
#define BYTESWAP_REG      , [idx2be] "b" (idx2be)
#else
#define BYTESWAP_INIT
#define BYTESWAP(x)
#define BYTESWAPX(d, s)   vand(d, s, s)
#define BYTESWAP_REG
#endif

static const uint32_t ctrinc[] = {
	0, 0, 0, 1
};
static const uint32_t ctrinc_x4[] = {
	0, 0, 0, 4
};
#define INCR_128_INIT      lxvw4x(60, 0, %[ctrinc])
#define INCR_128_X4_INIT   lxvw4x(60, 0, %[ctrinc_x4])
#define INCR_128(d, s) \
		vaddcuw(29, s, 28) \
		vadduwm(d, s, 28) \
		vsldoi(30, 29, 29, 4) \
		vaddcuw(29, d, 30) \
		vadduwm(d, d, 30) \
		vsldoi(30, 29, 29, 4) \
		vaddcuw(29, d, 30) \
		vadduwm(d, d, 30) \
		vsldoi(30, 29, 29, 4) \
		vadduwm(d, d, 30)

#define MKCTR(size) \
static void \
ctr_ ## size(const unsigned char *sk, \
	unsigned char *ctrbuf, unsigned char *buf, size_t num_blocks_x4) \
{ \
	long cc, cc0, cc1, cc2, cc3; \
 \
	cc = 0; \
	cc0 = 0; \
	cc1 = 16; \
	cc2 = 32; \
	cc3 = 48; \
	asm volatile ( \
 \
		/* \
		 * Load subkeys into v0..v10 \
		 */ \
		LOAD_SUBKEYS_ ## size \
		li(%[cc], 0) \
 \
		BYTESWAP_INIT \
		INCR_128_X4_INIT \
 \
		/* \
		 * Load current CTR counters into v16 to v19. \
		 */ \
		lxvw4x(48, %[cc0], %[ctrbuf]) \
		lxvw4x(49, %[cc1], %[ctrbuf]) \
		lxvw4x(50, %[cc2], %[ctrbuf]) \
		lxvw4x(51, %[cc3], %[ctrbuf]) \
		BYTESWAP(16) \
		BYTESWAP(17) \
		BYTESWAP(18) \
		BYTESWAP(19) \
 \
		mtctr(%[num_blocks_x4]) \
 \
	label(loop) \
		/* \
		 * Compute next counter values into v20..v23. \
		 */ \
		INCR_128(20, 16) \
		INCR_128(21, 17) \
		INCR_128(22, 18) \
		INCR_128(23, 19) \
 \
		/* \
		 * Encrypt counter values and XOR into next data blocks. \
		 */ \
		lxvw4x(56, %[cc0], %[buf]) \
		lxvw4x(57, %[cc1], %[buf]) \
		lxvw4x(58, %[cc2], %[buf]) \
		lxvw4x(59, %[cc3], %[buf]) \
		BYTESWAP(24) \
		BYTESWAP(25) \
		BYTESWAP(26) \
		BYTESWAP(27) \
		BLOCK_ENCRYPT_X4_ ## size(16, 17, 18, 19) \
		vxor(16, 16, 24) \
		vxor(17, 17, 25) \
		vxor(18, 18, 26) \
		vxor(19, 19, 27) \
		BYTESWAP(16) \
		BYTESWAP(17) \
		BYTESWAP(18) \
		BYTESWAP(19) \
		stxvw4x(48, %[cc0], %[buf]) \
		stxvw4x(49, %[cc1], %[buf]) \
		stxvw4x(50, %[cc2], %[buf]) \
		stxvw4x(51, %[cc3], %[buf]) \
 \
		/* \
		 * Update counters and data pointer. \
		 */ \
		vand(16, 20, 20) \
		vand(17, 21, 21) \
		vand(18, 22, 22) \
		vand(19, 23, 23) \
		addi(%[buf], %[buf], 64) \
 \
		bdnz(loop) \
 \
		/* \
		 * Write back new counter values. \
		 */ \
		BYTESWAP(16) \
		BYTESWAP(17) \
		BYTESWAP(18) \
		BYTESWAP(19) \
		stxvw4x(48, %[cc0], %[ctrbuf]) \
		stxvw4x(49, %[cc1], %[ctrbuf]) \
		stxvw4x(50, %[cc2], %[ctrbuf]) \
		stxvw4x(51, %[cc3], %[ctrbuf]) \
 \
: [cc] "+b" (cc), [buf] "+b" (buf), \
	[cc0] "+b" (cc0), [cc1] "+b" (cc1), [cc2] "+b" (cc2), [cc3] "+b" (cc3) \
: [sk] "b" (sk), [ctrbuf] "b" (ctrbuf), \
	[num_blocks_x4] "b" (num_blocks_x4), [ctrinc_x4] "b" (ctrinc_x4) \
	BYTESWAP_REG \
: "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", \
  "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", \
  "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", \
  "v30", "ctr", "memory" \
	); \
}

MKCTR(128)
MKCTR(192)
MKCTR(256)

#define MKCBCMAC(size) \
static void \
cbcmac_ ## size(const unsigned char *sk, \
	unsigned char *cbcmac, const unsigned char *buf, size_t num_blocks) \
{ \
	long cc; \
 \
	cc = 0; \
	asm volatile ( \
 \
		/* \
		 * Load subkeys into v0..v10 \
		 */ \
		LOAD_SUBKEYS_ ## size \
		li(%[cc], 0) \
 \
		BYTESWAP_INIT \
 \
		/* \
		 * Load current CBC-MAC value into v16. \
		 */ \
		lxvw4x(48, %[cc], %[cbcmac]) \
		BYTESWAP(16) \
 \
		mtctr(%[num_blocks]) \
 \
	label(loop) \
		/* \
		 * Load next block, XOR into current CBC-MAC value, \
		 * and then encrypt it. \
		 */ \
		lxvw4x(49, %[cc], %[buf]) \
		BYTESWAP(17) \
		vxor(16, 16, 17) \
		BLOCK_ENCRYPT_ ## size(16) \
		addi(%[buf], %[buf], 16) \
 \
		bdnz(loop) \
 \
		/* \
		 * Write back new CBC-MAC value. \
		 */ \
		BYTESWAP(16) \
		stxvw4x(48, %[cc], %[cbcmac]) \
 \
: [cc] "+b" (cc), [buf] "+b" (buf) \
: [sk] "b" (sk), [cbcmac] "b" (cbcmac), [num_blocks] "b" (num_blocks) \
	BYTESWAP_REG \
: "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", \
  "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", \
  "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", \
  "v30", "ctr", "memory" \
	); \
}

MKCBCMAC(128)
MKCBCMAC(192)
MKCBCMAC(256)

#define MKENCRYPT(size) \
static void \
ctrcbc_ ## size ## _encrypt(const unsigned char *sk, \
	unsigned char *ctr, unsigned char *cbcmac, unsigned char *buf, \
	size_t num_blocks) \
{ \
	long cc; \
 \
	cc = 0; \
	asm volatile ( \
 \
		/* \
		 * Load subkeys into v0..v10 \
		 */ \
		LOAD_SUBKEYS_ ## size \
		li(%[cc], 0) \
 \
		BYTESWAP_INIT \
		INCR_128_INIT \
 \
		/* \
		 * Load current CTR counter into v16, and current \
		 * CBC-MAC IV into v17. \
		 */ \
		lxvw4x(48, %[cc], %[ctr]) \
		lxvw4x(49, %[cc], %[cbcmac]) \
		BYTESWAP(16) \
		BYTESWAP(17) \
 \
		/* \
		 * At each iteration, we do two parallel encryption: \
		 *  - new counter value for encryption of the next block; \
		 *  - CBC-MAC over the previous encrypted block. \
		 * Thus, each plaintext block implies two AES instances, \
		 * over two successive iterations. This requires a single \
		 * counter encryption before the loop, and a single \
		 * CBC-MAC encryption after the loop. \
		 */ \
 \
		/* \
		 * Encrypt first block (into v20). \
		 */ \
		lxvw4x(52, %[cc], %[buf]) \
		BYTESWAP(20) \
		INCR_128(22, 16) \
		BLOCK_ENCRYPT_ ## size(16) \
		vxor(20, 20, 16) \
		BYTESWAPX(21, 20) \
		stxvw4x(53, %[cc], %[buf]) \
		vand(16, 22, 22) \
		addi(%[buf], %[buf], 16) \
 \
		/* \
		 * Load loop counter; skip the loop if there is only \
		 * one block in total (already handled by the boundary \
		 * conditions). \
		 */ \
		mtctr(%[num_blocks]) \
		bdz(fastexit) \
 \
	label(loop) \
		/* \
		 * Upon loop entry: \
		 *    v16   counter value for next block \
		 *    v17   current CBC-MAC value \
		 *    v20   encrypted previous block \
		 */ \
		vxor(17, 17, 20) \
		INCR_128(22, 16) \
		lxvw4x(52, %[cc], %[buf]) \
		BYTESWAP(20) \
		BLOCK_ENCRYPT_X2_ ## size(16, 17) \
		vxor(20, 20, 16) \
		BYTESWAPX(21, 20) \
		stxvw4x(53, %[cc], %[buf]) \
		addi(%[buf], %[buf], 16) \
		vand(16, 22, 22) \
 \
		bdnz(loop) \
 \
	label(fastexit) \
		vxor(17, 17, 20) \
		BLOCK_ENCRYPT_ ## size(17) \
		BYTESWAP(16) \
		BYTESWAP(17) \
		stxvw4x(48, %[cc], %[ctr]) \
		stxvw4x(49, %[cc], %[cbcmac]) \
 \
: [cc] "+b" (cc), [buf] "+b" (buf) \
: [sk] "b" (sk), [ctr] "b" (ctr), [cbcmac] "b" (cbcmac), \
	[num_blocks] "b" (num_blocks), [ctrinc] "b" (ctrinc) \
	BYTESWAP_REG \
: "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", \
  "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", \
  "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", \
  "v30", "ctr", "memory" \
	); \
}

MKENCRYPT(128)
MKENCRYPT(192)
MKENCRYPT(256)

#define MKDECRYPT(size) \
static void \
ctrcbc_ ## size ## _decrypt(const unsigned char *sk, \
	unsigned char *ctr, unsigned char *cbcmac, unsigned char *buf, \
	size_t num_blocks) \
{ \
	long cc; \
 \
	cc = 0; \
	asm volatile ( \
 \
		/* \
		 * Load subkeys into v0..v10 \
		 */ \
		LOAD_SUBKEYS_ ## size \
		li(%[cc], 0) \
 \
		BYTESWAP_INIT \
		INCR_128_INIT \
 \
		/* \
		 * Load current CTR counter into v16, and current \
		 * CBC-MAC IV into v17. \
		 */ \
		lxvw4x(48, %[cc], %[ctr]) \
		lxvw4x(49, %[cc], %[cbcmac]) \
		BYTESWAP(16) \
		BYTESWAP(17) \
 \
		/* \
		 * At each iteration, we do two parallel encryption: \
		 *  - new counter value for decryption of the next block; \
		 *  - CBC-MAC over the next encrypted block. \
		 * Each iteration performs the two AES instances related \
		 * to the current block; there is thus no need for some \
		 * extra pre-loop and post-loop work as in encryption. \
		 */ \
 \
		mtctr(%[num_blocks]) \
 \
	label(loop) \
		/* \
		 * Upon loop entry: \
		 *    v16   counter value for next block \
		 *    v17   current CBC-MAC value \
		 */ \
		lxvw4x(52, %[cc], %[buf]) \
		BYTESWAP(20) \
		vxor(17, 17, 20) \
		INCR_128(22, 16) \
		BLOCK_ENCRYPT_X2_ ## size(16, 17) \
		vxor(20, 20, 16) \
		BYTESWAPX(21, 20) \
		stxvw4x(53, %[cc], %[buf]) \
		addi(%[buf], %[buf], 16) \
		vand(16, 22, 22) \
 \
		bdnz(loop) \
 \
		/* \
		 * Store back counter and CBC-MAC value. \
		 */ \
		BYTESWAP(16) \
		BYTESWAP(17) \
		stxvw4x(48, %[cc], %[ctr]) \
		stxvw4x(49, %[cc], %[cbcmac]) \
 \
: [cc] "+b" (cc), [buf] "+b" (buf) \
: [sk] "b" (sk), [ctr] "b" (ctr), [cbcmac] "b" (cbcmac), \
	[num_blocks] "b" (num_blocks), [ctrinc] "b" (ctrinc) \
	BYTESWAP_REG \
: "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", \
  "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", \
  "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", \
  "v30", "ctr", "memory" \
	); \
}

MKDECRYPT(128)
MKDECRYPT(192)
MKDECRYPT(256)

/* see bearssl_block.h */
void
br_aes_pwr8_ctrcbc_encrypt(const br_aes_pwr8_ctrcbc_keys *ctx,
	void *ctr, void *cbcmac, void *data, size_t len)
{
	if (len == 0) {
		return;
	}
	switch (ctx->num_rounds) {
	case 10:
		ctrcbc_128_encrypt(ctx->skey.skni, ctr, cbcmac, data, len >> 4);
		break;
	case 12:
		ctrcbc_192_encrypt(ctx->skey.skni, ctr, cbcmac, data, len >> 4);
		break;
	default:
		ctrcbc_256_encrypt(ctx->skey.skni, ctr, cbcmac, data, len >> 4);
		break;
	}
}

/* see bearssl_block.h */
void
br_aes_pwr8_ctrcbc_decrypt(const br_aes_pwr8_ctrcbc_keys *ctx,
	void *ctr, void *cbcmac, void *data, size_t len)
{
	if (len == 0) {
		return;
	}
	switch (ctx->num_rounds) {
	case 10:
		ctrcbc_128_decrypt(ctx->skey.skni, ctr, cbcmac, data, len >> 4);
		break;
	case 12:
		ctrcbc_192_decrypt(ctx->skey.skni, ctr, cbcmac, data, len >> 4);
		break;
	default:
		ctrcbc_256_decrypt(ctx->skey.skni, ctr, cbcmac, data, len >> 4);
		break;
	}
}

static inline void
incr_ctr(void *dst, const void *src)
{
	uint64_t hi, lo;

	hi = br_dec64be(src);
	lo = br_dec64be((const unsigned char *)src + 8);
	lo ++;
	hi += ((lo | -lo) >> 63) ^ (uint64_t)1;
	br_enc64be(dst, hi);
	br_enc64be((unsigned char *)dst + 8, lo);
}

/* see bearssl_block.h */
void
br_aes_pwr8_ctrcbc_ctr(const br_aes_pwr8_ctrcbc_keys *ctx,
	void *ctr, void *data, size_t len)
{
	unsigned char ctrbuf[64];

	memcpy(ctrbuf, ctr, 16);
	incr_ctr(ctrbuf + 16, ctrbuf);
	incr_ctr(ctrbuf + 32, ctrbuf + 16);
	incr_ctr(ctrbuf + 48, ctrbuf + 32);
	if (len >= 64) {
		switch (ctx->num_rounds) {
		case 10:
			ctr_128(ctx->skey.skni, ctrbuf, data, len >> 6);
			break;
		case 12:
			ctr_192(ctx->skey.skni, ctrbuf, data, len >> 6);
			break;
		default:
			ctr_256(ctx->skey.skni, ctrbuf, data, len >> 6);
			break;
		}
		data = (unsigned char *)data + (len & ~(size_t)63);
		len &= 63;
	}
	if (len > 0) {
		unsigned char tmp[64];

		if (len >= 32) {
			if (len >= 48) {
				memcpy(ctr, ctrbuf + 48, 16);
			} else {
				memcpy(ctr, ctrbuf + 32, 16);
			}
		} else {
			if (len >= 16) {
				memcpy(ctr, ctrbuf + 16, 16);
			}
		}
		memcpy(tmp, data, len);
		memset(tmp + len, 0, (sizeof tmp) - len);
		switch (ctx->num_rounds) {
		case 10:
			ctr_128(ctx->skey.skni, ctrbuf, tmp, 1);
			break;
		case 12:
			ctr_192(ctx->skey.skni, ctrbuf, tmp, 1);
			break;
		default:
			ctr_256(ctx->skey.skni, ctrbuf, tmp, 1);
			break;
		}
		memcpy(data, tmp, len);
	} else {
		memcpy(ctr, ctrbuf, 16);
	}
}

/* see bearssl_block.h */
void
br_aes_pwr8_ctrcbc_mac(const br_aes_pwr8_ctrcbc_keys *ctx,
	void *cbcmac, const void *data, size_t len)
{
	if (len > 0) {
		switch (ctx->num_rounds) {
		case 10:
			cbcmac_128(ctx->skey.skni, cbcmac, data, len >> 4);
			break;
		case 12:
			cbcmac_192(ctx->skey.skni, cbcmac, data, len >> 4);
			break;
		default:
			cbcmac_256(ctx->skey.skni, cbcmac, data, len >> 4);
			break;
		}
	}
}

/* see bearssl_block.h */
const br_block_ctrcbc_class br_aes_pwr8_ctrcbc_vtable = {
	sizeof(br_aes_pwr8_ctrcbc_keys),
	16,
	4,
	(void (*)(const br_block_ctrcbc_class **, const void *, size_t))
		&br_aes_pwr8_ctrcbc_init,
	(void (*)(const br_block_ctrcbc_class *const *,
		void *, void *, void *, size_t))
		&br_aes_pwr8_ctrcbc_encrypt,
	(void (*)(const br_block_ctrcbc_class *const *,
		void *, void *, void *, size_t))
		&br_aes_pwr8_ctrcbc_decrypt,
	(void (*)(const br_block_ctrcbc_class *const *,
		void *, void *, size_t))
		&br_aes_pwr8_ctrcbc_ctr,
	(void (*)(const br_block_ctrcbc_class *const *,
		void *, const void *, size_t))
		&br_aes_pwr8_ctrcbc_mac
};

#else

/* see bearssl_block.h */
const br_block_ctrcbc_class *
br_aes_pwr8_ctrcbc_get_vtable(void)
{
	return NULL;
}

#endif
