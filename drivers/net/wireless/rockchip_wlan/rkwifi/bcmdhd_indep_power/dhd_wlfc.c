/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DHD PROP_TXSTATUS Module.
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dhd_wlfc.c 679733 2017-01-17 06:40:39Z $
 *
 */


#include <typedefs.h>
#include <osl.h>

#include <bcmutils.h>
#include <bcmendian.h>

#include <dngl_stats.h>
#include <dhd.h>

#include <dhd_bus.h>

#include <dhd_dbg.h>
#include <dhd_config.h>
#include <wl_android.h>

#ifdef PROP_TXSTATUS /* a form of flow control between host and dongle */
#include <wlfc_proto.h>
#include <dhd_wlfc.h>
#endif

#ifdef DHDTCPACK_SUPPRESS
#include <dhd_ip.h>
#endif /* DHDTCPACK_SUPPRESS */


/*
 * wlfc naming and lock rules:
 *
 * 1. Private functions name like _dhd_wlfc_XXX, declared as static and avoid wlfc lock operation.
 * 2. Public functions name like dhd_wlfc_XXX, use wlfc lock if needed.
 * 3. Non-Proptxstatus module call public functions only and avoid wlfc lock operation.
 *
 */

#if defined(DHD_WLFC_THREAD)
#define WLFC_THREAD_QUICK_RETRY_WAIT_MS    10      /* 10 msec */
#define WLFC_THREAD_RETRY_WAIT_MS          10000   /* 10 sec */
#endif /* defined (DHD_WLFC_THREAD) */


#ifdef PROP_TXSTATUS

#define DHD_WLFC_QMON_COMPLETE(entry)


/** reordering related */

#if defined(DHD_WLFC_THREAD)
static void
_dhd_wlfc_thread_wakeup(dhd_pub_t *dhdp)
{
	dhdp->wlfc_thread_go = TRUE;
	wake_up_interruptible(&dhdp->wlfc_wqhead);
}
#endif /* DHD_WLFC_THREAD */

static uint16
_dhd_wlfc_adjusted_seq(void* p, uint8 current_seq)
{
	uint16 seq;

	if (!p) {
		return 0xffff;
	}

	seq = WL_TXSTATUS_GET_FREERUNCTR(DHD_PKTTAG_H2DTAG(PKTTAG(p)));
	if (seq < current_seq) {
		/* wrap around */
		seq += 256;
	}

	return seq;
}

/**
 * Enqueue a caller supplied packet on a caller supplied precedence queue, optionally reorder
 * suppressed packets.
 *    @param[in] pq       caller supplied packet queue to enqueue the packet on
 *    @param[in] prec     precedence of the to-be-queued packet
 *    @param[in] p        transmit packet to enqueue
 *    @param[in] qHead    if TRUE, enqueue to head instead of tail. Used to maintain d11 seq order.
 *    @param[in] current_seq
 *    @param[in] reOrder  reOrder on odd precedence (=suppress queue)
 */
static void
_dhd_wlfc_prec_enque(struct pktq *pq, int prec, void* p, bool qHead,
	uint8 current_seq, bool reOrder)
{
	struct pktq_prec *q;
	uint16 seq, seq2;
	void *p2, *p2_prev;

	if (!p)
		return;

	ASSERT(prec >= 0 && prec < pq->num_prec);
	ASSERT(PKTLINK(p) == NULL);		/* queueing chains not allowed */

	ASSERT(!pktq_full(pq));
	ASSERT(!pktq_pfull(pq, prec));

	q = &pq->q[prec];

	PKTSETLINK(p, NULL);
	if (q->head == NULL) {
		/* empty queue */
		q->head = p;
		q->tail = p;
	} else {
		if (reOrder && (prec & 1)) {
			seq = _dhd_wlfc_adjusted_seq(p, current_seq);
			p2 = qHead ? q->head : q->tail;
			seq2 = _dhd_wlfc_adjusted_seq(p2, current_seq);

			if ((qHead &&((seq+1) > seq2)) || (!qHead && ((seq2+1) > seq))) {
				/* need reorder */
				p2 = q->head;
				p2_prev = NULL;
				seq2 = _dhd_wlfc_adjusted_seq(p2, current_seq);

				while (seq > seq2) {
					p2_prev = p2;
					p2 = PKTLINK(p2);
					if (!p2) {
						break;
					}
					seq2 = _dhd_wlfc_adjusted_seq(p2, current_seq);
				}

				if (p2_prev == NULL) {
					/* insert head */
					PKTSETLINK(p, q->head);
					q->head = p;
				} else if (p2 == NULL) {
					/* insert tail */
					PKTSETLINK(p2_prev, p);
					q->tail = p;
				} else {
					/* insert after p2_prev */
					PKTSETLINK(p, PKTLINK(p2_prev));
					PKTSETLINK(p2_prev, p);
				}
				goto exit;
			}
		}

		if (qHead) {
			PKTSETLINK(p, q->head);
			q->head = p;
		} else {
			PKTSETLINK(q->tail, p);
			q->tail = p;
		}
	}

exit:

	q->len++;
	pq->len++;

	if (pq->hi_prec < prec)
		pq->hi_prec = (uint8)prec;
} /* _dhd_wlfc_prec_enque */

/**
 * Create a place to store all packet pointers submitted to the firmware until a status comes back,
 * suppress or otherwise.
 *
 * hang-er: noun, a contrivance on which things are hung, as a hook.
 */
/** @deprecated soon */
static void*
_dhd_wlfc_hanger_create(dhd_pub_t *dhd, int max_items)
{
	int i;
	wlfc_hanger_t* hanger;

	/* allow only up to a specific size for now */
	ASSERT(max_items == WLFC_HANGER_MAXITEMS);

	if ((hanger = (wlfc_hanger_t*)DHD_OS_PREALLOC(dhd, DHD_PREALLOC_DHD_WLFC_HANGER,
		WLFC_HANGER_SIZE(max_items))) == NULL) {
		return NULL;
	}
	memset(hanger, 0, WLFC_HANGER_SIZE(max_items));
	hanger->max_items = max_items;

	for (i = 0; i < hanger->max_items; i++) {
		hanger->items[i].state = WLFC_HANGER_ITEM_STATE_FREE;
	}
	return hanger;
}

/** @deprecated soon */
static int
_dhd_wlfc_hanger_delete(dhd_pub_t *dhd, void* hanger)
{
	wlfc_hanger_t* h = (wlfc_hanger_t*)hanger;

	if (h) {
		DHD_OS_PREFREE(dhd, h, WLFC_HANGER_SIZE(h->max_items));
		return BCME_OK;
	}
	return BCME_BADARG;
}

/** @deprecated soon */
static uint16
_dhd_wlfc_hanger_get_free_slot(void* hanger)
{
	uint32 i;
	wlfc_hanger_t* h = (wlfc_hanger_t*)hanger;

	if (h) {
		i = h->slot_pos + 1;
		if (i == h->max_items) {
			i = 0;
		}
		while (i != h->slot_pos) {
			if (h->items[i].state == WLFC_HANGER_ITEM_STATE_FREE) {
				h->slot_pos = i;
				return (uint16)i;
			}
			i++;
			if (i == h->max_items)
				i = 0;
		}
		h->failed_slotfind++;
	}
	return WLFC_HANGER_MAXITEMS;
}

/** @deprecated soon */
static int
_dhd_wlfc_hanger_get_genbit(void* hanger, void* pkt, uint32 slot_id, int* gen)
{
	int rc = BCME_OK;
	wlfc_hanger_t* h = (wlfc_hanger_t*)hanger;

	*gen = 0xff;

	/* this packet was not pushed at the time it went to the firmware */
	if (slot_id == WLFC_HANGER_MAXITEMS)
		return BCME_NOTFOUND;

	if (h) {
		if (h->items[slot_id].state != WLFC_HANGER_ITEM_STATE_FREE) {
			*gen = h->items[slot_id].gen;
		}
		else {
			DHD_ERROR(("Error: %s():%d item not used\n",
				__FUNCTION__, __LINE__));
			rc = BCME_NOTFOUND;
		}

	} else {
		rc = BCME_BADARG;
	}

	return rc;
}

/** @deprecated soon */
static int
_dhd_wlfc_hanger_pushpkt(void* hanger, void* pkt, uint32 slot_id)
{
	int rc = BCME_OK;
	wlfc_hanger_t* h = (wlfc_hanger_t*)hanger;

	if (h && (slot_id < WLFC_HANGER_MAXITEMS)) {
		if (h->items[slot_id].state == WLFC_HANGER_ITEM_STATE_FREE) {
			h->items[slot_id].state = WLFC_HANGER_ITEM_STATE_INUSE;
			h->items[slot_id].pkt = pkt;
			h->items[slot_id].pkt_state = 0;
			h->items[slot_id].pkt_txstatus = 0;
			h->pushed++;
		} else {
			h->failed_to_push++;
			rc = BCME_NOTFOUND;
		}
	} else {
		rc = BCME_BADARG;
	}

	return rc;
}

/** @deprecated soon */
static int
_dhd_wlfc_hanger_poppkt(void* hanger, uint32 slot_id, void** pktout, bool remove_from_hanger)
{
	int rc = BCME_OK;
	wlfc_hanger_t* h = (wlfc_hanger_t*)hanger;

	*pktout = NULL;

	/* this packet was not pushed at the time it went to the firmware */
	if (slot_id == WLFC_HANGER_MAXITEMS)
		return BCME_NOTFOUND;

	if (h) {
		if (h->items[slot_id].state != WLFC_HANGER_ITEM_STATE_FREE) {
			*pktout = h->items[slot_id].pkt;
			if (remove_from_hanger) {
				h->items[slot_id].state =
					WLFC_HANGER_ITEM_STATE_FREE;
				h->items[slot_id].pkt = NULL;
				h->items[slot_id].gen = 0xff;
				h->items[slot_id].identifier = 0;
				h->popped++;
			}
		} else {
			h->failed_to_pop++;
			rc = BCME_NOTFOUND;
		}
	} else {
		rc = BCME_BADARG;
	}

	return rc;
}

/** @deprecated soon */
static int
_dhd_wlfc_hanger_mark_suppressed(void* hanger, uint32 slot_id, uint8 gen)
{
	int rc = BCME_OK;
	wlfc_hanger_t* h = (wlfc_hanger_t*)hanger;

	/* this packet was not pushed at the time it went to the firmware */
	if (slot_id == WLFC_HANGER_MAXITEMS)
		return BCME_NOTFOUND;
	if (h) {
		h->items[slot_id].gen = gen;
		if (h->items[slot_id].state == WLFC_HANGER_ITEM_STATE_INUSE) {
			h->items[slot_id].state = WLFC_HANGER_ITEM_STATE_INUSE_SUPPRESSED;
		} else {
			rc = BCME_BADARG;
		}
	} else {
		rc = BCME_BADARG;
	}

	return rc;
}

/** remove reference of specific packet in hanger */
/** @deprecated soon */
static bool
_dhd_wlfc_hanger_remove_reference(wlfc_hanger_t* h, void* pkt)
{
	int i;

	if (!h || !pkt) {
		return FALSE;
	}

	i = WL_TXSTATUS_GET_HSLOT(DHD_PKTTAG_H2DTAG(PKTTAG(pkt)));

	if ((i < h->max_items) && (pkt == h->items[i].pkt)) {
		if (h->items[i].state == WLFC_HANGER_ITEM_STATE_INUSE_SUPPRESSED) {
			h->items[i].state = WLFC_HANGER_ITEM_STATE_FREE;
			h->items[i].pkt = NULL;
			h->items[i].gen = 0xff;
			h->items[i].identifier = 0;
			return TRUE;
		} else {
			DHD_ERROR(("Error: %s():%d item not suppressed\n",
				__FUNCTION__, __LINE__));
		}
	}

	return FALSE;
}

/** afq = At Firmware Queue, queue containing packets pending in the dongle */
static int
_dhd_wlfc_enque_afq(athost_wl_status_info_t* ctx, void *p)
{
	wlfc_mac_descriptor_t* entry;
	uint16 entry_idx = WL_TXSTATUS_GET_HSLOT(DHD_PKTTAG_H2DTAG(PKTTAG(p)));
	uint8 prec = DHD_PKTTAG_FIFO(PKTTAG(p));

	if (entry_idx < WLFC_MAC_DESC_TABLE_SIZE)
		entry  = &ctx->destination_entries.nodes[entry_idx];
	else if (entry_idx < (WLFC_MAC_DESC_TABLE_SIZE + WLFC_MAX_IFNUM))
		entry = &ctx->destination_entries.interfaces[entry_idx - WLFC_MAC_DESC_TABLE_SIZE];
	else
		entry = &ctx->destination_entries.other;

	pktq_penq(&entry->afq, prec, p);

	return BCME_OK;
}

/** afq = At Firmware Queue, queue containing packets pending in the dongle */
static int
_dhd_wlfc_deque_afq(athost_wl_status_info_t* ctx, uint16 hslot, uint8 hcnt, uint8 prec,
	void **pktout)
{
	wlfc_mac_descriptor_t *entry;
	struct pktq *pq;
	struct pktq_prec *q;
	void *p, *b;

	if (!ctx) {
		DHD_ERROR(("%s: ctx(%p), pktout(%p)\n", __FUNCTION__, ctx, pktout));
		return BCME_BADARG;
	}

	if (pktout) {
		*pktout = NULL;
	}

	ASSERT(hslot < (WLFC_MAC_DESC_TABLE_SIZE + WLFC_MAX_IFNUM + 1));

	if (hslot < WLFC_MAC_DESC_TABLE_SIZE)
		entry  = &ctx->destination_entries.nodes[hslot];
	else if (hslot < (WLFC_MAC_DESC_TABLE_SIZE + WLFC_MAX_IFNUM))
		entry = &ctx->destination_entries.interfaces[hslot - WLFC_MAC_DESC_TABLE_SIZE];
	else
		entry = &ctx->destination_entries.other;

	pq = &entry->afq;

	ASSERT(prec < pq->num_prec);

	q = &pq->q[prec];

	b = NULL;
	p = q->head;

	while (p && (hcnt != WL_TXSTATUS_GET_FREERUNCTR(DHD_PKTTAG_H2DTAG(PKTTAG(p)))))
	{
		b = p;
		p = PKTLINK(p);
	}

	if (p == NULL) {
		/* none is matched */
		if (b) {
			DHD_ERROR(("%s: can't find matching seq(%d)\n", __FUNCTION__, hcnt));
		} else {
			DHD_ERROR(("%s: queue is empty\n", __FUNCTION__));
		}

		return BCME_ERROR;
	}

	bcm_pkt_validate_chk(p);

	if (!b) {
		/* head packet is matched */
		if ((q->head = PKTLINK(p)) == NULL) {
			q->tail = NULL;
		}
	} else {
		/* middle packet is matched */
		DHD_INFO(("%s: out of order, seq(%d), head_seq(%d)\n", __FUNCTION__, hcnt,
			WL_TXSTATUS_GET_FREERUNCTR(DHD_PKTTAG_H2DTAG(PKTTAG(q->head)))));
		ctx->stats.ooo_pkts[prec]++;
		PKTSETLINK(b, PKTLINK(p));
		if (PKTLINK(p) == NULL) {
			q->tail = b;
		}
	}

	q->len--;
	pq->len--;

	PKTSETLINK(p, NULL);

	if (pktout) {
		*pktout = p;
	}

	return BCME_OK;
} /* _dhd_wlfc_deque_afq */

/**
 * Flow control information piggy backs on packets, in the form of one or more TLVs. This function
 * pushes one or more TLVs onto a packet that is going to be sent towards the dongle.
 *
 *     @param[in]     ctx
 *     @param[in/out] packet
 *     @param[in]     tim_signal TRUE if parameter 'tim_bmp' is valid
 *     @param[in]     tim_bmp
 *     @param[in]     mac_handle
 *     @param[in]     htodtag
 *     @param[in]     htodseq d11 seqno for seqno reuse, only used if 'seq reuse' was agreed upon
 *                    earlier between host and firmware.
 *     @param[in]     skip_wlfc_hdr
 */
static int
_dhd_wlfc_pushheader(athost_wl_status_info_t* ctx, void** packet, bool tim_signal,
	uint8 tim_bmp, uint8 mac_handle, uint32 htodtag, uint16 htodseq, bool skip_wlfc_hdr)
{
	uint32 wl_pktinfo = 0;
	uint8* wlh;
	uint8 dataOffset = 0;
	uint8 fillers;
	uint8 tim_signal_len = 0;
	dhd_pub_t *dhdp = (dhd_pub_t *)ctx->dhdp;

	struct bdc_header *h;
	void *p = *packet;

	if (skip_wlfc_hdr)
		goto push_bdc_hdr;

	if (tim_signal) {
		tim_signal_len = TLV_HDR_LEN + WLFC_CTL_VALUE_LEN_PENDING_TRAFFIC_BMP;
	}

	/* +2 is for Type[1] and Len[1] in TLV, plus TIM signal */
	dataOffset = WLFC_CTL_VALUE_LEN_PKTTAG + TLV_HDR_LEN + tim_signal_len;
	if (WLFC_GET_REUSESEQ(dhdp->wlfc_mode)) {
		dataOffset += WLFC_CTL_VALUE_LEN_SEQ;
	}

	fillers = ROUNDUP(dataOffset, 4) - dataOffset;
	dataOffset += fillers;

	PKTPUSH(ctx->osh, p, dataOffset);
	wlh = (uint8*) PKTDATA(ctx->osh, p);

	wl_pktinfo = htol32(htodtag);

	wlh[TLV_TAG_OFF] = WLFC_CTL_TYPE_PKTTAG;
	wlh[TLV_LEN_OFF] = WLFC_CTL_VALUE_LEN_PKTTAG;
	memcpy(&wlh[TLV_HDR_LEN] /* dst */, &wl_pktinfo, sizeof(uint32));

	if (WLFC_GET_REUSESEQ(dhdp->wlfc_mode)) {
		uint16 wl_seqinfo = htol16(htodseq);
		wlh[TLV_LEN_OFF] += WLFC_CTL_VALUE_LEN_SEQ;
		memcpy(&wlh[TLV_HDR_LEN + WLFC_CTL_VALUE_LEN_PKTTAG], &wl_seqinfo,
			WLFC_CTL_VALUE_LEN_SEQ);
	}

	if (tim_signal_len) {
		wlh[dataOffset - fillers - tim_signal_len ] =
			WLFC_CTL_TYPE_PENDING_TRAFFIC_BMP;
		wlh[dataOffset - fillers - tim_signal_len + 1] =
			WLFC_CTL_VALUE_LEN_PENDING_TRAFFIC_BMP;
		wlh[dataOffset - fillers - tim_signal_len + 2] = mac_handle;
		wlh[dataOffset - fillers - tim_signal_len + 3] = tim_bmp;
	}
	if (fillers)
		memset(&wlh[dataOffset - fillers], WLFC_CTL_TYPE_FILLER, fillers);

push_bdc_hdr:
	PKTPUSH(ctx->osh, p, BDC_HEADER_LEN);
	h = (struct bdc_header *)PKTDATA(ctx->osh, p);
	h->flags = (BDC_PROTO_VER << BDC_FLAG_VER_SHIFT);
	if (PKTSUMNEEDED(p))
		h->flags |= BDC_FLAG_SUM_NEEDED;


	h->priority = (PKTPRIO(p) & BDC_PRIORITY_MASK);
	h->flags2 = 0;
	h->dataOffset = dataOffset >> 2;
	BDC_SET_IF_IDX(h, DHD_PKTTAG_IF(PKTTAG(p)));
	*packet = p;
	return BCME_OK;
} /* _dhd_wlfc_pushheader */

/**
 * Removes (PULLs) flow control related headers from the caller supplied packet, is invoked eg
 * when a packet is about to be freed.
 */
static int
_dhd_wlfc_pullheader(athost_wl_status_info_t* ctx, void* pktbuf)
{
	struct bdc_header *h;

	if (PKTLEN(ctx->osh, pktbuf) < BDC_HEADER_LEN) {
		DHD_ERROR(("%s: rx data too short (%d < %d)\n", __FUNCTION__,
		           PKTLEN(ctx->osh, pktbuf), BDC_HEADER_LEN));
		return BCME_ERROR;
	}
	h = (struct bdc_header *)PKTDATA(ctx->osh, pktbuf);

	/* pull BDC header */
	PKTPULL(ctx->osh, pktbuf, BDC_HEADER_LEN);

	if (PKTLEN(ctx->osh, pktbuf) < (uint)(h->dataOffset << 2)) {
		DHD_ERROR(("%s: rx data too short (%d < %d)\n", __FUNCTION__,
		           PKTLEN(ctx->osh, pktbuf), (h->dataOffset << 2)));
		return BCME_ERROR;
	}

	/* pull wl-header */
	PKTPULL(ctx->osh, pktbuf, (h->dataOffset << 2));
	return BCME_OK;
}

/**
 * @param[in/out] p packet
 */
static wlfc_mac_descriptor_t*
_dhd_wlfc_find_table_entry(athost_wl_status_info_t* ctx, void* p)
{
	int i;
	wlfc_mac_descriptor_t* table = ctx->destination_entries.nodes;
	uint8 ifid = DHD_PKTTAG_IF(PKTTAG(p));
	uint8* dstn = DHD_PKTTAG_DSTN(PKTTAG(p));
	wlfc_mac_descriptor_t* entry = DHD_PKTTAG_ENTRY(PKTTAG(p));
	int iftype = ctx->destination_entries.interfaces[ifid].iftype;

	/* saved one exists, return it */
	if (entry)
		return entry;

	/* Multicast destination, STA and P2P clients get the interface entry.
	 * STA/GC gets the Mac Entry for TDLS destinations, TDLS destinations
	 * have their own entry.
	 */
	if ((iftype == WLC_E_IF_ROLE_STA || ETHER_ISMULTI(dstn) ||
		iftype == WLC_E_IF_ROLE_P2P_CLIENT) &&
		(ctx->destination_entries.interfaces[ifid].occupied)) {
			entry = &ctx->destination_entries.interfaces[ifid];
	}

	if (entry && ETHER_ISMULTI(dstn)) {
		DHD_PKTTAG_SET_ENTRY(PKTTAG(p), entry);
		return entry;
	}

	for (i = 0; i < WLFC_MAC_DESC_TABLE_SIZE; i++) {
		if (table[i].occupied) {
			if (table[i].interface_id == ifid) {
				if (!memcmp(table[i].ea, dstn, ETHER_ADDR_LEN)) {
					entry = &table[i];
					break;
				}
			}
		}
	}

	if (entry == NULL)
		entry = &ctx->destination_entries.other;

	DHD_PKTTAG_SET_ENTRY(PKTTAG(p), entry);

	return entry;
} /* _dhd_wlfc_find_table_entry */

