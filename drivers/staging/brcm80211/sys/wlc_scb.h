/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _wlc_scb_h_
#define _wlc_scb_h_

#include <proto/802.1d.h>

extern bool wlc_aggregatable(wlc_info_t *wlc, uint8 tid);

#define AMPDU_TX_BA_MAX_WSIZE	64	/* max Tx ba window size (in pdu) */
/* structure to store per-tid state for the ampdu initiator */
typedef struct scb_ampdu_tid_ini {
	uint32 magic;
	uint8 tx_in_transit;	/* number of pending mpdus in transit in driver */
	uint8 tid;		/* initiator tid for easy lookup */
	uint8 txretry[AMPDU_TX_BA_MAX_WSIZE];	/* tx retry count; indexed by seq modulo */
	struct scb *scb;	/* backptr for easy lookup */
} scb_ampdu_tid_ini_t;

#define AMPDU_MAX_SCB_TID	NUMPRIO

typedef struct scb_ampdu {
	struct scb *scb;	/* back pointer for easy reference */
	uint8 mpdu_density;	/* mpdu density */
	uint8 max_pdu;		/* max pdus allowed in ampdu */
	uint8 release;		/* # of mpdus released at a time */
	uint16 min_len;		/* min mpdu len to support the density */
	uint32 max_rxlen;	/* max ampdu rcv length; 8k, 16k, 32k, 64k */
	struct pktq txq;	/* sdu transmit queue pending aggregation */

	/* This could easily be a ini[] pointer and we keep this info in wl itself instead
	 * of having mac80211 hold it for us.  Also could be made dynamic per tid instead of
	 * static.
	 */
	scb_ampdu_tid_ini_t ini[AMPDU_MAX_SCB_TID];	/* initiator info - per tid (NUMPRIO) */
} scb_ampdu_t;

#define SCB_MAGIC 	0xbeefcafe
#define INI_MAGIC 	0xabcd1234

/* station control block - one per remote MAC address */
struct scb {
	uint32 magic;
	uint32 flags;		/* various bit flags as defined below */
	uint32 flags2;		/* various bit flags2 as defined below */
	uint8 state;		/* current state bitfield of auth/assoc process */
	struct ether_addr ea;	/* station address */
	void *fragbuf[NUMPRIO];	/* defragmentation buffer per prio */
	uint fragresid[NUMPRIO];	/* #bytes unused in frag buffer per prio */

	uint16 seqctl[NUMPRIO];	/* seqctl of last received frame (for dups) */
	uint16 seqctl_nonqos;	/* seqctl of last received frame (for dups) for
				 * non-QoS data and management
				 */
	uint16 seqnum[NUMPRIO];	/* WME: driver maintained sw seqnum per priority */

	scb_ampdu_t scb_ampdu;	/* AMPDU state including per tid info */
};

