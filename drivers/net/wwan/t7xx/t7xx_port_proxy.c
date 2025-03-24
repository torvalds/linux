// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Moises Veleta <moises.veleta@intel.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 *
 * Contributors:
 *  Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *  Chandrashekar Devegowda <chandrashekar.devegowda@intel.com>
 *  Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *  Eliot Lee <eliot.lee@intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/wwan.h>

#include "t7xx_hif_cldma.h"
#include "t7xx_modem_ops.h"
#include "t7xx_port.h"
#include "t7xx_port_proxy.h"
#include "t7xx_state_monitor.h"

#define Q_IDX_CTRL			0
#define Q_IDX_MBIM_MIPC		2
#define Q_IDX_ADB			3
#define Q_IDX_AT_CMD			5

#define INVALID_SEQ_NUM			GENMASK(15, 0)

#define for_each_proxy_port(i, p, proxy)	\
	for (i = 0, (p) = &(proxy)->ports[i];	\
	     i < (proxy)->port_count;		\
	     i++, (p) = &(proxy)->ports[i])

#define T7XX_MAX_POSSIBLE_PORTS_NUM	\
	(max(ARRAY_SIZE(t7xx_port_conf), ARRAY_SIZE(t7xx_early_port_conf)))

static const struct t7xx_port_conf t7xx_port_conf[] = {
	{
		.tx_ch = PORT_CH_UART2_TX,
		.rx_ch = PORT_CH_UART2_RX,
		.txq_index = Q_IDX_AT_CMD,
		.rxq_index = Q_IDX_AT_CMD,
		.txq_exp_index = 0xff,
		.rxq_exp_index = 0xff,
		.path_id = CLDMA_ID_MD,
		.ops = &wwan_sub_port_ops,
		.name = "AT",
		.port_type = WWAN_PORT_AT,
	}, {
		.tx_ch = PORT_CH_MBIM_TX,
		.rx_ch = PORT_CH_MBIM_RX,
		.txq_index = Q_IDX_MBIM_MIPC,
		.rxq_index = Q_IDX_MBIM_MIPC,
		.path_id = CLDMA_ID_MD,
		.ops = &wwan_sub_port_ops,
		.name = "MBIM",
		.port_type = WWAN_PORT_MBIM,
	}, {
#ifdef CONFIG_WWAN_DEBUGFS
		.tx_ch = PORT_CH_MD_LOG_TX,
		.rx_ch = PORT_CH_MD_LOG_RX,
		.txq_index = 7,
		.rxq_index = 7,
		.txq_exp_index = 7,
		.rxq_exp_index = 7,
		.path_id = CLDMA_ID_MD,
		.ops = &t7xx_trace_port_ops,
		.name = "mdlog",
	}, {
#endif
		.tx_ch = PORT_CH_CONTROL_TX,
		.rx_ch = PORT_CH_CONTROL_RX,
		.txq_index = Q_IDX_CTRL,
		.rxq_index = Q_IDX_CTRL,
		.path_id = CLDMA_ID_MD,
		.ops = &ctl_port_ops,
		.name = "t7xx_ctrl",
	}, {
		.tx_ch = PORT_CH_AP_CONTROL_TX,
		.rx_ch = PORT_CH_AP_CONTROL_RX,
		.txq_index = Q_IDX_CTRL,
		.rxq_index = Q_IDX_CTRL,
		.path_id = CLDMA_ID_AP,
		.ops = &ctl_port_ops,
		.name = "t7xx_ap_ctrl",
	}, {
		.tx_ch = PORT_CH_AP_ADB_TX,
		.rx_ch = PORT_CH_AP_ADB_RX,
		.txq_index = Q_IDX_ADB,
		.rxq_index = Q_IDX_ADB,
		.path_id = CLDMA_ID_AP,
		.ops = &wwan_sub_port_ops,
		.name = "adb",
		.port_type = WWAN_PORT_ADB,
		.debug = true,
	}, {
		.tx_ch = PORT_CH_MIPC_TX,
		.rx_ch = PORT_CH_MIPC_RX,
		.txq_index = Q_IDX_MBIM_MIPC,
		.rxq_index = Q_IDX_MBIM_MIPC,
		.path_id = CLDMA_ID_MD,
		.ops = &wwan_sub_port_ops,
		.name = "mipc",
		.port_type = WWAN_PORT_MIPC,
		.debug = true,
	}
};

