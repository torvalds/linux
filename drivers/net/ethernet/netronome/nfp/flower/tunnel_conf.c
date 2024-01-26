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

#define NFP_TUN_PRE_TUN_RULE_LIMIT	32
#define NFP_TUN_PRE_TUN_RULE_DEL	BIT(0)
#define NFP_TUN_PRE_TUN_IDX_BIT		BIT(3)
#define NFP_TUN_PRE_TUN_IPV6_BIT	BIT(7)

/**
 * struct nfp_tun_pre_tun_rule - rule matched before decap
 * @flags:		options for the rule offset
 * @port_idx:		index of destination MAC address for the rule
 * @vlan_tci:		VLAN info associated with MAC
 * @host_ctx_id:	stats context of rule to update
 */
struct nfp_tun_pre_tun_rule {
	__be32 flags;
	__be16 port_idx;
	__be16 vlan_tci;
	__be32 host_ctx_id;
};

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
 * struct nfp_tun_active_tuns_v6 - periodic message of active IPv6 tunnels
 * @seq:		sequence number of the message
 * @count:		number of tunnels report in message
 * @flags:		options part of the request
 * @tun_info.ipv6:		dest IPv6 address of active route
 * @tun_info.egress_port:	port the encapsulated packet egressed
 * @tun_info.extra:		reserved for future use
 * @tun_info:		tunnels that have sent traffic in reported period
 */
struct nfp_tun_active_tuns_v6 {
	__be32 seq;
	__be32 count;
	__be32 flags;
	struct route_ip_info_v6 {
		struct in6_addr ipv6;
		__be32 egress_port;
		__be32 extra[2];
	} tun_info[];
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
 * struct nfp_tun_req_route_ipv6 - NFP requests an IPv6 route/neighbour lookup
 * @ingress_port:	ingress port of packet that signalled request
 * @ipv6_addr:		destination ipv6 address for route
 */
struct nfp_tun_req_route_ipv6 {
	__be32 ingress_port;
	struct in6_addr ipv6_addr;
};

/**
 * struct nfp_offloaded_route - routes that are offloaded to the NFP
 * @list:	list pointer
 * @ip_add:	destination of route - can be IPv4 or IPv6
 */
struct nfp_offloaded_route {
	struct list_head list;
	u8 ip_add[];
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

#define NFP_FL_IPV6_ADDRS_MAX        4

/**
 * struct nfp_tun_ipv6_addr - set the IP address list on the NFP
 * @count:	number of IPs populated in the array
 * @ipv6_addr:	array of IPV6_ADDRS_MAX 128 bit IPv6 addresses
 */
struct nfp_tun_ipv6_addr {
	__be32 count;
	struct in6_addr ipv6_addr[NFP_FL_IPV6_ADDRS_MAX];
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

/**
 * struct nfp_neigh_update_work - update neighbour information to nfp
 * @work:	Work queue for writing neigh to the nfp
 * @n:		neighbour entry
 * @app:	Back pointer to app
 */
struct nfp_neigh_update_work {
	struct work_struct work;
	struct neighbour *n;
	struct nfp_app *app;
};

enum nfp_flower_mac_offload_cmd {
	NFP_TUNNEL_MAC_OFFLOAD_ADD =		0,
	NFP_TUNNEL_MAC_OFFLOAD_DEL =		1,
	NFP_TUNNEL_MAC_OFFLOAD_MOD =		2,
};

#define NFP_MAX_MAC_INDEX       0xff

/**
 * struct nfp_tun_offloaded_mac - hashtable entry for an offloaded MAC
 * @ht_node:		Hashtable entry
 * @addr:		Offloaded MAC address
 * @index:		Offloaded index for given MAC address
 * @ref_count:		Number of devs using this MAC address
 * @repr_list:		List of reprs sharing this MAC address
 * @bridge_count:	Number of bridge/internal devs with MAC
 */
struct nfp_tun_offloaded_mac {
	struct rhash_head ht_node;
	u8 addr[ETH_ALEN];
	u16 index;
	int ref_count;
	struct list_head repr_list;
	int bridge_count;
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
	if (pay_len != struct_size(payload, tun_info, count)) {
		nfp_flower_cmsg_warn(app, "Corruption in tunnel keep-alive message.\n");
		return;
	}

