/*
 * ntp_request.c - respond to information requests
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_request.h"
#include "ntp_control.h"
#include "ntp_refclock.h"
#include "ntp_if.h"
#include "ntp_stdlib.h"
#include "ntp_assert.h"

#include <stdio.h>
#include <stddef.h>
#include <signal.h>
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#include <arpa/inet.h>

#include "recvbuff.h"

#ifdef KERNEL_PLL
#include "ntp_syscall.h"
#endif /* KERNEL_PLL */

/*
 * Structure to hold request procedure information
 */
#define	NOAUTH	0
#define	AUTH	1

#define	NO_REQUEST	(-1)
/*
 * Because we now have v6 addresses in the messages, we need to compensate
 * for the larger size.  Therefore, we introduce the alternate size to 
 * keep us friendly with older implementations.  A little ugly.
 */
static int client_v6_capable = 0;   /* the client can handle longer messages */

#define v6sizeof(type)	(client_v6_capable ? sizeof(type) : v4sizeof(type))

struct req_proc {
	short request_code;	/* defined request code */
	short needs_auth;	/* true when authentication needed */
	short sizeofitem;	/* size of request data item (older size)*/
	short v6_sizeofitem;	/* size of request data item (new size)*/
	void (*handler) (sockaddr_u *, endpt *,
			   struct req_pkt *);	/* routine to handle request */
};

/*
 * Universal request codes
 */
static const struct req_proc univ_codes[] = {
	{ NO_REQUEST,		NOAUTH,	 0,	0, NULL }
};

static	void	req_ack	(sockaddr_u *, endpt *, struct req_pkt *, int);
static	void *	prepare_pkt	(sockaddr_u *, endpt *,
				 struct req_pkt *, size_t);
static	void *	more_pkt	(void);
static	void	flush_pkt	(void);
static	void	list_peers	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	list_peers_sum	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	peer_info	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	peer_stats	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	sys_info	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	sys_stats	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	mem_stats	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	io_stats	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	timer_stats	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	loop_info	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	do_conf		(sockaddr_u *, endpt *, struct req_pkt *);
static	void	do_unconf	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	set_sys_flag	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	clr_sys_flag	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	setclr_flags	(sockaddr_u *, endpt *, struct req_pkt *, u_long);
static	void	list_restrict4	(const restrict_u *, struct info_restrict **);
static	void	list_restrict6	(const restrict_u *, struct info_restrict **);
static	void	list_restrict	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	do_resaddflags	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	do_ressubflags	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	do_unrestrict	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	do_restrict	(sockaddr_u *, endpt *, struct req_pkt *, restrict_op);
static	void	mon_getlist	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	reset_stats	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	reset_peer	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	do_key_reread	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	trust_key	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	untrust_key	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	do_trustkey	(sockaddr_u *, endpt *, struct req_pkt *, u_long);
static	void	get_auth_info	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	req_get_traps	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	req_set_trap	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	req_clr_trap	(sockaddr_u *, endpt *, struct req_pkt *);
static	void	do_setclr_trap	(sockaddr_u *, endpt *, struct req_pkt *, int);
static	void	set_request_keyid (sockaddr_u *, endpt *, struct req_pkt *);
static	void	set_control_keyid (sockaddr_u *, endpt *, struct req_pkt *);
static	void	get_ctl_stats   (sockaddr_u *, endpt *, struct req_pkt *);
static	void	get_if_stats    (sockaddr_u *, endpt *, struct req_pkt *);
static	void	do_if_reload    (sockaddr_u *, endpt *, struct req_pkt *);
#ifdef KERNEL_PLL
static	void	get_kernel_info (sockaddr_u *, endpt *, struct req_pkt *);
#endif /* KERNEL_PLL */
#ifdef REFCLOCK
static	void	get_clock_info (sockaddr_u *, endpt *, struct req_pkt *);
static	void	set_clock_fudge (sockaddr_u *, endpt *, struct req_pkt *);
#endif	/* REFCLOCK */
#ifdef REFCLOCK
static	void	get_clkbug_info (sockaddr_u *, endpt *, struct req_pkt *);
#endif	/* REFCLOCK */

/*
 * ntpd request codes
 */
static const struct req_proc ntp_codes[] = {
	{ REQ_PEER_LIST,	NOAUTH,	0, 0,	list_peers },
	{ REQ_PEER_LIST_SUM,	NOAUTH,	0, 0,	list_peers_sum },
	{ REQ_PEER_INFO,    NOAUTH, v4sizeof(struct info_peer_list),
				sizeof(struct info_peer_list), peer_info},
	{ REQ_PEER_STATS,   NOAUTH, v4sizeof(struct info_peer_list),
				sizeof(struct info_peer_list), peer_stats},
	{ REQ_SYS_INFO,		NOAUTH,	0, 0,	sys_info },
	{ REQ_SYS_STATS,	NOAUTH,	0, 0,	sys_stats },
	{ REQ_IO_STATS,		NOAUTH,	0, 0,	io_stats },
	{ REQ_MEM_STATS,	NOAUTH,	0, 0,	mem_stats },
	{ REQ_LOOP_INFO,	NOAUTH,	0, 0,	loop_info },
	{ REQ_TIMER_STATS,	NOAUTH,	0, 0,	timer_stats },
	{ REQ_CONFIG,	    AUTH, v4sizeof(struct conf_peer),
				sizeof(struct conf_peer), do_conf },
	{ REQ_UNCONFIG,	    AUTH, v4sizeof(struct conf_unpeer),
				sizeof(struct conf_unpeer), do_unconf },
	{ REQ_SET_SYS_FLAG, AUTH, sizeof(struct conf_sys_flags),
				sizeof(struct conf_sys_flags), set_sys_flag },
	{ REQ_CLR_SYS_FLAG, AUTH, sizeof(struct conf_sys_flags), 
				sizeof(struct conf_sys_flags),  clr_sys_flag },
	{ REQ_GET_RESTRICT,	NOAUTH,	0, 0,	list_restrict },
	{ REQ_RESADDFLAGS, AUTH, v4sizeof(struct conf_restrict),
				sizeof(struct conf_restrict), do_resaddflags },
	{ REQ_RESSUBFLAGS, AUTH, v4sizeof(struct conf_restrict),
				sizeof(struct conf_restrict), do_ressubflags },
	{ REQ_UNRESTRICT, AUTH, v4sizeof(struct conf_restrict),
				sizeof(struct conf_restrict), do_unrestrict },
	{ REQ_MON_GETLIST,	NOAUTH,	0, 0,	mon_getlist },
	{ REQ_MON_GETLIST_1,	NOAUTH,	0, 0,	mon_getlist },
	{ REQ_RESET_STATS, AUTH, sizeof(struct reset_flags), 0, reset_stats },
	{ REQ_RESET_PEER,  AUTH, v4sizeof(struct conf_unpeer),
				sizeof(struct conf_unpeer), reset_peer },
	{ REQ_REREAD_KEYS,	AUTH,	0, 0,	do_key_reread },
	{ REQ_TRUSTKEY,   AUTH, sizeof(u_long), sizeof(u_long), trust_key },
	{ REQ_UNTRUSTKEY, AUTH, sizeof(u_long), sizeof(u_long), untrust_key },
	{ REQ_AUTHINFO,		NOAUTH,	0, 0,	get_auth_info },
	{ REQ_TRAPS,		NOAUTH, 0, 0,	req_get_traps },
	{ REQ_ADD_TRAP,	AUTH, v4sizeof(struct conf_trap),
				sizeof(struct conf_trap), req_set_trap },
	{ REQ_CLR_TRAP,	AUTH, v4sizeof(struct conf_trap),
				sizeof(struct conf_trap), req_clr_trap },
	{ REQ_REQUEST_KEY, AUTH, sizeof(u_long), sizeof(u_long), 
				set_request_keyid },
	{ REQ_CONTROL_KEY, AUTH, sizeof(u_long), sizeof(u_long), 
				set_control_keyid },
	{ REQ_GET_CTLSTATS,	NOAUTH,	0, 0,	get_ctl_stats },
#ifdef KERNEL_PLL
	{ REQ_GET_KERNEL,	NOAUTH,	0, 0,	get_kernel_info },
#endif
#ifdef REFCLOCK
	{ REQ_GET_CLOCKINFO, NOAUTH, sizeof(u_int32), sizeof(u_int32), 
				get_clock_info },
	{ REQ_SET_CLKFUDGE, AUTH, sizeof(struct conf_fudge), 
				sizeof(struct conf_fudge), set_clock_fudge },
	{ REQ_GET_CLKBUGINFO, NOAUTH, sizeof(u_int32), sizeof(u_int32),
				get_clkbug_info },
#endif
	{ REQ_IF_STATS,		AUTH, 0, 0,	get_if_stats },
	{ REQ_IF_RELOAD,	AUTH, 0, 0,	do_if_reload },

	{ NO_REQUEST,		NOAUTH,	0, 0,	0 }
};


/*
 * Authentication keyid used to authenticate requests.  Zero means we
 * don't allow writing anything.
 */
keyid_t info_auth_keyid;

/*
 * Statistic counters to keep track of requests and responses.
 */
u_long numrequests;		/* number of requests we've received */
u_long numresppkts;		/* number of resp packets sent with data */

/*
 * lazy way to count errors, indexed by the error code
 */
u_long errorcounter[MAX_INFO_ERR + 1];

/*
 * A hack.  To keep the authentication module clear of ntp-ism's, we
 * include a time reset variable for its stats here.
 */
u_long auth_timereset;

/*
 * Response packet used by these routines.  Also some state information
 * so that we can handle packet formatting within a common set of
 * subroutines.  Note we try to enter data in place whenever possible,
 * but the need to set the more bit correctly means we occasionally
 * use the extra buffer and copy.
 */
static struct resp_pkt rpkt;
static int reqver;
static int seqno;
static int nitems;
static int itemsize;
static int databytes;
static char exbuf[RESP_DATA_SIZE];
static int usingexbuf;
static sockaddr_u *toaddr;
static endpt *frominter;

/*
 * init_request - initialize request data
 */
void
init_request (void)
{
	size_t i;

	numrequests = 0;
	numresppkts = 0;
	auth_timereset = 0;
	info_auth_keyid = 0;	/* by default, can't do this */

	for (i = 0; i < sizeof(errorcounter)/sizeof(errorcounter[0]); i++)
	    errorcounter[i] = 0;
}


/*
 * req_ack - acknowledge request with no data
 */
static void
req_ack(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt,
	int errcode
	)
{
	/*
	 * fill in the fields
	 */
	rpkt.rm_vn_mode = RM_VN_MODE(RESP_BIT, 0, reqver);
	rpkt.auth_seq = AUTH_SEQ(0, 0);
	rpkt.implementation = inpkt->implementation;
	rpkt.request = inpkt->request;
	rpkt.err_nitems = ERR_NITEMS(errcode, 0); 
	rpkt.mbz_itemsize = MBZ_ITEMSIZE(0);

	/*
	 * send packet and bump counters
	 */
	sendpkt(srcadr, inter, -1, (struct pkt *)&rpkt, RESP_HEADER_SIZE);
	errorcounter[errcode]++;
}


