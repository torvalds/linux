// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/nf_conntrack_labels.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_acct.h>
#include <uapi/linux/tc_act/tc_pedit.h>
#include <net/tc_act/tc_ct.h>
#include <net/flow_offload.h>
#include <linux/workqueue.h>

#include "en/tc_ct.h"
#include "en.h"
#include "en_tc.h"
#include "en_rep.h"
#include "eswitch_offloads_chains.h"

#define MLX5_CT_ZONE_BITS (mlx5e_tc_attr_to_reg_mappings[ZONE_TO_REG].mlen * 8)
#define MLX5_CT_ZONE_MASK GENMASK(MLX5_CT_ZONE_BITS - 1, 0)
#define MLX5_CT_STATE_ESTABLISHED_BIT BIT(1)
#define MLX5_CT_STATE_TRK_BIT BIT(2)

#define MLX5_FTE_ID_BITS (mlx5e_tc_attr_to_reg_mappings[FTEID_TO_REG].mlen * 8)
#define MLX5_FTE_ID_MAX GENMASK(MLX5_FTE_ID_BITS - 1, 0)
#define MLX5_FTE_ID_MASK MLX5_FTE_ID_MAX

#define ct_dbg(fmt, args...)\
	netdev_dbg(ct_priv->netdev, "ct_debug: " fmt "\n", ##args)

struct mlx5_tc_ct_priv {
	struct mlx5_eswitch *esw;
	const struct net_device *netdev;
	struct idr fte_ids;
	struct mlx5_flow_table *ct;
	struct mlx5_flow_table *ct_nat;
	struct mlx5_flow_table *post_ct;
	struct mutex control_lock; /* guards parallel adds/dels */
};

struct mlx5_ct_flow {
	struct mlx5_esw_flow_attr pre_ct_attr;
	struct mlx5_esw_flow_attr post_ct_attr;
	struct mlx5_flow_handle *pre_ct_rule;
	struct mlx5_flow_handle *post_ct_rule;
	u32 fte_id;
	u32 chain_mapping;
};

static struct mlx5_tc_ct_priv *
mlx5_tc_ct_get_ct_priv(struct mlx5e_priv *priv)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_rep_uplink_priv *uplink_priv;
	struct mlx5e_rep_priv *uplink_rpriv;

	uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	uplink_priv = &uplink_rpriv->uplink_priv;
	return uplink_priv->ct_priv;
}

int
mlx5_tc_ct_parse_match(struct mlx5e_priv *priv,
		       struct mlx5_flow_spec *spec,
		       struct flow_cls_offload *f,
		       struct netlink_ext_ack *extack)
{
	struct mlx5_tc_ct_priv *ct_priv = mlx5_tc_ct_get_ct_priv(priv);
	struct flow_dissector_key_ct *mask, *key;
	bool trk, est, untrk, unest, new, unnew;
	u32 ctstate = 0, ctstate_mask = 0;
	u16 ct_state_on, ct_state_off;
	u16 ct_state, ct_state_mask;
	struct flow_match_ct match;

	if (!flow_rule_match_key(f->rule, FLOW_DISSECTOR_KEY_CT))
		return 0;

	if (!ct_priv) {
		NL_SET_ERR_MSG_MOD(extack,
				   "offload of ct matching isn't available");
		return -EOPNOTSUPP;
	}

	flow_rule_match_ct(f->rule, &match);

	key = match.key;
	mask = match.mask;

	ct_state = key->ct_state;
	ct_state_mask = mask->ct_state;

