/*
 * ntp_proto.c - NTP version 4 protocol machinery
 *
 * ATTENTION: Get approval from Harlan on all changes to this file!
 *	    (Harlan will be discussing these changes with Dave Mills.)
 *
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "ntpd.h"
#include "ntp_stdlib.h"
#include "ntp_unixtime.h"
#include "ntp_control.h"
#include "ntp_string.h"
#include "ntp_leapsec.h"
#include "refidsmear.h"
#include "lib_strbuf.h"

#include <stdio.h>
#ifdef HAVE_LIBSCF_H
#include <libscf.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* [Bug 3031] define automatic broadcastdelay cutoff preset */
#ifndef BDELAY_DEFAULT
# define BDELAY_DEFAULT (-0.050)
#endif

/*
 * This macro defines the authentication state. If x is 1 authentication
 * is required; otherwise it is optional.
 */
#define	AUTH(x, y)	((x) ? (y) == AUTH_OK \
			     : (y) == AUTH_OK || (y) == AUTH_NONE)

typedef enum
auth_state {
	AUTH_UNKNOWN = -1,	/* Unknown */
	AUTH_NONE,		/* authentication not required */
	AUTH_OK,		/* authentication OK */
	AUTH_ERROR,		/* authentication error */
	AUTH_CRYPTO		/* crypto_NAK */
} auth_code;

/*
 * Set up Kiss Code values
 */

typedef enum
kiss_codes {
	NOKISS,				/* No Kiss Code */
	RATEKISS,			/* Rate limit Kiss Code */
	DENYKISS,			/* Deny Kiss */
	RSTRKISS,			/* Restricted Kiss */
	XKISS				/* Experimental Kiss */
} kiss_code;

typedef enum
nak_error_codes {
	NONAK,				/* No NAK seen */
	INVALIDNAK,			/* NAK cannot be used */
	VALIDNAK			/* NAK is valid */
} nak_code;

/*
 * traffic shaping parameters
 */
#define	NTP_IBURST	6	/* packets in iburst */
#define	RESP_DELAY	1	/* refclock burst delay (s) */

/*
 * pool soliciting restriction duration (s)
 */
#define	POOL_SOLICIT_WINDOW	8

/*
 * peer_select groups statistics for a peer used by clock_select() and
 * clock_cluster().
 */
typedef struct peer_select_tag {
	struct peer *	peer;
	double		synch;	/* sync distance */
	double		error;	/* jitter */
	double		seljit;	/* selection jitter */
} peer_select;

/*
 * System variables are declared here. Unless specified otherwise, all
 * times are in seconds.
 */
u_char	sys_leap;		/* system leap indicator, use set_sys_leap() to change this */
u_char	xmt_leap;		/* leap indicator sent in client requests, set up by set_sys_leap() */
u_char	sys_stratum;		/* system stratum */
s_char	sys_precision;		/* local clock precision (log2 s) */
double	sys_rootdelay;		/* roundtrip delay to primary source */
double	sys_rootdisp;		/* dispersion to primary source */
u_int32 sys_refid;		/* reference id (network byte order) */
l_fp	sys_reftime;		/* last update time */
struct	peer *sys_peer;		/* current peer */

#ifdef LEAP_SMEAR
struct leap_smear_info leap_smear;
#endif
int leap_sec_in_progress;

/*
 * Rate controls. Leaky buckets are used to throttle the packet
 * transmission rates in order to protect busy servers such as at NIST
 * and USNO. There is a counter for each association and another for KoD
 * packets. The association counter decrements each second, but not
 * below zero. Each time a packet is sent the counter is incremented by
 * a configurable value representing the average interval between
 * packets. A packet is delayed as long as the counter is greater than
 * zero. Note this does not affect the time value computations.
 */
/*
 * Nonspecified system state variables
 */
int	sys_bclient;		/* broadcast client enable */
double	sys_bdelay;		/* broadcast client default delay */
int	sys_authenticate;	/* requre authentication for config */
l_fp	sys_authdelay;		/* authentication delay */
double	sys_offset;	/* current local clock offset */
double	sys_mindisp = MINDISPERSE; /* minimum distance (s) */
double	sys_maxdist = MAXDISTANCE; /* selection threshold */
double	sys_jitter;		/* system jitter */
u_long	sys_epoch;		/* last clock update time */
static	double sys_clockhop;	/* clockhop threshold */
static int leap_vote_ins;	/* leap consensus for insert */
static int leap_vote_del;	/* leap consensus for delete */
keyid_t	sys_private;		/* private value for session seed */
int	sys_manycastserver;	/* respond to manycast client pkts */
int	ntp_mode7;		/* respond to ntpdc (mode7) */
int	peer_ntpdate;		/* active peers in ntpdate mode */
int	sys_survivors;		/* truest of the truechimers */
char	*sys_ident = NULL;	/* identity scheme */

/*
 * TOS and multicast mapping stuff
 */
int	sys_floor = 0;		/* cluster stratum floor */
u_char	sys_bcpollbstep = 0;	/* Broadcast Poll backstep gate */
int	sys_ceiling = STRATUM_UNSPEC - 1; /* cluster stratum ceiling */
int	sys_minsane = 1;	/* minimum candidates */
int	sys_minclock = NTP_MINCLOCK; /* minimum candidates */
int	sys_maxclock = NTP_MAXCLOCK; /* maximum candidates */
int	sys_cohort = 0;		/* cohort switch */
int	sys_orphan = STRATUM_UNSPEC + 1; /* orphan stratum */
int	sys_orphwait = NTP_ORPHWAIT; /* orphan wait */
int	sys_beacon = BEACON;	/* manycast beacon interval */
u_int	sys_ttlmax;		/* max ttl mapping vector index */
u_char	sys_ttl[MAX_TTL];	/* ttl mapping vector */

/*
 * Statistics counters - first the good, then the bad
 */
u_long	sys_stattime;		/* elapsed time */
u_long	sys_received;		/* packets received */
u_long	sys_processed;		/* packets for this host */
u_long	sys_newversion;		/* current version */
u_long	sys_oldversion;		/* old version */
u_long	sys_restricted;		/* access denied */
u_long	sys_badlength;		/* bad length or format */
u_long	sys_badauth;		/* bad authentication */
u_long	sys_declined;		/* declined */
u_long	sys_limitrejected;	/* rate exceeded */
u_long	sys_kodsent;		/* KoD sent */

/*
 * Mechanism knobs: how soon do we peer_clear() or unpeer()?
 *
 * The default way is "on-receipt".  If this was a packet from a
 * well-behaved source, on-receipt will offer the fastest recovery.
 * If this was from a DoS attack, the default way makes it easier
 * for a bad-guy to DoS us.  So look and see what bites you harder
 * and choose according to your environment.
 */
int peer_clear_digest_early	= 1;	/* bad digest (TEST5) and Autokey */
int unpeer_crypto_early		= 1;	/* bad crypto (TEST9) */
int unpeer_crypto_nak_early	= 1;	/* crypto_NAK (TEST5) */
int unpeer_digest_early		= 1;	/* bad digest (TEST5) */

int dynamic_interleave = DYNAMIC_INTERLEAVE;	/* Bug 2978 mitigation */

int kiss_code_check(u_char hisleap, u_char hisstratum, u_char hismode, u_int32 refid);
nak_code	valid_NAK	(struct peer *peer, struct recvbuf *rbufp, u_char hismode);
static	double	root_distance	(struct peer *);
static	void	clock_combine	(peer_select *, int, int);
static	void	peer_xmit	(struct peer *);
static	void	fast_xmit	(struct recvbuf *, int, keyid_t, int);
static	void	pool_xmit	(struct peer *);
static	void	clock_update	(struct peer *);
static	void	measure_precision(void);
static	double	measure_tick_fuzz(void);
static	int	local_refid	(struct peer *);
static	int	peer_unfit	(struct peer *);
#ifdef AUTOKEY
static	int	group_test	(char *, char *);
#endif /* AUTOKEY */
#ifdef WORKER
void	pool_name_resolved	(int, int, void *, const char *,
				 const char *, const struct addrinfo *,
				 const struct addrinfo *);
#endif /* WORKER */

const char *	amtoa		(int am);


void
set_sys_leap(
	u_char new_sys_leap
	)
{
	sys_leap = new_sys_leap;
	xmt_leap = sys_leap;

	/*
	 * Under certain conditions we send faked leap bits to clients, so
	 * eventually change xmt_leap below, but never change LEAP_NOTINSYNC.
	 */
	if (xmt_leap != LEAP_NOTINSYNC) {
		if (leap_sec_in_progress) {
			/* always send "not sync" */
			xmt_leap = LEAP_NOTINSYNC;
		}
#ifdef LEAP_SMEAR
		else {
			/*
			 * If leap smear is enabled in general we must
			 * never send a leap second warning to clients,
			 * so make sure we only send "in sync".
			 */
			if (leap_smear.enabled)
				xmt_leap = LEAP_NOWARNING;
		}
#endif	/* LEAP_SMEAR */
	}
}


/*
 * Kiss Code check
 */
int
kiss_code_check(
	u_char hisleap,
	u_char hisstratum,
	u_char hismode,
	u_int32 refid
	)
{

	if (   hismode == MODE_SERVER
	    && hisleap == LEAP_NOTINSYNC
	    && hisstratum == STRATUM_UNSPEC) {
		if(memcmp(&refid,"RATE", 4) == 0) {
			return (RATEKISS);
		} else if(memcmp(&refid,"DENY", 4) == 0) {
			return (DENYKISS);
		} else if(memcmp(&refid,"RSTR", 4) == 0) {
			return (RSTRKISS);
		} else if(memcmp(&refid,"X", 1) == 0) {
			return (XKISS);
		}
	}
	return (NOKISS);
}


/*
 * Check that NAK is valid
 */
nak_code
valid_NAK(
	  struct peer *peer,
	  struct recvbuf *rbufp,
	  u_char hismode
	  )
{
	int		base_packet_length = MIN_V4_PKT_LEN;
	int		remainder_size;
	struct pkt *	rpkt;
	int		keyid;
	l_fp		p_org;	/* origin timestamp */
	const l_fp *	myorg;	/* selected peer origin */

	/*
	 * Check to see if there is something beyond the basic packet
	 */
	if (rbufp->recv_length == base_packet_length) {
		return NONAK;
	}

	remainder_size = rbufp->recv_length - base_packet_length;
	/*
	 * Is this a potential NAK?
	 */
	if (remainder_size != 4) {
		return NONAK;
	}

	/*
	 * Only server responses can contain NAK's
	 */

	if (hismode != MODE_SERVER &&
	    hismode != MODE_ACTIVE &&
	    hismode != MODE_PASSIVE
	    ) {
		return INVALIDNAK;
	}

	/*
	 * Make sure that the extra field in the packet is all zeros
	 */
	rpkt = &rbufp->recv_pkt;
	keyid = ntohl(((u_int32 *)rpkt)[base_packet_length / 4]);
	if (keyid != 0) {
		return INVALIDNAK;
	}

	/*
	 * During the first few packets of the autokey dance there will
	 * not (yet) be a keyid, but in this case FLAG_SKEY is set.
	 * So the NAK is invalid if either there's no peer, or
	 * if the keyid is 0 and FLAG_SKEY is not set.
	 */
	if (!peer || (!peer->keyid && !(peer->flags & FLAG_SKEY))) {
		return INVALIDNAK;
	}

	/*
	 * The ORIGIN must match, or this cannot be a valid NAK, either.
	 */
	NTOHL_FP(&rpkt->org, &p_org);
	if (peer->flip > 0)
		myorg = &peer->borg;
	else
		myorg = &peer->aorg;

	if (L_ISZERO(&p_org) ||
	    L_ISZERO( myorg) ||
	    !L_ISEQU(&p_org, myorg)) {
		return INVALIDNAK;
	}

	/* If we ever passed all that checks, we should be safe. Well,
	 * as safe as we can ever be with an unauthenticated crypto-nak.
	 */
	return VALIDNAK;
}


/*
 * transmit - transmit procedure called by poll timeout
 */
void
transmit(
	struct peer *peer	/* peer structure pointer */
	)
{
	u_char	hpoll;

	/*
	 * The polling state machine. There are two kinds of machines,
	 * those that never expect a reply (broadcast and manycast
	 * server modes) and those that do (all other modes). The dance
	 * is intricate...
	 */
	hpoll = peer->hpoll;

	/*
	 * If we haven't received anything (even if unsync) since last
	 * send, reset ppoll.
	 */
	if (peer->outdate > peer->timelastrec && !peer->reach)
		peer->ppoll = peer->maxpoll;

	/*
	 * In broadcast mode the poll interval is never changed from
	 * minpoll.
	 */
	if (peer->cast_flags & (MDF_BCAST | MDF_MCAST)) {
		peer->outdate = current_time;
		poll_update(peer, hpoll);
		if (sys_leap != LEAP_NOTINSYNC)
			peer_xmit(peer);
		return;
	}

	/*
	 * In manycast mode we start with unity ttl. The ttl is
	 * increased by one for each poll until either sys_maxclock
	 * servers have been found or the maximum ttl is reached. When
	 * sys_maxclock servers are found we stop polling until one or
	 * more servers have timed out or until less than sys_minclock
	 * associations turn up. In this case additional better servers
	 * are dragged in and preempt the existing ones.  Once every
	 * sys_beacon seconds we are to transmit unconditionally, but
	 * this code is not quite right -- peer->unreach counts polls
	 * and is being compared with sys_beacon, so the beacons happen
	 * every sys_beacon polls.
	 */
	if (peer->cast_flags & MDF_ACAST) {
		peer->outdate = current_time;
		poll_update(peer, hpoll);
		if (peer->unreach > sys_beacon) {
			peer->unreach = 0;
			peer->ttl = 0;
			peer_xmit(peer);
		} else if (   sys_survivors < sys_minclock
			   || peer_associations < sys_maxclock) {
			if (peer->ttl < sys_ttlmax)
				peer->ttl++;
			peer_xmit(peer);
		}
		peer->unreach++;
		return;
	}

	/*
	 * Pool associations transmit unicast solicitations when there
	 * are less than a hard limit of 2 * sys_maxclock associations,
	 * and either less than sys_minclock survivors or less than
	 * sys_maxclock associations.  The hard limit prevents unbounded
	 * growth in associations if the system clock or network quality
	 * result in survivor count dipping below sys_minclock often.
	 * This was observed testing with pool, where sys_maxclock == 12
	 * resulted in 60 associations without the hard limit.  A
	 * similar hard limit on manycastclient ephemeral associations
	 * may be appropriate.
	 */
	if (peer->cast_flags & MDF_POOL) {
		peer->outdate = current_time;
		poll_update(peer, hpoll);
		if (   (peer_associations <= 2 * sys_maxclock)
		    && (   peer_associations < sys_maxclock
			|| sys_survivors < sys_minclock))
			pool_xmit(peer);
		return;
	}

	/*
	 * In unicast modes the dance is much more intricate. It is
	 * designed to back off whenever possible to minimize network
	 * traffic.
	 */
	if (peer->burst == 0) {
		u_char oreach;

		/*
		 * Update the reachability status. If not heard for
		 * three consecutive polls, stuff infinity in the clock
		 * filter.
		 */
		oreach = peer->reach;
		peer->outdate = current_time;
		peer->unreach++;
		peer->reach <<= 1;
		if (!peer->reach) {

			/*
			 * Here the peer is unreachable. If it was
			 * previously reachable raise a trap. Send a
			 * burst if enabled.
			 */
			clock_filter(peer, 0., 0., MAXDISPERSE);
			if (oreach) {
				peer_unfit(peer);
				report_event(PEVNT_UNREACH, peer, NULL);
			}
			if (   (peer->flags & FLAG_IBURST)
			    && peer->retry == 0)
				peer->retry = NTP_RETRY;
		} else {

			/*
			 * Here the peer is reachable. Send a burst if
			 * enabled and the peer is fit.  Reset unreach
			 * for persistent and ephemeral associations.
			 * Unreach is also reset for survivors in
			 * clock_select().
			 */
			hpoll = sys_poll;
			if (!(peer->flags & FLAG_PREEMPT))
				peer->unreach = 0;
			if (   (peer->flags & FLAG_BURST)
			    && peer->retry == 0
			    && !peer_unfit(peer))
				peer->retry = NTP_RETRY;
		}

		/*
		 * Watch for timeout.  If ephemeral, toss the rascal;
		 * otherwise, bump the poll interval. Note the
		 * poll_update() routine will clamp it to maxpoll.
		 * If preemptible and we have more peers than maxclock,
		 * and this peer has the minimum score of preemptibles,
		 * demobilize.
		 */
		if (peer->unreach >= NTP_UNREACH) {
			hpoll++;
			/* ephemeral: no FLAG_CONFIG nor FLAG_PREEMPT */
			if (!(peer->flags & (FLAG_CONFIG | FLAG_PREEMPT))) {
				report_event(PEVNT_RESTART, peer, "timeout");
				peer_clear(peer, "TIME");
				unpeer(peer);
				return;
			}
			if (   (peer->flags & FLAG_PREEMPT)
			    && (peer_associations > sys_maxclock)
			    && score_all(peer)) {
				report_event(PEVNT_RESTART, peer, "timeout");
				peer_clear(peer, "TIME");
				unpeer(peer);
				return;
			}
		}
	} else {
		peer->burst--;
		if (peer->burst == 0) {

			/*
			 * If ntpdate mode and the clock has not been
			 * set and all peers have completed the burst,
			 * we declare a successful failure.
			 */
			if (mode_ntpdate) {
				peer_ntpdate--;
				if (peer_ntpdate == 0) {
					msyslog(LOG_NOTICE,
					    "ntpd: no servers found");
					if (!msyslog_term)
						printf(
						    "ntpd: no servers found\n");
					exit (0);
				}
			}
		}
	}
	if (peer->retry > 0)
		peer->retry--;

	/*
	 * Do not transmit if in broadcast client mode.
	 */
	poll_update(peer, hpoll);
	if (peer->hmode != MODE_BCLIENT)
		peer_xmit(peer);

	return;
}


const char *
amtoa(
	int am
	)
{
	char *bp;

	switch(am) {
	    case AM_ERR:	return "AM_ERR";
	    case AM_NOMATCH:	return "AM_NOMATCH";
	    case AM_PROCPKT:	return "AM_PROCPKT";
	    case AM_BCST:	return "AM_BCST";
	    case AM_FXMIT:	return "AM_FXMIT";
	    case AM_MANYCAST:	return "AM_MANYCAST";
	    case AM_NEWPASS:	return "AM_NEWPASS";
	    case AM_NEWBCL:	return "AM_NEWBCL";
	    case AM_POSSBCL:	return "AM_POSSBCL";
	    default:
		LIB_GETBUF(bp);
		snprintf(bp, LIB_BUFLENGTH, "AM_#%d", am);
		return bp;
	}
}


/*
 * receive - receive procedure called for each packet received
 */
