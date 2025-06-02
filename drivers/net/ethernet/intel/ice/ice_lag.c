// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2021, Intel Corporation. */

/* Link Aggregation code */

#include "ice.h"
#include "ice_lib.h"
#include "ice_lag.h"

#define ICE_LAG_RES_SHARED	BIT(14)
#define ICE_LAG_RES_VALID	BIT(15)

#define LACP_TRAIN_PKT_LEN		16
static const u8 lacp_train_pkt[LACP_TRAIN_PKT_LEN] = { 0, 0, 0, 0, 0, 0,
						       0, 0, 0, 0, 0, 0,
						       0x88, 0x09, 0, 0 };

#define ICE_RECIPE_LEN			64
static const u8 ice_dflt_vsi_rcp[ICE_RECIPE_LEN] = {
	0x05, 0, 0, 0, 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0x85, 0, 0x01, 0, 0, 0, 0xff, 0xff, 0x08, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0x30 };
static const u8 ice_lport_rcp[ICE_RECIPE_LEN] = {
	0x05, 0, 0, 0, 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0x85, 0, 0x16, 0, 0, 0, 0xff, 0xff, 0x07, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0x30 };

/**
 * ice_lag_set_primary - set PF LAG state as Primary
 * @lag: LAG info struct
 */
static void ice_lag_set_primary(struct ice_lag *lag)
{
	struct ice_pf *pf = lag->pf;

	if (!pf)
		return;

	if (lag->role != ICE_LAG_UNSET && lag->role != ICE_LAG_BACKUP) {
		dev_warn(ice_pf_to_dev(pf), "%s: Attempt to be Primary, but incompatible state.\n",
			 netdev_name(lag->netdev));
		return;
	}

	lag->role = ICE_LAG_PRIMARY;
}

/**
 * ice_lag_set_backup - set PF LAG state to Backup
 * @lag: LAG info struct
 */
static void ice_lag_set_backup(struct ice_lag *lag)
{
	struct ice_pf *pf = lag->pf;

	if (!pf)
		return;

	if (lag->role != ICE_LAG_UNSET && lag->role != ICE_LAG_PRIMARY) {
		dev_dbg(ice_pf_to_dev(pf), "%s: Attempt to be Backup, but incompatible state\n",
			netdev_name(lag->netdev));
		return;
	}

	lag->role = ICE_LAG_BACKUP;
}

/**
 * netif_is_same_ice - determine if netdev is on the same ice NIC as local PF
 * @pf: local PF struct
 * @netdev: netdev we are evaluating
 */
static bool netif_is_same_ice(struct ice_pf *pf, struct net_device *netdev)
{
	struct ice_netdev_priv *np;
	struct ice_pf *test_pf;
	struct ice_vsi *vsi;

	if (!netif_is_ice(netdev))
		return false;

	np = netdev_priv(netdev);
	if (!np)
		return false;

	vsi = np->vsi;
	if (!vsi)
		return false;

	test_pf = vsi->back;
	if (!test_pf)
		return false;

	if (pf->pdev->bus != test_pf->pdev->bus ||
	    pf->pdev->slot != test_pf->pdev->slot)
		return false;

	return true;
}

/**
 * ice_netdev_to_lag - return pointer to associated lag struct from netdev
 * @netdev: pointer to net_device struct to query
 */
static struct ice_lag *ice_netdev_to_lag(struct net_device *netdev)
{
	struct ice_netdev_priv *np;
	struct ice_vsi *vsi;

	if (!netif_is_ice(netdev))
		return NULL;

	np = netdev_priv(netdev);
	if (!np)
		return NULL;

	vsi = np->vsi;
	if (!vsi)
		return NULL;

	return vsi->back->lag;
}

/**
 * ice_lag_find_hw_by_lport - return an hw struct from bond members lport
 * @lag: lag struct
 * @lport: lport value to search for
 */
static struct ice_hw *
ice_lag_find_hw_by_lport(struct ice_lag *lag, u8 lport)
{
	struct ice_lag_netdev_list *entry;
	struct net_device *tmp_netdev;
	struct ice_netdev_priv *np;
	struct ice_hw *hw;

	list_for_each_entry(entry, lag->netdev_head, node) {
		tmp_netdev = entry->netdev;
		if (!tmp_netdev || !netif_is_ice(tmp_netdev))
			continue;

		np = netdev_priv(tmp_netdev);
		if (!np || !np->vsi)
			continue;

		hw = &np->vsi->back->hw;
		if (hw->port_info->lport == lport)
			return hw;
	}

	return NULL;
}

/**
 * ice_pkg_has_lport_extract - check if lport extraction supported
 * @hw: HW struct
 */
static bool ice_pkg_has_lport_extract(struct ice_hw *hw)
{
	int i;

	for (i = 0; i < hw->blk[ICE_BLK_SW].es.count; i++) {
		u16 offset;
		u8 fv_prot;

		ice_find_prot_off(hw, ICE_BLK_SW, ICE_SW_DEFAULT_PROFILE, i,
				  &fv_prot, &offset);
		if (fv_prot == ICE_FV_PROT_MDID &&
		    offset == ICE_LP_EXT_BUF_OFFSET)
			return true;
	}
	return false;
}

/**
 * ice_lag_find_primary - returns pointer to primary interfaces lag struct
 * @lag: local interfaces lag struct
 */
static struct ice_lag *ice_lag_find_primary(struct ice_lag *lag)
{
	struct ice_lag *primary_lag = NULL;
	struct list_head *tmp;

	list_for_each(tmp, lag->netdev_head) {
		struct ice_lag_netdev_list *entry;
		struct ice_lag *tmp_lag;

		entry = list_entry(tmp, struct ice_lag_netdev_list, node);
		tmp_lag = ice_netdev_to_lag(entry->netdev);
		if (tmp_lag && tmp_lag->primary) {
			primary_lag = tmp_lag;
			break;
		}
	}

	return primary_lag;
}

/**
 * ice_lag_cfg_fltr - Add/Remove rule for LAG
 * @lag: lag struct for local interface
 * @act: rule action
 * @recipe_id: recipe id for the new rule
 * @rule_idx: pointer to rule index
 * @direction: ICE_FLTR_RX or ICE_FLTR_TX
 * @add: boolean on whether we are adding filters
 */
static int
ice_lag_cfg_fltr(struct ice_lag *lag, u32 act, u16 recipe_id, u16 *rule_idx,
		 u8 direction, bool add)
{
	struct ice_sw_rule_lkup_rx_tx *s_rule;
	u16 s_rule_sz, vsi_num;
	struct ice_hw *hw;
	u8 *eth_hdr;
	u32 opc;
	int err;

	hw = &lag->pf->hw;
	vsi_num = ice_get_hw_vsi_num(hw, 0);

	s_rule_sz = ICE_SW_RULE_RX_TX_ETH_HDR_SIZE(s_rule);
	s_rule = kzalloc(s_rule_sz, GFP_KERNEL);
	if (!s_rule) {
		dev_err(ice_pf_to_dev(lag->pf), "error allocating rule for LAG\n");
		return -ENOMEM;
	}

	if (add) {
		eth_hdr = s_rule->hdr_data;
		ice_fill_eth_hdr(eth_hdr);

		act |= FIELD_PREP(ICE_SINGLE_ACT_VSI_ID_M, vsi_num);

		s_rule->recipe_id = cpu_to_le16(recipe_id);
		if (direction == ICE_FLTR_RX) {
			s_rule->hdr.type =
				cpu_to_le16(ICE_AQC_SW_RULES_T_LKUP_RX);
			s_rule->src = cpu_to_le16(hw->port_info->lport);
		} else {
			s_rule->hdr.type =
				cpu_to_le16(ICE_AQC_SW_RULES_T_LKUP_TX);
			s_rule->src = cpu_to_le16(vsi_num);
		}
		s_rule->act = cpu_to_le32(act);
		s_rule->hdr_len = cpu_to_le16(DUMMY_ETH_HDR_LEN);
		opc = ice_aqc_opc_add_sw_rules;
	} else {
		s_rule->index = cpu_to_le16(*rule_idx);
		opc = ice_aqc_opc_remove_sw_rules;
	}

	err = ice_aq_sw_rules(&lag->pf->hw, s_rule, s_rule_sz, 1, opc, NULL);
	if (err)
		goto dflt_fltr_free;

	if (add)
		*rule_idx = le16_to_cpu(s_rule->index);
	else
		*rule_idx = 0;

dflt_fltr_free:
	kfree(s_rule);
	return err;
}

/**
 * ice_lag_cfg_dflt_fltr - Add/Remove default VSI rule for LAG
 * @lag: lag struct for local interface
 * @add: boolean on whether to add filter
 */
static int
ice_lag_cfg_dflt_fltr(struct ice_lag *lag, bool add)
{
	u32 act = ICE_SINGLE_ACT_VSI_FORWARDING |
		ICE_SINGLE_ACT_VALID_BIT | ICE_SINGLE_ACT_LAN_ENABLE;
	int err;

	err = ice_lag_cfg_fltr(lag, act, lag->pf_recipe, &lag->pf_rx_rule_id,
			       ICE_FLTR_RX, add);
	if (err)
		goto err_rx;

	act = ICE_SINGLE_ACT_VSI_FORWARDING | ICE_SINGLE_ACT_VALID_BIT |
	      ICE_SINGLE_ACT_LB_ENABLE;
	err = ice_lag_cfg_fltr(lag, act, lag->pf_recipe, &lag->pf_tx_rule_id,
			       ICE_FLTR_TX, add);
	if (err)
		goto err_tx;

	return 0;

err_tx:
	ice_lag_cfg_fltr(lag, act, lag->pf_recipe, &lag->pf_rx_rule_id,
			 ICE_FLTR_RX, !add);
err_rx:
	return err;
}

