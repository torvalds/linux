/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_router.c
 * Copyright (c) 2016-2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2016 Ido Schimmel <idosch@mellanox.com>
 * Copyright (c) 2016 Yotam Gigi <yotamg@mellanox.com>
 * Copyright (c) 2017 Petr Machata <petrm@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

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
#include <net/netevent.h>
#include <net/neighbour.h>
#include <net/arp.h>
#include <net/ip_fib.h>
#include <net/ip6_fib.h>
#include <net/fib_rules.h>
#include <net/ip_tunnels.h>
#include <net/l3mdev.h>
#include <net/addrconf.h>
#include <net/ndisc.h>
#include <net/ipv6.h>
#include <net/fib_notifier.h>

#include "spectrum.h"
#include "core.h"
#include "reg.h"
#include "spectrum_cnt.h"
#include "spectrum_dpipe.h"
#include "spectrum_ipip.h"
#include "spectrum_router.h"

struct mlxsw_sp_vr;
struct mlxsw_sp_lpm_tree;
struct mlxsw_sp_rif_ops;

struct mlxsw_sp_router {
	struct mlxsw_sp *mlxsw_sp;
	struct mlxsw_sp_rif **rifs;
	struct mlxsw_sp_vr *vrs;
	struct rhashtable neigh_ht;
	struct rhashtable nexthop_group_ht;
	struct rhashtable nexthop_ht;
	struct {
		struct mlxsw_sp_lpm_tree *trees;
		unsigned int tree_count;
	} lpm;
	struct {
		struct delayed_work dw;
		unsigned long interval;	/* ms */
	} neighs_update;
	struct delayed_work nexthop_probe_dw;
#define MLXSW_SP_UNRESOLVED_NH_PROBE_INTERVAL 5000 /* ms */
	struct list_head nexthop_neighs_list;
	struct list_head ipip_list;
	bool aborted;
	struct notifier_block fib_nb;
	const struct mlxsw_sp_rif_ops **rif_ops_arr;
	const struct mlxsw_sp_ipip_ops **ipip_ops_arr;
};

struct mlxsw_sp_rif {
	struct list_head nexthop_list;
	struct list_head neigh_list;
	struct net_device *dev;
	struct mlxsw_sp_fid *fid;
	unsigned char addr[ETH_ALEN];
	int mtu;
	u16 rif_index;
	u16 vr_id;
	const struct mlxsw_sp_rif_ops *ops;
	struct mlxsw_sp *mlxsw_sp;

	unsigned int counter_ingress;
	bool counter_ingress_valid;
	unsigned int counter_egress;
	bool counter_egress_valid;
};

struct mlxsw_sp_rif_params {
	struct net_device *dev;
	union {
		u16 system_port;
		u16 lag_id;
	};
	u16 vid;
	bool lag;
};

struct mlxsw_sp_rif_subport {
	struct mlxsw_sp_rif common;
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
	u16 ul_vr_id; /* Reserved for Spectrum-2. */
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
	int (*configure)(struct mlxsw_sp_rif *rif);
	void (*deconfigure)(struct mlxsw_sp_rif *rif);
	struct mlxsw_sp_fid * (*fid_get)(struct mlxsw_sp_rif *rif);
};

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

static int mlxsw_sp_rif_counter_clear(struct mlxsw_sp *mlxsw_sp,
				      unsigned int counter_index)
{
	char ricnt_pl[MLXSW_REG_RICNT_LEN];

	mlxsw_reg_ricnt_pack(ricnt_pl, counter_index,
			     MLXSW_REG_RICNT_OPCODE_CLEAR);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ricnt), ricnt_pl);
}

int mlxsw_sp_rif_counter_alloc(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_rif *rif,
			       enum mlxsw_sp_rif_counter_dir dir)
{
	unsigned int *p_counter_index;
	int err;

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

void mlxsw_sp_rif_counter_free(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_rif *rif,
			       enum mlxsw_sp_rif_counter_dir dir)
{
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
	mlxsw_sp_rif_counter_alloc(mlxsw_sp, rif, MLXSW_SP_RIF_COUNTER_EGRESS);
}

static void mlxsw_sp_rif_counters_free(struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;

	mlxsw_sp_rif_counter_free(mlxsw_sp, rif, MLXSW_SP_RIF_COUNTER_EGRESS);
}

static struct mlxsw_sp_rif *
mlxsw_sp_rif_find_by_dev(const struct mlxsw_sp *mlxsw_sp,
			 const struct net_device *dev);

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

static bool
mlxsw_sp_prefix_usage_none(struct mlxsw_sp_prefix_usage *prefix_usage)
{
	struct mlxsw_sp_prefix_usage prefix_usage_none = {{ 0 } };

	return mlxsw_sp_prefix_usage_eq(prefix_usage, &prefix_usage_none);
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

	/* This is a special case of local delivery, where a packet should be
	 * decapsulated on reception. Note that there is no corresponding ENCAP,
	 * because that's a type of next hop, not of FIB entry. (There can be
	 * several next hops in a REMOTE entry, and some of them may be
	 * encapsulating entries.)
	 */
	MLXSW_SP_FIB_ENTRY_TYPE_IPIP_DECAP,
};

struct mlxsw_sp_nexthop_group;
struct mlxsw_sp_fib;

struct mlxsw_sp_fib_node {
	struct list_head entry_list;
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
	struct list_head list;
	struct mlxsw_sp_fib_node *fib_node;
	enum mlxsw_sp_fib_entry_type type;
	struct list_head nexthop_group_node;
	struct mlxsw_sp_nexthop_group *nh_group;
	struct mlxsw_sp_fib_entry_decap decap; /* Valid for decap entries. */
};

struct mlxsw_sp_fib4_entry {
	struct mlxsw_sp_fib_entry common;
	u32 tb_id;
	u32 prio;
	u8 tos;
	u8 type;
};

struct mlxsw_sp_fib6_entry {
	struct mlxsw_sp_fib_entry common;
	struct list_head rt6_list;
	unsigned int nrt6;
};

struct mlxsw_sp_rt6 {
	struct list_head list;
	struct rt6_info *rt;
};

struct mlxsw_sp_lpm_tree {
	u8 id; /* tree ID */
	unsigned int ref_count;
	enum mlxsw_sp_l3proto proto;
	struct mlxsw_sp_prefix_usage prefix_usage;
};

struct mlxsw_sp_fib {
	struct rhashtable ht;
	struct list_head node_list;
	struct mlxsw_sp_vr *vr;
	struct mlxsw_sp_lpm_tree *lpm_tree;
	unsigned long prefix_ref_count[MLXSW_SP_PREFIX_COUNT];
	struct mlxsw_sp_prefix_usage prefix_usage;
	enum mlxsw_sp_l3proto proto;
};

struct mlxsw_sp_vr {
	u16 id; /* virtual router ID */
	u32 tb_id; /* kernel fib table id */
	unsigned int rif_count;
	struct mlxsw_sp_fib *fib4;
	struct mlxsw_sp_fib *fib6;
};

static const struct rhashtable_params mlxsw_sp_fib_ht_params;

static struct mlxsw_sp_fib *mlxsw_sp_fib_create(struct mlxsw_sp_vr *vr,
						enum mlxsw_sp_l3proto proto)
{
	struct mlxsw_sp_fib *fib;
	int err;

	fib = kzalloc(sizeof(*fib), GFP_KERNEL);
	if (!fib)
		return ERR_PTR(-ENOMEM);
	err = rhashtable_init(&fib->ht, &mlxsw_sp_fib_ht_params);
	if (err)
		goto err_rhashtable_init;
	INIT_LIST_HEAD(&fib->node_list);
	fib->proto = proto;
	fib->vr = vr;
	return fib;

err_rhashtable_init:
	kfree(fib);
	return ERR_PTR(err);
}

static void mlxsw_sp_fib_destroy(struct mlxsw_sp_fib *fib)
{
	WARN_ON(!list_empty(&fib->node_list));
	WARN_ON(fib->lpm_tree);
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
					     prefix_usage))
			return lpm_tree;
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
	struct mlxsw_sp_lpm_tree *lpm_tree;
	u64 max_trees;
	int i;

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

	return 0;
}

static void mlxsw_sp_lpm_fini(struct mlxsw_sp *mlxsw_sp)
{
	kfree(mlxsw_sp->router->lpm.trees);
}

static bool mlxsw_sp_vr_is_used(const struct mlxsw_sp_vr *vr)
{
	return !!vr->fib4 || !!vr->fib6;
}

static struct mlxsw_sp_vr *mlxsw_sp_vr_find_unused(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_vr *vr;
	int i;

	for (i = 0; i < MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_VRS); i++) {
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
	/* For our purpose, squash main and local table into one */
	if (tb_id == RT_TABLE_LOCAL)
		tb_id = RT_TABLE_MAIN;
	return tb_id;
}

static struct mlxsw_sp_vr *mlxsw_sp_vr_find(struct mlxsw_sp *mlxsw_sp,
					    u32 tb_id)
{
	struct mlxsw_sp_vr *vr;
	int i;

	tb_id = mlxsw_sp_fix_tb_id(tb_id);

	for (i = 0; i < MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_VRS); i++) {
		vr = &mlxsw_sp->router->vrs[i];
		if (mlxsw_sp_vr_is_used(vr) && vr->tb_id == tb_id)
			return vr;
	}
	return NULL;
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
					      u32 tb_id)
{
	struct mlxsw_sp_fib *fib4;
	struct mlxsw_sp_fib *fib6;
	struct mlxsw_sp_vr *vr;
	int err;

	vr = mlxsw_sp_vr_find_unused(mlxsw_sp);
	if (!vr)
		return ERR_PTR(-EBUSY);
	fib4 = mlxsw_sp_fib_create(vr, MLXSW_SP_L3_PROTO_IPV4);
	if (IS_ERR(fib4))
		return ERR_CAST(fib4);
	fib6 = mlxsw_sp_fib_create(vr, MLXSW_SP_L3_PROTO_IPV6);
	if (IS_ERR(fib6)) {
		err = PTR_ERR(fib6);
		goto err_fib6_create;
	}
	vr->fib4 = fib4;
	vr->fib6 = fib6;
	vr->tb_id = tb_id;
	return vr;

err_fib6_create:
	mlxsw_sp_fib_destroy(fib4);
	return ERR_PTR(err);
}

static void mlxsw_sp_vr_destroy(struct mlxsw_sp_vr *vr)
{
	mlxsw_sp_fib_destroy(vr->fib6);
	vr->fib6 = NULL;
	mlxsw_sp_fib_destroy(vr->fib4);
	vr->fib4 = NULL;
}

static struct mlxsw_sp_vr *mlxsw_sp_vr_get(struct mlxsw_sp *mlxsw_sp, u32 tb_id)
{
	struct mlxsw_sp_vr *vr;

	tb_id = mlxsw_sp_fix_tb_id(tb_id);
	vr = mlxsw_sp_vr_find(mlxsw_sp, tb_id);
	if (!vr)
		vr = mlxsw_sp_vr_create(mlxsw_sp, tb_id);
	return vr;
}

static void mlxsw_sp_vr_put(struct mlxsw_sp_vr *vr)
{
	if (!vr->rif_count && list_empty(&vr->fib4->node_list) &&
	    list_empty(&vr->fib6->node_list))
		mlxsw_sp_vr_destroy(vr);
}

static bool
mlxsw_sp_vr_lpm_tree_should_replace(struct mlxsw_sp_vr *vr,
				    enum mlxsw_sp_l3proto proto, u8 tree_id)
{
	struct mlxsw_sp_fib *fib = mlxsw_sp_vr_fib(vr, proto);

	if (!mlxsw_sp_vr_is_used(vr))
		return false;
	if (fib->lpm_tree && fib->lpm_tree->id == tree_id)
		return true;
	return false;
}

static int mlxsw_sp_vr_lpm_tree_replace(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib *fib,
					struct mlxsw_sp_lpm_tree *new_tree)
{
	struct mlxsw_sp_lpm_tree *old_tree = fib->lpm_tree;
	int err;

	err = mlxsw_sp_vr_lpm_tree_bind(mlxsw_sp, fib, new_tree->id);
	if (err)
		return err;
	fib->lpm_tree = new_tree;
	mlxsw_sp_lpm_tree_hold(new_tree);
	mlxsw_sp_lpm_tree_put(mlxsw_sp, old_tree);
	return 0;
}

static int mlxsw_sp_vrs_lpm_tree_replace(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_fib *fib,
					 struct mlxsw_sp_lpm_tree *new_tree)
{
	struct mlxsw_sp_lpm_tree *old_tree = fib->lpm_tree;
	enum mlxsw_sp_l3proto proto = fib->proto;
	u8 old_id, new_id = new_tree->id;
	struct mlxsw_sp_vr *vr;
	int i, err;

	if (!old_tree)
		goto no_replace;
	old_id = old_tree->id;

	for (i = 0; i < MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_VRS); i++) {
		vr = &mlxsw_sp->router->vrs[i];
		if (!mlxsw_sp_vr_lpm_tree_should_replace(vr, proto, old_id))
			continue;
		err = mlxsw_sp_vr_lpm_tree_replace(mlxsw_sp,
						   mlxsw_sp_vr_fib(vr, proto),
						   new_tree);
		if (err)
			goto err_tree_replace;
	}

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

no_replace:
	err = mlxsw_sp_vr_lpm_tree_bind(mlxsw_sp, fib, new_tree->id);
	if (err)
		return err;
	fib->lpm_tree = new_tree;
	mlxsw_sp_lpm_tree_hold(new_tree);
	return 0;
}

static void
mlxsw_sp_vrs_prefixes(struct mlxsw_sp *mlxsw_sp,
		      enum mlxsw_sp_l3proto proto,
		      struct mlxsw_sp_prefix_usage *req_prefix_usage)
{
	int i;

	for (i = 0; i < MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_VRS); i++) {
		struct mlxsw_sp_vr *vr = &mlxsw_sp->router->vrs[i];
		struct mlxsw_sp_fib *fib = mlxsw_sp_vr_fib(vr, proto);
		unsigned char prefix;

		if (!mlxsw_sp_vr_is_used(vr))
			continue;
		mlxsw_sp_prefix_usage_for_each(prefix, &fib->prefix_usage)
			mlxsw_sp_prefix_usage_set(req_prefix_usage, prefix);
	}
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

static struct net_device *
__mlxsw_sp_ipip_netdev_ul_dev_get(const struct net_device *ol_dev)
{
	struct ip_tunnel *tun = netdev_priv(ol_dev);
	struct net *net = dev_net(ol_dev);

	return __dev_get_by_index(net, tun->parms.link);
}

static u32 mlxsw_sp_ipip_dev_ul_tb_id(const struct net_device *ol_dev)
{
	struct net_device *d = __mlxsw_sp_ipip_netdev_ul_dev_get(ol_dev);

	if (d)
		return l3mdev_fib_table(d) ? : RT_TABLE_MAIN;
	else
		return l3mdev_fib_table(ol_dev) ? : RT_TABLE_MAIN;
}

static struct mlxsw_sp_rif *
mlxsw_sp_rif_create(struct mlxsw_sp *mlxsw_sp,
		    const struct mlxsw_sp_rif_params *params);

static struct mlxsw_sp_rif_ipip_lb *
mlxsw_sp_ipip_ol_ipip_lb_create(struct mlxsw_sp *mlxsw_sp,
				enum mlxsw_sp_ipip_type ipipt,
				struct net_device *ol_dev)
{
	struct mlxsw_sp_rif_params_ipip_lb lb_params;
	const struct mlxsw_sp_ipip_ops *ipip_ops;
	struct mlxsw_sp_rif *rif;

	ipip_ops = mlxsw_sp->router->ipip_ops_arr[ipipt];
	lb_params = (struct mlxsw_sp_rif_params_ipip_lb) {
		.common.dev = ol_dev,
		.common.lag = false,
		.lb_config = ipip_ops->ol_loopback_config(mlxsw_sp, ol_dev),
	};

	rif = mlxsw_sp_rif_create(mlxsw_sp, &lb_params.common);
	if (IS_ERR(rif))
		return ERR_CAST(rif);
	return container_of(rif, struct mlxsw_sp_rif_ipip_lb, common);
}

static struct mlxsw_sp_ipip_entry *
mlxsw_sp_ipip_entry_alloc(struct mlxsw_sp *mlxsw_sp,
			  enum mlxsw_sp_ipip_type ipipt,
			  struct net_device *ol_dev)
{
	struct mlxsw_sp_ipip_entry *ipip_entry;
	struct mlxsw_sp_ipip_entry *ret = NULL;

	ipip_entry = kzalloc(sizeof(*ipip_entry), GFP_KERNEL);
	if (!ipip_entry)
		return ERR_PTR(-ENOMEM);

	ipip_entry->ol_lb = mlxsw_sp_ipip_ol_ipip_lb_create(mlxsw_sp, ipipt,
							    ol_dev);
	if (IS_ERR(ipip_entry->ol_lb)) {
		ret = ERR_CAST(ipip_entry->ol_lb);
		goto err_ol_ipip_lb_create;
	}

	ipip_entry->ipipt = ipipt;
	ipip_entry->ol_dev = ol_dev;

	return ipip_entry;

err_ol_ipip_lb_create:
	kfree(ipip_entry);
	return ret;
}

static void
mlxsw_sp_ipip_entry_destroy(struct mlxsw_sp_ipip_entry *ipip_entry)
{
	WARN_ON(ipip_entry->ref_count > 0);
	mlxsw_sp_rif_destroy(&ipip_entry->ol_lb->common);
	kfree(ipip_entry);
}

static __be32
mlxsw_sp_ipip_netdev_saddr4(const struct net_device *ol_dev)
{
	struct ip_tunnel *tun = netdev_priv(ol_dev);

	return tun->parms.iph.saddr;
}

union mlxsw_sp_l3addr
mlxsw_sp_ipip_netdev_saddr(enum mlxsw_sp_l3proto proto,
			   const struct net_device *ol_dev)
{
	switch (proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		return (union mlxsw_sp_l3addr) {
			.addr4 = mlxsw_sp_ipip_netdev_saddr4(ol_dev),
		};
	case MLXSW_SP_L3_PROTO_IPV6:
		break;
	};

	WARN_ON(1);
	return (union mlxsw_sp_l3addr) {
		.addr4 = 0,
	};
}

__be32 mlxsw_sp_ipip_netdev_daddr4(const struct net_device *ol_dev)
{
	struct ip_tunnel *tun = netdev_priv(ol_dev);

	return tun->parms.iph.daddr;
}

union mlxsw_sp_l3addr
mlxsw_sp_ipip_netdev_daddr(enum mlxsw_sp_l3proto proto,
			   const struct net_device *ol_dev)
{
	switch (proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		return (union mlxsw_sp_l3addr) {
			.addr4 = mlxsw_sp_ipip_netdev_daddr4(ol_dev),
		};
	case MLXSW_SP_L3_PROTO_IPV6:
		break;
	};

	WARN_ON(1);
	return (union mlxsw_sp_l3addr) {
		.addr4 = 0,
	};
}

