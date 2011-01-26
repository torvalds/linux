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
#include <linux/slab.h>
#include <linux/dmi.h>
#include <linux/pci.h>
#include <net/arp.h>
#include <net/route.h>
#include <net/sock.h>
#include <net/pkt_sched.h>
#include "osd.h"
#include "logging.h"
#include "version_info.h"
#include "vmbus.h"
#include "netvsc_api.h"

struct net_device_context {
	/* point back to our device context */
	struct vm_device *device_ctx;
	unsigned long avail;
};

struct netvsc_driver_context {
	/* !! These must be the first 2 fields !! */
	/* Which is a bug FIXME! */
	struct driver_context drv_ctx;
	struct netvsc_driver drv_obj;
};

#define PACKET_PAGES_LOWATER  8
/* Need this many pages to handle worst case fragmented packet */
#define PACKET_PAGES_HIWATER  (MAX_SKB_FRAGS + 2)

static int ring_size = 128;
module_param(ring_size, int, S_IRUGO);
MODULE_PARM_DESC(ring_size, "Ring buffer size (# of pages)");

/* The one and only one */
static struct netvsc_driver_context g_netvsc_drv;

/* no-op so the netdev core doesn't return -EINVAL when modifying the the
 * multicast address list in SIOCADDMULTI. hv is setup to get all multicast
 * when it calls RndisFilterOnOpen() */
static void netvsc_set_multicast_list(struct net_device *net)
{
}

static int netvsc_open(struct net_device *net)
{
	struct net_device_context *net_device_ctx = netdev_priv(net);
	struct hv_device *device_obj = &net_device_ctx->device_ctx->device_obj;
	int ret = 0;

	if (netif_carrier_ok(net)) {
		/* Open up the device */
		ret = rndis_filter_open(device_obj);
		if (ret != 0) {
			DPRINT_ERR(NETVSC_DRV,
				   "unable to open device (ret %d).", ret);
			return ret;
		}

		netif_start_queue(net);
	} else {
		DPRINT_ERR(NETVSC_DRV, "unable to open device...link is down.");
	}

	return ret;
}

static int netvsc_close(struct net_device *net)
{
	struct net_device_context *net_device_ctx = netdev_priv(net);
	struct hv_device *device_obj = &net_device_ctx->device_ctx->device_obj;
	int ret;

	netif_stop_queue(net);

	ret = rndis_filter_close(device_obj);
	if (ret != 0)
		DPRINT_ERR(NETVSC_DRV, "unable to close device (ret %d).", ret);

	return ret;
}

static void netvsc_xmit_completion(void *context)
{
	struct hv_netvsc_packet *packet = (struct hv_netvsc_packet *)context;
	struct sk_buff *skb = (struct sk_buff *)
		(unsigned long)packet->completion.send.send_completion_tid;

	kfree(packet);

	if (skb) {
		struct net_device *net = skb->dev;
		struct net_device_context *net_device_ctx = netdev_priv(net);
		unsigned int num_pages = skb_shinfo(skb)->nr_frags + 2;

		dev_kfree_skb_any(skb);

		if ((net_device_ctx->avail += num_pages) >= PACKET_PAGES_HIWATER)
 			netif_wake_queue(net);
	}
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
	int ret;
	unsigned int i, num_pages;

	DPRINT_DBG(NETVSC_DRV, "xmit packet - len %d data_len %d",
		   skb->len, skb->data_len);

	/* Add 1 for skb->data and additional one for RNDIS */
	num_pages = skb_shinfo(skb)->nr_frags + 1 + 1;
	if (num_pages > net_device_ctx->avail)
		return NETDEV_TX_BUSY;

	/* Allocate a netvsc packet based on # of frags. */
	packet = kzalloc(sizeof(struct hv_netvsc_packet) +
			 (num_pages * sizeof(struct hv_page_buffer)) +
			 net_drv_obj->req_ext_size, GFP_ATOMIC);
	if (!packet) {
		/* out of memory, silently drop packet */
		DPRINT_ERR(NETVSC_DRV, "unable to allocate hv_netvsc_packet");

		dev_kfree_skb(skb);
		net->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	packet->extension = (void *)(unsigned long)packet +
				sizeof(struct hv_netvsc_packet) +
				    (num_pages * sizeof(struct hv_page_buffer));

	/* Setup the rndis header */
	packet->page_buf_cnt = num_pages;

	/* TODO: Flush all write buffers/ memory fence ??? */
	/* wmb(); */

	/* Initialize it from the skb */
	packet->total_data_buflen	= skb->len;

	/* Start filling in the page buffers starting after RNDIS buffer. */
	packet->page_buf[1].Pfn = virt_to_phys(skb->data) >> PAGE_SHIFT;
	packet->page_buf[1].Offset
		= (unsigned long)skb->data & (PAGE_SIZE - 1);
	packet->page_buf[1].Length = skb_headlen(skb);

	/* Additional fragments are after SKB data */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *f = &skb_shinfo(skb)->frags[i];

		packet->page_buf[i+2].Pfn = page_to_pfn(f->page);
		packet->page_buf[i+2].Offset = f->page_offset;
		packet->page_buf[i+2].Length = f->size;
	}

	/* Set the completion routine */
	packet->completion.send.send_completion = netvsc_xmit_completion;
	packet->completion.send.send_completion_ctx = packet;
	packet->completion.send.send_completion_tid = (unsigned long)skb;

	ret = net_drv_obj->send(&net_device_ctx->device_ctx->device_obj,
				  packet);
	if (ret == 0) {
		net->stats.tx_bytes += skb->len;
		net->stats.tx_packets++;

		DPRINT_DBG(NETVSC_DRV, "# of xmits %lu total size %lu",
			   net->stats.tx_packets,
			   net->stats.tx_bytes);

		if ((net_device_ctx->avail -= num_pages) < PACKET_PAGES_LOWATER)
			netif_stop_queue(net);
	} else {
		/* we are shutting down or bus overloaded, just drop packet */
		net->stats.tx_dropped++;
		netvsc_xmit_completion(packet);
	}

	return NETDEV_TX_OK;
}

