/*
 * rionet - Ethernet driver over RapidIO messaging services
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/slab.h>
#include <linux/rio_ids.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/crc32.h>
#include <linux/ethtool.h>

#define DRV_NAME        "rionet"
#define DRV_VERSION     "0.3"
#define DRV_AUTHOR      "Matt Porter <mporter@kernel.crashing.org>"
#define DRV_DESC        "Ethernet over RapidIO"

MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION(DRV_DESC);
MODULE_LICENSE("GPL");

#define RIONET_DEFAULT_MSGLEVEL \
			(NETIF_MSG_DRV          | \
			 NETIF_MSG_LINK         | \
			 NETIF_MSG_RX_ERR       | \
			 NETIF_MSG_TX_ERR)

#define RIONET_DOORBELL_JOIN	0x1000
#define RIONET_DOORBELL_LEAVE	0x1001

#define RIONET_MAILBOX		0

#define RIONET_TX_RING_SIZE	CONFIG_RIONET_TX_SIZE
#define RIONET_RX_RING_SIZE	CONFIG_RIONET_RX_SIZE
#define RIONET_MAX_NETS		8

struct rionet_private {
	struct rio_mport *mport;
	struct sk_buff *rx_skb[RIONET_RX_RING_SIZE];
	struct sk_buff *tx_skb[RIONET_TX_RING_SIZE];
	int rx_slot;
	int tx_slot;
	int tx_cnt;
	int ack_slot;
	spinlock_t lock;
	spinlock_t tx_lock;
	u32 msg_enable;
};

struct rionet_peer {
	struct list_head node;
	struct rio_dev *rdev;
	struct resource *res;
};

struct rionet_net {
	struct net_device *ndev;
	struct list_head peers;
	struct rio_dev **active;
	int nact;	/* number of active peers */
};

static struct rionet_net nets[RIONET_MAX_NETS];

#define is_rionet_capable(src_ops, dst_ops)			\
			((src_ops & RIO_SRC_OPS_DATA_MSG) &&	\
			 (dst_ops & RIO_DST_OPS_DATA_MSG) &&	\
			 (src_ops & RIO_SRC_OPS_DOORBELL) &&	\
			 (dst_ops & RIO_DST_OPS_DOORBELL))
#define dev_rionet_capable(dev) \
	is_rionet_capable(dev->src_ops, dev->dst_ops)

#define RIONET_MAC_MATCH(x)	(!memcmp((x), "\00\01\00\01", 4))
#define RIONET_GET_DESTID(x)	((*((u8 *)x + 4) << 8) | *((u8 *)x + 5))

static int rionet_rx_clean(struct net_device *ndev)
{
	int i;
	int error = 0;
	struct rionet_private *rnet = netdev_priv(ndev);
	void *data;

	i = rnet->rx_slot;

	do {
		if (!rnet->rx_skb[i])
			continue;

		if (!(data = rio_get_inb_message(rnet->mport, RIONET_MAILBOX)))
			break;

		rnet->rx_skb[i]->data = data;
		skb_put(rnet->rx_skb[i], RIO_MAX_MSG_SIZE);
		rnet->rx_skb[i]->protocol =
		    eth_type_trans(rnet->rx_skb[i], ndev);
		error = netif_rx(rnet->rx_skb[i]);

		if (error == NET_RX_DROP) {
			ndev->stats.rx_dropped++;
		} else {
			ndev->stats.rx_packets++;
			ndev->stats.rx_bytes += RIO_MAX_MSG_SIZE;
		}

	} while ((i = (i + 1) % RIONET_RX_RING_SIZE) != rnet->rx_slot);

	return i;
}

static void rionet_rx_fill(struct net_device *ndev, int end)
{
	int i;
	struct rionet_private *rnet = netdev_priv(ndev);

	i = rnet->rx_slot;
	do {
		rnet->rx_skb[i] = dev_alloc_skb(RIO_MAX_MSG_SIZE);

		if (!rnet->rx_skb[i])
			break;

		rio_add_inb_buffer(rnet->mport, RIONET_MAILBOX,
				   rnet->rx_skb[i]->data);
	} while ((i = (i + 1) % RIONET_RX_RING_SIZE) != end);

	rnet->rx_slot = i;
}

