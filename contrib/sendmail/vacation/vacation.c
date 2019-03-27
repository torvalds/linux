/*
 * Copyright (c) 1999-2002, 2009 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1983, 1987, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1983 Eric P. Allman.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>

SM_IDSTR(copyright,
"@(#) Copyright (c) 1999-2002, 2009 Proofpoint, Inc. and its suppliers.\n\
	All rights reserved.\n\
     Copyright (c) 1983, 1987, 1993\n\
	The Regents of the University of California.  All rights reserved.\n\
     Copyright (c) 1983 Eric P. Allman.  All rights reserved.\n")

SM_IDSTR(id, "@(#)$Id: vacation.c,v 8.148 2013-11-22 20:52:02 ca Exp $")


#include <ctype.h>
#include <stdlib.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#ifdef EX_OK
# undef EX_OK		/* unistd.h may have another use for this */
#endif /* EX_OK */
#include <sm/sysexits.h>

#include <sm/cf.h>
#include <sm/mbdb.h>
#include "sendmail/sendmail.h"
#include <sendmail/pathnames.h>
#include "libsmdb/smdb.h"

#define ONLY_ONCE	((time_t) 0)	/* send at most one reply */
#define INTERVAL_UNDEF	((time_t) (-1))	/* no value given */

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

static int readheaders __P((bool));
static bool junkmail __P((char *));
static bool nsearch __P((char *, char *));
static void usage __P((void));
static void setinterval __P((time_t));
static bool recent __P((void));
static void setreply __P((char *, time_t));
static void sendmessage __P((char *, char *, char *));
static void xclude __P((SM_FILE_T *));

/*
**  VACATION -- return a message to the sender when on vacation.
**
**	This program is invoked as a message receiver.  It returns a
**	message specified by the user to whomever sent the mail, taking
**	care not to return a message too often to prevent "I am on
**	vacation" loops.
*/

#define	VDB	".vacation"		/* vacation database */
#define	VMSG	".vacation.msg"		/* vacation message */
#define SECSPERDAY	(60 * 60 * 24)
#define DAYSPERWEEK	7

typedef struct alias
{
	char *name;
	struct alias *next;
} ALIAS;

ALIAS *Names = NULL;

SMDB_DATABASE *Db;

char From[MAXLINE];
bool CloseMBDB = false;

#if defined(__hpux) || defined(__osf__)
# ifndef SM_CONF_SYSLOG_INT
#  define SM_CONF_SYSLOG_INT	1
# endif /* SM_CONF_SYSLOG_INT */
#endif /* defined(__hpux) || defined(__osf__) */

#if SM_CONF_SYSLOG_INT
# define SYSLOG_RET_T	int
# define SYSLOG_RET	return 0
#else /* SM_CONF_SYSLOG_INT */
# define SYSLOG_RET_T	void
# define SYSLOG_RET
#endif /* SM_CONF_SYSLOG_INT */

typedef SYSLOG_RET_T SYSLOG_T __P((int, const char *, ...));
SYSLOG_T *msglog = syslog;
static SYSLOG_RET_T debuglog __P((int, const char *, ...));
static void eatmsg __P((void));
static void listdb __P((void));

/* exit after reading input */
#define EXITIT(excode)			\
{					\
	eatmsg();			\
	if (CloseMBDB)			\
	{				\
		sm_mbdb_terminate();	\
		CloseMBDB = false;	\
	}				\
	return excode;			\
}

