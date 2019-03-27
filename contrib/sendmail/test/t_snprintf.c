/*
 * Copyright (c) 2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <stdio.h>
#include <sysexits.h>

#ifndef lint
static char id[] = "@(#)$Id: t_snprintf.c,v 8.5 2013-11-22 20:52:01 ca Exp $";
#endif /* ! lint */

#define TEST_STRING	"1234567890"

int
main(argc, argv)
	int argc;
	char **argv;
{
	int r;
	char buf[5];

	r = snprintf(buf, sizeof buf, "%s", TEST_STRING);

	if (buf[sizeof buf - 1] != '\0' ||
	    r != strlen(TEST_STRING))
	{
		fprintf(stderr, "Add the following to devtools/Site/site.config.m4:\n\n");
		fprintf(stderr, "APPENDDEF(`confENVDEF', `-DSNPRINTF_IS_BROKEN=1')\n\n");
		exit(EX_OSERR);
	}
	fprintf(stderr, "snprintf() appears to work properly\n");
	exit(EX_OK);
}
