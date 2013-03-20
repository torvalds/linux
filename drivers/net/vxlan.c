/*
 * VXLAN: Virtual eXtensible Local Area Network
 *
 * Copyright (c) 2012 Vyatta Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * TODO
 *  - use IANA UDP port number (when defined)
 *  - IPv6 (not in RFC)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/rculist.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/igmp.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/hash.h>
#include <linux/ethtool.h>
#include <net/arp.h>
#include <net/ndisc.h>
#include <net/ip.h>
#include <net/ipip.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/rtnetlink.h>
#include <net/route.h>
#include <net/dsfield.h>
#include <net/inet_ecn.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>

#define VXLAN_VERSION	"0.1"

#define VNI_HASH_BITS	10
#define VNI_HASH_SIZE	(1<<VNI_HASH_BITS)
#define FDB_HASH_BITS	8
#define FDB_HASH_SIZE	(1<<FDB_HASH_BITS)
#define FDB_AGE_DEFAULT 300 /* 5 min */
#define FDB_AGE_INTERVAL (10 * HZ)	/* rescan interval */

#define VXLAN_N_VID	(1u << 24)
#define VXLAN_VID_MASK	(VXLAN_N_VID - 1)
/* IP header + UDP + VXLAN + Ethernet header */
#define VXLAN_HEADROOM (20 + 8 + 8 + 14)

#define VXLAN_FLAGS 0x08000000	/* struct vxlanhdr.vx_flags required value. */

/* VXLAN protocol header */
struct vxlanhdr {
	__be32 vx_flags;
	__be32 vx_vni;
};

/* UDP port for VXLAN traffic. */
static unsigned int vxlan_port __read_mostly = 8472;
module_param_named(udp_port, vxlan_port, uint, 0444);
MODULE_PARM_DESC(udp_port, "Destination UDP port");

static bool log_ecn_error = true;
module_param(log_ecn_error, bool, 0644);
MODULE_PARM_DESC(log_ecn_error, "Log packets received with corrupted ECN");

/* per-net private data for this module */
static unsigned int vxlan_net_id;
struct vxlan_net {
	struct socket	  *sock;	/* UDP encap socket */
	struct hlist_head vni_list[VNI_HASH_SIZE];
};

/* Forwarding table entry */
struct vxlan_fdb {
	struct hlist_node hlist;	/* linked list of entries */
	struct rcu_head	  rcu;
	unsigned long	  updated;	/* jiffies */
	unsigned long	  used;
	__be32		  remote_ip;
	u16		  state;	/* see ndm_state */
	u8		  eth_addr[ETH_ALEN];
};

/* Per-cpu network traffic stats */
struct vxlan_stats {
	u64			rx_packets;
	u64			rx_bytes;
	u64			tx_packets;
	u64			tx_bytes;
	struct u64_stats_sync	syncp;
};

/* Pseudo network device */
struct vxlan_dev {
	struct hlist_node hlist;
	struct net_device *dev;
	struct vxlan_stats __percpu *stats;
	__u32		  vni;		/* virtual network id */
	__be32	          gaddr;	/* multicast group */
	__be32		  saddr;	/* source address */
	unsigned int      link;		/* link to multicast over */
	__u16		  port_min;	/* source port range */
	__u16		  port_max;
	__u8		  tos;		/* TOS override */
	__u8		  ttl;
	u32		  flags;	/* VXLAN_F_* below */

	unsigned long	  age_interval;
	struct timer_list age_timer;
	spinlock_t	  hash_lock;
	unsigned int	  addrcnt;
	unsigned int	  addrmax;

	struct hlist_head fdb_head[FDB_HASH_SIZE];
};

#define VXLAN_F_LEARN	0x01
#define VXLAN_F_PROXY	0x02
#define VXLAN_F_RSC	0x04
#define VXLAN_F_L2MISS	0x08
#define VXLAN_F_L3MISS	0x10

/* salt for hash table */
static u32 vxlan_salt __read_mostly;

static inline struct hlist_head *vni_head(struct net *net, u32 id)
{
	struct vxlan_net *vn = net_generic(net, vxlan_net_id);

	return &vn->vni_list[hash_32(id, VNI_HASH_BITS)];
}

/* Look up VNI in a per net namespace table */
static struct vxlan_dev *vxlan_find_vni(struct net *net, u32 id)
{
	struct vxlan_dev *vxlan;

	hlist_for_each_entry_rcu(vxlan, vni_head(net, id), hlist) {
		if (vxlan->vni == id)
			return vxlan;
	}

	return NULL;
}

/* Fill in neighbour message in skbuff. */
static int vxlan_fdb_info(struct sk_buff *skb, struct vxlan_dev *vxlan,
			   const struct vxlan_fdb *fdb,
			   u32 portid, u32 seq, int type, unsigned int flags)
{
	unsigned long now = jiffies;
	struct nda_cacheinfo ci;
	struct nlmsghdr *nlh;
	struct ndmsg *ndm;
	bool send_ip, send_eth;

	nlh = nlmsg_put(skb, portid, seq, type, sizeof(*ndm), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	ndm = nlmsg_data(nlh);
	memset(ndm, 0, sizeof(*ndm));

	send_eth = send_ip = true;

	if (type == RTM_GETNEIGH) {
		ndm->ndm_family	= AF_INET;
		send_ip = fdb->remote_ip != 0;
		send_eth = !is_zero_ether_addr(fdb->eth_addr);
	} else
		ndm->ndm_family	= AF_BRIDGE;
	ndm->ndm_state = fdb->state;
	ndm->ndm_ifindex = vxlan->dev->ifindex;
	ndm->ndm_flags = NTF_SELF;
	ndm->ndm_type = NDA_DST;

	if (send_eth && nla_put(skb, NDA_LLADDR, ETH_ALEN, &fdb->eth_addr))
		goto nla_put_failure;

	if (send_ip && nla_put_be32(skb, NDA_DST, fdb->remote_ip))
		goto nla_put_failure;

	ci.ndm_used	 = jiffies_to_clock_t(now - fdb->used);
	ci.ndm_confirmed = 0;
	ci.ndm_updated	 = jiffies_to_clock_t(now - fdb->updated);
	ci.ndm_refcnt	 = 0;

	if (nla_put(skb, NDA_CACHEINFO, sizeof(ci), &ci))
		goto nla_put_failure;

	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static inline size_t vxlan_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct ndmsg))
		+ nla_total_size(ETH_ALEN) /* NDA_LLADDR */
		+ nla_total_size(sizeof(__be32)) /* NDA_DST */
		+ nla_total_size(sizeof(struct nda_cacheinfo));
}

