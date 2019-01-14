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
#include <net/sch_generic.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_skbedit.h>
#include <linux/mlx5/fs.h>
#include <linux/mlx5/device.h>
#include <linux/rhashtable.h>
#include <net/switchdev.h>
#include <net/tc_act/tc_mirred.h>
#include <net/tc_act/tc_vlan.h>
#include <net/tc_act/tc_tunnel_key.h>
#include <net/tc_act/tc_pedit.h>
#include <net/tc_act/tc_csum.h>
#include <net/vxlan.h>
#include <net/arp.h>
#include "en.h"
#include "en_rep.h"
#include "en_tc.h"
#include "eswitch.h"
#include "lib/vxlan.h"
#include "fs_core.h"
#include "en/port.h"

struct mlx5_nic_flow_attr {
	u32 action;
	u32 flow_tag;
	u32 mod_hdr_id;
	u32 hairpin_tirn;
	u8 match_level;
	struct mlx5_flow_table	*hairpin_ft;
};

#define MLX5E_TC_FLOW_BASE (MLX5E_TC_LAST_EXPORTED_BIT + 1)

enum {
	MLX5E_TC_FLOW_INGRESS	= MLX5E_TC_INGRESS,
	MLX5E_TC_FLOW_EGRESS	= MLX5E_TC_EGRESS,
	MLX5E_TC_FLOW_ESWITCH	= BIT(MLX5E_TC_FLOW_BASE),
	MLX5E_TC_FLOW_NIC	= BIT(MLX5E_TC_FLOW_BASE + 1),
	MLX5E_TC_FLOW_OFFLOADED	= BIT(MLX5E_TC_FLOW_BASE + 2),
	MLX5E_TC_FLOW_HAIRPIN	= BIT(MLX5E_TC_FLOW_BASE + 3),
	MLX5E_TC_FLOW_HAIRPIN_RSS = BIT(MLX5E_TC_FLOW_BASE + 4),
};

#define MLX5E_TC_MAX_SPLITS 1

struct mlx5e_tc_flow {
	struct rhash_head	node;
	struct mlx5e_priv	*priv;
	u64			cookie;
	u8			flags;
	struct mlx5_flow_handle *rule[MLX5E_TC_MAX_SPLITS + 1];
	struct list_head	encap;   /* flows sharing the same encap ID */
	struct list_head	mod_hdr; /* flows sharing the same mod hdr ID */
	struct list_head	hairpin; /* flows sharing the same hairpin */
	union {
		struct mlx5_esw_flow_attr esw_attr[0];
		struct mlx5_nic_flow_attr nic_attr[0];
	};
};

struct mlx5e_tc_flow_parse_attr {
	struct ip_tunnel_info tun_info;
	struct mlx5_flow_spec spec;
	int num_mod_hdr_actions;
	void *mod_hdr_actions;
	int mirred_ifindex;
};

enum {
	MLX5_HEADER_TYPE_VXLAN = 0x0,
	MLX5_HEADER_TYPE_NVGRE = 0x1,
};

#define MLX5E_TC_TABLE_NUM_GROUPS 4
#define MLX5E_TC_TABLE_MAX_GROUP_SIZE BIT(16)

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

	/* flows sharing the same hairpin */
	struct list_head flows;

	u16 peer_vhca_id;
	u8 prio;
	struct mlx5e_hairpin *hp;
};

struct mod_hdr_key {
	int num_actions;
	void *actions;
};

struct mlx5e_mod_hdr_entry {
	/* a node of a hash table which keeps all the mod_hdr entries */
	struct hlist_node mod_hdr_hlist;

	/* flows sharing the same mod_hdr entry */
	struct list_head flows;

	struct mod_hdr_key key;

	u32 mod_hdr_id;
};

#define MLX5_MH_ACT_SZ MLX5_UN_SZ_BYTES(set_action_in_add_action_in_auto)

static inline u32 hash_mod_hdr_info(struct mod_hdr_key *key)
{
	return jhash(key->actions,
		     key->num_actions * MLX5_MH_ACT_SZ, 0);
}

static inline int cmp_mod_hdr_info(struct mod_hdr_key *a,
				   struct mod_hdr_key *b)
{
	if (a->num_actions != b->num_actions)
		return 1;

	return memcmp(a->actions, b->actions, a->num_actions * MLX5_MH_ACT_SZ);
}

static int mlx5e_attach_mod_hdr(struct mlx5e_priv *priv,
				struct mlx5e_tc_flow *flow,
				struct mlx5e_tc_flow_parse_attr *parse_attr)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	int num_actions, actions_size, namespace, err;
	struct mlx5e_mod_hdr_entry *mh;
	struct mod_hdr_key key;
	bool found = false;
	u32 hash_key;

	num_actions  = parse_attr->num_mod_hdr_actions;
	actions_size = MLX5_MH_ACT_SZ * num_actions;

	key.actions = parse_attr->mod_hdr_actions;
	key.num_actions = num_actions;

	hash_key = hash_mod_hdr_info(&key);

	if (flow->flags & MLX5E_TC_FLOW_ESWITCH) {
		namespace = MLX5_FLOW_NAMESPACE_FDB;
		hash_for_each_possible(esw->offloads.mod_hdr_tbl, mh,
				       mod_hdr_hlist, hash_key) {
			if (!cmp_mod_hdr_info(&mh->key, &key)) {
				found = true;
				break;
			}
		}
	} else {
		namespace = MLX5_FLOW_NAMESPACE_KERNEL;
		hash_for_each_possible(priv->fs.tc.mod_hdr_tbl, mh,
				       mod_hdr_hlist, hash_key) {
			if (!cmp_mod_hdr_info(&mh->key, &key)) {
				found = true;
				break;
			}
		}
	}

	if (found)
		goto attach_flow;

	mh = kzalloc(sizeof(*mh) + actions_size, GFP_KERNEL);
	if (!mh)
		return -ENOMEM;

	mh->key.actions = (void *)mh + sizeof(*mh);
	memcpy(mh->key.actions, key.actions, actions_size);
	mh->key.num_actions = num_actions;
	INIT_LIST_HEAD(&mh->flows);

	err = mlx5_modify_header_alloc(priv->mdev, namespace,
				       mh->key.num_actions,
				       mh->key.actions,
				       &mh->mod_hdr_id);
	if (err)
		goto out_err;

	if (flow->flags & MLX5E_TC_FLOW_ESWITCH)
		hash_add(esw->offloads.mod_hdr_tbl, &mh->mod_hdr_hlist, hash_key);
	else
		hash_add(priv->fs.tc.mod_hdr_tbl, &mh->mod_hdr_hlist, hash_key);

attach_flow:
	list_add(&flow->mod_hdr, &mh->flows);
	if (flow->flags & MLX5E_TC_FLOW_ESWITCH)
		flow->esw_attr->mod_hdr_id = mh->mod_hdr_id;
	else
		flow->nic_attr->mod_hdr_id = mh->mod_hdr_id;

	return 0;

out_err:
	kfree(mh);
	return err;
}

static void mlx5e_detach_mod_hdr(struct mlx5e_priv *priv,
				 struct mlx5e_tc_flow *flow)
{
	struct list_head *next = flow->mod_hdr.next;

	list_del(&flow->mod_hdr);

	if (list_empty(next)) {
		struct mlx5e_mod_hdr_entry *mh;

		mh = list_entry(next, struct mlx5e_mod_hdr_entry, flows);

		mlx5_modify_header_dealloc(priv->mdev, mh->mod_hdr_id);
		hash_del(&mh->mod_hdr_hlist);
		kfree(mh);
	}
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
	u32 in[MLX5_ST_SZ_DW(create_tir_in)] = {0};
	void *tirc;
	int err;

	err = mlx5_core_alloc_transport_domain(hp->func_mdev, &hp->tdn);
	if (err)
		goto alloc_tdn_err;

	tirc = MLX5_ADDR_OF(create_tir_in, in, ctx);

	MLX5_SET(tirc, tirc, disp_type, MLX5_TIRC_DISP_TYPE_DIRECT);
	MLX5_SET(tirc, tirc, inline_rqn, hp->pair->rqn[0]);
	MLX5_SET(tirc, tirc, transport_domain, hp->tdn);

	err = mlx5_core_create_tir(hp->func_mdev, in, MLX5_ST_SZ_BYTES(create_tir_in), &hp->tirn);
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
		if (priv->channels.params.rss_hfunc == ETH_RSS_HASH_XOR)
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
		memset(in, 0, MLX5_ST_SZ_BYTES(create_tir_in));
		tirc = MLX5_ADDR_OF(create_tir_in, in, ctx);

		MLX5_SET(tirc, tirc, transport_domain, hp->tdn);
		MLX5_SET(tirc, tirc, disp_type, MLX5_TIRC_DISP_TYPE_INDIRECT);
		MLX5_SET(tirc, tirc, indirect_table, hp->indir_rqt.rqtn);
		mlx5e_build_indir_tir_ctx_hash(&priv->channels.params, tt, tirc, false);

		err = mlx5_core_create_tir(hp->func_mdev, in,
					   MLX5_ST_SZ_BYTES(create_tir_in), &hp->indir_tirn[tt]);
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

	ft_attr->max_fte = MLX5E_NUM_TT;
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
		if (hpe->peer_vhca_id == peer_vhca_id && hpe->prio == prio)
			return hpe;
	}

	return NULL;
}

#define UNKNOWN_MATCH_PRIO 8

static int mlx5e_hairpin_get_prio(struct mlx5e_priv *priv,
				  struct mlx5_flow_spec *spec, u8 *match_prio)
{
	void *headers_c, *headers_v;
	u8 prio_val, prio_mask = 0;
	bool vlan_present;

#ifdef CONFIG_MLX5_CORE_EN_DCB
	if (priv->dcbx_dp.trust_state != MLX5_QPTS_TRUST_PCP) {
		netdev_warn(priv->netdev,
			    "only PCP trust state supported for hairpin\n");
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
		netdev_warn(priv->netdev,
			    "masked priority match not supported for hairpin\n");
		return -EOPNOTSUPP;
	}

	*match_prio = prio_val;
	return 0;
}

static int mlx5e_hairpin_flow_add(struct mlx5e_priv *priv,
				  struct mlx5e_tc_flow *flow,
				  struct mlx5e_tc_flow_parse_attr *parse_attr)
{
	int peer_ifindex = parse_attr->mirred_ifindex;
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
		netdev_warn(priv->netdev, "hairpin is not supported\n");
		return -EOPNOTSUPP;
	}

	peer_id = MLX5_CAP_GEN(peer_mdev, vhca_id);
	err = mlx5e_hairpin_get_prio(priv, &parse_attr->spec, &match_prio);
	if (err)
		return err;
	hpe = mlx5e_hairpin_get(priv, peer_id, match_prio);
	if (hpe)
		goto attach_flow;

	hpe = kzalloc(sizeof(*hpe), GFP_KERNEL);
	if (!hpe)
		return -ENOMEM;

	INIT_LIST_HEAD(&hpe->flows);
	hpe->peer_vhca_id = peer_id;
	hpe->prio = match_prio;

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
	if (IS_ERR(hp)) {
		err = PTR_ERR(hp);
		goto create_hairpin_err;
	}

	netdev_dbg(priv->netdev, "add hairpin: tirn %x rqn %x peer %s sqn %x prio %d (log) data %d packets %d\n",
		   hp->tirn, hp->pair->rqn[0], hp->pair->peer_mdev->priv.name,
		   hp->pair->sqn[0], match_prio, params.log_data_size, params.log_num_packets);

	hpe->hp = hp;
	hash_add(priv->fs.tc.hairpin_tbl, &hpe->hairpin_hlist,
		 hash_hairpin_info(peer_id, match_prio));