/*
 * netvsc_linkstatus_callback - Link up/down notification
 */
static void netvsc_linkstatus_callback(struct hv_device *device_obj,
				       unsigned int status)
{
	struct vm_device *device_ctx = to_vm_device(device_obj);
	struct net_device *net = dev_get_drvdata(&device_ctx->device);

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
}

/*
 * netvsc_recv_callback -  Callback when we receive a packet from the
 * "wire" on the specified device.
 */
static int netvsc_recv_callback(struct hv_device *device_obj,
				struct hv_netvsc_packet *packet)
{
	struct vm_device *device_ctx = to_vm_device(device_obj);
	struct net_device *net = dev_get_drvdata(&device_ctx->device);
	struct sk_buff *skb;
	void *data;
	int i;
	unsigned long flags;

	if (!net) {
		DPRINT_ERR(NETVSC_DRV, "got receive callback but net device "
				"not initialized yet");
		return 0;
	}

	/* Allocate a skb - TODO direct I/O to pages? */
	skb = netdev_alloc_skb_ip_align(net, packet->total_data_buflen);
	if (unlikely(!skb)) {
		++net->stats.rx_dropped;
		return 0;
	}

	/* for kmap_atomic */
	local_irq_save(flags);

	/*
	 * Copy to skb. This copy is needed here since the memory pointed by
	 * hv_netvsc_packet cannot be deallocated
	 */
	for (i = 0; i < packet->page_buf_cnt; i++) {
		data = kmap_atomic(pfn_to_page(packet->page_buf[i].Pfn),
					       KM_IRQ1);
		data = (void *)(unsigned long)data +
				packet->page_buf[i].Offset;

		memcpy(skb_put(skb, packet->page_buf[i].Length), data,
		       packet->page_buf[i].Length);

		kunmap_atomic((void *)((unsigned long)data -
				       packet->page_buf[i].Offset), KM_IRQ1);
	}

	local_irq_restore(flags);

	skb->protocol = eth_type_trans(skb, net);
	skb->ip_summed = CHECKSUM_NONE;

	net->stats.rx_packets++;
	net->stats.rx_bytes += skb->len;

	/*
	 * Pass the skb back up. Network stack will deallocate the skb when it
	 * is done.
	 * TODO - use NAPI?
	 */
	netif_rx(skb);

	DPRINT_DBG(NETVSC_DRV, "# of recvs %lu total size %lu",
		   net->stats.rx_packets, net->stats.rx_bytes);

	return 0;
}

static void netvsc_get_drvinfo(struct net_device *net,
			       struct ethtool_drvinfo *info)
{
	strcpy(info->driver, "hv_netvsc");
	strcpy(info->version, HV_DRV_VERSION);
	strcpy(info->fw_version, "N/A");
}

static const struct ethtool_ops ethtool_ops = {
	.get_drvinfo	= netvsc_get_drvinfo,
	.get_sg		= ethtool_op_get_sg,
	.set_sg		= ethtool_op_set_sg,
	.get_link	= ethtool_op_get_link,
};

static const struct net_device_ops device_ops = {
	.ndo_open =			netvsc_open,
	.ndo_stop =			netvsc_close,
	.ndo_start_xmit =		netvsc_start_xmit,
	.ndo_set_multicast_list =	netvsc_set_multicast_list,
	.ndo_change_mtu =		eth_change_mtu,
	.ndo_validate_addr =		eth_validate_addr,
	.ndo_set_mac_address =		eth_mac_addr,
};

