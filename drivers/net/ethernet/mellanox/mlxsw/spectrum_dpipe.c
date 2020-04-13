// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <net/devlink.h>

#include "spectrum.h"
#include "spectrum_dpipe.h"
#include "spectrum_router.h"

enum mlxsw_sp_field_metadata_id {
	MLXSW_SP_DPIPE_FIELD_METADATA_ERIF_PORT,
	MLXSW_SP_DPIPE_FIELD_METADATA_L3_FORWARD,
	MLXSW_SP_DPIPE_FIELD_METADATA_L3_DROP,
	MLXSW_SP_DPIPE_FIELD_METADATA_ADJ_INDEX,
	MLXSW_SP_DPIPE_FIELD_METADATA_ADJ_SIZE,
	MLXSW_SP_DPIPE_FIELD_METADATA_ADJ_HASH_INDEX,
};

static struct devlink_dpipe_field mlxsw_sp_dpipe_fields_metadata[] = {
	{
		.name = "erif_port",
		.id = MLXSW_SP_DPIPE_FIELD_METADATA_ERIF_PORT,
		.bitwidth = 32,
		.mapping_type = DEVLINK_DPIPE_FIELD_MAPPING_TYPE_IFINDEX,
	},
	{
		.name = "l3_forward",
		.id = MLXSW_SP_DPIPE_FIELD_METADATA_L3_FORWARD,
		.bitwidth = 1,
	},
	{
		.name = "l3_drop",
		.id = MLXSW_SP_DPIPE_FIELD_METADATA_L3_DROP,
		.bitwidth = 1,
	},
	{
		.name = "adj_index",
		.id = MLXSW_SP_DPIPE_FIELD_METADATA_ADJ_INDEX,
		.bitwidth = 32,
	},
	{
		.name = "adj_size",
		.id = MLXSW_SP_DPIPE_FIELD_METADATA_ADJ_SIZE,
		.bitwidth = 32,
	},
	{
		.name = "adj_hash_index",
		.id = MLXSW_SP_DPIPE_FIELD_METADATA_ADJ_HASH_INDEX,
		.bitwidth = 32,
	},
};

enum mlxsw_sp_dpipe_header_id {
	MLXSW_SP_DPIPE_HEADER_METADATA,
};

static struct devlink_dpipe_header mlxsw_sp_dpipe_header_metadata = {
	.name = "mlxsw_meta",
	.id = MLXSW_SP_DPIPE_HEADER_METADATA,
	.fields = mlxsw_sp_dpipe_fields_metadata,
	.fields_count = ARRAY_SIZE(mlxsw_sp_dpipe_fields_metadata),
};

static struct devlink_dpipe_header *mlxsw_dpipe_headers[] = {
	&mlxsw_sp_dpipe_header_metadata,
	&devlink_dpipe_header_ethernet,
	&devlink_dpipe_header_ipv4,
	&devlink_dpipe_header_ipv6,
};

static struct devlink_dpipe_headers mlxsw_sp_dpipe_headers = {
	.headers = mlxsw_dpipe_headers,
	.headers_count = ARRAY_SIZE(mlxsw_dpipe_headers),
};

static int mlxsw_sp_dpipe_table_erif_actions_dump(void *priv,
						  struct sk_buff *skb)
{
	struct devlink_dpipe_action action = {0};
	int err;

	action.type = DEVLINK_DPIPE_ACTION_TYPE_FIELD_MODIFY;
	action.header = &mlxsw_sp_dpipe_header_metadata;
	action.field_id = MLXSW_SP_DPIPE_FIELD_METADATA_L3_FORWARD;

	err = devlink_dpipe_action_put(skb, &action);
	if (err)
		return err;

	action.type = DEVLINK_DPIPE_ACTION_TYPE_FIELD_MODIFY;
	action.header = &mlxsw_sp_dpipe_header_metadata;
	action.field_id = MLXSW_SP_DPIPE_FIELD_METADATA_L3_DROP;

	return devlink_dpipe_action_put(skb, &action);
}

static int mlxsw_sp_dpipe_table_erif_matches_dump(void *priv,
						  struct sk_buff *skb)
{
	struct devlink_dpipe_match match = {0};

	match.type = DEVLINK_DPIPE_MATCH_TYPE_FIELD_EXACT;
	match.header = &mlxsw_sp_dpipe_header_metadata;
	match.field_id = MLXSW_SP_DPIPE_FIELD_METADATA_ERIF_PORT;

	return devlink_dpipe_match_put(skb, &match);
}

static void
mlxsw_sp_erif_match_action_prepare(struct devlink_dpipe_match *match,
				   struct devlink_dpipe_action *action)
{
	action->type = DEVLINK_DPIPE_ACTION_TYPE_FIELD_MODIFY;
	action->header = &mlxsw_sp_dpipe_header_metadata;
	action->field_id = MLXSW_SP_DPIPE_FIELD_METADATA_L3_FORWARD;

	match->type = DEVLINK_DPIPE_MATCH_TYPE_FIELD_EXACT;
	match->header = &mlxsw_sp_dpipe_header_metadata;
	match->field_id = MLXSW_SP_DPIPE_FIELD_METADATA_ERIF_PORT;
}

static int mlxsw_sp_erif_entry_prepare(struct devlink_dpipe_entry *entry,
				       struct devlink_dpipe_value *match_value,
				       struct devlink_dpipe_match *match,
				       struct devlink_dpipe_value *action_value,
				       struct devlink_dpipe_action *action)
{
	entry->match_values = match_value;
	entry->match_values_count = 1;

	entry->action_values = action_value;
	entry->action_values_count = 1;

	match_value->match = match;
	match_value->value_size = sizeof(u32);
	match_value->value = kmalloc(match_value->value_size, GFP_KERNEL);
	if (!match_value->value)
		return -ENOMEM;

	action_value->action = action;
	action_value->value_size = sizeof(u32);
	action_value->value = kmalloc(action_value->value_size, GFP_KERNEL);
	if (!action_value->value)
		goto err_action_alloc;
	return 0;

err_action_alloc:
	kfree(match_value->value);
	return -ENOMEM;
}