attach_flow:
	if (hpe->hp->num_channels > 1) {
		flow->flags |= MLX5E_TC_FLOW_HAIRPIN_RSS;
		flow->nic_attr->hairpin_ft = hpe->hp->ttc.ft.t;
	} else {
		flow->nic_attr->hairpin_tirn = hpe->hp->tirn;
	}
	list_add(&flow->hairpin, &hpe->flows);

	return 0;

create_hairpin_err:
	kfree(hpe);
	return err;
}

static void mlx5e_hairpin_flow_del(struct mlx5e_priv *priv,
				   struct mlx5e_tc_flow *flow)
{
	struct list_head *next = flow->hairpin.next;

	list_del(&flow->hairpin);

	/* no more hairpin flows for us, release the hairpin pair */
	if (list_empty(next)) {
		struct mlx5e_hairpin_entry *hpe;

		hpe = list_entry(next, struct mlx5e_hairpin_entry, flows);

		netdev_dbg(priv->netdev, "del hairpin: peer %s\n",
			   hpe->hp->pair->peer_mdev->priv.name);

		mlx5e_hairpin_destroy(hpe->hp);
		hash_del(&hpe->hairpin_hlist);
		kfree(hpe);
	}
}

static struct mlx5_flow_handle *
mlx5e_tc_add_nic_flow(struct mlx5e_priv *priv,
		      struct mlx5e_tc_flow_parse_attr *parse_attr,
		      struct mlx5e_tc_flow *flow)
{
	struct mlx5_nic_flow_attr *attr = flow->nic_attr;
	struct mlx5_core_dev *dev = priv->mdev;
	struct mlx5_flow_destination dest[2] = {};
	struct mlx5_flow_act flow_act = {
		.action = attr->action,
		.has_flow_tag = true,
		.flow_tag = attr->flow_tag,
		.encap_id = 0,
	};
	struct mlx5_fc *counter = NULL;
	struct mlx5_flow_handle *rule;
	bool table_created = false;
	int err, dest_ix = 0;

	if (flow->flags & MLX5E_TC_FLOW_HAIRPIN) {
		err = mlx5e_hairpin_flow_add(priv, flow, parse_attr);
		if (err) {
			rule = ERR_PTR(err);
			goto err_add_hairpin_flow;
		}
		if (flow->flags & MLX5E_TC_FLOW_HAIRPIN_RSS) {
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
		if (IS_ERR(counter)) {
			rule = ERR_CAST(counter);
			goto err_fc_create;
		}
		dest[dest_ix].type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
		dest[dest_ix].counter = counter;
		dest_ix++;
	}

	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR) {
		err = mlx5e_attach_mod_hdr(priv, flow, parse_attr);
		flow_act.modify_id = attr->mod_hdr_id;
		kfree(parse_attr->mod_hdr_actions);
		if (err) {
			rule = ERR_PTR(err);
			goto err_create_mod_hdr_id;
		}
	}

	if (IS_ERR_OR_NULL(priv->fs.tc.t)) {
		int tc_grp_size, tc_tbl_size;
		u32 max_flow_counter;

		max_flow_counter = (MLX5_CAP_GEN(dev, max_flow_counter_31_16) << 16) |
				    MLX5_CAP_GEN(dev, max_flow_counter_15_0);

		tc_grp_size = min_t(int, max_flow_counter, MLX5E_TC_TABLE_MAX_GROUP_SIZE);

		tc_tbl_size = min_t(int, tc_grp_size * MLX5E_TC_TABLE_NUM_GROUPS,
				    BIT(MLX5_CAP_FLOWTABLE_NIC_RX(dev, log_max_ft_size)));

		priv->fs.tc.t =
			mlx5_create_auto_grouped_flow_table(priv->fs.ns,
							    MLX5E_TC_PRIO,
							    tc_tbl_size,
							    MLX5E_TC_TABLE_NUM_GROUPS,
							    MLX5E_TC_FT_LEVEL, 0);
		if (IS_ERR(priv->fs.tc.t)) {
			netdev_err(priv->netdev,
				   "Failed to create tc offload table\n");
			rule = ERR_CAST(priv->fs.tc.t);
			goto err_create_ft;
		}

		table_created = true;
	}

	if (attr->match_level != MLX5_MATCH_NONE)
		parse_attr->spec.match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;

	rule = mlx5_add_flow_rules(priv->fs.tc.t, &parse_attr->spec,
				   &flow_act, dest, dest_ix);

	if (IS_ERR(rule))
		goto err_add_rule;

	return rule;

err_add_rule:
	if (table_created) {
		mlx5_destroy_flow_table(priv->fs.tc.t);
		priv->fs.tc.t = NULL;
	}
err_create_ft:
	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR)
		mlx5e_detach_mod_hdr(priv, flow);
err_create_mod_hdr_id:
	mlx5_fc_destroy(dev, counter);
err_fc_create:
	if (flow->flags & MLX5E_TC_FLOW_HAIRPIN)
		mlx5e_hairpin_flow_del(priv, flow);
err_add_hairpin_flow:
	return rule;
}

static void mlx5e_tc_del_nic_flow(struct mlx5e_priv *priv,
				  struct mlx5e_tc_flow *flow)
{
	struct mlx5_nic_flow_attr *attr = flow->nic_attr;
	struct mlx5_fc *counter = NULL;

	counter = mlx5_flow_rule_counter(flow->rule[0]);
	mlx5_del_flow_rules(flow->rule[0]);
	mlx5_fc_destroy(priv->mdev, counter);

	if (!mlx5e_tc_num_filters(priv) && priv->fs.tc.t) {
		mlx5_destroy_flow_table(priv->fs.tc.t);
		priv->fs.tc.t = NULL;
	}

	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR)
		mlx5e_detach_mod_hdr(priv, flow);

	if (flow->flags & MLX5E_TC_FLOW_HAIRPIN)
		mlx5e_hairpin_flow_del(priv, flow);
}

static void mlx5e_detach_encap(struct mlx5e_priv *priv,
			       struct mlx5e_tc_flow *flow);

static int mlx5e_attach_encap(struct mlx5e_priv *priv,
			      struct ip_tunnel_info *tun_info,
			      struct net_device *mirred_dev,
			      struct net_device **encap_dev,
			      struct mlx5e_tc_flow *flow);

static struct mlx5_flow_handle *
mlx5e_tc_add_fdb_flow(struct mlx5e_priv *priv,
		      struct mlx5e_tc_flow_parse_attr *parse_attr,
		      struct mlx5e_tc_flow *flow)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_esw_flow_attr *attr = flow->esw_attr;
	struct net_device *out_dev, *encap_dev = NULL;
	struct mlx5_flow_handle *rule = NULL;
	struct mlx5e_rep_priv *rpriv;
	struct mlx5e_priv *out_priv;
	int err;

	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_ENCAP) {
		out_dev = __dev_get_by_index(dev_net(priv->netdev),
					     attr->parse_attr->mirred_ifindex);
		err = mlx5e_attach_encap(priv, &parse_attr->tun_info,
					 out_dev, &encap_dev, flow);
		if (err) {
			rule = ERR_PTR(err);
			if (err != -EAGAIN)
				goto err_attach_encap;
		}
		out_priv = netdev_priv(encap_dev);
		rpriv = out_priv->ppriv;
		attr->out_rep[attr->out_count] = rpriv->rep;
		attr->out_mdev[attr->out_count++] = out_priv->mdev;
	}

	err = mlx5_eswitch_add_vlan_action(esw, attr);
	if (err) {
		rule = ERR_PTR(err);
		goto err_add_vlan;
	}

	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR) {
		err = mlx5e_attach_mod_hdr(priv, flow, parse_attr);
		kfree(parse_attr->mod_hdr_actions);
		if (err) {
			rule = ERR_PTR(err);
			goto err_mod_hdr;
		}
	}

	/* we get here if (1) there's no error (rule being null) or when
	 * (2) there's an encap action and we're on -EAGAIN (no valid neigh)
	 */
	if (rule != ERR_PTR(-EAGAIN)) {
		rule = mlx5_eswitch_add_offloaded_rule(esw, &parse_attr->spec, attr);
		if (IS_ERR(rule))
			goto err_add_rule;

		if (attr->mirror_count) {
			flow->rule[1] = mlx5_eswitch_add_fwd_rule(esw, &parse_attr->spec, attr);
			if (IS_ERR(flow->rule[1]))
				goto err_fwd_rule;
		}
	}
	return rule;

err_fwd_rule:
	mlx5_eswitch_del_offloaded_rule(esw, rule, attr);
	rule = flow->rule[1];
err_add_rule:
	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR)
		mlx5e_detach_mod_hdr(priv, flow);
err_mod_hdr:
	mlx5_eswitch_del_vlan_action(esw, attr);
err_add_vlan:
	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_ENCAP)
		mlx5e_detach_encap(priv, flow);
err_attach_encap:
	return rule;
}

static void mlx5e_tc_del_fdb_flow(struct mlx5e_priv *priv,
				  struct mlx5e_tc_flow *flow)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_esw_flow_attr *attr = flow->esw_attr;

	if (flow->flags & MLX5E_TC_FLOW_OFFLOADED) {
		flow->flags &= ~MLX5E_TC_FLOW_OFFLOADED;
		if (attr->mirror_count)
			mlx5_eswitch_del_offloaded_rule(esw, flow->rule[1], attr);
		mlx5_eswitch_del_offloaded_rule(esw, flow->rule[0], attr);
	}

	mlx5_eswitch_del_vlan_action(esw, attr);

	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_ENCAP) {
		mlx5e_detach_encap(priv, flow);
		kvfree(attr->parse_attr);
	}

	if (attr->action & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR)
		mlx5e_detach_mod_hdr(priv, flow);
}

void mlx5e_tc_encap_flows_add(struct mlx5e_priv *priv,
			      struct mlx5e_encap_entry *e)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_esw_flow_attr *esw_attr;
	struct mlx5e_tc_flow *flow;
	int err;

	err = mlx5_encap_alloc(priv->mdev, e->tunnel_type,
			       e->encap_size, e->encap_header,
			       &e->encap_id);
	if (err) {
		mlx5_core_warn(priv->mdev, "Failed to offload cached encapsulation header, %d\n",
			       err);
		return;
	}
	e->flags |= MLX5_ENCAP_ENTRY_VALID;
	mlx5e_rep_queue_neigh_stats_work(priv);

	list_for_each_entry(flow, &e->flows, encap) {
		esw_attr = flow->esw_attr;
		esw_attr->encap_id = e->encap_id;
		flow->rule[0] = mlx5_eswitch_add_offloaded_rule(esw, &esw_attr->parse_attr->spec, esw_attr);
		if (IS_ERR(flow->rule[0])) {
			err = PTR_ERR(flow->rule[0]);
			mlx5_core_warn(priv->mdev, "Failed to update cached encapsulation flow, %d\n",
				       err);
			continue;
		}

		if (esw_attr->mirror_count) {
			flow->rule[1] = mlx5_eswitch_add_fwd_rule(esw, &esw_attr->parse_attr->spec, esw_attr);
			if (IS_ERR(flow->rule[1])) {
				mlx5_eswitch_del_offloaded_rule(esw, flow->rule[0], esw_attr);
				err = PTR_ERR(flow->rule[1]);
				mlx5_core_warn(priv->mdev, "Failed to update cached mirror flow, %d\n",
					       err);
				continue;
			}
		}

		flow->flags |= MLX5E_TC_FLOW_OFFLOADED;
	}
}

