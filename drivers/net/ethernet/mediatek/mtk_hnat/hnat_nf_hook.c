/*   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   Copyright (C) 2014-2016 Sean Wang <sean.wang@mediatek.com>
 *   Copyright (C) 2016-2017 John Crispin <blogic@openwrt.org>
 */

#include <linux/netfilter_bridge.h>

#include <linux/of.h>
#include <net/arp.h>
#include <net/neighbour.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/net_namespace.h>

#include "nf_hnat_mtk.h"
#include "hnat.h"

#include "../mtk_eth_soc.h"

static unsigned int skb_to_hnat_info(struct sk_buff *skb,
				     const struct net_device *dev,
				     struct foe_entry *foe)
{
	struct foe_entry entry = { 0 };
	int lan = IS_LAN(dev);
	int wan = IS_WAN(dev);
	struct ethhdr *eth;
	struct iphdr *iph;
	struct tcphdr *tcph;
	struct udphdr *udph;
	int tcp = 0;
	int ipv4 = 0;
	u32 gmac;

	eth = eth_hdr(skb);
	switch (ntohs(eth->h_proto)) {
	case ETH_P_IP:
		ipv4 = 1;
		break;

	default:
		return -1;
	}

	iph = ip_hdr(skb);
	switch (iph->protocol) {
	case IPPROTO_TCP:
		tcph = tcp_hdr(skb);
		tcp = 1;
		break;

	case IPPROTO_UDP:
		udph = udp_hdr(skb);
		break;

	default:
		return -1;
	}

	entry.ipv4_hnapt.etype = htons(ETH_P_IP);

	if (lan) {
		entry.ipv4_hnapt.etype = htons(ETH_P_8021Q);
		entry.bfib1.vlan_layer = 1;

		/* lan0-port1, lan1-port2, lan2-port3, lan3-port4 */
		entry.ipv4_hnapt.vlan1 = BIT((dev->name[3] - '0')+1);   
	} else if (wan) {
		entry.ipv4_hnapt.etype = htons(ETH_P_8021Q);
		entry.bfib1.vlan_layer = 1;

		/* wan port 0  */
		entry.ipv4_hnapt.vlan1 = BIT(0);   
	}

	if (dev->priv_flags & IFF_802_1Q_VLAN) {
		struct vlan_dev_priv *vlan = vlan_dev_priv(dev);

		entry.ipv4_hnapt.etype = htons(ETH_P_8021Q);
		entry.bfib1.vlan_layer = 1;
		entry.ipv4_hnapt.vlan2 = vlan->vlan_id;
	}

	entry.ipv4_hnapt.dmac_hi = swab32(*((u32*) eth->h_dest));
	entry.ipv4_hnapt.dmac_lo = swab16(*((u16*) &eth->h_dest[4]));
	entry.ipv4_hnapt.smac_hi = swab32(*((u32*) eth->h_source));
	entry.ipv4_hnapt.smac_lo = swab16(*((u16*) &eth->h_source[4]));
	entry.ipv4_hnapt.pppoe_id = 0;
	entry.bfib1.psn = 0;
	entry.ipv4_hnapt.bfib1.vpm = 1;

	if (ipv4)
		entry.ipv4_hnapt.bfib1.pkt_type = IPV4_HNAPT;

	entry.ipv4_hnapt.new_sip = ntohl(iph->saddr);
	entry.ipv4_hnapt.new_dip = ntohl(iph->daddr);
	entry.ipv4_hnapt.iblk2.dscp = iph->tos;
#if defined(CONFIG_NET_MEDIATEK_HW_QOS)
	entry.ipv4_hnapt.iblk2.qid = skb->mark & 0x7;
	if (lan)
		entry.ipv4_hnapt.iblk2.qid += 8;
	entry.ipv4_hnapt.iblk2.fqos = 1;
#endif
	if (tcp) {
		entry.ipv4_hnapt.new_sport = ntohs(tcph->source);
		entry.ipv4_hnapt.new_dport = ntohs(tcph->dest);
		entry.ipv4_hnapt.bfib1.udp = 0;
	} else {
		entry.ipv4_hnapt.new_sport = ntohs(udph->source);
		entry.ipv4_hnapt.new_dport = ntohs(udph->dest);
		entry.ipv4_hnapt.bfib1.udp = 1;
	}

	if (IS_LAN(dev))
		gmac = NR_GMAC1_PORT;
	else if (IS_WAN(dev))
		gmac = NR_GMAC2_PORT;

	if (is_multicast_ether_addr(&eth->h_dest[0]))
		entry.ipv4_hnapt.iblk2.mcast = 1;
	else
		entry.ipv4_hnapt.iblk2.mcast = 0;

	entry.ipv4_hnapt.iblk2.dp = gmac;
	entry.ipv4_hnapt.iblk2.port_mg = 0x3f;
	entry.ipv4_hnapt.iblk2.port_ag = (skb->mark >> 3) & 0x1f;
	if (IS_LAN(dev))
		entry.ipv4_hnapt.iblk2.port_ag += 32;
	entry.bfib1.time_stamp = readl((host->fe_base + 0x0010)) & (0xFFFF);
	entry.ipv4_hnapt.bfib1.ttl = 1;
	entry.ipv4_hnapt.bfib1.cah = 1;
	entry.ipv4_hnapt.bfib1.ka = 1;
	entry.bfib1.state = BIND;

	entry.ipv4_hnapt.sip = foe->ipv4_hnapt.sip;
	entry.ipv4_hnapt.dip = foe->ipv4_hnapt.dip;
	entry.ipv4_hnapt.sport = foe->ipv4_hnapt.sport;
	entry.ipv4_hnapt.dport = foe->ipv4_hnapt.dport;