static bool mlxsw_sp_l3addr_eq(const union mlxsw_sp_l3addr *addr1,
			       const union mlxsw_sp_l3addr *addr2)
{
	return !memcmp(addr1, addr2, sizeof(*addr1));
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

static int
mlxsw_sp_fib_entry_decap_init(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_fib_entry *fib_entry,
			      struct mlxsw_sp_ipip_entry *ipip_entry)
{
	u32 tunnel_index;
	int err;

	err = mlxsw_sp_kvdl_alloc(mlxsw_sp, 1, &tunnel_index);
	if (err)
		return err;

	ipip_entry->decap_fib_entry = fib_entry;
	fib_entry->decap.ipip_entry = ipip_entry;
	fib_entry->decap.tunnel_index = tunnel_index;
	return 0;
}

static void mlxsw_sp_fib_entry_decap_fini(struct mlxsw_sp *mlxsw_sp,
					  struct mlxsw_sp_fib_entry *fib_entry)
{
	/* Unlink this node from the IPIP entry that it's the decap entry of. */
	fib_entry->decap.ipip_entry->decap_fib_entry = NULL;
	fib_entry->decap.ipip_entry = NULL;
	mlxsw_sp_kvdl_free(mlxsw_sp, fib_entry->decap.tunnel_index);
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

/* Given an IPIP entry, find the corresponding decap route. */
static struct mlxsw_sp_fib_entry *
mlxsw_sp_ipip_entry_find_decap(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_ipip_entry *ipip_entry)
{
	static struct mlxsw_sp_fib_node *fib_node;
	const struct mlxsw_sp_ipip_ops *ipip_ops;
	struct mlxsw_sp_fib_entry *fib_entry;
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
		WARN_ON(1);
		return NULL;
	}

	fib_node = mlxsw_sp_fib_node_lookup(ul_fib, saddrp, saddr_len,
					    saddr_prefix_len);
	if (!fib_node || list_empty(&fib_node->entry_list))
		return NULL;

	fib_entry = list_first_entry(&fib_node->entry_list,
				     struct mlxsw_sp_fib_entry, list);
	if (fib_entry->type != MLXSW_SP_FIB_ENTRY_TYPE_TRAP)
		return NULL;

	return fib_entry;
}

static struct mlxsw_sp_ipip_entry *
mlxsw_sp_ipip_entry_get(struct mlxsw_sp *mlxsw_sp,
			enum mlxsw_sp_ipip_type ipipt,
			struct net_device *ol_dev)
{
	u32 ul_tb_id = mlxsw_sp_ipip_dev_ul_tb_id(ol_dev);
	struct mlxsw_sp_router *router = mlxsw_sp->router;
	struct mlxsw_sp_fib_entry *decap_fib_entry;
	struct mlxsw_sp_ipip_entry *ipip_entry;
	enum mlxsw_sp_l3proto ul_proto;
	union mlxsw_sp_l3addr saddr;

	list_for_each_entry(ipip_entry, &mlxsw_sp->router->ipip_list,
			    ipip_list_node) {
		if (ipip_entry->ol_dev == ol_dev)
			goto inc_ref_count;

		/* The configuration where several tunnels have the same local
		 * address in the same underlay table needs special treatment in
		 * the HW. That is currently not implemented in the driver.
		 */
		ul_proto = router->ipip_ops_arr[ipip_entry->ipipt]->ul_proto;
		saddr = mlxsw_sp_ipip_netdev_saddr(ul_proto, ol_dev);
		if (mlxsw_sp_ipip_entry_saddr_matches(mlxsw_sp, ul_proto, saddr,
						      ul_tb_id, ipip_entry))
			return ERR_PTR(-EEXIST);
	}

	ipip_entry = mlxsw_sp_ipip_entry_alloc(mlxsw_sp, ipipt, ol_dev);
	if (IS_ERR(ipip_entry))
		return ipip_entry;

	decap_fib_entry = mlxsw_sp_ipip_entry_find_decap(mlxsw_sp, ipip_entry);
	if (decap_fib_entry)
		mlxsw_sp_ipip_entry_promote_decap(mlxsw_sp, ipip_entry,
						  decap_fib_entry);

	list_add_tail(&ipip_entry->ipip_list_node,
		      &mlxsw_sp->router->ipip_list);

inc_ref_count:
	++ipip_entry->ref_count;
	return ipip_entry;
}

static void
mlxsw_sp_ipip_entry_put(struct mlxsw_sp *mlxsw_sp,
			struct mlxsw_sp_ipip_entry *ipip_entry)
{
	if (--ipip_entry->ref_count == 0) {
		list_del(&ipip_entry->ipip_list_node);
		if (ipip_entry->decap_fib_entry)
			mlxsw_sp_ipip_entry_demote_decap(mlxsw_sp, ipip_entry);
		mlxsw_sp_ipip_entry_destroy(ipip_entry);
	}
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
	struct net_device *ipip_ul_dev;

	if (mlxsw_sp->router->ipip_ops_arr[ipipt]->ul_proto != ul_proto)
		return false;

	ipip_ul_dev = __mlxsw_sp_ipip_netdev_ul_dev_get(ipip_entry->ol_dev);
	return mlxsw_sp_ipip_entry_saddr_matches(mlxsw_sp, ul_proto, ul_dip,
						 ul_tb_id, ipip_entry) &&
	       (!ipip_ul_dev || ipip_ul_dev == ul_dev);
}

/* Given decap parameters, find the corresponding IPIP entry. */
static struct mlxsw_sp_ipip_entry *
mlxsw_sp_ipip_entry_find_by_decap(struct mlxsw_sp *mlxsw_sp,
				  const struct net_device *ul_dev,
				  enum mlxsw_sp_l3proto ul_proto,
				  union mlxsw_sp_l3addr ul_dip)
{
	struct mlxsw_sp_ipip_entry *ipip_entry;

	list_for_each_entry(ipip_entry, &mlxsw_sp->router->ipip_list,
			    ipip_list_node)
		if (mlxsw_sp_ipip_entry_matches_decap(mlxsw_sp, ul_dev,
						      ul_proto, ul_dip,
						      ipip_entry))
			return ipip_entry;

	return NULL;
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
	if (neigh_entry->rif_list_node.next == &rif->neigh_list)
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
	struct net_device *dev;
	struct neighbour *n;
	__be32 dipn;
	u32 dip;
	u16 rif;

	mlxsw_reg_rauhtd_ent_ipv4_unpack(rauhtd_pl, ent_index, &rif, &dip);

	if (!mlxsw_sp->router->rifs[rif]) {
		dev_err_ratelimited(mlxsw_sp->bus_info->dev, "Incorrect RIF in neighbour entry\n");
		return;
	}

	dipn = htonl(dip);
	dev = mlxsw_sp->router->rifs[rif]->dev;
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

	dev = mlxsw_sp->router->rifs[rif]->dev;
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

	/* Make sure the neighbour's netdev isn't removed in the
	 * process.
	 */
	rtnl_lock();
	do {
		mlxsw_reg_rauhtd_pack(rauhtd_pl, type);
		err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(rauhtd),
				      rauhtd_pl);
		if (err) {
			dev_err_ratelimited(mlxsw_sp->bus_info->dev, "Failed to dump neighbour talbe\n");
			break;
		}
		num_rec = mlxsw_reg_rauhtd_num_rec_get(rauhtd_pl);
		for (i = 0; i < num_rec; i++)
			mlxsw_sp_router_neigh_rec_process(mlxsw_sp, rauhtd_pl,
							  i);
	} while (mlxsw_sp_router_rauhtd_is_full(rauhtd_pl));
	rtnl_unlock();

	return err;
}

static int mlxsw_sp_router_neighs_update_rauhtd(struct mlxsw_sp *mlxsw_sp)
{
	enum mlxsw_reg_rauhtd_type type;
	char *rauhtd_pl;
	int err;

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

	/* Take RTNL mutex here to prevent lists from changes */
	rtnl_lock();
	list_for_each_entry(neigh_entry, &mlxsw_sp->router->nexthop_neighs_list,
			    nexthop_neighs_list_node)
		/* If this neigh have nexthops, make the kernel think this neigh
		 * is active regardless of the traffic.
		 */
		neigh_event_send(neigh_entry->key.n, NULL);
	rtnl_unlock();
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
	 *
	 * Take RTNL mutex here to prevent lists from changes.
	 */
	rtnl_lock();
	list_for_each_entry(neigh_entry, &router->nexthop_neighs_list,
			    nexthop_neighs_list_node)
		if (!neigh_entry->connected)
			neigh_event_send(neigh_entry->key.n, NULL);
	rtnl_unlock();

	mlxsw_core_schedule_dw(&router->nexthop_probe_dw,
			       MLXSW_SP_UNRESOLVED_NH_PROBE_INTERVAL);
}

static void
mlxsw_sp_nexthop_neigh_update(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_neigh_entry *neigh_entry,
			      bool removing);

static enum mlxsw_reg_rauht_op mlxsw_sp_rauht_op(bool adding)
{
	return adding ? MLXSW_REG_RAUHT_OP_WRITE_ADD :
			MLXSW_REG_RAUHT_OP_WRITE_DELETE;
}

static void
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
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rauht), rauht_pl);
}

static void
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
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rauht), rauht_pl);
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
	if (!adding && !neigh_entry->connected)
		return;
	neigh_entry->connected = adding;
	if (neigh_entry->key.n->tbl->family == AF_INET) {
		mlxsw_sp_router_neigh_entry_op4(mlxsw_sp, neigh_entry,
						mlxsw_sp_rauht_op(adding));
	} else if (neigh_entry->key.n->tbl->family == AF_INET6) {
		if (mlxsw_sp_neigh_ipv6_ignore(neigh_entry))
			return;
		mlxsw_sp_router_neigh_entry_op6(mlxsw_sp, neigh_entry,
						mlxsw_sp_rauht_op(adding));
	} else {
		WARN_ON_ONCE(1);
	}
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

struct mlxsw_sp_neigh_event_work {
	struct work_struct work;
	struct mlxsw_sp *mlxsw_sp;
	struct neighbour *n;
};

static void mlxsw_sp_router_neigh_event_work(struct work_struct *work)
{
	struct mlxsw_sp_neigh_event_work *neigh_work =
		container_of(work, struct mlxsw_sp_neigh_event_work, work);
	struct mlxsw_sp *mlxsw_sp = neigh_work->mlxsw_sp;
	struct mlxsw_sp_neigh_entry *neigh_entry;
	struct neighbour *n = neigh_work->n;
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

	rtnl_lock();
	entry_connected = nud_state & NUD_VALID && !dead;
	neigh_entry = mlxsw_sp_neigh_entry_lookup(mlxsw_sp, n);
	if (!entry_connected && !neigh_entry)
		goto out;
	if (!neigh_entry) {
		neigh_entry = mlxsw_sp_neigh_entry_create(mlxsw_sp, n);
		if (IS_ERR(neigh_entry))
			goto out;
	}

	memcpy(neigh_entry->ha, ha, ETH_ALEN);
	mlxsw_sp_neigh_entry_update(mlxsw_sp, neigh_entry, entry_connected);
	mlxsw_sp_nexthop_neigh_update(mlxsw_sp, neigh_entry, !entry_connected);

	if (!neigh_entry->connected && list_empty(&neigh_entry->nexthop_list))
		mlxsw_sp_neigh_entry_destroy(mlxsw_sp, neigh_entry);

out:
	rtnl_unlock();
	neigh_release(n);
	kfree(neigh_work);
}

