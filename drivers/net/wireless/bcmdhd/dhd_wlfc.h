/*
* Copyright (C) 1999-2014, Broadcom Corporation
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
* $Id: dhd_wlfc.h 485659 2014-06-16 21:33:12Z $
*
*/
#ifndef __wlfc_host_driver_definitions_h__
#define __wlfc_host_driver_definitions_h__


/* #define OOO_DEBUG */

#define WLFC_UNSUPPORTED -9999

#define WLFC_NO_TRAFFIC	-1
#define WLFC_MULTI_TRAFFIC 0

#define BUS_RETRIES 1	/* # of retries before aborting a bus tx operation */

/* 16 bits will provide an absolute max of 65536 slots */
#define WLFC_HANGER_MAXITEMS 3072

#define WLFC_HANGER_ITEM_STATE_FREE			1
#define WLFC_HANGER_ITEM_STATE_INUSE			2
#define WLFC_HANGER_ITEM_STATE_INUSE_SUPPRESSED		3
#define WLFC_HANGER_ITEM_STATE_WAIT_CLEAN		4

#define WLFC_HANGER_ITEM_WAIT_EVENT_COUNT		2
#define WLFC_HANGER_ITEM_WAIT_EVENT_INVALID		255

typedef enum {
	Q_TYPE_PSQ,
	Q_TYPE_AFQ
} q_type_t;

typedef enum ewlfc_packet_state {
	eWLFC_PKTTYPE_NEW,
	eWLFC_PKTTYPE_DELAYED,
	eWLFC_PKTTYPE_SUPPRESSED,
	eWLFC_PKTTYPE_MAX
} ewlfc_packet_state_t;

typedef enum ewlfc_mac_entry_action {
	eWLFC_MAC_ENTRY_ACTION_ADD,
	eWLFC_MAC_ENTRY_ACTION_DEL,
	eWLFC_MAC_ENTRY_ACTION_UPDATE,
	eWLFC_MAC_ENTRY_ACTION_MAX
} ewlfc_mac_entry_action_t;

typedef struct wlfc_hanger_item {
	uint8	state;
	uint8   gen;
	uint8	waitevent;	/* wait txstatus_update and txcomplete before free a packet */
	uint8	pad;
	uint32	identifier;
	void*	pkt;
#ifdef PROP_TXSTATUS_DEBUG
	uint32	push_time;
#endif
	struct wlfc_hanger_item *next;
} wlfc_hanger_item_t;

typedef struct wlfc_hanger {
	int max_items;
	uint32 pushed;
	uint32 popped;
	uint32 failed_to_push;
	uint32 failed_to_pop;
	uint32 failed_slotfind;
	uint32 slot_pos;
	wlfc_hanger_item_t items[1];
} wlfc_hanger_t;

#define WLFC_HANGER_SIZE(n)	((sizeof(wlfc_hanger_t) - \
	sizeof(wlfc_hanger_item_t)) + ((n)*sizeof(wlfc_hanger_item_t)))

#define WLFC_STATE_OPEN		1
#define WLFC_STATE_CLOSE	2

#define WLFC_PSQ_PREC_COUNT		((AC_COUNT + 1) * 2) /* 2 for each AC traffic and bc/mc */
#define WLFC_AFQ_PREC_COUNT		(AC_COUNT + 1)

#define WLFC_PSQ_LEN			2048

#define WLFC_FLOWCONTROL_HIWATER	(2048 - 256)
#define WLFC_FLOWCONTROL_LOWATER	256

#define WLFC_LOG_BUF_SIZE		(1024*1024)

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
	/* packets at firmware */
	struct pktq	afq;
	/* The AC pending bitmap that was reported to the fw at last change */
	uint8 traffic_lastreported_bmp;
	/* The new AC pending bitmap */
	uint8 traffic_pending_bmp;
	/* 1= send on next opportunity */
	uint8 send_tim_signal;
	uint8 mac_handle;
	/* Number of packets at dongle for this entry. */
	uint transit_count;
	/* Numbe of suppression to wait before evict from delayQ */
	uint suppr_transit_count;
	/* flag. TRUE when in suppress state */
	uint8 suppressed;


