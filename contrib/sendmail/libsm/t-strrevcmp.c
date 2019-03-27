/*
 * Copyright (c) 2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_IDSTR(id, "@(#)$Id: t-strrevcmp.c,v 1.4 2013-11-22 20:51:44 ca Exp $")

#include <sm/exc.h>
#include <sm/io.h>
#include <sm/string.h>
#include <sm/test.h>

int
main(argc, argv)
	int argc;
	char **argv;
{
	char *s1;
	char *s2;

	sm_test_begin(argc, argv, "test string compare");

	s1 = "equal";
	s2 = "equal";
	SM_TEST(sm_strrevcmp(s1, s2) == 0);

	s1 = "equal";
	s2 = "qual";
	SM_TEST(sm_strrevcmp(s1, s2) > 0);

	s1 = "qual";
	s2 = "equal";
	SM_TEST(sm_strrevcmp(s1, s2) < 0);

	s1 = "Equal";
	s2 = "equal";
	SM_TEST(sm_strrevcmp(s1, s2) < 0);

	s1 = "Equal";
	s2 = "equal";
	SM_TEST(sm_strrevcasecmp(s1, s2) == 0);

	s1 = "Equal";
	s2 = "eQuaL";
	SM_TEST(sm_strrevcasecmp(s1, s2) == 0);

	return sm_test_end();
}
