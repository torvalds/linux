// SPDX-License-Identifier: GPL-2.0-only
/*
 * VXLAN: Virtual eXtensible Local Area Network
 *
 * Copyright (c) 2012-2013 Vyatta Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/udp.h>
#include <linux/igmp.h>
#include <linux/if_ether.h>
#include <linux/ethtool.h>
#include <net/arp.h>
#include <net/ndisc.h>
#include <net/gro.h>
#include <net/ipv6_stubs.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/rtnetlink.h>
#include <net/inet_ecn.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/tun_proto.h>
#include <net/vxlan.h>
#include <net/nexthop.h>

#if IS_ENABLED(CONFIG_IPV6)
#include <net/ip6_tunnel.h>
#include <net/ip6_checksum.h>
#endif

#include "vxlan_private.h"

#define VXLAN_VERSION	"0.1"

#define FDB_AGE_DEFAULT 300 /* 5 min */
#define FDB_AGE_INTERVAL (10 * HZ)	/* rescan interval */

/* UDP port for VXLAN traffic.
 * The IANA assigned port is 4789, but the Linux default is 8472
 * for compatibility with early adopters.
 */
static unsigned short vxlan_port __read_mostly = 8472;
module_param_named(udp_port, vxlan_port, ushort, 0444);
MODULE_PARM_DESC(udp_port, "Destination UDP port");

static bool log_ecn_error = true;
module_param(log_ecn_error, bool, 0644);
MODULE_PARM_DESC(log_ecn_error, "Log packets received with corrupted ECN");

unsigned int vxlan_net_id;

const u8 all_zeros_mac[ETH_ALEN + 2];
static struct rtnl_link_ops vxlan_link_ops;

static int vxlan_sock_add(struct vxlan_dev *vxlan);

static void vxlan_vs_del_dev(struct vxlan_dev *vxlan);

/* salt for hash table */
static u32 vxlan_salt __read_mostly;

static inline bool vxlan_collect_metadata(struct vxlan_sock *vs)
{
	return vs->flags & VXLAN_F_COLLECT_METADATA ||
	       ip_tunnel_collect_metadata();
}

/* Find VXLAN socket based on network namespace, address family, UDP port,
 * enabled unshareable flags and socket device binding (see l3mdev with
 * non-default VRF).
 */
static struct vxlan_sock *vxlan_find_sock(struct net *net, sa_family_t family,
					  __be16 port, u32 flags, int ifindex)
{
	struct vxlan_sock *vs;

	flags &= VXLAN_F_RCV_FLAGS;

	hlist_for_each_entry_rcu(vs, vs_head(net, port), hlist) {
		if (inet_sk(vs->sock->sk)->inet_sport == port &&
		    vxlan_get_sk_family(vs) == family &&
		    vs->flags == flags &&
		    vs->sock->sk->sk_bound_dev_if == ifindex)
			return vs;
	}
	return NULL;
}

static struct vxlan_dev *vxlan_vs_find_vni(struct vxlan_sock *vs,
					   int ifindex, __be32 vni,
					   struct vxlan_vni_node **vninode)
{
	struct vxlan_vni_node *vnode;
	struct vxlan_dev_node *node;

	/* For flow based devices, map all packets to VNI 0 */
	if (vs->flags & VXLAN_F_COLLECT_METADATA &&
	    !(vs->flags & VXLAN_F_VNIFILTER))
		vni = 0;

	hlist_for_each_entry_rcu(node, vni_head(vs, vni), hlist) {
		if (!node->vxlan)
			continue;
		vnode = NULL;
		if (node->vxlan->cfg.flags & VXLAN_F_VNIFILTER) {
			vnode = vxlan_vnifilter_lookup(node->vxlan, vni);
			if (!vnode)
				continue;
		} else if (node->vxlan->default_dst.remote_vni != vni) {
			continue;
		}

		if (IS_ENABLED(CONFIG_IPV6)) {
			const struct vxlan_config *cfg = &node->vxlan->cfg;

			if ((cfg->flags & VXLAN_F_IPV6_LINKLOCAL) &&
			    cfg->remote_ifindex != ifindex)
				continue;
		}

		if (vninode)
			*vninode = vnode;
		return node->vxlan;
	}

	return NULL;
}

/* Look up VNI in a per net namespace table */
static struct vxlan_dev *vxlan_find_vni(struct net *net, int ifindex,
					__be32 vni, sa_family_t family,
					__be16 port, u32 flags)
{
	struct vxlan_sock *vs;

	vs = vxlan_find_sock(net, family, port, flags, ifindex);
	if (!vs)
		return NULL;

	return vxlan_vs_find_vni(vs, ifindex, vni, NULL);
}

/* Fill in neighbour message in skbuff. */
static int vxlan_fdb_info(struct sk_buff *skb, struct vxlan_dev *vxlan,
			  const struct vxlan_fdb *fdb,
			  u32 portid, u32 seq, int type, unsigned int flags,
			  const struct vxlan_rdst *rdst)
{
	unsigned long now = jiffies;
	struct nda_cacheinfo ci;
	bool send_ip, send_eth;
	struct nlmsghdr *nlh;
	struct nexthop *nh;
	struct ndmsg *ndm;
	int nh_family;
	u32 nh_id;

	nlh = nlmsg_put(skb, portid, seq, type, sizeof(*ndm), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	ndm = nlmsg_data(nlh);
	memset(ndm, 0, sizeof(*ndm));

	send_eth = send_ip = true;

	rcu_read_lock();
	nh = rcu_dereference(fdb->nh);
	if (nh) {
		nh_family = nexthop_get_family(nh);
		nh_id = nh->id;
	}
	rcu_read_unlock();

	if (type == RTM_GETNEIGH) {
		if (rdst) {
			send_ip = !vxlan_addr_any(&rdst->remote_ip);
			ndm->ndm_family = send_ip ? rdst->remote_ip.sa.sa_family : AF_INET;
		} else if (nh) {
			ndm->ndm_family = nh_family;
		}
		send_eth = !is_zero_ether_addr(fdb->eth_addr);
	} else
		ndm->ndm_family	= AF_BRIDGE;
	ndm->ndm_state = fdb->state;
	ndm->ndm_ifindex = vxlan->dev->ifindex;
	ndm->ndm_flags = fdb->flags;
	if (rdst && rdst->offloaded)
		ndm->ndm_flags |= NTF_OFFLOADED;
	ndm->ndm_type = RTN_UNICAST;

	if (!net_eq(dev_net(vxlan->dev), vxlan->net) &&
	    nla_put_s32(skb, NDA_LINK_NETNSID,
			peernet2id(dev_net(vxlan->dev), vxlan->net)))
		goto nla_put_failure;

	if (send_eth && nla_put(skb, NDA_LLADDR, ETH_ALEN, &fdb->eth_addr))
		goto nla_put_failure;
	if (nh) {
		if (nla_put_u32(skb, NDA_NH_ID, nh_id))
			goto nla_put_failure;
	} else if (rdst) {
		if (send_ip && vxlan_nla_put_addr(skb, NDA_DST,
						  &rdst->remote_ip))
			goto nla_put_failure;

		if (rdst->remote_port &&
		    rdst->remote_port != vxlan->cfg.dst_port &&
		    nla_put_be16(skb, NDA_PORT, rdst->remote_port))
			goto nla_put_failure;
		if (rdst->remote_vni != vxlan->default_dst.remote_vni &&
		    nla_put_u32(skb, NDA_VNI, be32_to_cpu(rdst->remote_vni)))
			goto nla_put_failure;
		if (rdst->remote_ifindex &&
		    nla_put_u32(skb, NDA_IFINDEX, rdst->remote_ifindex))
			goto nla_put_failure;
	}

	if ((vxlan->cfg.flags & VXLAN_F_COLLECT_METADATA) && fdb->vni &&
	    nla_put_u32(skb, NDA_SRC_VNI,
			be32_to_cpu(fdb->vni)))
		goto nla_put_failure;

	ci.ndm_used	 = jiffies_to_clock_t(now - fdb->used);
	ci.ndm_confirmed = 0;
	ci.ndm_updated	 = jiffies_to_clock_t(now - fdb->updated);
	ci.ndm_refcnt	 = 0;

	if (nla_put(skb, NDA_CACHEINFO, sizeof(ci), &ci))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static inline size_t vxlan_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct ndmsg))
		+ nla_total_size(ETH_ALEN) /* NDA_LLADDR */
		+ nla_total_size(sizeof(struct in6_addr)) /* NDA_DST */
		+ nla_total_size(sizeof(__be16)) /* NDA_PORT */
		+ nla_total_size(sizeof(__be32)) /* NDA_VNI */
		+ nla_total_size(sizeof(__u32)) /* NDA_IFINDEX */
		+ nla_total_size(sizeof(__s32)) /* NDA_LINK_NETNSID */
		+ nla_total_size(sizeof(struct nda_cacheinfo));
}

static void __vxlan_fdb_notify(struct vxlan_dev *vxlan, struct vxlan_fdb *fdb,
			       struct vxlan_rdst *rd, int type)
{
	struct net *net = dev_net(vxlan->dev);
	struct sk_buff *skb;
	int err = -ENOBUFS;

	skb = nlmsg_new(vxlan_nlmsg_size(), GFP_ATOMIC);
	if (skb == NULL)
		goto errout;

	err = vxlan_fdb_info(skb, vxlan, fdb, 0, 0, type, 0, rd);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in vxlan_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}

	rtnl_notify(skb, net, 0, RTNLGRP_NEIGH, NULL, GFP_ATOMIC);
	return;
errout:
	rtnl_set_sk_err(net, RTNLGRP_NEIGH, err);
}

static void vxlan_fdb_switchdev_notifier_info(const struct vxlan_dev *vxlan,
			    const struct vxlan_fdb *fdb,
			    const struct vxlan_rdst *rd,
			    struct netlink_ext_ack *extack,
			    struct switchdev_notifier_vxlan_fdb_info *fdb_info)
{
	fdb_info->info.dev = vxlan->dev;
	fdb_info->info.extack = extack;
	fdb_info->remote_ip = rd->remote_ip;
	fdb_info->remote_port = rd->remote_port;
	fdb_info->remote_vni = rd->remote_vni;
	fdb_info->remote_ifindex = rd->remote_ifindex;
	memcpy(fdb_info->eth_addr, fdb->eth_addr, ETH_ALEN);
	fdb_info->vni = fdb->vni;
	fdb_info->offloaded = rd->offloaded;
	fdb_info->added_by_user = fdb->flags & NTF_VXLAN_ADDED_BY_USER;
}

static int vxlan_fdb_switchdev_call_notifiers(struct vxlan_dev *vxlan,
					      struct vxlan_fdb *fdb,
					      struct vxlan_rdst *rd,
					      bool adding,
					      struct netlink_ext_ack *extack)
{
	struct switchdev_notifier_vxlan_fdb_info info;
	enum switchdev_notifier_type notifier_type;
	int ret;

	if (WARN_ON(!rd))
		return 0;

	notifier_type = adding ? SWITCHDEV_VXLAN_FDB_ADD_TO_DEVICE
			       : SWITCHDEV_VXLAN_FDB_DEL_TO_DEVICE;
	vxlan_fdb_switchdev_notifier_info(vxlan, fdb, rd, NULL, &info);
	ret = call_switchdev_notifiers(notifier_type, vxlan->dev,
				       &info.info, extack);
	return notifier_to_errno(ret);
}

static int vxlan_fdb_notify(struct vxlan_dev *vxlan, struct vxlan_fdb *fdb,
			    struct vxlan_rdst *rd, int type, bool swdev_notify,
			    struct netlink_ext_ack *extack)
{
	int err;

	if (swdev_notify && rd) {
		switch (type) {
		case RTM_NEWNEIGH:
			err = vxlan_fdb_switchdev_call_notifiers(vxlan, fdb, rd,
								 true, extack);
			if (err)
				return err;
			break;
		case RTM_DELNEIGH:
			vxlan_fdb_switchdev_call_notifiers(vxlan, fdb, rd,
							   false, extack);
			break;
		}
	}

	__vxlan_fdb_notify(vxlan, fdb, rd, type);
	return 0;
}

static void vxlan_ip_miss(struct net_device *dev, union vxlan_addr *ipa)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct vxlan_fdb f = {
		.state = NUD_STALE,
	};
	struct vxlan_rdst remote = {
		.remote_ip = *ipa, /* goes to NDA_DST */
		.remote_vni = cpu_to_be32(VXLAN_N_VID),
	};

	vxlan_fdb_notify(vxlan, &f, &remote, RTM_GETNEIGH, true, NULL);
}

static void vxlan_fdb_miss(struct vxlan_dev *vxlan, const u8 eth_addr[ETH_ALEN])
{
	struct vxlan_fdb f = {
		.state = NUD_STALE,
	};
	struct vxlan_rdst remote = { };

	memcpy(f.eth_addr, eth_addr, ETH_ALEN);

	vxlan_fdb_notify(vxlan, &f, &remote, RTM_GETNEIGH, true, NULL);
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

u32 eth_vni_hash(const unsigned char *addr, __be32 vni)
{
	/* use 1 byte of OUI and 3 bytes of NIC */
	u32 key = get_unaligned((u32 *)(addr + 2));

	return jhash_2words(key, vni, vxlan_salt) & (FDB_HASH_SIZE - 1);
}

u32 fdb_head_index(struct vxlan_dev *vxlan, const u8 *mac, __be32 vni)
{
	if (vxlan->cfg.flags & VXLAN_F_COLLECT_METADATA)
		return eth_vni_hash(mac, vni);
	else
		return eth_hash(mac);
}

/* Hash chain to use given mac address */
static inline struct hlist_head *vxlan_fdb_head(struct vxlan_dev *vxlan,
						const u8 *mac, __be32 vni)
{
	return &vxlan->fdb_head[fdb_head_index(vxlan, mac, vni)];
}

/* Look up Ethernet address in forwarding table */
static struct vxlan_fdb *__vxlan_find_mac(struct vxlan_dev *vxlan,
					  const u8 *mac, __be32 vni)
{
	struct hlist_head *head = vxlan_fdb_head(vxlan, mac, vni);
	struct vxlan_fdb *f;

	hlist_for_each_entry_rcu(f, head, hlist) {
		if (ether_addr_equal(mac, f->eth_addr)) {
			if (vxlan->cfg.flags & VXLAN_F_COLLECT_METADATA) {
				if (vni == f->vni)
					return f;
			} else {
				return f;
			}
		}
	}

	return NULL;
}

static struct vxlan_fdb *vxlan_find_mac(struct vxlan_dev *vxlan,
					const u8 *mac, __be32 vni)
{
	struct vxlan_fdb *f;

	f = __vxlan_find_mac(vxlan, mac, vni);
	if (f && f->used != jiffies)
		f->used = jiffies;

	return f;
}

/* caller should hold vxlan->hash_lock */
static struct vxlan_rdst *vxlan_fdb_find_rdst(struct vxlan_fdb *f,
					      union vxlan_addr *ip, __be16 port,
					      __be32 vni, __u32 ifindex)
{
	struct vxlan_rdst *rd;

	list_for_each_entry(rd, &f->remotes, list) {
		if (vxlan_addr_equal(&rd->remote_ip, ip) &&
		    rd->remote_port == port &&
		    rd->remote_vni == vni &&
		    rd->remote_ifindex == ifindex)
			return rd;
	}

	return NULL;
}

int vxlan_fdb_find_uc(struct net_device *dev, const u8 *mac, __be32 vni,
		      struct switchdev_notifier_vxlan_fdb_info *fdb_info)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	u8 eth_addr[ETH_ALEN + 2] = { 0 };
	struct vxlan_rdst *rdst;
	struct vxlan_fdb *f;
	int rc = 0;

	if (is_multicast_ether_addr(mac) ||
	    is_zero_ether_addr(mac))
		return -EINVAL;

	ether_addr_copy(eth_addr, mac);

	rcu_read_lock();

	f = __vxlan_find_mac(vxlan, eth_addr, vni);
	if (!f) {
		rc = -ENOENT;
		goto out;
	}

	rdst = first_remote_rcu(f);
	vxlan_fdb_switchdev_notifier_info(vxlan, f, rdst, NULL, fdb_info);

out:
	rcu_read_unlock();
	return rc;
}
EXPORT_SYMBOL_GPL(vxlan_fdb_find_uc);

static int vxlan_fdb_notify_one(struct notifier_block *nb,
				const struct vxlan_dev *vxlan,
				const struct vxlan_fdb *f,
				const struct vxlan_rdst *rdst,
				struct netlink_ext_ack *extack)
{
	struct switchdev_notifier_vxlan_fdb_info fdb_info;
	int rc;

	vxlan_fdb_switchdev_notifier_info(vxlan, f, rdst, extack, &fdb_info);
	rc = nb->notifier_call(nb, SWITCHDEV_VXLAN_FDB_ADD_TO_DEVICE,
			       &fdb_info);
	return notifier_to_errno(rc);
}

int vxlan_fdb_replay(const struct net_device *dev, __be32 vni,
		     struct notifier_block *nb,
		     struct netlink_ext_ack *extack)
{
	struct vxlan_dev *vxlan;
	struct vxlan_rdst *rdst;
	struct vxlan_fdb *f;
	unsigned int h;
	int rc = 0;

	if (!netif_is_vxlan(dev))
		return -EINVAL;
	vxlan = netdev_priv(dev);

	for (h = 0; h < FDB_HASH_SIZE; ++h) {
		spin_lock_bh(&vxlan->hash_lock[h]);
		hlist_for_each_entry(f, &vxlan->fdb_head[h], hlist) {
			if (f->vni == vni) {
				list_for_each_entry(rdst, &f->remotes, list) {
					rc = vxlan_fdb_notify_one(nb, vxlan,
								  f, rdst,
								  extack);
					if (rc)
						goto unlock;
				}
			}
		}
		spin_unlock_bh(&vxlan->hash_lock[h]);
	}
	return 0;

unlock:
	spin_unlock_bh(&vxlan->hash_lock[h]);
	return rc;
}
EXPORT_SYMBOL_GPL(vxlan_fdb_replay);

void vxlan_fdb_clear_offload(const struct net_device *dev, __be32 vni)
{
	struct vxlan_dev *vxlan;
	struct vxlan_rdst *rdst;
	struct vxlan_fdb *f;
	unsigned int h;

	if (!netif_is_vxlan(dev))
		return;
	vxlan = netdev_priv(dev);

	for (h = 0; h < FDB_HASH_SIZE; ++h) {
		spin_lock_bh(&vxlan->hash_lock[h]);
		hlist_for_each_entry(f, &vxlan->fdb_head[h], hlist)
			if (f->vni == vni)
				list_for_each_entry(rdst, &f->remotes, list)
					rdst->offloaded = false;
		spin_unlock_bh(&vxlan->hash_lock[h]);
	}

}
EXPORT_SYMBOL_GPL(vxlan_fdb_clear_offload);

/* Replace destination of unicast mac */
static int vxlan_fdb_replace(struct vxlan_fdb *f,
			     union vxlan_addr *ip, __be16 port, __be32 vni,
			     __u32 ifindex, struct vxlan_rdst *oldrd)
{
	struct vxlan_rdst *rd;

	rd = vxlan_fdb_find_rdst(f, ip, port, vni, ifindex);
	if (rd)
		return 0;

	rd = list_first_entry_or_null(&f->remotes, struct vxlan_rdst, list);
	if (!rd)
		return 0;

	*oldrd = *rd;
	dst_cache_reset(&rd->dst_cache);
	rd->remote_ip = *ip;
	rd->remote_port = port;
	rd->remote_vni = vni;
	rd->remote_ifindex = ifindex;
	rd->offloaded = false;
	return 1;
}

/* Add/update destinations for multicast */
static int vxlan_fdb_append(struct vxlan_fdb *f,
			    union vxlan_addr *ip, __be16 port, __be32 vni,
			    __u32 ifindex, struct vxlan_rdst **rdp)
{
	struct vxlan_rdst *rd;

	rd = vxlan_fdb_find_rdst(f, ip, port, vni, ifindex);
	if (rd)
		return 0;

	rd = kmalloc(sizeof(*rd), GFP_ATOMIC);
	if (rd == NULL)
		return -ENOMEM;

	if (dst_cache_init(&rd->dst_cache, GFP_ATOMIC)) {
		kfree(rd);
		return -ENOMEM;
	}

	rd->remote_ip = *ip;
	rd->remote_port = port;
	rd->offloaded = false;
	rd->remote_vni = vni;
	rd->remote_ifindex = ifindex;

	list_add_tail_rcu(&rd->list, &f->remotes);

	*rdp = rd;
	return 1;
}

static bool vxlan_parse_gpe_proto(struct vxlanhdr *hdr, __be16 *protocol)
{
	struct vxlanhdr_gpe *gpe = (struct vxlanhdr_gpe *)hdr;

	/* Need to have Next Protocol set for interfaces in GPE mode. */
	if (!gpe->np_applied)
		return false;
	/* "The initial version is 0. If a receiver does not support the
	 * version indicated it MUST drop the packet.
	 */
	if (gpe->version != 0)
		return false;
	/* "When the O bit is set to 1, the packet is an OAM packet and OAM
	 * processing MUST occur." However, we don't implement OAM
	 * processing, thus drop the packet.
	 */
	if (gpe->oam_flag)
		return false;

	*protocol = tun_p_to_eth_p(gpe->next_protocol);
	if (!*protocol)
		return false;

	return true;
}

static struct vxlanhdr *vxlan_gro_remcsum(struct sk_buff *skb,
					  unsigned int off,
					  struct vxlanhdr *vh, size_t hdrlen,
					  __be32 vni_field,
					  struct gro_remcsum *grc,
					  bool nopartial)
{
	size_t start, offset;

	if (skb->remcsum_offload)
		return vh;

	if (!NAPI_GRO_CB(skb)->csum_valid)
		return NULL;

	start = vxlan_rco_start(vni_field);
	offset = start + vxlan_rco_offset(vni_field);

	vh = skb_gro_remcsum_process(skb, (void *)vh, off, hdrlen,
				     start, offset, grc, nopartial);

	skb->remcsum_offload = 1;

	return vh;
}

