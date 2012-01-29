/*
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/atomic.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <net/arp.h>
#include <net/route.h>
#include <net/sock.h>
#include <net/pkt_sched.h>

#include "hyperv_net.h"

struct net_device_context {
	/* point back to our device context */
	struct hv_device *device_ctx;
	struct delayed_work dwork;
};


static int ring_size = 128;
module_param(ring_size, int, S_IRUGO);
MODULE_PARM_DESC(ring_size, "Ring buffer size (# of pages)");

struct set_multicast_work {
	struct work_struct work;
	struct net_device *net;
};

static void do_set_multicast(struct work_struct *w)
{
	struct set_multicast_work *swk =
		container_of(w, struct set_multicast_work, work);
	struct net_device *net = swk->net;

	struct net_device_context *ndevctx = netdev_priv(net);
	struct netvsc_device *nvdev;
	struct rndis_device *rdev;

	nvdev = hv_get_drvdata(ndevctx->device_ctx);
	if (nvdev == NULL)
		goto out;

	rdev = nvdev->extension;
	if (rdev == NULL)
		goto out;

	if (net->flags & IFF_PROMISC)
		rndis_filter_set_packet_filter(rdev,
			NDIS_PACKET_TYPE_PROMISCUOUS);
	else
		rndis_filter_set_packet_filter(rdev,
			NDIS_PACKET_TYPE_BROADCAST |
			NDIS_PACKET_TYPE_ALL_MULTICAST |
			NDIS_PACKET_TYPE_DIRECTED);

out:
	kfree(w);
}

static void netvsc_set_multicast_list(struct net_device *net)
{
	struct set_multicast_work *swk =
		kmalloc(sizeof(struct set_multicast_work), GFP_ATOMIC);
	if (swk == NULL)
		return;

	swk->net = net;
	INIT_WORK(&swk->work, do_set_multicast);
	schedule_work(&swk->work);
}

static int netvsc_open(struct net_device *net)
{
	struct net_device_context *net_device_ctx = netdev_priv(net);
	struct hv_device *device_obj = net_device_ctx->device_ctx;
	int ret = 0;

	/* Open up the device */
	ret = rndis_filter_open(device_obj);
	if (ret != 0) {
		netdev_err(net, "unable to open device (ret %d).\n", ret);
		return ret;
	}

	netif_start_queue(net);

	return ret;
}

static int netvsc_close(struct net_device *net)
{
	struct net_device_context *net_device_ctx = netdev_priv(net);
	struct hv_device *device_obj = net_device_ctx->device_ctx;
	int ret;

	netif_stop_queue(net);

	ret = rndis_filter_close(device_obj);
	if (ret != 0)
		netdev_err(net, "unable to close device (ret %d).\n", ret);

	return ret;
}

static void netvsc_xmit_completion(void *context)
{
	struct hv_netvsc_packet *packet = (struct hv_netvsc_packet *)context;
	struct sk_buff *skb = (struct sk_buff *)
		(unsigned long)packet->completion.send.send_completion_tid;

	kfree(packet);

	if (skb)
		dev_kfree_skb_any(skb);
}

static int netvsc_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	struct net_device_context *net_device_ctx = netdev_priv(net);
	struct hv_netvsc_packet *packet;
	int ret;
	unsigned int i, num_pages, npg_data;

	/* Add multipage for skb->data and additional one for RNDIS */
	npg_data = (((unsigned long)skb->data + skb_headlen(skb) - 1)
		>> PAGE_SHIFT) - ((unsigned long)skb->data >> PAGE_SHIFT) + 1;
	num_pages = skb_shinfo(skb)->nr_frags + npg_data + 1;

	/* Allocate a netvsc packet based on # of frags. */
	packet = kzalloc(sizeof(struct hv_netvsc_packet) +
			 (num_pages * sizeof(struct hv_page_buffer)) +
			 sizeof(struct rndis_filter_packet), GFP_ATOMIC);
	if (!packet) {
		/* out of memory, drop packet */
		netdev_err(net, "unable to allocate hv_netvsc_packet\n");

		dev_kfree_skb(skb);
		net->stats.tx_dropped++;
		return NETDEV_TX_BUSY;
	}

	packet->extension = (void *)(unsigned long)packet +
				sizeof(struct hv_netvsc_packet) +
				    (num_pages * sizeof(struct hv_page_buffer));

	/* Setup the rndis header */
	packet->page_buf_cnt = num_pages;

	/* Initialize it from the skb */
	packet->total_data_buflen = skb->len;

	/* Start filling in the page buffers starting after RNDIS buffer. */
	packet->page_buf[1].pfn = virt_to_phys(skb->data) >> PAGE_SHIFT;
	packet->page_buf[1].offset
		= (unsigned long)skb->data & (PAGE_SIZE - 1);
	if (npg_data == 1)
		packet->page_buf[1].len = skb_headlen(skb);
	else
		packet->page_buf[1].len = PAGE_SIZE
			- packet->page_buf[1].offset;

	for (i = 2; i <= npg_data; i++) {
		packet->page_buf[i].pfn = virt_to_phys(skb->data
			+ PAGE_SIZE * (i-1)) >> PAGE_SHIFT;
		packet->page_buf[i].offset = 0;
		packet->page_buf[i].len = PAGE_SIZE;
	}
	if (npg_data > 1)
		packet->page_buf[npg_data].len = (((unsigned long)skb->data
			+ skb_headlen(skb) - 1) & (PAGE_SIZE - 1)) + 1;

	/* Additional fragments are after SKB data */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		const skb_frag_t *f = &skb_shinfo(skb)->frags[i];

		packet->page_buf[i+npg_data+1].pfn =
			page_to_pfn(skb_frag_page(f));
		packet->page_buf[i+npg_data+1].offset = f->page_offset;
		packet->page_buf[i+npg_data+1].len = skb_frag_size(f);
	}

	/* Set the completion routine */
	packet->completion.send.send_completion = netvsc_xmit_completion;
	packet->completion.send.send_completion_ctx = packet;
	packet->completion.send.send_completion_tid = (unsigned long)skb;

	ret = rndis_filter_send(net_device_ctx->device_ctx,
				  packet);
	if (ret == 0) {
		net->stats.tx_bytes += skb->len;
		net->stats.tx_packets++;
	} else {
		/* we are shutting down or bus overloaded, just drop packet */
		net->stats.tx_dropped++;
		kfree(packet);
		dev_kfree_skb_any(skb);
	}

	return ret ? NETDEV_TX_BUSY : NETDEV_TX_OK;
}