	if (ct_state_mask & ~(TCA_FLOWER_KEY_CT_FLAGS_TRACKED |
			      TCA_FLOWER_KEY_CT_FLAGS_ESTABLISHED |
			      TCA_FLOWER_KEY_CT_FLAGS_NEW)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "only ct_state trk, est and new are supported for offload");
		return -EOPNOTSUPP;
	}

	if (mask->ct_labels[1] || mask->ct_labels[2] || mask->ct_labels[3]) {
		NL_SET_ERR_MSG_MOD(extack,
				   "only lower 32bits of ct_labels are supported for offload");
		return -EOPNOTSUPP;
	}

	ct_state_on = ct_state & ct_state_mask;
	ct_state_off = (ct_state & ct_state_mask) ^ ct_state_mask;
	trk = ct_state_on & TCA_FLOWER_KEY_CT_FLAGS_TRACKED;
	new = ct_state_on & TCA_FLOWER_KEY_CT_FLAGS_NEW;
	est = ct_state_on & TCA_FLOWER_KEY_CT_FLAGS_ESTABLISHED;
	untrk = ct_state_off & TCA_FLOWER_KEY_CT_FLAGS_TRACKED;
	unnew = ct_state_off & TCA_FLOWER_KEY_CT_FLAGS_NEW;
	unest = ct_state_off & TCA_FLOWER_KEY_CT_FLAGS_ESTABLISHED;

	ctstate |= trk ? MLX5_CT_STATE_TRK_BIT : 0;
	ctstate |= est ? MLX5_CT_STATE_ESTABLISHED_BIT : 0;
	ctstate_mask |= (untrk || trk) ? MLX5_CT_STATE_TRK_BIT : 0;
	ctstate_mask |= (unest || est) ? MLX5_CT_STATE_ESTABLISHED_BIT : 0;

	if (new) {
		NL_SET_ERR_MSG_MOD(extack,
				   "matching on ct_state +new isn't supported");
		return -EOPNOTSUPP;
	}

	if (mask->ct_zone)
		mlx5e_tc_match_to_reg_match(spec, ZONE_TO_REG,
					    key->ct_zone, MLX5_CT_ZONE_MASK);
	if (ctstate_mask)
		mlx5e_tc_match_to_reg_match(spec, CTSTATE_TO_REG,
					    ctstate, ctstate_mask);
	if (mask->ct_mark)
		mlx5e_tc_match_to_reg_match(spec, MARK_TO_REG,
					    key->ct_mark, mask->ct_mark);
	if (mask->ct_labels[0])
		mlx5e_tc_match_to_reg_match(spec, LABELS_TO_REG,
					    key->ct_labels[0],
					    mask->ct_labels[0]);

	return 0;
}

int
mlx5_tc_ct_parse_action(struct mlx5e_priv *priv,
			struct mlx5_esw_flow_attr *attr,
			const struct flow_action_entry *act,
			struct netlink_ext_ack *extack)
{
	struct mlx5_tc_ct_priv *ct_priv = mlx5_tc_ct_get_ct_priv(priv);

	if (!ct_priv) {
		NL_SET_ERR_MSG_MOD(extack,
				   "offload of ct action isn't available");
		return -EOPNOTSUPP;
	}

	attr->ct_attr.zone = act->ct.zone;
	attr->ct_attr.ct_action = act->ct.action;

	return 0;
}

/* We translate the tc filter with CT action to the following HW model:
 *
 * +-------------------+      +--------------------+    +--------------+
 * + pre_ct (tc chain) +----->+ CT (nat or no nat) +--->+ post_ct      +----->
 * + original match    +  |   + tuple + zone match + |  + fte_id match +  |
 * +-------------------+  |   +--------------------+ |  +--------------+  |
 *                        v                          v                    v
 *                       set chain miss mapping  set mark             original
 *                       set fte_id              set label            filter
 *                       set zone                set established      actions
 *                       set tunnel_id           do nat (if needed)
 *                       do decap
 */
static int
__mlx5_tc_ct_flow_offload(struct mlx5e_priv *priv,
			  struct mlx5e_tc_flow *flow,
			  struct mlx5_flow_spec *orig_spec,
			  struct mlx5_esw_flow_attr *attr,
			  struct mlx5_flow_handle **flow_rule)
{
	struct mlx5_tc_ct_priv *ct_priv = mlx5_tc_ct_get_ct_priv(priv);
	bool nat = attr->ct_attr.ct_action & TCA_CT_ACT_NAT;
	struct mlx5e_tc_mod_hdr_acts pre_mod_acts = {};
	struct mlx5_eswitch *esw = ct_priv->esw;
	struct mlx5_flow_spec post_ct_spec = {};
	struct mlx5_esw_flow_attr *pre_ct_attr;
	struct  mlx5_modify_hdr *mod_hdr;
	struct mlx5_flow_handle *rule;
	struct mlx5_ct_flow *ct_flow;
	int chain_mapping = 0, err;
	u32 fte_id = 1;

	ct_flow = kzalloc(sizeof(*ct_flow), GFP_KERNEL);
	if (!ct_flow)
		return -ENOMEM;

	err = idr_alloc_u32(&ct_priv->fte_ids, ct_flow, &fte_id,
			    MLX5_FTE_ID_MAX, GFP_KERNEL);
	if (err) {
		netdev_warn(priv->netdev,
			    "Failed to allocate fte id, err: %d\n", err);
		goto err_idr;
	}
	ct_flow->fte_id = fte_id;

