/*
 * Copyright (c) 1998-2004 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1993 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>

SM_IDSTR(copyright,
"@(#) Copyright (c) 1998-2004 Proofpoint, Inc. and its suppliers.\n\
	All rights reserved.\n\
     Copyright (c) 1993 Eric P. Allman.  All rights reserved.\n\
     Copyright (c) 1993\n\
	The Regents of the University of California.  All rights reserved.\n")

SM_IDSTR(id, "@(#)$Id: smrsh.c,v 8.66 2013-11-22 20:52:00 ca Exp $")

/*
**  SMRSH -- sendmail restricted shell
**
**	This is a patch to get around the prog mailer bugs in most
**	versions of sendmail.
**
**	Use this in place of /bin/sh in the "prog" mailer definition
**	in your sendmail.cf file.  You then create CMDDIR (owned by
**	root, mode 755) and put links to any programs you want
**	available to prog mailers in that directory.  This should
**	include things like "vacation" and "procmail", but not "sed"
**	or "sh".
**
**	Leading pathnames are stripped from program names so that
**	existing .forward files that reference things like
**	"/usr/bin/vacation" will continue to work.
**
**	The following characters are completely illegal:
**		<  >  ^  &  `  (  ) \n \r
**	The following characters are sometimes illegal:
**		|  &
**	This is more restrictive than strictly necessary.
**
**	To use this, add FEATURE(`smrsh') to your .mc file.
**
**	This can be used on any version of sendmail.
**
**	In loving memory of RTM.  11/02/93.
*/

#include <unistd.h>
#include <sm/io.h>
#include <sm/limits.h>
#include <sm/string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#ifdef EX_OK
# undef EX_OK
#endif /* EX_OK */
#include <sysexits.h>
#include <syslog.h>
#include <stdlib.h>

#include <sm/conf.h>
#include <sm/errstring.h>

/* directory in which all commands must reside */
#ifndef CMDDIR
# ifdef SMRSH_CMDDIR
#  define CMDDIR	SMRSH_CMDDIR
# else /* SMRSH_CMDDIR */
#  define CMDDIR	"/usr/adm/sm.bin"
# endif /* SMRSH_CMDDIR */
#endif /* ! CMDDIR */

/* characters disallowed in the shell "-c" argument */
#define SPECIALS	"<|>^();&`$\r\n"

/* default search path */
#ifndef PATH
# ifdef SMRSH_PATH
#  define PATH		SMRSH_PATH
# else /* SMRSH_PATH */
#  define PATH		"/bin:/usr/bin:/usr/ucb"
# endif /* SMRSH_PATH */
#endif /* ! PATH */

char newcmdbuf[1000];
char *prg, *par;

static void	addcmd __P((char *, bool, size_t));

/*
**  ADDCMD -- add a string to newcmdbuf, check for overflow
**
**    Parameters:
**	s -- string to add
**	cmd -- it's a command: prepend CMDDIR/
**	len -- length of string to add
**
**    Side Effects:
**	changes newcmdbuf or exits with a failure.
**
*/

static void
addcmd(s, cmd, len)
	char *s;
	bool cmd;
	size_t len;
{
	if (s == NULL || *s == '\0')
		return;

	/* enough space for s (len) and CMDDIR + "/" and '\0'? */
	if (sizeof newcmdbuf - strlen(newcmdbuf) <=
	    len + 1 + (cmd ? (strlen(CMDDIR) + 1) : 0))
	{
		(void)sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				    "%s: command too long: %s\n", prg, par);
#ifndef DEBUG
		syslog(LOG_WARNING, "command too long: %.40s", par);
#endif /* ! DEBUG */
		exit(EX_UNAVAILABLE);
	}
	if (cmd)
		(void) sm_strlcat2(newcmdbuf, CMDDIR, "/", sizeof newcmdbuf);
	(void) strncat(newcmdbuf, s, len);
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	register char *p;
	register char *q;
	register char *r;
	register char *cmd;
	int isexec;
	int save_errno;
	char *newenv[2];
	char pathbuf[1000];
	char specialbuf[32];
	struct stat st;

