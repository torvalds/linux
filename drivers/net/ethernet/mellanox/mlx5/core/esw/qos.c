// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "eswitch.h"
#include "lib/mlx5.h"
#include "esw/qos.h"
#include "en/port.h"
#define CREATE_TRACE_POINTS
#include "diag/qos_tracepoint.h"

/* Minimum supported BW share value by the HW is 1 Mbit/sec */
#define MLX5_MIN_BW_SHARE 1

/* Holds rate nodes associated with an E-Switch. */
struct mlx5_qos_domain {
	/* Serializes access to all qos changes in the qos domain. */
	struct mutex lock;
	/* List of all mlx5_esw_sched_nodes. */
	struct list_head nodes;
};

static void esw_qos_lock(struct mlx5_eswitch *esw)
{
	mutex_lock(&esw->qos.domain->lock);
}

static void esw_qos_unlock(struct mlx5_eswitch *esw)
{
	mutex_unlock(&esw->qos.domain->lock);
}

static void esw_assert_qos_lock_held(struct mlx5_eswitch *esw)
{
	lockdep_assert_held(&esw->qos.domain->lock);
}

static struct mlx5_qos_domain *esw_qos_domain_alloc(void)
{
	struct mlx5_qos_domain *qos_domain;

	qos_domain = kzalloc(sizeof(*qos_domain), GFP_KERNEL);
	if (!qos_domain)
		return NULL;

	mutex_init(&qos_domain->lock);
	INIT_LIST_HEAD(&qos_domain->nodes);

	return qos_domain;
}

static int esw_qos_domain_init(struct mlx5_eswitch *esw)
{
	esw->qos.domain = esw_qos_domain_alloc();

	return esw->qos.domain ? 0 : -ENOMEM;
}

static void esw_qos_domain_release(struct mlx5_eswitch *esw)
{
	kfree(esw->qos.domain);
	esw->qos.domain = NULL;
}

enum sched_node_type {
	SCHED_NODE_TYPE_VPORTS_TSAR,
	SCHED_NODE_TYPE_VPORT,
	SCHED_NODE_TYPE_TC_ARBITER_TSAR,
	SCHED_NODE_TYPE_RATE_LIMITER,
	SCHED_NODE_TYPE_VPORT_TC,
	SCHED_NODE_TYPE_VPORTS_TC_TSAR,
};

static const char * const sched_node_type_str[] = {
	[SCHED_NODE_TYPE_VPORTS_TSAR] = "vports TSAR",
	[SCHED_NODE_TYPE_VPORT] = "vport",
	[SCHED_NODE_TYPE_TC_ARBITER_TSAR] = "TC Arbiter TSAR",
	[SCHED_NODE_TYPE_RATE_LIMITER] = "Rate Limiter",
	[SCHED_NODE_TYPE_VPORT_TC] = "vport TC",
	[SCHED_NODE_TYPE_VPORTS_TC_TSAR] = "vports TC TSAR",
};

struct mlx5_esw_sched_node {
	u32 ix;
	/* Bandwidth parameters. */
	u32 max_rate;
	u32 min_rate;
	/* A computed value indicating relative min_rate between node's children. */
	u32 bw_share;
	/* The parent node in the rate hierarchy. */
	struct mlx5_esw_sched_node *parent;
	/* Entry in the parent node's children list. */
	struct list_head entry;
	/* The type of this node in the rate hierarchy. */
	enum sched_node_type type;
	/* The eswitch this node belongs to. */
	struct mlx5_eswitch *esw;
	/* The children nodes of this node, empty list for leaf nodes. */
	struct list_head children;
	/* Valid only if this node is associated with a vport. */
	struct mlx5_vport *vport;
	/* Level in the hierarchy. The root node level is 1. */
	u8 level;
	/* Valid only when this node represents a traffic class. */
	u8 tc;
	/* Valid only for a TC arbiter node or vport TC arbiter. */
	u32 tc_bw[DEVLINK_RATE_TCS_MAX];
};

static void esw_qos_node_attach_to_parent(struct mlx5_esw_sched_node *node)
{
	if (!node->parent) {
		/* Root children are assigned a depth level of 2. */
		node->level = 2;
		list_add_tail(&node->entry, &node->esw->qos.domain->nodes);
	} else {
		node->level = node->parent->level + 1;
		list_add_tail(&node->entry, &node->parent->children);
	}
}

static int esw_qos_num_tcs(struct mlx5_core_dev *dev)
{
	int num_tcs = mlx5_max_tc(dev) + 1;

	return num_tcs < DEVLINK_RATE_TCS_MAX ? num_tcs : DEVLINK_RATE_TCS_MAX;
}

static void
esw_qos_node_set_parent(struct mlx5_esw_sched_node *node, struct mlx5_esw_sched_node *parent)
{
	list_del_init(&node->entry);
	node->parent = parent;
	if (parent)
		node->esw = parent->esw;
	esw_qos_node_attach_to_parent(node);
}

static void esw_qos_nodes_set_parent(struct list_head *nodes,
				     struct mlx5_esw_sched_node *parent)
{
	struct mlx5_esw_sched_node *node, *tmp;

	list_for_each_entry_safe(node, tmp, nodes, entry) {
		esw_qos_node_set_parent(node, parent);
		if (!list_empty(&node->children) &&
		    parent->type == SCHED_NODE_TYPE_TC_ARBITER_TSAR) {
			struct mlx5_esw_sched_node *child;

			list_for_each_entry(child, &node->children, entry) {
				struct mlx5_vport *vport = child->vport;

				if (vport)
					vport->qos.sched_node->parent = parent;
			}
		}
	}
}

void mlx5_esw_qos_vport_qos_free(struct mlx5_vport *vport)
{
	if (vport->qos.sched_nodes) {
		int num_tcs = esw_qos_num_tcs(vport->qos.sched_node->esw->dev);
		int i;

		for (i = 0; i < num_tcs; i++)
			kfree(vport->qos.sched_nodes[i]);
		kfree(vport->qos.sched_nodes);
	}

	kfree(vport->qos.sched_node);
	memset(&vport->qos, 0, sizeof(vport->qos));
}

u32 mlx5_esw_qos_vport_get_sched_elem_ix(const struct mlx5_vport *vport)
{
	if (!vport->qos.sched_node)
		return 0;

	return vport->qos.sched_node->ix;
}

struct mlx5_esw_sched_node *
mlx5_esw_qos_vport_get_parent(const struct mlx5_vport *vport)
{
	if (!vport->qos.sched_node)
		return NULL;

	return vport->qos.sched_node->parent;
}

static void esw_qos_sched_elem_warn(struct mlx5_esw_sched_node *node, int err, const char *op)
{
	switch (node->type) {
	case SCHED_NODE_TYPE_VPORTS_TC_TSAR:
		esw_warn(node->esw->dev,
			 "E-Switch %s %s scheduling element failed (tc=%d,err=%d)\n",
			 op, sched_node_type_str[node->type], node->tc, err);
		break;
	case SCHED_NODE_TYPE_VPORT_TC:
		esw_warn(node->esw->dev,
			 "E-Switch %s %s scheduling element failed (vport=%d,tc=%d,err=%d)\n",
			 op,
			 sched_node_type_str[node->type],
			 node->vport->vport, node->tc, err);
		break;
	case SCHED_NODE_TYPE_VPORT:
		esw_warn(node->esw->dev,
			 "E-Switch %s %s scheduling element failed (vport=%d,err=%d)\n",
			 op, sched_node_type_str[node->type], node->vport->vport, err);
		break;
	case SCHED_NODE_TYPE_RATE_LIMITER:
	case SCHED_NODE_TYPE_TC_ARBITER_TSAR:
	case SCHED_NODE_TYPE_VPORTS_TSAR:
		esw_warn(node->esw->dev,
			 "E-Switch %s %s scheduling element failed (err=%d)\n",
			 op, sched_node_type_str[node->type], err);
		break;
	default:
		esw_warn(node->esw->dev,
			 "E-Switch %s scheduling element failed (err=%d)\n",
			 op, err);
		break;
	}
}

static int esw_qos_node_create_sched_element(struct mlx5_esw_sched_node *node, void *ctx,
					     struct netlink_ext_ack *extack)
{
	int err;

	err = mlx5_create_scheduling_element_cmd(node->esw->dev, SCHEDULING_HIERARCHY_E_SWITCH, ctx,
						 &node->ix);
	if (err) {
		esw_qos_sched_elem_warn(node, err, "create");
		NL_SET_ERR_MSG_MOD(extack, "E-Switch create scheduling element failed");
	}

	return err;
}

static int esw_qos_node_destroy_sched_element(struct mlx5_esw_sched_node *node,
					      struct netlink_ext_ack *extack)
{
	int err;

	err = mlx5_destroy_scheduling_element_cmd(node->esw->dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  node->ix);
	if (err) {
		esw_qos_sched_elem_warn(node, err, "destroy");
		NL_SET_ERR_MSG_MOD(extack, "E-Switch destroying scheduling element failed.");
	}

	return err;
}

