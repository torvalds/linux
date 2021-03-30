// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021 Mellanox Technologies. */

#include <linux/netdevice.h>
#include <linux/list.h>
#include <linux/rhashtable.h>
#include <linux/if_bridge.h>
#include <net/switchdev.h>
#include "bridge.h"
#include "eswitch.h"
#include "fs_core.h"

#define MLX5_ESW_BRIDGE_INGRESS_TABLE_SIZE 64000
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_MAC_GRP_IDX_FROM 0
#define MLX5_ESW_BRIDGE_INGRESS_TABLE_MAC_GRP_IDX_TO (MLX5_ESW_BRIDGE_INGRESS_TABLE_SIZE - 1)

#define MLX5_ESW_BRIDGE_EGRESS_TABLE_SIZE 64000
#define MLX5_ESW_BRIDGE_EGRESS_TABLE_MAC_GRP_IDX_FROM 0
#define MLX5_ESW_BRIDGE_EGRESS_TABLE_MAC_GRP_IDX_TO (MLX5_ESW_BRIDGE_EGRESS_TABLE_SIZE - 1)

enum {
	MLX5_ESW_BRIDGE_LEVEL_INGRESS_TABLE,
	MLX5_ESW_BRIDGE_LEVEL_EGRESS_TABLE,
};

struct mlx5_esw_bridge_fdb_key {
	unsigned char addr[ETH_ALEN];
	u16 vid;
};

enum {
	MLX5_ESW_BRIDGE_FLAG_ADDED_BY_USER = BIT(0),
};

struct mlx5_esw_bridge_fdb_entry {
	struct mlx5_esw_bridge_fdb_key key;
	struct rhash_head ht_node;
	struct net_device *dev;
	struct list_head list;
	u16 vport_num;
	u16 flags;

	struct mlx5_flow_handle *ingress_handle;
	struct mlx5_fc *ingress_counter;
	unsigned long lastuse;
	struct mlx5_flow_handle *egress_handle;
};

static const struct rhashtable_params fdb_ht_params = {
	.key_offset = offsetof(struct mlx5_esw_bridge_fdb_entry, key),
	.key_len = sizeof(struct mlx5_esw_bridge_fdb_key),
	.head_offset = offsetof(struct mlx5_esw_bridge_fdb_entry, ht_node),
	.automatic_shrinking = true,
};

struct mlx5_esw_bridge {
	int ifindex;
	int refcnt;
	struct list_head list;
	struct mlx5_esw_bridge_offloads *br_offloads;

	struct list_head fdb_list;
	struct rhashtable fdb_ht;

	struct mlx5_flow_table *egress_ft;
	struct mlx5_flow_group *egress_mac_fg;
	unsigned long ageing_time;
};

static void
mlx5_esw_bridge_fdb_offload_notify(struct net_device *dev, const unsigned char *addr, u16 vid,
				   unsigned long val)
{
	struct switchdev_notifier_fdb_info send_info;

	send_info.addr = addr;
	send_info.vid = vid;
	send_info.offloaded = true;
	call_switchdev_notifiers(val, dev, &send_info.info, NULL);
}

static struct mlx5_flow_table *
mlx5_esw_bridge_table_create(int max_fte, u32 level, struct mlx5_eswitch *esw)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_table *fdb;

	ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_FDB);
	if (!ns) {
		esw_warn(dev, "Failed to get FDB namespace\n");
		return ERR_PTR(-ENOENT);
	}

	ft_attr.max_fte = max_fte;
	ft_attr.level = level;
	ft_attr.prio = FDB_BR_OFFLOAD;
	fdb = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(fdb))
		esw_warn(dev, "Failed to create bridge FDB Table (err=%ld)\n", PTR_ERR(fdb));

	return fdb;
}