static int mlxsw_sp_erif_entry_get(struct mlxsw_sp *mlxsw_sp,
				   struct devlink_dpipe_entry *entry,
				   struct mlxsw_sp_rif *rif,
				   bool counters_enabled)
{
	u32 *action_value;
	u32 *rif_value;
	u64 cnt;
	int err;

	/* Set Match RIF index */
	rif_value = entry->match_values->value;
	*rif_value = mlxsw_sp_rif_index(rif);
	entry->match_values->mapping_value = mlxsw_sp_rif_dev_ifindex(rif);
	entry->match_values->mapping_valid = true;

	/* Set Action Forwarding */
	action_value = entry->action_values->value;
	*action_value = 1;

	entry->counter_valid = false;
	entry->counter = 0;
	entry->index = mlxsw_sp_rif_index(rif);

	if (!counters_enabled)
		return 0;

	err = mlxsw_sp_rif_counter_value_get(mlxsw_sp, rif,
					     MLXSW_SP_RIF_COUNTER_EGRESS,
					     &cnt);
	if (!err) {
		entry->counter = cnt;
		entry->counter_valid = true;
	}
	return 0;
}

static int
mlxsw_sp_dpipe_table_erif_entries_dump(void *priv, bool counters_enabled,
				       struct devlink_dpipe_dump_ctx *dump_ctx)
{
	struct devlink_dpipe_value match_value, action_value;
	struct devlink_dpipe_action action = {0};
	struct devlink_dpipe_match match = {0};
	struct devlink_dpipe_entry entry = {0};
	struct mlxsw_sp *mlxsw_sp = priv;
	unsigned int rif_count;
	int i, j;
	int err;

	memset(&match_value, 0, sizeof(match_value));
	memset(&action_value, 0, sizeof(action_value));

	mlxsw_sp_erif_match_action_prepare(&match, &action);
	err = mlxsw_sp_erif_entry_prepare(&entry, &match_value, &match,
					  &action_value, &action);
	if (err)
		return err;

	rif_count = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS);
	mutex_lock(&mlxsw_sp->router->lock);
	i = 0;
start_again:
	err = devlink_dpipe_entry_ctx_prepare(dump_ctx);
	if (err)
		goto err_ctx_prepare;
	j = 0;
	for (; i < rif_count; i++) {
		struct mlxsw_sp_rif *rif = mlxsw_sp_rif_by_index(mlxsw_sp, i);

		if (!rif || !mlxsw_sp_rif_dev(rif))
			continue;
		err = mlxsw_sp_erif_entry_get(mlxsw_sp, &entry, rif,
					      counters_enabled);
		if (err)
			goto err_entry_get;
		err = devlink_dpipe_entry_ctx_append(dump_ctx, &entry);
		if (err) {
			if (err == -EMSGSIZE) {
				if (!j)
					goto err_entry_append;
				break;
			}
			goto err_entry_append;
		}
		j++;
	}

	devlink_dpipe_entry_ctx_close(dump_ctx);
	if (i != rif_count)
		goto start_again;
	mutex_unlock(&mlxsw_sp->router->lock);

	devlink_dpipe_entry_clear(&entry);
	return 0;
err_entry_append:
err_entry_get:
err_ctx_prepare:
	mutex_unlock(&mlxsw_sp->router->lock);
	devlink_dpipe_entry_clear(&entry);
	return err;
}

static int mlxsw_sp_dpipe_table_erif_counters_update(void *priv, bool enable)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	int i;

	mutex_lock(&mlxsw_sp->router->lock);
	for (i = 0; i < MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS); i++) {
		struct mlxsw_sp_rif *rif = mlxsw_sp_rif_by_index(mlxsw_sp, i);

		if (!rif)
			continue;
		if (enable)
			mlxsw_sp_rif_counter_alloc(mlxsw_sp, rif,
						   MLXSW_SP_RIF_COUNTER_EGRESS);
		else
			mlxsw_sp_rif_counter_free(mlxsw_sp, rif,
						  MLXSW_SP_RIF_COUNTER_EGRESS);
	}
	mutex_unlock(&mlxsw_sp->router->lock);
	return 0;
}

static u64 mlxsw_sp_dpipe_table_erif_size_get(void *priv)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	return MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS);
}

static struct devlink_dpipe_table_ops mlxsw_sp_erif_ops = {
	.matches_dump = mlxsw_sp_dpipe_table_erif_matches_dump,
	.actions_dump = mlxsw_sp_dpipe_table_erif_actions_dump,
	.entries_dump = mlxsw_sp_dpipe_table_erif_entries_dump,
	.counters_set_update = mlxsw_sp_dpipe_table_erif_counters_update,
	.size_get = mlxsw_sp_dpipe_table_erif_size_get,
};

static int mlxsw_sp_dpipe_erif_table_init(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);

	return devlink_dpipe_table_register(devlink,
					    MLXSW_SP_DPIPE_TABLE_NAME_ERIF,
					    &mlxsw_sp_erif_ops,
					    mlxsw_sp, false);
}

static void mlxsw_sp_dpipe_erif_table_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);

	devlink_dpipe_table_unregister(devlink, MLXSW_SP_DPIPE_TABLE_NAME_ERIF);
}