static int netvsc_probe(struct device *device)
{
	struct driver_context *driver_ctx =
		driver_to_driver_context(device->driver);
	struct netvsc_driver_context *net_drv_ctx =
		(struct netvsc_driver_context *)driver_ctx;
	struct netvsc_driver *net_drv_obj = &net_drv_ctx->drv_obj;
	struct vm_device *device_ctx = device_to_vm_device(device);
	struct hv_device *device_obj = &device_ctx->device_obj;
	struct net_device *net = NULL;
	struct net_device_context *net_device_ctx;
	struct netvsc_device_info device_info;
	int ret;

	if (!net_drv_obj->base.OnDeviceAdd)
		return -1;

	net = alloc_etherdev(sizeof(struct net_device_context));
	if (!net)
		return -1;

	/* Set initial state */
	netif_carrier_off(net);
	netif_stop_queue(net);

	net_device_ctx = netdev_priv(net);
	net_device_ctx->device_ctx = device_ctx;
	net_device_ctx->avail = ring_size;
	dev_set_drvdata(device, net);

	/* Notify the netvsc driver of the new device */
	ret = net_drv_obj->base.OnDeviceAdd(device_obj, &device_info);
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
		if (!device_info.link_state)
			netif_carrier_on(net);

	memcpy(net->dev_addr, device_info.mac_adr, ETH_ALEN);

	net->netdev_ops = &device_ops;

	/* TODO: Add GSO and Checksum offload */
	net->features = NETIF_F_SG;

	SET_ETHTOOL_OPS(net, &ethtool_ops);
	SET_NETDEV_DEV(net, device);

	ret = register_netdev(net);
	if (ret != 0) {
		/* Remove the device and release the resource */
		net_drv_obj->base.OnDeviceRemove(device_obj);
		free_netdev(net);
	}

	return ret;
}

static int netvsc_remove(struct device *device)
{
	struct driver_context *driver_ctx =
		driver_to_driver_context(device->driver);
	struct netvsc_driver_context *net_drv_ctx =
		(struct netvsc_driver_context *)driver_ctx;
	struct netvsc_driver *net_drv_obj = &net_drv_ctx->drv_obj;
	struct vm_device *device_ctx = device_to_vm_device(device);
	struct net_device *net = dev_get_drvdata(&device_ctx->device);
	struct hv_device *device_obj = &device_ctx->device_obj;
	int ret;

	if (net == NULL) {
		DPRINT_INFO(NETVSC, "no net device to remove");
		return 0;
	}

	if (!net_drv_obj->base.OnDeviceRemove)
		return -1;

	/* Stop outbound asap */
	netif_stop_queue(net);
	/* netif_carrier_off(net); */

	unregister_netdev(net);

	/*
	 * Call to the vsc driver to let it know that the device is being
	 * removed
	 */
	ret = net_drv_obj->base.OnDeviceRemove(device_obj);
	if (ret != 0) {
		/* TODO: */
		DPRINT_ERR(NETVSC, "unable to remove vsc device (ret %d)", ret);
	}

	free_netdev(net);
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

	if (netvsc_drv_obj->base.OnCleanup)
		netvsc_drv_obj->base.OnCleanup(&netvsc_drv_obj->base);

	vmbus_child_driver_unregister(drv_ctx);

	return;
}

static int netvsc_drv_init(int (*drv_init)(struct hv_driver *drv))
{
	struct netvsc_driver *net_drv_obj = &g_netvsc_drv.drv_obj;
	struct driver_context *drv_ctx = &g_netvsc_drv.drv_ctx;
	int ret;

	net_drv_obj->ring_buf_size = ring_size * PAGE_SIZE;
	net_drv_obj->recv_cb = netvsc_recv_callback;
	net_drv_obj->link_status_change = netvsc_linkstatus_callback;

	/* Callback to client driver to complete the initialization */
	drv_init(&net_drv_obj->base);

	drv_ctx->driver.name = net_drv_obj->base.name;
	memcpy(&drv_ctx->class_id, &net_drv_obj->base.deviceType,
	       sizeof(struct hv_guid));

	drv_ctx->probe = netvsc_probe;
	drv_ctx->remove = netvsc_remove;

	/* The driver belongs to vmbus */
	ret = vmbus_child_driver_register(drv_ctx);

	return ret;
}

static const struct dmi_system_id __initconst
hv_netvsc_dmi_table[] __maybe_unused  = {
	{
		.ident = "Hyper-V",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Virtual Machine"),
			DMI_MATCH(DMI_BOARD_NAME, "Virtual Machine"),
		},
	},
	{ },
};
MODULE_DEVICE_TABLE(dmi, hv_netvsc_dmi_table);

static int __init netvsc_init(void)
{
	DPRINT_INFO(NETVSC_DRV, "Netvsc initializing....");

	if (!dmi_check_system(hv_netvsc_dmi_table))
		return -ENODEV;

	return netvsc_drv_init(netvsc_initialize);
}

static void __exit netvsc_exit(void)
{
	netvsc_drv_exit();
}

static const struct pci_device_id __initconst
hv_netvsc_pci_table[] __maybe_unused = {
	{ PCI_DEVICE(0x1414, 0x5353) }, /* VGA compatible controller */
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, hv_netvsc_pci_table);

MODULE_LICENSE("GPL");
MODULE_VERSION(HV_DRV_VERSION);
MODULE_DESCRIPTION("Microsoft Hyper-V network driver");

module_init(netvsc_init);
module_exit(netvsc_exit);