#ifdef PROP_TXSTATUS_DEBUG
	uint32 dstncredit_sent_packets;
	uint32 dstncredit_acks;
	uint32 opened_ct;
	uint32 closed_ct;
#endif
	struct wlfc_mac_descriptor* prev;
	struct wlfc_mac_descriptor* next;
} wlfc_mac_descriptor_t;

typedef struct dhd_wlfc_commit_info {
	uint8					needs_hdr;
	uint8					ac_fifo_credit_spent;
	ewlfc_packet_state_t	pkt_type;
	wlfc_mac_descriptor_t*	mac_entry;
	void*					p;
} dhd_wlfc_commit_info_t;

#define WLFC_DECR_SEQCOUNT(entry, prec) do { if (entry->seq[(prec)] == 0) {\
	entry->seq[prec] = 0xff; } else entry->seq[prec]--;} while (0)

#define WLFC_INCR_SEQCOUNT(entry, prec) entry->seq[(prec)]++
#define WLFC_SEQCOUNT(entry, prec) entry->seq[(prec)]

typedef struct athost_wl_stat_counters {
	uint32	pktin;
	uint32	pktout;
	uint32	pkt2bus;
	uint32	pktdropped;
	uint32	tlv_parse_failed;
	uint32	rollback;
	uint32	rollback_failed;
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
	uint32	send_pkts[AC_COUNT + 1];
	uint32	drop_pkts[WLFC_PSQ_PREC_COUNT];
	uint32	ooo_pkts[AC_COUNT + 1];
#ifdef PROP_TXSTATUS_DEBUG
	/* all pkt2bus -> txstatus latency accumulated */
	uint32	latency_sample_count;
	uint32	total_status_latency;
	uint32	latency_most_recent;
	int	idx_delta;
	uint32	deltas[10];
	uint32	fifo_credits_sent[6];
	uint32	fifo_credits_back[6];
	uint32	dropped_qfull[6];
	uint32	signal_only_pkts_sent;
	uint32	signal_only_pkts_freed;
#endif
	uint32	cleanup_txq_cnt;
	uint32	cleanup_psq_cnt;
	uint32	cleanup_fw_cnt;
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
#define WLFC_ONLY_AMPDU_HOSTREORDER		3

/* How long to defer borrowing in milliseconds */
#define WLFC_BORROW_DEFER_PERIOD_MS 100

/* How long to defer flow control in milliseconds */
#define WLFC_FC_DEFER_PERIOD_MS 200

/* How long to detect occurance per AC in miliseconds */
#define WLFC_RX_DETECTION_THRESHOLD_MS	100

/* Mask to represent available ACs (note: BC/MC is ignored */
#define WLFC_AC_MASK 0xF

typedef struct athost_wl_status_info {
	uint8	last_seqid_to_wlc;

	/* OSL handle */
	osl_t*	osh;
	/* dhd pub */
	void*	dhdp;

	/* stats */
	athost_wl_stat_counters_t stats;

	int		Init_FIFO_credit[AC_COUNT + 2];

	/* the additional ones are for bc/mc and ATIM FIFO */
	int		FIFO_credit[AC_COUNT + 2];

	/* Credit borrow counts for each FIFO from each of the other FIFOs */
	int		credits_borrowed[AC_COUNT + 2][AC_COUNT + 2];

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

	wlfc_mac_descriptor_t *active_entry_head;
	int active_entry_count;

	wlfc_mac_descriptor_t* requested_entry[WLFC_MAC_DESC_TABLE_SIZE];
	int requested_entry_count;

	/* pkt counts for each interface and ac */
	int	pkt_cnt_in_q[WLFC_MAX_IFNUM][AC_COUNT+1];
	int	pkt_cnt_per_ac[AC_COUNT+1];
	uint8	allow_fc;
	uint32  fc_defer_timestamp;
	uint32	rx_timestamp[AC_COUNT+1];
	/* ON/OFF state for flow control to the host network interface */
	uint8	hostif_flow_state[WLFC_MAX_IFNUM];
	uint8	host_ifidx;
	/* to flow control an OS interface */
	uint8	toggle_host_if;

	/* To borrow credits */
	uint8   allow_credit_borrow;

	/* ac number for the first single ac traffic */
	uint8	single_ac;

	/* Timestamp for the first single ac traffic */
	uint32  single_ac_timestamp;

	bool	bcmc_credit_supported;

} athost_wl_status_info_t;

/* Please be mindful that total pkttag space is 32 octets only */
typedef struct dhd_pkttag {
	/*
	b[15]  - 1 = wlfc packet
	b[14:13]  - encryption exemption
	b[12 ] - 1 = event channel
	b[11 ] - 1 = this packet was sent in response to one time packet request,
	do not increment credit on status for this one. [WLFC_CTL_TYPE_MAC_REQUEST_PACKET].
	b[10 ] - 1 = signal-only-packet to firmware [i.e. nothing to piggyback on]
	b[9  ] - 1 = packet is host->firmware (transmit direction)
	       - 0 = packet received from firmware (firmware->host)
	b[8  ] - 1 = packet was sent due to credit_request (pspoll),
	             packet does not count against FIFO credit.
	       - 0 = normal transaction, packet counts against FIFO credit
	b[7  ] - 1 = AP, 0 = STA
	b[6:4] - AC FIFO number
	b[3:0] - interface index
	*/
	uint16	if_flags;
	/* destination MAC address for this packet so that not every
	module needs to open the packet to find this
	*/
	uint8	dstn_ether[ETHER_ADDR_LEN];
	/*
	This 32-bit goes from host to device for every packet.
	*/
	uint32	htod_tag;

	/*
	This 16-bit is original seq number for every suppress packet.
	*/
	uint16	htod_seq;

	/*
	This address is mac entry for every packet.
	*/
	void*	entry;
	/* bus specific stuff */
	union {
		struct {
			void* stuff;
			uint32 thing1;
			uint32 thing2;
		} sd;
		struct {
			void* bus;
			void* urb;
		} usb;
	} bus_specific;
} dhd_pkttag_t;

#define DHD_PKTTAG_WLFCPKT_MASK			0x1
#define DHD_PKTTAG_WLFCPKT_SHIFT		15
#define DHD_PKTTAG_WLFCPKT_SET(tag, value)	((dhd_pkttag_t*)(tag))->if_flags = \
	(((dhd_pkttag_t*)(tag))->if_flags & \
	~(DHD_PKTTAG_WLFCPKT_MASK << DHD_PKTTAG_WLFCPKT_SHIFT)) | \
	(((value) & DHD_PKTTAG_WLFCPKT_MASK) << DHD_PKTTAG_WLFCPKT_SHIFT)
#define DHD_PKTTAG_WLFCPKT(tag)	((((dhd_pkttag_t*)(tag))->if_flags >> \
	DHD_PKTTAG_WLFCPKT_SHIFT) & DHD_PKTTAG_WLFCPKT_MASK)