static struct vxlanhdr *vxlan_gro_prepare_receive(struct sock *sk,
						  struct list_head *head,
						  struct sk_buff *skb,
						  struct gro_remcsum *grc)
{
	struct sk_buff *p;
	struct vxlanhdr *vh, *vh2;
	unsigned int hlen, off_vx;
	struct vxlan_sock *vs = rcu_dereference_sk_user_data(sk);
	__be32 flags;

	skb_gro_remcsum_init(grc);

	off_vx = skb_gro_offset(skb);
	hlen = off_vx + sizeof(*vh);
	vh = skb_gro_header(skb, hlen, off_vx);
	if (unlikely(!vh))
		return NULL;

	skb_gro_postpull_rcsum(skb, vh, sizeof(struct vxlanhdr));

	flags = vh->vx_flags;

	if ((flags & VXLAN_HF_RCO) && (vs->flags & VXLAN_F_REMCSUM_RX)) {
		vh = vxlan_gro_remcsum(skb, off_vx, vh, sizeof(struct vxlanhdr),
				       vh->vx_vni, grc,
				       !!(vs->flags &
					  VXLAN_F_REMCSUM_NOPARTIAL));

		if (!vh)
			return NULL;
	}

	skb_gro_pull(skb, sizeof(struct vxlanhdr)); /* pull vxlan header */

	list_for_each_entry(p, head, list) {
		if (!NAPI_GRO_CB(p)->same_flow)
			continue;

		vh2 = (struct vxlanhdr *)(p->data + off_vx);
		if (vh->vx_flags != vh2->vx_flags ||
		    vh->vx_vni != vh2->vx_vni) {
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}
	}

	return vh;
}

static struct sk_buff *vxlan_gro_receive(struct sock *sk,
					 struct list_head *head,
					 struct sk_buff *skb)
{
	struct sk_buff *pp = NULL;
	struct gro_remcsum grc;
	int flush = 1;

	if (vxlan_gro_prepare_receive(sk, head, skb, &grc)) {
		pp = call_gro_receive(eth_gro_receive, head, skb);
		flush = 0;
	}
	skb_gro_flush_final_remcsum(skb, pp, flush, &grc);
	return pp;
}

static struct sk_buff *vxlan_gpe_gro_receive(struct sock *sk,
					     struct list_head *head,
					     struct sk_buff *skb)
{
	const struct packet_offload *ptype;
	struct sk_buff *pp = NULL;
	struct gro_remcsum grc;
	struct vxlanhdr *vh;
	__be16 protocol;
	int flush = 1;

	vh = vxlan_gro_prepare_receive(sk, head, skb, &grc);
	if (vh) {
		if (!vxlan_parse_gpe_proto(vh, &protocol))
			goto out;
		ptype = gro_find_receive_by_type(protocol);
		if (!ptype)
			goto out;
		pp = call_gro_receive(ptype->callbacks.gro_receive, head, skb);
		flush = 0;
	}
out:
	skb_gro_flush_final_remcsum(skb, pp, flush, &grc);
	return pp;
}

static int vxlan_gro_complete(struct sock *sk, struct sk_buff *skb, int nhoff)
{
	/* Sets 'skb->inner_mac_header' since we are always called with
	 * 'skb->encapsulation' set.
	 */
	return eth_gro_complete(skb, nhoff + sizeof(struct vxlanhdr));
}

static int vxlan_gpe_gro_complete(struct sock *sk, struct sk_buff *skb, int nhoff)
{
	struct vxlanhdr *vh = (struct vxlanhdr *)(skb->data + nhoff);
	const struct packet_offload *ptype;
	int err = -ENOSYS;
	__be16 protocol;

	if (!vxlan_parse_gpe_proto(vh, &protocol))
		return err;
	ptype = gro_find_complete_by_type(protocol);
	if (ptype)
		err = ptype->callbacks.gro_complete(skb, nhoff + sizeof(struct vxlanhdr));
	return err;
}

static struct vxlan_fdb *vxlan_fdb_alloc(struct vxlan_dev *vxlan, const u8 *mac,
					 __u16 state, __be32 src_vni,
					 __u16 ndm_flags)
{
	struct vxlan_fdb *f;

	f = kmalloc(sizeof(*f), GFP_ATOMIC);
	if (!f)
		return NULL;
	f->state = state;
	f->flags = ndm_flags;
	f->updated = f->used = jiffies;
	f->vni = src_vni;
	f->nh = NULL;
	RCU_INIT_POINTER(f->vdev, vxlan);
	INIT_LIST_HEAD(&f->nh_list);
	INIT_LIST_HEAD(&f->remotes);
	memcpy(f->eth_addr, mac, ETH_ALEN);

	return f;
}

static void vxlan_fdb_insert(struct vxlan_dev *vxlan, const u8 *mac,
			     __be32 src_vni, struct vxlan_fdb *f)
{
	++vxlan->addrcnt;
	hlist_add_head_rcu(&f->hlist,
			   vxlan_fdb_head(vxlan, mac, src_vni));
}

static int vxlan_fdb_nh_update(struct vxlan_dev *vxlan, struct vxlan_fdb *fdb,
			       u32 nhid, struct netlink_ext_ack *extack)
{
	struct nexthop *old_nh = rtnl_dereference(fdb->nh);
	struct nexthop *nh;
	int err = -EINVAL;

	if (old_nh && old_nh->id == nhid)
		return 0;

	nh = nexthop_find_by_id(vxlan->net, nhid);
	if (!nh) {
		NL_SET_ERR_MSG(extack, "Nexthop id does not exist");
		goto err_inval;
	}

	if (!nexthop_get(nh)) {
		NL_SET_ERR_MSG(extack, "Nexthop has been deleted");
		nh = NULL;
		goto err_inval;
	}
	if (!nexthop_is_fdb(nh)) {
		NL_SET_ERR_MSG(extack, "Nexthop is not a fdb nexthop");
		goto err_inval;
	}

	if (!nexthop_is_multipath(nh)) {
		NL_SET_ERR_MSG(extack, "Nexthop is not a multipath group");
		goto err_inval;
	}

	/* check nexthop group family */
	switch (vxlan->default_dst.remote_ip.sa.sa_family) {
	case AF_INET:
		if (!nexthop_has_v4(nh)) {
			err = -EAFNOSUPPORT;
			NL_SET_ERR_MSG(extack, "Nexthop group family not supported");
			goto err_inval;
		}
		break;
	case AF_INET6:
		if (nexthop_has_v4(nh)) {
			err = -EAFNOSUPPORT;
			NL_SET_ERR_MSG(extack, "Nexthop group family not supported");
			goto err_inval;
		}
	}

	if (old_nh) {
		list_del_rcu(&fdb->nh_list);
		nexthop_put(old_nh);
	}
	rcu_assign_pointer(fdb->nh, nh);
	list_add_tail_rcu(&fdb->nh_list, &nh->fdb_list);
	return 1;

err_inval:
	if (nh)
		nexthop_put(nh);
	return err;
}

int vxlan_fdb_create(struct vxlan_dev *vxlan,
		     const u8 *mac, union vxlan_addr *ip,
		     __u16 state, __be16 port, __be32 src_vni,
		     __be32 vni, __u32 ifindex, __u16 ndm_flags,
		     u32 nhid, struct vxlan_fdb **fdb,
		     struct netlink_ext_ack *extack)
{
	struct vxlan_rdst *rd = NULL;
	struct vxlan_fdb *f;
	int rc;

	if (vxlan->cfg.addrmax &&
	    vxlan->addrcnt >= vxlan->cfg.addrmax)
		return -ENOSPC;

	netdev_dbg(vxlan->dev, "add %pM -> %pIS\n", mac, ip);
	f = vxlan_fdb_alloc(vxlan, mac, state, src_vni, ndm_flags);
	if (!f)
		return -ENOMEM;

	if (nhid)
		rc = vxlan_fdb_nh_update(vxlan, f, nhid, extack);
	else
		rc = vxlan_fdb_append(f, ip, port, vni, ifindex, &rd);
	if (rc < 0)
		goto errout;

	*fdb = f;

	return 0;

errout:
	kfree(f);
	return rc;
}

static void __vxlan_fdb_free(struct vxlan_fdb *f)
{
	struct vxlan_rdst *rd, *nd;
	struct nexthop *nh;

	nh = rcu_dereference_raw(f->nh);
	if (nh) {
		rcu_assign_pointer(f->nh, NULL);
		rcu_assign_pointer(f->vdev, NULL);
		nexthop_put(nh);
	}

	list_for_each_entry_safe(rd, nd, &f->remotes, list) {
		dst_cache_destroy(&rd->dst_cache);
		kfree(rd);
	}
	kfree(f);
}

static void vxlan_fdb_free(struct rcu_head *head)
{
	struct vxlan_fdb *f = container_of(head, struct vxlan_fdb, rcu);

	__vxlan_fdb_free(f);
}

static void vxlan_fdb_destroy(struct vxlan_dev *vxlan, struct vxlan_fdb *f,
			      bool do_notify, bool swdev_notify)
{
	struct vxlan_rdst *rd;

	netdev_dbg(vxlan->dev, "delete %pM\n", f->eth_addr);

	--vxlan->addrcnt;
	if (do_notify) {
		if (rcu_access_pointer(f->nh))
			vxlan_fdb_notify(vxlan, f, NULL, RTM_DELNEIGH,
					 swdev_notify, NULL);
		else
			list_for_each_entry(rd, &f->remotes, list)
				vxlan_fdb_notify(vxlan, f, rd, RTM_DELNEIGH,
						 swdev_notify, NULL);
	}

	hlist_del_rcu(&f->hlist);
	list_del_rcu(&f->nh_list);
	call_rcu(&f->rcu, vxlan_fdb_free);
}

static void vxlan_dst_free(struct rcu_head *head)
{
	struct vxlan_rdst *rd = container_of(head, struct vxlan_rdst, rcu);

	dst_cache_destroy(&rd->dst_cache);
	kfree(rd);
}

static int vxlan_fdb_update_existing(struct vxlan_dev *vxlan,
				     union vxlan_addr *ip,
				     __u16 state, __u16 flags,
				     __be16 port, __be32 vni,
				     __u32 ifindex, __u16 ndm_flags,
				     struct vxlan_fdb *f, u32 nhid,
				     bool swdev_notify,
				     struct netlink_ext_ack *extack)
{
	__u16 fdb_flags = (ndm_flags & ~NTF_USE);
	struct vxlan_rdst *rd = NULL;
	struct vxlan_rdst oldrd;
	int notify = 0;
	int rc = 0;
	int err;

	if (nhid && !rcu_access_pointer(f->nh)) {
		NL_SET_ERR_MSG(extack,
			       "Cannot replace an existing non nexthop fdb with a nexthop");
		return -EOPNOTSUPP;
	}

	if (nhid && (flags & NLM_F_APPEND)) {
		NL_SET_ERR_MSG(extack,
			       "Cannot append to a nexthop fdb");
		return -EOPNOTSUPP;
	}

	/* Do not allow an externally learned entry to take over an entry added
	 * by the user.
	 */
	if (!(fdb_flags & NTF_EXT_LEARNED) ||
	    !(f->flags & NTF_VXLAN_ADDED_BY_USER)) {
		if (f->state != state) {
			f->state = state;
			f->updated = jiffies;
			notify = 1;
		}
		if (f->flags != fdb_flags) {
			f->flags = fdb_flags;
			f->updated = jiffies;
			notify = 1;
		}
	}

	if ((flags & NLM_F_REPLACE)) {
		/* Only change unicasts */
		if (!(is_multicast_ether_addr(f->eth_addr) ||
		      is_zero_ether_addr(f->eth_addr))) {
			if (nhid) {
				rc = vxlan_fdb_nh_update(vxlan, f, nhid, extack);
				if (rc < 0)
					return rc;
			} else {
				rc = vxlan_fdb_replace(f, ip, port, vni,
						       ifindex, &oldrd);
			}
			notify |= rc;
		} else {
			NL_SET_ERR_MSG(extack, "Cannot replace non-unicast fdb entries");
			return -EOPNOTSUPP;
		}
	}
	if ((flags & NLM_F_APPEND) &&
	    (is_multicast_ether_addr(f->eth_addr) ||
	     is_zero_ether_addr(f->eth_addr))) {
		rc = vxlan_fdb_append(f, ip, port, vni, ifindex, &rd);

		if (rc < 0)
			return rc;
		notify |= rc;
	}

	if (ndm_flags & NTF_USE)
		f->used = jiffies;

	if (notify) {
		if (rd == NULL)
			rd = first_remote_rtnl(f);

		err = vxlan_fdb_notify(vxlan, f, rd, RTM_NEWNEIGH,
				       swdev_notify, extack);
		if (err)
			goto err_notify;
	}

	return 0;

err_notify:
	if (nhid)
		return err;
	if ((flags & NLM_F_REPLACE) && rc)
		*rd = oldrd;
	else if ((flags & NLM_F_APPEND) && rc) {
		list_del_rcu(&rd->list);
		call_rcu(&rd->rcu, vxlan_dst_free);
	}
	return err;
}

static int vxlan_fdb_update_create(struct vxlan_dev *vxlan,
				   const u8 *mac, union vxlan_addr *ip,
				   __u16 state, __u16 flags,
				   __be16 port, __be32 src_vni, __be32 vni,
				   __u32 ifindex, __u16 ndm_flags, u32 nhid,
				   bool swdev_notify,
				   struct netlink_ext_ack *extack)
{
	__u16 fdb_flags = (ndm_flags & ~NTF_USE);
	struct vxlan_fdb *f;
	int rc;

	/* Disallow replace to add a multicast entry */
	if ((flags & NLM_F_REPLACE) &&
	    (is_multicast_ether_addr(mac) || is_zero_ether_addr(mac)))
		return -EOPNOTSUPP;

	netdev_dbg(vxlan->dev, "add %pM -> %pIS\n", mac, ip);
	rc = vxlan_fdb_create(vxlan, mac, ip, state, port, src_vni,
			      vni, ifindex, fdb_flags, nhid, &f, extack);
	if (rc < 0)
		return rc;

	vxlan_fdb_insert(vxlan, mac, src_vni, f);
	rc = vxlan_fdb_notify(vxlan, f, first_remote_rtnl(f), RTM_NEWNEIGH,
			      swdev_notify, extack);
	if (rc)
		goto err_notify;

	return 0;

err_notify:
	vxlan_fdb_destroy(vxlan, f, false, false);
	return rc;
}

/* Add new entry to forwarding table -- assumes lock held */
int vxlan_fdb_update(struct vxlan_dev *vxlan,
		     const u8 *mac, union vxlan_addr *ip,
		     __u16 state, __u16 flags,
		     __be16 port, __be32 src_vni, __be32 vni,
		     __u32 ifindex, __u16 ndm_flags, u32 nhid,
		     bool swdev_notify,
		     struct netlink_ext_ack *extack)
{
	struct vxlan_fdb *f;

	f = __vxlan_find_mac(vxlan, mac, src_vni);
	if (f) {
		if (flags & NLM_F_EXCL) {
			netdev_dbg(vxlan->dev,
				   "lost race to create %pM\n", mac);
			return -EEXIST;
		}

		return vxlan_fdb_update_existing(vxlan, ip, state, flags, port,
						 vni, ifindex, ndm_flags, f,
						 nhid, swdev_notify, extack);
	} else {
		if (!(flags & NLM_F_CREATE))
			return -ENOENT;

		return vxlan_fdb_update_create(vxlan, mac, ip, state, flags,
					       port, src_vni, vni, ifindex,
					       ndm_flags, nhid, swdev_notify,
					       extack);
	}
}

static void vxlan_fdb_dst_destroy(struct vxlan_dev *vxlan, struct vxlan_fdb *f,
				  struct vxlan_rdst *rd, bool swdev_notify)
{
	list_del_rcu(&rd->list);
	vxlan_fdb_notify(vxlan, f, rd, RTM_DELNEIGH, swdev_notify, NULL);
	call_rcu(&rd->rcu, vxlan_dst_free);
}

static int vxlan_fdb_parse(struct nlattr *tb[], struct vxlan_dev *vxlan,
			   union vxlan_addr *ip, __be16 *port, __be32 *src_vni,
			   __be32 *vni, u32 *ifindex, u32 *nhid,
			   struct netlink_ext_ack *extack)
{
	struct net *net = dev_net(vxlan->dev);
	int err;

	if (tb[NDA_NH_ID] &&
	    (tb[NDA_DST] || tb[NDA_VNI] || tb[NDA_IFINDEX] || tb[NDA_PORT])) {
		NL_SET_ERR_MSG(extack, "DST, VNI, ifindex and port are mutually exclusive with NH_ID");
		return -EINVAL;
	}

	if (tb[NDA_DST]) {
		err = vxlan_nla_get_addr(ip, tb[NDA_DST]);
		if (err) {
			NL_SET_ERR_MSG(extack, "Unsupported address family");
			return err;
		}
	} else {
		union vxlan_addr *remote = &vxlan->default_dst.remote_ip;

		if (remote->sa.sa_family == AF_INET) {
			ip->sin.sin_addr.s_addr = htonl(INADDR_ANY);
			ip->sa.sa_family = AF_INET;
#if IS_ENABLED(CONFIG_IPV6)
		} else {
			ip->sin6.sin6_addr = in6addr_any;
			ip->sa.sa_family = AF_INET6;
#endif
		}
	}

	if (tb[NDA_PORT]) {
		if (nla_len(tb[NDA_PORT]) != sizeof(__be16)) {
			NL_SET_ERR_MSG(extack, "Invalid vxlan port");
			return -EINVAL;
		}
		*port = nla_get_be16(tb[NDA_PORT]);
	} else {
		*port = vxlan->cfg.dst_port;
	}

	if (tb[NDA_VNI]) {
		if (nla_len(tb[NDA_VNI]) != sizeof(u32)) {
			NL_SET_ERR_MSG(extack, "Invalid vni");
			return -EINVAL;
		}
		*vni = cpu_to_be32(nla_get_u32(tb[NDA_VNI]));
	} else {
		*vni = vxlan->default_dst.remote_vni;
	}

	if (tb[NDA_SRC_VNI]) {
		if (nla_len(tb[NDA_SRC_VNI]) != sizeof(u32)) {
			NL_SET_ERR_MSG(extack, "Invalid src vni");
			return -EINVAL;
		}
		*src_vni = cpu_to_be32(nla_get_u32(tb[NDA_SRC_VNI]));
	} else {
		*src_vni = vxlan->default_dst.remote_vni;
	}

	if (tb[NDA_IFINDEX]) {
		struct net_device *tdev;

		if (nla_len(tb[NDA_IFINDEX]) != sizeof(u32)) {
			NL_SET_ERR_MSG(extack, "Invalid ifindex");
			return -EINVAL;
		}
		*ifindex = nla_get_u32(tb[NDA_IFINDEX]);
		tdev = __dev_get_by_index(net, *ifindex);
		if (!tdev) {
			NL_SET_ERR_MSG(extack, "Device not found");
			return -EADDRNOTAVAIL;
		}
	} else {
		*ifindex = 0;
	}

	*nhid = nla_get_u32_default(tb[NDA_NH_ID], 0);

	return 0;
}

/* Add static entry (via netlink) */
static int vxlan_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
			 struct net_device *dev,
			 const unsigned char *addr, u16 vid, u16 flags,
			 bool *notified, struct netlink_ext_ack *extack)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	/* struct net *net = dev_net(vxlan->dev); */
	union vxlan_addr ip;
	__be16 port;
	__be32 src_vni, vni;
	u32 ifindex, nhid;
	u32 hash_index;
	int err;

	if (!(ndm->ndm_state & (NUD_PERMANENT|NUD_REACHABLE))) {
		pr_info("RTM_NEWNEIGH with invalid state %#x\n",
			ndm->ndm_state);
		return -EINVAL;
	}

	if (!tb || (!tb[NDA_DST] && !tb[NDA_NH_ID]))
		return -EINVAL;

	err = vxlan_fdb_parse(tb, vxlan, &ip, &port, &src_vni, &vni, &ifindex,
			      &nhid, extack);
	if (err)
		return err;

	if (vxlan->default_dst.remote_ip.sa.sa_family != ip.sa.sa_family)
		return -EAFNOSUPPORT;

	hash_index = fdb_head_index(vxlan, addr, src_vni);
	spin_lock_bh(&vxlan->hash_lock[hash_index]);
	err = vxlan_fdb_update(vxlan, addr, &ip, ndm->ndm_state, flags,
			       port, src_vni, vni, ifindex,
			       ndm->ndm_flags | NTF_VXLAN_ADDED_BY_USER,
			       nhid, true, extack);
	spin_unlock_bh(&vxlan->hash_lock[hash_index]);

	if (!err)
		*notified = true;

	return err;
}

int __vxlan_fdb_delete(struct vxlan_dev *vxlan,
		       const unsigned char *addr, union vxlan_addr ip,
		       __be16 port, __be32 src_vni, __be32 vni,
		       u32 ifindex, bool swdev_notify)
{
	struct vxlan_rdst *rd = NULL;
	struct vxlan_fdb *f;
	int err = -ENOENT;

	f = vxlan_find_mac(vxlan, addr, src_vni);
	if (!f)
		return err;

	if (!vxlan_addr_any(&ip)) {
		rd = vxlan_fdb_find_rdst(f, &ip, port, vni, ifindex);
		if (!rd)
			goto out;
	}

	/* remove a destination if it's not the only one on the list,
	 * otherwise destroy the fdb entry
	 */
	if (rd && !list_is_singular(&f->remotes)) {
		vxlan_fdb_dst_destroy(vxlan, f, rd, swdev_notify);
		goto out;
	}

	vxlan_fdb_destroy(vxlan, f, true, swdev_notify);

out:
	return 0;
}

