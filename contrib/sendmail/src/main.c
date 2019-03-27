/*
 * Copyright (c) 1998-2006, 2008, 2009, 2011 Proofpoint, Inc. and its suppliers.
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

#define _DEFINE
#include <sendmail.h>
#include <sm/sendmail.h>
#include <sm/xtrap.h>
#include <sm/signal.h>

#ifndef lint
SM_UNUSED(static char copyright[]) =
"@(#) Copyright (c) 1998-2013 Proofpoint, Inc. and its suppliers.\n\
	All rights reserved.\n\
     Copyright (c) 1983, 1995-1997 Eric P. Allman.  All rights reserved.\n\
     Copyright (c) 1988, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* ! lint */

SM_RCSID("@(#)$Id: main.c,v 8.988 2013-11-23 02:52:37 gshapiro Exp $")


#if NETINET || NETINET6
# include <arpa/inet.h>
#endif /* NETINET || NETINET6 */

/* for getcfname() */
#include <sendmail/pathnames.h>

static SM_DEBUG_T
DebugNoPRestart = SM_DEBUG_INITIALIZER("no_persistent_restart",
	"@(#)$Debug: no_persistent_restart - don't restart, log only $");

static void	dump_class __P((STAB *, int));
static void	obsolete __P((char **));
static void	testmodeline __P((char *, ENVELOPE *));
static char	*getextenv __P((const char *));
static void	sm_printoptions __P((char **));
static SIGFUNC_DECL	intindebug __P((int));
static SIGFUNC_DECL	sighup __P((int));
static SIGFUNC_DECL	sigpipe __P((int));
static SIGFUNC_DECL	sigterm __P((int));
#ifdef SIGUSR1
static SIGFUNC_DECL	sigusr1 __P((int));
#endif /* SIGUSR1 */

/*
**  SENDMAIL -- Post mail to a set of destinations.
**
**	This is the basic mail router.  All user mail programs should
**	call this routine to actually deliver mail.  Sendmail in
**	turn calls a bunch of mail servers that do the real work of
**	delivering the mail.
**
**	Sendmail is driven by settings read in from /etc/mail/sendmail.cf
**	(read by readcf.c).
**
**	Usage:
**		/usr/lib/sendmail [flags] addr ...
**
**		See the associated documentation for details.
**
**	Authors:
**		Eric Allman, UCB/INGRES (until 10/81).
**			     Britton-Lee, Inc., purveyors of fine
**				database computers (11/81 - 10/88).
**			     International Computer Science Institute
**				(11/88 - 9/89).
**			     UCB/Mammoth Project (10/89 - 7/95).
**			     InReference, Inc. (8/95 - 1/97).
**			     Sendmail, Inc. (1/98 - 9/13).
**		The support of my employers is gratefully acknowledged.
**			Few of them (Britton-Lee in particular) have had
**			anything to gain from my involvement in this project.
**
**		Gregory Neil Shapiro,
**			Worcester Polytechnic Institute	(until 3/98).
**			Sendmail, Inc. (3/98 - 10/13).
**			Proofpoint, Inc. (10/13 - present).
**
**		Claus Assmann,
**			Sendmail, Inc. (12/98 - 10/13).
**			Proofpoint, Inc. (10/13 - present).
*/

char		*FullName;	/* sender's full name */
ENVELOPE	BlankEnvelope;	/* a "blank" envelope */
static ENVELOPE	MainEnvelope;	/* the envelope around the basic letter */
ADDRESS		NullAddress =	/* a null address */
		{ "", "", NULL, "" };
char		*CommandLineArgs;	/* command line args for pid file */
bool		Warn_Q_option = false;	/* warn about Q option use */
static int	MissingFds = 0;	/* bit map of fds missing on startup */
char		*Mbdb = "pw";	/* mailbox database defaults to /etc/passwd */

#ifdef NGROUPS_MAX
GIDSET_T	InitialGidSet[NGROUPS_MAX];
#endif /* NGROUPS_MAX */

#define MAXCONFIGLEVEL	10	/* highest config version level known */

#if SASL
static sasl_callback_t srvcallbacks[] =
{
	{	SASL_CB_VERIFYFILE,	(sasl_callback_ft)&safesaslfile,	NULL	},
	{	SASL_CB_PROXY_POLICY,	(sasl_callback_ft)&proxy_policy,	NULL	},
	{	SASL_CB_LIST_END,	NULL,		NULL	}
};
#endif /* SASL */

unsigned int	SubmitMode;
int		SyslogPrefixLen; /* estimated length of syslog prefix */
#define PIDLEN		6	/* pid length for computing SyslogPrefixLen */
#ifndef SL_FUDGE
# define SL_FUDGE	10	/* fudge offset for SyslogPrefixLen */
#endif /* ! SL_FUDGE */
#define SLDLL		8	/* est. length of default syslog label */


/* Some options are dangerous to allow users to use in non-submit mode */
#define CHECK_AGAINST_OPMODE(cmd)					\
{									\
	if (extraprivs &&						\
	    OpMode != MD_DELIVER && OpMode != MD_SMTP &&		\
	    OpMode != MD_ARPAFTP && OpMode != MD_CHECKCONFIG &&		\
	    OpMode != MD_VERIFY && OpMode != MD_TEST)			\
	{								\
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,		\
				     "WARNING: Ignoring submission mode -%c option (not in submission mode)\n", \
		       (cmd));						\
		break;							\
	}								\
	if (extraprivs && queuerun)					\
	{								\
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,		\
				     "WARNING: Ignoring submission mode -%c option with -q\n", \
		       (cmd));						\
		break;							\
	}								\
}

int
main(argc, argv, envp)
	int argc;
	char **argv;
	char **envp;
{
	register char *p;
	char **av;
	extern char Version[];
	char *ep, *from;
	STAB *st;
	register int i;
	int j;
	int dp;
	int fill_errno;
	int qgrp = NOQGRP;		/* queue group to process */
	bool safecf = true;
	BITMAP256 *p_flags = NULL;	/* daemon flags */
	bool warn_C_flag = false;
	bool auth = true;		/* whether to set e_auth_param */
	char warn_f_flag = '\0';
	bool run_in_foreground = false;	/* -bD mode */
	bool queuerun = false, debug = false;
	struct passwd *pw;
	struct hostent *hp;
	char *nullserver = NULL;
	char *authinfo = NULL;
	char *sysloglabel = NULL;	/* label for syslog */
	char *conffile = NULL;		/* name of .cf file */
	char *queuegroup = NULL;	/* queue group to process */
	char *quarantining = NULL;	/* quarantine queue items? */
	bool extraprivs;
	bool forged, negate;
	bool queuepersistent = false;	/* queue runner process runs forever */
	bool foregroundqueue = false;	/* queue run in foreground */
	bool save_val;			/* to save some bool var. */
	int cftype;			/* which cf file to use? */
	SM_FILE_T *smdebug;
	static time_t starttime = 0;	/* when was process started */
	struct stat traf_st;		/* for TrafficLog FIFO check */
	char buf[MAXLINE];
	char jbuf[MAXHOSTNAMELEN];	/* holds MyHostName */
	static char rnamebuf[MAXNAME];	/* holds RealUserName */
	char *emptyenviron[1];
#if STARTTLS
	bool tls_ok;
#endif /* STARTTLS */
	QUEUE_CHAR *new;
	ENVELOPE *e;
	extern int DtableSize;
	extern int optind;
	extern int opterr;
	extern char *optarg;
	extern char **environ;
#if SASL
	extern void sm_sasl_init __P((void));
#endif /* SASL */

#if USE_ENVIRON
	envp = environ;
#endif /* USE_ENVIRON */

	/* turn off profiling */
	SM_PROF(0);

	/* install default exception handler */
	sm_exc_newthread(fatal_error);

	/* set the default in/out channel so errors reported to screen */
	InChannel = smioin;
	OutChannel = smioout;

	/*
	**  Check to see if we reentered.
	**	This would normally happen if e_putheader or e_putbody
	**	were NULL when invoked.
	*/

	if (starttime != 0)
	{
		syserr("main: reentered!");
		abort();
	}
	starttime = curtime();

	/* avoid null pointer dereferences */
	TermEscape.te_rv_on = TermEscape.te_under_on = TermEscape.te_normal = "";

	RealUid = getuid();
	RealGid = getgid();

	/* Check if sendmail is running with extra privs */
	extraprivs = (RealUid != 0 &&
		      (geteuid() != getuid() || getegid() != getgid()));

	CurrentPid = getpid();

	/* get whatever .cf file is right for the opmode */
	cftype = SM_GET_RIGHT_CF;

	/* in 4.4BSD, the table can be huge; impose a reasonable limit */
	DtableSize = getdtsize();
	if (DtableSize > 256)
		DtableSize = 256;

	/*
	**  Be sure we have enough file descriptors.
	**	But also be sure that 0, 1, & 2 are open.
	*/

	/* reset errno and fill_errno; the latter is used way down below */
	errno = fill_errno = 0;
	fill_fd(STDIN_FILENO, NULL);
	if (errno != 0)
		fill_errno = errno;
	fill_fd(STDOUT_FILENO, NULL);
	if (errno != 0)
		fill_errno = errno;
	fill_fd(STDERR_FILENO, NULL);
	if (errno != 0)
		fill_errno = errno;

	sm_closefrom(STDERR_FILENO + 1, DtableSize);
	errno = 0;
	smdebug = NULL;

#if LOG
# ifndef SM_LOG_STR
#  define SM_LOG_STR	"sendmail"
# endif /* ! SM_LOG_STR */
#  ifdef LOG_MAIL
	openlog(SM_LOG_STR, LOG_PID, LOG_MAIL);
#  else /* LOG_MAIL */
	openlog(SM_LOG_STR, LOG_PID);
#  endif /* LOG_MAIL */
#endif /* LOG */

	/*
	**  Seed the random number generator.
	**  Used for queue file names, picking a queue directory, and
	**  MX randomization.
	*/

	seed_random();

	/* do machine-dependent initializations */
	init_md(argc, argv);


	SyslogPrefixLen = PIDLEN + (MAXQFNAME - 3) + SL_FUDGE + SLDLL;

	/* reset status from syserr() calls for missing file descriptors */
	Errors = 0;
	ExitStat = EX_OK;

	SubmitMode = SUBMIT_UNKNOWN;
#if _FFR_LOCAL_DAEMON
	LocalDaemon = false;
# if NETINET6
	V6LoopbackAddrFound = false;
# endif /* NETINET6 */
#endif /* _FFR_LOCAL_DAEMON */
#if XDEBUG
	checkfd012("after openlog");
#endif /* XDEBUG */

	tTsetup(tTdvect, sizeof(tTdvect), "0-99.1,*_trace_*.1");

#ifdef NGROUPS_MAX
	/* save initial group set for future checks */
	i = getgroups(NGROUPS_MAX, InitialGidSet);
	if (i <= 0)
	{
		InitialGidSet[0] = (GID_T) -1;
		i = 0;
	}
	while (i < NGROUPS_MAX)
		InitialGidSet[i++] = InitialGidSet[0];
#endif /* NGROUPS_MAX */

	/* drop group id privileges (RunAsUser not yet set) */
	dp = drop_privileges(false);
	setstat(dp);

#ifdef SIGUSR1
	/* Only allow root (or non-set-*-ID binaries) to use SIGUSR1 */
	if (!extraprivs)
	{
		/* arrange to dump state on user-1 signal */
		(void) sm_signal(SIGUSR1, sigusr1);
	}
	else
	{
		/* ignore user-1 signal */
		(void) sm_signal(SIGUSR1, SIG_IGN);
	}
#endif /* SIGUSR1 */

	/* initialize for setproctitle */
	initsetproctitle(argc, argv, envp);

	/* Handle any non-getoptable constructions. */
	obsolete(argv);

	/*
	**  Do a quick prescan of the argument list.
	*/


	/* find initial opMode */
	OpMode = MD_DELIVER;
	av = argv;
	p = strrchr(*av, '/');
	if (p++ == NULL)
		p = *av;
	if (strcmp(p, "newaliases") == 0)
		OpMode = MD_INITALIAS;
	else if (strcmp(p, "mailq") == 0)
		OpMode = MD_PRINT;
	else if (strcmp(p, "smtpd") == 0)
		OpMode = MD_DAEMON;
	else if (strcmp(p, "hoststat") == 0)
		OpMode = MD_HOSTSTAT;
	else if (strcmp(p, "purgestat") == 0)
		OpMode = MD_PURGESTAT;

#if defined(__osf__) || defined(_AIX3)
# define OPTIONS	"A:B:b:C:cD:d:e:F:f:Gh:IiL:M:mN:nO:o:p:Q:q:R:r:sTtV:vX:x"
#endif /* defined(__osf__) || defined(_AIX3) */
#if defined(sony_news)
# define OPTIONS	"A:B:b:C:cD:d:E:e:F:f:Gh:IiJ:L:M:mN:nO:o:p:Q:q:R:r:sTtV:vX:"
#endif /* defined(sony_news) */
#ifndef OPTIONS
# define OPTIONS	"A:B:b:C:cD:d:e:F:f:Gh:IiL:M:mN:nO:o:p:Q:q:R:r:sTtV:vX:"
#endif /* ! OPTIONS */

	/* Set to 0 to allow -b; need to check optarg before using it! */
	opterr = 0;
	while ((j = getopt(argc, argv, OPTIONS)) != -1)
	{
		switch (j)
		{
		  case 'b':	/* operations mode */
			j = (optarg == NULL) ? ' ' : *optarg;
			switch (j)
			{
			  case MD_DAEMON:
			  case MD_FGDAEMON:
			  case MD_SMTP:
			  case MD_INITALIAS:
			  case MD_DELIVER:
			  case MD_VERIFY:
			  case MD_TEST:
			  case MD_PRINT:
			  case MD_PRINTNQE:
			  case MD_HOSTSTAT:
			  case MD_PURGESTAT:
			  case MD_ARPAFTP:
			  case MD_CHECKCONFIG:
				OpMode = j;
				break;

#if _FFR_LOCAL_DAEMON
			  case MD_LOCAL:
				OpMode = MD_DAEMON;
				LocalDaemon = true;
				break;
#endif /* _FFR_LOCAL_DAEMON */

			  case MD_FREEZE:
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Frozen configurations unsupported\n");
				return EX_USAGE;

			  default:
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Invalid operation mode %c\n",
						     j);
				return EX_USAGE;
			}
			break;

		  case 'D':
			if (debug)
			{
				errno = 0;
				syserr("-D file must be before -d");
				ExitStat = EX_USAGE;
				break;
			}
			dp = drop_privileges(true);
			setstat(dp);
			smdebug = sm_io_open(SmFtStdio, SM_TIME_DEFAULT,
					    optarg, SM_IO_APPEND, NULL);
			if (smdebug == NULL)
			{
				syserr("cannot open %s", optarg);
				ExitStat = EX_CANTCREAT;
				break;
			}
			sm_debug_setfile(smdebug);
			break;

		  case 'd':
			debug = true;
			tTflag(optarg);
			(void) sm_io_setvbuf(sm_debug_file(), SM_TIME_DEFAULT,
					     (char *) NULL, SM_IO_NBF,
					     SM_IO_BUFSIZ);
			break;

		  case 'G':	/* relay (gateway) submission */
			SubmitMode = SUBMIT_MTA;
			break;

		  case 'L':
			if (optarg == NULL)
			{
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "option requires an argument -- '%c'",
						     (char) j);
				return EX_USAGE;
			}
			j = SM_MIN(strlen(optarg), 32) + 1;
			sysloglabel = xalloc(j);
			(void) sm_strlcpy(sysloglabel, optarg, j);
			SyslogPrefixLen = PIDLEN + (MAXQFNAME - 3) +
					  SL_FUDGE + j;
			break;

		  case 'Q':
		  case 'q':
			/* just check if it is there */
			queuerun = true;
			break;
		}
	}
	opterr = 1;

	/* Don't leak queue information via debug flags */
	if (extraprivs && queuerun && debug)
	{
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "WARNING: Can not use -d with -q.  Disabling debugging.\n");
		sm_debug_close();
		sm_debug_setfile(NULL);
		(void) memset(tTdvect, '\0', sizeof(tTdvect));
	}

#if LOG
	if (sysloglabel != NULL)
	{
		/* Sanitize the string */
		for (p = sysloglabel; *p != '\0'; p++)
		{
			if (!isascii(*p) || !isprint(*p) || *p == '%')
				*p = '*';
		}
		closelog();
#  ifdef LOG_MAIL
		openlog(sysloglabel, LOG_PID, LOG_MAIL);
#  else /* LOG_MAIL */
		openlog(sysloglabel, LOG_PID);
#  endif /* LOG_MAIL */
	}
