/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#include <net/flow_dissector.h>
#include <net/flow_offload.h>
#include <net/sch_generic.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_skbedit.h>
#include <linux/mlx5/fs.h>
#include <linux/mlx5/device.h>
#include <linux/rhashtable.h>
#include <linux/refcount.h>
#include <linux/completion.h>
#include <net/tc_act/tc_mirred.h>
#include <net/tc_act/tc_vlan.h>
#include <net/tc_act/tc_tunnel_key.h>
#include <net/tc_act/tc_pedit.h>
#include <net/tc_act/tc_csum.h>
#include <net/tc_act/tc_mpls.h>
#include <net/arp.h>
#include <net/ipv6_stubs.h>
#include <net/bareudp.h>
#include <net/bonding.h>
#include "en.h"
#include "en_rep.h"
#include "en/rep/tc.h"
#include "en/rep/neigh.h"
#include "en_tc.h"
#include "eswitch.h"
#include "esw/chains.h"
#include "fs_core.h"
#include "en/port.h"
#include "en/tc_tun.h"
#include "en/mapping.h"
#include "en/tc_ct.h"
#include "en/mod_hdr.h"
#include "lib/devcom.h"
#include "lib/geneve.h"
#include "diag/en_tc_tracepoint.h"

#define MLX5_MH_ACT_SZ MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)

struct mlx5_nic_flow_attr {
	u32 action;
	u32 flow_tag;
	struct mlx5_modify_hdr *modify_hdr;
	u32 hairpin_tirn;
	u8 match_level;
	struct mlx5_flow_table	*hairpin_ft;
	struct mlx5_fc		*counter;
};

#define MLX5E_TC_FLOW_BASE (MLX5E_TC_FLAG_LAST_EXPORTED_BIT + 1)

enum {
	MLX5E_TC_FLOW_FLAG_INGRESS	= MLX5E_TC_FLAG_INGRESS_BIT,
	MLX5E_TC_FLOW_FLAG_EGRESS	= MLX5E_TC_FLAG_EGRESS_BIT,
	MLX5E_TC_FLOW_FLAG_ESWITCH	= MLX5E_TC_FLAG_ESW_OFFLOAD_BIT,
	MLX5E_TC_FLOW_FLAG_FT		= MLX5E_TC_FLAG_FT_OFFLOAD_BIT,
	MLX5E_TC_FLOW_FLAG_NIC		= MLX5E_TC_FLAG_NIC_OFFLOAD_BIT,
	MLX5E_TC_FLOW_FLAG_OFFLOADED	= MLX5E_TC_FLOW_BASE,
	MLX5E_TC_FLOW_FLAG_HAIRPIN	= MLX5E_TC_FLOW_BASE + 1,
	MLX5E_TC_FLOW_FLAG_HAIRPIN_RSS	= MLX5E_TC_FLOW_BASE + 2,
	MLX5E_TC_FLOW_FLAG_SLOW		= MLX5E_TC_FLOW_BASE + 3,
	MLX5E_TC_FLOW_FLAG_DUP		= MLX5E_TC_FLOW_BASE + 4,
	MLX5E_TC_FLOW_FLAG_NOT_READY	= MLX5E_TC_FLOW_BASE + 5,
	MLX5E_TC_FLOW_FLAG_DELETED	= MLX5E_TC_FLOW_BASE + 6,
	MLX5E_TC_FLOW_FLAG_CT		= MLX5E_TC_FLOW_BASE + 7,
	MLX5E_TC_FLOW_FLAG_L3_TO_L2_DECAP = MLX5E_TC_FLOW_BASE + 8,
};

#define MLX5E_TC_MAX_SPLITS 1

/* Helper struct for accessing a struct containing list_head array.
 * Containing struct
 *   |- Helper array
 *      [0] Helper item 0
 *          |- list_head item 0
 *          |- index (0)
 *      [1] Helper item 1
 *          |- list_head item 1
 *          |- index (1)
 * To access the containing struct from one of the list_head items:
 * 1. Get the helper item from the list_head item using
 *    helper item =
 *        container_of(list_head item, helper struct type, list_head field)
 * 2. Get the contining struct from the helper item and its index in the array:
 *    containing struct =
 *        container_of(helper item, containing struct type, helper field[index])
 */
struct encap_flow_item {
	struct mlx5e_encap_entry *e; /* attached encap instance */
	struct list_head list;
	int index;
};

struct mlx5e_tc_flow {
	struct rhash_head	node;
	struct mlx5e_priv	*priv;
	u64			cookie;
	unsigned long		flags;
	struct mlx5_flow_handle *rule[MLX5E_TC_MAX_SPLITS + 1];

	/* flows sharing the same reformat object - currently mpls decap */
	struct list_head l3_to_l2_reformat;
	struct mlx5e_decap_entry *decap_reformat;

	/* Flow can be associated with multiple encap IDs.
	 * The number of encaps is bounded by the number of supported
	 * destinations.
	 */
	struct encap_flow_item encaps[MLX5_MAX_FLOW_FWD_VPORTS];
	struct mlx5e_tc_flow    *peer_flow;
	struct mlx5e_mod_hdr_handle *mh; /* attached mod header instance */
	struct mlx5e_hairpin_entry *hpe; /* attached hairpin instance */
	struct list_head	hairpin; /* flows sharing the same hairpin */
	struct list_head	peer;    /* flows with peer flow */
	struct list_head	unready; /* flows not ready to be offloaded (e.g due to missing route) */
	struct net_device	*orig_dev; /* netdev adding flow first */
	int			tmp_efi_index;
	struct list_head	tmp_list; /* temporary flow list used by neigh update */
	refcount_t		refcnt;
	struct rcu_head		rcu_head;
	struct completion	init_done;
	int tunnel_id; /* the mapped tunnel id of this flow */

	union {
		struct mlx5_esw_flow_attr esw_attr[0];
		struct mlx5_nic_flow_attr nic_attr[0];
	};
};

struct mlx5e_tc_flow_parse_attr {
	const struct ip_tunnel_info *tun_info[MLX5_MAX_FLOW_FWD_VPORTS];
	struct net_device *filter_dev;
	struct mlx5_flow_spec spec;
	struct mlx5e_tc_mod_hdr_acts mod_hdr_acts;
	int mirred_ifindex[MLX5_MAX_FLOW_FWD_VPORTS];
	struct ethhdr eth;
};

#define MLX5E_TC_TABLE_NUM_GROUPS 4
#define MLX5E_TC_TABLE_MAX_GROUP_SIZE BIT(16)

struct mlx5e_tc_attr_to_reg_mapping mlx5e_tc_attr_to_reg_mappings[] = {
	[CHAIN_TO_REG] = {
		.mfield = MLX5_ACTION_IN_FIELD_METADATA_REG_C_0,
		.moffset = 0,
		.mlen = 2,
	},
	[TUNNEL_TO_REG] = {
		.mfield = MLX5_ACTION_IN_FIELD_METADATA_REG_C_1,
		.moffset = 1,
		.mlen = 3,
		.soffset = MLX5_BYTE_OFF(fte_match_param,
					 misc_parameters_2.metadata_reg_c_1),
	},
	[ZONE_TO_REG] = zone_to_reg_ct,
	[ZONE_RESTORE_TO_REG] = zone_restore_to_reg_ct,
	[CTSTATE_TO_REG] = ctstate_to_reg_ct,
	[MARK_TO_REG] = mark_to_reg_ct,
	[LABELS_TO_REG] = labels_to_reg_ct,
	[FTEID_TO_REG] = fteid_to_reg_ct,
};

static void mlx5e_put_flow_tunnel_id(struct mlx5e_tc_flow *flow);

void
mlx5e_tc_match_to_reg_match(struct mlx5_flow_spec *spec,
			    enum mlx5e_tc_attr_to_reg type,
			    u32 data,
			    u32 mask)
{
	int soffset = mlx5e_tc_attr_to_reg_mappings[type].soffset;
	int match_len = mlx5e_tc_attr_to_reg_mappings[type].mlen;
	void *headers_c = spec->match_criteria;
	void *headers_v = spec->match_value;
	void *fmask, *fval;

	fmask = headers_c + soffset;
	fval = headers_v + soffset;

	mask = (__force u32)(cpu_to_be32(mask)) >> (32 - (match_len * 8));
	data = (__force u32)(cpu_to_be32(data)) >> (32 - (match_len * 8));

	memcpy(fmask, &mask, match_len);
	memcpy(fval, &data, match_len);

	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS_2;
}

void
mlx5e_tc_match_to_reg_get_match(struct mlx5_flow_spec *spec,
				enum mlx5e_tc_attr_to_reg type,
				u32 *data,
				u32 *mask)
{
	int soffset = mlx5e_tc_attr_to_reg_mappings[type].soffset;
	int match_len = mlx5e_tc_attr_to_reg_mappings[type].mlen;
	void *headers_c = spec->match_criteria;
	void *headers_v = spec->match_value;
	void *fmask, *fval;

	fmask = headers_c + soffset;
	fval = headers_v + soffset;

	memcpy(mask, fmask, match_len);
	memcpy(data, fval, match_len);

	*mask = be32_to_cpu((__force __be32)(*mask << (32 - (match_len * 8))));
	*data = be32_to_cpu((__force __be32)(*data << (32 - (match_len * 8))));
}

int
mlx5e_tc_match_to_reg_set(struct mlx5_core_dev *mdev,
			  struct mlx5e_tc_mod_hdr_acts *mod_hdr_acts,
			  enum mlx5e_tc_attr_to_reg type,
			  u32 data)
{
	int moffset = mlx5e_tc_attr_to_reg_mappings[type].moffset;
	int mfield = mlx5e_tc_attr_to_reg_mappings[type].mfield;
	int mlen = mlx5e_tc_attr_to_reg_mappings[type].mlen;
	char *modact;
	int err;

	err = alloc_mod_hdr_actions(mdev, MLX5_FLOW_NAMESPACE_FDB,
				    mod_hdr_acts);
	if (err)
		return err;

	modact = mod_hdr_acts->actions +
		 (mod_hdr_acts->num_actions * MLX5_MH_ACT_SZ);

	/* Firmware has 5bit length field and 0 means 32bits */
	if (mlen == 4)
		mlen = 0;

	MLX5_SET(set_action_in, modact, action_type, MLX5_ACTION_TYPE_SET);
	MLX5_SET(set_action_in, modact, field, mfield);
	MLX5_SET(set_action_in, modact, offset, moffset * 8);
	MLX5_SET(set_action_in, modact, length, mlen * 8);
	MLX5_SET(set_action_in, modact, data, data);
	mod_hdr_acts->num_actions++;

	return 0;
}

struct mlx5e_hairpin {
	struct mlx5_hairpin *pair;

	struct mlx5_core_dev *func_mdev;
	struct mlx5e_priv *func_priv;
	u32 tdn;
	u32 tirn;

	int num_channels;
	struct mlx5e_rqt indir_rqt;
	u32 indir_tirn[MLX5E_NUM_INDIR_TIRS];
	struct mlx5e_ttc_table ttc;
};

struct mlx5e_hairpin_entry {
	/* a node of a hash table which keeps all the  hairpin entries */
	struct hlist_node hairpin_hlist;

	/* protects flows list */
	spinlock_t flows_lock;
	/* flows sharing the same hairpin */
	struct list_head flows;
	/* hpe's that were not fully initialized when dead peer update event
	 * function traversed them.
	 */
	struct list_head dead_peer_wait_list;

	u16 peer_vhca_id;
	u8 prio;
	struct mlx5e_hairpin *hp;
	refcount_t refcnt;
	struct completion res_ready;
};

static void mlx5e_tc_del_flow(struct mlx5e_priv *priv,
			      struct mlx5e_tc_flow *flow);

static struct mlx5e_tc_flow *mlx5e_flow_get(struct mlx5e_tc_flow *flow)
{
	if (!flow || !refcount_inc_not_zero(&flow->refcnt))
		return ERR_PTR(-EINVAL);
	return flow;
}

static void mlx5e_flow_put(struct mlx5e_priv *priv,
			   struct mlx5e_tc_flow *flow)
{
	if (refcount_dec_and_test(&flow->refcnt)) {
		mlx5e_tc_del_flow(priv, flow);
		kfree_rcu(flow, rcu_head);
	}
}

static void __flow_flag_set(struct mlx5e_tc_flow *flow, unsigned long flag)
{
	/* Complete all memory stores before setting bit. */
	smp_mb__before_atomic();
	set_bit(flag, &flow->flags);
}

#define flow_flag_set(flow, flag) __flow_flag_set(flow, MLX5E_TC_FLOW_FLAG_##flag)

static bool __flow_flag_test_and_set(struct mlx5e_tc_flow *flow,
				     unsigned long flag)
{
	/* test_and_set_bit() provides all necessary barriers */
	return test_and_set_bit(flag, &flow->flags);
}

#define flow_flag_test_and_set(flow, flag)			\
	__flow_flag_test_and_set(flow,				\
				 MLX5E_TC_FLOW_FLAG_##flag)

static void __flow_flag_clear(struct mlx5e_tc_flow *flow, unsigned long flag)
{
	/* Complete all memory stores before clearing bit. */
	smp_mb__before_atomic();
	clear_bit(flag, &flow->flags);
}

#define flow_flag_clear(flow, flag) __flow_flag_clear(flow, \
						      MLX5E_TC_FLOW_FLAG_##flag)

static bool __flow_flag_test(struct mlx5e_tc_flow *flow, unsigned long flag)
{
	bool ret = test_bit(flag, &flow->flags);

	/* Read fields of flow structure only after checking flags. */
	smp_mb__after_atomic();
	return ret;
}

#define flow_flag_test(flow, flag) __flow_flag_test(flow, \
						    MLX5E_TC_FLOW_FLAG_##flag)

static bool mlx5e_is_eswitch_flow(struct mlx5e_tc_flow *flow)
{
	return flow_flag_test(flow, ESWITCH);
}

static bool mlx5e_is_ft_flow(struct mlx5e_tc_flow *flow)
{
	return flow_flag_test(flow, FT);
}

static bool mlx5e_is_offloaded_flow(struct mlx5e_tc_flow *flow)
{
	return flow_flag_test(flow, OFFLOADED);
}

static int get_flow_name_space(struct mlx5e_tc_flow *flow)
{
	return mlx5e_is_eswitch_flow(flow) ?
		MLX5_FLOW_NAMESPACE_FDB : MLX5_FLOW_NAMESPACE_KERNEL;
}

static struct mod_hdr_tbl *
get_mod_hdr_table(struct mlx5e_priv *priv, struct mlx5e_tc_flow *flow)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;

	return get_flow_name_space(flow) == MLX5_FLOW_NAMESPACE_FDB ?
		&esw->offloads.mod_hdr :
		&priv->fs.tc.mod_hdr;
}

static int mlx5e_attach_mod_hdr(struct mlx5e_priv *priv,
				struct mlx5e_tc_flow *flow,
				struct mlx5e_tc_flow_parse_attr *parse_attr)
{
	struct mlx5_modify_hdr *modify_hdr;
	struct mlx5e_mod_hdr_handle *mh;

	mh = mlx5e_mod_hdr_attach(priv->mdev, get_mod_hdr_table(priv, flow),
				  get_flow_name_space(flow),
				  &parse_attr->mod_hdr_acts);
	if (IS_ERR(mh))
		return PTR_ERR(mh);

	modify_hdr = mlx5e_mod_hdr_get(mh);
	if (mlx5e_is_eswitch_flow(flow))
		flow->esw_attr->modify_hdr = modify_hdr;
	else
		flow->nic_attr->modify_hdr = modify_hdr;
	flow->mh = mh;

	return 0;
}

static void mlx5e_detach_mod_hdr(struct mlx5e_priv *priv,
				 struct mlx5e_tc_flow *flow)
{
	/* flow wasn't fully initialized */
	if (!flow->mh)
		return;

	mlx5e_mod_hdr_detach(priv->mdev, get_mod_hdr_table(priv, flow),
			     flow->mh);
	flow->mh = NULL;
}

static
struct mlx5_core_dev *mlx5e_hairpin_get_mdev(struct net *net, int ifindex)
{
	struct net_device *netdev;
	struct mlx5e_priv *priv;

	netdev = __dev_get_by_index(net, ifindex);
	priv = netdev_priv(netdev);
	return priv->mdev;
}

static int mlx5e_hairpin_create_transport(struct mlx5e_hairpin *hp)
{
	u32 in[MLX5_ST_SZ_DW(create_tir_in)] = {};
	void *tirc;
	int err;

	err = mlx5_core_alloc_transport_domain(hp->func_mdev, &hp->tdn);
	if (err)
		goto alloc_tdn_err;

	tirc = MLX5_ADDR_OF(create_tir_in, in, ctx);

	MLX5_SET(tirc, tirc, disp_type, MLX5_TIRC_DISP_TYPE_DIRECT);
	MLX5_SET(tirc, tirc, inline_rqn, hp->pair->rqn[0]);
	MLX5_SET(tirc, tirc, transport_domain, hp->tdn);

	err = mlx5_core_create_tir(hp->func_mdev, in, &hp->tirn);
	if (err)
		goto create_tir_err;

	return 0;

create_tir_err:
	mlx5_core_dealloc_transport_domain(hp->func_mdev, hp->tdn);
alloc_tdn_err:
	return err;
}

static void mlx5e_hairpin_destroy_transport(struct mlx5e_hairpin *hp)
{
	mlx5_core_destroy_tir(hp->func_mdev, hp->tirn);
	mlx5_core_dealloc_transport_domain(hp->func_mdev, hp->tdn);
}

static void mlx5e_hairpin_fill_rqt_rqns(struct mlx5e_hairpin *hp, void *rqtc)
{
	u32 indirection_rqt[MLX5E_INDIR_RQT_SIZE], rqn;
	struct mlx5e_priv *priv = hp->func_priv;
	int i, ix, sz = MLX5E_INDIR_RQT_SIZE;

	mlx5e_build_default_indir_rqt(indirection_rqt, sz,
				      hp->num_channels);

	for (i = 0; i < sz; i++) {
		ix = i;
		if (priv->rss_params.hfunc == ETH_RSS_HASH_XOR)
			ix = mlx5e_bits_invert(i, ilog2(sz));
		ix = indirection_rqt[ix];
		rqn = hp->pair->rqn[ix];
		MLX5_SET(rqtc, rqtc, rq_num[i], rqn);
	}
}

static int mlx5e_hairpin_create_indirect_rqt(struct mlx5e_hairpin *hp)
{
	int inlen, err, sz = MLX5E_INDIR_RQT_SIZE;
	struct mlx5e_priv *priv = hp->func_priv;
	struct mlx5_core_dev *mdev = priv->mdev;
	void *rqtc;
	u32 *in;

	inlen = MLX5_ST_SZ_BYTES(create_rqt_in) + sizeof(u32) * sz;
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	rqtc = MLX5_ADDR_OF(create_rqt_in, in, rqt_context);

	MLX5_SET(rqtc, rqtc, rqt_actual_size, sz);
	MLX5_SET(rqtc, rqtc, rqt_max_size, sz);

	mlx5e_hairpin_fill_rqt_rqns(hp, rqtc);

	err = mlx5_core_create_rqt(mdev, in, inlen, &hp->indir_rqt.rqtn);
	if (!err)
		hp->indir_rqt.enabled = true;

	kvfree(in);
	return err;
}

static int mlx5e_hairpin_create_indirect_tirs(struct mlx5e_hairpin *hp)
{
	struct mlx5e_priv *priv = hp->func_priv;
	u32 in[MLX5_ST_SZ_DW(create_tir_in)];
	int tt, i, err;
	void *tirc;

	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++) {
		struct mlx5e_tirc_config ttconfig = mlx5e_tirc_get_default_config(tt);

		memset(in, 0, MLX5_ST_SZ_BYTES(create_tir_in));
		tirc = MLX5_ADDR_OF(create_tir_in, in, ctx);

		MLX5_SET(tirc, tirc, transport_domain, hp->tdn);
		MLX5_SET(tirc, tirc, disp_type, MLX5_TIRC_DISP_TYPE_INDIRECT);
		MLX5_SET(tirc, tirc, indirect_table, hp->indir_rqt.rqtn);
		mlx5e_build_indir_tir_ctx_hash(&priv->rss_params, &ttconfig, tirc, false);

		err = mlx5_core_create_tir(hp->func_mdev, in,
					   &hp->indir_tirn[tt]);
		if (err) {
			mlx5_core_warn(hp->func_mdev, "create indirect tirs failed, %d\n", err);
			goto err_destroy_tirs;
		}
	}
	return 0;

err_destroy_tirs:
	for (i = 0; i < tt; i++)
		mlx5_core_destroy_tir(hp->func_mdev, hp->indir_tirn[i]);
	return err;
}

static void mlx5e_hairpin_destroy_indirect_tirs(struct mlx5e_hairpin *hp)
{
	int tt;

	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++)
		mlx5_core_destroy_tir(hp->func_mdev, hp->indir_tirn[tt]);
}

static void mlx5e_hairpin_set_ttc_params(struct mlx5e_hairpin *hp,
					 struct ttc_params *ttc_params)
{
	struct mlx5_flow_table_attr *ft_attr = &ttc_params->ft_attr;
	int tt;

	memset(ttc_params, 0, sizeof(*ttc_params));

	ttc_params->any_tt_tirn = hp->tirn;

	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++)
		ttc_params->indir_tirn[tt] = hp->indir_tirn[tt];

	ft_attr->max_fte = MLX5E_TTC_TABLE_SIZE;
	ft_attr->level = MLX5E_TC_TTC_FT_LEVEL;
	ft_attr->prio = MLX5E_TC_PRIO;
}

static int mlx5e_hairpin_rss_init(struct mlx5e_hairpin *hp)
{
	struct mlx5e_priv *priv = hp->func_priv;
	struct ttc_params ttc_params;
	int err;

	err = mlx5e_hairpin_create_indirect_rqt(hp);
	if (err)
		return err;

	err = mlx5e_hairpin_create_indirect_tirs(hp);
	if (err)
		goto err_create_indirect_tirs;

	mlx5e_hairpin_set_ttc_params(hp, &ttc_params);
	err = mlx5e_create_ttc_table(priv, &ttc_params, &hp->ttc);
	if (err)
		goto err_create_ttc_table;

	netdev_dbg(priv->netdev, "add hairpin: using %d channels rss ttc table id %x\n",
		   hp->num_channels, hp->ttc.ft.t->id);

	return 0;

err_create_ttc_table:
	mlx5e_hairpin_destroy_indirect_tirs(hp);
err_create_indirect_tirs:
	mlx5e_destroy_rqt(priv, &hp->indir_rqt);

	return err;
}

