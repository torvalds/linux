/*
 * Copyright (c) 1998-2013 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1983, 1995-1997 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sendmail.h>

SM_RCSID("@(#)$Id: conf.c,v 8.1192 2014-01-27 18:23:21 ca Exp $")

#include <sm/sendmail.h>
#include <sendmail/pathnames.h>
#if NEWDB
# include "sm/bdb.h"
#endif /* NEWDB */

#include <daemon.h>
#include "map.h"

#ifdef DEC
# if NETINET6
/* for the IPv6 device lookup */
#  define _SOCKADDR_LEN
#  include <macros.h>
# endif /* NETINET6 */
#endif /* DEC */

# include <sys/ioctl.h>
# include <sys/param.h>

#include <limits.h>
#if NETINET || NETINET6
# include <arpa/inet.h>
#endif /* NETINET || NETINET6 */
#if HASULIMIT && defined(HPUX11)
# include <ulimit.h>
#endif /* HASULIMIT && defined(HPUX11) */

static void	setupmaps __P((void));
static void	setupmailers __P((void));
static void	setupqueues __P((void));
static int	get_num_procs_online __P((void));
static int	add_hostnames __P((SOCKADDR *));

#if NETINET6 && NEEDSGETIPNODE
static struct hostent *sm_getipnodebyname __P((const char *, int, int, int *));
static struct hostent *sm_getipnodebyaddr __P((const void *, size_t, int, int *));
#else /* NETINET6 && NEEDSGETIPNODE */
#define sm_getipnodebyname getipnodebyname
#define sm_getipnodebyaddr getipnodebyaddr
#endif /* NETINET6 && NEEDSGETIPNODE */


/*
**  CONF.C -- Sendmail Configuration Tables.
**
**	Defines the configuration of this installation.
**
**	Configuration Variables:
**		HdrInfo -- a table describing well-known header fields.
**			Each entry has the field name and some flags,
**			which are described in sendmail.h.
**
**	Notes:
**		I have tried to put almost all the reasonable
**		configuration information into the configuration
**		file read at runtime.  My intent is that anything
**		here is a function of the version of UNIX you
**		are running, or is really static -- for example
**		the headers are a superset of widely used
**		protocols.  If you find yourself playing with
**		this file too much, you may be making a mistake!
*/


/*
**  Header info table
**	Final (null) entry contains the flags used for any other field.
**
**	Not all of these are actually handled specially by sendmail
**	at this time.  They are included as placeholders, to let
**	you know that "someday" I intend to have sendmail do
**	something with them.
*/

struct hdrinfo	HdrInfo[] =
{
		/* originator fields, most to least significant */
	{ "resent-sender",		H_FROM|H_RESENT,	NULL	},
	{ "resent-from",		H_FROM|H_RESENT,	NULL	},
	{ "resent-reply-to",		H_FROM|H_RESENT,	NULL	},
	{ "sender",			H_FROM,			NULL	},
	{ "from",			H_FROM,			NULL	},
	{ "reply-to",			H_FROM,			NULL	},
	{ "errors-to",			H_FROM|H_ERRORSTO,	NULL	},
	{ "full-name",			H_ACHECK,		NULL	},
	{ "return-receipt-to",		H_RECEIPTTO,		NULL	},
	{ "delivery-receipt-to",	H_RECEIPTTO,		NULL	},
	{ "disposition-notification-to",	H_FROM,		NULL	},

		/* destination fields */
	{ "to",				H_RCPT,			NULL	},
	{ "resent-to",			H_RCPT|H_RESENT,	NULL	},
	{ "cc",				H_RCPT,			NULL	},
	{ "resent-cc",			H_RCPT|H_RESENT,	NULL	},
	{ "bcc",			H_RCPT|H_BCC,		NULL	},
	{ "resent-bcc",			H_RCPT|H_BCC|H_RESENT,	NULL	},
	{ "apparently-to",		H_RCPT,			NULL	},

		/* message identification and control */
	{ "message-id",			0,			NULL	},
	{ "resent-message-id",		H_RESENT,		NULL	},
	{ "message",			H_EOH,			NULL	},
	{ "text",			H_EOH,			NULL	},

		/* date fields */
	{ "date",			0,			NULL	},
	{ "resent-date",		H_RESENT,		NULL	},

		/* trace fields */
	{ "received",			H_TRACE|H_FORCE,	NULL	},
	{ "x400-received",		H_TRACE|H_FORCE,	NULL	},
	{ "via",			H_TRACE|H_FORCE,	NULL	},
	{ "mail-from",			H_TRACE|H_FORCE,	NULL	},

		/* miscellaneous fields */
	{ "comments",			H_FORCE|H_ENCODABLE,	NULL	},
	{ "return-path",		H_FORCE|H_ACHECK|H_BINDLATE,	NULL	},
	{ "content-transfer-encoding",	H_CTE,			NULL	},
	{ "content-type",		H_CTYPE,		NULL	},
	{ "content-length",		H_ACHECK,		NULL	},
	{ "subject",			H_ENCODABLE,		NULL	},
	{ "x-authentication-warning",	H_FORCE,		NULL	},

	{ NULL,				0,			NULL	}
};



/*
**  Privacy values
*/

struct prival PrivacyValues[] =
{
	{ "public",		PRIV_PUBLIC		},
	{ "needmailhelo",	PRIV_NEEDMAILHELO	},
	{ "needexpnhelo",	PRIV_NEEDEXPNHELO	},
	{ "needvrfyhelo",	PRIV_NEEDVRFYHELO	},
	{ "noexpn",		PRIV_NOEXPN		},
	{ "novrfy",		PRIV_NOVRFY		},
	{ "restrictexpand",	PRIV_RESTRICTEXPAND	},
	{ "restrictmailq",	PRIV_RESTRICTMAILQ	},
	{ "restrictqrun",	PRIV_RESTRICTQRUN	},
	{ "noetrn",		PRIV_NOETRN		},
	{ "noverb",		PRIV_NOVERB		},
	{ "authwarnings",	PRIV_AUTHWARNINGS	},
	{ "noreceipts",		PRIV_NORECEIPTS		},
	{ "nobodyreturn",	PRIV_NOBODYRETN		},
	{ "goaway",		PRIV_GOAWAY		},
	{ "noactualrecipient",	PRIV_NOACTUALRECIPIENT	},
	{ NULL,			0			}
};

/*
**  DontBlameSendmail values
*/

struct dbsval DontBlameSendmailValues[] =
{
	{ "safe",			DBS_SAFE			},
	{ "assumesafechown",		DBS_ASSUMESAFECHOWN		},
	{ "groupwritabledirpathsafe",	DBS_GROUPWRITABLEDIRPATHSAFE	},
	{ "groupwritableforwardfilesafe",
					DBS_GROUPWRITABLEFORWARDFILESAFE },
	{ "groupwritableincludefilesafe",
					DBS_GROUPWRITABLEINCLUDEFILESAFE },
	{ "groupwritablealiasfile",	DBS_GROUPWRITABLEALIASFILE	},
	{ "worldwritablealiasfile",	DBS_WORLDWRITABLEALIASFILE	},
	{ "forwardfileinunsafedirpath",	DBS_FORWARDFILEINUNSAFEDIRPATH	},
	{ "includefileinunsafedirpath",	DBS_INCLUDEFILEINUNSAFEDIRPATH	},
	{ "mapinunsafedirpath",		DBS_MAPINUNSAFEDIRPATH	},
	{ "linkedaliasfileinwritabledir",
					DBS_LINKEDALIASFILEINWRITABLEDIR },
	{ "linkedclassfileinwritabledir",
					DBS_LINKEDCLASSFILEINWRITABLEDIR },
	{ "linkedforwardfileinwritabledir",
					DBS_LINKEDFORWARDFILEINWRITABLEDIR },
	{ "linkedincludefileinwritabledir",
					DBS_LINKEDINCLUDEFILEINWRITABLEDIR },
	{ "linkedmapinwritabledir",	DBS_LINKEDMAPINWRITABLEDIR	},
	{ "linkedserviceswitchfileinwritabledir",
					DBS_LINKEDSERVICESWITCHFILEINWRITABLEDIR },
	{ "filedeliverytohardlink",	DBS_FILEDELIVERYTOHARDLINK	},
	{ "filedeliverytosymlink",	DBS_FILEDELIVERYTOSYMLINK	},
	{ "writemaptohardlink",		DBS_WRITEMAPTOHARDLINK		},
	{ "writemaptosymlink",		DBS_WRITEMAPTOSYMLINK		},
	{ "writestatstohardlink",	DBS_WRITESTATSTOHARDLINK	},
	{ "writestatstosymlink",	DBS_WRITESTATSTOSYMLINK		},
	{ "forwardfileingroupwritabledirpath",
					DBS_FORWARDFILEINGROUPWRITABLEDIRPATH },
	{ "includefileingroupwritabledirpath",
					DBS_INCLUDEFILEINGROUPWRITABLEDIRPATH },
	{ "classfileinunsafedirpath",	DBS_CLASSFILEINUNSAFEDIRPATH	},
	{ "errorheaderinunsafedirpath",	DBS_ERRORHEADERINUNSAFEDIRPATH	},
	{ "helpfileinunsafedirpath",	DBS_HELPFILEINUNSAFEDIRPATH	},
	{ "forwardfileinunsafedirpathsafe",
					DBS_FORWARDFILEINUNSAFEDIRPATHSAFE },
	{ "includefileinunsafedirpathsafe",
					DBS_INCLUDEFILEINUNSAFEDIRPATHSAFE },
	{ "runprograminunsafedirpath",	DBS_RUNPROGRAMINUNSAFEDIRPATH	},
	{ "runwritableprogram",		DBS_RUNWRITABLEPROGRAM		},
	{ "nonrootsafeaddr",		DBS_NONROOTSAFEADDR		},
	{ "truststickybit",		DBS_TRUSTSTICKYBIT		},
	{ "dontwarnforwardfileinunsafedirpath",
					DBS_DONTWARNFORWARDFILEINUNSAFEDIRPATH },
	{ "insufficiententropy",	DBS_INSUFFICIENTENTROPY },
	{ "groupreadablesasldbfile",	DBS_GROUPREADABLESASLDBFILE	},
	{ "groupwritablesasldbfile",	DBS_GROUPWRITABLESASLDBFILE	},
	{ "groupwritableforwardfile",	DBS_GROUPWRITABLEFORWARDFILE	},
	{ "groupwritableincludefile",	DBS_GROUPWRITABLEINCLUDEFILE	},
	{ "worldwritableforwardfile",	DBS_WORLDWRITABLEFORWARDFILE	},
	{ "worldwritableincludefile",	DBS_WORLDWRITABLEINCLUDEFILE	},
	{ "groupreadablekeyfile",	DBS_GROUPREADABLEKEYFILE	},
	{ "groupreadabledefaultauthinfofile",
					DBS_GROUPREADABLEAUTHINFOFILE	},
	{ NULL,				0				}
};

/*
**  Miscellaneous stuff.
*/

int	DtableSize =	50;		/* max open files; reset in 4.2bsd */
/*
**  SETDEFAULTS -- set default values
**
**	Some of these must be initialized using direct code since they
**	depend on run-time values. So let's do all of them this way.
**
**	Parameters:
**		e -- the default envelope.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Initializes a bunch of global variables to their
**		default values.
*/

#define MINUTES		* 60
#define HOURS		* 60 MINUTES
#define DAYS		* 24 HOURS

#ifndef MAXRULERECURSION
# define MAXRULERECURSION	50	/* max ruleset recursion depth */
#endif /* ! MAXRULERECURSION */

void
setdefaults(e)
	register ENVELOPE *e;
{
	int i;
	int numprocs;
	struct passwd *pw;

	numprocs = get_num_procs_online();
	SpaceSub = ' ';				/* option B */
	QueueLA = 8 * numprocs;			/* option x */
	RefuseLA = 12 * numprocs;		/* option X */
	WkRecipFact = 30000L;			/* option y */
	WkClassFact = 1800L;			/* option z */
	WkTimeFact = 90000L;			/* option Z */
	QueueFactor = WkRecipFact * 20;		/* option q */
	QueueMode = QM_NORMAL;		/* what queue items to act upon */
	FileMode = (RealUid != geteuid()) ? 0644 : 0600;
						/* option F */
	QueueFileMode = (RealUid != geteuid()) ? 0644 : 0600;
						/* option QueueFileMode */

	if (((pw = sm_getpwnam("mailnull")) != NULL && pw->pw_uid != 0) ||
	    ((pw = sm_getpwnam("sendmail")) != NULL && pw->pw_uid != 0) ||
	    ((pw = sm_getpwnam("daemon")) != NULL && pw->pw_uid != 0))
	{
		DefUid = pw->pw_uid;		/* option u */
		DefGid = pw->pw_gid;		/* option g */
		DefUser = newstr(pw->pw_name);
	}
	else
	{
		DefUid = 1;			/* option u */
		DefGid = 1;			/* option g */
		setdefuser();
	}
	TrustedUid = 0;
	if (tTd(37, 4))
		sm_dprintf("setdefaults: DefUser=%s, DefUid=%ld, DefGid=%ld\n",
			DefUser != NULL ? DefUser : "<1:1>",
			(long) DefUid, (long) DefGid);
	CheckpointInterval = 10;		/* option C */
	MaxHopCount = 25;			/* option h */
	set_delivery_mode(SM_FORK, e);		/* option d */
	e->e_errormode = EM_PRINT;		/* option e */
	e->e_qgrp = NOQGRP;
	e->e_qdir = NOQDIR;
	e->e_xfqgrp = NOQGRP;
	e->e_xfqdir = NOQDIR;
	e->e_ctime = curtime();
	SevenBitInput = false;			/* option 7 */
	MaxMciCache = 1;			/* option k */
	MciCacheTimeout = 5 MINUTES;		/* option K */
	LogLevel = 9;				/* option L */
#if MILTER
	MilterLogLevel = -1;
#endif /* MILTER */
	inittimeouts(NULL, false);		/* option r */
	PrivacyFlags = PRIV_PUBLIC;		/* option p */
	MeToo = true;				/* option m */
	SendMIMEErrors = true;			/* option f */
	SuperSafe = SAFE_REALLY;		/* option s */
	clrbitmap(DontBlameSendmail);		/* DontBlameSendmail option */
#if MIME8TO7
	MimeMode = MM_CVTMIME|MM_PASS8BIT;	/* option 8 */
#else /* MIME8TO7 */
	MimeMode = MM_PASS8BIT;
#endif /* MIME8TO7 */
	for (i = 0; i < MAXTOCLASS; i++)
	{
		TimeOuts.to_q_return[i] = 5 DAYS;	/* option T */
		TimeOuts.to_q_warning[i] = 0;		/* option T */
	}
	ServiceSwitchFile = "/etc/mail/service.switch";
	ServiceCacheMaxAge = (time_t) 10;
	HostsFile = _PATH_HOSTS;
	PidFile = newstr(_PATH_SENDMAILPID);
	MustQuoteChars = "@,;:\\()[].'";
	MciInfoTimeout = 30 MINUTES;
	MaxRuleRecursion = MAXRULERECURSION;
	MaxAliasRecursion = 10;
	MaxMacroRecursion = 10;
	ColonOkInAddr = true;
	DontLockReadFiles = true;
	DontProbeInterfaces = DPI_PROBEALL;
	DoubleBounceAddr = "postmaster";
	MaxHeadersLength = MAXHDRSLEN;
	MaxMimeHeaderLength = MAXLINE;
	MaxMimeFieldLength = MaxMimeHeaderLength / 2;
	MaxForwardEntries = 0;
	FastSplit = 1;
	MaxNOOPCommands = MAXNOOPCOMMANDS;
#if SASL
	AuthMechanisms = newstr(AUTH_MECHANISMS);
	AuthRealm = NULL;
	MaxSLBits = INT_MAX;
#endif /* SASL */
#if STARTTLS
	TLS_Srv_Opts = TLS_I_SRV;
	if (NULL == EVP_digest)
		EVP_digest = EVP_md5();
#endif /* STARTTLS */
#ifdef HESIOD_INIT
	HesiodContext = NULL;
#endif /* HESIOD_INIT */
#if NETINET6
	/* Detect if IPv6 is available at run time */
	i = socket(AF_INET6, SOCK_STREAM, 0);
	if (i >= 0)
	{
		InetMode = AF_INET6;
		(void) close(i);
	}
	else
		InetMode = AF_INET;
#if !IPV6_FULL
	UseCompressedIPv6Addresses = true;
#endif
#else /* NETINET6 */
	InetMode = AF_INET;
#endif /* NETINET6 */
	ControlSocketName = NULL;
	memset(&ConnectOnlyTo, '\0', sizeof(ConnectOnlyTo));
	DataFileBufferSize = 4096;
	XscriptFileBufferSize = 4096;
	for (i = 0; i < MAXRWSETS; i++)
		RuleSetNames[i] = NULL;
#if MILTER
	InputFilters[0] = NULL;
#endif /* MILTER */
	RejectLogInterval = 3 HOURS;
#if REQUIRES_DIR_FSYNC
	RequiresDirfsync = true;
#endif /* REQUIRES_DIR_FSYNC */
#if _FFR_RCPTTHROTDELAY
	BadRcptThrottleDelay = 1;
#endif /* _FFR_RCPTTHROTDELAY */
	ConnectionRateWindowSize = 60;
#if _FFR_BOUNCE_QUEUE
	BounceQueue = NOQGRP;
#endif /* _FFR_BOUNCE_QUEUE */
	setupmaps();
	setupqueues();
	setupmailers();
	setupheaders();
}


/*
**  SETDEFUSER -- set/reset DefUser using DefUid (for initgroups())
*/

void
setdefuser()
{
	struct passwd *defpwent;
	static char defuserbuf[40];

	DefUser = defuserbuf;
	defpwent = sm_getpwuid(DefUid);
	(void) sm_strlcpy(defuserbuf,
			  (defpwent == NULL || defpwent->pw_name == NULL)
			   ? "nobody" : defpwent->pw_name,
			  sizeof(defuserbuf));
	if (tTd(37, 4))
		sm_dprintf("setdefuser: DefUid=%ld, DefUser=%s\n",
			   (long) DefUid, DefUser);
}
/*
**  SETUPQUEUES -- initialize default queues
**
**	The mqueue QUEUE structure gets filled in after readcf() but
**	we need something to point to now for the mailer setup,
**	which use "mqueue" as default queue.
*/

static void
setupqueues()
{
	char buf[100];

	MaxRunnersPerQueue = 1;
	(void) sm_strlcpy(buf, "mqueue, P=/var/spool/mqueue", sizeof(buf));
	makequeue(buf, false);
}
/*
**  SETUPMAILERS -- initialize default mailers
*/

static void
setupmailers()
{
	char buf[100];

	(void) sm_strlcpy(buf, "prog, P=/bin/sh, F=lsouDq9, T=X-Unix/X-Unix/X-Unix, A=sh -c \201u",
			sizeof(buf));
	makemailer(buf);

	(void) sm_strlcpy(buf, "*file*, P=[FILE], F=lsDFMPEouq9, T=X-Unix/X-Unix/X-Unix, A=FILE \201u",
			sizeof(buf));
	makemailer(buf);

	(void) sm_strlcpy(buf, "*include*, P=/dev/null, F=su, A=INCLUDE \201u",
			sizeof(buf));
	makemailer(buf);
	initerrmailers();
}
/*
**  SETUPMAPS -- set up map classes
*/

#define MAPDEF(name, ext, flags, parse, open, close, lookup, store) \
	{ \
		extern bool parse __P((MAP *, char *)); \
		extern bool open __P((MAP *, int)); \
		extern void close __P((MAP *)); \
		extern char *lookup __P((MAP *, char *, char **, int *)); \
		extern void store __P((MAP *, char *, char *)); \
		s = stab(name, ST_MAPCLASS, ST_ENTER); \
		s->s_mapclass.map_cname = name; \
		s->s_mapclass.map_ext = ext; \
		s->s_mapclass.map_cflags = flags; \
		s->s_mapclass.map_parse = parse; \
		s->s_mapclass.map_open = open; \
		s->s_mapclass.map_close = close; \
		s->s_mapclass.map_lookup = lookup; \
		s->s_mapclass.map_store = store; \
	}

static void
setupmaps()
{
	register STAB *s;

#if NEWDB
# if DB_VERSION_MAJOR > 1
	int major_v, minor_v, patch_v;

	(void) db_version(&major_v, &minor_v, &patch_v);
	if (major_v != DB_VERSION_MAJOR || minor_v != DB_VERSION_MINOR)
	{
		errno = 0;
		syserr("Berkeley DB version mismatch: compiled against %d.%d.%d, run-time linked against %d.%d.%d",
		  DB_VERSION_MAJOR, DB_VERSION_MINOR, DB_VERSION_PATCH,
		  major_v, minor_v, patch_v);
	}
# endif /* DB_VERSION_MAJOR > 1 */

	MAPDEF("hash", ".db", MCF_ALIASOK|MCF_REBUILDABLE,
		map_parseargs, hash_map_open, db_map_close,
		db_map_lookup, db_map_store);

	MAPDEF("btree", ".db", MCF_ALIASOK|MCF_REBUILDABLE,
		map_parseargs, bt_map_open, db_map_close,
		db_map_lookup, db_map_store);
#endif /* NEWDB */

#if NDBM
	MAPDEF("dbm", ".dir", MCF_ALIASOK|MCF_REBUILDABLE,
		map_parseargs, ndbm_map_open, ndbm_map_close,
		ndbm_map_lookup, ndbm_map_store);
#endif /* NDBM */

#if NIS
	MAPDEF("nis", NULL, MCF_ALIASOK,
		map_parseargs, nis_map_open, null_map_close,
		nis_map_lookup, null_map_store);
#endif /* NIS */

#if NISPLUS
	MAPDEF("nisplus", NULL, MCF_ALIASOK,
		map_parseargs, nisplus_map_open, null_map_close,
		nisplus_map_lookup, null_map_store);
#endif /* NISPLUS */

#if LDAPMAP
	MAPDEF("ldap", NULL, MCF_ALIASOK|MCF_NOTPERSIST,
		ldapmap_parseargs, ldapmap_open, ldapmap_close,
		ldapmap_lookup, null_map_store);
#endif /* LDAPMAP */

#if PH_MAP
	MAPDEF("ph", NULL, MCF_NOTPERSIST,
		ph_map_parseargs, ph_map_open, ph_map_close,
		ph_map_lookup, null_map_store);
#endif /* PH_MAP */

#if MAP_NSD
	/* IRIX 6.5 nsd support */
	MAPDEF("nsd", NULL, MCF_ALIASOK,
	       map_parseargs, null_map_open, null_map_close,
	       nsd_map_lookup, null_map_store);
#endif /* MAP_NSD */

#if HESIOD
	MAPDEF("hesiod", NULL, MCF_ALIASOK|MCF_ALIASONLY,
		map_parseargs, hes_map_open, hes_map_close,
		hes_map_lookup, null_map_store);
#endif /* HESIOD */

#if NETINFO
	MAPDEF("netinfo", NULL, MCF_ALIASOK,
		map_parseargs, ni_map_open, null_map_close,
		ni_map_lookup, null_map_store);
#endif /* NETINFO */

#if 0
	MAPDEF("dns", NULL, 0,
		dns_map_init, null_map_open, null_map_close,
		dns_map_lookup, null_map_store);
#endif /* 0 */

#if NAMED_BIND
# if DNSMAP
#  if _FFR_DNSMAP_ALIASABLE
	MAPDEF("dns", NULL, MCF_ALIASOK,
	       dns_map_parseargs, dns_map_open, null_map_close,
	       dns_map_lookup, null_map_store);
#  else /* _FFR_DNSMAP_ALIASABLE */
	MAPDEF("dns", NULL, 0,
	       dns_map_parseargs, dns_map_open, null_map_close,
	       dns_map_lookup, null_map_store);
#  endif /* _FFR_DNSMAP_ALIASABLE */
# endif /* DNSMAP */
#endif /* NAMED_BIND */

#if NAMED_BIND
	/* best MX DNS lookup */
	MAPDEF("bestmx", NULL, MCF_OPTFILE,
		map_parseargs, null_map_open, null_map_close,
		bestmx_map_lookup, null_map_store);
#endif /* NAMED_BIND */

	MAPDEF("host", NULL, 0,
		host_map_init, null_map_open, null_map_close,
		host_map_lookup, null_map_store);

	MAPDEF("text", NULL, MCF_ALIASOK,
		map_parseargs, text_map_open, null_map_close,
		text_map_lookup, null_map_store);

	MAPDEF("stab", NULL, MCF_ALIASOK|MCF_ALIASONLY,
		map_parseargs, stab_map_open, null_map_close,
		stab_map_lookup, stab_map_store);

	MAPDEF("implicit", NULL, MCF_ALIASOK|MCF_ALIASONLY|MCF_REBUILDABLE,
		map_parseargs, impl_map_open, impl_map_close,
		impl_map_lookup, impl_map_store);

	/* access to system passwd file */
	MAPDEF("user", NULL, MCF_OPTFILE,
		map_parseargs, user_map_open, null_map_close,
		user_map_lookup, null_map_store);

	/* dequote map */
	MAPDEF("dequote", NULL, 0,
		dequote_init, null_map_open, null_map_close,
		dequote_map, null_map_store);

#if MAP_REGEX
	MAPDEF("regex", NULL, 0,
		regex_map_init, null_map_open, null_map_close,
		regex_map_lookup, null_map_store);
#endif /* MAP_REGEX */

#if USERDB
	/* user database */
	MAPDEF("userdb", ".db", 0,
		map_parseargs, null_map_open, null_map_close,
		udb_map_lookup, null_map_store);
#endif /* USERDB */

	/* arbitrary programs */
	MAPDEF("program", NULL, MCF_ALIASOK,
		map_parseargs, null_map_open, null_map_close,
		prog_map_lookup, null_map_store);

	/* sequenced maps */
	MAPDEF("sequence", NULL, MCF_ALIASOK,
		seq_map_parse, null_map_open, null_map_close,
		seq_map_lookup, seq_map_store);

	/* switched interface to sequenced maps */
	MAPDEF("switch", NULL, MCF_ALIASOK,
		map_parseargs, switch_map_open, null_map_close,
		seq_map_lookup, seq_map_store);

	/* null map lookup -- really for internal use only */
	MAPDEF("null", NULL, MCF_ALIASOK|MCF_OPTFILE,
		map_parseargs, null_map_open, null_map_close,
		null_map_lookup, null_map_store);

	/* syslog map -- logs information to syslog */
	MAPDEF("syslog", NULL, 0,
		syslog_map_parseargs, null_map_open, null_map_close,
		syslog_map_lookup, null_map_store);

	/* macro storage map -- rulesets can set macros */
	MAPDEF("macro", NULL, 0,
		dequote_init, null_map_open, null_map_close,
		macro_map_lookup, null_map_store);

	/* arithmetic map -- add/subtract/compare */
	MAPDEF("arith", NULL, 0,
		dequote_init, null_map_open, null_map_close,
		arith_map_lookup, null_map_store);

	/* "arpa" map -- IP -> arpa */
	MAPDEF("arpa", NULL, 0,
		dequote_init, null_map_open, null_map_close,
		arpa_map_lookup, null_map_store);

#if SOCKETMAP
	/* arbitrary daemons */
	MAPDEF("socket", NULL, MCF_ALIASOK,
		map_parseargs, socket_map_open, socket_map_close,
		socket_map_lookup, null_map_store);
#endif /* SOCKETMAP */

#if _FFR_DPRINTF_MAP
	/* dprintf map -- logs information to syslog */
	MAPDEF("dprintf", NULL, 0,
		dprintf_map_parseargs, null_map_open, null_map_close,
		dprintf_map_lookup, null_map_store);
#endif /* _FFR_DPRINTF_MAP */

	if (tTd(38, 2))
	{
		/* bogus map -- always return tempfail */
		MAPDEF("bogus",	NULL, MCF_ALIASOK|MCF_OPTFILE,
		       map_parseargs, null_map_open, null_map_close,
		       bogus_map_lookup, null_map_store);
	}
}