static int mlxsw_sp_dpipe_table_host_matches_dump(struct sk_buff *skb, int type)
{
	struct devlink_dpipe_match match = {0};
	int err;

	match.type = DEVLINK_DPIPE_MATCH_TYPE_FIELD_EXACT;
	match.header = &mlxsw_sp_dpipe_header_metadata;
	match.field_id = MLXSW_SP_DPIPE_FIELD_METADATA_ERIF_PORT;

	err = devlink_dpipe_match_put(skb, &match);
	if (err)
		return err;

	switch (type) {
	case AF_INET:
		match.type = DEVLINK_DPIPE_MATCH_TYPE_FIELD_EXACT;
		match.header = &devlink_dpipe_header_ipv4;
		match.field_id = DEVLINK_DPIPE_FIELD_IPV4_DST_IP;
		break;
	case AF_INET6:
		match.type = DEVLINK_DPIPE_MATCH_TYPE_FIELD_EXACT;
		match.header = &devlink_dpipe_header_ipv6;
		match.field_id = DEVLINK_DPIPE_FIELD_IPV6_DST_IP;
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	return devlink_dpipe_match_put(skb, &match);
}

static int
mlxsw_sp_dpipe_table_host4_matches_dump(void *priv, struct sk_buff *skb)
{
	return mlxsw_sp_dpipe_table_host_matches_dump(skb, AF_INET);
}

static int
mlxsw_sp_dpipe_table_host_actions_dump(void *priv, struct sk_buff *skb)
{
	struct devlink_dpipe_action action = {0};

	action.type = DEVLINK_DPIPE_ACTION_TYPE_FIELD_MODIFY;
	action.header = &devlink_dpipe_header_ethernet;
	action.field_id = DEVLINK_DPIPE_FIELD_ETHERNET_DST_MAC;

	return devlink_dpipe_action_put(skb, &action);
}

enum mlxsw_sp_dpipe_table_host_match {
	MLXSW_SP_DPIPE_TABLE_HOST_MATCH_RIF,
	MLXSW_SP_DPIPE_TABLE_HOST_MATCH_DIP,
	MLXSW_SP_DPIPE_TABLE_HOST_MATCH_COUNT,
};

static void
mlxsw_sp_dpipe_table_host_match_action_prepare(struct devlink_dpipe_match *matches,
					       struct devlink_dpipe_action *action,
					       int type)
{
	struct devlink_dpipe_match *match;

	match = &matches[MLXSW_SP_DPIPE_TABLE_HOST_MATCH_RIF];
	match->type = DEVLINK_DPIPE_MATCH_TYPE_FIELD_EXACT;
	match->header = &mlxsw_sp_dpipe_header_metadata;
	match->field_id = MLXSW_SP_DPIPE_FIELD_METADATA_ERIF_PORT;

	match = &matches[MLXSW_SP_DPIPE_TABLE_HOST_MATCH_DIP];
	match->type = DEVLINK_DPIPE_MATCH_TYPE_FIELD_EXACT;
	switch (type) {
	case AF_INET:
		match->header = &devlink_dpipe_header_ipv4;
		match->field_id = DEVLINK_DPIPE_FIELD_IPV4_DST_IP;
		break;
	case AF_INET6:
		match->header = &devlink_dpipe_header_ipv6;
		match->field_id = DEVLINK_DPIPE_FIELD_IPV6_DST_IP;
		break;
	default:
		WARN_ON(1);
		return;
	}

	action->type = DEVLINK_DPIPE_ACTION_TYPE_FIELD_MODIFY;
	action->header = &devlink_dpipe_header_ethernet;
	action->field_id = DEVLINK_DPIPE_FIELD_ETHERNET_DST_MAC;
}

static int
mlxsw_sp_dpipe_table_host_entry_prepare(struct devlink_dpipe_entry *entry,
					struct devlink_dpipe_value *match_values,
					struct devlink_dpipe_match *matches,
					struct devlink_dpipe_value *action_value,
					struct devlink_dpipe_action *action,
					int type)
{
	struct devlink_dpipe_value *match_value;
	struct devlink_dpipe_match *match;

	entry->match_values = match_values;
	entry->match_values_count = MLXSW_SP_DPIPE_TABLE_HOST_MATCH_COUNT;

	entry->action_values = action_value;
	entry->action_values_count = 1;

	match = &matches[MLXSW_SP_DPIPE_TABLE_HOST_MATCH_RIF];
	match_value = &match_values[MLXSW_SP_DPIPE_TABLE_HOST_MATCH_RIF];

	match_value->match = match;
	match_value->value_size = sizeof(u32);
	match_value->value = kmalloc(match_value->value_size, GFP_KERNEL);
	if (!match_value->value)
		return -ENOMEM;

	match = &matches[MLXSW_SP_DPIPE_TABLE_HOST_MATCH_DIP];
	match_value = &match_values[MLXSW_SP_DPIPE_TABLE_HOST_MATCH_DIP];

	match_value->match = match;
	switch (type) {
	case AF_INET:
		match_value->value_size = sizeof(u32);
		break;
	case AF_INET6:
		match_value->value_size = sizeof(struct in6_addr);
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	match_value->value = kmalloc(match_value->value_size, GFP_KERNEL);
	if (!match_value->value)
		return -ENOMEM;

	action_value->action = action;
	action_value->value_size = sizeof(u64);
	action_value->value = kmalloc(action_value->value_size, GFP_KERNEL);
	if (!action_value->value)
		return -ENOMEM;

	return 0;
}

static void
__mlxsw_sp_dpipe_table_host_entry_fill(struct devlink_dpipe_entry *entry,
				       struct mlxsw_sp_rif *rif,
				       unsigned char *ha, void *dip)
{
	struct devlink_dpipe_value *value;
	u32 *rif_value;
	u8 *ha_value;

	/* Set Match RIF index */
	value = &entry->match_values[MLXSW_SP_DPIPE_TABLE_HOST_MATCH_RIF];

	rif_value = value->value;
	*rif_value = mlxsw_sp_rif_index(rif);
	value->mapping_value = mlxsw_sp_rif_dev_ifindex(rif);
	value->mapping_valid = true;

	/* Set Match DIP */
	value = &entry->match_values[MLXSW_SP_DPIPE_TABLE_HOST_MATCH_DIP];
	memcpy(value->value, dip, value->value_size);

	/* Set Action DMAC */
	value = entry->action_values;
	ha_value = value->value;
	ether_addr_copy(ha_value, ha);
}

static void
mlxsw_sp_dpipe_table_host4_entry_fill(struct devlink_dpipe_entry *entry,
				      struct mlxsw_sp_neigh_entry *neigh_entry,
				      struct mlxsw_sp_rif *rif)
{
	unsigned char *ha;
	u32 dip;

	ha = mlxsw_sp_neigh_entry_ha(neigh_entry);
	dip = mlxsw_sp_neigh4_entry_dip(neigh_entry);
	__mlxsw_sp_dpipe_table_host_entry_fill(entry, rif, ha, &dip);
}

static void
mlxsw_sp_dpipe_table_host6_entry_fill(struct devlink_dpipe_entry *entry,
				      struct mlxsw_sp_neigh_entry *neigh_entry,
				      struct mlxsw_sp_rif *rif)
{
	struct in6_addr *dip;
	unsigned char *ha;

	ha = mlxsw_sp_neigh_entry_ha(neigh_entry);
	dip = mlxsw_sp_neigh6_entry_dip(neigh_entry);

	__mlxsw_sp_dpipe_table_host_entry_fill(entry, rif, ha, dip);
}

static void
mlxsw_sp_dpipe_table_host_entry_fill(struct mlxsw_sp *mlxsw_sp,
				     struct devlink_dpipe_entry *entry,
				     struct mlxsw_sp_neigh_entry *neigh_entry,
				     struct mlxsw_sp_rif *rif,
				     int type)
{
	int err;

	switch (type) {
	case AF_INET:
		mlxsw_sp_dpipe_table_host4_entry_fill(entry, neigh_entry, rif);
		break;
	case AF_INET6:
		mlxsw_sp_dpipe_table_host6_entry_fill(entry, neigh_entry, rif);
		break;
	default:
		WARN_ON(1);
		return;
	}

	err = mlxsw_sp_neigh_counter_get(mlxsw_sp, neigh_entry,
					 &entry->counter);
	if (!err)
		entry->counter_valid = true;
}

static int
mlxsw_sp_dpipe_table_host_entries_get(struct mlxsw_sp *mlxsw_sp,
				      struct devlink_dpipe_entry *entry,
				      bool counters_enabled,
				      struct devlink_dpipe_dump_ctx *dump_ctx,
				      int type)
{
	int rif_neigh_count = 0;
	int rif_neigh_skip = 0;
	int neigh_count = 0;
	int rif_count;
	int i, j;
	int err;

	mutex_lock(&mlxsw_sp->router->lock);
	i = 0;
	rif_count = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS);
start_again:
	err = devlink_dpipe_entry_ctx_prepare(dump_ctx);
	if (err)
		goto err_ctx_prepare;
	j = 0;
	rif_neigh_skip = rif_neigh_count;
	for (; i < MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS); i++) {
		struct mlxsw_sp_rif *rif = mlxsw_sp_rif_by_index(mlxsw_sp, i);
		struct mlxsw_sp_neigh_entry *neigh_entry;

		if (!rif)
			continue;

		rif_neigh_count = 0;
		mlxsw_sp_rif_neigh_for_each(neigh_entry, rif) {
			int neigh_type = mlxsw_sp_neigh_entry_type(neigh_entry);

			if (neigh_type != type)
				continue;

			if (neigh_type == AF_INET6 &&
			    mlxsw_sp_neigh_ipv6_ignore(neigh_entry))
				continue;

			if (rif_neigh_count < rif_neigh_skip)
				goto skip;

			mlxsw_sp_dpipe_table_host_entry_fill(mlxsw_sp, entry,
							     neigh_entry, rif,
							     type);
			entry->index = neigh_count;
			err = devlink_dpipe_entry_ctx_append(dump_ctx, entry);
			if (err) {
				if (err == -EMSGSIZE) {
					if (!j)
						goto err_entry_append;
					else
						goto out;
				}
				goto err_entry_append;
			}
			neigh_count++;
			j++;
skip:
			rif_neigh_count++;
		}
		rif_neigh_skip = 0;
	}
out:
	devlink_dpipe_entry_ctx_close(dump_ctx);
	if (i != rif_count)
		goto start_again;

	mutex_unlock(&mlxsw_sp->router->lock);
	return 0;

err_ctx_prepare:
err_entry_append:
	mutex_unlock(&mlxsw_sp->router->lock);
	return err;
}

