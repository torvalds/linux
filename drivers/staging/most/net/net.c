// SPDX-License-Identifier: GPL-2.0
/*
 * net.c - Networking component for Mostcore
 *
 * Copyright (C) 2015, Microchip Technology Germany II GmbH & Co. KG
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
#include <linux/most.h>

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

static inline bool pms_is_mamac(char *buf, u32 len)
{
	return (len > MDP_HDR_LEN &&
		EXTRACT_BIT_SET(PMS_FIFONO, buf[3]) == PMS_FIFONO_MDP &&
		EXTRACT_BIT_SET(PMS_TELID, buf[14]) == PMS_TELID_UNSEGM_MAMAC);
}

struct net_dev_channel {
	bool linked;
	int ch_id;
};

struct net_dev_context {
	struct most_interface *iface;
	bool is_mamac;
	struct net_device *dev;
	struct net_dev_channel rx;
	struct net_dev_channel tx;
	struct list_head list;
};

static struct list_head net_devices = LIST_HEAD_INIT(net_devices);
static DEFINE_MUTEX(probe_disc_mt); /* ch->linked = true, most_nd_open */
static DEFINE_SPINLOCK(list_lock); /* list_head, ch->linked = false, dev_hold */
static struct most_component comp;

static int skb_to_mamac(const struct sk_buff *skb, struct mbo *mbo)
{
	u8 *buff = mbo->virt_address;
	static const u8 broadcast[] = { 0x03, 0xFF };
	const u8 *dest_addr = skb->data + 4;
	const u8 *eth_type = skb->data + 12;
	unsigned int payload_len = skb->len - ETH_HLEN;
	unsigned int mdp_len = payload_len + MDP_HDR_LEN;

	if (mdp_len < skb->len) {
		pr_err("drop: too large packet! (%u)\n", skb->len);
		return -EINVAL;
	}

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

	if (mep_len < skb->len) {
		pr_err("drop: too large packet! (%u)\n", skb->len);
		return -EINVAL;
	}

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
	struct net_dev_context *nd = netdev_priv(dev);
	int err = eth_mac_addr(dev, p);

	if (err)
		return err;

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

static void on_netinfo(struct most_interface *iface,
		       unsigned char link_stat, unsigned char *mac_addr);

static int most_nd_open(struct net_device *dev)
{
	struct net_dev_context *nd = netdev_priv(dev);
	int ret = 0;

	mutex_lock(&probe_disc_mt);

	if (most_start_channel(nd->iface, nd->rx.ch_id, &comp)) {
		netdev_err(dev, "most_start_channel() failed\n");
		ret = -EBUSY;
		goto unlock;
	}

	if (most_start_channel(nd->iface, nd->tx.ch_id, &comp)) {
		netdev_err(dev, "most_start_channel() failed\n");
		most_stop_channel(nd->iface, nd->rx.ch_id, &comp);
		ret = -EBUSY;
		goto unlock;
	}

	netif_carrier_off(dev);
	if (is_valid_ether_addr(dev->dev_addr))
		netif_dormant_off(dev);
	else
		netif_dormant_on(dev);
	netif_wake_queue(dev);
	if (nd->iface->request_netinfo)
		nd->iface->request_netinfo(nd->iface, nd->tx.ch_id, on_netinfo);

unlock:
	mutex_unlock(&probe_disc_mt);
	return ret;
}

static int most_nd_stop(struct net_device *dev)
{
	struct net_dev_context *nd = netdev_priv(dev);

	netif_stop_queue(dev);
	if (nd->iface->request_netinfo)
		nd->iface->request_netinfo(nd->iface, nd->tx.ch_id, NULL);
	most_stop_channel(nd->iface, nd->rx.ch_id, &comp);
	most_stop_channel(nd->iface, nd->tx.ch_id, &comp);

	return 0;
}

static netdev_tx_t most_nd_start_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct net_dev_context *nd = netdev_priv(dev);
	struct mbo *mbo;
	int ret;

	mbo = most_get_mbo(nd->iface, nd->tx.ch_id, &comp);

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
	ether_setup(dev);
	dev->netdev_ops = &most_nd_ops;
}

static struct net_dev_context *get_net_dev(struct most_interface *iface)
{
	struct net_dev_context *nd;

	list_for_each_entry(nd, &net_devices, list)
		if (nd->iface == iface)
			return nd;
	return NULL;
}

static struct net_dev_context *get_net_dev_hold(struct most_interface *iface)
{
	struct net_dev_context *nd;
	unsigned long flags;

	spin_lock_irqsave(&list_lock, flags);
	nd = get_net_dev(iface);
	if (nd && nd->rx.linked && nd->tx.linked)
		dev_hold(nd->dev);
	else
		nd = NULL;
	spin_unlock_irqrestore(&list_lock, flags);
	return nd;
}

static int comp_probe_channel(struct most_interface *iface, int channel_idx,
			      struct most_channel_config *ccfg, char *name,
			      char *args)
{
	struct net_dev_context *nd;
	struct net_dev_channel *ch;
	struct net_device *dev;
	unsigned long flags;
	int ret = 0;

	if (!iface)
		return -EINVAL;

	if (ccfg->data_type != MOST_CH_ASYNC)
		return -EINVAL;

	mutex_lock(&probe_disc_mt);
	nd = get_net_dev(iface);
	if (!nd) {
		dev = alloc_netdev(sizeof(struct net_dev_context), "meth%d",
				   NET_NAME_UNKNOWN, most_nd_setup);
		if (!dev) {
			ret = -ENOMEM;
			goto unlock;
		}

		nd = netdev_priv(dev);
		nd->iface = iface;
		nd->dev = dev;

		spin_lock_irqsave(&list_lock, flags);
		list_add(&nd->list, &net_devices);
		spin_unlock_irqrestore(&list_lock, flags);

		ch = ccfg->direction == MOST_CH_TX ? &nd->tx : &nd->rx;
	} else {
		ch = ccfg->direction == MOST_CH_TX ? &nd->tx : &nd->rx;
		if (ch->linked) {
			pr_err("direction is allocated\n");
			ret = -EINVAL;
			goto unlock;
		}

		if (register_netdev(nd->dev)) {
			pr_err("register_netdev() failed\n");
			ret = -EINVAL;
			goto unlock;
		}
	}
	ch->ch_id = channel_idx;
	ch->linked = true;

unlock:
	mutex_unlock(&probe_disc_mt);
	return ret;
}

