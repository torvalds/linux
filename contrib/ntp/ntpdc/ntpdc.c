/*
 * ntpdc - control and monitor your ntpd daemon
 */
#include <config.h>
#include <stdio.h>
#include <stddef.h>
#include <ctype.h>
#include <signal.h>
#include <setjmp.h>
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

#include "ntpdc.h"
#include "ntp_select.h"
#include "ntp_stdlib.h"
#include "ntp_assert.h"
#include "ntp_lineedit.h"
#ifdef OPENSSL
#include "openssl/evp.h"
#include "openssl/objects.h"
#endif
#include <ssl_applink.c>

#include "ntp_libopts.h"
#include "ntpdc-opts.h"
#include "safecast.h"

#ifdef SYS_VXWORKS
				/* vxWorks needs mode flag -casey*/
# define open(name, flags)   open(name, flags, 0777)
# define SERVER_PORT_NUM     123
#endif

/* We use COMMAND as an autogen keyword */
#ifdef COMMAND
# undef COMMAND
#endif

/*
 * Because we now potentially understand a lot of commands (and
 * it requires a lot of commands to talk to ntpd) we will run
 * interactive if connected to a terminal.
 */
static	int	interactive = 0;	/* set to 1 when we should prompt */
static	const char *	prompt = "ntpdc> ";	/* prompt to ask him about */

/*
 * Keyid used for authenticated requests.  Obtained on the fly.
 */
static	u_long	info_auth_keyid;
static int keyid_entered = 0;

static	int	info_auth_keytype = NID_md5;	/* MD5 */
static	size_t	info_auth_hashlen = 16;		/* MD5 */
u_long	current_time;		/* needed by authkeys; not used */

/*
 * for get_systime()
 */
s_char	sys_precision;		/* local clock precision (log2 s) */

int		ntpdcmain	(int,	char **);
/*
 * Built in command handler declarations
 */
static	int	openhost	(const char *);
static	int	sendpkt		(void *, size_t);
static	void	growpktdata	(void);
static	int	getresponse	(int, int, size_t *, size_t *, const char **, size_t);
static	int	sendrequest	(int, int, int, size_t, size_t, const char *);
static	void	getcmds		(void);
static	RETSIGTYPE abortcmd	(int);
static	void	docmd		(const char *);
static	void	tokenize	(const char *, char **, int *);
static	int	findcmd		(char *, struct xcmd *, struct xcmd *, struct xcmd **);
static	int	getarg		(char *, int, arg_v *);
static	int	getnetnum	(const char *, sockaddr_u *, char *, int);
static	void	help		(struct parse *, FILE *);
static	int	helpsort	(const void *, const void *);
static	void	printusage	(struct xcmd *, FILE *);
static	void	timeout		(struct parse *, FILE *);
static	void	my_delay	(struct parse *, FILE *);
static	void	host		(struct parse *, FILE *);
static	void	keyid		(struct parse *, FILE *);
static	void	keytype		(struct parse *, FILE *);
static	void	passwd		(struct parse *, FILE *);
static	void	hostnames	(struct parse *, FILE *);
static	void	setdebug	(struct parse *, FILE *);
static	void	quit		(struct parse *, FILE *);
static	void	version		(struct parse *, FILE *);
static	void	warning		(const char *, ...)
    __attribute__((__format__(__printf__, 1, 2)));
static	void	error		(const char *, ...)
    __attribute__((__format__(__printf__, 1, 2)));
static	u_long	getkeyid	(const char *);



/*
 * Built-in commands we understand
 */
static	struct xcmd builtins[] = {
	{ "?",		help,		{  OPT|NTP_STR, NO, NO, NO },
	  { "command", "", "", "" },
	  "tell the use and syntax of commands" },
	{ "help",	help,		{  OPT|NTP_STR, NO, NO, NO },
	  { "command", "", "", "" },
	  "tell the use and syntax of commands" },
	{ "timeout",	timeout,	{ OPT|NTP_UINT, NO, NO, NO },
	  { "msec", "", "", "" },
	  "set the primary receive time out" },
	{ "delay",	my_delay,	{ OPT|NTP_INT, NO, NO, NO },
	  { "msec", "", "", "" },
	  "set the delay added to encryption time stamps" },
	{ "host",	host,		{ OPT|NTP_STR, OPT|NTP_STR, NO, NO },
	  { "-4|-6", "hostname", "", "" },
	  "specify the host whose NTP server we talk to" },
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
	  "exit ntpdc" },
	{ "exit",	quit,		{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "exit ntpdc" },
	{ "keyid",	keyid,		{ OPT|NTP_UINT, NO, NO, NO },
	  { "key#", "", "", "" },
	  "set/show keyid to use for authenticated requests" },
	{ "keytype",	keytype,	{ OPT|NTP_STR, NO, NO, NO },
	  { "(md5|des)", "", "", "" },
	  "set/show key authentication type for authenticated requests (des|md5)" },
	{ "version",	version,	{ NO, NO, NO, NO },
	  { "", "", "", "" },
	  "print version number" },
	{ 0,		0,		{ NO, NO, NO, NO },
	  { "", "", "", "" }, "" }
};