/**
 * ice_lag_cfg_drop_fltr - Add/Remove lport drop rule
 * @lag: lag struct for local interface
 * @add: boolean on whether to add filter
 */
static int
ice_lag_cfg_drop_fltr(struct ice_lag *lag, bool add)
{
	u32 act = ICE_SINGLE_ACT_VSI_FORWARDING |
		  ICE_SINGLE_ACT_VALID_BIT |
		  ICE_SINGLE_ACT_DROP;

	return ice_lag_cfg_fltr(lag, act, lag->lport_recipe,
				&lag->lport_rule_idx, ICE_FLTR_RX, add);
}

/**
 * ice_lag_cfg_pf_fltrs - set filters up for new active port
 * @lag: local interfaces lag struct
 * @ptr: opaque data containing notifier event
 */
static void
ice_lag_cfg_pf_fltrs(struct ice_lag *lag, void *ptr)
{
	struct netdev_notifier_bonding_info *info;
	struct netdev_bonding_info *bonding_info;
	struct net_device *event_netdev;
	struct device *dev;

	event_netdev = netdev_notifier_info_to_dev(ptr);
	/* not for this netdev */
	if (event_netdev != lag->netdev)
		return;

	info = (struct netdev_notifier_bonding_info *)ptr;
	bonding_info = &info->bonding_info;
	dev = ice_pf_to_dev(lag->pf);

	/* interface not active - remove old default VSI rule */
	if (bonding_info->slave.state && lag->pf_rx_rule_id) {
		if (ice_lag_cfg_dflt_fltr(lag, false))
			dev_err(dev, "Error removing old default VSI filter\n");
		if (ice_lag_cfg_drop_fltr(lag, true))
			dev_err(dev, "Error adding new drop filter\n");
		return;
	}

	/* interface becoming active - add new default VSI rule */
	if (!bonding_info->slave.state && !lag->pf_rx_rule_id) {
		if (ice_lag_cfg_dflt_fltr(lag, true))
			dev_err(dev, "Error adding new default VSI filter\n");
		if (lag->lport_rule_idx && ice_lag_cfg_drop_fltr(lag, false))
			dev_err(dev, "Error removing old drop filter\n");
	}
}

/**
 * ice_display_lag_info - print LAG info
 * @lag: LAG info struct
 */
static void ice_display_lag_info(struct ice_lag *lag)
{
	const char *name, *upper, *role, *bonded, *primary;
	struct device *dev = &lag->pf->pdev->dev;

	name = lag->netdev ? netdev_name(lag->netdev) : "unset";
	upper = lag->upper_netdev ? netdev_name(lag->upper_netdev) : "unset";
	primary = lag->primary ? "TRUE" : "FALSE";
	bonded = lag->bonded ? "BONDED" : "UNBONDED";

	switch (lag->role) {
	case ICE_LAG_NONE:
		role = "NONE";
		break;
	case ICE_LAG_PRIMARY:
		role = "PRIMARY";
		break;
	case ICE_LAG_BACKUP:
		role = "BACKUP";
		break;
	case ICE_LAG_UNSET:
		role = "UNSET";
		break;
	default:
		role = "ERROR";
	}

	dev_dbg(dev, "%s %s, upper:%s, role:%s, primary:%s\n", name, bonded,
		upper, role, primary);
}

/**
 * ice_lag_qbuf_recfg - generate a buffer of queues for a reconfigure command
 * @hw: HW struct that contains the queue contexts
 * @qbuf: pointer to buffer to populate
 * @vsi_num: index of the VSI in PF space
 * @numq: number of queues to search for
 * @tc: traffic class that contains the queues
 *
 * function returns the number of valid queues in buffer
 */
static u16
ice_lag_qbuf_recfg(struct ice_hw *hw, struct ice_aqc_cfg_txqs_buf *qbuf,
		   u16 vsi_num, u16 numq, u8 tc)
{
	struct ice_q_ctx *q_ctx;
	u16 qid, count = 0;
	struct ice_pf *pf;
	int i;

	pf = hw->back;
	for (i = 0; i < numq; i++) {
		q_ctx = ice_get_lan_q_ctx(hw, vsi_num, tc, i);
		if (!q_ctx) {
			dev_dbg(ice_hw_to_dev(hw), "%s queue %d NO Q CONTEXT\n",
				__func__, i);
			continue;
		}
		if (q_ctx->q_teid == ICE_INVAL_TEID) {
			dev_dbg(ice_hw_to_dev(hw), "%s queue %d INVAL TEID\n",
				__func__, i);
			continue;
		}
		if (q_ctx->q_handle == ICE_INVAL_Q_HANDLE) {
			dev_dbg(ice_hw_to_dev(hw), "%s queue %d INVAL Q HANDLE\n",
				__func__, i);
			continue;
		}

		qid = pf->vsi[vsi_num]->txq_map[q_ctx->q_handle];
		qbuf->queue_info[count].q_handle = cpu_to_le16(qid);
		qbuf->queue_info[count].tc = tc;
		qbuf->queue_info[count].q_teid = cpu_to_le32(q_ctx->q_teid);
		count++;
	}

	return count;
}

/**
 * ice_lag_get_sched_parent - locate or create a sched node parent
 * @hw: HW struct for getting parent in
 * @tc: traffic class on parent/node
 */
static struct ice_sched_node *
ice_lag_get_sched_parent(struct ice_hw *hw, u8 tc)
{
	struct ice_sched_node *tc_node, *aggnode, *parent = NULL;
	u16 num_nodes[ICE_AQC_TOPO_MAX_LEVEL_NUM] = { 0 };
	struct ice_port_info *pi = hw->port_info;
	struct device *dev;
	u8 aggl, vsil;
	int n;

	dev = ice_hw_to_dev(hw);

	tc_node = ice_sched_get_tc_node(pi, tc);
	if (!tc_node) {
		dev_warn(dev, "Failure to find TC node for LAG move\n");
		return parent;
	}

	aggnode = ice_sched_get_agg_node(pi, tc_node, ICE_DFLT_AGG_ID);
	if (!aggnode) {
		dev_warn(dev, "Failure to find aggregate node for LAG move\n");
		return parent;
	}

	aggl = ice_sched_get_agg_layer(hw);
	vsil = ice_sched_get_vsi_layer(hw);

	for (n = aggl + 1; n < vsil; n++)
		num_nodes[n] = 1;

	for (n = 0; n < aggnode->num_children; n++) {
		parent = ice_sched_get_free_vsi_parent(hw, aggnode->children[n],
						       num_nodes);
		if (parent)
			return parent;
	}

	/* if free parent not found - add one */
	parent = aggnode;
	for (n = aggl + 1; n < vsil; n++) {
		u16 num_nodes_added;
		u32 first_teid;
		int err;

		err = ice_sched_add_nodes_to_layer(pi, tc_node, parent, n,
						   num_nodes[n], &first_teid,
						   &num_nodes_added);
		if (err || num_nodes[n] != num_nodes_added)
			return NULL;

		if (num_nodes_added)
			parent = ice_sched_find_node_by_teid(tc_node,
							     first_teid);
		else
			parent = parent->children[0];
		if (!parent) {
			dev_warn(dev, "Failure to add new parent for LAG move\n");
			return parent;
		}
	}

	return parent;
}

/**
 * ice_lag_move_vf_node_tc - move scheduling nodes for one VF on one TC
 * @lag: lag info struct
 * @oldport: lport of previous nodes location
 * @newport: lport of destination nodes location
 * @vsi_num: array index of VSI in PF space
 * @tc: traffic class to move
 */
static void
ice_lag_move_vf_node_tc(struct ice_lag *lag, u8 oldport, u8 newport,
			u16 vsi_num, u8 tc)
{
	DEFINE_RAW_FLEX(struct ice_aqc_move_elem, buf, teid, 1);
	struct device *dev = ice_pf_to_dev(lag->pf);
	u16 numq, valq, num_moved, qbuf_size;
	u16 buf_size = __struct_size(buf);
	struct ice_aqc_cfg_txqs_buf *qbuf;
	struct ice_sched_node *n_prt;
	struct ice_hw *new_hw = NULL;
	__le32 teid, parent_teid;
	struct ice_vsi_ctx *ctx;
	u32 tmp_teid;

	ctx = ice_get_vsi_ctx(&lag->pf->hw, vsi_num);
	if (!ctx) {
		dev_warn(dev, "Unable to locate VSI context for LAG failover\n");
		return;
	}

	/* check to see if this VF is enabled on this TC */
	if (!ctx->sched.vsi_node[tc])
		return;

	/* locate HW struct for destination port */
	new_hw = ice_lag_find_hw_by_lport(lag, newport);
	if (!new_hw) {
		dev_warn(dev, "Unable to locate HW struct for LAG node destination\n");
		return;
	}

	numq = ctx->num_lan_q_entries[tc];
	teid = ctx->sched.vsi_node[tc]->info.node_teid;
	tmp_teid = le32_to_cpu(teid);
	parent_teid = ctx->sched.vsi_node[tc]->info.parent_teid;
	/* if no teid assigned or numq == 0, then this TC is not active */
	if (!tmp_teid || !numq)
		return;

	/* suspend VSI subtree for Traffic Class "tc" on
	 * this VF's VSI
	 */
	if (ice_sched_suspend_resume_elems(&lag->pf->hw, 1, &tmp_teid, true))
		dev_dbg(dev, "Problem suspending traffic for LAG node move\n");

	/* reconfigure all VF's queues on this Traffic Class
	 * to new port
	 */
	qbuf_size = struct_size(qbuf, queue_info, numq);
	qbuf = kzalloc(qbuf_size, GFP_KERNEL);
	if (!qbuf) {
		dev_warn(dev, "Failure allocating memory for VF queue recfg buffer\n");
		goto resume_traffic;
	}

	/* add the per queue info for the reconfigure command buffer */
	valq = ice_lag_qbuf_recfg(&lag->pf->hw, qbuf, vsi_num, numq, tc);
	if (!valq) {
		dev_dbg(dev, "No valid queues found for LAG failover\n");
		goto qbuf_none;
	}

	if (ice_aq_cfg_lan_txq(&lag->pf->hw, qbuf, qbuf_size, valq, oldport,
			       newport, NULL)) {
		dev_warn(dev, "Failure to configure queues for LAG failover\n");
		goto qbuf_err;
	}

qbuf_none:
	kfree(qbuf);

	/* find new parent in destination port's tree for VF VSI node on this
	 * Traffic Class
	 */
	n_prt = ice_lag_get_sched_parent(new_hw, tc);
	if (!n_prt)
		goto resume_traffic;

	/* Move Vf's VSI node for this TC to newport's scheduler tree */
	buf->hdr.src_parent_teid = parent_teid;
	buf->hdr.dest_parent_teid = n_prt->info.node_teid;
	buf->hdr.num_elems = cpu_to_le16(1);
	buf->hdr.mode = ICE_AQC_MOVE_ELEM_MODE_KEEP_OWN;
	buf->teid[0] = teid;

	if (ice_aq_move_sched_elems(&lag->pf->hw, buf, buf_size, &num_moved))
		dev_warn(dev, "Failure to move VF nodes for failover\n");
	else
		ice_sched_update_parent(n_prt, ctx->sched.vsi_node[tc]);

	goto resume_traffic;

qbuf_err:
	kfree(qbuf);

resume_traffic:
	/* restart traffic for VSI node */
	if (ice_sched_suspend_resume_elems(&lag->pf->hw, 1, &tmp_teid, false))
		dev_dbg(dev, "Problem restarting traffic for LAG node move\n");
}

