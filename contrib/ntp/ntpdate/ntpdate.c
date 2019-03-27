/*
 * ntpdate - set the time of day by polling one or more NTP servers
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef HAVE_NETINFO
#include <netinfo/ni.h>
#endif

#include "ntp_machine.h"
#include "ntp_fp.h"
#include "ntp.h"
#include "ntp_io.h"
#include "timevalops.h"
#include "ntpdate.h"
#include "ntp_string.h"
#include "ntp_syslog.h"
#include "ntp_select.h"
#include "ntp_stdlib.h"
#include <ssl_applink.c>

#include "isc/net.h"
#include "isc/result.h"
#include "isc/sockaddr.h"

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <stdio.h>
#include <signal.h>
#include <ctype.h>
#ifdef HAVE_POLL_H
# include <poll.h>
#endif
#ifdef HAVE_SYS_SIGNAL_H
# include <sys/signal.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
# include <sys/resource.h>
#endif

#include <arpa/inet.h>

#ifdef SYS_VXWORKS
# include "ioLib.h"
# include "sockLib.h"
# include "timers.h"

/* select wants a zero structure ... */
struct timeval timeout = {0,0};
#elif defined(SYS_WINNT)
/*
 * Windows does not abort a select select call if SIGALRM goes off
 * so a 200 ms timeout is needed (TIMER_HZ is 5).
 */
struct sock_timeval timeout = {0,1000000/TIMER_HZ};
#else
struct timeval timeout = {60,0};
#endif

#ifdef HAVE_NETINFO
#include <netinfo/ni.h>
#endif

#include "recvbuff.h"

#ifdef SYS_WINNT
#define TARGET_RESOLUTION 1  /* Try for 1-millisecond accuracy
				on Windows NT timers. */
#pragma comment(lib, "winmm")
isc_boolean_t ntp_port_inuse(int af, u_short port);
UINT wTimerRes;
#endif /* SYS_WINNT */

/*
 * Scheduling priority we run at
 */
#ifndef SYS_VXWORKS
# define	NTPDATE_PRIO	(-12)
#else
# define	NTPDATE_PRIO	(100)
#endif

#ifdef HAVE_TIMER_CREATE
/* POSIX TIMERS - vxWorks doesn't have itimer - casey */
static timer_t ntpdate_timerid;
#endif

/*
 * Compatibility stuff for Version 2
 */
#define NTP_MAXSKW	0x28f	/* 0.01 sec in fp format */
#define NTP_MINDIST	0x51f	/* 0.02 sec in fp format */
#define PEER_MAXDISP	(64*FP_SECOND)	/* maximum dispersion (fp 64) */
#define NTP_INFIN	15	/* max stratum, infinity a la Bellman-Ford */
#define NTP_MAXWGT	(8*FP_SECOND)	/* maximum select weight 8 seconds */
#define NTP_MAXLIST	5	/* maximum select list size */
#define PEER_SHIFT	8	/* 8 suitable for crystal time base */

/*
 * for get_systime()
 */
s_char	sys_precision;		/* local clock precision (log2 s) */

/*
 * File descriptor masks etc. for call to select
 */

int ai_fam_templ;
int nbsock;			/* the number of sockets used */
SOCKET fd[MAX_AF];
int fd_family[MAX_AF];		/* to remember the socket family */
#ifdef HAVE_POLL_H
struct pollfd fdmask[MAX_AF];
#else
fd_set fdmask;
SOCKET maxfd;
#endif
int polltest = 0;

/*
 * Initializing flag.  All async routines watch this and only do their
 * thing when it is clear.
 */
int initializing = 1;

/*
 * Alarm flag.	Set when an alarm occurs
 */
volatile int alarm_flag = 0;

/*
 * Simple query flag.
 */
int simple_query = 0;

/*
 * Unprivileged port flag.
 */
int unpriv_port = 0;

/*
 * Program name.
 */
char const *progname;

/*
 * Systemwide parameters and flags
 */
int sys_samples = 0;		/* number of samples/server, will be modified later */
u_long sys_timeout = DEFTIMEOUT; /* timeout time, in TIMER_HZ units */
struct server *sys_servers;	/* the server list */
int sys_numservers = 0; 	/* number of servers to poll */
int sys_authenticate = 0;	/* true when authenticating */
u_int32 sys_authkey = 0;	/* set to authentication key in use */
u_long sys_authdelay = 0;	/* authentication delay */
int sys_version = NTP_VERSION;	/* version to poll with */

/*
 * The current internal time
 */
u_long current_time = 0;

/*
 * Counter for keeping track of completed servers
 */
int complete_servers = 0;

/*
 * File of encryption keys
 */

#ifndef KEYFILE
# ifndef SYS_WINNT
#define KEYFILE 	"/etc/ntp.keys"
# else
#define KEYFILE 	"%windir%\\ntp.keys"
# endif /* SYS_WINNT */
#endif /* KEYFILE */

#ifndef SYS_WINNT
const char *key_file = KEYFILE;
#else
char key_file_storage[MAX_PATH+1], *key_file ;
#endif	 /* SYS_WINNT */

/*
 * Miscellaneous flags
 */
int verbose = 0;
int always_step = 0;
int never_step = 0;

int 	ntpdatemain (int, char **);

static	void	transmit	(struct server *);
static	void	receive 	(struct recvbuf *);
static	void	server_data (struct server *, s_fp, l_fp *, u_fp);
static	void	clock_filter	(struct server *);
static	struct server *clock_select (void);
static	int clock_adjust	(void);
static	void	addserver	(char *);
static	struct server *findserver (sockaddr_u *);
		void	timer		(void);
static	void	init_alarm	(void);
#ifndef SYS_WINNT
static	RETSIGTYPE alarming (int);
#endif /* SYS_WINNT */
static	void	init_io 	(void);
static	void	sendpkt 	(sockaddr_u *, struct pkt *, int);
void	input_handler	(void);

static	int l_adj_systime	(l_fp *);
static	int l_step_systime	(l_fp *);

static	void	print_server (struct server *, FILE *);

#ifdef SYS_WINNT
int 	on = 1;
WORD	wVersionRequested;
WSADATA	wsaData;
#endif /* SYS_WINNT */

#ifdef NO_MAIN_ALLOWED
CALL(ntpdate,"ntpdate",ntpdatemain);

void clear_globals()
{
  /*
   * Debugging flag
   */
  debug = 0;

  ntp_optind = 0;
  /*
   * Initializing flag.  All async routines watch this and only do their
   * thing when it is clear.
   */
  initializing = 1;

  /*
   * Alarm flag.  Set when an alarm occurs
   */
  alarm_flag = 0;

  /*
   * Simple query flag.
   */
  simple_query = 0;

  /*
   * Unprivileged port flag.
   */
  unpriv_port = 0;

  /*
   * Systemwide parameters and flags
   */
  sys_numservers = 0;	  /* number of servers to poll */
  sys_authenticate = 0;   /* true when authenticating */
  sys_authkey = 0;	   /* set to authentication key in use */
  sys_authdelay = 0;   /* authentication delay */
  sys_version = NTP_VERSION;  /* version to poll with */

  /*
   * The current internal time
   */
  current_time = 0;

  /*
   * Counter for keeping track of completed servers
   */
  complete_servers = 0;
  verbose = 0;
  always_step = 0;
  never_step = 0;
}
#endif

#ifdef HAVE_NETINFO
static ni_namelist *getnetinfoservers (void);
#endif

/*
 * Main program.  Initialize us and loop waiting for I/O and/or
 * timer expiries.
 */
#ifndef NO_MAIN_ALLOWED
int
main(
	int argc,
	char *argv[]
	)
{
	return ntpdatemain (argc, argv);
}
#endif /* NO_MAIN_ALLOWED */