/**
 * In case a packet must be dropped (because eg the queues are full), various tallies have to be
 * be updated. Called from several other functions.
 *     @param[in] dhdp pointer to public DHD structure
 *     @param[in] prec precedence of the packet
 *     @param[in] p    the packet to be dropped
 *     @param[in] bPktInQ TRUE if packet is part of a queue
 */
static int
_dhd_wlfc_prec_drop(dhd_pub_t *dhdp, int prec, void* p, bool bPktInQ)
{
	athost_wl_status_info_t* ctx;
	void *pout = NULL;

	ASSERT(dhdp && p);
	ASSERT(prec >= 0 && prec <= WLFC_PSQ_PREC_COUNT);

	ctx = (athost_wl_status_info_t*)dhdp->wlfc_state;

	if (!WLFC_GET_AFQ(dhdp->wlfc_mode) && (prec & 1)) {
		/* suppressed queue, need pop from hanger */
		_dhd_wlfc_hanger_poppkt(ctx->hanger, WL_TXSTATUS_GET_HSLOT(DHD_PKTTAG_H2DTAG
					(PKTTAG(p))), &pout, TRUE);
		ASSERT(p == pout);
	}

	if (!(prec & 1)) {
#ifdef DHDTCPACK_SUPPRESS
		/* pkt in delayed q, so fake push BDC header for
		 * dhd_tcpack_check_xmit() and dhd_txcomplete().
		 */
		_dhd_wlfc_pushheader(ctx, &p, FALSE, 0, 0, 0, 0, TRUE);

		/* This packet is about to be freed, so remove it from tcp_ack_info_tbl
		 * This must be one of...
		 * 1. A pkt already in delayQ is evicted by another pkt with higher precedence
		 * in _dhd_wlfc_prec_enq_with_drop()
		 * 2. A pkt could not be enqueued to delayQ because it is full,
		 * in _dhd_wlfc_enque_delayq().
		 * 3. A pkt could not be enqueued to delayQ because it is full,
		 * in _dhd_wlfc_rollback_packet_toq().
		 */
		if (dhd_tcpack_check_xmit(dhdp, p) == BCME_ERROR) {
			DHD_ERROR(("%s %d: tcpack_suppress ERROR!!!"
				" Stop using it\n",
				__FUNCTION__, __LINE__));
			dhd_tcpack_suppress_set(dhdp, TCPACK_SUP_OFF);
		}
#endif /* DHDTCPACK_SUPPRESS */
	}

	if (bPktInQ) {
		ctx->pkt_cnt_in_q[DHD_PKTTAG_IF(PKTTAG(p))][prec>>1]--;
		ctx->pkt_cnt_per_ac[prec>>1]--;
		ctx->pkt_cnt_in_psq--;
	}

	ctx->pkt_cnt_in_drv[DHD_PKTTAG_IF(PKTTAG(p))][DHD_PKTTAG_FIFO(PKTTAG(p))]--;
	ctx->stats.pktout++;
	ctx->stats.drop_pkts[prec]++;

	dhd_txcomplete(dhdp, p, FALSE);
	PKTFREE(ctx->osh, p, TRUE);

	return 0;
} /* _dhd_wlfc_prec_drop */

/**
 * Called when eg the host handed a new packet over to the driver, or when the dongle reported
 * that a packet could currently not be transmitted (=suppressed). This function enqueues a transmit
 * packet in the host driver to be (re)transmitted at a later opportunity.
 *     @param[in] dhdp pointer to public DHD structure
 *     @param[in] qHead When TRUE, queue packet at head instead of tail, to preserve d11 sequence
 */
static bool
_dhd_wlfc_prec_enq_with_drop(dhd_pub_t *dhdp, struct pktq *pq, void *pkt, int prec, bool qHead,
	uint8 current_seq)
{
	void *p = NULL;
	int eprec = -1;		/* precedence to evict from */
	athost_wl_status_info_t* ctx;

	ASSERT(dhdp && pq && pkt);
	ASSERT(prec >= 0 && prec < pq->num_prec);

	ctx = (athost_wl_status_info_t*)dhdp->wlfc_state;

	/* Fast case, precedence queue is not full and we are also not
	 * exceeding total queue length
	 */
	if (!pktq_pfull(pq, prec) && !pktq_full(pq)) {
		goto exit;
	}

	/* Determine precedence from which to evict packet, if any */
	if (pktq_pfull(pq, prec)) {
		eprec = prec;
	} else if (pktq_full(pq)) {
		p = pktq_peek_tail(pq, &eprec);
		if (!p) {
			DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
			return FALSE;
		}
		if ((eprec > prec) || (eprec < 0)) {
			if (!pktq_pempty(pq, prec)) {
				eprec = prec;
			} else {
				return FALSE;
			}
		}
	}

	/* Evict if needed */
	if (eprec >= 0) {
		/* Detect queueing to unconfigured precedence */
		ASSERT(!pktq_pempty(pq, eprec));
		/* Evict all fragmented frames */
		dhd_prec_drop_pkts(dhdp, pq, eprec, _dhd_wlfc_prec_drop);
	}

exit:
	/* Enqueue */
	_dhd_wlfc_prec_enque(pq, prec, pkt, qHead, current_seq,
		WLFC_GET_REORDERSUPP(dhdp->wlfc_mode));
	ctx->pkt_cnt_in_q[DHD_PKTTAG_IF(PKTTAG(pkt))][prec>>1]++;
	ctx->pkt_cnt_per_ac[prec>>1]++;
	ctx->pkt_cnt_in_psq++;

	return TRUE;
} /* _dhd_wlfc_prec_enq_with_drop */

/**
 * Called during eg the 'committing' of a transmit packet from the OS layer to a lower layer, in
 * the event that this 'commit' failed.
 */
static int
_dhd_wlfc_rollback_packet_toq(athost_wl_status_info_t* ctx,
	void* p, ewlfc_packet_state_t pkt_type, uint32 hslot)
{
	/*
	 * put the packet back to the head of queue
	 * - suppressed packet goes back to suppress sub-queue
	 * - pull out the header, if new or delayed packet
	 *
	 * Note: hslot is used only when header removal is done.
	 */
	wlfc_mac_descriptor_t* entry;
	int rc = BCME_OK;
	int prec, fifo_id;

	entry = _dhd_wlfc_find_table_entry(ctx, p);
	prec = DHD_PKTTAG_FIFO(PKTTAG(p));
	fifo_id = prec << 1;
	if (pkt_type == eWLFC_PKTTYPE_SUPPRESSED)
		fifo_id += 1;
	if (entry != NULL) {
		/*
		if this packet did not count against FIFO credit, it must have
		taken a requested_credit from the firmware (for pspoll etc.)
		*/
		if ((prec != AC_COUNT) && !DHD_PKTTAG_CREDITCHECK(PKTTAG(p)))
			entry->requested_credit++;

		if (pkt_type == eWLFC_PKTTYPE_DELAYED) {
			/* decrement sequence count */
			WLFC_DECR_SEQCOUNT(entry, prec);
			/* remove header first */
			rc = _dhd_wlfc_pullheader(ctx, p);
			if (rc != BCME_OK) {
				DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
				goto exit;
			}
		}

		if (_dhd_wlfc_prec_enq_with_drop(ctx->dhdp, &entry->psq, p, fifo_id, TRUE,
			WLFC_SEQCOUNT(entry, fifo_id>>1))
			== FALSE) {
			/* enque failed */
			DHD_ERROR(("Error: %s():%d, fifo_id(%d)\n",
				__FUNCTION__, __LINE__, fifo_id));
			rc = BCME_ERROR;
		}
	} else {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		rc = BCME_ERROR;
	}

exit:
	if (rc != BCME_OK) {
		ctx->stats.rollback_failed++;
		_dhd_wlfc_prec_drop(ctx->dhdp, fifo_id, p, FALSE);
	} else {
		ctx->stats.rollback++;
	}

	return rc;
} /* _dhd_wlfc_rollback_packet_toq */

/** Returns TRUE if host OS -> DHD flow control is allowed on the caller supplied interface */
static bool
_dhd_wlfc_allow_fc(athost_wl_status_info_t* ctx, uint8 ifid)
{
	int prec, ac_traffic = WLFC_NO_TRAFFIC;

	for (prec = 0; prec < AC_COUNT; prec++) {
		if (ctx->pkt_cnt_in_drv[ifid][prec] > 0) {
			if (ac_traffic == WLFC_NO_TRAFFIC)
				ac_traffic = prec + 1;
			else if (ac_traffic != (prec + 1))
				ac_traffic = WLFC_MULTI_TRAFFIC;
		}
	}

	if (ac_traffic >= 1 && ac_traffic <= AC_COUNT) {
		/* single AC (BE/BK/VI/VO) in queue */
		if (ctx->allow_fc) {
			return TRUE;
		} else {
			uint32 delta;
			uint32 curr_t = OSL_SYSUPTIME();

			if (ctx->fc_defer_timestamp == 0) {
				/* first single ac scenario */
				ctx->fc_defer_timestamp = curr_t;
				return FALSE;
			}

			/* single AC duration, this handles wrap around, e.g. 1 - ~0 = 2. */
			delta = curr_t - ctx->fc_defer_timestamp;
			if (delta >= WLFC_FC_DEFER_PERIOD_MS) {
				ctx->allow_fc = TRUE;
			}
		}
	} else {
		/* multiple ACs or BCMC in queue */
		ctx->allow_fc = FALSE;
		ctx->fc_defer_timestamp = 0;
	}

	return ctx->allow_fc;
} /* _dhd_wlfc_allow_fc */

/**
 * Starts or stops the flow of transmit packets from the host OS towards the DHD, depending on
 * low/high watermarks.
 */
static void
_dhd_wlfc_flow_control_check(athost_wl_status_info_t* ctx, struct pktq* pq, uint8 if_id)
{
	dhd_pub_t *dhdp;

	ASSERT(ctx);

	dhdp = (dhd_pub_t *)ctx->dhdp;
	ASSERT(dhdp);

	if (dhdp->skip_fc && dhdp->skip_fc((void *)dhdp, if_id))
		return;

	if ((ctx->hostif_flow_state[if_id] == OFF) && !_dhd_wlfc_allow_fc(ctx, if_id))
		return;

	if ((pq->len <= WLFC_FLOWCONTROL_LOWATER) && (ctx->hostif_flow_state[if_id] == ON)) {
		/* start traffic */
		ctx->hostif_flow_state[if_id] = OFF;
		/*
		WLFC_DBGMESG(("qlen:%02d, if:%02d, ->OFF, start traffic %s()\n",
		pq->len, if_id, __FUNCTION__));
		*/
		WLFC_DBGMESG(("F"));

		dhd_txflowcontrol(dhdp, if_id, OFF);

		ctx->toggle_host_if = 0;
	}

	if ((pq->len >= WLFC_FLOWCONTROL_HIWATER) && (ctx->hostif_flow_state[if_id] == OFF)) {
		/* stop traffic */
		ctx->hostif_flow_state[if_id] = ON;
		/*
		WLFC_DBGMESG(("qlen:%02d, if:%02d, ->ON, stop traffic   %s()\n",
		pq->len, if_id, __FUNCTION__));
		*/
		WLFC_DBGMESG(("N"));

		dhd_txflowcontrol(dhdp, if_id, ON);

		ctx->host_ifidx = if_id;
		ctx->toggle_host_if = 1;
	}

	return;
} /* _dhd_wlfc_flow_control_check */

static int
_dhd_wlfc_send_signalonly_packet(athost_wl_status_info_t* ctx, wlfc_mac_descriptor_t* entry,
	uint8 ta_bmp)
{
	int rc = BCME_OK;
	void* p = NULL;
	int dummylen = ((dhd_pub_t *)ctx->dhdp)->hdrlen+ 16;
	dhd_pub_t *dhdp = (dhd_pub_t *)ctx->dhdp;

	if (dhdp->proptxstatus_txoff) {
		rc = BCME_NORESOURCE;
		return rc;
	}

	/* allocate a dummy packet */
	p = PKTGET(ctx->osh, dummylen, TRUE);
	if (p) {
		PKTPULL(ctx->osh, p, dummylen);
		DHD_PKTTAG_SET_H2DTAG(PKTTAG(p), 0);
		_dhd_wlfc_pushheader(ctx, &p, TRUE, ta_bmp, entry->mac_handle, 0, 0, FALSE);
		DHD_PKTTAG_SETSIGNALONLY(PKTTAG(p), 1);
		DHD_PKTTAG_WLFCPKT_SET(PKTTAG(p), 1);
#ifdef PROP_TXSTATUS_DEBUG
		ctx->stats.signal_only_pkts_sent++;
#endif

#if defined(BCMPCIE)
		rc = dhd_bus_txdata(dhdp->bus, p, ctx->host_ifidx);
#else
		rc = dhd_bus_txdata(dhdp->bus, p);
#endif
		if (rc != BCME_OK) {
			_dhd_wlfc_pullheader(ctx, p);
			PKTFREE(ctx->osh, p, TRUE);
		}
	} else {
		DHD_ERROR(("%s: couldn't allocate new %d-byte packet\n",
		           __FUNCTION__, dummylen));
		rc = BCME_NOMEM;
		dhdp->tx_pktgetfail++;
	}

	return rc;
} /* _dhd_wlfc_send_signalonly_packet */

/**
 * Called on eg receiving 'mac close' indication from dongle. Updates the per-MAC administration
 * maintained in caller supplied parameter 'entry'.
 *
 *    @param[in/out] entry  administration about a remote MAC entity
 *    @param[in]     prec   precedence queue for this remote MAC entitity
 *
 * Return value: TRUE if traffic availability changed
 */
static bool
_dhd_wlfc_traffic_pending_check(athost_wl_status_info_t* ctx, wlfc_mac_descriptor_t* entry,
	int prec)
{
	bool rc = FALSE;

	if (entry->state == WLFC_STATE_CLOSE) {
		if ((pktq_plen(&entry->psq, (prec << 1)) == 0) &&
			(pktq_plen(&entry->psq, ((prec << 1) + 1)) == 0)) {
			/* no packets in both 'normal' and 'suspended' queues */
			if (entry->traffic_pending_bmp & NBITVAL(prec)) {
				rc = TRUE;
				entry->traffic_pending_bmp =
					entry->traffic_pending_bmp & ~ NBITVAL(prec);
			}
		} else {
			/* packets are queued in host for transmission to dongle */
			if (!(entry->traffic_pending_bmp & NBITVAL(prec))) {
				rc = TRUE;
				entry->traffic_pending_bmp =
					entry->traffic_pending_bmp | NBITVAL(prec);
			}
		}
	}

	if (rc) {
		/* request a TIM update to firmware at the next piggyback opportunity */
		if (entry->traffic_lastreported_bmp != entry->traffic_pending_bmp) {
			entry->send_tim_signal = 1;
			_dhd_wlfc_send_signalonly_packet(ctx, entry, entry->traffic_pending_bmp);
			entry->traffic_lastreported_bmp = entry->traffic_pending_bmp;
			entry->send_tim_signal = 0;
		} else {
			rc = FALSE;
		}
	}

	return rc;
} /* _dhd_wlfc_traffic_pending_check */

/**
 * Called on receiving a 'd11 suppressed' or 'wl suppressed' tx status from the firmware. Enqueues
 * the packet to transmit to firmware again at a later opportunity.
 */
static int
_dhd_wlfc_enque_suppressed(athost_wl_status_info_t* ctx, int prec, void* p)
{
	wlfc_mac_descriptor_t* entry;

	entry = _dhd_wlfc_find_table_entry(ctx, p);
	if (entry == NULL) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_NOTFOUND;
	}
	/*
	- suppressed packets go to sub_queue[2*prec + 1] AND
	- delayed packets go to sub_queue[2*prec + 0] to ensure
	order of delivery.
	*/
	if (_dhd_wlfc_prec_enq_with_drop(ctx->dhdp, &entry->psq, p, ((prec << 1) + 1), FALSE,
		WLFC_SEQCOUNT(entry, prec))
		== FALSE) {
		ctx->stats.delayq_full_error++;
		/* WLFC_DBGMESG(("Error: %s():%d\n", __FUNCTION__, __LINE__)); */
		WLFC_DBGMESG(("s"));
		return BCME_ERROR;
	}

	/* A packet has been pushed, update traffic availability bitmap, if applicable */
	_dhd_wlfc_traffic_pending_check(ctx, entry, prec);
	_dhd_wlfc_flow_control_check(ctx, &entry->psq, DHD_PKTTAG_IF(PKTTAG(p)));
	return BCME_OK;
}

/**
 * Called when a transmit packet is about to be 'committed' from the OS layer to a lower layer
 * towards the dongle (eg the DBUS layer). Updates wlfc administration. May modify packet.
 *
 *     @param[in/out] ctx    driver specific flow control administration
 *     @param[in/out] entry  The remote MAC entity for which the packet is destined.
 *     @param[in/out] packet Packet to send. This function optionally adds TLVs to the packet.
 *     @param[in] header_needed True if packet is 'new' to flow control
 *     @param[out] slot Handle to container in which the packet was 'parked'
 */
static int
_dhd_wlfc_pretx_pktprocess(athost_wl_status_info_t* ctx,
	wlfc_mac_descriptor_t* entry, void** packet, int header_needed, uint32* slot)
{
	int rc = BCME_OK;
	int hslot = WLFC_HANGER_MAXITEMS;
	bool send_tim_update = FALSE;
	uint32 htod = 0;
	uint16 htodseq = 0;
	uint8 free_ctr;
	int gen = 0xff;
	dhd_pub_t *dhdp = (dhd_pub_t *)ctx->dhdp;
	void * p = *packet;

	*slot = hslot;

	if (entry == NULL) {
		entry = _dhd_wlfc_find_table_entry(ctx, p);
	}

	if (entry == NULL) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_ERROR;
	}

	if (entry->send_tim_signal) {
		/* sends a traffic indication bitmap to the dongle */
		send_tim_update = TRUE;
		entry->send_tim_signal = 0;
		entry->traffic_lastreported_bmp = entry->traffic_pending_bmp;
	}

	if (header_needed) {
		if (WLFC_GET_AFQ(dhdp->wlfc_mode)) {
			hslot = (uint)(entry - &ctx->destination_entries.nodes[0]);
		} else {
			hslot = _dhd_wlfc_hanger_get_free_slot(ctx->hanger);
		}
		gen = entry->generation;
		free_ctr = WLFC_SEQCOUNT(entry, DHD_PKTTAG_FIFO(PKTTAG(p)));
	} else {
		if (WLFC_GET_REUSESEQ(dhdp->wlfc_mode)) {
			htodseq = DHD_PKTTAG_H2DSEQ(PKTTAG(p));
		}

		hslot = WL_TXSTATUS_GET_HSLOT(DHD_PKTTAG_H2DTAG(PKTTAG(p)));

		if (WLFC_GET_REORDERSUPP(dhdp->wlfc_mode)) {
			gen = entry->generation;
		} else if (WLFC_GET_AFQ(dhdp->wlfc_mode)) {
			gen = WL_TXSTATUS_GET_GENERATION(DHD_PKTTAG_H2DTAG(PKTTAG(p)));
		} else {
			_dhd_wlfc_hanger_get_genbit(ctx->hanger, p, hslot, &gen);
		}

		free_ctr = WL_TXSTATUS_GET_FREERUNCTR(DHD_PKTTAG_H2DTAG(PKTTAG(p)));
		/* remove old header */
		_dhd_wlfc_pullheader(ctx, p);
	}

	if (hslot >= WLFC_HANGER_MAXITEMS) {
		DHD_ERROR(("Error: %s():no hanger slot available\n", __FUNCTION__));
		return BCME_ERROR;
	}

	WL_TXSTATUS_SET_FREERUNCTR(htod, free_ctr);
	WL_TXSTATUS_SET_HSLOT(htod, hslot);
	WL_TXSTATUS_SET_FIFO(htod, DHD_PKTTAG_FIFO(PKTTAG(p)));
	WL_TXSTATUS_SET_FLAGS(htod, WLFC_PKTFLAG_PKTFROMHOST);
	WL_TXSTATUS_SET_GENERATION(htod, gen);
	DHD_PKTTAG_SETPKTDIR(PKTTAG(p), 1);

	if (!DHD_PKTTAG_CREDITCHECK(PKTTAG(p))) {
		/*
		Indicate that this packet is being sent in response to an
		explicit request from the firmware side.
		*/
		WLFC_PKTFLAG_SET_PKTREQUESTED(htod);
	} else {
		WLFC_PKTFLAG_CLR_PKTREQUESTED(htod);
	}

	rc = _dhd_wlfc_pushheader(ctx, &p, send_tim_update,
		entry->traffic_lastreported_bmp, entry->mac_handle, htod, htodseq, FALSE);
	if (rc == BCME_OK) {
		DHD_PKTTAG_SET_H2DTAG(PKTTAG(p), htod);

		if (!WLFC_GET_AFQ(dhdp->wlfc_mode)) {
			wlfc_hanger_t *h = (wlfc_hanger_t*)(ctx->hanger);
			if (header_needed) {
				/*
				a new header was created for this packet.
				push to hanger slot and scrub q. Since bus
				send succeeded, increment seq number as well.
				*/
				rc = _dhd_wlfc_hanger_pushpkt(ctx->hanger, p, hslot);
				if (rc == BCME_OK) {
#ifdef PROP_TXSTATUS_DEBUG
					h->items[hslot].push_time =
						OSL_SYSUPTIME();
#endif
				} else {
					DHD_ERROR(("%s() hanger_pushpkt() failed, rc: %d\n",
						__FUNCTION__, rc));
				}
			} else {
				/* clear hanger state */
				if (((wlfc_hanger_t*)(ctx->hanger))->items[hslot].pkt != p)
					DHD_ERROR(("%s() pkt not match: cur %p, hanger pkt %p\n",
						__FUNCTION__, p, h->items[hslot].pkt));
				ASSERT(h->items[hslot].pkt == p);
				bcm_object_feature_set(h->items[hslot].pkt,
					BCM_OBJECT_FEATURE_PKT_STATE, 0);
				h->items[hslot].pkt_state = 0;
				h->items[hslot].pkt_txstatus = 0;
				h->items[hslot].state = WLFC_HANGER_ITEM_STATE_INUSE;
			}
		}

		if ((rc == BCME_OK) && header_needed) {
			/* increment free running sequence count */
			WLFC_INCR_SEQCOUNT(entry, DHD_PKTTAG_FIFO(PKTTAG(p)));
		}
	}
	*slot = hslot;
	*packet = p;
	return rc;
} /* _dhd_wlfc_pretx_pktprocess */