static int esw_qos_sched_elem_config(struct mlx5_esw_sched_node *node, u32 max_rate, u32 bw_share,
				     struct netlink_ext_ack *extack)
{
	u32 sched_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	struct mlx5_core_dev *dev = node->esw->dev;
	u32 bitmask = 0;
	int err;

	if (!MLX5_CAP_GEN(dev, qos) || !MLX5_CAP_QOS(dev, esw_scheduling))
		return -EOPNOTSUPP;

	if (bw_share && (!MLX5_CAP_QOS(dev, esw_bw_share) ||
			 MLX5_CAP_QOS(dev, max_tsar_bw_share) < MLX5_MIN_BW_SHARE))
		return -EOPNOTSUPP;

	if (node->max_rate == max_rate && node->bw_share == bw_share)
		return 0;

	if (node->max_rate != max_rate) {
		MLX5_SET(scheduling_context, sched_ctx, max_average_bw, max_rate);
		bitmask |= MODIFY_SCHEDULING_ELEMENT_IN_MODIFY_BITMASK_MAX_AVERAGE_BW;
	}
	if (node->bw_share != bw_share) {
		MLX5_SET(scheduling_context, sched_ctx, bw_share, bw_share);
		bitmask |= MODIFY_SCHEDULING_ELEMENT_IN_MODIFY_BITMASK_BW_SHARE;
	}

	err = mlx5_modify_scheduling_element_cmd(dev,
						 SCHEDULING_HIERARCHY_E_SWITCH,
						 sched_ctx,
						 node->ix,
						 bitmask);
	if (err) {
		esw_qos_sched_elem_warn(node, err, "modify");
		NL_SET_ERR_MSG_MOD(extack, "E-Switch modify scheduling element failed");

		return err;
	}

	node->max_rate = max_rate;
	node->bw_share = bw_share;
	if (node->type == SCHED_NODE_TYPE_VPORTS_TSAR)
		trace_mlx5_esw_node_qos_config(dev, node, node->ix, bw_share, max_rate);
	else if (node->type == SCHED_NODE_TYPE_VPORT)
		trace_mlx5_esw_vport_qos_config(dev, node->vport, bw_share, max_rate);

	return 0;
}

static int esw_qos_create_rate_limit_element(struct mlx5_esw_sched_node *node,
					     struct netlink_ext_ack *extack)
{
	u32 sched_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};

	if (!mlx5_qos_element_type_supported(
		node->esw->dev,
		SCHEDULING_CONTEXT_ELEMENT_TYPE_RATE_LIMIT,
		SCHEDULING_HIERARCHY_E_SWITCH))
		return -EOPNOTSUPP;

	MLX5_SET(scheduling_context, sched_ctx, max_average_bw, node->max_rate);
	MLX5_SET(scheduling_context, sched_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_RATE_LIMIT);

	return esw_qos_node_create_sched_element(node, sched_ctx, extack);
}

static u32 esw_qos_calculate_min_rate_divider(struct mlx5_eswitch *esw,
					      struct mlx5_esw_sched_node *parent)
{
	struct list_head *nodes = parent ? &parent->children : &esw->qos.domain->nodes;
	u32 fw_max_bw_share = MLX5_CAP_QOS(esw->dev, max_tsar_bw_share);
	struct mlx5_esw_sched_node *node;
	u32 max_guarantee = 0;

	/* Find max min_rate across all nodes.
	 * This will correspond to fw_max_bw_share in the final bw_share calculation.
	 */
	list_for_each_entry(node, nodes, entry) {
		if (node->esw == esw && node->ix != esw->qos.root_tsar_ix &&
		    node->min_rate > max_guarantee)
			max_guarantee = node->min_rate;
	}

	if (max_guarantee)
		return max_t(u32, max_guarantee / fw_max_bw_share, 1);

	/* If nodes max min_rate divider is 0 but their parent has bw_share
	 * configured, then set bw_share for nodes to minimal value.
	 */

	if (parent && parent->bw_share)
		return 1;

	/* If the node nodes has min_rate configured, a divider of 0 sets all
	 * nodes' bw_share to 0, effectively disabling min guarantees.
	 */
	return 0;
}

static u32 esw_qos_calc_bw_share(u32 value, u32 divider, u32 fw_max)
{
	if (!divider)
		return 0;
	return min_t(u32, fw_max,
		     max_t(u32,
			   DIV_ROUND_UP(value, divider), MLX5_MIN_BW_SHARE));
}

static void esw_qos_update_sched_node_bw_share(struct mlx5_esw_sched_node *node,
					       u32 divider,
					       struct netlink_ext_ack *extack)
{
	u32 fw_max_bw_share = MLX5_CAP_QOS(node->esw->dev, max_tsar_bw_share);
	u32 bw_share;

	bw_share = esw_qos_calc_bw_share(node->min_rate, divider, fw_max_bw_share);

	esw_qos_sched_elem_config(node, node->max_rate, bw_share, extack);
}

static void esw_qos_normalize_min_rate(struct mlx5_eswitch *esw,
				       struct mlx5_esw_sched_node *parent,
				       struct netlink_ext_ack *extack)
{
	struct list_head *nodes = parent ? &parent->children : &esw->qos.domain->nodes;
	u32 divider = esw_qos_calculate_min_rate_divider(esw, parent);
	struct mlx5_esw_sched_node *node;

	list_for_each_entry(node, nodes, entry) {
		if (node->esw != esw || node->ix == esw->qos.root_tsar_ix)
			continue;

		/* Vports TC TSARs don't have a minimum rate configured,
		 * so there's no need to update the bw_share on them.
		 */
		if (node->type != SCHED_NODE_TYPE_VPORTS_TC_TSAR) {
			esw_qos_update_sched_node_bw_share(node, divider,
							   extack);
		}

		if (list_empty(&node->children))
			continue;

		esw_qos_normalize_min_rate(node->esw, node, extack);
	}
}

static u32 esw_qos_calculate_tc_bw_divider(u32 *tc_bw)
{
	u32 total = 0;
	int i;

	for (i = 0; i < DEVLINK_RATE_TCS_MAX; i++)
		total += tc_bw[i];

	/* If total is zero, tc-bw config is disabled and we shouldn't reach
	 * here.
	 */
	return WARN_ON(!total) ? 1 : total;
}

static int esw_qos_set_node_min_rate(struct mlx5_esw_sched_node *node,
				     u32 min_rate, struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = node->esw;

	if (min_rate == node->min_rate)
		return 0;

	node->min_rate = min_rate;
	esw_qos_normalize_min_rate(esw, node->parent, extack);

	return 0;
}

static int
esw_qos_create_node_sched_elem(struct mlx5_core_dev *dev, u32 parent_element_id,
			       u32 max_rate, u32 bw_share, u32 *tsar_ix)
{
	u32 tsar_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	void *attr;

	if (!mlx5_qos_element_type_supported(dev,
					     SCHEDULING_CONTEXT_ELEMENT_TYPE_TSAR,
					     SCHEDULING_HIERARCHY_E_SWITCH) ||
	    !mlx5_qos_tsar_type_supported(dev,
					  TSAR_ELEMENT_TSAR_TYPE_DWRR,
					  SCHEDULING_HIERARCHY_E_SWITCH))
		return -EOPNOTSUPP;

	MLX5_SET(scheduling_context, tsar_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_TSAR);
	MLX5_SET(scheduling_context, tsar_ctx, parent_element_id,
		 parent_element_id);
	MLX5_SET(scheduling_context, tsar_ctx, max_average_bw, max_rate);
	MLX5_SET(scheduling_context, tsar_ctx, bw_share, bw_share);
	attr = MLX5_ADDR_OF(scheduling_context, tsar_ctx, element_attributes);
	MLX5_SET(tsar_element, attr, tsar_type, TSAR_ELEMENT_TSAR_TYPE_DWRR);

	return mlx5_create_scheduling_element_cmd(dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  tsar_ctx,
						  tsar_ix);
}

static int
esw_qos_vport_create_sched_element(struct mlx5_esw_sched_node *vport_node,
				   struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *parent = vport_node->parent;
	u32 sched_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	struct mlx5_core_dev *dev = vport_node->esw->dev;
	void *attr;

	if (!mlx5_qos_element_type_supported(
		dev,
		SCHEDULING_CONTEXT_ELEMENT_TYPE_VPORT,
		SCHEDULING_HIERARCHY_E_SWITCH))
		return -EOPNOTSUPP;

	MLX5_SET(scheduling_context, sched_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_VPORT);
	attr = MLX5_ADDR_OF(scheduling_context, sched_ctx, element_attributes);
	MLX5_SET(vport_element, attr, vport_number, vport_node->vport->vport);
	MLX5_SET(scheduling_context, sched_ctx, parent_element_id,
		 parent ? parent->ix : vport_node->esw->qos.root_tsar_ix);
	MLX5_SET(scheduling_context, sched_ctx, max_average_bw,
		 vport_node->max_rate);

	return esw_qos_node_create_sched_element(vport_node, sched_ctx, extack);
}

