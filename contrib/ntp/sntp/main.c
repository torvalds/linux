#include <config.h>

#include <event2/util.h>
#include <event2/event.h>

#include "ntp_workimpl.h"
#ifdef WORK_THREAD
# include <event2/thread.h>
#endif

#include "main.h"
#include "ntp_libopts.h"
#include "kod_management.h"
#include "networking.h"
#include "utilities.h"
#include "log.h"
#include "libntp.h"


int shutting_down;
int time_derived;
int time_adjusted;
int n_pending_dns = 0;
int n_pending_ntp = 0;
int ai_fam_pref = AF_UNSPEC;
int ntpver = 4;
double steplimit = -1;
SOCKET sock4 = -1;		/* Socket for IPv4 */
SOCKET sock6 = -1;		/* Socket for IPv6 */
/*
** BCAST *must* listen on port 123 (by default), so we can only
** use the UCST sockets (above) if they too are using port 123
*/
SOCKET bsock4 = -1;		/* Broadcast Socket for IPv4 */
SOCKET bsock6 = -1;		/* Broadcast Socket for IPv6 */
struct event_base *base;
struct event *ev_sock4;
struct event *ev_sock6;
struct event *ev_worker_timeout;
struct event *ev_xmt_timer;

struct dns_ctx {
	const char *	name;
	int		flags;
#define CTX_BCST	0x0001
#define CTX_UCST	0x0002
#define CTX_xCST	0x0003
#define CTX_CONC	0x0004
#define CTX_unused	0xfffd
	int		key_id;
	struct timeval	timeout;
	struct key *	key;
};

typedef struct sent_pkt_tag sent_pkt;
struct sent_pkt_tag {
	sent_pkt *		link;
	struct dns_ctx *	dctx;
	sockaddr_u		addr;
	time_t			stime;
	int			done;
	struct pkt		x_pkt;
};

typedef struct xmt_ctx_tag xmt_ctx;
struct xmt_ctx_tag {
	xmt_ctx *		link;
	SOCKET			sock;
	time_t			sched;
	sent_pkt *		spkt;
};

struct timeval	gap;
xmt_ctx *	xmt_q;
struct key *	keys = NULL;
int		response_timeout;
struct timeval	response_tv;
struct timeval	start_tv;
/* check the timeout at least once per second */
struct timeval	wakeup_tv = { 0, 888888 };

sent_pkt *	fam_listheads[2];
#define v4_pkts_list	(fam_listheads[0])
#define v6_pkts_list	(fam_listheads[1])

static union {
	struct pkt pkt;
	char   buf[LEN_PKT_NOMAC + NTP_MAXEXTEN + MAX_MAC_LEN];
} rbuf;

#define r_pkt  rbuf.pkt

#ifdef HAVE_DROPROOT
int droproot;			/* intres imports these */
int root_dropped;
#endif
u_long current_time;		/* libntp/authkeys.c */

void open_sockets(void);
void handle_lookup(const char *name, int flags);
void sntp_addremove_fd(int fd, int is_pipe, int remove_it);
void worker_timeout(evutil_socket_t, short, void *);
void worker_resp_cb(evutil_socket_t, short, void *);
void sntp_name_resolved(int, int, void *, const char *, const char *,
			const struct addrinfo *,
			const struct addrinfo *);
void queue_xmt(SOCKET sock, struct dns_ctx *dctx, sent_pkt *spkt,
	       u_int xmt_delay);
void xmt_timer_cb(evutil_socket_t, short, void *ptr);
void xmt(xmt_ctx *xctx);
int  check_kod(const struct addrinfo *ai);
void timeout_query(sent_pkt *);
void timeout_queries(void);
void sock_cb(evutil_socket_t, short, void *);
void check_exit_conditions(void);
void sntp_libevent_log_cb(int, const char *);
void set_li_vn_mode(struct pkt *spkt, char leap, char version, char mode);
int  set_time(double offset);
void dec_pending_ntp(const char *, sockaddr_u *);
int  libevent_version_ok(void);
int  gettimeofday_cached(struct event_base *b, struct timeval *tv);


/*
 * The actual main function.
 */
