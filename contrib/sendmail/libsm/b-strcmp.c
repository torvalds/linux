/*
 * Copyright (c) 2000-2001, 2004 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: b-strcmp.c,v 1.15 2013-11-22 20:51:42 ca Exp $")
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sm/time.h>
#include <sm/string.h>

#define toseconds(x, y)	(x.tv_sec - y.tv_sec)
#define SIZE	512
#define LOOPS	4000000L	/* initial number of loops */
#define MAXTIME	30L	/* "maximum" time to run single test */

void fatal __P((char *));
void purpose __P((void));
int main __P((int, char *[]));

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
	printf("strcasecmp() and sm_strcasecmp().\n");
	printf("These tests may take several minutes to complete.\n");
}

int
main(argc, argv)
	int argc;
	char *argv[];
{
	long a;
	int k;
	bool doit = false;
	long loops;
	long j;
	long one, two;
	struct timeval t1, t2;
	char src1[SIZE], src2[SIZE];

# define OPTIONS	"d"
	while ((k = getopt(argc, argv, OPTIONS)) != -1)
	{
		switch ((char) k)
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

	/* Run-time comments to the user */
	purpose();
	printf("\n");
	for (k = 0; k < 3; k++)
	{
		switch (k)
		{
		  case 0:
			(void) sm_strlcpy(src1, "1234567890", SIZE);
			(void) sm_strlcpy(src2, "1234567890", SIZE);
			break;
		  case 1:
			(void) sm_strlcpy(src1, "1234567890", SIZE);
			(void) sm_strlcpy(src2, "1234567891", SIZE);
			break;
		  case 2:
			(void) sm_strlcpy(src1, "1234567892", SIZE);
			(void) sm_strlcpy(src2, "1234567891", SIZE);
			break;
		}
		printf("Test %d: strcasecmp(%s, %s) versus sm_strcasecmp()\n",
			k, src1, src2);
		loops = LOOPS;
		for (;;)
		{
			j = 0;
			if (gettimeofday(&t1, NULL) < 0)
				fatal("gettimeofday");
			for (a = 0; a < loops; a++)
				j += strcasecmp(src1, src2);
			if (gettimeofday(&t2, NULL) < 0)
				fatal("gettimeofday");
			one = toseconds(t2, t1);
			printf("\tstrcasecmp() result: %ld seconds [%ld]\n",
				one, j);

			j = 0;
			if (gettimeofday(&t1, NULL) < 0)
				fatal("gettimeofday");
			for (a = 0; a < loops; a++)
				j += sm_strcasecmp(src1, src2);
			if (gettimeofday(&t2, NULL) < 0)
				fatal("gettimeofday");
			two = toseconds(t2, t1);
			printf("\tsm_strcasecmp() result: %ld seconds [%ld]\n",
				two, j);

			if (abs(one - two) > 2)
				break;
			loops += loops;
			if (loops < 0L || one > MAXTIME)
			{
				printf("\t\t** results too close: no decision\n");
				break;
			}
			else
			{
				printf("\t\t** results too close redoing test %ld times **\n",
					loops);
			}
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
	return 0;
}
