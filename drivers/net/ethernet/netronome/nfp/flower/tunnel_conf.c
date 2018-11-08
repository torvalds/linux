// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#include <linux/etherdevice.h>
#include <linux/inetdevice.h>
#include <net/netevent.h>
#include <net/vxlan.h>
#include <linux/idr.h>
#include <net/dst_metadata.h>
#include <net/arp.h>

#include "cmsg.h"
#include "main.h"
#include "../nfp_net_repr.h"
#include "../nfp_net.h"

#define NFP_FL_MAX_ROUTES               32

/**
 * struct nfp_tun_active_tuns - periodic message of active tunnels
 * @seq:		sequence number of the message
 * @count:		number of tunnels report in message
 * @flags:		options part of the request
 * @tun_info.ipv4:		dest IPv4 address of active route
 * @tun_info.egress_port:	port the encapsulated packet egressed
 * @tun_info.extra:		reserved for future use
 * @tun_info:		tunnels that have sent traffic in reported period
 */
struct nfp_tun_active_tuns {
	__be32 seq;
	__be32 count;
	__be32 flags;
	struct route_ip_info {
		__be32 ipv4;
		__be32 egress_port;
		__be32 extra[2];
	} tun_info[];
};

/**
 * struct nfp_tun_neigh - neighbour/route entry on the NFP
 * @dst_ipv4:	destination IPv4 address
 * @src_ipv4:	source IPv4 address
 * @dst_addr:	destination MAC address
 * @src_addr:	source MAC address
 * @port_id:	NFP port to output packet on - associated with source IPv4
 */
struct nfp_tun_neigh {
	__be32 dst_ipv4;
	__be32 src_ipv4;
	u8 dst_addr[ETH_ALEN];
	u8 src_addr[ETH_ALEN];
	__be32 port_id;
};

/**
 * struct nfp_tun_req_route_ipv4 - NFP requests a route/neighbour lookup
 * @ingress_port:	ingress port of packet that signalled request
 * @ipv4_addr:		destination ipv4 address for route
 * @reserved:		reserved for future use
 */
struct nfp_tun_req_route_ipv4 {
	__be32 ingress_port;
	__be32 ipv4_addr;
	__be32 reserved[2];
};

/**
 * struct nfp_ipv4_route_entry - routes that are offloaded to the NFP
 * @ipv4_addr:	destination of route
 * @list:	list pointer
 */
struct nfp_ipv4_route_entry {
	__be32 ipv4_addr;
	struct list_head list;
};

#define NFP_FL_IPV4_ADDRS_MAX        32

/**
 * struct nfp_tun_ipv4_addr - set the IP address list on the NFP
 * @count:	number of IPs populated in the array
 * @ipv4_addr:	array of IPV4_ADDRS_MAX 32 bit IPv4 addresses
 */
struct nfp_tun_ipv4_addr {
	__be32 count;
	__be32 ipv4_addr[NFP_FL_IPV4_ADDRS_MAX];
};

/**
 * struct nfp_ipv4_addr_entry - cached IPv4 addresses
 * @ipv4_addr:	IP address
 * @ref_count:	number of rules currently using this IP
 * @list:	list pointer
 */
struct nfp_ipv4_addr_entry {
	__be32 ipv4_addr;
	int ref_count;
	struct list_head list;
};

/**
 * struct nfp_tun_mac_addr - configure MAC address of tunnel EP on NFP
 * @reserved:	reserved for future use
 * @count:	number of MAC addresses in the message
 * @addresses.index:	index of MAC address in the lookup table
 * @addresses.addr:	interface MAC address
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

void nfp_tunnel_keep_alive(struct nfp_app *app, struct sk_buff *skb)
{
	struct nfp_tun_active_tuns *payload;
	struct net_device *netdev;
	int count, i, pay_len;
	struct neighbour *n;
	__be32 ipv4_addr;
	u32 port;

	payload = nfp_flower_cmsg_get_data(skb);
	count = be32_to_cpu(payload->count);
	if (count > NFP_FL_MAX_ROUTES) {
		nfp_flower_cmsg_warn(app, "Tunnel keep-alive request exceeds max routes.\n");
		return;
	}

	pay_len = nfp_flower_cmsg_get_data_len(skb);
	if (pay_len != sizeof(struct nfp_tun_active_tuns) +
	    sizeof(struct route_ip_info) * count) {
		nfp_flower_cmsg_warn(app, "Corruption in tunnel keep-alive message.\n");
		return;
	}

	for (i = 0; i < count; i++) {
		ipv4_addr = payload->tun_info[i].ipv4;
		port = be32_to_cpu(payload->tun_info[i].egress_port);
		netdev = nfp_app_repr_get(app, port);
		if (!netdev)
			continue;

		n = neigh_lookup(&arp_tbl, &ipv4_addr, netdev);
		if (!n)
			continue;

		/* Update the used timestamp of neighbour */
		neigh_event_send(n, NULL);
		neigh_release(n);
	}
}

