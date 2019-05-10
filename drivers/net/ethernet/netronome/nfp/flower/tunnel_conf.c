// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#include <linux/etherdevice.h>
#include <linux/inetdevice.h>
#include <net/netevent.h>
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

#define NFP_TUN_MAC_OFFLOAD_DEL_FLAG	0x2

/**
 * struct nfp_tun_mac_addr_offload - configure MAC address of tunnel EP on NFP
 * @flags:	MAC address offload options
 * @count:	number of MAC addresses in the message (should be 1)
 * @index:	index of MAC address in the lookup table
 * @addr:	interface MAC address
 */
struct nfp_tun_mac_addr_offload {
	__be16 flags;
	__be16 count;
	__be16 index;
	u8 addr[ETH_ALEN];
};

enum nfp_flower_mac_offload_cmd {
	NFP_TUNNEL_MAC_OFFLOAD_ADD =		0,
	NFP_TUNNEL_MAC_OFFLOAD_DEL =		1,
	NFP_TUNNEL_MAC_OFFLOAD_MOD =		2,
};

#define NFP_MAX_MAC_INDEX       0xff

/**
 * struct nfp_tun_offloaded_mac - hashtable entry for an offloaded MAC
 * @ht_node:	Hashtable entry
 * @addr:	Offloaded MAC address
 * @index:	Offloaded index for given MAC address
 * @ref_count:	Number of devs using this MAC address
 * @repr_list:	List of reprs sharing this MAC address
 */
struct nfp_tun_offloaded_mac {
	struct rhash_head ht_node;
	u8 addr[ETH_ALEN];
	u16 index;
	int ref_count;
	struct list_head repr_list;
};

static const struct rhashtable_params offloaded_macs_params = {
	.key_offset	= offsetof(struct nfp_tun_offloaded_mac, addr),
	.head_offset	= offsetof(struct nfp_tun_offloaded_mac, ht_node),
	.key_len	= ETH_ALEN,
	.automatic_shrinking	= true,
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

	spin_lock_bh(&priv->tun.neigh_off_lock);
	list_for_each_safe(ptr, storage, &priv->tun.neigh_off_list) {
		entry = list_entry(ptr, struct nfp_ipv4_route_entry, list);
		if (entry->ipv4_addr == ipv4_addr) {
			spin_unlock_bh(&priv->tun.neigh_off_lock);
			return true;
		}
	}
	spin_unlock_bh(&priv->tun.neigh_off_lock);
	return false;
}

static void nfp_tun_add_route_to_cache(struct nfp_app *app, __be32 ipv4_addr)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_ipv4_route_entry *entry;
	struct list_head *ptr, *storage;

	spin_lock_bh(&priv->tun.neigh_off_lock);
	list_for_each_safe(ptr, storage, &priv->tun.neigh_off_list) {
		entry = list_entry(ptr, struct nfp_ipv4_route_entry, list);
		if (entry->ipv4_addr == ipv4_addr) {
			spin_unlock_bh(&priv->tun.neigh_off_lock);
			return;
		}
	}
	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry) {
		spin_unlock_bh(&priv->tun.neigh_off_lock);
		nfp_flower_cmsg_warn(app, "Mem error when storing new route.\n");
		return;
	}

	entry->ipv4_addr = ipv4_addr;
	list_add_tail(&entry->list, &priv->tun.neigh_off_list);
	spin_unlock_bh(&priv->tun.neigh_off_lock);
}

static void nfp_tun_del_route_from_cache(struct nfp_app *app, __be32 ipv4_addr)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_ipv4_route_entry *entry;
	struct list_head *ptr, *storage;

	spin_lock_bh(&priv->tun.neigh_off_lock);
	list_for_each_safe(ptr, storage, &priv->tun.neigh_off_list) {
		entry = list_entry(ptr, struct nfp_ipv4_route_entry, list);
		if (entry->ipv4_addr == ipv4_addr) {
			list_del(&entry->list);
			kfree(entry);
			break;
		}
	}
	spin_unlock_bh(&priv->tun.neigh_off_lock);
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

	app_priv = container_of(nb, struct nfp_flower_priv, tun.neigh_nb);
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
	mutex_lock(&priv->tun.ipv4_off_lock);
	count = 0;
	list_for_each_safe(ptr, storage, &priv->tun.ipv4_off_list) {
		if (count >= NFP_FL_IPV4_ADDRS_MAX) {
			mutex_unlock(&priv->tun.ipv4_off_lock);
			nfp_flower_cmsg_warn(app, "IPv4 offload exceeds limit.\n");
			return;
		}
		entry = list_entry(ptr, struct nfp_ipv4_addr_entry, list);
		payload.ipv4_addr[count++] = entry->ipv4_addr;
	}
	payload.count = cpu_to_be32(count);
	mutex_unlock(&priv->tun.ipv4_off_lock);

	nfp_flower_xmit_tun_conf(app, NFP_FLOWER_CMSG_TYPE_TUN_IPS,
				 sizeof(struct nfp_tun_ipv4_addr),
				 &payload, GFP_KERNEL);
}