/* Delete entry (via netlink) */
static int vxlan_fdb_delete(struct ndmsg *ndm, struct nlattr *tb[],
			    struct net_device *dev,
			    const unsigned char *addr, u16 vid, bool *notified,
			    struct netlink_ext_ack *extack)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	union vxlan_addr ip;
	__be32 src_vni, vni;
	u32 ifindex, nhid;
	u32 hash_index;
	__be16 port;
	int err;

	err = vxlan_fdb_parse(tb, vxlan, &ip, &port, &src_vni, &vni, &ifindex,
			      &nhid, extack);
	if (err)
		return err;

	hash_index = fdb_head_index(vxlan, addr, src_vni);
	spin_lock_bh(&vxlan->hash_lock[hash_index]);
	err = __vxlan_fdb_delete(vxlan, addr, ip, port, src_vni, vni, ifindex,
				 true);
	spin_unlock_bh(&vxlan->hash_lock[hash_index]);

	if (!err)
		*notified = true;

	return err;
}

/* Dump forwarding table */
static int vxlan_fdb_dump(struct sk_buff *skb, struct netlink_callback *cb,
			  struct net_device *dev,
			  struct net_device *filter_dev, int *idx)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	unsigned int h;
	int err = 0;

	for (h = 0; h < FDB_HASH_SIZE; ++h) {
		struct vxlan_fdb *f;

		rcu_read_lock();
		hlist_for_each_entry_rcu(f, &vxlan->fdb_head[h], hlist) {
			struct vxlan_rdst *rd;

			if (rcu_access_pointer(f->nh)) {
				if (*idx < cb->args[2])
					goto skip_nh;
				err = vxlan_fdb_info(skb, vxlan, f,
						     NETLINK_CB(cb->skb).portid,
						     cb->nlh->nlmsg_seq,
						     RTM_NEWNEIGH,
						     NLM_F_MULTI, NULL);
				if (err < 0) {
					rcu_read_unlock();
					goto out;
				}
skip_nh:
				*idx += 1;
				continue;
			}

			list_for_each_entry_rcu(rd, &f->remotes, list) {
				if (*idx < cb->args[2])
					goto skip;

				err = vxlan_fdb_info(skb, vxlan, f,
						     NETLINK_CB(cb->skb).portid,
						     cb->nlh->nlmsg_seq,
						     RTM_NEWNEIGH,
						     NLM_F_MULTI, rd);
				if (err < 0) {
					rcu_read_unlock();
					goto out;
				}
skip:
				*idx += 1;
			}
		}
		rcu_read_unlock();
	}
out:
	return err;
}

static int vxlan_fdb_get(struct sk_buff *skb,
			 struct nlattr *tb[],
			 struct net_device *dev,
			 const unsigned char *addr,
			 u16 vid, u32 portid, u32 seq,
			 struct netlink_ext_ack *extack)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct vxlan_fdb *f;
	__be32 vni;
	int err;

	if (tb[NDA_VNI])
		vni = cpu_to_be32(nla_get_u32(tb[NDA_VNI]));
	else
		vni = vxlan->default_dst.remote_vni;

	rcu_read_lock();

	f = __vxlan_find_mac(vxlan, addr, vni);
	if (!f) {
		NL_SET_ERR_MSG(extack, "Fdb entry not found");
		err = -ENOENT;
		goto errout;
	}

	err = vxlan_fdb_info(skb, vxlan, f, portid, seq,
			     RTM_NEWNEIGH, 0, first_remote_rcu(f));
errout:
	rcu_read_unlock();
	return err;
}

/* Watch incoming packets to learn mapping between Ethernet address
 * and Tunnel endpoint.
 */
static enum skb_drop_reason vxlan_snoop(struct net_device *dev,
					union vxlan_addr *src_ip,
					const u8 *src_mac, u32 src_ifindex,
					__be32 vni)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct vxlan_fdb *f;
	u32 ifindex = 0;

	/* Ignore packets from invalid src-address */
	if (!is_valid_ether_addr(src_mac))
		return SKB_DROP_REASON_MAC_INVALID_SOURCE;

#if IS_ENABLED(CONFIG_IPV6)
	if (src_ip->sa.sa_family == AF_INET6 &&
	    (ipv6_addr_type(&src_ip->sin6.sin6_addr) & IPV6_ADDR_LINKLOCAL))
		ifindex = src_ifindex;
#endif

	f = vxlan_find_mac(vxlan, src_mac, vni);
	if (likely(f)) {
		struct vxlan_rdst *rdst = first_remote_rcu(f);

		if (likely(vxlan_addr_equal(&rdst->remote_ip, src_ip) &&
			   rdst->remote_ifindex == ifindex))
			return SKB_NOT_DROPPED_YET;

		/* Don't migrate static entries, drop packets */
		if (f->state & (NUD_PERMANENT | NUD_NOARP))
			return SKB_DROP_REASON_VXLAN_ENTRY_EXISTS;

		/* Don't override an fdb with nexthop with a learnt entry */
		if (rcu_access_pointer(f->nh))
			return SKB_DROP_REASON_VXLAN_ENTRY_EXISTS;

		if (net_ratelimit())
			netdev_info(dev,
				    "%pM migrated from %pIS to %pIS\n",
				    src_mac, &rdst->remote_ip.sa, &src_ip->sa);

		rdst->remote_ip = *src_ip;
		f->updated = jiffies;
		vxlan_fdb_notify(vxlan, f, rdst, RTM_NEWNEIGH, true, NULL);
	} else {
		u32 hash_index = fdb_head_index(vxlan, src_mac, vni);

		/* learned new entry */
		spin_lock(&vxlan->hash_lock[hash_index]);

		/* close off race between vxlan_flush and incoming packets */
		if (netif_running(dev))
			vxlan_fdb_update(vxlan, src_mac, src_ip,
					 NUD_REACHABLE,
					 NLM_F_EXCL|NLM_F_CREATE,
					 vxlan->cfg.dst_port,
					 vni,
					 vxlan->default_dst.remote_vni,
					 ifindex, NTF_SELF, 0, true, NULL);
		spin_unlock(&vxlan->hash_lock[hash_index]);
	}

	return SKB_NOT_DROPPED_YET;
}

static bool __vxlan_sock_release_prep(struct vxlan_sock *vs)
{
	struct vxlan_net *vn;

	if (!vs)
		return false;
	if (!refcount_dec_and_test(&vs->refcnt))
		return false;

	vn = net_generic(sock_net(vs->sock->sk), vxlan_net_id);
	spin_lock(&vn->sock_lock);
	hlist_del_rcu(&vs->hlist);
	udp_tunnel_notify_del_rx_port(vs->sock,
				      (vs->flags & VXLAN_F_GPE) ?
				      UDP_TUNNEL_TYPE_VXLAN_GPE :
				      UDP_TUNNEL_TYPE_VXLAN);
	spin_unlock(&vn->sock_lock);

	return true;
}

static void vxlan_sock_release(struct vxlan_dev *vxlan)
{
	struct vxlan_sock *sock4 = rtnl_dereference(vxlan->vn4_sock);
#if IS_ENABLED(CONFIG_IPV6)
	struct vxlan_sock *sock6 = rtnl_dereference(vxlan->vn6_sock);

	RCU_INIT_POINTER(vxlan->vn6_sock, NULL);
#endif

	RCU_INIT_POINTER(vxlan->vn4_sock, NULL);
	synchronize_net();

	if (vxlan->cfg.flags & VXLAN_F_VNIFILTER)
		vxlan_vs_del_vnigrp(vxlan);
	else
		vxlan_vs_del_dev(vxlan);

	if (__vxlan_sock_release_prep(sock4)) {
		udp_tunnel_sock_release(sock4->sock);
		kfree(sock4);
	}

#if IS_ENABLED(CONFIG_IPV6)
	if (__vxlan_sock_release_prep(sock6)) {
		udp_tunnel_sock_release(sock6->sock);
		kfree(sock6);
	}
#endif
}

static enum skb_drop_reason vxlan_remcsum(struct vxlanhdr *unparsed,
					  struct sk_buff *skb,
					  u32 vxflags)
{
	enum skb_drop_reason reason;
	size_t start, offset;

	if (!(unparsed->vx_flags & VXLAN_HF_RCO) || skb->remcsum_offload)
		goto out;

	start = vxlan_rco_start(unparsed->vx_vni);
	offset = start + vxlan_rco_offset(unparsed->vx_vni);

	reason = pskb_may_pull_reason(skb, offset + sizeof(u16));
	if (reason)
		return reason;

	skb_remcsum_process(skb, (void *)(vxlan_hdr(skb) + 1), start, offset,
			    !!(vxflags & VXLAN_F_REMCSUM_NOPARTIAL));
out:
	unparsed->vx_flags &= ~VXLAN_HF_RCO;
	unparsed->vx_vni &= VXLAN_VNI_MASK;

	return SKB_NOT_DROPPED_YET;
}

static void vxlan_parse_gbp_hdr(struct vxlanhdr *unparsed,
				struct sk_buff *skb, u32 vxflags,
				struct vxlan_metadata *md)
{
	struct vxlanhdr_gbp *gbp = (struct vxlanhdr_gbp *)unparsed;
	struct metadata_dst *tun_dst;

	if (!(unparsed->vx_flags & VXLAN_HF_GBP))
		goto out;

	md->gbp = ntohs(gbp->policy_id);

	tun_dst = (struct metadata_dst *)skb_dst(skb);
	if (tun_dst) {
		__set_bit(IP_TUNNEL_VXLAN_OPT_BIT,
			  tun_dst->u.tun_info.key.tun_flags);
		tun_dst->u.tun_info.options_len = sizeof(*md);
	}
	if (gbp->dont_learn)
		md->gbp |= VXLAN_GBP_DONT_LEARN;

	if (gbp->policy_applied)
		md->gbp |= VXLAN_GBP_POLICY_APPLIED;

	/* In flow-based mode, GBP is carried in dst_metadata */
	if (!(vxflags & VXLAN_F_COLLECT_METADATA))
		skb->mark = md->gbp;
out:
	unparsed->vx_flags &= ~VXLAN_GBP_USED_BITS;
}

static enum skb_drop_reason vxlan_set_mac(struct vxlan_dev *vxlan,
					  struct vxlan_sock *vs,
					  struct sk_buff *skb, __be32 vni)
{
	union vxlan_addr saddr;
	u32 ifindex = skb->dev->ifindex;

	skb_reset_mac_header(skb);
	skb->protocol = eth_type_trans(skb, vxlan->dev);
	skb_postpull_rcsum(skb, eth_hdr(skb), ETH_HLEN);

	/* Ignore packet loops (and multicast echo) */
	if (ether_addr_equal(eth_hdr(skb)->h_source, vxlan->dev->dev_addr))
		return SKB_DROP_REASON_LOCAL_MAC;

	/* Get address from the outer IP header */
	if (vxlan_get_sk_family(vs) == AF_INET) {
		saddr.sin.sin_addr.s_addr = ip_hdr(skb)->saddr;
		saddr.sa.sa_family = AF_INET;
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		saddr.sin6.sin6_addr = ipv6_hdr(skb)->saddr;
		saddr.sa.sa_family = AF_INET6;
#endif
	}

	if (!(vxlan->cfg.flags & VXLAN_F_LEARN))
		return SKB_NOT_DROPPED_YET;

	return vxlan_snoop(skb->dev, &saddr, eth_hdr(skb)->h_source,
			   ifindex, vni);
}

static bool vxlan_ecn_decapsulate(struct vxlan_sock *vs, void *oiph,
				  struct sk_buff *skb)
{
	int err = 0;

	if (vxlan_get_sk_family(vs) == AF_INET)
		err = IP_ECN_decapsulate(oiph, skb);
#if IS_ENABLED(CONFIG_IPV6)
	else
		err = IP6_ECN_decapsulate(oiph, skb);
#endif

	if (unlikely(err) && log_ecn_error) {
		if (vxlan_get_sk_family(vs) == AF_INET)
			net_info_ratelimited("non-ECT from %pI4 with TOS=%#x\n",
					     &((struct iphdr *)oiph)->saddr,
					     ((struct iphdr *)oiph)->tos);
		else
			net_info_ratelimited("non-ECT from %pI6\n",
					     &((struct ipv6hdr *)oiph)->saddr);
	}
	return err <= 1;
}

/* Callback from net/ipv4/udp.c to receive packets */
static int vxlan_rcv(struct sock *sk, struct sk_buff *skb)
{
	struct vxlan_vni_node *vninode = NULL;
	struct vxlan_dev *vxlan;
	struct vxlan_sock *vs;
	struct vxlanhdr unparsed;
	struct vxlan_metadata _md;
	struct vxlan_metadata *md = &_md;
	__be16 protocol = htons(ETH_P_TEB);
	enum skb_drop_reason reason;
	bool raw_proto = false;
	void *oiph;
	__be32 vni = 0;
	int nh;

	/* Need UDP and VXLAN header to be present */
	reason = pskb_may_pull_reason(skb, VXLAN_HLEN);
	if (reason)
		goto drop;

	unparsed = *vxlan_hdr(skb);
	/* VNI flag always required to be set */
	if (!(unparsed.vx_flags & VXLAN_HF_VNI)) {
		netdev_dbg(skb->dev, "invalid vxlan flags=%#x vni=%#x\n",
			   ntohl(vxlan_hdr(skb)->vx_flags),
			   ntohl(vxlan_hdr(skb)->vx_vni));
		reason = SKB_DROP_REASON_VXLAN_INVALID_HDR;
		/* Return non vxlan pkt */
		goto drop;
	}
	unparsed.vx_flags &= ~VXLAN_HF_VNI;
	unparsed.vx_vni &= ~VXLAN_VNI_MASK;

	vs = rcu_dereference_sk_user_data(sk);
	if (!vs)
		goto drop;

	vni = vxlan_vni(vxlan_hdr(skb)->vx_vni);

	vxlan = vxlan_vs_find_vni(vs, skb->dev->ifindex, vni, &vninode);
	if (!vxlan) {
		reason = SKB_DROP_REASON_VXLAN_VNI_NOT_FOUND;
		goto drop;
	}

	/* For backwards compatibility, only allow reserved fields to be
	 * used by VXLAN extensions if explicitly requested.
	 */
	if (vs->flags & VXLAN_F_GPE) {
		if (!vxlan_parse_gpe_proto(&unparsed, &protocol))
			goto drop;
		unparsed.vx_flags &= ~VXLAN_GPE_USED_BITS;
		raw_proto = true;
	}

	if (__iptunnel_pull_header(skb, VXLAN_HLEN, protocol, raw_proto,
				   !net_eq(vxlan->net, dev_net(vxlan->dev)))) {
		reason = SKB_DROP_REASON_NOMEM;
		goto drop;
	}

	if (vs->flags & VXLAN_F_REMCSUM_RX) {
		reason = vxlan_remcsum(&unparsed, skb, vs->flags);
		if (unlikely(reason))
			goto drop;
	}

	if (vxlan_collect_metadata(vs)) {
		IP_TUNNEL_DECLARE_FLAGS(flags) = { };
		struct metadata_dst *tun_dst;

		__set_bit(IP_TUNNEL_KEY_BIT, flags);
		tun_dst = udp_tun_rx_dst(skb, vxlan_get_sk_family(vs), flags,
					 key32_to_tunnel_id(vni), sizeof(*md));

		if (!tun_dst) {
			reason = SKB_DROP_REASON_NOMEM;
			goto drop;
		}

		md = ip_tunnel_info_opts(&tun_dst->u.tun_info);

		skb_dst_set(skb, (struct dst_entry *)tun_dst);
	} else {
		memset(md, 0, sizeof(*md));
	}

	if (vs->flags & VXLAN_F_GBP)
		vxlan_parse_gbp_hdr(&unparsed, skb, vs->flags, md);
	/* Note that GBP and GPE can never be active together. This is
	 * ensured in vxlan_dev_configure.
	 */

	if (unparsed.vx_flags || unparsed.vx_vni) {
		/* If there are any unprocessed flags remaining treat
		 * this as a malformed packet. This behavior diverges from
		 * VXLAN RFC (RFC7348) which stipulates that bits in reserved
		 * in reserved fields are to be ignored. The approach here
		 * maintains compatibility with previous stack code, and also
		 * is more robust and provides a little more security in
		 * adding extensions to VXLAN.
		 */
		reason = SKB_DROP_REASON_VXLAN_INVALID_HDR;
		goto drop;
	}

	if (!raw_proto) {
		reason = vxlan_set_mac(vxlan, vs, skb, vni);
		if (reason)
			goto drop;
	} else {
		skb_reset_mac_header(skb);
		skb->dev = vxlan->dev;
		skb->pkt_type = PACKET_HOST;
	}

	/* Save offset of outer header relative to skb->head,
	 * because we are going to reset the network header to the inner header
	 * and might change skb->head.
	 */
	nh = skb_network_header(skb) - skb->head;

	skb_reset_network_header(skb);

	reason = pskb_inet_may_pull_reason(skb);
	if (reason) {
		DEV_STATS_INC(vxlan->dev, rx_length_errors);
		DEV_STATS_INC(vxlan->dev, rx_errors);
		vxlan_vnifilter_count(vxlan, vni, vninode,
				      VXLAN_VNI_STATS_RX_ERRORS, 0);
		goto drop;
	}

	/* Get the outer header. */
	oiph = skb->head + nh;

	if (!vxlan_ecn_decapsulate(vs, oiph, skb)) {
		reason = SKB_DROP_REASON_IP_TUNNEL_ECN;
		DEV_STATS_INC(vxlan->dev, rx_frame_errors);
		DEV_STATS_INC(vxlan->dev, rx_errors);
		vxlan_vnifilter_count(vxlan, vni, vninode,
				      VXLAN_VNI_STATS_RX_ERRORS, 0);
		goto drop;
	}

	rcu_read_lock();

	if (unlikely(!(vxlan->dev->flags & IFF_UP))) {
		rcu_read_unlock();
		dev_core_stats_rx_dropped_inc(vxlan->dev);
		vxlan_vnifilter_count(vxlan, vni, vninode,
				      VXLAN_VNI_STATS_RX_DROPS, 0);
		reason = SKB_DROP_REASON_DEV_READY;
		goto drop;
	}

	dev_sw_netstats_rx_add(vxlan->dev, skb->len);
	vxlan_vnifilter_count(vxlan, vni, vninode, VXLAN_VNI_STATS_RX, skb->len);
	gro_cells_receive(&vxlan->gro_cells, skb);

	rcu_read_unlock();

	return 0;

drop:
	reason = reason ?: SKB_DROP_REASON_NOT_SPECIFIED;
	/* Consume bad packet */
	kfree_skb_reason(skb, reason);
	return 0;
}

/* Callback from net/ipv{4,6}/udp.c to check that we have a VNI for errors */
static int vxlan_err_lookup(struct sock *sk, struct sk_buff *skb)
{
	struct vxlan_dev *vxlan;
	struct vxlan_sock *vs;
	struct vxlanhdr *hdr;
	__be32 vni;

	if (!pskb_may_pull(skb, skb_transport_offset(skb) + VXLAN_HLEN))
		return -EINVAL;

	hdr = vxlan_hdr(skb);

	if (!(hdr->vx_flags & VXLAN_HF_VNI))
		return -EINVAL;

	vs = rcu_dereference_sk_user_data(sk);
	if (!vs)
		return -ENOENT;

	vni = vxlan_vni(hdr->vx_vni);
	vxlan = vxlan_vs_find_vni(vs, skb->dev->ifindex, vni, NULL);
	if (!vxlan)
		return -ENOENT;

	return 0;
}

static int arp_reduce(struct net_device *dev, struct sk_buff *skb, __be32 vni)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct arphdr *parp;
	u8 *arpptr, *sha;
	__be32 sip, tip;
	struct neighbour *n;

	if (dev->flags & IFF_NOARP)
		goto out;

	if (!pskb_may_pull(skb, arp_hdr_len(dev))) {
		dev_core_stats_tx_dropped_inc(dev);
		vxlan_vnifilter_count(vxlan, vni, NULL,
				      VXLAN_VNI_STATS_TX_DROPS, 0);
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
		struct vxlan_fdb *f;
		struct sk_buff	*reply;

		if (!(READ_ONCE(n->nud_state) & NUD_CONNECTED)) {
			neigh_release(n);
			goto out;
		}

		f = vxlan_find_mac(vxlan, n->ha, vni);
		if (f && vxlan_addr_any(&(first_remote_rcu(f)->remote_ip))) {
			/* bridge-local neighbor */
			neigh_release(n);
			goto out;
		}

		reply = arp_create(ARPOP_REPLY, ETH_P_ARP, sip, dev, tip, sha,
				n->ha, sha);

		neigh_release(n);

		if (reply == NULL)
			goto out;

		skb_reset_mac_header(reply);
		__skb_pull(reply, skb_network_offset(reply));
		reply->ip_summed = CHECKSUM_UNNECESSARY;
		reply->pkt_type = PACKET_HOST;

		if (netif_rx(reply) == NET_RX_DROP) {
			dev_core_stats_rx_dropped_inc(dev);
			vxlan_vnifilter_count(vxlan, vni, NULL,
					      VXLAN_VNI_STATS_RX_DROPS, 0);
		}

	} else if (vxlan->cfg.flags & VXLAN_F_L3MISS) {
		union vxlan_addr ipa = {
			.sin.sin_addr.s_addr = tip,
			.sin.sin_family = AF_INET,
		};

		vxlan_ip_miss(dev, &ipa);
	}
out:
	consume_skb(skb);
	return NETDEV_TX_OK;
}

#if IS_ENABLED(CONFIG_IPV6)
static struct sk_buff *vxlan_na_create(struct sk_buff *request,
	struct neighbour *n, bool isrouter)
{
	struct net_device *dev = request->dev;
	struct sk_buff *reply;
	struct nd_msg *ns, *na;
	struct ipv6hdr *pip6;
	u8 *daddr;
	int na_olen = 8; /* opt hdr + ETH_ALEN for target */
	int ns_olen;
	int i, len;

	if (dev == NULL || !pskb_may_pull(request, request->len))
		return NULL;

	len = LL_RESERVED_SPACE(dev) + sizeof(struct ipv6hdr) +
		sizeof(*na) + na_olen + dev->needed_tailroom;
	reply = alloc_skb(len, GFP_ATOMIC);
	if (reply == NULL)
		return NULL;

	reply->protocol = htons(ETH_P_IPV6);
	reply->dev = dev;
	skb_reserve(reply, LL_RESERVED_SPACE(request->dev));
	skb_push(reply, sizeof(struct ethhdr));
	skb_reset_mac_header(reply);