int
ntpdatemain (
	int argc,
	char *argv[]
	)
{
	int was_alarmed;
	int tot_recvbufs;
	struct recvbuf *rbuf;
	l_fp tmp;
	int errflg;
	int c;
	int nfound;

#ifdef HAVE_NETINFO
	ni_namelist *netinfoservers;
#endif
#ifdef SYS_WINNT
	key_file = key_file_storage;

	if (!ExpandEnvironmentStrings(KEYFILE, key_file, MAX_PATH))
		msyslog(LOG_ERR, "ExpandEnvironmentStrings(KEYFILE) failed: %m");

	ssl_applink();
#endif /* SYS_WINNT */

#ifdef NO_MAIN_ALLOWED
	clear_globals();
#endif

	init_lib();	/* sets up ipv4_works, ipv6_works */

	/* Check to see if we have IPv6. Otherwise default to IPv4 */
	if (!ipv6_works)
		ai_fam_templ = AF_INET;

	errflg = 0;
	progname = argv[0];
	syslogit = 0;

	/*
	 * Decode argument list
	 */
	while ((c = ntp_getopt(argc, argv, "46a:bBde:k:o:p:qst:uv")) != EOF)
		switch (c)
		{
		case '4':
			ai_fam_templ = AF_INET;
			break;
		case '6':
			ai_fam_templ = AF_INET6;
			break;
		case 'a':
			c = atoi(ntp_optarg);
			sys_authenticate = 1;
			sys_authkey = c;
			break;
		case 'b':
			always_step++;
			never_step = 0;
			break;
		case 'B':
			never_step++;
			always_step = 0;
			break;
		case 'd':
			++debug;
			break;
		case 'e':
			if (!atolfp(ntp_optarg, &tmp)
			|| tmp.l_ui != 0) {
				(void) fprintf(stderr,
					   "%s: encryption delay %s is unlikely\n",
					   progname, ntp_optarg);
				errflg++;
			} else {
				sys_authdelay = tmp.l_uf;
			}
			break;
		case 'k':
			key_file = ntp_optarg;
			break;
		case 'o':
			sys_version = atoi(ntp_optarg);
			break;
		case 'p':
			c = atoi(ntp_optarg);
			if (c <= 0 || c > NTP_SHIFT) {
				(void) fprintf(stderr,
					   "%s: number of samples (%d) is invalid\n",
					   progname, c);
				errflg++;
			} else {
				sys_samples = c;
			}
			break;
		case 'q':
			simple_query = 1;
			break;
		case 's':
			syslogit = 1;
			break;
		case 't':
			if (!atolfp(ntp_optarg, &tmp)) {
				(void) fprintf(stderr,
					   "%s: timeout %s is undecodeable\n",
					   progname, ntp_optarg);
				errflg++;
			} else {
				sys_timeout = ((LFPTOFP(&tmp) * TIMER_HZ)
					   + 0x8000) >> 16;
				sys_timeout = max(sys_timeout, MINTIMEOUT);
			}
			break;
		case 'v':
			verbose = 1;
			break;
		case 'u':
			unpriv_port = 1;
			break;
		case '?':
			++errflg;
			break;
		default:
			break;
	    }

	if (errflg) {
		(void) fprintf(stderr,
		    "usage: %s [-46bBdqsuv] [-a key#] [-e delay] [-k file] [-p samples] [-o version#] [-t timeo] server ...\n",
		    progname);
		exit(2);
	}

	/*
	 * If number of Samples (-p) not specified by user:
	 * - if a simple_query (-q) just ONE will do
	 * - otherwise the normal is DEFSAMPLES
	 */
	if (sys_samples == 0)
		 sys_samples = (simple_query ? 1 : DEFSAMPLES);

	if (debug || simple_query) {
#ifdef HAVE_SETVBUF
		static char buf[BUFSIZ];
		setvbuf(stdout, buf, _IOLBF, BUFSIZ);
#else
		setlinebuf(stdout);
#endif
	}

	/*
	 * Logging.  Open the syslog if we have to
	 */
	if (syslogit) {
#if !defined (SYS_WINNT) && !defined (SYS_VXWORKS) && !defined SYS_CYGWIN32
# ifndef	LOG_DAEMON
		openlog("ntpdate", LOG_PID);
# else

#  ifndef	LOG_NTP
#	define	LOG_NTP LOG_DAEMON
#  endif
		openlog("ntpdate", LOG_PID | LOG_NDELAY, LOG_NTP);
		if (debug)
			setlogmask(LOG_UPTO(LOG_DEBUG));
		else
			setlogmask(LOG_UPTO(LOG_INFO));
# endif /* LOG_DAEMON */
#endif	/* SYS_WINNT */
	}

	if (debug || verbose)
		msyslog(LOG_NOTICE, "%s", Version);

	/*
	 * Add servers we are going to be polling
	 */
#ifdef HAVE_NETINFO
	netinfoservers = getnetinfoservers();
#endif

	for ( ; ntp_optind < argc; ntp_optind++)
		addserver(argv[ntp_optind]);

#ifdef HAVE_NETINFO
	if (netinfoservers) {
		if ( netinfoservers->ni_namelist_len &&
		    *netinfoservers->ni_namelist_val ) {
			u_int servercount = 0;
			while (servercount < netinfoservers->ni_namelist_len) {
				if (debug) msyslog(LOG_DEBUG,
						   "Adding time server %s from NetInfo configuration.",
						   netinfoservers->ni_namelist_val[servercount]);
				addserver(netinfoservers->ni_namelist_val[servercount++]);
			}
		}
		ni_namelist_free(netinfoservers);
		free(netinfoservers);
	}
#endif

	if (sys_numservers == 0) {
		msyslog(LOG_ERR, "no servers can be used, exiting");
		exit(1);
	}

	/*
	 * Initialize the time of day routines and the I/O subsystem
	 */
	if (sys_authenticate) {
		init_auth();
		if (!authreadkeys(key_file)) {
			msyslog(LOG_ERR, "no key file <%s>, exiting", key_file);
			exit(1);
		}
		authtrust(sys_authkey, 1);
		if (!authistrusted(sys_authkey)) {
			msyslog(LOG_ERR, "authentication key %lu unknown",
				(unsigned long) sys_authkey);
			exit(1);
		}
	}
	init_io();
	init_alarm();

	/*
	 * Set the priority.
	 */
#ifdef SYS_VXWORKS
	taskPrioritySet( taskIdSelf(), NTPDATE_PRIO);
#endif
#if defined(HAVE_ATT_NICE)
	nice (NTPDATE_PRIO);
#endif
#if defined(HAVE_BSD_NICE)
	(void) setpriority(PRIO_PROCESS, 0, NTPDATE_PRIO);
#endif


	initializing = 0;
	was_alarmed = 0;

	while (complete_servers < sys_numservers) {
#ifdef HAVE_POLL_H
		struct pollfd* rdfdes;
		rdfdes = fdmask;
#else
		fd_set rdfdes;
		rdfdes = fdmask;
#endif

		if (alarm_flag) {		/* alarmed? */
			was_alarmed = 1;
			alarm_flag = 0;
		}
		tot_recvbufs = full_recvbuffs();	/* get received buffers */

		if (!was_alarmed && tot_recvbufs == 0) {
			/*
			 * Nothing to do.	 Wait for something.
			 */
#ifdef HAVE_POLL_H
			nfound = poll(rdfdes, (unsigned int)nbsock, timeout.tv_sec * 1000);

#else
			nfound = select(maxfd, &rdfdes, NULL, NULL,
					&timeout);
#endif
			if (nfound > 0)
				input_handler();
			else if (nfound == SOCKET_ERROR)
			{
#ifndef SYS_WINNT
				if (errno != EINTR)
#else
				if (WSAGetLastError() != WSAEINTR)
#endif
					msyslog(LOG_ERR,
#ifdef HAVE_POLL_H
						"poll() error: %m"
#else
						"select() error: %m"
#endif
						);
			} else if (errno != 0) {
#ifndef SYS_VXWORKS
				msyslog(LOG_DEBUG,
#ifdef HAVE_POLL_H
					"poll(): nfound = %d, error: %m",
#else
					"select(): nfound = %d, error: %m",
#endif
					nfound);
#endif
			}
			if (alarm_flag) {		/* alarmed? */
				was_alarmed = 1;
				alarm_flag = 0;
			}
			tot_recvbufs = full_recvbuffs();	/* get received buffers */
		}

		/*
		 * Out here, signals are unblocked.  Call receive
		 * procedure for each incoming packet.
		 */
		rbuf = get_full_recv_buffer();
		while (rbuf != NULL)
		{
			receive(rbuf);
			freerecvbuf(rbuf);
			rbuf = get_full_recv_buffer();
		}

		/*
		 * Call timer to process any timeouts
		 */
		if (was_alarmed) {
			timer();
			was_alarmed = 0;
		}

		/*
		 * Go around again
		 */
	}

	/*
	 * When we get here we've completed the polling of all servers.
	 * Adjust the clock, then exit.
	 */
#ifdef SYS_WINNT
	WSACleanup();
#endif
#ifdef SYS_VXWORKS
	close (fd);
	timer_delete(ntpdate_timerid);
#endif

	return clock_adjust();
}


