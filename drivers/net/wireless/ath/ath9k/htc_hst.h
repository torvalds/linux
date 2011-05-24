/*
 * Copyright (c) 2010-2011 Atheros Communications Inc.
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

#ifndef HTC_HST_H
#define HTC_HST_H

struct ath9k_htc_priv;
struct htc_target;
struct ath9k_htc_tx_ctl;

enum ath9k_hif_transports {
	ATH9K_HIF_USB,
};

struct ath9k_htc_hif {
	struct list_head list;
	const enum ath9k_hif_transports transport;
	const char *name;

	u8 control_dl_pipe;
	u8 control_ul_pipe;

	void (*start) (void *hif_handle);
	void (*stop) (void *hif_handle);
	void (*sta_drain) (void *hif_handle, u8 idx);
	int (*send) (void *hif_handle, u8 pipe, struct sk_buff *buf);
};

enum htc_endpoint_id {
	ENDPOINT_UNUSED = -1,
	ENDPOINT0 = 0,
	ENDPOINT1 = 1,
	ENDPOINT2 = 2,
	ENDPOINT3 = 3,
	ENDPOINT4 = 4,
	ENDPOINT5 = 5,
	ENDPOINT6 = 6,
	ENDPOINT7 = 7,
	ENDPOINT8 = 8,
	ENDPOINT_MAX = 22
};

/* Htc frame hdr flags */
#define HTC_FLAGS_RECV_TRAILER (1 << 1)

struct htc_frame_hdr {
	u8 endpoint_id;
	u8 flags;
	__be16 payload_len;
	u8 control[4];
} __packed;

struct htc_ready_msg {
	__be16 message_id;
	__be16 credits;
	__be16 credit_size;
	u8 max_endpoints;
	u8 pad;
} __packed;

struct htc_config_pipe_msg {
	__be16 message_id;
	u8 pipe_id;
	u8 credits;
} __packed;

struct htc_ep_callbacks {
	void *priv;
	void (*tx) (void *, struct sk_buff *, enum htc_endpoint_id, bool txok);
	void (*rx) (void *, struct sk_buff *, enum htc_endpoint_id);
};

struct htc_endpoint {
	u16 service_id;

	struct htc_ep_callbacks ep_callbacks;
	u32 max_txqdepth;
	int max_msglen;

	u8 ul_pipeid;
	u8 dl_pipeid;
};

#define HTC_MAX_CONTROL_MESSAGE_LENGTH 255
#define HTC_CONTROL_BUFFER_SIZE	\
	(HTC_MAX_CONTROL_MESSAGE_LENGTH + sizeof(struct htc_frame_hdr))

#define HTC_OP_START_WAIT           BIT(0)
#define HTC_OP_CONFIG_PIPE_CREDITS  BIT(1)

struct htc_target {
	void *hif_dev;
	struct ath9k_htc_priv *drv_priv;
	struct device *dev;
	struct ath9k_htc_hif *hif;
	struct htc_endpoint endpoint[ENDPOINT_MAX];
	struct completion target_wait;
	struct completion cmd_wait;
	struct list_head list;
	enum htc_endpoint_id conn_rsp_epid;
	u16 credits;
	u16 credit_size;
	u8 htc_flags;
	atomic_t tgt_ready;
};

enum htc_msg_id {
	HTC_MSG_READY_ID = 1,
	HTC_MSG_CONNECT_SERVICE_ID,
	HTC_MSG_CONNECT_SERVICE_RESPONSE_ID,
	HTC_MSG_SETUP_COMPLETE_ID,
	HTC_MSG_CONFIG_PIPE_ID,
	HTC_MSG_CONFIG_PIPE_RESPONSE_ID,
};

struct htc_service_connreq {
	u16 service_id;
	u16 con_flags;
	u32 max_send_qdepth;
	struct htc_ep_callbacks ep_callbacks;
};

/* Current service IDs */