void
receive(
	struct recvbuf *rbufp
	)
{
	register struct peer *peer;	/* peer structure pointer */
	register struct pkt *pkt;	/* receive packet pointer */
	u_char	hisversion;		/* packet version */
	u_char	hisleap;		/* packet leap indicator */
	u_char	hismode;		/* packet mode */
	u_char	hisstratum;		/* packet stratum */
	r4addr	r4a;			/* address restrictions */
	u_short	restrict_mask;		/* restrict bits */
	const char *hm_str;		/* hismode string */
	const char *am_str;		/* association match string */
	int	kissCode = NOKISS;	/* Kiss Code */
	int	has_mac;		/* length of MAC field */
	int	authlen;		/* offset of MAC field */
	auth_code is_authentic = AUTH_UNKNOWN;	/* Was AUTH_NONE */
	nak_code crypto_nak_test;	/* result of crypto-NAK check */
	int	retcode = AM_NOMATCH;	/* match code */
	keyid_t	skeyid = 0;		/* key IDs */
	u_int32	opcode = 0;		/* extension field opcode */
	sockaddr_u *dstadr_sin;		/* active runway */
	struct peer *peer2;		/* aux peer structure pointer */
	endpt	*match_ep;		/* newpeer() local address */
	l_fp	p_org;			/* origin timestamp */
	l_fp	p_rec;			/* receive timestamp */
	l_fp	p_xmt;			/* transmit timestamp */
#ifdef AUTOKEY
	char	hostname[NTP_MAXSTRLEN + 1];
	char	*groupname = NULL;
	struct autokey *ap;		/* autokey structure pointer */
	int	rval;			/* cookie snatcher */
	keyid_t	pkeyid = 0, tkeyid = 0;	/* key IDs */
#endif	/* AUTOKEY */
#ifdef HAVE_NTP_SIGND
	static unsigned char zero_key[16];
#endif /* HAVE_NTP_SIGND */

	/*
	 * Note that there are many places we do not call record_raw_stats().
	 *
	 * We only want to call it *after* we've sent a response, or perhaps
	 * when we've decided to drop a packet.
	 */

	/*
	 * Monitor the packet and get restrictions. Note that the packet
	 * length for control and private mode packets must be checked
	 * by the service routines. Some restrictions have to be handled
	 * later in order to generate a kiss-o'-death packet.
	 */
	/*
	 * Bogus port check is before anything, since it probably
	 * reveals a clogging attack.
	 */
	sys_received++;
	if (0 == SRCPORT(&rbufp->recv_srcadr)) {
		sys_badlength++;
		return;				/* bogus port */
	}
	restrictions(&rbufp->recv_srcadr, &r4a);
	restrict_mask = r4a.rflags;

	pkt = &rbufp->recv_pkt;
	hisversion = PKT_VERSION(pkt->li_vn_mode);
	hisleap = PKT_LEAP(pkt->li_vn_mode);
	hismode = (int)PKT_MODE(pkt->li_vn_mode);
	hisstratum = PKT_TO_STRATUM(pkt->stratum);
	DPRINTF(1, ("receive: at %ld %s<-%s ippeerlimit %d mode %d iflags %s restrict %s org %#010x.%08x xmt %#010x.%08x\n",
		    current_time, stoa(&rbufp->dstadr->sin),
		    stoa(&rbufp->recv_srcadr), r4a.ippeerlimit, hismode,
		    build_iflags(rbufp->dstadr->flags),
		    build_rflags(restrict_mask),
		    ntohl(pkt->org.l_ui), ntohl(pkt->org.l_uf),
		    ntohl(pkt->xmt.l_ui), ntohl(pkt->xmt.l_uf)));

	/* See basic mode and broadcast checks, below */
	INSIST(0 != hisstratum);

	if (restrict_mask & RES_IGNORE) {
		DPRINTF(2, ("receive: drop: RES_IGNORE\n"));
		sys_restricted++;
		return;				/* ignore everything */
	}
	if (hismode == MODE_PRIVATE) {
		if (!ntp_mode7 || (restrict_mask & RES_NOQUERY)) {
			DPRINTF(2, ("receive: drop: RES_NOQUERY\n"));
			sys_restricted++;
			return;			/* no query private */
		}
		process_private(rbufp, ((restrict_mask &
		    RES_NOMODIFY) == 0));
		return;
	}
	if (hismode == MODE_CONTROL) {
		if (restrict_mask & RES_NOQUERY) {
			DPRINTF(2, ("receive: drop: RES_NOQUERY\n"));
			sys_restricted++;
			return;			/* no query control */
		}
		process_control(rbufp, restrict_mask);
		return;
	}
	if (restrict_mask & RES_DONTSERVE) {
		DPRINTF(2, ("receive: drop: RES_DONTSERVE\n"));
		sys_restricted++;
		return;				/* no time serve */
	}

	/*
	 * This is for testing. If restricted drop ten percent of
	 * surviving packets.
	 */
	if (restrict_mask & RES_FLAKE) {
		if ((double)ntp_random() / 0x7fffffff < .1) {
			DPRINTF(2, ("receive: drop: RES_FLAKE\n"));
			sys_restricted++;
			return;			/* no flakeway */
		}
	}

	/*
	** Format Layer Checks
	**
	** Validate the packet format.  The packet size, packet header,
	** and any extension field lengths are checked.  We identify
	** the beginning of the MAC, to identify the upper limit of
	** of the hash computation.
	**
	** In case of a format layer check violation, the packet is
	** discarded with no further processing.
	*/

	/*
	 * Version check must be after the query packets, since they
	 * intentionally use an early version.
	 */
	if (hisversion == NTP_VERSION) {
		sys_newversion++;		/* new version */
	} else if (   !(restrict_mask & RES_VERSION)
		   && hisversion >= NTP_OLDVERSION) {
		sys_oldversion++;		/* previous version */
	} else {
		DPRINTF(2, ("receive: drop: RES_VERSION\n"));
		sys_badlength++;
		return;				/* old version */
	}

	/*
	 * Figure out his mode and validate the packet. This has some
	 * legacy raunch that probably should be removed. In very early
	 * NTP versions mode 0 was equivalent to what later versions
	 * would interpret as client mode.
	 */
	if (hismode == MODE_UNSPEC) {
		if (hisversion == NTP_OLDVERSION) {
			hismode = MODE_CLIENT;
		} else {
			DPRINTF(2, ("receive: drop: MODE_UNSPEC\n"));
			sys_badlength++;
			return;			/* invalid mode */
		}
	}

	/*
	 * Parse the extension field if present. We figure out whether
	 * an extension field is present by measuring the MAC size. If
	 * the number of words following the packet header is 0, no MAC
	 * is present and the packet is not authenticated. If 1, the
	 * packet is a crypto-NAK; if 3, the packet is authenticated
	 * with DES; if 5, the packet is authenticated with MD5; if 6,
	 * the packet is authenticated with SHA. If 2 or * 4, the packet
	 * is a runt and discarded forthwith. If greater than 6, an
	 * extension field is present, so we subtract the length of the
	 * field and go around again.
	 *
	 * Note the above description is lame.  We should/could also check
	 * the two bytes that make up the EF type and subtype, and then
	 * check the two bytes that tell us the EF length.  A legacy MAC
	 * has a 4 byte keyID, and for conforming symmetric keys its value
	 * must be <= 64k, meaning the top two bytes will always be zero.
	 * Since the EF Type of 0 is reserved/unused, there's no way a
	 * conforming legacy MAC could ever be misinterpreted as an EF.
	 *
	 * There is more, but this isn't the place to document it.
	 */

	authlen = LEN_PKT_NOMAC;
	has_mac = rbufp->recv_length - authlen;
	while (has_mac > 0) {
		u_int32	len;
#ifdef AUTOKEY
		u_int32	hostlen;
		struct exten *ep;
#endif /*AUTOKEY */

		if (has_mac % 4 != 0 || has_mac < (int)MIN_MAC_LEN) {
			DPRINTF(2, ("receive: drop: bad post-packet length\n"));
			sys_badlength++;
			return;			/* bad length */
		}
		/*
		 * This next test is clearly wrong - it needlessly
		 * prohibits short EFs (which don't yet exist)
		 */
		if (has_mac <= (int)MAX_MAC_LEN) {
			skeyid = ntohl(((u_int32 *)pkt)[authlen / 4]);
			break;

		} else {
			opcode = ntohl(((u_int32 *)pkt)[authlen / 4]);
			len = opcode & 0xffff;
			if (   len % 4 != 0
			    || len < 4
			    || (int)len + authlen > rbufp->recv_length) {
				DPRINTF(2, ("receive: drop: bad EF length\n"));
				sys_badlength++;
				return;		/* bad length */
			}
#ifdef AUTOKEY
			/*
			 * Extract calling group name for later.  If
			 * sys_groupname is non-NULL, there must be
			 * a group name provided to elicit a response.
			 */
			if (   (opcode & 0x3fff0000) == CRYPTO_ASSOC
			    && sys_groupname != NULL) {
				ep = (struct exten *)&((u_int32 *)pkt)[authlen / 4];
				hostlen = ntohl(ep->vallen);
				if (   hostlen >= sizeof(hostname)
				    || hostlen > len -
						offsetof(struct exten, pkt)) {
					DPRINTF(2, ("receive: drop: bad autokey hostname length\n"));
					sys_badlength++;
					return;		/* bad length */
				}
				memcpy(hostname, &ep->pkt, hostlen);
				hostname[hostlen] = '\0';
				groupname = strchr(hostname, '@');
				if (groupname == NULL) {
					DPRINTF(2, ("receive: drop: empty autokey groupname\n"));
					sys_declined++;
					return;
				}
				groupname++;
			}
#endif /* AUTOKEY */
			authlen += len;
			has_mac -= len;
		}
	}

	/*
	 * If has_mac is < 0 we had a malformed packet.
	 */
	if (has_mac < 0) {
		DPRINTF(2, ("receive: drop: post-packet under-read\n"));
		sys_badlength++;
		return;		/* bad length */
	}

	/*
	** Packet Data Verification Layer
	**
	** This layer verifies the packet data content.  If
	** authentication is required, a MAC must be present.
	** If a MAC is present, it must validate.
	** Crypto-NAK?  Look - a shiny thing!
	**
	** If authentication fails, we're done.
	*/

	/*
	 * If authentication is explicitly required, a MAC must be present.
	 */
	if (restrict_mask & RES_DONTTRUST && has_mac == 0) {
		DPRINTF(2, ("receive: drop: RES_DONTTRUST\n"));
		sys_restricted++;
		return;				/* access denied */
	}

	/*
	 * Update the MRU list and finger the cloggers. It can be a
	 * little expensive, so turn it off for production use.
	 * RES_LIMITED and RES_KOD will be cleared in the returned
	 * restrict_mask unless one or both actions are warranted.
	 */
	restrict_mask = ntp_monitor(rbufp, restrict_mask);
	if (restrict_mask & RES_LIMITED) {
		sys_limitrejected++;
		if (   !(restrict_mask & RES_KOD)
		    || MODE_BROADCAST == hismode
		    || MODE_SERVER == hismode) {
			if (MODE_SERVER == hismode) {
				DPRINTF(1, ("Possibly self-induced rate limiting of MODE_SERVER from %s\n",
					stoa(&rbufp->recv_srcadr)));
			} else {
				DPRINTF(2, ("receive: drop: RES_KOD\n"));
			}
			return;			/* rate exceeded */
		}
		if (hismode == MODE_CLIENT)
			fast_xmit(rbufp, MODE_SERVER, skeyid,
			    restrict_mask);
		else
			fast_xmit(rbufp, MODE_ACTIVE, skeyid,
			    restrict_mask);
		return;				/* rate exceeded */
	}
	restrict_mask &= ~RES_KOD;

	/*
	 * We have tossed out as many buggy packets as possible early in
	 * the game to reduce the exposure to a clogging attack. Now we
	 * have to burn some cycles to find the association and
	 * authenticate the packet if required. Note that we burn only
	 * digest cycles, again to reduce exposure. There may be no
	 * matching association and that's okay.
	 *
	 * More on the autokey mambo. Normally the local interface is
	 * found when the association was mobilized with respect to a
	 * designated remote address. We assume packets arriving from
	 * the remote address arrive via this interface and the local
	 * address used to construct the autokey is the unicast address
	 * of the interface. However, if the sender is a broadcaster,
	 * the interface broadcast address is used instead.
	 * Notwithstanding this technobabble, if the sender is a
	 * multicaster, the broadcast address is null, so we use the
	 * unicast address anyway. Don't ask.
	 */

	peer = findpeer(rbufp,  hismode, &retcode);
	dstadr_sin = &rbufp->dstadr->sin;
	NTOHL_FP(&pkt->org, &p_org);
	NTOHL_FP(&pkt->rec, &p_rec);
	NTOHL_FP(&pkt->xmt, &p_xmt);
	hm_str = modetoa(hismode);
	am_str = amtoa(retcode);

	/*
	 * Authentication is conditioned by three switches:
	 *
	 * NOPEER  (RES_NOPEER) do not mobilize an association unless
	 *         authenticated
	 * NOTRUST (RES_DONTTRUST) do not allow access unless
	 *         authenticated (implies NOPEER)
	 * enable  (sys_authenticate) master NOPEER switch, by default
	 *         on
	 *
	 * The NOPEER and NOTRUST can be specified on a per-client basis
	 * using the restrict command. The enable switch if on implies
	 * NOPEER for all clients. There are four outcomes:
	 *
	 * NONE    The packet has no MAC.
	 * OK      the packet has a MAC and authentication succeeds
	 * ERROR   the packet has a MAC and authentication fails
	 * CRYPTO  crypto-NAK. The MAC has four octets only.
	 *
	 * Note: The AUTH(x, y) macro is used to filter outcomes. If x
	 * is zero, acceptable outcomes of y are NONE and OK. If x is
	 * one, the only acceptable outcome of y is OK.
	 */
	crypto_nak_test = valid_NAK(peer, rbufp, hismode);

	/*
	 * Drop any invalid crypto-NAKs
	 */
	if (crypto_nak_test == INVALIDNAK) {
		report_event(PEVNT_AUTH, peer, "Invalid_NAK");
		if (0 != peer) {
			peer->badNAK++;
		}
		msyslog(LOG_ERR, "Invalid-NAK error at %ld %s<-%s",
			current_time, stoa(dstadr_sin), stoa(&rbufp->recv_srcadr));
		return;
	}

	if (has_mac == 0) {
		restrict_mask &= ~RES_MSSNTP;
		is_authentic = AUTH_NONE; /* not required */
		DPRINTF(1, ("receive: at %ld %s<-%s mode %d/%s:%s len %d org %#010x.%08x xmt %#010x.%08x NOMAC\n",
			    current_time, stoa(dstadr_sin),
			    stoa(&rbufp->recv_srcadr), hismode, hm_str, am_str,
			    authlen,
			    ntohl(pkt->org.l_ui), ntohl(pkt->org.l_uf),
			    ntohl(pkt->xmt.l_ui), ntohl(pkt->xmt.l_uf)));
	} else if (crypto_nak_test == VALIDNAK) {
		restrict_mask &= ~RES_MSSNTP;
		is_authentic = AUTH_CRYPTO; /* crypto-NAK */
		DPRINTF(1, ("receive: at %ld %s<-%s mode %d/%s:%s keyid %08x len %d auth %d org %#010x.%08x xmt %#010x.%08x CRYPTONAK\n",
			    current_time, stoa(dstadr_sin),
			    stoa(&rbufp->recv_srcadr), hismode, hm_str, am_str,
			    skeyid, authlen + has_mac, is_authentic,
			    ntohl(pkt->org.l_ui), ntohl(pkt->org.l_uf),
			    ntohl(pkt->xmt.l_ui), ntohl(pkt->xmt.l_uf)));

#ifdef HAVE_NTP_SIGND
		/*
		 * If the signature is 20 bytes long, the last 16 of
		 * which are zero, then this is a Microsoft client
		 * wanting AD-style authentication of the server's
		 * reply.
		 *
		 * This is described in Microsoft's WSPP docs, in MS-SNTP:
		 * http://msdn.microsoft.com/en-us/library/cc212930.aspx
		 */
	} else if (   has_mac == MAX_MD5_LEN
		   && (restrict_mask & RES_MSSNTP)
		   && (retcode == AM_FXMIT || retcode == AM_NEWPASS)
		   && (memcmp(zero_key, (char *)pkt + authlen + 4,
			      MAX_MD5_LEN - 4) == 0)) {
		is_authentic = AUTH_NONE;
		DPRINTF(1, ("receive: at %ld %s<-%s mode %d/%s:%s len %d org %#010x.%08x xmt %#010x.%08x SIGND\n",
			    current_time, stoa(dstadr_sin),
			    stoa(&rbufp->recv_srcadr), hismode, hm_str, am_str,
			    authlen,
			    ntohl(pkt->org.l_ui), ntohl(pkt->org.l_uf),
			    ntohl(pkt->xmt.l_ui), ntohl(pkt->xmt.l_uf)));
#endif /* HAVE_NTP_SIGND */

	} else {
		/*
		 * has_mac is not 0
		 * Not a VALID_NAK
		 * Not an MS-SNTP SIGND packet
		 *
		 * So there is a MAC here.
		 */

		restrict_mask &= ~RES_MSSNTP;
#ifdef AUTOKEY
		/*
		 * For autokey modes, generate the session key
		 * and install in the key cache. Use the socket
		 * broadcast or unicast address as appropriate.
		 */
		if (crypto_flags && skeyid > NTP_MAXKEY) {

			/*
			 * More on the autokey dance (AKD). A cookie is
			 * constructed from public and private values.
			 * For broadcast packets, the cookie is public
			 * (zero). For packets that match no
			 * association, the cookie is hashed from the
			 * addresses and private value. For server
			 * packets, the cookie was previously obtained
			 * from the server. For symmetric modes, the
			 * cookie was previously constructed using an
			 * agreement protocol; however, should PKI be
			 * unavailable, we construct a fake agreement as
			 * the EXOR of the peer and host cookies.
			 *
			 * hismode	ephemeral	persistent
			 * =======================================
			 * active	0		cookie#
			 * passive	0%		cookie#
			 * client	sys cookie	0%
			 * server	0%		sys cookie
			 * broadcast	0		0
			 *
			 * # if unsync, 0
			 * % can't happen
			 */
			if (has_mac < (int)MAX_MD5_LEN) {
				DPRINTF(2, ("receive: drop: MD5 digest too short\n"));
				sys_badauth++;
				return;
			}
			if (hismode == MODE_BROADCAST) {

				/*
				 * For broadcaster, use the interface
				 * broadcast address when available;
				 * otherwise, use the unicast address
				 * found when the association was
				 * mobilized. However, if this is from
				 * the wildcard interface, game over.
				 */
				if (   crypto_flags
				    && rbufp->dstadr ==
				       ANY_INTERFACE_CHOOSE(&rbufp->recv_srcadr)) {
					DPRINTF(2, ("receive: drop: BCAST from wildcard\n"));
					sys_restricted++;
					return;		/* no wildcard */
				}
				pkeyid = 0;
				if (!SOCK_UNSPEC(&rbufp->dstadr->bcast))
					dstadr_sin =
					    &rbufp->dstadr->bcast;
			} else if (peer == NULL) {
				pkeyid = session_key(
				    &rbufp->recv_srcadr, dstadr_sin, 0,
				    sys_private, 0);
			} else {
				pkeyid = peer->pcookie;
			}

			/*
			 * The session key includes both the public
			 * values and cookie. In case of an extension
			 * field, the cookie used for authentication
			 * purposes is zero. Note the hash is saved for
			 * use later in the autokey mambo.
			 */
			if (authlen > (int)LEN_PKT_NOMAC && pkeyid != 0) {
				session_key(&rbufp->recv_srcadr,
				    dstadr_sin, skeyid, 0, 2);
				tkeyid = session_key(
				    &rbufp->recv_srcadr, dstadr_sin,
				    skeyid, pkeyid, 0);
			} else {
				tkeyid = session_key(
				    &rbufp->recv_srcadr, dstadr_sin,
				    skeyid, pkeyid, 2);
			}

		}
#endif	/* AUTOKEY */

		/*
		 * Compute the cryptosum. Note a clogging attack may
		 * succeed in bloating the key cache. If an autokey,
		 * purge it immediately, since we won't be needing it
		 * again. If the packet is authentic, it can mobilize an
		 * association. Note that there is no key zero.
		 */
		if (!authdecrypt(skeyid, (u_int32 *)pkt, authlen,
		    has_mac))
			is_authentic = AUTH_ERROR;
		else
			is_authentic = AUTH_OK;
#ifdef AUTOKEY
		if (crypto_flags && skeyid > NTP_MAXKEY)
			authtrust(skeyid, 0);
#endif	/* AUTOKEY */
		DPRINTF(1, ("receive: at %ld %s<-%s mode %d/%s:%s keyid %08x len %d auth %d org %#010x.%08x xmt %#010x.%08x MAC\n",
			    current_time, stoa(dstadr_sin),
			    stoa(&rbufp->recv_srcadr), hismode, hm_str, am_str,
			    skeyid, authlen + has_mac, is_authentic,
			    ntohl(pkt->org.l_ui), ntohl(pkt->org.l_uf),
			    ntohl(pkt->xmt.l_ui), ntohl(pkt->xmt.l_uf)));
	}


	/*
	 * Bug 3454:
	 *
	 * Now come at this from a different perspective:
	 * - If we expect a MAC and it's not there, we drop it.
	 * - If we expect one keyID and get another, we drop it.
	 * - If we have a MAC ahd it hasn't been validated yet, try.
	 * - if the provided MAC doesn't validate, we drop it.
	 *
	 * There might be more to this.
	 */
	if (0 != peer && 0 != peer->keyid) {
		/* Should we msyslog() any of these? */

		/*
		 * This should catch:
		 * - no keyID where one is expected,
		 * - different keyID than what we expect.
		 */
		if (peer->keyid != skeyid) {
			DPRINTF(2, ("receive: drop: Wanted keyID %d, got %d from %s\n",
				    peer->keyid, skeyid,
				    stoa(&rbufp->recv_srcadr)));
			sys_restricted++;
			return;			/* drop: access denied */
		}

		/*
		 * if has_mac != 0 ...
		 * - If it has not yet been validated, do so.
		 *   (under what circumstances might that happen?)
		 * - if missing or bad MAC, log and drop.
		 */
		if (0 != has_mac) {
			if (is_authentic == AUTH_UNKNOWN) {
				/* How can this happen? */
				DPRINTF(2, ("receive: 3454 check: AUTH_UNKNOWN from %s\n",
				    stoa(&rbufp->recv_srcadr)));
				if (!authdecrypt(skeyid, (u_int32 *)pkt, authlen,
				    has_mac)) {
					/* MAC invalid or not found */
					is_authentic = AUTH_ERROR;
				} else {
					is_authentic = AUTH_OK;
				}
			}
			if (is_authentic != AUTH_OK) {
				DPRINTF(2, ("receive: drop: missing or bad MAC from %s\n",
					    stoa(&rbufp->recv_srcadr)));
				sys_restricted++;
				return;		/* drop: access denied */
			}
		}
	}
	/**/

	/*
	** On-Wire Protocol Layer
	**
	** Verify protocol operations consistent with the on-wire protocol.
	** The protocol discards bogus and duplicate packets as well as
	** minimizes disruptions doe to protocol restarts and dropped
	** packets.  The operations are controlled by two timestamps:
	** the transmit timestamp saved in the client state variables,
	** and the origin timestamp in the server packet header.  The
	** comparison of these two timestamps is called the loopback test.
	** The transmit timestamp functions as a nonce to verify that the
	** response corresponds to the original request.  The transmit
	** timestamp also serves to discard replays of the most recent
	** packet.  Upon failure of either test, the packet is discarded
	** with no further action.
	*/

	/*
	 * The association matching rules are implemented by a set of
	 * routines and an association table. A packet matching an
	 * association is processed by the peer process for that
	 * association. If there are no errors, an ephemeral association
	 * is mobilized: a broadcast packet mobilizes a broadcast client
	 * aassociation; a manycast server packet mobilizes a manycast
	 * client association; a symmetric active packet mobilizes a
	 * symmetric passive association.
	 */
	DPRINTF(1, ("receive: MATCH_ASSOC dispatch: mode %d/%s:%s \n",
		hismode, hm_str, am_str));
	switch (retcode) {

	/*
	 * This is a client mode packet not matching any association. If
	 * an ordinary client, simply toss a server mode packet back
	 * over the fence. If a manycast client, we have to work a
	 * little harder.
	 *
	 * There are cases here where we do not call record_raw_stats().
	 */
	case AM_FXMIT:

		/*
		 * If authentication OK, send a server reply; otherwise,
		 * send a crypto-NAK.
		 */
		if (!(rbufp->dstadr->flags & INT_MCASTOPEN)) {
			/* HMS: would be nice to log FAST_XMIT|BADAUTH|RESTRICTED */
			record_raw_stats(&rbufp->recv_srcadr,
			    &rbufp->dstadr->sin,
			    &p_org, &p_rec, &p_xmt, &rbufp->recv_time,
			    PKT_LEAP(pkt->li_vn_mode),
			    PKT_VERSION(pkt->li_vn_mode),
			    PKT_MODE(pkt->li_vn_mode),
			    PKT_TO_STRATUM(pkt->stratum),
			    pkt->ppoll,
			    pkt->precision,
			    FPTOD(NTOHS_FP(pkt->rootdelay)),
			    FPTOD(NTOHS_FP(pkt->rootdisp)),
			    pkt->refid,
			    rbufp->recv_length - MIN_V4_PKT_LEN, (u_char *)&pkt->exten);

			if (AUTH(restrict_mask & RES_DONTTRUST,
			   is_authentic)) {
				fast_xmit(rbufp, MODE_SERVER, skeyid,
				    restrict_mask);
			} else if (is_authentic == AUTH_ERROR) {
				fast_xmit(rbufp, MODE_SERVER, 0,
				    restrict_mask);
				sys_badauth++;
			} else {
				DPRINTF(2, ("receive: AM_FXMIT drop: !mcast restricted\n"));
				sys_restricted++;
			}

			return;			/* hooray */
		}

		/*
		 * This must be manycast. Do not respond if not
		 * configured as a manycast server.
		 */
		if (!sys_manycastserver) {
			DPRINTF(2, ("receive: AM_FXMIT drop: Not manycastserver\n"));
			sys_restricted++;
			return;			/* not enabled */
		}

#ifdef AUTOKEY
		/*
		 * Do not respond if not the same group.
		 */
		if (group_test(groupname, NULL)) {
			DPRINTF(2, ("receive: AM_FXMIT drop: empty groupname\n"));
			sys_declined++;
			return;
		}
#endif /* AUTOKEY */

		/*
		 * Do not respond if we are not synchronized or our
		 * stratum is greater than the manycaster or the
		 * manycaster has already synchronized to us.
		 */
		if (   sys_leap == LEAP_NOTINSYNC
		    || sys_stratum >= hisstratum
		    || (!sys_cohort && sys_stratum == hisstratum + 1)
		    || rbufp->dstadr->addr_refid == pkt->refid) {
			DPRINTF(2, ("receive: AM_FXMIT drop: LEAP_NOTINSYNC || stratum || loop\n"));
			sys_declined++;
			return;			/* no help */
		}

		/*
		 * Respond only if authentication succeeds. Don't do a
		 * crypto-NAK, as that would not be useful.
		 */
		if (AUTH(restrict_mask & RES_DONTTRUST, is_authentic)) {
			record_raw_stats(&rbufp->recv_srcadr,
			    &rbufp->dstadr->sin,
			    &p_org, &p_rec, &p_xmt, &rbufp->recv_time,
			    PKT_LEAP(pkt->li_vn_mode),
			    PKT_VERSION(pkt->li_vn_mode),
			    PKT_MODE(pkt->li_vn_mode),
			    PKT_TO_STRATUM(pkt->stratum),
			    pkt->ppoll,
			    pkt->precision,
			    FPTOD(NTOHS_FP(pkt->rootdelay)),
			    FPTOD(NTOHS_FP(pkt->rootdisp)),
			    pkt->refid,
			    rbufp->recv_length - MIN_V4_PKT_LEN, (u_char *)&pkt->exten);

			fast_xmit(rbufp, MODE_SERVER, skeyid,
			    restrict_mask);
		}
		return;				/* hooray */

	/*
	 * This is a server mode packet returned in response to a client
	 * mode packet sent to a multicast group address (for
	 * manycastclient) or to a unicast address (for pool). The
	 * origin timestamp is a good nonce to reliably associate the
	 * reply with what was sent. If there is no match, that's
	 * curious and could be an intruder attempting to clog, so we
	 * just ignore it.
	 *
	 * If the packet is authentic and the manycastclient or pool
	 * association is found, we mobilize a client association and
	 * copy pertinent variables from the manycastclient or pool
	 * association to the new client association. If not, just
	 * ignore the packet.
	 *
	 * There is an implosion hazard at the manycast client, since
	 * the manycast servers send the server packet immediately. If
	 * the guy is already here, don't fire up a duplicate.
	 *
	 * There are cases here where we do not call record_raw_stats().
	 */
	case AM_MANYCAST:

#ifdef AUTOKEY
		/*
		 * Do not respond if not the same group.
		 */
		if (group_test(groupname, NULL)) {
			DPRINTF(2, ("receive: AM_MANYCAST drop: empty groupname\n"));
			sys_declined++;
			return;
		}
#endif /* AUTOKEY */
		if ((peer2 = findmanycastpeer(rbufp)) == NULL) {
			DPRINTF(2, ("receive: AM_MANYCAST drop: No manycast peer\n"));
			sys_restricted++;
			return;			/* not enabled */
		}
		if (!AUTH(  (!(peer2->cast_flags & MDF_POOL)
			     && sys_authenticate)
			  || (restrict_mask & (RES_NOPEER |
			      RES_DONTTRUST)), is_authentic)
		    /* MC: RES_NOEPEER? */
		   ) {
			DPRINTF(2, ("receive: AM_MANYCAST drop: bad auth || (NOPEER|DONTTRUST)\n"));
			sys_restricted++;
			return;			/* access denied */
		}

		/*
		 * Do not respond if unsynchronized or stratum is below
		 * the floor or at or above the ceiling.
		 */
		if (   hisleap == LEAP_NOTINSYNC
		    || hisstratum < sys_floor
		    || hisstratum >= sys_ceiling) {
			DPRINTF(2, ("receive: AM_MANYCAST drop: unsync/stratum\n"));
			sys_declined++;
			return;			/* no help */
		}
		peer = newpeer(&rbufp->recv_srcadr, NULL, rbufp->dstadr,
			       r4a.ippeerlimit, MODE_CLIENT, hisversion,
			       peer2->minpoll, peer2->maxpoll,
			       FLAG_PREEMPT | (FLAG_IBURST & peer2->flags),
			       MDF_UCAST | MDF_UCLNT, 0, skeyid, sys_ident);
		if (NULL == peer) {
			DPRINTF(2, ("receive: AM_MANYCAST drop: duplicate\n"));
			sys_declined++;
			return;			/* ignore duplicate */
		}

		/*
		 * After each ephemeral pool association is spun,
		 * accelerate the next poll for the pool solicitor so
		 * the pool will fill promptly.
		 */
		if (peer2->cast_flags & MDF_POOL)
			peer2->nextdate = current_time + 1;

		/*
		 * Further processing of the solicitation response would
		 * simply detect its origin timestamp as bogus for the
		 * brand-new association (it matches the prototype
		 * association) and tinker with peer->nextdate delaying
		 * first sync.
		 */
		return;		/* solicitation response handled */

	/*
	 * This is the first packet received from a broadcast server. If
	 * the packet is authentic and we are enabled as broadcast
	 * client, mobilize a broadcast client association. We don't
	 * kiss any frogs here.
	 *
	 * There are cases here where we do not call record_raw_stats().
	 */
	case AM_NEWBCL:

#ifdef AUTOKEY
		/*
		 * Do not respond if not the same group.
		 */
		if (group_test(groupname, sys_ident)) {
			DPRINTF(2, ("receive: AM_NEWBCL drop: groupname mismatch\n"));
			sys_declined++;
			return;
		}
#endif /* AUTOKEY */
		if (sys_bclient == 0) {
			DPRINTF(2, ("receive: AM_NEWBCL drop: not a bclient\n"));
			sys_restricted++;
			return;			/* not enabled */
		}
		if (!AUTH(sys_authenticate | (restrict_mask &
			  (RES_NOPEER | RES_DONTTRUST)), is_authentic)
		    /* NEWBCL: RES_NOEPEER? */
		   ) {
			DPRINTF(2, ("receive: AM_NEWBCL drop: AUTH failed\n"));
			sys_restricted++;
			return;			/* access denied */
		}

		/*
		 * Do not respond if unsynchronized or stratum is below
		 * the floor or at or above the ceiling.
		 */
		if (   hisleap == LEAP_NOTINSYNC
		    || hisstratum < sys_floor
		    || hisstratum >= sys_ceiling) {
			DPRINTF(2, ("receive: AM_NEWBCL drop: Unsync or bad stratum\n"));
			sys_declined++;
			return;			/* no help */
		}

#ifdef AUTOKEY
		/*
		 * Do not respond if Autokey and the opcode is not a
		 * CRYPTO_ASSOC response with association ID.
		 */
		if (   crypto_flags && skeyid > NTP_MAXKEY
		    && (opcode & 0xffff0000) != (CRYPTO_ASSOC | CRYPTO_RESP)) {
			DPRINTF(2, ("receive: AM_NEWBCL drop: Autokey but not CRYPTO_ASSOC\n"));
			sys_declined++;
			return;			/* protocol error */
		}
#endif	/* AUTOKEY */

		/*
		 * Broadcasts received via a multicast address may
		 * arrive after a unicast volley has begun
		 * with the same remote address.  newpeer() will not
		 * find duplicate associations on other local endpoints
		 * if a non-NULL endpoint is supplied.  multicastclient
		 * ephemeral associations are unique across all local
		 * endpoints.
		 */
		if (!(INT_MCASTOPEN & rbufp->dstadr->flags))
			match_ep = rbufp->dstadr;
		else
			match_ep = NULL;

		/*
		 * Determine whether to execute the initial volley.
		 */
		if (sys_bdelay > 0.0) {
#ifdef AUTOKEY
			/*
			 * If a two-way exchange is not possible,
			 * neither is Autokey.
			 */
			if (crypto_flags && skeyid > NTP_MAXKEY) {
				sys_restricted++;
				DPRINTF(2, ("receive: AM_NEWBCL drop: Autokey but not 2-way\n"));
				return;		/* no autokey */
			}
#endif	/* AUTOKEY */

			/*
			 * Do not execute the volley. Start out in
			 * broadcast client mode.
			 */
			peer = newpeer(&rbufp->recv_srcadr, NULL, match_ep,
			    r4a.ippeerlimit, MODE_BCLIENT, hisversion,
			    pkt->ppoll, pkt->ppoll,
			    FLAG_PREEMPT, MDF_BCLNT, 0, skeyid, sys_ident);
			if (NULL == peer) {
				DPRINTF(2, ("receive: AM_NEWBCL drop: duplicate\n"));
				sys_restricted++;
				return;		/* ignore duplicate */

			} else {
				peer->delay = sys_bdelay;
				peer->bxmt = p_xmt;
			}
			break;
		}

		/*
		 * Execute the initial volley in order to calibrate the
		 * propagation delay and run the Autokey protocol.
		 *
		 * Note that the minpoll is taken from the broadcast
		 * packet, normally 6 (64 s) and that the poll interval
		 * is fixed at this value.
		 */
		peer = newpeer(&rbufp->recv_srcadr, NULL, match_ep,
			       r4a.ippeerlimit, MODE_CLIENT, hisversion,
			       pkt->ppoll, pkt->ppoll,
			       FLAG_BC_VOL | FLAG_IBURST | FLAG_PREEMPT, MDF_BCLNT,
			       0, skeyid, sys_ident);
		if (NULL == peer) {
			DPRINTF(2, ("receive: AM_NEWBCL drop: empty newpeer() failed\n"));
			sys_restricted++;
			return;			/* ignore duplicate */
		}
		peer->bxmt = p_xmt;
#ifdef AUTOKEY
		if (skeyid > NTP_MAXKEY)
			crypto_recv(peer, rbufp);
#endif	/* AUTOKEY */

		return;				/* hooray */

	/*
	 * This is the first packet received from a potential ephemeral
	 * symmetric active peer.  First, deal with broken Windows clients.
	 * Then, if NOEPEER is enabled, drop it.  If the packet meets our
	 * authenticty requirements and is the first he sent, mobilize
	 * a passive association.
	 * Otherwise, kiss the frog.
	 *
	 * There are cases here where we do not call record_raw_stats().
	 */
	case AM_NEWPASS:

		DEBUG_REQUIRE(MODE_ACTIVE == hismode);

#ifdef AUTOKEY
		/*
		 * Do not respond if not the same group.
		 */
		if (group_test(groupname, sys_ident)) {
			DPRINTF(2, ("receive: AM_NEWPASS drop: Autokey group mismatch\n"));
			sys_declined++;
			return;
		}
#endif /* AUTOKEY */
		if (!AUTH(sys_authenticate | (restrict_mask &
			  (RES_NOPEER | RES_DONTTRUST)), is_authentic)
		   ) {
			/*
			 * If authenticated but cannot mobilize an
			 * association, send a symmetric passive
			 * response without mobilizing an association.
			 * This is for drat broken Windows clients. See
			 * Microsoft KB 875424 for preferred workaround.
			 */
			if (AUTH(restrict_mask & RES_DONTTRUST,
				 is_authentic)) {
				fast_xmit(rbufp, MODE_PASSIVE, skeyid,
				    restrict_mask);
				return;			/* hooray */
			}
			/* HMS: Why is this next set of lines a feature? */
			if (is_authentic == AUTH_ERROR) {
				fast_xmit(rbufp, MODE_PASSIVE, 0,
				    restrict_mask);
				sys_restricted++;
				return;
			}

			if (restrict_mask & RES_NOEPEER) {
				DPRINTF(2, ("receive: AM_NEWPASS drop: NOEPEER\n"));
				sys_declined++;
				return;
			}

			/* [Bug 2941]
			 * If we got here, the packet isn't part of an
			 * existing association, either isn't correctly
			 * authenticated or it is but we are refusing
			 * ephemeral peer requests, and it didn't meet
			 * either of the previous two special cases so we
			 * should just drop it on the floor.  For example,
			 * crypto-NAKs (is_authentic == AUTH_CRYPTO)
			 * will make it this far.  This is just
			 * debug-printed and not logged to avoid log
			 * flooding.
			 */
			DPRINTF(2, ("receive: at %ld refusing to mobilize passive association"
				    " with unknown peer %s mode %d/%s:%s keyid %08x len %d auth %d\n",
				    current_time, stoa(&rbufp->recv_srcadr),
				    hismode, hm_str, am_str, skeyid,
				    (authlen + has_mac), is_authentic));
			sys_declined++;
			return;
		}

		if (restrict_mask & RES_NOEPEER) {
			DPRINTF(2, ("receive: AM_NEWPASS drop: NOEPEER\n"));
			sys_declined++;
			return;
		}

		/*
		 * Do not respond if synchronized and if stratum is
		 * below the floor or at or above the ceiling. Note,
		 * this allows an unsynchronized peer to synchronize to
		 * us. It would be very strange if he did and then was
		 * nipped, but that could only happen if we were
		 * operating at the top end of the range.  It also means
		 * we will spin an ephemeral association in response to
		 * MODE_ACTIVE KoDs, which will time out eventually.
		 */
		if (   hisleap != LEAP_NOTINSYNC
		    && (hisstratum < sys_floor || hisstratum >= sys_ceiling)) {
			DPRINTF(2, ("receive: AM_NEWPASS drop: Autokey group mismatch\n"));
			sys_declined++;
			return;			/* no help */
		}

		/*
		 * The message is correctly authenticated and allowed.
		 * Mobilize a symmetric passive association, if we won't
		 * exceed the ippeerlimit.
		 */
		if ((peer = newpeer(&rbufp->recv_srcadr, NULL, rbufp->dstadr,
				    r4a.ippeerlimit, MODE_PASSIVE, hisversion,
				    pkt->ppoll, NTP_MAXDPOLL, 0, MDF_UCAST, 0,
				    skeyid, sys_ident)) == NULL) {
			DPRINTF(2, ("receive: AM_NEWPASS drop: newpeer() failed\n"));
			sys_declined++;
			return;			/* ignore duplicate */
		}
		break;


	/*
	 * Process regular packet. Nothing special.
	 *
	 * There are cases here where we do not call record_raw_stats().
	 */
	case AM_PROCPKT:

#ifdef AUTOKEY
		/*
		 * Do not respond if not the same group.
		 */
		if (group_test(groupname, peer->ident)) {
			DPRINTF(2, ("receive: AM_PROCPKT drop: Autokey group mismatch\n"));
			sys_declined++;
			return;
		}
#endif /* AUTOKEY */

		if (MODE_BROADCAST == hismode) {
			int	bail = 0;
			l_fp	tdiff;
			u_long	deadband;

			DPRINTF(2, ("receive: PROCPKT/BROADCAST: prev pkt %ld seconds ago, ppoll: %d, %d secs\n",
				    (current_time - peer->timelastrec),
				    peer->ppoll, (1 << peer->ppoll)
				    ));
			/* Things we can check:
			 *
			 * Did the poll interval change?
			 * Is the poll interval in the packet in-range?
			 * Did this packet arrive too soon?
			 * Is the timestamp in this packet monotonic
			 *  with respect to the previous packet?
			 */

			/* This is noteworthy, not error-worthy */
			if (pkt->ppoll != peer->ppoll) {
				msyslog(LOG_INFO, "receive: broadcast poll from %s changed from %u to %u",
					stoa(&rbufp->recv_srcadr),
					peer->ppoll, pkt->ppoll);
			}

			/* This is error-worthy */
			if (   pkt->ppoll < peer->minpoll
			    || pkt->ppoll > peer->maxpoll) {
				msyslog(LOG_INFO, "receive: broadcast poll of %u from %s is out-of-range (%d to %d)!",
					pkt->ppoll, stoa(&rbufp->recv_srcadr),
					peer->minpoll, peer->maxpoll);
				++bail;
			}

			/* too early? worth an error, too!
			 *
			 * [Bug 3113] Ensure that at least one poll
			 * interval has elapsed since the last **clean**
			 * packet was received.  We limit the check to
			 * **clean** packets to prevent replayed packets
			 * and incorrectly authenticated packets, which
			 * we'll discard, from being used to create a
			 * denial of service condition.
			 */
			deadband = (1u << pkt->ppoll);
			if (FLAG_BC_VOL & peer->flags)
				deadband -= 3;	/* allow greater fuzz after volley */
			if ((current_time - peer->timereceived) < deadband) {
				msyslog(LOG_INFO, "receive: broadcast packet from %s arrived after %lu, not %lu seconds!",
					stoa(&rbufp->recv_srcadr),
					(current_time - peer->timereceived),
					deadband);
				++bail;
			}

			/* Alert if time from the server is non-monotonic.
			 *
			 * [Bug 3114] is about Broadcast mode replay DoS.
			 *
			 * Broadcast mode *assumes* a trusted network.
			 * Even so, it's nice to be robust in the face
			 * of attacks.
			 *
			 * If we get an authenticated broadcast packet
			 * with an "earlier" timestamp, it means one of
			 * two things:
			 *
			 * - the broadcast server had a backward step.
			 *
			 * - somebody is trying a replay attack.
			 *
			 * deadband: By default, we assume the broadcast
			 * network is trustable, so we take our accepted
			 * broadcast packets as we receive them.  But
			 * some folks might want to take additional poll
			 * delays before believing a backward step.
			 */
			if (sys_bcpollbstep) {
				/* pkt->ppoll or peer->ppoll ? */
				deadband = (1u << pkt->ppoll)
					   * sys_bcpollbstep + 2;
			} else {
				deadband = 0;
			}

			if (L_ISZERO(&peer->bxmt)) {
				tdiff.l_ui = tdiff.l_uf = 0;
			} else {
				tdiff = p_xmt;
				L_SUB(&tdiff, &peer->bxmt);
			}
			if (   tdiff.l_i < 0
			    && (current_time - peer->timereceived) < deadband)
			{
				msyslog(LOG_INFO, "receive: broadcast packet from %s contains non-monotonic timestamp: %#010x.%08x -> %#010x.%08x",
					stoa(&rbufp->recv_srcadr),
					peer->bxmt.l_ui, peer->bxmt.l_uf,
					p_xmt.l_ui, p_xmt.l_uf
					);
				++bail;
			}

			if (bail) {
				DPRINTF(2, ("receive: AM_PROCPKT drop: bail\n"));
				peer->timelastrec = current_time;
				sys_declined++;
				return;
			}
		}

		break;

	/*
	 * A passive packet matches a passive association. This is
	 * usually the result of reconfiguring a client on the fly. As
	 * this association might be legitimate and this packet an
	 * attempt to deny service, just ignore it.
	 */
	case AM_ERR:
		DPRINTF(2, ("receive: AM_ERR drop.\n"));
		sys_declined++;
		return;

	/*
	 * For everything else there is the bit bucket.
	 */
	default:
		DPRINTF(2, ("receive: default drop.\n"));
		sys_declined++;
		return;
	}

#ifdef AUTOKEY
	/*
	 * If the association is configured for Autokey, the packet must
	 * have a public key ID; if not, the packet must have a
	 * symmetric key ID.
	 */
	if (   is_authentic != AUTH_CRYPTO
	    && (   ((peer->flags & FLAG_SKEY) && skeyid <= NTP_MAXKEY)
	        || (!(peer->flags & FLAG_SKEY) && skeyid > NTP_MAXKEY))) {
		DPRINTF(2, ("receive: drop: Autokey but wrong/bad auth\n"));
		sys_badauth++;
		return;
	}
#endif	/* AUTOKEY */

	peer->received++;
	peer->flash &= ~PKT_TEST_MASK;
	if (peer->flags & FLAG_XBOGUS) {
		peer->flags &= ~FLAG_XBOGUS;
		peer->flash |= TEST3;
	}

	/*
	 * Next comes a rigorous schedule of timestamp checking. If the
	 * transmit timestamp is zero, the server has not initialized in
	 * interleaved modes or is horribly broken.
	 *
	 * A KoD packet we pay attention to cannot have a 0 transmit
	 * timestamp.
	 */

	kissCode = kiss_code_check(hisleap, hisstratum, hismode, pkt->refid);

	if (L_ISZERO(&p_xmt)) {
		peer->flash |= TEST3;			/* unsynch */
		if (kissCode != NOKISS) {		/* KoD packet */
			peer->bogusorg++;		/* for TEST2 or TEST3 */
			msyslog(LOG_INFO,
				"receive: Unexpected zero transmit timestamp in KoD from %s",
				ntoa(&peer->srcadr));
			return;
		}

	/*
	 * If the transmit timestamp duplicates our previous one, the
	 * packet is a replay. This prevents the bad guys from replaying
	 * the most recent packet, authenticated or not.
	 */
	} else if (L_ISEQU(&peer->xmt, &p_xmt)) {
		DPRINTF(2, ("receive: drop: Duplicate xmit\n"));
		peer->flash |= TEST1;			/* duplicate */
		peer->oldpkt++;
		return;

	/*
	 * If this is a broadcast mode packet, make sure hisstratum
	 * is appropriate.  Don't do anything else here - we wait to
	 * see if this is an interleave broadcast packet until after
	 * we've validated the MAC that SHOULD be provided.
	 *
	 * hisstratum cannot be 0 - see assertion above.
	 * If hisstratum is 15, then we'll advertise as UNSPEC but
	 * at least we'll be able to sync with the broadcast server.
	 */
	} else if (hismode == MODE_BROADCAST) {
		/* 0 is unexpected too, and impossible */
		if (STRATUM_UNSPEC <= hisstratum) {
			/* Is this a ++sys_declined or ??? */
			msyslog(LOG_INFO,
				"receive: Unexpected stratum (%d) in broadcast from %s",
				hisstratum, ntoa(&peer->srcadr));
			return;
		}

	/*
	 * Basic KoD validation checking:
	 *
	 * KoD packets are a mixed-blessing.  Forged KoD packets
	 * are DoS attacks.  There are rare situations where we might
	 * get a valid KoD response, though.  Since KoD packets are
	 * a special case that complicate the checks we do next, we
	 * handle the basic KoD checks here.
	 *
	 * Note that we expect the incoming KoD packet to have its
	 * (nonzero) org, rec, and xmt timestamps set to the xmt timestamp
	 * that we have previously sent out.  Watch interleave mode.
	 */
	} else if (kissCode != NOKISS) {
		DEBUG_INSIST(!L_ISZERO(&p_xmt));
		if (   L_ISZERO(&p_org)		/* We checked p_xmt above */
		    || L_ISZERO(&p_rec)) {
			peer->bogusorg++;
			msyslog(LOG_INFO,
				"receive: KoD packet from %s has a zero org or rec timestamp.  Ignoring.",
				ntoa(&peer->srcadr));
			return;
		}

		if (   !L_ISEQU(&p_xmt, &p_org)
		    || !L_ISEQU(&p_xmt, &p_rec)) {
			peer->bogusorg++;
			msyslog(LOG_INFO,
				"receive: KoD packet from %s has inconsistent xmt/org/rec timestamps.  Ignoring.",
				ntoa(&peer->srcadr));
			return;
		}

		/* Be conservative */
		if (peer->flip == 0 && !L_ISEQU(&p_org, &peer->aorg)) {
			peer->bogusorg++;
			msyslog(LOG_INFO,
				"receive: flip 0 KoD origin timestamp %#010x.%08x from %s does not match %#010x.%08x - ignoring.",
				p_org.l_ui, p_org.l_uf,
				ntoa(&peer->srcadr),
				peer->aorg.l_ui, peer->aorg.l_uf);
			return;
		} else if (peer->flip == 1 && !L_ISEQU(&p_org, &peer->borg)) {
			peer->bogusorg++;
			msyslog(LOG_INFO,
				"receive: flip 1 KoD origin timestamp %#010x.%08x from %s does not match interleave %#010x.%08x - ignoring.",
				p_org.l_ui, p_org.l_uf,
				ntoa(&peer->srcadr),
				peer->borg.l_ui, peer->borg.l_uf);
			return;
		}

	/*
	 * Basic mode checks:
	 *
	 * If there is no origin timestamp, it's either an initial packet
	 * or we've already received a response to our query.  Of course,
	 * should 'aorg' be all-zero because this really was the original
	 * transmit timestamp, we'll ignore this reply.  There is a window
	 * of one nanosecond once every 136 years' time where this is
	 * possible.  We currently ignore this situation, as a completely
	 * zero timestamp is (quietly?) disallowed.
	 *
	 * Otherwise, check for bogus packet in basic mode.
	 * If it is bogus, switch to interleaved mode and resynchronize,
	 * but only after confirming the packet is not bogus in
	 * symmetric interleaved mode.
	 *
	 * This could also mean somebody is forging packets claiming to
	 * be from us, attempting to cause our server to KoD us.
	 *
	 * We have earlier asserted that hisstratum cannot be 0.
	 * If hisstratum is STRATUM_UNSPEC, it means he's not sync'd.
	 */
	} else if (peer->flip == 0) {
		if (0) {
		} else if (L_ISZERO(&p_org)) {
			const char *action;

#ifdef BUG3361
			msyslog(LOG_INFO,
				"receive: BUG 3361: Clearing peer->aorg ");
			L_CLR(&peer->aorg);
#endif
			/**/
			switch (hismode) {
			/* We allow 0org for: */
			    case UCHAR_MAX:
				action = "Allow";
				break;
			/* We disallow 0org for: */
			    case MODE_UNSPEC:
			    case MODE_ACTIVE:
			    case MODE_PASSIVE:
			    case MODE_CLIENT:
			    case MODE_SERVER:
			    case MODE_BROADCAST:
				action = "Drop";
				peer->bogusorg++;
				peer->flash |= TEST2;	/* bogus */
				break;
			    default:
				action = "";	/* for cranky compilers / MSVC */
				INSIST(!"receive(): impossible hismode");
				break;
			}
			/**/
			msyslog(LOG_INFO,
				"receive: %s 0 origin timestamp from %s@%s xmt %#010x.%08x",
				action, hm_str, ntoa(&peer->srcadr),
				ntohl(pkt->xmt.l_ui), ntohl(pkt->xmt.l_uf));
		} else if (!L_ISEQU(&p_org, &peer->aorg)) {
			/* are there cases here where we should bail? */
			/* Should we set TEST2 if we decide to try xleave? */
			peer->bogusorg++;
			peer->flash |= TEST2;	/* bogus */
			msyslog(LOG_INFO,
				"receive: Unexpected origin timestamp %#010x.%08x does not match aorg %#010x.%08x from %s@%s xmt %#010x.%08x",
				ntohl(pkt->org.l_ui), ntohl(pkt->org.l_uf),
				peer->aorg.l_ui, peer->aorg.l_uf,
				hm_str, ntoa(&peer->srcadr),
				ntohl(pkt->xmt.l_ui), ntohl(pkt->xmt.l_uf));
			if (  !L_ISZERO(&peer->dst)
			    && L_ISEQU(&p_org, &peer->dst)) {
				/* Might be the start of an interleave */
				if (dynamic_interleave) {
					peer->flip = 1;
					report_event(PEVNT_XLEAVE, peer, NULL);
				} else {
					msyslog(LOG_INFO,
						"receive: Dynamic interleave from %s@%s denied",
						hm_str, ntoa(&peer->srcadr));
				}
			}
		} else {
			L_CLR(&peer->aorg);
		}

	/*
	 * Check for valid nonzero timestamp fields.
	 */
	} else if (   L_ISZERO(&p_org)
		   || L_ISZERO(&p_rec)
		   || L_ISZERO(&peer->dst)) {
		peer->flash |= TEST3;		/* unsynch */

	/*
	 * Check for bogus packet in interleaved symmetric mode. This
	 * can happen if a packet is lost, duplicated or crossed. If
	 * found, flip and resynchronize.
	 */
	} else if (   !L_ISZERO(&peer->dst)
		   && !L_ISEQU(&p_org, &peer->dst)) {
		DPRINTF(2, ("receive: drop: Bogus packet in interleaved symmetric mode\n"));
		peer->bogusorg++;
		peer->flags |= FLAG_XBOGUS;
		peer->flash |= TEST2;		/* bogus */
#ifdef BUG3453
		return; /* Bogus packet, we are done */
#endif
	}

	/**/

	/*
	 * If this is a crypto_NAK, the server cannot authenticate a
	 * client packet. The server might have just changed keys. Clear
	 * the association and restart the protocol.
	 */
	if (crypto_nak_test == VALIDNAK) {
		report_event(PEVNT_AUTH, peer, "crypto_NAK");
		peer->flash |= TEST5;		/* bad auth */
		peer->badauth++;
		if (peer->flags & FLAG_PREEMPT) {
			if (unpeer_crypto_nak_early) {
				unpeer(peer);
			}
			DPRINTF(2, ("receive: drop: PREEMPT crypto_NAK\n"));
			return;
		}
#ifdef AUTOKEY
		if (peer->crypto) {
			peer_clear(peer, "AUTH");
		}
#endif	/* AUTOKEY */
		DPRINTF(2, ("receive: drop: crypto_NAK\n"));
		return;

	/*
	 * If the digest fails or it's missing for authenticated
	 * associations, the client cannot authenticate a server
	 * reply to a client packet previously sent. The loopback check
	 * is designed to avoid a bait-and-switch attack, which was
	 * possible in past versions. If symmetric modes, return a
	 * crypto-NAK. The peer should restart the protocol.
	 */
	} else if (!AUTH(peer->keyid || has_mac ||
			 (restrict_mask & RES_DONTTRUST), is_authentic)) {

		if (peer->flash & PKT_TEST_MASK) {
			msyslog(LOG_INFO,
				"receive: Bad auth in packet with bad timestamps from %s denied - spoof?",
				ntoa(&peer->srcadr));
			return;
		}

		report_event(PEVNT_AUTH, peer, "digest");
		peer->flash |= TEST5;		/* bad auth */
		peer->badauth++;
		if (   has_mac
		    && (   hismode == MODE_ACTIVE
			|| hismode == MODE_PASSIVE))
			fast_xmit(rbufp, MODE_ACTIVE, 0, restrict_mask);
		if (peer->flags & FLAG_PREEMPT) {
			if (unpeer_digest_early) {
				unpeer(peer);
			}
		}
#ifdef AUTOKEY
		else if (peer_clear_digest_early && peer->crypto) {
			peer_clear(peer, "AUTH");
		}
#endif	/* AUTOKEY */
		DPRINTF(2, ("receive: drop: Bad or missing AUTH\n"));
		return;
	}

	/*
	 * For broadcast packets:
	 *
	 * HMS: This next line never made much sense to me, even
	 * when it was up higher:
	 *   If an initial volley, bail out now and let the
	 *   client do its stuff.
	 *
	 * If the packet has not failed authentication, then
	 * - if the origin timestamp is nonzero this is an
	 *   interleaved broadcast, so restart the protocol.
	 * - else, this is not an interleaved broadcast packet.
	 */
	if (hismode == MODE_BROADCAST) {
		if (   is_authentic == AUTH_OK
		    || is_authentic == AUTH_NONE) {
			if (!L_ISZERO(&p_org)) {
				if (!(peer->flags & FLAG_XB)) {
					msyslog(LOG_INFO,
						"receive: Broadcast server at %s is in interleave mode",
						ntoa(&peer->srcadr));
					peer->flags |= FLAG_XB;
					peer->aorg = p_xmt;
					peer->borg = rbufp->recv_time;
					report_event(PEVNT_XLEAVE, peer, NULL);
					return;
				}
			} else if (peer->flags & FLAG_XB) {
				msyslog(LOG_INFO,
					"receive: Broadcast server at %s is no longer in interleave mode",
					ntoa(&peer->srcadr));
				peer->flags &= ~FLAG_XB;
			}
		} else {
			msyslog(LOG_INFO,
				"receive: Bad broadcast auth (%d) from %s",
				is_authentic, ntoa(&peer->srcadr));
		}

		/*
		 * Now that we know the packet is correctly authenticated,
		 * update peer->bxmt.
		 */
		peer->bxmt = p_xmt;
	}


	/*
	** Update the state variables.
	*/
	if (peer->flip == 0) {
		if (hismode != MODE_BROADCAST)
			peer->rec = p_xmt;
		peer->dst = rbufp->recv_time;
	}
	peer->xmt = p_xmt;

	/*
	 * Set the peer ppoll to the maximum of the packet ppoll and the
	 * peer minpoll. If a kiss-o'-death, set the peer minpoll to
	 * this maximum and advance the headway to give the sender some
	 * headroom. Very intricate.
	 */

	/*
	 * Check for any kiss codes. Note this is only used when a server
	 * responds to a packet request.
	 */

	/*
	 * Check to see if this is a RATE Kiss Code
	 * Currently this kiss code will accept whatever poll
	 * rate that the server sends
	 */
	peer->ppoll = max(peer->minpoll, pkt->ppoll);
	if (kissCode == RATEKISS) {
		peer->selbroken++;	/* Increment the KoD count */
		report_event(PEVNT_RATE, peer, NULL);
		if (pkt->ppoll > peer->minpoll)
			peer->minpoll = peer->ppoll;
		peer->burst = peer->retry = 0;
		peer->throttle = (NTP_SHIFT + 1) * (1 << peer->minpoll);
		poll_update(peer, pkt->ppoll);
		return;				/* kiss-o'-death */
	}
	if (kissCode != NOKISS) {
		peer->selbroken++;	/* Increment the KoD count */
		return;		/* Drop any other kiss code packets */
	}


	/*
	 * XXX
	 */


	/*
	 * If:
	 *	- this is a *cast (uni-, broad-, or m-) server packet
	 *	- and it's symmetric-key authenticated
	 * then see if the sender's IP is trusted for this keyid.
	 * If it is, great - nothing special to do here.
	 * Otherwise, we should report and bail.
	 *
	 * Autokey-authenticated packets are accepted.
	 */

	switch (hismode) {
	    case MODE_SERVER:		/* server mode */
	    case MODE_BROADCAST:	/* broadcast mode */
	    case MODE_ACTIVE:		/* symmetric active mode */
	    case MODE_PASSIVE:		/* symmetric passive mode */
		if (   is_authentic == AUTH_OK
		    && skeyid
		    && skeyid <= NTP_MAXKEY
		    && !authistrustedip(skeyid, &peer->srcadr)) {
			report_event(PEVNT_AUTH, peer, "authIP");
			peer->badauth++;
			return;
		}
		break;

	    case MODE_CLIENT:		/* client mode */
#if 0		/* At this point, MODE_CONTROL is overloaded by MODE_BCLIENT */
	    case MODE_CONTROL:		/* control mode */
#endif
	    case MODE_PRIVATE:		/* private mode */
	    case MODE_BCLIENT:		/* broadcast client mode */
		break;

	    case MODE_UNSPEC:		/* unspecified (old version) */
	    default:
		msyslog(LOG_INFO,
			"receive: Unexpected mode (%d) in packet from %s",
			hismode, ntoa(&peer->srcadr));
		break;
	}


	/*
	 * That was hard and I am sweaty, but the packet is squeaky
	 * clean. Get on with real work.
	 */
	peer->timereceived = current_time;
	peer->timelastrec = current_time;
	if (is_authentic == AUTH_OK)
		peer->flags |= FLAG_AUTHENTIC;
	else
		peer->flags &= ~FLAG_AUTHENTIC;

#ifdef AUTOKEY
	/*
	 * More autokey dance. The rules of the cha-cha are as follows:
	 *
	 * 1. If there is no key or the key is not auto, do nothing.
	 *
	 * 2. If this packet is in response to the one just previously
	 *    sent or from a broadcast server, do the extension fields.
	 *    Otherwise, assume bogosity and bail out.
	 *
	 * 3. If an extension field contains a verified signature, it is
	 *    self-authenticated and we sit the dance.
	 *
	 * 4. If this is a server reply, check only to see that the
	 *    transmitted key ID matches the received key ID.
	 *
	 * 5. Check to see that one or more hashes of the current key ID
	 *    matches the previous key ID or ultimate original key ID
	 *    obtained from the broadcaster or symmetric peer. If no
	 *    match, sit the dance and call for new autokey values.
	 *
	 * In case of crypto error, fire the orchestra, stop dancing and
	 * restart the protocol.
	 */
	if (peer->flags & FLAG_SKEY) {
		/*
		 * Decrement remaining autokey hashes. This isn't
		 * perfect if a packet is lost, but results in no harm.
		 */
		ap = (struct autokey *)peer->recval.ptr;
		if (ap != NULL) {
			if (ap->seq > 0)
				ap->seq--;
		}
		peer->flash |= TEST8;
		rval = crypto_recv(peer, rbufp);
		if (rval == XEVNT_OK) {
			peer->unreach = 0;
		} else {
			if (rval == XEVNT_ERR) {
				report_event(PEVNT_RESTART, peer,
				    "crypto error");
				peer_clear(peer, "CRYP");
				peer->flash |= TEST9;	/* bad crypt */
				if (peer->flags & FLAG_PREEMPT) {
					if (unpeer_crypto_early) {
						unpeer(peer);
					}
				}
			}
			return;
		}

		/*
		 * If server mode, verify the receive key ID matches
		 * the transmit key ID.
		 */
		if (hismode == MODE_SERVER) {
			if (skeyid == peer->keyid)
				peer->flash &= ~TEST8;

		/*
		 * If an extension field is present, verify only that it
		 * has been correctly signed. We don't need a sequence
		 * check here, but the sequence continues.
		 */
		} else if (!(peer->flash & TEST8)) {
			peer->pkeyid = skeyid;

		/*
		 * Now the fun part. Here, skeyid is the current ID in
		 * the packet, pkeyid is the ID in the last packet and
		 * tkeyid is the hash of skeyid. If the autokey values
		 * have not been received, this is an automatic error.
		 * If so, check that the tkeyid matches pkeyid. If not,
		 * hash tkeyid and try again. If the number of hashes
		 * exceeds the number remaining in the sequence, declare
		 * a successful failure and refresh the autokey values.
		 */
		} else if (ap != NULL) {
			int i;

			for (i = 0; ; i++) {
				if (   tkeyid == peer->pkeyid
				    || tkeyid == ap->key) {
					peer->flash &= ~TEST8;
					peer->pkeyid = skeyid;
					ap->seq -= i;
					break;
				}
				if (i > ap->seq) {
					peer->crypto &=
					    ~CRYPTO_FLAG_AUTO;
					break;
				}
				tkeyid = session_key(
				    &rbufp->recv_srcadr, dstadr_sin,
				    tkeyid, pkeyid, 0);
			}
			if (peer->flash & TEST8)
				report_event(PEVNT_AUTH, peer, "keylist");
		}
		if (!(peer->crypto & CRYPTO_FLAG_PROV)) /* test 9 */
			peer->flash |= TEST8;	/* bad autokey */

		/*
		 * The maximum lifetime of the protocol is about one
		 * week before restarting the Autokey protocol to
		 * refresh certificates and leapseconds values.
		 */
		if (current_time > peer->refresh) {
			report_event(PEVNT_RESTART, peer,
			    "crypto refresh");
			peer_clear(peer, "TIME");
			return;
		}
	}
#endif	/* AUTOKEY */

	/*
	 * The dance is complete and the flash bits have been lit. Toss
	 * the packet over the fence for processing, which may light up
	 * more flashers.
	 */
	process_packet(peer, pkt, rbufp->recv_length);

	/*
	 * In interleaved mode update the state variables. Also adjust the
	 * transmit phase to avoid crossover.
	 */
	if (peer->flip != 0) {
		peer->rec = p_rec;
		peer->dst = rbufp->recv_time;
		if (peer->nextdate - current_time < (1U << min(peer->ppoll,
		    peer->hpoll)) / 2)
			peer->nextdate++;
		else
			peer->nextdate--;
	}
}


