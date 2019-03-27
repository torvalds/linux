/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "inner.h"

#define HASH_SIZE(cname)   br_ ## cname ## _SIZE

#define SPEED_HASH(Name, cname) \
static void \
test_speed_ ## cname(void) \
{ \
	unsigned char buf[8192]; \
	unsigned char tmp[HASH_SIZE(cname)]; \
	br_ ## cname ## _context mc; \
	int i; \
	long num; \
 \
	memset(buf, 'T', sizeof buf); \
	for (i = 0; i < 10; i ++) { \
		br_ ## cname ## _init(&mc); \
		br_ ## cname ## _update(&mc, buf, sizeof buf); \
		br_ ## cname ## _out(&mc, tmp); \
	} \
	num = 10; \
	for (;;) { \
		clock_t begin, end; \
		double tt; \
		long k; \
 \
		br_ ## cname ## _init(&mc); \
		begin = clock(); \
		for (k = num; k > 0; k --) { \
			br_ ## cname ## _update(&mc, buf, sizeof buf); \
		} \
		end = clock(); \
		br_ ## cname ## _out(&mc, tmp); \
		tt = (double)(end - begin) / CLOCKS_PER_SEC; \
		if (tt >= 2.0) { \
			printf("%-30s %8.2f MB/s\n", #Name, \
				((double)sizeof buf) * (double)num \
				/ (tt * 1000000.0)); \
			fflush(stdout); \
			return; \
		} \
		num <<= 1; \
	} \
}

#define BLOCK_SIZE(cname)   br_ ## cname ## _BLOCK_SIZE

