/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_IDSTR(id, "@(#)$Id: t-strl.c,v 1.16 2013-11-22 20:51:44 ca Exp $")

#include <stdlib.h>
#include <stdio.h>
#include <sm/heap.h>
#include <sm/string.h>
#include <sm/test.h>

#define MAXL	16
#define N	5
#define SIZE	128

int
main(argc, argv)
	int argc;
	char *argv[];
{
	char *s1, *s2, *s3;
	int one, two, k;
	char src1[N][SIZE], dst1[SIZE], dst2[SIZE];
	char *r;

	sm_test_begin(argc, argv, "test strl* string functions");
	s1 = "abc";
	s2 = "123";
	s3 = sm_malloc_x(MAXL);

	SM_TEST(sm_strlcpy(s3, s1, 4) == 3);
	SM_TEST(strcmp(s1, s3) == 0);

	SM_TEST(sm_strlcat(s3, s2, 8) == 6);
	r ="abc123";
	SM_TEST(strcmp(s3, r) == 0);

	SM_TEST(sm_strlcpy(s3, s1, 2) == 3);
	r = "a";
	SM_TEST(strcmp(s3, r) == 0);

	SM_TEST(sm_strlcat(s3, s2, 3) == 4);
	r = "a1";
	SM_TEST(strcmp(s3, r) == 0);

	SM_TEST(sm_strlcpy(s3, s1, 4) == 3);
	r = ":";
	SM_TEST(sm_strlcat2(s3, r, s2, MAXL) == 7);
	r = "abc:123";
	SM_TEST(strcmp(s3, r) == 0);

	SM_TEST(sm_strlcpy(s3, s1, 4) == 3);
	r = ":";
	SM_TEST(sm_strlcat2(s3, r, s2, 6) == 7);
	r = "abc:1";
	SM_TEST(strcmp(s3, r) == 0);

	SM_TEST(sm_strlcpy(s3, s1, 4) == 3);
	r = ":";
	SM_TEST(sm_strlcat2(s3, r, s2, 2) == 7);
	r = "abc";
	SM_TEST(strcmp(s3, r) == 0);

	SM_TEST(sm_strlcpy(s3, s1, 4) == 3);
	r = ":";
	SM_TEST(sm_strlcat2(s3, r, s2, 4) == 7);
	r = "abc";
	SM_TEST(strcmp(s3, r) == 0);

	SM_TEST(sm_strlcpy(s3, s1, 4) == 3);
	r = ":";
	SM_TEST(sm_strlcat2(s3, r, s2, 5) == 7);
	r = "abc:";
	SM_TEST(strcmp(s3, r) == 0);

	SM_TEST(sm_strlcpy(s3, s1, 4) == 3);
	r = ":";
	SM_TEST(sm_strlcat2(s3, r, s2, 6) == 7);
	r = "abc:1";
	SM_TEST(strcmp(s3, r) == 0);

	for (k = 0; k < N; k++)
	{
		(void) sm_strlcpy(src1[k], "abcdef", sizeof src1);
	}

	one = sm_strlcpyn(dst1, sizeof dst1, 3, src1[0], "/", src1[1]);
	two = sm_snprintf(dst2, sizeof dst2, "%s/%s", src1[0], src1[1]);
	SM_TEST(one == two);
	SM_TEST(strcmp(dst1, dst2) == 0);
	one = sm_strlcpyn(dst1, 10, 3, src1[0], "/", src1[1]);
	two = sm_snprintf(dst2, 10, "%s/%s", src1[0], src1[1]);
	SM_TEST(one == two);
	SM_TEST(strcmp(dst1, dst2) == 0);
	one = sm_strlcpyn(dst1, 5, 3, src1[0], "/", src1[1]);
	two = sm_snprintf(dst2, 5, "%s/%s", src1[0], src1[1]);
	SM_TEST(one == two);
	SM_TEST(strcmp(dst1, dst2) == 0);
	one = sm_strlcpyn(dst1, 0, 3, src1[0], "/", src1[1]);
	two = sm_snprintf(dst2, 0, "%s/%s", src1[0], src1[1]);
	SM_TEST(one == two);
	SM_TEST(strcmp(dst1, dst2) == 0);
	one = sm_strlcpyn(dst1, sizeof dst1, 5, src1[0], "/", src1[1], "/", src1[2]);
	two = sm_snprintf(dst2, sizeof dst2, "%s/%s/%s", src1[0], src1[1], src1[2]);
	SM_TEST(one == two);
	SM_TEST(strcmp(dst1, dst2) == 0);
	one = sm_strlcpyn(dst1, 15, 5, src1[0], "/", src1[1], "/", src1[2]);
	two = sm_snprintf(dst2, 15, "%s/%s/%s", src1[0], src1[1], src1[2]);
	SM_TEST(one == two);
	SM_TEST(strcmp(dst1, dst2) == 0);
	one = sm_strlcpyn(dst1, 20, 5, src1[0], "/", src1[1], "/", src1[2]);
	two = sm_snprintf(dst2, 20, "%s/%s/%s", src1[0], src1[1], src1[2]);
	SM_TEST(one == two);
	SM_TEST(strcmp(dst1, dst2) == 0);

	one = sm_strlcpyn(dst1, sizeof dst1, 0);
	SM_TEST(one == 0);
	r = "";
	SM_TEST(strcmp(dst1, r) == 0);
	one = sm_strlcpyn(dst1, 20, 1, src1[0]);
	two = sm_snprintf(dst2, 20, "%s", src1[0]);
	SM_TEST(one == two);
	one = sm_strlcpyn(dst1, 2, 1, src1[0]);
	two = sm_snprintf(dst2, 2, "%s", src1[0]);
	SM_TEST(one == two);

	return sm_test_end();
}