/**
 * ice_lag_build_netdev_list - populate the lag struct's netdev list
 * @lag: local lag struct
 * @ndlist: pointer to netdev list to populate
 */
static void ice_lag_build_netdev_list(struct ice_lag *lag,
				      struct ice_lag_netdev_list *ndlist)
{
	struct ice_lag_netdev_list *nl;
	struct net_device *tmp_nd;

	INIT_LIST_HEAD(&ndlist->node);
	rcu_read_lock();
	for_each_netdev_in_bond_rcu(lag->upper_netdev, tmp_nd) {
		nl = kzalloc(sizeof(*nl), GFP_ATOMIC);
		if (!nl)
			break;

		nl->netdev = tmp_nd;
		list_add(&nl->node, &ndlist->node);
	}
	rcu_read_unlock();
	lag->netdev_head = &ndlist->node;
}

/**
 * ice_lag_destroy_netdev_list - free lag struct's netdev list
 * @lag: pointer to local lag struct
 * @ndlist: pointer to lag struct netdev list
 */
static void ice_lag_destroy_netdev_list(struct ice_lag *lag,
					struct ice_lag_netdev_list *ndlist)
{
	struct ice_lag_netdev_list *entry, *n;

	rcu_read_lock();
	list_for_each_entry_safe(entry, n, &ndlist->node, node) {
		list_del(&entry->node);
		kfree(entry);
	}
	rcu_read_unlock();
	lag->netdev_head = NULL;
}

/**
 * ice_lag_move_single_vf_nodes - Move Tx scheduling nodes for single VF
 * @lag: primary interface LAG struct
 * @oldport: lport of previous interface
 * @newport: lport of destination interface
 * @vsi_num: SW index of VF's VSI
 */
static void
ice_lag_move_single_vf_nodes(struct ice_lag *lag, u8 oldport, u8 newport,
			     u16 vsi_num)
{
	u8 tc;

	ice_for_each_traffic_class(tc)
		ice_lag_move_vf_node_tc(lag, oldport, newport, vsi_num, tc);
}

/**
 * ice_lag_move_new_vf_nodes - Move Tx scheduling nodes for a VF if required
 * @vf: the VF to move Tx nodes for
 *
 * Called just after configuring new VF queues. Check whether the VF Tx
 * scheduling nodes need to be updated to fail over to the active port. If so,
 * move them now.
 */
void ice_lag_move_new_vf_nodes(struct ice_vf *vf)
{
	struct ice_lag_netdev_list ndlist;
	u8 pri_port, act_port;
	struct ice_lag *lag;
	struct ice_vsi *vsi;
	struct ice_pf *pf;

	vsi = ice_get_vf_vsi(vf);

	if (WARN_ON(!vsi))
		return;

	if (WARN_ON(vsi->type != ICE_VSI_VF))
		return;

	pf = vf->pf;
	lag = pf->lag;

	mutex_lock(&pf->lag_mutex);
	if (!lag->bonded)
		goto new_vf_unlock;

	pri_port = pf->hw.port_info->lport;
	act_port = lag->active_port;

	if (lag->upper_netdev)
		ice_lag_build_netdev_list(lag, &ndlist);

	if (ice_is_feature_supported(pf, ICE_F_SRIOV_LAG) &&
	    lag->bonded && lag->primary && pri_port != act_port &&
	    !list_empty(lag->netdev_head))
		ice_lag_move_single_vf_nodes(lag, pri_port, act_port, vsi->idx);

	ice_lag_destroy_netdev_list(lag, &ndlist);

new_vf_unlock:
	mutex_unlock(&pf->lag_mutex);
}

/**
 * ice_lag_move_vf_nodes - move Tx scheduling nodes for all VFs to new port
 * @lag: lag info struct
 * @oldport: lport of previous interface
 * @newport: lport of destination interface
 */
static void ice_lag_move_vf_nodes(struct ice_lag *lag, u8 oldport, u8 newport)
{
	struct ice_pf *pf;
	int i;

	if (!lag->primary)
		return;

	pf = lag->pf;
	ice_for_each_vsi(pf, i)
		if (pf->vsi[i] && pf->vsi[i]->type == ICE_VSI_VF)
			ice_lag_move_single_vf_nodes(lag, oldport, newport, i);
}

/**
 * ice_lag_move_vf_nodes_cfg - move vf nodes outside LAG netdev event context
 * @lag: local lag struct
 * @src_prt: lport value for source port
 * @dst_prt: lport value for destination port
 *
 * This function is used to move nodes during an out-of-netdev-event situation,
 * primarily when the driver needs to reconfigure or recreate resources.
 *
 * Must be called while holding the lag_mutex to avoid lag events from
 * processing while out-of-sync moves are happening.  Also, paired moves,
 * such as used in a reset flow, should both be called under the same mutex
 * lock to avoid changes between start of reset and end of reset.
 */
void ice_lag_move_vf_nodes_cfg(struct ice_lag *lag, u8 src_prt, u8 dst_prt)
{
	struct ice_lag_netdev_list ndlist;

	ice_lag_build_netdev_list(lag, &ndlist);
	ice_lag_move_vf_nodes(lag, src_prt, dst_prt);
	ice_lag_destroy_netdev_list(lag, &ndlist);
}

#define ICE_LAG_SRIOV_CP_RECIPE		10
#define ICE_LAG_SRIOV_TRAIN_PKT_LEN	16

/**
 * ice_lag_cfg_cp_fltr - configure filter for control packets
 * @lag: local interface's lag struct
 * @add: add or remove rule
 */
static void
ice_lag_cfg_cp_fltr(struct ice_lag *lag, bool add)
{
	struct ice_sw_rule_lkup_rx_tx *s_rule = NULL;
	struct ice_vsi *vsi;
	u16 buf_len, opc;

	vsi = lag->pf->vsi[0];

	buf_len = ICE_SW_RULE_RX_TX_HDR_SIZE(s_rule,
					     ICE_LAG_SRIOV_TRAIN_PKT_LEN);
	s_rule = kzalloc(buf_len, GFP_KERNEL);
	if (!s_rule) {
		netdev_warn(lag->netdev, "-ENOMEM error configuring CP filter\n");
		return;
	}

	if (add) {
		s_rule->hdr.type = cpu_to_le16(ICE_AQC_SW_RULES_T_LKUP_RX);
		s_rule->recipe_id = cpu_to_le16(ICE_LAG_SRIOV_CP_RECIPE);
		s_rule->src = cpu_to_le16(vsi->port_info->lport);
		s_rule->act = cpu_to_le32(ICE_FWD_TO_VSI |
					  ICE_SINGLE_ACT_LAN_ENABLE |
					  ICE_SINGLE_ACT_VALID_BIT |
					  FIELD_PREP(ICE_SINGLE_ACT_VSI_ID_M, vsi->vsi_num));
		s_rule->hdr_len = cpu_to_le16(ICE_LAG_SRIOV_TRAIN_PKT_LEN);
		memcpy(s_rule->hdr_data, lacp_train_pkt, LACP_TRAIN_PKT_LEN);
		opc = ice_aqc_opc_add_sw_rules;
	} else {
		opc = ice_aqc_opc_remove_sw_rules;
		s_rule->index = cpu_to_le16(lag->cp_rule_idx);
	}
	if (ice_aq_sw_rules(&lag->pf->hw, s_rule, buf_len, 1, opc, NULL)) {
		netdev_warn(lag->netdev, "Error %s CP rule for fail-over\n",
			    add ? "ADDING" : "REMOVING");
		goto cp_free;
	}

	if (add)
		lag->cp_rule_idx = le16_to_cpu(s_rule->index);
	else
		lag->cp_rule_idx = 0;

cp_free:
	kfree(s_rule);
}

/**
 * ice_lag_info_event - handle NETDEV_BONDING_INFO event
 * @lag: LAG info struct
 * @ptr: opaque data pointer
 *
 * ptr is to be cast to (netdev_notifier_bonding_info *)
 */
