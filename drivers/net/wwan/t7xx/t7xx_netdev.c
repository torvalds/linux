// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Chandrashekar Devegowda <chandrashekar.devegowda@intel.com>
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 *
 * Contributors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *  Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *  Eliot Lee <eliot.lee@intel.com>
 *  Moises Veleta <moises.veleta@intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/netdev_features.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/wwan.h>
#include <net/pkt_sched.h>

#include "t7xx_hif_dpmaif_rx.h"
#include "t7xx_hif_dpmaif_tx.h"
#include "t7xx_netdev.h"
#include "t7xx_pci.h"
#include "t7xx_port_proxy.h"
#include "t7xx_state_monitor.h"

#define IP_MUX_SESSION_DEFAULT	0

static int t7xx_ccmni_open(struct net_device *dev)
{
	struct t7xx_ccmni *ccmni = wwan_netdev_drvpriv(dev);

	netif_carrier_on(dev);
	netif_tx_start_all_queues(dev);
	atomic_inc(&ccmni->usage);
	return 0;
}

static int t7xx_ccmni_close(struct net_device *dev)
{
	struct t7xx_ccmni *ccmni = wwan_netdev_drvpriv(dev);

	atomic_dec(&ccmni->usage);
	netif_carrier_off(dev);
	netif_tx_disable(dev);
	return 0;
}

static int t7xx_ccmni_send_packet(struct t7xx_ccmni *ccmni, struct sk_buff *skb,
				  unsigned int txq_number)
{
	struct t7xx_ccmni_ctrl *ctlb = ccmni->ctlb;
	struct t7xx_skb_cb *skb_cb = T7XX_SKB_CB(skb);

	skb_cb->netif_idx = ccmni->index;

	if (t7xx_dpmaif_tx_send_skb(ctlb->hif_ctrl, txq_number, skb))
		return NETDEV_TX_BUSY;

	return 0;
}

static netdev_tx_t t7xx_ccmni_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct t7xx_ccmni *ccmni = wwan_netdev_drvpriv(dev);
	int skb_len = skb->len;

	/* If MTU is changed or there is no headroom, drop the packet */
	if (skb->len > dev->mtu || skb_headroom(skb) < sizeof(struct ccci_header)) {
		dev_kfree_skb(skb);
		dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	if (t7xx_ccmni_send_packet(ccmni, skb, DPMAIF_TX_DEFAULT_QUEUE))
		return NETDEV_TX_BUSY;

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb_len;

	return NETDEV_TX_OK;
}

static void t7xx_ccmni_tx_timeout(struct net_device *dev, unsigned int __always_unused txqueue)
{
	struct t7xx_ccmni *ccmni = netdev_priv(dev);

	dev->stats.tx_errors++;

	if (atomic_read(&ccmni->usage) > 0)
		netif_tx_wake_all_queues(dev);
}

static const struct net_device_ops ccmni_netdev_ops = {
	.ndo_open	  = t7xx_ccmni_open,
	.ndo_stop	  = t7xx_ccmni_close,
	.ndo_start_xmit   = t7xx_ccmni_start_xmit,
	.ndo_tx_timeout   = t7xx_ccmni_tx_timeout,
};

static void t7xx_ccmni_start(struct t7xx_ccmni_ctrl *ctlb)
{
	struct t7xx_ccmni *ccmni;
	int i;

	for (i = 0; i < ctlb->nic_dev_num; i++) {
		ccmni = ctlb->ccmni_inst[i];
		if (!ccmni)
			continue;

		if (atomic_read(&ccmni->usage) > 0) {
			netif_tx_start_all_queues(ccmni->dev);
			netif_carrier_on(ccmni->dev);
		}
	}
}

static void t7xx_ccmni_pre_stop(struct t7xx_ccmni_ctrl *ctlb)
{
	struct t7xx_ccmni *ccmni;
	int i;

	for (i = 0; i < ctlb->nic_dev_num; i++) {
		ccmni = ctlb->ccmni_inst[i];
		if (!ccmni)
			continue;

		if (atomic_read(&ccmni->usage) > 0)
			netif_tx_disable(ccmni->dev);
	}
}

static void t7xx_ccmni_post_stop(struct t7xx_ccmni_ctrl *ctlb)
{
	struct t7xx_ccmni *ccmni;
	int i;

	for (i = 0; i < ctlb->nic_dev_num; i++) {
		ccmni = ctlb->ccmni_inst[i];
		if (!ccmni)
			continue;

		if (atomic_read(&ccmni->usage) > 0)
			netif_carrier_off(ccmni->dev);
	}
}