/*
 * process_packet - Packet Procedure, a la Section 3.4.4 of RFC-1305
 *	Or almost, at least.  If we're in here we have a reasonable
 *	expectation that we will be having a long term
 *	relationship with this host.
 */
void
process_packet(
	register struct peer *peer,
	register struct pkt *pkt,
	u_int	len
	)
{
	double	t34, t21;
	double	p_offset, p_del, p_disp;
	l_fp	p_rec, p_xmt, p_org, p_reftime, ci;
	u_char	pmode, pleap, pversion, pstratum;
	char	statstr[NTP_MAXSTRLEN];
#ifdef ASSYM
	int	itemp;
	double	etemp, ftemp, td;
#endif /* ASSYM */

#if 0
	sys_processed++;
	peer->processed++;
#endif
	p_del = FPTOD(NTOHS_FP(pkt->rootdelay));
	p_offset = 0;
	p_disp = FPTOD(NTOHS_FP(pkt->rootdisp));
	NTOHL_FP(&pkt->reftime, &p_reftime);
	NTOHL_FP(&pkt->org, &p_org);
	NTOHL_FP(&pkt->rec, &p_rec);
	NTOHL_FP(&pkt->xmt, &p_xmt);
	pmode = PKT_MODE(pkt->li_vn_mode);
	pleap = PKT_LEAP(pkt->li_vn_mode);
	pversion = PKT_VERSION(pkt->li_vn_mode);
	pstratum = PKT_TO_STRATUM(pkt->stratum);

	/**/

	/**/

	/*
	 * Verify the server is synchronized; that is, the leap bits,
	 * stratum and root distance are valid.
	 */
	if (   pleap == LEAP_NOTINSYNC		/* test 6 */
	    || pstratum < sys_floor || pstratum >= sys_ceiling)
		peer->flash |= TEST6;		/* bad synch or strat */
	if (p_del / 2 + p_disp >= MAXDISPERSE)	/* test 7 */
		peer->flash |= TEST7;		/* bad header */

	/*
	 * If any tests fail at this point, the packet is discarded.
	 * Note that some flashers may have already been set in the
	 * receive() routine.
	 */
	if (peer->flash & PKT_TEST_MASK) {
		peer->seldisptoolarge++;
		DPRINTF(1, ("packet: flash header %04x\n",
			    peer->flash));
		poll_update(peer, peer->hpoll);	/* ppoll updated? */
		return;
	}

	/**/

#if 1
	sys_processed++;
	peer->processed++;
#endif

	/*
	 * Capture the header values in the client/peer association..
	 */
	record_raw_stats(&peer->srcadr,
	    peer->dstadr ? &peer->dstadr->sin : NULL,
	    &p_org, &p_rec, &p_xmt, &peer->dst,
	    pleap, pversion, pmode, pstratum, pkt->ppoll, pkt->precision,
	    p_del, p_disp, pkt->refid,
	    len - MIN_V4_PKT_LEN, (u_char *)&pkt->exten);
	peer->leap = pleap;
	peer->stratum = min(pstratum, STRATUM_UNSPEC);
	peer->pmode = pmode;
	peer->precision = pkt->precision;
	peer->rootdelay = p_del;
	peer->rootdisp = p_disp;
	peer->refid = pkt->refid;		/* network byte order */
	peer->reftime = p_reftime;

	/*
	 * First, if either burst mode is armed, enable the burst.
	 * Compute the headway for the next packet and delay if
	 * necessary to avoid exceeding the threshold.
	 */
	if (peer->retry > 0) {
		peer->retry = 0;
		if (peer->reach)
			peer->burst = min(1 << (peer->hpoll -
			    peer->minpoll), NTP_SHIFT) - 1;
		else
			peer->burst = NTP_IBURST - 1;
		if (peer->burst > 0)
			peer->nextdate = current_time;
	}
	poll_update(peer, peer->hpoll);

	/**/

	/*
	 * If the peer was previously unreachable, raise a trap. In any
	 * case, mark it reachable.
	 */
	if (!peer->reach) {
		report_event(PEVNT_REACH, peer, NULL);
		peer->timereachable = current_time;
	}
	peer->reach |= 1;

	/*
	 * For a client/server association, calculate the clock offset,
	 * roundtrip delay and dispersion. The equations are reordered
	 * from the spec for more efficient use of temporaries. For a
	 * broadcast association, offset the last measurement by the
	 * computed delay during the client/server volley. Note the
	 * computation of dispersion includes the system precision plus
	 * that due to the frequency error since the origin time.
	 *
	 * It is very important to respect the hazards of overflow. The
	 * only permitted operation on raw timestamps is subtraction,
	 * where the result is a signed quantity spanning from 68 years
	 * in the past to 68 years in the future. To avoid loss of
	 * precision, these calculations are done using 64-bit integer
	 * arithmetic. However, the offset and delay calculations are
	 * sums and differences of these first-order differences, which
	 * if done using 64-bit integer arithmetic, would be valid over
	 * only half that span. Since the typical first-order
	 * differences are usually very small, they are converted to 64-
	 * bit doubles and all remaining calculations done in floating-
	 * double arithmetic. This preserves the accuracy while
	 * retaining the 68-year span.
	 *
	 * There are three interleaving schemes, basic, interleaved
	 * symmetric and interleaved broadcast. The timestamps are
	 * idioscyncratically different. See the onwire briefing/white
	 * paper at www.eecis.udel.edu/~mills for details.
	 *
	 * Interleaved symmetric mode
	 * t1 = peer->aorg/borg, t2 = peer->rec, t3 = p_xmt,
	 * t4 = peer->dst
	 */
	if (peer->flip != 0) {
		ci = p_xmt;				/* t3 - t4 */
		L_SUB(&ci, &peer->dst);
		LFPTOD(&ci, t34);
		ci = p_rec;				/* t2 - t1 */
		if (peer->flip > 0)
			L_SUB(&ci, &peer->borg);
		else
			L_SUB(&ci, &peer->aorg);
		LFPTOD(&ci, t21);
		p_del = t21 - t34;
		p_offset = (t21 + t34) / 2.;
		if (p_del < 0 || p_del > 1.) {
			snprintf(statstr, sizeof(statstr),
			    "t21 %.6f t34 %.6f", t21, t34);
			report_event(PEVNT_XERR, peer, statstr);
			return;
		}

	/*
	 * Broadcast modes
	 */
	} else if (peer->pmode == MODE_BROADCAST) {

		/*
		 * Interleaved broadcast mode. Use interleaved timestamps.
		 * t1 = peer->borg, t2 = p_org, t3 = p_org, t4 = aorg
		 */
		if (peer->flags & FLAG_XB) {
			ci = p_org;			/* delay */
			L_SUB(&ci, &peer->aorg);
			LFPTOD(&ci, t34);
			ci = p_org;			/* t2 - t1 */
			L_SUB(&ci, &peer->borg);
			LFPTOD(&ci, t21);
			peer->aorg = p_xmt;
			peer->borg = peer->dst;
			if (t34 < 0 || t34 > 1.) {
				/* drop all if in the initial volley */
				if (FLAG_BC_VOL & peer->flags)
					goto bcc_init_volley_fail;
				snprintf(statstr, sizeof(statstr),
				    "offset %.6f delay %.6f", t21, t34);
				report_event(PEVNT_XERR, peer, statstr);
				return;
			}
			p_offset = t21;
			peer->xleave = t34;

		/*
		 * Basic broadcast - use direct timestamps.
		 * t3 = p_xmt, t4 = peer->dst
		 */
		} else {
			ci = p_xmt;		/* t3 - t4 */
			L_SUB(&ci, &peer->dst);
			LFPTOD(&ci, t34);
			p_offset = t34;
		}

		/*
		 * When calibration is complete and the clock is
		 * synchronized, the bias is calculated as the difference
		 * between the unicast timestamp and the broadcast
		 * timestamp. This works for both basic and interleaved
		 * modes.
		 * [Bug 3031] Don't keep this peer when the delay
		 * calculation gives reason to suspect clock steps.
		 * This is assumed for delays > 50ms.
		 */
		if (FLAG_BC_VOL & peer->flags) {
			peer->flags &= ~FLAG_BC_VOL;
			peer->delay = fabs(peer->offset - p_offset) * 2;
			DPRINTF(2, ("broadcast volley: initial delay=%.6f\n",
				peer->delay));
			if (peer->delay > fabs(sys_bdelay)) {
		bcc_init_volley_fail:
				DPRINTF(2, ("%s", "broadcast volley: initial delay exceeds limit\n"));
				unpeer(peer);
				return;
			}
		}
		peer->nextdate = current_time + (1u << peer->ppoll) - 2u;
		p_del = peer->delay;
		p_offset += p_del / 2;


	/*
	 * Basic mode, otherwise known as the old fashioned way.
	 *
	 * t1 = p_org, t2 = p_rec, t3 = p_xmt, t4 = peer->dst
	 */
	} else {
		ci = p_xmt;				/* t3 - t4 */
		L_SUB(&ci, &peer->dst);
		LFPTOD(&ci, t34);
		ci = p_rec;				/* t2 - t1 */
		L_SUB(&ci, &p_org);
		LFPTOD(&ci, t21);
		p_del = fabs(t21 - t34);
		p_offset = (t21 + t34) / 2.;
	}
	p_del = max(p_del, LOGTOD(sys_precision));
	p_disp = LOGTOD(sys_precision) + LOGTOD(peer->precision) +
	    clock_phi * p_del;

#if ASSYM
	/*
	 * This code calculates the outbound and inbound data rates by
	 * measuring the differences between timestamps at different
	 * packet lengths. This is helpful in cases of large asymmetric
	 * delays commonly experienced on deep space communication
	 * links.
	 */
	if (peer->t21_last > 0 && peer->t34_bytes > 0) {
		itemp = peer->t21_bytes - peer->t21_last;
		if (itemp > 25) {
			etemp = t21 - peer->t21;
			if (fabs(etemp) > 1e-6) {
				ftemp = itemp / etemp;
				if (ftemp > 1000.)
					peer->r21 = ftemp;
			}
		}
		itemp = len - peer->t34_bytes;
		if (itemp > 25) {
			etemp = -t34 - peer->t34;
			if (fabs(etemp) > 1e-6) {
				ftemp = itemp / etemp;
				if (ftemp > 1000.)
					peer->r34 = ftemp;
			}
		}
	}

	/*
	 * The following section compensates for different data rates on
	 * the outbound (d21) and inbound (t34) directions. To do this,
	 * it finds t such that r21 * t - r34 * (d - t) = 0, where d is
	 * the roundtrip delay. Then it calculates the correction as a
	 * fraction of d.
	 */
	peer->t21 = t21;
	peer->t21_last = peer->t21_bytes;
	peer->t34 = -t34;
	peer->t34_bytes = len;
	DPRINTF(2, ("packet: t21 %.9lf %d t34 %.9lf %d\n", peer->t21,
		    peer->t21_bytes, peer->t34, peer->t34_bytes));
	if (peer->r21 > 0 && peer->r34 > 0 && p_del > 0) {
		if (peer->pmode != MODE_BROADCAST)
			td = (peer->r34 / (peer->r21 + peer->r34) -
			    .5) * p_del;
		else
			td = 0;

		/*
		 * Unfortunately, in many cases the errors are
		 * unacceptable, so for the present the rates are not
		 * used. In future, we might find conditions where the
		 * calculations are useful, so this should be considered
		 * a work in progress.
		 */
		t21 -= td;
		t34 -= td;
		DPRINTF(2, ("packet: del %.6lf r21 %.1lf r34 %.1lf %.6lf\n",
			    p_del, peer->r21 / 1e3, peer->r34 / 1e3,
			    td));
	}
#endif /* ASSYM */

	/*
	 * That was awesome. Now hand off to the clock filter.
	 */
	clock_filter(peer, p_offset + peer->bias, p_del, p_disp);

	/*
	 * If we are in broadcast calibrate mode, return to broadcast
	 * client mode when the client is fit and the autokey dance is
	 * complete.
	 */
	if (   (FLAG_BC_VOL & peer->flags)
	    && MODE_CLIENT == peer->hmode
	    && !(TEST11 & peer_unfit(peer))) {	/* distance exceeded */
#ifdef AUTOKEY
		if (peer->flags & FLAG_SKEY) {
			if (!(~peer->crypto & CRYPTO_FLAG_ALL))
				peer->hmode = MODE_BCLIENT;
		} else {
			peer->hmode = MODE_BCLIENT;
		}
#else	/* !AUTOKEY follows */
		peer->hmode = MODE_BCLIENT;
#endif	/* !AUTOKEY */
	}
}