/*
 * transmit - transmit a packet to the given server, or mark it completed.
 *		This is called by the timeout routine and by the receive
 *		procedure.
 */
static void
transmit(
	register struct server *server
	)
{
	struct pkt xpkt;

	if (server->filter_nextpt < server->xmtcnt) {
		l_fp ts;
		/*
		 * Last message to this server timed out.  Shift
		 * zeros into the filter.
		 */
		L_CLR(&ts);
		server_data(server, 0, &ts, 0);
	}

	if ((int)server->filter_nextpt >= sys_samples) {
		/*
		 * Got all the data we need.  Mark this guy
		 * completed and return.
		 */
		server->event_time = 0;
		complete_servers++;
		return;
	}

	if (debug)
		printf("transmit(%s)\n", stoa(&server->srcadr));

	/*
	 * If we're here, send another message to the server.  Fill in
	 * the packet and let 'er rip.
	 */
	xpkt.li_vn_mode = PKT_LI_VN_MODE(LEAP_NOTINSYNC,
					 sys_version, MODE_CLIENT);
	xpkt.stratum = STRATUM_TO_PKT(STRATUM_UNSPEC);
	xpkt.ppoll = NTP_MINPOLL;
	xpkt.precision = NTPDATE_PRECISION;
	xpkt.rootdelay = htonl(NTPDATE_DISTANCE);
	xpkt.rootdisp = htonl(NTPDATE_DISP);
	xpkt.refid = htonl(NTPDATE_REFID);
	L_CLR(&xpkt.reftime);
	L_CLR(&xpkt.org);
	L_CLR(&xpkt.rec);

	/*
	 * Determine whether to authenticate or not.	If so,
	 * fill in the extended part of the packet and do it.
	 * If not, just timestamp it and send it away.
	 */
	if (sys_authenticate) {
		size_t len;

		xpkt.exten[0] = htonl(sys_authkey);
		get_systime(&server->xmt);
		L_ADDUF(&server->xmt, sys_authdelay);
		HTONL_FP(&server->xmt, &xpkt.xmt);
		len = authencrypt(sys_authkey, (u_int32 *)&xpkt, LEN_PKT_NOMAC);
		sendpkt(&server->srcadr, &xpkt, (int)(LEN_PKT_NOMAC + len));

		if (debug > 1)
			printf("transmit auth to %s\n",
			   stoa(&server->srcadr));
	} else {
		get_systime(&(server->xmt));
		HTONL_FP(&server->xmt, &xpkt.xmt);
		sendpkt(&server->srcadr, &xpkt, LEN_PKT_NOMAC);

		if (debug > 1)
			printf("transmit to %s\n", stoa(&server->srcadr));
	}

	/*
	 * Update the server timeout and transmit count
	 */
	server->event_time = current_time + sys_timeout;
	server->xmtcnt++;
}


/*
 * receive - receive and process an incoming frame
 */
static void
receive(
	struct recvbuf *rbufp
	)
{
	register struct pkt *rpkt;
	register struct server *server;
	register s_fp di;
	l_fp t10, t23, tmp;
	l_fp org;
	l_fp rec;
	l_fp ci;
	int has_mac;
	int is_authentic;

	if (debug)
		printf("receive(%s)\n", stoa(&rbufp->recv_srcadr));
	/*
	 * Check to see if the packet basically looks like something
	 * intended for us.
	 */
	if (rbufp->recv_length == LEN_PKT_NOMAC)
		has_mac = 0;
	else if (rbufp->recv_length >= (int)LEN_PKT_NOMAC)
		has_mac = 1;
	else {
		if (debug)
			printf("receive: packet length %d\n",
			   rbufp->recv_length);
		return; 		/* funny length packet */
	}

	rpkt = &(rbufp->recv_pkt);
	if (PKT_VERSION(rpkt->li_vn_mode) < NTP_OLDVERSION ||
		PKT_VERSION(rpkt->li_vn_mode) > NTP_VERSION) {
		return;
	}

	if ((PKT_MODE(rpkt->li_vn_mode) != MODE_SERVER
		 && PKT_MODE(rpkt->li_vn_mode) != MODE_PASSIVE)
		|| rpkt->stratum >= STRATUM_UNSPEC) {
		if (debug)
			printf("receive: mode %d stratum %d\n",
			   PKT_MODE(rpkt->li_vn_mode), rpkt->stratum);
		return;
	}

	/*
	 * So far, so good.  See if this is from a server we know.
	 */
	server = findserver(&(rbufp->recv_srcadr));
	if (server == NULL) {
		if (debug)
			printf("receive: server not found\n");
		return;
	}

	/*
	 * Decode the org timestamp and make sure we're getting a response
	 * to our last request.
	 */
	NTOHL_FP(&rpkt->org, &org);
	if (!L_ISEQU(&org, &server->xmt)) {
		if (debug)
			printf("receive: pkt.org and peer.xmt differ\n");
		return;
	}

	/*
	 * Check out the authenticity if we're doing that.
	 */
	if (!sys_authenticate)
		is_authentic = 1;
	else {
		is_authentic = 0;

		if (debug > 3)
			printf("receive: rpkt keyid=%ld sys_authkey=%ld decrypt=%ld\n",
			   (long int)ntohl(rpkt->exten[0]), (long int)sys_authkey,
			   (long int)authdecrypt(sys_authkey, (u_int32 *)rpkt,
				LEN_PKT_NOMAC, (size_t)(rbufp->recv_length - LEN_PKT_NOMAC)));

		if (has_mac && ntohl(rpkt->exten[0]) == sys_authkey &&
			authdecrypt(sys_authkey, (u_int32 *)rpkt, LEN_PKT_NOMAC,
			(size_t)(rbufp->recv_length - LEN_PKT_NOMAC)))
			is_authentic = 1;
		if (debug)
			printf("receive: authentication %s\n",
			   is_authentic ? "passed" : "failed");
	}
	server->trust <<= 1;
	if (!is_authentic)
		server->trust |= 1;

	/*
	 * Check for a KoD (rate limiting) response, cease and decist.
	 */
	if (LEAP_NOTINSYNC == PKT_LEAP(rpkt->li_vn_mode) &&
	    STRATUM_PKT_UNSPEC == rpkt->stratum &&
	    !memcmp("RATE", &rpkt->refid, 4)) {
		msyslog(LOG_ERR, "%s rate limit response from server.",
			stoa(&rbufp->recv_srcadr));
		server->event_time = 0;
		complete_servers++;
		return;
	}

	/*
	 * Looks good.	Record info from the packet.
	 */
	server->leap = PKT_LEAP(rpkt->li_vn_mode);
	server->stratum = PKT_TO_STRATUM(rpkt->stratum);
	server->precision = rpkt->precision;
	server->rootdelay = ntohl(rpkt->rootdelay);
	server->rootdisp = ntohl(rpkt->rootdisp);
	server->refid = rpkt->refid;
	NTOHL_FP(&rpkt->reftime, &server->reftime);
	NTOHL_FP(&rpkt->rec, &rec);
	NTOHL_FP(&rpkt->xmt, &server->org);

	/*
	 * Make sure the server is at least somewhat sane. If not, try
	 * again.
	 */
	if (L_ISZERO(&rec) || !L_ISHIS(&server->org, &rec)) {
		server->event_time = current_time + sys_timeout;
		return;
	}

	/*
	 * Calculate the round trip delay (di) and the clock offset (ci).
	 * We use the equations (reordered from those in the spec):
	 *
	 * d = (t2 - t3) - (t1 - t0)
	 * c = ((t2 - t3) + (t1 - t0)) / 2
	 */
	t10 = server->org;		/* pkt.xmt == t1 */
	L_SUB(&t10, &rbufp->recv_time); /* recv_time == t0*/

	t23 = rec;			/* pkt.rec == t2 */
	L_SUB(&t23, &org);		/* pkt->org == t3 */

	/* now have (t2 - t3) and (t0 - t1).	Calculate (ci) and (di) */
	/*
	 * Calculate (ci) = ((t1 - t0) / 2) + ((t2 - t3) / 2)
	 * For large offsets this may prevent an overflow on '+'
	 */
	ci = t10;
	L_RSHIFT(&ci);
	tmp = t23;
	L_RSHIFT(&tmp);
	L_ADD(&ci, &tmp);

	/*
	 * Calculate di in t23 in full precision, then truncate
	 * to an s_fp.
	 */
	L_SUB(&t23, &t10);
	di = LFPTOFP(&t23);

	if (debug > 3)
		printf("offset: %s, delay %s\n", lfptoa(&ci, 6), fptoa(di, 5));

	di += (FP_SECOND >> (-(int)NTPDATE_PRECISION))
		+ (FP_SECOND >> (-(int)server->precision)) + NTP_MAXSKW;

	if (di <= 0) {		/* value still too raunchy to use? */
		L_CLR(&ci);
		di = 0;
	} else {
		di = max(di, NTP_MINDIST);
	}

	/*
	 * Shift this data in, then schedule another transmit.
	 */
	server_data(server, (s_fp) di, &ci, 0);

	if ((int)server->filter_nextpt >= sys_samples) {
		/*
		 * Got all the data we need.  Mark this guy
		 * completed and return.
		 */
		server->event_time = 0;
		complete_servers++;
		return;
	}

	server->event_time = current_time + sys_timeout;
}


