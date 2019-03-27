/*
 * ntp_request.h - definitions for the ntpd remote query facility
 */

#ifndef NTP_REQUEST_H
#define NTP_REQUEST_H

#include "stddef.h"
#include "ntp_types.h"
#include "recvbuff.h"

/*
 * A mode 7 packet is used exchanging data between an NTP server
 * and a client for purposes other than time synchronization, e.g.
 * monitoring, statistics gathering and configuration.  A mode 7
 * packet has the following format:
 *
 *    0			  1		      2			  3
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |R|M| VN  | Mode|A|  Sequence   | Implementation|   Req Code    |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |  Err  | Number of data items  |  MBZ  |   Size of data item   |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |								     |
 *   |            Data (Minimum 0 octets, maximum 500 octets)        |
 *   |								     |
 *                            [...]
 *   |								     |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |               Encryption Keyid (when A bit set)               |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |								     |
 *   |          Message Authentication Code (when A bit set)         |
 *   |								     |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * where the fields are (note that the client sends requests, the server
 * responses):
 *
 * Response Bit:  This packet is a response (if clear, packet is a request).
 *
 * More Bit:	Set for all packets but the last in a response which
 *		requires more than one packet.
 *
 * Version Number: 2 for current version
 *
 * Mode:	Always 7
 *
 * Authenticated bit: If set, this packet is authenticated.
 *
 * Sequence number: For a multipacket response, contains the sequence
 *		number of this packet.  0 is the first in the sequence,
 *		127 (or less) is the last.  The More Bit must be set in
 *		all packets but the last.
 *
 * Implementation number: The number of the implementation this request code
 *		is defined by.  An implementation number of zero is used
 *		for requst codes/data formats which all implementations
 *		agree on.  Implementation number 255 is reserved (for
 *		extensions, in case we run out).
 *
 * Request code: An implementation-specific code which specifies the
 *		operation to be (which has been) performed and/or the
 *		format and semantics of the data included in the packet.
 *
 * Err:		Must be 0 for a request.  For a response, holds an error
 *		code relating to the request.  If nonzero, the operation
 *		requested wasn't performed.
 *
 *		0 - no error
 *		1 - incompatible implementation number
 *		2 - unimplemented request code
 *		3 - format error (wrong data items, data size, packet size etc.)
 *		4 - no data available (e.g. request for details on unknown peer)
 *		5-6 I don't know
 *		7 - authentication failure (i.e. permission denied)
 *
 * Number of data items: number of data items in packet.  0 to 500
 *
 * MBZ:		A reserved data field, must be zero in requests and responses.
 *
 * Size of data item: size of each data item in packet.  0 to 500
 *
 * Data:	Variable sized area containing request/response data.  For
 *		requests and responses the size in octets must be greater
 *		than or equal to the product of the number of data items
 *		and the size of a data item.  For requests the data area
 *		must be exactly 40 octets in length.  For responses the
 *		data area may be any length between 0 and 500 octets
 *		inclusive.
 *
 * Message Authentication Code: Same as NTP spec, in definition and function.
 *		May optionally be included in requests which require
 *		authentication, is never included in responses.
 *
 * The version number, mode and keyid have the same function and are
 * in the same location as a standard NTP packet.  The request packet
 * is the same size as a standard NTP packet to ease receive buffer
 * management, and to allow the same encryption procedure to be used
 * both on mode 7 and standard NTP packets.  The mac is included when
 * it is required that a request be authenticated, the keyid should be
 * zero in requests in which the mac is not included.
 *
 * The data format depends on the implementation number/request code pair
 * and whether the packet is a request or a response.  The only requirement
 * is that data items start in the octet immediately following the size
 * word and that data items be concatenated without padding between (i.e.
 * if the data area is larger than data_items*size, all padding is at
 * the end).  Padding is ignored, other than for encryption purposes.
 * Implementations using encryption might want to include a time stamp
 * or other data in the request packet padding.  The key used for requests
 * is implementation defined, but key 15 is suggested as a default.
 */

/*
 * union of raw addresses to save space
 */
union addrun {
	struct in6_addr addr6;
	struct in_addr  addr;
};

#define	MODE7_PAYLOAD_LIM	176

typedef union req_data_u_tag {
	u_int32	u32[MODE7_PAYLOAD_LIM / sizeof(u_int32)];
	char data[MODE7_PAYLOAD_LIM];	/* data area (176 byte max) */
} req_data_u;				/* struct conf_peer must fit */

/*
 * A request packet.  These are almost a fixed length.
 */