static int
mlxsw_sp_dpipe_table_host_entries_dump(struct mlxsw_sp *mlxsw_sp,
				       bool counters_enabled,
				       struct devlink_dpipe_dump_ctx *dump_ctx,
				       int type)
{
	struct devlink_dpipe_value match_values[MLXSW_SP_DPIPE_TABLE_HOST_MATCH_COUNT];
	struct devlink_dpipe_match matches[MLXSW_SP_DPIPE_TABLE_HOST_MATCH_COUNT];
	struct devlink_dpipe_value action_value;
	struct devlink_dpipe_action action = {0};
	struct devlink_dpipe_entry entry = {0};
	int err;

	memset(matches, 0, MLXSW_SP_DPIPE_TABLE_HOST_MATCH_COUNT *
			   sizeof(matches[0]));
	memset(match_values, 0, MLXSW_SP_DPIPE_TABLE_HOST_MATCH_COUNT *
				sizeof(match_values[0]));
	memset(&action_value, 0, sizeof(action_value));

	mlxsw_sp_dpipe_table_host_match_action_prepare(matches, &action, type);
	err = mlxsw_sp_dpipe_table_host_entry_prepare(&entry, match_values,
						      matches, &action_value,
						      &action, type);
	if (err)
		goto out;

	err = mlxsw_sp_dpipe_table_host_entries_get(mlxsw_sp, &entry,
						    counters_enabled, dump_ctx,
						    type);
out:
	devlink_dpipe_entry_clear(&entry);
	return err;
}

static int
mlxsw_sp_dpipe_table_host4_entries_dump(void *priv, bool counters_enabled,
					struct devlink_dpipe_dump_ctx *dump_ctx)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	return mlxsw_sp_dpipe_table_host_entries_dump(mlxsw_sp,
						      counters_enabled,
						      dump_ctx, AF_INET);
}