static const struct t7xx_port_conf t7xx_early_port_conf[] = {
	{
		.tx_ch = PORT_CH_UNIMPORTANT,
		.rx_ch = PORT_CH_UNIMPORTANT,
		.txq_index = CLDMA_Q_IDX_DUMP,
		.rxq_index = CLDMA_Q_IDX_DUMP,
		.txq_exp_index = CLDMA_Q_IDX_DUMP,
		.rxq_exp_index = CLDMA_Q_IDX_DUMP,
		.path_id = CLDMA_ID_AP,
		.ops = &wwan_sub_port_ops,
		.name = "fastboot",
		.port_type = WWAN_PORT_FASTBOOT,
	},
};

static struct t7xx_port *t7xx_proxy_get_port_by_ch(struct port_proxy *port_prox, enum port_ch ch)
{
	const struct t7xx_port_conf *port_conf;
	struct t7xx_port *port;
	int i;

	for_each_proxy_port(i, port, port_prox) {
		port_conf = port->port_conf;
		if (port_conf->rx_ch == ch || port_conf->tx_ch == ch)
			return port;
	}

	return NULL;
}

static u16 t7xx_port_next_rx_seq_num(struct t7xx_port *port, struct ccci_header *ccci_h)
{
	u32 status = le32_to_cpu(ccci_h->status);
	u16 seq_num, next_seq_num;
	bool assert_bit;

	seq_num = FIELD_GET(CCCI_H_SEQ_FLD, status);
	next_seq_num = (seq_num + 1) & FIELD_MAX(CCCI_H_SEQ_FLD);
	assert_bit = status & CCCI_H_AST_BIT;
	if (!assert_bit || port->seq_nums[MTK_RX] == INVALID_SEQ_NUM)
		return next_seq_num;

	if (seq_num != port->seq_nums[MTK_RX])
		dev_warn_ratelimited(port->dev,
				     "seq num out-of-order %u != %u (header %X, len %X)\n",
				     seq_num, port->seq_nums[MTK_RX],
				     le32_to_cpu(ccci_h->packet_header),
				     le32_to_cpu(ccci_h->packet_len));

	return next_seq_num;
}

void t7xx_port_proxy_reset(struct port_proxy *port_prox)
{
	struct t7xx_port *port;
	int i;

	for_each_proxy_port(i, port, port_prox) {
		port->seq_nums[MTK_RX] = INVALID_SEQ_NUM;
		port->seq_nums[MTK_TX] = 0;
	}
}

static int t7xx_port_get_queue_no(struct t7xx_port *port)
{
	const struct t7xx_port_conf *port_conf = port->port_conf;
	struct t7xx_fsm_ctl *ctl = port->t7xx_dev->md->fsm_ctl;

	return t7xx_fsm_get_md_state(ctl) == MD_STATE_EXCEPTION ?
		port_conf->txq_exp_index : port_conf->txq_index;
}

static void t7xx_port_struct_init(struct t7xx_port *port)
{
	INIT_LIST_HEAD(&port->entry);
	INIT_LIST_HEAD(&port->queue_entry);
	skb_queue_head_init(&port->rx_skb_list);
	init_waitqueue_head(&port->rx_wq);
	port->seq_nums[MTK_RX] = INVALID_SEQ_NUM;
	port->seq_nums[MTK_TX] = 0;
	atomic_set(&port->usage_cnt, 0);
}