/* SCB flags */
#define SCB_NONERP		0x0001	/* No ERP */
#define SCB_LONGSLOT		0x0002	/* Long Slot */
#define SCB_SHORTPREAMBLE	0x0004	/* Short Preamble ok */
#define SCB_8021XHDR		0x0008	/* 802.1x Header */
#define SCB_WPA_SUP		0x0010	/* 0 - authenticator, 1 - supplicant */
#define SCB_DEAUTH		0x0020	/* 0 - ok to deauth, 1 - no (just did) */
#define SCB_WMECAP		0x0040	/* WME Cap; may ONLY be set if WME_ENAB(wlc) */
#define SCB_BRCM		0x0100	/* BRCM AP or STA */
#define SCB_WDS_LINKUP		0x0200	/* WDS link up */
#define SCB_RESERVED1		0x0400
#define SCB_RESERVED2		0x0800
#define SCB_MYAP		0x1000	/* We are associated to this AP */
#define SCB_PENDING_PROBE	0x2000	/* Probe is pending to this SCB */
#define SCB_AMSDUCAP		0x4000	/* A-MSDU capable */
#define SCB_BACAP		0x8000	/* pre-n blockack capable */
#define SCB_HTCAP		0x10000	/* HT (MIMO) capable device */
#define SCB_RECV_PM		0x20000	/* state of PM bit in last data frame recv'd */
#define SCB_AMPDUCAP		0x40000	/* A-MPDU capable */
#define SCB_IS40		0x80000	/* 40MHz capable */
#define SCB_NONGF		0x100000	/* Not Green Field capable */
#define SCB_APSDCAP		0x200000	/* APSD capable */
#define SCB_PENDING_FREE	0x400000	/* marked for deletion - clip recursion */
#define SCB_PENDING_PSPOLL	0x800000	/* PS-Poll is pending to this SCB */
#define SCB_RIFSCAP		0x1000000	/* RIFS capable */
#define SCB_HT40INTOLERANT	0x2000000	/* 40 Intolerant */
#define SCB_WMEPS		0x4000000	/* PS + WME w/o APSD capable */
#define SCB_SENT_APSD_TRIG	0x8000000	/* APSD Trigger Null Frame was recently sent */
#define SCB_COEX_MGMT		0x10000000	/* Coexistence Management supported */
#define SCB_IBSS_PEER		0x20000000	/* Station is an IBSS peer */
#define SCB_STBCCAP		0x40000000	/* STBC Capable */

/* scb flags2 */
#define SCB2_SGI20_CAP		0x00000001	/* 20MHz SGI Capable */
#define SCB2_SGI40_CAP		0x00000002	/* 40MHz SGI Capable */
#define SCB2_RX_LARGE_AGG	0x00000004	/* device can rx large aggs */
#define SCB2_INTERNAL		0x00000008	/* This scb is an internal scb */
#define SCB2_IN_ASSOC		0x00000010	/* Incoming assocation in progress */
#define SCB2_RESERVED1		0x00000040
#define SCB2_LDPCCAP		0x00000080	/* LDPC Cap */

/* scb association state bitfield */
#define UNAUTHENTICATED		0	/* unknown */
#define AUTHENTICATED		1	/* 802.11 authenticated (open or shared key) */
#define ASSOCIATED		2	/* 802.11 associated */
#define PENDING_AUTH		4	/* Waiting for 802.11 authentication response */
#define PENDING_ASSOC		8	/* Waiting for 802.11 association response */
#define AUTHORIZED		0x10	/* 802.1X authorized */
#define TAKEN4IBSS		0x80	/* Taken */

/* scb association state helpers */
#define SCB_ASSOCIATED(a)	((a)->state & ASSOCIATED)
#define SCB_AUTHENTICATED(a)	((a)->state & AUTHENTICATED)
#define SCB_AUTHORIZED(a)	((a)->state & AUTHORIZED)

/* flag access */
#define SCB_ISMYAP(a)           ((a)->flags & SCB_MYAP)
#define SCB_ISPERMANENT(a)      ((a)->permanent)
#define	SCB_INTERNAL(a) 	((a)->flags2 & SCB2_INTERNAL)
/* scb association state helpers w/ respect to ssid (in case of multi ssids)
 * The bit set in the bit field is relative to the current state (i.e. if
 * the current state is "associated", a 1 at the position "i" means the
 * sta is associated to ssid "i"
 */
#define SCB_ASSOCIATED_BSSCFG(a, i)	\
	(((a)->state & ASSOCIATED) && isset(&(scb->auth_bsscfg), i))

#define SCB_AUTHENTICATED_BSSCFG(a, i)	\
	(((a)->state & AUTHENTICATED) && isset(&(scb->auth_bsscfg), i))

#define SCB_AUTHORIZED_BSSCFG(a, i)	\
	(((a)->state & AUTHORIZED) && isset(&(scb->auth_bsscfg), i))

#define SCB_LONG_TIMEOUT	3600	/* # seconds of idle time after which we proactively
					 * free an authenticated SCB
					 */
