/*
 * Copyright (c) 2000 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_IDSTR(id, "@(#)$Id: t-match.c,v 1.10 2013-11-22 20:51:43 ca Exp $")

#include <sm/string.h>
#include <sm/io.h>
#include <sm/test.h>

#define try(str, pat, want) \
	got = sm_match(str, pat); \
	if (!SM_TEST(got == want)) \
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, \
			"sm_match(\"%s\", \"%s\") returns %s\n", \
			str, pat, got ? "true" : "false");

int
main(argc, argv)
	int argc;
	char **argv;
{
	bool got;

	sm_test_begin(argc, argv, "test sm_match");

	try("foo", "foo", true);
	try("foo", "bar", false);
	try("foo[bar", "foo[bar", true);
	try("foo[bar]", "foo[bar]", false);
	try("foob", "foo[bar]", true);
	try("a-b", "a[]-]b", true);
	try("abcde", "a*e", true);
	try("[", "[[]", true);
	try("c", "[a-z]", true);
	try("C", "[a-z]", false);
	try("F:sm.heap", "[!F]*", false);
	try("E:sm.err", "[!F]*", true);

	return sm_test_end();
}
