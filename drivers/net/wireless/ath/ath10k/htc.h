/* SPDX-License-Identifier: ISC */
/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2016 Qualcomm Atheros, Inc.
 */

#ifndef _HTC_H_
#define _HTC_H_

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/bug.h>
#include <linux/skbuff.h>
#include <linux/timer.h>

struct ath10k;

/****************/
/* HTC protocol */
/****************/

/*
 * HTC - host-target control protocol
 *
 * tx packets are generally <htc_hdr><payload>
 * rx packets are more complex: <htc_hdr><payload><trailer>
 *
 * The payload + trailer length is stored in len.
 * To get payload-only length one needs to payload - trailer_len.
 *
 * Trailer contains (possibly) multiple <htc_record>.
 * Each record is a id-len-value.
 *
 * HTC header flags, control_byte0, control_byte1
 * have different meaning depending whether its tx
 * or rx.
 *
 * Alignment: htc_hdr, payload and trailer are
 * 4-byte aligned.
 */

#define HTC_HOST_MAX_MSG_PER_RX_BUNDLE        8

enum ath10k_htc_tx_flags {
	ATH10K_HTC_FLAG_NEED_CREDIT_UPDATE = 0x01,
	ATH10K_HTC_FLAG_SEND_BUNDLE        = 0x02
};

enum ath10k_htc_rx_flags {
	ATH10K_HTC_FLAGS_RECV_1MORE_BLOCK = 0x01,
	ATH10K_HTC_FLAG_TRAILER_PRESENT = 0x02,
	ATH10K_HTC_FLAG_BUNDLE_MASK     = 0xF0
};

struct ath10k_htc_hdr {
	u8 eid; /* @enum ath10k_htc_ep_id */
	u8 flags; /* @enum ath10k_htc_tx_flags, ath10k_htc_rx_flags */
	__le16 len;
	union {
		u8 trailer_len; /* for rx */
		u8 control_byte0;
	} __packed;
	union {
		u8 seq_no; /* for tx */
		u8 control_byte1;
	} __packed;
	u8 pad0;
	u8 pad1;
} __packed __aligned(4);

enum ath10k_ath10k_htc_msg_id {
	ATH10K_HTC_MSG_READY_ID                = 1,
	ATH10K_HTC_MSG_CONNECT_SERVICE_ID      = 2,
	ATH10K_HTC_MSG_CONNECT_SERVICE_RESP_ID = 3,
	ATH10K_HTC_MSG_SETUP_COMPLETE_ID       = 4,
	ATH10K_HTC_MSG_SETUP_COMPLETE_EX_ID    = 5,
	ATH10K_HTC_MSG_SEND_SUSPEND_COMPLETE   = 6
};

enum ath10k_htc_version {
	ATH10K_HTC_VERSION_2P0 = 0x00, /* 2.0 */
	ATH10K_HTC_VERSION_2P1 = 0x01, /* 2.1 */
};

enum ath10k_htc_conn_flags {
	ATH10K_HTC_CONN_FLAGS_THRESHOLD_LEVEL_ONE_FOURTH    = 0x0,
	ATH10K_HTC_CONN_FLAGS_THRESHOLD_LEVEL_ONE_HALF      = 0x1,
	ATH10K_HTC_CONN_FLAGS_THRESHOLD_LEVEL_THREE_FOURTHS = 0x2,
	ATH10K_HTC_CONN_FLAGS_THRESHOLD_LEVEL_UNITY         = 0x3,
#define ATH10K_HTC_CONN_FLAGS_THRESHOLD_LEVEL_MASK 0x3
	ATH10K_HTC_CONN_FLAGS_REDUCE_CREDIT_DRIBBLE    = 1 << 2,
	ATH10K_HTC_CONN_FLAGS_DISABLE_CREDIT_FLOW_CTRL = 1 << 3
#define ATH10K_HTC_CONN_FLAGS_RECV_ALLOC_MASK 0xFF00
#define ATH10K_HTC_CONN_FLAGS_RECV_ALLOC_LSB  8
};

