/*
 * Copyright (c) 2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_IDSTR(id, "@(#)$Id: t-cf.c,v 1.8 2013-11-22 20:51:43 ca Exp $")

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sm/cf.h>

int
main(argc, argv)
	int argc;
	char **argv;
{
	SM_CF_OPT_T opt;
	int err;

	if (argc != 3)
	{
		fprintf(stderr, "Usage: %s .cf-file option\n", argv[0]);
		exit(1);
	}
	opt.opt_name = argv[2];
	opt.opt_val = NULL;
	err = sm_cf_getopt(argv[1], 1, &opt);
	if (err)
	{
		fprintf(stderr, "%s: %s\n", argv[1], strerror(err));
		exit(1);
	}
	if (opt.opt_val == NULL)
		printf("Error: option \"%s\" not found\n", opt.opt_name);
	else
		printf("%s=%s\n", opt.opt_name, opt.opt_val);
	return 0;
}
