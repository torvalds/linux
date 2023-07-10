// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2016-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/rhashtable.h>
#include <linux/bitops.h>
#include <linux/in6.h>
#include <linux/notifier.h>
#include <linux/inetdevice.h>
#include <linux/netdevice.h>
#include <linux/if_bridge.h>
#include <linux/socket.h>
#include <linux/route.h>
#include <linux/gcd.h>
#include <linux/if_macvlan.h>
#include <linux/refcount.h>
#include <linux/jhash.h>
#include <linux/net_namespace.h>
#include <linux/mutex.h>
#include <linux/genalloc.h>
#include <net/netevent.h>
#include <net/neighbour.h>
#include <net/arp.h>
#include <net/inet_dscp.h>
#include <net/ip_fib.h>
#include <net/ip6_fib.h>
#include <net/nexthop.h>
#include <net/fib_rules.h>
#include <net/ip_tunnels.h>
#include <net/l3mdev.h>
#include <net/addrconf.h>
#include <net/ndisc.h>
#include <net/ipv6.h>
#include <net/fib_notifier.h>
#include <net/switchdev.h>

#include "spectrum.h"
#include "core.h"
#include "reg.h"
#include "spectrum_cnt.h"
#include "spectrum_dpipe.h"
#include "spectrum_ipip.h"
#include "spectrum_mr.h"
#include "spectrum_mr_tcam.h"
#include "spectrum_router.h"
#include "spectrum_span.h"

struct mlxsw_sp_fib;
struct mlxsw_sp_vr;
struct mlxsw_sp_lpm_tree;
struct mlxsw_sp_rif_ops;

struct mlxsw_sp_crif_key {
	struct net_device *dev;
};

struct mlxsw_sp_crif {
	struct mlxsw_sp_crif_key key;
	struct rhash_head ht_node;
	bool can_destroy;
	struct list_head nexthop_list;
	struct mlxsw_sp_rif *rif;
};

static const struct rhashtable_params mlxsw_sp_crif_ht_params = {
	.key_offset = offsetof(struct mlxsw_sp_crif, key),
	.key_len = sizeof_field(struct mlxsw_sp_crif, key),
	.head_offset = offsetof(struct mlxsw_sp_crif, ht_node),
};

struct mlxsw_sp_rif {
	struct mlxsw_sp_crif *crif; /* NULL for underlay RIF */
	struct list_head neigh_list;
	struct mlxsw_sp_fid *fid;
	unsigned char addr[ETH_ALEN];
	int mtu;
	u16 rif_index;
	u8 mac_profile_id;
	u8 rif_entries;
	u16 vr_id;
	const struct mlxsw_sp_rif_ops *ops;
	struct mlxsw_sp *mlxsw_sp;

	unsigned int counter_ingress;
	bool counter_ingress_valid;
	unsigned int counter_egress;
	bool counter_egress_valid;
};

static struct net_device *mlxsw_sp_rif_dev(const struct mlxsw_sp_rif *rif)
{
	if (!rif->crif)
		return NULL;
	return rif->crif->key.dev;
}

struct mlxsw_sp_rif_params {
	struct net_device *dev;
	union {
		u16 system_port;
		u16 lag_id;
	};
	u16 vid;
	bool lag;
	bool double_entry;
};

struct mlxsw_sp_rif_subport {
	struct mlxsw_sp_rif common;
	refcount_t ref_count;
	union {
		u16 system_port;
		u16 lag_id;
	};
	u16 vid;
	bool lag;
};

struct mlxsw_sp_rif_ipip_lb {
	struct mlxsw_sp_rif common;
	struct mlxsw_sp_rif_ipip_lb_config lb_config;
	u16 ul_vr_id;	/* Spectrum-1. */
	u16 ul_rif_id;	/* Spectrum-2+. */
};

struct mlxsw_sp_rif_params_ipip_lb {
	struct mlxsw_sp_rif_params common;
	struct mlxsw_sp_rif_ipip_lb_config lb_config;
};

struct mlxsw_sp_rif_ops {
	enum mlxsw_sp_rif_type type;
	size_t rif_size;

	void (*setup)(struct mlxsw_sp_rif *rif,
		      const struct mlxsw_sp_rif_params *params);
	int (*configure)(struct mlxsw_sp_rif *rif,
			 struct netlink_ext_ack *extack);
	void (*deconfigure)(struct mlxsw_sp_rif *rif);
	struct mlxsw_sp_fid * (*fid_get)(struct mlxsw_sp_rif *rif,
					 struct netlink_ext_ack *extack);
	void (*fdb_del)(struct mlxsw_sp_rif *rif, const char *mac);
};

struct mlxsw_sp_rif_mac_profile {
	unsigned char mac_prefix[ETH_ALEN];
	refcount_t ref_count;
	u8 id;
};

struct mlxsw_sp_router_ops {
	int (*init)(struct mlxsw_sp *mlxsw_sp);
	int (*ipips_init)(struct mlxsw_sp *mlxsw_sp);
};

static struct mlxsw_sp_rif *
mlxsw_sp_rif_find_by_dev(const struct mlxsw_sp *mlxsw_sp,
			 const struct net_device *dev);
static void mlxsw_sp_rif_destroy(struct mlxsw_sp_rif *rif);
static void mlxsw_sp_lpm_tree_hold(struct mlxsw_sp_lpm_tree *lpm_tree);
static void mlxsw_sp_lpm_tree_put(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_lpm_tree *lpm_tree);
static int mlxsw_sp_vr_lpm_tree_bind(struct mlxsw_sp *mlxsw_sp,
				     const struct mlxsw_sp_fib *fib,
				     u8 tree_id);
static int mlxsw_sp_vr_lpm_tree_unbind(struct mlxsw_sp *mlxsw_sp,
				       const struct mlxsw_sp_fib *fib);

static unsigned int *
mlxsw_sp_rif_p_counter_get(struct mlxsw_sp_rif *rif,
			   enum mlxsw_sp_rif_counter_dir dir)
{
	switch (dir) {
	case MLXSW_SP_RIF_COUNTER_EGRESS:
		return &rif->counter_egress;
	case MLXSW_SP_RIF_COUNTER_INGRESS:
		return &rif->counter_ingress;
	}
	return NULL;
}

static bool
mlxsw_sp_rif_counter_valid_get(struct mlxsw_sp_rif *rif,
			       enum mlxsw_sp_rif_counter_dir dir)
{
	switch (dir) {
	case MLXSW_SP_RIF_COUNTER_EGRESS:
		return rif->counter_egress_valid;
	case MLXSW_SP_RIF_COUNTER_INGRESS:
		return rif->counter_ingress_valid;
	}
	return false;
}

static void
mlxsw_sp_rif_counter_valid_set(struct mlxsw_sp_rif *rif,
			       enum mlxsw_sp_rif_counter_dir dir,
			       bool valid)
{
	switch (dir) {
	case MLXSW_SP_RIF_COUNTER_EGRESS:
		rif->counter_egress_valid = valid;
		break;
	case MLXSW_SP_RIF_COUNTER_INGRESS:
		rif->counter_ingress_valid = valid;
		break;
	}
}

static int mlxsw_sp_rif_counter_edit(struct mlxsw_sp *mlxsw_sp, u16 rif_index,
				     unsigned int counter_index, bool enable,
				     enum mlxsw_sp_rif_counter_dir dir)
{
	char ritr_pl[MLXSW_REG_RITR_LEN];
	bool is_egress = false;
	int err;

	if (dir == MLXSW_SP_RIF_COUNTER_EGRESS)
		is_egress = true;
	mlxsw_reg_ritr_rif_pack(ritr_pl, rif_index);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
	if (err)
		return err;

	mlxsw_reg_ritr_counter_pack(ritr_pl, counter_index, enable,
				    is_egress);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
}

int mlxsw_sp_rif_counter_value_get(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_rif *rif,
				   enum mlxsw_sp_rif_counter_dir dir, u64 *cnt)
{
	char ricnt_pl[MLXSW_REG_RICNT_LEN];
	unsigned int *p_counter_index;
	bool valid;
	int err;

	valid = mlxsw_sp_rif_counter_valid_get(rif, dir);
	if (!valid)
		return -EINVAL;

	p_counter_index = mlxsw_sp_rif_p_counter_get(rif, dir);
	if (!p_counter_index)
		return -EINVAL;
	mlxsw_reg_ricnt_pack(ricnt_pl, *p_counter_index,
			     MLXSW_REG_RICNT_OPCODE_NOP);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ricnt), ricnt_pl);
	if (err)
		return err;
	*cnt = mlxsw_reg_ricnt_good_unicast_packets_get(ricnt_pl);
	return 0;
}

struct mlxsw_sp_rif_counter_set_basic {
	u64 good_unicast_packets;
	u64 good_multicast_packets;
	u64 good_broadcast_packets;
	u64 good_unicast_bytes;
	u64 good_multicast_bytes;
	u64 good_broadcast_bytes;
	u64 error_packets;
	u64 discard_packets;
	u64 error_bytes;
	u64 discard_bytes;
};

static int
mlxsw_sp_rif_counter_fetch_clear(struct mlxsw_sp_rif *rif,
				 enum mlxsw_sp_rif_counter_dir dir,
				 struct mlxsw_sp_rif_counter_set_basic *set)
{
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	char ricnt_pl[MLXSW_REG_RICNT_LEN];
	unsigned int *p_counter_index;
	int err;

	if (!mlxsw_sp_rif_counter_valid_get(rif, dir))
		return -EINVAL;

	p_counter_index = mlxsw_sp_rif_p_counter_get(rif, dir);
	if (!p_counter_index)
		return -EINVAL;

	mlxsw_reg_ricnt_pack(ricnt_pl, *p_counter_index,
			     MLXSW_REG_RICNT_OPCODE_CLEAR);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ricnt), ricnt_pl);
	if (err)
		return err;

	if (!set)
		return 0;

#define MLXSW_SP_RIF_COUNTER_EXTRACT(NAME)				\
		(set->NAME = mlxsw_reg_ricnt_ ## NAME ## _get(ricnt_pl))

	MLXSW_SP_RIF_COUNTER_EXTRACT(good_unicast_packets);
	MLXSW_SP_RIF_COUNTER_EXTRACT(good_multicast_packets);
	MLXSW_SP_RIF_COUNTER_EXTRACT(good_broadcast_packets);
	MLXSW_SP_RIF_COUNTER_EXTRACT(good_unicast_bytes);
	MLXSW_SP_RIF_COUNTER_EXTRACT(good_multicast_bytes);
	MLXSW_SP_RIF_COUNTER_EXTRACT(good_broadcast_bytes);
	MLXSW_SP_RIF_COUNTER_EXTRACT(error_packets);
	MLXSW_SP_RIF_COUNTER_EXTRACT(discard_packets);
	MLXSW_SP_RIF_COUNTER_EXTRACT(error_bytes);
	MLXSW_SP_RIF_COUNTER_EXTRACT(discard_bytes);

#undef MLXSW_SP_RIF_COUNTER_EXTRACT

	return 0;
}

static int mlxsw_sp_rif_counter_clear(struct mlxsw_sp *mlxsw_sp,
				      unsigned int counter_index)
{
	char ricnt_pl[MLXSW_REG_RICNT_LEN];

	mlxsw_reg_ricnt_pack(ricnt_pl, counter_index,
			     MLXSW_REG_RICNT_OPCODE_CLEAR);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ricnt), ricnt_pl);
}

int mlxsw_sp_rif_counter_alloc(struct mlxsw_sp_rif *rif,
			       enum mlxsw_sp_rif_counter_dir dir)
{
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	unsigned int *p_counter_index;
	int err;

	if (mlxsw_sp_rif_counter_valid_get(rif, dir))
		return 0;

	p_counter_index = mlxsw_sp_rif_p_counter_get(rif, dir);
	if (!p_counter_index)
		return -EINVAL;

	err = mlxsw_sp_counter_alloc(mlxsw_sp, MLXSW_SP_COUNTER_SUB_POOL_RIF,
				     p_counter_index);
	if (err)
		return err;

	err = mlxsw_sp_rif_counter_clear(mlxsw_sp, *p_counter_index);
	if (err)
		goto err_counter_clear;

	err = mlxsw_sp_rif_counter_edit(mlxsw_sp, rif->rif_index,
					*p_counter_index, true, dir);
	if (err)
		goto err_counter_edit;
	mlxsw_sp_rif_counter_valid_set(rif, dir, true);
	return 0;

err_counter_edit:
err_counter_clear:
	mlxsw_sp_counter_free(mlxsw_sp, MLXSW_SP_COUNTER_SUB_POOL_RIF,
			      *p_counter_index);
	return err;
}

void mlxsw_sp_rif_counter_free(struct mlxsw_sp_rif *rif,
			       enum mlxsw_sp_rif_counter_dir dir)
{
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	unsigned int *p_counter_index;

	if (!mlxsw_sp_rif_counter_valid_get(rif, dir))
		return;

	p_counter_index = mlxsw_sp_rif_p_counter_get(rif, dir);
	if (WARN_ON(!p_counter_index))
		return;
	mlxsw_sp_rif_counter_edit(mlxsw_sp, rif->rif_index,
				  *p_counter_index, false, dir);
	mlxsw_sp_counter_free(mlxsw_sp, MLXSW_SP_COUNTER_SUB_POOL_RIF,
			      *p_counter_index);
	mlxsw_sp_rif_counter_valid_set(rif, dir, false);
}

static void mlxsw_sp_rif_counters_alloc(struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	struct devlink *devlink;

	devlink = priv_to_devlink(mlxsw_sp->core);
	if (!devlink_dpipe_table_counter_enabled(devlink,
						 MLXSW_SP_DPIPE_TABLE_NAME_ERIF))
		return;
	mlxsw_sp_rif_counter_alloc(rif, MLXSW_SP_RIF_COUNTER_EGRESS);
}

static void mlxsw_sp_rif_counters_free(struct mlxsw_sp_rif *rif)
{
	mlxsw_sp_rif_counter_free(rif, MLXSW_SP_RIF_COUNTER_EGRESS);
}

#define MLXSW_SP_PREFIX_COUNT (sizeof(struct in6_addr) * BITS_PER_BYTE + 1)

struct mlxsw_sp_prefix_usage {
	DECLARE_BITMAP(b, MLXSW_SP_PREFIX_COUNT);
};

#define mlxsw_sp_prefix_usage_for_each(prefix, prefix_usage) \
	for_each_set_bit(prefix, (prefix_usage)->b, MLXSW_SP_PREFIX_COUNT)

static bool
mlxsw_sp_prefix_usage_eq(struct mlxsw_sp_prefix_usage *prefix_usage1,
			 struct mlxsw_sp_prefix_usage *prefix_usage2)
{
	return !memcmp(prefix_usage1, prefix_usage2, sizeof(*prefix_usage1));
}

static void
mlxsw_sp_prefix_usage_cpy(struct mlxsw_sp_prefix_usage *prefix_usage1,
			  struct mlxsw_sp_prefix_usage *prefix_usage2)
{
	memcpy(prefix_usage1, prefix_usage2, sizeof(*prefix_usage1));
}

static void
mlxsw_sp_prefix_usage_set(struct mlxsw_sp_prefix_usage *prefix_usage,
			  unsigned char prefix_len)
{
	set_bit(prefix_len, prefix_usage->b);
}

static void
mlxsw_sp_prefix_usage_clear(struct mlxsw_sp_prefix_usage *prefix_usage,
			    unsigned char prefix_len)
{
	clear_bit(prefix_len, prefix_usage->b);
}

struct mlxsw_sp_fib_key {
	unsigned char addr[sizeof(struct in6_addr)];
	unsigned char prefix_len;
};

enum mlxsw_sp_fib_entry_type {
	MLXSW_SP_FIB_ENTRY_TYPE_REMOTE,
	MLXSW_SP_FIB_ENTRY_TYPE_LOCAL,
	MLXSW_SP_FIB_ENTRY_TYPE_TRAP,
	MLXSW_SP_FIB_ENTRY_TYPE_BLACKHOLE,
	MLXSW_SP_FIB_ENTRY_TYPE_UNREACHABLE,

	/* This is a special case of local delivery, where a packet should be
	 * decapsulated on reception. Note that there is no corresponding ENCAP,
	 * because that's a type of next hop, not of FIB entry. (There can be
	 * several next hops in a REMOTE entry, and some of them may be
	 * encapsulating entries.)
	 */
	MLXSW_SP_FIB_ENTRY_TYPE_IPIP_DECAP,
	MLXSW_SP_FIB_ENTRY_TYPE_NVE_DECAP,
};

struct mlxsw_sp_nexthop_group_info;
struct mlxsw_sp_nexthop_group;
struct mlxsw_sp_fib_entry;

struct mlxsw_sp_fib_node {
	struct mlxsw_sp_fib_entry *fib_entry;
	struct list_head list;
	struct rhash_head ht_node;
	struct mlxsw_sp_fib *fib;
	struct mlxsw_sp_fib_key key;
};

struct mlxsw_sp_fib_entry_decap {
	struct mlxsw_sp_ipip_entry *ipip_entry;
	u32 tunnel_index;
};

struct mlxsw_sp_fib_entry {
	struct mlxsw_sp_fib_node *fib_node;
	enum mlxsw_sp_fib_entry_type type;
	struct list_head nexthop_group_node;
	struct mlxsw_sp_nexthop_group *nh_group;
	struct mlxsw_sp_fib_entry_decap decap; /* Valid for decap entries. */
};

struct mlxsw_sp_fib4_entry {
	struct mlxsw_sp_fib_entry common;
	struct fib_info *fi;
	u32 tb_id;
	dscp_t dscp;
	u8 type;
};

struct mlxsw_sp_fib6_entry {
	struct mlxsw_sp_fib_entry common;
	struct list_head rt6_list;
	unsigned int nrt6;
};

struct mlxsw_sp_rt6 {
	struct list_head list;
	struct fib6_info *rt;
};

struct mlxsw_sp_lpm_tree {
	u8 id; /* tree ID */
	unsigned int ref_count;
	enum mlxsw_sp_l3proto proto;
	unsigned long prefix_ref_count[MLXSW_SP_PREFIX_COUNT];
	struct mlxsw_sp_prefix_usage prefix_usage;
};

struct mlxsw_sp_fib {
	struct rhashtable ht;
	struct list_head node_list;
	struct mlxsw_sp_vr *vr;
	struct mlxsw_sp_lpm_tree *lpm_tree;
	enum mlxsw_sp_l3proto proto;
};

struct mlxsw_sp_vr {
	u16 id; /* virtual router ID */
	u32 tb_id; /* kernel fib table id */
	unsigned int rif_count;
	struct mlxsw_sp_fib *fib4;
	struct mlxsw_sp_fib *fib6;
	struct mlxsw_sp_mr_table *mr_table[MLXSW_SP_L3_PROTO_MAX];
	struct mlxsw_sp_rif *ul_rif;
	refcount_t ul_rif_refcnt;
};

static const struct rhashtable_params mlxsw_sp_fib_ht_params;

static struct mlxsw_sp_fib *mlxsw_sp_fib_create(struct mlxsw_sp *mlxsw_sp,
						struct mlxsw_sp_vr *vr,
						enum mlxsw_sp_l3proto proto)
{
	struct mlxsw_sp_lpm_tree *lpm_tree;
	struct mlxsw_sp_fib *fib;
	int err;

	lpm_tree = mlxsw_sp->router->lpm.proto_trees[proto];
	fib = kzalloc(sizeof(*fib), GFP_KERNEL);
	if (!fib)
		return ERR_PTR(-ENOMEM);
	err = rhashtable_init(&fib->ht, &mlxsw_sp_fib_ht_params);
	if (err)
		goto err_rhashtable_init;
	INIT_LIST_HEAD(&fib->node_list);
	fib->proto = proto;
	fib->vr = vr;
	fib->lpm_tree = lpm_tree;
	mlxsw_sp_lpm_tree_hold(lpm_tree);
	err = mlxsw_sp_vr_lpm_tree_bind(mlxsw_sp, fib, lpm_tree->id);
	if (err)
		goto err_lpm_tree_bind;
	return fib;

err_lpm_tree_bind:
	mlxsw_sp_lpm_tree_put(mlxsw_sp, lpm_tree);
err_rhashtable_init:
	kfree(fib);
	return ERR_PTR(err);
}

static void mlxsw_sp_fib_destroy(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_fib *fib)
{
	mlxsw_sp_vr_lpm_tree_unbind(mlxsw_sp, fib);
	mlxsw_sp_lpm_tree_put(mlxsw_sp, fib->lpm_tree);
	WARN_ON(!list_empty(&fib->node_list));
	rhashtable_destroy(&fib->ht);
	kfree(fib);
}

static struct mlxsw_sp_lpm_tree *
mlxsw_sp_lpm_tree_find_unused(struct mlxsw_sp *mlxsw_sp)
{
	static struct mlxsw_sp_lpm_tree *lpm_tree;
	int i;

	for (i = 0; i < mlxsw_sp->router->lpm.tree_count; i++) {
		lpm_tree = &mlxsw_sp->router->lpm.trees[i];
		if (lpm_tree->ref_count == 0)
			return lpm_tree;
	}
	return NULL;
}

static int mlxsw_sp_lpm_tree_alloc(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_lpm_tree *lpm_tree)
{
	char ralta_pl[MLXSW_REG_RALTA_LEN];

	mlxsw_reg_ralta_pack(ralta_pl, true,
			     (enum mlxsw_reg_ralxx_protocol) lpm_tree->proto,
			     lpm_tree->id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralta), ralta_pl);
}

static void mlxsw_sp_lpm_tree_free(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_lpm_tree *lpm_tree)
{
	char ralta_pl[MLXSW_REG_RALTA_LEN];

	mlxsw_reg_ralta_pack(ralta_pl, false,
			     (enum mlxsw_reg_ralxx_protocol) lpm_tree->proto,
			     lpm_tree->id);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralta), ralta_pl);
}

static int
mlxsw_sp_lpm_tree_left_struct_set(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_prefix_usage *prefix_usage,
				  struct mlxsw_sp_lpm_tree *lpm_tree)
{
	char ralst_pl[MLXSW_REG_RALST_LEN];
	u8 root_bin = 0;
	u8 prefix;
	u8 last_prefix = MLXSW_REG_RALST_BIN_NO_CHILD;

	mlxsw_sp_prefix_usage_for_each(prefix, prefix_usage)
		root_bin = prefix;

	mlxsw_reg_ralst_pack(ralst_pl, root_bin, lpm_tree->id);
	mlxsw_sp_prefix_usage_for_each(prefix, prefix_usage) {
		if (prefix == 0)
			continue;
		mlxsw_reg_ralst_bin_pack(ralst_pl, prefix, last_prefix,
					 MLXSW_REG_RALST_BIN_NO_CHILD);
		last_prefix = prefix;
	}
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralst), ralst_pl);
}

static struct mlxsw_sp_lpm_tree *
mlxsw_sp_lpm_tree_create(struct mlxsw_sp *mlxsw_sp,
			 struct mlxsw_sp_prefix_usage *prefix_usage,
			 enum mlxsw_sp_l3proto proto)
{
	struct mlxsw_sp_lpm_tree *lpm_tree;
	int err;

	lpm_tree = mlxsw_sp_lpm_tree_find_unused(mlxsw_sp);
	if (!lpm_tree)
		return ERR_PTR(-EBUSY);
	lpm_tree->proto = proto;
	err = mlxsw_sp_lpm_tree_alloc(mlxsw_sp, lpm_tree);
	if (err)
		return ERR_PTR(err);

	err = mlxsw_sp_lpm_tree_left_struct_set(mlxsw_sp, prefix_usage,
						lpm_tree);
	if (err)
		goto err_left_struct_set;
	memcpy(&lpm_tree->prefix_usage, prefix_usage,
	       sizeof(lpm_tree->prefix_usage));
	memset(&lpm_tree->prefix_ref_count, 0,
	       sizeof(lpm_tree->prefix_ref_count));
	lpm_tree->ref_count = 1;
	return lpm_tree;

err_left_struct_set:
	mlxsw_sp_lpm_tree_free(mlxsw_sp, lpm_tree);
	return ERR_PTR(err);
}

static void mlxsw_sp_lpm_tree_destroy(struct mlxsw_sp *mlxsw_sp,
				      struct mlxsw_sp_lpm_tree *lpm_tree)
{
	mlxsw_sp_lpm_tree_free(mlxsw_sp, lpm_tree);
}

static struct mlxsw_sp_lpm_tree *
mlxsw_sp_lpm_tree_get(struct mlxsw_sp *mlxsw_sp,
		      struct mlxsw_sp_prefix_usage *prefix_usage,
		      enum mlxsw_sp_l3proto proto)
{
	struct mlxsw_sp_lpm_tree *lpm_tree;
	int i;

	for (i = 0; i < mlxsw_sp->router->lpm.tree_count; i++) {
		lpm_tree = &mlxsw_sp->router->lpm.trees[i];
		if (lpm_tree->ref_count != 0 &&
		    lpm_tree->proto == proto &&
		    mlxsw_sp_prefix_usage_eq(&lpm_tree->prefix_usage,
					     prefix_usage)) {
			mlxsw_sp_lpm_tree_hold(lpm_tree);
			return lpm_tree;
		}
	}
	return mlxsw_sp_lpm_tree_create(mlxsw_sp, prefix_usage, proto);
}

static void mlxsw_sp_lpm_tree_hold(struct mlxsw_sp_lpm_tree *lpm_tree)
{
	lpm_tree->ref_count++;
}

static void mlxsw_sp_lpm_tree_put(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_lpm_tree *lpm_tree)
{
	if (--lpm_tree->ref_count == 0)
		mlxsw_sp_lpm_tree_destroy(mlxsw_sp, lpm_tree);
}

#define MLXSW_SP_LPM_TREE_MIN 1 /* tree 0 is reserved */

static int mlxsw_sp_lpm_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_prefix_usage req_prefix_usage = {{ 0 } };
	struct mlxsw_sp_lpm_tree *lpm_tree;
	u64 max_trees;
	int err, i;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, MAX_LPM_TREES))
		return -EIO;

	max_trees = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_LPM_TREES);
	mlxsw_sp->router->lpm.tree_count = max_trees - MLXSW_SP_LPM_TREE_MIN;
	mlxsw_sp->router->lpm.trees = kcalloc(mlxsw_sp->router->lpm.tree_count,
					     sizeof(struct mlxsw_sp_lpm_tree),
					     GFP_KERNEL);
	if (!mlxsw_sp->router->lpm.trees)
		return -ENOMEM;

	for (i = 0; i < mlxsw_sp->router->lpm.tree_count; i++) {
		lpm_tree = &mlxsw_sp->router->lpm.trees[i];
		lpm_tree->id = i + MLXSW_SP_LPM_TREE_MIN;
	}

	lpm_tree = mlxsw_sp_lpm_tree_get(mlxsw_sp, &req_prefix_usage,
					 MLXSW_SP_L3_PROTO_IPV4);
	if (IS_ERR(lpm_tree)) {
		err = PTR_ERR(lpm_tree);
		goto err_ipv4_tree_get;
	}
	mlxsw_sp->router->lpm.proto_trees[MLXSW_SP_L3_PROTO_IPV4] = lpm_tree;

	lpm_tree = mlxsw_sp_lpm_tree_get(mlxsw_sp, &req_prefix_usage,
					 MLXSW_SP_L3_PROTO_IPV6);
	if (IS_ERR(lpm_tree)) {
		err = PTR_ERR(lpm_tree);
		goto err_ipv6_tree_get;
	}
	mlxsw_sp->router->lpm.proto_trees[MLXSW_SP_L3_PROTO_IPV6] = lpm_tree;

	return 0;

err_ipv6_tree_get:
	lpm_tree = mlxsw_sp->router->lpm.proto_trees[MLXSW_SP_L3_PROTO_IPV4];
	mlxsw_sp_lpm_tree_put(mlxsw_sp, lpm_tree);
err_ipv4_tree_get:
	kfree(mlxsw_sp->router->lpm.trees);
	return err;
}

static void mlxsw_sp_lpm_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_lpm_tree *lpm_tree;

	lpm_tree = mlxsw_sp->router->lpm.proto_trees[MLXSW_SP_L3_PROTO_IPV6];
	mlxsw_sp_lpm_tree_put(mlxsw_sp, lpm_tree);

	lpm_tree = mlxsw_sp->router->lpm.proto_trees[MLXSW_SP_L3_PROTO_IPV4];
	mlxsw_sp_lpm_tree_put(mlxsw_sp, lpm_tree);

	kfree(mlxsw_sp->router->lpm.trees);
}

static bool mlxsw_sp_vr_is_used(const struct mlxsw_sp_vr *vr)
{
	return !!vr->fib4 || !!vr->fib6 ||
	       !!vr->mr_table[MLXSW_SP_L3_PROTO_IPV4] ||
	       !!vr->mr_table[MLXSW_SP_L3_PROTO_IPV6];
}

static struct mlxsw_sp_vr *mlxsw_sp_vr_find_unused(struct mlxsw_sp *mlxsw_sp)
{
	int max_vrs = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_VRS);
	struct mlxsw_sp_vr *vr;
	int i;

	for (i = 0; i < max_vrs; i++) {
		vr = &mlxsw_sp->router->vrs[i];
		if (!mlxsw_sp_vr_is_used(vr))
			return vr;
	}
	return NULL;
}

static int mlxsw_sp_vr_lpm_tree_bind(struct mlxsw_sp *mlxsw_sp,
				     const struct mlxsw_sp_fib *fib, u8 tree_id)
{
	char raltb_pl[MLXSW_REG_RALTB_LEN];

	mlxsw_reg_raltb_pack(raltb_pl, fib->vr->id,
			     (enum mlxsw_reg_ralxx_protocol) fib->proto,
			     tree_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(raltb), raltb_pl);
}

static int mlxsw_sp_vr_lpm_tree_unbind(struct mlxsw_sp *mlxsw_sp,
				       const struct mlxsw_sp_fib *fib)
{
	char raltb_pl[MLXSW_REG_RALTB_LEN];

	/* Bind to tree 0 which is default */
	mlxsw_reg_raltb_pack(raltb_pl, fib->vr->id,
			     (enum mlxsw_reg_ralxx_protocol) fib->proto, 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(raltb), raltb_pl);
}

static u32 mlxsw_sp_fix_tb_id(u32 tb_id)
{
	/* For our purpose, squash main, default and local tables into one */
	if (tb_id == RT_TABLE_LOCAL || tb_id == RT_TABLE_DEFAULT)
		tb_id = RT_TABLE_MAIN;
	return tb_id;
}

static struct mlxsw_sp_vr *mlxsw_sp_vr_find(struct mlxsw_sp *mlxsw_sp,
					    u32 tb_id)
{
	int max_vrs = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_VRS);
	struct mlxsw_sp_vr *vr;
	int i;

	tb_id = mlxsw_sp_fix_tb_id(tb_id);

	for (i = 0; i < max_vrs; i++) {
		vr = &mlxsw_sp->router->vrs[i];
		if (mlxsw_sp_vr_is_used(vr) && vr->tb_id == tb_id)
			return vr;
	}
	return NULL;
}

int mlxsw_sp_router_tb_id_vr_id(struct mlxsw_sp *mlxsw_sp, u32 tb_id,
				u16 *vr_id)
{
	struct mlxsw_sp_vr *vr;
	int err = 0;

	mutex_lock(&mlxsw_sp->router->lock);
	vr = mlxsw_sp_vr_find(mlxsw_sp, tb_id);
	if (!vr) {
		err = -ESRCH;
		goto out;
	}
	*vr_id = vr->id;
out:
	mutex_unlock(&mlxsw_sp->router->lock);
	return err;
}

static struct mlxsw_sp_fib *mlxsw_sp_vr_fib(const struct mlxsw_sp_vr *vr,
					    enum mlxsw_sp_l3proto proto)
{
	switch (proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		return vr->fib4;
	case MLXSW_SP_L3_PROTO_IPV6:
		return vr->fib6;
	}
	return NULL;
}

static struct mlxsw_sp_vr *mlxsw_sp_vr_create(struct mlxsw_sp *mlxsw_sp,
					      u32 tb_id,
					      struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_mr_table *mr4_table, *mr6_table;
	struct mlxsw_sp_fib *fib4;
	struct mlxsw_sp_fib *fib6;
	struct mlxsw_sp_vr *vr;
	int err;

	vr = mlxsw_sp_vr_find_unused(mlxsw_sp);
	if (!vr) {
		NL_SET_ERR_MSG_MOD(extack, "Exceeded number of supported virtual routers");
		return ERR_PTR(-EBUSY);
	}
	fib4 = mlxsw_sp_fib_create(mlxsw_sp, vr, MLXSW_SP_L3_PROTO_IPV4);
	if (IS_ERR(fib4))
		return ERR_CAST(fib4);
	fib6 = mlxsw_sp_fib_create(mlxsw_sp, vr, MLXSW_SP_L3_PROTO_IPV6);
	if (IS_ERR(fib6)) {
		err = PTR_ERR(fib6);
		goto err_fib6_create;
	}
	mr4_table = mlxsw_sp_mr_table_create(mlxsw_sp, vr->id,
					     MLXSW_SP_L3_PROTO_IPV4);
	if (IS_ERR(mr4_table)) {
		err = PTR_ERR(mr4_table);
		goto err_mr4_table_create;
	}
	mr6_table = mlxsw_sp_mr_table_create(mlxsw_sp, vr->id,
					     MLXSW_SP_L3_PROTO_IPV6);
	if (IS_ERR(mr6_table)) {
		err = PTR_ERR(mr6_table);
		goto err_mr6_table_create;
	}

	vr->fib4 = fib4;
	vr->fib6 = fib6;
	vr->mr_table[MLXSW_SP_L3_PROTO_IPV4] = mr4_table;
	vr->mr_table[MLXSW_SP_L3_PROTO_IPV6] = mr6_table;
	vr->tb_id = tb_id;
	return vr;

err_mr6_table_create:
	mlxsw_sp_mr_table_destroy(mr4_table);
err_mr4_table_create:
	mlxsw_sp_fib_destroy(mlxsw_sp, fib6);
err_fib6_create:
	mlxsw_sp_fib_destroy(mlxsw_sp, fib4);
	return ERR_PTR(err);
}

static void mlxsw_sp_vr_destroy(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_vr *vr)
{
	mlxsw_sp_mr_table_destroy(vr->mr_table[MLXSW_SP_L3_PROTO_IPV6]);
	vr->mr_table[MLXSW_SP_L3_PROTO_IPV6] = NULL;
	mlxsw_sp_mr_table_destroy(vr->mr_table[MLXSW_SP_L3_PROTO_IPV4]);
	vr->mr_table[MLXSW_SP_L3_PROTO_IPV4] = NULL;
	mlxsw_sp_fib_destroy(mlxsw_sp, vr->fib6);
	vr->fib6 = NULL;
	mlxsw_sp_fib_destroy(mlxsw_sp, vr->fib4);
	vr->fib4 = NULL;
}

static struct mlxsw_sp_vr *mlxsw_sp_vr_get(struct mlxsw_sp *mlxsw_sp, u32 tb_id,
					   struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_vr *vr;

	tb_id = mlxsw_sp_fix_tb_id(tb_id);
	vr = mlxsw_sp_vr_find(mlxsw_sp, tb_id);
	if (!vr)
		vr = mlxsw_sp_vr_create(mlxsw_sp, tb_id, extack);
	return vr;
}

static void mlxsw_sp_vr_put(struct mlxsw_sp *mlxsw_sp, struct mlxsw_sp_vr *vr)
{
	if (!vr->rif_count && list_empty(&vr->fib4->node_list) &&
	    list_empty(&vr->fib6->node_list) &&
	    mlxsw_sp_mr_table_empty(vr->mr_table[MLXSW_SP_L3_PROTO_IPV4]) &&
	    mlxsw_sp_mr_table_empty(vr->mr_table[MLXSW_SP_L3_PROTO_IPV6]))
		mlxsw_sp_vr_destroy(mlxsw_sp, vr);
}

static bool
mlxsw_sp_vr_lpm_tree_should_replace(struct mlxsw_sp_vr *vr,
				    enum mlxsw_sp_l3proto proto, u8 tree_id)
{
	struct mlxsw_sp_fib *fib = mlxsw_sp_vr_fib(vr, proto);

	if (!mlxsw_sp_vr_is_used(vr))
		return false;
	if (fib->lpm_tree->id == tree_id)
		return true;
	return false;
}

static int mlxsw_sp_vr_lpm_tree_replace(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib *fib,
					struct mlxsw_sp_lpm_tree *new_tree)
{
	struct mlxsw_sp_lpm_tree *old_tree = fib->lpm_tree;
	int err;

	fib->lpm_tree = new_tree;
	mlxsw_sp_lpm_tree_hold(new_tree);
	err = mlxsw_sp_vr_lpm_tree_bind(mlxsw_sp, fib, new_tree->id);
	if (err)
		goto err_tree_bind;
	mlxsw_sp_lpm_tree_put(mlxsw_sp, old_tree);
	return 0;

err_tree_bind:
	mlxsw_sp_lpm_tree_put(mlxsw_sp, new_tree);
	fib->lpm_tree = old_tree;
	return err;
}

static int mlxsw_sp_vrs_lpm_tree_replace(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_fib *fib,
					 struct mlxsw_sp_lpm_tree *new_tree)
{
	int max_vrs = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_VRS);
	enum mlxsw_sp_l3proto proto = fib->proto;
	struct mlxsw_sp_lpm_tree *old_tree;
	u8 old_id, new_id = new_tree->id;
	struct mlxsw_sp_vr *vr;
	int i, err;

	old_tree = mlxsw_sp->router->lpm.proto_trees[proto];
	old_id = old_tree->id;

	for (i = 0; i < max_vrs; i++) {
		vr = &mlxsw_sp->router->vrs[i];
		if (!mlxsw_sp_vr_lpm_tree_should_replace(vr, proto, old_id))
			continue;
		err = mlxsw_sp_vr_lpm_tree_replace(mlxsw_sp,
						   mlxsw_sp_vr_fib(vr, proto),
						   new_tree);
		if (err)
			goto err_tree_replace;
	}

	memcpy(new_tree->prefix_ref_count, old_tree->prefix_ref_count,
	       sizeof(new_tree->prefix_ref_count));
	mlxsw_sp->router->lpm.proto_trees[proto] = new_tree;
	mlxsw_sp_lpm_tree_put(mlxsw_sp, old_tree);

	return 0;

err_tree_replace:
	for (i--; i >= 0; i--) {
		if (!mlxsw_sp_vr_lpm_tree_should_replace(vr, proto, new_id))
			continue;
		mlxsw_sp_vr_lpm_tree_replace(mlxsw_sp,
					     mlxsw_sp_vr_fib(vr, proto),
					     old_tree);
	}
	return err;
}

static int mlxsw_sp_vrs_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_vr *vr;
	u64 max_vrs;
	int i;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, MAX_VRS))
		return -EIO;

	max_vrs = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_VRS);
	mlxsw_sp->router->vrs = kcalloc(max_vrs, sizeof(struct mlxsw_sp_vr),
					GFP_KERNEL);
	if (!mlxsw_sp->router->vrs)
		return -ENOMEM;

	for (i = 0; i < max_vrs; i++) {
		vr = &mlxsw_sp->router->vrs[i];
		vr->id = i;
	}

	return 0;
}

static void mlxsw_sp_router_fib_flush(struct mlxsw_sp *mlxsw_sp);

static void mlxsw_sp_vrs_fini(struct mlxsw_sp *mlxsw_sp)
{
	/* At this stage we're guaranteed not to have new incoming
	 * FIB notifications and the work queue is free from FIBs
	 * sitting on top of mlxsw netdevs. However, we can still
	 * have other FIBs queued. Flush the queue before flushing
	 * the device's tables. No need for locks, as we're the only
	 * writer.
	 */
	mlxsw_core_flush_owq();
	mlxsw_sp_router_fib_flush(mlxsw_sp);
	kfree(mlxsw_sp->router->vrs);
}

u32 mlxsw_sp_ipip_dev_ul_tb_id(const struct net_device *ol_dev)
{
	struct net_device *d;
	u32 tb_id;

	rcu_read_lock();
	d = mlxsw_sp_ipip_netdev_ul_dev_get(ol_dev);
	if (d)
		tb_id = l3mdev_fib_table(d) ? : RT_TABLE_MAIN;
	else
		tb_id = RT_TABLE_MAIN;
	rcu_read_unlock();

	return tb_id;
}

static void
mlxsw_sp_crif_init(struct mlxsw_sp_crif *crif, struct net_device *dev)
{
	crif->key.dev = dev;
	INIT_LIST_HEAD(&crif->nexthop_list);
}

static struct mlxsw_sp_crif *
mlxsw_sp_crif_alloc(struct net_device *dev)
{
	struct mlxsw_sp_crif *crif;

	crif = kzalloc(sizeof(*crif), GFP_KERNEL);
	if (!crif)
		return NULL;

	mlxsw_sp_crif_init(crif, dev);
	return crif;
}

static void mlxsw_sp_crif_free(struct mlxsw_sp_crif *crif)
{
	if (WARN_ON(crif->rif))
		return;

	WARN_ON(!list_empty(&crif->nexthop_list));
	kfree(crif);
}

static int mlxsw_sp_crif_insert(struct mlxsw_sp_router *router,
				struct mlxsw_sp_crif *crif)
{
	return rhashtable_insert_fast(&router->crif_ht, &crif->ht_node,
				      mlxsw_sp_crif_ht_params);
}

static void mlxsw_sp_crif_remove(struct mlxsw_sp_router *router,
				 struct mlxsw_sp_crif *crif)
{
	rhashtable_remove_fast(&router->crif_ht, &crif->ht_node,
			       mlxsw_sp_crif_ht_params);
}

static struct mlxsw_sp_crif *
mlxsw_sp_crif_lookup(struct mlxsw_sp_router *router,
		     const struct net_device *dev)
{
	struct mlxsw_sp_crif_key key = {
		.dev = (struct net_device *)dev,
	};

	return rhashtable_lookup_fast(&router->crif_ht, &key,
				      mlxsw_sp_crif_ht_params);
}

static struct mlxsw_sp_rif *
mlxsw_sp_rif_create(struct mlxsw_sp *mlxsw_sp,
		    const struct mlxsw_sp_rif_params *params,
		    struct netlink_ext_ack *extack);

static struct mlxsw_sp_rif_ipip_lb *
mlxsw_sp_ipip_ol_ipip_lb_create(struct mlxsw_sp *mlxsw_sp,
				enum mlxsw_sp_ipip_type ipipt,
				struct net_device *ol_dev,
				struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_rif_params_ipip_lb lb_params;
	const struct mlxsw_sp_ipip_ops *ipip_ops;
	struct mlxsw_sp_rif *rif;

	ipip_ops = mlxsw_sp->router->ipip_ops_arr[ipipt];
	lb_params = (struct mlxsw_sp_rif_params_ipip_lb) {
		.common.dev = ol_dev,
		.common.lag = false,
		.common.double_entry = ipip_ops->double_rif_entry,
		.lb_config = ipip_ops->ol_loopback_config(mlxsw_sp, ol_dev),
	};

	rif = mlxsw_sp_rif_create(mlxsw_sp, &lb_params.common, extack);
	if (IS_ERR(rif))
		return ERR_CAST(rif);
	return container_of(rif, struct mlxsw_sp_rif_ipip_lb, common);
}

static struct mlxsw_sp_ipip_entry *
mlxsw_sp_ipip_entry_alloc(struct mlxsw_sp *mlxsw_sp,
			  enum mlxsw_sp_ipip_type ipipt,
			  struct net_device *ol_dev)
{
	const struct mlxsw_sp_ipip_ops *ipip_ops;
	struct mlxsw_sp_ipip_entry *ipip_entry;
	struct mlxsw_sp_ipip_entry *ret = NULL;
	int err;

	ipip_ops = mlxsw_sp->router->ipip_ops_arr[ipipt];
	ipip_entry = kzalloc(sizeof(*ipip_entry), GFP_KERNEL);
	if (!ipip_entry)
		return ERR_PTR(-ENOMEM);

	ipip_entry->ol_lb = mlxsw_sp_ipip_ol_ipip_lb_create(mlxsw_sp, ipipt,
							    ol_dev, NULL);
	if (IS_ERR(ipip_entry->ol_lb)) {
		ret = ERR_CAST(ipip_entry->ol_lb);
		goto err_ol_ipip_lb_create;
	}

	ipip_entry->ipipt = ipipt;
	ipip_entry->ol_dev = ol_dev;
	ipip_entry->parms = ipip_ops->parms_init(ol_dev);

	err = ipip_ops->rem_ip_addr_set(mlxsw_sp, ipip_entry);
	if (err) {
		ret = ERR_PTR(err);
		goto err_rem_ip_addr_set;
	}

	return ipip_entry;

err_rem_ip_addr_set:
	mlxsw_sp_rif_destroy(&ipip_entry->ol_lb->common);
err_ol_ipip_lb_create:
	kfree(ipip_entry);
	return ret;
}

static void mlxsw_sp_ipip_entry_dealloc(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_ipip_entry *ipip_entry)
{
	const struct mlxsw_sp_ipip_ops *ipip_ops =
		mlxsw_sp->router->ipip_ops_arr[ipip_entry->ipipt];

	ipip_ops->rem_ip_addr_unset(mlxsw_sp, ipip_entry);
	mlxsw_sp_rif_destroy(&ipip_entry->ol_lb->common);
	kfree(ipip_entry);
}

static bool
mlxsw_sp_ipip_entry_saddr_matches(struct mlxsw_sp *mlxsw_sp,
				  const enum mlxsw_sp_l3proto ul_proto,
				  union mlxsw_sp_l3addr saddr,
				  u32 ul_tb_id,
				  struct mlxsw_sp_ipip_entry *ipip_entry)
{
	u32 tun_ul_tb_id = mlxsw_sp_ipip_dev_ul_tb_id(ipip_entry->ol_dev);
	enum mlxsw_sp_ipip_type ipipt = ipip_entry->ipipt;
	union mlxsw_sp_l3addr tun_saddr;

	if (mlxsw_sp->router->ipip_ops_arr[ipipt]->ul_proto != ul_proto)
		return false;

	tun_saddr = mlxsw_sp_ipip_netdev_saddr(ul_proto, ipip_entry->ol_dev);
	return tun_ul_tb_id == ul_tb_id &&
	       mlxsw_sp_l3addr_eq(&tun_saddr, &saddr);
}

static int mlxsw_sp_ipip_decap_parsing_depth_inc(struct mlxsw_sp *mlxsw_sp,
						 enum mlxsw_sp_ipip_type ipipt)
{
	const struct mlxsw_sp_ipip_ops *ipip_ops;

	ipip_ops = mlxsw_sp->router->ipip_ops_arr[ipipt];

	/* Not all tunnels require to increase the default pasing depth
	 * (96 bytes).
	 */
	if (ipip_ops->inc_parsing_depth)
		return mlxsw_sp_parsing_depth_inc(mlxsw_sp);

	return 0;
}

static void mlxsw_sp_ipip_decap_parsing_depth_dec(struct mlxsw_sp *mlxsw_sp,
						  enum mlxsw_sp_ipip_type ipipt)
{
	const struct mlxsw_sp_ipip_ops *ipip_ops =
		mlxsw_sp->router->ipip_ops_arr[ipipt];

	if (ipip_ops->inc_parsing_depth)
		mlxsw_sp_parsing_depth_dec(mlxsw_sp);
}

static int
mlxsw_sp_fib_entry_decap_init(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_fib_entry *fib_entry,
			      struct mlxsw_sp_ipip_entry *ipip_entry)
{
	u32 tunnel_index;
	int err;

	err = mlxsw_sp_kvdl_alloc(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_ADJ,
				  1, &tunnel_index);
	if (err)
		return err;

	err = mlxsw_sp_ipip_decap_parsing_depth_inc(mlxsw_sp,
						    ipip_entry->ipipt);
	if (err)
		goto err_parsing_depth_inc;

	ipip_entry->decap_fib_entry = fib_entry;
	fib_entry->decap.ipip_entry = ipip_entry;
	fib_entry->decap.tunnel_index = tunnel_index;

	return 0;

err_parsing_depth_inc:
	mlxsw_sp_kvdl_free(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_ADJ, 1,
			   fib_entry->decap.tunnel_index);
	return err;
}

static void mlxsw_sp_fib_entry_decap_fini(struct mlxsw_sp *mlxsw_sp,
					  struct mlxsw_sp_fib_entry *fib_entry)
{
	enum mlxsw_sp_ipip_type ipipt = fib_entry->decap.ipip_entry->ipipt;

	/* Unlink this node from the IPIP entry that it's the decap entry of. */
	fib_entry->decap.ipip_entry->decap_fib_entry = NULL;
	fib_entry->decap.ipip_entry = NULL;
	mlxsw_sp_ipip_decap_parsing_depth_dec(mlxsw_sp, ipipt);
	mlxsw_sp_kvdl_free(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_ADJ,
			   1, fib_entry->decap.tunnel_index);
}

static struct mlxsw_sp_fib_node *
mlxsw_sp_fib_node_lookup(struct mlxsw_sp_fib *fib, const void *addr,
			 size_t addr_len, unsigned char prefix_len);
static int mlxsw_sp_fib_entry_update(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_fib_entry *fib_entry);

static void
mlxsw_sp_ipip_entry_demote_decap(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_ipip_entry *ipip_entry)
{
	struct mlxsw_sp_fib_entry *fib_entry = ipip_entry->decap_fib_entry;

	mlxsw_sp_fib_entry_decap_fini(mlxsw_sp, fib_entry);
	fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_TRAP;

	mlxsw_sp_fib_entry_update(mlxsw_sp, fib_entry);
}

static void
mlxsw_sp_ipip_entry_promote_decap(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_ipip_entry *ipip_entry,
				  struct mlxsw_sp_fib_entry *decap_fib_entry)
{
	if (mlxsw_sp_fib_entry_decap_init(mlxsw_sp, decap_fib_entry,
					  ipip_entry))
		return;
	decap_fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_IPIP_DECAP;

	if (mlxsw_sp_fib_entry_update(mlxsw_sp, decap_fib_entry))
		mlxsw_sp_ipip_entry_demote_decap(mlxsw_sp, ipip_entry);
}

static struct mlxsw_sp_fib_entry *
mlxsw_sp_router_ip2me_fib_entry_find(struct mlxsw_sp *mlxsw_sp, u32 tb_id,
				     enum mlxsw_sp_l3proto proto,
				     const union mlxsw_sp_l3addr *addr,
				     enum mlxsw_sp_fib_entry_type type)
{
	struct mlxsw_sp_fib_node *fib_node;
	unsigned char addr_prefix_len;
	struct mlxsw_sp_fib *fib;
	struct mlxsw_sp_vr *vr;
	const void *addrp;
	size_t addr_len;
	u32 addr4;

	vr = mlxsw_sp_vr_find(mlxsw_sp, tb_id);
	if (!vr)
		return NULL;
	fib = mlxsw_sp_vr_fib(vr, proto);

	switch (proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		addr4 = be32_to_cpu(addr->addr4);
		addrp = &addr4;
		addr_len = 4;
		addr_prefix_len = 32;
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		addrp = &addr->addr6;
		addr_len = 16;
		addr_prefix_len = 128;
		break;
	default:
		WARN_ON(1);
		return NULL;
	}

	fib_node = mlxsw_sp_fib_node_lookup(fib, addrp, addr_len,
					    addr_prefix_len);
	if (!fib_node || fib_node->fib_entry->type != type)
		return NULL;

	return fib_node->fib_entry;
}

/* Given an IPIP entry, find the corresponding decap route. */
static struct mlxsw_sp_fib_entry *
mlxsw_sp_ipip_entry_find_decap(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_ipip_entry *ipip_entry)
{
	static struct mlxsw_sp_fib_node *fib_node;
	const struct mlxsw_sp_ipip_ops *ipip_ops;
	unsigned char saddr_prefix_len;
	union mlxsw_sp_l3addr saddr;
	struct mlxsw_sp_fib *ul_fib;
	struct mlxsw_sp_vr *ul_vr;
	const void *saddrp;
	size_t saddr_len;
	u32 ul_tb_id;
	u32 saddr4;

	ipip_ops = mlxsw_sp->router->ipip_ops_arr[ipip_entry->ipipt];

	ul_tb_id = mlxsw_sp_ipip_dev_ul_tb_id(ipip_entry->ol_dev);
	ul_vr = mlxsw_sp_vr_find(mlxsw_sp, ul_tb_id);
	if (!ul_vr)
		return NULL;

	ul_fib = mlxsw_sp_vr_fib(ul_vr, ipip_ops->ul_proto);
	saddr = mlxsw_sp_ipip_netdev_saddr(ipip_ops->ul_proto,
					   ipip_entry->ol_dev);

	switch (ipip_ops->ul_proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		saddr4 = be32_to_cpu(saddr.addr4);
		saddrp = &saddr4;
		saddr_len = 4;
		saddr_prefix_len = 32;
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		saddrp = &saddr.addr6;
		saddr_len = 16;
		saddr_prefix_len = 128;
		break;
	default:
		WARN_ON(1);
		return NULL;
	}

	fib_node = mlxsw_sp_fib_node_lookup(ul_fib, saddrp, saddr_len,
					    saddr_prefix_len);
	if (!fib_node ||
	    fib_node->fib_entry->type != MLXSW_SP_FIB_ENTRY_TYPE_TRAP)
		return NULL;

	return fib_node->fib_entry;
}

static struct mlxsw_sp_ipip_entry *
mlxsw_sp_ipip_entry_create(struct mlxsw_sp *mlxsw_sp,
			   enum mlxsw_sp_ipip_type ipipt,
			   struct net_device *ol_dev)
{
	struct mlxsw_sp_ipip_entry *ipip_entry;

	ipip_entry = mlxsw_sp_ipip_entry_alloc(mlxsw_sp, ipipt, ol_dev);
	if (IS_ERR(ipip_entry))
		return ipip_entry;

	list_add_tail(&ipip_entry->ipip_list_node,
		      &mlxsw_sp->router->ipip_list);

	return ipip_entry;
}

static void
mlxsw_sp_ipip_entry_destroy(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_ipip_entry *ipip_entry)
{
	list_del(&ipip_entry->ipip_list_node);
	mlxsw_sp_ipip_entry_dealloc(mlxsw_sp, ipip_entry);
}

static bool
mlxsw_sp_ipip_entry_matches_decap(struct mlxsw_sp *mlxsw_sp,
				  const struct net_device *ul_dev,
				  enum mlxsw_sp_l3proto ul_proto,
				  union mlxsw_sp_l3addr ul_dip,
				  struct mlxsw_sp_ipip_entry *ipip_entry)
{
	u32 ul_tb_id = l3mdev_fib_table(ul_dev) ? : RT_TABLE_MAIN;
	enum mlxsw_sp_ipip_type ipipt = ipip_entry->ipipt;

	if (mlxsw_sp->router->ipip_ops_arr[ipipt]->ul_proto != ul_proto)
		return false;

	return mlxsw_sp_ipip_entry_saddr_matches(mlxsw_sp, ul_proto, ul_dip,
						 ul_tb_id, ipip_entry);
}

/* Given decap parameters, find the corresponding IPIP entry. */
static struct mlxsw_sp_ipip_entry *
mlxsw_sp_ipip_entry_find_by_decap(struct mlxsw_sp *mlxsw_sp, int ul_dev_ifindex,
				  enum mlxsw_sp_l3proto ul_proto,
				  union mlxsw_sp_l3addr ul_dip)
{
	struct mlxsw_sp_ipip_entry *ipip_entry = NULL;
	struct net_device *ul_dev;

	rcu_read_lock();

	ul_dev = dev_get_by_index_rcu(mlxsw_sp_net(mlxsw_sp), ul_dev_ifindex);
	if (!ul_dev)
		goto out_unlock;

	list_for_each_entry(ipip_entry, &mlxsw_sp->router->ipip_list,
			    ipip_list_node)
		if (mlxsw_sp_ipip_entry_matches_decap(mlxsw_sp, ul_dev,
						      ul_proto, ul_dip,
						      ipip_entry))
			goto out_unlock;

	rcu_read_unlock();

	return NULL;

out_unlock:
	rcu_read_unlock();
	return ipip_entry;
}

static bool mlxsw_sp_netdev_ipip_type(const struct mlxsw_sp *mlxsw_sp,
				      const struct net_device *dev,
				      enum mlxsw_sp_ipip_type *p_type)
{
	struct mlxsw_sp_router *router = mlxsw_sp->router;
	const struct mlxsw_sp_ipip_ops *ipip_ops;
	enum mlxsw_sp_ipip_type ipipt;

	for (ipipt = 0; ipipt < MLXSW_SP_IPIP_TYPE_MAX; ++ipipt) {
		ipip_ops = router->ipip_ops_arr[ipipt];
		if (dev->type == ipip_ops->dev_type) {
			if (p_type)
				*p_type = ipipt;
			return true;
		}
	}
	return false;
}

static bool mlxsw_sp_netdev_is_ipip_ol(const struct mlxsw_sp *mlxsw_sp,
				       const struct net_device *dev)
{
	return mlxsw_sp_netdev_ipip_type(mlxsw_sp, dev, NULL);
}

static struct mlxsw_sp_ipip_entry *
mlxsw_sp_ipip_entry_find_by_ol_dev(struct mlxsw_sp *mlxsw_sp,
				   const struct net_device *ol_dev)
{
	struct mlxsw_sp_ipip_entry *ipip_entry;

	list_for_each_entry(ipip_entry, &mlxsw_sp->router->ipip_list,
			    ipip_list_node)
		if (ipip_entry->ol_dev == ol_dev)
			return ipip_entry;

	return NULL;
}

static struct mlxsw_sp_ipip_entry *
mlxsw_sp_ipip_entry_find_by_ul_dev(const struct mlxsw_sp *mlxsw_sp,
				   const struct net_device *ul_dev,
				   struct mlxsw_sp_ipip_entry *start)
{
	struct mlxsw_sp_ipip_entry *ipip_entry;

	ipip_entry = list_prepare_entry(start, &mlxsw_sp->router->ipip_list,
					ipip_list_node);
	list_for_each_entry_continue(ipip_entry, &mlxsw_sp->router->ipip_list,
				     ipip_list_node) {
		struct net_device *ol_dev = ipip_entry->ol_dev;
		struct net_device *ipip_ul_dev;

		rcu_read_lock();
		ipip_ul_dev = mlxsw_sp_ipip_netdev_ul_dev_get(ol_dev);
		rcu_read_unlock();

		if (ipip_ul_dev == ul_dev)
			return ipip_entry;
	}

	return NULL;
}

static bool mlxsw_sp_netdev_is_ipip_ul(struct mlxsw_sp *mlxsw_sp,
				       const struct net_device *dev)
{
	return mlxsw_sp_ipip_entry_find_by_ul_dev(mlxsw_sp, dev, NULL);
}

static bool mlxsw_sp_netdevice_ipip_can_offload(struct mlxsw_sp *mlxsw_sp,
						const struct net_device *ol_dev,
						enum mlxsw_sp_ipip_type ipipt)
{
	const struct mlxsw_sp_ipip_ops *ops
		= mlxsw_sp->router->ipip_ops_arr[ipipt];

	return ops->can_offload(mlxsw_sp, ol_dev);
}

static int mlxsw_sp_netdevice_ipip_ol_reg_event(struct mlxsw_sp *mlxsw_sp,
						struct net_device *ol_dev)
{
	enum mlxsw_sp_ipip_type ipipt = MLXSW_SP_IPIP_TYPE_MAX;
	struct mlxsw_sp_ipip_entry *ipip_entry;
	enum mlxsw_sp_l3proto ul_proto;
	union mlxsw_sp_l3addr saddr;
	u32 ul_tb_id;

	mlxsw_sp_netdev_ipip_type(mlxsw_sp, ol_dev, &ipipt);
	if (mlxsw_sp_netdevice_ipip_can_offload(mlxsw_sp, ol_dev, ipipt)) {
		ul_tb_id = mlxsw_sp_ipip_dev_ul_tb_id(ol_dev);
		ul_proto = mlxsw_sp->router->ipip_ops_arr[ipipt]->ul_proto;
		saddr = mlxsw_sp_ipip_netdev_saddr(ul_proto, ol_dev);
		if (!mlxsw_sp_ipip_demote_tunnel_by_saddr(mlxsw_sp, ul_proto,
							  saddr, ul_tb_id,
							  NULL)) {
			ipip_entry = mlxsw_sp_ipip_entry_create(mlxsw_sp, ipipt,
								ol_dev);
			if (IS_ERR(ipip_entry))
				return PTR_ERR(ipip_entry);
		}
	}

	return 0;
}

static void mlxsw_sp_netdevice_ipip_ol_unreg_event(struct mlxsw_sp *mlxsw_sp,
						   struct net_device *ol_dev)
{
	struct mlxsw_sp_ipip_entry *ipip_entry;

	ipip_entry = mlxsw_sp_ipip_entry_find_by_ol_dev(mlxsw_sp, ol_dev);
	if (ipip_entry)
		mlxsw_sp_ipip_entry_destroy(mlxsw_sp, ipip_entry);
}

static void
mlxsw_sp_ipip_entry_ol_up_event(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_ipip_entry *ipip_entry)
{
	struct mlxsw_sp_fib_entry *decap_fib_entry;

	decap_fib_entry = mlxsw_sp_ipip_entry_find_decap(mlxsw_sp, ipip_entry);
	if (decap_fib_entry)
		mlxsw_sp_ipip_entry_promote_decap(mlxsw_sp, ipip_entry,
						  decap_fib_entry);
}

static int
mlxsw_sp_rif_ipip_lb_op(struct mlxsw_sp_rif_ipip_lb *lb_rif, u16 ul_vr_id,
			u16 ul_rif_id, bool enable)
{
	struct mlxsw_sp_rif_ipip_lb_config lb_cf = lb_rif->lb_config;
	struct net_device *dev = mlxsw_sp_rif_dev(&lb_rif->common);
	enum mlxsw_reg_ritr_loopback_ipip_options ipip_options;
	struct mlxsw_sp_rif *rif = &lb_rif->common;
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	char ritr_pl[MLXSW_REG_RITR_LEN];
	struct in6_addr *saddr6;
	u32 saddr4;

	ipip_options = MLXSW_REG_RITR_LOOPBACK_IPIP_OPTIONS_GRE_KEY_PRESET;
	switch (lb_cf.ul_protocol) {
	case MLXSW_SP_L3_PROTO_IPV4:
		saddr4 = be32_to_cpu(lb_cf.saddr.addr4);
		mlxsw_reg_ritr_pack(ritr_pl, enable, MLXSW_REG_RITR_LOOPBACK_IF,
				    rif->rif_index, rif->vr_id, dev->mtu);
		mlxsw_reg_ritr_loopback_ipip4_pack(ritr_pl, lb_cf.lb_ipipt,
						   ipip_options, ul_vr_id,
						   ul_rif_id, saddr4,
						   lb_cf.okey);
		break;

	case MLXSW_SP_L3_PROTO_IPV6:
		saddr6 = &lb_cf.saddr.addr6;
		mlxsw_reg_ritr_pack(ritr_pl, enable, MLXSW_REG_RITR_LOOPBACK_IF,
				    rif->rif_index, rif->vr_id, dev->mtu);
		mlxsw_reg_ritr_loopback_ipip6_pack(ritr_pl, lb_cf.lb_ipipt,
						   ipip_options, ul_vr_id,
						   ul_rif_id, saddr6,
						   lb_cf.okey);
		break;
	}

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
}

static int mlxsw_sp_netdevice_ipip_ol_update_mtu(struct mlxsw_sp *mlxsw_sp,
						 struct net_device *ol_dev)
{
	struct mlxsw_sp_ipip_entry *ipip_entry;
	struct mlxsw_sp_rif_ipip_lb *lb_rif;
	int err = 0;

	ipip_entry = mlxsw_sp_ipip_entry_find_by_ol_dev(mlxsw_sp, ol_dev);
	if (ipip_entry) {
		lb_rif = ipip_entry->ol_lb;
		err = mlxsw_sp_rif_ipip_lb_op(lb_rif, lb_rif->ul_vr_id,
					      lb_rif->ul_rif_id, true);
		if (err)
			goto out;
		lb_rif->common.mtu = ol_dev->mtu;
	}

out:
	return err;
}

static void mlxsw_sp_netdevice_ipip_ol_up_event(struct mlxsw_sp *mlxsw_sp,
						struct net_device *ol_dev)
{
	struct mlxsw_sp_ipip_entry *ipip_entry;

	ipip_entry = mlxsw_sp_ipip_entry_find_by_ol_dev(mlxsw_sp, ol_dev);
	if (ipip_entry)
		mlxsw_sp_ipip_entry_ol_up_event(mlxsw_sp, ipip_entry);
}

static void
mlxsw_sp_ipip_entry_ol_down_event(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_ipip_entry *ipip_entry)
{
	if (ipip_entry->decap_fib_entry)
		mlxsw_sp_ipip_entry_demote_decap(mlxsw_sp, ipip_entry);
}

static void mlxsw_sp_netdevice_ipip_ol_down_event(struct mlxsw_sp *mlxsw_sp,
						  struct net_device *ol_dev)
{
	struct mlxsw_sp_ipip_entry *ipip_entry;

	ipip_entry = mlxsw_sp_ipip_entry_find_by_ol_dev(mlxsw_sp, ol_dev);
	if (ipip_entry)
		mlxsw_sp_ipip_entry_ol_down_event(mlxsw_sp, ipip_entry);
}

static void mlxsw_sp_nexthop_rif_update(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_rif *rif);

static void mlxsw_sp_rif_migrate_destroy(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_rif *old_rif,
					 struct mlxsw_sp_rif *new_rif,
					 bool migrate_nhs)
{
	struct mlxsw_sp_crif *crif = old_rif->crif;
	struct mlxsw_sp_crif mock_crif = {};

	if (migrate_nhs)
		mlxsw_sp_nexthop_rif_update(mlxsw_sp, new_rif);

	/* Plant a mock CRIF so that destroying the old RIF doesn't unoffload
	 * our nexthops and IPIP tunnels, and doesn't sever the crif->rif link.
	 */
	mlxsw_sp_crif_init(&mock_crif, crif->key.dev);
	old_rif->crif = &mock_crif;
	mock_crif.rif = old_rif;
	mlxsw_sp_rif_destroy(old_rif);
}

static int
mlxsw_sp_ipip_entry_ol_lb_update(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_ipip_entry *ipip_entry,
				 bool keep_encap,
				 struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_rif_ipip_lb *old_lb_rif = ipip_entry->ol_lb;
	struct mlxsw_sp_rif_ipip_lb *new_lb_rif;

	new_lb_rif = mlxsw_sp_ipip_ol_ipip_lb_create(mlxsw_sp,
						     ipip_entry->ipipt,
						     ipip_entry->ol_dev,
						     extack);
	if (IS_ERR(new_lb_rif))
		return PTR_ERR(new_lb_rif);
	ipip_entry->ol_lb = new_lb_rif;

	mlxsw_sp_rif_migrate_destroy(mlxsw_sp, &old_lb_rif->common,
				     &new_lb_rif->common, keep_encap);
	return 0;
}

/**
 * __mlxsw_sp_ipip_entry_update_tunnel - Update offload related to IPIP entry.
 * @mlxsw_sp: mlxsw_sp.
 * @ipip_entry: IPIP entry.
 * @recreate_loopback: Recreates the associated loopback RIF.
 * @keep_encap: Updates next hops that use the tunnel netdevice. This is only
 *              relevant when recreate_loopback is true.
 * @update_nexthops: Updates next hops, keeping the current loopback RIF. This
 *                   is only relevant when recreate_loopback is false.
 * @extack: extack.
 *
 * Return: Non-zero value on failure.
 */
int __mlxsw_sp_ipip_entry_update_tunnel(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_ipip_entry *ipip_entry,
					bool recreate_loopback,
					bool keep_encap,
					bool update_nexthops,
					struct netlink_ext_ack *extack)
{
	int err;

	/* RIFs can't be edited, so to update loopback, we need to destroy and
	 * recreate it. That creates a window of opportunity where RALUE and
	 * RATR registers end up referencing a RIF that's already gone. RATRs
	 * are handled in mlxsw_sp_ipip_entry_ol_lb_update(), and to take care
	 * of RALUE, demote the decap route back.
	 */
	if (ipip_entry->decap_fib_entry)
		mlxsw_sp_ipip_entry_demote_decap(mlxsw_sp, ipip_entry);

	if (recreate_loopback) {
		err = mlxsw_sp_ipip_entry_ol_lb_update(mlxsw_sp, ipip_entry,
						       keep_encap, extack);
		if (err)
			return err;
	} else if (update_nexthops) {
		mlxsw_sp_nexthop_rif_update(mlxsw_sp,
					    &ipip_entry->ol_lb->common);
	}

	if (ipip_entry->ol_dev->flags & IFF_UP)
		mlxsw_sp_ipip_entry_ol_up_event(mlxsw_sp, ipip_entry);

	return 0;
}

static int mlxsw_sp_netdevice_ipip_ol_vrf_event(struct mlxsw_sp *mlxsw_sp,
						struct net_device *ol_dev,
						struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_ipip_entry *ipip_entry =
		mlxsw_sp_ipip_entry_find_by_ol_dev(mlxsw_sp, ol_dev);

	if (!ipip_entry)
		return 0;

	return __mlxsw_sp_ipip_entry_update_tunnel(mlxsw_sp, ipip_entry,
						   true, false, false, extack);
}

static int
mlxsw_sp_netdevice_ipip_ul_vrf_event(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_ipip_entry *ipip_entry,
				     struct net_device *ul_dev,
				     bool *demote_this,
				     struct netlink_ext_ack *extack)
{
	u32 ul_tb_id = l3mdev_fib_table(ul_dev) ? : RT_TABLE_MAIN;
	enum mlxsw_sp_l3proto ul_proto;
	union mlxsw_sp_l3addr saddr;

	/* Moving underlay to a different VRF might cause local address
	 * conflict, and the conflicting tunnels need to be demoted.
	 */
	ul_proto = mlxsw_sp->router->ipip_ops_arr[ipip_entry->ipipt]->ul_proto;
	saddr = mlxsw_sp_ipip_netdev_saddr(ul_proto, ipip_entry->ol_dev);
	if (mlxsw_sp_ipip_demote_tunnel_by_saddr(mlxsw_sp, ul_proto,
						 saddr, ul_tb_id,
						 ipip_entry)) {
		*demote_this = true;
		return 0;
	}

	return __mlxsw_sp_ipip_entry_update_tunnel(mlxsw_sp, ipip_entry,
						   true, true, false, extack);
}

static int
mlxsw_sp_netdevice_ipip_ul_up_event(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_ipip_entry *ipip_entry,
				    struct net_device *ul_dev)
{
	return __mlxsw_sp_ipip_entry_update_tunnel(mlxsw_sp, ipip_entry,
						   false, false, true, NULL);
}

static int
mlxsw_sp_netdevice_ipip_ul_down_event(struct mlxsw_sp *mlxsw_sp,
				      struct mlxsw_sp_ipip_entry *ipip_entry,
				      struct net_device *ul_dev)
{
	/* A down underlay device causes encapsulated packets to not be
	 * forwarded, but decap still works. So refresh next hops without
	 * touching anything else.
	 */
	return __mlxsw_sp_ipip_entry_update_tunnel(mlxsw_sp, ipip_entry,
						   false, false, true, NULL);
}

static int
mlxsw_sp_netdevice_ipip_ol_change_event(struct mlxsw_sp *mlxsw_sp,
					struct net_device *ol_dev,
					struct netlink_ext_ack *extack)
{
	const struct mlxsw_sp_ipip_ops *ipip_ops;
	struct mlxsw_sp_ipip_entry *ipip_entry;
	int err;

	ipip_entry = mlxsw_sp_ipip_entry_find_by_ol_dev(mlxsw_sp, ol_dev);
	if (!ipip_entry)
		/* A change might make a tunnel eligible for offloading, but
		 * that is currently not implemented. What falls to slow path
		 * stays there.
		 */
		return 0;

	/* A change might make a tunnel not eligible for offloading. */
	if (!mlxsw_sp_netdevice_ipip_can_offload(mlxsw_sp, ol_dev,
						 ipip_entry->ipipt)) {
		mlxsw_sp_ipip_entry_demote_tunnel(mlxsw_sp, ipip_entry);
		return 0;
	}

	ipip_ops = mlxsw_sp->router->ipip_ops_arr[ipip_entry->ipipt];
	err = ipip_ops->ol_netdev_change(mlxsw_sp, ipip_entry, extack);
	return err;
}

void mlxsw_sp_ipip_entry_demote_tunnel(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_ipip_entry *ipip_entry)
{
	struct net_device *ol_dev = ipip_entry->ol_dev;

	if (ol_dev->flags & IFF_UP)
		mlxsw_sp_ipip_entry_ol_down_event(mlxsw_sp, ipip_entry);
	mlxsw_sp_ipip_entry_destroy(mlxsw_sp, ipip_entry);
}

/* The configuration where several tunnels have the same local address in the
 * same underlay table needs special treatment in the HW. That is currently not
 * implemented in the driver. This function finds and demotes the first tunnel
 * with a given source address, except the one passed in the argument
 * `except'.
 */
bool
mlxsw_sp_ipip_demote_tunnel_by_saddr(struct mlxsw_sp *mlxsw_sp,
				     enum mlxsw_sp_l3proto ul_proto,
				     union mlxsw_sp_l3addr saddr,
				     u32 ul_tb_id,
				     const struct mlxsw_sp_ipip_entry *except)
{
	struct mlxsw_sp_ipip_entry *ipip_entry, *tmp;

	list_for_each_entry_safe(ipip_entry, tmp, &mlxsw_sp->router->ipip_list,
				 ipip_list_node) {
		if (ipip_entry != except &&
		    mlxsw_sp_ipip_entry_saddr_matches(mlxsw_sp, ul_proto, saddr,
						      ul_tb_id, ipip_entry)) {
			mlxsw_sp_ipip_entry_demote_tunnel(mlxsw_sp, ipip_entry);
			return true;
		}
	}

	return false;
}

static void mlxsw_sp_ipip_demote_tunnel_by_ul_netdev(struct mlxsw_sp *mlxsw_sp,
						     struct net_device *ul_dev)
{
	struct mlxsw_sp_ipip_entry *ipip_entry, *tmp;

	list_for_each_entry_safe(ipip_entry, tmp, &mlxsw_sp->router->ipip_list,
				 ipip_list_node) {
		struct net_device *ol_dev = ipip_entry->ol_dev;
		struct net_device *ipip_ul_dev;

		rcu_read_lock();
		ipip_ul_dev = mlxsw_sp_ipip_netdev_ul_dev_get(ol_dev);
		rcu_read_unlock();
		if (ipip_ul_dev == ul_dev)
			mlxsw_sp_ipip_entry_demote_tunnel(mlxsw_sp, ipip_entry);
	}
}

static int mlxsw_sp_netdevice_ipip_ol_event(struct mlxsw_sp *mlxsw_sp,
					    struct net_device *ol_dev,
					    unsigned long event,
					    struct netdev_notifier_info *info)
{
	struct netdev_notifier_changeupper_info *chup;
	struct netlink_ext_ack *extack;
	int err = 0;

	switch (event) {
	case NETDEV_REGISTER:
		err = mlxsw_sp_netdevice_ipip_ol_reg_event(mlxsw_sp, ol_dev);
		break;
	case NETDEV_UNREGISTER:
		mlxsw_sp_netdevice_ipip_ol_unreg_event(mlxsw_sp, ol_dev);
		break;
	case NETDEV_UP:
		mlxsw_sp_netdevice_ipip_ol_up_event(mlxsw_sp, ol_dev);
		break;
	case NETDEV_DOWN:
		mlxsw_sp_netdevice_ipip_ol_down_event(mlxsw_sp, ol_dev);
		break;
	case NETDEV_CHANGEUPPER:
		chup = container_of(info, typeof(*chup), info);
		extack = info->extack;
		if (netif_is_l3_master(chup->upper_dev))
			err = mlxsw_sp_netdevice_ipip_ol_vrf_event(mlxsw_sp,
								   ol_dev,
								   extack);
		break;
	case NETDEV_CHANGE:
		extack = info->extack;
		err = mlxsw_sp_netdevice_ipip_ol_change_event(mlxsw_sp,
							      ol_dev, extack);
		break;
	case NETDEV_CHANGEMTU:
		err = mlxsw_sp_netdevice_ipip_ol_update_mtu(mlxsw_sp, ol_dev);
		break;
	}
	return err;
}

static int
__mlxsw_sp_netdevice_ipip_ul_event(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_ipip_entry *ipip_entry,
				   struct net_device *ul_dev,
				   bool *demote_this,
				   unsigned long event,
				   struct netdev_notifier_info *info)
{
	struct netdev_notifier_changeupper_info *chup;
	struct netlink_ext_ack *extack;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		chup = container_of(info, typeof(*chup), info);
		extack = info->extack;
		if (netif_is_l3_master(chup->upper_dev))
			return mlxsw_sp_netdevice_ipip_ul_vrf_event(mlxsw_sp,
								    ipip_entry,
								    ul_dev,
								    demote_this,
								    extack);
		break;

	case NETDEV_UP:
		return mlxsw_sp_netdevice_ipip_ul_up_event(mlxsw_sp, ipip_entry,
							   ul_dev);
	case NETDEV_DOWN:
		return mlxsw_sp_netdevice_ipip_ul_down_event(mlxsw_sp,
							     ipip_entry,
							     ul_dev);
	}
	return 0;
}

static int
mlxsw_sp_netdevice_ipip_ul_event(struct mlxsw_sp *mlxsw_sp,
				 struct net_device *ul_dev,
				 unsigned long event,
				 struct netdev_notifier_info *info)
{
	struct mlxsw_sp_ipip_entry *ipip_entry = NULL;
	int err;

	while ((ipip_entry = mlxsw_sp_ipip_entry_find_by_ul_dev(mlxsw_sp,
								ul_dev,
								ipip_entry))) {
		struct mlxsw_sp_ipip_entry *prev;
		bool demote_this = false;

		err = __mlxsw_sp_netdevice_ipip_ul_event(mlxsw_sp, ipip_entry,
							 ul_dev, &demote_this,
							 event, info);
		if (err) {
			mlxsw_sp_ipip_demote_tunnel_by_ul_netdev(mlxsw_sp,
								 ul_dev);
			return err;
		}

		if (demote_this) {
			if (list_is_first(&ipip_entry->ipip_list_node,
					  &mlxsw_sp->router->ipip_list))
				prev = NULL;
			else
				/* This can't be cached from previous iteration,
				 * because that entry could be gone now.
				 */
				prev = list_prev_entry(ipip_entry,
						       ipip_list_node);
			mlxsw_sp_ipip_entry_demote_tunnel(mlxsw_sp, ipip_entry);
			ipip_entry = prev;
		}
	}

	return 0;
}

int mlxsw_sp_router_nve_promote_decap(struct mlxsw_sp *mlxsw_sp, u32 ul_tb_id,
				      enum mlxsw_sp_l3proto ul_proto,
				      const union mlxsw_sp_l3addr *ul_sip,
				      u32 tunnel_index)
{
	enum mlxsw_sp_fib_entry_type type = MLXSW_SP_FIB_ENTRY_TYPE_TRAP;
	struct mlxsw_sp_router *router = mlxsw_sp->router;
	struct mlxsw_sp_fib_entry *fib_entry;
	int err = 0;

	mutex_lock(&mlxsw_sp->router->lock);

	if (WARN_ON_ONCE(router->nve_decap_config.valid)) {
		err = -EINVAL;
		goto out;
	}

	router->nve_decap_config.ul_tb_id = ul_tb_id;
	router->nve_decap_config.tunnel_index = tunnel_index;
	router->nve_decap_config.ul_proto = ul_proto;
	router->nve_decap_config.ul_sip = *ul_sip;
	router->nve_decap_config.valid = true;

	/* It is valid to create a tunnel with a local IP and only later
	 * assign this IP address to a local interface
	 */
	fib_entry = mlxsw_sp_router_ip2me_fib_entry_find(mlxsw_sp, ul_tb_id,
							 ul_proto, ul_sip,
							 type);
	if (!fib_entry)
		goto out;

	fib_entry->decap.tunnel_index = tunnel_index;
	fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_NVE_DECAP;

	err = mlxsw_sp_fib_entry_update(mlxsw_sp, fib_entry);
	if (err)
		goto err_fib_entry_update;

	goto out;

err_fib_entry_update:
	fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_TRAP;
	mlxsw_sp_fib_entry_update(mlxsw_sp, fib_entry);
out:
	mutex_unlock(&mlxsw_sp->router->lock);
	return err;
}

void mlxsw_sp_router_nve_demote_decap(struct mlxsw_sp *mlxsw_sp, u32 ul_tb_id,
				      enum mlxsw_sp_l3proto ul_proto,
				      const union mlxsw_sp_l3addr *ul_sip)
{
	enum mlxsw_sp_fib_entry_type type = MLXSW_SP_FIB_ENTRY_TYPE_NVE_DECAP;
	struct mlxsw_sp_router *router = mlxsw_sp->router;
	struct mlxsw_sp_fib_entry *fib_entry;

	mutex_lock(&mlxsw_sp->router->lock);

	if (WARN_ON_ONCE(!router->nve_decap_config.valid))
		goto out;

	router->nve_decap_config.valid = false;

	fib_entry = mlxsw_sp_router_ip2me_fib_entry_find(mlxsw_sp, ul_tb_id,
							 ul_proto, ul_sip,
							 type);
	if (!fib_entry)
		goto out;

	fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_TRAP;
	mlxsw_sp_fib_entry_update(mlxsw_sp, fib_entry);
out:
	mutex_unlock(&mlxsw_sp->router->lock);
}

static bool mlxsw_sp_router_nve_is_decap(struct mlxsw_sp *mlxsw_sp,
					 u32 ul_tb_id,
					 enum mlxsw_sp_l3proto ul_proto,
					 const union mlxsw_sp_l3addr *ul_sip)
{
	struct mlxsw_sp_router *router = mlxsw_sp->router;

	return router->nve_decap_config.valid &&
	       router->nve_decap_config.ul_tb_id == ul_tb_id &&
	       router->nve_decap_config.ul_proto == ul_proto &&
	       !memcmp(&router->nve_decap_config.ul_sip, ul_sip,
		       sizeof(*ul_sip));
}

struct mlxsw_sp_neigh_key {
	struct neighbour *n;
};

struct mlxsw_sp_neigh_entry {
	struct list_head rif_list_node;
	struct rhash_head ht_node;
	struct mlxsw_sp_neigh_key key;
	u16 rif;
	bool connected;
	unsigned char ha[ETH_ALEN];
	struct list_head nexthop_list; /* list of nexthops using
					* this neigh entry
					*/
	struct list_head nexthop_neighs_list_node;
	unsigned int counter_index;
	bool counter_valid;
};

static const struct rhashtable_params mlxsw_sp_neigh_ht_params = {
	.key_offset = offsetof(struct mlxsw_sp_neigh_entry, key),
	.head_offset = offsetof(struct mlxsw_sp_neigh_entry, ht_node),
	.key_len = sizeof(struct mlxsw_sp_neigh_key),
};

struct mlxsw_sp_neigh_entry *
mlxsw_sp_rif_neigh_next(struct mlxsw_sp_rif *rif,
			struct mlxsw_sp_neigh_entry *neigh_entry)
{
	if (!neigh_entry) {
		if (list_empty(&rif->neigh_list))
			return NULL;
		else
			return list_first_entry(&rif->neigh_list,
						typeof(*neigh_entry),
						rif_list_node);
	}
	if (list_is_last(&neigh_entry->rif_list_node, &rif->neigh_list))
		return NULL;
	return list_next_entry(neigh_entry, rif_list_node);
}

int mlxsw_sp_neigh_entry_type(struct mlxsw_sp_neigh_entry *neigh_entry)
{
	return neigh_entry->key.n->tbl->family;
}

unsigned char *
mlxsw_sp_neigh_entry_ha(struct mlxsw_sp_neigh_entry *neigh_entry)
{
	return neigh_entry->ha;
}

u32 mlxsw_sp_neigh4_entry_dip(struct mlxsw_sp_neigh_entry *neigh_entry)
{
	struct neighbour *n;

	n = neigh_entry->key.n;
	return ntohl(*((__be32 *) n->primary_key));
}

struct in6_addr *
mlxsw_sp_neigh6_entry_dip(struct mlxsw_sp_neigh_entry *neigh_entry)
{
	struct neighbour *n;

	n = neigh_entry->key.n;
	return (struct in6_addr *) &n->primary_key;
}

int mlxsw_sp_neigh_counter_get(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_neigh_entry *neigh_entry,
			       u64 *p_counter)
{
	if (!neigh_entry->counter_valid)
		return -EINVAL;

	return mlxsw_sp_flow_counter_get(mlxsw_sp, neigh_entry->counter_index,
					 p_counter, NULL);
}

static struct mlxsw_sp_neigh_entry *
mlxsw_sp_neigh_entry_alloc(struct mlxsw_sp *mlxsw_sp, struct neighbour *n,
			   u16 rif)
{
	struct mlxsw_sp_neigh_entry *neigh_entry;

	neigh_entry = kzalloc(sizeof(*neigh_entry), GFP_KERNEL);
	if (!neigh_entry)
		return NULL;

	neigh_entry->key.n = n;
	neigh_entry->rif = rif;
	INIT_LIST_HEAD(&neigh_entry->nexthop_list);

	return neigh_entry;
}

static void mlxsw_sp_neigh_entry_free(struct mlxsw_sp_neigh_entry *neigh_entry)
{
	kfree(neigh_entry);
}

static int
mlxsw_sp_neigh_entry_insert(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_neigh_entry *neigh_entry)
{
	return rhashtable_insert_fast(&mlxsw_sp->router->neigh_ht,
				      &neigh_entry->ht_node,
				      mlxsw_sp_neigh_ht_params);
}

static void
mlxsw_sp_neigh_entry_remove(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_neigh_entry *neigh_entry)
{
	rhashtable_remove_fast(&mlxsw_sp->router->neigh_ht,
			       &neigh_entry->ht_node,
			       mlxsw_sp_neigh_ht_params);
}

static bool
mlxsw_sp_neigh_counter_should_alloc(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_neigh_entry *neigh_entry)
{
	struct devlink *devlink;
	const char *table_name;

	switch (mlxsw_sp_neigh_entry_type(neigh_entry)) {
	case AF_INET:
		table_name = MLXSW_SP_DPIPE_TABLE_NAME_HOST4;
		break;
	case AF_INET6:
		table_name = MLXSW_SP_DPIPE_TABLE_NAME_HOST6;
		break;
	default:
		WARN_ON(1);
		return false;
	}

	devlink = priv_to_devlink(mlxsw_sp->core);
	return devlink_dpipe_table_counter_enabled(devlink, table_name);
}

static void
mlxsw_sp_neigh_counter_alloc(struct mlxsw_sp *mlxsw_sp,
			     struct mlxsw_sp_neigh_entry *neigh_entry)
{
	if (!mlxsw_sp_neigh_counter_should_alloc(mlxsw_sp, neigh_entry))
		return;

	if (mlxsw_sp_flow_counter_alloc(mlxsw_sp, &neigh_entry->counter_index))
		return;

	neigh_entry->counter_valid = true;
}

static void
mlxsw_sp_neigh_counter_free(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_neigh_entry *neigh_entry)
{
	if (!neigh_entry->counter_valid)
		return;
	mlxsw_sp_flow_counter_free(mlxsw_sp,
				   neigh_entry->counter_index);
	neigh_entry->counter_valid = false;
}

static struct mlxsw_sp_neigh_entry *
mlxsw_sp_neigh_entry_create(struct mlxsw_sp *mlxsw_sp, struct neighbour *n)
{
	struct mlxsw_sp_neigh_entry *neigh_entry;
	struct mlxsw_sp_rif *rif;
	int err;

	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, n->dev);
	if (!rif)
		return ERR_PTR(-EINVAL);

	neigh_entry = mlxsw_sp_neigh_entry_alloc(mlxsw_sp, n, rif->rif_index);
	if (!neigh_entry)
		return ERR_PTR(-ENOMEM);

	err = mlxsw_sp_neigh_entry_insert(mlxsw_sp, neigh_entry);
	if (err)
		goto err_neigh_entry_insert;

	mlxsw_sp_neigh_counter_alloc(mlxsw_sp, neigh_entry);
	atomic_inc(&mlxsw_sp->router->neighs_update.neigh_count);
	list_add(&neigh_entry->rif_list_node, &rif->neigh_list);

	return neigh_entry;

err_neigh_entry_insert:
	mlxsw_sp_neigh_entry_free(neigh_entry);
	return ERR_PTR(err);
}

static void
mlxsw_sp_neigh_entry_destroy(struct mlxsw_sp *mlxsw_sp,
			     struct mlxsw_sp_neigh_entry *neigh_entry)
{
	list_del(&neigh_entry->rif_list_node);
	atomic_dec(&mlxsw_sp->router->neighs_update.neigh_count);
	mlxsw_sp_neigh_counter_free(mlxsw_sp, neigh_entry);
	mlxsw_sp_neigh_entry_remove(mlxsw_sp, neigh_entry);
	mlxsw_sp_neigh_entry_free(neigh_entry);
}

static struct mlxsw_sp_neigh_entry *
mlxsw_sp_neigh_entry_lookup(struct mlxsw_sp *mlxsw_sp, struct neighbour *n)
{
	struct mlxsw_sp_neigh_key key;

	key.n = n;
	return rhashtable_lookup_fast(&mlxsw_sp->router->neigh_ht,
				      &key, mlxsw_sp_neigh_ht_params);
}

static void
mlxsw_sp_router_neighs_update_interval_init(struct mlxsw_sp *mlxsw_sp)
{
	unsigned long interval;

#if IS_ENABLED(CONFIG_IPV6)
	interval = min_t(unsigned long,
			 NEIGH_VAR(&arp_tbl.parms, DELAY_PROBE_TIME),
			 NEIGH_VAR(&nd_tbl.parms, DELAY_PROBE_TIME));
#else
	interval = NEIGH_VAR(&arp_tbl.parms, DELAY_PROBE_TIME);
#endif
	mlxsw_sp->router->neighs_update.interval = jiffies_to_msecs(interval);
}

static void mlxsw_sp_router_neigh_ent_ipv4_process(struct mlxsw_sp *mlxsw_sp,
						   char *rauhtd_pl,
						   int ent_index)
{
	u64 max_rifs = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS);
	struct net_device *dev;
	struct neighbour *n;
	__be32 dipn;
	u32 dip;
	u16 rif;

	mlxsw_reg_rauhtd_ent_ipv4_unpack(rauhtd_pl, ent_index, &rif, &dip);

	if (WARN_ON_ONCE(rif >= max_rifs))
		return;
	if (!mlxsw_sp->router->rifs[rif]) {
		dev_err_ratelimited(mlxsw_sp->bus_info->dev, "Incorrect RIF in neighbour entry\n");
		return;
	}

	dipn = htonl(dip);
	dev = mlxsw_sp_rif_dev(mlxsw_sp->router->rifs[rif]);
	n = neigh_lookup(&arp_tbl, &dipn, dev);
	if (!n)
		return;

	netdev_dbg(dev, "Updating neighbour with IP=%pI4h\n", &dip);
	neigh_event_send(n, NULL);
	neigh_release(n);
}

#if IS_ENABLED(CONFIG_IPV6)
static void mlxsw_sp_router_neigh_ent_ipv6_process(struct mlxsw_sp *mlxsw_sp,
						   char *rauhtd_pl,
						   int rec_index)
{
	struct net_device *dev;
	struct neighbour *n;
	struct in6_addr dip;
	u16 rif;

	mlxsw_reg_rauhtd_ent_ipv6_unpack(rauhtd_pl, rec_index, &rif,
					 (char *) &dip);

	if (!mlxsw_sp->router->rifs[rif]) {
		dev_err_ratelimited(mlxsw_sp->bus_info->dev, "Incorrect RIF in neighbour entry\n");
		return;
	}

	dev = mlxsw_sp_rif_dev(mlxsw_sp->router->rifs[rif]);
	n = neigh_lookup(&nd_tbl, &dip, dev);
	if (!n)
		return;

	netdev_dbg(dev, "Updating neighbour with IP=%pI6c\n", &dip);
	neigh_event_send(n, NULL);
	neigh_release(n);
}
#else
static void mlxsw_sp_router_neigh_ent_ipv6_process(struct mlxsw_sp *mlxsw_sp,
						   char *rauhtd_pl,
						   int rec_index)
{
}
#endif

static void mlxsw_sp_router_neigh_rec_ipv4_process(struct mlxsw_sp *mlxsw_sp,
						   char *rauhtd_pl,
						   int rec_index)
{
	u8 num_entries;
	int i;

	num_entries = mlxsw_reg_rauhtd_ipv4_rec_num_entries_get(rauhtd_pl,
								rec_index);
	/* Hardware starts counting at 0, so add 1. */
	num_entries++;

	/* Each record consists of several neighbour entries. */
	for (i = 0; i < num_entries; i++) {
		int ent_index;

		ent_index = rec_index * MLXSW_REG_RAUHTD_IPV4_ENT_PER_REC + i;
		mlxsw_sp_router_neigh_ent_ipv4_process(mlxsw_sp, rauhtd_pl,
						       ent_index);
	}

}

static void mlxsw_sp_router_neigh_rec_ipv6_process(struct mlxsw_sp *mlxsw_sp,
						   char *rauhtd_pl,
						   int rec_index)
{
	/* One record contains one entry. */
	mlxsw_sp_router_neigh_ent_ipv6_process(mlxsw_sp, rauhtd_pl,
					       rec_index);
}

static void mlxsw_sp_router_neigh_rec_process(struct mlxsw_sp *mlxsw_sp,
					      char *rauhtd_pl, int rec_index)
{
	switch (mlxsw_reg_rauhtd_rec_type_get(rauhtd_pl, rec_index)) {
	case MLXSW_REG_RAUHTD_TYPE_IPV4:
		mlxsw_sp_router_neigh_rec_ipv4_process(mlxsw_sp, rauhtd_pl,
						       rec_index);
		break;
	case MLXSW_REG_RAUHTD_TYPE_IPV6:
		mlxsw_sp_router_neigh_rec_ipv6_process(mlxsw_sp, rauhtd_pl,
						       rec_index);
		break;
	}
}

static bool mlxsw_sp_router_rauhtd_is_full(char *rauhtd_pl)
{
	u8 num_rec, last_rec_index, num_entries;

	num_rec = mlxsw_reg_rauhtd_num_rec_get(rauhtd_pl);
	last_rec_index = num_rec - 1;

	if (num_rec < MLXSW_REG_RAUHTD_REC_MAX_NUM)
		return false;
	if (mlxsw_reg_rauhtd_rec_type_get(rauhtd_pl, last_rec_index) ==
	    MLXSW_REG_RAUHTD_TYPE_IPV6)
		return true;

	num_entries = mlxsw_reg_rauhtd_ipv4_rec_num_entries_get(rauhtd_pl,
								last_rec_index);
	if (++num_entries == MLXSW_REG_RAUHTD_IPV4_ENT_PER_REC)
		return true;
	return false;
}

static int
__mlxsw_sp_router_neighs_update_rauhtd(struct mlxsw_sp *mlxsw_sp,
				       char *rauhtd_pl,
				       enum mlxsw_reg_rauhtd_type type)
{
	int i, num_rec;
	int err;

	/* Ensure the RIF we read from the device does not change mid-dump. */
	mutex_lock(&mlxsw_sp->router->lock);
	do {
		mlxsw_reg_rauhtd_pack(rauhtd_pl, type);
		err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(rauhtd),
				      rauhtd_pl);
		if (err) {
			dev_err_ratelimited(mlxsw_sp->bus_info->dev, "Failed to dump neighbour table\n");
			break;
		}
		num_rec = mlxsw_reg_rauhtd_num_rec_get(rauhtd_pl);
		for (i = 0; i < num_rec; i++)
			mlxsw_sp_router_neigh_rec_process(mlxsw_sp, rauhtd_pl,
							  i);
	} while (mlxsw_sp_router_rauhtd_is_full(rauhtd_pl));
	mutex_unlock(&mlxsw_sp->router->lock);

	return err;
}

static int mlxsw_sp_router_neighs_update_rauhtd(struct mlxsw_sp *mlxsw_sp)
{
	enum mlxsw_reg_rauhtd_type type;
	char *rauhtd_pl;
	int err;

	if (!atomic_read(&mlxsw_sp->router->neighs_update.neigh_count))
		return 0;

	rauhtd_pl = kmalloc(MLXSW_REG_RAUHTD_LEN, GFP_KERNEL);
	if (!rauhtd_pl)
		return -ENOMEM;

	type = MLXSW_REG_RAUHTD_TYPE_IPV4;
	err = __mlxsw_sp_router_neighs_update_rauhtd(mlxsw_sp, rauhtd_pl, type);
	if (err)
		goto out;

	type = MLXSW_REG_RAUHTD_TYPE_IPV6;
	err = __mlxsw_sp_router_neighs_update_rauhtd(mlxsw_sp, rauhtd_pl, type);
out:
	kfree(rauhtd_pl);
	return err;
}

static void mlxsw_sp_router_neighs_update_nh(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_neigh_entry *neigh_entry;

	mutex_lock(&mlxsw_sp->router->lock);
	list_for_each_entry(neigh_entry, &mlxsw_sp->router->nexthop_neighs_list,
			    nexthop_neighs_list_node)
		/* If this neigh have nexthops, make the kernel think this neigh
		 * is active regardless of the traffic.
		 */
		neigh_event_send(neigh_entry->key.n, NULL);
	mutex_unlock(&mlxsw_sp->router->lock);
}

static void
mlxsw_sp_router_neighs_update_work_schedule(struct mlxsw_sp *mlxsw_sp)
{
	unsigned long interval = mlxsw_sp->router->neighs_update.interval;

	mlxsw_core_schedule_dw(&mlxsw_sp->router->neighs_update.dw,
			       msecs_to_jiffies(interval));
}

static void mlxsw_sp_router_neighs_update_work(struct work_struct *work)
{
	struct mlxsw_sp_router *router;
	int err;

	router = container_of(work, struct mlxsw_sp_router,
			      neighs_update.dw.work);
	err = mlxsw_sp_router_neighs_update_rauhtd(router->mlxsw_sp);
	if (err)
		dev_err(router->mlxsw_sp->bus_info->dev, "Could not update kernel for neigh activity");

	mlxsw_sp_router_neighs_update_nh(router->mlxsw_sp);

	mlxsw_sp_router_neighs_update_work_schedule(router->mlxsw_sp);
}

static void mlxsw_sp_router_probe_unresolved_nexthops(struct work_struct *work)
{
	struct mlxsw_sp_neigh_entry *neigh_entry;
	struct mlxsw_sp_router *router;

	router = container_of(work, struct mlxsw_sp_router,
			      nexthop_probe_dw.work);
	/* Iterate over nexthop neighbours, find those who are unresolved and
	 * send arp on them. This solves the chicken-egg problem when
	 * the nexthop wouldn't get offloaded until the neighbor is resolved
	 * but it wouldn't get resolved ever in case traffic is flowing in HW
	 * using different nexthop.
	 */
	mutex_lock(&router->lock);
	list_for_each_entry(neigh_entry, &router->nexthop_neighs_list,
			    nexthop_neighs_list_node)
		if (!neigh_entry->connected)
			neigh_event_send(neigh_entry->key.n, NULL);
	mutex_unlock(&router->lock);

	mlxsw_core_schedule_dw(&router->nexthop_probe_dw,
			       MLXSW_SP_UNRESOLVED_NH_PROBE_INTERVAL);
}

static void
mlxsw_sp_nexthop_neigh_update(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_neigh_entry *neigh_entry,
			      bool removing, bool dead);

static enum mlxsw_reg_rauht_op mlxsw_sp_rauht_op(bool adding)
{
	return adding ? MLXSW_REG_RAUHT_OP_WRITE_ADD :
			MLXSW_REG_RAUHT_OP_WRITE_DELETE;
}

static int
mlxsw_sp_router_neigh_entry_op4(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_neigh_entry *neigh_entry,
				enum mlxsw_reg_rauht_op op)
{
	struct neighbour *n = neigh_entry->key.n;
	u32 dip = ntohl(*((__be32 *) n->primary_key));
	char rauht_pl[MLXSW_REG_RAUHT_LEN];

	mlxsw_reg_rauht_pack4(rauht_pl, op, neigh_entry->rif, neigh_entry->ha,
			      dip);
	if (neigh_entry->counter_valid)
		mlxsw_reg_rauht_pack_counter(rauht_pl,
					     neigh_entry->counter_index);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rauht), rauht_pl);
}

static int
mlxsw_sp_router_neigh_entry_op6(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_neigh_entry *neigh_entry,
				enum mlxsw_reg_rauht_op op)
{
	struct neighbour *n = neigh_entry->key.n;
	char rauht_pl[MLXSW_REG_RAUHT_LEN];
	const char *dip = n->primary_key;

	mlxsw_reg_rauht_pack6(rauht_pl, op, neigh_entry->rif, neigh_entry->ha,
			      dip);
	if (neigh_entry->counter_valid)
		mlxsw_reg_rauht_pack_counter(rauht_pl,
					     neigh_entry->counter_index);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rauht), rauht_pl);
}

bool mlxsw_sp_neigh_ipv6_ignore(struct mlxsw_sp_neigh_entry *neigh_entry)
{
	struct neighbour *n = neigh_entry->key.n;

	/* Packets with a link-local destination address are trapped
	 * after LPM lookup and never reach the neighbour table, so
	 * there is no need to program such neighbours to the device.
	 */
	if (ipv6_addr_type((struct in6_addr *) &n->primary_key) &
	    IPV6_ADDR_LINKLOCAL)
		return true;
	return false;
}

static void
mlxsw_sp_neigh_entry_update(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_neigh_entry *neigh_entry,
			    bool adding)
{
	enum mlxsw_reg_rauht_op op = mlxsw_sp_rauht_op(adding);
	int err;

	if (!adding && !neigh_entry->connected)
		return;
	neigh_entry->connected = adding;
	if (neigh_entry->key.n->tbl->family == AF_INET) {
		err = mlxsw_sp_router_neigh_entry_op4(mlxsw_sp, neigh_entry,
						      op);
		if (err)
			return;
	} else if (neigh_entry->key.n->tbl->family == AF_INET6) {
		if (mlxsw_sp_neigh_ipv6_ignore(neigh_entry))
			return;
		err = mlxsw_sp_router_neigh_entry_op6(mlxsw_sp, neigh_entry,
						      op);
		if (err)
			return;
	} else {
		WARN_ON_ONCE(1);
		return;
	}

	if (adding)
		neigh_entry->key.n->flags |= NTF_OFFLOADED;
	else
		neigh_entry->key.n->flags &= ~NTF_OFFLOADED;
}

void
mlxsw_sp_neigh_entry_counter_update(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_neigh_entry *neigh_entry,
				    bool adding)
{
	if (adding)
		mlxsw_sp_neigh_counter_alloc(mlxsw_sp, neigh_entry);
	else
		mlxsw_sp_neigh_counter_free(mlxsw_sp, neigh_entry);
	mlxsw_sp_neigh_entry_update(mlxsw_sp, neigh_entry, true);
}

struct mlxsw_sp_netevent_work {
	struct work_struct work;
	struct mlxsw_sp *mlxsw_sp;
	struct neighbour *n;
};

static void mlxsw_sp_router_neigh_event_work(struct work_struct *work)
{
	struct mlxsw_sp_netevent_work *net_work =
		container_of(work, struct mlxsw_sp_netevent_work, work);
	struct mlxsw_sp *mlxsw_sp = net_work->mlxsw_sp;
	struct mlxsw_sp_neigh_entry *neigh_entry;
	struct neighbour *n = net_work->n;
	unsigned char ha[ETH_ALEN];
	bool entry_connected;
	u8 nud_state, dead;

	/* If these parameters are changed after we release the lock,
	 * then we are guaranteed to receive another event letting us
	 * know about it.
	 */
	read_lock_bh(&n->lock);
	memcpy(ha, n->ha, ETH_ALEN);
	nud_state = n->nud_state;
	dead = n->dead;
	read_unlock_bh(&n->lock);

	mutex_lock(&mlxsw_sp->router->lock);
	mlxsw_sp_span_respin(mlxsw_sp);

	entry_connected = nud_state & NUD_VALID && !dead;
	neigh_entry = mlxsw_sp_neigh_entry_lookup(mlxsw_sp, n);
	if (!entry_connected && !neigh_entry)
		goto out;
	if (!neigh_entry) {
		neigh_entry = mlxsw_sp_neigh_entry_create(mlxsw_sp, n);
		if (IS_ERR(neigh_entry))
			goto out;
	}

	if (neigh_entry->connected && entry_connected &&
	    !memcmp(neigh_entry->ha, ha, ETH_ALEN))
		goto out;

	memcpy(neigh_entry->ha, ha, ETH_ALEN);
	mlxsw_sp_neigh_entry_update(mlxsw_sp, neigh_entry, entry_connected);
	mlxsw_sp_nexthop_neigh_update(mlxsw_sp, neigh_entry, !entry_connected,
				      dead);

	if (!neigh_entry->connected && list_empty(&neigh_entry->nexthop_list))
		mlxsw_sp_neigh_entry_destroy(mlxsw_sp, neigh_entry);

out:
	mutex_unlock(&mlxsw_sp->router->lock);
	neigh_release(n);
	kfree(net_work);
}

static int mlxsw_sp_mp_hash_init(struct mlxsw_sp *mlxsw_sp);

static void mlxsw_sp_router_mp_hash_event_work(struct work_struct *work)
{
	struct mlxsw_sp_netevent_work *net_work =
		container_of(work, struct mlxsw_sp_netevent_work, work);
	struct mlxsw_sp *mlxsw_sp = net_work->mlxsw_sp;

	mlxsw_sp_mp_hash_init(mlxsw_sp);
	kfree(net_work);
}

static int __mlxsw_sp_router_init(struct mlxsw_sp *mlxsw_sp);

static void mlxsw_sp_router_update_priority_work(struct work_struct *work)
{
	struct mlxsw_sp_netevent_work *net_work =
		container_of(work, struct mlxsw_sp_netevent_work, work);
	struct mlxsw_sp *mlxsw_sp = net_work->mlxsw_sp;

	__mlxsw_sp_router_init(mlxsw_sp);
	kfree(net_work);
}

static int mlxsw_sp_router_schedule_work(struct net *net,
					 struct mlxsw_sp_router *router,
					 struct neighbour *n,
					 void (*cb)(struct work_struct *))
{
	struct mlxsw_sp_netevent_work *net_work;

	if (!net_eq(net, mlxsw_sp_net(router->mlxsw_sp)))
		return NOTIFY_DONE;

	net_work = kzalloc(sizeof(*net_work), GFP_ATOMIC);
	if (!net_work)
		return NOTIFY_BAD;

	INIT_WORK(&net_work->work, cb);
	net_work->mlxsw_sp = router->mlxsw_sp;
	net_work->n = n;
	mlxsw_core_schedule_work(&net_work->work);
	return NOTIFY_DONE;
}

static bool mlxsw_sp_dev_lower_is_port(struct net_device *dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port;

	rcu_read_lock();
	mlxsw_sp_port = mlxsw_sp_port_dev_lower_find_rcu(dev);
	rcu_read_unlock();
	return !!mlxsw_sp_port;
}

static int mlxsw_sp_router_netevent_event(struct notifier_block *nb,
					  unsigned long event, void *ptr)
{
	struct mlxsw_sp_router *router;
	unsigned long interval;
	struct neigh_parms *p;
	struct neighbour *n;
	struct net *net;

	router = container_of(nb, struct mlxsw_sp_router, netevent_nb);

	switch (event) {
	case NETEVENT_DELAY_PROBE_TIME_UPDATE:
		p = ptr;

		/* We don't care about changes in the default table. */
		if (!p->dev || (p->tbl->family != AF_INET &&
				p->tbl->family != AF_INET6))
			return NOTIFY_DONE;

		/* We are in atomic context and can't take RTNL mutex,
		 * so use RCU variant to walk the device chain.
		 */
		if (!mlxsw_sp_dev_lower_is_port(p->dev))
			return NOTIFY_DONE;

		interval = jiffies_to_msecs(NEIGH_VAR(p, DELAY_PROBE_TIME));
		router->neighs_update.interval = interval;
		break;
	case NETEVENT_NEIGH_UPDATE:
		n = ptr;
		net = neigh_parms_net(n->parms);

		if (n->tbl->family != AF_INET && n->tbl->family != AF_INET6)
			return NOTIFY_DONE;

		if (!mlxsw_sp_dev_lower_is_port(n->dev))
			return NOTIFY_DONE;

		/* Take a reference to ensure the neighbour won't be
		 * destructed until we drop the reference in delayed
		 * work.
		 */
		neigh_clone(n);
		return mlxsw_sp_router_schedule_work(net, router, n,
				mlxsw_sp_router_neigh_event_work);

	case NETEVENT_IPV4_MPATH_HASH_UPDATE:
	case NETEVENT_IPV6_MPATH_HASH_UPDATE:
		return mlxsw_sp_router_schedule_work(ptr, router, NULL,
				mlxsw_sp_router_mp_hash_event_work);

	case NETEVENT_IPV4_FWD_UPDATE_PRIORITY_UPDATE:
		return mlxsw_sp_router_schedule_work(ptr, router, NULL,
				mlxsw_sp_router_update_priority_work);
	}

	return NOTIFY_DONE;
}

static int mlxsw_sp_neigh_init(struct mlxsw_sp *mlxsw_sp)
{
	int err;

	err = rhashtable_init(&mlxsw_sp->router->neigh_ht,
			      &mlxsw_sp_neigh_ht_params);
	if (err)
		return err;

	/* Initialize the polling interval according to the default
	 * table.
	 */
	mlxsw_sp_router_neighs_update_interval_init(mlxsw_sp);

	/* Create the delayed works for the activity_update */
	INIT_DELAYED_WORK(&mlxsw_sp->router->neighs_update.dw,
			  mlxsw_sp_router_neighs_update_work);
	INIT_DELAYED_WORK(&mlxsw_sp->router->nexthop_probe_dw,
			  mlxsw_sp_router_probe_unresolved_nexthops);
	atomic_set(&mlxsw_sp->router->neighs_update.neigh_count, 0);
	mlxsw_core_schedule_dw(&mlxsw_sp->router->neighs_update.dw, 0);
	mlxsw_core_schedule_dw(&mlxsw_sp->router->nexthop_probe_dw, 0);
	return 0;
}

static void mlxsw_sp_neigh_fini(struct mlxsw_sp *mlxsw_sp)
{
	cancel_delayed_work_sync(&mlxsw_sp->router->neighs_update.dw);
	cancel_delayed_work_sync(&mlxsw_sp->router->nexthop_probe_dw);
	rhashtable_destroy(&mlxsw_sp->router->neigh_ht);
}

static void mlxsw_sp_neigh_rif_gone_sync(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp_neigh_entry *neigh_entry, *tmp;

	list_for_each_entry_safe(neigh_entry, tmp, &rif->neigh_list,
				 rif_list_node) {
		mlxsw_sp_neigh_entry_update(mlxsw_sp, neigh_entry, false);
		mlxsw_sp_neigh_entry_destroy(mlxsw_sp, neigh_entry);
	}
}

enum mlxsw_sp_nexthop_type {
	MLXSW_SP_NEXTHOP_TYPE_ETH,
	MLXSW_SP_NEXTHOP_TYPE_IPIP,
};

enum mlxsw_sp_nexthop_action {
	/* Nexthop forwards packets to an egress RIF */
	MLXSW_SP_NEXTHOP_ACTION_FORWARD,
	/* Nexthop discards packets */
	MLXSW_SP_NEXTHOP_ACTION_DISCARD,
	/* Nexthop traps packets */
	MLXSW_SP_NEXTHOP_ACTION_TRAP,
};

struct mlxsw_sp_nexthop_key {
	struct fib_nh *fib_nh;
};

struct mlxsw_sp_nexthop {
	struct list_head neigh_list_node; /* member of neigh entry list */
	struct list_head crif_list_node;
	struct list_head router_list_node;
	struct mlxsw_sp_nexthop_group_info *nhgi; /* pointer back to the group
						   * this nexthop belongs to
						   */
	struct rhash_head ht_node;
	struct neigh_table *neigh_tbl;
	struct mlxsw_sp_nexthop_key key;
	unsigned char gw_addr[sizeof(struct in6_addr)];
	int ifindex;
	int nh_weight;
	int norm_nh_weight;
	int num_adj_entries;
	struct mlxsw_sp_crif *crif;
	u8 should_offload:1, /* set indicates this nexthop should be written
			      * to the adjacency table.
			      */
	   offloaded:1, /* set indicates this nexthop was written to the
			 * adjacency table.
			 */
	   update:1; /* set indicates this nexthop should be updated in the
		      * adjacency table (f.e., its MAC changed).
		      */
	enum mlxsw_sp_nexthop_action action;
	enum mlxsw_sp_nexthop_type type;
	union {
		struct mlxsw_sp_neigh_entry *neigh_entry;
		struct mlxsw_sp_ipip_entry *ipip_entry;
	};
	unsigned int counter_index;
	bool counter_valid;
};

static struct net_device *
mlxsw_sp_nexthop_dev(const struct mlxsw_sp_nexthop *nh)
{
	if (!nh->crif)
		return NULL;
	return nh->crif->key.dev;
}

enum mlxsw_sp_nexthop_group_type {
	MLXSW_SP_NEXTHOP_GROUP_TYPE_IPV4,
	MLXSW_SP_NEXTHOP_GROUP_TYPE_IPV6,
	MLXSW_SP_NEXTHOP_GROUP_TYPE_OBJ,
};

struct mlxsw_sp_nexthop_group_info {
	struct mlxsw_sp_nexthop_group *nh_grp;
	u32 adj_index;
	u16 ecmp_size;
	u16 count;
	int sum_norm_weight;
	u8 adj_index_valid:1,
	   gateway:1, /* routes using the group use a gateway */
	   is_resilient:1;
	struct list_head list; /* member in nh_res_grp_list */
	struct mlxsw_sp_nexthop nexthops[];
};

static struct mlxsw_sp_rif *
mlxsw_sp_nhgi_rif(const struct mlxsw_sp_nexthop_group_info *nhgi)
{
	struct mlxsw_sp_crif *crif = nhgi->nexthops[0].crif;

	if (!crif)
		return NULL;
	return crif->rif;
}

struct mlxsw_sp_nexthop_group_vr_key {
	u16 vr_id;
	enum mlxsw_sp_l3proto proto;
};

struct mlxsw_sp_nexthop_group_vr_entry {
	struct list_head list; /* member in vr_list */
	struct rhash_head ht_node; /* member in vr_ht */
	refcount_t ref_count;
	struct mlxsw_sp_nexthop_group_vr_key key;
};

struct mlxsw_sp_nexthop_group {
	struct rhash_head ht_node;
	struct list_head fib_list; /* list of fib entries that use this group */
	union {
		struct {
			struct fib_info *fi;
		} ipv4;
		struct {
			u32 id;
		} obj;
	};
	struct mlxsw_sp_nexthop_group_info *nhgi;
	struct list_head vr_list;
	struct rhashtable vr_ht;
	enum mlxsw_sp_nexthop_group_type type;
	bool can_destroy;
};

void mlxsw_sp_nexthop_counter_alloc(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_nexthop *nh)
{
	struct devlink *devlink;

	devlink = priv_to_devlink(mlxsw_sp->core);
	if (!devlink_dpipe_table_counter_enabled(devlink,
						 MLXSW_SP_DPIPE_TABLE_NAME_ADJ))
		return;

	if (mlxsw_sp_flow_counter_alloc(mlxsw_sp, &nh->counter_index))
		return;

	nh->counter_valid = true;
}

void mlxsw_sp_nexthop_counter_free(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_nexthop *nh)
{
	if (!nh->counter_valid)
		return;
	mlxsw_sp_flow_counter_free(mlxsw_sp, nh->counter_index);
	nh->counter_valid = false;
}

int mlxsw_sp_nexthop_counter_get(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_nexthop *nh, u64 *p_counter)
{
	if (!nh->counter_valid)
		return -EINVAL;

	return mlxsw_sp_flow_counter_get(mlxsw_sp, nh->counter_index,
					 p_counter, NULL);
}

struct mlxsw_sp_nexthop *mlxsw_sp_nexthop_next(struct mlxsw_sp_router *router,
					       struct mlxsw_sp_nexthop *nh)
{
	if (!nh) {
		if (list_empty(&router->nexthop_list))
			return NULL;
		else
			return list_first_entry(&router->nexthop_list,
						typeof(*nh), router_list_node);
	}
	if (list_is_last(&nh->router_list_node, &router->nexthop_list))
		return NULL;
	return list_next_entry(nh, router_list_node);
}

bool mlxsw_sp_nexthop_is_forward(const struct mlxsw_sp_nexthop *nh)
{
	return nh->offloaded && nh->action == MLXSW_SP_NEXTHOP_ACTION_FORWARD;
}

unsigned char *mlxsw_sp_nexthop_ha(struct mlxsw_sp_nexthop *nh)
{
	if (nh->type != MLXSW_SP_NEXTHOP_TYPE_ETH ||
	    !mlxsw_sp_nexthop_is_forward(nh))
		return NULL;
	return nh->neigh_entry->ha;
}

int mlxsw_sp_nexthop_indexes(struct mlxsw_sp_nexthop *nh, u32 *p_adj_index,
			     u32 *p_adj_size, u32 *p_adj_hash_index)
{
	struct mlxsw_sp_nexthop_group_info *nhgi = nh->nhgi;
	u32 adj_hash_index = 0;
	int i;

	if (!nh->offloaded || !nhgi->adj_index_valid)
		return -EINVAL;

	*p_adj_index = nhgi->adj_index;
	*p_adj_size = nhgi->ecmp_size;

	for (i = 0; i < nhgi->count; i++) {
		struct mlxsw_sp_nexthop *nh_iter = &nhgi->nexthops[i];

		if (nh_iter == nh)
			break;
		if (nh_iter->offloaded)
			adj_hash_index += nh_iter->num_adj_entries;
	}

	*p_adj_hash_index = adj_hash_index;
	return 0;
}

struct mlxsw_sp_rif *mlxsw_sp_nexthop_rif(struct mlxsw_sp_nexthop *nh)
{
	if (WARN_ON(!nh->crif))
		return NULL;
	return nh->crif->rif;
}

bool mlxsw_sp_nexthop_group_has_ipip(struct mlxsw_sp_nexthop *nh)
{
	struct mlxsw_sp_nexthop_group_info *nhgi = nh->nhgi;
	int i;

	for (i = 0; i < nhgi->count; i++) {
		struct mlxsw_sp_nexthop *nh_iter = &nhgi->nexthops[i];

		if (nh_iter->type == MLXSW_SP_NEXTHOP_TYPE_IPIP)
			return true;
	}
	return false;
}

static const struct rhashtable_params mlxsw_sp_nexthop_group_vr_ht_params = {
	.key_offset = offsetof(struct mlxsw_sp_nexthop_group_vr_entry, key),
	.head_offset = offsetof(struct mlxsw_sp_nexthop_group_vr_entry, ht_node),
	.key_len = sizeof(struct mlxsw_sp_nexthop_group_vr_key),
	.automatic_shrinking = true,
};

static struct mlxsw_sp_nexthop_group_vr_entry *
mlxsw_sp_nexthop_group_vr_entry_lookup(struct mlxsw_sp_nexthop_group *nh_grp,
				       const struct mlxsw_sp_fib *fib)
{
	struct mlxsw_sp_nexthop_group_vr_key key;

	memset(&key, 0, sizeof(key));
	key.vr_id = fib->vr->id;
	key.proto = fib->proto;
	return rhashtable_lookup_fast(&nh_grp->vr_ht, &key,
				      mlxsw_sp_nexthop_group_vr_ht_params);
}

static int
mlxsw_sp_nexthop_group_vr_entry_create(struct mlxsw_sp_nexthop_group *nh_grp,
				       const struct mlxsw_sp_fib *fib)
{
	struct mlxsw_sp_nexthop_group_vr_entry *vr_entry;
	int err;

	vr_entry = kzalloc(sizeof(*vr_entry), GFP_KERNEL);
	if (!vr_entry)
		return -ENOMEM;

	vr_entry->key.vr_id = fib->vr->id;
	vr_entry->key.proto = fib->proto;
	refcount_set(&vr_entry->ref_count, 1);

	err = rhashtable_insert_fast(&nh_grp->vr_ht, &vr_entry->ht_node,
				     mlxsw_sp_nexthop_group_vr_ht_params);
	if (err)
		goto err_hashtable_insert;

	list_add(&vr_entry->list, &nh_grp->vr_list);

	return 0;

err_hashtable_insert:
	kfree(vr_entry);
	return err;
}

static void
mlxsw_sp_nexthop_group_vr_entry_destroy(struct mlxsw_sp_nexthop_group *nh_grp,
					struct mlxsw_sp_nexthop_group_vr_entry *vr_entry)
{
	list_del(&vr_entry->list);
	rhashtable_remove_fast(&nh_grp->vr_ht, &vr_entry->ht_node,
			       mlxsw_sp_nexthop_group_vr_ht_params);
	kfree(vr_entry);
}

static int
mlxsw_sp_nexthop_group_vr_link(struct mlxsw_sp_nexthop_group *nh_grp,
			       const struct mlxsw_sp_fib *fib)
{
	struct mlxsw_sp_nexthop_group_vr_entry *vr_entry;

	vr_entry = mlxsw_sp_nexthop_group_vr_entry_lookup(nh_grp, fib);
	if (vr_entry) {
		refcount_inc(&vr_entry->ref_count);
		return 0;
	}

	return mlxsw_sp_nexthop_group_vr_entry_create(nh_grp, fib);
}

static void
mlxsw_sp_nexthop_group_vr_unlink(struct mlxsw_sp_nexthop_group *nh_grp,
				 const struct mlxsw_sp_fib *fib)
{
	struct mlxsw_sp_nexthop_group_vr_entry *vr_entry;

	vr_entry = mlxsw_sp_nexthop_group_vr_entry_lookup(nh_grp, fib);
	if (WARN_ON_ONCE(!vr_entry))
		return;

	if (!refcount_dec_and_test(&vr_entry->ref_count))
		return;

	mlxsw_sp_nexthop_group_vr_entry_destroy(nh_grp, vr_entry);
}

struct mlxsw_sp_nexthop_group_cmp_arg {
	enum mlxsw_sp_nexthop_group_type type;
	union {
		struct fib_info *fi;
		struct mlxsw_sp_fib6_entry *fib6_entry;
		u32 id;
	};
};

static bool
mlxsw_sp_nexthop6_group_has_nexthop(const struct mlxsw_sp_nexthop_group *nh_grp,
				    const struct in6_addr *gw, int ifindex,
				    int weight)
{
	int i;

	for (i = 0; i < nh_grp->nhgi->count; i++) {
		const struct mlxsw_sp_nexthop *nh;

		nh = &nh_grp->nhgi->nexthops[i];
		if (nh->ifindex == ifindex && nh->nh_weight == weight &&
		    ipv6_addr_equal(gw, (struct in6_addr *) nh->gw_addr))
			return true;
	}

	return false;
}

static bool
mlxsw_sp_nexthop6_group_cmp(const struct mlxsw_sp_nexthop_group *nh_grp,
			    const struct mlxsw_sp_fib6_entry *fib6_entry)
{
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6;

	if (nh_grp->nhgi->count != fib6_entry->nrt6)
		return false;

	list_for_each_entry(mlxsw_sp_rt6, &fib6_entry->rt6_list, list) {
		struct fib6_nh *fib6_nh = mlxsw_sp_rt6->rt->fib6_nh;
		struct in6_addr *gw;
		int ifindex, weight;

		ifindex = fib6_nh->fib_nh_dev->ifindex;
		weight = fib6_nh->fib_nh_weight;
		gw = &fib6_nh->fib_nh_gw6;
		if (!mlxsw_sp_nexthop6_group_has_nexthop(nh_grp, gw, ifindex,
							 weight))
			return false;
	}

	return true;
}

static int
mlxsw_sp_nexthop_group_cmp(struct rhashtable_compare_arg *arg, const void *ptr)
{
	const struct mlxsw_sp_nexthop_group_cmp_arg *cmp_arg = arg->key;
	const struct mlxsw_sp_nexthop_group *nh_grp = ptr;

	if (nh_grp->type != cmp_arg->type)
		return 1;

	switch (cmp_arg->type) {
	case MLXSW_SP_NEXTHOP_GROUP_TYPE_IPV4:
		return cmp_arg->fi != nh_grp->ipv4.fi;
	case MLXSW_SP_NEXTHOP_GROUP_TYPE_IPV6:
		return !mlxsw_sp_nexthop6_group_cmp(nh_grp,
						    cmp_arg->fib6_entry);
	case MLXSW_SP_NEXTHOP_GROUP_TYPE_OBJ:
		return cmp_arg->id != nh_grp->obj.id;
	default:
		WARN_ON(1);
		return 1;
	}
}

static u32 mlxsw_sp_nexthop_group_hash_obj(const void *data, u32 len, u32 seed)
{
	const struct mlxsw_sp_nexthop_group *nh_grp = data;
	const struct mlxsw_sp_nexthop *nh;
	struct fib_info *fi;
	unsigned int val;
	int i;

	switch (nh_grp->type) {
	case MLXSW_SP_NEXTHOP_GROUP_TYPE_IPV4:
		fi = nh_grp->ipv4.fi;
		return jhash(&fi, sizeof(fi), seed);
	case MLXSW_SP_NEXTHOP_GROUP_TYPE_IPV6:
		val = nh_grp->nhgi->count;
		for (i = 0; i < nh_grp->nhgi->count; i++) {
			nh = &nh_grp->nhgi->nexthops[i];
			val ^= jhash(&nh->ifindex, sizeof(nh->ifindex), seed);
			val ^= jhash(&nh->gw_addr, sizeof(nh->gw_addr), seed);
		}
		return jhash(&val, sizeof(val), seed);
	case MLXSW_SP_NEXTHOP_GROUP_TYPE_OBJ:
		return jhash(&nh_grp->obj.id, sizeof(nh_grp->obj.id), seed);
	default:
		WARN_ON(1);
		return 0;
	}
}

static u32
mlxsw_sp_nexthop6_group_hash(struct mlxsw_sp_fib6_entry *fib6_entry, u32 seed)
{
	unsigned int val = fib6_entry->nrt6;
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6;

	list_for_each_entry(mlxsw_sp_rt6, &fib6_entry->rt6_list, list) {
		struct fib6_nh *fib6_nh = mlxsw_sp_rt6->rt->fib6_nh;
		struct net_device *dev = fib6_nh->fib_nh_dev;
		struct in6_addr *gw = &fib6_nh->fib_nh_gw6;

		val ^= jhash(&dev->ifindex, sizeof(dev->ifindex), seed);
		val ^= jhash(gw, sizeof(*gw), seed);
	}

	return jhash(&val, sizeof(val), seed);
}

static u32
mlxsw_sp_nexthop_group_hash(const void *data, u32 len, u32 seed)
{
	const struct mlxsw_sp_nexthop_group_cmp_arg *cmp_arg = data;

	switch (cmp_arg->type) {
	case MLXSW_SP_NEXTHOP_GROUP_TYPE_IPV4:
		return jhash(&cmp_arg->fi, sizeof(cmp_arg->fi), seed);
	case MLXSW_SP_NEXTHOP_GROUP_TYPE_IPV6:
		return mlxsw_sp_nexthop6_group_hash(cmp_arg->fib6_entry, seed);
	case MLXSW_SP_NEXTHOP_GROUP_TYPE_OBJ:
		return jhash(&cmp_arg->id, sizeof(cmp_arg->id), seed);
	default:
		WARN_ON(1);
		return 0;
	}
}

static const struct rhashtable_params mlxsw_sp_nexthop_group_ht_params = {
	.head_offset = offsetof(struct mlxsw_sp_nexthop_group, ht_node),
	.hashfn	     = mlxsw_sp_nexthop_group_hash,
	.obj_hashfn  = mlxsw_sp_nexthop_group_hash_obj,
	.obj_cmpfn   = mlxsw_sp_nexthop_group_cmp,
};

static int mlxsw_sp_nexthop_group_insert(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_nexthop_group *nh_grp)
{
	if (nh_grp->type == MLXSW_SP_NEXTHOP_GROUP_TYPE_IPV6 &&
	    !nh_grp->nhgi->gateway)
		return 0;

	return rhashtable_insert_fast(&mlxsw_sp->router->nexthop_group_ht,
				      &nh_grp->ht_node,
				      mlxsw_sp_nexthop_group_ht_params);
}

static void mlxsw_sp_nexthop_group_remove(struct mlxsw_sp *mlxsw_sp,
					  struct mlxsw_sp_nexthop_group *nh_grp)
{
	if (nh_grp->type == MLXSW_SP_NEXTHOP_GROUP_TYPE_IPV6 &&
	    !nh_grp->nhgi->gateway)
		return;

	rhashtable_remove_fast(&mlxsw_sp->router->nexthop_group_ht,
			       &nh_grp->ht_node,
			       mlxsw_sp_nexthop_group_ht_params);
}

static struct mlxsw_sp_nexthop_group *
mlxsw_sp_nexthop4_group_lookup(struct mlxsw_sp *mlxsw_sp,
			       struct fib_info *fi)
{
	struct mlxsw_sp_nexthop_group_cmp_arg cmp_arg;

	cmp_arg.type = MLXSW_SP_NEXTHOP_GROUP_TYPE_IPV4;
	cmp_arg.fi = fi;
	return rhashtable_lookup_fast(&mlxsw_sp->router->nexthop_group_ht,
				      &cmp_arg,
				      mlxsw_sp_nexthop_group_ht_params);
}

static struct mlxsw_sp_nexthop_group *
mlxsw_sp_nexthop6_group_lookup(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_fib6_entry *fib6_entry)
{
	struct mlxsw_sp_nexthop_group_cmp_arg cmp_arg;

	cmp_arg.type = MLXSW_SP_NEXTHOP_GROUP_TYPE_IPV6;
	cmp_arg.fib6_entry = fib6_entry;
	return rhashtable_lookup_fast(&mlxsw_sp->router->nexthop_group_ht,
				      &cmp_arg,
				      mlxsw_sp_nexthop_group_ht_params);
}

static const struct rhashtable_params mlxsw_sp_nexthop_ht_params = {
	.key_offset = offsetof(struct mlxsw_sp_nexthop, key),
	.head_offset = offsetof(struct mlxsw_sp_nexthop, ht_node),
	.key_len = sizeof(struct mlxsw_sp_nexthop_key),
};

static int mlxsw_sp_nexthop_insert(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_nexthop *nh)
{
	return rhashtable_insert_fast(&mlxsw_sp->router->nexthop_ht,
				      &nh->ht_node, mlxsw_sp_nexthop_ht_params);
}

static void mlxsw_sp_nexthop_remove(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_nexthop *nh)
{
	rhashtable_remove_fast(&mlxsw_sp->router->nexthop_ht, &nh->ht_node,
			       mlxsw_sp_nexthop_ht_params);
}

static struct mlxsw_sp_nexthop *
mlxsw_sp_nexthop_lookup(struct mlxsw_sp *mlxsw_sp,
			struct mlxsw_sp_nexthop_key key)
{
	return rhashtable_lookup_fast(&mlxsw_sp->router->nexthop_ht, &key,
				      mlxsw_sp_nexthop_ht_params);
}

static int mlxsw_sp_adj_index_mass_update_vr(struct mlxsw_sp *mlxsw_sp,
					     enum mlxsw_sp_l3proto proto,
					     u16 vr_id,
					     u32 adj_index, u16 ecmp_size,
					     u32 new_adj_index,
					     u16 new_ecmp_size)
{
	char raleu_pl[MLXSW_REG_RALEU_LEN];

	mlxsw_reg_raleu_pack(raleu_pl,
			     (enum mlxsw_reg_ralxx_protocol) proto, vr_id,
			     adj_index, ecmp_size, new_adj_index,
			     new_ecmp_size);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(raleu), raleu_pl);
}

static int mlxsw_sp_adj_index_mass_update(struct mlxsw_sp *mlxsw_sp,
					  struct mlxsw_sp_nexthop_group *nh_grp,
					  u32 old_adj_index, u16 old_ecmp_size)
{
	struct mlxsw_sp_nexthop_group_info *nhgi = nh_grp->nhgi;
	struct mlxsw_sp_nexthop_group_vr_entry *vr_entry;
	int err;

	list_for_each_entry(vr_entry, &nh_grp->vr_list, list) {
		err = mlxsw_sp_adj_index_mass_update_vr(mlxsw_sp,
							vr_entry->key.proto,
							vr_entry->key.vr_id,
							old_adj_index,
							old_ecmp_size,
							nhgi->adj_index,
							nhgi->ecmp_size);
		if (err)
			goto err_mass_update_vr;
	}
	return 0;

err_mass_update_vr:
	list_for_each_entry_continue_reverse(vr_entry, &nh_grp->vr_list, list)
		mlxsw_sp_adj_index_mass_update_vr(mlxsw_sp, vr_entry->key.proto,
						  vr_entry->key.vr_id,
						  nhgi->adj_index,
						  nhgi->ecmp_size,
						  old_adj_index, old_ecmp_size);
	return err;
}

static int __mlxsw_sp_nexthop_eth_update(struct mlxsw_sp *mlxsw_sp,
					 u32 adj_index,
					 struct mlxsw_sp_nexthop *nh,
					 bool force, char *ratr_pl)
{
	struct mlxsw_sp_neigh_entry *neigh_entry = nh->neigh_entry;
	struct mlxsw_sp_rif *rif = mlxsw_sp_nexthop_rif(nh);
	enum mlxsw_reg_ratr_op op;
	u16 rif_index;

	rif_index = rif ? rif->rif_index :
			  mlxsw_sp->router->lb_crif->rif->rif_index;
	op = force ? MLXSW_REG_RATR_OP_WRITE_WRITE_ENTRY :
		     MLXSW_REG_RATR_OP_WRITE_WRITE_ENTRY_ON_ACTIVITY;
	mlxsw_reg_ratr_pack(ratr_pl, op, true, MLXSW_REG_RATR_TYPE_ETHERNET,
			    adj_index, rif_index);
	switch (nh->action) {
	case MLXSW_SP_NEXTHOP_ACTION_FORWARD:
		mlxsw_reg_ratr_eth_entry_pack(ratr_pl, neigh_entry->ha);
		break;
	case MLXSW_SP_NEXTHOP_ACTION_DISCARD:
		mlxsw_reg_ratr_trap_action_set(ratr_pl,
					       MLXSW_REG_RATR_TRAP_ACTION_DISCARD_ERRORS);
		break;
	case MLXSW_SP_NEXTHOP_ACTION_TRAP:
		mlxsw_reg_ratr_trap_action_set(ratr_pl,
					       MLXSW_REG_RATR_TRAP_ACTION_TRAP);
		mlxsw_reg_ratr_trap_id_set(ratr_pl, MLXSW_TRAP_ID_RTR_EGRESS0);
		break;
	default:
		WARN_ON_ONCE(1);
		return -EINVAL;
	}
	if (nh->counter_valid)
		mlxsw_reg_ratr_counter_pack(ratr_pl, nh->counter_index, true);
	else
		mlxsw_reg_ratr_counter_pack(ratr_pl, 0, false);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ratr), ratr_pl);
}

int mlxsw_sp_nexthop_eth_update(struct mlxsw_sp *mlxsw_sp, u32 adj_index,
				struct mlxsw_sp_nexthop *nh, bool force,
				char *ratr_pl)
{
	int i;

	for (i = 0; i < nh->num_adj_entries; i++) {
		int err;

		err = __mlxsw_sp_nexthop_eth_update(mlxsw_sp, adj_index + i,
						    nh, force, ratr_pl);
		if (err)
			return err;
	}

	return 0;
}

static int __mlxsw_sp_nexthop_ipip_update(struct mlxsw_sp *mlxsw_sp,
					  u32 adj_index,
					  struct mlxsw_sp_nexthop *nh,
					  bool force, char *ratr_pl)
{
	const struct mlxsw_sp_ipip_ops *ipip_ops;

	ipip_ops = mlxsw_sp->router->ipip_ops_arr[nh->ipip_entry->ipipt];
	return ipip_ops->nexthop_update(mlxsw_sp, adj_index, nh->ipip_entry,
					force, ratr_pl);
}

static int mlxsw_sp_nexthop_ipip_update(struct mlxsw_sp *mlxsw_sp,
					u32 adj_index,
					struct mlxsw_sp_nexthop *nh, bool force,
					char *ratr_pl)
{
	int i;

	for (i = 0; i < nh->num_adj_entries; i++) {
		int err;

		err = __mlxsw_sp_nexthop_ipip_update(mlxsw_sp, adj_index + i,
						     nh, force, ratr_pl);
		if (err)
			return err;
	}

	return 0;
}

static int mlxsw_sp_nexthop_update(struct mlxsw_sp *mlxsw_sp, u32 adj_index,
				   struct mlxsw_sp_nexthop *nh, bool force,
				   char *ratr_pl)
{
	/* When action is discard or trap, the nexthop must be
	 * programmed as an Ethernet nexthop.
	 */
	if (nh->type == MLXSW_SP_NEXTHOP_TYPE_ETH ||
	    nh->action == MLXSW_SP_NEXTHOP_ACTION_DISCARD ||
	    nh->action == MLXSW_SP_NEXTHOP_ACTION_TRAP)
		return mlxsw_sp_nexthop_eth_update(mlxsw_sp, adj_index, nh,
						   force, ratr_pl);
	else
		return mlxsw_sp_nexthop_ipip_update(mlxsw_sp, adj_index, nh,
						    force, ratr_pl);
}

static int
mlxsw_sp_nexthop_group_update(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_nexthop_group_info *nhgi,
			      bool reallocate)
{
	char ratr_pl[MLXSW_REG_RATR_LEN];
	u32 adj_index = nhgi->adj_index; /* base */
	struct mlxsw_sp_nexthop *nh;
	int i;

	for (i = 0; i < nhgi->count; i++) {
		nh = &nhgi->nexthops[i];

		if (!nh->should_offload) {
			nh->offloaded = 0;
			continue;
		}

		if (nh->update || reallocate) {
			int err = 0;

			err = mlxsw_sp_nexthop_update(mlxsw_sp, adj_index, nh,
						      true, ratr_pl);
			if (err)
				return err;
			nh->update = 0;
			nh->offloaded = 1;
		}
		adj_index += nh->num_adj_entries;
	}
	return 0;
}

static int
mlxsw_sp_nexthop_fib_entries_update(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_nexthop_group *nh_grp)
{
	struct mlxsw_sp_fib_entry *fib_entry;
	int err;

	list_for_each_entry(fib_entry, &nh_grp->fib_list, nexthop_group_node) {
		err = mlxsw_sp_fib_entry_update(mlxsw_sp, fib_entry);
		if (err)
			return err;
	}
	return 0;
}

struct mlxsw_sp_adj_grp_size_range {
	u16 start; /* Inclusive */
	u16 end; /* Inclusive */
};

/* Ordered by range start value */
static const struct mlxsw_sp_adj_grp_size_range
mlxsw_sp1_adj_grp_size_ranges[] = {
	{ .start = 1, .end = 64 },
	{ .start = 512, .end = 512 },
	{ .start = 1024, .end = 1024 },
	{ .start = 2048, .end = 2048 },
	{ .start = 4096, .end = 4096 },
};

/* Ordered by range start value */
static const struct mlxsw_sp_adj_grp_size_range
mlxsw_sp2_adj_grp_size_ranges[] = {
	{ .start = 1, .end = 128 },
	{ .start = 256, .end = 256 },
	{ .start = 512, .end = 512 },
	{ .start = 1024, .end = 1024 },
	{ .start = 2048, .end = 2048 },
	{ .start = 4096, .end = 4096 },
};

static void mlxsw_sp_adj_grp_size_round_up(const struct mlxsw_sp *mlxsw_sp,
					   u16 *p_adj_grp_size)
{
	int i;

	for (i = 0; i < mlxsw_sp->router->adj_grp_size_ranges_count; i++) {
		const struct mlxsw_sp_adj_grp_size_range *size_range;

		size_range = &mlxsw_sp->router->adj_grp_size_ranges[i];

		if (*p_adj_grp_size >= size_range->start &&
		    *p_adj_grp_size <= size_range->end)
			return;

		if (*p_adj_grp_size <= size_range->end) {
			*p_adj_grp_size = size_range->end;
			return;
		}
	}
}

static void mlxsw_sp_adj_grp_size_round_down(const struct mlxsw_sp *mlxsw_sp,
					     u16 *p_adj_grp_size,
					     unsigned int alloc_size)
{
	int i;

	for (i = mlxsw_sp->router->adj_grp_size_ranges_count - 1; i >= 0; i--) {
		const struct mlxsw_sp_adj_grp_size_range *size_range;

		size_range = &mlxsw_sp->router->adj_grp_size_ranges[i];

		if (alloc_size >= size_range->end) {
			*p_adj_grp_size = size_range->end;
			return;
		}
	}
}

static int mlxsw_sp_fix_adj_grp_size(struct mlxsw_sp *mlxsw_sp,
				     u16 *p_adj_grp_size)
{
	unsigned int alloc_size;
	int err;

	/* Round up the requested group size to the next size supported
	 * by the device and make sure the request can be satisfied.
	 */
	mlxsw_sp_adj_grp_size_round_up(mlxsw_sp, p_adj_grp_size);
	err = mlxsw_sp_kvdl_alloc_count_query(mlxsw_sp,
					      MLXSW_SP_KVDL_ENTRY_TYPE_ADJ,
					      *p_adj_grp_size, &alloc_size);
	if (err)
		return err;
	/* It is possible the allocation results in more allocated
	 * entries than requested. Try to use as much of them as
	 * possible.
	 */
	mlxsw_sp_adj_grp_size_round_down(mlxsw_sp, p_adj_grp_size, alloc_size);

	return 0;
}

static void
mlxsw_sp_nexthop_group_normalize(struct mlxsw_sp_nexthop_group_info *nhgi)
{
	int i, g = 0, sum_norm_weight = 0;
	struct mlxsw_sp_nexthop *nh;

	for (i = 0; i < nhgi->count; i++) {
		nh = &nhgi->nexthops[i];

		if (!nh->should_offload)
			continue;
		if (g > 0)
			g = gcd(nh->nh_weight, g);
		else
			g = nh->nh_weight;
	}

	for (i = 0; i < nhgi->count; i++) {
		nh = &nhgi->nexthops[i];

		if (!nh->should_offload)
			continue;
		nh->norm_nh_weight = nh->nh_weight / g;
		sum_norm_weight += nh->norm_nh_weight;
	}

	nhgi->sum_norm_weight = sum_norm_weight;
}

static void
mlxsw_sp_nexthop_group_rebalance(struct mlxsw_sp_nexthop_group_info *nhgi)
{
	int i, weight = 0, lower_bound = 0;
	int total = nhgi->sum_norm_weight;
	u16 ecmp_size = nhgi->ecmp_size;

	for (i = 0; i < nhgi->count; i++) {
		struct mlxsw_sp_nexthop *nh = &nhgi->nexthops[i];
		int upper_bound;

		if (!nh->should_offload)
			continue;
		weight += nh->norm_nh_weight;
		upper_bound = DIV_ROUND_CLOSEST(ecmp_size * weight, total);
		nh->num_adj_entries = upper_bound - lower_bound;
		lower_bound = upper_bound;
	}
}

static struct mlxsw_sp_nexthop *
mlxsw_sp_rt6_nexthop(struct mlxsw_sp_nexthop_group *nh_grp,
		     const struct mlxsw_sp_rt6 *mlxsw_sp_rt6);

static void
mlxsw_sp_nexthop4_group_offload_refresh(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_nexthop_group *nh_grp)
{
	int i;

	for (i = 0; i < nh_grp->nhgi->count; i++) {
		struct mlxsw_sp_nexthop *nh = &nh_grp->nhgi->nexthops[i];

		if (nh->offloaded)
			nh->key.fib_nh->fib_nh_flags |= RTNH_F_OFFLOAD;
		else
			nh->key.fib_nh->fib_nh_flags &= ~RTNH_F_OFFLOAD;
	}
}

static void
__mlxsw_sp_nexthop6_group_offload_refresh(struct mlxsw_sp_nexthop_group *nh_grp,
					  struct mlxsw_sp_fib6_entry *fib6_entry)
{
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6;

	list_for_each_entry(mlxsw_sp_rt6, &fib6_entry->rt6_list, list) {
		struct fib6_nh *fib6_nh = mlxsw_sp_rt6->rt->fib6_nh;
		struct mlxsw_sp_nexthop *nh;

		nh = mlxsw_sp_rt6_nexthop(nh_grp, mlxsw_sp_rt6);
		if (nh && nh->offloaded)
			fib6_nh->fib_nh_flags |= RTNH_F_OFFLOAD;
		else
			fib6_nh->fib_nh_flags &= ~RTNH_F_OFFLOAD;
	}
}

static void
mlxsw_sp_nexthop6_group_offload_refresh(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_nexthop_group *nh_grp)
{
	struct mlxsw_sp_fib6_entry *fib6_entry;

	/* Unfortunately, in IPv6 the route and the nexthop are described by
	 * the same struct, so we need to iterate over all the routes using the
	 * nexthop group and set / clear the offload indication for them.
	 */
	list_for_each_entry(fib6_entry, &nh_grp->fib_list,
			    common.nexthop_group_node)
		__mlxsw_sp_nexthop6_group_offload_refresh(nh_grp, fib6_entry);
}

static void
mlxsw_sp_nexthop_bucket_offload_refresh(struct mlxsw_sp *mlxsw_sp,
					const struct mlxsw_sp_nexthop *nh,
					u16 bucket_index)
{
	struct mlxsw_sp_nexthop_group *nh_grp = nh->nhgi->nh_grp;
	bool offload = false, trap = false;

	if (nh->offloaded) {
		if (nh->action == MLXSW_SP_NEXTHOP_ACTION_TRAP)
			trap = true;
		else
			offload = true;
	}
	nexthop_bucket_set_hw_flags(mlxsw_sp_net(mlxsw_sp), nh_grp->obj.id,
				    bucket_index, offload, trap);
}

static void
mlxsw_sp_nexthop_obj_group_offload_refresh(struct mlxsw_sp *mlxsw_sp,
					   struct mlxsw_sp_nexthop_group *nh_grp)
{
	int i;

	/* Do not update the flags if the nexthop group is being destroyed
	 * since:
	 * 1. The nexthop objects is being deleted, in which case the flags are
	 * irrelevant.
	 * 2. The nexthop group was replaced by a newer group, in which case
	 * the flags of the nexthop object were already updated based on the
	 * new group.
	 */
	if (nh_grp->can_destroy)
		return;

	nexthop_set_hw_flags(mlxsw_sp_net(mlxsw_sp), nh_grp->obj.id,
			     nh_grp->nhgi->adj_index_valid, false);

	/* Update flags of individual nexthop buckets in case of a resilient
	 * nexthop group.
	 */
	if (!nh_grp->nhgi->is_resilient)
		return;

	for (i = 0; i < nh_grp->nhgi->count; i++) {
		struct mlxsw_sp_nexthop *nh = &nh_grp->nhgi->nexthops[i];

		mlxsw_sp_nexthop_bucket_offload_refresh(mlxsw_sp, nh, i);
	}
}

static void
mlxsw_sp_nexthop_group_offload_refresh(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_nexthop_group *nh_grp)
{
	switch (nh_grp->type) {
	case MLXSW_SP_NEXTHOP_GROUP_TYPE_IPV4:
		mlxsw_sp_nexthop4_group_offload_refresh(mlxsw_sp, nh_grp);
		break;
	case MLXSW_SP_NEXTHOP_GROUP_TYPE_IPV6:
		mlxsw_sp_nexthop6_group_offload_refresh(mlxsw_sp, nh_grp);
		break;
	case MLXSW_SP_NEXTHOP_GROUP_TYPE_OBJ:
		mlxsw_sp_nexthop_obj_group_offload_refresh(mlxsw_sp, nh_grp);
		break;
	}
}

static int
mlxsw_sp_nexthop_group_refresh(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_nexthop_group *nh_grp)
{
	struct mlxsw_sp_nexthop_group_info *nhgi = nh_grp->nhgi;
	u16 ecmp_size, old_ecmp_size;
	struct mlxsw_sp_nexthop *nh;
	bool offload_change = false;
	u32 adj_index;
	bool old_adj_index_valid;
	u32 old_adj_index;
	int i, err2, err;

	if (!nhgi->gateway)
		return mlxsw_sp_nexthop_fib_entries_update(mlxsw_sp, nh_grp);

	for (i = 0; i < nhgi->count; i++) {
		nh = &nhgi->nexthops[i];

		if (nh->should_offload != nh->offloaded) {
			offload_change = true;
			if (nh->should_offload)
				nh->update = 1;
		}
	}
	if (!offload_change) {
		/* Nothing was added or removed, so no need to reallocate. Just
		 * update MAC on existing adjacency indexes.
		 */
		err = mlxsw_sp_nexthop_group_update(mlxsw_sp, nhgi, false);
		if (err) {
			dev_warn(mlxsw_sp->bus_info->dev, "Failed to update neigh MAC in adjacency table.\n");
			goto set_trap;
		}
		/* Flags of individual nexthop buckets might need to be
		 * updated.
		 */
		mlxsw_sp_nexthop_group_offload_refresh(mlxsw_sp, nh_grp);
		return 0;
	}
	mlxsw_sp_nexthop_group_normalize(nhgi);
	if (!nhgi->sum_norm_weight) {
		/* No neigh of this group is connected so we just set
		 * the trap and let everthing flow through kernel.
		 */
		err = 0;
		goto set_trap;
	}

	ecmp_size = nhgi->sum_norm_weight;
	err = mlxsw_sp_fix_adj_grp_size(mlxsw_sp, &ecmp_size);
	if (err)
		/* No valid allocation size available. */
		goto set_trap;

	err = mlxsw_sp_kvdl_alloc(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_ADJ,
				  ecmp_size, &adj_index);
	if (err) {
		/* We ran out of KVD linear space, just set the
		 * trap and let everything flow through kernel.
		 */
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to allocate KVD linear area for nexthop group.\n");
		goto set_trap;
	}
	old_adj_index_valid = nhgi->adj_index_valid;
	old_adj_index = nhgi->adj_index;
	old_ecmp_size = nhgi->ecmp_size;
	nhgi->adj_index_valid = 1;
	nhgi->adj_index = adj_index;
	nhgi->ecmp_size = ecmp_size;
	mlxsw_sp_nexthop_group_rebalance(nhgi);
	err = mlxsw_sp_nexthop_group_update(mlxsw_sp, nhgi, true);
	if (err) {
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to update neigh MAC in adjacency table.\n");
		goto set_trap;
	}

	mlxsw_sp_nexthop_group_offload_refresh(mlxsw_sp, nh_grp);

	if (!old_adj_index_valid) {
		/* The trap was set for fib entries, so we have to call
		 * fib entry update to unset it and use adjacency index.
		 */
		err = mlxsw_sp_nexthop_fib_entries_update(mlxsw_sp, nh_grp);
		if (err) {
			dev_warn(mlxsw_sp->bus_info->dev, "Failed to add adjacency index to fib entries.\n");
			goto set_trap;
		}
		return 0;
	}

	err = mlxsw_sp_adj_index_mass_update(mlxsw_sp, nh_grp,
					     old_adj_index, old_ecmp_size);
	mlxsw_sp_kvdl_free(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_ADJ,
			   old_ecmp_size, old_adj_index);
	if (err) {
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to mass-update adjacency index for nexthop group.\n");
		goto set_trap;
	}

	return 0;

set_trap:
	old_adj_index_valid = nhgi->adj_index_valid;
	nhgi->adj_index_valid = 0;
	for (i = 0; i < nhgi->count; i++) {
		nh = &nhgi->nexthops[i];
		nh->offloaded = 0;
	}
	err2 = mlxsw_sp_nexthop_fib_entries_update(mlxsw_sp, nh_grp);
	if (err2)
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to set traps for fib entries.\n");
	mlxsw_sp_nexthop_group_offload_refresh(mlxsw_sp, nh_grp);
	if (old_adj_index_valid)
		mlxsw_sp_kvdl_free(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_ADJ,
				   nhgi->ecmp_size, nhgi->adj_index);
	return err;
}

static void __mlxsw_sp_nexthop_neigh_update(struct mlxsw_sp_nexthop *nh,
					    bool removing)
{
	if (!removing) {
		nh->action = MLXSW_SP_NEXTHOP_ACTION_FORWARD;
		nh->should_offload = 1;
	} else if (nh->nhgi->is_resilient) {
		nh->action = MLXSW_SP_NEXTHOP_ACTION_TRAP;
		nh->should_offload = 1;
	} else {
		nh->should_offload = 0;
	}
	nh->update = 1;
}

static int
mlxsw_sp_nexthop_dead_neigh_replace(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_neigh_entry *neigh_entry)
{
	struct neighbour *n, *old_n = neigh_entry->key.n;
	struct mlxsw_sp_nexthop *nh;
	struct net_device *dev;
	bool entry_connected;
	u8 nud_state, dead;
	int err;

	nh = list_first_entry(&neigh_entry->nexthop_list,
			      struct mlxsw_sp_nexthop, neigh_list_node);
	dev = mlxsw_sp_nexthop_dev(nh);

	n = neigh_lookup(nh->neigh_tbl, &nh->gw_addr, dev);
	if (!n) {
		n = neigh_create(nh->neigh_tbl, &nh->gw_addr, dev);
		if (IS_ERR(n))
			return PTR_ERR(n);
		neigh_event_send(n, NULL);
	}

	mlxsw_sp_neigh_entry_remove(mlxsw_sp, neigh_entry);
	neigh_entry->key.n = n;
	err = mlxsw_sp_neigh_entry_insert(mlxsw_sp, neigh_entry);
	if (err)
		goto err_neigh_entry_insert;

	read_lock_bh(&n->lock);
	nud_state = n->nud_state;
	dead = n->dead;
	read_unlock_bh(&n->lock);
	entry_connected = nud_state & NUD_VALID && !dead;

	list_for_each_entry(nh, &neigh_entry->nexthop_list,
			    neigh_list_node) {
		neigh_release(old_n);
		neigh_clone(n);
		__mlxsw_sp_nexthop_neigh_update(nh, !entry_connected);
		mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh->nhgi->nh_grp);
	}

	neigh_release(n);

	return 0;

err_neigh_entry_insert:
	neigh_entry->key.n = old_n;
	mlxsw_sp_neigh_entry_insert(mlxsw_sp, neigh_entry);
	neigh_release(n);
	return err;
}

static void
mlxsw_sp_nexthop_neigh_update(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_neigh_entry *neigh_entry,
			      bool removing, bool dead)
{
	struct mlxsw_sp_nexthop *nh;

	if (list_empty(&neigh_entry->nexthop_list))
		return;

	if (dead) {
		int err;

		err = mlxsw_sp_nexthop_dead_neigh_replace(mlxsw_sp,
							  neigh_entry);
		if (err)
			dev_err(mlxsw_sp->bus_info->dev, "Failed to replace dead neigh\n");
		return;
	}

	list_for_each_entry(nh, &neigh_entry->nexthop_list,
			    neigh_list_node) {
		__mlxsw_sp_nexthop_neigh_update(nh, removing);
		mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh->nhgi->nh_grp);
	}
}

static void mlxsw_sp_nexthop_crif_init(struct mlxsw_sp_nexthop *nh,
				       struct mlxsw_sp_crif *crif)
{
	if (nh->crif)
		return;

	nh->crif = crif;
	list_add(&nh->crif_list_node, &crif->nexthop_list);
}

static void mlxsw_sp_nexthop_crif_fini(struct mlxsw_sp_nexthop *nh)
{
	if (!nh->crif)
		return;

	list_del(&nh->crif_list_node);
	nh->crif = NULL;
}

static int mlxsw_sp_nexthop_neigh_init(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_nexthop *nh)
{
	struct mlxsw_sp_neigh_entry *neigh_entry;
	struct net_device *dev;
	struct neighbour *n;
	u8 nud_state, dead;
	int err;

	if (WARN_ON(!nh->crif->rif))
		return 0;

	if (!nh->nhgi->gateway || nh->neigh_entry)
		return 0;
	dev = mlxsw_sp_nexthop_dev(nh);

	/* Take a reference of neigh here ensuring that neigh would
	 * not be destructed before the nexthop entry is finished.
	 * The reference is taken either in neigh_lookup() or
	 * in neigh_create() in case n is not found.
	 */
	n = neigh_lookup(nh->neigh_tbl, &nh->gw_addr, dev);
	if (!n) {
		n = neigh_create(nh->neigh_tbl, &nh->gw_addr, dev);
		if (IS_ERR(n))
			return PTR_ERR(n);
		neigh_event_send(n, NULL);
	}
	neigh_entry = mlxsw_sp_neigh_entry_lookup(mlxsw_sp, n);
	if (!neigh_entry) {
		neigh_entry = mlxsw_sp_neigh_entry_create(mlxsw_sp, n);
		if (IS_ERR(neigh_entry)) {
			err = -EINVAL;
			goto err_neigh_entry_create;
		}
	}

	/* If that is the first nexthop connected to that neigh, add to
	 * nexthop_neighs_list
	 */
	if (list_empty(&neigh_entry->nexthop_list))
		list_add_tail(&neigh_entry->nexthop_neighs_list_node,
			      &mlxsw_sp->router->nexthop_neighs_list);

	nh->neigh_entry = neigh_entry;
	list_add_tail(&nh->neigh_list_node, &neigh_entry->nexthop_list);
	read_lock_bh(&n->lock);
	nud_state = n->nud_state;
	dead = n->dead;
	read_unlock_bh(&n->lock);
	__mlxsw_sp_nexthop_neigh_update(nh, !(nud_state & NUD_VALID && !dead));

	return 0;

err_neigh_entry_create:
	neigh_release(n);
	return err;
}

static void mlxsw_sp_nexthop_neigh_fini(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_nexthop *nh)
{
	struct mlxsw_sp_neigh_entry *neigh_entry = nh->neigh_entry;
	struct neighbour *n;

	if (!neigh_entry)
		return;
	n = neigh_entry->key.n;

	__mlxsw_sp_nexthop_neigh_update(nh, true);
	list_del(&nh->neigh_list_node);
	nh->neigh_entry = NULL;

	/* If that is the last nexthop connected to that neigh, remove from
	 * nexthop_neighs_list
	 */
	if (list_empty(&neigh_entry->nexthop_list))
		list_del(&neigh_entry->nexthop_neighs_list_node);

	if (!neigh_entry->connected && list_empty(&neigh_entry->nexthop_list))
		mlxsw_sp_neigh_entry_destroy(mlxsw_sp, neigh_entry);

	neigh_release(n);
}

static bool mlxsw_sp_ipip_netdev_ul_up(struct net_device *ol_dev)
{
	struct net_device *ul_dev;
	bool is_up;

	rcu_read_lock();
	ul_dev = mlxsw_sp_ipip_netdev_ul_dev_get(ol_dev);
	is_up = ul_dev ? (ul_dev->flags & IFF_UP) : true;
	rcu_read_unlock();

	return is_up;
}

static void mlxsw_sp_nexthop_ipip_init(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_nexthop *nh,
				       struct mlxsw_sp_ipip_entry *ipip_entry)
{
	struct mlxsw_sp_crif *crif;
	bool removing;

	if (!nh->nhgi->gateway || nh->ipip_entry)
		return;

	crif = mlxsw_sp_crif_lookup(mlxsw_sp->router, ipip_entry->ol_dev);
	if (WARN_ON(!crif))
		return;

	nh->ipip_entry = ipip_entry;
	removing = !mlxsw_sp_ipip_netdev_ul_up(ipip_entry->ol_dev);
	__mlxsw_sp_nexthop_neigh_update(nh, removing);
	mlxsw_sp_nexthop_crif_init(nh, crif);
}

static void mlxsw_sp_nexthop_ipip_fini(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_nexthop *nh)
{
	struct mlxsw_sp_ipip_entry *ipip_entry = nh->ipip_entry;

	if (!ipip_entry)
		return;

	__mlxsw_sp_nexthop_neigh_update(nh, true);
	nh->ipip_entry = NULL;
}

static bool mlxsw_sp_nexthop4_ipip_type(const struct mlxsw_sp *mlxsw_sp,
					const struct fib_nh *fib_nh,
					enum mlxsw_sp_ipip_type *p_ipipt)
{
	struct net_device *dev = fib_nh->fib_nh_dev;

	return dev &&
	       fib_nh->nh_parent->fib_type == RTN_UNICAST &&
	       mlxsw_sp_netdev_ipip_type(mlxsw_sp, dev, p_ipipt);
}

static int mlxsw_sp_nexthop_type_init(struct mlxsw_sp *mlxsw_sp,
				      struct mlxsw_sp_nexthop *nh,
				      const struct net_device *dev)
{
	const struct mlxsw_sp_ipip_ops *ipip_ops;
	struct mlxsw_sp_ipip_entry *ipip_entry;
	struct mlxsw_sp_crif *crif;
	int err;

	ipip_entry = mlxsw_sp_ipip_entry_find_by_ol_dev(mlxsw_sp, dev);
	if (ipip_entry) {
		ipip_ops = mlxsw_sp->router->ipip_ops_arr[ipip_entry->ipipt];
		if (ipip_ops->can_offload(mlxsw_sp, dev)) {
			nh->type = MLXSW_SP_NEXTHOP_TYPE_IPIP;
			mlxsw_sp_nexthop_ipip_init(mlxsw_sp, nh, ipip_entry);
			return 0;
		}
	}

	nh->type = MLXSW_SP_NEXTHOP_TYPE_ETH;
	crif = mlxsw_sp_crif_lookup(mlxsw_sp->router, dev);
	if (!crif)
		return 0;

	mlxsw_sp_nexthop_crif_init(nh, crif);

	if (!crif->rif)
		return 0;

	err = mlxsw_sp_nexthop_neigh_init(mlxsw_sp, nh);
	if (err)
		goto err_neigh_init;

	return 0;

err_neigh_init:
	mlxsw_sp_nexthop_crif_fini(nh);
	return err;
}

static void mlxsw_sp_nexthop_type_rif_gone(struct mlxsw_sp *mlxsw_sp,
					   struct mlxsw_sp_nexthop *nh)
{
	switch (nh->type) {
	case MLXSW_SP_NEXTHOP_TYPE_ETH:
		mlxsw_sp_nexthop_neigh_fini(mlxsw_sp, nh);
		break;
	case MLXSW_SP_NEXTHOP_TYPE_IPIP:
		mlxsw_sp_nexthop_ipip_fini(mlxsw_sp, nh);
		break;
	}
}

static void mlxsw_sp_nexthop_type_fini(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_nexthop *nh)
{
	mlxsw_sp_nexthop_type_rif_gone(mlxsw_sp, nh);
	mlxsw_sp_nexthop_crif_fini(nh);
}

static int mlxsw_sp_nexthop4_init(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_nexthop_group *nh_grp,
				  struct mlxsw_sp_nexthop *nh,
				  struct fib_nh *fib_nh)
{
	struct net_device *dev = fib_nh->fib_nh_dev;
	struct in_device *in_dev;
	int err;

	nh->nhgi = nh_grp->nhgi;
	nh->key.fib_nh = fib_nh;
#ifdef CONFIG_IP_ROUTE_MULTIPATH
	nh->nh_weight = fib_nh->fib_nh_weight;
#else
	nh->nh_weight = 1;
#endif
	memcpy(&nh->gw_addr, &fib_nh->fib_nh_gw4, sizeof(fib_nh->fib_nh_gw4));
	nh->neigh_tbl = &arp_tbl;
	err = mlxsw_sp_nexthop_insert(mlxsw_sp, nh);
	if (err)
		return err;

	mlxsw_sp_nexthop_counter_alloc(mlxsw_sp, nh);
	list_add_tail(&nh->router_list_node, &mlxsw_sp->router->nexthop_list);

	if (!dev)
		return 0;
	nh->ifindex = dev->ifindex;

	rcu_read_lock();
	in_dev = __in_dev_get_rcu(dev);
	if (in_dev && IN_DEV_IGNORE_ROUTES_WITH_LINKDOWN(in_dev) &&
	    fib_nh->fib_nh_flags & RTNH_F_LINKDOWN) {
		rcu_read_unlock();
		return 0;
	}
	rcu_read_unlock();

	err = mlxsw_sp_nexthop_type_init(mlxsw_sp, nh, dev);
	if (err)
		goto err_nexthop_neigh_init;

	return 0;

err_nexthop_neigh_init:
	list_del(&nh->router_list_node);
	mlxsw_sp_nexthop_counter_free(mlxsw_sp, nh);
	mlxsw_sp_nexthop_remove(mlxsw_sp, nh);
	return err;
}

static void mlxsw_sp_nexthop4_fini(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_nexthop *nh)
{
	mlxsw_sp_nexthop_type_fini(mlxsw_sp, nh);
	list_del(&nh->router_list_node);
	mlxsw_sp_nexthop_counter_free(mlxsw_sp, nh);
	mlxsw_sp_nexthop_remove(mlxsw_sp, nh);
}

static void mlxsw_sp_nexthop4_event(struct mlxsw_sp *mlxsw_sp,
				    unsigned long event, struct fib_nh *fib_nh)
{
	struct mlxsw_sp_nexthop_key key;
	struct mlxsw_sp_nexthop *nh;

	key.fib_nh = fib_nh;
	nh = mlxsw_sp_nexthop_lookup(mlxsw_sp, key);
	if (!nh)
		return;

	switch (event) {
	case FIB_EVENT_NH_ADD:
		mlxsw_sp_nexthop_type_init(mlxsw_sp, nh, fib_nh->fib_nh_dev);
		break;
	case FIB_EVENT_NH_DEL:
		mlxsw_sp_nexthop_type_fini(mlxsw_sp, nh);
		break;
	}

	mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh->nhgi->nh_grp);
}

static void mlxsw_sp_nexthop_rif_update(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_rif *rif)
{
	struct net_device *dev = mlxsw_sp_rif_dev(rif);
	struct mlxsw_sp_nexthop *nh;
	bool removing;

	list_for_each_entry(nh, &rif->crif->nexthop_list, crif_list_node) {
		switch (nh->type) {
		case MLXSW_SP_NEXTHOP_TYPE_ETH:
			removing = false;
			break;
		case MLXSW_SP_NEXTHOP_TYPE_IPIP:
			removing = !mlxsw_sp_ipip_netdev_ul_up(dev);
			break;
		default:
			WARN_ON(1);
			continue;
		}

		__mlxsw_sp_nexthop_neigh_update(nh, removing);
		mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh->nhgi->nh_grp);
	}
}

static void mlxsw_sp_nexthop_rif_gone_sync(struct mlxsw_sp *mlxsw_sp,
					   struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp_nexthop *nh, *tmp;

	list_for_each_entry_safe(nh, tmp, &rif->crif->nexthop_list,
				 crif_list_node) {
		mlxsw_sp_nexthop_type_rif_gone(mlxsw_sp, nh);
		mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh->nhgi->nh_grp);
	}
}

static int mlxsw_sp_adj_trap_entry_init(struct mlxsw_sp *mlxsw_sp)
{
	enum mlxsw_reg_ratr_trap_action trap_action;
	char ratr_pl[MLXSW_REG_RATR_LEN];
	int err;

	err = mlxsw_sp_kvdl_alloc(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_ADJ, 1,
				  &mlxsw_sp->router->adj_trap_index);
	if (err)
		return err;

	trap_action = MLXSW_REG_RATR_TRAP_ACTION_TRAP;
	mlxsw_reg_ratr_pack(ratr_pl, MLXSW_REG_RATR_OP_WRITE_WRITE_ENTRY, true,
			    MLXSW_REG_RATR_TYPE_ETHERNET,
			    mlxsw_sp->router->adj_trap_index,
			    mlxsw_sp->router->lb_crif->rif->rif_index);
	mlxsw_reg_ratr_trap_action_set(ratr_pl, trap_action);
	mlxsw_reg_ratr_trap_id_set(ratr_pl, MLXSW_TRAP_ID_RTR_EGRESS0);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ratr), ratr_pl);
	if (err)
		goto err_ratr_write;

	return 0;

err_ratr_write:
	mlxsw_sp_kvdl_free(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_ADJ, 1,
			   mlxsw_sp->router->adj_trap_index);
	return err;
}

static void mlxsw_sp_adj_trap_entry_fini(struct mlxsw_sp *mlxsw_sp)
{
	mlxsw_sp_kvdl_free(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_ADJ, 1,
			   mlxsw_sp->router->adj_trap_index);
}

static int mlxsw_sp_nexthop_group_inc(struct mlxsw_sp *mlxsw_sp)
{
	int err;

	if (refcount_inc_not_zero(&mlxsw_sp->router->num_groups))
		return 0;

	err = mlxsw_sp_adj_trap_entry_init(mlxsw_sp);
	if (err)
		return err;

	refcount_set(&mlxsw_sp->router->num_groups, 1);

	return 0;
}

static void mlxsw_sp_nexthop_group_dec(struct mlxsw_sp *mlxsw_sp)
{
	if (!refcount_dec_and_test(&mlxsw_sp->router->num_groups))
		return;

	mlxsw_sp_adj_trap_entry_fini(mlxsw_sp);
}

static void
mlxsw_sp_nh_grp_activity_get(struct mlxsw_sp *mlxsw_sp,
			     const struct mlxsw_sp_nexthop_group *nh_grp,
			     unsigned long *activity)
{
	char *ratrad_pl;
	int i, err;

	ratrad_pl = kmalloc(MLXSW_REG_RATRAD_LEN, GFP_KERNEL);
	if (!ratrad_pl)
		return;

	mlxsw_reg_ratrad_pack(ratrad_pl, nh_grp->nhgi->adj_index,
			      nh_grp->nhgi->count);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ratrad), ratrad_pl);
	if (err)
		goto out;

	for (i = 0; i < nh_grp->nhgi->count; i++) {
		if (!mlxsw_reg_ratrad_activity_vector_get(ratrad_pl, i))
			continue;
		bitmap_set(activity, i, 1);
	}

out:
	kfree(ratrad_pl);
}

#define MLXSW_SP_NH_GRP_ACTIVITY_UPDATE_INTERVAL 1000 /* ms */

static void
mlxsw_sp_nh_grp_activity_update(struct mlxsw_sp *mlxsw_sp,
				const struct mlxsw_sp_nexthop_group *nh_grp)
{
	unsigned long *activity;

	activity = bitmap_zalloc(nh_grp->nhgi->count, GFP_KERNEL);
	if (!activity)
		return;

	mlxsw_sp_nh_grp_activity_get(mlxsw_sp, nh_grp, activity);
	nexthop_res_grp_activity_update(mlxsw_sp_net(mlxsw_sp), nh_grp->obj.id,
					nh_grp->nhgi->count, activity);

	bitmap_free(activity);
}

static void
mlxsw_sp_nh_grp_activity_work_schedule(struct mlxsw_sp *mlxsw_sp)
{
	unsigned int interval = MLXSW_SP_NH_GRP_ACTIVITY_UPDATE_INTERVAL;

	mlxsw_core_schedule_dw(&mlxsw_sp->router->nh_grp_activity_dw,
			       msecs_to_jiffies(interval));
}

static void mlxsw_sp_nh_grp_activity_work(struct work_struct *work)
{
	struct mlxsw_sp_nexthop_group_info *nhgi;
	struct mlxsw_sp_router *router;
	bool reschedule = false;

	router = container_of(work, struct mlxsw_sp_router,
			      nh_grp_activity_dw.work);

	mutex_lock(&router->lock);

	list_for_each_entry(nhgi, &router->nh_res_grp_list, list) {
		mlxsw_sp_nh_grp_activity_update(router->mlxsw_sp, nhgi->nh_grp);
		reschedule = true;
	}

	mutex_unlock(&router->lock);

	if (!reschedule)
		return;
	mlxsw_sp_nh_grp_activity_work_schedule(router->mlxsw_sp);
}

static int
mlxsw_sp_nexthop_obj_single_validate(struct mlxsw_sp *mlxsw_sp,
				     const struct nh_notifier_single_info *nh,
				     struct netlink_ext_ack *extack)
{
	int err = -EINVAL;

	if (nh->is_fdb)
		NL_SET_ERR_MSG_MOD(extack, "FDB nexthops are not supported");
	else if (nh->has_encap)
		NL_SET_ERR_MSG_MOD(extack, "Encapsulating nexthops are not supported");
	else
		err = 0;

	return err;
}

static int
mlxsw_sp_nexthop_obj_group_entry_validate(struct mlxsw_sp *mlxsw_sp,
					  const struct nh_notifier_single_info *nh,
					  struct netlink_ext_ack *extack)
{
	int err;

	err = mlxsw_sp_nexthop_obj_single_validate(mlxsw_sp, nh, extack);
	if (err)
		return err;

	/* Device only nexthops with an IPIP device are programmed as
	 * encapsulating adjacency entries.
	 */
	if (!nh->gw_family && !nh->is_reject &&
	    !mlxsw_sp_netdev_ipip_type(mlxsw_sp, nh->dev, NULL)) {
		NL_SET_ERR_MSG_MOD(extack, "Nexthop group entry does not have a gateway");
		return -EINVAL;
	}

	return 0;
}

static int
mlxsw_sp_nexthop_obj_group_validate(struct mlxsw_sp *mlxsw_sp,
				    const struct nh_notifier_grp_info *nh_grp,
				    struct netlink_ext_ack *extack)
{
	int i;

	if (nh_grp->is_fdb) {
		NL_SET_ERR_MSG_MOD(extack, "FDB nexthop groups are not supported");
		return -EINVAL;
	}

	for (i = 0; i < nh_grp->num_nh; i++) {
		const struct nh_notifier_single_info *nh;
		int err;

		nh = &nh_grp->nh_entries[i].nh;
		err = mlxsw_sp_nexthop_obj_group_entry_validate(mlxsw_sp, nh,
								extack);
		if (err)
			return err;
	}

	return 0;
}

static int
mlxsw_sp_nexthop_obj_res_group_size_validate(struct mlxsw_sp *mlxsw_sp,
					     const struct nh_notifier_res_table_info *nh_res_table,
					     struct netlink_ext_ack *extack)
{
	unsigned int alloc_size;
	bool valid_size = false;
	int err, i;

	if (nh_res_table->num_nh_buckets < 32) {
		NL_SET_ERR_MSG_MOD(extack, "Minimum number of buckets is 32");
		return -EINVAL;
	}

	for (i = 0; i < mlxsw_sp->router->adj_grp_size_ranges_count; i++) {
		const struct mlxsw_sp_adj_grp_size_range *size_range;

		size_range = &mlxsw_sp->router->adj_grp_size_ranges[i];

		if (nh_res_table->num_nh_buckets >= size_range->start &&
		    nh_res_table->num_nh_buckets <= size_range->end) {
			valid_size = true;
			break;
		}
	}

	if (!valid_size) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid number of buckets");
		return -EINVAL;
	}

	err = mlxsw_sp_kvdl_alloc_count_query(mlxsw_sp,
					      MLXSW_SP_KVDL_ENTRY_TYPE_ADJ,
					      nh_res_table->num_nh_buckets,
					      &alloc_size);
	if (err || nh_res_table->num_nh_buckets != alloc_size) {
		NL_SET_ERR_MSG_MOD(extack, "Number of buckets does not fit allocation size of any KVDL partition");
		return -EINVAL;
	}

	return 0;
}

static int
mlxsw_sp_nexthop_obj_res_group_validate(struct mlxsw_sp *mlxsw_sp,
					const struct nh_notifier_res_table_info *nh_res_table,
					struct netlink_ext_ack *extack)
{
	int err;
	u16 i;

	err = mlxsw_sp_nexthop_obj_res_group_size_validate(mlxsw_sp,
							   nh_res_table,
							   extack);
	if (err)
		return err;

	for (i = 0; i < nh_res_table->num_nh_buckets; i++) {
		const struct nh_notifier_single_info *nh;
		int err;

		nh = &nh_res_table->nhs[i];
		err = mlxsw_sp_nexthop_obj_group_entry_validate(mlxsw_sp, nh,
								extack);
		if (err)
			return err;
	}

	return 0;
}

static int mlxsw_sp_nexthop_obj_validate(struct mlxsw_sp *mlxsw_sp,
					 unsigned long event,
					 struct nh_notifier_info *info)
{
	struct nh_notifier_single_info *nh;

	if (event != NEXTHOP_EVENT_REPLACE &&
	    event != NEXTHOP_EVENT_RES_TABLE_PRE_REPLACE &&
	    event != NEXTHOP_EVENT_BUCKET_REPLACE)
		return 0;

	switch (info->type) {
	case NH_NOTIFIER_INFO_TYPE_SINGLE:
		return mlxsw_sp_nexthop_obj_single_validate(mlxsw_sp, info->nh,
							    info->extack);
	case NH_NOTIFIER_INFO_TYPE_GRP:
		return mlxsw_sp_nexthop_obj_group_validate(mlxsw_sp,
							   info->nh_grp,
							   info->extack);
	case NH_NOTIFIER_INFO_TYPE_RES_TABLE:
		return mlxsw_sp_nexthop_obj_res_group_validate(mlxsw_sp,
							       info->nh_res_table,
							       info->extack);
	case NH_NOTIFIER_INFO_TYPE_RES_BUCKET:
		nh = &info->nh_res_bucket->new_nh;
		return mlxsw_sp_nexthop_obj_group_entry_validate(mlxsw_sp, nh,
								 info->extack);
	default:
		NL_SET_ERR_MSG_MOD(info->extack, "Unsupported nexthop type");
		return -EOPNOTSUPP;
	}
}

static bool mlxsw_sp_nexthop_obj_is_gateway(struct mlxsw_sp *mlxsw_sp,
					    const struct nh_notifier_info *info)
{
	const struct net_device *dev;

	switch (info->type) {
	case NH_NOTIFIER_INFO_TYPE_SINGLE:
		dev = info->nh->dev;
		return info->nh->gw_family || info->nh->is_reject ||
		       mlxsw_sp_netdev_ipip_type(mlxsw_sp, dev, NULL);
	case NH_NOTIFIER_INFO_TYPE_GRP:
	case NH_NOTIFIER_INFO_TYPE_RES_TABLE:
		/* Already validated earlier. */
		return true;
	default:
		return false;
	}
}

static void mlxsw_sp_nexthop_obj_blackhole_init(struct mlxsw_sp *mlxsw_sp,
						struct mlxsw_sp_nexthop *nh)
{
	nh->action = MLXSW_SP_NEXTHOP_ACTION_DISCARD;
	nh->should_offload = 1;
	/* While nexthops that discard packets do not forward packets
	 * via an egress RIF, they still need to be programmed using a
	 * valid RIF, so use the loopback RIF created during init.
	 */
	nh->crif = mlxsw_sp->router->lb_crif;
}

static void mlxsw_sp_nexthop_obj_blackhole_fini(struct mlxsw_sp *mlxsw_sp,
						struct mlxsw_sp_nexthop *nh)
{
	nh->crif = NULL;
	nh->should_offload = 0;
}

static int
mlxsw_sp_nexthop_obj_init(struct mlxsw_sp *mlxsw_sp,
			  struct mlxsw_sp_nexthop_group *nh_grp,
			  struct mlxsw_sp_nexthop *nh,
			  struct nh_notifier_single_info *nh_obj, int weight)
{
	struct net_device *dev = nh_obj->dev;
	int err;

	nh->nhgi = nh_grp->nhgi;
	nh->nh_weight = weight;

	switch (nh_obj->gw_family) {
	case AF_INET:
		memcpy(&nh->gw_addr, &nh_obj->ipv4, sizeof(nh_obj->ipv4));
		nh->neigh_tbl = &arp_tbl;
		break;
	case AF_INET6:
		memcpy(&nh->gw_addr, &nh_obj->ipv6, sizeof(nh_obj->ipv6));
#if IS_ENABLED(CONFIG_IPV6)
		nh->neigh_tbl = &nd_tbl;
#endif
		break;
	}

	mlxsw_sp_nexthop_counter_alloc(mlxsw_sp, nh);
	list_add_tail(&nh->router_list_node, &mlxsw_sp->router->nexthop_list);
	nh->ifindex = dev->ifindex;

	err = mlxsw_sp_nexthop_type_init(mlxsw_sp, nh, dev);
	if (err)
		goto err_type_init;

	if (nh_obj->is_reject)
		mlxsw_sp_nexthop_obj_blackhole_init(mlxsw_sp, nh);

	/* In a resilient nexthop group, all the nexthops must be written to
	 * the adjacency table. Even if they do not have a valid neighbour or
	 * RIF.
	 */
	if (nh_grp->nhgi->is_resilient && !nh->should_offload) {
		nh->action = MLXSW_SP_NEXTHOP_ACTION_TRAP;
		nh->should_offload = 1;
	}

	return 0;

err_type_init:
	list_del(&nh->router_list_node);
	mlxsw_sp_nexthop_counter_free(mlxsw_sp, nh);
	return err;
}

static void mlxsw_sp_nexthop_obj_fini(struct mlxsw_sp *mlxsw_sp,
				      struct mlxsw_sp_nexthop *nh)
{
	if (nh->action == MLXSW_SP_NEXTHOP_ACTION_DISCARD)
		mlxsw_sp_nexthop_obj_blackhole_fini(mlxsw_sp, nh);
	mlxsw_sp_nexthop_type_fini(mlxsw_sp, nh);
	list_del(&nh->router_list_node);
	mlxsw_sp_nexthop_counter_free(mlxsw_sp, nh);
	nh->should_offload = 0;
}

static int
mlxsw_sp_nexthop_obj_group_info_init(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_nexthop_group *nh_grp,
				     struct nh_notifier_info *info)
{
	struct mlxsw_sp_nexthop_group_info *nhgi;
	struct mlxsw_sp_nexthop *nh;
	bool is_resilient = false;
	unsigned int nhs;
	int err, i;

	switch (info->type) {
	case NH_NOTIFIER_INFO_TYPE_SINGLE:
		nhs = 1;
		break;
	case NH_NOTIFIER_INFO_TYPE_GRP:
		nhs = info->nh_grp->num_nh;
		break;
	case NH_NOTIFIER_INFO_TYPE_RES_TABLE:
		nhs = info->nh_res_table->num_nh_buckets;
		is_resilient = true;
		break;
	default:
		return -EINVAL;
	}

	nhgi = kzalloc(struct_size(nhgi, nexthops, nhs), GFP_KERNEL);
	if (!nhgi)
		return -ENOMEM;
	nh_grp->nhgi = nhgi;
	nhgi->nh_grp = nh_grp;
	nhgi->gateway = mlxsw_sp_nexthop_obj_is_gateway(mlxsw_sp, info);
	nhgi->is_resilient = is_resilient;
	nhgi->count = nhs;
	for (i = 0; i < nhgi->count; i++) {
		struct nh_notifier_single_info *nh_obj;
		int weight;

		nh = &nhgi->nexthops[i];
		switch (info->type) {
		case NH_NOTIFIER_INFO_TYPE_SINGLE:
			nh_obj = info->nh;
			weight = 1;
			break;
		case NH_NOTIFIER_INFO_TYPE_GRP:
			nh_obj = &info->nh_grp->nh_entries[i].nh;
			weight = info->nh_grp->nh_entries[i].weight;
			break;
		case NH_NOTIFIER_INFO_TYPE_RES_TABLE:
			nh_obj = &info->nh_res_table->nhs[i];
			weight = 1;
			break;
		default:
			err = -EINVAL;
			goto err_nexthop_obj_init;
		}
		err = mlxsw_sp_nexthop_obj_init(mlxsw_sp, nh_grp, nh, nh_obj,
						weight);
		if (err)
			goto err_nexthop_obj_init;
	}
	err = mlxsw_sp_nexthop_group_inc(mlxsw_sp);
	if (err)
		goto err_group_inc;
	err = mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh_grp);
	if (err) {
		NL_SET_ERR_MSG_MOD(info->extack, "Failed to write adjacency entries to the device");
		goto err_group_refresh;
	}

	/* Add resilient nexthop groups to a list so that the activity of their
	 * nexthop buckets will be periodically queried and cleared.
	 */
	if (nhgi->is_resilient) {
		if (list_empty(&mlxsw_sp->router->nh_res_grp_list))
			mlxsw_sp_nh_grp_activity_work_schedule(mlxsw_sp);
		list_add(&nhgi->list, &mlxsw_sp->router->nh_res_grp_list);
	}

	return 0;

err_group_refresh:
	mlxsw_sp_nexthop_group_dec(mlxsw_sp);
err_group_inc:
	i = nhgi->count;
err_nexthop_obj_init:
	for (i--; i >= 0; i--) {
		nh = &nhgi->nexthops[i];
		mlxsw_sp_nexthop_obj_fini(mlxsw_sp, nh);
	}
	kfree(nhgi);
	return err;
}

static void
mlxsw_sp_nexthop_obj_group_info_fini(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_nexthop_group *nh_grp)
{
	struct mlxsw_sp_nexthop_group_info *nhgi = nh_grp->nhgi;
	struct mlxsw_sp_router *router = mlxsw_sp->router;
	int i;

	if (nhgi->is_resilient) {
		list_del(&nhgi->list);
		if (list_empty(&mlxsw_sp->router->nh_res_grp_list))
			cancel_delayed_work(&router->nh_grp_activity_dw);
	}

	mlxsw_sp_nexthop_group_dec(mlxsw_sp);
	for (i = nhgi->count - 1; i >= 0; i--) {
		struct mlxsw_sp_nexthop *nh = &nhgi->nexthops[i];

		mlxsw_sp_nexthop_obj_fini(mlxsw_sp, nh);
	}
	mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh_grp);
	WARN_ON_ONCE(nhgi->adj_index_valid);
	kfree(nhgi);
}

static struct mlxsw_sp_nexthop_group *
mlxsw_sp_nexthop_obj_group_create(struct mlxsw_sp *mlxsw_sp,
				  struct nh_notifier_info *info)
{
	struct mlxsw_sp_nexthop_group *nh_grp;
	int err;

	nh_grp = kzalloc(sizeof(*nh_grp), GFP_KERNEL);
	if (!nh_grp)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&nh_grp->vr_list);
	err = rhashtable_init(&nh_grp->vr_ht,
			      &mlxsw_sp_nexthop_group_vr_ht_params);
	if (err)
		goto err_nexthop_group_vr_ht_init;
	INIT_LIST_HEAD(&nh_grp->fib_list);
	nh_grp->type = MLXSW_SP_NEXTHOP_GROUP_TYPE_OBJ;
	nh_grp->obj.id = info->id;

	err = mlxsw_sp_nexthop_obj_group_info_init(mlxsw_sp, nh_grp, info);
	if (err)
		goto err_nexthop_group_info_init;

	nh_grp->can_destroy = false;

	return nh_grp;

err_nexthop_group_info_init:
	rhashtable_destroy(&nh_grp->vr_ht);
err_nexthop_group_vr_ht_init:
	kfree(nh_grp);
	return ERR_PTR(err);
}

static void
mlxsw_sp_nexthop_obj_group_destroy(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_nexthop_group *nh_grp)
{
	if (!nh_grp->can_destroy)
		return;
	mlxsw_sp_nexthop_obj_group_info_fini(mlxsw_sp, nh_grp);
	WARN_ON_ONCE(!list_empty(&nh_grp->fib_list));
	WARN_ON_ONCE(!list_empty(&nh_grp->vr_list));
	rhashtable_destroy(&nh_grp->vr_ht);
	kfree(nh_grp);
}

static struct mlxsw_sp_nexthop_group *
mlxsw_sp_nexthop_obj_group_lookup(struct mlxsw_sp *mlxsw_sp, u32 id)
{
	struct mlxsw_sp_nexthop_group_cmp_arg cmp_arg;

	cmp_arg.type = MLXSW_SP_NEXTHOP_GROUP_TYPE_OBJ;
	cmp_arg.id = id;
	return rhashtable_lookup_fast(&mlxsw_sp->router->nexthop_group_ht,
				      &cmp_arg,
				      mlxsw_sp_nexthop_group_ht_params);
}

static int mlxsw_sp_nexthop_obj_group_add(struct mlxsw_sp *mlxsw_sp,
					  struct mlxsw_sp_nexthop_group *nh_grp)
{
	return mlxsw_sp_nexthop_group_insert(mlxsw_sp, nh_grp);
}

static int
mlxsw_sp_nexthop_obj_group_replace(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_nexthop_group *nh_grp,
				   struct mlxsw_sp_nexthop_group *old_nh_grp,
				   struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_nexthop_group_info *old_nhgi = old_nh_grp->nhgi;
	struct mlxsw_sp_nexthop_group_info *new_nhgi = nh_grp->nhgi;
	int err;

	old_nh_grp->nhgi = new_nhgi;
	new_nhgi->nh_grp = old_nh_grp;
	nh_grp->nhgi = old_nhgi;
	old_nhgi->nh_grp = nh_grp;

	if (old_nhgi->adj_index_valid && new_nhgi->adj_index_valid) {
		/* Both the old adjacency index and the new one are valid.
		 * Routes are currently using the old one. Tell the device to
		 * replace the old adjacency index with the new one.
		 */
		err = mlxsw_sp_adj_index_mass_update(mlxsw_sp, old_nh_grp,
						     old_nhgi->adj_index,
						     old_nhgi->ecmp_size);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to replace old adjacency index with new one");
			goto err_out;
		}
	} else if (old_nhgi->adj_index_valid && !new_nhgi->adj_index_valid) {
		/* The old adjacency index is valid, while the new one is not.
		 * Iterate over all the routes using the group and change them
		 * to trap packets to the CPU.
		 */
		err = mlxsw_sp_nexthop_fib_entries_update(mlxsw_sp, old_nh_grp);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to update routes to trap packets");
			goto err_out;
		}
	} else if (!old_nhgi->adj_index_valid && new_nhgi->adj_index_valid) {
		/* The old adjacency index is invalid, while the new one is.
		 * Iterate over all the routes using the group and change them
		 * to forward packets using the new valid index.
		 */
		err = mlxsw_sp_nexthop_fib_entries_update(mlxsw_sp, old_nh_grp);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to update routes to forward packets");
			goto err_out;
		}
	}

	/* Make sure the flags are set / cleared based on the new nexthop group
	 * information.
	 */
	mlxsw_sp_nexthop_obj_group_offload_refresh(mlxsw_sp, old_nh_grp);

	/* At this point 'nh_grp' is just a shell that is not used by anyone
	 * and its nexthop group info is the old info that was just replaced
	 * with the new one. Remove it.
	 */
	nh_grp->can_destroy = true;
	mlxsw_sp_nexthop_obj_group_destroy(mlxsw_sp, nh_grp);

	return 0;

err_out:
	old_nhgi->nh_grp = old_nh_grp;
	nh_grp->nhgi = new_nhgi;
	new_nhgi->nh_grp = nh_grp;
	old_nh_grp->nhgi = old_nhgi;
	return err;
}

static int mlxsw_sp_nexthop_obj_new(struct mlxsw_sp *mlxsw_sp,
				    struct nh_notifier_info *info)
{
	struct mlxsw_sp_nexthop_group *nh_grp, *old_nh_grp;
	struct netlink_ext_ack *extack = info->extack;
	int err;

	nh_grp = mlxsw_sp_nexthop_obj_group_create(mlxsw_sp, info);
	if (IS_ERR(nh_grp))
		return PTR_ERR(nh_grp);

	old_nh_grp = mlxsw_sp_nexthop_obj_group_lookup(mlxsw_sp, info->id);
	if (!old_nh_grp)
		err = mlxsw_sp_nexthop_obj_group_add(mlxsw_sp, nh_grp);
	else
		err = mlxsw_sp_nexthop_obj_group_replace(mlxsw_sp, nh_grp,
							 old_nh_grp, extack);

	if (err) {
		nh_grp->can_destroy = true;
		mlxsw_sp_nexthop_obj_group_destroy(mlxsw_sp, nh_grp);
	}

	return err;
}

static void mlxsw_sp_nexthop_obj_del(struct mlxsw_sp *mlxsw_sp,
				     struct nh_notifier_info *info)
{
	struct mlxsw_sp_nexthop_group *nh_grp;

	nh_grp = mlxsw_sp_nexthop_obj_group_lookup(mlxsw_sp, info->id);
	if (!nh_grp)
		return;

	nh_grp->can_destroy = true;
	mlxsw_sp_nexthop_group_remove(mlxsw_sp, nh_grp);

	/* If the group still has routes using it, then defer the delete
	 * operation until the last route using it is deleted.
	 */
	if (!list_empty(&nh_grp->fib_list))
		return;
	mlxsw_sp_nexthop_obj_group_destroy(mlxsw_sp, nh_grp);
}

static int mlxsw_sp_nexthop_obj_bucket_query(struct mlxsw_sp *mlxsw_sp,
					     u32 adj_index, char *ratr_pl)
{
	MLXSW_REG_ZERO(ratr, ratr_pl);
	mlxsw_reg_ratr_op_set(ratr_pl, MLXSW_REG_RATR_OP_QUERY_READ);
	mlxsw_reg_ratr_adjacency_index_low_set(ratr_pl, adj_index);
	mlxsw_reg_ratr_adjacency_index_high_set(ratr_pl, adj_index >> 16);

	return mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ratr), ratr_pl);
}

static int mlxsw_sp_nexthop_obj_bucket_compare(char *ratr_pl, char *ratr_pl_new)
{
	/* Clear the opcode and activity on both the old and new payload as
	 * they are irrelevant for the comparison.
	 */
	mlxsw_reg_ratr_op_set(ratr_pl, MLXSW_REG_RATR_OP_QUERY_READ);
	mlxsw_reg_ratr_a_set(ratr_pl, 0);
	mlxsw_reg_ratr_op_set(ratr_pl_new, MLXSW_REG_RATR_OP_QUERY_READ);
	mlxsw_reg_ratr_a_set(ratr_pl_new, 0);

	/* If the contents of the adjacency entry are consistent with the
	 * replacement request, then replacement was successful.
	 */
	if (!memcmp(ratr_pl, ratr_pl_new, MLXSW_REG_RATR_LEN))
		return 0;

	return -EINVAL;
}

static int
mlxsw_sp_nexthop_obj_bucket_adj_update(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_nexthop *nh,
				       struct nh_notifier_info *info)
{
	u16 bucket_index = info->nh_res_bucket->bucket_index;
	struct netlink_ext_ack *extack = info->extack;
	bool force = info->nh_res_bucket->force;
	char ratr_pl_new[MLXSW_REG_RATR_LEN];
	char ratr_pl[MLXSW_REG_RATR_LEN];
	u32 adj_index;
	int err;

	/* No point in trying an atomic replacement if the idle timer interval
	 * is smaller than the interval in which we query and clear activity.
	 */
	if (!force && info->nh_res_bucket->idle_timer_ms <
	    MLXSW_SP_NH_GRP_ACTIVITY_UPDATE_INTERVAL)
		force = true;

	adj_index = nh->nhgi->adj_index + bucket_index;
	err = mlxsw_sp_nexthop_update(mlxsw_sp, adj_index, nh, force, ratr_pl);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to overwrite nexthop bucket");
		return err;
	}

	if (!force) {
		err = mlxsw_sp_nexthop_obj_bucket_query(mlxsw_sp, adj_index,
							ratr_pl_new);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to query nexthop bucket state after replacement. State might be inconsistent");
			return err;
		}

		err = mlxsw_sp_nexthop_obj_bucket_compare(ratr_pl, ratr_pl_new);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Nexthop bucket was not replaced because it was active during replacement");
			return err;
		}
	}

	nh->update = 0;
	nh->offloaded = 1;
	mlxsw_sp_nexthop_bucket_offload_refresh(mlxsw_sp, nh, bucket_index);

	return 0;
}

static int mlxsw_sp_nexthop_obj_bucket_replace(struct mlxsw_sp *mlxsw_sp,
					       struct nh_notifier_info *info)
{
	u16 bucket_index = info->nh_res_bucket->bucket_index;
	struct netlink_ext_ack *extack = info->extack;
	struct mlxsw_sp_nexthop_group_info *nhgi;
	struct nh_notifier_single_info *nh_obj;
	struct mlxsw_sp_nexthop_group *nh_grp;
	struct mlxsw_sp_nexthop *nh;
	int err;

	nh_grp = mlxsw_sp_nexthop_obj_group_lookup(mlxsw_sp, info->id);
	if (!nh_grp) {
		NL_SET_ERR_MSG_MOD(extack, "Nexthop group was not found");
		return -EINVAL;
	}

	nhgi = nh_grp->nhgi;

	if (bucket_index >= nhgi->count) {
		NL_SET_ERR_MSG_MOD(extack, "Nexthop bucket index out of range");
		return -EINVAL;
	}

	nh = &nhgi->nexthops[bucket_index];
	mlxsw_sp_nexthop_obj_fini(mlxsw_sp, nh);

	nh_obj = &info->nh_res_bucket->new_nh;
	err = mlxsw_sp_nexthop_obj_init(mlxsw_sp, nh_grp, nh, nh_obj, 1);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to initialize nexthop object for nexthop bucket replacement");
		goto err_nexthop_obj_init;
	}

	err = mlxsw_sp_nexthop_obj_bucket_adj_update(mlxsw_sp, nh, info);
	if (err)
		goto err_nexthop_obj_bucket_adj_update;

	return 0;

err_nexthop_obj_bucket_adj_update:
	mlxsw_sp_nexthop_obj_fini(mlxsw_sp, nh);
err_nexthop_obj_init:
	nh_obj = &info->nh_res_bucket->old_nh;
	mlxsw_sp_nexthop_obj_init(mlxsw_sp, nh_grp, nh, nh_obj, 1);
	/* The old adjacency entry was not overwritten */
	nh->update = 0;
	nh->offloaded = 1;
	return err;
}

static int mlxsw_sp_nexthop_obj_event(struct notifier_block *nb,
				      unsigned long event, void *ptr)
{
	struct nh_notifier_info *info = ptr;
	struct mlxsw_sp_router *router;
	int err = 0;

	router = container_of(nb, struct mlxsw_sp_router, nexthop_nb);
	err = mlxsw_sp_nexthop_obj_validate(router->mlxsw_sp, event, info);
	if (err)
		goto out;

	mutex_lock(&router->lock);

	switch (event) {
	case NEXTHOP_EVENT_REPLACE:
		err = mlxsw_sp_nexthop_obj_new(router->mlxsw_sp, info);
		break;
	case NEXTHOP_EVENT_DEL:
		mlxsw_sp_nexthop_obj_del(router->mlxsw_sp, info);
		break;
	case NEXTHOP_EVENT_BUCKET_REPLACE:
		err = mlxsw_sp_nexthop_obj_bucket_replace(router->mlxsw_sp,
							  info);
		break;
	default:
		break;
	}

	mutex_unlock(&router->lock);

out:
	return notifier_from_errno(err);
}

static bool mlxsw_sp_fi_is_gateway(const struct mlxsw_sp *mlxsw_sp,
				   struct fib_info *fi)
{
	const struct fib_nh *nh = fib_info_nh(fi, 0);

	return nh->fib_nh_gw_family ||
	       mlxsw_sp_nexthop4_ipip_type(mlxsw_sp, nh, NULL);
}

static int
mlxsw_sp_nexthop4_group_info_init(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_nexthop_group *nh_grp)
{
	unsigned int nhs = fib_info_num_path(nh_grp->ipv4.fi);
	struct mlxsw_sp_nexthop_group_info *nhgi;
	struct mlxsw_sp_nexthop *nh;
	int err, i;

	nhgi = kzalloc(struct_size(nhgi, nexthops, nhs), GFP_KERNEL);
	if (!nhgi)
		return -ENOMEM;
	nh_grp->nhgi = nhgi;
	nhgi->nh_grp = nh_grp;
	nhgi->gateway = mlxsw_sp_fi_is_gateway(mlxsw_sp, nh_grp->ipv4.fi);
	nhgi->count = nhs;
	for (i = 0; i < nhgi->count; i++) {
		struct fib_nh *fib_nh;

		nh = &nhgi->nexthops[i];
		fib_nh = fib_info_nh(nh_grp->ipv4.fi, i);
		err = mlxsw_sp_nexthop4_init(mlxsw_sp, nh_grp, nh, fib_nh);
		if (err)
			goto err_nexthop4_init;
	}
	err = mlxsw_sp_nexthop_group_inc(mlxsw_sp);
	if (err)
		goto err_group_inc;
	err = mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh_grp);
	if (err)
		goto err_group_refresh;

	return 0;

err_group_refresh:
	mlxsw_sp_nexthop_group_dec(mlxsw_sp);
err_group_inc:
	i = nhgi->count;
err_nexthop4_init:
	for (i--; i >= 0; i--) {
		nh = &nhgi->nexthops[i];
		mlxsw_sp_nexthop4_fini(mlxsw_sp, nh);
	}
	kfree(nhgi);
	return err;
}

static void
mlxsw_sp_nexthop4_group_info_fini(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_nexthop_group *nh_grp)
{
	struct mlxsw_sp_nexthop_group_info *nhgi = nh_grp->nhgi;
	int i;

	mlxsw_sp_nexthop_group_dec(mlxsw_sp);
	for (i = nhgi->count - 1; i >= 0; i--) {
		struct mlxsw_sp_nexthop *nh = &nhgi->nexthops[i];

		mlxsw_sp_nexthop4_fini(mlxsw_sp, nh);
	}
	mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh_grp);
	WARN_ON_ONCE(nhgi->adj_index_valid);
	kfree(nhgi);
}

static struct mlxsw_sp_nexthop_group *
mlxsw_sp_nexthop4_group_create(struct mlxsw_sp *mlxsw_sp, struct fib_info *fi)
{
	struct mlxsw_sp_nexthop_group *nh_grp;
	int err;

	nh_grp = kzalloc(sizeof(*nh_grp), GFP_KERNEL);
	if (!nh_grp)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&nh_grp->vr_list);
	err = rhashtable_init(&nh_grp->vr_ht,
			      &mlxsw_sp_nexthop_group_vr_ht_params);
	if (err)
		goto err_nexthop_group_vr_ht_init;
	INIT_LIST_HEAD(&nh_grp->fib_list);
	nh_grp->type = MLXSW_SP_NEXTHOP_GROUP_TYPE_IPV4;
	nh_grp->ipv4.fi = fi;
	fib_info_hold(fi);

	err = mlxsw_sp_nexthop4_group_info_init(mlxsw_sp, nh_grp);
	if (err)
		goto err_nexthop_group_info_init;

	err = mlxsw_sp_nexthop_group_insert(mlxsw_sp, nh_grp);
	if (err)
		goto err_nexthop_group_insert;

	nh_grp->can_destroy = true;

	return nh_grp;

err_nexthop_group_insert:
	mlxsw_sp_nexthop4_group_info_fini(mlxsw_sp, nh_grp);
err_nexthop_group_info_init:
	fib_info_put(fi);
	rhashtable_destroy(&nh_grp->vr_ht);
err_nexthop_group_vr_ht_init:
	kfree(nh_grp);
	return ERR_PTR(err);
}

static void
mlxsw_sp_nexthop4_group_destroy(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_nexthop_group *nh_grp)
{
	if (!nh_grp->can_destroy)
		return;
	mlxsw_sp_nexthop_group_remove(mlxsw_sp, nh_grp);
	mlxsw_sp_nexthop4_group_info_fini(mlxsw_sp, nh_grp);
	fib_info_put(nh_grp->ipv4.fi);
	WARN_ON_ONCE(!list_empty(&nh_grp->vr_list));
	rhashtable_destroy(&nh_grp->vr_ht);
	kfree(nh_grp);
}

static int mlxsw_sp_nexthop4_group_get(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_fib_entry *fib_entry,
				       struct fib_info *fi)
{
	struct mlxsw_sp_nexthop_group *nh_grp;

	if (fi->nh) {
		nh_grp = mlxsw_sp_nexthop_obj_group_lookup(mlxsw_sp,
							   fi->nh->id);
		if (WARN_ON_ONCE(!nh_grp))
			return -EINVAL;
		goto out;
	}

	nh_grp = mlxsw_sp_nexthop4_group_lookup(mlxsw_sp, fi);
	if (!nh_grp) {
		nh_grp = mlxsw_sp_nexthop4_group_create(mlxsw_sp, fi);
		if (IS_ERR(nh_grp))
			return PTR_ERR(nh_grp);
	}
out:
	list_add_tail(&fib_entry->nexthop_group_node, &nh_grp->fib_list);
	fib_entry->nh_group = nh_grp;
	return 0;
}

static void mlxsw_sp_nexthop4_group_put(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_nexthop_group *nh_grp = fib_entry->nh_group;

	list_del(&fib_entry->nexthop_group_node);
	if (!list_empty(&nh_grp->fib_list))
		return;

	if (nh_grp->type == MLXSW_SP_NEXTHOP_GROUP_TYPE_OBJ) {
		mlxsw_sp_nexthop_obj_group_destroy(mlxsw_sp, nh_grp);
		return;
	}

	mlxsw_sp_nexthop4_group_destroy(mlxsw_sp, nh_grp);
}

static bool
mlxsw_sp_fib4_entry_should_offload(const struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_fib4_entry *fib4_entry;

	fib4_entry = container_of(fib_entry, struct mlxsw_sp_fib4_entry,
				  common);
	return !fib4_entry->dscp;
}

static bool
mlxsw_sp_fib_entry_should_offload(const struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_nexthop_group *nh_group = fib_entry->nh_group;

	switch (fib_entry->fib_node->fib->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		if (!mlxsw_sp_fib4_entry_should_offload(fib_entry))
			return false;
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		break;
	}

	switch (fib_entry->type) {
	case MLXSW_SP_FIB_ENTRY_TYPE_REMOTE:
		return !!nh_group->nhgi->adj_index_valid;
	case MLXSW_SP_FIB_ENTRY_TYPE_LOCAL:
		return !!mlxsw_sp_nhgi_rif(nh_group->nhgi);
	case MLXSW_SP_FIB_ENTRY_TYPE_BLACKHOLE:
	case MLXSW_SP_FIB_ENTRY_TYPE_IPIP_DECAP:
	case MLXSW_SP_FIB_ENTRY_TYPE_NVE_DECAP:
		return true;
	default:
		return false;
	}
}

static struct mlxsw_sp_nexthop *
mlxsw_sp_rt6_nexthop(struct mlxsw_sp_nexthop_group *nh_grp,
		     const struct mlxsw_sp_rt6 *mlxsw_sp_rt6)
{
	int i;

	for (i = 0; i < nh_grp->nhgi->count; i++) {
		struct mlxsw_sp_nexthop *nh = &nh_grp->nhgi->nexthops[i];
		struct net_device *dev = mlxsw_sp_nexthop_dev(nh);
		struct fib6_info *rt = mlxsw_sp_rt6->rt;

		if (dev && dev == rt->fib6_nh->fib_nh_dev &&
		    ipv6_addr_equal((const struct in6_addr *) &nh->gw_addr,
				    &rt->fib6_nh->fib_nh_gw6))
			return nh;
	}

	return NULL;
}

static void
mlxsw_sp_fib4_offload_failed_flag_set(struct mlxsw_sp *mlxsw_sp,
				      struct fib_entry_notifier_info *fen_info)
{
	u32 *p_dst = (u32 *) &fen_info->dst;
	struct fib_rt_info fri;

	fri.fi = fen_info->fi;
	fri.tb_id = fen_info->tb_id;
	fri.dst = cpu_to_be32(*p_dst);
	fri.dst_len = fen_info->dst_len;
	fri.dscp = fen_info->dscp;
	fri.type = fen_info->type;
	fri.offload = false;
	fri.trap = false;
	fri.offload_failed = true;
	fib_alias_hw_flags_set(mlxsw_sp_net(mlxsw_sp), &fri);
}

static void
mlxsw_sp_fib4_entry_hw_flags_set(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_fib_entry *fib_entry)
{
	u32 *p_dst = (u32 *) fib_entry->fib_node->key.addr;
	int dst_len = fib_entry->fib_node->key.prefix_len;
	struct mlxsw_sp_fib4_entry *fib4_entry;
	struct fib_rt_info fri;
	bool should_offload;

	should_offload = mlxsw_sp_fib_entry_should_offload(fib_entry);
	fib4_entry = container_of(fib_entry, struct mlxsw_sp_fib4_entry,
				  common);
	fri.fi = fib4_entry->fi;
	fri.tb_id = fib4_entry->tb_id;
	fri.dst = cpu_to_be32(*p_dst);
	fri.dst_len = dst_len;
	fri.dscp = fib4_entry->dscp;
	fri.type = fib4_entry->type;
	fri.offload = should_offload;
	fri.trap = !should_offload;
	fri.offload_failed = false;
	fib_alias_hw_flags_set(mlxsw_sp_net(mlxsw_sp), &fri);
}

static void
mlxsw_sp_fib4_entry_hw_flags_clear(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_fib_entry *fib_entry)
{
	u32 *p_dst = (u32 *) fib_entry->fib_node->key.addr;
	int dst_len = fib_entry->fib_node->key.prefix_len;
	struct mlxsw_sp_fib4_entry *fib4_entry;
	struct fib_rt_info fri;

	fib4_entry = container_of(fib_entry, struct mlxsw_sp_fib4_entry,
				  common);
	fri.fi = fib4_entry->fi;
	fri.tb_id = fib4_entry->tb_id;
	fri.dst = cpu_to_be32(*p_dst);
	fri.dst_len = dst_len;
	fri.dscp = fib4_entry->dscp;
	fri.type = fib4_entry->type;
	fri.offload = false;
	fri.trap = false;
	fri.offload_failed = false;
	fib_alias_hw_flags_set(mlxsw_sp_net(mlxsw_sp), &fri);
}

#if IS_ENABLED(CONFIG_IPV6)
static void
mlxsw_sp_fib6_offload_failed_flag_set(struct mlxsw_sp *mlxsw_sp,
				      struct fib6_info **rt_arr,
				      unsigned int nrt6)
{
	int i;

	/* In IPv6 a multipath route is represented using multiple routes, so
	 * we need to set the flags on all of them.
	 */
	for (i = 0; i < nrt6; i++)
		fib6_info_hw_flags_set(mlxsw_sp_net(mlxsw_sp), rt_arr[i],
				       false, false, true);
}
#else
static void
mlxsw_sp_fib6_offload_failed_flag_set(struct mlxsw_sp *mlxsw_sp,
				      struct fib6_info **rt_arr,
				      unsigned int nrt6)
{
}
#endif

#if IS_ENABLED(CONFIG_IPV6)
static void
mlxsw_sp_fib6_entry_hw_flags_set(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_fib6_entry *fib6_entry;
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6;
	bool should_offload;

	should_offload = mlxsw_sp_fib_entry_should_offload(fib_entry);

	/* In IPv6 a multipath route is represented using multiple routes, so
	 * we need to set the flags on all of them.
	 */
	fib6_entry = container_of(fib_entry, struct mlxsw_sp_fib6_entry,
				  common);
	list_for_each_entry(mlxsw_sp_rt6, &fib6_entry->rt6_list, list)
		fib6_info_hw_flags_set(mlxsw_sp_net(mlxsw_sp), mlxsw_sp_rt6->rt,
				       should_offload, !should_offload, false);
}
#else
static void
mlxsw_sp_fib6_entry_hw_flags_set(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_fib_entry *fib_entry)
{
}
#endif

#if IS_ENABLED(CONFIG_IPV6)
static void
mlxsw_sp_fib6_entry_hw_flags_clear(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_fib6_entry *fib6_entry;
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6;

	fib6_entry = container_of(fib_entry, struct mlxsw_sp_fib6_entry,
				  common);
	list_for_each_entry(mlxsw_sp_rt6, &fib6_entry->rt6_list, list)
		fib6_info_hw_flags_set(mlxsw_sp_net(mlxsw_sp), mlxsw_sp_rt6->rt,
				       false, false, false);
}
#else
static void
mlxsw_sp_fib6_entry_hw_flags_clear(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_fib_entry *fib_entry)
{
}
#endif

static void
mlxsw_sp_fib_entry_hw_flags_set(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_fib_entry *fib_entry)
{
	switch (fib_entry->fib_node->fib->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		mlxsw_sp_fib4_entry_hw_flags_set(mlxsw_sp, fib_entry);
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		mlxsw_sp_fib6_entry_hw_flags_set(mlxsw_sp, fib_entry);
		break;
	}
}

static void
mlxsw_sp_fib_entry_hw_flags_clear(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_fib_entry *fib_entry)
{
	switch (fib_entry->fib_node->fib->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		mlxsw_sp_fib4_entry_hw_flags_clear(mlxsw_sp, fib_entry);
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		mlxsw_sp_fib6_entry_hw_flags_clear(mlxsw_sp, fib_entry);
		break;
	}
}

static void
mlxsw_sp_fib_entry_hw_flags_refresh(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_fib_entry *fib_entry,
				    enum mlxsw_reg_ralue_op op)
{
	switch (op) {
	case MLXSW_REG_RALUE_OP_WRITE_WRITE:
		mlxsw_sp_fib_entry_hw_flags_set(mlxsw_sp, fib_entry);
		break;
	case MLXSW_REG_RALUE_OP_WRITE_DELETE:
		mlxsw_sp_fib_entry_hw_flags_clear(mlxsw_sp, fib_entry);
		break;
	default:
		break;
	}
}

static void
mlxsw_sp_fib_entry_ralue_pack(char *ralue_pl,
			      const struct mlxsw_sp_fib_entry *fib_entry,
			      enum mlxsw_reg_ralue_op op)
{
	struct mlxsw_sp_fib *fib = fib_entry->fib_node->fib;
	enum mlxsw_reg_ralxx_protocol proto;
	u32 *p_dip;

	proto = (enum mlxsw_reg_ralxx_protocol) fib->proto;

	switch (fib->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		p_dip = (u32 *) fib_entry->fib_node->key.addr;
		mlxsw_reg_ralue_pack4(ralue_pl, proto, op, fib->vr->id,
				      fib_entry->fib_node->key.prefix_len,
				      *p_dip);
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		mlxsw_reg_ralue_pack6(ralue_pl, proto, op, fib->vr->id,
				      fib_entry->fib_node->key.prefix_len,
				      fib_entry->fib_node->key.addr);
		break;
	}
}

static int mlxsw_sp_fib_entry_op_remote(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib_entry *fib_entry,
					enum mlxsw_reg_ralue_op op)
{
	struct mlxsw_sp_nexthop_group *nh_group = fib_entry->nh_group;
	struct mlxsw_sp_nexthop_group_info *nhgi = nh_group->nhgi;
	char ralue_pl[MLXSW_REG_RALUE_LEN];
	enum mlxsw_reg_ralue_trap_action trap_action;
	u16 trap_id = 0;
	u32 adjacency_index = 0;
	u16 ecmp_size = 0;

	/* In case the nexthop group adjacency index is valid, use it
	 * with provided ECMP size. Otherwise, setup trap and pass
	 * traffic to kernel.
	 */
	if (mlxsw_sp_fib_entry_should_offload(fib_entry)) {
		trap_action = MLXSW_REG_RALUE_TRAP_ACTION_NOP;
		adjacency_index = nhgi->adj_index;
		ecmp_size = nhgi->ecmp_size;
	} else if (!nhgi->adj_index_valid && nhgi->count &&
		   mlxsw_sp_nhgi_rif(nhgi)) {
		trap_action = MLXSW_REG_RALUE_TRAP_ACTION_NOP;
		adjacency_index = mlxsw_sp->router->adj_trap_index;
		ecmp_size = 1;
	} else {
		trap_action = MLXSW_REG_RALUE_TRAP_ACTION_TRAP;
		trap_id = MLXSW_TRAP_ID_RTR_INGRESS0;
	}

	mlxsw_sp_fib_entry_ralue_pack(ralue_pl, fib_entry, op);
	mlxsw_reg_ralue_act_remote_pack(ralue_pl, trap_action, trap_id,
					adjacency_index, ecmp_size);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralue), ralue_pl);
}

static int mlxsw_sp_fib_entry_op_local(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_fib_entry *fib_entry,
				       enum mlxsw_reg_ralue_op op)
{
	struct mlxsw_sp_rif *rif = mlxsw_sp_nhgi_rif(fib_entry->nh_group->nhgi);
	enum mlxsw_reg_ralue_trap_action trap_action;
	char ralue_pl[MLXSW_REG_RALUE_LEN];
	u16 trap_id = 0;
	u16 rif_index = 0;

	if (mlxsw_sp_fib_entry_should_offload(fib_entry)) {
		trap_action = MLXSW_REG_RALUE_TRAP_ACTION_NOP;
		rif_index = rif->rif_index;
	} else {
		trap_action = MLXSW_REG_RALUE_TRAP_ACTION_TRAP;
		trap_id = MLXSW_TRAP_ID_RTR_INGRESS0;
	}

	mlxsw_sp_fib_entry_ralue_pack(ralue_pl, fib_entry, op);
	mlxsw_reg_ralue_act_local_pack(ralue_pl, trap_action, trap_id,
				       rif_index);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralue), ralue_pl);
}

static int mlxsw_sp_fib_entry_op_trap(struct mlxsw_sp *mlxsw_sp,
				      struct mlxsw_sp_fib_entry *fib_entry,
				      enum mlxsw_reg_ralue_op op)
{
	char ralue_pl[MLXSW_REG_RALUE_LEN];

	mlxsw_sp_fib_entry_ralue_pack(ralue_pl, fib_entry, op);
	mlxsw_reg_ralue_act_ip2me_pack(ralue_pl);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralue), ralue_pl);
}

static int mlxsw_sp_fib_entry_op_blackhole(struct mlxsw_sp *mlxsw_sp,
					   struct mlxsw_sp_fib_entry *fib_entry,
					   enum mlxsw_reg_ralue_op op)
{
	enum mlxsw_reg_ralue_trap_action trap_action;
	char ralue_pl[MLXSW_REG_RALUE_LEN];

	trap_action = MLXSW_REG_RALUE_TRAP_ACTION_DISCARD_ERROR;
	mlxsw_sp_fib_entry_ralue_pack(ralue_pl, fib_entry, op);
	mlxsw_reg_ralue_act_local_pack(ralue_pl, trap_action, 0, 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralue), ralue_pl);
}

static int
mlxsw_sp_fib_entry_op_unreachable(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_fib_entry *fib_entry,
				  enum mlxsw_reg_ralue_op op)
{
	enum mlxsw_reg_ralue_trap_action trap_action;
	char ralue_pl[MLXSW_REG_RALUE_LEN];
	u16 trap_id;

	trap_action = MLXSW_REG_RALUE_TRAP_ACTION_TRAP;
	trap_id = MLXSW_TRAP_ID_RTR_INGRESS1;

	mlxsw_sp_fib_entry_ralue_pack(ralue_pl, fib_entry, op);
	mlxsw_reg_ralue_act_local_pack(ralue_pl, trap_action, trap_id, 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralue), ralue_pl);
}

static int
mlxsw_sp_fib_entry_op_ipip_decap(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_fib_entry *fib_entry,
				 enum mlxsw_reg_ralue_op op)
{
	struct mlxsw_sp_ipip_entry *ipip_entry = fib_entry->decap.ipip_entry;
	const struct mlxsw_sp_ipip_ops *ipip_ops;
	char ralue_pl[MLXSW_REG_RALUE_LEN];
	int err;

	if (WARN_ON(!ipip_entry))
		return -EINVAL;

	ipip_ops = mlxsw_sp->router->ipip_ops_arr[ipip_entry->ipipt];
	err = ipip_ops->decap_config(mlxsw_sp, ipip_entry,
				     fib_entry->decap.tunnel_index);
	if (err)
		return err;

	mlxsw_sp_fib_entry_ralue_pack(ralue_pl, fib_entry, op);
	mlxsw_reg_ralue_act_ip2me_tun_pack(ralue_pl,
					   fib_entry->decap.tunnel_index);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralue), ralue_pl);
}

static int mlxsw_sp_fib_entry_op_nve_decap(struct mlxsw_sp *mlxsw_sp,
					   struct mlxsw_sp_fib_entry *fib_entry,
					   enum mlxsw_reg_ralue_op op)
{
	char ralue_pl[MLXSW_REG_RALUE_LEN];

	mlxsw_sp_fib_entry_ralue_pack(ralue_pl, fib_entry, op);
	mlxsw_reg_ralue_act_ip2me_tun_pack(ralue_pl,
					   fib_entry->decap.tunnel_index);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralue), ralue_pl);
}

static int __mlxsw_sp_fib_entry_op(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_fib_entry *fib_entry,
				   enum mlxsw_reg_ralue_op op)
{
	switch (fib_entry->type) {
	case MLXSW_SP_FIB_ENTRY_TYPE_REMOTE:
		return mlxsw_sp_fib_entry_op_remote(mlxsw_sp, fib_entry, op);
	case MLXSW_SP_FIB_ENTRY_TYPE_LOCAL:
		return mlxsw_sp_fib_entry_op_local(mlxsw_sp, fib_entry, op);
	case MLXSW_SP_FIB_ENTRY_TYPE_TRAP:
		return mlxsw_sp_fib_entry_op_trap(mlxsw_sp, fib_entry, op);
	case MLXSW_SP_FIB_ENTRY_TYPE_BLACKHOLE:
		return mlxsw_sp_fib_entry_op_blackhole(mlxsw_sp, fib_entry, op);
	case MLXSW_SP_FIB_ENTRY_TYPE_UNREACHABLE:
		return mlxsw_sp_fib_entry_op_unreachable(mlxsw_sp, fib_entry,
							 op);
	case MLXSW_SP_FIB_ENTRY_TYPE_IPIP_DECAP:
		return mlxsw_sp_fib_entry_op_ipip_decap(mlxsw_sp,
							fib_entry, op);
	case MLXSW_SP_FIB_ENTRY_TYPE_NVE_DECAP:
		return mlxsw_sp_fib_entry_op_nve_decap(mlxsw_sp, fib_entry, op);
	}
	return -EINVAL;
}

static int mlxsw_sp_fib_entry_op(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_fib_entry *fib_entry,
				 enum mlxsw_reg_ralue_op op)
{
	int err = __mlxsw_sp_fib_entry_op(mlxsw_sp, fib_entry, op);

	if (err)
		return err;

	mlxsw_sp_fib_entry_hw_flags_refresh(mlxsw_sp, fib_entry, op);

	return err;
}

static int mlxsw_sp_fib_entry_update(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_fib_entry *fib_entry)
{
	return mlxsw_sp_fib_entry_op(mlxsw_sp, fib_entry,
				     MLXSW_REG_RALUE_OP_WRITE_WRITE);
}

static int mlxsw_sp_fib_entry_del(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_fib_entry *fib_entry)
{
	return mlxsw_sp_fib_entry_op(mlxsw_sp, fib_entry,
				     MLXSW_REG_RALUE_OP_WRITE_DELETE);
}

static int
mlxsw_sp_fib4_entry_type_set(struct mlxsw_sp *mlxsw_sp,
			     const struct fib_entry_notifier_info *fen_info,
			     struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_nexthop_group_info *nhgi = fib_entry->nh_group->nhgi;
	union mlxsw_sp_l3addr dip = { .addr4 = htonl(fen_info->dst) };
	struct mlxsw_sp_router *router = mlxsw_sp->router;
	u32 tb_id = mlxsw_sp_fix_tb_id(fen_info->tb_id);
	int ifindex = nhgi->nexthops[0].ifindex;
	struct mlxsw_sp_ipip_entry *ipip_entry;

	switch (fen_info->type) {
	case RTN_LOCAL:
		ipip_entry = mlxsw_sp_ipip_entry_find_by_decap(mlxsw_sp, ifindex,
							       MLXSW_SP_L3_PROTO_IPV4, dip);
		if (ipip_entry && ipip_entry->ol_dev->flags & IFF_UP) {
			fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_IPIP_DECAP;
			return mlxsw_sp_fib_entry_decap_init(mlxsw_sp,
							     fib_entry,
							     ipip_entry);
		}
		if (mlxsw_sp_router_nve_is_decap(mlxsw_sp, tb_id,
						 MLXSW_SP_L3_PROTO_IPV4,
						 &dip)) {
			u32 tunnel_index;

			tunnel_index = router->nve_decap_config.tunnel_index;
			fib_entry->decap.tunnel_index = tunnel_index;
			fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_NVE_DECAP;
			return 0;
		}
		fallthrough;
	case RTN_BROADCAST:
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_TRAP;
		return 0;
	case RTN_BLACKHOLE:
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_BLACKHOLE;
		return 0;
	case RTN_UNREACHABLE:
	case RTN_PROHIBIT:
		/* Packets hitting these routes need to be trapped, but
		 * can do so with a lower priority than packets directed
		 * at the host, so use action type local instead of trap.
		 */
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_UNREACHABLE;
		return 0;
	case RTN_UNICAST:
		if (nhgi->gateway)
			fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_REMOTE;
		else
			fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_LOCAL;
		return 0;
	default:
		return -EINVAL;
	}
}

static void
mlxsw_sp_fib_entry_type_unset(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_fib_entry *fib_entry)
{
	switch (fib_entry->type) {
	case MLXSW_SP_FIB_ENTRY_TYPE_IPIP_DECAP:
		mlxsw_sp_fib_entry_decap_fini(mlxsw_sp, fib_entry);
		break;
	default:
		break;
	}
}

static void
mlxsw_sp_fib4_entry_type_unset(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_fib4_entry *fib4_entry)
{
	mlxsw_sp_fib_entry_type_unset(mlxsw_sp, &fib4_entry->common);
}

static struct mlxsw_sp_fib4_entry *
mlxsw_sp_fib4_entry_create(struct mlxsw_sp *mlxsw_sp,
			   struct mlxsw_sp_fib_node *fib_node,
			   const struct fib_entry_notifier_info *fen_info)
{
	struct mlxsw_sp_fib4_entry *fib4_entry;
	struct mlxsw_sp_fib_entry *fib_entry;
	int err;

	fib4_entry = kzalloc(sizeof(*fib4_entry), GFP_KERNEL);
	if (!fib4_entry)
		return ERR_PTR(-ENOMEM);
	fib_entry = &fib4_entry->common;

	err = mlxsw_sp_nexthop4_group_get(mlxsw_sp, fib_entry, fen_info->fi);
	if (err)
		goto err_nexthop4_group_get;

	err = mlxsw_sp_nexthop_group_vr_link(fib_entry->nh_group,
					     fib_node->fib);
	if (err)
		goto err_nexthop_group_vr_link;

	err = mlxsw_sp_fib4_entry_type_set(mlxsw_sp, fen_info, fib_entry);
	if (err)
		goto err_fib4_entry_type_set;

	fib4_entry->fi = fen_info->fi;
	fib_info_hold(fib4_entry->fi);
	fib4_entry->tb_id = fen_info->tb_id;
	fib4_entry->type = fen_info->type;
	fib4_entry->dscp = fen_info->dscp;

	fib_entry->fib_node = fib_node;

	return fib4_entry;

err_fib4_entry_type_set:
	mlxsw_sp_nexthop_group_vr_unlink(fib_entry->nh_group, fib_node->fib);
err_nexthop_group_vr_link:
	mlxsw_sp_nexthop4_group_put(mlxsw_sp, &fib4_entry->common);
err_nexthop4_group_get:
	kfree(fib4_entry);
	return ERR_PTR(err);
}

static void mlxsw_sp_fib4_entry_destroy(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib4_entry *fib4_entry)
{
	struct mlxsw_sp_fib_node *fib_node = fib4_entry->common.fib_node;

	fib_info_put(fib4_entry->fi);
	mlxsw_sp_fib4_entry_type_unset(mlxsw_sp, fib4_entry);
	mlxsw_sp_nexthop_group_vr_unlink(fib4_entry->common.nh_group,
					 fib_node->fib);
	mlxsw_sp_nexthop4_group_put(mlxsw_sp, &fib4_entry->common);
	kfree(fib4_entry);
}

static struct mlxsw_sp_fib4_entry *
mlxsw_sp_fib4_entry_lookup(struct mlxsw_sp *mlxsw_sp,
			   const struct fib_entry_notifier_info *fen_info)
{
	struct mlxsw_sp_fib4_entry *fib4_entry;
	struct mlxsw_sp_fib_node *fib_node;
	struct mlxsw_sp_fib *fib;
	struct mlxsw_sp_vr *vr;

	vr = mlxsw_sp_vr_find(mlxsw_sp, fen_info->tb_id);
	if (!vr)
		return NULL;
	fib = mlxsw_sp_vr_fib(vr, MLXSW_SP_L3_PROTO_IPV4);

	fib_node = mlxsw_sp_fib_node_lookup(fib, &fen_info->dst,
					    sizeof(fen_info->dst),
					    fen_info->dst_len);
	if (!fib_node)
		return NULL;

	fib4_entry = container_of(fib_node->fib_entry,
				  struct mlxsw_sp_fib4_entry, common);
	if (fib4_entry->tb_id == fen_info->tb_id &&
	    fib4_entry->dscp == fen_info->dscp &&
	    fib4_entry->type == fen_info->type &&
	    fib4_entry->fi == fen_info->fi)
		return fib4_entry;

	return NULL;
}

static const struct rhashtable_params mlxsw_sp_fib_ht_params = {
	.key_offset = offsetof(struct mlxsw_sp_fib_node, key),
	.head_offset = offsetof(struct mlxsw_sp_fib_node, ht_node),
	.key_len = sizeof(struct mlxsw_sp_fib_key),
	.automatic_shrinking = true,
};

static int mlxsw_sp_fib_node_insert(struct mlxsw_sp_fib *fib,
				    struct mlxsw_sp_fib_node *fib_node)
{
	return rhashtable_insert_fast(&fib->ht, &fib_node->ht_node,
				      mlxsw_sp_fib_ht_params);
}

static void mlxsw_sp_fib_node_remove(struct mlxsw_sp_fib *fib,
				     struct mlxsw_sp_fib_node *fib_node)
{
	rhashtable_remove_fast(&fib->ht, &fib_node->ht_node,
			       mlxsw_sp_fib_ht_params);
}

static struct mlxsw_sp_fib_node *
mlxsw_sp_fib_node_lookup(struct mlxsw_sp_fib *fib, const void *addr,
			 size_t addr_len, unsigned char prefix_len)
{
	struct mlxsw_sp_fib_key key;

	memset(&key, 0, sizeof(key));
	memcpy(key.addr, addr, addr_len);
	key.prefix_len = prefix_len;
	return rhashtable_lookup_fast(&fib->ht, &key, mlxsw_sp_fib_ht_params);
}

static struct mlxsw_sp_fib_node *
mlxsw_sp_fib_node_create(struct mlxsw_sp_fib *fib, const void *addr,
			 size_t addr_len, unsigned char prefix_len)
{
	struct mlxsw_sp_fib_node *fib_node;

	fib_node = kzalloc(sizeof(*fib_node), GFP_KERNEL);
	if (!fib_node)
		return NULL;

	list_add(&fib_node->list, &fib->node_list);
	memcpy(fib_node->key.addr, addr, addr_len);
	fib_node->key.prefix_len = prefix_len;

	return fib_node;
}

static void mlxsw_sp_fib_node_destroy(struct mlxsw_sp_fib_node *fib_node)
{
	list_del(&fib_node->list);
	kfree(fib_node);
}

static int mlxsw_sp_fib_lpm_tree_link(struct mlxsw_sp *mlxsw_sp,
				      struct mlxsw_sp_fib_node *fib_node)
{
	struct mlxsw_sp_prefix_usage req_prefix_usage;
	struct mlxsw_sp_fib *fib = fib_node->fib;
	struct mlxsw_sp_lpm_tree *lpm_tree;
	int err;

	lpm_tree = mlxsw_sp->router->lpm.proto_trees[fib->proto];
	if (lpm_tree->prefix_ref_count[fib_node->key.prefix_len] != 0)
		goto out;

	mlxsw_sp_prefix_usage_cpy(&req_prefix_usage, &lpm_tree->prefix_usage);
	mlxsw_sp_prefix_usage_set(&req_prefix_usage, fib_node->key.prefix_len);
	lpm_tree = mlxsw_sp_lpm_tree_get(mlxsw_sp, &req_prefix_usage,
					 fib->proto);
	if (IS_ERR(lpm_tree))
		return PTR_ERR(lpm_tree);

	err = mlxsw_sp_vrs_lpm_tree_replace(mlxsw_sp, fib, lpm_tree);
	if (err)
		goto err_lpm_tree_replace;

out:
	lpm_tree->prefix_ref_count[fib_node->key.prefix_len]++;
	return 0;

err_lpm_tree_replace:
	mlxsw_sp_lpm_tree_put(mlxsw_sp, lpm_tree);
	return err;
}

static void mlxsw_sp_fib_lpm_tree_unlink(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_fib_node *fib_node)
{
	struct mlxsw_sp_lpm_tree *lpm_tree = fib_node->fib->lpm_tree;
	struct mlxsw_sp_prefix_usage req_prefix_usage;
	struct mlxsw_sp_fib *fib = fib_node->fib;
	int err;

	if (--lpm_tree->prefix_ref_count[fib_node->key.prefix_len] != 0)
		return;
	/* Try to construct a new LPM tree from the current prefix usage
	 * minus the unused one. If we fail, continue using the old one.
	 */
	mlxsw_sp_prefix_usage_cpy(&req_prefix_usage, &lpm_tree->prefix_usage);
	mlxsw_sp_prefix_usage_clear(&req_prefix_usage,
				    fib_node->key.prefix_len);
	lpm_tree = mlxsw_sp_lpm_tree_get(mlxsw_sp, &req_prefix_usage,
					 fib->proto);
	if (IS_ERR(lpm_tree))
		return;

	err = mlxsw_sp_vrs_lpm_tree_replace(mlxsw_sp, fib, lpm_tree);
	if (err)
		goto err_lpm_tree_replace;

	return;

err_lpm_tree_replace:
	mlxsw_sp_lpm_tree_put(mlxsw_sp, lpm_tree);
}

static int mlxsw_sp_fib_node_init(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_fib_node *fib_node,
				  struct mlxsw_sp_fib *fib)
{
	int err;

	err = mlxsw_sp_fib_node_insert(fib, fib_node);
	if (err)
		return err;
	fib_node->fib = fib;

	err = mlxsw_sp_fib_lpm_tree_link(mlxsw_sp, fib_node);
	if (err)
		goto err_fib_lpm_tree_link;

	return 0;

err_fib_lpm_tree_link:
	fib_node->fib = NULL;
	mlxsw_sp_fib_node_remove(fib, fib_node);
	return err;
}

static void mlxsw_sp_fib_node_fini(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_fib_node *fib_node)
{
	struct mlxsw_sp_fib *fib = fib_node->fib;

	mlxsw_sp_fib_lpm_tree_unlink(mlxsw_sp, fib_node);
	fib_node->fib = NULL;
	mlxsw_sp_fib_node_remove(fib, fib_node);
}

static struct mlxsw_sp_fib_node *
mlxsw_sp_fib_node_get(struct mlxsw_sp *mlxsw_sp, u32 tb_id, const void *addr,
		      size_t addr_len, unsigned char prefix_len,
		      enum mlxsw_sp_l3proto proto)
{
	struct mlxsw_sp_fib_node *fib_node;
	struct mlxsw_sp_fib *fib;
	struct mlxsw_sp_vr *vr;
	int err;

	vr = mlxsw_sp_vr_get(mlxsw_sp, tb_id, NULL);
	if (IS_ERR(vr))
		return ERR_CAST(vr);
	fib = mlxsw_sp_vr_fib(vr, proto);

	fib_node = mlxsw_sp_fib_node_lookup(fib, addr, addr_len, prefix_len);
	if (fib_node)
		return fib_node;

	fib_node = mlxsw_sp_fib_node_create(fib, addr, addr_len, prefix_len);
	if (!fib_node) {
		err = -ENOMEM;
		goto err_fib_node_create;
	}

	err = mlxsw_sp_fib_node_init(mlxsw_sp, fib_node, fib);
	if (err)
		goto err_fib_node_init;

	return fib_node;

err_fib_node_init:
	mlxsw_sp_fib_node_destroy(fib_node);
err_fib_node_create:
	mlxsw_sp_vr_put(mlxsw_sp, vr);
	return ERR_PTR(err);
}

static void mlxsw_sp_fib_node_put(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_fib_node *fib_node)
{
	struct mlxsw_sp_vr *vr = fib_node->fib->vr;

	if (fib_node->fib_entry)
		return;
	mlxsw_sp_fib_node_fini(mlxsw_sp, fib_node);
	mlxsw_sp_fib_node_destroy(fib_node);
	mlxsw_sp_vr_put(mlxsw_sp, vr);
}

static int mlxsw_sp_fib_node_entry_link(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_fib_node *fib_node = fib_entry->fib_node;
	int err;

	fib_node->fib_entry = fib_entry;

	err = mlxsw_sp_fib_entry_update(mlxsw_sp, fib_entry);
	if (err)
		goto err_fib_entry_update;

	return 0;

err_fib_entry_update:
	fib_node->fib_entry = NULL;
	return err;
}

static void
mlxsw_sp_fib_node_entry_unlink(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_fib_node *fib_node = fib_entry->fib_node;

	mlxsw_sp_fib_entry_del(mlxsw_sp, fib_entry);
	fib_node->fib_entry = NULL;
}

static bool mlxsw_sp_fib4_allow_replace(struct mlxsw_sp_fib4_entry *fib4_entry)
{
	struct mlxsw_sp_fib_node *fib_node = fib4_entry->common.fib_node;
	struct mlxsw_sp_fib4_entry *fib4_replaced;

	if (!fib_node->fib_entry)
		return true;

	fib4_replaced = container_of(fib_node->fib_entry,
				     struct mlxsw_sp_fib4_entry, common);
	if (fib4_entry->tb_id == RT_TABLE_MAIN &&
	    fib4_replaced->tb_id == RT_TABLE_LOCAL)
		return false;

	return true;
}

static int
mlxsw_sp_router_fib4_replace(struct mlxsw_sp *mlxsw_sp,
			     const struct fib_entry_notifier_info *fen_info)
{
	struct mlxsw_sp_fib4_entry *fib4_entry, *fib4_replaced;
	struct mlxsw_sp_fib_entry *replaced;
	struct mlxsw_sp_fib_node *fib_node;
	int err;

	if (fen_info->fi->nh &&
	    !mlxsw_sp_nexthop_obj_group_lookup(mlxsw_sp, fen_info->fi->nh->id))
		return 0;

	fib_node = mlxsw_sp_fib_node_get(mlxsw_sp, fen_info->tb_id,
					 &fen_info->dst, sizeof(fen_info->dst),
					 fen_info->dst_len,
					 MLXSW_SP_L3_PROTO_IPV4);
	if (IS_ERR(fib_node)) {
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to get FIB node\n");
		return PTR_ERR(fib_node);
	}

	fib4_entry = mlxsw_sp_fib4_entry_create(mlxsw_sp, fib_node, fen_info);
	if (IS_ERR(fib4_entry)) {
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to create FIB entry\n");
		err = PTR_ERR(fib4_entry);
		goto err_fib4_entry_create;
	}

	if (!mlxsw_sp_fib4_allow_replace(fib4_entry)) {
		mlxsw_sp_fib4_entry_destroy(mlxsw_sp, fib4_entry);
		mlxsw_sp_fib_node_put(mlxsw_sp, fib_node);
		return 0;
	}

	replaced = fib_node->fib_entry;
	err = mlxsw_sp_fib_node_entry_link(mlxsw_sp, &fib4_entry->common);
	if (err) {
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to link FIB entry to node\n");
		goto err_fib_node_entry_link;
	}

	/* Nothing to replace */
	if (!replaced)
		return 0;

	mlxsw_sp_fib_entry_hw_flags_clear(mlxsw_sp, replaced);
	fib4_replaced = container_of(replaced, struct mlxsw_sp_fib4_entry,
				     common);
	mlxsw_sp_fib4_entry_destroy(mlxsw_sp, fib4_replaced);

	return 0;

err_fib_node_entry_link:
	fib_node->fib_entry = replaced;
	mlxsw_sp_fib4_entry_destroy(mlxsw_sp, fib4_entry);
err_fib4_entry_create:
	mlxsw_sp_fib_node_put(mlxsw_sp, fib_node);
	return err;
}

static void mlxsw_sp_router_fib4_del(struct mlxsw_sp *mlxsw_sp,
				     struct fib_entry_notifier_info *fen_info)
{
	struct mlxsw_sp_fib4_entry *fib4_entry;
	struct mlxsw_sp_fib_node *fib_node;

	fib4_entry = mlxsw_sp_fib4_entry_lookup(mlxsw_sp, fen_info);
	if (!fib4_entry)
		return;
	fib_node = fib4_entry->common.fib_node;

	mlxsw_sp_fib_node_entry_unlink(mlxsw_sp, &fib4_entry->common);
	mlxsw_sp_fib4_entry_destroy(mlxsw_sp, fib4_entry);
	mlxsw_sp_fib_node_put(mlxsw_sp, fib_node);
}

static bool mlxsw_sp_fib6_rt_should_ignore(const struct fib6_info *rt)
{
	/* Multicast routes aren't supported, so ignore them. Neighbour
	 * Discovery packets are specifically trapped.
	 */
	if (ipv6_addr_type(&rt->fib6_dst.addr) & IPV6_ADDR_MULTICAST)
		return true;

	/* Cloned routes are irrelevant in the forwarding path. */
	if (rt->fib6_flags & RTF_CACHE)
		return true;

	return false;
}

static struct mlxsw_sp_rt6 *mlxsw_sp_rt6_create(struct fib6_info *rt)
{
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6;

	mlxsw_sp_rt6 = kzalloc(sizeof(*mlxsw_sp_rt6), GFP_KERNEL);
	if (!mlxsw_sp_rt6)
		return ERR_PTR(-ENOMEM);

	/* In case of route replace, replaced route is deleted with
	 * no notification. Take reference to prevent accessing freed
	 * memory.
	 */
	mlxsw_sp_rt6->rt = rt;
	fib6_info_hold(rt);

	return mlxsw_sp_rt6;
}

#if IS_ENABLED(CONFIG_IPV6)
static void mlxsw_sp_rt6_release(struct fib6_info *rt)
{
	fib6_info_release(rt);
}
#else
static void mlxsw_sp_rt6_release(struct fib6_info *rt)
{
}
#endif

static void mlxsw_sp_rt6_destroy(struct mlxsw_sp_rt6 *mlxsw_sp_rt6)
{
	struct fib6_nh *fib6_nh = mlxsw_sp_rt6->rt->fib6_nh;

	if (!mlxsw_sp_rt6->rt->nh)
		fib6_nh->fib_nh_flags &= ~RTNH_F_OFFLOAD;
	mlxsw_sp_rt6_release(mlxsw_sp_rt6->rt);
	kfree(mlxsw_sp_rt6);
}

static struct fib6_info *
mlxsw_sp_fib6_entry_rt(const struct mlxsw_sp_fib6_entry *fib6_entry)
{
	return list_first_entry(&fib6_entry->rt6_list, struct mlxsw_sp_rt6,
				list)->rt;
}

static struct mlxsw_sp_rt6 *
mlxsw_sp_fib6_entry_rt_find(const struct mlxsw_sp_fib6_entry *fib6_entry,
			    const struct fib6_info *rt)
{
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6;

	list_for_each_entry(mlxsw_sp_rt6, &fib6_entry->rt6_list, list) {
		if (mlxsw_sp_rt6->rt == rt)
			return mlxsw_sp_rt6;
	}

	return NULL;
}

static bool mlxsw_sp_nexthop6_ipip_type(const struct mlxsw_sp *mlxsw_sp,
					const struct fib6_info *rt,
					enum mlxsw_sp_ipip_type *ret)
{
	return rt->fib6_nh->fib_nh_dev &&
	       mlxsw_sp_netdev_ipip_type(mlxsw_sp, rt->fib6_nh->fib_nh_dev, ret);
}

static int mlxsw_sp_nexthop6_init(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_nexthop_group *nh_grp,
				  struct mlxsw_sp_nexthop *nh,
				  const struct fib6_info *rt)
{
	struct net_device *dev = rt->fib6_nh->fib_nh_dev;
	int err;

	nh->nhgi = nh_grp->nhgi;
	nh->nh_weight = rt->fib6_nh->fib_nh_weight;
	memcpy(&nh->gw_addr, &rt->fib6_nh->fib_nh_gw6, sizeof(nh->gw_addr));
#if IS_ENABLED(CONFIG_IPV6)
	nh->neigh_tbl = &nd_tbl;
#endif
	mlxsw_sp_nexthop_counter_alloc(mlxsw_sp, nh);

	list_add_tail(&nh->router_list_node, &mlxsw_sp->router->nexthop_list);

	if (!dev)
		return 0;
	nh->ifindex = dev->ifindex;

	err = mlxsw_sp_nexthop_type_init(mlxsw_sp, nh, dev);
	if (err)
		goto err_nexthop_type_init;

	return 0;

err_nexthop_type_init:
	list_del(&nh->router_list_node);
	mlxsw_sp_nexthop_counter_free(mlxsw_sp, nh);
	return err;
}

static void mlxsw_sp_nexthop6_fini(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_nexthop *nh)
{
	mlxsw_sp_nexthop_type_fini(mlxsw_sp, nh);
	list_del(&nh->router_list_node);
	mlxsw_sp_nexthop_counter_free(mlxsw_sp, nh);
}

static bool mlxsw_sp_rt6_is_gateway(const struct mlxsw_sp *mlxsw_sp,
				    const struct fib6_info *rt)
{
	return rt->fib6_nh->fib_nh_gw_family ||
	       mlxsw_sp_nexthop6_ipip_type(mlxsw_sp, rt, NULL);
}

static int
mlxsw_sp_nexthop6_group_info_init(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_nexthop_group *nh_grp,
				  struct mlxsw_sp_fib6_entry *fib6_entry)
{
	struct mlxsw_sp_nexthop_group_info *nhgi;
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6;
	struct mlxsw_sp_nexthop *nh;
	int err, i;

	nhgi = kzalloc(struct_size(nhgi, nexthops, fib6_entry->nrt6),
		       GFP_KERNEL);
	if (!nhgi)
		return -ENOMEM;
	nh_grp->nhgi = nhgi;
	nhgi->nh_grp = nh_grp;
	mlxsw_sp_rt6 = list_first_entry(&fib6_entry->rt6_list,
					struct mlxsw_sp_rt6, list);
	nhgi->gateway = mlxsw_sp_rt6_is_gateway(mlxsw_sp, mlxsw_sp_rt6->rt);
	nhgi->count = fib6_entry->nrt6;
	for (i = 0; i < nhgi->count; i++) {
		struct fib6_info *rt = mlxsw_sp_rt6->rt;

		nh = &nhgi->nexthops[i];
		err = mlxsw_sp_nexthop6_init(mlxsw_sp, nh_grp, nh, rt);
		if (err)
			goto err_nexthop6_init;
		mlxsw_sp_rt6 = list_next_entry(mlxsw_sp_rt6, list);
	}
	nh_grp->nhgi = nhgi;
	err = mlxsw_sp_nexthop_group_inc(mlxsw_sp);
	if (err)
		goto err_group_inc;
	err = mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh_grp);
	if (err)
		goto err_group_refresh;

	return 0;

err_group_refresh:
	mlxsw_sp_nexthop_group_dec(mlxsw_sp);
err_group_inc:
	i = nhgi->count;
err_nexthop6_init:
	for (i--; i >= 0; i--) {
		nh = &nhgi->nexthops[i];
		mlxsw_sp_nexthop6_fini(mlxsw_sp, nh);
	}
	kfree(nhgi);
	return err;
}

static void
mlxsw_sp_nexthop6_group_info_fini(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_nexthop_group *nh_grp)
{
	struct mlxsw_sp_nexthop_group_info *nhgi = nh_grp->nhgi;
	int i;

	mlxsw_sp_nexthop_group_dec(mlxsw_sp);
	for (i = nhgi->count - 1; i >= 0; i--) {
		struct mlxsw_sp_nexthop *nh = &nhgi->nexthops[i];

		mlxsw_sp_nexthop6_fini(mlxsw_sp, nh);
	}
	mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh_grp);
	WARN_ON_ONCE(nhgi->adj_index_valid);
	kfree(nhgi);
}

static struct mlxsw_sp_nexthop_group *
mlxsw_sp_nexthop6_group_create(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_fib6_entry *fib6_entry)
{
	struct mlxsw_sp_nexthop_group *nh_grp;
	int err;

	nh_grp = kzalloc(sizeof(*nh_grp), GFP_KERNEL);
	if (!nh_grp)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&nh_grp->vr_list);
	err = rhashtable_init(&nh_grp->vr_ht,
			      &mlxsw_sp_nexthop_group_vr_ht_params);
	if (err)
		goto err_nexthop_group_vr_ht_init;
	INIT_LIST_HEAD(&nh_grp->fib_list);
	nh_grp->type = MLXSW_SP_NEXTHOP_GROUP_TYPE_IPV6;

	err = mlxsw_sp_nexthop6_group_info_init(mlxsw_sp, nh_grp, fib6_entry);
	if (err)
		goto err_nexthop_group_info_init;

	err = mlxsw_sp_nexthop_group_insert(mlxsw_sp, nh_grp);
	if (err)
		goto err_nexthop_group_insert;

	nh_grp->can_destroy = true;

	return nh_grp;

err_nexthop_group_insert:
	mlxsw_sp_nexthop6_group_info_fini(mlxsw_sp, nh_grp);
err_nexthop_group_info_init:
	rhashtable_destroy(&nh_grp->vr_ht);
err_nexthop_group_vr_ht_init:
	kfree(nh_grp);
	return ERR_PTR(err);
}

static void
mlxsw_sp_nexthop6_group_destroy(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_nexthop_group *nh_grp)
{
	if (!nh_grp->can_destroy)
		return;
	mlxsw_sp_nexthop_group_remove(mlxsw_sp, nh_grp);
	mlxsw_sp_nexthop6_group_info_fini(mlxsw_sp, nh_grp);
	WARN_ON_ONCE(!list_empty(&nh_grp->vr_list));
	rhashtable_destroy(&nh_grp->vr_ht);
	kfree(nh_grp);
}

static int mlxsw_sp_nexthop6_group_get(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_fib6_entry *fib6_entry)
{
	struct fib6_info *rt = mlxsw_sp_fib6_entry_rt(fib6_entry);
	struct mlxsw_sp_nexthop_group *nh_grp;

	if (rt->nh) {
		nh_grp = mlxsw_sp_nexthop_obj_group_lookup(mlxsw_sp,
							   rt->nh->id);
		if (WARN_ON_ONCE(!nh_grp))
			return -EINVAL;
		goto out;
	}

	nh_grp = mlxsw_sp_nexthop6_group_lookup(mlxsw_sp, fib6_entry);
	if (!nh_grp) {
		nh_grp = mlxsw_sp_nexthop6_group_create(mlxsw_sp, fib6_entry);
		if (IS_ERR(nh_grp))
			return PTR_ERR(nh_grp);
	}

	/* The route and the nexthop are described by the same struct, so we
	 * need to the update the nexthop offload indication for the new route.
	 */
	__mlxsw_sp_nexthop6_group_offload_refresh(nh_grp, fib6_entry);

out:
	list_add_tail(&fib6_entry->common.nexthop_group_node,
		      &nh_grp->fib_list);
	fib6_entry->common.nh_group = nh_grp;

	return 0;
}

static void mlxsw_sp_nexthop6_group_put(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_nexthop_group *nh_grp = fib_entry->nh_group;

	list_del(&fib_entry->nexthop_group_node);
	if (!list_empty(&nh_grp->fib_list))
		return;

	if (nh_grp->type == MLXSW_SP_NEXTHOP_GROUP_TYPE_OBJ) {
		mlxsw_sp_nexthop_obj_group_destroy(mlxsw_sp, nh_grp);
		return;
	}

	mlxsw_sp_nexthop6_group_destroy(mlxsw_sp, nh_grp);
}

static int
mlxsw_sp_nexthop6_group_update(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_fib6_entry *fib6_entry)
{
	struct mlxsw_sp_nexthop_group *old_nh_grp = fib6_entry->common.nh_group;
	struct mlxsw_sp_fib_node *fib_node = fib6_entry->common.fib_node;
	int err;

	mlxsw_sp_nexthop_group_vr_unlink(old_nh_grp, fib_node->fib);
	fib6_entry->common.nh_group = NULL;
	list_del(&fib6_entry->common.nexthop_group_node);

	err = mlxsw_sp_nexthop6_group_get(mlxsw_sp, fib6_entry);
	if (err)
		goto err_nexthop6_group_get;

	err = mlxsw_sp_nexthop_group_vr_link(fib6_entry->common.nh_group,
					     fib_node->fib);
	if (err)
		goto err_nexthop_group_vr_link;

	/* In case this entry is offloaded, then the adjacency index
	 * currently associated with it in the device's table is that
	 * of the old group. Start using the new one instead.
	 */
	err = mlxsw_sp_fib_entry_update(mlxsw_sp, &fib6_entry->common);
	if (err)
		goto err_fib_entry_update;

	if (list_empty(&old_nh_grp->fib_list))
		mlxsw_sp_nexthop6_group_destroy(mlxsw_sp, old_nh_grp);

	return 0;

err_fib_entry_update:
	mlxsw_sp_nexthop_group_vr_unlink(fib6_entry->common.nh_group,
					 fib_node->fib);
err_nexthop_group_vr_link:
	mlxsw_sp_nexthop6_group_put(mlxsw_sp, &fib6_entry->common);
err_nexthop6_group_get:
	list_add_tail(&fib6_entry->common.nexthop_group_node,
		      &old_nh_grp->fib_list);
	fib6_entry->common.nh_group = old_nh_grp;
	mlxsw_sp_nexthop_group_vr_link(old_nh_grp, fib_node->fib);
	return err;
}

static int
mlxsw_sp_fib6_entry_nexthop_add(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_fib6_entry *fib6_entry,
				struct fib6_info **rt_arr, unsigned int nrt6)
{
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6;
	int err, i;

	for (i = 0; i < nrt6; i++) {
		mlxsw_sp_rt6 = mlxsw_sp_rt6_create(rt_arr[i]);
		if (IS_ERR(mlxsw_sp_rt6)) {
			err = PTR_ERR(mlxsw_sp_rt6);
			goto err_rt6_unwind;
		}

		list_add_tail(&mlxsw_sp_rt6->list, &fib6_entry->rt6_list);
		fib6_entry->nrt6++;
	}

	err = mlxsw_sp_nexthop6_group_update(mlxsw_sp, fib6_entry);
	if (err)
		goto err_rt6_unwind;

	return 0;

err_rt6_unwind:
	for (; i > 0; i--) {
		fib6_entry->nrt6--;
		mlxsw_sp_rt6 = list_last_entry(&fib6_entry->rt6_list,
					       struct mlxsw_sp_rt6, list);
		list_del(&mlxsw_sp_rt6->list);
		mlxsw_sp_rt6_destroy(mlxsw_sp_rt6);
	}
	return err;
}

static void
mlxsw_sp_fib6_entry_nexthop_del(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_fib6_entry *fib6_entry,
				struct fib6_info **rt_arr, unsigned int nrt6)
{
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6;
	int i;

	for (i = 0; i < nrt6; i++) {
		mlxsw_sp_rt6 = mlxsw_sp_fib6_entry_rt_find(fib6_entry,
							   rt_arr[i]);
		if (WARN_ON_ONCE(!mlxsw_sp_rt6))
			continue;

		fib6_entry->nrt6--;
		list_del(&mlxsw_sp_rt6->list);
		mlxsw_sp_rt6_destroy(mlxsw_sp_rt6);
	}

	mlxsw_sp_nexthop6_group_update(mlxsw_sp, fib6_entry);
}

static int
mlxsw_sp_fib6_entry_type_set_local(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_fib_entry *fib_entry,
				   const struct fib6_info *rt)
{
	struct mlxsw_sp_nexthop_group_info *nhgi = fib_entry->nh_group->nhgi;
	union mlxsw_sp_l3addr dip = { .addr6 = rt->fib6_dst.addr };
	u32 tb_id = mlxsw_sp_fix_tb_id(rt->fib6_table->tb6_id);
	struct mlxsw_sp_router *router = mlxsw_sp->router;
	int ifindex = nhgi->nexthops[0].ifindex;
	struct mlxsw_sp_ipip_entry *ipip_entry;

	fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_TRAP;
	ipip_entry = mlxsw_sp_ipip_entry_find_by_decap(mlxsw_sp, ifindex,
						       MLXSW_SP_L3_PROTO_IPV6,
						       dip);

	if (ipip_entry && ipip_entry->ol_dev->flags & IFF_UP) {
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_IPIP_DECAP;
		return mlxsw_sp_fib_entry_decap_init(mlxsw_sp, fib_entry,
						     ipip_entry);
	}
	if (mlxsw_sp_router_nve_is_decap(mlxsw_sp, tb_id,
					 MLXSW_SP_L3_PROTO_IPV6, &dip)) {
		u32 tunnel_index;

		tunnel_index = router->nve_decap_config.tunnel_index;
		fib_entry->decap.tunnel_index = tunnel_index;
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_NVE_DECAP;
	}

	return 0;
}

static int mlxsw_sp_fib6_entry_type_set(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib_entry *fib_entry,
					const struct fib6_info *rt)
{
	if (rt->fib6_flags & RTF_LOCAL)
		return mlxsw_sp_fib6_entry_type_set_local(mlxsw_sp, fib_entry,
							  rt);
	if (rt->fib6_flags & RTF_ANYCAST)
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_TRAP;
	else if (rt->fib6_type == RTN_BLACKHOLE)
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_BLACKHOLE;
	else if (rt->fib6_flags & RTF_REJECT)
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_UNREACHABLE;
	else if (fib_entry->nh_group->nhgi->gateway)
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_REMOTE;
	else
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_LOCAL;

	return 0;
}

static void
mlxsw_sp_fib6_entry_rt_destroy_all(struct mlxsw_sp_fib6_entry *fib6_entry)
{
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6, *tmp;

	list_for_each_entry_safe(mlxsw_sp_rt6, tmp, &fib6_entry->rt6_list,
				 list) {
		fib6_entry->nrt6--;
		list_del(&mlxsw_sp_rt6->list);
		mlxsw_sp_rt6_destroy(mlxsw_sp_rt6);
	}
}

static struct mlxsw_sp_fib6_entry *
mlxsw_sp_fib6_entry_create(struct mlxsw_sp *mlxsw_sp,
			   struct mlxsw_sp_fib_node *fib_node,
			   struct fib6_info **rt_arr, unsigned int nrt6)
{
	struct mlxsw_sp_fib6_entry *fib6_entry;
	struct mlxsw_sp_fib_entry *fib_entry;
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6;
	int err, i;

	fib6_entry = kzalloc(sizeof(*fib6_entry), GFP_KERNEL);
	if (!fib6_entry)
		return ERR_PTR(-ENOMEM);
	fib_entry = &fib6_entry->common;

	INIT_LIST_HEAD(&fib6_entry->rt6_list);

	for (i = 0; i < nrt6; i++) {
		mlxsw_sp_rt6 = mlxsw_sp_rt6_create(rt_arr[i]);
		if (IS_ERR(mlxsw_sp_rt6)) {
			err = PTR_ERR(mlxsw_sp_rt6);
			goto err_rt6_unwind;
		}
		list_add_tail(&mlxsw_sp_rt6->list, &fib6_entry->rt6_list);
		fib6_entry->nrt6++;
	}

	err = mlxsw_sp_nexthop6_group_get(mlxsw_sp, fib6_entry);
	if (err)
		goto err_rt6_unwind;

	err = mlxsw_sp_nexthop_group_vr_link(fib_entry->nh_group,
					     fib_node->fib);
	if (err)
		goto err_nexthop_group_vr_link;

	err = mlxsw_sp_fib6_entry_type_set(mlxsw_sp, fib_entry, rt_arr[0]);
	if (err)
		goto err_fib6_entry_type_set;

	fib_entry->fib_node = fib_node;

	return fib6_entry;

err_fib6_entry_type_set:
	mlxsw_sp_nexthop_group_vr_unlink(fib_entry->nh_group, fib_node->fib);
err_nexthop_group_vr_link:
	mlxsw_sp_nexthop6_group_put(mlxsw_sp, fib_entry);
err_rt6_unwind:
	for (; i > 0; i--) {
		fib6_entry->nrt6--;
		mlxsw_sp_rt6 = list_last_entry(&fib6_entry->rt6_list,
					       struct mlxsw_sp_rt6, list);
		list_del(&mlxsw_sp_rt6->list);
		mlxsw_sp_rt6_destroy(mlxsw_sp_rt6);
	}
	kfree(fib6_entry);
	return ERR_PTR(err);
}

static void
mlxsw_sp_fib6_entry_type_unset(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_fib6_entry *fib6_entry)
{
	mlxsw_sp_fib_entry_type_unset(mlxsw_sp, &fib6_entry->common);
}

static void mlxsw_sp_fib6_entry_destroy(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib6_entry *fib6_entry)
{
	struct mlxsw_sp_fib_node *fib_node = fib6_entry->common.fib_node;

	mlxsw_sp_fib6_entry_type_unset(mlxsw_sp, fib6_entry);
	mlxsw_sp_nexthop_group_vr_unlink(fib6_entry->common.nh_group,
					 fib_node->fib);
	mlxsw_sp_nexthop6_group_put(mlxsw_sp, &fib6_entry->common);
	mlxsw_sp_fib6_entry_rt_destroy_all(fib6_entry);
	WARN_ON(fib6_entry->nrt6);
	kfree(fib6_entry);
}

static struct mlxsw_sp_fib6_entry *
mlxsw_sp_fib6_entry_lookup(struct mlxsw_sp *mlxsw_sp,
			   const struct fib6_info *rt)
{
	struct mlxsw_sp_fib6_entry *fib6_entry;
	struct mlxsw_sp_fib_node *fib_node;
	struct mlxsw_sp_fib *fib;
	struct fib6_info *cmp_rt;
	struct mlxsw_sp_vr *vr;

	vr = mlxsw_sp_vr_find(mlxsw_sp, rt->fib6_table->tb6_id);
	if (!vr)
		return NULL;
	fib = mlxsw_sp_vr_fib(vr, MLXSW_SP_L3_PROTO_IPV6);

	fib_node = mlxsw_sp_fib_node_lookup(fib, &rt->fib6_dst.addr,
					    sizeof(rt->fib6_dst.addr),
					    rt->fib6_dst.plen);
	if (!fib_node)
		return NULL;

	fib6_entry = container_of(fib_node->fib_entry,
				  struct mlxsw_sp_fib6_entry, common);
	cmp_rt = mlxsw_sp_fib6_entry_rt(fib6_entry);
	if (rt->fib6_table->tb6_id == cmp_rt->fib6_table->tb6_id &&
	    rt->fib6_metric == cmp_rt->fib6_metric &&
	    mlxsw_sp_fib6_entry_rt_find(fib6_entry, rt))
		return fib6_entry;

	return NULL;
}

static bool mlxsw_sp_fib6_allow_replace(struct mlxsw_sp_fib6_entry *fib6_entry)
{
	struct mlxsw_sp_fib_node *fib_node = fib6_entry->common.fib_node;
	struct mlxsw_sp_fib6_entry *fib6_replaced;
	struct fib6_info *rt, *rt_replaced;

	if (!fib_node->fib_entry)
		return true;

	fib6_replaced = container_of(fib_node->fib_entry,
				     struct mlxsw_sp_fib6_entry,
				     common);
	rt = mlxsw_sp_fib6_entry_rt(fib6_entry);
	rt_replaced = mlxsw_sp_fib6_entry_rt(fib6_replaced);
	if (rt->fib6_table->tb6_id == RT_TABLE_MAIN &&
	    rt_replaced->fib6_table->tb6_id == RT_TABLE_LOCAL)
		return false;

	return true;
}

static int mlxsw_sp_router_fib6_replace(struct mlxsw_sp *mlxsw_sp,
					struct fib6_info **rt_arr,
					unsigned int nrt6)
{
	struct mlxsw_sp_fib6_entry *fib6_entry, *fib6_replaced;
	struct mlxsw_sp_fib_entry *replaced;
	struct mlxsw_sp_fib_node *fib_node;
	struct fib6_info *rt = rt_arr[0];
	int err;

	if (rt->fib6_src.plen)
		return -EINVAL;

	if (mlxsw_sp_fib6_rt_should_ignore(rt))
		return 0;

	if (rt->nh && !mlxsw_sp_nexthop_obj_group_lookup(mlxsw_sp, rt->nh->id))
		return 0;

	fib_node = mlxsw_sp_fib_node_get(mlxsw_sp, rt->fib6_table->tb6_id,
					 &rt->fib6_dst.addr,
					 sizeof(rt->fib6_dst.addr),
					 rt->fib6_dst.plen,
					 MLXSW_SP_L3_PROTO_IPV6);
	if (IS_ERR(fib_node))
		return PTR_ERR(fib_node);

	fib6_entry = mlxsw_sp_fib6_entry_create(mlxsw_sp, fib_node, rt_arr,
						nrt6);
	if (IS_ERR(fib6_entry)) {
		err = PTR_ERR(fib6_entry);
		goto err_fib6_entry_create;
	}

	if (!mlxsw_sp_fib6_allow_replace(fib6_entry)) {
		mlxsw_sp_fib6_entry_destroy(mlxsw_sp, fib6_entry);
		mlxsw_sp_fib_node_put(mlxsw_sp, fib_node);
		return 0;
	}

	replaced = fib_node->fib_entry;
	err = mlxsw_sp_fib_node_entry_link(mlxsw_sp, &fib6_entry->common);
	if (err)
		goto err_fib_node_entry_link;

	/* Nothing to replace */
	if (!replaced)
		return 0;

	mlxsw_sp_fib_entry_hw_flags_clear(mlxsw_sp, replaced);
	fib6_replaced = container_of(replaced, struct mlxsw_sp_fib6_entry,
				     common);
	mlxsw_sp_fib6_entry_destroy(mlxsw_sp, fib6_replaced);

	return 0;

err_fib_node_entry_link:
	fib_node->fib_entry = replaced;
	mlxsw_sp_fib6_entry_destroy(mlxsw_sp, fib6_entry);
err_fib6_entry_create:
	mlxsw_sp_fib_node_put(mlxsw_sp, fib_node);
	return err;
}

static int mlxsw_sp_router_fib6_append(struct mlxsw_sp *mlxsw_sp,
				       struct fib6_info **rt_arr,
				       unsigned int nrt6)
{
	struct mlxsw_sp_fib6_entry *fib6_entry;
	struct mlxsw_sp_fib_node *fib_node;
	struct fib6_info *rt = rt_arr[0];
	int err;

	if (rt->fib6_src.plen)
		return -EINVAL;

	if (mlxsw_sp_fib6_rt_should_ignore(rt))
		return 0;

	fib_node = mlxsw_sp_fib_node_get(mlxsw_sp, rt->fib6_table->tb6_id,
					 &rt->fib6_dst.addr,
					 sizeof(rt->fib6_dst.addr),
					 rt->fib6_dst.plen,
					 MLXSW_SP_L3_PROTO_IPV6);
	if (IS_ERR(fib_node))
		return PTR_ERR(fib_node);

	if (WARN_ON_ONCE(!fib_node->fib_entry)) {
		mlxsw_sp_fib_node_put(mlxsw_sp, fib_node);
		return -EINVAL;
	}

	fib6_entry = container_of(fib_node->fib_entry,
				  struct mlxsw_sp_fib6_entry, common);
	err = mlxsw_sp_fib6_entry_nexthop_add(mlxsw_sp, fib6_entry, rt_arr,
					      nrt6);
	if (err)
		goto err_fib6_entry_nexthop_add;

	return 0;

err_fib6_entry_nexthop_add:
	mlxsw_sp_fib_node_put(mlxsw_sp, fib_node);
	return err;
}

static void mlxsw_sp_router_fib6_del(struct mlxsw_sp *mlxsw_sp,
				     struct fib6_info **rt_arr,
				     unsigned int nrt6)
{
	struct mlxsw_sp_fib6_entry *fib6_entry;
	struct mlxsw_sp_fib_node *fib_node;
	struct fib6_info *rt = rt_arr[0];

	if (mlxsw_sp_fib6_rt_should_ignore(rt))
		return;

	/* Multipath routes are first added to the FIB trie and only then
	 * notified. If we vetoed the addition, we will get a delete
	 * notification for a route we do not have. Therefore, do not warn if
	 * route was not found.
	 */
	fib6_entry = mlxsw_sp_fib6_entry_lookup(mlxsw_sp, rt);
	if (!fib6_entry)
		return;

	/* If not all the nexthops are deleted, then only reduce the nexthop
	 * group.
	 */
	if (nrt6 != fib6_entry->nrt6) {
		mlxsw_sp_fib6_entry_nexthop_del(mlxsw_sp, fib6_entry, rt_arr,
						nrt6);
		return;
	}

	fib_node = fib6_entry->common.fib_node;

	mlxsw_sp_fib_node_entry_unlink(mlxsw_sp, &fib6_entry->common);
	mlxsw_sp_fib6_entry_destroy(mlxsw_sp, fib6_entry);
	mlxsw_sp_fib_node_put(mlxsw_sp, fib_node);
}

static struct mlxsw_sp_mr_table *
mlxsw_sp_router_fibmr_family_to_table(struct mlxsw_sp_vr *vr, int family)
{
	if (family == RTNL_FAMILY_IPMR)
		return vr->mr_table[MLXSW_SP_L3_PROTO_IPV4];
	else
		return vr->mr_table[MLXSW_SP_L3_PROTO_IPV6];
}

static int mlxsw_sp_router_fibmr_add(struct mlxsw_sp *mlxsw_sp,
				     struct mfc_entry_notifier_info *men_info,
				     bool replace)
{
	struct mlxsw_sp_mr_table *mrt;
	struct mlxsw_sp_vr *vr;

	vr = mlxsw_sp_vr_get(mlxsw_sp, men_info->tb_id, NULL);
	if (IS_ERR(vr))
		return PTR_ERR(vr);

	mrt = mlxsw_sp_router_fibmr_family_to_table(vr, men_info->info.family);
	return mlxsw_sp_mr_route_add(mrt, men_info->mfc, replace);
}

static void mlxsw_sp_router_fibmr_del(struct mlxsw_sp *mlxsw_sp,
				      struct mfc_entry_notifier_info *men_info)
{
	struct mlxsw_sp_mr_table *mrt;
	struct mlxsw_sp_vr *vr;

	vr = mlxsw_sp_vr_find(mlxsw_sp, men_info->tb_id);
	if (WARN_ON(!vr))
		return;

	mrt = mlxsw_sp_router_fibmr_family_to_table(vr, men_info->info.family);
	mlxsw_sp_mr_route_del(mrt, men_info->mfc);
	mlxsw_sp_vr_put(mlxsw_sp, vr);
}

static int
mlxsw_sp_router_fibmr_vif_add(struct mlxsw_sp *mlxsw_sp,
			      struct vif_entry_notifier_info *ven_info)
{
	struct mlxsw_sp_mr_table *mrt;
	struct mlxsw_sp_rif *rif;
	struct mlxsw_sp_vr *vr;

	vr = mlxsw_sp_vr_get(mlxsw_sp, ven_info->tb_id, NULL);
	if (IS_ERR(vr))
		return PTR_ERR(vr);

	mrt = mlxsw_sp_router_fibmr_family_to_table(vr, ven_info->info.family);
	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, ven_info->dev);
	return mlxsw_sp_mr_vif_add(mrt, ven_info->dev,
				   ven_info->vif_index,
				   ven_info->vif_flags, rif);
}

static void
mlxsw_sp_router_fibmr_vif_del(struct mlxsw_sp *mlxsw_sp,
			      struct vif_entry_notifier_info *ven_info)
{
	struct mlxsw_sp_mr_table *mrt;
	struct mlxsw_sp_vr *vr;

	vr = mlxsw_sp_vr_find(mlxsw_sp, ven_info->tb_id);
	if (WARN_ON(!vr))
		return;

	mrt = mlxsw_sp_router_fibmr_family_to_table(vr, ven_info->info.family);
	mlxsw_sp_mr_vif_del(mrt, ven_info->vif_index);
	mlxsw_sp_vr_put(mlxsw_sp, vr);
}

static void mlxsw_sp_fib4_node_flush(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_fib_node *fib_node)
{
	struct mlxsw_sp_fib4_entry *fib4_entry;

	fib4_entry = container_of(fib_node->fib_entry,
				  struct mlxsw_sp_fib4_entry, common);
	mlxsw_sp_fib_node_entry_unlink(mlxsw_sp, fib_node->fib_entry);
	mlxsw_sp_fib4_entry_destroy(mlxsw_sp, fib4_entry);
	mlxsw_sp_fib_node_put(mlxsw_sp, fib_node);
}

static void mlxsw_sp_fib6_node_flush(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_fib_node *fib_node)
{
	struct mlxsw_sp_fib6_entry *fib6_entry;

	fib6_entry = container_of(fib_node->fib_entry,
				  struct mlxsw_sp_fib6_entry, common);
	mlxsw_sp_fib_node_entry_unlink(mlxsw_sp, fib_node->fib_entry);
	mlxsw_sp_fib6_entry_destroy(mlxsw_sp, fib6_entry);
	mlxsw_sp_fib_node_put(mlxsw_sp, fib_node);
}

static void mlxsw_sp_fib_node_flush(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_fib_node *fib_node)
{
	switch (fib_node->fib->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		mlxsw_sp_fib4_node_flush(mlxsw_sp, fib_node);
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		mlxsw_sp_fib6_node_flush(mlxsw_sp, fib_node);
		break;
	}
}

static void mlxsw_sp_vr_fib_flush(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_vr *vr,
				  enum mlxsw_sp_l3proto proto)
{
	struct mlxsw_sp_fib *fib = mlxsw_sp_vr_fib(vr, proto);
	struct mlxsw_sp_fib_node *fib_node, *tmp;

	list_for_each_entry_safe(fib_node, tmp, &fib->node_list, list) {
		bool do_break = &tmp->list == &fib->node_list;

		mlxsw_sp_fib_node_flush(mlxsw_sp, fib_node);
		if (do_break)
			break;
	}
}

static void mlxsw_sp_router_fib_flush(struct mlxsw_sp *mlxsw_sp)
{
	int max_vrs = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_VRS);
	int i, j;

	for (i = 0; i < max_vrs; i++) {
		struct mlxsw_sp_vr *vr = &mlxsw_sp->router->vrs[i];

		if (!mlxsw_sp_vr_is_used(vr))
			continue;

		for (j = 0; j < MLXSW_SP_L3_PROTO_MAX; j++)
			mlxsw_sp_mr_table_flush(vr->mr_table[j]);
		mlxsw_sp_vr_fib_flush(mlxsw_sp, vr, MLXSW_SP_L3_PROTO_IPV4);

		/* If virtual router was only used for IPv4, then it's no
		 * longer used.
		 */
		if (!mlxsw_sp_vr_is_used(vr))
			continue;
		mlxsw_sp_vr_fib_flush(mlxsw_sp, vr, MLXSW_SP_L3_PROTO_IPV6);
	}
}

struct mlxsw_sp_fib6_event_work {
	struct fib6_info **rt_arr;
	unsigned int nrt6;
};

struct mlxsw_sp_fib_event_work {
	struct work_struct work;
	union {
		struct mlxsw_sp_fib6_event_work fib6_work;
		struct fib_entry_notifier_info fen_info;
		struct fib_rule_notifier_info fr_info;
		struct fib_nh_notifier_info fnh_info;
		struct mfc_entry_notifier_info men_info;
		struct vif_entry_notifier_info ven_info;
	};
	struct mlxsw_sp *mlxsw_sp;
	unsigned long event;
};

static int
mlxsw_sp_router_fib6_work_init(struct mlxsw_sp_fib6_event_work *fib6_work,
			       struct fib6_entry_notifier_info *fen6_info)
{
	struct fib6_info *rt = fen6_info->rt;
	struct fib6_info **rt_arr;
	struct fib6_info *iter;
	unsigned int nrt6;
	int i = 0;

	nrt6 = fen6_info->nsiblings + 1;

	rt_arr = kcalloc(nrt6, sizeof(struct fib6_info *), GFP_ATOMIC);
	if (!rt_arr)
		return -ENOMEM;

	fib6_work->rt_arr = rt_arr;
	fib6_work->nrt6 = nrt6;

	rt_arr[0] = rt;
	fib6_info_hold(rt);

	if (!fen6_info->nsiblings)
		return 0;

	list_for_each_entry(iter, &rt->fib6_siblings, fib6_siblings) {
		if (i == fen6_info->nsiblings)
			break;

		rt_arr[i + 1] = iter;
		fib6_info_hold(iter);
		i++;
	}
	WARN_ON_ONCE(i != fen6_info->nsiblings);

	return 0;
}

static void
mlxsw_sp_router_fib6_work_fini(struct mlxsw_sp_fib6_event_work *fib6_work)
{
	int i;

	for (i = 0; i < fib6_work->nrt6; i++)
		mlxsw_sp_rt6_release(fib6_work->rt_arr[i]);
	kfree(fib6_work->rt_arr);
}

static void mlxsw_sp_router_fib4_event_work(struct work_struct *work)
{
	struct mlxsw_sp_fib_event_work *fib_work =
		container_of(work, struct mlxsw_sp_fib_event_work, work);
	struct mlxsw_sp *mlxsw_sp = fib_work->mlxsw_sp;
	int err;

	mutex_lock(&mlxsw_sp->router->lock);
	mlxsw_sp_span_respin(mlxsw_sp);

	switch (fib_work->event) {
	case FIB_EVENT_ENTRY_REPLACE:
		err = mlxsw_sp_router_fib4_replace(mlxsw_sp,
						   &fib_work->fen_info);
		if (err) {
			dev_warn(mlxsw_sp->bus_info->dev, "FIB replace failed.\n");
			mlxsw_sp_fib4_offload_failed_flag_set(mlxsw_sp,
							      &fib_work->fen_info);
		}
		fib_info_put(fib_work->fen_info.fi);
		break;
	case FIB_EVENT_ENTRY_DEL:
		mlxsw_sp_router_fib4_del(mlxsw_sp, &fib_work->fen_info);
		fib_info_put(fib_work->fen_info.fi);
		break;
	case FIB_EVENT_NH_ADD:
	case FIB_EVENT_NH_DEL:
		mlxsw_sp_nexthop4_event(mlxsw_sp, fib_work->event,
					fib_work->fnh_info.fib_nh);
		fib_info_put(fib_work->fnh_info.fib_nh->nh_parent);
		break;
	}
	mutex_unlock(&mlxsw_sp->router->lock);
	kfree(fib_work);
}

static void mlxsw_sp_router_fib6_event_work(struct work_struct *work)
{
	struct mlxsw_sp_fib_event_work *fib_work =
		    container_of(work, struct mlxsw_sp_fib_event_work, work);
	struct mlxsw_sp_fib6_event_work *fib6_work = &fib_work->fib6_work;
	struct mlxsw_sp *mlxsw_sp = fib_work->mlxsw_sp;
	int err;

	mutex_lock(&mlxsw_sp->router->lock);
	mlxsw_sp_span_respin(mlxsw_sp);

	switch (fib_work->event) {
	case FIB_EVENT_ENTRY_REPLACE:
		err = mlxsw_sp_router_fib6_replace(mlxsw_sp,
						   fib6_work->rt_arr,
						   fib6_work->nrt6);
		if (err) {
			dev_warn(mlxsw_sp->bus_info->dev, "FIB replace failed.\n");
			mlxsw_sp_fib6_offload_failed_flag_set(mlxsw_sp,
							      fib6_work->rt_arr,
							      fib6_work->nrt6);
		}
		mlxsw_sp_router_fib6_work_fini(fib6_work);
		break;
	case FIB_EVENT_ENTRY_APPEND:
		err = mlxsw_sp_router_fib6_append(mlxsw_sp,
						  fib6_work->rt_arr,
						  fib6_work->nrt6);
		if (err) {
			dev_warn(mlxsw_sp->bus_info->dev, "FIB append failed.\n");
			mlxsw_sp_fib6_offload_failed_flag_set(mlxsw_sp,
							      fib6_work->rt_arr,
							      fib6_work->nrt6);
		}
		mlxsw_sp_router_fib6_work_fini(fib6_work);
		break;
	case FIB_EVENT_ENTRY_DEL:
		mlxsw_sp_router_fib6_del(mlxsw_sp,
					 fib6_work->rt_arr,
					 fib6_work->nrt6);
		mlxsw_sp_router_fib6_work_fini(fib6_work);
		break;
	}
	mutex_unlock(&mlxsw_sp->router->lock);
	kfree(fib_work);
}

static void mlxsw_sp_router_fibmr_event_work(struct work_struct *work)
{
	struct mlxsw_sp_fib_event_work *fib_work =
		container_of(work, struct mlxsw_sp_fib_event_work, work);
	struct mlxsw_sp *mlxsw_sp = fib_work->mlxsw_sp;
	bool replace;
	int err;

	rtnl_lock();
	mutex_lock(&mlxsw_sp->router->lock);
	switch (fib_work->event) {
	case FIB_EVENT_ENTRY_REPLACE:
	case FIB_EVENT_ENTRY_ADD:
		replace = fib_work->event == FIB_EVENT_ENTRY_REPLACE;

		err = mlxsw_sp_router_fibmr_add(mlxsw_sp, &fib_work->men_info,
						replace);
		if (err)
			dev_warn(mlxsw_sp->bus_info->dev, "MR entry add failed.\n");
		mr_cache_put(fib_work->men_info.mfc);
		break;
	case FIB_EVENT_ENTRY_DEL:
		mlxsw_sp_router_fibmr_del(mlxsw_sp, &fib_work->men_info);
		mr_cache_put(fib_work->men_info.mfc);
		break;
	case FIB_EVENT_VIF_ADD:
		err = mlxsw_sp_router_fibmr_vif_add(mlxsw_sp,
						    &fib_work->ven_info);
		if (err)
			dev_warn(mlxsw_sp->bus_info->dev, "MR VIF add failed.\n");
		dev_put(fib_work->ven_info.dev);
		break;
	case FIB_EVENT_VIF_DEL:
		mlxsw_sp_router_fibmr_vif_del(mlxsw_sp,
					      &fib_work->ven_info);
		dev_put(fib_work->ven_info.dev);
		break;
	}
	mutex_unlock(&mlxsw_sp->router->lock);
	rtnl_unlock();
	kfree(fib_work);
}

static void mlxsw_sp_router_fib4_event(struct mlxsw_sp_fib_event_work *fib_work,
				       struct fib_notifier_info *info)
{
	struct fib_entry_notifier_info *fen_info;
	struct fib_nh_notifier_info *fnh_info;

	switch (fib_work->event) {
	case FIB_EVENT_ENTRY_REPLACE:
	case FIB_EVENT_ENTRY_DEL:
		fen_info = container_of(info, struct fib_entry_notifier_info,
					info);
		fib_work->fen_info = *fen_info;
		/* Take reference on fib_info to prevent it from being
		 * freed while work is queued. Release it afterwards.
		 */
		fib_info_hold(fib_work->fen_info.fi);
		break;
	case FIB_EVENT_NH_ADD:
	case FIB_EVENT_NH_DEL:
		fnh_info = container_of(info, struct fib_nh_notifier_info,
					info);
		fib_work->fnh_info = *fnh_info;
		fib_info_hold(fib_work->fnh_info.fib_nh->nh_parent);
		break;
	}
}

static int mlxsw_sp_router_fib6_event(struct mlxsw_sp_fib_event_work *fib_work,
				      struct fib_notifier_info *info)
{
	struct fib6_entry_notifier_info *fen6_info;
	int err;

	switch (fib_work->event) {
	case FIB_EVENT_ENTRY_REPLACE:
	case FIB_EVENT_ENTRY_APPEND:
	case FIB_EVENT_ENTRY_DEL:
		fen6_info = container_of(info, struct fib6_entry_notifier_info,
					 info);
		err = mlxsw_sp_router_fib6_work_init(&fib_work->fib6_work,
						     fen6_info);
		if (err)
			return err;
		break;
	}

	return 0;
}

static void
mlxsw_sp_router_fibmr_event(struct mlxsw_sp_fib_event_work *fib_work,
			    struct fib_notifier_info *info)
{
	switch (fib_work->event) {
	case FIB_EVENT_ENTRY_REPLACE:
	case FIB_EVENT_ENTRY_ADD:
	case FIB_EVENT_ENTRY_DEL:
		memcpy(&fib_work->men_info, info, sizeof(fib_work->men_info));
		mr_cache_hold(fib_work->men_info.mfc);
		break;
	case FIB_EVENT_VIF_ADD:
	case FIB_EVENT_VIF_DEL:
		memcpy(&fib_work->ven_info, info, sizeof(fib_work->ven_info));
		dev_hold(fib_work->ven_info.dev);
		break;
	}
}

static int mlxsw_sp_router_fib_rule_event(unsigned long event,
					  struct fib_notifier_info *info,
					  struct mlxsw_sp *mlxsw_sp)
{
	struct netlink_ext_ack *extack = info->extack;
	struct fib_rule_notifier_info *fr_info;
	struct fib_rule *rule;
	int err = 0;

	/* nothing to do at the moment */
	if (event == FIB_EVENT_RULE_DEL)
		return 0;

	fr_info = container_of(info, struct fib_rule_notifier_info, info);
	rule = fr_info->rule;

	/* Rule only affects locally generated traffic */
	if (rule->iifindex == mlxsw_sp_net(mlxsw_sp)->loopback_dev->ifindex)
		return 0;

	switch (info->family) {
	case AF_INET:
		if (!fib4_rule_default(rule) && !rule->l3mdev)
			err = -EOPNOTSUPP;
		break;
	case AF_INET6:
		if (!fib6_rule_default(rule) && !rule->l3mdev)
			err = -EOPNOTSUPP;
		break;
	case RTNL_FAMILY_IPMR:
		if (!ipmr_rule_default(rule) && !rule->l3mdev)
			err = -EOPNOTSUPP;
		break;
	case RTNL_FAMILY_IP6MR:
		if (!ip6mr_rule_default(rule) && !rule->l3mdev)
			err = -EOPNOTSUPP;
		break;
	}

	if (err < 0)
		NL_SET_ERR_MSG_MOD(extack, "FIB rules not supported");

	return err;
}

/* Called with rcu_read_lock() */
static int mlxsw_sp_router_fib_event(struct notifier_block *nb,
				     unsigned long event, void *ptr)
{
	struct mlxsw_sp_fib_event_work *fib_work;
	struct fib_notifier_info *info = ptr;
	struct mlxsw_sp_router *router;
	int err;

	if ((info->family != AF_INET && info->family != AF_INET6 &&
	     info->family != RTNL_FAMILY_IPMR &&
	     info->family != RTNL_FAMILY_IP6MR))
		return NOTIFY_DONE;

	router = container_of(nb, struct mlxsw_sp_router, fib_nb);

	switch (event) {
	case FIB_EVENT_RULE_ADD:
	case FIB_EVENT_RULE_DEL:
		err = mlxsw_sp_router_fib_rule_event(event, info,
						     router->mlxsw_sp);
		return notifier_from_errno(err);
	case FIB_EVENT_ENTRY_ADD:
	case FIB_EVENT_ENTRY_REPLACE:
	case FIB_EVENT_ENTRY_APPEND:
		if (info->family == AF_INET) {
			struct fib_entry_notifier_info *fen_info = ptr;

			if (fen_info->fi->fib_nh_is_v6) {
				NL_SET_ERR_MSG_MOD(info->extack, "IPv6 gateway with IPv4 route is not supported");
				return notifier_from_errno(-EINVAL);
			}
		}
		break;
	}

	fib_work = kzalloc(sizeof(*fib_work), GFP_ATOMIC);
	if (!fib_work)
		return NOTIFY_BAD;

	fib_work->mlxsw_sp = router->mlxsw_sp;
	fib_work->event = event;

	switch (info->family) {
	case AF_INET:
		INIT_WORK(&fib_work->work, mlxsw_sp_router_fib4_event_work);
		mlxsw_sp_router_fib4_event(fib_work, info);
		break;
	case AF_INET6:
		INIT_WORK(&fib_work->work, mlxsw_sp_router_fib6_event_work);
		err = mlxsw_sp_router_fib6_event(fib_work, info);
		if (err)
			goto err_fib_event;
		break;
	case RTNL_FAMILY_IP6MR:
	case RTNL_FAMILY_IPMR:
		INIT_WORK(&fib_work->work, mlxsw_sp_router_fibmr_event_work);
		mlxsw_sp_router_fibmr_event(fib_work, info);
		break;
	}

	mlxsw_core_schedule_work(&fib_work->work);

	return NOTIFY_DONE;

err_fib_event:
	kfree(fib_work);
	return NOTIFY_BAD;
}

static struct mlxsw_sp_rif *
mlxsw_sp_rif_find_by_dev(const struct mlxsw_sp *mlxsw_sp,
			 const struct net_device *dev)
{
	int max_rifs = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS);
	int i;

	for (i = 0; i < max_rifs; i++)
		if (mlxsw_sp->router->rifs[i] &&
		    mlxsw_sp_rif_dev_is(mlxsw_sp->router->rifs[i], dev))
			return mlxsw_sp->router->rifs[i];

	return NULL;
}

bool mlxsw_sp_rif_exists(struct mlxsw_sp *mlxsw_sp,
			 const struct net_device *dev)
{
	struct mlxsw_sp_rif *rif;

	mutex_lock(&mlxsw_sp->router->lock);
	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, dev);
	mutex_unlock(&mlxsw_sp->router->lock);

	return rif;
}

u16 mlxsw_sp_rif_vid(struct mlxsw_sp *mlxsw_sp, const struct net_device *dev)
{
	struct mlxsw_sp_rif *rif;
	u16 vid = 0;

	mutex_lock(&mlxsw_sp->router->lock);
	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, dev);
	if (!rif)
		goto out;

	/* We only return the VID for VLAN RIFs. Otherwise we return an
	 * invalid value (0).
	 */
	if (rif->ops->type != MLXSW_SP_RIF_TYPE_VLAN)
		goto out;

	vid = mlxsw_sp_fid_8021q_vid(rif->fid);

out:
	mutex_unlock(&mlxsw_sp->router->lock);
	return vid;
}

static int mlxsw_sp_router_rif_disable(struct mlxsw_sp *mlxsw_sp, u16 rif)
{
	char ritr_pl[MLXSW_REG_RITR_LEN];
	int err;

	mlxsw_reg_ritr_rif_pack(ritr_pl, rif);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
	if (err)
		return err;

	mlxsw_reg_ritr_enable_set(ritr_pl, false);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
}

static void mlxsw_sp_router_rif_gone_sync(struct mlxsw_sp *mlxsw_sp,
					  struct mlxsw_sp_rif *rif)
{
	/* Signal to nexthop cleanup that the RIF is going away. */
	rif->crif->rif = NULL;

	mlxsw_sp_router_rif_disable(mlxsw_sp, rif->rif_index);
	mlxsw_sp_nexthop_rif_gone_sync(mlxsw_sp, rif);
	mlxsw_sp_neigh_rif_gone_sync(mlxsw_sp, rif);
}

static bool __mlxsw_sp_dev_addr_list_empty(const struct net_device *dev)
{
	struct inet6_dev *inet6_dev;
	struct in_device *idev;

	idev = __in_dev_get_rcu(dev);
	if (idev && idev->ifa_list)
		return false;

	inet6_dev = __in6_dev_get(dev);
	if (inet6_dev && !list_empty(&inet6_dev->addr_list))
		return false;

	return true;
}

static bool mlxsw_sp_dev_addr_list_empty(const struct net_device *dev)
{
	bool addr_list_empty;

	rcu_read_lock();
	addr_list_empty = __mlxsw_sp_dev_addr_list_empty(dev);
	rcu_read_unlock();

	return addr_list_empty;
}

static bool
mlxsw_sp_rif_should_config(struct mlxsw_sp_rif *rif, struct net_device *dev,
			   unsigned long event)
{
	bool addr_list_empty;

	switch (event) {
	case NETDEV_UP:
		return rif == NULL;
	case NETDEV_DOWN:
		addr_list_empty = mlxsw_sp_dev_addr_list_empty(dev);

		/* macvlans do not have a RIF, but rather piggy back on the
		 * RIF of their lower device.
		 */
		if (netif_is_macvlan(dev) && addr_list_empty)
			return true;

		if (rif && addr_list_empty &&
		    !netif_is_l3_slave(mlxsw_sp_rif_dev(rif)))
			return true;
		/* It is possible we already removed the RIF ourselves
		 * if it was assigned to a netdev that is now a bridge
		 * or LAG slave.
		 */
		return false;
	}

	return false;
}

static enum mlxsw_sp_rif_type
mlxsw_sp_dev_rif_type(const struct mlxsw_sp *mlxsw_sp,
		      const struct net_device *dev)
{
	enum mlxsw_sp_fid_type type;

	if (mlxsw_sp_netdev_ipip_type(mlxsw_sp, dev, NULL))
		return MLXSW_SP_RIF_TYPE_IPIP_LB;

	/* Otherwise RIF type is derived from the type of the underlying FID. */
	if (is_vlan_dev(dev) && netif_is_bridge_master(vlan_dev_real_dev(dev)))
		type = MLXSW_SP_FID_TYPE_8021Q;
	else if (netif_is_bridge_master(dev) && br_vlan_enabled(dev))
		type = MLXSW_SP_FID_TYPE_8021Q;
	else if (netif_is_bridge_master(dev))
		type = MLXSW_SP_FID_TYPE_8021D;
	else
		type = MLXSW_SP_FID_TYPE_RFID;

	return mlxsw_sp_fid_type_rif_type(mlxsw_sp, type);
}

static int mlxsw_sp_rif_index_alloc(struct mlxsw_sp *mlxsw_sp, u16 *p_rif_index,
				    u8 rif_entries)
{
	*p_rif_index = gen_pool_alloc(mlxsw_sp->router->rifs_table,
				      rif_entries);
	if (*p_rif_index == 0)
		return -ENOBUFS;
	*p_rif_index -= MLXSW_SP_ROUTER_GENALLOC_OFFSET;

	/* RIF indexes must be aligned to the allocation size. */
	WARN_ON_ONCE(*p_rif_index % rif_entries);

	return 0;
}

static void mlxsw_sp_rif_index_free(struct mlxsw_sp *mlxsw_sp, u16 rif_index,
				    u8 rif_entries)
{
	gen_pool_free(mlxsw_sp->router->rifs_table,
		      MLXSW_SP_ROUTER_GENALLOC_OFFSET + rif_index, rif_entries);
}

static struct mlxsw_sp_rif *mlxsw_sp_rif_alloc(size_t rif_size, u16 rif_index,
					       u16 vr_id,
					       struct mlxsw_sp_crif *crif)
{
	struct net_device *l3_dev = crif ? crif->key.dev : NULL;
	struct mlxsw_sp_rif *rif;

	rif = kzalloc(rif_size, GFP_KERNEL);
	if (!rif)
		return NULL;

	INIT_LIST_HEAD(&rif->neigh_list);
	if (l3_dev) {
		ether_addr_copy(rif->addr, l3_dev->dev_addr);
		rif->mtu = l3_dev->mtu;
	}
	rif->vr_id = vr_id;
	rif->rif_index = rif_index;
	if (crif) {
		rif->crif = crif;
		crif->rif = rif;
	}

	return rif;
}

static void mlxsw_sp_rif_free(struct mlxsw_sp_rif *rif)
{
	WARN_ON(!list_empty(&rif->neigh_list));

	if (rif->crif)
		rif->crif->rif = NULL;
	kfree(rif);
}

struct mlxsw_sp_rif *mlxsw_sp_rif_by_index(const struct mlxsw_sp *mlxsw_sp,
					   u16 rif_index)
{
	return mlxsw_sp->router->rifs[rif_index];
}

u16 mlxsw_sp_rif_index(const struct mlxsw_sp_rif *rif)
{
	return rif->rif_index;
}

u16 mlxsw_sp_ipip_lb_rif_index(const struct mlxsw_sp_rif_ipip_lb *lb_rif)
{
	return lb_rif->common.rif_index;
}

u16 mlxsw_sp_ipip_lb_ul_vr_id(const struct mlxsw_sp_rif_ipip_lb *lb_rif)
{
	struct net_device *dev = mlxsw_sp_rif_dev(&lb_rif->common);
	u32 ul_tb_id = mlxsw_sp_ipip_dev_ul_tb_id(dev);
	struct mlxsw_sp_vr *ul_vr;

	ul_vr = mlxsw_sp_vr_get(lb_rif->common.mlxsw_sp, ul_tb_id, NULL);
	if (WARN_ON(IS_ERR(ul_vr)))
		return 0;

	return ul_vr->id;
}

u16 mlxsw_sp_ipip_lb_ul_rif_id(const struct mlxsw_sp_rif_ipip_lb *lb_rif)
{
	return lb_rif->ul_rif_id;
}

static bool
mlxsw_sp_router_port_l3_stats_enabled(struct mlxsw_sp_rif *rif)
{
	return mlxsw_sp_rif_counter_valid_get(rif,
					      MLXSW_SP_RIF_COUNTER_EGRESS) &&
	       mlxsw_sp_rif_counter_valid_get(rif,
					      MLXSW_SP_RIF_COUNTER_INGRESS);
}

static int
mlxsw_sp_router_port_l3_stats_enable(struct mlxsw_sp_rif *rif)
{
	int err;

	err = mlxsw_sp_rif_counter_alloc(rif, MLXSW_SP_RIF_COUNTER_INGRESS);
	if (err)
		return err;

	/* Clear stale data. */
	err = mlxsw_sp_rif_counter_fetch_clear(rif,
					       MLXSW_SP_RIF_COUNTER_INGRESS,
					       NULL);
	if (err)
		goto err_clear_ingress;

	err = mlxsw_sp_rif_counter_alloc(rif, MLXSW_SP_RIF_COUNTER_EGRESS);
	if (err)
		goto err_alloc_egress;

	/* Clear stale data. */
	err = mlxsw_sp_rif_counter_fetch_clear(rif,
					       MLXSW_SP_RIF_COUNTER_EGRESS,
					       NULL);
	if (err)
		goto err_clear_egress;

	return 0;

err_clear_egress:
	mlxsw_sp_rif_counter_free(rif, MLXSW_SP_RIF_COUNTER_EGRESS);
err_alloc_egress:
err_clear_ingress:
	mlxsw_sp_rif_counter_free(rif, MLXSW_SP_RIF_COUNTER_INGRESS);
	return err;
}

static void
mlxsw_sp_router_port_l3_stats_disable(struct mlxsw_sp_rif *rif)
{
	mlxsw_sp_rif_counter_free(rif, MLXSW_SP_RIF_COUNTER_EGRESS);
	mlxsw_sp_rif_counter_free(rif, MLXSW_SP_RIF_COUNTER_INGRESS);
}

static void
mlxsw_sp_router_port_l3_stats_report_used(struct mlxsw_sp_rif *rif,
					  struct netdev_notifier_offload_xstats_info *info)
{
	if (!mlxsw_sp_router_port_l3_stats_enabled(rif))
		return;
	netdev_offload_xstats_report_used(info->report_used);
}

static int
mlxsw_sp_router_port_l3_stats_fetch(struct mlxsw_sp_rif *rif,
				    struct rtnl_hw_stats64 *p_stats)
{
	struct mlxsw_sp_rif_counter_set_basic ingress;
	struct mlxsw_sp_rif_counter_set_basic egress;
	int err;

	err = mlxsw_sp_rif_counter_fetch_clear(rif,
					       MLXSW_SP_RIF_COUNTER_INGRESS,
					       &ingress);
	if (err)
		return err;

	err = mlxsw_sp_rif_counter_fetch_clear(rif,
					       MLXSW_SP_RIF_COUNTER_EGRESS,
					       &egress);
	if (err)
		return err;

#define MLXSW_SP_ROUTER_ALL_GOOD(SET, SFX)		\
		((SET.good_unicast_ ## SFX) +		\
		 (SET.good_multicast_ ## SFX) +		\
		 (SET.good_broadcast_ ## SFX))

	p_stats->rx_packets = MLXSW_SP_ROUTER_ALL_GOOD(ingress, packets);
	p_stats->tx_packets = MLXSW_SP_ROUTER_ALL_GOOD(egress, packets);
	p_stats->rx_bytes = MLXSW_SP_ROUTER_ALL_GOOD(ingress, bytes);
	p_stats->tx_bytes = MLXSW_SP_ROUTER_ALL_GOOD(egress, bytes);
	p_stats->rx_errors = ingress.error_packets;
	p_stats->tx_errors = egress.error_packets;
	p_stats->rx_dropped = ingress.discard_packets;
	p_stats->tx_dropped = egress.discard_packets;
	p_stats->multicast = ingress.good_multicast_packets +
			     ingress.good_broadcast_packets;

#undef MLXSW_SP_ROUTER_ALL_GOOD

	return 0;
}

static int
mlxsw_sp_router_port_l3_stats_report_delta(struct mlxsw_sp_rif *rif,
					   struct netdev_notifier_offload_xstats_info *info)
{
	struct rtnl_hw_stats64 stats = {};
	int err;

	if (!mlxsw_sp_router_port_l3_stats_enabled(rif))
		return 0;

	err = mlxsw_sp_router_port_l3_stats_fetch(rif, &stats);
	if (err)
		return err;

	netdev_offload_xstats_report_delta(info->report_delta, &stats);
	return 0;
}

struct mlxsw_sp_router_hwstats_notify_work {
	struct work_struct work;
	struct net_device *dev;
};

static void mlxsw_sp_router_hwstats_notify_work(struct work_struct *work)
{
	struct mlxsw_sp_router_hwstats_notify_work *hws_work =
		container_of(work, struct mlxsw_sp_router_hwstats_notify_work,
			     work);

	rtnl_lock();
	rtnl_offload_xstats_notify(hws_work->dev);
	rtnl_unlock();
	dev_put(hws_work->dev);
	kfree(hws_work);
}

static void
mlxsw_sp_router_hwstats_notify_schedule(struct net_device *dev)
{
	struct mlxsw_sp_router_hwstats_notify_work *hws_work;

	/* To collect notification payload, the core ends up sending another
	 * notifier block message, which would deadlock on the attempt to
	 * acquire the router lock again. Just postpone the notification until
	 * later.
	 */

	hws_work = kzalloc(sizeof(*hws_work), GFP_KERNEL);
	if (!hws_work)
		return;

	INIT_WORK(&hws_work->work, mlxsw_sp_router_hwstats_notify_work);
	dev_hold(dev);
	hws_work->dev = dev;
	mlxsw_core_schedule_work(&hws_work->work);
}

int mlxsw_sp_rif_dev_ifindex(const struct mlxsw_sp_rif *rif)
{
	return mlxsw_sp_rif_dev(rif)->ifindex;
}

bool mlxsw_sp_rif_has_dev(const struct mlxsw_sp_rif *rif)
{
	return !!mlxsw_sp_rif_dev(rif);
}

bool mlxsw_sp_rif_dev_is(const struct mlxsw_sp_rif *rif,
			 const struct net_device *dev)
{
	return mlxsw_sp_rif_dev(rif) == dev;
}

static void mlxsw_sp_rif_push_l3_stats(struct mlxsw_sp_rif *rif)
{
	struct rtnl_hw_stats64 stats = {};

	if (!mlxsw_sp_router_port_l3_stats_fetch(rif, &stats))
		netdev_offload_xstats_push_delta(mlxsw_sp_rif_dev(rif),
						 NETDEV_OFFLOAD_XSTATS_TYPE_L3,
						 &stats);
}

static struct mlxsw_sp_rif *
mlxsw_sp_rif_create(struct mlxsw_sp *mlxsw_sp,
		    const struct mlxsw_sp_rif_params *params,
		    struct netlink_ext_ack *extack)
{
	u8 rif_entries = params->double_entry ? 2 : 1;
	u32 tb_id = l3mdev_fib_table(params->dev);
	const struct mlxsw_sp_rif_ops *ops;
	struct mlxsw_sp_fid *fid = NULL;
	enum mlxsw_sp_rif_type type;
	struct mlxsw_sp_crif *crif;
	struct mlxsw_sp_rif *rif;
	struct mlxsw_sp_vr *vr;
	u16 rif_index;
	int i, err;

	type = mlxsw_sp_dev_rif_type(mlxsw_sp, params->dev);
	ops = mlxsw_sp->router->rif_ops_arr[type];

	vr = mlxsw_sp_vr_get(mlxsw_sp, tb_id ? : RT_TABLE_MAIN, extack);
	if (IS_ERR(vr))
		return ERR_CAST(vr);
	vr->rif_count++;

	err = mlxsw_sp_rif_index_alloc(mlxsw_sp, &rif_index, rif_entries);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Exceeded number of supported router interfaces");
		goto err_rif_index_alloc;
	}

	crif = mlxsw_sp_crif_lookup(mlxsw_sp->router, params->dev);
	if (WARN_ON(!crif)) {
		err = -ENOENT;
		goto err_crif_lookup;
	}

	rif = mlxsw_sp_rif_alloc(ops->rif_size, rif_index, vr->id, crif);
	if (!rif) {
		err = -ENOMEM;
		goto err_rif_alloc;
	}
	dev_hold(params->dev);
	mlxsw_sp->router->rifs[rif_index] = rif;
	rif->mlxsw_sp = mlxsw_sp;
	rif->ops = ops;
	rif->rif_entries = rif_entries;

	if (ops->fid_get) {
		fid = ops->fid_get(rif, extack);
		if (IS_ERR(fid)) {
			err = PTR_ERR(fid);
			goto err_fid_get;
		}
		rif->fid = fid;
	}

	if (ops->setup)
		ops->setup(rif, params);

	err = ops->configure(rif, extack);
	if (err)
		goto err_configure;

	for (i = 0; i < MLXSW_SP_L3_PROTO_MAX; i++) {
		err = mlxsw_sp_mr_rif_add(vr->mr_table[i], rif);
		if (err)
			goto err_mr_rif_add;
	}

	if (netdev_offload_xstats_enabled(params->dev,
					  NETDEV_OFFLOAD_XSTATS_TYPE_L3)) {
		err = mlxsw_sp_router_port_l3_stats_enable(rif);
		if (err)
			goto err_stats_enable;
		mlxsw_sp_router_hwstats_notify_schedule(params->dev);
	} else {
		mlxsw_sp_rif_counters_alloc(rif);
	}

	atomic_add(rif_entries, &mlxsw_sp->router->rifs_count);
	return rif;

err_stats_enable:
err_mr_rif_add:
	for (i--; i >= 0; i--)
		mlxsw_sp_mr_rif_del(vr->mr_table[i], rif);
	ops->deconfigure(rif);
err_configure:
	if (fid)
		mlxsw_sp_fid_put(fid);
err_fid_get:
	mlxsw_sp->router->rifs[rif_index] = NULL;
	dev_put(params->dev);
	mlxsw_sp_rif_free(rif);
err_rif_alloc:
err_crif_lookup:
	mlxsw_sp_rif_index_free(mlxsw_sp, rif_index, rif_entries);
err_rif_index_alloc:
	vr->rif_count--;
	mlxsw_sp_vr_put(mlxsw_sp, vr);
	return ERR_PTR(err);
}

static void mlxsw_sp_rif_destroy(struct mlxsw_sp_rif *rif)
{
	struct net_device *dev = mlxsw_sp_rif_dev(rif);
	const struct mlxsw_sp_rif_ops *ops = rif->ops;
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	struct mlxsw_sp_crif *crif = rif->crif;
	struct mlxsw_sp_fid *fid = rif->fid;
	u8 rif_entries = rif->rif_entries;
	u16 rif_index = rif->rif_index;
	struct mlxsw_sp_vr *vr;
	int i;

	atomic_sub(rif_entries, &mlxsw_sp->router->rifs_count);
	mlxsw_sp_router_rif_gone_sync(mlxsw_sp, rif);
	vr = &mlxsw_sp->router->vrs[rif->vr_id];

	if (netdev_offload_xstats_enabled(dev, NETDEV_OFFLOAD_XSTATS_TYPE_L3)) {
		mlxsw_sp_rif_push_l3_stats(rif);
		mlxsw_sp_router_port_l3_stats_disable(rif);
		mlxsw_sp_router_hwstats_notify_schedule(dev);
	} else {
		mlxsw_sp_rif_counters_free(rif);
	}

	for (i = 0; i < MLXSW_SP_L3_PROTO_MAX; i++)
		mlxsw_sp_mr_rif_del(vr->mr_table[i], rif);
	ops->deconfigure(rif);
	if (fid)
		/* Loopback RIFs are not associated with a FID. */
		mlxsw_sp_fid_put(fid);
	mlxsw_sp->router->rifs[rif->rif_index] = NULL;
	dev_put(dev);
	mlxsw_sp_rif_free(rif);
	mlxsw_sp_rif_index_free(mlxsw_sp, rif_index, rif_entries);
	vr->rif_count--;
	mlxsw_sp_vr_put(mlxsw_sp, vr);

	if (crif->can_destroy)
		mlxsw_sp_crif_free(crif);
}

void mlxsw_sp_rif_destroy_by_dev(struct mlxsw_sp *mlxsw_sp,
				 struct net_device *dev)
{
	struct mlxsw_sp_rif *rif;

	mutex_lock(&mlxsw_sp->router->lock);
	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, dev);
	if (!rif)
		goto out;
	mlxsw_sp_rif_destroy(rif);
out:
	mutex_unlock(&mlxsw_sp->router->lock);
}

static void
mlxsw_sp_rif_subport_params_init(struct mlxsw_sp_rif_params *params,
				 struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan)
{
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp_port_vlan->mlxsw_sp_port;

	params->vid = mlxsw_sp_port_vlan->vid;
	params->lag = mlxsw_sp_port->lagged;
	if (params->lag)
		params->lag_id = mlxsw_sp_port->lag_id;
	else
		params->system_port = mlxsw_sp_port->local_port;
}

static struct mlxsw_sp_rif_subport *
mlxsw_sp_rif_subport_rif(const struct mlxsw_sp_rif *rif)
{
	return container_of(rif, struct mlxsw_sp_rif_subport, common);
}

static struct mlxsw_sp_rif *
mlxsw_sp_rif_subport_get(struct mlxsw_sp *mlxsw_sp,
			 const struct mlxsw_sp_rif_params *params,
			 struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_rif_subport *rif_subport;
	struct mlxsw_sp_rif *rif;

	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, params->dev);
	if (!rif)
		return mlxsw_sp_rif_create(mlxsw_sp, params, extack);

	rif_subport = mlxsw_sp_rif_subport_rif(rif);
	refcount_inc(&rif_subport->ref_count);
	return rif;
}

static void mlxsw_sp_rif_subport_put(struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp_rif_subport *rif_subport;

	rif_subport = mlxsw_sp_rif_subport_rif(rif);
	if (!refcount_dec_and_test(&rif_subport->ref_count))
		return;

	mlxsw_sp_rif_destroy(rif);
}

static int mlxsw_sp_rif_mac_profile_index_alloc(struct mlxsw_sp *mlxsw_sp,
						struct mlxsw_sp_rif_mac_profile *profile,
						struct netlink_ext_ack *extack)
{
	u8 max_rif_mac_profiles = mlxsw_sp->router->max_rif_mac_profile;
	struct mlxsw_sp_router *router = mlxsw_sp->router;
	int id;

	id = idr_alloc(&router->rif_mac_profiles_idr, profile, 0,
		       max_rif_mac_profiles, GFP_KERNEL);

	if (id >= 0) {
		profile->id = id;
		return 0;
	}

	if (id == -ENOSPC)
		NL_SET_ERR_MSG_MOD(extack,
				   "Exceeded number of supported router interface MAC profiles");

	return id;
}

static struct mlxsw_sp_rif_mac_profile *
mlxsw_sp_rif_mac_profile_index_free(struct mlxsw_sp *mlxsw_sp, u8 mac_profile)
{
	struct mlxsw_sp_rif_mac_profile *profile;

	profile = idr_remove(&mlxsw_sp->router->rif_mac_profiles_idr,
			     mac_profile);
	WARN_ON(!profile);
	return profile;
}

static struct mlxsw_sp_rif_mac_profile *
mlxsw_sp_rif_mac_profile_alloc(const char *mac)
{
	struct mlxsw_sp_rif_mac_profile *profile;

	profile = kzalloc(sizeof(*profile), GFP_KERNEL);
	if (!profile)
		return NULL;

	ether_addr_copy(profile->mac_prefix, mac);
	refcount_set(&profile->ref_count, 1);
	return profile;
}

static struct mlxsw_sp_rif_mac_profile *
mlxsw_sp_rif_mac_profile_find(const struct mlxsw_sp *mlxsw_sp, const char *mac)
{
	struct mlxsw_sp_router *router = mlxsw_sp->router;
	struct mlxsw_sp_rif_mac_profile *profile;
	int id;

	idr_for_each_entry(&router->rif_mac_profiles_idr, profile, id) {
		if (ether_addr_equal_masked(profile->mac_prefix, mac,
					    mlxsw_sp->mac_mask))
			return profile;
	}

	return NULL;
}

static u64 mlxsw_sp_rif_mac_profiles_occ_get(void *priv)
{
	const struct mlxsw_sp *mlxsw_sp = priv;

	return atomic_read(&mlxsw_sp->router->rif_mac_profiles_count);
}

static u64 mlxsw_sp_rifs_occ_get(void *priv)
{
	const struct mlxsw_sp *mlxsw_sp = priv;

	return atomic_read(&mlxsw_sp->router->rifs_count);
}

static struct mlxsw_sp_rif_mac_profile *
mlxsw_sp_rif_mac_profile_create(struct mlxsw_sp *mlxsw_sp, const char *mac,
				struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_rif_mac_profile *profile;
	int err;

	profile = mlxsw_sp_rif_mac_profile_alloc(mac);
	if (!profile)
		return ERR_PTR(-ENOMEM);

	err = mlxsw_sp_rif_mac_profile_index_alloc(mlxsw_sp, profile, extack);
	if (err)
		goto profile_index_alloc_err;

	atomic_inc(&mlxsw_sp->router->rif_mac_profiles_count);
	return profile;

profile_index_alloc_err:
	kfree(profile);
	return ERR_PTR(err);
}

static void mlxsw_sp_rif_mac_profile_destroy(struct mlxsw_sp *mlxsw_sp,
					     u8 mac_profile)
{
	struct mlxsw_sp_rif_mac_profile *profile;

	atomic_dec(&mlxsw_sp->router->rif_mac_profiles_count);
	profile = mlxsw_sp_rif_mac_profile_index_free(mlxsw_sp, mac_profile);
	kfree(profile);
}

static int mlxsw_sp_rif_mac_profile_get(struct mlxsw_sp *mlxsw_sp,
					const char *mac, u8 *p_mac_profile,
					struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_rif_mac_profile *profile;

	profile = mlxsw_sp_rif_mac_profile_find(mlxsw_sp, mac);
	if (profile) {
		refcount_inc(&profile->ref_count);
		goto out;
	}

	profile = mlxsw_sp_rif_mac_profile_create(mlxsw_sp, mac, extack);
	if (IS_ERR(profile))
		return PTR_ERR(profile);

out:
	*p_mac_profile = profile->id;
	return 0;
}

static void mlxsw_sp_rif_mac_profile_put(struct mlxsw_sp *mlxsw_sp,
					 u8 mac_profile)
{
	struct mlxsw_sp_rif_mac_profile *profile;

	profile = idr_find(&mlxsw_sp->router->rif_mac_profiles_idr,
			   mac_profile);
	if (WARN_ON(!profile))
		return;

	if (!refcount_dec_and_test(&profile->ref_count))
		return;

	mlxsw_sp_rif_mac_profile_destroy(mlxsw_sp, mac_profile);
}

static bool mlxsw_sp_rif_mac_profile_is_shared(const struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	struct mlxsw_sp_rif_mac_profile *profile;

	profile = idr_find(&mlxsw_sp->router->rif_mac_profiles_idr,
			   rif->mac_profile_id);
	if (WARN_ON(!profile))
		return false;

	return refcount_read(&profile->ref_count) > 1;
}

static int mlxsw_sp_rif_mac_profile_edit(struct mlxsw_sp_rif *rif,
					 const char *new_mac)
{
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	struct mlxsw_sp_rif_mac_profile *profile;

	profile = idr_find(&mlxsw_sp->router->rif_mac_profiles_idr,
			   rif->mac_profile_id);
	if (WARN_ON(!profile))
		return -EINVAL;

	ether_addr_copy(profile->mac_prefix, new_mac);
	return 0;
}

static int
mlxsw_sp_rif_mac_profile_replace(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_rif *rif,
				 const char *new_mac,
				 struct netlink_ext_ack *extack)
{
	u8 mac_profile;
	int err;

	if (!mlxsw_sp_rif_mac_profile_is_shared(rif) &&
	    !mlxsw_sp_rif_mac_profile_find(mlxsw_sp, new_mac))
		return mlxsw_sp_rif_mac_profile_edit(rif, new_mac);

	err = mlxsw_sp_rif_mac_profile_get(mlxsw_sp, new_mac,
					   &mac_profile, extack);
	if (err)
		return err;

	mlxsw_sp_rif_mac_profile_put(mlxsw_sp, rif->mac_profile_id);
	rif->mac_profile_id = mac_profile;
	return 0;
}

static int
__mlxsw_sp_port_vlan_router_join(struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan,
				 struct net_device *l3_dev,
				 struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp_port_vlan->mlxsw_sp_port;
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_rif_params params = {
		.dev = l3_dev,
	};
	u16 vid = mlxsw_sp_port_vlan->vid;
	struct mlxsw_sp_rif *rif;
	struct mlxsw_sp_fid *fid;
	int err;

	mlxsw_sp_rif_subport_params_init(&params, mlxsw_sp_port_vlan);
	rif = mlxsw_sp_rif_subport_get(mlxsw_sp, &params, extack);
	if (IS_ERR(rif))
		return PTR_ERR(rif);

	/* FID was already created, just take a reference */
	fid = rif->ops->fid_get(rif, extack);
	err = mlxsw_sp_fid_port_vid_map(fid, mlxsw_sp_port, vid);
	if (err)
		goto err_fid_port_vid_map;

	err = mlxsw_sp_port_vid_learning_set(mlxsw_sp_port, vid, false);
	if (err)
		goto err_port_vid_learning_set;

	err = mlxsw_sp_port_vid_stp_set(mlxsw_sp_port, vid,
					BR_STATE_FORWARDING);
	if (err)
		goto err_port_vid_stp_set;

	mlxsw_sp_port_vlan->fid = fid;

	return 0;

err_port_vid_stp_set:
	mlxsw_sp_port_vid_learning_set(mlxsw_sp_port, vid, true);
err_port_vid_learning_set:
	mlxsw_sp_fid_port_vid_unmap(fid, mlxsw_sp_port, vid);
err_fid_port_vid_map:
	mlxsw_sp_fid_put(fid);
	mlxsw_sp_rif_subport_put(rif);
	return err;
}

static void
__mlxsw_sp_port_vlan_router_leave(struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan)
{
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp_port_vlan->mlxsw_sp_port;
	struct mlxsw_sp_fid *fid = mlxsw_sp_port_vlan->fid;
	struct mlxsw_sp_rif *rif = mlxsw_sp_fid_rif(fid);
	u16 vid = mlxsw_sp_port_vlan->vid;

	if (WARN_ON(mlxsw_sp_fid_type(fid) != MLXSW_SP_FID_TYPE_RFID))
		return;

	mlxsw_sp_port_vlan->fid = NULL;
	mlxsw_sp_port_vid_stp_set(mlxsw_sp_port, vid, BR_STATE_BLOCKING);
	mlxsw_sp_port_vid_learning_set(mlxsw_sp_port, vid, true);
	mlxsw_sp_fid_port_vid_unmap(fid, mlxsw_sp_port, vid);
	mlxsw_sp_fid_put(fid);
	mlxsw_sp_rif_subport_put(rif);
}

static int
mlxsw_sp_port_vlan_router_join_existing(struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan,
					struct net_device *l3_dev,
					struct netlink_ext_ack *extack)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port_vlan->mlxsw_sp_port->mlxsw_sp;

	lockdep_assert_held(&mlxsw_sp->router->lock);

	if (!mlxsw_sp_rif_find_by_dev(mlxsw_sp, l3_dev))
		return 0;

	return __mlxsw_sp_port_vlan_router_join(mlxsw_sp_port_vlan, l3_dev,
						extack);
}

void
mlxsw_sp_port_vlan_router_leave(struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port_vlan->mlxsw_sp_port->mlxsw_sp;

	mutex_lock(&mlxsw_sp->router->lock);
	__mlxsw_sp_port_vlan_router_leave(mlxsw_sp_port_vlan);
	mutex_unlock(&mlxsw_sp->router->lock);
}

static int mlxsw_sp_inetaddr_port_vlan_event(struct net_device *l3_dev,
					     struct net_device *port_dev,
					     unsigned long event, u16 vid,
					     struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(port_dev);
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;

	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_find_by_vid(mlxsw_sp_port, vid);
	if (WARN_ON(!mlxsw_sp_port_vlan))
		return -EINVAL;

	switch (event) {
	case NETDEV_UP:
		return __mlxsw_sp_port_vlan_router_join(mlxsw_sp_port_vlan,
							l3_dev, extack);
	case NETDEV_DOWN:
		__mlxsw_sp_port_vlan_router_leave(mlxsw_sp_port_vlan);
		break;
	}

	return 0;
}

static int mlxsw_sp_inetaddr_port_event(struct net_device *port_dev,
					unsigned long event,
					struct netlink_ext_ack *extack)
{
	if (netif_is_any_bridge_port(port_dev) || netif_is_lag_port(port_dev))
		return 0;

	return mlxsw_sp_inetaddr_port_vlan_event(port_dev, port_dev, event,
						 MLXSW_SP_DEFAULT_VID, extack);
}

static int __mlxsw_sp_inetaddr_lag_event(struct net_device *l3_dev,
					 struct net_device *lag_dev,
					 unsigned long event, u16 vid,
					 struct netlink_ext_ack *extack)
{
	struct net_device *port_dev;
	struct list_head *iter;
	int err;

	netdev_for_each_lower_dev(lag_dev, port_dev, iter) {
		if (mlxsw_sp_port_dev_check(port_dev)) {
			err = mlxsw_sp_inetaddr_port_vlan_event(l3_dev,
								port_dev,
								event, vid,
								extack);
			if (err)
				return err;
		}
	}

	return 0;
}

static int mlxsw_sp_inetaddr_lag_event(struct net_device *lag_dev,
				       unsigned long event,
				       struct netlink_ext_ack *extack)
{
	if (netif_is_bridge_port(lag_dev))
		return 0;

	return __mlxsw_sp_inetaddr_lag_event(lag_dev, lag_dev, event,
					     MLXSW_SP_DEFAULT_VID, extack);
}

static int mlxsw_sp_inetaddr_bridge_event(struct mlxsw_sp *mlxsw_sp,
					  struct net_device *l3_dev,
					  unsigned long event,
					  struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_rif_params params = {
		.dev = l3_dev,
	};
	struct mlxsw_sp_rif *rif;

	switch (event) {
	case NETDEV_UP:
		if (netif_is_bridge_master(l3_dev) && br_vlan_enabled(l3_dev)) {
			u16 proto;

			br_vlan_get_proto(l3_dev, &proto);
			if (proto == ETH_P_8021AD) {
				NL_SET_ERR_MSG_MOD(extack, "Adding an IP address to 802.1ad bridge is not supported");
				return -EOPNOTSUPP;
			}
		}
		rif = mlxsw_sp_rif_create(mlxsw_sp, &params, extack);
		if (IS_ERR(rif))
			return PTR_ERR(rif);
		break;
	case NETDEV_DOWN:
		rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, l3_dev);
		mlxsw_sp_rif_destroy(rif);
		break;
	}

	return 0;
}

static int mlxsw_sp_inetaddr_vlan_event(struct mlxsw_sp *mlxsw_sp,
					struct net_device *vlan_dev,
					unsigned long event,
					struct netlink_ext_ack *extack)
{
	struct net_device *real_dev = vlan_dev_real_dev(vlan_dev);
	u16 vid = vlan_dev_vlan_id(vlan_dev);

	if (netif_is_bridge_port(vlan_dev))
		return 0;

	if (mlxsw_sp_port_dev_check(real_dev))
		return mlxsw_sp_inetaddr_port_vlan_event(vlan_dev, real_dev,
							 event, vid, extack);
	else if (netif_is_lag_master(real_dev))
		return __mlxsw_sp_inetaddr_lag_event(vlan_dev, real_dev, event,
						     vid, extack);
	else if (netif_is_bridge_master(real_dev) && br_vlan_enabled(real_dev))
		return mlxsw_sp_inetaddr_bridge_event(mlxsw_sp, vlan_dev, event,
						      extack);

	return 0;
}

static bool mlxsw_sp_rif_macvlan_is_vrrp4(const u8 *mac)
{
	u8 vrrp4[ETH_ALEN] = { 0x00, 0x00, 0x5e, 0x00, 0x01, 0x00 };
	u8 mask[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };

	return ether_addr_equal_masked(mac, vrrp4, mask);
}

static bool mlxsw_sp_rif_macvlan_is_vrrp6(const u8 *mac)
{
	u8 vrrp6[ETH_ALEN] = { 0x00, 0x00, 0x5e, 0x00, 0x02, 0x00 };
	u8 mask[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };

	return ether_addr_equal_masked(mac, vrrp6, mask);
}

static int mlxsw_sp_rif_vrrp_op(struct mlxsw_sp *mlxsw_sp, u16 rif_index,
				const u8 *mac, bool adding)
{
	char ritr_pl[MLXSW_REG_RITR_LEN];
	u8 vrrp_id = adding ? mac[5] : 0;
	int err;

	if (!mlxsw_sp_rif_macvlan_is_vrrp4(mac) &&
	    !mlxsw_sp_rif_macvlan_is_vrrp6(mac))
		return 0;

	mlxsw_reg_ritr_rif_pack(ritr_pl, rif_index);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
	if (err)
		return err;

	if (mlxsw_sp_rif_macvlan_is_vrrp4(mac))
		mlxsw_reg_ritr_if_vrrp_id_ipv4_set(ritr_pl, vrrp_id);
	else
		mlxsw_reg_ritr_if_vrrp_id_ipv6_set(ritr_pl, vrrp_id);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
}

static int mlxsw_sp_rif_macvlan_add(struct mlxsw_sp *mlxsw_sp,
				    const struct net_device *macvlan_dev,
				    struct netlink_ext_ack *extack)
{
	struct macvlan_dev *vlan = netdev_priv(macvlan_dev);
	struct mlxsw_sp_rif *rif;
	int err;

	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, vlan->lowerdev);
	if (!rif) {
		NL_SET_ERR_MSG_MOD(extack, "macvlan is only supported on top of router interfaces");
		return -EOPNOTSUPP;
	}

	err = mlxsw_sp_rif_fdb_op(mlxsw_sp, macvlan_dev->dev_addr,
				  mlxsw_sp_fid_index(rif->fid), true);
	if (err)
		return err;

	err = mlxsw_sp_rif_vrrp_op(mlxsw_sp, rif->rif_index,
				   macvlan_dev->dev_addr, true);
	if (err)
		goto err_rif_vrrp_add;

	/* Make sure the bridge driver does not have this MAC pointing at
	 * some other port.
	 */
	if (rif->ops->fdb_del)
		rif->ops->fdb_del(rif, macvlan_dev->dev_addr);

	return 0;

err_rif_vrrp_add:
	mlxsw_sp_rif_fdb_op(mlxsw_sp, macvlan_dev->dev_addr,
			    mlxsw_sp_fid_index(rif->fid), false);
	return err;
}

static void __mlxsw_sp_rif_macvlan_del(struct mlxsw_sp *mlxsw_sp,
				       const struct net_device *macvlan_dev)
{
	struct macvlan_dev *vlan = netdev_priv(macvlan_dev);
	struct mlxsw_sp_rif *rif;

	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, vlan->lowerdev);
	/* If we do not have a RIF, then we already took care of
	 * removing the macvlan's MAC during RIF deletion.
	 */
	if (!rif)
		return;
	mlxsw_sp_rif_vrrp_op(mlxsw_sp, rif->rif_index, macvlan_dev->dev_addr,
			     false);
	mlxsw_sp_rif_fdb_op(mlxsw_sp, macvlan_dev->dev_addr,
			    mlxsw_sp_fid_index(rif->fid), false);
}

void mlxsw_sp_rif_macvlan_del(struct mlxsw_sp *mlxsw_sp,
			      const struct net_device *macvlan_dev)
{
	mutex_lock(&mlxsw_sp->router->lock);
	__mlxsw_sp_rif_macvlan_del(mlxsw_sp, macvlan_dev);
	mutex_unlock(&mlxsw_sp->router->lock);
}

static int mlxsw_sp_inetaddr_macvlan_event(struct mlxsw_sp *mlxsw_sp,
					   struct net_device *macvlan_dev,
					   unsigned long event,
					   struct netlink_ext_ack *extack)
{
	switch (event) {
	case NETDEV_UP:
		return mlxsw_sp_rif_macvlan_add(mlxsw_sp, macvlan_dev, extack);
	case NETDEV_DOWN:
		__mlxsw_sp_rif_macvlan_del(mlxsw_sp, macvlan_dev);
		break;
	}

	return 0;
}

static int __mlxsw_sp_inetaddr_event(struct mlxsw_sp *mlxsw_sp,
				     struct net_device *dev,
				     unsigned long event,
				     struct netlink_ext_ack *extack)
{
	if (mlxsw_sp_port_dev_check(dev))
		return mlxsw_sp_inetaddr_port_event(dev, event, extack);
	else if (netif_is_lag_master(dev))
		return mlxsw_sp_inetaddr_lag_event(dev, event, extack);
	else if (netif_is_bridge_master(dev))
		return mlxsw_sp_inetaddr_bridge_event(mlxsw_sp, dev, event,
						      extack);
	else if (is_vlan_dev(dev))
		return mlxsw_sp_inetaddr_vlan_event(mlxsw_sp, dev, event,
						    extack);
	else if (netif_is_macvlan(dev))
		return mlxsw_sp_inetaddr_macvlan_event(mlxsw_sp, dev, event,
						       extack);
	else
		return 0;
}

static int mlxsw_sp_inetaddr_event(struct notifier_block *nb,
				   unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *) ptr;
	struct net_device *dev = ifa->ifa_dev->dev;
	struct mlxsw_sp_router *router;
	struct mlxsw_sp_rif *rif;
	int err = 0;

	/* NETDEV_UP event is handled by mlxsw_sp_inetaddr_valid_event */
	if (event == NETDEV_UP)
		return NOTIFY_DONE;

	router = container_of(nb, struct mlxsw_sp_router, inetaddr_nb);
	mutex_lock(&router->lock);
	rif = mlxsw_sp_rif_find_by_dev(router->mlxsw_sp, dev);
	if (!mlxsw_sp_rif_should_config(rif, dev, event))
		goto out;

	err = __mlxsw_sp_inetaddr_event(router->mlxsw_sp, dev, event, NULL);
out:
	mutex_unlock(&router->lock);
	return notifier_from_errno(err);
}

static int mlxsw_sp_inetaddr_valid_event(struct notifier_block *unused,
					 unsigned long event, void *ptr)
{
	struct in_validator_info *ivi = (struct in_validator_info *) ptr;
	struct net_device *dev = ivi->ivi_dev->dev;
	struct mlxsw_sp *mlxsw_sp;
	struct mlxsw_sp_rif *rif;
	int err = 0;

	mlxsw_sp = mlxsw_sp_lower_get(dev);
	if (!mlxsw_sp)
		return NOTIFY_DONE;

	mutex_lock(&mlxsw_sp->router->lock);
	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, dev);
	if (!mlxsw_sp_rif_should_config(rif, dev, event))
		goto out;

	err = __mlxsw_sp_inetaddr_event(mlxsw_sp, dev, event, ivi->extack);
out:
	mutex_unlock(&mlxsw_sp->router->lock);
	return notifier_from_errno(err);
}

struct mlxsw_sp_inet6addr_event_work {
	struct work_struct work;
	struct mlxsw_sp *mlxsw_sp;
	struct net_device *dev;
	unsigned long event;
};

static void mlxsw_sp_inet6addr_event_work(struct work_struct *work)
{
	struct mlxsw_sp_inet6addr_event_work *inet6addr_work =
		container_of(work, struct mlxsw_sp_inet6addr_event_work, work);
	struct mlxsw_sp *mlxsw_sp = inet6addr_work->mlxsw_sp;
	struct net_device *dev = inet6addr_work->dev;
	unsigned long event = inet6addr_work->event;
	struct mlxsw_sp_rif *rif;

	rtnl_lock();
	mutex_lock(&mlxsw_sp->router->lock);

	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, dev);
	if (!mlxsw_sp_rif_should_config(rif, dev, event))
		goto out;

	__mlxsw_sp_inetaddr_event(mlxsw_sp, dev, event, NULL);
out:
	mutex_unlock(&mlxsw_sp->router->lock);
	rtnl_unlock();
	dev_put(dev);
	kfree(inet6addr_work);
}

/* Called with rcu_read_lock() */
static int mlxsw_sp_inet6addr_event(struct notifier_block *nb,
				    unsigned long event, void *ptr)
{
	struct inet6_ifaddr *if6 = (struct inet6_ifaddr *) ptr;
	struct mlxsw_sp_inet6addr_event_work *inet6addr_work;
	struct net_device *dev = if6->idev->dev;
	struct mlxsw_sp_router *router;

	/* NETDEV_UP event is handled by mlxsw_sp_inet6addr_valid_event */
	if (event == NETDEV_UP)
		return NOTIFY_DONE;

	inet6addr_work = kzalloc(sizeof(*inet6addr_work), GFP_ATOMIC);
	if (!inet6addr_work)
		return NOTIFY_BAD;

	router = container_of(nb, struct mlxsw_sp_router, inet6addr_nb);
	INIT_WORK(&inet6addr_work->work, mlxsw_sp_inet6addr_event_work);
	inet6addr_work->mlxsw_sp = router->mlxsw_sp;
	inet6addr_work->dev = dev;
	inet6addr_work->event = event;
	dev_hold(dev);
	mlxsw_core_schedule_work(&inet6addr_work->work);

	return NOTIFY_DONE;
}

static int mlxsw_sp_inet6addr_valid_event(struct notifier_block *unused,
					  unsigned long event, void *ptr)
{
	struct in6_validator_info *i6vi = (struct in6_validator_info *) ptr;
	struct net_device *dev = i6vi->i6vi_dev->dev;
	struct mlxsw_sp *mlxsw_sp;
	struct mlxsw_sp_rif *rif;
	int err = 0;

	mlxsw_sp = mlxsw_sp_lower_get(dev);
	if (!mlxsw_sp)
		return NOTIFY_DONE;

	mutex_lock(&mlxsw_sp->router->lock);
	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, dev);
	if (!mlxsw_sp_rif_should_config(rif, dev, event))
		goto out;

	err = __mlxsw_sp_inetaddr_event(mlxsw_sp, dev, event, i6vi->extack);
out:
	mutex_unlock(&mlxsw_sp->router->lock);
	return notifier_from_errno(err);
}

static int mlxsw_sp_rif_edit(struct mlxsw_sp *mlxsw_sp, u16 rif_index,
			     const char *mac, int mtu, u8 mac_profile)
{
	char ritr_pl[MLXSW_REG_RITR_LEN];
	int err;

	mlxsw_reg_ritr_rif_pack(ritr_pl, rif_index);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
	if (err)
		return err;

	mlxsw_reg_ritr_mtu_set(ritr_pl, mtu);
	mlxsw_reg_ritr_if_mac_memcpy_to(ritr_pl, mac);
	mlxsw_reg_ritr_if_mac_profile_id_set(ritr_pl, mac_profile);
	mlxsw_reg_ritr_op_set(ritr_pl, MLXSW_REG_RITR_RIF_CREATE);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
}

static int
mlxsw_sp_router_port_change_event(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_rif *rif,
				  struct netlink_ext_ack *extack)
{
	struct net_device *dev = mlxsw_sp_rif_dev(rif);
	u8 old_mac_profile;
	u16 fid_index;
	int err;

	fid_index = mlxsw_sp_fid_index(rif->fid);

	err = mlxsw_sp_rif_fdb_op(mlxsw_sp, rif->addr, fid_index, false);
	if (err)
		return err;

	old_mac_profile = rif->mac_profile_id;
	err = mlxsw_sp_rif_mac_profile_replace(mlxsw_sp, rif, dev->dev_addr,
					       extack);
	if (err)
		goto err_rif_mac_profile_replace;

	err = mlxsw_sp_rif_edit(mlxsw_sp, rif->rif_index, dev->dev_addr,
				dev->mtu, rif->mac_profile_id);
	if (err)
		goto err_rif_edit;

	err = mlxsw_sp_rif_fdb_op(mlxsw_sp, dev->dev_addr, fid_index, true);
	if (err)
		goto err_rif_fdb_op;

	if (rif->mtu != dev->mtu) {
		struct mlxsw_sp_vr *vr;
		int i;

		/* The RIF is relevant only to its mr_table instance, as unlike
		 * unicast routing, in multicast routing a RIF cannot be shared
		 * between several multicast routing tables.
		 */
		vr = &mlxsw_sp->router->vrs[rif->vr_id];
		for (i = 0; i < MLXSW_SP_L3_PROTO_MAX; i++)
			mlxsw_sp_mr_rif_mtu_update(vr->mr_table[i],
						   rif, dev->mtu);
	}

	ether_addr_copy(rif->addr, dev->dev_addr);
	rif->mtu = dev->mtu;

	netdev_dbg(dev, "Updated RIF=%d\n", rif->rif_index);

	return 0;

err_rif_fdb_op:
	mlxsw_sp_rif_edit(mlxsw_sp, rif->rif_index, rif->addr, rif->mtu,
			  old_mac_profile);
err_rif_edit:
	mlxsw_sp_rif_mac_profile_replace(mlxsw_sp, rif, rif->addr, extack);
err_rif_mac_profile_replace:
	mlxsw_sp_rif_fdb_op(mlxsw_sp, rif->addr, fid_index, true);
	return err;
}

static int mlxsw_sp_router_port_pre_changeaddr_event(struct mlxsw_sp_rif *rif,
			    struct netdev_notifier_pre_changeaddr_info *info)
{
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	struct mlxsw_sp_rif_mac_profile *profile;
	struct netlink_ext_ack *extack;
	u8 max_rif_mac_profiles;
	u64 occ;

	extack = netdev_notifier_info_to_extack(&info->info);

	profile = mlxsw_sp_rif_mac_profile_find(mlxsw_sp, info->dev_addr);
	if (profile)
		return 0;

	max_rif_mac_profiles = mlxsw_sp->router->max_rif_mac_profile;
	occ = mlxsw_sp_rif_mac_profiles_occ_get(mlxsw_sp);
	if (occ < max_rif_mac_profiles)
		return 0;

	if (!mlxsw_sp_rif_mac_profile_is_shared(rif))
		return 0;

	NL_SET_ERR_MSG_MOD(extack, "Exceeded number of supported router interface MAC profiles");
	return -ENOBUFS;
}

static bool mlxsw_sp_router_netdevice_interesting(struct mlxsw_sp *mlxsw_sp,
						  struct net_device *dev)
{
	struct vlan_dev_priv *vlan;

	if (netif_is_lag_master(dev) ||
	    netif_is_bridge_master(dev) ||
	    mlxsw_sp_port_dev_check(dev) ||
	    mlxsw_sp_netdev_is_ipip_ol(mlxsw_sp, dev) ||
	    netif_is_l3_master(dev))
		return true;

	if (!is_vlan_dev(dev))
		return false;

	vlan = vlan_dev_priv(dev);
	return netif_is_lag_master(vlan->real_dev) ||
	       netif_is_bridge_master(vlan->real_dev) ||
	       mlxsw_sp_port_dev_check(vlan->real_dev);
}

static struct mlxsw_sp_crif *
mlxsw_sp_crif_register(struct mlxsw_sp_router *router, struct net_device *dev)
{
	struct mlxsw_sp_crif *crif;
	int err;

	if (WARN_ON(mlxsw_sp_crif_lookup(router, dev)))
		return NULL;

	crif = mlxsw_sp_crif_alloc(dev);
	if (!crif)
		return ERR_PTR(-ENOMEM);

	err = mlxsw_sp_crif_insert(router, crif);
	if (err)
		goto err_netdev_insert;

	return crif;

err_netdev_insert:
	mlxsw_sp_crif_free(crif);
	return ERR_PTR(err);
}

static void mlxsw_sp_crif_unregister(struct mlxsw_sp_router *router,
				     struct mlxsw_sp_crif *crif)
{
	struct mlxsw_sp_nexthop *nh, *tmp;

	mlxsw_sp_crif_remove(router, crif);

	list_for_each_entry_safe(nh, tmp, &crif->nexthop_list, crif_list_node)
		mlxsw_sp_nexthop_type_fini(router->mlxsw_sp, nh);

	if (crif->rif)
		crif->can_destroy = true;
	else
		mlxsw_sp_crif_free(crif);
}

static int mlxsw_sp_netdevice_register(struct mlxsw_sp_router *router,
				       struct net_device *dev)
{
	struct mlxsw_sp_crif *crif;

	if (!mlxsw_sp_router_netdevice_interesting(router->mlxsw_sp, dev))
		return 0;

	crif = mlxsw_sp_crif_register(router, dev);
	return PTR_ERR_OR_ZERO(crif);
}

static void mlxsw_sp_netdevice_unregister(struct mlxsw_sp_router *router,
					  struct net_device *dev)
{
	struct mlxsw_sp_crif *crif;

	if (!mlxsw_sp_router_netdevice_interesting(router->mlxsw_sp, dev))
		return;

	/* netdev_run_todo(), by way of netdev_wait_allrefs_any(), rebroadcasts
	 * the NETDEV_UNREGISTER message, so we can get here twice. If that's
	 * what happened, the netdevice state is NETREG_UNREGISTERED. In that
	 * case, we expect to have collected the CRIF already, and warn if it
	 * still exists. Otherwise we expect the CRIF to exist.
	 */
	crif = mlxsw_sp_crif_lookup(router, dev);
	if (dev->reg_state == NETREG_UNREGISTERED) {
		if (!WARN_ON(crif))
			return;
	}
	if (WARN_ON(!crif))
		return;

	mlxsw_sp_crif_unregister(router, crif);
}

static bool mlxsw_sp_is_offload_xstats_event(unsigned long event)
{
	switch (event) {
	case NETDEV_OFFLOAD_XSTATS_ENABLE:
	case NETDEV_OFFLOAD_XSTATS_DISABLE:
	case NETDEV_OFFLOAD_XSTATS_REPORT_USED:
	case NETDEV_OFFLOAD_XSTATS_REPORT_DELTA:
		return true;
	}

	return false;
}

static int
mlxsw_sp_router_port_offload_xstats_cmd(struct mlxsw_sp_rif *rif,
					unsigned long event,
					struct netdev_notifier_offload_xstats_info *info)
{
	switch (info->type) {
	case NETDEV_OFFLOAD_XSTATS_TYPE_L3:
		break;
	default:
		return 0;
	}

	switch (event) {
	case NETDEV_OFFLOAD_XSTATS_ENABLE:
		return mlxsw_sp_router_port_l3_stats_enable(rif);
	case NETDEV_OFFLOAD_XSTATS_DISABLE:
		mlxsw_sp_router_port_l3_stats_disable(rif);
		return 0;
	case NETDEV_OFFLOAD_XSTATS_REPORT_USED:
		mlxsw_sp_router_port_l3_stats_report_used(rif, info);
		return 0;
	case NETDEV_OFFLOAD_XSTATS_REPORT_DELTA:
		return mlxsw_sp_router_port_l3_stats_report_delta(rif, info);
	}

	WARN_ON_ONCE(1);
	return 0;
}

static int
mlxsw_sp_netdevice_offload_xstats_cmd(struct mlxsw_sp *mlxsw_sp,
				      struct net_device *dev,
				      unsigned long event,
				      struct netdev_notifier_offload_xstats_info *info)
{
	struct mlxsw_sp_rif *rif;

	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, dev);
	if (!rif)
		return 0;

	return mlxsw_sp_router_port_offload_xstats_cmd(rif, event, info);
}

static bool mlxsw_sp_is_router_event(unsigned long event)
{
	switch (event) {
	case NETDEV_PRE_CHANGEADDR:
	case NETDEV_CHANGEADDR:
	case NETDEV_CHANGEMTU:
		return true;
	default:
		return false;
	}
}

static int mlxsw_sp_netdevice_router_port_event(struct net_device *dev,
						unsigned long event, void *ptr)
{
	struct netlink_ext_ack *extack = netdev_notifier_info_to_extack(ptr);
	struct mlxsw_sp *mlxsw_sp;
	struct mlxsw_sp_rif *rif;

	mlxsw_sp = mlxsw_sp_lower_get(dev);
	if (!mlxsw_sp)
		return 0;

	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, dev);
	if (!rif)
		return 0;

	switch (event) {
	case NETDEV_CHANGEMTU:
	case NETDEV_CHANGEADDR:
		return mlxsw_sp_router_port_change_event(mlxsw_sp, rif, extack);
	case NETDEV_PRE_CHANGEADDR:
		return mlxsw_sp_router_port_pre_changeaddr_event(rif, ptr);
	default:
		WARN_ON_ONCE(1);
		break;
	}

	return 0;
}

static int mlxsw_sp_port_vrf_join(struct mlxsw_sp *mlxsw_sp,
				  struct net_device *l3_dev,
				  struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_rif *rif;

	/* If netdev is already associated with a RIF, then we need to
	 * destroy it and create a new one with the new virtual router ID.
	 */
	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, l3_dev);
	if (rif)
		__mlxsw_sp_inetaddr_event(mlxsw_sp, l3_dev, NETDEV_DOWN,
					  extack);

	return __mlxsw_sp_inetaddr_event(mlxsw_sp, l3_dev, NETDEV_UP, extack);
}

static void mlxsw_sp_port_vrf_leave(struct mlxsw_sp *mlxsw_sp,
				    struct net_device *l3_dev)
{
	struct mlxsw_sp_rif *rif;

	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, l3_dev);
	if (!rif)
		return;
	__mlxsw_sp_inetaddr_event(mlxsw_sp, l3_dev, NETDEV_DOWN, NULL);
}

static bool mlxsw_sp_is_vrf_event(unsigned long event, void *ptr)
{
	struct netdev_notifier_changeupper_info *info = ptr;

	if (event != NETDEV_PRECHANGEUPPER && event != NETDEV_CHANGEUPPER)
		return false;
	return netif_is_l3_master(info->upper_dev);
}

static int
mlxsw_sp_netdevice_vrf_event(struct net_device *l3_dev, unsigned long event,
			     struct netdev_notifier_changeupper_info *info)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_lower_get(l3_dev);
	int err = 0;

	/* We do not create a RIF for a macvlan, but only use it to
	 * direct more MAC addresses to the router.
	 */
	if (!mlxsw_sp || netif_is_macvlan(l3_dev))
		return 0;

	switch (event) {
	case NETDEV_PRECHANGEUPPER:
		break;
	case NETDEV_CHANGEUPPER:
		if (info->linking) {
			struct netlink_ext_ack *extack;

			extack = netdev_notifier_info_to_extack(&info->info);
			err = mlxsw_sp_port_vrf_join(mlxsw_sp, l3_dev, extack);
		} else {
			mlxsw_sp_port_vrf_leave(mlxsw_sp, l3_dev);
		}
		break;
	}

	return err;
}

static int
mlxsw_sp_port_vid_router_join_existing(struct mlxsw_sp_port *mlxsw_sp_port,
				       u16 vid, struct net_device *dev,
				       struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;

	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_find_by_vid(mlxsw_sp_port,
							    vid);
	if (WARN_ON(!mlxsw_sp_port_vlan))
		return -EINVAL;

	return mlxsw_sp_port_vlan_router_join_existing(mlxsw_sp_port_vlan,
						       dev, extack);
}

static int __mlxsw_sp_router_port_join_lag(struct mlxsw_sp_port *mlxsw_sp_port,
					   struct net_device *lag_dev,
					   struct netlink_ext_ack *extack)
{
	u16 default_vid = MLXSW_SP_DEFAULT_VID;

	return mlxsw_sp_port_vid_router_join_existing(mlxsw_sp_port,
						      default_vid, lag_dev,
						      extack);
}

int mlxsw_sp_router_port_join_lag(struct mlxsw_sp_port *mlxsw_sp_port,
				  struct net_device *lag_dev,
				  struct netlink_ext_ack *extack)
{
	int err;

	mutex_lock(&mlxsw_sp_port->mlxsw_sp->router->lock);
	err = __mlxsw_sp_router_port_join_lag(mlxsw_sp_port, lag_dev, extack);
	mutex_unlock(&mlxsw_sp_port->mlxsw_sp->router->lock);

	return err;
}

static int mlxsw_sp_router_netdevice_event(struct notifier_block *nb,
					   unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct mlxsw_sp_router *router;
	struct mlxsw_sp *mlxsw_sp;
	int err = 0;

	router = container_of(nb, struct mlxsw_sp_router, netdevice_nb);
	mlxsw_sp = router->mlxsw_sp;

	mutex_lock(&mlxsw_sp->router->lock);

	if (event == NETDEV_REGISTER) {
		err = mlxsw_sp_netdevice_register(router, dev);
		if (err)
			/* No need to roll this back, UNREGISTER will collect it
			 * anyhow.
			 */
			goto out;
	}

	if (mlxsw_sp_is_offload_xstats_event(event))
		err = mlxsw_sp_netdevice_offload_xstats_cmd(mlxsw_sp, dev,
							    event, ptr);
	else if (mlxsw_sp_netdev_is_ipip_ol(mlxsw_sp, dev))
		err = mlxsw_sp_netdevice_ipip_ol_event(mlxsw_sp, dev,
						       event, ptr);
	else if (mlxsw_sp_netdev_is_ipip_ul(mlxsw_sp, dev))
		err = mlxsw_sp_netdevice_ipip_ul_event(mlxsw_sp, dev,
						       event, ptr);
	else if (mlxsw_sp_is_router_event(event))
		err = mlxsw_sp_netdevice_router_port_event(dev, event, ptr);
	else if (mlxsw_sp_is_vrf_event(event, ptr))
		err = mlxsw_sp_netdevice_vrf_event(dev, event, ptr);

	if (event == NETDEV_UNREGISTER)
		mlxsw_sp_netdevice_unregister(router, dev);

out:
	mutex_unlock(&mlxsw_sp->router->lock);

	return notifier_from_errno(err);
}

static int __mlxsw_sp_rif_macvlan_flush(struct net_device *dev,
					struct netdev_nested_priv *priv)
{
	struct mlxsw_sp_rif *rif = (struct mlxsw_sp_rif *)priv->data;

	if (!netif_is_macvlan(dev))
		return 0;

	return mlxsw_sp_rif_fdb_op(rif->mlxsw_sp, dev->dev_addr,
				   mlxsw_sp_fid_index(rif->fid), false);
}

static int mlxsw_sp_rif_macvlan_flush(struct mlxsw_sp_rif *rif)
{
	struct net_device *dev = mlxsw_sp_rif_dev(rif);
	struct netdev_nested_priv priv = {
		.data = (void *)rif,
	};

	if (!netif_is_macvlan_port(dev))
		return 0;

	netdev_warn(dev, "Router interface is deleted. Upper macvlans will not work\n");
	return netdev_walk_all_upper_dev_rcu(dev,
					     __mlxsw_sp_rif_macvlan_flush, &priv);
}

static void mlxsw_sp_rif_subport_setup(struct mlxsw_sp_rif *rif,
				       const struct mlxsw_sp_rif_params *params)
{
	struct mlxsw_sp_rif_subport *rif_subport;

	rif_subport = mlxsw_sp_rif_subport_rif(rif);
	refcount_set(&rif_subport->ref_count, 1);
	rif_subport->vid = params->vid;
	rif_subport->lag = params->lag;
	if (params->lag)
		rif_subport->lag_id = params->lag_id;
	else
		rif_subport->system_port = params->system_port;
}

static int mlxsw_sp_rif_subport_op(struct mlxsw_sp_rif *rif, bool enable)
{
	struct net_device *dev = mlxsw_sp_rif_dev(rif);
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	struct mlxsw_sp_rif_subport *rif_subport;
	char ritr_pl[MLXSW_REG_RITR_LEN];
	u16 efid;

	rif_subport = mlxsw_sp_rif_subport_rif(rif);
	mlxsw_reg_ritr_pack(ritr_pl, enable, MLXSW_REG_RITR_SP_IF,
			    rif->rif_index, rif->vr_id, dev->mtu);
	mlxsw_reg_ritr_mac_pack(ritr_pl, dev->dev_addr);
	mlxsw_reg_ritr_if_mac_profile_id_set(ritr_pl, rif->mac_profile_id);
	efid = mlxsw_sp_fid_index(rif->fid);
	mlxsw_reg_ritr_sp_if_pack(ritr_pl, rif_subport->lag,
				  rif_subport->lag ? rif_subport->lag_id :
						     rif_subport->system_port,
				  efid, 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
}

static int mlxsw_sp_rif_subport_configure(struct mlxsw_sp_rif *rif,
					  struct netlink_ext_ack *extack)
{
	struct net_device *dev = mlxsw_sp_rif_dev(rif);
	u8 mac_profile;
	int err;

	err = mlxsw_sp_rif_mac_profile_get(rif->mlxsw_sp, rif->addr,
					   &mac_profile, extack);
	if (err)
		return err;
	rif->mac_profile_id = mac_profile;

	err = mlxsw_sp_rif_subport_op(rif, true);
	if (err)
		goto err_rif_subport_op;

	err = mlxsw_sp_rif_fdb_op(rif->mlxsw_sp, dev->dev_addr,
				  mlxsw_sp_fid_index(rif->fid), true);
	if (err)
		goto err_rif_fdb_op;

	err = mlxsw_sp_fid_rif_set(rif->fid, rif);
	if (err)
		goto err_fid_rif_set;

	return 0;

err_fid_rif_set:
	mlxsw_sp_rif_fdb_op(rif->mlxsw_sp, dev->dev_addr,
			    mlxsw_sp_fid_index(rif->fid), false);
err_rif_fdb_op:
	mlxsw_sp_rif_subport_op(rif, false);
err_rif_subport_op:
	mlxsw_sp_rif_mac_profile_put(rif->mlxsw_sp, mac_profile);
	return err;
}

static void mlxsw_sp_rif_subport_deconfigure(struct mlxsw_sp_rif *rif)
{
	struct net_device *dev = mlxsw_sp_rif_dev(rif);
	struct mlxsw_sp_fid *fid = rif->fid;

	mlxsw_sp_fid_rif_unset(fid);
	mlxsw_sp_rif_fdb_op(rif->mlxsw_sp, dev->dev_addr,
			    mlxsw_sp_fid_index(fid), false);
	mlxsw_sp_rif_macvlan_flush(rif);
	mlxsw_sp_rif_subport_op(rif, false);
	mlxsw_sp_rif_mac_profile_put(rif->mlxsw_sp, rif->mac_profile_id);
}

static struct mlxsw_sp_fid *
mlxsw_sp_rif_subport_fid_get(struct mlxsw_sp_rif *rif,
			     struct netlink_ext_ack *extack)
{
	return mlxsw_sp_fid_rfid_get(rif->mlxsw_sp, rif->rif_index);
}

static const struct mlxsw_sp_rif_ops mlxsw_sp_rif_subport_ops = {
	.type			= MLXSW_SP_RIF_TYPE_SUBPORT,
	.rif_size		= sizeof(struct mlxsw_sp_rif_subport),
	.setup			= mlxsw_sp_rif_subport_setup,
	.configure		= mlxsw_sp_rif_subport_configure,
	.deconfigure		= mlxsw_sp_rif_subport_deconfigure,
	.fid_get		= mlxsw_sp_rif_subport_fid_get,
};

static int mlxsw_sp_rif_fid_op(struct mlxsw_sp_rif *rif, u16 fid, bool enable)
{
	enum mlxsw_reg_ritr_if_type type = MLXSW_REG_RITR_FID_IF;
	struct net_device *dev = mlxsw_sp_rif_dev(rif);
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	char ritr_pl[MLXSW_REG_RITR_LEN];

	mlxsw_reg_ritr_pack(ritr_pl, enable, type, rif->rif_index, rif->vr_id,
			    dev->mtu);
	mlxsw_reg_ritr_mac_pack(ritr_pl, dev->dev_addr);
	mlxsw_reg_ritr_if_mac_profile_id_set(ritr_pl, rif->mac_profile_id);
	mlxsw_reg_ritr_fid_if_fid_set(ritr_pl, fid);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
}

u16 mlxsw_sp_router_port(const struct mlxsw_sp *mlxsw_sp)
{
	return mlxsw_core_max_ports(mlxsw_sp->core) + 1;
}

static int mlxsw_sp_rif_fid_configure(struct mlxsw_sp_rif *rif,
				      struct netlink_ext_ack *extack)
{
	struct net_device *dev = mlxsw_sp_rif_dev(rif);
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	u16 fid_index = mlxsw_sp_fid_index(rif->fid);
	u8 mac_profile;
	int err;

	err = mlxsw_sp_rif_mac_profile_get(mlxsw_sp, rif->addr,
					   &mac_profile, extack);
	if (err)
		return err;
	rif->mac_profile_id = mac_profile;

	err = mlxsw_sp_rif_fid_op(rif, fid_index, true);
	if (err)
		goto err_rif_fid_op;

	err = mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_MC,
				     mlxsw_sp_router_port(mlxsw_sp), true);
	if (err)
		goto err_fid_mc_flood_set;

	err = mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_BC,
				     mlxsw_sp_router_port(mlxsw_sp), true);
	if (err)
		goto err_fid_bc_flood_set;

	err = mlxsw_sp_rif_fdb_op(rif->mlxsw_sp, dev->dev_addr,
				  mlxsw_sp_fid_index(rif->fid), true);
	if (err)
		goto err_rif_fdb_op;

	err = mlxsw_sp_fid_rif_set(rif->fid, rif);
	if (err)
		goto err_fid_rif_set;

	return 0;

err_fid_rif_set:
	mlxsw_sp_rif_fdb_op(rif->mlxsw_sp, dev->dev_addr,
			    mlxsw_sp_fid_index(rif->fid), false);
err_rif_fdb_op:
	mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_BC,
			       mlxsw_sp_router_port(mlxsw_sp), false);
err_fid_bc_flood_set:
	mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_MC,
			       mlxsw_sp_router_port(mlxsw_sp), false);
err_fid_mc_flood_set:
	mlxsw_sp_rif_fid_op(rif, fid_index, false);
err_rif_fid_op:
	mlxsw_sp_rif_mac_profile_put(mlxsw_sp, mac_profile);
	return err;
}

static void mlxsw_sp_rif_fid_deconfigure(struct mlxsw_sp_rif *rif)
{
	struct net_device *dev = mlxsw_sp_rif_dev(rif);
	u16 fid_index = mlxsw_sp_fid_index(rif->fid);
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	struct mlxsw_sp_fid *fid = rif->fid;

	mlxsw_sp_fid_rif_unset(fid);
	mlxsw_sp_rif_fdb_op(rif->mlxsw_sp, dev->dev_addr,
			    mlxsw_sp_fid_index(fid), false);
	mlxsw_sp_rif_macvlan_flush(rif);
	mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_BC,
			       mlxsw_sp_router_port(mlxsw_sp), false);
	mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_MC,
			       mlxsw_sp_router_port(mlxsw_sp), false);
	mlxsw_sp_rif_fid_op(rif, fid_index, false);
	mlxsw_sp_rif_mac_profile_put(rif->mlxsw_sp, rif->mac_profile_id);
}

static struct mlxsw_sp_fid *
mlxsw_sp_rif_fid_fid_get(struct mlxsw_sp_rif *rif,
			 struct netlink_ext_ack *extack)
{
	int rif_ifindex = mlxsw_sp_rif_dev_ifindex(rif);

	return mlxsw_sp_fid_8021d_get(rif->mlxsw_sp, rif_ifindex);
}

static void mlxsw_sp_rif_fid_fdb_del(struct mlxsw_sp_rif *rif, const char *mac)
{
	struct switchdev_notifier_fdb_info info = {};
	struct net_device *dev;

	dev = br_fdb_find_port(mlxsw_sp_rif_dev(rif), mac, 0);
	if (!dev)
		return;

	info.addr = mac;
	info.vid = 0;
	call_switchdev_notifiers(SWITCHDEV_FDB_DEL_TO_BRIDGE, dev, &info.info,
				 NULL);
}

static const struct mlxsw_sp_rif_ops mlxsw_sp_rif_fid_ops = {
	.type			= MLXSW_SP_RIF_TYPE_FID,
	.rif_size		= sizeof(struct mlxsw_sp_rif),
	.configure		= mlxsw_sp_rif_fid_configure,
	.deconfigure		= mlxsw_sp_rif_fid_deconfigure,
	.fid_get		= mlxsw_sp_rif_fid_fid_get,
	.fdb_del		= mlxsw_sp_rif_fid_fdb_del,
};

static struct mlxsw_sp_fid *
mlxsw_sp_rif_vlan_fid_get(struct mlxsw_sp_rif *rif,
			  struct netlink_ext_ack *extack)
{
	struct net_device *dev = mlxsw_sp_rif_dev(rif);
	struct net_device *br_dev;
	u16 vid;
	int err;

	if (is_vlan_dev(dev)) {
		vid = vlan_dev_vlan_id(dev);
		br_dev = vlan_dev_real_dev(dev);
		if (WARN_ON(!netif_is_bridge_master(br_dev)))
			return ERR_PTR(-EINVAL);
	} else {
		err = br_vlan_get_pvid(dev, &vid);
		if (err < 0 || !vid) {
			NL_SET_ERR_MSG_MOD(extack, "Couldn't determine bridge PVID");
			return ERR_PTR(-EINVAL);
		}
	}

	return mlxsw_sp_fid_8021q_get(rif->mlxsw_sp, vid);
}

static void mlxsw_sp_rif_vlan_fdb_del(struct mlxsw_sp_rif *rif, const char *mac)
{
	struct net_device *rif_dev = mlxsw_sp_rif_dev(rif);
	struct switchdev_notifier_fdb_info info = {};
	u16 vid = mlxsw_sp_fid_8021q_vid(rif->fid);
	struct net_device *br_dev;
	struct net_device *dev;

	br_dev = is_vlan_dev(rif_dev) ? vlan_dev_real_dev(rif_dev) : rif_dev;
	dev = br_fdb_find_port(br_dev, mac, vid);
	if (!dev)
		return;

	info.addr = mac;
	info.vid = vid;
	call_switchdev_notifiers(SWITCHDEV_FDB_DEL_TO_BRIDGE, dev, &info.info,
				 NULL);
}

static int mlxsw_sp_rif_vlan_op(struct mlxsw_sp_rif *rif, u16 vid, u16 efid,
				bool enable)
{
	struct net_device *dev = mlxsw_sp_rif_dev(rif);
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	char ritr_pl[MLXSW_REG_RITR_LEN];

	mlxsw_reg_ritr_vlan_if_pack(ritr_pl, enable, rif->rif_index, rif->vr_id,
				    dev->mtu, dev->dev_addr,
				    rif->mac_profile_id, vid, efid);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
}

static int mlxsw_sp_rif_vlan_configure(struct mlxsw_sp_rif *rif, u16 efid,
				       struct netlink_ext_ack *extack)
{
	struct net_device *dev = mlxsw_sp_rif_dev(rif);
	u16 vid = mlxsw_sp_fid_8021q_vid(rif->fid);
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	u8 mac_profile;
	int err;

	err = mlxsw_sp_rif_mac_profile_get(mlxsw_sp, rif->addr,
					   &mac_profile, extack);
	if (err)
		return err;
	rif->mac_profile_id = mac_profile;

	err = mlxsw_sp_rif_vlan_op(rif, vid, efid, true);
	if (err)
		goto err_rif_vlan_fid_op;

	err = mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_MC,
				     mlxsw_sp_router_port(mlxsw_sp), true);
	if (err)
		goto err_fid_mc_flood_set;

	err = mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_BC,
				     mlxsw_sp_router_port(mlxsw_sp), true);
	if (err)
		goto err_fid_bc_flood_set;

	err = mlxsw_sp_rif_fdb_op(rif->mlxsw_sp, dev->dev_addr,
				  mlxsw_sp_fid_index(rif->fid), true);
	if (err)
		goto err_rif_fdb_op;

	err = mlxsw_sp_fid_rif_set(rif->fid, rif);
	if (err)
		goto err_fid_rif_set;

	return 0;

err_fid_rif_set:
	mlxsw_sp_rif_fdb_op(rif->mlxsw_sp, dev->dev_addr,
			    mlxsw_sp_fid_index(rif->fid), false);
err_rif_fdb_op:
	mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_BC,
			       mlxsw_sp_router_port(mlxsw_sp), false);
err_fid_bc_flood_set:
	mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_MC,
			       mlxsw_sp_router_port(mlxsw_sp), false);
err_fid_mc_flood_set:
	mlxsw_sp_rif_vlan_op(rif, vid, 0, false);
err_rif_vlan_fid_op:
	mlxsw_sp_rif_mac_profile_put(mlxsw_sp, mac_profile);
	return err;
}

static void mlxsw_sp_rif_vlan_deconfigure(struct mlxsw_sp_rif *rif)
{
	struct net_device *dev = mlxsw_sp_rif_dev(rif);
	u16 vid = mlxsw_sp_fid_8021q_vid(rif->fid);
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;

	mlxsw_sp_fid_rif_unset(rif->fid);
	mlxsw_sp_rif_fdb_op(rif->mlxsw_sp, dev->dev_addr,
			    mlxsw_sp_fid_index(rif->fid), false);
	mlxsw_sp_rif_macvlan_flush(rif);
	mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_BC,
			       mlxsw_sp_router_port(mlxsw_sp), false);
	mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_MC,
			       mlxsw_sp_router_port(mlxsw_sp), false);
	mlxsw_sp_rif_vlan_op(rif, vid, 0, false);
	mlxsw_sp_rif_mac_profile_put(rif->mlxsw_sp, rif->mac_profile_id);
}

static int mlxsw_sp1_rif_vlan_configure(struct mlxsw_sp_rif *rif,
					struct netlink_ext_ack *extack)
{
	return mlxsw_sp_rif_vlan_configure(rif, 0, extack);
}

static const struct mlxsw_sp_rif_ops mlxsw_sp1_rif_vlan_ops = {
	.type			= MLXSW_SP_RIF_TYPE_VLAN,
	.rif_size		= sizeof(struct mlxsw_sp_rif),
	.configure		= mlxsw_sp1_rif_vlan_configure,
	.deconfigure		= mlxsw_sp_rif_vlan_deconfigure,
	.fid_get		= mlxsw_sp_rif_vlan_fid_get,
	.fdb_del		= mlxsw_sp_rif_vlan_fdb_del,
};

static int mlxsw_sp2_rif_vlan_configure(struct mlxsw_sp_rif *rif,
					struct netlink_ext_ack *extack)
{
	u16 efid = mlxsw_sp_fid_index(rif->fid);

	return mlxsw_sp_rif_vlan_configure(rif, efid, extack);
}

static const struct mlxsw_sp_rif_ops mlxsw_sp2_rif_vlan_ops = {
	.type			= MLXSW_SP_RIF_TYPE_VLAN,
	.rif_size		= sizeof(struct mlxsw_sp_rif),
	.configure		= mlxsw_sp2_rif_vlan_configure,
	.deconfigure		= mlxsw_sp_rif_vlan_deconfigure,
	.fid_get		= mlxsw_sp_rif_vlan_fid_get,
	.fdb_del		= mlxsw_sp_rif_vlan_fdb_del,
};

static struct mlxsw_sp_rif_ipip_lb *
mlxsw_sp_rif_ipip_lb_rif(struct mlxsw_sp_rif *rif)
{
	return container_of(rif, struct mlxsw_sp_rif_ipip_lb, common);
}

static void
mlxsw_sp_rif_ipip_lb_setup(struct mlxsw_sp_rif *rif,
			   const struct mlxsw_sp_rif_params *params)
{
	struct mlxsw_sp_rif_params_ipip_lb *params_lb;
	struct mlxsw_sp_rif_ipip_lb *rif_lb;

	params_lb = container_of(params, struct mlxsw_sp_rif_params_ipip_lb,
				 common);
	rif_lb = mlxsw_sp_rif_ipip_lb_rif(rif);
	rif_lb->lb_config = params_lb->lb_config;
}

static int
mlxsw_sp1_rif_ipip_lb_configure(struct mlxsw_sp_rif *rif,
				struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_rif_ipip_lb *lb_rif = mlxsw_sp_rif_ipip_lb_rif(rif);
	struct net_device *dev = mlxsw_sp_rif_dev(rif);
	u32 ul_tb_id = mlxsw_sp_ipip_dev_ul_tb_id(dev);
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	struct mlxsw_sp_vr *ul_vr;
	int err;

	ul_vr = mlxsw_sp_vr_get(mlxsw_sp, ul_tb_id, extack);
	if (IS_ERR(ul_vr))
		return PTR_ERR(ul_vr);

	err = mlxsw_sp_rif_ipip_lb_op(lb_rif, ul_vr->id, 0, true);
	if (err)
		goto err_loopback_op;

	lb_rif->ul_vr_id = ul_vr->id;
	lb_rif->ul_rif_id = 0;
	++ul_vr->rif_count;
	return 0;

err_loopback_op:
	mlxsw_sp_vr_put(mlxsw_sp, ul_vr);
	return err;
}

static void mlxsw_sp1_rif_ipip_lb_deconfigure(struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp_rif_ipip_lb *lb_rif = mlxsw_sp_rif_ipip_lb_rif(rif);
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	struct mlxsw_sp_vr *ul_vr;

	ul_vr = &mlxsw_sp->router->vrs[lb_rif->ul_vr_id];
	mlxsw_sp_rif_ipip_lb_op(lb_rif, ul_vr->id, 0, false);

	--ul_vr->rif_count;
	mlxsw_sp_vr_put(mlxsw_sp, ul_vr);
}

static const struct mlxsw_sp_rif_ops mlxsw_sp1_rif_ipip_lb_ops = {
	.type			= MLXSW_SP_RIF_TYPE_IPIP_LB,
	.rif_size		= sizeof(struct mlxsw_sp_rif_ipip_lb),
	.setup                  = mlxsw_sp_rif_ipip_lb_setup,
	.configure		= mlxsw_sp1_rif_ipip_lb_configure,
	.deconfigure		= mlxsw_sp1_rif_ipip_lb_deconfigure,
};

static const struct mlxsw_sp_rif_ops *mlxsw_sp1_rif_ops_arr[] = {
	[MLXSW_SP_RIF_TYPE_SUBPORT]	= &mlxsw_sp_rif_subport_ops,
	[MLXSW_SP_RIF_TYPE_VLAN]	= &mlxsw_sp1_rif_vlan_ops,
	[MLXSW_SP_RIF_TYPE_FID]		= &mlxsw_sp_rif_fid_ops,
	[MLXSW_SP_RIF_TYPE_IPIP_LB]	= &mlxsw_sp1_rif_ipip_lb_ops,
};

static int
mlxsw_sp_rif_ipip_lb_ul_rif_op(struct mlxsw_sp_rif *ul_rif, bool enable)
{
	struct mlxsw_sp *mlxsw_sp = ul_rif->mlxsw_sp;
	char ritr_pl[MLXSW_REG_RITR_LEN];

	mlxsw_reg_ritr_pack(ritr_pl, enable, MLXSW_REG_RITR_LOOPBACK_IF,
			    ul_rif->rif_index, ul_rif->vr_id, IP_MAX_MTU);
	mlxsw_reg_ritr_loopback_protocol_set(ritr_pl,
					     MLXSW_REG_RITR_LOOPBACK_GENERIC);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
}

static struct mlxsw_sp_rif *
mlxsw_sp_ul_rif_create(struct mlxsw_sp *mlxsw_sp, struct mlxsw_sp_vr *vr,
		       struct mlxsw_sp_crif *ul_crif,
		       struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_rif *ul_rif;
	u8 rif_entries = 1;
	u16 rif_index;
	int err;

	err = mlxsw_sp_rif_index_alloc(mlxsw_sp, &rif_index, rif_entries);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Exceeded number of supported router interfaces");
		return ERR_PTR(err);
	}

	ul_rif = mlxsw_sp_rif_alloc(sizeof(*ul_rif), rif_index, vr->id,
				    ul_crif);
	if (!ul_rif) {
		err = -ENOMEM;
		goto err_rif_alloc;
	}

	mlxsw_sp->router->rifs[rif_index] = ul_rif;
	ul_rif->mlxsw_sp = mlxsw_sp;
	ul_rif->rif_entries = rif_entries;
	err = mlxsw_sp_rif_ipip_lb_ul_rif_op(ul_rif, true);
	if (err)
		goto ul_rif_op_err;

	atomic_add(rif_entries, &mlxsw_sp->router->rifs_count);
	return ul_rif;

ul_rif_op_err:
	mlxsw_sp->router->rifs[rif_index] = NULL;
	mlxsw_sp_rif_free(ul_rif);
err_rif_alloc:
	mlxsw_sp_rif_index_free(mlxsw_sp, rif_index, rif_entries);
	return ERR_PTR(err);
}

static void mlxsw_sp_ul_rif_destroy(struct mlxsw_sp_rif *ul_rif)
{
	struct mlxsw_sp *mlxsw_sp = ul_rif->mlxsw_sp;
	u8 rif_entries = ul_rif->rif_entries;
	u16 rif_index = ul_rif->rif_index;

	atomic_sub(rif_entries, &mlxsw_sp->router->rifs_count);
	mlxsw_sp_rif_ipip_lb_ul_rif_op(ul_rif, false);
	mlxsw_sp->router->rifs[ul_rif->rif_index] = NULL;
	mlxsw_sp_rif_free(ul_rif);
	mlxsw_sp_rif_index_free(mlxsw_sp, rif_index, rif_entries);
}

static struct mlxsw_sp_rif *
mlxsw_sp_ul_rif_get(struct mlxsw_sp *mlxsw_sp, u32 tb_id,
		    struct mlxsw_sp_crif *ul_crif,
		    struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_vr *vr;
	int err;

	vr = mlxsw_sp_vr_get(mlxsw_sp, tb_id, extack);
	if (IS_ERR(vr))
		return ERR_CAST(vr);

	if (refcount_inc_not_zero(&vr->ul_rif_refcnt))
		return vr->ul_rif;

	vr->ul_rif = mlxsw_sp_ul_rif_create(mlxsw_sp, vr, ul_crif, extack);
	if (IS_ERR(vr->ul_rif)) {
		err = PTR_ERR(vr->ul_rif);
		goto err_ul_rif_create;
	}

	vr->rif_count++;
	refcount_set(&vr->ul_rif_refcnt, 1);

	return vr->ul_rif;

err_ul_rif_create:
	mlxsw_sp_vr_put(mlxsw_sp, vr);
	return ERR_PTR(err);
}

static void mlxsw_sp_ul_rif_put(struct mlxsw_sp_rif *ul_rif)
{
	struct mlxsw_sp *mlxsw_sp = ul_rif->mlxsw_sp;
	struct mlxsw_sp_vr *vr;

	vr = &mlxsw_sp->router->vrs[ul_rif->vr_id];

	if (!refcount_dec_and_test(&vr->ul_rif_refcnt))
		return;

	vr->rif_count--;
	mlxsw_sp_ul_rif_destroy(ul_rif);
	mlxsw_sp_vr_put(mlxsw_sp, vr);
}

int mlxsw_sp_router_ul_rif_get(struct mlxsw_sp *mlxsw_sp, u32 ul_tb_id,
			       u16 *ul_rif_index)
{
	struct mlxsw_sp_rif *ul_rif;
	int err = 0;

	mutex_lock(&mlxsw_sp->router->lock);
	ul_rif = mlxsw_sp_ul_rif_get(mlxsw_sp, ul_tb_id, NULL, NULL);
	if (IS_ERR(ul_rif)) {
		err = PTR_ERR(ul_rif);
		goto out;
	}
	*ul_rif_index = ul_rif->rif_index;
out:
	mutex_unlock(&mlxsw_sp->router->lock);
	return err;
}

void mlxsw_sp_router_ul_rif_put(struct mlxsw_sp *mlxsw_sp, u16 ul_rif_index)
{
	struct mlxsw_sp_rif *ul_rif;

	mutex_lock(&mlxsw_sp->router->lock);
	ul_rif = mlxsw_sp->router->rifs[ul_rif_index];
	if (WARN_ON(!ul_rif))
		goto out;

	mlxsw_sp_ul_rif_put(ul_rif);
out:
	mutex_unlock(&mlxsw_sp->router->lock);
}

static int
mlxsw_sp2_rif_ipip_lb_configure(struct mlxsw_sp_rif *rif,
				struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_rif_ipip_lb *lb_rif = mlxsw_sp_rif_ipip_lb_rif(rif);
	struct net_device *dev = mlxsw_sp_rif_dev(rif);
	u32 ul_tb_id = mlxsw_sp_ipip_dev_ul_tb_id(dev);
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	struct mlxsw_sp_rif *ul_rif;
	int err;

	ul_rif = mlxsw_sp_ul_rif_get(mlxsw_sp, ul_tb_id, NULL, extack);
	if (IS_ERR(ul_rif))
		return PTR_ERR(ul_rif);

	err = mlxsw_sp_rif_ipip_lb_op(lb_rif, 0, ul_rif->rif_index, true);
	if (err)
		goto err_loopback_op;

	lb_rif->ul_vr_id = 0;
	lb_rif->ul_rif_id = ul_rif->rif_index;

	return 0;

err_loopback_op:
	mlxsw_sp_ul_rif_put(ul_rif);
	return err;
}

static void mlxsw_sp2_rif_ipip_lb_deconfigure(struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp_rif_ipip_lb *lb_rif = mlxsw_sp_rif_ipip_lb_rif(rif);
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	struct mlxsw_sp_rif *ul_rif;

	ul_rif = mlxsw_sp_rif_by_index(mlxsw_sp, lb_rif->ul_rif_id);
	mlxsw_sp_rif_ipip_lb_op(lb_rif, 0, lb_rif->ul_rif_id, false);
	mlxsw_sp_ul_rif_put(ul_rif);
}

static const struct mlxsw_sp_rif_ops mlxsw_sp2_rif_ipip_lb_ops = {
	.type			= MLXSW_SP_RIF_TYPE_IPIP_LB,
	.rif_size		= sizeof(struct mlxsw_sp_rif_ipip_lb),
	.setup                  = mlxsw_sp_rif_ipip_lb_setup,
	.configure		= mlxsw_sp2_rif_ipip_lb_configure,
	.deconfigure		= mlxsw_sp2_rif_ipip_lb_deconfigure,
};

static const struct mlxsw_sp_rif_ops *mlxsw_sp2_rif_ops_arr[] = {
	[MLXSW_SP_RIF_TYPE_SUBPORT]	= &mlxsw_sp_rif_subport_ops,
	[MLXSW_SP_RIF_TYPE_VLAN]	= &mlxsw_sp2_rif_vlan_ops,
	[MLXSW_SP_RIF_TYPE_FID]		= &mlxsw_sp_rif_fid_ops,
	[MLXSW_SP_RIF_TYPE_IPIP_LB]	= &mlxsw_sp2_rif_ipip_lb_ops,
};

static int mlxsw_sp_rifs_table_init(struct mlxsw_sp *mlxsw_sp)
{
	struct gen_pool *rifs_table;
	int err;

	rifs_table = gen_pool_create(0, -1);
	if (!rifs_table)
		return -ENOMEM;

	gen_pool_set_algo(rifs_table, gen_pool_first_fit_order_align,
			  NULL);

	err = gen_pool_add(rifs_table, MLXSW_SP_ROUTER_GENALLOC_OFFSET,
			   MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS), -1);
	if (err)
		goto err_gen_pool_add;

	mlxsw_sp->router->rifs_table = rifs_table;

	return 0;

err_gen_pool_add:
	gen_pool_destroy(rifs_table);
	return err;
}

static void mlxsw_sp_rifs_table_fini(struct mlxsw_sp *mlxsw_sp)
{
	gen_pool_destroy(mlxsw_sp->router->rifs_table);
}

static int mlxsw_sp_rifs_init(struct mlxsw_sp *mlxsw_sp)
{
	u64 max_rifs = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS);
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	struct mlxsw_core *core = mlxsw_sp->core;
	int err;

	if (!MLXSW_CORE_RES_VALID(core, MAX_RIF_MAC_PROFILES))
		return -EIO;
	mlxsw_sp->router->max_rif_mac_profile =
		MLXSW_CORE_RES_GET(core, MAX_RIF_MAC_PROFILES);

	mlxsw_sp->router->rifs = kcalloc(max_rifs,
					 sizeof(struct mlxsw_sp_rif *),
					 GFP_KERNEL);
	if (!mlxsw_sp->router->rifs)
		return -ENOMEM;

	err = mlxsw_sp_rifs_table_init(mlxsw_sp);
	if (err)
		goto err_rifs_table_init;

	idr_init(&mlxsw_sp->router->rif_mac_profiles_idr);
	atomic_set(&mlxsw_sp->router->rif_mac_profiles_count, 0);
	atomic_set(&mlxsw_sp->router->rifs_count, 0);
	devl_resource_occ_get_register(devlink,
				       MLXSW_SP_RESOURCE_RIF_MAC_PROFILES,
				       mlxsw_sp_rif_mac_profiles_occ_get,
				       mlxsw_sp);
	devl_resource_occ_get_register(devlink,
				       MLXSW_SP_RESOURCE_RIFS,
				       mlxsw_sp_rifs_occ_get,
				       mlxsw_sp);

	return 0;

err_rifs_table_init:
	kfree(mlxsw_sp->router->rifs);
	return err;
}

static void mlxsw_sp_rifs_fini(struct mlxsw_sp *mlxsw_sp)
{
	int max_rifs = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS);
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	int i;

	WARN_ON_ONCE(atomic_read(&mlxsw_sp->router->rifs_count));
	for (i = 0; i < max_rifs; i++)
		WARN_ON_ONCE(mlxsw_sp->router->rifs[i]);

	devl_resource_occ_get_unregister(devlink, MLXSW_SP_RESOURCE_RIFS);
	devl_resource_occ_get_unregister(devlink,
					 MLXSW_SP_RESOURCE_RIF_MAC_PROFILES);
	WARN_ON(!idr_is_empty(&mlxsw_sp->router->rif_mac_profiles_idr));
	idr_destroy(&mlxsw_sp->router->rif_mac_profiles_idr);
	mlxsw_sp_rifs_table_fini(mlxsw_sp);
	kfree(mlxsw_sp->router->rifs);
}

static int
mlxsw_sp_ipip_config_tigcr(struct mlxsw_sp *mlxsw_sp)
{
	char tigcr_pl[MLXSW_REG_TIGCR_LEN];

	mlxsw_reg_tigcr_pack(tigcr_pl, true, 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(tigcr), tigcr_pl);
}

static int mlxsw_sp_ipips_init(struct mlxsw_sp *mlxsw_sp)
{
	int err;

	INIT_LIST_HEAD(&mlxsw_sp->router->ipip_list);

	err = mlxsw_sp_ipip_ecn_encap_init(mlxsw_sp);
	if (err)
		return err;
	err = mlxsw_sp_ipip_ecn_decap_init(mlxsw_sp);
	if (err)
		return err;

	return mlxsw_sp_ipip_config_tigcr(mlxsw_sp);
}

static int mlxsw_sp1_ipips_init(struct mlxsw_sp *mlxsw_sp)
{
	mlxsw_sp->router->ipip_ops_arr = mlxsw_sp1_ipip_ops_arr;
	return mlxsw_sp_ipips_init(mlxsw_sp);
}

static int mlxsw_sp2_ipips_init(struct mlxsw_sp *mlxsw_sp)
{
	mlxsw_sp->router->ipip_ops_arr = mlxsw_sp2_ipip_ops_arr;
	return mlxsw_sp_ipips_init(mlxsw_sp);
}

static void mlxsw_sp_ipips_fini(struct mlxsw_sp *mlxsw_sp)
{
	WARN_ON(!list_empty(&mlxsw_sp->router->ipip_list));
}

static void mlxsw_sp_router_fib_dump_flush(struct notifier_block *nb)
{
	struct mlxsw_sp_router *router;

	/* Flush pending FIB notifications and then flush the device's
	 * table before requesting another dump. The FIB notification
	 * block is unregistered, so no need to take RTNL.
	 */
	mlxsw_core_flush_owq();
	router = container_of(nb, struct mlxsw_sp_router, fib_nb);
	mlxsw_sp_router_fib_flush(router->mlxsw_sp);
}

#ifdef CONFIG_IP_ROUTE_MULTIPATH
struct mlxsw_sp_mp_hash_config {
	DECLARE_BITMAP(headers, __MLXSW_REG_RECR2_HEADER_CNT);
	DECLARE_BITMAP(fields, __MLXSW_REG_RECR2_FIELD_CNT);
	DECLARE_BITMAP(inner_headers, __MLXSW_REG_RECR2_HEADER_CNT);
	DECLARE_BITMAP(inner_fields, __MLXSW_REG_RECR2_INNER_FIELD_CNT);
	bool inc_parsing_depth;
};

#define MLXSW_SP_MP_HASH_HEADER_SET(_headers, _header) \
	bitmap_set(_headers, MLXSW_REG_RECR2_##_header, 1)

#define MLXSW_SP_MP_HASH_FIELD_SET(_fields, _field) \
	bitmap_set(_fields, MLXSW_REG_RECR2_##_field, 1)

#define MLXSW_SP_MP_HASH_FIELD_RANGE_SET(_fields, _field, _nr) \
	bitmap_set(_fields, MLXSW_REG_RECR2_##_field, _nr)

static void mlxsw_sp_mp_hash_inner_l3(struct mlxsw_sp_mp_hash_config *config)
{
	unsigned long *inner_headers = config->inner_headers;
	unsigned long *inner_fields = config->inner_fields;

	/* IPv4 inner */
	MLXSW_SP_MP_HASH_HEADER_SET(inner_headers, IPV4_EN_NOT_TCP_NOT_UDP);
	MLXSW_SP_MP_HASH_HEADER_SET(inner_headers, IPV4_EN_TCP_UDP);
	MLXSW_SP_MP_HASH_FIELD_RANGE_SET(inner_fields, INNER_IPV4_SIP0, 4);
	MLXSW_SP_MP_HASH_FIELD_RANGE_SET(inner_fields, INNER_IPV4_DIP0, 4);
	/* IPv6 inner */
	MLXSW_SP_MP_HASH_HEADER_SET(inner_headers, IPV6_EN_NOT_TCP_NOT_UDP);
	MLXSW_SP_MP_HASH_HEADER_SET(inner_headers, IPV6_EN_TCP_UDP);
	MLXSW_SP_MP_HASH_FIELD_SET(inner_fields, INNER_IPV6_SIP0_7);
	MLXSW_SP_MP_HASH_FIELD_RANGE_SET(inner_fields, INNER_IPV6_SIP8, 8);
	MLXSW_SP_MP_HASH_FIELD_SET(inner_fields, INNER_IPV6_DIP0_7);
	MLXSW_SP_MP_HASH_FIELD_RANGE_SET(inner_fields, INNER_IPV6_DIP8, 8);
	MLXSW_SP_MP_HASH_FIELD_SET(inner_fields, INNER_IPV6_NEXT_HEADER);
	MLXSW_SP_MP_HASH_FIELD_SET(inner_fields, INNER_IPV6_FLOW_LABEL);
}

static void mlxsw_sp_mp4_hash_outer_addr(struct mlxsw_sp_mp_hash_config *config)
{
	unsigned long *headers = config->headers;
	unsigned long *fields = config->fields;

	MLXSW_SP_MP_HASH_HEADER_SET(headers, IPV4_EN_NOT_TCP_NOT_UDP);
	MLXSW_SP_MP_HASH_HEADER_SET(headers, IPV4_EN_TCP_UDP);
	MLXSW_SP_MP_HASH_FIELD_RANGE_SET(fields, IPV4_SIP0, 4);
	MLXSW_SP_MP_HASH_FIELD_RANGE_SET(fields, IPV4_DIP0, 4);
}

static void
mlxsw_sp_mp_hash_inner_custom(struct mlxsw_sp_mp_hash_config *config,
			      u32 hash_fields)
{
	unsigned long *inner_headers = config->inner_headers;
	unsigned long *inner_fields = config->inner_fields;

	/* IPv4 Inner */
	MLXSW_SP_MP_HASH_HEADER_SET(inner_headers, IPV4_EN_NOT_TCP_NOT_UDP);
	MLXSW_SP_MP_HASH_HEADER_SET(inner_headers, IPV4_EN_TCP_UDP);
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_SRC_IP)
		MLXSW_SP_MP_HASH_FIELD_RANGE_SET(inner_fields, INNER_IPV4_SIP0, 4);
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_DST_IP)
		MLXSW_SP_MP_HASH_FIELD_RANGE_SET(inner_fields, INNER_IPV4_DIP0, 4);
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_IP_PROTO)
		MLXSW_SP_MP_HASH_FIELD_SET(inner_fields, INNER_IPV4_PROTOCOL);
	/* IPv6 inner */
	MLXSW_SP_MP_HASH_HEADER_SET(inner_headers, IPV6_EN_NOT_TCP_NOT_UDP);
	MLXSW_SP_MP_HASH_HEADER_SET(inner_headers, IPV6_EN_TCP_UDP);
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_SRC_IP) {
		MLXSW_SP_MP_HASH_FIELD_SET(inner_fields, INNER_IPV6_SIP0_7);
		MLXSW_SP_MP_HASH_FIELD_RANGE_SET(inner_fields, INNER_IPV6_SIP8, 8);
	}
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_DST_IP) {
		MLXSW_SP_MP_HASH_FIELD_SET(inner_fields, INNER_IPV6_DIP0_7);
		MLXSW_SP_MP_HASH_FIELD_RANGE_SET(inner_fields, INNER_IPV6_DIP8, 8);
	}
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_IP_PROTO)
		MLXSW_SP_MP_HASH_FIELD_SET(inner_fields, INNER_IPV6_NEXT_HEADER);
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_FLOWLABEL)
		MLXSW_SP_MP_HASH_FIELD_SET(inner_fields, INNER_IPV6_FLOW_LABEL);
	/* L4 inner */
	MLXSW_SP_MP_HASH_HEADER_SET(inner_headers, TCP_UDP_EN_IPV4);
	MLXSW_SP_MP_HASH_HEADER_SET(inner_headers, TCP_UDP_EN_IPV6);
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_SRC_PORT)
		MLXSW_SP_MP_HASH_FIELD_SET(inner_fields, INNER_TCP_UDP_SPORT);
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_DST_PORT)
		MLXSW_SP_MP_HASH_FIELD_SET(inner_fields, INNER_TCP_UDP_DPORT);
}

static void mlxsw_sp_mp4_hash_init(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_mp_hash_config *config)
{
	struct net *net = mlxsw_sp_net(mlxsw_sp);
	unsigned long *headers = config->headers;
	unsigned long *fields = config->fields;
	u32 hash_fields;

	switch (READ_ONCE(net->ipv4.sysctl_fib_multipath_hash_policy)) {
	case 0:
		mlxsw_sp_mp4_hash_outer_addr(config);
		break;
	case 1:
		mlxsw_sp_mp4_hash_outer_addr(config);
		MLXSW_SP_MP_HASH_HEADER_SET(headers, TCP_UDP_EN_IPV4);
		MLXSW_SP_MP_HASH_FIELD_SET(fields, IPV4_PROTOCOL);
		MLXSW_SP_MP_HASH_FIELD_SET(fields, TCP_UDP_SPORT);
		MLXSW_SP_MP_HASH_FIELD_SET(fields, TCP_UDP_DPORT);
		break;
	case 2:
		/* Outer */
		mlxsw_sp_mp4_hash_outer_addr(config);
		/* Inner */
		mlxsw_sp_mp_hash_inner_l3(config);
		break;
	case 3:
		hash_fields = READ_ONCE(net->ipv4.sysctl_fib_multipath_hash_fields);
		/* Outer */
		MLXSW_SP_MP_HASH_HEADER_SET(headers, IPV4_EN_NOT_TCP_NOT_UDP);
		MLXSW_SP_MP_HASH_HEADER_SET(headers, IPV4_EN_TCP_UDP);
		MLXSW_SP_MP_HASH_HEADER_SET(headers, TCP_UDP_EN_IPV4);
		if (hash_fields & FIB_MULTIPATH_HASH_FIELD_SRC_IP)
			MLXSW_SP_MP_HASH_FIELD_RANGE_SET(fields, IPV4_SIP0, 4);
		if (hash_fields & FIB_MULTIPATH_HASH_FIELD_DST_IP)
			MLXSW_SP_MP_HASH_FIELD_RANGE_SET(fields, IPV4_DIP0, 4);
		if (hash_fields & FIB_MULTIPATH_HASH_FIELD_IP_PROTO)
			MLXSW_SP_MP_HASH_FIELD_SET(fields, IPV4_PROTOCOL);
		if (hash_fields & FIB_MULTIPATH_HASH_FIELD_SRC_PORT)
			MLXSW_SP_MP_HASH_FIELD_SET(fields, TCP_UDP_SPORT);
		if (hash_fields & FIB_MULTIPATH_HASH_FIELD_DST_PORT)
			MLXSW_SP_MP_HASH_FIELD_SET(fields, TCP_UDP_DPORT);
		/* Inner */
		mlxsw_sp_mp_hash_inner_custom(config, hash_fields);
		break;
	}
}

static void mlxsw_sp_mp6_hash_outer_addr(struct mlxsw_sp_mp_hash_config *config)
{
	unsigned long *headers = config->headers;
	unsigned long *fields = config->fields;

	MLXSW_SP_MP_HASH_HEADER_SET(headers, IPV6_EN_NOT_TCP_NOT_UDP);
	MLXSW_SP_MP_HASH_HEADER_SET(headers, IPV6_EN_TCP_UDP);
	MLXSW_SP_MP_HASH_FIELD_SET(fields, IPV6_SIP0_7);
	MLXSW_SP_MP_HASH_FIELD_RANGE_SET(fields, IPV6_SIP8, 8);
	MLXSW_SP_MP_HASH_FIELD_SET(fields, IPV6_DIP0_7);
	MLXSW_SP_MP_HASH_FIELD_RANGE_SET(fields, IPV6_DIP8, 8);
}

static void mlxsw_sp_mp6_hash_init(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_mp_hash_config *config)
{
	u32 hash_fields = ip6_multipath_hash_fields(mlxsw_sp_net(mlxsw_sp));
	unsigned long *headers = config->headers;
	unsigned long *fields = config->fields;

	switch (ip6_multipath_hash_policy(mlxsw_sp_net(mlxsw_sp))) {
	case 0:
		mlxsw_sp_mp6_hash_outer_addr(config);
		MLXSW_SP_MP_HASH_FIELD_SET(fields, IPV6_NEXT_HEADER);
		MLXSW_SP_MP_HASH_FIELD_SET(fields, IPV6_FLOW_LABEL);
		break;
	case 1:
		mlxsw_sp_mp6_hash_outer_addr(config);
		MLXSW_SP_MP_HASH_HEADER_SET(headers, TCP_UDP_EN_IPV6);
		MLXSW_SP_MP_HASH_FIELD_SET(fields, IPV6_NEXT_HEADER);
		MLXSW_SP_MP_HASH_FIELD_SET(fields, TCP_UDP_SPORT);
		MLXSW_SP_MP_HASH_FIELD_SET(fields, TCP_UDP_DPORT);
		break;
	case 2:
		/* Outer */
		mlxsw_sp_mp6_hash_outer_addr(config);
		MLXSW_SP_MP_HASH_FIELD_SET(fields, IPV6_NEXT_HEADER);
		MLXSW_SP_MP_HASH_FIELD_SET(fields, IPV6_FLOW_LABEL);
		/* Inner */
		mlxsw_sp_mp_hash_inner_l3(config);
		config->inc_parsing_depth = true;
		break;
	case 3:
		/* Outer */
		MLXSW_SP_MP_HASH_HEADER_SET(headers, IPV6_EN_NOT_TCP_NOT_UDP);
		MLXSW_SP_MP_HASH_HEADER_SET(headers, IPV6_EN_TCP_UDP);
		MLXSW_SP_MP_HASH_HEADER_SET(headers, TCP_UDP_EN_IPV6);
		if (hash_fields & FIB_MULTIPATH_HASH_FIELD_SRC_IP) {
			MLXSW_SP_MP_HASH_FIELD_SET(fields, IPV6_SIP0_7);
			MLXSW_SP_MP_HASH_FIELD_RANGE_SET(fields, IPV6_SIP8, 8);
		}
		if (hash_fields & FIB_MULTIPATH_HASH_FIELD_DST_IP) {
			MLXSW_SP_MP_HASH_FIELD_SET(fields, IPV6_DIP0_7);
			MLXSW_SP_MP_HASH_FIELD_RANGE_SET(fields, IPV6_DIP8, 8);
		}
		if (hash_fields & FIB_MULTIPATH_HASH_FIELD_IP_PROTO)
			MLXSW_SP_MP_HASH_FIELD_SET(fields, IPV6_NEXT_HEADER);
		if (hash_fields & FIB_MULTIPATH_HASH_FIELD_FLOWLABEL)
			MLXSW_SP_MP_HASH_FIELD_SET(fields, IPV6_FLOW_LABEL);
		if (hash_fields & FIB_MULTIPATH_HASH_FIELD_SRC_PORT)
			MLXSW_SP_MP_HASH_FIELD_SET(fields, TCP_UDP_SPORT);
		if (hash_fields & FIB_MULTIPATH_HASH_FIELD_DST_PORT)
			MLXSW_SP_MP_HASH_FIELD_SET(fields, TCP_UDP_DPORT);
		/* Inner */
		mlxsw_sp_mp_hash_inner_custom(config, hash_fields);
		if (hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_MASK)
			config->inc_parsing_depth = true;
		break;
	}
}

static int mlxsw_sp_mp_hash_parsing_depth_adjust(struct mlxsw_sp *mlxsw_sp,
						 bool old_inc_parsing_depth,
						 bool new_inc_parsing_depth)
{
	int err;

	if (!old_inc_parsing_depth && new_inc_parsing_depth) {
		err = mlxsw_sp_parsing_depth_inc(mlxsw_sp);
		if (err)
			return err;
		mlxsw_sp->router->inc_parsing_depth = true;
	} else if (old_inc_parsing_depth && !new_inc_parsing_depth) {
		mlxsw_sp_parsing_depth_dec(mlxsw_sp);
		mlxsw_sp->router->inc_parsing_depth = false;
	}

	return 0;
}

static int mlxsw_sp_mp_hash_init(struct mlxsw_sp *mlxsw_sp)
{
	bool old_inc_parsing_depth, new_inc_parsing_depth;
	struct mlxsw_sp_mp_hash_config config = {};
	char recr2_pl[MLXSW_REG_RECR2_LEN];
	unsigned long bit;
	u32 seed;
	int err;

	seed = jhash(mlxsw_sp->base_mac, sizeof(mlxsw_sp->base_mac), 0);
	mlxsw_reg_recr2_pack(recr2_pl, seed);
	mlxsw_sp_mp4_hash_init(mlxsw_sp, &config);
	mlxsw_sp_mp6_hash_init(mlxsw_sp, &config);

	old_inc_parsing_depth = mlxsw_sp->router->inc_parsing_depth;
	new_inc_parsing_depth = config.inc_parsing_depth;
	err = mlxsw_sp_mp_hash_parsing_depth_adjust(mlxsw_sp,
						    old_inc_parsing_depth,
						    new_inc_parsing_depth);
	if (err)
		return err;

	for_each_set_bit(bit, config.headers, __MLXSW_REG_RECR2_HEADER_CNT)
		mlxsw_reg_recr2_outer_header_enables_set(recr2_pl, bit, 1);
	for_each_set_bit(bit, config.fields, __MLXSW_REG_RECR2_FIELD_CNT)
		mlxsw_reg_recr2_outer_header_fields_enable_set(recr2_pl, bit, 1);
	for_each_set_bit(bit, config.inner_headers, __MLXSW_REG_RECR2_HEADER_CNT)
		mlxsw_reg_recr2_inner_header_enables_set(recr2_pl, bit, 1);
	for_each_set_bit(bit, config.inner_fields, __MLXSW_REG_RECR2_INNER_FIELD_CNT)
		mlxsw_reg_recr2_inner_header_fields_enable_set(recr2_pl, bit, 1);

	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(recr2), recr2_pl);
	if (err)
		goto err_reg_write;

	return 0;

err_reg_write:
	mlxsw_sp_mp_hash_parsing_depth_adjust(mlxsw_sp, new_inc_parsing_depth,
					      old_inc_parsing_depth);
	return err;
}

static void mlxsw_sp_mp_hash_fini(struct mlxsw_sp *mlxsw_sp)
{
	bool old_inc_parsing_depth = mlxsw_sp->router->inc_parsing_depth;

	mlxsw_sp_mp_hash_parsing_depth_adjust(mlxsw_sp, old_inc_parsing_depth,
					      false);
}
#else
static int mlxsw_sp_mp_hash_init(struct mlxsw_sp *mlxsw_sp)
{
	return 0;
}

static void mlxsw_sp_mp_hash_fini(struct mlxsw_sp *mlxsw_sp)
{
}
#endif

static int mlxsw_sp_dscp_init(struct mlxsw_sp *mlxsw_sp)
{
	char rdpm_pl[MLXSW_REG_RDPM_LEN];
	unsigned int i;

	MLXSW_REG_ZERO(rdpm, rdpm_pl);

	/* HW is determining switch priority based on DSCP-bits, but the
	 * kernel is still doing that based on the ToS. Since there's a
	 * mismatch in bits we need to make sure to translate the right
	 * value ToS would observe, skipping the 2 least-significant ECN bits.
	 */
	for (i = 0; i < MLXSW_REG_RDPM_DSCP_ENTRY_REC_MAX_COUNT; i++)
		mlxsw_reg_rdpm_pack(rdpm_pl, i, rt_tos2priority(i << 2));

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rdpm), rdpm_pl);
}

static int __mlxsw_sp_router_init(struct mlxsw_sp *mlxsw_sp)
{
	struct net *net = mlxsw_sp_net(mlxsw_sp);
	char rgcr_pl[MLXSW_REG_RGCR_LEN];
	u64 max_rifs;
	bool usp;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, MAX_RIFS))
		return -EIO;
	max_rifs = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS);
	usp = READ_ONCE(net->ipv4.sysctl_ip_fwd_update_priority);

	mlxsw_reg_rgcr_pack(rgcr_pl, true, true);
	mlxsw_reg_rgcr_max_router_interfaces_set(rgcr_pl, max_rifs);
	mlxsw_reg_rgcr_usp_set(rgcr_pl, usp);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rgcr), rgcr_pl);
}

static void __mlxsw_sp_router_fini(struct mlxsw_sp *mlxsw_sp)
{
	char rgcr_pl[MLXSW_REG_RGCR_LEN];

	mlxsw_reg_rgcr_pack(rgcr_pl, false, false);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rgcr), rgcr_pl);
}

static int mlxsw_sp_lb_rif_init(struct mlxsw_sp *mlxsw_sp,
				struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_router *router = mlxsw_sp->router;
	struct mlxsw_sp_rif *lb_rif;
	int err;

	router->lb_crif = mlxsw_sp_crif_alloc(NULL);
	if (!router->lb_crif)
		return -ENOMEM;

	/* Create a generic loopback RIF associated with the main table
	 * (default VRF). Any table can be used, but the main table exists
	 * anyway, so we do not waste resources. Loopback RIFs are usually
	 * created with a NULL CRIF, but this RIF is used as a fallback RIF
	 * for blackhole nexthops, and nexthops expect to have a valid CRIF.
	 */
	lb_rif = mlxsw_sp_ul_rif_get(mlxsw_sp, RT_TABLE_MAIN, router->lb_crif,
				     extack);
	if (IS_ERR(lb_rif)) {
		err = PTR_ERR(lb_rif);
		goto err_ul_rif_get;
	}

	return 0;

err_ul_rif_get:
	mlxsw_sp_crif_free(router->lb_crif);
	return err;
}

static void mlxsw_sp_lb_rif_fini(struct mlxsw_sp *mlxsw_sp)
{
	mlxsw_sp_ul_rif_put(mlxsw_sp->router->lb_crif->rif);
	mlxsw_sp_crif_free(mlxsw_sp->router->lb_crif);
}

static int mlxsw_sp1_router_init(struct mlxsw_sp *mlxsw_sp)
{
	size_t size_ranges_count = ARRAY_SIZE(mlxsw_sp1_adj_grp_size_ranges);

	mlxsw_sp->router->rif_ops_arr = mlxsw_sp1_rif_ops_arr;
	mlxsw_sp->router->adj_grp_size_ranges = mlxsw_sp1_adj_grp_size_ranges;
	mlxsw_sp->router->adj_grp_size_ranges_count = size_ranges_count;

	return 0;
}

const struct mlxsw_sp_router_ops mlxsw_sp1_router_ops = {
	.init = mlxsw_sp1_router_init,
	.ipips_init = mlxsw_sp1_ipips_init,
};

static int mlxsw_sp2_router_init(struct mlxsw_sp *mlxsw_sp)
{
	size_t size_ranges_count = ARRAY_SIZE(mlxsw_sp2_adj_grp_size_ranges);

	mlxsw_sp->router->rif_ops_arr = mlxsw_sp2_rif_ops_arr;
	mlxsw_sp->router->adj_grp_size_ranges = mlxsw_sp2_adj_grp_size_ranges;
	mlxsw_sp->router->adj_grp_size_ranges_count = size_ranges_count;

	return 0;
}

const struct mlxsw_sp_router_ops mlxsw_sp2_router_ops = {
	.init = mlxsw_sp2_router_init,
	.ipips_init = mlxsw_sp2_ipips_init,
};

int mlxsw_sp_router_init(struct mlxsw_sp *mlxsw_sp,
			 struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_router *router;
	struct notifier_block *nb;
	int err;

	router = kzalloc(sizeof(*mlxsw_sp->router), GFP_KERNEL);
	if (!router)
		return -ENOMEM;
	mutex_init(&router->lock);
	mlxsw_sp->router = router;
	router->mlxsw_sp = mlxsw_sp;

	err = mlxsw_sp->router_ops->init(mlxsw_sp);
	if (err)
		goto err_router_ops_init;

	INIT_LIST_HEAD(&mlxsw_sp->router->nh_res_grp_list);
	INIT_DELAYED_WORK(&mlxsw_sp->router->nh_grp_activity_dw,
			  mlxsw_sp_nh_grp_activity_work);
	INIT_LIST_HEAD(&mlxsw_sp->router->nexthop_neighs_list);
	err = __mlxsw_sp_router_init(mlxsw_sp);
	if (err)
		goto err_router_init;

	err = mlxsw_sp->router_ops->ipips_init(mlxsw_sp);
	if (err)
		goto err_ipips_init;

	err = rhashtable_init(&mlxsw_sp->router->crif_ht,
			      &mlxsw_sp_crif_ht_params);
	if (err)
		goto err_crif_ht_init;

	err = mlxsw_sp_rifs_init(mlxsw_sp);
	if (err)
		goto err_rifs_init;

	err = rhashtable_init(&mlxsw_sp->router->nexthop_ht,
			      &mlxsw_sp_nexthop_ht_params);
	if (err)
		goto err_nexthop_ht_init;

	err = rhashtable_init(&mlxsw_sp->router->nexthop_group_ht,
			      &mlxsw_sp_nexthop_group_ht_params);
	if (err)
		goto err_nexthop_group_ht_init;

	INIT_LIST_HEAD(&mlxsw_sp->router->nexthop_list);
	err = mlxsw_sp_lpm_init(mlxsw_sp);
	if (err)
		goto err_lpm_init;

	err = mlxsw_sp_mr_init(mlxsw_sp, &mlxsw_sp_mr_tcam_ops);
	if (err)
		goto err_mr_init;

	err = mlxsw_sp_vrs_init(mlxsw_sp);
	if (err)
		goto err_vrs_init;

	err = mlxsw_sp_lb_rif_init(mlxsw_sp, extack);
	if (err)
		goto err_lb_rif_init;

	err = mlxsw_sp_neigh_init(mlxsw_sp);
	if (err)
		goto err_neigh_init;

	err = mlxsw_sp_mp_hash_init(mlxsw_sp);
	if (err)
		goto err_mp_hash_init;

	err = mlxsw_sp_dscp_init(mlxsw_sp);
	if (err)
		goto err_dscp_init;

	router->inetaddr_nb.notifier_call = mlxsw_sp_inetaddr_event;
	err = register_inetaddr_notifier(&router->inetaddr_nb);
	if (err)
		goto err_register_inetaddr_notifier;

	router->inet6addr_nb.notifier_call = mlxsw_sp_inet6addr_event;
	err = register_inet6addr_notifier(&router->inet6addr_nb);
	if (err)
		goto err_register_inet6addr_notifier;

	router->inetaddr_valid_nb.notifier_call = mlxsw_sp_inetaddr_valid_event;
	err = register_inetaddr_validator_notifier(&router->inetaddr_valid_nb);
	if (err)
		goto err_register_inetaddr_valid_notifier;

	nb = &router->inet6addr_valid_nb;
	nb->notifier_call = mlxsw_sp_inet6addr_valid_event;
	err = register_inet6addr_validator_notifier(nb);
	if (err)
		goto err_register_inet6addr_valid_notifier;

	mlxsw_sp->router->netevent_nb.notifier_call =
		mlxsw_sp_router_netevent_event;
	err = register_netevent_notifier(&mlxsw_sp->router->netevent_nb);
	if (err)
		goto err_register_netevent_notifier;

	mlxsw_sp->router->nexthop_nb.notifier_call =
		mlxsw_sp_nexthop_obj_event;
	err = register_nexthop_notifier(mlxsw_sp_net(mlxsw_sp),
					&mlxsw_sp->router->nexthop_nb,
					extack);
	if (err)
		goto err_register_nexthop_notifier;

	mlxsw_sp->router->fib_nb.notifier_call = mlxsw_sp_router_fib_event;
	err = register_fib_notifier(mlxsw_sp_net(mlxsw_sp),
				    &mlxsw_sp->router->fib_nb,
				    mlxsw_sp_router_fib_dump_flush, extack);
	if (err)
		goto err_register_fib_notifier;

	mlxsw_sp->router->netdevice_nb.notifier_call =
		mlxsw_sp_router_netdevice_event;
	err = register_netdevice_notifier_net(mlxsw_sp_net(mlxsw_sp),
					      &mlxsw_sp->router->netdevice_nb);
	if (err)
		goto err_register_netdev_notifier;

	return 0;

err_register_netdev_notifier:
	unregister_fib_notifier(mlxsw_sp_net(mlxsw_sp),
				&mlxsw_sp->router->fib_nb);
err_register_fib_notifier:
	unregister_nexthop_notifier(mlxsw_sp_net(mlxsw_sp),
				    &mlxsw_sp->router->nexthop_nb);
err_register_nexthop_notifier:
	unregister_netevent_notifier(&mlxsw_sp->router->netevent_nb);
err_register_netevent_notifier:
	unregister_inet6addr_validator_notifier(&router->inet6addr_valid_nb);
err_register_inet6addr_valid_notifier:
	unregister_inetaddr_validator_notifier(&router->inetaddr_valid_nb);
err_register_inetaddr_valid_notifier:
	unregister_inet6addr_notifier(&router->inet6addr_nb);
err_register_inet6addr_notifier:
	unregister_inetaddr_notifier(&router->inetaddr_nb);
err_register_inetaddr_notifier:
	mlxsw_core_flush_owq();
err_dscp_init:
	mlxsw_sp_mp_hash_fini(mlxsw_sp);
err_mp_hash_init:
	mlxsw_sp_neigh_fini(mlxsw_sp);
err_neigh_init:
	mlxsw_sp_lb_rif_fini(mlxsw_sp);
err_lb_rif_init:
	mlxsw_sp_vrs_fini(mlxsw_sp);
err_vrs_init:
	mlxsw_sp_mr_fini(mlxsw_sp);
err_mr_init:
	mlxsw_sp_lpm_fini(mlxsw_sp);
err_lpm_init:
	rhashtable_destroy(&mlxsw_sp->router->nexthop_group_ht);
err_nexthop_group_ht_init:
	rhashtable_destroy(&mlxsw_sp->router->nexthop_ht);
err_nexthop_ht_init:
	mlxsw_sp_rifs_fini(mlxsw_sp);
err_rifs_init:
	rhashtable_destroy(&mlxsw_sp->router->crif_ht);
err_crif_ht_init:
	mlxsw_sp_ipips_fini(mlxsw_sp);
err_ipips_init:
	__mlxsw_sp_router_fini(mlxsw_sp);
err_router_init:
	cancel_delayed_work_sync(&mlxsw_sp->router->nh_grp_activity_dw);
err_router_ops_init:
	mutex_destroy(&mlxsw_sp->router->lock);
	kfree(mlxsw_sp->router);
	return err;
}

void mlxsw_sp_router_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_router *router = mlxsw_sp->router;

	unregister_netdevice_notifier_net(mlxsw_sp_net(mlxsw_sp),
					  &router->netdevice_nb);
	unregister_fib_notifier(mlxsw_sp_net(mlxsw_sp), &router->fib_nb);
	unregister_nexthop_notifier(mlxsw_sp_net(mlxsw_sp),
				    &router->nexthop_nb);
	unregister_netevent_notifier(&router->netevent_nb);
	unregister_inet6addr_validator_notifier(&router->inet6addr_valid_nb);
	unregister_inetaddr_validator_notifier(&router->inetaddr_valid_nb);
	unregister_inet6addr_notifier(&router->inet6addr_nb);
	unregister_inetaddr_notifier(&router->inetaddr_nb);
	mlxsw_core_flush_owq();
	mlxsw_sp_mp_hash_fini(mlxsw_sp);
	mlxsw_sp_neigh_fini(mlxsw_sp);
	mlxsw_sp_lb_rif_fini(mlxsw_sp);
	mlxsw_sp_vrs_fini(mlxsw_sp);
	mlxsw_sp_mr_fini(mlxsw_sp);
	mlxsw_sp_lpm_fini(mlxsw_sp);
	rhashtable_destroy(&router->nexthop_group_ht);
	rhashtable_destroy(&router->nexthop_ht);
	mlxsw_sp_rifs_fini(mlxsw_sp);
	rhashtable_destroy(&mlxsw_sp->router->crif_ht);
	mlxsw_sp_ipips_fini(mlxsw_sp);
	__mlxsw_sp_router_fini(mlxsw_sp);
	cancel_delayed_work_sync(&router->nh_grp_activity_dw);
	mutex_destroy(&router->lock);
	kfree(router);
}