#undef MAPDEF
/*
**  INITHOSTMAPS -- initial host-dependent maps
**
**	This should act as an interface to any local service switch
**	provided by the host operating system.
**
**	Parameters:
**		none
**
**	Returns:
**		none
**
**	Side Effects:
**		Should define maps "host" and "users" as necessary
**		for this OS.  If they are not defined, they will get
**		a default value later.  It should check to make sure
**		they are not defined first, since it's possible that
**		the config file has provided an override.
*/

void
inithostmaps()
{
	register int i;
	int nmaps;
	char *maptype[MAXMAPSTACK];
	short mapreturn[MAXMAPACTIONS];
	char buf[MAXLINE];

	/*
	**  Make sure we have a host map.
	*/

	if (stab("host", ST_MAP, ST_FIND) == NULL)
	{
		/* user didn't initialize: set up host map */
		(void) sm_strlcpy(buf, "host host", sizeof(buf));
#if NAMED_BIND
		if (ConfigLevel >= 2)
			(void) sm_strlcat(buf, " -a. -D", sizeof(buf));
#endif /* NAMED_BIND */
		(void) makemapentry(buf);
	}

	/*
	**  Set up default aliases maps
	*/

	nmaps = switch_map_find("aliases", maptype, mapreturn);
	for (i = 0; i < nmaps; i++)
	{
		if (strcmp(maptype[i], "files") == 0 &&
		    stab("aliases.files", ST_MAP, ST_FIND) == NULL)
		{
			(void) sm_strlcpy(buf, "aliases.files null",
					  sizeof(buf));
			(void) makemapentry(buf);
		}
#if NISPLUS
		else if (strcmp(maptype[i], "nisplus") == 0 &&
			 stab("aliases.nisplus", ST_MAP, ST_FIND) == NULL)
		{
			(void) sm_strlcpy(buf, "aliases.nisplus nisplus -kalias -vexpansion mail_aliases.org_dir",
				sizeof(buf));
			(void) makemapentry(buf);
		}
#endif /* NISPLUS */
#if NIS
		else if (strcmp(maptype[i], "nis") == 0 &&
			 stab("aliases.nis", ST_MAP, ST_FIND) == NULL)
		{
			(void) sm_strlcpy(buf, "aliases.nis nis mail.aliases",
				sizeof(buf));
			(void) makemapentry(buf);
		}
#endif /* NIS */
#if NETINFO
		else if (strcmp(maptype[i], "netinfo") == 0 &&
			 stab("aliases.netinfo", ST_MAP, ST_FIND) == NULL)
		{
			(void) sm_strlcpy(buf, "aliases.netinfo netinfo -z, /aliases",
				sizeof(buf));
			(void) makemapentry(buf);
		}
#endif /* NETINFO */
#if HESIOD
		else if (strcmp(maptype[i], "hesiod") == 0 &&
			 stab("aliases.hesiod", ST_MAP, ST_FIND) == NULL)
		{
			(void) sm_strlcpy(buf, "aliases.hesiod hesiod aliases",
				sizeof(buf));
			(void) makemapentry(buf);
		}
#endif /* HESIOD */
#if LDAPMAP && defined(SUN_EXTENSIONS) && \
    defined(SUN_SIMPLIFIED_LDAP) && HASLDAPGETALIASBYNAME
		else if (strcmp(maptype[i], "ldap") == 0 &&
		    stab("aliases.ldap", ST_MAP, ST_FIND) == NULL)
		{
			(void) sm_strlcpy(buf, "aliases.ldap ldap -b . -h localhost -k mail=%0 -v mailgroup",
				sizeof buf);
			(void) makemapentry(buf);
		}
#endif /* LDAPMAP && defined(SUN_EXTENSIONS) && ... */
	}
	if (stab("aliases", ST_MAP, ST_FIND) == NULL)
	{
		(void) sm_strlcpy(buf, "aliases switch aliases", sizeof(buf));
		(void) makemapentry(buf);
	}
}

/*
**  SWITCH_MAP_FIND -- find the list of types associated with a map
**
**	This is the system-dependent interface to the service switch.
**
**	Parameters:
**		service -- the name of the service of interest.
**		maptype -- an out-array of strings containing the types
**			of access to use for this service.  There can
**			be at most MAXMAPSTACK types for a single service.
**		mapreturn -- an out-array of return information bitmaps
**			for the map.
**
**	Returns:
**		The number of map types filled in, or -1 for failure.
**
**	Side effects:
**		Preserves errno so nothing in the routine clobbers it.
*/

#if defined(SOLARIS) || (defined(sony_news) && defined(__svr4))
# define _USE_SUN_NSSWITCH_
#endif /* defined(SOLARIS) || (defined(sony_news) && defined(__svr4)) */

#if _FFR_HPUX_NSSWITCH
# ifdef __hpux
#  define _USE_SUN_NSSWITCH_
# endif /* __hpux */
#endif /* _FFR_HPUX_NSSWITCH */

#ifdef _USE_SUN_NSSWITCH_
# include <nsswitch.h>
#endif /* _USE_SUN_NSSWITCH_ */

#if defined(ultrix) || (defined(__osf__) && defined(__alpha))
# define _USE_DEC_SVC_CONF_
#endif /* defined(ultrix) || (defined(__osf__) && defined(__alpha)) */

#ifdef _USE_DEC_SVC_CONF_
# include <sys/svcinfo.h>
#endif /* _USE_DEC_SVC_CONF_ */

int
switch_map_find(service, maptype, mapreturn)
	char *service;
	char *maptype[MAXMAPSTACK];
	short mapreturn[MAXMAPACTIONS];
{
	int svcno = 0;
	int save_errno = errno;

#ifdef _USE_SUN_NSSWITCH_
	struct __nsw_switchconfig *nsw_conf;
	enum __nsw_parse_err pserr;
	struct __nsw_lookup *lk;
	static struct __nsw_lookup lkp0 =
		{ "files", {1, 0, 0, 0}, NULL, NULL };
	static struct __nsw_switchconfig lkp_default =
		{ 0, "sendmail", 3, &lkp0 };

	for (svcno = 0; svcno < MAXMAPACTIONS; svcno++)
		mapreturn[svcno] = 0;

	if ((nsw_conf = __nsw_getconfig(service, &pserr)) == NULL)
		lk = lkp_default.lookups;
	else
		lk = nsw_conf->lookups;
	svcno = 0;
	while (lk != NULL && svcno < MAXMAPSTACK)
	{
		maptype[svcno] = lk->service_name;
		if (lk->actions[__NSW_NOTFOUND] == __NSW_RETURN)
			mapreturn[MA_NOTFOUND] |= 1 << svcno;
		if (lk->actions[__NSW_TRYAGAIN] == __NSW_RETURN)
			mapreturn[MA_TRYAGAIN] |= 1 << svcno;
		if (lk->actions[__NSW_UNAVAIL] == __NSW_RETURN)
			mapreturn[MA_TRYAGAIN] |= 1 << svcno;
		svcno++;
		lk = lk->next;
	}
	errno = save_errno;
	return svcno;
#endif /* _USE_SUN_NSSWITCH_ */

#ifdef _USE_DEC_SVC_CONF_
	struct svcinfo *svcinfo;
	int svc;

	for (svcno = 0; svcno < MAXMAPACTIONS; svcno++)
		mapreturn[svcno] = 0;

	svcinfo = getsvc();
	if (svcinfo == NULL)
		goto punt;
	if (strcmp(service, "hosts") == 0)
		svc = SVC_HOSTS;
	else if (strcmp(service, "aliases") == 0)
		svc = SVC_ALIASES;
	else if (strcmp(service, "passwd") == 0)
		svc = SVC_PASSWD;
	else
	{
		errno = save_errno;
		return -1;
	}
	for (svcno = 0; svcno < SVC_PATHSIZE && svcno < MAXMAPSTACK; svcno++)
	{
		switch (svcinfo->svcpath[svc][svcno])
		{
		  case SVC_LOCAL:
			maptype[svcno] = "files";
			break;

		  case SVC_YP:
			maptype[svcno] = "nis";
			break;

		  case SVC_BIND:
			maptype[svcno] = "dns";
			break;

# ifdef SVC_HESIOD
		  case SVC_HESIOD:
			maptype[svcno] = "hesiod";
			break;
# endif /* SVC_HESIOD */

		  case SVC_LAST:
			errno = save_errno;
			return svcno;
		}
	}
	errno = save_errno;
	return svcno;
#endif /* _USE_DEC_SVC_CONF_ */

#if !defined(_USE_SUN_NSSWITCH_) && !defined(_USE_DEC_SVC_CONF_)
	/*
	**  Fall-back mechanism.
	*/

	STAB *st;
	static time_t servicecachetime;	/* time service switch was cached */
	time_t now = curtime();

	for (svcno = 0; svcno < MAXMAPACTIONS; svcno++)
		mapreturn[svcno] = 0;

	if ((now - servicecachetime) > (time_t) ServiceCacheMaxAge)
	{
		/* (re)read service switch */
		register SM_FILE_T *fp;
		long sff = SFF_REGONLY|SFF_OPENASROOT|SFF_NOLOCK;

		if (!bitnset(DBS_LINKEDSERVICESWITCHFILEINWRITABLEDIR,
			    DontBlameSendmail))
			sff |= SFF_NOWLINK;

		if (ConfigFileRead)
			servicecachetime = now;
		fp = safefopen(ServiceSwitchFile, O_RDONLY, 0, sff);
		if (fp != NULL)
		{
			char buf[MAXLINE];

			while (sm_io_fgets(fp, SM_TIME_DEFAULT, buf,
					   sizeof(buf)) >= 0)
			{
				register char *p;

				p = strpbrk(buf, "#\n");
				if (p != NULL)
					*p = '\0';
#ifndef SM_NSSWITCH_DELIMS
# define SM_NSSWITCH_DELIMS	" \t"
#endif /* SM_NSSWITCH_DELIMS */
				p = strpbrk(buf, SM_NSSWITCH_DELIMS);
				if (p != NULL)
					*p++ = '\0';
				if (buf[0] == '\0')
					continue;
				if (p == NULL)
				{
					sm_syslog(LOG_ERR, NOQID,
						  "Bad line on %.100s: %.100s",
						  ServiceSwitchFile,
						  buf);
					continue;
				}
				while (isascii(*p) && isspace(*p))
					p++;
				if (*p == '\0')
					continue;

				/*
				**  Find/allocate space for this service entry.
				**	Space for all of the service strings
				**	are allocated at once.  This means
				**	that we only have to free the first
				**	one to free all of them.
				*/

				st = stab(buf, ST_SERVICE, ST_ENTER);
				if (st->s_service[0] != NULL)
					sm_free((void *) st->s_service[0]); /* XXX */
				p = newstr(p);
				for (svcno = 0; svcno < MAXMAPSTACK; )
				{
					if (*p == '\0')
						break;
					st->s_service[svcno++] = p;
					p = strpbrk(p, " \t");
					if (p == NULL)
						break;
					*p++ = '\0';
					while (isascii(*p) && isspace(*p))
						p++;
				}
				if (svcno < MAXMAPSTACK)
					st->s_service[svcno] = NULL;
			}
			(void) sm_io_close(fp, SM_TIME_DEFAULT);
		}
	}

	/* look up entry in cache */
	st = stab(service, ST_SERVICE, ST_FIND);
	if (st != NULL && st->s_service[0] != NULL)
	{
		/* extract data */
		svcno = 0;
		while (svcno < MAXMAPSTACK)
		{
			maptype[svcno] = st->s_service[svcno];
			if (maptype[svcno++] == NULL)
				break;
		}
		errno = save_errno;
		return --svcno;
	}
#endif /* !defined(_USE_SUN_NSSWITCH_) && !defined(_USE_DEC_SVC_CONF_) */

#if !defined(_USE_SUN_NSSWITCH_)
	/* if the service file doesn't work, use an absolute fallback */
# ifdef _USE_DEC_SVC_CONF_
  punt:
# endif /* _USE_DEC_SVC_CONF_ */
	for (svcno = 0; svcno < MAXMAPACTIONS; svcno++)
		mapreturn[svcno] = 0;
	svcno = 0;
	if (strcmp(service, "aliases") == 0)
	{
		maptype[svcno++] = "files";
# if defined(AUTO_NETINFO_ALIASES) && defined (NETINFO)
		maptype[svcno++] = "netinfo";
# endif /* defined(AUTO_NETINFO_ALIASES) && defined (NETINFO) */
# ifdef AUTO_NIS_ALIASES
#  if NISPLUS
		maptype[svcno++] = "nisplus";
#  endif /* NISPLUS */
#  if NIS
		maptype[svcno++] = "nis";
#  endif /* NIS */
# endif /* AUTO_NIS_ALIASES */
		errno = save_errno;
		return svcno;
	}
	if (strcmp(service, "hosts") == 0)
	{
# if NAMED_BIND
		maptype[svcno++] = "dns";
# else /* NAMED_BIND */
#  if defined(sun) && !defined(BSD)
		/* SunOS */
		maptype[svcno++] = "nis";
#  endif /* defined(sun) && !defined(BSD) */
# endif /* NAMED_BIND */
# if defined(AUTO_NETINFO_HOSTS) && defined (NETINFO)
		maptype[svcno++] = "netinfo";
# endif /* defined(AUTO_NETINFO_HOSTS) && defined (NETINFO) */
		maptype[svcno++] = "files";
		errno = save_errno;
		return svcno;
	}
	errno = save_errno;
	return -1;
#endif /* !defined(_USE_SUN_NSSWITCH_) */
}
/*
**  USERNAME -- return the user id of the logged in user.
**
**	Parameters:
**		none.
**
**	Returns:
**		The login name of the logged in user.
**
**	Side Effects:
**		none.
**
**	Notes:
**		The return value is statically allocated.
*/

char *
username()
{
	static char *myname = NULL;
	extern char *getlogin();
	register struct passwd *pw;

	/* cache the result */
	if (myname == NULL)
	{
		myname = getlogin();
		if (myname == NULL || myname[0] == '\0')
		{
			pw = sm_getpwuid(RealUid);
			if (pw != NULL)
				myname = pw->pw_name;
		}
		else
		{
			uid_t uid = RealUid;

			if ((pw = sm_getpwnam(myname)) == NULL ||
			      (uid != 0 && uid != pw->pw_uid))
			{
				pw = sm_getpwuid(uid);
				if (pw != NULL)
					myname = pw->pw_name;
			}
		}
		if (myname == NULL || myname[0] == '\0')
		{
			syserr("554 5.3.0 Who are you?");
			myname = "postmaster";
		}
		else if (strpbrk(myname, ",;:/|\"\\") != NULL)
			myname = addquotes(myname, NULL);
		else
			myname = sm_pstrdup_x(myname);
	}
	return myname;
}
/*
**  TTYPATH -- Get the path of the user's tty
**
**	Returns the pathname of the user's tty.  Returns NULL if
**	the user is not logged in or if s/he has write permission
**	denied.
**
**	Parameters:
**		none
**
**	Returns:
**		pathname of the user's tty.
**		NULL if not logged in or write permission denied.
**
**	Side Effects:
**		none.
**
**	WARNING:
**		Return value is in a local buffer.
**
**	Called By:
**		savemail
*/

char *
ttypath()
{
	struct stat stbuf;
	register char *pathn;
	extern char *ttyname();
	extern char *getlogin();

	/* compute the pathname of the controlling tty */
	if ((pathn = ttyname(2)) == NULL && (pathn = ttyname(1)) == NULL &&
	    (pathn = ttyname(0)) == NULL)
	{
		errno = 0;
		return NULL;
	}

	/* see if we have write permission */
	if (stat(pathn, &stbuf) < 0 || !bitset(S_IWOTH, stbuf.st_mode))
	{
		errno = 0;
		return NULL;
	}

	/* see if the user is logged in */
	if (getlogin() == NULL)
		return NULL;

	/* looks good */
	return pathn;
}
/*
**  CHECKCOMPAT -- check for From and To person compatible.
**
**	This routine can be supplied on a per-installation basis
**	to determine whether a person is allowed to send a message.
**	This allows restriction of certain types of internet
**	forwarding or registration of users.
**
**	If the hosts are found to be incompatible, an error
**	message should be given using "usrerr" and an EX_ code
**	should be returned.  You can also set to->q_status to
**	a DSN-style status code.
**
**	EF_NO_BODY_RETN can be set in e->e_flags to suppress the
**	body during the return-to-sender function; this should be done
**	on huge messages.  This bit may already be set by the ESMTP
**	protocol.
**
**	Parameters:
**		to -- the person being sent to.
**
**	Returns:
**		an exit status
**
**	Side Effects:
**		none (unless you include the usrerr stuff)
*/

int
checkcompat(to, e)
	register ADDRESS *to;
	register ENVELOPE *e;
{
	if (tTd(49, 1))
		sm_dprintf("checkcompat(to=%s, from=%s)\n",
			to->q_paddr, e->e_from.q_paddr);

#ifdef EXAMPLE_CODE
	/* this code is intended as an example only */
	register STAB *s;

	s = stab("arpa", ST_MAILER, ST_FIND);
	if (s != NULL && strcmp(e->e_from.q_mailer->m_name, "local") != 0 &&
	    to->q_mailer == s->s_mailer)
	{
		usrerr("553 No ARPA mail through this machine: see your system administration");
		/* e->e_flags |= EF_NO_BODY_RETN; to suppress body on return */
		to->q_status = "5.7.1";
		return EX_UNAVAILABLE;
	}
#endif /* EXAMPLE_CODE */
	return EX_OK;
}

#ifdef SUN_EXTENSIONS
static void
init_md_sun()
{
	struct stat sbuf;

	/* Check for large file descriptor */
	if (fstat(fileno(stdin), &sbuf) < 0)
	{
		if (errno == EOVERFLOW)
		{
			perror("stdin");
			exit(EX_NOINPUT);
		}
	}
}
#endif /* SUN_EXTENSIONS */

/*
**  INIT_MD -- do machine dependent initializations
**
**	Systems that have global modes that should be set should do
**	them here rather than in main.
*/

#ifdef _AUX_SOURCE
# include <compat.h>
#endif /* _AUX_SOURCE */

#if SHARE_V1
# include <shares.h>
#endif /* SHARE_V1 */

void
init_md(argc, argv)
	int argc;
	char **argv;
{
#ifdef _AUX_SOURCE
	setcompat(getcompat() | COMPAT_BSDPROT);
#endif /* _AUX_SOURCE */

#ifdef SUN_EXTENSIONS
	init_md_sun();
#endif /* SUN_EXTENSIONS */

#if _CONVEX_SOURCE
	/* keep gethostby*() from stripping the local domain name */
	set_domain_trim_off();
#endif /* _CONVEX_SOURCE */
#if defined(__QNX__) && !defined(__QNXNTO__)
	/*
	**  Due to QNX's network distributed nature, you can target a tcpip
	**  stack on a different node in the qnx network; this patch lets
	**  this feature work.  The __sock_locate() must be done before the
	**  environment is clear.
	*/
	__sock_locate();
#endif /* __QNX__ */
#if SECUREWARE || defined(_SCO_unix_)
	set_auth_parameters(argc, argv);

# ifdef _SCO_unix_
	/*
	**  This is required for highest security levels (the kernel
	**  won't let it call set*uid() or run setuid binaries without
	**  it).  It may be necessary on other SECUREWARE systems.
	*/

	if (getluid() == -1)
		setluid(0);
# endif /* _SCO_unix_ */
#endif /* SECUREWARE || defined(_SCO_unix_) */


#ifdef VENDOR_DEFAULT
	VendorCode = VENDOR_DEFAULT;
#else /* VENDOR_DEFAULT */
	VendorCode = VENDOR_BERKELEY;
#endif /* VENDOR_DEFAULT */
}
/*
**  INIT_VENDOR_MACROS -- vendor-dependent macro initializations
**
**	Called once, on startup.
**
**	Parameters:
**		e -- the global envelope.
**
**	Returns:
**		none.
**
**	Side Effects:
**		vendor-dependent.
*/

void
init_vendor_macros(e)
	register ENVELOPE *e;
{
}
/*
**  GETLA -- get the current load average
**
**	This code stolen from la.c.
**
**	Parameters:
**		none.
**
**	Returns:
**		The current load average as an integer.
**
**	Side Effects:
**		none.
*/

/* try to guess what style of load average we have */
#define LA_ZERO		1	/* always return load average as zero */
#define LA_INT		2	/* read kmem for avenrun; interpret as long */
#define LA_FLOAT	3	/* read kmem for avenrun; interpret as float */
#define LA_SUBR		4	/* call getloadavg */
#define LA_MACH		5	/* MACH load averages (as on NeXT boxes) */
#define LA_SHORT	6	/* read kmem for avenrun; interpret as short */
#define LA_PROCSTR	7	/* read string ("1.17") from /proc/loadavg */
#define LA_READKSYM	8	/* SVR4: use MIOC_READKSYM ioctl call */
#define LA_DGUX		9	/* special DGUX implementation */
#define LA_HPUX		10	/* special HPUX implementation */
#define LA_IRIX6	11	/* special IRIX 6.2 implementation */
#define LA_KSTAT	12	/* special Solaris kstat(3k) implementation */
#define LA_DEVSHORT	13	/* read short from a device */
#define LA_ALPHAOSF	14	/* Digital UNIX (OSF/1 on Alpha) table() call */
#define LA_PSET		15	/* Solaris per-processor-set load average */
#define LA_LONGLONG	17 /* read kmem for avenrun; interpret as long long */

/* do guesses based on general OS type */
#ifndef LA_TYPE
# define LA_TYPE	LA_ZERO
#endif /* ! LA_TYPE */

#ifndef FSHIFT
# if defined(unixpc)
#  define FSHIFT	5
# endif /* defined(unixpc) */

# if defined(__alpha) || defined(IRIX)
#  define FSHIFT	10
# endif /* defined(__alpha) || defined(IRIX) */

#endif /* ! FSHIFT */

#ifndef FSHIFT
# define FSHIFT		8
#endif /* ! FSHIFT */

#ifndef FSCALE
# define FSCALE		(1 << FSHIFT)
#endif /* ! FSCALE */

#ifndef LA_AVENRUN
# ifdef SYSTEM5
#  define LA_AVENRUN	"avenrun"
# else /* SYSTEM5 */
#  define LA_AVENRUN	"_avenrun"
# endif /* SYSTEM5 */
#endif /* ! LA_AVENRUN */

/* _PATH_KMEM should be defined in <paths.h> */
#ifndef _PATH_KMEM
# define _PATH_KMEM	"/dev/kmem"
#endif /* ! _PATH_KMEM */

#if (LA_TYPE == LA_INT) || (LA_TYPE == LA_FLOAT) || (LA_TYPE == LA_SHORT) || (LA_TYPE == LA_LONGLONG)

# include <nlist.h>

/* _PATH_UNIX should be defined in <paths.h> */
# ifndef _PATH_UNIX
#  if defined(SYSTEM5)
#   define _PATH_UNIX	"/unix"
#  else /* defined(SYSTEM5) */
#   define _PATH_UNIX	"/vmunix"
#  endif /* defined(SYSTEM5) */
# endif /* ! _PATH_UNIX */

# ifdef _AUX_SOURCE
struct nlist	Nl[2];
# else /* _AUX_SOURCE */
struct nlist	Nl[] =
{
	{ LA_AVENRUN },
	{ 0 },
};
# endif /* _AUX_SOURCE */
# define X_AVENRUN	0