/*
 * server_data - add a sample to the server's filter registers
 */
static void
server_data(
	register struct server *server,
	s_fp d,
	l_fp *c,
	u_fp e
	)
{
	u_short i;

	i = server->filter_nextpt;
	if (i < NTP_SHIFT) {
		server->filter_delay[i] = d;
		server->filter_offset[i] = *c;
		server->filter_soffset[i] = LFPTOFP(c);
		server->filter_error[i] = e;
		server->filter_nextpt = (u_short)(i + 1);
	}
}


/*
 * clock_filter - determine a server's delay, dispersion and offset
 */
static void
clock_filter(
	register struct server *server
	)
{
	register int i, j;
	int ord[NTP_SHIFT];

	INSIST((0 < sys_samples) && (sys_samples <= NTP_SHIFT));

	/*
	 * Sort indices into increasing delay order
	 */
	for (i = 0; i < sys_samples; i++)
		ord[i] = i;

	for (i = 0; i < (sys_samples-1); i++) {
		for (j = i+1; j < sys_samples; j++) {
			if (server->filter_delay[ord[j]] == 0)
				continue;
			if (server->filter_delay[ord[i]] == 0
				|| (server->filter_delay[ord[i]]
				> server->filter_delay[ord[j]])) {
				register int tmp;

				tmp = ord[i];
				ord[i] = ord[j];
				ord[j] = tmp;
			}
		}
	}

	/*
	 * Now compute the dispersion, and assign values to delay and
	 * offset.	If there are no samples in the register, delay and
	 * offset go to zero and dispersion is set to the maximum.
	 */
	if (server->filter_delay[ord[0]] == 0) {
		server->delay = 0;
		L_CLR(&server->offset);
		server->soffset = 0;
		server->dispersion = PEER_MAXDISP;
	} else {
		register s_fp d;

		server->delay = server->filter_delay[ord[0]];
		server->offset = server->filter_offset[ord[0]];
		server->soffset = LFPTOFP(&server->offset);
		server->dispersion = 0;
		for (i = 1; i < sys_samples; i++) {
			if (server->filter_delay[ord[i]] == 0)
				d = PEER_MAXDISP;
			else {
				d = server->filter_soffset[ord[i]]
					- server->filter_soffset[ord[0]];
				if (d < 0)
					d = -d;
				if (d > PEER_MAXDISP)
					d = PEER_MAXDISP;
			}
			/*
			 * XXX This *knows* PEER_FILTER is 1/2
			 */
			server->dispersion += (u_fp)(d) >> i;
		}
	}
	/*
	 * We're done
	 */
}


/*
 * clock_select - select the pick-of-the-litter clock from the samples
 *		  we've got.
 */
static struct server *
clock_select(void)
{
	struct server *server;
	u_int nlist;
	s_fp d;
	u_int count;
	u_int i;
	u_int j;
	u_int k;
	int n;
	s_fp local_threshold;
	struct server *server_list[NTP_MAXCLOCK];
	u_fp server_badness[NTP_MAXCLOCK];
	struct server *sys_server;

	/*
	 * This first chunk of code is supposed to go through all
	 * servers we know about to find the NTP_MAXLIST servers which
	 * are most likely to succeed. We run through the list
	 * doing the sanity checks and trying to insert anyone who
	 * looks okay. We are at all times aware that we should
	 * only keep samples from the top two strata and we only need
	 * NTP_MAXLIST of them.
	 */
	nlist = 0;	/* none yet */
	for (server = sys_servers; server != NULL; server = server->next_server) {
		if (server->stratum == 0) {
			if (debug)
				printf("%s: Server dropped: no data\n", ntoa(&server->srcadr));
			continue;	/* no data */
		}
		if (server->stratum > NTP_INFIN) {
			if (debug)
				printf("%s: Server dropped: strata too high\n", ntoa(&server->srcadr));
			continue;	/* stratum no good */
		}
		if (server->delay > NTP_MAXWGT) {
			if (debug)
				printf("%s: Server dropped: server too far away\n",
					ntoa(&server->srcadr));
			continue;	/* too far away */
		}
		if (server->leap == LEAP_NOTINSYNC) {
			if (debug)
				printf("%s: Server dropped: leap not in sync\n", ntoa(&server->srcadr));
			continue;	/* he's in trouble */
		}
		if (!L_ISHIS(&server->org, &server->reftime)) {
			if (debug)
				printf("%s: Server dropped: server is very broken\n",
				       ntoa(&server->srcadr));
			continue;	/* very broken host */
		}
		if ((server->org.l_ui - server->reftime.l_ui)
		    >= NTP_MAXAGE) {
			if (debug)
				printf("%s: Server dropped: server has gone too long without sync\n",
				       ntoa(&server->srcadr));
			continue;	/* too long without sync */
		}
		if (server->trust != 0) {
			if (debug)
				printf("%s: Server dropped: Server is untrusted\n",
				       ntoa(&server->srcadr));
			continue;
		}

		/*
		 * This one seems sane.  Find where he belongs
		 * on the list.
		 */
		d = server->dispersion + server->dispersion;
		for (i = 0; i < nlist; i++)
			if (server->stratum <= server_list[i]->stratum)
			break;
		for ( ; i < nlist; i++) {
			if (server->stratum < server_list[i]->stratum)
				break;
			if (d < (s_fp) server_badness[i])
				break;
		}

		/*
		 * If i points past the end of the list, this
		 * guy is a loser, else stick him in.
		 */
		if (i >= NTP_MAXLIST)
			continue;
		for (j = nlist; j > i; j--)
			if (j < NTP_MAXLIST) {
				server_list[j] = server_list[j-1];
				server_badness[j]
					= server_badness[j-1];
			}

		server_list[i] = server;
		server_badness[i] = d;
		if (nlist < NTP_MAXLIST)
			nlist++;
	}

	/*
	 * Got the five-or-less best.	 Cut the list where the number of
	 * strata exceeds two.
	 */
	count = 0;
	for (i = 1; i < nlist; i++)
		if (server_list[i]->stratum > server_list[i-1]->stratum) {
			count++;
			if (2 == count) {
				nlist = i;
				break;
			}
		}

	/*
	 * Whew!  What we should have by now is 0 to 5 candidates for
	 * the job of syncing us.  If we have none, we're out of luck.
	 * If we have one, he's a winner.  If we have more, do falseticker
	 * detection.
	 */

	if (0 == nlist)
		sys_server = NULL;
	else if (1 == nlist) {
		sys_server = server_list[0];
	} else {
		/*
		 * Re-sort by stratum, bdelay estimate quality and
		 * server.delay.
		 */
		for (i = 0; i < nlist-1; i++)
			for (j = i+1; j < nlist; j++) {
				if (server_list[i]->stratum <
				    server_list[j]->stratum)
					/* already sorted by stratum */
					break;
				if (server_list[i]->delay <
				    server_list[j]->delay)
					continue;
				server = server_list[i];
				server_list[i] = server_list[j];
				server_list[j] = server;
			}

		/*
		 * Calculate the fixed part of the dispersion limit
		 */
		local_threshold = (FP_SECOND >> (-(int)NTPDATE_PRECISION))
			+ NTP_MAXSKW;

		/*
		 * Now drop samples until we're down to one.
		 */
		while (nlist > 1) {
			for (k = 0; k < nlist; k++) {
				server_badness[k] = 0;
				for (j = 0; j < nlist; j++) {
					if (j == k) /* with self? */
						continue;
					d = server_list[j]->soffset -
					    server_list[k]->soffset;
					if (d < 0)	/* abs value */
						d = -d;
					/*
					 * XXX This code *knows* that
					 * NTP_SELECT is 3/4
					 */
					for (i = 0; i < j; i++)
						d = (d>>1) + (d>>2);
					server_badness[k] += d;
				}
			}

			/*
			 * We now have an array of nlist badness
			 * coefficients.	Find the badest.  Find
			 * the minimum precision while we're at
			 * it.
			 */
			i = 0;
			n = server_list[0]->precision;;
			for (j = 1; j < nlist; j++) {
				if (server_badness[j] >= server_badness[i])
					i = j;
				if (n > server_list[j]->precision)
					n = server_list[j]->precision;
			}

			/*
			 * i is the index of the server with the worst
			 * dispersion.	If his dispersion is less than
			 * the threshold, stop now, else delete him and
			 * continue around again.
			 */
			if ( (s_fp) server_badness[i] < (local_threshold
							 + (FP_SECOND >> (-n))))
				break;
			for (j = i + 1; j < nlist; j++)
				server_list[j-1] = server_list[j];
			nlist--;
		}

		/*
		 * What remains is a list of less than 5 servers.  Take
		 * the best.
		 */
		sys_server = server_list[0];
	}

	/*
	 * That's it.  Return our server.
	 */
	return sys_server;
}


