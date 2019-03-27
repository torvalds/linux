/* 	$OpenBSD: test_sshbuf_misc.c,v 1.2 2016/05/03 13:48:33 djm Exp $ */
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

#include "../test_helper/test_helper.h"

#include "sshbuf.h"

void sshbuf_misc_tests(void);

void
sshbuf_misc_tests(void)
{
	struct sshbuf *p1;
	char tmp[512], *p;
	FILE *out;
	size_t sz;

	TEST_START("sshbuf_dump");
	out = tmpfile();
	ASSERT_PTR_NE(out, NULL);
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_u32(p1, 0x12345678), 0);
	sshbuf_dump(p1, out);
	fflush(out);
	rewind(out);
	sz = fread(tmp, 1, sizeof(tmp), out);
	ASSERT_INT_EQ(ferror(out), 0);
	ASSERT_INT_NE(feof(out), 0);
	ASSERT_SIZE_T_GT(sz, 0);
	tmp[sz] = '\0';
	ASSERT_PTR_NE(strstr(tmp, "12 34 56 78"), NULL);
	fclose(out);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_dtob16");
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_u32(p1, 0x12345678), 0);
	p = sshbuf_dtob16(p1);
	ASSERT_PTR_NE(p, NULL);
	ASSERT_STRING_EQ(p, "12345678");
	free(p);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_dtob64 len 1");
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_u8(p1, 0x11), 0);
	p = sshbuf_dtob64(p1);
	ASSERT_PTR_NE(p, NULL);
	ASSERT_STRING_EQ(p, "EQ==");
	free(p);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_dtob64 len 2");
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_u8(p1, 0x11), 0);
	ASSERT_INT_EQ(sshbuf_put_u8(p1, 0x22), 0);
	p = sshbuf_dtob64(p1);
	ASSERT_PTR_NE(p, NULL);
	ASSERT_STRING_EQ(p, "ESI=");
	free(p);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_dtob64 len 3");
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_u8(p1, 0x11), 0);
	ASSERT_INT_EQ(sshbuf_put_u8(p1, 0x22), 0);
	ASSERT_INT_EQ(sshbuf_put_u8(p1, 0x33), 0);
	p = sshbuf_dtob64(p1);
	ASSERT_PTR_NE(p, NULL);
	ASSERT_STRING_EQ(p, "ESIz");
	free(p);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_dtob64 len 8191");
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_reserve(p1, 8192, NULL), 0);
	bzero(sshbuf_mutable_ptr(p1), 8192);
	p = sshbuf_dtob64(p1);
	ASSERT_PTR_NE(p, NULL);
	ASSERT_SIZE_T_EQ(strlen(p), ((8191 + 2) / 3) * 4);
	free(p);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_b64tod len 1");
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_b64tod(p1, "0A=="), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 1);
	ASSERT_U8_EQ(*sshbuf_ptr(p1), 0xd0);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_b64tod len 2");
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_b64tod(p1, "0A8="), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 2);
	ASSERT_U16_EQ(PEEK_U16(sshbuf_ptr(p1)), 0xd00f);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_b64tod len 4");
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_b64tod(p1, "0A/QDw=="), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 4);
	ASSERT_U32_EQ(PEEK_U32(sshbuf_ptr(p1)), 0xd00fd00f);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_dup_string");
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	/* Check empty buffer */
	p = sshbuf_dup_string(p1);
	ASSERT_PTR_NE(p, NULL);
	ASSERT_SIZE_T_EQ(strlen(p), 0);
	free(p);
	/* Check buffer with string */
	ASSERT_INT_EQ(sshbuf_put(p1, "quad1", strlen("quad1")), 0);
	p = sshbuf_dup_string(p1);
	ASSERT_PTR_NE(p, NULL);
	ASSERT_SIZE_T_EQ(strlen(p), strlen("quad1"));
	ASSERT_STRING_EQ(p, "quad1");
	free(p);
	/* Check buffer with terminating nul */
	ASSERT_INT_EQ(sshbuf_put(p1, "\0", 1), 0);
	p = sshbuf_dup_string(p1);
	ASSERT_PTR_NE(p, NULL);
	ASSERT_SIZE_T_EQ(strlen(p), strlen("quad1"));
	ASSERT_STRING_EQ(p, "quad1");
	free(p);
	/* Check buffer with data after nul (expect failure) */
	ASSERT_INT_EQ(sshbuf_put(p1, "quad2", strlen("quad2")), 0);
	p = sshbuf_dup_string(p1);
	ASSERT_PTR_EQ(p, NULL);
	sshbuf_free(p1);
	TEST_DONE();
}