static int rionet_queue_tx_msg(struct sk_buff *skb, struct net_device *ndev,
			       struct rio_dev *rdev)
{
	struct rionet_private *rnet = netdev_priv(ndev);

	rio_add_outb_message(rnet->mport, rdev, 0, skb->data, skb->len);
	rnet->tx_skb[rnet->tx_slot] = skb;

	ndev->stats.tx_packets++;
	ndev->stats.tx_bytes += skb->len;

	if (++rnet->tx_cnt == RIONET_TX_RING_SIZE)
		netif_stop_queue(ndev);

	++rnet->tx_slot;
	rnet->tx_slot &= (RIONET_TX_RING_SIZE - 1);

	if (netif_msg_tx_queued(rnet))
		printk(KERN_INFO "%s: queued skb len %8.8x\n", DRV_NAME,
		       skb->len);

	return 0;
}

static int rionet_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	int i;
	struct rionet_private *rnet = netdev_priv(ndev);
	struct ethhdr *eth = (struct ethhdr *)skb->data;
	u16 destid;
	unsigned long flags;
	int add_num = 1;

	local_irq_save(flags);
	if (!spin_trylock(&rnet->tx_lock)) {
		local_irq_restore(flags);
		return NETDEV_TX_LOCKED;
	}

	if (is_multicast_ether_addr(eth->h_dest))
		add_num = nets[rnet->mport->id].nact;

	if ((rnet->tx_cnt + add_num) > RIONET_TX_RING_SIZE) {
		netif_stop_queue(ndev);
		spin_unlock_irqrestore(&rnet->tx_lock, flags);
		printk(KERN_ERR "%s: BUG! Tx Ring full when queue awake!\n",
		       ndev->name);
		return NETDEV_TX_BUSY;
	}

	if (is_multicast_ether_addr(eth->h_dest)) {
		int count = 0;

		for (i = 0; i < RIO_MAX_ROUTE_ENTRIES(rnet->mport->sys_size);
				i++)
			if (nets[rnet->mport->id].active[i]) {
				rionet_queue_tx_msg(skb, ndev,
					nets[rnet->mport->id].active[i]);
				if (count)
					atomic_inc(&skb->users);
				count++;
			}
	} else if (RIONET_MAC_MATCH(eth->h_dest)) {
		destid = RIONET_GET_DESTID(eth->h_dest);
		if (nets[rnet->mport->id].active[destid])
			rionet_queue_tx_msg(skb, ndev,
					nets[rnet->mport->id].active[destid]);
		else {
			/*
			 * If the target device was removed from the list of
			 * active peers but we still have TX packets targeting
			 * it just report sending a packet to the target
			 * (without actual packet transfer).
			 */
			dev_kfree_skb_any(skb);
			ndev->stats.tx_packets++;
			ndev->stats.tx_bytes += skb->len;
		}
	}

	spin_unlock_irqrestore(&rnet->tx_lock, flags);

	return NETDEV_TX_OK;
}

static void rionet_dbell_event(struct rio_mport *mport, void *dev_id, u16 sid, u16 tid,
			       u16 info)
{
	struct net_device *ndev = dev_id;
	struct rionet_private *rnet = netdev_priv(ndev);
	struct rionet_peer *peer;

	if (netif_msg_intr(rnet))
		printk(KERN_INFO "%s: doorbell sid %4.4x tid %4.4x info %4.4x",
		       DRV_NAME, sid, tid, info);
	if (info == RIONET_DOORBELL_JOIN) {
		if (!nets[rnet->mport->id].active[sid]) {
			list_for_each_entry(peer,
					   &nets[rnet->mport->id].peers, node) {
				if (peer->rdev->destid == sid) {
					nets[rnet->mport->id].active[sid] =
								peer->rdev;
					nets[rnet->mport->id].nact++;
				}
			}
			rio_mport_send_doorbell(mport, sid,
						RIONET_DOORBELL_JOIN);
		}
	} else if (info == RIONET_DOORBELL_LEAVE) {
		nets[rnet->mport->id].active[sid] = NULL;
		nets[rnet->mport->id].nact--;
	} else {
		if (netif_msg_intr(rnet))
			printk(KERN_WARNING "%s: unhandled doorbell\n",
			       DRV_NAME);
	}
}