/*
 * clock_adjust - process what we've received, and adjust the time
 *		 if we got anything decent.
 */
static int
clock_adjust(void)
{
	register struct server *sp, *server;
	int dostep;

	for (sp = sys_servers; sp != NULL; sp = sp->next_server)
		clock_filter(sp);
	server = clock_select();

	if (debug || simple_query) {
		if (debug)
			printf ("\n");
		for (sp = sys_servers; sp != NULL; sp = sp->next_server)
			print_server(sp, stdout);
	}

	if (server == 0) {
		msyslog(LOG_ERR,
			"no server suitable for synchronization found");
		return(1);
	}

	if (always_step) {
		dostep = 1;
	} else if (never_step) {
		dostep = 0;
	} else {
		/* [Bug 3023] get absolute difference, avoiding signed
		 * integer overflow like hell.
		 */
		u_fp absoffset;
		if (server->soffset < 0)
			absoffset = 1u + (u_fp)(-(server->soffset + 1));
		else
			absoffset = (u_fp)server->soffset;
		dostep = (absoffset >= NTPDATE_THRESHOLD);
	}

	if (dostep) {
		if (simple_query || l_step_systime(&server->offset)){
			msyslog(LOG_NOTICE, "step time server %s offset %s sec",
				stoa(&server->srcadr),
				lfptoa(&server->offset, 6));
		}
	} else {
		if (simple_query || l_adj_systime(&server->offset)) {
			msyslog(LOG_NOTICE, "adjust time server %s offset %s sec",
				stoa(&server->srcadr),
				lfptoa(&server->offset, 6));
		}
	}
	return(0);
}


/*
 * is_unreachable - check to see if we have a route to given destination
 *		    (non-blocking).
 */
static int
is_reachable (sockaddr_u *dst)
{
	SOCKET sockfd;

	sockfd = socket(AF(dst), SOCK_DGRAM, 0);
	if (sockfd == -1) {
		return 0;
	}

	if (connect(sockfd, &dst->sa, SOCKLEN(dst))) {
		closesocket(sockfd);
		return 0;
	}
	closesocket(sockfd);
	return 1;
}



/* XXX ELIMINATE: merge BIG slew into adj_systime in lib/systime.c */
/*
 * addserver - determine a server's address and allocate a new structure
 *		for it.
 */
static void
addserver(
	char *serv
	)
{
	register struct server *server;
	/* Address infos structure to store result of getaddrinfo */
	struct addrinfo *addrResult, *ptr;
	/* Address infos structure to store hints for getaddrinfo */
	struct addrinfo hints;
	/* Error variable for getaddrinfo */
	int error;
	/* Service name */
	char service[5];
	sockaddr_u addr;

	strlcpy(service, "ntp", sizeof(service));

	/* Get host address. Looking for UDP datagram connection. */
	ZERO(hints);
	hints.ai_family = ai_fam_templ;
	hints.ai_socktype = SOCK_DGRAM;

#ifdef DEBUG
	if (debug)
		printf("Looking for host %s and service %s\n", serv, service);
#endif

	error = getaddrinfo(serv, service, &hints, &addrResult);
	if (error != 0) {
		/* Conduct more refined error analysis */
		if (error == EAI_FAIL || error == EAI_AGAIN){
			/* Name server is unusable. Exit after failing on the
			   first server, in order to shorten the timeout caused
			   by waiting for resolution of several servers */
			fprintf(stderr, "Exiting, name server cannot be used: %s (%d)",
				gai_strerror(error), error);
			msyslog(LOG_ERR, "name server cannot be used: %s (%d)",
				gai_strerror(error), error);
			exit(1);
		}
		fprintf(stderr, "Error resolving %s: %s (%d)\n", serv,
			gai_strerror(error), error);
		msyslog(LOG_ERR, "Can't find host %s: %s (%d)", serv,
			gai_strerror(error), error);
		return;
	}
#ifdef DEBUG
	if (debug) {
		ZERO(addr);
		INSIST(addrResult->ai_addrlen <= sizeof(addr));
		memcpy(&addr, addrResult->ai_addr, addrResult->ai_addrlen);
		fprintf(stderr, "host found : %s\n", stohost(&addr));
	}
#endif

	/* We must get all returned server in case the first one fails */
	for (ptr = addrResult; ptr != NULL; ptr = ptr->ai_next) {
		ZERO(addr);
		INSIST(ptr->ai_addrlen <= sizeof(addr));
		memcpy(&addr, ptr->ai_addr, ptr->ai_addrlen);
		if (is_reachable(&addr)) {
			server = emalloc_zero(sizeof(*server));
			memcpy(&server->srcadr, ptr->ai_addr, ptr->ai_addrlen);
			server->event_time = ++sys_numservers;
			if (sys_servers == NULL)
				sys_servers = server;
			else {
				struct server *sp;

				for (sp = sys_servers; sp->next_server != NULL;
				     sp = sp->next_server)
					/* empty */;
				sp->next_server = server;
			}
		}
	}

	freeaddrinfo(addrResult);
}


/*
 * findserver - find a server in the list given its address
 * ***(For now it isn't totally AF-Independant, to check later..)
 */
static struct server *
findserver(
	sockaddr_u *addr
	)
{
	struct server *server;
	struct server *mc_server;

	mc_server = NULL;
	if (SRCPORT(addr) != NTP_PORT)
		return 0;

	for (server = sys_servers; server != NULL;
	     server = server->next_server) {
		if (SOCK_EQ(addr, &server->srcadr))
			return server;

		if (AF(addr) == AF(&server->srcadr)) {
			if (IS_MCAST(&server->srcadr))
				mc_server = server;
		}
	}

	if (mc_server != NULL) {

		struct server *sp;

		if (mc_server->event_time != 0) {
			mc_server->event_time = 0;
			complete_servers++;
		}

		server = emalloc_zero(sizeof(*server));

		server->srcadr = *addr;

		server->event_time = ++sys_numservers;

		for (sp = sys_servers; sp->next_server != NULL;
		     sp = sp->next_server)
			/* empty */;
		sp->next_server = server;
		transmit(server);
	}
	return NULL;
}


/*
 * timer - process a timer interrupt
 */
void
timer(void)
{
	struct server *server;

	/*
	 * Bump the current idea of the time
	 */
	current_time++;

	/*
	 * Search through the server list looking for guys
	 * who's event timers have expired.  Give these to
	 * the transmit routine.
	 */
	for (server = sys_servers; server != NULL;
	     server = server->next_server) {
		if (server->event_time != 0
		    && server->event_time <= current_time)
			transmit(server);
	}
}