int
sntp_main (
	int argc,
	char **argv,
	const char *sntpVersion
	)
{
	int			i;
	int			exitcode;
	int			optct;
	struct event_config *	evcfg;

	/* Initialize logging system - sets up progname */
	sntp_init_logging(argv[0]);

	if (!libevent_version_ok())
		exit(EX_SOFTWARE);

	init_lib();
	init_auth();

	optct = ntpOptionProcess(&sntpOptions, argc, argv);
	argc -= optct;
	argv += optct;


	debug = OPT_VALUE_SET_DEBUG_LEVEL;

	TRACE(2, ("init_lib() done, %s%s\n",
		  (ipv4_works)
		      ? "ipv4_works "
		      : "",
		  (ipv6_works)
		      ? "ipv6_works "
		      : ""));
	ntpver = OPT_VALUE_NTPVERSION;
	steplimit = OPT_VALUE_STEPLIMIT / 1e3;
	gap.tv_usec = max(0, OPT_VALUE_GAP * 1000);
	gap.tv_usec = min(gap.tv_usec, 999999);

	if (HAVE_OPT(LOGFILE))
		open_logfile(OPT_ARG(LOGFILE));

	msyslog(LOG_INFO, "%s", sntpVersion);

	if (0 == argc && !HAVE_OPT(BROADCAST) && !HAVE_OPT(CONCURRENT)) {
		printf("%s: Must supply at least one of -b hostname, -c hostname, or hostname.\n",
		       progname);
		exit(EX_USAGE);
	}


	/*
	** Eventually, we probably want:
	** - separate bcst and ucst timeouts (why?)
	** - multiple --timeout values in the commandline
	*/

	response_timeout = OPT_VALUE_TIMEOUT;
	response_tv.tv_sec = response_timeout;
	response_tv.tv_usec = 0;

	/* IPv6 available? */
	if (isc_net_probeipv6() != ISC_R_SUCCESS) {
		ai_fam_pref = AF_INET;
		TRACE(1, ("No ipv6 support available, forcing ipv4\n"));
	} else {
		/* Check for options -4 and -6 */
		if (HAVE_OPT(IPV4))
			ai_fam_pref = AF_INET;
		else if (HAVE_OPT(IPV6))
			ai_fam_pref = AF_INET6;
	}

	/* TODO: Parse config file if declared */

	/*
	** Init the KOD system.
	** For embedded systems with no writable filesystem,
	** -K /dev/null can be used to disable KoD storage.
	*/
	kod_init_kod_db(OPT_ARG(KOD), FALSE);

	/* HMS: Check and see what happens if KEYFILE doesn't exist */
	auth_init(OPT_ARG(KEYFILE), &keys);

	/*
	** Considering employing a variable that prevents functions of doing
	** anything until everything is initialized properly
	**
	** HMS: What exactly does the above mean?
	*/
	event_set_log_callback(&sntp_libevent_log_cb);
	if (debug > 0)
		event_enable_debug_mode();
#ifdef WORK_THREAD
	evthread_use_pthreads();
	/* we use libevent from main thread only, locks should be academic */
	if (debug > 0)
		evthread_enable_lock_debuging();
#endif
	evcfg = event_config_new();
	if (NULL == evcfg) {
		printf("%s: event_config_new() failed!\n", progname);
		return -1;
	}
#ifndef HAVE_SOCKETPAIR
	event_config_require_features(evcfg, EV_FEATURE_FDS);
#endif
	/* all libevent calls are from main thread */
	/* event_config_set_flag(evcfg, EVENT_BASE_FLAG_NOLOCK); */
	base = event_base_new_with_config(evcfg);
	event_config_free(evcfg);
	if (NULL == base) {
		printf("%s: event_base_new() failed!\n", progname);
		return -1;
	}

	/* wire into intres resolver */
	worker_per_query = TRUE;
	addremove_io_fd = &sntp_addremove_fd;

	open_sockets();

	if (HAVE_OPT(BROADCAST)) {
		int		cn = STACKCT_OPT(  BROADCAST );
		const char **	cp = STACKLST_OPT( BROADCAST );

		while (cn-- > 0) {
			handle_lookup(*cp, CTX_BCST);
			cp++;
		}
	}

	if (HAVE_OPT(CONCURRENT)) {
		int		cn = STACKCT_OPT( CONCURRENT );
		const char **	cp = STACKLST_OPT( CONCURRENT );

		while (cn-- > 0) {
			handle_lookup(*cp, CTX_UCST | CTX_CONC);
			cp++;
		}
	}

	for (i = 0; i < argc; ++i)
		handle_lookup(argv[i], CTX_UCST);

	gettimeofday_cached(base, &start_tv);
	event_base_dispatch(base);
	event_base_free(base);

	if (!time_adjusted &&
	    (ENABLED_OPT(STEP) || ENABLED_OPT(SLEW)))
		exitcode = 1;
	else
		exitcode = 0;

	return exitcode;
}


/*
** open sockets and make them non-blocking
*/
void
open_sockets(
	void
	)
{
	sockaddr_u	name;

	if (-1 == sock4) {
		sock4 = socket(PF_INET, SOCK_DGRAM, 0);
		if (-1 == sock4) {
			/* error getting a socket */
			msyslog(LOG_ERR, "open_sockets: socket(PF_INET) failed: %m");
			exit(1);
		}
		/* Make it non-blocking */
		make_socket_nonblocking(sock4);

		/* Let's try using a wildcard... */
		ZERO(name);
		AF(&name) = AF_INET;
		SET_ADDR4N(&name, INADDR_ANY);
		SET_PORT(&name, (HAVE_OPT(USERESERVEDPORT) ? 123 : 0));

		if (-1 == bind(sock4, &name.sa,
			       SOCKLEN(&name))) {
			msyslog(LOG_ERR, "open_sockets: bind(sock4) failed: %m");
			exit(1);
		}

		/* Register an NTP callback for recv/timeout */
		ev_sock4 = event_new(base, sock4,
				     EV_TIMEOUT | EV_READ | EV_PERSIST,
				     &sock_cb, NULL);
		if (NULL == ev_sock4) {
			msyslog(LOG_ERR,
				"open_sockets: event_new(base, sock4) failed!");
		} else {
			event_add(ev_sock4, &wakeup_tv);
		}
	}

	/* We may not always have IPv6... */
	if (-1 == sock6 && ipv6_works) {
		sock6 = socket(PF_INET6, SOCK_DGRAM, 0);
		if (-1 == sock6 && ipv6_works) {
			/* error getting a socket */
			msyslog(LOG_ERR, "open_sockets: socket(PF_INET6) failed: %m");
			exit(1);
		}
		/* Make it non-blocking */
		make_socket_nonblocking(sock6);

		/* Let's try using a wildcard... */
		ZERO(name);
		AF(&name) = AF_INET6;
		SET_ADDR6N(&name, in6addr_any);
		SET_PORT(&name, (HAVE_OPT(USERESERVEDPORT) ? 123 : 0));

		if (-1 == bind(sock6, &name.sa,
			       SOCKLEN(&name))) {
			msyslog(LOG_ERR, "open_sockets: bind(sock6) failed: %m");
			exit(1);
		}
		/* Register an NTP callback for recv/timeout */
		ev_sock6 = event_new(base, sock6,
				     EV_TIMEOUT | EV_READ | EV_PERSIST,
				     &sock_cb, NULL);
		if (NULL == ev_sock6) {
			msyslog(LOG_ERR,
				"open_sockets: event_new(base, sock6) failed!");
		} else {
			event_add(ev_sock6, &wakeup_tv);
		}
	}
	
	return;
}