#define SPEED_BLOCKCIPHER_CBC(Name, fname, cname, klen, dir) \
static void \
test_speed_ ## fname(void) \
{ \
	unsigned char key[klen]; \
	unsigned char buf[8192 - (8192 % BLOCK_SIZE(cname))]; \
	unsigned char iv[BLOCK_SIZE(cname)]; \
	const br_block_cbc ## dir ## _class *vt; \
	br_ ## cname ## _cbc ## dir ## _keys ec; \
	int i; \
	long num; \
 \
	memset(key, 'T', sizeof key); \
	memset(buf, 'P', sizeof buf); \
	memset(iv, 'X', sizeof iv); \
	vt = br_ ## cname ## _cbc ## dir ## _get_vtable(); \
	if (vt == NULL) { \
		printf("%-30s UNAVAILABLE\n", #Name); \
		fflush(stdout); \
		return; \
	} \
	for (i = 0; i < 10; i ++) { \
		vt->init(&ec.vtable, key, sizeof key); \
		vt->run(&ec.vtable, iv, buf, sizeof buf); \
	} \
	num = 10; \
	for (;;) { \
		clock_t begin, end; \
		double tt; \
		long k; \
 \
		vt->init(&ec.vtable, key, sizeof key); \
		begin = clock(); \
		for (k = num; k > 0; k --) { \
			vt->run(&ec.vtable, iv, buf, sizeof buf); \
		} \
		end = clock(); \
		tt = (double)(end - begin) / CLOCKS_PER_SEC; \
		if (tt >= 2.0) { \
			printf("%-30s %8.2f MB/s\n", #Name, \
				((double)sizeof buf) * (double)num \
				/ (tt * 1000000.0)); \
			fflush(stdout); \
			return; \
		} \
		num <<= 1; \
	} \
}

#define SPEED_BLOCKCIPHER_CTR(Name, fname, cname, klen) \
static void \
test_speed_ ## fname(void) \
{ \
	unsigned char key[klen]; \
	unsigned char buf[8192 - (8192 % BLOCK_SIZE(cname))]; \
	unsigned char iv[BLOCK_SIZE(cname) - 4]; \
	const br_block_ctr_class *vt; \
	br_ ## cname ## _ctr_keys ec; \
	int i; \
	long num; \
 \
	memset(key, 'T', sizeof key); \
	memset(buf, 'P', sizeof buf); \
	memset(iv, 'X', sizeof iv); \
	vt = br_ ## cname ## _ctr_get_vtable(); \
	if (vt == NULL) { \
		printf("%-30s UNAVAILABLE\n", #Name); \
		fflush(stdout); \
		return; \
	} \
	for (i = 0; i < 10; i ++) { \
		vt->init(&ec.vtable, key, sizeof key); \
		vt->run(&ec.vtable, iv, 1, buf, sizeof buf); \
	} \
	num = 10; \
	for (;;) { \
		clock_t begin, end; \
		double tt; \
		long k; \
 \
		vt->init(&ec.vtable, key, sizeof key); \
		begin = clock(); \
		for (k = num; k > 0; k --) { \
			vt->run(&ec.vtable, iv, 1, buf, sizeof buf); \
		} \
		end = clock(); \
		tt = (double)(end - begin) / CLOCKS_PER_SEC; \
		if (tt >= 2.0) { \
			printf("%-30s %8.2f MB/s\n", #Name, \
				((double)sizeof buf) * (double)num \
				/ (tt * 1000000.0)); \
			fflush(stdout); \
			return; \
		} \
		num <<= 1; \
	} \
}

#define SPEED_CHACHA20(Name, fname) \
static void \
test_speed_ ## fname(void) \
{ \
	br_chacha20_run bc; \
	unsigned char key[32]; \
	unsigned char buf[8192]; \
	unsigned char iv[12]; \
	int i; \
	long num; \
 \
	bc = br_ ## fname ## _get(); \
	if (bc == 0) { \
		printf("%-30s UNAVAILABLE\n", #Name); \
		fflush(stdout); \
		return; \
	} \
	memset(key, 'T', sizeof key); \
	memset(buf, 'P', sizeof buf); \
	memset(iv, 'X', sizeof iv); \
	for (i = 0; i < 10; i ++) { \
		bc(key, iv, i, buf, sizeof buf); \
	} \
	num = 10; \
	for (;;) { \
		clock_t begin, end; \
		double tt; \
		long k; \
 \
		begin = clock(); \
		for (k = num; k > 0; k --) { \
			bc(key, iv, (uint32_t)k, buf, sizeof buf); \
		} \
		end = clock(); \
		tt = (double)(end - begin) / CLOCKS_PER_SEC; \
		if (tt >= 2.0) { \
			printf("%-30s %8.2f MB/s\n", #Name, \
				((double)sizeof buf) * (double)num \
				/ (tt * 1000000.0)); \
			fflush(stdout); \
			return; \
		} \
		num <<= 1; \
	} \
}

SPEED_HASH(MD5, md5)
SPEED_HASH(SHA-1, sha1)
SPEED_HASH(SHA-256, sha256)
SPEED_HASH(SHA-512, sha512)

/*
 * There are no vtable selection functions for the portable implementations,
 * so we define some custom macros.
 */
#define br_aes_big_cbcenc_get_vtable()     (&br_aes_big_cbcenc_vtable)
#define br_aes_big_cbcdec_get_vtable()     (&br_aes_big_cbcdec_vtable)
#define br_aes_big_ctr_get_vtable()        (&br_aes_big_ctr_vtable)
#define br_aes_big_ctrcbc_get_vtable()     (&br_aes_big_ctrcbc_vtable)
#define br_aes_small_cbcenc_get_vtable()   (&br_aes_small_cbcenc_vtable)
#define br_aes_small_cbcdec_get_vtable()   (&br_aes_small_cbcdec_vtable)
#define br_aes_small_ctr_get_vtable()      (&br_aes_small_ctr_vtable)
#define br_aes_small_ctrcbc_get_vtable()   (&br_aes_small_ctrcbc_vtable)
#define br_aes_ct_cbcenc_get_vtable()      (&br_aes_ct_cbcenc_vtable)
#define br_aes_ct_cbcdec_get_vtable()      (&br_aes_ct_cbcdec_vtable)
#define br_aes_ct_ctr_get_vtable()         (&br_aes_ct_ctr_vtable)
#define br_aes_ct_ctrcbc_get_vtable()      (&br_aes_ct_ctrcbc_vtable)
#define br_aes_ct64_cbcenc_get_vtable()    (&br_aes_ct64_cbcenc_vtable)
#define br_aes_ct64_cbcdec_get_vtable()    (&br_aes_ct64_cbcdec_vtable)
#define br_aes_ct64_ctr_get_vtable()       (&br_aes_ct64_ctr_vtable)
#define br_aes_ct64_ctrcbc_get_vtable()    (&br_aes_ct64_ctrcbc_vtable)
#define br_chacha20_ct_get()               (&br_chacha20_ct_run)

#define SPEED_AES(iname) \
SPEED_BLOCKCIPHER_CBC(AES-128 CBC encrypt (iname), aes128_ ## iname ## _cbcenc, aes_ ## iname, 16, enc) \
SPEED_BLOCKCIPHER_CBC(AES-128 CBC decrypt (iname), aes128_ ## iname ## _cbcdec, aes_ ## iname, 16, dec) \
SPEED_BLOCKCIPHER_CBC(AES-192 CBC encrypt (iname), aes192_ ## iname ## _cbcenc, aes_ ## iname, 24, enc) \
SPEED_BLOCKCIPHER_CBC(AES-192 CBC decrypt (iname), aes192_ ## iname ## _cbcdec, aes_ ## iname, 24, dec) \
SPEED_BLOCKCIPHER_CBC(AES-256 CBC encrypt (iname), aes256_ ## iname ## _cbcenc, aes_ ## iname, 32, enc) \
SPEED_BLOCKCIPHER_CBC(AES-256 CBC decrypt (iname), aes256_ ## iname ## _cbcdec, aes_ ## iname, 32, dec) \
SPEED_BLOCKCIPHER_CTR(AES-128 CTR (iname), aes128_ ## iname ## _ctr, aes_ ## iname, 16) \
SPEED_BLOCKCIPHER_CTR(AES-192 CTR (iname), aes192_ ## iname ## _ctr, aes_ ## iname, 24) \
SPEED_BLOCKCIPHER_CTR(AES-256 CTR (iname), aes256_ ## iname ## _ctr, aes_ ## iname, 32)

SPEED_AES(big)
SPEED_AES(small)
SPEED_AES(ct)
SPEED_AES(ct64)
SPEED_AES(x86ni)
SPEED_AES(pwr8)

#define br_des_tab_cbcenc_get_vtable()     (&br_des_tab_cbcenc_vtable)
#define br_des_tab_cbcdec_get_vtable()     (&br_des_tab_cbcdec_vtable)
#define br_des_ct_cbcenc_get_vtable()      (&br_des_ct_cbcenc_vtable)
#define br_des_ct_cbcdec_get_vtable()      (&br_des_ct_cbcdec_vtable)

#define SPEED_DES(iname) \
SPEED_BLOCKCIPHER_CBC(DES CBC encrypt (iname), des_ ## iname ## _cbcenc, des_ ## iname, 8, enc) \
SPEED_BLOCKCIPHER_CBC(DES CBC decrypt (iname), des_ ## iname ## _cbcdec, des_ ## iname, 8, dec) \
SPEED_BLOCKCIPHER_CBC(3DES CBC encrypt (iname), 3des_ ## iname ## _cbcenc, des_ ## iname, 24, enc) \
SPEED_BLOCKCIPHER_CBC(3DES CBC decrypt (iname), 3des_ ## iname ## _cbcdec, des_ ## iname, 24, dec)

SPEED_DES(tab)
SPEED_DES(ct)

SPEED_CHACHA20(ChaCha20 (ct), chacha20_ct)
SPEED_CHACHA20(ChaCha20 (sse2), chacha20_sse2)

static void
test_speed_ghash_inner(char *name, br_ghash gh)
{
	unsigned char buf[8192], h[16], y[16];
	int i;
	long num;

	memset(buf, 'T', sizeof buf);
	memset(h, 'P', sizeof h);
	memset(y, 0, sizeof y);
	for (i = 0; i < 10; i ++) {
		gh(y, h, buf, sizeof buf);
	}
	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			gh(y, h, buf, sizeof buf);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 2.0) {
			printf("%-30s %8.2f MB/s\n", name,
				((double)sizeof buf) * (double)num
				/ (tt * 1000000.0));
			fflush(stdout);
			return;
		}
		num <<= 1;
	}
}

static void
test_speed_ghash_ctmul(void)
{
	test_speed_ghash_inner("GHASH (ctmul)", &br_ghash_ctmul);
}

static void
test_speed_ghash_ctmul32(void)
{
	test_speed_ghash_inner("GHASH (ctmul32)", &br_ghash_ctmul32);
}

static void
test_speed_ghash_ctmul64(void)
{
	test_speed_ghash_inner("GHASH (ctmul64)", &br_ghash_ctmul64);
}

static void
test_speed_ghash_pclmul(void)
{
	br_ghash gh;

	gh = br_ghash_pclmul_get();
	if (gh == 0) {
		printf("%-30s UNAVAILABLE\n", "GHASH (pclmul)");
		fflush(stdout);
	} else {
		test_speed_ghash_inner("GHASH (pclmul)", gh);
	}
}

static void
test_speed_ghash_pwr8(void)
{
	br_ghash gh;

	gh = br_ghash_pwr8_get();
	if (gh == 0) {
		printf("%-30s UNAVAILABLE\n", "GHASH (pwr8)");
		fflush(stdout);
	} else {
		test_speed_ghash_inner("GHASH (pwr8)", gh);
	}
}

static uint32_t
fake_chacha20(const void *key, const void *iv,
	uint32_t cc, void *data, size_t len)
{
	(void)key;
	(void)iv;
	(void)data;
	(void)len;
	return cc + (uint32_t)((len + 63) >> 6);
}

/*
 * To speed-test Poly1305, we run it with a do-nothing stub instead of
 * ChaCha20.
 */
static void
test_speed_poly1305_inner(char *name, br_poly1305_run pl)
{
	unsigned char buf[8192], key[32], iv[12], aad[13], tag[16];
	int i;
	long num;

	memset(key, 'K', sizeof key);
	memset(iv, 'I', sizeof iv);
	memset(aad, 'A', sizeof aad);
	memset(buf, 'T', sizeof buf);
	for (i = 0; i < 10; i ++) {
		pl(key, iv, buf, sizeof buf,
			aad, sizeof aad, tag, &fake_chacha20, 0);
	}
	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			pl(key, iv, buf, sizeof buf,
				aad, sizeof aad, tag, &fake_chacha20, 0);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 2.0) {
			printf("%-30s %8.2f MB/s\n", name,
				((double)sizeof buf) * (double)num
				/ (tt * 1000000.0));
			fflush(stdout);
			return;
		}
		num <<= 1;
	}
}

static void
test_speed_poly1305_ctmul(void)
{
	test_speed_poly1305_inner("Poly1305 (ctmul)", &br_poly1305_ctmul_run);
}

static void
test_speed_poly1305_ctmul32(void)
{
	test_speed_poly1305_inner("Poly1305 (ctmul32)",
		&br_poly1305_ctmul32_run);
}

static void
test_speed_poly1305_ctmulq(void)
{
	br_poly1305_run bp;

	bp = br_poly1305_ctmulq_get();
	if (bp == 0) {
		printf("%-30s UNAVAILABLE\n", "Poly1305 (ctmulq)");
	} else {
		test_speed_poly1305_inner("Poly1305 (ctmulq)", bp);
	}
}

static void
test_speed_poly1305_i15(void)
{
	test_speed_poly1305_inner("Poly1305 (i15)", &br_poly1305_i15_run);
}

static void
test_speed_eax_inner(char *name,
	const br_block_ctrcbc_class *vt, size_t key_len)
{
	unsigned char buf[8192], key[32], nonce[16], aad[16], tag[16];
	int i;
	long num;
	br_aes_gen_ctrcbc_keys ac;
	br_eax_context ec;

	if (vt == NULL) {
		printf("%-30s UNAVAILABLE\n", name);
		fflush(stdout);
		return;
	}
	memset(key, 'K', key_len);
	memset(nonce, 'N', sizeof nonce);
	memset(aad, 'A', sizeof aad);
	memset(buf, 'T', sizeof buf);
	for (i = 0; i < 10; i ++) {
		vt->init(&ac.vtable, key, key_len);
		br_eax_init(&ec, &ac.vtable);
		br_eax_reset(&ec, nonce, sizeof nonce);
		br_eax_aad_inject(&ec, aad, sizeof aad);
		br_eax_flip(&ec);
		br_eax_run(&ec, 1, buf, sizeof buf);
		br_eax_get_tag(&ec, tag);
	}
	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			vt->init(&ac.vtable, key, key_len);
			br_eax_init(&ec, &ac.vtable);
			br_eax_reset(&ec, nonce, sizeof nonce);
			br_eax_aad_inject(&ec, aad, sizeof aad);
			br_eax_flip(&ec);
			br_eax_run(&ec, 1, buf, sizeof buf);
			br_eax_get_tag(&ec, tag);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 2.0) {
			printf("%-30s %8.2f MB/s\n", name,
				((double)sizeof buf) * (double)num
				/ (tt * 1000000.0));
			fflush(stdout);
			return;
		}
		num <<= 1;
	}
}

#define SPEED_EAX(Algo, algo, keysize, impl) \
static void \
test_speed_eax_ ## algo ## keysize ## _ ## impl(void) \
{ \
	test_speed_eax_inner("EAX " #Algo "-" #keysize "(" #impl ")", \
		br_ ## algo ## _ ## impl ##  _ctrcbc_get_vtable() \
		, (keysize) >> 3); \
}

SPEED_EAX(AES, aes, 128, big)
SPEED_EAX(AES, aes, 128, small)
SPEED_EAX(AES, aes, 128, ct)
SPEED_EAX(AES, aes, 128, ct64)
SPEED_EAX(AES, aes, 128, x86ni)
SPEED_EAX(AES, aes, 128, pwr8)
SPEED_EAX(AES, aes, 192, big)
SPEED_EAX(AES, aes, 192, small)
SPEED_EAX(AES, aes, 192, ct)
SPEED_EAX(AES, aes, 192, ct64)
SPEED_EAX(AES, aes, 192, x86ni)
SPEED_EAX(AES, aes, 192, pwr8)
SPEED_EAX(AES, aes, 256, big)
SPEED_EAX(AES, aes, 256, small)
SPEED_EAX(AES, aes, 256, ct)
SPEED_EAX(AES, aes, 256, ct64)
SPEED_EAX(AES, aes, 256, x86ni)
SPEED_EAX(AES, aes, 256, pwr8)

static void
test_speed_shake_inner(int security_level)
{
	unsigned char buf[8192];
	br_shake_context sc;
	int i;
	long num;

	memset(buf, 'D', sizeof buf);
	br_shake_init(&sc, security_level);
	for (i = 0; i < 10; i ++) {
		br_shake_inject(&sc, buf, sizeof buf);
	}
	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			br_shake_inject(&sc, buf, sizeof buf);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 2.0) {
			printf("SHAKE%-3d (inject)              %8.2f MB/s\n",
				security_level,
				((double)sizeof buf) * (double)num
				/ (tt * 1000000.0));
			fflush(stdout);
			break;
		}
		num <<= 1;
	}

	br_shake_flip(&sc);
	for (i = 0; i < 10; i ++) {
		br_shake_produce(&sc, buf, sizeof buf);
	}

	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			br_shake_produce(&sc, buf, sizeof buf);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 2.0) {
			printf("SHAKE%-3d (produce)             %8.2f MB/s\n",
				security_level,
				((double)sizeof buf) * (double)num
				/ (tt * 1000000.0));
			fflush(stdout);
			break;
		}
		num <<= 1;
	}
}

static void
test_speed_shake128(void)
{
	test_speed_shake_inner(128);
}

static void
test_speed_shake256(void)
{
	test_speed_shake_inner(256);
}

static const unsigned char RSA_N[] = {
	0xE9, 0xF2, 0x4A, 0x2F, 0x96, 0xDF, 0x0A, 0x23,
	0x01, 0x85, 0xF1, 0x2C, 0xB2, 0xA8, 0xEF, 0x23,
	0xCE, 0x2E, 0xB0, 0x4E, 0x18, 0x31, 0x95, 0x5B,
	0x98, 0x2D, 0x9B, 0x8C, 0xE3, 0x1A, 0x2B, 0x96,
	0xB5, 0xC7, 0xEE, 0xED, 0x72, 0x43, 0x2D, 0xFE,
	0x7F, 0x61, 0x33, 0xEA, 0x14, 0xFC, 0xDE, 0x80,
	0x17, 0x42, 0xF0, 0xF3, 0xC3, 0xC7, 0x89, 0x47,
	0x76, 0x5B, 0xFA, 0x33, 0xC4, 0x8C, 0x94, 0xDE,
	0x6A, 0x75, 0xD8, 0x1A, 0xF4, 0x49, 0xBC, 0xF3,
	0xB7, 0x9E, 0x2C, 0x8D, 0xEC, 0x5A, 0xEE, 0xBF,
	0x4B, 0x5A, 0x7F, 0xEF, 0x21, 0x39, 0xDB, 0x1D,
	0x83, 0x5E, 0x7E, 0x2F, 0xAA, 0x5E, 0xBA, 0x28,
	0xC3, 0xA2, 0x53, 0x19, 0xFB, 0x2F, 0x78, 0x6B,
	0x14, 0x60, 0x49, 0x3C, 0xCC, 0x1B, 0xE9, 0x1E,
	0x3D, 0x10, 0xA4, 0xEB, 0x7F, 0x66, 0x98, 0xF6,
	0xC3, 0xAC, 0x35, 0xF5, 0x01, 0x84, 0xFF, 0x7D,
	0x1F, 0x72, 0xBE, 0xB4, 0xD1, 0x89, 0xC8, 0xDD,
	0x44, 0xE7, 0xB5, 0x2E, 0x2C, 0xE1, 0x85, 0xF5,
	0x15, 0x50, 0xA9, 0x08, 0xC7, 0x67, 0xD9, 0x2B,
	0x6C, 0x11, 0xB3, 0xEB, 0x28, 0x8D, 0xF4, 0xCC,
	0xE3, 0xC3, 0xC5, 0x04, 0x0E, 0x7C, 0x8D, 0xDB,
	0x39, 0x06, 0x6A, 0x74, 0x75, 0xDF, 0xA8, 0x0F,
	0xDA, 0x67, 0x5A, 0x73, 0x1E, 0xFD, 0x8E, 0x4C,
	0xEE, 0x17, 0xEE, 0x1E, 0x67, 0xDB, 0x98, 0x70,
	0x60, 0xF7, 0xB9, 0xB5, 0x1F, 0x19, 0x93, 0xD6,
	0x3F, 0x2F, 0x1F, 0xB6, 0x5B, 0x59, 0xAA, 0x85,
	0xBB, 0x25, 0xE4, 0x13, 0xEF, 0xE7, 0xB9, 0x87,
	0x9C, 0x3F, 0x5E, 0xE4, 0x08, 0xA3, 0x51, 0xCF,
	0x8B, 0xAD, 0xF4, 0xE6, 0x1A, 0x5F, 0x51, 0xDD,
	0xA8, 0xBE, 0xE8, 0xD1, 0x20, 0x19, 0x61, 0x6C,
	0x18, 0xAB, 0xCA, 0x0A, 0xD9, 0x82, 0xA6, 0x94,
	0xD5, 0x69, 0x2A, 0xF6, 0x43, 0x66, 0x31, 0x09
};

static const unsigned char RSA_E[] = {
	0x01, 0x00, 0x01
};

static const unsigned char RSA_P[] = {
	0xFD, 0x39, 0x40, 0x56, 0x20, 0x80, 0xC5, 0x81,
	0x4C, 0x5F, 0x0C, 0x1A, 0x52, 0x84, 0x03, 0x2F,
	0xCE, 0x82, 0xB0, 0xD8, 0x30, 0x23, 0x7F, 0x77,
	0x45, 0xC2, 0x01, 0xC4, 0x68, 0x96, 0x0D, 0xA7,
	0x22, 0xA9, 0x6C, 0xA9, 0x1A, 0x33, 0xE5, 0x2F,
	0xB5, 0x07, 0x9A, 0xF9, 0xEA, 0x33, 0xA5, 0xC8,
	0x96, 0x60, 0x6A, 0xCA, 0xEB, 0xE5, 0x6E, 0x09,
	0x46, 0x7E, 0x2D, 0xEF, 0x93, 0x7D, 0x56, 0xED,
	0x75, 0x70, 0x3B, 0x96, 0xC4, 0xD5, 0xDB, 0x0B,
	0x3F, 0x69, 0xDF, 0x06, 0x18, 0x76, 0xF4, 0xCF,
	0xF8, 0x84, 0x22, 0xDF, 0xBD, 0x71, 0x62, 0x7B,
	0x67, 0x99, 0xBC, 0x09, 0x95, 0x54, 0xA4, 0x98,
	0x83, 0xF5, 0xA9, 0xCF, 0x09, 0xA5, 0x1F, 0x61,
	0x25, 0xB4, 0x70, 0x6C, 0x91, 0xB8, 0xB3, 0xD0,
	0xCE, 0x9C, 0x45, 0x65, 0x9B, 0xEF, 0xD4, 0x70,
	0xBE, 0x86, 0xD2, 0x98, 0x5D, 0xEB, 0xE3, 0xFF
};

static const unsigned char RSA_Q[] = {
	0xEC, 0x82, 0xEE, 0x63, 0x5F, 0x40, 0x52, 0xDB,
	0x38, 0x7A, 0x37, 0x6A, 0x54, 0x5B, 0xD9, 0xA0,
	0x73, 0xB4, 0xBB, 0x52, 0xB2, 0x84, 0x07, 0xD0,
	0xCC, 0x82, 0x0D, 0x20, 0xB3, 0xFA, 0xD5, 0xB6,
	0x25, 0x92, 0x35, 0x4D, 0xB4, 0xC7, 0x36, 0x48,
	0xCE, 0x5E, 0x21, 0x4A, 0xA6, 0x74, 0x65, 0xF4,
	0x7D, 0x1D, 0xBC, 0x3B, 0xE2, 0xF4, 0x3E, 0x11,
	0x58, 0x10, 0x6C, 0x04, 0x46, 0x9E, 0x8D, 0x57,
	0xE0, 0x04, 0xE2, 0xEC, 0x47, 0xCF, 0xB3, 0x2A,
	0xFD, 0x4C, 0x55, 0x18, 0xDB, 0xDE, 0x3B, 0xDC,
	0xF4, 0x5B, 0xDA, 0xF3, 0x1A, 0xC8, 0x41, 0x6F,
	0x73, 0x3B, 0xFE, 0x3C, 0xA0, 0xDB, 0xBA, 0x6E,
	0x65, 0xA5, 0xE8, 0x02, 0xA5, 0x6C, 0xEA, 0x03,
	0xF6, 0x99, 0xF7, 0xCB, 0x4B, 0xB7, 0x11, 0x51,
	0x93, 0x88, 0x3F, 0xF9, 0x06, 0x85, 0xA9, 0x1E,
	0xCA, 0x64, 0xF8, 0x11, 0xA5, 0x1A, 0xCA, 0xF7
};

static const unsigned char RSA_DP[] = {
	0x77, 0x95, 0xE0, 0x02, 0x4C, 0x9B, 0x43, 0xAA,
	0xCA, 0x4C, 0x60, 0xC4, 0xD5, 0x8F, 0x2E, 0x8A,
	0x17, 0x36, 0xB5, 0x19, 0x83, 0xB2, 0x5F, 0xF2,
	0x0D, 0xE9, 0x8F, 0x38, 0x18, 0x44, 0x34, 0xF2,
	0x67, 0x76, 0x27, 0xB0, 0xBC, 0x85, 0x21, 0x89,
	0x24, 0x2F, 0x11, 0x4B, 0x51, 0x05, 0x4F, 0x17,
	0xA9, 0x9C, 0xA3, 0x12, 0x6D, 0xD1, 0x0D, 0xE4,
	0x27, 0x7C, 0x53, 0x69, 0x3E, 0xF8, 0x04, 0x63,
	0x64, 0x00, 0xBA, 0xC3, 0x7A, 0xF5, 0x9B, 0xDA,
	0x75, 0xFA, 0x23, 0xAF, 0x17, 0x42, 0xA6, 0x5E,
	0xC8, 0xF8, 0x6E, 0x17, 0xC7, 0xB9, 0x92, 0x4E,
	0xC1, 0x20, 0x63, 0x23, 0x0B, 0x78, 0xCB, 0xBA,
	0x93, 0x27, 0x23, 0x28, 0x79, 0x5F, 0x97, 0xB0,
	0x23, 0x44, 0x51, 0x8B, 0x94, 0x4D, 0xEB, 0xED,
	0x82, 0x85, 0x5E, 0x68, 0x9B, 0xF9, 0xE9, 0x13,
	0xCD, 0x86, 0x92, 0x52, 0x0E, 0x98, 0xE6, 0x35
};

static const unsigned char RSA_DQ[] = {
	0xD8, 0xDD, 0x71, 0xB3, 0x62, 0xBA, 0xBB, 0x7E,
	0xD1, 0xF9, 0x96, 0xE8, 0x83, 0xB3, 0xB9, 0x08,
	0x9C, 0x30, 0x03, 0x77, 0xDF, 0xC2, 0x9A, 0xDC,
	0x05, 0x39, 0xD6, 0xC9, 0xBE, 0xDE, 0x68, 0xA9,
	0xDD, 0x27, 0x84, 0x82, 0xDD, 0x19, 0xB1, 0x97,
	0xEE, 0xCA, 0x77, 0x22, 0x59, 0x20, 0xEF, 0xFF,
	0xCF, 0xDD, 0xBD, 0x24, 0xF8, 0x84, 0xD6, 0x88,
	0xD6, 0xC4, 0x30, 0x17, 0x77, 0x9D, 0x98, 0xA3,
	0x14, 0x01, 0xC7, 0x05, 0xBB, 0x0F, 0x23, 0x0D,
	0x6F, 0x37, 0x57, 0xEC, 0x34, 0x67, 0x41, 0x62,
	0xE8, 0x19, 0x75, 0xD9, 0x66, 0x1C, 0x6B, 0x8B,
	0xC3, 0x11, 0x26, 0x9C, 0xF7, 0x2E, 0xA3, 0x72,
	0xE8, 0xF7, 0xC8, 0x96, 0xEC, 0x92, 0xC2, 0xBD,
	0xA1, 0x98, 0x2A, 0x93, 0x99, 0xB8, 0xA2, 0x43,
	0xB7, 0xD0, 0xBE, 0x40, 0x1C, 0x8F, 0xE0, 0xB4,
	0x20, 0x07, 0x97, 0x43, 0xAE, 0xAD, 0xB3, 0x9F
};

static const unsigned char RSA_IQ[] = {
	0xB7, 0xE2, 0x60, 0xA9, 0x62, 0xEC, 0xEC, 0x0B,
	0x57, 0x02, 0x96, 0xF9, 0x36, 0x35, 0x2C, 0x37,
	0xAF, 0xC2, 0xEE, 0x71, 0x49, 0x26, 0x8E, 0x0F,
	0x27, 0xB1, 0xFA, 0x0F, 0xEA, 0xDC, 0xF0, 0x8B,
	0x53, 0x6C, 0xB2, 0x46, 0x27, 0xCD, 0x29, 0xA2,
	0x35, 0x0F, 0x5D, 0x8A, 0x3F, 0x20, 0x8C, 0x13,
	0x3D, 0xA1, 0xFF, 0x85, 0x91, 0x99, 0xE8, 0x50,
	0xED, 0xF1, 0x29, 0x00, 0xEE, 0x24, 0x90, 0xB5,
	0x5F, 0x3A, 0x74, 0x26, 0xD7, 0xA2, 0x24, 0x8D,
	0x89, 0x88, 0xD8, 0x35, 0x22, 0x22, 0x8A, 0x66,
	0x5D, 0x5C, 0xDE, 0x83, 0x8C, 0xFA, 0x27, 0xE6,
	0xB9, 0xEB, 0x72, 0x08, 0xCD, 0x53, 0x4B, 0x93,
	0x0F, 0xAD, 0xC3, 0xF8, 0x7C, 0xFE, 0x84, 0xD7,
	0x08, 0xF3, 0xBE, 0x3D, 0x60, 0x1E, 0x95, 0x8D,
	0x44, 0x5B, 0x65, 0x7E, 0xC1, 0x30, 0xC3, 0x84,
	0xC0, 0xB0, 0xFE, 0xBF, 0x28, 0x54, 0x1E, 0xC4
};

static const br_rsa_public_key RSA_PK = {
	(void *)RSA_N, sizeof RSA_N,
	(void *)RSA_E, sizeof RSA_E
};

static const br_rsa_private_key RSA_SK = {
	2048,
	(void *)RSA_P, sizeof RSA_P,
	(void *)RSA_Q, sizeof RSA_Q,
	(void *)RSA_DP, sizeof RSA_DP,
	(void *)RSA_DQ, sizeof RSA_DQ,
	(void *)RSA_IQ, sizeof RSA_IQ
};

static void
test_speed_rsa_inner(char *name,
	br_rsa_public fpub, br_rsa_private fpriv, br_rsa_keygen kgen)
{
	unsigned char tmp[sizeof RSA_N];
	int i;
	long num;
	/*
	br_hmac_drbg_context rng;
	*/
	br_aesctr_drbg_context rng;
	const br_block_ctr_class *ictr;

	memset(tmp, 'R', sizeof tmp);
	tmp[0] = 0;
	for (i = 0; i < 10; i ++) {
		if (!fpriv(tmp, &RSA_SK)) {
			abort();
		}
	}
	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			fpriv(tmp, &RSA_SK);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 2.0) {
			printf("%-30s %8.2f priv/s\n", name,
				(double)num / tt);
			fflush(stdout);
			break;
		}
		num <<= 1;
	}
	for (i = 0; i < 10; i ++) {
		if (!fpub(tmp, sizeof tmp, &RSA_PK)) {
			abort();
		}
	}
	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			fpub(tmp, sizeof tmp, &RSA_PK);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 2.0) {
			printf("%-30s %8.2f pub/s\n", name,
				(double)num / tt);
			fflush(stdout);
			break;
		}
		num <<= 1;
	}

	if (kgen == 0) {
		printf("%-30s KEYGEN UNAVAILABLE\n", name);
		fflush(stdout);
		return;
	}
	/*
	br_hmac_drbg_init(&rng, &br_sha256_vtable, "RSA keygen seed", 15);
	*/
	ictr = br_aes_x86ni_ctr_get_vtable();
	if (ictr == NULL) {
		ictr = br_aes_pwr8_ctr_get_vtable();
		if (ictr == NULL) {
#if BR_64
			ictr = &br_aes_ct64_ctr_vtable;
#else
			ictr = &br_aes_ct_ctr_vtable;
#endif
		}
	}
	br_aesctr_drbg_init(&rng, ictr, "RSA keygen seed", 15);

	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			br_rsa_private_key sk;
			unsigned char kbuf[BR_RSA_KBUF_PRIV_SIZE(1024)];

			kgen(&rng.vtable, &sk, kbuf, NULL, NULL, 1024, 0);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 10.0) {
			printf("%-30s %8.2f kgen[1024]/s\n", name,
				(double)num / tt);
			fflush(stdout);
			break;
		}
		num <<= 1;
	}

	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			br_rsa_private_key sk;
			unsigned char kbuf[BR_RSA_KBUF_PRIV_SIZE(2048)];

			kgen(&rng.vtable, &sk, kbuf, NULL, NULL, 2048, 0);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 10.0) {
			printf("%-30s %8.2f kgen[2048]/s\n", name,
				(double)num / tt);
			fflush(stdout);
			break;
		}
		num <<= 1;
	}
}