static bool nfp_tun_is_netdev_to_offload(struct net_device *netdev)
{
	if (!netdev->rtnl_link_ops)
		return false;
	if (!strcmp(netdev->rtnl_link_ops->kind, "openvswitch"))
		return true;
	if (netif_is_vxlan(netdev))
		return true;

	return false;
}

static int
nfp_flower_xmit_tun_conf(struct nfp_app *app, u8 mtype, u16 plen, void *pdata,
			 gfp_t flag)
{
	struct sk_buff *skb;
	unsigned char *msg;

	skb = nfp_flower_cmsg_alloc(app, plen, mtype, flag);
	if (!skb)
		return -ENOMEM;

	msg = nfp_flower_cmsg_get_data(skb);
	memcpy(msg, pdata, nfp_flower_cmsg_get_data_len(skb));

	nfp_ctrl_tx(app->ctrl, skb);
	return 0;
}

static bool nfp_tun_has_route(struct nfp_app *app, __be32 ipv4_addr)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_ipv4_route_entry *entry;
	struct list_head *ptr, *storage;

	spin_lock_bh(&priv->nfp_neigh_off_lock);
	list_for_each_safe(ptr, storage, &priv->nfp_neigh_off_list) {
		entry = list_entry(ptr, struct nfp_ipv4_route_entry, list);
		if (entry->ipv4_addr == ipv4_addr) {
			spin_unlock_bh(&priv->nfp_neigh_off_lock);
			return true;
		}
	}
	spin_unlock_bh(&priv->nfp_neigh_off_lock);
	return false;
}

static void nfp_tun_add_route_to_cache(struct nfp_app *app, __be32 ipv4_addr)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_ipv4_route_entry *entry;
	struct list_head *ptr, *storage;

	spin_lock_bh(&priv->nfp_neigh_off_lock);
	list_for_each_safe(ptr, storage, &priv->nfp_neigh_off_list) {
		entry = list_entry(ptr, struct nfp_ipv4_route_entry, list);
		if (entry->ipv4_addr == ipv4_addr) {
			spin_unlock_bh(&priv->nfp_neigh_off_lock);
			return;
		}
	}
	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry) {
		spin_unlock_bh(&priv->nfp_neigh_off_lock);
		nfp_flower_cmsg_warn(app, "Mem error when storing new route.\n");
		return;
	}

	entry->ipv4_addr = ipv4_addr;
	list_add_tail(&entry->list, &priv->nfp_neigh_off_list);
	spin_unlock_bh(&priv->nfp_neigh_off_lock);
}

static void nfp_tun_del_route_from_cache(struct nfp_app *app, __be32 ipv4_addr)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_ipv4_route_entry *entry;
	struct list_head *ptr, *storage;

	spin_lock_bh(&priv->nfp_neigh_off_lock);
	list_for_each_safe(ptr, storage, &priv->nfp_neigh_off_list) {
		entry = list_entry(ptr, struct nfp_ipv4_route_entry, list);
		if (entry->ipv4_addr == ipv4_addr) {
			list_del(&entry->list);
			kfree(entry);
			break;
		}
	}
	spin_unlock_bh(&priv->nfp_neigh_off_lock);
}

static void
nfp_tun_write_neigh(struct net_device *netdev, struct nfp_app *app,
		    struct flowi4 *flow, struct neighbour *neigh, gfp_t flag)
{
	struct nfp_tun_neigh payload;

	/* Only offload representor IPv4s for now. */
	if (!nfp_netdev_is_nfp_repr(netdev))
		return;

	memset(&payload, 0, sizeof(struct nfp_tun_neigh));
	payload.dst_ipv4 = flow->daddr;

	/* If entry has expired send dst IP with all other fields 0. */
	if (!(neigh->nud_state & NUD_VALID) || neigh->dead) {
		nfp_tun_del_route_from_cache(app, payload.dst_ipv4);
		/* Trigger ARP to verify invalid neighbour state. */
		neigh_event_send(neigh, NULL);
		goto send_msg;
	}

	/* Have a valid neighbour so populate rest of entry. */
	payload.src_ipv4 = flow->saddr;
	ether_addr_copy(payload.src_addr, netdev->dev_addr);
	neigh_ha_snapshot(payload.dst_addr, neigh, netdev);
	payload.port_id = cpu_to_be32(nfp_repr_get_port_id(netdev));
	/* Add destination of new route to NFP cache. */
	nfp_tun_add_route_to_cache(app, payload.dst_ipv4);

send_msg:
	nfp_flower_xmit_tun_conf(app, NFP_FLOWER_CMSG_TYPE_TUN_NEIGH,
				 sizeof(struct nfp_tun_neigh),
				 (unsigned char *)&payload, flag);
}