struct sk_buff *t7xx_port_alloc_skb(int payload)
{
	struct sk_buff *skb = __dev_alloc_skb(payload + sizeof(struct ccci_header), GFP_KERNEL);

	if (skb)
		skb_reserve(skb, sizeof(struct ccci_header));

	return skb;
}

struct sk_buff *t7xx_ctrl_alloc_skb(int payload)
{
	struct sk_buff *skb = t7xx_port_alloc_skb(payload + sizeof(struct ctrl_msg_header));

	if (skb)
		skb_reserve(skb, sizeof(struct ctrl_msg_header));

	return skb;
}

/**
 * t7xx_port_enqueue_skb() - Enqueue the received skb into the port's rx_skb_list.
 * @port: port context.
 * @skb: received skb.
 *
 * Return:
 * * 0		- Success.
 * * -ENOBUFS	- Not enough buffer space. Caller will try again later, skb is not consumed.
 */
int t7xx_port_enqueue_skb(struct t7xx_port *port, struct sk_buff *skb)
{
	unsigned long flags;

	spin_lock_irqsave(&port->rx_wq.lock, flags);
	if (port->rx_skb_list.qlen >= port->rx_length_th) {
		spin_unlock_irqrestore(&port->rx_wq.lock, flags);

		return -ENOBUFS;
	}
	__skb_queue_tail(&port->rx_skb_list, skb);
	spin_unlock_irqrestore(&port->rx_wq.lock, flags);

	wake_up_all(&port->rx_wq);
	return 0;
}

int t7xx_get_port_mtu(struct t7xx_port *port)
{
	enum cldma_id path_id = port->port_conf->path_id;
	int tx_qno = t7xx_port_get_queue_no(port);
	struct cldma_ctrl *md_ctrl;

	md_ctrl = port->t7xx_dev->md->md_ctrl[path_id];
	return md_ctrl->tx_ring[tx_qno].pkt_size;
}

int t7xx_port_send_raw_skb(struct t7xx_port *port, struct sk_buff *skb)
{
	enum cldma_id path_id = port->port_conf->path_id;
	struct cldma_ctrl *md_ctrl;
	int ret, tx_qno;

	md_ctrl = port->t7xx_dev->md->md_ctrl[path_id];
	tx_qno = t7xx_port_get_queue_no(port);
	ret = t7xx_cldma_send_skb(md_ctrl, tx_qno, skb);
	if (ret)
		dev_err(port->dev, "Failed to send skb: %d\n", ret);

	return ret;
}

static int t7xx_port_send_ccci_skb(struct t7xx_port *port, struct sk_buff *skb,
				   unsigned int pkt_header, unsigned int ex_msg)
{
	const struct t7xx_port_conf *port_conf = port->port_conf;
	struct ccci_header *ccci_h;
	u32 status;
	int ret;

	ccci_h = skb_push(skb, sizeof(*ccci_h));
	status = FIELD_PREP(CCCI_H_CHN_FLD, port_conf->tx_ch) |
		 FIELD_PREP(CCCI_H_SEQ_FLD, port->seq_nums[MTK_TX]) | CCCI_H_AST_BIT;
	ccci_h->status = cpu_to_le32(status);
	ccci_h->packet_header = cpu_to_le32(pkt_header);
	ccci_h->packet_len = cpu_to_le32(skb->len);
	ccci_h->ex_msg = cpu_to_le32(ex_msg);

	ret = t7xx_port_send_raw_skb(port, skb);
	if (ret)
		return ret;

	port->seq_nums[MTK_TX]++;
	return 0;
}

int t7xx_port_send_ctl_skb(struct t7xx_port *port, struct sk_buff *skb, unsigned int msg,
			   unsigned int ex_msg)
{
	struct ctrl_msg_header *ctrl_msg_h;
	unsigned int msg_len = skb->len;
	u32 pkt_header = 0;

	ctrl_msg_h = skb_push(skb, sizeof(*ctrl_msg_h));
	ctrl_msg_h->ctrl_msg_id = cpu_to_le32(msg);
	ctrl_msg_h->ex_msg = cpu_to_le32(ex_msg);
	ctrl_msg_h->data_length = cpu_to_le32(msg_len);

	if (!msg_len)
		pkt_header = CCCI_HEADER_NO_DATA;

	return t7xx_port_send_ccci_skb(port, skb, pkt_header, ex_msg);
}