	ns = (struct nd_msg *)(ipv6_hdr(request) + 1);

	daddr = eth_hdr(request)->h_source;
	ns_olen = request->len - skb_network_offset(request) -
		sizeof(struct ipv6hdr) - sizeof(*ns);
	for (i = 0; i < ns_olen-1; i += (ns->opt[i+1]<<3)) {
		if (!ns->opt[i + 1]) {
			kfree_skb(reply);
			return NULL;
		}
		if (ns->opt[i] == ND_OPT_SOURCE_LL_ADDR) {
			daddr = ns->opt + i + sizeof(struct nd_opt_hdr);
			break;
		}
	}

	/* Ethernet header */
	ether_addr_copy(eth_hdr(reply)->h_dest, daddr);
	ether_addr_copy(eth_hdr(reply)->h_source, n->ha);
	eth_hdr(reply)->h_proto = htons(ETH_P_IPV6);
	reply->protocol = htons(ETH_P_IPV6);

	skb_pull(reply, sizeof(struct ethhdr));
	skb_reset_network_header(reply);
	skb_put(reply, sizeof(struct ipv6hdr));

	/* IPv6 header */

	pip6 = ipv6_hdr(reply);
	memset(pip6, 0, sizeof(struct ipv6hdr));
	pip6->version = 6;
	pip6->priority = ipv6_hdr(request)->priority;
	pip6->nexthdr = IPPROTO_ICMPV6;
	pip6->hop_limit = 255;
	pip6->daddr = ipv6_hdr(request)->saddr;
	pip6->saddr = *(struct in6_addr *)n->primary_key;

	skb_pull(reply, sizeof(struct ipv6hdr));
	skb_reset_transport_header(reply);

	/* Neighbor Advertisement */
	na = skb_put_zero(reply, sizeof(*na) + na_olen);
	na->icmph.icmp6_type = NDISC_NEIGHBOUR_ADVERTISEMENT;
	na->icmph.icmp6_router = isrouter;
	na->icmph.icmp6_override = 1;
	na->icmph.icmp6_solicited = 1;
	na->target = ns->target;
	ether_addr_copy(&na->opt[2], n->ha);
	na->opt[0] = ND_OPT_TARGET_LL_ADDR;
	na->opt[1] = na_olen >> 3;

	na->icmph.icmp6_cksum = csum_ipv6_magic(&pip6->saddr,
		&pip6->daddr, sizeof(*na)+na_olen, IPPROTO_ICMPV6,
		csum_partial(na, sizeof(*na)+na_olen, 0));

	pip6->payload_len = htons(sizeof(*na)+na_olen);

	skb_push(reply, sizeof(struct ipv6hdr));

	reply->ip_summed = CHECKSUM_UNNECESSARY;

	return reply;
}

static int neigh_reduce(struct net_device *dev, struct sk_buff *skb, __be32 vni)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	const struct in6_addr *daddr;
	const struct ipv6hdr *iphdr;
	struct inet6_dev *in6_dev;
	struct neighbour *n;
	struct nd_msg *msg;

	rcu_read_lock();
	in6_dev = __in6_dev_get(dev);
	if (!in6_dev)
		goto out;

	iphdr = ipv6_hdr(skb);
	daddr = &iphdr->daddr;
	msg = (struct nd_msg *)(iphdr + 1);

	if (ipv6_addr_loopback(daddr) ||
	    ipv6_addr_is_multicast(&msg->target))
		goto out;

	n = neigh_lookup(ipv6_stub->nd_tbl, &msg->target, dev);

	if (n) {
		struct vxlan_fdb *f;
		struct sk_buff *reply;

		if (!(READ_ONCE(n->nud_state) & NUD_CONNECTED)) {
			neigh_release(n);
			goto out;
		}

		f = vxlan_find_mac(vxlan, n->ha, vni);
		if (f && vxlan_addr_any(&(first_remote_rcu(f)->remote_ip))) {
			/* bridge-local neighbor */
			neigh_release(n);
			goto out;
		}

		reply = vxlan_na_create(skb, n,
					!!(f ? f->flags & NTF_ROUTER : 0));

		neigh_release(n);

		if (reply == NULL)
			goto out;

		if (netif_rx(reply) == NET_RX_DROP) {
			dev_core_stats_rx_dropped_inc(dev);
			vxlan_vnifilter_count(vxlan, vni, NULL,
					      VXLAN_VNI_STATS_RX_DROPS, 0);
		}
	} else if (vxlan->cfg.flags & VXLAN_F_L3MISS) {
		union vxlan_addr ipa = {
			.sin6.sin6_addr = msg->target,
			.sin6.sin6_family = AF_INET6,
		};

		vxlan_ip_miss(dev, &ipa);
	}

out:
	rcu_read_unlock();
	consume_skb(skb);
	return NETDEV_TX_OK;
}
#endif

static bool route_shortcircuit(struct net_device *dev, struct sk_buff *skb)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct neighbour *n;

	if (is_multicast_ether_addr(eth_hdr(skb)->h_dest))
		return false;

	n = NULL;
	switch (ntohs(eth_hdr(skb)->h_proto)) {
	case ETH_P_IP:
	{
		struct iphdr *pip;

		if (!pskb_may_pull(skb, sizeof(struct iphdr)))
			return false;
		pip = ip_hdr(skb);
		n = neigh_lookup(&arp_tbl, &pip->daddr, dev);
		if (!n && (vxlan->cfg.flags & VXLAN_F_L3MISS)) {
			union vxlan_addr ipa = {
				.sin.sin_addr.s_addr = pip->daddr,
				.sin.sin_family = AF_INET,
			};

			vxlan_ip_miss(dev, &ipa);
			return false;
		}

		break;
	}
#if IS_ENABLED(CONFIG_IPV6)
	case ETH_P_IPV6:
	{
		struct ipv6hdr *pip6;

		if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
			return false;
		pip6 = ipv6_hdr(skb);
		n = neigh_lookup(ipv6_stub->nd_tbl, &pip6->daddr, dev);
		if (!n && (vxlan->cfg.flags & VXLAN_F_L3MISS)) {
			union vxlan_addr ipa = {
				.sin6.sin6_addr = pip6->daddr,
				.sin6.sin6_family = AF_INET6,
			};

			vxlan_ip_miss(dev, &ipa);
			return false;
		}

		break;
	}
#endif
	default:
		return false;
	}

	if (n) {
		bool diff;

		diff = !ether_addr_equal(eth_hdr(skb)->h_dest, n->ha);
		if (diff) {
			memcpy(eth_hdr(skb)->h_source, eth_hdr(skb)->h_dest,
				dev->addr_len);
			memcpy(eth_hdr(skb)->h_dest, n->ha, dev->addr_len);
		}
		neigh_release(n);
		return diff;
	}

	return false;
}

static int vxlan_build_gpe_hdr(struct vxlanhdr *vxh, __be16 protocol)
{
	struct vxlanhdr_gpe *gpe = (struct vxlanhdr_gpe *)vxh;

	gpe->np_applied = 1;
	gpe->next_protocol = tun_p_from_eth_p(protocol);
	if (!gpe->next_protocol)
		return -EPFNOSUPPORT;
	return 0;
}

static int vxlan_build_skb(struct sk_buff *skb, struct dst_entry *dst,
			   int iphdr_len, __be32 vni,
			   struct vxlan_metadata *md, u32 vxflags,
			   bool udp_sum)
{
	struct vxlanhdr *vxh;
	int min_headroom;
	int err;
	int type = udp_sum ? SKB_GSO_UDP_TUNNEL_CSUM : SKB_GSO_UDP_TUNNEL;
	__be16 inner_protocol = htons(ETH_P_TEB);

	if ((vxflags & VXLAN_F_REMCSUM_TX) &&
	    skb->ip_summed == CHECKSUM_PARTIAL) {
		int csum_start = skb_checksum_start_offset(skb);

		if (csum_start <= VXLAN_MAX_REMCSUM_START &&
		    !(csum_start & VXLAN_RCO_SHIFT_MASK) &&
		    (skb->csum_offset == offsetof(struct udphdr, check) ||
		     skb->csum_offset == offsetof(struct tcphdr, check)))
			type |= SKB_GSO_TUNNEL_REMCSUM;
	}

	min_headroom = LL_RESERVED_SPACE(dst->dev) + dst->header_len
			+ VXLAN_HLEN + iphdr_len;

	/* Need space for new headers (invalidates iph ptr) */
	err = skb_cow_head(skb, min_headroom);
	if (unlikely(err))
		return err;

	err = iptunnel_handle_offloads(skb, type);
	if (err)
		return err;

	vxh = __skb_push(skb, sizeof(*vxh));
	vxh->vx_flags = VXLAN_HF_VNI;
	vxh->vx_vni = vxlan_vni_field(vni);

	if (type & SKB_GSO_TUNNEL_REMCSUM) {
		unsigned int start;

		start = skb_checksum_start_offset(skb) - sizeof(struct vxlanhdr);
		vxh->vx_vni |= vxlan_compute_rco(start, skb->csum_offset);
		vxh->vx_flags |= VXLAN_HF_RCO;

		if (!skb_is_gso(skb)) {
			skb->ip_summed = CHECKSUM_NONE;
			skb->encapsulation = 0;
		}
	}

	if (vxflags & VXLAN_F_GBP)
		vxlan_build_gbp_hdr(vxh, md);
	if (vxflags & VXLAN_F_GPE) {
		err = vxlan_build_gpe_hdr(vxh, skb->protocol);
		if (err < 0)
			return err;
		inner_protocol = skb->protocol;
	}

	skb_set_inner_protocol(skb, inner_protocol);
	return 0;
}

/* Bypass encapsulation if the destination is local */
static void vxlan_encap_bypass(struct sk_buff *skb, struct vxlan_dev *src_vxlan,
			       struct vxlan_dev *dst_vxlan, __be32 vni,
			       bool snoop)
{
	union vxlan_addr loopback;
	union vxlan_addr *remote_ip = &dst_vxlan->default_dst.remote_ip;
	struct net_device *dev;
	int len = skb->len;

	skb->pkt_type = PACKET_HOST;
	skb->encapsulation = 0;
	skb->dev = dst_vxlan->dev;
	__skb_pull(skb, skb_network_offset(skb));

	if (remote_ip->sa.sa_family == AF_INET) {
		loopback.sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		loopback.sa.sa_family =  AF_INET;
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		loopback.sin6.sin6_addr = in6addr_loopback;
		loopback.sa.sa_family =  AF_INET6;
#endif
	}

	rcu_read_lock();
	dev = skb->dev;
	if (unlikely(!(dev->flags & IFF_UP))) {
		kfree_skb_reason(skb, SKB_DROP_REASON_DEV_READY);
		goto drop;
	}

	if ((dst_vxlan->cfg.flags & VXLAN_F_LEARN) && snoop)
		vxlan_snoop(dev, &loopback, eth_hdr(skb)->h_source, 0, vni);

	dev_sw_netstats_tx_add(src_vxlan->dev, 1, len);
	vxlan_vnifilter_count(src_vxlan, vni, NULL, VXLAN_VNI_STATS_TX, len);

	if (__netif_rx(skb) == NET_RX_SUCCESS) {
		dev_sw_netstats_rx_add(dst_vxlan->dev, len);
		vxlan_vnifilter_count(dst_vxlan, vni, NULL, VXLAN_VNI_STATS_RX,
				      len);
	} else {
drop:
		dev_core_stats_rx_dropped_inc(dev);
		vxlan_vnifilter_count(dst_vxlan, vni, NULL,
				      VXLAN_VNI_STATS_RX_DROPS, 0);
	}
	rcu_read_unlock();
}

static int encap_bypass_if_local(struct sk_buff *skb, struct net_device *dev,
				 struct vxlan_dev *vxlan,
				 int addr_family,
				 __be16 dst_port, int dst_ifindex, __be32 vni,
				 struct dst_entry *dst,
				 u32 rt_flags)
{
#if IS_ENABLED(CONFIG_IPV6)
	/* IPv6 rt-flags are checked against RTF_LOCAL, but the value of
	 * RTF_LOCAL is equal to RTCF_LOCAL. So to keep code simple
	 * we can use RTCF_LOCAL which works for ipv4 and ipv6 route entry.
	 */
	BUILD_BUG_ON(RTCF_LOCAL != RTF_LOCAL);
#endif
	/* Bypass encapsulation if the destination is local */
	if (rt_flags & RTCF_LOCAL &&
	    !(rt_flags & (RTCF_BROADCAST | RTCF_MULTICAST)) &&
	    vxlan->cfg.flags & VXLAN_F_LOCALBYPASS) {
		struct vxlan_dev *dst_vxlan;

		dst_release(dst);
		dst_vxlan = vxlan_find_vni(vxlan->net, dst_ifindex, vni,
					   addr_family, dst_port,
					   vxlan->cfg.flags);
		if (!dst_vxlan) {
			DEV_STATS_INC(dev, tx_errors);
			vxlan_vnifilter_count(vxlan, vni, NULL,
					      VXLAN_VNI_STATS_TX_ERRORS, 0);
			kfree_skb_reason(skb, SKB_DROP_REASON_VXLAN_VNI_NOT_FOUND);

			return -ENOENT;
		}
		vxlan_encap_bypass(skb, vxlan, dst_vxlan, vni, true);
		return 1;
	}

	return 0;
}

void vxlan_xmit_one(struct sk_buff *skb, struct net_device *dev,
		    __be32 default_vni, struct vxlan_rdst *rdst, bool did_rsc)
{
	struct dst_cache *dst_cache;
	struct ip_tunnel_info *info;
	struct ip_tunnel_key *pkey;
	struct ip_tunnel_key key;
	struct vxlan_dev *vxlan = netdev_priv(dev);
	const struct iphdr *old_iph;
	struct vxlan_metadata _md;
	struct vxlan_metadata *md = &_md;
	unsigned int pkt_len = skb->len;
	__be16 src_port = 0, dst_port;
	struct dst_entry *ndst = NULL;
	int addr_family;
	__u8 tos, ttl;
	int ifindex;
	int err;
	u32 flags = vxlan->cfg.flags;
	bool use_cache;
	bool udp_sum = false;
	bool xnet = !net_eq(vxlan->net, dev_net(vxlan->dev));
	enum skb_drop_reason reason;
	bool no_eth_encap;
	__be32 vni = 0;

	no_eth_encap = flags & VXLAN_F_GPE && skb->protocol != htons(ETH_P_TEB);
	reason = skb_vlan_inet_prepare(skb, no_eth_encap);
	if (reason)
		goto drop;

	reason = SKB_DROP_REASON_NOT_SPECIFIED;
	old_iph = ip_hdr(skb);

	info = skb_tunnel_info(skb);
	use_cache = ip_tunnel_dst_cache_usable(skb, info);

	if (rdst) {
		memset(&key, 0, sizeof(key));
		pkey = &key;

		if (vxlan_addr_any(&rdst->remote_ip)) {
			if (did_rsc) {
				/* short-circuited back to local bridge */
				vxlan_encap_bypass(skb, vxlan, vxlan,
						   default_vni, true);
				return;
			}
			goto drop;
		}

		addr_family = vxlan->cfg.saddr.sa.sa_family;
		dst_port = rdst->remote_port ? rdst->remote_port : vxlan->cfg.dst_port;
		vni = (rdst->remote_vni) ? : default_vni;
		ifindex = rdst->remote_ifindex;

		if (addr_family == AF_INET) {
			key.u.ipv4.src = vxlan->cfg.saddr.sin.sin_addr.s_addr;
			key.u.ipv4.dst = rdst->remote_ip.sin.sin_addr.s_addr;
		} else {
			key.u.ipv6.src = vxlan->cfg.saddr.sin6.sin6_addr;
			key.u.ipv6.dst = rdst->remote_ip.sin6.sin6_addr;
		}

		dst_cache = &rdst->dst_cache;
		md->gbp = skb->mark;
		if (flags & VXLAN_F_TTL_INHERIT) {
			ttl = ip_tunnel_get_ttl(old_iph, skb);
		} else {
			ttl = vxlan->cfg.ttl;
			if (!ttl && vxlan_addr_multicast(&rdst->remote_ip))
				ttl = 1;
		}
		tos = vxlan->cfg.tos;
		if (tos == 1)
			tos = ip_tunnel_get_dsfield(old_iph, skb);
		if (tos && !info)
			use_cache = false;

		if (addr_family == AF_INET)
			udp_sum = !(flags & VXLAN_F_UDP_ZERO_CSUM_TX);
		else
			udp_sum = !(flags & VXLAN_F_UDP_ZERO_CSUM6_TX);
#if IS_ENABLED(CONFIG_IPV6)
		switch (vxlan->cfg.label_policy) {
		case VXLAN_LABEL_FIXED:
			key.label = vxlan->cfg.label;
			break;
		case VXLAN_LABEL_INHERIT:
			key.label = ip_tunnel_get_flowlabel(old_iph, skb);
			break;
		default:
			DEBUG_NET_WARN_ON_ONCE(1);
			goto drop;
		}
#endif
	} else {
		if (!info) {
			WARN_ONCE(1, "%s: Missing encapsulation instructions\n",
				  dev->name);
			goto drop;
		}
		pkey = &info->key;
		addr_family = ip_tunnel_info_af(info);
		dst_port = info->key.tp_dst ? : vxlan->cfg.dst_port;
		vni = tunnel_id_to_key32(info->key.tun_id);
		ifindex = 0;
		dst_cache = &info->dst_cache;
		if (test_bit(IP_TUNNEL_VXLAN_OPT_BIT, info->key.tun_flags)) {
			if (info->options_len < sizeof(*md))
				goto drop;
			md = ip_tunnel_info_opts(info);
		}
		ttl = info->key.ttl;
		tos = info->key.tos;
		udp_sum = test_bit(IP_TUNNEL_CSUM_BIT, info->key.tun_flags);
	}
	src_port = udp_flow_src_port(dev_net(dev), skb, vxlan->cfg.port_min,
				     vxlan->cfg.port_max, true);

	rcu_read_lock();
	if (addr_family == AF_INET) {
		struct vxlan_sock *sock4 = rcu_dereference(vxlan->vn4_sock);
		struct rtable *rt;
		__be16 df = 0;
		__be32 saddr;

		if (!ifindex)
			ifindex = sock4->sock->sk->sk_bound_dev_if;

		rt = udp_tunnel_dst_lookup(skb, dev, vxlan->net, ifindex,
					   &saddr, pkey, src_port, dst_port,
					   tos, use_cache ? dst_cache : NULL);
		if (IS_ERR(rt)) {
			err = PTR_ERR(rt);
			reason = SKB_DROP_REASON_IP_OUTNOROUTES;
			goto tx_error;
		}

		if (!info) {
			/* Bypass encapsulation if the destination is local */
			err = encap_bypass_if_local(skb, dev, vxlan, AF_INET,
						    dst_port, ifindex, vni,
						    &rt->dst, rt->rt_flags);
			if (err)
				goto out_unlock;

			if (vxlan->cfg.df == VXLAN_DF_SET) {
				df = htons(IP_DF);
			} else if (vxlan->cfg.df == VXLAN_DF_INHERIT) {
				struct ethhdr *eth = eth_hdr(skb);

				if (ntohs(eth->h_proto) == ETH_P_IPV6 ||
				    (ntohs(eth->h_proto) == ETH_P_IP &&
				     old_iph->frag_off & htons(IP_DF)))
					df = htons(IP_DF);
			}
		} else if (test_bit(IP_TUNNEL_DONT_FRAGMENT_BIT,
				    info->key.tun_flags)) {
			df = htons(IP_DF);
		}

		ndst = &rt->dst;
		err = skb_tunnel_check_pmtu(skb, ndst, vxlan_headroom(flags & VXLAN_F_GPE),
					    netif_is_any_bridge_port(dev));
		if (err < 0) {
			goto tx_error;
		} else if (err) {
			if (info) {
				struct ip_tunnel_info *unclone;

				unclone = skb_tunnel_info_unclone(skb);
				if (unlikely(!unclone))
					goto tx_error;

				unclone->key.u.ipv4.src = pkey->u.ipv4.dst;
				unclone->key.u.ipv4.dst = saddr;
			}
			vxlan_encap_bypass(skb, vxlan, vxlan, vni, false);
			dst_release(ndst);
			goto out_unlock;
		}

		tos = ip_tunnel_ecn_encap(tos, old_iph, skb);
		ttl = ttl ? : ip4_dst_hoplimit(&rt->dst);
		err = vxlan_build_skb(skb, ndst, sizeof(struct iphdr),
				      vni, md, flags, udp_sum);
		if (err < 0) {
			reason = SKB_DROP_REASON_NOMEM;
			goto tx_error;
		}

		udp_tunnel_xmit_skb(rt, sock4->sock->sk, skb, saddr,
				    pkey->u.ipv4.dst, tos, ttl, df,
				    src_port, dst_port, xnet, !udp_sum);
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		struct vxlan_sock *sock6 = rcu_dereference(vxlan->vn6_sock);
		struct in6_addr saddr;

		if (!ifindex)
			ifindex = sock6->sock->sk->sk_bound_dev_if;

		ndst = udp_tunnel6_dst_lookup(skb, dev, vxlan->net, sock6->sock,
					      ifindex, &saddr, pkey,
					      src_port, dst_port, tos,
					      use_cache ? dst_cache : NULL);
		if (IS_ERR(ndst)) {
			err = PTR_ERR(ndst);
			ndst = NULL;
			reason = SKB_DROP_REASON_IP_OUTNOROUTES;
			goto tx_error;
		}

		if (!info) {
			u32 rt6i_flags = dst_rt6_info(ndst)->rt6i_flags;

			err = encap_bypass_if_local(skb, dev, vxlan, AF_INET6,
						    dst_port, ifindex, vni,
						    ndst, rt6i_flags);
			if (err)
				goto out_unlock;
		}

		err = skb_tunnel_check_pmtu(skb, ndst,
					    vxlan_headroom((flags & VXLAN_F_GPE) | VXLAN_F_IPV6),
					    netif_is_any_bridge_port(dev));
		if (err < 0) {
			goto tx_error;
		} else if (err) {
			if (info) {
				struct ip_tunnel_info *unclone;

				unclone = skb_tunnel_info_unclone(skb);
				if (unlikely(!unclone))
					goto tx_error;

				unclone->key.u.ipv6.src = pkey->u.ipv6.dst;
				unclone->key.u.ipv6.dst = saddr;
			}

			vxlan_encap_bypass(skb, vxlan, vxlan, vni, false);
			dst_release(ndst);
			goto out_unlock;
		}

		tos = ip_tunnel_ecn_encap(tos, old_iph, skb);
		ttl = ttl ? : ip6_dst_hoplimit(ndst);
		skb_scrub_packet(skb, xnet);
		err = vxlan_build_skb(skb, ndst, sizeof(struct ipv6hdr),
				      vni, md, flags, udp_sum);
		if (err < 0) {
			reason = SKB_DROP_REASON_NOMEM;
			goto tx_error;
		}

		udp_tunnel6_xmit_skb(ndst, sock6->sock->sk, skb, dev,
				     &saddr, &pkey->u.ipv6.dst, tos, ttl,
				     pkey->label, src_port, dst_port, !udp_sum);
#endif
	}
	vxlan_vnifilter_count(vxlan, vni, NULL, VXLAN_VNI_STATS_TX, pkt_len);
out_unlock:
	rcu_read_unlock();
	return;

drop:
	dev_core_stats_tx_dropped_inc(dev);
	vxlan_vnifilter_count(vxlan, vni, NULL, VXLAN_VNI_STATS_TX_DROPS, 0);
	kfree_skb_reason(skb, reason);
	return;

tx_error:
	rcu_read_unlock();
	if (err == -ELOOP)
		DEV_STATS_INC(dev, collisions);
	else if (err == -ENETUNREACH)
		DEV_STATS_INC(dev, tx_carrier_errors);
	dst_release(ndst);
	DEV_STATS_INC(dev, tx_errors);
	vxlan_vnifilter_count(vxlan, vni, NULL, VXLAN_VNI_STATS_TX_ERRORS, 0);
	kfree_skb_reason(skb, reason);
}