/*
 * clock_update - Called at system process update intervals.
 */
static void
clock_update(
	struct peer *peer	/* peer structure pointer */
	)
{
	double	dtemp;
	l_fp	now;
#ifdef HAVE_LIBSCF_H
	char	*fmri;
#endif /* HAVE_LIBSCF_H */

	/*
	 * Update the system state variables. We do this very carefully,
	 * as the poll interval might need to be clamped differently.
	 */
	sys_peer = peer;
	sys_epoch = peer->epoch;
	if (sys_poll < peer->minpoll)
		sys_poll = peer->minpoll;
	if (sys_poll > peer->maxpoll)
		sys_poll = peer->maxpoll;
	poll_update(peer, sys_poll);
	sys_stratum = min(peer->stratum + 1, STRATUM_UNSPEC);
	if (   peer->stratum == STRATUM_REFCLOCK
	    || peer->stratum == STRATUM_UNSPEC)
		sys_refid = peer->refid;
	else
		sys_refid = addr2refid(&peer->srcadr);
	/*
	 * Root Dispersion (E) is defined (in RFC 5905) as:
	 *
	 * E = p.epsilon_r + p.epsilon + p.psi + PHI*(s.t - p.t) + |THETA|
	 *
	 * where:
	 *  p.epsilon_r is the PollProc's root dispersion
	 *  p.epsilon   is the PollProc's dispersion
	 *  p.psi       is the PollProc's jitter
	 *  THETA       is the combined offset
	 *
	 * NB: Think Hard about where these numbers come from and
	 * what they mean.  When did peer->update happen?  Has anything
	 * interesting happened since then?  What values are the most
	 * defensible?  Why?
	 *
	 * DLM thinks this equation is probably the best of all worse choices.
	 */
	dtemp	= peer->rootdisp
		+ peer->disp
		+ sys_jitter
		+ clock_phi * (current_time - peer->update)
		+ fabs(sys_offset);

	if (dtemp > sys_mindisp)
		sys_rootdisp = dtemp;
	else
		sys_rootdisp = sys_mindisp;
	sys_rootdelay = peer->delay + peer->rootdelay;
	sys_reftime = peer->dst;

	DPRINTF(1, ("clock_update: at %lu sample %lu associd %d\n",
		    current_time, peer->epoch, peer->associd));

	/*
	 * Comes now the moment of truth. Crank the clock discipline and
	 * see what comes out.
	 */
	switch (local_clock(peer, sys_offset)) {

	/*
	 * Clock exceeds panic threshold. Life as we know it ends.
	 */
	case -1:
#ifdef HAVE_LIBSCF_H
		/*
		 * For Solaris enter the maintenance mode.
		 */
		if ((fmri = getenv("SMF_FMRI")) != NULL) {
			if (smf_maintain_instance(fmri, 0) < 0) {
				printf("smf_maintain_instance: %s\n",
				    scf_strerror(scf_error()));
				exit(1);
			}
			/*
			 * Sleep until SMF kills us.
			 */
			for (;;)
				pause();
		}
#endif /* HAVE_LIBSCF_H */
		exit (-1);
		/* not reached */

	/*
	 * Clock was stepped. Flush all time values of all peers.
	 */
	case 2:
		clear_all();
		set_sys_leap(LEAP_NOTINSYNC);
		sys_stratum = STRATUM_UNSPEC;
		memcpy(&sys_refid, "STEP", 4);
		sys_rootdelay = 0;
		sys_rootdisp = 0;
		L_CLR(&sys_reftime);
		sys_jitter = LOGTOD(sys_precision);
		leapsec_reset_frame();
		break;

	/*
	 * Clock was slewed. Handle the leapsecond stuff.
	 */
	case 1:

		/*
		 * If this is the first time the clock is set, reset the
		 * leap bits. If crypto, the timer will goose the setup
		 * process.
		 */
		if (sys_leap == LEAP_NOTINSYNC) {
			set_sys_leap(LEAP_NOWARNING);
#ifdef AUTOKEY
			if (crypto_flags)
				crypto_update();
#endif	/* AUTOKEY */
			/*
			 * If our parent process is waiting for the
			 * first clock sync, send them home satisfied.
			 */
#ifdef HAVE_WORKING_FORK
			if (waitsync_fd_to_close != -1) {
				close(waitsync_fd_to_close);
				waitsync_fd_to_close = -1;
				DPRINTF(1, ("notified parent --wait-sync is done\n"));
			}
#endif /* HAVE_WORKING_FORK */

		}

		/*
		 * If there is no leap second pending and the number of
		 * survivor leap bits is greater than half the number of
		 * survivors, try to schedule a leap for the end of the
		 * current month. (This only works if no leap second for
		 * that range is in the table, so doing this more than
		 * once is mostly harmless.)
		 */
		if (leapsec == LSPROX_NOWARN) {
			if (   leap_vote_ins > leap_vote_del
			    && leap_vote_ins > sys_survivors / 2) {
				get_systime(&now);
				leapsec_add_dyn(TRUE, now.l_ui, NULL);
			}
			if (   leap_vote_del > leap_vote_ins
			    && leap_vote_del > sys_survivors / 2) {
				get_systime(&now);
				leapsec_add_dyn(FALSE, now.l_ui, NULL);
			}
		}
		break;

	/*
	 * Popcorn spike or step threshold exceeded. Pretend it never
	 * happened.
	 */
	default:
		break;
	}
}