int mlxsw_sp_router_netevent_event(struct notifier_block *unused,
				   unsigned long event, void *ptr)
{
	struct mlxsw_sp_neigh_event_work *neigh_work;
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct mlxsw_sp *mlxsw_sp;
	unsigned long interval;
	struct neigh_parms *p;
	struct neighbour *n;

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
		mlxsw_sp_port = mlxsw_sp_port_lower_dev_hold(p->dev);
		if (!mlxsw_sp_port)
			return NOTIFY_DONE;

		mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
		interval = jiffies_to_msecs(NEIGH_VAR(p, DELAY_PROBE_TIME));
		mlxsw_sp->router->neighs_update.interval = interval;

		mlxsw_sp_port_dev_put(mlxsw_sp_port);
		break;
	case NETEVENT_NEIGH_UPDATE:
		n = ptr;

		if (n->tbl->family != AF_INET && n->tbl->family != AF_INET6)
			return NOTIFY_DONE;

		mlxsw_sp_port = mlxsw_sp_port_lower_dev_hold(n->dev);
		if (!mlxsw_sp_port)
			return NOTIFY_DONE;

		neigh_work = kzalloc(sizeof(*neigh_work), GFP_ATOMIC);
		if (!neigh_work) {
			mlxsw_sp_port_dev_put(mlxsw_sp_port);
			return NOTIFY_BAD;
		}

		INIT_WORK(&neigh_work->work, mlxsw_sp_router_neigh_event_work);
		neigh_work->mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
		neigh_work->n = n;

		/* Take a reference to ensure the neighbour won't be
		 * destructed until we drop the reference in delayed
		 * work.
		 */
		neigh_clone(n);
		mlxsw_core_schedule_work(&neigh_work->work);
		mlxsw_sp_port_dev_put(mlxsw_sp_port);
		break;
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

struct mlxsw_sp_nexthop_key {
	struct fib_nh *fib_nh;
};

struct mlxsw_sp_nexthop {
	struct list_head neigh_list_node; /* member of neigh entry list */
	struct list_head rif_list_node;
	struct mlxsw_sp_nexthop_group *nh_grp; /* pointer back to the group
						* this belongs to
						*/
	struct rhash_head ht_node;
	struct mlxsw_sp_nexthop_key key;
	unsigned char gw_addr[sizeof(struct in6_addr)];
	int ifindex;
	struct mlxsw_sp_rif *rif;
	u8 should_offload:1, /* set indicates this neigh is connected and
			      * should be put to KVD linear area of this group.
			      */
	   offloaded:1, /* set in case the neigh is actually put into
			 * KVD linear area of this group.
			 */
	   update:1; /* set indicates that MAC of this neigh should be
		      * updated in HW
		      */
	enum mlxsw_sp_nexthop_type type;
	union {
		struct mlxsw_sp_neigh_entry *neigh_entry;
		struct mlxsw_sp_ipip_entry *ipip_entry;
	};
};

struct mlxsw_sp_nexthop_group {
	void *priv;
	struct rhash_head ht_node;
	struct list_head fib_list; /* list of fib entries that use this group */
	struct neigh_table *neigh_tbl;
	u8 adj_index_valid:1,
	   gateway:1; /* routes using the group use a gateway */
	u32 adj_index;
	u16 ecmp_size;
	u16 count;
	struct mlxsw_sp_nexthop nexthops[0];
#define nh_rif	nexthops[0].rif
};

static struct fib_info *
mlxsw_sp_nexthop4_group_fi(const struct mlxsw_sp_nexthop_group *nh_grp)
{
	return nh_grp->priv;
}

struct mlxsw_sp_nexthop_group_cmp_arg {
	enum mlxsw_sp_l3proto proto;
	union {
		struct fib_info *fi;
		struct mlxsw_sp_fib6_entry *fib6_entry;
	};
};

static bool
mlxsw_sp_nexthop6_group_has_nexthop(const struct mlxsw_sp_nexthop_group *nh_grp,
				    const struct in6_addr *gw, int ifindex)
{
	int i;

	for (i = 0; i < nh_grp->count; i++) {
		const struct mlxsw_sp_nexthop *nh;

		nh = &nh_grp->nexthops[i];
		if (nh->ifindex == ifindex &&
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

	if (nh_grp->count != fib6_entry->nrt6)
		return false;

	list_for_each_entry(mlxsw_sp_rt6, &fib6_entry->rt6_list, list) {
		struct in6_addr *gw;
		int ifindex;

		ifindex = mlxsw_sp_rt6->rt->dst.dev->ifindex;
		gw = &mlxsw_sp_rt6->rt->rt6i_gateway;
		if (!mlxsw_sp_nexthop6_group_has_nexthop(nh_grp, gw, ifindex))
			return false;
	}

	return true;
}

static int
mlxsw_sp_nexthop_group_cmp(struct rhashtable_compare_arg *arg, const void *ptr)
{
	const struct mlxsw_sp_nexthop_group_cmp_arg *cmp_arg = arg->key;
	const struct mlxsw_sp_nexthop_group *nh_grp = ptr;

	switch (cmp_arg->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		return cmp_arg->fi != mlxsw_sp_nexthop4_group_fi(nh_grp);
	case MLXSW_SP_L3_PROTO_IPV6:
		return !mlxsw_sp_nexthop6_group_cmp(nh_grp,
						    cmp_arg->fib6_entry);
	default:
		WARN_ON(1);
		return 1;
	}
}

static int
mlxsw_sp_nexthop_group_type(const struct mlxsw_sp_nexthop_group *nh_grp)
{
	return nh_grp->neigh_tbl->family;
}

static u32 mlxsw_sp_nexthop_group_hash_obj(const void *data, u32 len, u32 seed)
{
	const struct mlxsw_sp_nexthop_group *nh_grp = data;
	const struct mlxsw_sp_nexthop *nh;
	struct fib_info *fi;
	unsigned int val;
	int i;

	switch (mlxsw_sp_nexthop_group_type(nh_grp)) {
	case AF_INET:
		fi = mlxsw_sp_nexthop4_group_fi(nh_grp);
		return jhash(&fi, sizeof(fi), seed);
	case AF_INET6:
		val = nh_grp->count;
		for (i = 0; i < nh_grp->count; i++) {
			nh = &nh_grp->nexthops[i];
			val ^= nh->ifindex;
		}
		return jhash(&val, sizeof(val), seed);
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
	struct net_device *dev;

	list_for_each_entry(mlxsw_sp_rt6, &fib6_entry->rt6_list, list) {
		dev = mlxsw_sp_rt6->rt->dst.dev;
		val ^= dev->ifindex;
	}

	return jhash(&val, sizeof(val), seed);
}

static u32
mlxsw_sp_nexthop_group_hash(const void *data, u32 len, u32 seed)
{
	const struct mlxsw_sp_nexthop_group_cmp_arg *cmp_arg = data;

	switch (cmp_arg->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		return jhash(&cmp_arg->fi, sizeof(cmp_arg->fi), seed);
	case MLXSW_SP_L3_PROTO_IPV6:
		return mlxsw_sp_nexthop6_group_hash(cmp_arg->fib6_entry, seed);
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
	if (mlxsw_sp_nexthop_group_type(nh_grp) == AF_INET6 &&
	    !nh_grp->gateway)
		return 0;

	return rhashtable_insert_fast(&mlxsw_sp->router->nexthop_group_ht,
				      &nh_grp->ht_node,
				      mlxsw_sp_nexthop_group_ht_params);
}

static void mlxsw_sp_nexthop_group_remove(struct mlxsw_sp *mlxsw_sp,
					  struct mlxsw_sp_nexthop_group *nh_grp)
{
	if (mlxsw_sp_nexthop_group_type(nh_grp) == AF_INET6 &&
	    !nh_grp->gateway)
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

	cmp_arg.proto = MLXSW_SP_L3_PROTO_IPV4;
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

	cmp_arg.proto = MLXSW_SP_L3_PROTO_IPV6;
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
					     const struct mlxsw_sp_fib *fib,
					     u32 adj_index, u16 ecmp_size,
					     u32 new_adj_index,
					     u16 new_ecmp_size)
{
	char raleu_pl[MLXSW_REG_RALEU_LEN];

	mlxsw_reg_raleu_pack(raleu_pl,
			     (enum mlxsw_reg_ralxx_protocol) fib->proto,
			     fib->vr->id, adj_index, ecmp_size, new_adj_index,
			     new_ecmp_size);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(raleu), raleu_pl);
}

static int mlxsw_sp_adj_index_mass_update(struct mlxsw_sp *mlxsw_sp,
					  struct mlxsw_sp_nexthop_group *nh_grp,
					  u32 old_adj_index, u16 old_ecmp_size)
{
	struct mlxsw_sp_fib_entry *fib_entry;
	struct mlxsw_sp_fib *fib = NULL;
	int err;

	list_for_each_entry(fib_entry, &nh_grp->fib_list, nexthop_group_node) {
		if (fib == fib_entry->fib_node->fib)
			continue;
		fib = fib_entry->fib_node->fib;
		err = mlxsw_sp_adj_index_mass_update_vr(mlxsw_sp, fib,
							old_adj_index,
							old_ecmp_size,
							nh_grp->adj_index,
							nh_grp->ecmp_size);
		if (err)
			return err;
	}
	return 0;
}

static int mlxsw_sp_nexthop_mac_update(struct mlxsw_sp *mlxsw_sp, u32 adj_index,
				       struct mlxsw_sp_nexthop *nh)
{
	struct mlxsw_sp_neigh_entry *neigh_entry = nh->neigh_entry;
	char ratr_pl[MLXSW_REG_RATR_LEN];

	mlxsw_reg_ratr_pack(ratr_pl, MLXSW_REG_RATR_OP_WRITE_WRITE_ENTRY,
			    true, MLXSW_REG_RATR_TYPE_ETHERNET,
			    adj_index, neigh_entry->rif);
	mlxsw_reg_ratr_eth_entry_pack(ratr_pl, neigh_entry->ha);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ratr), ratr_pl);
}

static int mlxsw_sp_nexthop_ipip_update(struct mlxsw_sp *mlxsw_sp,
					u32 adj_index,
					struct mlxsw_sp_nexthop *nh)
{
	const struct mlxsw_sp_ipip_ops *ipip_ops;

	ipip_ops = mlxsw_sp->router->ipip_ops_arr[nh->ipip_entry->ipipt];
	return ipip_ops->nexthop_update(mlxsw_sp, adj_index, nh->ipip_entry);
}

static int
mlxsw_sp_nexthop_group_update(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_nexthop_group *nh_grp,
			      bool reallocate)
{
	u32 adj_index = nh_grp->adj_index; /* base */
	struct mlxsw_sp_nexthop *nh;
	int i;
	int err;

	for (i = 0; i < nh_grp->count; i++) {
		nh = &nh_grp->nexthops[i];

		if (!nh->should_offload) {
			nh->offloaded = 0;
			continue;
		}

		if (nh->update || reallocate) {
			switch (nh->type) {
			case MLXSW_SP_NEXTHOP_TYPE_ETH:
				err = mlxsw_sp_nexthop_mac_update
					    (mlxsw_sp, adj_index, nh);
				break;
			case MLXSW_SP_NEXTHOP_TYPE_IPIP:
				err = mlxsw_sp_nexthop_ipip_update
					    (mlxsw_sp, adj_index, nh);
				break;
			}
			if (err)
				return err;
			nh->update = 0;
			nh->offloaded = 1;
		}
		adj_index++;
	}
	return 0;
}

static bool
mlxsw_sp_fib_node_entry_is_first(const struct mlxsw_sp_fib_node *fib_node,
				 const struct mlxsw_sp_fib_entry *fib_entry);

static int
mlxsw_sp_nexthop_fib_entries_update(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_nexthop_group *nh_grp)
{
	struct mlxsw_sp_fib_entry *fib_entry;
	int err;

	list_for_each_entry(fib_entry, &nh_grp->fib_list, nexthop_group_node) {
		if (!mlxsw_sp_fib_node_entry_is_first(fib_entry->fib_node,
						      fib_entry))
			continue;
		err = mlxsw_sp_fib_entry_update(mlxsw_sp, fib_entry);
		if (err)
			return err;
	}
	return 0;
}

static void
mlxsw_sp_fib_entry_offload_refresh(struct mlxsw_sp_fib_entry *fib_entry,
				   enum mlxsw_reg_ralue_op op, int err);

static void
mlxsw_sp_nexthop_fib_entries_refresh(struct mlxsw_sp_nexthop_group *nh_grp)
{
	enum mlxsw_reg_ralue_op op = MLXSW_REG_RALUE_OP_WRITE_WRITE;
	struct mlxsw_sp_fib_entry *fib_entry;

	list_for_each_entry(fib_entry, &nh_grp->fib_list, nexthop_group_node) {
		if (!mlxsw_sp_fib_node_entry_is_first(fib_entry->fib_node,
						      fib_entry))
			continue;
		mlxsw_sp_fib_entry_offload_refresh(fib_entry, op, 0);
	}
}

static void
mlxsw_sp_nexthop_group_refresh(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_nexthop_group *nh_grp)
{
	struct mlxsw_sp_nexthop *nh;
	bool offload_change = false;
	u32 adj_index;
	u16 ecmp_size = 0;
	bool old_adj_index_valid;
	u32 old_adj_index;
	u16 old_ecmp_size;
	int i;
	int err;

	if (!nh_grp->gateway) {
		mlxsw_sp_nexthop_fib_entries_update(mlxsw_sp, nh_grp);
		return;
	}

	for (i = 0; i < nh_grp->count; i++) {
		nh = &nh_grp->nexthops[i];

		if (nh->should_offload != nh->offloaded) {
			offload_change = true;
			if (nh->should_offload)
				nh->update = 1;
		}
		if (nh->should_offload)
			ecmp_size++;
	}
	if (!offload_change) {
		/* Nothing was added or removed, so no need to reallocate. Just
		 * update MAC on existing adjacency indexes.
		 */
		err = mlxsw_sp_nexthop_group_update(mlxsw_sp, nh_grp, false);
		if (err) {
			dev_warn(mlxsw_sp->bus_info->dev, "Failed to update neigh MAC in adjacency table.\n");
			goto set_trap;
		}
		return;
	}
	if (!ecmp_size)
		/* No neigh of this group is connected so we just set
		 * the trap and let everthing flow through kernel.
		 */
		goto set_trap;

	err = mlxsw_sp_kvdl_alloc(mlxsw_sp, ecmp_size, &adj_index);
	if (err) {
		/* We ran out of KVD linear space, just set the
		 * trap and let everything flow through kernel.
		 */
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to allocate KVD linear area for nexthop group.\n");
		goto set_trap;
	}
	old_adj_index_valid = nh_grp->adj_index_valid;
	old_adj_index = nh_grp->adj_index;
	old_ecmp_size = nh_grp->ecmp_size;
	nh_grp->adj_index_valid = 1;
	nh_grp->adj_index = adj_index;
	nh_grp->ecmp_size = ecmp_size;
	err = mlxsw_sp_nexthop_group_update(mlxsw_sp, nh_grp, true);
	if (err) {
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to update neigh MAC in adjacency table.\n");
		goto set_trap;
	}

	if (!old_adj_index_valid) {
		/* The trap was set for fib entries, so we have to call
		 * fib entry update to unset it and use adjacency index.
		 */
		err = mlxsw_sp_nexthop_fib_entries_update(mlxsw_sp, nh_grp);
		if (err) {
			dev_warn(mlxsw_sp->bus_info->dev, "Failed to add adjacency index to fib entries.\n");
			goto set_trap;
		}
		return;
	}

	err = mlxsw_sp_adj_index_mass_update(mlxsw_sp, nh_grp,
					     old_adj_index, old_ecmp_size);
	mlxsw_sp_kvdl_free(mlxsw_sp, old_adj_index);
	if (err) {
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to mass-update adjacency index for nexthop group.\n");
		goto set_trap;
	}

	/* Offload state within the group changed, so update the flags. */
	mlxsw_sp_nexthop_fib_entries_refresh(nh_grp);

	return;

set_trap:
	old_adj_index_valid = nh_grp->adj_index_valid;
	nh_grp->adj_index_valid = 0;
	for (i = 0; i < nh_grp->count; i++) {
		nh = &nh_grp->nexthops[i];
		nh->offloaded = 0;
	}
	err = mlxsw_sp_nexthop_fib_entries_update(mlxsw_sp, nh_grp);
	if (err)
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to set traps for fib entries.\n");
	if (old_adj_index_valid)
		mlxsw_sp_kvdl_free(mlxsw_sp, nh_grp->adj_index);
}

static void __mlxsw_sp_nexthop_neigh_update(struct mlxsw_sp_nexthop *nh,
					    bool removing)
{
	if (!removing)
		nh->should_offload = 1;
	else
		nh->should_offload = 0;
	nh->update = 1;
}

static void
mlxsw_sp_nexthop_neigh_update(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_neigh_entry *neigh_entry,
			      bool removing)
{
	struct mlxsw_sp_nexthop *nh;

	list_for_each_entry(nh, &neigh_entry->nexthop_list,
			    neigh_list_node) {
		__mlxsw_sp_nexthop_neigh_update(nh, removing);
		mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh->nh_grp);
	}
}

static void mlxsw_sp_nexthop_rif_init(struct mlxsw_sp_nexthop *nh,
				      struct mlxsw_sp_rif *rif)
{
	if (nh->rif)
		return;

	nh->rif = rif;
	list_add(&nh->rif_list_node, &rif->nexthop_list);
}

static void mlxsw_sp_nexthop_rif_fini(struct mlxsw_sp_nexthop *nh)
{
	if (!nh->rif)
		return;

	list_del(&nh->rif_list_node);
	nh->rif = NULL;
}

static int mlxsw_sp_nexthop_neigh_init(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_nexthop *nh)
{
	struct mlxsw_sp_neigh_entry *neigh_entry;
	struct neighbour *n;
	u8 nud_state, dead;
	int err;

	if (!nh->nh_grp->gateway || nh->neigh_entry)
		return 0;

	/* Take a reference of neigh here ensuring that neigh would
	 * not be destructed before the nexthop entry is finished.
	 * The reference is taken either in neigh_lookup() or
	 * in neigh_create() in case n is not found.
	 */
	n = neigh_lookup(nh->nh_grp->neigh_tbl, &nh->gw_addr, nh->rif->dev);
	if (!n) {
		n = neigh_create(nh->nh_grp->neigh_tbl, &nh->gw_addr,
				 nh->rif->dev);
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

static int mlxsw_sp_nexthop_ipip_init(struct mlxsw_sp *mlxsw_sp,
				      enum mlxsw_sp_ipip_type ipipt,
				      struct mlxsw_sp_nexthop *nh,
				      struct net_device *ol_dev)
{
	if (!nh->nh_grp->gateway || nh->ipip_entry)
		return 0;

	nh->ipip_entry = mlxsw_sp_ipip_entry_get(mlxsw_sp, ipipt, ol_dev);
	if (IS_ERR(nh->ipip_entry))
		return PTR_ERR(nh->ipip_entry);

	__mlxsw_sp_nexthop_neigh_update(nh, false);
	return 0;
}

static void mlxsw_sp_nexthop_ipip_fini(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_nexthop *nh)
{
	struct mlxsw_sp_ipip_entry *ipip_entry = nh->ipip_entry;

	if (!ipip_entry)
		return;

	__mlxsw_sp_nexthop_neigh_update(nh, true);
	mlxsw_sp_ipip_entry_put(mlxsw_sp, ipip_entry);
	nh->ipip_entry = NULL;
}

static bool mlxsw_sp_nexthop4_ipip_type(const struct mlxsw_sp *mlxsw_sp,
					const struct fib_nh *fib_nh,
					enum mlxsw_sp_ipip_type *p_ipipt)
{
	struct net_device *dev = fib_nh->nh_dev;

	return dev &&
	       fib_nh->nh_parent->fib_type == RTN_UNICAST &&
	       mlxsw_sp_netdev_ipip_type(mlxsw_sp, dev, p_ipipt);
}

static void mlxsw_sp_nexthop_type_fini(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_nexthop *nh)
{
	switch (nh->type) {
	case MLXSW_SP_NEXTHOP_TYPE_ETH:
		mlxsw_sp_nexthop_neigh_fini(mlxsw_sp, nh);
		mlxsw_sp_nexthop_rif_fini(nh);
		break;
	case MLXSW_SP_NEXTHOP_TYPE_IPIP:
		mlxsw_sp_nexthop_rif_fini(nh);
		mlxsw_sp_nexthop_ipip_fini(mlxsw_sp, nh);
		break;
	}
}

static int mlxsw_sp_nexthop4_type_init(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_nexthop *nh,
				       struct fib_nh *fib_nh)
{
	struct mlxsw_sp_router *router = mlxsw_sp->router;
	struct net_device *dev = fib_nh->nh_dev;
	enum mlxsw_sp_ipip_type ipipt;
	struct mlxsw_sp_rif *rif;
	int err;

	if (mlxsw_sp_nexthop4_ipip_type(mlxsw_sp, fib_nh, &ipipt) &&
	    router->ipip_ops_arr[ipipt]->can_offload(mlxsw_sp, dev,
						     MLXSW_SP_L3_PROTO_IPV4)) {
		nh->type = MLXSW_SP_NEXTHOP_TYPE_IPIP;
		err = mlxsw_sp_nexthop_ipip_init(mlxsw_sp, ipipt, nh, dev);
		if (err)
			return err;
		mlxsw_sp_nexthop_rif_init(nh, &nh->ipip_entry->ol_lb->common);
		return 0;
	}

	nh->type = MLXSW_SP_NEXTHOP_TYPE_ETH;
	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, dev);
	if (!rif)
		return 0;

	mlxsw_sp_nexthop_rif_init(nh, rif);
	err = mlxsw_sp_nexthop_neigh_init(mlxsw_sp, nh);
	if (err)
		goto err_neigh_init;

	return 0;

err_neigh_init:
	mlxsw_sp_nexthop_rif_fini(nh);
	return err;
}

static void mlxsw_sp_nexthop4_type_fini(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_nexthop *nh)
{
	mlxsw_sp_nexthop_type_fini(mlxsw_sp, nh);
}

static int mlxsw_sp_nexthop4_init(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_nexthop_group *nh_grp,
				  struct mlxsw_sp_nexthop *nh,
				  struct fib_nh *fib_nh)
{
	struct net_device *dev = fib_nh->nh_dev;
	struct in_device *in_dev;
	int err;

	nh->nh_grp = nh_grp;
	nh->key.fib_nh = fib_nh;
	memcpy(&nh->gw_addr, &fib_nh->nh_gw, sizeof(fib_nh->nh_gw));
	err = mlxsw_sp_nexthop_insert(mlxsw_sp, nh);
	if (err)
		return err;

	if (!dev)
		return 0;

	in_dev = __in_dev_get_rtnl(dev);
	if (in_dev && IN_DEV_IGNORE_ROUTES_WITH_LINKDOWN(in_dev) &&
	    fib_nh->nh_flags & RTNH_F_LINKDOWN)
		return 0;

	err = mlxsw_sp_nexthop4_type_init(mlxsw_sp, nh, fib_nh);
	if (err)
		goto err_nexthop_neigh_init;

	return 0;

err_nexthop_neigh_init:
	mlxsw_sp_nexthop_remove(mlxsw_sp, nh);
	return err;
}

static void mlxsw_sp_nexthop4_fini(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_nexthop *nh)
{
	mlxsw_sp_nexthop4_type_fini(mlxsw_sp, nh);
	mlxsw_sp_nexthop_remove(mlxsw_sp, nh);
}

static void mlxsw_sp_nexthop4_event(struct mlxsw_sp *mlxsw_sp,
				    unsigned long event, struct fib_nh *fib_nh)
{
	struct mlxsw_sp_nexthop_key key;
	struct mlxsw_sp_nexthop *nh;

	if (mlxsw_sp->router->aborted)
		return;

	key.fib_nh = fib_nh;
	nh = mlxsw_sp_nexthop_lookup(mlxsw_sp, key);
	if (WARN_ON_ONCE(!nh))
		return;

	switch (event) {
	case FIB_EVENT_NH_ADD:
		mlxsw_sp_nexthop4_type_init(mlxsw_sp, nh, fib_nh);
		break;
	case FIB_EVENT_NH_DEL:
		mlxsw_sp_nexthop4_type_fini(mlxsw_sp, nh);
		break;
	}

	mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh->nh_grp);
}

static void mlxsw_sp_nexthop_rif_gone_sync(struct mlxsw_sp *mlxsw_sp,
					   struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp_nexthop *nh, *tmp;

	list_for_each_entry_safe(nh, tmp, &rif->nexthop_list, rif_list_node) {
		mlxsw_sp_nexthop_type_fini(mlxsw_sp, nh);
		mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh->nh_grp);
	}
}

static bool mlxsw_sp_fi_is_gateway(const struct mlxsw_sp *mlxsw_sp,
				   const struct fib_info *fi)
{
	return fi->fib_nh->nh_scope == RT_SCOPE_LINK ||
	       mlxsw_sp_nexthop4_ipip_type(mlxsw_sp, fi->fib_nh, NULL);
}

static struct mlxsw_sp_nexthop_group *
mlxsw_sp_nexthop4_group_create(struct mlxsw_sp *mlxsw_sp, struct fib_info *fi)
{
	struct mlxsw_sp_nexthop_group *nh_grp;
	struct mlxsw_sp_nexthop *nh;
	struct fib_nh *fib_nh;
	size_t alloc_size;
	int i;
	int err;

	alloc_size = sizeof(*nh_grp) +
		     fi->fib_nhs * sizeof(struct mlxsw_sp_nexthop);
	nh_grp = kzalloc(alloc_size, GFP_KERNEL);
	if (!nh_grp)
		return ERR_PTR(-ENOMEM);
	nh_grp->priv = fi;
	INIT_LIST_HEAD(&nh_grp->fib_list);
	nh_grp->neigh_tbl = &arp_tbl;

	nh_grp->gateway = mlxsw_sp_fi_is_gateway(mlxsw_sp, fi);
	nh_grp->count = fi->fib_nhs;
	fib_info_hold(fi);
	for (i = 0; i < nh_grp->count; i++) {
		nh = &nh_grp->nexthops[i];
		fib_nh = &fi->fib_nh[i];
		err = mlxsw_sp_nexthop4_init(mlxsw_sp, nh_grp, nh, fib_nh);
		if (err)
			goto err_nexthop4_init;
	}
	err = mlxsw_sp_nexthop_group_insert(mlxsw_sp, nh_grp);
	if (err)
		goto err_nexthop_group_insert;
	mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh_grp);
	return nh_grp;