static int
nfp_tun_neigh_event_handler(struct notifier_block *nb, unsigned long event,
			    void *ptr)
{
	struct nfp_flower_priv *app_priv;
	struct netevent_redirect *redir;
	struct flowi4 flow = {};
	struct neighbour *n;
	struct nfp_app *app;
	struct rtable *rt;
	int err;

	switch (event) {
	case NETEVENT_REDIRECT:
		redir = (struct netevent_redirect *)ptr;
		n = redir->neigh;
		break;
	case NETEVENT_NEIGH_UPDATE:
		n = (struct neighbour *)ptr;
		break;
	default:
		return NOTIFY_DONE;
	}

	flow.daddr = *(__be32 *)n->primary_key;

	/* Only concerned with route changes for representors. */
	if (!nfp_netdev_is_nfp_repr(n->dev))
		return NOTIFY_DONE;

	app_priv = container_of(nb, struct nfp_flower_priv, nfp_tun_neigh_nb);
	app = app_priv->app;

	/* Only concerned with changes to routes already added to NFP. */
	if (!nfp_tun_has_route(app, flow.daddr))
		return NOTIFY_DONE;

#if IS_ENABLED(CONFIG_INET)
	/* Do a route lookup to populate flow data. */
	rt = ip_route_output_key(dev_net(n->dev), &flow);
	err = PTR_ERR_OR_ZERO(rt);
	if (err)
		return NOTIFY_DONE;

	ip_rt_put(rt);
#else
	return NOTIFY_DONE;
#endif

	flow.flowi4_proto = IPPROTO_UDP;
	nfp_tun_write_neigh(n->dev, app, &flow, n, GFP_ATOMIC);

	return NOTIFY_OK;
}

void nfp_tunnel_request_route(struct nfp_app *app, struct sk_buff *skb)
{
	struct nfp_tun_req_route_ipv4 *payload;
	struct net_device *netdev;
	struct flowi4 flow = {};
	struct neighbour *n;
	struct rtable *rt;
	int err;

	payload = nfp_flower_cmsg_get_data(skb);

	netdev = nfp_app_repr_get(app, be32_to_cpu(payload->ingress_port));
	if (!netdev)
		goto route_fail_warning;

	flow.daddr = payload->ipv4_addr;
	flow.flowi4_proto = IPPROTO_UDP;

#if IS_ENABLED(CONFIG_INET)
	/* Do a route lookup on same namespace as ingress port. */
	rt = ip_route_output_key(dev_net(netdev), &flow);
	err = PTR_ERR_OR_ZERO(rt);
	if (err)
		goto route_fail_warning;
#else
	goto route_fail_warning;
#endif

	/* Get the neighbour entry for the lookup */
	n = dst_neigh_lookup(&rt->dst, &flow.daddr);
	ip_rt_put(rt);
	if (!n)
		goto route_fail_warning;
	nfp_tun_write_neigh(n->dev, app, &flow, n, GFP_KERNEL);
	neigh_release(n);
	return;

route_fail_warning:
	nfp_flower_cmsg_warn(app, "Requested route not found.\n");
}

static void nfp_tun_write_ipv4_list(struct nfp_app *app)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_ipv4_addr_entry *entry;
	struct nfp_tun_ipv4_addr payload;
	struct list_head *ptr, *storage;
	int count;

	memset(&payload, 0, sizeof(struct nfp_tun_ipv4_addr));
	mutex_lock(&priv->nfp_ipv4_off_lock);
	count = 0;
	list_for_each_safe(ptr, storage, &priv->nfp_ipv4_off_list) {
		if (count >= NFP_FL_IPV4_ADDRS_MAX) {
			mutex_unlock(&priv->nfp_ipv4_off_lock);
			nfp_flower_cmsg_warn(app, "IPv4 offload exceeds limit.\n");
			return;
		}
		entry = list_entry(ptr, struct nfp_ipv4_addr_entry, list);
		payload.ipv4_addr[count++] = entry->ipv4_addr;
	}
	payload.count = cpu_to_be32(count);
	mutex_unlock(&priv->nfp_ipv4_off_lock);

	nfp_flower_xmit_tun_conf(app, NFP_FLOWER_CMSG_TYPE_TUN_IPS,
				 sizeof(struct nfp_tun_ipv4_addr),
				 &payload, GFP_KERNEL);
}