static void
test_speed_rsa_i15(void)
{
	test_speed_rsa_inner("RSA i15",
		&br_rsa_i15_public, &br_rsa_i15_private, &br_rsa_i15_keygen);
}

static void
test_speed_rsa_i31(void)
{
	test_speed_rsa_inner("RSA i31",
		&br_rsa_i31_public, &br_rsa_i31_private, &br_rsa_i31_keygen);
}

static void
test_speed_rsa_i32(void)
{
	test_speed_rsa_inner("RSA i32",
		&br_rsa_i32_public, &br_rsa_i32_private, 0);
}

static void
test_speed_rsa_i62(void)
{
	br_rsa_public pub;
	br_rsa_private priv;
	br_rsa_keygen kgen;

	pub = br_rsa_i62_public_get();
	priv = br_rsa_i62_private_get();
	kgen = br_rsa_i62_keygen_get();
	if (pub) {
		test_speed_rsa_inner("RSA i62", pub, priv, kgen);
	} else {
		printf("%-30s UNAVAILABLE\n", "RSA i62");
	}
}

static void
test_speed_ec_inner_1(const char *name,
	const br_ec_impl *impl, const br_ec_curve_def *cd)
{
	unsigned char bx[80], U[160];
	uint32_t x[22], n[22];
	size_t nlen, ulen;
	int i;
	long num;

	nlen = cd->order_len;
	br_i31_decode(n, cd->order, nlen);
	memset(bx, 'T', sizeof bx);
	br_i31_decode_reduce(x, bx, sizeof bx, n);
	br_i31_encode(bx, nlen, x);
	ulen = cd->generator_len;
	memcpy(U, cd->generator, ulen);
	for (i = 0; i < 10; i ++) {
		impl->mul(U, ulen, bx, nlen, cd->curve);
	}
	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			impl->mul(U, ulen, bx, nlen, cd->curve);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 2.0) {
			printf("%-30s %8.2f mul/s\n", name,
				(double)num / tt);
			fflush(stdout);
			break;
		}
		num <<= 1;
	}
}