/*
 * The code duplication in the following subroutine sucks, but
 * we need to appease ansi2knr.
 */

#ifndef SYS_WINNT
/*
 * alarming - record the occurance of an alarm interrupt
 */
static RETSIGTYPE
alarming(
	int sig
	)
{
	alarm_flag++;
}
#else	/* SYS_WINNT follows */
void CALLBACK
alarming(UINT uTimerID, UINT uMsg, DWORD dwUser, DWORD dw1, DWORD dw2)
{
	UNUSED_ARG(uTimerID); UNUSED_ARG(uMsg); UNUSED_ARG(dwUser);
	UNUSED_ARG(dw1); UNUSED_ARG(dw2);

	alarm_flag++;
}

static void
callTimeEndPeriod(void)
{
	timeEndPeriod( wTimerRes );
	wTimerRes = 0;
}
#endif /* SYS_WINNT */


/*
 * init_alarm - set up the timer interrupt
 */
static void
init_alarm(void)
{
#ifndef SYS_WINNT
# ifdef HAVE_TIMER_CREATE
	struct itimerspec its;
# else
	struct itimerval itv;
# endif
#else	/* SYS_WINNT follows */
	TIMECAPS tc;
	UINT wTimerID;
	HANDLE hToken;
	TOKEN_PRIVILEGES tkp;
	DWORD dwUser = 0;
#endif /* SYS_WINNT */

	alarm_flag = 0;

#ifndef SYS_WINNT
# ifdef HAVE_TIMER_CREATE
	alarm_flag = 0;
	/* this code was put in as setitimer() is non existant this us the
	 * POSIX "equivalents" setup - casey
	 */
	/* ntpdate_timerid is global - so we can kill timer later */
	if (timer_create (CLOCK_REALTIME, NULL, &ntpdate_timerid) ==
#  ifdef SYS_VXWORKS
		ERROR
#  else
		-1
#  endif
		)
	{
		fprintf (stderr, "init_alarm(): timer_create (...) FAILED\n");
		return;
	}

	/*	TIMER_HZ = (5)
	 * Set up the alarm interrupt.	The first comes 1/(2*TIMER_HZ)
	 * seconds from now and they continue on every 1/TIMER_HZ seconds.
	 */
	signal_no_reset(SIGALRM, alarming);
	its.it_interval.tv_sec = 0;
	its.it_value.tv_sec = 0;
	its.it_interval.tv_nsec = 1000000000/TIMER_HZ;
	its.it_value.tv_nsec = 1000000000/(TIMER_HZ<<1);
	timer_settime(ntpdate_timerid, 0 /* !TIMER_ABSTIME */, &its, NULL);
# else	/* !HAVE_TIMER_CREATE follows */
	/*
	 * Set up the alarm interrupt.	The first comes 1/(2*TIMER_HZ)
	 * seconds from now and they continue on every 1/TIMER_HZ seconds.
	 */
	signal_no_reset(SIGALRM, alarming);
	itv.it_interval.tv_sec = 0;
	itv.it_value.tv_sec = 0;
	itv.it_interval.tv_usec = 1000000/TIMER_HZ;
	itv.it_value.tv_usec = 1000000/(TIMER_HZ<<1);

	setitimer(ITIMER_REAL, &itv, NULL);
# endif	/* !HAVE_TIMER_CREATE */
#else	/* SYS_WINNT follows */
	_tzset();

	if (!simple_query && !debug) {
		/*
		 * Get privileges needed for fiddling with the clock
		 */

		/* get the current process token handle */
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
			msyslog(LOG_ERR, "OpenProcessToken failed: %m");
			exit(1);
		}
		/* get the LUID for system-time privilege. */
		LookupPrivilegeValue(NULL, SE_SYSTEMTIME_NAME, &tkp.Privileges[0].Luid);
		tkp.PrivilegeCount = 1;		/* one privilege to set */
		tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		/* get set-time privilege for this process. */
		AdjustTokenPrivileges(hToken, FALSE, &tkp, 0,(PTOKEN_PRIVILEGES) NULL, 0);
		/* cannot test return value of AdjustTokenPrivileges. */
		if (GetLastError() != ERROR_SUCCESS)
			msyslog(LOG_ERR, "AdjustTokenPrivileges failed: %m");
	}

	/*
	 * Set up timer interrupts for every 2**EVENT_TIMEOUT seconds
	 * Under Win/NT, expiry of timer interval leads to invocation
	 * of a callback function (on a different thread) rather than
	 * generating an alarm signal
	 */

	/* determine max and min resolution supported */
	if(timeGetDevCaps(&tc, sizeof(TIMECAPS)) != TIMERR_NOERROR) {
		msyslog(LOG_ERR, "timeGetDevCaps failed: %m");
		exit(1);
	}
	wTimerRes = min(max(tc.wPeriodMin, TARGET_RESOLUTION), tc.wPeriodMax);
	/* establish the minimum timer resolution that we'll use */
	timeBeginPeriod(wTimerRes);
	atexit(callTimeEndPeriod);

	/* start the timer event */
	wTimerID = timeSetEvent(
		(UINT) (1000/TIMER_HZ),		/* Delay */
		wTimerRes,			/* Resolution */
		(LPTIMECALLBACK) alarming,	/* Callback function */
		(DWORD) dwUser,			/* User data */
		TIME_PERIODIC);			/* Event type (periodic) */
	if (wTimerID == 0) {
		msyslog(LOG_ERR, "timeSetEvent failed: %m");
		exit(1);
	}
#endif /* SYS_WINNT */
}




/*
 * We do asynchronous input using the SIGIO facility.  A number of
 * recvbuf buffers are preallocated for input.	In the signal
 * handler we poll to see if the socket is ready and read the
 * packets from it into the recvbuf's along with a time stamp and
 * an indication of the source host and the interface it was received
 * through.  This allows us to get as accurate receive time stamps
 * as possible independent of other processing going on.
 *
 * We allocate a number of recvbufs equal to the number of servers
 * plus 2.	This should be plenty.
 */


/*
 * init_io - initialize I/O data and open socket
 */