/*
 * prepare_pkt - prepare response packet for transmission, return pointer
 *		 to storage for data item.
 */
static void *
prepare_pkt(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *pkt,
	size_t structsize
	)
{
	DPRINTF(4, ("request: preparing pkt\n"));

	/*
	 * Fill in the implementation, request and itemsize fields
	 * since these won't change.
	 */
	rpkt.implementation = pkt->implementation;
	rpkt.request = pkt->request;
	rpkt.mbz_itemsize = MBZ_ITEMSIZE(structsize);

	/*
	 * Compute the static data needed to carry on.
	 */
	toaddr = srcadr;
	frominter = inter;
	seqno = 0;
	nitems = 0;
	itemsize = structsize;
	databytes = 0;
	usingexbuf = 0;

	/*
	 * return the beginning of the packet buffer.
	 */
	return &rpkt.u;
}


/*
 * more_pkt - return a data pointer for a new item.
 */
static void *
more_pkt(void)
{
	/*
	 * If we were using the extra buffer, send the packet.
	 */
	if (usingexbuf) {
		DPRINTF(3, ("request: sending pkt\n"));
		rpkt.rm_vn_mode = RM_VN_MODE(RESP_BIT, MORE_BIT, reqver);
		rpkt.auth_seq = AUTH_SEQ(0, seqno);
		rpkt.err_nitems = htons((u_short)nitems);
		sendpkt(toaddr, frominter, -1, (struct pkt *)&rpkt,
			RESP_HEADER_SIZE + databytes);
		numresppkts++;

		/*
		 * Copy data out of exbuf into the packet.
		 */
		memcpy(&rpkt.u.data[0], exbuf, (unsigned)itemsize);
		seqno++;
		databytes = 0;
		nitems = 0;
		usingexbuf = 0;
	}

	databytes += itemsize;
	nitems++;
	if (databytes + itemsize <= RESP_DATA_SIZE) {
		DPRINTF(4, ("request: giving him more data\n"));
		/*
		 * More room in packet.  Give him the
		 * next address.
		 */
		return &rpkt.u.data[databytes];
	} else {
		/*
		 * No room in packet.  Give him the extra
		 * buffer unless this was the last in the sequence.
		 */
		DPRINTF(4, ("request: into extra buffer\n"));
		if (seqno == MAXSEQ)
			return NULL;
		else {
			usingexbuf = 1;
			return exbuf;
		}
	}
}


/*
 * flush_pkt - we're done, return remaining information.
 */
static void
flush_pkt(void)
{
	DPRINTF(3, ("request: flushing packet, %d items\n", nitems));
	/*
	 * Must send the last packet.  If nothing in here and nothing
	 * has been sent, send an error saying no data to be found.
	 */
	if (seqno == 0 && nitems == 0)
		req_ack(toaddr, frominter, (struct req_pkt *)&rpkt,
			INFO_ERR_NODATA);
	else {
		rpkt.rm_vn_mode = RM_VN_MODE(RESP_BIT, 0, reqver);
		rpkt.auth_seq = AUTH_SEQ(0, seqno);
		rpkt.err_nitems = htons((u_short)nitems);
		sendpkt(toaddr, frominter, -1, (struct pkt *)&rpkt,
			RESP_HEADER_SIZE+databytes);
		numresppkts++;
	}
}



/*
 * Given a buffer, return the packet mode
 */
int
get_packet_mode(struct recvbuf *rbufp)
{
	struct req_pkt *inpkt = (struct req_pkt *)&rbufp->recv_pkt;
	return (INFO_MODE(inpkt->rm_vn_mode));
}


/*
 * process_private - process private mode (7) packets
 */
void
process_private(
	struct recvbuf *rbufp,
	int mod_okay
	)
{
	static u_long quiet_until;
	struct req_pkt *inpkt;
	struct req_pkt_tail *tailinpkt;
	sockaddr_u *srcadr;
	endpt *inter;
	const struct req_proc *proc;
	int ec;
	short temp_size;
	l_fp ftmp;
	double dtemp;
	size_t recv_len;
	size_t noslop_len;
	size_t mac_len;

	/*
	 * Initialize pointers, for convenience
	 */
	recv_len = rbufp->recv_length;
	inpkt = (struct req_pkt *)&rbufp->recv_pkt;
	srcadr = &rbufp->recv_srcadr;
	inter = rbufp->dstadr;

	DPRINTF(3, ("process_private: impl %d req %d\n",
		    inpkt->implementation, inpkt->request));

	/*
	 * Do some sanity checks on the packet.  Return a format
	 * error if it fails.
	 */
	ec = 0;
	if (   (++ec, ISRESPONSE(inpkt->rm_vn_mode))
	    || (++ec, ISMORE(inpkt->rm_vn_mode))
	    || (++ec, INFO_VERSION(inpkt->rm_vn_mode) > NTP_VERSION)
	    || (++ec, INFO_VERSION(inpkt->rm_vn_mode) < NTP_OLDVERSION)
	    || (++ec, INFO_SEQ(inpkt->auth_seq) != 0)
	    || (++ec, INFO_ERR(inpkt->err_nitems) != 0)
	    || (++ec, INFO_MBZ(inpkt->mbz_itemsize) != 0)
	    || (++ec, rbufp->recv_length < (int)REQ_LEN_HDR)
		) {
		NLOG(NLOG_SYSEVENT)
			if (current_time >= quiet_until) {
				msyslog(LOG_ERR,
					"process_private: drop test %d"
					" failed, pkt from %s",
					ec, stoa(srcadr));
				quiet_until = current_time + 60;
			}
		return;
	}

	reqver = INFO_VERSION(inpkt->rm_vn_mode);

	/*
	 * Get the appropriate procedure list to search.
	 */
	if (inpkt->implementation == IMPL_UNIV)
		proc = univ_codes;
	else if ((inpkt->implementation == IMPL_XNTPD) ||
		 (inpkt->implementation == IMPL_XNTPD_OLD))
		proc = ntp_codes;
	else {
		req_ack(srcadr, inter, inpkt, INFO_ERR_IMPL);
		return;
	}

	/*
	 * Search the list for the request codes.  If it isn't one
	 * we know, return an error.
	 */
	while (proc->request_code != NO_REQUEST) {
		if (proc->request_code == (short) inpkt->request)
			break;
		proc++;
	}
	if (proc->request_code == NO_REQUEST) {
		req_ack(srcadr, inter, inpkt, INFO_ERR_REQ);
		return;
	}

	DPRINTF(4, ("found request in tables\n"));

	/*
	 * If we need data, check to see if we have some.  If we
	 * don't, check to see that there is none (picky, picky).
	 */	

	/* This part is a bit tricky, we want to be sure that the size
	 * returned is either the old or the new size.  We also can find
	 * out if the client can accept both types of messages this way. 
	 *
	 * Handle the exception of REQ_CONFIG. It can have two data sizes.
	 */
	temp_size = INFO_ITEMSIZE(inpkt->mbz_itemsize);
	if ((temp_size != proc->sizeofitem &&
	     temp_size != proc->v6_sizeofitem) &&
	    !(inpkt->implementation == IMPL_XNTPD &&
	      inpkt->request == REQ_CONFIG &&
	      temp_size == sizeof(struct old_conf_peer))) {
		DPRINTF(3, ("process_private: wrong item size, received %d, should be %d or %d\n",
			    temp_size, proc->sizeofitem, proc->v6_sizeofitem));
		req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
		return;
	}
	if ((proc->sizeofitem != 0) &&
	    ((size_t)(temp_size * INFO_NITEMS(inpkt->err_nitems)) >
	     (recv_len - REQ_LEN_HDR))) {
		DPRINTF(3, ("process_private: not enough data\n"));
		req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
		return;
	}

	switch (inpkt->implementation) {
	case IMPL_XNTPD:
		client_v6_capable = 1;
		break;
	case IMPL_XNTPD_OLD:
		client_v6_capable = 0;
		break;
	default:
		req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
		return;
	}

	/*
	 * If we need to authenticate, do so.  Note that an
	 * authenticatable packet must include a mac field, must
	 * have used key info_auth_keyid and must have included
	 * a time stamp in the appropriate field.  The time stamp
	 * must be within INFO_TS_MAXSKEW of the receive
	 * time stamp.
	 */
	if (proc->needs_auth && sys_authenticate) {

		if (recv_len < (REQ_LEN_HDR +
		    (INFO_ITEMSIZE(inpkt->mbz_itemsize) *
		    INFO_NITEMS(inpkt->err_nitems)) +
		    REQ_TAIL_MIN)) {
			req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
			return;
		}

		/*
		 * For 16-octet digests, regardless of itemsize and
		 * nitems, authenticated requests are a fixed size
		 * with the timestamp, key ID, and digest located
		 * at the end of the packet.  Because the key ID
		 * determining the digest size precedes the digest,
		 * for larger digests the fixed size request scheme
		 * is abandoned and the timestamp, key ID, and digest
		 * are located relative to the start of the packet,
		 * with the digest size determined by the packet size.
		 */
		noslop_len = REQ_LEN_HDR
			     + INFO_ITEMSIZE(inpkt->mbz_itemsize) *
			       INFO_NITEMS(inpkt->err_nitems)
			     + sizeof(inpkt->tstamp);
		/* 32-bit alignment */
		noslop_len = (noslop_len + 3) & ~3;
		if (recv_len > (noslop_len + MAX_MAC_LEN))
			mac_len = 20;
		else
			mac_len = recv_len - noslop_len;

		tailinpkt = (void *)((char *)inpkt + recv_len -
			    (mac_len + sizeof(inpkt->tstamp)));

		/*
		 * If this guy is restricted from doing this, don't let
		 * him.  If the wrong key was used, or packet doesn't
		 * have mac, return.
		 */
		/* XXX: Use authistrustedip(), or equivalent. */
		if (!INFO_IS_AUTH(inpkt->auth_seq) || !info_auth_keyid
		    || ntohl(tailinpkt->keyid) != info_auth_keyid) {
			DPRINTF(5, ("failed auth %d info_auth_keyid %u pkt keyid %u maclen %lu\n",
				    INFO_IS_AUTH(inpkt->auth_seq),
				    info_auth_keyid,
				    ntohl(tailinpkt->keyid), (u_long)mac_len));
#ifdef DEBUG
			msyslog(LOG_DEBUG,
				"process_private: failed auth %d info_auth_keyid %u pkt keyid %u maclen %lu\n",
				INFO_IS_AUTH(inpkt->auth_seq),
				info_auth_keyid,
				ntohl(tailinpkt->keyid), (u_long)mac_len);
#endif
			req_ack(srcadr, inter, inpkt, INFO_ERR_AUTH);
			return;
		}
		if (recv_len > REQ_LEN_NOMAC + MAX_MAC_LEN) {
			DPRINTF(5, ("bad pkt length %zu\n", recv_len));
			msyslog(LOG_ERR,
				"process_private: bad pkt length %zu",
				recv_len);
			req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
			return;
		}
		if (!mod_okay || !authhavekey(info_auth_keyid)) {
			DPRINTF(5, ("failed auth mod_okay %d\n",
				    mod_okay));
#ifdef DEBUG
			msyslog(LOG_DEBUG,
				"process_private: failed auth mod_okay %d\n",
				mod_okay);
#endif
			if (!mod_okay) {
				sys_restricted++;
			}
			req_ack(srcadr, inter, inpkt, INFO_ERR_AUTH);
			return;
		}

		/*
		 * calculate absolute time difference between xmit time stamp
		 * and receive time stamp.  If too large, too bad.
		 */
		NTOHL_FP(&tailinpkt->tstamp, &ftmp);
		L_SUB(&ftmp, &rbufp->recv_time);
		LFPTOD(&ftmp, dtemp);
		if (fabs(dtemp) > INFO_TS_MAXSKEW) {
			/*
			 * He's a loser.  Tell him.
			 */
			DPRINTF(5, ("xmit/rcv timestamp delta %g > INFO_TS_MAXSKEW %g\n",
				    dtemp, INFO_TS_MAXSKEW));
			req_ack(srcadr, inter, inpkt, INFO_ERR_AUTH);
			return;
		}

		/*
		 * So far so good.  See if decryption works out okay.
		 */
		if (!authdecrypt(info_auth_keyid, (u_int32 *)inpkt,
				 recv_len - mac_len, mac_len)) {
			DPRINTF(5, ("authdecrypt failed\n"));
			req_ack(srcadr, inter, inpkt, INFO_ERR_AUTH);
			return;
		}
	}

	DPRINTF(3, ("process_private: all okay, into handler\n"));
	/*
	 * Packet is okay.  Call the handler to send him data.
	 */
	(proc->handler)(srcadr, inter, inpkt);
}