#define SCB_SHORT_TIMEOUT	  60	/* # seconds of idle time after which we will reclaim an
					 * authenticated SCB if we would otherwise fail
					 * an SCB allocation.
					 */
#define SCB_TIMEOUT		  60	/* # seconds: interval to probe idle STAs */
#define SCB_ACTIVITY_TIME	   5	/* # seconds: skip probe if activity during this time */
#define SCB_GRACE_ATTEMPTS	   3	/* # attempts to probe sta beyond scb_activity_time */

/* scb_info macros */
#define SCB_PS(a)		NULL
#define SCB_WDS(a)		NULL
#define SCB_INTERFACE(a)        ((a)->bsscfg->wlcif->wlif)
#define SCB_WLCIFP(a)           (((a)->bsscfg->wlcif))
#define WLC_BCMC_PSMODE(wlc, bsscfg) (TRUE)

#define SCB_WME(a)		((a)->flags & SCB_WMECAP)	/* Also implies WME_ENAB(wlc) */

#define SCB_AMPDU(a)		TRUE
#define SCB_AMSDU(a)		FALSE

#define SCB_HT_CAP(a)		((a)->flags & SCB_HTCAP)
#define SCB_ISGF_CAP(a)		(((a)->flags & (SCB_HTCAP | SCB_NONGF)) == SCB_HTCAP)
#define SCB_NONGF_CAP(a)	(((a)->flags & (SCB_HTCAP | SCB_NONGF)) == \
					(SCB_HTCAP | SCB_NONGF))
#define SCB_COEX_CAP(a)		((a)->flags & SCB_COEX_MGMT)
#define SCB_STBC_CAP(a)		((a)->flags & SCB_STBCCAP)
#define SCB_LDPC_CAP(a)		(SCB_HT_CAP(a) && ((a)->flags2 & SCB2_LDPCCAP))

#define SCB_IS_IBSS_PEER(a)	((a)->flags & SCB_IBSS_PEER)
#define SCB_SET_IBSS_PEER(a)	((a)->flags |= SCB_IBSS_PEER)
#define SCB_UNSET_IBSS_PEER(a)	((a)->flags &= ~SCB_IBSS_PEER)

#define SCB_11E(a)		FALSE

#define SCB_QOS(a)		((a)->flags & (SCB_WMECAP | SCB_HTCAP))

#define SCB_BSSCFG(a)           ((a)->bsscfg)

#define SCB_SEQNUM(scb, prio)	((scb)->seqnum[(prio)])

#define SCB_ISMULTI(a)	ETHER_ISMULTI((a)->ea.octet)
#define SCB_ISVALID(a, _pkttag_dbgid)	((a) && (a)->_dbgid == (_pkttag_dbgid))

/* API for accessing SCB pointer in WLPKTTAG */
#ifdef BCMDBG
#define WLPKTTAGSCBSET(p, scb) { WLPKTTAG(p)->_scb = scb; WLPKTTAG(p)->_scb_dbgid = scb->_dbgid; }
#define WLPKTTAGSCBCLR(p) { WLPKTTAG(p)->_scb = NULL; WLPKTTAG(p)->_scb_dbgid = 0; }
#else
#define WLPKTTAGSCBSET(p, scb) (WLPKTTAG(p)->_scb = scb)
#define WLPKTTAGSCBCLR(p) (WLPKTTAG(p)->_scb = NULL)
#endif

#define WLCNTSCBINCR(a)		/* No stats support */
#define WLCNTSCBDECR(a)		/* No stats support */
#define WLCNTSCBADD(a, delta)	/* No stats support */
#define WLCNTSCBSET(a, value)	/* No stats support */
#define WLCNTSCBVAL(a)		0	/* No stats support */
#define WLCNTSCB_COND_SET(c, a, v)	/* No stats support */
#define WLCNTSCB_COND_ADD(c, a, d)	/* No stats support */
#define WLCNTSCB_COND_INCR(c, a)	/* No stats support */

#endif				/* _wlc_scb_h_ */