int
getla()
{
	int j;
	static int kmem = -1;
# if LA_TYPE == LA_INT
	long avenrun[3];
# else /* LA_TYPE == LA_INT */
#  if LA_TYPE == LA_SHORT
	short avenrun[3];
#  else
#   if LA_TYPE == LA_LONGLONG
	long long avenrun[3];
#   else /* LA_TYPE == LA_LONGLONG */
	double avenrun[3];
#   endif /* LA_TYPE == LA_LONGLONG */
#  endif /* LA_TYPE == LA_SHORT */
# endif /* LA_TYPE == LA_INT */
	extern off_t lseek();

	if (kmem < 0)
	{
# ifdef _AUX_SOURCE
		(void) sm_strlcpy(Nl[X_AVENRUN].n_name, LA_AVENRUN,
			       sizeof(Nl[X_AVENRUN].n_name));
		Nl[1].n_name[0] = '\0';
# endif /* _AUX_SOURCE */

# if defined(_AIX3) || defined(_AIX4)
		if (knlist(Nl, 1, sizeof(Nl[0])) < 0)
# else /* defined(_AIX3) || defined(_AIX4) */
		if (nlist(_PATH_UNIX, Nl) < 0)
# endif /* defined(_AIX3) || defined(_AIX4) */
		{
			if (tTd(3, 1))
				sm_dprintf("getla: nlist(%s): %s\n", _PATH_UNIX,
					   sm_errstring(errno));
			return -1;
		}
		if (Nl[X_AVENRUN].n_value == 0)
		{
			if (tTd(3, 1))
				sm_dprintf("getla: nlist(%s, %s) ==> 0\n",
					_PATH_UNIX, LA_AVENRUN);
			return -1;
		}
# ifdef NAMELISTMASK
		Nl[X_AVENRUN].n_value &= NAMELISTMASK;
# endif /* NAMELISTMASK */

		kmem = open(_PATH_KMEM, 0, 0);
		if (kmem < 0)
		{
			if (tTd(3, 1))
				sm_dprintf("getla: open(/dev/kmem): %s\n",
					   sm_errstring(errno));
			return -1;
		}
		if ((j = fcntl(kmem, F_GETFD, 0)) < 0 ||
		    fcntl(kmem, F_SETFD, j | FD_CLOEXEC) < 0)
		{
			if (tTd(3, 1))
				sm_dprintf("getla: fcntl(/dev/kmem, FD_CLOEXEC): %s\n",
					   sm_errstring(errno));
			(void) close(kmem);
			kmem = -1;
			return -1;
		}
	}
	if (tTd(3, 20))
		sm_dprintf("getla: symbol address = %#lx\n",
			(unsigned long) Nl[X_AVENRUN].n_value);
	if (lseek(kmem, (off_t) Nl[X_AVENRUN].n_value, SEEK_SET) == -1 ||
	    read(kmem, (char *) avenrun, sizeof(avenrun)) != sizeof(avenrun))
	{
		/* thank you Ian */
		if (tTd(3, 1))
			sm_dprintf("getla: lseek or read: %s\n",
				   sm_errstring(errno));
		return -1;
	}
# if (LA_TYPE == LA_INT) || (LA_TYPE == LA_SHORT) || (LA_TYPE == LA_LONGLONG)
	if (tTd(3, 5))
	{
#  if LA_TYPE == LA_SHORT
		sm_dprintf("getla: avenrun = %d", avenrun[0]);
		if (tTd(3, 15))
			sm_dprintf(", %d, %d", avenrun[1], avenrun[2]);
#  else /* LA_TYPE == LA_SHORT */
#   if LA_TYPE == LA_LONGLONG
		sm_dprintf("getla: avenrun = %lld", avenrun[0]);
		if (tTd(3, 15))
			sm_dprintf(", %lld, %lld", avenrun[1], avenrun[2]);
#   else /* LA_TYPE == LA_LONGLONG */
		sm_dprintf("getla: avenrun = %ld", avenrun[0]);
		if (tTd(3, 15))
			sm_dprintf(", %ld, %ld", avenrun[1], avenrun[2]);
#   endif /* LA_TYPE == LA_LONGLONG */
#  endif /* LA_TYPE == LA_SHORT */
		sm_dprintf("\n");
	}
	if (tTd(3, 1))
		sm_dprintf("getla: %d\n",
			(int) (avenrun[0] + FSCALE/2) >> FSHIFT);
	return ((int) (avenrun[0] + FSCALE/2) >> FSHIFT);
# else /* (LA_TYPE == LA_INT) || (LA_TYPE == LA_SHORT) || (LA_TYPE == LA_LONGLONG) */
	if (tTd(3, 5))
	{
		sm_dprintf("getla: avenrun = %g", avenrun[0]);
		if (tTd(3, 15))
			sm_dprintf(", %g, %g", avenrun[1], avenrun[2]);
		sm_dprintf("\n");
	}
	if (tTd(3, 1))
		sm_dprintf("getla: %d\n", (int) (avenrun[0] +0.5));
	return ((int) (avenrun[0] + 0.5));
# endif /* (LA_TYPE == LA_INT) || (LA_TYPE == LA_SHORT) || (LA_TYPE == LA_LONGLONG) */
}

#endif /* (LA_TYPE == LA_INT) || (LA_TYPE == LA_FLOAT) || (LA_TYPE == LA_SHORT) || (LA_TYPE == LA_LONGLONG) */

#if LA_TYPE == LA_READKSYM

# include <sys/ksym.h>

int
getla()
{
	int j;
	static int kmem = -1;
	long avenrun[3];
	struct mioc_rksym mirk;

	if (kmem < 0)
	{
		kmem = open("/dev/kmem", 0, 0);
		if (kmem < 0)
		{
			if (tTd(3, 1))
				sm_dprintf("getla: open(/dev/kmem): %s\n",
					   sm_errstring(errno));
			return -1;
		}
		if ((j = fcntl(kmem, F_GETFD, 0)) < 0 ||
		    fcntl(kmem, F_SETFD, j | FD_CLOEXEC) < 0)
		{
			if (tTd(3, 1))
				sm_dprintf("getla: fcntl(/dev/kmem, FD_CLOEXEC): %s\n",
					   sm_errstring(errno));
			(void) close(kmem);
			kmem = -1;
			return -1;
		}
	}
	mirk.mirk_symname = LA_AVENRUN;
	mirk.mirk_buf = avenrun;
	mirk.mirk_buflen = sizeof(avenrun);
	if (ioctl(kmem, MIOC_READKSYM, &mirk) < 0)
	{
		if (tTd(3, 1))
			sm_dprintf("getla: ioctl(MIOC_READKSYM) failed: %s\n",
				   sm_errstring(errno));
		return -1;
	}
	if (tTd(3, 5))
	{
		sm_dprintf("getla: avenrun = %d", avenrun[0]);
		if (tTd(3, 15))
			sm_dprintf(", %d, %d", avenrun[1], avenrun[2]);
		sm_dprintf("\n");
	}
	if (tTd(3, 1))
		sm_dprintf("getla: %d\n",
			(int) (avenrun[0] + FSCALE/2) >> FSHIFT);
	return ((int) (avenrun[0] + FSCALE/2) >> FSHIFT);
}

#endif /* LA_TYPE == LA_READKSYM */

#if LA_TYPE == LA_DGUX

# include <sys/dg_sys_info.h>

int
getla()
{
	struct dg_sys_info_load_info load_info;

	dg_sys_info((long *)&load_info,
		DG_SYS_INFO_LOAD_INFO_TYPE, DG_SYS_INFO_LOAD_VERSION_0);

	if (tTd(3, 1))
		sm_dprintf("getla: %d\n", (int) (load_info.one_minute + 0.5));

	return ((int) (load_info.one_minute + 0.5));
}

#endif /* LA_TYPE == LA_DGUX */

#if LA_TYPE == LA_HPUX

/* forward declarations to keep gcc from complaining */
struct pst_dynamic;
struct pst_status;
struct pst_static;
struct pst_vminfo;
struct pst_diskinfo;
struct pst_processor;
struct pst_lv;
struct pst_swapinfo;

# include <sys/param.h>
# include <sys/pstat.h>

int
getla()
{
	struct pst_dynamic pstd;

	if (pstat_getdynamic(&pstd, sizeof(struct pst_dynamic),
			     (size_t) 1, 0) == -1)
		return 0;

	if (tTd(3, 1))
		sm_dprintf("getla: %d\n", (int) (pstd.psd_avg_1_min + 0.5));

	return (int) (pstd.psd_avg_1_min + 0.5);
}

#endif /* LA_TYPE == LA_HPUX */

#if LA_TYPE == LA_SUBR

int
getla()
{
	double avenrun[3];

	if (getloadavg(avenrun, sizeof(avenrun) / sizeof(avenrun[0])) < 0)
	{
		if (tTd(3, 1))
			sm_dprintf("getla: getloadavg failed: %s",
				   sm_errstring(errno));
		return -1;
	}
	if (tTd(3, 1))
		sm_dprintf("getla: %d\n", (int) (avenrun[0] +0.5));
	return ((int) (avenrun[0] + 0.5));
}

#endif /* LA_TYPE == LA_SUBR */

#if LA_TYPE == LA_MACH

/*
**  This has been tested on NEXTSTEP release 2.1/3.X.
*/

# if defined(NX_CURRENT_COMPILER_RELEASE) && NX_CURRENT_COMPILER_RELEASE > NX_COMPILER_RELEASE_3_0
#  include <mach/mach.h>
# else /* defined(NX_CURRENT_COMPILER_RELEASE) && NX_CURRENT_COMPILER_RELEASE > NX_COMPILER_RELEASE_3_0 */
#  include <mach.h>
# endif /* defined(NX_CURRENT_COMPILER_RELEASE) && NX_CURRENT_COMPILER_RELEASE > NX_COMPILER_RELEASE_3_0 */

int
getla()
{
	processor_set_t default_set;
	kern_return_t error;
	unsigned int info_count;
	struct processor_set_basic_info info;
	host_t host;

	error = processor_set_default(host_self(), &default_set);
	if (error != KERN_SUCCESS)
	{
		if (tTd(3, 1))
			sm_dprintf("getla: processor_set_default failed: %s",
				   sm_errstring(errno));
		return -1;
	}
	info_count = PROCESSOR_SET_BASIC_INFO_COUNT;
	if (processor_set_info(default_set, PROCESSOR_SET_BASIC_INFO,
			       &host, (processor_set_info_t)&info,
			       &info_count) != KERN_SUCCESS)
	{
		if (tTd(3, 1))
			sm_dprintf("getla: processor_set_info failed: %s",
				   sm_errstring(errno));
		return -1;
	}
	if (tTd(3, 1))
		sm_dprintf("getla: %d\n",
			(int) ((info.load_average + (LOAD_SCALE / 2)) /
			       LOAD_SCALE));
	return (int) (info.load_average + (LOAD_SCALE / 2)) / LOAD_SCALE;
}

#endif /* LA_TYPE == LA_MACH */

#if LA_TYPE == LA_PROCSTR
# if SM_CONF_BROKEN_STRTOD
	ERROR: This OS has most likely a broken strtod() implemenentation.
	ERROR: The function is required for getla().
	ERROR: Check the compilation options _LA_PROCSTR and
	ERROR: _SM_CONF_BROKEN_STRTOD (without the leading _).
# endif /* SM_CONF_BROKEN_STRTOD */

/*
**  Read /proc/loadavg for the load average.  This is assumed to be
**  in a format like "0.15 0.12 0.06".
**
**	Initially intended for Linux.  This has been in the kernel
**	since at least 0.99.15.
*/

# ifndef _PATH_LOADAVG
#  define _PATH_LOADAVG	"/proc/loadavg"
# endif /* ! _PATH_LOADAVG */

int
getla()
{
	double avenrun;
	register int result;
	SM_FILE_T *fp;

	fp = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, _PATH_LOADAVG, SM_IO_RDONLY,
			NULL);
	if (fp == NULL)
	{
		if (tTd(3, 1))
			sm_dprintf("getla: sm_io_open(%s): %s\n",
				   _PATH_LOADAVG, sm_errstring(errno));
		return -1;
	}
	result = sm_io_fscanf(fp, SM_TIME_DEFAULT, "%lf", &avenrun);
	(void) sm_io_close(fp, SM_TIME_DEFAULT);
	if (result != 1)
	{
		if (tTd(3, 1))
			sm_dprintf("getla: sm_io_fscanf() = %d: %s\n",
				   result, sm_errstring(errno));
		return -1;
	}

	if (tTd(3, 1))
		sm_dprintf("getla(): %.2f\n", avenrun);

	return ((int) (avenrun + 0.5));
}

#endif /* LA_TYPE == LA_PROCSTR */

#if LA_TYPE == LA_IRIX6

# include <sys/sysmp.h>

# ifdef _UNICOSMP
#  define CAST_SYSMP(x)	(x)
# else /* _UNICOSMP */
#  define CAST_SYSMP(x)	((x) & 0x7fffffff)
# endif /* _UNICOSMP */

int
getla(void)
{
	int j;
	static int kmem = -1;
	int avenrun[3];

	if (kmem < 0)
	{
		kmem = open(_PATH_KMEM, 0, 0);
		if (kmem < 0)
		{
			if (tTd(3, 1))
				sm_dprintf("getla: open(%s): %s\n", _PATH_KMEM,
					   sm_errstring(errno));
			return -1;
		}
		if ((j = fcntl(kmem, F_GETFD, 0)) < 0 ||
		    fcntl(kmem, F_SETFD, j | FD_CLOEXEC) < 0)
		{
			if (tTd(3, 1))
				sm_dprintf("getla: fcntl(/dev/kmem, FD_CLOEXEC): %s\n",
					   sm_errstring(errno));
			(void) close(kmem);
			kmem = -1;
			return -1;
		}
	}

	if (lseek(kmem, CAST_SYSMP(sysmp(MP_KERNADDR, MPKA_AVENRUN)), SEEK_SET)
		== -1 ||
	    read(kmem, (char *) avenrun, sizeof(avenrun)) != sizeof(avenrun))
	{
		if (tTd(3, 1))
			sm_dprintf("getla: lseek or read: %s\n",
				   sm_errstring(errno));
		return -1;
	}
	if (tTd(3, 5))
	{
		sm_dprintf("getla: avenrun = %ld", (long int) avenrun[0]);
		if (tTd(3, 15))
			sm_dprintf(", %ld, %ld",
				(long int) avenrun[1], (long int) avenrun[2]);
		sm_dprintf("\n");
	}

	if (tTd(3, 1))
		sm_dprintf("getla: %d\n",
			(int) (avenrun[0] + FSCALE/2) >> FSHIFT);
	return ((int) (avenrun[0] + FSCALE/2) >> FSHIFT);

}
#endif /* LA_TYPE == LA_IRIX6 */

#if LA_TYPE == LA_KSTAT

# include <kstat.h>

int
getla()
{
	static kstat_ctl_t *kc = NULL;
	static kstat_t *ksp = NULL;
	kstat_named_t *ksn;
	int la;

	if (kc == NULL)		/* if not initialized before */
		kc = kstat_open();
	if (kc == NULL)
	{
		if (tTd(3, 1))
			sm_dprintf("getla: kstat_open(): %s\n",
				   sm_errstring(errno));
		return -1;
	}
	if (ksp == NULL)
		ksp = kstat_lookup(kc, "unix", 0, "system_misc");
	if (ksp == NULL)
	{
		if (tTd(3, 1))
			sm_dprintf("getla: kstat_lookup(): %s\n",
				   sm_errstring(errno));
		return -1;
	}
	if (kstat_read(kc, ksp, NULL) < 0)
	{
		if (tTd(3, 1))
			sm_dprintf("getla: kstat_read(): %s\n",
				   sm_errstring(errno));
		return -1;
	}
	ksn = (kstat_named_t *) kstat_data_lookup(ksp, "avenrun_1min");
	la = ((double) ksn->value.ul + FSCALE/2) / FSCALE;
	/* kstat_close(kc); /o do not close for fast access */
	return la;
}

#endif /* LA_TYPE == LA_KSTAT */

#if LA_TYPE == LA_DEVSHORT

/*
**  Read /dev/table/avenrun for the load average.  This should contain
**  three shorts for the 1, 5, and 15 minute loads.  We only read the
**  first, since that's all we care about.
**
**	Intended for SCO OpenServer 5.
*/

# ifndef _PATH_AVENRUN
#  define _PATH_AVENRUN	"/dev/table/avenrun"
# endif /* ! _PATH_AVENRUN */

int
getla()
{
	static int afd = -1;
	short avenrun;
	int loadav;
	int r;

	errno = EBADF;

	if (afd == -1 || lseek(afd, 0L, SEEK_SET) == -1)
	{
		if (errno != EBADF)
			return -1;
		afd = open(_PATH_AVENRUN, O_RDONLY|O_SYNC);
		if (afd < 0)
		{
			sm_syslog(LOG_ERR, NOQID,
				"can't open %s: %s",
				_PATH_AVENRUN, sm_errstring(errno));
			return -1;
		}
	}

	r = read(afd, &avenrun, sizeof(avenrun));
	if (r != sizeof(avenrun))
	{
		sm_syslog(LOG_ERR, NOQID,
			"can't read %s: %s", _PATH_AVENRUN,
			r == -1 ? sm_errstring(errno) : "short read");
		return -1;
	}

	if (tTd(3, 5))
		sm_dprintf("getla: avenrun = %d\n", avenrun);
	loadav = (int) (avenrun + FSCALE/2) >> FSHIFT;
	if (tTd(3, 1))
		sm_dprintf("getla: %d\n", loadav);
	return loadav;
}

#endif /* LA_TYPE == LA_DEVSHORT */

#if LA_TYPE == LA_ALPHAOSF
struct rtentry;
struct mbuf;
# include <sys/table.h>

int
getla()
{
	int ave = 0;
	struct tbl_loadavg tab;

	if (table(TBL_LOADAVG, 0, &tab, 1, sizeof(tab)) == -1)
	{
		if (tTd(3, 1))
			sm_dprintf("getla: table %s\n", sm_errstring(errno));
		return -1;
	}

	if (tTd(3, 1))
		sm_dprintf("getla: scale = %d\n", tab.tl_lscale);

	if (tab.tl_lscale)
		ave = ((tab.tl_avenrun.l[2] + (tab.tl_lscale/2)) /
		       tab.tl_lscale);
	else
		ave = (int) (tab.tl_avenrun.d[2] + 0.5);

	if (tTd(3, 1))
		sm_dprintf("getla: %d\n", ave);

	return ave;
}

#endif /* LA_TYPE == LA_ALPHAOSF */

#if LA_TYPE == LA_PSET

int
getla()
{
	double avenrun[3];

	if (pset_getloadavg(PS_MYID, avenrun,
			    sizeof(avenrun) / sizeof(avenrun[0])) < 0)
	{
		if (tTd(3, 1))
			sm_dprintf("getla: pset_getloadavg failed: %s",
				   sm_errstring(errno));
		return -1;
	}
	if (tTd(3, 1))
		sm_dprintf("getla: %d\n", (int) (avenrun[0] +0.5));
	return ((int) (avenrun[0] + 0.5));
}

#endif /* LA_TYPE == LA_PSET */

#if LA_TYPE == LA_ZERO

int
getla()
{
	if (tTd(3, 1))
		sm_dprintf("getla: ZERO\n");
	return 0;
}

#endif /* LA_TYPE == LA_ZERO */

/*
 * Copyright 1989 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of M.I.T. not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  M.I.T. makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * M.I.T. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL M.I.T.
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:  Many and varied...
 */

/* Non Apollo stuff removed by Don Lewis 11/15/93 */
#ifndef lint
SM_UNUSED(static char  rcsid[]) = "@(#)$OrigId: getloadavg.c,v 1.16 1991/06/21 12:51:15 paul Exp $";
#endif /* ! lint */

#ifdef apollo
# undef volatile
# include <apollo/base.h>

/* ARGSUSED */
int getloadavg( call_data )
	caddr_t call_data;	/* pointer to (double) return value */
{
	double *avenrun = (double *) call_data;
	int i;
	status_$t      st;
	long loadav[3];

	proc1_$get_loadav(loadav, &st);
	*avenrun = loadav[0] / (double) (1 << 16);
	return 0;
}
#endif /* apollo */
/*
**  SM_GETLA -- get the current load average
**
**	Parameters:
**		none
**
**	Returns:
**		none
**
**	Side Effects:
**		Set CurrentLA to the current load average.
**		Set {load_avg} in GlobalMacros to the current load average.
*/

void
sm_getla()
{
	char labuf[8];

	CurrentLA = getla();
	(void) sm_snprintf(labuf, sizeof(labuf), "%d", CurrentLA);
	macdefine(&GlobalMacros, A_TEMP, macid("{load_avg}"), labuf);
}
/*
**  SHOULDQUEUE -- should this message be queued or sent?
**
**	Compares the message cost to the load average to decide.
**
**	Note: Do NOT change this API! It is documented in op.me
**		and theoretically the user can change this function...
**
**	Parameters:
**		pri -- the priority of the message in question.
**		ct -- the message creation time (unused, but see above).
**
**	Returns:
**		true -- if this message should be queued up for the
**			time being.
**		false -- if the load is low enough to send this message.
**
**	Side Effects:
**		none.
*/

/* ARGSUSED1 */
bool
shouldqueue(pri, ct)
	long pri;
	time_t ct;
{
	bool rval;
#if _FFR_MEMSTAT
	long memfree;
#endif /* _FFR_MEMSTAT */

	if (tTd(3, 30))
		sm_dprintf("shouldqueue: CurrentLA=%d, pri=%ld: ",
			CurrentLA, pri);

#if _FFR_MEMSTAT
	if (QueueLowMem > 0 &&
	    sm_memstat_get(MemoryResource, &memfree) >= 0 &&
	    memfree < QueueLowMem)
	{
		if (tTd(3, 30))
			sm_dprintf("true (memfree=%ld < QueueLowMem=%ld)\n",
				memfree, QueueLowMem);
		return true;
	}
#endif /* _FFR_MEMSTAT */
	if (CurrentLA < QueueLA)
	{
		if (tTd(3, 30))
			sm_dprintf("false (CurrentLA < QueueLA)\n");
		return false;
	}
	rval = pri > (QueueFactor / (CurrentLA - QueueLA + 1));
	if (tTd(3, 30))
		sm_dprintf("%s (by calculation)\n", rval ? "true" : "false");
	return rval;
}

/*
**  REFUSECONNECTIONS -- decide if connections should be refused
**
**	Parameters:
**		e -- the current envelope.
**		dn -- number of daemon.
**		active -- was this daemon actually active?
**
**	Returns:
**		true if incoming SMTP connections should be refused
**			(for now).
**		false if we should accept new work.
**
**	Side Effects:
**		Sets process title when it is rejecting connections.
*/

bool
refuseconnections(e, dn, active)
	ENVELOPE *e;
	int dn;
	bool active;
{
	static time_t lastconn[MAXDAEMONS];
	static int conncnt[MAXDAEMONS];
	static time_t firstrejtime[MAXDAEMONS];
	static time_t nextlogtime[MAXDAEMONS];
	int limit;
#if _FFR_MEMSTAT
	long memfree;
#endif /* _FFR_MEMSTAT */

#if XLA
	if (!xla_smtp_ok())
		return true;
#endif /* XLA */

	SM_ASSERT(dn >= 0);
	SM_ASSERT(dn < MAXDAEMONS);
	if (ConnRateThrottle > 0)
	{
		time_t now;

		now = curtime();
		if (active)
		{
			if (now != lastconn[dn])
			{
				lastconn[dn] = now;
				conncnt[dn] = 1;
			}
			else if (conncnt[dn]++ > ConnRateThrottle)
			{
#define D_MSG_CRT "deferring connections on daemon %s: %d per second"
				/* sleep to flatten out connection load */
				sm_setproctitle(true, e, D_MSG_CRT,
						Daemons[dn].d_name,
						ConnRateThrottle);
				if (LogLevel > 8)
					sm_syslog(LOG_INFO, NOQID, D_MSG_CRT,
						  Daemons[dn].d_name,
						  ConnRateThrottle);
				(void) sleep(1);
			}
		}
		else if (now != lastconn[dn])
			conncnt[dn] = 0;
	}


#if _FFR_MEMSTAT
	if (RefuseLowMem > 0 &&
	    sm_memstat_get(MemoryResource, &memfree) >= 0 &&
	    memfree < RefuseLowMem)
	{
# define R_MSG_LM "rejecting connections on daemon %s: free memory: %ld"
		sm_setproctitle(true, e, R_MSG_LM, Daemons[dn].d_name, memfree);
		if (LogLevel > 8)
			sm_syslog(LOG_NOTICE, NOQID, R_MSG_LM,
				Daemons[dn].d_name, memfree);
		return true;
	}
#endif /* _FFR_MEMSTAT */
	sm_getla();
	limit = (Daemons[dn].d_refuseLA != DPO_NOTSET) ?
		Daemons[dn].d_refuseLA : RefuseLA;
	if (limit > 0 && CurrentLA >= limit)
	{
		time_t now;

# define R_MSG_LA "rejecting connections on daemon %s: load average: %d"
# define R2_MSG_LA "have been rejecting connections on daemon %s for %s"
		sm_setproctitle(true, e, R_MSG_LA, Daemons[dn].d_name,
				CurrentLA);
		if (LogLevel > 8)
			sm_syslog(LOG_NOTICE, NOQID, R_MSG_LA,
				Daemons[dn].d_name, CurrentLA);
		now = curtime();
		if (firstrejtime[dn] == 0)
		{
			firstrejtime[dn] = now;
			nextlogtime[dn] = now + RejectLogInterval;
		}
		else if (nextlogtime[dn] < now)
		{
			sm_syslog(LOG_ERR, NOQID, R2_MSG_LA, Daemons[dn].d_name,
				  pintvl(now - firstrejtime[dn], true));
			nextlogtime[dn] = now + RejectLogInterval;
		}
		return true;
	}
	else
		firstrejtime[dn] = 0;

	limit = (Daemons[dn].d_delayLA != DPO_NOTSET) ?
		Daemons[dn].d_delayLA : DelayLA;
	if (limit > 0 && CurrentLA >= limit)
	{
		time_t now;
		static time_t log_delay = (time_t) 0;

# define MIN_DELAY_LOG	90	/* wait before logging this again */
# define D_MSG_LA "delaying connections on daemon %s: load average=%d >= %d"
		/* sleep to flatten out connection load */
		sm_setproctitle(true, e, D_MSG_LA, Daemons[dn].d_name,
				CurrentLA, limit);
		if (LogLevel > 8 && (now = curtime()) > log_delay)
		{
			sm_syslog(LOG_INFO, NOQID, D_MSG_LA,
				  Daemons[dn].d_name, CurrentLA, limit);
			log_delay = now + MIN_DELAY_LOG;
		}
		(void) sleep(1);
	}

	limit = (Daemons[dn].d_maxchildren != DPO_NOTSET) ?
		Daemons[dn].d_maxchildren : MaxChildren;
	if (limit > 0 && CurChildren >= limit)
	{
		proc_list_probe();
		if (CurChildren >= limit)
		{
#define R_MSG_CHILD "rejecting connections on daemon %s: %d children, max %d"
			sm_setproctitle(true, e, R_MSG_CHILD,
					Daemons[dn].d_name, CurChildren,
					limit);
			if (LogLevel > 8)
				sm_syslog(LOG_INFO, NOQID, R_MSG_CHILD,
					Daemons[dn].d_name, CurChildren,
					limit);
			return true;
		}
	}
	return false;
}

