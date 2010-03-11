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
#include <linux/init.h>
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
#include <net/arp.h>
#include <net/route.h>
#include <net/sock.h>
#include <net/pkt_sched.h>
#include "osd.h"
#include "logging.h"
#include "vmbus.h"
#include "NetVscApi.h"

MODULE_LICENSE("GPL");

struct net_device_context {
	/* point back to our device context */
	struct device_context *device_ctx;
	struct net_device_stats stats;
};

struct netvsc_driver_context {
	/* !! These must be the first 2 fields !! */
	/* Which is a bug FIXME! */
	struct driver_context drv_ctx;
	struct netvsc_driver drv_obj;
};

static int netvsc_ringbuffer_size = NETVSC_DEVICE_RING_BUFFER_SIZE;

/* The one and only one */
static struct netvsc_driver_context g_netvsc_drv;

static struct net_device_stats *netvsc_get_stats(struct net_device *net)
{
	struct net_device_context *net_device_ctx = netdev_priv(net);

	return &net_device_ctx->stats;
}

static void netvsc_set_multicast_list(struct net_device *net)
{
}

static int netvsc_open(struct net_device *net)
{
	struct net_device_context *net_device_ctx = netdev_priv(net);
	struct driver_context *driver_ctx =
	    driver_to_driver_context(net_device_ctx->device_ctx->device.driver);
	struct netvsc_driver_context *net_drv_ctx =
		(struct netvsc_driver_context *)driver_ctx;
	struct netvsc_driver *net_drv_obj = &net_drv_ctx->drv_obj;
	struct hv_device *device_obj = &net_device_ctx->device_ctx->device_obj;
	int ret = 0;

	DPRINT_ENTER(NETVSC_DRV);

	if (netif_carrier_ok(net)) {
		memset(&net_device_ctx->stats, 0,
		       sizeof(struct net_device_stats));

		/* Open up the device */
		ret = net_drv_obj->OnOpen(device_obj);
		if (ret != 0) {
			DPRINT_ERR(NETVSC_DRV,
				   "unable to open device (ret %d).", ret);
			return ret;
		}

		netif_start_queue(net);
	} else {
		DPRINT_ERR(NETVSC_DRV, "unable to open device...link is down.");
	}

	DPRINT_EXIT(NETVSC_DRV);
	return ret;
}

static int netvsc_close(struct net_device *net)
{
	struct net_device_context *net_device_ctx = netdev_priv(net);
	struct driver_context *driver_ctx =
	    driver_to_driver_context(net_device_ctx->device_ctx->device.driver);
	struct netvsc_driver_context *net_drv_ctx =
		(struct netvsc_driver_context *)driver_ctx;
	struct netvsc_driver *net_drv_obj = &net_drv_ctx->drv_obj;
	struct hv_device *device_obj = &net_device_ctx->device_ctx->device_obj;
	int ret;

	DPRINT_ENTER(NETVSC_DRV);

	netif_stop_queue(net);

	ret = net_drv_obj->OnClose(device_obj);
	if (ret != 0)
		DPRINT_ERR(NETVSC_DRV, "unable to close device (ret %d).", ret);

	DPRINT_EXIT(NETVSC_DRV);

	return ret;
}

static void netvsc_xmit_completion(void *context)
{
	struct hv_netvsc_packet *packet = (struct hv_netvsc_packet *)context;
	struct sk_buff *skb = (struct sk_buff *)
		(unsigned long)packet->Completion.Send.SendCompletionTid;
	struct net_device *net;

	DPRINT_ENTER(NETVSC_DRV);

	kfree(packet);

	if (skb) {
		net = skb->dev;
		dev_kfree_skb_any(skb);

		if (netif_queue_stopped(net)) {
			DPRINT_INFO(NETVSC_DRV, "net device (%p) waking up...",
				    net);

			netif_wake_queue(net);
		}
	}

	DPRINT_EXIT(NETVSC_DRV);
}