static void rionet_inb_msg_event(struct rio_mport *mport, void *dev_id, int mbox, int slot)
{
	int n;
	struct net_device *ndev = dev_id;
	struct rionet_private *rnet = netdev_priv(ndev);

	if (netif_msg_intr(rnet))
		printk(KERN_INFO "%s: inbound message event, mbox %d slot %d\n",
		       DRV_NAME, mbox, slot);

	spin_lock(&rnet->lock);
	if ((n = rionet_rx_clean(ndev)) != rnet->rx_slot)
		rionet_rx_fill(ndev, n);
	spin_unlock(&rnet->lock);
}

static void rionet_outb_msg_event(struct rio_mport *mport, void *dev_id, int mbox, int slot)
{
	struct net_device *ndev = dev_id;
	struct rionet_private *rnet = netdev_priv(ndev);

	spin_lock(&rnet->lock);

	if (netif_msg_intr(rnet))
		printk(KERN_INFO
		       "%s: outbound message event, mbox %d slot %d\n",
		       DRV_NAME, mbox, slot);

	while (rnet->tx_cnt && (rnet->ack_slot != slot)) {
		/* dma unmap single */
		dev_kfree_skb_irq(rnet->tx_skb[rnet->ack_slot]);
		rnet->tx_skb[rnet->ack_slot] = NULL;
		++rnet->ack_slot;
		rnet->ack_slot &= (RIONET_TX_RING_SIZE - 1);
		rnet->tx_cnt--;
	}

	if (rnet->tx_cnt < RIONET_TX_RING_SIZE)
		netif_wake_queue(ndev);

	spin_unlock(&rnet->lock);
}

static int rionet_open(struct net_device *ndev)
{
	int i, rc = 0;
	struct rionet_peer *peer, *tmp;
	struct rionet_private *rnet = netdev_priv(ndev);

	if (netif_msg_ifup(rnet))
		printk(KERN_INFO "%s: open\n", DRV_NAME);

	if ((rc = rio_request_inb_dbell(rnet->mport,
					(void *)ndev,
					RIONET_DOORBELL_JOIN,
					RIONET_DOORBELL_LEAVE,
					rionet_dbell_event)) < 0)
		goto out;

	if ((rc = rio_request_inb_mbox(rnet->mport,
				       (void *)ndev,
				       RIONET_MAILBOX,
				       RIONET_RX_RING_SIZE,
				       rionet_inb_msg_event)) < 0)
		goto out;

	if ((rc = rio_request_outb_mbox(rnet->mport,
					(void *)ndev,
					RIONET_MAILBOX,
					RIONET_TX_RING_SIZE,
					rionet_outb_msg_event)) < 0)
		goto out;

	/* Initialize inbound message ring */
	for (i = 0; i < RIONET_RX_RING_SIZE; i++)
		rnet->rx_skb[i] = NULL;
	rnet->rx_slot = 0;
	rionet_rx_fill(ndev, 0);

	rnet->tx_slot = 0;
	rnet->tx_cnt = 0;
	rnet->ack_slot = 0;

	netif_carrier_on(ndev);
	netif_start_queue(ndev);

	list_for_each_entry_safe(peer, tmp,
				 &nets[rnet->mport->id].peers, node) {
		if (!(peer->res = rio_request_outb_dbell(peer->rdev,
							 RIONET_DOORBELL_JOIN,
							 RIONET_DOORBELL_LEAVE)))
		{
			printk(KERN_ERR "%s: error requesting doorbells\n",
			       DRV_NAME);
			continue;
		}

		/* Send a join message */
		rio_send_doorbell(peer->rdev, RIONET_DOORBELL_JOIN);
	}

      out:
	return rc;
}