/*
 * netvsc_linkstatus_callback - Link up/down notification
 */
void netvsc_linkstatus_callback(struct hv_device *device_obj,
				       unsigned int status)
{
	struct net_device *net;
	struct net_device_context *ndev_ctx;
	struct netvsc_device *net_device;

	net_device = hv_get_drvdata(device_obj);
	net = net_device->ndev;

	if (!net) {
		netdev_err(net, "got link status but net device "
				"not initialized yet\n");
		return;
	}

	if (status == 1) {
		netif_carrier_on(net);
		netif_wake_queue(net);
		ndev_ctx = netdev_priv(net);
		schedule_delayed_work(&ndev_ctx->dwork, 0);
		schedule_delayed_work(&ndev_ctx->dwork, msecs_to_jiffies(20));
	} else {
		netif_carrier_off(net);
		netif_stop_queue(net);
	}
}

/*
 * netvsc_recv_callback -  Callback when we receive a packet from the
 * "wire" on the specified device.
 */
int netvsc_recv_callback(struct hv_device *device_obj,
				struct hv_netvsc_packet *packet)
{
	struct net_device *net = dev_get_drvdata(&device_obj->device);
	struct sk_buff *skb;
	struct netvsc_device *net_device;

	net_device = hv_get_drvdata(device_obj);
	net = net_device->ndev;

	if (!net) {
		netdev_err(net, "got receive callback but net device"
			" not initialized yet\n");
		return 0;
	}

	/* Allocate a skb - TODO direct I/O to pages? */
	skb = netdev_alloc_skb_ip_align(net, packet->total_data_buflen);
	if (unlikely(!skb)) {
		++net->stats.rx_dropped;
		return 0;
	}

	/*
	 * Copy to skb. This copy is needed here since the memory pointed by
	 * hv_netvsc_packet cannot be deallocated
	 */
	memcpy(skb_put(skb, packet->total_data_buflen), packet->data,
		packet->total_data_buflen);

	skb->protocol = eth_type_trans(skb, net);
	skb->ip_summed = CHECKSUM_NONE;

	net->stats.rx_packets++;
	net->stats.rx_bytes += packet->total_data_buflen;

	/*
	 * Pass the skb back up. Network stack will deallocate the skb when it
	 * is done.
	 * TODO - use NAPI?
	 */
	netif_rx(skb);

	return 0;
}

static void netvsc_get_drvinfo(struct net_device *net,
			       struct ethtool_drvinfo *info)
{
	strcpy(info->driver, "hv_netvsc");
	strcpy(info->version, HV_DRV_VERSION);
	strcpy(info->fw_version, "N/A");
}

static int netvsc_change_mtu(struct net_device *ndev, int mtu)
{
	struct net_device_context *ndevctx = netdev_priv(ndev);
	struct hv_device *hdev =  ndevctx->device_ctx;
	struct netvsc_device *nvdev = hv_get_drvdata(hdev);
	struct netvsc_device_info device_info;
	int limit = ETH_DATA_LEN;

	if (nvdev == NULL || nvdev->destroy)
		return -ENODEV;

	if (nvdev->nvsp_version == NVSP_PROTOCOL_VERSION_2)
		limit = NETVSC_MTU;

	if (mtu < 68 || mtu > limit)
		return -EINVAL;

	nvdev->start_remove = true;
	cancel_delayed_work_sync(&ndevctx->dwork);
	netif_stop_queue(ndev);
	rndis_filter_device_remove(hdev);

	ndev->mtu = mtu;

	ndevctx->device_ctx = hdev;
	hv_set_drvdata(hdev, ndev);
	device_info.ring_size = ring_size;
	rndis_filter_device_add(hdev, &device_info);
	netif_wake_queue(ndev);

	return 0;
}

