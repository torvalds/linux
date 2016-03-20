/*
 * Networking AIM - Networking Application Interface Module for MostCore
 *
 * Copyright (C) 2015, Microchip Technology Germany II GmbH & Co. KG
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This file is licensed under GPLv2.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/kobject.h>
#include "mostcore.h"
#include "networking.h"

#define MEP_HDR_LEN 8
#define MDP_HDR_LEN 16
#define MAMAC_DATA_LEN (1024 - MDP_HDR_LEN)

#define PMHL 5

#define PMS_TELID_UNSEGM_MAMAC	0x0A
#define PMS_FIFONO_MDP		0x01
#define PMS_FIFONO_MEP		0x04
#define PMS_MSGTYPE_DATA	0x04
#define PMS_DEF_PRIO		0
#define MEP_DEF_RETRY		15

#define PMS_FIFONO_MASK		0x07
#define PMS_FIFONO_SHIFT	3
#define PMS_RETRY_SHIFT		4
#define PMS_TELID_MASK		0x0F
#define PMS_TELID_SHIFT		4

#define HB(value)		((u8)((u16)(value) >> 8))
#define LB(value)		((u8)(value))

#define EXTRACT_BIT_SET(bitset_name, value) \
	(((value) >> bitset_name##_SHIFT) & bitset_name##_MASK)

#define PMS_IS_MEP(buf, len) \
	((len) > MEP_HDR_LEN && \
	 EXTRACT_BIT_SET(PMS_FIFONO, (buf)[3]) == PMS_FIFONO_MEP)

#define PMS_IS_MAMAC(buf, len) \
	((len) > MDP_HDR_LEN && \
	 EXTRACT_BIT_SET(PMS_FIFONO, (buf)[3]) == PMS_FIFONO_MDP && \
	 EXTRACT_BIT_SET(PMS_TELID, (buf)[14]) == PMS_TELID_UNSEGM_MAMAC)

struct net_dev_channel {
	bool linked;
	int ch_id;
};

struct net_dev_context {
	struct most_interface *iface;
	bool channels_opened;
	bool is_mamac;
	unsigned char link_stat;
	struct net_device *dev;
	struct net_dev_channel rx;
	struct net_dev_channel tx;
	struct list_head list;
};

static struct list_head net_devices = LIST_HEAD_INIT(net_devices);
static struct spinlock list_lock;
static struct most_aim aim;

static int skb_to_mamac(const struct sk_buff *skb, struct mbo *mbo)
{
	u8 *buff = mbo->virt_address;
	const u8 broadcast[] = { 0x03, 0xFF };
	const u8 *dest_addr = skb->data + 4;
	const u8 *eth_type = skb->data + 12;
	unsigned int payload_len = skb->len - ETH_HLEN;
	unsigned int mdp_len = payload_len + MDP_HDR_LEN;

	if (mbo->buffer_length < mdp_len) {
		pr_err("drop: too small buffer! (%d for %d)\n",
		       mbo->buffer_length, mdp_len);
		return -EINVAL;
	}

	if (skb->len < ETH_HLEN) {
		pr_err("drop: too small packet! (%d)\n", skb->len);
		return -EINVAL;
	}

	if (dest_addr[0] == 0xFF && dest_addr[1] == 0xFF)
		dest_addr = broadcast;

	*buff++ = HB(mdp_len - 2);
	*buff++ = LB(mdp_len - 2);

	*buff++ = PMHL;
	*buff++ = (PMS_FIFONO_MDP << PMS_FIFONO_SHIFT) | PMS_MSGTYPE_DATA;
	*buff++ = PMS_DEF_PRIO;
	*buff++ = dest_addr[0];
	*buff++ = dest_addr[1];
	*buff++ = 0x00;

	*buff++ = HB(payload_len + 6);
	*buff++ = LB(payload_len + 6);

	/* end of FPH here */

	*buff++ = eth_type[0];
	*buff++ = eth_type[1];
	*buff++ = 0;
	*buff++ = 0;

	*buff++ = PMS_TELID_UNSEGM_MAMAC << 4 | HB(payload_len);
	*buff++ = LB(payload_len);

	memcpy(buff, skb->data + ETH_HLEN, payload_len);
	mbo->buffer_length = mdp_len;
	return 0;
}

