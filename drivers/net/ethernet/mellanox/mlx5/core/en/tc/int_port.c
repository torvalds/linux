// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/mlx5/fs.h>
#include "en/mapping.h"
#include "en/tc/int_port.h"
#include "en.h"
#include "en_rep.h"
#include "en_tc.h"

struct mlx5e_tc_int_port {
	enum mlx5e_tc_int_port_type type;
	int ifindex;
	u32 match_metadata;
	u32 mapping;
	struct list_head list;
	struct mlx5_flow_handle *rx_rule;
	refcount_t refcnt;
	struct rcu_head rcu_head;
};

struct mlx5e_tc_int_port_priv {
	struct mlx5_core_dev *dev;
	struct mutex int_ports_lock; /* Protects int ports list */
	struct list_head int_ports; /* Uses int_ports_lock */
	u16 num_ports;
	bool ul_rep_rx_ready; /* Set when uplink is performing teardown */
	struct mapping_ctx *metadata_mapping; /* Metadata for source port rewrite and matching */
};

bool mlx5e_tc_int_port_supported(const struct mlx5_eswitch *esw)
{
	return mlx5_eswitch_vport_match_metadata_enabled(esw) &&
	       MLX5_CAP_GEN(esw->dev, reg_c_preserve);
}

u32 mlx5e_tc_int_port_get_metadata(struct mlx5e_tc_int_port *int_port)
{
	return int_port->match_metadata;
}

int mlx5e_tc_int_port_get_flow_source(struct mlx5e_tc_int_port *int_port)
{
	/* For egress forwarding we can have the case
	 * where the packet came from a vport and redirected
	 * to int port or it came from the uplink, going
	 * via internal port and hairpinned back to uplink
	 * so we set the source to any port in this case.
	 */
	return int_port->type == MLX5E_TC_INT_PORT_EGRESS ?
		MLX5_FLOW_CONTEXT_FLOW_SOURCE_ANY_VPORT :
		MLX5_FLOW_CONTEXT_FLOW_SOURCE_UPLINK;
}

u32 mlx5e_tc_int_port_get_metadata_for_match(struct mlx5e_tc_int_port *int_port)
{
	return int_port->match_metadata << (32 - ESW_SOURCE_PORT_METADATA_BITS);
}

static struct mlx5_flow_handle *
mlx5e_int_port_create_rx_rule(struct mlx5_eswitch *esw,
			      struct mlx5e_tc_int_port *int_port,
			      struct mlx5_flow_destination *dest)

{
	struct mlx5_flow_context *flow_context;
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *flow_rule;
	struct mlx5_flow_spec *spec;
	void *misc;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return ERR_PTR(-ENOMEM);

	misc = MLX5_ADDR_OF(fte_match_param, spec->match_value, misc_parameters_2);
	MLX5_SET(fte_match_set_misc2, misc, metadata_reg_c_0,
		 mlx5e_tc_int_port_get_metadata_for_match(int_port));

	misc = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters_2);
	MLX5_SET(fte_match_set_misc2, misc, metadata_reg_c_0,
		 mlx5_eswitch_get_vport_metadata_mask());

	spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS_2;

	/* Overwrite flow tag with the int port metadata mapping
	 * instead of the chain mapping.
	 */
	flow_context = &spec->flow_context;
	flow_context->flags |= FLOW_CONTEXT_HAS_TAG;
	flow_context->flow_tag = int_port->mapping;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	flow_rule = mlx5_add_flow_rules(esw->offloads.ft_offloads, spec,
					&flow_act, dest, 1);
	if (IS_ERR(flow_rule))
		mlx5_core_warn(esw->dev, "ft offloads: Failed to add internal vport rx rule err %ld\n",
			       PTR_ERR(flow_rule));

	kvfree(spec);

	return flow_rule;
}

static struct mlx5e_tc_int_port *
mlx5e_int_port_lookup(struct mlx5e_tc_int_port_priv *priv,
		      int ifindex,
		      enum mlx5e_tc_int_port_type type)
{
	struct mlx5e_tc_int_port *int_port;

	if (!priv->ul_rep_rx_ready)
		goto not_found;

	list_for_each_entry(int_port, &priv->int_ports, list)
		if (int_port->ifindex == ifindex && int_port->type == type) {
			refcount_inc(&int_port->refcnt);
			return int_port;
		}

not_found:
	return NULL;
}

static int mlx5e_int_port_metadata_alloc(struct mlx5e_tc_int_port_priv *priv,
					 int ifindex, enum mlx5e_tc_int_port_type type,
					 u32 *id)
{
	u32 mapped_key[2] = {type, ifindex};
	int err;

	err = mapping_add(priv->metadata_mapping, mapped_key, id);
	if (err)
		return err;

	/* Fill upper 4 bits of PFNUM with reserved value */
	*id |= 0xf << ESW_VPORT_BITS;

	return 0;
}

static void mlx5e_int_port_metadata_free(struct mlx5e_tc_int_port_priv *priv,
					 u32 id)
{
	id &= (1 << ESW_VPORT_BITS) - 1;
	mapping_remove(priv->metadata_mapping, id);
}