	/* Base esw attributes of both rules on original rule attribute */
	pre_ct_attr = &ct_flow->pre_ct_attr;
	memcpy(pre_ct_attr, attr, sizeof(*attr));
	memcpy(&ct_flow->post_ct_attr, attr, sizeof(*attr));

	/* Modify the original rule's action to fwd and modify, leave decap */
	pre_ct_attr->action = attr->action & MLX5_FLOW_CONTEXT_ACTION_DECAP;
	pre_ct_attr->action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
			       MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;

	/* Write chain miss tag for miss in ct table as we
	 * don't go though all prios of this chain as normal tc rules
	 * miss.
	 */
	err = mlx5_esw_chains_get_chain_mapping(esw, attr->chain,
						&chain_mapping);
	if (err) {
		ct_dbg("Failed to get chain register mapping for chain");
		goto err_get_chain;
	}
	ct_flow->chain_mapping = chain_mapping;

	err = mlx5e_tc_match_to_reg_set(esw->dev, &pre_mod_acts,
					CHAIN_TO_REG, chain_mapping);
	if (err) {
		ct_dbg("Failed to set chain register mapping");
		goto err_mapping;
	}

	err = mlx5e_tc_match_to_reg_set(esw->dev, &pre_mod_acts, ZONE_TO_REG,
					attr->ct_attr.zone &
					MLX5_CT_ZONE_MASK);
	if (err) {
		ct_dbg("Failed to set zone register mapping");
		goto err_mapping;
	}

	err = mlx5e_tc_match_to_reg_set(esw->dev, &pre_mod_acts,
					FTEID_TO_REG, fte_id);
	if (err) {
		ct_dbg("Failed to set fte_id register mapping");
		goto err_mapping;
	}

	/* If original flow is decap, we do it before going into ct table
	 * so add a rewrite for the tunnel match_id.
	 */
	if ((pre_ct_attr->action & MLX5_FLOW_CONTEXT_ACTION_DECAP) &&
	    attr->chain == 0) {
		u32 tun_id = mlx5e_tc_get_flow_tun_id(flow);

		err = mlx5e_tc_match_to_reg_set(esw->dev, &pre_mod_acts,
						TUNNEL_TO_REG,
						tun_id);
		if (err) {
			ct_dbg("Failed to set tunnel register mapping");
			goto err_mapping;
		}
	}

	mod_hdr = mlx5_modify_header_alloc(esw->dev,
					   MLX5_FLOW_NAMESPACE_FDB,
					   pre_mod_acts.num_actions,
					   pre_mod_acts.actions);
	if (IS_ERR(mod_hdr)) {
		err = PTR_ERR(mod_hdr);
		ct_dbg("Failed to create pre ct mod hdr");
		goto err_mapping;
	}
	pre_ct_attr->modify_hdr = mod_hdr;

	/* Post ct rule matches on fte_id and executes original rule's
	 * tc rule action
	 */
	mlx5e_tc_match_to_reg_match(&post_ct_spec, FTEID_TO_REG,
				    fte_id, MLX5_FTE_ID_MASK);

	/* Put post_ct rule on post_ct fdb */
	ct_flow->post_ct_attr.chain = 0;
	ct_flow->post_ct_attr.prio = 0;
	ct_flow->post_ct_attr.fdb = ct_priv->post_ct;

	ct_flow->post_ct_attr.inner_match_level = MLX5_MATCH_NONE;
	ct_flow->post_ct_attr.outer_match_level = MLX5_MATCH_NONE;
	ct_flow->post_ct_attr.action &= ~(MLX5_FLOW_CONTEXT_ACTION_DECAP);
	rule = mlx5_eswitch_add_offloaded_rule(esw, &post_ct_spec,
					       &ct_flow->post_ct_attr);
	ct_flow->post_ct_rule = rule;
	if (IS_ERR(ct_flow->post_ct_rule)) {
		err = PTR_ERR(ct_flow->post_ct_rule);
		ct_dbg("Failed to add post ct rule");
		goto err_insert_post_ct;
	}