static void mlx5e_hairpin_rss_cleanup(struct mlx5e_hairpin *hp)
{
	struct mlx5e_priv *priv = hp->func_priv;

	mlx5e_destroy_ttc_table(priv, &hp->ttc);
	mlx5e_hairpin_destroy_indirect_tirs(hp);
	mlx5e_destroy_rqt(priv, &hp->indir_rqt);
}

static struct mlx5e_hairpin *
mlx5e_hairpin_create(struct mlx5e_priv *priv, struct mlx5_hairpin_params *params,
		     int peer_ifindex)
{
	struct mlx5_core_dev *func_mdev, *peer_mdev;
	struct mlx5e_hairpin *hp;
	struct mlx5_hairpin *pair;
	int err;

	hp = kzalloc(sizeof(*hp), GFP_KERNEL);
	if (!hp)
		return ERR_PTR(-ENOMEM);

	func_mdev = priv->mdev;
	peer_mdev = mlx5e_hairpin_get_mdev(dev_net(priv->netdev), peer_ifindex);

	pair = mlx5_core_hairpin_create(func_mdev, peer_mdev, params);
	if (IS_ERR(pair)) {
		err = PTR_ERR(pair);
		goto create_pair_err;
	}
	hp->pair = pair;
	hp->func_mdev = func_mdev;
	hp->func_priv = priv;
	hp->num_channels = params->num_channels;

	err = mlx5e_hairpin_create_transport(hp);
	if (err)
		goto create_transport_err;

	if (hp->num_channels > 1) {
		err = mlx5e_hairpin_rss_init(hp);
		if (err)
			goto rss_init_err;
	}

	return hp;

rss_init_err:
	mlx5e_hairpin_destroy_transport(hp);
create_transport_err:
	mlx5_core_hairpin_destroy(hp->pair);
create_pair_err:
	kfree(hp);
	return ERR_PTR(err);
}

static void mlx5e_hairpin_destroy(struct mlx5e_hairpin *hp)
{
	if (hp->num_channels > 1)
		mlx5e_hairpin_rss_cleanup(hp);
	mlx5e_hairpin_destroy_transport(hp);
	mlx5_core_hairpin_destroy(hp->pair);
	kvfree(hp);
}

static inline u32 hash_hairpin_info(u16 peer_vhca_id, u8 prio)
{
	return (peer_vhca_id << 16 | prio);
}

static struct mlx5e_hairpin_entry *mlx5e_hairpin_get(struct mlx5e_priv *priv,
						     u16 peer_vhca_id, u8 prio)
{
	struct mlx5e_hairpin_entry *hpe;
	u32 hash_key = hash_hairpin_info(peer_vhca_id, prio);

	hash_for_each_possible(priv->fs.tc.hairpin_tbl, hpe,
			       hairpin_hlist, hash_key) {
		if (hpe->peer_vhca_id == peer_vhca_id && hpe->prio == prio) {
			refcount_inc(&hpe->refcnt);
			return hpe;
		}
	}

	return NULL;
}

static void mlx5e_hairpin_put(struct mlx5e_priv *priv,
			      struct mlx5e_hairpin_entry *hpe)
{
	/* no more hairpin flows for us, release the hairpin pair */
	if (!refcount_dec_and_mutex_lock(&hpe->refcnt, &priv->fs.tc.hairpin_tbl_lock))
		return;
	hash_del(&hpe->hairpin_hlist);
	mutex_unlock(&priv->fs.tc.hairpin_tbl_lock);

	if (!IS_ERR_OR_NULL(hpe->hp)) {
		netdev_dbg(priv->netdev, "del hairpin: peer %s\n",
			   dev_name(hpe->hp->pair->peer_mdev->device));

		mlx5e_hairpin_destroy(hpe->hp);
	}

	WARN_ON(!list_empty(&hpe->flows));
	kfree(hpe);
}

#define UNKNOWN_MATCH_PRIO 8

static int mlx5e_hairpin_get_prio(struct mlx5e_priv *priv,
				  struct mlx5_flow_spec *spec, u8 *match_prio,
				  struct netlink_ext_ack *extack)
{
	void *headers_c, *headers_v;
	u8 prio_val, prio_mask = 0;
	bool vlan_present;

#ifdef CONFIG_MLX5_CORE_EN_DCB
	if (priv->dcbx_dp.trust_state != MLX5_QPTS_TRUST_PCP) {
		NL_SET_ERR_MSG_MOD(extack,
				   "only PCP trust state supported for hairpin");
		return -EOPNOTSUPP;
	}
#endif
	headers_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, outer_headers);
	headers_v = MLX5_ADDR_OF(fte_match_param, spec->match_value, outer_headers);

	vlan_present = MLX5_GET(fte_match_set_lyr_2_4, headers_v, cvlan_tag);
	if (vlan_present) {
		prio_mask = MLX5_GET(fte_match_set_lyr_2_4, headers_c, first_prio);
		prio_val = MLX5_GET(fte_match_set_lyr_2_4, headers_v, first_prio);
	}

	if (!vlan_present || !prio_mask) {
		prio_val = UNKNOWN_MATCH_PRIO;
	} else if (prio_mask != 0x7) {
		NL_SET_ERR_MSG_MOD(extack,
				   "masked priority match not supported for hairpin");
		return -EOPNOTSUPP;
	}

	*match_prio = prio_val;
	return 0;
}

static int mlx5e_hairpin_flow_add(struct mlx5e_priv *priv,
				  struct mlx5e_tc_flow *flow,
				  struct mlx5e_tc_flow_parse_attr *parse_attr,
				  struct netlink_ext_ack *extack)
{
	int peer_ifindex = parse_attr->mirred_ifindex[0];
	struct mlx5_hairpin_params params;
	struct mlx5_core_dev *peer_mdev;
	struct mlx5e_hairpin_entry *hpe;
	struct mlx5e_hairpin *hp;
	u64 link_speed64;
	u32 link_speed;
	u8 match_prio;
	u16 peer_id;
	int err;

	peer_mdev = mlx5e_hairpin_get_mdev(dev_net(priv->netdev), peer_ifindex);
	if (!MLX5_CAP_GEN(priv->mdev, hairpin) || !MLX5_CAP_GEN(peer_mdev, hairpin)) {
		NL_SET_ERR_MSG_MOD(extack, "hairpin is not supported");
		return -EOPNOTSUPP;
	}

	peer_id = MLX5_CAP_GEN(peer_mdev, vhca_id);
	err = mlx5e_hairpin_get_prio(priv, &parse_attr->spec, &match_prio,
				     extack);
	if (err)
		return err;

	mutex_lock(&priv->fs.tc.hairpin_tbl_lock);
	hpe = mlx5e_hairpin_get(priv, peer_id, match_prio);
	if (hpe) {
		mutex_unlock(&priv->fs.tc.hairpin_tbl_lock);
		wait_for_completion(&hpe->res_ready);

		if (IS_ERR(hpe->hp)) {
			err = -EREMOTEIO;
			goto out_err;
		}
		goto attach_flow;
	}

	hpe = kzalloc(sizeof(*hpe), GFP_KERNEL);
	if (!hpe) {
		mutex_unlock(&priv->fs.tc.hairpin_tbl_lock);
		return -ENOMEM;
	}

	spin_lock_init(&hpe->flows_lock);
	INIT_LIST_HEAD(&hpe->flows);
	INIT_LIST_HEAD(&hpe->dead_peer_wait_list);
	hpe->peer_vhca_id = peer_id;
	hpe->prio = match_prio;
	refcount_set(&hpe->refcnt, 1);
	init_completion(&hpe->res_ready);

	hash_add(priv->fs.tc.hairpin_tbl, &hpe->hairpin_hlist,
		 hash_hairpin_info(peer_id, match_prio));
	mutex_unlock(&priv->fs.tc.hairpin_tbl_lock);

	params.log_data_size = 15;
	params.log_data_size = min_t(u8, params.log_data_size,
				     MLX5_CAP_GEN(priv->mdev, log_max_hairpin_wq_data_sz));
	params.log_data_size = max_t(u8, params.log_data_size,
				     MLX5_CAP_GEN(priv->mdev, log_min_hairpin_wq_data_sz));

	params.log_num_packets = params.log_data_size -
				 MLX5_MPWRQ_MIN_LOG_STRIDE_SZ(priv->mdev);
	params.log_num_packets = min_t(u8, params.log_num_packets,
				       MLX5_CAP_GEN(priv->mdev, log_max_hairpin_num_packets));

	params.q_counter = priv->q_counter;
	/* set hairpin pair per each 50Gbs share of the link */
	mlx5e_port_max_linkspeed(priv->mdev, &link_speed);
	link_speed = max_t(u32, link_speed, 50000);
	link_speed64 = link_speed;
	do_div(link_speed64, 50000);
	params.num_channels = link_speed64;

	hp = mlx5e_hairpin_create(priv, &params, peer_ifindex);
	hpe->hp = hp;
	complete_all(&hpe->res_ready);
	if (IS_ERR(hp)) {
		err = PTR_ERR(hp);
		goto out_err;
	}

	netdev_dbg(priv->netdev, "add hairpin: tirn %x rqn %x peer %s sqn %x prio %d (log) data %d packets %d\n",
		   hp->tirn, hp->pair->rqn[0],
		   dev_name(hp->pair->peer_mdev->device),
		   hp->pair->sqn[0], match_prio, params.log_data_size, params.log_num_packets);

attach_flow:
	if (hpe->hp->num_channels > 1) {
		flow_flag_set(flow, HAIRPIN_RSS);
		flow->nic_attr->hairpin_ft = hpe->hp->ttc.ft.t;
	} else {
		flow->nic_attr->hairpin_tirn = hpe->hp->tirn;
	}

	flow->hpe = hpe;
	spin_lock(&hpe->flows_lock);
	list_add(&flow->hairpin, &hpe->flows);
	spin_unlock(&hpe->flows_lock);

	return 0;

out_err:
	mlx5e_hairpin_put(priv, hpe);
	return err;
}

static void mlx5e_hairpin_flow_del(struct mlx5e_priv *priv,
				   struct mlx5e_tc_flow *flow)
{
	/* flow wasn't fully initialized */
	if (!flow->hpe)
		return;

	spin_lock(&flow->hpe->flows_lock);
	list_del(&flow->hairpin);
	spin_unlock(&flow->hpe->flows_lock);

	mlx5e_hairpin_put(priv, flow->hpe);
	flow->hpe = NULL;
}

static int
mlx5e_tc_add_nic_flow(struct mlx5e_priv *priv,
		      struct mlx5e_tc_flow_parse_attr *parse_attr,
		      struct mlx5e_tc_flow *flow,
		      struct netlink_ext_ack *extack)
{
	struct mlx5_flow_context *flow_context = &parse_attr->spec.flow_context;
	struct mlx5_nic_flow_attr *attr = flow->nic_attr;
	struct mlx5_core_dev *dev = priv->mdev;
	struct mlx5_flow_destination dest[2] = {};
	struct mlx5_flow_act flow_act = {
		.action = attr->action,
		.flags    = FLOW_ACT_NO_APPEND,
	};
	struct mlx5_fc *counter = NULL;
	int err, dest_ix = 0;

	flow_context->flags |= FLOW_CONTEXT_HAS_TAG;
	flow_context->flow_tag = attr->flow_tag;

	if (flow_flag_test(flow, HAIRPIN)) {
		err = mlx5e_hairpin_flow_add(priv, flow, parse_attr, extack);
		if (err)
			return err;

		if (flow_flag_test(flow, HAIRPIN_RSS)) {
			dest[dest_ix].type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
			dest[dest_ix].ft = attr->hairpin_ft;
		} else {
			dest[dest_ix].type = MLX5_FLOW_DESTINATION_TYPE_TIR;
			dest[dest_ix].tir_num = attr->hairpin_tirn;
		}
		dest_ix++;
	} else if (attr->action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST) {
		dest[dest_ix].type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
		dest[dest_ix].ft = priv->fs.vlan.ft.t;
		dest_ix++;
	}

	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_COUNT) {
		counter = mlx5_fc_create(dev, true);
		if (IS_ERR(counter))
			return PTR_ERR(counter);

		dest[dest_ix].type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
		dest[dest_ix].counter_id = mlx5_fc_id(counter);
		dest_ix++;
		attr->counter = counter;
	}

	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR) {
		err = mlx5e_attach_mod_hdr(priv, flow, parse_attr);
		flow_act.modify_hdr = attr->modify_hdr;
		dealloc_mod_hdr_actions(&parse_attr->mod_hdr_acts);
		if (err)
			return err;
	}

	mutex_lock(&priv->fs.tc.t_lock);
	if (IS_ERR_OR_NULL(priv->fs.tc.t)) {
		struct mlx5_flow_table_attr ft_attr = {};
		int tc_grp_size, tc_tbl_size, tc_num_grps;
		u32 max_flow_counter;

		max_flow_counter = (MLX5_CAP_GEN(dev, max_flow_counter_31_16) << 16) |
				    MLX5_CAP_GEN(dev, max_flow_counter_15_0);

		tc_grp_size = min_t(int, max_flow_counter, MLX5E_TC_TABLE_MAX_GROUP_SIZE);

		tc_tbl_size = min_t(int, tc_grp_size * MLX5E_TC_TABLE_NUM_GROUPS,
				    BIT(MLX5_CAP_FLOWTABLE_NIC_RX(dev, log_max_ft_size)));
		tc_num_grps = MLX5E_TC_TABLE_NUM_GROUPS;

		ft_attr.prio = MLX5E_TC_PRIO;
		ft_attr.max_fte = tc_tbl_size;
		ft_attr.level = MLX5E_TC_FT_LEVEL;
		ft_attr.autogroup.max_num_groups = tc_num_grps;
		priv->fs.tc.t =
			mlx5_create_auto_grouped_flow_table(priv->fs.ns,
							    &ft_attr);
		if (IS_ERR(priv->fs.tc.t)) {
			mutex_unlock(&priv->fs.tc.t_lock);
			NL_SET_ERR_MSG_MOD(extack,
					   "Failed to create tc offload table");
			netdev_err(priv->netdev,
				   "Failed to create tc offload table\n");
			return PTR_ERR(priv->fs.tc.t);
		}
	}

	if (attr->match_level != MLX5_MATCH_NONE)
		parse_attr->spec.match_criteria_enable |= MLX5_MATCH_OUTER_HEADERS;

	flow->rule[0] = mlx5_add_flow_rules(priv->fs.tc.t, &parse_attr->spec,
					    &flow_act, dest, dest_ix);
	mutex_unlock(&priv->fs.tc.t_lock);

	return PTR_ERR_OR_ZERO(flow->rule[0]);
}

static void mlx5e_tc_del_nic_flow(struct mlx5e_priv *priv,
				  struct mlx5e_tc_flow *flow)
{
	struct mlx5_nic_flow_attr *attr = flow->nic_attr;
	struct mlx5_fc *counter = NULL;

	counter = attr->counter;
	if (!IS_ERR_OR_NULL(flow->rule[0]))
		mlx5_del_flow_rules(flow->rule[0]);
	mlx5_fc_destroy(priv->mdev, counter);

	mutex_lock(&priv->fs.tc.t_lock);
	if (!mlx5e_tc_num_filters(priv, MLX5_TC_FLAG(NIC_OFFLOAD)) && priv->fs.tc.t) {
		mlx5_destroy_flow_table(priv->fs.tc.t);
		priv->fs.tc.t = NULL;
	}
	mutex_unlock(&priv->fs.tc.t_lock);

	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR)
		mlx5e_detach_mod_hdr(priv, flow);

	if (flow_flag_test(flow, HAIRPIN))
		mlx5e_hairpin_flow_del(priv, flow);
}

static void mlx5e_detach_encap(struct mlx5e_priv *priv,
			       struct mlx5e_tc_flow *flow, int out_index);

static int mlx5e_attach_encap(struct mlx5e_priv *priv,
			      struct mlx5e_tc_flow *flow,
			      struct net_device *mirred_dev,
			      int out_index,
			      struct netlink_ext_ack *extack,
			      struct net_device **encap_dev,
			      bool *encap_valid);
static int mlx5e_attach_decap(struct mlx5e_priv *priv,
			      struct mlx5e_tc_flow *flow,
			      struct netlink_ext_ack *extack);
static void mlx5e_detach_decap(struct mlx5e_priv *priv,
			       struct mlx5e_tc_flow *flow);

static struct mlx5_flow_handle *
mlx5e_tc_offload_fdb_rules(struct mlx5_eswitch *esw,
			   struct mlx5e_tc_flow *flow,
			   struct mlx5_flow_spec *spec,
			   struct mlx5_esw_flow_attr *attr)
{
	struct mlx5e_tc_mod_hdr_acts *mod_hdr_acts;
	struct mlx5_flow_handle *rule;

	if (flow_flag_test(flow, CT)) {
		mod_hdr_acts = &attr->parse_attr->mod_hdr_acts;

		return mlx5_tc_ct_flow_offload(flow->priv, flow, spec, attr,
					       mod_hdr_acts);
	}

	rule = mlx5_eswitch_add_offloaded_rule(esw, spec, attr);
	if (IS_ERR(rule))
		return rule;

	if (attr->split_count) {
		flow->rule[1] = mlx5_eswitch_add_fwd_rule(esw, spec, attr);
		if (IS_ERR(flow->rule[1])) {
			mlx5_eswitch_del_offloaded_rule(esw, rule, attr);
			return flow->rule[1];
		}
	}

	return rule;
}

static void
mlx5e_tc_unoffload_fdb_rules(struct mlx5_eswitch *esw,
			     struct mlx5e_tc_flow *flow,
			     struct mlx5_esw_flow_attr *attr)
{
	flow_flag_clear(flow, OFFLOADED);

	if (flow_flag_test(flow, CT)) {
		mlx5_tc_ct_delete_flow(flow->priv, flow, attr);
		return;
	}

	if (attr->split_count)
		mlx5_eswitch_del_fwd_rule(esw, flow->rule[1], attr);

	mlx5_eswitch_del_offloaded_rule(esw, flow->rule[0], attr);
}

static struct mlx5_flow_handle *
mlx5e_tc_offload_to_slow_path(struct mlx5_eswitch *esw,
			      struct mlx5e_tc_flow *flow,
			      struct mlx5_flow_spec *spec)
{
	struct mlx5_esw_flow_attr slow_attr;
	struct mlx5_flow_handle *rule;

	memcpy(&slow_attr, flow->esw_attr, sizeof(slow_attr));
	slow_attr.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	slow_attr.split_count = 0;
	slow_attr.flags |= MLX5_ESW_ATTR_FLAG_SLOW_PATH;

	rule = mlx5e_tc_offload_fdb_rules(esw, flow, spec, &slow_attr);
	if (!IS_ERR(rule))
		flow_flag_set(flow, SLOW);

	return rule;
}

static void
mlx5e_tc_unoffload_from_slow_path(struct mlx5_eswitch *esw,
				  struct mlx5e_tc_flow *flow)
{
	struct mlx5_esw_flow_attr slow_attr;

	memcpy(&slow_attr, flow->esw_attr, sizeof(slow_attr));
	slow_attr.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	slow_attr.split_count = 0;
	slow_attr.flags |= MLX5_ESW_ATTR_FLAG_SLOW_PATH;
	mlx5e_tc_unoffload_fdb_rules(esw, flow, &slow_attr);
	flow_flag_clear(flow, SLOW);
}

/* Caller must obtain uplink_priv->unready_flows_lock mutex before calling this
 * function.
 */
static void unready_flow_add(struct mlx5e_tc_flow *flow,
			     struct list_head *unready_flows)
{
	flow_flag_set(flow, NOT_READY);
	list_add_tail(&flow->unready, unready_flows);
}

/* Caller must obtain uplink_priv->unready_flows_lock mutex before calling this
 * function.
 */
static void unready_flow_del(struct mlx5e_tc_flow *flow)
{
	list_del(&flow->unready);
	flow_flag_clear(flow, NOT_READY);
}

static void add_unready_flow(struct mlx5e_tc_flow *flow)
{
	struct mlx5_rep_uplink_priv *uplink_priv;
	struct mlx5e_rep_priv *rpriv;
	struct mlx5_eswitch *esw;

	esw = flow->priv->mdev->priv.eswitch;
	rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	uplink_priv = &rpriv->uplink_priv;

	mutex_lock(&uplink_priv->unready_flows_lock);
	unready_flow_add(flow, &uplink_priv->unready_flows);
	mutex_unlock(&uplink_priv->unready_flows_lock);
}

static void remove_unready_flow(struct mlx5e_tc_flow *flow)
{
	struct mlx5_rep_uplink_priv *uplink_priv;
	struct mlx5e_rep_priv *rpriv;
	struct mlx5_eswitch *esw;

	esw = flow->priv->mdev->priv.eswitch;
	rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	uplink_priv = &rpriv->uplink_priv;

	mutex_lock(&uplink_priv->unready_flows_lock);
	unready_flow_del(flow);
	mutex_unlock(&uplink_priv->unready_flows_lock);
}