/* Must be called with priv->int_ports_lock held */
static struct mlx5e_tc_int_port *
mlx5e_int_port_add(struct mlx5e_tc_int_port_priv *priv,
		   int ifindex,
		   enum mlx5e_tc_int_port_type type)
{
	struct mlx5_eswitch *esw = priv->dev->priv.eswitch;
	struct mlx5_mapped_obj mapped_obj = {};
	struct mlx5e_rep_priv *uplink_rpriv;
	struct mlx5e_tc_int_port *int_port;
	struct mlx5_flow_destination dest;
	struct mapping_ctx *ctx;
	u32 match_metadata;
	u32 mapping;
	int err;

	if (priv->num_ports == MLX5E_TC_MAX_INT_PORT_NUM) {
		mlx5_core_dbg(priv->dev, "Cannot add a new int port, max supported %d",
			      MLX5E_TC_MAX_INT_PORT_NUM);
		return ERR_PTR(-ENOSPC);
	}

	int_port = kzalloc(sizeof(*int_port), GFP_KERNEL);
	if (!int_port)
		return ERR_PTR(-ENOMEM);

	err = mlx5e_int_port_metadata_alloc(priv, ifindex, type, &match_metadata);
	if (err) {
		mlx5_core_warn(esw->dev, "Cannot add a new internal port, metadata allocation failed for ifindex %d",
			       ifindex);
		goto err_metadata;
	}

	/* map metadata to reg_c0 object for miss handling */
	ctx = esw->offloads.reg_c0_obj_pool;
	mapped_obj.type = MLX5_MAPPED_OBJ_INT_PORT_METADATA;
	mapped_obj.int_port_metadata = match_metadata;
	err = mapping_add(ctx, &mapped_obj, &mapping);
	if (err)
		goto err_map;

	int_port->type = type;
	int_port->ifindex = ifindex;
	int_port->match_metadata = match_metadata;
	int_port->mapping = mapping;

	/* Create a match on internal vport metadata in vport table */
	uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);

	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = uplink_rpriv->root_ft;

	int_port->rx_rule = mlx5e_int_port_create_rx_rule(esw, int_port, &dest);
	if (IS_ERR(int_port->rx_rule)) {
		err = PTR_ERR(int_port->rx_rule);
		mlx5_core_warn(esw->dev, "Can't add internal port rx rule, err %d", err);
		goto err_rx_rule;
	}

	refcount_set(&int_port->refcnt, 1);
	list_add_rcu(&int_port->list, &priv->int_ports);
	priv->num_ports++;

	return int_port;

err_rx_rule:
	mapping_remove(ctx, int_port->mapping);

err_map:
	mlx5e_int_port_metadata_free(priv, match_metadata);

err_metadata:
	kfree(int_port);

	return ERR_PTR(err);
}

/* Must be called with priv->int_ports_lock held */
static void
mlx5e_int_port_remove(struct mlx5e_tc_int_port_priv *priv,
		      struct mlx5e_tc_int_port *int_port)
{
	struct mlx5_eswitch *esw = priv->dev->priv.eswitch;
	struct mapping_ctx *ctx;

	ctx = esw->offloads.reg_c0_obj_pool;

	list_del_rcu(&int_port->list);

	/* The following parameters are not used by the
	 * rcu readers of this int_port object so it is
	 * safe to release them.
	 */
	if (int_port->rx_rule)
		mlx5_del_flow_rules(int_port->rx_rule);
	mapping_remove(ctx, int_port->mapping);
	mlx5e_int_port_metadata_free(priv, int_port->match_metadata);
	kfree_rcu(int_port);
	priv->num_ports--;
}

/* Must be called with rcu_read_lock held */
static struct mlx5e_tc_int_port *
mlx5e_int_port_get_from_metadata(struct mlx5e_tc_int_port_priv *priv,
				 u32 metadata)
{
	struct mlx5e_tc_int_port *int_port;

	list_for_each_entry_rcu(int_port, &priv->int_ports, list)
		if (int_port->match_metadata == metadata)
			return int_port;

	return NULL;
}

struct mlx5e_tc_int_port *
mlx5e_tc_int_port_get(struct mlx5e_tc_int_port_priv *priv,
		      int ifindex,
		      enum mlx5e_tc_int_port_type type)
{
	struct mlx5e_tc_int_port *int_port;

	if (!priv)
		return ERR_PTR(-EOPNOTSUPP);

	mutex_lock(&priv->int_ports_lock);

	/* Reject request if ul rep not ready */
	if (!priv->ul_rep_rx_ready) {
		int_port = ERR_PTR(-EOPNOTSUPP);
		goto done;
	}

	int_port = mlx5e_int_port_lookup(priv, ifindex, type);
	if (int_port)
		goto done;

	/* Alloc and add new int port to list */
	int_port = mlx5e_int_port_add(priv, ifindex, type);

done:
	mutex_unlock(&priv->int_ports_lock);

	return int_port;
}