void nfp_tunnel_add_ipv4_off(struct nfp_app *app, __be32 ipv4)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_ipv4_addr_entry *entry;
	struct list_head *ptr, *storage;

	mutex_lock(&priv->nfp_ipv4_off_lock);
	list_for_each_safe(ptr, storage, &priv->nfp_ipv4_off_list) {
		entry = list_entry(ptr, struct nfp_ipv4_addr_entry, list);
		if (entry->ipv4_addr == ipv4) {
			entry->ref_count++;
			mutex_unlock(&priv->nfp_ipv4_off_lock);
			return;
		}
	}

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		mutex_unlock(&priv->nfp_ipv4_off_lock);
		nfp_flower_cmsg_warn(app, "Mem error when offloading IP address.\n");
		return;
	}
	entry->ipv4_addr = ipv4;
	entry->ref_count = 1;
	list_add_tail(&entry->list, &priv->nfp_ipv4_off_list);
	mutex_unlock(&priv->nfp_ipv4_off_lock);

	nfp_tun_write_ipv4_list(app);
}

void nfp_tunnel_del_ipv4_off(struct nfp_app *app, __be32 ipv4)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_ipv4_addr_entry *entry;
	struct list_head *ptr, *storage;

	mutex_lock(&priv->nfp_ipv4_off_lock);
	list_for_each_safe(ptr, storage, &priv->nfp_ipv4_off_list) {
		entry = list_entry(ptr, struct nfp_ipv4_addr_entry, list);
		if (entry->ipv4_addr == ipv4) {
			entry->ref_count--;
			if (!entry->ref_count) {
				list_del(&entry->list);
				kfree(entry);
			}
			break;
		}
	}
	mutex_unlock(&priv->nfp_ipv4_off_lock);

	nfp_tun_write_ipv4_list(app);
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
				       pay_size, payload, GFP_KERNEL);

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

	/* Initialise priv data for IPv4 offloading. */
	mutex_init(&priv->nfp_ipv4_off_lock);
	INIT_LIST_HEAD(&priv->nfp_ipv4_off_list);

	/* Initialise priv data for neighbour offloading. */
	spin_lock_init(&priv->nfp_neigh_off_lock);
	INIT_LIST_HEAD(&priv->nfp_neigh_off_list);
	priv->nfp_tun_neigh_nb.notifier_call = nfp_tun_neigh_event_handler;

	err = register_netdevice_notifier(&priv->nfp_tun_mac_nb);
	if (err)
		goto err_free_mac_ida;

	err = register_netevent_notifier(&priv->nfp_tun_neigh_nb);
	if (err)
		goto err_unreg_mac_nb;

	/* Parse netdevs already registered for MACs that need offloaded. */
	rtnl_lock();
	for_each_netdev(&init_net, netdev)
		nfp_tun_add_to_mac_offload_list(netdev, app);
	rtnl_unlock();

	return 0;

err_unreg_mac_nb:
	unregister_netdevice_notifier(&priv->nfp_tun_mac_nb);
err_free_mac_ida:
	ida_destroy(&priv->nfp_mac_off_ids);
	return err;
}

void nfp_tunnel_config_stop(struct nfp_app *app)
{
	struct nfp_tun_mac_offload_entry *mac_entry;
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_ipv4_route_entry *route_entry;
	struct nfp_tun_mac_non_nfp_idx *mac_idx;
	struct nfp_ipv4_addr_entry *ip_entry;
	struct list_head *ptr, *storage;

	unregister_netdevice_notifier(&priv->nfp_tun_mac_nb);
	unregister_netevent_notifier(&priv->nfp_tun_neigh_nb);

	/* Free any memory that may be occupied by MAC list. */
	list_for_each_safe(ptr, storage, &priv->nfp_mac_off_list) {
		mac_entry = list_entry(ptr, struct nfp_tun_mac_offload_entry,
				       list);
		list_del(&mac_entry->list);
		kfree(mac_entry);
	}

	/* Free any memory that may be occupied by MAC index list. */
	list_for_each_safe(ptr, storage, &priv->nfp_mac_index_list) {
		mac_idx = list_entry(ptr, struct nfp_tun_mac_non_nfp_idx,
				     list);
		list_del(&mac_idx->list);
		kfree(mac_idx);
	}

	ida_destroy(&priv->nfp_mac_off_ids);

	/* Free any memory that may be occupied by ipv4 list. */
	list_for_each_safe(ptr, storage, &priv->nfp_ipv4_off_list) {
		ip_entry = list_entry(ptr, struct nfp_ipv4_addr_entry, list);
		list_del(&ip_entry->list);
		kfree(ip_entry);
	}

	/* Free any memory that may be occupied by the route list. */
	list_for_each_safe(ptr, storage, &priv->nfp_neigh_off_list) {
		route_entry = list_entry(ptr, struct nfp_ipv4_route_entry,
					 list);
		list_del(&route_entry->list);
		kfree(route_entry);
	}
}
