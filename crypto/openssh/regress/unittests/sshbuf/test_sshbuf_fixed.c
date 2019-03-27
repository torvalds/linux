/* 	$OpenBSD: test_sshbuf_fixed.c,v 1.1 2014/04/30 05:32:00 djm Exp $ */
/*
 * Regress test for sshbuf.h buffer API
 *
 * Placed in the public domain
 */

#define SSHBUF_INTERNAL 1  /* access internals for testing */
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
#include "ssherr.h"

void sshbuf_fixed(void);

const u_char test_buf[] = "\x01\x12\x34\x56\x78\x00\x00\x00\x05hello";

void
sshbuf_fixed(void)
{
	struct sshbuf *p1, *p2, *p3;
	u_char c;
	char *s;
	u_int i;
	size_t l;

	TEST_START("sshbuf_from");
	p1 = sshbuf_from(test_buf, sizeof(test_buf));
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_PTR_EQ(sshbuf_mutable_ptr(p1), NULL);
	ASSERT_INT_EQ(sshbuf_check_reserve(p1, 1), SSH_ERR_BUFFER_READ_ONLY);
	ASSERT_INT_EQ(sshbuf_reserve(p1, 1, NULL), SSH_ERR_BUFFER_READ_ONLY);
	ASSERT_INT_EQ(sshbuf_set_max_size(p1, 200), SSH_ERR_BUFFER_READ_ONLY);
	ASSERT_INT_EQ(sshbuf_put_u32(p1, 0x12345678), SSH_ERR_BUFFER_READ_ONLY);
	ASSERT_SIZE_T_EQ(sshbuf_avail(p1), 0);
	ASSERT_PTR_EQ(sshbuf_ptr(p1), test_buf);
	sshbuf_free(p1);
	TEST_DONE();

	TEST_START("sshbuf_from data");
	p1 = sshbuf_from(test_buf, sizeof(test_buf) - 1);
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_PTR_EQ(sshbuf_ptr(p1), test_buf);
	ASSERT_INT_EQ(sshbuf_get_u8(p1, &c), 0);
	ASSERT_PTR_EQ(sshbuf_ptr(p1), test_buf + 1);
	ASSERT_U8_EQ(c, 1);
	ASSERT_INT_EQ(sshbuf_get_u32(p1, &i), 0);
	ASSERT_PTR_EQ(sshbuf_ptr(p1), test_buf + 5);
	ASSERT_U32_EQ(i, 0x12345678);
	ASSERT_INT_EQ(sshbuf_get_cstring(p1, &s, &l), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), 0);
	ASSERT_STRING_EQ(s, "hello");
	ASSERT_SIZE_T_EQ(l, 5);
	sshbuf_free(p1);
	free(s);
	TEST_DONE();

	TEST_START("sshbuf_fromb ");
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_U_INT_EQ(sshbuf_refcount(p1), 1);
	ASSERT_PTR_EQ(sshbuf_parent(p1), NULL);
	ASSERT_INT_EQ(sshbuf_put(p1, test_buf, sizeof(test_buf) - 1), 0);
	p2 = sshbuf_fromb(p1);
	ASSERT_PTR_NE(p2, NULL);
	ASSERT_U_INT_EQ(sshbuf_refcount(p1), 2);
	ASSERT_PTR_EQ(sshbuf_parent(p1), NULL);
	ASSERT_PTR_EQ(sshbuf_parent(p2), p1);
	ASSERT_PTR_EQ(sshbuf_ptr(p2), sshbuf_ptr(p1));
	ASSERT_PTR_NE(sshbuf_ptr(p1), NULL);
	ASSERT_PTR_NE(sshbuf_ptr(p2), NULL);
	ASSERT_PTR_EQ(sshbuf_mutable_ptr(p1), NULL);
	ASSERT_PTR_EQ(sshbuf_mutable_ptr(p2), NULL);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), sshbuf_len(p2));
	ASSERT_INT_EQ(sshbuf_get_u8(p2, &c), 0);
	ASSERT_PTR_EQ(sshbuf_ptr(p2), sshbuf_ptr(p1) + 1);
	ASSERT_U8_EQ(c, 1);
	ASSERT_INT_EQ(sshbuf_get_u32(p2, &i), 0);
	ASSERT_PTR_EQ(sshbuf_ptr(p2), sshbuf_ptr(p1) + 5);
	ASSERT_U32_EQ(i, 0x12345678);
	ASSERT_INT_EQ(sshbuf_get_cstring(p2, &s, &l), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p2), 0);
	ASSERT_STRING_EQ(s, "hello");
	ASSERT_SIZE_T_EQ(l, 5);
	sshbuf_free(p1);
	ASSERT_U_INT_EQ(sshbuf_refcount(p1), 1);
	sshbuf_free(p2);
	free(s);
	TEST_DONE();

	TEST_START("sshbuf_froms");
	p1 = sshbuf_new();
	ASSERT_PTR_NE(p1, NULL);
	ASSERT_INT_EQ(sshbuf_put_u8(p1, 0x01), 0);
	ASSERT_INT_EQ(sshbuf_put_u32(p1, 0x12345678), 0);
	ASSERT_INT_EQ(sshbuf_put_cstring(p1, "hello"), 0);
	p2 = sshbuf_new();
	ASSERT_PTR_NE(p2, NULL);
	ASSERT_SIZE_T_EQ(sshbuf_len(p1), sizeof(test_buf) - 1);
	ASSERT_INT_EQ(sshbuf_put_stringb(p2, p1), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p2), sizeof(test_buf) + 4 - 1);
	ASSERT_INT_EQ(sshbuf_froms(p2, &p3), 0);
	ASSERT_SIZE_T_EQ(sshbuf_len(p2), 0);
	ASSERT_PTR_NE(p3, NULL);
	ASSERT_PTR_NE(sshbuf_ptr(p3), NULL);
	ASSERT_SIZE_T_EQ(sshbuf_len(p3), sizeof(test_buf) - 1);
	ASSERT_MEM_EQ(sshbuf_ptr(p3), test_buf, sizeof(test_buf) - 1);
	sshbuf_free(p3);
	ASSERT_INT_EQ(sshbuf_put_stringb(p2, p1), 0);
	ASSERT_INT_EQ(sshbuf_consume_end(p2, 1), 0);
	ASSERT_INT_EQ(sshbuf_froms(p2, &p3), SSH_ERR_MESSAGE_INCOMPLETE);
	ASSERT_PTR_EQ(p3, NULL);
	sshbuf_free(p2);
	sshbuf_free(p1);
}