/*
 * poll_update - update peer poll interval
 */
void
poll_update(
	struct peer *peer,	/* peer structure pointer */
	u_char	mpoll
	)
{
	u_long	next, utemp;
	u_char	hpoll;

	/*
	 * This routine figures out when the next poll should be sent.
	 * That turns out to be wickedly complicated. One problem is
	 * that sometimes the time for the next poll is in the past when
	 * the poll interval is reduced. We watch out for races here
	 * between the receive process and the poll process.
	 *
	 * Clamp the poll interval between minpoll and maxpoll.
	 */
	hpoll = max(min(peer->maxpoll, mpoll), peer->minpoll);

#ifdef AUTOKEY
	/*
	 * If during the crypto protocol the poll interval has changed,
	 * the lifetimes in the key list are probably bogus. Purge the
	 * the key list and regenerate it later.
	 */
	if ((peer->flags & FLAG_SKEY) && hpoll != peer->hpoll)
		key_expire(peer);
#endif	/* AUTOKEY */
	peer->hpoll = hpoll;

	/*
	 * There are three variables important for poll scheduling, the
	 * current time (current_time), next scheduled time (nextdate)
	 * and the earliest time (utemp). The earliest time is 2 s
	 * seconds, but could be more due to rate management. When
	 * sending in a burst, use the earliest time. When not in a
	 * burst but with a reply pending, send at the earliest time
	 * unless the next scheduled time has not advanced. This can
	 * only happen if multiple replies are pending in the same
	 * response interval. Otherwise, send at the later of the next
	 * scheduled time and the earliest time.
	 *
	 * Now we figure out if there is an override. If a burst is in
	 * progress and we get called from the receive process, just
	 * slink away. If called from the poll process, delay 1 s for a
	 * reference clock, otherwise 2 s.
	 */
	utemp = current_time + max(peer->throttle - (NTP_SHIFT - 1) *
	    (1 << peer->minpoll), ntp_minpkt);
	if (peer->burst > 0) {
		if (peer->nextdate > current_time)
			return;
#ifdef REFCLOCK
		else if (peer->flags & FLAG_REFCLOCK)
			peer->nextdate = current_time + RESP_DELAY;
#endif /* REFCLOCK */
		else
			peer->nextdate = utemp;

#ifdef AUTOKEY
	/*
	 * If a burst is not in progress and a crypto response message
	 * is pending, delay 2 s, but only if this is a new interval.
	 */
	} else if (peer->cmmd != NULL) {
		if (peer->nextdate > current_time) {
			if (peer->nextdate + ntp_minpkt != utemp)
				peer->nextdate = utemp;
		} else {
			peer->nextdate = utemp;
		}
#endif	/* AUTOKEY */

	/*
	 * The ordinary case. If a retry, use minpoll; if unreachable,
	 * use host poll; otherwise, use the minimum of host and peer
	 * polls; In other words, oversampling is okay but
	 * understampling is evil. Use the maximum of this value and the
	 * headway. If the average headway is greater than the headway
	 * threshold, increase the headway by the minimum interval.
	 */
	} else {
		if (peer->retry > 0)
			hpoll = peer->minpoll;
		else
			hpoll = min(peer->ppoll, peer->hpoll);
#ifdef REFCLOCK
		if (peer->flags & FLAG_REFCLOCK)
			next = 1 << hpoll;
		else
#endif /* REFCLOCK */
			next = ((0x1000UL | (ntp_random() & 0x0ff)) <<
			    hpoll) >> 12;
		next += peer->outdate;
		if (next > utemp)
			peer->nextdate = next;
		else
			peer->nextdate = utemp;
		if (peer->throttle > (1 << peer->minpoll))
			peer->nextdate += ntp_minpkt;
	}
	DPRINTF(2, ("poll_update: at %lu %s poll %d burst %d retry %d head %d early %lu next %lu\n",
		    current_time, ntoa(&peer->srcadr), peer->hpoll,
		    peer->burst, peer->retry, peer->throttle,
		    utemp - current_time, peer->nextdate -
		    current_time));
}


/*
 * peer_clear - clear peer filter registers.  See Section 3.4.8 of the
 * spec.
 */
void
peer_clear(
	struct peer *peer,		/* peer structure */
	const char *ident		/* tally lights */
	)
{
	u_char	u;
	l_fp	bxmt = peer->bxmt;	/* bcast clients retain this! */

#ifdef AUTOKEY
	/*
	 * If cryptographic credentials have been acquired, toss them to
	 * Valhalla. Note that autokeys are ephemeral, in that they are
	 * tossed immediately upon use. Therefore, the keylist can be
	 * purged anytime without needing to preserve random keys. Note
	 * that, if the peer is purged, the cryptographic variables are
	 * purged, too. This makes it much harder to sneak in some
	 * unauthenticated data in the clock filter.
	 */
	key_expire(peer);
	if (peer->iffval != NULL)
		BN_free(peer->iffval);
	value_free(&peer->cookval);
	value_free(&peer->recval);
	value_free(&peer->encrypt);
	value_free(&peer->sndval);
	if (peer->cmmd != NULL)
		free(peer->cmmd);
	if (peer->subject != NULL)
		free(peer->subject);
	if (peer->issuer != NULL)
		free(peer->issuer);
#endif /* AUTOKEY */

	/*
	 * Clear all values, including the optional crypto values above.
	 */
	memset(CLEAR_TO_ZERO(peer), 0, LEN_CLEAR_TO_ZERO(peer));
	peer->ppoll = peer->maxpoll;
	peer->hpoll = peer->minpoll;
	peer->disp = MAXDISPERSE;
	peer->flash = peer_unfit(peer);
	peer->jitter = LOGTOD(sys_precision);

	/* Don't throw away our broadcast replay protection */
	if (peer->hmode == MODE_BCLIENT)
		peer->bxmt = bxmt;

	/*
	 * If interleave mode, initialize the alternate origin switch.
	 */
	if (peer->flags & FLAG_XLEAVE)
		peer->flip = 1;
	for (u = 0; u < NTP_SHIFT; u++) {
		peer->filter_order[u] = u;
		peer->filter_disp[u] = MAXDISPERSE;
	}
#ifdef REFCLOCK
	if (!(peer->flags & FLAG_REFCLOCK)) {
#endif
		peer->leap = LEAP_NOTINSYNC;
		peer->stratum = STRATUM_UNSPEC;
		memcpy(&peer->refid, ident, 4);
#ifdef REFCLOCK
	} else {
		/* Clear refclock sample filter */
		peer->procptr->codeproc = 0;
		peer->procptr->coderecv = 0;
	}
#endif

	/*
	 * During initialization use the association count to spread out
	 * the polls at one-second intervals. Passive associations'
	 * first poll is delayed by the "discard minimum" to avoid rate
	 * limiting. Other post-startup new or cleared associations
	 * randomize the first poll over the minimum poll interval to
	 * avoid implosion.
	 */
	peer->nextdate = peer->update = peer->outdate = current_time;
	if (initializing) {
		peer->nextdate += peer_associations;
	} else if (MODE_PASSIVE == peer->hmode) {
		peer->nextdate += ntp_minpkt;
	} else {
		peer->nextdate += ntp_random() % peer->minpoll;
	}
#ifdef AUTOKEY
	peer->refresh = current_time + (1 << NTP_REFRESH);
#endif	/* AUTOKEY */
	DPRINTF(1, ("peer_clear: at %ld next %ld associd %d refid %s\n",
		    current_time, peer->nextdate, peer->associd,
		    ident));
}


/*
 * clock_filter - add incoming clock sample to filter register and run
 *		  the filter procedure to find the best sample.
 */
