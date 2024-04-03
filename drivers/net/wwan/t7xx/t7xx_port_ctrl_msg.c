// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 *  Moises Veleta <moises.veleta@intel.com>
 *
 * Contributors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *  Eliot Lee <eliot.lee@intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>

#include "t7xx_port.h"
#include "t7xx_port_proxy.h"
#include "t7xx_state_monitor.h"

#define PORT_MSG_VERSION	GENMASK(31, 16)
#define PORT_MSG_PRT_CNT	GENMASK(15, 0)

struct port_msg {
	__le32	head_pattern;
	__le32	info;
	__le32	tail_pattern;
	__le32	data[];
};

static int port_ctl_send_msg_to_md(struct t7xx_port *port, unsigned int msg, unsigned int ex_msg)
{
	struct sk_buff *skb;
	int ret;

	skb = t7xx_ctrl_alloc_skb(0);
	if (!skb)
		return -ENOMEM;

	ret = t7xx_port_send_ctl_skb(port, skb, msg, ex_msg);
	if (ret)
		dev_kfree_skb_any(skb);

	return ret;
}

static int fsm_ee_message_handler(struct t7xx_port *port, struct t7xx_fsm_ctl *ctl,
				  struct sk_buff *skb)
{
	struct ctrl_msg_header *ctrl_msg_h = (struct ctrl_msg_header *)skb->data;
	struct device *dev = &ctl->md->t7xx_dev->pdev->dev;
	enum md_state md_state;
	int ret = -EINVAL;

	md_state = t7xx_fsm_get_md_state(ctl);
	if (md_state != MD_STATE_EXCEPTION) {
		dev_err(dev, "Receive invalid MD_EX %x when MD state is %d\n",
			ctrl_msg_h->ex_msg, md_state);
		return -EINVAL;
	}

	switch (le32_to_cpu(ctrl_msg_h->ctrl_msg_id)) {
	case CTL_ID_MD_EX:
		if (le32_to_cpu(ctrl_msg_h->ex_msg) != MD_EX_CHK_ID) {
			dev_err(dev, "Receive invalid MD_EX %x\n", ctrl_msg_h->ex_msg);
			break;
		}

		ret = port_ctl_send_msg_to_md(port, CTL_ID_MD_EX, MD_EX_CHK_ID);
		if (ret) {
			dev_err(dev, "Failed to send exception message to modem\n");
			break;
		}

		ret = t7xx_fsm_append_event(ctl, FSM_EVENT_MD_EX, NULL, 0);
		if (ret)
			dev_err(dev, "Failed to append Modem Exception event");

		break;

	case CTL_ID_MD_EX_ACK:
		if (le32_to_cpu(ctrl_msg_h->ex_msg) != MD_EX_CHK_ACK_ID) {
			dev_err(dev, "Receive invalid MD_EX_ACK %x\n", ctrl_msg_h->ex_msg);
			break;
		}

		ret = t7xx_fsm_append_event(ctl, FSM_EVENT_MD_EX_REC_OK, NULL, 0);
		if (ret)
			dev_err(dev, "Failed to append Modem Exception Received event");

		break;

	case CTL_ID_MD_EX_PASS:
		ret = t7xx_fsm_append_event(ctl, FSM_EVENT_MD_EX_PASS, NULL, 0);
		if (ret)
			dev_err(dev, "Failed to append Modem Exception Passed event");

		break;

	case CTL_ID_DRV_VER_ERROR:
		dev_err(dev, "AP/MD driver version mismatch\n");
	}

	return ret;
}

/**
 * t7xx_port_enum_msg_handler() - Parse the port enumeration message to create/remove nodes.
 * @md: Modem context.
 * @msg: Message.
 *
 * Used to control create/remove device node.
 *
 * Return:
 * * 0		- Success.
 * * -EFAULT	- Message check failure.
 */
int t7xx_port_enum_msg_handler(struct t7xx_modem *md, void *msg)
{
	struct device *dev = &md->t7xx_dev->pdev->dev;
	unsigned int version, port_count, i;
	struct port_msg *port_msg = msg;

	version = FIELD_GET(PORT_MSG_VERSION, le32_to_cpu(port_msg->info));
	if (version != PORT_ENUM_VER ||
	    le32_to_cpu(port_msg->head_pattern) != PORT_ENUM_HEAD_PATTERN ||
	    le32_to_cpu(port_msg->tail_pattern) != PORT_ENUM_TAIL_PATTERN) {
		dev_err(dev, "Invalid port control message %x:%x:%x\n",
			version, le32_to_cpu(port_msg->head_pattern),
			le32_to_cpu(port_msg->tail_pattern));
		return -EFAULT;
	}

	port_count = FIELD_GET(PORT_MSG_PRT_CNT, le32_to_cpu(port_msg->info));
	for (i = 0; i < port_count; i++) {
		u32 port_info = le32_to_cpu(port_msg->data[i]);
		unsigned int ch_id;
		bool en_flag;

		ch_id = FIELD_GET(PORT_INFO_CH_ID, port_info);
		en_flag = port_info & PORT_INFO_ENFLG;
		if (t7xx_port_proxy_chl_enable_disable(md->port_prox, ch_id, en_flag))
			dev_dbg(dev, "Port:%x not found\n", ch_id);
	}

	return 0;
}