int t7xx_port_send_skb(struct t7xx_port *port, struct sk_buff *skb, unsigned int pkt_header,
		       unsigned int ex_msg)
{
	struct t7xx_fsm_ctl *ctl = port->t7xx_dev->md->fsm_ctl;
	unsigned int fsm_state;

	fsm_state = t7xx_fsm_get_ctl_state(ctl);
	if (fsm_state != FSM_STATE_PRE_START) {
		const struct t7xx_port_conf *port_conf = port->port_conf;
		enum md_state md_state = t7xx_fsm_get_md_state(ctl);

		switch (md_state) {
		case MD_STATE_EXCEPTION:
			if (port_conf->tx_ch != PORT_CH_MD_LOG_TX)
				return -EBUSY;
			break;

		case MD_STATE_WAITING_FOR_HS1:
		case MD_STATE_WAITING_FOR_HS2:
		case MD_STATE_STOPPED:
		case MD_STATE_WAITING_TO_STOP:
		case MD_STATE_INVALID:
			return -ENODEV;

		default:
			break;
		}
	}

	return t7xx_port_send_ccci_skb(port, skb, pkt_header, ex_msg);
}

static void t7xx_proxy_setup_ch_mapping(struct port_proxy *port_prox)
{
	struct t7xx_port *port;

	int i, j;

	for (i = 0; i < ARRAY_SIZE(port_prox->rx_ch_ports); i++)
		INIT_LIST_HEAD(&port_prox->rx_ch_ports[i]);

	for (j = 0; j < ARRAY_SIZE(port_prox->queue_ports); j++) {
		for (i = 0; i < ARRAY_SIZE(port_prox->queue_ports[j]); i++)
			INIT_LIST_HEAD(&port_prox->queue_ports[j][i]);
	}

	for_each_proxy_port(i, port, port_prox) {
		const struct t7xx_port_conf *port_conf = port->port_conf;
		enum cldma_id path_id = port_conf->path_id;
		u8 ch_id;

		ch_id = FIELD_GET(PORT_CH_ID_MASK, port_conf->rx_ch);
		list_add_tail(&port->entry, &port_prox->rx_ch_ports[ch_id]);
		list_add_tail(&port->queue_entry,
			      &port_prox->queue_ports[path_id][port_conf->rxq_index]);
	}
}

/**
 * t7xx_port_proxy_recv_skb_from_dedicated_queue() - Dispatch early port received skb.
 * @queue: CLDMA queue.
 * @skb: Socket buffer.
 *
 * Return:
 ** 0		- Packet consumed.
 ** -ERROR	- Failed to process skb.
 */
int t7xx_port_proxy_recv_skb_from_dedicated_queue(struct cldma_queue *queue, struct sk_buff *skb)
{
	struct t7xx_pci_dev *t7xx_dev = queue->md_ctrl->t7xx_dev;
	struct port_proxy *port_prox = t7xx_dev->md->port_prox;
	const struct t7xx_port_conf *port_conf;
	struct t7xx_port *port;
	int ret;

	port = &port_prox->ports[0];
	if (WARN_ON_ONCE(port->port_conf->rxq_index != queue->index)) {
		dev_kfree_skb_any(skb);
		return -EINVAL;
	}

	port_conf = port->port_conf;
	ret = port_conf->ops->recv_skb(port, skb);
	if (ret < 0 && ret != -ENOBUFS) {
		dev_err(port->dev, "drop on RX ch %d, %d\n", port_conf->rx_ch, ret);
		dev_kfree_skb_any(skb);
	}

	return ret;
}