static void ice_lag_info_event(struct ice_lag *lag, void *ptr)
{
	struct netdev_notifier_bonding_info *info;
	struct netdev_bonding_info *bonding_info;
	struct net_device *event_netdev;
	const char *lag_netdev_name;

	event_netdev = netdev_notifier_info_to_dev(ptr);
	info = ptr;
	lag_netdev_name = netdev_name(lag->netdev);
	bonding_info = &info->bonding_info;

	if (event_netdev != lag->netdev || !lag->bonded || !lag->upper_netdev)
		return;

	if (bonding_info->master.bond_mode != BOND_MODE_ACTIVEBACKUP) {
		netdev_dbg(lag->netdev, "Bonding event recv, but mode not active/backup\n");
		goto lag_out;
	}

	if (strcmp(bonding_info->slave.slave_name, lag_netdev_name)) {
		netdev_dbg(lag->netdev, "Bonding event recv, but secondary info not for us\n");
		goto lag_out;
	}

	if (bonding_info->slave.state)
		ice_lag_set_backup(lag);
	else
		ice_lag_set_primary(lag);

lag_out:
	ice_display_lag_info(lag);
}

/**
 * ice_lag_reclaim_vf_tc - move scheduling nodes back to primary interface
 * @lag: primary interface lag struct
 * @src_hw: HW struct current node location
 * @vsi_num: VSI index in PF space
 * @tc: traffic class to move
 */
static void
ice_lag_reclaim_vf_tc(struct ice_lag *lag, struct ice_hw *src_hw, u16 vsi_num,
		      u8 tc)
{
	DEFINE_RAW_FLEX(struct ice_aqc_move_elem, buf, teid, 1);
	struct device *dev = ice_pf_to_dev(lag->pf);
	u16 numq, valq, num_moved, qbuf_size;
	u16 buf_size = __struct_size(buf);
	struct ice_aqc_cfg_txqs_buf *qbuf;
	struct ice_sched_node *n_prt;
	__le32 teid, parent_teid;
	struct ice_vsi_ctx *ctx;
	struct ice_hw *hw;
	u32 tmp_teid;

	hw = &lag->pf->hw;
	ctx = ice_get_vsi_ctx(hw, vsi_num);
	if (!ctx) {
		dev_warn(dev, "Unable to locate VSI context for LAG reclaim\n");
		return;
	}

	/* check to see if this VF is enabled on this TC */
	if (!ctx->sched.vsi_node[tc])
		return;

	numq = ctx->num_lan_q_entries[tc];
	teid = ctx->sched.vsi_node[tc]->info.node_teid;
	tmp_teid = le32_to_cpu(teid);
	parent_teid = ctx->sched.vsi_node[tc]->info.parent_teid;

	/* if !teid or !numq, then this TC is not active */
	if (!tmp_teid || !numq)
		return;

	/* suspend traffic */
	if (ice_sched_suspend_resume_elems(hw, 1, &tmp_teid, true))
		dev_dbg(dev, "Problem suspending traffic for LAG node move\n");

	/* reconfig queues for new port */
	qbuf_size = struct_size(qbuf, queue_info, numq);
	qbuf = kzalloc(qbuf_size, GFP_KERNEL);
	if (!qbuf) {
		dev_warn(dev, "Failure allocating memory for VF queue recfg buffer\n");
		goto resume_reclaim;
	}

	/* add the per queue info for the reconfigure command buffer */
	valq = ice_lag_qbuf_recfg(hw, qbuf, vsi_num, numq, tc);
	if (!valq) {
		dev_dbg(dev, "No valid queues found for LAG reclaim\n");
		goto reclaim_none;
	}

	if (ice_aq_cfg_lan_txq(hw, qbuf, qbuf_size, numq,
			       src_hw->port_info->lport, hw->port_info->lport,
			       NULL)) {
		dev_warn(dev, "Failure to configure queues for LAG failover\n");
		goto reclaim_qerr;
	}

reclaim_none:
	kfree(qbuf);

	/* find parent in primary tree */
	n_prt = ice_lag_get_sched_parent(hw, tc);
	if (!n_prt)
		goto resume_reclaim;

	/* Move node to new parent */
	buf->hdr.src_parent_teid = parent_teid;
	buf->hdr.dest_parent_teid = n_prt->info.node_teid;
	buf->hdr.num_elems = cpu_to_le16(1);
	buf->hdr.mode = ICE_AQC_MOVE_ELEM_MODE_KEEP_OWN;
	buf->teid[0] = teid;

	if (ice_aq_move_sched_elems(&lag->pf->hw, buf, buf_size, &num_moved))
		dev_warn(dev, "Failure to move VF nodes for LAG reclaim\n");
	else
		ice_sched_update_parent(n_prt, ctx->sched.vsi_node[tc]);

	goto resume_reclaim;

reclaim_qerr:
	kfree(qbuf);

resume_reclaim:
	/* restart traffic */
	if (ice_sched_suspend_resume_elems(hw, 1, &tmp_teid, false))
		dev_warn(dev, "Problem restarting traffic for LAG node reclaim\n");
}

/**
 * ice_lag_reclaim_vf_nodes - When interface leaving bond primary reclaims nodes
 * @lag: primary interface lag struct
 * @src_hw: HW struct for current node location
 */
static void
ice_lag_reclaim_vf_nodes(struct ice_lag *lag, struct ice_hw *src_hw)
{
	struct ice_pf *pf;
	int i, tc;

	if (!lag->primary || !src_hw)
		return;

	pf = lag->pf;
	ice_for_each_vsi(pf, i)
		if (pf->vsi[i] && pf->vsi[i]->type == ICE_VSI_VF)
			ice_for_each_traffic_class(tc)
				ice_lag_reclaim_vf_tc(lag, src_hw, i, tc);
}

/**
 * ice_lag_link - handle LAG link event
 * @lag: LAG info struct
 */
static void ice_lag_link(struct ice_lag *lag)
{
	struct ice_pf *pf = lag->pf;

	if (lag->bonded)
		dev_warn(ice_pf_to_dev(pf), "%s Already part of a bond\n",
			 netdev_name(lag->netdev));

	lag->bonded = true;
	lag->role = ICE_LAG_UNSET;
	netdev_info(lag->netdev, "Shared SR-IOV resources in bond are active\n");
}

/**
 * ice_lag_config_eswitch - configure eswitch to work with LAG
 * @lag: lag info struct
 * @netdev: active network interface device struct
 *
 * Updates all port representors in eswitch to use @netdev for Tx.
 *
 * Configures the netdev to keep dst metadata (also used in representor Tx).
 * This is required for an uplink without switchdev mode configured.
 */
static void ice_lag_config_eswitch(struct ice_lag *lag,
				   struct net_device *netdev)
{
	struct ice_repr *repr;
	unsigned long id;

	xa_for_each(&lag->pf->eswitch.reprs, id, repr)
		repr->dst->u.port_info.lower_dev = netdev;

	netif_keep_dst(netdev);
}

/**
 * ice_lag_unlink - handle unlink event
 * @lag: LAG info struct
 */
static void ice_lag_unlink(struct ice_lag *lag)
{
	u8 pri_port, act_port, loc_port;
	struct ice_pf *pf = lag->pf;

	if (!lag->bonded) {
		netdev_dbg(lag->netdev, "bonding unlink event on non-LAG netdev\n");
		return;
	}

	if (lag->primary) {
		act_port = lag->active_port;
		pri_port = lag->pf->hw.port_info->lport;
		if (act_port != pri_port && act_port != ICE_LAG_INVALID_PORT)
			ice_lag_move_vf_nodes(lag, act_port, pri_port);
		lag->primary = false;
		lag->active_port = ICE_LAG_INVALID_PORT;

		/* Config primary's eswitch back to normal operation. */
		ice_lag_config_eswitch(lag, lag->netdev);
	} else {
		struct ice_lag *primary_lag;

		primary_lag = ice_lag_find_primary(lag);
		if (primary_lag) {
			act_port = primary_lag->active_port;
			pri_port = primary_lag->pf->hw.port_info->lport;
			loc_port = pf->hw.port_info->lport;
			if (act_port == loc_port &&
			    act_port != ICE_LAG_INVALID_PORT) {
				ice_lag_reclaim_vf_nodes(primary_lag,
							 &lag->pf->hw);
				primary_lag->active_port = ICE_LAG_INVALID_PORT;
			}
		}
	}

	lag->bonded = false;
	lag->role = ICE_LAG_NONE;
	lag->upper_netdev = NULL;
}

/**
 * ice_lag_link_unlink - helper function to call lag_link/unlink
 * @lag: lag info struct
 * @ptr: opaque pointer data
 */
static void ice_lag_link_unlink(struct ice_lag *lag, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);
	struct netdev_notifier_changeupper_info *info = ptr;

	if (netdev != lag->netdev)
		return;

	if (info->linking)
		ice_lag_link(lag);
	else
		ice_lag_unlink(lag);
}

/**
 * ice_lag_set_swid - set the SWID on secondary interface
 * @primary_swid: primary interface's SWID
 * @local_lag: local interfaces LAG struct
 * @link: Is this a linking activity
 *
 * If link is false, then primary_swid should be expected to not be valid
 * This function should never be called in interrupt context.
 */