struct req_pkt {
	u_char rm_vn_mode;		/* response, more, version, mode */
	u_char auth_seq;		/* key, sequence number */
	u_char implementation;		/* implementation number */
	u_char request;			/* request number */
	u_short err_nitems;		/* error code/number of data items */
	u_short mbz_itemsize;		/* item size */
	req_data_u u;			/* data area */
	l_fp tstamp;			/* time stamp, for authentication */
	keyid_t keyid;			/* (optional) encryption key */
	char mac[MAX_MDG_LEN];		/* (optional) auth code */
};

/*
 * The req_pkt_tail structure is used by ntpd to adjust for different
 * packet sizes that may arrive.
 */
struct req_pkt_tail {
	l_fp tstamp;			/* time stamp, for authentication */
	keyid_t keyid;			/* (optional) encryption key */
	char mac[MAX_MDG_LEN];		/* (optional) auth code */
};

/* MODE_PRIVATE request packet header length before optional items. */
#define	REQ_LEN_HDR	(offsetof(struct req_pkt, u))
/* MODE_PRIVATE request packet fixed length without MAC. */
#define	REQ_LEN_NOMAC	(offsetof(struct req_pkt, keyid))
/* MODE_PRIVATE req_pkt_tail minimum size (16 octet digest) */
#define REQ_TAIL_MIN	\
	(sizeof(struct req_pkt_tail) - (MAX_MAC_LEN - MAX_MD5_LEN))

/*
 * A MODE_PRIVATE response packet.  The length here is variable, this
 * is a maximally sized one.  Note that this implementation doesn't
 * authenticate responses.
 */
#define	RESP_HEADER_SIZE	(offsetof(struct resp_pkt, u))
#define	RESP_DATA_SIZE		500

typedef union resp_pkt_u_tag {
	char data[RESP_DATA_SIZE];
	u_int32 u32[RESP_DATA_SIZE / sizeof(u_int32)];
} resp_pkt_u;

struct resp_pkt {
	u_char rm_vn_mode;		/* response, more, version, mode */
	u_char auth_seq;		/* key, sequence number */
	u_char implementation;		/* implementation number */
	u_char request;			/* request number */
	u_short err_nitems;		/* error code/number of data items */
	u_short mbz_itemsize;		/* item size */
	resp_pkt_u u;			/* data area */
};


/*
 * Information error codes
 */
#define	INFO_OKAY	0
#define	INFO_ERR_IMPL	1	/* incompatible implementation */
#define	INFO_ERR_REQ	2	/* unknown request code */
#define	INFO_ERR_FMT	3	/* format error */
#define	INFO_ERR_NODATA	4	/* no data for this request */
#define	INFO_ERR_AUTH	7	/* authentication failure */
#define	MAX_INFO_ERR	INFO_ERR_AUTH

/*
 * Maximum sequence number.
 */
#define	MAXSEQ	127


/*
 * Bit setting macros for multifield items.
 */
#define	RESP_BIT	0x80
#define	MORE_BIT	0x40

#define	ISRESPONSE(rm_vn_mode)	(((rm_vn_mode)&RESP_BIT)!=0)
#define	ISMORE(rm_vn_mode)	(((rm_vn_mode)&MORE_BIT)!=0)
#define INFO_VERSION(rm_vn_mode) ((u_char)(((rm_vn_mode)>>3)&0x7))
#define	INFO_MODE(rm_vn_mode)	((rm_vn_mode)&0x7)

#define	RM_VN_MODE(resp, more, version)		\
				((u_char)(((resp)?RESP_BIT:0)\
				|((more)?MORE_BIT:0)\
				|((version?version:(NTP_OLDVERSION+1))<<3)\
				|(MODE_PRIVATE)))

#define	INFO_IS_AUTH(auth_seq)	(((auth_seq) & 0x80) != 0)
#define	INFO_SEQ(auth_seq)	((auth_seq)&0x7f)
#define	AUTH_SEQ(auth, seq)	((u_char)((((auth)!=0)?0x80:0)|((seq)&0x7f)))

#define	INFO_ERR(err_nitems)	((u_short)((ntohs(err_nitems)>>12)&0xf))
#define	INFO_NITEMS(err_nitems)	((u_short)(ntohs(err_nitems)&0xfff))
#define	ERR_NITEMS(err, nitems)	(htons((u_short)((((u_short)(err)<<12)&0xf000)\
				|((u_short)(nitems)&0xfff))))

#define	INFO_MBZ(mbz_itemsize)	((ntohs(mbz_itemsize)>>12)&0xf)
#define	INFO_ITEMSIZE(mbz_itemsize)	((u_short)(ntohs(mbz_itemsize)&0xfff))
#define	MBZ_ITEMSIZE(itemsize)	(htons((u_short)(itemsize)))