/*
**  SETPROCTITLE -- set process title for ps
**
**	Parameters:
**		fmt -- a printf style format string.
**		a, b, c -- possible parameters to fmt.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Clobbers argv of our main procedure so ps(1) will
**		display the title.
*/

#define SPT_NONE	0	/* don't use it at all */
#define SPT_REUSEARGV	1	/* cover argv with title information */
#define SPT_BUILTIN	2	/* use libc builtin */
#define SPT_PSTAT	3	/* use pstat(PSTAT_SETCMD, ...) */
#define SPT_PSSTRINGS	4	/* use PS_STRINGS->... */
#define SPT_SYSMIPS	5	/* use sysmips() supported by NEWS-OS 6 */
#define SPT_SCO		6	/* write kernel u. area */
#define SPT_CHANGEARGV	7	/* write our own strings into argv[] */

#ifndef SPT_TYPE
# define SPT_TYPE	SPT_REUSEARGV
#endif /* ! SPT_TYPE */


#if SPT_TYPE != SPT_NONE && SPT_TYPE != SPT_BUILTIN

# if SPT_TYPE == SPT_PSTAT
#  include <sys/pstat.h>
# endif /* SPT_TYPE == SPT_PSTAT */
# if SPT_TYPE == SPT_PSSTRINGS
#  include <machine/vmparam.h>
#  include <sys/exec.h>
#  ifndef PS_STRINGS	/* hmmmm....  apparently not available after all */
#   undef SPT_TYPE
#   define SPT_TYPE	SPT_REUSEARGV
#  else /* ! PS_STRINGS */
#   ifndef NKPDE			/* FreeBSD 2.0 */
#    define NKPDE 63
typedef unsigned int	*pt_entry_t;
#   endif /* ! NKPDE */
#  endif /* ! PS_STRINGS */
# endif /* SPT_TYPE == SPT_PSSTRINGS */

# if SPT_TYPE == SPT_PSSTRINGS || SPT_TYPE == SPT_CHANGEARGV
#  define SETPROC_STATIC	static
# else /* SPT_TYPE == SPT_PSSTRINGS || SPT_TYPE == SPT_CHANGEARGV */
#  define SETPROC_STATIC
# endif /* SPT_TYPE == SPT_PSSTRINGS || SPT_TYPE == SPT_CHANGEARGV */

# if SPT_TYPE == SPT_SYSMIPS
#  include <sys/sysmips.h>
#  include <sys/sysnews.h>
# endif /* SPT_TYPE == SPT_SYSMIPS */

# if SPT_TYPE == SPT_SCO
#  include <sys/immu.h>
#  include <sys/dir.h>
#  include <sys/user.h>
#  include <sys/fs/s5param.h>
#  if PSARGSZ > MAXLINE
#   define SPT_BUFSIZE	PSARGSZ
#  endif /* PSARGSZ > MAXLINE */
# endif /* SPT_TYPE == SPT_SCO */

# ifndef SPT_PADCHAR
#  define SPT_PADCHAR	' '
# endif /* ! SPT_PADCHAR */

#endif /* SPT_TYPE != SPT_NONE && SPT_TYPE != SPT_BUILTIN */

#ifndef SPT_BUFSIZE
# define SPT_BUFSIZE	MAXLINE
#endif /* ! SPT_BUFSIZE */

#if _FFR_SPT_ALIGN

/*
**  It looks like the Compaq Tru64 5.1A now aligns argv and envp to
**  64 bit alignment, so unless each piece of argv and envp is a multiple
**  of 8 bytes (including terminating NULL), initsetproctitle() won't use
**  any of the space beyond argv[0].  Be sure to set SPT_ALIGN_SIZE if
**  you use this FFR.
*/

# ifdef SPT_ALIGN_SIZE
#  define SPT_ALIGN(x, align)	(((((x) + SPT_ALIGN_SIZE) >> (align)) << (align)) - 1)
# else /* SPT_ALIGN_SIZE */
#  define SPT_ALIGN(x, align)	(x)
# endif /* SPT_ALIGN_SIZE */
#else /* _FFR_SPT_ALIGN */
# define SPT_ALIGN(x, align)	(x)
#endif /* _FFR_SPT_ALIGN */

/*
**  Pointers for setproctitle.
**	This allows "ps" listings to give more useful information.
*/

static char	**Argv = NULL;		/* pointer to argument vector */
static char	*LastArgv = NULL;	/* end of argv */
#if SPT_TYPE != SPT_BUILTIN
static void	setproctitle __P((const char *, ...));
#endif /* SPT_TYPE != SPT_BUILTIN */

void
initsetproctitle(argc, argv, envp)
	int argc;
	char **argv;
	char **envp;
{
	register int i;
	int align;
	extern char **environ;

	/*
	**  Move the environment so setproctitle can use the space at
	**  the top of memory.
	*/

	if (envp != NULL)
	{
		for (i = 0; envp[i] != NULL; i++)
			continue;
		environ = (char **) xalloc(sizeof(char *) * (i + 1));
		for (i = 0; envp[i] != NULL; i++)
			environ[i] = newstr(envp[i]);
		environ[i] = NULL;
	}

	/*
	**  Save start and extent of argv for setproctitle.
	*/

	Argv = argv;

	/*
	**  Determine how much space we can use for setproctitle.
	**  Use all contiguous argv and envp pointers starting at argv[0]
	*/

	align = -1;
# if _FFR_SPT_ALIGN
#  ifdef SPT_ALIGN_SIZE
	for (i = SPT_ALIGN_SIZE; i > 0; i >>= 1)
		align++;
#  endif /* SPT_ALIGN_SIZE */
# endif /* _FFR_SPT_ALIGN */

	for (i = 0; i < argc; i++)
	{
		if (i == 0 || LastArgv + 1 == argv[i])
			LastArgv = argv[i] + SPT_ALIGN(strlen(argv[i]), align);
	}
	for (i = 0; LastArgv != NULL && envp != NULL && envp[i] != NULL; i++)
	{
		if (LastArgv + 1 == envp[i])
			LastArgv = envp[i] + SPT_ALIGN(strlen(envp[i]), align);
	}
}

#if SPT_TYPE != SPT_BUILTIN

/*VARARGS1*/
static void
# ifdef __STDC__
setproctitle(const char *fmt, ...)
# else /* __STDC__ */
setproctitle(fmt, va_alist)
	const char *fmt;
	va_dcl
# endif /* __STDC__ */
{
# if SPT_TYPE != SPT_NONE
	register int i;
	register char *p;
	SETPROC_STATIC char buf[SPT_BUFSIZE];
	SM_VA_LOCAL_DECL
#  if SPT_TYPE == SPT_PSTAT
	union pstun pst;
#  endif /* SPT_TYPE == SPT_PSTAT */
#  if SPT_TYPE == SPT_SCO
	int j;
	off_t seek_off;
	static int kmem = -1;
	static pid_t kmempid = -1;
	struct user u;
#  endif /* SPT_TYPE == SPT_SCO */

	p = buf;

	/* print sendmail: heading for grep */
	(void) sm_strlcpy(p, "sendmail: ", SPACELEFT(buf, p));
	p += strlen(p);

	/* print the argument string */
	SM_VA_START(ap, fmt);
	(void) sm_vsnprintf(p, SPACELEFT(buf, p), fmt, ap);
	SM_VA_END(ap);

	i = (int) strlen(buf);
	if (i < 0)
		return;

#  if SPT_TYPE == SPT_PSTAT
	pst.pst_command = buf;
	pstat(PSTAT_SETCMD, pst, i, 0, 0);
#  endif /* SPT_TYPE == SPT_PSTAT */
#  if SPT_TYPE == SPT_PSSTRINGS
	PS_STRINGS->ps_nargvstr = 1;
	PS_STRINGS->ps_argvstr = buf;
#  endif /* SPT_TYPE == SPT_PSSTRINGS */
#  if SPT_TYPE == SPT_SYSMIPS
	sysmips(SONY_SYSNEWS, NEWS_SETPSARGS, buf);
#  endif /* SPT_TYPE == SPT_SYSMIPS */
#  if SPT_TYPE == SPT_SCO
	if (kmem < 0 || kmempid != CurrentPid)
	{
		if (kmem >= 0)
			(void) close(kmem);
		kmem = open(_PATH_KMEM, O_RDWR, 0);
		if (kmem < 0)
			return;
		if ((j = fcntl(kmem, F_GETFD, 0)) < 0 ||
		    fcntl(kmem, F_SETFD, j | FD_CLOEXEC) < 0)
		{
			(void) close(kmem);
			kmem = -1;
			return;
		}
		kmempid = CurrentPid;
	}
	buf[PSARGSZ - 1] = '\0';
	seek_off = UVUBLK + (off_t) u.u_psargs - (off_t) &u;
	if (lseek(kmem, (off_t) seek_off, SEEK_SET) == seek_off)
		(void) write(kmem, buf, PSARGSZ);
#  endif /* SPT_TYPE == SPT_SCO */
#  if SPT_TYPE == SPT_REUSEARGV
	if (LastArgv == NULL)
		return;

	if (i > LastArgv - Argv[0] - 2)
	{
		i = LastArgv - Argv[0] - 2;
		buf[i] = '\0';
	}
	(void) sm_strlcpy(Argv[0], buf, i + 1);
	p = &Argv[0][i];
	while (p < LastArgv)
		*p++ = SPT_PADCHAR;
	Argv[1] = NULL;
#  endif /* SPT_TYPE == SPT_REUSEARGV */
#  if SPT_TYPE == SPT_CHANGEARGV
	Argv[0] = buf;
	Argv[1] = 0;
#  endif /* SPT_TYPE == SPT_CHANGEARGV */
# endif /* SPT_TYPE != SPT_NONE */
}

#endif /* SPT_TYPE != SPT_BUILTIN */
/*
**  SM_SETPROCTITLE -- set process task and set process title for ps
**
**	Possibly set process status and call setproctitle() to
**	change the ps display.
**
**	Parameters:
**		status -- whether or not to store as process status
**		e -- the current envelope.
**		fmt -- a printf style format string.
**		a, b, c -- possible parameters to fmt.
**
**	Returns:
**		none.
*/

/*VARARGS3*/
void
#ifdef __STDC__
sm_setproctitle(bool status, ENVELOPE *e, const char *fmt, ...)
#else /* __STDC__ */
sm_setproctitle(status, e, fmt, va_alist)
	bool status;
	ENVELOPE *e;
	const char *fmt;
	va_dcl
#endif /* __STDC__ */
{
	char buf[SPT_BUFSIZE];
	SM_VA_LOCAL_DECL

	/* print the argument string */
	SM_VA_START(ap, fmt);
	(void) sm_vsnprintf(buf, sizeof(buf), fmt, ap);
	SM_VA_END(ap);

	if (status)
		proc_list_set(CurrentPid, buf);

	if (ProcTitlePrefix != NULL)
	{
		char prefix[SPT_BUFSIZE];

		expand(ProcTitlePrefix, prefix, sizeof(prefix), e);
		setproctitle("%s: %s", prefix, buf);
	}
	else
		setproctitle("%s", buf);
}
/*
**  WAITFOR -- wait for a particular process id.
**
**	Parameters:
**		pid -- process id to wait for.
**
**	Returns:
**		status of pid.
**		-1 if pid never shows up.
**
**	Side Effects:
**		none.
*/

int
waitfor(pid)
	pid_t pid;
{
	int st;
	pid_t i;

	do
	{
		errno = 0;
		i = sm_wait(&st);
		if (i > 0)
			proc_list_drop(i, st, NULL);
	} while ((i >= 0 || errno == EINTR) && i != pid);
	if (i < 0)
		return -1;
	return st;
}
/*
**  SM_WAIT -- wait
**
**	Parameters:
**		status -- pointer to status (return value)
**
**	Returns:
**		pid
*/

pid_t
sm_wait(status)
	int *status;
{
# ifdef WAITUNION
	union wait st;
# else /* WAITUNION */
	auto int st;
# endif /* WAITUNION */
	pid_t i;
# if defined(ISC_UNIX) || defined(_SCO_unix_)
	int savesig;
# endif /* defined(ISC_UNIX) || defined(_SCO_unix_) */

# if defined(ISC_UNIX) || defined(_SCO_unix_)
	savesig = sm_releasesignal(SIGCHLD);
# endif /* defined(ISC_UNIX) || defined(_SCO_unix_) */
	i = wait(&st);
# if defined(ISC_UNIX) || defined(_SCO_unix_)
	if (savesig > 0)
		sm_blocksignal(SIGCHLD);
# endif /* defined(ISC_UNIX) || defined(_SCO_unix_) */
# ifdef WAITUNION
	*status = st.w_status;
# else /* WAITUNION */
	*status = st;
# endif /* WAITUNION */
	return i;
}
/*
**  REAPCHILD -- pick up the body of my child, lest it become a zombie
**
**	Parameters:
**		sig -- the signal that got us here (unused).
**
**	Returns:
**		none.
**
**	Side Effects:
**		Picks up extant zombies.
**		Control socket exits may restart/shutdown daemon.
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

/* ARGSUSED0 */
SIGFUNC_DECL
reapchild(sig)
	int sig;
{
	int save_errno = errno;
	int st;
	pid_t pid;
# if HASWAITPID
	auto int status;
	int count;

	count = 0;
	while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
	{
		st = status;
		if (count++ > 1000)
			break;
# else /* HASWAITPID */
#  ifdef WNOHANG
	union wait status;

	while ((pid = wait3(&status, WNOHANG, (struct rusage *) NULL)) > 0)
	{
		st = status.w_status;
#  else /* WNOHANG */
	auto int status;

	/*
	**  Catch one zombie -- we will be re-invoked (we hope) if there
	**  are more.  Unreliable signals probably break this, but this
	**  is the "old system" situation -- waitpid or wait3 are to be
	**  strongly preferred.
	*/

	if ((pid = wait(&status)) > 0)
	{
		st = status;
#  endif /* WNOHANG */
# endif /* HASWAITPID */
		/* Drop PID and check if it was a control socket child */
		proc_list_drop(pid, st, NULL);
	}
	FIX_SYSV_SIGNAL(sig, reapchild);
	errno = save_errno;
	return SIGFUNC_RETURN;
}
/*
**  GETDTSIZE -- return number of file descriptors
**
**	Only on non-BSD systems
**
**	Parameters:
**		none
**
**	Returns:
**		size of file descriptor table
**
**	Side Effects:
**		none
*/

#ifdef SOLARIS
# include <sys/resource.h>
#endif /* SOLARIS */

int
getdtsize()
{
# ifdef RLIMIT_NOFILE
	struct rlimit rl;

	if (getrlimit(RLIMIT_NOFILE, &rl) >= 0)
		return rl.rlim_cur;
# endif /* RLIMIT_NOFILE */

# if HASGETDTABLESIZE
	return getdtablesize();
# else /* HASGETDTABLESIZE */
#  ifdef _SC_OPEN_MAX
	return sysconf(_SC_OPEN_MAX);
#  else /* _SC_OPEN_MAX */
	return NOFILE;
#  endif /* _SC_OPEN_MAX */
# endif /* HASGETDTABLESIZE */
}
/*
**  UNAME -- get the UUCP name of this system.
*/

#if !HASUNAME

int
uname(name)
	struct utsname *name;
{
	SM_FILE_T *file;
	char *n;

	name->nodename[0] = '\0';

	/* try /etc/whoami -- one line with the node name */
	if ((file = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, "/etc/whoami",
			       SM_IO_RDONLY, NULL)) != NULL)
	{
		(void) sm_io_fgets(file, SM_TIME_DEFAULT, name->nodename,
				   NODE_LENGTH + 1);
		(void) sm_io_close(file, SM_TIME_DEFAULT);
		n = strchr(name->nodename, '\n');
		if (n != NULL)
			*n = '\0';
		if (name->nodename[0] != '\0')
			return 0;
	}

	/* try /usr/include/whoami.h -- has a #define somewhere */
	if ((file = sm_io_open(SmFtStdio, SM_TIME_DEFAULT,
			       "/usr/include/whoami.h", SM_IO_RDONLY, NULL))
	    != NULL)
	{
		char buf[MAXLINE];

		while (sm_io_fgets(file, SM_TIME_DEFAULT,
				   buf, sizeof(buf)) >= 0)
		{
			if (sm_io_sscanf(buf, "#define sysname \"%*[^\"]\"",
					NODE_LENGTH, name->nodename) > 0)
				break;
		}
		(void) sm_io_close(file, SM_TIME_DEFAULT);
		if (name->nodename[0] != '\0')
			return 0;
	}

	return -1;
}
#endif /* !HASUNAME */
/*
**  INITGROUPS -- initialize groups
**
**	Stub implementation for System V style systems
*/

#if !HASINITGROUPS

initgroups(name, basegid)
	char *name;
	int basegid;
{
	return 0;
}

#endif /* !HASINITGROUPS */
/*
**  SETGROUPS -- set group list
**
**	Stub implementation for systems that don't have group lists
*/

#ifndef NGROUPS_MAX

int
setgroups(ngroups, grouplist)
	int ngroups;
	GIDSET_T grouplist[];
{
	return 0;
}

#endif /* ! NGROUPS_MAX */
/*
**  SETSID -- set session id (for non-POSIX systems)
*/

#if !HASSETSID

pid_t
setsid __P ((void))
{
#  ifdef TIOCNOTTY
	int fd;

	fd = open("/dev/tty", O_RDWR, 0);
	if (fd >= 0)
	{
		(void) ioctl(fd, TIOCNOTTY, (char *) 0);
		(void) close(fd);
	}
#  endif /* TIOCNOTTY */
#  ifdef SYS5SETPGRP
	return setpgrp();
#  else /* SYS5SETPGRP */
	return setpgid(0, CurrentPid);
#  endif /* SYS5SETPGRP */
}

#endif /* !HASSETSID */
/*
**  FSYNC -- dummy fsync
*/

#if NEEDFSYNC

fsync(fd)
	int fd;
{
# ifdef O_SYNC
	return fcntl(fd, F_SETFL, O_SYNC);
# else /* O_SYNC */
	/* nothing we can do */
	return 0;
# endif /* O_SYNC */
}

#endif /* NEEDFSYNC */
/*
**  DGUX_INET_ADDR -- inet_addr for DG/UX
**
**	Data General DG/UX version of inet_addr returns a struct in_addr
**	instead of a long.  This patches things.  Only needed on versions
**	prior to 5.4.3.
*/

#ifdef DGUX_5_4_2

# undef inet_addr

long
dgux_inet_addr(host)
	char *host;
{
	struct in_addr haddr;

	haddr = inet_addr(host);
	return haddr.s_addr;
}

#endif /* DGUX_5_4_2 */
/*
**  GETOPT -- for old systems or systems with bogus implementations
*/

#if !SM_CONF_GETOPT

/*
 * Copyright (c) 1985 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */


/*
**  this version hacked to add `atend' flag to allow state machine
**  to reset if invoked by the program to scan args for a 2nd time
*/

# if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)getopt.c	4.3 (Berkeley) 3/9/86";
# endif /* defined(LIBC_SCCS) && !defined(lint) */

/*
**  get option letter from argument vector
*/
# ifdef _CONVEX_SOURCE
extern int	optind, opterr, optopt;
extern char	*optarg;
# else /* _CONVEX_SOURCE */
int	opterr = 1;		/* if error message should be printed */
int	optind = 1;		/* index into parent argv vector */
int	optopt = 0;		/* character checked for validity */
char	*optarg = NULL;		/* argument associated with option */
# endif /* _CONVEX_SOURCE */

# define BADCH	(int)'?'
# define EMSG	""
# define tell(s)	if (opterr) \
			{sm_io_fputs(smioerr, SM_TIME_DEFAULT, *nargv); \
			(void) sm_io_fputs(smioerr, SM_TIME_DEFAULT, s); \
			(void) sm_io_putc(smioerr, SM_TIME_DEFAULT, optopt); \
			(void) sm_io_putc(smioerr, SM_TIME_DEFAULT, '\n'); \
			return BADCH;}

int
getopt(nargc,nargv,ostr)
	int		nargc;
	char *const	*nargv;
	const char	*ostr;
{
	static char	*place = EMSG;	/* option letter processing */
	static char	atend = 0;
	register char	*oli = NULL;	/* option letter list index */

	if (atend) {
		atend = 0;
		place = EMSG;
	}
	if(!*place) {			/* update scanning pointer */
		if (optind >= nargc || *(place = nargv[optind]) != '-' || !*++place) {
			atend++;
			return -1;
		}
		if (*place == '-') {	/* found "--" */
			++optind;
			atend++;
			return -1;
		}
	}				/* option letter okay? */
	if ((optopt = (int)*place++) == (int)':' || !(oli = strchr(ostr,optopt))) {
		if (!*place) ++optind;
		tell(": illegal option -- ");
	}
	if (oli && *++oli != ':') {		/* don't need argument */
		optarg = NULL;
		if (!*place) ++optind;
	}
	else {				/* need an argument */
		if (*place) optarg = place;	/* no white space */
		else if (nargc <= ++optind) {	/* no arg */
			place = EMSG;
			tell(": option requires an argument -- ");
		}
		else optarg = nargv[optind];	/* white space */
		place = EMSG;
		++optind;
	}
	return optopt;			/* dump back option letter */
}

#endif /* !SM_CONF_GETOPT */
/*
**  USERSHELLOK -- tell if a user's shell is ok for unrestricted use
**
**	Parameters:
**		user -- the name of the user we are checking.
**		shell -- the user's shell from /etc/passwd
**
**	Returns:
**		true -- if it is ok to use this for unrestricted access.
**		false -- if the shell is restricted.
*/

#if !HASGETUSERSHELL

# ifndef _PATH_SHELLS
#  define _PATH_SHELLS	"/etc/shells"
# endif /* ! _PATH_SHELLS */

# if defined(_AIX3) || defined(_AIX4)
#  include <userconf.h>
#  if _AIX4 >= 40200
#   include <userpw.h>
#  endif /* _AIX4 >= 40200 */
#  include <usersec.h>
# endif /* defined(_AIX3) || defined(_AIX4) */

static char	*DefaultUserShells[] =
{
	"/bin/sh",		/* standard shell */
# ifdef MPE
	"/SYS/PUB/CI",
# else /* MPE */
	"/usr/bin/sh",
	"/bin/csh",		/* C shell */
	"/usr/bin/csh",
# endif /* MPE */
# ifdef __hpux
#  ifdef V4FS
	"/usr/bin/rsh",		/* restricted Bourne shell */
	"/usr/bin/ksh",		/* Korn shell */
	"/usr/bin/rksh",	/* restricted Korn shell */
	"/usr/bin/pam",
	"/usr/bin/keysh",	/* key shell (extended Korn shell) */
	"/usr/bin/posix/sh",
#  else /* V4FS */
	"/bin/rsh",		/* restricted Bourne shell */
	"/bin/ksh",		/* Korn shell */
	"/bin/rksh",		/* restricted Korn shell */
	"/bin/pam",
	"/usr/bin/keysh",	/* key shell (extended Korn shell) */
	"/bin/posix/sh",
	"/sbin/sh",
#  endif /* V4FS */
# endif /* __hpux */
# if defined(_AIX3) || defined(_AIX4)
	"/bin/ksh",		/* Korn shell */
	"/usr/bin/ksh",
	"/bin/tsh",		/* trusted shell */
	"/usr/bin/tsh",
	"/bin/bsh",		/* Bourne shell */
	"/usr/bin/bsh",
# endif /* defined(_AIX3) || defined(_AIX4) */
# if defined(__svr4__) || defined(__svr5__)
	"/bin/ksh",		/* Korn shell */
	"/usr/bin/ksh",
# endif /* defined(__svr4__) || defined(__svr5__) */
# ifdef sgi
	"/sbin/sh",		/* SGI's shells really live in /sbin */
	"/usr/bin/sh",
	"/sbin/bsh",		/* classic Bourne shell */
	"/bin/bsh",
	"/usr/bin/bsh",
	"/sbin/csh",		/* standard csh */
	"/bin/csh",
	"/usr/bin/csh",
	"/sbin/jsh",		/* classic Bourne shell w/ job control*/
	"/bin/jsh",
	"/usr/bin/jsh",
	"/bin/ksh",		/* Korn shell */
	"/sbin/ksh",
	"/usr/bin/ksh",
	"/sbin/tcsh",		/* Extended csh */
	"/bin/tcsh",
	"/usr/bin/tcsh",
# endif /* sgi */
	NULL
};

#endif /* !HASGETUSERSHELL */

#define WILDCARD_SHELL	"/SENDMAIL/ANY/SHELL/"

bool
usershellok(user, shell)
	char *user;
	char *shell;
{
# if HASGETUSERSHELL
	register char *p;
	extern char *getusershell();

	if (shell == NULL || shell[0] == '\0' || wordinclass(user, 't') ||
	    ConfigLevel <= 1)
		return true;

	setusershell();
	while ((p = getusershell()) != NULL)
		if (strcmp(p, shell) == 0 || strcmp(p, WILDCARD_SHELL) == 0)
			break;
	endusershell();
	return p != NULL;
# else /* HASGETUSERSHELL */
#  if USEGETCONFATTR
	auto char *v;
#  endif /* USEGETCONFATTR */
	register SM_FILE_T *shellf;
	char buf[MAXLINE];

	if (shell == NULL || shell[0] == '\0' || wordinclass(user, 't') ||
	    ConfigLevel <= 1)
		return true;

#  if USEGETCONFATTR
	/*
	**  Naturally IBM has a "better" idea.....
	**
	**	What a crock.  This interface isn't documented, it is
	**	considered part of the security library (-ls), and it
	**	only works if you are running as root (since the list
	**	of valid shells is obviously a source of great concern).
	**	I recommend that you do NOT define USEGETCONFATTR,
	**	especially since you are going to have to set up an
	**	/etc/shells anyhow to handle the cases where getconfattr
	**	fails.
	*/

	if (getconfattr(SC_SYS_LOGIN, SC_SHELLS, &v, SEC_LIST) == 0 && v != NULL)
	{
		while (*v != '\0')
		{
			if (strcmp(v, shell) == 0 || strcmp(v, WILDCARD_SHELL) == 0)
				return true;
			v += strlen(v) + 1;
		}
		return false;
	}
#  endif /* USEGETCONFATTR */

	shellf = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, _PATH_SHELLS,
			    SM_IO_RDONLY, NULL);
	if (shellf == NULL)
	{
		/* no /etc/shells; see if it is one of the std shells */
		char **d;

		if (errno != ENOENT && LogLevel > 3)
			sm_syslog(LOG_ERR, NOQID,
				  "usershellok: cannot open %s: %s",
				  _PATH_SHELLS, sm_errstring(errno));

		for (d = DefaultUserShells; *d != NULL; d++)
		{
			if (strcmp(shell, *d) == 0)
				return true;
		}
		return false;
	}

	while (sm_io_fgets(shellf, SM_TIME_DEFAULT, buf, sizeof(buf)) >= 0)
	{
		register char *p, *q;

		p = buf;
		while (*p != '\0' && *p != '#' && *p != '/')
			p++;
		if (*p == '#' || *p == '\0')
			continue;
		q = p;
		while (*p != '\0' && *p != '#' && !(isascii(*p) && isspace(*p)))
			p++;
		*p = '\0';
		if (strcmp(shell, q) == 0 || strcmp(WILDCARD_SHELL, q) == 0)
		{
			(void) sm_io_close(shellf, SM_TIME_DEFAULT);
			return true;
		}
	}
	(void) sm_io_close(shellf, SM_TIME_DEFAULT);
	return false;
# endif /* HASGETUSERSHELL */
}
/*
**  FREEDISKSPACE -- see how much free space is on the queue filesystem
**
**	Only implemented if you have statfs.
**
**	Parameters:
**		dir -- the directory in question.
**		bsize -- a variable into which the filesystem
**			block size is stored.
**
**	Returns:
**		The number of blocks free on the queue filesystem.
**		-1 if the statfs call fails.
**
**	Side effects:
**		Puts the filesystem block size into bsize.
*/