	memcpy(foe, &entry, sizeof(entry));

	return 0;
}

static unsigned int mtk_hnat_nf_post_routing(struct sk_buff *skb,
					     const struct net_device *out,
					     unsigned int (*fn)(struct sk_buff *, const struct net_device *),
					     const char *func)
{
	struct foe_entry *entry;
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	const struct nf_conn_help *help;

#if 0
	if ((skb->mark & 0x7) < 4)
		return 0;
#endif

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return 0;

	/* rcu_read_lock()ed by nf_hook_slow */
	help = nfct_help(ct);
	if (help && rcu_dereference(help->helper))
		return 0;

	if ((FROM_GE_WAN(skb) || FROM_GE_LAN(skb)) &&
	    skb_hnat_is_hashed(skb) &&
	    (skb_hnat_reason(skb) == HIT_BIND_KEEPALIVE_DUP_OLD_HDR))
		return -1;

	if ((IS_LAN(out) && FROM_GE_WAN(skb)) ||
	    (IS_WAN(out) && FROM_GE_LAN(skb))) {
		if (!skb_hnat_is_hashed(skb))
			return 0;

		entry = &host->foe_table_cpu[skb_hnat_entry(skb)];
		if (entry_hnat_is_bound(entry))
			return 0;

		if (skb_hnat_reason(skb) == HIT_UNBIND_RATE_REACH &&
		    skb_hnat_alg(skb) == 0) {
			if (fn && fn(skb, out))
				return 0;
			skb_to_hnat_info(skb, out, entry);
		}
	}

	return 0;
}

static unsigned int mtk_hnat_nf_pre_routing(void *priv,
					    struct sk_buff *skb,
					    const struct nf_hook_state *state)
{
	if (IS_WAN(state->in))
		HNAT_SKB_CB(skb)->iif = FOE_MAGIC_GE_WAN;
	else if (IS_LAN(state->in))
		HNAT_SKB_CB(skb)->iif = FOE_MAGIC_GE_LAN;
	else if (!IS_BR(state->in))
		HNAT_SKB_CB(skb)->iif = FOE_INVALID;

	return NF_ACCEPT;
}

static unsigned int hnat_get_nexthop(struct sk_buff *skb, const struct net_device *out) {

	u32 nexthop;
	struct neighbour *neigh;
	struct dst_entry *dst = skb_dst(skb);
	struct rtable *rt = (struct rtable *)dst;
	struct net_device *dev = (__force struct net_device *)out;

	rcu_read_lock_bh();
	nexthop = (__force u32) rt_nexthop(rt, ip_hdr(skb)->daddr);
	neigh = __ipv4_neigh_lookup_noref(dev, nexthop);
	if (unlikely(!neigh)) {
		dev_err(host->dev, "%s:++ no neigh\n", __func__);
		return -1;
	}

	/* why do we get all zero ethernet address ? */
	if (!is_valid_ether_addr(neigh->ha)){
		rcu_read_unlock_bh();
		return -1;
	}

	memcpy(eth_hdr(skb)->h_dest, neigh->ha, ETH_ALEN);
	memcpy(eth_hdr(skb)->h_source, out->dev_addr, ETH_ALEN);

	rcu_read_unlock_bh();

	return 0;
}

static unsigned int mtk_hnat_ipv4_nf_post_routing(void *priv,
						 struct sk_buff *skb,
						 const struct nf_hook_state *state)
{
	if (!mtk_hnat_nf_post_routing(skb, state->out, hnat_get_nexthop, __func__))
		return NF_ACCEPT;

	return NF_DROP;
}

static unsigned int mtk_hnat_br_nf_post_routing(void *priv,
						 struct sk_buff *skb,
						 const struct nf_hook_state *state)
{
	if (!mtk_hnat_nf_post_routing(skb, state->out , 0, __func__))
		return NF_ACCEPT;

	return NF_DROP;
}

static struct nf_hook_ops mtk_hnat_nf_ops[] __read_mostly = {
	{
		.hook = mtk_hnat_nf_pre_routing,
		.pf = NFPROTO_IPV4,
		.hooknum = NF_INET_PRE_ROUTING,
		.priority = NF_IP_PRI_FIRST,
	}, {
		.hook = mtk_hnat_ipv4_nf_post_routing,
		.pf = NFPROTO_IPV4,
		.hooknum = NF_INET_POST_ROUTING,
		.priority = NF_IP_PRI_LAST,
	}, {
		.hook = mtk_hnat_nf_pre_routing,
		.pf = NFPROTO_BRIDGE,
		.hooknum = NF_BR_PRE_ROUTING,
		.priority = NF_BR_PRI_FIRST,
	}, {
		.hook = mtk_hnat_br_nf_post_routing,
		.pf = NFPROTO_BRIDGE,
		.hooknum = NF_BR_POST_ROUTING,
		.priority = NF_BR_PRI_LAST - 1,
	},
};

/*
int hnat_register_nf_hooks(void)
{
	return nf_register_hooks(mtk_hnat_nf_ops,
				 ARRAY_SIZE(mtk_hnat_nf_ops));
}

void hnat_unregister_nf_hooks(void)
{
	nf_unregister_hooks(mtk_hnat_nf_ops,
			    ARRAY_SIZE(mtk_hnat_nf_ops));
}
*/
int hnat_register_nf_hooks(struct net *net)
{
	return nf_register_net_hooks(net,mtk_hnat_nf_ops,
				 ARRAY_SIZE(mtk_hnat_nf_ops));
}

void hnat_unregister_nf_hooks(struct net *net)
{
	nf_unregister_net_hooks(net,mtk_hnat_nf_ops,
			    ARRAY_SIZE(mtk_hnat_nf_ops));
}