/*
 * Default values we use.
 */
#define	DEFHOST		"localhost"	/* default host name */
#define	DEFTIMEOUT	(5)		/* 5 second time out */
#define	DEFSTIMEOUT	(2)		/* 2 second time out after first */
#define	DEFDELAY	0x51EB852	/* 20 milliseconds, l_fp fraction */
#define	LENHOSTNAME	256		/* host name is 256 characters long */
#define	MAXCMDS		100		/* maximum commands on cmd line */
#define	MAXHOSTS	200		/* maximum hosts on cmd line */
#define	MAXLINE		512		/* maximum line length */
#define	MAXTOKENS	(1+1+MAXARGS+MOREARGS+2)	/* maximum number of usable tokens */
#define	SCREENWIDTH  	78		/* nominal screen width in columns */

/*
 * Some variables used and manipulated locally
 */
static	struct sock_timeval tvout = { DEFTIMEOUT, 0 };	/* time out for reads */
static	struct sock_timeval tvsout = { DEFSTIMEOUT, 0 };/* secondary time out */
static	l_fp delay_time;				/* delay time */
static	char currenthost[LENHOSTNAME];			/* current host name */
int showhostnames = 1;					/* show host names by default */

static	int ai_fam_templ;				/* address family */
static	int ai_fam_default;				/* default address family */
static	SOCKET sockfd;					/* fd socket is opened on */
static	int havehost = 0;				/* set to 1 when host open */
int s_port = 0;

/*
 * Holds data returned from queries.  We allocate INITDATASIZE
 * octets to begin with, increasing this as we need to.
 */
#define	INITDATASIZE	(sizeof(struct resp_pkt) * 16)
#define	INCDATASIZE	(sizeof(struct resp_pkt) * 8)

static	char *pktdata;
static	int pktdatasize;

/*
 * These are used to help the magic with old and new versions of ntpd.
 */
int impl_ver = IMPL_XNTPD;
static int req_pkt_size = REQ_LEN_NOMAC;

/*
 * For commands typed on the command line (with the -c option)
 */
static	int numcmds = 0;
static	const char *ccmds[MAXCMDS];
#define	ADDCMD(cp)	if (numcmds < MAXCMDS) ccmds[numcmds++] = (cp)

/*
 * When multiple hosts are specified.
 */
static	int numhosts = 0;
static	const char *chosts[MAXHOSTS];
#define	ADDHOST(cp)	if (numhosts < MAXHOSTS) chosts[numhosts++] = (cp)

/*
 * Error codes for internal use
 */
#define	ERR_INCOMPLETE		16
#define	ERR_TIMEOUT		17

/*
 * Macro definitions we use
 */
#define	ISSPACE(c)	((c) == ' ' || (c) == '\t')
#define	ISEOL(c)	((c) == '\n' || (c) == '\r' || (c) == '\0')
#define	STREQ(a, b)	(*(a) == *(b) && strcmp((a), (b)) == 0)

/*
 * Jump buffer for longjumping back to the command level.
 *
 * See ntpq/ntpq.c for an explanation why 'sig{set,long}jmp()' is used
 * when available.
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
 * Pointer to current output unit
 */
static	FILE *current_output = NULL;

/*
 * Command table imported from ntpdc_ops.c
 */
extern struct xcmd opcmds[];

char const *progname;

#ifdef NO_MAIN_ALLOWED
CALL(ntpdc,"ntpdc",ntpdcmain);
#else
int
main(
	int argc,
	char *argv[]
	)
{
	return ntpdcmain(argc, argv);
}
#endif

#ifdef SYS_VXWORKS
void clear_globals(void)
{
    showhostnames = 0;              /* show host names by default */
    havehost = 0;                   /* set to 1 when host open */
    numcmds = 0;
    numhosts = 0;
}
#endif

/*
 * main - parse arguments and handle options
 */