#define DHD_PKTTAG_EXEMPT_MASK			0x3
#define DHD_PKTTAG_EXEMPT_SHIFT			13
#define DHD_PKTTAG_EXEMPT_SET(tag, value)	((dhd_pkttag_t*)(tag))->if_flags = \
	(((dhd_pkttag_t*)(tag))->if_flags & \
	~(DHD_PKTTAG_EXEMPT_MASK << DHD_PKTTAG_EXEMPT_SHIFT)) | \
	(((value) & DHD_PKTTAG_EXEMPT_MASK) << DHD_PKTTAG_EXEMPT_SHIFT)
#define DHD_PKTTAG_EXEMPT(tag)	((((dhd_pkttag_t*)(tag))->if_flags >> \
	DHD_PKTTAG_EXEMPT_SHIFT) & DHD_PKTTAG_EXEMPT_MASK)

#define DHD_PKTTAG_EVENT_MASK			0x1
#define DHD_PKTTAG_EVENT_SHIFT			12
#define DHD_PKTTAG_SETEVENT(tag, event)	((dhd_pkttag_t*)(tag))->if_flags = \
	(((dhd_pkttag_t*)(tag))->if_flags & \
	~(DHD_PKTTAG_EVENT_MASK << DHD_PKTTAG_EVENT_SHIFT)) | \
	(((event) & DHD_PKTTAG_EVENT_MASK) << DHD_PKTTAG_EVENT_SHIFT)