static void vxlan_xmit_nh(struct sk_buff *skb, struct net_device *dev,
			  struct vxlan_fdb *f, __be32 vni, bool did_rsc)
{
	struct vxlan_rdst nh_rdst;
	struct nexthop *nh;
	bool do_xmit;
	u32 hash;

	memset(&nh_rdst, 0, sizeof(struct vxlan_rdst));
	hash = skb_get_hash(skb);

	rcu_read_lock();
	nh = rcu_dereference(f->nh);
	if (!nh) {
		rcu_read_unlock();
		goto drop;
	}
	do_xmit = vxlan_fdb_nh_path_select(nh, hash, &nh_rdst);
	rcu_read_unlock();

	if (likely(do_xmit))
		vxlan_xmit_one(skb, dev, vni, &nh_rdst, did_rsc);
	else
		goto drop;

	return;

drop:
	dev_core_stats_tx_dropped_inc(dev);
	vxlan_vnifilter_count(netdev_priv(dev), vni, NULL,
			      VXLAN_VNI_STATS_TX_DROPS, 0);
	dev_kfree_skb(skb);
}

static netdev_tx_t vxlan_xmit_nhid(struct sk_buff *skb, struct net_device *dev,
				   u32 nhid, __be32 vni)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct vxlan_rdst nh_rdst;
	struct nexthop *nh;
	bool do_xmit;
	u32 hash;

	memset(&nh_rdst, 0, sizeof(struct vxlan_rdst));
	hash = skb_get_hash(skb);

	rcu_read_lock();
	nh = nexthop_find_by_id(dev_net(dev), nhid);
	if (unlikely(!nh || !nexthop_is_fdb(nh) || !nexthop_is_multipath(nh))) {
		rcu_read_unlock();
		goto drop;
	}
	do_xmit = vxlan_fdb_nh_path_select(nh, hash, &nh_rdst);
	rcu_read_unlock();

	if (vxlan->cfg.saddr.sa.sa_family != nh_rdst.remote_ip.sa.sa_family)
		goto drop;

	if (likely(do_xmit))
		vxlan_xmit_one(skb, dev, vni, &nh_rdst, false);
	else
		goto drop;

	return NETDEV_TX_OK;

drop:
	dev_core_stats_tx_dropped_inc(dev);
	vxlan_vnifilter_count(netdev_priv(dev), vni, NULL,
			      VXLAN_VNI_STATS_TX_DROPS, 0);
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
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
	struct vxlan_rdst *rdst, *fdst = NULL;
	const struct ip_tunnel_info *info;
	struct vxlan_fdb *f;
	struct ethhdr *eth;
	__be32 vni = 0;
	u32 nhid = 0;
	bool did_rsc;

	info = skb_tunnel_info(skb);

	skb_reset_mac_header(skb);

	if (vxlan->cfg.flags & VXLAN_F_COLLECT_METADATA) {
		if (info && info->mode & IP_TUNNEL_INFO_BRIDGE &&
		    info->mode & IP_TUNNEL_INFO_TX) {
			vni = tunnel_id_to_key32(info->key.tun_id);
			nhid = info->key.nhid;
		} else {
			if (info && info->mode & IP_TUNNEL_INFO_TX)
				vxlan_xmit_one(skb, dev, vni, NULL, false);
			else
				kfree_skb_reason(skb, SKB_DROP_REASON_TUNNEL_TXINFO);
			return NETDEV_TX_OK;
		}
	}

	if (vxlan->cfg.flags & VXLAN_F_PROXY) {
		eth = eth_hdr(skb);
		if (ntohs(eth->h_proto) == ETH_P_ARP)
			return arp_reduce(dev, skb, vni);
#if IS_ENABLED(CONFIG_IPV6)
		else if (ntohs(eth->h_proto) == ETH_P_IPV6 &&
			 pskb_may_pull(skb, sizeof(struct ipv6hdr) +
					    sizeof(struct nd_msg)) &&
			 ipv6_hdr(skb)->nexthdr == IPPROTO_ICMPV6) {
			struct nd_msg *m = (struct nd_msg *)(ipv6_hdr(skb) + 1);

			if (m->icmph.icmp6_code == 0 &&
			    m->icmph.icmp6_type == NDISC_NEIGHBOUR_SOLICITATION)
				return neigh_reduce(dev, skb, vni);
		}
#endif
	}

	if (nhid)
		return vxlan_xmit_nhid(skb, dev, nhid, vni);

	if (vxlan->cfg.flags & VXLAN_F_MDB) {
		struct vxlan_mdb_entry *mdb_entry;

		rcu_read_lock();
		mdb_entry = vxlan_mdb_entry_skb_get(vxlan, skb, vni);
		if (mdb_entry) {
			netdev_tx_t ret;

			ret = vxlan_mdb_xmit(vxlan, mdb_entry, skb);
			rcu_read_unlock();
			return ret;
		}
		rcu_read_unlock();
	}

	eth = eth_hdr(skb);
	f = vxlan_find_mac(vxlan, eth->h_dest, vni);
	did_rsc = false;

	if (f && (f->flags & NTF_ROUTER) && (vxlan->cfg.flags & VXLAN_F_RSC) &&
	    (ntohs(eth->h_proto) == ETH_P_IP ||
	     ntohs(eth->h_proto) == ETH_P_IPV6)) {
		did_rsc = route_shortcircuit(dev, skb);
		if (did_rsc)
			f = vxlan_find_mac(vxlan, eth->h_dest, vni);
	}

	if (f == NULL) {
		f = vxlan_find_mac(vxlan, all_zeros_mac, vni);
		if (f == NULL) {
			if ((vxlan->cfg.flags & VXLAN_F_L2MISS) &&
			    !is_multicast_ether_addr(eth->h_dest))
				vxlan_fdb_miss(vxlan, eth->h_dest);

			dev_core_stats_tx_dropped_inc(dev);
			vxlan_vnifilter_count(vxlan, vni, NULL,
					      VXLAN_VNI_STATS_TX_DROPS, 0);
			kfree_skb_reason(skb, SKB_DROP_REASON_VXLAN_NO_REMOTE);
			return NETDEV_TX_OK;
		}
	}

	if (rcu_access_pointer(f->nh)) {
		vxlan_xmit_nh(skb, dev, f,
			      (vni ? : vxlan->default_dst.remote_vni), did_rsc);
	} else {
		list_for_each_entry_rcu(rdst, &f->remotes, list) {
			struct sk_buff *skb1;

			if (!fdst) {
				fdst = rdst;
				continue;
			}
			skb1 = skb_clone(skb, GFP_ATOMIC);
			if (skb1)
				vxlan_xmit_one(skb1, dev, vni, rdst, did_rsc);
		}
		if (fdst)
			vxlan_xmit_one(skb, dev, vni, fdst, did_rsc);
		else
			kfree_skb_reason(skb, SKB_DROP_REASON_VXLAN_NO_REMOTE);
	}

	return NETDEV_TX_OK;
}

/* Walk the forwarding table and purge stale entries */
static void vxlan_cleanup(struct timer_list *t)
{
	struct vxlan_dev *vxlan = from_timer(vxlan, t, age_timer);
	unsigned long next_timer = jiffies + FDB_AGE_INTERVAL;
	unsigned int h;

	if (!netif_running(vxlan->dev))
		return;

	for (h = 0; h < FDB_HASH_SIZE; ++h) {
		struct hlist_node *p, *n;

		spin_lock(&vxlan->hash_lock[h]);
		hlist_for_each_safe(p, n, &vxlan->fdb_head[h]) {
			struct vxlan_fdb *f
				= container_of(p, struct vxlan_fdb, hlist);
			unsigned long timeout;

			if (f->state & (NUD_PERMANENT | NUD_NOARP))
				continue;

			if (f->flags & NTF_EXT_LEARNED)
				continue;

			timeout = f->used + vxlan->cfg.age_interval * HZ;
			if (time_before_eq(timeout, jiffies)) {
				netdev_dbg(vxlan->dev,
					   "garbage collect %pM\n",
					   f->eth_addr);
				f->state = NUD_STALE;
				vxlan_fdb_destroy(vxlan, f, true, true);
			} else if (time_before(timeout, next_timer))
				next_timer = timeout;
		}
		spin_unlock(&vxlan->hash_lock[h]);
	}

	mod_timer(&vxlan->age_timer, next_timer);
}

static void vxlan_vs_del_dev(struct vxlan_dev *vxlan)
{
	struct vxlan_net *vn = net_generic(vxlan->net, vxlan_net_id);

	spin_lock(&vn->sock_lock);
	hlist_del_init_rcu(&vxlan->hlist4.hlist);
#if IS_ENABLED(CONFIG_IPV6)
	hlist_del_init_rcu(&vxlan->hlist6.hlist);
#endif
	spin_unlock(&vn->sock_lock);
}

static void vxlan_vs_add_dev(struct vxlan_sock *vs, struct vxlan_dev *vxlan,
			     struct vxlan_dev_node *node)
{
	struct vxlan_net *vn = net_generic(vxlan->net, vxlan_net_id);
	__be32 vni = vxlan->default_dst.remote_vni;

	node->vxlan = vxlan;
	spin_lock(&vn->sock_lock);
	hlist_add_head_rcu(&node->hlist, vni_head(vs, vni));
	spin_unlock(&vn->sock_lock);
}

/* Setup stats when device is created */
static int vxlan_init(struct net_device *dev)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	int err;

	if (vxlan->cfg.flags & VXLAN_F_VNIFILTER)
		vxlan_vnigroup_init(vxlan);

	err = gro_cells_init(&vxlan->gro_cells, dev);
	if (err)
		goto err_vnigroup_uninit;

	err = vxlan_mdb_init(vxlan);
	if (err)
		goto err_gro_cells_destroy;

	netdev_lockdep_set_classes(dev);
	return 0;

err_gro_cells_destroy:
	gro_cells_destroy(&vxlan->gro_cells);
err_vnigroup_uninit:
	if (vxlan->cfg.flags & VXLAN_F_VNIFILTER)
		vxlan_vnigroup_uninit(vxlan);
	return err;
}

static void vxlan_fdb_delete_default(struct vxlan_dev *vxlan, __be32 vni)
{
	struct vxlan_fdb *f;
	u32 hash_index = fdb_head_index(vxlan, all_zeros_mac, vni);

	spin_lock_bh(&vxlan->hash_lock[hash_index]);
	f = __vxlan_find_mac(vxlan, all_zeros_mac, vni);
	if (f)
		vxlan_fdb_destroy(vxlan, f, true, true);
	spin_unlock_bh(&vxlan->hash_lock[hash_index]);
}

static void vxlan_uninit(struct net_device *dev)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);

	vxlan_mdb_fini(vxlan);

	if (vxlan->cfg.flags & VXLAN_F_VNIFILTER)
		vxlan_vnigroup_uninit(vxlan);

	gro_cells_destroy(&vxlan->gro_cells);

	vxlan_fdb_delete_default(vxlan, vxlan->cfg.vni);
}

/* Start ageing timer and join group when device is brought up */
static int vxlan_open(struct net_device *dev)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	int ret;

	ret = vxlan_sock_add(vxlan);
	if (ret < 0)
		return ret;

	ret = vxlan_multicast_join(vxlan);
	if (ret) {
		vxlan_sock_release(vxlan);
		return ret;
	}

	if (vxlan->cfg.age_interval)
		mod_timer(&vxlan->age_timer, jiffies + FDB_AGE_INTERVAL);

	return ret;
}

struct vxlan_fdb_flush_desc {
	bool				ignore_default_entry;
	unsigned long                   state;
	unsigned long			state_mask;
	unsigned long                   flags;
	unsigned long			flags_mask;
	__be32				src_vni;
	u32				nhid;
	__be32				vni;
	__be16				port;
	union vxlan_addr		dst_ip;
};

static bool vxlan_fdb_is_default_entry(const struct vxlan_fdb *f,
				       const struct vxlan_dev *vxlan)
{
	return is_zero_ether_addr(f->eth_addr) && f->vni == vxlan->cfg.vni;
}

static bool vxlan_fdb_nhid_matches(const struct vxlan_fdb *f, u32 nhid)
{
	struct nexthop *nh = rtnl_dereference(f->nh);

	return nh && nh->id == nhid;
}

static bool vxlan_fdb_flush_matches(const struct vxlan_fdb *f,
				    const struct vxlan_dev *vxlan,
				    const struct vxlan_fdb_flush_desc *desc)
{
	if (desc->state_mask && (f->state & desc->state_mask) != desc->state)
		return false;

	if (desc->flags_mask && (f->flags & desc->flags_mask) != desc->flags)
		return false;

	if (desc->ignore_default_entry && vxlan_fdb_is_default_entry(f, vxlan))
		return false;

	if (desc->src_vni && f->vni != desc->src_vni)
		return false;

	if (desc->nhid && !vxlan_fdb_nhid_matches(f, desc->nhid))
		return false;

	return true;
}

static bool
vxlan_fdb_flush_should_match_remotes(const struct vxlan_fdb_flush_desc *desc)
{
	return desc->vni || desc->port || desc->dst_ip.sa.sa_family;
}

static bool
vxlan_fdb_flush_remote_matches(const struct vxlan_fdb_flush_desc *desc,
			       const struct vxlan_rdst *rd)
{
	if (desc->vni && rd->remote_vni != desc->vni)
		return false;

	if (desc->port && rd->remote_port != desc->port)
		return false;

	if (desc->dst_ip.sa.sa_family &&
	    !vxlan_addr_equal(&rd->remote_ip, &desc->dst_ip))
		return false;

	return true;
}

static void
vxlan_fdb_flush_match_remotes(struct vxlan_fdb *f, struct vxlan_dev *vxlan,
			      const struct vxlan_fdb_flush_desc *desc,
			      bool *p_destroy_fdb)
{
	bool remotes_flushed = false;
	struct vxlan_rdst *rd, *tmp;

	list_for_each_entry_safe(rd, tmp, &f->remotes, list) {
		if (!vxlan_fdb_flush_remote_matches(desc, rd))
			continue;

		vxlan_fdb_dst_destroy(vxlan, f, rd, true);
		remotes_flushed = true;
	}

	*p_destroy_fdb = remotes_flushed && list_empty(&f->remotes);
}

/* Purge the forwarding table */
static void vxlan_flush(struct vxlan_dev *vxlan,
			const struct vxlan_fdb_flush_desc *desc)
{
	bool match_remotes = vxlan_fdb_flush_should_match_remotes(desc);
	unsigned int h;

	for (h = 0; h < FDB_HASH_SIZE; ++h) {
		struct hlist_node *p, *n;

		spin_lock_bh(&vxlan->hash_lock[h]);
		hlist_for_each_safe(p, n, &vxlan->fdb_head[h]) {
			struct vxlan_fdb *f
				= container_of(p, struct vxlan_fdb, hlist);

			if (!vxlan_fdb_flush_matches(f, vxlan, desc))
				continue;

			if (match_remotes) {
				bool destroy_fdb = false;

				vxlan_fdb_flush_match_remotes(f, vxlan, desc,
							      &destroy_fdb);

				if (!destroy_fdb)
					continue;
			}

			vxlan_fdb_destroy(vxlan, f, true, true);
		}
		spin_unlock_bh(&vxlan->hash_lock[h]);
	}
}

static const struct nla_policy vxlan_del_bulk_policy[NDA_MAX + 1] = {
	[NDA_SRC_VNI]   = { .type = NLA_U32 },
	[NDA_NH_ID]	= { .type = NLA_U32 },
	[NDA_VNI]	= { .type = NLA_U32 },
	[NDA_PORT]	= { .type = NLA_U16 },
	[NDA_DST]	= NLA_POLICY_RANGE(NLA_BINARY, sizeof(struct in_addr),
					   sizeof(struct in6_addr)),
	[NDA_NDM_STATE_MASK]	= { .type = NLA_U16 },
	[NDA_NDM_FLAGS_MASK]	= { .type = NLA_U8 },
};

#define VXLAN_FDB_FLUSH_IGNORED_NDM_FLAGS (NTF_MASTER | NTF_SELF)
#define VXLAN_FDB_FLUSH_ALLOWED_NDM_STATES (NUD_PERMANENT | NUD_NOARP)
#define VXLAN_FDB_FLUSH_ALLOWED_NDM_FLAGS (NTF_EXT_LEARNED | NTF_OFFLOADED | \
					   NTF_ROUTER)

static int vxlan_fdb_delete_bulk(struct nlmsghdr *nlh, struct net_device *dev,
				 struct netlink_ext_ack *extack)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct vxlan_fdb_flush_desc desc = {};
	struct ndmsg *ndm = nlmsg_data(nlh);
	struct nlattr *tb[NDA_MAX + 1];
	u8 ndm_flags;
	int err;

	ndm_flags = ndm->ndm_flags & ~VXLAN_FDB_FLUSH_IGNORED_NDM_FLAGS;

	err = nlmsg_parse(nlh, sizeof(*ndm), tb, NDA_MAX, vxlan_del_bulk_policy,
			  extack);
	if (err)
		return err;

	if (ndm_flags & ~VXLAN_FDB_FLUSH_ALLOWED_NDM_FLAGS) {
		NL_SET_ERR_MSG(extack, "Unsupported fdb flush ndm flag bits set");
		return -EINVAL;
	}
	if (ndm->ndm_state & ~VXLAN_FDB_FLUSH_ALLOWED_NDM_STATES) {
		NL_SET_ERR_MSG(extack, "Unsupported fdb flush ndm state bits set");
		return -EINVAL;
	}

	desc.state = ndm->ndm_state;
	desc.flags = ndm_flags;

	if (tb[NDA_NDM_STATE_MASK])
		desc.state_mask = nla_get_u16(tb[NDA_NDM_STATE_MASK]);

	if (tb[NDA_NDM_FLAGS_MASK])
		desc.flags_mask = nla_get_u8(tb[NDA_NDM_FLAGS_MASK]);

	if (tb[NDA_SRC_VNI])
		desc.src_vni = cpu_to_be32(nla_get_u32(tb[NDA_SRC_VNI]));

	if (tb[NDA_NH_ID])
		desc.nhid = nla_get_u32(tb[NDA_NH_ID]);

	if (tb[NDA_VNI])
		desc.vni = cpu_to_be32(nla_get_u32(tb[NDA_VNI]));

	if (tb[NDA_PORT])
		desc.port = nla_get_be16(tb[NDA_PORT]);

	if (tb[NDA_DST]) {
		union vxlan_addr ip;

		err = vxlan_nla_get_addr(&ip, tb[NDA_DST]);
		if (err) {
			NL_SET_ERR_MSG_ATTR(extack, tb[NDA_DST],
					    "Unsupported address family");
			return err;
		}
		desc.dst_ip = ip;
	}

	vxlan_flush(vxlan, &desc);

	return 0;
}

/* Cleanup timer and forwarding table on shutdown */
static int vxlan_stop(struct net_device *dev)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct vxlan_fdb_flush_desc desc = {
		/* Default entry is deleted at vxlan_uninit. */
		.ignore_default_entry = true,
		.state = 0,
		.state_mask = NUD_PERMANENT | NUD_NOARP,
	};

	vxlan_multicast_leave(vxlan);

	del_timer_sync(&vxlan->age_timer);

	vxlan_flush(vxlan, &desc);
	vxlan_sock_release(vxlan);

	return 0;
}

/* Stub, nothing needs to be done. */
static void vxlan_set_multicast_list(struct net_device *dev)
{
}

static int vxlan_change_mtu(struct net_device *dev, int new_mtu)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct vxlan_rdst *dst = &vxlan->default_dst;
	struct net_device *lowerdev = __dev_get_by_index(vxlan->net,
							 dst->remote_ifindex);

	/* This check is different than dev->max_mtu, because it looks at
	 * the lowerdev->mtu, rather than the static dev->max_mtu
	 */
	if (lowerdev) {
		int max_mtu = lowerdev->mtu - vxlan_headroom(vxlan->cfg.flags);
		if (new_mtu > max_mtu)
			return -EINVAL;
	}

	WRITE_ONCE(dev->mtu, new_mtu);
	return 0;
}