/*
** handle_lookup
*/
void
handle_lookup(
	const char *name,
	int flags
	)
{
	struct addrinfo	hints;	/* Local copy is OK */
	struct dns_ctx *ctx;
	char *		name_copy;
	size_t		name_sz;
	size_t		octets;

	TRACE(1, ("handle_lookup(%s,%#x)\n", name, flags));

	ZERO(hints);
	hints.ai_family = ai_fam_pref;
	hints.ai_flags = AI_CANONNAME | Z_AI_NUMERICSERV;
	/*
	** Unless we specify a socktype, we'll get at least two
	** entries for each address: one for TCP and one for
	** UDP. That's not what we want.
	*/
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;

	name_sz = 1 + strlen(name);
	octets = sizeof(*ctx) + name_sz;	// Space for a ctx and the name
	ctx = emalloc_zero(octets);		// ctx at ctx[0]
	name_copy = (char *)(ctx + 1);		// Put the name at ctx[1]
	memcpy(name_copy, name, name_sz);	// copy the name to ctx[1]
	ctx->name = name_copy;			// point to it...
	ctx->flags = flags;
	ctx->timeout = response_tv;
	ctx->key = NULL;

	/* The following should arguably be passed in... */
	if (ENABLED_OPT(AUTHENTICATION)) {
		ctx->key_id = OPT_VALUE_AUTHENTICATION;
		get_key(ctx->key_id, &ctx->key);
		if (NULL == ctx->key) {
			fprintf(stderr, "%s: Authentication with keyID %d requested, but no matching keyID found in <%s>!\n",
				progname, ctx->key_id, OPT_ARG(KEYFILE));
			exit(1);
		}
	} else {
		ctx->key_id = -1;
	}

	++n_pending_dns;
	getaddrinfo_sometime(name, "123", &hints, 0,
			     &sntp_name_resolved, ctx);
}


/*
** DNS Callback:
** - For each IP:
** - - open a socket
** - - increment n_pending_ntp
** - - send a request if this is a Unicast callback
** - - queue wait for response
** - decrement n_pending_dns
*/
void
sntp_name_resolved(
	int			rescode,
	int			gai_errno,
	void *			context,
	const char *		name,
	const char *		service,
	const struct addrinfo *	hints,
	const struct addrinfo *	addr
	)
{
	struct dns_ctx *	dctx;
	sent_pkt *		spkt;
	const struct addrinfo *	ai;
	SOCKET			sock;
	u_int			xmt_delay_v4;
	u_int			xmt_delay_v6;
	u_int			xmt_delay;
	size_t			octets;

	xmt_delay_v4 = 0;
	xmt_delay_v6 = 0;
	dctx = context;
	if (rescode) {
#ifdef EAI_SYSTEM
		if (EAI_SYSTEM == rescode) {
			errno = gai_errno;
			mfprintf(stderr, "%s lookup error %m\n",
				 dctx->name);
		} else
#endif
			fprintf(stderr, "%s lookup error %s\n",
				dctx->name, gai_strerror(rescode));
	} else {
		TRACE(3, ("%s [%s]\n", dctx->name,
			  (addr->ai_canonname != NULL)
			      ? addr->ai_canonname
			      : ""));

		for (ai = addr; ai != NULL; ai = ai->ai_next) {

			if (check_kod(ai))
				continue;

			switch (ai->ai_family) {

			case AF_INET:
				sock = sock4;
				xmt_delay = xmt_delay_v4;
				xmt_delay_v4++;
				break;

			case AF_INET6:
				if (!ipv6_works)
					continue;

				sock = sock6;
				xmt_delay = xmt_delay_v6;
				xmt_delay_v6++;
				break;

			default:
				msyslog(LOG_ERR, "sntp_name_resolved: unexpected ai_family: %d",
					ai->ai_family);
				exit(1);
				break;
			}

			/*
			** We're waiting for a response for either unicast
			** or broadcast, so...
			*/
			++n_pending_ntp;

			/* If this is for a unicast IP, queue a request */
			if (dctx->flags & CTX_UCST) {
				spkt = emalloc_zero(sizeof(*spkt));
				spkt->dctx = dctx;
				octets = min(ai->ai_addrlen, sizeof(spkt->addr));
				memcpy(&spkt->addr, ai->ai_addr, octets);
				queue_xmt(sock, dctx, spkt, xmt_delay);
			}
		}
	}
	/* n_pending_dns really should be >0 here... */
	--n_pending_dns;
	check_exit_conditions();
}


/*
** queue_xmt
*/
void
queue_xmt(
	SOCKET			sock,
	struct dns_ctx *	dctx,
	sent_pkt *		spkt,
	u_int			xmt_delay
	)
{
	sockaddr_u *	dest;
	sent_pkt **	pkt_listp;
	sent_pkt *	match;
	xmt_ctx *	xctx;
	struct timeval	start_cb;
	struct timeval	delay;

	dest = &spkt->addr;
	if (IS_IPV6(dest))
		pkt_listp = &v6_pkts_list;
	else
		pkt_listp = &v4_pkts_list;

	/* reject attempts to add address already listed */
	for (match = *pkt_listp; match != NULL; match = match->link) {
		if (ADDR_PORT_EQ(&spkt->addr, &match->addr)) {
			if (strcasecmp(spkt->dctx->name,
				       match->dctx->name))
				printf("%s %s duplicate address from %s ignored.\n",
				       sptoa(&match->addr),
				       match->dctx->name,
				       spkt->dctx->name);
			else
				printf("%s %s, duplicate address ignored.\n",
				       sptoa(&match->addr),
				       match->dctx->name);
			dec_pending_ntp(spkt->dctx->name, &spkt->addr);
			free(spkt);
			return;
		}
	}

	LINK_SLIST(*pkt_listp, spkt, link);	

	xctx = emalloc_zero(sizeof(*xctx));
	xctx->sock = sock;
	xctx->spkt = spkt;
	gettimeofday_cached(base, &start_cb);
	xctx->sched = start_cb.tv_sec + (2 * xmt_delay);

	LINK_SORT_SLIST(xmt_q, xctx, (xctx->sched < L_S_S_CUR()->sched),
			link, xmt_ctx);
	if (xmt_q == xctx) {
		/*
		 * The new entry is the first scheduled.  The timer is
		 * either not active or is set for the second xmt
		 * context in xmt_q.
		 */
		if (NULL == ev_xmt_timer)
			ev_xmt_timer = event_new(base, INVALID_SOCKET,
						 EV_TIMEOUT,
						 &xmt_timer_cb, NULL);
		if (NULL == ev_xmt_timer) {
			msyslog(LOG_ERR,
				"queue_xmt: event_new(base, -1, EV_TIMEOUT) failed!");
			exit(1);
		}
		ZERO(delay);
		if (xctx->sched > start_cb.tv_sec)
			delay.tv_sec = xctx->sched - start_cb.tv_sec;
		event_add(ev_xmt_timer, &delay);
		TRACE(2, ("queue_xmt: xmt timer for %u usec\n",
			  (u_int)delay.tv_usec));
	}
}