void
clock_filter(
	struct peer *peer,		/* peer structure pointer */
	double	sample_offset,		/* clock offset */
	double	sample_delay,		/* roundtrip delay */
	double	sample_disp		/* dispersion */
	)
{
	double	dst[NTP_SHIFT];		/* distance vector */
	int	ord[NTP_SHIFT];		/* index vector */
	int	i, j, k, m;
	double	dtemp, etemp;
	char	tbuf[80];

	/*
	 * A sample consists of the offset, delay, dispersion and epoch
	 * of arrival. The offset and delay are determined by the on-
	 * wire protocol. The dispersion grows from the last outbound
	 * packet to the arrival of this one increased by the sum of the
	 * peer precision and the system precision as required by the
	 * error budget. First, shift the new arrival into the shift
	 * register discarding the oldest one.
	 */
	j = peer->filter_nextpt;
	peer->filter_offset[j] = sample_offset;
	peer->filter_delay[j] = sample_delay;
	peer->filter_disp[j] = sample_disp;
	peer->filter_epoch[j] = current_time;
	j = (j + 1) % NTP_SHIFT;
	peer->filter_nextpt = j;

	/*
	 * Update dispersions since the last update and at the same
	 * time initialize the distance and index lists. Since samples
	 * become increasingly uncorrelated beyond the Allan intercept,
	 * only under exceptional cases will an older sample be used.
	 * Therefore, the distance list uses a compound metric. If the
	 * dispersion is greater than the maximum dispersion, clamp the
	 * distance at that value. If the time since the last update is
	 * less than the Allan intercept use the delay; otherwise, use
	 * the sum of the delay and dispersion.
	 */
	dtemp = clock_phi * (current_time - peer->update);
	peer->update = current_time;
	for (i = NTP_SHIFT - 1; i >= 0; i--) {
		if (i != 0)
			peer->filter_disp[j] += dtemp;
		if (peer->filter_disp[j] >= MAXDISPERSE) {
			peer->filter_disp[j] = MAXDISPERSE;
			dst[i] = MAXDISPERSE;
		} else if (peer->update - peer->filter_epoch[j] >
		    (u_long)ULOGTOD(allan_xpt)) {
			dst[i] = peer->filter_delay[j] +
			    peer->filter_disp[j];
		} else {
			dst[i] = peer->filter_delay[j];
		}
		ord[i] = j;
		j = (j + 1) % NTP_SHIFT;
	}

	/*
	 * If the clock has stabilized, sort the samples by distance.
	 */
	if (freq_cnt == 0) {
		for (i = 1; i < NTP_SHIFT; i++) {
			for (j = 0; j < i; j++) {
				if (dst[j] > dst[i]) {
					k = ord[j];
					ord[j] = ord[i];
					ord[i] = k;
					etemp = dst[j];
					dst[j] = dst[i];
					dst[i] = etemp;
				}
			}
		}
	}

	/*
	 * Copy the index list to the association structure so ntpq
	 * can see it later. Prune the distance list to leave only
	 * samples less than the maximum dispersion, which disfavors
	 * uncorrelated samples older than the Allan intercept. To
	 * further improve the jitter estimate, of the remainder leave
	 * only samples less than the maximum distance, but keep at
	 * least two samples for jitter calculation.
	 */
	m = 0;
	for (i = 0; i < NTP_SHIFT; i++) {
		peer->filter_order[i] = (u_char) ord[i];
		if (   dst[i] >= MAXDISPERSE
		    || (m >= 2 && dst[i] >= sys_maxdist))
			continue;
		m++;
	}

	/*
	 * Compute the dispersion and jitter. The dispersion is weighted
	 * exponentially by NTP_FWEIGHT (0.5) so it is normalized close
	 * to 1.0. The jitter is the RMS differences relative to the
	 * lowest delay sample.
	 */
	peer->disp = peer->jitter = 0;
	k = ord[0];
	for (i = NTP_SHIFT - 1; i >= 0; i--) {
		j = ord[i];
		peer->disp = NTP_FWEIGHT * (peer->disp +
		    peer->filter_disp[j]);
		if (i < m)
			peer->jitter += DIFF(peer->filter_offset[j],
			    peer->filter_offset[k]);
	}

	/*
	 * If no acceptable samples remain in the shift register,
	 * quietly tiptoe home leaving only the dispersion. Otherwise,
	 * save the offset, delay and jitter. Note the jitter must not
	 * be less than the precision.
	 */
	if (m == 0) {
		clock_select();
		return;
	}
	etemp = fabs(peer->offset - peer->filter_offset[k]);
	peer->offset = peer->filter_offset[k];
	peer->delay = peer->filter_delay[k];
	if (m > 1)
		peer->jitter /= m - 1;
	peer->jitter = max(SQRT(peer->jitter), LOGTOD(sys_precision));

	/*
	 * If the the new sample and the current sample are both valid
	 * and the difference between their offsets exceeds CLOCK_SGATE
	 * (3) times the jitter and the interval between them is less
	 * than twice the host poll interval, consider the new sample
	 * a popcorn spike and ignore it.
	 */
	if (   peer->disp < sys_maxdist
	    && peer->filter_disp[k] < sys_maxdist
	    && etemp > CLOCK_SGATE * peer->jitter
	    && peer->filter_epoch[k] - peer->epoch
	       < 2. * ULOGTOD(peer->hpoll)) {
		snprintf(tbuf, sizeof(tbuf), "%.6f s", etemp);
		report_event(PEVNT_POPCORN, peer, tbuf);
		return;
	}

	/*
	 * A new minimum sample is useful only if it is later than the
	 * last one used. In this design the maximum lifetime of any
	 * sample is not greater than eight times the poll interval, so
	 * the maximum interval between minimum samples is eight
	 * packets.
	 */
	if (peer->filter_epoch[k] <= peer->epoch) {
	DPRINTF(2, ("clock_filter: old sample %lu\n", current_time -
		    peer->filter_epoch[k]));
		return;
	}
	peer->epoch = peer->filter_epoch[k];

	/*
	 * The mitigated sample statistics are saved for later
	 * processing. If not synchronized or not in a burst, tickle the
	 * clock select algorithm.
	 */
	record_peer_stats(&peer->srcadr, ctlpeerstatus(peer),
	    peer->offset, peer->delay, peer->disp, peer->jitter);
	DPRINTF(1, ("clock_filter: n %d off %.6f del %.6f dsp %.6f jit %.6f\n",
		    m, peer->offset, peer->delay, peer->disp,
		    peer->jitter));
	if (peer->burst == 0 || sys_leap == LEAP_NOTINSYNC)
		clock_select();
}


/*
 * clock_select - find the pick-of-the-litter clock
 *
 * LOCKCLOCK: (1) If the local clock is the prefer peer, it will always
 * be enabled, even if declared falseticker, (2) only the prefer peer
 * can be selected as the system peer, (3) if the external source is
 * down, the system leap bits are set to 11 and the stratum set to
 * infinity.
 */
void
clock_select(void)
{
	struct peer *peer;
	int	i, j, k, n;
	int	nlist, nl2;
	int	allow;
	int	speer;
	double	d, e, f, g;
	double	high, low;
	double	speermet;
	double	orphmet = 2.0 * U_INT32_MAX; /* 2x is greater than */
	struct endpoint endp;
	struct peer *osys_peer;
	struct peer *sys_prefer = NULL;	/* prefer peer */
	struct peer *typesystem = NULL;
	struct peer *typeorphan = NULL;
#ifdef REFCLOCK
	struct peer *typeacts = NULL;
	struct peer *typelocal = NULL;
	struct peer *typepps = NULL;
#endif /* REFCLOCK */
	static struct endpoint *endpoint = NULL;
	static int *indx = NULL;
	static peer_select *peers = NULL;
	static u_int endpoint_size = 0;
	static u_int peers_size = 0;
	static u_int indx_size = 0;
	size_t octets;

	/*
	 * Initialize and create endpoint, index and peer lists big
	 * enough to handle all associations.
	 */
	osys_peer = sys_peer;
	sys_survivors = 0;
#ifdef LOCKCLOCK
	set_sys_leap(LEAP_NOTINSYNC);
	sys_stratum = STRATUM_UNSPEC;
	memcpy(&sys_refid, "DOWN", 4);
#endif /* LOCKCLOCK */

	/*
	 * Allocate dynamic space depending on the number of
	 * associations.
	 */
	nlist = 1;
	for (peer = peer_list; peer != NULL; peer = peer->p_link)
		nlist++;
	endpoint_size = ALIGNED_SIZE(nlist * 2 * sizeof(*endpoint));
	peers_size = ALIGNED_SIZE(nlist * sizeof(*peers));
	indx_size = ALIGNED_SIZE(nlist * 2 * sizeof(*indx));
	octets = endpoint_size + peers_size + indx_size;
	endpoint = erealloc(endpoint, octets);
	peers = INC_ALIGNED_PTR(endpoint, endpoint_size);
	indx = INC_ALIGNED_PTR(peers, peers_size);

	/*
	 * Initially, we populate the island with all the rifraff peers
	 * that happen to be lying around. Those with seriously
	 * defective clocks are immediately booted off the island. Then,
	 * the falsetickers are culled and put to sea. The truechimers
	 * remaining are subject to repeated rounds where the most
	 * unpopular at each round is kicked off. When the population
	 * has dwindled to sys_minclock, the survivors split a million
	 * bucks and collectively crank the chimes.
	 */
	nlist = nl2 = 0;	/* none yet */
	for (peer = peer_list; peer != NULL; peer = peer->p_link) {
		peer->new_status = CTL_PST_SEL_REJECT;

		/*
		 * Leave the island immediately if the peer is
		 * unfit to synchronize.
		 */
		if (peer_unfit(peer)) {
			continue;
		}

		/*
		 * If this peer is an orphan parent, elect the
		 * one with the lowest metric defined as the
		 * IPv4 address or the first 64 bits of the
		 * hashed IPv6 address.  To ensure convergence
		 * on the same selected orphan, consider as
		 * well that this system may have the lowest
		 * metric and be the orphan parent.  If this
		 * system wins, sys_peer will be NULL to trigger
		 * orphan mode in timer().
		 */
		if (peer->stratum == sys_orphan) {
			u_int32	localmet;
			u_int32 peermet;

			if (peer->dstadr != NULL)
				localmet = ntohl(peer->dstadr->addr_refid);
			else
				localmet = U_INT32_MAX;
			peermet = ntohl(addr2refid(&peer->srcadr));
			if (peermet < localmet && peermet < orphmet) {
				typeorphan = peer;
				orphmet = peermet;
			}
			continue;
		}

		/*
		 * If this peer could have the orphan parent
		 * as a synchronization ancestor, exclude it
		 * from selection to avoid forming a
		 * synchronization loop within the orphan mesh,
		 * triggering stratum climb to infinity
		 * instability.  Peers at stratum higher than
		 * the orphan stratum could have the orphan
		 * parent in ancestry so are excluded.
		 * See http://bugs.ntp.org/2050
		 */
		if (peer->stratum > sys_orphan) {
			continue;
		}
#ifdef REFCLOCK
		/*
		 * The following are special cases. We deal
		 * with them later.
		 */
		if (!(peer->flags & FLAG_PREFER)) {
			switch (peer->refclktype) {
			case REFCLK_LOCALCLOCK:
				if (   current_time > orphwait
				    && typelocal == NULL)
					typelocal = peer;
				continue;

			case REFCLK_ACTS:
				if (   current_time > orphwait
				    && typeacts == NULL)
					typeacts = peer;
				continue;
			}
		}
#endif /* REFCLOCK */

		/*
		 * If we get this far, the peer can stay on the
		 * island, but does not yet have the immunity
		 * idol.
		 */
		peer->new_status = CTL_PST_SEL_SANE;
		f = root_distance(peer);
		peers[nlist].peer = peer;
		peers[nlist].error = peer->jitter;
		peers[nlist].synch = f;
		nlist++;

		/*
		 * Insert each interval endpoint on the unsorted
		 * endpoint[] list.
		 */
		e = peer->offset;
		endpoint[nl2].type = -1;	/* lower end */
		endpoint[nl2].val = e - f;
		nl2++;
		endpoint[nl2].type = 1;		/* upper end */
		endpoint[nl2].val = e + f;
		nl2++;
	}
	/*
	 * Construct sorted indx[] of endpoint[] indexes ordered by
	 * offset.
	 */
	for (i = 0; i < nl2; i++)
		indx[i] = i;
	for (i = 0; i < nl2; i++) {
		endp = endpoint[indx[i]];
		e = endp.val;
		k = i;
		for (j = i + 1; j < nl2; j++) {
			endp = endpoint[indx[j]];
			if (endp.val < e) {
				e = endp.val;
				k = j;
			}
		}
		if (k != i) {
			j = indx[k];
			indx[k] = indx[i];
			indx[i] = j;
		}
	}
	for (i = 0; i < nl2; i++)
		DPRINTF(3, ("select: endpoint %2d %.6f\n",
			endpoint[indx[i]].type, endpoint[indx[i]].val));

	/*
	 * This is the actual algorithm that cleaves the truechimers
	 * from the falsetickers. The original algorithm was described
	 * in Keith Marzullo's dissertation, but has been modified for
	 * better accuracy.
	 *
	 * Briefly put, we first assume there are no falsetickers, then
	 * scan the candidate list first from the low end upwards and
	 * then from the high end downwards. The scans stop when the
	 * number of intersections equals the number of candidates less
	 * the number of falsetickers. If this doesn't happen for a
	 * given number of falsetickers, we bump the number of
	 * falsetickers and try again. If the number of falsetickers
	 * becomes equal to or greater than half the number of
	 * candidates, the Albanians have won the Byzantine wars and
	 * correct synchronization is not possible.
	 *
	 * Here, nlist is the number of candidates and allow is the
	 * number of falsetickers. Upon exit, the truechimers are the
	 * survivors with offsets not less than low and not greater than
	 * high. There may be none of them.
	 */
	low = 1e9;
	high = -1e9;
	for (allow = 0; 2 * allow < nlist; allow++) {

		/*
		 * Bound the interval (low, high) as the smallest
		 * interval containing points from the most sources.
		 */
		n = 0;
		for (i = 0; i < nl2; i++) {
			low = endpoint[indx[i]].val;
			n -= endpoint[indx[i]].type;
			if (n >= nlist - allow)
				break;
		}
		n = 0;
		for (j = nl2 - 1; j >= 0; j--) {
			high = endpoint[indx[j]].val;
			n += endpoint[indx[j]].type;
			if (n >= nlist - allow)
				break;
		}

		/*
		 * If an interval containing truechimers is found, stop.
		 * If not, increase the number of falsetickers and go
		 * around again.
		 */
		if (high > low)
			break;
	}

	/*
	 * Clustering algorithm. Whittle candidate list of falsetickers,
	 * who leave the island immediately. The TRUE peer is always a
	 * truechimer. We must leave at least one peer to collect the
	 * million bucks.
	 *
	 * We assert the correct time is contained in the interval, but
	 * the best offset estimate for the interval might not be
	 * contained in the interval. For this purpose, a truechimer is
	 * defined as the midpoint of an interval that overlaps the
	 * intersection interval.
	 */
	j = 0;
	for (i = 0; i < nlist; i++) {
		double	h;

		peer = peers[i].peer;
		h = peers[i].synch;
		if ((   high <= low
		     || peer->offset + h < low
		     || peer->offset - h > high
		    ) && !(peer->flags & FLAG_TRUE))
			continue;

#ifdef REFCLOCK
		/*
		 * Eligible PPS peers must survive the intersection
		 * algorithm. Use the first one found, but don't
		 * include any of them in the cluster population.
		 */
		if (peer->flags & FLAG_PPS) {
			if (typepps == NULL)
				typepps = peer;
			if (!(peer->flags & FLAG_TSTAMP_PPS))
				continue;
		}
#endif /* REFCLOCK */

		if (j != i)
			peers[j] = peers[i];
		j++;
	}
	nlist = j;

	/*
	 * If no survivors remain at this point, check if the modem
	 * driver, local driver or orphan parent in that order. If so,
	 * nominate the first one found as the only survivor.
	 * Otherwise, give up and leave the island to the rats.
	 */
	if (nlist == 0) {
		peers[0].error = 0;
		peers[0].synch = sys_mindisp;
#ifdef REFCLOCK
		if (typeacts != NULL) {
			peers[0].peer = typeacts;
			nlist = 1;
		} else if (typelocal != NULL) {
			peers[0].peer = typelocal;
			nlist = 1;
		} else
#endif /* REFCLOCK */
		if (typeorphan != NULL) {
			peers[0].peer = typeorphan;
			nlist = 1;
		}
	}

	/*
	 * Mark the candidates at this point as truechimers.
	 */
	for (i = 0; i < nlist; i++) {
		peers[i].peer->new_status = CTL_PST_SEL_SELCAND;
		DPRINTF(2, ("select: survivor %s %f\n",
			stoa(&peers[i].peer->srcadr), peers[i].synch));
	}

	/*
	 * Now, vote outliers off the island by select jitter weighted
	 * by root distance. Continue voting as long as there are more
	 * than sys_minclock survivors and the select jitter of the peer
	 * with the worst metric is greater than the minimum peer
	 * jitter. Stop if we are about to discard a TRUE or PREFER
	 * peer, who of course have the immunity idol.
	 */
	while (1) {
		d = 1e9;
		e = -1e9;
		g = 0;
		k = 0;
		for (i = 0; i < nlist; i++) {
			if (peers[i].error < d)
				d = peers[i].error;
			peers[i].seljit = 0;
			if (nlist > 1) {
				f = 0;
				for (j = 0; j < nlist; j++)
					f += DIFF(peers[j].peer->offset,
					    peers[i].peer->offset);
				peers[i].seljit = SQRT(f / (nlist - 1));
			}
			if (peers[i].seljit * peers[i].synch > e) {
				g = peers[i].seljit;
				e = peers[i].seljit * peers[i].synch;
				k = i;
			}
		}
		g = max(g, LOGTOD(sys_precision));
		if (   nlist <= max(1, sys_minclock)
		    || g <= d
		    || ((FLAG_TRUE | FLAG_PREFER) & peers[k].peer->flags))
			break;

		DPRINTF(3, ("select: drop %s seljit %.6f jit %.6f\n",
			ntoa(&peers[k].peer->srcadr), g, d));
		if (nlist > sys_maxclock)
			peers[k].peer->new_status = CTL_PST_SEL_EXCESS;
		for (j = k + 1; j < nlist; j++)
			peers[j - 1] = peers[j];
		nlist--;
	}

	/*
	 * What remains is a list usually not greater than sys_minclock
	 * peers. Note that unsynchronized peers cannot survive this
	 * far.  Count and mark these survivors.
	 *
	 * While at it, count the number of leap warning bits found.
	 * This will be used later to vote the system leap warning bit.
	 * If a leap warning bit is found on a reference clock, the vote
	 * is always won.
	 *
	 * Choose the system peer using a hybrid metric composed of the
	 * selection jitter scaled by the root distance augmented by
	 * stratum scaled by sys_mindisp (.001 by default). The goal of
	 * the small stratum factor is to avoid clockhop between a
	 * reference clock and a network peer which has a refclock and
	 * is using an older ntpd, which does not floor sys_rootdisp at
	 * sys_mindisp.
	 *
	 * In contrast, ntpd 4.2.6 and earlier used stratum primarily
	 * in selecting the system peer, using a weight of 1 second of
	 * additional root distance per stratum.  This heavy bias is no
	 * longer appropriate, as the scaled root distance provides a
	 * more rational metric carrying the cumulative error budget.
	 */
	e = 1e9;
	speer = 0;
	leap_vote_ins = 0;
	leap_vote_del = 0;
	for (i = 0; i < nlist; i++) {
		peer = peers[i].peer;
		peer->unreach = 0;
		peer->new_status = CTL_PST_SEL_SYNCCAND;
		sys_survivors++;
		if (peer->leap == LEAP_ADDSECOND) {
			if (peer->flags & FLAG_REFCLOCK)
				leap_vote_ins = nlist;
			else if (leap_vote_ins < nlist)
				leap_vote_ins++;
		}
		if (peer->leap == LEAP_DELSECOND) {
			if (peer->flags & FLAG_REFCLOCK)
				leap_vote_del = nlist;
			else if (leap_vote_del < nlist)
				leap_vote_del++;
		}
		if (peer->flags & FLAG_PREFER)
			sys_prefer = peer;
		speermet = peers[i].seljit * peers[i].synch +
		    peer->stratum * sys_mindisp;
		if (speermet < e) {
			e = speermet;
			speer = i;
		}
	}

	/*
	 * Unless there are at least sys_misane survivors, leave the
	 * building dark. Otherwise, do a clockhop dance. Ordinarily,
	 * use the selected survivor speer. However, if the current
	 * system peer is not speer, stay with the current system peer
	 * as long as it doesn't get too old or too ugly.
	 */
	if (nlist > 0 && nlist >= sys_minsane) {
		double	x;

		typesystem = peers[speer].peer;
		if (osys_peer == NULL || osys_peer == typesystem) {
			sys_clockhop = 0;
		} else if ((x = fabs(typesystem->offset -
		    osys_peer->offset)) < sys_mindisp) {
			if (sys_clockhop == 0)
				sys_clockhop = sys_mindisp;
			else
				sys_clockhop *= .5;
			DPRINTF(1, ("select: clockhop %d %.6f %.6f\n",
				j, x, sys_clockhop));
			if (fabs(x) < sys_clockhop)
				typesystem = osys_peer;
			else
				sys_clockhop = 0;
		} else {
			sys_clockhop = 0;
		}
	}

	/*
	 * Mitigation rules of the game. We have the pick of the
	 * litter in typesystem if any survivors are left. If
	 * there is a prefer peer, use its offset and jitter.
	 * Otherwise, use the combined offset and jitter of all kitters.
	 */
	if (typesystem != NULL) {
		if (sys_prefer == NULL) {
			typesystem->new_status = CTL_PST_SEL_SYSPEER;
			clock_combine(peers, sys_survivors, speer);
		} else {
			typesystem = sys_prefer;
			sys_clockhop = 0;
			typesystem->new_status = CTL_PST_SEL_SYSPEER;
			sys_offset = typesystem->offset;
			sys_jitter = typesystem->jitter;
		}
		DPRINTF(1, ("select: combine offset %.9f jitter %.9f\n",
			sys_offset, sys_jitter));
	}
#ifdef REFCLOCK
	/*
	 * If a PPS driver is lit and the combined offset is less than
	 * 0.4 s, select the driver as the PPS peer and use its offset
	 * and jitter. However, if this is the atom driver, use it only
	 * if there is a prefer peer or there are no survivors and none
	 * are required.
	 */
	if (   typepps != NULL
	    && fabs(sys_offset) < 0.4
	    && (   typepps->refclktype != REFCLK_ATOM_PPS
		|| (   typepps->refclktype == REFCLK_ATOM_PPS
		    && (   sys_prefer != NULL
			|| (typesystem == NULL && sys_minsane == 0))))) {
		typesystem = typepps;
		sys_clockhop = 0;
		typesystem->new_status = CTL_PST_SEL_PPS;
		sys_offset = typesystem->offset;
		sys_jitter = typesystem->jitter;
		DPRINTF(1, ("select: pps offset %.9f jitter %.9f\n",
			sys_offset, sys_jitter));
	}
#endif /* REFCLOCK */

	/*
	 * If there are no survivors at this point, there is no
	 * system peer. If so and this is an old update, keep the
	 * current statistics, but do not update the clock.
	 */
	if (typesystem == NULL) {
		if (osys_peer != NULL) {
			if (sys_orphwait > 0)
				orphwait = current_time + sys_orphwait;
			report_event(EVNT_NOPEER, NULL, NULL);
		}
		sys_peer = NULL;
		for (peer = peer_list; peer != NULL; peer = peer->p_link)
			peer->status = peer->new_status;
		return;
	}

	/*
	 * Do not use old data, as this may mess up the clock discipline
	 * stability.
	 */
	if (typesystem->epoch <= sys_epoch)
		return;

	/*
	 * We have found the alpha male. Wind the clock.
	 */
	if (osys_peer != typesystem)
		report_event(PEVNT_NEWPEER, typesystem, NULL);
	for (peer = peer_list; peer != NULL; peer = peer->p_link)
		peer->status = peer->new_status;
	clock_update(typesystem);
}