#endif /* LOG */

	/* set up the blank envelope */
	BlankEnvelope.e_puthdr = putheader;
	BlankEnvelope.e_putbody = putbody;
	BlankEnvelope.e_xfp = NULL;
	STRUCTCOPY(NullAddress, BlankEnvelope.e_from);
	CurEnv = &BlankEnvelope;
	STRUCTCOPY(NullAddress, MainEnvelope.e_from);

	/*
	**  Set default values for variables.
	**	These cannot be in initialized data space.
	*/

	setdefaults(&BlankEnvelope);
	initmacros(&BlankEnvelope);

	/* reset macro */
	set_op_mode(OpMode);
	if (OpMode == MD_DAEMON)
		DaemonPid = CurrentPid;	/* needed for finis() to work */

	pw = sm_getpwuid(RealUid);
	if (pw != NULL)
		(void) sm_strlcpy(rnamebuf, pw->pw_name, sizeof(rnamebuf));
	else
		(void) sm_snprintf(rnamebuf, sizeof(rnamebuf), "Unknown UID %d",
				   (int) RealUid);

	RealUserName = rnamebuf;

	if (tTd(0, 101))
	{
		sm_dprintf("Version %s\n", Version);
		finis(false, true, EX_OK);
		/* NOTREACHED */
	}

	/*
	**  if running non-set-user-ID binary as non-root, pretend
	**  we are the RunAsUid
	*/

	if (RealUid != 0 && geteuid() == RealUid)
	{
		if (tTd(47, 1))
			sm_dprintf("Non-set-user-ID binary: RunAsUid = RealUid = %d\n",
				   (int) RealUid);
		RunAsUid = RealUid;
	}
	else if (geteuid() != 0)
		RunAsUid = geteuid();

	EffGid = getegid();
	if (RealUid != 0 && EffGid == RealGid)
		RunAsGid = RealGid;

	if (tTd(47, 5))
	{
		sm_dprintf("main: e/ruid = %d/%d e/rgid = %d/%d\n",
			   (int) geteuid(), (int) getuid(),
			   (int) getegid(), (int) getgid());
		sm_dprintf("main: RunAsUser = %d:%d\n",
			   (int) RunAsUid, (int) RunAsGid);
	}

	/* save command line arguments */
	j = 0;
	for (av = argv; *av != NULL; )
		j += strlen(*av++) + 1;
	SaveArgv = (char **) xalloc(sizeof(char *) * (argc + 1));
	CommandLineArgs = xalloc(j);
	p = CommandLineArgs;
	for (av = argv, i = 0; *av != NULL; )
	{
		int h;

		SaveArgv[i++] = newstr(*av);
		if (av != argv)
			*p++ = ' ';
		(void) sm_strlcpy(p, *av++, j);
		h = strlen(p);
		p += h;
		j -= h + 1;
	}
	SaveArgv[i] = NULL;

	if (tTd(0, 1))
	{
		extern char *CompileOptions[];

		sm_dprintf("Version %s\n Compiled with:", Version);
		sm_printoptions(CompileOptions);
	}
	if (tTd(0, 10))
	{
		extern char *OsCompileOptions[];

		sm_dprintf("    OS Defines:");
		sm_printoptions(OsCompileOptions);
#ifdef _PATH_UNIX
		sm_dprintf("Kernel symbols:\t%s\n", _PATH_UNIX);
#endif /* _PATH_UNIX */

		sm_dprintf("     Conf file:\t%s (default for MSP)\n",
			   getcfname(OpMode, SubmitMode, SM_GET_SUBMIT_CF,
				     conffile));
		sm_dprintf("     Conf file:\t%s (default for MTA)\n",
			   getcfname(OpMode, SubmitMode, SM_GET_SENDMAIL_CF,
				     conffile));
		sm_dprintf("      Pid file:\t%s (default)\n", PidFile);
	}

	if (tTd(0, 12))
	{
		extern char *SmCompileOptions[];

		sm_dprintf(" libsm Defines:");
		sm_printoptions(SmCompileOptions);
	}

	if (tTd(0, 13))
	{
		extern char *FFRCompileOptions[];

		sm_dprintf("   FFR Defines:");
		sm_printoptions(FFRCompileOptions);
	}

#if STARTTLS
	if (tTd(0, 14))
	{
		/* exit(EX_CONFIG) if different? */
		sm_dprintf("       OpenSSL: compiled 0x%08x\n",
			   (uint) OPENSSL_VERSION_NUMBER);
		sm_dprintf("       OpenSSL: linked   0x%08x\n",
			   (uint) SSLeay());
	}
#endif /* STARTTLS */

	/* clear sendmail's environment */
	ExternalEnviron = environ;
	emptyenviron[0] = NULL;
	environ = emptyenviron;

	/*
	**  restore any original TZ setting until TimeZoneSpec has been
	**  determined - or early log messages may get bogus time stamps
	*/

	if ((p = getextenv("TZ")) != NULL)
	{
		char *tz;
		int tzlen;

		/* XXX check for reasonable length? */
		tzlen = strlen(p) + 4;
		tz = xalloc(tzlen);
		(void) sm_strlcpyn(tz, tzlen, 2, "TZ=", p);

		/* XXX check return code? */
		(void) putenv(tz);
	}

	/* prime the child environment */
	sm_setuserenv("AGENT", "sendmail");

	(void) sm_signal(SIGPIPE, SIG_IGN);
	OldUmask = umask(022);
	FullName = getextenv("NAME");
	if (FullName != NULL)
		FullName = newstr(FullName);

	/*
	**  Initialize name server if it is going to be used.
	*/

#if NAMED_BIND
	if (!bitset(RES_INIT, _res.options))
		(void) res_init();
	if (tTd(8, 8))
		_res.options |= RES_DEBUG;
	else
		_res.options &= ~RES_DEBUG;
# ifdef RES_NOALIASES
	_res.options |= RES_NOALIASES;
# endif /* RES_NOALIASES */
	TimeOuts.res_retry[RES_TO_DEFAULT] = _res.retry;
	TimeOuts.res_retry[RES_TO_FIRST] = _res.retry;
	TimeOuts.res_retry[RES_TO_NORMAL] = _res.retry;
	TimeOuts.res_retrans[RES_TO_DEFAULT] = _res.retrans;
	TimeOuts.res_retrans[RES_TO_FIRST] = _res.retrans;
	TimeOuts.res_retrans[RES_TO_NORMAL] = _res.retrans;
#endif /* NAMED_BIND */

	errno = 0;
	from = NULL;

	/* initialize some macros, etc. */
	init_vendor_macros(&BlankEnvelope);

	/* version */
	macdefine(&BlankEnvelope.e_macro, A_PERM, 'v', Version);

	/* hostname */
	hp = myhostname(jbuf, sizeof(jbuf));
	if (jbuf[0] != '\0')
	{
		struct utsname utsname;

		if (tTd(0, 4))
			sm_dprintf("Canonical name: %s\n", jbuf);
		macdefine(&BlankEnvelope.e_macro, A_TEMP, 'w', jbuf);
		macdefine(&BlankEnvelope.e_macro, A_TEMP, 'j', jbuf);
		setclass('w', jbuf);

		p = strchr(jbuf, '.');
		if (p != NULL && p[1] != '\0')
			macdefine(&BlankEnvelope.e_macro, A_TEMP, 'm', &p[1]);

		if (uname(&utsname) >= 0)
			p = utsname.nodename;
		else
		{
			if (tTd(0, 22))
				sm_dprintf("uname failed (%s)\n",
					   sm_errstring(errno));
			makelower(jbuf);
			p = jbuf;
		}
		if (tTd(0, 4))
			sm_dprintf(" UUCP nodename: %s\n", p);
		macdefine(&BlankEnvelope.e_macro, A_TEMP, 'k', p);
		setclass('k', p);
		setclass('w', p);
	}
	if (hp != NULL)
	{
		for (av = hp->h_aliases; av != NULL && *av != NULL; av++)
		{
			if (tTd(0, 4))
				sm_dprintf("\ta.k.a.: %s\n", *av);
			setclass('w', *av);
		}
#if NETINET || NETINET6
		for (i = 0; i >= 0 && hp->h_addr_list[i] != NULL; i++)
		{
# if NETINET6
			char *addr;
			char buf6[INET6_ADDRSTRLEN];
			struct in6_addr ia6;
# endif /* NETINET6 */
# if NETINET
			struct in_addr ia;
# endif /* NETINET */
			char ipbuf[103];

			ipbuf[0] = '\0';
			switch (hp->h_addrtype)
			{
# if NETINET
			  case AF_INET:
				if (hp->h_length != INADDRSZ)
					break;

				memmove(&ia, hp->h_addr_list[i], INADDRSZ);
				(void) sm_snprintf(ipbuf, sizeof(ipbuf),
						   "[%.100s]", inet_ntoa(ia));
				break;
# endif /* NETINET */

# if NETINET6
			  case AF_INET6:
				if (hp->h_length != IN6ADDRSZ)
					break;

				memmove(&ia6, hp->h_addr_list[i], IN6ADDRSZ);
				addr = anynet_ntop(&ia6, buf6, sizeof(buf6));
				if (addr != NULL)
					(void) sm_snprintf(ipbuf, sizeof(ipbuf),
							   "[%.100s]", addr);
				break;
# endif /* NETINET6 */
			}
			if (ipbuf[0] == '\0')
				break;

			if (tTd(0, 4))
				sm_dprintf("\ta.k.a.: %s\n", ipbuf);
			setclass('w', ipbuf);
		}
#endif /* NETINET || NETINET6 */
#if NETINET6
		freehostent(hp);
		hp = NULL;
#endif /* NETINET6 */
	}

	/* current time */
	macdefine(&BlankEnvelope.e_macro, A_TEMP, 'b', arpadate((char *) NULL));

	/* current load average */
	sm_getla();

	QueueLimitRecipient = (QUEUE_CHAR *) NULL;
	QueueLimitSender = (QUEUE_CHAR *) NULL;
	QueueLimitId = (QUEUE_CHAR *) NULL;
	QueueLimitQuarantine = (QUEUE_CHAR *) NULL;

	/*
	**  Crack argv.
	*/

	optind = 1;
	while ((j = getopt(argc, argv, OPTIONS)) != -1)
	{
		switch (j)
		{
		  case 'b':	/* operations mode */
			/* already done */
			break;

		  case 'A':	/* use Alternate sendmail/submit.cf */
			cftype = optarg[0] == 'c' ? SM_GET_SUBMIT_CF
						  : SM_GET_SENDMAIL_CF;
			break;

		  case 'B':	/* body type */
			CHECK_AGAINST_OPMODE(j);
			BlankEnvelope.e_bodytype = newstr(optarg);
			break;

		  case 'C':	/* select configuration file (already done) */
			if (RealUid != 0)
				warn_C_flag = true;
			conffile = newstr(optarg);
			dp = drop_privileges(true);
			setstat(dp);
			safecf = false;
			break;

		  case 'D':
		  case 'd':	/* debugging */
			/* already done */
			break;

		  case 'f':	/* from address */
		  case 'r':	/* obsolete -f flag */
			CHECK_AGAINST_OPMODE(j);
			if (from != NULL)
			{
				usrerr("More than one \"from\" person");
				ExitStat = EX_USAGE;
				break;
			}
			if (optarg[0] == '\0')
				from = newstr("<>");
			else
				from = newstr(denlstring(optarg, true, true));
			if (strcmp(RealUserName, from) != 0)
				warn_f_flag = j;
			break;

		  case 'F':	/* set full name */
			CHECK_AGAINST_OPMODE(j);
			FullName = newstr(optarg);
			break;

		  case 'G':	/* relay (gateway) submission */
			/* already set */
			CHECK_AGAINST_OPMODE(j);
			break;

		  case 'h':	/* hop count */
			CHECK_AGAINST_OPMODE(j);
			BlankEnvelope.e_hopcount = (short) strtol(optarg, &ep,
								  10);
			(void) sm_snprintf(buf, sizeof(buf), "%d",
					   BlankEnvelope.e_hopcount);
			macdefine(&BlankEnvelope.e_macro, A_TEMP, 'c', buf);

			if (*ep)
			{
				usrerr("Bad hop count (%s)", optarg);
				ExitStat = EX_USAGE;
			}
			break;

		  case 'L':	/* program label */
			/* already set */
			break;

		  case 'n':	/* don't alias */
			CHECK_AGAINST_OPMODE(j);
			NoAlias = true;
			break;

		  case 'N':	/* delivery status notifications */
			CHECK_AGAINST_OPMODE(j);
			DefaultNotify |= QHASNOTIFY;
			macdefine(&BlankEnvelope.e_macro, A_TEMP,
				macid("{dsn_notify}"), optarg);
			if (sm_strcasecmp(optarg, "never") == 0)
				break;
			for (p = optarg; p != NULL; optarg = p)
			{
				p = strchr(p, ',');
				if (p != NULL)
					*p++ = '\0';
				if (sm_strcasecmp(optarg, "success") == 0)
					DefaultNotify |= QPINGONSUCCESS;
				else if (sm_strcasecmp(optarg, "failure") == 0)
					DefaultNotify |= QPINGONFAILURE;
				else if (sm_strcasecmp(optarg, "delay") == 0)
					DefaultNotify |= QPINGONDELAY;
				else
				{
					usrerr("Invalid -N argument");
					ExitStat = EX_USAGE;
				}
			}
			break;

		  case 'o':	/* set option */
			setoption(*optarg, optarg + 1, false, true,
				  &BlankEnvelope);
			break;

		  case 'O':	/* set option (long form) */
			setoption(' ', optarg, false, true, &BlankEnvelope);
			break;

		  case 'p':	/* set protocol */
			CHECK_AGAINST_OPMODE(j);
			p = strchr(optarg, ':');
			if (p != NULL)
			{
				*p++ = '\0';
				if (*p != '\0')
				{
					i = strlen(p) + 1;
					ep = sm_malloc_x(i);
					cleanstrcpy(ep, p, i);
					macdefine(&BlankEnvelope.e_macro,
						  A_HEAP, 's', ep);
				}
			}
			if (*optarg != '\0')
			{
				i = strlen(optarg) + 1;
				ep = sm_malloc_x(i);
				cleanstrcpy(ep, optarg, i);
				macdefine(&BlankEnvelope.e_macro, A_HEAP,
					  'r', ep);
			}
			break;

		  case 'Q':	/* change quarantining on queued items */
			/* sanity check */
			if (OpMode != MD_DELIVER &&
			    OpMode != MD_QUEUERUN)
			{
				usrerr("Can not use -Q with -b%c", OpMode);
				ExitStat = EX_USAGE;
				break;
			}

			if (OpMode == MD_DELIVER)
				set_op_mode(MD_QUEUERUN);

			FullName = NULL;

			quarantining = newstr(optarg);
			break;

		  case 'q':	/* run queue files at intervals */
			/* sanity check */
			if (OpMode != MD_DELIVER &&
			    OpMode != MD_DAEMON &&
			    OpMode != MD_FGDAEMON &&
			    OpMode != MD_PRINT &&
			    OpMode != MD_PRINTNQE &&
			    OpMode != MD_QUEUERUN)
			{
				usrerr("Can not use -q with -b%c", OpMode);
				ExitStat = EX_USAGE;
				break;
			}

			/* don't override -bd, -bD or -bp */
			if (OpMode == MD_DELIVER)
				set_op_mode(MD_QUEUERUN);

			FullName = NULL;
			negate = optarg[0] == '!';
			if (negate)
			{
				/* negate meaning of pattern match */
				optarg++; /* skip '!' for next switch */
			}

			switch (optarg[0])
			{
			  case 'G': /* Limit by queue group name */
				if (negate)
				{
					usrerr("Can not use -q!G");
					ExitStat = EX_USAGE;
					break;
				}
				if (queuegroup != NULL)
				{
					usrerr("Can not use multiple -qG options");
					ExitStat = EX_USAGE;
					break;
				}
				queuegroup = newstr(&optarg[1]);
				break;

			  case 'I': /* Limit by ID */
				new = (QUEUE_CHAR *) xalloc(sizeof(*new));
				new->queue_match = newstr(&optarg[1]);
				new->queue_negate = negate;
				new->queue_next = QueueLimitId;
				QueueLimitId = new;
				break;

			  case 'R': /* Limit by recipient */
				new = (QUEUE_CHAR *) xalloc(sizeof(*new));
				new->queue_match = newstr(&optarg[1]);
				new->queue_negate = negate;
				new->queue_next = QueueLimitRecipient;
				QueueLimitRecipient = new;
				break;

			  case 'S': /* Limit by sender */
				new = (QUEUE_CHAR *) xalloc(sizeof(*new));
				new->queue_match = newstr(&optarg[1]);
				new->queue_negate = negate;
				new->queue_next = QueueLimitSender;
				QueueLimitSender = new;
				break;

			  case 'f': /* foreground queue run */
				foregroundqueue  = true;
				break;

			  case 'Q': /* Limit by quarantine message */
				if (optarg[1] != '\0')
				{
					new = (QUEUE_CHAR *) xalloc(sizeof(*new));
					new->queue_match = newstr(&optarg[1]);
					new->queue_negate = negate;
					new->queue_next = QueueLimitQuarantine;
					QueueLimitQuarantine = new;
				}
				QueueMode = QM_QUARANTINE;
				break;

			  case 'L': /* act on lost items */
				QueueMode = QM_LOST;
				break;

			  case 'p': /* Persistent queue */
				queuepersistent = true;
				if (QueueIntvl == 0)
					QueueIntvl = 1;
				if (optarg[1] == '\0')
					break;
				++optarg;
				/* FALLTHROUGH */

			  default:
				i = Errors;
				QueueIntvl = convtime(optarg, 'm');
				if (QueueIntvl < 0)
				{
					usrerr("Invalid -q value");
					ExitStat = EX_USAGE;
				}

				/* check for bad conversion */
				if (i < Errors)
					ExitStat = EX_USAGE;
				break;
			}
			break;

		  case 'R':	/* DSN RET: what to return */
			CHECK_AGAINST_OPMODE(j);
			if (bitset(EF_RET_PARAM, BlankEnvelope.e_flags))
			{
				usrerr("Duplicate -R flag");
				ExitStat = EX_USAGE;
				break;
			}
			BlankEnvelope.e_flags |= EF_RET_PARAM;
			if (sm_strcasecmp(optarg, "hdrs") == 0)
				BlankEnvelope.e_flags |= EF_NO_BODY_RETN;
			else if (sm_strcasecmp(optarg, "full") != 0)
			{
				usrerr("Invalid -R value");
				ExitStat = EX_USAGE;
			}
			macdefine(&BlankEnvelope.e_macro, A_TEMP,
				  macid("{dsn_ret}"), optarg);
			break;

		  case 't':	/* read recipients from message */
			CHECK_AGAINST_OPMODE(j);
			GrabTo = true;
			break;

		  case 'V':	/* DSN ENVID: set "original" envelope id */
			CHECK_AGAINST_OPMODE(j);
			if (!xtextok(optarg))
			{
				usrerr("Invalid syntax in -V flag");
				ExitStat = EX_USAGE;
			}
			else
			{
				BlankEnvelope.e_envid = newstr(optarg);
				macdefine(&BlankEnvelope.e_macro, A_TEMP,
					  macid("{dsn_envid}"), optarg);
			}
			break;

		  case 'X':	/* traffic log file */
			dp = drop_privileges(true);
			setstat(dp);
			if (stat(optarg, &traf_st) == 0 &&
			    S_ISFIFO(traf_st.st_mode))
				TrafficLogFile = sm_io_open(SmFtStdio,
							    SM_TIME_DEFAULT,
							    optarg,
							    SM_IO_WRONLY, NULL);
			else
				TrafficLogFile = sm_io_open(SmFtStdio,
							    SM_TIME_DEFAULT,
							    optarg,
							    SM_IO_APPEND, NULL);
			if (TrafficLogFile == NULL)
			{
				syserr("cannot open %s", optarg);
				ExitStat = EX_CANTCREAT;
				break;
			}
			(void) sm_io_setvbuf(TrafficLogFile, SM_TIME_DEFAULT,
					     NULL, SM_IO_LBF, 0);
			break;

			/* compatibility flags */
		  case 'c':	/* connect to non-local mailers */
		  case 'i':	/* don't let dot stop me */
		  case 'm':	/* send to me too */
		  case 'T':	/* set timeout interval */
		  case 'v':	/* give blow-by-blow description */
			setoption(j, "T", false, true, &BlankEnvelope);
			break;

		  case 'e':	/* error message disposition */
		  case 'M':	/* define macro */
			setoption(j, optarg, false, true, &BlankEnvelope);
			break;

		  case 's':	/* save From lines in headers */
			setoption('f', "T", false, true, &BlankEnvelope);
			break;

#ifdef DBM
		  case 'I':	/* initialize alias DBM file */
			set_op_mode(MD_INITALIAS);
			break;
#endif /* DBM */

#if defined(__osf__) || defined(_AIX3)
		  case 'x':	/* random flag that OSF/1 & AIX mailx passes */
			break;
#endif /* defined(__osf__) || defined(_AIX3) */
#if defined(sony_news)
		  case 'E':
		  case 'J':	/* ignore flags for Japanese code conversion
				   implemented on Sony NEWS */
			break;
#endif /* defined(sony_news) */

		  default:
			finis(true, true, EX_USAGE);
			/* NOTREACHED */
			break;
		}
	}

	/* if we've had errors so far, exit now */
	if ((ExitStat != EX_OK && OpMode != MD_TEST && OpMode != MD_CHECKCONFIG) ||
	    ExitStat == EX_OSERR)
	{
		finis(false, true, ExitStat);
		/* NOTREACHED */
	}

	if (bitset(SUBMIT_MTA, SubmitMode))
	{
		/* If set daemon_flags on command line, don't reset it */
		if (macvalue(macid("{daemon_flags}"), &BlankEnvelope) == NULL)
			macdefine(&BlankEnvelope.e_macro, A_PERM,
				  macid("{daemon_flags}"), "CC f");
	}
	else if (OpMode == MD_DELIVER || OpMode == MD_SMTP)
	{
		SubmitMode = SUBMIT_MSA;

		/* If set daemon_flags on command line, don't reset it */
		if (macvalue(macid("{daemon_flags}"), &BlankEnvelope) == NULL)
			macdefine(&BlankEnvelope.e_macro, A_PERM,
				  macid("{daemon_flags}"), "c u");
	}

	/*
	**  Do basic initialization.
	**	Read system control file.
	**	Extract special fields for local use.
	*/