static void vxlan_fdb_notify(struct vxlan_dev *vxlan,
			     const struct vxlan_fdb *fdb, int type)
{
	struct net *net = dev_net(vxlan->dev);
	struct sk_buff *skb;
	int err = -ENOBUFS;

	skb = nlmsg_new(vxlan_nlmsg_size(), GFP_ATOMIC);
	if (skb == NULL)
		goto errout;

	err = vxlan_fdb_info(skb, vxlan, fdb, 0, 0, type, 0);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in vxlan_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}

	rtnl_notify(skb, net, 0, RTNLGRP_NEIGH, NULL, GFP_ATOMIC);
	return;
errout:
	if (err < 0)
		rtnl_set_sk_err(net, RTNLGRP_NEIGH, err);
}

static void vxlan_ip_miss(struct net_device *dev, __be32 ipa)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct vxlan_fdb f;

	memset(&f, 0, sizeof f);
	f.state = NUD_STALE;
	f.remote_ip = ipa; /* goes to NDA_DST */

	vxlan_fdb_notify(vxlan, &f, RTM_GETNEIGH);
}

static void vxlan_fdb_miss(struct vxlan_dev *vxlan, const u8 eth_addr[ETH_ALEN])
{
	struct vxlan_fdb	f;

	memset(&f, 0, sizeof f);
	f.state = NUD_STALE;
	memcpy(f.eth_addr, eth_addr, ETH_ALEN);

	vxlan_fdb_notify(vxlan, &f, RTM_GETNEIGH);
}

/* Hash Ethernet address */
static u32 eth_hash(const unsigned char *addr)
{
	u64 value = get_unaligned((u64 *)addr);

	/* only want 6 bytes */
#ifdef __BIG_ENDIAN
	value >>= 16;
#else
	value <<= 16;
#endif
	return hash_64(value, FDB_HASH_BITS);
}

/* Hash chain to use given mac address */
static inline struct hlist_head *vxlan_fdb_head(struct vxlan_dev *vxlan,
						const u8 *mac)
{
	return &vxlan->fdb_head[eth_hash(mac)];
}

/* Look up Ethernet address in forwarding table */
static struct vxlan_fdb *vxlan_find_mac(struct vxlan_dev *vxlan,
					const u8 *mac)

{
	struct hlist_head *head = vxlan_fdb_head(vxlan, mac);
	struct vxlan_fdb *f;

	hlist_for_each_entry_rcu(f, head, hlist) {
		if (compare_ether_addr(mac, f->eth_addr) == 0)
			return f;
	}

	return NULL;
}

/* Add new entry to forwarding table -- assumes lock held */
static int vxlan_fdb_create(struct vxlan_dev *vxlan,
			    const u8 *mac, __be32 ip,
			    __u16 state, __u16 flags)
{
	struct vxlan_fdb *f;
	int notify = 0;

	f = vxlan_find_mac(vxlan, mac);
	if (f) {
		if (flags & NLM_F_EXCL) {
			netdev_dbg(vxlan->dev,
				   "lost race to create %pM\n", mac);
			return -EEXIST;
		}
		if (f->state != state) {
			f->state = state;
			f->updated = jiffies;
			notify = 1;
		}
	} else {
		if (!(flags & NLM_F_CREATE))
			return -ENOENT;

		if (vxlan->addrmax && vxlan->addrcnt >= vxlan->addrmax)
			return -ENOSPC;

		netdev_dbg(vxlan->dev, "add %pM -> %pI4\n", mac, &ip);
		f = kmalloc(sizeof(*f), GFP_ATOMIC);
		if (!f)
			return -ENOMEM;

		notify = 1;
		f->remote_ip = ip;
		f->state = state;
		f->updated = f->used = jiffies;
		memcpy(f->eth_addr, mac, ETH_ALEN);

		++vxlan->addrcnt;
		hlist_add_head_rcu(&f->hlist,
				   vxlan_fdb_head(vxlan, mac));
	}

	if (notify)
		vxlan_fdb_notify(vxlan, f, RTM_NEWNEIGH);

	return 0;
}

static void vxlan_fdb_destroy(struct vxlan_dev *vxlan, struct vxlan_fdb *f)
{
	netdev_dbg(vxlan->dev,
		    "delete %pM\n", f->eth_addr);

	--vxlan->addrcnt;
	vxlan_fdb_notify(vxlan, f, RTM_DELNEIGH);

	hlist_del_rcu(&f->hlist);
	kfree_rcu(f, rcu);
}

/* Add static entry (via netlink) */
static int vxlan_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
			 struct net_device *dev,
			 const unsigned char *addr, u16 flags)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	__be32 ip;
	int err;

	if (!(ndm->ndm_state & (NUD_PERMANENT|NUD_REACHABLE))) {
		pr_info("RTM_NEWNEIGH with invalid state %#x\n",
			ndm->ndm_state);
		return -EINVAL;
	}

	if (tb[NDA_DST] == NULL)
		return -EINVAL;

	if (nla_len(tb[NDA_DST]) != sizeof(__be32))
		return -EAFNOSUPPORT;

	ip = nla_get_be32(tb[NDA_DST]);

	spin_lock_bh(&vxlan->hash_lock);
	err = vxlan_fdb_create(vxlan, addr, ip, ndm->ndm_state, flags);
	spin_unlock_bh(&vxlan->hash_lock);

	return err;
}

/* Delete entry (via netlink) */
static int vxlan_fdb_delete(struct ndmsg *ndm, struct nlattr *tb[],
			    struct net_device *dev,
			    const unsigned char *addr)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct vxlan_fdb *f;
	int err = -ENOENT;

	spin_lock_bh(&vxlan->hash_lock);
	f = vxlan_find_mac(vxlan, addr);
	if (f) {
		vxlan_fdb_destroy(vxlan, f);
		err = 0;
	}
	spin_unlock_bh(&vxlan->hash_lock);

	return err;
}

/* Dump forwarding table */
static int vxlan_fdb_dump(struct sk_buff *skb, struct netlink_callback *cb,
			  struct net_device *dev, int idx)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	unsigned int h;

	for (h = 0; h < FDB_HASH_SIZE; ++h) {
		struct vxlan_fdb *f;
		int err;

		hlist_for_each_entry_rcu(f, &vxlan->fdb_head[h], hlist) {
			if (idx < cb->args[0])
				goto skip;

			err = vxlan_fdb_info(skb, vxlan, f,
					     NETLINK_CB(cb->skb).portid,
					     cb->nlh->nlmsg_seq,
					     RTM_NEWNEIGH,
					     NLM_F_MULTI);
			if (err < 0)
				break;
skip:
			++idx;
		}
	}

	return idx;
}