void mlx5e_tc_encap_flows_del(struct mlx5e_priv *priv,
			      struct mlx5e_encap_entry *e)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_tc_flow *flow;

	list_for_each_entry(flow, &e->flows, encap) {
		if (flow->flags & MLX5E_TC_FLOW_OFFLOADED) {
			struct mlx5_esw_flow_attr *attr = flow->esw_attr;

			flow->flags &= ~MLX5E_TC_FLOW_OFFLOADED;
			if (attr->mirror_count)
				mlx5_eswitch_del_offloaded_rule(esw, flow->rule[1], attr);
			mlx5_eswitch_del_offloaded_rule(esw, flow->rule[0], attr);
		}
	}

	if (e->flags & MLX5_ENCAP_ENTRY_VALID) {
		e->flags &= ~MLX5_ENCAP_ENTRY_VALID;
		mlx5_encap_dealloc(priv->mdev, e->encap_id);
	}
}

void mlx5e_tc_update_neigh_used_value(struct mlx5e_neigh_hash_entry *nhe)
{
	struct mlx5e_neigh *m_neigh = &nhe->m_neigh;
	u64 bytes, packets, lastuse = 0;
	struct mlx5e_tc_flow *flow;
	struct mlx5e_encap_entry *e;
	struct mlx5_fc *counter;
	struct neigh_table *tbl;
	bool neigh_used = false;
	struct neighbour *n;

	if (m_neigh->family == AF_INET)
		tbl = &arp_tbl;
#if IS_ENABLED(CONFIG_IPV6)
	else if (m_neigh->family == AF_INET6)
		tbl = &nd_tbl;
#endif
	else
		return;

	list_for_each_entry(e, &nhe->encap_list, encap_list) {
		if (!(e->flags & MLX5_ENCAP_ENTRY_VALID))
			continue;
		list_for_each_entry(flow, &e->flows, encap) {
			if (flow->flags & MLX5E_TC_FLOW_OFFLOADED) {
				counter = mlx5_flow_rule_counter(flow->rule[0]);
				mlx5_fc_query_cached(counter, &bytes, &packets, &lastuse);
				if (time_after((unsigned long)lastuse, nhe->reported_lastuse)) {
					neigh_used = true;
					break;
				}
			}
		}
		if (neigh_used)
			break;
	}

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

static void mlx5e_detach_encap(struct mlx5e_priv *priv,
			       struct mlx5e_tc_flow *flow)
{
	struct list_head *next = flow->encap.next;

	list_del(&flow->encap);
	if (list_empty(next)) {
		struct mlx5e_encap_entry *e;

		e = list_entry(next, struct mlx5e_encap_entry, flows);
		mlx5e_rep_encap_entry_detach(netdev_priv(e->out_dev), e);

		if (e->flags & MLX5_ENCAP_ENTRY_VALID)
			mlx5_encap_dealloc(priv->mdev, e->encap_id);

		hash_del_rcu(&e->encap_hlist);
		kfree(e->encap_header);
		kfree(e);
	}
}

static void mlx5e_tc_del_flow(struct mlx5e_priv *priv,
			      struct mlx5e_tc_flow *flow)
{
	if (flow->flags & MLX5E_TC_FLOW_ESWITCH)
		mlx5e_tc_del_fdb_flow(priv, flow);
	else
		mlx5e_tc_del_nic_flow(priv, flow);
}

static void parse_vxlan_attr(struct mlx5_flow_spec *spec,
			     struct tc_cls_flower_offload *f)
{
	void *headers_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				       outer_headers);
	void *headers_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				       outer_headers);
	void *misc_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				    misc_parameters);
	void *misc_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    misc_parameters);

	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, ip_protocol);
	MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_protocol, IPPROTO_UDP);

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct flow_dissector_key_keyid *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_KEYID,
						  f->key);
		struct flow_dissector_key_keyid *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_KEYID,
						  f->mask);
		MLX5_SET(fte_match_set_misc, misc_c, vxlan_vni,
			 be32_to_cpu(mask->keyid));
		MLX5_SET(fte_match_set_misc, misc_v, vxlan_vni,
			 be32_to_cpu(key->keyid));
	}
}

static int parse_tunnel_attr(struct mlx5e_priv *priv,
			     struct mlx5_flow_spec *spec,
			     struct tc_cls_flower_offload *f)
{
	void *headers_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				       outer_headers);
	void *headers_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				       outer_headers);

	struct flow_dissector_key_control *enc_control =
		skb_flow_dissector_target(f->dissector,
					  FLOW_DISSECTOR_KEY_ENC_CONTROL,
					  f->key);

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_ENC_PORTS)) {
		struct flow_dissector_key_ports *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_PORTS,
						  f->key);
		struct flow_dissector_key_ports *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_PORTS,
						  f->mask);

		/* Full udp dst port must be given */
		if (memchr_inv(&mask->dst, 0xff, sizeof(mask->dst)))
			goto vxlan_match_offload_err;

		if (mlx5_vxlan_lookup_port(priv->mdev->vxlan, be16_to_cpu(key->dst)) &&
		    MLX5_CAP_ESW(priv->mdev, vxlan_encap_decap))
			parse_vxlan_attr(spec, f);
		else {
			netdev_warn(priv->netdev,
				    "%d isn't an offloaded vxlan udp dport\n", be16_to_cpu(key->dst));
			return -EOPNOTSUPP;
		}

		MLX5_SET(fte_match_set_lyr_2_4, headers_c,
			 udp_dport, ntohs(mask->dst));
		MLX5_SET(fte_match_set_lyr_2_4, headers_v,
			 udp_dport, ntohs(key->dst));

		MLX5_SET(fte_match_set_lyr_2_4, headers_c,
			 udp_sport, ntohs(mask->src));
		MLX5_SET(fte_match_set_lyr_2_4, headers_v,
			 udp_sport, ntohs(key->src));
	} else { /* udp dst port must be given */
vxlan_match_offload_err:
		netdev_warn(priv->netdev,
			    "IP tunnel decap offload supported only for vxlan, must set UDP dport\n");
		return -EOPNOTSUPP;
	}

	if (enc_control->addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		struct flow_dissector_key_ipv4_addrs *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS,
						  f->key);
		struct flow_dissector_key_ipv4_addrs *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS,
						  f->mask);
		MLX5_SET(fte_match_set_lyr_2_4, headers_c,
			 src_ipv4_src_ipv6.ipv4_layout.ipv4,
			 ntohl(mask->src));
		MLX5_SET(fte_match_set_lyr_2_4, headers_v,
			 src_ipv4_src_ipv6.ipv4_layout.ipv4,
			 ntohl(key->src));

		MLX5_SET(fte_match_set_lyr_2_4, headers_c,
			 dst_ipv4_dst_ipv6.ipv4_layout.ipv4,
			 ntohl(mask->dst));
		MLX5_SET(fte_match_set_lyr_2_4, headers_v,
			 dst_ipv4_dst_ipv6.ipv4_layout.ipv4,
			 ntohl(key->dst));

		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, ethertype);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ethertype, ETH_P_IP);
	} else if (enc_control->addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
		struct flow_dissector_key_ipv6_addrs *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS,
						  f->key);
		struct flow_dissector_key_ipv6_addrs *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS,
						  f->mask);

		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       &mask->src, MLX5_FLD_SZ_BYTES(ipv6_layout, ipv6));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       &key->src, MLX5_FLD_SZ_BYTES(ipv6_layout, ipv6));

		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       &mask->dst, MLX5_FLD_SZ_BYTES(ipv6_layout, ipv6));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       &key->dst, MLX5_FLD_SZ_BYTES(ipv6_layout, ipv6));

		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, ethertype);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ethertype, ETH_P_IPV6);
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_ENC_IP)) {
		struct flow_dissector_key_ip *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_IP,
						  f->key);
		struct flow_dissector_key_ip *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_IP,
						  f->mask);

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ip_ecn, mask->tos & 0x3);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_ecn, key->tos & 0x3);

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ip_dscp, mask->tos >> 2);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_dscp, key->tos  >> 2);

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ttl_hoplimit, mask->ttl);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ttl_hoplimit, key->ttl);
	}

	/* Enforce DMAC when offloading incoming tunneled flows.
	 * Flow counters require a match on the DMAC.
	 */
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, dmac_47_16);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, dmac_15_0);
	ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				     dmac_47_16), priv->netdev->dev_addr);

	/* let software handle IP fragments */
	MLX5_SET(fte_match_set_lyr_2_4, headers_c, frag, 1);
	MLX5_SET(fte_match_set_lyr_2_4, headers_v, frag, 0);

	return 0;
}