#if XDEBUG
	checkfd012("before readcf");
#endif /* XDEBUG */
	vendor_pre_defaults(&BlankEnvelope);

	readcf(getcfname(OpMode, SubmitMode, cftype, conffile),
			 safecf, &BlankEnvelope);
#if !defined(_USE_SUN_NSSWITCH_) && !defined(_USE_DEC_SVC_CONF_)
	ConfigFileRead = true;
#endif /* !defined(_USE_SUN_NSSWITCH_) && !defined(_USE_DEC_SVC_CONF_) */
	vendor_post_defaults(&BlankEnvelope);

	/* now we can complain about missing fds */
	if (MissingFds != 0 && LogLevel > 8)
	{
		char mbuf[MAXLINE];

		mbuf[0] = '\0';
		if (bitset(1 << STDIN_FILENO, MissingFds))
			(void) sm_strlcat(mbuf, ", stdin", sizeof(mbuf));
		if (bitset(1 << STDOUT_FILENO, MissingFds))
			(void) sm_strlcat(mbuf, ", stdout", sizeof(mbuf));
		if (bitset(1 << STDERR_FILENO, MissingFds))
			(void) sm_strlcat(mbuf, ", stderr", sizeof(mbuf));

		/* Notice: fill_errno is from high above: fill_fd() */
		sm_syslog(LOG_WARNING, NOQID,
			  "File descriptors missing on startup: %s; %s",
			  &mbuf[2], sm_errstring(fill_errno));
	}

	/* Remove the ability for a normal user to send signals */
	if (RealUid != 0 && RealUid != geteuid())
	{
		uid_t new_uid = geteuid();

#if HASSETREUID
		/*
		**  Since we can differentiate between uid and euid,
		**  make the uid a different user so the real user
		**  can't send signals.  However, it doesn't need to be
		**  root (euid has root).
		*/

		if (new_uid == 0)
			new_uid = DefUid;
		if (tTd(47, 5))
			sm_dprintf("Changing real uid to %d\n", (int) new_uid);
		if (setreuid(new_uid, geteuid()) < 0)
		{
			syserr("main: setreuid(%d, %d) failed",
			       (int) new_uid, (int) geteuid());
			finis(false, true, EX_OSERR);
			/* NOTREACHED */
		}
		if (tTd(47, 10))
			sm_dprintf("Now running as e/ruid %d:%d\n",
				   (int) geteuid(), (int) getuid());
#else /* HASSETREUID */
		/*
		**  Have to change both effective and real so need to
		**  change them both to effective to keep privs.
		*/

		if (tTd(47, 5))
			sm_dprintf("Changing uid to %d\n", (int) new_uid);
		if (setuid(new_uid) < 0)
		{
			syserr("main: setuid(%d) failed", (int) new_uid);
			finis(false, true, EX_OSERR);
			/* NOTREACHED */
		}
		if (tTd(47, 10))
			sm_dprintf("Now running as e/ruid %d:%d\n",
				   (int) geteuid(), (int) getuid());
#endif /* HASSETREUID */
	}

#if NAMED_BIND
	if (FallbackMX != NULL)
		(void) getfallbackmxrr(FallbackMX);
#endif /* NAMED_BIND */

	if (SuperSafe == SAFE_INTERACTIVE && !SM_IS_INTERACTIVE(CurEnv->e_sendmode))
	{
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "WARNING: SuperSafe=interactive should only be used with\n         DeliveryMode=interactive\n");
	}

	if (UseMSP && (OpMode == MD_DAEMON || OpMode == MD_FGDAEMON))
	{
		usrerr("Mail submission program cannot be used as daemon");
		finis(false, true, EX_USAGE);
	}

	if (OpMode == MD_DELIVER || OpMode == MD_SMTP ||
	    OpMode == MD_QUEUERUN || OpMode == MD_ARPAFTP ||
	    OpMode == MD_DAEMON || OpMode == MD_FGDAEMON)
		makeworkgroups();

	/* set up the basic signal handlers */
	if (sm_signal(SIGINT, SIG_IGN) != SIG_IGN)
		(void) sm_signal(SIGINT, intsig);
	(void) sm_signal(SIGTERM, intsig);

	/* Enforce use of local time (null string overrides this) */
	if (TimeZoneSpec == NULL)
		unsetenv("TZ");
	else if (TimeZoneSpec[0] != '\0')
		sm_setuserenv("TZ", TimeZoneSpec);
	else
		sm_setuserenv("TZ", NULL);
	tzset();

	/* initialize mailbox database */
	i = sm_mbdb_initialize(Mbdb);
	if (i != EX_OK)
	{
		usrerr("Can't initialize mailbox database \"%s\": %s",
		       Mbdb, sm_strexit(i));
		ExitStat = i;
	}

	/* avoid denial-of-service attacks */
	resetlimits();

	if (OpMode == MD_TEST)
	{
		/* can't be done after readcf if RunAs* is used */
		dp = drop_privileges(true);
		if (dp != EX_OK)
		{
			finis(false, true, dp);
			/* NOTREACHED */
		}
	}
	else if (OpMode != MD_DAEMON && OpMode != MD_FGDAEMON)
	{
		/* drop privileges -- daemon mode done after socket/bind */
		dp = drop_privileges(false);
		setstat(dp);
		if (dp == EX_OK && UseMSP && (geteuid() == 0 || getuid() == 0))
		{
			usrerr("Mail submission program must have RunAsUser set to non root user");
			finis(false, true, EX_CONFIG);
			/* NOTREACHED */
		}
	}

#if NAMED_BIND
	_res.retry = TimeOuts.res_retry[RES_TO_DEFAULT];
	_res.retrans = TimeOuts.res_retrans[RES_TO_DEFAULT];
#endif /* NAMED_BIND */

	/*
	**  Find our real host name for future logging.
	*/

	authinfo = getauthinfo(STDIN_FILENO, &forged);
	macdefine(&BlankEnvelope.e_macro, A_TEMP, '_', authinfo);

	/* suppress error printing if errors mailed back or whatever */
	if (BlankEnvelope.e_errormode != EM_PRINT)
		HoldErrs = true;

	/* set up the $=m class now, after .cf has a chance to redefine $m */
	expand("\201m", jbuf, sizeof(jbuf), &BlankEnvelope);
	if (jbuf[0] != '\0')
		setclass('m', jbuf);

	/* probe interfaces and locate any additional names */
	if (DontProbeInterfaces != DPI_PROBENONE)
		load_if_names();

	if (tTd(0, 10))
	{
		char pidpath[MAXPATHLEN];

		/* Now we know which .cf file we use */
		sm_dprintf("     Conf file:\t%s (selected)\n",
			   getcfname(OpMode, SubmitMode, cftype, conffile));
		expand(PidFile, pidpath, sizeof(pidpath), &BlankEnvelope);
		sm_dprintf("      Pid file:\t%s (selected)\n", pidpath);
	}

	if (tTd(0, 1))
	{
		sm_dprintf("\n============ SYSTEM IDENTITY (after readcf) ============");
		sm_dprintf("\n      (short domain name) $w = ");
		xputs(sm_debug_file(), macvalue('w', &BlankEnvelope));
		sm_dprintf("\n  (canonical domain name) $j = ");
		xputs(sm_debug_file(), macvalue('j', &BlankEnvelope));
		sm_dprintf("\n         (subdomain name) $m = ");
		xputs(sm_debug_file(), macvalue('m', &BlankEnvelope));
		sm_dprintf("\n              (node name) $k = ");
		xputs(sm_debug_file(), macvalue('k', &BlankEnvelope));
		sm_dprintf("\n========================================================\n\n");
	}

	/*
	**  Do more command line checking -- these are things that
	**  have to modify the results of reading the config file.
	*/

	/* process authorization warnings from command line */
	if (warn_C_flag)
		auth_warning(&BlankEnvelope, "Processed by %s with -C %s",
			     RealUserName, conffile);
	if (Warn_Q_option && !wordinclass(RealUserName, 't'))
		auth_warning(&BlankEnvelope, "Processed from queue %s",
			     QueueDir);
	if (sysloglabel != NULL && !wordinclass(RealUserName, 't') &&
	    RealUid != 0 && RealUid != TrustedUid && LogLevel > 1)
		sm_syslog(LOG_WARNING, NOQID, "user %d changed syslog label",
			  (int) RealUid);

	/* check body type for legality */
	i = check_bodytype(BlankEnvelope.e_bodytype);
	if (i == BODYTYPE_ILLEGAL)
	{
		usrerr("Illegal body type %s", BlankEnvelope.e_bodytype);
		BlankEnvelope.e_bodytype = NULL;
	}
	else if (i != BODYTYPE_NONE)
		SevenBitInput = (i == BODYTYPE_7BIT);

	/* tweak default DSN notifications */
	if (DefaultNotify == 0)
		DefaultNotify = QPINGONFAILURE|QPINGONDELAY;

	/* check for sane configuration level */
	if (ConfigLevel > MAXCONFIGLEVEL)
	{
		syserr("Warning: .cf version level (%d) exceeds sendmail version %s functionality (%d)",
		       ConfigLevel, Version, MAXCONFIGLEVEL);
	}

	/* need MCI cache to have persistence */
	if (HostStatDir != NULL && MaxMciCache == 0)
	{
		HostStatDir = NULL;
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Warning: HostStatusDirectory disabled with ConnectionCacheSize = 0\n");
	}

	/* need HostStatusDir in order to have SingleThreadDelivery */
	if (SingleThreadDelivery && HostStatDir == NULL)
	{
		SingleThreadDelivery = false;
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Warning: HostStatusDirectory required for SingleThreadDelivery\n");
	}

#if _FFR_MEMSTAT
	j = sm_memstat_open();
	if (j < 0 && (RefuseLowMem > 0 || QueueLowMem > 0) && LogLevel > 4)
	{
		sm_syslog(LOG_WARNING, NOQID,
			  "cannot get memory statistics, settings ignored, error=%d"
			  , j);
	}
