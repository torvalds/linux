/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_IDSTR(id, "@(#)$Id: t-float.c,v 1.19 2013-11-22 20:51:43 ca Exp $")

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
	double d, d2;
	double ld;
	char buf[128];
	char *r;

	/*
	**  Sendmail uses printf and scanf with doubles,
	**  so make sure that this works.
	*/

	sm_test_begin(argc, argv, "test floating point stuff");

	d = 1.125;
	sm_snprintf(buf, sizeof(buf), "%d %.3f %d", 0, d, 1);
	r = "0 1.125 1";
	if (!SM_TEST(strcmp(buf, r) == 0))
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "got %s instead\n", buf);

	d = 1.125;
	sm_snprintf(buf, sizeof(buf), "%.3f", d);
	r = "1.125";
	if (!SM_TEST(strcmp(buf, r) == 0))
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "got %s instead\n", buf);
	d2 = 0.0;
	sm_io_sscanf(buf, "%lf", &d2);
#if SM_CONF_BROKEN_STRTOD
	if (d != d2)
	{
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "wanted %f, got %f\n", d, d2);
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "error ignored since SM_CONF_BROKEN_STRTOD is set for this OS\n");
	}
#else /* SM_CONF_BROKEN_STRTOD */
	if (!SM_TEST(d == d2))
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "wanted %f, got %f\n", d, d2);
#endif /* SM_CONF_BROKEN_STRTOD */

	ld = 2.5;
	sm_snprintf(buf, sizeof(buf), "%.3f %.1f", d, ld);
	r = "1.125 2.5";
	if (!SM_TEST(strcmp(buf, r) == 0))
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "got %s instead\n", buf);
	return sm_test_end();
}