#define DHD_PKTTAG_EVENT(tag)	((((dhd_pkttag_t*)(tag))->if_flags >> \
	DHD_PKTTAG_EVENT_SHIFT) & DHD_PKTTAG_EVENT_MASK)

#define DHD_PKTTAG_ONETIMEPKTRQST_MASK		0x1
#define DHD_PKTTAG_ONETIMEPKTRQST_SHIFT		11
#define DHD_PKTTAG_SETONETIMEPKTRQST(tag)	((dhd_pkttag_t*)(tag))->if_flags = \
	(((dhd_pkttag_t*)(tag))->if_flags & \
	~(DHD_PKTTAG_ONETIMEPKTRQST_MASK << DHD_PKTTAG_ONETIMEPKTRQST_SHIFT)) | \
	(1 << DHD_PKTTAG_ONETIMEPKTRQST_SHIFT)
#define DHD_PKTTAG_ONETIMEPKTRQST(tag)	((((dhd_pkttag_t*)(tag))->if_flags >> \
	DHD_PKTTAG_ONETIMEPKTRQST_SHIFT) & DHD_PKTTAG_ONETIMEPKTRQST_MASK)

#define DHD_PKTTAG_SIGNALONLY_MASK		0x1
#define DHD_PKTTAG_SIGNALONLY_SHIFT		10
#define DHD_PKTTAG_SETSIGNALONLY(tag, signalonly)	((dhd_pkttag_t*)(tag))->if_flags = \
	(((dhd_pkttag_t*)(tag))->if_flags & \
	~(DHD_PKTTAG_SIGNALONLY_MASK << DHD_PKTTAG_SIGNALONLY_SHIFT)) | \
	(((signalonly) & DHD_PKTTAG_SIGNALONLY_MASK) << DHD_PKTTAG_SIGNALONLY_SHIFT)
#define DHD_PKTTAG_SIGNALONLY(tag)	((((dhd_pkttag_t*)(tag))->if_flags >> \
	DHD_PKTTAG_SIGNALONLY_SHIFT) & DHD_PKTTAG_SIGNALONLY_MASK)

#define DHD_PKTTAG_PKTDIR_MASK			0x1
#define DHD_PKTTAG_PKTDIR_SHIFT			9
#define DHD_PKTTAG_SETPKTDIR(tag, dir)	((dhd_pkttag_t*)(tag))->if_flags = \
	(((dhd_pkttag_t*)(tag))->if_flags & \
	~(DHD_PKTTAG_PKTDIR_MASK << DHD_PKTTAG_PKTDIR_SHIFT)) | \
	(((dir) & DHD_PKTTAG_PKTDIR_MASK) << DHD_PKTTAG_PKTDIR_SHIFT)
#define DHD_PKTTAG_PKTDIR(tag)	((((dhd_pkttag_t*)(tag))->if_flags >> \
	DHD_PKTTAG_PKTDIR_SHIFT) & DHD_PKTTAG_PKTDIR_MASK)

#define DHD_PKTTAG_CREDITCHECK_MASK		0x1
#define DHD_PKTTAG_CREDITCHECK_SHIFT		8
#define DHD_PKTTAG_SETCREDITCHECK(tag, check)	((dhd_pkttag_t*)(tag))->if_flags = \
	(((dhd_pkttag_t*)(tag))->if_flags & \
	~(DHD_PKTTAG_CREDITCHECK_MASK << DHD_PKTTAG_CREDITCHECK_SHIFT)) | \
	(((check) & DHD_PKTTAG_CREDITCHECK_MASK) << DHD_PKTTAG_CREDITCHECK_SHIFT)