static int
esw_qos_vport_tc_create_sched_element(struct mlx5_esw_sched_node *vport_tc_node,
				      u32 rate_limit_elem_ix,
				      struct netlink_ext_ack *extack)
{
	u32 sched_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	struct mlx5_core_dev *dev = vport_tc_node->esw->dev;
	void *attr;

	if (!mlx5_qos_element_type_supported(
		dev,
		SCHEDULING_CONTEXT_ELEMENT_TYPE_VPORT_TC,
		SCHEDULING_HIERARCHY_E_SWITCH))
		return -EOPNOTSUPP;

	MLX5_SET(scheduling_context, sched_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_VPORT_TC);
	attr = MLX5_ADDR_OF(scheduling_context, sched_ctx, element_attributes);
	MLX5_SET(vport_tc_element, attr, vport_number,
		 vport_tc_node->vport->vport);
	MLX5_SET(vport_tc_element, attr, traffic_class, vport_tc_node->tc);
	MLX5_SET(scheduling_context, sched_ctx, max_bw_obj_id,
		 rate_limit_elem_ix);
	MLX5_SET(scheduling_context, sched_ctx, parent_element_id,
		 vport_tc_node->parent->ix);
	MLX5_SET(scheduling_context, sched_ctx, bw_share,
		 vport_tc_node->bw_share);

	return esw_qos_node_create_sched_element(vport_tc_node, sched_ctx,
						 extack);
}

static struct mlx5_esw_sched_node *
__esw_qos_alloc_node(struct mlx5_eswitch *esw, u32 tsar_ix, enum sched_node_type type,
		     struct mlx5_esw_sched_node *parent)
{
	struct mlx5_esw_sched_node *node;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return NULL;

	node->esw = esw;
	node->ix = tsar_ix;
	node->type = type;
	node->parent = parent;
	INIT_LIST_HEAD(&node->children);
	esw_qos_node_attach_to_parent(node);
	if (!parent) {
		/* The caller is responsible for inserting the node into the
		 * parent list if necessary. This function can also be used with
		 * a NULL parent, which doesn't necessarily indicate that it
		 * refers to the root scheduling element.
		 */
		list_del_init(&node->entry);
	}

	return node;
}

static void __esw_qos_free_node(struct mlx5_esw_sched_node *node)
{
	list_del(&node->entry);
	kfree(node);
}

static void esw_qos_destroy_node(struct mlx5_esw_sched_node *node, struct netlink_ext_ack *extack)
{
	esw_qos_node_destroy_sched_element(node, extack);
	__esw_qos_free_node(node);
}

static int esw_qos_create_vports_tc_node(struct mlx5_esw_sched_node *parent,
					 u8 tc, struct netlink_ext_ack *extack)
{
	u32 tsar_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	struct mlx5_core_dev *dev = parent->esw->dev;
	struct mlx5_esw_sched_node *vports_tc_node;
	void *attr;
	int err;

	if (!mlx5_qos_element_type_supported(
		dev,
		SCHEDULING_CONTEXT_ELEMENT_TYPE_TSAR,
		SCHEDULING_HIERARCHY_E_SWITCH) ||
	    !mlx5_qos_tsar_type_supported(dev,
					  TSAR_ELEMENT_TSAR_TYPE_DWRR,
					  SCHEDULING_HIERARCHY_E_SWITCH))
		return -EOPNOTSUPP;

	vports_tc_node = __esw_qos_alloc_node(parent->esw, 0,
					      SCHED_NODE_TYPE_VPORTS_TC_TSAR,
					      parent);
	if (!vports_tc_node) {
		NL_SET_ERR_MSG_MOD(extack, "E-Switch alloc node failed");
		esw_warn(dev, "Failed to alloc vports TC node (tc=%d)\n", tc);
		return -ENOMEM;
	}

	attr = MLX5_ADDR_OF(scheduling_context, tsar_ctx, element_attributes);
	MLX5_SET(tsar_element, attr, tsar_type, TSAR_ELEMENT_TSAR_TYPE_DWRR);
	MLX5_SET(tsar_element, attr, traffic_class, tc);
	MLX5_SET(scheduling_context, tsar_ctx, parent_element_id, parent->ix);
	MLX5_SET(scheduling_context, tsar_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_TSAR);

	err = esw_qos_node_create_sched_element(vports_tc_node, tsar_ctx,
						extack);
	if (err)
		goto err_create_sched_element;

	vports_tc_node->tc = tc;

	return 0;

err_create_sched_element:
	__esw_qos_free_node(vports_tc_node);
	return err;
}

static void
esw_qos_tc_arbiter_get_bw_shares(struct mlx5_esw_sched_node *tc_arbiter_node,
				 u32 *tc_bw)
{
	memcpy(tc_bw, tc_arbiter_node->tc_bw, sizeof(tc_arbiter_node->tc_bw));
}

static void
esw_qos_set_tc_arbiter_bw_shares(struct mlx5_esw_sched_node *tc_arbiter_node,
				 u32 *tc_bw, struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = tc_arbiter_node->esw;
	struct mlx5_esw_sched_node *vports_tc_node;
	u32 divider, fw_max_bw_share;

	fw_max_bw_share = MLX5_CAP_QOS(esw->dev, max_tsar_bw_share);
	divider = esw_qos_calculate_tc_bw_divider(tc_bw);
	list_for_each_entry(vports_tc_node, &tc_arbiter_node->children, entry) {
		u8 tc = vports_tc_node->tc;
		u32 bw_share;

		tc_arbiter_node->tc_bw[tc] = tc_bw[tc];
		bw_share = tc_bw[tc] * fw_max_bw_share;
		bw_share = esw_qos_calc_bw_share(bw_share, divider,
						 fw_max_bw_share);
		esw_qos_sched_elem_config(vports_tc_node, 0, bw_share, extack);
	}
}

static void
esw_qos_destroy_vports_tc_nodes(struct mlx5_esw_sched_node *tc_arbiter_node,
				struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *vports_tc_node, *tmp;

	list_for_each_entry_safe(vports_tc_node, tmp,
				 &tc_arbiter_node->children, entry)
		esw_qos_destroy_node(vports_tc_node, extack);
}

static int
esw_qos_create_vports_tc_nodes(struct mlx5_esw_sched_node *tc_arbiter_node,
			       struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = tc_arbiter_node->esw;
	int err, i, num_tcs = esw_qos_num_tcs(esw->dev);

	for (i = 0; i < num_tcs; i++) {
		err = esw_qos_create_vports_tc_node(tc_arbiter_node, i, extack);
		if (err)
			goto err_tc_node_create;
	}

	return 0;

err_tc_node_create:
	esw_qos_destroy_vports_tc_nodes(tc_arbiter_node, NULL);
	return err;
}

static int esw_qos_create_tc_arbiter_sched_elem(
		struct mlx5_esw_sched_node *tc_arbiter_node,
		struct netlink_ext_ack *extack)
{
	u32 tsar_ctx[MLX5_ST_SZ_DW(scheduling_context)] = {};
	u32 tsar_parent_ix;
	void *attr;

	if (!mlx5_qos_tsar_type_supported(tc_arbiter_node->esw->dev,
					  TSAR_ELEMENT_TSAR_TYPE_TC_ARB,
					  SCHEDULING_HIERARCHY_E_SWITCH)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "E-Switch TC Arbiter scheduling element is not supported");
		return -EOPNOTSUPP;
	}

	attr = MLX5_ADDR_OF(scheduling_context, tsar_ctx, element_attributes);
	MLX5_SET(tsar_element, attr, tsar_type, TSAR_ELEMENT_TSAR_TYPE_TC_ARB);
	tsar_parent_ix = tc_arbiter_node->parent ? tc_arbiter_node->parent->ix :
			 tc_arbiter_node->esw->qos.root_tsar_ix;
	MLX5_SET(scheduling_context, tsar_ctx, parent_element_id,
		 tsar_parent_ix);
	MLX5_SET(scheduling_context, tsar_ctx, element_type,
		 SCHEDULING_CONTEXT_ELEMENT_TYPE_TSAR);
	MLX5_SET(scheduling_context, tsar_ctx, max_average_bw,
		 tc_arbiter_node->max_rate);
	MLX5_SET(scheduling_context, tsar_ctx, bw_share,
		 tc_arbiter_node->bw_share);

	return esw_qos_node_create_sched_element(tc_arbiter_node, tsar_ctx,
						 extack);
}

static struct mlx5_esw_sched_node *
__esw_qos_create_vports_sched_node(struct mlx5_eswitch *esw, struct mlx5_esw_sched_node *parent,
				   struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *node;
	u32 tsar_ix;
	int err;

	err = esw_qos_create_node_sched_elem(esw->dev, esw->qos.root_tsar_ix, 0,
					     0, &tsar_ix);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "E-Switch create TSAR for node failed");
		return ERR_PTR(err);
	}

	node = __esw_qos_alloc_node(esw, tsar_ix, SCHED_NODE_TYPE_VPORTS_TSAR, parent);
	if (!node) {
		NL_SET_ERR_MSG_MOD(extack, "E-Switch alloc node failed");
		err = -ENOMEM;
		goto err_alloc_node;
	}

	list_add_tail(&node->entry, &esw->qos.domain->nodes);
	esw_qos_normalize_min_rate(esw, NULL, extack);
	trace_mlx5_esw_node_qos_create(esw->dev, node, node->ix);

	return node;

