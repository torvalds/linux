/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for Alibaba Elastic Ethernet Adapter.
 *
 * Copyright (C) 2025 Alibaba Inc.
 */

#ifndef __EEA_ADMINQ_H__
#define __EEA_ADMINQ_H__

struct eea_aq_cfg {
	__le32 rx_depth_max;
	__le32 rx_depth_def;

	__le32 tx_depth_max;
	__le32 tx_depth_def;

	__le32 max_tso_size;
	__le32 max_tso_segs;

	u8 mac[ETH_ALEN];
	__le16 status;

	__le16 mtu;
	__le16 reserved0;
	__le16 reserved1;
	u8 reserved2;
	u8 reserved3;

	__le16 reserved4;
	__le16 reserved5;
	__le16 reserved6;
};

struct eea_aq_queue_status {
	__le16 qidx;
#define EEA_QUEUE_STATUS_OK 0
#define EEA_QUEUE_STATUS_NEED_RESET 1
	__le16 status;
};

struct __eea_aq_dev_status {
#define EEA_LINK_DOWN_STATUS  0
#define EEA_LINK_UP_STATUS    1
	__le16 link_status;
	__le16 reserved;

	struct eea_aq_queue_status q_status[];
};

struct eea_aq_dev_status {
	u32 num;
	struct __eea_aq_dev_status *status;
};

struct eea_aq {
	struct eea_ring *ring;
	u32 num;
	bool broken;
	u16 phase;

	/* lock for adminq exec */
	struct mutex lock;

	u32 q_req_size;
	u32 q_res_size;
	struct eea_aq_create *q_req_buf;
	__le32 *q_res_buf;
};

struct eea_net;

int eea_create_adminq(struct eea_net *enet, u32 qid);
void eea_destroy_adminq(struct eea_net *enet);

int eea_adminq_query_cfg(struct eea_net *enet, struct eea_aq_cfg *cfg);

int eea_adminq_create_q(struct eea_net *enet, u32 num, u32 flags);
int eea_adminq_destroy_all_q(struct eea_net *enet);
int eea_adminq_dev_status(struct eea_net *enet,
			  struct eea_aq_dev_status *dstatus);
void eea_adminq_config_host_info(struct eea_net *enet);
#endif