void
mlx5e_tc_int_port_put(struct mlx5e_tc_int_port_priv *priv,
		      struct mlx5e_tc_int_port *int_port)
{
	if (!refcount_dec_and_mutex_lock(&int_port->refcnt, &priv->int_ports_lock))
		return;

	mlx5e_int_port_remove(priv, int_port);
	mutex_unlock(&priv->int_ports_lock);
}

struct mlx5e_tc_int_port_priv *
mlx5e_tc_int_port_init(struct mlx5e_priv *priv)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_tc_int_port_priv *int_port_priv;
	u64 mapping_id;

	if (!mlx5e_tc_int_port_supported(esw))
		return NULL;

	int_port_priv = kzalloc(sizeof(*int_port_priv), GFP_KERNEL);
	if (!int_port_priv)
		return NULL;

	mapping_id = mlx5_query_nic_system_image_guid(priv->mdev);

	int_port_priv->metadata_mapping = mapping_create_for_id(mapping_id, MAPPING_TYPE_INT_PORT,
								sizeof(u32) * 2,
								(1 << ESW_VPORT_BITS) - 1, true);
	if (IS_ERR(int_port_priv->metadata_mapping)) {
		mlx5_core_warn(priv->mdev, "Can't allocate metadata mapping of int port offload, err=%ld\n",
			       PTR_ERR(int_port_priv->metadata_mapping));
		goto err_mapping;
	}

	int_port_priv->dev = priv->mdev;
	mutex_init(&int_port_priv->int_ports_lock);
	INIT_LIST_HEAD(&int_port_priv->int_ports);

	return int_port_priv;

err_mapping:
	kfree(int_port_priv);

	return NULL;
}

void
mlx5e_tc_int_port_cleanup(struct mlx5e_tc_int_port_priv *priv)
{
	if (!priv)
		return;

	mutex_destroy(&priv->int_ports_lock);
	mapping_destroy(priv->metadata_mapping);
	kfree(priv);
}

/* Int port rx rules reside in ul rep rx tables.
 * It is possible the ul rep will go down while there are
 * still int port rules in its rx table so proper cleanup
 * is required to free resources.
 */
void mlx5e_tc_int_port_init_rep_rx(struct mlx5e_priv *priv)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_rep_uplink_priv *uplink_priv;
	struct mlx5e_tc_int_port_priv *ppriv;
	struct mlx5e_rep_priv *uplink_rpriv;

	uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	uplink_priv = &uplink_rpriv->uplink_priv;

	ppriv = uplink_priv->int_port_priv;

	if (!ppriv)
		return;

	mutex_lock(&ppriv->int_ports_lock);
	ppriv->ul_rep_rx_ready = true;
	mutex_unlock(&ppriv->int_ports_lock);
}

void mlx5e_tc_int_port_cleanup_rep_rx(struct mlx5e_priv *priv)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_rep_uplink_priv *uplink_priv;
	struct mlx5e_tc_int_port_priv *ppriv;
	struct mlx5e_rep_priv *uplink_rpriv;
	struct mlx5e_tc_int_port *int_port;

	uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	uplink_priv = &uplink_rpriv->uplink_priv;

	ppriv = uplink_priv->int_port_priv;

	if (!ppriv)
		return;

	mutex_lock(&ppriv->int_ports_lock);

	ppriv->ul_rep_rx_ready = false;

	list_for_each_entry(int_port, &ppriv->int_ports, list) {
		if (!IS_ERR_OR_NULL(int_port->rx_rule))
			mlx5_del_flow_rules(int_port->rx_rule);

		int_port->rx_rule = NULL;
	}

	mutex_unlock(&ppriv->int_ports_lock);
}

bool
mlx5e_tc_int_port_dev_fwd(struct mlx5e_tc_int_port_priv *priv,
			  struct sk_buff *skb, u32 int_vport_metadata,
			  bool *forward_tx)
{
	enum mlx5e_tc_int_port_type fwd_type;
	struct mlx5e_tc_int_port *int_port;
	struct net_device *dev;
	int ifindex;

	if (!priv)
		return false;

	rcu_read_lock();
	int_port = mlx5e_int_port_get_from_metadata(priv, int_vport_metadata);
	if (!int_port) {
		rcu_read_unlock();
		mlx5_core_dbg(priv->dev, "Unable to find int port with metadata 0x%.8x\n",
			      int_vport_metadata);
		return false;
	}

	ifindex = int_port->ifindex;
	fwd_type = int_port->type;
	rcu_read_unlock();

	dev = dev_get_by_index(&init_net, ifindex);
	if (!dev) {
		mlx5_core_dbg(priv->dev,
			      "Couldn't find internal port device with ifindex: %d\n",
			      ifindex);
		return false;
	}

	skb->skb_iif = dev->ifindex;
	skb->dev = dev;

	if (fwd_type == MLX5E_TC_INT_PORT_INGRESS) {
		skb->pkt_type = PACKET_HOST;
		skb_set_redirected(skb, true);
		*forward_tx = false;
	} else {
		skb_reset_network_header(skb);
		skb_push_rcsum(skb, skb->mac_len);
		skb_set_redirected(skb, false);
		*forward_tx = true;
	}

	return true;
}