static struct mlx5_flow_group *
mlx5_esw_bridge_ingress_mac_fg_create(struct mlx5_eswitch *esw, struct mlx5_flow_table *ingress_ft)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_group *fg;
	u32 *in, *match;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return ERR_PTR(-ENOMEM);

	MLX5_SET(create_flow_group_in, in, match_criteria_enable,
		 MLX5_MATCH_OUTER_HEADERS | MLX5_MATCH_MISC_PARAMETERS_2);
	match = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);

	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.smac_47_16);
	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.smac_15_0);

	MLX5_SET(fte_match_param, match, misc_parameters_2.metadata_reg_c_0,
		 mlx5_eswitch_get_vport_metadata_mask());

	MLX5_SET(create_flow_group_in, in, start_flow_index,
		 MLX5_ESW_BRIDGE_INGRESS_TABLE_MAC_GRP_IDX_FROM);
	MLX5_SET(create_flow_group_in, in, end_flow_index,
		 MLX5_ESW_BRIDGE_INGRESS_TABLE_MAC_GRP_IDX_TO);

	fg = mlx5_create_flow_group(ingress_ft, in);
	if (IS_ERR(fg))
		esw_warn(esw->dev,
			 "Failed to create bridge ingress table MAC flow group (err=%ld)\n",
			 PTR_ERR(fg));

	kvfree(in);
	return fg;
}

static struct mlx5_flow_group *
mlx5_esw_bridge_egress_mac_fg_create(struct mlx5_eswitch *esw, struct mlx5_flow_table *egress_ft)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_group *fg;
	u32 *in, *match;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return ERR_PTR(-ENOMEM);

	MLX5_SET(create_flow_group_in, in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	match = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);

	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.dmac_47_16);
	MLX5_SET_TO_ONES(fte_match_param, match, outer_headers.dmac_15_0);

	MLX5_SET(create_flow_group_in, in, start_flow_index,
		 MLX5_ESW_BRIDGE_EGRESS_TABLE_MAC_GRP_IDX_FROM);
	MLX5_SET(create_flow_group_in, in, end_flow_index,
		 MLX5_ESW_BRIDGE_EGRESS_TABLE_MAC_GRP_IDX_TO);

	fg = mlx5_create_flow_group(egress_ft, in);
	if (IS_ERR(fg))
		esw_warn(esw->dev,
			 "Failed to create bridge egress table MAC flow group (err=%ld)\n",
			 PTR_ERR(fg));
	kvfree(in);
	return fg;
}

static int
mlx5_esw_bridge_ingress_table_init(struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_flow_table *ingress_ft;
	struct mlx5_flow_group *mac_fg;
	int err;

	if (!mlx5_eswitch_vport_match_metadata_enabled(br_offloads->esw))
		return -EOPNOTSUPP;

	ingress_ft = mlx5_esw_bridge_table_create(MLX5_ESW_BRIDGE_INGRESS_TABLE_SIZE,
						  MLX5_ESW_BRIDGE_LEVEL_INGRESS_TABLE,
						  br_offloads->esw);
	if (IS_ERR(ingress_ft))
		return PTR_ERR(ingress_ft);

	mac_fg = mlx5_esw_bridge_ingress_mac_fg_create(br_offloads->esw, ingress_ft);
	if (IS_ERR(mac_fg)) {
		err = PTR_ERR(mac_fg);
		goto err_mac_fg;
	}

	br_offloads->ingress_ft = ingress_ft;
	br_offloads->ingress_mac_fg = mac_fg;
	return 0;

err_mac_fg:
	mlx5_destroy_flow_table(ingress_ft);
	return err;
}

static void
mlx5_esw_bridge_ingress_table_cleanup(struct mlx5_esw_bridge_offloads *br_offloads)
{
	mlx5_destroy_flow_group(br_offloads->ingress_mac_fg);
	br_offloads->ingress_mac_fg = NULL;
	mlx5_destroy_flow_table(br_offloads->ingress_ft);
	br_offloads->ingress_ft = NULL;
}

static int
mlx5_esw_bridge_egress_table_init(struct mlx5_esw_bridge_offloads *br_offloads,
				  struct mlx5_esw_bridge *bridge)
{
	struct mlx5_flow_table *egress_ft;
	struct mlx5_flow_group *mac_fg;
	int err;

	egress_ft = mlx5_esw_bridge_table_create(MLX5_ESW_BRIDGE_EGRESS_TABLE_SIZE,
						 MLX5_ESW_BRIDGE_LEVEL_EGRESS_TABLE,
						 br_offloads->esw);
	if (IS_ERR(egress_ft))
		return PTR_ERR(egress_ft);

	mac_fg = mlx5_esw_bridge_egress_mac_fg_create(br_offloads->esw, egress_ft);
	if (IS_ERR(mac_fg)) {
		err = PTR_ERR(mac_fg);
		goto err_mac_fg;
	}

