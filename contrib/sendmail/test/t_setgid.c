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
**  This program checks to see if your version of setgid works.
**  Compile it, make it set-group-ID guest, and run it as yourself (NOT as
**  root and not as member of the group guest).
**
**  Compilation is trivial -- just "cc t_setgid.c".  Make it set-group-ID,
**  guest and then execute it as a non-root user.
*/

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

#ifndef lint
static char id[] = "@(#)$Id: t_setgid.c,v 1.7 2013-11-22 20:52:01 ca Exp $";
#endif /* ! lint */

static void
printgids(str, r, e)
	char *str;
	gid_t r, e;
{
	printf("%s (should be %d/%d): r/egid=%d/%d\n", str, (int) r, (int) e,
	       (int) getgid(), (int) getegid());
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	int fail = 0;
	int res;
	gid_t realgid = getgid();
	gid_t effgid = getegid();

	printgids("initial gids", realgid, effgid);

	if (effgid == realgid)
	{
		printf("SETUP ERROR: re-run set-group-ID guest\n");
		exit(1);
	}

#if SM_CONF_SETREGID
	res = setregid(effgid, effgid);
#else /* SM_CONF_SETREGID */
	res = setgid(effgid);
#endif /* SM_CONF_SETREGID */

	printf("setgid(%d)=%d %s\n", (int) effgid, res,
		res < 0 ? "failure" : "ok");
#if SM_CONF_SETREGID
	printgids("after setregid()", effgid, effgid);
#else /* SM_CONF_SETREGID */
	printgids("after setgid()", effgid, effgid);
#endif /* SM_CONF_SETREGID */

	if (getegid() != effgid)
	{
		fail++;
		printf("MAYDAY!  Wrong effective gid\n");
	}

	if (getgid() != effgid)
	{
		fail++;
		printf("MAYDAY!  Wrong real gid\n");
	}

	/* do activity here */
	if (setgid(0) == 0)
	{
		fail++;
		printf("MAYDAY!  setgid(0) succeeded (should have failed)\n");
	}
	else
	{
		printf("setgid(0) failed (this is correct)\n");
	}
	printgids("after setgid(0)", effgid, effgid);

	if (getegid() != effgid)
	{
		fail++;
		printf("MAYDAY!  Wrong effective gid\n");
	}
	if (getgid() != effgid)
	{
		fail++;
		printf("MAYDAY!  Wrong real gid\n");
	}
	printf("\n");

	if (fail > 0)
	{
		printf("\nThis system cannot use %s to set the real gid to the effective gid\nand clear the saved gid.\n",
#if SM_CONF_SETREGID
			"setregid"
#else /* SM_CONF_SETREGID */
			"setgid"
#endif /* SM_CONF_SETREGID */
			);
		exit(1);
	}

	printf("\nIt is possible to use setgid on this system\n");
	exit(0);
}