	rcu_read_lock();
	for (i = 0; i < count; i++) {
		ipv4_addr = payload->tun_info[i].ipv4;
		port = be32_to_cpu(payload->tun_info[i].egress_port);
		netdev = nfp_app_dev_get(app, port, NULL);
		if (!netdev)
			continue;

		n = neigh_lookup(&arp_tbl, &ipv4_addr, netdev);
		if (!n)
			continue;

		/* Update the used timestamp of neighbour */
		neigh_event_send(n, NULL);
		neigh_release(n);
	}
	rcu_read_unlock();
}

void nfp_tunnel_keep_alive_v6(struct nfp_app *app, struct sk_buff *skb)
{
#if IS_ENABLED(CONFIG_IPV6)
	struct nfp_tun_active_tuns_v6 *payload;
	struct net_device *netdev;
	int count, i, pay_len;
	struct neighbour *n;
	void *ipv6_add;
	u32 port;

	payload = nfp_flower_cmsg_get_data(skb);
	count = be32_to_cpu(payload->count);
	if (count > NFP_FL_IPV6_ADDRS_MAX) {
		nfp_flower_cmsg_warn(app, "IPv6 tunnel keep-alive request exceeds max routes.\n");
		return;
	}

	pay_len = nfp_flower_cmsg_get_data_len(skb);
	if (pay_len != struct_size(payload, tun_info, count)) {
		nfp_flower_cmsg_warn(app, "Corruption in tunnel keep-alive message.\n");
		return;
	}

	rcu_read_lock();
	for (i = 0; i < count; i++) {
		ipv6_add = &payload->tun_info[i].ipv6;
		port = be32_to_cpu(payload->tun_info[i].egress_port);
		netdev = nfp_app_dev_get(app, port, NULL);
		if (!netdev)
			continue;

		n = neigh_lookup(&nd_tbl, ipv6_add, netdev);
		if (!n)
			continue;

		/* Update the used timestamp of neighbour */
		neigh_event_send(n, NULL);
		neigh_release(n);
	}
	rcu_read_unlock();
#endif
}

static int
nfp_flower_xmit_tun_conf(struct nfp_app *app, u8 mtype, u16 plen, void *pdata,
			 gfp_t flag)
{
	struct nfp_flower_priv *priv = app->priv;
	struct sk_buff *skb;
	unsigned char *msg;

	if (!(priv->flower_ext_feats & NFP_FL_FEATS_DECAP_V2) &&
	    (mtype == NFP_FLOWER_CMSG_TYPE_TUN_NEIGH ||
	     mtype == NFP_FLOWER_CMSG_TYPE_TUN_NEIGH_V6))
		plen -= sizeof(struct nfp_tun_neigh_ext);

	if (!(priv->flower_ext_feats & NFP_FL_FEATS_TUNNEL_NEIGH_LAG) &&
	    (mtype == NFP_FLOWER_CMSG_TYPE_TUN_NEIGH ||
	     mtype == NFP_FLOWER_CMSG_TYPE_TUN_NEIGH_V6))
		plen -= sizeof(struct nfp_tun_neigh_lag);

	skb = nfp_flower_cmsg_alloc(app, plen, mtype, flag);
	if (!skb)
		return -ENOMEM;

	msg = nfp_flower_cmsg_get_data(skb);
	memcpy(msg, pdata, nfp_flower_cmsg_get_data_len(skb));

	nfp_ctrl_tx(app->ctrl, skb);
	return 0;
}

static void
nfp_tun_mutual_link(struct nfp_predt_entry *predt,
		    struct nfp_neigh_entry *neigh)
{
	struct nfp_fl_payload *flow_pay = predt->flow_pay;
	struct nfp_tun_neigh_ext *ext;
	struct nfp_tun_neigh *common;

	if (flow_pay->pre_tun_rule.is_ipv6 != neigh->is_ipv6)
		return;

	/* In the case of bonding it is possible that there might already
	 * be a flow linked (as the MAC address gets shared). If a flow
	 * is already linked just return.
	 */
	if (neigh->flow)
		return;

	common = neigh->is_ipv6 ?
		 &((struct nfp_tun_neigh_v6 *)neigh->payload)->common :
		 &((struct nfp_tun_neigh_v4 *)neigh->payload)->common;
	ext = neigh->is_ipv6 ?
		 &((struct nfp_tun_neigh_v6 *)neigh->payload)->ext :
		 &((struct nfp_tun_neigh_v4 *)neigh->payload)->ext;

	if (memcmp(flow_pay->pre_tun_rule.loc_mac,
		   common->src_addr, ETH_ALEN) ||
	    memcmp(flow_pay->pre_tun_rule.rem_mac,
		   common->dst_addr, ETH_ALEN))
		return;

	list_add(&neigh->list_head, &predt->nn_list);
	neigh->flow = predt;
	ext->host_ctx = flow_pay->meta.host_ctx_id;
	ext->vlan_tci = flow_pay->pre_tun_rule.vlan_tci;
	ext->vlan_tpid = flow_pay->pre_tun_rule.vlan_tpid;
}

static void
nfp_tun_link_predt_entries(struct nfp_app *app,
			   struct nfp_neigh_entry *nn_entry)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_predt_entry *predt, *tmp;

	list_for_each_entry_safe(predt, tmp, &priv->predt_list, list_head) {
		nfp_tun_mutual_link(predt, nn_entry);
	}
}

void nfp_tun_link_and_update_nn_entries(struct nfp_app *app,
					struct nfp_predt_entry *predt)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_neigh_entry *nn_entry;
	struct rhashtable_iter iter;
	size_t neigh_size;
	u8 type;