static int
mlx5e_tc_add_fdb_flow(struct mlx5e_priv *priv,
		      struct mlx5e_tc_flow *flow,
		      struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_esw_flow_attr *attr = flow->esw_attr;
	struct mlx5e_tc_flow_parse_attr *parse_attr = attr->parse_attr;
	struct net_device *out_dev, *encap_dev = NULL;
	struct mlx5_fc *counter = NULL;
	struct mlx5e_rep_priv *rpriv;
	struct mlx5e_priv *out_priv;
	bool encap_valid = true;
	u32 max_prio, max_chain;
	int err = 0;
	int out_index;

	if (!mlx5_esw_chains_prios_supported(esw) && attr->prio != 1) {
		NL_SET_ERR_MSG_MOD(extack,
				   "E-switch priorities unsupported, upgrade FW");
		return -EOPNOTSUPP;
	}

	/* We check chain range only for tc flows.
	 * For ft flows, we checked attr->chain was originally 0 and set it to
	 * FDB_FT_CHAIN which is outside tc range.
	 * See mlx5e_rep_setup_ft_cb().
	 */
	max_chain = mlx5_esw_chains_get_chain_range(esw);
	if (!mlx5e_is_ft_flow(flow) && attr->chain > max_chain) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Requested chain is out of supported range");
		return -EOPNOTSUPP;
	}

	max_prio = mlx5_esw_chains_get_prio_range(esw);
	if (attr->prio > max_prio) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Requested priority is out of supported range");
		return -EOPNOTSUPP;
	}

	if (flow_flag_test(flow, L3_TO_L2_DECAP)) {
		err = mlx5e_attach_decap(priv, flow, extack);
		if (err)
			return err;
	}

	for (out_index = 0; out_index < MLX5_MAX_FLOW_FWD_VPORTS; out_index++) {
		int mirred_ifindex;

		if (!(attr->dests[out_index].flags & MLX5_ESW_DEST_ENCAP))
			continue;

		mirred_ifindex = parse_attr->mirred_ifindex[out_index];
		out_dev = __dev_get_by_index(dev_net(priv->netdev),
					     mirred_ifindex);
		err = mlx5e_attach_encap(priv, flow, out_dev, out_index,
					 extack, &encap_dev, &encap_valid);
		if (err)
			return err;

		out_priv = netdev_priv(encap_dev);
		rpriv = out_priv->ppriv;
		attr->dests[out_index].rep = rpriv->rep;
		attr->dests[out_index].mdev = out_priv->mdev;
	}

	err = mlx5_eswitch_add_vlan_action(esw, attr);
	if (err)
		return err;

	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR &&
	    !(attr->ct_attr.ct_action & TCA_CT_ACT_CLEAR)) {
		err = mlx5e_attach_mod_hdr(priv, flow, parse_attr);
		dealloc_mod_hdr_actions(&parse_attr->mod_hdr_acts);
		if (err)
			return err;
	}

	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_COUNT) {
		counter = mlx5_fc_create(attr->counter_dev, true);
		if (IS_ERR(counter))
			return PTR_ERR(counter);

		attr->counter = counter;
	}

	/* we get here if one of the following takes place:
	 * (1) there's no error
	 * (2) there's an encap action and we don't have valid neigh
	 */
	if (!encap_valid)
		flow->rule[0] = mlx5e_tc_offload_to_slow_path(esw, flow, &parse_attr->spec);
	else
		flow->rule[0] = mlx5e_tc_offload_fdb_rules(esw, flow, &parse_attr->spec, attr);

	if (IS_ERR(flow->rule[0]))
		return PTR_ERR(flow->rule[0]);
	else
		flow_flag_set(flow, OFFLOADED);

	return 0;
}

static bool mlx5_flow_has_geneve_opt(struct mlx5e_tc_flow *flow)
{
	struct mlx5_flow_spec *spec = &flow->esw_attr->parse_attr->spec;
	void *headers_v = MLX5_ADDR_OF(fte_match_param,
				       spec->match_value,
				       misc_parameters_3);
	u32 geneve_tlv_opt_0_data = MLX5_GET(fte_match_set_misc3,
					     headers_v,
					     geneve_tlv_option_0_data);

	return !!geneve_tlv_opt_0_data;
}

static void mlx5e_tc_del_fdb_flow(struct mlx5e_priv *priv,
				  struct mlx5e_tc_flow *flow)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_esw_flow_attr *attr = flow->esw_attr;
	int out_index;

	mlx5e_put_flow_tunnel_id(flow);

	if (flow_flag_test(flow, NOT_READY))
		remove_unready_flow(flow);

	if (mlx5e_is_offloaded_flow(flow)) {
		if (flow_flag_test(flow, SLOW))
			mlx5e_tc_unoffload_from_slow_path(esw, flow);
		else
			mlx5e_tc_unoffload_fdb_rules(esw, flow, attr);
	}

	if (mlx5_flow_has_geneve_opt(flow))
		mlx5_geneve_tlv_option_del(priv->mdev->geneve);

	mlx5_eswitch_del_vlan_action(esw, attr);

	for (out_index = 0; out_index < MLX5_MAX_FLOW_FWD_VPORTS; out_index++)
		if (attr->dests[out_index].flags & MLX5_ESW_DEST_ENCAP) {
			mlx5e_detach_encap(priv, flow, out_index);
			kfree(attr->parse_attr->tun_info[out_index]);
		}
	kvfree(attr->parse_attr);

	mlx5_tc_ct_match_del(priv, &flow->esw_attr->ct_attr);

	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR)
		mlx5e_detach_mod_hdr(priv, flow);

	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_COUNT)
		mlx5_fc_destroy(attr->counter_dev, attr->counter);

	if (flow_flag_test(flow, L3_TO_L2_DECAP))
		mlx5e_detach_decap(priv, flow);
}

void mlx5e_tc_encap_flows_add(struct mlx5e_priv *priv,
			      struct mlx5e_encap_entry *e,
			      struct list_head *flow_list)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_esw_flow_attr *esw_attr;
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	struct mlx5e_tc_flow *flow;
	int err;

	e->pkt_reformat = mlx5_packet_reformat_alloc(priv->mdev,
						     e->reformat_type,
						     e->encap_size, e->encap_header,
						     MLX5_FLOW_NAMESPACE_FDB);
	if (IS_ERR(e->pkt_reformat)) {
		mlx5_core_warn(priv->mdev, "Failed to offload cached encapsulation header, %lu\n",
			       PTR_ERR(e->pkt_reformat));
		return;
	}
	e->flags |= MLX5_ENCAP_ENTRY_VALID;
	mlx5e_rep_queue_neigh_stats_work(priv);

	list_for_each_entry(flow, flow_list, tmp_list) {
		bool all_flow_encaps_valid = true;
		int i;

		if (!mlx5e_is_offloaded_flow(flow))
			continue;
		esw_attr = flow->esw_attr;
		spec = &esw_attr->parse_attr->spec;

		esw_attr->dests[flow->tmp_efi_index].pkt_reformat = e->pkt_reformat;
		esw_attr->dests[flow->tmp_efi_index].flags |= MLX5_ESW_DEST_ENCAP_VALID;
		/* Flow can be associated with multiple encap entries.
		 * Before offloading the flow verify that all of them have
		 * a valid neighbour.
		 */
		for (i = 0; i < MLX5_MAX_FLOW_FWD_VPORTS; i++) {
			if (!(esw_attr->dests[i].flags & MLX5_ESW_DEST_ENCAP))
				continue;
			if (!(esw_attr->dests[i].flags & MLX5_ESW_DEST_ENCAP_VALID)) {
				all_flow_encaps_valid = false;
				break;
			}
		}
		/* Do not offload flows with unresolved neighbors */
		if (!all_flow_encaps_valid)
			continue;
		/* update from slow path rule to encap rule */
		rule = mlx5e_tc_offload_fdb_rules(esw, flow, spec, esw_attr);
		if (IS_ERR(rule)) {
			err = PTR_ERR(rule);
			mlx5_core_warn(priv->mdev, "Failed to update cached encapsulation flow, %d\n",
				       err);
			continue;
		}

		mlx5e_tc_unoffload_from_slow_path(esw, flow);
		flow->rule[0] = rule;
		/* was unset when slow path rule removed */
		flow_flag_set(flow, OFFLOADED);
	}
}

void mlx5e_tc_encap_flows_del(struct mlx5e_priv *priv,
			      struct mlx5e_encap_entry *e,
			      struct list_head *flow_list)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	struct mlx5e_tc_flow *flow;
	int err;

	list_for_each_entry(flow, flow_list, tmp_list) {
		if (!mlx5e_is_offloaded_flow(flow))
			continue;
		spec = &flow->esw_attr->parse_attr->spec;

		/* update from encap rule to slow path rule */
		rule = mlx5e_tc_offload_to_slow_path(esw, flow, spec);
		/* mark the flow's encap dest as non-valid */
		flow->esw_attr->dests[flow->tmp_efi_index].flags &= ~MLX5_ESW_DEST_ENCAP_VALID;

		if (IS_ERR(rule)) {
			err = PTR_ERR(rule);
			mlx5_core_warn(priv->mdev, "Failed to update slow path (encap) flow, %d\n",
				       err);
			continue;
		}

		mlx5e_tc_unoffload_fdb_rules(esw, flow, flow->esw_attr);
		flow->rule[0] = rule;
		/* was unset when fast path rule removed */
		flow_flag_set(flow, OFFLOADED);
	}

	/* we know that the encap is valid */
	e->flags &= ~MLX5_ENCAP_ENTRY_VALID;
	mlx5_packet_reformat_dealloc(priv->mdev, e->pkt_reformat);
}

static struct mlx5_fc *mlx5e_tc_get_counter(struct mlx5e_tc_flow *flow)
{
	if (mlx5e_is_eswitch_flow(flow))
		return flow->esw_attr->counter;
	else
		return flow->nic_attr->counter;
}

/* Takes reference to all flows attached to encap and adds the flows to
 * flow_list using 'tmp_list' list_head in mlx5e_tc_flow.
 */
void mlx5e_take_all_encap_flows(struct mlx5e_encap_entry *e, struct list_head *flow_list)
{
	struct encap_flow_item *efi;
	struct mlx5e_tc_flow *flow;

	list_for_each_entry(efi, &e->flows, list) {
		flow = container_of(efi, struct mlx5e_tc_flow, encaps[efi->index]);
		if (IS_ERR(mlx5e_flow_get(flow)))
			continue;
		wait_for_completion(&flow->init_done);

		flow->tmp_efi_index = efi->index;
		list_add(&flow->tmp_list, flow_list);
	}
}

/* Iterate over tmp_list of flows attached to flow_list head. */
void mlx5e_put_encap_flow_list(struct mlx5e_priv *priv, struct list_head *flow_list)
{
	struct mlx5e_tc_flow *flow, *tmp;

	list_for_each_entry_safe(flow, tmp, flow_list, tmp_list)
		mlx5e_flow_put(priv, flow);
}

static struct mlx5e_encap_entry *
mlx5e_get_next_valid_encap(struct mlx5e_neigh_hash_entry *nhe,
			   struct mlx5e_encap_entry *e)
{
	struct mlx5e_encap_entry *next = NULL;

retry:
	rcu_read_lock();

	/* find encap with non-zero reference counter value */
	for (next = e ?
		     list_next_or_null_rcu(&nhe->encap_list,
					   &e->encap_list,
					   struct mlx5e_encap_entry,
					   encap_list) :
		     list_first_or_null_rcu(&nhe->encap_list,
					    struct mlx5e_encap_entry,
					    encap_list);
	     next;
	     next = list_next_or_null_rcu(&nhe->encap_list,
					  &next->encap_list,
					  struct mlx5e_encap_entry,
					  encap_list))
		if (mlx5e_encap_take(next))
			break;

	rcu_read_unlock();

	/* release starting encap */
	if (e)
		mlx5e_encap_put(netdev_priv(e->out_dev), e);
	if (!next)
		return next;

	/* wait for encap to be fully initialized */
	wait_for_completion(&next->res_ready);
	/* continue searching if encap entry is not in valid state after completion */
	if (!(next->flags & MLX5_ENCAP_ENTRY_VALID)) {
		e = next;
		goto retry;
	}

	return next;
}

void mlx5e_tc_update_neigh_used_value(struct mlx5e_neigh_hash_entry *nhe)
{
	struct mlx5e_neigh *m_neigh = &nhe->m_neigh;
	struct mlx5e_encap_entry *e = NULL;
	struct mlx5e_tc_flow *flow;
	struct mlx5_fc *counter;
	struct neigh_table *tbl;
	bool neigh_used = false;
	struct neighbour *n;
	u64 lastuse;

	if (m_neigh->family == AF_INET)
		tbl = &arp_tbl;
#if IS_ENABLED(CONFIG_IPV6)
	else if (m_neigh->family == AF_INET6)
		tbl = ipv6_stub->nd_tbl;
#endif
	else
		return;

	/* mlx5e_get_next_valid_encap() releases previous encap before returning
	 * next one.
	 */
	while ((e = mlx5e_get_next_valid_encap(nhe, e)) != NULL) {
		struct mlx5e_priv *priv = netdev_priv(e->out_dev);
		struct encap_flow_item *efi, *tmp;
		struct mlx5_eswitch *esw;
		LIST_HEAD(flow_list);

		esw = priv->mdev->priv.eswitch;
		mutex_lock(&esw->offloads.encap_tbl_lock);
		list_for_each_entry_safe(efi, tmp, &e->flows, list) {
			flow = container_of(efi, struct mlx5e_tc_flow,
					    encaps[efi->index]);
			if (IS_ERR(mlx5e_flow_get(flow)))
				continue;
			list_add(&flow->tmp_list, &flow_list);

			if (mlx5e_is_offloaded_flow(flow)) {
				counter = mlx5e_tc_get_counter(flow);
				lastuse = mlx5_fc_query_lastuse(counter);
				if (time_after((unsigned long)lastuse, nhe->reported_lastuse)) {
					neigh_used = true;
					break;
				}
			}
		}
		mutex_unlock(&esw->offloads.encap_tbl_lock);

		mlx5e_put_encap_flow_list(priv, &flow_list);
		if (neigh_used) {
			/* release current encap before breaking the loop */
			mlx5e_encap_put(priv, e);
			break;
		}
	}

	trace_mlx5e_tc_update_neigh_used_value(nhe, neigh_used);

	if (neigh_used) {
		nhe->reported_lastuse = jiffies;

		/* find the relevant neigh according to the cached device and
		 * dst ip pair
		 */
		n = neigh_lookup(tbl, &m_neigh->dst_ip, m_neigh->dev);
		if (!n)
			return;

		neigh_event_send(n, NULL);
		neigh_release(n);
	}
}

static void mlx5e_encap_dealloc(struct mlx5e_priv *priv, struct mlx5e_encap_entry *e)
{
	WARN_ON(!list_empty(&e->flows));

	if (e->compl_result > 0) {
		mlx5e_rep_encap_entry_detach(netdev_priv(e->out_dev), e);

		if (e->flags & MLX5_ENCAP_ENTRY_VALID)
			mlx5_packet_reformat_dealloc(priv->mdev, e->pkt_reformat);
	}

	kfree(e->tun_info);
	kfree(e->encap_header);
	kfree_rcu(e, rcu);
}

static void mlx5e_decap_dealloc(struct mlx5e_priv *priv,
				struct mlx5e_decap_entry *d)
{
	WARN_ON(!list_empty(&d->flows));

	if (!d->compl_result)
		mlx5_packet_reformat_dealloc(priv->mdev, d->pkt_reformat);

	kfree_rcu(d, rcu);
}

void mlx5e_encap_put(struct mlx5e_priv *priv, struct mlx5e_encap_entry *e)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;

	if (!refcount_dec_and_mutex_lock(&e->refcnt, &esw->offloads.encap_tbl_lock))
		return;
	hash_del_rcu(&e->encap_hlist);
	mutex_unlock(&esw->offloads.encap_tbl_lock);

	mlx5e_encap_dealloc(priv, e);
}

static void mlx5e_decap_put(struct mlx5e_priv *priv, struct mlx5e_decap_entry *d)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;

	if (!refcount_dec_and_mutex_lock(&d->refcnt, &esw->offloads.decap_tbl_lock))
		return;
	hash_del_rcu(&d->hlist);
	mutex_unlock(&esw->offloads.decap_tbl_lock);

	mlx5e_decap_dealloc(priv, d);
}

static void mlx5e_detach_encap(struct mlx5e_priv *priv,
			       struct mlx5e_tc_flow *flow, int out_index)
{
	struct mlx5e_encap_entry *e = flow->encaps[out_index].e;
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;

	/* flow wasn't fully initialized */
	if (!e)
		return;

	mutex_lock(&esw->offloads.encap_tbl_lock);
	list_del(&flow->encaps[out_index].list);
	flow->encaps[out_index].e = NULL;
	if (!refcount_dec_and_test(&e->refcnt)) {
		mutex_unlock(&esw->offloads.encap_tbl_lock);
		return;
	}
	hash_del_rcu(&e->encap_hlist);
	mutex_unlock(&esw->offloads.encap_tbl_lock);

	mlx5e_encap_dealloc(priv, e);
}

static void mlx5e_detach_decap(struct mlx5e_priv *priv,
			       struct mlx5e_tc_flow *flow)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_decap_entry *d = flow->decap_reformat;

	if (!d)
		return;

	mutex_lock(&esw->offloads.decap_tbl_lock);
	list_del(&flow->l3_to_l2_reformat);
	flow->decap_reformat = NULL;

	if (!refcount_dec_and_test(&d->refcnt)) {
		mutex_unlock(&esw->offloads.decap_tbl_lock);
		return;
	}
	hash_del_rcu(&d->hlist);
	mutex_unlock(&esw->offloads.decap_tbl_lock);

	mlx5e_decap_dealloc(priv, d);
}

static void __mlx5e_tc_del_fdb_peer_flow(struct mlx5e_tc_flow *flow)
{
	struct mlx5_eswitch *esw = flow->priv->mdev->priv.eswitch;

	if (!flow_flag_test(flow, ESWITCH) ||
	    !flow_flag_test(flow, DUP))
		return;

	mutex_lock(&esw->offloads.peer_mutex);
	list_del(&flow->peer);
	mutex_unlock(&esw->offloads.peer_mutex);

	flow_flag_clear(flow, DUP);

	if (refcount_dec_and_test(&flow->peer_flow->refcnt)) {
		mlx5e_tc_del_fdb_flow(flow->peer_flow->priv, flow->peer_flow);
		kfree(flow->peer_flow);
	}

	flow->peer_flow = NULL;
}

static void mlx5e_tc_del_fdb_peer_flow(struct mlx5e_tc_flow *flow)
{
	struct mlx5_core_dev *dev = flow->priv->mdev;
	struct mlx5_devcom *devcom = dev->priv.devcom;
	struct mlx5_eswitch *peer_esw;

	peer_esw = mlx5_devcom_get_peer_data(devcom, MLX5_DEVCOM_ESW_OFFLOADS);
	if (!peer_esw)
		return;

	__mlx5e_tc_del_fdb_peer_flow(flow);
	mlx5_devcom_release_peer_data(devcom, MLX5_DEVCOM_ESW_OFFLOADS);
}

static void mlx5e_tc_del_flow(struct mlx5e_priv *priv,
			      struct mlx5e_tc_flow *flow)
{
	if (mlx5e_is_eswitch_flow(flow)) {
		mlx5e_tc_del_fdb_peer_flow(flow);
		mlx5e_tc_del_fdb_flow(priv, flow);
	} else {
		mlx5e_tc_del_nic_flow(priv, flow);
	}
}

static int flow_has_tc_fwd_action(struct flow_cls_offload *f)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct flow_action *flow_action = &rule->action;
	const struct flow_action_entry *act;
	int i;

	flow_action_for_each(i, act, flow_action) {
		switch (act->id) {
		case FLOW_ACTION_GOTO:
			return true;
		default:
			continue;
		}
	}

	return false;
}

static int
enc_opts_is_dont_care_or_full_match(struct mlx5e_priv *priv,
				    struct flow_dissector_key_enc_opts *opts,
				    struct netlink_ext_ack *extack,
				    bool *dont_care)
{
	struct geneve_opt *opt;
	int off = 0;

	*dont_care = true;

	while (opts->len > off) {
		opt = (struct geneve_opt *)&opts->data[off];

		if (!(*dont_care) || opt->opt_class || opt->type ||
		    memchr_inv(opt->opt_data, 0, opt->length * 4)) {
			*dont_care = false;

			if (opt->opt_class != htons(U16_MAX) ||
			    opt->type != U8_MAX) {
				NL_SET_ERR_MSG(extack,
					       "Partial match of tunnel options in chain > 0 isn't supported");
				netdev_warn(priv->netdev,
					    "Partial match of tunnel options in chain > 0 isn't supported");
				return -EOPNOTSUPP;
			}
		}

		off += sizeof(struct geneve_opt) + opt->length * 4;
	}

	return 0;
}

#define COPY_DISSECTOR(rule, diss_key, dst)\
({ \
	struct flow_rule *__rule = (rule);\
	typeof(dst) __dst = dst;\
\
	memcpy(__dst,\
	       skb_flow_dissector_target(__rule->match.dissector,\
					 diss_key,\
					 __rule->match.key),\
	       sizeof(*__dst));\
})

static int mlx5e_get_flow_tunnel_id(struct mlx5e_priv *priv,
				    struct mlx5e_tc_flow *flow,
				    struct flow_cls_offload *f,
				    struct net_device *filter_dev)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct netlink_ext_ack *extack = f->common.extack;
	struct mlx5_esw_flow_attr *attr = flow->esw_attr;
	struct mlx5e_tc_mod_hdr_acts *mod_hdr_acts;
	struct flow_match_enc_opts enc_opts_match;
	struct tunnel_match_enc_opts tun_enc_opts;
	struct mlx5_rep_uplink_priv *uplink_priv;
	struct mlx5e_rep_priv *uplink_rpriv;
	struct tunnel_match_key tunnel_key;
	bool enc_opts_is_dont_care = true;
	u32 tun_id, enc_opts_id = 0;
	struct mlx5_eswitch *esw;
	u32 value, mask;
	int err;

	esw = priv->mdev->priv.eswitch;
	uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	uplink_priv = &uplink_rpriv->uplink_priv;

	memset(&tunnel_key, 0, sizeof(tunnel_key));
	COPY_DISSECTOR(rule, FLOW_DISSECTOR_KEY_ENC_CONTROL,
		       &tunnel_key.enc_control);
	if (tunnel_key.enc_control.addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS)
		COPY_DISSECTOR(rule, FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS,
			       &tunnel_key.enc_ipv4);
	else
		COPY_DISSECTOR(rule, FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS,
			       &tunnel_key.enc_ipv6);
	COPY_DISSECTOR(rule, FLOW_DISSECTOR_KEY_ENC_IP, &tunnel_key.enc_ip);
	COPY_DISSECTOR(rule, FLOW_DISSECTOR_KEY_ENC_PORTS,
		       &tunnel_key.enc_tp);
	COPY_DISSECTOR(rule, FLOW_DISSECTOR_KEY_ENC_KEYID,
		       &tunnel_key.enc_key_id);
	tunnel_key.filter_ifindex = filter_dev->ifindex;

	err = mapping_add(uplink_priv->tunnel_mapping, &tunnel_key, &tun_id);
	if (err)
		return err;

	flow_rule_match_enc_opts(rule, &enc_opts_match);
	err = enc_opts_is_dont_care_or_full_match(priv,
						  enc_opts_match.mask,
						  extack,
						  &enc_opts_is_dont_care);
	if (err)
		goto err_enc_opts;

	if (!enc_opts_is_dont_care) {
		memset(&tun_enc_opts, 0, sizeof(tun_enc_opts));
		memcpy(&tun_enc_opts.key, enc_opts_match.key,
		       sizeof(*enc_opts_match.key));
		memcpy(&tun_enc_opts.mask, enc_opts_match.mask,
		       sizeof(*enc_opts_match.mask));

		err = mapping_add(uplink_priv->tunnel_enc_opts_mapping,
				  &tun_enc_opts, &enc_opts_id);
		if (err)
			goto err_enc_opts;
	}

	value = tun_id << ENC_OPTS_BITS | enc_opts_id;
	mask = enc_opts_id ? TUNNEL_ID_MASK :
			     (TUNNEL_ID_MASK & ~ENC_OPTS_BITS_MASK);

	if (attr->chain) {
		mlx5e_tc_match_to_reg_match(&attr->parse_attr->spec,
					    TUNNEL_TO_REG, value, mask);
	} else {
		mod_hdr_acts = &attr->parse_attr->mod_hdr_acts;
		err = mlx5e_tc_match_to_reg_set(priv->mdev,
						mod_hdr_acts,
						TUNNEL_TO_REG, value);
		if (err)
			goto err_set;

		attr->action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
	}

	flow->tunnel_id = value;
	return 0;