static void
ice_lag_set_swid(u16 primary_swid, struct ice_lag *local_lag,
		 bool link)
{
	struct ice_aqc_alloc_free_res_elem *buf;
	struct ice_aqc_set_port_params *cmd;
	struct ice_aq_desc desc;
	u16 buf_len, swid;
	int status, i;

	buf_len = struct_size(buf, elem, 1);
	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf) {
		dev_err(ice_pf_to_dev(local_lag->pf), "-ENOMEM error setting SWID\n");
		return;
	}

	buf->num_elems = cpu_to_le16(1);
	buf->res_type = cpu_to_le16(ICE_AQC_RES_TYPE_SWID);
	/* if unlinnking need to free the shared resource */
	if (!link && local_lag->bond_swid) {
		buf->elem[0].e.sw_resp = cpu_to_le16(local_lag->bond_swid);
		status = ice_aq_alloc_free_res(&local_lag->pf->hw, buf,
					       buf_len, ice_aqc_opc_free_res);
		if (status)
			dev_err(ice_pf_to_dev(local_lag->pf), "Error freeing SWID during LAG unlink\n");
		local_lag->bond_swid = 0;
	}

	if (link) {
		buf->res_type |=  cpu_to_le16(ICE_LAG_RES_SHARED |
					      ICE_LAG_RES_VALID);
		/* store the primary's SWID in case it leaves bond first */
		local_lag->bond_swid = primary_swid;
		buf->elem[0].e.sw_resp = cpu_to_le16(local_lag->bond_swid);
	} else {
		buf->elem[0].e.sw_resp =
			cpu_to_le16(local_lag->pf->hw.port_info->sw_id);
	}

	status = ice_aq_alloc_free_res(&local_lag->pf->hw, buf, buf_len,
				       ice_aqc_opc_alloc_res);
	if (status)
		dev_err(ice_pf_to_dev(local_lag->pf), "Error subscribing to SWID 0x%04X\n",
			local_lag->bond_swid);

	kfree(buf);

	/* Configure port param SWID to correct value */
	if (link)
		swid = primary_swid;
	else
		swid = local_lag->pf->hw.port_info->sw_id;

	cmd = &desc.params.set_port_params;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_set_port_params);

	cmd->swid = cpu_to_le16(ICE_AQC_PORT_SWID_VALID | swid);
	/* If this is happening in reset context, it is possible that the
	 * primary interface has not finished setting its SWID to SHARED
	 * yet.  Allow retries to account for this timing issue between
	 * interfaces.
	 */
	for (i = 0; i < ICE_LAG_RESET_RETRIES; i++) {
		status = ice_aq_send_cmd(&local_lag->pf->hw, &desc, NULL, 0,
					 NULL);
		if (!status)
			break;

		usleep_range(1000, 2000);
	}

	if (status)
		dev_err(ice_pf_to_dev(local_lag->pf), "Error setting SWID in port params %d\n",
			status);
}

/**
 * ice_lag_primary_swid - set/clear the SHARED attrib of primary's SWID
 * @lag: primary interface's lag struct
 * @link: is this a linking activity
 *
 * Implement setting primary SWID as shared using 0x020B
 */
static void ice_lag_primary_swid(struct ice_lag *lag, bool link)
{
	struct ice_hw *hw;
	u16 swid;

	hw = &lag->pf->hw;
	swid = hw->port_info->sw_id;

	if (ice_share_res(hw, ICE_AQC_RES_TYPE_SWID, link, swid))
		dev_warn(ice_pf_to_dev(lag->pf), "Failure to set primary interface shared status\n");
}

/**
 * ice_lag_add_prune_list - Adds event_pf's VSI to primary's prune list
 * @lag: lag info struct
 * @event_pf: PF struct for VSI we are adding to primary's prune list
 */
static void ice_lag_add_prune_list(struct ice_lag *lag, struct ice_pf *event_pf)
{
	u16 num_vsi, rule_buf_sz, vsi_list_id, event_vsi_num, prim_vsi_idx;
	struct ice_sw_rule_vsi_list *s_rule = NULL;
	struct device *dev;

	num_vsi = 1;

	dev = ice_pf_to_dev(lag->pf);
	event_vsi_num = event_pf->vsi[0]->vsi_num;
	prim_vsi_idx = lag->pf->vsi[0]->idx;

	if (!ice_find_vsi_list_entry(&lag->pf->hw, ICE_SW_LKUP_VLAN,
				     prim_vsi_idx, &vsi_list_id)) {
		dev_warn(dev, "Could not locate prune list when setting up SRIOV LAG\n");
		return;
	}

	rule_buf_sz = (u16)ICE_SW_RULE_VSI_LIST_SIZE(s_rule, num_vsi);
	s_rule = kzalloc(rule_buf_sz, GFP_KERNEL);
	if (!s_rule) {
		dev_warn(dev, "Error allocating space for prune list when configuring SRIOV LAG\n");
		return;
	}

	s_rule->hdr.type = cpu_to_le16(ICE_AQC_SW_RULES_T_PRUNE_LIST_SET);
	s_rule->index = cpu_to_le16(vsi_list_id);
	s_rule->number_vsi = cpu_to_le16(num_vsi);
	s_rule->vsi[0] = cpu_to_le16(event_vsi_num);

	if (ice_aq_sw_rules(&event_pf->hw, s_rule, rule_buf_sz, 1,
			    ice_aqc_opc_update_sw_rules, NULL))
		dev_warn(dev, "Error adding VSI prune list\n");
	kfree(s_rule);
}

/**
 * ice_lag_del_prune_list - Remove secondary's vsi from primary's prune list
 * @lag: primary interface's ice_lag struct
 * @event_pf: PF struct for unlinking interface
 */
static void ice_lag_del_prune_list(struct ice_lag *lag, struct ice_pf *event_pf)
{
	u16 num_vsi, vsi_num, vsi_idx, rule_buf_sz, vsi_list_id;
	struct ice_sw_rule_vsi_list *s_rule = NULL;
	struct device *dev;

	num_vsi = 1;

	dev = ice_pf_to_dev(lag->pf);
	vsi_num = event_pf->vsi[0]->vsi_num;
	vsi_idx = lag->pf->vsi[0]->idx;

	if (!ice_find_vsi_list_entry(&lag->pf->hw, ICE_SW_LKUP_VLAN,
				     vsi_idx, &vsi_list_id)) {
		dev_warn(dev, "Could not locate prune list when unwinding SRIOV LAG\n");
		return;
	}

	rule_buf_sz = (u16)ICE_SW_RULE_VSI_LIST_SIZE(s_rule, num_vsi);
	s_rule = kzalloc(rule_buf_sz, GFP_KERNEL);
	if (!s_rule) {
		dev_warn(dev, "Error allocating prune list when unwinding SRIOV LAG\n");
		return;
	}

	s_rule->hdr.type = cpu_to_le16(ICE_AQC_SW_RULES_T_PRUNE_LIST_CLEAR);
	s_rule->index = cpu_to_le16(vsi_list_id);
	s_rule->number_vsi = cpu_to_le16(num_vsi);
	s_rule->vsi[0] = cpu_to_le16(vsi_num);

	if (ice_aq_sw_rules(&event_pf->hw, (struct ice_aqc_sw_rules *)s_rule,
			    rule_buf_sz, 1, ice_aqc_opc_update_sw_rules, NULL))
		dev_warn(dev, "Error clearing VSI prune list\n");

	kfree(s_rule);
}

/**
 * ice_lag_init_feature_support_flag - Check for package and NVM support for LAG
 * @pf: PF struct
 */
static void ice_lag_init_feature_support_flag(struct ice_pf *pf)
{
	struct ice_hw_common_caps *caps;

	caps = &pf->hw.dev_caps.common_cap;
	if (caps->roce_lag)
		ice_set_feature_support(pf, ICE_F_ROCE_LAG);
	else
		ice_clear_feature_support(pf, ICE_F_ROCE_LAG);

	if (caps->sriov_lag && ice_pkg_has_lport_extract(&pf->hw))
		ice_set_feature_support(pf, ICE_F_SRIOV_LAG);
	else
		ice_clear_feature_support(pf, ICE_F_SRIOV_LAG);
}

/**
 * ice_lag_changeupper_event - handle LAG changeupper event
 * @lag: LAG info struct
 * @ptr: opaque pointer data
 */
static void ice_lag_changeupper_event(struct ice_lag *lag, void *ptr)
{
	struct netdev_notifier_changeupper_info *info;
	struct ice_lag *primary_lag;
	struct net_device *netdev;

	info = ptr;
	netdev = netdev_notifier_info_to_dev(ptr);

	/* not for this netdev */
	if (netdev != lag->netdev)
		return;

	primary_lag = ice_lag_find_primary(lag);
	if (info->linking) {
		lag->upper_netdev = info->upper_dev;
		/* If there is not already a primary interface in the LAG,
		 * then mark this one as primary.
		 */
		if (!primary_lag) {
			lag->primary = true;
			/* Configure primary's SWID to be shared */
			ice_lag_primary_swid(lag, true);
			primary_lag = lag;
		} else {
			u16 swid;

			swid = primary_lag->pf->hw.port_info->sw_id;
			ice_lag_set_swid(swid, lag, true);
			ice_lag_add_prune_list(primary_lag, lag->pf);
			ice_lag_cfg_drop_fltr(lag, true);
		}
		/* add filter for primary control packets */
		ice_lag_cfg_cp_fltr(lag, true);
	} else {
		if (!primary_lag && lag->primary)
			primary_lag = lag;

		if (!lag->primary) {
			ice_lag_set_swid(0, lag, false);
		} else {
			if (primary_lag && lag->primary) {
				ice_lag_primary_swid(lag, false);
				ice_lag_del_prune_list(primary_lag, lag->pf);
			}
		}
		/* remove filter for control packets */
		ice_lag_cfg_cp_fltr(lag, false);
	}
}

/**
 * ice_lag_monitor_link - monitor interfaces entering/leaving the aggregate
 * @lag: lag info struct
 * @ptr: opaque data containing notifier event
 *
 * This function only operates after a primary has been set.
 */
