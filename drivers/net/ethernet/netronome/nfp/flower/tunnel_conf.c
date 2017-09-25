/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/etherdevice.h>
#include <linux/idr.h>
#include <net/dst_metadata.h>

#include "cmsg.h"
#include "main.h"
#include "../nfp_net_repr.h"
#include "../nfp_net.h"

/**
 * struct nfp_tun_mac_addr - configure MAC address of tunnel EP on NFP
 * @reserved:	reserved for future use
 * @count:	number of MAC addresses in the message
 * @index:	index of MAC address in the lookup table
 * @addr:	interface MAC address
 * @addresses:	series of MACs to offload
 */
struct nfp_tun_mac_addr {
	__be16 reserved;
	__be16 count;
	struct index_mac_addr {
		__be16 index;
		u8 addr[ETH_ALEN];
	} addresses[];
};

/**
 * struct nfp_tun_mac_offload_entry - list of MACs to offload
 * @index:	index of MAC address for offloading
 * @addr:	interface MAC address
 * @list:	list pointer
 */
struct nfp_tun_mac_offload_entry {
	__be16 index;
	u8 addr[ETH_ALEN];
	struct list_head list;
};

#define NFP_MAX_MAC_INDEX       0xff

/**
 * struct nfp_tun_mac_non_nfp_idx - converts non NFP netdev ifindex to 8-bit id
 * @ifindex:	netdev ifindex of the device
 * @index:	index of netdevs mac on NFP
 * @list:	list pointer
 */
struct nfp_tun_mac_non_nfp_idx {
	int ifindex;
	u8 index;
	struct list_head list;
};

static bool nfp_tun_is_netdev_to_offload(struct net_device *netdev)
{
	if (!netdev->rtnl_link_ops)
		return false;
	if (!strcmp(netdev->rtnl_link_ops->kind, "openvswitch"))
		return true;
	if (!strcmp(netdev->rtnl_link_ops->kind, "vxlan"))
		return true;

	return false;
}

static int
nfp_flower_xmit_tun_conf(struct nfp_app *app, u8 mtype, u16 plen, void *pdata)
{
	struct sk_buff *skb;
	unsigned char *msg;

	skb = nfp_flower_cmsg_alloc(app, plen, mtype);
	if (!skb)
		return -ENOMEM;

	msg = nfp_flower_cmsg_get_data(skb);
	memcpy(msg, pdata, nfp_flower_cmsg_get_data_len(skb));

	nfp_ctrl_tx(app->ctrl, skb);
	return 0;
}

void nfp_tunnel_write_macs(struct nfp_app *app)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_tun_mac_offload_entry *entry;
	struct nfp_tun_mac_addr *payload;
	struct list_head *ptr, *storage;
	int mac_count, err, pay_size;

	mutex_lock(&priv->nfp_mac_off_lock);
	if (!priv->nfp_mac_off_count) {
		mutex_unlock(&priv->nfp_mac_off_lock);
		return;
	}

	pay_size = sizeof(struct nfp_tun_mac_addr) +
		   sizeof(struct index_mac_addr) * priv->nfp_mac_off_count;

	payload = kzalloc(pay_size, GFP_KERNEL);
	if (!payload) {
		mutex_unlock(&priv->nfp_mac_off_lock);
		return;
	}

	payload->count = cpu_to_be16(priv->nfp_mac_off_count);

	mac_count = 0;
	list_for_each_safe(ptr, storage, &priv->nfp_mac_off_list) {
		entry = list_entry(ptr, struct nfp_tun_mac_offload_entry,
				   list);
		payload->addresses[mac_count].index = entry->index;
		ether_addr_copy(payload->addresses[mac_count].addr,
				entry->addr);
		mac_count++;
	}

	err = nfp_flower_xmit_tun_conf(app, NFP_FLOWER_CMSG_TYPE_TUN_MAC,
				       pay_size, payload);

	kfree(payload);

	if (err) {
		mutex_unlock(&priv->nfp_mac_off_lock);
		/* Write failed so retain list for future retry. */
		return;
	}

	/* If list was successfully offloaded, flush it. */
	list_for_each_safe(ptr, storage, &priv->nfp_mac_off_list) {
		entry = list_entry(ptr, struct nfp_tun_mac_offload_entry,
				   list);
		list_del(&entry->list);
		kfree(entry);
	}

	priv->nfp_mac_off_count = 0;
	mutex_unlock(&priv->nfp_mac_off_lock);
}