static const struct ethtool_ops ethtool_ops = {
	.get_drvinfo	= netvsc_get_drvinfo,
	.get_link	= ethtool_op_get_link,
};

static const struct net_device_ops device_ops = {
	.ndo_open =			netvsc_open,
	.ndo_stop =			netvsc_close,
	.ndo_start_xmit =		netvsc_start_xmit,
	.ndo_set_rx_mode =		netvsc_set_multicast_list,
	.ndo_change_mtu =		netvsc_change_mtu,
	.ndo_validate_addr =		eth_validate_addr,
	.ndo_set_mac_address =		eth_mac_addr,
};

/*
 * Send GARP packet to network peers after migrations.
 * After Quick Migration, the network is not immediately operational in the
 * current context when receiving RNDIS_STATUS_MEDIA_CONNECT event. So, add
 * another netif_notify_peers() into a delayed work, otherwise GARP packet
 * will not be sent after quick migration, and cause network disconnection.
 */
static void netvsc_send_garp(struct work_struct *w)
{
	struct net_device_context *ndev_ctx;
	struct net_device *net;
	struct netvsc_device *net_device;

	ndev_ctx = container_of(w, struct net_device_context, dwork.work);
	net_device = hv_get_drvdata(ndev_ctx->device_ctx);
	net = net_device->ndev;
	netif_notify_peers(net);
}


static int netvsc_probe(struct hv_device *dev,
			const struct hv_vmbus_device_id *dev_id)
{
	struct net_device *net = NULL;
	struct net_device_context *net_device_ctx;
	struct netvsc_device_info device_info;
	int ret;

	net = alloc_etherdev(sizeof(struct net_device_context));
	if (!net)
		return -ENOMEM;

	/* Set initial state */
	netif_carrier_off(net);

	net_device_ctx = netdev_priv(net);
	net_device_ctx->device_ctx = dev;
	hv_set_drvdata(dev, net);
	INIT_DELAYED_WORK(&net_device_ctx->dwork, netvsc_send_garp);

	net->netdev_ops = &device_ops;

	/* TODO: Add GSO and Checksum offload */
	net->hw_features = NETIF_F_SG;
	net->features = NETIF_F_SG;

	SET_ETHTOOL_OPS(net, &ethtool_ops);
	SET_NETDEV_DEV(net, &dev->device);

	ret = register_netdev(net);
	if (ret != 0) {
		pr_err("Unable to register netdev.\n");
		free_netdev(net);
		goto out;
	}

	/* Notify the netvsc driver of the new device */
	device_info.ring_size = ring_size;
	ret = rndis_filter_device_add(dev, &device_info);
	if (ret != 0) {
		netdev_err(net, "unable to add netvsc device (ret %d)\n", ret);
		unregister_netdev(net);
		free_netdev(net);
		hv_set_drvdata(dev, NULL);
		return ret;
	}
	memcpy(net->dev_addr, device_info.mac_adr, ETH_ALEN);

	netif_carrier_on(net);

out:
	return ret;
}

static int netvsc_remove(struct hv_device *dev)
{
	struct net_device *net;
	struct net_device_context *ndev_ctx;
	struct netvsc_device *net_device;

	net_device = hv_get_drvdata(dev);
	net = net_device->ndev;

	if (net == NULL) {
		dev_err(&dev->device, "No net device to remove\n");
		return 0;
	}

	net_device->start_remove = true;

	ndev_ctx = netdev_priv(net);
	cancel_delayed_work_sync(&ndev_ctx->dwork);

	/* Stop outbound asap */
	netif_stop_queue(net);

	unregister_netdev(net);

	/*
	 * Call to the vsc driver to let it know that the device is being
	 * removed
	 */
	rndis_filter_device_remove(dev);

	free_netdev(net);
	return 0;
}

static const struct hv_vmbus_device_id id_table[] = {
	/* Network guid */
	{ VMBUS_DEVICE(0x63, 0x51, 0x61, 0xF8, 0x3E, 0xDF, 0xc5, 0x46,
		       0x91, 0x3F, 0xF2, 0xD2, 0xF9, 0x65, 0xED, 0x0E) },
	{ },
};

MODULE_DEVICE_TABLE(vmbus, id_table);

/* The one and only one */
static struct  hv_driver netvsc_drv = {
	.name = "netvsc",
	.id_table = id_table,
	.probe = netvsc_probe,
	.remove = netvsc_remove,
};

static void __exit netvsc_drv_exit(void)
{
	vmbus_driver_unregister(&netvsc_drv);
}

static int __init netvsc_drv_init(void)
{
	return vmbus_driver_register(&netvsc_drv);
}

MODULE_LICENSE("GPL");
MODULE_VERSION(HV_DRV_VERSION);
MODULE_DESCRIPTION("Microsoft Hyper-V network driver");

module_init(netvsc_drv_init);
module_exit(netvsc_drv_exit);