/* Watch incoming packets to learn mapping between Ethernet address
 * and Tunnel endpoint.
 */
static void vxlan_snoop(struct net_device *dev,
			__be32 src_ip, const u8 *src_mac)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct vxlan_fdb *f;
	int err;

	f = vxlan_find_mac(vxlan, src_mac);
	if (likely(f)) {
		f->used = jiffies;
		if (likely(f->remote_ip == src_ip))
			return;

		if (net_ratelimit())
			netdev_info(dev,
				    "%pM migrated from %pI4 to %pI4\n",
				    src_mac, &f->remote_ip, &src_ip);

		f->remote_ip = src_ip;
		f->updated = jiffies;
	} else {
		/* learned new entry */
		spin_lock(&vxlan->hash_lock);
		err = vxlan_fdb_create(vxlan, src_mac, src_ip,
				       NUD_REACHABLE,
				       NLM_F_EXCL|NLM_F_CREATE);
		spin_unlock(&vxlan->hash_lock);
	}
}


/* See if multicast group is already in use by other ID */
static bool vxlan_group_used(struct vxlan_net *vn,
			     const struct vxlan_dev *this)
{
	const struct vxlan_dev *vxlan;
	unsigned h;

	for (h = 0; h < VNI_HASH_SIZE; ++h)
		hlist_for_each_entry(vxlan, &vn->vni_list[h], hlist) {
			if (vxlan == this)
				continue;

			if (!netif_running(vxlan->dev))
				continue;

			if (vxlan->gaddr == this->gaddr)
				return true;
		}

	return false;
}

/* kernel equivalent to IP_ADD_MEMBERSHIP */
static int vxlan_join_group(struct net_device *dev)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct vxlan_net *vn = net_generic(dev_net(dev), vxlan_net_id);
	struct sock *sk = vn->sock->sk;
	struct ip_mreqn mreq = {
		.imr_multiaddr.s_addr	= vxlan->gaddr,
		.imr_ifindex		= vxlan->link,
	};
	int err;

	/* Already a member of group */
	if (vxlan_group_used(vn, vxlan))
		return 0;

	/* Need to drop RTNL to call multicast join */
	rtnl_unlock();
	lock_sock(sk);
	err = ip_mc_join_group(sk, &mreq);
	release_sock(sk);
	rtnl_lock();

	return err;
}


/* kernel equivalent to IP_DROP_MEMBERSHIP */
static int vxlan_leave_group(struct net_device *dev)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct vxlan_net *vn = net_generic(dev_net(dev), vxlan_net_id);
	int err = 0;
	struct sock *sk = vn->sock->sk;
	struct ip_mreqn mreq = {
		.imr_multiaddr.s_addr	= vxlan->gaddr,
		.imr_ifindex		= vxlan->link,
	};

	/* Only leave group when last vxlan is done. */
	if (vxlan_group_used(vn, vxlan))
		return 0;

	/* Need to drop RTNL to call multicast leave */
	rtnl_unlock();
	lock_sock(sk);
	err = ip_mc_leave_group(sk, &mreq);
	release_sock(sk);
	rtnl_lock();

	return err;
}

/* Callback from net/ipv4/udp.c to receive packets */
static int vxlan_udp_encap_recv(struct sock *sk, struct sk_buff *skb)
{
	struct iphdr *oip;
	struct vxlanhdr *vxh;
	struct vxlan_dev *vxlan;
	struct vxlan_stats *stats;
	__u32 vni;
	int err;

	/* pop off outer UDP header */
	__skb_pull(skb, sizeof(struct udphdr));

	/* Need Vxlan and inner Ethernet header to be present */
	if (!pskb_may_pull(skb, sizeof(struct vxlanhdr)))
		goto error;

	/* Drop packets with reserved bits set */
	vxh = (struct vxlanhdr *) skb->data;
	if (vxh->vx_flags != htonl(VXLAN_FLAGS) ||
	    (vxh->vx_vni & htonl(0xff))) {
		netdev_dbg(skb->dev, "invalid vxlan flags=%#x vni=%#x\n",
			   ntohl(vxh->vx_flags), ntohl(vxh->vx_vni));
		goto error;
	}

	__skb_pull(skb, sizeof(struct vxlanhdr));

	/* Is this VNI defined? */
	vni = ntohl(vxh->vx_vni) >> 8;
	vxlan = vxlan_find_vni(sock_net(sk), vni);
	if (!vxlan) {
		netdev_dbg(skb->dev, "unknown vni %d\n", vni);
		goto drop;
	}

	if (!pskb_may_pull(skb, ETH_HLEN)) {
		vxlan->dev->stats.rx_length_errors++;
		vxlan->dev->stats.rx_errors++;
		goto drop;
	}

	skb_reset_mac_header(skb);

	/* Re-examine inner Ethernet packet */
	oip = ip_hdr(skb);
	skb->protocol = eth_type_trans(skb, vxlan->dev);

	/* Ignore packet loops (and multicast echo) */
	if (compare_ether_addr(eth_hdr(skb)->h_source,
			       vxlan->dev->dev_addr) == 0)
		goto drop;

	if (vxlan->flags & VXLAN_F_LEARN)
		vxlan_snoop(skb->dev, oip->saddr, eth_hdr(skb)->h_source);

	__skb_tunnel_rx(skb, vxlan->dev);
	skb_reset_network_header(skb);

	/* If the NIC driver gave us an encapsulated packet with
	 * CHECKSUM_UNNECESSARY and Rx checksum feature is enabled,
	 * leave the CHECKSUM_UNNECESSARY, the device checksummed it
	 * for us. Otherwise force the upper layers to verify it.
	 */
	if (skb->ip_summed != CHECKSUM_UNNECESSARY || !skb->encapsulation ||
	    !(vxlan->dev->features & NETIF_F_RXCSUM))
		skb->ip_summed = CHECKSUM_NONE;

	skb->encapsulation = 0;

	err = IP_ECN_decapsulate(oip, skb);
	if (unlikely(err)) {
		if (log_ecn_error)
			net_info_ratelimited("non-ECT from %pI4 with TOS=%#x\n",
					     &oip->saddr, oip->tos);
		if (err > 1) {
			++vxlan->dev->stats.rx_frame_errors;
			++vxlan->dev->stats.rx_errors;
			goto drop;
		}
	}

	stats = this_cpu_ptr(vxlan->stats);
	u64_stats_update_begin(&stats->syncp);
	stats->rx_packets++;
	stats->rx_bytes += skb->len;
	u64_stats_update_end(&stats->syncp);

	netif_rx(skb);

	return 0;
error:
	/* Put UDP header back */
	__skb_push(skb, sizeof(struct udphdr));

	return 1;
drop:
	/* Consume bad packet */
	kfree_skb(skb);
	return 0;
}