#endif /* _FFR_MEMSTAT */

	/* check for permissions */
	if (RealUid != 0 &&
	    RealUid != TrustedUid)
	{
		char *action = NULL;

		switch (OpMode)
		{
		  case MD_QUEUERUN:
			if (quarantining != NULL)
				action = "quarantine jobs";
			else
			{
				/* Normal users can do a single queue run */
				if (QueueIntvl == 0)
					break;
			}

			/* but not persistent queue runners */
			if (action == NULL)
				action = "start a queue runner daemon";
			/* FALLTHROUGH */

		  case MD_PURGESTAT:
			if (action == NULL)
				action = "purge host status";
			/* FALLTHROUGH */

		  case MD_DAEMON:
		  case MD_FGDAEMON:
			if (action == NULL)
				action = "run daemon";

			if (tTd(65, 1))
				sm_dprintf("Deny user %d attempt to %s\n",
					   (int) RealUid, action);

			if (LogLevel > 1)
				sm_syslog(LOG_ALERT, NOQID,
					  "user %d attempted to %s",
					  (int) RealUid, action);
			HoldErrs = false;
			usrerr("Permission denied (real uid not trusted)");
			finis(false, true, EX_USAGE);
			/* NOTREACHED */
			break;

		  case MD_VERIFY:
			if (bitset(PRIV_RESTRICTEXPAND, PrivacyFlags))
			{
				/*
				**  If -bv and RestrictExpand,
				**  drop privs to prevent normal
				**  users from reading private
				**  aliases/forwards/:include:s
				*/

				if (tTd(65, 1))
					sm_dprintf("Drop privs for user %d attempt to expand (RestrictExpand)\n",
						   (int) RealUid);

				dp = drop_privileges(true);

				/* Fake address safety */
				if (tTd(65, 1))
					sm_dprintf("Faking DontBlameSendmail=NonRootSafeAddr\n");
				setbitn(DBS_NONROOTSAFEADDR, DontBlameSendmail);

				if (dp != EX_OK)
				{
					if (tTd(65, 1))
						sm_dprintf("Failed to drop privs for user %d attempt to expand, exiting\n",
							   (int) RealUid);
					CurEnv->e_id = NULL;
					finis(true, true, dp);
					/* NOTREACHED */
				}
			}
			break;

		  case MD_TEST:
		  case MD_CHECKCONFIG:
		  case MD_PRINT:
		  case MD_PRINTNQE:
		  case MD_FREEZE:
		  case MD_HOSTSTAT:
			/* Nothing special to check */
			break;

		  case MD_INITALIAS:
			if (!wordinclass(RealUserName, 't'))
			{
				if (tTd(65, 1))
					sm_dprintf("Deny user %d attempt to rebuild the alias map\n",
						   (int) RealUid);
				if (LogLevel > 1)
					sm_syslog(LOG_ALERT, NOQID,
						  "user %d attempted to rebuild the alias map",
						  (int) RealUid);
				HoldErrs = false;
				usrerr("Permission denied (real uid not trusted)");
				finis(false, true, EX_USAGE);
				/* NOTREACHED */
			}
			if (UseMSP)
			{
				HoldErrs = false;
				usrerr("User %d cannot rebuild aliases in mail submission program",
				       (int) RealUid);
				finis(false, true, EX_USAGE);
				/* NOTREACHED */
			}
			/* FALLTHROUGH */

		  default:
			if (bitset(PRIV_RESTRICTEXPAND, PrivacyFlags) &&
			    Verbose != 0)
			{
				/*
				**  If -v and RestrictExpand, reset
				**  Verbose to prevent normal users
				**  from seeing the expansion of
				**  aliases/forwards/:include:s
				*/

				if (tTd(65, 1))
					sm_dprintf("Dropping verbosity for user %d (RestrictExpand)\n",
						   (int) RealUid);
				Verbose = 0;
			}
			break;
		}
	}

	if (MeToo)
		BlankEnvelope.e_flags |= EF_METOO;

	switch (OpMode)
	{
	  case MD_TEST:
		/* don't have persistent host status in test mode */
		HostStatDir = NULL;
		/* FALLTHROUGH */

	  case MD_CHECKCONFIG:
		if (Verbose == 0)
			Verbose = 2;
		BlankEnvelope.e_errormode = EM_PRINT;
		HoldErrs = false;
		break;

	  case MD_VERIFY:
		BlankEnvelope.e_errormode = EM_PRINT;
		HoldErrs = false;
		/* arrange to exit cleanly on hangup signal */
		if (sm_signal(SIGHUP, SIG_IGN) == (sigfunc_t) SIG_DFL)
			(void) sm_signal(SIGHUP, intsig);
		if (geteuid() != 0)
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Notice: -bv may give misleading output for non-privileged user\n");
		break;

	  case MD_FGDAEMON:
		run_in_foreground = true;
		set_op_mode(MD_DAEMON);
		/* FALLTHROUGH */

	  case MD_DAEMON:
		vendor_daemon_setup(&BlankEnvelope);

		/* remove things that don't make sense in daemon mode */
		FullName = NULL;
		GrabTo = false;

		/* arrange to restart on hangup signal */
		if (SaveArgv[0] == NULL || SaveArgv[0][0] != '/')
			sm_syslog(LOG_WARNING, NOQID,
				  "daemon invoked without full pathname; kill -1 won't work");
		break;

	  case MD_INITALIAS:
		Verbose = 2;
		BlankEnvelope.e_errormode = EM_PRINT;
		HoldErrs = false;
		/* FALLTHROUGH */

	  default:
		/* arrange to exit cleanly on hangup signal */
		if (sm_signal(SIGHUP, SIG_IGN) == (sigfunc_t) SIG_DFL)
			(void) sm_signal(SIGHUP, intsig);
		break;
	}

	/* special considerations for FullName */
	if (FullName != NULL)
	{
		char *full = NULL;

		/* full names can't have newlines */
		if (strchr(FullName, '\n') != NULL)
		{
			full = newstr(denlstring(FullName, true, true));
			FullName = full;
		}

		/* check for characters that may have to be quoted */
		if (!rfc822_string(FullName))
		{
			/*
			**  Quote a full name with special characters
			**  as a comment so crackaddr() doesn't destroy
			**  the name portion of the address.
			*/

			FullName = addquotes(FullName, NULL);
			if (full != NULL)
				sm_free(full);  /* XXX */
		}
	}

	/* do heuristic mode adjustment */
	if (Verbose)
	{
		/* turn off noconnect option */
		setoption('c', "F", true, false, &BlankEnvelope);

		/* turn on interactive delivery */
		setoption('d', "", true, false, &BlankEnvelope);
	}

#ifdef VENDOR_CODE
	/* check for vendor mismatch */
	if (VendorCode != VENDOR_CODE)
	{
		message("Warning: .cf file vendor code mismatch: sendmail expects vendor %s, .cf file vendor is %s",
			getvendor(VENDOR_CODE), getvendor(VendorCode));
	}
#endif /* VENDOR_CODE */

	/* check for out of date configuration level */
	if (ConfigLevel < MAXCONFIGLEVEL)
	{
		message("Warning: .cf file is out of date: sendmail %s supports version %d, .cf file is version %d",
			Version, MAXCONFIGLEVEL, ConfigLevel);
	}

	if (ConfigLevel < 3)
		UseErrorsTo = true;

	/* set options that were previous macros */
	if (SmtpGreeting == NULL)
	{
		if (ConfigLevel < 7 &&
		    (p = macvalue('e', &BlankEnvelope)) != NULL)
			SmtpGreeting = newstr(p);
		else
			SmtpGreeting = "\201j Sendmail \201v ready at \201b";
	}
	if (UnixFromLine == NULL)
	{
		if (ConfigLevel < 7 &&
		    (p = macvalue('l', &BlankEnvelope)) != NULL)
			UnixFromLine = newstr(p);
		else
			UnixFromLine = "From \201g  \201d";
	}
	SmtpError[0] = '\0';

	/* our name for SMTP codes */
	expand("\201j", jbuf, sizeof(jbuf), &BlankEnvelope);
	if (jbuf[0] == '\0')
		PSTRSET(MyHostName, "localhost");
	else
		PSTRSET(MyHostName, jbuf);
	if (strchr(MyHostName, '.') == NULL)
		message("WARNING: local host name (%s) is not qualified; see cf/README: WHO AM I?",
			MyHostName);

	/* make certain that this name is part of the $=w class */
	setclass('w', MyHostName);

	/* fill in the structure of the *default* queue */
	st = stab("mqueue", ST_QUEUE, ST_FIND);
	if (st == NULL)
		syserr("No default queue (mqueue) defined");
	else
		set_def_queueval(st->s_quegrp, true);

	/* the indices of built-in mailers */
	st = stab("local", ST_MAILER, ST_FIND);
	if (st != NULL)
		LocalMailer = st->s_mailer;
	else if (OpMode != MD_TEST || !warn_C_flag)
		syserr("No local mailer defined");

	st = stab("prog", ST_MAILER, ST_FIND);
	if (st == NULL)
		syserr("No prog mailer defined");
	else
	{
		ProgMailer = st->s_mailer;
		clrbitn(M_MUSER, ProgMailer->m_flags);
	}

	st = stab("*file*", ST_MAILER, ST_FIND);
	if (st == NULL)
		syserr("No *file* mailer defined");
	else
	{
		FileMailer = st->s_mailer;
		clrbitn(M_MUSER, FileMailer->m_flags);
	}

	st = stab("*include*", ST_MAILER, ST_FIND);
	if (st == NULL)
		syserr("No *include* mailer defined");
	else
		InclMailer = st->s_mailer;

	if (ConfigLevel < 6)
	{
		/* heuristic tweaking of local mailer for back compat */
		if (LocalMailer != NULL)
		{
			setbitn(M_ALIASABLE, LocalMailer->m_flags);
			setbitn(M_HASPWENT, LocalMailer->m_flags);
			setbitn(M_TRYRULESET5, LocalMailer->m_flags);
			setbitn(M_CHECKINCLUDE, LocalMailer->m_flags);
			setbitn(M_CHECKPROG, LocalMailer->m_flags);
			setbitn(M_CHECKFILE, LocalMailer->m_flags);
			setbitn(M_CHECKUDB, LocalMailer->m_flags);
		}
		if (ProgMailer != NULL)
			setbitn(M_RUNASRCPT, ProgMailer->m_flags);
		if (FileMailer != NULL)
			setbitn(M_RUNASRCPT, FileMailer->m_flags);
	}
	if (ConfigLevel < 7)
	{
		if (LocalMailer != NULL)
			setbitn(M_VRFY250, LocalMailer->m_flags);
		if (ProgMailer != NULL)
			setbitn(M_VRFY250, ProgMailer->m_flags);
		if (FileMailer != NULL)
			setbitn(M_VRFY250, FileMailer->m_flags);
	}

	/* MIME Content-Types that cannot be transfer encoded */
	setclass('n', "multipart/signed");

	/* MIME message/xxx subtypes that can be treated as messages */
	setclass('s', "rfc822");

	/* MIME Content-Transfer-Encodings that can be encoded */
	setclass('e', "7bit");
	setclass('e', "8bit");
	setclass('e', "binary");

#ifdef USE_B_CLASS
	/* MIME Content-Types that should be treated as binary */
	setclass('b', "image");
	setclass('b', "audio");
	setclass('b', "video");
	setclass('b', "application/octet-stream");
#endif /* USE_B_CLASS */

	/* MIME headers which have fields to check for overflow */
	setclass(macid("{checkMIMEFieldHeaders}"), "content-disposition");
	setclass(macid("{checkMIMEFieldHeaders}"), "content-type");

	/* MIME headers to check for length overflow */
	setclass(macid("{checkMIMETextHeaders}"), "content-description");

	/* MIME headers to check for overflow and rebalance */
	setclass(macid("{checkMIMEHeaders}"), "content-disposition");
	setclass(macid("{checkMIMEHeaders}"), "content-id");
	setclass(macid("{checkMIMEHeaders}"), "content-transfer-encoding");
	setclass(macid("{checkMIMEHeaders}"), "content-type");
	setclass(macid("{checkMIMEHeaders}"), "mime-version");

	/* Macros to save in the queue file -- don't remove any */
	setclass(macid("{persistentMacros}"), "r");
	setclass(macid("{persistentMacros}"), "s");
	setclass(macid("{persistentMacros}"), "_");
	setclass(macid("{persistentMacros}"), "{if_addr}");
	setclass(macid("{persistentMacros}"), "{daemon_flags}");

	/* operate in queue directory */
	if (QueueDir == NULL || *QueueDir == '\0')
	{
		if (OpMode != MD_TEST)
		{
			syserr("QueueDirectory (Q) option must be set");
			ExitStat = EX_CONFIG;
		}
	}
	else
	{
		if (OpMode != MD_TEST)
			setup_queues(OpMode == MD_DAEMON);
	}

	/* check host status directory for validity */
	if (HostStatDir != NULL && !path_is_dir(HostStatDir, false))
	{
		/* cannot use this value */
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Warning: Cannot use HostStatusDirectory = %s: %s\n",
				     HostStatDir, sm_errstring(errno));
		HostStatDir = NULL;
	}

	if (OpMode == MD_QUEUERUN &&
	    RealUid != 0 && bitset(PRIV_RESTRICTQRUN, PrivacyFlags))
	{
		struct stat stbuf;

		/* check to see if we own the queue directory */
		if (stat(".", &stbuf) < 0)
			syserr("main: cannot stat %s", QueueDir);
		if (stbuf.st_uid != RealUid)
		{
			/* nope, really a botch */
			HoldErrs = false;
			usrerr("You do not have permission to process the queue");
			finis(false, true, EX_NOPERM);
			/* NOTREACHED */
		}
	}

#if MILTER
	/* sanity checks on milter filters */
	if (OpMode == MD_DAEMON || OpMode == MD_SMTP)
	{
		milter_config(InputFilterList, InputFilters, MAXFILTERS);
		setup_daemon_milters();
	}
#endif /* MILTER */

	/* Convert queuegroup string to qgrp number */
	if (queuegroup != NULL)
	{
		qgrp = name2qid(queuegroup);
		if (qgrp == NOQGRP)
		{
			HoldErrs = false;
			usrerr("Queue group %s unknown", queuegroup);
			finis(false, true, ExitStat);
			/* NOTREACHED */
		}
	}

	/* if checking config or have had errors so far, exit now */
	if (OpMode == MD_CHECKCONFIG || (ExitStat != EX_OK && OpMode != MD_TEST))
	{
		finis(false, true, ExitStat);
		/* NOTREACHED */
	}

#if SASL
	/* sendmail specific SASL initialization */
	sm_sasl_init();
#endif /* SASL */

#if XDEBUG
	checkfd012("before main() initmaps");
#endif /* XDEBUG */

	/*
	**  Do operation-mode-dependent initialization.
	*/

	switch (OpMode)
	{
	  case MD_PRINT:
		/* print the queue */
		HoldErrs = false;
		(void) dropenvelope(&BlankEnvelope, true, false);
		(void) sm_signal(SIGPIPE, sigpipe);
		if (qgrp != NOQGRP)
		{
			int j;

			/* Selecting a particular queue group to run */
			for (j = 0; j < Queue[qgrp]->qg_numqueues; j++)
			{
				if (StopRequest)
					stop_sendmail();
				(void) print_single_queue(qgrp, j);
			}
			finis(false, true, EX_OK);
			/* NOTREACHED */
		}
		printqueue();
		finis(false, true, EX_OK);
		/* NOTREACHED */
		break;

	  case MD_PRINTNQE:
		/* print number of entries in queue */
		(void) dropenvelope(&BlankEnvelope, true, false);
		(void) sm_signal(SIGPIPE, sigpipe);
		printnqe(smioout, NULL);
		finis(false, true, EX_OK);
		/* NOTREACHED */
		break;

	  case MD_QUEUERUN:
		/* only handle quarantining here */
		if (quarantining == NULL)
			break;

		if (QueueMode != QM_QUARANTINE &&
		    QueueMode != QM_NORMAL)
		{
			HoldErrs = false;
			usrerr("Can not use -Q with -q%c", QueueMode);
			ExitStat = EX_USAGE;
			finis(false, true, ExitStat);
			/* NOTREACHED */
		}
		quarantine_queue(quarantining, qgrp);
		finis(false, true, EX_OK);
		break;

	  case MD_HOSTSTAT:
		(void) sm_signal(SIGPIPE, sigpipe);
		(void) mci_traverse_persistent(mci_print_persistent, NULL);
		finis(false, true, EX_OK);
		/* NOTREACHED */
		break;

	  case MD_PURGESTAT:
		(void) mci_traverse_persistent(mci_purge_persistent, NULL);
		finis(false, true, EX_OK);
		/* NOTREACHED */
		break;

	  case MD_INITALIAS:
		/* initialize maps */
		initmaps();
		finis(false, true, ExitStat);
		/* NOTREACHED */
		break;

	  case MD_SMTP:
	  case MD_DAEMON:
		/* reset DSN parameters */
		DefaultNotify = QPINGONFAILURE|QPINGONDELAY;
		macdefine(&BlankEnvelope.e_macro, A_PERM,
			  macid("{dsn_notify}"), NULL);
		BlankEnvelope.e_envid = NULL;
		macdefine(&BlankEnvelope.e_macro, A_PERM,
			  macid("{dsn_envid}"), NULL);
		BlankEnvelope.e_flags &= ~(EF_RET_PARAM|EF_NO_BODY_RETN);
		macdefine(&BlankEnvelope.e_macro, A_PERM,
			  macid("{dsn_ret}"), NULL);

		/* don't open maps for daemon -- done below in child */
		break;
	}

	if (tTd(0, 15))
	{
		/* print configuration table (or at least part of it) */
		if (tTd(0, 90))
			printrules();
		for (i = 0; i < MAXMAILERS; i++)
		{
			if (Mailer[i] != NULL)
				printmailer(sm_debug_file(), Mailer[i]);
		}
	}

	/*
	**  Switch to the main envelope.
	*/

	CurEnv = newenvelope(&MainEnvelope, &BlankEnvelope,
			     sm_rpool_new_x(NULL));
	MainEnvelope.e_flags = BlankEnvelope.e_flags;

	/*
	**  If test mode, read addresses from stdin and process.
	*/

	if (OpMode == MD_TEST)
	{
		if (isatty(sm_io_getinfo(smioin, SM_IO_WHAT_FD, NULL)))
			Verbose = 2;

		if (Verbose)
		{
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "ADDRESS TEST MODE (ruleset 3 NOT automatically invoked)\n");
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Enter <ruleset> <address>\n");
		}
		macdefine(&(MainEnvelope.e_macro), A_PERM,
			  macid("{addr_type}"), "e r");
		for (;;)
		{
			SM_TRY
			{
				(void) sm_signal(SIGINT, intindebug);
				(void) sm_releasesignal(SIGINT);
				if (Verbose == 2)
					(void) sm_io_fprintf(smioout,
							     SM_TIME_DEFAULT,
							     "> ");
				(void) sm_io_flush(smioout, SM_TIME_DEFAULT);
				if (sm_io_fgets(smioin, SM_TIME_DEFAULT, buf,
						sizeof(buf)) < 0)
					testmodeline("/quit", &MainEnvelope);
				p = strchr(buf, '\n');
				if (p != NULL)
					*p = '\0';
				if (Verbose < 2)
					(void) sm_io_fprintf(smioout,
							     SM_TIME_DEFAULT,
							     "> %s\n", buf);
				testmodeline(buf, &MainEnvelope);
			}
			SM_EXCEPT(exc, "[!F]*")
			{
				/*
				**  8.10 just prints \n on interrupt.
				**  I'm printing the exception here in case
				**  sendmail is extended to raise additional
				**  exceptions in this context.
				*/

				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "\n");
				sm_exc_print(exc, smioout);
			}
			SM_END_TRY
		}
	}