#define EXITM(excode)			\
{					\
	if (!initdb && !list)		\
		eatmsg();		\
	if (CloseMBDB)			\
	{				\
		sm_mbdb_terminate();	\
		CloseMBDB = false;	\
	}				\
	exit(excode);			\
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	bool alwaysrespond = false;
	bool initdb, exclude;
	bool runasuser = false;
	bool list = false;
	int mfail = 0, ufail = 0;
	int ch;
	int result;
	long sff;
	time_t interval;
	struct passwd *pw;
	ALIAS *cur;
	char *dbfilename = NULL;
	char *msgfilename = NULL;
	char *cfpath = NULL;
	char *name = NULL;
	char *returnaddr = NULL;
	SMDB_USER_INFO user_info;
	static char rnamebuf[MAXNAME];
	extern int optind, opterr;
	extern char *optarg;

	/* Vars needed to link with smutil */
	clrbitmap(DontBlameSendmail);
	RunAsUid = RealUid = getuid();
	RunAsGid = RealGid = getgid();
	pw = getpwuid(RealUid);
	if (pw != NULL)
	{
		if (strlen(pw->pw_name) > MAXNAME - 1)
			pw->pw_name[MAXNAME] = '\0';
		sm_snprintf(rnamebuf, sizeof rnamebuf, "%s", pw->pw_name);
	}
	else
		sm_snprintf(rnamebuf, sizeof rnamebuf,
			    "Unknown UID %d", (int) RealUid);
	RunAsUserName = RealUserName = rnamebuf;

# ifdef LOG_MAIL
	openlog("vacation", LOG_PID, LOG_MAIL);
# else /* LOG_MAIL */
	openlog("vacation", LOG_PID);
# endif /* LOG_MAIL */

	opterr = 0;
	initdb = false;
	exclude = false;
	interval = INTERVAL_UNDEF;
	*From = '\0';


#define OPTIONS	"a:C:df:Iijlm:R:r:s:t:Uxz"

	while (mfail == 0 && ufail == 0 &&
	       (ch = getopt(argc, argv, OPTIONS)) != -1)
	{
		switch((char)ch)
		{
		  case 'a':			/* alias */
			cur = (ALIAS *) malloc((unsigned int) sizeof(ALIAS));
			if (cur == NULL)
			{
				mfail++;
				break;
			}
			cur->name = optarg;
			cur->next = Names;
			Names = cur;
			break;

		  case 'C':
			cfpath = optarg;
			break;

		  case 'd':			/* debug mode */
			msglog = debuglog;
			break;

		  case 'f':		/* alternate database */
			dbfilename = optarg;
			break;

		  case 'I':			/* backward compatible */
		  case 'i':			/* init the database */
			initdb = true;
			break;

		  case 'j':
			alwaysrespond = true;
			break;

		  case 'l':
			list = true;		/* list the database */
			break;

		  case 'm':		/* alternate message file */
			msgfilename = optarg;
			break;

		  case 'R':
			returnaddr = optarg;
			break;

		  case 'r':
			if (isascii(*optarg) && isdigit(*optarg))
			{
				interval = atol(optarg) * SECSPERDAY;
				if (interval < 0)
					ufail++;
			}
			else
				interval = ONLY_ONCE;
			break;

		  case 's':		/* alternate sender name */
			(void) sm_strlcpy(From, optarg, sizeof From);
			break;

		  case 't':		/* SunOS: -t1d (default expire) */
			break;

		  case 'U':		/* run as single user mode */
			runasuser = true;
			break;

		  case 'x':
			exclude = true;
			break;

		  case 'z':
			returnaddr = "<>";
			break;

		  case '?':
		  default:
			ufail++;
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (mfail != 0)
	{
		msglog(LOG_NOTICE,
		       "vacation: can't allocate memory for alias.\n");
		EXITM(EX_TEMPFAIL);
	}
	if (ufail != 0)
		usage();

	if (argc != 1)
	{
		if (!initdb && !list && !exclude)
			usage();
		if ((pw = getpwuid(getuid())) == NULL)
		{
			msglog(LOG_ERR,
			       "vacation: no such user uid %u.\n", getuid());
			EXITM(EX_NOUSER);
		}
		name = strdup(pw->pw_name);
		user_info.smdbu_id = pw->pw_uid;
		user_info.smdbu_group_id = pw->pw_gid;
		(void) sm_strlcpy(user_info.smdbu_name, pw->pw_name,
				  SMDB_MAX_USER_NAME_LEN);
		if (chdir(pw->pw_dir) != 0)
		{
			msglog(LOG_NOTICE,
			       "vacation: no such directory %s.\n",
			       pw->pw_dir);
			EXITM(EX_NOINPUT);
		}
	}
	else if (runasuser)
	{
		name = strdup(*argv);
		if (dbfilename == NULL || msgfilename == NULL)
		{
			msglog(LOG_NOTICE,
			       "vacation: -U requires setting both -f and -m\n");
			EXITM(EX_NOINPUT);
		}
		user_info.smdbu_id = pw->pw_uid;
		user_info.smdbu_group_id = pw->pw_gid;
		(void) sm_strlcpy(user_info.smdbu_name, pw->pw_name,
			       SMDB_MAX_USER_NAME_LEN);
	}
	else
	{
		int err;
		SM_CF_OPT_T mbdbname;
		SM_MBDB_T user;

		cfpath = getcfname(0, 0, SM_GET_SENDMAIL_CF, cfpath);
		mbdbname.opt_name = "MailboxDatabase";
		mbdbname.opt_val = "pw";
		(void) sm_cf_getopt(cfpath, 1, &mbdbname);
		err = sm_mbdb_initialize(mbdbname.opt_val);
		if (err != EX_OK)
		{
			msglog(LOG_ERR,
			       "vacation: can't open mailbox database: %s.\n",
			       sm_strexit(err));
			EXITM(err);
		}
		CloseMBDB = true;
		err = sm_mbdb_lookup(*argv, &user);
		if (err == EX_NOUSER)
		{
			msglog(LOG_ERR, "vacation: no such user %s.\n", *argv);
			EXITM(EX_NOUSER);
		}
		if (err != EX_OK)
		{
			msglog(LOG_ERR,
			       "vacation: can't read mailbox database: %s.\n",
			       sm_strexit(err));
			EXITM(err);
		}
		name = strdup(user.mbdb_name);
		if (chdir(user.mbdb_homedir) != 0)
		{
			msglog(LOG_NOTICE,
			       "vacation: no such directory %s.\n",
			       user.mbdb_homedir);
			EXITM(EX_NOINPUT);
		}
		user_info.smdbu_id = user.mbdb_uid;
		user_info.smdbu_group_id = user.mbdb_gid;
		(void) sm_strlcpy(user_info.smdbu_name, user.mbdb_name,
			       SMDB_MAX_USER_NAME_LEN);
	}
	if (name == NULL)
	{
		msglog(LOG_ERR,
		       "vacation: can't allocate memory for username.\n");
		EXITM(EX_OSERR);
	}

	if (dbfilename == NULL)
		dbfilename = VDB;
	if (msgfilename == NULL)
		msgfilename = VMSG;

	sff = SFF_CREAT;
	if (getegid() != getgid())
	{
		/* Allow a set-group-ID vacation binary */
		RunAsGid = user_info.smdbu_group_id = getegid();
		sff |= SFF_OPENASROOT;
	}
	if (getuid() == 0)
	{
		/* Allow root to initialize user's vacation databases */
		sff |= SFF_OPENASROOT|SFF_ROOTOK;

		/* ... safely */
		sff |= SFF_NOSLINK|SFF_NOHLINK|SFF_REGONLY;
	}


	result = smdb_open_database(&Db, dbfilename,
				    O_CREAT|O_RDWR | (initdb ? O_TRUNC : 0),
				    S_IRUSR|S_IWUSR, sff,
				    SMDB_TYPE_DEFAULT, &user_info, NULL);
	if (result != SMDBE_OK)
	{
		msglog(LOG_NOTICE, "vacation: %s: %s\n", dbfilename,
		       sm_errstring(result));
		EXITM(EX_DATAERR);
	}

	if (list)
	{
		listdb();
		(void) Db->smdb_close(Db);
		exit(EX_OK);
	}

	if (interval != INTERVAL_UNDEF)
		setinterval(interval);

	if (initdb && !exclude)
	{
		(void) Db->smdb_close(Db);
		exit(EX_OK);
	}

	if (exclude)
	{
		xclude(smioin);
		(void) Db->smdb_close(Db);
		EXITM(EX_OK);
	}

	if ((cur = (ALIAS *) malloc((unsigned int) sizeof(ALIAS))) == NULL)
	{
		msglog(LOG_NOTICE,
		       "vacation: can't allocate memory for username.\n");
		(void) Db->smdb_close(Db);
		EXITM(EX_OSERR);
	}
	cur->name = name;
	cur->next = Names;
	Names = cur;

	result = readheaders(alwaysrespond);
	if (result == EX_OK && !recent())
	{
		time_t now;

		(void) time(&now);
		setreply(From, now);
		(void) Db->smdb_close(Db);
		sendmessage(name, msgfilename, returnaddr);
	}
	else
		(void) Db->smdb_close(Db);
	if (result == EX_NOUSER)
		result = EX_OK;
	exit(result);
}

/*
** EATMSG -- read stdin till EOF
**
**	Parameters:
**		none.
**
**	Returns:
**		nothing.
**
*/

static void
eatmsg()
{
	/*
	**  read the rest of the e-mail and ignore it to avoid problems
	**  with EPIPE in sendmail
	*/
	while (getc(stdin) != EOF)
		continue;
}

/*
** READHEADERS -- read mail headers
**
**	Parameters:
**		alwaysrespond -- respond regardless of whether msg is to me
**
**	Returns:
**		a exit code: NOUSER if no reply, OK if reply, * if error
**
**	Side Effects:
**		may exit().
**
*/

static int
readheaders(alwaysrespond)
	bool alwaysrespond;
{
	bool tome, cont;
	register char *p;
	register ALIAS *cur;
	char buf[MAXLINE];

	cont = false;
	tome = alwaysrespond;
	while (sm_io_fgets(smioin, SM_TIME_DEFAULT, buf, sizeof(buf)) >= 0 &&
	       *buf != '\n')
	{
		switch(*buf)
		{
		  case 'F':		/* "From " */
			cont = false;
			if (strncmp(buf, "From ", 5) == 0)
			{
				bool quoted = false;

				p = buf + 5;
				while (*p != '\0')
				{
					/* escaped character */
					if (*p == '\\')
					{
						p++;
						if (*p == '\0')
						{
							msglog(LOG_NOTICE,
							       "vacation: badly formatted \"From \" line.\n");
							EXITIT(EX_DATAERR);
						}
					}
					else if (*p == '"')
						quoted = !quoted;
					else if (*p == '\r' || *p == '\n')
						break;
					else if (*p == ' ' && !quoted)
						break;
					p++;
				}
				if (quoted)
				{
					msglog(LOG_NOTICE,
					       "vacation: badly formatted \"From \" line.\n");
					EXITIT(EX_DATAERR);
				}
				*p = '\0';

				/* ok since both strings have MAXLINE length */
				if (*From == '\0')
					(void) sm_strlcpy(From, buf + 5,
							  sizeof From);
				if ((p = strchr(buf + 5, '\n')) != NULL)
					*p = '\0';
				if (junkmail(buf + 5))
					EXITIT(EX_NOUSER);
			}
			break;

		  case 'P':		/* "Precedence:" */
		  case 'p':
			cont = false;
			if (strlen(buf) <= 10 ||
			    strncasecmp(buf, "Precedence", 10) != 0 ||
			    (buf[10] != ':' && buf[10] != ' ' &&
			     buf[10] != '\t'))
				break;
			if ((p = strchr(buf, ':')) == NULL)
				break;
			while (*++p != '\0' && isascii(*p) && isspace(*p));
			if (*p == '\0')
				break;
			if (strncasecmp(p, "junk", 4) == 0 ||
			    strncasecmp(p, "bulk", 4) == 0 ||
			    strncasecmp(p, "list", 4) == 0)
				EXITIT(EX_NOUSER);
			break;

		  case 'C':		/* "Cc:" */
		  case 'c':
			if (strncasecmp(buf, "Cc:", 3) != 0)
				break;
			cont = true;
			goto findme;

		  case 'T':		/* "To:" */
		  case 't':
			if (strncasecmp(buf, "To:", 3) != 0)
				break;
			cont = true;
			goto findme;

		  default:
			if (!isascii(*buf) || !isspace(*buf) || !cont || tome)
			{
				cont = false;
				break;
			}
findme:
			for (cur = Names;
			     !tome && cur != NULL;
			     cur = cur->next)
				tome = nsearch(cur->name, buf);
		}
	}
	if (!tome)
		EXITIT(EX_NOUSER);
	if (*From == '\0')
	{
		msglog(LOG_NOTICE, "vacation: no initial \"From \" line.\n");
		EXITIT(EX_DATAERR);
	}
	EXITIT(EX_OK);
}

/*
** NSEARCH --
**	do a nice, slow, search of a string for a substring.
**
**	Parameters:
**		name -- name to search.
**		str -- string in which to search.
**
**	Returns:
**		is name a substring of str?
**
*/

static bool
nsearch(name, str)
	register char *name, *str;
{
	register size_t len;
	register char *s;

	len = strlen(name);

	for (s = str; *s != '\0'; ++s)
	{
		/*
		**  Check to make sure that the string matches and
		**  the previous character is not an alphanumeric and
		**  the next character after the match is not an alphanumeric.
		**
		**  This prevents matching "eric" to "derick" while still
		**  matching "eric" to "<eric+detail>".
		*/

		if (tolower(*s) == tolower(*name) &&
		    strncasecmp(name, s, len) == 0 &&
		    (s == str || !isascii(*(s - 1)) || !isalnum(*(s - 1))) &&
		    (!isascii(*(s + len)) || !isalnum(*(s + len))))
			return true;
	}
	return false;
}

/*
** JUNKMAIL --
**	read the header and return if automagic/junk/bulk/list mail
**
**	Parameters:
**		from -- sender address.
**
**	Returns:
**		is this some automated/junk/bulk/list mail?
**
*/

struct ignore
{
	char	*name;
	size_t	len;
};

typedef struct ignore IGNORE_T;

#define MAX_USER_LEN 256	/* maximum length of local part (sender) */

/* delimiters for the local part of an address */
#define isdelim(c)	((c) == '%' || (c) == '@' || (c) == '+')

static bool
junkmail(from)
	char *from;
{
	bool quot;
	char *e;
	size_t len;
	IGNORE_T *cur;
	char sender[MAX_USER_LEN];
	static IGNORE_T ignore[] =
	{
		{ "postmaster",		10	},
		{ "uucp",		4	},
		{ "mailer-daemon",	13	},
		{ "mailer",		6	},
		{ NULL,			0	}
	};

	static IGNORE_T ignorepost[] =
	{
		{ "-request",		8	},
		{ "-relay",		6	},
		{ "-owner",		6	},
		{ NULL,			0	}
	};

	static IGNORE_T ignorepre[] =
	{
		{ "owner-",		6	},
		{ NULL,			0	}
	};

	/*
	**  This is mildly amusing, and I'm not positive it's right; trying
	**  to find the "real" name of the sender, assuming that addresses
	**  will be some variant of:
	**
	**  From site!site!SENDER%site.domain%site.domain@site.domain
	*/

	quot = false;
	e = from;
	len = 0;
	while (*e != '\0' && (quot || !isdelim(*e)))
	{
		if (*e == '"')
		{
			quot = !quot;
			++e;
			continue;
		}
		if (*e == '\\')
		{
			if (*(++e) == '\0')
			{
				/* '\\' at end of string? */
				break;
			}
			if (len < MAX_USER_LEN)
				sender[len++] = *e;
			++e;
			continue;
		}
		if (*e == '!' && !quot)
		{
			len = 0;
			sender[len] = '\0';
		}
		else
			if (len < MAX_USER_LEN)
				sender[len++] = *e;
		++e;
	}
	if (len < MAX_USER_LEN)
		sender[len] = '\0';
	else
		sender[MAX_USER_LEN - 1] = '\0';

	if (len <= 0)
		return false;
#if 0
	if (quot)
		return false;	/* syntax error... */
#endif /* 0 */

	/* test prefixes */
	for (cur = ignorepre; cur->name != NULL; ++cur)
	{
		if (len >= cur->len &&
		    strncasecmp(cur->name, sender, cur->len) == 0)
			return true;
	}

	/*
	**  If the name is truncated, don't test the rest.
	**	We could extract the "tail" of the sender address and
	**	compare it it ignorepost, however, it seems not worth
	**	the effort.
	**	The address surely can't match any entry in ignore[]
	**	(as long as all of them are shorter than MAX_USER_LEN).
	*/

	if (len > MAX_USER_LEN)
		return false;

	/* test full local parts */
	for (cur = ignore; cur->name != NULL; ++cur)
	{
		if (len == cur->len &&
		    strncasecmp(cur->name, sender, cur->len) == 0)
			return true;
	}

	/* test postfixes */
	for (cur = ignorepost; cur->name != NULL; ++cur)
	{
		if (len >= cur->len &&
		    strncasecmp(cur->name, e - cur->len - 1,
				cur->len) == 0)
			return true;
	}
	return false;
}

#define	VIT	"__VACATION__INTERVAL__TIMER__"

/*
** RECENT --
**	find out if user has gotten a vacation message recently.
**
**	Parameters:
**		none.
**
**	Returns:
**		true iff user has gotten a vacation message recently.
**
*/

static bool
recent()
{
	SMDB_DBENT key, data;
	time_t then, next;
	bool trydomain = false;
	int st;
	char *domain;

	memset(&key, '\0', sizeof key);
	memset(&data, '\0', sizeof data);

	/* get interval time */
	key.data = VIT;
	key.size = sizeof(VIT);

	st = Db->smdb_get(Db, &key, &data, 0);
	if (st != SMDBE_OK)
		next = SECSPERDAY * DAYSPERWEEK;
	else
		memmove(&next, data.data, sizeof(next));

	memset(&data, '\0', sizeof data);

	/* get record for this address */
	key.data = From;
	key.size = strlen(From);

	do
	{
		st = Db->smdb_get(Db, &key, &data, 0);
		if (st == SMDBE_OK)
		{
			memmove(&then, data.data, sizeof(then));
			if (next == ONLY_ONCE || then == ONLY_ONCE ||
			    then + next > time(NULL))
				return true;
		}
		if ((trydomain = !trydomain) &&
		    (domain = strchr(From, '@')) != NULL)
		{
			key.data = domain;
			key.size = strlen(domain);
		}
	} while (trydomain);
	return false;
}

/*
** SETINTERVAL --
**	store the reply interval
**
**	Parameters:
**		interval -- time interval for replies.
**
**	Returns:
**		nothing.
**
**	Side Effects:
**		stores the reply interval in database.
*/

static void
setinterval(interval)
	time_t interval;
{
	SMDB_DBENT key, data;

	memset(&key, '\0', sizeof key);
	memset(&data, '\0', sizeof data);

	key.data = VIT;
	key.size = sizeof(VIT);
	data.data = (char*) &interval;
	data.size = sizeof(interval);
	(void) (Db->smdb_put)(Db, &key, &data, 0);
}

/*
** SETREPLY --
**	store that this user knows about the vacation.
**
**	Parameters:
**		from -- sender address.
**		when -- last reply time.
**
**	Returns:
**		nothing.
**
**	Side Effects:
**		stores user/time in database.
*/

static void
setreply(from, when)
	char *from;
	time_t when;
{
	SMDB_DBENT key, data;

	memset(&key, '\0', sizeof key);
	memset(&data, '\0', sizeof data);

	key.data = from;
	key.size = strlen(from);
	data.data = (char*) &when;
	data.size = sizeof(when);
	(void) (Db->smdb_put)(Db, &key, &data, 0);
}

/*
** XCLUDE --
**	add users to vacation db so they don't get a reply.
**
**	Parameters:
**		f -- file pointer with list of address to exclude
**
**	Returns:
**		nothing.
**
**	Side Effects:
**		stores users in database.
*/

static void
xclude(f)
	SM_FILE_T *f;
{
	char buf[MAXLINE], *p;

	if (f == NULL)
		return;
	while (sm_io_fgets(f, SM_TIME_DEFAULT, buf, sizeof buf) >= 0)
	{
		if ((p = strchr(buf, '\n')) != NULL)
			*p = '\0';
		setreply(buf, ONLY_ONCE);
	}
}

/*
** SENDMESSAGE --
**	exec sendmail to send the vacation file to sender
**
**	Parameters:
**		myname -- user name.
**		msgfn -- name of file with vacation message.
**		sender -- use as sender address
**
**	Returns:
**		nothing.
**
**	Side Effects:
**		sends vacation reply.
*/

static void
sendmessage(myname, msgfn, sender)
	char *myname;
	char *msgfn;
	char *sender;
{
	SM_FILE_T *mfp, *sfp;
	int i;
	int pvect[2];
	char *pv[8];
	char buf[MAXLINE];

	mfp = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, msgfn, SM_IO_RDONLY, NULL);
	if (mfp == NULL)
	{
		if (msgfn[0] == '/')
			msglog(LOG_NOTICE, "vacation: no %s file.\n", msgfn);
		else
			msglog(LOG_NOTICE, "vacation: no ~%s/%s file.\n",
			       myname, msgfn);
		exit(EX_NOINPUT);
	}
	if (pipe(pvect) < 0)
	{
		msglog(LOG_ERR, "vacation: pipe: %s", sm_errstring(errno));
		exit(EX_OSERR);
	}
	pv[0] = "sendmail";
	pv[1] = "-oi";
	pv[2] = "-f";
	if (sender != NULL)
		pv[3] = sender;
	else
		pv[3] = myname;
	pv[4] = "--";
	pv[5] = From;
	pv[6] = NULL;
	i = fork();
	if (i < 0)
	{
		msglog(LOG_ERR, "vacation: fork: %s", sm_errstring(errno));
		exit(EX_OSERR);
	}
	if (i == 0)
	{
		(void) dup2(pvect[0], 0);
		(void) close(pvect[0]);
		(void) close(pvect[1]);
		(void) sm_io_close(mfp, SM_TIME_DEFAULT);
		(void) execv(_PATH_SENDMAIL, pv);
		msglog(LOG_ERR, "vacation: can't exec %s: %s",
			_PATH_SENDMAIL, sm_errstring(errno));
		exit(EX_UNAVAILABLE);
	}
	/* check return status of the following calls? XXX */
	(void) close(pvect[0]);
	if ((sfp = sm_io_open(SmFtStdiofd, SM_TIME_DEFAULT,
			      (void *) &(pvect[1]),
			      SM_IO_WRONLY, NULL)) != NULL)
	{
#if _FFR_VAC_WAIT4SM
# ifdef WAITUNION
		union wait st;
# else /* WAITUNION */
		auto int st;
# endif /* WAITUNION */
#endif /* _FFR_VAC_WAIT4SM */

		(void) sm_io_fprintf(sfp, SM_TIME_DEFAULT, "To: %s\n", From);
		(void) sm_io_fprintf(sfp, SM_TIME_DEFAULT,
				     "Auto-Submitted: auto-replied\n");
		while (sm_io_fgets(mfp, SM_TIME_DEFAULT, buf, sizeof buf) >= 0)
			(void) sm_io_fputs(sfp, SM_TIME_DEFAULT, buf);
		(void) sm_io_close(mfp, SM_TIME_DEFAULT);
		(void) sm_io_close(sfp, SM_TIME_DEFAULT);
#if _FFR_VAC_WAIT4SM
		(void) wait(&st);
#endif /* _FFR_VAC_WAIT4SM */
	}
	else
	{
		(void) sm_io_close(mfp, SM_TIME_DEFAULT);
		msglog(LOG_ERR, "vacation: can't open pipe to sendmail");
		exit(EX_UNAVAILABLE);
	}
}

static void
usage()
{
	msglog(LOG_NOTICE,
	       "uid %u: usage: vacation [-a alias] [-C cfpath] [-d] [-f db] [-i] [-j] [-l] [-m msg] [-R returnaddr] [-r interval] [-s sender] [-t time] [-U] [-x] [-z] login\n",
	       getuid());
	exit(EX_USAGE);
}

/*
** LISTDB -- list the contents of the vacation database
**
**	Parameters:
**		none.
**
**	Returns:
**		nothing.
*/

static void
listdb()
{
	int result;
	time_t t;
	SMDB_CURSOR *cursor = NULL;
	SMDB_DBENT db_key, db_value;

	memset(&db_key, '\0', sizeof db_key);
	memset(&db_value, '\0', sizeof db_value);

	result = Db->smdb_cursor(Db, &cursor, 0);
	if (result != SMDBE_OK)
	{
		sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
			      "vacation: set cursor: %s\n",
			      sm_errstring(result));
		return;
	}

	while ((result = cursor->smdbc_get(cursor, &db_key, &db_value,
					   SMDB_CURSOR_GET_NEXT)) == SMDBE_OK)
	{
		char *timestamp;

		/* skip magic VIT entry */
		if (db_key.size == strlen(VIT) + 1 &&
		    strncmp((char *)db_key.data, VIT,
			    (int)db_key.size - 1) == 0)
			continue;

		/* skip bogus values */
		if (db_value.size != sizeof t)
		{
			sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				      "vacation: %.*s invalid time stamp\n",
				      (int) db_key.size, (char *) db_key.data);
			continue;
		}

		memcpy(&t, db_value.data, sizeof t);

		if (db_key.size > 40)
			db_key.size = 40;

		if (t <= 0)
		{
			/* must be an exclude */
			timestamp = "(exclusion)\n";
		}
		else
		{
			timestamp = ctime(&t);
		}
		sm_io_fprintf(smioout, SM_TIME_DEFAULT, "%-40.*s %-10s",
			      (int) db_key.size, (char *) db_key.data,
			      timestamp);

		memset(&db_key, '\0', sizeof db_key);
		memset(&db_value, '\0', sizeof db_value);
	}

	if (result != SMDBE_OK && result != SMDBE_LAST_ENTRY)
	{
		sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
			      "vacation: get value at cursor: %s\n",
			      sm_errstring(result));
		if (cursor != NULL)
		{
			(void) cursor->smdbc_close(cursor);
			cursor = NULL;
		}
		return;
	}
	(void) cursor->smdbc_close(cursor);
	cursor = NULL;
}

/*
** DEBUGLOG -- write message to standard error
**
**	Append a message to the standard error for the convenience of
**	end-users debugging without access to the syslog messages.
**
**	Parameters:
**		i -- syslog log level
**		fmt -- string format
**
**	Returns:
**		nothing.
*/

/*VARARGS2*/
static SYSLOG_RET_T
#ifdef __STDC__
debuglog(int i, const char *fmt, ...)
#else /* __STDC__ */
debuglog(i, fmt, va_alist)
	int i;
	const char *fmt;
	va_dcl
#endif /* __STDC__ */

{
	SM_VA_LOCAL_DECL

	SM_VA_START(ap, fmt);
	sm_io_vfprintf(smioerr, SM_TIME_DEFAULT, fmt, ap);
	SM_VA_END(ap);
	SYSLOG_RET;
}
