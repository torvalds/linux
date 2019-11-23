/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#ifndef ATH11K_HTC_H
#define ATH11K_HTC_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/bug.h>
#include <linux/skbuff.h>
#include <linux/timer.h>

struct ath11k_base;

#define HTC_HDR_ENDPOINTID                       GENMASK(7, 0)
#define HTC_HDR_FLAGS                            GENMASK(15, 8)
#define HTC_HDR_PAYLOADLEN                       GENMASK(31, 16)
#define HTC_HDR_CONTROLBYTES0                    GENMASK(7, 0)
#define HTC_HDR_CONTROLBYTES1                    GENMASK(15, 8)
#define HTC_HDR_RESERVED                         GENMASK(31, 16)

#define HTC_SVC_MSG_SERVICE_ID                   GENMASK(31, 16)
#define HTC_SVC_MSG_CONNECTIONFLAGS              GENMASK(15, 0)
#define HTC_SVC_MSG_SERVICEMETALENGTH            GENMASK(23, 16)
#define HTC_READY_MSG_CREDITCOUNT                GENMASK(31, 16)
#define HTC_READY_MSG_CREDITSIZE                 GENMASK(15, 0)
#define HTC_READY_MSG_MAXENDPOINTS               GENMASK(23, 16)

#define HTC_READY_EX_MSG_HTCVERSION              GENMASK(7, 0)
#define HTC_READY_EX_MSG_MAXMSGSPERHTCBUNDLE     GENMASK(15, 8)

#define HTC_SVC_RESP_MSG_SERVICEID           GENMASK(31, 16)
#define HTC_SVC_RESP_MSG_STATUS              GENMASK(7, 0)
#define HTC_SVC_RESP_MSG_ENDPOINTID          GENMASK(15, 8)
#define HTC_SVC_RESP_MSG_MAXMSGSIZE          GENMASK(31, 16)
#define HTC_SVC_RESP_MSG_SERVICEMETALENGTH   GENMASK(7, 0)

#define HTC_MSG_MESSAGEID                        GENMASK(15, 0)
#define HTC_SETUP_COMPLETE_EX_MSG_SETUPFLAGS     GENMASK(31, 0)
#define HTC_SETUP_COMPLETE_EX_MSG_MAXMSGSPERBUNDLEDRECV      GENMASK(7, 0)
#define HTC_SETUP_COMPLETE_EX_MSG_RSVD0          GENMASK(15, 8)
#define HTC_SETUP_COMPLETE_EX_MSG_RSVD1          GENMASK(23, 16)
#define HTC_SETUP_COMPLETE_EX_MSG_RSVD2          GENMASK(31, 24)

enum ath11k_htc_tx_flags {
	ATH11K_HTC_FLAG_NEED_CREDIT_UPDATE = 0x01,
	ATH11K_HTC_FLAG_SEND_BUNDLE        = 0x02
};

enum ath11k_htc_rx_flags {
	ATH11K_HTC_FLAG_TRAILER_PRESENT = 0x02,
	ATH11K_HTC_FLAG_BUNDLE_MASK     = 0xF0
};

struct ath11k_htc_hdr {
	u32 htc_info;
	u32 ctrl_info;
} __packed __aligned(4);

enum ath11k_htc_msg_id {
	ATH11K_HTC_MSG_READY_ID                = 1,
	ATH11K_HTC_MSG_CONNECT_SERVICE_ID      = 2,
	ATH11K_HTC_MSG_CONNECT_SERVICE_RESP_ID = 3,
	ATH11K_HTC_MSG_SETUP_COMPLETE_ID       = 4,
	ATH11K_HTC_MSG_SETUP_COMPLETE_EX_ID    = 5,
	ATH11K_HTC_MSG_SEND_SUSPEND_COMPLETE   = 6
};

enum ath11k_htc_version {
	ATH11K_HTC_VERSION_2P0 = 0x00, /* 2.0 */
	ATH11K_HTC_VERSION_2P1 = 0x01, /* 2.1 */
};

#define ATH11K_HTC_CONN_FLAGS_THRESHOLD_LEVEL_MASK GENMASK(1, 0)
#define ATH11K_HTC_CONN_FLAGS_RECV_ALLOC GENMASK(15, 8)

enum ath11k_htc_conn_flags {
	ATH11K_HTC_CONN_FLAGS_THRESHOLD_LEVEL_ONE_FOURTH    = 0x0,
	ATH11K_HTC_CONN_FLAGS_THRESHOLD_LEVEL_ONE_HALF      = 0x1,
	ATH11K_HTC_CONN_FLAGS_THRESHOLD_LEVEL_THREE_FOURTHS = 0x2,
	ATH11K_HTC_CONN_FLAGS_THRESHOLD_LEVEL_UNITY         = 0x3,
	ATH11K_HTC_CONN_FLAGS_REDUCE_CREDIT_DRIBBLE    = 1 << 2,
	ATH11K_HTC_CONN_FLAGS_DISABLE_CREDIT_FLOW_CTRL = 1 << 3
};