/**
 * A remote wireless mac may be temporarily 'closed' due to power management. Returns '1' if remote
 * mac is in the 'open' state, otherwise '0'.
 */
static int
_dhd_wlfc_is_destination_open(athost_wl_status_info_t* ctx,
	wlfc_mac_descriptor_t* entry, int prec)
{
	wlfc_mac_descriptor_t* interfaces = ctx->destination_entries.interfaces;

	if (entry->interface_id >= WLFC_MAX_IFNUM) {
		ASSERT(&ctx->destination_entries.other == entry);
		return 1;
	}

	if (interfaces[entry->interface_id].iftype ==
		WLC_E_IF_ROLE_P2P_GO) {
		/* - destination interface is of type p2p GO.
		For a p2pGO interface, if the destination is OPEN but the interface is
		CLOSEd, do not send traffic. But if the dstn is CLOSEd while there is
		destination-specific-credit left send packets. This is because the
		firmware storing the destination-specific-requested packet in queue.
		*/
		if ((entry->state == WLFC_STATE_CLOSE) && (entry->requested_credit == 0) &&
			(entry->requested_packet == 0)) {
			return 0;
		}
	}

	/* AP, p2p_go -> unicast desc entry, STA/p2p_cl -> interface desc. entry */
	if ((((entry->state == WLFC_STATE_CLOSE) ||
		(interfaces[entry->interface_id].state == WLFC_STATE_CLOSE)) &&
		(entry->requested_credit == 0) &&
		(entry->requested_packet == 0)) ||
		(!(entry->ac_bitmap & (1 << prec)))) {
		return 0;
	}

	return 1;
} /* _dhd_wlfc_is_destination_open */

/**
 * Dequeues a suppressed or delayed packet from a queue
 *    @param[in/out] ctx          Driver specific flow control administration
 *    @param[in]  prec            Precedence of queue to dequeue from
 *    @param[out] ac_credit_spent Boolean, returns 0 or 1
 *    @param[out] needs_hdr       Boolean, returns 0 or 1
 *    @param[out] entry_out       The remote MAC for which the packet is destined
 *    @param[in]  only_no_credit  If TRUE, searches all entries instead of just the active ones
 *
 * Return value: the dequeued packet
 */
static void*
_dhd_wlfc_deque_delayedq(athost_wl_status_info_t* ctx, int prec,
	uint8* ac_credit_spent, uint8* needs_hdr, wlfc_mac_descriptor_t** entry_out,
	bool only_no_credit)
{
	wlfc_mac_descriptor_t* entry;
	int total_entries;
	void* p = NULL;
	int i;
	uint8 credit_spent = ((prec == AC_COUNT) && !ctx->bcmc_credit_supported) ? 0 : 1;

	*entry_out = NULL;
	/* most cases a packet will count against FIFO credit */
	*ac_credit_spent = credit_spent;

	/* search all entries, include nodes as well as interfaces */
	if (only_no_credit) {
		total_entries = ctx->requested_entry_count;
	} else {
		total_entries = ctx->active_entry_count;
	}

	for (i = 0; i < total_entries; i++) {
		if (only_no_credit) {
			entry = ctx->requested_entry[i];
		} else {
			entry = ctx->active_entry_head;
			/* move head to ensure fair round-robin */
			ctx->active_entry_head = ctx->active_entry_head->next;
		}
		ASSERT(entry);

		if (entry->occupied && _dhd_wlfc_is_destination_open(ctx, entry, prec) &&
#ifdef PROPTX_MAXCOUNT
			(entry->transit_count < entry->transit_maxcount) &&
#endif /* PROPTX_MAXCOUNT */
			(entry->transit_count < WL_TXSTATUS_FREERUNCTR_MASK) &&
			(!entry->suppressed)) {
			*ac_credit_spent = credit_spent;
			if (entry->state == WLFC_STATE_CLOSE) {
				*ac_credit_spent = 0;
			}

			/* higher precedence will be picked up first,
			 * i.e. suppressed packets before delayed ones
			 */
			p = pktq_pdeq(&entry->psq, PSQ_SUP_IDX(prec));
			*needs_hdr = 0;
			if (p == NULL) {
				/* De-Q from delay Q */
				p = pktq_pdeq(&entry->psq, PSQ_DLY_IDX(prec));
				*needs_hdr = 1;
			}

			if (p != NULL) {
				bcm_pkt_validate_chk(p);
				/* did the packet come from suppress sub-queue? */
				if (entry->requested_credit > 0) {
					entry->requested_credit--;
#ifdef PROP_TXSTATUS_DEBUG
					entry->dstncredit_sent_packets++;
#endif
				} else if (entry->requested_packet > 0) {
					entry->requested_packet--;
					DHD_PKTTAG_SETONETIMEPKTRQST(PKTTAG(p));
				}

				*entry_out = entry;
				ctx->pkt_cnt_in_q[DHD_PKTTAG_IF(PKTTAG(p))][prec]--;
				ctx->pkt_cnt_per_ac[prec]--;
				ctx->pkt_cnt_in_psq--;
				_dhd_wlfc_flow_control_check(ctx, &entry->psq,
					DHD_PKTTAG_IF(PKTTAG(p)));
				/*
				 * A packet has been picked up, update traffic availability bitmap,
				 * if applicable.
				 */
				_dhd_wlfc_traffic_pending_check(ctx, entry, prec);
				return p;
			}
		}
	}
	return NULL;
} /* _dhd_wlfc_deque_delayedq */

/** Enqueues caller supplied packet on either a 'suppressed' or 'delayed' queue */
static int
_dhd_wlfc_enque_delayq(athost_wl_status_info_t* ctx, void* pktbuf, int prec)
{
	wlfc_mac_descriptor_t* entry;

	if (pktbuf != NULL) {
		entry = _dhd_wlfc_find_table_entry(ctx, pktbuf);
		if (entry == NULL) {
			DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
			return BCME_ERROR;
		}

		/*
		- suppressed packets go to sub_queue[2*prec + 1] AND
		- delayed packets go to sub_queue[2*prec + 0] to ensure
		order of delivery.
		*/
		if (_dhd_wlfc_prec_enq_with_drop(ctx->dhdp, &entry->psq, pktbuf, (prec << 1),
			FALSE, WLFC_SEQCOUNT(entry, prec))
			== FALSE) {
			WLFC_DBGMESG(("D"));
			ctx->stats.delayq_full_error++;
			return BCME_ERROR;
		}


		/* A packet has been pushed, update traffic availability bitmap, if applicable */
		_dhd_wlfc_traffic_pending_check(ctx, entry, prec);
	}

	return BCME_OK;
} /* _dhd_wlfc_enque_delayq */

/** Returns TRUE if caller supplied packet is destined for caller supplied interface */
static bool _dhd_wlfc_ifpkt_fn(void* p, void *p_ifid)
{
	if (!p || !p_ifid)
		return FALSE;

	return (DHD_PKTTAG_WLFCPKT(PKTTAG(p))&& (*((uint8 *)p_ifid) == DHD_PKTTAG_IF(PKTTAG(p))));
}

/** Returns TRUE if caller supplied packet is destined for caller supplied remote MAC */
static bool _dhd_wlfc_entrypkt_fn(void* p, void *entry)
{
	if (!p || !entry)
		return FALSE;

	return (DHD_PKTTAG_WLFCPKT(PKTTAG(p))&& (entry == DHD_PKTTAG_ENTRY(PKTTAG(p))));
}

static void
_dhd_wlfc_return_implied_credit(athost_wl_status_info_t* wlfc, void* pkt)
{
	dhd_pub_t *dhdp;
	bool credit_return = FALSE;

	if (!wlfc || !pkt) {
		return;
	}

	dhdp = (dhd_pub_t *)(wlfc->dhdp);
	if (dhdp && (dhdp->proptxstatus_mode == WLFC_FCMODE_IMPLIED_CREDIT) &&
		DHD_PKTTAG_CREDITCHECK(PKTTAG(pkt))) {
		int lender, credit_returned = 0;
		uint8 fifo_id = DHD_PKTTAG_FIFO(PKTTAG(pkt));

		credit_return = TRUE;

		/* Note that borrower is fifo_id */
		/* Return credits to highest priority lender first */
		for (lender = AC_COUNT; lender >= 0; lender--) {
			if (wlfc->credits_borrowed[fifo_id][lender] > 0) {
				wlfc->FIFO_credit[lender]++;
				wlfc->credits_borrowed[fifo_id][lender]--;
				credit_returned = 1;
				break;
			}
		}

		if (!credit_returned) {
			wlfc->FIFO_credit[fifo_id]++;
		}
	}

	BCM_REFERENCE(credit_return);
#if defined(DHD_WLFC_THREAD)
	if (credit_return) {
		_dhd_wlfc_thread_wakeup(dhdp);
	}
#endif /* defined(DHD_WLFC_THREAD) */
}

/** Removes and frees a packet from the hanger. Called during eg tx complete. */
static void
_dhd_wlfc_hanger_free_pkt(athost_wl_status_info_t* wlfc, uint32 slot_id, uint8 pkt_state,
	int pkt_txstatus)
{
	wlfc_hanger_t* hanger;
	wlfc_hanger_item_t* item;

	if (!wlfc)
		return;

	hanger = (wlfc_hanger_t*)wlfc->hanger;
	if (!hanger)
		return;

	if (slot_id == WLFC_HANGER_MAXITEMS)
		return;

	item = &hanger->items[slot_id];

	if (item->pkt) {
		item->pkt_state |= pkt_state;
		if (pkt_txstatus != -1)
			item->pkt_txstatus = (uint8)pkt_txstatus;
		bcm_object_feature_set(item->pkt, BCM_OBJECT_FEATURE_PKT_STATE, item->pkt_state);
		if (item->pkt_state == WLFC_HANGER_PKT_STATE_COMPLETE) {
			void *p = NULL;
			void *pkt = item->pkt;
			uint8 old_state = item->state;
			int ret = _dhd_wlfc_hanger_poppkt(wlfc->hanger, slot_id, &p, TRUE);
			BCM_REFERENCE(ret);
			BCM_REFERENCE(pkt);
			ASSERT((ret == BCME_OK) && p && (pkt == p));
			if (old_state == WLFC_HANGER_ITEM_STATE_INUSE_SUPPRESSED) {
				printf("ERROR: free a suppressed pkt %p state %d pkt_state %d\n",
					pkt, old_state, item->pkt_state);
			}
			ASSERT(old_state != WLFC_HANGER_ITEM_STATE_INUSE_SUPPRESSED);

			/* free packet */
			wlfc->pkt_cnt_in_drv[DHD_PKTTAG_IF(PKTTAG(p))]
				[DHD_PKTTAG_FIFO(PKTTAG(p))]--;
			wlfc->stats.pktout++;
			dhd_txcomplete((dhd_pub_t *)wlfc->dhdp, p, item->pkt_txstatus);
			PKTFREE(wlfc->osh, p, TRUE);
		}
	} else {
		/* free slot */
		if (item->state == WLFC_HANGER_ITEM_STATE_FREE)
			DHD_ERROR(("Error: %s():%d Multiple TXSTATUS or BUSRETURNED: %d (%d)\n",
			    __FUNCTION__, __LINE__, item->pkt_state, pkt_state));
		item->state = WLFC_HANGER_ITEM_STATE_FREE;
	}
} /* _dhd_wlfc_hanger_free_pkt */

/** Called during eg detach() */
static void
_dhd_wlfc_pktq_flush(athost_wl_status_info_t* ctx, struct pktq *pq,
	bool dir, f_processpkt_t fn, void *arg, q_type_t q_type)
{
	int prec;
	dhd_pub_t *dhdp = (dhd_pub_t *)ctx->dhdp;

	ASSERT(dhdp);

	/* Optimize flush, if pktq len = 0, just return.
	 * pktq len of 0 means pktq's prec q's are all empty.
	 */
	if (pq->len == 0) {
		return;
	}

	for (prec = 0; prec < pq->num_prec; prec++) {
		struct pktq_prec *q;
		void *p, *prev = NULL;

		q = &pq->q[prec];
		p = q->head;
		while (p) {
			bcm_pkt_validate_chk(p);
			if (fn == NULL || (*fn)(p, arg)) {
				bool head = (p == q->head);
				if (head)
					q->head = PKTLINK(p);
				else
					PKTSETLINK(prev, PKTLINK(p));
				if (q_type == Q_TYPE_PSQ) {
					if (!WLFC_GET_AFQ(dhdp->wlfc_mode) && (prec & 1)) {
						_dhd_wlfc_hanger_remove_reference(ctx->hanger, p);
					}
					ctx->pkt_cnt_in_q[DHD_PKTTAG_IF(PKTTAG(p))][prec>>1]--;
					ctx->pkt_cnt_per_ac[prec>>1]--;
					ctx->pkt_cnt_in_psq--;
					ctx->stats.cleanup_psq_cnt++;
					if (!(prec & 1)) {
						/* pkt in delayed q, so fake push BDC header for
						 * dhd_tcpack_check_xmit() and dhd_txcomplete().
						 */
						_dhd_wlfc_pushheader(ctx, &p, FALSE, 0, 0,
							0, 0, TRUE);
#ifdef DHDTCPACK_SUPPRESS
						if (dhd_tcpack_check_xmit(dhdp, p) == BCME_ERROR) {
							DHD_ERROR(("%s %d: tcpack_suppress ERROR!!!"
								" Stop using it\n",
								__FUNCTION__, __LINE__));
							dhd_tcpack_suppress_set(dhdp,
								TCPACK_SUP_OFF);
						}
#endif /* DHDTCPACK_SUPPRESS */
					}
				} else if (q_type == Q_TYPE_AFQ) {
					wlfc_mac_descriptor_t* entry =
						_dhd_wlfc_find_table_entry(ctx, p);
					if (entry->transit_count)
						entry->transit_count--;
					if (entry->suppr_transit_count) {
						entry->suppr_transit_count--;
						if (entry->suppressed &&
							(!entry->onbus_pkts_count) &&
							(!entry->suppr_transit_count))
							entry->suppressed = FALSE;
					}
					_dhd_wlfc_return_implied_credit(ctx, p);
					ctx->stats.cleanup_fw_cnt++;
				}
				PKTSETLINK(p, NULL);
				if (dir) {
					ctx->pkt_cnt_in_drv[DHD_PKTTAG_IF(PKTTAG(p))][prec>>1]--;
					ctx->stats.pktout++;
					dhd_txcomplete(dhdp, p, FALSE);
				}
				PKTFREE(ctx->osh, p, dir);

				q->len--;
				pq->len--;
				p = (head ? q->head : PKTLINK(prev));
			} else {
				prev = p;
				p = PKTLINK(p);
			}
		}

		if (q->head == NULL) {
			ASSERT(q->len == 0);
			q->tail = NULL;
		}

	}

	if (fn == NULL)
		ASSERT(pq->len == 0);
} /* _dhd_wlfc_pktq_flush */

#ifndef BCMDBUS
/** !BCMDBUS specific function. Dequeues a packet from the caller supplied queue. */
static void*
_dhd_wlfc_pktq_pdeq_with_fn(struct pktq *pq, int prec, f_processpkt_t fn, void *arg)
{
	struct pktq_prec *q;
	void *p, *prev = NULL;

	ASSERT(prec >= 0 && prec < pq->num_prec);

	q = &pq->q[prec];
	p = q->head;

	while (p) {
		if (fn == NULL || (*fn)(p, arg)) {
			break;
		} else {
			prev = p;
			p = PKTLINK(p);
		}
	}
	if (p == NULL)
		return NULL;

	bcm_pkt_validate_chk(p);

	if (prev == NULL) {
		if ((q->head = PKTLINK(p)) == NULL) {
			q->tail = NULL;
		}
	} else {
		PKTSETLINK(prev, PKTLINK(p));
		if (q->tail == p) {
			q->tail = prev;
		}
	}

	q->len--;

	pq->len--;

	PKTSETLINK(p, NULL);

	return p;
}

/** !BCMDBUS specific function */
static void
_dhd_wlfc_cleanup_txq(dhd_pub_t *dhd, f_processpkt_t fn, void *arg)
{
	int prec;
	void *pkt = NULL, *head = NULL, *tail = NULL;
	struct pktq *txq = (struct pktq *)dhd_bus_txq(dhd->bus);
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)dhd->wlfc_state;
	wlfc_hanger_t* h = (wlfc_hanger_t*)wlfc->hanger;
	wlfc_mac_descriptor_t* entry;

	dhd_os_sdlock_txq(dhd);
	for (prec = 0; prec < txq->num_prec; prec++) {
		while ((pkt = _dhd_wlfc_pktq_pdeq_with_fn(txq, prec, fn, arg))) {
#ifdef DHDTCPACK_SUPPRESS
			if (dhd_tcpack_check_xmit(dhd, pkt) == BCME_ERROR) {
				DHD_ERROR(("%s %d: tcpack_suppress ERROR!!! Stop using it\n",
					__FUNCTION__, __LINE__));
				dhd_tcpack_suppress_set(dhd, TCPACK_SUP_OFF);
			}
#endif /* DHDTCPACK_SUPPRESS */
			if (!head) {
				head = pkt;
			}
			if (tail) {
				PKTSETLINK(tail, pkt);
			}
			tail = pkt;
		}
	}
	dhd_os_sdunlock_txq(dhd);


	while ((pkt = head)) {
		head = PKTLINK(pkt);
		PKTSETLINK(pkt, NULL);
		entry = _dhd_wlfc_find_table_entry(wlfc, pkt);

		if (!WLFC_GET_AFQ(dhd->wlfc_mode) &&
			!_dhd_wlfc_hanger_remove_reference(h, pkt)) {
			DHD_ERROR(("%s: can't find pkt(%p) in hanger, free it anyway\n",
				__FUNCTION__, pkt));
		}
		if (entry->transit_count)
			entry->transit_count--;
		if (entry->suppr_transit_count) {
			entry->suppr_transit_count--;
			if (entry->suppressed &&
				(!entry->onbus_pkts_count) &&
				(!entry->suppr_transit_count))
				entry->suppressed = FALSE;
		}
		_dhd_wlfc_return_implied_credit(wlfc, pkt);
		wlfc->pkt_cnt_in_drv[DHD_PKTTAG_IF(PKTTAG(pkt))][DHD_PKTTAG_FIFO(PKTTAG(pkt))]--;
		wlfc->stats.pktout++;
		wlfc->stats.cleanup_txq_cnt++;
		dhd_txcomplete(dhd, pkt, FALSE);
		PKTFREE(wlfc->osh, pkt, TRUE);
	}
} /* _dhd_wlfc_cleanup_txq */
#endif /* !BCMDBUS */

/** called during eg detach */
void
_dhd_wlfc_cleanup(dhd_pub_t *dhd, f_processpkt_t fn, void *arg)
{
	int i;
	int total_entries;
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)dhd->wlfc_state;
	wlfc_mac_descriptor_t* table;
	wlfc_hanger_t* h = (wlfc_hanger_t*)wlfc->hanger;

	wlfc->stats.cleanup_txq_cnt = 0;
	wlfc->stats.cleanup_psq_cnt = 0;
	wlfc->stats.cleanup_fw_cnt = 0;

	/*
	*  flush sequence should be txq -> psq -> hanger/afq, hanger has to be last one
	*/
#ifndef BCMDBUS
	/* flush bus->txq */
	_dhd_wlfc_cleanup_txq(dhd, fn, arg);
