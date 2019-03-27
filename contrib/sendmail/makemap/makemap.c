/*
 * Copyright (c) 1998-2002, 2004, 2008 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1992 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>

SM_IDSTR(copyright,
"@(#) Copyright (c) 1998-2002, 2004 Proofpoint, Inc. and its suppliers.\n\
	All rights reserved.\n\
     Copyright (c) 1992 Eric P. Allman.  All rights reserved.\n\
     Copyright (c) 1992, 1993\n\
	The Regents of the University of California.  All rights reserved.\n")

SM_IDSTR(id, "@(#)$Id: makemap.c,v 8.183 2013-11-22 20:51:52 ca Exp $")


#include <sys/types.h>
#ifndef ISC_UNIX
# include <sys/file.h>
#endif /* ! ISC_UNIX */
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef EX_OK
# undef EX_OK		/* unistd.h may have another use for this */
#endif /* EX_OK */
#include <sysexits.h>
#include <sendmail/sendmail.h>
#include <sendmail/pathnames.h>
#include <libsmdb/smdb.h>

uid_t	RealUid;
gid_t	RealGid;
char	*RealUserName;
uid_t	RunAsUid;
gid_t	RunAsGid;
char	*RunAsUserName;
int	Verbose = 2;
bool	DontInitGroups = false;
uid_t	TrustedUid = 0;
BITMAP256 DontBlameSendmail;

#define BUFSIZE		1024
#define ISSEP(c) (sep == '\0' ? isascii(c) && isspace(c) : (c) == sep)

static void usage __P((char *));

static void
usage(progname)
	char *progname;
{
	sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
		      "Usage: %s [-C cffile] [-N] [-c cachesize] [-D commentchar]\n",
		      progname);
	sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
		      "       %*s [-d] [-e] [-f] [-l] [-o] [-r] [-s] [-t delimiter]\n",
		      (int) strlen(progname), "");
	sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
		      "       %*s [-u] [-v] type mapname\n",
		      (int) strlen(progname), "");
	exit(EX_USAGE);
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	char *progname;
	char *cfile;
	bool inclnull = false;
	bool notrunc = false;
	bool allowreplace = false;
	bool allowempty = false;
	bool verbose = false;
	bool foldcase = true;
	bool unmake = false;
	char sep = '\0';
	char comment = '#';
	int exitstat;
	int opt;
	char *typename = NULL;
	char *mapname = NULL;
	unsigned int lineno;
	int st;
	int mode;
	int smode;
	int putflags = 0;
	long sff = SFF_ROOTOK|SFF_REGONLY;
	struct passwd *pw;
	SMDB_DATABASE *database;
	SMDB_CURSOR *cursor;
	SMDB_DBENT db_key, db_val;
	SMDB_DBPARAMS params;
	SMDB_USER_INFO user_info;
	char ibuf[BUFSIZE];
#if HASFCHOWN
	SM_FILE_T *cfp;
	char buf[MAXLINE];
#endif /* HASFCHOWN */
	static char rnamebuf[MAXNAME];	/* holds RealUserName */
	extern char *optarg;
	extern int optind;

	memset(&params, '\0', sizeof params);
	params.smdbp_cache_size = 1024 * 1024;

	progname = strrchr(argv[0], '/');
	if (progname != NULL)
		progname++;
	else
		progname = argv[0];
	cfile = getcfname(0, 0, SM_GET_SENDMAIL_CF, NULL);

	clrbitmap(DontBlameSendmail);
	RunAsUid = RealUid = getuid();
	RunAsGid = RealGid = getgid();
	pw = getpwuid(RealUid);
	if (pw != NULL)
		(void) sm_strlcpy(rnamebuf, pw->pw_name, sizeof rnamebuf);
	else
		(void) sm_snprintf(rnamebuf, sizeof rnamebuf,
		    "Unknown UID %d", (int) RealUid);
	RunAsUserName = RealUserName = rnamebuf;
	user_info.smdbu_id = RunAsUid;
	user_info.smdbu_group_id = RunAsGid;
	(void) sm_strlcpy(user_info.smdbu_name, RunAsUserName,
		       SMDB_MAX_USER_NAME_LEN);