static void
mlxsw_sp_dpipe_table_host_counters_update(struct mlxsw_sp *mlxsw_sp,
					  bool enable, int type)
{
	int i;

	mutex_lock(&mlxsw_sp->router->lock);
	for (i = 0; i < MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS); i++) {
		struct mlxsw_sp_rif *rif = mlxsw_sp_rif_by_index(mlxsw_sp, i);
		struct mlxsw_sp_neigh_entry *neigh_entry;

		if (!rif)
			continue;
		mlxsw_sp_rif_neigh_for_each(neigh_entry, rif) {
			int neigh_type = mlxsw_sp_neigh_entry_type(neigh_entry);

			if (neigh_type != type)
				continue;

			if (neigh_type == AF_INET6 &&
			    mlxsw_sp_neigh_ipv6_ignore(neigh_entry))
				continue;

			mlxsw_sp_neigh_entry_counter_update(mlxsw_sp,
							    neigh_entry,
							    enable);
		}
	}
	mutex_unlock(&mlxsw_sp->router->lock);
}

static int mlxsw_sp_dpipe_table_host4_counters_update(void *priv, bool enable)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	mlxsw_sp_dpipe_table_host_counters_update(mlxsw_sp, enable, AF_INET);
	return 0;
}

static u64
mlxsw_sp_dpipe_table_host_size_get(struct mlxsw_sp *mlxsw_sp, int type)
{
	u64 size = 0;
	int i;

	mutex_lock(&mlxsw_sp->router->lock);
	for (i = 0; i < MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_RIFS); i++) {
		struct mlxsw_sp_rif *rif = mlxsw_sp_rif_by_index(mlxsw_sp, i);
		struct mlxsw_sp_neigh_entry *neigh_entry;

		if (!rif)
			continue;
		mlxsw_sp_rif_neigh_for_each(neigh_entry, rif) {
			int neigh_type = mlxsw_sp_neigh_entry_type(neigh_entry);

			if (neigh_type != type)
				continue;

			if (neigh_type == AF_INET6 &&
			    mlxsw_sp_neigh_ipv6_ignore(neigh_entry))
				continue;

			size++;
		}
	}
	mutex_unlock(&mlxsw_sp->router->lock);

	return size;
}

static u64 mlxsw_sp_dpipe_table_host4_size_get(void *priv)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	return mlxsw_sp_dpipe_table_host_size_get(mlxsw_sp, AF_INET);
}

static struct devlink_dpipe_table_ops mlxsw_sp_host4_ops = {
	.matches_dump = mlxsw_sp_dpipe_table_host4_matches_dump,
	.actions_dump = mlxsw_sp_dpipe_table_host_actions_dump,
	.entries_dump = mlxsw_sp_dpipe_table_host4_entries_dump,
	.counters_set_update = mlxsw_sp_dpipe_table_host4_counters_update,
	.size_get = mlxsw_sp_dpipe_table_host4_size_get,
};

#define MLXSW_SP_DPIPE_TABLE_RESOURCE_UNIT_HOST4 1

static int mlxsw_sp_dpipe_host4_table_init(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	int err;

	err = devlink_dpipe_table_register(devlink,
					   MLXSW_SP_DPIPE_TABLE_NAME_HOST4,
					   &mlxsw_sp_host4_ops,
					   mlxsw_sp, false);
	if (err)
		return err;

	err = devlink_dpipe_table_resource_set(devlink,
					       MLXSW_SP_DPIPE_TABLE_NAME_HOST4,
					       MLXSW_SP_RESOURCE_KVD_HASH_SINGLE,
					       MLXSW_SP_DPIPE_TABLE_RESOURCE_UNIT_HOST4);
	if (err)
		goto err_resource_set;

	return 0;

err_resource_set:
	devlink_dpipe_table_unregister(devlink,
				       MLXSW_SP_DPIPE_TABLE_NAME_HOST4);
	return err;
}

static void mlxsw_sp_dpipe_host4_table_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);

	devlink_dpipe_table_unregister(devlink,
				       MLXSW_SP_DPIPE_TABLE_NAME_HOST4);
}

static int
mlxsw_sp_dpipe_table_host6_matches_dump(void *priv, struct sk_buff *skb)
{
	return mlxsw_sp_dpipe_table_host_matches_dump(skb, AF_INET6);
}

static int
mlxsw_sp_dpipe_table_host6_entries_dump(void *priv, bool counters_enabled,
					struct devlink_dpipe_dump_ctx *dump_ctx)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	return mlxsw_sp_dpipe_table_host_entries_dump(mlxsw_sp,
						      counters_enabled,
						      dump_ctx, AF_INET6);
}

static int mlxsw_sp_dpipe_table_host6_counters_update(void *priv, bool enable)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	mlxsw_sp_dpipe_table_host_counters_update(mlxsw_sp, enable, AF_INET6);
	return 0;
}

static u64 mlxsw_sp_dpipe_table_host6_size_get(void *priv)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	return mlxsw_sp_dpipe_table_host_size_get(mlxsw_sp, AF_INET6);
}

static struct devlink_dpipe_table_ops mlxsw_sp_host6_ops = {
	.matches_dump = mlxsw_sp_dpipe_table_host6_matches_dump,
	.actions_dump = mlxsw_sp_dpipe_table_host_actions_dump,
	.entries_dump = mlxsw_sp_dpipe_table_host6_entries_dump,
	.counters_set_update = mlxsw_sp_dpipe_table_host6_counters_update,
	.size_get = mlxsw_sp_dpipe_table_host6_size_get,
};

#define MLXSW_SP_DPIPE_TABLE_RESOURCE_UNIT_HOST6 2

static int mlxsw_sp_dpipe_host6_table_init(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	int err;

	err = devlink_dpipe_table_register(devlink,
					   MLXSW_SP_DPIPE_TABLE_NAME_HOST6,
					   &mlxsw_sp_host6_ops,
					   mlxsw_sp, false);
	if (err)
		return err;

	err = devlink_dpipe_table_resource_set(devlink,
					       MLXSW_SP_DPIPE_TABLE_NAME_HOST6,
					       MLXSW_SP_RESOURCE_KVD_HASH_DOUBLE,
					       MLXSW_SP_DPIPE_TABLE_RESOURCE_UNIT_HOST6);
	if (err)
		goto err_resource_set;

	return 0;

err_resource_set:
	devlink_dpipe_table_unregister(devlink,
				       MLXSW_SP_DPIPE_TABLE_NAME_HOST6);
	return err;
}