/*
 * list_peers - send a list of the peers
 */
static void
list_peers(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	struct info_peer_list *	ip;
	const struct peer *	pp;

	ip = (struct info_peer_list *)prepare_pkt(srcadr, inter, inpkt,
	    v6sizeof(struct info_peer_list));
	for (pp = peer_list; pp != NULL && ip != NULL; pp = pp->p_link) {
		if (IS_IPV6(&pp->srcadr)) {
			if (!client_v6_capable)
				continue;			
			ip->addr6 = SOCK_ADDR6(&pp->srcadr);
			ip->v6_flag = 1;
		} else {
			ip->addr = NSRCADR(&pp->srcadr);
			if (client_v6_capable)
				ip->v6_flag = 0;
		}

		ip->port = NSRCPORT(&pp->srcadr);
		ip->hmode = pp->hmode;
		ip->flags = 0;
		if (pp->flags & FLAG_CONFIG)
			ip->flags |= INFO_FLAG_CONFIG;
		if (pp == sys_peer)
			ip->flags |= INFO_FLAG_SYSPEER;
		if (pp->status == CTL_PST_SEL_SYNCCAND)
			ip->flags |= INFO_FLAG_SEL_CANDIDATE;
		if (pp->status >= CTL_PST_SEL_SYSPEER)
			ip->flags |= INFO_FLAG_SHORTLIST;
		ip = (struct info_peer_list *)more_pkt();
	}	/* for pp */

	flush_pkt();
}


/*
 * list_peers_sum - return extended peer list
 */
static void
list_peers_sum(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	struct info_peer_summary *	ips;
	const struct peer *		pp;
	l_fp 				ltmp;

	DPRINTF(3, ("wants peer list summary\n"));

	ips = (struct info_peer_summary *)prepare_pkt(srcadr, inter, inpkt,
	    v6sizeof(struct info_peer_summary));
	for (pp = peer_list; pp != NULL && ips != NULL; pp = pp->p_link) {
		DPRINTF(4, ("sum: got one\n"));
		/*
		 * Be careful here not to return v6 peers when we
		 * want only v4.
		 */
		if (IS_IPV6(&pp->srcadr)) {
			if (!client_v6_capable)
				continue;
			ips->srcadr6 = SOCK_ADDR6(&pp->srcadr);
			ips->v6_flag = 1;
			if (pp->dstadr)
				ips->dstadr6 = SOCK_ADDR6(&pp->dstadr->sin);
			else
				ZERO(ips->dstadr6);
		} else {
			ips->srcadr = NSRCADR(&pp->srcadr);
			if (client_v6_capable)
				ips->v6_flag = 0;
			
			if (pp->dstadr) {
				if (!pp->processed)
					ips->dstadr = NSRCADR(&pp->dstadr->sin);
				else {
					if (MDF_BCAST == pp->cast_flags)
						ips->dstadr = NSRCADR(&pp->dstadr->bcast);
					else if (pp->cast_flags) {
						ips->dstadr = NSRCADR(&pp->dstadr->sin);
						if (!ips->dstadr)
							ips->dstadr = NSRCADR(&pp->dstadr->bcast);
					}
				}
			} else {
				ips->dstadr = 0;
			}
		}
		
		ips->srcport = NSRCPORT(&pp->srcadr);
		ips->stratum = pp->stratum;
		ips->hpoll = pp->hpoll;
		ips->ppoll = pp->ppoll;
		ips->reach = pp->reach;
		ips->flags = 0;
		if (pp == sys_peer)
			ips->flags |= INFO_FLAG_SYSPEER;
		if (pp->flags & FLAG_CONFIG)
			ips->flags |= INFO_FLAG_CONFIG;
		if (pp->flags & FLAG_REFCLOCK)
			ips->flags |= INFO_FLAG_REFCLOCK;
		if (pp->flags & FLAG_PREFER)
			ips->flags |= INFO_FLAG_PREFER;
		if (pp->flags & FLAG_BURST)
			ips->flags |= INFO_FLAG_BURST;
		if (pp->status == CTL_PST_SEL_SYNCCAND)
			ips->flags |= INFO_FLAG_SEL_CANDIDATE;
		if (pp->status >= CTL_PST_SEL_SYSPEER)
			ips->flags |= INFO_FLAG_SHORTLIST;
		ips->hmode = pp->hmode;
		ips->delay = HTONS_FP(DTOFP(pp->delay));
		DTOLFP(pp->offset, &ltmp);
		HTONL_FP(&ltmp, &ips->offset);
		ips->dispersion = HTONS_FP(DTOUFP(SQRT(pp->disp)));

		ips = (struct info_peer_summary *)more_pkt();
	}	/* for pp */

	flush_pkt();
}


/*
 * peer_info - send information for one or more peers
 */
static void
peer_info (
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	u_short			items;
	size_t			item_sz;
	char *			datap;
	struct info_peer_list	ipl;
	struct peer *		pp;
	struct info_peer *	ip;
	int			i;
	int			j;
	sockaddr_u		addr;
	l_fp			ltmp;

	items = INFO_NITEMS(inpkt->err_nitems);
	item_sz = INFO_ITEMSIZE(inpkt->mbz_itemsize);
	datap = inpkt->u.data;
	if (item_sz != sizeof(ipl)) {
		req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
		return;
	}
	ip = prepare_pkt(srcadr, inter, inpkt,
			 v6sizeof(struct info_peer));
	while (items-- > 0 && ip != NULL) {
		ZERO(ipl);
		memcpy(&ipl, datap, item_sz);
		ZERO_SOCK(&addr);
		NSRCPORT(&addr) = ipl.port;
		if (client_v6_capable && ipl.v6_flag) {
			AF(&addr) = AF_INET6;
			SOCK_ADDR6(&addr) = ipl.addr6;
		} else {
			AF(&addr) = AF_INET;
			NSRCADR(&addr) = ipl.addr;
		}
#ifdef ISC_PLATFORM_HAVESALEN
		addr.sa.sa_len = SOCKLEN(&addr);
#endif
		datap += item_sz;

		pp = findexistingpeer(&addr, NULL, NULL, -1, 0, NULL);
		if (NULL == pp)
			continue;
		if (IS_IPV6(srcadr)) {
			if (pp->dstadr)
				ip->dstadr6 =
				    (MDF_BCAST == pp->cast_flags)
					? SOCK_ADDR6(&pp->dstadr->bcast)
					: SOCK_ADDR6(&pp->dstadr->sin);
			else
				ZERO(ip->dstadr6);

			ip->srcadr6 = SOCK_ADDR6(&pp->srcadr);
			ip->v6_flag = 1;
		} else {
			if (pp->dstadr) {
				if (!pp->processed)
					ip->dstadr = NSRCADR(&pp->dstadr->sin);
				else {
					if (MDF_BCAST == pp->cast_flags)
						ip->dstadr = NSRCADR(&pp->dstadr->bcast);
					else if (pp->cast_flags) {
						ip->dstadr = NSRCADR(&pp->dstadr->sin);
						if (!ip->dstadr)
							ip->dstadr = NSRCADR(&pp->dstadr->bcast);
					}
				}
			} else
				ip->dstadr = 0;

			ip->srcadr = NSRCADR(&pp->srcadr);
			if (client_v6_capable)
				ip->v6_flag = 0;
		}
		ip->srcport = NSRCPORT(&pp->srcadr);
		ip->flags = 0;
		if (pp == sys_peer)
			ip->flags |= INFO_FLAG_SYSPEER;
		if (pp->flags & FLAG_CONFIG)
			ip->flags |= INFO_FLAG_CONFIG;
		if (pp->flags & FLAG_REFCLOCK)
			ip->flags |= INFO_FLAG_REFCLOCK;
		if (pp->flags & FLAG_PREFER)
			ip->flags |= INFO_FLAG_PREFER;
		if (pp->flags & FLAG_BURST)
			ip->flags |= INFO_FLAG_BURST;
		if (pp->status == CTL_PST_SEL_SYNCCAND)
			ip->flags |= INFO_FLAG_SEL_CANDIDATE;
		if (pp->status >= CTL_PST_SEL_SYSPEER)
			ip->flags |= INFO_FLAG_SHORTLIST;
		ip->leap = pp->leap;
		ip->hmode = pp->hmode;
		ip->pmode = pp->pmode;
		ip->keyid = pp->keyid;
		ip->stratum = pp->stratum;
		ip->ppoll = pp->ppoll;
		ip->hpoll = pp->hpoll;
		ip->precision = pp->precision;
		ip->version = pp->version;
		ip->reach = pp->reach;
		ip->unreach = (u_char)pp->unreach;
		ip->flash = (u_char)pp->flash;
		ip->flash2 = (u_short)pp->flash;
		ip->estbdelay = HTONS_FP(DTOFP(pp->delay));
		ip->ttl = (u_char)pp->ttl;
		ip->associd = htons(pp->associd);
		ip->rootdelay = HTONS_FP(DTOUFP(pp->rootdelay));
		ip->rootdispersion = HTONS_FP(DTOUFP(pp->rootdisp));
		ip->refid = pp->refid;
		HTONL_FP(&pp->reftime, &ip->reftime);
		HTONL_FP(&pp->aorg, &ip->org);
		HTONL_FP(&pp->rec, &ip->rec);
		HTONL_FP(&pp->xmt, &ip->xmt);
		j = pp->filter_nextpt - 1;
		for (i = 0; i < NTP_SHIFT; i++, j--) {
			if (j < 0)
				j = NTP_SHIFT-1;
			ip->filtdelay[i] = HTONS_FP(DTOFP(pp->filter_delay[j]));
			DTOLFP(pp->filter_offset[j], &ltmp);
			HTONL_FP(&ltmp, &ip->filtoffset[i]);
			ip->order[i] = (u_char)((pp->filter_nextpt +
						 NTP_SHIFT - 1) -
						pp->filter_order[i]);
			if (ip->order[i] >= NTP_SHIFT)
				ip->order[i] -= NTP_SHIFT;
		}
		DTOLFP(pp->offset, &ltmp);
		HTONL_FP(&ltmp, &ip->offset);
		ip->delay = HTONS_FP(DTOFP(pp->delay));
		ip->dispersion = HTONS_FP(DTOUFP(SQRT(pp->disp)));
		ip->selectdisp = HTONS_FP(DTOUFP(SQRT(pp->jitter)));
		ip = more_pkt();
	}
	flush_pkt();
}