err_set:
	if (enc_opts_id)
		mapping_remove(uplink_priv->tunnel_enc_opts_mapping,
			       enc_opts_id);
err_enc_opts:
	mapping_remove(uplink_priv->tunnel_mapping, tun_id);
	return err;
}

static void mlx5e_put_flow_tunnel_id(struct mlx5e_tc_flow *flow)
{
	u32 enc_opts_id = flow->tunnel_id & ENC_OPTS_BITS_MASK;
	u32 tun_id = flow->tunnel_id >> ENC_OPTS_BITS;
	struct mlx5_rep_uplink_priv *uplink_priv;
	struct mlx5e_rep_priv *uplink_rpriv;
	struct mlx5_eswitch *esw;

	esw = flow->priv->mdev->priv.eswitch;
	uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	uplink_priv = &uplink_rpriv->uplink_priv;

	if (tun_id)
		mapping_remove(uplink_priv->tunnel_mapping, tun_id);
	if (enc_opts_id)
		mapping_remove(uplink_priv->tunnel_enc_opts_mapping,
			       enc_opts_id);
}

u32 mlx5e_tc_get_flow_tun_id(struct mlx5e_tc_flow *flow)
{
	return flow->tunnel_id;
}

void mlx5e_tc_set_ethertype(struct mlx5_core_dev *mdev,
			    struct flow_match_basic *match, bool outer,
			    void *headers_c, void *headers_v)
{
	bool ip_version_cap;

	ip_version_cap = outer ?
		MLX5_CAP_FLOWTABLE_NIC_RX(mdev,
					  ft_field_support.outer_ip_version) :
		MLX5_CAP_FLOWTABLE_NIC_RX(mdev,
					  ft_field_support.inner_ip_version);

	if (ip_version_cap && match->mask->n_proto == htons(0xFFFF) &&
	    (match->key->n_proto == htons(ETH_P_IP) ||
	     match->key->n_proto == htons(ETH_P_IPV6))) {
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, ip_version);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_version,
			 match->key->n_proto == htons(ETH_P_IP) ? 4 : 6);
	} else {
		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ethertype,
			 ntohs(match->mask->n_proto));
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ethertype,
			 ntohs(match->key->n_proto));
	}
}

static int parse_tunnel_attr(struct mlx5e_priv *priv,
			     struct mlx5e_tc_flow *flow,
			     struct mlx5_flow_spec *spec,
			     struct flow_cls_offload *f,
			     struct net_device *filter_dev,
			     u8 *match_level,
			     bool *match_inner)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct netlink_ext_ack *extack = f->common.extack;
	bool needs_mapping, sets_mapping;
	int err;

	if (!mlx5e_is_eswitch_flow(flow))
		return -EOPNOTSUPP;

	needs_mapping = !!flow->esw_attr->chain;
	sets_mapping = !flow->esw_attr->chain && flow_has_tc_fwd_action(f);
	*match_inner = !needs_mapping;

	if ((needs_mapping || sets_mapping) &&
	    !mlx5_eswitch_reg_c1_loopback_enabled(esw)) {
		NL_SET_ERR_MSG(extack,
			       "Chains on tunnel devices isn't supported without register loopback support");
		netdev_warn(priv->netdev,
			    "Chains on tunnel devices isn't supported without register loopback support");
		return -EOPNOTSUPP;
	}

	if (!flow->esw_attr->chain) {
		err = mlx5e_tc_tun_parse(filter_dev, priv, spec, f,
					 match_level);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Failed to parse tunnel attributes");
			netdev_warn(priv->netdev,
				    "Failed to parse tunnel attributes");
			return err;
		}

		/* With mpls over udp we decapsulate using packet reformat
		 * object
		 */
		if (!netif_is_bareudp(filter_dev))
			flow->esw_attr->action |= MLX5_FLOW_CONTEXT_ACTION_DECAP;
	}

	if (!needs_mapping && !sets_mapping)
		return 0;

	return mlx5e_get_flow_tunnel_id(priv, flow, f, filter_dev);
}

static void *get_match_inner_headers_criteria(struct mlx5_flow_spec *spec)
{
	return MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
			    inner_headers);
}

static void *get_match_inner_headers_value(struct mlx5_flow_spec *spec)
{
	return MLX5_ADDR_OF(fte_match_param, spec->match_value,
			    inner_headers);
}

static void *get_match_outer_headers_criteria(struct mlx5_flow_spec *spec)
{
	return MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
			    outer_headers);
}

static void *get_match_outer_headers_value(struct mlx5_flow_spec *spec)
{
	return MLX5_ADDR_OF(fte_match_param, spec->match_value,
			    outer_headers);
}

static void *get_match_headers_value(u32 flags,
				     struct mlx5_flow_spec *spec)
{
	return (flags & MLX5_FLOW_CONTEXT_ACTION_DECAP) ?
		get_match_inner_headers_value(spec) :
		get_match_outer_headers_value(spec);
}

static void *get_match_headers_criteria(u32 flags,
					struct mlx5_flow_spec *spec)
{
	return (flags & MLX5_FLOW_CONTEXT_ACTION_DECAP) ?
		get_match_inner_headers_criteria(spec) :
		get_match_outer_headers_criteria(spec);
}

static int mlx5e_flower_parse_meta(struct net_device *filter_dev,
				   struct flow_cls_offload *f)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct netlink_ext_ack *extack = f->common.extack;
	struct net_device *ingress_dev;
	struct flow_match_meta match;

	if (!flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_META))
		return 0;

	flow_rule_match_meta(rule, &match);
	if (match.mask->ingress_ifindex != 0xFFFFFFFF) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported ingress ifindex mask");
		return -EOPNOTSUPP;
	}

	ingress_dev = __dev_get_by_index(dev_net(filter_dev),
					 match.key->ingress_ifindex);
	if (!ingress_dev) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Can't find the ingress port to match on");
		return -ENOENT;
	}

	if (ingress_dev != filter_dev) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Can't match on the ingress filter port");
		return -EOPNOTSUPP;
	}

	return 0;
}

static bool skip_key_basic(struct net_device *filter_dev,
			   struct flow_cls_offload *f)
{
	/* When doing mpls over udp decap, the user needs to provide
	 * MPLS_UC as the protocol in order to be able to match on mpls
	 * label fields.  However, the actual ethertype is IP so we want to
	 * avoid matching on this, otherwise we'll fail the match.
	 */
	if (netif_is_bareudp(filter_dev) && f->common.chain_index == 0)
		return true;

	return false;
}

static int __parse_cls_flower(struct mlx5e_priv *priv,
			      struct mlx5e_tc_flow *flow,
			      struct mlx5_flow_spec *spec,
			      struct flow_cls_offload *f,
			      struct net_device *filter_dev,
			      u8 *inner_match_level, u8 *outer_match_level)
{
	struct netlink_ext_ack *extack = f->common.extack;
	void *headers_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				       outer_headers);
	void *headers_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				       outer_headers);
	void *misc_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				    misc_parameters);
	void *misc_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    misc_parameters);
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct flow_dissector *dissector = rule->match.dissector;
	u16 addr_type = 0;
	u8 ip_proto = 0;
	u8 *match_level;
	int err;

	match_level = outer_match_level;

	if (dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_META) |
	      BIT(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_VLAN) |
	      BIT(FLOW_DISSECTOR_KEY_CVLAN) |
	      BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_PORTS) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_KEYID) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_PORTS)	|
	      BIT(FLOW_DISSECTOR_KEY_ENC_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_TCP) |
	      BIT(FLOW_DISSECTOR_KEY_IP)  |
	      BIT(FLOW_DISSECTOR_KEY_CT) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_IP) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_OPTS) |
	      BIT(FLOW_DISSECTOR_KEY_MPLS))) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported key");
		netdev_warn(priv->netdev, "Unsupported key used: 0x%x\n",
			    dissector->used_keys);
		return -EOPNOTSUPP;
	}

	if (mlx5e_get_tc_tun(filter_dev)) {
		bool match_inner = false;

		err = parse_tunnel_attr(priv, flow, spec, f, filter_dev,
					outer_match_level, &match_inner);
		if (err)
			return err;

		if (match_inner) {
			/* header pointers should point to the inner headers
			 * if the packet was decapsulated already.
			 * outer headers are set by parse_tunnel_attr.
			 */
			match_level = inner_match_level;
			headers_c = get_match_inner_headers_criteria(spec);
			headers_v = get_match_inner_headers_value(spec);
		}
	}

	err = mlx5e_flower_parse_meta(filter_dev, f);
	if (err)
		return err;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC) &&
	    !skip_key_basic(filter_dev, f)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		mlx5e_tc_set_ethertype(priv->mdev, &match,
				       match_level == outer_match_level,
				       headers_c, headers_v);

		if (match.mask->n_proto)
			*match_level = MLX5_MATCH_L2;
	}
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN) ||
	    is_vlan_dev(filter_dev)) {
		struct flow_dissector_key_vlan filter_dev_mask;
		struct flow_dissector_key_vlan filter_dev_key;
		struct flow_match_vlan match;

		if (is_vlan_dev(filter_dev)) {
			match.key = &filter_dev_key;
			match.key->vlan_id = vlan_dev_vlan_id(filter_dev);
			match.key->vlan_tpid = vlan_dev_vlan_proto(filter_dev);
			match.key->vlan_priority = 0;
			match.mask = &filter_dev_mask;
			memset(match.mask, 0xff, sizeof(*match.mask));
			match.mask->vlan_priority = 0;
		} else {
			flow_rule_match_vlan(rule, &match);
		}
		if (match.mask->vlan_id ||
		    match.mask->vlan_priority ||
		    match.mask->vlan_tpid) {
			if (match.key->vlan_tpid == htons(ETH_P_8021AD)) {
				MLX5_SET(fte_match_set_lyr_2_4, headers_c,
					 svlan_tag, 1);
				MLX5_SET(fte_match_set_lyr_2_4, headers_v,
					 svlan_tag, 1);
			} else {
				MLX5_SET(fte_match_set_lyr_2_4, headers_c,
					 cvlan_tag, 1);
				MLX5_SET(fte_match_set_lyr_2_4, headers_v,
					 cvlan_tag, 1);
			}

			MLX5_SET(fte_match_set_lyr_2_4, headers_c, first_vid,
				 match.mask->vlan_id);
			MLX5_SET(fte_match_set_lyr_2_4, headers_v, first_vid,
				 match.key->vlan_id);

			MLX5_SET(fte_match_set_lyr_2_4, headers_c, first_prio,
				 match.mask->vlan_priority);
			MLX5_SET(fte_match_set_lyr_2_4, headers_v, first_prio,
				 match.key->vlan_priority);

			*match_level = MLX5_MATCH_L2;
		}
	} else if (*match_level != MLX5_MATCH_NONE) {
		/* cvlan_tag enabled in match criteria and
		 * disabled in match value means both S & C tags
		 * don't exist (untagged of both)
		 */
		MLX5_SET(fte_match_set_lyr_2_4, headers_c, cvlan_tag, 1);
		*match_level = MLX5_MATCH_L2;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CVLAN)) {
		struct flow_match_vlan match;

		flow_rule_match_cvlan(rule, &match);
		if (match.mask->vlan_id ||
		    match.mask->vlan_priority ||
		    match.mask->vlan_tpid) {
			if (match.key->vlan_tpid == htons(ETH_P_8021AD)) {
				MLX5_SET(fte_match_set_misc, misc_c,
					 outer_second_svlan_tag, 1);
				MLX5_SET(fte_match_set_misc, misc_v,
					 outer_second_svlan_tag, 1);
			} else {
				MLX5_SET(fte_match_set_misc, misc_c,
					 outer_second_cvlan_tag, 1);
				MLX5_SET(fte_match_set_misc, misc_v,
					 outer_second_cvlan_tag, 1);
			}

			MLX5_SET(fte_match_set_misc, misc_c, outer_second_vid,
				 match.mask->vlan_id);
			MLX5_SET(fte_match_set_misc, misc_v, outer_second_vid,
				 match.key->vlan_id);
			MLX5_SET(fte_match_set_misc, misc_c, outer_second_prio,
				 match.mask->vlan_priority);
			MLX5_SET(fte_match_set_misc, misc_v, outer_second_prio,
				 match.key->vlan_priority);

			*match_level = MLX5_MATCH_L2;
			spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS;
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match;

		flow_rule_match_eth_addrs(rule, &match);
		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
					     dmac_47_16),
				match.mask->dst);
		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
					     dmac_47_16),
				match.key->dst);

		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
					     smac_47_16),
				match.mask->src);
		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
					     smac_47_16),
				match.key->src);

		if (!is_zero_ether_addr(match.mask->src) ||
		    !is_zero_ether_addr(match.mask->dst))
			*match_level = MLX5_MATCH_L2;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_match_control match;

		flow_rule_match_control(rule, &match);
		addr_type = match.key->addr_type;

		/* the HW doesn't support frag first/later */
		if (match.mask->flags & FLOW_DIS_FIRST_FRAG)
			return -EOPNOTSUPP;

		if (match.mask->flags & FLOW_DIS_IS_FRAGMENT) {
			MLX5_SET(fte_match_set_lyr_2_4, headers_c, frag, 1);
			MLX5_SET(fte_match_set_lyr_2_4, headers_v, frag,
				 match.key->flags & FLOW_DIS_IS_FRAGMENT);

			/* the HW doesn't need L3 inline to match on frag=no */
			if (!(match.key->flags & FLOW_DIS_IS_FRAGMENT))
				*match_level = MLX5_MATCH_L2;
	/* ***  L2 attributes parsing up to here *** */
			else
				*match_level = MLX5_MATCH_L3;
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		ip_proto = match.key->ip_proto;

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ip_protocol,
			 match.mask->ip_proto);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_protocol,
			 match.key->ip_proto);

		if (match.mask->ip_proto)
			*match_level = MLX5_MATCH_L3;
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		struct flow_match_ipv4_addrs match;

		flow_rule_match_ipv4_addrs(rule, &match);
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    src_ipv4_src_ipv6.ipv4_layout.ipv4),
		       &match.mask->src, sizeof(match.mask->src));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    src_ipv4_src_ipv6.ipv4_layout.ipv4),
		       &match.key->src, sizeof(match.key->src));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
		       &match.mask->dst, sizeof(match.mask->dst));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
		       &match.key->dst, sizeof(match.key->dst));

		if (match.mask->src || match.mask->dst)
			*match_level = MLX5_MATCH_L3;
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
		struct flow_match_ipv6_addrs match;

		flow_rule_match_ipv6_addrs(rule, &match);
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       &match.mask->src, sizeof(match.mask->src));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       &match.key->src, sizeof(match.key->src));

		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       &match.mask->dst, sizeof(match.mask->dst));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       &match.key->dst, sizeof(match.key->dst));

		if (ipv6_addr_type(&match.mask->src) != IPV6_ADDR_ANY ||
		    ipv6_addr_type(&match.mask->dst) != IPV6_ADDR_ANY)
			*match_level = MLX5_MATCH_L3;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IP)) {
		struct flow_match_ip match;

		flow_rule_match_ip(rule, &match);
		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ip_ecn,
			 match.mask->tos & 0x3);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_ecn,
			 match.key->tos & 0x3);

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ip_dscp,
			 match.mask->tos >> 2);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_dscp,
			 match.key->tos  >> 2);

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ttl_hoplimit,
			 match.mask->ttl);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ttl_hoplimit,
			 match.key->ttl);

		if (match.mask->ttl &&
		    !MLX5_CAP_ESW_FLOWTABLE_FDB(priv->mdev,
						ft_field_support.outer_ipv4_ttl)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Matching on TTL is not supported");
			return -EOPNOTSUPP;
		}

		if (match.mask->tos || match.mask->ttl)
			*match_level = MLX5_MATCH_L3;
	}

	/* ***  L3 attributes parsing up to here *** */

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		flow_rule_match_ports(rule, &match);
		switch (ip_proto) {
		case IPPROTO_TCP:
			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 tcp_sport, ntohs(match.mask->src));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 tcp_sport, ntohs(match.key->src));

			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 tcp_dport, ntohs(match.mask->dst));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 tcp_dport, ntohs(match.key->dst));
			break;

		case IPPROTO_UDP:
			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 udp_sport, ntohs(match.mask->src));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 udp_sport, ntohs(match.key->src));

			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 udp_dport, ntohs(match.mask->dst));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 udp_dport, ntohs(match.key->dst));
			break;
		default:
			NL_SET_ERR_MSG_MOD(extack,
					   "Only UDP and TCP transports are supported for L4 matching");
			netdev_err(priv->netdev,
				   "Only UDP and TCP transport are supported\n");
			return -EINVAL;
		}

		if (match.mask->src || match.mask->dst)
			*match_level = MLX5_MATCH_L4;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_TCP)) {
		struct flow_match_tcp match;

		flow_rule_match_tcp(rule, &match);
		MLX5_SET(fte_match_set_lyr_2_4, headers_c, tcp_flags,
			 ntohs(match.mask->flags));
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, tcp_flags,
			 ntohs(match.key->flags));

		if (match.mask->flags)
			*match_level = MLX5_MATCH_L4;
	}

	return 0;
}

static int parse_cls_flower(struct mlx5e_priv *priv,
			    struct mlx5e_tc_flow *flow,
			    struct mlx5_flow_spec *spec,
			    struct flow_cls_offload *f,
			    struct net_device *filter_dev)
{
	u8 inner_match_level, outer_match_level, non_tunnel_match_level;
	struct netlink_ext_ack *extack = f->common.extack;
	struct mlx5_core_dev *dev = priv->mdev;
	struct mlx5_eswitch *esw = dev->priv.eswitch;
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep;
	bool is_eswitch_flow;
	int err;

	inner_match_level = MLX5_MATCH_NONE;
	outer_match_level = MLX5_MATCH_NONE;

	err = __parse_cls_flower(priv, flow, spec, f, filter_dev,
				 &inner_match_level, &outer_match_level);
	non_tunnel_match_level = (inner_match_level == MLX5_MATCH_NONE) ?
				 outer_match_level : inner_match_level;

	is_eswitch_flow = mlx5e_is_eswitch_flow(flow);
	if (!err && is_eswitch_flow) {
		rep = rpriv->rep;
		if (rep->vport != MLX5_VPORT_UPLINK &&
		    (esw->offloads.inline_mode != MLX5_INLINE_MODE_NONE &&
		    esw->offloads.inline_mode < non_tunnel_match_level)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Flow is not offloaded due to min inline setting");
			netdev_warn(priv->netdev,
				    "Flow is not offloaded due to min inline setting, required %d actual %d\n",
				    non_tunnel_match_level, esw->offloads.inline_mode);
			return -EOPNOTSUPP;
		}
	}

	if (is_eswitch_flow) {
		flow->esw_attr->inner_match_level = inner_match_level;
		flow->esw_attr->outer_match_level = outer_match_level;
	} else {
		flow->nic_attr->match_level = non_tunnel_match_level;
	}

	return err;
}

struct pedit_headers {
	struct ethhdr  eth;
	struct vlan_hdr vlan;
	struct iphdr   ip4;
	struct ipv6hdr ip6;
	struct tcphdr  tcp;
	struct udphdr  udp;
};

struct pedit_headers_action {
	struct pedit_headers	vals;
	struct pedit_headers	masks;
	u32			pedits;
};

static int pedit_header_offsets[] = {
	[FLOW_ACT_MANGLE_HDR_TYPE_ETH] = offsetof(struct pedit_headers, eth),
	[FLOW_ACT_MANGLE_HDR_TYPE_IP4] = offsetof(struct pedit_headers, ip4),
	[FLOW_ACT_MANGLE_HDR_TYPE_IP6] = offsetof(struct pedit_headers, ip6),
	[FLOW_ACT_MANGLE_HDR_TYPE_TCP] = offsetof(struct pedit_headers, tcp),
	[FLOW_ACT_MANGLE_HDR_TYPE_UDP] = offsetof(struct pedit_headers, udp),
};

#define pedit_header(_ph, _htype) ((void *)(_ph) + pedit_header_offsets[_htype])

static int set_pedit_val(u8 hdr_type, u32 mask, u32 val, u32 offset,
			 struct pedit_headers_action *hdrs)
{
	u32 *curr_pmask, *curr_pval;

	curr_pmask = (u32 *)(pedit_header(&hdrs->masks, hdr_type) + offset);
	curr_pval  = (u32 *)(pedit_header(&hdrs->vals, hdr_type) + offset);

	if (*curr_pmask & mask)  /* disallow acting twice on the same location */
		goto out_err;

	*curr_pmask |= mask;
	*curr_pval  |= (val & mask);

	return 0;

out_err:
	return -EOPNOTSUPP;
}

struct mlx5_fields {
	u8  field;
	u8  field_bsize;
	u32 field_mask;
	u32 offset;
	u32 match_offset;
};

#define OFFLOAD(fw_field, field_bsize, field_mask, field, off, match_field) \
		{MLX5_ACTION_IN_FIELD_OUT_ ## fw_field, field_bsize, field_mask, \
		 offsetof(struct pedit_headers, field) + (off), \
		 MLX5_BYTE_OFF(fte_match_set_lyr_2_4, match_field)}

/* masked values are the same and there are no rewrites that do not have a
 * match.
 */
#define SAME_VAL_MASK(type, valp, maskp, matchvalp, matchmaskp) ({ \
	type matchmaskx = *(type *)(matchmaskp); \
	type matchvalx = *(type *)(matchvalp); \
	type maskx = *(type *)(maskp); \
	type valx = *(type *)(valp); \
	\
	(valx & maskx) == (matchvalx & matchmaskx) && !(maskx & (maskx ^ \
								 matchmaskx)); \
})