static void
test_speed_ec_inner_2(const char *name,
	const br_ec_impl *impl, const br_ec_curve_def *cd)
{
	unsigned char bx[80], U[160];
	uint32_t x[22], n[22];
	size_t nlen;
	int i;
	long num;

	nlen = cd->order_len;
	br_i31_decode(n, cd->order, nlen);
	memset(bx, 'T', sizeof bx);
	br_i31_decode_reduce(x, bx, sizeof bx, n);
	br_i31_encode(bx, nlen, x);
	for (i = 0; i < 10; i ++) {
		impl->mulgen(U, bx, nlen, cd->curve);
	}
	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			impl->mulgen(U, bx, nlen, cd->curve);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 2.0) {
			printf("%-30s %8.2f mul/s\n", name,
				(double)num / tt);
			fflush(stdout);
			break;
		}
		num <<= 1;
	}
}

static void
test_speed_ec_inner(const char *name,
	const br_ec_impl *impl, const br_ec_curve_def *cd)
{
	char tmp[50];

	test_speed_ec_inner_1(name, impl, cd);
	sprintf(tmp, "%s (FP)", name);
	test_speed_ec_inner_2(tmp, impl, cd);
}

static void
test_speed_ec_p256_m15(void)
{
	test_speed_ec_inner("EC p256_m15",
		&br_ec_p256_m15, &br_secp256r1);
}

