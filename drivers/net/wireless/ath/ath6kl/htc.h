/*
 * Copyright (c) 2004-2011 Atheros Communications Inc.
 * Copyright (c) 2011 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef HTC_H
#define HTC_H

#include "common.h"

/* frame header flags */

/* send direction */
#define HTC_FLAGS_NEED_CREDIT_UPDATE (1 << 0)
#define HTC_FLAGS_SEND_BUNDLE        (1 << 1)
#define HTC_FLAGS_TX_FIXUP_NETBUF    (1 << 2)

/* receive direction */
#define HTC_FLG_RX_UNUSED        (1 << 0)
#define HTC_FLG_RX_TRAILER       (1 << 1)
/* Bundle count maske and shift */
#define HTC_FLG_RX_BNDL_CNT	 (0xF0)
#define HTC_FLG_RX_BNDL_CNT_S	 4

#define HTC_HDR_LENGTH  (sizeof(struct htc_frame_hdr))
#define HTC_MAX_PAYLOAD_LENGTH   (4096 - sizeof(struct htc_frame_hdr))

/* HTC control message IDs */

#define HTC_MSG_READY_ID		1
#define HTC_MSG_CONN_SVC_ID		2
#define HTC_MSG_CONN_SVC_RESP_ID	3
#define HTC_MSG_SETUP_COMPLETE_ID	4
#define HTC_MSG_SETUP_COMPLETE_EX_ID	5

#define HTC_MAX_CTRL_MSG_LEN  256

#define HTC_VERSION_2P0  0x00
#define HTC_VERSION_2P1  0x01

#define HTC_SERVICE_META_DATA_MAX_LENGTH 128

#define HTC_CONN_FLGS_THRESH_LVL_QUAT		0x0
#define HTC_CONN_FLGS_THRESH_LVL_HALF		0x1
#define HTC_CONN_FLGS_THRESH_LVL_THREE_QUAT	0x2
#define HTC_CONN_FLGS_REDUCE_CRED_DRIB		0x4
#define HTC_CONN_FLGS_THRESH_MASK		0x3
/* disable credit flow control on a specific service */
#define HTC_CONN_FLGS_DISABLE_CRED_FLOW_CTRL          (1 << 3)
#define HTC_CONN_FLGS_SET_RECV_ALLOC_SHIFT    8
#define HTC_CONN_FLGS_SET_RECV_ALLOC_MASK     0xFF00

/* connect response status codes */
#define HTC_SERVICE_SUCCESS      0
#define HTC_SERVICE_NOT_FOUND    1
#define HTC_SERVICE_FAILED       2

/* no resources (i.e. no more endpoints) */
#define HTC_SERVICE_NO_RESOURCES 3

/* specific service is not allowing any more endpoints */
#define HTC_SERVICE_NO_MORE_EP   4

/* report record IDs */
#define HTC_RECORD_NULL             0
#define HTC_RECORD_CREDITS          1
#define HTC_RECORD_LOOKAHEAD        2
#define HTC_RECORD_LOOKAHEAD_BUNDLE 3

#define HTC_SETUP_COMP_FLG_RX_BNDL_EN     (1 << 0)
#define HTC_SETUP_COMP_FLG_DISABLE_TX_CREDIT_FLOW (1 << 1)

#define MAKE_SERVICE_ID(group, index) \
	(int)(((int)group << 8) | (int)(index))

/* NOTE: service ID of 0x0000 is reserved and should never be used */
#define HTC_CTRL_RSVD_SVC MAKE_SERVICE_ID(RSVD_SERVICE_GROUP, 1)
#define WMI_CONTROL_SVC   MAKE_SERVICE_ID(WMI_SERVICE_GROUP, 0)
#define WMI_DATA_BE_SVC   MAKE_SERVICE_ID(WMI_SERVICE_GROUP, 1)
#define WMI_DATA_BK_SVC   MAKE_SERVICE_ID(WMI_SERVICE_GROUP, 2)
#define WMI_DATA_VI_SVC   MAKE_SERVICE_ID(WMI_SERVICE_GROUP, 3)
#define WMI_DATA_VO_SVC   MAKE_SERVICE_ID(WMI_SERVICE_GROUP, 4)
#define WMI_MAX_SERVICES  5

#define WMM_NUM_AC  4