#define OPTIONS		"C:D:Nc:deflorst:uv"
	while ((opt = getopt(argc, argv, OPTIONS)) != -1)
	{
		switch (opt)
		{
		  case 'C':
			cfile = optarg;
			break;

		  case 'N':
			inclnull = true;
			break;

		  case 'c':
			params.smdbp_cache_size = atol(optarg);
			break;

		  case 'd':
			params.smdbp_allow_dup = true;
			break;

		  case 'e':
			allowempty = true;
			break;

		  case 'f':
			foldcase = false;
			break;

		  case 'D':
			comment = *optarg;
			break;

		  case 'l':
			smdb_print_available_types();
			exit(EX_OK);
			break;

		  case 'o':
			notrunc = true;
			break;

		  case 'r':
			allowreplace = true;
			break;

		  case 's':
			setbitn(DBS_MAPINUNSAFEDIRPATH, DontBlameSendmail);
			setbitn(DBS_WRITEMAPTOHARDLINK, DontBlameSendmail);
			setbitn(DBS_WRITEMAPTOSYMLINK, DontBlameSendmail);
			setbitn(DBS_LINKEDMAPINWRITABLEDIR, DontBlameSendmail);
			break;

		  case 't':
			if (optarg == NULL || *optarg == '\0')
			{
				sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
					      "Invalid separator\n");
				break;
			}
			sep = *optarg;
			break;

		  case 'u':
			unmake = true;
			break;

		  case 'v':
			verbose = true;
			break;

		  default:
			usage(progname);
			/* NOTREACHED */
		}
	}

	if (!bitnset(DBS_WRITEMAPTOSYMLINK, DontBlameSendmail))
		sff |= SFF_NOSLINK;
	if (!bitnset(DBS_WRITEMAPTOHARDLINK, DontBlameSendmail))
		sff |= SFF_NOHLINK;
	if (!bitnset(DBS_LINKEDMAPINWRITABLEDIR, DontBlameSendmail))
		sff |= SFF_NOWLINK;

	argc -= optind;
	argv += optind;
	if (argc != 2)
	{
		usage(progname);
		/* NOTREACHED */
	}
	else
	{
		typename = argv[0];
		mapname = argv[1];
	}

#if HASFCHOWN
	/* Find TrustedUser value in sendmail.cf */
	if ((cfp = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, cfile, SM_IO_RDONLY,
			      NULL)) == NULL)
	{
		sm_io_fprintf(smioerr, SM_TIME_DEFAULT, "makemap: %s: %s\n",
			      cfile, sm_errstring(errno));
		exit(EX_NOINPUT);
	}
	while (sm_io_fgets(cfp, SM_TIME_DEFAULT, buf, sizeof(buf)) >= 0)
	{
		register char *b;

		if ((b = strchr(buf, '\n')) != NULL)
			*b = '\0';

		b = buf;
		switch (*b++)
		{
		  case 'O':		/* option */
			if (strncasecmp(b, " TrustedUser", 12) == 0 &&
			    !(isascii(b[12]) && isalnum(b[12])))
			{
				b = strchr(b, '=');
				if (b == NULL)
					continue;
				while (isascii(*++b) && isspace(*b))
					continue;
				if (isascii(*b) && isdigit(*b))
					TrustedUid = atoi(b);
				else
				{
					TrustedUid = 0;
					pw = getpwnam(b);
					if (pw == NULL)
						(void) sm_io_fprintf(smioerr,
								     SM_TIME_DEFAULT,
								     "TrustedUser: unknown user %s\n", b);
					else
						TrustedUid = pw->pw_uid;
				}

# ifdef UID_MAX
				if (TrustedUid > UID_MAX)
				{
					(void) sm_io_fprintf(smioerr,
							     SM_TIME_DEFAULT,
							     "TrustedUser: uid value (%ld) > UID_MAX (%ld)",
						(long) TrustedUid,
						(long) UID_MAX);
					TrustedUid = 0;
				}
# endif /* UID_MAX */
				break;
			}


		  default:
			continue;
		}
	}
	(void) sm_io_close(cfp, SM_TIME_DEFAULT);