/*
** xmt_timer_cb
*/
void
xmt_timer_cb(
	evutil_socket_t	fd,
	short		what,
	void *		ctx
	)
{
	struct timeval	start_cb;
	struct timeval	delay;
	xmt_ctx *	x;

	UNUSED_ARG(fd);
	UNUSED_ARG(ctx);
	DEBUG_INSIST(EV_TIMEOUT == what);

	if (NULL == xmt_q || shutting_down)
		return;
	gettimeofday_cached(base, &start_cb);
	if (xmt_q->sched <= start_cb.tv_sec) {
		UNLINK_HEAD_SLIST(x, xmt_q, link);
		TRACE(2, ("xmt_timer_cb: at .%6.6u -> %s\n",
			  (u_int)start_cb.tv_usec, stoa(&x->spkt->addr)));
		xmt(x);
		free(x);
		if (NULL == xmt_q)
			return;
	}
	if (xmt_q->sched <= start_cb.tv_sec) {
		event_add(ev_xmt_timer, &gap);
		TRACE(2, ("xmt_timer_cb: at .%6.6u gap %6.6u\n",
			  (u_int)start_cb.tv_usec,
			  (u_int)gap.tv_usec));
	} else {
		delay.tv_sec = xmt_q->sched - start_cb.tv_sec;
		delay.tv_usec = 0;
		event_add(ev_xmt_timer, &delay);
		TRACE(2, ("xmt_timer_cb: at .%6.6u next %ld seconds\n",
			  (u_int)start_cb.tv_usec,
			  (long)delay.tv_sec));
	}
}


/*
** xmt()
*/
void
xmt(
	xmt_ctx *	xctx
	)
{
	SOCKET		sock = xctx->sock;
	struct dns_ctx *dctx = xctx->spkt->dctx;
	sent_pkt *	spkt = xctx->spkt;
	sockaddr_u *	dst = &spkt->addr;
	struct timeval	tv_xmt;
	struct pkt	x_pkt;
	size_t		pkt_len;
	int		sent;

	if (0 != gettimeofday(&tv_xmt, NULL)) {
		msyslog(LOG_ERR,
			"xmt: gettimeofday() failed: %m");
		exit(1);
	}
	tv_xmt.tv_sec += JAN_1970;

	pkt_len = generate_pkt(&x_pkt, &tv_xmt, dctx->key_id,
			       dctx->key);

	sent = sendpkt(sock, dst, &x_pkt, pkt_len);
	if (sent) {
		/* Save the packet we sent... */
		memcpy(&spkt->x_pkt, &x_pkt, min(sizeof(spkt->x_pkt),
		       pkt_len));
		spkt->stime = tv_xmt.tv_sec - JAN_1970;

		TRACE(2, ("xmt: %lx.%6.6u %s %s\n", (u_long)tv_xmt.tv_sec,
			  (u_int)tv_xmt.tv_usec, dctx->name, stoa(dst)));
	} else {
		dec_pending_ntp(dctx->name, dst);
	}

	return;
}


/*
 * timeout_queries() -- give up on unrequited NTP queries
 */
void
timeout_queries(void)
{
	struct timeval	start_cb;
	u_int		idx;
	sent_pkt *	head;
	sent_pkt *	spkt;
	sent_pkt *	spkt_next;
	long		age;
	int didsomething = 0;

	TRACE(3, ("timeout_queries: called to check %u items\n",
		  (unsigned)COUNTOF(fam_listheads)));

	gettimeofday_cached(base, &start_cb);
	for (idx = 0; idx < COUNTOF(fam_listheads); idx++) {
		head = fam_listheads[idx];
		for (spkt = head; spkt != NULL; spkt = spkt_next) {
			char xcst;

			didsomething = 1;
			switch (spkt->dctx->flags & CTX_xCST) {
			    case CTX_BCST:
				xcst = 'B';
				break;

			    case CTX_UCST:
				xcst = 'U';
				break;

			    default:
				INSIST(!"spkt->dctx->flags neither UCST nor BCST");
				break;
			}

			spkt_next = spkt->link;
			if (0 == spkt->stime || spkt->done)
				continue;
			age = start_cb.tv_sec - spkt->stime;
			TRACE(3, ("%s %s %cCST age %ld\n",
				  stoa(&spkt->addr),
				  spkt->dctx->name, xcst, age));
			if (age > response_timeout)
				timeout_query(spkt);
		}
	}
	// Do we care about didsomething?
	TRACE(3, ("timeout_queries: didsomething is %d, age is %ld\n",
		  didsomething, (long) (start_cb.tv_sec - start_tv.tv_sec)));
	if (start_cb.tv_sec - start_tv.tv_sec > response_timeout) {
		TRACE(3, ("timeout_queries: bail!\n"));
		event_base_loopexit(base, NULL);
		shutting_down = TRUE;
	}
}


void dec_pending_ntp(
	const char *	name,
	sockaddr_u *	server
	)
{
	if (n_pending_ntp > 0) {
		--n_pending_ntp;
		check_exit_conditions();
	} else {
		INSIST(0 == n_pending_ntp);
		TRACE(1, ("n_pending_ntp was zero before decrement for %s\n",
			  hostnameaddr(name, server)));
	}
}