static int arp_reduce(struct net_device *dev, struct sk_buff *skb)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct arphdr *parp;
	u8 *arpptr, *sha;
	__be32 sip, tip;
	struct neighbour *n;

	if (dev->flags & IFF_NOARP)
		goto out;

	if (!pskb_may_pull(skb, arp_hdr_len(dev))) {
		dev->stats.tx_dropped++;
		goto out;
	}
	parp = arp_hdr(skb);

	if ((parp->ar_hrd != htons(ARPHRD_ETHER) &&
	     parp->ar_hrd != htons(ARPHRD_IEEE802)) ||
	    parp->ar_pro != htons(ETH_P_IP) ||
	    parp->ar_op != htons(ARPOP_REQUEST) ||
	    parp->ar_hln != dev->addr_len ||
	    parp->ar_pln != 4)
		goto out;
	arpptr = (u8 *)parp + sizeof(struct arphdr);
	sha = arpptr;
	arpptr += dev->addr_len;	/* sha */
	memcpy(&sip, arpptr, sizeof(sip));
	arpptr += sizeof(sip);
	arpptr += dev->addr_len;	/* tha */
	memcpy(&tip, arpptr, sizeof(tip));

	if (ipv4_is_loopback(tip) ||
	    ipv4_is_multicast(tip))
		goto out;

	n = neigh_lookup(&arp_tbl, &tip, dev);

	if (n) {
		struct vxlan_dev *vxlan = netdev_priv(dev);
		struct vxlan_fdb *f;
		struct sk_buff	*reply;

		if (!(n->nud_state & NUD_CONNECTED)) {
			neigh_release(n);
			goto out;
		}

		f = vxlan_find_mac(vxlan, n->ha);
		if (f && f->remote_ip == 0) {
			/* bridge-local neighbor */
			neigh_release(n);
			goto out;
		}

		reply = arp_create(ARPOP_REPLY, ETH_P_ARP, sip, dev, tip, sha,
				n->ha, sha);

		neigh_release(n);

		skb_reset_mac_header(reply);
		__skb_pull(reply, skb_network_offset(reply));
		reply->ip_summed = CHECKSUM_UNNECESSARY;
		reply->pkt_type = PACKET_HOST;

		if (netif_rx_ni(reply) == NET_RX_DROP)
			dev->stats.rx_dropped++;
	} else if (vxlan->flags & VXLAN_F_L3MISS)
		vxlan_ip_miss(dev, tip);
out:
	consume_skb(skb);
	return NETDEV_TX_OK;
}

static bool route_shortcircuit(struct net_device *dev, struct sk_buff *skb)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct neighbour *n;
	struct iphdr *pip;

	if (is_multicast_ether_addr(eth_hdr(skb)->h_dest))
		return false;

	n = NULL;
	switch (ntohs(eth_hdr(skb)->h_proto)) {
	case ETH_P_IP:
		if (!pskb_may_pull(skb, sizeof(struct iphdr)))
			return false;
		pip = ip_hdr(skb);
		n = neigh_lookup(&arp_tbl, &pip->daddr, dev);
		break;
	default:
		return false;
	}

	if (n) {
		bool diff;

		diff = compare_ether_addr(eth_hdr(skb)->h_dest, n->ha) != 0;
		if (diff) {
			memcpy(eth_hdr(skb)->h_source, eth_hdr(skb)->h_dest,
				dev->addr_len);
			memcpy(eth_hdr(skb)->h_dest, n->ha, dev->addr_len);
		}
		neigh_release(n);
		return diff;
	} else if (vxlan->flags & VXLAN_F_L3MISS)
		vxlan_ip_miss(dev, pip->daddr);
	return false;
}

/* Extract dsfield from inner protocol */
static inline u8 vxlan_get_dsfield(const struct iphdr *iph,
				   const struct sk_buff *skb)
{
	if (skb->protocol == htons(ETH_P_IP))
		return iph->tos;
	else if (skb->protocol == htons(ETH_P_IPV6))
		return ipv6_get_dsfield((const struct ipv6hdr *)iph);
	else
		return 0;
}

/* Propogate ECN bits out */
static inline u8 vxlan_ecn_encap(u8 tos,
				 const struct iphdr *iph,
				 const struct sk_buff *skb)
{
	u8 inner = vxlan_get_dsfield(iph, skb);

	return INET_ECN_encapsulate(tos, inner);
}

static void vxlan_sock_free(struct sk_buff *skb)
{
	sock_put(skb->sk);
}

/* On transmit, associate with the tunnel socket */
static void vxlan_set_owner(struct net_device *dev, struct sk_buff *skb)
{
	struct vxlan_net *vn = net_generic(dev_net(dev), vxlan_net_id);
	struct sock *sk = vn->sock->sk;

	skb_orphan(skb);
	sock_hold(sk);
	skb->sk = sk;
	skb->destructor = vxlan_sock_free;
}

/* Compute source port for outgoing packet
 *   first choice to use L4 flow hash since it will spread
 *     better and maybe available from hardware
 *   secondary choice is to use jhash on the Ethernet header
 */
static u16 vxlan_src_port(const struct vxlan_dev *vxlan, struct sk_buff *skb)
{
	unsigned int range = (vxlan->port_max - vxlan->port_min) + 1;
	u32 hash;

	hash = skb_get_rxhash(skb);
	if (!hash)
		hash = jhash(skb->data, 2 * ETH_ALEN,
			     (__force u32) skb->protocol);

	return (((u64) hash * range) >> 32) + vxlan->port_min;
}

/* Transmit local packets over Vxlan
 *
 * Outer IP header inherits ECN and DF from inner header.
 * Outer UDP destination is the VXLAN assigned port.
 *           source port is based on hash of flow
 */