	/* Change original rule point to ct table */
	pre_ct_attr->dest_chain = 0;
	pre_ct_attr->dest_ft = nat ? ct_priv->ct_nat : ct_priv->ct;
	ct_flow->pre_ct_rule = mlx5_eswitch_add_offloaded_rule(esw,
							       orig_spec,
							       pre_ct_attr);
	if (IS_ERR(ct_flow->pre_ct_rule)) {
		err = PTR_ERR(ct_flow->pre_ct_rule);
		ct_dbg("Failed to add pre ct rule");
		goto err_insert_orig;
	}

	attr->ct_attr.ct_flow = ct_flow;
	*flow_rule = ct_flow->post_ct_rule;
	dealloc_mod_hdr_actions(&pre_mod_acts);

	return 0;

err_insert_orig:
	mlx5_eswitch_del_offloaded_rule(ct_priv->esw, ct_flow->post_ct_rule,
					&ct_flow->post_ct_attr);
err_insert_post_ct:
	mlx5_modify_header_dealloc(priv->mdev, pre_ct_attr->modify_hdr);
err_mapping:
	dealloc_mod_hdr_actions(&pre_mod_acts);
	mlx5_esw_chains_put_chain_mapping(esw, ct_flow->chain_mapping);
err_get_chain:
	idr_remove(&ct_priv->fte_ids, fte_id);
err_idr:
	kfree(ct_flow);
	netdev_warn(priv->netdev, "Failed to offload ct flow, err %d\n", err);
	return err;
}

struct mlx5_flow_handle *
mlx5_tc_ct_flow_offload(struct mlx5e_priv *priv,
			struct mlx5e_tc_flow *flow,
			struct mlx5_flow_spec *spec,
			struct mlx5_esw_flow_attr *attr)
{
	struct mlx5_tc_ct_priv *ct_priv = mlx5_tc_ct_get_ct_priv(priv);
	struct mlx5_flow_handle *rule;
	int err;

	if (!ct_priv)
		return ERR_PTR(-EOPNOTSUPP);

	mutex_lock(&ct_priv->control_lock);
	err = __mlx5_tc_ct_flow_offload(priv, flow, spec, attr, &rule);
	mutex_unlock(&ct_priv->control_lock);
	if (err)
		return ERR_PTR(err);

	return rule;
}

static void
__mlx5_tc_ct_delete_flow(struct mlx5_tc_ct_priv *ct_priv,
			 struct mlx5_ct_flow *ct_flow)
{
	struct mlx5_esw_flow_attr *pre_ct_attr = &ct_flow->pre_ct_attr;
	struct mlx5_eswitch *esw = ct_priv->esw;

	mlx5_eswitch_del_offloaded_rule(esw, ct_flow->pre_ct_rule,
					pre_ct_attr);
	mlx5_modify_header_dealloc(esw->dev, pre_ct_attr->modify_hdr);
	mlx5_eswitch_del_offloaded_rule(esw, ct_flow->post_ct_rule,
					&ct_flow->post_ct_attr);
	mlx5_esw_chains_put_chain_mapping(esw, ct_flow->chain_mapping);
	idr_remove(&ct_priv->fte_ids, ct_flow->fte_id);
	kfree(ct_flow);
}

void
mlx5_tc_ct_delete_flow(struct mlx5e_priv *priv, struct mlx5e_tc_flow *flow,
		       struct mlx5_esw_flow_attr *attr)
{
	struct mlx5_tc_ct_priv *ct_priv = mlx5_tc_ct_get_ct_priv(priv);
	struct mlx5_ct_flow *ct_flow = attr->ct_attr.ct_flow;

	/* We are called on error to clean up stuff from parsing
	 * but we don't have anything for now
	 */
	if (!ct_flow)
		return;

	mutex_lock(&ct_priv->control_lock);
	__mlx5_tc_ct_delete_flow(ct_priv, ct_flow);
	mutex_unlock(&ct_priv->control_lock);
}

static int
mlx5_tc_ct_init_check_support(struct mlx5_eswitch *esw,
			      const char **err_msg)
{
#if !IS_ENABLED(CONFIG_NET_TC_SKB_EXT)
	/* cannot restore chain ID on HW miss */

	*err_msg = "tc skb extension missing";
	return -EOPNOTSUPP;
#endif

	if (!MLX5_CAP_ESW_FLOWTABLE_FDB(esw->dev, ignore_flow_level)) {
		*err_msg = "firmware level support is missing";
		return -EOPNOTSUPP;
	}

	if (!mlx5_eswitch_vlan_actions_supported(esw->dev, 1)) {
		/* vlan workaround should be avoided for multi chain rules.
		 * This is just a sanity check as pop vlan action should
		 * be supported by any FW that supports ignore_flow_level
		 */

		*err_msg = "firmware vlan actions support is missing";
		return -EOPNOTSUPP;
	}

	if (!MLX5_CAP_ESW_FLOWTABLE(esw->dev,
				    fdb_modify_header_fwd_to_table)) {
		/* CT always writes to registers which are mod header actions.
		 * Therefore, mod header and goto is required
		 */

		*err_msg = "firmware fwd and modify support is missing";
		return -EOPNOTSUPP;
	}

	if (!mlx5_eswitch_reg_c1_loopback_enabled(esw)) {
		*err_msg = "register loopback isn't supported";
		return -EOPNOTSUPP;
	}

	return 0;
}