/*
 * peer_stats - send statistics for one or more peers
 */
static void
peer_stats (
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	u_short			items;
	size_t			item_sz;
	char *			datap;
	struct info_peer_list	ipl;
	struct peer *		pp;
	struct info_peer_stats *ip;
	sockaddr_u addr;

	DPRINTF(1, ("peer_stats: called\n"));
	items = INFO_NITEMS(inpkt->err_nitems);
	item_sz = INFO_ITEMSIZE(inpkt->mbz_itemsize);
	datap = inpkt->u.data;
	if (item_sz > sizeof(ipl)) {
		req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
		return;
	}
	ip = prepare_pkt(srcadr, inter, inpkt,
			 v6sizeof(struct info_peer_stats));
	while (items-- > 0 && ip != NULL) {
		ZERO(ipl);
		memcpy(&ipl, datap, item_sz);
		ZERO(addr);
		NSRCPORT(&addr) = ipl.port;
		if (client_v6_capable && ipl.v6_flag) {
			AF(&addr) = AF_INET6;
			SOCK_ADDR6(&addr) = ipl.addr6;
		} else {
			AF(&addr) = AF_INET;
			NSRCADR(&addr) = ipl.addr;
		}	
#ifdef ISC_PLATFORM_HAVESALEN
		addr.sa.sa_len = SOCKLEN(&addr);
#endif
		DPRINTF(1, ("peer_stats: looking for %s, %d, %d\n",
			    stoa(&addr), ipl.port, NSRCPORT(&addr)));

		datap += item_sz;

		pp = findexistingpeer(&addr, NULL, NULL, -1, 0, NULL);
		if (NULL == pp)
			continue;

		DPRINTF(1, ("peer_stats: found %s\n", stoa(&addr)));

		if (IS_IPV4(&pp->srcadr)) {
			if (pp->dstadr) {
				if (!pp->processed)
					ip->dstadr = NSRCADR(&pp->dstadr->sin);
				else {
					if (MDF_BCAST == pp->cast_flags)
						ip->dstadr = NSRCADR(&pp->dstadr->bcast);
					else if (pp->cast_flags) {
						ip->dstadr = NSRCADR(&pp->dstadr->sin);
						if (!ip->dstadr)
							ip->dstadr = NSRCADR(&pp->dstadr->bcast);
					}
				}
			} else
				ip->dstadr = 0;
			
			ip->srcadr = NSRCADR(&pp->srcadr);
			if (client_v6_capable)
				ip->v6_flag = 0;
		} else {
			if (pp->dstadr)
				ip->dstadr6 =
				    (MDF_BCAST == pp->cast_flags)
					? SOCK_ADDR6(&pp->dstadr->bcast)
					: SOCK_ADDR6(&pp->dstadr->sin);
			else
				ZERO(ip->dstadr6);

			ip->srcadr6 = SOCK_ADDR6(&pp->srcadr);
			ip->v6_flag = 1;
		}	
		ip->srcport = NSRCPORT(&pp->srcadr);
		ip->flags = 0;
		if (pp == sys_peer)
		    ip->flags |= INFO_FLAG_SYSPEER;
		if (pp->flags & FLAG_CONFIG)
		    ip->flags |= INFO_FLAG_CONFIG;
		if (pp->flags & FLAG_REFCLOCK)
		    ip->flags |= INFO_FLAG_REFCLOCK;
		if (pp->flags & FLAG_PREFER)
		    ip->flags |= INFO_FLAG_PREFER;
		if (pp->flags & FLAG_BURST)
		    ip->flags |= INFO_FLAG_BURST;
		if (pp->flags & FLAG_IBURST)
		    ip->flags |= INFO_FLAG_IBURST;
		if (pp->status == CTL_PST_SEL_SYNCCAND)
		    ip->flags |= INFO_FLAG_SEL_CANDIDATE;
		if (pp->status >= CTL_PST_SEL_SYSPEER)
		    ip->flags |= INFO_FLAG_SHORTLIST;
		ip->flags = htons(ip->flags);
		ip->timereceived = htonl((u_int32)(current_time - pp->timereceived));
		ip->timetosend = htonl(pp->nextdate - current_time);
		ip->timereachable = htonl((u_int32)(current_time - pp->timereachable));
		ip->sent = htonl((u_int32)(pp->sent));
		ip->processed = htonl((u_int32)(pp->processed));
		ip->badauth = htonl((u_int32)(pp->badauth));
		ip->bogusorg = htonl((u_int32)(pp->bogusorg));
		ip->oldpkt = htonl((u_int32)(pp->oldpkt));
		ip->seldisp = htonl((u_int32)(pp->seldisptoolarge));
		ip->selbroken = htonl((u_int32)(pp->selbroken));
		ip->candidate = pp->status;
		ip = (struct info_peer_stats *)more_pkt();
	}
	flush_pkt();
}


/*
 * sys_info - return system info
 */
static void
sys_info(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	register struct info_sys *is;

	is = (struct info_sys *)prepare_pkt(srcadr, inter, inpkt,
	    v6sizeof(struct info_sys));

	if (sys_peer) {
		if (IS_IPV4(&sys_peer->srcadr)) {
			is->peer = NSRCADR(&sys_peer->srcadr);
			if (client_v6_capable)
				is->v6_flag = 0;
		} else if (client_v6_capable) {
			is->peer6 = SOCK_ADDR6(&sys_peer->srcadr);
			is->v6_flag = 1;
		}
		is->peer_mode = sys_peer->hmode;
	} else {
		is->peer = 0;
		if (client_v6_capable) {
			is->v6_flag = 0;
		}
		is->peer_mode = 0;
	}

	is->leap = sys_leap;
	is->stratum = sys_stratum;
	is->precision = sys_precision;
	is->rootdelay = htonl(DTOFP(sys_rootdelay));
	is->rootdispersion = htonl(DTOUFP(sys_rootdisp));
	is->frequency = htonl(DTOFP(sys_jitter));
	is->stability = htonl(DTOUFP(clock_stability * 1e6));
	is->refid = sys_refid;
	HTONL_FP(&sys_reftime, &is->reftime);

	is->poll = sys_poll;
	
	is->flags = 0;
	if (sys_authenticate)
		is->flags |= INFO_FLAG_AUTHENTICATE;
	if (sys_bclient)
		is->flags |= INFO_FLAG_BCLIENT;
#ifdef REFCLOCK
	if (cal_enable)
		is->flags |= INFO_FLAG_CAL;
#endif /* REFCLOCK */
	if (kern_enable)
		is->flags |= INFO_FLAG_KERNEL;
	if (mon_enabled != MON_OFF)
		is->flags |= INFO_FLAG_MONITOR;
	if (ntp_enable)
		is->flags |= INFO_FLAG_NTP;
	if (hardpps_enable)
		is->flags |= INFO_FLAG_PPS_SYNC;
	if (stats_control)
		is->flags |= INFO_FLAG_FILEGEN;
	is->bdelay = HTONS_FP(DTOFP(sys_bdelay));
	HTONL_UF(sys_authdelay.l_uf, &is->authdelay);
	(void) more_pkt();
	flush_pkt();
}


/*
 * sys_stats - return system statistics
 */
static void
sys_stats(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	register struct info_sys_stats *ss;

	ss = (struct info_sys_stats *)prepare_pkt(srcadr, inter, inpkt,
		sizeof(struct info_sys_stats));
	ss->timeup = htonl((u_int32)current_time);
	ss->timereset = htonl((u_int32)(current_time - sys_stattime));
	ss->denied = htonl((u_int32)sys_restricted);
	ss->oldversionpkt = htonl((u_int32)sys_oldversion);
	ss->newversionpkt = htonl((u_int32)sys_newversion);
	ss->unknownversion = htonl((u_int32)sys_declined);
	ss->badlength = htonl((u_int32)sys_badlength);
	ss->processed = htonl((u_int32)sys_processed);
	ss->badauth = htonl((u_int32)sys_badauth);
	ss->limitrejected = htonl((u_int32)sys_limitrejected);
	ss->received = htonl((u_int32)sys_received);
	ss->lamport = htonl((u_int32)sys_lamport);
	ss->tsrounding = htonl((u_int32)sys_tsrounding);
	(void) more_pkt();
	flush_pkt();
}


/*
 * mem_stats - return memory statistics
 */
static void
mem_stats(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	register struct info_mem_stats *ms;
	register int i;

	ms = (struct info_mem_stats *)prepare_pkt(srcadr, inter, inpkt,
						  sizeof(struct info_mem_stats));

	ms->timereset = htonl((u_int32)(current_time - peer_timereset));
	ms->totalpeermem = htons((u_short)total_peer_structs);
	ms->freepeermem = htons((u_short)peer_free_count);
	ms->findpeer_calls = htonl((u_int32)findpeer_calls);
	ms->allocations = htonl((u_int32)peer_allocations);
	ms->demobilizations = htonl((u_int32)peer_demobilizations);

	for (i = 0; i < NTP_HASH_SIZE; i++)
		ms->hashcount[i] = (u_char)
		    max((u_int)peer_hash_count[i], UCHAR_MAX);

	(void) more_pkt();
	flush_pkt();
}


/*
 * io_stats - return io statistics
 */
static void
io_stats(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	struct info_io_stats *io;

	io = (struct info_io_stats *)prepare_pkt(srcadr, inter, inpkt,
						 sizeof(struct info_io_stats));

	io->timereset = htonl((u_int32)(current_time - io_timereset));
	io->totalrecvbufs = htons((u_short) total_recvbuffs());
	io->freerecvbufs = htons((u_short) free_recvbuffs());
	io->fullrecvbufs = htons((u_short) full_recvbuffs());
	io->lowwater = htons((u_short) lowater_additions());
	io->dropped = htonl((u_int32)packets_dropped);
	io->ignored = htonl((u_int32)packets_ignored);
	io->received = htonl((u_int32)packets_received);
	io->sent = htonl((u_int32)packets_sent);
	io->notsent = htonl((u_int32)packets_notsent);
	io->interrupts = htonl((u_int32)handler_calls);
	io->int_received = htonl((u_int32)handler_pkts);

	(void) more_pkt();
	flush_pkt();
}