void nfp_tunnel_add_ipv4_off(struct nfp_app *app, __be32 ipv4)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_ipv4_addr_entry *entry;
	struct list_head *ptr, *storage;

	mutex_lock(&priv->tun.ipv4_off_lock);
	list_for_each_safe(ptr, storage, &priv->tun.ipv4_off_list) {
		entry = list_entry(ptr, struct nfp_ipv4_addr_entry, list);
		if (entry->ipv4_addr == ipv4) {
			entry->ref_count++;
			mutex_unlock(&priv->tun.ipv4_off_lock);
			return;
		}
	}

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		mutex_unlock(&priv->tun.ipv4_off_lock);
		nfp_flower_cmsg_warn(app, "Mem error when offloading IP address.\n");
		return;
	}
	entry->ipv4_addr = ipv4;
	entry->ref_count = 1;
	list_add_tail(&entry->list, &priv->tun.ipv4_off_list);
	mutex_unlock(&priv->tun.ipv4_off_lock);

	nfp_tun_write_ipv4_list(app);
}

void nfp_tunnel_del_ipv4_off(struct nfp_app *app, __be32 ipv4)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_ipv4_addr_entry *entry;
	struct list_head *ptr, *storage;

	mutex_lock(&priv->tun.ipv4_off_lock);
	list_for_each_safe(ptr, storage, &priv->tun.ipv4_off_list) {
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
	mutex_unlock(&priv->tun.ipv4_off_lock);

	nfp_tun_write_ipv4_list(app);
}

static int
__nfp_tunnel_offload_mac(struct nfp_app *app, u8 *mac, u16 idx, bool del)
{
	struct nfp_tun_mac_addr_offload payload;

	memset(&payload, 0, sizeof(payload));

	if (del)
		payload.flags = cpu_to_be16(NFP_TUN_MAC_OFFLOAD_DEL_FLAG);

	/* FW supports multiple MACs per cmsg but restrict to single. */
	payload.count = cpu_to_be16(1);
	payload.index = cpu_to_be16(idx);
	ether_addr_copy(payload.addr, mac);

	return nfp_flower_xmit_tun_conf(app, NFP_FLOWER_CMSG_TYPE_TUN_MAC,
					sizeof(struct nfp_tun_mac_addr_offload),
					&payload, GFP_KERNEL);
}

static bool nfp_tunnel_port_is_phy_repr(int port)
{
	if (FIELD_GET(NFP_FLOWER_CMSG_PORT_TYPE, port) ==
	    NFP_FLOWER_CMSG_PORT_TYPE_PHYS_PORT)
		return true;

	return false;
}

static u16 nfp_tunnel_get_mac_idx_from_phy_port_id(int port)
{
	return port << 8 | NFP_FLOWER_CMSG_PORT_TYPE_PHYS_PORT;
}

static u16 nfp_tunnel_get_global_mac_idx_from_ida(int id)
{
	return id << 8 | NFP_FLOWER_CMSG_PORT_TYPE_OTHER_PORT;
}

static int nfp_tunnel_get_ida_from_global_mac_idx(u16 nfp_mac_idx)
{
	return nfp_mac_idx >> 8;
}

static bool nfp_tunnel_is_mac_idx_global(u16 nfp_mac_idx)
{
	return (nfp_mac_idx & 0xff) == NFP_FLOWER_CMSG_PORT_TYPE_OTHER_PORT;
}