int
ntpdcmain(
	int argc,
	char *argv[]
	)
{
	delay_time.l_ui = 0;
	delay_time.l_uf = DEFDELAY;

#ifdef SYS_VXWORKS
	clear_globals();
	taskPrioritySet(taskIdSelf(), 100 );
#endif

	init_lib();	/* sets up ipv4_works, ipv6_works */
	ssl_applink();
	init_auth();

	/* Check to see if we have IPv6. Otherwise default to IPv4 */
	if (!ipv6_works)
		ai_fam_default = AF_INET;

	progname = argv[0];

	{
		int optct = ntpOptionProcess(&ntpdcOptions, argc, argv);
		argc -= optct;
		argv += optct;
	}

	if (HAVE_OPT(IPV4))
		ai_fam_templ = AF_INET;
	else if (HAVE_OPT(IPV6))
		ai_fam_templ = AF_INET6;
	else
		ai_fam_templ = ai_fam_default;

	if (HAVE_OPT(COMMAND)) {
		int		cmdct = STACKCT_OPT( COMMAND );
		const char**	cmds  = STACKLST_OPT( COMMAND );

		while (cmdct-- > 0) {
			ADDCMD(*cmds++);
		}
	}

	debug = OPT_VALUE_SET_DEBUG_LEVEL;

	if (HAVE_OPT(INTERACTIVE)) {
		interactive = 1;
	}

	if (HAVE_OPT(NUMERIC)) {
		showhostnames = 0;
	}

	if (HAVE_OPT(LISTPEERS)) {
		ADDCMD("listpeers");
	}

	if (HAVE_OPT(PEERS)) {
		ADDCMD("peers");
	}

	if (HAVE_OPT(SHOWPEERS)) {
		ADDCMD("dmpeers");
	}

	if (ntp_optind == argc) {
		ADDHOST(DEFHOST);
	} else {
		for (; ntp_optind < argc; ntp_optind++)
		    ADDHOST(argv[ntp_optind]);
	}

	if (numcmds == 0 && interactive == 0
	    && isatty(fileno(stdin)) && isatty(fileno(stderr))) {
		interactive = 1;
	}

#ifndef SYS_WINNT /* Under NT cannot handle SIGINT, WIN32 spawns a handler */
	if (interactive)
		(void) signal_no_reset(SIGINT, abortcmd);
#endif /* SYS_WINNT */

	/*
	 * Initialize the packet data buffer
	 */
	pktdatasize = INITDATASIZE;
	pktdata = emalloc(INITDATASIZE);

	if (numcmds == 0) {
		(void) openhost(chosts[0]);
		getcmds();
	} else {
		int ihost;
		int icmd;

		for (ihost = 0; ihost < numhosts; ihost++) {
			if (openhost(chosts[ihost]))
			    for (icmd = 0; icmd < numcmds; icmd++) {
				    if (numhosts > 1) 
					printf ("--- %s ---\n",chosts[ihost]);
				    docmd(ccmds[icmd]);
			    }
		}
	}
#ifdef SYS_WINNT
	WSACleanup();
#endif
	return(0);
} /* main end */


/*
 * openhost - open a socket to a host
 */
static int
openhost(
	const char *hname
	)
{
	char temphost[LENHOSTNAME];
	int a_info;
	struct addrinfo hints, *ai = NULL;
	sockaddr_u addr;
	size_t octets;
	const char *cp;
	char name[LENHOSTNAME];
	char service[5];

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
	strlcpy(service, "ntp", sizeof(service));
	ZERO(hints);
	hints.ai_family = ai_fam_templ;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = Z_AI_NUMERICHOST;

	a_info = getaddrinfo(hname, service, &hints, &ai);
	if (a_info == EAI_NONAME
#ifdef EAI_NODATA
	    || a_info == EAI_NODATA
#endif
	   ) {
		hints.ai_flags = AI_CANONNAME;
#ifdef AI_ADDRCONFIG
		hints.ai_flags |= AI_ADDRCONFIG;
#endif
		a_info = getaddrinfo(hname, service, &hints, &ai);	
	}
	/* Some older implementations don't like AI_ADDRCONFIG. */
	if (a_info == EAI_BADFLAGS) {
		hints.ai_flags = AI_CANONNAME;
		a_info = getaddrinfo(hname, service, &hints, &ai);	
	}
	if (a_info != 0) {
		fprintf(stderr, "%s\n", gai_strerror(a_info));
		if (ai != NULL)
			freeaddrinfo(ai);
		return 0;
	}

	/* 
	 * getaddrinfo() has returned without error so ai should not 
	 * be NULL.
	 */
	INSIST(ai != NULL);
	ZERO(addr);
	octets = min(sizeof(addr), ai->ai_addrlen);
	memcpy(&addr, ai->ai_addr, octets);

	if (ai->ai_canonname == NULL)
		strlcpy(temphost, stoa(&addr), sizeof(temphost));
	else
		strlcpy(temphost, ai->ai_canonname, sizeof(temphost));

	if (debug > 2)
		printf("Opening host %s\n", temphost);

	if (havehost == 1) {
		if (debug > 2)
			printf("Closing old host %s\n", currenthost);
		closesocket(sockfd);
		havehost = 0;
	}
	strlcpy(currenthost, temphost, sizeof(currenthost));
	
	/* port maps to the same in both families */
	s_port = NSRCPORT(&addr);; 
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

		err = setsockopt(INVALID_SOCKET, SOL_SOCKET, SO_OPENTYPE, (void *)&optionValue, sizeof(optionValue));
		if (err != NO_ERROR) {
			(void) fprintf(stderr, "cannot open nonoverlapped sockets\n");
			exit(1);
		}
	}
#endif /* SYS_WINNT */

	sockfd = socket(ai->ai_family, SOCK_DGRAM, 0);
	if (sockfd == INVALID_SOCKET) {
		error("socket");
		exit(-1);
	}
	