static void
clock_combine(
	peer_select *	peers,	/* survivor list */
	int		npeers,	/* number of survivors */
	int		syspeer	/* index of sys.peer */
	)
{
	int	i;
	double	x, y, z, w;

	y = z = w = 0;
	for (i = 0; i < npeers; i++) {
		x = 1. / peers[i].synch;
		y += x;
		z += x * peers[i].peer->offset;
		w += x * DIFF(peers[i].peer->offset,
		    peers[syspeer].peer->offset);
	}
	sys_offset = z / y;
	sys_jitter = SQRT(w / y + SQUARE(peers[syspeer].seljit));
}


/*
 * root_distance - compute synchronization distance from peer to root
 */
static double
root_distance(
	struct peer *peer	/* peer structure pointer */
	)
{
	double	dtemp;

	/*
	 * Root Distance (LAMBDA) is defined as:
	 * (delta + DELTA)/2 + epsilon + EPSILON + D
	 *
	 * where:
	 *  delta   is the round-trip delay
	 *  DELTA   is the root delay
	 *  epsilon is the peer dispersion
	 *	    + (15 usec each second)
	 *  EPSILON is the root dispersion
	 *  D       is sys_jitter
	 *
	 * NB: Think hard about why we are using these values, and what
	 * the alternatives are, and the various pros/cons.
	 *
	 * DLM thinks these are probably the best choices from any of the
	 * other worse choices.
	 */
	dtemp = (peer->delay + peer->rootdelay) / 2
		+ peer->disp
		  + clock_phi * (current_time - peer->update)
		+ peer->rootdisp
		+ peer->jitter;
	/*
	 * Careful squeak here. The value returned must be greater than
	 * the minimum root dispersion in order to avoid clockhop with
	 * highly precise reference clocks. Note that the root distance
	 * cannot exceed the sys_maxdist, as this is the cutoff by the
	 * selection algorithm.
	 */
	if (dtemp < sys_mindisp)
		dtemp = sys_mindisp;
	return (dtemp);
}


/*
 * peer_xmit - send packet for persistent association.
 */
static void
peer_xmit(
	struct peer *peer	/* peer structure pointer */
	)
{
	struct pkt xpkt;	/* transmit packet */
	size_t	sendlen, authlen;
	keyid_t	xkeyid = 0;	/* transmit key ID */
	l_fp	xmt_tx, xmt_ty;

	if (!peer->dstadr)	/* drop peers without interface */
		return;

	xpkt.li_vn_mode = PKT_LI_VN_MODE(sys_leap, peer->version,
	    peer->hmode);
	xpkt.stratum = STRATUM_TO_PKT(sys_stratum);
	xpkt.ppoll = peer->hpoll;
	xpkt.precision = sys_precision;
	xpkt.refid = sys_refid;
	xpkt.rootdelay = HTONS_FP(DTOFP(sys_rootdelay));
	xpkt.rootdisp =  HTONS_FP(DTOUFP(sys_rootdisp));
	HTONL_FP(&sys_reftime, &xpkt.reftime);
	HTONL_FP(&peer->rec, &xpkt.org);
	HTONL_FP(&peer->dst, &xpkt.rec);

	/*
	 * If the received packet contains a MAC, the transmitted packet
	 * is authenticated and contains a MAC. If not, the transmitted
	 * packet is not authenticated.
	 *
	 * It is most important when autokey is in use that the local
	 * interface IP address be known before the first packet is
	 * sent. Otherwise, it is not possible to compute a correct MAC
	 * the recipient will accept. Thus, the I/O semantics have to do
	 * a little more work. In particular, the wildcard interface
	 * might not be usable.
	 */
	sendlen = LEN_PKT_NOMAC;
	if (
#ifdef AUTOKEY
	    !(peer->flags & FLAG_SKEY) &&
#endif	/* !AUTOKEY */
	    peer->keyid == 0) {

		/*
		 * Transmit a-priori timestamps
		 */
		get_systime(&xmt_tx);
		if (peer->flip == 0) {	/* basic mode */
			peer->aorg = xmt_tx;
			HTONL_FP(&xmt_tx, &xpkt.xmt);
		} else {		/* interleaved modes */
			if (peer->hmode == MODE_BROADCAST) { /* bcst */
				HTONL_FP(&xmt_tx, &xpkt.xmt);
				if (peer->flip > 0)
					HTONL_FP(&peer->borg,
					    &xpkt.org);
				else
					HTONL_FP(&peer->aorg,
					    &xpkt.org);
			} else {	/* symmetric */
				if (peer->flip > 0)
					HTONL_FP(&peer->borg,
					    &xpkt.xmt);
				else
					HTONL_FP(&peer->aorg,
					    &xpkt.xmt);
			}
		}
		peer->t21_bytes = sendlen;
		sendpkt(&peer->srcadr, peer->dstadr,
			sys_ttl[(peer->ttl >= sys_ttlmax) ? sys_ttlmax : peer->ttl],
			&xpkt, sendlen);
		peer->sent++;
		peer->throttle += (1 << peer->minpoll) - 2;

		/*
		 * Capture a-posteriori timestamps
		 */
		get_systime(&xmt_ty);
		if (peer->flip != 0) {		/* interleaved modes */
			if (peer->flip > 0)
				peer->aorg = xmt_ty;
			else
				peer->borg = xmt_ty;
			peer->flip = -peer->flip;
		}
		L_SUB(&xmt_ty, &xmt_tx);
		LFPTOD(&xmt_ty, peer->xleave);
		DPRINTF(1, ("peer_xmit: at %ld %s->%s mode %d len %zu xmt %#010x.%08x\n",
			    current_time,
			    peer->dstadr ? stoa(&peer->dstadr->sin) : "-",
			    stoa(&peer->srcadr), peer->hmode, sendlen,
			    xmt_tx.l_ui, xmt_tx.l_uf));
		return;
	}

	/*
	 * Authentication is enabled, so the transmitted packet must be
	 * authenticated. If autokey is enabled, fuss with the various
	 * modes; otherwise, symmetric key cryptography is used.
	 */
#ifdef AUTOKEY
	if (peer->flags & FLAG_SKEY) {
		struct exten *exten;	/* extension field */

		/*
		 * The Public Key Dance (PKD): Cryptographic credentials
		 * are contained in extension fields, each including a
		 * 4-octet length/code word followed by a 4-octet
		 * association ID and optional additional data. Optional
		 * data includes a 4-octet data length field followed by
		 * the data itself. Request messages are sent from a
		 * configured association; response messages can be sent
		 * from a configured association or can take the fast
		 * path without ever matching an association. Response
		 * messages have the same code as the request, but have
		 * a response bit and possibly an error bit set. In this
		 * implementation, a message may contain no more than
		 * one command and one or more responses.
		 *
		 * Cryptographic session keys include both a public and
		 * a private componet. Request and response messages
		 * using extension fields are always sent with the
		 * private component set to zero. Packets without
		 * extension fields indlude the private component when
		 * the session key is generated.
		 */
		while (1) {

			/*
			 * Allocate and initialize a keylist if not
			 * already done. Then, use the list in inverse
			 * order, discarding keys once used. Keep the
			 * latest key around until the next one, so
			 * clients can use client/server packets to
			 * compute propagation delay.
			 *
			 * Note that once a key is used from the list,
			 * it is retained in the key cache until the
			 * next key is used. This is to allow a client
			 * to retrieve the encrypted session key
			 * identifier to verify authenticity.
			 *
			 * If for some reason a key is no longer in the
			 * key cache, a birthday has happened or the key
			 * has expired, so the pseudo-random sequence is
			 * broken. In that case, purge the keylist and
			 * regenerate it.
			 */
			if (peer->keynumber == 0)
				make_keylist(peer, peer->dstadr);
			else
				peer->keynumber--;
			xkeyid = peer->keylist[peer->keynumber];
			if (authistrusted(xkeyid))
				break;
			else
				key_expire(peer);
		}
		peer->keyid = xkeyid;
		exten = NULL;
		switch (peer->hmode) {

		/*
		 * In broadcast server mode the autokey values are
		 * required by the broadcast clients. Push them when a
		 * new keylist is generated; otherwise, push the
		 * association message so the client can request them at
		 * other times.
		 */
		case MODE_BROADCAST:
			if (peer->flags & FLAG_ASSOC)
				exten = crypto_args(peer, CRYPTO_AUTO |
				    CRYPTO_RESP, peer->associd, NULL);
			else
				exten = crypto_args(peer, CRYPTO_ASSOC |
				    CRYPTO_RESP, peer->associd, NULL);
			break;

		/*
		 * In symmetric modes the parameter, certificate,
		 * identity, cookie and autokey exchanges are
		 * required. The leapsecond exchange is optional. But, a
		 * peer will not believe the other peer until the other
		 * peer has synchronized, so the certificate exchange
		 * might loop until then. If a peer finds a broken
		 * autokey sequence, it uses the autokey exchange to
		 * retrieve the autokey values. In any case, if a new
		 * keylist is generated, the autokey values are pushed.
		 */
		case MODE_ACTIVE:
		case MODE_PASSIVE:

			/*
			 * Parameter, certificate and identity.
			 */
			if (!peer->crypto)
				exten = crypto_args(peer, CRYPTO_ASSOC,
				    peer->associd, hostval.ptr);
			else if (!(peer->crypto & CRYPTO_FLAG_CERT))
				exten = crypto_args(peer, CRYPTO_CERT,
				    peer->associd, peer->issuer);
			else if (!(peer->crypto & CRYPTO_FLAG_VRFY))
				exten = crypto_args(peer,
				    crypto_ident(peer), peer->associd,
				    NULL);

			/*
			 * Cookie and autokey. We request the cookie
			 * only when the this peer and the other peer
			 * are synchronized. But, this peer needs the
			 * autokey values when the cookie is zero. Any
			 * time we regenerate the key list, we offer the
			 * autokey values without being asked. If for
			 * some reason either peer finds a broken
			 * autokey sequence, the autokey exchange is
			 * used to retrieve the autokey values.
			 */
			else if (   sys_leap != LEAP_NOTINSYNC
				 && peer->leap != LEAP_NOTINSYNC
				 && !(peer->crypto & CRYPTO_FLAG_COOK))
				exten = crypto_args(peer, CRYPTO_COOK,
				    peer->associd, NULL);
			else if (!(peer->crypto & CRYPTO_FLAG_AUTO))
				exten = crypto_args(peer, CRYPTO_AUTO,
				    peer->associd, NULL);
			else if (   peer->flags & FLAG_ASSOC
				 && peer->crypto & CRYPTO_FLAG_SIGN)
				exten = crypto_args(peer, CRYPTO_AUTO |
				    CRYPTO_RESP, peer->assoc, NULL);

			/*
			 * Wait for clock sync, then sign the
			 * certificate and retrieve the leapsecond
			 * values.
			 */
			else if (sys_leap == LEAP_NOTINSYNC)
				break;

			else if (!(peer->crypto & CRYPTO_FLAG_SIGN))
				exten = crypto_args(peer, CRYPTO_SIGN,
				    peer->associd, hostval.ptr);
			else if (!(peer->crypto & CRYPTO_FLAG_LEAP))
				exten = crypto_args(peer, CRYPTO_LEAP,
				    peer->associd, NULL);
			break;

		/*
		 * In client mode the parameter, certificate, identity,
		 * cookie and sign exchanges are required. The
		 * leapsecond exchange is optional. If broadcast client
		 * mode the same exchanges are required, except that the
		 * autokey exchange is substitutes for the cookie
		 * exchange, since the cookie is always zero. If the
		 * broadcast client finds a broken autokey sequence, it
		 * uses the autokey exchange to retrieve the autokey
		 * values.
		 */
		case MODE_CLIENT:

			/*
			 * Parameter, certificate and identity.
			 */
			if (!peer->crypto)
				exten = crypto_args(peer, CRYPTO_ASSOC,
				    peer->associd, hostval.ptr);
			else if (!(peer->crypto & CRYPTO_FLAG_CERT))
				exten = crypto_args(peer, CRYPTO_CERT,
				    peer->associd, peer->issuer);
			else if (!(peer->crypto & CRYPTO_FLAG_VRFY))
				exten = crypto_args(peer,
				    crypto_ident(peer), peer->associd,
				    NULL);

			/*
			 * Cookie and autokey. These are requests, but
			 * we use the peer association ID with autokey
			 * rather than our own.
			 */
			else if (!(peer->crypto & CRYPTO_FLAG_COOK))
				exten = crypto_args(peer, CRYPTO_COOK,
				    peer->associd, NULL);
			else if (!(peer->crypto & CRYPTO_FLAG_AUTO))
				exten = crypto_args(peer, CRYPTO_AUTO,
				    peer->assoc, NULL);

			/*
			 * Wait for clock sync, then sign the
			 * certificate and retrieve the leapsecond
			 * values.
			 */
			else if (sys_leap == LEAP_NOTINSYNC)
				break;

			else if (!(peer->crypto & CRYPTO_FLAG_SIGN))
				exten = crypto_args(peer, CRYPTO_SIGN,
				    peer->associd, hostval.ptr);
			else if (!(peer->crypto & CRYPTO_FLAG_LEAP))
				exten = crypto_args(peer, CRYPTO_LEAP,
				    peer->associd, NULL);
			break;
		}

		/*
		 * Add a queued extension field if present. This is
		 * always a request message, so the reply ID is already
		 * in the message. If an error occurs, the error bit is
		 * lit in the response.
		 */
		if (peer->cmmd != NULL) {
			u_int32 temp32;

			temp32 = CRYPTO_RESP;
			peer->cmmd->opcode |= htonl(temp32);
			sendlen += crypto_xmit(peer, &xpkt, NULL,
			    sendlen, peer->cmmd, 0);
			free(peer->cmmd);
			peer->cmmd = NULL;
		}

		/*
		 * Add an extension field created above. All but the
		 * autokey response message are request messages.
		 */
		if (exten != NULL) {
			if (exten->opcode != 0)
				sendlen += crypto_xmit(peer, &xpkt,
				    NULL, sendlen, exten, 0);
			free(exten);
		}

		/*
		 * Calculate the next session key. Since extension
		 * fields are present, the cookie value is zero.
		 */
		if (sendlen > (int)LEN_PKT_NOMAC) {
			session_key(&peer->dstadr->sin, &peer->srcadr,
			    xkeyid, 0, 2);
		}
	}
#endif	/* AUTOKEY */

	/*
	 * Transmit a-priori timestamps
	 */
	get_systime(&xmt_tx);
	if (peer->flip == 0) {		/* basic mode */
		peer->aorg = xmt_tx;
		HTONL_FP(&xmt_tx, &xpkt.xmt);
	} else {			/* interleaved modes */
		if (peer->hmode == MODE_BROADCAST) { /* bcst */
			HTONL_FP(&xmt_tx, &xpkt.xmt);
			if (peer->flip > 0)
				HTONL_FP(&peer->borg, &xpkt.org);
			else
				HTONL_FP(&peer->aorg, &xpkt.org);
		} else {		/* symmetric */
			if (peer->flip > 0)
				HTONL_FP(&peer->borg, &xpkt.xmt);
			else
				HTONL_FP(&peer->aorg, &xpkt.xmt);
		}
	}
	xkeyid = peer->keyid;
	authlen = authencrypt(xkeyid, (u_int32 *)&xpkt, sendlen);
	if (authlen == 0) {
		report_event(PEVNT_AUTH, peer, "no key");
		peer->flash |= TEST5;		/* auth error */
		peer->badauth++;
		return;
	}
	sendlen += authlen;
#ifdef AUTOKEY
	if (xkeyid > NTP_MAXKEY)
		authtrust(xkeyid, 0);
#endif	/* AUTOKEY */
	if (sendlen > sizeof(xpkt)) {
		msyslog(LOG_ERR, "peer_xmit: buffer overflow %zu", sendlen);
		exit (-1);
	}
	peer->t21_bytes = sendlen;
	sendpkt(&peer->srcadr, peer->dstadr,
		sys_ttl[(peer->ttl >= sys_ttlmax) ? sys_ttlmax : peer->ttl],
		&xpkt, sendlen);
	peer->sent++;
	peer->throttle += (1 << peer->minpoll) - 2;

	/*
	 * Capture a-posteriori timestamps
	 */
	get_systime(&xmt_ty);
	if (peer->flip != 0) {			/* interleaved modes */
		if (peer->flip > 0)
			peer->aorg = xmt_ty;
		else
			peer->borg = xmt_ty;
		peer->flip = -peer->flip;
	}
	L_SUB(&xmt_ty, &xmt_tx);
	LFPTOD(&xmt_ty, peer->xleave);
#ifdef AUTOKEY
	DPRINTF(1, ("peer_xmit: at %ld %s->%s mode %d keyid %08x len %zu index %d\n",
		    current_time, latoa(peer->dstadr),
		    ntoa(&peer->srcadr), peer->hmode, xkeyid, sendlen,
		    peer->keynumber));
#else	/* !AUTOKEY follows */
	DPRINTF(1, ("peer_xmit: at %ld %s->%s mode %d keyid %08x len %zu\n",
		    current_time, peer->dstadr ?
		    ntoa(&peer->dstadr->sin) : "-",
		    ntoa(&peer->srcadr), peer->hmode, xkeyid, sendlen));
#endif	/* !AUTOKEY */

	return;
}


#ifdef LEAP_SMEAR

static void
leap_smear_add_offs(
	l_fp *t,
	l_fp *t_recv
	)
{

	L_ADD(t, &leap_smear.offset);

	/*
	** XXX: Should the smear be added to the root dispersion?
	*/

	return;
}

#endif /* LEAP_SMEAR */


/*
 * fast_xmit - Send packet for nonpersistent association. Note that
 * neither the source or destination can be a broadcast address.
 */
static void
fast_xmit(
	struct recvbuf *rbufp,	/* receive packet pointer */
	int	xmode,		/* receive mode */
	keyid_t	xkeyid,		/* transmit key ID */
	int	flags		/* restrict mask */
	)
{
	struct pkt xpkt;	/* transmit packet structure */
	struct pkt *rpkt;	/* receive packet structure */
	l_fp	xmt_tx, xmt_ty;
	size_t	sendlen;
#ifdef AUTOKEY
	u_int32	temp32;
#endif

	/*
	 * Initialize transmit packet header fields from the receive
	 * buffer provided. We leave the fields intact as received, but
	 * set the peer poll at the maximum of the receive peer poll and
	 * the system minimum poll (ntp_minpoll). This is for KoD rate
	 * control and not strictly specification compliant, but doesn't
	 * break anything.
	 *
	 * If the gazinta was from a multicast address, the gazoutta
	 * must go out another way.
	 */
	rpkt = &rbufp->recv_pkt;
	if (rbufp->dstadr->flags & INT_MCASTOPEN)
		rbufp->dstadr = findinterface(&rbufp->recv_srcadr);

	/*
	 * If this is a kiss-o'-death (KoD) packet, show leap
	 * unsynchronized, stratum zero, reference ID the four-character
	 * kiss code and system root delay. Note we don't reveal the
	 * local time, so these packets can't be used for
	 * synchronization.
	 */
	if (flags & RES_KOD) {
		sys_kodsent++;
		xpkt.li_vn_mode = PKT_LI_VN_MODE(LEAP_NOTINSYNC,
		    PKT_VERSION(rpkt->li_vn_mode), xmode);
		xpkt.stratum = STRATUM_PKT_UNSPEC;
		xpkt.ppoll = max(rpkt->ppoll, ntp_minpoll);
		xpkt.precision = rpkt->precision;
		memcpy(&xpkt.refid, "RATE", 4);
		xpkt.rootdelay = rpkt->rootdelay;
		xpkt.rootdisp = rpkt->rootdisp;
		xpkt.reftime = rpkt->reftime;
		xpkt.org = rpkt->xmt;
		xpkt.rec = rpkt->xmt;
		xpkt.xmt = rpkt->xmt;

	/*
	 * This is a normal packet. Use the system variables.
	 */
	} else {
#ifdef LEAP_SMEAR
		/*
		 * Make copies of the variables which can be affected by smearing.
		 */
		l_fp this_ref_time;
		l_fp this_recv_time;
#endif

		/*
		 * If we are inside the leap smear interval we add the current smear offset to
		 * the packet receive time, to the packet transmit time, and eventually to the
		 * reftime to make sure the reftime isn't later than the transmit/receive times.
		 */
		xpkt.li_vn_mode = PKT_LI_VN_MODE(xmt_leap,
		    PKT_VERSION(rpkt->li_vn_mode), xmode);

		xpkt.stratum = STRATUM_TO_PKT(sys_stratum);
		xpkt.ppoll = max(rpkt->ppoll, ntp_minpoll);
		xpkt.precision = sys_precision;
		xpkt.refid = sys_refid;
		xpkt.rootdelay = HTONS_FP(DTOFP(sys_rootdelay));
		xpkt.rootdisp = HTONS_FP(DTOUFP(sys_rootdisp));

#ifdef LEAP_SMEAR
		this_ref_time = sys_reftime;
		if (leap_smear.in_progress) {
			leap_smear_add_offs(&this_ref_time, NULL);
			xpkt.refid = convertLFPToRefID(leap_smear.offset);
			DPRINTF(2, ("fast_xmit: leap_smear.in_progress: refid %8x, smear %s\n",
				ntohl(xpkt.refid),
				lfptoa(&leap_smear.offset, 8)
				));
		}
		HTONL_FP(&this_ref_time, &xpkt.reftime);
#else
		HTONL_FP(&sys_reftime, &xpkt.reftime);
#endif

		xpkt.org = rpkt->xmt;

#ifdef LEAP_SMEAR
		this_recv_time = rbufp->recv_time;
		if (leap_smear.in_progress)
			leap_smear_add_offs(&this_recv_time, NULL);
		HTONL_FP(&this_recv_time, &xpkt.rec);
#else
		HTONL_FP(&rbufp->recv_time, &xpkt.rec);
#endif

		get_systime(&xmt_tx);
#ifdef LEAP_SMEAR
		if (leap_smear.in_progress)
			leap_smear_add_offs(&xmt_tx, &this_recv_time);
#endif
		HTONL_FP(&xmt_tx, &xpkt.xmt);
	}

#ifdef HAVE_NTP_SIGND
	if (flags & RES_MSSNTP) {
		send_via_ntp_signd(rbufp, xmode, xkeyid, flags, &xpkt);
		return;
	}
#endif /* HAVE_NTP_SIGND */

	/*
	 * If the received packet contains a MAC, the transmitted packet
	 * is authenticated and contains a MAC. If not, the transmitted
	 * packet is not authenticated.
	 */
	sendlen = LEN_PKT_NOMAC;
	if (rbufp->recv_length == sendlen) {
		sendpkt(&rbufp->recv_srcadr, rbufp->dstadr, 0, &xpkt,
		    sendlen);
		DPRINTF(1, ("fast_xmit: at %ld %s->%s mode %d len %lu\n",
			    current_time, stoa(&rbufp->dstadr->sin),
			    stoa(&rbufp->recv_srcadr), xmode,
			    (u_long)sendlen));
		return;
	}

	/*
	 * The received packet contains a MAC, so the transmitted packet
	 * must be authenticated. For symmetric key cryptography, use
	 * the predefined and trusted symmetric keys to generate the
	 * cryptosum. For autokey cryptography, use the server private
	 * value to generate the cookie, which is unique for every
	 * source-destination-key ID combination.
	 */
#ifdef AUTOKEY
	if (xkeyid > NTP_MAXKEY) {
		keyid_t cookie;

		/*
		 * The only way to get here is a reply to a legitimate
		 * client request message, so the mode must be
		 * MODE_SERVER. If an extension field is present, there
		 * can be only one and that must be a command. Do what
		 * needs, but with private value of zero so the poor
		 * jerk can decode it. If no extension field is present,
		 * use the cookie to generate the session key.
		 */
		cookie = session_key(&rbufp->recv_srcadr,
		    &rbufp->dstadr->sin, 0, sys_private, 0);
		if ((size_t)rbufp->recv_length > sendlen + MAX_MAC_LEN) {
			session_key(&rbufp->dstadr->sin,
			    &rbufp->recv_srcadr, xkeyid, 0, 2);
			temp32 = CRYPTO_RESP;
			rpkt->exten[0] |= htonl(temp32);
			sendlen += crypto_xmit(NULL, &xpkt, rbufp,
			    sendlen, (struct exten *)rpkt->exten,
			    cookie);
		} else {
			session_key(&rbufp->dstadr->sin,
			    &rbufp->recv_srcadr, xkeyid, cookie, 2);
		}
	}
#endif	/* AUTOKEY */
	get_systime(&xmt_tx);
	sendlen += authencrypt(xkeyid, (u_int32 *)&xpkt, sendlen);
#ifdef AUTOKEY
	if (xkeyid > NTP_MAXKEY)
		authtrust(xkeyid, 0);
#endif	/* AUTOKEY */
	sendpkt(&rbufp->recv_srcadr, rbufp->dstadr, 0, &xpkt, sendlen);
	get_systime(&xmt_ty);
	L_SUB(&xmt_ty, &xmt_tx);
	sys_authdelay = xmt_ty;
	DPRINTF(1, ("fast_xmit: at %ld %s->%s mode %d keyid %08x len %lu\n",
		    current_time, ntoa(&rbufp->dstadr->sin),
		    ntoa(&rbufp->recv_srcadr), xmode, xkeyid,
		    (u_long)sendlen));
}