/* reserved and used to flush ALL packets */
#define HTC_TX_PACKET_TAG_ALL          0
#define HTC_SERVICE_TX_PACKET_TAG      1
#define HTC_TX_PACKET_TAG_USER_DEFINED (HTC_SERVICE_TX_PACKET_TAG + 9)

/* more packets on this endpoint are being fetched */
#define HTC_RX_FLAGS_INDICATE_MORE_PKTS  (1 << 0)

/* TODO.. for BMI */
#define ENDPOINT1 0
/* TODO -remove me, but we have to fix BMI first */
#define HTC_MAILBOX_NUM_MAX    4

/* enable send bundle padding for this endpoint */
#define HTC_FLGS_TX_BNDL_PAD_EN	 (1 << 0)
#define HTC_EP_ACTIVE                            ((u32) (1u << 31))

/* HTC operational parameters */
#define HTC_TARGET_RESPONSE_TIMEOUT        2000	/* in ms */
#define HTC_TARGET_RESPONSE_POLL_WAIT      10
#define HTC_TARGET_RESPONSE_POLL_COUNT     200
#define HTC_TARGET_DEBUG_INTR_MASK         0x01
#define HTC_TARGET_CREDIT_INTR_MASK        0xF0

#define HTC_HOST_MAX_MSG_PER_BUNDLE        8
#define HTC_MIN_HTC_MSGS_TO_BUNDLE         2

/* packet flags */

#define HTC_RX_PKT_IGNORE_LOOKAHEAD      (1 << 0)
#define HTC_RX_PKT_REFRESH_HDR           (1 << 1)
#define HTC_RX_PKT_PART_OF_BUNDLE        (1 << 2)
#define HTC_RX_PKT_NO_RECYCLE            (1 << 3)

#define NUM_CONTROL_BUFFERS     8
#define NUM_CONTROL_TX_BUFFERS  2
#define NUM_CONTROL_RX_BUFFERS  (NUM_CONTROL_BUFFERS - NUM_CONTROL_TX_BUFFERS)

#define HTC_RECV_WAIT_BUFFERS        (1 << 0)
#define HTC_OP_STATE_STOPPING        (1 << 0)
#define HTC_OP_STATE_SETUP_COMPLETE  (1 << 1)

/*
 * The frame header length and message formats defined herein were selected
 * to accommodate optimal alignment for target processing. This reduces
 * code size and improves performance. Any changes to the header length may
 * alter the alignment and cause exceptions on the target. When adding to
 * the messagestructures insure that fields are properly aligned.
 */

/* HTC frame header
 *
 * NOTE: do not remove or re-arrange the fields, these are minimally
 * required to take advantage of 4-byte lookaheads in some hardware
 * implementations.
 */
struct htc_frame_hdr {
	u8 eid;
	u8 flags;

	/* length of data (including trailer) that follows the header */
	__le16 payld_len;

	/* end of 4-byte lookahead */

	u8 ctrl[2];
} __packed;

/* HTC ready message */
struct htc_ready_msg {
	__le16 msg_id;
	__le16 cred_cnt;
	__le16 cred_sz;
	u8 max_ep;
	u8 pad;
} __packed;

/* extended HTC ready message */
struct htc_ready_ext_msg {
	struct htc_ready_msg ver2_0_info;
	u8 htc_ver;
	u8 msg_per_htc_bndl;
} __packed;

/* connect service */
struct htc_conn_service_msg {
	__le16 msg_id;
	__le16 svc_id;
	__le16 conn_flags;
	u8 svc_meta_len;
	u8 pad;
} __packed;

/* connect response */
struct htc_conn_service_resp {
	__le16 msg_id;
	__le16 svc_id;
	u8 status;
	u8 eid;
	__le16 max_msg_sz;
	u8 svc_meta_len;
	u8 pad;
} __packed;

struct htc_setup_comp_msg {
	__le16 msg_id;
} __packed;

/* extended setup completion message */
struct htc_setup_comp_ext_msg {
	__le16 msg_id;
	__le32 flags;
	u8 msg_per_rxbndl;
	u8 Rsvd[3];
} __packed;

struct htc_record_hdr {
	u8 rec_id;
	u8 len;
} __packed;

struct htc_credit_report {
	u8 eid;
	u8 credits;
} __packed;

/*
 * NOTE: The lk_ahd array is guarded by a pre_valid
 * and Post Valid guard bytes. The pre_valid bytes must
 * equal the inverse of the post_valid byte.
 */