static int __parse_cls_flower(struct mlx5e_priv *priv,
			      struct mlx5_flow_spec *spec,
			      struct tc_cls_flower_offload *f,
			      u8 *match_level)
{
	void *headers_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				       outer_headers);
	void *headers_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				       outer_headers);
	void *misc_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				    misc_parameters);
	void *misc_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    misc_parameters);
	u16 addr_type = 0;
	u8 ip_proto = 0;

	*match_level = MLX5_MATCH_NONE;

	if (f->dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_CONTROL) |
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
	      BIT(FLOW_DISSECTOR_KEY_ENC_IP))) {
		netdev_warn(priv->netdev, "Unsupported key used: 0x%x\n",
			    f->dissector->used_keys);
		return -EOPNOTSUPP;
	}

	if ((dissector_uses_key(f->dissector,
				FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS) ||
	     dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_ENC_KEYID) ||
	     dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_ENC_PORTS)) &&
	    dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_ENC_CONTROL)) {
		struct flow_dissector_key_control *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ENC_CONTROL,
						  f->key);
		switch (key->addr_type) {
		case FLOW_DISSECTOR_KEY_IPV4_ADDRS:
		case FLOW_DISSECTOR_KEY_IPV6_ADDRS:
			if (parse_tunnel_attr(priv, spec, f))
				return -EOPNOTSUPP;
			break;
		default:
			return -EOPNOTSUPP;
		}

		/* In decap flow, header pointers should point to the inner
		 * headers, outer header were already set by parse_tunnel_attr
		 */
		headers_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
					 inner_headers);
		headers_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
					 inner_headers);
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_dissector_key_eth_addrs *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ETH_ADDRS,
						  f->key);
		struct flow_dissector_key_eth_addrs *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_ETH_ADDRS,
						  f->mask);

		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
					     dmac_47_16),
				mask->dst);
		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
					     dmac_47_16),
				key->dst);

		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
					     smac_47_16),
				mask->src);
		ether_addr_copy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
					     smac_47_16),
				key->src);

		if (!is_zero_ether_addr(mask->src) || !is_zero_ether_addr(mask->dst))
			*match_level = MLX5_MATCH_L2;
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_dissector_key_vlan *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_VLAN,
						  f->key);
		struct flow_dissector_key_vlan *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_VLAN,
						  f->mask);
		if (mask->vlan_id || mask->vlan_priority || mask->vlan_tpid) {
			if (key->vlan_tpid == htons(ETH_P_8021AD)) {
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

			MLX5_SET(fte_match_set_lyr_2_4, headers_c, first_vid, mask->vlan_id);
			MLX5_SET(fte_match_set_lyr_2_4, headers_v, first_vid, key->vlan_id);

			MLX5_SET(fte_match_set_lyr_2_4, headers_c, first_prio, mask->vlan_priority);
			MLX5_SET(fte_match_set_lyr_2_4, headers_v, first_prio, key->vlan_priority);

			*match_level = MLX5_MATCH_L2;
		}
	} else {
		MLX5_SET(fte_match_set_lyr_2_4, headers_c, svlan_tag, 1);
		MLX5_SET(fte_match_set_lyr_2_4, headers_c, cvlan_tag, 1);
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_CVLAN)) {
		struct flow_dissector_key_vlan *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_CVLAN,
						  f->key);
		struct flow_dissector_key_vlan *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_CVLAN,
						  f->mask);
		if (mask->vlan_id || mask->vlan_priority || mask->vlan_tpid) {
			if (key->vlan_tpid == htons(ETH_P_8021AD)) {
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
				 mask->vlan_id);
			MLX5_SET(fte_match_set_misc, misc_v, outer_second_vid,
				 key->vlan_id);
			MLX5_SET(fte_match_set_misc, misc_c, outer_second_prio,
				 mask->vlan_priority);
			MLX5_SET(fte_match_set_misc, misc_v, outer_second_prio,
				 key->vlan_priority);

			*match_level = MLX5_MATCH_L2;
		}
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_dissector_key_basic *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  f->key);
		struct flow_dissector_key_basic *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  f->mask);
		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ethertype,
			 ntohs(mask->n_proto));
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ethertype,
			 ntohs(key->n_proto));

		if (mask->n_proto)
			*match_level = MLX5_MATCH_L2;
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_dissector_key_control *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_CONTROL,
						  f->key);

		struct flow_dissector_key_control *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_CONTROL,
						  f->mask);
		addr_type = key->addr_type;

		/* the HW doesn't support frag first/later */
		if (mask->flags & FLOW_DIS_FIRST_FRAG)
			return -EOPNOTSUPP;

		if (mask->flags & FLOW_DIS_IS_FRAGMENT) {
			MLX5_SET(fte_match_set_lyr_2_4, headers_c, frag, 1);
			MLX5_SET(fte_match_set_lyr_2_4, headers_v, frag,
				 key->flags & FLOW_DIS_IS_FRAGMENT);

			/* the HW doesn't need L3 inline to match on frag=no */
			if (!(key->flags & FLOW_DIS_IS_FRAGMENT))
				*match_level = MLX5_INLINE_MODE_L2;
	/* ***  L2 attributes parsing up to here *** */
			else
				*match_level = MLX5_INLINE_MODE_IP;
		}
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_dissector_key_basic *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  f->key);
		struct flow_dissector_key_basic *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  f->mask);
		ip_proto = key->ip_proto;

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ip_protocol,
			 mask->ip_proto);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_protocol,
			 key->ip_proto);

		if (mask->ip_proto)
			*match_level = MLX5_MATCH_L3;
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		struct flow_dissector_key_ipv4_addrs *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_IPV4_ADDRS,
						  f->key);
		struct flow_dissector_key_ipv4_addrs *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_IPV4_ADDRS,
						  f->mask);

		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    src_ipv4_src_ipv6.ipv4_layout.ipv4),
		       &mask->src, sizeof(mask->src));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    src_ipv4_src_ipv6.ipv4_layout.ipv4),
		       &key->src, sizeof(key->src));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
		       &mask->dst, sizeof(mask->dst));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
		       &key->dst, sizeof(key->dst));

		if (mask->src || mask->dst)
			*match_level = MLX5_MATCH_L3;
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
		struct flow_dissector_key_ipv6_addrs *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_IPV6_ADDRS,
						  f->key);
		struct flow_dissector_key_ipv6_addrs *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_IPV6_ADDRS,
						  f->mask);

		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       &mask->src, sizeof(mask->src));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       &key->src, sizeof(key->src));

		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_c,
				    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       &mask->dst, sizeof(mask->dst));
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
				    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       &key->dst, sizeof(key->dst));

		if (ipv6_addr_type(&mask->src) != IPV6_ADDR_ANY ||
		    ipv6_addr_type(&mask->dst) != IPV6_ADDR_ANY)
			*match_level = MLX5_MATCH_L3;
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_IP)) {
		struct flow_dissector_key_ip *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_IP,
						  f->key);
		struct flow_dissector_key_ip *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_IP,
						  f->mask);

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ip_ecn, mask->tos & 0x3);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_ecn, key->tos & 0x3);

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ip_dscp, mask->tos >> 2);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_dscp, key->tos  >> 2);

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, ttl_hoplimit, mask->ttl);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ttl_hoplimit, key->ttl);

		if (mask->ttl &&
		    !MLX5_CAP_ESW_FLOWTABLE_FDB(priv->mdev,
						ft_field_support.outer_ipv4_ttl))
			return -EOPNOTSUPP;

		if (mask->tos || mask->ttl)
			*match_level = MLX5_MATCH_L3;
	}

	/* ***  L3 attributes parsing up to here *** */

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_dissector_key_ports *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_PORTS,
						  f->key);
		struct flow_dissector_key_ports *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_PORTS,
						  f->mask);
		switch (ip_proto) {
		case IPPROTO_TCP:
			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 tcp_sport, ntohs(mask->src));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 tcp_sport, ntohs(key->src));

			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 tcp_dport, ntohs(mask->dst));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 tcp_dport, ntohs(key->dst));
			break;

		case IPPROTO_UDP:
			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 udp_sport, ntohs(mask->src));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 udp_sport, ntohs(key->src));

			MLX5_SET(fte_match_set_lyr_2_4, headers_c,
				 udp_dport, ntohs(mask->dst));
			MLX5_SET(fte_match_set_lyr_2_4, headers_v,
				 udp_dport, ntohs(key->dst));
			break;
		default:
			netdev_err(priv->netdev,
				   "Only UDP and TCP transport are supported\n");
			return -EINVAL;
		}

		if (mask->src || mask->dst)
			*match_level = MLX5_MATCH_L4;
	}

	if (dissector_uses_key(f->dissector, FLOW_DISSECTOR_KEY_TCP)) {
		struct flow_dissector_key_tcp *key =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_TCP,
						  f->key);
		struct flow_dissector_key_tcp *mask =
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_TCP,
						  f->mask);

		MLX5_SET(fte_match_set_lyr_2_4, headers_c, tcp_flags,
			 ntohs(mask->flags));
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, tcp_flags,
			 ntohs(key->flags));

		if (mask->flags)
			*match_level = MLX5_MATCH_L4;
	}

	return 0;
}

static int parse_cls_flower(struct mlx5e_priv *priv,
			    struct mlx5e_tc_flow *flow,
			    struct mlx5_flow_spec *spec,
			    struct tc_cls_flower_offload *f)
{
	struct mlx5_core_dev *dev = priv->mdev;
	struct mlx5_eswitch *esw = dev->priv.eswitch;
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep;
	u8 match_level;
	int err;

	err = __parse_cls_flower(priv, spec, f, &match_level);

	if (!err && (flow->flags & MLX5E_TC_FLOW_ESWITCH)) {
		rep = rpriv->rep;
		if (rep->vport != FDB_UPLINK_VPORT &&
		    (esw->offloads.inline_mode != MLX5_INLINE_MODE_NONE &&
		    esw->offloads.inline_mode < match_level)) {
			netdev_warn(priv->netdev,
				    "Flow is not offloaded due to min inline setting, required %d actual %d\n",
				    match_level, esw->offloads.inline_mode);
			return -EOPNOTSUPP;
		}
	}

	if (flow->flags & MLX5E_TC_FLOW_ESWITCH)
		flow->esw_attr->match_level = match_level;
	else
		flow->nic_attr->match_level = match_level;

	return err;
}

struct pedit_headers {
	struct ethhdr  eth;
	struct iphdr   ip4;
	struct ipv6hdr ip6;
	struct tcphdr  tcp;
	struct udphdr  udp;
};

static int pedit_header_offsets[] = {
	[TCA_PEDIT_KEY_EX_HDR_TYPE_ETH] = offsetof(struct pedit_headers, eth),
	[TCA_PEDIT_KEY_EX_HDR_TYPE_IP4] = offsetof(struct pedit_headers, ip4),
	[TCA_PEDIT_KEY_EX_HDR_TYPE_IP6] = offsetof(struct pedit_headers, ip6),
	[TCA_PEDIT_KEY_EX_HDR_TYPE_TCP] = offsetof(struct pedit_headers, tcp),
	[TCA_PEDIT_KEY_EX_HDR_TYPE_UDP] = offsetof(struct pedit_headers, udp),
};

#define pedit_header(_ph, _htype) ((void *)(_ph) + pedit_header_offsets[_htype])

static int set_pedit_val(u8 hdr_type, u32 mask, u32 val, u32 offset,
			 struct pedit_headers *masks,
			 struct pedit_headers *vals)
{
	u32 *curr_pmask, *curr_pval;

	if (hdr_type >= __PEDIT_HDR_TYPE_MAX)
		goto out_err;

	curr_pmask = (u32 *)(pedit_header(masks, hdr_type) + offset);
	curr_pval  = (u32 *)(pedit_header(vals, hdr_type) + offset);

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
	u8  size;
	u32 offset;
};