static struct t7xx_port *t7xx_port_proxy_find_port(struct t7xx_pci_dev *t7xx_dev,
						   struct cldma_queue *queue, u16 channel)
{
	struct port_proxy *port_prox = t7xx_dev->md->port_prox;
	struct list_head *port_list;
	struct t7xx_port *port;
	u8 ch_id;

	ch_id = FIELD_GET(PORT_CH_ID_MASK, channel);
	port_list = &port_prox->rx_ch_ports[ch_id];
	list_for_each_entry(port, port_list, entry) {
		const struct t7xx_port_conf *port_conf = port->port_conf;

		if (queue->md_ctrl->hif_id == port_conf->path_id &&
		    channel == port_conf->rx_ch)
			return port;
	}

	return NULL;
}

/**
 * t7xx_port_proxy_recv_skb() - Dispatch received skb.
 * @queue: CLDMA queue.
 * @skb: Socket buffer.
 *
 * Return:
 ** 0		- Packet consumed.
 ** -ERROR	- Failed to process skb.
 */
int t7xx_port_proxy_recv_skb(struct cldma_queue *queue, struct sk_buff *skb)
{
	struct ccci_header *ccci_h = (struct ccci_header *)skb->data;
	struct t7xx_pci_dev *t7xx_dev = queue->md_ctrl->t7xx_dev;
	struct t7xx_fsm_ctl *ctl = t7xx_dev->md->fsm_ctl;
	struct device *dev = queue->md_ctrl->dev;
	const struct t7xx_port_conf *port_conf;
	struct t7xx_port *port;
	u16 seq_num, channel;
	int ret;

	channel = FIELD_GET(CCCI_H_CHN_FLD, le32_to_cpu(ccci_h->status));
	if (t7xx_fsm_get_md_state(ctl) == MD_STATE_INVALID) {
		dev_err_ratelimited(dev, "Packet drop on channel 0x%x, modem not ready\n", channel);
		goto drop_skb;
	}

	port = t7xx_port_proxy_find_port(t7xx_dev, queue, channel);
	if (!port) {
		dev_err_ratelimited(dev, "Packet drop on channel 0x%x, port not found\n", channel);
		goto drop_skb;
	}

	seq_num = t7xx_port_next_rx_seq_num(port, ccci_h);
	port_conf = port->port_conf;
	skb_pull(skb, sizeof(*ccci_h));

	ret = port_conf->ops->recv_skb(port, skb);
	/* Error indicates to try again later */
	if (ret) {
		skb_push(skb, sizeof(*ccci_h));
		return ret;
	}

	port->seq_nums[MTK_RX] = seq_num;
	return 0;

drop_skb:
	dev_kfree_skb_any(skb);
	return 0;
}

/**
 * t7xx_port_proxy_md_status_notify() - Notify all ports of state.
 *@port_prox: The port_proxy pointer.
 *@state: State.
 *
 * Called by t7xx_fsm. Used to dispatch modem status for all ports,
 * which want to know MD state transition.
 */
void t7xx_port_proxy_md_status_notify(struct port_proxy *port_prox, unsigned int state)
{
	struct t7xx_port *port;
	int i;

	for_each_proxy_port(i, port, port_prox) {
		const struct t7xx_port_conf *port_conf = port->port_conf;

		if (port_conf->ops->md_state_notify)
			port_conf->ops->md_state_notify(port, state);
	}
}

static void t7xx_proxy_init_all_ports(struct t7xx_modem *md)
{
	struct port_proxy *port_prox = md->port_prox;
	struct t7xx_port *port;
	int i;

	for_each_proxy_port(i, port, port_prox) {
		const struct t7xx_port_conf *port_conf = port->port_conf;

		t7xx_port_struct_init(port);

		if (port_conf->tx_ch == PORT_CH_CONTROL_TX)
			md->core_md.ctl_port = port;

		if (port_conf->tx_ch == PORT_CH_AP_CONTROL_TX)
			md->core_ap.ctl_port = port;

		port->t7xx_dev = md->t7xx_dev;
		port->dev = &md->t7xx_dev->pdev->dev;
		spin_lock_init(&port->port_update_lock);
		port->chan_enable = false;

		if (!port_conf->debug &&
		    port_conf->ops &&
		    port_conf->ops->init)
			port_conf->ops->init(port);
	}

	t7xx_proxy_setup_ch_mapping(port_prox);
}

