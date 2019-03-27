/* 	$OpenBSD: test_sshbuf_getput_crypto.c,v 1.1 2014/04/30 05:32:00 djm Exp $ */
/*
 * Regress test for sshbuf.h buffer API
 *
 * Placed in the public domain
 */

#include "includes.h"

#include <sys/types.h>
#include <sys/param.h>
#include <stdio.h>
#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif
#include <stdlib.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/objects.h>
#ifdef OPENSSL_HAS_NISTP256
# include <openssl/ec.h>
#endif

#include "../test_helper/test_helper.h"
#include "ssherr.h"
#include "sshbuf.h"

void sshbuf_getput_crypto_tests(void);

void
sshbuf_getput_crypto_tests(void)
{
	struct sshbuf *p1;
	BIGNUM *bn, *bn2;
	/* This one has num_bits != num_bytes * 8 to test bignum1 encoding */
	const char *hexbn1 = "0102030405060708090a0b0c0d0e0f10";
	/* This one has MSB set to test bignum2 encoding negative-avoidance */
	const char *hexbn2 = "f0e0d0c0b0a0908070605040302010007fff11";
	u_char expbn1[] = {
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
	};
	u_char expbn2[] = {
		0xf0, 0xe0, 0xd0, 0xc0, 0xb0, 0xa0, 0x90, 0x80,
		0x70, 0x60, 0x50, 0x40, 0x30, 0x20, 0x10, 0x00,
		0x7f, 0xff, 0x11
	};
#if defined(OPENSSL_HAS_ECC) && defined(OPENSSL_HAS_NISTP256)
	const u_char *d;
	size_t s;
	BIGNUM *bn_x, *bn_y;
	int ec256_nid = NID_X9_62_prime256v1;
	char *ec256_x = "0C828004839D0106AA59575216191357"
		        "34B451459DADB586677EF9DF55784999";
	char *ec256_y = "4D196B50F0B4E94B3C73E3A9D4CD9DF2"
	                "C8F9A35E42BDD047550F69D80EC23CD4";
	u_char expec256[] = {
		0x04,
		0x0c, 0x82, 0x80, 0x04, 0x83, 0x9d, 0x01, 0x06,
		0xaa, 0x59, 0x57, 0x52, 0x16, 0x19, 0x13, 0x57,
		0x34, 0xb4, 0x51, 0x45, 0x9d, 0xad, 0xb5, 0x86,
		0x67, 0x7e, 0xf9, 0xdf, 0x55, 0x78, 0x49, 0x99,
		0x4d, 0x19, 0x6b, 0x50, 0xf0, 0xb4, 0xe9, 0x4b,
		0x3c, 0x73, 0xe3, 0xa9, 0xd4, 0xcd, 0x9d, 0xf2,
		0xc8, 0xf9, 0xa3, 0x5e, 0x42, 0xbd, 0xd0, 0x47,
		0x55, 0x0f, 0x69, 0xd8, 0x0e, 0xc2, 0x3c, 0xd4
	};
	EC_KEY *eck;
	EC_POINT *ecp;
#endif
	int r;

#define MKBN(b, bnn) \
	do { \
		bnn = NULL; \
		ASSERT_INT_GT(BN_hex2bn(&bnn, b), 0); \
	} while (0)

	TEST_START("sshbuf_put_bignum1");
	MKBN(hexbn1, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_bignum1(p1, bn), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), sizeof(expbn1) + 2);
	ASSERT_U16_EQ(PEEK_U16(sshbuf_ptr(p1)), (u_int16_t)BN_num_bits(bn));
	ASSERT_MEM_EQ(sshbuf_ptr(p1) + 2, expbn1, sizeof(expbn1));
	BN_free(bn);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_put_bignum1 limited");
	MKBN(hexbn1, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_set_max_size(p1, sizeof(expbn1) + 1), 0);
	r = sshbuf_put_bignum1(p1, bn);
	ASSERT_INT_EQ(r, SSH_ERR_NO_BUFFER_SPACE);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 0);
	BN_free(bn);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_put_bignum1 bn2");
	MKBN(hexbn2, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_bignum1(p1, bn), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), sizeof(expbn2) + 2);
	ASSERT_U16_EQ(PEEK_U16(sshbuf_ptr(p1)), (u_int16_t)BN_num_bits(bn));
	ASSERT_MEM_EQ(sshbuf_ptr(p1) + 2, expbn2, sizeof(expbn2));
	BN_free(bn);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_put_bignum1 bn2 limited");
	MKBN(hexbn2, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_set_max_size(p1, sizeof(expbn1) + 1), 0);
	r = sshbuf_put_bignum1(p1, bn);
	ASSERT_INT_EQ(r, SSH_ERR_NO_BUFFER_SPACE);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 0);
	BN_free(bn);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_put_bignum2");
	MKBN(hexbn1, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_bignum2(p1, bn), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), sizeof(expbn1) + 4);
	ASSERT_U32_EQ(PEEK_U32(sshbuf_ptr(p1)), (u_int32_t)BN_num_bytes(bn));
	ASSERT_MEM_EQ(sshbuf_ptr(p1) + 4, expbn1, sizeof(expbn1));
	BN_free(bn);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_put_bignum2 limited");
	MKBN(hexbn1, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_set_max_size(p1, sizeof(expbn1) + 3), 0);
	r = sshbuf_put_bignum2(p1, bn);
	ASSERT_INT_EQ(r, SSH_ERR_NO_BUFFER_SPACE);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 0);
	BN_free(bn);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_put_bignum2 bn2");
	MKBN(hexbn2, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_bignum2(p1, bn), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), sizeof(expbn2) + 4 + 1); /* MSB */
	ASSERT_U32_EQ(PEEK_U32(sshbuf_ptr(p1)), (u_int32_t)BN_num_bytes(bn) + 1);
	ASSERT_U8_EQ(*(sshbuf_ptr(p1) + 4), 0x00);
	ASSERT_MEM_EQ(sshbuf_ptr(p1) + 5, expbn2, sizeof(expbn2));
	BN_free(bn);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_put_bignum2 bn2 limited");
	MKBN(hexbn2, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_set_max_size(p1, sizeof(expbn2) + 3), 0);
	r = sshbuf_put_bignum2(p1, bn);
	ASSERT_INT_EQ(r, SSH_ERR_NO_BUFFER_SPACE);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 0);
	BN_free(bn);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_get_bignum1");
	MKBN(hexbn1, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_u16(p1, BN_num_bits(bn)), 0);
	ASSERT_INT_EQ(sshbuf_put(p1, expbn1, sizeof(expbn1)), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 2 + sizeof(expbn1));
	ASSERT_INT_EQ(sshbuf_put_u16(p1, 0xd00f), 0);
	bn2 = BN_new();
	ASSERT_INT_EQ(sshbuf_get_bignum1(p1, bn2), 0);
	ASSERT_BIGNUM_EQ(bn, bn2);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 2);
	BN_free(bn);
	BN_free(bn2);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_get_bignum1 truncated");
	MKBN(hexbn1, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_u16(p1, BN_num_bits(bn)), 0);
	ASSERT_INT_EQ(sshbuf_put(p1, expbn1, sizeof(expbn1) - 1), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 2 + sizeof(expbn1) - 1);
	bn2 = BN_new();
	r = sshbuf_get_bignum1(p1, bn2);
	ASSERT_INT_EQ(r, SSH_ERR_MESSAGE_INCOMPLETE);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 2 + sizeof(expbn1) - 1);
	BN_free(bn);
	BN_free(bn2);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_get_bignum1 giant");
	MKBN(hexbn1, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_u16(p1, 0xffff), 0);
	ASSERT_INT_EQ(sshbuf_reserve(p1, (0xffff + 7) / 8, NULL), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 2 + ((0xffff + 7) / 8));
	bn2 = BN_new();
	r = sshbuf_get_bignum1(p1, bn2);
	ASSERT_INT_EQ(r, SSH_ERR_BIGNUM_TOO_LARGE);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 2 + ((0xffff + 7) / 8));
	BN_free(bn);
	BN_free(bn2);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_get_bignum1 bn2");
	MKBN(hexbn2, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_u16(p1, BN_num_bits(bn)), 0);
	ASSERT_INT_EQ(sshbuf_put(p1, expbn2, sizeof(expbn2)), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 2 + sizeof(expbn2));
	ASSERT_INT_EQ(sshbuf_put_u16(p1, 0xd00f), 0);
	bn2 = BN_new();
	ASSERT_INT_EQ(sshbuf_get_bignum1(p1, bn2), 0);
	ASSERT_BIGNUM_EQ(bn, bn2);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 2);
	BN_free(bn);
	BN_free(bn2);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_get_bignum1 bn2 truncated");
	MKBN(hexbn2, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_u16(p1, BN_num_bits(bn)), 0);
	ASSERT_INT_EQ(sshbuf_put(p1, expbn2, sizeof(expbn2) - 1), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 2 + sizeof(expbn2) - 1);
	bn2 = BN_new();
	r = sshbuf_get_bignum1(p1, bn2);
	ASSERT_INT_EQ(r, SSH_ERR_MESSAGE_INCOMPLETE);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 2 + sizeof(expbn2) - 1);
	BN_free(bn);
	BN_free(bn2);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_get_bignum2");
	MKBN(hexbn1, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_u32(p1, BN_num_bytes(bn)), 0);
	ASSERT_INT_EQ(sshbuf_put(p1, expbn1, sizeof(expbn1)), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 4 + sizeof(expbn1));
	ASSERT_INT_EQ(sshbuf_put_u16(p1, 0xd00f), 0);
	bn2 = BN_new();
	ASSERT_INT_EQ(sshbuf_get_bignum2(p1, bn2), 0);
	ASSERT_BIGNUM_EQ(bn, bn2);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 2);
	BN_free(bn);
	BN_free(bn2);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_get_bignum2 truncated");
	MKBN(hexbn1, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_u32(p1, BN_num_bytes(bn)), 0);
	ASSERT_INT_EQ(sshbuf_put(p1, expbn1, sizeof(expbn1) - 1), 0);
	bn2 = BN_new();
	r = sshbuf_get_bignum2(p1, bn2);
	ASSERT_INT_EQ(r, SSH_ERR_MESSAGE_INCOMPLETE);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), sizeof(expbn1) + 3);
	BN_free(bn);
	BN_free(bn2);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_get_bignum2 giant");
	MKBN(hexbn1, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_u32(p1, 65536), 0);
	ASSERT_INT_EQ(sshbuf_reserve(p1, 65536, NULL), 0);
	bn2 = BN_new();
	r = sshbuf_get_bignum2(p1, bn2);
	ASSERT_INT_EQ(r, SSH_ERR_BIGNUM_TOO_LARGE);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 65536 + 4);
	BN_free(bn);
	BN_free(bn2);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_get_bignum2 bn2");
	MKBN(hexbn2, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_u32(p1, BN_num_bytes(bn) + 1), 0); /* MSB */
	ASSERT_INT_EQ(sshbuf_put_u8(p1, 0x00), 0);
	ASSERT_INT_EQ(sshbuf_put(p1, expbn2, sizeof(expbn2)), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 4 + 1 + sizeof(expbn2));
	ASSERT_INT_EQ(sshbuf_put_u16(p1, 0xd00f), 0);
	bn2 = BN_new();
	ASSERT_INT_EQ(sshbuf_get_bignum2(p1, bn2), 0);
	ASSERT_BIGNUM_EQ(bn, bn2);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 2);
	BN_free(bn);
	BN_free(bn2);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_get_bignum2 bn2 truncated");
	MKBN(hexbn2, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_u32(p1, BN_num_bytes(bn) + 1), 0);
	ASSERT_INT_EQ(sshbuf_put_u8(p1, 0x00), 0);
	ASSERT_INT_EQ(sshbuf_put(p1, expbn2, sizeof(expbn2) - 1), 0);
	bn2 = BN_new();
	r = sshbuf_get_bignum2(p1, bn2);
	ASSERT_INT_EQ(r, SSH_ERR_MESSAGE_INCOMPLETE);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), sizeof(expbn2) + 1 + 4 - 1);
	BN_free(bn);
	BN_free(bn2);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_get_bignum2 bn2 negative");
	MKBN(hexbn2, bn);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_u32(p1, BN_num_bytes(bn)), 0);
	ASSERT_INT_EQ(sshbuf_put(p1, expbn2, sizeof(expbn2)), 0);
	bn2 = BN_new();
	r = sshbuf_get_bignum2(p1, bn2);
	ASSERT_INT_EQ(r, SSH_ERR_BIGNUM_IS_NEGATIVE);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), sizeof(expbn2) + 4);
	BN_free(bn);
	BN_free(bn2);
	sshbuf_free(p1);
	TEST_DONE();