#define OFFLOAD(fw_field, size, field, off) \
		{MLX5_ACTION_IN_FIELD_OUT_ ## fw_field, size, offsetof(struct pedit_headers, field) + (off)}

static struct mlx5_fields fields[] = {
	OFFLOAD(DMAC_47_16, 4, eth.h_dest[0], 0),
	OFFLOAD(DMAC_15_0,  2, eth.h_dest[4], 0),
	OFFLOAD(SMAC_47_16, 4, eth.h_source[0], 0),
	OFFLOAD(SMAC_15_0,  2, eth.h_source[4], 0),
	OFFLOAD(ETHERTYPE,  2, eth.h_proto, 0),

	OFFLOAD(IP_TTL, 1, ip4.ttl,   0),
	OFFLOAD(SIPV4,  4, ip4.saddr, 0),
	OFFLOAD(DIPV4,  4, ip4.daddr, 0),

	OFFLOAD(SIPV6_127_96, 4, ip6.saddr.s6_addr32[0], 0),
	OFFLOAD(SIPV6_95_64,  4, ip6.saddr.s6_addr32[1], 0),
	OFFLOAD(SIPV6_63_32,  4, ip6.saddr.s6_addr32[2], 0),
	OFFLOAD(SIPV6_31_0,   4, ip6.saddr.s6_addr32[3], 0),
	OFFLOAD(DIPV6_127_96, 4, ip6.daddr.s6_addr32[0], 0),
	OFFLOAD(DIPV6_95_64,  4, ip6.daddr.s6_addr32[1], 0),
	OFFLOAD(DIPV6_63_32,  4, ip6.daddr.s6_addr32[2], 0),
	OFFLOAD(DIPV6_31_0,   4, ip6.daddr.s6_addr32[3], 0),
	OFFLOAD(IPV6_HOPLIMIT, 1, ip6.hop_limit, 0),

	OFFLOAD(TCP_SPORT, 2, tcp.source,  0),
	OFFLOAD(TCP_DPORT, 2, tcp.dest,    0),
	OFFLOAD(TCP_FLAGS, 1, tcp.ack_seq, 5),

	OFFLOAD(UDP_SPORT, 2, udp.source, 0),
	OFFLOAD(UDP_DPORT, 2, udp.dest,   0),
};

/* On input attr->num_mod_hdr_actions tells how many HW actions can be parsed at
 * max from the SW pedit action. On success, it says how many HW actions were
 * actually parsed.
 */
static int offload_pedit_fields(struct pedit_headers *masks,
				struct pedit_headers *vals,
				struct mlx5e_tc_flow_parse_attr *parse_attr)
{
	struct pedit_headers *set_masks, *add_masks, *set_vals, *add_vals;
	int i, action_size, nactions, max_actions, first, last, next_z;
	void *s_masks_p, *a_masks_p, *vals_p;
	struct mlx5_fields *f;
	u8 cmd, field_bsize;
	u32 s_mask, a_mask;
	unsigned long mask;
	__be32 mask_be32;
	__be16 mask_be16;
	void *action;

	set_masks = &masks[TCA_PEDIT_KEY_EX_CMD_SET];
	add_masks = &masks[TCA_PEDIT_KEY_EX_CMD_ADD];
	set_vals = &vals[TCA_PEDIT_KEY_EX_CMD_SET];
	add_vals = &vals[TCA_PEDIT_KEY_EX_CMD_ADD];

	action_size = MLX5_UN_SZ_BYTES(set_action_in_add_action_in_auto);
	action = parse_attr->mod_hdr_actions;
	max_actions = parse_attr->num_mod_hdr_actions;
	nactions = 0;

	for (i = 0; i < ARRAY_SIZE(fields); i++) {
		f = &fields[i];
		/* avoid seeing bits set from previous iterations */
		s_mask = 0;
		a_mask = 0;

		s_masks_p = (void *)set_masks + f->offset;
		a_masks_p = (void *)add_masks + f->offset;

		memcpy(&s_mask, s_masks_p, f->size);
		memcpy(&a_mask, a_masks_p, f->size);

		if (!s_mask && !a_mask) /* nothing to offload here */
			continue;

		if (s_mask && a_mask) {
			printk(KERN_WARNING "mlx5: can't set and add to the same HW field (%x)\n", f->field);
			return -EOPNOTSUPP;
		}

		if (nactions == max_actions) {
			printk(KERN_WARNING "mlx5: parsed %d pedit actions, can't do more\n", nactions);
			return -EOPNOTSUPP;
		}

		if (s_mask) {
			cmd  = MLX5_ACTION_TYPE_SET;
			mask = s_mask;
			vals_p = (void *)set_vals + f->offset;
			/* clear to denote we consumed this field */
			memset(s_masks_p, 0, f->size);
		} else {
			cmd  = MLX5_ACTION_TYPE_ADD;
			mask = a_mask;
			vals_p = (void *)add_vals + f->offset;
			/* clear to denote we consumed this field */
			memset(a_masks_p, 0, f->size);
		}

		field_bsize = f->size * BITS_PER_BYTE;

		if (field_bsize == 32) {
			mask_be32 = *(__be32 *)&mask;
			mask = (__force unsigned long)cpu_to_le32(be32_to_cpu(mask_be32));
		} else if (field_bsize == 16) {
			mask_be16 = *(__be16 *)&mask;
			mask = (__force unsigned long)cpu_to_le16(be16_to_cpu(mask_be16));
		}

		first = find_first_bit(&mask, field_bsize);
		next_z = find_next_zero_bit(&mask, field_bsize, first);
		last  = find_last_bit(&mask, field_bsize);
		if (first < next_z && next_z < last) {
			printk(KERN_WARNING "mlx5: rewrite of few sub-fields (mask %lx) isn't offloaded\n",
			       mask);
			return -EOPNOTSUPP;
		}

		MLX5_SET(set_action_in, action, action_type, cmd);
		MLX5_SET(set_action_in, action, field, f->field);

		if (cmd == MLX5_ACTION_TYPE_SET) {
			MLX5_SET(set_action_in, action, offset, first);
			/* length is num of bits to be written, zero means length of 32 */
			MLX5_SET(set_action_in, action, length, (last - first + 1));
		}

		if (field_bsize == 32)
			MLX5_SET(set_action_in, action, data, ntohl(*(__be32 *)vals_p) >> first);
		else if (field_bsize == 16)
			MLX5_SET(set_action_in, action, data, ntohs(*(__be16 *)vals_p) >> first);
		else if (field_bsize == 8)
			MLX5_SET(set_action_in, action, data, *(u8 *)vals_p >> first);

		action += action_size;
		nactions++;
	}

	parse_attr->num_mod_hdr_actions = nactions;
	return 0;
}

static int alloc_mod_hdr_actions(struct mlx5e_priv *priv,
				 const struct tc_action *a, int namespace,
				 struct mlx5e_tc_flow_parse_attr *parse_attr)
{
	int nkeys, action_size, max_actions;

	nkeys = tcf_pedit_nkeys(a);
	action_size = MLX5_UN_SZ_BYTES(set_action_in_add_action_in_auto);

	if (namespace == MLX5_FLOW_NAMESPACE_FDB) /* FDB offloading */
		max_actions = MLX5_CAP_ESW_FLOWTABLE_FDB(priv->mdev, max_modify_header_actions);
	else /* namespace is MLX5_FLOW_NAMESPACE_KERNEL - NIC offloading */
		max_actions = MLX5_CAP_FLOWTABLE_NIC_RX(priv->mdev, max_modify_header_actions);

	/* can get up to crazingly 16 HW actions in 32 bits pedit SW key */
	max_actions = min(max_actions, nkeys * 16);

	parse_attr->mod_hdr_actions = kcalloc(max_actions, action_size, GFP_KERNEL);
	if (!parse_attr->mod_hdr_actions)
		return -ENOMEM;

	parse_attr->num_mod_hdr_actions = max_actions;
	return 0;
}

static const struct pedit_headers zero_masks = {};

static int parse_tc_pedit_action(struct mlx5e_priv *priv,
				 const struct tc_action *a, int namespace,
				 struct mlx5e_tc_flow_parse_attr *parse_attr)
{
	struct pedit_headers masks[__PEDIT_CMD_MAX], vals[__PEDIT_CMD_MAX], *cmd_masks;
	int nkeys, i, err = -EOPNOTSUPP;
	u32 mask, val, offset;
	u8 cmd, htype;

	nkeys = tcf_pedit_nkeys(a);

	memset(masks, 0, sizeof(struct pedit_headers) * __PEDIT_CMD_MAX);
	memset(vals,  0, sizeof(struct pedit_headers) * __PEDIT_CMD_MAX);

	for (i = 0; i < nkeys; i++) {
		htype = tcf_pedit_htype(a, i);
		cmd = tcf_pedit_cmd(a, i);
		err = -EOPNOTSUPP; /* can't be all optimistic */

		if (htype == TCA_PEDIT_KEY_EX_HDR_TYPE_NETWORK) {
			netdev_warn(priv->netdev, "legacy pedit isn't offloaded\n");
			goto out_err;
		}

		if (cmd != TCA_PEDIT_KEY_EX_CMD_SET && cmd != TCA_PEDIT_KEY_EX_CMD_ADD) {
			netdev_warn(priv->netdev, "pedit cmd %d isn't offloaded\n", cmd);
			goto out_err;
		}

		mask = tcf_pedit_mask(a, i);
		val = tcf_pedit_val(a, i);
		offset = tcf_pedit_offset(a, i);

		err = set_pedit_val(htype, ~mask, val, offset, &masks[cmd], &vals[cmd]);
		if (err)
			goto out_err;
	}

	err = alloc_mod_hdr_actions(priv, a, namespace, parse_attr);
	if (err)
		goto out_err;

	err = offload_pedit_fields(masks, vals, parse_attr);
	if (err < 0)
		goto out_dealloc_parsed_actions;

	for (cmd = 0; cmd < __PEDIT_CMD_MAX; cmd++) {
		cmd_masks = &masks[cmd];
		if (memcmp(cmd_masks, &zero_masks, sizeof(zero_masks))) {
			netdev_warn(priv->netdev, "attempt to offload an unsupported field (cmd %d)\n", cmd);
			print_hex_dump(KERN_WARNING, "mask: ", DUMP_PREFIX_ADDRESS,
				       16, 1, cmd_masks, sizeof(zero_masks), true);
			err = -EOPNOTSUPP;
			goto out_dealloc_parsed_actions;
		}
	}

	return 0;

out_dealloc_parsed_actions:
	kfree(parse_attr->mod_hdr_actions);
out_err:
	return err;
}

static bool csum_offload_supported(struct mlx5e_priv *priv, u32 action, u32 update_flags)
{
	u32 prot_flags = TCA_CSUM_UPDATE_FLAG_IPV4HDR | TCA_CSUM_UPDATE_FLAG_TCP |
			 TCA_CSUM_UPDATE_FLAG_UDP;

	/*  The HW recalcs checksums only if re-writing headers */
	if (!(action & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR)) {
		netdev_warn(priv->netdev,
			    "TC csum action is only offloaded with pedit\n");
		return false;
	}

	if (update_flags & ~prot_flags) {
		netdev_warn(priv->netdev,
			    "can't offload TC csum action for some header/s - flags %#x\n",
			    update_flags);
		return false;
	}

	return true;
}

static bool modify_header_match_supported(struct mlx5_flow_spec *spec,
					  struct tcf_exts *exts)
{
	const struct tc_action *a;
	bool modify_ip_header;
	LIST_HEAD(actions);
	u8 htype, ip_proto;
	void *headers_v;
	u16 ethertype;
	int nkeys, i;

	headers_v = MLX5_ADDR_OF(fte_match_param, spec->match_value, outer_headers);
	ethertype = MLX5_GET(fte_match_set_lyr_2_4, headers_v, ethertype);

	/* for non-IP we only re-write MACs, so we're okay */
	if (ethertype != ETH_P_IP && ethertype != ETH_P_IPV6)
		goto out_ok;

	modify_ip_header = false;
	tcf_exts_for_each_action(i, a, exts) {
		int k;

		if (!is_tcf_pedit(a))
			continue;

		nkeys = tcf_pedit_nkeys(a);
		for (k = 0; k < nkeys; k++) {
			htype = tcf_pedit_htype(a, k);
			if (htype == TCA_PEDIT_KEY_EX_HDR_TYPE_IP4 ||
			    htype == TCA_PEDIT_KEY_EX_HDR_TYPE_IP6) {
				modify_ip_header = true;
				break;
			}
		}
	}

	ip_proto = MLX5_GET(fte_match_set_lyr_2_4, headers_v, ip_protocol);
	if (modify_ip_header && ip_proto != IPPROTO_TCP &&
	    ip_proto != IPPROTO_UDP && ip_proto != IPPROTO_ICMP) {
		pr_info("can't offload re-write of ip proto %d\n", ip_proto);
		return false;
	}

out_ok:
	return true;
}

static bool actions_match_supported(struct mlx5e_priv *priv,
				    struct tcf_exts *exts,
				    struct mlx5e_tc_flow_parse_attr *parse_attr,
				    struct mlx5e_tc_flow *flow)
{
	u32 actions;

	if (flow->flags & MLX5E_TC_FLOW_ESWITCH)
		actions = flow->esw_attr->action;
	else
		actions = flow->nic_attr->action;

	if (flow->flags & MLX5E_TC_FLOW_EGRESS &&
	    !(actions & MLX5_FLOW_CONTEXT_ACTION_DECAP))
		return false;

	if (actions & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR)
		return modify_header_match_supported(&parse_attr->spec, exts);

	return true;
}

static bool same_hw_devs(struct mlx5e_priv *priv, struct mlx5e_priv *peer_priv)
{
	struct mlx5_core_dev *fmdev, *pmdev;
	u64 fsystem_guid, psystem_guid;

	fmdev = priv->mdev;
	pmdev = peer_priv->mdev;

	mlx5_query_nic_vport_system_image_guid(fmdev, &fsystem_guid);
	mlx5_query_nic_vport_system_image_guid(pmdev, &psystem_guid);

	return (fsystem_guid == psystem_guid);
}

static int parse_tc_nic_actions(struct mlx5e_priv *priv, struct tcf_exts *exts,
				struct mlx5e_tc_flow_parse_attr *parse_attr,
				struct mlx5e_tc_flow *flow)
{
	struct mlx5_nic_flow_attr *attr = flow->nic_attr;
	const struct tc_action *a;
	LIST_HEAD(actions);
	u32 action = 0;
	int err, i;

	if (!tcf_exts_has_actions(exts))
		return -EINVAL;

	attr->flow_tag = MLX5_FS_DEFAULT_FLOW_TAG;

	tcf_exts_for_each_action(i, a, exts) {
		if (is_tcf_gact_shot(a)) {
			action |= MLX5_FLOW_CONTEXT_ACTION_DROP;
			if (MLX5_CAP_FLOWTABLE(priv->mdev,
					       flow_table_properties_nic_receive.flow_counter))
				action |= MLX5_FLOW_CONTEXT_ACTION_COUNT;
			continue;
		}

		if (is_tcf_pedit(a)) {
			err = parse_tc_pedit_action(priv, a, MLX5_FLOW_NAMESPACE_KERNEL,
						    parse_attr);
			if (err)
				return err;

			action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR |
				  MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
			continue;
		}

		if (is_tcf_csum(a)) {
			if (csum_offload_supported(priv, action,
						   tcf_csum_update_flags(a)))
				continue;

			return -EOPNOTSUPP;
		}

		if (is_tcf_mirred_egress_redirect(a)) {
			struct net_device *peer_dev = tcf_mirred_dev(a);

			if (priv->netdev->netdev_ops == peer_dev->netdev_ops &&
			    same_hw_devs(priv, netdev_priv(peer_dev))) {
				parse_attr->mirred_ifindex = peer_dev->ifindex;
				flow->flags |= MLX5E_TC_FLOW_HAIRPIN;
				action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
					  MLX5_FLOW_CONTEXT_ACTION_COUNT;
			} else {
				netdev_warn(priv->netdev, "device %s not on same HW, can't offload\n",
					    peer_dev->name);
				return -EINVAL;
			}
			continue;
		}

		if (is_tcf_skbedit_mark(a)) {
			u32 mark = tcf_skbedit_mark(a);

			if (mark & ~MLX5E_TC_FLOW_ID_MASK) {
				netdev_warn(priv->netdev, "Bad flow mark - only 16 bit is supported: 0x%x\n",
					    mark);
				return -EINVAL;
			}

			attr->flow_tag = mark;
			action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
			continue;
		}

		return -EINVAL;
	}

	attr->action = action;
	if (!actions_match_supported(priv, exts, parse_attr, flow))
		return -EOPNOTSUPP;

	return 0;
}

static inline int cmp_encap_info(struct ip_tunnel_key *a,
				 struct ip_tunnel_key *b)
{
	return memcmp(a, b, sizeof(*a));
}

static inline int hash_encap_info(struct ip_tunnel_key *key)
{
	return jhash(key, sizeof(*key), 0);
}

static int mlx5e_route_lookup_ipv4(struct mlx5e_priv *priv,
				   struct net_device *mirred_dev,
				   struct net_device **out_dev,
				   struct flowi4 *fl4,
				   struct neighbour **out_n,
				   u8 *out_ttl)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_rep_priv *uplink_rpriv;
	struct rtable *rt;
	struct neighbour *n = NULL;

#if IS_ENABLED(CONFIG_INET)
	int ret;

	rt = ip_route_output_key(dev_net(mirred_dev), fl4);
	ret = PTR_ERR_OR_ZERO(rt);
	if (ret)
		return ret;
#else
	return -EOPNOTSUPP;
#endif
	uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	/* if the egress device isn't on the same HW e-switch, we use the uplink */
	if (!switchdev_port_same_parent_id(priv->netdev, rt->dst.dev))
		*out_dev = uplink_rpriv->netdev;
	else
		*out_dev = rt->dst.dev;

	if (!(*out_ttl))
		*out_ttl = ip4_dst_hoplimit(&rt->dst);
	n = dst_neigh_lookup(&rt->dst, &fl4->daddr);
	ip_rt_put(rt);
	if (!n)
		return -ENOMEM;

	*out_n = n;
	return 0;
}