static int skb_to_mep(const struct sk_buff *skb, struct mbo *mbo)
{
	u8 *buff = mbo->virt_address;
	unsigned int mep_len = skb->len + MEP_HDR_LEN;

	if (mbo->buffer_length < mep_len) {
		pr_err("drop: too small buffer! (%d for %d)\n",
		       mbo->buffer_length, mep_len);
		return -EINVAL;
	}

	*buff++ = HB(mep_len - 2);
	*buff++ = LB(mep_len - 2);

	*buff++ = PMHL;
	*buff++ = (PMS_FIFONO_MEP << PMS_FIFONO_SHIFT) | PMS_MSGTYPE_DATA;
	*buff++ = (MEP_DEF_RETRY << PMS_RETRY_SHIFT) | PMS_DEF_PRIO;
	*buff++ = 0;
	*buff++ = 0;
	*buff++ = 0;

	memcpy(buff, skb->data, skb->len);
	mbo->buffer_length = mep_len;
	return 0;
}

static int most_nd_set_mac_address(struct net_device *dev, void *p)
{
	struct net_dev_context *nd = dev->ml_priv;
	int err = eth_mac_addr(dev, p);

	if (err)
		return err;

	BUG_ON(nd->dev != dev);

	nd->is_mamac =
		(dev->dev_addr[0] == 0 && dev->dev_addr[1] == 0 &&
		 dev->dev_addr[2] == 0 && dev->dev_addr[3] == 0);

	/*
	 * Set default MTU for the given packet type.
	 * It is still possible to change MTU using ip tools afterwards.
	 */
	dev->mtu = nd->is_mamac ? MAMAC_DATA_LEN : ETH_DATA_LEN;

	return 0;
}

static int most_nd_open(struct net_device *dev)
{
	struct net_dev_context *nd = dev->ml_priv;

	netdev_info(dev, "open net device\n");

	BUG_ON(nd->dev != dev);

	if (nd->channels_opened)
		return -EFAULT;

	BUG_ON(!nd->tx.linked || !nd->rx.linked);

	if (most_start_channel(nd->iface, nd->rx.ch_id, &aim)) {
		netdev_err(dev, "most_start_channel() failed\n");
		return -EBUSY;
	}

	if (most_start_channel(nd->iface, nd->tx.ch_id, &aim)) {
		netdev_err(dev, "most_start_channel() failed\n");
		most_stop_channel(nd->iface, nd->rx.ch_id, &aim);
		return -EBUSY;
	}

	nd->channels_opened = true;

	if (nd->is_mamac) {
		nd->link_stat = 1;
		netif_wake_queue(dev);
	} else {
		nd->iface->request_netinfo(nd->iface, nd->tx.ch_id);
	}

	return 0;
}

static int most_nd_stop(struct net_device *dev)
{
	struct net_dev_context *nd = dev->ml_priv;

	netdev_info(dev, "stop net device\n");

	BUG_ON(nd->dev != dev);
	netif_stop_queue(dev);

	if (nd->channels_opened) {
		most_stop_channel(nd->iface, nd->rx.ch_id, &aim);
		most_stop_channel(nd->iface, nd->tx.ch_id, &aim);
		nd->channels_opened = false;
	}

	return 0;
}