err_alloc_node:
	if (mlx5_destroy_scheduling_element_cmd(esw->dev,
						SCHEDULING_HIERARCHY_E_SWITCH,
						tsar_ix))
		NL_SET_ERR_MSG_MOD(extack, "E-Switch destroy TSAR for node failed");
	return ERR_PTR(err);
}

static int esw_qos_get(struct mlx5_eswitch *esw, struct netlink_ext_ack *extack);
static void esw_qos_put(struct mlx5_eswitch *esw);

static struct mlx5_esw_sched_node *
esw_qos_create_vports_sched_node(struct mlx5_eswitch *esw, struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *node;
	int err;

	esw_assert_qos_lock_held(esw);
	if (!MLX5_CAP_QOS(esw->dev, log_esw_max_sched_depth))
		return ERR_PTR(-EOPNOTSUPP);

	err = esw_qos_get(esw, extack);
	if (err)
		return ERR_PTR(err);

	node = __esw_qos_create_vports_sched_node(esw, NULL, extack);
	if (IS_ERR(node))
		esw_qos_put(esw);

	return node;
}

static void __esw_qos_destroy_node(struct mlx5_esw_sched_node *node, struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = node->esw;

	if (node->type == SCHED_NODE_TYPE_TC_ARBITER_TSAR)
		esw_qos_destroy_vports_tc_nodes(node, extack);

	trace_mlx5_esw_node_qos_destroy(esw->dev, node, node->ix);
	esw_qos_destroy_node(node, extack);
	esw_qos_normalize_min_rate(esw, NULL, extack);
}

static int esw_qos_create(struct mlx5_eswitch *esw, struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = esw->dev;
	int err;

	if (!MLX5_CAP_GEN(dev, qos) || !MLX5_CAP_QOS(dev, esw_scheduling))
		return -EOPNOTSUPP;

	err = esw_qos_create_node_sched_elem(esw->dev, 0, 0, 0,
					     &esw->qos.root_tsar_ix);
	if (err) {
		esw_warn(dev, "E-Switch create root TSAR failed (%d)\n", err);
		return err;
	}

	refcount_set(&esw->qos.refcnt, 1);

	return 0;
}

static void esw_qos_destroy(struct mlx5_eswitch *esw)
{
	int err;

	err = mlx5_destroy_scheduling_element_cmd(esw->dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  esw->qos.root_tsar_ix);
	if (err)
		esw_warn(esw->dev, "E-Switch destroy root TSAR failed (%d)\n", err);
}

static int esw_qos_get(struct mlx5_eswitch *esw, struct netlink_ext_ack *extack)
{
	int err = 0;

	esw_assert_qos_lock_held(esw);
	if (!refcount_inc_not_zero(&esw->qos.refcnt)) {
		/* esw_qos_create() set refcount to 1 only on success.
		 * No need to decrement on failure.
		 */
		err = esw_qos_create(esw, extack);
	}

	return err;
}

static void esw_qos_put(struct mlx5_eswitch *esw)
{
	esw_assert_qos_lock_held(esw);
	if (refcount_dec_and_test(&esw->qos.refcnt))
		esw_qos_destroy(esw);
}

static void
esw_qos_tc_arbiter_scheduling_teardown(struct mlx5_esw_sched_node *node,
				       struct netlink_ext_ack *extack)
{
	/* Clean up all Vports TC nodes within the TC arbiter node. */
	esw_qos_destroy_vports_tc_nodes(node, extack);
	/* Destroy the scheduling element for the TC arbiter node itself. */
	esw_qos_node_destroy_sched_element(node, extack);
}

static int esw_qos_tc_arbiter_scheduling_setup(struct mlx5_esw_sched_node *node,
					       struct netlink_ext_ack *extack)
{
	u32 curr_ix = node->ix;
	int err;

	err = esw_qos_create_tc_arbiter_sched_elem(node, extack);
	if (err)
		return err;
	/* Initialize the vports TC nodes within created TC arbiter TSAR. */
	err = esw_qos_create_vports_tc_nodes(node, extack);
	if (err)
		goto err_vports_tc_nodes;

	node->type = SCHED_NODE_TYPE_TC_ARBITER_TSAR;

	return 0;

err_vports_tc_nodes:
	/* If initialization fails, clean up the scheduling element
	 * for the TC arbiter node.
	 */
	esw_qos_node_destroy_sched_element(node, NULL);
	node->ix = curr_ix;
	return err;
}

static int
esw_qos_create_vport_tc_sched_node(struct mlx5_vport *vport,
				   u32 rate_limit_elem_ix,
				   struct mlx5_esw_sched_node *vports_tc_node,
				   struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *vport_node = vport->qos.sched_node;
	struct mlx5_esw_sched_node *vport_tc_node;
	u8 tc = vports_tc_node->tc;
	int err;

	vport_tc_node = __esw_qos_alloc_node(vport_node->esw, 0,
					     SCHED_NODE_TYPE_VPORT_TC,
					     vports_tc_node);
	if (!vport_tc_node)
		return -ENOMEM;

	vport_tc_node->min_rate = vport_node->min_rate;
	vport_tc_node->tc = tc;
	vport_tc_node->vport = vport;
	err = esw_qos_vport_tc_create_sched_element(vport_tc_node,
						    rate_limit_elem_ix,
						    extack);
	if (err)
		goto err_out;

	vport->qos.sched_nodes[tc] = vport_tc_node;

	return 0;
err_out:
	__esw_qos_free_node(vport_tc_node);
	return err;
}

static void
esw_qos_destroy_vport_tc_sched_elements(struct mlx5_vport *vport,
					struct netlink_ext_ack *extack)
{
	int i, num_tcs = esw_qos_num_tcs(vport->qos.sched_node->esw->dev);

	for (i = 0; i < num_tcs; i++) {
		if (vport->qos.sched_nodes[i]) {
			__esw_qos_destroy_node(vport->qos.sched_nodes[i],
					       extack);
		}
	}

	kfree(vport->qos.sched_nodes);
	vport->qos.sched_nodes = NULL;
}

static int
esw_qos_create_vport_tc_sched_elements(struct mlx5_vport *vport,
				       enum sched_node_type type,
				       struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *vport_node = vport->qos.sched_node;
	struct mlx5_esw_sched_node *tc_arbiter_node, *vports_tc_node;
	int err, num_tcs = esw_qos_num_tcs(vport_node->esw->dev);
	u32 rate_limit_elem_ix;

	vport->qos.sched_nodes = kcalloc(num_tcs,
					 sizeof(struct mlx5_esw_sched_node *),
					 GFP_KERNEL);
	if (!vport->qos.sched_nodes) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Allocating the vport TC scheduling elements failed.");
		return -ENOMEM;
	}

	rate_limit_elem_ix = type == SCHED_NODE_TYPE_RATE_LIMITER ?
			     vport_node->ix : 0;
	tc_arbiter_node = type == SCHED_NODE_TYPE_RATE_LIMITER ?
			   vport_node->parent : vport_node;
	list_for_each_entry(vports_tc_node, &tc_arbiter_node->children, entry) {
		err = esw_qos_create_vport_tc_sched_node(vport,
							 rate_limit_elem_ix,
							 vports_tc_node,
							 extack);
		if (err)
			goto err_create_vport_tc;
	}

	return 0;

err_create_vport_tc:
	esw_qos_destroy_vport_tc_sched_elements(vport, NULL);

	return err;
}

static int
esw_qos_vport_tc_enable(struct mlx5_vport *vport, enum sched_node_type type,
			struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *vport_node = vport->qos.sched_node;
	struct mlx5_esw_sched_node *parent = vport_node->parent;
	int err;

	if (type == SCHED_NODE_TYPE_TC_ARBITER_TSAR) {
		int new_level, max_level;

		/* Increase the parent's level by 2 to account for both the
		 * TC arbiter and the vports TC scheduling element.
		 */
		new_level = (parent ? parent->level : 2) + 2;
		max_level = 1 << MLX5_CAP_QOS(vport_node->esw->dev,
					      log_esw_max_sched_depth);
		if (new_level > max_level) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "TC arbitration on leafs is not supported beyond max depth %d",
					       max_level);
			return -EOPNOTSUPP;
		}
	}

	esw_assert_qos_lock_held(vport->dev->priv.eswitch);

	if (type == SCHED_NODE_TYPE_RATE_LIMITER)
		err = esw_qos_create_rate_limit_element(vport_node, extack);
	else
		err = esw_qos_tc_arbiter_scheduling_setup(vport_node, extack);
	if (err)
		return err;

	/* Rate limiters impact multiple nodes not directly connected to them
	 * and are not direct members of the QoS hierarchy.
	 * Unlink it from the parent to reflect that.
	 */
	if (type == SCHED_NODE_TYPE_RATE_LIMITER) {
		list_del_init(&vport_node->entry);
		vport_node->level = 0;
	}

	err  = esw_qos_create_vport_tc_sched_elements(vport, type, extack);
	if (err)
		goto err_sched_nodes;

	return 0;