static void t7xx_ccmni_wwan_setup(struct net_device *dev)
{
	dev->hard_header_len += sizeof(struct ccci_header);

	dev->mtu = ETH_DATA_LEN;
	dev->max_mtu = CCMNI_MTU_MAX;
	BUILD_BUG_ON(CCMNI_MTU_MAX > DPMAIF_HW_MTU_SIZE);

	dev->tx_queue_len = DEFAULT_TX_QUEUE_LEN;
	dev->watchdog_timeo = CCMNI_NETDEV_WDT_TO;

	dev->flags = IFF_POINTOPOINT | IFF_NOARP;

	dev->features = NETIF_F_VLAN_CHALLENGED;

	dev->features |= NETIF_F_SG;
	dev->hw_features |= NETIF_F_SG;

	dev->features |= NETIF_F_HW_CSUM;
	dev->hw_features |= NETIF_F_HW_CSUM;

	dev->features |= NETIF_F_RXCSUM;
	dev->hw_features |= NETIF_F_RXCSUM;

	dev->needs_free_netdev = true;

	dev->type = ARPHRD_NONE;

	dev->netdev_ops = &ccmni_netdev_ops;
}

static int t7xx_ccmni_wwan_newlink(void *ctxt, struct net_device *dev, u32 if_id,
				   struct netlink_ext_ack *extack)
{
	struct t7xx_ccmni_ctrl *ctlb = ctxt;
	struct t7xx_ccmni *ccmni;
	int ret;

	if (if_id >= ARRAY_SIZE(ctlb->ccmni_inst))
		return -EINVAL;

	ccmni = wwan_netdev_drvpriv(dev);
	ccmni->index = if_id;
	ccmni->ctlb = ctlb;
	ccmni->dev = dev;
	atomic_set(&ccmni->usage, 0);
	ctlb->ccmni_inst[if_id] = ccmni;

	ret = register_netdevice(dev);
	if (ret)
		return ret;

	netif_device_attach(dev);
	return 0;
}

static void t7xx_ccmni_wwan_dellink(void *ctxt, struct net_device *dev, struct list_head *head)
{
	struct t7xx_ccmni *ccmni = wwan_netdev_drvpriv(dev);
	struct t7xx_ccmni_ctrl *ctlb = ctxt;
	u8 if_id = ccmni->index;

	if (if_id >= ARRAY_SIZE(ctlb->ccmni_inst))
		return;

	if (WARN_ON(ctlb->ccmni_inst[if_id] != ccmni))
		return;

	unregister_netdevice(dev);
}

static const struct wwan_ops ccmni_wwan_ops = {
	.priv_size = sizeof(struct t7xx_ccmni),
	.setup     = t7xx_ccmni_wwan_setup,
	.newlink   = t7xx_ccmni_wwan_newlink,
	.dellink   = t7xx_ccmni_wwan_dellink,
};

static int t7xx_ccmni_register_wwan(struct t7xx_ccmni_ctrl *ctlb)
{
	struct device *dev = ctlb->hif_ctrl->dev;
	int ret;

	if (ctlb->wwan_is_registered)
		return 0;

	/* WWAN core will create a netdev for the default IP MUX channel */
	ret = wwan_register_ops(dev, &ccmni_wwan_ops, ctlb, IP_MUX_SESSION_DEFAULT);
	if (ret < 0) {
		dev_err(dev, "Unable to register WWAN ops, %d\n", ret);
		return ret;
	}

	ctlb->wwan_is_registered = true;
	return 0;
}

static int t7xx_ccmni_md_state_callback(enum md_state state, void *para)
{
	struct t7xx_ccmni_ctrl *ctlb = para;
	struct device *dev;
	int ret = 0;

	dev = ctlb->hif_ctrl->dev;
	ctlb->md_sta = state;

	switch (state) {
	case MD_STATE_READY:
		ret = t7xx_ccmni_register_wwan(ctlb);
		if (!ret)
			t7xx_ccmni_start(ctlb);
		break;

	case MD_STATE_EXCEPTION:
	case MD_STATE_STOPPED:
		t7xx_ccmni_pre_stop(ctlb);

		ret = t7xx_dpmaif_md_state_callback(ctlb->hif_ctrl, state);
		if (ret < 0)
			dev_err(dev, "DPMAIF md state callback err, state=%d\n", state);

		t7xx_ccmni_post_stop(ctlb);
		break;

	case MD_STATE_WAITING_FOR_HS1:
	case MD_STATE_WAITING_TO_STOP:
		ret = t7xx_dpmaif_md_state_callback(ctlb->hif_ctrl, state);
		if (ret < 0)
			dev_err(dev, "DPMAIF md state callback err, state=%d\n", state);

		break;

	default:
		break;
	}

	return ret;
}

static void init_md_status_notifier(struct t7xx_pci_dev *t7xx_dev)
{
	struct t7xx_ccmni_ctrl	*ctlb = t7xx_dev->ccmni_ctlb;
	struct t7xx_fsm_notifier *md_status_notifier;

	md_status_notifier = &ctlb->md_status_notify;
	INIT_LIST_HEAD(&md_status_notifier->entry);
	md_status_notifier->notifier_fn = t7xx_ccmni_md_state_callback;
	md_status_notifier->data = ctlb;

	t7xx_fsm_notifier_register(t7xx_dev->md, md_status_notifier);
}