	rhashtable_walk_enter(&priv->neigh_table, &iter);
	rhashtable_walk_start(&iter);
	while ((nn_entry = rhashtable_walk_next(&iter)) != NULL) {
		if (IS_ERR(nn_entry))
			continue;
		nfp_tun_mutual_link(predt, nn_entry);
		neigh_size = nn_entry->is_ipv6 ?
			     sizeof(struct nfp_tun_neigh_v6) :
			     sizeof(struct nfp_tun_neigh_v4);
		type = nn_entry->is_ipv6 ? NFP_FLOWER_CMSG_TYPE_TUN_NEIGH_V6 :
					   NFP_FLOWER_CMSG_TYPE_TUN_NEIGH;
		nfp_flower_xmit_tun_conf(app, type, neigh_size,
					 nn_entry->payload,
					 GFP_ATOMIC);
	}
	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);
}

static void nfp_tun_cleanup_nn_entries(struct nfp_app *app)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_neigh_entry *neigh;
	struct nfp_tun_neigh_ext *ext;
	struct rhashtable_iter iter;
	size_t neigh_size;
	u8 type;

	rhashtable_walk_enter(&priv->neigh_table, &iter);
	rhashtable_walk_start(&iter);
	while ((neigh = rhashtable_walk_next(&iter)) != NULL) {
		if (IS_ERR(neigh))
			continue;
		ext = neigh->is_ipv6 ?
			 &((struct nfp_tun_neigh_v6 *)neigh->payload)->ext :
			 &((struct nfp_tun_neigh_v4 *)neigh->payload)->ext;
		ext->host_ctx = cpu_to_be32(U32_MAX);
		ext->vlan_tpid = cpu_to_be16(U16_MAX);
		ext->vlan_tci = cpu_to_be16(U16_MAX);

		neigh_size = neigh->is_ipv6 ?
			     sizeof(struct nfp_tun_neigh_v6) :
			     sizeof(struct nfp_tun_neigh_v4);
		type = neigh->is_ipv6 ? NFP_FLOWER_CMSG_TYPE_TUN_NEIGH_V6 :
					   NFP_FLOWER_CMSG_TYPE_TUN_NEIGH;
		nfp_flower_xmit_tun_conf(app, type, neigh_size, neigh->payload,
					 GFP_ATOMIC);

		rhashtable_remove_fast(&priv->neigh_table, &neigh->ht_node,
				       neigh_table_params);
		if (neigh->flow)
			list_del(&neigh->list_head);
		kfree(neigh);
	}
	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);
}

void nfp_tun_unlink_and_update_nn_entries(struct nfp_app *app,
					  struct nfp_predt_entry *predt)
{
	struct nfp_neigh_entry *neigh, *tmp;
	struct nfp_tun_neigh_ext *ext;
	size_t neigh_size;
	u8 type;

	list_for_each_entry_safe(neigh, tmp, &predt->nn_list, list_head) {
		ext = neigh->is_ipv6 ?
			 &((struct nfp_tun_neigh_v6 *)neigh->payload)->ext :
			 &((struct nfp_tun_neigh_v4 *)neigh->payload)->ext;
		neigh->flow = NULL;
		ext->host_ctx = cpu_to_be32(U32_MAX);
		ext->vlan_tpid = cpu_to_be16(U16_MAX);
		ext->vlan_tci = cpu_to_be16(U16_MAX);
		list_del(&neigh->list_head);
		neigh_size = neigh->is_ipv6 ?
			     sizeof(struct nfp_tun_neigh_v6) :
			     sizeof(struct nfp_tun_neigh_v4);
		type = neigh->is_ipv6 ? NFP_FLOWER_CMSG_TYPE_TUN_NEIGH_V6 :
					   NFP_FLOWER_CMSG_TYPE_TUN_NEIGH;
		nfp_flower_xmit_tun_conf(app, type, neigh_size, neigh->payload,
					 GFP_ATOMIC);
	}
}

