/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2015-2016 Quantenna Communications. All rights reserved. */

#ifndef _QTN_FMAC_TRANS_H_
#define _QTN_FMAC_TRANS_H_

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/mutex.h>

#include "qlink.h"

#define QTNF_CMD_FLAG_RESP_REQ		BIT(0)

#define QTNF_MAX_CMD_BUF_SIZE	2048
#define QTNF_DEF_CMD_HROOM	4

struct qtnf_bus;

struct qtnf_cmd_ctl_node {
	struct completion cmd_resp_completion;
	struct sk_buff *resp_skb;
	u16 seq_num;
	bool waiting_for_resp;
	spinlock_t resp_lock; /* lock for resp_skb & waiting_for_resp changes */
};

struct qtnf_qlink_transport {
	struct qtnf_cmd_ctl_node curr_cmd;
	struct sk_buff_head event_queue;
	size_t event_queue_max_len;
};

void qtnf_trans_init(struct qtnf_bus *bus);
void qtnf_trans_free(struct qtnf_bus *bus);

int qtnf_trans_send_next_cmd(struct qtnf_bus *bus);
int qtnf_trans_handle_rx_ctl_packet(struct qtnf_bus *bus, struct sk_buff *skb);
int qtnf_trans_send_cmd_with_resp(struct qtnf_bus *bus,
				  struct sk_buff *cmd_skb,
				  struct sk_buff **response_skb);

#endif /* _QTN_FMAC_TRANS_H_ */
