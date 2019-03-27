/*
 * Copyright (c) 2005-2007 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_IDSTR(id, "@(#)$Id: t-memstat.c,v 1.11 2013-11-22 20:51:43 ca Exp $")

#include <sm/misc.h>

/*
**  Simple test program for memstat
*/

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>

extern char *optarg;
extern int optind;

void
usage(prg)
	char *prg;
{
	fprintf(stderr, "usage: %s [options]\n", prg);
	fprintf(stderr, "options:\n");
	fprintf(stderr, "-l n    loop n times\n");
	fprintf(stderr, "-m n    allocate n bytes per iteration\n");
	fprintf(stderr, "-r name use name as resource to query\n");
	fprintf(stderr, "-s n    sleep n seconds per iteration\n");
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	int r, r2, i, l, slp, sz;
	long v;
	char *resource;

	l = 1;
	sz = slp = 0;
	resource = NULL;
	while ((r = getopt(argc, argv, "l:m:r:s:")) != -1)
	{
		switch ((char) r)
		{
		  case 'l':
			l = strtol(optarg, NULL, 0);
			break;

		  case 'm':
			sz = strtol(optarg, NULL, 0);
			break;

		  case 'r':
			resource = strdup(optarg);
			if (resource == NULL)
			{
				fprintf(stderr, "strdup(%s) failed\n",
					optarg);
				exit(1);
			}
			break;

		  case 's':
			slp = strtol(optarg, NULL, 0);
			break;

		  default:
			usage(argv[0]);
			exit(1);
		}
	}

	r = sm_memstat_open();
	r2 = -1;
	for (i = 0; i < l; i++)
	{
		char *mem;

		r2 = sm_memstat_get(resource, &v);
		if (slp > 0 && i + 1 < l && 0 == r)
		{
			printf("open=%d, memstat=%d, %s=%ld\n", r, r2,
				resource != NULL ? resource : "default-value",
				v);
			sleep(slp);
			if (sz > 0)
			{
				/*
				**  Just allocate some memory to test the
				**  values that are returned.
				**  Note: this is a memory leak, but that
				**  doesn't matter here.
				*/

				mem = malloc(sz);
				if (NULL == mem)
					printf("malloc(%d) failed\n", sz);
			}
		}
	}
	printf("open=%d, memstat=%d, %s=%ld\n", r, r2,
		resource != NULL ? resource : "default-value", v);
	r = sm_memstat_close();
	return r;
}