static struct nfp_tun_offloaded_mac *
nfp_tunnel_lookup_offloaded_macs(struct nfp_app *app, u8 *mac)
{
	struct nfp_flower_priv *priv = app->priv;

	return rhashtable_lookup_fast(&priv->tun.offloaded_macs, mac,
				      offloaded_macs_params);
}

static void
nfp_tunnel_offloaded_macs_inc_ref_and_link(struct nfp_tun_offloaded_mac *entry,
					   struct net_device *netdev, bool mod)
{
	if (nfp_netdev_is_nfp_repr(netdev)) {
		struct nfp_flower_repr_priv *repr_priv;
		struct nfp_repr *repr;

		repr = netdev_priv(netdev);
		repr_priv = repr->app_priv;

		/* If modifing MAC, remove repr from old list first. */
		if (mod)
			list_del(&repr_priv->mac_list);

		list_add_tail(&repr_priv->mac_list, &entry->repr_list);
	}

	entry->ref_count++;
}

static int
nfp_tunnel_add_shared_mac(struct nfp_app *app, struct net_device *netdev,
			  int port, bool mod)
{
	struct nfp_flower_priv *priv = app->priv;
	int ida_idx = NFP_MAX_MAC_INDEX, err;
	struct nfp_tun_offloaded_mac *entry;
	u16 nfp_mac_idx = 0;

	entry = nfp_tunnel_lookup_offloaded_macs(app, netdev->dev_addr);
	if (entry && nfp_tunnel_is_mac_idx_global(entry->index)) {
		nfp_tunnel_offloaded_macs_inc_ref_and_link(entry, netdev, mod);
		return 0;
	}

	/* Assign a global index if non-repr or MAC address is now shared. */
	if (entry || !port) {
		ida_idx = ida_simple_get(&priv->tun.mac_off_ids, 0,
					 NFP_MAX_MAC_INDEX, GFP_KERNEL);
		if (ida_idx < 0)
			return ida_idx;

		nfp_mac_idx = nfp_tunnel_get_global_mac_idx_from_ida(ida_idx);
	} else {
		nfp_mac_idx = nfp_tunnel_get_mac_idx_from_phy_port_id(port);
	}

	if (!entry) {
		entry = kzalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry) {
			err = -ENOMEM;
			goto err_free_ida;
		}

		ether_addr_copy(entry->addr, netdev->dev_addr);
		INIT_LIST_HEAD(&entry->repr_list);

		if (rhashtable_insert_fast(&priv->tun.offloaded_macs,
					   &entry->ht_node,
					   offloaded_macs_params)) {
			err = -ENOMEM;
			goto err_free_entry;
		}
	}

	err = __nfp_tunnel_offload_mac(app, netdev->dev_addr,
				       nfp_mac_idx, false);
	if (err) {
		/* If not shared then free. */
		if (!entry->ref_count)
			goto err_remove_hash;
		goto err_free_ida;
	}

	entry->index = nfp_mac_idx;
	nfp_tunnel_offloaded_macs_inc_ref_and_link(entry, netdev, mod);

	return 0;

err_remove_hash:
	rhashtable_remove_fast(&priv->tun.offloaded_macs, &entry->ht_node,
			       offloaded_macs_params);
err_free_entry:
	kfree(entry);
err_free_ida:
	if (ida_idx != NFP_MAX_MAC_INDEX)
		ida_simple_remove(&priv->tun.mac_off_ids, ida_idx);

	return err;
}