struct htc_lookahead_report {
	u8 pre_valid;
	u8 lk_ahd[4];
	u8 post_valid;
} __packed;

struct htc_bundle_lkahd_rpt {
	u8 lk_ahd[4];
} __packed;

/* Current service IDs */

enum htc_service_grp_ids {
	RSVD_SERVICE_GROUP = 0,
	WMI_SERVICE_GROUP = 1,

	HTC_TEST_GROUP = 254,
	HTC_SERVICE_GROUP_LAST = 255
};

/* ------ endpoint IDS ------ */

enum htc_endpoint_id {
	ENDPOINT_UNUSED = -1,
	ENDPOINT_0 = 0,
	ENDPOINT_1 = 1,
	ENDPOINT_2 = 2,
	ENDPOINT_3,
	ENDPOINT_4,
	ENDPOINT_5,
	ENDPOINT_6,
	ENDPOINT_7,
	ENDPOINT_8,
	ENDPOINT_MAX,
};

struct htc_tx_packet_info {
	u16 tag;
	int cred_used;
	u8 flags;
	int seqno;
};

struct htc_rx_packet_info {
	u32 exp_hdr;
	u32 rx_flags;
	u32 indicat_flags;
};

struct htc_target;

/* wrapper around endpoint-specific packets */
struct htc_packet {
	struct list_head list;

	/* caller's per packet specific context */
	void *pkt_cntxt;

	/*
	 * the true buffer start , the caller can store the real
	 * buffer start here.  In receive callbacks, the HTC layer
	 * sets buf to the start of the payload past the header.
	 * This field allows the caller to reset buf when it recycles
	 * receive packets back to HTC.
	 */
	u8 *buf_start;

	/*
	 * Pointer to the start of the buffer. In the transmit
	 * direction this points to the start of the payload. In the
	 * receive direction, however, the buffer when queued up
	 * points to the start of the HTC header but when returned
	 * to the caller points to the start of the payload
	 */
	u8 *buf;
	u32 buf_len;

	/* actual length of payload */
	u32 act_len;

	/* endpoint that this packet was sent/recv'd from */
	enum htc_endpoint_id endpoint;

	/* completion status */

	int status;
	union {
		struct htc_tx_packet_info tx;
		struct htc_rx_packet_info rx;
	} info;

	void (*completion) (struct htc_target *, struct htc_packet *);
	struct htc_target *context;

	/*
	 * optimization for network-oriented data, the HTC packet
	 * can pass the network buffer corresponding to the HTC packet
	 * lower layers may optimized the transfer knowing this is
	 * a network buffer
	 */
	struct sk_buff *skb;
};

enum htc_send_full_action {
	HTC_SEND_FULL_KEEP = 0,
	HTC_SEND_FULL_DROP = 1,
};

struct htc_ep_callbacks {
	void (*tx_complete) (struct htc_target *, struct htc_packet *);
	void (*rx) (struct htc_target *, struct htc_packet *);
	void (*rx_refill) (struct htc_target *, enum htc_endpoint_id endpoint);
	enum htc_send_full_action (*tx_full) (struct htc_target *,
					      struct htc_packet *);
	struct htc_packet *(*rx_allocthresh) (struct htc_target *,
					      enum htc_endpoint_id, int);
	void (*tx_comp_multi) (struct htc_target *, struct list_head *);
	int rx_alloc_thresh;
	int rx_refill_thresh;
};

/* service connection information */
struct htc_service_connect_req {
	u16 svc_id;
	u16 conn_flags;
	struct htc_ep_callbacks ep_cb;
	int max_txq_depth;
	u32 flags;
	unsigned int max_rxmsg_sz;
};

/* service connection response information */
struct htc_service_connect_resp {
	u8 buf_len;
	u8 act_len;
	enum htc_endpoint_id endpoint;
	unsigned int len_max;
	u8 resp_code;
};

/* endpoint distributionstructure */
struct htc_endpoint_credit_dist {
	struct list_head list;

	/* Service ID (set by HTC) */
	u16 svc_id;

	/* endpoint for this distributionstruct (set by HTC) */
	enum htc_endpoint_id endpoint;

	u32 dist_flags;

	/*
	 * credits for normal operation, anything above this
	 * indicates the endpoint is over-subscribed.
	 */
	int cred_norm;

	/* floor for credit distribution */
	int cred_min;

	int cred_assngd;