	bridge->egress_ft = egress_ft;
	bridge->egress_mac_fg = mac_fg;
	return 0;

err_mac_fg:
	mlx5_destroy_flow_table(egress_ft);
	return err;
}

static void
mlx5_esw_bridge_egress_table_cleanup(struct mlx5_esw_bridge *bridge)
{
	mlx5_destroy_flow_group(bridge->egress_mac_fg);
	mlx5_destroy_flow_table(bridge->egress_ft);
}

static struct mlx5_flow_handle *
mlx5_esw_bridge_ingress_flow_create(u16 vport_num, const unsigned char *addr, u16 vid,
				    u32 counter_id, struct mlx5_esw_bridge *bridge)
{
	struct mlx5_esw_bridge_offloads *br_offloads = bridge->br_offloads;
	struct mlx5_flow_act flow_act = {
		.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST | MLX5_FLOW_CONTEXT_ACTION_COUNT,
		.flags = FLOW_ACT_NO_APPEND,
	};
	struct mlx5_flow_destination dests[2] = {};
	struct mlx5_flow_spec *rule_spec;
	struct mlx5_flow_handle *handle;
	u8 *smac_v, *smac_c;

	rule_spec = kvzalloc(sizeof(*rule_spec), GFP_KERNEL);
	if (!rule_spec)
		return ERR_PTR(-ENOMEM);

	rule_spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS | MLX5_MATCH_MISC_PARAMETERS_2;

	smac_v = MLX5_ADDR_OF(fte_match_param, rule_spec->match_value,
			      outer_headers.smac_47_16);
	ether_addr_copy(smac_v, addr);
	smac_c = MLX5_ADDR_OF(fte_match_param, rule_spec->match_criteria,
			      outer_headers.smac_47_16);
	eth_broadcast_addr(smac_c);

	MLX5_SET(fte_match_param, rule_spec->match_criteria,
		 misc_parameters_2.metadata_reg_c_0, mlx5_eswitch_get_vport_metadata_mask());
	MLX5_SET(fte_match_param, rule_spec->match_value, misc_parameters_2.metadata_reg_c_0,
		 mlx5_eswitch_get_vport_metadata_for_match(br_offloads->esw, vport_num));

	dests[0].type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dests[0].ft = bridge->egress_ft;
	dests[1].type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
	dests[1].counter_id = counter_id;

	handle = mlx5_add_flow_rules(br_offloads->ingress_ft, rule_spec, &flow_act, dests,
				     ARRAY_SIZE(dests));

	kvfree(rule_spec);
	return handle;
}

static struct mlx5_flow_handle *
mlx5_esw_bridge_egress_flow_create(u16 vport_num, const unsigned char *addr, u16 vid,
				   struct mlx5_esw_bridge *bridge)
{
	struct mlx5_flow_destination dest = {
		.type = MLX5_FLOW_DESTINATION_TYPE_VPORT,
		.vport.num = vport_num,
	};
	struct mlx5_flow_act flow_act = {
		.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
		.flags = FLOW_ACT_NO_APPEND,
	};
	struct mlx5_flow_spec *rule_spec;
	struct mlx5_flow_handle *handle;
	u8 *dmac_v, *dmac_c;

	rule_spec = kvzalloc(sizeof(*rule_spec), GFP_KERNEL);
	if (!rule_spec)
		return ERR_PTR(-ENOMEM);

	rule_spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;

	dmac_v = MLX5_ADDR_OF(fte_match_param, rule_spec->match_value,
			      outer_headers.dmac_47_16);
	ether_addr_copy(dmac_v, addr);
	dmac_c = MLX5_ADDR_OF(fte_match_param, rule_spec->match_criteria,
			      outer_headers.dmac_47_16);
	eth_broadcast_addr(dmac_c);

	handle = mlx5_add_flow_rules(bridge->egress_ft, rule_spec, &flow_act, &dest, 1);

	kvfree(rule_spec);
	return handle;
}