#if STARTTLS
	tls_ok = true;
	if (OpMode == MD_QUEUERUN || OpMode == MD_DELIVER ||
	    OpMode == MD_ARPAFTP)
	{
		/* check whether STARTTLS is turned off for the client */
		if (chkclientmodifiers(D_NOTLS))
			tls_ok = false;
	}
	else if (OpMode == MD_DAEMON || OpMode == MD_FGDAEMON ||
		 OpMode == MD_SMTP)
	{
		/* check whether STARTTLS is turned off */
		if (chkdaemonmodifiers(D_NOTLS) && chkclientmodifiers(D_NOTLS))
			tls_ok = false;
	}
	else	/* other modes don't need STARTTLS */
		tls_ok = false;

	if (tls_ok)
	{
		/* basic TLS initialization */
		tls_ok = init_tls_library(FipsMode);
		if (!tls_ok && FipsMode)
		{
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "ERROR: FIPSMode failed to initialize\n");
			exit(EX_USAGE);
		}
	}

	if (!tls_ok && (OpMode == MD_QUEUERUN || OpMode == MD_DELIVER))
	{
		/* disable TLS for client */
		setclttls(false);
	}
#endif /* STARTTLS */

	/*
	**  If collecting stuff from the queue, go start doing that.
	*/

	if (OpMode == MD_QUEUERUN && QueueIntvl == 0)
	{
		pid_t pid = -1;

#if STARTTLS
		/* init TLS for client, ignore result for now */
		(void) initclttls(tls_ok);
#endif /* STARTTLS */

		/*
		**  The parent process of the caller of runqueue() needs
		**  to stay around for a possible SIGTERM. The SIGTERM will
		**  tell this process that all of the queue runners children
		**  need to be sent SIGTERM as well. At the same time, we
		**  want to return control to the command line. So we do an
		**  extra fork().
		*/

		if (Verbose || foregroundqueue || (pid = fork()) <= 0)
		{
			/*
			**  If the fork() failed we should still try to do
			**  the queue run. If it succeeded then the child
			**  is going to start the run and wait for all
			**  of the children to finish.
			*/

			if (pid == 0)
			{
				/* Reset global flags */
				RestartRequest = NULL;
				ShutdownRequest = NULL;
				PendingSignal = 0;

				/* disconnect from terminal */
				disconnect(2, CurEnv);
			}

			CurrentPid = getpid();
			if (qgrp != NOQGRP)
			{
				int rwgflags = RWG_NONE;

				/*
				**  To run a specific queue group mark it to
				**  be run, select the work group it's in and
				**  increment the work counter.
				*/

				for (i = 0; i < NumQueue && Queue[i] != NULL;
				     i++)
					Queue[i]->qg_nextrun = (time_t) -1;
				Queue[qgrp]->qg_nextrun = 0;
				if (Verbose)
					rwgflags |= RWG_VERBOSE;
				if (queuepersistent)
					rwgflags |= RWG_PERSISTENT;
				rwgflags |= RWG_FORCE;
				(void) run_work_group(Queue[qgrp]->qg_wgrp,
						      rwgflags);
			}
			else
				(void) runqueue(false, Verbose,
						queuepersistent, true);

			/* set the title to make it easier to find */
			sm_setproctitle(true, CurEnv, "Queue control");
			(void) sm_signal(SIGCHLD, SIG_DFL);
			while (CurChildren > 0)
			{
				int status;
				pid_t ret;

				errno = 0;
				while ((ret = sm_wait(&status)) <= 0)
				{
					if (errno == ECHILD)
					{
						/*
						**  Oops... something got messed
						**  up really bad. Waiting for
						**  non-existent children
						**  shouldn't happen. Let's get
						**  out of here.
						*/

						CurChildren = 0;
						break;
					}
					continue;
				}

				/* something is really really wrong */
				if (errno == ECHILD)
				{
					sm_syslog(LOG_ERR, NOQID,
						  "queue control process: lost all children: wait returned ECHILD");
					break;
				}

				/* Only drop when a child gives status */
				if (WIFSTOPPED(status))
					continue;

				proc_list_drop(ret, status, NULL);
			}
		}
		finis(true, true, ExitStat);
		/* NOTREACHED */
	}

# if SASL
	if (OpMode == MD_SMTP || OpMode == MD_DAEMON)
	{
		/* check whether AUTH is turned off for the server */
		if (!chkdaemonmodifiers(D_NOAUTH) &&
		    (i = sasl_server_init(srvcallbacks, "Sendmail")) != SASL_OK)
			syserr("!sasl_server_init failed! [%s]",
				sasl_errstring(i, NULL, NULL));
	}
# endif /* SASL */

	if (OpMode == MD_SMTP)
	{
		proc_list_add(CurrentPid, "Sendmail SMTP Agent",
			      PROC_DAEMON, 0, -1, NULL);

		/* clean up background delivery children */
		(void) sm_signal(SIGCHLD, reapchild);
	}

	/*
	**  If a daemon, wait for a request.
	**	getrequests will always return in a child.
	**	If we should also be processing the queue, start
	**		doing it in background.
	**	We check for any errors that might have happened
	**		during startup.
	*/

	if (OpMode == MD_DAEMON || QueueIntvl > 0)
	{
		char dtype[200];

		/* avoid cleanup in finis(), DaemonPid will be set below */
		DaemonPid = 0;
		if (!run_in_foreground && !tTd(99, 100))
		{
			/* put us in background */
			i = fork();
			if (i < 0)
				syserr("daemon: cannot fork");
			if (i != 0)
			{
				finis(false, true, EX_OK);
				/* NOTREACHED */
			}

			/*
			**  Initialize exception stack and default exception
			**  handler for child process.
			*/

			/* Reset global flags */
			RestartRequest = NULL;
			RestartWorkGroup = false;
			ShutdownRequest = NULL;
			PendingSignal = 0;
			CurrentPid = getpid();

			sm_exc_newthread(fatal_error);

			/* disconnect from our controlling tty */
			disconnect(2, &MainEnvelope);
		}

		dtype[0] = '\0';
		if (OpMode == MD_DAEMON)
		{
			(void) sm_strlcat(dtype, "+SMTP", sizeof(dtype));
			DaemonPid = CurrentPid;
		}
		if (QueueIntvl > 0)
		{
			(void) sm_strlcat2(dtype,
					   queuepersistent
					   ? "+persistent-queueing@"
					   : "+queueing@",
					   pintvl(QueueIntvl, true),
					   sizeof(dtype));
		}
		if (tTd(0, 1))
			(void) sm_strlcat(dtype, "+debugging", sizeof(dtype));

		sm_syslog(LOG_INFO, NOQID,
			  "starting daemon (%s): %s", Version, dtype + 1);
#if XLA
		xla_create_file();
#endif /* XLA */

		/* save daemon type in a macro for possible PidFile use */
		macdefine(&BlankEnvelope.e_macro, A_TEMP,
			macid("{daemon_info}"), dtype + 1);

		/* save queue interval in a macro for possible PidFile use */
		macdefine(&MainEnvelope.e_macro, A_TEMP,
			macid("{queue_interval}"), pintvl(QueueIntvl, true));

		/* workaround: can't seem to release the signal in the parent */
		(void) sm_signal(SIGHUP, sighup);
		(void) sm_releasesignal(SIGHUP);
		(void) sm_signal(SIGTERM, sigterm);

		if (QueueIntvl > 0)
		{
#if _FFR_RUNPQG
			if (qgrp != NOQGRP)
			{
				int rwgflags = RWG_NONE;

				/*
				**  To run a specific queue group mark it to
				**  be run, select the work group it's in and
				**  increment the work counter.
				*/

				for (i = 0; i < NumQueue && Queue[i] != NULL;
				     i++)
					Queue[i]->qg_nextrun = (time_t) -1;
				Queue[qgrp]->qg_nextrun = 0;
				if (Verbose)
					rwgflags |= RWG_VERBOSE;
				if (queuepersistent)
					rwgflags |= RWG_PERSISTENT;
				rwgflags |= RWG_FORCE;
				(void) run_work_group(Queue[qgrp]->qg_wgrp,
						      rwgflags);
			}
			else
#endif /* _FFR_RUNPQG */
				(void) runqueue(true, false, queuepersistent,
						true);

			/*
			**  If queuepersistent but not in daemon mode then
			**  we're going to do the queue runner monitoring here.
			**  If in daemon mode then the monitoring will happen
			**  elsewhere.
			*/

			if (OpMode != MD_DAEMON && queuepersistent)
			{
				/*
				**  Write the pid to file
				**  XXX Overwrites sendmail.pid
				*/

				log_sendmail_pid(&MainEnvelope);

				/* set the title to make it easier to find */
				sm_setproctitle(true, CurEnv, "Queue control");
				(void) sm_signal(SIGCHLD, SIG_DFL);
				while (CurChildren > 0)
				{
					int status;
					pid_t ret;
					int group;

					CHECK_RESTART;
					errno = 0;
					while ((ret = sm_wait(&status)) <= 0)
					{
						/*
						**  Waiting for non-existent
						**  children shouldn't happen.
						**  Let's get out of here if
						**  it occurs.
						*/

						if (errno == ECHILD)
						{
							CurChildren = 0;
							break;
						}
						continue;
					}

					/* something is really really wrong */
					if (errno == ECHILD)
					{
						sm_syslog(LOG_ERR, NOQID,
							  "persistent queue runner control process: lost all children: wait returned ECHILD");
						break;
					}

					if (WIFSTOPPED(status))
						continue;

					/* Probe only on a child status */
					proc_list_drop(ret, status, &group);

					if (WIFSIGNALED(status))
					{
						if (WCOREDUMP(status))
						{
							sm_syslog(LOG_ERR, NOQID,
								  "persistent queue runner=%d core dumped, signal=%d",
								  group, WTERMSIG(status));

							/* don't restart this */
							mark_work_group_restart(
								group, -1);
							continue;
						}

						sm_syslog(LOG_ERR, NOQID,
							  "persistent queue runner=%d died, pid=%ld, signal=%d",
							  group, (long) ret,
							  WTERMSIG(status));
					}

					/*
					**  When debugging active, don't
					**  restart the persistent queues.
					**  But do log this as info.
					*/

					if (sm_debug_active(&DebugNoPRestart,
							    1))
					{
						sm_syslog(LOG_DEBUG, NOQID,
							  "persistent queue runner=%d, exited",
							  group);
						mark_work_group_restart(group,
									-1);
					}
					CHECK_RESTART;
				}
				finis(true, true, ExitStat);
				/* NOTREACHED */
			}

			if (OpMode != MD_DAEMON)
			{
				char qtype[200];

				/*
				**  Write the pid to file
				**  XXX Overwrites sendmail.pid
				*/

				log_sendmail_pid(&MainEnvelope);

				/* set the title to make it easier to find */
				qtype[0] = '\0';
				(void) sm_strlcpyn(qtype, sizeof(qtype), 4,
						   "Queue runner@",
						   pintvl(QueueIntvl, true),
						   " for ",
						   QueueDir);
				sm_setproctitle(true, CurEnv, qtype);
				for (;;)
				{
					(void) pause();

					CHECK_RESTART;

					if (doqueuerun())
						(void) runqueue(true, false,
								false, false);
				}
			}
		}
		(void) dropenvelope(&MainEnvelope, true, false);

#if STARTTLS
		/* init TLS for server, ignore result for now */
		(void) initsrvtls(tls_ok);
#endif /* STARTTLS */

	nextreq:
		p_flags = getrequests(&MainEnvelope);

		/* drop privileges */
		(void) drop_privileges(false);

		/*
		**  Get authentication data
		**  Set _ macro in BlankEnvelope before calling newenvelope().
		*/

#if _FFR_XCNCT
		if (bitnset(D_XCNCT, *p_flags) || bitnset(D_XCNCT_M, *p_flags))
		{
			/* copied from getauthinfo() */
			if (RealHostName == NULL)
			{
				RealHostName = newstr(hostnamebyanyaddr(&RealHostAddr));
				if (strlen(RealHostName) > MAXNAME)
					RealHostName[MAXNAME] = '\0'; /* XXX - 1 ? */
			}
			snprintf(buf, sizeof(buf), "%s [%s]",
				RealHostName, anynet_ntoa(&RealHostAddr));

			forged = bitnset(D_XCNCT_M, *p_flags);
			if (forged)
			{
				(void) sm_strlcat(buf, " (may be forged)",
						sizeof(buf));
				macdefine(&BlankEnvelope.e_macro, A_PERM,
					  macid("{client_resolve}"), "FORGED");
			}

			/* HACK! variable used only two times right below */
			authinfo = buf;
			if (tTd(75, 9))
				sm_syslog(LOG_INFO, NOQID,
					"main: where=not_calling_getauthinfo, RealHostAddr=%s",
					anynet_ntoa(&RealHostAddr));
		}
		else
		/* WARNING: "non-braced" else */
#endif /* _FFR_XCNCT */
		authinfo = getauthinfo(sm_io_getinfo(InChannel, SM_IO_WHAT_FD,
						     NULL), &forged);
		macdefine(&BlankEnvelope.e_macro, A_TEMP, '_', authinfo);
		if (tTd(75, 9))
			sm_syslog(LOG_INFO, NOQID,
				"main: where=after_getauthinfo, RealHostAddr=%s",
				anynet_ntoa(&RealHostAddr));

		/* at this point we are in a child: reset state */
		sm_rpool_free(MainEnvelope.e_rpool);
		(void) newenvelope(&MainEnvelope, &MainEnvelope,
				   sm_rpool_new_x(NULL));
	}

	if (LogLevel > 9)
	{
		/* log connection information */
		sm_syslog(LOG_INFO, NULL, "connect from %s", authinfo);
	}

	/*
	**  If running SMTP protocol, start collecting and executing
	**  commands.  This will never return.
	*/

	if (OpMode == MD_SMTP || OpMode == MD_DAEMON)
	{
		char pbuf[20];

		/*
		**  Save some macros for check_* rulesets.
		*/

		if (forged)
		{
			char ipbuf[103];

			(void) sm_snprintf(ipbuf, sizeof(ipbuf), "[%.100s]",
					   anynet_ntoa(&RealHostAddr));
			macdefine(&BlankEnvelope.e_macro, A_TEMP,
				  macid("{client_name}"), ipbuf);
		}
		else
			macdefine(&BlankEnvelope.e_macro, A_PERM,
				  macid("{client_name}"), RealHostName);
		macdefine(&BlankEnvelope.e_macro, A_PERM,
			  macid("{client_ptr}"), RealHostName);
		macdefine(&BlankEnvelope.e_macro, A_TEMP,
			  macid("{client_addr}"), anynet_ntoa(&RealHostAddr));
		sm_getla();

		switch (RealHostAddr.sa.sa_family)
		{
#if NETINET
		  case AF_INET:
			(void) sm_snprintf(pbuf, sizeof(pbuf), "%d",
					   ntohs(RealHostAddr.sin.sin_port));
			break;
#endif /* NETINET */
#if NETINET6
		  case AF_INET6:
			(void) sm_snprintf(pbuf, sizeof(pbuf), "%d",
					   ntohs(RealHostAddr.sin6.sin6_port));
			break;
#endif /* NETINET6 */
		  default:
			(void) sm_snprintf(pbuf, sizeof(pbuf), "0");
			break;
		}
		macdefine(&BlankEnvelope.e_macro, A_TEMP,
			macid("{client_port}"), pbuf);

		if (OpMode == MD_DAEMON)
		{
			ENVELOPE *saved_env;

			/* validate the connection */
			HoldErrs = true;
			saved_env = CurEnv;
			CurEnv = &BlankEnvelope;
			nullserver = validate_connection(&RealHostAddr,
						macvalue(macid("{client_name}"),
							&BlankEnvelope),
						&BlankEnvelope);
			if (bitset(EF_DISCARD, BlankEnvelope.e_flags))
				MainEnvelope.e_flags |= EF_DISCARD;
			CurEnv = saved_env;
			HoldErrs = false;
		}
		else if (p_flags == NULL)
		{
			p_flags = (BITMAP256 *) xalloc(sizeof(*p_flags));
			clrbitmap(p_flags);
		}
#if STARTTLS
		if (OpMode == MD_SMTP)
			(void) initsrvtls(tls_ok);
#endif /* STARTTLS */

		/* turn off profiling */
		SM_PROF(1);
		smtp(nullserver, *p_flags, &MainEnvelope);

		if (tTd(93, 100))
		{
			/* turn off profiling */
			SM_PROF(0);
			if (OpMode == MD_DAEMON)
				goto nextreq;
		}
	}

	sm_rpool_free(MainEnvelope.e_rpool);
	clearenvelope(&MainEnvelope, false, sm_rpool_new_x(NULL));
	if (OpMode == MD_VERIFY)
	{
		set_delivery_mode(SM_VERIFY, &MainEnvelope);
		PostMasterCopy = NULL;
	}
	else
	{
		/* interactive -- all errors are global */
		MainEnvelope.e_flags |= EF_GLOBALERRS|EF_LOGSENDER;
	}

	/*
	**  Do basic system initialization and set the sender
	*/

	initsys(&MainEnvelope);
	macdefine(&MainEnvelope.e_macro, A_PERM, macid("{ntries}"), "0");
	macdefine(&MainEnvelope.e_macro, A_PERM, macid("{nrcpts}"), "0");
	setsender(from, &MainEnvelope, NULL, '\0', false);
	if (warn_f_flag != '\0' && !wordinclass(RealUserName, 't') &&
	    (!bitnset(M_LOCALMAILER, MainEnvelope.e_from.q_mailer->m_flags) ||
	     strcmp(MainEnvelope.e_from.q_user, RealUserName) != 0))
	{
		auth_warning(&MainEnvelope, "%s set sender to %s using -%c",
			     RealUserName, from, warn_f_flag);
#if SASL
		auth = false;
#endif /* SASL */
	}
	if (auth)
	{
		char *fv;

		/* set the initial sender for AUTH= to $f@$j */
		fv = macvalue('f', &MainEnvelope);
		if (fv == NULL || *fv == '\0')
			MainEnvelope.e_auth_param = NULL;
		else
		{
			if (strchr(fv, '@') == NULL)
			{
				i = strlen(fv) + strlen(macvalue('j',
							&MainEnvelope)) + 2;
				p = sm_malloc_x(i);
				(void) sm_strlcpyn(p, i, 3, fv, "@",
						   macvalue('j',
							    &MainEnvelope));
			}
			else
				p = sm_strdup_x(fv);
			MainEnvelope.e_auth_param = sm_rpool_strdup_x(MainEnvelope.e_rpool,
								      xtextify(p, "="));
			sm_free(p);  /* XXX */
		}
	}
	if (macvalue('s', &MainEnvelope) == NULL)
		macdefine(&MainEnvelope.e_macro, A_PERM, 's', RealHostName);

	av = argv + optind;
	if (*av == NULL && !GrabTo)
	{
		MainEnvelope.e_to = NULL;
		MainEnvelope.e_flags |= EF_GLOBALERRS;
		HoldErrs = false;
		SuperSafe = SAFE_NO;
		usrerr("Recipient names must be specified");

		/* collect body for UUCP return */
		if (OpMode != MD_VERIFY)
			collect(InChannel, false, NULL, &MainEnvelope, true);
		finis(true, true, EX_USAGE);
		/* NOTREACHED */
	}

	/*
	**  Scan argv and deliver the message to everyone.
	*/

	save_val = LogUsrErrs;
	LogUsrErrs = true;
	sendtoargv(av, &MainEnvelope);
	LogUsrErrs = save_val;

	/* if we have had errors sofar, arrange a meaningful exit stat */
	if (Errors > 0 && ExitStat == EX_OK)
		ExitStat = EX_USAGE;