static int rionet_close(struct net_device *ndev)
{
	struct rionet_private *rnet = netdev_priv(ndev);
	struct rionet_peer *peer, *tmp;
	int i;

	if (netif_msg_ifup(rnet))
		printk(KERN_INFO "%s: close %s\n", DRV_NAME, ndev->name);

	netif_stop_queue(ndev);
	netif_carrier_off(ndev);

	for (i = 0; i < RIONET_RX_RING_SIZE; i++)
		kfree_skb(rnet->rx_skb[i]);

	list_for_each_entry_safe(peer, tmp,
				 &nets[rnet->mport->id].peers, node) {
		if (nets[rnet->mport->id].active[peer->rdev->destid]) {
			rio_send_doorbell(peer->rdev, RIONET_DOORBELL_LEAVE);
			nets[rnet->mport->id].active[peer->rdev->destid] = NULL;
		}
		rio_release_outb_dbell(peer->rdev, peer->res);
	}

	rio_release_inb_dbell(rnet->mport, RIONET_DOORBELL_JOIN,
			      RIONET_DOORBELL_LEAVE);
	rio_release_inb_mbox(rnet->mport, RIONET_MAILBOX);
	rio_release_outb_mbox(rnet->mport, RIONET_MAILBOX);

	return 0;
}

static int rionet_remove_dev(struct device *dev, struct subsys_interface *sif)
{
	struct rio_dev *rdev = to_rio_dev(dev);
	unsigned char netid = rdev->net->hport->id;
	struct rionet_peer *peer, *tmp;

	if (dev_rionet_capable(rdev)) {
		list_for_each_entry_safe(peer, tmp, &nets[netid].peers, node) {
			if (peer->rdev == rdev) {
				if (nets[netid].active[rdev->destid]) {
					nets[netid].active[rdev->destid] = NULL;
					nets[netid].nact--;
				}

				list_del(&peer->node);
				kfree(peer);
				break;
			}
		}
	}

	return 0;
}

static void rionet_get_drvinfo(struct net_device *ndev,
			       struct ethtool_drvinfo *info)
{
	struct rionet_private *rnet = netdev_priv(ndev);

	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->fw_version, "n/a", sizeof(info->fw_version));
	strlcpy(info->bus_info, rnet->mport->name, sizeof(info->bus_info));
}

static u32 rionet_get_msglevel(struct net_device *ndev)
{
	struct rionet_private *rnet = netdev_priv(ndev);

	return rnet->msg_enable;
}

static void rionet_set_msglevel(struct net_device *ndev, u32 value)
{
	struct rionet_private *rnet = netdev_priv(ndev);

	rnet->msg_enable = value;
}

static const struct ethtool_ops rionet_ethtool_ops = {
	.get_drvinfo = rionet_get_drvinfo,
	.get_msglevel = rionet_get_msglevel,
	.set_msglevel = rionet_set_msglevel,
	.get_link = ethtool_op_get_link,
};

static const struct net_device_ops rionet_netdev_ops = {
	.ndo_open		= rionet_open,
	.ndo_stop		= rionet_close,
	.ndo_start_xmit		= rionet_start_xmit,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
};

static int rionet_setup_netdev(struct rio_mport *mport, struct net_device *ndev)
{
	int rc = 0;
	struct rionet_private *rnet;
	u16 device_id;
	const size_t rionet_active_bytes = sizeof(void *) *
				RIO_MAX_ROUTE_ENTRIES(mport->sys_size);

	nets[mport->id].active = (struct rio_dev **)__get_free_pages(GFP_KERNEL,
						get_order(rionet_active_bytes));
	if (!nets[mport->id].active) {
		rc = -ENOMEM;
		goto out;
	}
	memset((void *)nets[mport->id].active, 0, rionet_active_bytes);

	/* Set up private area */
	rnet = netdev_priv(ndev);
	rnet->mport = mport;

	/* Set the default MAC address */
	device_id = rio_local_get_device_id(mport);
	ndev->dev_addr[0] = 0x00;
	ndev->dev_addr[1] = 0x01;
	ndev->dev_addr[2] = 0x00;
	ndev->dev_addr[3] = 0x01;
	ndev->dev_addr[4] = device_id >> 8;
	ndev->dev_addr[5] = device_id & 0xff;

	ndev->netdev_ops = &rionet_netdev_ops;
	ndev->mtu = RIO_MAX_MSG_SIZE - 14;
	ndev->features = NETIF_F_LLTX;
	SET_NETDEV_DEV(ndev, &mport->dev);
	ndev->ethtool_ops = &rionet_ethtool_ops;

	spin_lock_init(&rnet->lock);
	spin_lock_init(&rnet->tx_lock);

	rnet->msg_enable = RIONET_DEFAULT_MSGLEVEL;

	rc = register_netdev(ndev);
	if (rc != 0)
		goto out;

	printk(KERN_INFO "%s: %s %s Version %s, MAC %pM, %s\n",
	       ndev->name,
	       DRV_NAME,
	       DRV_DESC,
	       DRV_VERSION,
	       ndev->dev_addr,
	       mport->name);

      out:
	return rc;
}