static int netvsc_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	struct net_device_context *net_device_ctx = netdev_priv(net);
	struct driver_context *driver_ctx =
	    driver_to_driver_context(net_device_ctx->device_ctx->device.driver);
	struct netvsc_driver_context *net_drv_ctx =
		(struct netvsc_driver_context *)driver_ctx;
	struct netvsc_driver *net_drv_obj = &net_drv_ctx->drv_obj;
	struct hv_netvsc_packet *packet;
	int i;
	int ret;
	int num_frags;
	int retries = 0;

	DPRINT_ENTER(NETVSC_DRV);

	/* Support only 1 chain of frags */
	ASSERT(skb_shinfo(skb)->frag_list == NULL);
	ASSERT(skb->dev == net);

	DPRINT_DBG(NETVSC_DRV, "xmit packet - len %d data_len %d",
		   skb->len, skb->data_len);

	/* Add 1 for skb->data and any additional ones requested */
	num_frags = skb_shinfo(skb)->nr_frags + 1 +
		    net_drv_obj->AdditionalRequestPageBufferCount;

	/* Allocate a netvsc packet based on # of frags. */
	packet = kzalloc(sizeof(struct hv_netvsc_packet) +
			 (num_frags * sizeof(struct hv_page_buffer)) +
			 net_drv_obj->RequestExtSize, GFP_ATOMIC);
	if (!packet) {
		DPRINT_ERR(NETVSC_DRV, "unable to allocate hv_netvsc_packet");
		return -1;
	}

	packet->Extension = (void *)(unsigned long)packet +
				sizeof(struct hv_netvsc_packet) +
				    (num_frags * sizeof(struct hv_page_buffer));

	/* Setup the rndis header */
	packet->PageBufferCount = num_frags;

	/* TODO: Flush all write buffers/ memory fence ??? */
	/* wmb(); */

	/* Initialize it from the skb */
	ASSERT(skb->data);
	packet->TotalDataBufferLength	= skb->len;

	/*
	 * Start filling in the page buffers starting at
	 * AdditionalRequestPageBufferCount offset
	 */
	packet->PageBuffers[net_drv_obj->AdditionalRequestPageBufferCount].Pfn = virt_to_phys(skb->data) >> PAGE_SHIFT;
	packet->PageBuffers[net_drv_obj->AdditionalRequestPageBufferCount].Offset = (unsigned long)skb->data & (PAGE_SIZE - 1);
	packet->PageBuffers[net_drv_obj->AdditionalRequestPageBufferCount].Length = skb->len - skb->data_len;

	ASSERT((skb->len - skb->data_len) <= PAGE_SIZE);

	for (i = net_drv_obj->AdditionalRequestPageBufferCount + 1;
	     i < num_frags; i++) {
		packet->PageBuffers[i].Pfn =
			page_to_pfn(skb_shinfo(skb)->frags[i-(net_drv_obj->AdditionalRequestPageBufferCount+1)].page);
		packet->PageBuffers[i].Offset =
			skb_shinfo(skb)->frags[i-(net_drv_obj->AdditionalRequestPageBufferCount+1)].page_offset;
		packet->PageBuffers[i].Length =
			skb_shinfo(skb)->frags[i-(net_drv_obj->AdditionalRequestPageBufferCount+1)].size;
	}

	/* Set the completion routine */
	packet->Completion.Send.OnSendCompletion = netvsc_xmit_completion;
	packet->Completion.Send.SendCompletionContext = packet;
	packet->Completion.Send.SendCompletionTid = (unsigned long)skb;

