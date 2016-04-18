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

#ifndef _BRCM_SCB_H_
#define _BRCM_SCB_H_

#include <linux/if_ether.h>
#include <brcmu_utils.h>
#include <defs.h>
#include "types.h"

#define AMPDU_TX_BA_MAX_WSIZE	64	/* max Tx ba window size (in pdu) */

#define AMPDU_MAX_SCB_TID	NUMPRIO

/* scb flags */
#define SCB_WMECAP		0x0040
#define SCB_HTCAP		0x10000	/* HT (MIMO) capable device */
#define SCB_IS40		0x80000	/* 40MHz capable */
#define SCB_STBCCAP		0x40000000	/* STBC Capable */

#define SCB_MAGIC	0xbeefcafe

/* structure to store per-tid state for the ampdu initiator */
struct scb_ampdu_tid_ini {
	u8 tid;		  /* initiator tid for easy lookup */
	/* tx retry count; indexed by seq modulo */
	u8 txretry[AMPDU_TX_BA_MAX_WSIZE];
	struct scb *scb;  /* backptr for easy lookup */
	u8 ba_wsize;	  /* negotiated ba window size (in pdu) */
};

struct scb_ampdu {
	struct scb *scb;	/* back pointer for easy reference */
	u8 mpdu_density;	/* mpdu density */
	u8 max_pdu;		/* max pdus allowed in ampdu */
	u8 release;		/* # of mpdus released at a time */
	u16 min_len;		/* min mpdu len to support the density */
	u32 max_rx_ampdu_bytes;	/* max ampdu rcv length; 8k, 16k, 32k, 64k */

	/*
	 * This could easily be a ini[] pointer and we keep this info in wl
	 * itself instead of having mac80211 hold it for us. Also could be made
	 * dynamic per tid instead of static.
	 */
	/* initiator info - per tid (NUMPRIO): */
	struct scb_ampdu_tid_ini ini[AMPDU_MAX_SCB_TID];
};

/* station control block - one per remote MAC address */
struct scb {
	u32 magic;
	u32 flags;	/* various bit flags as defined below */
	u32 flags2;	/* various bit flags2 as defined below */
	u8 state;	/* current state bitfield of auth/assoc process */
	u8 ea[ETH_ALEN];	/* station address */
	uint fragresid[NUMPRIO];/* #bytes unused in frag buffer per prio */

	u16 seqctl[NUMPRIO];	/* seqctl of last received frame (for dups) */
	/* seqctl of last received frame (for dups) for non-QoS data and
	 * management */
	u16 seqctl_nonqos;
	u16 seqnum[NUMPRIO];/* WME: driver maintained sw seqnum per priority */

	struct scb_ampdu scb_ampdu;	/* AMPDU state including per tid info */
};

#endif				/* _BRCM_SCB_H_ */