static void
nfp_tun_write_neigh(struct net_device *netdev, struct nfp_app *app,
		    void *flow, struct neighbour *neigh, bool is_ipv6,
		    bool override)
{
	bool neigh_invalid = !(neigh->nud_state & NUD_VALID) || neigh->dead;
	size_t neigh_size = is_ipv6 ? sizeof(struct nfp_tun_neigh_v6) :
			    sizeof(struct nfp_tun_neigh_v4);
	unsigned long cookie = (unsigned long)neigh;
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_tun_neigh_lag lag_info;
	struct nfp_neigh_entry *nn_entry;
	u32 port_id;
	u8 mtype;

	port_id = nfp_flower_get_port_id_from_netdev(app, netdev);
	if (!port_id)
		return;

	if ((port_id & NFP_FL_LAG_OUT) == NFP_FL_LAG_OUT) {
		memset(&lag_info, 0, sizeof(struct nfp_tun_neigh_lag));
		nfp_flower_lag_get_info_from_netdev(app, netdev, &lag_info);
	}

	spin_lock_bh(&priv->predt_lock);
	nn_entry = rhashtable_lookup_fast(&priv->neigh_table, &cookie,
					  neigh_table_params);
	if (!nn_entry && !neigh_invalid) {
		struct nfp_tun_neigh_ext *ext;
		struct nfp_tun_neigh_lag *lag;
		struct nfp_tun_neigh *common;

		nn_entry = kzalloc(sizeof(*nn_entry) + neigh_size,
				   GFP_ATOMIC);
		if (!nn_entry)
			goto err;

		nn_entry->payload = (char *)&nn_entry[1];
		nn_entry->neigh_cookie = cookie;
		nn_entry->is_ipv6 = is_ipv6;
		nn_entry->flow = NULL;
		if (is_ipv6) {
			struct flowi6 *flowi6 = (struct flowi6 *)flow;
			struct nfp_tun_neigh_v6 *payload;

			payload = (struct nfp_tun_neigh_v6 *)nn_entry->payload;
			payload->src_ipv6 = flowi6->saddr;
			payload->dst_ipv6 = flowi6->daddr;
			common = &payload->common;
			ext = &payload->ext;
			lag = &payload->lag;
			mtype = NFP_FLOWER_CMSG_TYPE_TUN_NEIGH_V6;
		} else {
			struct flowi4 *flowi4 = (struct flowi4 *)flow;
			struct nfp_tun_neigh_v4 *payload;

			payload = (struct nfp_tun_neigh_v4 *)nn_entry->payload;
			payload->src_ipv4 = flowi4->saddr;
			payload->dst_ipv4 = flowi4->daddr;
			common = &payload->common;
			ext = &payload->ext;
			lag = &payload->lag;
			mtype = NFP_FLOWER_CMSG_TYPE_TUN_NEIGH;
		}
		ext->host_ctx = cpu_to_be32(U32_MAX);
		ext->vlan_tpid = cpu_to_be16(U16_MAX);
		ext->vlan_tci = cpu_to_be16(U16_MAX);
		ether_addr_copy(common->src_addr, netdev->dev_addr);
		neigh_ha_snapshot(common->dst_addr, neigh, netdev);

		if ((port_id & NFP_FL_LAG_OUT) == NFP_FL_LAG_OUT)
			memcpy(lag, &lag_info, sizeof(struct nfp_tun_neigh_lag));
		common->port_id = cpu_to_be32(port_id);

		if (rhashtable_insert_fast(&priv->neigh_table,
					   &nn_entry->ht_node,
					   neigh_table_params))
			goto err;

		nfp_tun_link_predt_entries(app, nn_entry);
		nfp_flower_xmit_tun_conf(app, mtype, neigh_size,
					 nn_entry->payload,
					 GFP_ATOMIC);
	} else if (nn_entry && neigh_invalid) {
		if (is_ipv6) {
			struct flowi6 *flowi6 = (struct flowi6 *)flow;
			struct nfp_tun_neigh_v6 *payload;

			payload = (struct nfp_tun_neigh_v6 *)nn_entry->payload;
			memset(payload, 0, sizeof(struct nfp_tun_neigh_v6));
			payload->dst_ipv6 = flowi6->daddr;
			mtype = NFP_FLOWER_CMSG_TYPE_TUN_NEIGH_V6;
		} else {
			struct flowi4 *flowi4 = (struct flowi4 *)flow;
			struct nfp_tun_neigh_v4 *payload;

			payload = (struct nfp_tun_neigh_v4 *)nn_entry->payload;
			memset(payload, 0, sizeof(struct nfp_tun_neigh_v4));
			payload->dst_ipv4 = flowi4->daddr;
			mtype = NFP_FLOWER_CMSG_TYPE_TUN_NEIGH;
		}
		/* Trigger ARP to verify invalid neighbour state. */
		neigh_event_send(neigh, NULL);
		rhashtable_remove_fast(&priv->neigh_table,
				       &nn_entry->ht_node,
				       neigh_table_params);

		nfp_flower_xmit_tun_conf(app, mtype, neigh_size,
					 nn_entry->payload,
					 GFP_ATOMIC);

		if (nn_entry->flow)
			list_del(&nn_entry->list_head);
		kfree(nn_entry);
	} else if (nn_entry && !neigh_invalid) {
		struct nfp_tun_neigh *common;
		u8 dst_addr[ETH_ALEN];
		bool is_mac_change;

		if (is_ipv6) {
			struct nfp_tun_neigh_v6 *payload;

			payload = (struct nfp_tun_neigh_v6 *)nn_entry->payload;
			common = &payload->common;
			mtype = NFP_FLOWER_CMSG_TYPE_TUN_NEIGH_V6;
		} else {
			struct nfp_tun_neigh_v4 *payload;

			payload = (struct nfp_tun_neigh_v4 *)nn_entry->payload;
			common = &payload->common;
			mtype = NFP_FLOWER_CMSG_TYPE_TUN_NEIGH;
		}

		ether_addr_copy(dst_addr, common->dst_addr);
		neigh_ha_snapshot(common->dst_addr, neigh, netdev);
		is_mac_change = !ether_addr_equal(dst_addr, common->dst_addr);
		if (override || is_mac_change) {
			if (is_mac_change && nn_entry->flow) {
				list_del(&nn_entry->list_head);
				nn_entry->flow = NULL;
			}
			nfp_tun_link_predt_entries(app, nn_entry);
			nfp_flower_xmit_tun_conf(app, mtype, neigh_size,
						 nn_entry->payload,
						 GFP_ATOMIC);
		}
	}

	spin_unlock_bh(&priv->predt_lock);
	return;

err:
	kfree(nn_entry);
	spin_unlock_bh(&priv->predt_lock);
	nfp_flower_cmsg_warn(app, "Neighbour configuration failed.\n");
}