static netdev_tx_t vxlan_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct rtable *rt;
	const struct iphdr *old_iph;
	struct ethhdr *eth;
	struct iphdr *iph;
	struct vxlanhdr *vxh;
	struct udphdr *uh;
	struct flowi4 fl4;
	unsigned int pkt_len = skb->len;
	__be32 dst;
	__u16 src_port;
	__be16 df = 0;
	__u8 tos, ttl;
	int err;
	bool did_rsc = false;
	const struct vxlan_fdb *f;

	skb_reset_mac_header(skb);
	eth = eth_hdr(skb);

	if ((vxlan->flags & VXLAN_F_PROXY) && ntohs(eth->h_proto) == ETH_P_ARP)
		return arp_reduce(dev, skb);
	else if ((vxlan->flags&VXLAN_F_RSC) && ntohs(eth->h_proto) == ETH_P_IP)
		did_rsc = route_shortcircuit(dev, skb);

	f = vxlan_find_mac(vxlan, eth->h_dest);
	if (f == NULL) {
		did_rsc = false;
		dst = vxlan->gaddr;
		if (!dst && (vxlan->flags & VXLAN_F_L2MISS) &&
		    !is_multicast_ether_addr(eth->h_dest))
			vxlan_fdb_miss(vxlan, eth->h_dest);
	} else
		dst = f->remote_ip;

	if (!dst) {
		if (did_rsc) {
			__skb_pull(skb, skb_network_offset(skb));
			skb->ip_summed = CHECKSUM_NONE;
			skb->pkt_type = PACKET_HOST;

			/* short-circuited back to local bridge */
			if (netif_rx(skb) == NET_RX_SUCCESS) {
				struct vxlan_stats *stats =
						this_cpu_ptr(vxlan->stats);

				u64_stats_update_begin(&stats->syncp);
				stats->tx_packets++;
				stats->tx_bytes += pkt_len;
				u64_stats_update_end(&stats->syncp);
			} else {
				dev->stats.tx_errors++;
				dev->stats.tx_aborted_errors++;
			}
			return NETDEV_TX_OK;
		}
		goto drop;
	}

	if (!skb->encapsulation) {
		skb_reset_inner_headers(skb);
		skb->encapsulation = 1;
	}

	/* Need space for new headers (invalidates iph ptr) */
	if (skb_cow_head(skb, VXLAN_HEADROOM))
		goto drop;

	old_iph = ip_hdr(skb);

	ttl = vxlan->ttl;
	if (!ttl && IN_MULTICAST(ntohl(dst)))
		ttl = 1;

	tos = vxlan->tos;
	if (tos == 1)
		tos = vxlan_get_dsfield(old_iph, skb);

	src_port = vxlan_src_port(vxlan, skb);

	memset(&fl4, 0, sizeof(fl4));
	fl4.flowi4_oif = vxlan->link;
	fl4.flowi4_tos = RT_TOS(tos);
	fl4.daddr = dst;
	fl4.saddr = vxlan->saddr;

	rt = ip_route_output_key(dev_net(dev), &fl4);
	if (IS_ERR(rt)) {
		netdev_dbg(dev, "no route to %pI4\n", &dst);
		dev->stats.tx_carrier_errors++;
		goto tx_error;
	}

	if (rt->dst.dev == dev) {
		netdev_dbg(dev, "circular route to %pI4\n", &dst);
		ip_rt_put(rt);
		dev->stats.collisions++;
		goto tx_error;
	}

	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));
	IPCB(skb)->flags &= ~(IPSKB_XFRM_TUNNEL_SIZE | IPSKB_XFRM_TRANSFORMED |
			      IPSKB_REROUTED);
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->dst);

	vxh = (struct vxlanhdr *) __skb_push(skb, sizeof(*vxh));
	vxh->vx_flags = htonl(VXLAN_FLAGS);
	vxh->vx_vni = htonl(vxlan->vni << 8);

	__skb_push(skb, sizeof(*uh));
	skb_reset_transport_header(skb);
	uh = udp_hdr(skb);

	uh->dest = htons(vxlan_port);
	uh->source = htons(src_port);

	uh->len = htons(skb->len);
	uh->check = 0;

	__skb_push(skb, sizeof(*iph));
	skb_reset_network_header(skb);
	iph		= ip_hdr(skb);
	iph->version	= 4;
	iph->ihl	= sizeof(struct iphdr) >> 2;
	iph->frag_off	= df;
	iph->protocol	= IPPROTO_UDP;
	iph->tos	= vxlan_ecn_encap(tos, old_iph, skb);
	iph->daddr	= dst;
	iph->saddr	= fl4.saddr;
	iph->ttl	= ttl ? : ip4_dst_hoplimit(&rt->dst);
	tunnel_ip_select_ident(skb, old_iph, &rt->dst);

	nf_reset(skb);

	vxlan_set_owner(dev, skb);

	/* See iptunnel_xmit() */
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		skb->ip_summed = CHECKSUM_NONE;

	err = ip_local_out(skb);
	if (likely(net_xmit_eval(err) == 0)) {
		struct vxlan_stats *stats = this_cpu_ptr(vxlan->stats);

		u64_stats_update_begin(&stats->syncp);
		stats->tx_packets++;
		stats->tx_bytes += pkt_len;
		u64_stats_update_end(&stats->syncp);
	} else {
		dev->stats.tx_errors++;
		dev->stats.tx_aborted_errors++;
	}
	return NETDEV_TX_OK;

drop:
	dev->stats.tx_dropped++;
	goto tx_free;

tx_error:
	dev->stats.tx_errors++;
tx_free:
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

/* Walk the forwarding table and purge stale entries */
static void vxlan_cleanup(unsigned long arg)
{
	struct vxlan_dev *vxlan = (struct vxlan_dev *) arg;
	unsigned long next_timer = jiffies + FDB_AGE_INTERVAL;
	unsigned int h;

	if (!netif_running(vxlan->dev))
		return;

	spin_lock_bh(&vxlan->hash_lock);
	for (h = 0; h < FDB_HASH_SIZE; ++h) {
		struct hlist_node *p, *n;
		hlist_for_each_safe(p, n, &vxlan->fdb_head[h]) {
			struct vxlan_fdb *f
				= container_of(p, struct vxlan_fdb, hlist);
			unsigned long timeout;

			if (f->state & NUD_PERMANENT)
				continue;

			timeout = f->used + vxlan->age_interval * HZ;
			if (time_before_eq(timeout, jiffies)) {
				netdev_dbg(vxlan->dev,
					   "garbage collect %pM\n",
					   f->eth_addr);
				f->state = NUD_STALE;
				vxlan_fdb_destroy(vxlan, f);
			} else if (time_before(timeout, next_timer))
				next_timer = timeout;
		}
	}
	spin_unlock_bh(&vxlan->hash_lock);

	mod_timer(&vxlan->age_timer, next_timer);
}