err_sched_nodes:
	if (type == SCHED_NODE_TYPE_RATE_LIMITER) {
		esw_qos_node_destroy_sched_element(vport_node, NULL);
		esw_qos_node_attach_to_parent(vport_node);
	} else {
		esw_qos_tc_arbiter_scheduling_teardown(vport_node, NULL);
	}
	return err;
}

static void esw_qos_vport_tc_disable(struct mlx5_vport *vport,
				     struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *vport_node = vport->qos.sched_node;
	enum sched_node_type curr_type = vport_node->type;

	esw_qos_destroy_vport_tc_sched_elements(vport, extack);

	if (curr_type == SCHED_NODE_TYPE_RATE_LIMITER)
		esw_qos_node_destroy_sched_element(vport_node, extack);
	else
		esw_qos_tc_arbiter_scheduling_teardown(vport_node, extack);
}

static int esw_qos_set_vport_tcs_min_rate(struct mlx5_vport *vport,
					  u32 min_rate,
					  struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *vport_node = vport->qos.sched_node;
	int err, i, num_tcs = esw_qos_num_tcs(vport_node->esw->dev);

	for (i = 0; i < num_tcs; i++) {
		err = esw_qos_set_node_min_rate(vport->qos.sched_nodes[i],
						min_rate, extack);
		if (err)
			goto err_out;
	}
	vport_node->min_rate = min_rate;

	return 0;
err_out:
	for (--i; i >= 0; i--) {
		esw_qos_set_node_min_rate(vport->qos.sched_nodes[i],
					  vport_node->min_rate, extack);
	}
	return err;
}

static void esw_qos_vport_disable(struct mlx5_vport *vport, struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *vport_node = vport->qos.sched_node;
	enum sched_node_type curr_type = vport_node->type;

	if (curr_type == SCHED_NODE_TYPE_VPORT)
		esw_qos_node_destroy_sched_element(vport_node, extack);
	else
		esw_qos_vport_tc_disable(vport, extack);

	vport_node->bw_share = 0;
	memset(vport_node->tc_bw, 0, sizeof(vport_node->tc_bw));
	list_del_init(&vport_node->entry);
	esw_qos_normalize_min_rate(vport_node->esw, vport_node->parent, extack);

	trace_mlx5_esw_vport_qos_destroy(vport_node->esw->dev, vport);
}

static int esw_qos_vport_enable(struct mlx5_vport *vport,
				enum sched_node_type type,
				struct mlx5_esw_sched_node *parent,
				struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *vport_node = vport->qos.sched_node;
	int err;

	esw_assert_qos_lock_held(vport->dev->priv.eswitch);

	esw_qos_node_set_parent(vport_node, parent);
	if (type == SCHED_NODE_TYPE_VPORT)
		err = esw_qos_vport_create_sched_element(vport_node, extack);
	else
		err = esw_qos_vport_tc_enable(vport, type, extack);
	if (err)
		return err;

	vport_node->type = type;
	esw_qos_normalize_min_rate(vport_node->esw, parent, extack);
	trace_mlx5_esw_vport_qos_create(vport->dev, vport, vport_node->max_rate,
					vport_node->bw_share);

	return 0;
}

static int mlx5_esw_qos_vport_enable(struct mlx5_vport *vport, enum sched_node_type type,
				     struct mlx5_esw_sched_node *parent, u32 max_rate,
				     u32 min_rate, struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = vport->dev->priv.eswitch;
	struct mlx5_esw_sched_node *sched_node;
	struct mlx5_eswitch *parent_esw;
	int err;

	esw_assert_qos_lock_held(esw);
	err = esw_qos_get(esw, extack);
	if (err)
		return err;

	parent_esw = parent ? parent->esw : esw;
	sched_node = __esw_qos_alloc_node(parent_esw, 0, type, parent);
	if (!sched_node) {
		esw_qos_put(esw);
		return -ENOMEM;
	}
	if (!parent)
		list_add_tail(&sched_node->entry, &esw->qos.domain->nodes);

	sched_node->max_rate = max_rate;
	sched_node->min_rate = min_rate;
	sched_node->vport = vport;
	vport->qos.sched_node = sched_node;
	err = esw_qos_vport_enable(vport, type, parent, extack);
	if (err) {
		__esw_qos_free_node(sched_node);
		esw_qos_put(esw);
		vport->qos.sched_node = NULL;
	}

	return err;
}

static void mlx5_esw_qos_vport_disable_locked(struct mlx5_vport *vport)
{
	struct mlx5_eswitch *esw = vport->dev->priv.eswitch;

	esw_assert_qos_lock_held(esw);
	if (!vport->qos.sched_node)
		return;

	esw_qos_vport_disable(vport, NULL);
	mlx5_esw_qos_vport_qos_free(vport);
	esw_qos_put(esw);
}

void mlx5_esw_qos_vport_disable(struct mlx5_vport *vport)
{
	struct mlx5_eswitch *esw = vport->dev->priv.eswitch;
	struct mlx5_esw_sched_node *parent;

	lockdep_assert_held(&esw->state_lock);
	esw_qos_lock(esw);
	if (!vport->qos.sched_node)
		goto unlock;

	parent = vport->qos.sched_node->parent;
	WARN(parent, "Disabling QoS on port before detaching it from node");

	mlx5_esw_qos_vport_disable_locked(vport);
unlock:
	esw_qos_unlock(esw);
}

static int mlx5_esw_qos_set_vport_max_rate(struct mlx5_vport *vport, u32 max_rate,
					   struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *vport_node = vport->qos.sched_node;

	esw_assert_qos_lock_held(vport->dev->priv.eswitch);

	if (!vport_node)
		return mlx5_esw_qos_vport_enable(vport, SCHED_NODE_TYPE_VPORT, NULL, max_rate, 0,
						 extack);
	else
		return esw_qos_sched_elem_config(vport_node, max_rate, vport_node->bw_share,
						 extack);
}

static int mlx5_esw_qos_set_vport_min_rate(struct mlx5_vport *vport, u32 min_rate,
					   struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *vport_node = vport->qos.sched_node;

	esw_assert_qos_lock_held(vport->dev->priv.eswitch);

	if (!vport_node)
		return mlx5_esw_qos_vport_enable(vport, SCHED_NODE_TYPE_VPORT, NULL, 0, min_rate,
						 extack);
	else if (vport_node->type == SCHED_NODE_TYPE_RATE_LIMITER)
		return esw_qos_set_vport_tcs_min_rate(vport, min_rate, extack);
	else
		return esw_qos_set_node_min_rate(vport_node, min_rate, extack);
}

int mlx5_esw_qos_set_vport_rate(struct mlx5_vport *vport, u32 max_rate, u32 min_rate)
{
	struct mlx5_eswitch *esw = vport->dev->priv.eswitch;
	int err;

	esw_qos_lock(esw);
	err = mlx5_esw_qos_set_vport_min_rate(vport, min_rate, NULL);
	if (!err)
		err = mlx5_esw_qos_set_vport_max_rate(vport, max_rate, NULL);
	esw_qos_unlock(esw);
	return err;
}

bool mlx5_esw_qos_get_vport_rate(struct mlx5_vport *vport, u32 *max_rate, u32 *min_rate)
{
	struct mlx5_eswitch *esw = vport->dev->priv.eswitch;
	bool enabled;

	esw_qos_lock(esw);
	enabled = !!vport->qos.sched_node;
	if (enabled) {
		*max_rate = vport->qos.sched_node->max_rate;
		*min_rate = vport->qos.sched_node->min_rate;
	}
	esw_qos_unlock(esw);
	return enabled;
}

static int esw_qos_vport_tc_check_type(enum sched_node_type curr_type,
				       enum sched_node_type new_type,
				       struct netlink_ext_ack *extack)
{
	if (curr_type == SCHED_NODE_TYPE_TC_ARBITER_TSAR &&
	    new_type == SCHED_NODE_TYPE_RATE_LIMITER) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot switch from vport-level TC arbitration to node-level TC arbitration");
		return -EOPNOTSUPP;
	}

	if (curr_type == SCHED_NODE_TYPE_RATE_LIMITER &&
	    new_type == SCHED_NODE_TYPE_TC_ARBITER_TSAR) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot switch from node-level TC arbitration to vport-level TC arbitration");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int esw_qos_vport_update(struct mlx5_vport *vport,
				enum sched_node_type type,
				struct mlx5_esw_sched_node *parent,
				struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *vport_node = vport->qos.sched_node;
	struct mlx5_esw_sched_node *curr_parent = vport_node->parent;
	enum sched_node_type curr_type = vport_node->type;
	u32 curr_tc_bw[DEVLINK_RATE_TCS_MAX] = {0};
	int err;

	esw_assert_qos_lock_held(vport->dev->priv.eswitch);
	if (curr_type == type && curr_parent == parent)
		return 0;

	err = esw_qos_vport_tc_check_type(curr_type, type, extack);
	if (err)
		return err;

	if (curr_type == SCHED_NODE_TYPE_TC_ARBITER_TSAR && curr_type == type)
		esw_qos_tc_arbiter_get_bw_shares(vport_node, curr_tc_bw);

	esw_qos_vport_disable(vport, extack);

	err = esw_qos_vport_enable(vport, type, parent, extack);
	if (err) {
		esw_qos_vport_enable(vport, curr_type, curr_parent, NULL);
		extack = NULL;
	}

	if (curr_type == SCHED_NODE_TYPE_TC_ARBITER_TSAR && curr_type == type) {
		esw_qos_set_tc_arbiter_bw_shares(vport_node, curr_tc_bw,
						 extack);
	}

	return err;
}

