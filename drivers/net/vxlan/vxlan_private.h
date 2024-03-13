/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	Vxlan private header file
 *
 */

#ifndef _VXLAN_PRIVATE_H
#define _VXLAN_PRIVATE_H

#include <linux/rhashtable.h>

extern unsigned int vxlan_net_id;
extern const u8 all_zeros_mac[ETH_ALEN + 2];
extern const struct rhashtable_params vxlan_vni_rht_params;

#define PORT_HASH_BITS	8
#define PORT_HASH_SIZE  (1 << PORT_HASH_BITS)

/* per-network namespace private data for this module */
struct vxlan_net {
	struct list_head  vxlan_list;
	struct hlist_head sock_list[PORT_HASH_SIZE];
	spinlock_t	  sock_lock;
	struct notifier_block nexthop_notifier_block;
};

/* Forwarding table entry */
struct vxlan_fdb {
	struct hlist_node hlist;	/* linked list of entries */
	struct rcu_head	  rcu;
	unsigned long	  updated;	/* jiffies */
	unsigned long	  used;
	struct list_head  remotes;
	u8		  eth_addr[ETH_ALEN];
	u16		  state;	/* see ndm_state */
	__be32		  vni;
	u16		  flags;	/* see ndm_flags and below */
	struct list_head  nh_list;
	struct nexthop __rcu *nh;
	struct vxlan_dev  __rcu *vdev;
};

#define NTF_VXLAN_ADDED_BY_USER 0x100

/* Virtual Network hash table head */
static inline struct hlist_head *vni_head(struct vxlan_sock *vs, __be32 vni)
{
	return &vs->vni_list[hash_32((__force u32)vni, VNI_HASH_BITS)];
}

/* Socket hash table head */
static inline struct hlist_head *vs_head(struct net *net, __be16 port)
{
	struct vxlan_net *vn = net_generic(net, vxlan_net_id);

	return &vn->sock_list[hash_32(ntohs(port), PORT_HASH_BITS)];
}

/* First remote destination for a forwarding entry.
 * Guaranteed to be non-NULL because remotes are never deleted.
 */
static inline struct vxlan_rdst *first_remote_rcu(struct vxlan_fdb *fdb)
{
	if (rcu_access_pointer(fdb->nh))
		return NULL;
	return list_entry_rcu(fdb->remotes.next, struct vxlan_rdst, list);
}

static inline struct vxlan_rdst *first_remote_rtnl(struct vxlan_fdb *fdb)
{
	if (rcu_access_pointer(fdb->nh))
		return NULL;
	return list_first_entry(&fdb->remotes, struct vxlan_rdst, list);
}

#if IS_ENABLED(CONFIG_IPV6)
static inline
bool vxlan_addr_equal(const union vxlan_addr *a, const union vxlan_addr *b)
{
	if (a->sa.sa_family != b->sa.sa_family)
		return false;
	if (a->sa.sa_family == AF_INET6)
		return ipv6_addr_equal(&a->sin6.sin6_addr, &b->sin6.sin6_addr);
	else
		return a->sin.sin_addr.s_addr == b->sin.sin_addr.s_addr;
}

#else /* !CONFIG_IPV6 */

static inline
bool vxlan_addr_equal(const union vxlan_addr *a, const union vxlan_addr *b)
{
	return a->sin.sin_addr.s_addr == b->sin.sin_addr.s_addr;
}

#endif

static inline struct vxlan_vni_node *
vxlan_vnifilter_lookup(struct vxlan_dev *vxlan, __be32 vni)
{
	struct vxlan_vni_group *vg;

	vg = rcu_dereference_rtnl(vxlan->vnigrp);
	if (!vg)
		return NULL;

	return rhashtable_lookup_fast(&vg->vni_hash, &vni,
				      vxlan_vni_rht_params);
}

/* vxlan_core.c */
int vxlan_fdb_create(struct vxlan_dev *vxlan,
		     const u8 *mac, union vxlan_addr *ip,
		     __u16 state, __be16 port, __be32 src_vni,
		     __be32 vni, __u32 ifindex, __u16 ndm_flags,
		     u32 nhid, struct vxlan_fdb **fdb,
		     struct netlink_ext_ack *extack);
int __vxlan_fdb_delete(struct vxlan_dev *vxlan,
		       const unsigned char *addr, union vxlan_addr ip,
		       __be16 port, __be32 src_vni, __be32 vni,
		       u32 ifindex, bool swdev_notify);
u32 eth_vni_hash(const unsigned char *addr, __be32 vni);
u32 fdb_head_index(struct vxlan_dev *vxlan, const u8 *mac, __be32 vni);
int vxlan_fdb_update(struct vxlan_dev *vxlan,
		     const u8 *mac, union vxlan_addr *ip,
		     __u16 state, __u16 flags,
		     __be16 port, __be32 src_vni, __be32 vni,
		     __u32 ifindex, __u16 ndm_flags, u32 nhid,
		     bool swdev_notify, struct netlink_ext_ack *extack);
int vxlan_vni_in_use(struct net *src_net, struct vxlan_dev *vxlan,
		     struct vxlan_config *conf, __be32 vni);

/* vxlan_vnifilter.c */
int vxlan_vnigroup_init(struct vxlan_dev *vxlan);
void vxlan_vnigroup_uninit(struct vxlan_dev *vxlan);

void vxlan_vnifilter_init(void);
void vxlan_vnifilter_uninit(void);
void vxlan_vnifilter_count(struct vxlan_dev *vxlan, __be32 vni,
			   struct vxlan_vni_node *vninode,
			   int type, unsigned int len);

void vxlan_vs_add_vnigrp(struct vxlan_dev *vxlan,
			 struct vxlan_sock *vs,
			 bool ipv6);
void vxlan_vs_del_vnigrp(struct vxlan_dev *vxlan);
int vxlan_vnilist_update_group(struct vxlan_dev *vxlan,
			       union vxlan_addr *old_remote_ip,
			       union vxlan_addr *new_remote_ip,
			       struct netlink_ext_ack *extack);


/* vxlan_multicast.c */
int vxlan_multicast_join(struct vxlan_dev *vxlan);
int vxlan_multicast_leave(struct vxlan_dev *vxlan);
bool vxlan_group_used(struct vxlan_net *vn, struct vxlan_dev *dev,
		      __be32 vni, union vxlan_addr *rip, int rifindex);
int vxlan_igmp_join(struct vxlan_dev *vxlan, union vxlan_addr *rip,
		    int rifindex);
int vxlan_igmp_leave(struct vxlan_dev *vxlan, union vxlan_addr *rip,
		     int rifindex);
#endif