static void mlxsw_sp_dpipe_host6_table_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);

	devlink_dpipe_table_unregister(devlink,
				       MLXSW_SP_DPIPE_TABLE_NAME_HOST6);
}

static int mlxsw_sp_dpipe_table_adj_matches_dump(void *priv,
						 struct sk_buff *skb)
{
	struct devlink_dpipe_match match = {0};
	int err;

	match.type = DEVLINK_DPIPE_MATCH_TYPE_FIELD_EXACT;
	match.header = &mlxsw_sp_dpipe_header_metadata;
	match.field_id = MLXSW_SP_DPIPE_FIELD_METADATA_ADJ_INDEX;

	err = devlink_dpipe_match_put(skb, &match);
	if (err)
		return err;

	match.type = DEVLINK_DPIPE_MATCH_TYPE_FIELD_EXACT;
	match.header = &mlxsw_sp_dpipe_header_metadata;
	match.field_id = MLXSW_SP_DPIPE_FIELD_METADATA_ADJ_SIZE;

	err = devlink_dpipe_match_put(skb, &match);
	if (err)
		return err;

	match.type = DEVLINK_DPIPE_MATCH_TYPE_FIELD_EXACT;
	match.header = &mlxsw_sp_dpipe_header_metadata;
	match.field_id = MLXSW_SP_DPIPE_FIELD_METADATA_ADJ_HASH_INDEX;

	return devlink_dpipe_match_put(skb, &match);
}

static int mlxsw_sp_dpipe_table_adj_actions_dump(void *priv,
						 struct sk_buff *skb)
{
	struct devlink_dpipe_action action = {0};
	int err;

	action.type = DEVLINK_DPIPE_ACTION_TYPE_FIELD_MODIFY;
	action.header = &devlink_dpipe_header_ethernet;
	action.field_id = DEVLINK_DPIPE_FIELD_ETHERNET_DST_MAC;

	err = devlink_dpipe_action_put(skb, &action);
	if (err)
		return err;

	action.type = DEVLINK_DPIPE_ACTION_TYPE_FIELD_MODIFY;
	action.header = &mlxsw_sp_dpipe_header_metadata;
	action.field_id = MLXSW_SP_DPIPE_FIELD_METADATA_ERIF_PORT;

	return devlink_dpipe_action_put(skb, &action);
}

static u64 mlxsw_sp_dpipe_table_adj_size(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_nexthop *nh;
	u64 size = 0;

	mlxsw_sp_nexthop_for_each(nh, mlxsw_sp->router)
		if (mlxsw_sp_nexthop_offload(nh) &&
		    !mlxsw_sp_nexthop_group_has_ipip(nh))
			size++;
	return size;
}

enum mlxsw_sp_dpipe_table_adj_match {
	MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_INDEX,
	MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_SIZE,
	MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_HASH_INDEX,
	MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_COUNT,
};

enum mlxsw_sp_dpipe_table_adj_action {
	MLXSW_SP_DPIPE_TABLE_ADJ_ACTION_DST_MAC,
	MLXSW_SP_DPIPE_TABLE_ADJ_ACTION_ERIF_PORT,
	MLXSW_SP_DPIPE_TABLE_ADJ_ACTION_COUNT,
};

static void
mlxsw_sp_dpipe_table_adj_match_action_prepare(struct devlink_dpipe_match *matches,
					      struct devlink_dpipe_action *actions)
{
	struct devlink_dpipe_action *action;
	struct devlink_dpipe_match *match;

	match = &matches[MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_INDEX];
	match->type = DEVLINK_DPIPE_MATCH_TYPE_FIELD_EXACT;
	match->header = &mlxsw_sp_dpipe_header_metadata;
	match->field_id = MLXSW_SP_DPIPE_FIELD_METADATA_ADJ_INDEX;

	match = &matches[MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_SIZE];
	match->type = DEVLINK_DPIPE_MATCH_TYPE_FIELD_EXACT;
	match->header = &mlxsw_sp_dpipe_header_metadata;
	match->field_id = MLXSW_SP_DPIPE_FIELD_METADATA_ADJ_SIZE;

	match = &matches[MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_HASH_INDEX];
	match->type = DEVLINK_DPIPE_MATCH_TYPE_FIELD_EXACT;
	match->header = &mlxsw_sp_dpipe_header_metadata;
	match->field_id = MLXSW_SP_DPIPE_FIELD_METADATA_ADJ_HASH_INDEX;

	action = &actions[MLXSW_SP_DPIPE_TABLE_ADJ_ACTION_DST_MAC];
	action->type = DEVLINK_DPIPE_ACTION_TYPE_FIELD_MODIFY;
	action->header = &devlink_dpipe_header_ethernet;
	action->field_id = DEVLINK_DPIPE_FIELD_ETHERNET_DST_MAC;

	action = &actions[MLXSW_SP_DPIPE_TABLE_ADJ_ACTION_ERIF_PORT];
	action->type = DEVLINK_DPIPE_ACTION_TYPE_FIELD_MODIFY;
	action->header = &mlxsw_sp_dpipe_header_metadata;
	action->field_id = MLXSW_SP_DPIPE_FIELD_METADATA_ERIF_PORT;
}

static int
mlxsw_sp_dpipe_table_adj_entry_prepare(struct devlink_dpipe_entry *entry,
				       struct devlink_dpipe_value *match_values,
				       struct devlink_dpipe_match *matches,
				       struct devlink_dpipe_value *action_values,
				       struct devlink_dpipe_action *actions)
{	struct devlink_dpipe_value *action_value;
	struct devlink_dpipe_value *match_value;
	struct devlink_dpipe_action *action;
	struct devlink_dpipe_match *match;

	entry->match_values = match_values;
	entry->match_values_count = MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_COUNT;

	entry->action_values = action_values;
	entry->action_values_count = MLXSW_SP_DPIPE_TABLE_ADJ_ACTION_COUNT;