#define DHD_PKTTAG_CREDITCHECK(tag)	((((dhd_pkttag_t*)(tag))->if_flags >> \
	DHD_PKTTAG_CREDITCHECK_SHIFT) & DHD_PKTTAG_CREDITCHECK_MASK)

#define DHD_PKTTAG_IFTYPE_MASK			0x1
#define DHD_PKTTAG_IFTYPE_SHIFT			7
#define DHD_PKTTAG_SETIFTYPE(tag, isAP)	((dhd_pkttag_t*)(tag))->if_flags = \
	(((dhd_pkttag_t*)(tag))->if_flags & \
	~(DHD_PKTTAG_IFTYPE_MASK << DHD_PKTTAG_IFTYPE_SHIFT)) | \
	(((isAP) & DHD_PKTTAG_IFTYPE_MASK) << DHD_PKTTAG_IFTYPE_SHIFT)
#define DHD_PKTTAG_IFTYPE(tag)	((((dhd_pkttag_t*)(tag))->if_flags >> \
	DHD_PKTTAG_IFTYPE_SHIFT) & DHD_PKTTAG_IFTYPE_MASK)

#define DHD_PKTTAG_FIFO_MASK			0x7
#define DHD_PKTTAG_FIFO_SHIFT			4
#define DHD_PKTTAG_SETFIFO(tag, fifo)	((dhd_pkttag_t*)(tag))->if_flags = \
	(((dhd_pkttag_t*)(tag))->if_flags & ~(DHD_PKTTAG_FIFO_MASK << DHD_PKTTAG_FIFO_SHIFT)) | \
	(((fifo) & DHD_PKTTAG_FIFO_MASK) << DHD_PKTTAG_FIFO_SHIFT)
#define DHD_PKTTAG_FIFO(tag)		((((dhd_pkttag_t*)(tag))->if_flags >> \
	DHD_PKTTAG_FIFO_SHIFT) & DHD_PKTTAG_FIFO_MASK)

#define DHD_PKTTAG_IF_MASK			0xf
#define DHD_PKTTAG_IF_SHIFT			0
#define DHD_PKTTAG_SETIF(tag, if)	((dhd_pkttag_t*)(tag))->if_flags = \
	(((dhd_pkttag_t*)(tag))->if_flags & ~(DHD_PKTTAG_IF_MASK << DHD_PKTTAG_IF_SHIFT)) | \
	(((if) & DHD_PKTTAG_IF_MASK) << DHD_PKTTAG_IF_SHIFT)
#define DHD_PKTTAG_IF(tag)		((((dhd_pkttag_t*)(tag))->if_flags >> \
	DHD_PKTTAG_IF_SHIFT) & DHD_PKTTAG_IF_MASK)

#define DHD_PKTTAG_SETDSTN(tag, dstn_MAC_ea)	memcpy(((dhd_pkttag_t*)((tag)))->dstn_ether, \
	(dstn_MAC_ea), ETHER_ADDR_LEN)
#define DHD_PKTTAG_DSTN(tag)	((dhd_pkttag_t*)(tag))->dstn_ether

#define DHD_PKTTAG_SET_H2DTAG(tag, h2dvalue)	((dhd_pkttag_t*)(tag))->htod_tag = (h2dvalue)
#define DHD_PKTTAG_H2DTAG(tag)			(((dhd_pkttag_t*)(tag))->htod_tag)

#define DHD_PKTTAG_SET_H2DSEQ(tag, seq)		((dhd_pkttag_t*)(tag))->htod_seq = (seq)
#define DHD_PKTTAG_H2DSEQ(tag)			(((dhd_pkttag_t*)(tag))->htod_seq)

#define DHD_PKTTAG_SET_ENTRY(tag, entry)	((dhd_pkttag_t*)(tag))->entry = (entry)
#define DHD_PKTTAG_ENTRY(tag)			(((dhd_pkttag_t*)(tag))->entry)