static bool is_merged_eswitch_dev(struct mlx5e_priv *priv,
				  struct net_device *peer_netdev)
{
	struct mlx5e_priv *peer_priv;

	peer_priv = netdev_priv(peer_netdev);

	return (MLX5_CAP_ESW(priv->mdev, merged_eswitch) &&
		(priv->netdev->netdev_ops == peer_netdev->netdev_ops) &&
		same_hw_devs(priv, peer_priv) &&
		MLX5_VPORT_MANAGER(peer_priv->mdev) &&
		(peer_priv->mdev->priv.eswitch->mode == SRIOV_OFFLOADS));
}

static int mlx5e_route_lookup_ipv6(struct mlx5e_priv *priv,
				   struct net_device *mirred_dev,
				   struct net_device **out_dev,
				   struct flowi6 *fl6,
				   struct neighbour **out_n,
				   u8 *out_ttl)
{
	struct neighbour *n = NULL;
	struct dst_entry *dst;

#if IS_ENABLED(CONFIG_INET) && IS_ENABLED(CONFIG_IPV6)
	struct mlx5e_rep_priv *uplink_rpriv;
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	int ret;

	ret = ipv6_stub->ipv6_dst_lookup(dev_net(mirred_dev), NULL, &dst,
					 fl6);
	if (ret < 0)
		return ret;

	if (!(*out_ttl))
		*out_ttl = ip6_dst_hoplimit(dst);

	uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	/* if the egress device isn't on the same HW e-switch, we use the uplink */
	if (!switchdev_port_same_parent_id(priv->netdev, dst->dev))
		*out_dev = uplink_rpriv->netdev;
	else
		*out_dev = dst->dev;
#else
	return -EOPNOTSUPP;
#endif

	n = dst_neigh_lookup(dst, &fl6->daddr);
	dst_release(dst);
	if (!n)
		return -ENOMEM;

	*out_n = n;
	return 0;
}

static void gen_vxlan_header_ipv4(struct net_device *out_dev,
				  char buf[], int encap_size,
				  unsigned char h_dest[ETH_ALEN],
				  u8 tos, u8 ttl,
				  __be32 daddr,
				  __be32 saddr,
				  __be16 udp_dst_port,
				  __be32 vx_vni)
{
	struct ethhdr *eth = (struct ethhdr *)buf;
	struct iphdr  *ip = (struct iphdr *)((char *)eth + sizeof(struct ethhdr));
	struct udphdr *udp = (struct udphdr *)((char *)ip + sizeof(struct iphdr));
	struct vxlanhdr *vxh = (struct vxlanhdr *)((char *)udp + sizeof(struct udphdr));

	memset(buf, 0, encap_size);

	ether_addr_copy(eth->h_dest, h_dest);
	ether_addr_copy(eth->h_source, out_dev->dev_addr);
	eth->h_proto = htons(ETH_P_IP);

	ip->daddr = daddr;
	ip->saddr = saddr;

	ip->tos = tos;
	ip->ttl = ttl;
	ip->protocol = IPPROTO_UDP;
	ip->version = 0x4;
	ip->ihl = 0x5;

	udp->dest = udp_dst_port;
	vxh->vx_flags = VXLAN_HF_VNI;
	vxh->vx_vni = vxlan_vni_field(vx_vni);
}

static void gen_vxlan_header_ipv6(struct net_device *out_dev,
				  char buf[], int encap_size,
				  unsigned char h_dest[ETH_ALEN],
				  u8 tos, u8 ttl,
				  struct in6_addr *daddr,
				  struct in6_addr *saddr,
				  __be16 udp_dst_port,
				  __be32 vx_vni)
{
	struct ethhdr *eth = (struct ethhdr *)buf;
	struct ipv6hdr *ip6h = (struct ipv6hdr *)((char *)eth + sizeof(struct ethhdr));
	struct udphdr *udp = (struct udphdr *)((char *)ip6h + sizeof(struct ipv6hdr));
	struct vxlanhdr *vxh = (struct vxlanhdr *)((char *)udp + sizeof(struct udphdr));

	memset(buf, 0, encap_size);

	ether_addr_copy(eth->h_dest, h_dest);
	ether_addr_copy(eth->h_source, out_dev->dev_addr);
	eth->h_proto = htons(ETH_P_IPV6);

	ip6_flow_hdr(ip6h, tos, 0);
	/* the HW fills up ipv6 payload len */
	ip6h->nexthdr     = IPPROTO_UDP;
	ip6h->hop_limit   = ttl;
	ip6h->daddr	  = *daddr;
	ip6h->saddr	  = *saddr;

	udp->dest = udp_dst_port;
	vxh->vx_flags = VXLAN_HF_VNI;
	vxh->vx_vni = vxlan_vni_field(vx_vni);
}

static int mlx5e_create_encap_header_ipv4(struct mlx5e_priv *priv,
					  struct net_device *mirred_dev,
					  struct mlx5e_encap_entry *e)
{
	int max_encap_size = MLX5_CAP_ESW(priv->mdev, max_encap_header_size);
	int ipv4_encap_size = ETH_HLEN + sizeof(struct iphdr) + VXLAN_HLEN;
	struct ip_tunnel_key *tun_key = &e->tun_info.key;
	struct net_device *out_dev;
	struct neighbour *n = NULL;
	struct flowi4 fl4 = {};
	u8 nud_state, tos, ttl;
	char *encap_header;
	int err;

	if (max_encap_size < ipv4_encap_size) {
		mlx5_core_warn(priv->mdev, "encap size %d too big, max supported is %d\n",
			       ipv4_encap_size, max_encap_size);
		return -EOPNOTSUPP;
	}

	encap_header = kzalloc(ipv4_encap_size, GFP_KERNEL);
	if (!encap_header)
		return -ENOMEM;

	switch (e->tunnel_type) {
	case MLX5_HEADER_TYPE_VXLAN:
		fl4.flowi4_proto = IPPROTO_UDP;
		fl4.fl4_dport = tun_key->tp_dst;
		break;
	default:
		err = -EOPNOTSUPP;
		goto free_encap;
	}

	tos = tun_key->tos;
	ttl = tun_key->ttl;

	fl4.flowi4_tos = tun_key->tos;
	fl4.daddr = tun_key->u.ipv4.dst;
	fl4.saddr = tun_key->u.ipv4.src;