/* Setup stats when device is created */
static int vxlan_init(struct net_device *dev)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);

	vxlan->stats = alloc_percpu(struct vxlan_stats);
	if (!vxlan->stats)
		return -ENOMEM;

	return 0;
}

/* Start ageing timer and join group when device is brought up */
static int vxlan_open(struct net_device *dev)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	int err;

	if (vxlan->gaddr) {
		err = vxlan_join_group(dev);
		if (err)
			return err;
	}

	if (vxlan->age_interval)
		mod_timer(&vxlan->age_timer, jiffies + FDB_AGE_INTERVAL);

	return 0;
}

/* Purge the forwarding table */
static void vxlan_flush(struct vxlan_dev *vxlan)
{
	unsigned h;

	spin_lock_bh(&vxlan->hash_lock);
	for (h = 0; h < FDB_HASH_SIZE; ++h) {
		struct hlist_node *p, *n;
		hlist_for_each_safe(p, n, &vxlan->fdb_head[h]) {
			struct vxlan_fdb *f
				= container_of(p, struct vxlan_fdb, hlist);
			vxlan_fdb_destroy(vxlan, f);
		}
	}
	spin_unlock_bh(&vxlan->hash_lock);
}

/* Cleanup timer and forwarding table on shutdown */
static int vxlan_stop(struct net_device *dev)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);

	if (vxlan->gaddr)
		vxlan_leave_group(dev);

	del_timer_sync(&vxlan->age_timer);

	vxlan_flush(vxlan);

	return 0;
}

/* Merge per-cpu statistics */
static struct rtnl_link_stats64 *vxlan_stats64(struct net_device *dev,
					       struct rtnl_link_stats64 *stats)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct vxlan_stats tmp, sum = { 0 };
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		unsigned int start;
		const struct vxlan_stats *stats
			= per_cpu_ptr(vxlan->stats, cpu);

		do {
			start = u64_stats_fetch_begin_bh(&stats->syncp);
			memcpy(&tmp, stats, sizeof(tmp));
		} while (u64_stats_fetch_retry_bh(&stats->syncp, start));

		sum.tx_bytes   += tmp.tx_bytes;
		sum.tx_packets += tmp.tx_packets;
		sum.rx_bytes   += tmp.rx_bytes;
		sum.rx_packets += tmp.rx_packets;
	}

	stats->tx_bytes   = sum.tx_bytes;
	stats->tx_packets = sum.tx_packets;
	stats->rx_bytes   = sum.rx_bytes;
	stats->rx_packets = sum.rx_packets;

	stats->multicast = dev->stats.multicast;
	stats->rx_length_errors = dev->stats.rx_length_errors;
	stats->rx_frame_errors = dev->stats.rx_frame_errors;
	stats->rx_errors = dev->stats.rx_errors;

	stats->tx_dropped = dev->stats.tx_dropped;
	stats->tx_carrier_errors  = dev->stats.tx_carrier_errors;
	stats->tx_aborted_errors  = dev->stats.tx_aborted_errors;
	stats->collisions  = dev->stats.collisions;
	stats->tx_errors = dev->stats.tx_errors;

	return stats;
}

/* Stub, nothing needs to be done. */
static void vxlan_set_multicast_list(struct net_device *dev)
{
}

static const struct net_device_ops vxlan_netdev_ops = {
	.ndo_init		= vxlan_init,
	.ndo_open		= vxlan_open,
	.ndo_stop		= vxlan_stop,
	.ndo_start_xmit		= vxlan_xmit,
	.ndo_get_stats64	= vxlan_stats64,
	.ndo_set_rx_mode	= vxlan_set_multicast_list,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_fdb_add		= vxlan_fdb_add,
	.ndo_fdb_del		= vxlan_fdb_delete,
	.ndo_fdb_dump		= vxlan_fdb_dump,
};

/* Info for udev, that this is a virtual tunnel endpoint */
static struct device_type vxlan_type = {
	.name = "vxlan",
};

static void vxlan_free(struct net_device *dev)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);

	free_percpu(vxlan->stats);
	free_netdev(dev);
}

/* Initialize the device structure. */
static void vxlan_setup(struct net_device *dev)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	unsigned h;
	int low, high;

	eth_hw_addr_random(dev);
	ether_setup(dev);
	dev->hard_header_len = ETH_HLEN + VXLAN_HEADROOM;

	dev->netdev_ops = &vxlan_netdev_ops;
	dev->destructor = vxlan_free;
	SET_NETDEV_DEVTYPE(dev, &vxlan_type);

	dev->tx_queue_len = 0;
	dev->features	|= NETIF_F_LLTX;
	dev->features	|= NETIF_F_NETNS_LOCAL;
	dev->features	|= NETIF_F_SG | NETIF_F_HW_CSUM;
	dev->features   |= NETIF_F_RXCSUM;

	dev->hw_features |= NETIF_F_SG | NETIF_F_HW_CSUM | NETIF_F_RXCSUM;
	dev->priv_flags	&= ~IFF_XMIT_DST_RELEASE;
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;

	spin_lock_init(&vxlan->hash_lock);

	init_timer_deferrable(&vxlan->age_timer);
	vxlan->age_timer.function = vxlan_cleanup;
	vxlan->age_timer.data = (unsigned long) vxlan;

	inet_get_local_port_range(&low, &high);
	vxlan->port_min = low;
	vxlan->port_max = high;

	vxlan->dev = dev;

	for (h = 0; h < FDB_HASH_SIZE; ++h)
		INIT_HLIST_HEAD(&vxlan->fdb_head[h]);
}

static const struct nla_policy vxlan_policy[IFLA_VXLAN_MAX + 1] = {
	[IFLA_VXLAN_ID]		= { .type = NLA_U32 },
	[IFLA_VXLAN_GROUP]	= { .len = FIELD_SIZEOF(struct iphdr, daddr) },
	[IFLA_VXLAN_LINK]	= { .type = NLA_U32 },
	[IFLA_VXLAN_LOCAL]	= { .len = FIELD_SIZEOF(struct iphdr, saddr) },
	[IFLA_VXLAN_TOS]	= { .type = NLA_U8 },
	[IFLA_VXLAN_TTL]	= { .type = NLA_U8 },
	[IFLA_VXLAN_LEARNING]	= { .type = NLA_U8 },
	[IFLA_VXLAN_AGEING]	= { .type = NLA_U32 },
	[IFLA_VXLAN_LIMIT]	= { .type = NLA_U32 },
	[IFLA_VXLAN_PORT_RANGE] = { .len  = sizeof(struct ifla_vxlan_port_range) },
	[IFLA_VXLAN_PROXY]	= { .type = NLA_U8 },
	[IFLA_VXLAN_RSC]	= { .type = NLA_U8 },
	[IFLA_VXLAN_L2MISS]	= { .type = NLA_U8 },
	[IFLA_VXLAN_L3MISS]	= { .type = NLA_U8 },
};