#define PSQ_SUP_IDX(x) (x * 2 + 1)
#define PSQ_DLY_IDX(x) (x * 2)

typedef int (*f_commitpkt_t)(void* ctx, void* p);
typedef bool (*f_processpkt_t)(void* p, void* arg);

#ifdef PROP_TXSTATUS_DEBUG
#define DHD_WLFC_CTRINC_MAC_CLOSE(entry)	do { (entry)->closed_ct++; } while (0)
#define DHD_WLFC_CTRINC_MAC_OPEN(entry)		do { (entry)->opened_ct++; } while (0)
#else
#define DHD_WLFC_CTRINC_MAC_CLOSE(entry)	do {} while (0)
#define DHD_WLFC_CTRINC_MAC_OPEN(entry)		do {} while (0)
#endif

/* public functions */
int dhd_wlfc_parse_header_info(dhd_pub_t *dhd, void* pktbuf, int tlv_hdr_len,
	uchar *reorder_info_buf, uint *reorder_info_len);
int dhd_wlfc_commit_packets(dhd_pub_t *dhdp, f_commitpkt_t fcommit,
	void* commit_ctx, void *pktbuf, bool need_toggle_host_if);
int dhd_wlfc_txcomplete(dhd_pub_t *dhd, void *txp, bool success);
int dhd_wlfc_init(dhd_pub_t *dhd);
int dhd_wlfc_hostreorder_init(dhd_pub_t *dhd);
#ifdef SUPPORT_P2P_GO_PS
int dhd_wlfc_suspend(dhd_pub_t *dhd);
int dhd_wlfc_resume(dhd_pub_t *dhd);
#endif /* SUPPORT_P2P_GO_PS */
int dhd_wlfc_cleanup_txq(dhd_pub_t *dhd, f_processpkt_t fn, void *arg);
int dhd_wlfc_cleanup(dhd_pub_t *dhd, f_processpkt_t fn, void* arg);
int dhd_wlfc_deinit(dhd_pub_t *dhd);
int dhd_wlfc_interface_event(dhd_pub_t *dhdp, uint8 action, uint8 ifid, uint8 iftype, uint8* ea);
int dhd_wlfc_FIFOcreditmap_event(dhd_pub_t *dhdp, uint8* event_data);
int dhd_wlfc_BCMCCredit_support_event(dhd_pub_t *dhdp);
int dhd_wlfc_enable(dhd_pub_t *dhdp);
int dhd_wlfc_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf);
int dhd_wlfc_clear_counts(dhd_pub_t *dhd);
int dhd_wlfc_get_enable(dhd_pub_t *dhd, bool *val);
int dhd_wlfc_get_mode(dhd_pub_t *dhd, int *val);
int dhd_wlfc_set_mode(dhd_pub_t *dhd, int val);
bool dhd_wlfc_is_supported(dhd_pub_t *dhd);
bool dhd_wlfc_is_header_only_pkt(dhd_pub_t * dhd, void *pktbuf);
int dhd_wlfc_flowcontrol(dhd_pub_t *dhdp, bool state, bool bAcquireLock);
int dhd_wlfc_save_rxpath_ac_time(dhd_pub_t * dhd, uint8 prio);

int dhd_wlfc_get_module_ignore(dhd_pub_t *dhd, int *val);
int dhd_wlfc_set_module_ignore(dhd_pub_t *dhd, int val);
int dhd_wlfc_get_credit_ignore(dhd_pub_t *dhd, int *val);
int dhd_wlfc_set_credit_ignore(dhd_pub_t *dhd, int val);
int dhd_wlfc_get_txstatus_ignore(dhd_pub_t *dhd, int *val);
int dhd_wlfc_set_txstatus_ignore(dhd_pub_t *dhd, int val);

int dhd_wlfc_get_rxpkt_chk(dhd_pub_t *dhd, int *val);
int dhd_wlfc_set_rxpkt_chk(dhd_pub_t *dhd, int val);
#endif /* __wlfc_host_driver_definitions_h__ */