#ifdef NEED_RCVBUF_SLOP
# ifdef SO_RCVBUF
	{
		int rbufsize = INITDATASIZE + 2048; /* 2K for slop */

		if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF,
			       (void *)&rbufsize, sizeof(int)) == -1)
		    error("setsockopt");
	}
# endif
#endif

#ifdef SYS_VXWORKS
	if (connect(sockfd, (struct sockaddr *)&hostaddr, 
		    sizeof(hostaddr)) == -1)
#else
	if (connect(sockfd, ai->ai_addr, ai->ai_addrlen) == -1)
#endif /* SYS_VXWORKS */
	{
		error("connect");
		exit(-1);
	}

	freeaddrinfo(ai);
	havehost = 1;
	req_pkt_size = REQ_LEN_NOMAC;
	impl_ver = IMPL_XNTPD;
	return 1;
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
	if (send(sockfd, xdata, xdatalen, 0) == -1) {
		warning("write to %s failed", currenthost);
		return -1;
	}

	return 0;
}


/*
 * growpktdata - grow the packet data area
 */
static void
growpktdata(void)
{
	size_t priorsz;

	priorsz = (size_t)pktdatasize;
	pktdatasize += INCDATASIZE;
	pktdata = erealloc_zero(pktdata, (size_t)pktdatasize, priorsz);
}


/*
 * getresponse - get a (series of) response packet(s) and return the data
 */
