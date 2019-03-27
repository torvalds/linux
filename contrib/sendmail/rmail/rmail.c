/*
 * Copyright (c) 1998-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 * $FreeBSD$
 *
 */

#include <sm/gen.h>

SM_IDSTR(copyright,
"@(#) Copyright (c) 1998-2001 Proofpoint, Inc. and its suppliers.\n\
	All rights reserved.\n\
     Copyright (c) 1988, 1993\n\
	The Regents of the University of California.  All rights reserved.\n")

SM_IDSTR(id, "@(#)$Id: rmail.c,v 8.63 2013-11-22 20:51:53 ca Exp $")

/*
 * RMAIL -- UUCP mail server.
 *
 * This program reads the >From ... remote from ... lines that UUCP is so
 * fond of and turns them into something reasonable.  It then execs sendmail
 * with various options built from these lines.
 *
 * The expected syntax is:
 *
 *	 <user> := [-a-z0-9]+
 *	 <date> := ctime format
 *	 <site> := [-a-z0-9!]+
 * <blank line> := "^\n$"
 *	 <from> := "From" <space> <user> <space> <date>
 *		  [<space> "remote from" <space> <site>]
 *    <forward> := ">" <from>
 *	    msg := <from> <forward>* <blank-line> <body>
 *
 * The output of rmail(8) compresses the <forward> lines into a single
 * from path.
 *
 * The err(3) routine is included here deliberately to make this code
 * a bit more portable.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <ctype.h>
#include <fcntl.h>
#include <sm/io.h>
#include <stdlib.h>
#include <sm/string.h>
#include <unistd.h>
#ifdef EX_OK
# undef EX_OK		/* unistd.h may have another use for this */
#endif /* EX_OK */
#include <sysexits.h>

#include <sm/conf.h>
#include <sm/errstring.h>
#include <sendmail/pathnames.h>

static void err __P((int, const char *, ...));
static void usage __P((void));
static char *xalloc __P((int));

#define newstr(s)	strcpy(xalloc(strlen(s) + 1), s)

static char *
xalloc(sz)
	register int sz;
{
	register char *p;

	/* some systems can't handle size zero mallocs */
	if (sz <= 0)
		sz = 1;

	p = malloc(sz);
	if (p == NULL)
		err(EX_TEMPFAIL, "out of memory");
	return (p);
}