retry_send:
	ret = net_drv_obj->OnSend(&net_device_ctx->device_ctx->device_obj,
				  packet);

	if (ret == 0) {
		ret = NETDEV_TX_OK;
		net_device_ctx->stats.tx_bytes += skb->len;
		net_device_ctx->stats.tx_packets++;
	} else {
		retries++;
		if (retries < 4) {
			DPRINT_ERR(NETVSC_DRV, "unable to send..."
					"retrying %d...", retries);
			udelay(100);
			goto retry_send;
		}

		/* no more room or we are shutting down */
		DPRINT_ERR(NETVSC_DRV, "unable to send (%d)..."
			   "marking net device (%p) busy", ret, net);
		DPRINT_INFO(NETVSC_DRV, "net device (%p) stopping", net);

		ret = NETDEV_TX_BUSY;
		net_device_ctx->stats.tx_dropped++;

		netif_stop_queue(net);

		/*
		 * Null it since the caller will free it instead of the
		 * completion routine
		 */
		packet->Completion.Send.SendCompletionTid = 0;

		/*
		 * Release the resources since we will not get any send
		 * completion
		 */
		netvsc_xmit_completion((void *)packet);
	}

	DPRINT_DBG(NETVSC_DRV, "# of xmits %lu total size %lu",
		   net_device_ctx->stats.tx_packets,
		   net_device_ctx->stats.tx_bytes);

	DPRINT_EXIT(NETVSC_DRV);
	return ret;
}

/**
 * netvsc_linkstatus_callback - Link up/down notification
 */
static void netvsc_linkstatus_callback(struct hv_device *device_obj,
				       unsigned int status)
{
	struct device_context *device_ctx = to_device_context(device_obj);
	struct net_device *net = dev_get_drvdata(&device_ctx->device);

	DPRINT_ENTER(NETVSC_DRV);

	if (!net) {
		DPRINT_ERR(NETVSC_DRV, "got link status but net device "
				"not initialized yet");
		return;
	}

	if (status == 1) {
		netif_carrier_on(net);
		netif_wake_queue(net);
	} else {
		netif_carrier_off(net);
		netif_stop_queue(net);
	}
	DPRINT_EXIT(NETVSC_DRV);
}

/**
 * netvsc_recv_callback -  Callback when we receive a packet from the "wire" on the specified device.
 */
static int netvsc_recv_callback(struct hv_device *device_obj,
				struct hv_netvsc_packet *packet)
{
	struct device_context *device_ctx = to_device_context(device_obj);
	struct net_device *net = dev_get_drvdata(&device_ctx->device);
	struct net_device_context *net_device_ctx;
	struct sk_buff *skb;
	void *data;
	int ret;
	int i;
	unsigned long flags;

	DPRINT_ENTER(NETVSC_DRV);

	if (!net) {
		DPRINT_ERR(NETVSC_DRV, "got receive callback but net device "
				"not initialized yet");
		return 0;
	}

	net_device_ctx = netdev_priv(net);

	/* Allocate a skb - TODO preallocate this */
	/* Pad 2-bytes to align IP header to 16 bytes */
	skb = dev_alloc_skb(packet->TotalDataBufferLength + 2);
	ASSERT(skb);
	skb_reserve(skb, 2);
	skb->dev = net;

	/* for kmap_atomic */
	local_irq_save(flags);

	/*
	 * Copy to skb. This copy is needed here since the memory pointed by
	 * hv_netvsc_packet cannot be deallocated
	 */
	for (i = 0; i < packet->PageBufferCount; i++) {
		data = kmap_atomic(pfn_to_page(packet->PageBuffers[i].Pfn),
					       KM_IRQ1);
		data = (void *)(unsigned long)data +
				packet->PageBuffers[i].Offset;

		memcpy(skb_put(skb, packet->PageBuffers[i].Length), data,
		       packet->PageBuffers[i].Length);

		kunmap_atomic((void *)((unsigned long)data -
				       packet->PageBuffers[i].Offset), KM_IRQ1);
	}

	local_irq_restore(flags);

	skb->protocol = eth_type_trans(skb, net);

	skb->ip_summed = CHECKSUM_NONE;

	/*
	 * Pass the skb back up. Network stack will deallocate the skb when it
	 * is done
	 */
	ret = netif_rx(skb);