static void ice_lag_monitor_link(struct ice_lag *lag, void *ptr)
{
	struct netdev_notifier_changeupper_info *info;
	struct ice_hw *prim_hw, *active_hw;
	struct net_device *event_netdev;
	struct ice_pf *pf;
	u8 prim_port;

	if (!lag->primary)
		return;

	event_netdev = netdev_notifier_info_to_dev(ptr);
	if (!netif_is_same_ice(lag->pf, event_netdev))
		return;

	pf = lag->pf;
	prim_hw = &pf->hw;
	prim_port = prim_hw->port_info->lport;

	info = (struct netdev_notifier_changeupper_info *)ptr;
	if (info->upper_dev != lag->upper_netdev)
		return;

	if (!info->linking) {
		/* Since there are only two interfaces allowed in SRIOV+LAG, if
		 * one port is leaving, then nodes need to be on primary
		 * interface.
		 */
		if (prim_port != lag->active_port &&
		    lag->active_port != ICE_LAG_INVALID_PORT) {
			active_hw = ice_lag_find_hw_by_lport(lag,
							     lag->active_port);
			ice_lag_reclaim_vf_nodes(lag, active_hw);
			lag->active_port = ICE_LAG_INVALID_PORT;
		}
	}
}

/**
 * ice_lag_monitor_active - main PF keep track of which port is active
 * @lag: lag info struct
 * @ptr: opaque data containing notifier event
 *
 * This function is for the primary PF to monitor changes in which port is
 * active and handle changes for SRIOV VF functionality
 */
static void ice_lag_monitor_active(struct ice_lag *lag, void *ptr)
{
	struct net_device *event_netdev, *event_upper;
	struct netdev_notifier_bonding_info *info;
	struct netdev_bonding_info *bonding_info;
	struct ice_netdev_priv *event_np;
	struct ice_pf *pf, *event_pf;
	u8 prim_port, event_port;

	if (!lag->primary)
		return;

	pf = lag->pf;
	if (!pf)
		return;

	event_netdev = netdev_notifier_info_to_dev(ptr);
	rcu_read_lock();
	event_upper = netdev_master_upper_dev_get_rcu(event_netdev);
	rcu_read_unlock();
	if (!netif_is_ice(event_netdev) || event_upper != lag->upper_netdev)
		return;

	event_np = netdev_priv(event_netdev);
	event_pf = event_np->vsi->back;
	event_port = event_pf->hw.port_info->lport;
	prim_port = pf->hw.port_info->lport;

	info = (struct netdev_notifier_bonding_info *)ptr;
	bonding_info = &info->bonding_info;

	if (!bonding_info->slave.state) {
		/* if no port is currently active, then nodes and filters exist
		 * on primary port, check if we need to move them
		 */
		if (lag->active_port == ICE_LAG_INVALID_PORT) {
			if (event_port != prim_port)
				ice_lag_move_vf_nodes(lag, prim_port,
						      event_port);
			lag->active_port = event_port;
			ice_lag_config_eswitch(lag, event_netdev);
			return;
		}

		/* active port is already set and is current event port */
		if (lag->active_port == event_port)
			return;
		/* new active port */
		ice_lag_move_vf_nodes(lag, lag->active_port, event_port);
		lag->active_port = event_port;
		ice_lag_config_eswitch(lag, event_netdev);
	} else {
		/* port not set as currently active (e.g. new active port
		 * has already claimed the nodes and filters
		 */
		if (lag->active_port != event_port)
			return;
		/* This is the case when neither port is active (both link down)
		 * Link down on the bond - set active port to invalid and move
		 * nodes and filters back to primary if not already there
		 */
		if (event_port != prim_port)
			ice_lag_move_vf_nodes(lag, event_port, prim_port);
		lag->active_port = ICE_LAG_INVALID_PORT;
	}
}

/**
 * ice_lag_chk_comp - evaluate bonded interface for feature support
 * @lag: lag info struct
 * @ptr: opaque data for netdev event info
 */
static bool
ice_lag_chk_comp(struct ice_lag *lag, void *ptr)
{
	struct net_device *event_netdev, *event_upper;
	struct netdev_notifier_bonding_info *info;
	struct netdev_bonding_info *bonding_info;
	struct list_head *tmp;
	struct device *dev;
	int count = 0;

	if (!lag->primary)
		return true;

	event_netdev = netdev_notifier_info_to_dev(ptr);
	rcu_read_lock();
	event_upper = netdev_master_upper_dev_get_rcu(event_netdev);
	rcu_read_unlock();
	if (event_upper != lag->upper_netdev)
		return true;

	dev = ice_pf_to_dev(lag->pf);

	/* only supporting switchdev mode for SRIOV VF LAG.
	 * primary interface has to be in switchdev mode
	 */
	if (!ice_is_switchdev_running(lag->pf)) {
		dev_info(dev, "Primary interface not in switchdev mode - VF LAG disabled\n");
		return false;
	}

	info = (struct netdev_notifier_bonding_info *)ptr;
	bonding_info = &info->bonding_info;
	lag->bond_mode = bonding_info->master.bond_mode;
	if (lag->bond_mode != BOND_MODE_ACTIVEBACKUP) {
		dev_info(dev, "Bond Mode not ACTIVE-BACKUP - VF LAG disabled\n");
		return false;
	}

	list_for_each(tmp, lag->netdev_head) {
		struct ice_dcbx_cfg *dcb_cfg, *peer_dcb_cfg;
		struct ice_lag_netdev_list *entry;
		struct ice_netdev_priv *peer_np;
		struct net_device *peer_netdev;
		struct ice_vsi *vsi, *peer_vsi;
		struct ice_pf *peer_pf;

		entry = list_entry(tmp, struct ice_lag_netdev_list, node);
		peer_netdev = entry->netdev;
		if (!netif_is_ice(peer_netdev)) {
			dev_info(dev, "Found %s non-ice netdev in LAG - VF LAG disabled\n",
				 netdev_name(peer_netdev));
			return false;
		}

		count++;
		if (count > 2) {
			dev_info(dev, "Found more than two netdevs in LAG - VF LAG disabled\n");
			return false;
		}

		peer_np = netdev_priv(peer_netdev);
		vsi = ice_get_main_vsi(lag->pf);
		peer_vsi = peer_np->vsi;
		if (lag->pf->pdev->bus != peer_vsi->back->pdev->bus ||
		    lag->pf->pdev->slot != peer_vsi->back->pdev->slot) {
			dev_info(dev, "Found %s on different device in LAG - VF LAG disabled\n",
				 netdev_name(peer_netdev));
			return false;
		}

		dcb_cfg = &vsi->port_info->qos_cfg.local_dcbx_cfg;
		peer_dcb_cfg = &peer_vsi->port_info->qos_cfg.local_dcbx_cfg;
		if (memcmp(dcb_cfg, peer_dcb_cfg,
			   sizeof(struct ice_dcbx_cfg))) {
			dev_info(dev, "Found %s with different DCB in LAG - VF LAG disabled\n",
				 netdev_name(peer_netdev));
			return false;
		}

		peer_pf = peer_vsi->back;
		if (test_bit(ICE_FLAG_FW_LLDP_AGENT, peer_pf->flags)) {
			dev_warn(dev, "Found %s with FW LLDP agent active - VF LAG disabled\n",
				 netdev_name(peer_netdev));
			return false;
		}
	}

	return true;
}

/**
 * ice_lag_unregister - handle netdev unregister events
 * @lag: LAG info struct
 * @event_netdev: netdev struct for target of notifier event
 */
static void
ice_lag_unregister(struct ice_lag *lag, struct net_device *event_netdev)
{
	struct ice_netdev_priv *np;
	struct ice_pf *event_pf;
	struct ice_lag *p_lag;

	p_lag = ice_lag_find_primary(lag);
	np = netdev_priv(event_netdev);
	event_pf = np->vsi->back;

	if (p_lag) {
		if (p_lag->active_port != p_lag->pf->hw.port_info->lport &&
		    p_lag->active_port != ICE_LAG_INVALID_PORT) {
			struct ice_hw *active_hw;

			active_hw = ice_lag_find_hw_by_lport(lag,
							     p_lag->active_port);
			if (active_hw)
				ice_lag_reclaim_vf_nodes(p_lag, active_hw);
			lag->active_port = ICE_LAG_INVALID_PORT;
		}
	}

	/* primary processing for primary */
	if (lag->primary && lag->netdev == event_netdev)
		ice_lag_primary_swid(lag, false);

	/* primary processing for secondary */
	if (lag->primary && lag->netdev != event_netdev)
		ice_lag_del_prune_list(lag, event_pf);

	/* secondary processing for secondary */
	if (!lag->primary && lag->netdev == event_netdev)
		ice_lag_set_swid(0, lag, false);
}

/**
 * ice_lag_monitor_rdma - set and clear rdma functionality
 * @lag: pointer to lag struct
 * @ptr: opaque data for netdev event info
 */
static void
ice_lag_monitor_rdma(struct ice_lag *lag, void *ptr)
{
	struct netdev_notifier_changeupper_info *info;
	struct net_device *netdev;

	info = ptr;
	netdev = netdev_notifier_info_to_dev(ptr);

	if (netdev != lag->netdev)
		return;

	if (info->linking)
		ice_clear_rdma_cap(lag->pf);
	else
		ice_set_rdma_cap(lag->pf);
}

/**
 * ice_lag_chk_disabled_bond - monitor interfaces entering/leaving disabled bond
 * @lag: lag info struct
 * @ptr: opaque data containing event
 *
 * as interfaces enter a bond - determine if the bond is currently
 * SRIOV LAG compliant and flag if not.  As interfaces leave the
 * bond, reset their compliant status.
 */
static void ice_lag_chk_disabled_bond(struct ice_lag *lag, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);
	struct netdev_notifier_changeupper_info *info = ptr;
	struct ice_lag *prim_lag;

	if (netdev != lag->netdev)
		return;

	if (info->linking) {
		prim_lag = ice_lag_find_primary(lag);
		if (prim_lag &&
		    !ice_is_feature_supported(prim_lag->pf, ICE_F_SRIOV_LAG)) {
			ice_clear_feature_support(lag->pf, ICE_F_SRIOV_LAG);
			netdev_info(netdev, "Interface added to non-compliant SRIOV LAG aggregate\n");
		}
	} else {
		ice_lag_init_feature_support_flag(lag->pf);
	}
}