#endif /* !BCMDBUS */

	/* flush psq, search all entries, include nodes as well as interfaces */
	total_entries = sizeof(wlfc->destination_entries)/sizeof(wlfc_mac_descriptor_t);
	table = (wlfc_mac_descriptor_t*)&wlfc->destination_entries;

	for (i = 0; i < total_entries; i++) {
		if (table[i].occupied) {
			/* release packets held in PSQ (both delayed and suppressed) */
			if (table[i].psq.len) {
				WLFC_DBGMESG(("%s(): PSQ[%d].len = %d\n",
					__FUNCTION__, i, table[i].psq.len));
				_dhd_wlfc_pktq_flush(wlfc, &table[i].psq, TRUE,
					fn, arg, Q_TYPE_PSQ);
			}

			/* free packets held in AFQ */
			if (WLFC_GET_AFQ(dhd->wlfc_mode) && (table[i].afq.len)) {
				_dhd_wlfc_pktq_flush(wlfc, &table[i].afq, TRUE,
					fn, arg, Q_TYPE_AFQ);
			}

			if ((fn == NULL) && (&table[i] != &wlfc->destination_entries.other)) {
				table[i].occupied = 0;
				if (table[i].transit_count || table[i].suppr_transit_count) {
					DHD_ERROR(("%s: table[%d] transit(%d), suppr_tansit(%d)\n",
						__FUNCTION__, i,
						table[i].transit_count,
						table[i].suppr_transit_count));
				}
			}
		}
	}

	/*
		. flush remained pkt in hanger queue, not in bus->txq nor psq.
		. the remained pkt was successfully downloaded to dongle already.
		. hanger slot state cannot be set to free until receive txstatus update.
	*/
	if (!WLFC_GET_AFQ(dhd->wlfc_mode)) {
		for (i = 0; i < h->max_items; i++) {
			if ((h->items[i].state == WLFC_HANGER_ITEM_STATE_INUSE) ||
				(h->items[i].state == WLFC_HANGER_ITEM_STATE_INUSE_SUPPRESSED)) {
				if (fn == NULL || (*fn)(h->items[i].pkt, arg)) {
					h->items[i].state = WLFC_HANGER_ITEM_STATE_FLUSHED;
				}
			}
		}
	}

	return;
} /* _dhd_wlfc_cleanup */

/** Called after eg the dongle signalled a new remote MAC that it connected with to the DHD */
static int
_dhd_wlfc_mac_entry_update(athost_wl_status_info_t* ctx, wlfc_mac_descriptor_t* entry,
	uint8 action, uint8 ifid, uint8 iftype, uint8* ea,
	f_processpkt_t fn, void *arg)
{
	int rc = BCME_OK;


	if ((action == eWLFC_MAC_ENTRY_ACTION_ADD) || (action == eWLFC_MAC_ENTRY_ACTION_UPDATE)) {
		entry->occupied = 1;
		entry->state = WLFC_STATE_OPEN;
		entry->requested_credit = 0;
		entry->interface_id = ifid;
		entry->iftype = iftype;
		entry->ac_bitmap = 0xff; /* update this when handling APSD */

		/* for an interface entry we may not care about the MAC address */
		if (ea != NULL)
			memcpy(&entry->ea[0], ea, ETHER_ADDR_LEN);

		if (action == eWLFC_MAC_ENTRY_ACTION_ADD) {
			entry->suppressed = FALSE;
			entry->transit_count = 0;
#ifdef PROPTX_MAXCOUNT
			entry->transit_maxcount = wl_ext_get_wlfc_maxcount(ctx->dhdp, ifid);
#endif /* PROPTX_MAXCOUNT */
			entry->suppr_transit_count = 0;
			entry->onbus_pkts_count = 0;
		}

		if (action == eWLFC_MAC_ENTRY_ACTION_ADD) {
			dhd_pub_t *dhdp = (dhd_pub_t *)(ctx->dhdp);

			pktq_init(&entry->psq, WLFC_PSQ_PREC_COUNT, WLFC_PSQ_LEN);
			_dhd_wlfc_flow_control_check(ctx, &entry->psq, ifid);

			if (WLFC_GET_AFQ(dhdp->wlfc_mode)) {
				pktq_init(&entry->afq, WLFC_AFQ_PREC_COUNT, WLFC_PSQ_LEN);
			}

			if (entry->next == NULL) {
				/* not linked to anywhere, add to tail */
				if (ctx->active_entry_head) {
					entry->prev = ctx->active_entry_head->prev;
					ctx->active_entry_head->prev->next = entry;
					ctx->active_entry_head->prev = entry;
					entry->next = ctx->active_entry_head;
				} else {
					ASSERT(ctx->active_entry_count == 0);
					entry->prev = entry->next = entry;
					ctx->active_entry_head = entry;
				}
				ctx->active_entry_count++;
			} else {
				DHD_ERROR(("%s():%d, entry(%d)\n", __FUNCTION__, __LINE__,
					(int)(entry - &ctx->destination_entries.nodes[0])));
			}
		}
	} else if (action == eWLFC_MAC_ENTRY_ACTION_DEL) {
		/* When the entry is deleted, the packets that are queued in the entry must be
		   cleanup. The cleanup action should be before the occupied is set as 0.
		*/
		_dhd_wlfc_cleanup(ctx->dhdp, fn, arg);
		_dhd_wlfc_flow_control_check(ctx, &entry->psq, ifid);

		entry->occupied = 0;
		entry->state = WLFC_STATE_CLOSE;
		memset(&entry->ea[0], 0, ETHER_ADDR_LEN);

		if (entry->next) {
			/* not floating, remove from Q */
			if (ctx->active_entry_count <= 1) {
				/* last item */
				ctx->active_entry_head = NULL;
				ctx->active_entry_count = 0;
			} else {
				entry->prev->next = entry->next;
				entry->next->prev = entry->prev;
				if (entry == ctx->active_entry_head) {
					ctx->active_entry_head = entry->next;
				}
				ctx->active_entry_count--;
			}
			entry->next = entry->prev = NULL;
		} else {
			DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		}
	}
	return rc;
} /* _dhd_wlfc_mac_entry_update */


#ifdef LIMIT_BORROW

/** LIMIT_BORROW specific function */
static int
_dhd_wlfc_borrow_credit(athost_wl_status_info_t* ctx, int highest_lender_ac, int borrower_ac,
	bool bBorrowAll)
{
	int lender_ac, borrow_limit = 0;
	int rc = -1;

	if (ctx == NULL) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return -1;
	}

	/* Borrow from lowest priority available AC (including BC/MC credits) */
	for (lender_ac = 0; lender_ac <= highest_lender_ac; lender_ac++) {
		if (!bBorrowAll) {
			borrow_limit = ctx->Init_FIFO_credit[lender_ac]/WLFC_BORROW_LIMIT_RATIO;
		} else {
			borrow_limit = 0;
		}

		if (ctx->FIFO_credit[lender_ac] > borrow_limit) {
			ctx->credits_borrowed[borrower_ac][lender_ac]++;
			ctx->FIFO_credit[lender_ac]--;
			rc = lender_ac;
			break;
		}
	}

	return rc;
}

/** LIMIT_BORROW specific function */
static int _dhd_wlfc_return_credit(athost_wl_status_info_t* ctx, int lender_ac, int borrower_ac)
{
	if ((ctx == NULL) || (lender_ac < 0) || (lender_ac > AC_COUNT) ||
		(borrower_ac < 0) || (borrower_ac > AC_COUNT)) {
		DHD_ERROR(("Error: %s():%d, ctx(%p), lender_ac(%d), borrower_ac(%d)\n",
			__FUNCTION__, __LINE__, ctx, lender_ac, borrower_ac));

		return BCME_BADARG;
	}

	ctx->credits_borrowed[borrower_ac][lender_ac]--;
	ctx->FIFO_credit[lender_ac]++;

	return BCME_OK;
}

#endif /* LIMIT_BORROW */

/**
 * Called on an interface event (WLC_E_IF) indicated by firmware.
 *     @param action : eg eWLFC_MAC_ENTRY_ACTION_UPDATE or eWLFC_MAC_ENTRY_ACTION_ADD
 */
static int
_dhd_wlfc_interface_entry_update(void* state,
	uint8 action, uint8 ifid, uint8 iftype, uint8* ea)
{
	athost_wl_status_info_t* ctx = (athost_wl_status_info_t*)state;
	wlfc_mac_descriptor_t* entry;

	if (ifid >= WLFC_MAX_IFNUM)
		return BCME_BADARG;

	entry = &ctx->destination_entries.interfaces[ifid];

	return _dhd_wlfc_mac_entry_update(ctx, entry, action, ifid, iftype, ea,
		_dhd_wlfc_ifpkt_fn, &ifid);
}

/**
 * Called eg on receiving a WLC_E_BCMC_CREDIT_SUPPORT event from the dongle (broadcast/multicast
 * specific)
 */
static int
_dhd_wlfc_BCMCCredit_support_update(void* state)
{
	athost_wl_status_info_t* ctx = (athost_wl_status_info_t*)state;

	ctx->bcmc_credit_supported = TRUE;
	return BCME_OK;
}

/** Called eg on receiving a WLC_E_FIFO_CREDIT_MAP event from the dongle */
static int
_dhd_wlfc_FIFOcreditmap_update(void* state, uint8* credits)
{
	athost_wl_status_info_t* ctx = (athost_wl_status_info_t*)state;
	int i;

	for (i = 0; i <= 4; i++) {
		if (ctx->Init_FIFO_credit[i] != ctx->FIFO_credit[i]) {
			DHD_ERROR(("%s: credit[i] is not returned, (%d %d)\n",
				__FUNCTION__, ctx->Init_FIFO_credit[i], ctx->FIFO_credit[i]));
		}
	}

	/* update the AC FIFO credit map */
	ctx->FIFO_credit[0] += (credits[0] - ctx->Init_FIFO_credit[0]);
	ctx->FIFO_credit[1] += (credits[1] - ctx->Init_FIFO_credit[1]);
	ctx->FIFO_credit[2] += (credits[2] - ctx->Init_FIFO_credit[2]);
	ctx->FIFO_credit[3] += (credits[3] - ctx->Init_FIFO_credit[3]);
	ctx->FIFO_credit[4] += (credits[4] - ctx->Init_FIFO_credit[4]);

	ctx->Init_FIFO_credit[0] = credits[0];
	ctx->Init_FIFO_credit[1] = credits[1];
	ctx->Init_FIFO_credit[2] = credits[2];
	ctx->Init_FIFO_credit[3] = credits[3];
	ctx->Init_FIFO_credit[4] = credits[4];

	/* credit for ATIM FIFO is not used yet. */
	ctx->Init_FIFO_credit[5] = ctx->FIFO_credit[5] = 0;

	return BCME_OK;
}

/**
 * Called during committing of a transmit packet from the OS DHD layer to the next layer towards
 * the dongle (eg the DBUS layer). All transmit packets flow via this function to the next layer.
 *
 *     @param[in/out] ctx      Driver specific flow control administration
 *     @param[in] ac           Access Category (QoS) of called supplied packet
 *     @param[in] commit_info  Contains eg the packet to send
 *     @param[in] fcommit      Function pointer to transmit function of next software layer
 *     @param[in] commit_ctx   Opaque context used when calling next layer
 */
static int
_dhd_wlfc_handle_packet_commit(athost_wl_status_info_t* ctx, int ac,
    dhd_wlfc_commit_info_t *commit_info, f_commitpkt_t fcommit, void* commit_ctx)
{
	uint32 hslot;
	int	rc;
	dhd_pub_t *dhdp = (dhd_pub_t *)(ctx->dhdp);

	/*
		if ac_fifo_credit_spent = 0

		This packet will not count against the FIFO credit.
		To ensure the txstatus corresponding to this packet
		does not provide an implied credit (default behavior)
		mark the packet accordingly.

		if ac_fifo_credit_spent = 1

		This is a normal packet and it counts against the FIFO
		credit count.
	*/
	DHD_PKTTAG_SETCREDITCHECK(PKTTAG(commit_info->p), commit_info->ac_fifo_credit_spent);
	rc = _dhd_wlfc_pretx_pktprocess(ctx, commit_info->mac_entry, &commit_info->p,
	     commit_info->needs_hdr, &hslot);

	if (rc == BCME_OK) {
		rc = fcommit(commit_ctx, commit_info->p);
		if (rc == BCME_OK) {
			uint8 gen = WL_TXSTATUS_GET_GENERATION(
				DHD_PKTTAG_H2DTAG(PKTTAG(commit_info->p)));
			ctx->stats.pkt2bus++;
			if (commit_info->ac_fifo_credit_spent || (ac == AC_COUNT)) {
				ctx->stats.send_pkts[ac]++;
				WLFC_HOST_FIFO_CREDIT_INC_SENTCTRS(ctx, ac);
			}

			if (gen != commit_info->mac_entry->generation) {
				/* will be suppressed back by design */
				if (!commit_info->mac_entry->suppressed) {
					commit_info->mac_entry->suppressed = TRUE;
				}
				commit_info->mac_entry->suppr_transit_count++;
			}
			commit_info->mac_entry->transit_count++;
			commit_info->mac_entry->onbus_pkts_count++;
		} else if (commit_info->needs_hdr) {
			if (!WLFC_GET_AFQ(dhdp->wlfc_mode)) {
				void *pout = NULL;
				/* pop hanger for delayed packet */
				_dhd_wlfc_hanger_poppkt(ctx->hanger, WL_TXSTATUS_GET_HSLOT(
					DHD_PKTTAG_H2DTAG(PKTTAG(commit_info->p))), &pout, TRUE);
				ASSERT(commit_info->p == pout);
			}
		}
	} else {
		ctx->stats.generic_error++;
	}

	if (rc != BCME_OK) {
		/*
		   pretx pkt process or bus commit has failed, rollback.
		   - remove wl-header for a delayed packet
		   - save wl-header header for suppressed packets
		   - reset credit check flag
		*/
		_dhd_wlfc_rollback_packet_toq(ctx, commit_info->p, commit_info->pkt_type, hslot);
		DHD_PKTTAG_SETCREDITCHECK(PKTTAG(commit_info->p), 0);
	}

	return rc;
} /* _dhd_wlfc_handle_packet_commit */

/** Returns remote MAC descriptor for caller supplied MAC address */
static uint8
_dhd_wlfc_find_mac_desc_id_from_mac(dhd_pub_t *dhdp, uint8 *ea)
{
	wlfc_mac_descriptor_t* table =
		((athost_wl_status_info_t*)dhdp->wlfc_state)->destination_entries.nodes;
	uint8 table_index;

	if (ea != NULL) {
		for (table_index = 0; table_index < WLFC_MAC_DESC_TABLE_SIZE; table_index++) {
			if ((memcmp(ea, &table[table_index].ea[0], ETHER_ADDR_LEN) == 0) &&
				table[table_index].occupied)
				return table_index;
		}
	}
	return WLFC_MAC_DESC_ID_INVALID;
}

/**
 * Called when the host receives a WLFC_CTL_TYPE_TXSTATUS event from the dongle, indicating the
 * status of a frame that the dongle attempted to transmit over the wireless medium.
 */
static int
dhd_wlfc_suppressed_acked_update(dhd_pub_t *dhd, uint16 hslot, uint8 prec, uint8 hcnt)
{
	athost_wl_status_info_t* ctx;
	wlfc_mac_descriptor_t* entry = NULL;
	struct pktq *pq;
	struct pktq_prec *q;
	void *p, *b;

	if (!dhd) {
		DHD_ERROR(("%s: dhd(%p)\n", __FUNCTION__, dhd));
		return BCME_BADARG;
	}
	ctx = (athost_wl_status_info_t*)dhd->wlfc_state;
	if (!ctx) {
		DHD_ERROR(("%s: ctx(%p)\n", __FUNCTION__, ctx));
		return BCME_ERROR;
	}

	ASSERT(hslot < (WLFC_MAC_DESC_TABLE_SIZE + WLFC_MAX_IFNUM + 1));

	if (hslot < WLFC_MAC_DESC_TABLE_SIZE)
		entry  = &ctx->destination_entries.nodes[hslot];
	else if (hslot < (WLFC_MAC_DESC_TABLE_SIZE + WLFC_MAX_IFNUM))
		entry = &ctx->destination_entries.interfaces[hslot - WLFC_MAC_DESC_TABLE_SIZE];
	else
		entry = &ctx->destination_entries.other;

	pq = &entry->psq;

	ASSERT(((prec << 1) + 1) < pq->num_prec);

	q = &pq->q[((prec << 1) + 1)];

	b = NULL;
	p = q->head;

	while (p && (hcnt != WL_TXSTATUS_GET_FREERUNCTR(DHD_PKTTAG_H2DTAG(PKTTAG(p))))) {
		b = p;
		p = PKTLINK(p);
	}

	if (p == NULL) {
		/* none is matched */
		if (b) {
			DHD_ERROR(("%s: can't find matching seq(%d)\n", __FUNCTION__, hcnt));
		} else {
			DHD_ERROR(("%s: queue is empty\n", __FUNCTION__));
		}

		return BCME_ERROR;
	}

	if (!b) {
		/* head packet is matched */
		if ((q->head = PKTLINK(p)) == NULL) {
			q->tail = NULL;
		}
	} else {
		/* middle packet is matched */
		PKTSETLINK(b, PKTLINK(p));
		if (PKTLINK(p) == NULL) {
			q->tail = b;
		}
	}

	q->len--;
	pq->len--;
	ctx->pkt_cnt_in_q[DHD_PKTTAG_IF(PKTTAG(p))][prec]--;
	ctx->pkt_cnt_per_ac[prec]--;

	PKTSETLINK(p, NULL);

	if (WLFC_GET_AFQ(dhd->wlfc_mode)) {
		_dhd_wlfc_enque_afq(ctx, p);
	} else {
		_dhd_wlfc_hanger_pushpkt(ctx->hanger, p, hslot);
	}

	entry->transit_count++;

	return BCME_OK;
}

static int
_dhd_wlfc_compressed_txstatus_update(dhd_pub_t *dhd, uint8* pkt_info, uint8 len, void** p_mac)
{
	uint8 status_flag_ori, status_flag;
	uint32 status;
	int ret = BCME_OK;
	int remove_from_hanger_ori, remove_from_hanger = 1;
	void* pktbuf = NULL;
	uint8 fifo_id = 0, gen = 0, count = 0, hcnt;
	uint16 hslot;
	wlfc_mac_descriptor_t* entry = NULL;
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)dhd->wlfc_state;
	uint16 seq = 0, seq_fromfw = 0, seq_num = 0;

	memcpy(&status, pkt_info, sizeof(uint32));
	status = ltoh32(status);
	status_flag = WL_TXSTATUS_GET_FLAGS(status);
	hcnt = WL_TXSTATUS_GET_FREERUNCTR(status);
	hslot = WL_TXSTATUS_GET_HSLOT(status);
	fifo_id = WL_TXSTATUS_GET_FIFO(status);
	gen = WL_TXSTATUS_GET_GENERATION(status);

	if (WLFC_GET_REUSESEQ(dhd->wlfc_mode)) {
		memcpy(&seq, pkt_info + WLFC_CTL_VALUE_LEN_TXSTATUS, WLFC_CTL_VALUE_LEN_SEQ);
		seq = ltoh16(seq);
		seq_fromfw = GET_WL_HAS_ASSIGNED_SEQ(seq);
		seq_num = WL_SEQ_GET_NUM(seq);
	}

	wlfc->stats.txstatus_in += len;

	if (status_flag == WLFC_CTL_PKTFLAG_DISCARD) {
		wlfc->stats.pkt_freed += len;
	} else if (status_flag == WLFC_CTL_PKTFLAG_DISCARD_NOACK) {
		wlfc->stats.pkt_freed += len;
	} else if (status_flag == WLFC_CTL_PKTFLAG_D11SUPPRESS) {
		wlfc->stats.d11_suppress += len;
		remove_from_hanger = 0;
	} else if (status_flag == WLFC_CTL_PKTFLAG_WLSUPPRESS) {
		wlfc->stats.wl_suppress += len;
		remove_from_hanger = 0;
	} else if (status_flag == WLFC_CTL_PKTFLAG_TOSSED_BYWLC) {
		wlfc->stats.wlc_tossed_pkts += len;
	} else if (status_flag == WLFC_CTL_PKTFLAG_SUPPRESS_ACKED) {
		wlfc->stats.pkt_freed += len;
	}

	if (dhd->proptxstatus_txstatus_ignore) {
		if (!remove_from_hanger) {
			DHD_ERROR(("suppress txstatus: %d\n", status_flag));
		}
		return BCME_OK;
	}

	status_flag_ori = status_flag;
	remove_from_hanger_ori = remove_from_hanger;

	while (count < len) {
		if (status_flag == WLFC_CTL_PKTFLAG_SUPPRESS_ACKED) {
			dhd_wlfc_suppressed_acked_update(dhd, hslot, fifo_id, hcnt);
		}
		if (WLFC_GET_AFQ(dhd->wlfc_mode)) {
			ret = _dhd_wlfc_deque_afq(wlfc, hslot, hcnt, fifo_id, &pktbuf);
		} else {
			status_flag = status_flag_ori;
			remove_from_hanger = remove_from_hanger_ori;
			ret = _dhd_wlfc_hanger_poppkt(wlfc->hanger, hslot, &pktbuf, FALSE);
			if (!pktbuf) {
				_dhd_wlfc_hanger_free_pkt(wlfc, hslot,
					WLFC_HANGER_PKT_STATE_TXSTATUS, -1);
				goto cont;
			} else {
				wlfc_hanger_t* h = (wlfc_hanger_t*)wlfc->hanger;
				if (h->items[hslot].state == WLFC_HANGER_ITEM_STATE_FLUSHED) {
					status_flag = WLFC_CTL_PKTFLAG_DISCARD;
					remove_from_hanger = 1;
				}
			}
		}

		if ((ret != BCME_OK) || !pktbuf) {
			goto cont;
		}

		bcm_pkt_validate_chk(pktbuf);

		/* set fifo_id to correct value because not all FW does that */
		fifo_id = DHD_PKTTAG_FIFO(PKTTAG(pktbuf));

		entry = _dhd_wlfc_find_table_entry(wlfc, pktbuf);

		if (!remove_from_hanger) {
			/* this packet was suppressed */
			if (!entry->suppressed || (entry->generation != gen)) {
				if (!entry->suppressed) {
					entry->suppr_transit_count = entry->transit_count;
					if (p_mac) {
						*p_mac = entry;
					}
				} else {
					DHD_ERROR(("gen(%d), entry->generation(%d)\n",
						gen, entry->generation));
				}
				entry->suppressed = TRUE;

			}
			entry->generation = gen;
		}

#ifdef PROP_TXSTATUS_DEBUG
		if (!WLFC_GET_AFQ(dhd->wlfc_mode))
		{
			uint32 new_t = OSL_SYSUPTIME();
			uint32 old_t;
			uint32 delta;
			old_t = ((wlfc_hanger_t*)(wlfc->hanger))->items[hslot].push_time;


			wlfc->stats.latency_sample_count++;
			if (new_t > old_t)
				delta = new_t - old_t;
			else
				delta = 0xffffffff + new_t - old_t;
			wlfc->stats.total_status_latency += delta;
			wlfc->stats.latency_most_recent = delta;

			wlfc->stats.deltas[wlfc->stats.idx_delta++] = delta;
			if (wlfc->stats.idx_delta == sizeof(wlfc->stats.deltas)/sizeof(uint32))
				wlfc->stats.idx_delta = 0;
		}
#endif /* PROP_TXSTATUS_DEBUG */

		/* pick up the implicit credit from this packet */
		if (DHD_PKTTAG_CREDITCHECK(PKTTAG(pktbuf))) {
			_dhd_wlfc_return_implied_credit(wlfc, pktbuf);
		} else {
			/*
			if this packet did not count against FIFO credit, it must have
			taken a requested_credit from the destination entry (for pspoll etc.)
			*/
			if (!DHD_PKTTAG_ONETIMEPKTRQST(PKTTAG(pktbuf))) {
				entry->requested_credit++;
#if defined(DHD_WLFC_THREAD)
				_dhd_wlfc_thread_wakeup(dhd);
#endif /* DHD_WLFC_THREAD */
			}
#ifdef PROP_TXSTATUS_DEBUG
			entry->dstncredit_acks++;
#endif
		}

		if ((status_flag == WLFC_CTL_PKTFLAG_D11SUPPRESS) ||
			(status_flag == WLFC_CTL_PKTFLAG_WLSUPPRESS)) {
			/* save generation bit inside packet */
			WL_TXSTATUS_SET_GENERATION(DHD_PKTTAG_H2DTAG(PKTTAG(pktbuf)), gen);

			if (WLFC_GET_REUSESEQ(dhd->wlfc_mode)) {
				WL_SEQ_SET_REUSE(DHD_PKTTAG_H2DSEQ(PKTTAG(pktbuf)), seq_fromfw);
				WL_SEQ_SET_NUM(DHD_PKTTAG_H2DSEQ(PKTTAG(pktbuf)), seq_num);
			}

			ret = _dhd_wlfc_enque_suppressed(wlfc, fifo_id, pktbuf);
			if (ret != BCME_OK) {
				/* delay q is full, drop this packet */
				DHD_WLFC_QMON_COMPLETE(entry);
				_dhd_wlfc_prec_drop(dhd, (fifo_id << 1) + 1, pktbuf, FALSE);
			} else {
				if (!WLFC_GET_AFQ(dhd->wlfc_mode)) {
					/* Mark suppressed to avoid a double free
					during wlfc cleanup
					*/
					_dhd_wlfc_hanger_mark_suppressed(wlfc->hanger, hslot, gen);
				}
			}
		} else {

			DHD_WLFC_QMON_COMPLETE(entry);

			if (!WLFC_GET_AFQ(dhd->wlfc_mode)) {
				_dhd_wlfc_hanger_free_pkt(wlfc, hslot,
					WLFC_HANGER_PKT_STATE_TXSTATUS, TRUE);
			} else {
				dhd_txcomplete(dhd, pktbuf, TRUE);
				wlfc->pkt_cnt_in_drv[DHD_PKTTAG_IF(PKTTAG(pktbuf))]
					[DHD_PKTTAG_FIFO(PKTTAG(pktbuf))]--;
				wlfc->stats.pktout++;
				/* free the packet */
				PKTFREE(wlfc->osh, pktbuf, TRUE);
			}
		}
		/* pkt back from firmware side */
		if (entry->transit_count)
			entry->transit_count--;
		if (entry->suppr_transit_count) {
			entry->suppr_transit_count--;
			if (entry->suppressed &&
				(!entry->onbus_pkts_count) &&
				(!entry->suppr_transit_count))
				entry->suppressed = FALSE;
		}

cont:
		hcnt = (hcnt + 1) & WL_TXSTATUS_FREERUNCTR_MASK;
		if (!WLFC_GET_AFQ(dhd->wlfc_mode)) {
			hslot = (hslot + 1) & WL_TXSTATUS_HSLOT_MASK;
		}

		if (WLFC_GET_REUSESEQ(dhd->wlfc_mode) && seq_fromfw) {
			seq_num = (seq_num + 1) & WL_SEQ_NUM_MASK;
		}

		count++;
	}

	return BCME_OK;
} /* _dhd_wlfc_compressed_txstatus_update */