static int vxlan_validate(struct nlattr *tb[], struct nlattr *data[])
{
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN) {
			pr_debug("invalid link address (not ethernet)\n");
			return -EINVAL;
		}

		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS]))) {
			pr_debug("invalid all zero ethernet address\n");
			return -EADDRNOTAVAIL;
		}
	}

	if (!data)
		return -EINVAL;

	if (data[IFLA_VXLAN_ID]) {
		__u32 id = nla_get_u32(data[IFLA_VXLAN_ID]);
		if (id >= VXLAN_VID_MASK)
			return -ERANGE;
	}

	if (data[IFLA_VXLAN_GROUP]) {
		__be32 gaddr = nla_get_be32(data[IFLA_VXLAN_GROUP]);
		if (!IN_MULTICAST(ntohl(gaddr))) {
			pr_debug("group address is not IPv4 multicast\n");
			return -EADDRNOTAVAIL;
		}
	}

	if (data[IFLA_VXLAN_PORT_RANGE]) {
		const struct ifla_vxlan_port_range *p
			= nla_data(data[IFLA_VXLAN_PORT_RANGE]);

		if (ntohs(p->high) < ntohs(p->low)) {
			pr_debug("port range %u .. %u not valid\n",
				 ntohs(p->low), ntohs(p->high));
			return -EINVAL;
		}
	}

	return 0;
}

static void vxlan_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->version, VXLAN_VERSION, sizeof(drvinfo->version));
	strlcpy(drvinfo->driver, "vxlan", sizeof(drvinfo->driver));
}

static const struct ethtool_ops vxlan_ethtool_ops = {
	.get_drvinfo	= vxlan_get_drvinfo,
	.get_link	= ethtool_op_get_link,
};

static int vxlan_newlink(struct net *net, struct net_device *dev,
			 struct nlattr *tb[], struct nlattr *data[])
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	__u32 vni;
	int err;

	if (!data[IFLA_VXLAN_ID])
		return -EINVAL;

	vni = nla_get_u32(data[IFLA_VXLAN_ID]);
	if (vxlan_find_vni(net, vni)) {
		pr_info("duplicate VNI %u\n", vni);
		return -EEXIST;
	}
	vxlan->vni = vni;

	if (data[IFLA_VXLAN_GROUP])
		vxlan->gaddr = nla_get_be32(data[IFLA_VXLAN_GROUP]);

	if (data[IFLA_VXLAN_LOCAL])
		vxlan->saddr = nla_get_be32(data[IFLA_VXLAN_LOCAL]);

	if (data[IFLA_VXLAN_LINK] &&
	    (vxlan->link = nla_get_u32(data[IFLA_VXLAN_LINK]))) {
		struct net_device *lowerdev
			 = __dev_get_by_index(net, vxlan->link);

		if (!lowerdev) {
			pr_info("ifindex %d does not exist\n", vxlan->link);
			return -ENODEV;
		}

		if (!tb[IFLA_MTU])
			dev->mtu = lowerdev->mtu - VXLAN_HEADROOM;

		/* update header length based on lower device */
		dev->hard_header_len = lowerdev->hard_header_len +
				       VXLAN_HEADROOM;
	}

	if (data[IFLA_VXLAN_TOS])
		vxlan->tos  = nla_get_u8(data[IFLA_VXLAN_TOS]);

	if (data[IFLA_VXLAN_TTL])
		vxlan->ttl = nla_get_u8(data[IFLA_VXLAN_TTL]);

	if (!data[IFLA_VXLAN_LEARNING] || nla_get_u8(data[IFLA_VXLAN_LEARNING]))
		vxlan->flags |= VXLAN_F_LEARN;

	if (data[IFLA_VXLAN_AGEING])
		vxlan->age_interval = nla_get_u32(data[IFLA_VXLAN_AGEING]);
	else
		vxlan->age_interval = FDB_AGE_DEFAULT;

	if (data[IFLA_VXLAN_PROXY] && nla_get_u8(data[IFLA_VXLAN_PROXY]))
		vxlan->flags |= VXLAN_F_PROXY;

	if (data[IFLA_VXLAN_RSC] && nla_get_u8(data[IFLA_VXLAN_RSC]))
		vxlan->flags |= VXLAN_F_RSC;

	if (data[IFLA_VXLAN_L2MISS] && nla_get_u8(data[IFLA_VXLAN_L2MISS]))
		vxlan->flags |= VXLAN_F_L2MISS;

	if (data[IFLA_VXLAN_L3MISS] && nla_get_u8(data[IFLA_VXLAN_L3MISS]))
		vxlan->flags |= VXLAN_F_L3MISS;

	if (data[IFLA_VXLAN_LIMIT])
		vxlan->addrmax = nla_get_u32(data[IFLA_VXLAN_LIMIT]);

	if (data[IFLA_VXLAN_PORT_RANGE]) {
		const struct ifla_vxlan_port_range *p
			= nla_data(data[IFLA_VXLAN_PORT_RANGE]);
		vxlan->port_min = ntohs(p->low);
		vxlan->port_max = ntohs(p->high);
	}

	SET_ETHTOOL_OPS(dev, &vxlan_ethtool_ops);

	err = register_netdevice(dev);
	if (!err)
		hlist_add_head_rcu(&vxlan->hlist, vni_head(net, vxlan->vni));

	return err;
}

static void vxlan_dellink(struct net_device *dev, struct list_head *head)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);

	hlist_del_rcu(&vxlan->hlist);

	unregister_netdevice_queue(dev, head);
}