static struct mlx5_esw_bridge *mlx5_esw_bridge_create(int ifindex,
						      struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_esw_bridge *bridge;
	int err;

	bridge = kvzalloc(sizeof(*bridge), GFP_KERNEL);
	if (!bridge)
		return ERR_PTR(-ENOMEM);

	bridge->br_offloads = br_offloads;
	err = mlx5_esw_bridge_egress_table_init(br_offloads, bridge);
	if (err)
		goto err_egress_tbl;

	err = rhashtable_init(&bridge->fdb_ht, &fdb_ht_params);
	if (err)
		goto err_fdb_ht;

	INIT_LIST_HEAD(&bridge->fdb_list);
	bridge->ifindex = ifindex;
	bridge->refcnt = 1;
	bridge->ageing_time = BR_DEFAULT_AGEING_TIME;
	list_add(&bridge->list, &br_offloads->bridges);

	return bridge;

err_fdb_ht:
	mlx5_esw_bridge_egress_table_cleanup(bridge);
err_egress_tbl:
	kvfree(bridge);
	return ERR_PTR(err);
}

static void mlx5_esw_bridge_get(struct mlx5_esw_bridge *bridge)
{
	bridge->refcnt++;
}

static void mlx5_esw_bridge_put(struct mlx5_esw_bridge_offloads *br_offloads,
				struct mlx5_esw_bridge *bridge)
{
	if (--bridge->refcnt)
		return;

	mlx5_esw_bridge_egress_table_cleanup(bridge);
	list_del(&bridge->list);
	rhashtable_destroy(&bridge->fdb_ht);
	kvfree(bridge);

	if (list_empty(&br_offloads->bridges))
		mlx5_esw_bridge_ingress_table_cleanup(br_offloads);
}

static struct mlx5_esw_bridge *
mlx5_esw_bridge_lookup(int ifindex, struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_esw_bridge *bridge;

	ASSERT_RTNL();

	list_for_each_entry(bridge, &br_offloads->bridges, list) {
		if (bridge->ifindex == ifindex) {
			mlx5_esw_bridge_get(bridge);
			return bridge;
		}
	}

	if (!br_offloads->ingress_ft) {
		int err = mlx5_esw_bridge_ingress_table_init(br_offloads);

		if (err)
			return ERR_PTR(err);
	}

	bridge = mlx5_esw_bridge_create(ifindex, br_offloads);
	if (IS_ERR(bridge) && list_empty(&br_offloads->bridges))
		mlx5_esw_bridge_ingress_table_cleanup(br_offloads);
	return bridge;
}

static void
mlx5_esw_bridge_fdb_entry_cleanup(struct mlx5_esw_bridge_fdb_entry *entry,
				  struct mlx5_esw_bridge *bridge)
{
	rhashtable_remove_fast(&bridge->fdb_ht, &entry->ht_node, fdb_ht_params);
	mlx5_del_flow_rules(entry->egress_handle);
	mlx5_del_flow_rules(entry->ingress_handle);
	mlx5_fc_destroy(bridge->br_offloads->esw->dev, entry->ingress_counter);
	list_del(&entry->list);
	kvfree(entry);
}

static struct mlx5_esw_bridge_fdb_entry *
mlx5_esw_bridge_fdb_entry_init(struct net_device *dev, u16 vport_num, const unsigned char *addr,
			       u16 vid, bool added_by_user, struct mlx5_eswitch *esw,
			       struct mlx5_esw_bridge *bridge)
{
	struct mlx5_esw_bridge_fdb_entry *entry;
	struct mlx5_flow_handle *handle;
	struct mlx5_fc *counter;
	struct mlx5e_priv *priv;
	int err;

	priv = netdev_priv(dev);
	entry = kvzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return ERR_PTR(-ENOMEM);

	ether_addr_copy(entry->key.addr, addr);
	entry->key.vid = vid;
	entry->dev = dev;
	entry->vport_num = vport_num;
	entry->lastuse = jiffies;
	if (added_by_user)
		entry->flags |= MLX5_ESW_BRIDGE_FLAG_ADDED_BY_USER;

	counter = mlx5_fc_create(priv->mdev, true);
	if (IS_ERR(counter)) {
		err = PTR_ERR(counter);
		goto err_ingress_fc_create;
	}
	entry->ingress_counter = counter;

	handle = mlx5_esw_bridge_ingress_flow_create(vport_num, addr, vid, mlx5_fc_id(counter),
						     bridge);
	if (IS_ERR(handle)) {
		err = PTR_ERR(handle);
		esw_warn(esw->dev, "Failed to create ingress flow(vport=%u,err=%d)\n",
			 vport_num, err);
		goto err_ingress_flow_create;
	}
	entry->ingress_handle = handle;