static void
nfp_tun_release_neigh_update_work(struct nfp_neigh_update_work *update_work)
{
	neigh_release(update_work->n);
	kfree(update_work);
}

static void nfp_tun_neigh_update(struct work_struct *work)
{
	struct nfp_neigh_update_work *update_work;
	struct nfp_app *app;
	struct neighbour *n;
	bool neigh_invalid;
	int err;

	update_work = container_of(work, struct nfp_neigh_update_work, work);
	app = update_work->app;
	n = update_work->n;

	if (!nfp_flower_get_port_id_from_netdev(app, n->dev))
		goto out;

#if IS_ENABLED(CONFIG_INET)
	neigh_invalid = !(n->nud_state & NUD_VALID) || n->dead;
	if (n->tbl->family == AF_INET6) {
#if IS_ENABLED(CONFIG_IPV6)
		struct flowi6 flow6 = {};

		flow6.daddr = *(struct in6_addr *)n->primary_key;
		if (!neigh_invalid) {
			struct dst_entry *dst;
			/* Use ipv6_dst_lookup_flow to populate flow6->saddr
			 * and other fields. This information is only needed
			 * for new entries, lookup can be skipped when an entry
			 * gets invalidated - as only the daddr is needed for
			 * deleting.
			 */
			dst = ip6_dst_lookup_flow(dev_net(n->dev), NULL,
						  &flow6, NULL);
			if (IS_ERR(dst))
				goto out;

			dst_release(dst);
		}
		nfp_tun_write_neigh(n->dev, app, &flow6, n, true, false);
#endif /* CONFIG_IPV6 */
	} else {
		struct flowi4 flow4 = {};

		flow4.daddr = *(__be32 *)n->primary_key;
		if (!neigh_invalid) {
			struct rtable *rt;
			/* Use ip_route_output_key to populate flow4->saddr and
			 * other fields. This information is only needed for
			 * new entries, lookup can be skipped when an entry
			 * gets invalidated - as only the daddr is needed for
			 * deleting.
			 */
			rt = ip_route_output_key(dev_net(n->dev), &flow4);
			err = PTR_ERR_OR_ZERO(rt);
			if (err)
				goto out;

			ip_rt_put(rt);
		}
		nfp_tun_write_neigh(n->dev, app, &flow4, n, false, false);
	}
#endif /* CONFIG_INET */
out:
	nfp_tun_release_neigh_update_work(update_work);
}

static struct nfp_neigh_update_work *
nfp_tun_alloc_neigh_update_work(struct nfp_app *app, struct neighbour *n)
{
	struct nfp_neigh_update_work *update_work;

	update_work = kzalloc(sizeof(*update_work), GFP_ATOMIC);
	if (!update_work)
		return NULL;

	INIT_WORK(&update_work->work, nfp_tun_neigh_update);
	neigh_hold(n);
	update_work->n = n;
	update_work->app = app;

	return update_work;
}

static int
nfp_tun_neigh_event_handler(struct notifier_block *nb, unsigned long event,
			    void *ptr)
{
	struct nfp_neigh_update_work *update_work;
	struct nfp_flower_priv *app_priv;
	struct netevent_redirect *redir;
	struct neighbour *n;
	struct nfp_app *app;

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
#if IS_ENABLED(CONFIG_IPV6)
	if (n->tbl != ipv6_stub->nd_tbl && n->tbl != &arp_tbl)
#else
	if (n->tbl != &arp_tbl)
#endif
		return NOTIFY_DONE;

	app_priv = container_of(nb, struct nfp_flower_priv, tun.neigh_nb);
	app = app_priv->app;
	update_work = nfp_tun_alloc_neigh_update_work(app, n);
	if (!update_work)
		return NOTIFY_DONE;

	queue_work(system_highpri_wq, &update_work->work);

	return NOTIFY_DONE;
}

void nfp_tunnel_request_route_v4(struct nfp_app *app, struct sk_buff *skb)
{
	struct nfp_tun_req_route_ipv4 *payload;
	struct net_device *netdev;
	struct flowi4 flow = {};
	struct neighbour *n;
	struct rtable *rt;
	int err;

	payload = nfp_flower_cmsg_get_data(skb);

	rcu_read_lock();
	netdev = nfp_app_dev_get(app, be32_to_cpu(payload->ingress_port), NULL);
	if (!netdev)
		goto fail_rcu_unlock;
	dev_hold(netdev);

	flow.daddr = payload->ipv4_addr;
	flow.flowi4_proto = IPPROTO_UDP;

#if IS_ENABLED(CONFIG_INET)
	/* Do a route lookup on same namespace as ingress port. */
	rt = ip_route_output_key(dev_net(netdev), &flow);
	err = PTR_ERR_OR_ZERO(rt);
	if (err)
		goto fail_rcu_unlock;
#else
	goto fail_rcu_unlock;
#endif

	/* Get the neighbour entry for the lookup */
	n = dst_neigh_lookup(&rt->dst, &flow.daddr);
	ip_rt_put(rt);
	if (!n)
		goto fail_rcu_unlock;
	rcu_read_unlock();

	nfp_tun_write_neigh(n->dev, app, &flow, n, false, true);
	neigh_release(n);
	dev_put(netdev);
	return;

fail_rcu_unlock:
	rcu_read_unlock();
	dev_put(netdev);
	nfp_flower_cmsg_warn(app, "Requested route not found.\n");
}