#if defined(OPENSSL_HAS_ECC) && defined(OPENSSL_HAS_NISTP256)
	TEST_START("sshbuf_put_ec");
	eck = EC_KEY_new_by_curve_name(ec256_nid);
	ASSERT_PTR_NE(eck, NULL);
	ecp = EC_POINT_new(EC_KEY_get0_group(eck));
	ASSERT_PTR_NE(ecp, NULL);
	MKBN(ec256_x, bn_x);
	MKBN(ec256_y, bn_y);
	ASSERT_INT_EQ(EC_POINT_set_affine_coordinates_GFp(
	    EC_KEY_get0_group(eck), ecp, bn_x, bn_y, NULL), 1);
	ASSERT_INT_EQ(EC_KEY_set_public_key(eck, ecp), 1);
	BN_free(bn_x);
	BN_free(bn_y);
	EC_POINT_free(ecp);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_eckey(p1, eck), 0);
	ASSERT_INT_EQ(sshbuf_get_string_direct(p1, &d, &s), 0);
	ASSERT_SIZE_T_EQ(s, sizeof(expec256));
	ASSERT_MEM_EQ(d, expec256, sizeof(expec256));
	sshbuf_free(p1);
	EC_KEY_free(eck);
	TEST_DONE();

	TEST_START("sshbuf_get_ec");
	eck = EC_KEY_new_by_curve_name(ec256_nid);
	ASSERT_PTR_NE(eck, NULL);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_string(p1, expec256, sizeof(expec256)), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), sizeof(expec256) + 4);
	ASSERT_INT_EQ(sshbuf_put_u8(p1, 0x00), 0);
	ASSERT_INT_EQ(sshbuf_get_eckey(p1, eck), 0);
	bn_x = BN_new();
	bn_y = BN_new();
	ASSERT_PTR_NE(bn_x, NULL);
	ASSERT_PTR_NE(bn_y, NULL);
	ASSERT_INT_EQ(EC_POINT_get_affine_coordinates_GFp(
	    EC_KEY_get0_group(eck), EC_KEY_get0_public_key(eck),
	    bn_x, bn_y, NULL), 1);
	MKBN(ec256_x, bn);
	MKBN(ec256_y, bn2);
	ASSERT_INT_EQ(BN_cmp(bn_x, bn), 0);
	ASSERT_INT_EQ(BN_cmp(bn_y, bn2), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 1);
	sshbuf_free(p1);
	EC_KEY_free(eck);
	BN_free(bn_x);
	BN_free(bn_y);
	BN_free(bn);
	BN_free(bn2);
	TEST_DONE();
#endif
}