static void
test_speed_ec_p256_m31(void)
{
	test_speed_ec_inner("EC p256_m31",
		&br_ec_p256_m31, &br_secp256r1);
}

static void
test_speed_ec_p256_m62(void)
{
	const br_ec_impl *ec;

	ec = br_ec_p256_m62_get();
	if (ec != NULL) {
		test_speed_ec_inner("EC p256_m62", ec, &br_secp256r1);
	} else {
		printf("%-30s UNAVAILABLE\n", "EC p256_m62");
	}
}

static void
test_speed_ec_p256_m64(void)
{
	const br_ec_impl *ec;

	ec = br_ec_p256_m64_get();
	if (ec != NULL) {
		test_speed_ec_inner("EC p256_m64", ec, &br_secp256r1);
	} else {
		printf("%-30s UNAVAILABLE\n", "EC p256_m64");
	}
}

static void
test_speed_ec_prime_i15(void)
{
	test_speed_ec_inner("EC prime_i15 P-256",
		&br_ec_prime_i15, &br_secp256r1);
	test_speed_ec_inner("EC prime_i15 P-384",
		&br_ec_prime_i15, &br_secp384r1);
	test_speed_ec_inner("EC prime_i15 P-521",
		&br_ec_prime_i15, &br_secp521r1);
}