/* statfs types */
# define SFS_NONE	0	/* no statfs implementation */
# define SFS_USTAT	1	/* use ustat */
# define SFS_4ARGS	2	/* use four-argument statfs call */
# define SFS_VFS	3	/* use <sys/vfs.h> implementation */
# define SFS_MOUNT	4	/* use <sys/mount.h> implementation */
# define SFS_STATFS	5	/* use <sys/statfs.h> implementation */
# define SFS_STATVFS	6	/* use <sys/statvfs.h> implementation */

# ifndef SFS_TYPE
#  define SFS_TYPE	SFS_NONE
# endif /* ! SFS_TYPE */

# if SFS_TYPE == SFS_USTAT
#  include <ustat.h>
# endif /* SFS_TYPE == SFS_USTAT */
# if SFS_TYPE == SFS_4ARGS || SFS_TYPE == SFS_STATFS
#  include <sys/statfs.h>
# endif /* SFS_TYPE == SFS_4ARGS || SFS_TYPE == SFS_STATFS */
# if SFS_TYPE == SFS_VFS
#  include <sys/vfs.h>
# endif /* SFS_TYPE == SFS_VFS */
# if SFS_TYPE == SFS_MOUNT
#  include <sys/mount.h>
# endif /* SFS_TYPE == SFS_MOUNT */
# if SFS_TYPE == SFS_STATVFS
#  include <sys/statvfs.h>
# endif /* SFS_TYPE == SFS_STATVFS */

long
freediskspace(dir, bsize)
	const char *dir;
	long *bsize;
{
# if SFS_TYPE == SFS_NONE
	if (bsize != NULL)
		*bsize = 4096L;

	/* assume free space is plentiful */
	return (long) LONG_MAX;
# else /* SFS_TYPE == SFS_NONE */
#  if SFS_TYPE == SFS_USTAT
	struct ustat fs;
	struct stat statbuf;
#   define FSBLOCKSIZE	DEV_BSIZE
#   define SFS_BAVAIL	f_tfree
#  else /* SFS_TYPE == SFS_USTAT */
#   if defined(ultrix)
	struct fs_data fs;
#    define SFS_BAVAIL	fd_bfreen
#    define FSBLOCKSIZE	1024L
#   else /* defined(ultrix) */
#    if SFS_TYPE == SFS_STATVFS
	struct statvfs fs;
#     define FSBLOCKSIZE	fs.f_frsize
#    else /* SFS_TYPE == SFS_STATVFS */
	struct statfs fs;
#     define FSBLOCKSIZE	fs.f_bsize
#    endif /* SFS_TYPE == SFS_STATVFS */
#   endif /* defined(ultrix) */
#  endif /* SFS_TYPE == SFS_USTAT */
#  ifndef SFS_BAVAIL
#   define SFS_BAVAIL f_bavail
#  endif /* ! SFS_BAVAIL */

#  if SFS_TYPE == SFS_USTAT
	if (stat(dir, &statbuf) == 0 && ustat(statbuf.st_dev, &fs) == 0)
#  else /* SFS_TYPE == SFS_USTAT */
#   if SFS_TYPE == SFS_4ARGS
	if (statfs(dir, &fs, sizeof(fs), 0) == 0)
#   else /* SFS_TYPE == SFS_4ARGS */
#    if SFS_TYPE == SFS_STATVFS
	if (statvfs(dir, &fs) == 0)
#    else /* SFS_TYPE == SFS_STATVFS */
#     if defined(ultrix)
	if (statfs(dir, &fs) > 0)
#     else /* defined(ultrix) */
	if (statfs(dir, &fs) == 0)
#     endif /* defined(ultrix) */
#    endif /* SFS_TYPE == SFS_STATVFS */
#   endif /* SFS_TYPE == SFS_4ARGS */
#  endif /* SFS_TYPE == SFS_USTAT */
	{
		if (bsize != NULL)
			*bsize = FSBLOCKSIZE;
		if (fs.SFS_BAVAIL <= 0)
			return 0;
		else if (fs.SFS_BAVAIL > LONG_MAX)
			return (long) LONG_MAX;
		else
			return (long) fs.SFS_BAVAIL;
	}
	return -1;
# endif /* SFS_TYPE == SFS_NONE */
}
/*
**  ENOUGHDISKSPACE -- is there enough free space on the queue file systems?
**
**	Parameters:
**		msize -- the size to check against.  If zero, we don't yet
**		know how big the message will be, so just check for
**		a "reasonable" amount.
**		e -- envelope, or NULL -- controls logging
**
**	Returns:
**		true if in every queue group there is at least one
**		queue directory whose file system contains enough free space.
**		false otherwise.
**
**	Side Effects:
**		If there is not enough disk space and e != NULL
**		then sm_syslog is called.
*/

bool
enoughdiskspace(msize, e)
	long msize;
	ENVELOPE *e;
{
	int i;

#if _FFR_TESTS
	if (tTd(4, 101))
		return false;
#endif /* _FFR_TESTS */
	if (MinBlocksFree <= 0 && msize <= 0)
	{
		if (tTd(4, 80))
			sm_dprintf("enoughdiskspace: no threshold\n");
		return true;
	}

	filesys_update();
	for (i = 0; i < NumQueue; ++i)
	{
		if (pickqdir(Queue[i], msize, e) < 0)
			return false;
	}
	return true;
}
/*
**  TRANSIENTERROR -- tell if an error code indicates a transient failure
**
**	This looks at an errno value and tells if this is likely to
**	go away if retried later.
**
**	Parameters:
**		err -- the errno code to classify.
**
**	Returns:
**		true if this is probably transient.
**		false otherwise.
*/

bool
transienterror(err)
	int err;
{
	switch (err)
	{
	  case EIO:			/* I/O error */
	  case ENXIO:			/* Device not configured */
	  case EAGAIN:			/* Resource temporarily unavailable */
	  case ENOMEM:			/* Cannot allocate memory */
	  case ENODEV:			/* Operation not supported by device */
	  case ENFILE:			/* Too many open files in system */
	  case EMFILE:			/* Too many open files */
	  case ENOSPC:			/* No space left on device */
	  case ETIMEDOUT:		/* Connection timed out */
#ifdef ESTALE
	  case ESTALE:			/* Stale NFS file handle */
#endif /* ESTALE */
#ifdef ENETDOWN
	  case ENETDOWN:		/* Network is down */
#endif /* ENETDOWN */
#ifdef ENETUNREACH
	  case ENETUNREACH:		/* Network is unreachable */
#endif /* ENETUNREACH */
#ifdef ENETRESET
	  case ENETRESET:		/* Network dropped connection on reset */
#endif /* ENETRESET */
#ifdef ECONNABORTED
	  case ECONNABORTED:		/* Software caused connection abort */
#endif /* ECONNABORTED */
#ifdef ECONNRESET
	  case ECONNRESET:		/* Connection reset by peer */
#endif /* ECONNRESET */
#ifdef ENOBUFS
	  case ENOBUFS:			/* No buffer space available */
#endif /* ENOBUFS */
#ifdef ESHUTDOWN
	  case ESHUTDOWN:		/* Can't send after socket shutdown */
#endif /* ESHUTDOWN */
#ifdef ECONNREFUSED
	  case ECONNREFUSED:		/* Connection refused */
#endif /* ECONNREFUSED */
#ifdef EHOSTDOWN
	  case EHOSTDOWN:		/* Host is down */
#endif /* EHOSTDOWN */
#ifdef EHOSTUNREACH
	  case EHOSTUNREACH:		/* No route to host */
#endif /* EHOSTUNREACH */
#ifdef EDQUOT
	  case EDQUOT:			/* Disc quota exceeded */
#endif /* EDQUOT */
#ifdef EPROCLIM
	  case EPROCLIM:		/* Too many processes */
#endif /* EPROCLIM */
#ifdef EUSERS
	  case EUSERS:			/* Too many users */
#endif /* EUSERS */
#ifdef EDEADLK
	  case EDEADLK:			/* Resource deadlock avoided */
#endif /* EDEADLK */
#ifdef EISCONN
	  case EISCONN:			/* Socket already connected */
#endif /* EISCONN */
#ifdef EINPROGRESS
	  case EINPROGRESS:		/* Operation now in progress */
#endif /* EINPROGRESS */
#ifdef EALREADY
	  case EALREADY:		/* Operation already in progress */
#endif /* EALREADY */
#ifdef EADDRINUSE
	  case EADDRINUSE:		/* Address already in use */
#endif /* EADDRINUSE */
#ifdef EADDRNOTAVAIL
	  case EADDRNOTAVAIL:		/* Can't assign requested address */
#endif /* EADDRNOTAVAIL */
#ifdef ETXTBSY
	  case ETXTBSY:			/* (Apollo) file locked */
#endif /* ETXTBSY */
#if defined(ENOSR) && (!defined(ENOBUFS) || (ENOBUFS != ENOSR))
	  case ENOSR:			/* Out of streams resources */
#endif /* defined(ENOSR) && (!defined(ENOBUFS) || (ENOBUFS != ENOSR)) */
#ifdef ENOLCK
	  case ENOLCK:			/* No locks available */
#endif /* ENOLCK */
	  case E_SM_OPENTIMEOUT:	/* PSEUDO: open timed out */
		return true;
	}

	/* nope, must be permanent */
	return false;
}
/*
**  LOCKFILE -- lock a file using flock or (shudder) fcntl locking
**
**	Parameters:
**		fd -- the file descriptor of the file.
**		filename -- the file name (for error messages).
**		ext -- the filename extension.
**		type -- type of the lock.  Bits can be:
**			LOCK_EX -- exclusive lock.
**			LOCK_NB -- non-blocking.
**			LOCK_UN -- unlock.
**
**	Returns:
**		true if the lock was acquired.
**		false otherwise.
*/

bool
lockfile(fd, filename, ext, type)
	int fd;
	char *filename;
	char *ext;
	int type;
{
	int i;
	int save_errno;
# if !HASFLOCK
	int action;
	struct flock lfd;

	if (ext == NULL)
		ext = "";

	memset(&lfd, '\0', sizeof(lfd));
	if (bitset(LOCK_UN, type))
		lfd.l_type = F_UNLCK;
	else if (bitset(LOCK_EX, type))
		lfd.l_type = F_WRLCK;
	else
		lfd.l_type = F_RDLCK;

	if (bitset(LOCK_NB, type))
		action = F_SETLK;
	else
		action = F_SETLKW;

	if (tTd(55, 60))
		sm_dprintf("lockfile(%s%s, action=%d, type=%d): ",
			filename, ext, action, lfd.l_type);

	while ((i = fcntl(fd, action, &lfd)) < 0 && errno == EINTR)
		continue;
	if (i >= 0)
	{
		if (tTd(55, 60))
			sm_dprintf("SUCCESS\n");
		return true;
	}
	save_errno = errno;

	if (tTd(55, 60))
		sm_dprintf("(%s) ", sm_errstring(save_errno));

	/*
	**  On SunOS, if you are testing using -oQ/tmp/mqueue or
	**  -oA/tmp/aliases or anything like that, and /tmp is mounted
	**  as type "tmp" (that is, served from swap space), the
	**  previous fcntl will fail with "Invalid argument" errors.
	**  Since this is fairly common during testing, we will assume
	**  that this indicates that the lock is successfully grabbed.
	*/

	if (save_errno == EINVAL)
	{
		if (tTd(55, 60))
			sm_dprintf("SUCCESS\n");
		return true;
	}

	if (!bitset(LOCK_NB, type) ||
	    (save_errno != EACCES && save_errno != EAGAIN))
	{
		int omode = fcntl(fd, F_GETFL, 0);
		uid_t euid = geteuid();

		errno = save_errno;
		syserr("cannot lockf(%s%s, fd=%d, type=%o, omode=%o, euid=%ld)",
		       filename, ext, fd, type, omode, (long) euid);
		dumpfd(fd, true, true);
	}
# else /* !HASFLOCK */
	if (ext == NULL)
		ext = "";

	if (tTd(55, 60))
		sm_dprintf("lockfile(%s%s, type=%o): ", filename, ext, type);

	while ((i = flock(fd, type)) < 0 && errno == EINTR)
		continue;
	if (i >= 0)
	{
		if (tTd(55, 60))
			sm_dprintf("SUCCESS\n");
		return true;
	}
	save_errno = errno;

	if (tTd(55, 60))
		sm_dprintf("(%s) ", sm_errstring(save_errno));

	if (!bitset(LOCK_NB, type) || save_errno != EWOULDBLOCK)
	{
		int omode = fcntl(fd, F_GETFL, 0);
		uid_t euid = geteuid();

		errno = save_errno;
		syserr("cannot flock(%s%s, fd=%d, type=%o, omode=%o, euid=%ld)",
			filename, ext, fd, type, omode, (long) euid);
		dumpfd(fd, true, true);
	}
# endif /* !HASFLOCK */
	if (tTd(55, 60))
		sm_dprintf("FAIL\n");
	errno = save_errno;
	return false;
}
/*
**  CHOWNSAFE -- tell if chown is "safe" (executable only by root)
**
**	Unfortunately, given that we can't predict other systems on which
**	a remote mounted (NFS) filesystem will be mounted, the answer is
**	almost always that this is unsafe.
**
**	Note also that many operating systems have non-compliant
**	implementations of the _POSIX_CHOWN_RESTRICTED variable and the
**	fpathconf() routine.  According to IEEE 1003.1-1990, if
**	_POSIX_CHOWN_RESTRICTED is defined and not equal to -1, then
**	no non-root process can give away the file.  However, vendors
**	don't take NFS into account, so a comfortable value of
**	_POSIX_CHOWN_RESTRICTED tells us nothing.
**
**	Also, some systems (e.g., IRIX 6.2) return 1 from fpathconf()
**	even on files where chown is not restricted.  Many systems get
**	this wrong on NFS-based filesystems (that is, they say that chown
**	is restricted [safe] on NFS filesystems where it may not be, since
**	other systems can access the same filesystem and do file giveaway;
**	only the NFS server knows for sure!)  Hence, it is important to
**	get the value of SAFENFSPATHCONF correct -- it should be defined
**	_only_ after testing (see test/t_pathconf.c) a system on an unsafe
**	NFS-based filesystem to ensure that you can get meaningful results.
**	If in doubt, assume unsafe!
**
**	You may also need to tweak IS_SAFE_CHOWN -- it should be a
**	condition indicating whether the return from pathconf indicates
**	that chown is safe (typically either > 0 or >= 0 -- there isn't
**	even any agreement about whether a zero return means that a file
**	is or is not safe).  It defaults to "> 0".
**
**	If the parent directory is safe (writable only by owner back
**	to the root) then we can relax slightly and trust fpathconf
**	in more circumstances.  This is really a crock -- if this is an
**	NFS mounted filesystem then we really know nothing about the
**	underlying implementation.  However, most systems pessimize and
**	return an error (EINVAL or EOPNOTSUPP) on NFS filesystems, which
**	we interpret as unsafe, as we should.  Thus, this heuristic gets
**	us into a possible problem only on systems that have a broken
**	pathconf implementation and which are also poorly configured
**	(have :include: files in group- or world-writable directories).
**
**	Parameters:
**		fd -- the file descriptor to check.
**		safedir -- set if the parent directory is safe.
**
**	Returns:
**		true -- if the chown(2) operation is "safe" -- that is,
**			only root can chown the file to an arbitrary user.
**		false -- if an arbitrary user can give away a file.
*/

#ifndef IS_SAFE_CHOWN
# define IS_SAFE_CHOWN	> 0
#endif /* ! IS_SAFE_CHOWN */

bool
chownsafe(fd, safedir)
	int fd;
	bool safedir;
{
# if (!defined(_POSIX_CHOWN_RESTRICTED) || _POSIX_CHOWN_RESTRICTED != -1) && \
    (defined(_PC_CHOWN_RESTRICTED) || defined(_GNU_TYPES_H))
	int rval;

	/* give the system administrator a chance to override */
	if (bitnset(DBS_ASSUMESAFECHOWN, DontBlameSendmail))
		return true;

	/*
	**  Some systems (e.g., SunOS) seem to have the call and the
	**  #define _PC_CHOWN_RESTRICTED, but don't actually implement
	**  the call.  This heuristic checks for that.
	*/

	errno = 0;
	rval = fpathconf(fd, _PC_CHOWN_RESTRICTED);
#  if SAFENFSPATHCONF
	return errno == 0 && rval IS_SAFE_CHOWN;
#  else /* SAFENFSPATHCONF */
	return safedir && errno == 0 && rval IS_SAFE_CHOWN;
#  endif /* SAFENFSPATHCONF */
# else /* (!defined(_POSIX_CHOWN_RESTRICTED) || _POSIX_CHOWN_RESTRICTED != -1) && ... */
	return bitnset(DBS_ASSUMESAFECHOWN, DontBlameSendmail);
# endif /* (!defined(_POSIX_CHOWN_RESTRICTED) || _POSIX_CHOWN_RESTRICTED != -1) && ... */
}
/*
**  RESETLIMITS -- reset system controlled resource limits
**
**	This is to avoid denial-of-service attacks
**
**	Parameters:
**		none
**
**	Returns:
**		none
*/

#if HASSETRLIMIT
# ifdef RLIMIT_NEEDS_SYS_TIME_H
#  include <sm/time.h>
# endif /* RLIMIT_NEEDS_SYS_TIME_H */
# include <sys/resource.h>
#endif /* HASSETRLIMIT */

void
resetlimits()
{
#if HASSETRLIMIT
	struct rlimit lim;

	lim.rlim_cur = lim.rlim_max = RLIM_INFINITY;
	(void) setrlimit(RLIMIT_CPU, &lim);
	(void) setrlimit(RLIMIT_FSIZE, &lim);
# ifdef RLIMIT_NOFILE
	lim.rlim_cur = lim.rlim_max = FD_SETSIZE;
	(void) setrlimit(RLIMIT_NOFILE, &lim);
# endif /* RLIMIT_NOFILE */
#else /* HASSETRLIMIT */
# if HASULIMIT
	(void) ulimit(2, 0x3fffff);
	(void) ulimit(4, FD_SETSIZE);
# endif /* HASULIMIT */
#endif /* HASSETRLIMIT */
	errno = 0;
}
/*
**  SETVENDOR -- process vendor code from V configuration line
**
**	Parameters:
**		vendor -- string representation of vendor.
**
**	Returns:
**		true -- if ok.
**		false -- if vendor code could not be processed.
**
**	Side Effects:
**		It is reasonable to set mode flags here to tweak
**		processing in other parts of the code if necessary.
**		For example, if you are a vendor that uses $%y to
**		indicate YP lookups, you could enable that here.
*/

bool
setvendor(vendor)
	char *vendor;
{
	if (sm_strcasecmp(vendor, "Berkeley") == 0)
	{
		VendorCode = VENDOR_BERKELEY;
		return true;
	}

	/* add vendor extensions here */

#ifdef SUN_EXTENSIONS
	if (sm_strcasecmp(vendor, "Sun") == 0)
	{
		VendorCode = VENDOR_SUN;
		return true;
	}
#endif /* SUN_EXTENSIONS */
#ifdef DEC
	if (sm_strcasecmp(vendor, "Digital") == 0)
	{
		VendorCode = VENDOR_DEC;
		return true;
	}
#endif /* DEC */

#if defined(VENDOR_NAME) && defined(VENDOR_CODE)
	if (sm_strcasecmp(vendor, VENDOR_NAME) == 0)
	{
		VendorCode = VENDOR_CODE;
		return true;
	}
#endif /* defined(VENDOR_NAME) && defined(VENDOR_CODE) */

	return false;
}
/*
**  GETVENDOR -- return vendor name based on vendor code
**
**	Parameters:
**		vendorcode -- numeric representation of vendor.
**
**	Returns:
**		string containing vendor name.
*/

char *
getvendor(vendorcode)
	int vendorcode;
{
#if defined(VENDOR_NAME) && defined(VENDOR_CODE)
	/*
	**  Can't have the same switch case twice so need to
	**  handle VENDOR_CODE outside of switch.  It might
	**  match one of the existing VENDOR_* codes.
	*/

	if (vendorcode == VENDOR_CODE)
		return VENDOR_NAME;
#endif /* defined(VENDOR_NAME) && defined(VENDOR_CODE) */

	switch (vendorcode)
	{
	  case VENDOR_BERKELEY:
		return "Berkeley";

	  case VENDOR_SUN:
		return "Sun";

	  case VENDOR_HP:
		return "HP";

	  case VENDOR_IBM:
		return "IBM";

	  case VENDOR_SENDMAIL:
		return "Sendmail";

	  default:
		return "Unknown";
	}
}
/*
**  VENDOR_PRE_DEFAULTS, VENDOR_POST_DEFAULTS -- set vendor-specific defaults
**
**	Vendor_pre_defaults is called before reading the configuration
**	file; vendor_post_defaults is called immediately after.
**
**	Parameters:
**		e -- the global environment to initialize.
**
**	Returns:
**		none.
*/

#if SHARE_V1
int	DefShareUid;	/* default share uid to run as -- unused??? */
#endif /* SHARE_V1 */

void
vendor_pre_defaults(e)
	ENVELOPE *e;
{
#if SHARE_V1
	/* OTHERUID is defined in shares.h, do not be alarmed */
	DefShareUid = OTHERUID;
#endif /* SHARE_V1 */
#if defined(SUN_EXTENSIONS) && defined(SUN_DEFAULT_VALUES)
	sun_pre_defaults(e);
#endif /* defined(SUN_EXTENSIONS) && defined(SUN_DEFAULT_VALUES) */
#ifdef apollo
	/*
	**  stupid domain/os can't even open
	**  /etc/mail/sendmail.cf without this
	*/

	sm_setuserenv("ISP", NULL);
	sm_setuserenv("SYSTYPE", NULL);
#endif /* apollo */
}


void
vendor_post_defaults(e)
	ENVELOPE *e;
{
#ifdef __QNX__
	/* Makes sure the SOCK environment variable remains */
	sm_setuserenv("SOCK", NULL);
#endif /* __QNX__ */
#if defined(SUN_EXTENSIONS) && defined(SUN_DEFAULT_VALUES)
	sun_post_defaults(e);
#endif /* defined(SUN_EXTENSIONS) && defined(SUN_DEFAULT_VALUES) */
}
/*
**  VENDOR_DAEMON_SETUP -- special vendor setup needed for daemon mode
*/

void
vendor_daemon_setup(e)
	ENVELOPE *e;
{
#if HASSETLOGIN
	(void) setlogin(RunAsUserName);
#endif /* HASSETLOGIN */
#if SECUREWARE
	if (getluid() != -1)
	{
		usrerr("Daemon cannot have LUID");
		finis(false, true, EX_USAGE);
	}
#endif /* SECUREWARE */
}
/*
**  VENDOR_SET_UID -- do setup for setting a user id
**
**	This is called when we are still root.
**
**	Parameters:
**		uid -- the uid we are about to become.
**
**	Returns:
**		none.
*/

