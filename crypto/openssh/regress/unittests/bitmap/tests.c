/* 	$OpenBSD: tests.c,v 1.1 2015/01/15 07:36:28 djm Exp $ */
/*
 * Regress test for bitmap.h bitmap API
 *
 * Placed in the public domain
 */

#include "includes.h"

#include <sys/types.h>
#include <sys/param.h>
#include <stdio.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <stdlib.h>
#include <string.h>

#include <openssl/bn.h>

#include "../test_helper/test_helper.h"

#include "bitmap.h"

#define NTESTS 131

void
tests(void)
{
	struct bitmap *b;
	BIGNUM *bn;
	size_t len;
	int i, j, k, n;
	u_char bbuf[1024], bnbuf[1024];
	int r;

	TEST_START("bitmap_new");
	b = bitmap_new();
	ASSERT_PTR_NE(b, NULL);
	bn = BN_new();
	ASSERT_PTR_NE(bn, NULL);
	TEST_DONE();

	TEST_START("bitmap_set_bit / bitmap_test_bit");
	for (i = -1; i < NTESTS; i++) {
		for (j = -1; j < NTESTS; j++) {
			for (k = -1; k < NTESTS; k++) {
				bitmap_zero(b);
				BN_clear(bn);

				test_subtest_info("set %d/%d/%d", i, j, k);
				/* Set bits */
				if (i >= 0) {
					ASSERT_INT_EQ(bitmap_set_bit(b, i), 0);
					ASSERT_INT_EQ(BN_set_bit(bn, i), 1);
				}
				if (j >= 0) {
					ASSERT_INT_EQ(bitmap_set_bit(b, j), 0);
					ASSERT_INT_EQ(BN_set_bit(bn, j), 1);
				}
				if (k >= 0) {
					ASSERT_INT_EQ(bitmap_set_bit(b, k), 0);
					ASSERT_INT_EQ(BN_set_bit(bn, k), 1);
				}

				/* Check perfect match between bitmap and bn */
				test_subtest_info("match %d/%d/%d", i, j, k);
				for (n = 0; n < NTESTS; n++) {
					ASSERT_INT_EQ(BN_is_bit_set(bn, n),
					    bitmap_test_bit(b, n));
				}

				/* Test length calculations */
				test_subtest_info("length %d/%d/%d", i, j, k);
				ASSERT_INT_EQ(BN_num_bits(bn),
				    (int)bitmap_nbits(b));
				ASSERT_INT_EQ(BN_num_bytes(bn),
				    (int)bitmap_nbytes(b));

				/* Test serialisation */
				test_subtest_info("serialise %d/%d/%d",
				    i, j, k);
				len = bitmap_nbytes(b);
				memset(bbuf, 0xfc, sizeof(bbuf));
				ASSERT_INT_EQ(bitmap_to_string(b, bbuf,
				    sizeof(bbuf)), 0);
				for (n = len; n < (int)sizeof(bbuf); n++)
					ASSERT_U8_EQ(bbuf[n], 0xfc);
				r = BN_bn2bin(bn, bnbuf);
				ASSERT_INT_GE(r, 0);
				ASSERT_INT_EQ(r, (int)len);
				ASSERT_MEM_EQ(bbuf, bnbuf, len);

				/* Test deserialisation */
				test_subtest_info("deserialise %d/%d/%d",
				    i, j, k);
				bitmap_zero(b);
				ASSERT_INT_EQ(bitmap_from_string(b, bnbuf,
				    len), 0);
				for (n = 0; n < NTESTS; n++) {
					ASSERT_INT_EQ(BN_is_bit_set(bn, n),
					    bitmap_test_bit(b, n));
				}

				/* Test clearing bits */
				test_subtest_info("clear %d/%d/%d",
				    i, j, k);
				for (n = 0; n < NTESTS; n++) {
					ASSERT_INT_EQ(bitmap_set_bit(b, n), 0);
					ASSERT_INT_EQ(BN_set_bit(bn, n), 1);
				}
				if (i >= 0) {
					bitmap_clear_bit(b, i);
					BN_clear_bit(bn, i);
				}
				if (j >= 0) {
					bitmap_clear_bit(b, j);
					BN_clear_bit(bn, j);
				}
				if (k >= 0) {
					bitmap_clear_bit(b, k);
					BN_clear_bit(bn, k);
				}
				for (n = 0; n < NTESTS; n++) {
					ASSERT_INT_EQ(BN_is_bit_set(bn, n),
					    bitmap_test_bit(b, n));
				}
			}
		}
	}
	bitmap_free(b);
	BN_free(bn);
	TEST_DONE();
}