	switch (ret) {
	case NET_RX_DROP:
		net_device_ctx->stats.rx_dropped++;
		break;
	default:
		net_device_ctx->stats.rx_packets++;
		net_device_ctx->stats.rx_bytes += skb->len;
		break;

	}
	DPRINT_DBG(NETVSC_DRV, "# of recvs %lu total size %lu",
		   net_device_ctx->stats.rx_packets,
		   net_device_ctx->stats.rx_bytes);

	DPRINT_EXIT(NETVSC_DRV);

	return 0;
}

static const struct net_device_ops device_ops = {
	.ndo_open =			netvsc_open,
	.ndo_stop =			netvsc_close,
	.ndo_start_xmit =		netvsc_start_xmit,
	.ndo_get_stats =		netvsc_get_stats,
	.ndo_set_multicast_list =	netvsc_set_multicast_list,
};

static int netvsc_probe(struct device *device)
{
	struct driver_context *driver_ctx =
		driver_to_driver_context(device->driver);
	struct netvsc_driver_context *net_drv_ctx =
		(struct netvsc_driver_context *)driver_ctx;
	struct netvsc_driver *net_drv_obj = &net_drv_ctx->drv_obj;
	struct device_context *device_ctx = device_to_device_context(device);
	struct hv_device *device_obj = &device_ctx->device_obj;
	struct net_device *net = NULL;
	struct net_device_context *net_device_ctx;
	struct netvsc_device_info device_info;
	int ret;

	DPRINT_ENTER(NETVSC_DRV);

	if (!net_drv_obj->Base.OnDeviceAdd)
		return -1;

	net = alloc_etherdev(sizeof(struct net_device_context));
	if (!net)
		return -1;

	/* Set initial state */
	netif_carrier_off(net);
	netif_stop_queue(net);

	net_device_ctx = netdev_priv(net);
	net_device_ctx->device_ctx = device_ctx;
	dev_set_drvdata(device, net);

	/* Notify the netvsc driver of the new device */
	ret = net_drv_obj->Base.OnDeviceAdd(device_obj, &device_info);
	if (ret != 0) {
		free_netdev(net);
		dev_set_drvdata(device, NULL);

		DPRINT_ERR(NETVSC_DRV, "unable to add netvsc device (ret %d)",
			   ret);
		return ret;
	}

	/*
	 * If carrier is still off ie we did not get a link status callback,
	 * update it if necessary
	 */
	/*
	 * FIXME: We should use a atomic or test/set instead to avoid getting
	 * out of sync with the device's link status
	 */
	if (!netif_carrier_ok(net))
		if (!device_info.LinkState)
			netif_carrier_on(net);

	memcpy(net->dev_addr, device_info.MacAddr, ETH_ALEN);

	net->netdev_ops = &device_ops;

	SET_NETDEV_DEV(net, device);

	ret = register_netdev(net);
	if (ret != 0) {
		/* Remove the device and release the resource */
		net_drv_obj->Base.OnDeviceRemove(device_obj);
		free_netdev(net);
	}

	DPRINT_EXIT(NETVSC_DRV);
	return ret;
}

static int netvsc_remove(struct device *device)
{
	struct driver_context *driver_ctx =
		driver_to_driver_context(device->driver);
	struct netvsc_driver_context *net_drv_ctx =
		(struct netvsc_driver_context *)driver_ctx;
	struct netvsc_driver *net_drv_obj = &net_drv_ctx->drv_obj;
	struct device_context *device_ctx = device_to_device_context(device);
	struct net_device *net = dev_get_drvdata(&device_ctx->device);
	struct hv_device *device_obj = &device_ctx->device_obj;
	int ret;

	DPRINT_ENTER(NETVSC_DRV);

	if (net == NULL) {
		DPRINT_INFO(NETVSC, "no net device to remove");
		DPRINT_EXIT(NETVSC_DRV);
		return 0;
	}

	if (!net_drv_obj->Base.OnDeviceRemove) {
		DPRINT_EXIT(NETVSC_DRV);
		return -1;
	}

	/* Stop outbound asap */
	netif_stop_queue(net);
	/* netif_carrier_off(net); */

	unregister_netdev(net);

	/*
	 * Call to the vsc driver to let it know that the device is being
	 * removed
	 */
	ret = net_drv_obj->Base.OnDeviceRemove(device_obj);
	if (ret != 0) {
		/* TODO: */
		DPRINT_ERR(NETVSC, "unable to remove vsc device (ret %d)", ret);
	}

	free_netdev(net);
	DPRINT_EXIT(NETVSC_DRV);
	return ret;
}

