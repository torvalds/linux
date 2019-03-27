/* $NetBSD: t_bitstring.c,v 1.4 2012/03/25 06:54:04 joerg Exp $ */

/*-
 * Copyright (c) 1993, 2008, 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <bitstring.h>
#include <stdio.h>
#include <stdlib.h>

#include <atf-c.h>

static void
clearbits(bitstr_t *b, int n)
{
	int i = bitstr_size(n);

	while(i--)
		*(b + i) = 0;
}

static void
printbits(FILE *file, bitstr_t *b, int n)
{
	int i;
	int jc, js;

	bit_ffc(b, n, &jc);
	bit_ffs(b, n, &js);

	(void) fprintf(file, "%3d %3d ", jc, js);

	for (i=0; i < n; i++) {
		(void) fprintf(file, "%c", (bit_test(b, i) ? '1' : '0'));
	}

	(void) fprintf(file, "%c", '\n');
}

static void
calculate_data(FILE *file, const int test_length)
{
	int i;
	bitstr_t *bs;

	assert(test_length >= 4);

	(void) fprintf(file, "Testing with TEST_LENGTH = %d\n\n", test_length);

	(void) fprintf(file, "test _bit_byte, _bit_mask, and bitstr_size\n");
	(void) fprintf(file, "  i   _bit_byte(i)   _bit_mask(i) bitstr_size(i)\n");

	for (i=0; i < test_length; i++) {
		(void) fprintf(file, "%3d%15u%15u%15zu\n",
			i, _bit_byte(i), _bit_mask(i), bitstr_size(i));
	}

	bs = bit_alloc(test_length);
	clearbits(bs, test_length);
	(void) fprintf(file, "\ntest bit_alloc, clearbits, bit_ffc, bit_ffs\n");
	(void) fprintf(file, "be:   0  -1 ");
	for (i=0; i < test_length; i++)
		(void) fprintf(file, "%c", '0');
	(void) fprintf(file, "\nis: ");
	printbits(file, bs, test_length);

	(void) fprintf(file, "\ntest bit_set\n");
	for (i=0; i < test_length; i+=3)
		bit_set(bs, i);
	(void) fprintf(file, "be:   1   0 ");
	for (i=0; i < test_length; i++)
		(void) fprintf(file, "%c", "100"[i % 3]);
	(void) fprintf(file, "\nis: ");
	printbits(file, bs, test_length);

	(void) fprintf(file, "\ntest bit_clear\n");
	for (i=0; i < test_length; i+=6)
		bit_clear(bs, i);
	(void) fprintf(file, "be:   0   3 ");
	for (i=0; i < test_length; i++)
		(void) fprintf(file, "%c", "000100"[i % 6]);
	(void) fprintf(file, "\nis: ");
	printbits(file, bs, test_length);

	(void) fprintf(file, "\ntest bit_test using previous bitstring\n");
	(void) fprintf(file, "  i    bit_test(i)\n");
	for (i=0; i < test_length; i++)
		(void) fprintf(file, "%3d%15d\n", i, bit_test(bs, i));

	clearbits(bs, test_length);
	(void) fprintf(file, "\ntest clearbits\n");
	(void) fprintf(file, "be:   0  -1 ");
	for (i=0; i < test_length; i++)
		(void) fprintf(file, "%c", '0');
	(void) fprintf(file, "\nis: ");
	printbits(file, bs, test_length);

	(void) fprintf(file, "\ntest bit_nset and bit_nclear\n");
	bit_nset(bs, 1, test_length - 2);
	(void) fprintf(file, "be:   0   1 0");
	for (i=0; i < test_length - 2; i++)
		(void) fprintf(file, "%c", '1');
	(void) fprintf(file, "0\nis: ");
	printbits(file, bs, test_length);

	bit_nclear(bs, 2, test_length - 3);
	(void) fprintf(file, "be:   0   1 01");
	for (i=0; i < test_length - 4; i++)
		(void) fprintf(file, "%c", '0');
	(void) fprintf(file, "10\nis: ");
	printbits(file, bs, test_length);

	bit_nclear(bs, 0, test_length - 1);
	(void) fprintf(file, "be:   0  -1 ");
	for (i=0; i < test_length; i++)
		(void) fprintf(file, "%c", '0');
	(void) fprintf(file, "\nis: ");
	printbits(file, bs, test_length);
	bit_nset(bs, 0, test_length - 2);
	(void) fprintf(file, "be: %3d   0 ",test_length - 1);
	for (i=0; i < test_length - 1; i++)
		(void) fprintf(file, "%c", '1');
	fprintf(file, "%c", '0');
	(void) fprintf(file, "\nis: ");
	printbits(file, bs, test_length);
	bit_nclear(bs, 0, test_length - 1);
	(void) fprintf(file, "be:   0  -1 ");
	for (i=0; i < test_length; i++)
		(void) fprintf(file, "%c", '0');
	(void) fprintf(file, "\nis: ");
	printbits(file, bs, test_length);

	(void) fprintf(file, "\n");
	(void) fprintf(file, "first 1 bit should move right 1 position each line\n");
	for (i=0; i < test_length; i++) {
		bit_nclear(bs, 0, test_length - 1);
		bit_nset(bs, i, test_length - 1);
		(void) fprintf(file, "%3d ", i); printbits(file, bs, test_length);
	}

	(void) fprintf(file, "\n");
	(void) fprintf(file, "first 0 bit should move right 1 position each line\n");
	for (i=0; i < test_length; i++) {
		bit_nset(bs, 0, test_length - 1);
		bit_nclear(bs, i, test_length - 1);
		(void) fprintf(file, "%3d ", i); printbits(file, bs, test_length);
	}

	(void) fprintf(file, "\n");
	(void) fprintf(file, "first 0 bit should move left 1 position each line\n");
	for (i=0; i < test_length; i++) {
		bit_nclear(bs, 0, test_length - 1);
		bit_nset(bs, 0, test_length - 1 - i);
		(void) fprintf(file, "%3d ", i); printbits(file, bs, test_length);
	}

	(void) fprintf(file, "\n");
	(void) fprintf(file, "first 1 bit should move left 1 position each line\n");
	for (i=0; i < test_length; i++) {
		bit_nset(bs, 0, test_length - 1);
		bit_nclear(bs, 0, test_length - 1 - i);
		(void) fprintf(file, "%3d ", i); printbits(file, bs, test_length);
	}

	(void) fprintf(file, "\n");
	(void) fprintf(file, "0 bit should move right 1 position each line\n");
	for (i=0; i < test_length; i++) {
		bit_nset(bs, 0, test_length - 1);
		bit_nclear(bs, i, i);
		(void) fprintf(file, "%3d ", i); printbits(file, bs, test_length);
	}

	(void) fprintf(file, "\n");
	(void) fprintf(file, "1 bit should move right 1 position each line\n");
	for (i=0; i < test_length; i++) {
		bit_nclear(bs, 0, test_length - 1);
		bit_nset(bs, i, i);
		(void) fprintf(file, "%3d ", i); printbits(file, bs, test_length);
	}

	(void) free(bs);
}

static void
one_check(const atf_tc_t *tc, const int test_length)
{
	FILE *out;
	char command[1024];

	ATF_REQUIRE((out = fopen("out", "w")) != NULL);
	calculate_data(out, test_length);
	fclose(out);

	/* XXX The following is a huge hack that was added to simplify the
	 * conversion of these tests from src/regress/ to src/tests/.  The
	 * tests in this file should be checking their own results, without
	 * having to resort to external data files. */
	snprintf(command, sizeof(command), "diff -u %s/d_bitstring_%d.out out",
	    atf_tc_get_config_var(tc, "srcdir"), test_length);
	if (system(command) != EXIT_SUCCESS)
		atf_tc_fail("Test failed; see output for details");
}