/*
 * Implementation numbers.  One for universal use and one for ntpd.
 */
#define	IMPL_UNIV	0
#define	IMPL_XNTPD_OLD	2	/* Used by pre ipv6 ntpdc */
#define	IMPL_XNTPD	3	/* Used by post ipv6 ntpdc */

/*
 * Some limits related to authentication.  Frames which are
 * authenticated must include a time stamp which differs from
 * the receive time stamp by no more than 10 seconds.
 */
#define	INFO_TS_MAXSKEW	10.

/*
 * Universal request codes go here.  There aren't any.
 */

/*
 * ntpdc -> ntpd request codes go here.
 */
#define	REQ_PEER_LIST		0	/* return list of peers */
#define	REQ_PEER_LIST_SUM	1	/* return summary info for all peers */
#define	REQ_PEER_INFO		2	/* get standard information on peer */
#define	REQ_PEER_STATS		3	/* get statistics for peer */
#define	REQ_SYS_INFO		4	/* get system information */
#define	REQ_SYS_STATS		5	/* get system stats */
#define	REQ_IO_STATS		6	/* get I/O stats */
#define REQ_MEM_STATS		7	/* stats related to peer list maint */
#define	REQ_LOOP_INFO		8	/* info from the loop filter */
#define	REQ_TIMER_STATS		9	/* get timer stats */
#define	REQ_CONFIG		10	/* configure a new peer */
#define	REQ_UNCONFIG		11	/* unconfigure an existing peer */
#define	REQ_SET_SYS_FLAG	12	/* set system flags */
#define	REQ_CLR_SYS_FLAG	13	/* clear system flags */
#define	REQ_MONITOR		14	/* (not used) */
#define	REQ_NOMONITOR		15	/* (not used) */
#define	REQ_GET_RESTRICT	16	/* return restrict list */
#define	REQ_RESADDFLAGS		17	/* add flags to restrict list */
#define	REQ_RESSUBFLAGS		18	/* remove flags from restrict list */
#define	REQ_UNRESTRICT		19	/* remove entry from restrict list */
#define	REQ_MON_GETLIST		20	/* return data collected by monitor */
#define	REQ_RESET_STATS		21	/* reset stat counters */
#define	REQ_RESET_PEER		22	/* reset peer stat counters */
#define	REQ_REREAD_KEYS		23	/* reread the encryption key file */
#define	REQ_DO_DIRTY_HACK	24	/* (not used) */
#define	REQ_DONT_DIRTY_HACK	25	/* (not used) */
#define	REQ_TRUSTKEY		26	/* add a trusted key */
#define	REQ_UNTRUSTKEY		27	/* remove a trusted key */
#define	REQ_AUTHINFO		28	/* return authentication info */
#define REQ_TRAPS		29	/* return currently set traps */
#define	REQ_ADD_TRAP		30	/* add a trap */
#define	REQ_CLR_TRAP		31	/* clear a trap */
#define	REQ_REQUEST_KEY		32	/* define a new request keyid */
#define	REQ_CONTROL_KEY		33	/* define a new control keyid */
#define	REQ_GET_CTLSTATS	34	/* get stats from the control module */
#define	REQ_GET_LEAPINFO	35	/* (not used) */
#define	REQ_GET_CLOCKINFO	36	/* get clock information */
#define	REQ_SET_CLKFUDGE	37	/* set clock fudge factors */
#define REQ_GET_KERNEL		38	/* get kernel pll/pps information */
#define	REQ_GET_CLKBUGINFO	39	/* get clock debugging info */
#define	REQ_SET_PRECISION	41	/* (not used) */
#define	REQ_MON_GETLIST_1	42	/* return collected v1 monitor data */
#define	REQ_HOSTNAME_ASSOCID	43	/* Here is a hostname + assoc_id */
#define REQ_IF_STATS		44	/* get interface statistics */
#define REQ_IF_RELOAD		45	/* reload interface list */

/* Determine size of pre-v6 version of structures */
#define v4sizeof(type)		offsetof(type, v6_flag)

/*
 * Flags in the peer information returns
 */
#define	INFO_FLAG_CONFIG	0x1
#define	INFO_FLAG_SYSPEER	0x2
#define INFO_FLAG_BURST		0x4
#define	INFO_FLAG_REFCLOCK	0x8
#define	INFO_FLAG_PREFER	0x10
#define	INFO_FLAG_AUTHENABLE	0x20
#define	INFO_FLAG_SEL_CANDIDATE	0x40
#define	INFO_FLAG_SHORTLIST	0x80
#define	INFO_FLAG_IBURST	0x100