static int
nfp_tunnel_del_shared_mac(struct nfp_app *app, struct net_device *netdev,
			  u8 *mac, bool mod)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_flower_repr_priv *repr_priv;
	struct nfp_tun_offloaded_mac *entry;
	struct nfp_repr *repr;
	int ida_idx;

	entry = nfp_tunnel_lookup_offloaded_macs(app, mac);
	if (!entry)
		return 0;

	entry->ref_count--;
	/* If del is part of a mod then mac_list is still in use elsewheree. */
	if (nfp_netdev_is_nfp_repr(netdev) && !mod) {
		repr = netdev_priv(netdev);
		repr_priv = repr->app_priv;
		list_del(&repr_priv->mac_list);
	}

	/* If MAC is now used by 1 repr set the offloaded MAC index to port. */
	if (entry->ref_count == 1 && list_is_singular(&entry->repr_list)) {
		u16 nfp_mac_idx;
		int port, err;

		repr_priv = list_first_entry(&entry->repr_list,
					     struct nfp_flower_repr_priv,
					     mac_list);
		repr = repr_priv->nfp_repr;
		port = nfp_repr_get_port_id(repr->netdev);
		nfp_mac_idx = nfp_tunnel_get_mac_idx_from_phy_port_id(port);
		err = __nfp_tunnel_offload_mac(app, mac, nfp_mac_idx, false);
		if (err) {
			nfp_flower_cmsg_warn(app, "MAC offload index revert failed on %s.\n",
					     netdev_name(netdev));
			return 0;
		}

		ida_idx = nfp_tunnel_get_ida_from_global_mac_idx(entry->index);
		ida_simple_remove(&priv->tun.mac_off_ids, ida_idx);
		entry->index = nfp_mac_idx;
		return 0;
	}

	if (entry->ref_count)
		return 0;

	WARN_ON_ONCE(rhashtable_remove_fast(&priv->tun.offloaded_macs,
					    &entry->ht_node,
					    offloaded_macs_params));
	/* If MAC has global ID then extract and free the ida entry. */
	if (nfp_tunnel_is_mac_idx_global(entry->index)) {
		ida_idx = nfp_tunnel_get_ida_from_global_mac_idx(entry->index);
		ida_simple_remove(&priv->tun.mac_off_ids, ida_idx);
	}

	kfree(entry);

	return __nfp_tunnel_offload_mac(app, mac, 0, true);
}

static int
nfp_tunnel_offload_mac(struct nfp_app *app, struct net_device *netdev,
		       enum nfp_flower_mac_offload_cmd cmd)
{
	struct nfp_flower_non_repr_priv *nr_priv = NULL;
	bool non_repr = false, *mac_offloaded;
	u8 *off_mac = NULL;
	int err, port = 0;

	if (nfp_netdev_is_nfp_repr(netdev)) {
		struct nfp_flower_repr_priv *repr_priv;
		struct nfp_repr *repr;

		repr = netdev_priv(netdev);
		if (repr->app != app)
			return 0;

		repr_priv = repr->app_priv;
		mac_offloaded = &repr_priv->mac_offloaded;
		off_mac = &repr_priv->offloaded_mac_addr[0];
		port = nfp_repr_get_port_id(netdev);
		if (!nfp_tunnel_port_is_phy_repr(port))
			return 0;
	} else if (nfp_fl_is_netdev_to_offload(netdev)) {
		nr_priv = nfp_flower_non_repr_priv_get(app, netdev);
		if (!nr_priv)
			return -ENOMEM;

		mac_offloaded = &nr_priv->mac_offloaded;
		off_mac = &nr_priv->offloaded_mac_addr[0];
		non_repr = true;
	} else {
		return 0;
	}

	if (!is_valid_ether_addr(netdev->dev_addr)) {
		err = -EINVAL;
		goto err_put_non_repr_priv;
	}

	if (cmd == NFP_TUNNEL_MAC_OFFLOAD_MOD && !*mac_offloaded)
		cmd = NFP_TUNNEL_MAC_OFFLOAD_ADD;

	switch (cmd) {
	case NFP_TUNNEL_MAC_OFFLOAD_ADD:
		err = nfp_tunnel_add_shared_mac(app, netdev, port, false);
		if (err)
			goto err_put_non_repr_priv;

		if (non_repr)
			__nfp_flower_non_repr_priv_get(nr_priv);

		*mac_offloaded = true;
		ether_addr_copy(off_mac, netdev->dev_addr);
		break;
	case NFP_TUNNEL_MAC_OFFLOAD_DEL:
		/* Only attempt delete if add was successful. */
		if (!*mac_offloaded)
			break;

		if (non_repr)
			__nfp_flower_non_repr_priv_put(nr_priv);

		*mac_offloaded = false;

		err = nfp_tunnel_del_shared_mac(app, netdev, netdev->dev_addr,
						false);
		if (err)
			goto err_put_non_repr_priv;

		break;
	case NFP_TUNNEL_MAC_OFFLOAD_MOD:
		/* Ignore if changing to the same address. */
		if (ether_addr_equal(netdev->dev_addr, off_mac))
			break;

		err = nfp_tunnel_add_shared_mac(app, netdev, port, true);
		if (err)
			goto err_put_non_repr_priv;

		/* Delete the previous MAC address. */
		err = nfp_tunnel_del_shared_mac(app, netdev, off_mac, true);
		if (err)
			nfp_flower_cmsg_warn(app, "Failed to remove offload of replaced MAC addr on %s.\n",
					     netdev_name(netdev));

		ether_addr_copy(off_mac, netdev->dev_addr);
		break;
	default:
		err = -EINVAL;
		goto err_put_non_repr_priv;
	}