	handle = mlx5_esw_bridge_egress_flow_create(vport_num, addr, vid, bridge);
	if (IS_ERR(handle)) {
		err = PTR_ERR(handle);
		esw_warn(esw->dev, "Failed to create egress flow(vport=%u,err=%d)\n",
			 vport_num, err);
		goto err_egress_flow_create;
	}
	entry->egress_handle = handle;

	err = rhashtable_insert_fast(&bridge->fdb_ht, &entry->ht_node, fdb_ht_params);
	if (err) {
		esw_warn(esw->dev, "Failed to insert FDB flow(vport=%u,err=%d)\n", vport_num, err);
		goto err_ht_init;
	}

	list_add(&entry->list, &bridge->fdb_list);
	return entry;

err_ht_init:
	mlx5_del_flow_rules(entry->egress_handle);
err_egress_flow_create:
	mlx5_del_flow_rules(entry->ingress_handle);
err_ingress_flow_create:
	mlx5_fc_destroy(priv->mdev, entry->ingress_counter);
err_ingress_fc_create:
	kvfree(entry);
	return ERR_PTR(err);
}

int mlx5_esw_bridge_ageing_time_set(unsigned long ageing_time, struct mlx5_eswitch *esw,
				    struct mlx5_vport *vport)
{
	if (!vport->bridge)
		return -EINVAL;

	vport->bridge->ageing_time = ageing_time;
	return 0;
}

static int mlx5_esw_bridge_vport_init(struct mlx5_esw_bridge *bridge,
				      struct mlx5_vport *vport)
{
	vport->bridge = bridge;
	return 0;
}

static int mlx5_esw_bridge_vport_cleanup(struct mlx5_esw_bridge_offloads *br_offloads,
					 struct mlx5_vport *vport)
{
	struct mlx5_esw_bridge *bridge = vport->bridge;
	struct mlx5_esw_bridge_fdb_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &bridge->fdb_list, list)
		if (entry->vport_num == vport->vport)
			mlx5_esw_bridge_fdb_entry_cleanup(entry, bridge);

	mlx5_esw_bridge_put(br_offloads, bridge);
	vport->bridge = NULL;
	return 0;
}

int mlx5_esw_bridge_vport_link(int ifindex, struct mlx5_esw_bridge_offloads *br_offloads,
			       struct mlx5_vport *vport, struct netlink_ext_ack *extack)
{
	struct mlx5_esw_bridge *bridge;

	WARN_ON(vport->bridge);

	bridge = mlx5_esw_bridge_lookup(ifindex, br_offloads);
	if (IS_ERR(bridge)) {
		NL_SET_ERR_MSG_MOD(extack, "Error checking for existing bridge with same ifindex");
		return PTR_ERR(bridge);
	}

	return mlx5_esw_bridge_vport_init(bridge, vport);
}

int mlx5_esw_bridge_vport_unlink(int ifindex, struct mlx5_esw_bridge_offloads *br_offloads,
				 struct mlx5_vport *vport, struct netlink_ext_ack *extack)
{
	struct mlx5_esw_bridge *bridge = vport->bridge;

	if (!bridge) {
		NL_SET_ERR_MSG_MOD(extack, "Port is not attached to any bridge");
		return -EINVAL;
	}
	if (bridge->ifindex != ifindex) {
		NL_SET_ERR_MSG_MOD(extack, "Port is attached to another bridge");
		return -EINVAL;
	}

	return mlx5_esw_bridge_vport_cleanup(br_offloads, vport);
}

void mlx5_esw_bridge_fdb_create(struct net_device *dev, struct mlx5_eswitch *esw,
				struct mlx5_vport *vport,
				struct switchdev_notifier_fdb_info *fdb_info)
{
	struct mlx5_esw_bridge *bridge = vport->bridge;
	struct mlx5_esw_bridge_fdb_entry *entry;
	u16 vport_num = vport->vport;

	if (!bridge) {
		esw_info(esw->dev, "Vport is not assigned to bridge (vport=%u)\n", vport_num);
		return;
	}

	entry = mlx5_esw_bridge_fdb_entry_init(dev, vport_num, fdb_info->addr, fdb_info->vid,
					       fdb_info->added_by_user, esw, bridge);
	if (IS_ERR(entry))
		return;