/**
 * Called when eg host receives a 'WLFC_CTL_TYPE_FIFO_CREDITBACK' event from the dongle.
 *    @param[in] credits caller supplied credit that will be added to the host credit.
 */
static int
_dhd_wlfc_fifocreditback_indicate(dhd_pub_t *dhd, uint8* credits)
{
	int i;
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)dhd->wlfc_state;
	for (i = 0; i < WLFC_CTL_VALUE_LEN_FIFO_CREDITBACK; i++) {
#ifdef PROP_TXSTATUS_DEBUG
		wlfc->stats.fifo_credits_back[i] += credits[i];
#endif

		/* update FIFO credits */
		if (dhd->proptxstatus_mode == WLFC_FCMODE_EXPLICIT_CREDIT)
		{
			int lender; /* Note that borrower is i */

			/* Return credits to highest priority lender first */
			for (lender = AC_COUNT; (lender >= 0) && (credits[i] > 0); lender--) {
				if (wlfc->credits_borrowed[i][lender] > 0) {
					if (credits[i] >= wlfc->credits_borrowed[i][lender]) {
						credits[i] -=
							(uint8)wlfc->credits_borrowed[i][lender];
						wlfc->FIFO_credit[lender] +=
						    wlfc->credits_borrowed[i][lender];
						wlfc->credits_borrowed[i][lender] = 0;
					} else {
						wlfc->credits_borrowed[i][lender] -= credits[i];
						wlfc->FIFO_credit[lender] += credits[i];
						credits[i] = 0;
					}
				}
			}

			/* If we have more credits left over, these must belong to the AC */
			if (credits[i] > 0) {
				wlfc->FIFO_credit[i] += credits[i];
			}

			if (wlfc->FIFO_credit[i] > wlfc->Init_FIFO_credit[i]) {
				wlfc->FIFO_credit[i] = wlfc->Init_FIFO_credit[i];
			}
		}
	}

#if defined(DHD_WLFC_THREAD)
	_dhd_wlfc_thread_wakeup(dhd);
#endif /* defined(DHD_WLFC_THREAD) */

	return BCME_OK;
} /* _dhd_wlfc_fifocreditback_indicate */

#ifndef BCMDBUS
/** !BCMDBUS specific function */
static void
_dhd_wlfc_suppress_txq(dhd_pub_t *dhd, f_processpkt_t fn, void *arg)
{
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)dhd->wlfc_state;
	wlfc_mac_descriptor_t* entry;
	int prec;
	void *pkt = NULL, *head = NULL, *tail = NULL;
	struct pktq *txq = (struct pktq *)dhd_bus_txq(dhd->bus);
	uint8	results[WLFC_CTL_VALUE_LEN_TXSTATUS+WLFC_CTL_VALUE_LEN_SEQ];
	uint8 credits[WLFC_CTL_VALUE_LEN_FIFO_CREDITBACK] = {0};
	uint32 htod = 0;
	uint16 htodseq = 0;
	bool bCreditUpdate = FALSE;

	dhd_os_sdlock_txq(dhd);
	for (prec = 0; prec < txq->num_prec; prec++) {
		while ((pkt = _dhd_wlfc_pktq_pdeq_with_fn(txq, prec, fn, arg))) {
			if (!head) {
				head = pkt;
			}
			if (tail) {
				PKTSETLINK(tail, pkt);
			}
			tail = pkt;
		}
	}
	dhd_os_sdunlock_txq(dhd);

	while ((pkt = head)) {
		head = PKTLINK(pkt);
		PKTSETLINK(pkt, NULL);

		entry = _dhd_wlfc_find_table_entry(wlfc, pkt);
		if (!entry) {
			PKTFREE(dhd->osh, pkt, TRUE);
			continue;
		}
		if (entry->onbus_pkts_count > 0) {
			entry->onbus_pkts_count--;
		}
		if (entry->suppressed &&
				(!entry->onbus_pkts_count) &&
				(!entry->suppr_transit_count)) {
			entry->suppressed = FALSE;
		}
		/* fake a suppression txstatus */
		htod = DHD_PKTTAG_H2DTAG(PKTTAG(pkt));
		WL_TXSTATUS_SET_FLAGS(htod, WLFC_CTL_PKTFLAG_WLSUPPRESS);
		WL_TXSTATUS_SET_GENERATION(htod, entry->generation);
		htod = htol32(htod);
		memcpy(results, &htod, WLFC_CTL_VALUE_LEN_TXSTATUS);
		if (WLFC_GET_REUSESEQ(dhd->wlfc_mode)) {
			htodseq = DHD_PKTTAG_H2DSEQ(PKTTAG(pkt));
			if (IS_WL_TO_REUSE_SEQ(htodseq)) {
				SET_WL_HAS_ASSIGNED_SEQ(htodseq);
				RESET_WL_TO_REUSE_SEQ(htodseq);
			}
			htodseq = htol16(htodseq);
			memcpy(results + WLFC_CTL_VALUE_LEN_TXSTATUS, &htodseq,
				WLFC_CTL_VALUE_LEN_SEQ);
		}
		if (WLFC_GET_AFQ(dhd->wlfc_mode)) {
			_dhd_wlfc_enque_afq(wlfc, pkt);
		}
		_dhd_wlfc_compressed_txstatus_update(dhd, results, 1, NULL);

		/* fake a fifo credit back */
		if (DHD_PKTTAG_CREDITCHECK(PKTTAG(pkt))) {
			credits[DHD_PKTTAG_FIFO(PKTTAG(pkt))]++;
			bCreditUpdate = TRUE;
		}
	}

	if (bCreditUpdate) {
		_dhd_wlfc_fifocreditback_indicate(dhd, credits);
	}
} /* _dhd_wlfc_suppress_txq */
#endif /* !BCMDBUS */

static int
_dhd_wlfc_dbg_senum_check(dhd_pub_t *dhd, uint8 *value)
{
	uint32 timestamp;

	(void)dhd;

	bcopy(&value[2], &timestamp, sizeof(uint32));
	timestamp = ltoh32(timestamp);
	DHD_INFO(("RXPKT: SEQ: %d, timestamp %d\n", value[1], timestamp));
	return BCME_OK;
}

static int
_dhd_wlfc_rssi_indicate(dhd_pub_t *dhd, uint8* rssi)
{
	(void)dhd;
	(void)rssi;
	return BCME_OK;
}

static void
_dhd_wlfc_add_requested_entry(athost_wl_status_info_t* wlfc, wlfc_mac_descriptor_t* entry)
{
	int i;

	if (!wlfc || !entry) {
		return;
	}

	for (i = 0; i < wlfc->requested_entry_count; i++) {
		if (entry == wlfc->requested_entry[i]) {
			break;
		}
	}

	if (i == wlfc->requested_entry_count) {
		/* no match entry found */
		ASSERT(wlfc->requested_entry_count <= (WLFC_MAC_DESC_TABLE_SIZE-1));
		wlfc->requested_entry[wlfc->requested_entry_count++] = entry;
	}
}

/** called on eg receiving 'mac open' event from the dongle. */
static void
_dhd_wlfc_remove_requested_entry(athost_wl_status_info_t* wlfc, wlfc_mac_descriptor_t* entry)
{
	int i;

	if (!wlfc || !entry) {
		return;
	}

	for (i = 0; i < wlfc->requested_entry_count; i++) {
		if (entry == wlfc->requested_entry[i]) {
			break;
		}
	}

	if (i < wlfc->requested_entry_count) {
		/* found */
		ASSERT(wlfc->requested_entry_count > 0);
		wlfc->requested_entry_count--;
		if (i != wlfc->requested_entry_count) {
			wlfc->requested_entry[i] =
				wlfc->requested_entry[wlfc->requested_entry_count];
		}
		wlfc->requested_entry[wlfc->requested_entry_count] = NULL;
	}
}

/** called on eg receiving a WLFC_CTL_TYPE_MACDESC_ADD TLV from the dongle */
static int
_dhd_wlfc_mac_table_update(dhd_pub_t *dhd, uint8* value, uint8 type)
{
	int rc;
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)dhd->wlfc_state;
	wlfc_mac_descriptor_t* table;
	uint8 existing_index;
	uint8 table_index;
	uint8 ifid;
	uint8* ea;

	WLFC_DBGMESG(("%s(), mac [%02x:%02x:%02x:%02x:%02x:%02x],%s,idx:%d,id:0x%02x\n",
		__FUNCTION__, value[2], value[3], value[4], value[5], value[6], value[7],
		((type == WLFC_CTL_TYPE_MACDESC_ADD) ? "ADD":"DEL"),
		WLFC_MAC_DESC_GET_LOOKUP_INDEX(value[0]), value[0]));

	table = wlfc->destination_entries.nodes;
	table_index = WLFC_MAC_DESC_GET_LOOKUP_INDEX(value[0]);
	ifid = value[1];
	ea = &value[2];

	_dhd_wlfc_remove_requested_entry(wlfc, &table[table_index]);
	if (type == WLFC_CTL_TYPE_MACDESC_ADD) {
		existing_index = _dhd_wlfc_find_mac_desc_id_from_mac(dhd, &value[2]);
		if ((existing_index != WLFC_MAC_DESC_ID_INVALID) &&
			(existing_index != table_index) && table[existing_index].occupied) {
			/*
			there is an existing different entry, free the old one
			and move it to new index if necessary.
			*/
			rc = _dhd_wlfc_mac_entry_update(wlfc, &table[existing_index],
				eWLFC_MAC_ENTRY_ACTION_DEL, table[existing_index].interface_id,
				table[existing_index].iftype, NULL, _dhd_wlfc_entrypkt_fn,
				&table[existing_index]);
		}

		if (!table[table_index].occupied) {
			/* this new MAC entry does not exist, create one */
			table[table_index].mac_handle = value[0];
			rc = _dhd_wlfc_mac_entry_update(wlfc, &table[table_index],
				eWLFC_MAC_ENTRY_ACTION_ADD, ifid,
				wlfc->destination_entries.interfaces[ifid].iftype,
				ea, NULL, NULL);
		} else {
			/* the space should have been empty, but it's not */
			wlfc->stats.mac_update_failed++;
		}
	}

	if (type == WLFC_CTL_TYPE_MACDESC_DEL) {
		if (table[table_index].occupied) {
				rc = _dhd_wlfc_mac_entry_update(wlfc, &table[table_index],
					eWLFC_MAC_ENTRY_ACTION_DEL, ifid,
					wlfc->destination_entries.interfaces[ifid].iftype,
					ea, _dhd_wlfc_entrypkt_fn, &table[table_index]);
		} else {
			/* the space should have been occupied, but it's not */
			wlfc->stats.mac_update_failed++;
		}
	}
	BCM_REFERENCE(rc);
	return BCME_OK;
} /* _dhd_wlfc_mac_table_update */

/** Called on a 'mac open' or 'mac close' event indicated by the dongle */
static int
_dhd_wlfc_psmode_update(dhd_pub_t *dhd, uint8* value, uint8 type)
{
	/* Handle PS on/off indication */
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)dhd->wlfc_state;
	wlfc_mac_descriptor_t* table;
	wlfc_mac_descriptor_t* desc; /* a table maps from mac handle to mac descriptor */
	uint8 mac_handle = value[0];
	int i;

	table = wlfc->destination_entries.nodes;
	desc = &table[WLFC_MAC_DESC_GET_LOOKUP_INDEX(mac_handle)];
	if (desc->occupied) {
		if (type == WLFC_CTL_TYPE_MAC_OPEN) {
			desc->state = WLFC_STATE_OPEN;
			desc->ac_bitmap = 0xff;
			DHD_WLFC_CTRINC_MAC_OPEN(desc);
			desc->requested_credit = 0;
			desc->requested_packet = 0;
			_dhd_wlfc_remove_requested_entry(wlfc, desc);
		} else {
			desc->state = WLFC_STATE_CLOSE;
			DHD_WLFC_CTRINC_MAC_CLOSE(desc);
			/* Indicate to firmware if there is any traffic pending. */
			for (i = 0; i < AC_COUNT; i++) {
				_dhd_wlfc_traffic_pending_check(wlfc, desc, i);
			}
		}
	} else {
		wlfc->stats.psmode_update_failed++;
	}

	return BCME_OK;
} /* _dhd_wlfc_psmode_update */

/** called upon receiving 'interface open' or 'interface close' event from the dongle */
static int
_dhd_wlfc_interface_update(dhd_pub_t *dhd, uint8* value, uint8 type)
{
	/* Handle PS on/off indication */
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)dhd->wlfc_state;
	wlfc_mac_descriptor_t* table;
	uint8 if_id = value[0];

	if (if_id < WLFC_MAX_IFNUM) {
		table = wlfc->destination_entries.interfaces;
		if (table[if_id].occupied) {
			if (type == WLFC_CTL_TYPE_INTERFACE_OPEN) {
				table[if_id].state = WLFC_STATE_OPEN;
				/* WLFC_DBGMESG(("INTERFACE[%d] OPEN\n", if_id)); */
			} else {
				table[if_id].state = WLFC_STATE_CLOSE;
				/* WLFC_DBGMESG(("INTERFACE[%d] CLOSE\n", if_id)); */
			}
			return BCME_OK;
		}
	}
	wlfc->stats.interface_update_failed++;

	return BCME_OK;
}

/** Called on receiving a WLFC_CTL_TYPE_MAC_REQUEST_CREDIT TLV from the dongle */
static int
_dhd_wlfc_credit_request(dhd_pub_t *dhd, uint8* value)
{
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)dhd->wlfc_state;
	wlfc_mac_descriptor_t* table;
	wlfc_mac_descriptor_t* desc;
	uint8 mac_handle;
	uint8 credit;

	table = wlfc->destination_entries.nodes;
	mac_handle = value[1];
	credit = value[0];

	desc = &table[WLFC_MAC_DESC_GET_LOOKUP_INDEX(mac_handle)];
	if (desc->occupied) {
		desc->requested_credit = credit;

		desc->ac_bitmap = value[2] & (~(1<<AC_COUNT));
		_dhd_wlfc_add_requested_entry(wlfc, desc);
#if defined(DHD_WLFC_THREAD)
		if (credit) {
			_dhd_wlfc_thread_wakeup(dhd);
		}
#endif /* DHD_WLFC_THREAD */
	} else {
		wlfc->stats.credit_request_failed++;
	}

	return BCME_OK;
}

/** Called on receiving a WLFC_CTL_TYPE_MAC_REQUEST_PACKET TLV from the dongle */
static int
_dhd_wlfc_packet_request(dhd_pub_t *dhd, uint8* value)
{
	athost_wl_status_info_t* wlfc = (athost_wl_status_info_t*)dhd->wlfc_state;
	wlfc_mac_descriptor_t* table;
	wlfc_mac_descriptor_t* desc;
	uint8 mac_handle;
	uint8 packet_count;

	table = wlfc->destination_entries.nodes;
	mac_handle = value[1];
	packet_count = value[0];

	desc = &table[WLFC_MAC_DESC_GET_LOOKUP_INDEX(mac_handle)];
	if (desc->occupied) {
		desc->requested_packet = packet_count;

		desc->ac_bitmap = value[2] & (~(1<<AC_COUNT));
		_dhd_wlfc_add_requested_entry(wlfc, desc);
#if defined(DHD_WLFC_THREAD)
		if (packet_count) {
			_dhd_wlfc_thread_wakeup(dhd);
		}
#endif /* DHD_WLFC_THREAD */
	} else {
		wlfc->stats.packet_request_failed++;
	}

	return BCME_OK;
}

/** Called when host receives a WLFC_CTL_TYPE_HOST_REORDER_RXPKTS TLV from the dongle */
static void
_dhd_wlfc_reorderinfo_indicate(uint8 *val, uint8 len, uchar *info_buf, uint *info_len)
{
	if (info_len) {
		/* Check copy length to avoid buffer overrun. In case of length exceeding
		*  WLHOST_REORDERDATA_TOTLEN, return failure instead sending incomplete result
		*  of length WLHOST_REORDERDATA_TOTLEN
		*/
		if ((info_buf) && (len <= WLHOST_REORDERDATA_TOTLEN)) {
			bcopy(val, info_buf, len);
			*info_len = len;
		} else {
			*info_len = 0;
		}
	}
}

/*
 * public functions
 */

bool dhd_wlfc_is_supported(dhd_pub_t *dhd)
{
	bool rc = TRUE;

	if (dhd == NULL) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return FALSE;
	}

	dhd_os_wlfc_block(dhd);

	if (!dhd->wlfc_state || (dhd->proptxstatus_mode == WLFC_FCMODE_NONE)) {
		rc =  FALSE;
	}

	dhd_os_wlfc_unblock(dhd);

	return rc;
}