static int esw_qos_vport_update_parent(struct mlx5_vport *vport, struct mlx5_esw_sched_node *parent,
				       struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = vport->dev->priv.eswitch;
	struct mlx5_esw_sched_node *curr_parent;
	enum sched_node_type type;

	esw_assert_qos_lock_held(esw);
	curr_parent = vport->qos.sched_node->parent;
	if (curr_parent == parent)
		return 0;

	/* Set vport QoS type based on parent node type if different from
	 * default QoS; otherwise, use the vport's current QoS type.
	 */
	if (parent && parent->type == SCHED_NODE_TYPE_TC_ARBITER_TSAR)
		type = SCHED_NODE_TYPE_RATE_LIMITER;
	else if (curr_parent &&
		 curr_parent->type == SCHED_NODE_TYPE_TC_ARBITER_TSAR)
		type = SCHED_NODE_TYPE_VPORT;
	else
		type = vport->qos.sched_node->type;

	return esw_qos_vport_update(vport, type, parent, extack);
}

static void
esw_qos_switch_vport_tcs_to_vport(struct mlx5_esw_sched_node *tc_arbiter_node,
				  struct mlx5_esw_sched_node *node,
				  struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *vports_tc_node, *vport_tc_node, *tmp;

	vports_tc_node = list_first_entry(&tc_arbiter_node->children,
					  struct mlx5_esw_sched_node,
					  entry);

	list_for_each_entry_safe(vport_tc_node, tmp, &vports_tc_node->children,
				 entry)
		esw_qos_vport_update_parent(vport_tc_node->vport, node, extack);
}

static int esw_qos_switch_tc_arbiter_node_to_vports(
	struct mlx5_esw_sched_node *tc_arbiter_node,
	struct mlx5_esw_sched_node *node,
	struct netlink_ext_ack *extack)
{
	u32 parent_tsar_ix = node->parent ?
			     node->parent->ix : node->esw->qos.root_tsar_ix;
	int err;

	err = esw_qos_create_node_sched_elem(node->esw->dev, parent_tsar_ix,
					     node->max_rate, node->bw_share,
					     &node->ix);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to create scheduling element for vports node when disabling vports TC QoS");
		return err;
	}

	node->type = SCHED_NODE_TYPE_VPORTS_TSAR;

	/* Disable TC QoS for vports in the arbiter node. */
	esw_qos_switch_vport_tcs_to_vport(tc_arbiter_node, node, extack);

	return 0;
}

static int esw_qos_switch_vports_node_to_tc_arbiter(
	struct mlx5_esw_sched_node *node,
	struct mlx5_esw_sched_node *tc_arbiter_node,
	struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *vport_node, *tmp;
	struct mlx5_vport *vport;
	int err;

	/* Enable TC QoS for each vport in the node. */
	list_for_each_entry_safe(vport_node, tmp, &node->children, entry) {
		vport = vport_node->vport;
		err = esw_qos_vport_update_parent(vport, tc_arbiter_node,
						  extack);
		if  (err)
			goto err_out;
	}

	/* Destroy the current vports node TSAR. */
	err = mlx5_destroy_scheduling_element_cmd(node->esw->dev,
						  SCHEDULING_HIERARCHY_E_SWITCH,
						  node->ix);
	if (err)
		goto err_out;

	return 0;
err_out:
	/* Restore vports back into the node if an error occurs. */
	esw_qos_switch_vport_tcs_to_vport(tc_arbiter_node, node, NULL);

	return err;
}

static struct mlx5_esw_sched_node *
esw_qos_move_node(struct mlx5_esw_sched_node *curr_node)
{
	struct mlx5_esw_sched_node *new_node;

	new_node = __esw_qos_alloc_node(curr_node->esw, curr_node->ix,
					curr_node->type, NULL);
	if (!new_node)
		return ERR_PTR(-ENOMEM);

	esw_qos_nodes_set_parent(&curr_node->children, new_node);
	return new_node;
}

static int esw_qos_node_disable_tc_arbitration(struct mlx5_esw_sched_node *node,
					       struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *curr_node;
	int err;

	if (node->type != SCHED_NODE_TYPE_TC_ARBITER_TSAR)
		return 0;

	/* Allocate a new rate node to hold the current state, which will allow
	 * for restoring the vports back to this node after disabling TC
	 * arbitration.
	 */
	curr_node = esw_qos_move_node(node);
	if (IS_ERR(curr_node)) {
		NL_SET_ERR_MSG_MOD(extack, "Failed setting up vports node");
		return PTR_ERR(curr_node);
	}

	/* Disable TC QoS for all vports, and assign them back to the node. */
	err = esw_qos_switch_tc_arbiter_node_to_vports(curr_node, node, extack);
	if (err)
		goto err_out;

	/* Clean up the TC arbiter node after disabling TC QoS for vports. */
	esw_qos_tc_arbiter_scheduling_teardown(curr_node, extack);
	goto out;
err_out:
	esw_qos_nodes_set_parent(&curr_node->children, node);
out:
	__esw_qos_free_node(curr_node);
	return err;
}

static int esw_qos_node_enable_tc_arbitration(struct mlx5_esw_sched_node *node,
					      struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *curr_node, *child;
	int err, new_level, max_level;

	if (node->type == SCHED_NODE_TYPE_TC_ARBITER_TSAR)
		return 0;

	/* Increase the hierarchy level by one to account for the additional
	 * vports TC scheduling node, and verify that the new level does not
	 * exceed the maximum allowed depth.
	 */
	new_level = node->level + 1;
	max_level = 1 << MLX5_CAP_QOS(node->esw->dev, log_esw_max_sched_depth);
	if (new_level > max_level) {
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "TC arbitration on nodes is not supported beyond max depth %d",
				       max_level);
		return -EOPNOTSUPP;
	}

	/* Ensure the node does not contain non-leaf children before assigning
	 * TC bandwidth.
	 */
	if (!list_empty(&node->children)) {
		list_for_each_entry(child, &node->children, entry) {
			if (!child->vport) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Cannot configure TC bandwidth on a node with non-leaf children");
				return -EOPNOTSUPP;
			}
		}
	}

	/* Allocate a new node that will store the information of the current
	 * node. This will be used later to restore the node if necessary.
	 */
	curr_node = esw_qos_move_node(node);
	if (IS_ERR(curr_node)) {
		NL_SET_ERR_MSG_MOD(extack, "Failed setting up node TC QoS");
		return PTR_ERR(curr_node);
	}

	/* Initialize the TC arbiter node for QoS management.
	 * This step prepares the node for handling Traffic Class arbitration.
	 */
	err = esw_qos_tc_arbiter_scheduling_setup(node, extack);
	if (err)
		goto err_setup;

	/* Enable TC QoS for each vport within the current node. */
	err = esw_qos_switch_vports_node_to_tc_arbiter(curr_node, node, extack);
	if (err)
		goto err_switch_vports;
	goto out;

err_switch_vports:
	esw_qos_tc_arbiter_scheduling_teardown(node, NULL);
	node->ix = curr_node->ix;
	node->type = curr_node->type;
err_setup:
	esw_qos_nodes_set_parent(&curr_node->children, node);
out:
	__esw_qos_free_node(curr_node);
	return err;
}

static u32 mlx5_esw_qos_lag_link_speed_get_locked(struct mlx5_core_dev *mdev)
{
	struct ethtool_link_ksettings lksettings;
	struct net_device *slave, *master;
	u32 speed = SPEED_UNKNOWN;

	/* Lock ensures a stable reference to master and slave netdevice
	 * while port speed of master is queried.
	 */
	ASSERT_RTNL();

	slave = mlx5_uplink_netdev_get(mdev);
	if (!slave)
		goto out;

	master = netdev_master_upper_dev_get(slave);
	if (master && !__ethtool_get_link_ksettings(master, &lksettings))
		speed = lksettings.base.speed;

out:
	mlx5_uplink_netdev_put(mdev, slave);
	return speed;
}