void timeout_query(
	sent_pkt *	spkt
	)
{
	sockaddr_u *	server;
	char		xcst;


	switch (spkt->dctx->flags & CTX_xCST) {
	    case CTX_BCST:
		xcst = 'B';
		break;

	    case CTX_UCST:
		xcst = 'U';
		break;

	    default:
		INSIST(!"spkt->dctx->flags neither UCST nor BCST");
		break;
	}
	spkt->done = TRUE;
	server = &spkt->addr;
	msyslog(LOG_INFO, "%s no %cCST response after %d seconds",
		hostnameaddr(spkt->dctx->name, server), xcst,
		response_timeout);
	dec_pending_ntp(spkt->dctx->name, server);
	return;
}


/*
** check_kod
*/
int
check_kod(
	const struct addrinfo *	ai
	)
{
	char *hostname;
	struct kod_entry *reason;

	/* Is there a KoD on file for this address? */
	hostname = addrinfo_to_str(ai);
	TRACE(2, ("check_kod: checking <%s>\n", hostname));
	if (search_entry(hostname, &reason)) {
		printf("prior KoD for %s, skipping.\n",
			hostname);
		free(reason);
		free(hostname);

		return 1;
	}
	free(hostname);

	return 0;
}


/*
** Socket readable/timeout Callback:
** Read in the packet
** Unicast:
** - close socket
** - decrement n_pending_ntp
** - If packet is good, set the time and "exit"
** Broadcast:
** - If packet is good, set the time and "exit"
*/
void
sock_cb(
	evutil_socket_t fd,
	short what,
	void *ptr
	)
{
	sockaddr_u	sender;
	sockaddr_u *	psau;
	sent_pkt **	p_pktlist;
	sent_pkt *	spkt;
	int		rpktl;
	int		rc;

	INSIST(sock4 == fd || sock6 == fd);

	TRACE(3, ("sock_cb: event on sock%s:%s%s%s%s\n",
		  (fd == sock6)
		      ? "6"
		      : "4",
		  (what & EV_TIMEOUT) ? " timeout" : "",
		  (what & EV_READ)    ? " read" : "",
		  (what & EV_WRITE)   ? " write" : "",
		  (what & EV_SIGNAL)  ? " signal" : ""));

	if (!(EV_READ & what)) {
		if (EV_TIMEOUT & what)
			timeout_queries();

		return;
	}

	/* Read in the packet */
	rpktl = recvdata(fd, &sender, &rbuf, sizeof(rbuf));
	if (rpktl < 0) {
		msyslog(LOG_DEBUG, "recvfrom error %m");
		return;
	}

	if (sock6 == fd)
		p_pktlist = &v6_pkts_list;
	else
		p_pktlist = &v4_pkts_list;

	for (spkt = *p_pktlist; spkt != NULL; spkt = spkt->link) {
		psau = &spkt->addr;
		if (SOCK_EQ(&sender, psau))
			break;
	}
	if (NULL == spkt) {
		msyslog(LOG_WARNING,
			"Packet from unexpected source %s dropped",
			sptoa(&sender));
		return;
	}

	TRACE(1, ("sock_cb: %s %s\n", spkt->dctx->name,
		  sptoa(&sender)));

	rpktl = process_pkt(&r_pkt, &sender, rpktl, MODE_SERVER,
			    &spkt->x_pkt, "sock_cb");

	TRACE(2, ("sock_cb: process_pkt returned %d\n", rpktl));

	/* If this is a Unicast packet, one down ... */
	if (!spkt->done && (CTX_UCST & spkt->dctx->flags)) {
		dec_pending_ntp(spkt->dctx->name, &spkt->addr);
		spkt->done = TRUE;
	}


	/* If the packet is good, set the time and we're all done */
	rc = handle_pkt(rpktl, &r_pkt, &spkt->addr, spkt->dctx->name);
	if (0 != rc)
		TRACE(1, ("sock_cb: handle_pkt() returned %d\n", rc));
	check_exit_conditions();
}


/*
 * check_exit_conditions()
 *
 * If sntp has a reply, ask the event loop to stop after this round of
 * callbacks, unless --wait was used.
 */
void
check_exit_conditions(void)
{
	if ((0 == n_pending_ntp && 0 == n_pending_dns) ||
	    (time_derived && !HAVE_OPT(WAIT))) {
		event_base_loopexit(base, NULL);
		shutting_down = TRUE;
	} else {
		TRACE(2, ("%d NTP and %d name queries pending\n",
			  n_pending_ntp, n_pending_dns));
	}
}


/*
 * sntp_addremove_fd() is invoked by the intres blocking worker code
 * to read from a pipe, or to stop same.
 */
void sntp_addremove_fd(
	int	fd,
	int	is_pipe,
	int	remove_it
	)
{
	u_int		idx;
	blocking_child *c;
	struct event *	ev;

#ifdef HAVE_SOCKETPAIR
	if (is_pipe) {
		/* sntp only asks for EV_FEATURE_FDS without HAVE_SOCKETPAIR */
		msyslog(LOG_ERR, "fatal: pipes not supported on systems with socketpair()");
		exit(1);
	}
#endif

	c = NULL;
	for (idx = 0; idx < blocking_children_alloc; idx++) {
		c = blocking_children[idx];
		if (NULL == c)
			continue;
		if (fd == c->resp_read_pipe)
			break;
	}
	if (idx == blocking_children_alloc)
		return;

	if (remove_it) {
		ev = c->resp_read_ctx;
		c->resp_read_ctx = NULL;
		event_del(ev);
		event_free(ev);

		return;
	}

	ev = event_new(base, fd, EV_READ | EV_PERSIST,
		       &worker_resp_cb, c);
	if (NULL == ev) {
		msyslog(LOG_ERR,
			"sntp_addremove_fd: event_new(base, fd) failed!");
		return;
	}
	c->resp_read_ctx = ev;
	event_add(ev, NULL);
}


/* called by forked intres child to close open descriptors */
#ifdef WORK_FORK
void
kill_asyncio(
	int	startfd
	)
{
	if (INVALID_SOCKET != sock4) {
		closesocket(sock4);
		sock4 = INVALID_SOCKET;
	}
	if (INVALID_SOCKET != sock6) {
		closesocket(sock6);
		sock6 = INVALID_SOCKET;
	}
	if (INVALID_SOCKET != bsock4) {
		closesocket(sock4);
		sock4 = INVALID_SOCKET;
	}
	if (INVALID_SOCKET != bsock6) {
		closesocket(sock6);
		sock6 = INVALID_SOCKET;
	}
}
#endif