static bool cmp_val_mask(void *valp, void *maskp, void *matchvalp,
			 void *matchmaskp, u8 bsize)
{
	bool same = false;

	switch (bsize) {
	case 8:
		same = SAME_VAL_MASK(u8, valp, maskp, matchvalp, matchmaskp);
		break;
	case 16:
		same = SAME_VAL_MASK(u16, valp, maskp, matchvalp, matchmaskp);
		break;
	case 32:
		same = SAME_VAL_MASK(u32, valp, maskp, matchvalp, matchmaskp);
		break;
	}

	return same;
}

static struct mlx5_fields fields[] = {
	OFFLOAD(DMAC_47_16, 32, U32_MAX, eth.h_dest[0], 0, dmac_47_16),
	OFFLOAD(DMAC_15_0,  16, U16_MAX, eth.h_dest[4], 0, dmac_15_0),
	OFFLOAD(SMAC_47_16, 32, U32_MAX, eth.h_source[0], 0, smac_47_16),
	OFFLOAD(SMAC_15_0,  16, U16_MAX, eth.h_source[4], 0, smac_15_0),
	OFFLOAD(ETHERTYPE,  16, U16_MAX, eth.h_proto, 0, ethertype),
	OFFLOAD(FIRST_VID,  16, U16_MAX, vlan.h_vlan_TCI, 0, first_vid),

	OFFLOAD(IP_DSCP, 8,    0xfc, ip4.tos,   0, ip_dscp),
	OFFLOAD(IP_TTL,  8,  U8_MAX, ip4.ttl,   0, ttl_hoplimit),
	OFFLOAD(SIPV4,  32, U32_MAX, ip4.saddr, 0, src_ipv4_src_ipv6.ipv4_layout.ipv4),
	OFFLOAD(DIPV4,  32, U32_MAX, ip4.daddr, 0, dst_ipv4_dst_ipv6.ipv4_layout.ipv4),

	OFFLOAD(SIPV6_127_96, 32, U32_MAX, ip6.saddr.s6_addr32[0], 0,
		src_ipv4_src_ipv6.ipv6_layout.ipv6[0]),
	OFFLOAD(SIPV6_95_64,  32, U32_MAX, ip6.saddr.s6_addr32[1], 0,
		src_ipv4_src_ipv6.ipv6_layout.ipv6[4]),
	OFFLOAD(SIPV6_63_32,  32, U32_MAX, ip6.saddr.s6_addr32[2], 0,
		src_ipv4_src_ipv6.ipv6_layout.ipv6[8]),
	OFFLOAD(SIPV6_31_0,   32, U32_MAX, ip6.saddr.s6_addr32[3], 0,
		src_ipv4_src_ipv6.ipv6_layout.ipv6[12]),
	OFFLOAD(DIPV6_127_96, 32, U32_MAX, ip6.daddr.s6_addr32[0], 0,
		dst_ipv4_dst_ipv6.ipv6_layout.ipv6[0]),
	OFFLOAD(DIPV6_95_64,  32, U32_MAX, ip6.daddr.s6_addr32[1], 0,
		dst_ipv4_dst_ipv6.ipv6_layout.ipv6[4]),
	OFFLOAD(DIPV6_63_32,  32, U32_MAX, ip6.daddr.s6_addr32[2], 0,
		dst_ipv4_dst_ipv6.ipv6_layout.ipv6[8]),
	OFFLOAD(DIPV6_31_0,   32, U32_MAX, ip6.daddr.s6_addr32[3], 0,
		dst_ipv4_dst_ipv6.ipv6_layout.ipv6[12]),
	OFFLOAD(IPV6_HOPLIMIT, 8,  U8_MAX, ip6.hop_limit, 0, ttl_hoplimit),

	OFFLOAD(TCP_SPORT, 16, U16_MAX, tcp.source,  0, tcp_sport),
	OFFLOAD(TCP_DPORT, 16, U16_MAX, tcp.dest,    0, tcp_dport),
	/* in linux iphdr tcp_flags is 8 bits long */
	OFFLOAD(TCP_FLAGS,  8,  U8_MAX, tcp.ack_seq, 5, tcp_flags),

	OFFLOAD(UDP_SPORT, 16, U16_MAX, udp.source, 0, udp_sport),
	OFFLOAD(UDP_DPORT, 16, U16_MAX, udp.dest,   0, udp_dport),
};

static unsigned long mask_to_le(unsigned long mask, int size)
{
	__be32 mask_be32;
	__be16 mask_be16;

	if (size == 32) {
		mask_be32 = (__force __be32)(mask);
		mask = (__force unsigned long)cpu_to_le32(be32_to_cpu(mask_be32));
	} else if (size == 16) {
		mask_be32 = (__force __be32)(mask);
		mask_be16 = *(__be16 *)&mask_be32;
		mask = (__force unsigned long)cpu_to_le16(be16_to_cpu(mask_be16));
	}

	return mask;
}
static int offload_pedit_fields(struct mlx5e_priv *priv,
				int namespace,
				struct pedit_headers_action *hdrs,
				struct mlx5e_tc_flow_parse_attr *parse_attr,
				u32 *action_flags,
				struct netlink_ext_ack *extack)
{
	struct pedit_headers *set_masks, *add_masks, *set_vals, *add_vals;
	int i, action_size, first, last, next_z;
	void *headers_c, *headers_v, *action, *vals_p;
	u32 *s_masks_p, *a_masks_p, s_mask, a_mask;
	struct mlx5e_tc_mod_hdr_acts *mod_acts;
	struct mlx5_fields *f;
	unsigned long mask, field_mask;
	int err;
	u8 cmd;

	mod_acts = &parse_attr->mod_hdr_acts;
	headers_c = get_match_headers_criteria(*action_flags, &parse_attr->spec);
	headers_v = get_match_headers_value(*action_flags, &parse_attr->spec);

	set_masks = &hdrs[0].masks;
	add_masks = &hdrs[1].masks;
	set_vals = &hdrs[0].vals;
	add_vals = &hdrs[1].vals;

	action_size = MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto);

	for (i = 0; i < ARRAY_SIZE(fields); i++) {
		bool skip;

		f = &fields[i];
		/* avoid seeing bits set from previous iterations */
		s_mask = 0;
		a_mask = 0;

		s_masks_p = (void *)set_masks + f->offset;
		a_masks_p = (void *)add_masks + f->offset;

		s_mask = *s_masks_p & f->field_mask;
		a_mask = *a_masks_p & f->field_mask;

		if (!s_mask && !a_mask) /* nothing to offload here */
			continue;

		if (s_mask && a_mask) {
			NL_SET_ERR_MSG_MOD(extack,
					   "can't set and add to the same HW field");
			printk(KERN_WARNING "mlx5: can't set and add to the same HW field (%x)\n", f->field);
			return -EOPNOTSUPP;
		}

		skip = false;
		if (s_mask) {
			void *match_mask = headers_c + f->match_offset;
			void *match_val = headers_v + f->match_offset;

			cmd  = MLX5_ACTION_TYPE_SET;
			mask = s_mask;
			vals_p = (void *)set_vals + f->offset;
			/* don't rewrite if we have a match on the same value */
			if (cmp_val_mask(vals_p, s_masks_p, match_val,
					 match_mask, f->field_bsize))
				skip = true;
			/* clear to denote we consumed this field */
			*s_masks_p &= ~f->field_mask;
		} else {
			cmd  = MLX5_ACTION_TYPE_ADD;
			mask = a_mask;
			vals_p = (void *)add_vals + f->offset;
			/* add 0 is no change */
			if ((*(u32 *)vals_p & f->field_mask) == 0)
				skip = true;
			/* clear to denote we consumed this field */
			*a_masks_p &= ~f->field_mask;
		}
		if (skip)
			continue;

		mask = mask_to_le(mask, f->field_bsize);

		first = find_first_bit(&mask, f->field_bsize);
		next_z = find_next_zero_bit(&mask, f->field_bsize, first);
		last  = find_last_bit(&mask, f->field_bsize);
		if (first < next_z && next_z < last) {
			NL_SET_ERR_MSG_MOD(extack,
					   "rewrite of few sub-fields isn't supported");
			printk(KERN_WARNING "mlx5: rewrite of few sub-fields (mask %lx) isn't offloaded\n",
			       mask);
			return -EOPNOTSUPP;
		}

		err = alloc_mod_hdr_actions(priv->mdev, namespace, mod_acts);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack,
					   "too many pedit actions, can't offload");
			mlx5_core_warn(priv->mdev,
				       "mlx5: parsed %d pedit actions, can't do more\n",
				       mod_acts->num_actions);
			return err;
		}

		action = mod_acts->actions +
			 (mod_acts->num_actions * action_size);
		MLX5_SET(set_action_in, action, action_type, cmd);
		MLX5_SET(set_action_in, action, field, f->field);

		if (cmd == MLX5_ACTION_TYPE_SET) {
			int start;

			field_mask = mask_to_le(f->field_mask, f->field_bsize);

			/* if field is bit sized it can start not from first bit */
			start = find_first_bit(&field_mask, f->field_bsize);

			MLX5_SET(set_action_in, action, offset, first - start);
			/* length is num of bits to be written, zero means length of 32 */
			MLX5_SET(set_action_in, action, length, (last - first + 1));
		}

		if (f->field_bsize == 32)
			MLX5_SET(set_action_in, action, data, ntohl(*(__be32 *)vals_p) >> first);
		else if (f->field_bsize == 16)
			MLX5_SET(set_action_in, action, data, ntohs(*(__be16 *)vals_p) >> first);
		else if (f->field_bsize == 8)
			MLX5_SET(set_action_in, action, data, *(u8 *)vals_p >> first);

		++mod_acts->num_actions;
	}

	return 0;
}

static int mlx5e_flow_namespace_max_modify_action(struct mlx5_core_dev *mdev,
						  int namespace)
{
	if (namespace == MLX5_FLOW_NAMESPACE_FDB) /* FDB offloading */
		return MLX5_CAP_ESW_FLOWTABLE_FDB(mdev, max_modify_header_actions);
	else /* namespace is MLX5_FLOW_NAMESPACE_KERNEL - NIC offloading */
		return MLX5_CAP_FLOWTABLE_NIC_RX(mdev, max_modify_header_actions);
}

int alloc_mod_hdr_actions(struct mlx5_core_dev *mdev,
			  int namespace,
			  struct mlx5e_tc_mod_hdr_acts *mod_hdr_acts)
{
	int action_size, new_num_actions, max_hw_actions;
	size_t new_sz, old_sz;
	void *ret;

	if (mod_hdr_acts->num_actions < mod_hdr_acts->max_actions)
		return 0;

	action_size = MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto);

	max_hw_actions = mlx5e_flow_namespace_max_modify_action(mdev,
								namespace);
	new_num_actions = min(max_hw_actions,
			      mod_hdr_acts->actions ?
			      mod_hdr_acts->max_actions * 2 : 1);
	if (mod_hdr_acts->max_actions == new_num_actions)
		return -ENOSPC;

	new_sz = action_size * new_num_actions;
	old_sz = mod_hdr_acts->max_actions * action_size;
	ret = krealloc(mod_hdr_acts->actions, new_sz, GFP_KERNEL);
	if (!ret)
		return -ENOMEM;

	memset(ret + old_sz, 0, new_sz - old_sz);
	mod_hdr_acts->actions = ret;
	mod_hdr_acts->max_actions = new_num_actions;

	return 0;
}

void dealloc_mod_hdr_actions(struct mlx5e_tc_mod_hdr_acts *mod_hdr_acts)
{
	kfree(mod_hdr_acts->actions);
	mod_hdr_acts->actions = NULL;
	mod_hdr_acts->num_actions = 0;
	mod_hdr_acts->max_actions = 0;
}

static const struct pedit_headers zero_masks = {};

static int
parse_pedit_to_modify_hdr(struct mlx5e_priv *priv,
			  const struct flow_action_entry *act, int namespace,
			  struct mlx5e_tc_flow_parse_attr *parse_attr,
			  struct pedit_headers_action *hdrs,
			  struct netlink_ext_ack *extack)
{
	u8 cmd = (act->id == FLOW_ACTION_MANGLE) ? 0 : 1;
	int err = -EOPNOTSUPP;
	u32 mask, val, offset;
	u8 htype;

	htype = act->mangle.htype;
	err = -EOPNOTSUPP; /* can't be all optimistic */

	if (htype == FLOW_ACT_MANGLE_UNSPEC) {
		NL_SET_ERR_MSG_MOD(extack, "legacy pedit isn't offloaded");
		goto out_err;
	}

	if (!mlx5e_flow_namespace_max_modify_action(priv->mdev, namespace)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "The pedit offload action is not supported");
		goto out_err;
	}

	mask = act->mangle.mask;
	val = act->mangle.val;
	offset = act->mangle.offset;

	err = set_pedit_val(htype, ~mask, val, offset, &hdrs[cmd]);
	if (err)
		goto out_err;

	hdrs[cmd].pedits++;

	return 0;
out_err:
	return err;
}

static int
parse_pedit_to_reformat(struct mlx5e_priv *priv,
			const struct flow_action_entry *act,
			struct mlx5e_tc_flow_parse_attr *parse_attr,
			struct netlink_ext_ack *extack)
{
	u32 mask, val, offset;
	u32 *p;

	if (act->id != FLOW_ACTION_MANGLE)
		return -EOPNOTSUPP;

	if (act->mangle.htype != FLOW_ACT_MANGLE_HDR_TYPE_ETH) {
		NL_SET_ERR_MSG_MOD(extack, "Only Ethernet modification is supported");
		return -EOPNOTSUPP;
	}

	mask = ~act->mangle.mask;
	val = act->mangle.val;
	offset = act->mangle.offset;
	p = (u32 *)&parse_attr->eth;
	*(p + (offset >> 2)) |= (val & mask);

	return 0;
}

static int parse_tc_pedit_action(struct mlx5e_priv *priv,
				 const struct flow_action_entry *act, int namespace,
				 struct mlx5e_tc_flow_parse_attr *parse_attr,
				 struct pedit_headers_action *hdrs,
				 struct mlx5e_tc_flow *flow,
				 struct netlink_ext_ack *extack)
{
	if (flow && flow_flag_test(flow, L3_TO_L2_DECAP))
		return parse_pedit_to_reformat(priv, act, parse_attr, extack);

	return parse_pedit_to_modify_hdr(priv, act, namespace,
					 parse_attr, hdrs, extack);
}

static int alloc_tc_pedit_action(struct mlx5e_priv *priv, int namespace,
				 struct mlx5e_tc_flow_parse_attr *parse_attr,
				 struct pedit_headers_action *hdrs,
				 u32 *action_flags,
				 struct netlink_ext_ack *extack)
{
	struct pedit_headers *cmd_masks;
	int err;
	u8 cmd;

	err = offload_pedit_fields(priv, namespace, hdrs, parse_attr,
				   action_flags, extack);
	if (err < 0)
		goto out_dealloc_parsed_actions;

	for (cmd = 0; cmd < __PEDIT_CMD_MAX; cmd++) {
		cmd_masks = &hdrs[cmd].masks;
		if (memcmp(cmd_masks, &zero_masks, sizeof(zero_masks))) {
			NL_SET_ERR_MSG_MOD(extack,
					   "attempt to offload an unsupported field");
			netdev_warn(priv->netdev, "attempt to offload an unsupported field (cmd %d)\n", cmd);
			print_hex_dump(KERN_WARNING, "mask: ", DUMP_PREFIX_ADDRESS,
				       16, 1, cmd_masks, sizeof(zero_masks), true);
			err = -EOPNOTSUPP;
			goto out_dealloc_parsed_actions;
		}
	}

	return 0;

out_dealloc_parsed_actions:
	dealloc_mod_hdr_actions(&parse_attr->mod_hdr_acts);
	return err;
}

static bool csum_offload_supported(struct mlx5e_priv *priv,
				   u32 action,
				   u32 update_flags,
				   struct netlink_ext_ack *extack)
{
	u32 prot_flags = TCA_CSUM_UPDATE_FLAG_IPV4HDR | TCA_CSUM_UPDATE_FLAG_TCP |
			 TCA_CSUM_UPDATE_FLAG_UDP;

	/*  The HW recalcs checksums only if re-writing headers */
	if (!(action & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "TC csum action is only offloaded with pedit");
		netdev_warn(priv->netdev,
			    "TC csum action is only offloaded with pedit\n");
		return false;
	}

	if (update_flags & ~prot_flags) {
		NL_SET_ERR_MSG_MOD(extack,
				   "can't offload TC csum action for some header/s");
		netdev_warn(priv->netdev,
			    "can't offload TC csum action for some header/s - flags %#x\n",
			    update_flags);
		return false;
	}

	return true;
}

struct ip_ttl_word {
	__u8	ttl;
	__u8	protocol;
	__sum16	check;
};

struct ipv6_hoplimit_word {
	__be16	payload_len;
	__u8	nexthdr;
	__u8	hop_limit;
};

static int is_action_keys_supported(const struct flow_action_entry *act,
				    bool ct_flow, bool *modify_ip_header,
				    bool *modify_tuple,
				    struct netlink_ext_ack *extack)
{
	u32 mask, offset;
	u8 htype;

	htype = act->mangle.htype;
	offset = act->mangle.offset;
	mask = ~act->mangle.mask;
	/* For IPv4 & IPv6 header check 4 byte word,
	 * to determine that modified fields
	 * are NOT ttl & hop_limit only.
	 */
	if (htype == FLOW_ACT_MANGLE_HDR_TYPE_IP4) {
		struct ip_ttl_word *ttl_word =
			(struct ip_ttl_word *)&mask;

		if (offset != offsetof(struct iphdr, ttl) ||
		    ttl_word->protocol ||
		    ttl_word->check) {
			*modify_ip_header = true;
		}

		if (offset >= offsetof(struct iphdr, saddr))
			*modify_tuple = true;

		if (ct_flow && *modify_tuple) {
			NL_SET_ERR_MSG_MOD(extack,
					   "can't offload re-write of ipv4 address with action ct");
			return -EOPNOTSUPP;
		}
	} else if (htype == FLOW_ACT_MANGLE_HDR_TYPE_IP6) {
		struct ipv6_hoplimit_word *hoplimit_word =
			(struct ipv6_hoplimit_word *)&mask;

		if (offset != offsetof(struct ipv6hdr, payload_len) ||
		    hoplimit_word->payload_len ||
		    hoplimit_word->nexthdr) {
			*modify_ip_header = true;
		}

		if (ct_flow && offset >= offsetof(struct ipv6hdr, saddr))
			*modify_tuple = true;

		if (ct_flow && *modify_tuple) {
			NL_SET_ERR_MSG_MOD(extack,
					   "can't offload re-write of ipv6 address with action ct");
			return -EOPNOTSUPP;
		}
	} else if (htype == FLOW_ACT_MANGLE_HDR_TYPE_TCP ||
		   htype == FLOW_ACT_MANGLE_HDR_TYPE_UDP) {
		*modify_tuple = true;
		if (ct_flow) {
			NL_SET_ERR_MSG_MOD(extack,
					   "can't offload re-write of transport header ports with action ct");
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static bool modify_header_match_supported(struct mlx5e_priv *priv,
					  struct mlx5_flow_spec *spec,
					  struct flow_action *flow_action,
					  u32 actions, bool ct_flow,
					  bool ct_clear,
					  struct netlink_ext_ack *extack)
{
	const struct flow_action_entry *act;
	bool modify_ip_header, modify_tuple;
	void *headers_c;
	void *headers_v;
	u16 ethertype;
	u8 ip_proto;
	int i, err;

	headers_c = get_match_headers_criteria(actions, spec);
	headers_v = get_match_headers_value(actions, spec);
	ethertype = MLX5_GET(fte_match_set_lyr_2_4, headers_v, ethertype);

	/* for non-IP we only re-write MACs, so we're okay */
	if (MLX5_GET(fte_match_set_lyr_2_4, headers_c, ip_version) == 0 &&
	    ethertype != ETH_P_IP && ethertype != ETH_P_IPV6)
		goto out_ok;

	modify_ip_header = false;
	modify_tuple = false;
	flow_action_for_each(i, act, flow_action) {
		if (act->id != FLOW_ACTION_MANGLE &&
		    act->id != FLOW_ACTION_ADD)
			continue;

		err = is_action_keys_supported(act, ct_flow,
					       &modify_ip_header,
					       &modify_tuple, extack);
		if (err)
			return err;
	}

	/* Add ct_state=-trk match so it will be offloaded for non ct flows
	 * (or after clear action), as otherwise, since the tuple is changed,
	 *  we can't restore ct state
	 */
	if (!ct_clear && modify_tuple &&
	    mlx5_tc_ct_add_no_trk_match(priv, spec)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "can't offload tuple modify header with ct matches");
		netdev_info(priv->netdev,
			    "can't offload tuple modify header with ct matches");
		return false;
	}

	ip_proto = MLX5_GET(fte_match_set_lyr_2_4, headers_v, ip_protocol);
	if (modify_ip_header && ip_proto != IPPROTO_TCP &&
	    ip_proto != IPPROTO_UDP && ip_proto != IPPROTO_ICMP) {
		NL_SET_ERR_MSG_MOD(extack,
				   "can't offload re-write of non TCP/UDP");
		netdev_info(priv->netdev, "can't offload re-write of ip proto %d\n",
			    ip_proto);
		return false;
	}

out_ok:
	return true;
}

static bool actions_match_supported(struct mlx5e_priv *priv,
				    struct flow_action *flow_action,
				    struct mlx5e_tc_flow_parse_attr *parse_attr,
				    struct mlx5e_tc_flow *flow,
				    struct netlink_ext_ack *extack)
{
	bool ct_flow = false, ct_clear = false;
	u32 actions;

	if (mlx5e_is_eswitch_flow(flow)) {
		actions = flow->esw_attr->action;
		ct_clear = flow->esw_attr->ct_attr.ct_action &
			   TCA_CT_ACT_CLEAR;
		ct_flow = flow_flag_test(flow, CT) && !ct_clear;
		if (flow->esw_attr->split_count && ct_flow) {
			/* All registers used by ct are cleared when using
			 * split rules.
			 */
			NL_SET_ERR_MSG_MOD(extack,
					   "Can't offload mirroring with action ct");
			return false;
		}
	} else {
		actions = flow->nic_attr->action;
	}

	if (actions & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR)
		return modify_header_match_supported(priv, &parse_attr->spec,
						     flow_action, actions,
						     ct_flow, ct_clear,
						     extack);

	return true;
}

static bool same_port_devs(struct mlx5e_priv *priv, struct mlx5e_priv *peer_priv)
{
	return priv->mdev == peer_priv->mdev;
}

static bool same_hw_devs(struct mlx5e_priv *priv, struct mlx5e_priv *peer_priv)
{
	struct mlx5_core_dev *fmdev, *pmdev;
	u64 fsystem_guid, psystem_guid;

	fmdev = priv->mdev;
	pmdev = peer_priv->mdev;

	fsystem_guid = mlx5_query_nic_system_image_guid(fmdev);
	psystem_guid = mlx5_query_nic_system_image_guid(pmdev);

	return (fsystem_guid == psystem_guid);
}

static int add_vlan_rewrite_action(struct mlx5e_priv *priv, int namespace,
				   const struct flow_action_entry *act,
				   struct mlx5e_tc_flow_parse_attr *parse_attr,
				   struct pedit_headers_action *hdrs,
				   u32 *action, struct netlink_ext_ack *extack)
{
	u16 mask16 = VLAN_VID_MASK;
	u16 val16 = act->vlan.vid & VLAN_VID_MASK;
	const struct flow_action_entry pedit_act = {
		.id = FLOW_ACTION_MANGLE,
		.mangle.htype = FLOW_ACT_MANGLE_HDR_TYPE_ETH,
		.mangle.offset = offsetof(struct vlan_ethhdr, h_vlan_TCI),
		.mangle.mask = ~(u32)be16_to_cpu(*(__be16 *)&mask16),
		.mangle.val = (u32)be16_to_cpu(*(__be16 *)&val16),
	};
	u8 match_prio_mask, match_prio_val;
	void *headers_c, *headers_v;
	int err;

	headers_c = get_match_headers_criteria(*action, &parse_attr->spec);
	headers_v = get_match_headers_value(*action, &parse_attr->spec);

	if (!(MLX5_GET(fte_match_set_lyr_2_4, headers_c, cvlan_tag) &&
	      MLX5_GET(fte_match_set_lyr_2_4, headers_v, cvlan_tag))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "VLAN rewrite action must have VLAN protocol match");
		return -EOPNOTSUPP;
	}

	match_prio_mask = MLX5_GET(fte_match_set_lyr_2_4, headers_c, first_prio);
	match_prio_val = MLX5_GET(fte_match_set_lyr_2_4, headers_v, first_prio);
	if (act->vlan.prio != (match_prio_val & match_prio_mask)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Changing VLAN prio is not supported");
		return -EOPNOTSUPP;
	}

	err = parse_tc_pedit_action(priv, &pedit_act, namespace, parse_attr, hdrs, NULL, extack);
	*action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;

	return err;
}

static int
add_vlan_prio_tag_rewrite_action(struct mlx5e_priv *priv,
				 struct mlx5e_tc_flow_parse_attr *parse_attr,
				 struct pedit_headers_action *hdrs,
				 u32 *action, struct netlink_ext_ack *extack)
{
	const struct flow_action_entry prio_tag_act = {
		.vlan.vid = 0,
		.vlan.prio =
			MLX5_GET(fte_match_set_lyr_2_4,
				 get_match_headers_value(*action,
							 &parse_attr->spec),
				 first_prio) &
			MLX5_GET(fte_match_set_lyr_2_4,
				 get_match_headers_criteria(*action,
							    &parse_attr->spec),
				 first_prio),
	};

	return add_vlan_rewrite_action(priv, MLX5_FLOW_NAMESPACE_FDB,
				       &prio_tag_act, parse_attr, hdrs, action,
				       extack);
}

static int parse_tc_nic_actions(struct mlx5e_priv *priv,
				struct flow_action *flow_action,
				struct mlx5e_tc_flow_parse_attr *parse_attr,
				struct mlx5e_tc_flow *flow,
				struct netlink_ext_ack *extack)
{
	struct mlx5_nic_flow_attr *attr = flow->nic_attr;
	struct pedit_headers_action hdrs[2] = {};
	const struct flow_action_entry *act;
	u32 action = 0;
	int err, i;