static int
getresponse(
	int implcode,
	int reqcode,
	size_t *ritems,
	size_t *rsize,
	const char **rdata,
	size_t esize
	)
{
	struct resp_pkt rpkt;
	struct sock_timeval tvo;
	size_t items;
	size_t i;
	size_t size;
	size_t datasize;
	char *datap;
	char *tmp_data;
	char haveseq[MAXSEQ+1];
	int firstpkt;
	int lastseq;
	int numrecv;
	int seq;
	fd_set fds;
	ssize_t n;
	int pad;
	/* absolute timeout checks. Not 'time_t' by intention! */
	uint32_t tobase;	/* base value for timeout */
	uint32_t tospan;	/* timeout span (max delay) */
	uint32_t todiff;	/* current delay */

	/*
	 * This is pretty tricky.  We may get between 1 and many packets
	 * back in response to the request.  We peel the data out of
	 * each packet and collect it in one long block.  When the last
	 * packet in the sequence is received we'll know how many we
	 * should have had.  Note we use one long time out, should reconsider.
	 */
	*ritems = 0;
	*rsize = 0;
	firstpkt = 1;
	numrecv = 0;
	*rdata = datap = pktdata;
	lastseq = 999;	/* too big to be a sequence number */
	ZERO(haveseq);
	FD_ZERO(&fds);
	tobase = (uint32_t)time(NULL);

    again:
	if (firstpkt)
		tvo = tvout;
	else
		tvo = tvsout;
	tospan = (uint32_t)tvo.tv_sec + (tvo.tv_usec != 0);
	
	FD_SET(sockfd, &fds);
	n = select(sockfd+1, &fds, NULL, NULL, &tvo);
	if (n == -1) {
		warning("select fails");
		return -1;
	}
	
	/*
	 * Check if this is already too late. Trash the data and fake a
	 * timeout if this is so.
	 */
	todiff = (((uint32_t)time(NULL)) - tobase) & 0x7FFFFFFFu;
	if ((n > 0) && (todiff > tospan)) {
		n = recv(sockfd, (char *)&rpkt, sizeof(rpkt), 0);
		n -= n; /* faked timeout return from 'select()'*/
	}
	
	if (n == 0) {
		/*
		 * Timed out.  Return what we have
		 */
		if (firstpkt) {
			(void) fprintf(stderr,
				       "%s: timed out, nothing received\n",
				       currenthost);
			return ERR_TIMEOUT;
		} else {
			(void) fprintf(stderr,
				       "%s: timed out with incomplete data\n",
				       currenthost);
			if (debug) {
				printf("Received sequence numbers");
				for (n = 0; n <= MAXSEQ; n++)
				    if (haveseq[n])
					printf(" %zd,", (size_t)n);
				if (lastseq != 999)
				    printf(" last frame received\n");
				else
				    printf(" last frame not received\n");
			}
			return ERR_INCOMPLETE;
		}
	}

	n = recv(sockfd, (char *)&rpkt, sizeof(rpkt), 0);
	if (n == -1) {
		warning("read");
		return -1;
	}


	/*
	 * Check for format errors.  Bug proofing.
	 */
	if (n < (ssize_t)RESP_HEADER_SIZE) {
		if (debug)
			printf("Short (%zd byte) packet received\n", (size_t)n);
		goto again;
	}
	if (INFO_VERSION(rpkt.rm_vn_mode) > NTP_VERSION ||
	    INFO_VERSION(rpkt.rm_vn_mode) < NTP_OLDVERSION) {
		if (debug)
			printf("Packet received with version %d\n",
			       INFO_VERSION(rpkt.rm_vn_mode));
		goto again;
	}
	if (INFO_MODE(rpkt.rm_vn_mode) != MODE_PRIVATE) {
		if (debug)
			printf("Packet received with mode %d\n",
			       INFO_MODE(rpkt.rm_vn_mode));
		goto again;
	}
	if (INFO_IS_AUTH(rpkt.auth_seq)) {
		if (debug)
			printf("Encrypted packet received\n");
		goto again;
	}
	if (!ISRESPONSE(rpkt.rm_vn_mode)) {
		if (debug)
			printf("Received request packet, wanted response\n");
		goto again;
	}
	if (INFO_MBZ(rpkt.mbz_itemsize) != 0) {
		if (debug)
			printf("Received packet with nonzero MBZ field!\n");
		goto again;
	}

	/*
	 * Check implementation/request.  Could be old data getting to us.
	 */
	if (rpkt.implementation != implcode || rpkt.request != reqcode) {
		if (debug)
			printf(
			    "Received implementation/request of %d/%d, wanted %d/%d",
			    rpkt.implementation, rpkt.request,
			    implcode, reqcode);
		goto again;
	}

	/*
	 * Check the error code.  If non-zero, return it.
	 */
	if (INFO_ERR(rpkt.err_nitems) != INFO_OKAY) {
		if (debug && ISMORE(rpkt.rm_vn_mode)) {
			printf("Error code %d received on not-final packet\n",
			       INFO_ERR(rpkt.err_nitems));
		}
		return (int)INFO_ERR(rpkt.err_nitems);
	}

	/*
	 * Collect items and size.  Make sure they make sense.
	 */
	items = INFO_NITEMS(rpkt.err_nitems);
	size = INFO_ITEMSIZE(rpkt.mbz_itemsize);
	if (esize > size)
		pad = esize - size;
	else 
		pad = 0;
	datasize = items * size;
	if ((size_t)datasize > (n-RESP_HEADER_SIZE)) {
		if (debug)
		    printf(
			    "Received items %zu, size %zu (total %zu), data in packet is %zu\n",
			    items, size, datasize, n-RESP_HEADER_SIZE);
		goto again;
	}

	/*
	 * If this isn't our first packet, make sure the size matches
	 * the other ones.
	 */
	if (!firstpkt && size != *rsize) {
		if (debug)
		    printf("Received itemsize %zu, previous %zu\n",
			   size, *rsize);
		goto again;
	}
	/*
	 * If we've received this before, +toss it
	 */
	seq = INFO_SEQ(rpkt.auth_seq);
	if (haveseq[seq]) {
		if (debug)
		    printf("Received duplicate sequence number %d\n", seq);
		goto again;
	}
	haveseq[seq] = 1;

	/*
	 * If this is the last in the sequence, record that.
	 */
	if (!ISMORE(rpkt.rm_vn_mode)) {
		if (lastseq != 999) {
			printf("Received second end sequence packet\n");
			goto again;
		}
		lastseq = seq;
	}

	/*
	 * So far, so good.  Copy this data into the output array. Bump
	 * the timeout base, in case we expect more data.
	 */
	tobase = (uint32_t)time(NULL);
	if ((datap + datasize + (pad * items)) > (pktdata + pktdatasize)) {
		size_t offset = datap - pktdata;
		growpktdata();
		*rdata = pktdata; /* might have been realloced ! */
		datap = pktdata + offset;
	}
	/* 
	 * We now move the pointer along according to size and number of
	 * items.  This is so we can play nice with older implementations
	 */

	tmp_data = rpkt.u.data;
	for (i = 0; i < items; i++) {
		memcpy(datap, tmp_data, (unsigned)size);
		tmp_data += size;
		zero_mem(datap + size, pad);
		datap += size + pad;
	}

	if (firstpkt) {
		firstpkt = 0;
		*rsize = size + pad;
	}
	*ritems += items;

	/*
	 * Finally, check the count of received packets.  If we've got them
	 * all, return
	 */
	++numrecv;
	if (numrecv <= lastseq)
		goto again;
	return INFO_OKAY;
}


/*
 * sendrequest - format and send a request packet
 *
 * Historically, ntpdc has used a fixed-size request packet regardless
 * of the actual payload size.  When authenticating, the timestamp, key
 * ID, and digest have been placed just before the end of the packet.
 * With the introduction in late 2009 of support for authenticated
 * ntpdc requests using larger 20-octet digests (vs. 16 for MD5), we
 * come up four bytes short.
 *
 * To maintain interop while allowing for larger digests, the behavior
 * is unchanged when using 16-octet digests.  For larger digests, the
 * timestamp, key ID, and digest are placed immediately following the
 * request payload, with the overall packet size variable.  ntpd can
 * distinguish 16-octet digests by the overall request size being
 * REQ_LEN_NOMAC + 4 + 16 with the auth bit enabled.  When using a
 * longer digest, that request size should be avoided.
 *
 * With the form used with 20-octet and larger digests, the timestamp,
 * key ID, and digest are located by ntpd relative to the start of the
 * packet, and the size of the digest is then implied by the packet
 * size.
 */