err_nexthop_group_insert:
err_nexthop4_init:
	for (i--; i >= 0; i--) {
		nh = &nh_grp->nexthops[i];
		mlxsw_sp_nexthop4_fini(mlxsw_sp, nh);
	}
	fib_info_put(fi);
	kfree(nh_grp);
	return ERR_PTR(err);
}

static void
mlxsw_sp_nexthop4_group_destroy(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_nexthop_group *nh_grp)
{
	struct mlxsw_sp_nexthop *nh;
	int i;

	mlxsw_sp_nexthop_group_remove(mlxsw_sp, nh_grp);
	for (i = 0; i < nh_grp->count; i++) {
		nh = &nh_grp->nexthops[i];
		mlxsw_sp_nexthop4_fini(mlxsw_sp, nh);
	}
	mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh_grp);
	WARN_ON_ONCE(nh_grp->adj_index_valid);
	fib_info_put(mlxsw_sp_nexthop4_group_fi(nh_grp));
	kfree(nh_grp);
}

static int mlxsw_sp_nexthop4_group_get(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_fib_entry *fib_entry,
				       struct fib_info *fi)
{
	struct mlxsw_sp_nexthop_group *nh_grp;

	nh_grp = mlxsw_sp_nexthop4_group_lookup(mlxsw_sp, fi);
	if (!nh_grp) {
		nh_grp = mlxsw_sp_nexthop4_group_create(mlxsw_sp, fi);
		if (IS_ERR(nh_grp))
			return PTR_ERR(nh_grp);
	}
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
	mlxsw_sp_nexthop4_group_destroy(mlxsw_sp, nh_grp);
}

static bool
mlxsw_sp_fib4_entry_should_offload(const struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_fib4_entry *fib4_entry;

	fib4_entry = container_of(fib_entry, struct mlxsw_sp_fib4_entry,
				  common);
	return !fib4_entry->tos;
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
		return !!nh_group->adj_index_valid;
	case MLXSW_SP_FIB_ENTRY_TYPE_LOCAL:
		return !!nh_group->nh_rif;
	case MLXSW_SP_FIB_ENTRY_TYPE_IPIP_DECAP:
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

	for (i = 0; i < nh_grp->count; i++) {
		struct mlxsw_sp_nexthop *nh = &nh_grp->nexthops[i];
		struct rt6_info *rt = mlxsw_sp_rt6->rt;

		if (nh->rif && nh->rif->dev == rt->dst.dev &&
		    ipv6_addr_equal((const struct in6_addr *) &nh->gw_addr,
				    &rt->rt6i_gateway))
			return nh;
		continue;
	}

	return NULL;
}

static void
mlxsw_sp_fib4_entry_offload_set(struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_nexthop_group *nh_grp = fib_entry->nh_group;
	int i;

	if (fib_entry->type == MLXSW_SP_FIB_ENTRY_TYPE_LOCAL ||
	    fib_entry->type == MLXSW_SP_FIB_ENTRY_TYPE_IPIP_DECAP) {
		nh_grp->nexthops->key.fib_nh->nh_flags |= RTNH_F_OFFLOAD;
		return;
	}

	for (i = 0; i < nh_grp->count; i++) {
		struct mlxsw_sp_nexthop *nh = &nh_grp->nexthops[i];

		if (nh->offloaded)
			nh->key.fib_nh->nh_flags |= RTNH_F_OFFLOAD;
		else
			nh->key.fib_nh->nh_flags &= ~RTNH_F_OFFLOAD;
	}
}

static void
mlxsw_sp_fib4_entry_offload_unset(struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_nexthop_group *nh_grp = fib_entry->nh_group;
	int i;

	if (!list_is_singular(&nh_grp->fib_list))
		return;

	for (i = 0; i < nh_grp->count; i++) {
		struct mlxsw_sp_nexthop *nh = &nh_grp->nexthops[i];

		nh->key.fib_nh->nh_flags &= ~RTNH_F_OFFLOAD;
	}
}

static void
mlxsw_sp_fib6_entry_offload_set(struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_fib6_entry *fib6_entry;
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6;

	fib6_entry = container_of(fib_entry, struct mlxsw_sp_fib6_entry,
				  common);

	if (fib_entry->type == MLXSW_SP_FIB_ENTRY_TYPE_LOCAL) {
		list_first_entry(&fib6_entry->rt6_list, struct mlxsw_sp_rt6,
				 list)->rt->rt6i_nh_flags |= RTNH_F_OFFLOAD;
		return;
	}

	list_for_each_entry(mlxsw_sp_rt6, &fib6_entry->rt6_list, list) {
		struct mlxsw_sp_nexthop_group *nh_grp = fib_entry->nh_group;
		struct mlxsw_sp_nexthop *nh;

		nh = mlxsw_sp_rt6_nexthop(nh_grp, mlxsw_sp_rt6);
		if (nh && nh->offloaded)
			mlxsw_sp_rt6->rt->rt6i_nh_flags |= RTNH_F_OFFLOAD;
		else
			mlxsw_sp_rt6->rt->rt6i_nh_flags &= ~RTNH_F_OFFLOAD;
	}
}

static void
mlxsw_sp_fib6_entry_offload_unset(struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_fib6_entry *fib6_entry;
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6;

	fib6_entry = container_of(fib_entry, struct mlxsw_sp_fib6_entry,
				  common);
	list_for_each_entry(mlxsw_sp_rt6, &fib6_entry->rt6_list, list) {
		struct rt6_info *rt = mlxsw_sp_rt6->rt;

		rt->rt6i_nh_flags &= ~RTNH_F_OFFLOAD;
	}
}

static void mlxsw_sp_fib_entry_offload_set(struct mlxsw_sp_fib_entry *fib_entry)
{
	switch (fib_entry->fib_node->fib->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		mlxsw_sp_fib4_entry_offload_set(fib_entry);
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		mlxsw_sp_fib6_entry_offload_set(fib_entry);
		break;
	}
}

static void
mlxsw_sp_fib_entry_offload_unset(struct mlxsw_sp_fib_entry *fib_entry)
{
	switch (fib_entry->fib_node->fib->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		mlxsw_sp_fib4_entry_offload_unset(fib_entry);
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		mlxsw_sp_fib6_entry_offload_unset(fib_entry);
		break;
	}
}

static void
mlxsw_sp_fib_entry_offload_refresh(struct mlxsw_sp_fib_entry *fib_entry,
				   enum mlxsw_reg_ralue_op op, int err)
{
	switch (op) {
	case MLXSW_REG_RALUE_OP_WRITE_DELETE:
		return mlxsw_sp_fib_entry_offload_unset(fib_entry);
	case MLXSW_REG_RALUE_OP_WRITE_WRITE:
		if (err)
			return;
		if (mlxsw_sp_fib_entry_should_offload(fib_entry))
			mlxsw_sp_fib_entry_offload_set(fib_entry);
		else if (!mlxsw_sp_fib_entry_should_offload(fib_entry))
			mlxsw_sp_fib_entry_offload_unset(fib_entry);
		return;
	default:
		return;
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
		adjacency_index = fib_entry->nh_group->adj_index;
		ecmp_size = fib_entry->nh_group->ecmp_size;
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
	struct mlxsw_sp_rif *rif = fib_entry->nh_group->nh_rif;
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

static int
mlxsw_sp_fib_entry_op_ipip_decap(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_fib_entry *fib_entry,
				 enum mlxsw_reg_ralue_op op)
{
	struct mlxsw_sp_ipip_entry *ipip_entry = fib_entry->decap.ipip_entry;
	const struct mlxsw_sp_ipip_ops *ipip_ops;

	if (WARN_ON(!ipip_entry))
		return -EINVAL;

	ipip_ops = mlxsw_sp->router->ipip_ops_arr[ipip_entry->ipipt];
	return ipip_ops->fib_entry_op(mlxsw_sp, ipip_entry, op,
				      fib_entry->decap.tunnel_index);
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
	case MLXSW_SP_FIB_ENTRY_TYPE_IPIP_DECAP:
		return mlxsw_sp_fib_entry_op_ipip_decap(mlxsw_sp,
							fib_entry, op);
	}
	return -EINVAL;
}

static int mlxsw_sp_fib_entry_op(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_fib_entry *fib_entry,
				 enum mlxsw_reg_ralue_op op)
{
	int err = __mlxsw_sp_fib_entry_op(mlxsw_sp, fib_entry, op);

	mlxsw_sp_fib_entry_offload_refresh(fib_entry, op, err);

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
	union mlxsw_sp_l3addr dip = { .addr4 = htonl(fen_info->dst) };
	struct net_device *dev = fen_info->fi->fib_dev;
	struct mlxsw_sp_ipip_entry *ipip_entry;
	struct fib_info *fi = fen_info->fi;

	switch (fen_info->type) {
	case RTN_LOCAL:
		ipip_entry = mlxsw_sp_ipip_entry_find_by_decap(mlxsw_sp, dev,
						 MLXSW_SP_L3_PROTO_IPV4, dip);
		if (ipip_entry) {
			fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_IPIP_DECAP;
			return mlxsw_sp_fib_entry_decap_init(mlxsw_sp,
							     fib_entry,
							     ipip_entry);
		}
		/* fall through */
	case RTN_BROADCAST:
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_TRAP;
		return 0;
	case RTN_UNREACHABLE: /* fall through */
	case RTN_BLACKHOLE: /* fall through */
	case RTN_PROHIBIT:
		/* Packets hitting these routes need to be trapped, but
		 * can do so with a lower priority than packets directed
		 * at the host, so use action type local instead of trap.
		 */
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_LOCAL;
		return 0;
	case RTN_UNICAST:
		if (mlxsw_sp_fi_is_gateway(mlxsw_sp, fi))
			fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_REMOTE;
		else
			fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_LOCAL;
		return 0;
	default:
		return -EINVAL;
	}
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

	err = mlxsw_sp_fib4_entry_type_set(mlxsw_sp, fen_info, fib_entry);
	if (err)
		goto err_fib4_entry_type_set;

	err = mlxsw_sp_nexthop4_group_get(mlxsw_sp, fib_entry, fen_info->fi);
	if (err)
		goto err_nexthop4_group_get;

	fib4_entry->prio = fen_info->fi->fib_priority;
	fib4_entry->tb_id = fen_info->tb_id;
	fib4_entry->type = fen_info->type;
	fib4_entry->tos = fen_info->tos;

	fib_entry->fib_node = fib_node;

	return fib4_entry;

err_nexthop4_group_get:
err_fib4_entry_type_set:
	kfree(fib4_entry);
	return ERR_PTR(err);
}

static void mlxsw_sp_fib4_entry_destroy(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib4_entry *fib4_entry)
{
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

	list_for_each_entry(fib4_entry, &fib_node->entry_list, common.list) {
		if (fib4_entry->tb_id == fen_info->tb_id &&
		    fib4_entry->tos == fen_info->tos &&
		    fib4_entry->type == fen_info->type &&
		    mlxsw_sp_nexthop4_group_fi(fib4_entry->common.nh_group) ==
		    fen_info->fi) {
			return fib4_entry;
		}
	}

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

	INIT_LIST_HEAD(&fib_node->entry_list);
	list_add(&fib_node->list, &fib->node_list);
	memcpy(fib_node->key.addr, addr, addr_len);
	fib_node->key.prefix_len = prefix_len;

	return fib_node;
}

static void mlxsw_sp_fib_node_destroy(struct mlxsw_sp_fib_node *fib_node)
{
	list_del(&fib_node->list);
	WARN_ON(!list_empty(&fib_node->entry_list));
	kfree(fib_node);
}

static bool
mlxsw_sp_fib_node_entry_is_first(const struct mlxsw_sp_fib_node *fib_node,
				 const struct mlxsw_sp_fib_entry *fib_entry)
{
	return list_first_entry(&fib_node->entry_list,
				struct mlxsw_sp_fib_entry, list) == fib_entry;
}

static int mlxsw_sp_fib_lpm_tree_link(struct mlxsw_sp *mlxsw_sp,
				      struct mlxsw_sp_fib *fib,
				      struct mlxsw_sp_fib_node *fib_node)
{
	struct mlxsw_sp_prefix_usage req_prefix_usage = {{ 0 } };
	struct mlxsw_sp_lpm_tree *lpm_tree;
	int err;

	/* Since the tree is shared between all virtual routers we must
	 * make sure it contains all the required prefix lengths. This
	 * can be computed by either adding the new prefix length to the
	 * existing prefix usage of a bound tree, or by aggregating the
	 * prefix lengths across all virtual routers and adding the new
	 * one as well.
	 */
	if (fib->lpm_tree)
		mlxsw_sp_prefix_usage_cpy(&req_prefix_usage,
					  &fib->lpm_tree->prefix_usage);
	else
		mlxsw_sp_vrs_prefixes(mlxsw_sp, fib->proto, &req_prefix_usage);
	mlxsw_sp_prefix_usage_set(&req_prefix_usage, fib_node->key.prefix_len);

	lpm_tree = mlxsw_sp_lpm_tree_get(mlxsw_sp, &req_prefix_usage,
					 fib->proto);
	if (IS_ERR(lpm_tree))
		return PTR_ERR(lpm_tree);

	if (fib->lpm_tree && fib->lpm_tree->id == lpm_tree->id)
		return 0;

	err = mlxsw_sp_vrs_lpm_tree_replace(mlxsw_sp, fib, lpm_tree);
	if (err)
		return err;

	return 0;
}

static void mlxsw_sp_fib_lpm_tree_unlink(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_fib *fib)
{
	if (!mlxsw_sp_prefix_usage_none(&fib->prefix_usage))
		return;
	mlxsw_sp_vr_lpm_tree_unbind(mlxsw_sp, fib);
	mlxsw_sp_lpm_tree_put(mlxsw_sp, fib->lpm_tree);
	fib->lpm_tree = NULL;
}

static void mlxsw_sp_fib_node_prefix_inc(struct mlxsw_sp_fib_node *fib_node)
{
	unsigned char prefix_len = fib_node->key.prefix_len;
	struct mlxsw_sp_fib *fib = fib_node->fib;

	if (fib->prefix_ref_count[prefix_len]++ == 0)
		mlxsw_sp_prefix_usage_set(&fib->prefix_usage, prefix_len);
}

static void mlxsw_sp_fib_node_prefix_dec(struct mlxsw_sp_fib_node *fib_node)
{
	unsigned char prefix_len = fib_node->key.prefix_len;
	struct mlxsw_sp_fib *fib = fib_node->fib;

	if (--fib->prefix_ref_count[prefix_len] == 0)
		mlxsw_sp_prefix_usage_clear(&fib->prefix_usage, prefix_len);
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

	err = mlxsw_sp_fib_lpm_tree_link(mlxsw_sp, fib, fib_node);
	if (err)
		goto err_fib_lpm_tree_link;

	mlxsw_sp_fib_node_prefix_inc(fib_node);

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

	mlxsw_sp_fib_node_prefix_dec(fib_node);
	mlxsw_sp_fib_lpm_tree_unlink(mlxsw_sp, fib);
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

	vr = mlxsw_sp_vr_get(mlxsw_sp, tb_id);
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
	mlxsw_sp_vr_put(vr);
	return ERR_PTR(err);
}

static void mlxsw_sp_fib_node_put(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_fib_node *fib_node)
{
	struct mlxsw_sp_vr *vr = fib_node->fib->vr;

	if (!list_empty(&fib_node->entry_list))
		return;
	mlxsw_sp_fib_node_fini(mlxsw_sp, fib_node);
	mlxsw_sp_fib_node_destroy(fib_node);
	mlxsw_sp_vr_put(vr);
}

static struct mlxsw_sp_fib4_entry *
mlxsw_sp_fib4_node_entry_find(const struct mlxsw_sp_fib_node *fib_node,
			      const struct mlxsw_sp_fib4_entry *new4_entry)
{
	struct mlxsw_sp_fib4_entry *fib4_entry;

	list_for_each_entry(fib4_entry, &fib_node->entry_list, common.list) {
		if (fib4_entry->tb_id > new4_entry->tb_id)
			continue;
		if (fib4_entry->tb_id != new4_entry->tb_id)
			break;
		if (fib4_entry->tos > new4_entry->tos)
			continue;
		if (fib4_entry->prio >= new4_entry->prio ||
		    fib4_entry->tos < new4_entry->tos)
			return fib4_entry;
	}

	return NULL;
}

static int
mlxsw_sp_fib4_node_list_append(struct mlxsw_sp_fib4_entry *fib4_entry,
			       struct mlxsw_sp_fib4_entry *new4_entry)
{
	struct mlxsw_sp_fib_node *fib_node;

	if (WARN_ON(!fib4_entry))
		return -EINVAL;

	fib_node = fib4_entry->common.fib_node;
	list_for_each_entry_from(fib4_entry, &fib_node->entry_list,
				 common.list) {
		if (fib4_entry->tb_id != new4_entry->tb_id ||
		    fib4_entry->tos != new4_entry->tos ||
		    fib4_entry->prio != new4_entry->prio)
			break;
	}