	if (!flow_action_has_entries(flow_action))
		return -EINVAL;

	if (!flow_action_hw_stats_check(flow_action, extack,
					FLOW_ACTION_HW_STATS_DELAYED_BIT))
		return -EOPNOTSUPP;

	attr->flow_tag = MLX5_FS_DEFAULT_FLOW_TAG;

	flow_action_for_each(i, act, flow_action) {
		switch (act->id) {
		case FLOW_ACTION_ACCEPT:
			action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
				  MLX5_FLOW_CONTEXT_ACTION_COUNT;
			break;
		case FLOW_ACTION_DROP:
			action |= MLX5_FLOW_CONTEXT_ACTION_DROP;
			if (MLX5_CAP_FLOWTABLE(priv->mdev,
					       flow_table_properties_nic_receive.flow_counter))
				action |= MLX5_FLOW_CONTEXT_ACTION_COUNT;
			break;
		case FLOW_ACTION_MANGLE:
		case FLOW_ACTION_ADD:
			err = parse_tc_pedit_action(priv, act, MLX5_FLOW_NAMESPACE_KERNEL,
						    parse_attr, hdrs, NULL, extack);
			if (err)
				return err;

			action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR |
				  MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
			break;
		case FLOW_ACTION_VLAN_MANGLE:
			err = add_vlan_rewrite_action(priv,
						      MLX5_FLOW_NAMESPACE_KERNEL,
						      act, parse_attr, hdrs,
						      &action, extack);
			if (err)
				return err;

			break;
		case FLOW_ACTION_CSUM:
			if (csum_offload_supported(priv, action,
						   act->csum_flags,
						   extack))
				break;

			return -EOPNOTSUPP;
		case FLOW_ACTION_REDIRECT: {
			struct net_device *peer_dev = act->dev;

			if (priv->netdev->netdev_ops == peer_dev->netdev_ops &&
			    same_hw_devs(priv, netdev_priv(peer_dev))) {
				parse_attr->mirred_ifindex[0] = peer_dev->ifindex;
				flow_flag_set(flow, HAIRPIN);
				action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
					  MLX5_FLOW_CONTEXT_ACTION_COUNT;
			} else {
				NL_SET_ERR_MSG_MOD(extack,
						   "device is not on same HW, can't offload");
				netdev_warn(priv->netdev, "device %s not on same HW, can't offload\n",
					    peer_dev->name);
				return -EINVAL;
			}
			}
			break;
		case FLOW_ACTION_MARK: {
			u32 mark = act->mark;

			if (mark & ~MLX5E_TC_FLOW_ID_MASK) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Bad flow mark - only 16 bit is supported");
				return -EINVAL;
			}

			attr->flow_tag = mark;
			action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
			}
			break;
		default:
			NL_SET_ERR_MSG_MOD(extack, "The offload action is not supported");
			return -EOPNOTSUPP;
		}
	}

	if (hdrs[TCA_PEDIT_KEY_EX_CMD_SET].pedits ||
	    hdrs[TCA_PEDIT_KEY_EX_CMD_ADD].pedits) {
		err = alloc_tc_pedit_action(priv, MLX5_FLOW_NAMESPACE_KERNEL,
					    parse_attr, hdrs, &action, extack);
		if (err)
			return err;
		/* in case all pedit actions are skipped, remove the MOD_HDR
		 * flag.
		 */
		if (parse_attr->mod_hdr_acts.num_actions == 0) {
			action &= ~MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
			dealloc_mod_hdr_actions(&parse_attr->mod_hdr_acts);
		}
	}

	attr->action = action;
	if (!actions_match_supported(priv, flow_action, parse_attr, flow, extack))
		return -EOPNOTSUPP;

	return 0;
}

struct encap_key {
	const struct ip_tunnel_key *ip_tun_key;
	struct mlx5e_tc_tunnel *tc_tunnel;
};

static inline int cmp_encap_info(struct encap_key *a,
				 struct encap_key *b)
{
	return memcmp(a->ip_tun_key, b->ip_tun_key, sizeof(*a->ip_tun_key)) ||
	       a->tc_tunnel->tunnel_type != b->tc_tunnel->tunnel_type;
}

static inline int cmp_decap_info(struct mlx5e_decap_key *a,
				 struct mlx5e_decap_key *b)
{
	return memcmp(&a->key, &b->key, sizeof(b->key));
}

static inline int hash_encap_info(struct encap_key *key)
{
	return jhash(key->ip_tun_key, sizeof(*key->ip_tun_key),
		     key->tc_tunnel->tunnel_type);
}

static inline int hash_decap_info(struct mlx5e_decap_key *key)
{
	return jhash(&key->key, sizeof(key->key), 0);
}

static bool is_merged_eswitch_vfs(struct mlx5e_priv *priv,
				  struct net_device *peer_netdev)
{
	struct mlx5e_priv *peer_priv;

	peer_priv = netdev_priv(peer_netdev);

	return (MLX5_CAP_ESW(priv->mdev, merged_eswitch) &&
		mlx5e_eswitch_vf_rep(priv->netdev) &&
		mlx5e_eswitch_vf_rep(peer_netdev) &&
		same_hw_devs(priv, peer_priv));
}

bool mlx5e_encap_take(struct mlx5e_encap_entry *e)
{
	return refcount_inc_not_zero(&e->refcnt);
}

static bool mlx5e_decap_take(struct mlx5e_decap_entry *e)
{
	return refcount_inc_not_zero(&e->refcnt);
}

static struct mlx5e_encap_entry *
mlx5e_encap_get(struct mlx5e_priv *priv, struct encap_key *key,
		uintptr_t hash_key)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_encap_entry *e;
	struct encap_key e_key;

	hash_for_each_possible_rcu(esw->offloads.encap_tbl, e,
				   encap_hlist, hash_key) {
		e_key.ip_tun_key = &e->tun_info->key;
		e_key.tc_tunnel = e->tunnel;
		if (!cmp_encap_info(&e_key, key) &&
		    mlx5e_encap_take(e))
			return e;
	}

	return NULL;
}

static struct mlx5e_decap_entry *
mlx5e_decap_get(struct mlx5e_priv *priv, struct mlx5e_decap_key *key,
		uintptr_t hash_key)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_decap_key r_key;
	struct mlx5e_decap_entry *e;

	hash_for_each_possible_rcu(esw->offloads.decap_tbl, e,
				   hlist, hash_key) {
		r_key = e->key;
		if (!cmp_decap_info(&r_key, key) &&
		    mlx5e_decap_take(e))
			return e;
	}
	return NULL;
}

static struct ip_tunnel_info *dup_tun_info(const struct ip_tunnel_info *tun_info)
{
	size_t tun_size = sizeof(*tun_info) + tun_info->options_len;

	return kmemdup(tun_info, tun_size, GFP_KERNEL);
}

static bool is_duplicated_encap_entry(struct mlx5e_priv *priv,
				      struct mlx5e_tc_flow *flow,
				      int out_index,
				      struct mlx5e_encap_entry *e,
				      struct netlink_ext_ack *extack)
{
	int i;

	for (i = 0; i < out_index; i++) {
		if (flow->encaps[i].e != e)
			continue;
		NL_SET_ERR_MSG_MOD(extack, "can't duplicate encap action");
		netdev_err(priv->netdev, "can't duplicate encap action\n");
		return true;
	}

	return false;
}

static int mlx5e_attach_encap(struct mlx5e_priv *priv,
			      struct mlx5e_tc_flow *flow,
			      struct net_device *mirred_dev,
			      int out_index,
			      struct netlink_ext_ack *extack,
			      struct net_device **encap_dev,
			      bool *encap_valid)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_esw_flow_attr *attr = flow->esw_attr;
	struct mlx5e_tc_flow_parse_attr *parse_attr;
	const struct ip_tunnel_info *tun_info;
	struct encap_key key;
	struct mlx5e_encap_entry *e;
	unsigned short family;
	uintptr_t hash_key;
	int err = 0;

	parse_attr = attr->parse_attr;
	tun_info = parse_attr->tun_info[out_index];
	family = ip_tunnel_info_af(tun_info);
	key.ip_tun_key = &tun_info->key;
	key.tc_tunnel = mlx5e_get_tc_tun(mirred_dev);
	if (!key.tc_tunnel) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported tunnel");
		return -EOPNOTSUPP;
	}

	hash_key = hash_encap_info(&key);

	mutex_lock(&esw->offloads.encap_tbl_lock);
	e = mlx5e_encap_get(priv, &key, hash_key);

	/* must verify if encap is valid or not */
	if (e) {
		/* Check that entry was not already attached to this flow */
		if (is_duplicated_encap_entry(priv, flow, out_index, e, extack)) {
			err = -EOPNOTSUPP;
			goto out_err;
		}

		mutex_unlock(&esw->offloads.encap_tbl_lock);
		wait_for_completion(&e->res_ready);

		/* Protect against concurrent neigh update. */
		mutex_lock(&esw->offloads.encap_tbl_lock);
		if (e->compl_result < 0) {
			err = -EREMOTEIO;
			goto out_err;
		}
		goto attach_flow;
	}

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e) {
		err = -ENOMEM;
		goto out_err;
	}

	refcount_set(&e->refcnt, 1);
	init_completion(&e->res_ready);

	tun_info = dup_tun_info(tun_info);
	if (!tun_info) {
		err = -ENOMEM;
		goto out_err_init;
	}
	e->tun_info = tun_info;
	err = mlx5e_tc_tun_init_encap_attr(mirred_dev, priv, e, extack);
	if (err)
		goto out_err_init;

	INIT_LIST_HEAD(&e->flows);
	hash_add_rcu(esw->offloads.encap_tbl, &e->encap_hlist, hash_key);
	mutex_unlock(&esw->offloads.encap_tbl_lock);

	if (family == AF_INET)
		err = mlx5e_tc_tun_create_header_ipv4(priv, mirred_dev, e);
	else if (family == AF_INET6)
		err = mlx5e_tc_tun_create_header_ipv6(priv, mirred_dev, e);

	/* Protect against concurrent neigh update. */
	mutex_lock(&esw->offloads.encap_tbl_lock);
	complete_all(&e->res_ready);
	if (err) {
		e->compl_result = err;
		goto out_err;
	}
	e->compl_result = 1;

attach_flow:
	flow->encaps[out_index].e = e;
	list_add(&flow->encaps[out_index].list, &e->flows);
	flow->encaps[out_index].index = out_index;
	*encap_dev = e->out_dev;
	if (e->flags & MLX5_ENCAP_ENTRY_VALID) {
		attr->dests[out_index].pkt_reformat = e->pkt_reformat;
		attr->dests[out_index].flags |= MLX5_ESW_DEST_ENCAP_VALID;
		*encap_valid = true;
	} else {
		*encap_valid = false;
	}
	mutex_unlock(&esw->offloads.encap_tbl_lock);

	return err;

out_err:
	mutex_unlock(&esw->offloads.encap_tbl_lock);
	if (e)
		mlx5e_encap_put(priv, e);
	return err;

out_err_init:
	mutex_unlock(&esw->offloads.encap_tbl_lock);
	kfree(tun_info);
	kfree(e);
	return err;
}

static int mlx5e_attach_decap(struct mlx5e_priv *priv,
			      struct mlx5e_tc_flow *flow,
			      struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_esw_flow_attr *attr = flow->esw_attr;
	struct mlx5e_tc_flow_parse_attr *parse_attr;
	struct mlx5e_decap_entry *d;
	struct mlx5e_decap_key key;
	uintptr_t hash_key;
	int err = 0;

	parse_attr = attr->parse_attr;
	if (sizeof(parse_attr->eth) > MLX5_CAP_ESW(priv->mdev, max_encap_header_size)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "encap header larger than max supported");
		return -EOPNOTSUPP;
	}

	key.key = parse_attr->eth;
	hash_key = hash_decap_info(&key);
	mutex_lock(&esw->offloads.decap_tbl_lock);
	d = mlx5e_decap_get(priv, &key, hash_key);
	if (d) {
		mutex_unlock(&esw->offloads.decap_tbl_lock);
		wait_for_completion(&d->res_ready);
		mutex_lock(&esw->offloads.decap_tbl_lock);
		if (d->compl_result) {
			err = -EREMOTEIO;
			goto out_free;
		}
		goto found;
	}

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d) {
		err = -ENOMEM;
		goto out_err;
	}

	d->key = key;
	refcount_set(&d->refcnt, 1);
	init_completion(&d->res_ready);
	INIT_LIST_HEAD(&d->flows);
	hash_add_rcu(esw->offloads.decap_tbl, &d->hlist, hash_key);
	mutex_unlock(&esw->offloads.decap_tbl_lock);

	d->pkt_reformat = mlx5_packet_reformat_alloc(priv->mdev,
						     MLX5_REFORMAT_TYPE_L3_TUNNEL_TO_L2,
						     sizeof(parse_attr->eth),
						     &parse_attr->eth,
						     MLX5_FLOW_NAMESPACE_FDB);
	if (IS_ERR(d->pkt_reformat)) {
		err = PTR_ERR(d->pkt_reformat);
		d->compl_result = err;
	}
	mutex_lock(&esw->offloads.decap_tbl_lock);
	complete_all(&d->res_ready);
	if (err)
		goto out_free;

found:
	flow->decap_reformat = d;
	attr->decap_pkt_reformat = d->pkt_reformat;
	list_add(&flow->l3_to_l2_reformat, &d->flows);
	mutex_unlock(&esw->offloads.decap_tbl_lock);
	return 0;

out_free:
	mutex_unlock(&esw->offloads.decap_tbl_lock);
	mlx5e_decap_put(priv, d);
	return err;

out_err:
	mutex_unlock(&esw->offloads.decap_tbl_lock);
	return err;
}

static int parse_tc_vlan_action(struct mlx5e_priv *priv,
				const struct flow_action_entry *act,
				struct mlx5_esw_flow_attr *attr,
				u32 *action)
{
	u8 vlan_idx = attr->total_vlan;

	if (vlan_idx >= MLX5_FS_VLAN_DEPTH)
		return -EOPNOTSUPP;

	switch (act->id) {
	case FLOW_ACTION_VLAN_POP:
		if (vlan_idx) {
			if (!mlx5_eswitch_vlan_actions_supported(priv->mdev,
								 MLX5_FS_VLAN_DEPTH))
				return -EOPNOTSUPP;

			*action |= MLX5_FLOW_CONTEXT_ACTION_VLAN_POP_2;
		} else {
			*action |= MLX5_FLOW_CONTEXT_ACTION_VLAN_POP;
		}
		break;
	case FLOW_ACTION_VLAN_PUSH:
		attr->vlan_vid[vlan_idx] = act->vlan.vid;
		attr->vlan_prio[vlan_idx] = act->vlan.prio;
		attr->vlan_proto[vlan_idx] = act->vlan.proto;
		if (!attr->vlan_proto[vlan_idx])
			attr->vlan_proto[vlan_idx] = htons(ETH_P_8021Q);

		if (vlan_idx) {
			if (!mlx5_eswitch_vlan_actions_supported(priv->mdev,
								 MLX5_FS_VLAN_DEPTH))
				return -EOPNOTSUPP;

			*action |= MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH_2;
		} else {
			if (!mlx5_eswitch_vlan_actions_supported(priv->mdev, 1) &&
			    (act->vlan.proto != htons(ETH_P_8021Q) ||
			     act->vlan.prio))
				return -EOPNOTSUPP;

			*action |= MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH;
		}
		break;
	default:
		return -EINVAL;
	}

	attr->total_vlan = vlan_idx + 1;

	return 0;
}

static struct net_device *get_fdb_out_dev(struct net_device *uplink_dev,
					  struct net_device *out_dev)
{
	struct net_device *fdb_out_dev = out_dev;
	struct net_device *uplink_upper;

	rcu_read_lock();
	uplink_upper = netdev_master_upper_dev_get_rcu(uplink_dev);
	if (uplink_upper && netif_is_lag_master(uplink_upper) &&
	    uplink_upper == out_dev) {
		fdb_out_dev = uplink_dev;
	} else if (netif_is_lag_master(out_dev)) {
		fdb_out_dev = bond_option_active_slave_get_rcu(netdev_priv(out_dev));
		if (fdb_out_dev &&
		    (!mlx5e_eswitch_rep(fdb_out_dev) ||
		     !netdev_port_same_parent_id(fdb_out_dev, uplink_dev)))
			fdb_out_dev = NULL;
	}
	rcu_read_unlock();
	return fdb_out_dev;
}

static int add_vlan_push_action(struct mlx5e_priv *priv,
				struct mlx5_esw_flow_attr *attr,
				struct net_device **out_dev,
				u32 *action)
{
	struct net_device *vlan_dev = *out_dev;
	struct flow_action_entry vlan_act = {
		.id = FLOW_ACTION_VLAN_PUSH,
		.vlan.vid = vlan_dev_vlan_id(vlan_dev),
		.vlan.proto = vlan_dev_vlan_proto(vlan_dev),
		.vlan.prio = 0,
	};
	int err;

	err = parse_tc_vlan_action(priv, &vlan_act, attr, action);
	if (err)
		return err;

	*out_dev = dev_get_by_index_rcu(dev_net(vlan_dev),
					dev_get_iflink(vlan_dev));
	if (is_vlan_dev(*out_dev))
		err = add_vlan_push_action(priv, attr, out_dev, action);

	return err;
}

static int add_vlan_pop_action(struct mlx5e_priv *priv,
			       struct mlx5_esw_flow_attr *attr,
			       u32 *action)
{
	struct flow_action_entry vlan_act = {
		.id = FLOW_ACTION_VLAN_POP,
	};
	int nest_level, err = 0;

	nest_level = attr->parse_attr->filter_dev->lower_level -
						priv->netdev->lower_level;
	while (nest_level--) {
		err = parse_tc_vlan_action(priv, &vlan_act, attr, action);
		if (err)
			return err;
	}

	return err;
}

static bool same_hw_reps(struct mlx5e_priv *priv,
			 struct net_device *peer_netdev)
{
	struct mlx5e_priv *peer_priv;

	peer_priv = netdev_priv(peer_netdev);

	return mlx5e_eswitch_rep(priv->netdev) &&
	       mlx5e_eswitch_rep(peer_netdev) &&
	       same_hw_devs(priv, peer_priv);
}

static bool is_lag_dev(struct mlx5e_priv *priv,
		       struct net_device *peer_netdev)
{
	return ((mlx5_lag_is_sriov(priv->mdev) ||
		 mlx5_lag_is_multipath(priv->mdev)) &&
		 same_hw_reps(priv, peer_netdev));
}

bool mlx5e_is_valid_eswitch_fwd_dev(struct mlx5e_priv *priv,
				    struct net_device *out_dev)
{
	if (is_merged_eswitch_vfs(priv, out_dev))
		return true;

	if (is_lag_dev(priv, out_dev))
		return true;