#endif /* HASFCHOWN */

	if (!params.smdbp_allow_dup && !allowreplace)
		putflags = SMDBF_NO_OVERWRITE;

	if (unmake)
	{
		mode = O_RDONLY;
		smode = S_IRUSR;
	}
	else
	{
		mode = O_RDWR;
		if (!notrunc)
		{
			mode |= O_CREAT|O_TRUNC;
			sff |= SFF_CREAT;
		}
		smode = S_IWUSR;
	}

	params.smdbp_num_elements = 4096;

	errno = smdb_open_database(&database, mapname, mode, smode, sff,
				   typename, &user_info, &params);
	if (errno != SMDBE_OK)
	{
		char *hint;

		if (errno == SMDBE_UNSUPPORTED_DB_TYPE &&
		    (hint = smdb_db_definition(typename)) != NULL)
			(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
					     "%s: Need to recompile with -D%s for %s support\n",
					     progname, hint, typename);
		else
			(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
					     "%s: error opening type %s map %s: %s\n",
					     progname, typename, mapname,
					     sm_errstring(errno));
		exit(EX_CANTCREAT);
	}

	(void) database->smdb_sync(database, 0);

	if (!unmake && geteuid() == 0 && TrustedUid != 0)
	{
		errno = database->smdb_set_owner(database, TrustedUid, -1);
		if (errno != SMDBE_OK)
		{
			(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
					     "WARNING: ownership change on %s failed %s",
					     mapname, sm_errstring(errno));
		}
	}

	/*
	**  Copy the data
	*/

	exitstat = EX_OK;
	if (unmake)
	{
		errno = database->smdb_cursor(database, &cursor, 0);
		if (errno != SMDBE_OK)
		{

			(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
					     "%s: cannot make cursor for type %s map %s\n",
					     progname, typename, mapname);
			exit(EX_SOFTWARE);
		}

		memset(&db_key, '\0', sizeof db_key);
		memset(&db_val, '\0', sizeof db_val);

		for (lineno = 0; ; lineno++)
		{
			errno = cursor->smdbc_get(cursor, &db_key, &db_val,
						  SMDB_CURSOR_GET_NEXT);
			if (errno != SMDBE_OK)
				break;

			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "%.*s%c%.*s\n",
					     (int) db_key.size,
					     (char *) db_key.data,
					     (sep != '\0') ? sep : '\t',
					     (int) db_val.size,
					     (char *)db_val.data);

		}
		(void) cursor->smdbc_close(cursor);
	}
	else
	{
		lineno = 0;
		while (sm_io_fgets(smioin, SM_TIME_DEFAULT, ibuf, sizeof ibuf)
		       >= 0)
		{
			register char *p;

			lineno++;

			/*
			**  Parse the line.
			*/

			p = strchr(ibuf, '\n');
			if (p != NULL)
				*p = '\0';
			else if (!sm_io_eof(smioin))
			{
				(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
						     "%s: %s: line %u: line too long (%ld bytes max)\n",
						     progname, mapname, lineno,
						     (long) sizeof ibuf);
				exitstat = EX_DATAERR;
				continue;
			}

			if (ibuf[0] == '\0' || ibuf[0] == comment)
				continue;
			if (sep == '\0' && isascii(ibuf[0]) && isspace(ibuf[0]))
			{
				(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
						     "%s: %s: line %u: syntax error (leading space)\n",
						     progname, mapname, lineno);
				exitstat = EX_DATAERR;
				continue;
			}

			memset(&db_key, '\0', sizeof db_key);
			memset(&db_val, '\0', sizeof db_val);
			db_key.data = ibuf;

			for (p = ibuf; *p != '\0' && !(ISSEP(*p)); p++)
			{
				if (foldcase && isascii(*p) && isupper(*p))
					*p = tolower(*p);
			}
			db_key.size = p - ibuf;
			if (inclnull)
				db_key.size++;

			if (*p != '\0')
				*p++ = '\0';
			while (*p != '\0' && ISSEP(*p))
				p++;
			if (!allowempty && *p == '\0')
			{
				(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
						     "%s: %s: line %u: no RHS for LHS %s\n",
						     progname, mapname, lineno,
						     (char *) db_key.data);
				exitstat = EX_DATAERR;
				continue;
			}

			db_val.data = p;
			db_val.size = strlen(p);
			if (inclnull)
				db_val.size++;

			/*
			**  Do the database insert.
			*/

			if (verbose)
			{
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "key=`%s', val=`%s'\n",
						     (char *) db_key.data,
						     (char *) db_val.data);
			}

			errno = database->smdb_put(database, &db_key, &db_val,
						   putflags);
			switch (errno)
			{
			  case SMDBE_KEY_EXIST:
				st = 1;
				break;

			  case 0:
				st = 0;
				break;

			  default:
				st = -1;
				break;
			}

			if (st < 0)
			{
				(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
						     "%s: %s: line %u: key %s: put error: %s\n",
						     progname, mapname, lineno,
						     (char *) db_key.data,
						     sm_errstring(errno));
				exitstat = EX_IOERR;
			}
			else if (st > 0)
			{
				(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
						     "%s: %s: line %u: key %s: duplicate key\n",
						     progname, mapname,
						     lineno,
						     (char *) db_key.data);
				exitstat = EX_DATAERR;
			}
		}
	}

	/*
	**  Now close the database.
	*/

	errno = database->smdb_close(database);
	if (errno != SMDBE_OK)
	{
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "%s: close(%s): %s\n",
				     progname, mapname, sm_errstring(errno));
		exitstat = EX_IOERR;
	}
	smdb_free_database(database);

	exit(exitstat);

	/* NOTREACHED */
	return exitstat;
}