/*
 * timer_stats - return timer statistics
 */
static void
timer_stats(
	sockaddr_u *		srcadr,
	endpt *			inter,
	struct req_pkt *	inpkt
	)
{
	struct info_timer_stats *	ts;
	u_long				sincereset;

	ts = (struct info_timer_stats *)prepare_pkt(srcadr, inter,
						    inpkt, sizeof(*ts));

	sincereset = current_time - timer_timereset;
	ts->timereset = htonl((u_int32)sincereset);
	ts->alarms = ts->timereset;
	ts->overflows = htonl((u_int32)alarm_overflow);
	ts->xmtcalls = htonl((u_int32)timer_xmtcalls);

	(void) more_pkt();
	flush_pkt();
}


/*
 * loop_info - return the current state of the loop filter
 */
static void
loop_info(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	struct info_loop *li;
	l_fp ltmp;

	li = (struct info_loop *)prepare_pkt(srcadr, inter, inpkt,
	    sizeof(struct info_loop));

	DTOLFP(last_offset, &ltmp);
	HTONL_FP(&ltmp, &li->last_offset);
	DTOLFP(drift_comp * 1e6, &ltmp);
	HTONL_FP(&ltmp, &li->drift_comp);
	li->compliance = htonl((u_int32)(tc_counter));
	li->watchdog_timer = htonl((u_int32)(current_time - sys_epoch));

	(void) more_pkt();
	flush_pkt();
}


/*
 * do_conf - add a peer to the configuration list
 */
static void
do_conf(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	u_short			items;
	size_t			item_sz;
	u_int			fl;
	char *			datap;
	struct conf_peer	temp_cp;
	sockaddr_u		peeraddr;

	/*
	 * Do a check of everything to see that it looks
	 * okay.  If not, complain about it.  Note we are
	 * very picky here.
	 */
	items = INFO_NITEMS(inpkt->err_nitems);
	item_sz = INFO_ITEMSIZE(inpkt->mbz_itemsize);
	datap = inpkt->u.data;
	if (item_sz > sizeof(temp_cp)) {
		req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
		return;
	}

	while (items-- > 0) {
		ZERO(temp_cp);
		memcpy(&temp_cp, datap, item_sz);
		ZERO_SOCK(&peeraddr);

		fl = 0;
		if (temp_cp.flags & CONF_FLAG_PREFER)
			fl |= FLAG_PREFER;
		if (temp_cp.flags & CONF_FLAG_BURST)
			fl |= FLAG_BURST;
		if (temp_cp.flags & CONF_FLAG_IBURST)
			fl |= FLAG_IBURST;
#ifdef AUTOKEY
		if (temp_cp.flags & CONF_FLAG_SKEY)
			fl |= FLAG_SKEY;
#endif	/* AUTOKEY */
		if (client_v6_capable && temp_cp.v6_flag) {
			AF(&peeraddr) = AF_INET6;
			SOCK_ADDR6(&peeraddr) = temp_cp.peeraddr6; 
		} else {
			AF(&peeraddr) = AF_INET;
			NSRCADR(&peeraddr) = temp_cp.peeraddr;
			/*
			 * Make sure the address is valid
			 */
			if (!ISREFCLOCKADR(&peeraddr) && 
			    ISBADADR(&peeraddr)) {
				req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
				return;
			}

		}
		NSRCPORT(&peeraddr) = htons(NTP_PORT);
#ifdef ISC_PLATFORM_HAVESALEN
		peeraddr.sa.sa_len = SOCKLEN(&peeraddr);
#endif

		/* check mode value: 0 <= hmode <= 6
		 *
		 * There's no good global define for that limit, and
		 * using a magic define is as good (or bad, actually) as
		 * a magic number. So we use the highest possible peer
		 * mode, and that is MODE_BCLIENT.
		 *
		 * [Bug 3009] claims that a problem occurs for hmode > 7,
		 * but the code in ntp_peer.c indicates trouble for any
		 * hmode > 6 ( --> MODE_BCLIENT).
		 */
		if (temp_cp.hmode > MODE_BCLIENT) {
			req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
			return;
		}
		
		/* Any more checks on the values? Unchecked at this
		 * point:
		 *   - version
		 *   - ttl
		 *   - keyid
		 *
		 *   - minpoll/maxpoll, but they are treated properly
		 *     for all cases internally. Checking not necessary.
		 *
		 * Note that we ignore any previously-specified ippeerlimit.
		 * If we're told to create the peer, we create the peer.
		 */
		
		/* finally create the peer */
		if (peer_config(&peeraddr, NULL, NULL, -1,
		    temp_cp.hmode, temp_cp.version, temp_cp.minpoll, 
		    temp_cp.maxpoll, fl, temp_cp.ttl, temp_cp.keyid,
		    NULL) == 0)
		{
			req_ack(srcadr, inter, inpkt, INFO_ERR_NODATA);
			return;
		}

		datap += item_sz;
	}
	req_ack(srcadr, inter, inpkt, INFO_OKAY);
}


/*
 * do_unconf - remove a peer from the configuration list
 */
static void
do_unconf(
	sockaddr_u *	srcadr,
	endpt *		inter,
	struct req_pkt *inpkt
	)
{
	u_short			items;
	size_t			item_sz;
	char *			datap;
	struct conf_unpeer	temp_cp;
	struct peer *		p;
	sockaddr_u		peeraddr;
	int			loops;

	/*
	 * This is a bit unstructured, but I like to be careful.
	 * We check to see that every peer exists and is actually
	 * configured.  If so, we remove them.  If not, we return
	 * an error.
	 *
	 * [Bug 3011] Even if we checked all peers given in the request
	 * in a dry run, there's still a chance that the caller played
	 * unfair and gave the same peer multiple times. So we still
	 * have to be prepared for nasty surprises in the second run ;)
	 */

	/* basic consistency checks */
	item_sz = INFO_ITEMSIZE(inpkt->mbz_itemsize);
	if (item_sz > sizeof(temp_cp)) {
		req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
		return;
	}

	/* now do two runs: first a dry run, then a busy one */
	for (loops = 0; loops != 2; ++loops) {
		items = INFO_NITEMS(inpkt->err_nitems);
		datap = inpkt->u.data;
		while (items-- > 0) {
			/* copy from request to local */
			ZERO(temp_cp);
			memcpy(&temp_cp, datap, item_sz);
			/* get address structure */
			ZERO_SOCK(&peeraddr);
			if (client_v6_capable && temp_cp.v6_flag) {
				AF(&peeraddr) = AF_INET6;
				SOCK_ADDR6(&peeraddr) = temp_cp.peeraddr6;
			} else {
				AF(&peeraddr) = AF_INET;
				NSRCADR(&peeraddr) = temp_cp.peeraddr;
			}
			SET_PORT(&peeraddr, NTP_PORT);
#ifdef ISC_PLATFORM_HAVESALEN
			peeraddr.sa.sa_len = SOCKLEN(&peeraddr);
#endif
			DPRINTF(1, ("searching for %s\n",
				    stoa(&peeraddr)));

			/* search for matching configred(!) peer */
			p = NULL;
			do {
				p = findexistingpeer(
					&peeraddr, NULL, p, -1, 0, NULL);
			} while (p && !(FLAG_CONFIG & p->flags));
			
			if (!loops && !p) {
				/* Item not found in dry run -- bail! */
				req_ack(srcadr, inter, inpkt,
					INFO_ERR_NODATA);
				return;
			} else if (loops && p) {
				/* Item found in busy run -- remove! */
				peer_clear(p, "GONE");
				unpeer(p);
			}
			datap += item_sz;
		}
	}

	/* report success */
	req_ack(srcadr, inter, inpkt, INFO_OKAY);
}


/*
 * set_sys_flag - set system flags
 */
static void
set_sys_flag(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	setclr_flags(srcadr, inter, inpkt, 1);
}


/*
 * clr_sys_flag - clear system flags
 */
static void
clr_sys_flag(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	setclr_flags(srcadr, inter, inpkt, 0);
}


/*
 * setclr_flags - do the grunge work of flag setting/clearing
 */
static void
setclr_flags(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt,
	u_long set
	)
{
	struct conf_sys_flags *sf;
	u_int32 flags;

	if (INFO_NITEMS(inpkt->err_nitems) > 1) {
		msyslog(LOG_ERR, "setclr_flags: err_nitems > 1");
		req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
		return;
	}

	sf = (struct conf_sys_flags *)&inpkt->u;
	flags = ntohl(sf->flags);
	
	if (flags & ~(SYS_FLAG_BCLIENT | SYS_FLAG_PPS |
		      SYS_FLAG_NTP | SYS_FLAG_KERNEL | SYS_FLAG_MONITOR |
		      SYS_FLAG_FILEGEN | SYS_FLAG_AUTH | SYS_FLAG_CAL)) {
		msyslog(LOG_ERR, "setclr_flags: extra flags: %#x",
			flags & ~(SYS_FLAG_BCLIENT | SYS_FLAG_PPS |
				  SYS_FLAG_NTP | SYS_FLAG_KERNEL |
				  SYS_FLAG_MONITOR | SYS_FLAG_FILEGEN |
				  SYS_FLAG_AUTH | SYS_FLAG_CAL));
		req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
		return;
	}

	if (flags & SYS_FLAG_BCLIENT)
		proto_config(PROTO_BROADCLIENT, set, 0., NULL);
	if (flags & SYS_FLAG_PPS)
		proto_config(PROTO_PPS, set, 0., NULL);
	if (flags & SYS_FLAG_NTP)
		proto_config(PROTO_NTP, set, 0., NULL);
	if (flags & SYS_FLAG_KERNEL)
		proto_config(PROTO_KERNEL, set, 0., NULL);
	if (flags & SYS_FLAG_MONITOR)
		proto_config(PROTO_MONITOR, set, 0., NULL);
	if (flags & SYS_FLAG_FILEGEN)
		proto_config(PROTO_FILEGEN, set, 0., NULL);
	if (flags & SYS_FLAG_AUTH)
		proto_config(PROTO_AUTHENTICATE, set, 0., NULL);
	if (flags & SYS_FLAG_CAL)
		proto_config(PROTO_CAL, set, 0., NULL);
	req_ack(srcadr, inter, inpkt, INFO_OKAY);
}

/* There have been some issues with the restrict list processing,
 * ranging from problems with deep recursion (resulting in stack
 * overflows) and overfull reply buffers.
 *
 * To avoid this trouble the list reversal is done iteratively using a
 * scratch pad.
 */
typedef struct RestrictStack RestrictStackT;
struct RestrictStack {
	RestrictStackT   *link;
	size_t            fcnt;
	const restrict_u *pres[63];
};

static size_t
getStackSheetSize(
	RestrictStackT *sp
	)
{
	if (sp)
		return sizeof(sp->pres)/sizeof(sp->pres[0]);
	return 0u;
}