/*
 * Flags in the system information returns
 */
#define INFO_FLAG_BCLIENT	0x1
#define INFO_FLAG_AUTHENTICATE	0x2
#define INFO_FLAG_NTP		0x4
#define INFO_FLAG_KERNEL	0x8
#define INFO_FLAG_MONITOR	0x40
#define INFO_FLAG_FILEGEN	0x80
#define INFO_FLAG_CAL		0x10
#define INFO_FLAG_PPS_SYNC	0x20

/*
 * Peer list structure.  Used to return raw lists of peers.  It goes
 * without saying that everything returned is in network byte order.
 * Well, it *would* have gone without saying, but somebody said it.
 */
struct info_peer_list {
	u_int32 addr;		/* address of peer */
	u_short port;		/* port number of peer */
	u_char hmode;		/* mode for this peer */
	u_char flags;		/* flags (from above) */
	u_int v6_flag;		/* is this v6 or not */
	u_int unused1;		/* (unused) padding for addr6 */
	struct in6_addr addr6;	/* v6 address of peer */
};


/*
 * Peer summary structure.  Sort of the info that ntpdc returns by default.
 */
struct info_peer_summary {
	u_int32 dstadr;		/* local address (zero for undetermined) */
	u_int32 srcadr;		/* source address */
	u_short srcport;	/* source port */
	u_char stratum;		/* stratum of peer */
	s_char hpoll;		/* host polling interval */
	s_char ppoll;		/* peer polling interval */
	u_char reach;		/* reachability register */
	u_char flags;		/* flags, from above */
	u_char hmode;		/* peer mode */
	s_fp delay;		/* peer.estdelay */
	l_fp offset;		/* peer.estoffset */
	u_fp dispersion;	/* peer.estdisp */
	u_int v6_flag;			/* is this v6 or not */
	u_int unused1;			/* (unused) padding for dstadr6 */
	struct in6_addr dstadr6;	/* local address (v6) */
	struct in6_addr srcadr6;	/* source address (v6) */
};


/*
 * Peer information structure.
 */
struct info_peer {
	u_int32 dstadr;		/* local address */
	u_int32	srcadr;		/* source address */
	u_short srcport;	/* remote port */
	u_char flags;		/* peer flags */
	u_char leap;		/* peer.leap */
	u_char hmode;		/* peer.hmode */
	u_char pmode;		/* peer.pmode */
	u_char stratum;		/* peer.stratum */
	u_char ppoll;		/* peer.ppoll */
	u_char hpoll;		/* peer.hpoll */
	s_char precision;	/* peer.precision */
	u_char version;		/* peer.version */
	u_char unused8;
	u_char reach;		/* peer.reach */
	u_char unreach;		/* peer.unreach */
	u_char flash;		/* old peer.flash */
	u_char ttl;		/* peer.ttl */
	u_short flash2;		/* new peer.flash */
	associd_t associd;	/* association ID */
	keyid_t keyid;		/* peer.keyid */
	u_int32 pkeyid;		/* unused */
	u_int32 refid;		/* peer.refid */
	u_int32 timer;		/* peer.timer */
	s_fp rootdelay;		/* peer.delay */
	u_fp rootdispersion;	/* peer.dispersion */
	l_fp reftime;		/* peer.reftime */
	l_fp org;		/* peer.org */
	l_fp rec;		/* peer.rec */
	l_fp xmt;		/* peer.xmt */
	s_fp filtdelay[NTP_SHIFT];	/* delay shift register */
	l_fp filtoffset[NTP_SHIFT];	/* offset shift register */
	u_char order[NTP_SHIFT];	/* order of peers from last filter */
	s_fp delay;		/* peer.estdelay */
	u_fp dispersion;	/* peer.estdisp */
	l_fp offset;		/* peer.estoffset */
	u_fp selectdisp;	/* peer select dispersion */
	int32 unused1;		/* (obsolete) */
	int32 unused2;
	int32 unused3;
	int32 unused4;
	int32 unused5;
	int32 unused6;
	int32 unused7;
	s_fp estbdelay;		/* broadcast offset */
	u_int v6_flag;			/* is this v6 or not */
	u_int unused9;			/* (unused) padding for dstadr6 */
	struct in6_addr dstadr6; 	/* local address (v6-like) */
	struct in6_addr srcadr6; 	/* sources address (v6-like) */
};


/*
 * Peer statistics structure
 */