	/* current credits available */
	int credits;

	/*
	 * pending credits to distribute on this endpoint, this
	 * is set by HTC when credit reports arrive.  The credit
	 * distribution functions sets this to zero when it distributes
	 * the credits.
	 */
	int cred_to_dist;

	/*
	 * the number of credits that the current pending TX packet needs
	 * to transmit. This is set by HTC when endpoint needs credits in
	 * order to transmit.
	 */
	int seek_cred;

	/* size in bytes of each credit */
	int cred_sz;

	/* credits required for a maximum sized messages */
	int cred_per_msg;

	/* reserved for HTC use */
	struct htc_endpoint *htc_ep;

	/*
	 * current depth of TX queue , i.e. messages waiting for credits
	 * This field is valid only when HTC_CREDIT_DIST_ACTIVITY_CHANGE
	 * or HTC_CREDIT_DIST_SEND_COMPLETE is indicated on an endpoint
	 * that has non-zero credits to recover.
	 */
	int txq_depth;
};

/*
 * credit distibution code that is passed into the distrbution function,
 * there are mandatory and optional codes that must be handled
 */
enum htc_credit_dist_reason {
	HTC_CREDIT_DIST_SEND_COMPLETE = 0,
	HTC_CREDIT_DIST_ACTIVITY_CHANGE = 1,
	HTC_CREDIT_DIST_SEEK_CREDITS,
};

struct ath6kl_htc_credit_info {
	int total_avail_credits;
	int cur_free_credits;

	/* list of lowest priority endpoints */
	struct list_head lowestpri_ep_dist;
};

/* endpoint statistics */
struct htc_endpoint_stats {
	/*
	 * number of times the host set the credit-low flag in a send
	 * message on this endpoint
	 */
	u32 cred_low_indicate;

	u32 tx_issued;
	u32 tx_pkt_bundled;
	u32 tx_bundles;
	u32 tx_dropped;

	/* running count of total credit reports received for this endpoint */
	u32 tx_cred_rpt;

	/* credit reports received from this endpoint's RX packets */
	u32 cred_rpt_from_rx;

	/* credit reports received from RX packets of other endpoints */
	u32 cred_rpt_from_other;

	/* credit reports received from endpoint 0 RX packets */
	u32 cred_rpt_ep0;

	/* count of credits received via Rx packets on this endpoint */
	u32 cred_from_rx;

	/* count of credits received via another endpoint */
	u32 cred_from_other;

	/* count of credits received via another endpoint */
	u32 cred_from_ep0;

	/* count of consummed credits */
	u32 cred_cosumd;

	/* count of credits returned */
	u32 cred_retnd;

	u32 rx_pkts;

	/* count of lookahead records found in Rx msg */
	u32 rx_lkahds;

	/* count of recv packets received in a bundle */
	u32 rx_bundl;

	/* count of number of bundled lookaheads */
	u32 rx_bundle_lkahd;

	/* count of the number of bundle indications from the HTC header */
	u32 rx_bundle_from_hdr;

	/* the number of times the recv allocation threshold was hit */
	u32 rx_alloc_thresh_hit;

	/* total number of bytes */
	u32 rxalloc_thresh_byte;
};

struct htc_endpoint {
	enum htc_endpoint_id eid;
	u16 svc_id;
	struct list_head txq;
	struct list_head rx_bufq;
	struct htc_endpoint_credit_dist cred_dist;
	struct htc_ep_callbacks ep_cb;
	int max_txq_depth;
	int len_max;
	int tx_proc_cnt;
	int rx_proc_cnt;
	struct htc_target *target;
	u8 seqno;
	u32 conn_flags;
	struct htc_endpoint_stats ep_st;
	u16 tx_drop_packet_threshold;

	struct {
		u8 pipeid_ul;
		u8 pipeid_dl;
		struct list_head tx_lookup_queue;
		bool tx_credit_flow_enabled;
	} pipe;
};

struct htc_control_buffer {
	struct htc_packet packet;
	u8 *buf;
};

struct htc_pipe_txcredit_alloc {
	u16 service_id;
	u8 credit_alloc;
};

enum htc_send_queue_result {
	HTC_SEND_QUEUE_OK = 0,	/* packet was queued */
	HTC_SEND_QUEUE_DROP = 1,	/* this packet should be dropped */
};

