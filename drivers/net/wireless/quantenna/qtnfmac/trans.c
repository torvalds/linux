/*
 * Copyright (c) 2015-2016 Quantenna Communications, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/types.h>
#include <linux/export.h>
#include <linux/slab.h>

#include "core.h"
#include "commands.h"
#include "event.h"
#include "bus.h"

#define QTNF_DEF_SYNC_CMD_TIMEOUT	(5 * HZ)

int qtnf_trans_send_cmd_with_resp(struct qtnf_bus *bus, struct sk_buff *cmd_skb,
				  struct sk_buff **response_skb)
{
	struct qtnf_cmd_ctl_node *ctl_node = &bus->trans.curr_cmd;
	struct qlink_cmd *cmd = (void *)cmd_skb->data;
	int ret = 0;
	long status;
	bool resp_not_handled = true;
	struct sk_buff *resp_skb = NULL;

	if (unlikely(!response_skb)) {
		dev_kfree_skb(cmd_skb);
		return -EFAULT;
	}

	spin_lock(&ctl_node->resp_lock);
	ctl_node->seq_num++;
	cmd->seq_num = cpu_to_le16(ctl_node->seq_num);
	WARN(ctl_node->resp_skb, "qtnfmac: response skb not empty\n");
	ctl_node->waiting_for_resp = true;
	spin_unlock(&ctl_node->resp_lock);

	ret = qtnf_bus_control_tx(bus, cmd_skb);
	dev_kfree_skb(cmd_skb);

	if (unlikely(ret))
		goto out;

	status = wait_for_completion_interruptible_timeout(
						&ctl_node->cmd_resp_completion,
						QTNF_DEF_SYNC_CMD_TIMEOUT);

	spin_lock(&ctl_node->resp_lock);
	resp_not_handled = ctl_node->waiting_for_resp;
	resp_skb = ctl_node->resp_skb;
	ctl_node->resp_skb = NULL;
	ctl_node->waiting_for_resp = false;
	spin_unlock(&ctl_node->resp_lock);

	if (unlikely(status <= 0)) {
		if (status == 0) {
			ret = -ETIMEDOUT;
			pr_err("response timeout\n");
		} else {
			ret = -EINTR;
			pr_debug("interrupted\n");
		}
	}

	if (unlikely(!resp_skb || resp_not_handled)) {
		if (!ret)
			ret = -EFAULT;

		goto out;
	}

	ret = 0;
	*response_skb = resp_skb;

out:
	if (unlikely(resp_skb && resp_not_handled))
		dev_kfree_skb(resp_skb);

	return ret;
}

static void qtnf_trans_signal_cmdresp(struct qtnf_bus *bus, struct sk_buff *skb)
{
	struct qtnf_cmd_ctl_node *ctl_node = &bus->trans.curr_cmd;
	const struct qlink_resp *resp = (const struct qlink_resp *)skb->data;
	const u16 recvd_seq_num = le16_to_cpu(resp->seq_num);

	spin_lock(&ctl_node->resp_lock);

	if (unlikely(!ctl_node->waiting_for_resp)) {
		pr_err("unexpected response\n");
		goto out_err;
	}

	if (unlikely(recvd_seq_num != ctl_node->seq_num)) {
		pr_err("seq num mismatch\n");
		goto out_err;
	}

	ctl_node->resp_skb = skb;
	ctl_node->waiting_for_resp = false;

	spin_unlock(&ctl_node->resp_lock);

	complete(&ctl_node->cmd_resp_completion);
	return;

out_err:
	spin_unlock(&ctl_node->resp_lock);
	dev_kfree_skb(skb);
}

static int qtnf_trans_event_enqueue(struct qtnf_bus *bus, struct sk_buff *skb)
{
	struct qtnf_qlink_transport *trans = &bus->trans;

	if (likely(skb_queue_len(&trans->event_queue) <
		   trans->event_queue_max_len)) {
		skb_queue_tail(&trans->event_queue, skb);
		queue_work(bus->workqueue, &bus->event_work);
	} else {
		pr_warn("event dropped due to queue overflow\n");
		dev_kfree_skb(skb);
		return -1;
	}

	return 0;
}

void qtnf_trans_init(struct qtnf_bus *bus)
{
	struct qtnf_qlink_transport *trans = &bus->trans;

	init_completion(&trans->curr_cmd.cmd_resp_completion);
	spin_lock_init(&trans->curr_cmd.resp_lock);

	spin_lock(&trans->curr_cmd.resp_lock);
	trans->curr_cmd.seq_num = 0;
	trans->curr_cmd.waiting_for_resp = false;
	trans->curr_cmd.resp_skb = NULL;
	spin_unlock(&trans->curr_cmd.resp_lock);

	/* Init event handling related fields */
	skb_queue_head_init(&trans->event_queue);
	trans->event_queue_max_len = QTNF_MAX_EVENT_QUEUE_LEN;
}

static void qtnf_trans_free_events(struct qtnf_bus *bus)
{
	struct sk_buff_head *event_queue = &bus->trans.event_queue;
	struct sk_buff *current_event_skb = skb_dequeue(event_queue);

	while (current_event_skb) {
		dev_kfree_skb_any(current_event_skb);
		current_event_skb = skb_dequeue(event_queue);
	}
}

void qtnf_trans_free(struct qtnf_bus *bus)
{
	if (!bus) {
		pr_err("invalid bus pointer\n");
		return;
	}

	qtnf_trans_free_events(bus);
}

int qtnf_trans_handle_rx_ctl_packet(struct qtnf_bus *bus, struct sk_buff *skb)
{
	const struct qlink_msg_header *header = (void *)skb->data;
	int ret = -1;

	if (unlikely(skb->len < sizeof(*header))) {
		pr_warn("packet is too small: %u\n", skb->len);
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	if (unlikely(skb->len != le16_to_cpu(header->len))) {
		pr_warn("cmd reply length mismatch: %u != %u\n",
			skb->len, le16_to_cpu(header->len));
		dev_kfree_skb(skb);
		return -EFAULT;
	}

	switch (le16_to_cpu(header->type)) {
	case QLINK_MSG_TYPE_CMDRSP:
		if (unlikely(skb->len < sizeof(struct qlink_cmd))) {
			pr_warn("cmd reply too short: %u\n", skb->len);
			dev_kfree_skb(skb);
			break;
		}

		qtnf_trans_signal_cmdresp(bus, skb);
		break;
	case QLINK_MSG_TYPE_EVENT:
		if (unlikely(skb->len < sizeof(struct qlink_event))) {
			pr_warn("event too short: %u\n", skb->len);
			dev_kfree_skb(skb);
			break;
		}

		ret = qtnf_trans_event_enqueue(bus, skb);
		break;
	default:
		pr_warn("unknown packet type: %x\n", le16_to_cpu(header->type));
		dev_kfree_skb(skb);
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(qtnf_trans_handle_rx_ctl_packet);