/*
 * worker_resp_cb() is invoked when resp_read_pipe is readable.
 */
void
worker_resp_cb(
	evutil_socket_t	fd,
	short		what,
	void *		ctx	/* blocking_child * */
	)
{
	blocking_child *	c;

	DEBUG_INSIST(EV_READ & what);
	c = ctx;
	DEBUG_INSIST(fd == c->resp_read_pipe);
	process_blocking_resp(c);
}


/*
 * intres_timeout_req(s) is invoked in the parent to schedule an idle
 * timeout to fire in s seconds, if not reset earlier by a call to
 * intres_timeout_req(0), which clears any pending timeout.  When the
 * timeout expires, worker_idle_timer_fired() is invoked (again, in the
 * parent).
 *
 * sntp and ntpd each provide implementations adapted to their timers.
 */
void
intres_timeout_req(
	u_int	seconds		/* 0 cancels */
	)
{
	struct timeval	tv_to;

	if (NULL == ev_worker_timeout) {
		ev_worker_timeout = event_new(base, -1,
					      EV_TIMEOUT | EV_PERSIST,
					      &worker_timeout, NULL);
		DEBUG_INSIST(NULL != ev_worker_timeout);
	} else {
		event_del(ev_worker_timeout);
	}
	if (0 == seconds)
		return;
	tv_to.tv_sec = seconds;
	tv_to.tv_usec = 0;
	event_add(ev_worker_timeout, &tv_to);
}


void
worker_timeout(
	evutil_socket_t	fd,
	short		what,
	void *		ctx
	)
{
	UNUSED_ARG(fd);
	UNUSED_ARG(ctx);

	DEBUG_REQUIRE(EV_TIMEOUT & what);
	worker_idle_timer_fired();
}


void
sntp_libevent_log_cb(
	int		severity,
	const char *	msg
	)
{
	int		level;

	switch (severity) {

	default:
	case _EVENT_LOG_DEBUG:
		level = LOG_DEBUG;
		break;

	case _EVENT_LOG_MSG:
		level = LOG_NOTICE;
		break;

	case _EVENT_LOG_WARN:
		level = LOG_WARNING;
		break;

	case _EVENT_LOG_ERR:
		level = LOG_ERR;
		break;
	}

	msyslog(level, "%s", msg);
}


int
generate_pkt (
	struct pkt *x_pkt,
	const struct timeval *tv_xmt,
	int key_id,
	struct key *pkt_key
	)
{
	l_fp	xmt_fp;
	int	pkt_len;
	int	mac_size;

	pkt_len = LEN_PKT_NOMAC;
	ZERO(*x_pkt);
	TVTOTS(tv_xmt, &xmt_fp);
	HTONL_FP(&xmt_fp, &x_pkt->xmt);
	x_pkt->stratum = STRATUM_TO_PKT(STRATUM_UNSPEC);
	x_pkt->ppoll = 8;
	/* FIXME! Modus broadcast + adr. check -> bdr. pkt */
	set_li_vn_mode(x_pkt, LEAP_NOTINSYNC, ntpver, 3);
	if (debug > 0) {
		printf("generate_pkt: key_id %d, key pointer %p\n", key_id, pkt_key);
	}
	if (pkt_key != NULL) {
		x_pkt->exten[0] = htonl(key_id);
		mac_size = make_mac(x_pkt, pkt_len, MAX_MDG_LEN,
				    pkt_key, (char *)&x_pkt->exten[1]);
		if (mac_size > 0)
			pkt_len += mac_size + KEY_MAC_LEN;
#ifdef DEBUG
		if (debug > 0) {
			printf("generate_pkt: mac_size is %d\n", mac_size);
		}
#endif

	}
	return pkt_len;
}


int
handle_pkt(
	int		rpktl,
	struct pkt *	rpkt,
	sockaddr_u *	host,
	const char *	hostname
	)
{
	char		disptxt[32];
	const char *	addrtxt;
	struct timeval	tv_dst;
	int		cnt;
	int		sw_case;
	int		digits;
	int		stratum;
	char *		ref;
	char *		ts_str;
	const char *	leaptxt;
	double		offset;
	double		precision;
	double		synch_distance;
	char *		p_SNTP_PRETEND_TIME;
	time_t		pretend_time;
#if SIZEOF_TIME_T == 8
	long long	ll;
#else
	long		l;
#endif

	ts_str = NULL;

	if (rpktl > 0)
		sw_case = 1;
	else
		sw_case = rpktl;

	switch (sw_case) {

	case SERVER_UNUSEABLE:
		return -1;
		break;

	case PACKET_UNUSEABLE:
		break;

	case SERVER_AUTH_FAIL:
		break;

	case KOD_DEMOBILIZE:
		/* Received a DENY or RESTR KOD packet */
		addrtxt = stoa(host);
		ref = (char *)&rpkt->refid;
		add_entry(addrtxt, ref);
		msyslog(LOG_WARNING, "KOD code %c%c%c%c from %s %s",
			ref[0], ref[1], ref[2], ref[3], addrtxt, hostname);
		break;

	case KOD_RATE:
		/*
		** Hmm...
		** We should probably call add_entry() with an
		** expiration timestamp of several seconds in the future,
		** and back-off even more if we get more RATE responses.
		*/
		break;

	case 1:
		TRACE(3, ("handle_pkt: %d bytes from %s %s\n",
			  rpktl, stoa(host), hostname));

		gettimeofday_cached(base, &tv_dst);

		p_SNTP_PRETEND_TIME = getenv("SNTP_PRETEND_TIME");
		if (p_SNTP_PRETEND_TIME) {
			pretend_time = 0;
#if SIZEOF_TIME_T == 4
			if (1 == sscanf(p_SNTP_PRETEND_TIME, "%ld", &l))
				pretend_time = (time_t)l;
#elif SIZEOF_TIME_T == 8
			if (1 == sscanf(p_SNTP_PRETEND_TIME, "%lld", &ll))
				pretend_time = (time_t)ll;
#else
# include "GRONK: unexpected value for SIZEOF_TIME_T"
#endif
			if (0 != pretend_time)
				tv_dst.tv_sec = pretend_time;
		}

		offset_calculation(rpkt, rpktl, &tv_dst, &offset,
				   &precision, &synch_distance);
		time_derived = TRUE;

		for (digits = 0; (precision *= 10.) < 1.; ++digits)
			/* empty */ ;
		if (digits > 6)
			digits = 6;

		ts_str = tv_to_str(&tv_dst);
		stratum = rpkt->stratum;
		if (0 == stratum)
				stratum = 16;

		if (synch_distance > 0.) {
			cnt = snprintf(disptxt, sizeof(disptxt),
				       " +/- %f", synch_distance);
			if ((size_t)cnt >= sizeof(disptxt))
				snprintf(disptxt, sizeof(disptxt),
					 "ERROR %d >= %d", cnt,
					 (int)sizeof(disptxt));
		} else {
			disptxt[0] = '\0';
		}

		switch (PKT_LEAP(rpkt->li_vn_mode)) {
		    case LEAP_NOWARNING:
		    	leaptxt = "no-leap";
			break;
		    case LEAP_ADDSECOND:
		    	leaptxt = "add-leap";
			break;
		    case LEAP_DELSECOND:
		    	leaptxt = "del-leap";
			break;
		    case LEAP_NOTINSYNC:
		    	leaptxt = "unsync";
			break;
		    default:
		    	leaptxt = "LEAP-ERROR";
			break;
		}

		msyslog(LOG_INFO, "%s %+.*f%s %s s%d %s%s", ts_str,
			digits, offset, disptxt,
			hostnameaddr(hostname, host), stratum,
			leaptxt,
			(time_adjusted)
			    ? " [excess]"
			    : "");
		free(ts_str);

		if (p_SNTP_PRETEND_TIME)
			return 0;

		if (!time_adjusted &&
		    (ENABLED_OPT(STEP) || ENABLED_OPT(SLEW)))
			return set_time(offset);

		return EX_OK;
	}

	return 1;
}