void
vendor_set_uid(uid)
	UID_T uid;
{
	/*
	**  We need to setup the share groups (lnodes)
	**  and add auditing information (luid's)
	**  before we loose our ``root''ness.
	*/
#if SHARE_V1
	if (setupshares(uid, syserr) != 0)
		syserr("Unable to set up shares");
#endif /* SHARE_V1 */
#if SECUREWARE
	(void) setup_secure(uid);
#endif /* SECUREWARE */
}
/*
**  VALIDATE_CONNECTION -- check connection for rationality
**
**	If the connection is rejected, this routine should log an
**	appropriate message -- but should never issue any SMTP protocol.
**
**	Parameters:
**		sap -- a pointer to a SOCKADDR naming the peer.
**		hostname -- the name corresponding to sap.
**		e -- the current envelope.
**
**	Returns:
**		error message from rejection.
**		NULL if not rejected.
*/

#if TCPWRAPPERS
# include <tcpd.h>

/* tcpwrappers does no logging, but you still have to declare these -- ugh */
int	allow_severity	= LOG_INFO;
int	deny_severity	= LOG_NOTICE;
#endif /* TCPWRAPPERS */

char *
validate_connection(sap, hostname, e)
	SOCKADDR *sap;
	char *hostname;
	ENVELOPE *e;
{
#if TCPWRAPPERS
	char *host;
	char *addr;
	extern int hosts_ctl();
#endif /* TCPWRAPPERS */

	if (tTd(48, 3))
		sm_dprintf("validate_connection(%s, %s)\n",
			hostname, anynet_ntoa(sap));

	connection_rate_check(sap, e);
	if (rscheck("check_relay", hostname, anynet_ntoa(sap), e,
		    RSF_RMCOMM|RSF_COUNT, 3, NULL, NOQID, NULL, NULL) != EX_OK)
	{
		static char reject[BUFSIZ*2];
		extern char MsgBuf[];

		if (tTd(48, 4))
			sm_dprintf("  ... validate_connection: BAD (rscheck)\n");

		if (strlen(MsgBuf) >= 3)
			(void) sm_strlcpy(reject, MsgBuf, sizeof(reject));
		else
			(void) sm_strlcpy(reject, "Access denied", sizeof(reject));

		return reject;
	}

#if TCPWRAPPERS
	if (hostname[0] == '[' && hostname[strlen(hostname) - 1] == ']')
		host = "unknown";
	else
		host = hostname;
	addr = anynet_ntoa(sap);

# if NETINET6
	/* TCP/Wrappers don't want the IPv6: protocol label */
	if (addr != NULL && sm_strncasecmp(addr, "IPv6:", 5) == 0)
		addr += 5;
# endif /* NETINET6 */

	if (!hosts_ctl("sendmail", host, addr, STRING_UNKNOWN))
	{
		if (tTd(48, 4))
			sm_dprintf("  ... validate_connection: BAD (tcpwrappers)\n");
		if (LogLevel > 3)
			sm_syslog(LOG_NOTICE, e->e_id,
				  "tcpwrappers (%s, %s) rejection",
				  host, addr);
		return "Access denied";
	}
#endif /* TCPWRAPPERS */
	if (tTd(48, 4))
		sm_dprintf("  ... validate_connection: OK\n");
	return NULL;
}

/*
**  STRTOL -- convert string to long integer
**
**	For systems that don't have it in the C library.
**
**	This is taken verbatim from the 4.4-Lite C library.
*/

#if NEEDSTRTOL

# if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)strtol.c	8.1 (Berkeley) 6/4/93";
# endif /* defined(LIBC_SCCS) && !defined(lint) */

/*
**  Convert a string to a long integer.
**
**  Ignores `locale' stuff.  Assumes that the upper and lower case
**  alphabets and digits are each contiguous.
*/

long
strtol(nptr, endptr, base)
	const char *nptr;
	char **endptr;
	register int base;
{
	register const char *s = nptr;
	register unsigned long acc;
	register int c;
	register unsigned long cutoff;
	register int neg = 0, any, cutlim;

	/*
	**  Skip white space and pick up leading +/- sign if any.
	**  If base is 0, allow 0x for hex and 0 for octal, else
	**  assume decimal; if base is already 16, allow 0x.
	*/
	do {
		c = *s++;
	} while (isascii(c) && isspace(c));
	if (c == '-') {
		neg = 1;
		c = *s++;
	} else if (c == '+')
		c = *s++;
	if ((base == 0 || base == 16) &&
	    c == '0' && (*s == 'x' || *s == 'X')) {
		c = s[1];
		s += 2;
		base = 16;
	}
	if (base == 0)
		base = c == '0' ? 8 : 10;

	/*
	**  Compute the cutoff value between legal numbers and illegal
	**  numbers.  That is the largest legal value, divided by the
	**  base.  An input number that is greater than this value, if
	**  followed by a legal input character, is too big.  One that
	**  is equal to this value may be valid or not; the limit
	**  between valid and invalid numbers is then based on the last
	**  digit.  For instance, if the range for longs is
	**  [-2147483648..2147483647] and the input base is 10,
	**  cutoff will be set to 214748364 and cutlim to either
	**  7 (neg==0) or 8 (neg==1), meaning that if we have accumulated
	**  a value > 214748364, or equal but the next digit is > 7 (or 8),
	**  the number is too big, and we will return a range error.
	**
	**  Set any if any `digits' consumed; make it negative to indicate
	**  overflow.
	*/
	cutoff = neg ? -(unsigned long) LONG_MIN : LONG_MAX;
	cutlim = cutoff % (unsigned long) base;
	cutoff /= (unsigned long) base;
	for (acc = 0, any = 0;; c = *s++) {
		if (isascii(c) && isdigit(c))
			c -= '0';
		else if (isascii(c) && isalpha(c))
			c -= isupper(c) ? 'A' - 10 : 'a' - 10;
		else
			break;
		if (c >= base)
			break;
		if (any < 0 || acc > cutoff || acc == cutoff && c > cutlim)
			any = -1;
		else {
			any = 1;
			acc *= base;
			acc += c;
		}
	}
	if (any < 0) {
		acc = neg ? LONG_MIN : LONG_MAX;
		errno = ERANGE;
	} else if (neg)
		acc = -acc;
	if (endptr != 0)
		*endptr = (char *)(any ? s - 1 : nptr);
	return acc;
}

#endif /* NEEDSTRTOL */
/*
**  STRSTR -- find first substring in string
**
**	Parameters:
**		big -- the big (full) string.
**		little -- the little (sub) string.
**
**	Returns:
**		A pointer to the first instance of little in big.
**		big if little is the null string.
**		NULL if little is not contained in big.
*/

#if NEEDSTRSTR

char *
strstr(big, little)
	char *big;
	char *little;
{
	register char *p = big;
	int l;

	if (*little == '\0')
		return big;
	l = strlen(little);

	while ((p = strchr(p, *little)) != NULL)
	{
		if (strncmp(p, little, l) == 0)
			return p;
		p++;
	}
	return NULL;
}

#endif /* NEEDSTRSTR */
/*
**  SM_GETHOSTBY{NAME,ADDR} -- compatibility routines for gethostbyXXX
**
**	Some operating systems have weird problems with the gethostbyXXX
**	routines.  For example, Solaris versions at least through 2.3
**	don't properly deliver a canonical h_name field.  This tries to
**	work around these problems.
**
**	Support IPv6 as well as IPv4.
*/

#if NETINET6 && NEEDSGETIPNODE

# ifndef AI_DEFAULT
#  define AI_DEFAULT	0	/* dummy */
# endif /* ! AI_DEFAULT */
# ifndef AI_ADDRCONFIG
#  define AI_ADDRCONFIG	0	/* dummy */
# endif /* ! AI_ADDRCONFIG */
# ifndef AI_V4MAPPED
#  define AI_V4MAPPED	0	/* dummy */
# endif /* ! AI_V4MAPPED */
# ifndef AI_ALL
#  define AI_ALL	0	/* dummy */
# endif /* ! AI_ALL */

static struct hostent *
sm_getipnodebyname(name, family, flags, err)
	const char *name;
	int family;
	int flags;
	int *err;
{
	struct hostent *h;
# if HAS_GETHOSTBYNAME2

	h = gethostbyname2(name, family);
	if (h == NULL)
		*err = h_errno;
	return h;

# else /* HAS_GETHOSTBYNAME2 */
	bool resv6 = true;

	if (family == AF_INET6)
	{
		/* From RFC2133, section 6.1 */
		resv6 = bitset(RES_USE_INET6, _res.options);
		_res.options |= RES_USE_INET6;
	}
	SM_SET_H_ERRNO(0);
	h = gethostbyname(name);
	if (!resv6)
		_res.options &= ~RES_USE_INET6;

	/* the function is supposed to return only the requested family */
	if (h != NULL && h->h_addrtype != family)
	{
#  if NETINET6
		freehostent(h);
#  endif /* NETINET6 */
		h = NULL;
		*err = NO_DATA;
	}
	else
		*err = h_errno;
	return h;
# endif /* HAS_GETHOSTBYNAME2 */
}

static struct hostent *
sm_getipnodebyaddr(addr, len, family, err)
	const void *addr;
	size_t len;
	int family;
	int *err;
{
	struct hostent *h;

	SM_SET_H_ERRNO(0);
	h = gethostbyaddr(addr, len, family);
	*err = h_errno;
	return h;
}

void
freehostent(h)
	struct hostent *h;
{
	/*
	**  Stub routine -- if they don't have getipnodeby*(),
	**  they probably don't have the free routine either.
	*/

	return;
}
#endif /* NETINET6 && NEEDSGETIPNODE */

struct hostent *
sm_gethostbyname(name, family)
	char *name;
	int family;
{
	int save_errno;
	struct hostent *h = NULL;
#if (SOLARIS > 10000 && SOLARIS < 20400) || (defined(SOLARIS) && SOLARIS < 204) || (defined(sony_news) && defined(__svr4))
# if SOLARIS == 20300 || SOLARIS == 203
	static struct hostent hp;
	static char buf[1000];
	extern struct hostent *_switch_gethostbyname_r();

	if (tTd(61, 10))
		sm_dprintf("_switch_gethostbyname_r(%s)... ", name);
	h = _switch_gethostbyname_r(name, &hp, buf, sizeof(buf), &h_errno);
	save_errno = errno;
# else /* SOLARIS == 20300 || SOLARIS == 203 */
	extern struct hostent *__switch_gethostbyname();

	if (tTd(61, 10))
		sm_dprintf("__switch_gethostbyname(%s)... ", name);
	h = __switch_gethostbyname(name);
	save_errno = errno;
# endif /* SOLARIS == 20300 || SOLARIS == 203 */
#else /* (SOLARIS > 10000 && SOLARIS < 20400) || (defined(SOLARIS) && SOLARIS < 204) || (defined(sony_news) && defined(__svr4)) */
	int nmaps;
# if NETINET6
#  ifndef SM_IPNODEBYNAME_FLAGS
    /* For IPv4-mapped addresses, use: AI_DEFAULT|AI_ALL */
#   define SM_IPNODEBYNAME_FLAGS	AI_ADDRCONFIG
#  endif /* SM_IPNODEBYNAME_FLAGS */

	int flags = SM_IPNODEBYNAME_FLAGS;
	int err;
# endif /* NETINET6 */
	char *maptype[MAXMAPSTACK];
	short mapreturn[MAXMAPACTIONS];
	char hbuf[MAXNAME];

	if (tTd(61, 10))
		sm_dprintf("sm_gethostbyname(%s, %d)... ", name, family);

# if NETINET6
#  if ADDRCONFIG_IS_BROKEN
	flags &= ~AI_ADDRCONFIG;
#  endif /* ADDRCONFIG_IS_BROKEN */
	h = sm_getipnodebyname(name, family, flags, &err);
	SM_SET_H_ERRNO(err);
# else /* NETINET6 */
	h = gethostbyname(name);
# endif /* NETINET6 */

	save_errno = errno;
	if (h == NULL)
	{
		if (tTd(61, 10))
			sm_dprintf("failure\n");

		nmaps = switch_map_find("hosts", maptype, mapreturn);
		while (--nmaps >= 0)
		{
			if (strcmp(maptype[nmaps], "nis") == 0 ||
			    strcmp(maptype[nmaps], "files") == 0)
				break;
		}

		if (nmaps >= 0)
		{
			/* try short name */
			if (strlen(name) > sizeof(hbuf) - 1)
			{
				errno = save_errno;
				return NULL;
			}
			(void) sm_strlcpy(hbuf, name, sizeof(hbuf));
			(void) shorten_hostname(hbuf);

			/* if it hasn't been shortened, there's no point */
			if (strcmp(hbuf, name) != 0)
			{
				if (tTd(61, 10))
					sm_dprintf("sm_gethostbyname(%s, %d)... ",
					       hbuf, family);

# if NETINET6
				h = sm_getipnodebyname(hbuf, family, flags, &err);
				SM_SET_H_ERRNO(err);
				save_errno = errno;
# else /* NETINET6 */
				h = gethostbyname(hbuf);
				save_errno = errno;
# endif /* NETINET6 */
			}
		}
	}
#endif /* (SOLARIS > 10000 && SOLARIS < 20400) || (defined(SOLARIS) && SOLARIS < 204) || (defined(sony_news) && defined(__svr4)) */

	/* the function is supposed to return only the requested family */
	if (h != NULL && h->h_addrtype != family)
	{
# if NETINET6
		freehostent(h);
# endif /* NETINET6 */
		h = NULL;
		SM_SET_H_ERRNO(NO_DATA);
	}

	if (tTd(61, 10))
	{
		if (h == NULL)
			sm_dprintf("failure\n");
		else
		{
			sm_dprintf("%s\n", h->h_name);
			if (tTd(61, 11))
			{
				struct in_addr ia;
				size_t i;
#if NETINET6
				struct in6_addr ia6;
				char buf6[INET6_ADDRSTRLEN];
#endif /* NETINET6 */

				if (h->h_aliases != NULL)
					for (i = 0; h->h_aliases[i] != NULL;
					     i++)
						sm_dprintf("\talias: %s\n",
							h->h_aliases[i]);
				for (i = 0; h->h_addr_list[i] != NULL; i++)
				{
					char *addr;

					addr = NULL;
#if NETINET6
					if (h->h_addrtype == AF_INET6)
					{
						memmove(&ia6, h->h_addr_list[i],
							IN6ADDRSZ);
						addr = anynet_ntop(&ia6,
							buf6, sizeof(buf6));
					}
					else
#endif /* NETINET6 */
					/* "else" in #if code above */
					{
						memmove(&ia, h->h_addr_list[i],
							INADDRSZ);
						addr = (char *) inet_ntoa(ia);
					}
					if (addr != NULL)
						sm_dprintf("\taddr: %s\n", addr);
				}
			}
		}
	}
	errno = save_errno;
	return h;
}

struct hostent *
sm_gethostbyaddr(addr, len, type)
	char *addr;
	int len;
	int type;
{
	struct hostent *hp;

#if NETINET6
	if (type == AF_INET6 &&
	    IN6_IS_ADDR_UNSPECIFIED((struct in6_addr *) addr))
	{
		/* Avoid reverse lookup for IPv6 unspecified address */
		SM_SET_H_ERRNO(HOST_NOT_FOUND);
		return NULL;
	}
#endif /* NETINET6 */

#if (SOLARIS > 10000 && SOLARIS < 20400) || (defined(SOLARIS) && SOLARIS < 204)
# if SOLARIS == 20300 || SOLARIS == 203
	{
		static struct hostent he;
		static char buf[1000];
		extern struct hostent *_switch_gethostbyaddr_r();

		hp = _switch_gethostbyaddr_r(addr, len, type, &he,
					     buf, sizeof(buf), &h_errno);
	}
# else /* SOLARIS == 20300 || SOLARIS == 203 */
	{
		extern struct hostent *__switch_gethostbyaddr();

		hp = __switch_gethostbyaddr(addr, len, type);
	}
# endif /* SOLARIS == 20300 || SOLARIS == 203 */
#else /* (SOLARIS > 10000 && SOLARIS < 20400) || (defined(SOLARIS) && SOLARIS < 204) */
# if NETINET6
	{
		int err;

		hp = sm_getipnodebyaddr(addr, len, type, &err);
		SM_SET_H_ERRNO(err);
	}
# else /* NETINET6 */
	hp = gethostbyaddr(addr, len, type);
# endif /* NETINET6 */
#endif /* (SOLARIS > 10000 && SOLARIS < 20400) || (defined(SOLARIS) && SOLARIS < 204) */
	return hp;
}
/*
**  SM_GETPW{NAM,UID} -- wrapper for getpwnam and getpwuid
*/

struct passwd *
sm_getpwnam(user)
	char *user;
{
#ifdef _AIX4
	extern struct passwd *_getpwnam_shadow(const char *, const int);

	return _getpwnam_shadow(user, 0);
#else /* _AIX4 */
	return getpwnam(user);
#endif /* _AIX4 */
}

struct passwd *
sm_getpwuid(uid)
	UID_T uid;
{
#if defined(_AIX4) && 0
	extern struct passwd *_getpwuid_shadow(const int, const int);

	return _getpwuid_shadow(uid,0);
#else /* defined(_AIX4) && 0 */
	return getpwuid(uid);
#endif /* defined(_AIX4) && 0 */
}
/*
**  SECUREWARE_SETUP_SECURE -- Convex SecureWare setup
**
**	Set up the trusted computing environment for C2 level security
**	under SecureWare.
**
**	Parameters:
**		uid -- uid of the user to initialize in the TCB
**
**	Returns:
**		none
**
**	Side Effects:
**		Initialized the user in the trusted computing base
*/

#if SECUREWARE

# include <sys/security.h>
# include <prot.h>

void
secureware_setup_secure(uid)
	UID_T uid;
{
	int rc;

	if (getluid() != -1)
		return;

	if ((rc = set_secure_info(uid)) != SSI_GOOD_RETURN)
	{
		switch (rc)
		{
		  case SSI_NO_PRPW_ENTRY:
			syserr("No protected passwd entry, uid = %d",
			       (int) uid);
			break;

		  case SSI_LOCKED:
			syserr("Account has been disabled, uid = %d",
			       (int) uid);
			break;

		  case SSI_RETIRED:
			syserr("Account has been retired, uid = %d",
			       (int) uid);
			break;

		  case SSI_BAD_SET_LUID:
			syserr("Could not set LUID, uid = %d", (int) uid);
			break;

		  case SSI_BAD_SET_PRIVS:
			syserr("Could not set kernel privs, uid = %d",
			       (int) uid);

		  default:
			syserr("Unknown return code (%d) from set_secure_info(%d)",
				rc, (int) uid);
			break;
		}
		finis(false, true, EX_NOPERM);
	}
}
#endif /* SECUREWARE */
/*
**  ADD_HOSTNAMES -- Add a hostname to class 'w' based on IP address
**
**	Add hostnames to class 'w' based on the IP address read from
**	the network interface.
**
**	Parameters:
**		sa -- a pointer to a SOCKADDR containing the address
**
**	Returns:
**		0 if successful, -1 if host lookup fails.
*/

static int
add_hostnames(sa)
	SOCKADDR *sa;
{
	struct hostent *hp;
	char **ha;
	char hnb[MAXHOSTNAMELEN];

	/* lookup name with IP address */
	switch (sa->sa.sa_family)
	{
#if NETINET
	  case AF_INET:
		hp = sm_gethostbyaddr((char *) &sa->sin.sin_addr,
				      sizeof(sa->sin.sin_addr),
				      sa->sa.sa_family);
		break;
#endif /* NETINET */

#if NETINET6
	  case AF_INET6:
		hp = sm_gethostbyaddr((char *) &sa->sin6.sin6_addr,
				      sizeof(sa->sin6.sin6_addr),
				      sa->sa.sa_family);
		break;
#endif /* NETINET6 */

	  default:
		/* Give warning about unsupported family */
		if (LogLevel > 3)
			sm_syslog(LOG_WARNING, NOQID,
				  "Unsupported address family %d: %.100s",
				  sa->sa.sa_family, anynet_ntoa(sa));
		return -1;
	}

	if (hp == NULL)
	{
		int save_errno = errno;

		if (LogLevel > 3 &&
#if NETINET && defined(IN_LINKLOCAL)
		    !(sa->sa.sa_family == AF_INET &&
		      IN_LINKLOCAL(ntohl(sa->sin.sin_addr.s_addr))) &&
#endif /* NETINET && defined(IN_LINKLOCAL) */
#if NETINET6
		    !(sa->sa.sa_family == AF_INET6 &&
		      IN6_IS_ADDR_LINKLOCAL(&sa->sin6.sin6_addr)) &&
#endif /* NETINET6 */
		    true)
			sm_syslog(LOG_WARNING, NOQID,
				  "gethostbyaddr(%.100s) failed: %d",
				  anynet_ntoa(sa),
#if NAMED_BIND
				  h_errno
#else /* NAMED_BIND */
				  -1
#endif /* NAMED_BIND */
				 );
		errno = save_errno;
		return -1;
	}

	/* save its cname */
	if (!wordinclass((char *) hp->h_name, 'w'))
	{
		setclass('w', (char *) hp->h_name);
		if (tTd(0, 4))
			sm_dprintf("\ta.k.a.: %s\n", hp->h_name);

		if (sm_snprintf(hnb, sizeof(hnb), "[%s]", hp->h_name) <
								sizeof(hnb)
		    && !wordinclass((char *) hnb, 'w'))
			setclass('w', hnb);
	}
	else
	{
		if (tTd(0, 43))
			sm_dprintf("\ta.k.a.: %s (already in $=w)\n", hp->h_name);
	}

	/* save all it aliases name */
	for (ha = hp->h_aliases; ha != NULL && *ha != NULL; ha++)
	{
		if (!wordinclass(*ha, 'w'))
		{
			setclass('w', *ha);
			if (tTd(0, 4))
				sm_dprintf("\ta.k.a.: %s\n", *ha);
			if (sm_snprintf(hnb, sizeof(hnb),
				     "[%s]", *ha) < sizeof(hnb) &&
			    !wordinclass((char *) hnb, 'w'))
				setclass('w', hnb);
		}
		else
		{
			if (tTd(0, 43))
				sm_dprintf("\ta.k.a.: %s (already in $=w)\n",
					*ha);
		}
	}
#if NETINET6
	freehostent(hp);
#endif /* NETINET6 */
	return 0;
}
/*
**  LOAD_IF_NAMES -- load interface-specific names into $=w
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Loads $=w with the names of all the interfaces.
*/

#if !NETINET
# define SIOCGIFCONF_IS_BROKEN	1 /* XXX */
#endif /* !NETINET */

#if defined(SIOCGIFCONF) && !SIOCGIFCONF_IS_BROKEN
struct rtentry;
struct mbuf;
# ifndef SUNOS403
#  include <sm/time.h>
# endif /* ! SUNOS403 */
# if (_AIX4 >= 40300) && !defined(_NET_IF_H)
#  undef __P
# endif /* (_AIX4 >= 40300) && !defined(_NET_IF_H) */
# include <net/if.h>
#endif /* defined(SIOCGIFCONF) && !SIOCGIFCONF_IS_BROKEN */

