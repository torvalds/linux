/*
 * Copyright (c) 1998-2002, 2004 Proofpoint, Inc. and its suppliers.
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
#ifndef lint
SM_UNUSED(static char copyright[]) =
"@(#) Copyright (c) 1998-2001 Proofpoint, Inc. and its suppliers.\n\
	All rights reserved.\n\
     Copyright (c) 1992 Eric P. Allman.  All rights reserved.\n\
     Copyright (c) 1992, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* ! lint */

#ifndef lint
SM_UNUSED(static char id[]) = "@(#)$Id: editmap.c,v 1.26 2013-11-22 20:51:26 ca Exp $";
#endif /* ! lint */


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
#include <assert.h>
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
#define ISSEP(c) (isascii(c) && isspace(c))


static void usage __P((char *));

static void
usage(progname)
	char *progname;
{
	fprintf(stderr,
		"Usage: %s [-C cffile] [-N] [-f] [-q|-u|-x] maptype mapname key [ \"value ...\" ]\n",
		progname);
	exit(EX_USAGE);
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	char *progname;
	char *cfile;
	bool query = false;
	bool update = false;
	bool remove = false;
	bool inclnull = false;
	bool foldcase = true;
	unsigned int nops = 0;
	int exitstat;
	int opt;
	char *typename = NULL;
	char *mapname = NULL;
	char *keyname = NULL;
	char *value = NULL;
	int mode;
	int smode;
	int putflags = 0;
	long sff = SFF_ROOTOK|SFF_REGONLY;
	struct passwd *pw;
	SMDB_DATABASE *database;
	SMDB_DBENT db_key, db_val;
	SMDB_DBPARAMS params;
	SMDB_USER_INFO user_info;
#if HASFCHOWN
	FILE *cfp;
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
	cfile = _PATH_SENDMAILCF;

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

#define OPTIONS		"C:fquxN"
	while ((opt = getopt(argc, argv, OPTIONS)) != -1)
	{
		switch (opt)
		{
		  case 'C':
			cfile = optarg;
			break;

		  case 'f':
			foldcase = false;
			break;

		  case 'q':
			query = true;
			nops++;
			break;

		  case 'u':
			update = true;
			nops++;
			break;

		  case 'x':
			remove = true;
			nops++;
			break;

		  case 'N':
			inclnull = true;
			break;

		  default:
			usage(progname);
			assert(0);  /* NOTREACHED */
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
	if ((nops != 1) ||
	    (query && argc != 3) ||
	    (remove && argc != 3) ||
	    (update && argc <= 3))
	{
		usage(progname);
		assert(0);  /* NOTREACHED */
	}

	typename = argv[0];
	mapname = argv[1];
	keyname = argv[2];
	if (update)
		value = argv[3];

	if (foldcase)
	{
		char *p;

		for (p = keyname; *p != '\0'; p++)
		{
			if (isascii(*p) && isupper(*p))
				*p = tolower(*p);
		}
	}


#if HASFCHOWN
	/* Find TrustedUser value in sendmail.cf */
	if ((cfp = fopen(cfile, "r")) == NULL)
	{
		fprintf(stderr, "%s: %s: %s\n", progname,
			cfile, sm_errstring(errno));
		exit(EX_NOINPUT);
	}
	while (fgets(buf, sizeof(buf), cfp) != NULL)
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
						fprintf(stderr,
							"TrustedUser: unknown user %s\n", b);
					else
						TrustedUid = pw->pw_uid;
				}

# ifdef UID_MAX
				if (TrustedUid > UID_MAX)
				{
					fprintf(stderr,
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
	(void) fclose(cfp);
#endif /* HASFCHOWN */

	if (query)
	{
		mode = O_RDONLY;
		smode = S_IRUSR;
	}
	else
	{
		mode = O_RDWR | O_CREAT;
		sff |= SFF_CREAT|SFF_NOTEXCL;
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
			fprintf(stderr,
				"%s: Need to recompile with -D%s for %s support\n",
				progname, hint, typename);
		else
			fprintf(stderr,
				"%s: error opening type %s map %s: %s\n",
				progname, typename, mapname,
				sm_errstring(errno));
		exit(EX_CANTCREAT);
	}

	(void) database->smdb_sync(database, 0);

	if (geteuid() == 0 && TrustedUid != 0)
	{
		errno = database->smdb_set_owner(database, TrustedUid, -1);
		if (errno != SMDBE_OK)
		{
			fprintf(stderr,
				"WARNING: ownership change on %s failed %s",
				mapname, sm_errstring(errno));
		}
	}

	exitstat = EX_OK;
	if (query)
	{
		memset(&db_key, '\0', sizeof db_key);
		memset(&db_val, '\0', sizeof db_val);

		db_key.data = keyname;
		db_key.size = strlen(keyname);
		if (inclnull)
			db_key.size++;

		errno = database->smdb_get(database, &db_key, &db_val, 0);
		if (errno != SMDBE_OK)
		{
			/* XXX - Need to distinguish between not found */
			fprintf(stderr,
				"%s: couldn't find key %s in map %s\n",
				progname, keyname, mapname);
			exitstat = EX_UNAVAILABLE;
		}
		else
		{
			printf("%.*s\n", (int) db_val.size,
			       (char *) db_val.data);
		}
	}
	else if (update)
	{
		memset(&db_key, '\0', sizeof db_key);
		memset(&db_val, '\0', sizeof db_val);

		db_key.data = keyname;
		db_key.size = strlen(keyname);
		if (inclnull)
			db_key.size++;
		db_val.data = value;
		db_val.size = strlen(value);
		if (inclnull)
			db_val.size++;

		errno = database->smdb_put(database, &db_key, &db_val,
					   putflags);
		if (errno != SMDBE_OK)
		{
			fprintf(stderr,
				"%s: error updating (%s, %s) in map %s: %s\n",
				progname, keyname, value, mapname,
				sm_errstring(errno));
			exitstat = EX_IOERR;
		}
	}
	else if (remove)
	{
		memset(&db_key, '\0', sizeof db_key);
		memset(&db_val, '\0', sizeof db_val);

		db_key.data = keyname;
		db_key.size = strlen(keyname);
		if (inclnull)
			db_key.size++;

		errno = database->smdb_del(database, &db_key, 0);

		switch (errno)
		{
		case SMDBE_NOT_FOUND:
			fprintf(stderr,
				"%s: key %s doesn't exist in map %s\n",
				progname, keyname, mapname);
			/* Don't set exitstat */
			break;
		case SMDBE_OK:
			/* All's well */
			break;
		default:
			fprintf(stderr,
				"%s: couldn't remove key %s in map %s (error)\n",
				progname, keyname, mapname);
			exitstat = EX_IOERR;
			break;
		}
	}
	else
	{
		assert(0);  /* NOT REACHED */
	}

	/*
	**  Now close the database.
	*/

	errno = database->smdb_close(database);
	if (errno != SMDBE_OK)
	{
		fprintf(stderr, "%s: close(%s): %s\n",
			progname, mapname, sm_errstring(errno));
		exitstat = EX_IOERR;
	}
	smdb_free_database(database);

	exit(exitstat);
	/* NOTREACHED */
	return exitstat;
}