enum ath10k_htc_conn_svc_status {
	ATH10K_HTC_CONN_SVC_STATUS_SUCCESS      = 0,
	ATH10K_HTC_CONN_SVC_STATUS_NOT_FOUND    = 1,
	ATH10K_HTC_CONN_SVC_STATUS_FAILED       = 2,
	ATH10K_HTC_CONN_SVC_STATUS_NO_RESOURCES = 3,
	ATH10K_HTC_CONN_SVC_STATUS_NO_MORE_EP   = 4
};

enum ath10k_htc_setup_complete_flags {
	ATH10K_HTC_SETUP_COMPLETE_FLAGS_RX_BNDL_EN = 1
};

struct ath10k_ath10k_htc_msg_hdr {
	__le16 message_id; /* @enum htc_message_id */
} __packed;

struct ath10k_htc_unknown {
	u8 pad0;
	u8 pad1;
} __packed;

struct ath10k_htc_ready {
	__le16 credit_count;
	__le16 credit_size;
	u8 max_endpoints;
	u8 pad0;
} __packed;

struct ath10k_htc_ready_extended {
	struct ath10k_htc_ready base;
	u8 htc_version; /* @enum ath10k_htc_version */
	u8 max_msgs_per_htc_bundle;
	u8 pad0;
	u8 pad1;
} __packed;

struct ath10k_htc_conn_svc {
	__le16 service_id;
	__le16 flags; /* @enum ath10k_htc_conn_flags */
	u8 pad0;
	u8 pad1;
} __packed;

struct ath10k_htc_conn_svc_response {
	__le16 service_id;
	u8 status; /* @enum ath10k_htc_conn_svc_status */
	u8 eid;
	__le16 max_msg_size;
} __packed;

struct ath10k_htc_setup_complete_extended {
	u8 pad0;
	u8 pad1;
	__le32 flags; /* @enum htc_setup_complete_flags */
	u8 max_msgs_per_bundled_recv;
	u8 pad2;
	u8 pad3;
	u8 pad4;
} __packed;

struct ath10k_htc_msg {
	struct ath10k_ath10k_htc_msg_hdr hdr;
	union {
		/* host-to-target */
		struct ath10k_htc_conn_svc connect_service;
		struct ath10k_htc_ready ready;
		struct ath10k_htc_ready_extended ready_ext;
		struct ath10k_htc_unknown unknown;
		struct ath10k_htc_setup_complete_extended setup_complete_ext;

		/* target-to-host */
		struct ath10k_htc_conn_svc_response connect_service_response;
	};
} __packed __aligned(4);

enum ath10k_ath10k_htc_record_id {
	ATH10K_HTC_RECORD_NULL             = 0,
	ATH10K_HTC_RECORD_CREDITS          = 1,
	ATH10K_HTC_RECORD_LOOKAHEAD        = 2,
	ATH10K_HTC_RECORD_LOOKAHEAD_BUNDLE = 3,
};

struct ath10k_ath10k_htc_record_hdr {
	u8 id; /* @enum ath10k_ath10k_htc_record_id */
	u8 len;
	u8 pad0;
	u8 pad1;
} __packed;

struct ath10k_htc_credit_report {
	u8 eid; /* @enum ath10k_htc_ep_id */
	u8 credits;
	u8 pad0;
	u8 pad1;
} __packed;

struct ath10k_htc_lookahead_report {
	u8 pre_valid;
	u8 pad0;
	u8 pad1;
	u8 pad2;
	u8 lookahead[4];
	u8 post_valid;
	u8 pad3;
	u8 pad4;
	u8 pad5;
} __packed;

struct ath10k_htc_lookahead_bundle {
	u8 lookahead[4];
} __packed;

