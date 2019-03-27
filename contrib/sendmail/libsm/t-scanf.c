/*
 * Copyright (c) 2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_IDSTR(id, "@(#)$Id: t-scanf.c,v 1.6 2013-11-22 20:51:43 ca Exp $")

#include <sm/limits.h>
#include <sm/io.h>
#include <sm/string.h>
#include <sm/test.h>
#include <sm/types.h>

int
main(argc, argv)
	int argc;
	char **argv;
{
	int i, d, h;
	char buf[128];
	char *r;

	sm_test_begin(argc, argv, "test scanf point stuff");
#if !SM_CONF_BROKEN_SIZE_T
	(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
"If tests for \"h == 2\" fail, check whether size_t is signed on your OS.\n\
If that is the case, add -DSM_CONF_BROKEN_SIZE_T to confENVDEF\n\
and start over. Otherwise contact sendmail.org.\n");
#endif /* !SM_CONF_BROKEN_SIZE_T */

	d = 2;
	sm_snprintf(buf, sizeof(buf), "%d", d);
	r = "2";
	if (!SM_TEST(strcmp(buf, r) == 0))
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "got %s instead\n", buf);

	i = sm_io_sscanf(buf, "%d", &h);
	SM_TEST(i == 1);
	SM_TEST(h == 2);

	d = 2;
	sm_snprintf(buf, sizeof(buf), "%d\n", d);
	r = "2\n";
	if (!SM_TEST(strcmp(buf, r) == 0))
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "got %s instead\n", buf);

	i = sm_io_sscanf(buf, "%d", &h);
	SM_TEST(i == 1);
	SM_TEST(h == 2);

	return sm_test_end();
}