/*
 * pool_xmit - resolve hostname or send unicast solicitation for pool.
 */
static void
pool_xmit(
	struct peer *pool	/* pool solicitor association */
	)
{
#ifdef WORKER
	struct pkt		xpkt;	/* transmit packet structure */
	struct addrinfo		hints;
	int			rc;
	struct interface *	lcladr;
	sockaddr_u *		rmtadr;
	r4addr			r4a;
	int			restrict_mask;
	struct peer *		p;
	l_fp			xmt_tx;

	if (NULL == pool->ai) {
		if (pool->addrs != NULL) {
			/* free() is used with copy_addrinfo_list() */
			free(pool->addrs);
			pool->addrs = NULL;
		}
		ZERO(hints);
		hints.ai_family = AF(&pool->srcadr);
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = IPPROTO_UDP;
		/* ignore getaddrinfo_sometime() errors, we will retry */
		rc = getaddrinfo_sometime(
			pool->hostname,
			"ntp",
			&hints,
			0,			/* no retry */
			&pool_name_resolved,
			(void *)(intptr_t)pool->associd);
		if (!rc)
			DPRINTF(1, ("pool DNS lookup %s started\n",
				pool->hostname));
		else
			msyslog(LOG_ERR,
				"unable to start pool DNS %s: %m",
				pool->hostname);
		return;
	}

	do {
		/* copy_addrinfo_list ai_addr points to a sockaddr_u */
		rmtadr = (sockaddr_u *)(void *)pool->ai->ai_addr;
		pool->ai = pool->ai->ai_next;
		p = findexistingpeer(rmtadr, NULL, NULL, MODE_CLIENT, 0, NULL);
	} while (p != NULL && pool->ai != NULL);
	if (p != NULL)
		return;	/* out of addresses, re-query DNS next poll */
	restrictions(rmtadr, &r4a);
	restrict_mask = r4a.rflags;
	if (RES_FLAGS & restrict_mask)
		restrict_source(rmtadr, 0,
				current_time + POOL_SOLICIT_WINDOW + 1);
	lcladr = findinterface(rmtadr);
	memset(&xpkt, 0, sizeof(xpkt));
	xpkt.li_vn_mode = PKT_LI_VN_MODE(sys_leap, pool->version,
					 MODE_CLIENT);
	xpkt.stratum = STRATUM_TO_PKT(sys_stratum);
	xpkt.ppoll = pool->hpoll;
	xpkt.precision = sys_precision;
	xpkt.refid = sys_refid;
	xpkt.rootdelay = HTONS_FP(DTOFP(sys_rootdelay));
	xpkt.rootdisp = HTONS_FP(DTOUFP(sys_rootdisp));
	HTONL_FP(&sys_reftime, &xpkt.reftime);
	get_systime(&xmt_tx);
	pool->aorg = xmt_tx;
	HTONL_FP(&xmt_tx, &xpkt.xmt);
	sendpkt(rmtadr, lcladr,
		sys_ttl[(pool->ttl >= sys_ttlmax) ? sys_ttlmax : pool->ttl],
		&xpkt, LEN_PKT_NOMAC);
	pool->sent++;
	pool->throttle += (1 << pool->minpoll) - 2;
	DPRINTF(1, ("pool_xmit: at %ld %s->%s pool\n",
		    current_time, latoa(lcladr), stoa(rmtadr)));
	msyslog(LOG_INFO, "Soliciting pool server %s", stoa(rmtadr));
#endif	/* WORKER */
}


#ifdef AUTOKEY
	/*
	 * group_test - test if this is the same group
	 *
	 * host		assoc		return		action
	 * none		none		0		mobilize *
	 * none		group		0		mobilize *
	 * group	none		0		mobilize *
	 * group	group		1		mobilize
	 * group	different	1		ignore
	 * * ignore if notrust
	 */
int
group_test(
	char	*grp,
	char	*ident
	)
{
	if (grp == NULL)
		return (0);

	if (strcmp(grp, sys_groupname) == 0)
		return (0);

	if (ident == NULL)
		return (1);

	if (strcmp(grp, ident) == 0)
		return (0);

	return (1);
}
#endif /* AUTOKEY */


#ifdef WORKER
void
pool_name_resolved(
	int			rescode,
	int			gai_errno,
	void *			context,
	const char *		name,
	const char *		service,
	const struct addrinfo *	hints,
	const struct addrinfo *	res
	)
{
	struct peer *	pool;	/* pool solicitor association */
	associd_t	assoc;

	if (rescode) {
		msyslog(LOG_ERR,
			"error resolving pool %s: %s (%d)",
			name, gai_strerror(rescode), rescode);
		return;
	}

	assoc = (associd_t)(intptr_t)context;
	pool = findpeerbyassoc(assoc);
	if (NULL == pool) {
		msyslog(LOG_ERR,
			"Could not find assoc %u for pool DNS %s",
			assoc, name);
		return;
	}
	DPRINTF(1, ("pool DNS %s completed\n", name));
	pool->addrs = copy_addrinfo_list(res);
	pool->ai = pool->addrs;
	pool_xmit(pool);

}
#endif	/* WORKER */


#ifdef AUTOKEY
/*
 * key_expire - purge the key list
 */
void
key_expire(
	struct peer *peer	/* peer structure pointer */
	)
{
	int i;

	if (peer->keylist != NULL) {
		for (i = 0; i <= peer->keynumber; i++)
			authtrust(peer->keylist[i], 0);
		free(peer->keylist);
		peer->keylist = NULL;
	}
	value_free(&peer->sndval);
	peer->keynumber = 0;
	peer->flags &= ~FLAG_ASSOC;
	DPRINTF(1, ("key_expire: at %lu associd %d\n", current_time,
		    peer->associd));
}
#endif	/* AUTOKEY */


/*
 * local_refid(peer) - check peer refid to avoid selecting peers
 *		       currently synced to this ntpd.
 */
static int
local_refid(
	struct peer *	p
	)
{
	endpt *	unicast_ep;

	if (p->dstadr != NULL && !(INT_MCASTIF & p->dstadr->flags))
		unicast_ep = p->dstadr;
	else
		unicast_ep = findinterface(&p->srcadr);

	if (unicast_ep != NULL && p->refid == unicast_ep->addr_refid)
		return TRUE;
	else
		return FALSE;
}


/*
 * Determine if the peer is unfit for synchronization
 *
 * A peer is unfit for synchronization if
 * > TEST10 bad leap or stratum below floor or at or above ceiling
 * > TEST11 root distance exceeded for remote peer
 * > TEST12 a direct or indirect synchronization loop would form
 * > TEST13 unreachable or noselect
 */
int				/* FALSE if fit, TRUE if unfit */
peer_unfit(
	struct peer *peer	/* peer structure pointer */
	)
{
	int	rval = 0;

	/*
	 * A stratum error occurs if (1) the server has never been
	 * synchronized, (2) the server stratum is below the floor or
	 * greater than or equal to the ceiling.
	 */
	if (   peer->leap == LEAP_NOTINSYNC
	    || peer->stratum < sys_floor
	    || peer->stratum >= sys_ceiling) {
		rval |= TEST10;		/* bad synch or stratum */
	}

	/*
	 * A distance error for a remote peer occurs if the root
	 * distance is greater than or equal to the distance threshold
	 * plus the increment due to one host poll interval.
	 */
	if (   !(peer->flags & FLAG_REFCLOCK)
	    && root_distance(peer) >= sys_maxdist
				      + clock_phi * ULOGTOD(peer->hpoll)) {
		rval |= TEST11;		/* distance exceeded */
	}

	/*
	 * A loop error occurs if the remote peer is synchronized to the
	 * local peer or if the remote peer is synchronized to the same
	 * server as the local peer but only if the remote peer is
	 * neither a reference clock nor an orphan.
	 */
	if (peer->stratum > 1 && local_refid(peer)) {
		rval |= TEST12;		/* synchronization loop */
	}

	/*
	 * An unreachable error occurs if the server is unreachable or
	 * the noselect bit is set.
	 */
	if (!peer->reach || (peer->flags & FLAG_NOSELECT)) {
		rval |= TEST13;		/* unreachable */
	}

	peer->flash &= ~PEER_TEST_MASK;
	peer->flash |= rval;
	return (rval);
}


/*
 * Find the precision of this particular machine
 */
#define MINSTEP		20e-9	/* minimum clock increment (s) */
#define MAXSTEP		1	/* maximum clock increment (s) */
#define MINCHANGES	12	/* minimum number of step samples */
#define MAXLOOPS	((int)(1. / MINSTEP))	/* avoid infinite loop */

/*
 * This routine measures the system precision defined as the minimum of
 * a sequence of differences between successive readings of the system
 * clock. However, if a difference is less than MINSTEP, the clock has
 * been read more than once during a clock tick and the difference is
 * ignored. We set MINSTEP greater than zero in case something happens
 * like a cache miss, and to tolerate underlying system clocks which
 * ensure each reading is strictly greater than prior readings while
 * using an underlying stepping (not interpolated) clock.
 *
 * sys_tick and sys_precision represent the time to read the clock for
 * systems with high-precision clocks, and the tick interval or step
 * size for lower-precision stepping clocks.
 *
 * This routine also measures the time to read the clock on stepping
 * system clocks by counting the number of readings between changes of
 * the underlying clock.  With either type of clock, the minimum time
 * to read the clock is saved as sys_fuzz, and used to ensure the
 * get_systime() readings always increase and are fuzzed below sys_fuzz.
 */
void
measure_precision(void)
{
	/*
	 * With sys_fuzz set to zero, get_systime() fuzzing of low bits
	 * is effectively disabled.  trunc_os_clock is FALSE to disable
	 * get_ostime() simulation of a low-precision system clock.
	 */
	set_sys_fuzz(0.);
	trunc_os_clock = FALSE;
	measured_tick = measure_tick_fuzz();
	set_sys_tick_precision(measured_tick);
	msyslog(LOG_INFO, "proto: precision = %.3f usec (%d)",
		sys_tick * 1e6, sys_precision);
	if (sys_fuzz < sys_tick) {
		msyslog(LOG_NOTICE, "proto: fuzz beneath %.3f usec",
			sys_fuzz * 1e6);
	}
}


/*
 * measure_tick_fuzz()
 *
 * measures the minimum time to read the clock (stored in sys_fuzz)
 * and returns the tick, the larger of the minimum increment observed
 * between successive clock readings and the time to read the clock.
 */
double
measure_tick_fuzz(void)
{
	l_fp	minstep;	/* MINSTEP as l_fp */
	l_fp	val;		/* current seconds fraction */
	l_fp	last;		/* last seconds fraction */
	l_fp	ldiff;		/* val - last */
	double	tick;		/* computed tick value */
	double	diff;
	long	repeats;
	long	max_repeats;
	int	changes;
	int	i;		/* log2 precision */

	tick = MAXSTEP;
	max_repeats = 0;
	repeats = 0;
	changes = 0;
	DTOLFP(MINSTEP, &minstep);
	get_systime(&last);
	for (i = 0; i < MAXLOOPS && changes < MINCHANGES; i++) {
		get_systime(&val);
		ldiff = val;
		L_SUB(&ldiff, &last);
		last = val;
		if (L_ISGT(&ldiff, &minstep)) {
			max_repeats = max(repeats, max_repeats);
			repeats = 0;
			changes++;
			LFPTOD(&ldiff, diff);
			tick = min(diff, tick);
		} else {
			repeats++;
		}
	}
	if (changes < MINCHANGES) {
		msyslog(LOG_ERR, "Fatal error: precision could not be measured (MINSTEP too large?)");
		exit(1);
	}

	if (0 == max_repeats) {
		set_sys_fuzz(tick);
	} else {
		set_sys_fuzz(tick / max_repeats);
	}

	return tick;
}


void
set_sys_tick_precision(
	double tick
	)
{
	int i;

	if (tick > 1.) {
		msyslog(LOG_ERR,
			"unsupported tick %.3f > 1s ignored", tick);
		return;
	}
	if (tick < measured_tick) {
		msyslog(LOG_ERR,
			"proto: tick %.3f less than measured tick %.3f, ignored",
			tick, measured_tick);
		return;
	} else if (tick > measured_tick) {
		trunc_os_clock = TRUE;
		msyslog(LOG_NOTICE,
			"proto: truncating system clock to multiples of %.9f",
			tick);
	}
	sys_tick = tick;

	/*
	 * Find the nearest power of two.
	 */
	for (i = 0; tick <= 1; i--)
		tick *= 2;
	if (tick - 1 > 1 - tick / 2)
		i++;

	sys_precision = (s_char)i;
}


/*
 * init_proto - initialize the protocol module's data
 */
void
init_proto(void)
{
	l_fp	dummy;
	int	i;

	/*
	 * Fill in the sys_* stuff.  Default is don't listen to
	 * broadcasting, require authentication.
	 */
	set_sys_leap(LEAP_NOTINSYNC);
	sys_stratum = STRATUM_UNSPEC;
	memcpy(&sys_refid, "INIT", 4);
	sys_peer = NULL;
	sys_rootdelay = 0;
	sys_rootdisp = 0;
	L_CLR(&sys_reftime);
	sys_jitter = 0;
	measure_precision();
	get_systime(&dummy);
	sys_survivors = 0;
	sys_manycastserver = 0;
	sys_bclient = 0;
	sys_bdelay = BDELAY_DEFAULT;	/*[Bug 3031] delay cutoff */
	sys_authenticate = 1;
	sys_stattime = current_time;
	orphwait = current_time + sys_orphwait;
	proto_clr_stats();
	for (i = 0; i < MAX_TTL; ++i)
		sys_ttl[i] = (u_char)((i * 256) / MAX_TTL);
	sys_ttlmax = (MAX_TTL - 1);
	hardpps_enable = 0;
	stats_control = 1;
}


/*
 * proto_config - configure the protocol module
 */
void
proto_config(
	int	item,
	u_long	value,
	double	dvalue,
	sockaddr_u *svalue
	)
{
	/*
	 * Figure out what he wants to change, then do it
	 */
	DPRINTF(2, ("proto_config: code %d value %lu dvalue %lf\n",
		    item, value, dvalue));

	switch (item) {

	/*
	 * enable and disable commands - arguments are Boolean.
	 */
	case PROTO_AUTHENTICATE: /* authentication (auth) */
		sys_authenticate = value;
		break;

	case PROTO_BROADCLIENT: /* broadcast client (bclient) */
		sys_bclient = (int)value;
		if (sys_bclient == 0)
			io_unsetbclient();
		else
			io_setbclient();
		break;

#ifdef REFCLOCK
	case PROTO_CAL:		/* refclock calibrate (calibrate) */
		cal_enable = value;
		break;
#endif /* REFCLOCK */

	case PROTO_KERNEL:	/* kernel discipline (kernel) */
		select_loop(value);
		break;

	case PROTO_MONITOR:	/* monitoring (monitor) */
		if (value)
			mon_start(MON_ON);
		else {
			mon_stop(MON_ON);
			if (mon_enabled)
				msyslog(LOG_WARNING,
					"restrict: 'monitor' cannot be disabled while 'limited' is enabled");
		}
		break;

	case PROTO_NTP:		/* NTP discipline (ntp) */
		ntp_enable = value;
		break;

	case PROTO_MODE7:	/* mode7 management (ntpdc) */
		ntp_mode7 = value;
		break;

	case PROTO_PPS:		/* PPS discipline (pps) */
		hardpps_enable = value;
		break;

	case PROTO_FILEGEN:	/* statistics (stats) */
		stats_control = value;
		break;

	/*
	 * tos command - arguments are double, sometimes cast to int
	 */

	case PROTO_BCPOLLBSTEP:	/* Broadcast Poll Backstep gate (bcpollbstep) */
		sys_bcpollbstep = (u_char)dvalue;
		break;

	case PROTO_BEACON:	/* manycast beacon (beacon) */
		sys_beacon = (int)dvalue;
		break;

	case PROTO_BROADDELAY:	/* default broadcast delay (bdelay) */
		sys_bdelay = (dvalue ? dvalue : BDELAY_DEFAULT);
		break;

	case PROTO_CEILING:	/* stratum ceiling (ceiling) */
		sys_ceiling = (int)dvalue;
		break;

	case PROTO_COHORT:	/* cohort switch (cohort) */
		sys_cohort = (int)dvalue;
		break;

	case PROTO_FLOOR:	/* stratum floor (floor) */
		sys_floor = (int)dvalue;
		break;

	case PROTO_MAXCLOCK:	/* maximum candidates (maxclock) */
		sys_maxclock = (int)dvalue;
		break;

	case PROTO_MAXDIST:	/* select threshold (maxdist) */
		sys_maxdist = dvalue;
		break;

	case PROTO_CALLDELAY:	/* modem call delay (mdelay) */
		break;		/* NOT USED */

	case PROTO_MINCLOCK:	/* minimum candidates (minclock) */
		sys_minclock = (int)dvalue;
		break;

	case PROTO_MINDISP:	/* minimum distance (mindist) */
		sys_mindisp = dvalue;
		break;

	case PROTO_MINSANE:	/* minimum survivors (minsane) */
		sys_minsane = (int)dvalue;
		break;

	case PROTO_ORPHAN:	/* orphan stratum (orphan) */
		sys_orphan = (int)dvalue;
		break;

	case PROTO_ORPHWAIT:	/* orphan wait (orphwait) */
		orphwait -= sys_orphwait;
		sys_orphwait = (int)dvalue;
		orphwait += sys_orphwait;
		break;

	/*
	 * Miscellaneous commands
	 */
	case PROTO_MULTICAST_ADD: /* add group address */
		if (svalue != NULL)
			io_multicast_add(svalue);
		sys_bclient = 1;
		break;

	case PROTO_MULTICAST_DEL: /* delete group address */
		if (svalue != NULL)
			io_multicast_del(svalue);
		break;

	/*
	 * Peer_clear Early policy choices
	 */

	case PROTO_PCEDIGEST:	/* Digest */
		peer_clear_digest_early = value;
		break;

	/*
	 * Unpeer Early policy choices
	 */

	case PROTO_UECRYPTO:	/* Crypto */
		unpeer_crypto_early = value;
		break;

	case PROTO_UECRYPTONAK:	/* Crypto_NAK */
		unpeer_crypto_nak_early = value;
		break;

	case PROTO_UEDIGEST:	/* Digest */
		unpeer_digest_early = value;
		break;

	default:
		msyslog(LOG_NOTICE,
		    "proto: unsupported option %d", item);
	}
}


/*
 * proto_clr_stats - clear protocol stat counters
 */
void
proto_clr_stats(void)
{
	sys_stattime = current_time;
	sys_received = 0;
	sys_processed = 0;
	sys_newversion = 0;
	sys_oldversion = 0;
	sys_declined = 0;
	sys_restricted = 0;
	sys_badlength = 0;
	sys_badauth = 0;
	sys_limitrejected = 0;
	sys_kodsent = 0;
	sys_lamport = 0;
	sys_tsrounding = 0;
}