void nfp_tunnel_request_route_v6(struct nfp_app *app, struct sk_buff *skb)
{
	struct nfp_tun_req_route_ipv6 *payload;
	struct net_device *netdev;
	struct flowi6 flow = {};
	struct dst_entry *dst;
	struct neighbour *n;

	payload = nfp_flower_cmsg_get_data(skb);

	rcu_read_lock();
	netdev = nfp_app_dev_get(app, be32_to_cpu(payload->ingress_port), NULL);
	if (!netdev)
		goto fail_rcu_unlock;
	dev_hold(netdev);

	flow.daddr = payload->ipv6_addr;
	flow.flowi6_proto = IPPROTO_UDP;

#if IS_ENABLED(CONFIG_INET) && IS_ENABLED(CONFIG_IPV6)
	dst = ipv6_stub->ipv6_dst_lookup_flow(dev_net(netdev), NULL, &flow,
					      NULL);
	if (IS_ERR(dst))
		goto fail_rcu_unlock;
#else
	goto fail_rcu_unlock;
#endif

	n = dst_neigh_lookup(dst, &flow.daddr);
	dst_release(dst);
	if (!n)
		goto fail_rcu_unlock;
	rcu_read_unlock();

	nfp_tun_write_neigh(n->dev, app, &flow, n, true, true);
	neigh_release(n);
	dev_put(netdev);
	return;

fail_rcu_unlock:
	rcu_read_unlock();
	dev_put(netdev);
	nfp_flower_cmsg_warn(app, "Requested IPv6 route not found.\n");
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

static void nfp_tun_write_ipv6_list(struct nfp_app *app)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_ipv6_addr_entry *entry;
	struct nfp_tun_ipv6_addr payload;
	int count = 0;

	memset(&payload, 0, sizeof(struct nfp_tun_ipv6_addr));
	mutex_lock(&priv->tun.ipv6_off_lock);
	list_for_each_entry(entry, &priv->tun.ipv6_off_list, list) {
		if (count >= NFP_FL_IPV6_ADDRS_MAX) {
			nfp_flower_cmsg_warn(app, "Too many IPv6 tunnel endpoint addresses, some cannot be offloaded.\n");
			break;
		}
		payload.ipv6_addr[count++] = entry->ipv6_addr;
	}
	mutex_unlock(&priv->tun.ipv6_off_lock);
	payload.count = cpu_to_be32(count);

	nfp_flower_xmit_tun_conf(app, NFP_FLOWER_CMSG_TYPE_TUN_IPS_V6,
				 sizeof(struct nfp_tun_ipv6_addr),
				 &payload, GFP_KERNEL);
}

struct nfp_ipv6_addr_entry *
nfp_tunnel_add_ipv6_off(struct nfp_app *app, struct in6_addr *ipv6)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_ipv6_addr_entry *entry;

	mutex_lock(&priv->tun.ipv6_off_lock);
	list_for_each_entry(entry, &priv->tun.ipv6_off_list, list)
		if (!memcmp(&entry->ipv6_addr, ipv6, sizeof(*ipv6))) {
			entry->ref_count++;
			mutex_unlock(&priv->tun.ipv6_off_lock);
			return entry;
		}

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		mutex_unlock(&priv->tun.ipv6_off_lock);
		nfp_flower_cmsg_warn(app, "Mem error when offloading IP address.\n");
		return NULL;
	}
	entry->ipv6_addr = *ipv6;
	entry->ref_count = 1;
	list_add_tail(&entry->list, &priv->tun.ipv6_off_list);
	mutex_unlock(&priv->tun.ipv6_off_lock);

	nfp_tun_write_ipv6_list(app);

	return entry;
}

void
nfp_tunnel_put_ipv6_off(struct nfp_app *app, struct nfp_ipv6_addr_entry *entry)
{
	struct nfp_flower_priv *priv = app->priv;
	bool freed = false;

	mutex_lock(&priv->tun.ipv6_off_lock);
	if (!--entry->ref_count) {
		list_del(&entry->list);
		kfree(entry);
		freed = true;
	}
	mutex_unlock(&priv->tun.ipv6_off_lock);

	if (freed)
		nfp_tun_write_ipv6_list(app);
}

