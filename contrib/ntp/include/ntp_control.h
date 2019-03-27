/*
 * ntp_control.h - definitions related to NTP mode 6 control messages
 */

#include "ntp_types.h"

typedef union ctl_pkt_u_tag {
	u_char data[480 + MAX_MAC_LEN]; /* data + auth */
	u_int32 u32[(480 + MAX_MAC_LEN) / sizeof(u_int32)];
} ctl_pkt_u;

struct ntp_control {
	u_char li_vn_mode;		/* leap, version, mode */
	u_char r_m_e_op;		/* response, more, error, opcode */
	u_short sequence;		/* sequence number of request */
	u_short status;			/* status word for association */
	associd_t associd;		/* association ID */
	u_short offset;			/* offset of this batch of data */
	u_short count;			/* count of data in this packet */
	ctl_pkt_u u;
};

/*
 * Length of the control header, in octets
 */
#define	CTL_HEADER_LEN		(offsetof(struct ntp_control, u))
#define	CTL_MAX_DATA_LEN	468


/*
 * Limits and things
 */
#define	CTL_MAXTRAPS	3		/* maximum number of traps we allow */
#define	CTL_TRAPTIME	(60*60)		/* time out traps in 1 hour */
#define	CTL_MAXAUTHSIZE	64		/* maximum size of an authen'ed req */

/*
 * Decoding for the r_m_e_op field
 */
#define	CTL_RESPONSE	0x80
#define	CTL_ERROR	0x40
#define	CTL_MORE	0x20
#define	CTL_OP_MASK	0x1f

#define	CTL_ISRESPONSE(r_m_e_op) ((CTL_RESPONSE	& (r_m_e_op)) != 0)
#define	CTL_ISMORE(r_m_e_op)	 ((CTL_MORE	& (r_m_e_op)) != 0)
#define	CTL_ISERROR(r_m_e_op)	 ((CTL_ERROR	& (r_m_e_op)) != 0)
#define	CTL_OP(r_m_e_op)	 (CTL_OP_MASK	& (r_m_e_op))

/*
 * Opcodes
 */
#define	CTL_OP_UNSPEC		0	/* unspeciffied */
#define	CTL_OP_READSTAT		1	/* read status */
#define	CTL_OP_READVAR		2	/* read variables */
#define	CTL_OP_WRITEVAR		3	/* write variables */
#define	CTL_OP_READCLOCK	4	/* read clock variables */
#define	CTL_OP_WRITECLOCK	5	/* write clock variables */
#define	CTL_OP_SETTRAP		6	/* set trap address */
#define	CTL_OP_ASYNCMSG		7	/* asynchronous message */
#define CTL_OP_CONFIGURE	8	/* runtime configuration */
#define CTL_OP_SAVECONFIG	9	/* save config to file */
#define CTL_OP_READ_MRU		10	/* retrieve MRU (mrulist) */
#define CTL_OP_READ_ORDLIST_A	11	/* ordered list req. auth. */
#define CTL_OP_REQ_NONCE	12	/* request a client nonce */
#define	CTL_OP_UNSETTRAP	31	/* unset trap */

/*
 * {En,De}coding of the system status word
 */
#define	CTL_SST_TS_UNSPEC	0	/* unspec */
#define	CTL_SST_TS_ATOM		1	/* pps */
#define	CTL_SST_TS_LF		2	/* lf radio */
#define	CTL_SST_TS_HF		3	/* hf radio */
#define	CTL_SST_TS_UHF		4	/* uhf radio */
#define	CTL_SST_TS_LOCAL	5	/* local */
#define	CTL_SST_TS_NTP		6	/* ntp */
#define	CTL_SST_TS_UDPTIME	7	/* other */
#define	CTL_SST_TS_WRSTWTCH	8	/* wristwatch */
#define	CTL_SST_TS_TELEPHONE	9	/* telephone */

#define	CTL_SYS_MAXEVENTS	15

#define	CTL_SYS_STATUS(li, source, nevnt, evnt) \
		(((((unsigned short)(li))<< 14)&0xc000) | \
		(((source)<<8)&0x3f00) | \
		(((nevnt)<<4)&0x00f0) | \
		((evnt)&0x000f))