static void
test_speed_ec_prime_i31(void)
{
	test_speed_ec_inner("EC prime_i31 P-256",
		&br_ec_prime_i31, &br_secp256r1);
	test_speed_ec_inner("EC prime_i31 P-384",
		&br_ec_prime_i31, &br_secp384r1);
	test_speed_ec_inner("EC prime_i31 P-521",
		&br_ec_prime_i31, &br_secp521r1);
}

static void
test_speed_ec_c25519_i15(void)
{
	test_speed_ec_inner("EC c25519_i15",
		&br_ec_c25519_i15, &br_curve25519);
}

static void
test_speed_ec_c25519_i31(void)
{
	test_speed_ec_inner("EC c25519_i31",
		&br_ec_c25519_i31, &br_curve25519);
}

static void
test_speed_ec_c25519_m15(void)
{
	test_speed_ec_inner("EC c25519_m15",
		&br_ec_c25519_m15, &br_curve25519);
}

static void
test_speed_ec_c25519_m31(void)
{
	test_speed_ec_inner("EC c25519_m31",
		&br_ec_c25519_m31, &br_curve25519);
}

static void
test_speed_ec_c25519_m62(void)
{
	const br_ec_impl *ec;

	ec = br_ec_c25519_m62_get();
	if (ec != NULL) {
		test_speed_ec_inner("EC c25519_m62", ec, &br_curve25519);
	} else {
		printf("%-30s UNAVAILABLE\n", "EC c25519_m62");
	}
}

static void
test_speed_ec_c25519_m64(void)
{
	const br_ec_impl *ec;

	ec = br_ec_c25519_m64_get();
	if (ec != NULL) {
		test_speed_ec_inner("EC c25519_m64", ec, &br_curve25519);
	} else {
		printf("%-30s UNAVAILABLE\n", "EC c25519_m64");
	}
}

static void
test_speed_ecdsa_inner(const char *name,
	const br_ec_impl *impl, const br_ec_curve_def *cd,
	br_ecdsa_sign sign, br_ecdsa_vrfy vrfy)
{
	unsigned char bx[80], U[160], hv[32], sig[160];
	uint32_t x[22], n[22];
	size_t nlen, ulen, sig_len;
	int i;
	long num;
	br_ec_private_key sk;
	br_ec_public_key pk;

	nlen = cd->order_len;
	br_i31_decode(n, cd->order, nlen);
	memset(bx, 'T', sizeof bx);
	br_i31_decode_reduce(x, bx, sizeof bx, n);
	br_i31_encode(bx, nlen, x);
	ulen = cd->generator_len;
	memcpy(U, cd->generator, ulen);
	impl->mul(U, ulen, bx, nlen, cd->curve);
	sk.curve = cd->curve;
	sk.x = bx;
	sk.xlen = nlen;
	pk.curve = cd->curve;
	pk.q = U;
	pk.qlen = ulen;

	memset(hv, 'H', sizeof hv);
	sig_len = sign(impl, &br_sha256_vtable, hv, &sk, sig);
	if (vrfy(impl, hv, sizeof hv, &pk, sig, sig_len) != 1) {
		fprintf(stderr, "self-test sign/verify failed\n");
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < 10; i ++) {
		hv[1] ++;
		sign(impl, &br_sha256_vtable, hv, &sk, sig);
		vrfy(impl, hv, sizeof hv, &pk, sig, sig_len);
	}

	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			hv[1] ++;
			sig_len = sign(impl, &br_sha256_vtable, hv, &sk, sig);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 2.0) {
			printf("%-30s %8.2f sign/s\n", name,
				(double)num / tt);
			fflush(stdout);
			break;
		}
		num <<= 1;
	}

	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			vrfy(impl, hv, sizeof hv, &pk, sig, sig_len);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 2.0) {
			printf("%-30s %8.2f verify/s\n", name,
				(double)num / tt);
			fflush(stdout);
			break;
		}
		num <<= 1;
	}
}

static void
test_speed_ecdsa_p256_m15(void)
{
	test_speed_ecdsa_inner("ECDSA m15 P-256",
		&br_ec_p256_m15, &br_secp256r1,
		&br_ecdsa_i15_sign_asn1,
		&br_ecdsa_i15_vrfy_asn1);
}

static void
test_speed_ecdsa_p256_m31(void)
{
	test_speed_ecdsa_inner("ECDSA m31 P-256",
		&br_ec_p256_m31, &br_secp256r1,
		&br_ecdsa_i31_sign_asn1,
		&br_ecdsa_i31_vrfy_asn1);
}

static void
test_speed_ecdsa_p256_m62(void)
{
	const br_ec_impl *ec;

	ec = br_ec_p256_m62_get();
	if (ec != NULL) {
		test_speed_ecdsa_inner("ECDSA m62 P-256",
			ec, &br_secp256r1,
			&br_ecdsa_i31_sign_asn1,
			&br_ecdsa_i31_vrfy_asn1);
	} else {
		printf("%-30s UNAVAILABLE\n", "ECDSA m62 P-256");
	}
}

static void
test_speed_ecdsa_p256_m64(void)
{
	const br_ec_impl *ec;

	ec = br_ec_p256_m64_get();
	if (ec != NULL) {
		test_speed_ecdsa_inner("ECDSA m64 P-256",
			ec, &br_secp256r1,
			&br_ecdsa_i31_sign_asn1,
			&br_ecdsa_i31_vrfy_asn1);
	} else {
		printf("%-30s UNAVAILABLE\n", "ECDSA m64 P-256");
	}
}

static void
test_speed_ecdsa_i15(void)
{
	test_speed_ecdsa_inner("ECDSA i15 P-256",
		&br_ec_prime_i15, &br_secp256r1,
		&br_ecdsa_i15_sign_asn1,
		&br_ecdsa_i15_vrfy_asn1);
	test_speed_ecdsa_inner("ECDSA i15 P-384",
		&br_ec_prime_i15, &br_secp384r1,
		&br_ecdsa_i15_sign_asn1,
		&br_ecdsa_i15_vrfy_asn1);
	test_speed_ecdsa_inner("ECDSA i15 P-521",
		&br_ec_prime_i15, &br_secp521r1,
		&br_ecdsa_i15_sign_asn1,
		&br_ecdsa_i15_vrfy_asn1);
}