struct ath10k_htc_record {
	struct ath10k_ath10k_htc_record_hdr hdr;
	union {
		struct ath10k_htc_credit_report credit_report[0];
		struct ath10k_htc_lookahead_report lookahead_report[0];
		struct ath10k_htc_lookahead_bundle lookahead_bundle[0];
		u8 pauload[0];
	};
} __packed __aligned(4);

/*
 * note: the trailer offset is dynamic depending
 * on payload length. this is only a struct layout draft
 */
struct ath10k_htc_frame {
	struct ath10k_htc_hdr hdr;
	union {
		struct ath10k_htc_msg msg;
		u8 payload[0];
	};
	struct ath10k_htc_record trailer[0];
} __packed __aligned(4);

/*******************/
/* Host-side stuff */
/*******************/

enum ath10k_htc_svc_gid {
	ATH10K_HTC_SVC_GRP_RSVD = 0,
	ATH10K_HTC_SVC_GRP_WMI = 1,
	ATH10K_HTC_SVC_GRP_NMI = 2,
	ATH10K_HTC_SVC_GRP_HTT = 3,
	ATH10K_LOG_SERVICE_GROUP = 6,

	ATH10K_HTC_SVC_GRP_TEST = 254,
	ATH10K_HTC_SVC_GRP_LAST = 255,
};

#define SVC(group, idx) \
	(int)(((int)(group) << 8) | (int)(idx))

enum ath10k_htc_svc_id {
	/* NOTE: service ID of 0x0000 is reserved and should never be used */
	ATH10K_HTC_SVC_ID_RESERVED	= 0x0000,
	ATH10K_HTC_SVC_ID_UNUSED	= ATH10K_HTC_SVC_ID_RESERVED,

	ATH10K_HTC_SVC_ID_RSVD_CTRL	= SVC(ATH10K_HTC_SVC_GRP_RSVD, 1),
	ATH10K_HTC_SVC_ID_WMI_CONTROL	= SVC(ATH10K_HTC_SVC_GRP_WMI, 0),
	ATH10K_HTC_SVC_ID_WMI_DATA_BE	= SVC(ATH10K_HTC_SVC_GRP_WMI, 1),
	ATH10K_HTC_SVC_ID_WMI_DATA_BK	= SVC(ATH10K_HTC_SVC_GRP_WMI, 2),
	ATH10K_HTC_SVC_ID_WMI_DATA_VI	= SVC(ATH10K_HTC_SVC_GRP_WMI, 3),
	ATH10K_HTC_SVC_ID_WMI_DATA_VO	= SVC(ATH10K_HTC_SVC_GRP_WMI, 4),

	ATH10K_HTC_SVC_ID_NMI_CONTROL	= SVC(ATH10K_HTC_SVC_GRP_NMI, 0),
	ATH10K_HTC_SVC_ID_NMI_DATA	= SVC(ATH10K_HTC_SVC_GRP_NMI, 1),

	ATH10K_HTC_SVC_ID_HTT_DATA_MSG	= SVC(ATH10K_HTC_SVC_GRP_HTT, 0),

	ATH10K_HTC_SVC_ID_HTT_DATA2_MSG = SVC(ATH10K_HTC_SVC_GRP_HTT, 1),
	ATH10K_HTC_SVC_ID_HTT_DATA3_MSG = SVC(ATH10K_HTC_SVC_GRP_HTT, 2),
	ATH10K_HTC_SVC_ID_HTT_LOG_MSG = SVC(ATH10K_LOG_SERVICE_GROUP, 0),
	/* raw stream service (i.e. flash, tcmd, calibration apps) */
	ATH10K_HTC_SVC_ID_TEST_RAW_STREAMS = SVC(ATH10K_HTC_SVC_GRP_TEST, 0),
};

#undef SVC

enum ath10k_htc_ep_id {
	ATH10K_HTC_EP_UNUSED = -1,
	ATH10K_HTC_EP_0 = 0,
	ATH10K_HTC_EP_1 = 1,
	ATH10K_HTC_EP_2,
	ATH10K_HTC_EP_3,
	ATH10K_HTC_EP_4,
	ATH10K_HTC_EP_5,
	ATH10K_HTC_EP_6,
	ATH10K_HTC_EP_7,
	ATH10K_HTC_EP_8,
	ATH10K_HTC_EP_COUNT,
};