static int nfp_tun_get_mac_idx(struct nfp_app *app, int ifindex)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_tun_mac_non_nfp_idx *entry;
	struct list_head *ptr, *storage;
	int idx;

	mutex_lock(&priv->nfp_mac_index_lock);
	list_for_each_safe(ptr, storage, &priv->nfp_mac_index_list) {
		entry = list_entry(ptr, struct nfp_tun_mac_non_nfp_idx, list);
		if (entry->ifindex == ifindex) {
			idx = entry->index;
			mutex_unlock(&priv->nfp_mac_index_lock);
			return idx;
		}
	}

	idx = ida_simple_get(&priv->nfp_mac_off_ids, 0,
			     NFP_MAX_MAC_INDEX, GFP_KERNEL);
	if (idx < 0) {
		mutex_unlock(&priv->nfp_mac_index_lock);
		return idx;
	}

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		mutex_unlock(&priv->nfp_mac_index_lock);
		return -ENOMEM;
	}
	entry->ifindex = ifindex;
	entry->index = idx;
	list_add_tail(&entry->list, &priv->nfp_mac_index_list);
	mutex_unlock(&priv->nfp_mac_index_lock);

	return idx;
}

static void nfp_tun_del_mac_idx(struct nfp_app *app, int ifindex)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_tun_mac_non_nfp_idx *entry;
	struct list_head *ptr, *storage;

	mutex_lock(&priv->nfp_mac_index_lock);
	list_for_each_safe(ptr, storage, &priv->nfp_mac_index_list) {
		entry = list_entry(ptr, struct nfp_tun_mac_non_nfp_idx, list);
		if (entry->ifindex == ifindex) {
			ida_simple_remove(&priv->nfp_mac_off_ids,
					  entry->index);
			list_del(&entry->list);
			kfree(entry);
			break;
		}
	}
	mutex_unlock(&priv->nfp_mac_index_lock);
}

static void nfp_tun_add_to_mac_offload_list(struct net_device *netdev,
					    struct nfp_app *app)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_tun_mac_offload_entry *entry;
	u16 nfp_mac_idx;
	int port = 0;

	/* Check if MAC should be offloaded. */
	if (!is_valid_ether_addr(netdev->dev_addr))
		return;

	if (nfp_netdev_is_nfp_repr(netdev))
		port = nfp_repr_get_port_id(netdev);
	else if (!nfp_tun_is_netdev_to_offload(netdev))
		return;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		nfp_flower_cmsg_warn(app, "Mem fail when offloading MAC.\n");
		return;
	}

	if (FIELD_GET(NFP_FLOWER_CMSG_PORT_TYPE, port) ==
	    NFP_FLOWER_CMSG_PORT_TYPE_PHYS_PORT) {
		nfp_mac_idx = port << 8 | NFP_FLOWER_CMSG_PORT_TYPE_PHYS_PORT;
	} else if (FIELD_GET(NFP_FLOWER_CMSG_PORT_TYPE, port) ==
		   NFP_FLOWER_CMSG_PORT_TYPE_PCIE_PORT) {
		port = FIELD_GET(NFP_FLOWER_CMSG_PORT_VNIC, port);
		nfp_mac_idx = port << 8 | NFP_FLOWER_CMSG_PORT_TYPE_PCIE_PORT;
	} else {
		/* Must assign our own unique 8-bit index. */
		int idx = nfp_tun_get_mac_idx(app, netdev->ifindex);

		if (idx < 0) {
			nfp_flower_cmsg_warn(app, "Can't assign non-repr MAC index.\n");
			kfree(entry);
			return;
		}
		nfp_mac_idx = idx << 8 | NFP_FLOWER_CMSG_PORT_TYPE_OTHER_PORT;
	}

	entry->index = cpu_to_be16(nfp_mac_idx);
	ether_addr_copy(entry->addr, netdev->dev_addr);

	mutex_lock(&priv->nfp_mac_off_lock);
	priv->nfp_mac_off_count++;
	list_add_tail(&entry->list, &priv->nfp_mac_off_list);
	mutex_unlock(&priv->nfp_mac_off_lock);
}