	list_add_tail(&new4_entry->common.list, &fib4_entry->common.list);
	return 0;
}

static int
mlxsw_sp_fib4_node_list_insert(struct mlxsw_sp_fib4_entry *new4_entry,
			       bool replace, bool append)
{
	struct mlxsw_sp_fib_node *fib_node = new4_entry->common.fib_node;
	struct mlxsw_sp_fib4_entry *fib4_entry;

	fib4_entry = mlxsw_sp_fib4_node_entry_find(fib_node, new4_entry);

	if (append)
		return mlxsw_sp_fib4_node_list_append(fib4_entry, new4_entry);
	if (replace && WARN_ON(!fib4_entry))
		return -EINVAL;

	/* Insert new entry before replaced one, so that we can later
	 * remove the second.
	 */
	if (fib4_entry) {
		list_add_tail(&new4_entry->common.list,
			      &fib4_entry->common.list);
	} else {
		struct mlxsw_sp_fib4_entry *last;

		list_for_each_entry(last, &fib_node->entry_list, common.list) {
			if (new4_entry->tb_id > last->tb_id)
				break;
			fib4_entry = last;
		}

		if (fib4_entry)
			list_add(&new4_entry->common.list,
				 &fib4_entry->common.list);
		else
			list_add(&new4_entry->common.list,
				 &fib_node->entry_list);
	}

	return 0;
}

static void
mlxsw_sp_fib4_node_list_remove(struct mlxsw_sp_fib4_entry *fib4_entry)
{
	list_del(&fib4_entry->common.list);
}

static int mlxsw_sp_fib_node_entry_add(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_fib_node *fib_node = fib_entry->fib_node;

	if (!mlxsw_sp_fib_node_entry_is_first(fib_node, fib_entry))
		return 0;

	/* To prevent packet loss, overwrite the previously offloaded
	 * entry.
	 */
	if (!list_is_singular(&fib_node->entry_list)) {
		enum mlxsw_reg_ralue_op op = MLXSW_REG_RALUE_OP_WRITE_DELETE;
		struct mlxsw_sp_fib_entry *n = list_next_entry(fib_entry, list);

		mlxsw_sp_fib_entry_offload_refresh(n, op, 0);
	}

	return mlxsw_sp_fib_entry_update(mlxsw_sp, fib_entry);
}

static void mlxsw_sp_fib_node_entry_del(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib_entry *fib_entry)
{
	struct mlxsw_sp_fib_node *fib_node = fib_entry->fib_node;

	if (!mlxsw_sp_fib_node_entry_is_first(fib_node, fib_entry))
		return;

	/* Promote the next entry by overwriting the deleted entry */
	if (!list_is_singular(&fib_node->entry_list)) {
		struct mlxsw_sp_fib_entry *n = list_next_entry(fib_entry, list);
		enum mlxsw_reg_ralue_op op = MLXSW_REG_RALUE_OP_WRITE_DELETE;

		mlxsw_sp_fib_entry_update(mlxsw_sp, n);
		mlxsw_sp_fib_entry_offload_refresh(fib_entry, op, 0);
		return;
	}

	mlxsw_sp_fib_entry_del(mlxsw_sp, fib_entry);
}

static int mlxsw_sp_fib4_node_entry_link(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_fib4_entry *fib4_entry,
					 bool replace, bool append)
{
	int err;

	err = mlxsw_sp_fib4_node_list_insert(fib4_entry, replace, append);
	if (err)
		return err;

	err = mlxsw_sp_fib_node_entry_add(mlxsw_sp, &fib4_entry->common);
	if (err)
		goto err_fib_node_entry_add;

	return 0;

err_fib_node_entry_add:
	mlxsw_sp_fib4_node_list_remove(fib4_entry);
	return err;
}

static void
mlxsw_sp_fib4_node_entry_unlink(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_fib4_entry *fib4_entry)
{
	mlxsw_sp_fib_node_entry_del(mlxsw_sp, &fib4_entry->common);
	mlxsw_sp_fib4_node_list_remove(fib4_entry);

	if (fib4_entry->common.type == MLXSW_SP_FIB_ENTRY_TYPE_IPIP_DECAP)
		mlxsw_sp_fib_entry_decap_fini(mlxsw_sp, &fib4_entry->common);
}

static void mlxsw_sp_fib4_entry_replace(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib4_entry *fib4_entry,
					bool replace)
{
	struct mlxsw_sp_fib_node *fib_node = fib4_entry->common.fib_node;
	struct mlxsw_sp_fib4_entry *replaced;

	if (!replace)
		return;

	/* We inserted the new entry before replaced one */
	replaced = list_next_entry(fib4_entry, common.list);

	mlxsw_sp_fib4_node_entry_unlink(mlxsw_sp, replaced);
	mlxsw_sp_fib4_entry_destroy(mlxsw_sp, replaced);
	mlxsw_sp_fib_node_put(mlxsw_sp, fib_node);
}

static int
mlxsw_sp_router_fib4_add(struct mlxsw_sp *mlxsw_sp,
			 const struct fib_entry_notifier_info *fen_info,
			 bool replace, bool append)
{
	struct mlxsw_sp_fib4_entry *fib4_entry;
	struct mlxsw_sp_fib_node *fib_node;
	int err;

	if (mlxsw_sp->router->aborted)
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

	err = mlxsw_sp_fib4_node_entry_link(mlxsw_sp, fib4_entry, replace,
					    append);
	if (err) {
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to link FIB entry to node\n");
		goto err_fib4_node_entry_link;
	}

	mlxsw_sp_fib4_entry_replace(mlxsw_sp, fib4_entry, replace);

	return 0;

err_fib4_node_entry_link:
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

	if (mlxsw_sp->router->aborted)
		return;

	fib4_entry = mlxsw_sp_fib4_entry_lookup(mlxsw_sp, fen_info);
	if (WARN_ON(!fib4_entry))
		return;
	fib_node = fib4_entry->common.fib_node;

	mlxsw_sp_fib4_node_entry_unlink(mlxsw_sp, fib4_entry);
	mlxsw_sp_fib4_entry_destroy(mlxsw_sp, fib4_entry);
	mlxsw_sp_fib_node_put(mlxsw_sp, fib_node);
}

static bool mlxsw_sp_fib6_rt_should_ignore(const struct rt6_info *rt)
{
	/* Packets with link-local destination IP arriving to the router
	 * are trapped to the CPU, so no need to program specific routes
	 * for them.
	 */
	if (ipv6_addr_type(&rt->rt6i_dst.addr) & IPV6_ADDR_LINKLOCAL)
		return true;

	/* Multicast routes aren't supported, so ignore them. Neighbour
	 * Discovery packets are specifically trapped.
	 */
	if (ipv6_addr_type(&rt->rt6i_dst.addr) & IPV6_ADDR_MULTICAST)
		return true;

	/* Cloned routes are irrelevant in the forwarding path. */
	if (rt->rt6i_flags & RTF_CACHE)
		return true;

	return false;
}

static struct mlxsw_sp_rt6 *mlxsw_sp_rt6_create(struct rt6_info *rt)
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
	rt6_hold(rt);

	return mlxsw_sp_rt6;
}

#if IS_ENABLED(CONFIG_IPV6)
static void mlxsw_sp_rt6_release(struct rt6_info *rt)
{
	rt6_release(rt);
}
#else
static void mlxsw_sp_rt6_release(struct rt6_info *rt)
{
}
#endif

static void mlxsw_sp_rt6_destroy(struct mlxsw_sp_rt6 *mlxsw_sp_rt6)
{
	mlxsw_sp_rt6_release(mlxsw_sp_rt6->rt);
	kfree(mlxsw_sp_rt6);
}

static bool mlxsw_sp_fib6_rt_can_mp(const struct rt6_info *rt)
{
	/* RTF_CACHE routes are ignored */
	return (rt->rt6i_flags & (RTF_GATEWAY | RTF_ADDRCONF)) == RTF_GATEWAY;
}

static struct rt6_info *
mlxsw_sp_fib6_entry_rt(const struct mlxsw_sp_fib6_entry *fib6_entry)
{
	return list_first_entry(&fib6_entry->rt6_list, struct mlxsw_sp_rt6,
				list)->rt;
}

static struct mlxsw_sp_fib6_entry *
mlxsw_sp_fib6_node_mp_entry_find(const struct mlxsw_sp_fib_node *fib_node,
				 const struct rt6_info *nrt, bool replace)
{
	struct mlxsw_sp_fib6_entry *fib6_entry;

	if (!mlxsw_sp_fib6_rt_can_mp(nrt) || replace)
		return NULL;

	list_for_each_entry(fib6_entry, &fib_node->entry_list, common.list) {
		struct rt6_info *rt = mlxsw_sp_fib6_entry_rt(fib6_entry);

		/* RT6_TABLE_LOCAL and RT6_TABLE_MAIN share the same
		 * virtual router.
		 */
		if (rt->rt6i_table->tb6_id > nrt->rt6i_table->tb6_id)
			continue;
		if (rt->rt6i_table->tb6_id != nrt->rt6i_table->tb6_id)
			break;
		if (rt->rt6i_metric < nrt->rt6i_metric)
			continue;
		if (rt->rt6i_metric == nrt->rt6i_metric &&
		    mlxsw_sp_fib6_rt_can_mp(rt))
			return fib6_entry;
		if (rt->rt6i_metric > nrt->rt6i_metric)
			break;
	}

	return NULL;
}

static struct mlxsw_sp_rt6 *
mlxsw_sp_fib6_entry_rt_find(const struct mlxsw_sp_fib6_entry *fib6_entry,
			    const struct rt6_info *rt)
{
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6;

	list_for_each_entry(mlxsw_sp_rt6, &fib6_entry->rt6_list, list) {
		if (mlxsw_sp_rt6->rt == rt)
			return mlxsw_sp_rt6;
	}

	return NULL;
}

static bool mlxsw_sp_nexthop6_ipip_type(const struct mlxsw_sp *mlxsw_sp,
					const struct rt6_info *rt,
					enum mlxsw_sp_ipip_type *ret)
{
	return rt->dst.dev &&
	       mlxsw_sp_netdev_ipip_type(mlxsw_sp, rt->dst.dev, ret);
}

static int mlxsw_sp_nexthop6_type_init(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_nexthop_group *nh_grp,
				       struct mlxsw_sp_nexthop *nh,
				       const struct rt6_info *rt)
{
	struct mlxsw_sp_router *router = mlxsw_sp->router;
	struct net_device *dev = rt->dst.dev;
	enum mlxsw_sp_ipip_type ipipt;
	struct mlxsw_sp_rif *rif;
	int err;

	if (mlxsw_sp_nexthop6_ipip_type(mlxsw_sp, rt, &ipipt) &&
	    router->ipip_ops_arr[ipipt]->can_offload(mlxsw_sp, dev,
						     MLXSW_SP_L3_PROTO_IPV6)) {
		nh->type = MLXSW_SP_NEXTHOP_TYPE_IPIP;
		err = mlxsw_sp_nexthop_ipip_init(mlxsw_sp, ipipt, nh, dev);
		if (err)
			return err;
		mlxsw_sp_nexthop_rif_init(nh, &nh->ipip_entry->ol_lb->common);
		return 0;
	}

	nh->type = MLXSW_SP_NEXTHOP_TYPE_ETH;
	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, dev);
	if (!rif)
		return 0;
	mlxsw_sp_nexthop_rif_init(nh, rif);

	err = mlxsw_sp_nexthop_neigh_init(mlxsw_sp, nh);
	if (err)
		goto err_nexthop_neigh_init;

	return 0;

err_nexthop_neigh_init:
	mlxsw_sp_nexthop_rif_fini(nh);
	return err;
}

static void mlxsw_sp_nexthop6_type_fini(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_nexthop *nh)
{
	mlxsw_sp_nexthop_type_fini(mlxsw_sp, nh);
}

static int mlxsw_sp_nexthop6_init(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_nexthop_group *nh_grp,
				  struct mlxsw_sp_nexthop *nh,
				  const struct rt6_info *rt)
{
	struct net_device *dev = rt->dst.dev;

	nh->nh_grp = nh_grp;
	memcpy(&nh->gw_addr, &rt->rt6i_gateway, sizeof(nh->gw_addr));

	if (!dev)
		return 0;
	nh->ifindex = dev->ifindex;

	return mlxsw_sp_nexthop6_type_init(mlxsw_sp, nh_grp, nh, rt);
}

static void mlxsw_sp_nexthop6_fini(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_nexthop *nh)
{
	mlxsw_sp_nexthop6_type_fini(mlxsw_sp, nh);
}

static bool mlxsw_sp_rt6_is_gateway(const struct mlxsw_sp *mlxsw_sp,
				    const struct rt6_info *rt)
{
	return rt->rt6i_flags & RTF_GATEWAY ||
	       mlxsw_sp_nexthop6_ipip_type(mlxsw_sp, rt, NULL);
}

static struct mlxsw_sp_nexthop_group *
mlxsw_sp_nexthop6_group_create(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_fib6_entry *fib6_entry)
{
	struct mlxsw_sp_nexthop_group *nh_grp;
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6;
	struct mlxsw_sp_nexthop *nh;
	size_t alloc_size;
	int i = 0;
	int err;

	alloc_size = sizeof(*nh_grp) +
		     fib6_entry->nrt6 * sizeof(struct mlxsw_sp_nexthop);
	nh_grp = kzalloc(alloc_size, GFP_KERNEL);
	if (!nh_grp)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&nh_grp->fib_list);
#if IS_ENABLED(CONFIG_IPV6)
	nh_grp->neigh_tbl = &nd_tbl;
#endif
	mlxsw_sp_rt6 = list_first_entry(&fib6_entry->rt6_list,
					struct mlxsw_sp_rt6, list);
	nh_grp->gateway = mlxsw_sp_rt6_is_gateway(mlxsw_sp, mlxsw_sp_rt6->rt);
	nh_grp->count = fib6_entry->nrt6;
	for (i = 0; i < nh_grp->count; i++) {
		struct rt6_info *rt = mlxsw_sp_rt6->rt;

		nh = &nh_grp->nexthops[i];
		err = mlxsw_sp_nexthop6_init(mlxsw_sp, nh_grp, nh, rt);
		if (err)
			goto err_nexthop6_init;
		mlxsw_sp_rt6 = list_next_entry(mlxsw_sp_rt6, list);
	}

	err = mlxsw_sp_nexthop_group_insert(mlxsw_sp, nh_grp);
	if (err)
		goto err_nexthop_group_insert;

	mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh_grp);
	return nh_grp;

err_nexthop_group_insert:
err_nexthop6_init:
	for (i--; i >= 0; i--) {
		nh = &nh_grp->nexthops[i];
		mlxsw_sp_nexthop6_fini(mlxsw_sp, nh);
	}
	kfree(nh_grp);
	return ERR_PTR(err);
}

static void
mlxsw_sp_nexthop6_group_destroy(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_nexthop_group *nh_grp)
{
	struct mlxsw_sp_nexthop *nh;
	int i = nh_grp->count;

	mlxsw_sp_nexthop_group_remove(mlxsw_sp, nh_grp);
	for (i--; i >= 0; i--) {
		nh = &nh_grp->nexthops[i];
		mlxsw_sp_nexthop6_fini(mlxsw_sp, nh);
	}
	mlxsw_sp_nexthop_group_refresh(mlxsw_sp, nh_grp);
	WARN_ON(nh_grp->adj_index_valid);
	kfree(nh_grp);
}

static int mlxsw_sp_nexthop6_group_get(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_fib6_entry *fib6_entry)
{
	struct mlxsw_sp_nexthop_group *nh_grp;

	nh_grp = mlxsw_sp_nexthop6_group_lookup(mlxsw_sp, fib6_entry);
	if (!nh_grp) {
		nh_grp = mlxsw_sp_nexthop6_group_create(mlxsw_sp, fib6_entry);
		if (IS_ERR(nh_grp))
			return PTR_ERR(nh_grp);
	}

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
	mlxsw_sp_nexthop6_group_destroy(mlxsw_sp, nh_grp);
}

static int
mlxsw_sp_nexthop6_group_update(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_fib6_entry *fib6_entry)
{
	struct mlxsw_sp_nexthop_group *old_nh_grp = fib6_entry->common.nh_group;
	int err;

	fib6_entry->common.nh_group = NULL;
	list_del(&fib6_entry->common.nexthop_group_node);

	err = mlxsw_sp_nexthop6_group_get(mlxsw_sp, fib6_entry);
	if (err)
		goto err_nexthop6_group_get;

	/* In case this entry is offloaded, then the adjacency index
	 * currently associated with it in the device's table is that
	 * of the old group. Start using the new one instead.
	 */
	err = mlxsw_sp_fib_node_entry_add(mlxsw_sp, &fib6_entry->common);
	if (err)
		goto err_fib_node_entry_add;

	if (list_empty(&old_nh_grp->fib_list))
		mlxsw_sp_nexthop6_group_destroy(mlxsw_sp, old_nh_grp);

	return 0;

err_fib_node_entry_add:
	mlxsw_sp_nexthop6_group_put(mlxsw_sp, &fib6_entry->common);
err_nexthop6_group_get:
	list_add_tail(&fib6_entry->common.nexthop_group_node,
		      &old_nh_grp->fib_list);
	fib6_entry->common.nh_group = old_nh_grp;
	return err;
}

static int
mlxsw_sp_fib6_entry_nexthop_add(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_fib6_entry *fib6_entry,
				struct rt6_info *rt)
{
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6;
	int err;

	mlxsw_sp_rt6 = mlxsw_sp_rt6_create(rt);
	if (IS_ERR(mlxsw_sp_rt6))
		return PTR_ERR(mlxsw_sp_rt6);

	list_add_tail(&mlxsw_sp_rt6->list, &fib6_entry->rt6_list);
	fib6_entry->nrt6++;

	err = mlxsw_sp_nexthop6_group_update(mlxsw_sp, fib6_entry);
	if (err)
		goto err_nexthop6_group_update;

	return 0;

err_nexthop6_group_update:
	fib6_entry->nrt6--;
	list_del(&mlxsw_sp_rt6->list);
	mlxsw_sp_rt6_destroy(mlxsw_sp_rt6);
	return err;
}

static void
mlxsw_sp_fib6_entry_nexthop_del(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_fib6_entry *fib6_entry,
				struct rt6_info *rt)
{
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6;

	mlxsw_sp_rt6 = mlxsw_sp_fib6_entry_rt_find(fib6_entry, rt);
	if (WARN_ON(!mlxsw_sp_rt6))
		return;

	fib6_entry->nrt6--;
	list_del(&mlxsw_sp_rt6->list);
	mlxsw_sp_nexthop6_group_update(mlxsw_sp, fib6_entry);
	mlxsw_sp_rt6_destroy(mlxsw_sp_rt6);
}

static void mlxsw_sp_fib6_entry_type_set(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_fib_entry *fib_entry,
					 const struct rt6_info *rt)
{
	/* Packets hitting RTF_REJECT routes need to be discarded by the
	 * stack. We can rely on their destination device not having a
	 * RIF (it's the loopback device) and can thus use action type
	 * local, which will cause them to be trapped with a lower
	 * priority than packets that need to be locally received.
	 */
	if (rt->rt6i_flags & (RTF_LOCAL | RTF_ANYCAST))
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_TRAP;
	else if (rt->rt6i_flags & RTF_REJECT)
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_LOCAL;
	else if (mlxsw_sp_rt6_is_gateway(mlxsw_sp, rt))
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_REMOTE;
	else
		fib_entry->type = MLXSW_SP_FIB_ENTRY_TYPE_LOCAL;
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
			   struct rt6_info *rt)
{
	struct mlxsw_sp_fib6_entry *fib6_entry;
	struct mlxsw_sp_fib_entry *fib_entry;
	struct mlxsw_sp_rt6 *mlxsw_sp_rt6;
	int err;

	fib6_entry = kzalloc(sizeof(*fib6_entry), GFP_KERNEL);
	if (!fib6_entry)
		return ERR_PTR(-ENOMEM);
	fib_entry = &fib6_entry->common;

	mlxsw_sp_rt6 = mlxsw_sp_rt6_create(rt);
	if (IS_ERR(mlxsw_sp_rt6)) {
		err = PTR_ERR(mlxsw_sp_rt6);
		goto err_rt6_create;
	}

	mlxsw_sp_fib6_entry_type_set(mlxsw_sp, fib_entry, mlxsw_sp_rt6->rt);

	INIT_LIST_HEAD(&fib6_entry->rt6_list);
	list_add_tail(&mlxsw_sp_rt6->list, &fib6_entry->rt6_list);
	fib6_entry->nrt6 = 1;
	err = mlxsw_sp_nexthop6_group_get(mlxsw_sp, fib6_entry);
	if (err)
		goto err_nexthop6_group_get;

	fib_entry->fib_node = fib_node;

	return fib6_entry;

err_nexthop6_group_get:
	list_del(&mlxsw_sp_rt6->list);
	mlxsw_sp_rt6_destroy(mlxsw_sp_rt6);
err_rt6_create:
	kfree(fib6_entry);
	return ERR_PTR(err);
}

static void mlxsw_sp_fib6_entry_destroy(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib6_entry *fib6_entry)
{
	mlxsw_sp_nexthop6_group_put(mlxsw_sp, &fib6_entry->common);
	mlxsw_sp_fib6_entry_rt_destroy_all(fib6_entry);
	WARN_ON(fib6_entry->nrt6);
	kfree(fib6_entry);
}