static void t7xx_ccmni_recv_skb(struct t7xx_pci_dev *t7xx_dev, struct sk_buff *skb)
{
	struct t7xx_skb_cb *skb_cb;
	struct net_device *net_dev;
	struct t7xx_ccmni *ccmni;
	int pkt_type, skb_len;
	u8 netif_id;

	skb_cb = T7XX_SKB_CB(skb);
	netif_id = skb_cb->netif_idx;
	ccmni = t7xx_dev->ccmni_ctlb->ccmni_inst[netif_id];
	if (!ccmni) {
		dev_kfree_skb(skb);
		return;
	}

	net_dev = ccmni->dev;
	skb->dev = net_dev;

	pkt_type = skb_cb->rx_pkt_type;
	if (pkt_type == PKT_TYPE_IP6)
		skb->protocol = htons(ETH_P_IPV6);
	else
		skb->protocol = htons(ETH_P_IP);

	skb_len = skb->len;
	netif_rx(skb);
	net_dev->stats.rx_packets++;
	net_dev->stats.rx_bytes += skb_len;
}

static void t7xx_ccmni_queue_tx_irq_notify(struct t7xx_ccmni_ctrl *ctlb, int qno)
{
	struct t7xx_ccmni *ccmni = ctlb->ccmni_inst[0];
	struct netdev_queue *net_queue;

	if (netif_running(ccmni->dev) && atomic_read(&ccmni->usage) > 0) {
		net_queue = netdev_get_tx_queue(ccmni->dev, qno);
		if (netif_tx_queue_stopped(net_queue))
			netif_tx_wake_queue(net_queue);
	}
}

static void t7xx_ccmni_queue_tx_full_notify(struct t7xx_ccmni_ctrl *ctlb, int qno)
{
	struct t7xx_ccmni *ccmni = ctlb->ccmni_inst[0];
	struct netdev_queue *net_queue;

	if (atomic_read(&ccmni->usage) > 0) {
		netdev_err(ccmni->dev, "TX queue %d is full\n", qno);
		net_queue = netdev_get_tx_queue(ccmni->dev, qno);
		netif_tx_stop_queue(net_queue);
	}
}

static void t7xx_ccmni_queue_state_notify(struct t7xx_pci_dev *t7xx_dev,
					  enum dpmaif_txq_state state, int qno)
{
	struct t7xx_ccmni_ctrl *ctlb = t7xx_dev->ccmni_ctlb;

	if (ctlb->md_sta != MD_STATE_READY)
		return;

	if (!ctlb->ccmni_inst[0]) {
		dev_warn(&t7xx_dev->pdev->dev, "No netdev registered yet\n");
		return;
	}

	if (state == DMPAIF_TXQ_STATE_IRQ)
		t7xx_ccmni_queue_tx_irq_notify(ctlb, qno);
	else if (state == DMPAIF_TXQ_STATE_FULL)
		t7xx_ccmni_queue_tx_full_notify(ctlb, qno);
}

int t7xx_ccmni_init(struct t7xx_pci_dev *t7xx_dev)
{
	struct device *dev = &t7xx_dev->pdev->dev;
	struct t7xx_ccmni_ctrl *ctlb;

	ctlb = devm_kzalloc(dev, sizeof(*ctlb), GFP_KERNEL);
	if (!ctlb)
		return -ENOMEM;

	t7xx_dev->ccmni_ctlb = ctlb;
	ctlb->t7xx_dev = t7xx_dev;
	ctlb->callbacks.state_notify = t7xx_ccmni_queue_state_notify;
	ctlb->callbacks.recv_skb = t7xx_ccmni_recv_skb;
	ctlb->nic_dev_num = NIC_DEV_DEFAULT;

	ctlb->hif_ctrl = t7xx_dpmaif_hif_init(t7xx_dev, &ctlb->callbacks);
	if (!ctlb->hif_ctrl)
		return -ENOMEM;

	init_md_status_notifier(t7xx_dev);
	return 0;
}

void t7xx_ccmni_exit(struct t7xx_pci_dev *t7xx_dev)
{
	struct t7xx_ccmni_ctrl *ctlb = t7xx_dev->ccmni_ctlb;

	t7xx_fsm_notifier_unregister(t7xx_dev->md, &ctlb->md_status_notify);

	if (ctlb->wwan_is_registered) {
		wwan_unregister_ops(&t7xx_dev->pdev->dev);
		ctlb->wwan_is_registered = false;
	}

	t7xx_dpmaif_hif_exit(ctlb->hif_ctrl);
}