ATF_TC(bits_8);
ATF_TC_HEAD(bits_8, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks 8-bit long bitstrings");
}
ATF_TC_BODY(bits_8, tc)
{
	one_check(tc, 8);
}

ATF_TC(bits_27);
ATF_TC_HEAD(bits_27, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks 27-bit long bitstrings");
}
ATF_TC_BODY(bits_27, tc)
{
	one_check(tc, 27);
}

ATF_TC(bits_32);
ATF_TC_HEAD(bits_32, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks 32-bit long bitstrings");
}
ATF_TC_BODY(bits_32, tc)
{
	one_check(tc, 32);
}

ATF_TC(bits_49);
ATF_TC_HEAD(bits_49, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks 49-bit long bitstrings");
}
ATF_TC_BODY(bits_49, tc)
{
	one_check(tc, 49);
}

ATF_TC(bits_64);
ATF_TC_HEAD(bits_64, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks 64-bit long bitstrings");
}
ATF_TC_BODY(bits_64, tc)
{
	one_check(tc, 64);
}

ATF_TC(bits_67);
ATF_TC_HEAD(bits_67, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks 67-bit long bitstrings");
}
ATF_TC_BODY(bits_67, tc)
{
	one_check(tc, 67);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, bits_8);
	ATF_TP_ADD_TC(tp, bits_27);
	ATF_TP_ADD_TC(tp, bits_32);
	ATF_TP_ADD_TC(tp, bits_49);
	ATF_TP_ADD_TC(tp, bits_64);
	ATF_TP_ADD_TC(tp, bits_67);

	return atf_no_error();
}