int
main(argc, argv)
	int argc;
	char *argv[];
{
	int ch, debug, i, pdes[2], pid, status;
	size_t fplen = 0, fptlen = 0, len;
	off_t offset;
	SM_FILE_T *fp;
	char *addrp = NULL, *domain, *p, *t;
	char *from_path, *from_sys, *from_user;
	char **args, buf[2048], lbuf[2048];
	struct stat sb;
	extern char *optarg;
	extern int optind;

	debug = 0;
	domain = "UUCP";		/* Default "domain". */
	while ((ch = getopt(argc, argv, "D:T")) != -1)
	{
		switch (ch)
		{
		  case 'T':
			debug = 1;
			break;

		  case 'D':
			domain = optarg;
			break;

		  case '?':
		  default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();

	from_path = from_sys = from_user = NULL;
	for (offset = 0; ; )
	{
		/* Get and nul-terminate the line. */
		if (sm_io_fgets(smioin, SM_TIME_DEFAULT, lbuf,
				sizeof(lbuf)) < 0)
			err(EX_DATAERR, "no data");
		if ((p = strchr(lbuf, '\n')) == NULL)
			err(EX_DATAERR, "line too long");
		*p = '\0';

		/* Parse lines until reach a non-"From" line. */
		if (!strncmp(lbuf, "From ", 5))
			addrp = lbuf + 5;
		else if (!strncmp(lbuf, ">From ", 6))
			addrp = lbuf + 6;
		else if (offset == 0)
			err(EX_DATAERR,
			    "missing or empty From line: %s", lbuf);
		else
		{
			*p = '\n';
			break;
		}

		if (addrp == NULL || *addrp == '\0')
			err(EX_DATAERR, "corrupted From line: %s", lbuf);

		/* Use the "remote from" if it exists. */
		for (p = addrp; (p = strchr(p + 1, 'r')) != NULL; )
		{
			if (!strncmp(p, "remote from ", 12))
			{
				for (t = p += 12; *t != '\0'; ++t)
				{
					if (isascii(*t) && isspace(*t))
						break;
				}
				*t = '\0';
				if (debug)
					(void) sm_io_fprintf(smioerr,
							     SM_TIME_DEFAULT,
							     "remote from: %s\n",
							     p);
				break;
			}
		}

		/* Else use the string up to the last bang. */
		if (p == NULL)
		{
			if (*addrp == '!')
				err(EX_DATAERR, "bang starts address: %s",
				    addrp);
			else if ((t = strrchr(addrp, '!')) != NULL)
			{
				*t = '\0';
				p = addrp;
				addrp = t + 1;
				if (*addrp == '\0')
					err(EX_DATAERR,
					    "corrupted From line: %s", lbuf);
				if (debug)
					(void) sm_io_fprintf(smioerr,
							     SM_TIME_DEFAULT,
							     "bang: %s\n", p);
			}
		}

		/* 'p' now points to any system string from this line. */
		if (p != NULL)
		{
			/* Nul terminate it as necessary. */
			for (t = p; *t != '\0'; ++t)
			{
				if (isascii(*t) && isspace(*t))
					break;
			}
			*t = '\0';

			/* If the first system, copy to the from_sys string. */
			if (from_sys == NULL)
			{
				from_sys = newstr(p);
				if (debug)
					(void) sm_io_fprintf(smioerr,
							     SM_TIME_DEFAULT,
							     "from_sys: %s\n",
							     from_sys);
			}

			/* Concatenate to the path string. */
			len = t - p;
			if (from_path == NULL)
			{
				fplen = 0;
				if ((from_path = malloc(fptlen = 256)) == NULL)
					err(EX_TEMPFAIL, "out of memory");
			}
			if (fplen + len + 2 > fptlen)
			{
				fptlen += SM_MAX(fplen + len + 2, 256);
				if ((from_path = realloc(from_path,
							 fptlen)) == NULL)
					err(EX_TEMPFAIL, "out of memory");
			}
			memmove(from_path + fplen, p, len);
			fplen += len;
			from_path[fplen++] = '!';
			from_path[fplen] = '\0';
		}

		/* Save off from user's address; the last one wins. */
		for (p = addrp; *p != '\0'; ++p)
		{
			if (isascii(*p) && isspace(*p))
				break;
		}
		*p = '\0';
		if (*addrp == '\0')
			addrp = "<>";
		if (from_user != NULL)
			free(from_user);
		from_user = newstr(addrp);

		if (debug)
		{
			if (from_path != NULL)
				(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
						     "from_path: %s\n",
						     from_path);
			(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
					     "from_user: %s\n", from_user);
		}

		if (offset != -1)
			offset = (off_t)sm_io_tell(smioin, SM_TIME_DEFAULT);
	}


	/* Allocate args (with room for sendmail args as well as recipients */
	args = (char **)xalloc(sizeof(*args) * (10 + argc));

	i = 0;
	args[i++] = _PATH_SENDMAIL;	/* Build sendmail's argument list. */
	args[i++] = "-G";		/* relay submission */
	args[i++] = "-oee";		/* No errors, just status. */
#ifdef QUEUE_ONLY
	args[i++] = "-odq";		/* Queue it, don't try to deliver. */
#else
	args[i++] = "-odi";		/* Deliver in foreground. */
#endif
	args[i++] = "-oi";		/* Ignore '.' on a line by itself. */

	/* set from system and protocol used */
	if (from_sys == NULL)
		sm_snprintf(buf, sizeof(buf), "-p%s", domain);
	else if (strchr(from_sys, '.') == NULL)
		sm_snprintf(buf, sizeof(buf), "-p%s:%s.%s",
			domain, from_sys, domain);
	else
		sm_snprintf(buf, sizeof(buf), "-p%s:%s", domain, from_sys);
	args[i++] = newstr(buf);

	/* Set name of ``from'' person. */
	sm_snprintf(buf, sizeof(buf), "-f%s%s",
		 from_path ? from_path : "", from_user);
	args[i++] = newstr(buf);

	/*
	**  Don't copy arguments beginning with - as they will be
	**  passed to sendmail and could be interpreted as flags.
	**  To prevent confusion of sendmail wrap < and > around
	**  the address (helps to pass addrs like @gw1,@gw2:aa@bb)
	*/

	while (*argv != NULL)
	{
		if (**argv == '-')
			err(EX_USAGE, "dash precedes argument: %s", *argv);

		if (strchr(*argv, ',') == NULL || strchr(*argv, '<') != NULL)
			args[i++] = *argv;
		else
		{
			len = strlen(*argv) + 3;
			if ((args[i] = malloc(len)) == NULL)
				err(EX_TEMPFAIL, "Cannot malloc");
			sm_snprintf(args[i++], len, "<%s>", *argv);
		}
		argv++;
		argc--;

		/* Paranoia check, argc used for args[] bound */
		if (argc < 0)
			err(EX_SOFTWARE, "Argument count mismatch");
	}
	args[i] = NULL;

	if (debug)
	{
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "Sendmail arguments:\n");
		for (i = 0; args[i] != NULL; i++)
			(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
					     "\t%s\n", args[i]);
	}

	/*
	**  If called with a regular file as standard input, seek to the right
	**  position in the file and just exec sendmail.  Could probably skip
	**  skip the stat, but it's not unreasonable to believe that a failed
	**  seek will cause future reads to fail.
	*/

	if (!fstat(STDIN_FILENO, &sb) && S_ISREG(sb.st_mode))
	{
		if (lseek(STDIN_FILENO, offset, SEEK_SET) != offset)
			err(EX_TEMPFAIL, "stdin seek");
		(void) execv(_PATH_SENDMAIL, args);
		err(EX_OSERR, "%s", _PATH_SENDMAIL);
	}

	if (pipe(pdes) < 0)
		err(EX_OSERR, "pipe failed");

	switch (pid = fork())
	{
	  case -1:				/* Err. */
		err(EX_OSERR, "fork failed");
		/* NOTREACHED */

	  case 0:				/* Child. */
		if (pdes[0] != STDIN_FILENO)
		{
			(void) dup2(pdes[0], STDIN_FILENO);
			(void) close(pdes[0]);
		}
		(void) close(pdes[1]);
		(void) execv(_PATH_SENDMAIL, args);
		err(EX_UNAVAILABLE, "%s", _PATH_SENDMAIL);
		/* NOTREACHED */
	}

	if ((fp = sm_io_open(SmFtStdiofd, SM_TIME_DEFAULT, (void *) &(pdes[1]),
			     SM_IO_WRONLY, NULL)) == NULL)
		err(EX_OSERR, "sm_io_open failed");
	(void) close(pdes[0]);

	/* Copy the file down the pipe. */
	do
	{
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "%s", lbuf);
	} while (sm_io_fgets(smioin, SM_TIME_DEFAULT, lbuf,
			     sizeof(lbuf)) >= 0);

	if (sm_io_error(smioin))
		err(EX_TEMPFAIL, "stdin: %s", sm_errstring(errno));

	if (sm_io_close(fp, SM_TIME_DEFAULT))
		err(EX_OSERR, "sm_io_close failed");

	if ((waitpid(pid, &status, 0)) == -1)
		err(EX_OSERR, "%s", _PATH_SENDMAIL);

	if (!WIFEXITED(status))
		err(EX_OSERR, "%s: did not terminate normally", _PATH_SENDMAIL);

	if (WEXITSTATUS(status))
		err(status, "%s: terminated with %d (non-zero) status",
		    _PATH_SENDMAIL, WEXITSTATUS(status));
	exit(EX_OK);
	/* NOTREACHED */
	return EX_OK;
}

static void
usage()
{
	(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
			     "usage: rmail [-T] [-D domain] user ...\n");
	exit(EX_USAGE);
}

static void
#ifdef __STDC__
err(int eval, const char *fmt, ...)
#else /* __STDC__ */
err(eval, fmt, va_alist)
	int eval;
	const char *fmt;
	va_dcl
#endif /* __STDC__ */
{
	SM_VA_LOCAL_DECL

	if (fmt != NULL)
	{
		SM_VA_START(ap, fmt);
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT, "rmail: ");
		(void) sm_io_vfprintf(smioerr, SM_TIME_DEFAULT, fmt, ap);
		SM_VA_END(ap);
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT, "\n");
	}
	exit(eval);
}