static int/*BOOL*/
pushRestriction(
	RestrictStackT  **spp,
	const restrict_u *ptr
	)
{
	RestrictStackT *sp;

	if (NULL == (sp = *spp) || 0 == sp->fcnt) {
		/* need another sheet in the scratch pad */
		sp = emalloc(sizeof(*sp));
		sp->link = *spp;
		sp->fcnt = getStackSheetSize(sp);
		*spp = sp;
	}
	sp->pres[--sp->fcnt] = ptr;
	return TRUE;
}

static int/*BOOL*/
popRestriction(
	RestrictStackT   **spp,
	const restrict_u **opp
	)
{
	RestrictStackT *sp;

	if (NULL == (sp = *spp) || sp->fcnt >= getStackSheetSize(sp))
		return FALSE;
	
	*opp = sp->pres[sp->fcnt++];
	if (sp->fcnt >= getStackSheetSize(sp)) {
		/* discard sheet from scratch pad */
		*spp = sp->link;
		free(sp);
	}
	return TRUE;
}

static void
flushRestrictionStack(
	RestrictStackT **spp
	)
{
	RestrictStackT *sp;

	while (NULL != (sp = *spp)) {
		*spp = sp->link;
		free(sp);
	}
}

/*
 * list_restrict4 - iterative helper for list_restrict dumps IPv4
 *		    restriction list in reverse order.
 */
static void
list_restrict4(
	const restrict_u *	res,
	struct info_restrict **	ppir
	)
{
	RestrictStackT *	rpad;
	struct info_restrict *	pir;

	pir = *ppir;
	for (rpad = NULL; res; res = res->link)
		if (!pushRestriction(&rpad, res))
			break;
	
	while (pir && popRestriction(&rpad, &res)) {
		pir->addr = htonl(res->u.v4.addr);
		if (client_v6_capable) 
			pir->v6_flag = 0;
		pir->mask = htonl(res->u.v4.mask);
		pir->count = htonl(res->count);
		pir->rflags = htons(res->rflags);
		pir->mflags = htons(res->mflags);
		pir = (struct info_restrict *)more_pkt();
	}
	flushRestrictionStack(&rpad);
	*ppir = pir;
}

/*
 * list_restrict6 - iterative helper for list_restrict dumps IPv6
 *		    restriction list in reverse order.
 */
static void
list_restrict6(
	const restrict_u *	res,
	struct info_restrict **	ppir
	)
{
	RestrictStackT *	rpad;
	struct info_restrict *	pir;

	pir = *ppir;
	for (rpad = NULL; res; res = res->link)
		if (!pushRestriction(&rpad, res))
			break;

	while (pir && popRestriction(&rpad, &res)) {
		pir->addr6 = res->u.v6.addr; 
		pir->mask6 = res->u.v6.mask;
		pir->v6_flag = 1;
		pir->count = htonl(res->count);
		pir->rflags = htons(res->rflags);
		pir->mflags = htons(res->mflags);
		pir = (struct info_restrict *)more_pkt();
	}
	flushRestrictionStack(&rpad);
	*ppir = pir;
}


/*
 * list_restrict - return the restrict list
 */
static void
list_restrict(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	struct info_restrict *ir;

	DPRINTF(3, ("wants restrict list summary\n"));

	ir = (struct info_restrict *)prepare_pkt(srcadr, inter, inpkt,
	    v6sizeof(struct info_restrict));
	
	/*
	 * The restriction lists are kept sorted in the reverse order
	 * than they were originally.  To preserve the output semantics,
	 * dump each list in reverse order. The workers take care of that.
	 */
	list_restrict4(restrictlist4, &ir);
	if (client_v6_capable)
		list_restrict6(restrictlist6, &ir);
	flush_pkt();
}


/*
 * do_resaddflags - add flags to a restrict entry (or create one)
 */
static void
do_resaddflags(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	do_restrict(srcadr, inter, inpkt, RESTRICT_FLAGS);
}



/*
 * do_ressubflags - remove flags from a restrict entry
 */
static void
do_ressubflags(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	do_restrict(srcadr, inter, inpkt, RESTRICT_UNFLAG);
}


/*
 * do_unrestrict - remove a restrict entry from the list
 */
static void
do_unrestrict(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	do_restrict(srcadr, inter, inpkt, RESTRICT_REMOVE);
}


/*
 * do_restrict - do the dirty stuff of dealing with restrictions
 */
static void
do_restrict(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt,
	restrict_op op
	)
{
	char *			datap;
	struct conf_restrict	cr;
	u_short			items;
	size_t			item_sz;
	sockaddr_u		matchaddr;
	sockaddr_u		matchmask;
	int			bad;

	switch(op) {
	    case RESTRICT_FLAGS:
	    case RESTRICT_UNFLAG:
	    case RESTRICT_REMOVE:
	    case RESTRICT_REMOVEIF:
	    	break;

	    default:
		req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
		return;
	}

	/*
	 * Do a check of the flags to make sure that only
	 * the NTPPORT flag is set, if any.  If not, complain
	 * about it.  Note we are very picky here.
	 */
	items = INFO_NITEMS(inpkt->err_nitems);
	item_sz = INFO_ITEMSIZE(inpkt->mbz_itemsize);
	datap = inpkt->u.data;
	if (item_sz > sizeof(cr)) {
		req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
		return;
	}

	bad = 0;
	while (items-- > 0 && !bad) {
		memcpy(&cr, datap, item_sz);
		cr.flags = ntohs(cr.flags);
		cr.mflags = ntohs(cr.mflags);
		if (~RESM_NTPONLY & cr.mflags)
			bad |= 1;
		if (~RES_ALLFLAGS & cr.flags)
			bad |= 2;
		if (INADDR_ANY != cr.mask) {
			if (client_v6_capable && cr.v6_flag) {
				if (IN6_IS_ADDR_UNSPECIFIED(&cr.addr6))
					bad |= 4;
			} else {
				if (INADDR_ANY == cr.addr)
					bad |= 8;
			}
		}
		datap += item_sz;
	}

	if (bad) {
		msyslog(LOG_ERR, "do_restrict: bad = %#x", bad);
		req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
		return;
	}

	/*
	 * Looks okay, try it out.  Needs to reload data pointer and
	 * item counter. (Talos-CAN-0052)
	 */
	ZERO_SOCK(&matchaddr);
	ZERO_SOCK(&matchmask);
	items = INFO_NITEMS(inpkt->err_nitems);
	datap = inpkt->u.data;

	while (items-- > 0) {
		memcpy(&cr, datap, item_sz);
		cr.flags = ntohs(cr.flags);
		cr.mflags = ntohs(cr.mflags);
		cr.ippeerlimit = ntohs(cr.ippeerlimit);
		if (client_v6_capable && cr.v6_flag) {
			AF(&matchaddr) = AF_INET6;
			AF(&matchmask) = AF_INET6;
			SOCK_ADDR6(&matchaddr) = cr.addr6;
			SOCK_ADDR6(&matchmask) = cr.mask6;
		} else {
			AF(&matchaddr) = AF_INET;
			AF(&matchmask) = AF_INET;
			NSRCADR(&matchaddr) = cr.addr;
			NSRCADR(&matchmask) = cr.mask;
		}
		hack_restrict(op, &matchaddr, &matchmask, cr.mflags,
			      cr.ippeerlimit, cr.flags, 0);
		datap += item_sz;
	}

	req_ack(srcadr, inter, inpkt, INFO_OKAY);
}


/*
 * mon_getlist - return monitor data
 */
static void
mon_getlist(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	req_ack(srcadr, inter, inpkt, INFO_ERR_NODATA);
}


/*
 * Module entry points and the flags they correspond with
 */
struct reset_entry {
	int flag;		/* flag this corresponds to */
	void (*handler)(void);	/* routine to handle request */
};

struct reset_entry reset_entries[] = {
	{ RESET_FLAG_ALLPEERS,	peer_all_reset },
	{ RESET_FLAG_IO,	io_clr_stats },
	{ RESET_FLAG_SYS,	proto_clr_stats },
	{ RESET_FLAG_MEM,	peer_clr_stats },
	{ RESET_FLAG_TIMER,	timer_clr_stats },
	{ RESET_FLAG_AUTH,	reset_auth_stats },
	{ RESET_FLAG_CTL,	ctl_clr_stats },
	{ 0,			0 }
};

/*
 * reset_stats - reset statistic counters here and there
 */
static void
reset_stats(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	struct reset_flags *rflags;
	u_long flags;
	struct reset_entry *rent;

	if (INFO_NITEMS(inpkt->err_nitems) > 1) {
		msyslog(LOG_ERR, "reset_stats: err_nitems > 1");
		req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
		return;
	}

	rflags = (struct reset_flags *)&inpkt->u;
	flags = ntohl(rflags->flags);

	if (flags & ~RESET_ALLFLAGS) {
		msyslog(LOG_ERR, "reset_stats: reset leaves %#lx",
			flags & ~RESET_ALLFLAGS);
		req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
		return;
	}

	for (rent = reset_entries; rent->flag != 0; rent++) {
		if (flags & rent->flag)
			(*rent->handler)();
	}
	req_ack(srcadr, inter, inpkt, INFO_OKAY);
}


/*
 * reset_peer - clear a peer's statistics
 */
static void
reset_peer(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	u_short			items;
	size_t			item_sz;
	char *			datap;
	struct conf_unpeer	cp;
	struct peer *		p;
	sockaddr_u		peeraddr;
	int			bad;

	/*
	 * We check first to see that every peer exists.  If not,
	 * we return an error.
	 */

	items = INFO_NITEMS(inpkt->err_nitems);
	item_sz = INFO_ITEMSIZE(inpkt->mbz_itemsize);
	datap = inpkt->u.data;
	if (item_sz > sizeof(cp)) {
		req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
		return;
	}

	bad = FALSE;
	while (items-- > 0 && !bad) {
		ZERO(cp);
		memcpy(&cp, datap, item_sz);
		ZERO_SOCK(&peeraddr);
		if (client_v6_capable && cp.v6_flag) {
			AF(&peeraddr) = AF_INET6;
			SOCK_ADDR6(&peeraddr) = cp.peeraddr6;
		} else {
			AF(&peeraddr) = AF_INET;
			NSRCADR(&peeraddr) = cp.peeraddr;
		}

#ifdef ISC_PLATFORM_HAVESALEN
		peeraddr.sa.sa_len = SOCKLEN(&peeraddr);
#endif
		p = findexistingpeer(&peeraddr, NULL, NULL, -1, 0, NULL);
		if (NULL == p)
			bad++;
		datap += item_sz;
	}

	if (bad) {
		req_ack(srcadr, inter, inpkt, INFO_ERR_NODATA);
		return;
	}

	/*
	 * Now do it in earnest. Needs to reload data pointer and item
	 * counter. (Talos-CAN-0052)
	 */
	
	items = INFO_NITEMS(inpkt->err_nitems);
	datap = inpkt->u.data;
	while (items-- > 0) {
		ZERO(cp);
		memcpy(&cp, datap, item_sz);
		ZERO_SOCK(&peeraddr);
		if (client_v6_capable && cp.v6_flag) {
			AF(&peeraddr) = AF_INET6;
			SOCK_ADDR6(&peeraddr) = cp.peeraddr6;
		} else {
			AF(&peeraddr) = AF_INET;
			NSRCADR(&peeraddr) = cp.peeraddr;
		}
		SET_PORT(&peeraddr, 123);
#ifdef ISC_PLATFORM_HAVESALEN
		peeraddr.sa.sa_len = SOCKLEN(&peeraddr);
#endif
		p = findexistingpeer(&peeraddr, NULL, NULL, -1, 0, NULL);
		while (p != NULL) {
			peer_reset(p);
			p = findexistingpeer(&peeraddr, NULL, p, -1, 0, NULL);
		}
		datap += item_sz;
	}

	req_ack(srcadr, inter, inpkt, INFO_OKAY);
}