static void
init_io(void)
{
	struct addrinfo *res, *ressave;
	struct addrinfo hints;
	sockaddr_u addr;
	char service[5];
	int rc;
	int optval = 1;
	int check_ntp_port_in_use = !debug && !simple_query && !unpriv_port;

	/*
	 * Init buffer free list and stat counters
	 */
	init_recvbuff(sys_numservers + 2);

	/*
	 * Open the socket
	 */

	strlcpy(service, "ntp", sizeof(service));

	/*
	 * Init hints addrinfo structure
	 */
	ZERO(hints);
	hints.ai_family = ai_fam_templ;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_socktype = SOCK_DGRAM;

	if (getaddrinfo(NULL, service, &hints, &res) != 0) {
		msyslog(LOG_ERR, "getaddrinfo() failed: %m");
		exit(1);
		/*NOTREACHED*/
	}

#ifdef SYS_WINNT
	if (check_ntp_port_in_use && ntp_port_inuse(AF_INET, NTP_PORT)){
		msyslog(LOG_ERR, "the NTP socket is in use, exiting: %m");
		exit(1);
	}
#endif

	/* Remember the address of the addrinfo structure chain */
	ressave = res;

	/*
	 * For each structure returned, open and bind socket
	 */
	for(nbsock = 0; (nbsock < MAX_AF) && res ; res = res->ai_next) {
	/* create a datagram (UDP) socket */
		fd[nbsock] = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (fd[nbsock] == SOCKET_ERROR) {
#ifndef SYS_WINNT
		if (errno == EPROTONOSUPPORT || errno == EAFNOSUPPORT ||
		    errno == EPFNOSUPPORT)
#else
		int err = WSAGetLastError();
		if (err == WSAEPROTONOSUPPORT || err == WSAEAFNOSUPPORT ||
		    err == WSAEPFNOSUPPORT)
#endif
			continue;
		msyslog(LOG_ERR, "socket() failed: %m");
		exit(1);
		/*NOTREACHED*/
		}
		/* set socket to reuse address */
		if (setsockopt(fd[nbsock], SOL_SOCKET, SO_REUSEADDR, (void*) &optval, sizeof(optval)) < 0) {
				msyslog(LOG_ERR, "setsockopt() SO_REUSEADDR failed: %m");
				exit(1);
				/*NOTREACHED*/
		}
#ifdef IPV6_V6ONLY
		/* Restricts AF_INET6 socket to IPv6 communications (see RFC 2553bis-03) */
		if (res->ai_family == AF_INET6)
			if (setsockopt(fd[nbsock], IPPROTO_IPV6, IPV6_V6ONLY, (void*) &optval, sizeof(optval)) < 0) {
				   msyslog(LOG_ERR, "setsockopt() IPV6_V6ONLY failed: %m");
					exit(1);
					/*NOTREACHED*/
		}
#endif

		/* Remember the socket family in fd_family structure */
		fd_family[nbsock] = res->ai_family;

		/*
		 * bind the socket to the NTP port
		 */
		if (check_ntp_port_in_use) {
			ZERO(addr);
			INSIST(res->ai_addrlen <= sizeof(addr));
			memcpy(&addr, res->ai_addr, res->ai_addrlen);
			rc = bind(fd[nbsock], &addr.sa, SOCKLEN(&addr));
			if (rc < 0) {
				if (EADDRINUSE == socket_errno())
					msyslog(LOG_ERR, "the NTP socket is in use, exiting");
				else
					msyslog(LOG_ERR, "bind() fails: %m");
				exit(1);
			}
		}

#ifdef HAVE_POLL_H
		fdmask[nbsock].fd = fd[nbsock];
		fdmask[nbsock].events = POLLIN;
#else
		FD_SET(fd[nbsock], &fdmask);
		if (maxfd < fd[nbsock]+1) {
			maxfd = fd[nbsock]+1;
		}
#endif

		/*
		 * set non-blocking,
		 */
#ifndef SYS_WINNT
# ifdef SYS_VXWORKS
		{
			int on = TRUE;

			if (ioctl(fd[nbsock],FIONBIO, &on) == ERROR) {
				msyslog(LOG_ERR, "ioctl(FIONBIO) fails: %m");
				exit(1);
			}
		}
# else /* not SYS_VXWORKS */
#  if defined(O_NONBLOCK)
		if (fcntl(fd[nbsock], F_SETFL, O_NONBLOCK) < 0) {
			msyslog(LOG_ERR, "fcntl(FNDELAY|FASYNC) fails: %m");
			exit(1);
			/*NOTREACHED*/
		}
#  else /* not O_NONBLOCK */
#	if defined(FNDELAY)
		if (fcntl(fd[nbsock], F_SETFL, FNDELAY) < 0) {
			msyslog(LOG_ERR, "fcntl(FNDELAY|FASYNC) fails: %m");
			exit(1);
			/*NOTREACHED*/
		}
#	else /* FNDELAY */
#	 include "Bletch: Need non blocking I/O"
#	endif /* FNDELAY */
#  endif /* not O_NONBLOCK */
# endif /* SYS_VXWORKS */
#else /* SYS_WINNT */
		if (ioctlsocket(fd[nbsock], FIONBIO, (u_long *) &on) == SOCKET_ERROR) {
			msyslog(LOG_ERR, "ioctlsocket(FIONBIO) fails: %m");
			exit(1);
		}
#endif /* SYS_WINNT */
		nbsock++;
	}
	freeaddrinfo(ressave);
}

/*
 * sendpkt - send a packet to the specified destination
 */
static void
sendpkt(
	sockaddr_u *dest,
	struct pkt *pkt,
	int len
	)
{
	int i;
	int cc;
	SOCKET sock = INVALID_SOCKET;

#ifdef SYS_WINNT
	DWORD err;
#endif /* SYS_WINNT */

	/* Find a local family compatible socket to send ntp packet to ntp server */
	for(i = 0; (i < MAX_AF); i++) {
		if(AF(dest) == fd_family[i]) {
			sock = fd[i];
		break;
		}
	}

	if (INVALID_SOCKET == sock) {
		msyslog(LOG_ERR, "cannot find family compatible socket to send ntp packet");
		exit(1);
		/*NOTREACHED*/
	}

	cc = sendto(sock, (char *)pkt, len, 0, (struct sockaddr *)dest,
			SOCKLEN(dest));

	if (SOCKET_ERROR == cc) {
#ifndef SYS_WINNT
		if (errno != EWOULDBLOCK && errno != ENOBUFS)
#else
		err = WSAGetLastError();
		if (err != WSAEWOULDBLOCK && err != WSAENOBUFS)
#endif /* SYS_WINNT */
			msyslog(LOG_ERR, "sendto(%s): %m", stohost(dest));
	}
}


/*
 * input_handler - receive packets asynchronously
 */
void
input_handler(void)
{
	register int n;
	register struct recvbuf *rb;
	struct sock_timeval tvzero;
	GETSOCKNAME_SOCKLEN_TYPE fromlen;
	l_fp ts;
	int i;
#ifdef HAVE_POLL_H
	struct pollfd fds[MAX_AF];
#else
	fd_set fds;
#endif
	SOCKET fdc = 0;

	/*
	 * Do a poll to see if we have data
	 */
	for (;;) {
		tvzero.tv_sec = tvzero.tv_usec = 0;
#ifdef HAVE_POLL_H
		memcpy(fds, fdmask, sizeof(fdmask));
		n = poll(fds, (unsigned int)nbsock, tvzero.tv_sec * 1000);

		/*
		 * Determine which socket received data
		 */

		for(i=0; i < nbsock; i++) {
			if(fds[i].revents & POLLIN) {
				fdc = fd[i];
				break;
			}
		}

#else
		fds = fdmask;
		n = select(maxfd, &fds, NULL, NULL, &tvzero);

		/*
		 * Determine which socket received data
		 */

		for(i=0; i < nbsock; i++) {
			if(FD_ISSET(fd[i], &fds)) {
				 fdc = fd[i];
				 break;
			}
		}

#endif

		/*
		 * If nothing to do, just return.  If an error occurred,
		 * complain and return.  If we've got some, freeze a
		 * timestamp.
		 */
		if (n == 0)
			return;
		else if (n == -1) {
			if (errno != EINTR)
				msyslog(LOG_ERR,
#ifdef HAVE_POLL_H
					"poll() error: %m"
#else
					"select() error: %m"
#endif
					);
			return;
		}
		get_systime(&ts);

		/*
		 * Get a buffer and read the frame.  If we
		 * haven't got a buffer, or this is received
		 * on the wild card socket, just dump the packet.
		 */
		if (initializing || free_recvbuffs() == 0) {
			char buf[100];


#ifndef SYS_WINNT
			(void) read(fdc, buf, sizeof buf);
#else
			/* NT's _read does not operate on nonblocking sockets
			 * either recvfrom or ReadFile() has to be used here.
			 * ReadFile is used in [ntpd]ntp_intres() and ntpdc,
			 * just to be different use recvfrom() here
			 */
			recvfrom(fdc, buf, sizeof(buf), 0, (struct sockaddr *)0, NULL);
#endif /* SYS_WINNT */
			continue;
		}

		rb = get_free_recv_buffer();

		fromlen = sizeof(rb->recv_srcadr);
		rb->recv_length = recvfrom(fdc, (char *)&rb->recv_pkt,
		   sizeof(rb->recv_pkt), 0,
		   (struct sockaddr *)&rb->recv_srcadr, &fromlen);
		if (rb->recv_length == -1) {
			freerecvbuf(rb);
			continue;
		}

		/*
		 * Got one.  Mark how and when it got here,
		 * put it on the full list.
		 */
		rb->recv_time = ts;
		add_full_recv_buffer(rb);
	}
}


/*
 * adj_systime - do a big long slew of the system time
 */