static unsigned long net_table[RIONET_MAX_NETS/sizeof(unsigned long) + 1];

static int rionet_add_dev(struct device *dev, struct subsys_interface *sif)
{
	int rc = -ENODEV;
	u32 lsrc_ops, ldst_ops;
	struct rionet_peer *peer;
	struct net_device *ndev = NULL;
	struct rio_dev *rdev = to_rio_dev(dev);
	unsigned char netid = rdev->net->hport->id;
	int oldnet;

	if (netid >= RIONET_MAX_NETS)
		return rc;

	oldnet = test_and_set_bit(netid, net_table);

	/*
	 * If first time through this net, make sure local device is rionet
	 * capable and setup netdev (this step will be skipped in later probes
	 * on the same net).
	 */
	if (!oldnet) {
		rio_local_read_config_32(rdev->net->hport, RIO_SRC_OPS_CAR,
					 &lsrc_ops);
		rio_local_read_config_32(rdev->net->hport, RIO_DST_OPS_CAR,
					 &ldst_ops);
		if (!is_rionet_capable(lsrc_ops, ldst_ops)) {
			printk(KERN_ERR
			       "%s: local device %s is not network capable\n",
			       DRV_NAME, rdev->net->hport->name);
			goto out;
		}

		/* Allocate our net_device structure */
		ndev = alloc_etherdev(sizeof(struct rionet_private));
		if (ndev == NULL) {
			rc = -ENOMEM;
			goto out;
		}
		nets[netid].ndev = ndev;
		rc = rionet_setup_netdev(rdev->net->hport, ndev);
		if (rc) {
			printk(KERN_ERR "%s: failed to setup netdev (rc=%d)\n",
			       DRV_NAME, rc);
			goto out;
		}

		INIT_LIST_HEAD(&nets[netid].peers);
		nets[netid].nact = 0;
	} else if (nets[netid].ndev == NULL)
		goto out;

	/*
	 * If the remote device has mailbox/doorbell capabilities,
	 * add it to the peer list.
	 */
	if (dev_rionet_capable(rdev)) {
		if (!(peer = kmalloc(sizeof(struct rionet_peer), GFP_KERNEL))) {
			rc = -ENOMEM;
			goto out;
		}
		peer->rdev = rdev;
		list_add_tail(&peer->node, &nets[netid].peers);
	}

	return 0;
out:
	return rc;
}

#ifdef MODULE
static struct rio_device_id rionet_id_table[] = {
	{RIO_DEVICE(RIO_ANY_ID, RIO_ANY_ID)},
	{ 0, }	/* terminate list */
};

MODULE_DEVICE_TABLE(rapidio, rionet_id_table);
#endif

static struct subsys_interface rionet_interface = {
	.name		= "rionet",
	.subsys		= &rio_bus_type,
	.add_dev	= rionet_add_dev,
	.remove_dev	= rionet_remove_dev,
};

static int __init rionet_init(void)
{
	return subsys_interface_register(&rionet_interface);
}

static void __exit rionet_exit(void)
{
	struct rionet_private *rnet;
	struct net_device *ndev;
	struct rionet_peer *peer, *tmp;
	int i;

	for (i = 0; i < RIONET_MAX_NETS; i++) {
		if (nets[i].ndev != NULL) {
			ndev = nets[i].ndev;
			rnet = netdev_priv(ndev);
			unregister_netdev(ndev);

			list_for_each_entry_safe(peer,
						 tmp, &nets[i].peers, node) {
				list_del(&peer->node);
				kfree(peer);
			}

			free_pages((unsigned long)nets[i].active,
				 get_order(sizeof(void *) *
				 RIO_MAX_ROUTE_ENTRIES(rnet->mport->sys_size)));
			nets[i].active = NULL;

			free_netdev(ndev);
		}
	}

	subsys_interface_unregister(&rionet_interface);
}

late_initcall(rionet_init);
module_exit(rionet_exit);