struct info_peer_stats {
	u_int32 dstadr;		/* local address */
	u_int32 srcadr;		/* remote address */
	u_short srcport;	/* remote port */
	u_short flags;		/* peer flags */
	u_int32 timereset;	/* time counters were reset */
	u_int32 timereceived;	/* time since a packet received */
	u_int32 timetosend;	/* time until a packet sent */
	u_int32 timereachable;	/* time peer has been reachable */
	u_int32 sent;		/* number sent */
	u_int32 unused1;	/* (unused) */
	u_int32 processed;	/* number processed */
	u_int32 unused2;	/* (unused) */
	u_int32 badauth;	/* bad authentication */
	u_int32 bogusorg;	/* bogus origin */
	u_int32 oldpkt;		/* duplicate */
	u_int32 unused3;	/* (unused) */
	u_int32 unused4;	/* (unused) */
	u_int32 seldisp;	/* bad dispersion */
	u_int32 selbroken;	/* bad reference time */
	u_int32 unused5;	/* (unused) */
	u_char candidate;	/* select order */
	u_char unused6;		/* (unused) */
	u_char unused7;		/* (unused) */
	u_char unused8;		/* (unused) */
	u_int v6_flag;			/* is this v6 or not */
	u_int unused9;			/* (unused) padding for dstadr6 */
	struct in6_addr dstadr6;	/* local address */
	struct in6_addr srcadr6;	/* remote address */
};


/*
 * Loop filter variables
 */
struct info_loop {
	l_fp last_offset;
	l_fp drift_comp;
	u_int32 compliance;
	u_int32 watchdog_timer;
};


/*
 * System info.  Mostly the sys.* variables, plus a few unique to
 * the implementation.
 */
struct info_sys {
	u_int32 peer;		/* system peer address (v4) */
	u_char peer_mode;	/* mode we are syncing to peer in */
	u_char leap;		/* system leap bits */
	u_char stratum;		/* our stratum */
	s_char precision;	/* local clock precision */
	s_fp rootdelay;		/* delay from sync source */
	u_fp rootdispersion;	/* dispersion from sync source */
	u_int32 refid;		/* reference ID of sync source */
	l_fp reftime;		/* system reference time */
	u_int32 poll;		/* system poll interval */
	u_char flags;		/* system flags */
	u_char unused1;		/* unused */
	u_char unused2;		/* unused */
	u_char unused3;		/* unused */
	s_fp bdelay;		/* default broadcast offset */
	s_fp frequency;		/* frequency residual (scaled ppm)  */
	l_fp authdelay;		/* default authentication delay */
	u_fp stability;		/* clock stability (scaled ppm) */
	u_int v6_flag;		/* is this v6 or not */
	u_int unused4;		/* unused, padding for peer6 */
	struct in6_addr peer6;	/* system peer address (v6) */
};


/*
 * System stats.  These are collected in the protocol module
 */
struct info_sys_stats {
	u_int32 timeup;		/* time since restart */
	u_int32 timereset;	/* time since reset */
	u_int32 denied;		/* access denied */
	u_int32 oldversionpkt;	/* recent version */
	u_int32 newversionpkt;	/* current version */
	u_int32 unknownversion;	/* bad version */
	u_int32 badlength;	/* bad length or format */
	u_int32 processed;	/* packets processed */
	u_int32 badauth;	/* bad authentication */
	u_int32 received;	/* packets received */
	u_int32 limitrejected;	/* rate exceeded */
	u_int32 lamport;	/* Lamport violations */
	u_int32 tsrounding;	/* Timestamp rounding errors */
};


/*
 * System stats - old version
 */
struct old_info_sys_stats {
	u_int32 timeup;		/* time since restart */
	u_int32 timereset;	/* time since reset */
	u_int32 denied;		/* access denied */
	u_int32 oldversionpkt;	/* recent version */
	u_int32 newversionpkt;	/* current version */
	u_int32 unknownversion;	/* bad version */
	u_int32 badlength;	/* bad length or format */
	u_int32 processed;	/* packets processed */
	u_int32 badauth;	/* bad authentication */
	u_int32 wanderhold;	/* (not used) */
};


/*
 * Peer memory statistics.  Collected in the peer module.
 */
struct info_mem_stats {
	u_int32 timereset;	/* time since reset */
	u_short totalpeermem;
	u_short freepeermem;
	u_int32 findpeer_calls;
	u_int32 allocations;
	u_int32 demobilizations;
	u_char hashcount[NTP_HASH_SIZE];
};


/*
 * I/O statistics.  Collected in the I/O module
 */