int dhd_wlfc_enable(dhd_pub_t *dhd)
{
	int i, rc = BCME_OK;
	athost_wl_status_info_t* wlfc;

	if (dhd == NULL) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);

	if (!dhd->wlfc_enabled || dhd->wlfc_state) {
		rc = BCME_OK;
		goto exit;
	}

	/* allocate space to track txstatus propagated from firmware */
	dhd->wlfc_state = DHD_OS_PREALLOC(dhd, DHD_PREALLOC_DHD_WLFC_INFO,
		sizeof(athost_wl_status_info_t));
	if (dhd->wlfc_state == NULL) {
		rc = BCME_NOMEM;
		goto exit;
	}

	/* initialize state space */
	wlfc = (athost_wl_status_info_t*)dhd->wlfc_state;
	memset(wlfc, 0, sizeof(athost_wl_status_info_t));

	/* remember osh & dhdp */
	wlfc->osh = dhd->osh;
	wlfc->dhdp = dhd;

	if (!WLFC_GET_AFQ(dhd->wlfc_mode)) {
		wlfc->hanger = _dhd_wlfc_hanger_create(dhd, WLFC_HANGER_MAXITEMS);
		if (wlfc->hanger == NULL) {
			DHD_OS_PREFREE(dhd, dhd->wlfc_state,
				sizeof(athost_wl_status_info_t));
			dhd->wlfc_state = NULL;
			rc = BCME_NOMEM;
			goto exit;
		}
	}

	dhd->proptxstatus_mode = WLFC_FCMODE_EXPLICIT_CREDIT;
	/* default to check rx pkt */
	dhd->wlfc_rxpkt_chk = TRUE;
	if (dhd->op_mode & DHD_FLAG_IBSS_MODE) {
		dhd->wlfc_rxpkt_chk = FALSE;
	}

	/* initialize all interfaces to accept traffic */
	for (i = 0; i < WLFC_MAX_IFNUM; i++) {
		wlfc->hostif_flow_state[i] = OFF;
	}

	_dhd_wlfc_mac_entry_update(wlfc, &wlfc->destination_entries.other,
		eWLFC_MAC_ENTRY_ACTION_ADD, 0xff, 0, NULL, NULL, NULL);

	wlfc->allow_credit_borrow = 0;
	wlfc->single_ac = 0;
	wlfc->single_ac_timestamp = 0;


exit:
	DHD_ERROR(("%s: ret=%d\n", __FUNCTION__, rc));
	dhd_os_wlfc_unblock(dhd);

	return rc;
} /* dhd_wlfc_enable */

#ifdef SUPPORT_P2P_GO_PS

/**
 * Called when the host platform enters a lower power mode, eg right before a system hibernate.
 * SUPPORT_P2P_GO_PS specific function.
 */
int
dhd_wlfc_suspend(dhd_pub_t *dhd)
{
	uint32 tlv = 0;

	DHD_TRACE(("%s: masking wlfc events\n", __FUNCTION__));
	if (!dhd->wlfc_enabled)
		return -1;

	if (!dhd_wl_ioctl_get_intiovar(dhd, "tlv", &tlv, WLC_GET_VAR, FALSE, 0))
		return -1;
	if ((tlv & (WLFC_FLAGS_RSSI_SIGNALS | WLFC_FLAGS_XONXOFF_SIGNALS)) == 0)
		return 0;
	tlv &= ~(WLFC_FLAGS_RSSI_SIGNALS | WLFC_FLAGS_XONXOFF_SIGNALS);
	if (!dhd_wl_ioctl_set_intiovar(dhd, "tlv", tlv, WLC_SET_VAR, TRUE, 0))
		return -1;

	return 0;
}

/**
 * Called when the host platform resumes from a power management operation, eg resume after a
 * system hibernate. SUPPORT_P2P_GO_PS specific function.
 */
int
dhd_wlfc_resume(dhd_pub_t *dhd)
{
	uint32 tlv = 0;

	DHD_TRACE(("%s: unmasking wlfc events\n", __FUNCTION__));
	if (!dhd->wlfc_enabled)
		return -1;

	if (!dhd_wl_ioctl_get_intiovar(dhd, "tlv", &tlv, WLC_GET_VAR, FALSE, 0))
		return -1;
	if ((tlv & (WLFC_FLAGS_RSSI_SIGNALS | WLFC_FLAGS_XONXOFF_SIGNALS)) ==
		(WLFC_FLAGS_RSSI_SIGNALS | WLFC_FLAGS_XONXOFF_SIGNALS))
		return 0;
	tlv |= (WLFC_FLAGS_RSSI_SIGNALS | WLFC_FLAGS_XONXOFF_SIGNALS);
	if (!dhd_wl_ioctl_set_intiovar(dhd, "tlv", tlv, WLC_SET_VAR, TRUE, 0))
		return -1;

	return 0;
}

#endif /* SUPPORT_P2P_GO_PS */

/** A flow control header was received from firmware, containing one or more TLVs */
int
dhd_wlfc_parse_header_info(dhd_pub_t *dhd, void* pktbuf, int tlv_hdr_len, uchar *reorder_info_buf,
	uint *reorder_info_len)
{
	uint8 type, len;
	uint8* value;
	uint8* tmpbuf;
	uint16 remainder = (uint16)tlv_hdr_len;
	uint16 processed = 0;
	athost_wl_status_info_t* wlfc = NULL;
	void* entry;

	if ((dhd == NULL) || (pktbuf == NULL)) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);

	if (dhd->proptxstatus_mode != WLFC_ONLY_AMPDU_HOSTREORDER) {
		if (!dhd->wlfc_state || (dhd->proptxstatus_mode == WLFC_FCMODE_NONE)) {
			dhd_os_wlfc_unblock(dhd);
			return WLFC_UNSUPPORTED;
		}
		wlfc = (athost_wl_status_info_t*)dhd->wlfc_state;
	}

	tmpbuf = (uint8*)PKTDATA(dhd->osh, pktbuf);

	if (remainder) {
		while ((processed < (WLFC_MAX_PENDING_DATALEN * 2)) && (remainder > 0)) {
			type = tmpbuf[processed];
			if (type == WLFC_CTL_TYPE_FILLER) {
				remainder -= 1;
				processed += 1;
				continue;
			}

			len  = tmpbuf[processed + 1];
			value = &tmpbuf[processed + 2];

			if (remainder < (2 + len))
				break;

			remainder -= 2 + len;
			processed += 2 + len;
			entry = NULL;

			DHD_INFO(("%s():%d type %d remainder %d processed %d\n",
				__FUNCTION__, __LINE__, type, remainder, processed));

			if (type == WLFC_CTL_TYPE_HOST_REORDER_RXPKTS)
				_dhd_wlfc_reorderinfo_indicate(value, len, reorder_info_buf,
					reorder_info_len);

			if (wlfc == NULL) {
				ASSERT(dhd->proptxstatus_mode == WLFC_ONLY_AMPDU_HOSTREORDER);

				if (type != WLFC_CTL_TYPE_HOST_REORDER_RXPKTS &&
					type != WLFC_CTL_TYPE_TRANS_ID)
					DHD_INFO(("%s():%d dhd->wlfc_state is NULL yet!"
					" type %d remainder %d processed %d\n",
					__FUNCTION__, __LINE__, type, remainder, processed));
				continue;
			}

			if (type == WLFC_CTL_TYPE_TXSTATUS) {
				_dhd_wlfc_compressed_txstatus_update(dhd, value, 1, &entry);
			} else if (type == WLFC_CTL_TYPE_COMP_TXSTATUS) {
				uint8 compcnt_offset = WLFC_CTL_VALUE_LEN_TXSTATUS;

				if (WLFC_GET_REUSESEQ(dhd->wlfc_mode)) {
					compcnt_offset += WLFC_CTL_VALUE_LEN_SEQ;
				}
				_dhd_wlfc_compressed_txstatus_update(dhd, value,
					value[compcnt_offset], &entry);
			} else if (type == WLFC_CTL_TYPE_FIFO_CREDITBACK) {
				_dhd_wlfc_fifocreditback_indicate(dhd, value);
			} else if (type == WLFC_CTL_TYPE_RSSI) {
				_dhd_wlfc_rssi_indicate(dhd, value);
			} else if (type == WLFC_CTL_TYPE_MAC_REQUEST_CREDIT) {
				_dhd_wlfc_credit_request(dhd, value);
			} else if (type == WLFC_CTL_TYPE_MAC_REQUEST_PACKET) {
				_dhd_wlfc_packet_request(dhd, value);
			} else if ((type == WLFC_CTL_TYPE_MAC_OPEN) ||
				(type == WLFC_CTL_TYPE_MAC_CLOSE)) {
				_dhd_wlfc_psmode_update(dhd, value, type);
			} else if ((type == WLFC_CTL_TYPE_MACDESC_ADD) ||
				(type == WLFC_CTL_TYPE_MACDESC_DEL)) {
				_dhd_wlfc_mac_table_update(dhd, value, type);
			} else if (type == WLFC_CTL_TYPE_TRANS_ID) {
				_dhd_wlfc_dbg_senum_check(dhd, value);
			} else if ((type == WLFC_CTL_TYPE_INTERFACE_OPEN) ||
				(type == WLFC_CTL_TYPE_INTERFACE_CLOSE)) {
				_dhd_wlfc_interface_update(dhd, value, type);
			}

#ifndef BCMDBUS
			if (entry && WLFC_GET_REORDERSUPP(dhd->wlfc_mode)) {
				/* suppress all packets for this mac entry from bus->txq */
				_dhd_wlfc_suppress_txq(dhd, _dhd_wlfc_entrypkt_fn, entry);
			}
#endif /* !BCMDBUS */
		} /* while */

		if (remainder != 0 && wlfc) {
			/* trouble..., something is not right */
			wlfc->stats.tlv_parse_failed++;
		}
	} /* if */

	if (wlfc)
		wlfc->stats.dhd_hdrpulls++;

	dhd_os_wlfc_unblock(dhd);
	return BCME_OK;
}

KERNEL_THREAD_RETURN_TYPE
dhd_wlfc_transfer_packets(void *data)
{
	dhd_pub_t *dhdp = (dhd_pub_t *)data;
	int ac, single_ac = 0, rc = BCME_OK;
	dhd_wlfc_commit_info_t  commit_info;
	athost_wl_status_info_t* ctx;
	int bus_retry_count = 0;
	int pkt_send = 0;
	int pkt_send_per_ac = 0;

	uint8 tx_map = 0; /* packets (send + in queue), Bitmask for 4 ACs + BC/MC */
	uint8 rx_map = 0; /* received packets, Bitmask for 4 ACs + BC/MC */
	uint8 packets_map = 0; /* packets in queue, Bitmask for 4 ACs + BC/MC */
	bool no_credit = FALSE;

	int lender;

#if defined(DHD_WLFC_THREAD)
	/* wait till someone wakeup me up, will change it at running time */
	int wait_msec = msecs_to_jiffies(0xFFFFFFFF);
#endif /* defined(DHD_WLFC_THREAD) */

#if defined(DHD_WLFC_THREAD)
	while (1) {
		bus_retry_count = 0;
		pkt_send = 0;
		tx_map = 0;
		rx_map = 0;
		packets_map = 0;
		wait_msec = wait_event_interruptible_timeout(dhdp->wlfc_wqhead,
			dhdp->wlfc_thread_go, wait_msec);
		if (kthread_should_stop()) {
			break;
		}
		dhdp->wlfc_thread_go = FALSE;

		dhd_os_wlfc_block(dhdp);
#endif /* defined(DHD_WLFC_THREAD) */
		ctx = (athost_wl_status_info_t*)dhdp->wlfc_state;
#if defined(DHD_WLFC_THREAD)
		if (!ctx)
			goto exit;
#endif /* defined(DHD_WLFC_THREAD) */

	memset(&commit_info, 0, sizeof(commit_info));

	/*
	Commit packets for regular AC traffic. Higher priority first.
	First, use up FIFO credits available to each AC. Based on distribution
	and credits left, borrow from other ACs as applicable

	-NOTE:
	If the bus between the host and firmware is overwhelmed by the
	traffic from host, it is possible that higher priority traffic
	starves the lower priority queue. If that occurs often, we may
	have to employ weighted round-robin or ucode scheme to avoid
	low priority packet starvation.
	*/

	for (ac = AC_COUNT; ac >= 0; ac--) {
		if (dhdp->wlfc_rxpkt_chk) {
			/* check rx packet */
			uint32 curr_t = OSL_SYSUPTIME(), delta;

			delta = curr_t - ctx->rx_timestamp[ac];
			if (delta < WLFC_RX_DETECTION_THRESHOLD_MS) {
				rx_map |= (1 << ac);
			}
		}

		if (ctx->pkt_cnt_per_ac[ac] == 0) {
			continue;
		}

		tx_map |= (1 << ac);
		single_ac = ac + 1;
		pkt_send_per_ac = 0;
		while ((FALSE == dhdp->proptxstatus_txoff) &&
				(pkt_send_per_ac < WLFC_PACKET_BOUND)) {
			/* packets from delayQ with less priority are fresh and
			 * they'd need header and have no MAC entry
			 */
			no_credit = (ctx->FIFO_credit[ac] < 1);
			if (dhdp->proptxstatus_credit_ignore ||
				((ac == AC_COUNT) && !ctx->bcmc_credit_supported)) {
				no_credit = FALSE;
			}

			lender = -1;
#ifdef LIMIT_BORROW
			if (no_credit && (ac < AC_COUNT) && (tx_map >= rx_map) &&
				dhdp->wlfc_borrow_allowed) {
				/* try borrow from lower priority */
				lender = _dhd_wlfc_borrow_credit(ctx, ac - 1, ac, FALSE);
				if (lender != -1) {
					no_credit = FALSE;
				}
			}
#endif
			commit_info.needs_hdr = 1;
			commit_info.mac_entry = NULL;
			commit_info.p = _dhd_wlfc_deque_delayedq(ctx, ac,
				&(commit_info.ac_fifo_credit_spent),
				&(commit_info.needs_hdr),
				&(commit_info.mac_entry),
				no_credit);
			commit_info.pkt_type = (commit_info.needs_hdr) ? eWLFC_PKTTYPE_DELAYED :
				eWLFC_PKTTYPE_SUPPRESSED;

			if (commit_info.p == NULL) {
#ifdef LIMIT_BORROW
				if (lender != -1 && dhdp->wlfc_borrow_allowed) {
					_dhd_wlfc_return_credit(ctx, lender, ac);
				}
#endif
				break;
			}

			if (!dhdp->proptxstatus_credit_ignore && (lender == -1)) {
				ASSERT(ctx->FIFO_credit[ac] >= commit_info.ac_fifo_credit_spent);
			}
			/* here we can ensure have credit or no credit needed */
			rc = _dhd_wlfc_handle_packet_commit(ctx, ac, &commit_info,
				ctx->fcommit, ctx->commit_ctx);

			/* Bus commits may fail (e.g. flow control); abort after retries */
			if (rc == BCME_OK) {
				pkt_send++;
				pkt_send_per_ac++;
				if (commit_info.ac_fifo_credit_spent && (lender == -1)) {
					ctx->FIFO_credit[ac]--;
				}
#ifdef LIMIT_BORROW
				else if (!commit_info.ac_fifo_credit_spent && (lender != -1) &&
					dhdp->wlfc_borrow_allowed) {
					_dhd_wlfc_return_credit(ctx, lender, ac);
				}
#endif
			} else {
#ifdef LIMIT_BORROW
				if (lender != -1 && dhdp->wlfc_borrow_allowed) {
					_dhd_wlfc_return_credit(ctx, lender, ac);
				}
#endif
				bus_retry_count++;
				if (bus_retry_count >= BUS_RETRIES) {
					DHD_ERROR(("%s: bus error %d\n", __FUNCTION__, rc));
					goto exit;
				}
			}
		}

		if (ctx->pkt_cnt_per_ac[ac]) {
			packets_map |= (1 << ac);
		}
	}

	if ((tx_map == 0) || dhdp->proptxstatus_credit_ignore) {
		/* nothing send out or remain in queue */
		rc = BCME_OK;
		goto exit;
	}

	if (((tx_map & (tx_map - 1)) == 0) && (tx_map >= rx_map)) {
		/* only one tx ac exist and no higher rx ac */
		if ((single_ac == ctx->single_ac) && ctx->allow_credit_borrow) {
			ac = single_ac - 1;
		} else {
			uint32 delta;
			uint32 curr_t = OSL_SYSUPTIME();

			if (single_ac != ctx->single_ac) {
				/* new single ac traffic (first single ac or different single ac) */
				ctx->allow_credit_borrow = 0;
				ctx->single_ac_timestamp = curr_t;
				ctx->single_ac = (uint8)single_ac;
				rc = BCME_OK;
				goto exit;
			}
			/* same ac traffic, check if it lasts enough time */
			delta = curr_t - ctx->single_ac_timestamp;

			if (delta >= WLFC_BORROW_DEFER_PERIOD_MS) {
				/* wait enough time, can borrow now */
				ctx->allow_credit_borrow = 1;
				ac = single_ac - 1;
			} else {
				rc = BCME_OK;
				goto exit;
			}
		}
	} else {
		/* If we have multiple AC traffic, turn off borrowing, mark time and bail out */
		ctx->allow_credit_borrow = 0;
		ctx->single_ac_timestamp = 0;
		ctx->single_ac = 0;
		rc = BCME_OK;
		goto exit;
	}

	if (packets_map == 0) {
		/* nothing to send, skip borrow */
		rc = BCME_OK;
		goto exit;
	}

	/* At this point, borrow all credits only for ac */
	while (FALSE == dhdp->proptxstatus_txoff) {
#ifdef LIMIT_BORROW
		if (dhdp->wlfc_borrow_allowed) {
			if ((lender = _dhd_wlfc_borrow_credit(ctx, AC_COUNT, ac, TRUE)) == -1) {
				break;
			}
		}
		else
			break;
#endif
		commit_info.p = _dhd_wlfc_deque_delayedq(ctx, ac,
			&(commit_info.ac_fifo_credit_spent),
			&(commit_info.needs_hdr),
			&(commit_info.mac_entry),
			FALSE);
		if (commit_info.p == NULL) {
			/* before borrow only one ac exists and now this only ac is empty */
#ifdef LIMIT_BORROW
			_dhd_wlfc_return_credit(ctx, lender, ac);
#endif
			break;
		}

		commit_info.pkt_type = (commit_info.needs_hdr) ? eWLFC_PKTTYPE_DELAYED :
			eWLFC_PKTTYPE_SUPPRESSED;

		rc = _dhd_wlfc_handle_packet_commit(ctx, ac, &commit_info,
		     ctx->fcommit, ctx->commit_ctx);

		/* Bus commits may fail (e.g. flow control); abort after retries */
		if (rc == BCME_OK) {
			pkt_send++;
			if (commit_info.ac_fifo_credit_spent) {
#ifndef LIMIT_BORROW
				ctx->FIFO_credit[ac]--;
#endif
			} else {
#ifdef LIMIT_BORROW
				_dhd_wlfc_return_credit(ctx, lender, ac);
#endif
			}
		} else {
#ifdef LIMIT_BORROW
			_dhd_wlfc_return_credit(ctx, lender, ac);
#endif
			bus_retry_count++;
			if (bus_retry_count >= BUS_RETRIES) {
				DHD_ERROR(("%s: bus error %d\n", __FUNCTION__, rc));
				goto exit;
			}
		}
	}

	BCM_REFERENCE(pkt_send);

exit:
#if defined(DHD_WLFC_THREAD)
		dhd_os_wlfc_unblock(dhdp);
		if (ctx && ctx->pkt_cnt_in_psq && pkt_send) {
			wait_msec = msecs_to_jiffies(WLFC_THREAD_QUICK_RETRY_WAIT_MS);
		} else {
			wait_msec = msecs_to_jiffies(WLFC_THREAD_RETRY_WAIT_MS);
		}
	}
	return 0;
#else
	return rc;
#endif /* defined(DHD_WLFC_THREAD) */
}

/**
 * Enqueues a transmit packet in the next layer towards the dongle, eg the DBUS layer. Called by
 * eg dhd_sendpkt().
 *     @param[in] dhdp                  Pointer to public DHD structure
 *     @param[in] fcommit               Pointer to transmit function of next layer
 *     @param[in] commit_ctx            Opaque context used when calling next layer
 *     @param[in] pktbuf                Packet to send
 *     @param[in] need_toggle_host_if   If TRUE, resets flag ctx->toggle_host_if
 */
int
dhd_wlfc_commit_packets(dhd_pub_t *dhdp, f_commitpkt_t fcommit, void* commit_ctx, void *pktbuf,
	bool need_toggle_host_if)
{
	int rc = BCME_OK;
	athost_wl_status_info_t* ctx;

#if defined(DHD_WLFC_THREAD)
	if (!pktbuf)
		return BCME_OK;
#endif /* defined(DHD_WLFC_THREAD) */

	if ((dhdp == NULL) || (fcommit == NULL)) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhdp);

	if (!dhdp->wlfc_state || (dhdp->proptxstatus_mode == WLFC_FCMODE_NONE)) {
		if (pktbuf) {
			DHD_PKTTAG_WLFCPKT_SET(PKTTAG(pktbuf), 0);
		}
		rc =  WLFC_UNSUPPORTED;
		goto exit;
	}

	ctx = (athost_wl_status_info_t*)dhdp->wlfc_state;

#ifdef BCMDBUS
	if (!dhdp->up || (dhdp->busstate == DHD_BUS_DOWN)) {
		if (pktbuf) {
			PKTFREE(ctx->osh, pktbuf, TRUE);
			rc = BCME_OK;
		}
		goto exit;
	}
#endif /* BCMDBUS */

	if (dhdp->proptxstatus_module_ignore) {
		if (pktbuf) {
			uint32 htod = 0;
			WL_TXSTATUS_SET_FLAGS(htod, WLFC_PKTFLAG_PKTFROMHOST);
			_dhd_wlfc_pushheader(ctx, &pktbuf, FALSE, 0, 0, htod, 0, FALSE);
			if (fcommit(commit_ctx, pktbuf)) {
				/* free it if failed, otherwise do it in tx complete cb */
				PKTFREE(ctx->osh, pktbuf, TRUE);
			}
			rc = BCME_OK;
		}
		goto exit;
	}

	if (pktbuf) {
		int ac = DHD_PKTTAG_FIFO(PKTTAG(pktbuf));
		ASSERT(ac <= AC_COUNT);
		DHD_PKTTAG_WLFCPKT_SET(PKTTAG(pktbuf), 1);
		/* en-queue the packets to respective queue. */
		rc = _dhd_wlfc_enque_delayq(ctx, pktbuf, ac);
		if (rc) {
			_dhd_wlfc_prec_drop(ctx->dhdp, (ac << 1), pktbuf, FALSE);
		} else {
			ctx->stats.pktin++;
			ctx->pkt_cnt_in_drv[DHD_PKTTAG_IF(PKTTAG(pktbuf))][ac]++;
		}
	}

	if (!ctx->fcommit) {
		ctx->fcommit = fcommit;
	} else {
		ASSERT(ctx->fcommit == fcommit);
	}
	if (!ctx->commit_ctx) {
		ctx->commit_ctx = commit_ctx;
	} else {
		ASSERT(ctx->commit_ctx == commit_ctx);
	}