#define	CTL_SYS_LI(status)	(((status)>>14) & 0x3)
#define	CTL_SYS_SOURCE(status)	(((status)>>8) & 0x3f)
#define	CTL_SYS_NEVNT(status)	(((status)>>4) & 0xf)
#define	CTL_SYS_EVENT(status)	((status) & 0xf)

/*
 * {En,De}coding of the peer status word
 */
#define	CTL_PST_CONFIG		0x80
#define	CTL_PST_AUTHENABLE	0x40
#define	CTL_PST_AUTHENTIC	0x20
#define	CTL_PST_REACH		0x10
#define	CTL_PST_BCAST		0x08

#define	CTL_PST_SEL_REJECT	0	/*   reject */
#define	CTL_PST_SEL_SANE	1	/* x falsetick */
#define	CTL_PST_SEL_CORRECT	2	/* . excess */
#define	CTL_PST_SEL_SELCAND	3	/* - outlier */
#define	CTL_PST_SEL_SYNCCAND	4	/* + candidate */
#define	CTL_PST_SEL_EXCESS	5	/* # backup */
#define	CTL_PST_SEL_SYSPEER	6	/* * sys.peer */
#define	CTL_PST_SEL_PPS		7	/* o pps.peer */

#define	CTL_PEER_MAXEVENTS	15

#define	CTL_PEER_STATUS(status, nevnt, evnt) \
		((((status)<<8) & 0xff00) | \
		(((nevnt)<<4) & 0x00f0) | \
		((evnt) & 0x000f))

#define	CTL_PEER_STATVAL(status)(((status)>>8) & 0xff)
#define	CTL_PEER_NEVNT(status)	(((status)>>4) & 0xf)
#define	CTL_PEER_EVENT(status)	((status) & 0xf)

/*
 * {En,De}coding of the clock status word
 */
#define	CTL_CLK_OKAY		0
#define	CTL_CLK_NOREPLY		1
#define	CTL_CLK_BADFORMAT	2
#define	CTL_CLK_FAULT		3
#define	CTL_CLK_PROPAGATION	4
#define	CTL_CLK_BADDATE		5
#define	CTL_CLK_BADTIME		6

#define	CTL_CLK_STATUS(status, event) \
		((((status)<<8) & 0xff00) | \
		((event) & 0x00ff))

/*
 * Error code responses returned when the E bit is set.
 */
#define	CERR_UNSPEC	0
#define	CERR_PERMISSION	1
#define	CERR_BADFMT	2
#define	CERR_BADOP	3
#define	CERR_BADASSOC	4
#define	CERR_UNKNOWNVAR	5
#define	CERR_BADVALUE	6
#define	CERR_RESTRICT	7

#define	CERR_NORESOURCE	CERR_PERMISSION	/* wish there was a different code */


/*
 * Definition of the structure used internally to hold trap information.
 * ntp_request.c wants to see this.
 */
struct ctl_trap {
	sockaddr_u tr_addr;		/* address of trap recipient */
	struct interface *tr_localaddr;	/* interface to send this through */
	u_long tr_settime;		/* time trap was set */
	u_long tr_count;		/* async messages sent to this guy */
	u_long tr_origtime;		/* time trap was originally set */
	u_long tr_resets;		/* count of resets for this trap */
	u_short tr_sequence;		/* trap sequence id */
	u_char tr_flags;		/* trap flags */
	u_char tr_version;		/* version number of trapper */
};
extern struct ctl_trap ctl_traps[CTL_MAXTRAPS];

/*
 * Flag bits
 */
#define	TRAP_INUSE	0x1		/* this trap is active */
#define	TRAP_NONPRIO	0x2		/* this trap is non-priority */
#define	TRAP_CONFIGURED	0x4		/* this trap was configured */

/*
 * Types of things we may deal with
 * shared between ntpq and library
 */
#define	TYPE_SYS	1
#define	TYPE_PEER	2
#define	TYPE_CLOCK	3

/*
 * IFSTATS_FIELDS is the number of fields ntpd supplies for each ifstats
 * row.  Similarly RESLIST_FIELDS for reslist.
 */
#define	IFSTATS_FIELDS	12
#define	RESLIST_FIELDS	4