#if _FFR_FIX_DASHT
	/*
	**  If using -t, force not sending to argv recipients, even
	**  if they are mentioned in the headers.
	*/

	if (GrabTo)
	{
		ADDRESS *q;

		for (q = MainEnvelope.e_sendqueue; q != NULL; q = q->q_next)
			q->q_state = QS_REMOVED;
	}
#endif /* _FFR_FIX_DASHT */

	/*
	**  Read the input mail.
	*/

	MainEnvelope.e_to = NULL;
	if (OpMode != MD_VERIFY || GrabTo)
	{
		int savederrors;
		unsigned long savedflags;

		/*
		**  workaround for compiler warning on Irix:
		**  do not initialize variable in the definition, but
		**  later on:
		**  warning(1548): transfer of control bypasses
		**  initialization of:
		**  variable "savederrors" (declared at line 2570)
		**  variable "savedflags" (declared at line 2571)
		**  goto giveup;
		*/

		savederrors = Errors;
		savedflags = MainEnvelope.e_flags & EF_FATALERRS;
		MainEnvelope.e_flags |= EF_GLOBALERRS;
		MainEnvelope.e_flags &= ~EF_FATALERRS;
		Errors = 0;
		buffer_errors();
		collect(InChannel, false, NULL, &MainEnvelope, true);

		/* header checks failed */
		if (Errors > 0)
		{
  giveup:
			if (!GrabTo)
			{
				/* Log who the mail would have gone to */
				logundelrcpts(&MainEnvelope,
					      MainEnvelope.e_message,
					      8, false);
			}
			flush_errors(true);
			finis(true, true, ExitStat);
			/* NOTREACHED */
			return -1;
		}

		/* bail out if message too large */
		if (bitset(EF_CLRQUEUE, MainEnvelope.e_flags))
		{
			finis(true, true, ExitStat != EX_OK ? ExitStat
							    : EX_DATAERR);
			/* NOTREACHED */
			return -1;
		}

		/* set message size */
		(void) sm_snprintf(buf, sizeof(buf), "%ld",
				   PRT_NONNEGL(MainEnvelope.e_msgsize));
		macdefine(&MainEnvelope.e_macro, A_TEMP,
			  macid("{msg_size}"), buf);

		Errors = savederrors;
		MainEnvelope.e_flags |= savedflags;
	}
	errno = 0;

	if (tTd(1, 1))
		sm_dprintf("From person = \"%s\"\n",
			   MainEnvelope.e_from.q_paddr);

	/* Check if quarantining stats should be updated */
	if (MainEnvelope.e_quarmsg != NULL)
		markstats(&MainEnvelope, NULL, STATS_QUARANTINE);

	/*
	**  Actually send everything.
	**	If verifying, just ack.
	*/

	if (Errors == 0)
	{
		if (!split_by_recipient(&MainEnvelope) &&
		    bitset(EF_FATALERRS, MainEnvelope.e_flags))
			goto giveup;
	}

	/* make sure we deliver at least the first envelope */
	i = FastSplit > 0 ? 0 : -1;
	for (e = &MainEnvelope; e != NULL; e = e->e_sibling, i++)
	{
		ENVELOPE *next;

		e->e_from.q_state = QS_SENDER;
		if (tTd(1, 5))
		{
			sm_dprintf("main[%d]: QS_SENDER ", i);
			printaddr(sm_debug_file(), &e->e_from, false);
		}
		e->e_to = NULL;
		sm_getla();
		GrabTo = false;
#if NAMED_BIND
		_res.retry = TimeOuts.res_retry[RES_TO_FIRST];
		_res.retrans = TimeOuts.res_retrans[RES_TO_FIRST];
#endif /* NAMED_BIND */
		next = e->e_sibling;
		e->e_sibling = NULL;

		/* after FastSplit envelopes: queue up */
		sendall(e, i >= FastSplit ? SM_QUEUE : SM_DEFAULT);
		e->e_sibling = next;
	}

	/*
	**  All done.
	**	Don't send return error message if in VERIFY mode.
	*/

	finis(true, true, ExitStat);
	/* NOTREACHED */
	return ExitStat;
}
/*
**  STOP_SENDMAIL -- Stop the running program
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
**
**	Side Effects:
**		exits.
*/

void
stop_sendmail()
{
	/* reset uid for process accounting */
	endpwent();
	(void) setuid(RealUid);
	exit(EX_OK);
}
/*
**  FINIS -- Clean up and exit.
**
**	Parameters:
**		drop -- whether or not to drop CurEnv envelope
**		cleanup -- call exit() or _exit()?
**		exitstat -- exit status to use for exit() call
**
**	Returns:
**		never
**
**	Side Effects:
**		exits sendmail
*/

void
finis(drop, cleanup, exitstat)
	bool drop;
	bool cleanup;
	volatile int exitstat;
{
	char pidpath[MAXPATHLEN];
	pid_t pid;

	/* Still want to process new timeouts added below */
	sm_clear_events();
	(void) sm_releasesignal(SIGALRM);

	if (tTd(2, 1))
	{
		sm_dprintf("\n====finis: stat %d e_id=%s e_flags=",
			   exitstat,
			   CurEnv->e_id == NULL ? "NOQUEUE" : CurEnv->e_id);
		printenvflags(CurEnv);
	}
	if (tTd(2, 9))
		printopenfds(false);

	SM_TRY
		/*
		**  Clean up.  This might raise E:mta.quickabort
		*/

		/* clean up temp files */
		CurEnv->e_to = NULL;
		if (drop)
		{
			if (CurEnv->e_id != NULL)
			{
				int r;

				r = dropenvelope(CurEnv, true, false);
				if (exitstat == EX_OK)
					exitstat = r;
				sm_rpool_free(CurEnv->e_rpool);
				CurEnv->e_rpool = NULL;

				/* these may have pointed to the rpool */
				CurEnv->e_to = NULL;
				CurEnv->e_message = NULL;
				CurEnv->e_statmsg = NULL;
				CurEnv->e_quarmsg = NULL;
				CurEnv->e_bodytype = NULL;
				CurEnv->e_id = NULL;
				CurEnv->e_envid = NULL;
				CurEnv->e_auth_param = NULL;
			}
			else
				poststats(StatFile);
		}

		/* flush any cached connections */
		mci_flush(true, NULL);

		/* close maps belonging to this pid */
		closemaps(false);

#if USERDB
		/* close UserDatabase */
		_udbx_close();
#endif /* USERDB */

#if SASL
		stop_sasl_client();
#endif /* SASL */

#if XLA
		/* clean up extended load average stuff */
		xla_all_end();
#endif /* XLA */

	SM_FINALLY
		/*
		**  And exit.
		*/

		if (LogLevel > 78)
			sm_syslog(LOG_DEBUG, CurEnv->e_id, "finis, pid=%d",
				  (int) CurrentPid);
		if (exitstat == EX_TEMPFAIL ||
		    CurEnv->e_errormode == EM_BERKNET)
			exitstat = EX_OK;

		/* XXX clean up queues and related data structures */
		cleanup_queues();
		pid = getpid();
#if SM_CONF_SHM
		cleanup_shm(DaemonPid == pid);
#endif /* SM_CONF_SHM */

		/* close locked pid file */
		close_sendmail_pid();

		if (DaemonPid == pid || PidFilePid == pid)
		{
			/* blow away the pid file */
			expand(PidFile, pidpath, sizeof(pidpath), CurEnv);
			(void) unlink(pidpath);
		}

		/* reset uid for process accounting */
		endpwent();
		sm_mbdb_terminate();
#if _FFR_MEMSTAT
		(void) sm_memstat_close();
#endif /* _FFR_MEMSTAT */
		(void) setuid(RealUid);
#if SM_HEAP_CHECK
		/* dump the heap, if we are checking for memory leaks */
		if (sm_debug_active(&SmHeapCheck, 2))
			sm_heap_report(smioout,
				       sm_debug_level(&SmHeapCheck) - 1);
#endif /* SM_HEAP_CHECK */
		if (sm_debug_active(&SmXtrapReport, 1))
			sm_dprintf("xtrap count = %d\n", SmXtrapCount);
		if (cleanup)
			exit(exitstat);
		else
			_exit(exitstat);
	SM_END_TRY
}
/*
**  INTINDEBUG -- signal handler for SIGINT in -bt mode
**
**	Parameters:
**		sig -- incoming signal.
**
**	Returns:
**		none.
**
**	Side Effects:
**		longjmps back to test mode loop.
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

/* Type of an exception generated on SIGINT during address test mode.  */
static const SM_EXC_TYPE_T EtypeInterrupt =
{
	SmExcTypeMagic,
	"S:mta.interrupt",
	"",
	sm_etype_printf,
	"interrupt",
};

/* ARGSUSED */
static SIGFUNC_DECL
intindebug(sig)
	int sig;
{
	int save_errno = errno;

	FIX_SYSV_SIGNAL(sig, intindebug);
	errno = save_errno;
	CHECK_CRITICAL(sig);
	errno = save_errno;
	sm_exc_raisenew_x(&EtypeInterrupt);
	errno = save_errno;
	return SIGFUNC_RETURN;
}
/*
**  SIGTERM -- SIGTERM handler for the daemon
**
**	Parameters:
**		sig -- signal number.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Sets ShutdownRequest which will hopefully trigger
**		the daemon to exit.
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

/* ARGSUSED */
static SIGFUNC_DECL
sigterm(sig)
	int sig;
{
	int save_errno = errno;

	FIX_SYSV_SIGNAL(sig, sigterm);
	ShutdownRequest = "signal";
	errno = save_errno;
	return SIGFUNC_RETURN;
}
/*
**  SIGHUP -- handle a SIGHUP signal
**
**	Parameters:
**		sig -- incoming signal.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Sets RestartRequest which should cause the daemon
**		to restart.
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

/* ARGSUSED */
static SIGFUNC_DECL
sighup(sig)
	int sig;
{
	int save_errno = errno;

	FIX_SYSV_SIGNAL(sig, sighup);
	RestartRequest = "signal";
	errno = save_errno;
	return SIGFUNC_RETURN;
}
/*
**  SIGPIPE -- signal handler for SIGPIPE
**
**	Parameters:
**		sig -- incoming signal.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Sets StopRequest which should cause the mailq/hoststatus
**		display to stop.
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

/* ARGSUSED */
static SIGFUNC_DECL
sigpipe(sig)
	int sig;
{
	int save_errno = errno;

	FIX_SYSV_SIGNAL(sig, sigpipe);
	StopRequest = true;
	errno = save_errno;
	return SIGFUNC_RETURN;
}
/*
**  INTSIG -- clean up on interrupt
**
**	This just arranges to exit.  It pessimizes in that it
**	may resend a message.
**
**	Parameters:
**		sig -- incoming signal.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Unlocks the current job.
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

/* ARGSUSED */
SIGFUNC_DECL
intsig(sig)
	int sig;
{
	bool drop = false;
	int save_errno = errno;

	FIX_SYSV_SIGNAL(sig, intsig);
	errno = save_errno;
	CHECK_CRITICAL(sig);
	sm_allsignals(true);
	IntSig = true;

	FileName = NULL;

	/* Clean-up on aborted stdin message submission */
	if  (OpMode == MD_SMTP ||
	     OpMode == MD_DELIVER ||
	     OpMode == MD_ARPAFTP)
	{
		if (CurEnv->e_id != NULL)
		{
			char *fn;

			fn = queuename(CurEnv, DATAFL_LETTER);
			if (fn != NULL)
				(void) unlink(fn);
			fn = queuename(CurEnv, ANYQFL_LETTER);
			if (fn != NULL)
				(void) unlink(fn);
		}
		_exit(EX_OK);
		/* NOTREACHED */
	}

	if (sig != 0 && LogLevel > 79)
		sm_syslog(LOG_DEBUG, CurEnv->e_id, "interrupt");
	if (OpMode != MD_TEST)
		unlockqueue(CurEnv);

	finis(drop, false, EX_OK);
	/* NOTREACHED */
}
/*
**  DISCONNECT -- remove our connection with any foreground process
**
**	Parameters:
**		droplev -- how "deeply" we should drop the line.
**			0 -- ignore signals, mail back errors, make sure
**			     output goes to stdout.
**			1 -- also, make stdout go to /dev/null.
**			2 -- also, disconnect from controlling terminal
**			     (only for daemon mode).
**		e -- the current envelope.
**
**	Returns:
**		none
**
**	Side Effects:
**		Trys to insure that we are immune to vagaries of
**		the controlling tty.
*/