#if defined(DHD_WLFC_THREAD)
	_dhd_wlfc_thread_wakeup(dhdp);
#else
	dhd_wlfc_transfer_packets(dhdp);
#endif /* defined(DHD_WLFC_THREAD) */

exit:
	dhd_os_wlfc_unblock(dhdp);
	return rc;
} /* dhd_wlfc_commit_packets */

/**
 * Called when the (lower) DBUS layer indicates completion (succesfull or not) of a transmit packet
 */
int
dhd_wlfc_txcomplete(dhd_pub_t *dhd, void *txp, bool success)
{
	athost_wl_status_info_t* wlfc;
	wlfc_mac_descriptor_t *entry;
	void* pout = NULL;
	int rtn = BCME_OK;
	if ((dhd == NULL) || (txp == NULL)) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	bcm_pkt_validate_chk(txp);

	dhd_os_wlfc_block(dhd);

	if (!dhd->wlfc_state || (dhd->proptxstatus_mode == WLFC_FCMODE_NONE)) {
		rtn = WLFC_UNSUPPORTED;
		goto EXIT;
	}

	wlfc = (athost_wl_status_info_t*)dhd->wlfc_state;
	if (DHD_PKTTAG_SIGNALONLY(PKTTAG(txp))) {
#ifdef PROP_TXSTATUS_DEBUG
		wlfc->stats.signal_only_pkts_freed++;
#endif
		/* is this a signal-only packet? */
		_dhd_wlfc_pullheader(wlfc, txp);
		PKTFREE(wlfc->osh, txp, TRUE);
		goto EXIT;
	}

	entry = _dhd_wlfc_find_table_entry(wlfc, txp);
	ASSERT(entry);

	if (!success || dhd->proptxstatus_txstatus_ignore) {
		WLFC_DBGMESG(("At: %s():%d, bus_complete() failure for %p, htod_tag:0x%08x\n",
			__FUNCTION__, __LINE__, txp, DHD_PKTTAG_H2DTAG(PKTTAG(txp))));
		if (!WLFC_GET_AFQ(dhd->wlfc_mode)) {
			_dhd_wlfc_hanger_poppkt(wlfc->hanger, WL_TXSTATUS_GET_HSLOT(
				DHD_PKTTAG_H2DTAG(PKTTAG(txp))), &pout, TRUE);
			ASSERT(txp == pout);
		}

		/* indicate failure and free the packet */
		dhd_txcomplete(dhd, txp, success);

		/* return the credit, if necessary */
		_dhd_wlfc_return_implied_credit(wlfc, txp);

		if (entry->transit_count)
			entry->transit_count--;
		if (entry->suppr_transit_count)
			entry->suppr_transit_count--;
		wlfc->pkt_cnt_in_drv[DHD_PKTTAG_IF(PKTTAG(txp))][DHD_PKTTAG_FIFO(PKTTAG(txp))]--;
		wlfc->stats.pktout++;
		PKTFREE(wlfc->osh, txp, TRUE);
	} else {
		/* bus confirmed pkt went to firmware side */
		if (WLFC_GET_AFQ(dhd->wlfc_mode)) {
			_dhd_wlfc_enque_afq(wlfc, txp);
		} else {
			int hslot = WL_TXSTATUS_GET_HSLOT(DHD_PKTTAG_H2DTAG(PKTTAG(txp)));
			_dhd_wlfc_hanger_free_pkt(wlfc, hslot,
				WLFC_HANGER_PKT_STATE_BUSRETURNED, -1);
		}
	}

	ASSERT(entry->onbus_pkts_count > 0);
	if (entry->onbus_pkts_count > 0)
		entry->onbus_pkts_count--;
	if (entry->suppressed &&
		(!entry->onbus_pkts_count) &&
		(!entry->suppr_transit_count))
		entry->suppressed = FALSE;
EXIT:
	dhd_os_wlfc_unblock(dhd);
	return rtn;
} /* dhd_wlfc_txcomplete */

int
dhd_wlfc_init(dhd_pub_t *dhd)
{
	/* enable all signals & indicate host proptxstatus logic is active */
	uint32 tlv, mode, fw_caps;
	int ret = 0;

	if (dhd == NULL) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);
	if (dhd->wlfc_enabled) {
		DHD_ERROR(("%s():%d, Already enabled!\n", __FUNCTION__, __LINE__));
		dhd_os_wlfc_unblock(dhd);
		return BCME_OK;
	}
	dhd->wlfc_enabled = TRUE;
	dhd_os_wlfc_unblock(dhd);

	tlv = WLFC_FLAGS_RSSI_SIGNALS |
		WLFC_FLAGS_XONXOFF_SIGNALS |
		WLFC_FLAGS_CREDIT_STATUS_SIGNALS |
		WLFC_FLAGS_HOST_PROPTXSTATUS_ACTIVE |
		WLFC_FLAGS_HOST_RXRERODER_ACTIVE;


	/*
	try to enable/disable signaling by sending "tlv" iovar. if that fails,
	fallback to no flow control? Print a message for now.
	*/

	/* enable proptxtstatus signaling by default */
	if (!dhd_wl_ioctl_set_intiovar(dhd, "tlv", tlv, WLC_SET_VAR, TRUE, 0)) {
		/*
		Leaving the message for now, it should be removed after a while; once
		the tlv situation is stable.
		*/
		DHD_PRINT("dhd_wlfc_init(): successfully %s bdcv2 tlv signaling, %d\n",
			dhd->wlfc_enabled?"enabled":"disabled", tlv);
	}

	mode = 0;

	/* query caps */
	ret = dhd_wl_ioctl_get_intiovar(dhd, "wlfc_mode", &fw_caps, WLC_GET_VAR, FALSE, 0);

	if (!ret) {
		DHD_PRINT("%s: query wlfc_mode succeed, fw_caps=0x%x\n", __FUNCTION__, fw_caps);

		if (WLFC_IS_OLD_DEF(fw_caps)) {
#ifdef BCMDBUS
			mode = WLFC_MODE_HANGER;
#else
			/* enable proptxtstatus v2 by default */
			mode = WLFC_MODE_AFQ;
#endif /* BCMDBUS */
		} else {
			WLFC_SET_AFQ(mode, WLFC_GET_AFQ(fw_caps));
#ifdef BCMDBUS
			WLFC_SET_AFQ(mode, 0);
#endif /* BCMDBUS */
			WLFC_SET_REUSESEQ(mode, WLFC_GET_REUSESEQ(fw_caps));
			WLFC_SET_REORDERSUPP(mode, WLFC_GET_REORDERSUPP(fw_caps));
		}
		ret = dhd_wl_ioctl_set_intiovar(dhd, "wlfc_mode", mode, WLC_SET_VAR, TRUE, 0);
	}

	dhd_os_wlfc_block(dhd);

	dhd->wlfc_mode = 0;
	if (ret >= 0) {
		if (WLFC_IS_OLD_DEF(mode)) {
			WLFC_SET_AFQ(dhd->wlfc_mode, (mode == WLFC_MODE_AFQ));
		} else {
			dhd->wlfc_mode = mode;
		}
	}

	DHD_PRINT("dhd_wlfc_init(): wlfc_mode=0x%x, ret=%d\n", dhd->wlfc_mode, ret);
#ifdef LIMIT_BORROW
	dhd->wlfc_borrow_allowed = TRUE;
#endif
	dhd_os_wlfc_unblock(dhd);

	if (dhd->plat_init)
		dhd->plat_init((void *)dhd);

	return BCME_OK;
} /* dhd_wlfc_init */

/** AMPDU host reorder specific function */
int
dhd_wlfc_hostreorder_init(dhd_pub_t *dhd)
{
	/* enable only ampdu hostreorder here */
	uint32 tlv;

	if (dhd == NULL) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	DHD_TRACE(("%s():%d Enter\n", __FUNCTION__, __LINE__));

	tlv = WLFC_FLAGS_HOST_RXRERODER_ACTIVE;

	/* enable proptxtstatus signaling by default */
	if (dhd_wl_ioctl_set_intiovar(dhd, "tlv", tlv, WLC_SET_VAR, TRUE, 0)) {
		DHD_ERROR(("%s(): failed to enable/disable bdcv2 tlv signaling\n",
			__FUNCTION__));
	} else {
		/*
		Leaving the message for now, it should be removed after a while; once
		the tlv situation is stable.
		*/
		DHD_PRINT("%s(): successful bdcv2 tlv signaling, %d\n",
			__FUNCTION__, tlv);
	}

	dhd_os_wlfc_block(dhd);
	dhd->proptxstatus_mode = WLFC_ONLY_AMPDU_HOSTREORDER;
	dhd_os_wlfc_unblock(dhd);
	/* terence 20161229: enable ampdu_hostreorder if tlv enable hostreorder */
	dhd_conf_set_intiovar(dhd, WLC_SET_VAR, "ampdu_hostreorder", 1, 0, TRUE);

	return BCME_OK;
}

#ifdef DHD_LOAD_CHIPALIVE
int
dhd_chipalive_wlfc_init(dhd_pub_t *dhd)
{
	char iovbuf[14]; /* Room for "tlv" + '\0' + parameter */
	/* enable all signals & indicate host proptxstatus logic is active */
	uint32 tlv, mode, fw_caps;
	int ret = 0;

	/* This function should do query fw tlv only to sync for dhd host, don't change fw state */

	ret= dhd_conf_get_iovar(dhd, 0, WLC_GET_VAR, "tlv", (char *)&tlv, sizeof(tlv));
	if (ret || tlv==0) {
		DHD_PRINT("%s: proptx is not enabled in fw, ret=%d\n", __FUNCTION__, ret);
		return -1;
	} else {
		dhd_os_wlfc_block(dhd);
		dhd->wlfc_enabled = TRUE;
		dhd_os_wlfc_unblock(dhd);
		DHD_PRINT("%s: tlv = %d\n", __FUNCTION__, tlv);
	}

	/* query caps */
	ret = bcm_mkiovar("wlfc_mode", (char *)&mode, 4, iovbuf, sizeof(iovbuf));
	if (ret > 0) {
		ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, iovbuf, sizeof(iovbuf), FALSE, 0);
	}

	if (ret >= 0) {
		fw_caps = *((uint32 *)iovbuf);
		mode = 0;
		DHD_PRINT("%s: query wlfc_mode succeed, fw_caps=0x%x\n", __FUNCTION__, fw_caps);

		if (WLFC_IS_OLD_DEF(fw_caps)) {
			/* enable proptxtstatus v2 by default */
			mode = WLFC_MODE_AFQ;
		} else {
			WLFC_SET_AFQ(mode, WLFC_GET_AFQ(fw_caps));
			WLFC_SET_REUSESEQ(mode, WLFC_GET_REUSESEQ(fw_caps));
			WLFC_SET_REORDERSUPP(mode, WLFC_GET_REORDERSUPP(fw_caps));
		}
	}

	dhd_os_wlfc_block(dhd);

	dhd->wlfc_mode = 0;
	if (ret >= 0) { // this ret value is from wlfc_mode supported in fw or not
		if (WLFC_IS_OLD_DEF(mode)) {
			WLFC_SET_AFQ(dhd->wlfc_mode, (mode == WLFC_MODE_AFQ));
		} else {
			dhd->wlfc_mode = mode;
		}
	}
	DHD_PRINT("%s: wlfc_mode=0x%x, ret=%d\n", __FUNCTION__, dhd->wlfc_mode, ret);

	dhd_os_wlfc_unblock(dhd);

	if (dhd->plat_init)
		dhd->plat_init((void *)dhd);

	return BCME_OK;
}
#endif

int
dhd_wlfc_cleanup_txq(dhd_pub_t *dhd, f_processpkt_t fn, void *arg)
{
	if (dhd == NULL) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);

	if (!dhd->wlfc_state || (dhd->proptxstatus_mode == WLFC_FCMODE_NONE)) {
		dhd_os_wlfc_unblock(dhd);
		return WLFC_UNSUPPORTED;
	}

#ifndef BCMDBUS
	_dhd_wlfc_cleanup_txq(dhd, fn, arg);
#endif /* !BCMDBUS */

	dhd_os_wlfc_unblock(dhd);

	return BCME_OK;
}

/** release all packet resources */
int
dhd_wlfc_cleanup(dhd_pub_t *dhd, f_processpkt_t fn, void *arg)
{
	if (dhd == NULL) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);

	if (!dhd->wlfc_state || (dhd->proptxstatus_mode == WLFC_FCMODE_NONE)) {
		dhd_os_wlfc_unblock(dhd);
		return WLFC_UNSUPPORTED;
	}

	_dhd_wlfc_cleanup(dhd, fn, arg);

	dhd_os_wlfc_unblock(dhd);

	return BCME_OK;
}

int
dhd_wlfc_deinit(dhd_pub_t *dhd)
{
	/* cleanup all psq related resources */
	athost_wl_status_info_t* wlfc;
	uint32 tlv = 0;
	uint32 hostreorder = 0;

	if (dhd == NULL) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);
	if (!dhd->wlfc_enabled) {
		DHD_ERROR(("%s():%d, Already disabled!\n", __FUNCTION__, __LINE__));
		dhd_os_wlfc_unblock(dhd);
		return BCME_OK;
	}

	dhd->wlfc_enabled = FALSE;
	dhd_os_wlfc_unblock(dhd);

	/* query ampdu hostreorder */
	(void) dhd_wl_ioctl_get_intiovar(dhd, "ampdu_hostreorder",
		&hostreorder, WLC_GET_VAR, FALSE, 0);

	if (hostreorder) {
		tlv = WLFC_FLAGS_HOST_RXRERODER_ACTIVE;
		DHD_ERROR(("%s():%d, maintain HOST RXRERODER flag in tvl\n",
			__FUNCTION__, __LINE__));
	}

	/* Disable proptxtstatus signaling for deinit */
	(void) dhd_wl_ioctl_set_intiovar(dhd, "tlv", tlv, WLC_SET_VAR, TRUE, 0);

	dhd_os_wlfc_block(dhd);

	if (!dhd->wlfc_state || (dhd->proptxstatus_mode == WLFC_FCMODE_NONE)) {
		dhd_os_wlfc_unblock(dhd);
		return WLFC_UNSUPPORTED;
	}

	wlfc = (athost_wl_status_info_t*)dhd->wlfc_state;

	_dhd_wlfc_cleanup(dhd, NULL, NULL);

	if (!WLFC_GET_AFQ(dhd->wlfc_mode)) {
		int i;
		wlfc_hanger_t* h = (wlfc_hanger_t*)wlfc->hanger;
		for (i = 0; i < h->max_items; i++) {
			if (h->items[i].state != WLFC_HANGER_ITEM_STATE_FREE) {
				_dhd_wlfc_hanger_free_pkt(wlfc, i,
					WLFC_HANGER_PKT_STATE_COMPLETE, TRUE);
			}
		}

		/* delete hanger */
		_dhd_wlfc_hanger_delete(dhd, h);
	}


	/* free top structure */
	DHD_OS_PREFREE(dhd, dhd->wlfc_state,
		sizeof(athost_wl_status_info_t));
	dhd->wlfc_state = NULL;
	dhd->proptxstatus_mode = hostreorder ?
		WLFC_ONLY_AMPDU_HOSTREORDER : WLFC_FCMODE_NONE;

	DHD_ERROR(("%s: wlfc_mode=0x%x, tlv=%d\n", __FUNCTION__, dhd->wlfc_mode, tlv));

	dhd_os_wlfc_unblock(dhd);

	if (dhd->plat_deinit)
		dhd->plat_deinit((void *)dhd);
	return BCME_OK;
} /* dhd_wlfc_init */

/**
 * Called on an interface event (WLC_E_IF) indicated by firmware
 *     @param[in] dhdp   Pointer to public DHD structure
 *     @param[in] action eg eWLFC_MAC_ENTRY_ACTION_UPDATE or eWLFC_MAC_ENTRY_ACTION_ADD
 */
int dhd_wlfc_interface_event(dhd_pub_t *dhdp, uint8 action, uint8 ifid, uint8 iftype, uint8* ea)
{
	int rc;

	if (dhdp == NULL) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhdp);

	if (!dhdp->wlfc_state || (dhdp->proptxstatus_mode == WLFC_FCMODE_NONE)) {
		dhd_os_wlfc_unblock(dhdp);
		return WLFC_UNSUPPORTED;
	}

	rc = _dhd_wlfc_interface_entry_update(dhdp->wlfc_state, action, ifid, iftype, ea);

	dhd_os_wlfc_unblock(dhdp);
	return rc;
}

/** Called eg on receiving a WLC_E_FIFO_CREDIT_MAP event from the dongle */
int dhd_wlfc_FIFOcreditmap_event(dhd_pub_t *dhdp, uint8* event_data)
{
	int rc;

	if (dhdp == NULL) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhdp);

	if (!dhdp->wlfc_state || (dhdp->proptxstatus_mode == WLFC_FCMODE_NONE)) {
		dhd_os_wlfc_unblock(dhdp);
		return WLFC_UNSUPPORTED;
	}

	rc = _dhd_wlfc_FIFOcreditmap_update(dhdp->wlfc_state, event_data);

	dhd_os_wlfc_unblock(dhdp);

	return rc;
}
#ifdef LIMIT_BORROW
int dhd_wlfc_disable_credit_borrow_event(dhd_pub_t *dhdp, uint8* event_data)
{
	if (dhdp == NULL || event_data == NULL) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}
	dhd_os_wlfc_block(dhdp);
	dhdp->wlfc_borrow_allowed = (bool)(*(uint32 *)event_data);
	dhd_os_wlfc_unblock(dhdp);

	return BCME_OK;
}
#endif /* LIMIT_BORROW */

/**
 * Called eg on receiving a WLC_E_BCMC_CREDIT_SUPPORT event from the dongle (broadcast/multicast
 * specific)
 */
int dhd_wlfc_BCMCCredit_support_event(dhd_pub_t *dhdp)
{
	int rc;

	if (dhdp == NULL) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhdp);

	if (!dhdp->wlfc_state || (dhdp->proptxstatus_mode == WLFC_FCMODE_NONE)) {
		dhd_os_wlfc_unblock(dhdp);
		return WLFC_UNSUPPORTED;
	}

	rc = _dhd_wlfc_BCMCCredit_support_update(dhdp->wlfc_state);

	dhd_os_wlfc_unblock(dhdp);
	return rc;
}