	err = mlx5e_route_lookup_ipv4(priv, mirred_dev, &out_dev,
				      &fl4, &n, &ttl);
	if (err)
		goto free_encap;

	/* used by mlx5e_detach_encap to lookup a neigh hash table
	 * entry in the neigh hash table when a user deletes a rule
	 */
	e->m_neigh.dev = n->dev;
	e->m_neigh.family = n->ops->family;
	memcpy(&e->m_neigh.dst_ip, n->primary_key, n->tbl->key_len);
	e->out_dev = out_dev;

	/* It's importent to add the neigh to the hash table before checking
	 * the neigh validity state. So if we'll get a notification, in case the
	 * neigh changes it's validity state, we would find the relevant neigh
	 * in the hash.
	 */
	err = mlx5e_rep_encap_entry_attach(netdev_priv(out_dev), e);
	if (err)
		goto free_encap;

	read_lock_bh(&n->lock);
	nud_state = n->nud_state;
	ether_addr_copy(e->h_dest, n->ha);
	read_unlock_bh(&n->lock);

	switch (e->tunnel_type) {
	case MLX5_HEADER_TYPE_VXLAN:
		gen_vxlan_header_ipv4(out_dev, encap_header,
				      ipv4_encap_size, e->h_dest, tos, ttl,
				      fl4.daddr,
				      fl4.saddr, tun_key->tp_dst,
				      tunnel_id_to_key32(tun_key->tun_id));
		break;
	default:
		err = -EOPNOTSUPP;
		goto destroy_neigh_entry;
	}
	e->encap_size = ipv4_encap_size;
	e->encap_header = encap_header;

	if (!(nud_state & NUD_VALID)) {
		neigh_event_send(n, NULL);
		err = -EAGAIN;
		goto out;
	}

	err = mlx5_encap_alloc(priv->mdev, e->tunnel_type,
			       ipv4_encap_size, encap_header, &e->encap_id);
	if (err)
		goto destroy_neigh_entry;

	e->flags |= MLX5_ENCAP_ENTRY_VALID;
	mlx5e_rep_queue_neigh_stats_work(netdev_priv(out_dev));
	neigh_release(n);
	return err;

destroy_neigh_entry:
	mlx5e_rep_encap_entry_detach(netdev_priv(e->out_dev), e);
free_encap:
	kfree(encap_header);
out:
	if (n)
		neigh_release(n);
	return err;
}

static int mlx5e_create_encap_header_ipv6(struct mlx5e_priv *priv,
					  struct net_device *mirred_dev,
					  struct mlx5e_encap_entry *e)
{
	int max_encap_size = MLX5_CAP_ESW(priv->mdev, max_encap_header_size);
	int ipv6_encap_size = ETH_HLEN + sizeof(struct ipv6hdr) + VXLAN_HLEN;
	struct ip_tunnel_key *tun_key = &e->tun_info.key;
	struct net_device *out_dev;
	struct neighbour *n = NULL;
	struct flowi6 fl6 = {};
	u8 nud_state, tos, ttl;
	char *encap_header;
	int err;

	if (max_encap_size < ipv6_encap_size) {
		mlx5_core_warn(priv->mdev, "encap size %d too big, max supported is %d\n",
			       ipv6_encap_size, max_encap_size);
		return -EOPNOTSUPP;
	}

	encap_header = kzalloc(ipv6_encap_size, GFP_KERNEL);
	if (!encap_header)
		return -ENOMEM;

	switch (e->tunnel_type) {
	case MLX5_HEADER_TYPE_VXLAN:
		fl6.flowi6_proto = IPPROTO_UDP;
		fl6.fl6_dport = tun_key->tp_dst;
		break;
	default:
		err = -EOPNOTSUPP;
		goto free_encap;
	}

	tos = tun_key->tos;
	ttl = tun_key->ttl;

	fl6.flowlabel = ip6_make_flowinfo(RT_TOS(tun_key->tos), tun_key->label);
	fl6.daddr = tun_key->u.ipv6.dst;
	fl6.saddr = tun_key->u.ipv6.src;

	err = mlx5e_route_lookup_ipv6(priv, mirred_dev, &out_dev,
				      &fl6, &n, &ttl);
	if (err)
		goto free_encap;

	/* used by mlx5e_detach_encap to lookup a neigh hash table
	 * entry in the neigh hash table when a user deletes a rule
	 */
	e->m_neigh.dev = n->dev;
	e->m_neigh.family = n->ops->family;
	memcpy(&e->m_neigh.dst_ip, n->primary_key, n->tbl->key_len);
	e->out_dev = out_dev;

	/* It's importent to add the neigh to the hash table before checking
	 * the neigh validity state. So if we'll get a notification, in case the
	 * neigh changes it's validity state, we would find the relevant neigh
	 * in the hash.
	 */
	err = mlx5e_rep_encap_entry_attach(netdev_priv(out_dev), e);
	if (err)
		goto free_encap;

	read_lock_bh(&n->lock);
	nud_state = n->nud_state;
	ether_addr_copy(e->h_dest, n->ha);
	read_unlock_bh(&n->lock);

	switch (e->tunnel_type) {
	case MLX5_HEADER_TYPE_VXLAN:
		gen_vxlan_header_ipv6(out_dev, encap_header,
				      ipv6_encap_size, e->h_dest, tos, ttl,
				      &fl6.daddr,
				      &fl6.saddr, tun_key->tp_dst,
				      tunnel_id_to_key32(tun_key->tun_id));
		break;
	default:
		err = -EOPNOTSUPP;
		goto destroy_neigh_entry;
	}

	e->encap_size = ipv6_encap_size;
	e->encap_header = encap_header;

	if (!(nud_state & NUD_VALID)) {
		neigh_event_send(n, NULL);
		err = -EAGAIN;
		goto out;
	}

	err = mlx5_encap_alloc(priv->mdev, e->tunnel_type,
			       ipv6_encap_size, encap_header, &e->encap_id);
	if (err)
		goto destroy_neigh_entry;

	e->flags |= MLX5_ENCAP_ENTRY_VALID;
	mlx5e_rep_queue_neigh_stats_work(netdev_priv(out_dev));
	neigh_release(n);
	return err;

destroy_neigh_entry:
	mlx5e_rep_encap_entry_detach(netdev_priv(e->out_dev), e);
free_encap:
	kfree(encap_header);
out:
	if (n)
		neigh_release(n);
	return err;
}

static int mlx5e_attach_encap(struct mlx5e_priv *priv,
			      struct ip_tunnel_info *tun_info,
			      struct net_device *mirred_dev,
			      struct net_device **encap_dev,
			      struct mlx5e_tc_flow *flow)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	unsigned short family = ip_tunnel_info_af(tun_info);
	struct mlx5_esw_flow_attr *attr = flow->esw_attr;
	struct ip_tunnel_key *key = &tun_info->key;
	struct mlx5e_encap_entry *e;
	int tunnel_type, err = 0;
	uintptr_t hash_key;
	bool found = false;

	/* udp dst port must be set */
	if (!memchr_inv(&key->tp_dst, 0, sizeof(key->tp_dst)))
		goto vxlan_encap_offload_err;

	/* setting udp src port isn't supported */
	if (memchr_inv(&key->tp_src, 0, sizeof(key->tp_src))) {
vxlan_encap_offload_err:
		netdev_warn(priv->netdev,
			    "must set udp dst port and not set udp src port\n");
		return -EOPNOTSUPP;
	}

	if (mlx5_vxlan_lookup_port(priv->mdev->vxlan, be16_to_cpu(key->tp_dst)) &&
	    MLX5_CAP_ESW(priv->mdev, vxlan_encap_decap)) {
		tunnel_type = MLX5_HEADER_TYPE_VXLAN;
	} else {
		netdev_warn(priv->netdev,
			    "%d isn't an offloaded vxlan udp dport\n", be16_to_cpu(key->tp_dst));
		return -EOPNOTSUPP;
	}

	hash_key = hash_encap_info(key);

	hash_for_each_possible_rcu(esw->offloads.encap_tbl, e,
				   encap_hlist, hash_key) {
		if (!cmp_encap_info(&e->tun_info.key, key)) {
			found = true;
			break;
		}
	}

	/* must verify if encap is valid or not */
	if (found)
		goto attach_flow;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return -ENOMEM;

	e->tun_info = *tun_info;
	e->tunnel_type = tunnel_type;
	INIT_LIST_HEAD(&e->flows);

	if (family == AF_INET)
		err = mlx5e_create_encap_header_ipv4(priv, mirred_dev, e);
	else if (family == AF_INET6)
		err = mlx5e_create_encap_header_ipv6(priv, mirred_dev, e);

	if (err && err != -EAGAIN)
		goto out_err;

	hash_add_rcu(esw->offloads.encap_tbl, &e->encap_hlist, hash_key);

attach_flow:
	list_add(&flow->encap, &e->flows);
	*encap_dev = e->out_dev;
	if (e->flags & MLX5_ENCAP_ENTRY_VALID)
		attr->encap_id = e->encap_id;
	else
		err = -EAGAIN;

	return err;

out_err:
	kfree(e);
	return err;
}

static int parse_tc_vlan_action(struct mlx5e_priv *priv,
				const struct tc_action *a,
				struct mlx5_esw_flow_attr *attr,
				u32 *action)
{
	u8 vlan_idx = attr->total_vlan;

	if (vlan_idx >= MLX5_FS_VLAN_DEPTH)
		return -EOPNOTSUPP;

	if (tcf_vlan_action(a) == TCA_VLAN_ACT_POP) {
		if (vlan_idx) {
			if (!mlx5_eswitch_vlan_actions_supported(priv->mdev,
								 MLX5_FS_VLAN_DEPTH))
				return -EOPNOTSUPP;

			*action |= MLX5_FLOW_CONTEXT_ACTION_VLAN_POP_2;
		} else {
			*action |= MLX5_FLOW_CONTEXT_ACTION_VLAN_POP;
		}
	} else if (tcf_vlan_action(a) == TCA_VLAN_ACT_PUSH) {
		attr->vlan_vid[vlan_idx] = tcf_vlan_push_vid(a);
		attr->vlan_prio[vlan_idx] = tcf_vlan_push_prio(a);
		attr->vlan_proto[vlan_idx] = tcf_vlan_push_proto(a);
		if (!attr->vlan_proto[vlan_idx])
			attr->vlan_proto[vlan_idx] = htons(ETH_P_8021Q);

		if (vlan_idx) {
			if (!mlx5_eswitch_vlan_actions_supported(priv->mdev,
								 MLX5_FS_VLAN_DEPTH))
				return -EOPNOTSUPP;

			*action |= MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH_2;
		} else {
			if (!mlx5_eswitch_vlan_actions_supported(priv->mdev, 1) &&
			    (tcf_vlan_push_proto(a) != htons(ETH_P_8021Q) ||
			     tcf_vlan_push_prio(a)))
				return -EOPNOTSUPP;

			*action |= MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH;
		}
	} else { /* action is TCA_VLAN_ACT_MODIFY */
		return -EOPNOTSUPP;
	}

	attr->total_vlan = vlan_idx + 1;

	return 0;
}

static int parse_tc_fdb_actions(struct mlx5e_priv *priv, struct tcf_exts *exts,
				struct mlx5e_tc_flow_parse_attr *parse_attr,
				struct mlx5e_tc_flow *flow)
{
	struct mlx5_esw_flow_attr *attr = flow->esw_attr;
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct ip_tunnel_info *info = NULL;
	const struct tc_action *a;
	LIST_HEAD(actions);
	bool encap = false;
	u32 action = 0;
	int err, i;