static int mlx5_esw_qos_max_link_speed_get(struct mlx5_core_dev *mdev, u32 *link_speed_max,
					   bool hold_rtnl_lock, struct netlink_ext_ack *extack)
{
	int err;

	if (!mlx5_lag_is_active(mdev))
		goto skip_lag;

	if (hold_rtnl_lock)
		rtnl_lock();

	*link_speed_max = mlx5_esw_qos_lag_link_speed_get_locked(mdev);

	if (hold_rtnl_lock)
		rtnl_unlock();

	if (*link_speed_max != (u32)SPEED_UNKNOWN)
		return 0;

skip_lag:
	err = mlx5_port_max_linkspeed(mdev, link_speed_max);
	if (err)
		NL_SET_ERR_MSG_MOD(extack, "Failed to get link maximum speed");

	return err;
}

static int mlx5_esw_qos_link_speed_verify(struct mlx5_core_dev *mdev,
					  const char *name, u32 link_speed_max,
					  u64 value, struct netlink_ext_ack *extack)
{
	if (value > link_speed_max) {
		pr_err("%s rate value %lluMbps exceed link maximum speed %u.\n",
		       name, value, link_speed_max);
		NL_SET_ERR_MSG_MOD(extack, "TX rate value exceed link maximum speed");
		return -EINVAL;
	}

	return 0;
}

int mlx5_esw_qos_modify_vport_rate(struct mlx5_eswitch *esw, u16 vport_num, u32 rate_mbps)
{
	struct mlx5_vport *vport;
	u32 link_speed_max;
	int err;

	vport = mlx5_eswitch_get_vport(esw, vport_num);
	if (IS_ERR(vport))
		return PTR_ERR(vport);

	if (rate_mbps) {
		err = mlx5_esw_qos_max_link_speed_get(esw->dev, &link_speed_max, false, NULL);
		if (err)
			return err;

		err = mlx5_esw_qos_link_speed_verify(esw->dev, "Police",
						     link_speed_max, rate_mbps, NULL);
		if (err)
			return err;
	}

	esw_qos_lock(esw);
	err = mlx5_esw_qos_set_vport_max_rate(vport, rate_mbps, NULL);
	esw_qos_unlock(esw);

	return err;
}

#define MLX5_LINKSPEED_UNIT 125000 /* 1Mbps in Bps */

/* Converts bytes per second value passed in a pointer into megabits per
 * second, rewriting last. If converted rate exceed link speed or is not a
 * fraction of Mbps - returns error.
 */
static int esw_qos_devlink_rate_to_mbps(struct mlx5_core_dev *mdev, const char *name,
					u64 *rate, struct netlink_ext_ack *extack)
{
	u32 link_speed_max, remainder;
	u64 value;
	int err;

	value = div_u64_rem(*rate, MLX5_LINKSPEED_UNIT, &remainder);
	if (remainder) {
		pr_err("%s rate value %lluBps not in link speed units of 1Mbps.\n",
		       name, *rate);
		NL_SET_ERR_MSG_MOD(extack, "TX rate value not in link speed units of 1Mbps");
		return -EINVAL;
	}

	err = mlx5_esw_qos_max_link_speed_get(mdev, &link_speed_max, true, extack);
	if (err)
		return err;

	err = mlx5_esw_qos_link_speed_verify(mdev, name, link_speed_max, value, extack);
	if (err)
		return err;

	*rate = value;
	return 0;
}

static bool esw_qos_validate_unsupported_tc_bw(struct mlx5_eswitch *esw,
					       u32 *tc_bw)
{
	int i, num_tcs = esw_qos_num_tcs(esw->dev);

	for (i = num_tcs; i < DEVLINK_RATE_TCS_MAX; i++) {
		if (tc_bw[i])
			return false;
	}

	return true;
}

static bool esw_qos_vport_validate_unsupported_tc_bw(struct mlx5_vport *vport,
						     u32 *tc_bw)
{
	struct mlx5_esw_sched_node *node = vport->qos.sched_node;
	struct mlx5_eswitch *esw = vport->dev->priv.eswitch;

	esw = (node && node->parent) ? node->parent->esw : esw;

	return esw_qos_validate_unsupported_tc_bw(esw, tc_bw);
}

static bool esw_qos_tc_bw_disabled(u32 *tc_bw)
{
	int i;

	for (i = 0; i < DEVLINK_RATE_TCS_MAX; i++) {
		if (tc_bw[i])
			return false;
	}

	return true;
}

static void esw_vport_qos_prune_empty(struct mlx5_vport *vport)
{
	struct mlx5_esw_sched_node *vport_node = vport->qos.sched_node;

	esw_assert_qos_lock_held(vport->dev->priv.eswitch);
	if (!vport_node)
		return;

	if (vport_node->parent || vport_node->max_rate ||
	    vport_node->min_rate || !esw_qos_tc_bw_disabled(vport_node->tc_bw))
		return;

	mlx5_esw_qos_vport_disable_locked(vport);
}

int mlx5_esw_qos_init(struct mlx5_eswitch *esw)
{
	if (esw->qos.domain)
		return 0;  /* Nothing to change. */

	return esw_qos_domain_init(esw);
}

void mlx5_esw_qos_cleanup(struct mlx5_eswitch *esw)
{
	if (esw->qos.domain)
		esw_qos_domain_release(esw);
}

/* Eswitch devlink rate API */

int mlx5_esw_devlink_rate_leaf_tx_share_set(struct devlink_rate *rate_leaf, void *priv,
					    u64 tx_share, struct netlink_ext_ack *extack)
{
	struct mlx5_vport *vport = priv;
	struct mlx5_eswitch *esw;
	int err;

	esw = vport->dev->priv.eswitch;
	if (!mlx5_esw_allowed(esw))
		return -EPERM;

	err = esw_qos_devlink_rate_to_mbps(vport->dev, "tx_share", &tx_share, extack);
	if (err)
		return err;

	esw_qos_lock(esw);
	err = mlx5_esw_qos_set_vport_min_rate(vport, tx_share, extack);
	if (err)
		goto out;
	esw_vport_qos_prune_empty(vport);
out:
	esw_qos_unlock(esw);
	return err;
}

int mlx5_esw_devlink_rate_leaf_tx_max_set(struct devlink_rate *rate_leaf, void *priv,
					  u64 tx_max, struct netlink_ext_ack *extack)
{
	struct mlx5_vport *vport = priv;
	struct mlx5_eswitch *esw;
	int err;

	esw = vport->dev->priv.eswitch;
	if (!mlx5_esw_allowed(esw))
		return -EPERM;

	err = esw_qos_devlink_rate_to_mbps(vport->dev, "tx_max", &tx_max, extack);
	if (err)
		return err;

	esw_qos_lock(esw);
	err = mlx5_esw_qos_set_vport_max_rate(vport, tx_max, extack);
	if (err)
		goto out;
	esw_vport_qos_prune_empty(vport);
out:
	esw_qos_unlock(esw);
	return err;
}

int mlx5_esw_devlink_rate_leaf_tc_bw_set(struct devlink_rate *rate_leaf,
					 void *priv,
					 u32 *tc_bw,
					 struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *vport_node;
	struct mlx5_vport *vport = priv;
	struct mlx5_eswitch *esw;
	bool disable;
	int err = 0;

	esw = vport->dev->priv.eswitch;
	if (!mlx5_esw_allowed(esw))
		return -EPERM;

	disable = esw_qos_tc_bw_disabled(tc_bw);
	esw_qos_lock(esw);

	if (!esw_qos_vport_validate_unsupported_tc_bw(vport, tc_bw)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "E-Switch traffic classes number is not supported");
		err = -EOPNOTSUPP;
		goto unlock;
	}

	vport_node = vport->qos.sched_node;
	if (disable && !vport_node)
		goto unlock;

	if (disable) {
		if (vport_node->type == SCHED_NODE_TYPE_TC_ARBITER_TSAR)
			err = esw_qos_vport_update(vport, SCHED_NODE_TYPE_VPORT,
						   vport_node->parent, extack);
		esw_vport_qos_prune_empty(vport);
		goto unlock;
	}

	if (!vport_node) {
		err = mlx5_esw_qos_vport_enable(vport,
						SCHED_NODE_TYPE_TC_ARBITER_TSAR,
						NULL, 0, 0, extack);
		vport_node = vport->qos.sched_node;
	} else {
		err = esw_qos_vport_update(vport,
					   SCHED_NODE_TYPE_TC_ARBITER_TSAR,
					   vport_node->parent, extack);
	}
	if (!err)
		esw_qos_set_tc_arbiter_bw_shares(vport_node, tc_bw, extack);
unlock:
	esw_qos_unlock(esw);
	return err;
}

int mlx5_esw_devlink_rate_node_tc_bw_set(struct devlink_rate *rate_node,
					 void *priv,
					 u32 *tc_bw,
					 struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *node = priv;
	struct mlx5_eswitch *esw = node->esw;
	bool disable;
	int err;

	if (!esw_qos_validate_unsupported_tc_bw(esw, tc_bw)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "E-Switch traffic classes number is not supported");
		return -EOPNOTSUPP;
	}

	disable = esw_qos_tc_bw_disabled(tc_bw);
	esw_qos_lock(esw);
	if (disable) {
		err = esw_qos_node_disable_tc_arbitration(node, extack);
		goto unlock;
	}

	err = esw_qos_node_enable_tc_arbitration(node, extack);
	if (!err)
		esw_qos_set_tc_arbiter_bw_shares(node, tc_bw, extack);
