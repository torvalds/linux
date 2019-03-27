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
**  Compilation is trivial -- just "cc t_dropgid.c".  Make it set-group-ID
**  guest and then execute it as a non-root user.
*/

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

#ifndef lint
static char id[] = "@(#)$Id: t_dropgid.c,v 1.7 2013-11-22 20:52:01 ca Exp $";
#endif /* ! lint */

static void
printgids(str, r, e)
	char *str;
	gid_t r, e;
{
	printf("%s (should be %d/%d): r/egid=%d/%d\n", str, (int) r, (int) e,
	       (int) getgid(), (int) getegid());
}

/* define only one of these */
#if HASSETEGID
# define SETGIDCALL	"setegid"
#endif /* HASSETEGID */
#if HASSETREGID
# define SETGIDCALL	"setregid"
#endif /* HASSETREGID */
#if HASSETRESGID
# define SETGIDCALL	"setresgid"
#endif /* HASSETRESGID */

#ifndef SETGIDCALL
#  define SETGIDCALL	"setgid"
#endif /* ! SETGIDCALL */

int
main(argc, argv)
	int argc;
	char **argv;
{
	int fail = 0;
	int res;
	gid_t realgid = getgid();
	gid_t effgid = getegid();
	char *prg = argv[0];

	printgids("initial gids", realgid, effgid);

	if (effgid == realgid)
	{
		printf("SETUP ERROR: re-run set-group-ID guest\n");
		printf("Use chgrp(1) and chmod(1)\n");
		printf("For example, do this as root ");
		printf("(nobody is the name of a group in this example):\n");
		printf("# chgrp nobody %s\n", prg);
		printf("# chmod g+s nobody %s\n", prg);
		exit(1);
	}

#if HASSETREGID
	res = setregid(realgid, realgid);
	printf("setregid(%d)=%d %s\n", (int) realgid, res,
		res < 0 ? "failure" : "ok");
	printgids("after setregid()", realgid, realgid);
#endif /* HASSETREGID */
#if HASSETRESGID
	res = setresgid(realgid, realgid, realgid);
	printf("setresgid(%d)=%d %s\n", (int) realgid, res,
		res < 0 ? "failure" : "ok");
	printgids("after setresgid()", realgid, realgid);
#endif /* HASSETRESGID */
#if HASSETEGID
	res = setegid(realgid);
	printf("setegid(%d)=%d %s\n", (int) realgid, res,
		res < 0 ? "failure" : "ok");
	printgids("after setegid()", realgid, realgid);
#endif /* HASSETEGID */
	res = setgid(realgid);
	printf("setgid(%d)=%d %s\n", (int) realgid, res,
		res < 0 ? "failure" : "ok");
	printgids("after setgid()", realgid, realgid);

	if (getegid() != realgid)
	{
		fail++;
		printf("MAYDAY!  Wrong effective gid\n");
	}

	if (getgid() != realgid)
	{
		fail++;
		printf("MAYDAY!  Wrong real gid\n");
	}

	/* do activity here */
	if (setgid(effgid) == 0)
	{
		fail++;
		printf("MAYDAY!  setgid(%d) succeeded (should have failed)\n",
			effgid);
	}
	else
	{
		printf("setgid(%d) failed (this is correct)\n", effgid);
	}
	printgids("after setgid() to egid", realgid, realgid);

	if (getegid() != realgid)
	{
		fail++;
		printf("MAYDAY!  Wrong effective gid\n");
	}
	if (getgid() != realgid)
	{
		fail++;
		printf("MAYDAY!  Wrong real gid\n");
	}
	printf("\n");

	if (fail > 0)
	{
		printf("\nThis system cannot use %s to give up set-group-ID rights\n",
		       SETGIDCALL);
#if !HASSETEGID
		printf("Maybe compile with -DHASSETEGID and try again\n");
#endif /* !HASSETEGID */
#if !HASSETREGID
		printf("Maybe compile with -DHASSETREGID and try again\n");
#endif /* !HASSETREGID */
#if !HASSETRESGID
		printf("Maybe compile with -DHASSETRESGID and try again\n");
#endif /* !HASSETRESGID */
		exit(1);
	}

	printf("\nIt is possible to use %s on this system\n", SETGIDCALL);
	exit(0);
}