static int vxlan_fill_metadata_dst(struct net_device *dev, struct sk_buff *skb)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct ip_tunnel_info *info = skb_tunnel_info(skb);
	__be16 sport, dport;

	sport = udp_flow_src_port(dev_net(dev), skb, vxlan->cfg.port_min,
				  vxlan->cfg.port_max, true);
	dport = info->key.tp_dst ? : vxlan->cfg.dst_port;

	if (ip_tunnel_info_af(info) == AF_INET) {
		struct vxlan_sock *sock4 = rcu_dereference(vxlan->vn4_sock);
		struct rtable *rt;

		if (!sock4)
			return -EIO;

		rt = udp_tunnel_dst_lookup(skb, dev, vxlan->net, 0,
					   &info->key.u.ipv4.src,
					   &info->key,
					   sport, dport, info->key.tos,
					   &info->dst_cache);
		if (IS_ERR(rt))
			return PTR_ERR(rt);
		ip_rt_put(rt);
	} else {
#if IS_ENABLED(CONFIG_IPV6)
		struct vxlan_sock *sock6 = rcu_dereference(vxlan->vn6_sock);
		struct dst_entry *ndst;

		if (!sock6)
			return -EIO;

		ndst = udp_tunnel6_dst_lookup(skb, dev, vxlan->net, sock6->sock,
					      0, &info->key.u.ipv6.src,
					      &info->key,
					      sport, dport, info->key.tos,
					      &info->dst_cache);
		if (IS_ERR(ndst))
			return PTR_ERR(ndst);
		dst_release(ndst);
#else /* !CONFIG_IPV6 */
		return -EPFNOSUPPORT;
#endif
	}
	info->key.tp_src = sport;
	info->key.tp_dst = dport;
	return 0;
}

static const struct net_device_ops vxlan_netdev_ether_ops = {
	.ndo_init		= vxlan_init,
	.ndo_uninit		= vxlan_uninit,
	.ndo_open		= vxlan_open,
	.ndo_stop		= vxlan_stop,
	.ndo_start_xmit		= vxlan_xmit,
	.ndo_set_rx_mode	= vxlan_set_multicast_list,
	.ndo_change_mtu		= vxlan_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_fdb_add		= vxlan_fdb_add,
	.ndo_fdb_del		= vxlan_fdb_delete,
	.ndo_fdb_del_bulk	= vxlan_fdb_delete_bulk,
	.ndo_fdb_dump		= vxlan_fdb_dump,
	.ndo_fdb_get		= vxlan_fdb_get,
	.ndo_mdb_add		= vxlan_mdb_add,
	.ndo_mdb_del		= vxlan_mdb_del,
	.ndo_mdb_del_bulk	= vxlan_mdb_del_bulk,
	.ndo_mdb_dump		= vxlan_mdb_dump,
	.ndo_mdb_get		= vxlan_mdb_get,
	.ndo_fill_metadata_dst	= vxlan_fill_metadata_dst,
};

static const struct net_device_ops vxlan_netdev_raw_ops = {
	.ndo_init		= vxlan_init,
	.ndo_uninit		= vxlan_uninit,
	.ndo_open		= vxlan_open,
	.ndo_stop		= vxlan_stop,
	.ndo_start_xmit		= vxlan_xmit,
	.ndo_change_mtu		= vxlan_change_mtu,
	.ndo_fill_metadata_dst	= vxlan_fill_metadata_dst,
};

/* Info for udev, that this is a virtual tunnel endpoint */
static const struct device_type vxlan_type = {
	.name = "vxlan",
};

/* Calls the ndo_udp_tunnel_add of the caller in order to
 * supply the listening VXLAN udp ports. Callers are expected
 * to implement the ndo_udp_tunnel_add.
 */
static void vxlan_offload_rx_ports(struct net_device *dev, bool push)
{
	struct vxlan_sock *vs;
	struct net *net = dev_net(dev);
	struct vxlan_net *vn = net_generic(net, vxlan_net_id);
	unsigned int i;

	spin_lock(&vn->sock_lock);
	for (i = 0; i < PORT_HASH_SIZE; ++i) {
		hlist_for_each_entry_rcu(vs, &vn->sock_list[i], hlist) {
			unsigned short type;

			if (vs->flags & VXLAN_F_GPE)
				type = UDP_TUNNEL_TYPE_VXLAN_GPE;
			else
				type = UDP_TUNNEL_TYPE_VXLAN;

			if (push)
				udp_tunnel_push_rx_port(dev, vs->sock, type);
			else
				udp_tunnel_drop_rx_port(dev, vs->sock, type);
		}
	}
	spin_unlock(&vn->sock_lock);
}

/* Initialize the device structure. */
static void vxlan_setup(struct net_device *dev)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	unsigned int h;

	eth_hw_addr_random(dev);
	ether_setup(dev);

	dev->needs_free_netdev = true;
	SET_NETDEV_DEVTYPE(dev, &vxlan_type);

	dev->features	|= NETIF_F_SG | NETIF_F_HW_CSUM | NETIF_F_FRAGLIST;
	dev->features   |= NETIF_F_RXCSUM;
	dev->features   |= NETIF_F_GSO_SOFTWARE;

	dev->vlan_features = dev->features;
	dev->hw_features |= NETIF_F_SG | NETIF_F_HW_CSUM | NETIF_F_FRAGLIST;
	dev->hw_features |= NETIF_F_RXCSUM;
	dev->hw_features |= NETIF_F_GSO_SOFTWARE;
	netif_keep_dst(dev);
	dev->priv_flags |= IFF_NO_QUEUE;
	dev->change_proto_down = true;
	dev->lltx = true;

	/* MTU range: 68 - 65535 */
	dev->min_mtu = ETH_MIN_MTU;
	dev->max_mtu = ETH_MAX_MTU;

	dev->pcpu_stat_type = NETDEV_PCPU_STAT_TSTATS;
	INIT_LIST_HEAD(&vxlan->next);

	timer_setup(&vxlan->age_timer, vxlan_cleanup, TIMER_DEFERRABLE);

	vxlan->dev = dev;

	for (h = 0; h < FDB_HASH_SIZE; ++h) {
		spin_lock_init(&vxlan->hash_lock[h]);
		INIT_HLIST_HEAD(&vxlan->fdb_head[h]);
	}
}

static void vxlan_ether_setup(struct net_device *dev)
{
	dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE;
	dev->netdev_ops = &vxlan_netdev_ether_ops;
}

static void vxlan_raw_setup(struct net_device *dev)
{
	dev->header_ops = NULL;
	dev->type = ARPHRD_NONE;
	dev->hard_header_len = 0;
	dev->addr_len = 0;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
	dev->netdev_ops = &vxlan_netdev_raw_ops;
}

static const struct nla_policy vxlan_policy[IFLA_VXLAN_MAX + 1] = {
	[IFLA_VXLAN_UNSPEC]     = { .strict_start_type = IFLA_VXLAN_LOCALBYPASS },
	[IFLA_VXLAN_ID]		= { .type = NLA_U32 },
	[IFLA_VXLAN_GROUP]	= { .len = sizeof_field(struct iphdr, daddr) },
	[IFLA_VXLAN_GROUP6]	= { .len = sizeof(struct in6_addr) },
	[IFLA_VXLAN_LINK]	= { .type = NLA_U32 },
	[IFLA_VXLAN_LOCAL]	= { .len = sizeof_field(struct iphdr, saddr) },
	[IFLA_VXLAN_LOCAL6]	= { .len = sizeof(struct in6_addr) },
	[IFLA_VXLAN_TOS]	= { .type = NLA_U8 },
	[IFLA_VXLAN_TTL]	= { .type = NLA_U8 },
	[IFLA_VXLAN_LABEL]	= { .type = NLA_U32 },
	[IFLA_VXLAN_LEARNING]	= { .type = NLA_U8 },
	[IFLA_VXLAN_AGEING]	= { .type = NLA_U32 },
	[IFLA_VXLAN_LIMIT]	= { .type = NLA_U32 },
	[IFLA_VXLAN_PORT_RANGE] = { .len  = sizeof(struct ifla_vxlan_port_range) },
	[IFLA_VXLAN_PROXY]	= { .type = NLA_U8 },
	[IFLA_VXLAN_RSC]	= { .type = NLA_U8 },
	[IFLA_VXLAN_L2MISS]	= { .type = NLA_U8 },
	[IFLA_VXLAN_L3MISS]	= { .type = NLA_U8 },
	[IFLA_VXLAN_COLLECT_METADATA]	= { .type = NLA_U8 },
	[IFLA_VXLAN_PORT]	= { .type = NLA_U16 },
	[IFLA_VXLAN_UDP_CSUM]	= { .type = NLA_U8 },
	[IFLA_VXLAN_UDP_ZERO_CSUM6_TX]	= { .type = NLA_U8 },
	[IFLA_VXLAN_UDP_ZERO_CSUM6_RX]	= { .type = NLA_U8 },
	[IFLA_VXLAN_REMCSUM_TX]	= { .type = NLA_U8 },
	[IFLA_VXLAN_REMCSUM_RX]	= { .type = NLA_U8 },
	[IFLA_VXLAN_GBP]	= { .type = NLA_FLAG, },
	[IFLA_VXLAN_GPE]	= { .type = NLA_FLAG, },
	[IFLA_VXLAN_REMCSUM_NOPARTIAL]	= { .type = NLA_FLAG },
	[IFLA_VXLAN_TTL_INHERIT]	= { .type = NLA_FLAG },
	[IFLA_VXLAN_DF]		= { .type = NLA_U8 },
	[IFLA_VXLAN_VNIFILTER]	= { .type = NLA_U8 },
	[IFLA_VXLAN_LOCALBYPASS]	= NLA_POLICY_MAX(NLA_U8, 1),
	[IFLA_VXLAN_LABEL_POLICY]       = NLA_POLICY_MAX(NLA_U32, VXLAN_LABEL_MAX),
};

static int vxlan_validate(struct nlattr *tb[], struct nlattr *data[],
			  struct netlink_ext_ack *extack)
{
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_ADDRESS],
					    "Provided link layer address is not Ethernet");
			return -EINVAL;
		}

		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS]))) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_ADDRESS],
					    "Provided Ethernet address is not unicast");
			return -EADDRNOTAVAIL;
		}
	}

	if (tb[IFLA_MTU]) {
		u32 mtu = nla_get_u32(tb[IFLA_MTU]);

		if (mtu < ETH_MIN_MTU || mtu > ETH_MAX_MTU) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_MTU],
					    "MTU must be between 68 and 65535");
			return -EINVAL;
		}
	}

	if (!data) {
		NL_SET_ERR_MSG(extack,
			       "Required attributes not provided to perform the operation");
		return -EINVAL;
	}

	if (data[IFLA_VXLAN_ID]) {
		u32 id = nla_get_u32(data[IFLA_VXLAN_ID]);

		if (id >= VXLAN_N_VID) {
			NL_SET_ERR_MSG_ATTR(extack, data[IFLA_VXLAN_ID],
					    "VXLAN ID must be lower than 16777216");
			return -ERANGE;
		}
	}

	if (data[IFLA_VXLAN_PORT_RANGE]) {
		const struct ifla_vxlan_port_range *p
			= nla_data(data[IFLA_VXLAN_PORT_RANGE]);

		if (ntohs(p->high) < ntohs(p->low)) {
			NL_SET_ERR_MSG_ATTR(extack, data[IFLA_VXLAN_PORT_RANGE],
					    "Invalid source port range");
			return -EINVAL;
		}
	}

	if (data[IFLA_VXLAN_DF]) {
		enum ifla_vxlan_df df = nla_get_u8(data[IFLA_VXLAN_DF]);

		if (df < 0 || df > VXLAN_DF_MAX) {
			NL_SET_ERR_MSG_ATTR(extack, data[IFLA_VXLAN_DF],
					    "Invalid DF attribute");
			return -EINVAL;
		}
	}

	return 0;
}

static void vxlan_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *drvinfo)
{
	strscpy(drvinfo->version, VXLAN_VERSION, sizeof(drvinfo->version));
	strscpy(drvinfo->driver, "vxlan", sizeof(drvinfo->driver));
}

static int vxlan_get_link_ksettings(struct net_device *dev,
				    struct ethtool_link_ksettings *cmd)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct vxlan_rdst *dst = &vxlan->default_dst;
	struct net_device *lowerdev = __dev_get_by_index(vxlan->net,
							 dst->remote_ifindex);

	if (!lowerdev) {
		cmd->base.duplex = DUPLEX_UNKNOWN;
		cmd->base.port = PORT_OTHER;
		cmd->base.speed = SPEED_UNKNOWN;

		return 0;
	}

	return __ethtool_get_link_ksettings(lowerdev, cmd);
}

static const struct ethtool_ops vxlan_ethtool_ops = {
	.get_drvinfo		= vxlan_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= vxlan_get_link_ksettings,
};

static struct socket *vxlan_create_sock(struct net *net, bool ipv6,
					__be16 port, u32 flags, int ifindex)
{
	struct socket *sock;
	struct udp_port_cfg udp_conf;
	int err;

	memset(&udp_conf, 0, sizeof(udp_conf));

	if (ipv6) {
		udp_conf.family = AF_INET6;
		udp_conf.use_udp6_rx_checksums =
		    !(flags & VXLAN_F_UDP_ZERO_CSUM6_RX);
		udp_conf.ipv6_v6only = 1;
	} else {
		udp_conf.family = AF_INET;
	}

	udp_conf.local_udp_port = port;
	udp_conf.bind_ifindex = ifindex;

	/* Open UDP socket */
	err = udp_sock_create(net, &udp_conf, &sock);
	if (err < 0)
		return ERR_PTR(err);

	udp_allow_gso(sock->sk);
	return sock;
}

/* Create new listen socket if needed */
static struct vxlan_sock *vxlan_socket_create(struct net *net, bool ipv6,
					      __be16 port, u32 flags,
					      int ifindex)
{
	struct vxlan_net *vn = net_generic(net, vxlan_net_id);
	struct vxlan_sock *vs;
	struct socket *sock;
	unsigned int h;
	struct udp_tunnel_sock_cfg tunnel_cfg;

	vs = kzalloc(sizeof(*vs), GFP_KERNEL);
	if (!vs)
		return ERR_PTR(-ENOMEM);

	for (h = 0; h < VNI_HASH_SIZE; ++h)
		INIT_HLIST_HEAD(&vs->vni_list[h]);

	sock = vxlan_create_sock(net, ipv6, port, flags, ifindex);
	if (IS_ERR(sock)) {
		kfree(vs);
		return ERR_CAST(sock);
	}

	vs->sock = sock;
	refcount_set(&vs->refcnt, 1);
	vs->flags = (flags & VXLAN_F_RCV_FLAGS);

	spin_lock(&vn->sock_lock);
	hlist_add_head_rcu(&vs->hlist, vs_head(net, port));
	udp_tunnel_notify_add_rx_port(sock,
				      (vs->flags & VXLAN_F_GPE) ?
				      UDP_TUNNEL_TYPE_VXLAN_GPE :
				      UDP_TUNNEL_TYPE_VXLAN);
	spin_unlock(&vn->sock_lock);

	/* Mark socket as an encapsulation socket. */
	memset(&tunnel_cfg, 0, sizeof(tunnel_cfg));
	tunnel_cfg.sk_user_data = vs;
	tunnel_cfg.encap_type = 1;
	tunnel_cfg.encap_rcv = vxlan_rcv;
	tunnel_cfg.encap_err_lookup = vxlan_err_lookup;
	tunnel_cfg.encap_destroy = NULL;
	if (vs->flags & VXLAN_F_GPE) {
		tunnel_cfg.gro_receive = vxlan_gpe_gro_receive;
		tunnel_cfg.gro_complete = vxlan_gpe_gro_complete;
	} else {
		tunnel_cfg.gro_receive = vxlan_gro_receive;
		tunnel_cfg.gro_complete = vxlan_gro_complete;
	}

	setup_udp_tunnel_sock(net, sock, &tunnel_cfg);

	return vs;
}

static int __vxlan_sock_add(struct vxlan_dev *vxlan, bool ipv6)
{
	struct vxlan_net *vn = net_generic(vxlan->net, vxlan_net_id);
	bool metadata = vxlan->cfg.flags & VXLAN_F_COLLECT_METADATA;
	struct vxlan_sock *vs = NULL;
	struct vxlan_dev_node *node;
	int l3mdev_index = 0;

	if (vxlan->cfg.remote_ifindex)
		l3mdev_index = l3mdev_master_upper_ifindex_by_index(
			vxlan->net, vxlan->cfg.remote_ifindex);

	if (!vxlan->cfg.no_share) {
		spin_lock(&vn->sock_lock);
		vs = vxlan_find_sock(vxlan->net, ipv6 ? AF_INET6 : AF_INET,
				     vxlan->cfg.dst_port, vxlan->cfg.flags,
				     l3mdev_index);
		if (vs && !refcount_inc_not_zero(&vs->refcnt)) {
			spin_unlock(&vn->sock_lock);
			return -EBUSY;
		}
		spin_unlock(&vn->sock_lock);
	}
	if (!vs)
		vs = vxlan_socket_create(vxlan->net, ipv6,
					 vxlan->cfg.dst_port, vxlan->cfg.flags,
					 l3mdev_index);
	if (IS_ERR(vs))
		return PTR_ERR(vs);
#if IS_ENABLED(CONFIG_IPV6)
	if (ipv6) {
		rcu_assign_pointer(vxlan->vn6_sock, vs);
		node = &vxlan->hlist6;
	} else
#endif
	{
		rcu_assign_pointer(vxlan->vn4_sock, vs);
		node = &vxlan->hlist4;
	}

	if (metadata && (vxlan->cfg.flags & VXLAN_F_VNIFILTER))
		vxlan_vs_add_vnigrp(vxlan, vs, ipv6);
	else
		vxlan_vs_add_dev(vs, vxlan, node);

	return 0;
}

static int vxlan_sock_add(struct vxlan_dev *vxlan)
{
	bool metadata = vxlan->cfg.flags & VXLAN_F_COLLECT_METADATA;
	bool ipv6 = vxlan->cfg.flags & VXLAN_F_IPV6 || metadata;
	bool ipv4 = !ipv6 || metadata;
	int ret = 0;

	RCU_INIT_POINTER(vxlan->vn4_sock, NULL);
#if IS_ENABLED(CONFIG_IPV6)
	RCU_INIT_POINTER(vxlan->vn6_sock, NULL);
	if (ipv6) {
		ret = __vxlan_sock_add(vxlan, true);
		if (ret < 0 && ret != -EAFNOSUPPORT)
			ipv4 = false;
	}
#endif
	if (ipv4)
		ret = __vxlan_sock_add(vxlan, false);
	if (ret < 0)
		vxlan_sock_release(vxlan);
	return ret;
}

int vxlan_vni_in_use(struct net *src_net, struct vxlan_dev *vxlan,
		     struct vxlan_config *conf, __be32 vni)
{
	struct vxlan_net *vn = net_generic(src_net, vxlan_net_id);
	struct vxlan_dev *tmp;

	list_for_each_entry(tmp, &vn->vxlan_list, next) {
		if (tmp == vxlan)
			continue;
		if (tmp->cfg.flags & VXLAN_F_VNIFILTER) {
			if (!vxlan_vnifilter_lookup(tmp, vni))
				continue;
		} else if (tmp->cfg.vni != vni) {
			continue;
		}
		if (tmp->cfg.dst_port != conf->dst_port)
			continue;
		if ((tmp->cfg.flags & (VXLAN_F_RCV_FLAGS | VXLAN_F_IPV6)) !=
		    (conf->flags & (VXLAN_F_RCV_FLAGS | VXLAN_F_IPV6)))
			continue;

		if ((conf->flags & VXLAN_F_IPV6_LINKLOCAL) &&
		    tmp->cfg.remote_ifindex != conf->remote_ifindex)
			continue;

		return -EEXIST;
	}

	return 0;
}

static int vxlan_config_validate(struct net *src_net, struct vxlan_config *conf,
				 struct net_device **lower,
				 struct vxlan_dev *old,
				 struct netlink_ext_ack *extack)
{
	bool use_ipv6 = false;

	if (conf->flags & VXLAN_F_GPE) {
		/* For now, allow GPE only together with
		 * COLLECT_METADATA. This can be relaxed later; in such
		 * case, the other side of the PtP link will have to be
		 * provided.
		 */
		if ((conf->flags & ~VXLAN_F_ALLOWED_GPE) ||
		    !(conf->flags & VXLAN_F_COLLECT_METADATA)) {
			NL_SET_ERR_MSG(extack,
				       "VXLAN GPE does not support this combination of attributes");
			return -EINVAL;
		}
	}

	if (!conf->remote_ip.sa.sa_family && !conf->saddr.sa.sa_family) {
		/* Unless IPv6 is explicitly requested, assume IPv4 */
		conf->remote_ip.sa.sa_family = AF_INET;
		conf->saddr.sa.sa_family = AF_INET;
	} else if (!conf->remote_ip.sa.sa_family) {
		conf->remote_ip.sa.sa_family = conf->saddr.sa.sa_family;
	} else if (!conf->saddr.sa.sa_family) {
		conf->saddr.sa.sa_family = conf->remote_ip.sa.sa_family;
	}

	if (conf->saddr.sa.sa_family != conf->remote_ip.sa.sa_family) {
		NL_SET_ERR_MSG(extack,
			       "Local and remote address must be from the same family");
		return -EINVAL;
	}

	if (vxlan_addr_multicast(&conf->saddr)) {
		NL_SET_ERR_MSG(extack, "Local address cannot be multicast");
		return -EINVAL;
	}