#ifndef DEBUG
# ifndef LOG_MAIL
	openlog("smrsh", 0);
# else /* ! LOG_MAIL */
	openlog("smrsh", LOG_ODELAY|LOG_CONS, LOG_MAIL);
# endif /* ! LOG_MAIL */
#endif /* ! DEBUG */

	(void) sm_strlcpyn(pathbuf, sizeof pathbuf, 2, "PATH=", PATH);
	newenv[0] = pathbuf;
	newenv[1] = NULL;

	/*
	**  Do basic argv usage checking
	*/

	prg = argv[0];

	if (argc != 3 || strcmp(argv[1], "-c") != 0)
	{
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "Usage: %s -c command\n", prg);
#ifndef DEBUG
		syslog(LOG_ERR, "usage");
#endif /* ! DEBUG */
		exit(EX_USAGE);
	}

	par = argv[2];

	/*
	**  Disallow special shell syntax.  This is overly restrictive,
	**  but it should shut down all attacks.
	**  Be sure to include 8-bit versions, since many shells strip
	**  the address to 7 bits before checking.
	*/

	if (strlen(SPECIALS) * 2 >= sizeof specialbuf)
	{
#ifndef DEBUG
		syslog(LOG_ERR, "too many specials: %.40s", SPECIALS);
#endif /* ! DEBUG */
		exit(EX_UNAVAILABLE);
	}
	(void) sm_strlcpy(specialbuf, SPECIALS, sizeof specialbuf);
	for (p = specialbuf; *p != '\0'; p++)
		*p |= '\200';
	(void) sm_strlcat(specialbuf, SPECIALS, sizeof specialbuf);

	/*
	**  Do a quick sanity check on command line length.
	*/

	if (strlen(par) > (sizeof newcmdbuf - sizeof CMDDIR - 2))
	{
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "%s: command too long: %s\n", prg, par);
#ifndef DEBUG
		syslog(LOG_WARNING, "command too long: %.40s", par);