static int nfp_tun_mac_event_handler(struct notifier_block *nb,
				     unsigned long event, void *ptr)
{
	struct nfp_flower_priv *app_priv;
	struct net_device *netdev;
	struct nfp_app *app;

	if (event == NETDEV_DOWN || event == NETDEV_UNREGISTER) {
		app_priv = container_of(nb, struct nfp_flower_priv,
					nfp_tun_mac_nb);
		app = app_priv->app;
		netdev = netdev_notifier_info_to_dev(ptr);

		/* If non-nfp netdev then free its offload index. */
		if (nfp_tun_is_netdev_to_offload(netdev))
			nfp_tun_del_mac_idx(app, netdev->ifindex);
	} else if (event == NETDEV_UP || event == NETDEV_CHANGEADDR ||
		   event == NETDEV_REGISTER) {
		app_priv = container_of(nb, struct nfp_flower_priv,
					nfp_tun_mac_nb);
		app = app_priv->app;
		netdev = netdev_notifier_info_to_dev(ptr);

		nfp_tun_add_to_mac_offload_list(netdev, app);

		/* Force a list write to keep NFP up to date. */
		nfp_tunnel_write_macs(app);
	}
	return NOTIFY_OK;
}

int nfp_tunnel_config_start(struct nfp_app *app)
{
	struct nfp_flower_priv *priv = app->priv;
	struct net_device *netdev;
	int err;

	/* Initialise priv data for MAC offloading. */
	priv->nfp_mac_off_count = 0;
	mutex_init(&priv->nfp_mac_off_lock);
	INIT_LIST_HEAD(&priv->nfp_mac_off_list);
	priv->nfp_tun_mac_nb.notifier_call = nfp_tun_mac_event_handler;
	mutex_init(&priv->nfp_mac_index_lock);
	INIT_LIST_HEAD(&priv->nfp_mac_index_list);
	ida_init(&priv->nfp_mac_off_ids);

	err = register_netdevice_notifier(&priv->nfp_tun_mac_nb);
	if (err)
		goto err_free_mac_ida;

	/* Parse netdevs already registered for MACs that need offloaded. */
	rtnl_lock();
	for_each_netdev(&init_net, netdev)
		nfp_tun_add_to_mac_offload_list(netdev, app);
	rtnl_unlock();

	return 0;

err_free_mac_ida:
	ida_destroy(&priv->nfp_mac_off_ids);
	return err;
}

void nfp_tunnel_config_stop(struct nfp_app *app)
{
	struct nfp_tun_mac_offload_entry *mac_entry;
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_tun_mac_non_nfp_idx *mac_idx;
	struct list_head *ptr, *storage;

	unregister_netdevice_notifier(&priv->nfp_tun_mac_nb);

	/* Free any memory that may be occupied by MAC list. */
	mutex_lock(&priv->nfp_mac_off_lock);
	list_for_each_safe(ptr, storage, &priv->nfp_mac_off_list) {
		mac_entry = list_entry(ptr, struct nfp_tun_mac_offload_entry,
				       list);
		list_del(&mac_entry->list);
		kfree(mac_entry);
	}
	mutex_unlock(&priv->nfp_mac_off_lock);

	/* Free any memory that may be occupied by MAC index list. */
	mutex_lock(&priv->nfp_mac_index_lock);
	list_for_each_safe(ptr, storage, &priv->nfp_mac_index_list) {
		mac_idx = list_entry(ptr, struct nfp_tun_mac_non_nfp_idx,
				     list);
		list_del(&mac_idx->list);
		kfree(mac_idx);
	}
	mutex_unlock(&priv->nfp_mac_index_lock);

	ida_destroy(&priv->nfp_mac_off_ids);
}