static size_t vxlan_get_size(const struct net_device *dev)
{

	return nla_total_size(sizeof(__u32)) +	/* IFLA_VXLAN_ID */
		nla_total_size(sizeof(__be32)) +/* IFLA_VXLAN_GROUP */
		nla_total_size(sizeof(__u32)) +	/* IFLA_VXLAN_LINK */
		nla_total_size(sizeof(__be32))+	/* IFLA_VXLAN_LOCAL */
		nla_total_size(sizeof(__u8)) +	/* IFLA_VXLAN_TTL */
		nla_total_size(sizeof(__u8)) +	/* IFLA_VXLAN_TOS */
		nla_total_size(sizeof(__u8)) +	/* IFLA_VXLAN_LEARNING */
		nla_total_size(sizeof(__u8)) +	/* IFLA_VXLAN_PROXY */
		nla_total_size(sizeof(__u8)) +	/* IFLA_VXLAN_RSC */
		nla_total_size(sizeof(__u8)) +	/* IFLA_VXLAN_L2MISS */
		nla_total_size(sizeof(__u8)) +	/* IFLA_VXLAN_L3MISS */
		nla_total_size(sizeof(__u32)) +	/* IFLA_VXLAN_AGEING */
		nla_total_size(sizeof(__u32)) +	/* IFLA_VXLAN_LIMIT */
		nla_total_size(sizeof(struct ifla_vxlan_port_range)) +
		0;
}

static int vxlan_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	const struct vxlan_dev *vxlan = netdev_priv(dev);
	struct ifla_vxlan_port_range ports = {
		.low =  htons(vxlan->port_min),
		.high = htons(vxlan->port_max),
	};

	if (nla_put_u32(skb, IFLA_VXLAN_ID, vxlan->vni))
		goto nla_put_failure;

	if (vxlan->gaddr && nla_put_be32(skb, IFLA_VXLAN_GROUP, vxlan->gaddr))
		goto nla_put_failure;

	if (vxlan->link && nla_put_u32(skb, IFLA_VXLAN_LINK, vxlan->link))
		goto nla_put_failure;

	if (vxlan->saddr && nla_put_be32(skb, IFLA_VXLAN_LOCAL, vxlan->saddr))
		goto nla_put_failure;

	if (nla_put_u8(skb, IFLA_VXLAN_TTL, vxlan->ttl) ||
	    nla_put_u8(skb, IFLA_VXLAN_TOS, vxlan->tos) ||
	    nla_put_u8(skb, IFLA_VXLAN_LEARNING,
			!!(vxlan->flags & VXLAN_F_LEARN)) ||
	    nla_put_u8(skb, IFLA_VXLAN_PROXY,
			!!(vxlan->flags & VXLAN_F_PROXY)) ||
	    nla_put_u8(skb, IFLA_VXLAN_RSC, !!(vxlan->flags & VXLAN_F_RSC)) ||
	    nla_put_u8(skb, IFLA_VXLAN_L2MISS,
			!!(vxlan->flags & VXLAN_F_L2MISS)) ||
	    nla_put_u8(skb, IFLA_VXLAN_L3MISS,
			!!(vxlan->flags & VXLAN_F_L3MISS)) ||
	    nla_put_u32(skb, IFLA_VXLAN_AGEING, vxlan->age_interval) ||
	    nla_put_u32(skb, IFLA_VXLAN_LIMIT, vxlan->addrmax))
		goto nla_put_failure;

	if (nla_put(skb, IFLA_VXLAN_PORT_RANGE, sizeof(ports), &ports))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static struct rtnl_link_ops vxlan_link_ops __read_mostly = {
	.kind		= "vxlan",
	.maxtype	= IFLA_VXLAN_MAX,
	.policy		= vxlan_policy,
	.priv_size	= sizeof(struct vxlan_dev),
	.setup		= vxlan_setup,
	.validate	= vxlan_validate,
	.newlink	= vxlan_newlink,
	.dellink	= vxlan_dellink,
	.get_size	= vxlan_get_size,
	.fill_info	= vxlan_fill_info,
};

static __net_init int vxlan_init_net(struct net *net)
{
	struct vxlan_net *vn = net_generic(net, vxlan_net_id);
	struct sock *sk;
	struct sockaddr_in vxlan_addr = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};
	int rc;
	unsigned h;

	/* Create UDP socket for encapsulation receive. */
	rc = sock_create_kern(AF_INET, SOCK_DGRAM, IPPROTO_UDP, &vn->sock);
	if (rc < 0) {
		pr_debug("UDP socket create failed\n");
		return rc;
	}
	/* Put in proper namespace */
	sk = vn->sock->sk;
	sk_change_net(sk, net);

	vxlan_addr.sin_port = htons(vxlan_port);

	rc = kernel_bind(vn->sock, (struct sockaddr *) &vxlan_addr,
			 sizeof(vxlan_addr));
	if (rc < 0) {
		pr_debug("bind for UDP socket %pI4:%u (%d)\n",
			 &vxlan_addr.sin_addr, ntohs(vxlan_addr.sin_port), rc);
		sk_release_kernel(sk);
		vn->sock = NULL;
		return rc;
	}

	/* Disable multicast loopback */
	inet_sk(sk)->mc_loop = 0;

	/* Mark socket as an encapsulation socket. */
	udp_sk(sk)->encap_type = 1;
	udp_sk(sk)->encap_rcv = vxlan_udp_encap_recv;
	udp_encap_enable();

	for (h = 0; h < VNI_HASH_SIZE; ++h)
		INIT_HLIST_HEAD(&vn->vni_list[h]);

	return 0;
}

static __net_exit void vxlan_exit_net(struct net *net)
{
	struct vxlan_net *vn = net_generic(net, vxlan_net_id);

	if (vn->sock) {
		sk_release_kernel(vn->sock->sk);
		vn->sock = NULL;
	}
}

static struct pernet_operations vxlan_net_ops = {
	.init = vxlan_init_net,
	.exit = vxlan_exit_net,
	.id   = &vxlan_net_id,
	.size = sizeof(struct vxlan_net),
};

static int __init vxlan_init_module(void)
{
	int rc;

	get_random_bytes(&vxlan_salt, sizeof(vxlan_salt));

	rc = register_pernet_device(&vxlan_net_ops);
	if (rc)
		goto out1;

	rc = rtnl_link_register(&vxlan_link_ops);
	if (rc)
		goto out2;

	return 0;

out2:
	unregister_pernet_device(&vxlan_net_ops);
out1:
	return rc;
}
module_init(vxlan_init_module);

static void __exit vxlan_cleanup_module(void)
{
	rtnl_link_unregister(&vxlan_link_ops);
	unregister_pernet_device(&vxlan_net_ops);
}
module_exit(vxlan_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_VERSION(VXLAN_VERSION);
MODULE_AUTHOR("Stephen Hemminger <shemminger@vyatta.com>");
MODULE_ALIAS_RTNL_LINK("vxlan");
