/*
* Copyright (C) 1999-2011, Broadcom Corporation
* 
*         Unless you and Broadcom execute a separate written software license
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
* $Id: dhd_wlfc.h,v 1.1.8.1 2010-09-09 22:41:08 Exp $
*
*/
#ifndef __wlfc_host_driver_definitions_h__
#define __wlfc_host_driver_definitions_h__

/* 16 bits will provide an absolute max of 65536 slots */
#define WLFC_HANGER_MAXITEMS 1024

#define WLFC_HANGER_ITEM_STATE_FREE		1
#define WLFC_HANGER_ITEM_STATE_INUSE	2

#define WLFC_PKTID_HSLOT_MASK			0xffff /* allow 16 bits only */
#define WLFC_PKTID_HSLOT_SHIFT			8

/* x -> TXSTATUS TAG to/from firmware */
#define WLFC_PKTID_HSLOT_GET(x)			\
	(((x) >> WLFC_PKTID_HSLOT_SHIFT) & WLFC_PKTID_HSLOT_MASK)
#define WLFC_PKTID_HSLOT_SET(var, slot)	\
	((var) = ((var) & ~(WLFC_PKTID_HSLOT_MASK << WLFC_PKTID_HSLOT_SHIFT)) | \
	(((slot) & WLFC_PKTID_HSLOT_MASK) << WLFC_PKTID_HSLOT_SHIFT))

#define WLFC_PKTID_FREERUNCTR_MASK	0xff

#define WLFC_PKTID_FREERUNCTR_GET(x)	((x) & WLFC_PKTID_FREERUNCTR_MASK)
#define WLFC_PKTID_FREERUNCTR_SET(var, ctr)	\
	((var) = (((var) & ~WLFC_PKTID_FREERUNCTR_MASK) | \
	(((ctr) & WLFC_PKTID_FREERUNCTR_MASK))))

#define WLFC_PKTQ_PENQ(pq, prec, p) ((pktq_full((pq)) || pktq_pfull((pq), (prec)))? \
	NULL : pktq_penq((pq), (prec), (p)))
#define WLFC_PKTQ_PENQ_HEAD(pq, prec, p) ((pktq_full((pq)) || pktq_pfull((pq), (prec))) ? \
	NULL : pktq_penq_head((pq), (prec), (p)))

typedef enum ewlfc_packet_state {
	eWLFC_PKTTYPE_NEW,
	eWLFC_PKTTYPE_DELAYED,
	eWLFC_PKTTYPE_SUPPRESSED,
	eWLFC_PKTTYPE_MAX
} ewlfc_packet_state_t;

typedef enum ewlfc_mac_entry_action {
	eWLFC_MAC_ENTRY_ACTION_ADD,
	eWLFC_MAC_ENTRY_ACTION_DEL,
	eWLFC_MAC_ENTRY_ACTION_MAX
} ewlfc_mac_entry_action_t;

typedef struct wlfc_hanger_item {
	uint8	state;
	uint8	pad[3];
	uint32	identifier;
	void*	pkt;
#ifdef PROP_TXSTATUS_DEBUG
	uint32	push_time;
#endif
} wlfc_hanger_item_t;

typedef struct wlfc_hanger {
	int max_items;
	uint32 pushed;
	uint32 popped;
	uint32 failed_to_push;
	uint32 failed_to_pop;
	uint32 failed_slotfind;
	wlfc_hanger_item_t items[1];
} wlfc_hanger_t;

#define WLFC_HANGER_SIZE(n)	((sizeof(wlfc_hanger_t) - \
	sizeof(wlfc_hanger_item_t)) + ((n)*sizeof(wlfc_hanger_item_t)))

#define WLFC_STATE_OPEN		1
#define WLFC_STATE_CLOSE	2

#define WLFC_PSQ_PREC_COUNT		((AC_COUNT + 1) * 2) /* 2 for each AC traffic and bc/mc */
#define WLFC_PSQ_LEN			64
#define WLFC_SENDQ_LEN			256

#define WLFC_FLOWCONTROL_DELTA		8
#define WLFC_FLOWCONTROL_HIWATER	(WLFC_PSQ_LEN - WLFC_FLOWCONTROL_DELTA)
#define WLFC_FLOWCONTROL_LOWATER	(WLFC_FLOWCONTROL_HIWATER - WLFC_FLOWCONTROL_DELTA)

typedef struct wlfc_mac_descriptor {
	uint8 occupied;
	uint8 interface_id;
	uint8 iftype;
	uint8 state;
	uint8 ac_bitmap; /* for APSD */
	uint8 requested_credit;
	uint8 requested_packet;
	uint8 ea[ETHER_ADDR_LEN];
	/*
	maintain (MAC,AC) based seq count for
	packets going to the device. As well as bc/mc.
	*/
	uint8 seq[AC_COUNT + 1];
	uint8 generation;
	struct pktq	psq;
	/* The AC pending bitmap that was reported to the fw at last change */
	uint8 traffic_lastreported_bmp;
	/* The new AC pending bitmap */
	uint8 traffic_pending_bmp;
	/* 1= send on next opportunity */
	uint8 send_tim_signal;
	uint8 mac_handle;
#ifdef PROP_TXSTATUS_DEBUG
	uint32 dstncredit_sent_packets;
	uint32 dstncredit_acks;
	uint32 opened_ct;
	uint32 closed_ct;
#endif
} wlfc_mac_descriptor_t;

