/*
 * ntpq - query an NTP server using mode 6 commands
 */
#include <config.h>
#include <ctype.h>
#include <signal.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#ifdef SYS_WINNT
# include <mswsock.h>
#endif
#include <isc/net.h>
#include <isc/result.h>

#include "ntpq.h"
#include "ntp_assert.h"
#include "ntp_stdlib.h"
#include "ntp_unixtime.h"
#include "ntp_calendar.h"
#include "ntp_select.h"
#include "ntp_assert.h"
#include "lib_strbuf.h"
#include "ntp_lineedit.h"
#include "ntp_debug.h"
#ifdef OPENSSL
# include "openssl/evp.h"
# include "openssl/objects.h"
# include "openssl/err.h"
# ifdef SYS_WINNT
#  include "openssl/opensslv.h"
#  if !defined(HAVE_EVP_MD_DO_ALL_SORTED) && OPENSSL_VERSION_NUMBER > 0x10000000L
#     define HAVE_EVP_MD_DO_ALL_SORTED	1
#  endif
# endif
# include "libssl_compat.h"
# ifdef HAVE_OPENSSL_CMAC_H
#  include <openssl/cmac.h>
#  define CMAC "AES128CMAC"
# endif
#endif
#include <ssl_applink.c>

#include "ntp_libopts.h"
#include "safecast.h"

#ifdef SYS_VXWORKS		/* vxWorks needs mode flag -casey*/
# define open(name, flags)   open(name, flags, 0777)
# define SERVER_PORT_NUM     123
#endif

/* we use COMMAND as an autogen keyword */
#ifdef COMMAND
# undef COMMAND
#endif

/*
 * Because we potentially understand a lot of commands we will run
 * interactive if connected to a terminal.
 */
int interactive = 0;		/* set to 1 when we should prompt */
const char *prompt = "ntpq> ";	/* prompt to ask him about */

/*
 * use old readvars behavior?  --old-rv processing in ntpq resets
 * this value based on the presence or absence of --old-rv.  It is
 * initialized to 1 here to maintain backward compatibility with
 * libntpq clients such as ntpsnmpd, which are free to reset it as
 * desired.
 */
int	old_rv = 1;

/*
 * How should we display the refid?
 * REFID_HASH, REFID_IPV4
 */
te_Refid drefid = -1;

/*
 * for get_systime()
 */
s_char	sys_precision;		/* local clock precision (log2 s) */

/*
 * Keyid used for authenticated requests.  Obtained on the fly.
 */
u_long info_auth_keyid = 0;

static	int	info_auth_keytype = NID_md5;	/* MD5 */
static	size_t	info_auth_hashlen = 16;		/* MD5 */
u_long	current_time;		/* needed by authkeys; not used */

/*
 * Flag which indicates we should always send authenticated requests
 */
int always_auth = 0;

/*
 * Flag which indicates raw mode output.
 */
int rawmode = 0;

/*
 * Packet version number we use
 */
u_char pktversion = NTP_OLDVERSION + 1;


/*
 * Format values
 */
#define	PADDING	0
#define	HA	1	/* host address */
#define	NA	2	/* network address */
#define	LP	3	/* leap (print in binary) */
#define	RF	4	/* refid (sometimes string, sometimes not) */
#define	AR	5	/* array of times */
#define FX	6	/* test flags */
#define TS	7	/* l_fp timestamp in hex */
#define	OC	8	/* integer, print in octal */
#define	EOV	255	/* end of table */

/*
 * For the most part ntpq simply displays what ntpd provides in the
 * mostly plain-text mode 6 responses.  A few variable names are by
 * default "cooked" to provide more human-friendly output.
 */
const var_format cookedvars[] = {
	{ "leap",		LP },
	{ "reach",		OC },
	{ "refid",		RF },
	{ "reftime",		TS },
	{ "clock",		TS },
	{ "org",		TS },
	{ "rec",		TS },
	{ "xmt",		TS },
	{ "flash",		FX },
	{ "srcadr",		HA },
	{ "peeradr",		HA },	/* compat with others */
	{ "dstadr",		NA },
	{ "filtdelay",		AR },
	{ "filtoffset",		AR },
	{ "filtdisp",		AR },
	{ "filterror",		AR },	/* compat with others */
};



/*
 * flasher bits
 */
static const char *tstflagnames[] = {
	"pkt_dup",		/* TEST1 */
	"pkt_bogus",		/* TEST2 */
	"pkt_unsync",		/* TEST3 */
	"pkt_denied",		/* TEST4 */
	"pkt_auth",		/* TEST5 */
	"pkt_stratum",		/* TEST6 */
	"pkt_header",		/* TEST7 */
	"pkt_autokey",		/* TEST8 */
	"pkt_crypto",		/* TEST9 */
	"peer_stratum",		/* TEST10 */
	"peer_dist",		/* TEST11 */
	"peer_loop",		/* TEST12 */
	"peer_unreach"		/* TEST13 */
};


int		ntpqmain	(int,	char **);
/*
 * Built in command handler declarations
 */
static	int	openhost	(const char *, int);
static	void	dump_hex_printable(const void *, size_t);
static	int	sendpkt		(void *, size_t);
static	int	getresponse	(int, int, u_short *, size_t *, const char **, int);
static	int	sendrequest	(int, associd_t, int, size_t, const char *);
static	char *	tstflags	(u_long);
#ifndef BUILD_AS_LIB
static	void	getcmds		(void);
#ifndef SYS_WINNT
static	int	abortcmd	(void);
#endif	/* SYS_WINNT */
static	void	docmd		(const char *);
static	void	tokenize	(const char *, char **, int *);
static	int	getarg		(const char *, int, arg_v *);
#endif	/* BUILD_AS_LIB */
static	int	findcmd		(const char *, struct xcmd *,
				 struct xcmd *, struct xcmd **);
static	int	rtdatetolfp	(char *, l_fp *);
static	int	decodearr	(char *, int *, l_fp *, int);
static	void	help		(struct parse *, FILE *);
static	int	helpsort	(const void *, const void *);
static	void	printusage	(struct xcmd *, FILE *);
static	void	timeout		(struct parse *, FILE *);
static	void	auth_delay	(struct parse *, FILE *);
static	void	host		(struct parse *, FILE *);
static	void	ntp_poll	(struct parse *, FILE *);
static	void	keyid		(struct parse *, FILE *);
static	void	keytype		(struct parse *, FILE *);
static	void	passwd		(struct parse *, FILE *);
static	void	hostnames	(struct parse *, FILE *);
static	void	setdebug	(struct parse *, FILE *);
static	void	quit		(struct parse *, FILE *);
static	void	showdrefid	(struct parse *, FILE *);
static	void	version		(struct parse *, FILE *);
static	void	raw		(struct parse *, FILE *);
static	void	cooked		(struct parse *, FILE *);
static	void	authenticate	(struct parse *, FILE *);
static	void	ntpversion	(struct parse *, FILE *);
static	void	warning		(const char *, ...) NTP_PRINTF(1, 2);
static	void	error		(const char *, ...) NTP_PRINTF(1, 2);
static	u_long	getkeyid	(const char *);
static	void	atoascii	(const char *, size_t, char *, size_t);
static	void	cookedprint	(int, size_t, const char *, int, int, FILE *);
static	void	rawprint	(int, size_t, const char *, int, int, FILE *);
static	void	startoutput	(void);
static	void	output		(FILE *, const char *, const char *);
static	void	endoutput	(FILE *);
static	void	outputarr	(FILE *, char *, int, l_fp *);
static	int	assoccmp	(const void *, const void *);
	u_short	varfmt		(const char *);
	void	ntpq_custom_opt_handler(tOptions *, tOptDesc *);

#ifndef BUILD_AS_LIB
static	char   *list_digest_names(void);
static	char   *insert_cmac	(char *list);
static	void	on_ctrlc	(void);
static	int	my_easprintf	(char**, const char *, ...) NTP_PRINTF(2, 3);
# if defined(OPENSSL) && defined(HAVE_EVP_MD_DO_ALL_SORTED)
static	void	list_md_fn	(const EVP_MD *m, const char *from,
				 const char *to, void *arg);
# endif /* defined(OPENSSL) && defined(HAVE_EVP_MD_DO_ALL_SORTED) */
#endif /* !defined(BUILD_AS_LIB) */


/* read a character from memory and expand to integer */
static inline int
pgetc(
	const char *cp
	)
{
	return (int)*(const unsigned char*)cp;
}



/*
 * Built-in commands we understand
 */