/*
 * do_key_reread - reread the encryption key file
 */
static void
do_key_reread(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	rereadkeys();
	req_ack(srcadr, inter, inpkt, INFO_OKAY);
}


/*
 * trust_key - make one or more keys trusted
 */
static void
trust_key(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	do_trustkey(srcadr, inter, inpkt, 1);
}


/*
 * untrust_key - make one or more keys untrusted
 */
static void
untrust_key(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	do_trustkey(srcadr, inter, inpkt, 0);
}


/*
 * do_trustkey - make keys either trustable or untrustable
 */
static void
do_trustkey(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt,
	u_long trust
	)
{
	register uint32_t *kp;
	register int items;

	items = INFO_NITEMS(inpkt->err_nitems);
	kp = (uint32_t *)&inpkt->u;
	while (items-- > 0) {
		authtrust(*kp, trust);
		kp++;
	}

	req_ack(srcadr, inter, inpkt, INFO_OKAY);
}


/*
 * get_auth_info - return some stats concerning the authentication module
 */
static void
get_auth_info(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	register struct info_auth *ia;

	ia = (struct info_auth *)prepare_pkt(srcadr, inter, inpkt,
					     sizeof(struct info_auth));

	ia->numkeys = htonl((u_int32)authnumkeys);
	ia->numfreekeys = htonl((u_int32)authnumfreekeys);
	ia->keylookups = htonl((u_int32)authkeylookups);
	ia->keynotfound = htonl((u_int32)authkeynotfound);
	ia->encryptions = htonl((u_int32)authencryptions);
	ia->decryptions = htonl((u_int32)authdecryptions);
	ia->keyuncached = htonl((u_int32)authkeyuncached);
	ia->expired = htonl((u_int32)authkeyexpired);
	ia->timereset = htonl((u_int32)(current_time - auth_timereset));
	
	(void) more_pkt();
	flush_pkt();
}



/*
 * reset_auth_stats - reset the authentication stat counters.  Done here
 *		      to keep ntp-isms out of the authentication module
 */
void
reset_auth_stats(void)
{
	authkeylookups = 0;
	authkeynotfound = 0;
	authencryptions = 0;
	authdecryptions = 0;
	authkeyuncached = 0;
	auth_timereset = current_time;
}


/*
 * req_get_traps - return information about current trap holders
 */
static void
req_get_traps(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	struct info_trap *it;
	struct ctl_trap *tr;
	size_t i;

	if (num_ctl_traps == 0) {
		req_ack(srcadr, inter, inpkt, INFO_ERR_NODATA);
		return;
	}

	it = (struct info_trap *)prepare_pkt(srcadr, inter, inpkt,
	    v6sizeof(struct info_trap));

	for (i = 0, tr = ctl_traps; it && i < COUNTOF(ctl_traps); i++, tr++) {
		if (tr->tr_flags & TRAP_INUSE) {
			if (IS_IPV4(&tr->tr_addr)) {
				if (tr->tr_localaddr == any_interface)
					it->local_address = 0;
				else
					it->local_address
					    = NSRCADR(&tr->tr_localaddr->sin);
				it->trap_address = NSRCADR(&tr->tr_addr);
				if (client_v6_capable)
					it->v6_flag = 0;
			} else {
				if (!client_v6_capable)
					continue;
				it->local_address6 
				    = SOCK_ADDR6(&tr->tr_localaddr->sin);
				it->trap_address6 = SOCK_ADDR6(&tr->tr_addr);
				it->v6_flag = 1;
			}
			it->trap_port = NSRCPORT(&tr->tr_addr);
			it->sequence = htons(tr->tr_sequence);
			it->settime = htonl((u_int32)(current_time - tr->tr_settime));
			it->origtime = htonl((u_int32)(current_time - tr->tr_origtime));
			it->resets = htonl((u_int32)tr->tr_resets);
			it->flags = htonl((u_int32)tr->tr_flags);
			it = (struct info_trap *)more_pkt();
		}
	}
	flush_pkt();
}


/*
 * req_set_trap - configure a trap
 */
static void
req_set_trap(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	do_setclr_trap(srcadr, inter, inpkt, 1);
}



/*
 * req_clr_trap - unconfigure a trap
 */
static void
req_clr_trap(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	do_setclr_trap(srcadr, inter, inpkt, 0);
}



/*
 * do_setclr_trap - do the grunge work of (un)configuring a trap
 */
static void
do_setclr_trap(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt,
	int set
	)
{
	register struct conf_trap *ct;
	register endpt *linter;
	int res;
	sockaddr_u laddr;

	/*
	 * Prepare sockaddr
	 */
	ZERO_SOCK(&laddr);
	AF(&laddr) = AF(srcadr);
	SET_PORT(&laddr, NTP_PORT);

	/*
	 * Restrict ourselves to one item only.  This eliminates
	 * the error reporting problem.
	 */
	if (INFO_NITEMS(inpkt->err_nitems) > 1) {
		msyslog(LOG_ERR, "do_setclr_trap: err_nitems > 1");
		req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
		return;
	}
	ct = (struct conf_trap *)&inpkt->u;

	/*
	 * Look for the local interface.  If none, use the default.
	 */
	if (ct->local_address == 0) {
		linter = any_interface;
	} else {
		if (IS_IPV4(&laddr))
			NSRCADR(&laddr) = ct->local_address;
		else
			SOCK_ADDR6(&laddr) = ct->local_address6;
		linter = findinterface(&laddr);
		if (NULL == linter) {
			req_ack(srcadr, inter, inpkt, INFO_ERR_NODATA);
			return;
		}
	}

	if (IS_IPV4(&laddr))
		NSRCADR(&laddr) = ct->trap_address;
	else
		SOCK_ADDR6(&laddr) = ct->trap_address6;
	if (ct->trap_port)
		NSRCPORT(&laddr) = ct->trap_port;
	else
		SET_PORT(&laddr, TRAPPORT);

	if (set) {
		res = ctlsettrap(&laddr, linter, 0,
				 INFO_VERSION(inpkt->rm_vn_mode));
	} else {
		res = ctlclrtrap(&laddr, linter, 0);
	}

	if (!res) {
		req_ack(srcadr, inter, inpkt, INFO_ERR_NODATA);
	} else {
		req_ack(srcadr, inter, inpkt, INFO_OKAY);
	}
	return;
}

/*
 * Validate a request packet for a new request or control key:
 *  - only one item allowed
 *  - key must be valid (that is, known, and not in the autokey range)
 */
static void
set_keyid_checked(
	keyid_t        *into,
	const char     *what,
	sockaddr_u     *srcadr,
	endpt          *inter,
	struct req_pkt *inpkt
	)
{
	keyid_t *pkeyid;
	keyid_t  tmpkey;

	/* restrict ourselves to one item only */
	if (INFO_NITEMS(inpkt->err_nitems) > 1) {
		msyslog(LOG_ERR, "set_keyid_checked[%s]: err_nitems > 1",
			what);
		req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
		return;
	}

	/* plug the new key from the packet */
	pkeyid = (keyid_t *)&inpkt->u;
	tmpkey = ntohl(*pkeyid);

	/* validate the new key id, claim data error on failure */
	if (tmpkey < 1 || tmpkey > NTP_MAXKEY || !auth_havekey(tmpkey)) {
		msyslog(LOG_ERR, "set_keyid_checked[%s]: invalid key id: %ld",
			what, (long)tmpkey);
		req_ack(srcadr, inter, inpkt, INFO_ERR_NODATA);
		return;
	}

	/* if we arrive here, the key is good -- use it */
	*into = tmpkey;
	req_ack(srcadr, inter, inpkt, INFO_OKAY);
}

/*
 * set_request_keyid - set the keyid used to authenticate requests
 */
static void
set_request_keyid(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	set_keyid_checked(&info_auth_keyid, "request",
			  srcadr, inter, inpkt);
}



/*
 * set_control_keyid - set the keyid used to authenticate requests
 */
static void
set_control_keyid(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	set_keyid_checked(&ctl_auth_keyid, "control",
			  srcadr, inter, inpkt);
}



/*
 * get_ctl_stats - return some stats concerning the control message module
 */
static void
get_ctl_stats(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	register struct info_control *ic;

	ic = (struct info_control *)prepare_pkt(srcadr, inter, inpkt,
						sizeof(struct info_control));

	ic->ctltimereset = htonl((u_int32)(current_time - ctltimereset));
	ic->numctlreq = htonl((u_int32)numctlreq);
	ic->numctlbadpkts = htonl((u_int32)numctlbadpkts);
	ic->numctlresponses = htonl((u_int32)numctlresponses);
	ic->numctlfrags = htonl((u_int32)numctlfrags);
	ic->numctlerrors = htonl((u_int32)numctlerrors);
	ic->numctltooshort = htonl((u_int32)numctltooshort);
	ic->numctlinputresp = htonl((u_int32)numctlinputresp);
	ic->numctlinputfrag = htonl((u_int32)numctlinputfrag);
	ic->numctlinputerr = htonl((u_int32)numctlinputerr);
	ic->numctlbadoffset = htonl((u_int32)numctlbadoffset);
	ic->numctlbadversion = htonl((u_int32)numctlbadversion);
	ic->numctldatatooshort = htonl((u_int32)numctldatatooshort);
	ic->numctlbadop = htonl((u_int32)numctlbadop);
	ic->numasyncmsgs = htonl((u_int32)numasyncmsgs);

	(void) more_pkt();
	flush_pkt();
}


#ifdef KERNEL_PLL
/*
 * get_kernel_info - get kernel pll/pps information
 */
static void
get_kernel_info(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	register struct info_kernel *ik;
	struct timex ntx;

	if (!pll_control) {
		req_ack(srcadr, inter, inpkt, INFO_ERR_NODATA);
		return;
	}

	ZERO(ntx);
	if (ntp_adjtime(&ntx) < 0)
		msyslog(LOG_ERR, "get_kernel_info: ntp_adjtime() failed: %m");
	ik = (struct info_kernel *)prepare_pkt(srcadr, inter, inpkt,
	    sizeof(struct info_kernel));

	/*
	 * pll variables
	 */
	ik->offset = htonl((u_int32)ntx.offset);
	ik->freq = htonl((u_int32)ntx.freq);
	ik->maxerror = htonl((u_int32)ntx.maxerror);
	ik->esterror = htonl((u_int32)ntx.esterror);
	ik->status = htons(ntx.status);
	ik->constant = htonl((u_int32)ntx.constant);
	ik->precision = htonl((u_int32)ntx.precision);
	ik->tolerance = htonl((u_int32)ntx.tolerance);

	/*
	 * pps variables
	 */
	ik->ppsfreq = htonl((u_int32)ntx.ppsfreq);
	ik->jitter = htonl((u_int32)ntx.jitter);
	ik->shift = htons(ntx.shift);
	ik->stabil = htonl((u_int32)ntx.stabil);
	ik->jitcnt = htonl((u_int32)ntx.jitcnt);
	ik->calcnt = htonl((u_int32)ntx.calcnt);
	ik->errcnt = htonl((u_int32)ntx.errcnt);
	ik->stbcnt = htonl((u_int32)ntx.stbcnt);
	
	(void) more_pkt();
	flush_pkt();
}
#endif /* KERNEL_PLL */