static void
test_speed_ecdsa_i31(void)
{
	test_speed_ecdsa_inner("ECDSA i31 P-256",
		&br_ec_prime_i31, &br_secp256r1,
		&br_ecdsa_i31_sign_asn1,
		&br_ecdsa_i31_vrfy_asn1);
	test_speed_ecdsa_inner("ECDSA i31 P-384",
		&br_ec_prime_i31, &br_secp384r1,
		&br_ecdsa_i31_sign_asn1,
		&br_ecdsa_i31_vrfy_asn1);
	test_speed_ecdsa_inner("ECDSA i31 P-521",
		&br_ec_prime_i31, &br_secp521r1,
		&br_ecdsa_i31_sign_asn1,
		&br_ecdsa_i31_vrfy_asn1);
}

static void
test_speed_i31(void)
{
	static const unsigned char bp[] = {
		/* A 521-bit prime integer (order of the P-521 curve). */
		0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFA, 0x51, 0x86, 0x87, 0x83, 0xBF, 0x2F,
		0x96, 0x6B, 0x7F, 0xCC, 0x01, 0x48, 0xF7, 0x09,
		0xA5, 0xD0, 0x3B, 0xB5, 0xC9, 0xB8, 0x89, 0x9C,
		0x47, 0xAE, 0xBB, 0x6F, 0xB7, 0x1E, 0x91, 0x38,
		0x64, 0x09
	};

	unsigned char tmp[60 + sizeof bp];
	uint32_t p[20], x[20], y[20], z[20], uu[60], p0i;
	int i;
	long num;

	br_i31_decode(p, bp, sizeof bp);
	p0i = br_i31_ninv31(p[1]);
	memset(tmp, 'T', sizeof tmp);
	br_i31_decode_reduce(x, tmp, sizeof tmp, p);
	memset(tmp, 'U', sizeof tmp);
	br_i31_decode_reduce(y, tmp, sizeof tmp, p);

	for (i = 0; i < 10; i ++) {
		br_i31_to_monty(x, p);
	}
	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			br_i31_to_monty(x, p);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 2.0) {
			printf("%-30s %8.2f ops/s\n", "i31 to_monty",
				(double)num / tt);
			fflush(stdout);
			break;
		}
		num <<= 1;
	}

	for (i = 0; i < 10; i ++) {
		br_i31_from_monty(x, p, p0i);
	}
	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			br_i31_from_monty(x, p, p0i);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 2.0) {
			printf("%-30s %8.2f ops/s\n", "i31 from_monty",
				(double)num / tt);
			fflush(stdout);
			break;
		}
		num <<= 1;
	}

	for (i = 0; i < 10; i ++) {
		br_i31_montymul(z, x, y, p, p0i);
	}
	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			br_i31_montymul(z, x, y, p, p0i);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 2.0) {
			printf("%-30s %8.2f ops/s\n", "i31 montymul",
				(double)num / tt);
			fflush(stdout);
			break;
		}
		num <<= 1;
	}

	for (i = 0; i < 10; i ++) {
		br_i31_moddiv(x, y, p, p0i, uu);
	}
	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			br_i31_moddiv(x, y, p, p0i, uu);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 2.0) {
			printf("%-30s %8.2f ops/s\n", "i31 moddiv",
				(double)num / tt);
			fflush(stdout);
			break;
		}
		num <<= 1;
	}
}

#if 0

static unsigned char P2048[] = {
	0xFD, 0xB6, 0xE0, 0x3E, 0x00, 0x49, 0x4C, 0xF0, 0x69, 0x3A, 0xDD, 0x7D,
	0xF8, 0xA2, 0x41, 0xB0, 0x6C, 0x67, 0xC5, 0xBA, 0xB8, 0x46, 0x80, 0xF5,
	0xBF, 0xAB, 0x98, 0xFC, 0x84, 0x73, 0xA5, 0x63, 0xC9, 0x52, 0x12, 0xDA,
	0x4C, 0xC1, 0x5B, 0x9D, 0x8D, 0xDF, 0xCD, 0xFE, 0xC5, 0xAD, 0x5A, 0x6F,
	0xDD, 0x02, 0xD9, 0xEC, 0x71, 0xEF, 0xEB, 0xB6, 0x95, 0xED, 0x94, 0x25,
	0x0E, 0x63, 0xDD, 0x6A, 0x52, 0xC7, 0x93, 0xAF, 0x85, 0x9D, 0x2C, 0xBE,
	0x5C, 0xBE, 0x35, 0xD8, 0xDD, 0x39, 0xEF, 0x1B, 0xB1, 0x49, 0x67, 0xB2,
	0x33, 0xC9, 0x7C, 0xE1, 0x51, 0x79, 0x51, 0x59, 0xCA, 0x6E, 0x2A, 0xDF,
	0x0D, 0x76, 0x1C, 0xE7, 0xA5, 0xC0, 0x1E, 0x6C, 0x56, 0x3A, 0x32, 0xE5,
	0xB5, 0xC5, 0xD4, 0xDB, 0xFE, 0xFF, 0xF8, 0xF2, 0x96, 0xA9, 0xC9, 0x65,
	0x59, 0x9E, 0x01, 0x79, 0x9D, 0x38, 0x68, 0x0F, 0xAD, 0x43, 0x3A, 0xD6,
	0x84, 0x0A, 0xE2, 0xEF, 0x96, 0xC1, 0x6D, 0x89, 0x74, 0x19, 0x63, 0x82,
	0x3B, 0xA0, 0x9C, 0xBA, 0x78, 0xDE, 0xDC, 0xC2, 0xE7, 0xD4, 0xFA, 0xD6,
	0x19, 0x21, 0x29, 0xAE, 0x5E, 0xF4, 0x38, 0x81, 0xC6, 0x9E, 0x0E, 0x3C,
	0xCD, 0xC0, 0xDC, 0x93, 0x5D, 0xFD, 0x9A, 0x5C, 0xAB, 0x54, 0x1F, 0xFF,
	0x9C, 0x12, 0x1B, 0x4C, 0xDF, 0x2D, 0x9C, 0x85, 0xF9, 0x68, 0x15, 0x89,
	0x42, 0x9B, 0x6C, 0x45, 0x89, 0x3A, 0xBC, 0xE9, 0x19, 0x91, 0xBE, 0x0C,
	0xEF, 0x90, 0xCC, 0xF6, 0xD6, 0xF0, 0x3D, 0x5C, 0xF5, 0xE5, 0x0F, 0x2F,
	0x02, 0x8A, 0x83, 0x4B, 0x93, 0x2F, 0x14, 0x12, 0x1F, 0x56, 0x9A, 0x12,
	0x58, 0x88, 0xAE, 0x60, 0xB8, 0x5A, 0xE4, 0xA1, 0xBF, 0x4A, 0x81, 0x84,
	0xAB, 0xBB, 0xE4, 0xD0, 0x1D, 0x41, 0xD9, 0x0A, 0xAB, 0x1E, 0x47, 0x5B,
	0x31, 0xAC, 0x2B, 0x73
};

static unsigned char G2048[] = {
	0x02
};

static void
test_speed_modpow(void)
{
	uint32_t mx[65], mp[65], me[65], t1[65], t2[65], len;
	unsigned char e[64];
	int i;
	long num;

	len = br_int_decode(mp, sizeof mp / sizeof mp[0],
		P2048, sizeof P2048);
	if (len != 65) {
		abort();
	}
	memset(e, 'P', sizeof e);
	if (!br_int_decode(me, sizeof me / sizeof me[0], e, sizeof e)) {
		abort();
	}
	if (!br_modint_decode(mx, mp, G2048, sizeof G2048)) {
		abort();
	}
	for (i = 0; i < 10; i ++) {
		br_modint_to_monty(mx, mp);
		br_modint_montypow(mx, me, mp, t1, t2);
		br_modint_from_monty(mx, mp);
	}
	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			br_modint_to_monty(mx, mp);
			br_modint_montypow(mx, me, mp, t1, t2);
			br_modint_from_monty(mx, mp);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 2.0) {
			printf("%-30s %8.2f exp/s\n", "pow[2048:256]",
				(double)num / tt);
			fflush(stdout);
			return;
		}
		num <<= 1;
	}
}