	return mlx5e_eswitch_rep(out_dev) &&
	       same_port_devs(priv, netdev_priv(out_dev));
}

static bool is_duplicated_output_device(struct net_device *dev,
					struct net_device *out_dev,
					int *ifindexes, int if_count,
					struct netlink_ext_ack *extack)
{
	int i;

	for (i = 0; i < if_count; i++) {
		if (ifindexes[i] == out_dev->ifindex) {
			NL_SET_ERR_MSG_MOD(extack,
					   "can't duplicate output to same device");
			netdev_err(dev, "can't duplicate output to same device: %s\n",
				   out_dev->name);
			return true;
		}
	}

	return false;
}

static int mlx5_validate_goto_chain(struct mlx5_eswitch *esw,
				    struct mlx5e_tc_flow *flow,
				    const struct flow_action_entry *act,
				    u32 actions,
				    struct netlink_ext_ack *extack)
{
	u32 max_chain = mlx5_esw_chains_get_chain_range(esw);
	struct mlx5_esw_flow_attr *attr = flow->esw_attr;
	bool ft_flow = mlx5e_is_ft_flow(flow);
	u32 dest_chain = act->chain_index;

	if (ft_flow) {
		NL_SET_ERR_MSG_MOD(extack, "Goto action is not supported");
		return -EOPNOTSUPP;
	}

	if (!mlx5_esw_chains_backwards_supported(esw) &&
	    dest_chain <= attr->chain) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Goto lower numbered chain isn't supported");
		return -EOPNOTSUPP;
	}
	if (dest_chain > max_chain) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Requested destination chain is out of supported range");
		return -EOPNOTSUPP;
	}

	if (actions & (MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT |
		       MLX5_FLOW_CONTEXT_ACTION_DECAP) &&
	    !MLX5_CAP_ESW_FLOWTABLE_FDB(esw->dev, reformat_and_fwd_to_table)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Goto chain is not allowed if action has reformat or decap");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int verify_uplink_forwarding(struct mlx5e_priv *priv,
				    struct mlx5e_tc_flow *flow,
				    struct net_device *out_dev,
				    struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_esw_flow_attr *attr = flow->esw_attr;
	struct mlx5e_rep_priv *rep_priv;

	/* Forwarding non encapsulated traffic between
	 * uplink ports is allowed only if
	 * termination_table_raw_traffic cap is set.
	 *
	 * Input vport was stored esw_attr->in_rep.
	 * In LAG case, *priv* is the private data of
	 * uplink which may be not the input vport.
	 */
	rep_priv = mlx5e_rep_to_rep_priv(attr->in_rep);

	if (!(mlx5e_eswitch_uplink_rep(rep_priv->netdev) &&
	      mlx5e_eswitch_uplink_rep(out_dev)))
		return 0;

	if (!MLX5_CAP_ESW_FLOWTABLE_FDB(esw->dev,
					termination_table_raw_traffic)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "devices are both uplink, can't offload forwarding");
			pr_err("devices %s %s are both uplink, can't offload forwarding\n",
			       priv->netdev->name, out_dev->name);
			return -EOPNOTSUPP;
	} else if (out_dev != rep_priv->netdev) {
		NL_SET_ERR_MSG_MOD(extack,
				   "devices are not the same uplink, can't offload forwarding");
		pr_err("devices %s %s are both uplink but not the same, can't offload forwarding\n",
		       priv->netdev->name, out_dev->name);
		return -EOPNOTSUPP;
	}
	return 0;
}

static int parse_tc_fdb_actions(struct mlx5e_priv *priv,
				struct flow_action *flow_action,
				struct mlx5e_tc_flow *flow,
				struct netlink_ext_ack *extack,
				struct net_device *filter_dev)
{
	struct pedit_headers_action hdrs[2] = {};
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_esw_flow_attr *attr = flow->esw_attr;
	struct mlx5e_tc_flow_parse_attr *parse_attr = attr->parse_attr;
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	const struct ip_tunnel_info *info = NULL;
	int ifindexes[MLX5_MAX_FLOW_FWD_VPORTS];
	bool ft_flow = mlx5e_is_ft_flow(flow);
	const struct flow_action_entry *act;
	bool encap = false, decap = false;
	u32 action = attr->action;
	int err, i, if_count = 0;
	bool mpls_push = false;

	if (!flow_action_has_entries(flow_action))
		return -EINVAL;

	if (!flow_action_hw_stats_check(flow_action, extack,
					FLOW_ACTION_HW_STATS_DELAYED_BIT))
		return -EOPNOTSUPP;

	flow_action_for_each(i, act, flow_action) {
		switch (act->id) {
		case FLOW_ACTION_DROP:
			action |= MLX5_FLOW_CONTEXT_ACTION_DROP |
				  MLX5_FLOW_CONTEXT_ACTION_COUNT;
			break;
		case FLOW_ACTION_MPLS_PUSH:
			if (!MLX5_CAP_ESW_FLOWTABLE_FDB(priv->mdev,
							reformat_l2_to_l3_tunnel) ||
			    act->mpls_push.proto != htons(ETH_P_MPLS_UC)) {
				NL_SET_ERR_MSG_MOD(extack,
						   "mpls push is supported only for mpls_uc protocol");
				return -EOPNOTSUPP;
			}
			mpls_push = true;
			break;
		case FLOW_ACTION_MPLS_POP:
			/* we only support mpls pop if it is the first action
			 * and the filter net device is bareudp. Subsequent
			 * actions can be pedit and the last can be mirred
			 * egress redirect.
			 */
			if (i) {
				NL_SET_ERR_MSG_MOD(extack,
						   "mpls pop supported only as first action");
				return -EOPNOTSUPP;
			}
			if (!netif_is_bareudp(filter_dev)) {
				NL_SET_ERR_MSG_MOD(extack,
						   "mpls pop supported only on bareudp devices");
				return -EOPNOTSUPP;
			}

			parse_attr->eth.h_proto = act->mpls_pop.proto;
			action |= MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT;
			flow_flag_set(flow, L3_TO_L2_DECAP);
			break;
		case FLOW_ACTION_MANGLE:
		case FLOW_ACTION_ADD:
			err = parse_tc_pedit_action(priv, act, MLX5_FLOW_NAMESPACE_FDB,
						    parse_attr, hdrs, flow, extack);
			if (err)
				return err;

			if (!flow_flag_test(flow, L3_TO_L2_DECAP)) {
				action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
				attr->split_count = attr->out_count;
			}
			break;
		case FLOW_ACTION_CSUM:
			if (csum_offload_supported(priv, action,
						   act->csum_flags, extack))
				break;

			return -EOPNOTSUPP;
		case FLOW_ACTION_REDIRECT:
		case FLOW_ACTION_MIRRED: {
			struct mlx5e_priv *out_priv;
			struct net_device *out_dev;

			out_dev = act->dev;
			if (!out_dev) {
				/* out_dev is NULL when filters with
				 * non-existing mirred device are replayed to
				 * the driver.
				 */
				return -EINVAL;
			}

			if (mpls_push && !netif_is_bareudp(out_dev)) {
				NL_SET_ERR_MSG_MOD(extack,
						   "mpls is supported only through a bareudp device");
				return -EOPNOTSUPP;
			}

			if (ft_flow && out_dev == priv->netdev) {
				/* Ignore forward to self rules generated
				 * by adding both mlx5 devs to the flow table
				 * block on a normal nft offload setup.
				 */
				return -EOPNOTSUPP;
			}

			if (attr->out_count >= MLX5_MAX_FLOW_FWD_VPORTS) {
				NL_SET_ERR_MSG_MOD(extack,
						   "can't support more output ports, can't offload forwarding");
				netdev_warn(priv->netdev,
					    "can't support more than %d output ports, can't offload forwarding\n",
					    attr->out_count);
				return -EOPNOTSUPP;
			}

			action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
				  MLX5_FLOW_CONTEXT_ACTION_COUNT;
			if (encap) {
				parse_attr->mirred_ifindex[attr->out_count] =
					out_dev->ifindex;
				parse_attr->tun_info[attr->out_count] = dup_tun_info(info);
				if (!parse_attr->tun_info[attr->out_count])
					return -ENOMEM;
				encap = false;
				attr->dests[attr->out_count].flags |=
					MLX5_ESW_DEST_ENCAP;
				attr->out_count++;
				/* attr->dests[].rep is resolved when we
				 * handle encap
				 */
			} else if (netdev_port_same_parent_id(priv->netdev, out_dev)) {
				struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
				struct net_device *uplink_dev = mlx5_eswitch_uplink_get_proto_dev(esw, REP_ETH);

				if (is_duplicated_output_device(priv->netdev,
								out_dev,
								ifindexes,
								if_count,
								extack))
					return -EOPNOTSUPP;

				ifindexes[if_count] = out_dev->ifindex;
				if_count++;

				out_dev = get_fdb_out_dev(uplink_dev, out_dev);
				if (!out_dev)
					return -ENODEV;

				if (is_vlan_dev(out_dev)) {
					err = add_vlan_push_action(priv, attr,
								   &out_dev,
								   &action);
					if (err)
						return err;
				}

				if (is_vlan_dev(parse_attr->filter_dev)) {
					err = add_vlan_pop_action(priv, attr,
								  &action);
					if (err)
						return err;
				}

				err = verify_uplink_forwarding(priv, flow, out_dev, extack);
				if (err)
					return err;

				if (!mlx5e_is_valid_eswitch_fwd_dev(priv, out_dev)) {
					NL_SET_ERR_MSG_MOD(extack,
							   "devices are not on same switch HW, can't offload forwarding");
					return -EOPNOTSUPP;
				}

				out_priv = netdev_priv(out_dev);
				rpriv = out_priv->ppriv;
				attr->dests[attr->out_count].rep = rpriv->rep;
				attr->dests[attr->out_count].mdev = out_priv->mdev;
				attr->out_count++;
			} else if (parse_attr->filter_dev != priv->netdev) {
				/* All mlx5 devices are called to configure
				 * high level device filters. Therefore, the
				 * *attempt* to  install a filter on invalid
				 * eswitch should not trigger an explicit error
				 */
				return -EINVAL;
			} else {
				NL_SET_ERR_MSG_MOD(extack,
						   "devices are not on same switch HW, can't offload forwarding");
				netdev_warn(priv->netdev,
					    "devices %s %s not on same switch HW, can't offload forwarding\n",
					    priv->netdev->name,
					    out_dev->name);
				return -EINVAL;
			}
			}
			break;
		case FLOW_ACTION_TUNNEL_ENCAP:
			info = act->tunnel;
			if (info)
				encap = true;
			else
				return -EOPNOTSUPP;

			break;
		case FLOW_ACTION_VLAN_PUSH:
		case FLOW_ACTION_VLAN_POP:
			if (act->id == FLOW_ACTION_VLAN_PUSH &&
			    (action & MLX5_FLOW_CONTEXT_ACTION_VLAN_POP)) {
				/* Replace vlan pop+push with vlan modify */
				action &= ~MLX5_FLOW_CONTEXT_ACTION_VLAN_POP;
				err = add_vlan_rewrite_action(priv,
							      MLX5_FLOW_NAMESPACE_FDB,
							      act, parse_attr, hdrs,
							      &action, extack);
			} else {
				err = parse_tc_vlan_action(priv, act, attr, &action);
			}
			if (err)
				return err;

			attr->split_count = attr->out_count;
			break;
		case FLOW_ACTION_VLAN_MANGLE:
			err = add_vlan_rewrite_action(priv,
						      MLX5_FLOW_NAMESPACE_FDB,
						      act, parse_attr, hdrs,
						      &action, extack);
			if (err)
				return err;

			attr->split_count = attr->out_count;
			break;
		case FLOW_ACTION_TUNNEL_DECAP:
			decap = true;
			break;
		case FLOW_ACTION_GOTO:
			err = mlx5_validate_goto_chain(esw, flow, act, action,
						       extack);
			if (err)
				return err;

			action |= MLX5_FLOW_CONTEXT_ACTION_COUNT;
			attr->dest_chain = act->chain_index;
			break;
		case FLOW_ACTION_CT:
			err = mlx5_tc_ct_parse_action(priv, attr, act, extack);
			if (err)
				return err;

			flow_flag_set(flow, CT);
			break;
		default:
			NL_SET_ERR_MSG_MOD(extack, "The offload action is not supported");
			return -EOPNOTSUPP;
		}
	}

	if (MLX5_CAP_GEN(esw->dev, prio_tag_required) &&
	    action & MLX5_FLOW_CONTEXT_ACTION_VLAN_POP) {
		/* For prio tag mode, replace vlan pop with rewrite vlan prio
		 * tag rewrite.
		 */
		action &= ~MLX5_FLOW_CONTEXT_ACTION_VLAN_POP;
		err = add_vlan_prio_tag_rewrite_action(priv, parse_attr, hdrs,
						       &action, extack);
		if (err)
			return err;
	}

	if (hdrs[TCA_PEDIT_KEY_EX_CMD_SET].pedits ||
	    hdrs[TCA_PEDIT_KEY_EX_CMD_ADD].pedits) {
		err = alloc_tc_pedit_action(priv, MLX5_FLOW_NAMESPACE_FDB,
					    parse_attr, hdrs, &action, extack);
		if (err)
			return err;
		/* in case all pedit actions are skipped, remove the MOD_HDR
		 * flag. we might have set split_count either by pedit or
		 * pop/push. if there is no pop/push either, reset it too.
		 */
		if (parse_attr->mod_hdr_acts.num_actions == 0) {
			action &= ~MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
			dealloc_mod_hdr_actions(&parse_attr->mod_hdr_acts);
			if (!((action & MLX5_FLOW_CONTEXT_ACTION_VLAN_POP) ||
			      (action & MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH)))
				attr->split_count = 0;
		}
	}

	attr->action = action;
	if (!actions_match_supported(priv, flow_action, parse_attr, flow, extack))
		return -EOPNOTSUPP;

	if (attr->dest_chain) {
		if (decap) {
			/* It can be supported if we'll create a mapping for
			 * the tunnel device only (without tunnel), and set
			 * this tunnel id with this decap flow.
			 *
			 * On restore (miss), we'll just set this saved tunnel
			 * device.
			 */

			NL_SET_ERR_MSG(extack,
				       "Decap with goto isn't supported");
			netdev_warn(priv->netdev,
				    "Decap with goto isn't supported");
			return -EOPNOTSUPP;
		}

		if (attr->action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Mirroring goto chain rules isn't supported");
			return -EOPNOTSUPP;
		}
		attr->action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	}

	if (!(attr->action &
	      (MLX5_FLOW_CONTEXT_ACTION_FWD_DEST | MLX5_FLOW_CONTEXT_ACTION_DROP))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Rule must have at least one forward/drop action");
		return -EOPNOTSUPP;
	}

	if (attr->split_count > 0 && !mlx5_esw_has_fwd_fdb(priv->mdev)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "current firmware doesn't support split rule for port mirroring");
		netdev_warn_once(priv->netdev, "current firmware doesn't support split rule for port mirroring\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

static void get_flags(int flags, unsigned long *flow_flags)
{
	unsigned long __flow_flags = 0;

	if (flags & MLX5_TC_FLAG(INGRESS))
		__flow_flags |= BIT(MLX5E_TC_FLOW_FLAG_INGRESS);
	if (flags & MLX5_TC_FLAG(EGRESS))
		__flow_flags |= BIT(MLX5E_TC_FLOW_FLAG_EGRESS);

	if (flags & MLX5_TC_FLAG(ESW_OFFLOAD))
		__flow_flags |= BIT(MLX5E_TC_FLOW_FLAG_ESWITCH);
	if (flags & MLX5_TC_FLAG(NIC_OFFLOAD))
		__flow_flags |= BIT(MLX5E_TC_FLOW_FLAG_NIC);
	if (flags & MLX5_TC_FLAG(FT_OFFLOAD))
		__flow_flags |= BIT(MLX5E_TC_FLOW_FLAG_FT);

	*flow_flags = __flow_flags;
}

static const struct rhashtable_params tc_ht_params = {
	.head_offset = offsetof(struct mlx5e_tc_flow, node),
	.key_offset = offsetof(struct mlx5e_tc_flow, cookie),
	.key_len = sizeof(((struct mlx5e_tc_flow *)0)->cookie),
	.automatic_shrinking = true,
};

static struct rhashtable *get_tc_ht(struct mlx5e_priv *priv,
				    unsigned long flags)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_rep_priv *uplink_rpriv;

	if (flags & MLX5_TC_FLAG(ESW_OFFLOAD)) {
		uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
		return &uplink_rpriv->uplink_priv.tc_ht;
	} else /* NIC offload */
		return &priv->fs.tc.ht;
}

static bool is_peer_flow_needed(struct mlx5e_tc_flow *flow)
{
	struct mlx5_esw_flow_attr *attr = flow->esw_attr;
	bool is_rep_ingress = attr->in_rep->vport != MLX5_VPORT_UPLINK &&
		flow_flag_test(flow, INGRESS);
	bool act_is_encap = !!(attr->action &
			       MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT);
	bool esw_paired = mlx5_devcom_is_paired(attr->in_mdev->priv.devcom,
						MLX5_DEVCOM_ESW_OFFLOADS);

	if (!esw_paired)
		return false;

	if ((mlx5_lag_is_sriov(attr->in_mdev) ||
	     mlx5_lag_is_multipath(attr->in_mdev)) &&
	    (is_rep_ingress || act_is_encap))
		return true;

	return false;
}

static int
mlx5e_alloc_flow(struct mlx5e_priv *priv, int attr_size,
		 struct flow_cls_offload *f, unsigned long flow_flags,
		 struct mlx5e_tc_flow_parse_attr **__parse_attr,
		 struct mlx5e_tc_flow **__flow)
{
	struct mlx5e_tc_flow_parse_attr *parse_attr;
	struct mlx5e_tc_flow *flow;
	int out_index, err;

	flow = kzalloc(sizeof(*flow) + attr_size, GFP_KERNEL);
	parse_attr = kvzalloc(sizeof(*parse_attr), GFP_KERNEL);
	if (!parse_attr || !flow) {
		err = -ENOMEM;
		goto err_free;
	}

	flow->cookie = f->cookie;
	flow->flags = flow_flags;
	flow->priv = priv;
	for (out_index = 0; out_index < MLX5_MAX_FLOW_FWD_VPORTS; out_index++)
		INIT_LIST_HEAD(&flow->encaps[out_index].list);
	INIT_LIST_HEAD(&flow->hairpin);
	INIT_LIST_HEAD(&flow->l3_to_l2_reformat);
	refcount_set(&flow->refcnt, 1);
	init_completion(&flow->init_done);

	*__flow = flow;
	*__parse_attr = parse_attr;

	return 0;

err_free:
	kfree(flow);
	kvfree(parse_attr);
	return err;
}

static void
mlx5e_flow_esw_attr_init(struct mlx5_esw_flow_attr *esw_attr,
			 struct mlx5e_priv *priv,
			 struct mlx5e_tc_flow_parse_attr *parse_attr,
			 struct flow_cls_offload *f,
			 struct mlx5_eswitch_rep *in_rep,
			 struct mlx5_core_dev *in_mdev)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;

	esw_attr->parse_attr = parse_attr;
	esw_attr->chain = f->common.chain_index;
	esw_attr->prio = f->common.prio;

	esw_attr->in_rep = in_rep;
	esw_attr->in_mdev = in_mdev;

	if (MLX5_CAP_ESW(esw->dev, counter_eswitch_affinity) ==
	    MLX5_COUNTER_SOURCE_ESWITCH)
		esw_attr->counter_dev = in_mdev;
	else
		esw_attr->counter_dev = priv->mdev;
}

static struct mlx5e_tc_flow *
__mlx5e_add_fdb_flow(struct mlx5e_priv *priv,
		     struct flow_cls_offload *f,
		     unsigned long flow_flags,
		     struct net_device *filter_dev,
		     struct mlx5_eswitch_rep *in_rep,
		     struct mlx5_core_dev *in_mdev)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct netlink_ext_ack *extack = f->common.extack;
	struct mlx5e_tc_flow_parse_attr *parse_attr;
	struct mlx5e_tc_flow *flow;
	int attr_size, err;

	flow_flags |= BIT(MLX5E_TC_FLOW_FLAG_ESWITCH);
	attr_size  = sizeof(struct mlx5_esw_flow_attr);
	err = mlx5e_alloc_flow(priv, attr_size, f, flow_flags,
			       &parse_attr, &flow);
	if (err)
		goto out;

	parse_attr->filter_dev = filter_dev;
	mlx5e_flow_esw_attr_init(flow->esw_attr,
				 priv, parse_attr,
				 f, in_rep, in_mdev);

	err = parse_cls_flower(flow->priv, flow, &parse_attr->spec,
			       f, filter_dev);
	if (err)
		goto err_free;

	/* actions validation depends on parsing the ct matches first */
	err = mlx5_tc_ct_match_add(priv, &parse_attr->spec, f,
				   &flow->esw_attr->ct_attr, extack);
	if (err)
		goto err_free;

	err = parse_tc_fdb_actions(priv, &rule->action, flow, extack, filter_dev);
	if (err)
		goto err_free;

	err = mlx5e_tc_add_fdb_flow(priv, flow, extack);
	complete_all(&flow->init_done);
	if (err) {
		if (!(err == -ENETUNREACH && mlx5_lag_is_multipath(in_mdev)))
			goto err_free;

		add_unready_flow(flow);
	}

	return flow;

err_free:
	mlx5e_flow_put(priv, flow);
out:
	return ERR_PTR(err);
}