void
load_if_names()
{
# if NETINET6 && defined(SIOCGLIFCONF)
#  ifdef __hpux

    /*
    **  Unfortunately, HP has changed all of the structures,
    **  making life difficult for implementors.
    */

#   define lifconf	if_laddrconf
#   define lifc_len	iflc_len
#   define lifc_buf	iflc_buf
#   define lifreq	if_laddrreq
#   define lifr_addr	iflr_addr
#   define lifr_name	iflr_name
#   define lifr_flags	iflr_flags
#   define ss_family	sa_family
#   undef SIOCGLIFNUM
#  endif /* __hpux */

	int s;
	int i;
	size_t len;
	int numifs;
	char *buf;
	struct lifconf lifc;
#  ifdef SIOCGLIFNUM
	struct lifnum lifn;
#  endif /* SIOCGLIFNUM */

	s = socket(InetMode, SOCK_DGRAM, 0);
	if (s == -1)
		return;

	/* get the list of known IP address from the kernel */
#  ifdef __hpux
	i = ioctl(s, SIOCGIFNUM, (char *) &numifs);
#  endif /* __hpux */
#  ifdef SIOCGLIFNUM
	lifn.lifn_family = AF_UNSPEC;
	lifn.lifn_flags = 0;
	i = ioctl(s, SIOCGLIFNUM, (char *)&lifn);
	numifs = lifn.lifn_count;
#  endif /* SIOCGLIFNUM */

#  if defined(__hpux) || defined(SIOCGLIFNUM)
	if (i < 0)
	{
		/* can't get number of interfaces -- fall back */
		if (tTd(0, 4))
			sm_dprintf("SIOCGLIFNUM failed: %s\n",
				   sm_errstring(errno));
		numifs = -1;
	}
	else if (tTd(0, 42))
		sm_dprintf("system has %d interfaces\n", numifs);
	if (numifs < 0)
#  endif /* defined(__hpux) || defined(SIOCGLIFNUM) */
		numifs = MAXINTERFACES;

	if (numifs <= 0)
	{
		(void) close(s);
		return;
	}

	len = lifc.lifc_len = numifs * sizeof(struct lifreq);
	buf = lifc.lifc_buf = xalloc(lifc.lifc_len);
#  ifndef __hpux
	lifc.lifc_family = AF_UNSPEC;
	lifc.lifc_flags = 0;
#  endif /* ! __hpux */
	if (ioctl(s, SIOCGLIFCONF, (char *)&lifc) < 0)
	{
		if (tTd(0, 4))
			sm_dprintf("SIOCGLIFCONF failed: %s\n",
				   sm_errstring(errno));
		(void) close(s);
		sm_free(buf);
		return;
	}

	/* scan the list of IP address */
	if (tTd(0, 40))
		sm_dprintf("scanning for interface specific names, lifc_len=%ld\n",
			   (long) len);

	for (i = 0; i < len && i >= 0; )
	{
		int flags;
		struct lifreq *ifr = (struct lifreq *)&buf[i];
		SOCKADDR *sa = (SOCKADDR *) &ifr->lifr_addr;
		int af = ifr->lifr_addr.ss_family;
		char *addr;
		char *name;
		struct in6_addr ia6;
		struct in_addr ia;
#  ifdef SIOCGLIFFLAGS
		struct lifreq ifrf;
#  endif /* SIOCGLIFFLAGS */
		char ip_addr[256];
		char buf6[INET6_ADDRSTRLEN];

		/*
		**  We must close and recreate the socket each time
		**  since we don't know what type of socket it is now
		**  (each status function may change it).
		*/

		(void) close(s);

		s = socket(af, SOCK_DGRAM, 0);
		if (s == -1)
		{
			sm_free(buf); /* XXX */
			return;
		}

		/*
		**  If we don't have a complete ifr structure,
		**  don't try to use it.
		*/

		if ((len - i) < sizeof(*ifr))
			break;

#  ifdef BSD4_4_SOCKADDR
		if (sa->sa.sa_len > sizeof(ifr->lifr_addr))
			i += sizeof(ifr->lifr_name) + sa->sa.sa_len;
		else
#  endif /* BSD4_4_SOCKADDR */
#  ifdef DEC
			/* fix for IPv6  size differences */
			i += sizeof(ifr->ifr_name) +
			     max(sizeof(ifr->ifr_addr), ifr->ifr_addr.sa_len);
#   else /* DEC */
			i += sizeof(*ifr);
#   endif /* DEC */

		if (tTd(0, 20))
			sm_dprintf("%s\n", anynet_ntoa(sa));

		if (af != AF_INET && af != AF_INET6)
			continue;

#  ifdef SIOCGLIFFLAGS
		memset(&ifrf, '\0', sizeof(struct lifreq));
		(void) sm_strlcpy(ifrf.lifr_name, ifr->lifr_name,
				  sizeof(ifrf.lifr_name));
		if (ioctl(s, SIOCGLIFFLAGS, (char *) &ifrf) < 0)
		{
			if (tTd(0, 4))
				sm_dprintf("SIOCGLIFFLAGS failed: %s\n",
					   sm_errstring(errno));
			continue;
		}

		name = ifr->lifr_name;
		flags = ifrf.lifr_flags;

		if (tTd(0, 41))
			sm_dprintf("\tflags: %lx\n", (unsigned long) flags);

		if (!bitset(IFF_UP, flags))
			continue;
#  endif /* SIOCGLIFFLAGS */

		ip_addr[0] = '\0';

		/* extract IP address from the list*/
		switch (af)
		{
		  case AF_INET6:
			SETV6LOOPBACKADDRFOUND(*sa);
#  ifdef __KAME__
			/* convert into proper scoped address */
			if ((IN6_IS_ADDR_LINKLOCAL(&sa->sin6.sin6_addr) ||
			     IN6_IS_ADDR_SITELOCAL(&sa->sin6.sin6_addr)) &&
			    sa->sin6.sin6_scope_id == 0)
			{
				struct in6_addr *ia6p;

				ia6p = &sa->sin6.sin6_addr;
				sa->sin6.sin6_scope_id = ntohs(ia6p->s6_addr[3] |
							       ((unsigned int)ia6p->s6_addr[2] << 8));
				ia6p->s6_addr[2] = ia6p->s6_addr[3] = 0;
			}
#  endif /* __KAME__ */
			ia6 = sa->sin6.sin6_addr;
			if (IN6_IS_ADDR_UNSPECIFIED(&ia6))
			{
				addr = anynet_ntop(&ia6, buf6, sizeof(buf6));
				message("WARNING: interface %s is UP with %s address",
					name, addr == NULL ? "(NULL)" : addr);
				continue;
			}

			/* save IP address in text from */
			addr = anynet_ntop(&ia6, buf6, sizeof(buf6));
			if (addr != NULL)
				(void) sm_snprintf(ip_addr, sizeof(ip_addr),
						   "[%.*s]",
						   (int) sizeof(ip_addr) - 3,
						   addr);
			break;

		  case AF_INET:
			ia = sa->sin.sin_addr;
			if (ia.s_addr == INADDR_ANY ||
			    ia.s_addr == INADDR_NONE)
			{
				message("WARNING: interface %s is UP with %s address",
					name, inet_ntoa(ia));
				continue;
			}

			/* save IP address in text from */
			(void) sm_snprintf(ip_addr, sizeof(ip_addr), "[%.*s]",
					(int) sizeof(ip_addr) - 3, inet_ntoa(ia));
			break;
		}

		if (*ip_addr == '\0')
			continue;

		if (!wordinclass(ip_addr, 'w'))
		{
			setclass('w', ip_addr);
			if (tTd(0, 4))
				sm_dprintf("\ta.k.a.: %s\n", ip_addr);
		}

#  ifdef SIOCGLIFFLAGS
		/* skip "loopback" interface "lo" */
		if (DontProbeInterfaces == DPI_SKIPLOOPBACK &&
		    bitset(IFF_LOOPBACK, flags))
			continue;
#  endif /* SIOCGLIFFLAGS */
		(void) add_hostnames(sa);
	}
	sm_free(buf); /* XXX */
	(void) close(s);
# else /* NETINET6 && defined(SIOCGLIFCONF) */
#  if defined(SIOCGIFCONF) && !SIOCGIFCONF_IS_BROKEN
	int s;
	int i;
	struct ifconf ifc;
	int numifs;

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == -1)
		return;

	/* get the list of known IP address from the kernel */
#   if defined(SIOCGIFNUM) && !SIOCGIFNUM_IS_BROKEN
	if (ioctl(s, SIOCGIFNUM, (char *) &numifs) < 0)
	{
		/* can't get number of interfaces -- fall back */
		if (tTd(0, 4))
			sm_dprintf("SIOCGIFNUM failed: %s\n",
				   sm_errstring(errno));
		numifs = -1;
	}
	else if (tTd(0, 42))
		sm_dprintf("system has %d interfaces\n", numifs);
	if (numifs < 0)
#   endif /* defined(SIOCGIFNUM) && !SIOCGIFNUM_IS_BROKEN */
		numifs = MAXINTERFACES;

	if (numifs <= 0)
	{
		(void) close(s);
		return;
	}
	ifc.ifc_len = numifs * sizeof(struct ifreq);
	ifc.ifc_buf = xalloc(ifc.ifc_len);
	if (ioctl(s, SIOCGIFCONF, (char *)&ifc) < 0)
	{
		if (tTd(0, 4))
			sm_dprintf("SIOCGIFCONF failed: %s\n",
				   sm_errstring(errno));
		(void) close(s);
		return;
	}

	/* scan the list of IP address */
	if (tTd(0, 40))
		sm_dprintf("scanning for interface specific names, ifc_len=%d\n",
			ifc.ifc_len);

	for (i = 0; i < ifc.ifc_len && i >= 0; )
	{
		int af;
		struct ifreq *ifr = (struct ifreq *) &ifc.ifc_buf[i];
		SOCKADDR *sa = (SOCKADDR *) &ifr->ifr_addr;
#   if NETINET6
		char *addr;
		struct in6_addr ia6;
#   endif /* NETINET6 */
		struct in_addr ia;
#   ifdef SIOCGIFFLAGS
		struct ifreq ifrf;
#   endif /* SIOCGIFFLAGS */
		char ip_addr[256];
#   if NETINET6
		char buf6[INET6_ADDRSTRLEN];
#   endif /* NETINET6 */

		/*
		**  If we don't have a complete ifr structure,
		**  don't try to use it.
		*/

		if ((ifc.ifc_len - i) < sizeof(*ifr))
			break;

#   ifdef BSD4_4_SOCKADDR
		if (sa->sa.sa_len > sizeof(ifr->ifr_addr))
			i += sizeof(ifr->ifr_name) + sa->sa.sa_len;
		else
#   endif /* BSD4_4_SOCKADDR */
			i += sizeof(*ifr);

		if (tTd(0, 20))
			sm_dprintf("%s\n", anynet_ntoa(sa));

		af = ifr->ifr_addr.sa_family;
		if (af != AF_INET
#   if NETINET6
		    && af != AF_INET6
#   endif /* NETINET6 */
		    )
			continue;

#   ifdef SIOCGIFFLAGS
		memset(&ifrf, '\0', sizeof(struct ifreq));
		(void) sm_strlcpy(ifrf.ifr_name, ifr->ifr_name,
			       sizeof(ifrf.ifr_name));
		(void) ioctl(s, SIOCGIFFLAGS, (char *) &ifrf);
		if (tTd(0, 41))
			sm_dprintf("\tflags: %lx\n",
				(unsigned long) ifrf.ifr_flags);
#    define IFRFREF ifrf
#   else /* SIOCGIFFLAGS */
#    define IFRFREF (*ifr)
#   endif /* SIOCGIFFLAGS */

		if (!bitset(IFF_UP, IFRFREF.ifr_flags))
			continue;

		ip_addr[0] = '\0';

		/* extract IP address from the list*/
		switch (af)
		{
		  case AF_INET:
			ia = sa->sin.sin_addr;
			if (ia.s_addr == INADDR_ANY ||
			    ia.s_addr == INADDR_NONE)
			{
				message("WARNING: interface %s is UP with %s address",
					ifr->ifr_name, inet_ntoa(ia));
				continue;
			}

			/* save IP address in text from */
			(void) sm_snprintf(ip_addr, sizeof(ip_addr), "[%.*s]",
					(int) sizeof(ip_addr) - 3,
					inet_ntoa(ia));
			break;

#   if NETINET6
		  case AF_INET6:
			SETV6LOOPBACKADDRFOUND(*sa);
#    ifdef __KAME__
			/* convert into proper scoped address */
			if ((IN6_IS_ADDR_LINKLOCAL(&sa->sin6.sin6_addr) ||
			     IN6_IS_ADDR_SITELOCAL(&sa->sin6.sin6_addr)) &&
			    sa->sin6.sin6_scope_id == 0)
			{
				struct in6_addr *ia6p;

				ia6p = &sa->sin6.sin6_addr;
				sa->sin6.sin6_scope_id = ntohs(ia6p->s6_addr[3] |
							       ((unsigned int)ia6p->s6_addr[2] << 8));
				ia6p->s6_addr[2] = ia6p->s6_addr[3] = 0;
			}
#    endif /* __KAME__ */
			ia6 = sa->sin6.sin6_addr;
			if (IN6_IS_ADDR_UNSPECIFIED(&ia6))
			{
				addr = anynet_ntop(&ia6, buf6, sizeof(buf6));
				message("WARNING: interface %s is UP with %s address",
					ifr->ifr_name,
					addr == NULL ? "(NULL)" : addr);
				continue;
			}

			/* save IP address in text from */
			addr = anynet_ntop(&ia6, buf6, sizeof(buf6));
			if (addr != NULL)
				(void) sm_snprintf(ip_addr, sizeof(ip_addr),
						   "[%.*s]",
						   (int) sizeof(ip_addr) - 3,
						   addr);
			break;

#   endif /* NETINET6 */
		}

		if (ip_addr[0] == '\0')
			continue;

		if (!wordinclass(ip_addr, 'w'))
		{
			setclass('w', ip_addr);
			if (tTd(0, 4))
				sm_dprintf("\ta.k.a.: %s\n", ip_addr);
		}

		/* skip "loopback" interface "lo" */
		if (DontProbeInterfaces == DPI_SKIPLOOPBACK &&
		    bitset(IFF_LOOPBACK, IFRFREF.ifr_flags))
			continue;

		(void) add_hostnames(sa);
	}
	sm_free(ifc.ifc_buf); /* XXX */
	(void) close(s);
#   undef IFRFREF
#  endif /* defined(SIOCGIFCONF) && !SIOCGIFCONF_IS_BROKEN */
# endif /* NETINET6 && defined(SIOCGLIFCONF) */
}
/*
**  ISLOOPBACK -- is socket address in the loopback net?
**
**	Parameters:
**		sa -- socket address.
**
**	Returns:
**		true -- is socket address in the loopback net?
**		false -- otherwise
**
*/

bool
isloopback(sa)
	SOCKADDR sa;
{
#if NETINET6
	if (IN6_IS_ADDR_LOOPBACK(&sa.sin6.sin6_addr))
		return true;
#else /* NETINET6 */
	/* XXX how to correctly extract IN_LOOPBACKNET part? */
	if (((ntohl(sa.sin.sin_addr.s_addr) & IN_CLASSA_NET)
	     >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET)
		return true;
#endif /* NETINET6 */
	return false;
}
/*
**  GET_NUM_PROCS_ONLINE -- return the number of processors currently online
**
**	Parameters:
**		none.
**
**	Returns:
**		The number of processors online.
*/

static int
get_num_procs_online()
{
	int nproc = 0;

#ifdef USESYSCTL
# if defined(CTL_HW) && defined(HW_NCPU)
	size_t sz;
	int mib[2];

	mib[0] = CTL_HW;
	mib[1] = HW_NCPU;
	sz = (size_t) sizeof(nproc);
	(void) sysctl(mib, 2, &nproc, &sz, NULL, 0);
# endif /* defined(CTL_HW) && defined(HW_NCPU) */
#else /* USESYSCTL */
# ifdef _SC_NPROCESSORS_ONLN
	nproc = (int) sysconf(_SC_NPROCESSORS_ONLN);
# else /* _SC_NPROCESSORS_ONLN */
#  ifdef __hpux
#   include <sys/pstat.h>
	struct pst_dynamic psd;

	if (pstat_getdynamic(&psd, sizeof(psd), (size_t)1, 0) != -1)
		nproc = psd.psd_proc_cnt;
#  endif /* __hpux */
# endif /* _SC_NPROCESSORS_ONLN */
#endif /* USESYSCTL */

	if (nproc <= 0)
		nproc = 1;
	return nproc;
}
/*
**  SM_CLOSEFROM -- close file descriptors
**
**	Parameters:
**		lowest -- first fd to close
**		highest -- last fd + 1 to close
**
**	Returns:
**		none
*/

void
sm_closefrom(lowest, highest)
	int lowest, highest;
{
#if HASCLOSEFROM
	closefrom(lowest);
#else /* HASCLOSEFROM */
	int i;

	for (i = lowest; i < highest; i++)
		(void) close(i);
#endif /* HASCLOSEFROM */
}
#if HASFDWALK
/*
**  CLOSEFD_WALK -- walk fd's arranging to close them
**	Callback for fdwalk()
**
**	Parameters:
**		lowest -- first fd to arrange to be closed
**		fd -- fd to arrange to be closed
**
**	Returns:
**		zero
*/

static int
closefd_walk(lowest, fd)
	void *lowest;
	int fd;
{
	if (fd >= *(int *)lowest)
		(void) fcntl(fd, F_SETFD, FD_CLOEXEC);
	return 0;
}
#endif /* HASFDWALK */
/*
**  SM_CLOSE_ON_EXEC -- arrange for file descriptors to be closed
**
**	Parameters:
**		lowest -- first fd to arrange to be closed
**		highest -- last fd + 1 to arrange to be closed
**
**	Returns:
**		none
*/

void
sm_close_on_exec(lowest, highest)
	int lowest, highest;
{
#if HASFDWALK
	(void) fdwalk(closefd_walk, &lowest);
#else /* HASFDWALK */
	int i, j;

	for (i = lowest; i < highest; i++)
	{
		if ((j = fcntl(i, F_GETFD, 0)) != -1)
			(void) fcntl(i, F_SETFD, j | FD_CLOEXEC);
	}
#endif /* HASFDWALK */
}
/*
**  SEED_RANDOM -- seed the random number generator
**
**	Parameters:
**		none
**
**	Returns:
**		none
*/

void
seed_random()
{
#if HASSRANDOMDEV
	srandomdev();
#else /* HASSRANDOMDEV */
	long seed;
	struct timeval t;

	seed = (long) CurrentPid;
	if (gettimeofday(&t, NULL) >= 0)
		seed += t.tv_sec + t.tv_usec;

# if HASRANDOM
	(void) srandom(seed);
# else /* HASRANDOM */
	(void) srand((unsigned int) seed);
# endif /* HASRANDOM */
#endif /* HASSRANDOMDEV */
}
/*
**  SM_SYSLOG -- syslog wrapper to keep messages under SYSLOG_BUFSIZE
**
**	Parameters:
**		level -- syslog level
**		id -- envelope ID or NULL (NOQUEUE)
**		fmt -- format string
**		arg... -- arguments as implied by fmt.
**
**	Returns:
**		none
*/

/* VARARGS3 */
void
#ifdef __STDC__
sm_syslog(int level, const char *id, const char *fmt, ...)
#else /* __STDC__ */
sm_syslog(level, id, fmt, va_alist)
	int level;
	const char *id;
	const char *fmt;
	va_dcl
#endif /* __STDC__ */
{
	char *buf;
	size_t bufsize;
	char *begin, *end;
	int save_errno;
	int seq = 1;
	int idlen;
	char buf0[MAXLINE];
	char *newstring;
	extern int SyslogPrefixLen;
	SM_VA_LOCAL_DECL

	save_errno = errno;
	if (id == NULL)
		id = "NOQUEUE";
	idlen = strlen(id) + SyslogPrefixLen;

	buf = buf0;
	bufsize = sizeof(buf0);

	for (;;)
	{
		int n;

		/* print log message into buf */
		SM_VA_START(ap, fmt);
		n = sm_vsnprintf(buf, bufsize, fmt, ap);
		SM_VA_END(ap);
		SM_ASSERT(n >= 0);
		if (n < bufsize)
			break;

		/* String too small, redo with correct size */
		bufsize = n + 1;
		if (buf != buf0)
		{
			sm_free(buf);
			buf = NULL;
		}
		buf = sm_malloc_x(bufsize);
	}

	/* clean up buf after it has been expanded with args */
	newstring = str2prt(buf);
	if ((strlen(newstring) + idlen + 1) < SYSLOG_BUFSIZE)
	{
#if LOG
		if (*id == '\0')
		{
			if (tTd(89, 10))
			{
				struct timeval tv;

				gettimeofday(&tv, NULL);
				sm_dprintf("%ld.%06ld %s\n", (long) tv.tv_sec,
					(long) tv.tv_usec, newstring);
			}
			else if (tTd(89, 8))
				sm_dprintf("%s\n", newstring);
			else
				syslog(level, "%s", newstring);
		}
		else
		{
			if (tTd(89, 10))
			{
				struct timeval tv;

				gettimeofday(&tv, NULL);
				sm_dprintf("%ld.%06ld %s: %s\n", (long) tv.tv_sec,
					(long) tv.tv_usec, id, newstring);
			}
			else if (tTd(89, 8))
				sm_dprintf("%s: %s\n", id, newstring);
			else
				syslog(level, "%s: %s", id, newstring);
		}
#else /* LOG */
		/*XXX should do something more sensible */
		if (*id == '\0')
			(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT, "%s\n",
					     newstring);
		else
			(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
					     "%s: %s\n", id, newstring);
#endif /* LOG */
		if (buf != buf0)
			sm_free(buf);
		errno = save_errno;
		return;
	}

/*
**  additional length for splitting: " ..." + 3, where 3 is magic to
**  have some data for the next entry.
*/

#define SL_SPLIT 7

	begin = newstring;
	idlen += 5;	/* strlen("[999]"), see below */
	while (*begin != '\0' &&
	       (strlen(begin) + idlen) > SYSLOG_BUFSIZE)
	{
		char save;

		if (seq >= 999)
		{
			/* Too many messages */
			break;
		}
		end = begin + SYSLOG_BUFSIZE - idlen - SL_SPLIT;
		while (end > begin)
		{
			/* Break on comma or space */
			if (*end == ',' || *end == ' ')
			{
				end++;	  /* Include separator */
				break;
			}
			end--;
		}
		/* No separator, break midstring... */
		if (end == begin)
			end = begin + SYSLOG_BUFSIZE - idlen - SL_SPLIT;
		save = *end;
		*end = 0;
#if LOG
		if (tTd(89, 8))
			sm_dprintf("%s[%d]: %s ...\n", id, seq++, begin);
		else
			syslog(level, "%s[%d]: %s ...", id, seq++, begin);
#else /* LOG */
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "%s[%d]: %s ...\n", id, seq++, begin);
#endif /* LOG */
		*end = save;
		begin = end;
	}
	if (seq >= 999)
	{
#if LOG
		if (tTd(89, 8))
			sm_dprintf("%s[%d]: log terminated, too many parts\n",
				id, seq);
		else
			syslog(level, "%s[%d]: log terminated, too many parts",
				id, seq);
#else /* LOG */
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
			      "%s[%d]: log terminated, too many parts\n", id, seq);
#endif /* LOG */
	}
	else if (*begin != '\0')
	{
#if LOG
		if (tTd(89, 8))
			sm_dprintf("%s[%d]: %s\n", id, seq, begin);
		else
			syslog(level, "%s[%d]: %s", id, seq, begin);
#else /* LOG */
		(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
				     "%s[%d]: %s\n", id, seq, begin);
#endif /* LOG */
	}
	if (buf != buf0)
		sm_free(buf);
	errno = save_errno;
}
/*
**  HARD_SYSLOG -- call syslog repeatedly until it works
**
**	Needed on HP-UX, which apparently doesn't guarantee that
**	syslog succeeds during interrupt handlers.
*/

#if defined(__hpux) && !defined(HPUX11)

# define MAXSYSLOGTRIES	100
# undef syslog
# ifdef V4FS
#  define XCNST	const
#  define CAST	(const char *)
# else /* V4FS */
#  define XCNST
#  define CAST
# endif /* V4FS */

void
# ifdef __STDC__
hard_syslog(int pri, XCNST char *msg, ...)
# else /* __STDC__ */
hard_syslog(pri, msg, va_alist)
	int pri;
	XCNST char *msg;
	va_dcl
# endif /* __STDC__ */
{
	int i;
	char buf[SYSLOG_BUFSIZE];
	SM_VA_LOCAL_DECL

	SM_VA_START(ap, msg);
	(void) sm_vsnprintf(buf, sizeof(buf), msg, ap);
	SM_VA_END(ap);

	for (i = MAXSYSLOGTRIES; --i >= 0 && syslog(pri, CAST "%s", buf) < 0; )
		continue;
}

# undef CAST
#endif /* defined(__hpux) && !defined(HPUX11) */
#if NEEDLOCAL_HOSTNAME_LENGTH
/*
**  LOCAL_HOSTNAME_LENGTH
**
**	This is required to get sendmail to compile against BIND 4.9.x
**	on Ultrix.
**
**	Unfortunately, a Compaq Y2K patch kit provides it without
**	bumping __RES in /usr/include/resolv.h so we can't automatically
**	figure out whether it is needed.
*/

int
local_hostname_length(hostname)
	char *hostname;
{
	size_t len_host, len_domain;

	if (!*_res.defdname)
		res_init();
	len_host = strlen(hostname);
	len_domain = strlen(_res.defdname);
	if (len_host > len_domain &&
	    (sm_strcasecmp(hostname + len_host - len_domain,
			_res.defdname) == 0) &&
	    hostname[len_host - len_domain - 1] == '.')
		return len_host - len_domain - 1;
	else
		return 0;
}
#endif /* NEEDLOCAL_HOSTNAME_LENGTH */

#if NEEDLINK
/*
**  LINK -- clone a file
**
**	Some OS's lacks link() and hard links.  Since sendmail is using
**	link() as an efficient way to clone files, this implementation
**	will simply do a file copy.
**
**	NOTE: This link() replacement is not a generic replacement as it
**	does not handle all of the semantics of the real link(2).
**
**	Parameters:
**		source -- pathname of existing file.
**		target -- pathname of link (clone) to be created.
**
**	Returns:
**		0 -- success.
**		-1 -- failure, see errno for details.
*/

int
link(source, target)
	const char *source;
	const char *target;
{
	int save_errno;
	int sff;
	int src = -1, dst = -1;
	ssize_t readlen;
	ssize_t writelen;
	char buf[BUFSIZ];
	struct stat st;

	sff = SFF_REGONLY|SFF_OPENASROOT;
	if (DontLockReadFiles)
		sff |= SFF_NOLOCK;

	/* Open the original file */
	src = safeopen((char *)source, O_RDONLY, 0, sff);
	if (src < 0)
		goto fail;

	/* Obtain the size and the mode */
	if (fstat(src, &st) < 0)
		goto fail;

	/* Create the duplicate copy */
	sff &= ~SFF_NOLOCK;
	sff |= SFF_CREAT;
	dst = safeopen((char *)target, O_CREAT|O_EXCL|O_WRONLY,
		       st.st_mode, sff);
	if (dst < 0)
		goto fail;

	/* Copy all of the bytes one buffer at a time */
	while ((readlen = read(src, &buf, sizeof(buf))) > 0)
	{
		ssize_t left = readlen;
		char *p = buf;

		while (left > 0 &&
		       (writelen = write(dst, p, (size_t) left)) >= 0)
		{
			left -= writelen;
			p += writelen;
		}
		if (writelen < 0)
			break;
	}

	/* Any trouble reading? */
	if (readlen < 0 || writelen < 0)
		goto fail;

	/* Close the input file */
	if (close(src) < 0)
	{
		src = -1;
		goto fail;
	}
	src = -1;

	/* Close the output file */
	if (close(dst) < 0)
	{
		/* don't set dst = -1 here so we unlink the file */
		goto fail;
	}

	/* Success */
	return 0;

 fail:
	save_errno = errno;
	if (src >= 0)
		(void) close(src);
	if (dst >= 0)
	{
		(void) unlink(target);
		(void) close(dst);
	}
	errno = save_errno;
	return -1;
}
#endif /* NEEDLINK */

/*
**  Compile-Time options
*/