static int comp_disconnect_channel(struct most_interface *iface,
				   int channel_idx)
{
	struct net_dev_context *nd;
	struct net_dev_channel *ch;
	unsigned long flags;
	int ret = 0;

	mutex_lock(&probe_disc_mt);
	nd = get_net_dev(iface);
	if (!nd) {
		ret = -EINVAL;
		goto unlock;
	}

	if (nd->rx.linked && channel_idx == nd->rx.ch_id) {
		ch = &nd->rx;
	} else if (nd->tx.linked && channel_idx == nd->tx.ch_id) {
		ch = &nd->tx;
	} else {
		ret = -EINVAL;
		goto unlock;
	}

	if (nd->rx.linked && nd->tx.linked) {
		spin_lock_irqsave(&list_lock, flags);
		ch->linked = false;
		spin_unlock_irqrestore(&list_lock, flags);

		/*
		 * do not call most_stop_channel() here, because channels are
		 * going to be closed in ndo_stop() after unregister_netdev()
		 */
		unregister_netdev(nd->dev);
	} else {
		spin_lock_irqsave(&list_lock, flags);
		list_del(&nd->list);
		spin_unlock_irqrestore(&list_lock, flags);

		free_netdev(nd->dev);
	}

unlock:
	mutex_unlock(&probe_disc_mt);
	return ret;
}

static int comp_resume_tx_channel(struct most_interface *iface,
				  int channel_idx)
{
	struct net_dev_context *nd;

	nd = get_net_dev_hold(iface);
	if (!nd)
		return 0;

	if (nd->tx.ch_id != channel_idx)
		goto put_nd;

	netif_wake_queue(nd->dev);

put_nd:
	dev_put(nd->dev);
	return 0;
}

static int comp_rx_data(struct mbo *mbo)
{
	const u32 zero = 0;
	struct net_dev_context *nd;
	char *buf = mbo->virt_address;
	u32 len = mbo->processed_length;
	struct sk_buff *skb;
	struct net_device *dev;
	unsigned int skb_len;
	int ret = 0;

	nd = get_net_dev_hold(mbo->ifp);
	if (!nd)
		return -EIO;

	if (nd->rx.ch_id != mbo->hdm_channel_id) {
		ret = -EIO;
		goto put_nd;
	}

	dev = nd->dev;

	if (nd->is_mamac) {
		if (!pms_is_mamac(buf, len)) {
			ret = -EIO;
			goto put_nd;
		}

		skb = dev_alloc_skb(len - MDP_HDR_LEN + 2 * ETH_ALEN + 2);
	} else {
		if (!PMS_IS_MEP(buf, len)) {
			ret = -EIO;
			goto put_nd;
		}

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
		skb_put_data(skb, &zero, 4);
		skb_put_data(skb, buf + 5, 2);

		/* eth type */
		skb_put_data(skb, buf + 10, 2);

		buf += MDP_HDR_LEN;
		len -= MDP_HDR_LEN;
	} else {
		buf += MEP_HDR_LEN;
		len -= MEP_HDR_LEN;
	}

	skb_put_data(skb, buf, len);
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

put_nd:
	dev_put(nd->dev);
	return ret;
}

static struct most_component comp = {
	.mod = THIS_MODULE,
	.name = "net",
	.probe_channel = comp_probe_channel,
	.disconnect_channel = comp_disconnect_channel,
	.tx_completion = comp_resume_tx_channel,
	.rx_completion = comp_rx_data,
};

static int __init most_net_init(void)
{
	int err;

	err = most_register_component(&comp);
	if (err)
		return err;
	err = most_register_configfs_subsys(&comp);
	if (err) {
		most_deregister_component(&comp);
		return err;
	}
	return 0;
}

static void __exit most_net_exit(void)
{
	most_deregister_configfs_subsys(&comp);
	most_deregister_component(&comp);
}

/**
 * on_netinfo - callback for HDM to be informed about HW's MAC
 * @param iface - most interface instance
 * @param link_stat - link status
 * @param mac_addr - MAC address
 */
static void on_netinfo(struct most_interface *iface,
		       unsigned char link_stat, unsigned char *mac_addr)
{
	struct net_dev_context *nd;
	struct net_device *dev;
	const u8 *m = mac_addr;

	nd = get_net_dev_hold(iface);
	if (!nd)
		return;

	dev = nd->dev;

	if (link_stat)
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);

	if (m && is_valid_ether_addr(m)) {
		if (!is_valid_ether_addr(dev->dev_addr)) {
			netdev_info(dev, "set mac %pM\n", m);
			ether_addr_copy(dev->dev_addr, m);
			netif_dormant_off(dev);
		} else if (!ether_addr_equal(dev->dev_addr, m)) {
			netdev_warn(dev, "reject mac %pM\n", m);
		}
	}

	dev_put(nd->dev);
}

module_init(most_net_init);
module_exit(most_net_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrey Shvetsov <andrey.shvetsov@k2l.de>");
MODULE_DESCRIPTION("Networking Component Module for Mostcore");