enum htc_service_group_ids{
	RSVD_SERVICE_GROUP = 0,
	WMI_SERVICE_GROUP = 1,

	HTC_SERVICE_GROUP_LAST = 255
};

#define MAKE_SERVICE_ID(group, index)		\
	(int)(((int)group << 8) | (int)(index))

/* NOTE: service ID of 0x0000 is reserved and should never be used */
#define HTC_CTRL_RSVD_SVC MAKE_SERVICE_ID(RSVD_SERVICE_GROUP, 1)
#define HTC_LOOPBACK_RSVD_SVC MAKE_SERVICE_ID(RSVD_SERVICE_GROUP, 2)

#define WMI_CONTROL_SVC   MAKE_SERVICE_ID(WMI_SERVICE_GROUP, 0)
#define WMI_BEACON_SVC	  MAKE_SERVICE_ID(WMI_SERVICE_GROUP, 1)
#define WMI_CAB_SVC	  MAKE_SERVICE_ID(WMI_SERVICE_GROUP, 2)
#define WMI_UAPSD_SVC	  MAKE_SERVICE_ID(WMI_SERVICE_GROUP, 3)
#define WMI_MGMT_SVC	  MAKE_SERVICE_ID(WMI_SERVICE_GROUP, 4)
#define WMI_DATA_VO_SVC   MAKE_SERVICE_ID(WMI_SERVICE_GROUP, 5)
#define WMI_DATA_VI_SVC   MAKE_SERVICE_ID(WMI_SERVICE_GROUP, 6)
#define WMI_DATA_BE_SVC   MAKE_SERVICE_ID(WMI_SERVICE_GROUP, 7)
#define WMI_DATA_BK_SVC   MAKE_SERVICE_ID(WMI_SERVICE_GROUP, 8)

struct htc_conn_svc_msg {
	__be16 msg_id;
	__be16 service_id;
	__be16 con_flags;
	u8 dl_pipeid;
	u8 ul_pipeid;
	u8 svc_meta_len;
	u8 pad;
} __packed;

/* connect response status codes */
#define HTC_SERVICE_SUCCESS      0
#define HTC_SERVICE_NOT_FOUND    1
#define HTC_SERVICE_FAILED       2
#define HTC_SERVICE_NO_RESOURCES 3
#define HTC_SERVICE_NO_MORE_EP   4

struct htc_conn_svc_rspmsg {
	__be16 msg_id;
	__be16 service_id;
	u8 status;
	u8 endpoint_id;
	__be16 max_msg_len;
	u8 svc_meta_len;
	u8 pad;
} __packed;

struct htc_comp_msg {
	__be16 msg_id;
} __packed;

int htc_init(struct htc_target *target);
int htc_connect_service(struct htc_target *target,
			  struct htc_service_connreq *service_connreq,
			  enum htc_endpoint_id *conn_rsp_eid);
int htc_send(struct htc_target *target, struct sk_buff *skb);
int htc_send_epid(struct htc_target *target, struct sk_buff *skb,
		  enum htc_endpoint_id epid);
void htc_stop(struct htc_target *target);
void htc_start(struct htc_target *target);
void htc_sta_drain(struct htc_target *target, u8 idx);

void ath9k_htc_rx_msg(struct htc_target *htc_handle,
		      struct sk_buff *skb, u32 len, u8 pipe_id);
void ath9k_htc_txcompletion_cb(struct htc_target *htc_handle,
			       struct sk_buff *skb, bool txok);

struct htc_target *ath9k_htc_hw_alloc(void *hif_handle,
				      struct ath9k_htc_hif *hif,
				      struct device *dev);
void ath9k_htc_hw_free(struct htc_target *htc);
int ath9k_htc_hw_init(struct htc_target *target,
		      struct device *dev, u16 devid, char *product,
		      u32 drv_info);
void ath9k_htc_hw_deinit(struct htc_target *target, bool hot_unplug);

#endif /* HTC_HST_H */