enum ath11k_htc_conn_svc_status {
	ATH11K_HTC_CONN_SVC_STATUS_SUCCESS      = 0,
	ATH11K_HTC_CONN_SVC_STATUS_NOT_FOUND    = 1,
	ATH11K_HTC_CONN_SVC_STATUS_FAILED       = 2,
	ATH11K_HTC_CONN_SVC_STATUS_NO_RESOURCES = 3,
	ATH11K_HTC_CONN_SVC_STATUS_NO_MORE_EP   = 4
};

struct ath11k_htc_ready {
	u32 id_credit_count;
	u32 size_ep;
} __packed;

struct ath11k_htc_ready_extended {
	struct ath11k_htc_ready base;
	u32 ver_bundle;
} __packed;

struct ath11k_htc_conn_svc {
	u32 msg_svc_id;
	u32 flags_len;
} __packed;

struct ath11k_htc_conn_svc_resp {
	u32 msg_svc_id;
	u32 flags_len;
	u32 svc_meta_pad;
} __packed;

struct ath11k_htc_setup_complete_extended {
	u32 msg_id;
	u32 flags;
	u32 max_msgs_per_bundled_recv;
} __packed;

struct ath11k_htc_msg {
	u32 msg_svc_id;
	u32 flags_len;
} __packed __aligned(4);

enum ath11k_htc_record_id {
	ATH11K_HTC_RECORD_NULL    = 0,
	ATH11K_HTC_RECORD_CREDITS = 1
};

struct ath11k_htc_record_hdr {
	u8 id; /* @enum ath11k_htc_record_id */
	u8 len;
	u8 pad0;
	u8 pad1;
} __packed;

struct ath11k_htc_credit_report {
	u8 eid; /* @enum ath11k_htc_ep_id */
	u8 credits;
	u8 pad0;
	u8 pad1;
} __packed;

struct ath11k_htc_record {
	struct ath11k_htc_record_hdr hdr;
	union {
		struct ath11k_htc_credit_report credit_report[0];
		u8 pauload[0];
	};
} __packed __aligned(4);

/* note: the trailer offset is dynamic depending
 * on payload length. this is only a struct layout draft
 */
struct ath11k_htc_frame {
	struct ath11k_htc_hdr hdr;
	union {
		struct ath11k_htc_msg msg;
		u8 payload[0];
	};
	struct ath11k_htc_record trailer[0];
} __packed __aligned(4);

enum ath11k_htc_svc_gid {
	ATH11K_HTC_SVC_GRP_RSVD = 0,
	ATH11K_HTC_SVC_GRP_WMI = 1,
	ATH11K_HTC_SVC_GRP_NMI = 2,
	ATH11K_HTC_SVC_GRP_HTT = 3,
	ATH11K_HTC_SVC_GRP_CFG = 4,
	ATH11K_HTC_SVC_GRP_IPA = 5,
	ATH11K_HTC_SVC_GRP_PKTLOG = 6,

	ATH11K_HTC_SVC_GRP_TEST = 254,
	ATH11K_HTC_SVC_GRP_LAST = 255,
};

#define SVC(group, idx) \
	(int)(((int)(group) << 8) | (int)(idx))

enum ath11k_htc_svc_id {
	/* NOTE: service ID of 0x0000 is reserved and should never be used */
	ATH11K_HTC_SVC_ID_RESERVED	= 0x0000,
	ATH11K_HTC_SVC_ID_UNUSED	= ATH11K_HTC_SVC_ID_RESERVED,

	ATH11K_HTC_SVC_ID_RSVD_CTRL	= SVC(ATH11K_HTC_SVC_GRP_RSVD, 1),
	ATH11K_HTC_SVC_ID_WMI_CONTROL	= SVC(ATH11K_HTC_SVC_GRP_WMI, 0),
	ATH11K_HTC_SVC_ID_WMI_DATA_BE	= SVC(ATH11K_HTC_SVC_GRP_WMI, 1),
	ATH11K_HTC_SVC_ID_WMI_DATA_BK	= SVC(ATH11K_HTC_SVC_GRP_WMI, 2),
	ATH11K_HTC_SVC_ID_WMI_DATA_VI	= SVC(ATH11K_HTC_SVC_GRP_WMI, 3),
	ATH11K_HTC_SVC_ID_WMI_DATA_VO	= SVC(ATH11K_HTC_SVC_GRP_WMI, 4),
	ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC1 = SVC(ATH11K_HTC_SVC_GRP_WMI, 5),
	ATH11K_HTC_SVC_ID_WMI_CONTROL_MAC2 = SVC(ATH11K_HTC_SVC_GRP_WMI, 6),

	ATH11K_HTC_SVC_ID_NMI_CONTROL	= SVC(ATH11K_HTC_SVC_GRP_NMI, 0),
	ATH11K_HTC_SVC_ID_NMI_DATA	= SVC(ATH11K_HTC_SVC_GRP_NMI, 1),