static void
mlx5_tc_ct_init_err(struct mlx5e_rep_priv *rpriv, const char *msg, int err)
{
	if (msg)
		netdev_warn(rpriv->netdev,
			    "tc ct offload not supported, %s, err: %d\n",
			    msg, err);
	else
		netdev_warn(rpriv->netdev,
			    "tc ct offload not supported, err: %d\n",
			    err);
}

int
mlx5_tc_ct_init(struct mlx5_rep_uplink_priv *uplink_priv)
{
	struct mlx5_tc_ct_priv *ct_priv;
	struct mlx5e_rep_priv *rpriv;
	struct mlx5_eswitch *esw;
	struct mlx5e_priv *priv;
	const char *msg;
	int err;

	rpriv = container_of(uplink_priv, struct mlx5e_rep_priv, uplink_priv);
	priv = netdev_priv(rpriv->netdev);
	esw = priv->mdev->priv.eswitch;

	err = mlx5_tc_ct_init_check_support(esw, &msg);
	if (err) {
		mlx5_tc_ct_init_err(rpriv, msg, err);
		goto err_support;
	}

	ct_priv = kzalloc(sizeof(*ct_priv), GFP_KERNEL);
	if (!ct_priv) {
		mlx5_tc_ct_init_err(rpriv, NULL, -ENOMEM);
		goto err_alloc;
	}

	ct_priv->esw = esw;
	ct_priv->netdev = rpriv->netdev;
	ct_priv->ct = mlx5_esw_chains_create_global_table(esw);
	if (IS_ERR(ct_priv->ct)) {
		err = PTR_ERR(ct_priv->ct);
		mlx5_tc_ct_init_err(rpriv, "failed to create ct table", err);
		goto err_ct_tbl;
	}

	ct_priv->ct_nat = mlx5_esw_chains_create_global_table(esw);
	if (IS_ERR(ct_priv->ct_nat)) {
		err = PTR_ERR(ct_priv->ct_nat);
		mlx5_tc_ct_init_err(rpriv, "failed to create ct nat table",
				    err);
		goto err_ct_nat_tbl;
	}

	ct_priv->post_ct = mlx5_esw_chains_create_global_table(esw);
	if (IS_ERR(ct_priv->post_ct)) {
		err = PTR_ERR(ct_priv->post_ct);
		mlx5_tc_ct_init_err(rpriv, "failed to create post ct table",
				    err);
		goto err_post_ct_tbl;
	}

	idr_init(&ct_priv->fte_ids);
	mutex_init(&ct_priv->control_lock);

	/* Done, set ct_priv to know it initializted */
	uplink_priv->ct_priv = ct_priv;

	return 0;

err_post_ct_tbl:
	mlx5_esw_chains_destroy_global_table(esw, ct_priv->ct_nat);
err_ct_nat_tbl:
	mlx5_esw_chains_destroy_global_table(esw, ct_priv->ct);
err_ct_tbl:
	kfree(ct_priv);
err_alloc:
err_support:

	return 0;
}

void
mlx5_tc_ct_clean(struct mlx5_rep_uplink_priv *uplink_priv)
{
	struct mlx5_tc_ct_priv *ct_priv = uplink_priv->ct_priv;

	if (!ct_priv)
		return;

	mlx5_esw_chains_destroy_global_table(ct_priv->esw, ct_priv->post_ct);
	mlx5_esw_chains_destroy_global_table(ct_priv->esw, ct_priv->ct_nat);
	mlx5_esw_chains_destroy_global_table(ct_priv->esw, ct_priv->ct);

	mutex_destroy(&ct_priv->control_lock);
	idr_destroy(&ct_priv->fte_ids);
	kfree(ct_priv);

	uplink_priv->ct_priv = NULL;
}