#endif /* ! DEBUG */
		exit(EX_UNAVAILABLE);
	}

	q = par;
	newcmdbuf[0] = '\0';
	isexec = false;

	while (*q != '\0')
	{
		/*
		**  Strip off a leading pathname on the command name.  For
		**  example, change /usr/ucb/vacation to vacation.
		*/

		/* strip leading spaces */
		while (*q != '\0' && isascii(*q) && isspace(*q))
			q++;
		if (*q == '\0')
		{
			if (isexec)
			{
				(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
						     "%s: missing command to exec\n",
						     prg);
#ifndef DEBUG
				syslog(LOG_CRIT, "uid %d: missing command to exec", (int) getuid());
#endif /* ! DEBUG */
				exit(EX_UNAVAILABLE);
			}
			break;
		}

		/* find the end of the command name */
		p = strpbrk(q, " \t");
		if (p == NULL)
			cmd = &q[strlen(q)];
		else
		{
			*p = '\0';
			cmd = p;
		}
		/* search backwards for last / (allow for 0200 bit) */
		while (cmd > q)
		{
			if ((*--cmd & 0177) == '/')
			{
				cmd++;
				break;
			}
		}
		/* cmd now points at final component of path name */

		/* allow a few shell builtins */
		if (strcmp(q, "exec") == 0 && p != NULL)
		{
			addcmd("exec ", false, strlen("exec "));

			/* test _next_ arg */
			q = ++p;
			isexec = true;
			continue;
		}
		else if (strcmp(q, "exit") == 0 || strcmp(q, "echo") == 0)
		{
			addcmd(cmd, false, strlen(cmd));

			/* test following chars */
		}
		else
		{
			char cmdbuf[MAXPATHLEN];

			/*
			**  Check to see if the command name is legal.
			*/

			if (sm_strlcpyn(cmdbuf, sizeof cmdbuf, 3, CMDDIR,
					"/", cmd) >= sizeof cmdbuf)
			{
				/* too long */
				(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
						     "%s: \"%s\" not available for sendmail programs (filename too long)\n",
						      prg, cmd);
				if (p != NULL)
					*p = ' ';
#ifndef DEBUG
				syslog(LOG_CRIT, "uid %d: attempt to use \"%s\" (filename too long)",
				       (int) getuid(), cmd);
#endif /* ! DEBUG */
				exit(EX_UNAVAILABLE);
			}

#ifdef DEBUG
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Trying %s\n", cmdbuf);
#endif /* DEBUG */
			if (stat(cmdbuf, &st) < 0)
			{
				/* can't stat it */
				(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
						     "%s: \"%s\" not available for sendmail programs (stat failed)\n",
						      prg, cmd);
				if (p != NULL)
					*p = ' ';
#ifndef DEBUG
				syslog(LOG_CRIT, "uid %d: attempt to use \"%s\" (stat failed)",
				       (int) getuid(), cmd);
#endif /* ! DEBUG */
				exit(EX_UNAVAILABLE);
			}
			if (!S_ISREG(st.st_mode)
#ifdef S_ISLNK
			    && !S_ISLNK(st.st_mode)
#endif /* S_ISLNK */
			   )
			{
				/* can't stat it */
				(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
						     "%s: \"%s\" not available for sendmail programs (not a file)\n",
						      prg, cmd);
				if (p != NULL)
					*p = ' ';
#ifndef DEBUG
				syslog(LOG_CRIT, "uid %d: attempt to use \"%s\" (not a file)",
				       (int) getuid(), cmd);
#endif /* ! DEBUG */
				exit(EX_UNAVAILABLE);
			}
			if (access(cmdbuf, X_OK) < 0)
			{
				/* oops....  crack attack possiblity */
				(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
						     "%s: \"%s\" not available for sendmail programs\n",
						      prg, cmd);
				if (p != NULL)
					*p = ' ';
#ifndef DEBUG
				syslog(LOG_CRIT, "uid %d: attempt to use \"%s\"",
				       (int) getuid(), cmd);
#endif /* ! DEBUG */
				exit(EX_UNAVAILABLE);
			}

			/*
			**  Create the actual shell input.
			*/

			addcmd(cmd, true, strlen(cmd));
		}
		isexec = false;

		if (p != NULL)
			*p = ' ';
		else
			break;

		r = strpbrk(p, specialbuf);
		if (r == NULL)
		{
			addcmd(p, false, strlen(p));
			break;
		}
#if ALLOWSEMI
		if (*r == ';')
		{
			addcmd(p, false,  r - p + 1);
			q = r + 1;
			continue;
		}
#endif /* ALLOWSEMI */
		if ((*r == '&' && *(r + 1) == '&') ||
		    (*r == '|' && *(r + 1) == '|'))
		{
			addcmd(p, false,  r - p + 2);
			q = r + 2;
			continue;
		}

		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "%s: cannot use %c in command\n", prg, *r);
#ifndef DEBUG
		syslog(LOG_CRIT, "uid %d: attempt to use %c in command: %s",
		       (int) getuid(), *r, par);
#endif /* ! DEBUG */
		exit(EX_UNAVAILABLE);
	}
	if (isexec)
	{
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "%s: missing command to exec\n", prg);
#ifndef DEBUG
		syslog(LOG_CRIT, "uid %d: missing command to exec",
		       (int) getuid());
#endif /* ! DEBUG */
		exit(EX_UNAVAILABLE);
	}
	/* make sure we created something */
	if (newcmdbuf[0] == '\0')
	{
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "Usage: %s -c command\n", prg);
#ifndef DEBUG
		syslog(LOG_ERR, "usage");
#endif /* ! DEBUG */
		exit(EX_USAGE);
	}

	/*
	**  Now invoke the shell
	*/

#ifdef DEBUG
	(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "%s\n", newcmdbuf);
#endif /* DEBUG */
	(void) execle("/bin/sh", "/bin/sh", "-c", newcmdbuf,
		      (char *)NULL, newenv);
	save_errno = errno;
#ifndef DEBUG
	syslog(LOG_CRIT, "Cannot exec /bin/sh: %s", sm_errstring(errno));
#endif /* ! DEBUG */
	errno = save_errno;
	sm_perror("/bin/sh");
	exit(EX_OSFILE);
	/* NOTREACHED */
	return EX_OSFILE;
}