#ifdef REFCLOCK
/*
 * get_clock_info - get info about a clock
 */
static void
get_clock_info(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	register struct info_clock *ic;
	register u_int32 *clkaddr;
	register int items;
	struct refclockstat clock_stat;
	sockaddr_u addr;
	l_fp ltmp;

	ZERO_SOCK(&addr);
	AF(&addr) = AF_INET;
#ifdef ISC_PLATFORM_HAVESALEN
	addr.sa.sa_len = SOCKLEN(&addr);
#endif
	SET_PORT(&addr, NTP_PORT);
	items = INFO_NITEMS(inpkt->err_nitems);
	clkaddr = &inpkt->u.u32[0];

	ic = (struct info_clock *)prepare_pkt(srcadr, inter, inpkt,
					      sizeof(struct info_clock));

	while (items-- > 0 && ic) {
		NSRCADR(&addr) = *clkaddr++;
		if (!ISREFCLOCKADR(&addr) || NULL ==
		    findexistingpeer(&addr, NULL, NULL, -1, 0, NULL)) {
			req_ack(srcadr, inter, inpkt, INFO_ERR_NODATA);
			return;
		}

		clock_stat.kv_list = (struct ctl_var *)0;

		refclock_control(&addr, NULL, &clock_stat);

		ic->clockadr = NSRCADR(&addr);
		ic->type = clock_stat.type;
		ic->flags = clock_stat.flags;
		ic->lastevent = clock_stat.lastevent;
		ic->currentstatus = clock_stat.currentstatus;
		ic->polls = htonl((u_int32)clock_stat.polls);
		ic->noresponse = htonl((u_int32)clock_stat.noresponse);
		ic->badformat = htonl((u_int32)clock_stat.badformat);
		ic->baddata = htonl((u_int32)clock_stat.baddata);
		ic->timestarted = htonl((u_int32)clock_stat.timereset);
		DTOLFP(clock_stat.fudgetime1, &ltmp);
		HTONL_FP(&ltmp, &ic->fudgetime1);
		DTOLFP(clock_stat.fudgetime2, &ltmp);
		HTONL_FP(&ltmp, &ic->fudgetime2);
		ic->fudgeval1 = htonl((u_int32)clock_stat.fudgeval1);
		/* [Bug3527] Backward Incompatible: ic->fudgeval2 is
		 * a string, instantiated via memcpy() so there is no
		 * endian issue to correct.
		 */
#ifdef DISABLE_BUG3527_FIX
		ic->fudgeval2 = htonl(clock_stat.fudgeval2);
#else
		ic->fudgeval2 = clock_stat.fudgeval2;
#endif

		free_varlist(clock_stat.kv_list);

		ic = (struct info_clock *)more_pkt();
	}
	flush_pkt();
}



/*
 * set_clock_fudge - get a clock's fudge factors
 */
static void
set_clock_fudge(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	register struct conf_fudge *cf;
	register int items;
	struct refclockstat clock_stat;
	sockaddr_u addr;
	l_fp ltmp;

	ZERO(addr);
	ZERO(clock_stat);
	items = INFO_NITEMS(inpkt->err_nitems);
	cf = (struct conf_fudge *)&inpkt->u;

	while (items-- > 0) {
		AF(&addr) = AF_INET;
		NSRCADR(&addr) = cf->clockadr;
#ifdef ISC_PLATFORM_HAVESALEN
		addr.sa.sa_len = SOCKLEN(&addr);
#endif
		SET_PORT(&addr, NTP_PORT);
		if (!ISREFCLOCKADR(&addr) || NULL ==
		    findexistingpeer(&addr, NULL, NULL, -1, 0, NULL)) {
			req_ack(srcadr, inter, inpkt, INFO_ERR_NODATA);
			return;
		}

		switch(ntohl(cf->which)) {
		    case FUDGE_TIME1:
			NTOHL_FP(&cf->fudgetime, &ltmp);
			LFPTOD(&ltmp, clock_stat.fudgetime1);
			clock_stat.haveflags = CLK_HAVETIME1;
			break;
		    case FUDGE_TIME2:
			NTOHL_FP(&cf->fudgetime, &ltmp);
			LFPTOD(&ltmp, clock_stat.fudgetime2);
			clock_stat.haveflags = CLK_HAVETIME2;
			break;
		    case FUDGE_VAL1:
			clock_stat.fudgeval1 = ntohl(cf->fudgeval_flags);
			clock_stat.haveflags = CLK_HAVEVAL1;
			break;
		    case FUDGE_VAL2:
			clock_stat.fudgeval2 = ntohl(cf->fudgeval_flags);
			clock_stat.haveflags = CLK_HAVEVAL2;
			break;
		    case FUDGE_FLAGS:
			clock_stat.flags = (u_char) (ntohl(cf->fudgeval_flags) & 0xf);
			clock_stat.haveflags =
				(CLK_HAVEFLAG1|CLK_HAVEFLAG2|CLK_HAVEFLAG3|CLK_HAVEFLAG4);
			break;
		    default:
			msyslog(LOG_ERR, "set_clock_fudge: default!");
			req_ack(srcadr, inter, inpkt, INFO_ERR_FMT);
			return;
		}

		refclock_control(&addr, &clock_stat, (struct refclockstat *)0);
	}

	req_ack(srcadr, inter, inpkt, INFO_OKAY);
}
#endif

#ifdef REFCLOCK
/*
 * get_clkbug_info - get debugging info about a clock
 */
static void
get_clkbug_info(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	register int i;
	register struct info_clkbug *ic;
	register u_int32 *clkaddr;
	register int items;
	struct refclockbug bug;
	sockaddr_u addr;

	ZERO_SOCK(&addr);
	AF(&addr) = AF_INET;
#ifdef ISC_PLATFORM_HAVESALEN
	addr.sa.sa_len = SOCKLEN(&addr);
#endif
	SET_PORT(&addr, NTP_PORT);
	items = INFO_NITEMS(inpkt->err_nitems);
	clkaddr = (u_int32 *)&inpkt->u;

	ic = (struct info_clkbug *)prepare_pkt(srcadr, inter, inpkt,
					       sizeof(struct info_clkbug));

	while (items-- > 0 && ic) {
		NSRCADR(&addr) = *clkaddr++;
		if (!ISREFCLOCKADR(&addr) || NULL ==
		    findexistingpeer(&addr, NULL, NULL, -1, 0, NULL)) {
			req_ack(srcadr, inter, inpkt, INFO_ERR_NODATA);
			return;
		}

		ZERO(bug);
		refclock_buginfo(&addr, &bug);
		if (bug.nvalues == 0 && bug.ntimes == 0) {
			req_ack(srcadr, inter, inpkt, INFO_ERR_NODATA);
			return;
		}

		ic->clockadr = NSRCADR(&addr);
		i = bug.nvalues;
		if (i > NUMCBUGVALUES)
		    i = NUMCBUGVALUES;
		ic->nvalues = (u_char)i;
		ic->svalues = htons((u_short) (bug.svalues & ((1<<i)-1)));
		while (--i >= 0)
		    ic->values[i] = htonl(bug.values[i]);

		i = bug.ntimes;
		if (i > NUMCBUGTIMES)
		    i = NUMCBUGTIMES;
		ic->ntimes = (u_char)i;
		ic->stimes = htonl(bug.stimes);
		while (--i >= 0) {
			HTONL_FP(&bug.times[i], &ic->times[i]);
		}

		ic = (struct info_clkbug *)more_pkt();
	}
	flush_pkt();
}
#endif

/*
 * receiver of interface structures
 */
static void
fill_info_if_stats(void *data, interface_info_t *interface_info)
{
	struct info_if_stats **ifsp = (struct info_if_stats **)data;
	struct info_if_stats *ifs = *ifsp;
	endpt *ep = interface_info->ep;

	if (NULL == ifs)
		return;
	
	ZERO(*ifs);
	
	if (IS_IPV6(&ep->sin)) {
		if (!client_v6_capable)
			return;
		ifs->v6_flag = 1;
		ifs->unaddr.addr6 = SOCK_ADDR6(&ep->sin);
		ifs->unbcast.addr6 = SOCK_ADDR6(&ep->bcast);
		ifs->unmask.addr6 = SOCK_ADDR6(&ep->mask);
	} else {
		ifs->v6_flag = 0;
		ifs->unaddr.addr = SOCK_ADDR4(&ep->sin);
		ifs->unbcast.addr = SOCK_ADDR4(&ep->bcast);
		ifs->unmask.addr = SOCK_ADDR4(&ep->mask);
	}
	ifs->v6_flag = htonl(ifs->v6_flag);
	strlcpy(ifs->name, ep->name, sizeof(ifs->name));
	ifs->family = htons(ep->family);
	ifs->flags = htonl(ep->flags);
	ifs->last_ttl = htonl(ep->last_ttl);
	ifs->num_mcast = htonl(ep->num_mcast);
	ifs->received = htonl(ep->received);
	ifs->sent = htonl(ep->sent);
	ifs->notsent = htonl(ep->notsent);
	ifs->ifindex = htonl(ep->ifindex);
	/* scope no longer in endpt, in in6_addr typically */
	ifs->scopeid = ifs->ifindex;
	ifs->ifnum = htonl(ep->ifnum);
	ifs->uptime = htonl(current_time - ep->starttime);
	ifs->ignore_packets = ep->ignore_packets;
	ifs->peercnt = htonl(ep->peercnt);
	ifs->action = interface_info->action;
	
	*ifsp = (struct info_if_stats *)more_pkt();
}

/*
 * get_if_stats - get interface statistics
 */
static void
get_if_stats(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	struct info_if_stats *ifs;

	DPRINTF(3, ("wants interface statistics\n"));

	ifs = (struct info_if_stats *)prepare_pkt(srcadr, inter, inpkt,
	    v6sizeof(struct info_if_stats));

	interface_enumerate(fill_info_if_stats, &ifs);
	
	flush_pkt();
}

static void
do_if_reload(
	sockaddr_u *srcadr,
	endpt *inter,
	struct req_pkt *inpkt
	)
{
	struct info_if_stats *ifs;

	DPRINTF(3, ("wants interface reload\n"));

	ifs = (struct info_if_stats *)prepare_pkt(srcadr, inter, inpkt,
	    v6sizeof(struct info_if_stats));

	interface_update(fill_info_if_stats, &ifs);
	
	flush_pkt();
}