	if (entry->flags & MLX5_ESW_BRIDGE_FLAG_ADDED_BY_USER)
		mlx5_esw_bridge_fdb_offload_notify(dev, entry->key.addr, entry->key.vid,
						   SWITCHDEV_FDB_OFFLOADED);
	else
		/* Take over dynamic entries to prevent kernel bridge from aging them out. */
		mlx5_esw_bridge_fdb_offload_notify(dev, entry->key.addr, entry->key.vid,
						   SWITCHDEV_FDB_ADD_TO_BRIDGE);
}

void mlx5_esw_bridge_fdb_remove(struct net_device *dev, struct mlx5_eswitch *esw,
				struct mlx5_vport *vport,
				struct switchdev_notifier_fdb_info *fdb_info)
{
	struct mlx5_esw_bridge *bridge = vport->bridge;
	struct mlx5_esw_bridge_fdb_entry *entry;
	struct mlx5_esw_bridge_fdb_key key;
	u16 vport_num = vport->vport;

	if (!bridge) {
		esw_warn(esw->dev, "Vport is not assigned to bridge (vport=%u)\n", vport_num);
		return;
	}

	ether_addr_copy(key.addr, fdb_info->addr);
	key.vid = fdb_info->vid;
	entry = rhashtable_lookup_fast(&bridge->fdb_ht, &key, fdb_ht_params);
	if (!entry) {
		esw_warn(esw->dev,
			 "FDB entry with specified key not found (MAC=%pM,vid=%u,vport=%u)\n",
			 key.addr, key.vid, vport_num);
		return;
	}

	if (!(entry->flags & MLX5_ESW_BRIDGE_FLAG_ADDED_BY_USER))
		mlx5_esw_bridge_fdb_offload_notify(dev, entry->key.addr, entry->key.vid,
						   SWITCHDEV_FDB_DEL_TO_BRIDGE);
	mlx5_esw_bridge_fdb_entry_cleanup(entry, bridge);
}

void mlx5_esw_bridge_update(struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_esw_bridge_fdb_entry *entry, *tmp;
	struct mlx5_esw_bridge *bridge;

	list_for_each_entry(bridge, &br_offloads->bridges, list) {
		list_for_each_entry_safe(entry, tmp, &bridge->fdb_list, list) {
			unsigned long lastuse =
				(unsigned long)mlx5_fc_query_lastuse(entry->ingress_counter);

			if (entry->flags & MLX5_ESW_BRIDGE_FLAG_ADDED_BY_USER)
				continue;

			if (time_after(lastuse, entry->lastuse)) {
				entry->lastuse = lastuse;
				/* refresh existing bridge entry */
				mlx5_esw_bridge_fdb_offload_notify(entry->dev, entry->key.addr,
								   entry->key.vid,
								   SWITCHDEV_FDB_ADD_TO_BRIDGE);
			} else if (time_is_before_jiffies(entry->lastuse + bridge->ageing_time)) {
				mlx5_esw_bridge_fdb_offload_notify(entry->dev, entry->key.addr,
								   entry->key.vid,
								   SWITCHDEV_FDB_DEL_TO_BRIDGE);
				mlx5_esw_bridge_fdb_entry_cleanup(entry, bridge);
			}
		}
	}
}

static void mlx5_esw_bridge_flush(struct mlx5_esw_bridge_offloads *br_offloads)
{
	struct mlx5_eswitch *esw = br_offloads->esw;
	struct mlx5_vport *vport;
	unsigned long i;

	mlx5_esw_for_each_vport(esw, i, vport)
		if (vport->bridge)
			mlx5_esw_bridge_vport_cleanup(br_offloads, vport);

	WARN_ONCE(!list_empty(&br_offloads->bridges),
		  "Cleaning up bridge offloads while still having bridges attached\n");
}

struct mlx5_esw_bridge_offloads *mlx5_esw_bridge_init(struct mlx5_eswitch *esw)
{
	struct mlx5_esw_bridge_offloads *br_offloads;

	br_offloads = kvzalloc(sizeof(*br_offloads), GFP_KERNEL);
	if (!br_offloads)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&br_offloads->bridges);
	br_offloads->esw = esw;
	esw->br_offloads = br_offloads;

	return br_offloads;
}

void mlx5_esw_bridge_cleanup(struct mlx5_eswitch *esw)
{
	struct mlx5_esw_bridge_offloads *br_offloads = esw->br_offloads;

	if (!br_offloads)
		return;

	mlx5_esw_bridge_flush(br_offloads);

	esw->br_offloads = NULL;
	kvfree(br_offloads);
}
