/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

/*
**  Compile this program using a command line similar to:
**	cc -O -L../OBJ/libsm -o b-strl b-strl.c -lsm
**  where "OBJ" is the name of the object directory for the platform
**  you are compiling on.
**  Then run the program:
**	./b-strl
**  and read the output for results and how to interpret the results.
*/

#include <sm/gen.h>
SM_RCSID("@(#)$Id: b-strl.c,v 1.26 2013-11-22 20:51:42 ca Exp $")
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sm/time.h>
#include <sm/string.h>

#define SRC_SIZE	512
#define toseconds(x, y)	(x.tv_sec - y.tv_sec)
#define LOOPS	4000000L	/* initial number of loops */
#define MAXTIME	30L	/* "maximum" time to run single test */

void
fatal(str)
	char *str;
{
	perror(str);
	exit(1);
}

void
purpose()
{
	printf("This program benchmarks the performance differences between\n");
	printf("strlcpy() and sm_strlcpy(), and strlcat() and sm_strlcat().\n");
	printf("These tests may take several minutes to complete.\n");
}

int
main(argc, argv)
	int argc;
	char *argv[];
{
#if !SM_CONF_STRL
	printf("The configuration indicates the system needs the libsm\n");
	printf("versions of strlcpy(3) and strlcat(3). Thus, performing\n");
	printf("these tests will not be of much use.\n");
	printf("If your OS has strlcpy(3) and strlcat(3) then set the macro\n");
	printf("SM_CONF_STRL to 1 in your site.config.m4 file\n");
	printf("(located in ../devtools/Site) and recompile this program.\n");
#else /* !SM_CONF_STRL */
	int ch;
	long a;
	bool doit = false;
	long loops = LOOPS;
	long one, two;
	struct timeval t1, t2;
	char dest[SRC_SIZE], source[SRC_SIZE];

# define OPTIONS	"d"
	while ((ch = getopt(argc, argv, OPTIONS)) != -1)
	{
		switch ((char) ch)
		{
		  case 'd':
			doit = true;
			break;

		  default:
			break;
		}
	}

	if (!doit)
	{
		purpose();
		printf("If you want to run it, specify -d as option.\n");
		return 0;
	}

	/*
	**  Let's place a small string at the head of dest for
	**  catenation to happen (it'll be ignored for the copy).
	*/
	(void) sm_strlcpy(dest, "a small string at the start! ", SRC_SIZE - 1);

	/*
	**  Let's place a larger string into source for the catenation and
	**  the copy.
	*/
	(void) strlcpy(source,
		" This is the longer string that will be used for catenation and copying for the the performace testing. The longer the string being catenated or copied the greater the difference in measureable performance\n",
		SRC_SIZE - 1);

	/* Run-time comments to the user */
	purpose();
	printf("\n");
	printf("Test 1: strlcat() versus sm_strlcat()\n");

redo_cat:
	if (gettimeofday(&t1, NULL) < 0)
		fatal("gettimeofday");

	for (a = 0; a < loops; a++)
		strlcat(dest, source, SRC_SIZE - 1);

	if (gettimeofday(&t2, NULL) < 0)
		fatal("gettimeofday");

	printf("\tstrlcat() result: %ld seconds\n", one = toseconds(t2, t1));

	if (gettimeofday(&t1, NULL) < 0)
		fatal("gettimeofday");

	for (a = 0; a < loops; a++)
		sm_strlcat(dest, source, SRC_SIZE - 1);

	if (gettimeofday(&t2, NULL) < 0)
		fatal("gettimeofday");

	printf("\tsm_strlcat() result: %ld seconds\n", two = toseconds(t2, t1));

	if (one - two >= -2 && one - two <= 2)
	{
		loops += loops;
		if (loops < 0L || one > MAXTIME)
		{
			printf("\t\t** results too close: no decision\n");
		}
		else
		{
			printf("\t\t** results too close redoing test %ld times **\n",
				loops);
			goto redo_cat;
		}
	}

	printf("\n");
	printf("Test 2: strlcpy() versus sm_strlpy()\n");
	loops = LOOPS;
redo_cpy:
	if (gettimeofday(&t1, NULL) < 0)
		fatal("gettimeofday");

	for (a = 0; a < loops; a++)
		strlcpy(dest, source, SRC_SIZE - 1);

	if (gettimeofday(&t2, NULL) < 0)
		fatal("gettimeofday");

	printf("\tstrlcpy() result: %ld seconds\n", one = toseconds(t2, t1));

	if (gettimeofday(&t1, NULL) < 0)
		fatal("gettimeofday");

	for (a = 0; a < loops; a++)
		sm_strlcpy(dest, source, SRC_SIZE - 1);

	if (gettimeofday(&t2, NULL) < 0)
		fatal("gettimeofday");

	printf("\tsm_strlcpy() result: %ld seconds\n", two = toseconds(t2, t1));

	if (one - two >= -2 && one - two <= 2)
	{
		loops += loops;
		if (loops < 0L || one > MAXTIME)
		{
			printf("\t\t** results too close: no decision\n");
		}
		else
		{
			printf("\t\t** results too close redoing test %ld times **\n",
				loops);
			goto redo_cpy;
		}
	}

	printf("\n\n");
	printf("Interpreting the results:\n");
	printf("\tFor differences larger than 2 seconds, the lower value is\n");
	printf("\tbetter and that function should be used for performance\n");
	printf("\treasons.\n\n");
	printf("This program will re-run the tests when the difference is\n");
	printf("less than 2 seconds.\n");
	printf("The result will vary depending on the compiler optimization\n");	printf("level used. Compiling the sendmail libsm library with a\n");
	printf("better optimization level can change the results.\n");
#endif /* !SM_CONF_STRL  */
	return 0;
}