void t7xx_proxy_debug_ports_show(struct t7xx_pci_dev *t7xx_dev, bool show)
{
	struct port_proxy *port_prox = t7xx_dev->md->port_prox;
	struct t7xx_port *port;
	int i;

	for_each_proxy_port(i, port, port_prox) {
		const struct t7xx_port_conf *port_conf = port->port_conf;

		if (port_conf->debug && port_conf->ops) {
			if (show && port_conf->ops->init)
				port_conf->ops->init(port);
			else if (!show && port_conf->ops->uninit)
				port_conf->ops->uninit(port);
		}
	}
}

void t7xx_port_proxy_set_cfg(struct t7xx_modem *md, enum port_cfg_id cfg_id)
{
	struct port_proxy *port_prox = md->port_prox;
	const struct t7xx_port_conf *port_conf;
	u32 port_count;
	int i;

	t7xx_port_proxy_uninit(port_prox);

	if (cfg_id == PORT_CFG_ID_EARLY) {
		port_conf = t7xx_early_port_conf;
		port_count = ARRAY_SIZE(t7xx_early_port_conf);
	} else {
		port_conf = t7xx_port_conf;
		port_count = ARRAY_SIZE(t7xx_port_conf);
	}

	for (i = 0; i < port_count; i++)
		port_prox->ports[i].port_conf = &port_conf[i];

	port_prox->cfg_id = cfg_id;
	port_prox->port_count = port_count;

	t7xx_proxy_init_all_ports(md);
}

static int t7xx_proxy_alloc(struct t7xx_modem *md)
{
	struct device *dev = &md->t7xx_dev->pdev->dev;
	struct port_proxy *port_prox;

	port_prox = devm_kzalloc(dev,
				 struct_size(port_prox,
					     ports,
					     T7XX_MAX_POSSIBLE_PORTS_NUM),
				 GFP_KERNEL);
	if (!port_prox)
		return -ENOMEM;

	md->port_prox = port_prox;
	port_prox->dev = dev;

	return 0;
}

/**
 * t7xx_port_proxy_init() - Initialize ports.
 * @md: Modem.
 *
 * Create all port instances.
 *
 * Return:
 * * 0		- Success.
 * * -ERROR	- Error code from failure sub-initializations.
 */
int t7xx_port_proxy_init(struct t7xx_modem *md)
{
	int ret;

	ret = t7xx_proxy_alloc(md);
	if (ret)
		return ret;

	return 0;
}

void t7xx_port_proxy_uninit(struct port_proxy *port_prox)
{
	struct t7xx_port *port;
	int i;

	for_each_proxy_port(i, port, port_prox) {
		const struct t7xx_port_conf *port_conf = port->port_conf;

		if (port_conf->ops && port_conf->ops->uninit)
			port_conf->ops->uninit(port);
	}
}

int t7xx_port_proxy_chl_enable_disable(struct port_proxy *port_prox, unsigned int ch_id,
				       bool en_flag)
{
	struct t7xx_port *port = t7xx_proxy_get_port_by_ch(port_prox, ch_id);
	const struct t7xx_port_conf *port_conf;

	if (!port)
		return -EINVAL;

	port_conf = port->port_conf;

	if (en_flag) {
		if (port_conf->ops->enable_chl)
			port_conf->ops->enable_chl(port);
	} else {
		if (port_conf->ops->disable_chl)
			port_conf->ops->disable_chl(port);
	}

	return 0;
}