struct ath10k_htc_ops {
	void (*target_send_suspend_complete)(struct ath10k *ar);
};

struct ath10k_htc_ep_ops {
	void (*ep_tx_complete)(struct ath10k *, struct sk_buff *);
	void (*ep_rx_complete)(struct ath10k *, struct sk_buff *);
	void (*ep_tx_credits)(struct ath10k *);
};

/* service connection information */
struct ath10k_htc_svc_conn_req {
	u16 service_id;
	struct ath10k_htc_ep_ops ep_ops;
	int max_send_queue_depth;
};

/* service connection response information */
struct ath10k_htc_svc_conn_resp {
	u8 buffer_len;
	u8 actual_len;
	enum ath10k_htc_ep_id eid;
	unsigned int max_msg_len;
	u8 connect_resp_code;
};

#define ATH10K_NUM_CONTROL_TX_BUFFERS 2
#define ATH10K_HTC_MAX_LEN 4096
#define ATH10K_HTC_MAX_CTRL_MSG_LEN 256
#define ATH10K_HTC_WAIT_TIMEOUT_HZ (1 * HZ)
#define ATH10K_HTC_CONTROL_BUFFER_SIZE (ATH10K_HTC_MAX_CTRL_MSG_LEN + \
					sizeof(struct ath10k_htc_hdr))
#define ATH10K_HTC_CONN_SVC_TIMEOUT_HZ (1 * HZ)

struct ath10k_htc_ep {
	struct ath10k_htc *htc;
	enum ath10k_htc_ep_id eid;
	enum ath10k_htc_svc_id service_id;
	struct ath10k_htc_ep_ops ep_ops;

	int max_tx_queue_depth;
	int max_ep_message_len;
	u8 ul_pipe_id;
	u8 dl_pipe_id;

	u8 seq_no; /* for debugging */
	int tx_credits;
	bool tx_credit_flow_enabled;
};

struct ath10k_htc_svc_tx_credits {
	u16 service_id;
	u8  credit_allocation;
};

struct ath10k_htc {
	struct ath10k *ar;
	struct ath10k_htc_ep endpoint[ATH10K_HTC_EP_COUNT];

	/* protects endpoints */
	spinlock_t tx_lock;

	struct ath10k_htc_ops htc_ops;

	u8 control_resp_buffer[ATH10K_HTC_MAX_CTRL_MSG_LEN];
	int control_resp_len;

	struct completion ctl_resp;

	int total_transmit_credits;
	int target_credit_size;
	u8 max_msgs_per_htc_bundle;
};

int ath10k_htc_init(struct ath10k *ar);
int ath10k_htc_wait_target(struct ath10k_htc *htc);
int ath10k_htc_start(struct ath10k_htc *htc);
int ath10k_htc_connect_service(struct ath10k_htc *htc,
			       struct ath10k_htc_svc_conn_req  *conn_req,
			       struct ath10k_htc_svc_conn_resp *conn_resp);
int ath10k_htc_send(struct ath10k_htc *htc, enum ath10k_htc_ep_id eid,
		    struct sk_buff *packet);
struct sk_buff *ath10k_htc_alloc_skb(struct ath10k *ar, int size);
void ath10k_htc_tx_completion_handler(struct ath10k *ar, struct sk_buff *skb);
void ath10k_htc_rx_completion_handler(struct ath10k *ar, struct sk_buff *skb);
void ath10k_htc_notify_tx_completion(struct ath10k_htc_ep *ep,
				     struct sk_buff *skb);
int ath10k_htc_process_trailer(struct ath10k_htc *htc,
			       u8 *buffer,
			       int length,
			       enum ath10k_htc_ep_id src_eid,
			       void *next_lookaheads,
			       int *next_lookaheads_len);

#endif