static struct mlxsw_sp_fib6_entry *
mlxsw_sp_fib6_node_entry_find(const struct mlxsw_sp_fib_node *fib_node,
			      const struct rt6_info *nrt, bool replace)
{
	struct mlxsw_sp_fib6_entry *fib6_entry, *fallback = NULL;

	list_for_each_entry(fib6_entry, &fib_node->entry_list, common.list) {
		struct rt6_info *rt = mlxsw_sp_fib6_entry_rt(fib6_entry);

		if (rt->rt6i_table->tb6_id > nrt->rt6i_table->tb6_id)
			continue;
		if (rt->rt6i_table->tb6_id != nrt->rt6i_table->tb6_id)
			break;
		if (replace && rt->rt6i_metric == nrt->rt6i_metric) {
			if (mlxsw_sp_fib6_rt_can_mp(rt) ==
			    mlxsw_sp_fib6_rt_can_mp(nrt))
				return fib6_entry;
			if (mlxsw_sp_fib6_rt_can_mp(nrt))
				fallback = fallback ?: fib6_entry;
		}
		if (rt->rt6i_metric > nrt->rt6i_metric)
			return fallback ?: fib6_entry;
	}

	return fallback;
}

static int
mlxsw_sp_fib6_node_list_insert(struct mlxsw_sp_fib6_entry *new6_entry,
			       bool replace)
{
	struct mlxsw_sp_fib_node *fib_node = new6_entry->common.fib_node;
	struct rt6_info *nrt = mlxsw_sp_fib6_entry_rt(new6_entry);
	struct mlxsw_sp_fib6_entry *fib6_entry;

	fib6_entry = mlxsw_sp_fib6_node_entry_find(fib_node, nrt, replace);

	if (replace && WARN_ON(!fib6_entry))
		return -EINVAL;

	if (fib6_entry) {
		list_add_tail(&new6_entry->common.list,
			      &fib6_entry->common.list);
	} else {
		struct mlxsw_sp_fib6_entry *last;

		list_for_each_entry(last, &fib_node->entry_list, common.list) {
			struct rt6_info *rt = mlxsw_sp_fib6_entry_rt(last);

			if (nrt->rt6i_table->tb6_id > rt->rt6i_table->tb6_id)
				break;
			fib6_entry = last;
		}

		if (fib6_entry)
			list_add(&new6_entry->common.list,
				 &fib6_entry->common.list);
		else
			list_add(&new6_entry->common.list,
				 &fib_node->entry_list);
	}

	return 0;
}

static void
mlxsw_sp_fib6_node_list_remove(struct mlxsw_sp_fib6_entry *fib6_entry)
{
	list_del(&fib6_entry->common.list);
}

static int mlxsw_sp_fib6_node_entry_link(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_fib6_entry *fib6_entry,
					 bool replace)
{
	int err;

	err = mlxsw_sp_fib6_node_list_insert(fib6_entry, replace);
	if (err)
		return err;

	err = mlxsw_sp_fib_node_entry_add(mlxsw_sp, &fib6_entry->common);
	if (err)
		goto err_fib_node_entry_add;

	return 0;

err_fib_node_entry_add:
	mlxsw_sp_fib6_node_list_remove(fib6_entry);
	return err;
}

static void
mlxsw_sp_fib6_node_entry_unlink(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_fib6_entry *fib6_entry)
{
	mlxsw_sp_fib_node_entry_del(mlxsw_sp, &fib6_entry->common);
	mlxsw_sp_fib6_node_list_remove(fib6_entry);
}

static struct mlxsw_sp_fib6_entry *
mlxsw_sp_fib6_entry_lookup(struct mlxsw_sp *mlxsw_sp,
			   const struct rt6_info *rt)
{
	struct mlxsw_sp_fib6_entry *fib6_entry;
	struct mlxsw_sp_fib_node *fib_node;
	struct mlxsw_sp_fib *fib;
	struct mlxsw_sp_vr *vr;

	vr = mlxsw_sp_vr_find(mlxsw_sp, rt->rt6i_table->tb6_id);
	if (!vr)
		return NULL;
	fib = mlxsw_sp_vr_fib(vr, MLXSW_SP_L3_PROTO_IPV6);

	fib_node = mlxsw_sp_fib_node_lookup(fib, &rt->rt6i_dst.addr,
					    sizeof(rt->rt6i_dst.addr),
					    rt->rt6i_dst.plen);
	if (!fib_node)
		return NULL;

	list_for_each_entry(fib6_entry, &fib_node->entry_list, common.list) {
		struct rt6_info *iter_rt = mlxsw_sp_fib6_entry_rt(fib6_entry);

		if (rt->rt6i_table->tb6_id == iter_rt->rt6i_table->tb6_id &&
		    rt->rt6i_metric == iter_rt->rt6i_metric &&
		    mlxsw_sp_fib6_entry_rt_find(fib6_entry, rt))
			return fib6_entry;
	}

	return NULL;
}

static void mlxsw_sp_fib6_entry_replace(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fib6_entry *fib6_entry,
					bool replace)
{
	struct mlxsw_sp_fib_node *fib_node = fib6_entry->common.fib_node;
	struct mlxsw_sp_fib6_entry *replaced;

	if (!replace)
		return;

	replaced = list_next_entry(fib6_entry, common.list);

	mlxsw_sp_fib6_node_entry_unlink(mlxsw_sp, replaced);
	mlxsw_sp_fib6_entry_destroy(mlxsw_sp, replaced);
	mlxsw_sp_fib_node_put(mlxsw_sp, fib_node);
}

static int mlxsw_sp_router_fib6_add(struct mlxsw_sp *mlxsw_sp,
				    struct rt6_info *rt, bool replace)
{
	struct mlxsw_sp_fib6_entry *fib6_entry;
	struct mlxsw_sp_fib_node *fib_node;
	int err;

	if (mlxsw_sp->router->aborted)
		return 0;

	if (rt->rt6i_src.plen)
		return -EINVAL;

	if (mlxsw_sp_fib6_rt_should_ignore(rt))
		return 0;

	fib_node = mlxsw_sp_fib_node_get(mlxsw_sp, rt->rt6i_table->tb6_id,
					 &rt->rt6i_dst.addr,
					 sizeof(rt->rt6i_dst.addr),
					 rt->rt6i_dst.plen,
					 MLXSW_SP_L3_PROTO_IPV6);
	if (IS_ERR(fib_node))
		return PTR_ERR(fib_node);

	/* Before creating a new entry, try to append route to an existing
	 * multipath entry.
	 */
	fib6_entry = mlxsw_sp_fib6_node_mp_entry_find(fib_node, rt, replace);
	if (fib6_entry) {
		err = mlxsw_sp_fib6_entry_nexthop_add(mlxsw_sp, fib6_entry, rt);
		if (err)
			goto err_fib6_entry_nexthop_add;
		return 0;
	}

	fib6_entry = mlxsw_sp_fib6_entry_create(mlxsw_sp, fib_node, rt);
	if (IS_ERR(fib6_entry)) {
		err = PTR_ERR(fib6_entry);
		goto err_fib6_entry_create;
	}

	err = mlxsw_sp_fib6_node_entry_link(mlxsw_sp, fib6_entry, replace);
	if (err)
		goto err_fib6_node_entry_link;

	mlxsw_sp_fib6_entry_replace(mlxsw_sp, fib6_entry, replace);

	return 0;

err_fib6_node_entry_link:
	mlxsw_sp_fib6_entry_destroy(mlxsw_sp, fib6_entry);
err_fib6_entry_create:
err_fib6_entry_nexthop_add:
	mlxsw_sp_fib_node_put(mlxsw_sp, fib_node);
	return err;
}

static void mlxsw_sp_router_fib6_del(struct mlxsw_sp *mlxsw_sp,
				     struct rt6_info *rt)
{
	struct mlxsw_sp_fib6_entry *fib6_entry;
	struct mlxsw_sp_fib_node *fib_node;

	if (mlxsw_sp->router->aborted)
		return;

	if (mlxsw_sp_fib6_rt_should_ignore(rt))
		return;

	fib6_entry = mlxsw_sp_fib6_entry_lookup(mlxsw_sp, rt);
	if (WARN_ON(!fib6_entry))
		return;

	/* If route is part of a multipath entry, but not the last one
	 * removed, then only reduce its nexthop group.
	 */
	if (!list_is_singular(&fib6_entry->rt6_list)) {
		mlxsw_sp_fib6_entry_nexthop_del(mlxsw_sp, fib6_entry, rt);
		return;
	}

	fib_node = fib6_entry->common.fib_node;

	mlxsw_sp_fib6_node_entry_unlink(mlxsw_sp, fib6_entry);
	mlxsw_sp_fib6_entry_destroy(mlxsw_sp, fib6_entry);
	mlxsw_sp_fib_node_put(mlxsw_sp, fib_node);
}

static int __mlxsw_sp_router_set_abort_trap(struct mlxsw_sp *mlxsw_sp,
					    enum mlxsw_reg_ralxx_protocol proto,
					    u8 tree_id)
{
	char ralta_pl[MLXSW_REG_RALTA_LEN];
	char ralst_pl[MLXSW_REG_RALST_LEN];
	int i, err;

	mlxsw_reg_ralta_pack(ralta_pl, true, proto, tree_id);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralta), ralta_pl);
	if (err)
		return err;

	mlxsw_reg_ralst_pack(ralst_pl, 0xff, tree_id);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralst), ralst_pl);
	if (err)
		return err;

	for (i = 0; i < MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_VRS); i++) {
		struct mlxsw_sp_vr *vr = &mlxsw_sp->router->vrs[i];
		char raltb_pl[MLXSW_REG_RALTB_LEN];
		char ralue_pl[MLXSW_REG_RALUE_LEN];

		mlxsw_reg_raltb_pack(raltb_pl, vr->id, proto, tree_id);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(raltb),
				      raltb_pl);
		if (err)
			return err;

		mlxsw_reg_ralue_pack(ralue_pl, proto,
				     MLXSW_REG_RALUE_OP_WRITE_WRITE, vr->id, 0);
		mlxsw_reg_ralue_act_ip2me_pack(ralue_pl);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralue),
				      ralue_pl);
		if (err)
			return err;
	}

	return 0;
}

static int mlxsw_sp_router_set_abort_trap(struct mlxsw_sp *mlxsw_sp)
{
	enum mlxsw_reg_ralxx_protocol proto = MLXSW_REG_RALXX_PROTOCOL_IPV4;
	int err;

	err = __mlxsw_sp_router_set_abort_trap(mlxsw_sp, proto,
					       MLXSW_SP_LPM_TREE_MIN);
	if (err)
		return err;

	proto = MLXSW_REG_RALXX_PROTOCOL_IPV6;
	return __mlxsw_sp_router_set_abort_trap(mlxsw_sp, proto,
						MLXSW_SP_LPM_TREE_MIN + 1);
}

static void mlxsw_sp_fib4_node_flush(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_fib_node *fib_node)
{
	struct mlxsw_sp_fib4_entry *fib4_entry, *tmp;

	list_for_each_entry_safe(fib4_entry, tmp, &fib_node->entry_list,
				 common.list) {
		bool do_break = &tmp->common.list == &fib_node->entry_list;

		mlxsw_sp_fib4_node_entry_unlink(mlxsw_sp, fib4_entry);
		mlxsw_sp_fib4_entry_destroy(mlxsw_sp, fib4_entry);
		mlxsw_sp_fib_node_put(mlxsw_sp, fib_node);
		/* Break when entry list is empty and node was freed.
		 * Otherwise, we'll access freed memory in the next
		 * iteration.
		 */
		if (do_break)
			break;
	}
}

static void mlxsw_sp_fib6_node_flush(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_fib_node *fib_node)
{
	struct mlxsw_sp_fib6_entry *fib6_entry, *tmp;

	list_for_each_entry_safe(fib6_entry, tmp, &fib_node->entry_list,
				 common.list) {
		bool do_break = &tmp->common.list == &fib_node->entry_list;

		mlxsw_sp_fib6_node_entry_unlink(mlxsw_sp, fib6_entry);
		mlxsw_sp_fib6_entry_destroy(mlxsw_sp, fib6_entry);
		mlxsw_sp_fib_node_put(mlxsw_sp, fib_node);
		if (do_break)
			break;
	}
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
	int i;

	for (i = 0; i < MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_VRS); i++) {
		struct mlxsw_sp_vr *vr = &mlxsw_sp->router->vrs[i];

		if (!mlxsw_sp_vr_is_used(vr))
			continue;
		mlxsw_sp_vr_fib_flush(mlxsw_sp, vr, MLXSW_SP_L3_PROTO_IPV4);

		/* If virtual router was only used for IPv4, then it's no
		 * longer used.
		 */
		if (!mlxsw_sp_vr_is_used(vr))
			continue;
		mlxsw_sp_vr_fib_flush(mlxsw_sp, vr, MLXSW_SP_L3_PROTO_IPV6);
	}
}

static void mlxsw_sp_router_fib_abort(struct mlxsw_sp *mlxsw_sp)
{
	int err;

	if (mlxsw_sp->router->aborted)
		return;
	dev_warn(mlxsw_sp->bus_info->dev, "FIB abort triggered. Note that FIB entries are no longer being offloaded to this device.\n");
	mlxsw_sp_router_fib_flush(mlxsw_sp);
	mlxsw_sp->router->aborted = true;
	err = mlxsw_sp_router_set_abort_trap(mlxsw_sp);
	if (err)
		dev_warn(mlxsw_sp->bus_info->dev, "Failed to set abort trap.\n");
}

struct mlxsw_sp_fib_event_work {
	struct work_struct work;
	union {
		struct fib6_entry_notifier_info fen6_info;
		struct fib_entry_notifier_info fen_info;
		struct fib_rule_notifier_info fr_info;
		struct fib_nh_notifier_info fnh_info;
	};
	struct mlxsw_sp *mlxsw_sp;
	unsigned long event;
};

static void mlxsw_sp_router_fib4_event_work(struct work_struct *work)
{
	struct mlxsw_sp_fib_event_work *fib_work =
		container_of(work, struct mlxsw_sp_fib_event_work, work);
	struct mlxsw_sp *mlxsw_sp = fib_work->mlxsw_sp;
	struct fib_rule *rule;
	bool replace, append;
	int err;

	/* Protect internal structures from changes */
	rtnl_lock();
	switch (fib_work->event) {
	case FIB_EVENT_ENTRY_REPLACE: /* fall through */
	case FIB_EVENT_ENTRY_APPEND: /* fall through */
	case FIB_EVENT_ENTRY_ADD:
		replace = fib_work->event == FIB_EVENT_ENTRY_REPLACE;
		append = fib_work->event == FIB_EVENT_ENTRY_APPEND;
		err = mlxsw_sp_router_fib4_add(mlxsw_sp, &fib_work->fen_info,
					       replace, append);
		if (err)
			mlxsw_sp_router_fib_abort(mlxsw_sp);
		fib_info_put(fib_work->fen_info.fi);
		break;
	case FIB_EVENT_ENTRY_DEL:
		mlxsw_sp_router_fib4_del(mlxsw_sp, &fib_work->fen_info);
		fib_info_put(fib_work->fen_info.fi);
		break;
	case FIB_EVENT_RULE_ADD: /* fall through */
	case FIB_EVENT_RULE_DEL:
		rule = fib_work->fr_info.rule;
		if (!fib4_rule_default(rule) && !rule->l3mdev)
			mlxsw_sp_router_fib_abort(mlxsw_sp);
		fib_rule_put(rule);
		break;
	case FIB_EVENT_NH_ADD: /* fall through */
	case FIB_EVENT_NH_DEL:
		mlxsw_sp_nexthop4_event(mlxsw_sp, fib_work->event,
					fib_work->fnh_info.fib_nh);
		fib_info_put(fib_work->fnh_info.fib_nh->nh_parent);
		break;
	}
	rtnl_unlock();
	kfree(fib_work);
}

static void mlxsw_sp_router_fib6_event_work(struct work_struct *work)
{
	struct mlxsw_sp_fib_event_work *fib_work =
		container_of(work, struct mlxsw_sp_fib_event_work, work);
	struct mlxsw_sp *mlxsw_sp = fib_work->mlxsw_sp;
	struct fib_rule *rule;
	bool replace;
	int err;

	rtnl_lock();
	switch (fib_work->event) {
	case FIB_EVENT_ENTRY_REPLACE: /* fall through */
	case FIB_EVENT_ENTRY_ADD:
		replace = fib_work->event == FIB_EVENT_ENTRY_REPLACE;
		err = mlxsw_sp_router_fib6_add(mlxsw_sp,
					       fib_work->fen6_info.rt, replace);
		if (err)
			mlxsw_sp_router_fib_abort(mlxsw_sp);
		mlxsw_sp_rt6_release(fib_work->fen6_info.rt);
		break;
	case FIB_EVENT_ENTRY_DEL:
		mlxsw_sp_router_fib6_del(mlxsw_sp, fib_work->fen6_info.rt);
		mlxsw_sp_rt6_release(fib_work->fen6_info.rt);
		break;
	case FIB_EVENT_RULE_ADD: /* fall through */
	case FIB_EVENT_RULE_DEL:
		rule = fib_work->fr_info.rule;
		if (!fib6_rule_default(rule) && !rule->l3mdev)
			mlxsw_sp_router_fib_abort(mlxsw_sp);
		fib_rule_put(rule);
		break;
	}
	rtnl_unlock();
	kfree(fib_work);
}

static void mlxsw_sp_router_fib4_event(struct mlxsw_sp_fib_event_work *fib_work,
				       struct fib_notifier_info *info)
{
	switch (fib_work->event) {
	case FIB_EVENT_ENTRY_REPLACE: /* fall through */
	case FIB_EVENT_ENTRY_APPEND: /* fall through */
	case FIB_EVENT_ENTRY_ADD: /* fall through */
	case FIB_EVENT_ENTRY_DEL:
		memcpy(&fib_work->fen_info, info, sizeof(fib_work->fen_info));
		/* Take referece on fib_info to prevent it from being
		 * freed while work is queued. Release it afterwards.
		 */
		fib_info_hold(fib_work->fen_info.fi);
		break;
	case FIB_EVENT_RULE_ADD: /* fall through */
	case FIB_EVENT_RULE_DEL:
		memcpy(&fib_work->fr_info, info, sizeof(fib_work->fr_info));
		fib_rule_get(fib_work->fr_info.rule);
		break;
	case FIB_EVENT_NH_ADD: /* fall through */
	case FIB_EVENT_NH_DEL:
		memcpy(&fib_work->fnh_info, info, sizeof(fib_work->fnh_info));
		fib_info_hold(fib_work->fnh_info.fib_nh->nh_parent);
		break;
	}
}