#define WLFC_DECR_SEQCOUNT(entry, prec) do { if (entry->seq[(prec)] == 0) {\
	entry->seq[prec] = 0xff; } else entry->seq[prec]--;} while (0)

#define WLFC_INCR_SEQCOUNT(entry, prec) entry->seq[(prec)]++
#define WLFC_SEQCOUNT(entry, prec) entry->seq[(prec)]

typedef struct athost_wl_stat_counters {
	uint32	pktin;
	uint32	pkt2bus;
	uint32	pktdropped;
	uint32	tlv_parse_failed;
	uint32	rollback;
	uint32	rollback_failed;
	uint32	sendq_full_error;
	uint32	delayq_full_error;
	uint32	credit_request_failed;
	uint32	packet_request_failed;
	uint32	mac_update_failed;
	uint32	psmode_update_failed;
	uint32	interface_update_failed;
	uint32	wlfc_header_only_pkt;
	uint32	txstatus_in;
	uint32	d11_suppress;
	uint32	wl_suppress;
	uint32	bad_suppress;
	uint32	pkt_freed;
	uint32	pkt_free_err;
	uint32	psq_wlsup_retx;
	uint32	psq_wlsup_enq;
	uint32	psq_d11sup_retx;
	uint32	psq_d11sup_enq;
	uint32	psq_hostq_retx;
	uint32	psq_hostq_enq;
	uint32	mac_handle_notfound;
	uint32	wlc_tossed_pkts;
	uint32	dhd_hdrpulls;
	uint32	generic_error;
	/* an extra one for bc/mc traffic */
	uint32	sendq_pkts[AC_COUNT + 1];
#ifdef PROP_TXSTATUS_DEBUG
	/* all pkt2bus -> txstatus latency accumulated */
	uint32	latency_sample_count;
	uint32	total_status_latency;
	uint32	latency_most_recent;
	int		idx_delta;
	uint32	deltas[10];
	uint32	fifo_credits_sent[6];
	uint32	fifo_credits_back[6];
	uint32	dropped_qfull[6];
	uint32	signal_only_pkts_sent;
	uint32	signal_only_pkts_freed;
#endif
} athost_wl_stat_counters_t;

#ifdef PROP_TXSTATUS_DEBUG
#define WLFC_HOST_FIFO_CREDIT_INC_SENTCTRS(ctx, ac) do { \
	(ctx)->stats.fifo_credits_sent[(ac)]++;} while (0)
#define WLFC_HOST_FIFO_CREDIT_INC_BACKCTRS(ctx, ac) do { \
	(ctx)->stats.fifo_credits_back[(ac)]++;} while (0)
#define WLFC_HOST_FIFO_DROPPEDCTR_INC(ctx, ac) do { \
	(ctx)->stats.dropped_qfull[(ac)]++;} while (0)
#else
#define WLFC_HOST_FIFO_CREDIT_INC_SENTCTRS(ctx, ac) do {} while (0)
#define WLFC_HOST_FIFO_CREDIT_INC_BACKCTRS(ctx, ac) do {} while (0)
#define WLFC_HOST_FIFO_DROPPEDCTR_INC(ctx, ac) do {} while (0)
#endif

#define WLFC_FCMODE_NONE				0
#define WLFC_FCMODE_IMPLIED_CREDIT		1
#define WLFC_FCMODE_EXPLICIT_CREDIT		2

#define WLFC_BORROW_DEFER_PERIOD_MS 100

/* Mask to represent available ACs (note: BC/MC is ignored */
#define WLFC_AC_MASK 0xF

/* Mask to check for only on-going AC_BE traffic */
#define WLFC_AC_BE_TRAFFIC_ONLY 0xD

typedef struct athost_wl_status_info {
	uint8	last_seqid_to_wlc;

	/* OSL handle */
	osl_t*	osh;
	/* dhd pub */
	void*	dhdp;

	/* stats */
	athost_wl_stat_counters_t stats;

	/* the additional ones are for bc/mc and ATIM FIFO */
	int		FIFO_credit[AC_COUNT + 2];

	/* Credit borrow counts for each FIFO from each of the other FIFOs */
	int		credits_borrowed[AC_COUNT + 2][AC_COUNT + 2];

	struct  pktq SENDQ;

	/* packet hanger and MAC->handle lookup table */
	void*	hanger;
	struct {
		/* table for individual nodes */
		wlfc_mac_descriptor_t	nodes[WLFC_MAC_DESC_TABLE_SIZE];
		/* table for interfaces */
		wlfc_mac_descriptor_t	interfaces[WLFC_MAX_IFNUM];
		/* OS may send packets to unknown (unassociated) destinations */
		/* A place holder for bc/mc and packets to unknown destinations */
		wlfc_mac_descriptor_t	other;
	} destination_entries;
	/* token position for different priority packets */
	uint8   token_pos[AC_COUNT+1];
	/* ON/OFF state for flow control to the host network interface */
	uint8	hostif_flow_state[WLFC_MAX_IFNUM];
	uint8	host_ifidx;
	/* to flow control an OS interface */
	uint8	toggle_host_if;

	/*
	Mode in which the dhd flow control shall operate. Must be set before
	traffic starts to the device.
	0 - Do not do any proptxtstatus flow control
	1 - Use implied credit from a packet status
	2 - Use explicit credit
	*/
	uint8	proptxstatus_mode;

	/* To borrow credits */
	uint8   allow_credit_borrow;

	/* Timestamp to compute how long to defer borrowing for */
	uint32  borrow_defer_timestamp;
} athost_wl_status_info_t;

#endif /* __wlfc_host_driver_definitions_h__ */