struct info_io_stats {
	u_int32 timereset;	/* time since reset */
	u_short totalrecvbufs;	/* total receive bufs */
	u_short freerecvbufs;	/* free buffers */
	u_short fullrecvbufs;	/* full buffers */
	u_short lowwater;	/* number of times we've added buffers */
	u_int32 dropped;	/* dropped packets */
	u_int32 ignored;	/* ignored packets */
	u_int32 received;	/* received packets */
	u_int32 sent;		/* packets sent */
	u_int32 notsent;	/* packets not sent */
	u_int32 interrupts;	/* interrupts we've handled */
	u_int32 int_received;	/* received by interrupt handler */
};


/*
 * Timer stats.  Guess where from.
 */
struct info_timer_stats {
	u_int32 timereset;	/* time since reset */
	u_int32 alarms;		/* alarms we've handled */
	u_int32 overflows;	/* timer overflows */
	u_int32 xmtcalls;	/* calls to xmit */
};


/*
 * Structure for passing peer configuration information
 */
struct old_conf_peer {
	u_int32 peeraddr;	/* address to poll */
	u_char hmode;		/* mode, either broadcast, active or client */
	u_char version;		/* version number to poll with */
	u_char minpoll;		/* min host poll interval */
	u_char maxpoll;		/* max host poll interval */
	u_char flags;		/* flags for this request */
	u_char ttl;		/* time to live (multicast) or refclock mode */
	u_short unused;		/* unused */
	keyid_t keyid;		/* key to use for this association */
};

struct conf_peer {
	u_int32 peeraddr;	/* address to poll */
	u_char hmode;		/* mode, either broadcast, active or client */
	u_char version;		/* version number to poll with */
	u_char minpoll;		/* min host poll interval */
	u_char maxpoll;		/* max host poll interval */
	u_char flags;		/* flags for this request */
	u_char ttl;		/* time to live (multicast) or refclock mode */
	u_short unused1;	/* unused */
	keyid_t keyid;		/* key to use for this association */
	char keystr[128];	/* public key file name */
	u_int v6_flag;		/* is this v6 or not */
	u_int unused2;			/* unused, padding for peeraddr6 */
	struct in6_addr peeraddr6;	/* ipv6 address to poll */
};

#define	CONF_FLAG_AUTHENABLE	0x01
#define CONF_FLAG_PREFER	0x02
#define CONF_FLAG_BURST		0x04
#define CONF_FLAG_IBURST	0x08
#define CONF_FLAG_NOSELECT	0x10
#define CONF_FLAG_SKEY		0x20

/*
 * Structure for passing peer deletion information.  Currently
 * we only pass the address and delete all configured peers with
 * this addess.
 */
struct conf_unpeer {
	u_int32 peeraddr;		/* address of peer */
	u_int v6_flag;			/* is this v6 or not */
	struct in6_addr peeraddr6;	/* address of peer (v6) */
};

/*
 * Structure for carrying system flags.
 */
struct conf_sys_flags {
	u_int32 flags;
};

/*
 * System flags we can set/clear
 */
#define	SYS_FLAG_BCLIENT	0x01
#define	SYS_FLAG_PPS		0x02
#define SYS_FLAG_NTP		0x04
#define SYS_FLAG_KERNEL		0x08
#define SYS_FLAG_MONITOR	0x10
#define SYS_FLAG_FILEGEN	0x20
#define SYS_FLAG_AUTH		0x40
#define SYS_FLAG_CAL		0x80

/*
 * Structure used for returning restrict entries
 */
struct info_restrict {
	u_int32 addr;		/* match address */
	u_int32 mask;		/* match mask */
	u_int32 count;		/* number of packets matched */
	u_short rflags;		/* restrict flags */
	u_short mflags;		/* match flags */
	u_int v6_flag;		/* is this v6 or not */
	u_int unused1;		/* unused, padding for addr6 */
	struct in6_addr addr6;	/* match address (v6) */
	struct in6_addr mask6; 	/* match mask (v6) */
};


/*
 * Structure used for specifying restrict entries
 */
struct conf_restrict {
	u_int32	addr;		/* match address */
	u_int32 mask;		/* match mask */
	short ippeerlimit;	/* ip peer limit */
	u_short flags;		/* restrict flags */
	u_short mflags;		/* match flags */
	u_int v6_flag;		/* is this v6 or not */
	struct in6_addr addr6; 	/* match address (v6) */
	struct in6_addr mask6; 	/* match mask (v6) */
};


/*
 * Structure used for returning monitor data
 */