static int
sendrequest(
	int implcode,
	int reqcode,
	int auth,
	size_t qitems,
	size_t qsize,
	const char *qdata
	)
{
	struct req_pkt qpkt;
	size_t	datasize;
	size_t	reqsize;
	u_long	key_id;
	l_fp	ts;
	l_fp *	ptstamp;
	size_t	maclen;
	char *	pass;

	ZERO(qpkt);
	qpkt.rm_vn_mode = RM_VN_MODE(0, 0, 0);
	qpkt.implementation = (u_char)implcode;
	qpkt.request = (u_char)reqcode;

	datasize = qitems * qsize;
	if (datasize && qdata != NULL) {
		memcpy(qpkt.u.data, qdata, datasize);
		qpkt.err_nitems = ERR_NITEMS(0, qitems);
		qpkt.mbz_itemsize = MBZ_ITEMSIZE(qsize);
	} else {
		qpkt.err_nitems = ERR_NITEMS(0, 0);
		qpkt.mbz_itemsize = MBZ_ITEMSIZE(qsize);  /* allow for optional first item */
	}

	if (!auth || (keyid_entered && info_auth_keyid == 0)) {
		qpkt.auth_seq = AUTH_SEQ(0, 0);
		return sendpkt(&qpkt, req_pkt_size);
	}

	if (info_auth_keyid == 0) {
		key_id = getkeyid("Keyid: ");
		if (!key_id) {
			fprintf(stderr, "Invalid key identifier\n");
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
	qpkt.auth_seq = AUTH_SEQ(1, 0);
	if (info_auth_hashlen > 16) {
		/*
		 * Only ntpd which expects REQ_LEN_NOMAC plus maclen
		 * octets in an authenticated request using a 16 octet
		 * digest (that is, a newer ntpd) will handle digests
		 * larger than 16 octets, so for longer digests, do
		 * not attempt to shorten the requests for downlevel
		 * ntpd compatibility.
		 */
		if (REQ_LEN_NOMAC != req_pkt_size)
			return 1;
		reqsize = REQ_LEN_HDR + datasize + sizeof(*ptstamp);
		/* align to 32 bits */
		reqsize = (reqsize + 3) & ~3;
	} else
		reqsize = req_pkt_size;
	ptstamp = (void *)((char *)&qpkt + reqsize);
	ptstamp--;
	get_systime(&ts);
	L_ADD(&ts, &delay_time);
	HTONL_FP(&ts, ptstamp);
	maclen = authencrypt(
		info_auth_keyid, (void *)&qpkt, size2int_chk(reqsize));
	if (!maclen) {  
		fprintf(stderr, "Key not found\n");
		return 1;
	} else if (maclen != (size_t)(info_auth_hashlen + sizeof(keyid_t))) {
		fprintf(stderr,
			"%zu octet MAC, %zu expected with %zu octet digest\n",
			maclen, (info_auth_hashlen + sizeof(keyid_t)),
			info_auth_hashlen);
		return 1;
	}
	return sendpkt(&qpkt, reqsize + maclen);
}


/*
 * doquery - send a request and process the response
 */
int
doquery(
	int implcode,
	int reqcode,
	int auth,
	size_t qitems,
	size_t qsize,
	const char *qdata,
	size_t *ritems,
	size_t *rsize,
	const char **rdata,
 	int quiet_mask,
	int esize
	)
{
	int res;
	char junk[512];
	fd_set fds;
	struct sock_timeval tvzero;

	/*
	 * Check to make sure host is open
	 */
	if (!havehost) {
		(void) fprintf(stderr, "***No host open, use `host' command\n");
		return -1;
	}

	/*
	 * Poll the socket and clear out any pending data
	 */
again:
	do {
		tvzero.tv_sec = tvzero.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(sockfd, &fds);
		res = select(sockfd+1, &fds, NULL, NULL, &tvzero);
		if (res == -1) {
			warning("polling select");
			return -1;
		} else if (res > 0)

		    (void) recv(sockfd, junk, sizeof junk, 0);
	} while (res > 0);


	/*
	 * send a request
	 */
	res = sendrequest(implcode, reqcode, auth, qitems, qsize, qdata);
	if (res != 0)
		return res;
	
	/*
	 * Get the response.  If we got a standard error, print a message
	 */
	res = getresponse(implcode, reqcode, ritems, rsize, rdata, esize);

	/*
	 * Try to be compatible with older implementations of ntpd.
	 */
	if (res == INFO_ERR_FMT && req_pkt_size != 48) {
		int oldsize;

		oldsize = req_pkt_size;

		switch(req_pkt_size) {
		case REQ_LEN_NOMAC:
			req_pkt_size = 160;
			break;
		case 160:
			req_pkt_size = 48;
			break;
		}
		if (impl_ver == IMPL_XNTPD) {
			fprintf(stderr,
			    "***Warning changing to older implementation\n");
			return INFO_ERR_IMPL;
		}

		fprintf(stderr,
		    "***Warning changing the request packet size from %d to %d\n",
		    oldsize, req_pkt_size);
		goto again;
	}

 	/* log error message if not told to be quiet */
 	if ((res > 0) && (((1 << res) & quiet_mask) == 0)) {
		switch(res) {
		case INFO_ERR_IMPL:
			/* Give us a chance to try the older implementation. */
			if (implcode == IMPL_XNTPD)
				break;
			(void) fprintf(stderr,
				       "***Server implementation incompatible with our own\n");
			break;
		case INFO_ERR_REQ:
			(void) fprintf(stderr,
				       "***Server doesn't implement this request\n");
			break;
		case INFO_ERR_FMT:
			(void) fprintf(stderr,
				       "***Server reports a format error in the received packet (shouldn't happen)\n");
			break;
		case INFO_ERR_NODATA:
			(void) fprintf(stderr,
				       "***Server reports data not found\n");
			break;
		case INFO_ERR_AUTH:
			(void) fprintf(stderr, "***Permission denied\n");
			break;
		case ERR_TIMEOUT:
			(void) fprintf(stderr, "***Request timed out\n");
			break;
		case ERR_INCOMPLETE:
			(void) fprintf(stderr,
				       "***Response from server was incomplete\n");
			break;
		default:
			(void) fprintf(stderr,
				       "***Server returns unknown error code %d\n", res);
			break;
		}
	}
	return res;
}


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


#ifndef SYS_WINNT /* Under NT cannot handle SIGINT, WIN32 spawns a handler */
/*
 * abortcmd - catch interrupts and abort the current command
 */
static RETSIGTYPE
abortcmd(
	int sig
	)
{
	if (current_output == stdout)
		(void)fflush(stdout);
	putc('\n', stderr);
	(void)fflush(stderr);
	if (jump) {
		jump = 0;
		LONGJMP(interrupt_buf, 1);
	}
}
#endif /* SYS_WINNT */

/*
 * docmd - decode the command line and execute a command
 */
static void
docmd(
	const char *cmdline
	)
{
	char *tokens[1+MAXARGS+MOREARGS+2];
	struct parse pcmd;
	int ntok;
	int i, ti;
	int rval;
	struct xcmd *xcmd;

	ai_fam_templ = ai_fam_default;
	/*
	 * Tokenize the command line.  If nothing on it, return.
	 */
	if (strlen(cmdline) >= MAXLINE) {
		fprintf(stderr, "***Command ignored, more than %d characters:\n%s\n",
			MAXLINE - 1, cmdline);
		return;
	}
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
	
	/*
	 * Save the keyword, then walk through the arguments, interpreting
	 * as we go.
	 */
	pcmd.keyword = tokens[0];
	pcmd.nargs = 0;
	ti = 1;
	for (i = 0; i < MAXARGS && xcmd->arg[i] != NO;) {
		if ((i+ti) >= ntok) {
			if (!(xcmd->arg[i] & OPT)) {
				printusage(xcmd, stderr);
				return;
			}
			break;
		}
		if ((xcmd->arg[i] & OPT) && (*tokens[i+ti] == '>'))
			break;
		rval = getarg(tokens[i+ti], (int)xcmd->arg[i], &pcmd.argval[i]);
		if (rval == -1) {
			ti++;
			continue;
		}
		if (rval == 0)
			return;
		pcmd.nargs++;
		i++;
	}

	/* Any extra args are assumed to be "OPT|NTP_STR". */
	for ( ; i < MAXARGS + MOREARGS;) {
	     if ((i+ti) >= ntok)
		  break;
		rval = getarg(tokens[i+ti], (int)(OPT|NTP_STR), &pcmd.argval[i]);
		if (rval == -1) {
			ti++;
			continue;
		}
		if (rval == 0)
			return;
		pcmd.nargs++;
		i++;
	}

	i += ti;
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
		while (ISSPACE(*cp))
		    cp++;
		if (ISEOL(*cp))
		    break;
		do {
			*sp++ = *cp++;
		} while (!ISSPACE(*cp) && !ISEOL(*cp));

		*sp++ = '\0';
	}
}



/*
 * findcmd - find a command in a command description table
 */
static int
findcmd(
	register char *str,
	struct xcmd *clist1,
	struct xcmd *clist2,
	struct xcmd **cmd
	)
{
	register struct xcmd *cl;
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
 * getarg - interpret an argument token
 *
 * string is always set.
 * type is set to the decoded type.
 *
 * return:	 0 - failure
 *		 1 - success
 *		-1 - skip to next token
 */
static int
getarg(
	char *str,
	int code,
	arg_v *argp
	)
{
	int isneg;
	char *cp, *np;
	static const char *digits = "0123456789";

	ZERO(*argp);
	argp->string = str;
	argp->type   = code & ~OPT;

	switch (argp->type) {
	    case NTP_STR:
		break;
	    case NTP_ADD:
		if (!strcmp("-6", str)) {
			ai_fam_templ = AF_INET6;
			return -1;
		} else if (!strcmp("-4", str)) {
			ai_fam_templ = AF_INET;
			return -1;
		}
		if (!getnetnum(str, &(argp->netnum), (char *)0, 0)) {
			return 0;
		}
		break;
	    case NTP_INT:
	    case NTP_UINT:
		isneg = 0;
		np = str;
		if (*np == '-') {
			np++;
			isneg = 1;
		}

		argp->uval = 0;
		do {
			cp = strchr(digits, *np);
			if (cp == NULL) {
				(void) fprintf(stderr,
					       "***Illegal integer value %s\n", str);
				return 0;
			}
			argp->uval *= 10;
			argp->uval += (u_long)(cp - digits);
		} while (*(++np) != '\0');

		if (isneg) {
			if ((code & ~OPT) == NTP_UINT) {
				(void) fprintf(stderr,
					       "***Value %s should be unsigned\n", str);
				return 0;
			}
			argp->ival = -argp->ival;
		}
		break;
	    case IP_VERSION:
		if (!strcmp("-6", str))
			argp->ival = 6 ;
		else if (!strcmp("-4", str))
			argp->ival = 4 ;
		else {
			(void) fprintf(stderr,
			    "***Version must be either 4 or 6\n");
			return 0;
		}
		break;
	}

	return 1;
}


/*
 * getnetnum - given a host name, return its net number
 *	       and (optional) full name
 */
static int
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
	if (!showhostnames || SOCK_UNSPEC(netnum))
		return stoa(netnum);
	else if (ISREFCLOCKADR(netnum))
		return refnumtoa(netnum);
	else
		return socktohost(netnum);
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
	struct xcmd *xcp;
	char *cmd;
	const char *list[100];
	size_t word, words;
	size_t row, rows;
	size_t col, cols;
	size_t length;

	if (pcmd->nargs == 0) {
		words = 0;
		for (xcp = builtins; xcp->keyword != 0; xcp++) {
			if (*(xcp->keyword) != '?')
				list[words++] = xcp->keyword;
		}
		for (xcp = opcmds; xcp->keyword != 0; xcp++)
			list[words++] = xcp->keyword;

		qsort((void *)list, words, sizeof(list[0]), helpsort);
		col = 0;
		for (word = 0; word < words; word++) {
			length = strlen(list[word]);
			col = max(col, length);
		}

		cols = SCREENWIDTH / ++col;
		rows = (words + cols - 1) / cols;

		fprintf(fp, "ntpdc commands:\n");

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
	int i, opt46;

	opt46 = 0;
	(void) fprintf(fp, "usage: %s", xcp->keyword);
	for (i = 0; i < MAXARGS && xcp->arg[i] != NO; i++) {
		if (opt46 == 0 && (xcp->arg[i] & ~OPT) == NTP_ADD) {
			(void) fprintf(fp, " [ -4|-6 ]");
			opt46 = 1;
		}
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
		val = tvout.tv_sec * 1000 + tvout.tv_usec / 1000;
		(void) fprintf(fp, "primary timeout %d ms\n", val);
	} else {
		tvout.tv_sec = pcmd->argval[0].uval / 1000;
		tvout.tv_usec = (pcmd->argval[0].uval - (tvout.tv_sec * 1000))
			* 1000;
	}
}


/*
 * my_delay - set delay for auth requests
 */
static void
my_delay(
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
		    (void) fprintf(fp, "current host is %s\n", currenthost);
		else
		    (void) fprintf(fp, "no current host\n");
		return;
	}

	i = 0;
	if (pcmd->nargs == 2) {
		if (!strcmp("-4", pcmd->argval[i].string))
			ai_fam_templ = AF_INET;
		else if (!strcmp("-6", pcmd->argval[i].string))
			ai_fam_templ = AF_INET6;
		else {
			if (havehost)
				(void) fprintf(fp,
				    "current host remains %s\n", currenthost);
			else
				(void) fprintf(fp, "still no current host\n");
			return;
		}
		i = 1;
	}
	if (openhost(pcmd->argval[i].string)) {
		(void) fprintf(fp, "current host set to %s\n", currenthost);
	} else {
		if (havehost)
		    (void) fprintf(fp,
				   "current host remains %s\n", currenthost);
		else
		    (void) fprintf(fp, "still no current host\n");
	}
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
		if (info_auth_keyid == 0 && !keyid_entered)
		    (void) fprintf(fp, "no keyid defined\n");
		else if (info_auth_keyid == 0 && keyid_entered)
		    (void) fprintf(fp, "no keyid will be sent\n");
		else
		    (void) fprintf(fp, "keyid is %lu\n", (u_long)info_auth_keyid);
	} else {
		info_auth_keyid = pcmd->argval[0].uval;
		keyid_entered = 1;
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
		fprintf(fp, "keytype must be 'md5'%s\n",
#ifdef OPENSSL
			" or a digest type provided by OpenSSL");
#else
			"");
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
	char *pass;

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
		if ('\0' == *pass) {
			fprintf(fp, "Password unchanged\n");
			return;
		}
	}
	authusekey(info_auth_keyid, info_auth_keytype, (u_char *)pass);
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
	    closesocket(sockfd);
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