	match = &matches[MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_INDEX];
	match_value = &match_values[MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_INDEX];

	match_value->match = match;
	match_value->value_size = sizeof(u32);
	match_value->value = kmalloc(match_value->value_size, GFP_KERNEL);
	if (!match_value->value)
		return -ENOMEM;

	match = &matches[MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_SIZE];
	match_value = &match_values[MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_SIZE];

	match_value->match = match;
	match_value->value_size = sizeof(u32);
	match_value->value = kmalloc(match_value->value_size, GFP_KERNEL);
	if (!match_value->value)
		return -ENOMEM;

	match = &matches[MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_HASH_INDEX];
	match_value = &match_values[MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_HASH_INDEX];

	match_value->match = match;
	match_value->value_size = sizeof(u32);
	match_value->value = kmalloc(match_value->value_size, GFP_KERNEL);
	if (!match_value->value)
		return -ENOMEM;

	action = &actions[MLXSW_SP_DPIPE_TABLE_ADJ_ACTION_DST_MAC];
	action_value = &action_values[MLXSW_SP_DPIPE_TABLE_ADJ_ACTION_DST_MAC];

	action_value->action = action;
	action_value->value_size = sizeof(u64);
	action_value->value = kmalloc(action_value->value_size, GFP_KERNEL);
	if (!action_value->value)
		return -ENOMEM;

	action = &actions[MLXSW_SP_DPIPE_TABLE_ADJ_ACTION_ERIF_PORT];
	action_value = &action_values[MLXSW_SP_DPIPE_TABLE_ADJ_ACTION_ERIF_PORT];

	action_value->action = action;
	action_value->value_size = sizeof(u32);
	action_value->value = kmalloc(action_value->value_size, GFP_KERNEL);
	if (!action_value->value)
		return -ENOMEM;

	return 0;
}

static void
__mlxsw_sp_dpipe_table_adj_entry_fill(struct devlink_dpipe_entry *entry,
				      u32 adj_index, u32 adj_size,
				      u32 adj_hash_index, unsigned char *ha,
				      struct mlxsw_sp_rif *rif)
{
	struct devlink_dpipe_value *value;
	u32 *p_rif_value;
	u32 *p_index;

	value = &entry->match_values[MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_INDEX];
	p_index = value->value;
	*p_index = adj_index;

	value = &entry->match_values[MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_SIZE];
	p_index = value->value;
	*p_index = adj_size;

	value = &entry->match_values[MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_HASH_INDEX];
	p_index = value->value;
	*p_index = adj_hash_index;

	value = &entry->action_values[MLXSW_SP_DPIPE_TABLE_ADJ_ACTION_DST_MAC];
	ether_addr_copy(value->value, ha);

	value = &entry->action_values[MLXSW_SP_DPIPE_TABLE_ADJ_ACTION_ERIF_PORT];
	p_rif_value = value->value;
	*p_rif_value = mlxsw_sp_rif_index(rif);
	value->mapping_value = mlxsw_sp_rif_dev_ifindex(rif);
	value->mapping_valid = true;
}

static void mlxsw_sp_dpipe_table_adj_entry_fill(struct mlxsw_sp *mlxsw_sp,
						struct mlxsw_sp_nexthop *nh,
						struct devlink_dpipe_entry *entry)
{
	struct mlxsw_sp_rif *rif = mlxsw_sp_nexthop_rif(nh);
	unsigned char *ha = mlxsw_sp_nexthop_ha(nh);
	u32 adj_hash_index = 0;
	u32 adj_index = 0;
	u32 adj_size = 0;
	int err;

	mlxsw_sp_nexthop_indexes(nh, &adj_index, &adj_size, &adj_hash_index);
	__mlxsw_sp_dpipe_table_adj_entry_fill(entry, adj_index, adj_size,
					      adj_hash_index, ha, rif);
	err = mlxsw_sp_nexthop_counter_get(mlxsw_sp, nh, &entry->counter);
	if (!err)
		entry->counter_valid = true;
}

static int
mlxsw_sp_dpipe_table_adj_entries_get(struct mlxsw_sp *mlxsw_sp,
				     struct devlink_dpipe_entry *entry,
				     bool counters_enabled,
				     struct devlink_dpipe_dump_ctx *dump_ctx)
{
	struct mlxsw_sp_nexthop *nh;
	int entry_index = 0;
	int nh_count_max;
	int nh_count = 0;
	int nh_skip;
	int j;
	int err;

	mutex_lock(&mlxsw_sp->router->lock);
	nh_count_max = mlxsw_sp_dpipe_table_adj_size(mlxsw_sp);
start_again:
	err = devlink_dpipe_entry_ctx_prepare(dump_ctx);
	if (err)
		goto err_ctx_prepare;
	j = 0;
	nh_skip = nh_count;
	nh_count = 0;
	mlxsw_sp_nexthop_for_each(nh, mlxsw_sp->router) {
		if (!mlxsw_sp_nexthop_offload(nh) ||
		    mlxsw_sp_nexthop_group_has_ipip(nh))
			continue;

		if (nh_count < nh_skip)
			goto skip;

		mlxsw_sp_dpipe_table_adj_entry_fill(mlxsw_sp, nh, entry);
		entry->index = entry_index;
		err = devlink_dpipe_entry_ctx_append(dump_ctx, entry);
		if (err) {
			if (err == -EMSGSIZE) {
				if (!j)
					goto err_entry_append;
				break;
			}
			goto err_entry_append;
		}
		entry_index++;
		j++;
skip:
		nh_count++;
	}

	devlink_dpipe_entry_ctx_close(dump_ctx);
	if (nh_count != nh_count_max)
		goto start_again;
	mutex_unlock(&mlxsw_sp->router->lock);

	return 0;

err_ctx_prepare:
err_entry_append:
	mutex_unlock(&mlxsw_sp->router->lock);
	return err;
}