void
disconnect(droplev, e)
	int droplev;
	register ENVELOPE *e;
{
	int fd;

	if (tTd(52, 1))
		sm_dprintf("disconnect: In %d Out %d, e=%p\n",
			   sm_io_getinfo(InChannel, SM_IO_WHAT_FD, NULL),
			   sm_io_getinfo(OutChannel, SM_IO_WHAT_FD, NULL), e);
	if (tTd(52, 100))
	{
		sm_dprintf("don't\n");
		return;
	}
	if (LogLevel > 93)
		sm_syslog(LOG_DEBUG, e->e_id,
			  "disconnect level %d",
			  droplev);

	/* be sure we don't get nasty signals */
	(void) sm_signal(SIGINT, SIG_IGN);
	(void) sm_signal(SIGQUIT, SIG_IGN);

	/* we can't communicate with our caller, so.... */
	HoldErrs = true;
	CurEnv->e_errormode = EM_MAIL;
	Verbose = 0;
	DisConnected = true;

	/* all input from /dev/null */
	if (InChannel != smioin)
	{
		(void) sm_io_close(InChannel, SM_TIME_DEFAULT);
		InChannel = smioin;
	}
	if (sm_io_reopen(SmFtStdio, SM_TIME_DEFAULT, SM_PATH_DEVNULL,
			 SM_IO_RDONLY, NULL, smioin) == NULL)
		sm_syslog(LOG_ERR, e->e_id,
			  "disconnect: sm_io_reopen(\"%s\") failed: %s",
			  SM_PATH_DEVNULL, sm_errstring(errno));

	/*
	**  output to the transcript
	**	We also compare the fd numbers here since OutChannel
	**	might be a layer on top of smioout due to encryption
	**	(see sfsasl.c).
	*/

	if (OutChannel != smioout &&
	    sm_io_getinfo(OutChannel, SM_IO_WHAT_FD, NULL) !=
	    sm_io_getinfo(smioout, SM_IO_WHAT_FD, NULL))
	{
		(void) sm_io_close(OutChannel, SM_TIME_DEFAULT);
		OutChannel = smioout;

#if 0
		/*
		**  Has smioout been closed? Reopen it.
		**	This shouldn't happen anymore, the code is here
		**	just as a reminder.
		*/

		if (smioout->sm_magic == NULL &&
		    sm_io_reopen(SmFtStdio, SM_TIME_DEFAULT, SM_PATH_DEVNULL,
				 SM_IO_WRONLY, NULL, smioout) == NULL)
			sm_syslog(LOG_ERR, e->e_id,
				  "disconnect: sm_io_reopen(\"%s\") failed: %s",
				  SM_PATH_DEVNULL, sm_errstring(errno));
#endif /* 0 */
	}
	if (droplev > 0)
	{
		fd = open(SM_PATH_DEVNULL, O_WRONLY, 0666);
		if (fd == -1)
		{
			sm_syslog(LOG_ERR, e->e_id,
				  "disconnect: open(\"%s\") failed: %s",
				  SM_PATH_DEVNULL, sm_errstring(errno));
		}
		(void) sm_io_flush(smioout, SM_TIME_DEFAULT);
		if (fd >= 0)
		{
			(void) dup2(fd, STDOUT_FILENO);
			(void) dup2(fd, STDERR_FILENO);
			(void) close(fd);
		}
	}

	/* drop our controlling TTY completely if possible */
	if (droplev > 1)
	{
		(void) setsid();
		errno = 0;
	}

#if XDEBUG
	checkfd012("disconnect");
#endif /* XDEBUG */

	if (LogLevel > 71)
		sm_syslog(LOG_DEBUG, e->e_id, "in background, pid=%d",
			  (int) CurrentPid);

	errno = 0;
}

static void
obsolete(argv)
	char *argv[];
{
	register char *ap;
	register char *op;

	while ((ap = *++argv) != NULL)
	{
		/* Return if "--" or not an option of any form. */
		if (ap[0] != '-' || ap[1] == '-')
			return;

		/* Don't allow users to use "-Q." or "-Q ." */
		if ((ap[1] == 'Q' && ap[2] == '.') ||
		    (ap[1] == 'Q' && argv[1] != NULL &&
		     argv[1][0] == '.' && argv[1][1] == '\0'))
		{
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Can not use -Q.\n");
			exit(EX_USAGE);
		}

		/* skip over options that do have a value */
		op = strchr(OPTIONS, ap[1]);
		if (op != NULL && *++op == ':' && ap[2] == '\0' &&
		    ap[1] != 'd' &&
#if defined(sony_news)
		    ap[1] != 'E' && ap[1] != 'J' &&
#endif /* defined(sony_news) */
		    argv[1] != NULL && argv[1][0] != '-')
		{
			argv++;
			continue;
		}

		/* If -C doesn't have an argument, use sendmail.cf. */
#define __DEFPATH	"sendmail.cf"
		if (ap[1] == 'C' && ap[2] == '\0')
		{
			*argv = xalloc(sizeof(__DEFPATH) + 2);
			(void) sm_strlcpyn(argv[0], sizeof(__DEFPATH) + 2, 2,
					   "-C", __DEFPATH);
		}

		/* If -q doesn't have an argument, run it once. */
		if (ap[1] == 'q' && ap[2] == '\0')
			*argv = "-q0";

		/* If -Q doesn't have an argument, disable quarantining */
		if (ap[1] == 'Q' && ap[2] == '\0')
			*argv = "-Q.";

		/* if -d doesn't have an argument, use 0-99.1 */
		if (ap[1] == 'd' && ap[2] == '\0')
			*argv = "-d0-99.1";

#if defined(sony_news)
		/* if -E doesn't have an argument, use -EC */
		if (ap[1] == 'E' && ap[2] == '\0')
			*argv = "-EC";

		/* if -J doesn't have an argument, use -JJ */
		if (ap[1] == 'J' && ap[2] == '\0')
			*argv = "-JJ";
#endif /* defined(sony_news) */
	}
}
/*
**  AUTH_WARNING -- specify authorization warning
**
**	Parameters:
**		e -- the current envelope.
**		msg -- the text of the message.
**		args -- arguments to the message.
**
**	Returns:
**		none.
*/

void
#ifdef __STDC__
auth_warning(register ENVELOPE *e, const char *msg, ...)
#else /* __STDC__ */
auth_warning(e, msg, va_alist)
	register ENVELOPE *e;
	const char *msg;
	va_dcl
#endif /* __STDC__ */
{
	char buf[MAXLINE];
	SM_VA_LOCAL_DECL

	if (bitset(PRIV_AUTHWARNINGS, PrivacyFlags))
	{
		register char *p;
		static char hostbuf[48];

		if (hostbuf[0] == '\0')
		{
			struct hostent *hp;

			hp = myhostname(hostbuf, sizeof(hostbuf));
#if NETINET6
			if (hp != NULL)
			{
				freehostent(hp);
				hp = NULL;
			}
#endif /* NETINET6 */
		}

		(void) sm_strlcpyn(buf, sizeof(buf), 2, hostbuf, ": ");
		p = &buf[strlen(buf)];
		SM_VA_START(ap, msg);
		(void) sm_vsnprintf(p, SPACELEFT(buf, p), msg, ap);
		SM_VA_END(ap);
		addheader("X-Authentication-Warning", buf, 0, e, true);
		if (LogLevel > 3)
			sm_syslog(LOG_INFO, e->e_id,
				  "Authentication-Warning: %.400s",
				  buf);
	}
}
/*
**  GETEXTENV -- get from external environment
**
**	Parameters:
**		envar -- the name of the variable to retrieve
**
**	Returns:
**		The value, if any.
*/

static char *
getextenv(envar)
	const char *envar;
{
	char **envp;
	int l;

	l = strlen(envar);
	for (envp = ExternalEnviron; envp != NULL && *envp != NULL; envp++)
	{
		if (strncmp(*envp, envar, l) == 0 && (*envp)[l] == '=')
			return &(*envp)[l + 1];
	}
	return NULL;
}
/*
**  SM_SETUSERENV -- set an environment variable in the propagated environment
**
**	Parameters:
**		envar -- the name of the environment variable.
**		value -- the value to which it should be set.  If
**			null, this is extracted from the incoming
**			environment.  If that is not set, the call
**			to sm_setuserenv is ignored.
**
**	Returns:
**		none.
*/

void
sm_setuserenv(envar, value)
	const char *envar;
	const char *value;
{
	int i, l;
	char **evp = UserEnviron;
	char *p;

	if (value == NULL)
	{
		value = getextenv(envar);
		if (value == NULL)
			return;
	}

	/* XXX enforce reasonable size? */
	i = strlen(envar) + 1;
	l = strlen(value) + i + 1;
	p = (char *) xalloc(l);
	(void) sm_strlcpyn(p, l, 3, envar, "=", value);

	while (*evp != NULL && strncmp(*evp, p, i) != 0)
		evp++;
	if (*evp != NULL)
	{
		*evp++ = p;
	}
	else if (evp < &UserEnviron[MAXUSERENVIRON])
	{
		*evp++ = p;
		*evp = NULL;
	}

	/* make sure it is in our environment as well */
	if (putenv(p) < 0)
		syserr("sm_setuserenv: putenv(%s) failed", p);
}
/*
**  DUMPSTATE -- dump state
**
**	For debugging.
*/

void
dumpstate(when)
	char *when;
{
	register char *j = macvalue('j', CurEnv);
	int rs;
	extern int NextMacroId;

	sm_syslog(LOG_DEBUG, CurEnv->e_id,
		  "--- dumping state on %s: $j = %s ---",
		  when,
		  j == NULL ? "<NULL>" : j);
	if (j != NULL)
	{
		if (!wordinclass(j, 'w'))
			sm_syslog(LOG_DEBUG, CurEnv->e_id,
				  "*** $j not in $=w ***");
	}
	sm_syslog(LOG_DEBUG, CurEnv->e_id, "CurChildren = %d", CurChildren);
	sm_syslog(LOG_DEBUG, CurEnv->e_id, "NextMacroId = %d (Max %d)",
		  NextMacroId, MAXMACROID);
	sm_syslog(LOG_DEBUG, CurEnv->e_id, "--- open file descriptors: ---");
	printopenfds(true);
	sm_syslog(LOG_DEBUG, CurEnv->e_id, "--- connection cache: ---");
	mci_dump_all(smioout, true);
	rs = strtorwset("debug_dumpstate", NULL, ST_FIND);
	if (rs > 0)
	{
		int status;
		register char **pvp;
		char *pv[MAXATOM + 1];

		pv[0] = NULL;
		status = REWRITE(pv, rs, CurEnv);
		sm_syslog(LOG_DEBUG, CurEnv->e_id,
			  "--- ruleset debug_dumpstate returns stat %d, pv: ---",
			  status);
		for (pvp = pv; *pvp != NULL; pvp++)
			sm_syslog(LOG_DEBUG, CurEnv->e_id, "%s", *pvp);
	}
	sm_syslog(LOG_DEBUG, CurEnv->e_id, "--- end of state dump ---");
}

#ifdef SIGUSR1
/*
**  SIGUSR1 -- Signal a request to dump state.
**
**	Parameters:
**		sig -- calling signal.
**
**	Returns:
**		none.
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
**
**		XXX: More work is needed for this signal handler.
*/

/* ARGSUSED */
static SIGFUNC_DECL
sigusr1(sig)
	int sig;
{
	int save_errno = errno;

	FIX_SYSV_SIGNAL(sig, sigusr1);
	errno = save_errno;
	CHECK_CRITICAL(sig);
	dumpstate("user signal");
# if SM_HEAP_CHECK
	dumpstab();
# endif /* SM_HEAP_CHECK */
	errno = save_errno;
	return SIGFUNC_RETURN;
}
#endif /* SIGUSR1 */

/*
**  DROP_PRIVILEGES -- reduce privileges to those of the RunAsUser option
**
**	Parameters:
**		to_real_uid -- if set, drop to the real uid instead
**			of the RunAsUser.
**
**	Returns:
**		EX_OSERR if the setuid failed.
**		EX_OK otherwise.
*/

int
drop_privileges(to_real_uid)
	bool to_real_uid;
{
	int rval = EX_OK;
	GIDSET_T emptygidset[1];

	if (tTd(47, 1))
		sm_dprintf("drop_privileges(%d): Real[UG]id=%ld:%ld, get[ug]id=%ld:%ld, gete[ug]id=%ld:%ld, RunAs[UG]id=%ld:%ld\n",
			   (int) to_real_uid,
			   (long) RealUid, (long) RealGid,
			   (long) getuid(), (long) getgid(),
			   (long) geteuid(), (long) getegid(),
			   (long) RunAsUid, (long) RunAsGid);

	if (to_real_uid)
	{
		RunAsUserName = RealUserName;
		RunAsUid = RealUid;
		RunAsGid = RealGid;
		EffGid = RunAsGid;
	}

	/* make sure no one can grab open descriptors for secret files */
	endpwent();
	sm_mbdb_terminate();

	/* reset group permissions; these can be set later */
	emptygidset[0] = (to_real_uid || RunAsGid != 0) ? RunAsGid : getegid();

	/*
	**  Notice:  on some OS (Linux...) the setgroups() call causes
	**	a logfile entry if sendmail is not run by root.
	**	However, it is unclear (no POSIX standard) whether
	**	setgroups() can only succeed if executed by root.
	**	So for now we keep it as it is; if you want to change it, use
	**  if (geteuid() == 0 && setgroups(1, emptygidset) == -1)
	*/

	if (setgroups(1, emptygidset) == -1 && geteuid() == 0)
	{
		syserr("drop_privileges: setgroups(1, %d) failed",
		       (int) emptygidset[0]);
		rval = EX_OSERR;
	}

	/* reset primary group id */
	if (to_real_uid)
	{
		/*
		**  Drop gid to real gid.
		**  On some OS we must reset the effective[/real[/saved]] gid,
		**  and then use setgid() to finally drop all group privileges.
		**  Later on we check whether we can get back the
		**  effective gid.
		*/

#if HASSETEGID
		if (setegid(RunAsGid) < 0)
		{
			syserr("drop_privileges: setegid(%d) failed",
			       (int) RunAsGid);
			rval = EX_OSERR;
		}
#else /* HASSETEGID */
# if HASSETREGID
		if (setregid(RunAsGid, RunAsGid) < 0)
		{
			syserr("drop_privileges: setregid(%d, %d) failed",
			       (int) RunAsGid, (int) RunAsGid);
			rval = EX_OSERR;
		}
# else /* HASSETREGID */
#  if HASSETRESGID
		if (setresgid(RunAsGid, RunAsGid, RunAsGid) < 0)
		{
			syserr("drop_privileges: setresgid(%d, %d, %d) failed",
			       (int) RunAsGid, (int) RunAsGid, (int) RunAsGid);
			rval = EX_OSERR;
		}
#  endif /* HASSETRESGID */
# endif /* HASSETREGID */
#endif /* HASSETEGID */
	}
	if (rval == EX_OK && (to_real_uid || RunAsGid != 0))
	{
		if (setgid(RunAsGid) < 0 && (!UseMSP || getegid() != RunAsGid))
		{
			syserr("drop_privileges: setgid(%ld) failed",
			       (long) RunAsGid);
			rval = EX_OSERR;
		}
		errno = 0;
		if (rval == EX_OK && getegid() != RunAsGid)
		{
			syserr("drop_privileges: Unable to set effective gid=%ld to RunAsGid=%ld",
			       (long) getegid(), (long) RunAsGid);
			rval = EX_OSERR;
		}
	}

	/* fiddle with uid */
	if (to_real_uid || RunAsUid != 0)
	{
		uid_t euid;

		/*
		**  Try to setuid(RunAsUid).
		**  euid must be RunAsUid,
		**  ruid must be RunAsUid unless (e|r)uid wasn't 0
		**	and we didn't have to drop privileges to the real uid.
		*/

		if (setuid(RunAsUid) < 0 ||
		    geteuid() != RunAsUid ||
		    (getuid() != RunAsUid &&
		     (to_real_uid || geteuid() == 0 || getuid() == 0)))
		{
#if HASSETREUID
			/*
			**  if ruid != RunAsUid, euid == RunAsUid, then
			**  try resetting just the real uid, then using
			**  setuid() to drop the saved-uid as well.
			*/

			if (geteuid() == RunAsUid)
			{
				if (setreuid(RunAsUid, -1) < 0)
				{
					syserr("drop_privileges: setreuid(%d, -1) failed",
					       (int) RunAsUid);
					rval = EX_OSERR;
				}
				if (setuid(RunAsUid) < 0)
				{
					syserr("drop_privileges: second setuid(%d) attempt failed",
					       (int) RunAsUid);
					rval = EX_OSERR;
				}
			}
			else
#endif /* HASSETREUID */
			{
				syserr("drop_privileges: setuid(%d) failed",
				       (int) RunAsUid);
				rval = EX_OSERR;
			}
		}
		euid = geteuid();
		if (RunAsUid != 0 && setuid(0) == 0)
		{
			/*
			**  Believe it or not, the Linux capability model
			**  allows a non-root process to override setuid()
			**  on a process running as root and prevent that
			**  process from dropping privileges.
			*/

			syserr("drop_privileges: setuid(0) succeeded (when it should not)");
			rval = EX_OSERR;
		}
		else if (RunAsUid != euid && setuid(euid) == 0)
		{
			/*
			**  Some operating systems will keep the saved-uid
			**  if a non-root effective-uid calls setuid(real-uid)
			**  making it possible to set it back again later.
			*/

			syserr("drop_privileges: Unable to drop non-root set-user-ID privileges");
			rval = EX_OSERR;
		}
	}

	if ((to_real_uid || RunAsGid != 0) &&
	    rval == EX_OK && RunAsGid != EffGid &&
	    getuid() != 0 && geteuid() != 0)
	{
		errno = 0;
		if (setgid(EffGid) == 0)
		{
			syserr("drop_privileges: setgid(%d) succeeded (when it should not)",
			       (int) EffGid);
			rval = EX_OSERR;
		}
	}

	if (tTd(47, 5))
	{
		sm_dprintf("drop_privileges: e/ruid = %d/%d e/rgid = %d/%d\n",
			   (int) geteuid(), (int) getuid(),
			   (int) getegid(), (int) getgid());
		sm_dprintf("drop_privileges: RunAsUser = %d:%d\n",
			   (int) RunAsUid, (int) RunAsGid);
		if (tTd(47, 10))
			sm_dprintf("drop_privileges: rval = %d\n", rval);
	}
	return rval;
}
/*
**  FILL_FD -- make sure a file descriptor has been properly allocated
**
**	Used to make sure that stdin/out/err are allocated on startup
**
**	Parameters:
**		fd -- the file descriptor to be filled.
**		where -- a string used for logging.  If NULL, this is
**			being called on startup, and logging should
**			not be done.
**
**	Returns:
**		none
**
**	Side Effects:
**		possibly changes MissingFds
*/