	if (conf->saddr.sa.sa_family == AF_INET6) {
		if (!IS_ENABLED(CONFIG_IPV6)) {
			NL_SET_ERR_MSG(extack,
				       "IPv6 support not enabled in the kernel");
			return -EPFNOSUPPORT;
		}
		use_ipv6 = true;
		conf->flags |= VXLAN_F_IPV6;

		if (!(conf->flags & VXLAN_F_COLLECT_METADATA)) {
			int local_type =
				ipv6_addr_type(&conf->saddr.sin6.sin6_addr);
			int remote_type =
				ipv6_addr_type(&conf->remote_ip.sin6.sin6_addr);

			if (local_type & IPV6_ADDR_LINKLOCAL) {
				if (!(remote_type & IPV6_ADDR_LINKLOCAL) &&
				    (remote_type != IPV6_ADDR_ANY)) {
					NL_SET_ERR_MSG(extack,
						       "Invalid combination of local and remote address scopes");
					return -EINVAL;
				}

				conf->flags |= VXLAN_F_IPV6_LINKLOCAL;
			} else {
				if (remote_type ==
				    (IPV6_ADDR_UNICAST | IPV6_ADDR_LINKLOCAL)) {
					NL_SET_ERR_MSG(extack,
						       "Invalid combination of local and remote address scopes");
					return -EINVAL;
				}

				conf->flags &= ~VXLAN_F_IPV6_LINKLOCAL;
			}
		}
	}

	if (conf->label && !use_ipv6) {
		NL_SET_ERR_MSG(extack,
			       "Label attribute only applies to IPv6 VXLAN devices");
		return -EINVAL;
	}

	if (conf->label_policy && !use_ipv6) {
		NL_SET_ERR_MSG(extack,
			       "Label policy only applies to IPv6 VXLAN devices");
		return -EINVAL;
	}

	if (conf->remote_ifindex) {
		struct net_device *lowerdev;

		lowerdev = __dev_get_by_index(src_net, conf->remote_ifindex);
		if (!lowerdev) {
			NL_SET_ERR_MSG(extack,
				       "Invalid local interface, device not found");
			return -ENODEV;
		}

#if IS_ENABLED(CONFIG_IPV6)
		if (use_ipv6) {
			struct inet6_dev *idev = __in6_dev_get(lowerdev);

			if (idev && idev->cnf.disable_ipv6) {
				NL_SET_ERR_MSG(extack,
					       "IPv6 support disabled by administrator");
				return -EPERM;
			}
		}
#endif

		*lower = lowerdev;
	} else {
		if (vxlan_addr_multicast(&conf->remote_ip)) {
			NL_SET_ERR_MSG(extack,
				       "Local interface required for multicast remote destination");

			return -EINVAL;
		}

#if IS_ENABLED(CONFIG_IPV6)
		if (conf->flags & VXLAN_F_IPV6_LINKLOCAL) {
			NL_SET_ERR_MSG(extack,
				       "Local interface required for link-local local/remote addresses");
			return -EINVAL;
		}
#endif

		*lower = NULL;
	}

	if (!conf->dst_port) {
		if (conf->flags & VXLAN_F_GPE)
			conf->dst_port = htons(IANA_VXLAN_GPE_UDP_PORT);
		else
			conf->dst_port = htons(vxlan_port);
	}

	if (!conf->age_interval)
		conf->age_interval = FDB_AGE_DEFAULT;

	if (vxlan_vni_in_use(src_net, old, conf, conf->vni)) {
		NL_SET_ERR_MSG(extack,
			       "A VXLAN device with the specified VNI already exists");
		return -EEXIST;
	}

	return 0;
}

static void vxlan_config_apply(struct net_device *dev,
			       struct vxlan_config *conf,
			       struct net_device *lowerdev,
			       struct net *src_net,
			       bool changelink)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct vxlan_rdst *dst = &vxlan->default_dst;
	unsigned short needed_headroom = ETH_HLEN;
	int max_mtu = ETH_MAX_MTU;
	u32 flags = conf->flags;

	if (!changelink) {
		if (flags & VXLAN_F_GPE)
			vxlan_raw_setup(dev);
		else
			vxlan_ether_setup(dev);

		if (conf->mtu)
			dev->mtu = conf->mtu;

		vxlan->net = src_net;
	}

	dst->remote_vni = conf->vni;

	memcpy(&dst->remote_ip, &conf->remote_ip, sizeof(conf->remote_ip));

	if (lowerdev) {
		dst->remote_ifindex = conf->remote_ifindex;

		netif_inherit_tso_max(dev, lowerdev);

		needed_headroom = lowerdev->hard_header_len;
		needed_headroom += lowerdev->needed_headroom;

		dev->needed_tailroom = lowerdev->needed_tailroom;

		max_mtu = lowerdev->mtu - vxlan_headroom(flags);
		if (max_mtu < ETH_MIN_MTU)
			max_mtu = ETH_MIN_MTU;

		if (!changelink && !conf->mtu)
			dev->mtu = max_mtu;
	}

	if (dev->mtu > max_mtu)
		dev->mtu = max_mtu;

	if (flags & VXLAN_F_COLLECT_METADATA)
		flags |= VXLAN_F_IPV6;
	needed_headroom += vxlan_headroom(flags);
	dev->needed_headroom = needed_headroom;

	memcpy(&vxlan->cfg, conf, sizeof(*conf));
}

static int vxlan_dev_configure(struct net *src_net, struct net_device *dev,
			       struct vxlan_config *conf, bool changelink,
			       struct netlink_ext_ack *extack)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct net_device *lowerdev;
	int ret;

	ret = vxlan_config_validate(src_net, conf, &lowerdev, vxlan, extack);
	if (ret)
		return ret;

	vxlan_config_apply(dev, conf, lowerdev, src_net, changelink);

	return 0;
}

static int __vxlan_dev_create(struct net *net, struct net_device *dev,
			      struct vxlan_config *conf,
			      struct netlink_ext_ack *extack)
{
	struct vxlan_net *vn = net_generic(net, vxlan_net_id);
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct net_device *remote_dev = NULL;
	struct vxlan_fdb *f = NULL;
	bool unregister = false;
	struct vxlan_rdst *dst;
	int err;

	dst = &vxlan->default_dst;
	err = vxlan_dev_configure(net, dev, conf, false, extack);
	if (err)
		return err;

	dev->ethtool_ops = &vxlan_ethtool_ops;

	/* create an fdb entry for a valid default destination */
	if (!vxlan_addr_any(&dst->remote_ip)) {
		err = vxlan_fdb_create(vxlan, all_zeros_mac,
				       &dst->remote_ip,
				       NUD_REACHABLE | NUD_PERMANENT,
				       vxlan->cfg.dst_port,
				       dst->remote_vni,
				       dst->remote_vni,
				       dst->remote_ifindex,
				       NTF_SELF, 0, &f, extack);
		if (err)
			return err;
	}

	err = register_netdevice(dev);
	if (err)
		goto errout;
	unregister = true;

	if (dst->remote_ifindex) {
		remote_dev = __dev_get_by_index(net, dst->remote_ifindex);
		if (!remote_dev) {
			err = -ENODEV;
			goto errout;
		}

		err = netdev_upper_dev_link(remote_dev, dev, extack);
		if (err)
			goto errout;
	}

	err = rtnl_configure_link(dev, NULL, 0, NULL);
	if (err < 0)
		goto unlink;

	if (f) {
		vxlan_fdb_insert(vxlan, all_zeros_mac, dst->remote_vni, f);

		/* notify default fdb entry */
		err = vxlan_fdb_notify(vxlan, f, first_remote_rtnl(f),
				       RTM_NEWNEIGH, true, extack);
		if (err) {
			vxlan_fdb_destroy(vxlan, f, false, false);
			if (remote_dev)
				netdev_upper_dev_unlink(remote_dev, dev);
			goto unregister;
		}
	}

	list_add(&vxlan->next, &vn->vxlan_list);
	if (remote_dev)
		dst->remote_dev = remote_dev;
	return 0;
unlink:
	if (remote_dev)
		netdev_upper_dev_unlink(remote_dev, dev);
errout:
	/* unregister_netdevice() destroys the default FDB entry with deletion
	 * notification. But the addition notification was not sent yet, so
	 * destroy the entry by hand here.
	 */
	if (f)
		__vxlan_fdb_free(f);
unregister:
	if (unregister)
		unregister_netdevice(dev);
	return err;
}

/* Set/clear flags based on attribute */
static int vxlan_nl2flag(struct vxlan_config *conf, struct nlattr *tb[],
			  int attrtype, unsigned long mask, bool changelink,
			  bool changelink_supported,
			  struct netlink_ext_ack *extack)
{
	unsigned long flags;

	if (!tb[attrtype])
		return 0;

	if (changelink && !changelink_supported) {
		vxlan_flag_attr_error(attrtype, extack);
		return -EOPNOTSUPP;
	}

	if (vxlan_policy[attrtype].type == NLA_FLAG)
		flags = conf->flags | mask;
	else if (nla_get_u8(tb[attrtype]))
		flags = conf->flags | mask;
	else
		flags = conf->flags & ~mask;

	conf->flags = flags;

	return 0;
}

static int vxlan_nl2conf(struct nlattr *tb[], struct nlattr *data[],
			 struct net_device *dev, struct vxlan_config *conf,
			 bool changelink, struct netlink_ext_ack *extack)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	int err = 0;

	memset(conf, 0, sizeof(*conf));

	/* if changelink operation, start with old existing cfg */
	if (changelink)
		memcpy(conf, &vxlan->cfg, sizeof(*conf));

	if (data[IFLA_VXLAN_ID]) {
		__be32 vni = cpu_to_be32(nla_get_u32(data[IFLA_VXLAN_ID]));

		if (changelink && (vni != conf->vni)) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_VXLAN_ID], "Cannot change VNI");
			return -EOPNOTSUPP;
		}
		conf->vni = cpu_to_be32(nla_get_u32(data[IFLA_VXLAN_ID]));
	}

	if (data[IFLA_VXLAN_GROUP]) {
		if (changelink && (conf->remote_ip.sa.sa_family != AF_INET)) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_VXLAN_GROUP], "New group address family does not match old group");
			return -EOPNOTSUPP;
		}

		conf->remote_ip.sin.sin_addr.s_addr = nla_get_in_addr(data[IFLA_VXLAN_GROUP]);
		conf->remote_ip.sa.sa_family = AF_INET;
	} else if (data[IFLA_VXLAN_GROUP6]) {
		if (!IS_ENABLED(CONFIG_IPV6)) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_VXLAN_GROUP6], "IPv6 support not enabled in the kernel");
			return -EPFNOSUPPORT;
		}

		if (changelink && (conf->remote_ip.sa.sa_family != AF_INET6)) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_VXLAN_GROUP6], "New group address family does not match old group");
			return -EOPNOTSUPP;
		}

		conf->remote_ip.sin6.sin6_addr = nla_get_in6_addr(data[IFLA_VXLAN_GROUP6]);
		conf->remote_ip.sa.sa_family = AF_INET6;
	}

	if (data[IFLA_VXLAN_LOCAL]) {
		if (changelink && (conf->saddr.sa.sa_family != AF_INET)) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_VXLAN_LOCAL], "New local address family does not match old");
			return -EOPNOTSUPP;
		}

		conf->saddr.sin.sin_addr.s_addr = nla_get_in_addr(data[IFLA_VXLAN_LOCAL]);
		conf->saddr.sa.sa_family = AF_INET;
	} else if (data[IFLA_VXLAN_LOCAL6]) {
		if (!IS_ENABLED(CONFIG_IPV6)) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_VXLAN_LOCAL6], "IPv6 support not enabled in the kernel");
			return -EPFNOSUPPORT;
		}

		if (changelink && (conf->saddr.sa.sa_family != AF_INET6)) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_VXLAN_LOCAL6], "New local address family does not match old");
			return -EOPNOTSUPP;
		}

		/* TODO: respect scope id */
		conf->saddr.sin6.sin6_addr = nla_get_in6_addr(data[IFLA_VXLAN_LOCAL6]);
		conf->saddr.sa.sa_family = AF_INET6;
	}

	if (data[IFLA_VXLAN_LINK])
		conf->remote_ifindex = nla_get_u32(data[IFLA_VXLAN_LINK]);

	if (data[IFLA_VXLAN_TOS])
		conf->tos  = nla_get_u8(data[IFLA_VXLAN_TOS]);

	if (data[IFLA_VXLAN_TTL])
		conf->ttl = nla_get_u8(data[IFLA_VXLAN_TTL]);

	if (data[IFLA_VXLAN_TTL_INHERIT]) {
		err = vxlan_nl2flag(conf, data, IFLA_VXLAN_TTL_INHERIT,
				    VXLAN_F_TTL_INHERIT, changelink, false,
				    extack);
		if (err)
			return err;

	}

	if (data[IFLA_VXLAN_LABEL])
		conf->label = nla_get_be32(data[IFLA_VXLAN_LABEL]) &
			     IPV6_FLOWLABEL_MASK;
	if (data[IFLA_VXLAN_LABEL_POLICY])
		conf->label_policy = nla_get_u32(data[IFLA_VXLAN_LABEL_POLICY]);

	if (data[IFLA_VXLAN_LEARNING]) {
		err = vxlan_nl2flag(conf, data, IFLA_VXLAN_LEARNING,
				    VXLAN_F_LEARN, changelink, true,
				    extack);
		if (err)
			return err;
	} else if (!changelink) {
		/* default to learn on a new device */
		conf->flags |= VXLAN_F_LEARN;
	}

	if (data[IFLA_VXLAN_AGEING])
		conf->age_interval = nla_get_u32(data[IFLA_VXLAN_AGEING]);

	if (data[IFLA_VXLAN_PROXY]) {
		err = vxlan_nl2flag(conf, data, IFLA_VXLAN_PROXY,
				    VXLAN_F_PROXY, changelink, false,
				    extack);
		if (err)
			return err;
	}

	if (data[IFLA_VXLAN_RSC]) {
		err = vxlan_nl2flag(conf, data, IFLA_VXLAN_RSC,
				    VXLAN_F_RSC, changelink, false,
				    extack);
		if (err)
			return err;
	}

	if (data[IFLA_VXLAN_L2MISS]) {
		err = vxlan_nl2flag(conf, data, IFLA_VXLAN_L2MISS,
				    VXLAN_F_L2MISS, changelink, false,
				    extack);
		if (err)
			return err;
	}

	if (data[IFLA_VXLAN_L3MISS]) {
		err = vxlan_nl2flag(conf, data, IFLA_VXLAN_L3MISS,
				    VXLAN_F_L3MISS, changelink, false,
				    extack);
		if (err)
			return err;
	}

	if (data[IFLA_VXLAN_LIMIT]) {
		if (changelink) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_VXLAN_LIMIT],
					    "Cannot change limit");
			return -EOPNOTSUPP;
		}
		conf->addrmax = nla_get_u32(data[IFLA_VXLAN_LIMIT]);
	}

	if (data[IFLA_VXLAN_COLLECT_METADATA]) {
		err = vxlan_nl2flag(conf, data, IFLA_VXLAN_COLLECT_METADATA,
				    VXLAN_F_COLLECT_METADATA, changelink, false,
				    extack);
		if (err)
			return err;
	}

	if (data[IFLA_VXLAN_PORT_RANGE]) {
		if (!changelink) {
			const struct ifla_vxlan_port_range *p
				= nla_data(data[IFLA_VXLAN_PORT_RANGE]);
			conf->port_min = ntohs(p->low);
			conf->port_max = ntohs(p->high);
		} else {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_VXLAN_PORT_RANGE],
					    "Cannot change port range");
			return -EOPNOTSUPP;
		}
	}

	if (data[IFLA_VXLAN_PORT]) {
		if (changelink) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_VXLAN_PORT],
					    "Cannot change port");
			return -EOPNOTSUPP;
		}
		conf->dst_port = nla_get_be16(data[IFLA_VXLAN_PORT]);
	}

	if (data[IFLA_VXLAN_UDP_CSUM]) {
		if (changelink) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_VXLAN_UDP_CSUM],
					    "Cannot change UDP_CSUM flag");
			return -EOPNOTSUPP;
		}
		if (!nla_get_u8(data[IFLA_VXLAN_UDP_CSUM]))
			conf->flags |= VXLAN_F_UDP_ZERO_CSUM_TX;
	}

	if (data[IFLA_VXLAN_LOCALBYPASS]) {
		err = vxlan_nl2flag(conf, data, IFLA_VXLAN_LOCALBYPASS,
				    VXLAN_F_LOCALBYPASS, changelink,
				    true, extack);
		if (err)
			return err;
	} else if (!changelink) {
		/* default to local bypass on a new device */
		conf->flags |= VXLAN_F_LOCALBYPASS;
	}

	if (data[IFLA_VXLAN_UDP_ZERO_CSUM6_TX]) {
		err = vxlan_nl2flag(conf, data, IFLA_VXLAN_UDP_ZERO_CSUM6_TX,
				    VXLAN_F_UDP_ZERO_CSUM6_TX, changelink,
				    false, extack);
		if (err)
			return err;
	}

	if (data[IFLA_VXLAN_UDP_ZERO_CSUM6_RX]) {
		err = vxlan_nl2flag(conf, data, IFLA_VXLAN_UDP_ZERO_CSUM6_RX,
				    VXLAN_F_UDP_ZERO_CSUM6_RX, changelink,
				    false, extack);
		if (err)
			return err;
	}

	if (data[IFLA_VXLAN_REMCSUM_TX]) {
		err = vxlan_nl2flag(conf, data, IFLA_VXLAN_REMCSUM_TX,
				    VXLAN_F_REMCSUM_TX, changelink, false,
				    extack);
		if (err)
			return err;
	}

	if (data[IFLA_VXLAN_REMCSUM_RX]) {
		err = vxlan_nl2flag(conf, data, IFLA_VXLAN_REMCSUM_RX,
				    VXLAN_F_REMCSUM_RX, changelink, false,
				    extack);
		if (err)
			return err;
	}

	if (data[IFLA_VXLAN_GBP]) {
		err = vxlan_nl2flag(conf, data, IFLA_VXLAN_GBP,
				    VXLAN_F_GBP, changelink, false, extack);
		if (err)
			return err;
	}

	if (data[IFLA_VXLAN_GPE]) {
		err = vxlan_nl2flag(conf, data, IFLA_VXLAN_GPE,
				    VXLAN_F_GPE, changelink, false,
				    extack);
		if (err)
			return err;
	}

	if (data[IFLA_VXLAN_REMCSUM_NOPARTIAL]) {
		err = vxlan_nl2flag(conf, data, IFLA_VXLAN_REMCSUM_NOPARTIAL,
				    VXLAN_F_REMCSUM_NOPARTIAL, changelink,
				    false, extack);
		if (err)
			return err;
	}

	if (tb[IFLA_MTU]) {
		if (changelink) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_MTU],
					    "Cannot change mtu");
			return -EOPNOTSUPP;
		}
		conf->mtu = nla_get_u32(tb[IFLA_MTU]);
	}

	if (data[IFLA_VXLAN_DF])
		conf->df = nla_get_u8(data[IFLA_VXLAN_DF]);

	if (data[IFLA_VXLAN_VNIFILTER]) {
		err = vxlan_nl2flag(conf, data, IFLA_VXLAN_VNIFILTER,
				    VXLAN_F_VNIFILTER, changelink, false,
				    extack);
		if (err)
			return err;

		if ((conf->flags & VXLAN_F_VNIFILTER) &&
		    !(conf->flags & VXLAN_F_COLLECT_METADATA)) {
			NL_SET_ERR_MSG_ATTR(extack, data[IFLA_VXLAN_VNIFILTER],
					    "vxlan vnifilter only valid in collect metadata mode");
			return -EINVAL;
		}
	}

	return 0;
}

static int vxlan_newlink(struct net *src_net, struct net_device *dev,
			 struct nlattr *tb[], struct nlattr *data[],
			 struct netlink_ext_ack *extack)
{
	struct vxlan_config conf;
	int err;

	err = vxlan_nl2conf(tb, data, dev, &conf, false, extack);
	if (err)
		return err;

	return __vxlan_dev_create(src_net, dev, &conf, extack);
}

static int vxlan_changelink(struct net_device *dev, struct nlattr *tb[],
			    struct nlattr *data[],
			    struct netlink_ext_ack *extack)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct net_device *lowerdev;
	struct vxlan_config conf;
	struct vxlan_rdst *dst;
	int err;

	dst = &vxlan->default_dst;
	err = vxlan_nl2conf(tb, data, dev, &conf, true, extack);
	if (err)
		return err;

	err = vxlan_config_validate(vxlan->net, &conf, &lowerdev,
				    vxlan, extack);
	if (err)
		return err;

	if (dst->remote_dev == lowerdev)
		lowerdev = NULL;

	err = netdev_adjacent_change_prepare(dst->remote_dev, lowerdev, dev,
					     extack);
	if (err)
		return err;

	/* handle default dst entry */
	if (!vxlan_addr_equal(&conf.remote_ip, &dst->remote_ip)) {
		u32 hash_index = fdb_head_index(vxlan, all_zeros_mac, conf.vni);

		spin_lock_bh(&vxlan->hash_lock[hash_index]);
		if (!vxlan_addr_any(&conf.remote_ip)) {
			err = vxlan_fdb_update(vxlan, all_zeros_mac,
					       &conf.remote_ip,
					       NUD_REACHABLE | NUD_PERMANENT,
					       NLM_F_APPEND | NLM_F_CREATE,
					       vxlan->cfg.dst_port,
					       conf.vni, conf.vni,
					       conf.remote_ifindex,
					       NTF_SELF, 0, true, extack);
			if (err) {
				spin_unlock_bh(&vxlan->hash_lock[hash_index]);
				netdev_adjacent_change_abort(dst->remote_dev,
							     lowerdev, dev);
				return err;
			}
		}
		if (!vxlan_addr_any(&dst->remote_ip))
			__vxlan_fdb_delete(vxlan, all_zeros_mac,
					   dst->remote_ip,
					   vxlan->cfg.dst_port,
					   dst->remote_vni,
					   dst->remote_vni,
					   dst->remote_ifindex,
					   true);
		spin_unlock_bh(&vxlan->hash_lock[hash_index]);

		/* If vni filtering device, also update fdb entries of
		 * all vnis that were using default remote ip
		 */
		if (vxlan->cfg.flags & VXLAN_F_VNIFILTER) {
			err = vxlan_vnilist_update_group(vxlan, &dst->remote_ip,
							 &conf.remote_ip, extack);
			if (err) {
				netdev_adjacent_change_abort(dst->remote_dev,
							     lowerdev, dev);
				return err;
			}
		}
	}

	if (conf.age_interval != vxlan->cfg.age_interval)
		mod_timer(&vxlan->age_timer, jiffies);

	netdev_adjacent_change_commit(dst->remote_dev, lowerdev, dev);
	if (lowerdev && lowerdev != dst->remote_dev)
		dst->remote_dev = lowerdev;
	vxlan_config_apply(dev, &conf, lowerdev, vxlan->net, true);
	return 0;
}