void
offset_calculation(
	struct pkt *rpkt,
	int rpktl,
	struct timeval *tv_dst,
	double *offset,
	double *precision,
	double *synch_distance
	)
{
	l_fp p_rec, p_xmt, p_ref, p_org, tmp, dst;
	u_fp p_rdly, p_rdsp;
	double t21, t34, delta;

	/* Convert timestamps from network to host byte order */
	p_rdly = NTOHS_FP(rpkt->rootdelay);
	p_rdsp = NTOHS_FP(rpkt->rootdisp);
	NTOHL_FP(&rpkt->reftime, &p_ref);
	NTOHL_FP(&rpkt->org, &p_org);
	NTOHL_FP(&rpkt->rec, &p_rec);
	NTOHL_FP(&rpkt->xmt, &p_xmt);

	*precision = LOGTOD(rpkt->precision);

	TRACE(3, ("offset_calculation: LOGTOD(rpkt->precision): %f\n", *precision));

	/* Compute offset etc. */
	tmp = p_rec;
	L_SUB(&tmp, &p_org);
	LFPTOD(&tmp, t21);
	TVTOTS(tv_dst, &dst);
	dst.l_ui += JAN_1970;
	tmp = p_xmt;
	L_SUB(&tmp, &dst);
	LFPTOD(&tmp, t34);
	*offset = (t21 + t34) / 2.;
	delta = t21 - t34;

	// synch_distance is:
	// (peer->delay + peer->rootdelay) / 2 + peer->disp
	// + peer->rootdisp + clock_phi * (current_time - peer->update)
	// + peer->jitter;
	//
	// and peer->delay = fabs(peer->offset - p_offset) * 2;
	// and peer->offset needs history, so we're left with
	// p_offset = (t21 + t34) / 2.;
	// peer->disp = 0; (we have no history to augment this)
	// clock_phi = 15e-6; 
	// peer->jitter = LOGTOD(sys_precision); (we have no history to augment this)
	// and ntp_proto.c:set_sys_tick_precision() should get us sys_precision.
	//
	// so our answer seems to be:
	//
	// (fabs(t21 + t34) + peer->rootdelay) / 3.
	// + 0 (peer->disp)
	// + peer->rootdisp
	// + 15e-6 (clock_phi)
	// + LOGTOD(sys_precision)

	INSIST( FPTOD(p_rdly) >= 0. );
#if 1
	*synch_distance = (fabs(t21 + t34) + FPTOD(p_rdly)) / 3.
		+ 0.
		+ FPTOD(p_rdsp)
		+ 15e-6
		+ 0.	/* LOGTOD(sys_precision) when we can get it */
		;
	INSIST( *synch_distance >= 0. );
#else
	*synch_distance = (FPTOD(p_rdly) + FPTOD(p_rdsp))/2.0;
#endif

#ifdef DEBUG
	if (debug > 3) {
		printf("sntp rootdelay: %f\n", FPTOD(p_rdly));
		printf("sntp rootdisp: %f\n", FPTOD(p_rdsp));
		printf("sntp syncdist: %f\n", *synch_distance);

		pkt_output(rpkt, rpktl, stdout);

		printf("sntp offset_calculation: rpkt->reftime:\n");
		l_fp_output(&p_ref, stdout);
		printf("sntp offset_calculation: rpkt->org:\n");
		l_fp_output(&p_org, stdout);
		printf("sntp offset_calculation: rpkt->rec:\n");
		l_fp_output(&p_rec, stdout);
		printf("sntp offset_calculation: rpkt->xmt:\n");
		l_fp_output(&p_xmt, stdout);
	}
#endif

	TRACE(3, ("sntp offset_calculation:\trec - org t21: %.6f\n"
		  "\txmt - dst t34: %.6f\tdelta: %.6f\toffset: %.6f\n",
		  t21, t34, delta, *offset));

	return;
}