/**
 * ice_lag_disable_sriov_bond - set members of bond as not supporting SRIOV LAG
 * @lag: primary interfaces lag struct
 */
static void ice_lag_disable_sriov_bond(struct ice_lag *lag)
{
	struct ice_netdev_priv *np;
	struct ice_pf *pf;

	np = netdev_priv(lag->netdev);
	pf = np->vsi->back;
	ice_clear_feature_support(pf, ICE_F_SRIOV_LAG);
}

/**
 * ice_lag_process_event - process a task assigned to the lag_wq
 * @work: pointer to work_struct
 */
static void ice_lag_process_event(struct work_struct *work)
{
	struct netdev_notifier_changeupper_info *info;
	struct ice_lag_work *lag_work;
	struct net_device *netdev;
	struct list_head *tmp, *n;
	struct ice_pf *pf;

	lag_work = container_of(work, struct ice_lag_work, lag_task);
	pf = lag_work->lag->pf;

	mutex_lock(&pf->lag_mutex);
	lag_work->lag->netdev_head = &lag_work->netdev_list.node;

	switch (lag_work->event) {
	case NETDEV_CHANGEUPPER:
		info = &lag_work->info.changeupper_info;
		ice_lag_chk_disabled_bond(lag_work->lag, info);
		if (ice_is_feature_supported(pf, ICE_F_SRIOV_LAG)) {
			ice_lag_monitor_link(lag_work->lag, info);
			ice_lag_changeupper_event(lag_work->lag, info);
			ice_lag_link_unlink(lag_work->lag, info);
		}
		ice_lag_monitor_rdma(lag_work->lag, info);
		break;
	case NETDEV_BONDING_INFO:
		if (ice_is_feature_supported(pf, ICE_F_SRIOV_LAG)) {
			if (!ice_lag_chk_comp(lag_work->lag,
					      &lag_work->info.bonding_info)) {
				netdev = lag_work->info.bonding_info.info.dev;
				ice_lag_disable_sriov_bond(lag_work->lag);
				ice_lag_unregister(lag_work->lag, netdev);
				goto lag_cleanup;
			}
			ice_lag_monitor_active(lag_work->lag,
					       &lag_work->info.bonding_info);
			ice_lag_cfg_pf_fltrs(lag_work->lag,
					     &lag_work->info.bonding_info);
		}
		ice_lag_info_event(lag_work->lag, &lag_work->info.bonding_info);
		break;
	case NETDEV_UNREGISTER:
		if (ice_is_feature_supported(pf, ICE_F_SRIOV_LAG)) {
			netdev = lag_work->info.bonding_info.info.dev;
			if ((netdev == lag_work->lag->netdev ||
			     lag_work->lag->primary) && lag_work->lag->bonded)
				ice_lag_unregister(lag_work->lag, netdev);
		}
		break;
	default:
		break;
	}

lag_cleanup:
	/* cleanup resources allocated for this work item */
	list_for_each_safe(tmp, n, &lag_work->netdev_list.node) {
		struct ice_lag_netdev_list *entry;

		entry = list_entry(tmp, struct ice_lag_netdev_list, node);
		list_del(&entry->node);
		kfree(entry);
	}
	lag_work->lag->netdev_head = NULL;

	mutex_unlock(&pf->lag_mutex);

	kfree(lag_work);
}

/**
 * ice_lag_event_handler - handle LAG events from netdev
 * @notif_blk: notifier block registered by this netdev
 * @event: event type
 * @ptr: opaque data containing notifier event
 */
static int
ice_lag_event_handler(struct notifier_block *notif_blk, unsigned long event,
		      void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);
	struct net_device *upper_netdev;
	struct ice_lag_work *lag_work;
	struct ice_lag *lag;

	if (!netif_is_ice(netdev))
		return NOTIFY_DONE;

	if (event != NETDEV_CHANGEUPPER && event != NETDEV_BONDING_INFO &&
	    event != NETDEV_UNREGISTER)
		return NOTIFY_DONE;

	if (!(netdev->priv_flags & IFF_BONDING))
		return NOTIFY_DONE;

	lag = container_of(notif_blk, struct ice_lag, notif_block);
	if (!lag->netdev)
		return NOTIFY_DONE;

	if (!net_eq(dev_net(netdev), &init_net))
		return NOTIFY_DONE;

	/* This memory will be freed at the end of ice_lag_process_event */
	lag_work = kzalloc(sizeof(*lag_work), GFP_KERNEL);
	if (!lag_work)
		return -ENOMEM;

	lag_work->event_netdev = netdev;
	lag_work->lag = lag;
	lag_work->event = event;
	if (event == NETDEV_CHANGEUPPER) {
		struct netdev_notifier_changeupper_info *info;

		info = ptr;
		upper_netdev = info->upper_dev;
	} else {
		upper_netdev = netdev_master_upper_dev_get(netdev);
	}

	INIT_LIST_HEAD(&lag_work->netdev_list.node);
	if (upper_netdev) {
		struct ice_lag_netdev_list *nd_list;
		struct net_device *tmp_nd;

		rcu_read_lock();
		for_each_netdev_in_bond_rcu(upper_netdev, tmp_nd) {
			nd_list = kzalloc(sizeof(*nd_list), GFP_ATOMIC);
			if (!nd_list)
				break;

			nd_list->netdev = tmp_nd;
			list_add(&nd_list->node, &lag_work->netdev_list.node);
		}
		rcu_read_unlock();
	}

	switch (event) {
	case NETDEV_CHANGEUPPER:
		lag_work->info.changeupper_info =
			*((struct netdev_notifier_changeupper_info *)ptr);
		break;
	case NETDEV_BONDING_INFO:
		lag_work->info.bonding_info =
			*((struct netdev_notifier_bonding_info *)ptr);
		break;
	default:
		lag_work->info.notifier_info =
			*((struct netdev_notifier_info *)ptr);
		break;
	}

	INIT_WORK(&lag_work->lag_task, ice_lag_process_event);
	queue_work(ice_lag_wq, &lag_work->lag_task);

	return NOTIFY_DONE;
}

/**
 * ice_register_lag_handler - register LAG handler on netdev
 * @lag: LAG struct
 */
static int ice_register_lag_handler(struct ice_lag *lag)
{
	struct device *dev = ice_pf_to_dev(lag->pf);
	struct notifier_block *notif_blk;

	notif_blk = &lag->notif_block;

	if (!notif_blk->notifier_call) {
		notif_blk->notifier_call = ice_lag_event_handler;
		if (register_netdevice_notifier(notif_blk)) {
			notif_blk->notifier_call = NULL;
			dev_err(dev, "FAIL register LAG event handler!\n");
			return -EINVAL;
		}
		dev_dbg(dev, "LAG event handler registered\n");
	}
	return 0;
}

/**
 * ice_unregister_lag_handler - unregister LAG handler on netdev
 * @lag: LAG struct
 */
static void ice_unregister_lag_handler(struct ice_lag *lag)
{
	struct device *dev = ice_pf_to_dev(lag->pf);
	struct notifier_block *notif_blk;

	notif_blk = &lag->notif_block;
	if (notif_blk->notifier_call) {
		unregister_netdevice_notifier(notif_blk);
		dev_dbg(dev, "LAG event handler unregistered\n");
	}
}

/**
 * ice_create_lag_recipe
 * @hw: pointer to HW struct
 * @rid: pointer to u16 to pass back recipe index
 * @base_recipe: recipe to base the new recipe on
 * @prio: priority for new recipe
 *
 * function returns 0 on error
 */
static int ice_create_lag_recipe(struct ice_hw *hw, u16 *rid,
				 const u8 *base_recipe, u8 prio)
{
	struct ice_aqc_recipe_data_elem *new_rcp;
	int err;

	err = ice_alloc_recipe(hw, rid);
	if (err)
		return err;

	new_rcp = kzalloc(ICE_RECIPE_LEN * ICE_MAX_NUM_RECIPES, GFP_KERNEL);
	if (!new_rcp)
		return -ENOMEM;

	memcpy(new_rcp, base_recipe, ICE_RECIPE_LEN);
	new_rcp->content.act_ctrl_fwd_priority = prio;
	new_rcp->content.rid = *rid | ICE_AQ_RECIPE_ID_IS_ROOT;
	new_rcp->recipe_indx = *rid;
	bitmap_zero((unsigned long *)new_rcp->recipe_bitmap,
		    ICE_MAX_NUM_RECIPES);
	set_bit(*rid, (unsigned long *)new_rcp->recipe_bitmap);

	err = ice_aq_add_recipe(hw, new_rcp, 1, NULL);
	if (err)
		*rid = 0;

	kfree(new_rcp);
	return err;
}

/**
 * ice_lag_move_vf_nodes_tc_sync - move a VF's nodes for a tc during reset
 * @lag: primary interfaces lag struct
 * @dest_hw: HW struct for destination's interface
 * @vsi_num: VSI index in PF space
 * @tc: traffic class to move
 */