static netdev_tx_t most_nd_start_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct net_dev_context *nd = dev->ml_priv;
	struct mbo *mbo;
	int ret;

	BUG_ON(nd->dev != dev);

	mbo = most_get_mbo(nd->iface, nd->tx.ch_id, &aim);

	if (!mbo) {
		netif_stop_queue(dev);
		dev->stats.tx_fifo_errors++;
		return NETDEV_TX_BUSY;
	}

	if (nd->is_mamac)
		ret = skb_to_mamac(skb, mbo);
	else
		ret = skb_to_mep(skb, mbo);

	if (ret) {
		most_put_mbo(mbo);
		dev->stats.tx_dropped++;
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	most_submit_mbo(mbo);
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops most_nd_ops = {
	.ndo_open = most_nd_open,
	.ndo_stop = most_nd_stop,
	.ndo_start_xmit = most_nd_start_xmit,
	.ndo_set_mac_address = most_nd_set_mac_address,
};

static void most_nd_setup(struct net_device *dev)
{
	netdev_info(dev, "setup net device\n");
	ether_setup(dev);
	dev->netdev_ops = &most_nd_ops;
}

static void most_net_rm_netdev_safe(struct net_dev_context *nd)
{
	if (!nd->dev)
		return;

	pr_info("remove net device %p\n", nd->dev);

	unregister_netdev(nd->dev);
	free_netdev(nd->dev);
	nd->dev = NULL;
}

static struct net_dev_context *get_net_dev_context(
	struct most_interface *iface)
{
	struct net_dev_context *nd, *tmp;

	spin_lock(&list_lock);
	list_for_each_entry_safe(nd, tmp, &net_devices, list) {
		if (nd->iface == iface) {
			spin_unlock(&list_lock);
			return nd;
		}
	}
	spin_unlock(&list_lock);
	return NULL;
}

static int aim_probe_channel(struct most_interface *iface, int channel_idx,
			     struct most_channel_config *ccfg,
			     struct kobject *parent, char *name)
{
	struct net_dev_context *nd;
	struct net_dev_channel *ch;

	if (!iface)
		return -EINVAL;

	if (ccfg->data_type != MOST_CH_ASYNC)
		return -EINVAL;

	nd = get_net_dev_context(iface);

	if (!nd) {
		nd = kzalloc(sizeof(*nd), GFP_KERNEL);
		if (!nd)
			return -ENOMEM;

		nd->iface = iface;

		spin_lock(&list_lock);
		list_add(&nd->list, &net_devices);
		spin_unlock(&list_lock);
	}

	ch = ccfg->direction == MOST_CH_TX ? &nd->tx : &nd->rx;
	if (ch->linked) {
		pr_err("only one channel per instance & direction allowed\n");
		return -EINVAL;
	}

	if (nd->tx.linked || nd->rx.linked) {
		struct net_device *dev =
			alloc_netdev(0, "meth%d", NET_NAME_UNKNOWN,
				     most_nd_setup);

		if (!dev) {
			pr_err("no memory for net_device\n");
			return -ENOMEM;
		}

		nd->dev = dev;
		ch->ch_id = channel_idx;
		ch->linked = true;

		dev->ml_priv = nd;
		if (register_netdev(dev)) {
			pr_err("registering net device failed\n");
			ch->linked = false;
			free_netdev(dev);
			return -EINVAL;
		}
	}

	ch->ch_id = channel_idx;
	ch->linked = true;

	return 0;
}

static int aim_disconnect_channel(struct most_interface *iface,
				  int channel_idx)
{
	struct net_dev_context *nd;
	struct net_dev_channel *ch;

	nd = get_net_dev_context(iface);
	if (!nd)
		return -EINVAL;

	if (nd->rx.linked && channel_idx == nd->rx.ch_id)
		ch = &nd->rx;
	else if (nd->tx.linked && channel_idx == nd->tx.ch_id)
		ch = &nd->tx;
	else
		return -EINVAL;

	ch->linked = false;

	/*
	 * do not call most_stop_channel() here, because channels are
	 * going to be closed in ndo_stop() after unregister_netdev()
	 */
	most_net_rm_netdev_safe(nd);

	if (!nd->rx.linked && !nd->tx.linked) {
		spin_lock(&list_lock);
		list_del(&nd->list);
		spin_unlock(&list_lock);
		kfree(nd);
	}

	return 0;
}

static int aim_resume_tx_channel(struct most_interface *iface,
				 int channel_idx)
{
	struct net_dev_context *nd;

	nd = get_net_dev_context(iface);
	if (!nd || !nd->channels_opened || nd->tx.ch_id != channel_idx)
		return 0;

	if (!nd->dev)
		return 0;

	netif_wake_queue(nd->dev);
	return 0;
}

static int aim_rx_data(struct mbo *mbo)
{
	const u32 zero = 0;
	struct net_dev_context *nd;
	char *buf = mbo->virt_address;
	u32 len = mbo->processed_length;
	struct sk_buff *skb;
	struct net_device *dev;
	unsigned int skb_len;

	nd = get_net_dev_context(mbo->ifp);
	if (!nd || !nd->channels_opened || nd->rx.ch_id != mbo->hdm_channel_id)
		return -EIO;

	dev = nd->dev;
	if (!dev) {
		pr_err_once("drop packet: missing net_device\n");
		return -EIO;
	}

	if (nd->is_mamac) {
		if (!PMS_IS_MAMAC(buf, len))
			return -EIO;

		skb = dev_alloc_skb(len - MDP_HDR_LEN + 2 * ETH_ALEN + 2);
	} else {
		if (!PMS_IS_MEP(buf, len))
			return -EIO;

		skb = dev_alloc_skb(len - MEP_HDR_LEN);
	}

	if (!skb) {
		dev->stats.rx_dropped++;
		pr_err_once("drop packet: no memory for skb\n");
		goto out;
	}

	skb->dev = dev;

	if (nd->is_mamac) {
		/* dest */
		ether_addr_copy(skb_put(skb, ETH_ALEN), dev->dev_addr);

		/* src */
		memcpy(skb_put(skb, 4), &zero, 4);
		memcpy(skb_put(skb, 2), buf + 5, 2);

		/* eth type */
		memcpy(skb_put(skb, 2), buf + 10, 2);

		buf += MDP_HDR_LEN;
		len -= MDP_HDR_LEN;
	} else {
		buf += MEP_HDR_LEN;
		len -= MEP_HDR_LEN;
	}

	memcpy(skb_put(skb, len), buf, len);
	skb->protocol = eth_type_trans(skb, dev);
	skb_len = skb->len;
	if (netif_rx(skb) == NET_RX_SUCCESS) {
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += skb_len;
	} else {
		dev->stats.rx_dropped++;
	}

out:
	most_put_mbo(mbo);
	return 0;
}

static struct most_aim aim = {
	.name = "networking",
	.probe_channel = aim_probe_channel,
	.disconnect_channel = aim_disconnect_channel,
	.tx_completion = aim_resume_tx_channel,
	.rx_completion = aim_rx_data,
};

static int __init most_net_init(void)
{
	pr_info("most_net_init()\n");
	spin_lock_init(&list_lock);
	return most_register_aim(&aim);
}

static void __exit most_net_exit(void)
{
	struct net_dev_context *nd, *tmp;

	spin_lock(&list_lock);
	list_for_each_entry_safe(nd, tmp, &net_devices, list) {
		list_del(&nd->list);
		spin_unlock(&list_lock);
		/*
		 * do not call most_stop_channel() here, because channels are
		 * going to be closed in ndo_stop() after unregister_netdev()
		 */
		most_net_rm_netdev_safe(nd);
		kfree(nd);
		spin_lock(&list_lock);
	}
	spin_unlock(&list_lock);

	most_deregister_aim(&aim);
	pr_info("most_net_exit()\n");
}

/**
 * most_deliver_netinfo - callback for HDM to be informed about HW's MAC
 * @param iface - most interface instance
 * @param link_stat - link status
 * @param mac_addr - MAC address
 */
void most_deliver_netinfo(struct most_interface *iface,
			  unsigned char link_stat, unsigned char *mac_addr)
{
	struct net_dev_context *nd;
	struct net_device *dev;

	pr_info("Received netinfo from %s\n", iface->description);

	nd = get_net_dev_context(iface);
	if (!nd)
		return;

	dev = nd->dev;
	if (!dev)
		return;

	if (mac_addr)
		ether_addr_copy(dev->dev_addr, mac_addr);

	if (nd->link_stat != link_stat) {
		nd->link_stat = link_stat;
		if (nd->link_stat)
			netif_wake_queue(dev);
		else
			netif_stop_queue(dev);
	}
}
EXPORT_SYMBOL(most_deliver_netinfo);

module_init(most_net_init);
module_exit(most_net_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrey Shvetsov <andrey.shvetsov@k2l.de>");
MODULE_DESCRIPTION("Networking Application Interface Module for MostCore");