/* Compute the 8 bits for li_vn_mode */
void
set_li_vn_mode (
	struct pkt *spkt,
	char leap,
	char version,
	char mode
	)
{
	if (leap > 3) {
		msyslog(LOG_DEBUG, "set_li_vn_mode: leap > 3, using max. 3");
		leap = 3;
	}

	if ((unsigned char)version > 7) {
		msyslog(LOG_DEBUG, "set_li_vn_mode: version < 0 or > 7, using 4");
		version = 4;
	}

	if (mode > 7) {
		msyslog(LOG_DEBUG, "set_li_vn_mode: mode > 7, using client mode 3");
		mode = 3;
	}

	spkt->li_vn_mode  = leap << 6;
	spkt->li_vn_mode |= version << 3;
	spkt->li_vn_mode |= mode;
}


/*
** set_time applies 'offset' to the local clock.
*/
int
set_time(
	double offset
	)
{
	int rc;

	if (time_adjusted)
		return EX_OK;

	/*
	** If we can step but we cannot slew, then step.
	** If we can step or slew and and |offset| > steplimit, then step.
	*/
	if (ENABLED_OPT(STEP) &&
	    (   !ENABLED_OPT(SLEW)
	     || (ENABLED_OPT(SLEW) && (fabs(offset) > steplimit))
	    )) {
		rc = step_systime(offset);

		/* If there was a problem, can we rely on errno? */
		if (1 == rc)
			time_adjusted = TRUE;
		return (time_adjusted)
			   ? EX_OK 
			   : 1;
		/*
		** In case of error, what should we use?
		** EX_UNAVAILABLE?
		** EX_OSERR?
		** EX_NOPERM?
		*/
	}

	if (ENABLED_OPT(SLEW)) {
		rc = adj_systime(offset);

		/* If there was a problem, can we rely on errno? */
		if (1 == rc)
			time_adjusted = TRUE;
		return (time_adjusted)
			   ? EX_OK 
			   : 1;
		/*
		** In case of error, what should we use?
		** EX_UNAVAILABLE?
		** EX_OSERR?
		** EX_NOPERM?
		*/
	}

	return EX_SOFTWARE;
}


int
libevent_version_ok(void)
{
	ev_uint32_t v_compile_maj;
	ev_uint32_t v_run_maj;

	v_compile_maj = LIBEVENT_VERSION_NUMBER & 0xffff0000;
	v_run_maj = event_get_version_number() & 0xffff0000;
	if (v_compile_maj != v_run_maj) {
		fprintf(stderr,
			"Incompatible libevent versions: have %s, built with %s\n",
			event_get_version(),
			LIBEVENT_VERSION);
		return 0;
	}
	return 1;
}

/*
 * gettimeofday_cached()
 *
 * Clones the event_base_gettimeofday_cached() interface but ensures the
 * times are always on the gettimeofday() 1970 scale.  Older libevent 2
 * sometimes used gettimeofday(), sometimes the since-system-start
 * clock_gettime(CLOCK_MONOTONIC), depending on the platform.
 *
 * It is not cleanly possible to tell which timescale older libevent is
 * using.
 *
 * The strategy involves 1 hour thresholds chosen to be far longer than
 * the duration of a round of libevent callbacks, which share a cached
 * start-of-round time.  First compare the last cached time with the
 * current gettimeofday() time.  If they are within one hour, libevent
 * is using the proper timescale so leave the offset 0.  Otherwise,
 * compare libevent's cached time and the current time on the monotonic
 * scale.  If they are within an hour, libevent is using the monotonic
 * scale so calculate the offset to add to such times to bring them to
 * gettimeofday()'s scale.
 */
int
gettimeofday_cached(
	struct event_base *	b,
	struct timeval *	caller_tv
	)
{
#if defined(_EVENT_HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
	static struct event_base *	cached_b;
	static struct timeval		cached;
	static struct timeval		adj_cached;
	static struct timeval		offset;
	static int			offset_ready;
	struct timeval			latest;
	struct timeval			systemt;
	struct timespec			ts;
	struct timeval			mono;
	struct timeval			diff;
	int				cgt_rc;
	int				gtod_rc;

	event_base_gettimeofday_cached(b, &latest);
	if (b == cached_b &&
	    !memcmp(&latest, &cached, sizeof(latest))) {
		*caller_tv = adj_cached;
		return 0;
	}
	cached = latest;
	cached_b = b;
	if (!offset_ready) {
		cgt_rc = clock_gettime(CLOCK_MONOTONIC, &ts);
		gtod_rc = gettimeofday(&systemt, NULL);
		if (0 != gtod_rc) {
			msyslog(LOG_ERR,
				"%s: gettimeofday() error %m",
				progname);
			exit(1);
		}
		diff = sub_tval(systemt, latest);
		if (debug > 1)
			printf("system minus cached %+ld.%06ld\n",
			       (long)diff.tv_sec, (long)diff.tv_usec);
		if (0 != cgt_rc || labs((long)diff.tv_sec) < 3600) {
			/*
			 * Either use_monotonic == 0, or this libevent
			 * has been repaired.  Leave offset at zero.
			 */
		} else {
			mono.tv_sec = ts.tv_sec;
			mono.tv_usec = ts.tv_nsec / 1000;
			diff = sub_tval(latest, mono);
			if (debug > 1)
				printf("cached minus monotonic %+ld.%06ld\n",
				       (long)diff.tv_sec, (long)diff.tv_usec);
			if (labs((long)diff.tv_sec) < 3600) {
				/* older libevent2 using monotonic */
				offset = sub_tval(systemt, mono);
				TRACE(1, ("%s: Offsetting libevent CLOCK_MONOTONIC times  by %+ld.%06ld\n",
					 "gettimeofday_cached",
					 (long)offset.tv_sec,
					 (long)offset.tv_usec));
			}
		}
		offset_ready = TRUE;
	}
	adj_cached = add_tval(cached, offset);
	*caller_tv = adj_cached;

	return 0;
#else
	return event_base_gettimeofday_cached(b, caller_tv);
#endif
}

/* Dummy function to satisfy libntp/work_fork.c */
extern int set_user_group_ids(void);
int set_user_group_ids(void)
{
    return 1;
}