static int
l_adj_systime(
	l_fp *ts
	)
{
	struct timeval adjtv, oadjtv;
	int isneg = 0;
	l_fp offset;
#ifndef STEP_SLEW
	l_fp overshoot;
#endif

	/*
	 * Take the absolute value of the offset
	 */
	offset = *ts;
	if (L_ISNEG(&offset)) {
		isneg = 1;
		L_NEG(&offset);
	}

#ifndef STEP_SLEW
	/*
	 * Calculate the overshoot.  XXX N.B. This code *knows*
	 * ADJ_OVERSHOOT is 1/2.
	 */
	overshoot = offset;
	L_RSHIFTU(&overshoot);
	if (overshoot.l_ui != 0 || (overshoot.l_uf > ADJ_MAXOVERSHOOT)) {
		overshoot.l_ui = 0;
		overshoot.l_uf = ADJ_MAXOVERSHOOT;
	}
	L_ADD(&offset, &overshoot);
#endif
	TSTOTV(&offset, &adjtv);

	if (isneg) {
		adjtv.tv_sec = -adjtv.tv_sec;
		adjtv.tv_usec = -adjtv.tv_usec;
	}

	if (!debug && (adjtv.tv_usec != 0)) {
		/* A time correction needs to be applied. */
#if !defined SYS_WINNT && !defined SYS_CYGWIN32
		/* Slew the time on systems that support this. */
		if (adjtime(&adjtv, &oadjtv) < 0) {
			msyslog(LOG_ERR, "Can't adjust the time of day: %m");
			exit(1);
		}
#else	/* SYS_WINNT or SYS_CYGWIN32 is defined */
		/*
		 * The NT SetSystemTimeAdjustment() call achieves slewing by
		 * changing the clock frequency. This means that we cannot specify
		 * it to slew the clock by a definite amount and then stop like
		 * the Unix adjtime() routine. We can technically adjust the clock
		 * frequency, have ntpdate sleep for a while, and then wake
		 * up and reset the clock frequency, but this might cause some
		 * grief if the user attempts to run ntpd immediately after
		 * ntpdate and the socket is in use.
		 */
		printf("\nSlewing the system time is not supported on Windows. Use the -b option to step the time.\n");
#endif	/* defined SYS_WINNT || defined SYS_CYGWIN32 */
	}
	return 1;
}


/*
 * This fuction is not the same as lib/systime step_systime!!!
 */
static int
l_step_systime(
	l_fp *ts
	)
{
	double dtemp;

#ifdef SLEWALWAYS
#ifdef STEP_SLEW
	l_fp ftmp;
	int isneg;
	int n;

	if (debug)
		return 1;

	/*
	 * Take the absolute value of the offset
	 */
	ftmp = *ts;

	if (L_ISNEG(&ftmp)) {
		L_NEG(&ftmp);
		isneg = 1;
	} else
		isneg = 0;

	if (ftmp.l_ui >= 3) {		/* Step it and slew - we might win */
		LFPTOD(ts, dtemp);
		n = step_systime(dtemp);
		if (n == 0)
			return 0;
		if (isneg)		/* WTF! */
			ts->l_ui = ~0;
		else
			ts->l_ui = ~0;
	}
	/*
	 * Just add adjustment into the current offset.  The update
	 * routine will take care of bringing the system clock into
	 * line.
	 */
#endif
	if (debug)
		return 1;
#ifdef FORCE_NTPDATE_STEP
	LFPTOD(ts, dtemp);
	return step_systime(dtemp);
#else
	l_adj_systime(ts);
	return 1;
#endif
#else /* SLEWALWAYS */
	if (debug)
		return 1;
	LFPTOD(ts, dtemp);
	return step_systime(dtemp);
#endif	/* SLEWALWAYS */
}


/* XXX ELIMINATE print_server similar in ntptrace.c, ntpdate.c */
/*
 * print_server - print detail information for a server
 */
static void
print_server(
	register struct server *pp,
	FILE *fp
	)
{
	register int i;
	char junk[5];
	const char *str;

	if (pp->stratum == 0)		/* Nothing received => nothing to print */
		return;

	if (!debug) {
		(void) fprintf(fp, "server %s, stratum %d, offset %s, delay %s\n",
				   stoa(&pp->srcadr), pp->stratum,
				   lfptoa(&pp->offset, 6), fptoa((s_fp)pp->delay, 5));
		return;
	}

	(void) fprintf(fp, "server %s, port %d\n",
			   stoa(&pp->srcadr), ntohs(((struct sockaddr_in*)&(pp->srcadr))->sin_port));

	(void) fprintf(fp, "stratum %d, precision %d, leap %c%c, trust %03o\n",
			   pp->stratum, pp->precision,
			   pp->leap & 0x2 ? '1' : '0',
			   pp->leap & 0x1 ? '1' : '0',
			   pp->trust);

	if (REFID_ISTEXT(pp->stratum)) {
		str = (char *) &pp->refid;
		for (i=0; i<4 && str[i]; i++) {
			junk[i] = (isprint(str[i]) ? str[i] : '.');
		}
		junk[i] = 0; // force terminating 0
		str = junk;
	} else {
		str = numtoa(pp->refid);
	}
	(void) fprintf(fp,
			"refid [%s], root delay %s, root dispersion %s\n",
			str, fptoa((s_fp)pp->rootdelay, 6),
			ufptoa(pp->rootdisp, 6));

	if (pp->xmtcnt != pp->filter_nextpt)
		(void) fprintf(fp, "transmitted %d, in filter %d\n",
			   pp->xmtcnt, pp->filter_nextpt);

	(void) fprintf(fp, "reference time:      %s\n",
			   prettydate(&pp->reftime));
	(void) fprintf(fp, "originate timestamp: %s\n",
			   prettydate(&pp->org));
	(void) fprintf(fp, "transmit timestamp:  %s\n",
			   prettydate(&pp->xmt));

	if (sys_samples > 1) {
		(void) fprintf(fp, "filter delay: ");
		for (i = 0; i < NTP_SHIFT; i++) {
			if (i == (NTP_SHIFT>>1))
				(void) fprintf(fp, "\n              ");
			(void) fprintf(fp, " %-10.10s", 
				(i<sys_samples ? fptoa(pp->filter_delay[i], 5) : "----"));
		}
		(void) fprintf(fp, "\n");

		(void) fprintf(fp, "filter offset:");
		for (i = 0; i < PEER_SHIFT; i++) {
			if (i == (PEER_SHIFT>>1))
				(void) fprintf(fp, "\n              ");
			(void) fprintf(fp, " %-10.10s", 
				(i<sys_samples ? lfptoa(&pp->filter_offset[i], 6): "----"));
		}
		(void) fprintf(fp, "\n");
	}

	(void) fprintf(fp, "delay %s, dispersion %s, ",
			   fptoa((s_fp)pp->delay, 5), ufptoa(pp->dispersion, 5));

	(void) fprintf(fp, "offset %s\n\n",
			   lfptoa(&pp->offset, 6));
}


#ifdef HAVE_NETINFO
static ni_namelist *
getnetinfoservers(void)
{
	ni_status status;
	void *domain;
	ni_id confdir;
	ni_namelist *namelist = emalloc(sizeof(ni_namelist));

	/* Find a time server in NetInfo */
	if ((status = ni_open(NULL, ".", &domain)) != NI_OK) return NULL;

	while (status = ni_pathsearch(domain, &confdir, NETINFO_CONFIG_DIR) == NI_NODIR) {
		void *next_domain;
		if (ni_open(domain, "..", &next_domain) != NI_OK) break;
		ni_free(domain);
		domain = next_domain;
	}
	if (status != NI_OK) return NULL;

	NI_INIT(namelist);
	if (ni_lookupprop(domain, &confdir, "server", namelist) != NI_OK) {
		ni_namelist_free(namelist);
		free(namelist);
		return NULL;
	}

	return(namelist);
}
#endif

#ifdef SYS_WINNT
isc_boolean_t ntp_port_inuse(int af, u_short port)
{
	/*
	 * Check if NTP socket is already in use on this system
	 * This is only for Windows Systems, as they tend not to fail on the real bind() below
	 */

	SOCKET checksocket;
	struct sockaddr_in checkservice;
	checksocket = socket(af, SOCK_DGRAM, 0);
	if (checksocket == INVALID_SOCKET) {
		return (ISC_TRUE);
	}

	checkservice.sin_family = (short) AF_INET;
	checkservice.sin_addr.s_addr = INADDR_LOOPBACK;
	checkservice.sin_port = htons(port);

	if (bind(checksocket, (struct sockaddr *)&checkservice,
		sizeof(checkservice)) == SOCKET_ERROR) {
		if ( WSAGetLastError() == WSAEADDRINUSE ){
			closesocket(checksocket);
			return (ISC_TRUE);
		}
	}
	closesocket(checksocket);
	return (ISC_FALSE);
}
#endif
