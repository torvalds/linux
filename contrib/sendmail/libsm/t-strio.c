/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_IDSTR(id, "@(#)$Id: t-strio.c,v 1.12 2013-11-22 20:51:44 ca Exp $")
#include <sm/string.h>
#include <sm/io.h>
#include <sm/test.h>

int
main(argc, argv)
	int argc;
	char *argv[];
{
	char buf[20];
	char *r;
	SM_FILE_T f;

	sm_test_begin(argc, argv, "test strio");
	(void) memset(buf, '.', 20);
	sm_strio_init(&f, buf, 10);
	(void) sm_io_fprintf(&f, SM_TIME_DEFAULT, "foobarbazoom");
	sm_io_flush(&f, SM_TIME_DEFAULT);
	r = "foobarbaz";
	SM_TEST(strcmp(buf, r) == 0);
	return sm_test_end();
}