static void vxlan_dellink(struct net_device *dev, struct list_head *head)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct vxlan_fdb_flush_desc desc = {
		/* Default entry is deleted at vxlan_uninit. */
		.ignore_default_entry = true,
	};

	vxlan_flush(vxlan, &desc);

	list_del(&vxlan->next);
	unregister_netdevice_queue(dev, head);
	if (vxlan->default_dst.remote_dev)
		netdev_upper_dev_unlink(vxlan->default_dst.remote_dev, dev);
}

static size_t vxlan_get_size(const struct net_device *dev)
{
	return nla_total_size(sizeof(__u32)) +	/* IFLA_VXLAN_ID */
		nla_total_size(sizeof(struct in6_addr)) + /* IFLA_VXLAN_GROUP{6} */
		nla_total_size(sizeof(__u32)) +	/* IFLA_VXLAN_LINK */
		nla_total_size(sizeof(struct in6_addr)) + /* IFLA_VXLAN_LOCAL{6} */
		nla_total_size(sizeof(__u8)) +	/* IFLA_VXLAN_TTL */
		nla_total_size(sizeof(__u8)) +	/* IFLA_VXLAN_TTL_INHERIT */
		nla_total_size(sizeof(__u8)) +	/* IFLA_VXLAN_TOS */
		nla_total_size(sizeof(__u8)) +	/* IFLA_VXLAN_DF */
		nla_total_size(sizeof(__be32)) + /* IFLA_VXLAN_LABEL */
		nla_total_size(sizeof(__u32)) +  /* IFLA_VXLAN_LABEL_POLICY */
		nla_total_size(sizeof(__u8)) +	/* IFLA_VXLAN_LEARNING */
		nla_total_size(sizeof(__u8)) +	/* IFLA_VXLAN_PROXY */
		nla_total_size(sizeof(__u8)) +	/* IFLA_VXLAN_RSC */
		nla_total_size(sizeof(__u8)) +	/* IFLA_VXLAN_L2MISS */
		nla_total_size(sizeof(__u8)) +	/* IFLA_VXLAN_L3MISS */
		nla_total_size(sizeof(__u8)) +	/* IFLA_VXLAN_COLLECT_METADATA */
		nla_total_size(sizeof(__u32)) +	/* IFLA_VXLAN_AGEING */
		nla_total_size(sizeof(__u32)) +	/* IFLA_VXLAN_LIMIT */
		nla_total_size(sizeof(__be16)) + /* IFLA_VXLAN_PORT */
		nla_total_size(sizeof(__u8)) + /* IFLA_VXLAN_UDP_CSUM */
		nla_total_size(sizeof(__u8)) + /* IFLA_VXLAN_UDP_ZERO_CSUM6_TX */
		nla_total_size(sizeof(__u8)) + /* IFLA_VXLAN_UDP_ZERO_CSUM6_RX */
		nla_total_size(sizeof(__u8)) + /* IFLA_VXLAN_REMCSUM_TX */
		nla_total_size(sizeof(__u8)) + /* IFLA_VXLAN_REMCSUM_RX */
		nla_total_size(sizeof(__u8)) + /* IFLA_VXLAN_LOCALBYPASS */
		/* IFLA_VXLAN_PORT_RANGE */
		nla_total_size(sizeof(struct ifla_vxlan_port_range)) +
		nla_total_size(0) + /* IFLA_VXLAN_GBP */
		nla_total_size(0) + /* IFLA_VXLAN_GPE */
		nla_total_size(0) + /* IFLA_VXLAN_REMCSUM_NOPARTIAL */
		nla_total_size(sizeof(__u8)) + /* IFLA_VXLAN_VNIFILTER */
		0;
}

static int vxlan_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	const struct vxlan_dev *vxlan = netdev_priv(dev);
	const struct vxlan_rdst *dst = &vxlan->default_dst;
	struct ifla_vxlan_port_range ports = {
		.low =  htons(vxlan->cfg.port_min),
		.high = htons(vxlan->cfg.port_max),
	};

	if (nla_put_u32(skb, IFLA_VXLAN_ID, be32_to_cpu(dst->remote_vni)))
		goto nla_put_failure;

	if (!vxlan_addr_any(&dst->remote_ip)) {
		if (dst->remote_ip.sa.sa_family == AF_INET) {
			if (nla_put_in_addr(skb, IFLA_VXLAN_GROUP,
					    dst->remote_ip.sin.sin_addr.s_addr))
				goto nla_put_failure;
#if IS_ENABLED(CONFIG_IPV6)
		} else {
			if (nla_put_in6_addr(skb, IFLA_VXLAN_GROUP6,
					     &dst->remote_ip.sin6.sin6_addr))
				goto nla_put_failure;
#endif
		}
	}

	if (dst->remote_ifindex && nla_put_u32(skb, IFLA_VXLAN_LINK, dst->remote_ifindex))
		goto nla_put_failure;

	if (!vxlan_addr_any(&vxlan->cfg.saddr)) {
		if (vxlan->cfg.saddr.sa.sa_family == AF_INET) {
			if (nla_put_in_addr(skb, IFLA_VXLAN_LOCAL,
					    vxlan->cfg.saddr.sin.sin_addr.s_addr))
				goto nla_put_failure;
#if IS_ENABLED(CONFIG_IPV6)
		} else {
			if (nla_put_in6_addr(skb, IFLA_VXLAN_LOCAL6,
					     &vxlan->cfg.saddr.sin6.sin6_addr))
				goto nla_put_failure;
#endif
		}
	}

	if (nla_put_u8(skb, IFLA_VXLAN_TTL, vxlan->cfg.ttl) ||
	    nla_put_u8(skb, IFLA_VXLAN_TTL_INHERIT,
		       !!(vxlan->cfg.flags & VXLAN_F_TTL_INHERIT)) ||
	    nla_put_u8(skb, IFLA_VXLAN_TOS, vxlan->cfg.tos) ||
	    nla_put_u8(skb, IFLA_VXLAN_DF, vxlan->cfg.df) ||
	    nla_put_be32(skb, IFLA_VXLAN_LABEL, vxlan->cfg.label) ||
	    nla_put_u32(skb, IFLA_VXLAN_LABEL_POLICY, vxlan->cfg.label_policy) ||
	    nla_put_u8(skb, IFLA_VXLAN_LEARNING,
		       !!(vxlan->cfg.flags & VXLAN_F_LEARN)) ||
	    nla_put_u8(skb, IFLA_VXLAN_PROXY,
		       !!(vxlan->cfg.flags & VXLAN_F_PROXY)) ||
	    nla_put_u8(skb, IFLA_VXLAN_RSC,
		       !!(vxlan->cfg.flags & VXLAN_F_RSC)) ||
	    nla_put_u8(skb, IFLA_VXLAN_L2MISS,
		       !!(vxlan->cfg.flags & VXLAN_F_L2MISS)) ||
	    nla_put_u8(skb, IFLA_VXLAN_L3MISS,
		       !!(vxlan->cfg.flags & VXLAN_F_L3MISS)) ||
	    nla_put_u8(skb, IFLA_VXLAN_COLLECT_METADATA,
		       !!(vxlan->cfg.flags & VXLAN_F_COLLECT_METADATA)) ||
	    nla_put_u32(skb, IFLA_VXLAN_AGEING, vxlan->cfg.age_interval) ||
	    nla_put_u32(skb, IFLA_VXLAN_LIMIT, vxlan->cfg.addrmax) ||
	    nla_put_be16(skb, IFLA_VXLAN_PORT, vxlan->cfg.dst_port) ||
	    nla_put_u8(skb, IFLA_VXLAN_UDP_CSUM,
		       !(vxlan->cfg.flags & VXLAN_F_UDP_ZERO_CSUM_TX)) ||
	    nla_put_u8(skb, IFLA_VXLAN_UDP_ZERO_CSUM6_TX,
		       !!(vxlan->cfg.flags & VXLAN_F_UDP_ZERO_CSUM6_TX)) ||
	    nla_put_u8(skb, IFLA_VXLAN_UDP_ZERO_CSUM6_RX,
		       !!(vxlan->cfg.flags & VXLAN_F_UDP_ZERO_CSUM6_RX)) ||
	    nla_put_u8(skb, IFLA_VXLAN_REMCSUM_TX,
		       !!(vxlan->cfg.flags & VXLAN_F_REMCSUM_TX)) ||
	    nla_put_u8(skb, IFLA_VXLAN_REMCSUM_RX,
		       !!(vxlan->cfg.flags & VXLAN_F_REMCSUM_RX)) ||
	    nla_put_u8(skb, IFLA_VXLAN_LOCALBYPASS,
		       !!(vxlan->cfg.flags & VXLAN_F_LOCALBYPASS)))
		goto nla_put_failure;

	if (nla_put(skb, IFLA_VXLAN_PORT_RANGE, sizeof(ports), &ports))
		goto nla_put_failure;

	if (vxlan->cfg.flags & VXLAN_F_GBP &&
	    nla_put_flag(skb, IFLA_VXLAN_GBP))
		goto nla_put_failure;

	if (vxlan->cfg.flags & VXLAN_F_GPE &&
	    nla_put_flag(skb, IFLA_VXLAN_GPE))
		goto nla_put_failure;

	if (vxlan->cfg.flags & VXLAN_F_REMCSUM_NOPARTIAL &&
	    nla_put_flag(skb, IFLA_VXLAN_REMCSUM_NOPARTIAL))
		goto nla_put_failure;

	if (vxlan->cfg.flags & VXLAN_F_VNIFILTER &&
	    nla_put_u8(skb, IFLA_VXLAN_VNIFILTER,
		       !!(vxlan->cfg.flags & VXLAN_F_VNIFILTER)))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static struct net *vxlan_get_link_net(const struct net_device *dev)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);

	return READ_ONCE(vxlan->net);
}

static struct rtnl_link_ops vxlan_link_ops __read_mostly = {
	.kind		= "vxlan",
	.maxtype	= IFLA_VXLAN_MAX,
	.policy		= vxlan_policy,
	.priv_size	= sizeof(struct vxlan_dev),
	.setup		= vxlan_setup,
	.validate	= vxlan_validate,
	.newlink	= vxlan_newlink,
	.changelink	= vxlan_changelink,
	.dellink	= vxlan_dellink,
	.get_size	= vxlan_get_size,
	.fill_info	= vxlan_fill_info,
	.get_link_net	= vxlan_get_link_net,
};

struct net_device *vxlan_dev_create(struct net *net, const char *name,
				    u8 name_assign_type,
				    struct vxlan_config *conf)
{
	struct nlattr *tb[IFLA_MAX + 1];
	struct net_device *dev;
	int err;

	memset(&tb, 0, sizeof(tb));

	dev = rtnl_create_link(net, name, name_assign_type,
			       &vxlan_link_ops, tb, NULL);
	if (IS_ERR(dev))
		return dev;

	err = __vxlan_dev_create(net, dev, conf, NULL);
	if (err < 0) {
		free_netdev(dev);
		return ERR_PTR(err);
	}

	err = rtnl_configure_link(dev, NULL, 0, NULL);
	if (err < 0) {
		LIST_HEAD(list_kill);

		vxlan_dellink(dev, &list_kill);
		unregister_netdevice_many(&list_kill);
		return ERR_PTR(err);
	}

	return dev;
}
EXPORT_SYMBOL_GPL(vxlan_dev_create);

static void vxlan_handle_lowerdev_unregister(struct vxlan_net *vn,
					     struct net_device *dev)
{
	struct vxlan_dev *vxlan, *next;
	LIST_HEAD(list_kill);

	list_for_each_entry_safe(vxlan, next, &vn->vxlan_list, next) {
		struct vxlan_rdst *dst = &vxlan->default_dst;

		/* In case we created vxlan device with carrier
		 * and we loose the carrier due to module unload
		 * we also need to remove vxlan device. In other
		 * cases, it's not necessary and remote_ifindex
		 * is 0 here, so no matches.
		 */
		if (dst->remote_ifindex == dev->ifindex)
			vxlan_dellink(vxlan->dev, &list_kill);
	}

	unregister_netdevice_many(&list_kill);
}

static int vxlan_netdevice_event(struct notifier_block *unused,
				 unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct vxlan_net *vn = net_generic(dev_net(dev), vxlan_net_id);

	if (event == NETDEV_UNREGISTER)
		vxlan_handle_lowerdev_unregister(vn, dev);
	else if (event == NETDEV_UDP_TUNNEL_PUSH_INFO)
		vxlan_offload_rx_ports(dev, true);
	else if (event == NETDEV_UDP_TUNNEL_DROP_INFO)
		vxlan_offload_rx_ports(dev, false);

	return NOTIFY_DONE;
}

static struct notifier_block vxlan_notifier_block __read_mostly = {
	.notifier_call = vxlan_netdevice_event,
};

static void
vxlan_fdb_offloaded_set(struct net_device *dev,
			struct switchdev_notifier_vxlan_fdb_info *fdb_info)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct vxlan_rdst *rdst;
	struct vxlan_fdb *f;
	u32 hash_index;

	hash_index = fdb_head_index(vxlan, fdb_info->eth_addr, fdb_info->vni);

	spin_lock_bh(&vxlan->hash_lock[hash_index]);

	f = vxlan_find_mac(vxlan, fdb_info->eth_addr, fdb_info->vni);
	if (!f)
		goto out;

	rdst = vxlan_fdb_find_rdst(f, &fdb_info->remote_ip,
				   fdb_info->remote_port,
				   fdb_info->remote_vni,
				   fdb_info->remote_ifindex);
	if (!rdst)
		goto out;

	rdst->offloaded = fdb_info->offloaded;

out:
	spin_unlock_bh(&vxlan->hash_lock[hash_index]);
}

static int
vxlan_fdb_external_learn_add(struct net_device *dev,
			     struct switchdev_notifier_vxlan_fdb_info *fdb_info)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct netlink_ext_ack *extack;
	u32 hash_index;
	int err;

	hash_index = fdb_head_index(vxlan, fdb_info->eth_addr, fdb_info->vni);
	extack = switchdev_notifier_info_to_extack(&fdb_info->info);

	spin_lock_bh(&vxlan->hash_lock[hash_index]);
	err = vxlan_fdb_update(vxlan, fdb_info->eth_addr, &fdb_info->remote_ip,
			       NUD_REACHABLE,
			       NLM_F_CREATE | NLM_F_REPLACE,
			       fdb_info->remote_port,
			       fdb_info->vni,
			       fdb_info->remote_vni,
			       fdb_info->remote_ifindex,
			       NTF_USE | NTF_SELF | NTF_EXT_LEARNED,
			       0, false, extack);
	spin_unlock_bh(&vxlan->hash_lock[hash_index]);

	return err;
}

static int
vxlan_fdb_external_learn_del(struct net_device *dev,
			     struct switchdev_notifier_vxlan_fdb_info *fdb_info)
{
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct vxlan_fdb *f;
	u32 hash_index;
	int err = 0;

	hash_index = fdb_head_index(vxlan, fdb_info->eth_addr, fdb_info->vni);
	spin_lock_bh(&vxlan->hash_lock[hash_index]);

	f = vxlan_find_mac(vxlan, fdb_info->eth_addr, fdb_info->vni);
	if (!f)
		err = -ENOENT;
	else if (f->flags & NTF_EXT_LEARNED)
		err = __vxlan_fdb_delete(vxlan, fdb_info->eth_addr,
					 fdb_info->remote_ip,
					 fdb_info->remote_port,
					 fdb_info->vni,
					 fdb_info->remote_vni,
					 fdb_info->remote_ifindex,
					 false);

	spin_unlock_bh(&vxlan->hash_lock[hash_index]);

	return err;
}

static int vxlan_switchdev_event(struct notifier_block *unused,
				 unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	struct switchdev_notifier_vxlan_fdb_info *fdb_info;
	int err = 0;

	switch (event) {
	case SWITCHDEV_VXLAN_FDB_OFFLOADED:
		vxlan_fdb_offloaded_set(dev, ptr);
		break;
	case SWITCHDEV_VXLAN_FDB_ADD_TO_BRIDGE:
		fdb_info = ptr;
		err = vxlan_fdb_external_learn_add(dev, fdb_info);
		if (err) {
			err = notifier_from_errno(err);
			break;
		}
		fdb_info->offloaded = true;
		vxlan_fdb_offloaded_set(dev, fdb_info);
		break;
	case SWITCHDEV_VXLAN_FDB_DEL_TO_BRIDGE:
		fdb_info = ptr;
		err = vxlan_fdb_external_learn_del(dev, fdb_info);
		if (err) {
			err = notifier_from_errno(err);
			break;
		}
		fdb_info->offloaded = false;
		vxlan_fdb_offloaded_set(dev, fdb_info);
		break;
	}

	return err;
}

static struct notifier_block vxlan_switchdev_notifier_block __read_mostly = {
	.notifier_call = vxlan_switchdev_event,
};

static void vxlan_fdb_nh_flush(struct nexthop *nh)
{
	struct vxlan_fdb *fdb;
	struct vxlan_dev *vxlan;
	u32 hash_index;

	rcu_read_lock();
	list_for_each_entry_rcu(fdb, &nh->fdb_list, nh_list) {
		vxlan = rcu_dereference(fdb->vdev);
		WARN_ON(!vxlan);
		hash_index = fdb_head_index(vxlan, fdb->eth_addr,
					    vxlan->default_dst.remote_vni);
		spin_lock_bh(&vxlan->hash_lock[hash_index]);
		if (!hlist_unhashed(&fdb->hlist))
			vxlan_fdb_destroy(vxlan, fdb, false, false);
		spin_unlock_bh(&vxlan->hash_lock[hash_index]);
	}
	rcu_read_unlock();
}

static int vxlan_nexthop_event(struct notifier_block *nb,
			       unsigned long event, void *ptr)
{
	struct nh_notifier_info *info = ptr;
	struct nexthop *nh;

	if (event != NEXTHOP_EVENT_DEL)
		return NOTIFY_DONE;

	nh = nexthop_find_by_id(info->net, info->id);
	if (!nh)
		return NOTIFY_DONE;

	vxlan_fdb_nh_flush(nh);

	return NOTIFY_DONE;
}

static __net_init int vxlan_init_net(struct net *net)
{
	struct vxlan_net *vn = net_generic(net, vxlan_net_id);
	unsigned int h;

	INIT_LIST_HEAD(&vn->vxlan_list);
	spin_lock_init(&vn->sock_lock);
	vn->nexthop_notifier_block.notifier_call = vxlan_nexthop_event;

	for (h = 0; h < PORT_HASH_SIZE; ++h)
		INIT_HLIST_HEAD(&vn->sock_list[h]);

	return register_nexthop_notifier(net, &vn->nexthop_notifier_block,
					 NULL);
}

static void __net_exit vxlan_destroy_tunnels(struct vxlan_net *vn,
					     struct list_head *dev_to_kill)
{
	struct vxlan_dev *vxlan, *next;

	list_for_each_entry_safe(vxlan, next, &vn->vxlan_list, next)
		vxlan_dellink(vxlan->dev, dev_to_kill);
}

static void __net_exit vxlan_exit_batch_rtnl(struct list_head *net_list,
					     struct list_head *dev_to_kill)
{
	struct net *net;

	ASSERT_RTNL();
	list_for_each_entry(net, net_list, exit_list) {
		struct vxlan_net *vn = net_generic(net, vxlan_net_id);

		__unregister_nexthop_notifier(net, &vn->nexthop_notifier_block);

		vxlan_destroy_tunnels(vn, dev_to_kill);
	}
}

static void __net_exit vxlan_exit_net(struct net *net)
{
	struct vxlan_net *vn = net_generic(net, vxlan_net_id);
	unsigned int h;

	for (h = 0; h < PORT_HASH_SIZE; ++h)
		WARN_ON_ONCE(!hlist_empty(&vn->sock_list[h]));
}

static struct pernet_operations vxlan_net_ops = {
	.init = vxlan_init_net,
	.exit_batch_rtnl = vxlan_exit_batch_rtnl,
	.exit = vxlan_exit_net,
	.id   = &vxlan_net_id,
	.size = sizeof(struct vxlan_net),
};

static int __init vxlan_init_module(void)
{
	int rc;

	get_random_bytes(&vxlan_salt, sizeof(vxlan_salt));

	rc = register_pernet_subsys(&vxlan_net_ops);
	if (rc)
		goto out1;

	rc = register_netdevice_notifier(&vxlan_notifier_block);
	if (rc)
		goto out2;

	rc = register_switchdev_notifier(&vxlan_switchdev_notifier_block);
	if (rc)
		goto out3;

	rc = rtnl_link_register(&vxlan_link_ops);
	if (rc)
		goto out4;

	rc = vxlan_vnifilter_init();
	if (rc)
		goto out5;

	return 0;
out5:
	rtnl_link_unregister(&vxlan_link_ops);
out4:
	unregister_switchdev_notifier(&vxlan_switchdev_notifier_block);
out3:
	unregister_netdevice_notifier(&vxlan_notifier_block);
out2:
	unregister_pernet_subsys(&vxlan_net_ops);
out1:
	return rc;
}
late_initcall(vxlan_init_module);

static void __exit vxlan_cleanup_module(void)
{
	vxlan_vnifilter_uninit();
	rtnl_link_unregister(&vxlan_link_ops);
	unregister_switchdev_notifier(&vxlan_switchdev_notifier_block);
	unregister_netdevice_notifier(&vxlan_notifier_block);
	unregister_pernet_subsys(&vxlan_net_ops);
	/* rcu_barrier() is called by netns */
}
module_exit(vxlan_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_VERSION(VXLAN_VERSION);
MODULE_AUTHOR("Stephen Hemminger <stephen@networkplumber.org>");
MODULE_DESCRIPTION("Driver for VXLAN encapsulated traffic");
MODULE_ALIAS_RTNL_LINK("vxlan");