static void mlxsw_sp_router_fib6_event(struct mlxsw_sp_fib_event_work *fib_work,
				       struct fib_notifier_info *info)
{
	switch (fib_work->event) {
	case FIB_EVENT_ENTRY_REPLACE: /* fall through */
	case FIB_EVENT_ENTRY_ADD: /* fall through */
	case FIB_EVENT_ENTRY_DEL:
		memcpy(&fib_work->fen6_info, info, sizeof(fib_work->fen6_info));
		rt6_hold(fib_work->fen6_info.rt);
		break;
	case FIB_EVENT_RULE_ADD: /* fall through */
	case FIB_EVENT_RULE_DEL:
		memcpy(&fib_work->fr_info, info, sizeof(fib_work->fr_info));
		fib_rule_get(fib_work->fr_info.rule);
		break;
	}
}

/* Called with rcu_read_lock() */
static int mlxsw_sp_router_fib_event(struct notifier_block *nb,
				     unsigned long event, void *ptr)
{
	struct mlxsw_sp_fib_event_work *fib_work;
	struct fib_notifier_info *info = ptr;
	struct mlxsw_sp_router *router;

	if (!net_eq(info->net, &init_net) ||
	    (info->family != AF_INET && info->family != AF_INET6))
		return NOTIFY_DONE;

	fib_work = kzalloc(sizeof(*fib_work), GFP_ATOMIC);
	if (WARN_ON(!fib_work))
		return NOTIFY_BAD;

	router = container_of(nb, struct mlxsw_sp_router, fib_nb);
	fib_work->mlxsw_sp = router->mlxsw_sp;
	fib_work->event = event;

	switch (info->family) {
	case AF_INET:
		INIT_WORK(&fib_work->work, mlxsw_sp_router_fib4_event_work);
		mlxsw_sp_router_fib4_event(fib_work, info);
		break;
	case AF_INET6:
		INIT_WORK(&fib_work->work, mlxsw_sp_router_fib6_event_work);
		mlxsw_sp_router_fib6_event(fib_work, info);
		break;
	}

	mlxsw_core_schedule_work(&fib_work->work);

	return NOTIFY_DONE;
}

static struct mlxsw_sp_rif *
mlxsw_sp_rif_find_by_dev(const struct mlxsw_sp *mlxsw_sp,
			 const struct net_device *dev)
{
	int i;

	for (i = 0; i < MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS); i++)
		if (mlxsw_sp->router->rifs[i] &&
		    mlxsw_sp->router->rifs[i]->dev == dev)
			return mlxsw_sp->router->rifs[i];

	return NULL;
}

static int mlxsw_sp_router_rif_disable(struct mlxsw_sp *mlxsw_sp, u16 rif)
{
	char ritr_pl[MLXSW_REG_RITR_LEN];
	int err;

	mlxsw_reg_ritr_rif_pack(ritr_pl, rif);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
	if (WARN_ON_ONCE(err))
		return err;

	mlxsw_reg_ritr_enable_set(ritr_pl, false);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
}

static void mlxsw_sp_router_rif_gone_sync(struct mlxsw_sp *mlxsw_sp,
					  struct mlxsw_sp_rif *rif)
{
	mlxsw_sp_router_rif_disable(mlxsw_sp, rif->rif_index);
	mlxsw_sp_nexthop_rif_gone_sync(mlxsw_sp, rif);
	mlxsw_sp_neigh_rif_gone_sync(mlxsw_sp, rif);
}

static bool
mlxsw_sp_rif_should_config(struct mlxsw_sp_rif *rif, struct net_device *dev,
			   unsigned long event)
{
	struct inet6_dev *inet6_dev;
	bool addr_list_empty = true;
	struct in_device *idev;

	switch (event) {
	case NETDEV_UP:
		return rif == NULL;
	case NETDEV_DOWN:
		idev = __in_dev_get_rtnl(dev);
		if (idev && idev->ifa_list)
			addr_list_empty = false;

		inet6_dev = __in6_dev_get(dev);
		if (addr_list_empty && inet6_dev &&
		    !list_empty(&inet6_dev->addr_list))
			addr_list_empty = false;

		if (rif && addr_list_empty &&
		    !netif_is_l3_slave(rif->dev))
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

static int mlxsw_sp_rif_index_alloc(struct mlxsw_sp *mlxsw_sp, u16 *p_rif_index)
{
	int i;

	for (i = 0; i < MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS); i++) {
		if (!mlxsw_sp->router->rifs[i]) {
			*p_rif_index = i;
			return 0;
		}
	}

	return -ENOBUFS;
}

static struct mlxsw_sp_rif *mlxsw_sp_rif_alloc(size_t rif_size, u16 rif_index,
					       u16 vr_id,
					       struct net_device *l3_dev)
{
	struct mlxsw_sp_rif *rif;

	rif = kzalloc(rif_size, GFP_KERNEL);
	if (!rif)
		return NULL;

	INIT_LIST_HEAD(&rif->nexthop_list);
	INIT_LIST_HEAD(&rif->neigh_list);
	ether_addr_copy(rif->addr, l3_dev->dev_addr);
	rif->mtu = l3_dev->mtu;
	rif->vr_id = vr_id;
	rif->dev = l3_dev;
	rif->rif_index = rif_index;

	return rif;
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
	return lb_rif->ul_vr_id;
}

int mlxsw_sp_rif_dev_ifindex(const struct mlxsw_sp_rif *rif)
{
	return rif->dev->ifindex;
}

static struct mlxsw_sp_rif *
mlxsw_sp_rif_create(struct mlxsw_sp *mlxsw_sp,
		    const struct mlxsw_sp_rif_params *params)
{
	u32 tb_id = l3mdev_fib_table(params->dev);
	const struct mlxsw_sp_rif_ops *ops;
	struct mlxsw_sp_fid *fid = NULL;
	enum mlxsw_sp_rif_type type;
	struct mlxsw_sp_rif *rif;
	struct mlxsw_sp_vr *vr;
	u16 rif_index;
	int err;

	type = mlxsw_sp_dev_rif_type(mlxsw_sp, params->dev);
	ops = mlxsw_sp->router->rif_ops_arr[type];

	vr = mlxsw_sp_vr_get(mlxsw_sp, tb_id ? : RT_TABLE_MAIN);
	if (IS_ERR(vr))
		return ERR_CAST(vr);
	vr->rif_count++;

	err = mlxsw_sp_rif_index_alloc(mlxsw_sp, &rif_index);
	if (err)
		goto err_rif_index_alloc;

	rif = mlxsw_sp_rif_alloc(ops->rif_size, rif_index, vr->id, params->dev);
	if (!rif) {
		err = -ENOMEM;
		goto err_rif_alloc;
	}
	rif->mlxsw_sp = mlxsw_sp;
	rif->ops = ops;

	if (ops->fid_get) {
		fid = ops->fid_get(rif);
		if (IS_ERR(fid)) {
			err = PTR_ERR(fid);
			goto err_fid_get;
		}
		rif->fid = fid;
	}

	if (ops->setup)
		ops->setup(rif, params);

	err = ops->configure(rif);
	if (err)
		goto err_configure;

	mlxsw_sp_rif_counters_alloc(rif);
	mlxsw_sp->router->rifs[rif_index] = rif;

	return rif;

err_configure:
	if (fid)
		mlxsw_sp_fid_put(fid);
err_fid_get:
	kfree(rif);
err_rif_alloc:
err_rif_index_alloc:
	vr->rif_count--;
	mlxsw_sp_vr_put(vr);
	return ERR_PTR(err);
}

void mlxsw_sp_rif_destroy(struct mlxsw_sp_rif *rif)
{
	const struct mlxsw_sp_rif_ops *ops = rif->ops;
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	struct mlxsw_sp_fid *fid = rif->fid;
	struct mlxsw_sp_vr *vr;

	mlxsw_sp_router_rif_gone_sync(mlxsw_sp, rif);
	vr = &mlxsw_sp->router->vrs[rif->vr_id];

	mlxsw_sp->router->rifs[rif->rif_index] = NULL;
	mlxsw_sp_rif_counters_free(rif);
	ops->deconfigure(rif);
	if (fid)
		/* Loopback RIFs are not associated with a FID. */
		mlxsw_sp_fid_put(fid);
	kfree(rif);
	vr->rif_count--;
	mlxsw_sp_vr_put(vr);
}

void mlxsw_sp_rif_destroy_by_dev(struct mlxsw_sp *mlxsw_sp,
				 struct net_device *dev)
{
	struct mlxsw_sp_rif *rif;

	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, dev);
	if (!rif)
		return;
	mlxsw_sp_rif_destroy(rif);
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

static int
mlxsw_sp_port_vlan_router_join(struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan,
			       struct net_device *l3_dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp_port_vlan->mlxsw_sp_port;
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u16 vid = mlxsw_sp_port_vlan->vid;
	struct mlxsw_sp_rif *rif;
	struct mlxsw_sp_fid *fid;
	int err;

	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, l3_dev);
	if (!rif) {
		struct mlxsw_sp_rif_params params = {
			.dev = l3_dev,
		};

		mlxsw_sp_rif_subport_params_init(&params, mlxsw_sp_port_vlan);
		rif = mlxsw_sp_rif_create(mlxsw_sp, &params);
		if (IS_ERR(rif))
			return PTR_ERR(rif);
	}

	/* FID was already created, just take a reference */
	fid = rif->ops->fid_get(rif);
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
	return err;
}

void
mlxsw_sp_port_vlan_router_leave(struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan)
{
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp_port_vlan->mlxsw_sp_port;
	struct mlxsw_sp_fid *fid = mlxsw_sp_port_vlan->fid;
	u16 vid = mlxsw_sp_port_vlan->vid;

	if (WARN_ON(mlxsw_sp_fid_type(fid) != MLXSW_SP_FID_TYPE_RFID))
		return;

	mlxsw_sp_port_vlan->fid = NULL;
	mlxsw_sp_port_vid_stp_set(mlxsw_sp_port, vid, BR_STATE_BLOCKING);
	mlxsw_sp_port_vid_learning_set(mlxsw_sp_port, vid, true);
	mlxsw_sp_fid_port_vid_unmap(fid, mlxsw_sp_port, vid);
	/* If router port holds the last reference on the rFID, then the
	 * associated Sub-port RIF will be destroyed.
	 */
	mlxsw_sp_fid_put(fid);
}

static int mlxsw_sp_inetaddr_port_vlan_event(struct net_device *l3_dev,
					     struct net_device *port_dev,
					     unsigned long event, u16 vid)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(port_dev);
	struct mlxsw_sp_port_vlan *mlxsw_sp_port_vlan;

	mlxsw_sp_port_vlan = mlxsw_sp_port_vlan_find_by_vid(mlxsw_sp_port, vid);
	if (WARN_ON(!mlxsw_sp_port_vlan))
		return -EINVAL;

	switch (event) {
	case NETDEV_UP:
		return mlxsw_sp_port_vlan_router_join(mlxsw_sp_port_vlan,
						      l3_dev);
	case NETDEV_DOWN:
		mlxsw_sp_port_vlan_router_leave(mlxsw_sp_port_vlan);
		break;
	}

	return 0;
}

static int mlxsw_sp_inetaddr_port_event(struct net_device *port_dev,
					unsigned long event)
{
	if (netif_is_bridge_port(port_dev) ||
	    netif_is_lag_port(port_dev) ||
	    netif_is_ovs_port(port_dev))
		return 0;

	return mlxsw_sp_inetaddr_port_vlan_event(port_dev, port_dev, event, 1);
}

static int __mlxsw_sp_inetaddr_lag_event(struct net_device *l3_dev,
					 struct net_device *lag_dev,
					 unsigned long event, u16 vid)
{
	struct net_device *port_dev;
	struct list_head *iter;
	int err;

	netdev_for_each_lower_dev(lag_dev, port_dev, iter) {
		if (mlxsw_sp_port_dev_check(port_dev)) {
			err = mlxsw_sp_inetaddr_port_vlan_event(l3_dev,
								port_dev,
								event, vid);
			if (err)
				return err;
		}
	}

	return 0;
}

static int mlxsw_sp_inetaddr_lag_event(struct net_device *lag_dev,
				       unsigned long event)
{
	if (netif_is_bridge_port(lag_dev))
		return 0;

	return __mlxsw_sp_inetaddr_lag_event(lag_dev, lag_dev, event, 1);
}

static int mlxsw_sp_inetaddr_bridge_event(struct net_device *l3_dev,
					  unsigned long event)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_lower_get(l3_dev);
	struct mlxsw_sp_rif_params params = {
		.dev = l3_dev,
	};
	struct mlxsw_sp_rif *rif;

	switch (event) {
	case NETDEV_UP:
		rif = mlxsw_sp_rif_create(mlxsw_sp, &params);
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

static int mlxsw_sp_inetaddr_vlan_event(struct net_device *vlan_dev,
					unsigned long event)
{
	struct net_device *real_dev = vlan_dev_real_dev(vlan_dev);
	u16 vid = vlan_dev_vlan_id(vlan_dev);

	if (netif_is_bridge_port(vlan_dev))
		return 0;

	if (mlxsw_sp_port_dev_check(real_dev))
		return mlxsw_sp_inetaddr_port_vlan_event(vlan_dev, real_dev,
							 event, vid);
	else if (netif_is_lag_master(real_dev))
		return __mlxsw_sp_inetaddr_lag_event(vlan_dev, real_dev, event,
						     vid);
	else if (netif_is_bridge_master(real_dev) && br_vlan_enabled(real_dev))
		return mlxsw_sp_inetaddr_bridge_event(vlan_dev, event);

	return 0;
}

static int __mlxsw_sp_inetaddr_event(struct net_device *dev,
				     unsigned long event)
{
	if (mlxsw_sp_port_dev_check(dev))
		return mlxsw_sp_inetaddr_port_event(dev, event);
	else if (netif_is_lag_master(dev))
		return mlxsw_sp_inetaddr_lag_event(dev, event);
	else if (netif_is_bridge_master(dev))
		return mlxsw_sp_inetaddr_bridge_event(dev, event);
	else if (is_vlan_dev(dev))
		return mlxsw_sp_inetaddr_vlan_event(dev, event);
	else
		return 0;
}

int mlxsw_sp_inetaddr_event(struct notifier_block *unused,
			    unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *) ptr;
	struct net_device *dev = ifa->ifa_dev->dev;
	struct mlxsw_sp *mlxsw_sp;
	struct mlxsw_sp_rif *rif;
	int err = 0;

	mlxsw_sp = mlxsw_sp_lower_get(dev);
	if (!mlxsw_sp)
		goto out;

	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, dev);
	if (!mlxsw_sp_rif_should_config(rif, dev, event))
		goto out;

	err = __mlxsw_sp_inetaddr_event(dev, event);
out:
	return notifier_from_errno(err);
}

struct mlxsw_sp_inet6addr_event_work {
	struct work_struct work;
	struct net_device *dev;
	unsigned long event;
};

static void mlxsw_sp_inet6addr_event_work(struct work_struct *work)
{
	struct mlxsw_sp_inet6addr_event_work *inet6addr_work =
		container_of(work, struct mlxsw_sp_inet6addr_event_work, work);
	struct net_device *dev = inet6addr_work->dev;
	unsigned long event = inet6addr_work->event;
	struct mlxsw_sp *mlxsw_sp;
	struct mlxsw_sp_rif *rif;

	rtnl_lock();
	mlxsw_sp = mlxsw_sp_lower_get(dev);
	if (!mlxsw_sp)
		goto out;

	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, dev);
	if (!mlxsw_sp_rif_should_config(rif, dev, event))
		goto out;

	__mlxsw_sp_inetaddr_event(dev, event);
out:
	rtnl_unlock();
	dev_put(dev);
	kfree(inet6addr_work);
}

/* Called with rcu_read_lock() */
int mlxsw_sp_inet6addr_event(struct notifier_block *unused,
			     unsigned long event, void *ptr)
{
	struct inet6_ifaddr *if6 = (struct inet6_ifaddr *) ptr;
	struct mlxsw_sp_inet6addr_event_work *inet6addr_work;
	struct net_device *dev = if6->idev->dev;

	if (!mlxsw_sp_port_dev_lower_find_rcu(dev))
		return NOTIFY_DONE;

	inet6addr_work = kzalloc(sizeof(*inet6addr_work), GFP_ATOMIC);
	if (!inet6addr_work)
		return NOTIFY_BAD;

	INIT_WORK(&inet6addr_work->work, mlxsw_sp_inet6addr_event_work);
	inet6addr_work->dev = dev;
	inet6addr_work->event = event;
	dev_hold(dev);
	mlxsw_core_schedule_work(&inet6addr_work->work);

	return NOTIFY_DONE;
}

static int mlxsw_sp_rif_edit(struct mlxsw_sp *mlxsw_sp, u16 rif_index,
			     const char *mac, int mtu)
{
	char ritr_pl[MLXSW_REG_RITR_LEN];
	int err;

	mlxsw_reg_ritr_rif_pack(ritr_pl, rif_index);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
	if (err)
		return err;

	mlxsw_reg_ritr_mtu_set(ritr_pl, mtu);
	mlxsw_reg_ritr_if_mac_memcpy_to(ritr_pl, mac);
	mlxsw_reg_ritr_op_set(ritr_pl, MLXSW_REG_RITR_RIF_CREATE);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
}

int mlxsw_sp_netdevice_router_port_event(struct net_device *dev)
{
	struct mlxsw_sp *mlxsw_sp;
	struct mlxsw_sp_rif *rif;
	u16 fid_index;
	int err;

	mlxsw_sp = mlxsw_sp_lower_get(dev);
	if (!mlxsw_sp)
		return 0;

	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, dev);
	if (!rif)
		return 0;
	fid_index = mlxsw_sp_fid_index(rif->fid);

	err = mlxsw_sp_rif_fdb_op(mlxsw_sp, rif->addr, fid_index, false);
	if (err)
		return err;

	err = mlxsw_sp_rif_edit(mlxsw_sp, rif->rif_index, dev->dev_addr,
				dev->mtu);
	if (err)
		goto err_rif_edit;

	err = mlxsw_sp_rif_fdb_op(mlxsw_sp, dev->dev_addr, fid_index, true);
	if (err)
		goto err_rif_fdb_op;

	ether_addr_copy(rif->addr, dev->dev_addr);
	rif->mtu = dev->mtu;

	netdev_dbg(dev, "Updated RIF=%d\n", rif->rif_index);

	return 0;

err_rif_fdb_op:
	mlxsw_sp_rif_edit(mlxsw_sp, rif->rif_index, rif->addr, rif->mtu);
err_rif_edit:
	mlxsw_sp_rif_fdb_op(mlxsw_sp, rif->addr, fid_index, true);
	return err;
}

static int mlxsw_sp_port_vrf_join(struct mlxsw_sp *mlxsw_sp,
				  struct net_device *l3_dev)
{
	struct mlxsw_sp_rif *rif;

	/* If netdev is already associated with a RIF, then we need to
	 * destroy it and create a new one with the new virtual router ID.
	 */
	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, l3_dev);
	if (rif)
		__mlxsw_sp_inetaddr_event(l3_dev, NETDEV_DOWN);

	return __mlxsw_sp_inetaddr_event(l3_dev, NETDEV_UP);
}

static void mlxsw_sp_port_vrf_leave(struct mlxsw_sp *mlxsw_sp,
				    struct net_device *l3_dev)
{
	struct mlxsw_sp_rif *rif;