struct ath6kl_htc_ops {
	void* (*create)(struct ath6kl *ar);
	int (*wait_target)(struct htc_target *target);
	int (*start)(struct htc_target *target);
	int (*conn_service)(struct htc_target *target,
			    struct htc_service_connect_req *req,
			    struct htc_service_connect_resp *resp);
	int  (*tx)(struct htc_target *target, struct htc_packet *packet);
	void (*stop)(struct htc_target *target);
	void (*cleanup)(struct htc_target *target);
	void (*flush_txep)(struct htc_target *target,
			   enum htc_endpoint_id endpoint, u16 tag);
	void (*flush_rx_buf)(struct htc_target *target);
	void (*activity_changed)(struct htc_target *target,
				 enum htc_endpoint_id endpoint,
				 bool active);
	int (*get_rxbuf_num)(struct htc_target *target,
			     enum htc_endpoint_id endpoint);
	int (*add_rxbuf_multiple)(struct htc_target *target,
				  struct list_head *pktq);
	int (*credit_setup)(struct htc_target *target,
			    struct ath6kl_htc_credit_info *cred_info);
	int (*tx_complete)(struct ath6kl *ar, struct sk_buff *skb);
	int (*rx_complete)(struct ath6kl *ar, struct sk_buff *skb, u8 pipe);
};

struct ath6kl_device;

/* our HTC target state */
struct htc_target {
	struct htc_endpoint endpoint[ENDPOINT_MAX];

	/* contains struct htc_endpoint_credit_dist */
	struct list_head cred_dist_list;

	struct list_head free_ctrl_txbuf;
	struct list_head free_ctrl_rxbuf;
	struct ath6kl_htc_credit_info *credit_info;
	int tgt_creds;
	unsigned int tgt_cred_sz;

	/* protects free_ctrl_txbuf and free_ctrl_rxbuf */
	spinlock_t htc_lock;

	/* FIXME: does this protext rx_bufq and endpoint structures or what? */
	spinlock_t rx_lock;

	/* protects endpoint->txq */
	spinlock_t tx_lock;

	struct ath6kl_device *dev;
	u32 htc_flags;
	u32 rx_st_flags;
	enum htc_endpoint_id ep_waiting;
	u8 htc_tgt_ver;

	/* max messages per bundle for HTC */
	int msg_per_bndl_max;

	u32 tx_bndl_mask;
	int rx_bndl_enable;
	int max_rx_bndl_sz;
	int max_tx_bndl_sz;

	u32 block_sz;
	u32 block_mask;

	int max_scat_entries;
	int max_xfer_szper_scatreq;

	int chk_irq_status_cnt;

	/* counts the number of Tx without bundling continously per AC */
	u32 ac_tx_count[WMM_NUM_AC];

	struct {
		struct htc_packet *htc_packet_pool;
		u8 ctrl_response_buf[HTC_MAX_CTRL_MSG_LEN];
		int ctrl_response_len;
		bool ctrl_response_valid;
		struct htc_pipe_txcredit_alloc txcredit_alloc[ENDPOINT_MAX];
	} pipe;
};

int ath6kl_htc_rxmsg_pending_handler(struct htc_target *target,
				     u32 msg_look_ahead, int *n_pkts);

static inline void set_htc_pkt_info(struct htc_packet *packet, void *context,
				    u8 *buf, unsigned int len,
				    enum htc_endpoint_id eid, u16 tag)
{
	packet->pkt_cntxt = context;
	packet->buf = buf;
	packet->act_len = len;
	packet->endpoint = eid;
	packet->info.tx.tag = tag;
}

static inline void htc_rxpkt_reset(struct htc_packet *packet)
{
	packet->buf = packet->buf_start;
	packet->act_len = 0;
}

static inline void set_htc_rxpkt_info(struct htc_packet *packet, void *context,
				      u8 *buf, unsigned long len,
				      enum htc_endpoint_id eid)
{
	packet->pkt_cntxt = context;
	packet->buf = buf;
	packet->buf_start = buf;
	packet->buf_len = len;
	packet->endpoint = eid;
}

static inline int get_queue_depth(struct list_head *queue)
{
	struct list_head *tmp_list;
	int depth = 0;

	list_for_each(tmp_list, queue)
	    depth++;

	return depth;
}

void ath6kl_htc_pipe_attach(struct ath6kl *ar);
void ath6kl_htc_mbox_attach(struct ath6kl *ar);

#endif