static int control_msg_handler(struct t7xx_port *port, struct sk_buff *skb)
{
	const struct t7xx_port_conf *port_conf = port->port_conf;
	struct t7xx_fsm_ctl *ctl = port->t7xx_dev->md->fsm_ctl;
	struct ctrl_msg_header *ctrl_msg_h;
	int ret = 0;

	ctrl_msg_h = (struct ctrl_msg_header *)skb->data;
	switch (le32_to_cpu(ctrl_msg_h->ctrl_msg_id)) {
	case CTL_ID_HS2_MSG:
		skb_pull(skb, sizeof(*ctrl_msg_h));

		if (port_conf->rx_ch == PORT_CH_CONTROL_RX ||
		    port_conf->rx_ch == PORT_CH_AP_CONTROL_RX) {
			int event = port_conf->rx_ch == PORT_CH_CONTROL_RX ?
				    FSM_EVENT_MD_HS2 : FSM_EVENT_AP_HS2;

			ret = t7xx_fsm_append_event(ctl, event, skb->data,
						    le32_to_cpu(ctrl_msg_h->data_length));
			if (ret)
				dev_err(port->dev, "Failed to append Handshake 2 event");
		}

		dev_kfree_skb_any(skb);
		break;

	case CTL_ID_MD_EX:
	case CTL_ID_MD_EX_ACK:
	case CTL_ID_MD_EX_PASS:
	case CTL_ID_DRV_VER_ERROR:
		ret = fsm_ee_message_handler(port, ctl, skb);
		dev_kfree_skb_any(skb);
		break;

	case CTL_ID_PORT_ENUM:
		skb_pull(skb, sizeof(*ctrl_msg_h));
		ret = t7xx_port_enum_msg_handler(ctl->md, (struct port_msg *)skb->data);
		if (!ret)
			ret = port_ctl_send_msg_to_md(port, CTL_ID_PORT_ENUM, 0);
		else
			ret = port_ctl_send_msg_to_md(port, CTL_ID_PORT_ENUM,
						      PORT_ENUM_VER_MISMATCH);

		break;

	default:
		ret = -EINVAL;
		dev_err(port->dev, "Unknown control message ID to FSM %x\n",
			le32_to_cpu(ctrl_msg_h->ctrl_msg_id));
		break;
	}

	if (ret)
		dev_err(port->dev, "%s control message handle error: %d\n", port_conf->name, ret);

	return ret;
}

static int port_ctl_rx_thread(void *arg)
{
	while (!kthread_should_stop()) {
		struct t7xx_port *port = arg;
		struct sk_buff *skb;
		unsigned long flags;

		spin_lock_irqsave(&port->rx_wq.lock, flags);
		if (skb_queue_empty(&port->rx_skb_list) &&
		    wait_event_interruptible_locked_irq(port->rx_wq,
							!skb_queue_empty(&port->rx_skb_list) ||
							kthread_should_stop())) {
			spin_unlock_irqrestore(&port->rx_wq.lock, flags);
			continue;
		}
		if (kthread_should_stop()) {
			spin_unlock_irqrestore(&port->rx_wq.lock, flags);
			break;
		}
		skb = __skb_dequeue(&port->rx_skb_list);
		spin_unlock_irqrestore(&port->rx_wq.lock, flags);

		control_msg_handler(port, skb);
	}

	return 0;
}

static int port_ctl_init(struct t7xx_port *port)
{
	const struct t7xx_port_conf *port_conf = port->port_conf;

	port->thread = kthread_run(port_ctl_rx_thread, port, "%s", port_conf->name);
	if (IS_ERR(port->thread)) {
		dev_err(port->dev, "Failed to start port control thread\n");
		return PTR_ERR(port->thread);
	}

	port->rx_length_th = CTRL_QUEUE_MAXLEN;
	return 0;
}

static void port_ctl_uninit(struct t7xx_port *port)
{
	unsigned long flags;
	struct sk_buff *skb;

	if (port->thread)
		kthread_stop(port->thread);

	spin_lock_irqsave(&port->rx_wq.lock, flags);
	port->rx_length_th = 0;
	while ((skb = __skb_dequeue(&port->rx_skb_list)) != NULL)
		dev_kfree_skb_any(skb);
	spin_unlock_irqrestore(&port->rx_wq.lock, flags);
}

struct port_ops ctl_port_ops = {
	.init = port_ctl_init,
	.recv_skb = t7xx_port_enqueue_skb,
	.uninit = port_ctl_uninit,
};
