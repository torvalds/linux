/*
 * Copyright (c) 1999 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

/*
**  The following test program tries the pathconf(2) routine.  It should
**  be run in a non-NFS-mounted directory (e.g., /tmp) and on remote (NFS)
**  mounted directories running both NFS-v2 and NFS-v3 from systems that
**  both do and do not permit file giveaway.
*/

#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#ifdef EX_OK
# undef EX_OK		/* unistd.h may have another use for this */
#endif /* EX_OK */
#include <sysexits.h>

#ifndef lint
static char id[] = "@(#)$Id: t_pathconf.c,v 8.7 2013-11-22 20:52:01 ca Exp $";
#endif /* ! lint */

int
main(argc, argv)
	int argc;
	char **argv;
{
	int fd;
	int i;
	char tbuf[100];
	extern int errno;

	if (geteuid() == 0)
	{
		printf("*** Run me as a non-root user! ***\n");
		exit(EX_USAGE);
	}

	strcpy(tbuf, "TXXXXXX");
	fd = mkstemp(tbuf);
	if (fd < 0)
	{
		printf("*** Could not create test file %s\n", tbuf);
		exit(EX_CANTCREAT);
	}
	errno = 0;
	i = pathconf(".", _PC_CHOWN_RESTRICTED);
	printf("pathconf(.) returns %2d, errno = %d\n", i, errno);
	errno = 0;
	i = pathconf(tbuf, _PC_CHOWN_RESTRICTED);
	printf("pathconf(%s) returns %2d, errno = %d\n", tbuf, i, errno);
	errno = 0;
	i = fpathconf(fd, _PC_CHOWN_RESTRICTED);
	printf("fpathconf(%s) returns %2d, errno = %d\n", tbuf, i, errno);
	if (errno == 0 && i >= 0)
	{
		/* so it claims that it doesn't work -- try anyhow */
		printf("  fpathconf claims that chown is safe ");
		if (fchown(fd, 1, 1) >= 0)
			printf("*** but fchown works anyhow! ***\n");
		else
			printf("and fchown agrees\n");
	}
	else
	{
		/* well, let's see what really happens */
		printf("  fpathconf claims that chown is not safe ");
		if (fchown(fd, 1, 1) >= 0)
			printf("as indeed it is not\n");
		else
			printf("*** but in fact it is safe ***\n");
	}
	(void) unlink(tbuf);
	exit(EX_OK);
}