/** debug specific function */
int
dhd_wlfc_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf)
{
	int i;
	uint8* ea;
	athost_wl_status_info_t* wlfc;
	wlfc_hanger_t* h;
	wlfc_mac_descriptor_t* mac_table;
	wlfc_mac_descriptor_t* interfaces;
	char* iftypes[] = {"STA", "AP", "WDS", "p2pGO", "p2pCL"};

	if (!dhdp || !strbuf) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhdp);

	if (!dhdp->wlfc_state || (dhdp->proptxstatus_mode == WLFC_FCMODE_NONE)) {
		dhd_os_wlfc_unblock(dhdp);
		return WLFC_UNSUPPORTED;
	}

	wlfc = (athost_wl_status_info_t*)dhdp->wlfc_state;

	h = (wlfc_hanger_t*)wlfc->hanger;
	if (h == NULL) {
		bcm_bprintf(strbuf, "wlfc-hanger not initialized yet\n");
	}

	mac_table = wlfc->destination_entries.nodes;
	interfaces = wlfc->destination_entries.interfaces;
	bcm_bprintf(strbuf, "---- wlfc stats ----\n");

	if (!WLFC_GET_AFQ(dhdp->wlfc_mode)) {
		h = (wlfc_hanger_t*)wlfc->hanger;
		if (h == NULL) {
			bcm_bprintf(strbuf, "wlfc-hanger not initialized yet\n");
		} else {
			bcm_bprintf(strbuf, "wlfc hanger (pushed,popped,f_push,"
				"f_pop,f_slot, pending) = (%d,%d,%d,%d,%d,%d)\n",
				h->pushed,
				h->popped,
				h->failed_to_push,
				h->failed_to_pop,
				h->failed_slotfind,
				(h->pushed - h->popped));
		}
	}

	bcm_bprintf(strbuf, "wlfc fail(tlv,credit_rqst,mac_update,psmode_update), "
		"(dq_full,rollback_fail) = (%d,%d,%d,%d), (%d,%d)\n",
		wlfc->stats.tlv_parse_failed,
		wlfc->stats.credit_request_failed,
		wlfc->stats.mac_update_failed,
		wlfc->stats.psmode_update_failed,
		wlfc->stats.delayq_full_error,
		wlfc->stats.rollback_failed);

	bcm_bprintf(strbuf, "PKTS (init_credit,credit,sent,drop_d,drop_s,outoforder) "
		"(AC0[%d,%d,%d,%d,%d,%d],AC1[%d,%d,%d,%d,%d,%d],AC2[%d,%d,%d,%d,%d,%d],"
		"AC3[%d,%d,%d,%d,%d,%d],BC_MC[%d,%d,%d,%d,%d,%d])\n",
		wlfc->Init_FIFO_credit[0], wlfc->FIFO_credit[0], wlfc->stats.send_pkts[0],
		wlfc->stats.drop_pkts[0], wlfc->stats.drop_pkts[1], wlfc->stats.ooo_pkts[0],
		wlfc->Init_FIFO_credit[1], wlfc->FIFO_credit[1], wlfc->stats.send_pkts[1],
		wlfc->stats.drop_pkts[2], wlfc->stats.drop_pkts[3], wlfc->stats.ooo_pkts[1],
		wlfc->Init_FIFO_credit[2], wlfc->FIFO_credit[2], wlfc->stats.send_pkts[2],
		wlfc->stats.drop_pkts[4], wlfc->stats.drop_pkts[5], wlfc->stats.ooo_pkts[2],
		wlfc->Init_FIFO_credit[3], wlfc->FIFO_credit[3], wlfc->stats.send_pkts[3],
		wlfc->stats.drop_pkts[6], wlfc->stats.drop_pkts[7], wlfc->stats.ooo_pkts[3],
		wlfc->Init_FIFO_credit[4], wlfc->FIFO_credit[4], wlfc->stats.send_pkts[4],
		wlfc->stats.drop_pkts[8], wlfc->stats.drop_pkts[9], wlfc->stats.ooo_pkts[4]);

	bcm_bprintf(strbuf, "\n");
	for (i = 0; i < WLFC_MAX_IFNUM; i++) {
		if (interfaces[i].occupied) {
			char* iftype_desc;

			if (interfaces[i].iftype > WLC_E_IF_ROLE_P2P_CLIENT)
				iftype_desc = "<Unknown";
			else
				iftype_desc = iftypes[interfaces[i].iftype];

			ea = interfaces[i].ea;
			bcm_bprintf(strbuf, "INTERFACE[%d].ea = "
				"[%02x:%02x:%02x:%02x:%02x:%02x], if:%d, type: %s "
				"netif_flow_control:%s\n", i,
				ea[0], ea[1], ea[2], ea[3], ea[4], ea[5],
				interfaces[i].interface_id,
				iftype_desc, ((wlfc->hostif_flow_state[i] == OFF)
				? " OFF":" ON"));

			bcm_bprintf(strbuf, "INTERFACE[%d].PSQ(len,state,credit),"
				"(trans,supp_trans,onbus)"
				"= (%d,%s,%d),(%d,%d,%d)\n",
				i,
				interfaces[i].psq.len,
				((interfaces[i].state ==
				WLFC_STATE_OPEN) ? "OPEN":"CLOSE"),
				interfaces[i].requested_credit,
				interfaces[i].transit_count,
				interfaces[i].suppr_transit_count,
				interfaces[i].onbus_pkts_count);

			bcm_bprintf(strbuf, "INTERFACE[%d].PSQ"
				"(delay0,sup0,afq0),(delay1,sup1,afq1),(delay2,sup2,afq2),"
				"(delay3,sup3,afq3),(delay4,sup4,afq4) = (%d,%d,%d),"
				"(%d,%d,%d),(%d,%d,%d),(%d,%d,%d),(%d,%d,%d)\n",
				i,
				interfaces[i].psq.q[0].len,
				interfaces[i].psq.q[1].len,
				interfaces[i].afq.q[0].len,
				interfaces[i].psq.q[2].len,
				interfaces[i].psq.q[3].len,
				interfaces[i].afq.q[1].len,
				interfaces[i].psq.q[4].len,
				interfaces[i].psq.q[5].len,
				interfaces[i].afq.q[2].len,
				interfaces[i].psq.q[6].len,
				interfaces[i].psq.q[7].len,
				interfaces[i].afq.q[3].len,
				interfaces[i].psq.q[8].len,
				interfaces[i].psq.q[9].len,
				interfaces[i].afq.q[4].len);
		}
	}

	bcm_bprintf(strbuf, "\n");
	for (i = 0; i < WLFC_MAC_DESC_TABLE_SIZE; i++) {
		if (mac_table[i].occupied) {
			ea = mac_table[i].ea;
			bcm_bprintf(strbuf, "MAC_table[%d].ea = "
				"[%02x:%02x:%02x:%02x:%02x:%02x], if:%d \n", i,
				ea[0], ea[1], ea[2], ea[3], ea[4], ea[5],
				mac_table[i].interface_id);

			bcm_bprintf(strbuf, "MAC_table[%d].PSQ(len,state,credit),"
				"(trans,supp_trans,onbus)"
				"= (%d,%s,%d),(%d,%d,%d)\n",
				i,
				mac_table[i].psq.len,
				((mac_table[i].state ==
				WLFC_STATE_OPEN) ? " OPEN":"CLOSE"),
				mac_table[i].requested_credit,
				mac_table[i].transit_count,
				mac_table[i].suppr_transit_count,
				mac_table[i].onbus_pkts_count);
#ifdef PROP_TXSTATUS_DEBUG
			bcm_bprintf(strbuf, "MAC_table[%d]: (opened, closed) = (%d, %d)\n",
				i, mac_table[i].opened_ct, mac_table[i].closed_ct);
#endif
			bcm_bprintf(strbuf, "MAC_table[%d].PSQ"
				"(delay0,sup0,afq0),(delay1,sup1,afq1),(delay2,sup2,afq2),"
				"(delay3,sup3,afq3),(delay4,sup4,afq4) =(%d,%d,%d),"
				"(%d,%d,%d),(%d,%d,%d),(%d,%d,%d),(%d,%d,%d)\n",
				i,
				mac_table[i].psq.q[0].len,
				mac_table[i].psq.q[1].len,
				mac_table[i].afq.q[0].len,
				mac_table[i].psq.q[2].len,
				mac_table[i].psq.q[3].len,
				mac_table[i].afq.q[1].len,
				mac_table[i].psq.q[4].len,
				mac_table[i].psq.q[5].len,
				mac_table[i].afq.q[2].len,
				mac_table[i].psq.q[6].len,
				mac_table[i].psq.q[7].len,
				mac_table[i].afq.q[3].len,
				mac_table[i].psq.q[8].len,
				mac_table[i].psq.q[9].len,
				mac_table[i].afq.q[4].len);

		}
	}

#ifdef PROP_TXSTATUS_DEBUG
	{
		int avg;
		int moving_avg = 0;
		int moving_samples;

		if (wlfc->stats.latency_sample_count) {
			moving_samples = sizeof(wlfc->stats.deltas)/sizeof(uint32);

			for (i = 0; i < moving_samples; i++)
				moving_avg += wlfc->stats.deltas[i];
			moving_avg /= moving_samples;

			avg = (100 * wlfc->stats.total_status_latency) /
				wlfc->stats.latency_sample_count;
			bcm_bprintf(strbuf, "txstatus latency (average, last, moving[%d]) = "
				"(%d.%d, %03d, %03d)\n",
				moving_samples, avg/100, (avg - (avg/100)*100),
				wlfc->stats.latency_most_recent,
				moving_avg);
		}
	}

	bcm_bprintf(strbuf, "wlfc- fifo[0-5] credit stats: sent = (%d,%d,%d,%d,%d,%d), "
		"back = (%d,%d,%d,%d,%d,%d)\n",
		wlfc->stats.fifo_credits_sent[0],
		wlfc->stats.fifo_credits_sent[1],
		wlfc->stats.fifo_credits_sent[2],
		wlfc->stats.fifo_credits_sent[3],
		wlfc->stats.fifo_credits_sent[4],
		wlfc->stats.fifo_credits_sent[5],

		wlfc->stats.fifo_credits_back[0],
		wlfc->stats.fifo_credits_back[1],
		wlfc->stats.fifo_credits_back[2],
		wlfc->stats.fifo_credits_back[3],
		wlfc->stats.fifo_credits_back[4],
		wlfc->stats.fifo_credits_back[5]);
	{
		uint32 fifo_cr_sent = 0;
		uint32 fifo_cr_acked = 0;
		uint32 request_cr_sent = 0;
		uint32 request_cr_ack = 0;
		uint32 bc_mc_cr_ack = 0;

		for (i = 0; i < sizeof(wlfc->stats.fifo_credits_sent)/sizeof(uint32); i++) {
			fifo_cr_sent += wlfc->stats.fifo_credits_sent[i];
		}

		for (i = 0; i < sizeof(wlfc->stats.fifo_credits_back)/sizeof(uint32); i++) {
			fifo_cr_acked += wlfc->stats.fifo_credits_back[i];
		}

		for (i = 0; i < WLFC_MAC_DESC_TABLE_SIZE; i++) {
			if (wlfc->destination_entries.nodes[i].occupied) {
				request_cr_sent +=
					wlfc->destination_entries.nodes[i].dstncredit_sent_packets;
			}
		}
		for (i = 0; i < WLFC_MAX_IFNUM; i++) {
			if (wlfc->destination_entries.interfaces[i].occupied) {
				request_cr_sent +=
				wlfc->destination_entries.interfaces[i].dstncredit_sent_packets;
			}
		}
		for (i = 0; i < WLFC_MAC_DESC_TABLE_SIZE; i++) {
			if (wlfc->destination_entries.nodes[i].occupied) {
				request_cr_ack +=
					wlfc->destination_entries.nodes[i].dstncredit_acks;
			}
		}
		for (i = 0; i < WLFC_MAX_IFNUM; i++) {
			if (wlfc->destination_entries.interfaces[i].occupied) {
				request_cr_ack +=
					wlfc->destination_entries.interfaces[i].dstncredit_acks;
			}
		}
		bcm_bprintf(strbuf, "wlfc- (sent, status) => pq(%d,%d), vq(%d,%d),"
			"other:%d, bc_mc:%d, signal-only, (sent,freed): (%d,%d)",
			fifo_cr_sent, fifo_cr_acked,
			request_cr_sent, request_cr_ack,
			wlfc->destination_entries.other.dstncredit_acks,
			bc_mc_cr_ack,
			wlfc->stats.signal_only_pkts_sent, wlfc->stats.signal_only_pkts_freed);
	}
#endif /* PROP_TXSTATUS_DEBUG */
	bcm_bprintf(strbuf, "\n");
	bcm_bprintf(strbuf, "wlfc- pkt((in,2bus,txstats,hdrpull,out),(dropped,hdr_only,wlc_tossed)"
		"(freed,free_err,rollback)) = "
		"((%d,%d,%d,%d,%d),(%d,%d,%d),(%d,%d,%d))\n",
		wlfc->stats.pktin,
		wlfc->stats.pkt2bus,
		wlfc->stats.txstatus_in,
		wlfc->stats.dhd_hdrpulls,
		wlfc->stats.pktout,

		wlfc->stats.pktdropped,
		wlfc->stats.wlfc_header_only_pkt,
		wlfc->stats.wlc_tossed_pkts,

		wlfc->stats.pkt_freed,
		wlfc->stats.pkt_free_err, wlfc->stats.rollback);

	bcm_bprintf(strbuf, "wlfc- suppress((d11,wlc,err),enq(d11,wl,hq,mac?),retx(d11,wlc,hq)) = "
		"((%d,%d,%d),(%d,%d,%d,%d),(%d,%d,%d))\n",
		wlfc->stats.d11_suppress,
		wlfc->stats.wl_suppress,
		wlfc->stats.bad_suppress,

		wlfc->stats.psq_d11sup_enq,
		wlfc->stats.psq_wlsup_enq,
		wlfc->stats.psq_hostq_enq,
		wlfc->stats.mac_handle_notfound,

		wlfc->stats.psq_d11sup_retx,
		wlfc->stats.psq_wlsup_retx,
		wlfc->stats.psq_hostq_retx);

	bcm_bprintf(strbuf, "wlfc- cleanup(txq,psq,fw) = (%d,%d,%d)\n",
		wlfc->stats.cleanup_txq_cnt,
		wlfc->stats.cleanup_psq_cnt,
		wlfc->stats.cleanup_fw_cnt);

	bcm_bprintf(strbuf, "wlfc- generic error: %d\n", wlfc->stats.generic_error);

	for (i = 0; i < WLFC_MAX_IFNUM; i++) {
		bcm_bprintf(strbuf, "wlfc- if[%d], pkt_cnt_in_q/AC[0-4] = (%d,%d,%d,%d,%d)\n", i,
			wlfc->pkt_cnt_in_q[i][0],
			wlfc->pkt_cnt_in_q[i][1],
			wlfc->pkt_cnt_in_q[i][2],
			wlfc->pkt_cnt_in_q[i][3],
			wlfc->pkt_cnt_in_q[i][4]);
	}
	bcm_bprintf(strbuf, "\n");

	dhd_os_wlfc_unblock(dhdp);
	return BCME_OK;
} /* dhd_wlfc_dump */

int dhd_wlfc_clear_counts(dhd_pub_t *dhd)
{
	athost_wl_status_info_t* wlfc;
	wlfc_hanger_t* hanger;

	if (dhd == NULL) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);

	if (!dhd->wlfc_state || (dhd->proptxstatus_mode == WLFC_FCMODE_NONE)) {
		dhd_os_wlfc_unblock(dhd);
		return WLFC_UNSUPPORTED;
	}

	wlfc = (athost_wl_status_info_t*)dhd->wlfc_state;

	memset(&wlfc->stats, 0, sizeof(athost_wl_stat_counters_t));

	if (!WLFC_GET_AFQ(dhd->wlfc_mode)) {
		hanger = (wlfc_hanger_t*)wlfc->hanger;

		hanger->pushed = 0;
		hanger->popped = 0;
		hanger->failed_slotfind = 0;
		hanger->failed_to_pop = 0;
		hanger->failed_to_push = 0;
	}

	dhd_os_wlfc_unblock(dhd);

	return BCME_OK;
}

/** returns TRUE if flow control is enabled */
int dhd_wlfc_get_enable(dhd_pub_t *dhd, bool *val)
{
	if (!dhd || !val) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);

	*val = dhd->wlfc_enabled;

	dhd_os_wlfc_unblock(dhd);

	return BCME_OK;
}

/** Called via an IOVAR */
int dhd_wlfc_get_mode(dhd_pub_t *dhd, int *val)
{
	if (!dhd || !val) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);

	*val = dhd->wlfc_state ? dhd->proptxstatus_mode : 0;

	dhd_os_wlfc_unblock(dhd);

	return BCME_OK;
}

/** Called via an IOVAR */
int dhd_wlfc_set_mode(dhd_pub_t *dhd, int val)
{
	if (!dhd) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);

	if (dhd->wlfc_state) {
		dhd->proptxstatus_mode = val & 0xff;
	}

	dhd_os_wlfc_unblock(dhd);

	return BCME_OK;
}

/** Called when rx frame is received from the dongle */
bool dhd_wlfc_is_header_only_pkt(dhd_pub_t * dhd, void *pktbuf)
{
	athost_wl_status_info_t* wlfc;
	bool rc = FALSE;

	if (dhd == NULL) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return FALSE;
	}

	dhd_os_wlfc_block(dhd);

	if (!dhd->wlfc_state || (dhd->proptxstatus_mode == WLFC_FCMODE_NONE)) {
		dhd_os_wlfc_unblock(dhd);
		return FALSE;
	}

	wlfc = (athost_wl_status_info_t*)dhd->wlfc_state;

	if (PKTLEN(wlfc->osh, pktbuf) == 0) {
		wlfc->stats.wlfc_header_only_pkt++;
		rc = TRUE;
	}

	dhd_os_wlfc_unblock(dhd);

	return rc;
}

int dhd_wlfc_flowcontrol(dhd_pub_t *dhdp, bool state, bool bAcquireLock)
{
	if (dhdp == NULL) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	if (bAcquireLock) {
		dhd_os_wlfc_block(dhdp);
	}

	if (!dhdp->wlfc_state || (dhdp->proptxstatus_mode == WLFC_FCMODE_NONE) ||
		dhdp->proptxstatus_module_ignore) {
		if (bAcquireLock) {
			dhd_os_wlfc_unblock(dhdp);
		}
		return WLFC_UNSUPPORTED;
	}

	if (state != dhdp->proptxstatus_txoff) {
		dhdp->proptxstatus_txoff = state;
	}

	if (bAcquireLock) {
		dhd_os_wlfc_unblock(dhdp);
	}

	return BCME_OK;
}

/** Called when eg an rx frame is received from the dongle */
int dhd_wlfc_save_rxpath_ac_time(dhd_pub_t * dhd, uint8 prio)
{
	athost_wl_status_info_t* wlfc;
	int rx_path_ac = -1;

	if ((dhd == NULL) || (prio >= NUMPRIO)) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);

	if (!dhd->wlfc_rxpkt_chk) {
		dhd_os_wlfc_unblock(dhd);
		return BCME_OK;
	}

	if (!dhd->wlfc_state || (dhd->proptxstatus_mode == WLFC_FCMODE_NONE)) {
		dhd_os_wlfc_unblock(dhd);
		return WLFC_UNSUPPORTED;
	}

	wlfc = (athost_wl_status_info_t*)dhd->wlfc_state;

	rx_path_ac = prio2fifo[prio];
	wlfc->rx_timestamp[rx_path_ac] = OSL_SYSUPTIME();

	dhd_os_wlfc_unblock(dhd);

	return BCME_OK;
}

/** called via an IOVAR */
int dhd_wlfc_get_module_ignore(dhd_pub_t *dhd, int *val)
{
	if (!dhd || !val) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);

	*val = dhd->proptxstatus_module_ignore;

	dhd_os_wlfc_unblock(dhd);

	return BCME_OK;
}

/** called via an IOVAR */
int dhd_wlfc_set_module_ignore(dhd_pub_t *dhd, int val)
{
	uint32 tlv = 0;
	bool bChanged = FALSE;

	if (!dhd) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);

	if ((bool)val != dhd->proptxstatus_module_ignore) {
		dhd->proptxstatus_module_ignore = (val != 0);
		/* force txstatus_ignore sync with proptxstatus_module_ignore */
		dhd->proptxstatus_txstatus_ignore = dhd->proptxstatus_module_ignore;
		if (FALSE == dhd->proptxstatus_module_ignore) {
			tlv = WLFC_FLAGS_RSSI_SIGNALS |
				WLFC_FLAGS_XONXOFF_SIGNALS |
				WLFC_FLAGS_CREDIT_STATUS_SIGNALS |
				WLFC_FLAGS_HOST_PROPTXSTATUS_ACTIVE;
		}
		/* always enable host reorder */
		tlv |= WLFC_FLAGS_HOST_RXRERODER_ACTIVE;
		bChanged = TRUE;
	}

	dhd_os_wlfc_unblock(dhd);

	if (bChanged) {
		/* select enable proptxtstatus signaling */
		if (dhd_wl_ioctl_set_intiovar(dhd, "tlv", tlv, WLC_SET_VAR, TRUE, 0)) {
			DHD_ERROR(("%s: failed to set bdcv2 tlv signaling to 0x%x\n",
				__FUNCTION__, tlv));
		} else {
			DHD_ERROR(("%s: successfully set bdcv2 tlv signaling to 0x%x\n",
				__FUNCTION__, tlv));
		}
	}

#if defined(DHD_WLFC_THREAD)
	_dhd_wlfc_thread_wakeup(dhd);
#endif /* defined(DHD_WLFC_THREAD) */

	return BCME_OK;
}

/** called via an IOVAR */
int dhd_wlfc_get_credit_ignore(dhd_pub_t *dhd, int *val)
{
	if (!dhd || !val) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);

	*val = dhd->proptxstatus_credit_ignore;

	dhd_os_wlfc_unblock(dhd);

	return BCME_OK;
}

/** called via an IOVAR */
int dhd_wlfc_set_credit_ignore(dhd_pub_t *dhd, int val)
{
	if (!dhd) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);

	dhd->proptxstatus_credit_ignore = (val != 0);

	dhd_os_wlfc_unblock(dhd);

	return BCME_OK;
}

/** called via an IOVAR */
int dhd_wlfc_get_txstatus_ignore(dhd_pub_t *dhd, int *val)
{
	if (!dhd || !val) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);

	*val = dhd->proptxstatus_txstatus_ignore;

	dhd_os_wlfc_unblock(dhd);

	return BCME_OK;
}

/** called via an IOVAR */
int dhd_wlfc_set_txstatus_ignore(dhd_pub_t *dhd, int val)
{
	if (!dhd) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);

	dhd->proptxstatus_txstatus_ignore = (val != 0);

	dhd_os_wlfc_unblock(dhd);

	return BCME_OK;
}

/** called via an IOVAR */
int dhd_wlfc_get_rxpkt_chk(dhd_pub_t *dhd, int *val)
{
	if (!dhd || !val) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);

	*val = dhd->wlfc_rxpkt_chk;

	dhd_os_wlfc_unblock(dhd);

	return BCME_OK;
}

/** called via an IOVAR */
int dhd_wlfc_set_rxpkt_chk(dhd_pub_t *dhd, int val)
{
	if (!dhd) {
		DHD_ERROR(("Error: %s():%d\n", __FUNCTION__, __LINE__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhd);

	dhd->wlfc_rxpkt_chk = (val != 0);

	dhd_os_wlfc_unblock(dhd);

	return BCME_OK;
}

#ifdef PROPTX_MAXCOUNT
int dhd_wlfc_update_maxcount(dhd_pub_t *dhdp, uint8 ifid, int maxcount)
{
	athost_wl_status_info_t* ctx;
	int rc = 0;

	if (dhdp == NULL) {
		DHD_ERROR(("%s: dhdp is NULL\n", __FUNCTION__));
		return BCME_BADARG;
	}

	dhd_os_wlfc_block(dhdp);

	if (!dhdp->wlfc_state || (dhdp->proptxstatus_mode == WLFC_FCMODE_NONE)) {
		rc = WLFC_UNSUPPORTED;
		goto exit;
	}

	if (ifid >= WLFC_MAX_IFNUM) {
		DHD_ERROR(("%s: bad ifid\n", __FUNCTION__));
		rc = BCME_BADARG;
		goto exit;
	}

	ctx = (athost_wl_status_info_t*)dhdp->wlfc_state;
	ctx->destination_entries.interfaces[ifid].transit_maxcount = maxcount;
exit:
	dhd_os_wlfc_unblock(dhdp);
	return rc;
}
#endif /* PROPTX_MAXCOUNT */

#endif /* PROP_TXSTATUS */