unlock:
	esw_qos_unlock(esw);
	return err;
}

int mlx5_esw_devlink_rate_node_tx_share_set(struct devlink_rate *rate_node, void *priv,
					    u64 tx_share, struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *node = priv;
	struct mlx5_eswitch *esw = node->esw;
	int err;

	err = esw_qos_devlink_rate_to_mbps(esw->dev, "tx_share", &tx_share, extack);
	if (err)
		return err;

	esw_qos_lock(esw);
	err = esw_qos_set_node_min_rate(node, tx_share, extack);
	esw_qos_unlock(esw);
	return err;
}

int mlx5_esw_devlink_rate_node_tx_max_set(struct devlink_rate *rate_node, void *priv,
					  u64 tx_max, struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *node = priv;
	struct mlx5_eswitch *esw = node->esw;
	int err;

	err = esw_qos_devlink_rate_to_mbps(esw->dev, "tx_max", &tx_max, extack);
	if (err)
		return err;

	esw_qos_lock(esw);
	err = esw_qos_sched_elem_config(node, tx_max, node->bw_share, extack);
	esw_qos_unlock(esw);
	return err;
}

int mlx5_esw_devlink_rate_node_new(struct devlink_rate *rate_node, void **priv,
				   struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *node;
	struct mlx5_eswitch *esw;
	int err = 0;

	esw = mlx5_devlink_eswitch_get(rate_node->devlink);
	if (IS_ERR(esw))
		return PTR_ERR(esw);

	esw_qos_lock(esw);
	if (esw->mode != MLX5_ESWITCH_OFFLOADS) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Rate node creation supported only in switchdev mode");
		err = -EOPNOTSUPP;
		goto unlock;
	}

	node = esw_qos_create_vports_sched_node(esw, extack);
	if (IS_ERR(node)) {
		err = PTR_ERR(node);
		goto unlock;
	}

	*priv = node;
unlock:
	esw_qos_unlock(esw);
	return err;
}

int mlx5_esw_devlink_rate_node_del(struct devlink_rate *rate_node, void *priv,
				   struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *node = priv;
	struct mlx5_eswitch *esw = node->esw;

	esw_qos_lock(esw);
	__esw_qos_destroy_node(node, extack);
	esw_qos_put(esw);
	esw_qos_unlock(esw);
	return 0;
}

int mlx5_esw_qos_vport_update_parent(struct mlx5_vport *vport, struct mlx5_esw_sched_node *parent,
				     struct netlink_ext_ack *extack)
{
	struct mlx5_eswitch *esw = vport->dev->priv.eswitch;
	int err = 0;

	if (parent && parent->esw != esw) {
		NL_SET_ERR_MSG_MOD(extack, "Cross E-Switch scheduling is not supported");
		return -EOPNOTSUPP;
	}

	esw_qos_lock(esw);
	if (!vport->qos.sched_node && parent) {
		enum sched_node_type type;

		type = parent->type == SCHED_NODE_TYPE_TC_ARBITER_TSAR ?
		       SCHED_NODE_TYPE_RATE_LIMITER : SCHED_NODE_TYPE_VPORT;
		err = mlx5_esw_qos_vport_enable(vport, type, parent, 0, 0,
						extack);
	} else if (vport->qos.sched_node) {
		err = esw_qos_vport_update_parent(vport, parent, extack);
	}
	esw_qos_unlock(esw);
	return err;
}

int mlx5_esw_devlink_rate_leaf_parent_set(struct devlink_rate *devlink_rate,
					  struct devlink_rate *parent,
					  void *priv, void *parent_priv,
					  struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *node = parent ? parent_priv : NULL;
	struct mlx5_vport *vport = priv;
	int err;

	err = mlx5_esw_qos_vport_update_parent(vport, node, extack);
	if (!err) {
		struct mlx5_eswitch *esw = vport->dev->priv.eswitch;

		esw_qos_lock(esw);
		esw_vport_qos_prune_empty(vport);
		esw_qos_unlock(esw);
	}

	return err;
}

static bool esw_qos_is_node_empty(struct mlx5_esw_sched_node *node)
{
	if (list_empty(&node->children))
		return true;

	if (node->type != SCHED_NODE_TYPE_TC_ARBITER_TSAR)
		return false;

	node = list_first_entry(&node->children, struct mlx5_esw_sched_node,
				entry);

	return esw_qos_is_node_empty(node);
}

static int
mlx5_esw_qos_node_validate_set_parent(struct mlx5_esw_sched_node *node,
				      struct mlx5_esw_sched_node *parent,
				      struct netlink_ext_ack *extack)
{
	u8 new_level, max_level;

	if (parent && parent->esw != node->esw) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot assign node to another E-Switch");
		return -EOPNOTSUPP;
	}

	if (!esw_qos_is_node_empty(node)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot reassign a node that contains rate objects");
		return -EOPNOTSUPP;
	}

	if (parent && parent->type == SCHED_NODE_TYPE_TC_ARBITER_TSAR) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot attach a node to a parent with TC bandwidth configured");
		return -EOPNOTSUPP;
	}

	new_level = parent ? parent->level + 1 : 2;
	if (node->type == SCHED_NODE_TYPE_TC_ARBITER_TSAR) {
		/* Increase by one to account for the vports TC scheduling
		 * element.
		 */
		new_level += 1;
	}

	max_level = 1 << MLX5_CAP_QOS(node->esw->dev, log_esw_max_sched_depth);
	if (new_level > max_level) {
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "Node hierarchy depth %d exceeds the maximum supported level %d",
				       new_level, max_level);
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
esw_qos_tc_arbiter_node_update_parent(struct mlx5_esw_sched_node *node,
				      struct mlx5_esw_sched_node *parent,
				      struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *curr_parent = node->parent;
	u32 curr_tc_bw[DEVLINK_RATE_TCS_MAX] = {0};
	struct mlx5_eswitch *esw = node->esw;
	int err;

	esw_qos_tc_arbiter_get_bw_shares(node, curr_tc_bw);
	esw_qos_tc_arbiter_scheduling_teardown(node, extack);
	esw_qos_node_set_parent(node, parent);
	err = esw_qos_tc_arbiter_scheduling_setup(node, extack);
	if (err) {
		esw_qos_node_set_parent(node, curr_parent);
		if (esw_qos_tc_arbiter_scheduling_setup(node, extack)) {
			esw_warn(esw->dev, "Node restore QoS failed\n");
			return err;
		}
	}
	esw_qos_set_tc_arbiter_bw_shares(node, curr_tc_bw, extack);

	return err;
}

static int esw_qos_vports_node_update_parent(struct mlx5_esw_sched_node *node,
					     struct mlx5_esw_sched_node *parent,
					     struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *curr_parent = node->parent;
	struct mlx5_eswitch *esw = node->esw;
	u32 parent_ix;
	int err;

	parent_ix = parent ? parent->ix : node->esw->qos.root_tsar_ix;
	mlx5_destroy_scheduling_element_cmd(esw->dev,
					    SCHEDULING_HIERARCHY_E_SWITCH,
					    node->ix);
	err = esw_qos_create_node_sched_elem(esw->dev, parent_ix,
					     node->max_rate, 0, &node->ix);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to create a node under the new hierarchy.");
		if (esw_qos_create_node_sched_elem(esw->dev, curr_parent->ix,
						   node->max_rate,
						   node->bw_share,
						   &node->ix))
			esw_warn(esw->dev, "Node restore QoS failed\n");

		return err;
	}
	esw_qos_node_set_parent(node, parent);
	node->bw_share = 0;

	return 0;
}

static int mlx5_esw_qos_node_update_parent(struct mlx5_esw_sched_node *node,
					   struct mlx5_esw_sched_node *parent,
					   struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *curr_parent;
	struct mlx5_eswitch *esw = node->esw;
	int err;

	err = mlx5_esw_qos_node_validate_set_parent(node, parent, extack);
	if (err)
		return err;

	esw_qos_lock(esw);
	curr_parent = node->parent;
	if (node->type == SCHED_NODE_TYPE_TC_ARBITER_TSAR) {
		err = esw_qos_tc_arbiter_node_update_parent(node, parent,
							    extack);
	} else {
		err = esw_qos_vports_node_update_parent(node, parent, extack);
	}

	if (err)
		goto out;

	esw_qos_normalize_min_rate(esw, curr_parent, extack);
	esw_qos_normalize_min_rate(esw, parent, extack);

out:
	esw_qos_unlock(esw);

	return err;
}

int mlx5_esw_devlink_rate_node_parent_set(struct devlink_rate *devlink_rate,
					  struct devlink_rate *parent,
					  void *priv, void *parent_priv,
					  struct netlink_ext_ack *extack)
{
	struct mlx5_esw_sched_node *node = priv, *parent_node;

	if (!parent)
		return mlx5_esw_qos_node_update_parent(node, NULL, extack);

	parent_node = parent_priv;
	return mlx5_esw_qos_node_update_parent(node, parent_node, extack);
}