char	*CompileOptions[] =
{
#if ALLOW_255
	"ALLOW_255",
#endif
#if NAMED_BIND
# if DNSMAP
	"DNSMAP",
# endif
#endif
#if EGD
	"EGD",
#endif
#if HESIOD
	"HESIOD",
#endif
#if HESIOD_ALLOW_NUMERIC_LOGIN
	"HESIOD_ALLOW_NUMERIC_LOGIN",
#endif
#if HES_GETMAILHOST
	"HES_GETMAILHOST",
#endif
#if IPV6_FULL
	/* Use uncompressed IPv6 address format (no "::") by default */
	"IPV6_FULL",
#endif
#if LDAPMAP
	"LDAPMAP",
#endif
#if LDAP_REFERRALS
	"LDAP_REFERRALS",
#endif
#if LOG
	"LOG",
#endif
#if MAP_NSD
	"MAP_NSD",
#endif
#if MAP_REGEX
	"MAP_REGEX",
#endif
#if MATCHGECOS
	"MATCHGECOS",
#endif
#if MILTER
	"MILTER",
#endif
#if MIME7TO8
	"MIME7TO8",
#endif
#if MIME7TO8_OLD
	"MIME7TO8_OLD",
#endif
#if MIME8TO7
	"MIME8TO7",
#endif
#if NAMED_BIND
	"NAMED_BIND",
#endif
#if NDBM
	"NDBM",
#endif
#if NETINET
	"NETINET",
#endif
#if NETINET6
	"NETINET6",
#endif
#if NETINFO
	"NETINFO",
#endif
#if NETISO
	"NETISO",
#endif
#if NETNS
	"NETNS",
#endif
#if NETUNIX
	"NETUNIX",
#endif
#if NETX25
	"NETX25",
#endif
#if NEWDB
	"NEWDB",
#endif
#if NIS
	"NIS",
#endif
#if NISPLUS
	"NISPLUS",
#endif
#if NO_DH
	"NO_DH",
#endif
#if PH_MAP
	"PH_MAP",
#endif
#ifdef PICKY_HELO_CHECK
	"PICKY_HELO_CHECK",
#endif
#if PIPELINING
	"PIPELINING",
#endif
#if SASL
# if SASL >= 20000
	"SASLv2",
# else /* SASL >= 20000 */
	"SASL",
# endif
#endif
#if SCANF
	"SCANF",
#endif
#if SM_LDAP_ERROR_ON_MISSING_ARGS
	"SM_LDAP_ERROR_ON_MISSING_ARGS",
#endif
#if SMTPDEBUG
	"SMTPDEBUG",
#endif
#if SOCKETMAP
	"SOCKETMAP",
#endif
#if STARTTLS
	"STARTTLS",
#endif
#if SUID_ROOT_FILES_OK
	"SUID_ROOT_FILES_OK",
#endif
#if TCPWRAPPERS
	"TCPWRAPPERS",
#endif
#if TLS_NO_RSA
	"TLS_NO_RSA",
#endif
#if TLS_VRFY_PER_CTX
	"TLS_VRFY_PER_CTX",
#endif
#if USERDB
	"USERDB",
#endif
#if USE_LDAP_INIT
	"USE_LDAP_INIT",
#endif
#if USE_TTYPATH
	"USE_TTYPATH",
#endif
#if XDEBUG
	"XDEBUG",
#endif
#if XLA
	"XLA",
#endif
	NULL
};


/*
**  OS compile options.
*/

char	*OsCompileOptions[] =
{
#if ADDRCONFIG_IS_BROKEN
	"ADDRCONFIG_IS_BROKEN",
#endif
#ifdef AUTO_NETINFO_HOSTS
	"AUTO_NETINFO_HOSTS",
#endif
#ifdef AUTO_NIS_ALIASES
	"AUTO_NIS_ALIASES",
#endif
#if BROKEN_RES_SEARCH
	"BROKEN_RES_SEARCH",
#endif
#ifdef BSD4_4_SOCKADDR
	"BSD4_4_SOCKADDR",
#endif
#if BOGUS_O_EXCL
	"BOGUS_O_EXCL",
#endif
#if DEC_OSF_BROKEN_GETPWENT
	"DEC_OSF_BROKEN_GETPWENT",
#endif
#if FAST_PID_RECYCLE
	"FAST_PID_RECYCLE",
#endif
#if HASCLOSEFROM
	"HASCLOSEFROM",
#endif
#if HASFCHOWN
	"HASFCHOWN",
#endif
#if HASFCHMOD
	"HASFCHMOD",
#endif
#if HASFDWALK
	"HASFDWALK",
#endif
#if HASFLOCK
	"HASFLOCK",
#endif
#if HASGETDTABLESIZE
	"HASGETDTABLESIZE",
#endif
#if HASGETUSERSHELL
	"HASGETUSERSHELL",
#endif
#if HASINITGROUPS
	"HASINITGROUPS",
#endif
#if HASLDAPGETALIASBYNAME
	"HASLDAPGETALIASBYNAME",
#endif
#if HASLSTAT
	"HASLSTAT",
#endif
#if HASNICE
	"HASNICE",
#endif
#if HASRANDOM
	"HASRANDOM",
#endif
#if HASRRESVPORT
	"HASRRESVPORT",
#endif
#if HASSETEGID
	"HASSETEGID",
#endif
#if HASSETLOGIN
	"HASSETLOGIN",
#endif
#if HASSETREGID
	"HASSETREGID",
#endif
#if HASSETRESGID
	"HASSETRESGID",
#endif
#if HASSETREUID
	"HASSETREUID",
#endif
#if HASSETRLIMIT
	"HASSETRLIMIT",
#endif
#if HASSETSID
	"HASSETSID",
#endif
#if HASSETUSERCONTEXT
	"HASSETUSERCONTEXT",
#endif
#if HASSETVBUF
	"HASSETVBUF",
#endif
#if HAS_ST_GEN
	"HAS_ST_GEN",
#endif
#if HASSRANDOMDEV
	"HASSRANDOMDEV",
#endif
#if HASURANDOMDEV
	"HASURANDOMDEV",
#endif
#if HASSTRERROR
	"HASSTRERROR",
#endif
#if HASULIMIT
	"HASULIMIT",
#endif
#if HASUNAME
	"HASUNAME",
#endif
#if HASUNSETENV
	"HASUNSETENV",
#endif
#if HASWAITPID
	"HASWAITPID",
#endif
#if HAVE_NANOSLEEP
	"HAVE_NANOSLEEP",
#endif
#if IDENTPROTO
	"IDENTPROTO",
#endif
#if IP_SRCROUTE
	"IP_SRCROUTE",
#endif
#if O_EXLOCK && HASFLOCK && !BOGUS_O_EXCL
	"LOCK_ON_OPEN",
#endif
#if MILTER_NO_NAGLE
	"MILTER_NO_NAGLE ",
#endif
#if NEEDFSYNC
	"NEEDFSYNC",
#endif
#if NEEDLINK
	"NEEDLINK",
#endif
#if NEEDLOCAL_HOSTNAME_LENGTH
	"NEEDLOCAL_HOSTNAME_LENGTH",
#endif
#if NEEDSGETIPNODE
	"NEEDSGETIPNODE",
#endif
#if NEEDSTRSTR
	"NEEDSTRSTR",
#endif
#if NEEDSTRTOL
	"NEEDSTRTOL",
#endif
#ifdef NO_GETSERVBYNAME
	"NO_GETSERVBYNAME",
#endif
#if NOFTRUNCATE
	"NOFTRUNCATE",
#endif
#if REQUIRES_DIR_FSYNC
	"REQUIRES_DIR_FSYNC",
#endif
#if RLIMIT_NEEDS_SYS_TIME_H
	"RLIMIT_NEEDS_SYS_TIME_H",
#endif
#if SAFENFSPATHCONF
	"SAFENFSPATHCONF",
#endif
#if SECUREWARE
	"SECUREWARE",
#endif
#if SFS_TYPE == SFS_4ARGS
	"SFS_4ARGS",
#elif SFS_TYPE == SFS_MOUNT
	"SFS_MOUNT",
#elif SFS_TYPE == SFS_NONE
	"SFS_NONE",
#elif SFS_TYPE == SFS_NT
	"SFS_NT",
#elif SFS_TYPE == SFS_STATFS
	"SFS_STATFS",
#elif SFS_TYPE == SFS_STATVFS
	"SFS_STATVFS",
#elif SFS_TYPE == SFS_USTAT
	"SFS_USTAT",
#elif SFS_TYPE == SFS_VFS
	"SFS_VFS",
#endif
#if SHARE_V1
	"SHARE_V1",
#endif
#if SIOCGIFCONF_IS_BROKEN
	"SIOCGIFCONF_IS_BROKEN",
#endif
#if SIOCGIFNUM_IS_BROKEN
	"SIOCGIFNUM_IS_BROKEN",
#endif
#if SNPRINTF_IS_BROKEN
	"SNPRINTF_IS_BROKEN",
#endif
#if SO_REUSEADDR_IS_BROKEN
	"SO_REUSEADDR_IS_BROKEN",
#endif
#if SYS5SETPGRP
	"SYS5SETPGRP",
#endif
#if SYSTEM5
	"SYSTEM5",
#endif
#if USE_DOUBLE_FORK
	"USE_DOUBLE_FORK",
#endif
#if USE_ENVIRON
	"USE_ENVIRON",
#endif
#if USE_SA_SIGACTION
	"USE_SA_SIGACTION",
#endif
#if USE_SIGLONGJMP
	"USE_SIGLONGJMP",
#endif
#if USEGETCONFATTR
	"USEGETCONFATTR",
#endif
#if USESETEUID
	"USESETEUID",
#endif
#ifdef USESYSCTL
	"USESYSCTL",
#endif
#if USE_OPENSSL_ENGINE
	"USE_OPENSSL_ENGINE",
#endif
#if USING_NETSCAPE_LDAP
	"USING_NETSCAPE_LDAP",
#endif
#ifdef WAITUNION
	"WAITUNION",
#endif
	NULL
};

/*
**  FFR compile options.
*/

char	*FFRCompileOptions[] =
{
#if _FFR_ADD_BCC
	"_FFR_ADD_BCC",
#endif
#if _FFR_ADDR_TYPE_MODES
	/* more info in {addr_type}, requires m4 changes! */
	"_FFR_ADDR_TYPE_MODES",
#endif
#if _FFR_ALIAS_DETAIL
	/* try to handle +detail for aliases */
	"_FFR_ALIAS_DETAIL",
#endif
#if _FFR_ALLOW_SASLINFO
	/* DefaultAuthInfo can be specified by user. */
	/* DefaultAuthInfo doesn't really work in 8.13 anymore. */
	"_FFR_ALLOW_SASLINFO",
#endif
#if _FFR_BADRCPT_SHUTDOWN
	/* shut down connection (421) if there are too many bad RCPTs */
	"_FFR_BADRCPT_SHUTDOWN",
#endif
#if _FFR_BESTMX_BETTER_TRUNCATION
	/* Better truncation of list of MX records for dns map. */
	"_FFR_BESTMX_BETTER_TRUNCATION",
#endif
#if _FFR_BOUNCE_QUEUE
	/* Separate, unprocessed queue for DSNs */
	/* John Gardiner Myers of Proofpoint */
	"_FFR_BOUNCE_QUEUE",
#endif
#if _FFR_CATCH_BROKEN_MTAS
	/* Deal with MTAs that send a reply during the DATA phase. */
	"_FFR_CATCH_BROKEN_MTAS",
#endif
#if _FFR_CHK_QUEUE
	/* Stricter checks about queue directory permissions. */
	"_FFR_CHK_QUEUE",
#endif
#if _FFR_CLIENT_SIZE
	/* Don't try to send mail if its size exceeds SIZE= of server. */
	"_FFR_CLIENT_SIZE",
#endif
#if _FFR_CRLPATH
	/* CRLPath; needs documentation; Al Smith */
	"_FFR_CRLPATH",
#endif
#if _FFR_DM_ONE
	/* deliver first TA in background, then queue */
	"_FFR_DM_ONE",
#endif
#if _FFR_DIGUNIX_SAFECHOWN
	/* Properly set SAFECHOWN (include/sm/conf.h) for Digital UNIX */
/* Problem noted by Anne Bennett of Concordia University */
	"_FFR_DIGUNIX_SAFECHOWN",
#endif
#if _FFR_DNSMAP_ALIASABLE
	/* Allow dns map type to be used for aliases. */
/* Don Lewis of TDK */
	"_FFR_DNSMAP_ALIASABLE",
#endif
#if _FFR_DONTLOCKFILESFORREAD_OPTION
	/* Enable DontLockFilesForRead option. */
	"_FFR_DONTLOCKFILESFORREAD_OPTION",
#endif
#if _FFR_DOTTED_USERNAMES
	/* Allow usernames with '.' */
	"_FFR_DOTTED_USERNAMES",
#endif
#if _FFR_DPO_CS
	/*
	**  Make DaemonPortOptions case sensitive.
	**  For some unknown reasons the code converted every option
	**  to uppercase (first letter only, as that's the only one that
	**  is actually checked). This prevented all new lower case options
	**  from working...
	**  The documentation doesn't say anything about case (in)sensitivity,
	**  which means it should be case sensitive by default,
	**  but it's not a good idea to change this within a patch release,
	**  so let's delay this to 8.15.
	*/

	"_FFR_DPO_CS",
#endif
#if _FFR_DPRINTF_MAP
	/* dprintf map for logging */
	"_FFR_DPRINTF_MAP",
#endif
#if _FFR_DROP_TRUSTUSER_WARNING
	/*
	**  Don't issue this warning:
	**  "readcf: option TrustedUser may cause problems on systems
	**  which do not support fchown() if UseMSP is not set.
	*/

	"_FFR_DROP_TRUSTUSER_WARNING",
#endif
#if _FFR_EIGHT_BIT_ADDR_OK
	/* EightBitAddrOK: allow 8-bit e-mail addresses */
	"_FFR_EIGHT_BIT_ADDR_OK",
#endif
#if _FFR_EXTRA_MAP_CHECK
	/* perform extra checks on $( $) in R lines */
	"_FFR_EXTRA_MAP_CHECK",
#endif
#if _FFR_GETHBN_ExFILE
	/*
	**  According to Motonori Nakamura some gethostbyname()
	**  implementations (TurboLinux?) may (temporarily) fail
	**  due to a lack of file descriptors. Enabling this FFR
	**  will check errno for EMFILE and ENFILE and in case of a match
	**  cause a temporary error instead of a permanent error.
	**  The right solution is of course to file a bug against those
	**  systems such that they actually set h_errno = TRY_AGAIN.
	*/

	"_FFR_GETHBN_ExFILE",
#endif
#if _FFR_FIPSMODE
	/* FIPSMode (if supported by OpenSSL library) */
	"_FFR_FIPSMODE",
#endif
#if _FFR_FIX_DASHT
	/*
	**  If using -t, force not sending to argv recipients, even
	**  if they are mentioned in the headers.
	*/

	"_FFR_FIX_DASHT",
#endif
#if _FFR_FORWARD_SYSERR
	/* Cause a "syserr" if forward file isn't "safe". */
	"_FFR_FORWARD_SYSERR",
#endif
#if _FFR_GEN_ORCPT
	/* Generate a ORCPT DSN arg if not already provided */
	"_FFR_GEN_ORCPT",
#endif
#if _FFR_HANDLE_ISO8859_GECOS
	/*
	**  Allow ISO 8859 characters in GECOS field: replace them
	**  with ASCII "equivalent".
	*/

/* Peter Eriksson of Linkopings universitet */
	"_FFR_HANDLE_ISO8859_GECOS",
#endif
#if _FFR_HANDLE_HDR_RW_TEMPFAIL
	/*
	**  Temporary header rewriting problems from remotename() etc
	**  are not "sticky" for mci (e.g., during queue runs).
	*/

	"_FFR_HANDLE_HDR_RW_TEMPFAIL",
#endif
#if _FFR_HPUX_NSSWITCH
	/* Use nsswitch on HP-UX */
	"_FFR_HPUX_NSSWITCH",
#endif
#if _FFR_IGNORE_BOGUS_ADDR
	/* Ignore addresses for which prescan() failed */
	"_FFR_IGNORE_BOGUS_ADDR",
#endif
#if _FFR_IGNORE_EXT_ON_HELO
	/* Ignore extensions offered in response to HELO */
	"_FFR_IGNORE_EXT_ON_HELO",
#endif
#if _FFR_LINUX_MHNL
	/* Set MAXHOSTNAMELEN to 256 (Linux) */
	"_FFR_LINUX_MHNL",
#endif
#if _FFR_LOCAL_DAEMON
	/* Local daemon mode (-bl) which only accepts loopback connections */
	"_FFR_LOCAL_DAEMON",
#endif
#if _FFR_LOG_MORE1
	/* log some TLS/AUTH info in from= too */
	"_FFR_LOG_MORE1",
#endif
#if _FFR_LOG_MORE2
	/* log some TLS info in to= too */
	"_FFR_LOG_MORE2",
#endif
#if _FFR_LOGREPLY
	"_FFR_LOGREPLY",
#endif
#if _FFR_MAIL_MACRO
	"_FFR_MAIL_MACRO",
#endif
#if _FFR_MAXDATASIZE
	/*
	**  It is possible that a header is larger than MILTER_CHUNK_SIZE,
	**  hence this shouldn't be used as limit for milter communication.
	**  see also libmilter/comm.c
	**  Gurusamy Sarathy of ActiveState
	*/

	"_FFR_MAXDATASIZE",
#endif
#if _FFR_MAX_FORWARD_ENTRIES
	/* Try to limit number of .forward entries */
	/* (doesn't work) */
/* Randall S. Winchester of the University of Maryland */
	"_FFR_MAX_FORWARD_ENTRIES",
#endif
#if _FFR_MAX_SLEEP_TIME
	/* Limit sleep(2) time in libsm/clock.c */
	"_FFR_MAX_SLEEP_TIME",
#endif
#if _FFR_MDS_NEGOTIATE
	/* MaxDataSize negotation with libmilter */
	"_FFR_MDS_NEGOTIATE",
#endif
#if _FFR_MEMSTAT
	/* Check free memory */
	"_FFR_MEMSTAT",
#endif
#if _FFR_MILTER_CHECK
	"_FFR_MILTER_CHECK",
#endif
#if _FFR_MILTER_CONNECT_REPLYCODE
	/* milter: propagate replycode returned by connect commands */
	/* John Gardiner Myers of Proofpoint */
	"_FFR_MILTER_CONNECT_REPLYCODE ",
#endif
#if _FFR_MILTER_CONVERT_ALL_LF_TO_CRLF
	/*
	**  milter_body() uses the same conversion algorithm as putbody()
	**  to translate the "local" df format (\n) to SMTP format (\r\n).
	**  However, putbody() and mime8to7() use different conversion
	**  algorithms.
	**  If the input date does not follow the SMTP standard
	**  (e.g., if it has "naked \r"s), then the output from putbody()
	**  and mime8to7() will most likely be different.
	**  By turning on this FFR milter_body() will try to "imitate"
	**  mime8to7().
	**  Note: there is no (simple) way to deal with both conversions
	**  in a consistent manner. Moreover, as the "GiGo" principle applies,
	**  it's not really worth to fix it.
	*/

	"_FFR_MILTER_CONVERT_ALL_LF_TO_CRLF",
#endif
#if _FFR_MILTER_CHECK_REJECTIONS_TOO
	/*
	**  Also send RCPTs that are rejected by check_rcpt to a milter
	**  (if requested during option negotiation).
	*/

	"_FFR_MILTER_CHECK_REJECTIONS_TOO",
#endif
#if _FFR_MILTER_ENHSC
	/* extract enhanced status code from milter replies for dsn= logging */
	"_FFR_MILTER_ENHSC",
#endif
#if _FFR_MIME7TO8_OLD
	/* Old mime7to8 code, the new is broken for at least one example. */
	"_FFR_MIME7TO8_OLD",
#endif
#if _FFR_MORE_MACROS
	/* allow more long macro names ("unprintable" characters). */
	"_FFR_MORE_MACROS",
#endif
#if _FFR_MSG_ACCEPT
	/* allow to override "Message accepted for delivery" */
	"_FFR_MSG_ACCEPT",
#endif
#if _FFR_NODELAYDSN_ON_HOLD
	/* Do not issue a DELAY DSN for mailers that use the hold flag. */
/* Steven Pitzl */
	"_FFR_NODELAYDSN_ON_HOLD",
#endif
#if _FFR_NO_PIPE
	/* Disable PIPELINING, delay client if used. */
	"_FFR_NO_PIPE",
#endif
#if _FFR_LDAP_NETWORK_TIMEOUT
	/* set LDAP_OPT_NETWORK_TIMEOUT if available (-c) */
	"_FFR_LDAP_NETWORK_TIMEOUT",
#endif
#if _FFR_LOG_NTRIES
	/* log ntries=, from Nik Clayton of FreeBSD */
	"_FFR_LOG_NTRIES",
#endif
#if _FFR_PROXY
	/* "proxy" (synchronous) delivery mode */
	"_FFR_PROXY",
#endif
#if _FFR_QF_PARANOIA
	"_FFR_QF_PARANOIA",
#endif
#if _FFR_QUEUE_GROUP_SORTORDER
	/* Allow QueueSortOrder per queue group. */
/* XXX: Still need to actually use qgrp->qg_sortorder */
	"_FFR_QUEUE_GROUP_SORTORDER",
#endif
#if _FFR_QUEUE_MACRO
	/* Define {queue} macro. */
	"_FFR_QUEUE_MACRO",
#endif
#if _FFR_QUEUE_RUN_PARANOIA
	/* Additional checks when doing queue runs; interval of checks */
	"_FFR_QUEUE_RUN_PARANOIA",
#endif
#if _FFR_QUEUE_SCHED_DBG
	/* Debug output for the queue scheduler. */
	"_FFR_QUEUE_SCHED_DBG",
#endif
#if _FFR_RCPTFLAGS
	"_FFR_RCPTFLAGS",
#endif
#if _FFR_RCPTTHROTDELAY
	/* configurable delay for BadRcptThrottle */
	"_FFR_RCPTTHROTDELAY",
#endif
#if _FFR_REDIRECTEMPTY
	/*
	**  envelope <> can't be sent to mailing lists, only owner-
	**  send spam of this type to owner- of the list
	**  ----  to stop spam from going to mailing lists.
	*/

	"_FFR_REDIRECTEMPTY",
#endif
#if _FFR_REJECT_NUL_BYTE
	/* reject NUL bytes in body */
	"_FFR_REJECT_NUL_BYTE",
#endif
#if _FFR_RESET_MACRO_GLOBALS
	/* Allow macro 'j' to be set dynamically via rulesets. */
	"_FFR_RESET_MACRO_GLOBALS",
#endif
#if _FFR_RHS
	/* Random shuffle for queue sorting. */
	"_FFR_RHS",
#endif
#if _FFR_RUNPQG
	/*
	**  allow -qGqueue_group -qp to work, i.e.,
	**  restrict a persistent queue runner to a queue group.
	*/

	"_FFR_RUNPQG",
#endif
#if _FFR_SESSID
	/* session id (for logging) */
	"_FFR_SESSID",
#endif
#if _FFR_SHM_STATUS
	/* Donated code (unused). */
	"_FFR_SHM_STATUS",
#endif
#if _FFR_LDAP_SINGLEDN
	/*
	**  The LDAP database map code in Sendmail 8.12.10, when
	**  given the -1 switch, would match only a single DN,
	**  but was able to return multiple attributes for that
	**  DN.  In Sendmail 8.13 this "bug" was corrected to
	**  only return if exactly one attribute matched.
	**
	**  Unfortunately, our configuration uses the former
	**  behaviour.  Attached is a relatively simple patch
	**  to 8.13.4 which adds a -2 switch (for lack of a
	**  better option) which returns the single dn/multiple
	**  attributes.
	**
	** Jeffrey T. Eaton, Carnegie-Mellon University
	*/

	"_FFR_LDAP_SINGLEDN",
#endif
#if _FFR_SKIP_DOMAINS
	/* process every N'th domain instead of every N'th message */
	"_FFR_SKIP_DOMAINS",
#endif
#if _FFR_SLEEP_USE_SELECT
	/* Use select(2) in libsm/clock.c to emulate sleep(2) */
	"_FFR_SLEEP_USE_SELECT ",
#endif
#if _FFR_SPT_ALIGN
	/*
	**  It looks like the Compaq Tru64 5.1A now aligns argv and envp to 64
	**  bit alignment, so unless each piece of argv and envp is a multiple
	**  of 8 bytes (including terminating NULL), initsetproctitle() won't
	**  use any of the space beyond argv[0]. Be sure to set SPT_ALIGN_SIZE
	**  if you use this FFR.
	*/

/* Chris Adams of HiWAAY Informations Services */
	"_FFR_SPT_ALIGN",
#endif
#if _FFR_SS_PER_DAEMON
	/* SuperSafe per DaemonPortOptions: 'T' (better letter?) */
	"_FFR_SS_PER_DAEMON",
#endif
#if _FFR_TESTS
	/* enable some test code */
	"_FFR_TESTS",
#endif
#if _FFR_TIMERS
	/* Donated code (unused). */
	"_FFR_TIMERS",
#endif
#if _FFR_TLS_EC
	"_FFR_TLS_EC",
#endif
#if _FFR_TLS_USE_CERTIFICATE_CHAIN_FILE
	/*
	**  Use SSL_CTX_use_certificate_chain_file()
	**  instead of SSL_CTX_use_certificate_file()
	*/

	"_FFR_TLS_USE_CERTIFICATE_CHAIN_FILE",
#endif
#if _FFR_TLS_SE_OPTS
	/* TLS session options */
	"_FFR_TLS_SE_OPTS",
#endif
#if _FFR_TRUSTED_QF
	/*
	**  If we don't own the file mark it as unsafe.
	**  However, allow TrustedUser to own it as well
	**  in case TrustedUser manipulates the queue.
	*/

	"_FFR_TRUSTED_QF",
#endif
#if _FFR_USE_GETPWNAM_ERRNO
	/*
	**  See libsm/mbdb.c: only enable this on OSs
	**  that implement the correct (POSIX) semantics.
	**  This will need to become an OS-specific #if enabled
	**  in one of the headers files under include/sm/os/ .
	*/

	"_FFR_USE_GETPWNAM_ERRNO",
#endif
#if _FFR_USE_SEM_LOCKING
	"_FFR_USE_SEM_LOCKING",
#endif
#if _FFR_USE_SETLOGIN
	/* Use setlogin() */
/* Peter Philipp */
	"_FFR_USE_SETLOGIN",
#endif
#if _FFR_XCNCT
	"_FFR_XCNCT",
#endif
	NULL
};