static void
ice_lag_move_vf_nodes_tc_sync(struct ice_lag *lag, struct ice_hw *dest_hw,
			      u16 vsi_num, u8 tc)
{
	DEFINE_RAW_FLEX(struct ice_aqc_move_elem, buf, teid, 1);
	struct device *dev = ice_pf_to_dev(lag->pf);
	u16 numq, valq, num_moved, qbuf_size;
	u16 buf_size = __struct_size(buf);
	struct ice_aqc_cfg_txqs_buf *qbuf;
	struct ice_sched_node *n_prt;
	__le32 teid, parent_teid;
	struct ice_vsi_ctx *ctx;
	struct ice_hw *hw;
	u32 tmp_teid;

	hw = &lag->pf->hw;
	ctx = ice_get_vsi_ctx(hw, vsi_num);
	if (!ctx) {
		dev_warn(dev, "LAG rebuild failed after reset due to VSI Context failure\n");
		return;
	}

	if (!ctx->sched.vsi_node[tc])
		return;

	numq = ctx->num_lan_q_entries[tc];
	teid = ctx->sched.vsi_node[tc]->info.node_teid;
	tmp_teid = le32_to_cpu(teid);
	parent_teid = ctx->sched.vsi_node[tc]->info.parent_teid;

	if (!tmp_teid || !numq)
		return;

	if (ice_sched_suspend_resume_elems(hw, 1, &tmp_teid, true))
		dev_dbg(dev, "Problem suspending traffic during reset rebuild\n");

	/* reconfig queues for new port */
	qbuf_size = struct_size(qbuf, queue_info, numq);
	qbuf = kzalloc(qbuf_size, GFP_KERNEL);
	if (!qbuf) {
		dev_warn(dev, "Failure allocating VF queue recfg buffer for reset rebuild\n");
		goto resume_sync;
	}

	/* add the per queue info for the reconfigure command buffer */
	valq = ice_lag_qbuf_recfg(hw, qbuf, vsi_num, numq, tc);
	if (!valq) {
		dev_warn(dev, "Failure to reconfig queues for LAG reset rebuild\n");
		goto sync_none;
	}

	if (ice_aq_cfg_lan_txq(hw, qbuf, qbuf_size, numq, hw->port_info->lport,
			       dest_hw->port_info->lport, NULL)) {
		dev_warn(dev, "Failure to configure queues for LAG reset rebuild\n");
		goto sync_qerr;
	}

sync_none:
	kfree(qbuf);

	/* find parent in destination tree */
	n_prt = ice_lag_get_sched_parent(dest_hw, tc);
	if (!n_prt)
		goto resume_sync;

	/* Move node to new parent */
	buf->hdr.src_parent_teid = parent_teid;
	buf->hdr.dest_parent_teid = n_prt->info.node_teid;
	buf->hdr.num_elems = cpu_to_le16(1);
	buf->hdr.mode = ICE_AQC_MOVE_ELEM_MODE_KEEP_OWN;
	buf->teid[0] = teid;

	if (ice_aq_move_sched_elems(&lag->pf->hw, buf, buf_size, &num_moved))
		dev_warn(dev, "Failure to move VF nodes for LAG reset rebuild\n");
	else
		ice_sched_update_parent(n_prt, ctx->sched.vsi_node[tc]);

	goto resume_sync;

sync_qerr:
	kfree(qbuf);

resume_sync:
	if (ice_sched_suspend_resume_elems(hw, 1, &tmp_teid, false))
		dev_warn(dev, "Problem restarting traffic for LAG node reset rebuild\n");
}

/**
 * ice_lag_move_vf_nodes_sync - move vf nodes to active interface
 * @lag: primary interfaces lag struct
 * @dest_hw: lport value for currently active port
 *
 * This function is used in a reset context, outside of event handling,
 * to move the VF nodes to the secondary interface when that interface
 * is the active interface during a reset rebuild
 */
static void
ice_lag_move_vf_nodes_sync(struct ice_lag *lag, struct ice_hw *dest_hw)
{
	struct ice_pf *pf;
	int i, tc;

	if (!lag->primary || !dest_hw)
		return;

	pf = lag->pf;
	ice_for_each_vsi(pf, i)
		if (pf->vsi[i] && pf->vsi[i]->type == ICE_VSI_VF)
			ice_for_each_traffic_class(tc)
				ice_lag_move_vf_nodes_tc_sync(lag, dest_hw, i,
							      tc);
}

/**
 * ice_init_lag - initialize support for LAG
 * @pf: PF struct
 *
 * Alloc memory for LAG structs and initialize the elements.
 * Memory will be freed in ice_deinit_lag
 */
int ice_init_lag(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_lag *lag;
	struct ice_vsi *vsi;
	u64 recipe_bits = 0;
	int n, err;

	ice_lag_init_feature_support_flag(pf);
	if (!ice_is_feature_supported(pf, ICE_F_SRIOV_LAG))
		return 0;

	pf->lag = kzalloc(sizeof(*lag), GFP_KERNEL);
	if (!pf->lag)
		return -ENOMEM;
	lag = pf->lag;

	vsi = ice_get_main_vsi(pf);
	if (!vsi) {
		dev_err(dev, "couldn't get main vsi, link aggregation init fail\n");
		err = -EIO;
		goto lag_error;
	}

	lag->pf = pf;
	lag->netdev = vsi->netdev;
	lag->role = ICE_LAG_NONE;
	lag->active_port = ICE_LAG_INVALID_PORT;
	lag->bonded = false;
	lag->upper_netdev = NULL;
	lag->notif_block.notifier_call = NULL;

	err = ice_register_lag_handler(lag);
	if (err) {
		dev_warn(dev, "INIT LAG: Failed to register event handler\n");
		goto lag_error;
	}

	err = ice_create_lag_recipe(&pf->hw, &lag->pf_recipe,
				    ice_dflt_vsi_rcp, 1);
	if (err)
		goto lag_error;

	err = ice_create_lag_recipe(&pf->hw, &lag->lport_recipe,
				    ice_lport_rcp, 3);
	if (err)
		goto free_rcp_res;

	/* associate recipes to profiles */
	for (n = 0; n < ICE_PROFID_IPV6_GTPU_IPV6_TCP_INNER; n++) {
		err = ice_aq_get_recipe_to_profile(&pf->hw, n,
						   &recipe_bits, NULL);
		if (err)
			continue;

		if (recipe_bits & BIT(ICE_SW_LKUP_DFLT)) {
			recipe_bits |= BIT(lag->pf_recipe) |
				       BIT(lag->lport_recipe);
			ice_aq_map_recipe_to_profile(&pf->hw, n,
						     recipe_bits, NULL);
		}
	}

	ice_display_lag_info(lag);

	dev_dbg(dev, "INIT LAG complete\n");
	return 0;

free_rcp_res:
	ice_free_hw_res(&pf->hw, ICE_AQC_RES_TYPE_RECIPE, 1,
			&pf->lag->pf_recipe);
lag_error:
	kfree(lag);
	pf->lag = NULL;
	return err;
}

/**
 * ice_deinit_lag - Clean up LAG
 * @pf: PF struct
 *
 * Clean up kernel LAG info and free memory
 * This function is meant to only be called on driver remove/shutdown
 */
void ice_deinit_lag(struct ice_pf *pf)
{
	struct ice_lag *lag;

	lag = pf->lag;

	if (!lag)
		return;

	if (lag->pf)
		ice_unregister_lag_handler(lag);

	flush_workqueue(ice_lag_wq);

	ice_free_hw_res(&pf->hw, ICE_AQC_RES_TYPE_RECIPE, 1,
			&pf->lag->pf_recipe);
	ice_free_hw_res(&pf->hw, ICE_AQC_RES_TYPE_RECIPE, 1,
			&pf->lag->lport_recipe);

	kfree(lag);

	pf->lag = NULL;
}

/**
 * ice_lag_rebuild - rebuild lag resources after reset
 * @pf: pointer to local pf struct
 *
 * PF resets are promoted to CORER resets when interface in an aggregate.  This
 * means that we need to rebuild the PF resources for the interface.  Since
 * this will happen outside the normal event processing, need to acquire the lag
 * lock.
 *
 * This function will also evaluate the VF resources if this is the primary
 * interface.
 */
void ice_lag_rebuild(struct ice_pf *pf)
{
	struct ice_lag_netdev_list ndlist;
	struct ice_lag *lag, *prim_lag;
	u8 act_port, loc_port;

	if (!pf->lag || !pf->lag->bonded)
		return;

	mutex_lock(&pf->lag_mutex);

	lag = pf->lag;
	if (lag->primary) {
		prim_lag = lag;
	} else {
		ice_lag_build_netdev_list(lag, &ndlist);
		prim_lag = ice_lag_find_primary(lag);
	}

	if (!prim_lag) {
		dev_dbg(ice_pf_to_dev(pf), "No primary interface in aggregate, can't rebuild\n");
		goto lag_rebuild_out;
	}

	act_port = prim_lag->active_port;
	loc_port = lag->pf->hw.port_info->lport;

	/* configure SWID for this port */
	if (lag->primary) {
		ice_lag_primary_swid(lag, true);
	} else {
		ice_lag_set_swid(prim_lag->pf->hw.port_info->sw_id, lag, true);
		ice_lag_add_prune_list(prim_lag, pf);
		if (act_port == loc_port)
			ice_lag_move_vf_nodes_sync(prim_lag, &pf->hw);
	}

	ice_lag_cfg_cp_fltr(lag, true);

	if (lag->pf_rx_rule_id)
		if (ice_lag_cfg_dflt_fltr(lag, true))
			dev_err(ice_pf_to_dev(pf), "Error adding default VSI rule in rebuild\n");

	ice_clear_rdma_cap(pf);
lag_rebuild_out:
	ice_lag_destroy_netdev_list(lag, &ndlist);
	mutex_unlock(&pf->lag_mutex);
}

/**
 * ice_lag_is_switchdev_running
 * @pf: pointer to PF structure
 *
 * Check if switchdev is running on any of the interfaces connected to lag.
 */
bool ice_lag_is_switchdev_running(struct ice_pf *pf)
{
	struct ice_lag *lag = pf->lag;
	struct net_device *tmp_nd;

	if (!ice_is_feature_supported(pf, ICE_F_SRIOV_LAG) || !lag)
		return false;

	rcu_read_lock();
	for_each_netdev_in_bond_rcu(lag->upper_netdev, tmp_nd) {
		struct ice_netdev_priv *priv = netdev_priv(tmp_nd);

		if (!netif_is_ice(tmp_nd) || !priv || !priv->vsi ||
		    !priv->vsi->back)
			continue;

		if (ice_is_switchdev_running(priv->vsi->back)) {
			rcu_read_unlock();
			return true;
		}
	}
	rcu_read_unlock();

	return false;
}