static int
mlxsw_sp_dpipe_table_adj_entries_dump(void *priv, bool counters_enabled,
				      struct devlink_dpipe_dump_ctx *dump_ctx)
{
	struct devlink_dpipe_value action_values[MLXSW_SP_DPIPE_TABLE_ADJ_ACTION_COUNT];
	struct devlink_dpipe_value match_values[MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_COUNT];
	struct devlink_dpipe_action actions[MLXSW_SP_DPIPE_TABLE_ADJ_ACTION_COUNT];
	struct devlink_dpipe_match matches[MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_COUNT];
	struct devlink_dpipe_entry entry = {0};
	struct mlxsw_sp *mlxsw_sp = priv;
	int err;

	memset(matches, 0, MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_COUNT *
			   sizeof(matches[0]));
	memset(match_values, 0, MLXSW_SP_DPIPE_TABLE_ADJ_MATCH_COUNT *
				sizeof(match_values[0]));
	memset(actions, 0, MLXSW_SP_DPIPE_TABLE_ADJ_ACTION_COUNT *
			   sizeof(actions[0]));
	memset(action_values, 0, MLXSW_SP_DPIPE_TABLE_ADJ_ACTION_COUNT *
				 sizeof(action_values[0]));

	mlxsw_sp_dpipe_table_adj_match_action_prepare(matches, actions);
	err = mlxsw_sp_dpipe_table_adj_entry_prepare(&entry,
						     match_values, matches,
						     action_values, actions);
	if (err)
		goto out;

	err = mlxsw_sp_dpipe_table_adj_entries_get(mlxsw_sp, &entry,
						   counters_enabled, dump_ctx);
out:
	devlink_dpipe_entry_clear(&entry);
	return err;
}

static int mlxsw_sp_dpipe_table_adj_counters_update(void *priv, bool enable)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	struct mlxsw_sp_nexthop *nh;
	u32 adj_hash_index = 0;
	u32 adj_index = 0;
	u32 adj_size = 0;

	mlxsw_sp_nexthop_for_each(nh, mlxsw_sp->router) {
		if (!mlxsw_sp_nexthop_offload(nh) ||
		    mlxsw_sp_nexthop_group_has_ipip(nh))
			continue;

		mlxsw_sp_nexthop_indexes(nh, &adj_index, &adj_size,
					 &adj_hash_index);
		if (enable)
			mlxsw_sp_nexthop_counter_alloc(mlxsw_sp, nh);
		else
			mlxsw_sp_nexthop_counter_free(mlxsw_sp, nh);
		mlxsw_sp_nexthop_update(mlxsw_sp,
					adj_index + adj_hash_index, nh);
	}
	return 0;
}

static u64
mlxsw_sp_dpipe_table_adj_size_get(void *priv)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	u64 size;

	mutex_lock(&mlxsw_sp->router->lock);
	size = mlxsw_sp_dpipe_table_adj_size(mlxsw_sp);
	mutex_unlock(&mlxsw_sp->router->lock);

	return size;
}

static struct devlink_dpipe_table_ops mlxsw_sp_dpipe_table_adj_ops = {
	.matches_dump = mlxsw_sp_dpipe_table_adj_matches_dump,
	.actions_dump = mlxsw_sp_dpipe_table_adj_actions_dump,
	.entries_dump = mlxsw_sp_dpipe_table_adj_entries_dump,
	.counters_set_update = mlxsw_sp_dpipe_table_adj_counters_update,
	.size_get = mlxsw_sp_dpipe_table_adj_size_get,
};

#define MLXSW_SP_DPIPE_TABLE_RESOURCE_UNIT_ADJ 1

static int mlxsw_sp_dpipe_adj_table_init(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	int err;

	err = devlink_dpipe_table_register(devlink,
					   MLXSW_SP_DPIPE_TABLE_NAME_ADJ,
					   &mlxsw_sp_dpipe_table_adj_ops,
					   mlxsw_sp, false);
	if (err)
		return err;

	err = devlink_dpipe_table_resource_set(devlink,
					       MLXSW_SP_DPIPE_TABLE_NAME_ADJ,
					       MLXSW_SP_RESOURCE_KVD_LINEAR,
					       MLXSW_SP_DPIPE_TABLE_RESOURCE_UNIT_ADJ);
	if (err)
		goto err_resource_set;

	return 0;

err_resource_set:
	devlink_dpipe_table_unregister(devlink,
				       MLXSW_SP_DPIPE_TABLE_NAME_ADJ);
	return err;
}

static void mlxsw_sp_dpipe_adj_table_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);

	devlink_dpipe_table_unregister(devlink,
				       MLXSW_SP_DPIPE_TABLE_NAME_ADJ);
}

int mlxsw_sp_dpipe_init(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	int err;

	err = devlink_dpipe_headers_register(devlink,
					     &mlxsw_sp_dpipe_headers);
	if (err)
		return err;
	err = mlxsw_sp_dpipe_erif_table_init(mlxsw_sp);
	if (err)
		goto err_erif_table_init;

	err = mlxsw_sp_dpipe_host4_table_init(mlxsw_sp);
	if (err)
		goto err_host4_table_init;

	err = mlxsw_sp_dpipe_host6_table_init(mlxsw_sp);
	if (err)
		goto err_host6_table_init;

	err = mlxsw_sp_dpipe_adj_table_init(mlxsw_sp);
	if (err)
		goto err_adj_table_init;

	return 0;
err_adj_table_init:
	mlxsw_sp_dpipe_host6_table_fini(mlxsw_sp);
err_host6_table_init:
	mlxsw_sp_dpipe_host4_table_fini(mlxsw_sp);
err_host4_table_init:
	mlxsw_sp_dpipe_erif_table_fini(mlxsw_sp);
err_erif_table_init:
	devlink_dpipe_headers_unregister(priv_to_devlink(mlxsw_sp->core));
	return err;
}

void mlxsw_sp_dpipe_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);

	mlxsw_sp_dpipe_adj_table_fini(mlxsw_sp);
	mlxsw_sp_dpipe_host6_table_fini(mlxsw_sp);
	mlxsw_sp_dpipe_host4_table_fini(mlxsw_sp);
	mlxsw_sp_dpipe_erif_table_fini(mlxsw_sp);
	devlink_dpipe_headers_unregister(devlink);
}