void
fill_fd(fd, where)
	int fd;
	char *where;
{
	int i;
	struct stat stbuf;

	if (fstat(fd, &stbuf) >= 0 || errno != EBADF)
		return;

	if (where != NULL)
		syserr("fill_fd: %s: fd %d not open", where, fd);
	else
		MissingFds |= 1 << fd;
	i = open(SM_PATH_DEVNULL, fd == 0 ? O_RDONLY : O_WRONLY, 0666);
	if (i < 0)
	{
		syserr("!fill_fd: %s: cannot open %s",
		       where == NULL ? "startup" : where, SM_PATH_DEVNULL);
	}
	if (fd != i)
	{
		(void) dup2(i, fd);
		(void) close(i);
	}
}
/*
**  SM_PRINTOPTIONS -- print options
**
**	Parameters:
**		options -- array of options.
**
**	Returns:
**		none.
*/

static void
sm_printoptions(options)
	char **options;
{
	int ll;
	char **av;

	av = options;
	ll = 7;
	while (*av != NULL)
	{
		if (ll + strlen(*av) > 63)
		{
			sm_dprintf("\n");
			ll = 0;
		}
		if (ll == 0)
			sm_dprintf("\t\t");
		else
			sm_dprintf(" ");
		sm_dprintf("%s", *av);
		ll += strlen(*av++) + 1;
	}
	sm_dprintf("\n");
}

/*
**  TO8BIT -- convert \octal sequences in a test mode input line
**
**	Parameters:
**		str -- the input line.
**
**	Returns:
**		none.
**
**	Side Effects:
**		replaces \0octal in str with octal value.
*/

static bool to8bit __P((char *));

static bool
to8bit(str)
	char *str;
{
	int c, len;
	char *out, *in;
	bool changed;

	if (str == NULL)
		return false;
	in = out = str;
	changed = false;
	len = 0;
	while ((c = (*str++ & 0377)) != '\0')
	{
		int oct, nxtc;

		++len;
		if (c == '\\' &&
		    (nxtc = (*str & 0377)) == '0')
		{
			oct = 0;
			while ((nxtc = (*str & 0377)) != '\0' &&
				isascii(nxtc) && isdigit(nxtc))
			{
				oct <<= 3;
				oct += nxtc - '0';
				++str;
				++len;
			}
			changed = true;
			c = oct;
		}
		*out++ = c;
	}
	*out++ = c;
	if (changed)
	{
		char *q;

		q = quote_internal_chars(in, in, &len);
		if (q != in)
			sm_strlcpy(in, q, len);
	}
	return changed;
}

/*
**  TESTMODELINE -- process a test mode input line
**
**	Parameters:
**		line -- the input line.
**		e -- the current environment.
**	Syntax:
**		#  a comment
**		.X process X as a configuration line
**		=X dump a configuration item (such as mailers)
**		$X dump a macro or class
**		/X try an activity
**		X  normal process through rule set X
*/

static void
testmodeline(line, e)
	char *line;
	ENVELOPE *e;
{
	register char *p;
	char *q;
	auto char *delimptr;
	int mid;
	int i, rs;
	STAB *map;
	char **s;
	struct rewrite *rw;
	ADDRESS a;
	char *lbp;
	auto int lbs;
	static int tryflags = RF_COPYNONE;
	char exbuf[MAXLINE];
	char lbuf[MAXLINE];
	extern unsigned char TokTypeNoC[];
	bool eightbit;

	/* skip leading spaces */
	while (*line == ' ')
		line++;

	lbp = NULL;
	eightbit = false;
	switch (line[0])
	{
	  case '#':
	  case '\0':
		return;

	  case '?':
		help("-bt", e);
		return;

	  case '.':		/* config-style settings */
		switch (line[1])
		{
		  case 'D':
			mid = macid_parse(&line[2], &delimptr);
			if (mid == 0)
				return;
			lbs = sizeof(lbuf);
			lbp = translate_dollars(delimptr, lbuf, &lbs);
			macdefine(&e->e_macro, A_TEMP, mid, lbp);
			if (lbp != lbuf)
				SM_FREE(lbp);
			break;

		  case 'C':
			if (line[2] == '\0')	/* not to call syserr() */
				return;

			mid = macid_parse(&line[2], &delimptr);
			if (mid == 0)
				return;
			lbs = sizeof(lbuf);
			lbp = translate_dollars(delimptr, lbuf, &lbs);
			expand(lbp, exbuf, sizeof(exbuf), e);
			if (lbp != lbuf)
				SM_FREE(lbp);
			p = exbuf;
			while (*p != '\0')
			{
				register char *wd;
				char delim;

				while (*p != '\0' && isascii(*p) && isspace(*p))
					p++;
				wd = p;
				while (*p != '\0' && !(isascii(*p) && isspace(*p)))
					p++;
				delim = *p;
				*p = '\0';
				if (wd[0] != '\0')
					setclass(mid, wd);
				*p = delim;
			}
			break;

		  case '\0':
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Usage: .[DC]macro value(s)\n");
			break;

		  default:
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Unknown \".\" command %s\n", line);
			break;
		}
		return;

	  case '=':		/* config-style settings */
		switch (line[1])
		{
		  case 'S':		/* dump rule set */
			rs = strtorwset(&line[2], NULL, ST_FIND);
			if (rs < 0)
			{
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Undefined ruleset %s\n", &line[2]);
				return;
			}
			rw = RewriteRules[rs];
			if (rw == NULL)
				return;
			do
			{
				(void) sm_io_putc(smioout, SM_TIME_DEFAULT,
						  'R');
				s = rw->r_lhs;
				while (*s != NULL)
				{
					xputs(smioout, *s++);
					(void) sm_io_putc(smioout,
							  SM_TIME_DEFAULT, ' ');
				}
				(void) sm_io_putc(smioout, SM_TIME_DEFAULT,
						  '\t');
				(void) sm_io_putc(smioout, SM_TIME_DEFAULT,
						  '\t');
				s = rw->r_rhs;
				while (*s != NULL)
				{
					xputs(smioout, *s++);
					(void) sm_io_putc(smioout,
							  SM_TIME_DEFAULT, ' ');
				}
				(void) sm_io_putc(smioout, SM_TIME_DEFAULT,
						  '\n');
			} while ((rw = rw->r_next) != NULL);
			break;

		  case 'M':
			for (i = 0; i < MAXMAILERS; i++)
			{
				if (Mailer[i] != NULL)
					printmailer(smioout, Mailer[i]);
			}
			break;

		  case '\0':
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Usage: =Sruleset or =M\n");
			break;

		  default:
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Unknown \"=\" command %s\n", line);
			break;
		}
		return;

	  case '-':		/* set command-line-like opts */
		switch (line[1])
		{
		  case 'd':
			tTflag(&line[2]);
			break;

		  case '\0':
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Usage: -d{debug arguments}\n");
			break;

		  default:
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Unknown \"-\" command %s\n", line);
			break;
		}
		return;

	  case '$':
		if (line[1] == '=')
		{
			mid = macid(&line[2]);
			if (mid != 0)
				stabapply(dump_class, mid);
			return;
		}
		mid = macid(&line[1]);
		if (mid == 0)
			return;
		p = macvalue(mid, e);
		if (p == NULL)
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Undefined\n");
		else
		{
			xputs(smioout, p);
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "\n");
		}
		return;

	  case '/':		/* miscellaneous commands */
		p = &line[strlen(line)];
		while (--p >= line && isascii(*p) && isspace(*p))
			*p = '\0';
		p = strpbrk(line, " \t");
		if (p != NULL)
		{
			while (isascii(*p) && isspace(*p))
				*p++ = '\0';
		}
		else
			p = "";
		if (line[1] == '\0')
		{
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Usage: /[canon|map|mx|parse|try|tryflags]\n");
			return;
		}
		if (sm_strcasecmp(&line[1], "quit") == 0)
		{
			CurEnv->e_id = NULL;
			finis(true, true, ExitStat);
			/* NOTREACHED */
		}
		if (sm_strcasecmp(&line[1], "mx") == 0)
		{
#if NAMED_BIND
			/* look up MX records */
			int nmx;
			auto int rcode;
			char *mxhosts[MAXMXHOSTS + 1];

			if (*p == '\0')
			{
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Usage: /mx address\n");
				return;
			}
			nmx = getmxrr(p, mxhosts, NULL, false, &rcode, true,
				      NULL);
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "getmxrr(%s) returns %d value(s):\n",
				p, nmx);
			for (i = 0; i < nmx; i++)
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "\t%s\n", mxhosts[i]);
#else /* NAMED_BIND */
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "No MX code compiled in\n");
#endif /* NAMED_BIND */
		}
		else if (sm_strcasecmp(&line[1], "canon") == 0)
		{
			char host[MAXHOSTNAMELEN];

			if (*p == '\0')
			{
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Usage: /canon address\n");
				return;
			}
			else if (sm_strlcpy(host, p, sizeof(host)) >= sizeof(host))
			{
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Name too long\n");
				return;
			}
			(void) getcanonname(host, sizeof(host), !HasWildcardMX,
					    NULL);
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "getcanonname(%s) returns %s\n",
					     p, host);
		}
		else if (sm_strcasecmp(&line[1], "map") == 0)
		{
			auto int rcode = EX_OK;
			char *av[2];

			if (*p == '\0')
			{
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Usage: /map mapname key\n");
				return;
			}
			for (q = p; *q != '\0' && !(isascii(*q) && isspace(*q));			     q++)
				continue;
			if (*q == '\0')
			{
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "No key specified\n");
				return;
			}
			*q++ = '\0';
			map = stab(p, ST_MAP, ST_FIND);
			if (map == NULL)
			{
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Map named \"%s\" not found\n", p);
				return;
			}
			if (!bitset(MF_OPEN, map->s_map.map_mflags) &&
			    !openmap(&(map->s_map)))
			{
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Map named \"%s\" not open\n", p);
				return;
			}
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "map_lookup: %s (%s) ", p, q);
			av[0] = q;
			av[1] = NULL;
			p = (*map->s_map.map_class->map_lookup)
					(&map->s_map, q, av, &rcode);
			if (p == NULL)
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "no match (%d)\n",
						     rcode);
			else
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "returns %s (%d)\n", p,
						     rcode);
		}
		else if (sm_strcasecmp(&line[1], "try") == 0)
		{
			MAILER *m;
			STAB *st;
			auto int rcode = EX_OK;

			q = strpbrk(p, " \t");
			if (q != NULL)
			{
				while (isascii(*q) && isspace(*q))
					*q++ = '\0';
			}
			if (q == NULL || *q == '\0')
			{
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Usage: /try mailer address\n");
				return;
			}
			st = stab(p, ST_MAILER, ST_FIND);
			if (st == NULL)
			{
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Unknown mailer %s\n", p);
				return;
			}
			m = st->s_mailer;
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Trying %s %s address %s for mailer %s\n",
				     bitset(RF_HEADERADDR, tryflags) ? "header"
							: "envelope",
				     bitset(RF_SENDERADDR, tryflags) ? "sender"
							: "recipient", q, p);
			p = remotename(q, m, tryflags, &rcode, CurEnv);
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Rcode = %d, addr = %s\n",
					     rcode, p == NULL ? "<NULL>" : p);
			e->e_to = NULL;
		}
		else if (sm_strcasecmp(&line[1], "tryflags") == 0)
		{
			if (*p == '\0')
			{
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Usage: /tryflags [Hh|Ee][Ss|Rr]\n");
				return;
			}
			for (; *p != '\0'; p++)
			{
				switch (*p)
				{
				  case 'H':
				  case 'h':
					tryflags |= RF_HEADERADDR;
					break;

				  case 'E':
				  case 'e':
					tryflags &= ~RF_HEADERADDR;
					break;

				  case 'S':
				  case 's':
					tryflags |= RF_SENDERADDR;
					break;

				  case 'R':
				  case 'r':
					tryflags &= ~RF_SENDERADDR;
					break;
				}
			}
			exbuf[0] = bitset(RF_HEADERADDR, tryflags) ? 'h' : 'e';
			exbuf[1] = ' ';
			exbuf[2] = bitset(RF_SENDERADDR, tryflags) ? 's' : 'r';
			exbuf[3] = '\0';
			macdefine(&e->e_macro, A_TEMP,
				macid("{addr_type}"), exbuf);
		}
		else if (sm_strcasecmp(&line[1], "parse") == 0)
		{
			if (*p == '\0')
			{
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Usage: /parse address\n");
				return;
			}
			q = crackaddr(p, e);
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Cracked address = ");
			xputs(smioout, q);
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "\nParsing %s %s address\n",
					     bitset(RF_HEADERADDR, tryflags) ?
							"header" : "envelope",
					     bitset(RF_SENDERADDR, tryflags) ?
							"sender" : "recipient");
			if (parseaddr(p, &a, tryflags, '\0', NULL, e, true)
			    == NULL)
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Cannot parse\n");
			else if (a.q_host != NULL && a.q_host[0] != '\0')
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "mailer %s, host %s, user %s\n",
						     a.q_mailer->m_name,
						     a.q_host,
						     a.q_user);
			else
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "mailer %s, user %s\n",
						     a.q_mailer->m_name,
						     a.q_user);
			e->e_to = NULL;
		}
		else if (sm_strcasecmp(&line[1], "header") == 0)
		{
			unsigned long ul;

			ul = chompheader(p, CHHDR_CHECK|CHHDR_USER, NULL, e);
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "ul = %lu\n", ul);
		}
#if NETINET || NETINET6
		else if (sm_strcasecmp(&line[1], "gethostbyname") == 0)
		{
			int family = AF_INET;

			q = strpbrk(p, " \t");
			if (q != NULL)
			{
				while (isascii(*q) && isspace(*q))
					*q++ = '\0';
# if NETINET6
				if (*q != '\0' && (strcmp(q, "inet6") == 0 ||
						   strcmp(q, "AAAA") == 0))
					family = AF_INET6;
# endif /* NETINET6 */
			}
			(void) sm_gethostbyname(p, family);
		}
#endif /* NETINET || NETINET6 */
		else
		{
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Unknown \"/\" command %s\n",
					     line);
		}
		(void) sm_io_flush(smioout, SM_TIME_DEFAULT);
		return;
	}

	for (p = line; isascii(*p) && isspace(*p); p++)
		continue;
	q = p;
	while (*p != '\0' && !(isascii(*p) && isspace(*p)))
		p++;
	if (*p == '\0')
	{
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "No address!\n");
		return;
	}
	*p = '\0';
	if (tTd(23, 101))
		eightbit = to8bit(p + 1);
	if (invalidaddr(p + 1, NULL, true))
		return;
	do
	{
		register char **pvp;
		char pvpbuf[PSBUFSIZE];

		pvp = prescan(++p, ',', pvpbuf, sizeof(pvpbuf), &delimptr,
			      ConfigLevel >= 9 ? TokTypeNoC : ExtTokenTab, false);
		if (pvp == NULL)
			continue;
		p = q;
		while (*p != '\0')
		{
			int status;

			rs = strtorwset(p, NULL, ST_FIND);
			if (rs < 0)
			{
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Undefined ruleset %s\n",
						     p);
				break;
			}
			status = REWRITE(pvp, rs, e);
			if (status != EX_OK)
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "== Ruleset %s (%d) status %d\n",
						     p, rs, status);
			else if (eightbit)
			{
				cataddr(pvp, NULL, exbuf, sizeof(exbuf), '\0',
					true);
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "cataddr: %s\n",
						     str2prt(exbuf));
			}
			while (*p != '\0' && *p++ != ',')
				continue;
		}
	} while (*(p = delimptr) != '\0');
	(void) sm_io_flush(smioout, SM_TIME_DEFAULT);
}

static void
dump_class(s, id)
	register STAB *s;
	int id;
{
	if (s->s_symtype != ST_CLASS)
		return;
	if (bitnset(bitidx(id), s->s_class))
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "%s\n", s->s_name);
}

/*
**  An exception type used to create QuickAbort exceptions.
**  This is my first cut at converting QuickAbort from longjmp to exceptions.
**  These exceptions have a single integer argument, which is the argument
**  to longjmp in the original code (either 1 or 2).  I don't know the
**  significance of 1 vs 2: the calls to setjmp don't care.
*/

const SM_EXC_TYPE_T EtypeQuickAbort =
{
	SmExcTypeMagic,
	"E:mta.quickabort",
	"i",
	sm_etype_printf,
	"quick abort %0",
};