static int netvsc_drv_exit_cb(struct device *dev, void *data)
{
	struct device **curr = (struct device **)data;

	*curr = dev;
	/* stop iterating */
	return 1;
}

static void netvsc_drv_exit(void)
{
	struct netvsc_driver *netvsc_drv_obj = &g_netvsc_drv.drv_obj;
	struct driver_context *drv_ctx = &g_netvsc_drv.drv_ctx;
	struct device *current_dev;
	int ret;

	DPRINT_ENTER(NETVSC_DRV);

	while (1) {
		current_dev = NULL;

		/* Get the device */
		ret = driver_for_each_device(&drv_ctx->driver, NULL,
					     &current_dev, netvsc_drv_exit_cb);
		if (ret)
			DPRINT_WARN(NETVSC_DRV,
				    "driver_for_each_device returned %d", ret);

		if (current_dev == NULL)
			break;

		/* Initiate removal from the top-down */
		DPRINT_INFO(NETVSC_DRV, "unregistering device (%p)...",
			    current_dev);

		device_unregister(current_dev);
	}

	if (netvsc_drv_obj->Base.OnCleanup)
		netvsc_drv_obj->Base.OnCleanup(&netvsc_drv_obj->Base);

	vmbus_child_driver_unregister(drv_ctx);

	DPRINT_EXIT(NETVSC_DRV);

	return;
}

static int netvsc_drv_init(int (*drv_init)(struct hv_driver *drv))
{
	struct netvsc_driver *net_drv_obj = &g_netvsc_drv.drv_obj;
	struct driver_context *drv_ctx = &g_netvsc_drv.drv_ctx;
	int ret;

	DPRINT_ENTER(NETVSC_DRV);

	vmbus_get_interface(&net_drv_obj->Base.VmbusChannelInterface);

	net_drv_obj->RingBufferSize = netvsc_ringbuffer_size;
	net_drv_obj->OnReceiveCallback = netvsc_recv_callback;
	net_drv_obj->OnLinkStatusChanged = netvsc_linkstatus_callback;

	/* Callback to client driver to complete the initialization */
	drv_init(&net_drv_obj->Base);

	drv_ctx->driver.name = net_drv_obj->Base.name;
	memcpy(&drv_ctx->class_id, &net_drv_obj->Base.deviceType,
	       sizeof(struct hv_guid));

	drv_ctx->probe = netvsc_probe;
	drv_ctx->remove = netvsc_remove;

	/* The driver belongs to vmbus */
	ret = vmbus_child_driver_register(drv_ctx);

	DPRINT_EXIT(NETVSC_DRV);

	return ret;
}

static int __init netvsc_init(void)
{
	int ret;

	DPRINT_ENTER(NETVSC_DRV);
	DPRINT_INFO(NETVSC_DRV, "Netvsc initializing....");

	ret = netvsc_drv_init(NetVscInitialize);

	DPRINT_EXIT(NETVSC_DRV);

	return ret;
}

static void __exit netvsc_exit(void)
{
	DPRINT_ENTER(NETVSC_DRV);
	netvsc_drv_exit();
	DPRINT_EXIT(NETVSC_DRV);
}

module_param(netvsc_ringbuffer_size, int, S_IRUGO);

module_init(netvsc_init);
module_exit(netvsc_exit);