struct info_monitor_1 {	
	u_int32 avg_int;	/* avg s between packets from this host */
	u_int32 last_int;	/* s since we last received a packet */
	u_int32 restr;		/* restrict bits (was named lastdrop) */
	u_int32 count;		/* count of packets received */
	u_int32 addr;		/* host address V4 style */
	u_int32 daddr;		/* destination host address */
	u_int32 flags;		/* flags about destination */
	u_short port;		/* port number of last reception */
	u_char mode;		/* mode of last packet */
	u_char version;		/* version number of last packet */
	u_int v6_flag;		/* is this v6 or not */
	u_int unused1;		/* unused, padding for addr6 */
	struct in6_addr addr6;	/* host address V6 style */
	struct in6_addr daddr6;	/* host address V6 style */
};


/*
 * Structure used for returning monitor data
 */
struct info_monitor {	
	u_int32 avg_int;	/* avg s between packets from this host */
	u_int32 last_int;	/* s since we last received a packet */
	u_int32 restr;		/* restrict bits (was named lastdrop) */
	u_int32 count;		/* count of packets received */
	u_int32 addr;		/* host address */
	u_short port;		/* port number of last reception */
	u_char mode;		/* mode of last packet */
	u_char version;		/* version number of last packet */
	u_int v6_flag;		/* is this v6 or not */
	u_int unused1;		/* unused, padding for addr6 */
	struct in6_addr addr6;	/* host v6 address */
};

/*
 * Structure used for returning monitor data (old format)
 */
struct old_info_monitor {	
	u_int32 lasttime;	/* last packet from this host */
	u_int32 firsttime;	/* first time we received a packet */
	u_int32 count;		/* count of packets received */
	u_int32 addr;		/* host address */
	u_short port;		/* port number of last reception */
	u_char mode;		/* mode of last packet */
	u_char version;		/* version number of last packet */
	u_int v6_flag;		/* is this v6 or not */
	struct in6_addr addr6;	/* host address  (v6)*/
};

/*
 * Structure used for passing indication of flags to clear
 */
struct reset_flags {
	u_int32 flags;
};

#define	RESET_FLAG_ALLPEERS	0x01
#define	RESET_FLAG_IO		0x02
#define	RESET_FLAG_SYS		0x04
#define	RESET_FLAG_MEM		0x08
#define	RESET_FLAG_TIMER	0x10
#define	RESET_FLAG_AUTH		0x20
#define	RESET_FLAG_CTL		0x40

#define	RESET_ALLFLAGS (	\
	RESET_FLAG_ALLPEERS |	\
	RESET_FLAG_IO |		\
	RESET_FLAG_SYS |	\
	RESET_FLAG_MEM |	\
	RESET_FLAG_TIMER |	\
	RESET_FLAG_AUTH |	\
	RESET_FLAG_CTL		\
)

/*
 * Structure used to return information concerning the authentication
 * module.
 */
struct info_auth {
	u_int32 timereset;	/* time counters were reset */
	u_int32 numkeys;	/* number of keys we know */
	u_int32 numfreekeys;	/* number of free keys */
	u_int32 keylookups;	/* calls to authhavekey() */
	u_int32 keynotfound;	/* requested key unknown */
	u_int32 encryptions;	/* number of encryptions */
	u_int32 decryptions;	/* number of decryptions */
	u_int32 expired;	/* number of expired keys */
	u_int32 keyuncached;	/* calls to encrypt/decrypt with uncached key */
};


/*
 * Structure used to pass trap information to the client
 */
struct info_trap {
	u_int32 local_address;	/* local interface addres (v4) */
	u_int32 trap_address;	/* remote client's addres (v4) */
	u_short trap_port;	/* remote port number */
	u_short sequence;	/* sequence number */
	u_int32 settime;	/* time trap last set */
	u_int32 origtime;	/* time trap originally set */
	u_int32 resets;		/* number of resets on this trap */
	u_int32 flags;		/* trap flags, as defined in ntp_control.h */
	u_int v6_flag;			/* is this v6 or not */
	struct in6_addr local_address6;	/* local interface address (v6) */
	struct in6_addr trap_address6;	/* remote client's address (v6) */
};

/*
 * Structure used to pass add/clear trap information to the client
 */
struct conf_trap {
	u_int32 local_address;	/* remote client's address */
	u_int32 trap_address;	/* local interface address */
	u_short trap_port;	/* remote client's port */
	u_short unused;		/* (unused) */
	u_int v6_flag;			/* is this v6 or not */
	struct in6_addr local_address6;	/* local interface address (v6) */
	struct in6_addr trap_address6;	/* remote client's address (v6) */
};


/*
 * Structure used to return statistics from the control module
 */