static int
__nfp_tunnel_offload_mac(struct nfp_app *app, const u8 *mac, u16 idx, bool del)
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
nfp_tunnel_lookup_offloaded_macs(struct nfp_app *app, const u8 *mac)
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
	} else if (nfp_flower_is_supported_bridge(netdev)) {
		entry->bridge_count++;
	}

	entry->ref_count++;
}

static int
nfp_tunnel_add_shared_mac(struct nfp_app *app, struct net_device *netdev,
			  int port, bool mod)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_tun_offloaded_mac *entry;
	int ida_idx = -1, err;
	u16 nfp_mac_idx = 0;

	entry = nfp_tunnel_lookup_offloaded_macs(app, netdev->dev_addr);
	if (entry && nfp_tunnel_is_mac_idx_global(entry->index)) {
		if (entry->bridge_count ||
		    !nfp_flower_is_supported_bridge(netdev)) {
			nfp_tunnel_offloaded_macs_inc_ref_and_link(entry,
								   netdev, mod);
			return 0;
		}

		/* MAC is global but matches need to go to pre_tun table. */
		nfp_mac_idx = entry->index | NFP_TUN_PRE_TUN_IDX_BIT;
	}

	if (!nfp_mac_idx) {
		/* Assign a global index if non-repr or MAC is now shared. */
		if (entry || !port) {
			ida_idx = ida_alloc_max(&priv->tun.mac_off_ids,
						NFP_MAX_MAC_INDEX, GFP_KERNEL);
			if (ida_idx < 0)
				return ida_idx;

			nfp_mac_idx =
				nfp_tunnel_get_global_mac_idx_from_ida(ida_idx);

			if (nfp_flower_is_supported_bridge(netdev))
				nfp_mac_idx |= NFP_TUN_PRE_TUN_IDX_BIT;

		} else {
			nfp_mac_idx =
				nfp_tunnel_get_mac_idx_from_phy_port_id(port);
		}
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
	if (ida_idx != -1)
		ida_free(&priv->tun.mac_off_ids, ida_idx);

	return err;
}

static int
nfp_tunnel_del_shared_mac(struct nfp_app *app, struct net_device *netdev,
			  const u8 *mac, bool mod)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_flower_repr_priv *repr_priv;
	struct nfp_tun_offloaded_mac *entry;
	struct nfp_repr *repr;
	u16 nfp_mac_idx;
	int ida_idx;

	entry = nfp_tunnel_lookup_offloaded_macs(app, mac);
	if (!entry)
		return 0;

	entry->ref_count--;
	/* If del is part of a mod then mac_list is still in use elsewhere. */
	if (nfp_netdev_is_nfp_repr(netdev) && !mod) {
		repr = netdev_priv(netdev);
		repr_priv = repr->app_priv;
		list_del(&repr_priv->mac_list);
	}

	if (nfp_flower_is_supported_bridge(netdev)) {
		entry->bridge_count--;

		if (!entry->bridge_count && entry->ref_count) {
			nfp_mac_idx = entry->index & ~NFP_TUN_PRE_TUN_IDX_BIT;
			if (__nfp_tunnel_offload_mac(app, mac, nfp_mac_idx,
						     false)) {
				nfp_flower_cmsg_warn(app, "MAC offload index revert failed on %s.\n",
						     netdev_name(netdev));
				return 0;
			}

			entry->index = nfp_mac_idx;
			return 0;
		}
	}

	/* If MAC is now used by 1 repr set the offloaded MAC index to port. */
	if (entry->ref_count == 1 && list_is_singular(&entry->repr_list)) {
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
		ida_free(&priv->tun.mac_off_ids, ida_idx);
		entry->index = nfp_mac_idx;
		return 0;
	}

	if (entry->ref_count)
		return 0;

	WARN_ON_ONCE(rhashtable_remove_fast(&priv->tun.offloaded_macs,
					    &entry->ht_node,
					    offloaded_macs_params));

	if (nfp_flower_is_supported_bridge(netdev))
		nfp_mac_idx = entry->index & ~NFP_TUN_PRE_TUN_IDX_BIT;
	else
		nfp_mac_idx = entry->index;

	/* If MAC has global ID then extract and free the ida entry. */
	if (nfp_tunnel_is_mac_idx_global(nfp_mac_idx)) {
		ida_idx = nfp_tunnel_get_ida_from_global_mac_idx(entry->index);
		ida_free(&priv->tun.mac_off_ids, ida_idx);
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
		if (repr_priv->on_bridge)
			return 0;

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
	} else if (event == NETDEV_CHANGEUPPER) {
		/* If a repr is attached to a bridge then tunnel packets
		 * entering the physical port are directed through the bridge
		 * datapath and cannot be directly detunneled. Therefore,
		 * associated offloaded MACs and indexes should not be used
		 * by fw for detunneling.
		 */
		struct netdev_notifier_changeupper_info *info = ptr;
		struct net_device *upper = info->upper_dev;
		struct nfp_flower_repr_priv *repr_priv;
		struct nfp_repr *repr;

		if (!nfp_netdev_is_nfp_repr(netdev) ||
		    !nfp_flower_is_supported_bridge(upper))
			return NOTIFY_OK;

		repr = netdev_priv(netdev);
		if (repr->app != app)
			return NOTIFY_OK;

		repr_priv = repr->app_priv;

		if (info->linking) {
			if (nfp_tunnel_offload_mac(app, netdev,
						   NFP_TUNNEL_MAC_OFFLOAD_DEL))
				nfp_flower_cmsg_warn(app, "Failed to delete offloaded MAC on %s.\n",
						     netdev_name(netdev));
			repr_priv->on_bridge = true;
		} else {
			repr_priv->on_bridge = false;

			if (!(netdev->flags & IFF_UP))
				return NOTIFY_OK;

			if (nfp_tunnel_offload_mac(app, netdev,
						   NFP_TUNNEL_MAC_OFFLOAD_ADD))
				nfp_flower_cmsg_warn(app, "Failed to offload MAC on %s.\n",
						     netdev_name(netdev));
		}
	}
	return NOTIFY_OK;
}

