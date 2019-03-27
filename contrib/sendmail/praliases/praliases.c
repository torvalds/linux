/*
 * Copyright (c) 1998-2001, 2008 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1983 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>

SM_IDSTR(copyright,
"@(#) Copyright (c) 1998-2001 Proofpoint, Inc. and its suppliers.\n\
	All rights reserved.\n\
     Copyright (c) 1983 Eric P. Allman.  All rights reserved.\n\
     Copyright (c) 1988, 1993\n\
	The Regents of the University of California.  All rights reserved.\n")

SM_IDSTR(id, "@(#)$Id: praliases.c,v 8.98 2013-11-22 20:51:53 ca Exp $")

#include <sys/types.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef EX_OK
# undef EX_OK		/* unistd.h may have another use for this */
#endif /* EX_OK */
#include <sysexits.h>


#ifndef NOT_SENDMAIL
# define NOT_SENDMAIL
#endif /* ! NOT_SENDMAIL */
#include <sendmail/sendmail.h>
#include <sendmail/pathnames.h>
#include <libsmdb/smdb.h>

static void praliases __P((char *, int, char **));

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

# define DELIMITERS		" ,/"
# define PATH_SEPARATOR		':'

int
main(argc, argv)
	int argc;
	char **argv;
{
	char *cfile;
	char *filename = NULL;
	SM_FILE_T *cfp;
	int ch;
	char afilebuf[MAXLINE];
	char buf[MAXLINE];
	struct passwd *pw;
	static char rnamebuf[MAXNAME];
	extern char *optarg;
	extern int optind;

	clrbitmap(DontBlameSendmail);
	RunAsUid = RealUid = getuid();
	RunAsGid = RealGid = getgid();
	pw = getpwuid(RealUid);
	if (pw != NULL)
	{
		if (strlen(pw->pw_name) > MAXNAME - 1)
			pw->pw_name[MAXNAME] = 0;
		sm_snprintf(rnamebuf, sizeof rnamebuf, "%s", pw->pw_name);
	}
	else
		(void) sm_snprintf(rnamebuf, sizeof rnamebuf,
		    "Unknown UID %d", (int) RealUid);
	RunAsUserName = RealUserName = rnamebuf;

	cfile = getcfname(0, 0, SM_GET_SENDMAIL_CF, NULL);
	while ((ch = getopt(argc, argv, "C:f:")) != -1)
	{
		switch ((char)ch) {
		case 'C':
			cfile = optarg;
			break;
		case 'f':
			filename = optarg;
			break;
		case '?':
		default:
			(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
			    "usage: praliases [-C cffile] [-f aliasfile]"
			    " [key ...]\n");
			exit(EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (filename != NULL)
	{
		praliases(filename, argc, argv);
		exit(EX_OK);
	}

	if ((cfp = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, cfile, SM_IO_RDONLY,
			      NULL)) == NULL)
	{
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "praliases: %s: %s\n", cfile,
				     sm_errstring(errno));
		exit(EX_NOINPUT);
	}

	while (sm_io_fgets(cfp, SM_TIME_DEFAULT, buf, sizeof(buf)) >= 0)
	{
		register char *b, *p;

		b = strchr(buf, '\n');
		if (b != NULL)
			*b = '\0';

		b = buf;
		switch (*b++)
		{
		  case 'O':		/* option -- see if alias file */
			if (sm_strncasecmp(b, " AliasFile", 10) == 0 &&
			    !(isascii(b[10]) && isalnum(b[10])))
			{
				/* new form -- find value */
				b = strchr(b, '=');
				if (b == NULL)
					continue;
				while (isascii(*++b) && isspace(*b))
					continue;
			}
			else if (*b++ != 'A')
			{
				/* something else boring */
				continue;
			}

			/* this is the A or AliasFile option -- save it */
			if (sm_strlcpy(afilebuf, b, sizeof afilebuf) >=
			    sizeof afilebuf)
			{
				(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				    "praliases: AliasFile filename too long: %.30s\n",
					b);
				(void) sm_io_close(cfp, SM_TIME_DEFAULT);
				exit(EX_CONFIG);
			}
			b = afilebuf;

			for (p = b; p != NULL; )
			{
				while (isascii(*p) && isspace(*p))
					p++;
				if (*p == '\0')
					break;
				b = p;

				p = strpbrk(p, DELIMITERS);

				/* find end of spec */
				if (p != NULL)
				{
					bool quoted = false;

					for (; *p != '\0'; p++)
					{
						/*
						**  Don't break into a quoted
						**  string.
						*/

						if (*p == '"')
							quoted = !quoted;
						else if (*p == ',' && !quoted)
							break;
					}

					/* No more alias specs follow */
					if (*p == '\0')
					{
						/* chop trailing whitespace */
						while (isascii(*p) &&
						       isspace(*p) &&
						       p > b)
							p--;
						*p = '\0';
						p = NULL;
					}
				}

				if (p != NULL)
				{
					char *e = p - 1;

					/* chop trailing whitespace */
					while (isascii(*e) &&
					       isspace(*e) &&
					       e > b)
						e--;
					*++e = '\0';
					*p++ = '\0';
				}
				praliases(b, argc, argv);
			}

		  default:
			continue;
		}
	}
	(void) sm_io_close(cfp, SM_TIME_DEFAULT);
	exit(EX_OK);
	/* NOTREACHED */
	return EX_OK;
}

static void
praliases(filename, argc, argv)
	char *filename;
	int argc;
	char **argv;
{
	int result;
	char *colon;
	char *db_name;
	char *db_type;
	SMDB_DATABASE *database = NULL;
	SMDB_CURSOR *cursor = NULL;
	SMDB_DBENT db_key, db_value;
	SMDB_DBPARAMS params;
	SMDB_USER_INFO user_info;

	colon = strchr(filename, PATH_SEPARATOR);
	if (colon == NULL)
	{
		db_name = filename;
		db_type = SMDB_TYPE_DEFAULT;
	}
	else
	{
		*colon = '\0';
		db_name = colon + 1;
		db_type = filename;
	}

	/* clean off arguments */
	for (;;)
	{
		while (isascii(*db_name) && isspace(*db_name))
			db_name++;

		if (*db_name != '-')
			break;
		while (*db_name != '\0' &&
		       !(isascii(*db_name) && isspace(*db_name)))
			db_name++;
	}

	/* Skip non-file based DB types */
	if (db_type != NULL && *db_type != '\0')
	{
		if (db_type != SMDB_TYPE_DEFAULT &&
		    strcmp(db_type, "hash") != 0 &&
		    strcmp(db_type, "btree") != 0 &&
		    strcmp(db_type, "dbm") != 0)
		{
			sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				      "praliases: Skipping non-file based alias type %s\n",
				db_type);
			return;
		}
	}

	if (*db_name == '\0' || (db_type != NULL && *db_type == '\0'))
	{
		if (colon != NULL)
			*colon = ':';
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
		    "praliases: illegal alias specification: %s\n", filename);
		goto fatal;
	}

	memset(&params, '\0', sizeof params);
	params.smdbp_cache_size = 1024 * 1024;

	user_info.smdbu_id = RunAsUid;
	user_info.smdbu_group_id = RunAsGid;
	(void) sm_strlcpy(user_info.smdbu_name, RunAsUserName,
			  SMDB_MAX_USER_NAME_LEN);

	result = smdb_open_database(&database, db_name, O_RDONLY, 0,
				    SFF_ROOTOK, db_type, &user_info, &params);
	if (result != SMDBE_OK)
	{
		sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
			      "praliases: %s: open: %s\n",
			      db_name, sm_errstring(result));
		goto fatal;
	}

	if (argc == 0)
	{
		memset(&db_key, '\0', sizeof db_key);
		memset(&db_value, '\0', sizeof db_value);

		result = database->smdb_cursor(database, &cursor, 0);
		if (result != SMDBE_OK)
		{
			(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
			    "praliases: %s: set cursor: %s\n", db_name,
			    sm_errstring(result));
			goto fatal;
		}

		while ((result = cursor->smdbc_get(cursor, &db_key, &db_value,
						   SMDB_CURSOR_GET_NEXT)) ==
						   SMDBE_OK)
		{
#if 0
			/* skip magic @:@ entry */
			if (db_key.size == 2 &&
			    db_key.data[0] == '@' &&
			    db_key.data[1] == '\0' &&
			    db_value.size == 2 &&
			    db_value.data[0] == '@' &&
			    db_value.data[1] == '\0')
				continue;
#endif /* 0 */

			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "%.*s:%.*s\n",
					     (int) db_key.size,
					     (char *) db_key.data,
					     (int) db_value.size,
					     (char *) db_value.data);
		}

		if (result != SMDBE_OK && result != SMDBE_LAST_ENTRY)
		{
			(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				"praliases: %s: get value at cursor: %s\n",
				db_name, sm_errstring(result));
			goto fatal;
		}
	}
	else for (; *argv != NULL; ++argv)
	{
		int get_res;

		memset(&db_key, '\0', sizeof db_key);
		memset(&db_value, '\0', sizeof db_value);
		db_key.data = *argv;
		db_key.size = strlen(*argv);
		get_res = database->smdb_get(database, &db_key, &db_value, 0);
		if (get_res == SMDBE_NOT_FOUND)
		{
			db_key.size++;
			get_res = database->smdb_get(database, &db_key,
						     &db_value, 0);
		}
		if (get_res == SMDBE_OK)
		{
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "%.*s:%.*s\n",
					     (int) db_key.size,
					     (char *) db_key.data,
					     (int) db_value.size,
					     (char *) db_value.data);
		}
		else
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "%s: No such key\n",
					     (char *)db_key.data);
	}

 fatal:
	if (cursor != NULL)
		(void) cursor->smdbc_close(cursor);
	if (database != NULL)
		(void) database->smdb_close(database);
	if (colon != NULL)
		*colon = ':';
	return;
}
