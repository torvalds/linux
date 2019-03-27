/*
 * Copyright (c) 2000-2002 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_IDSTR(Id, "@(#)$Id: test.c,v 1.17 2013-11-22 20:51:44 ca Exp $")

/*
**  Abstractions for writing libsm test programs.
*/

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sm/debug.h>
#include <sm/test.h>

extern char *optarg;
extern int optind;
extern int optopt;
extern int opterr;

int SmTestIndex;
int SmTestNumErrors;
bool SmTestVerbose;

static char Help[] = "\
%s [-h] [-d debugging] [-v]\n\
\n\
%s\n\
\n\
-h		Display this help information.\n\
-d debugging	Set debug activation levels.\n\
-v		Verbose output.\n\
";

static char Usage[] = "\
Usage: %s [-h] [-v]\n\
Use %s -h for help.\n\
";

/*
**  SM_TEST_BEGIN -- initialize test system.
**
**	Parameters:
**		argc -- argument counter.
**		argv -- argument vector.
**		testname -- description of tests.
**
**	Results:
**		none.
*/

void
sm_test_begin(argc, argv, testname)
	int argc;
	char **argv;
	char *testname;
{
	int c;

	SmTestIndex = 0;
	SmTestNumErrors = 0;
	SmTestVerbose = false;
	opterr = 0;

	while ((c = getopt(argc, argv, "vhd:")) != -1)
	{
		switch (c)
		{
		  case 'v':
			SmTestVerbose = true;
			break;
		  case 'd':
			sm_debug_addsettings_x(optarg);
			break;
		  case 'h':
			(void) fprintf(stdout, Help, argv[0], testname);
			exit(0);
		  default:
			(void) fprintf(stderr,
					"Unknown command line option -%c\n",
					optopt);
			(void) fprintf(stderr, Usage, argv[0], argv[0]);
			exit(1);
		}
	}
}

/*
**  SM_TEST -- single test.
**
**	Parameters:
**		success -- did test succeeed?
**		expr -- expression that has been evaluated.
**		filename -- guess...
**		lineno -- line number.
**
**	Results:
**		value of success.
*/

bool
sm_test(success, expr, filename, lineno)
	bool success;
	char *expr;
	char *filename;
	int lineno;
{
	++SmTestIndex;
	if (SmTestVerbose)
		(void) fprintf(stderr, "%d..", SmTestIndex);
	if (!success)
	{
		++SmTestNumErrors;
		if (!SmTestVerbose)
			(void) fprintf(stderr, "%d..", SmTestIndex);
		(void) fprintf(stderr, "bad! %s:%d %s\n", filename, lineno,
				expr);
	}
	else
	{
		if (SmTestVerbose)
			(void) fprintf(stderr, "ok\n");
	}
	return success;
}

/*
**  SM_TEST_END -- end of test system.
**
**	Parameters:
**		none.
**
**	Results:
**		number of errors.
*/

int
sm_test_end()
{
	(void) fprintf(stderr, "%d of %d tests completed successfully\n",
			SmTestIndex - SmTestNumErrors, SmTestIndex);
	if (SmTestNumErrors != 0)
		(void) fprintf(stderr, "*** %d error%s in test! ***\n",
				SmTestNumErrors,
				SmTestNumErrors > 1 ? "s" : "");

	return SmTestNumErrors;
}