struct xcmd builtins[] = {
	{ "?",		help,		{  OPT|NTP_STR, NO, NO, NO },
	  { "command", "", "", "" },
	  "tell the use and syntax of commands" },
	{ "help",	help,		{  OPT|NTP_STR, NO, NO, NO },
	  { "command", "", "", "" },
	  "tell the use and syntax of commands" },
	{ "timeout",	timeout,	{ OPT|NTP_UINT, NO, NO, NO },
	  { "msec", "", "", "" },
	  "set the primary receive time out" },
	{ "delay",	auth_delay,	{ OPT|NTP_INT, NO, NO, NO },
	  { "msec", "", "", "" },
	  "set the delay added to encryption time stamps" },
	{ "host",	host,		{ OPT|NTP_STR, OPT|NTP_STR, NO, NO },
	  { "-4|-6", "hostname", "", "" },
	  "specify the host whose NTP server we talk to" },
	{ "poll",	ntp_poll,	{ OPT|NTP_UINT, OPT|NTP_STR, NO, NO },
	  { "n", "verbose", "", "" },
	  "poll an NTP server in client mode `n' times" },
	{ "passwd",	passwd,		{ OPT|NTP_STR, NO, NO, NO },
	  { "", "", "", "" },
	  "specify a password to use for authenticated requests"},
	{ "hostnames",	hostnames,	{ OPT|NTP_STR, NO, NO, NO },
	  { "yes|no", "", "", "" },
	  "specify whether hostnames or net numbers are printed"},
	{ "debug",	setdebug,	{ OPT|NTP_STR, NO, NO, NO },
	  { "no|more|less", "", "", "" },
	  "set/change debugging level" },
	{ "quit",	quit,		{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "exit ntpq" },
	{ "exit",	quit,		{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "exit ntpq" },
	{ "keyid",	keyid,		{ OPT|NTP_UINT, NO, NO, NO },
	  { "key#", "", "", "" },
	  "set keyid to use for authenticated requests" },
	{ "drefid",	showdrefid,	{ OPT|NTP_STR, NO, NO, NO },
	  { "hash|ipv4", "", "", "" },
	  "display refid's as IPv4 or hash" },
	{ "version",	version,	{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "print version number" },
	{ "raw",	raw,		{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "do raw mode variable output" },
	{ "cooked",	cooked,		{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "do cooked mode variable output" },
	{ "authenticate", authenticate,	{ OPT|NTP_STR, NO, NO, NO },
	  { "yes|no", "", "", "" },
	  "always authenticate requests to this server" },
	{ "ntpversion",	ntpversion,	{ OPT|NTP_UINT, NO, NO, NO },
	  { "version number", "", "", "" },
	  "set the NTP version number to use for requests" },
	{ "keytype",	keytype,	{ OPT|NTP_STR, NO, NO, NO },
	  { "key type %s", "", "", "" },
	  NULL },
	{ 0,		0,		{ NO, NO, NO, NO },
	  { "", "", "", "" }, "" }
};


/*
 * Default values we use.
 */
#define	DEFHOST		"localhost"	/* default host name */
#define	DEFTIMEOUT	5		/* wait 5 seconds for 1st pkt */
#define	DEFSTIMEOUT	3		/* and 3 more for each additional */
/*
 * Requests are automatically retried once, so total timeout with no
 * response is a bit over 2 * DEFTIMEOUT, or 10 seconds.  At the other
 * extreme, a request eliciting 32 packets of responses each for some
 * reason nearly DEFSTIMEOUT seconds after the prior in that series,
 * with a single packet dropped, would take around 32 * DEFSTIMEOUT, or
 * 93 seconds to fail each of two times, or 186 seconds.
 * Some commands involve a series of requests, such as "peers" and
 * "mrulist", so the cumulative timeouts are even longer for those.
 */
#define	DEFDELAY	0x51EB852	/* 20 milliseconds, l_fp fraction */
#define	LENHOSTNAME	256		/* host name is 256 characters long */
#define	MAXCMDS		100		/* maximum commands on cmd line */
#define	MAXHOSTS	200		/* maximum hosts on cmd line */
#define	MAXLINE		512		/* maximum line length */
#define	MAXTOKENS	(1+MAXARGS+2)	/* maximum number of usable tokens */
#define	MAXVARLEN	256		/* maximum length of a variable name */
#define	MAXVALLEN	2048		/* maximum length of a variable value */
#define	MAXOUTLINE	72		/* maximum length of an output line */
#define SCREENWIDTH	76		/* nominal screen width in columns */

/*
 * Some variables used and manipulated locally
 */
struct sock_timeval tvout = { DEFTIMEOUT, 0 };	/* time out for reads */
struct sock_timeval tvsout = { DEFSTIMEOUT, 0 };/* secondary time out */
l_fp delay_time;				/* delay time */
char currenthost[LENHOSTNAME];			/* current host name */
int currenthostisnum;				/* is prior text from IP? */
struct sockaddr_in hostaddr;			/* host address */
int showhostnames = 1;				/* show host names by default */
int wideremote = 0;				/* show wide remote names? */

int ai_fam_templ;				/* address family */
int ai_fam_default;				/* default address family */
SOCKET sockfd;					/* fd socket is opened on */
int havehost = 0;				/* set to 1 when host open */
int s_port = 0;
struct servent *server_entry = NULL;		/* server entry for ntp */


/*
 * Sequence number used for requests.  It is incremented before
 * it is used.
 */
u_short sequence;

/*
 * Holds data returned from queries.  Declare buffer long to be sure of
 * alignment.
 */
#define	DATASIZE	(MAXFRAGS*480)	/* maximum amount of data */
long pktdata[DATASIZE/sizeof(long)];

/*
 * assoc_cache[] is a dynamic array which allows references to
 * associations using &1 ... &N for n associations, avoiding manual
 * lookup of the current association IDs for a given ntpd.  It also
 * caches the status word for each association, retrieved incidentally.
 */
struct association *	assoc_cache;
u_int assoc_cache_slots;/* count of allocated array entries */
u_int numassoc;		/* number of cached associations */

/*
 * For commands typed on the command line (with the -c option)
 */
size_t numcmds = 0;
const char *ccmds[MAXCMDS];
#define	ADDCMD(cp)	if (numcmds < MAXCMDS) ccmds[numcmds++] = (cp)

/*
 * When multiple hosts are specified.
 */

u_int numhosts;

chost chosts[MAXHOSTS];
#define	ADDHOST(cp)						\
	do {							\
		if (numhosts < MAXHOSTS) {			\
			chosts[numhosts].name = (cp);		\
			chosts[numhosts].fam = ai_fam_templ;	\
			numhosts++;				\
		}						\
	} while (0)

/*
 * Macro definitions we use
 */
#define	ISSPACE(c)	((c) == ' ' || (c) == '\t')
#define	ISEOL(c)	((c) == '\n' || (c) == '\r' || (c) == '\0')
#define	STREQ(a, b)	(*(a) == *(b) && strcmp((a), (b)) == 0)

/*
 * Jump buffer for longjumping back to the command level.
 *
 * Since we do this from a signal handler, we use 'sig{set,long}jmp()'
 * if available. The signal is blocked by default during the excution of
 * a signal handler, and it is unspecified if '{set,long}jmp()' save and
 * restore the signal mask. They do on BSD, it depends on the GLIBC
 * version on Linux, and the gods know what happens on other OSes...
 *
 * So we use the 'sig{set,long}jmp()' functions where available, because
 * for them the semantics are well-defined. If we have to fall back to
 * '{set,long}jmp()', the CTRL-C handling might be a bit erratic.
 */
#if HAVE_DECL_SIGSETJMP && HAVE_DECL_SIGLONGJMP
# define JMP_BUF	sigjmp_buf
# define SETJMP(x)	sigsetjmp((x), 1)
# define LONGJMP(x, v)	siglongjmp((x),(v))
#else
# define JMP_BUF	jmp_buf
# define SETJMP(x)	setjmp((x))
# define LONGJMP(x, v)	longjmp((x),(v))
#endif
static	JMP_BUF		interrupt_buf;
static	volatile int	jump = 0;

/*
 * Points at file being currently printed into
 */
FILE *current_output = NULL;

/*
 * Command table imported from ntpdc_ops.c
 */
extern struct xcmd opcmds[];

char const *progname;

#ifdef NO_MAIN_ALLOWED
#ifndef BUILD_AS_LIB
CALL(ntpq,"ntpq",ntpqmain);

void clear_globals(void)
{
	extern int ntp_optind;
	showhostnames = 0;	/* don'tshow host names by default */
	ntp_optind = 0;
	server_entry = NULL;	/* server entry for ntp */
	havehost = 0;		/* set to 1 when host open */
	numassoc = 0;		/* number of cached associations */
	numcmds = 0;
	numhosts = 0;
}
#endif /* !BUILD_AS_LIB */
#endif /* NO_MAIN_ALLOWED */

/*
 * main - parse arguments and handle options
 */
#ifndef NO_MAIN_ALLOWED
int
main(
	int argc,
	char *argv[]
	)
{
	return ntpqmain(argc, argv);
}
#endif


#ifndef BUILD_AS_LIB
int
ntpqmain(
	int argc,
	char *argv[]
	)
{
	u_int ihost;
	size_t icmd;


#ifdef SYS_VXWORKS
	clear_globals();
	taskPrioritySet(taskIdSelf(), 100 );
#endif

	delay_time.l_ui = 0;
	delay_time.l_uf = DEFDELAY;

	init_lib();	/* sets up ipv4_works, ipv6_works */
	ssl_applink();
	init_auth();

	/* Check to see if we have IPv6. Otherwise default to IPv4 */
	if (!ipv6_works)
		ai_fam_default = AF_INET;

	/* Fixup keytype's help based on available digest names */

	{
	    char *list;
	    char *msg;

	    list = list_digest_names();

	    for (icmd = 0; icmd < sizeof(builtins)/sizeof(*builtins); icmd++) {
		if (strcmp("keytype", builtins[icmd].keyword) == 0) {
		    break;
		}
	    }

	    /* CID: 1295478 */
	    /* This should only "trip" if "keytype" is removed from builtins */
	    INSIST(icmd < sizeof(builtins)/sizeof(*builtins));

#ifdef OPENSSL
	    builtins[icmd].desc[0] = "digest-name";
	    my_easprintf(&msg,
			 "set key type to use for authenticated requests, one of:%s",
			 list);
#else
	    builtins[icmd].desc[0] = "md5";
	    my_easprintf(&msg,
			 "set key type to use for authenticated requests (%s)",
			 list);
#endif
	    builtins[icmd].comment = msg;
	    free(list);
	}

	progname = argv[0];

	{
		int optct = ntpOptionProcess(&ntpqOptions, argc, argv);
		argc -= optct;
		argv += optct;
	}

	/*
	 * Process options other than -c and -p, which are specially
	 * handled by ntpq_custom_opt_handler().
	 */

	debug = OPT_VALUE_SET_DEBUG_LEVEL;

	if (HAVE_OPT(IPV4))
		ai_fam_templ = AF_INET;
	else if (HAVE_OPT(IPV6))
		ai_fam_templ = AF_INET6;
	else
		ai_fam_templ = ai_fam_default;

	if (HAVE_OPT(INTERACTIVE))
		interactive = 1;

	if (HAVE_OPT(NUMERIC))
		showhostnames = 0;

	if (HAVE_OPT(WIDE))
		wideremote = 1;

	old_rv = HAVE_OPT(OLD_RV);

	drefid = OPT_VALUE_REFID;

	if (0 == argc) {
		ADDHOST(DEFHOST);
	} else {
		for (ihost = 0; ihost < (u_int)argc; ihost++) {
			if ('-' == *argv[ihost]) {
				//
				// If I really cared I'd also check:
				// 0 == argv[ihost][2]
				//
				// and there are other cases as well...
				//
				if ('4' == argv[ihost][1]) {
					ai_fam_templ = AF_INET;
					continue;
				} else if ('6' == argv[ihost][1]) {
					ai_fam_templ = AF_INET6;
					continue;
				} else {
					// XXX Throw a usage error
				}
			}
			ADDHOST(argv[ihost]);
		}
	}

	if (numcmds == 0 && interactive == 0
	    && isatty(fileno(stdin)) && isatty(fileno(stderr))) {
		interactive = 1;
	}

	set_ctrl_c_hook(on_ctrlc);
#ifndef SYS_WINNT /* Under NT cannot handle SIGINT, WIN32 spawns a handler */
	if (interactive)
		push_ctrl_c_handler(abortcmd);
#endif /* SYS_WINNT */

	if (numcmds == 0) {
		(void) openhost(chosts[0].name, chosts[0].fam);
		getcmds();
	} else {
		for (ihost = 0; ihost < numhosts; ihost++) {
			if (openhost(chosts[ihost].name, chosts[ihost].fam)) {
				if (ihost && current_output)
					fputc('\n', current_output);
				for (icmd = 0; icmd < numcmds; icmd++) {
					if (icmd && current_output)
						fputc('\n', current_output);
					docmd(ccmds[icmd]);
				}
			}
		}
	}
#ifdef SYS_WINNT
	WSACleanup();
#endif /* SYS_WINNT */
	return 0;
}
#endif /* !BUILD_AS_LIB */

/*
 * openhost - open a socket to a host
 */
static	int
openhost(
	const char *hname,
	int	    fam
	)
{
	const char svc[] = "ntp";
	char temphost[LENHOSTNAME];
	int a_info;
	struct addrinfo hints, *ai;
	sockaddr_u addr;
	size_t octets;
	const char *cp;
	char name[LENHOSTNAME];

	/*
	 * We need to get by the [] if they were entered
	 */
	if (*hname == '[') {
		cp = strchr(hname + 1, ']');
		if (!cp || (octets = (size_t)(cp - hname) - 1) >= sizeof(name)) {
			errno = EINVAL;
			warning("%s", "bad hostname/address");
			return 0;
		}
		memcpy(name, hname + 1, octets);
		name[octets] = '\0';
		hname = name;
	}

	/*
	 * First try to resolve it as an ip address and if that fails,
	 * do a fullblown (dns) lookup. That way we only use the dns
	 * when it is needed and work around some implementations that
	 * will return an "IPv4-mapped IPv6 address" address if you
	 * give it an IPv4 address to lookup.
	 */
	ZERO(hints);
	hints.ai_family = fam;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = Z_AI_NUMERICHOST;
	ai = NULL;

	a_info = getaddrinfo(hname, svc, &hints, &ai);
	if (a_info == EAI_NONAME
#ifdef EAI_NODATA
	    || a_info == EAI_NODATA
#endif
	   ) {
		hints.ai_flags = AI_CANONNAME;
#ifdef AI_ADDRCONFIG
		hints.ai_flags |= AI_ADDRCONFIG;
#endif
		a_info = getaddrinfo(hname, svc, &hints, &ai);
	}
#ifdef AI_ADDRCONFIG
	/* Some older implementations don't like AI_ADDRCONFIG. */
	if (a_info == EAI_BADFLAGS) {
		hints.ai_flags &= ~AI_ADDRCONFIG;
		a_info = getaddrinfo(hname, svc, &hints, &ai);
	}
#endif
	if (a_info != 0) {
		fprintf(stderr, "%s\n", gai_strerror(a_info));
		return 0;
	}

	INSIST(ai != NULL);
	ZERO(addr);
	octets = min(sizeof(addr), ai->ai_addrlen);
	memcpy(&addr, ai->ai_addr, octets);

	if (ai->ai_canonname == NULL) {
		strlcpy(temphost, stoa(&addr), sizeof(temphost));
		currenthostisnum = TRUE;
	} else {
		strlcpy(temphost, ai->ai_canonname, sizeof(temphost));
		currenthostisnum = FALSE;
	}

	if (debug > 2)
		printf("Opening host %s (%s)\n",
			temphost,
			(ai->ai_family == AF_INET)
			? "AF_INET"
			: (ai->ai_family == AF_INET6)
			  ? "AF_INET6"
			  : "AF-???"
			);

	if (havehost == 1) {
		if (debug > 2)
			printf("Closing old host %s\n", currenthost);
		closesocket(sockfd);
		havehost = 0;
	}
	strlcpy(currenthost, temphost, sizeof(currenthost));

	/* port maps to the same location in both families */
	s_port = NSRCPORT(&addr);
#ifdef SYS_VXWORKS
	((struct sockaddr_in6 *)&hostaddr)->sin6_port = htons(SERVER_PORT_NUM);
	if (ai->ai_family == AF_INET)
		*(struct sockaddr_in *)&hostaddr=
			*((struct sockaddr_in *)ai->ai_addr);
	else
		*(struct sockaddr_in6 *)&hostaddr=
			*((struct sockaddr_in6 *)ai->ai_addr);
#endif /* SYS_VXWORKS */

#ifdef SYS_WINNT
	{
		int optionValue = SO_SYNCHRONOUS_NONALERT;
		int err;

		err = setsockopt(INVALID_SOCKET, SOL_SOCKET, SO_OPENTYPE,
				 (void *)&optionValue, sizeof(optionValue));
		if (err) {
			mfprintf(stderr,
				 "setsockopt(SO_SYNCHRONOUS_NONALERT)"
				 " error: %m\n");
			freeaddrinfo(ai);
			exit(1);
		}
	}
#endif /* SYS_WINNT */

	sockfd = socket(ai->ai_family, ai->ai_socktype,
			ai->ai_protocol);
	if (sockfd == INVALID_SOCKET) {
		error("socket");
		freeaddrinfo(ai);
		return 0;
	}


#ifdef NEED_RCVBUF_SLOP
# ifdef SO_RCVBUF
	{ int rbufsize = DATASIZE + 2048;	/* 2K for slop */
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF,
		       (void *)&rbufsize, sizeof(int)) == -1)
		error("setsockopt");
	}
# endif
#endif

	if
#ifdef SYS_VXWORKS
	   (connect(sockfd, (struct sockaddr *)&hostaddr,
		    sizeof(hostaddr)) == -1)
#else
	   (connect(sockfd, (struct sockaddr *)ai->ai_addr,
		ai->ai_addrlen) == -1)
#endif /* SYS_VXWORKS */
	{
		error("connect");
		freeaddrinfo(ai);
		return 0;
	}
	freeaddrinfo(ai);
	havehost = 1;
	numassoc = 0;

	return 1;
}


static void
dump_hex_printable(
	const void *	data,
	size_t		len
	)
{
	/* every line shows at most 16 bytes, so we need a buffer of
	 *   4 * 16 (2 xdigits, 1 char, one sep for xdigits)
	 * + 2 * 1  (block separators)
	 * + <LF> + <NUL>
	 *---------------
	 *  68 bytes
	 */
	static const char s_xdig[16] = "0123456789ABCDEF";

	char lbuf[68];
	int  ch, rowlen;
	const u_char * cdata = data;
	char *xptr, *pptr;

	while (len) {
		memset(lbuf, ' ', sizeof(lbuf));
		xptr = lbuf;
		pptr = lbuf + 3*16 + 2;

		rowlen = (len > 16) ? 16 : (int)len;
		len -= rowlen;
		
		do {
			ch = *cdata++;
			
			*xptr++ = s_xdig[ch >> 4  ];
			*xptr++ = s_xdig[ch & 0x0F];
			if (++xptr == lbuf + 3*8)
				++xptr;

			*pptr++ = isprint(ch) ? (char)ch : '.';
		} while (--rowlen);

		*pptr++ = '\n';
		*pptr   = '\0';
		fputs(lbuf, stdout);
	}
}


/* XXX ELIMINATE sendpkt similar in ntpq.c, ntpdc.c, ntp_io.c, ntptrace.c */
/*
 * sendpkt - send a packet to the remote host
 */
static int
sendpkt(
	void *	xdata,
	size_t	xdatalen
	)
{
	if (debug >= 3)
		printf("Sending %zu octets\n", xdatalen);

	if (send(sockfd, xdata, xdatalen, 0) == -1) {
		warning("write to %s failed", currenthost);
		return -1;
	}

	if (debug >= 4) {
		printf("Request packet:\n");
		dump_hex_printable(xdata, xdatalen);
	}
	return 0;
}

/*
 * getresponse - get a (series of) response packet(s) and return the data
 */
static int
getresponse(
	int opcode,
	int associd,
	u_short *rstatus,
	size_t *rsize,
	const char **rdata,
	int timeo
	)
{
	struct ntp_control rpkt;
	struct sock_timeval tvo;
	u_short offsets[MAXFRAGS+1];
	u_short counts[MAXFRAGS+1];
	u_short offset;
	u_short count;
	size_t numfrags;
	size_t f;
	size_t ff;
	int seenlastfrag;
	int shouldbesize;
	fd_set fds;
	int n;
	int errcode;
	/* absolute timeout checks. Not 'time_t' by intention! */
	uint32_t tobase;	/* base value for timeout */
	uint32_t tospan;	/* timeout span (max delay) */
	uint32_t todiff;	/* current delay */

	memset(offsets, 0, sizeof(offsets));
	memset(counts , 0, sizeof(counts ));
	
	/*
	 * This is pretty tricky.  We may get between 1 and MAXFRAG packets
	 * back in response to the request.  We peel the data out of
	 * each packet and collect it in one long block.  When the last
	 * packet in the sequence is received we'll know how much data we
	 * should have had.  Note we use one long time out, should reconsider.
	 */
	*rsize = 0;
	if (rstatus)
		*rstatus = 0;
	*rdata = (char *)pktdata;

	numfrags = 0;
	seenlastfrag = 0;

	tobase = (uint32_t)time(NULL);
	
	FD_ZERO(&fds);

	/*
	 * Loop until we have an error or a complete response.  Nearly all
	 * code paths to loop again use continue.
	 */
	for (;;) {

		if (numfrags == 0)
			tvo = tvout;
		else
			tvo = tvsout;
		tospan = (uint32_t)tvo.tv_sec + (tvo.tv_usec != 0);

		FD_SET(sockfd, &fds);
		n = select(sockfd+1, &fds, NULL, NULL, &tvo);
		if (n == -1) {
#if !defined(SYS_WINNT) && defined(EINTR)
			/* Windows does not know about EINTR (until very
			 * recently) and the handling of console events
			 * is *very* different from POSIX/UNIX signal
			 * handling anyway.
			 *
			 * Under non-windows targets we map EINTR as
			 * 'last packet was received' and try to exit
			 * the receive sequence.
			 */
			if (errno == EINTR) {
				seenlastfrag = 1;
				goto maybe_final;
			}
#endif
			warning("select fails");
			return -1;
		}

		/*
		 * Check if this is already too late. Trash the data and
		 * fake a timeout if this is so.
		 */
		todiff = (((uint32_t)time(NULL)) - tobase) & 0x7FFFFFFFu;
		if ((n > 0) && (todiff > tospan)) {
			n = recv(sockfd, (char *)&rpkt, sizeof(rpkt), 0);
			n -= n; /* faked timeout return from 'select()',
				 * execute RMW cycle on 'n'
				 */
		}
		
		if (n <= 0) {
			/*
			 * Timed out.  Return what we have
			 */
			if (numfrags == 0) {
				if (timeo)
					fprintf(stderr,
						"%s: timed out, nothing received\n",
						currenthost);
				return ERR_TIMEOUT;
			}
			if (timeo)
				fprintf(stderr,
					"%s: timed out with incomplete data\n",
					currenthost);
			if (debug) {
				fprintf(stderr,
					"ERR_INCOMPLETE: Received fragments:\n");
				for (f = 0; f < numfrags; f++)
					fprintf(stderr,
						"%2u: %5d %5d\t%3d octets\n",
						(u_int)f, offsets[f],
						offsets[f] +
						counts[f],
						counts[f]);
				fprintf(stderr,
					"last fragment %sreceived\n",
					(seenlastfrag)
					    ? ""
					    : "not ");
			}
			return ERR_INCOMPLETE;
		}

		n = recv(sockfd, (char *)&rpkt, sizeof(rpkt), 0);
		if (n < 0) {
			warning("read");
			return -1;
		}

		if (debug >= 4) {
			printf("Response packet:\n");
			dump_hex_printable(&rpkt, n);
		}

		/*
		 * Check for format errors.  Bug proofing.
		 */
		if (n < (int)CTL_HEADER_LEN) {
			if (debug)
				printf("Short (%d byte) packet received\n", n);
			continue;
		}
		if (PKT_VERSION(rpkt.li_vn_mode) > NTP_VERSION
		    || PKT_VERSION(rpkt.li_vn_mode) < NTP_OLDVERSION) {
			if (debug)
				printf("Packet received with version %d\n",
				       PKT_VERSION(rpkt.li_vn_mode));
			continue;
		}
		if (PKT_MODE(rpkt.li_vn_mode) != MODE_CONTROL) {
			if (debug)
				printf("Packet received with mode %d\n",
				       PKT_MODE(rpkt.li_vn_mode));
			continue;
		}
		if (!CTL_ISRESPONSE(rpkt.r_m_e_op)) {
			if (debug)
				printf("Received request packet, wanted response\n");
			continue;
		}

		/*
		 * Check opcode and sequence number for a match.
		 * Could be old data getting to us.
		 */
		if (ntohs(rpkt.sequence) != sequence) {
			if (debug)
				printf("Received sequnce number %d, wanted %d\n",
				       ntohs(rpkt.sequence), sequence);
			continue;
		}
		if (CTL_OP(rpkt.r_m_e_op) != opcode) {
			if (debug)
			    printf(
				    "Received opcode %d, wanted %d (sequence number okay)\n",
				    CTL_OP(rpkt.r_m_e_op), opcode);
			continue;
		}

		/*
		 * Check the error code.  If non-zero, return it.
		 */
		if (CTL_ISERROR(rpkt.r_m_e_op)) {
			errcode = (ntohs(rpkt.status) >> 8) & 0xff;
			if (CTL_ISMORE(rpkt.r_m_e_op))
				TRACE(1, ("Error code %d received on not-final packet\n",
					  errcode));
			if (errcode == CERR_UNSPEC)
				return ERR_UNSPEC;
			return errcode;
		}

		/*
		 * Check the association ID to make sure it matches what
		 * we sent.
		 */
		if (ntohs(rpkt.associd) != associd) {
			TRACE(1, ("Association ID %d doesn't match expected %d\n",
				  ntohs(rpkt.associd), associd));
			/*
			 * Hack for silly fuzzballs which, at the time of writing,
			 * return an assID of sys.peer when queried for system variables.
			 */
#ifdef notdef
			continue;
#endif
		}

		/*
		 * Collect offset and count.  Make sure they make sense.
		 */
		offset = ntohs(rpkt.offset);
		count = ntohs(rpkt.count);

		/*
		 * validate received payload size is padded to next 32-bit
		 * boundary and no smaller than claimed by rpkt.count
		 */
		if (n & 0x3) {
			TRACE(1, ("Response packet not padded, size = %d\n",
				  n));
			continue;
		}

		shouldbesize = (CTL_HEADER_LEN + count + 3) & ~3;

		if (n < shouldbesize) {
			printf("Response packet claims %u octets payload, above %ld received\n",
			       count, (long)(n - CTL_HEADER_LEN));
			return ERR_INCOMPLETE;
		}

		if (debug >= 3 && shouldbesize > n) {
			u_int32 key;
			u_int32 *lpkt;
			int maclen;

			/*
			 * Usually we ignore authentication, but for debugging purposes
			 * we watch it here.
			 */
			/* round to 8 octet boundary */
			shouldbesize = (shouldbesize + 7) & ~7;

			maclen = n - shouldbesize;
			if (maclen >= (int)MIN_MAC_LEN) {
				printf(
					"Packet shows signs of authentication (total %d, data %d, mac %d)\n",
					n, shouldbesize, maclen);
				lpkt = (u_int32 *)&rpkt;
				printf("%08lx %08lx %08lx %08lx %08lx %08lx\n",
				       (u_long)ntohl(lpkt[(n - maclen)/sizeof(u_int32) - 3]),
				       (u_long)ntohl(lpkt[(n - maclen)/sizeof(u_int32) - 2]),
				       (u_long)ntohl(lpkt[(n - maclen)/sizeof(u_int32) - 1]),
				       (u_long)ntohl(lpkt[(n - maclen)/sizeof(u_int32)]),
				       (u_long)ntohl(lpkt[(n - maclen)/sizeof(u_int32) + 1]),
				       (u_long)ntohl(lpkt[(n - maclen)/sizeof(u_int32) + 2]));
				key = ntohl(lpkt[(n - maclen) / sizeof(u_int32)]);
				printf("Authenticated with keyid %lu\n", (u_long)key);
				if (key != 0 && key != info_auth_keyid) {
					printf("We don't know that key\n");
				} else {
					if (authdecrypt(key, (u_int32 *)&rpkt,
					    n - maclen, maclen)) {
						printf("Auth okay!\n");
					} else {
						printf("Auth failed!\n");
					}
				}
			}
		}

		TRACE(2, ("Got packet, size = %d\n", n));
		if (count > (n - CTL_HEADER_LEN)) {
			TRACE(1, ("Received count of %u octets, data in packet is %ld\n",
				  count, (long)n - CTL_HEADER_LEN));
			continue;
		}
		if (count == 0 && CTL_ISMORE(rpkt.r_m_e_op)) {
			TRACE(1, ("Received count of 0 in non-final fragment\n"));
			continue;
		}
		if (offset + count > sizeof(pktdata)) {
			TRACE(1, ("Offset %u, count %u, too big for buffer\n",
				  offset, count));
			return ERR_TOOMUCH;
		}
		if (seenlastfrag && !CTL_ISMORE(rpkt.r_m_e_op)) {
			TRACE(1, ("Received second last fragment packet\n"));
			continue;
		}

		/*
		 * So far, so good.  Record this fragment, making sure it doesn't
		 * overlap anything.
		 */
		TRACE(2, ("Packet okay\n"));

		if (numfrags > (MAXFRAGS - 1)) {
			TRACE(2, ("Number of fragments exceeds maximum %d\n",
				  MAXFRAGS - 1));
			return ERR_TOOMUCH;
		}

		/*
		 * Find the position for the fragment relative to any
		 * previously received.
		 */
		for (f = 0;
		     f < numfrags && offsets[f] < offset;
		     f++) {
			/* empty body */ ;
		}

		if (f < numfrags && offset == offsets[f]) {
			TRACE(1, ("duplicate %u octets at %u ignored, prior %u at %u\n",
				  count, offset, counts[f], offsets[f]));
			continue;
		}

		if (f > 0 && (offsets[f-1] + counts[f-1]) > offset) {
			TRACE(1, ("received frag at %u overlaps with %u octet frag at %u\n",
				  offset, counts[f-1], offsets[f-1]));
			continue;
		}

		if (f < numfrags && (offset + count) > offsets[f]) {
			TRACE(1, ("received %u octet frag at %u overlaps with frag at %u\n",
				  count, offset, offsets[f]));
			continue;
		}

		for (ff = numfrags; ff > f; ff--) {
			offsets[ff] = offsets[ff-1];
			counts[ff] = counts[ff-1];
		}
		offsets[f] = offset;
		counts[f] = count;
		numfrags++;

		/*
		 * Got that stuffed in right.  Figure out if this was the last.
		 * Record status info out of the last packet.
		 */
		if (!CTL_ISMORE(rpkt.r_m_e_op)) {
			seenlastfrag = 1;
			if (rstatus != 0)
				*rstatus = ntohs(rpkt.status);
		}

		/*
		 * Copy the data into the data buffer, and bump the
		 * timout base in case we need more.
		 */
		memcpy((char *)pktdata + offset, &rpkt.u, count);
		tobase = (uint32_t)time(NULL);
		
		/*
		 * If we've seen the last fragment, look for holes in the sequence.
		 * If there aren't any, we're done.
		 */
#if !defined(SYS_WINNT) && defined(EINTR)
		maybe_final:
#endif

		if (seenlastfrag && offsets[0] == 0) {
			for (f = 1; f < numfrags; f++)
				if (offsets[f-1] + counts[f-1] !=
				    offsets[f])
					break;
			if (f == numfrags) {
				*rsize = offsets[f-1] + counts[f-1];
				TRACE(1, ("%lu packets reassembled into response\n",
					  (u_long)numfrags));
				return 0;
			}
		}
	}  /* giant for (;;) collecting response packets */
}  /* getresponse() */


/*
 * sendrequest - format and send a request packet
 */
static int
sendrequest(
	int opcode,
	associd_t associd,
	int auth,
	size_t qsize,
	const char *qdata
	)
{
	struct ntp_control qpkt;
	size_t	pktsize;
	u_long	key_id;
	char *	pass;
	size_t	maclen;

	/*
	 * Check to make sure the data will fit in one packet
	 */
	if (qsize > CTL_MAX_DATA_LEN) {
		fprintf(stderr,
			"***Internal error!  qsize (%zu) too large\n",
			qsize);
		return 1;
	}

	/*
	 * Fill in the packet
	 */
	qpkt.li_vn_mode = PKT_LI_VN_MODE(0, pktversion, MODE_CONTROL);
	qpkt.r_m_e_op = (u_char)(opcode & CTL_OP_MASK);
	qpkt.sequence = htons(sequence);
	qpkt.status = 0;
	qpkt.associd = htons((u_short)associd);
	qpkt.offset = 0;
	qpkt.count = htons((u_short)qsize);

	pktsize = CTL_HEADER_LEN;

	/*
	 * If we have data, copy and pad it out to a 32-bit boundary.
	 */
	if (qsize > 0) {
		memcpy(&qpkt.u, qdata, (size_t)qsize);
		pktsize += qsize;
		while (pktsize & (sizeof(u_int32) - 1)) {
			qpkt.u.data[qsize++] = 0;
			pktsize++;
		}
	}

	/*
	 * If it isn't authenticated we can just send it.  Otherwise
	 * we're going to have to think about it a little.
	 */
	if (!auth && !always_auth) {
		return sendpkt(&qpkt, pktsize);
	}

	/*
	 * Pad out packet to a multiple of 8 octets to be sure
	 * receiver can handle it.
	 */
	while (pktsize & 7) {
		qpkt.u.data[qsize++] = 0;
		pktsize++;
	}

	/*
	 * Get the keyid and the password if we don't have one.
	 */
	if (info_auth_keyid == 0) {
		key_id = getkeyid("Keyid: ");
		if (key_id == 0 || key_id > NTP_MAXKEY) {
			fprintf(stderr,
				"Invalid key identifier\n");
			return 1;
		}
		info_auth_keyid = key_id;
	}
	if (!authistrusted(info_auth_keyid)) {
		pass = getpass_keytype(info_auth_keytype);
		if ('\0' == pass[0]) {
			fprintf(stderr, "Invalid password\n");
			return 1;
		}
		authusekey(info_auth_keyid, info_auth_keytype,
			   (u_char *)pass);
		authtrust(info_auth_keyid, 1);
	}

	/*
	 * Do the encryption.
	 */
	maclen = authencrypt(info_auth_keyid, (void *)&qpkt, pktsize);
	if (!maclen) {
		fprintf(stderr, "Key not found\n");
		return 1;
	} else if ((size_t)maclen != (info_auth_hashlen + sizeof(keyid_t))) {
		fprintf(stderr,
			"%zu octet MAC, %zu expected with %zu octet digest\n",
			maclen, (info_auth_hashlen + sizeof(keyid_t)),
			info_auth_hashlen);
		return 1;
	}

	return sendpkt((char *)&qpkt, pktsize + maclen);
}


/*
 * show_error_msg - display the error text for a mode 6 error response.
 */
void
show_error_msg(
	int		m6resp,
	associd_t	associd
	)
{
	if (numhosts > 1)
		fprintf(stderr, "server=%s ", currenthost);

	switch (m6resp) {

	case CERR_BADFMT:
		fprintf(stderr,
		    "***Server reports a bad format request packet\n");
		break;

	case CERR_PERMISSION:
		fprintf(stderr,
		    "***Server disallowed request (authentication?)\n");
		break;

	case CERR_BADOP:
		fprintf(stderr,
		    "***Server reports a bad opcode in request\n");
		break;

	case CERR_BADASSOC:
		fprintf(stderr,
		    "***Association ID %d unknown to server\n",
		    associd);
		break;

	case CERR_UNKNOWNVAR:
		fprintf(stderr,
		    "***A request variable unknown to the server\n");
		break;

	case CERR_BADVALUE:
		fprintf(stderr,
		    "***Server indicates a request variable was bad\n");
		break;

	case ERR_UNSPEC:
		fprintf(stderr,
		    "***Server returned an unspecified error\n");
		break;

	case ERR_TIMEOUT:
		fprintf(stderr, "***Request timed out\n");
		break;

	case ERR_INCOMPLETE:
		fprintf(stderr,
		    "***Response from server was incomplete\n");
		break;

	case ERR_TOOMUCH:
		fprintf(stderr,
		    "***Buffer size exceeded for returned data\n");
		break;

	default:
		fprintf(stderr,
		    "***Server returns unknown error code %d\n",
		    m6resp);
	}
}

/*
 * doquery - send a request and process the response, displaying
 *	     error messages for any error responses.
 */
int
doquery(
	int opcode,
	associd_t associd,
	int auth,
	size_t qsize,
	const char *qdata,
	u_short *rstatus,
	size_t *rsize,
	const char **rdata
	)
{
	return doqueryex(opcode, associd, auth, qsize, qdata, rstatus,
			 rsize, rdata, FALSE);
}


/*
 * doqueryex - send a request and process the response, optionally
 *	       displaying error messages for any error responses.
 */
int
doqueryex(
	int opcode,
	associd_t associd,
	int auth,
	size_t qsize,
	const char *qdata,
	u_short *rstatus,
	size_t *rsize,
	const char **rdata,
	int quiet
	)
{
	int res;
	int done;

	/*
	 * Check to make sure host is open
	 */
	if (!havehost) {
		fprintf(stderr, "***No host open, use `host' command\n");
		return -1;
	}

	done = 0;
	sequence++;

    again:
	/*
	 * send a request
	 */
	res = sendrequest(opcode, associd, auth, qsize, qdata);
	if (res != 0)
		return res;

	/*
	 * Get the response.  If we got a standard error, print a message
	 */
	res = getresponse(opcode, associd, rstatus, rsize, rdata, done);

	if (res > 0) {
		if (!done && (res == ERR_TIMEOUT || res == ERR_INCOMPLETE)) {
			if (res == ERR_INCOMPLETE) {
				/*
				 * better bump the sequence so we don't
				 * get confused about differing fragments.
				 */
				sequence++;
			}
			done = 1;
			goto again;
		}
		if (!quiet)
			show_error_msg(res, associd);

	}
	return res;
}


#ifndef BUILD_AS_LIB
/*
 * getcmds - read commands from the standard input and execute them
 */
static void
getcmds(void)
{
	char *	line;
	int	count;

	ntp_readline_init(interactive ? prompt : NULL);

	for (;;) {
		line = ntp_readline(&count);
		if (NULL == line)
			break;
		docmd(line);
		free(line);
	}

	ntp_readline_uninit();
}
#endif /* !BUILD_AS_LIB */


#if !defined(SYS_WINNT) && !defined(BUILD_AS_LIB)
/*
 * abortcmd - catch interrupts and abort the current command
 */
static int
abortcmd(void)
{
	if (current_output == stdout)
		(void) fflush(stdout);
	putc('\n', stderr);
	(void) fflush(stderr);
	if (jump) {
		jump = 0;
		LONGJMP(interrupt_buf, 1);
	}
	return TRUE;
}
#endif	/* !SYS_WINNT && !BUILD_AS_LIB */


#ifndef	BUILD_AS_LIB
/*
 * docmd - decode the command line and execute a command
 */
static void
docmd(
	const char *cmdline
	)
{
	char *tokens[1+MAXARGS+2];
	struct parse pcmd;
	int ntok;
	static int i;
	struct xcmd *xcmd;

	/*
	 * Tokenize the command line.  If nothing on it, return.
	 */
	tokenize(cmdline, tokens, &ntok);
	if (ntok == 0)
	    return;

	/*
	 * Find the appropriate command description.
	 */
	i = findcmd(tokens[0], builtins, opcmds, &xcmd);
	if (i == 0) {
		(void) fprintf(stderr, "***Command `%s' unknown\n",
			       tokens[0]);
		return;
	} else if (i >= 2) {
		(void) fprintf(stderr, "***Command `%s' ambiguous\n",
			       tokens[0]);
		return;
	}

	/* Warn about ignored extra args */
	for (i = MAXARGS + 1; i < ntok ; ++i) {
		fprintf(stderr, "***Extra arg `%s' ignored\n", tokens[i]);
	}

	/*
	 * Save the keyword, then walk through the arguments, interpreting
	 * as we go.
	 */
	pcmd.keyword = tokens[0];
	pcmd.nargs = 0;
	for (i = 0; i < MAXARGS && xcmd->arg[i] != NO; i++) {
		if ((i+1) >= ntok) {
			if (!(xcmd->arg[i] & OPT)) {
				printusage(xcmd, stderr);
				return;
			}
			break;
		}
		if ((xcmd->arg[i] & OPT) && (*tokens[i+1] == '>'))
			break;
		if (!getarg(tokens[i+1], (int)xcmd->arg[i], &pcmd.argval[i]))
			return;
		pcmd.nargs++;
	}

	i++;
	if (i < ntok && *tokens[i] == '>') {
		char *fname;

		if (*(tokens[i]+1) != '\0')
			fname = tokens[i]+1;
		else if ((i+1) < ntok)
			fname = tokens[i+1];
		else {
			(void) fprintf(stderr, "***No file for redirect\n");
			return;
		}

		current_output = fopen(fname, "w");
		if (current_output == NULL) {
			(void) fprintf(stderr, "***Error opening %s: ", fname);
			perror("");
			return;
		}
	} else {
		current_output = stdout;
	}

	if (interactive) {
		if ( ! SETJMP(interrupt_buf)) {
			jump = 1;
			(xcmd->handler)(&pcmd, current_output);
			jump = 0;
		} else {
			fflush(current_output);
			fputs("\n >>> command aborted <<<\n", stderr);
			fflush(stderr);
		}

	} else {
		jump = 0;
		(xcmd->handler)(&pcmd, current_output);
	}
	if ((NULL != current_output) && (stdout != current_output)) {
		(void)fclose(current_output);
		current_output = NULL;
	}
}


/*
 * tokenize - turn a command line into tokens
 *
 * SK: Modified to allow a quoted string
 *
 * HMS: If the first character of the first token is a ':' then (after
 * eating inter-token whitespace) the 2nd token is the rest of the line.
 */

static void
tokenize(
	const char *line,
	char **tokens,
	int *ntok
	)
{
	register const char *cp;
	register char *sp;
	static char tspace[MAXLINE];

	sp = tspace;
	cp = line;
	for (*ntok = 0; *ntok < MAXTOKENS; (*ntok)++) {
		tokens[*ntok] = sp;

		/* Skip inter-token whitespace */
		while (ISSPACE(*cp))
		    cp++;

		/* If we're at EOL we're done */
		if (ISEOL(*cp))
		    break;

		/* If this is the 2nd token and the first token begins
		 * with a ':', then just grab to EOL.
		 */

		if (*ntok == 1 && tokens[0][0] == ':') {
			do {
				if (sp - tspace >= MAXLINE)
					goto toobig;
				*sp++ = *cp++;
			} while (!ISEOL(*cp));
		}

		/* Check if this token begins with a double quote.
		 * If yes, continue reading till the next double quote
		 */
		else if (*cp == '\"') {
			++cp;
			do {
				if (sp - tspace >= MAXLINE)
					goto toobig;
				*sp++ = *cp++;
			} while ((*cp != '\"') && !ISEOL(*cp));
			/* HMS: a missing closing " should be an error */
		}
		else {
			do {
				if (sp - tspace >= MAXLINE)
					goto toobig;
				*sp++ = *cp++;
			} while ((*cp != '\"') && !ISSPACE(*cp) && !ISEOL(*cp));
			/* HMS: Why check for a " in the previous line? */
		}

		if (sp - tspace >= MAXLINE)
			goto toobig;
		*sp++ = '\0';
	}
	return;

  toobig:
	*ntok = 0;
	fprintf(stderr,
		"***Line `%s' is too big\n",
		line);
	return;
}


/*
 * getarg - interpret an argument token
 */
static int
getarg(
	const char *str,
	int code,
	arg_v *argp
	)
{
	u_long ul;

	switch (code & ~OPT) {
	case NTP_STR:
		argp->string = str;
		break;

	case NTP_ADD:
		if (!getnetnum(str, &argp->netnum, NULL, 0))
			return 0;
		break;

	case NTP_UINT:
		if ('&' == str[0]) {
			if (!atouint(&str[1], &ul)) {
				fprintf(stderr,
					"***Association index `%s' invalid/undecodable\n",
					str);
				return 0;
			}
			if (0 == numassoc) {
				dogetassoc(stdout);
				if (0 == numassoc) {
					fprintf(stderr,
						"***No associations found, `%s' unknown\n",
						str);
					return 0;
				}
			}
			ul = min(ul, numassoc);
			argp->uval = assoc_cache[ul - 1].assid;
			break;
		}
		if (!atouint(str, &argp->uval)) {
			fprintf(stderr, "***Illegal unsigned value %s\n",
				str);
			return 0;
		}
		break;

	case NTP_INT:
		if (!atoint(str, &argp->ival)) {
			fprintf(stderr, "***Illegal integer value %s\n",
				str);
			return 0;
		}
		break;

	case IP_VERSION:
		if (!strcmp("-6", str)) {
			argp->ival = 6;
		} else if (!strcmp("-4", str)) {
			argp->ival = 4;
		} else {
			fprintf(stderr, "***Version must be either 4 or 6\n");
			return 0;
		}
		break;
	}

	return 1;
}
#endif	/* !BUILD_AS_LIB */


/*
 * findcmd - find a command in a command description table
 */
static int
findcmd(
	const char *	str,
	struct xcmd *	clist1,
	struct xcmd *	clist2,
	struct xcmd **	cmd
	)
{
	struct xcmd *cl;
	size_t clen;
	int nmatch;
	struct xcmd *nearmatch = NULL;
	struct xcmd *clist;

	clen = strlen(str);
	nmatch = 0;
	if (clist1 != 0)
	    clist = clist1;
	else if (clist2 != 0)
	    clist = clist2;
	else
	    return 0;

    again:
	for (cl = clist; cl->keyword != 0; cl++) {
		/* do a first character check, for efficiency */
		if (*str != *(cl->keyword))
		    continue;
		if (strncmp(str, cl->keyword, (unsigned)clen) == 0) {
			/*
			 * Could be extact match, could be approximate.
			 * Is exact if the length of the keyword is the
			 * same as the str.
			 */
			if (*((cl->keyword) + clen) == '\0') {
				*cmd = cl;
				return 1;
			}
			nmatch++;
			nearmatch = cl;
		}
	}

	/*
	 * See if there is more to do.  If so, go again.  Sorry about the
	 * goto, too much looking at BSD sources...
	 */
	if (clist == clist1 && clist2 != 0) {
		clist = clist2;
		goto again;
	}

	/*
	 * If we got extactly 1 near match, use it, else return number
	 * of matches.
	 */
	if (nmatch == 1) {
		*cmd = nearmatch;
		return 1;
	}
	return nmatch;
}


/*
 * getnetnum - given a host name, return its net number
 *	       and (optional) full name
 */
int
getnetnum(
	const char *hname,
	sockaddr_u *num,
	char *fullhost,
	int af
	)
{
	struct addrinfo hints, *ai = NULL;

	ZERO(hints);
	hints.ai_flags = AI_CANONNAME;
#ifdef AI_ADDRCONFIG
	hints.ai_flags |= AI_ADDRCONFIG;
#endif

	/*
	 * decodenetnum only works with addresses, but handles syntax
	 * that getaddrinfo doesn't:  [2001::1]:1234
	 */
	if (decodenetnum(hname, num)) {
		if (fullhost != NULL)
			getnameinfo(&num->sa, SOCKLEN(num), fullhost,
				    LENHOSTNAME, NULL, 0, 0);
		return 1;
	} else if (getaddrinfo(hname, "ntp", &hints, &ai) == 0) {
		INSIST(sizeof(*num) >= ai->ai_addrlen);
		memcpy(num, ai->ai_addr, ai->ai_addrlen);
		if (fullhost != NULL) {
			if (ai->ai_canonname != NULL)
				strlcpy(fullhost, ai->ai_canonname,
					LENHOSTNAME);
			else
				getnameinfo(&num->sa, SOCKLEN(num),
					    fullhost, LENHOSTNAME, NULL,
					    0, 0);
		}
		freeaddrinfo(ai);
		return 1;
	}
	fprintf(stderr, "***Can't find host %s\n", hname);

	return 0;
}


/*
 * nntohost - convert network number to host name.  This routine enforces
 *	       the showhostnames setting.
 */
const char *
nntohost(
	sockaddr_u *netnum
	)
{
	return nntohost_col(netnum, LIB_BUFLENGTH - 1, FALSE);
}


/*
 * nntohost_col - convert network number to host name in fixed width.
 *		  This routine enforces the showhostnames setting.
 *		  When displaying hostnames longer than the width,
 *		  the first part of the hostname is displayed.  When
 *		  displaying numeric addresses longer than the width,
 *		  Such as IPv6 addresses, the caller decides whether
 *		  the first or last of the numeric address is used.
 */
const char *
nntohost_col(
	sockaddr_u *	addr,
	size_t		width,
	int		preserve_lowaddrbits
	)
{
	const char *	out;

	if (!showhostnames || SOCK_UNSPEC(addr)) {
		if (preserve_lowaddrbits)
			out = trunc_left(stoa(addr), width);
		else
			out = trunc_right(stoa(addr), width);
	} else if (ISREFCLOCKADR(addr)) {
		out = refnumtoa(addr);
	} else {
		out = trunc_right(socktohost(addr), width);
	}
	return out;
}


/*
 * nntohostp() is the same as nntohost() plus a :port suffix
 */
const char *
nntohostp(
	sockaddr_u *netnum
	)
{
	const char *	hostn;
	char *		buf;

	if (!showhostnames || SOCK_UNSPEC(netnum))
		return sptoa(netnum);
	else if (ISREFCLOCKADR(netnum))
		return refnumtoa(netnum);

	hostn = socktohost(netnum);
	LIB_GETBUF(buf);
	snprintf(buf, LIB_BUFLENGTH, "%s:%u", hostn, SRCPORT(netnum));

	return buf;
}

/*
 * rtdatetolfp - decode an RT-11 date into an l_fp
 */
static int
rtdatetolfp(
	char *str,
	l_fp *lfp
	)
{
	register char *cp;
	register int i;
	struct calendar cal;
	char buf[4];

	cal.yearday = 0;

	/*
	 * An RT-11 date looks like:
	 *
	 * d[d]-Mth-y[y] hh:mm:ss
	 *
	 * (No docs, but assume 4-digit years are also legal...)
	 *
	 * d[d]-Mth-y[y[y[y]]] hh:mm:ss
	 */
	cp = str;
	if (!isdigit(pgetc(cp))) {
		if (*cp == '-') {
			/*
			 * Catch special case
			 */
			L_CLR(lfp);
			return 1;
		}
		return 0;
	}

	cal.monthday = (u_char) (*cp++ - '0');	/* ascii dependent */
	if (isdigit(pgetc(cp))) {
		cal.monthday = (u_char)((cal.monthday << 3) + (cal.monthday << 1));
		cal.monthday = (u_char)(cal.monthday + *cp++ - '0');
	}

	if (*cp++ != '-')
	    return 0;

	for (i = 0; i < 3; i++)
	    buf[i] = *cp++;
	buf[3] = '\0';

	for (i = 0; i < 12; i++)
	    if (STREQ(buf, months[i]))
		break;
	if (i == 12)
	    return 0;
	cal.month = (u_char)(i + 1);

	if (*cp++ != '-')
	    return 0;

	if (!isdigit(pgetc(cp)))
	    return 0;
	cal.year = (u_short)(*cp++ - '0');
	if (isdigit(pgetc(cp))) {
		cal.year = (u_short)((cal.year << 3) + (cal.year << 1));
		cal.year = (u_short)(*cp++ - '0');
	}
	if (isdigit(pgetc(cp))) {
		cal.year = (u_short)((cal.year << 3) + (cal.year << 1));
		cal.year = (u_short)(cal.year + *cp++ - '0');
	}
	if (isdigit(pgetc(cp))) {
		cal.year = (u_short)((cal.year << 3) + (cal.year << 1));
		cal.year = (u_short)(cal.year + *cp++ - '0');
	}

	/*
	 * Catch special case.  If cal.year == 0 this is a zero timestamp.
	 */
	if (cal.year == 0) {
		L_CLR(lfp);
		return 1;
	}

	if (*cp++ != ' ' || !isdigit(pgetc(cp)))
	    return 0;
	cal.hour = (u_char)(*cp++ - '0');
	if (isdigit(pgetc(cp))) {
		cal.hour = (u_char)((cal.hour << 3) + (cal.hour << 1));
		cal.hour = (u_char)(cal.hour + *cp++ - '0');
	}

	if (*cp++ != ':' || !isdigit(pgetc(cp)))
	    return 0;
	cal.minute = (u_char)(*cp++ - '0');
	if (isdigit(pgetc(cp))) {
		cal.minute = (u_char)((cal.minute << 3) + (cal.minute << 1));
		cal.minute = (u_char)(cal.minute + *cp++ - '0');
	}

	if (*cp++ != ':' || !isdigit(pgetc(cp)))
	    return 0;
	cal.second = (u_char)(*cp++ - '0');
	if (isdigit(pgetc(cp))) {
		cal.second = (u_char)((cal.second << 3) + (cal.second << 1));
		cal.second = (u_char)(cal.second + *cp++ - '0');
	}

	/*
	 * For RT-11, 1972 seems to be the pivot year
	 */
	if (cal.year < 72)
		cal.year += 2000;
	if (cal.year < 100)
		cal.year += 1900;

	lfp->l_ui = caltontp(&cal);
	lfp->l_uf = 0;
	return 1;
}


/*
 * decodets - decode a timestamp into an l_fp format number, with
 *	      consideration of fuzzball formats.
 */
int
decodets(
	char *str,
	l_fp *lfp
	)
{
	char *cp;
	char buf[30];
	size_t b;

	/*
	 * If it starts with a 0x, decode as hex.
	 */
	if (*str == '0' && (*(str+1) == 'x' || *(str+1) == 'X'))
		return hextolfp(str+2, lfp);

	/*
	 * If it starts with a '"', try it as an RT-11 date.
	 */
	if (*str == '"') {
		cp = str + 1;
		b = 0;
		while ('"' != *cp && '\0' != *cp &&
		       b < COUNTOF(buf) - 1)
			buf[b++] = *cp++;
		buf[b] = '\0';
		return rtdatetolfp(buf, lfp);
	}

	/*
	 * Might still be hex.  Check out the first character.  Talk
	 * about heuristics!
	 */
	if ((*str >= 'A' && *str <= 'F') || (*str >= 'a' && *str <= 'f'))
		return hextolfp(str, lfp);

	/*
	 * Try it as a decimal.  If this fails, try as an unquoted
	 * RT-11 date.  This code should go away eventually.
	 */
	if (atolfp(str, lfp))
		return 1;

	return rtdatetolfp(str, lfp);
}


/*
 * decodetime - decode a time value.  It should be in milliseconds
 */
int
decodetime(
	char *str,
	l_fp *lfp
	)
{
	return mstolfp(str, lfp);
}


/*
 * decodeint - decode an integer
 */
int
decodeint(
	char *str,
	long *val
	)
{
	if (*str == '0') {
		if (*(str+1) == 'x' || *(str+1) == 'X')
		    return hextoint(str+2, (u_long *)val);
		return octtoint(str, (u_long *)val);
	}
	return atoint(str, val);
}


/*
 * decodeuint - decode an unsigned integer
 */
int
decodeuint(
	char *str,
	u_long *val
	)
{
	if (*str == '0') {
		if (*(str + 1) == 'x' || *(str + 1) == 'X')
			return (hextoint(str + 2, val));
		return (octtoint(str, val));
	}
	return (atouint(str, val));
}


/*
 * decodearr - decode an array of time values
 */
static int
decodearr(
	char *cp,
	int  *narr,
	l_fp *lfpa,
	int   amax
	)
{
	char *bp;
	char buf[60];

	*narr = 0;

	while (*narr < amax && *cp) {
		if (isspace(pgetc(cp))) {
			do
				++cp;
			while (*cp && isspace(pgetc(cp)));
		} else {
			bp = buf;
			do {
				if (bp != (buf + sizeof(buf) - 1))
					*bp++ = *cp;
				++cp;
			} while (*cp && !isspace(pgetc(cp)));
			*bp = '\0';

			if (!decodetime(buf, lfpa))
				return 0;
			++(*narr);
			++lfpa;
		}
	}
	return 1;
}


/*
 * Finally, the built in command handlers
 */

/*
 * help - tell about commands, or details of a particular command
 */
static void
help(
	struct parse *pcmd,
	FILE *fp
	)
{
	struct xcmd *xcp = NULL;	/* quiet warning */
	const char *cmd;
	const char *list[100];
	size_t word, words;
	size_t row, rows;
	size_t col, cols;
	size_t length;

	if (pcmd->nargs == 0) {
		words = 0;
		for (xcp = builtins; xcp->keyword != NULL; xcp++) {
			if (*(xcp->keyword) != '?' &&
			    words < COUNTOF(list))
				list[words++] = xcp->keyword;
		}
		for (xcp = opcmds; xcp->keyword != NULL; xcp++)
			if (words < COUNTOF(list))
				list[words++] = xcp->keyword;

		qsort((void *)list, words, sizeof(list[0]), helpsort);
		col = 0;
		for (word = 0; word < words; word++) {
			length = strlen(list[word]);
			col = max(col, length);
		}

		cols = SCREENWIDTH / ++col;
		rows = (words + cols - 1) / cols;

		fprintf(fp, "ntpq commands:\n");

		for (row = 0; row < rows; row++) {
			for (word = row; word < words; word += rows)
				fprintf(fp, "%-*.*s", (int)col,
					(int)col - 1, list[word]);
			fprintf(fp, "\n");
		}
	} else {
		cmd = pcmd->argval[0].string;
		words = findcmd(cmd, builtins, opcmds, &xcp);
		if (words == 0) {
			fprintf(stderr,
				"Command `%s' is unknown\n", cmd);
			return;
		} else if (words >= 2) {
			fprintf(stderr,
				"Command `%s' is ambiguous\n", cmd);
			return;
		}
		fprintf(fp, "function: %s\n", xcp->comment);
		printusage(xcp, fp);
	}
}


/*
 * helpsort - do hostname qsort comparisons
 */
static int
helpsort(
	const void *t1,
	const void *t2
	)
{
	const char * const *	name1 = t1;
	const char * const *	name2 = t2;

	return strcmp(*name1, *name2);
}


/*
 * printusage - print usage information for a command
 */
static void
printusage(
	struct xcmd *xcp,
	FILE *fp
	)
{
	register int i;

	/* XXX: Do we need to warn about extra args here too? */

	(void) fprintf(fp, "usage: %s", xcp->keyword);
	for (i = 0; i < MAXARGS && xcp->arg[i] != NO; i++) {
		if (xcp->arg[i] & OPT)
		    (void) fprintf(fp, " [ %s ]", xcp->desc[i]);
		else
		    (void) fprintf(fp, " %s", xcp->desc[i]);
	}
	(void) fprintf(fp, "\n");
}


/*
 * timeout - set time out time
 */
static void
timeout(
	struct parse *pcmd,
	FILE *fp
	)
{
	int val;

	if (pcmd->nargs == 0) {
		val = (int)tvout.tv_sec * 1000 + tvout.tv_usec / 1000;
		(void) fprintf(fp, "primary timeout %d ms\n", val);
	} else {
		tvout.tv_sec = pcmd->argval[0].uval / 1000;
		tvout.tv_usec = (pcmd->argval[0].uval - ((long)tvout.tv_sec * 1000))
			* 1000;
	}
}


/*
 * auth_delay - set delay for auth requests
 */
static void
auth_delay(
	struct parse *pcmd,
	FILE *fp
	)
{
	int isneg;
	u_long val;

	if (pcmd->nargs == 0) {
		val = delay_time.l_ui * 1000 + delay_time.l_uf / 4294967;
		(void) fprintf(fp, "delay %lu ms\n", val);
	} else {
		if (pcmd->argval[0].ival < 0) {
			isneg = 1;
			val = (u_long)(-pcmd->argval[0].ival);
		} else {
			isneg = 0;
			val = (u_long)pcmd->argval[0].ival;
		}

		delay_time.l_ui = val / 1000;
		val %= 1000;
		delay_time.l_uf = val * 4294967;	/* 2**32/1000 */

		if (isneg)
		    L_NEG(&delay_time);
	}
}


/*
 * host - set the host we are dealing with.
 */
static void
host(
	struct parse *pcmd,
	FILE *fp
	)
{
	int i;

	if (pcmd->nargs == 0) {
		if (havehost)
			(void) fprintf(fp, "current host is %s\n",
					   currenthost);
		else
			(void) fprintf(fp, "no current host\n");
		return;
	}

	i = 0;
	ai_fam_templ = ai_fam_default;
	if (pcmd->nargs == 2) {
		if (!strcmp("-4", pcmd->argval[i].string))
			ai_fam_templ = AF_INET;
		else if (!strcmp("-6", pcmd->argval[i].string))
			ai_fam_templ = AF_INET6;
		else
			goto no_change;
		i = 1;
	}
	if (openhost(pcmd->argval[i].string, ai_fam_templ)) {
		fprintf(fp, "current host set to %s\n", currenthost);
	} else {
    no_change:
		if (havehost)
			fprintf(fp, "current host remains %s\n",
				currenthost);
		else
			fprintf(fp, "still no current host\n");
	}
}


/*
 * poll - do one (or more) polls of the host via NTP
 */
/*ARGSUSED*/
static void
ntp_poll(
	struct parse *pcmd,
	FILE *fp
	)
{
	(void) fprintf(fp, "poll not implemented yet\n");
}


/*
 * showdrefid2str - return a string explanation of the value of drefid
 */
static const char *
showdrefid2str(void)
{
	switch (drefid) {
	    case REFID_HASH:
	    	return "hash";
	    case REFID_IPV4:
	    	return "ipv4";
	    default:
	    	return "Unknown";
	}
}


/*
 * drefid - display/change "display hash" 
 */
static void
showdrefid(
	struct parse *pcmd,
	FILE *fp
	)
{
	if (pcmd->nargs == 0) {
		(void) fprintf(fp, "drefid value is %s\n", showdrefid2str());
		return;
	} else if (STREQ(pcmd->argval[0].string, "hash")) {
		drefid = REFID_HASH;
	} else if (STREQ(pcmd->argval[0].string, "ipv4")) {
		drefid = REFID_IPV4;
	} else {
		(void) fprintf(fp, "What?\n");
		return;
	}
	(void) fprintf(fp, "drefid value set to %s\n", showdrefid2str());
}


/*
 * keyid - get a keyid to use for authenticating requests
 */
static void
keyid(
	struct parse *pcmd,
	FILE *fp
	)
{
	if (pcmd->nargs == 0) {
		if (info_auth_keyid == 0)
		    (void) fprintf(fp, "no keyid defined\n");
		else
		    (void) fprintf(fp, "keyid is %lu\n", (u_long)info_auth_keyid);
	} else {
		/* allow zero so that keyid can be cleared. */
		if(pcmd->argval[0].uval > NTP_MAXKEY)
		    (void) fprintf(fp, "Invalid key identifier\n");
		info_auth_keyid = pcmd->argval[0].uval;
	}
}

/*
 * keytype - get type of key to use for authenticating requests
 */
static void
keytype(
	struct parse *pcmd,
	FILE *fp
	)
{
	const char *	digest_name;
	size_t		digest_len;
	int		key_type;

	if (!pcmd->nargs) {
		fprintf(fp, "keytype is %s with %lu octet digests\n",
			keytype_name(info_auth_keytype),
			(u_long)info_auth_hashlen);
		return;
	}

	digest_name = pcmd->argval[0].string;
	digest_len = 0;
	key_type = keytype_from_text(digest_name, &digest_len);

	if (!key_type) {
		fprintf(fp, "keytype is not valid. "
#ifdef OPENSSL
			"Type \"help keytype\" for the available digest types.\n");
#else
			"Only \"md5\" is available.\n");
#endif
		return;
	}

	info_auth_keytype = key_type;
	info_auth_hashlen = digest_len;
}


/*
 * passwd - get an authentication key
 */
/*ARGSUSED*/
static void
passwd(
	struct parse *pcmd,
	FILE *fp
	)
{
	const char *pass;

	if (info_auth_keyid == 0) {
		info_auth_keyid = getkeyid("Keyid: ");
		if (info_auth_keyid == 0) {
			(void)fprintf(fp, "Keyid must be defined\n");
			return;
		}
	}
	if (pcmd->nargs >= 1)
		pass = pcmd->argval[0].string;
	else {
		pass = getpass_keytype(info_auth_keytype);
		if ('\0' == pass[0]) {
			fprintf(fp, "Password unchanged\n");
			return;
		}
	}
	authusekey(info_auth_keyid, info_auth_keytype,
		   (const u_char *)pass);
	authtrust(info_auth_keyid, 1);
}


/*
 * hostnames - set the showhostnames flag
 */
static void
hostnames(
	struct parse *pcmd,
	FILE *fp
	)
{
	if (pcmd->nargs == 0) {
		if (showhostnames)
		    (void) fprintf(fp, "hostnames being shown\n");
		else
		    (void) fprintf(fp, "hostnames not being shown\n");
	} else {
		if (STREQ(pcmd->argval[0].string, "yes"))
		    showhostnames = 1;
		else if (STREQ(pcmd->argval[0].string, "no"))
		    showhostnames = 0;
		else
		    (void)fprintf(stderr, "What?\n");
	}
}



/*
 * setdebug - set/change debugging level
 */
static void
setdebug(
	struct parse *pcmd,
	FILE *fp
	)
{
	if (pcmd->nargs == 0) {
		(void) fprintf(fp, "debug level is %d\n", debug);
		return;
	} else if (STREQ(pcmd->argval[0].string, "no")) {
		debug = 0;
	} else if (STREQ(pcmd->argval[0].string, "more")) {
		debug++;
	} else if (STREQ(pcmd->argval[0].string, "less")) {
		debug--;
	} else {
		(void) fprintf(fp, "What?\n");
		return;
	}
	(void) fprintf(fp, "debug level set to %d\n", debug);
}


/*
 * quit - stop this nonsense
 */
/*ARGSUSED*/
static void
quit(
	struct parse *pcmd,
	FILE *fp
	)
{
	if (havehost)
	    closesocket(sockfd);	/* cleanliness next to godliness */
	exit(0);
}


/*
 * version - print the current version number
 */
/*ARGSUSED*/
static void
version(
	struct parse *pcmd,
	FILE *fp
	)
{

	(void) fprintf(fp, "%s\n", Version);
	return;
}


/*
 * raw - set raw mode output
 */
/*ARGSUSED*/
static void
raw(
	struct parse *pcmd,
	FILE *fp
	)
{
	rawmode = 1;
	(void) fprintf(fp, "Output set to raw\n");
}


/*
 * cooked - set cooked mode output
 */
/*ARGSUSED*/
static void
cooked(
	struct parse *pcmd,
	FILE *fp
	)
{
	rawmode = 0;
	(void) fprintf(fp, "Output set to cooked\n");
	return;
}


/*
 * authenticate - always authenticate requests to this host
 */
static void
authenticate(
	struct parse *pcmd,
	FILE *fp
	)
{
	if (pcmd->nargs == 0) {
		if (always_auth) {
			(void) fprintf(fp,
				       "authenticated requests being sent\n");
		} else
		    (void) fprintf(fp,
				   "unauthenticated requests being sent\n");
	} else {
		if (STREQ(pcmd->argval[0].string, "yes")) {
			always_auth = 1;
		} else if (STREQ(pcmd->argval[0].string, "no")) {
			always_auth = 0;
		} else
		    (void)fprintf(stderr, "What?\n");
	}
}


/*
 * ntpversion - choose the NTP version to use
 */
static void
ntpversion(
	struct parse *pcmd,
	FILE *fp
	)
{
	if (pcmd->nargs == 0) {
		(void) fprintf(fp,
			       "NTP version being claimed is %d\n", pktversion);
	} else {
		if (pcmd->argval[0].uval < NTP_OLDVERSION
		    || pcmd->argval[0].uval > NTP_VERSION) {
			(void) fprintf(stderr, "versions %d to %d, please\n",
				       NTP_OLDVERSION, NTP_VERSION);
		} else {
			pktversion = (u_char) pcmd->argval[0].uval;
		}
	}
}


static void __attribute__((__format__(__printf__, 1, 0)))
vwarning(const char *fmt, va_list ap)
{
	int serrno = errno;
	(void) fprintf(stderr, "%s: ", progname);
	vfprintf(stderr, fmt, ap);
	(void) fprintf(stderr, ": %s\n", strerror(serrno));
}

/*
 * warning - print a warning message
 */
static void __attribute__((__format__(__printf__, 1, 2)))
warning(
	const char *fmt,
	...
	)
{
	va_list ap;
	va_start(ap, fmt);
	vwarning(fmt, ap);
	va_end(ap);
}


/*
 * error - print a message and exit
 */
static void __attribute__((__format__(__printf__, 1, 2)))
error(
	const char *fmt,
	...
	)
{
	va_list ap;
	va_start(ap, fmt);
	vwarning(fmt, ap);
	va_end(ap);
	exit(1);
}
/*
 * getkeyid - prompt the user for a keyid to use
 */
static u_long
getkeyid(
	const char *keyprompt
	)
{
	int c;
	FILE *fi;
	char pbuf[20];
	size_t i;
	size_t ilim;

#ifndef SYS_WINNT
	if ((fi = fdopen(open("/dev/tty", 2), "r")) == NULL)
#else
	if ((fi = _fdopen(open("CONIN$", _O_TEXT), "r")) == NULL)
#endif /* SYS_WINNT */
		fi = stdin;
	else
		setbuf(fi, (char *)NULL);
	fprintf(stderr, "%s", keyprompt); fflush(stderr);
	for (i = 0, ilim = COUNTOF(pbuf) - 1;
	     i < ilim && (c = getc(fi)) != '\n' && c != EOF;
	     )
		pbuf[i++] = (char)c;
	pbuf[i] = '\0';
	if (fi != stdin)
		fclose(fi);

	return (u_long) atoi(pbuf);
}


/*
 * atoascii - printable-ize possibly ascii data using the character
 *	      transformations cat -v uses.
 */
static void
atoascii(
	const char *in,
	size_t in_octets,
	char *out,
	size_t out_octets
	)
{
	const u_char *	pchIn;
	const u_char *	pchInLimit;
	u_char *	pchOut;
	u_char		c;

	pchIn = (const u_char *)in;
	pchInLimit = pchIn + in_octets;
	pchOut = (u_char *)out;

	if (NULL == pchIn) {
		if (0 < out_octets)
			*pchOut = '\0';
		return;
	}

#define	ONEOUT(c)					\
do {							\
	if (0 == --out_octets) {			\
		*pchOut = '\0';				\
		return;					\
	}						\
	*pchOut++ = (c);				\
} while (0)

	for (	; pchIn < pchInLimit; pchIn++) {
		c = *pchIn;
		if ('\0' == c)
			break;
		if (c & 0x80) {
			ONEOUT('M');
			ONEOUT('-');
			c &= 0x7f;
		}
		if (c < ' ') {
			ONEOUT('^');
			ONEOUT((u_char)(c + '@'));
		} else if (0x7f == c) {
			ONEOUT('^');
			ONEOUT('?');
		} else
			ONEOUT(c);
	}
	ONEOUT('\0');

#undef ONEOUT
}


/*
 * makeascii - print possibly ascii data using the character
 *	       transformations that cat -v uses.
 */
void
makeascii(
	size_t length,
	const char *data,
	FILE *fp
	)
{
	const u_char *data_u_char;
	const u_char *cp;
	int c;

	data_u_char = (const u_char *)data;

	for (cp = data_u_char; cp < data_u_char + length; cp++) {
		c = (int)*cp;
		if (c & 0x80) {
			putc('M', fp);
			putc('-', fp);
			c &= 0x7f;
		}

		if (c < ' ') {
			putc('^', fp);
			putc(c + '@', fp);
		} else if (0x7f == c) {
			putc('^', fp);
			putc('?', fp);
		} else
			putc(c, fp);
	}
}


/*
 * asciize - same thing as makeascii except add a newline
 */
void
asciize(
	int length,
	char *data,
	FILE *fp
	)
{
	makeascii(length, data, fp);
	putc('\n', fp);
}


/*
 * truncate string to fit clipping excess at end.
 *	"too long"	->	"too l"
 * Used for hostnames.
 */
const char *
trunc_right(
	const char *	src,
	size_t		width
	)
{
	size_t	sl;
	char *	out;


	sl = strlen(src);
	if (sl > width && LIB_BUFLENGTH - 1 > width && width > 0) {
		LIB_GETBUF(out);
		memcpy(out, src, width);
		out[width] = '\0';

		return out;
	}

	return src;
}


/*
 * truncate string to fit by preserving right side and using '_' to hint
 *	"too long"	->	"_long"
 * Used for local IPv6 addresses, where low bits differentiate.
 */
const char *
trunc_left(
	const char *	src,
	size_t		width
	)
{
	size_t	sl;
	char *	out;


	sl = strlen(src);
	if (sl > width && LIB_BUFLENGTH - 1 > width && width > 1) {
		LIB_GETBUF(out);
		out[0] = '_';
		memcpy(&out[1], &src[sl + 1 - width], width);

		return out;
	}

	return src;
}


/*
 * Some circular buffer space
 */
#define	CBLEN	80
#define	NUMCB	6

char circ_buf[NUMCB][CBLEN];
int nextcb = 0;

/* --------------------------------------------------------------------
 * Parsing a response value list
 *
 * This sounds simple (and it actually is not really hard) but it has
 * some pitfalls.
 *
 * Rule1: CR/LF is never embedded in an item
 * Rule2: An item is a name, optionally followed by a value
 * Rule3: The value is separated from the name by a '='
 * Rule4: Items are separated by a ','
 * Rule5: values can be quoted by '"', in which case they can contain
 *        arbitrary characters but *not* '"', CR and LF
 *
 * There are a few implementations out there that require a somewhat
 * relaxed attitude when parsing a value list, especially since we want
 * to copy names and values into local buffers. If these would overflow,
 * the item should be skipped without terminating the parsing sequence.
 *
 * Also, for empty values, there might be a '=' after the name or not;
 * we treat that equivalent.
 *
 * Parsing an item definitely breaks on a CR/LF. If an item is not
 * followed by a comma (','), parsing stops. In the middle of a quoted
 * character sequence CR/LF terminates the parsing finally without
 * returning a value.
 *
 * White space and other noise is ignored when parsing the data buffer;
 * only CR, LF, ',', '=' and '"' are characters with a special meaning.
 * White space is stripped from the names and values *after* working
 * through the buffer, before making the local copies. If whitespace
 * stripping results in an empty name, parsing resumes.
 */

/*
 * nextvar parsing helpers
 */

/* predicate: allowed chars inside a quoted string */
static int/*BOOL*/ cp_qschar(int ch)
{
	return ch && (ch != '"' && ch != '\r' && ch != '\n');
}

/* predicate: allowed chars inside an unquoted string */
static int/*BOOL*/ cp_uqchar(int ch)
{
	return ch && (ch != ',' && ch != '"' && ch != '\r' && ch != '\n');
}

/* predicate: allowed chars inside a value name */
static int/*BOOL*/ cp_namechar(int ch)
{
	return ch && (ch != ',' && ch != '=' && ch != '\r' && ch != '\n'); 
}

/* predicate: characters *between* list items. We're relaxed here. */
static int/*BOOL*/ cp_ivspace(int ch)
{
	return (ch == ',' || (ch > 0 && ch <= ' ')); 
}

/* get current character (or NUL when on end) */
static inline int
pf_getch(
	const char **	datap,
	const char *	endp
	)
{
	return (*datap != endp)
	    ? *(const unsigned char*)*datap
	    : '\0';
}

/* get next character (or NUL when on end) */
static inline int
pf_nextch(
	const char **	datap,
	const char *	endp
	)
{
	return (*datap != endp && ++(*datap) != endp)
	    ? *(const unsigned char*)*datap
	    : '\0';
}

static size_t
str_strip(
	const char ** 	datap,
	size_t		len
	)
{
	static const char empty[] = "";
	
	if (*datap && len) {
		const char * cpl = *datap;
		const char * cpr = cpl + len;
		
		while (cpl != cpr && *(const unsigned char*)cpl <= ' ')
			++cpl;
		while (cpl != cpr && *(const unsigned char*)(cpr - 1) <= ' ')
			--cpr;
		*datap = cpl;		
		len = (size_t)(cpr - cpl);
	} else {
		*datap = empty;
		len = 0;
	}
	return len;
}

static void
pf_error(
	const char *	what,
	const char *	where,
	const char *	whend
	)
{
#   ifndef BUILD_AS_LIB
	
	FILE *	ofp = (debug > 0) ? stdout : stderr;
	size_t	len = (size_t)(whend - where);
	
	if (len > 50) /* *must* fit into an 'int'! */
		len = 50;
	fprintf(ofp, "nextvar: %s: '%.*s'\n",
		what, (int)len, where);
	
#   else  /*defined(BUILD_AS_LIB)*/

	UNUSED_ARG(what);
	UNUSED_ARG(where);
	UNUSED_ARG(whend);
	
#   endif /*defined(BUILD_AS_LIB)*/
}

/*
 * nextvar - find the next variable in the buffer
 */
int/*BOOL*/
nextvar(
	size_t *datalen,
	const char **datap,
	char **vname,
	char **vvalue
	)
{
	enum PState 	{ sDone, sInit, sName, sValU, sValQ };
	
	static char	name[MAXVARLEN], value[MAXVALLEN];

	const char	*cp, *cpend;
	const char	*np, *vp;
	size_t		nlen, vlen;
	int		ch;
	enum PState	st;
	
	cpend = *datap + *datalen;

  again:
	np   = vp   = NULL;
	nlen = vlen = 0;
	
	st = sInit;
	ch = pf_getch(datap, cpend);

	while (st != sDone) {
		switch (st)
		{
		case sInit:	/* handle inter-item chars */
			while (cp_ivspace(ch))
				ch = pf_nextch(datap, cpend);
			if (cp_namechar(ch)) {
				np = *datap;
				cp = np;
				st = sName;
				ch = pf_nextch(datap, cpend);
			} else {
				goto final_done;
			}
			break;
			    
		case sName:	/* collect name */
			while (cp_namechar(ch))
				ch = pf_nextch(datap, cpend);
			nlen = (size_t)(*datap - np);
			if (ch == '=') {
				ch = pf_nextch(datap, cpend);
				vp = *datap;
				st = sValU;
			} else {
				if (ch != ',')
					*datap = cpend;
				st = sDone;
			}
			break;
			
		case sValU:	/* collect unquoted part(s) of value */
			while (cp_uqchar(ch))
				ch = pf_nextch(datap, cpend);
			if (ch == '"') {
				ch = pf_nextch(datap, cpend);
				st = sValQ;
			} else {
				vlen = (size_t)(*datap - vp);
				if (ch != ',')
					*datap = cpend;
				st = sDone;
			}
			break;
			
		case sValQ:	/* collect quoted part(s) of value */
			while (cp_qschar(ch))
				ch = pf_nextch(datap, cpend);
			if (ch == '"') {
				ch = pf_nextch(datap, cpend);
				st = sValU;
			} else {
				pf_error("no closing quote, stop", cp, cpend);
				goto final_done;
			}
			break;
			
		default:
			pf_error("state machine error, stop", *datap, cpend);
			goto final_done;
		}
	}

	/* If name or value do not fit their buffer, croak and start
	 * over. If there's no name at all after whitespace stripping,
	 * redo silently.
	 */
	nlen = str_strip(&np, nlen);
	vlen = str_strip(&vp, vlen);
	
	if (nlen == 0) {
		goto again;
	}
	if (nlen >= sizeof(name)) {
		pf_error("runaway name", np, cpend);
		goto again;
	}
	if (vlen >= sizeof(value)) {
		pf_error("runaway value", vp, cpend);
		goto again;
	}

	/* copy name and value into NUL-terminated buffers */
	memcpy(name, np, nlen);
	name[nlen] = '\0';
	*vname = name;
	
	memcpy(value, vp, vlen);
	value[vlen] = '\0';
	*vvalue = value;

	/* check if there's more to do or if we are finshed */
	*datalen = (size_t)(cpend - *datap);
	return TRUE;

  final_done:
	*datap = cpend;
	*datalen = 0;
	return FALSE;
}


u_short
varfmt(const char * varname)
{
	u_int n;

	for (n = 0; n < COUNTOF(cookedvars); n++)
		if (!strcmp(varname, cookedvars[n].varname))
			return cookedvars[n].fmt;

	return PADDING;
}


/*
 * printvars - print variables returned in response packet
 */
void
printvars(
	size_t length,
	const char *data,
	int status,
	int sttype,
	int quiet,
	FILE *fp
	)
{
	if (rawmode)
	    rawprint(sttype, length, data, status, quiet, fp);
	else
	    cookedprint(sttype, length, data, status, quiet, fp);
}


/*
 * rawprint - do a printout of the data in raw mode
 */
static void
rawprint(
	int datatype,
	size_t length,
	const char *data,
	int status,
	int quiet,
	FILE *fp
	)
{
	const char *cp;
	const char *cpend;

	/*
	 * Essentially print the data as is.  We reformat unprintables, though.
	 */
	cp = data;
	cpend = data + length;

	if (!quiet)
		(void) fprintf(fp, "status=0x%04x,\n", status);

	while (cp < cpend) {
		if (*cp == '\r') {
			/*
			 * If this is a \r and the next character is a
			 * \n, supress this, else pretty print it.  Otherwise
			 * just output the character.
			 */
			if (cp == (cpend - 1) || *(cp + 1) != '\n')
			    makeascii(1, cp, fp);
		} else if (isspace(pgetc(cp)) || isprint(pgetc(cp)))
			putc(*cp, fp);
		else
			makeascii(1, cp, fp);
		cp++;
	}
}


/*
 * Global data used by the cooked output routines
 */
int out_chars;		/* number of characters output */
int out_linecount;	/* number of characters output on this line */


/*
 * startoutput - get ready to do cooked output
 */
static void
startoutput(void)
{
	out_chars = 0;
	out_linecount = 0;
}


/*
 * output - output a variable=value combination
 */
static void
output(
	FILE *fp,
	const char *name,
	const char *value
	)
{
	int len;

	/* strlen of "name=value" */
	len = size2int_sat(strlen(name) + 1 + strlen(value));

	if (out_chars != 0) {
		out_chars += 2;
		if ((out_linecount + len + 2) > MAXOUTLINE) {
			fputs(",\n", fp);
			out_linecount = 0;
		} else {
			fputs(", ", fp);
			out_linecount += 2;
		}
	}

	fputs(name, fp);
	putc('=', fp);
	fputs(value, fp);
	out_chars += len;
	out_linecount += len;
}


/*
 * endoutput - terminate a block of cooked output
 */
static void
endoutput(
	FILE *fp
	)
{
	if (out_chars != 0)
		putc('\n', fp);
}


/*
 * outputarr - output an array of values
 */
static void
outputarr(
	FILE *fp,
	char *name,
	int narr,
	l_fp *lfp
	)
{
	char *bp;
	char *cp;
	size_t i;
	size_t len;
	char buf[256];

	bp = buf;
	/*
	 * Hack to align delay and offset values
	 */
	for (i = (int)strlen(name); i < 11; i++)
		*bp++ = ' ';

	for (i = narr; i > 0; i--) {
		if (i != (size_t)narr)
			*bp++ = ' ';
		cp = lfptoms(lfp, 2);
		len = strlen(cp);
		if (len > 7) {
			cp[7] = '\0';
			len = 7;
		}
		while (len < 7) {
			*bp++ = ' ';
			len++;
		}
		while (*cp != '\0')
		    *bp++ = *cp++;
		lfp++;
	}
	*bp = '\0';
	output(fp, name, buf);
}

static char *
tstflags(
	u_long val
	)
{
#	if CBLEN < 10
#	 error BLEN is too small -- increase!
#	endif

	char *cp, *s;
	size_t cb, i;
	int l;

	s = cp = circ_buf[nextcb];
	if (++nextcb >= NUMCB)
		nextcb = 0;
	cb = sizeof(circ_buf[0]);

	l = snprintf(cp, cb, "%02lx", val);
	if (l < 0 || (size_t)l >= cb)
		goto fail;
	cp += l;
	cb -= l;
	if (!val) {
		l = strlcat(cp, " ok", cb);
		if ((size_t)l >= cb)
			goto fail;
		cp += l;
		cb -= l;
	} else {
		const char *sep;
		
		sep = " ";
		for (i = 0; i < COUNTOF(tstflagnames); i++) {
			if (val & 0x1) {
				l = snprintf(cp, cb, "%s%s", sep,
					     tstflagnames[i]);
				if (l < 0)
					goto fail;
				if ((size_t)l >= cb) {
					cp += cb - 4;
					cb = 4;
					l = strlcpy (cp, "...", cb);
					cp += l;
					cb -= l;
					break;
				}
				sep = ", ";
				cp += l;
				cb -= l;
			}
			val >>= 1;
		}
	}

	return s;

  fail:
	*cp = '\0';
	return s;
}

/*
 * cookedprint - output variables in cooked mode
 */
static void
cookedprint(
	int datatype,
	size_t length,
	const char *data,
	int status,
	int quiet,
	FILE *fp
	)
{
	char *name;
	char *value;
	char output_raw;
	int fmt;
	l_fp lfp;
	sockaddr_u hval;
	u_long uval;
	int narr;
	size_t len;
	l_fp lfparr[8];
	char b[12];
	char bn[2 * MAXVARLEN];
	char bv[2 * MAXVALLEN];

	UNUSED_ARG(datatype);

	if (!quiet)
		fprintf(fp, "status=%04x %s,\n", status,
			statustoa(datatype, status));

	startoutput();
	while (nextvar(&length, &data, &name, &value)) {
		fmt = varfmt(name);
		output_raw = 0;
		switch (fmt) {

		case PADDING:
			output_raw = '*';
			break;

		case TS:
			if (!value || !decodets(value, &lfp))
				output_raw = '?';
			else
				output(fp, name, prettydate(&lfp));
			break;

		case HA:	/* fallthru */
		case NA:
			if (!value || !decodenetnum(value, &hval)) {
				output_raw = '?';
			} else if (fmt == HA){
				output(fp, name, nntohost(&hval));
			} else {
				output(fp, name, stoa(&hval));
			}
			break;

		case RF:
			if (!value) {
				output_raw = '?';
			} else if (decodenetnum(value, &hval)) {
				if (ISREFCLOCKADR(&hval))
					output(fp, name,
					       refnumtoa(&hval));
				else
					output(fp, name, stoa(&hval));
			} else if (strlen(value) <= 4) {
				output(fp, name, value);
			} else {
				output_raw = '?';
			}
			break;

		case LP:
			if (!value || !decodeuint(value, &uval) || uval > 3) {
				output_raw = '?';
			} else {
				b[0] = (0x2 & uval)
					   ? '1'
					   : '0';
				b[1] = (0x1 & uval)
					   ? '1'
					   : '0';
				b[2] = '\0';
				output(fp, name, b);
			}
			break;

		case OC:
			if (!value || !decodeuint(value, &uval)) {
				output_raw = '?';
			} else {
				snprintf(b, sizeof(b), "%03lo", uval);
				output(fp, name, b);
			}
			break;

		case AR:
			if (!value || !decodearr(value, &narr, lfparr, 8))
				output_raw = '?';
			else
				outputarr(fp, name, narr, lfparr);
			break;

		case FX:
			if (!value || !decodeuint(value, &uval))
				output_raw = '?';
			else
				output(fp, name, tstflags(uval));
			break;

		default:
			fprintf(stderr, "Internal error in cookedprint, %s=%s, fmt %d\n",
				name, value, fmt);
			output_raw = '?';
			break;
		}

		if (output_raw != 0) {
			/* TALOS-CAN-0063: avoid buffer overrun */
			atoascii(name, MAXVARLEN, bn, sizeof(bn));
			if (output_raw != '*') {
				atoascii(value, MAXVALLEN,
					 bv, sizeof(bv) - 1);
				len = strlen(bv);
				bv[len] = output_raw;
				bv[len+1] = '\0';
			} else {
				atoascii(value, MAXVALLEN,
					 bv, sizeof(bv));
			}
			output(fp, bn, bv);
		}
	}
	endoutput(fp);
}


/*
 * sortassoc - sort associations in the cache into ascending order
 */
void
sortassoc(void)
{
	if (numassoc > 1)
		qsort(assoc_cache, (size_t)numassoc,
		      sizeof(assoc_cache[0]), &assoccmp);
}


/*
 * assoccmp - compare two associations
 */
static int
assoccmp(
	const void *t1,
	const void *t2
	)
{
	const struct association *ass1 = t1;
	const struct association *ass2 = t2;

	if (ass1->assid < ass2->assid)
		return -1;
	if (ass1->assid > ass2->assid)
		return 1;
	return 0;
}


/*
 * grow_assoc_cache() - enlarge dynamic assoc_cache array
 *
 * The strategy is to add an assumed 4k page size at a time, leaving
 * room for malloc() bookkeeping overhead equivalent to 4 pointers.
 */
void
grow_assoc_cache(void)
{
	static size_t	prior_sz;
	size_t		new_sz;

	new_sz = prior_sz + 4 * 1024;
	if (0 == prior_sz) {
		new_sz -= 4 * sizeof(void *);
	}
	assoc_cache = erealloc_zero(assoc_cache, new_sz, prior_sz); 
	prior_sz = new_sz;
	assoc_cache_slots = (u_int)(new_sz / sizeof(assoc_cache[0]));
}


/*
 * ntpq_custom_opt_handler - autoopts handler for -c and -p
 *
 * By default, autoopts loses the relative order of -c and -p options
 * on the command line.  This routine replaces the default handler for
 * those routines and builds a list of commands to execute preserving
 * the order.
 */
void
ntpq_custom_opt_handler(
	tOptions *pOptions,
	tOptDesc *pOptDesc
	)
{
	switch (pOptDesc->optValue) {

	default:
		fprintf(stderr,
			"ntpq_custom_opt_handler unexpected option '%c' (%d)\n",
			pOptDesc->optValue, pOptDesc->optValue);
		exit(1);

	case 'c':
		ADDCMD(pOptDesc->pzLastArg);
		break;

	case 'p':
		ADDCMD("peers");
		break;
	}
}
/*
 * Obtain list of digest names
 */

#if defined(OPENSSL) && !defined(HAVE_EVP_MD_DO_ALL_SORTED)
# if defined(_MSC_VER) && OPENSSL_VERSION_NUMBER >= 0x10100000L
#  define HAVE_EVP_MD_DO_ALL_SORTED
# endif
#endif

#ifdef OPENSSL
# ifdef HAVE_EVP_MD_DO_ALL_SORTED
#  define K_PER_LINE	8
#  define K_NL_PFX_STR	"\n    "
#  define K_DELIM_STR	", "

struct hstate {
	char *list;
	const char **seen;
	int idx;
};


#  ifndef BUILD_AS_LIB
static void
list_md_fn(const EVP_MD *m, const char *from, const char *to, void *arg)
{
	size_t 	       len, n;
	const char    *name, **seen;
	struct hstate *hstate = arg;
	const char    *cp;
	
	/* m is MD obj, from is name or alias, to is base name for alias */
	if (!m || !from || to)
		return; /* Ignore aliases */

	/* Discard MACs that NTP won't accept. */
	/* Keep this consistent with keytype_from_text() in ssl_init.c. */
	if (EVP_MD_size(m) > (MAX_MAC_LEN - sizeof(keyid_t)))
		return;
	
	name = EVP_MD_name(m);
	
	/* Lowercase names aren't accepted by keytype_from_text in ssl_init.c */
	
	for (cp = name; *cp; cp++)
		if (islower((unsigned char)*cp))
			return;

	len = (cp - name) + 1;
	
	/* There are duplicates.  Discard if name has been seen. */
	
	for (seen = hstate->seen; *seen; seen++)
		if (!strcmp(*seen, name))
			return;

	n = (seen - hstate->seen) + 2;
	hstate->seen = erealloc(hstate->seen, n * sizeof(*seen));
	hstate->seen[n-2] = name;
	hstate->seen[n-1] = NULL;
	
	if (hstate->list != NULL)
		len += strlen(hstate->list);

	len += (hstate->idx >= K_PER_LINE)
	    ? strlen(K_NL_PFX_STR)
	    : strlen(K_DELIM_STR);

	if (hstate->list == NULL) {
		hstate->list = (char *)emalloc(len);
		hstate->list[0] = '\0';
	} else {
		hstate->list = (char *)erealloc(hstate->list, len);
	}
	
	sprintf(hstate->list + strlen(hstate->list), "%s%s",
		((hstate->idx >= K_PER_LINE) ? K_NL_PFX_STR : K_DELIM_STR),
		name);
	
	if (hstate->idx >= K_PER_LINE)
		hstate->idx = 1;
	else
		hstate->idx++;
}
#  endif /* !defined(BUILD_AS_LIB) */

#  ifndef BUILD_AS_LIB
/* Insert CMAC into SSL digests list */
static char *
insert_cmac(char *list)
{
#ifdef ENABLE_CMAC
	int insert;
	size_t len;


	/* If list empty, we need to insert CMAC on new line */
	insert = (!list || !*list);
	
	if (insert) {
		len = strlen(K_NL_PFX_STR) + strlen(CMAC);
		list = (char *)erealloc(list, len + 1);
		sprintf(list, "%s%s", K_NL_PFX_STR, CMAC);
	} else {	/* List not empty */
		/* Check if CMAC already in list - future proofing */
		const char *cmac_sn;
		char *cmac_p;
		
		cmac_sn = OBJ_nid2sn(NID_cmac);
		cmac_p = list;
		insert = cmac_sn != NULL && *cmac_sn != '\0';
		
		/* CMAC in list if found, followed by nul char or ',' */
		while (insert && NULL != (cmac_p = strstr(cmac_p, cmac_sn))) {
			cmac_p += strlen(cmac_sn);
			/* Still need to insert if not nul and not ',' */
			insert = *cmac_p && ',' != *cmac_p;
		}
		
		/* Find proper insertion point */
		if (insert) {
			char *last_nl;
			char *point;
			char *delim;
			int found;
			
			/* Default to start if list empty */
			found = 0;
			delim = list;
			len = strlen(list);
			
			/* While new lines */
			while (delim < list + len && *delim &&
			       !strncmp(K_NL_PFX_STR, delim, strlen(K_NL_PFX_STR))) {
				point = delim + strlen(K_NL_PFX_STR);
				
				/* While digest names on line */
				while (point < list + len && *point) {
					/* Another digest after on same or next line? */
					delim = strstr( point, K_DELIM_STR);
					last_nl = strstr( point, K_NL_PFX_STR);
					
					/* No - end of list */
					if (!delim && !last_nl) {
						delim = list + len;
					} else
						/* New line and no delim or before delim? */
						if (last_nl && (!delim || last_nl < delim)) {
							delim = last_nl;
						}
					
					/* Found insertion point where CMAC before entry? */
					if (strncmp(CMAC, point, delim - point) < 0) {
						found = 1;
						break;
					}
					
					if (delim < list + len && *delim &&
					    !strncmp(K_DELIM_STR, delim, strlen(K_DELIM_STR))) {
						point += strlen(K_DELIM_STR);
					} else {
						break;
					}
				} /* While digest names on line */
			} /* While new lines */
			
			/* If found in list */
			if (found) {
				/* insert cmac and delim */
				/* Space for list could move - save offset */
				ptrdiff_t p_offset = point - list;
				len += strlen(CMAC) + strlen(K_DELIM_STR);
				list = (char *)erealloc(list, len + 1);
				point = list + p_offset;
				/* move to handle src/dest overlap */
				memmove(point + strlen(CMAC) + strlen(K_DELIM_STR),
					point, strlen(point) + 1);
				strncpy(point, CMAC, strlen(CMAC));
				strncpy(point + strlen(CMAC), K_DELIM_STR, strlen(K_DELIM_STR));
			} else {	/* End of list */
				/* append delim and cmac */
				len += strlen(K_DELIM_STR) + strlen(CMAC);
				list = (char *)erealloc(list, len + 1);
				strcpy(list + strlen(list), K_DELIM_STR);
				strcpy(list + strlen(list), CMAC);
			}
		} /* insert */
	} /* List not empty */
#endif /*ENABLE_CMAC*/
	return list;
}
#  endif /* !defined(BUILD_AS_LIB) */
# endif
#endif


#ifndef BUILD_AS_LIB
static char *
list_digest_names(void)
{
	char *list = NULL;
	
#ifdef OPENSSL
# ifdef HAVE_EVP_MD_DO_ALL_SORTED
	struct hstate hstate = { NULL, NULL, K_PER_LINE+1 };
	
	/* replace calloc(1, sizeof(const char *)) */
	hstate.seen = (const char **)emalloc_zero(sizeof(const char *));
	
	INIT_SSL();
	EVP_MD_do_all_sorted(list_md_fn, &hstate);
	list = hstate.list;
	free(hstate.seen);
	
	list = insert_cmac(list);	/* Insert CMAC into SSL digests list */
	
# else
	list = (char *)emalloc(sizeof("md5, others (upgrade to OpenSSL-1.0 for full list)"));
	strcpy(list, "md5, others (upgrade to OpenSSL-1.0 for full list)");
# endif
#else
	list = (char *)emalloc(sizeof("md5"));
	strcpy(list, "md5");
#endif
	
	return list;
}
#endif /* !defined(BUILD_AS_LIB) */

#define CTRLC_STACK_MAX 4
static volatile size_t		ctrlc_stack_len = 0;
static volatile Ctrl_C_Handler	ctrlc_stack[CTRLC_STACK_MAX];



int/*BOOL*/
push_ctrl_c_handler(
	Ctrl_C_Handler func
	)
{
	size_t size = ctrlc_stack_len;
	if (func && (size < CTRLC_STACK_MAX)) {
		ctrlc_stack[size] = func;
		ctrlc_stack_len = size + 1;
		return TRUE;
	}
	return FALSE;	
}

int/*BOOL*/
pop_ctrl_c_handler(
	Ctrl_C_Handler func
	)
{
	size_t size = ctrlc_stack_len;
	if (size) {
		--size;
		if (func == NULL || func == ctrlc_stack[size]) {
			ctrlc_stack_len = size;
			return TRUE;
		}
	}
	return FALSE;
}

#ifndef BUILD_AS_LIB
static void
on_ctrlc(void)
{
	size_t size = ctrlc_stack_len;
	while (size)
		if ((*ctrlc_stack[--size])())
			break;
}
#endif /* !defined(BUILD_AS_LIB) */

#ifndef BUILD_AS_LIB
static int
my_easprintf(
	char ** 	ppinto,
	const char *	fmt   ,
	...
	)
{
	va_list	va;
	int	prc;
	size_t	len = 128;
	char *	buf = emalloc(len);

  again:
	/* Note: we expect the memory allocation to fail long before the
	 * increment in buffer size actually overflows.
	 */
	buf = (buf) ? erealloc(buf, len) : emalloc(len);

	va_start(va, fmt);
	prc = vsnprintf(buf, len, fmt, va);
	va_end(va);

	if (prc < 0) {
		/* might be very old vsnprintf. Or actually MSVC... */
		len += len >> 1;
		goto again;
	}
	if ((size_t)prc >= len) {
		/* at least we have the proper size now... */
		len = (size_t)prc + 1;
		goto again;
	}
	if ((size_t)prc < (len - 32))
		buf = erealloc(buf, (size_t)prc + 1);
	*ppinto = buf;
	return prc;
}
#endif /* !defined(BUILD_AS_LIB) */
