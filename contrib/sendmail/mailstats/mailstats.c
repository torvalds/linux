/*
 * Copyright (c) 1998-2002, 2013 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1983 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *
 */

#include <sm/gen.h>

SM_IDSTR(copyright,
"@(#) Copyright (c) 1998-2002 Proofpoint, Inc. and its suppliers.\n\
	All rights reserved.\n\
     Copyright (c) 1988, 1993\n\
	The Regents of the University of California.  All rights reserved.\n")

SM_IDSTR(id, "@(#)$Id: mailstats.c,v 8.103 2013-11-22 20:51:51 ca Exp $")

#include <unistd.h>
#include <stddef.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#ifdef EX_OK
# undef EX_OK		/* unistd.h may have another use for this */
#endif /* EX_OK */
#include <sysexits.h>

#include <sm/errstring.h>
#include <sm/limits.h>
#include <sendmail/sendmail.h>
#include <sendmail/mailstats.h>
#include <sendmail/pathnames.h>


#define MNAMELEN	20	/* max length of mailer name */

int
main(argc, argv)
	int argc;
	char **argv;
{
	register int i;
	int mno;
	int save_errno;
	int ch, fd;
	char *sfile;
	char *cfile;
	SM_FILE_T *cfp;
	bool mnames;
	bool progmode;
	bool trunc;
	long frmsgs = 0, frbytes = 0, tomsgs = 0, tobytes = 0, rejmsgs = 0;
	long dismsgs = 0;
	long quarmsgs = 0;
	time_t now;
	char mtable[MAXMAILERS][MNAMELEN + 1];
	char sfilebuf[MAXPATHLEN];
	char buf[MAXLINE];
	struct statistics stats;
	extern char *ctime();
	extern char *optarg;
	extern int optind;
# define MSOPTS "cC:f:opP"

	cfile = getcfname(0, 0, SM_GET_SENDMAIL_CF, NULL);
	sfile = NULL;
	mnames = true;
	progmode = false;
	trunc = false;
	while ((ch = getopt(argc, argv, MSOPTS)) != -1)
	{
		switch (ch)
		{
		  case 'c':
			cfile = getcfname(0, 0, SM_GET_SUBMIT_CF, NULL);
			break;

		  case 'C':
			cfile = optarg;
			break;

		  case 'f':
			sfile = optarg;
			break;


		  case 'o':
			mnames = false;
			break;

		  case 'p':
			trunc = true;
			/* FALLTHROUGH */

		  case 'P':
			progmode = true;
			break;


		  case '?':
		  default:
  usage:
			(void) sm_io_fputs(smioerr, SM_TIME_DEFAULT,
			    "usage: mailstats [-C cffile] [-c] [-P] [-f stfile] [-o] [-p]\n");
			exit(EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 0)
		goto usage;

	if ((cfp = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, cfile, SM_IO_RDONLY,
			      NULL)) == NULL)
	{
		save_errno = errno;
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT, "mailstats: ");
		errno = save_errno;
		sm_perror(cfile);
		exit(EX_NOINPUT);
	}

	mno = 0;
	(void) sm_strlcpy(mtable[mno++], "prog", MNAMELEN + 1);
	(void) sm_strlcpy(mtable[mno++], "*file*", MNAMELEN + 1);
	(void) sm_strlcpy(mtable[mno++], "*include*", MNAMELEN + 1);

	while (sm_io_fgets(cfp, SM_TIME_DEFAULT, buf, sizeof(buf)) >= 0)
	{
		register char *b;
		char *s;
		register char *m;

		b = strchr(buf, '#');
		if (b == NULL)
			b = strchr(buf, '\n');
		if (b == NULL)
			b = &buf[strlen(buf)];
		while (isascii(*--b) && isspace(*b))
			continue;
		*++b = '\0';

		b = buf;
		switch (*b++)
		{
		  case 'M':		/* mailer definition */
			break;

		  case 'O':		/* option -- see if .st file */
			if (sm_strncasecmp(b, " StatusFile", 11) == 0 &&
			    !(isascii(b[11]) && isalnum(b[11])))
			{
				/* new form -- find value */
				b = strchr(b, '=');
				if (b == NULL)
					continue;
				while (isascii(*++b) && isspace(*b))
					continue;
			}
			else if (*b++ != 'S')
			{
				/* something else boring */
				continue;
			}

			/* this is the S or StatusFile option -- save it */
			if (sm_strlcpy(sfilebuf, b, sizeof sfilebuf) >=
			    sizeof sfilebuf)
			{
				(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
						     "StatusFile filename too long: %.30s...\n",
						     b);
				exit(EX_CONFIG);
			}
			if (sfile == NULL)
				sfile = sfilebuf;

		  default:
			continue;
		}

		if (mno >= MAXMAILERS)
		{
			(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
					     "Too many mailers defined, %d max.\n",
					     MAXMAILERS);
			exit(EX_SOFTWARE);
		}
		m = mtable[mno];
		s = m + MNAMELEN;		/* is [MNAMELEN + 1] */
		while (*b != ',' && !(isascii(*b) && isspace(*b)) &&
		       *b != '\0' && m < s)
			*m++ = *b++;
		*m = '\0';
		for (i = 0; i < mno; i++)
		{
			if (strcmp(mtable[i], mtable[mno]) == 0)
				break;
		}
		if (i == mno)
			mno++;
	}
	(void) sm_io_close(cfp, SM_TIME_DEFAULT);
	for (; mno < MAXMAILERS; mno++)
		mtable[mno][0] = '\0';

	if (sfile == NULL)
	{
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "mailstats: no statistics file located\n");
		exit(EX_OSFILE);
	}

	fd = open(sfile, O_RDONLY, 0600);
	if ((fd < 0) || (i = read(fd, &stats, sizeof stats)) < 0)
	{
		save_errno = errno;
		(void) sm_io_fputs(smioerr, SM_TIME_DEFAULT, "mailstats: ");
		errno = save_errno;
		sm_perror(sfile);
		exit(EX_NOINPUT);
	}
	if (i == 0)
	{
		(void) sleep(1);
		if ((i = read(fd, &stats, sizeof stats)) < 0)
		{
			save_errno = errno;
			(void) sm_io_fputs(smioerr, SM_TIME_DEFAULT,
					   "mailstats: ");
			errno = save_errno;
			sm_perror(sfile);
			exit(EX_NOINPUT);
		}
		else if (i == 0)
		{
			memset((ARBPTR_T) &stats, '\0', sizeof stats);
			(void) time(&stats.stat_itime);
		}
	}
	if (i != 0)
	{
		if (stats.stat_magic != STAT_MAGIC)
		{
			(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
					     "mailstats: incorrect magic number in %s\n",
					     sfile);
			exit(EX_OSERR);
		}
		else if (stats.stat_version != STAT_VERSION)
		{
			(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
					     "mailstats version (%d) incompatible with %s version (%d)\n",
					     STAT_VERSION, sfile,
					     stats.stat_version);

			exit(EX_OSERR);
		}
		else if (i != sizeof stats || stats.stat_size != sizeof(stats))
		{
			(void) sm_io_fputs(smioerr, SM_TIME_DEFAULT,
					   "mailstats: file size changed.\n");
			exit(EX_OSERR);
		}
	}


	if (progmode)
	{
		(void) time(&now);
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "%ld %ld\n",
				     (long) stats.stat_itime, (long) now);
	}
	else
	{
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Statistics from %s",
				     ctime(&stats.stat_itime));
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     " M   msgsfr  bytes_from   msgsto    bytes_to  msgsrej msgsdis");
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, " msgsqur");
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "%s\n",
				     mnames ? "  Mailer" : "");
	}
	for (i = 0; i < MAXMAILERS; i++)
	{
		if (stats.stat_nf[i] || stats.stat_nt[i] ||
		    stats.stat_nq[i] ||
		    stats.stat_nr[i] || stats.stat_nd[i])
		{
			char *format;

			if (progmode)
				format = "%2d %8ld %10ld %8ld %10ld   %6ld  %6ld";
			else
				format = "%2d %8ld %10ldK %8ld %10ldK   %6ld  %6ld";
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     format, i,
					     stats.stat_nf[i],
					     stats.stat_bf[i],
					     stats.stat_nt[i],
					     stats.stat_bt[i],
					     stats.stat_nr[i],
					     stats.stat_nd[i]);
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "  %6ld", stats.stat_nq[i]);
			if (mnames)
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "  %s",
						      mtable[i]);
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "\n");
			frmsgs += stats.stat_nf[i];
			frbytes += stats.stat_bf[i];
			tomsgs += stats.stat_nt[i];
			tobytes += stats.stat_bt[i];
			rejmsgs += stats.stat_nr[i];
			dismsgs += stats.stat_nd[i];
			quarmsgs += stats.stat_nq[i];
		}
	}
	if (progmode)
	{
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     " T %8ld %10ld %8ld %10ld   %6ld  %6ld",
				     frmsgs, frbytes, tomsgs, tobytes, rejmsgs,
				     dismsgs);
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "  %6ld", quarmsgs);
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "\n");
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     " C %8ld %8ld %6ld\n",
				     stats.stat_cf, stats.stat_ct,
				     stats.stat_cr);
		(void) close(fd);
		if (trunc)
		{
			fd = open(sfile, O_RDWR | O_TRUNC, 0600);
			if (fd >= 0)
				(void) close(fd);
		}
	}
	else
	{
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "=============================================================");
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "========");
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "\n");
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     " T %8ld %10ldK %8ld %10ldK   %6ld  %6ld",
				     frmsgs, frbytes, tomsgs, tobytes, rejmsgs,
				     dismsgs);
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "  %6ld", quarmsgs);
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT, "\n");
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     " C %8ld %10s  %8ld %10s    %6ld\n",
				     stats.stat_cf, "", stats.stat_ct, "",
				     stats.stat_cr);
	}
	exit(EX_OK);
	/* NOTREACHED */
	return EX_OK;
}