	ATH11K_HTC_SVC_ID_HTT_DATA_MSG	= SVC(ATH11K_HTC_SVC_GRP_HTT, 0),

	/* raw stream service (i.e. flash, tcmd, calibration apps) */
	ATH11K_HTC_SVC_ID_TEST_RAW_STREAMS = SVC(ATH11K_HTC_SVC_GRP_TEST, 0),
	ATH11K_HTC_SVC_ID_IPA_TX = SVC(ATH11K_HTC_SVC_GRP_IPA, 0),
	ATH11K_HTC_SVC_ID_PKT_LOG = SVC(ATH11K_HTC_SVC_GRP_PKTLOG, 0),
};

#undef SVC

enum ath11k_htc_ep_id {
	ATH11K_HTC_EP_UNUSED = -1,
	ATH11K_HTC_EP_0 = 0,
	ATH11K_HTC_EP_1 = 1,
	ATH11K_HTC_EP_2,
	ATH11K_HTC_EP_3,
	ATH11K_HTC_EP_4,
	ATH11K_HTC_EP_5,
	ATH11K_HTC_EP_6,
	ATH11K_HTC_EP_7,
	ATH11K_HTC_EP_8,
	ATH11K_HTC_EP_COUNT,
};

struct ath11k_htc_ops {
	void (*target_send_suspend_complete)(struct ath11k_base *ar);
};

struct ath11k_htc_ep_ops {
	void (*ep_tx_complete)(struct ath11k_base *, struct sk_buff *);
	void (*ep_rx_complete)(struct ath11k_base *, struct sk_buff *);
	void (*ep_tx_credits)(struct ath11k_base *);
};

/* service connection information */
struct ath11k_htc_svc_conn_req {
	u16 service_id;
	struct ath11k_htc_ep_ops ep_ops;
	int max_send_queue_depth;
};

/* service connection response information */
struct ath11k_htc_svc_conn_resp {
	u8 buffer_len;
	u8 actual_len;
	enum ath11k_htc_ep_id eid;
	unsigned int max_msg_len;
	u8 connect_resp_code;
};

#define ATH11K_NUM_CONTROL_TX_BUFFERS 2
#define ATH11K_HTC_MAX_LEN 4096
#define ATH11K_HTC_MAX_CTRL_MSG_LEN 256
#define ATH11K_HTC_WAIT_TIMEOUT_HZ (1 * HZ)
#define ATH11K_HTC_CONTROL_BUFFER_SIZE (ATH11K_HTC_MAX_CTRL_MSG_LEN + \
					sizeof(struct ath11k_htc_hdr))
#define ATH11K_HTC_CONN_SVC_TIMEOUT_HZ (1 * HZ)
#define ATH11K_HTC_MAX_SERVICE_ALLOC_ENTRIES 8

struct ath11k_htc_ep {
	struct ath11k_htc *htc;
	enum ath11k_htc_ep_id eid;
	enum ath11k_htc_svc_id service_id;
	struct ath11k_htc_ep_ops ep_ops;

	int max_tx_queue_depth;
	int max_ep_message_len;
	u8 ul_pipe_id;
	u8 dl_pipe_id;

	u8 seq_no; /* for debugging */
	int tx_credits;
	bool tx_credit_flow_enabled;
};

struct ath11k_htc_svc_tx_credits {
	u16 service_id;
	u8  credit_allocation;
};

struct ath11k_htc {
	struct ath11k_base *ab;
	struct ath11k_htc_ep endpoint[ATH11K_HTC_EP_COUNT];

	/* protects endpoints */
	spinlock_t tx_lock;

	struct ath11k_htc_ops htc_ops;

	u8 control_resp_buffer[ATH11K_HTC_MAX_CTRL_MSG_LEN];
	int control_resp_len;

	struct completion ctl_resp;

	int total_transmit_credits;
	struct ath11k_htc_svc_tx_credits
		service_alloc_table[ATH11K_HTC_MAX_SERVICE_ALLOC_ENTRIES];
	int target_credit_size;
	u8 wmi_ep_count;
};

int ath11k_htc_init(struct ath11k_base *ar);
int ath11k_htc_wait_target(struct ath11k_htc *htc);
int ath11k_htc_start(struct ath11k_htc *htc);
int ath11k_htc_connect_service(struct ath11k_htc *htc,
			       struct ath11k_htc_svc_conn_req  *conn_req,
			       struct ath11k_htc_svc_conn_resp *conn_resp);
int ath11k_htc_send(struct ath11k_htc *htc, enum ath11k_htc_ep_id eid,
		    struct sk_buff *packet);
struct sk_buff *ath11k_htc_alloc_skb(struct ath11k_base *ar, int size);
void ath11k_htc_rx_completion_handler(struct ath11k_base *ar,
				      struct sk_buff *skb);

#endif