	if (!tcf_exts_has_actions(exts))
		return -EINVAL;

	attr->in_rep = rpriv->rep;
	attr->in_mdev = priv->mdev;

	tcf_exts_for_each_action(i, a, exts) {
		if (is_tcf_gact_shot(a)) {
			action |= MLX5_FLOW_CONTEXT_ACTION_DROP |
				  MLX5_FLOW_CONTEXT_ACTION_COUNT;
			continue;
		}

		if (is_tcf_pedit(a)) {
			err = parse_tc_pedit_action(priv, a, MLX5_FLOW_NAMESPACE_FDB,
						    parse_attr);
			if (err)
				return err;

			action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
			attr->mirror_count = attr->out_count;
			continue;
		}

		if (is_tcf_csum(a)) {
			if (csum_offload_supported(priv, action,
						   tcf_csum_update_flags(a)))
				continue;

			return -EOPNOTSUPP;
		}

		if (is_tcf_mirred_egress_redirect(a) || is_tcf_mirred_egress_mirror(a)) {
			struct mlx5e_priv *out_priv;
			struct net_device *out_dev;

			out_dev = tcf_mirred_dev(a);

			if (attr->out_count >= MLX5_MAX_FLOW_FWD_VPORTS) {
				pr_err("can't support more than %d output ports, can't offload forwarding\n",
				       attr->out_count);
				return -EOPNOTSUPP;
			}

			if (switchdev_port_same_parent_id(priv->netdev,
							  out_dev) ||
			    is_merged_eswitch_dev(priv, out_dev)) {
				action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
					  MLX5_FLOW_CONTEXT_ACTION_COUNT;
				out_priv = netdev_priv(out_dev);
				rpriv = out_priv->ppriv;
				attr->out_rep[attr->out_count] = rpriv->rep;
				attr->out_mdev[attr->out_count++] = out_priv->mdev;
			} else if (encap) {
				parse_attr->mirred_ifindex = out_dev->ifindex;
				parse_attr->tun_info = *info;
				attr->parse_attr = parse_attr;
				action |= MLX5_FLOW_CONTEXT_ACTION_ENCAP |
					  MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
					  MLX5_FLOW_CONTEXT_ACTION_COUNT;
				/* attr->out_rep is resolved when we handle encap */
			} else {
				pr_err("devices %s %s not on same switch HW, can't offload forwarding\n",
				       priv->netdev->name, out_dev->name);
				return -EINVAL;
			}
			continue;
		}

		if (is_tcf_tunnel_set(a)) {
			info = tcf_tunnel_info(a);
			if (info)
				encap = true;
			else
				return -EOPNOTSUPP;
			attr->mirror_count = attr->out_count;
			continue;
		}

		if (is_tcf_vlan(a)) {
			err = parse_tc_vlan_action(priv, a, attr, &action);

			if (err)
				return err;

			attr->mirror_count = attr->out_count;
			continue;
		}

		if (is_tcf_tunnel_release(a)) {
			action |= MLX5_FLOW_CONTEXT_ACTION_DECAP;
			continue;
		}

		return -EINVAL;
	}

	attr->action = action;
	if (!actions_match_supported(priv, exts, parse_attr, flow))
		return -EOPNOTSUPP;

	if (attr->out_count > 1 && !mlx5_esw_has_fwd_fdb(priv->mdev)) {
		netdev_warn_once(priv->netdev, "current firmware doesn't support split rule for port mirroring\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

static void get_flags(int flags, u8 *flow_flags)
{
	u8 __flow_flags = 0;

	if (flags & MLX5E_TC_INGRESS)
		__flow_flags |= MLX5E_TC_FLOW_INGRESS;
	if (flags & MLX5E_TC_EGRESS)
		__flow_flags |= MLX5E_TC_FLOW_EGRESS;

	*flow_flags = __flow_flags;
}

static const struct rhashtable_params tc_ht_params = {
	.head_offset = offsetof(struct mlx5e_tc_flow, node),
	.key_offset = offsetof(struct mlx5e_tc_flow, cookie),
	.key_len = sizeof(((struct mlx5e_tc_flow *)0)->cookie),
	.automatic_shrinking = true,
};

static struct rhashtable *get_tc_ht(struct mlx5e_priv *priv)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_rep_priv *uplink_rpriv;

	if (MLX5_VPORT_MANAGER(priv->mdev) && esw->mode == SRIOV_OFFLOADS) {
		uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
		return &uplink_rpriv->tc_ht;
	} else
		return &priv->fs.tc.ht;
}

int mlx5e_configure_flower(struct mlx5e_priv *priv,
			   struct tc_cls_flower_offload *f, int flags)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_tc_flow_parse_attr *parse_attr;
	struct rhashtable *tc_ht = get_tc_ht(priv);
	struct mlx5e_tc_flow *flow;
	int attr_size, err = 0;
	u8 flow_flags = 0;

	get_flags(flags, &flow_flags);

	flow = rhashtable_lookup_fast(tc_ht, &f->cookie, tc_ht_params);
	if (flow) {
		netdev_warn_once(priv->netdev, "flow cookie %lx already exists, ignoring\n", f->cookie);
		return 0;
	}

	if (esw && esw->mode == SRIOV_OFFLOADS) {
		flow_flags |= MLX5E_TC_FLOW_ESWITCH;
		attr_size  = sizeof(struct mlx5_esw_flow_attr);
	} else {
		flow_flags |= MLX5E_TC_FLOW_NIC;
		attr_size  = sizeof(struct mlx5_nic_flow_attr);
	}

	flow = kzalloc(sizeof(*flow) + attr_size, GFP_KERNEL);
	parse_attr = kvzalloc(sizeof(*parse_attr), GFP_KERNEL);
	if (!parse_attr || !flow) {
		err = -ENOMEM;
		goto err_free;
	}

	flow->cookie = f->cookie;
	flow->flags = flow_flags;
	flow->priv = priv;

	err = parse_cls_flower(priv, flow, &parse_attr->spec, f);
	if (err < 0)
		goto err_free;

	if (flow->flags & MLX5E_TC_FLOW_ESWITCH) {
		err = parse_tc_fdb_actions(priv, f->exts, parse_attr, flow);
		if (err < 0)
			goto err_free;
		flow->rule[0] = mlx5e_tc_add_fdb_flow(priv, parse_attr, flow);
	} else {
		err = parse_tc_nic_actions(priv, f->exts, parse_attr, flow);
		if (err < 0)
			goto err_free;
		flow->rule[0] = mlx5e_tc_add_nic_flow(priv, parse_attr, flow);
	}

	if (IS_ERR(flow->rule[0])) {
		err = PTR_ERR(flow->rule[0]);
		if (err != -EAGAIN)
			goto err_free;
	}

	if (err != -EAGAIN)
		flow->flags |= MLX5E_TC_FLOW_OFFLOADED;

	if (!(flow->flags & MLX5E_TC_FLOW_ESWITCH) ||
	    !(flow->esw_attr->action & MLX5_FLOW_CONTEXT_ACTION_ENCAP))
		kvfree(parse_attr);

	err = rhashtable_insert_fast(tc_ht, &flow->node, tc_ht_params);
	if (err) {
		mlx5e_tc_del_flow(priv, flow);
		kfree(flow);
	}

	return err;

err_free:
	kvfree(parse_attr);
	kfree(flow);
	return err;
}

#define DIRECTION_MASK (MLX5E_TC_INGRESS | MLX5E_TC_EGRESS)
#define FLOW_DIRECTION_MASK (MLX5E_TC_FLOW_INGRESS | MLX5E_TC_FLOW_EGRESS)

static bool same_flow_direction(struct mlx5e_tc_flow *flow, int flags)
{
	if ((flow->flags & FLOW_DIRECTION_MASK) == (flags & DIRECTION_MASK))
		return true;

	return false;
}

int mlx5e_delete_flower(struct mlx5e_priv *priv,
			struct tc_cls_flower_offload *f, int flags)
{
	struct rhashtable *tc_ht = get_tc_ht(priv);
	struct mlx5e_tc_flow *flow;

	flow = rhashtable_lookup_fast(tc_ht, &f->cookie, tc_ht_params);
	if (!flow || !same_flow_direction(flow, flags))
		return -EINVAL;

	rhashtable_remove_fast(tc_ht, &flow->node, tc_ht_params);

	mlx5e_tc_del_flow(priv, flow);

	kfree(flow);

	return 0;
}

int mlx5e_stats_flower(struct mlx5e_priv *priv,
		       struct tc_cls_flower_offload *f, int flags)
{
	struct rhashtable *tc_ht = get_tc_ht(priv);
	struct mlx5e_tc_flow *flow;
	struct mlx5_fc *counter;
	u64 bytes;
	u64 packets;
	u64 lastuse;

	flow = rhashtable_lookup_fast(tc_ht, &f->cookie, tc_ht_params);
	if (!flow || !same_flow_direction(flow, flags))
		return -EINVAL;

	if (!(flow->flags & MLX5E_TC_FLOW_OFFLOADED))
		return 0;

	counter = mlx5_flow_rule_counter(flow->rule[0]);
	if (!counter)
		return 0;

	mlx5_fc_query_cached(counter, &bytes, &packets, &lastuse);

	tcf_exts_stats_update(f->exts, bytes, packets, lastuse);

	return 0;
}

static void mlx5e_tc_hairpin_update_dead_peer(struct mlx5e_priv *priv,
					      struct mlx5e_priv *peer_priv)
{
	struct mlx5_core_dev *peer_mdev = peer_priv->mdev;
	struct mlx5e_hairpin_entry *hpe;
	u16 peer_vhca_id;
	int bkt;

	if (!same_hw_devs(priv, peer_priv))
		return;

	peer_vhca_id = MLX5_CAP_GEN(peer_mdev, vhca_id);

	hash_for_each(priv->fs.tc.hairpin_tbl, bkt, hpe, hairpin_hlist) {
		if (hpe->peer_vhca_id == peer_vhca_id)
			hpe->hp->pair->peer_gone = true;
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

	hash_init(tc->mod_hdr_tbl);
	hash_init(tc->hairpin_tbl);

	err = rhashtable_init(&tc->ht, &tc_ht_params);
	if (err)
		return err;

	tc->netdevice_nb.notifier_call = mlx5e_tc_netdev_event;
	if (register_netdevice_notifier(&tc->netdevice_nb)) {
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
		unregister_netdevice_notifier(&tc->netdevice_nb);

	rhashtable_free_and_destroy(&tc->ht, _mlx5e_tc_del_flow, NULL);

	if (!IS_ERR_OR_NULL(tc->t)) {
		mlx5_destroy_flow_table(tc->t);
		tc->t = NULL;
	}
}

int mlx5e_tc_esw_init(struct rhashtable *tc_ht)
{
	return rhashtable_init(tc_ht, &tc_ht_params);
}

void mlx5e_tc_esw_cleanup(struct rhashtable *tc_ht)
{
	rhashtable_free_and_destroy(tc_ht, _mlx5e_tc_del_flow, NULL);
}

int mlx5e_tc_num_filters(struct mlx5e_priv *priv)
{
	struct rhashtable *tc_ht = get_tc_ht(priv);

	return atomic_read(&tc_ht->nelems);
}