int nfp_flower_xmit_pre_tun_flow(struct nfp_app *app,
				 struct nfp_fl_payload *flow)
{
	struct nfp_flower_priv *app_priv = app->priv;
	struct nfp_tun_offloaded_mac *mac_entry;
	struct nfp_flower_meta_tci *key_meta;
	struct nfp_tun_pre_tun_rule payload;
	struct net_device *internal_dev;
	int err;

	if (app_priv->pre_tun_rule_cnt == NFP_TUN_PRE_TUN_RULE_LIMIT)
		return -ENOSPC;

	memset(&payload, 0, sizeof(struct nfp_tun_pre_tun_rule));

	internal_dev = flow->pre_tun_rule.dev;
	payload.vlan_tci = flow->pre_tun_rule.vlan_tci;
	payload.host_ctx_id = flow->meta.host_ctx_id;

	/* Lookup MAC index for the pre-tunnel rule egress device.
	 * Note that because the device is always an internal port, it will
	 * have a constant global index so does not need to be tracked.
	 */
	mac_entry = nfp_tunnel_lookup_offloaded_macs(app,
						     internal_dev->dev_addr);
	if (!mac_entry)
		return -ENOENT;

	/* Set/clear IPV6 bit. cpu_to_be16() swap will lead to MSB being
	 * set/clear for port_idx.
	 */
	key_meta = (struct nfp_flower_meta_tci *)flow->unmasked_data;
	if (key_meta->nfp_flow_key_layer & NFP_FLOWER_LAYER_IPV6)
		mac_entry->index |= NFP_TUN_PRE_TUN_IPV6_BIT;
	else
		mac_entry->index &= ~NFP_TUN_PRE_TUN_IPV6_BIT;

	payload.port_idx = cpu_to_be16(mac_entry->index);

	/* Copy mac id and vlan to flow - dev may not exist at delete time. */
	flow->pre_tun_rule.vlan_tci = payload.vlan_tci;
	flow->pre_tun_rule.port_idx = payload.port_idx;

	err = nfp_flower_xmit_tun_conf(app, NFP_FLOWER_CMSG_TYPE_PRE_TUN_RULE,
				       sizeof(struct nfp_tun_pre_tun_rule),
				       (unsigned char *)&payload, GFP_KERNEL);
	if (err)
		return err;

	app_priv->pre_tun_rule_cnt++;

	return 0;
}

int nfp_flower_xmit_pre_tun_del_flow(struct nfp_app *app,
				     struct nfp_fl_payload *flow)
{
	struct nfp_flower_priv *app_priv = app->priv;
	struct nfp_tun_pre_tun_rule payload;
	u32 tmp_flags = 0;
	int err;

	memset(&payload, 0, sizeof(struct nfp_tun_pre_tun_rule));

	tmp_flags |= NFP_TUN_PRE_TUN_RULE_DEL;
	payload.flags = cpu_to_be32(tmp_flags);
	payload.vlan_tci = flow->pre_tun_rule.vlan_tci;
	payload.port_idx = flow->pre_tun_rule.port_idx;

	err = nfp_flower_xmit_tun_conf(app, NFP_FLOWER_CMSG_TYPE_PRE_TUN_RULE,
				       sizeof(struct nfp_tun_pre_tun_rule),
				       (unsigned char *)&payload, GFP_KERNEL);
	if (err)
		return err;

	app_priv->pre_tun_rule_cnt--;

	return 0;
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

	/* Initialise priv data for IPv4/v6 offloading. */
	mutex_init(&priv->tun.ipv4_off_lock);
	INIT_LIST_HEAD(&priv->tun.ipv4_off_list);
	mutex_init(&priv->tun.ipv6_off_lock);
	INIT_LIST_HEAD(&priv->tun.ipv6_off_list);

	/* Initialise priv data for neighbour offloading. */
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

	mutex_destroy(&priv->tun.ipv6_off_lock);

	/* Destroy rhash. Entries should be cleaned on netdev notifier unreg. */
	rhashtable_free_and_destroy(&priv->tun.offloaded_macs,
				    nfp_check_rhashtable_empty, NULL);

	nfp_tun_cleanup_nn_entries(app);
}