	rif = mlxsw_sp_rif_find_by_dev(mlxsw_sp, l3_dev);
	if (!rif)
		return;
	__mlxsw_sp_inetaddr_event(l3_dev, NETDEV_DOWN);
}

int mlxsw_sp_netdevice_vrf_event(struct net_device *l3_dev, unsigned long event,
				 struct netdev_notifier_changeupper_info *info)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_lower_get(l3_dev);
	int err = 0;

	if (!mlxsw_sp)
		return 0;

	switch (event) {
	case NETDEV_PRECHANGEUPPER:
		return 0;
	case NETDEV_CHANGEUPPER:
		if (info->linking)
			err = mlxsw_sp_port_vrf_join(mlxsw_sp, l3_dev);
		else
			mlxsw_sp_port_vrf_leave(mlxsw_sp, l3_dev);
		break;
	}

	return err;
}

static struct mlxsw_sp_rif_subport *
mlxsw_sp_rif_subport_rif(const struct mlxsw_sp_rif *rif)
{
	return container_of(rif, struct mlxsw_sp_rif_subport, common);
}

static void mlxsw_sp_rif_subport_setup(struct mlxsw_sp_rif *rif,
				       const struct mlxsw_sp_rif_params *params)
{
	struct mlxsw_sp_rif_subport *rif_subport;

	rif_subport = mlxsw_sp_rif_subport_rif(rif);
	rif_subport->vid = params->vid;
	rif_subport->lag = params->lag;
	if (params->lag)
		rif_subport->lag_id = params->lag_id;
	else
		rif_subport->system_port = params->system_port;
}

static int mlxsw_sp_rif_subport_op(struct mlxsw_sp_rif *rif, bool enable)
{
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	struct mlxsw_sp_rif_subport *rif_subport;
	char ritr_pl[MLXSW_REG_RITR_LEN];

	rif_subport = mlxsw_sp_rif_subport_rif(rif);
	mlxsw_reg_ritr_pack(ritr_pl, enable, MLXSW_REG_RITR_SP_IF,
			    rif->rif_index, rif->vr_id, rif->dev->mtu);
	mlxsw_reg_ritr_mac_pack(ritr_pl, rif->dev->dev_addr);
	mlxsw_reg_ritr_sp_if_pack(ritr_pl, rif_subport->lag,
				  rif_subport->lag ? rif_subport->lag_id :
						     rif_subport->system_port,
				  rif_subport->vid);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
}

static int mlxsw_sp_rif_subport_configure(struct mlxsw_sp_rif *rif)
{
	int err;

	err = mlxsw_sp_rif_subport_op(rif, true);
	if (err)
		return err;

	err = mlxsw_sp_rif_fdb_op(rif->mlxsw_sp, rif->dev->dev_addr,
				  mlxsw_sp_fid_index(rif->fid), true);
	if (err)
		goto err_rif_fdb_op;

	mlxsw_sp_fid_rif_set(rif->fid, rif);
	return 0;

err_rif_fdb_op:
	mlxsw_sp_rif_subport_op(rif, false);
	return err;
}

static void mlxsw_sp_rif_subport_deconfigure(struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp_fid *fid = rif->fid;

	mlxsw_sp_fid_rif_set(fid, NULL);
	mlxsw_sp_rif_fdb_op(rif->mlxsw_sp, rif->dev->dev_addr,
			    mlxsw_sp_fid_index(fid), false);
	mlxsw_sp_rif_subport_op(rif, false);
}

static struct mlxsw_sp_fid *
mlxsw_sp_rif_subport_fid_get(struct mlxsw_sp_rif *rif)
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

static int mlxsw_sp_rif_vlan_fid_op(struct mlxsw_sp_rif *rif,
				    enum mlxsw_reg_ritr_if_type type,
				    u16 vid_fid, bool enable)
{
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	char ritr_pl[MLXSW_REG_RITR_LEN];

	mlxsw_reg_ritr_pack(ritr_pl, enable, type, rif->rif_index, rif->vr_id,
			    rif->dev->mtu);
	mlxsw_reg_ritr_mac_pack(ritr_pl, rif->dev->dev_addr);
	mlxsw_reg_ritr_fid_set(ritr_pl, type, vid_fid);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
}

static u8 mlxsw_sp_router_port(const struct mlxsw_sp *mlxsw_sp)
{
	return mlxsw_core_max_ports(mlxsw_sp->core) + 1;
}

static int mlxsw_sp_rif_vlan_configure(struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	u16 vid = mlxsw_sp_fid_8021q_vid(rif->fid);
	int err;

	err = mlxsw_sp_rif_vlan_fid_op(rif, MLXSW_REG_RITR_VLAN_IF, vid, true);
	if (err)
		return err;

	err = mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_MC,
				     mlxsw_sp_router_port(mlxsw_sp), true);
	if (err)
		goto err_fid_mc_flood_set;

	err = mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_BC,
				     mlxsw_sp_router_port(mlxsw_sp), true);
	if (err)
		goto err_fid_bc_flood_set;

	err = mlxsw_sp_rif_fdb_op(rif->mlxsw_sp, rif->dev->dev_addr,
				  mlxsw_sp_fid_index(rif->fid), true);
	if (err)
		goto err_rif_fdb_op;

	mlxsw_sp_fid_rif_set(rif->fid, rif);
	return 0;

err_rif_fdb_op:
	mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_BC,
			       mlxsw_sp_router_port(mlxsw_sp), false);
err_fid_bc_flood_set:
	mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_MC,
			       mlxsw_sp_router_port(mlxsw_sp), false);
err_fid_mc_flood_set:
	mlxsw_sp_rif_vlan_fid_op(rif, MLXSW_REG_RITR_VLAN_IF, vid, false);
	return err;
}

static void mlxsw_sp_rif_vlan_deconfigure(struct mlxsw_sp_rif *rif)
{
	u16 vid = mlxsw_sp_fid_8021q_vid(rif->fid);
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	struct mlxsw_sp_fid *fid = rif->fid;

	mlxsw_sp_fid_rif_set(fid, NULL);
	mlxsw_sp_rif_fdb_op(rif->mlxsw_sp, rif->dev->dev_addr,
			    mlxsw_sp_fid_index(fid), false);
	mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_BC,
			       mlxsw_sp_router_port(mlxsw_sp), false);
	mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_MC,
			       mlxsw_sp_router_port(mlxsw_sp), false);
	mlxsw_sp_rif_vlan_fid_op(rif, MLXSW_REG_RITR_VLAN_IF, vid, false);
}

static struct mlxsw_sp_fid *
mlxsw_sp_rif_vlan_fid_get(struct mlxsw_sp_rif *rif)
{
	u16 vid = is_vlan_dev(rif->dev) ? vlan_dev_vlan_id(rif->dev) : 1;

	return mlxsw_sp_fid_8021q_get(rif->mlxsw_sp, vid);
}

static const struct mlxsw_sp_rif_ops mlxsw_sp_rif_vlan_ops = {
	.type			= MLXSW_SP_RIF_TYPE_VLAN,
	.rif_size		= sizeof(struct mlxsw_sp_rif),
	.configure		= mlxsw_sp_rif_vlan_configure,
	.deconfigure		= mlxsw_sp_rif_vlan_deconfigure,
	.fid_get		= mlxsw_sp_rif_vlan_fid_get,
};

static int mlxsw_sp_rif_fid_configure(struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	u16 fid_index = mlxsw_sp_fid_index(rif->fid);
	int err;

	err = mlxsw_sp_rif_vlan_fid_op(rif, MLXSW_REG_RITR_FID_IF, fid_index,
				       true);
	if (err)
		return err;

	err = mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_MC,
				     mlxsw_sp_router_port(mlxsw_sp), true);
	if (err)
		goto err_fid_mc_flood_set;

	err = mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_BC,
				     mlxsw_sp_router_port(mlxsw_sp), true);
	if (err)
		goto err_fid_bc_flood_set;

	err = mlxsw_sp_rif_fdb_op(rif->mlxsw_sp, rif->dev->dev_addr,
				  mlxsw_sp_fid_index(rif->fid), true);
	if (err)
		goto err_rif_fdb_op;

	mlxsw_sp_fid_rif_set(rif->fid, rif);
	return 0;

err_rif_fdb_op:
	mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_BC,
			       mlxsw_sp_router_port(mlxsw_sp), false);
err_fid_bc_flood_set:
	mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_MC,
			       mlxsw_sp_router_port(mlxsw_sp), false);
err_fid_mc_flood_set:
	mlxsw_sp_rif_vlan_fid_op(rif, MLXSW_REG_RITR_FID_IF, fid_index, false);
	return err;
}

static void mlxsw_sp_rif_fid_deconfigure(struct mlxsw_sp_rif *rif)
{
	u16 fid_index = mlxsw_sp_fid_index(rif->fid);
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	struct mlxsw_sp_fid *fid = rif->fid;

	mlxsw_sp_fid_rif_set(fid, NULL);
	mlxsw_sp_rif_fdb_op(rif->mlxsw_sp, rif->dev->dev_addr,
			    mlxsw_sp_fid_index(fid), false);
	mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_BC,
			       mlxsw_sp_router_port(mlxsw_sp), false);
	mlxsw_sp_fid_flood_set(rif->fid, MLXSW_SP_FLOOD_TYPE_MC,
			       mlxsw_sp_router_port(mlxsw_sp), false);
	mlxsw_sp_rif_vlan_fid_op(rif, MLXSW_REG_RITR_FID_IF, fid_index, false);
}

static struct mlxsw_sp_fid *
mlxsw_sp_rif_fid_fid_get(struct mlxsw_sp_rif *rif)
{
	return mlxsw_sp_fid_8021d_get(rif->mlxsw_sp, rif->dev->ifindex);
}

static const struct mlxsw_sp_rif_ops mlxsw_sp_rif_fid_ops = {
	.type			= MLXSW_SP_RIF_TYPE_FID,
	.rif_size		= sizeof(struct mlxsw_sp_rif),
	.configure		= mlxsw_sp_rif_fid_configure,
	.deconfigure		= mlxsw_sp_rif_fid_deconfigure,
	.fid_get		= mlxsw_sp_rif_fid_fid_get,
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
mlxsw_sp_rif_ipip_lb_op(struct mlxsw_sp_rif_ipip_lb *lb_rif,
			struct mlxsw_sp_vr *ul_vr, bool enable)
{
	struct mlxsw_sp_rif_ipip_lb_config lb_cf = lb_rif->lb_config;
	struct mlxsw_sp_rif *rif = &lb_rif->common;
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	char ritr_pl[MLXSW_REG_RITR_LEN];
	u32 saddr4;

	switch (lb_cf.ul_protocol) {
	case MLXSW_SP_L3_PROTO_IPV4:
		saddr4 = be32_to_cpu(lb_cf.saddr.addr4);
		mlxsw_reg_ritr_pack(ritr_pl, enable, MLXSW_REG_RITR_LOOPBACK_IF,
				    rif->rif_index, rif->vr_id, rif->dev->mtu);
		mlxsw_reg_ritr_loopback_ipip4_pack(ritr_pl, lb_cf.lb_ipipt,
			    MLXSW_REG_RITR_LOOPBACK_IPIP_OPTIONS_GRE_KEY_PRESET,
			    ul_vr->id, saddr4, lb_cf.okey);
		break;

	case MLXSW_SP_L3_PROTO_IPV6:
		return -EAFNOSUPPORT;
	}

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
}

static int
mlxsw_sp_rif_ipip_lb_configure(struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp_rif_ipip_lb *lb_rif = mlxsw_sp_rif_ipip_lb_rif(rif);
	u32 ul_tb_id = mlxsw_sp_ipip_dev_ul_tb_id(rif->dev);
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	struct mlxsw_sp_vr *ul_vr;
	int err;

	ul_vr = mlxsw_sp_vr_get(mlxsw_sp, ul_tb_id);
	if (IS_ERR(ul_vr))
		return PTR_ERR(ul_vr);

	err = mlxsw_sp_rif_ipip_lb_op(lb_rif, ul_vr, true);
	if (err)
		goto err_loopback_op;

	lb_rif->ul_vr_id = ul_vr->id;
	++ul_vr->rif_count;
	return 0;

err_loopback_op:
	mlxsw_sp_vr_put(ul_vr);
	return err;
}

static void mlxsw_sp_rif_ipip_lb_deconfigure(struct mlxsw_sp_rif *rif)
{
	struct mlxsw_sp_rif_ipip_lb *lb_rif = mlxsw_sp_rif_ipip_lb_rif(rif);
	struct mlxsw_sp *mlxsw_sp = rif->mlxsw_sp;
	struct mlxsw_sp_vr *ul_vr;

	ul_vr = &mlxsw_sp->router->vrs[lb_rif->ul_vr_id];
	mlxsw_sp_rif_ipip_lb_op(lb_rif, ul_vr, false);

	--ul_vr->rif_count;
	mlxsw_sp_vr_put(ul_vr);
}

static const struct mlxsw_sp_rif_ops mlxsw_sp_rif_ipip_lb_ops = {
	.type			= MLXSW_SP_RIF_TYPE_IPIP_LB,
	.rif_size		= sizeof(struct mlxsw_sp_rif_ipip_lb),
	.setup                  = mlxsw_sp_rif_ipip_lb_setup,
	.configure		= mlxsw_sp_rif_ipip_lb_configure,
	.deconfigure		= mlxsw_sp_rif_ipip_lb_deconfigure,
};

static const struct mlxsw_sp_rif_ops *mlxsw_sp_rif_ops_arr[] = {
	[MLXSW_SP_RIF_TYPE_SUBPORT]	= &mlxsw_sp_rif_subport_ops,
	[MLXSW_SP_RIF_TYPE_VLAN]	= &mlxsw_sp_rif_vlan_ops,
	[MLXSW_SP_RIF_TYPE_FID]		= &mlxsw_sp_rif_fid_ops,
	[MLXSW_SP_RIF_TYPE_IPIP_LB]	= &mlxsw_sp_rif_ipip_lb_ops,
};

static int mlxsw_sp_rifs_init(struct mlxsw_sp *mlxsw_sp)
{
	u64 max_rifs = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS);

	mlxsw_sp->router->rifs = kcalloc(max_rifs,
					 sizeof(struct mlxsw_sp_rif *),
					 GFP_KERNEL);
	if (!mlxsw_sp->router->rifs)
		return -ENOMEM;

	mlxsw_sp->router->rif_ops_arr = mlxsw_sp_rif_ops_arr;

	return 0;
}

static void mlxsw_sp_rifs_fini(struct mlxsw_sp *mlxsw_sp)
{
	int i;

	for (i = 0; i < MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS); i++)
		WARN_ON_ONCE(mlxsw_sp->router->rifs[i]);

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
	mlxsw_sp->router->ipip_ops_arr = mlxsw_sp_ipip_ops_arr;
	INIT_LIST_HEAD(&mlxsw_sp->router->ipip_list);
	return mlxsw_sp_ipip_config_tigcr(mlxsw_sp);
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

static int __mlxsw_sp_router_init(struct mlxsw_sp *mlxsw_sp)
{
	char rgcr_pl[MLXSW_REG_RGCR_LEN];
	u64 max_rifs;
	int err;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, MAX_RIFS))
		return -EIO;
	max_rifs = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS);

	mlxsw_reg_rgcr_pack(rgcr_pl, true, true);
	mlxsw_reg_rgcr_max_router_interfaces_set(rgcr_pl, max_rifs);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rgcr), rgcr_pl);
	if (err)
		return err;
	return 0;
}

static void __mlxsw_sp_router_fini(struct mlxsw_sp *mlxsw_sp)
{
	char rgcr_pl[MLXSW_REG_RGCR_LEN];

	mlxsw_reg_rgcr_pack(rgcr_pl, false, false);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rgcr), rgcr_pl);
}

int mlxsw_sp_router_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_router *router;
	int err;

	router = kzalloc(sizeof(*mlxsw_sp->router), GFP_KERNEL);
	if (!router)
		return -ENOMEM;
	mlxsw_sp->router = router;
	router->mlxsw_sp = mlxsw_sp;

	INIT_LIST_HEAD(&mlxsw_sp->router->nexthop_neighs_list);
	err = __mlxsw_sp_router_init(mlxsw_sp);
	if (err)
		goto err_router_init;

	err = mlxsw_sp_rifs_init(mlxsw_sp);
	if (err)
		goto err_rifs_init;

	err = mlxsw_sp_ipips_init(mlxsw_sp);
	if (err)
		goto err_ipips_init;

	err = rhashtable_init(&mlxsw_sp->router->nexthop_ht,
			      &mlxsw_sp_nexthop_ht_params);
	if (err)
		goto err_nexthop_ht_init;

	err = rhashtable_init(&mlxsw_sp->router->nexthop_group_ht,
			      &mlxsw_sp_nexthop_group_ht_params);
	if (err)
		goto err_nexthop_group_ht_init;

	err = mlxsw_sp_lpm_init(mlxsw_sp);
	if (err)
		goto err_lpm_init;

	err = mlxsw_sp_vrs_init(mlxsw_sp);
	if (err)
		goto err_vrs_init;

	err = mlxsw_sp_neigh_init(mlxsw_sp);
	if (err)
		goto err_neigh_init;

	mlxsw_sp->router->fib_nb.notifier_call = mlxsw_sp_router_fib_event;
	err = register_fib_notifier(&mlxsw_sp->router->fib_nb,
				    mlxsw_sp_router_fib_dump_flush);
	if (err)
		goto err_register_fib_notifier;

	return 0;

err_register_fib_notifier:
	mlxsw_sp_neigh_fini(mlxsw_sp);
err_neigh_init:
	mlxsw_sp_vrs_fini(mlxsw_sp);
err_vrs_init:
	mlxsw_sp_lpm_fini(mlxsw_sp);
err_lpm_init:
	rhashtable_destroy(&mlxsw_sp->router->nexthop_group_ht);
err_nexthop_group_ht_init:
	rhashtable_destroy(&mlxsw_sp->router->nexthop_ht);
err_nexthop_ht_init:
	mlxsw_sp_ipips_fini(mlxsw_sp);
err_ipips_init:
	mlxsw_sp_rifs_fini(mlxsw_sp);
err_rifs_init:
	__mlxsw_sp_router_fini(mlxsw_sp);
err_router_init:
	kfree(mlxsw_sp->router);
	return err;
}

void mlxsw_sp_router_fini(struct mlxsw_sp *mlxsw_sp)
{
	unregister_fib_notifier(&mlxsw_sp->router->fib_nb);
	mlxsw_sp_neigh_fini(mlxsw_sp);
	mlxsw_sp_vrs_fini(mlxsw_sp);
	mlxsw_sp_lpm_fini(mlxsw_sp);
	rhashtable_destroy(&mlxsw_sp->router->nexthop_group_ht);
	rhashtable_destroy(&mlxsw_sp->router->nexthop_ht);
	mlxsw_sp_ipips_fini(mlxsw_sp);
	mlxsw_sp_rifs_fini(mlxsw_sp);
	__mlxsw_sp_router_fini(mlxsw_sp);
	kfree(mlxsw_sp->router);
}