	if (non_repr)
		__nfp_flower_non_repr_priv_put(nr_priv);

	return 0;

err_put_non_repr_priv:
	if (non_repr)
		__nfp_flower_non_repr_priv_put(nr_priv);

	return err;
}

int nfp_tunnel_mac_event_handler(struct nfp_app *app,
				 struct net_device *netdev,
				 unsigned long event, void *ptr)
{
	int err;

	if (event == NETDEV_DOWN) {
		err = nfp_tunnel_offload_mac(app, netdev,
					     NFP_TUNNEL_MAC_OFFLOAD_DEL);
		if (err)
			nfp_flower_cmsg_warn(app, "Failed to delete offload MAC on %s.\n",
					     netdev_name(netdev));
	} else if (event == NETDEV_UP) {
		err = nfp_tunnel_offload_mac(app, netdev,
					     NFP_TUNNEL_MAC_OFFLOAD_ADD);
		if (err)
			nfp_flower_cmsg_warn(app, "Failed to offload MAC on %s.\n",
					     netdev_name(netdev));
	} else if (event == NETDEV_CHANGEADDR) {
		/* Only offload addr change if netdev is already up. */
		if (!(netdev->flags & IFF_UP))
			return NOTIFY_OK;

		err = nfp_tunnel_offload_mac(app, netdev,
					     NFP_TUNNEL_MAC_OFFLOAD_MOD);
		if (err)
			nfp_flower_cmsg_warn(app, "Failed to offload MAC change on %s.\n",
					     netdev_name(netdev));
	}
	return NOTIFY_OK;
}

int nfp_tunnel_config_start(struct nfp_app *app)
{
	struct nfp_flower_priv *priv = app->priv;
	int err;

	/* Initialise rhash for MAC offload tracking. */
	err = rhashtable_init(&priv->tun.offloaded_macs,
			      &offloaded_macs_params);
	if (err)
		return err;

	ida_init(&priv->tun.mac_off_ids);

	/* Initialise priv data for IPv4 offloading. */
	mutex_init(&priv->tun.ipv4_off_lock);
	INIT_LIST_HEAD(&priv->tun.ipv4_off_list);

	/* Initialise priv data for neighbour offloading. */
	spin_lock_init(&priv->tun.neigh_off_lock);
	INIT_LIST_HEAD(&priv->tun.neigh_off_list);
	priv->tun.neigh_nb.notifier_call = nfp_tun_neigh_event_handler;

	err = register_netevent_notifier(&priv->tun.neigh_nb);
	if (err) {
		rhashtable_free_and_destroy(&priv->tun.offloaded_macs,
					    nfp_check_rhashtable_empty, NULL);
		return err;
	}

	return 0;
}

void nfp_tunnel_config_stop(struct nfp_app *app)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_ipv4_route_entry *route_entry;
	struct nfp_ipv4_addr_entry *ip_entry;
	struct list_head *ptr, *storage;

	unregister_netevent_notifier(&priv->tun.neigh_nb);

	ida_destroy(&priv->tun.mac_off_ids);

	/* Free any memory that may be occupied by ipv4 list. */
	list_for_each_safe(ptr, storage, &priv->tun.ipv4_off_list) {
		ip_entry = list_entry(ptr, struct nfp_ipv4_addr_entry, list);
		list_del(&ip_entry->list);
		kfree(ip_entry);
	}

	/* Free any memory that may be occupied by the route list. */
	list_for_each_safe(ptr, storage, &priv->tun.neigh_off_list) {
		route_entry = list_entry(ptr, struct nfp_ipv4_route_entry,
					 list);
		list_del(&route_entry->list);
		kfree(route_entry);
	}

	/* Destroy rhash. Entries should be cleaned on netdev notifier unreg. */
	rhashtable_free_and_destroy(&priv->tun.offloaded_macs,
				    nfp_check_rhashtable_empty, NULL);
}