static void
test_speed_moddiv(void)
{
	uint32_t mx[65], my[65], mp[65], t1[65], t2[65], t3[65], len;
	unsigned char x[255], y[255];
	int i;
	long num;

	len = br_int_decode(mp, sizeof mp / sizeof mp[0],
		P2048, sizeof P2048);
	if (len != 65) {
		abort();
	}
	memset(x, 'T', sizeof x);
	memset(y, 'P', sizeof y);
	if (!br_modint_decode(mx, mp, x, sizeof x)) {
		abort();
	}
	if (!br_modint_decode(my, mp, y, sizeof y)) {
		abort();
	}
	for (i = 0; i < 10; i ++) {
		br_modint_div(mx, my, mp, t1, t2, t3);
	}
	num = 10;
	for (;;) {
		clock_t begin, end;
		double tt;
		long k;

		begin = clock();
		for (k = num; k > 0; k --) {
			br_modint_div(mx, my, mp, t1, t2, t3);
		}
		end = clock();
		tt = (double)(end - begin) / CLOCKS_PER_SEC;
		if (tt >= 2.0) {
			printf("%-30s %8.2f div/s\n", "div[2048]",
				(double)num / tt);
			fflush(stdout);
			return;
		}
		num <<= 1;
	}
}
#endif

#define STU(x)   { test_speed_ ## x, #x }

static const struct {
	void (*fn)(void);
	char *name;
} tfns[] = {
	STU(md5),
	STU(sha1),
	STU(sha256),
	STU(sha512),

	STU(aes128_big_cbcenc),
	STU(aes128_big_cbcdec),
	STU(aes192_big_cbcenc),
	STU(aes192_big_cbcdec),
	STU(aes256_big_cbcenc),
	STU(aes256_big_cbcdec),
	STU(aes128_big_ctr),
	STU(aes192_big_ctr),
	STU(aes256_big_ctr),

	STU(aes128_small_cbcenc),
	STU(aes128_small_cbcdec),
	STU(aes192_small_cbcenc),
	STU(aes192_small_cbcdec),
	STU(aes256_small_cbcenc),
	STU(aes256_small_cbcdec),
	STU(aes128_small_ctr),
	STU(aes192_small_ctr),
	STU(aes256_small_ctr),

	STU(aes128_ct_cbcenc),
	STU(aes128_ct_cbcdec),
	STU(aes192_ct_cbcenc),
	STU(aes192_ct_cbcdec),
	STU(aes256_ct_cbcenc),
	STU(aes256_ct_cbcdec),
	STU(aes128_ct_ctr),
	STU(aes192_ct_ctr),
	STU(aes256_ct_ctr),

	STU(aes128_ct64_cbcenc),
	STU(aes128_ct64_cbcdec),
	STU(aes192_ct64_cbcenc),
	STU(aes192_ct64_cbcdec),
	STU(aes256_ct64_cbcenc),
	STU(aes256_ct64_cbcdec),
	STU(aes128_ct64_ctr),
	STU(aes192_ct64_ctr),
	STU(aes256_ct64_ctr),

	STU(aes128_x86ni_cbcenc),
	STU(aes128_x86ni_cbcdec),
	STU(aes192_x86ni_cbcenc),
	STU(aes192_x86ni_cbcdec),
	STU(aes256_x86ni_cbcenc),
	STU(aes256_x86ni_cbcdec),
	STU(aes128_x86ni_ctr),
	STU(aes192_x86ni_ctr),
	STU(aes256_x86ni_ctr),

	STU(aes128_pwr8_cbcenc),
	STU(aes128_pwr8_cbcdec),
	STU(aes192_pwr8_cbcenc),
	STU(aes192_pwr8_cbcdec),
	STU(aes256_pwr8_cbcenc),
	STU(aes256_pwr8_cbcdec),
	STU(aes128_pwr8_ctr),
	STU(aes192_pwr8_ctr),
	STU(aes256_pwr8_ctr),

	STU(des_tab_cbcenc),
	STU(des_tab_cbcdec),
	STU(3des_tab_cbcenc),
	STU(3des_tab_cbcdec),

	STU(des_ct_cbcenc),
	STU(des_ct_cbcdec),
	STU(3des_ct_cbcenc),
	STU(3des_ct_cbcdec),

	STU(chacha20_ct),
	STU(chacha20_sse2),

	STU(ghash_ctmul),
	STU(ghash_ctmul32),
	STU(ghash_ctmul64),
	STU(ghash_pclmul),
	STU(ghash_pwr8),

	STU(poly1305_ctmul),
	STU(poly1305_ctmul32),
	STU(poly1305_ctmulq),
	STU(poly1305_i15),

	STU(eax_aes128_big),
	STU(eax_aes192_big),
	STU(eax_aes256_big),
	STU(eax_aes128_small),
	STU(eax_aes192_small),
	STU(eax_aes256_small),
	STU(eax_aes128_ct),
	STU(eax_aes192_ct),
	STU(eax_aes256_ct),
	STU(eax_aes128_ct64),
	STU(eax_aes192_ct64),
	STU(eax_aes256_ct64),
	STU(eax_aes128_x86ni),
	STU(eax_aes192_x86ni),
	STU(eax_aes256_x86ni),
	STU(eax_aes128_pwr8),
	STU(eax_aes192_pwr8),
	STU(eax_aes256_pwr8),

	STU(shake128),
	STU(shake256),

	STU(rsa_i15),
	STU(rsa_i31),
	STU(rsa_i32),
	STU(rsa_i62),
	STU(ec_prime_i15),
	STU(ec_prime_i31),
	STU(ec_p256_m15),
	STU(ec_p256_m31),
	STU(ec_p256_m62),
	STU(ec_p256_m64),
	STU(ec_c25519_i15),
	STU(ec_c25519_i31),
	STU(ec_c25519_m15),
	STU(ec_c25519_m31),
	STU(ec_c25519_m62),
	STU(ec_c25519_m64),
	STU(ecdsa_p256_m15),
	STU(ecdsa_p256_m31),
	STU(ecdsa_p256_m62),
	STU(ecdsa_p256_m64),
	STU(ecdsa_i15),
	STU(ecdsa_i31),

	STU(i31)
};

static int
eq_name(const char *s1, const char *s2)
{
	for (;;) {
		int c1, c2;

		for (;;) {
			c1 = *s1 ++;
			if (c1 >= 'A' && c1 <= 'Z') {
				c1 += 'a' - 'A';
			} else {
				switch (c1) {
				case '-': case '_': case '.': case ' ':
					continue;
				}
			}
			break;
		}
		for (;;) {
			c2 = *s2 ++;
			if (c2 >= 'A' && c2 <= 'Z') {
				c2 += 'a' - 'A';
			} else {
				switch (c2) {
				case '-': case '_': case '.': case ' ':
					continue;
				}
			}
			break;
		}
		if (c1 != c2) {
			return 0;
		}
		if (c1 == 0) {
			return 1;
		}
	}
}

int
main(int argc, char *argv[])
{
	size_t u;

	if (argc <= 1) {
		printf("usage: testspeed all | name...\n");
		printf("individual test names:\n");
		for (u = 0; u < (sizeof tfns) / (sizeof tfns[0]); u ++) {
			printf("   %s\n", tfns[u].name);
		}
	} else {
		for (u = 0; u < (sizeof tfns) / (sizeof tfns[0]); u ++) {
			int i;

			for (i = 1; i < argc; i ++) {
				if (eq_name(argv[i], tfns[u].name)
					|| eq_name(argv[i], "all"))
				{
					tfns[u].fn();
					break;
				}
			}
		}
	}
	return 0;
}