static int mlx5e_tc_add_fdb_peer_flow(struct flow_cls_offload *f,
				      struct mlx5e_tc_flow *flow,
				      unsigned long flow_flags)
{
	struct mlx5e_priv *priv = flow->priv, *peer_priv;
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch, *peer_esw;
	struct mlx5_devcom *devcom = priv->mdev->priv.devcom;
	struct mlx5e_tc_flow_parse_attr *parse_attr;
	struct mlx5e_rep_priv *peer_urpriv;
	struct mlx5e_tc_flow *peer_flow;
	struct mlx5_core_dev *in_mdev;
	int err = 0;

	peer_esw = mlx5_devcom_get_peer_data(devcom, MLX5_DEVCOM_ESW_OFFLOADS);
	if (!peer_esw)
		return -ENODEV;

	peer_urpriv = mlx5_eswitch_get_uplink_priv(peer_esw, REP_ETH);
	peer_priv = netdev_priv(peer_urpriv->netdev);

	/* in_mdev is assigned of which the packet originated from.
	 * So packets redirected to uplink use the same mdev of the
	 * original flow and packets redirected from uplink use the
	 * peer mdev.
	 */
	if (flow->esw_attr->in_rep->vport == MLX5_VPORT_UPLINK)
		in_mdev = peer_priv->mdev;
	else
		in_mdev = priv->mdev;

	parse_attr = flow->esw_attr->parse_attr;
	peer_flow = __mlx5e_add_fdb_flow(peer_priv, f, flow_flags,
					 parse_attr->filter_dev,
					 flow->esw_attr->in_rep, in_mdev);
	if (IS_ERR(peer_flow)) {
		err = PTR_ERR(peer_flow);
		goto out;
	}

	flow->peer_flow = peer_flow;
	flow_flag_set(flow, DUP);
	mutex_lock(&esw->offloads.peer_mutex);
	list_add_tail(&flow->peer, &esw->offloads.peer_flows);
	mutex_unlock(&esw->offloads.peer_mutex);

out:
	mlx5_devcom_release_peer_data(devcom, MLX5_DEVCOM_ESW_OFFLOADS);
	return err;
}

static int
mlx5e_add_fdb_flow(struct mlx5e_priv *priv,
		   struct flow_cls_offload *f,
		   unsigned long flow_flags,
		   struct net_device *filter_dev,
		   struct mlx5e_tc_flow **__flow)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *in_rep = rpriv->rep;
	struct mlx5_core_dev *in_mdev = priv->mdev;
	struct mlx5e_tc_flow *flow;
	int err;

	flow = __mlx5e_add_fdb_flow(priv, f, flow_flags, filter_dev, in_rep,
				    in_mdev);
	if (IS_ERR(flow))
		return PTR_ERR(flow);

	if (is_peer_flow_needed(flow)) {
		err = mlx5e_tc_add_fdb_peer_flow(f, flow, flow_flags);
		if (err) {
			mlx5e_tc_del_fdb_flow(priv, flow);
			goto out;
		}
	}

	*__flow = flow;

	return 0;

out:
	return err;
}

static int
mlx5e_add_nic_flow(struct mlx5e_priv *priv,
		   struct flow_cls_offload *f,
		   unsigned long flow_flags,
		   struct net_device *filter_dev,
		   struct mlx5e_tc_flow **__flow)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct netlink_ext_ack *extack = f->common.extack;
	struct mlx5e_tc_flow_parse_attr *parse_attr;
	struct mlx5e_tc_flow *flow;
	int attr_size, err;

	/* multi-chain not supported for NIC rules */
	if (!tc_cls_can_offload_and_chain0(priv->netdev, &f->common))
		return -EOPNOTSUPP;

	flow_flags |= BIT(MLX5E_TC_FLOW_FLAG_NIC);
	attr_size  = sizeof(struct mlx5_nic_flow_attr);
	err = mlx5e_alloc_flow(priv, attr_size, f, flow_flags,
			       &parse_attr, &flow);
	if (err)
		goto out;

	parse_attr->filter_dev = filter_dev;
	err = parse_cls_flower(flow->priv, flow, &parse_attr->spec,
			       f, filter_dev);
	if (err)
		goto err_free;

	err = parse_tc_nic_actions(priv, &rule->action, parse_attr, flow, extack);
	if (err)
		goto err_free;

	err = mlx5e_tc_add_nic_flow(priv, parse_attr, flow, extack);
	if (err)
		goto err_free;

	flow_flag_set(flow, OFFLOADED);
	kvfree(parse_attr);
	*__flow = flow;

	return 0;

err_free:
	mlx5e_flow_put(priv, flow);
	kvfree(parse_attr);
out:
	return err;
}

static int
mlx5e_tc_add_flow(struct mlx5e_priv *priv,
		  struct flow_cls_offload *f,
		  unsigned long flags,
		  struct net_device *filter_dev,
		  struct mlx5e_tc_flow **flow)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	unsigned long flow_flags;
	int err;

	get_flags(flags, &flow_flags);

	if (!tc_can_offload_extack(priv->netdev, f->common.extack))
		return -EOPNOTSUPP;

	if (esw && esw->mode == MLX5_ESWITCH_OFFLOADS)
		err = mlx5e_add_fdb_flow(priv, f, flow_flags,
					 filter_dev, flow);
	else
		err = mlx5e_add_nic_flow(priv, f, flow_flags,
					 filter_dev, flow);

	return err;
}

static bool is_flow_rule_duplicate_allowed(struct net_device *dev,
					   struct mlx5e_rep_priv *rpriv)
{
	/* Offloaded flow rule is allowed to duplicate on non-uplink representor
	 * sharing tc block with other slaves of a lag device. Rpriv can be NULL if this
	 * function is called from NIC mode.
	 */
	return netif_is_lag_port(dev) && rpriv && rpriv->rep->vport != MLX5_VPORT_UPLINK;
}

int mlx5e_configure_flower(struct net_device *dev, struct mlx5e_priv *priv,
			   struct flow_cls_offload *f, unsigned long flags)
{
	struct netlink_ext_ack *extack = f->common.extack;
	struct rhashtable *tc_ht = get_tc_ht(priv, flags);
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5e_tc_flow *flow;
	int err = 0;

	rcu_read_lock();
	flow = rhashtable_lookup(tc_ht, &f->cookie, tc_ht_params);
	if (flow) {
		/* Same flow rule offloaded to non-uplink representor sharing tc block,
		 * just return 0.
		 */
		if (is_flow_rule_duplicate_allowed(dev, rpriv) && flow->orig_dev != dev)
			goto rcu_unlock;

		NL_SET_ERR_MSG_MOD(extack,
				   "flow cookie already exists, ignoring");
		netdev_warn_once(priv->netdev,
				 "flow cookie %lx already exists, ignoring\n",
				 f->cookie);
		err = -EEXIST;
		goto rcu_unlock;
	}
rcu_unlock:
	rcu_read_unlock();
	if (flow)
		goto out;

	trace_mlx5e_configure_flower(f);
	err = mlx5e_tc_add_flow(priv, f, flags, dev, &flow);
	if (err)
		goto out;

	/* Flow rule offloaded to non-uplink representor sharing tc block,
	 * set the flow's owner dev.
	 */
	if (is_flow_rule_duplicate_allowed(dev, rpriv))
		flow->orig_dev = dev;

	err = rhashtable_lookup_insert_fast(tc_ht, &flow->node, tc_ht_params);
	if (err)
		goto err_free;

	return 0;

err_free:
	mlx5e_flow_put(priv, flow);
out:
	return err;
}

static bool same_flow_direction(struct mlx5e_tc_flow *flow, int flags)
{
	bool dir_ingress = !!(flags & MLX5_TC_FLAG(INGRESS));
	bool dir_egress = !!(flags & MLX5_TC_FLAG(EGRESS));

	return flow_flag_test(flow, INGRESS) == dir_ingress &&
		flow_flag_test(flow, EGRESS) == dir_egress;
}

int mlx5e_delete_flower(struct net_device *dev, struct mlx5e_priv *priv,
			struct flow_cls_offload *f, unsigned long flags)
{
	struct rhashtable *tc_ht = get_tc_ht(priv, flags);
	struct mlx5e_tc_flow *flow;
	int err;

	rcu_read_lock();
	flow = rhashtable_lookup(tc_ht, &f->cookie, tc_ht_params);
	if (!flow || !same_flow_direction(flow, flags)) {
		err = -EINVAL;
		goto errout;
	}

	/* Only delete the flow if it doesn't have MLX5E_TC_FLOW_DELETED flag
	 * set.
	 */
	if (flow_flag_test_and_set(flow, DELETED)) {
		err = -EINVAL;
		goto errout;
	}
	rhashtable_remove_fast(tc_ht, &flow->node, tc_ht_params);
	rcu_read_unlock();

	trace_mlx5e_delete_flower(f);
	mlx5e_flow_put(priv, flow);

	return 0;

errout:
	rcu_read_unlock();
	return err;
}

int mlx5e_stats_flower(struct net_device *dev, struct mlx5e_priv *priv,
		       struct flow_cls_offload *f, unsigned long flags)
{
	struct mlx5_devcom *devcom = priv->mdev->priv.devcom;
	struct rhashtable *tc_ht = get_tc_ht(priv, flags);
	struct mlx5_eswitch *peer_esw;
	struct mlx5e_tc_flow *flow;
	struct mlx5_fc *counter;
	u64 lastuse = 0;
	u64 packets = 0;
	u64 bytes = 0;
	int err = 0;

	rcu_read_lock();
	flow = mlx5e_flow_get(rhashtable_lookup(tc_ht, &f->cookie,
						tc_ht_params));
	rcu_read_unlock();
	if (IS_ERR(flow))
		return PTR_ERR(flow);

	if (!same_flow_direction(flow, flags)) {
		err = -EINVAL;
		goto errout;
	}

	if (mlx5e_is_offloaded_flow(flow) || flow_flag_test(flow, CT)) {
		counter = mlx5e_tc_get_counter(flow);
		if (!counter)
			goto errout;

		mlx5_fc_query_cached(counter, &bytes, &packets, &lastuse);
	}

	/* Under multipath it's possible for one rule to be currently
	 * un-offloaded while the other rule is offloaded.
	 */
	peer_esw = mlx5_devcom_get_peer_data(devcom, MLX5_DEVCOM_ESW_OFFLOADS);
	if (!peer_esw)
		goto out;

	if (flow_flag_test(flow, DUP) &&
	    flow_flag_test(flow->peer_flow, OFFLOADED)) {
		u64 bytes2;
		u64 packets2;
		u64 lastuse2;

		counter = mlx5e_tc_get_counter(flow->peer_flow);
		if (!counter)
			goto no_peer_counter;
		mlx5_fc_query_cached(counter, &bytes2, &packets2, &lastuse2);

		bytes += bytes2;
		packets += packets2;
		lastuse = max_t(u64, lastuse, lastuse2);
	}

no_peer_counter:
	mlx5_devcom_release_peer_data(devcom, MLX5_DEVCOM_ESW_OFFLOADS);
out:
	flow_stats_update(&f->stats, bytes, packets, 0, lastuse,
			  FLOW_ACTION_HW_STATS_DELAYED);
	trace_mlx5e_stats_flower(f);
errout:
	mlx5e_flow_put(priv, flow);
	return err;
}

static int apply_police_params(struct mlx5e_priv *priv, u32 rate,
			       struct netlink_ext_ack *extack)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch *esw;
	u16 vport_num;
	u32 rate_mbps;
	int err;

	vport_num = rpriv->rep->vport;
	if (vport_num >= MLX5_VPORT_ECPF) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Ingress rate limit is supported only for Eswitch ports connected to VFs");
		return -EOPNOTSUPP;
	}

	esw = priv->mdev->priv.eswitch;
	/* rate is given in bytes/sec.
	 * First convert to bits/sec and then round to the nearest mbit/secs.
	 * mbit means million bits.
	 * Moreover, if rate is non zero we choose to configure to a minimum of
	 * 1 mbit/sec.
	 */
	rate_mbps = rate ? max_t(u32, (rate * 8 + 500000) / 1000000, 1) : 0;
	err = mlx5_esw_modify_vport_rate(esw, vport_num, rate_mbps);
	if (err)
		NL_SET_ERR_MSG_MOD(extack, "failed applying action to hardware");

	return err;
}

static int scan_tc_matchall_fdb_actions(struct mlx5e_priv *priv,
					struct flow_action *flow_action,
					struct netlink_ext_ack *extack)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	const struct flow_action_entry *act;
	int err;
	int i;

	if (!flow_action_has_entries(flow_action)) {
		NL_SET_ERR_MSG_MOD(extack, "matchall called with no action");
		return -EINVAL;
	}

	if (!flow_offload_has_one_action(flow_action)) {
		NL_SET_ERR_MSG_MOD(extack, "matchall policing support only a single action");
		return -EOPNOTSUPP;
	}

	if (!flow_action_basic_hw_stats_check(flow_action, extack))
		return -EOPNOTSUPP;

	flow_action_for_each(i, act, flow_action) {
		switch (act->id) {
		case FLOW_ACTION_POLICE:
			err = apply_police_params(priv, act->police.rate_bytes_ps, extack);
			if (err)
				return err;

			rpriv->prev_vf_vport_stats = priv->stats.vf_vport;
			break;
		default:
			NL_SET_ERR_MSG_MOD(extack, "mlx5 supports only police action for matchall");
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

int mlx5e_tc_configure_matchall(struct mlx5e_priv *priv,
				struct tc_cls_matchall_offload *ma)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct netlink_ext_ack *extack = ma->common.extack;

	if (!mlx5_esw_qos_enabled(esw)) {
		NL_SET_ERR_MSG_MOD(extack, "QoS is not supported on this device");
		return -EOPNOTSUPP;
	}

	if (ma->common.prio != 1) {
		NL_SET_ERR_MSG_MOD(extack, "only priority 1 is supported");
		return -EINVAL;
	}

	return scan_tc_matchall_fdb_actions(priv, &ma->rule->action, extack);
}

int mlx5e_tc_delete_matchall(struct mlx5e_priv *priv,
			     struct tc_cls_matchall_offload *ma)
{
	struct netlink_ext_ack *extack = ma->common.extack;

	return apply_police_params(priv, 0, extack);
}

void mlx5e_tc_stats_matchall(struct mlx5e_priv *priv,
			     struct tc_cls_matchall_offload *ma)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct rtnl_link_stats64 cur_stats;
	u64 dbytes;
	u64 dpkts;

	cur_stats = priv->stats.vf_vport;
	dpkts = cur_stats.rx_packets - rpriv->prev_vf_vport_stats.rx_packets;
	dbytes = cur_stats.rx_bytes - rpriv->prev_vf_vport_stats.rx_bytes;
	rpriv->prev_vf_vport_stats = cur_stats;
	flow_stats_update(&ma->stats, dbytes, dpkts, 0, jiffies,
			  FLOW_ACTION_HW_STATS_DELAYED);
}

static void mlx5e_tc_hairpin_update_dead_peer(struct mlx5e_priv *priv,
					      struct mlx5e_priv *peer_priv)
{
	struct mlx5_core_dev *peer_mdev = peer_priv->mdev;
	struct mlx5e_hairpin_entry *hpe, *tmp;
	LIST_HEAD(init_wait_list);
	u16 peer_vhca_id;
	int bkt;

	if (!same_hw_devs(priv, peer_priv))
		return;

	peer_vhca_id = MLX5_CAP_GEN(peer_mdev, vhca_id);

	mutex_lock(&priv->fs.tc.hairpin_tbl_lock);
	hash_for_each(priv->fs.tc.hairpin_tbl, bkt, hpe, hairpin_hlist)
		if (refcount_inc_not_zero(&hpe->refcnt))
			list_add(&hpe->dead_peer_wait_list, &init_wait_list);
	mutex_unlock(&priv->fs.tc.hairpin_tbl_lock);

	list_for_each_entry_safe(hpe, tmp, &init_wait_list, dead_peer_wait_list) {
		wait_for_completion(&hpe->res_ready);
		if (!IS_ERR_OR_NULL(hpe->hp) && hpe->peer_vhca_id == peer_vhca_id)
			hpe->hp->pair->peer_gone = true;

		mlx5e_hairpin_put(priv, hpe);
	}
}

static int mlx5e_tc_netdev_event(struct notifier_block *this,
				 unsigned long event, void *ptr)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	struct mlx5e_flow_steering *fs;
	struct mlx5e_priv *peer_priv;
	struct mlx5e_tc_table *tc;
	struct mlx5e_priv *priv;

	if (ndev->netdev_ops != &mlx5e_netdev_ops ||
	    event != NETDEV_UNREGISTER ||
	    ndev->reg_state == NETREG_REGISTERED)
		return NOTIFY_DONE;

	tc = container_of(this, struct mlx5e_tc_table, netdevice_nb);
	fs = container_of(tc, struct mlx5e_flow_steering, tc);
	priv = container_of(fs, struct mlx5e_priv, fs);
	peer_priv = netdev_priv(ndev);
	if (priv == peer_priv ||
	    !(priv->netdev->features & NETIF_F_HW_TC))
		return NOTIFY_DONE;

	mlx5e_tc_hairpin_update_dead_peer(priv, peer_priv);

	return NOTIFY_DONE;
}

int mlx5e_tc_nic_init(struct mlx5e_priv *priv)
{
	struct mlx5e_tc_table *tc = &priv->fs.tc;
	int err;

	mlx5e_mod_hdr_tbl_init(&tc->mod_hdr);
	mutex_init(&tc->t_lock);
	mutex_init(&tc->hairpin_tbl_lock);
	hash_init(tc->hairpin_tbl);

	err = rhashtable_init(&tc->ht, &tc_ht_params);
	if (err)
		return err;

	tc->netdevice_nb.notifier_call = mlx5e_tc_netdev_event;
	err = register_netdevice_notifier_dev_net(priv->netdev,
						  &tc->netdevice_nb,
						  &tc->netdevice_nn);
	if (err) {
		tc->netdevice_nb.notifier_call = NULL;
		mlx5_core_warn(priv->mdev, "Failed to register netdev notifier\n");
	}

	return err;
}

static void _mlx5e_tc_del_flow(void *ptr, void *arg)
{
	struct mlx5e_tc_flow *flow = ptr;
	struct mlx5e_priv *priv = flow->priv;

	mlx5e_tc_del_flow(priv, flow);
	kfree(flow);
}

void mlx5e_tc_nic_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5e_tc_table *tc = &priv->fs.tc;

	if (tc->netdevice_nb.notifier_call)
		unregister_netdevice_notifier_dev_net(priv->netdev,
						      &tc->netdevice_nb,
						      &tc->netdevice_nn);

	mlx5e_mod_hdr_tbl_destroy(&tc->mod_hdr);
	mutex_destroy(&tc->hairpin_tbl_lock);

	rhashtable_destroy(&tc->ht);

	if (!IS_ERR_OR_NULL(tc->t)) {
		mlx5_destroy_flow_table(tc->t);
		tc->t = NULL;
	}
	mutex_destroy(&tc->t_lock);
}

int mlx5e_tc_esw_init(struct rhashtable *tc_ht)
{
	const size_t sz_enc_opts = sizeof(struct tunnel_match_enc_opts);
	struct mlx5_rep_uplink_priv *uplink_priv;
	struct mlx5e_rep_priv *priv;
	struct mapping_ctx *mapping;
	int err;

	uplink_priv = container_of(tc_ht, struct mlx5_rep_uplink_priv, tc_ht);
	priv = container_of(uplink_priv, struct mlx5e_rep_priv, uplink_priv);

	err = mlx5_tc_ct_init(uplink_priv);
	if (err)
		goto err_ct;

	mapping = mapping_create(sizeof(struct tunnel_match_key),
				 TUNNEL_INFO_BITS_MASK, true);
	if (IS_ERR(mapping)) {
		err = PTR_ERR(mapping);
		goto err_tun_mapping;
	}
	uplink_priv->tunnel_mapping = mapping;

	mapping = mapping_create(sz_enc_opts, ENC_OPTS_BITS_MASK, true);
	if (IS_ERR(mapping)) {
		err = PTR_ERR(mapping);
		goto err_enc_opts_mapping;
	}
	uplink_priv->tunnel_enc_opts_mapping = mapping;

	err = rhashtable_init(tc_ht, &tc_ht_params);
	if (err)
		goto err_ht_init;

	return err;

err_ht_init:
	mapping_destroy(uplink_priv->tunnel_enc_opts_mapping);
err_enc_opts_mapping:
	mapping_destroy(uplink_priv->tunnel_mapping);
err_tun_mapping:
	mlx5_tc_ct_clean(uplink_priv);
err_ct:
	netdev_warn(priv->netdev,
		    "Failed to initialize tc (eswitch), err: %d", err);
	return err;
}

void mlx5e_tc_esw_cleanup(struct rhashtable *tc_ht)
{
	struct mlx5_rep_uplink_priv *uplink_priv;

	rhashtable_free_and_destroy(tc_ht, _mlx5e_tc_del_flow, NULL);

	uplink_priv = container_of(tc_ht, struct mlx5_rep_uplink_priv, tc_ht);
	mapping_destroy(uplink_priv->tunnel_enc_opts_mapping);
	mapping_destroy(uplink_priv->tunnel_mapping);

	mlx5_tc_ct_clean(uplink_priv);
}

int mlx5e_tc_num_filters(struct mlx5e_priv *priv, unsigned long flags)
{
	struct rhashtable *tc_ht = get_tc_ht(priv, flags);

	return atomic_read(&tc_ht->nelems);
}

void mlx5e_tc_clean_fdb_peer_flows(struct mlx5_eswitch *esw)
{
	struct mlx5e_tc_flow *flow, *tmp;

	list_for_each_entry_safe(flow, tmp, &esw->offloads.peer_flows, peer)
		__mlx5e_tc_del_fdb_peer_flow(flow);
}

void mlx5e_tc_reoffload_flows_work(struct work_struct *work)
{
	struct mlx5_rep_uplink_priv *rpriv =
		container_of(work, struct mlx5_rep_uplink_priv,
			     reoffload_flows_work);
	struct mlx5e_tc_flow *flow, *tmp;

	mutex_lock(&rpriv->unready_flows_lock);
	list_for_each_entry_safe(flow, tmp, &rpriv->unready_flows, unready) {
		if (!mlx5e_tc_add_fdb_flow(flow->priv, flow, NULL))
			unready_flow_del(flow);
	}
	mutex_unlock(&rpriv->unready_flows_lock);
}

static int mlx5e_setup_tc_cls_flower(struct mlx5e_priv *priv,
				     struct flow_cls_offload *cls_flower,
				     unsigned long flags)
{
	switch (cls_flower->command) {
	case FLOW_CLS_REPLACE:
		return mlx5e_configure_flower(priv->netdev, priv, cls_flower,
					      flags);
	case FLOW_CLS_DESTROY:
		return mlx5e_delete_flower(priv->netdev, priv, cls_flower,
					   flags);
	case FLOW_CLS_STATS:
		return mlx5e_stats_flower(priv->netdev, priv, cls_flower,
					  flags);
	default:
		return -EOPNOTSUPP;
	}
}

int mlx5e_setup_tc_block_cb(enum tc_setup_type type, void *type_data,
			    void *cb_priv)
{
	unsigned long flags = MLX5_TC_FLAG(INGRESS) | MLX5_TC_FLAG(NIC_OFFLOAD);
	struct mlx5e_priv *priv = cb_priv;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return mlx5e_setup_tc_cls_flower(priv, type_data, flags);
	default:
		return -EOPNOTSUPP;
	}
}
