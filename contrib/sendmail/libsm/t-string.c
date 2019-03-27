/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_IDSTR(id, "@(#)$Id: t-string.c,v 1.12 2013-11-22 20:51:43 ca Exp $")

#include <sm/exc.h>
#include <sm/io.h>
#include <sm/string.h>
#include <sm/test.h>

int
main(argc, argv)
	int argc;
	char **argv;
{
	char *s;
	char buf[4096];
	char foo[4];
	char *r;
	int n;

	sm_test_begin(argc, argv, "test string utilities");

	s = sm_stringf_x("%.3s%03d", "foobar", 42);
	r = "foo042";
	SM_TEST(strcmp(s, r) == 0);

	s = sm_stringf_x("+%*x+", 2000, 0xCAFE);
	sm_snprintf(buf, 4096, "+%*x+", 2000, 0xCAFE);
	SM_TEST(strcmp(s, buf) == 0);

	foo[3] = 1;
	n = sm_snprintf(foo, sizeof(foo), "foobar%dbaz", 42);
	SM_TEST(n == 11);
	r = "foo";
	SM_TEST(strcmp(foo, r) == 0);

	return sm_test_end();
}