struct info_control {
	u_int32 ctltimereset;
	u_int32 numctlreq;	/* number of requests we've received */
	u_int32 numctlbadpkts;	/* number of bad control packets */
	u_int32 numctlresponses;	/* # resp packets sent */
	u_int32 numctlfrags;	/* # of fragments sent */
	u_int32 numctlerrors;	/* number of error responses sent */
	u_int32 numctltooshort;	/* number of too short input packets */
	u_int32 numctlinputresp;	/* number of responses on input */
	u_int32 numctlinputfrag;	/* number of fragments on input */
	u_int32 numctlinputerr;	/* # input pkts with err bit set */
	u_int32 numctlbadoffset;	/* # input pkts with nonzero offset */
	u_int32 numctlbadversion;	/* # input pkts with unknown version */
	u_int32 numctldatatooshort;	/* data too short for count */
	u_int32 numctlbadop;	/* bad op code found in packet */
	u_int32 numasyncmsgs;		/* # async messages we've sent */
};


/*
 * Structure used to return clock information
 */
struct info_clock {
	u_int32 clockadr;
	u_char type;
	u_char flags;
	u_char lastevent;
	u_char currentstatus;
	u_int32 polls;
	u_int32 noresponse;
	u_int32 badformat;
	u_int32 baddata;
	u_int32 timestarted;
	l_fp fudgetime1;
	l_fp fudgetime2;
	int32 fudgeval1;
	u_int32 fudgeval2;
};


/*
 * Structure used for setting clock fudge factors
 */
struct conf_fudge {
	u_int32 clockadr;
	u_int32 which;
	l_fp fudgetime;
	u_int32 fudgeval_flags;
};

#define	FUDGE_TIME1	1
#define	FUDGE_TIME2	2
#define	FUDGE_VAL1	3
#define	FUDGE_VAL2	4
#define	FUDGE_FLAGS	5


/*
 * Structure used for returning clock debugging info
 */
#define	NUMCBUGVALUES	16
#define	NUMCBUGTIMES	32

struct info_clkbug {
	u_int32 clockadr;
	u_char nvalues;
	u_char ntimes;
	u_short svalues;
	u_int32 stimes;
	u_int32 values[NUMCBUGVALUES];
	l_fp times[NUMCBUGTIMES];
};

/*
 * Structure used for returning kernel pll/PPS information
 */
struct info_kernel {
	int32 offset;
	int32 freq;
	int32 maxerror;
	int32 esterror;
	u_short status;
	u_short shift;
	int32 constant;
	int32 precision;
	int32 tolerance;

/*
 * Variables used only if PPS signal discipline is implemented
 */
	int32 ppsfreq;
	int32 jitter;
	int32 stabil;
	int32 jitcnt;
	int32 calcnt;
	int32 errcnt;
	int32 stbcnt;
};

/*
 * interface statistics
 */
struct info_if_stats {
	union addrun unaddr;		/* address */
	union addrun unbcast;		/* broadcast */
	union addrun unmask;		/* mask */
	u_int32 v6_flag;		/* is this v6 */
	char name[32];			/* name of interface */
	int32 flags;			/* interface flags */
	int32 last_ttl;			/* last TTL specified */
	int32 num_mcast;		/* No. of IP addresses in multicast socket */
	int32 received;			/* number of incoming packets */
	int32 sent;			/* number of outgoing packets */
	int32 notsent;			/* number of send failures */
	int32 uptime;			/* number of seconds this interface was active */
	u_int32 scopeid;		/* Scope used for Multicasting */
	u_int32 ifindex;		/* interface index - from system */
	u_int32 ifnum;			/* sequential interface number */
	u_int32 peercnt;		/* number of peers referencinf this interface - informational only */
	u_short family;			/* Address family */
	u_char ignore_packets;		/* Specify whether the packet should be ignored */
	u_char action;			/* reason the item is listed */
	int32 _filler0;			/* pad to a 64 bit size boundary */
};

#define IFS_EXISTS	1	/* just exists */
#define IFS_CREATED	2	/* was just created */
#define IFS_DELETED	3	/* was just delete */

/*
 * Info returned with IP -> hostname lookup
 */
/* 144 might need to become 32, matching data[] member of req_pkt */
#define NTP_MAXHOSTNAME (32 - sizeof(u_int32) - sizeof(u_short))
struct info_dns_assoc {
	u_int32 peeraddr;	/* peer address (HMS: being careful...) */
	associd_t associd;	/* association ID */
	char hostname[NTP_MAXHOSTNAME];	/* hostname */
};

/*
 * function declarations
 */
int get_packet_mode(struct recvbuf *rbufp); /* Return packet mode */

#endif /* NTP_REQUEST_H */
