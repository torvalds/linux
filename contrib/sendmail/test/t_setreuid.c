/*
 * Copyright (c) 2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

/*
**  This program checks to see if your version of setreuid works.
**  Compile it, make it set-user-ID root, and run it as yourself (NOT as
**  root).  If it won't compile or outputs any MAYDAY messages, don't
**  define HASSETREUID in conf.h.
**
**  Compilation is trivial -- just "cc t_setreuid.c".  Make it set-user-ID,
**  root and then execute it as a non-root user.
*/

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

#ifndef lint
static char id[] = "@(#)$Id: t_setreuid.c,v 8.10 2013-11-22 20:52:01 ca Exp $";
#endif /* ! lint */

#ifdef __hpux
# define setreuid(r, e)	setresuid(r, e, -1)
#endif /* __hpux */

static void
printuids(str, r, e)
	char *str;
	uid_t r, e;
{
	printf("%s (should be %d/%d): r/euid=%d/%d\n", str, (int) r, (int) e,
	       (int) getuid(), (int) geteuid());
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	int fail = 0;
	uid_t realuid = getuid();

	printuids("initial uids", realuid, 0);

	if (geteuid() != 0)
	{
		printf("SETUP ERROR: re-run set-user-ID root\n");
		exit(1);
	}

	if (getuid() == 0)
	{
		printf("SETUP ERROR: must be run by a non-root user\n");
		exit(1);
	}

	if (setreuid(0, 1) < 0)
	{
		fail++;
		printf("setreuid(0, 1) failure\n");
	}
	printuids("after setreuid(0, 1)", 0, 1);

	if (getuid() != 0)
	{
		fail++;
		printf("MAYDAY!  Wrong real uid\n");
	}

	if (geteuid() != 1)
	{
		fail++;
		printf("MAYDAY!  Wrong effective uid\n");
	}

	/* do activity here */

	if (setreuid(-1, 0) < 0)
	{
		fail++;
		printf("setreuid(-1, 0) failure\n");
	}
	printuids("after setreuid(-1, 0)", 0, 0);
	if (setreuid(realuid, 0) < 0)
	{
		fail++;
		printf("setreuid(%d, 0) failure\n", (int) realuid);
	}
	printuids("after setreuid(realuid, 0)", realuid, 0);

	if (geteuid() != 0)
	{
		fail++;
		printf("MAYDAY!  Wrong effective uid\n");
	}
	if (getuid() != realuid)
	{
		fail++;
		printf("MAYDAY!  Wrong real uid\n");
	}
	printf("\n");

	if (setreuid(0, 2) < 0)
	{
		fail++;
		printf("setreuid(0, 2) failure\n");
	}
	printuids("after setreuid(0, 2)", 0, 2);

	if (geteuid() != 2)
	{
		fail++;
		printf("MAYDAY!  Wrong effective uid\n");
	}

	if (getuid() != 0)
	{
		fail++;
		printf("MAYDAY!  Wrong real uid\n");
	}

	/* do activity here */

	if (setreuid(-1, 0) < 0)
	{
		fail++;
		printf("setreuid(-1, 0) failure\n");
	}
	printuids("after setreuid(-1, 0)", 0, 0);
	if (setreuid(realuid, 0) < 0)
	{
		fail++;
		printf("setreuid(%d, 0) failure\n", (int) realuid);
	}
	printuids("after setreuid(realuid, 0)", realuid, 0);

	if (geteuid() != 0)
	{
		fail++;
		printf("MAYDAY!  Wrong effective uid\n");
	}
	if (getuid() != realuid)
	{
		fail++;
		printf("MAYDAY!  Wrong real uid\n");
	}

	if (fail)
	{
		printf("\nThis system cannot use setreuid\n");
		exit(1);
	}

	printf("\nIt is safe to define HASSETREUID on this system\n");
	exit(0);
}
