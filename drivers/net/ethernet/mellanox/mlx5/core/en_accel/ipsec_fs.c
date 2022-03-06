// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */

#include <linux/netdevice.h>
#include "ipsec_offload.h"
#include "ipsec_fs.h"
#include "fs_core.h"

#define NUM_IPSEC_FTE BIT(15)

enum accel_fs_esp_type {
	ACCEL_FS_ESP4,
	ACCEL_FS_ESP6,
	ACCEL_FS_ESP_NUM_TYPES,
};

struct mlx5e_ipsec_rx_err {
	struct mlx5_flow_table *ft;
	struct mlx5_flow_handle *rule;
	struct mlx5_modify_hdr *copy_modify_hdr;
};

struct mlx5e_accel_fs_esp_prot {
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *miss_group;
	struct mlx5_flow_handle *miss_rule;
	struct mlx5_flow_destination default_dest;
	struct mlx5e_ipsec_rx_err rx_err;
	u32 refcnt;
	struct mutex prot_mutex; /* protect ESP4/ESP6 protocol */
};

struct mlx5e_accel_fs_esp {
	struct mlx5e_accel_fs_esp_prot fs_prot[ACCEL_FS_ESP_NUM_TYPES];
};

struct mlx5e_ipsec_tx {
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_table *ft;
	struct mutex mutex; /* Protect IPsec TX steering */
	u32 refcnt;
};

/* IPsec RX flow steering */
static enum mlx5_traffic_types fs_esp2tt(enum accel_fs_esp_type i)
{
	if (i == ACCEL_FS_ESP4)
		return MLX5_TT_IPV4_IPSEC_ESP;
	return MLX5_TT_IPV6_IPSEC_ESP;
}

static int rx_err_add_rule(struct mlx5e_priv *priv,
			   struct mlx5e_accel_fs_esp_prot *fs_prot,
			   struct mlx5e_ipsec_rx_err *rx_err)
{
	u8 action[MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)] = {};
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_flow_act flow_act = {};
	struct mlx5_modify_hdr *modify_hdr;
	struct mlx5_flow_handle *fte;
	struct mlx5_flow_spec *spec;
	int err = 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	/* Action to copy 7 bit ipsec_syndrome to regB[24:30] */
	MLX5_SET(copy_action_in, action, action_type, MLX5_ACTION_TYPE_COPY);
	MLX5_SET(copy_action_in, action, src_field, MLX5_ACTION_IN_FIELD_IPSEC_SYNDROME);
	MLX5_SET(copy_action_in, action, src_offset, 0);
	MLX5_SET(copy_action_in, action, length, 7);
	MLX5_SET(copy_action_in, action, dst_field, MLX5_ACTION_IN_FIELD_METADATA_REG_B);
	MLX5_SET(copy_action_in, action, dst_offset, 24);

	modify_hdr = mlx5_modify_header_alloc(mdev, MLX5_FLOW_NAMESPACE_KERNEL,
					      1, action);

	if (IS_ERR(modify_hdr)) {
		err = PTR_ERR(modify_hdr);
		netdev_err(priv->netdev,
			   "fail to alloc ipsec copy modify_header_id err=%d\n", err);
		goto out_spec;
	}

	/* create fte */
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_MOD_HDR |
			  MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	flow_act.modify_hdr = modify_hdr;
	fte = mlx5_add_flow_rules(rx_err->ft, spec, &flow_act,
				  &fs_prot->default_dest, 1);
	if (IS_ERR(fte)) {
		err = PTR_ERR(fte);
		netdev_err(priv->netdev, "fail to add ipsec rx err copy rule err=%d\n", err);
		goto out;
	}

	rx_err->rule = fte;
	rx_err->copy_modify_hdr = modify_hdr;

out:
	if (err)
		mlx5_modify_header_dealloc(mdev, modify_hdr);
out_spec:
	kvfree(spec);
	return err;
}

static void rx_err_del_rule(struct mlx5e_priv *priv,
			    struct mlx5e_ipsec_rx_err *rx_err)
{
	if (rx_err->rule) {
		mlx5_del_flow_rules(rx_err->rule);
		rx_err->rule = NULL;
	}

	if (rx_err->copy_modify_hdr) {
		mlx5_modify_header_dealloc(priv->mdev, rx_err->copy_modify_hdr);
		rx_err->copy_modify_hdr = NULL;
	}
}

static void rx_err_destroy_ft(struct mlx5e_priv *priv, struct mlx5e_ipsec_rx_err *rx_err)
{
	rx_err_del_rule(priv, rx_err);

	if (rx_err->ft) {
		mlx5_destroy_flow_table(rx_err->ft);
		rx_err->ft = NULL;
	}
}

static int rx_err_create_ft(struct mlx5e_priv *priv,
			    struct mlx5e_accel_fs_esp_prot *fs_prot,
			    struct mlx5e_ipsec_rx_err *rx_err)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_table *ft;
	int err;

	ft_attr.max_fte = 1;
	ft_attr.autogroup.max_num_groups = 1;
	ft_attr.level = MLX5E_ACCEL_FS_ESP_FT_ERR_LEVEL;
	ft_attr.prio = MLX5E_NIC_PRIO;
	ft = mlx5_create_auto_grouped_flow_table(priv->fs.ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		netdev_err(priv->netdev, "fail to create ipsec rx inline ft err=%d\n", err);
		return err;
	}

	rx_err->ft = ft;
	err = rx_err_add_rule(priv, fs_prot, rx_err);
	if (err)
		goto out_err;

	return 0;

out_err:
	mlx5_destroy_flow_table(ft);
	rx_err->ft = NULL;
	return err;
}

static void rx_fs_destroy(struct mlx5e_accel_fs_esp_prot *fs_prot)
{
	if (fs_prot->miss_rule) {
		mlx5_del_flow_rules(fs_prot->miss_rule);
		fs_prot->miss_rule = NULL;
	}

	if (fs_prot->miss_group) {
		mlx5_destroy_flow_group(fs_prot->miss_group);
		fs_prot->miss_group = NULL;
	}

	if (fs_prot->ft) {
		mlx5_destroy_flow_table(fs_prot->ft);
		fs_prot->ft = NULL;
	}
}

static int rx_fs_create(struct mlx5e_priv *priv,
			struct mlx5e_accel_fs_esp_prot *fs_prot)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_group *miss_group;
	struct mlx5_flow_handle *miss_rule;
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_spec *spec;
	struct mlx5_flow_table *ft;
	u32 *flow_group_in;
	int err = 0;

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!flow_group_in || !spec) {
		err = -ENOMEM;
		goto out;
	}

	/* Create FT */
	ft_attr.max_fte = NUM_IPSEC_FTE;
	ft_attr.level = MLX5E_ACCEL_FS_ESP_FT_LEVEL;
	ft_attr.prio = MLX5E_NIC_PRIO;
	ft_attr.autogroup.num_reserved_entries = 1;
	ft_attr.autogroup.max_num_groups = 1;
	ft = mlx5_create_auto_grouped_flow_table(priv->fs.ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		netdev_err(priv->netdev, "fail to create ipsec rx ft err=%d\n", err);
		goto out;
	}
	fs_prot->ft = ft;

	/* Create miss_group */
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, ft->max_fte - 1);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, ft->max_fte - 1);
	miss_group = mlx5_create_flow_group(ft, flow_group_in);
	if (IS_ERR(miss_group)) {
		err = PTR_ERR(miss_group);
		netdev_err(priv->netdev, "fail to create ipsec rx miss_group err=%d\n", err);
		goto out;
	}
	fs_prot->miss_group = miss_group;

	/* Create miss rule */
	miss_rule = mlx5_add_flow_rules(ft, spec, &flow_act, &fs_prot->default_dest, 1);
	if (IS_ERR(miss_rule)) {
		err = PTR_ERR(miss_rule);
		netdev_err(priv->netdev, "fail to create ipsec rx miss_rule err=%d\n", err);
		goto out;
	}
	fs_prot->miss_rule = miss_rule;

out:
	kvfree(flow_group_in);
	kvfree(spec);
	return err;
}

static int rx_destroy(struct mlx5e_priv *priv, enum accel_fs_esp_type type)
{
	struct mlx5e_accel_fs_esp_prot *fs_prot;
	struct mlx5e_accel_fs_esp *accel_esp;

	accel_esp = priv->ipsec->rx_fs;

	/* The netdev unreg already happened, so all offloaded rule are already removed */
	fs_prot = &accel_esp->fs_prot[type];

	rx_fs_destroy(fs_prot);

	rx_err_destroy_ft(priv, &fs_prot->rx_err);

	return 0;
}

static int rx_create(struct mlx5e_priv *priv, enum accel_fs_esp_type type)
{
	struct mlx5e_accel_fs_esp_prot *fs_prot;
	struct mlx5e_accel_fs_esp *accel_esp;
	int err;

	accel_esp = priv->ipsec->rx_fs;
	fs_prot = &accel_esp->fs_prot[type];

	fs_prot->default_dest =
		mlx5_ttc_get_default_dest(priv->fs.ttc, fs_esp2tt(type));

	err = rx_err_create_ft(priv, fs_prot, &fs_prot->rx_err);
	if (err)
		return err;

	err = rx_fs_create(priv, fs_prot);
	if (err)
		rx_destroy(priv, type);

	return err;
}

static int rx_ft_get(struct mlx5e_priv *priv, enum accel_fs_esp_type type)
{
	struct mlx5e_accel_fs_esp_prot *fs_prot;
	struct mlx5_flow_destination dest = {};
	struct mlx5e_accel_fs_esp *accel_esp;
	int err = 0;

	accel_esp = priv->ipsec->rx_fs;
	fs_prot = &accel_esp->fs_prot[type];
	mutex_lock(&fs_prot->prot_mutex);
	if (fs_prot->refcnt++)
		goto out;

	/* create FT */
	err = rx_create(priv, type);
	if (err) {
		fs_prot->refcnt--;
		goto out;
	}

	/* connect */
	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = fs_prot->ft;
	mlx5_ttc_fwd_dest(priv->fs.ttc, fs_esp2tt(type), &dest);

out:
	mutex_unlock(&fs_prot->prot_mutex);
	return err;
}

static void rx_ft_put(struct mlx5e_priv *priv, enum accel_fs_esp_type type)
{
	struct mlx5e_accel_fs_esp_prot *fs_prot;
	struct mlx5e_accel_fs_esp *accel_esp;

	accel_esp = priv->ipsec->rx_fs;
	fs_prot = &accel_esp->fs_prot[type];
	mutex_lock(&fs_prot->prot_mutex);
	if (--fs_prot->refcnt)
		goto out;

	/* disconnect */
	mlx5_ttc_fwd_default_dest(priv->fs.ttc, fs_esp2tt(type));

	/* remove FT */
	rx_destroy(priv, type);

out:
	mutex_unlock(&fs_prot->prot_mutex);
}

/* IPsec TX flow steering */
static int tx_create(struct mlx5e_priv *priv)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5e_ipsec *ipsec = priv->ipsec;
	struct mlx5_flow_table *ft;
	int err;

	ft_attr.max_fte = NUM_IPSEC_FTE;
	ft_attr.autogroup.max_num_groups = 1;
	ft = mlx5_create_auto_grouped_flow_table(ipsec->tx_fs->ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		netdev_err(priv->netdev, "fail to create ipsec tx ft err=%d\n", err);
		return err;
	}
	ipsec->tx_fs->ft = ft;
	return 0;
}

static void tx_destroy(struct mlx5e_priv *priv)
{
	struct mlx5e_ipsec *ipsec = priv->ipsec;

	if (IS_ERR_OR_NULL(ipsec->tx_fs->ft))
		return;

	mlx5_destroy_flow_table(ipsec->tx_fs->ft);
	ipsec->tx_fs->ft = NULL;
}

static int tx_ft_get(struct mlx5e_priv *priv)
{
	struct mlx5e_ipsec_tx *tx_fs = priv->ipsec->tx_fs;
	int err = 0;

	mutex_lock(&tx_fs->mutex);
	if (tx_fs->refcnt++)
		goto out;

	err = tx_create(priv);
	if (err) {
		tx_fs->refcnt--;
		goto out;
	}

out:
	mutex_unlock(&tx_fs->mutex);
	return err;
}

static void tx_ft_put(struct mlx5e_priv *priv)
{
	struct mlx5e_ipsec_tx *tx_fs = priv->ipsec->tx_fs;

	mutex_lock(&tx_fs->mutex);
	if (--tx_fs->refcnt)
		goto out;

	tx_destroy(priv);

out:
	mutex_unlock(&tx_fs->mutex);
}

static void setup_fte_common(struct mlx5_accel_esp_xfrm_attrs *attrs,
			     u32 ipsec_obj_id,
			     struct mlx5_flow_spec *spec,
			     struct mlx5_flow_act *flow_act)
{
	u8 ip_version = attrs->is_ipv6 ? 6 : 4;

	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS | MLX5_MATCH_MISC_PARAMETERS;

	/* ip_version */
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_version);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_version, ip_version);

	/* Non fragmented */
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.frag);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.frag, 0);

	/* ESP header */
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_protocol);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_protocol, IPPROTO_ESP);

	/* SPI number */
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, misc_parameters.outer_esp_spi);
	MLX5_SET(fte_match_param, spec->match_value, misc_parameters.outer_esp_spi,
		 be32_to_cpu(attrs->spi));

	if (ip_version == 4) {
		memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    outer_headers.src_ipv4_src_ipv6.ipv4_layout.ipv4),
		       &attrs->saddr.a4, 4);
		memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
		       &attrs->daddr.a4, 4);
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.src_ipv4_src_ipv6.ipv4_layout.ipv4);
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4);
	} else {
		memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    outer_headers.src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       &attrs->saddr.a6, 16);
		memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    outer_headers.dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       &attrs->daddr.a6, 16);
		memset(MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				    outer_headers.src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       0xff, 16);
		memset(MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				    outer_headers.dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       0xff, 16);
	}

	flow_act->ipsec_obj_id = ipsec_obj_id;
	flow_act->flags |= FLOW_ACT_NO_APPEND;
}

static int rx_add_rule(struct mlx5e_priv *priv,
		       struct mlx5_accel_esp_xfrm_attrs *attrs,
		       u32 ipsec_obj_id,
		       struct mlx5e_ipsec_rule *ipsec_rule)
{
	u8 action[MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)] = {};
	struct mlx5_modify_hdr *modify_hdr = NULL;
	struct mlx5e_accel_fs_esp_prot *fs_prot;
	struct mlx5_flow_destination dest = {};
	struct mlx5e_accel_fs_esp *accel_esp;
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *rule;
	enum accel_fs_esp_type type;
	struct mlx5_flow_spec *spec;
	int err = 0;

	accel_esp = priv->ipsec->rx_fs;
	type = attrs->is_ipv6 ? ACCEL_FS_ESP6 : ACCEL_FS_ESP4;
	fs_prot = &accel_esp->fs_prot[type];

	err = rx_ft_get(priv, type);
	if (err)
		return err;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		err = -ENOMEM;
		goto out_err;
	}

	setup_fte_common(attrs, ipsec_obj_id, spec, &flow_act);

	/* Set bit[31] ipsec marker */
	/* Set bit[23-0] ipsec_obj_id */
	MLX5_SET(set_action_in, action, action_type, MLX5_ACTION_TYPE_SET);
	MLX5_SET(set_action_in, action, field, MLX5_ACTION_IN_FIELD_METADATA_REG_B);
	MLX5_SET(set_action_in, action, data, (ipsec_obj_id | BIT(31)));
	MLX5_SET(set_action_in, action, offset, 0);
	MLX5_SET(set_action_in, action, length, 32);

	modify_hdr = mlx5_modify_header_alloc(priv->mdev, MLX5_FLOW_NAMESPACE_KERNEL,
					      1, action);
	if (IS_ERR(modify_hdr)) {
		err = PTR_ERR(modify_hdr);
		netdev_err(priv->netdev,
			   "fail to alloc ipsec set modify_header_id err=%d\n", err);
		modify_hdr = NULL;
		goto out_err;
	}

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
			  MLX5_FLOW_CONTEXT_ACTION_IPSEC_DECRYPT |
			  MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	flow_act.modify_hdr = modify_hdr;
	dest.ft = fs_prot->rx_err.ft;
	rule = mlx5_add_flow_rules(fs_prot->ft, spec, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		netdev_err(priv->netdev, "fail to add ipsec rule attrs->action=0x%x, err=%d\n",
			   attrs->action, err);
		goto out_err;
	}

	ipsec_rule->rule = rule;
	ipsec_rule->set_modify_hdr = modify_hdr;
	goto out;

out_err:
	if (modify_hdr)
		mlx5_modify_header_dealloc(priv->mdev, modify_hdr);
	rx_ft_put(priv, type);

out:
	kvfree(spec);
	return err;
}

static int tx_add_rule(struct mlx5e_priv *priv,
		       struct mlx5_accel_esp_xfrm_attrs *attrs,
		       u32 ipsec_obj_id,
		       struct mlx5e_ipsec_rule *ipsec_rule)
{
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	int err = 0;

	err = tx_ft_get(priv);
	if (err)
		return err;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		err = -ENOMEM;
		goto out;
	}

	setup_fte_common(attrs, ipsec_obj_id, spec, &flow_act);

	/* Add IPsec indicator in metadata_reg_a */
	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS_2;
	MLX5_SET(fte_match_param, spec->match_criteria, misc_parameters_2.metadata_reg_a,
		 MLX5_ETH_WQE_FT_META_IPSEC);
	MLX5_SET(fte_match_param, spec->match_value, misc_parameters_2.metadata_reg_a,
		 MLX5_ETH_WQE_FT_META_IPSEC);

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_ALLOW |
			  MLX5_FLOW_CONTEXT_ACTION_IPSEC_ENCRYPT;
	rule = mlx5_add_flow_rules(priv->ipsec->tx_fs->ft, spec, &flow_act, NULL, 0);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		netdev_err(priv->netdev, "fail to add ipsec rule attrs->action=0x%x, err=%d\n",
			   attrs->action, err);
		goto out;
	}

	ipsec_rule->rule = rule;

out:
	kvfree(spec);
	if (err)
		tx_ft_put(priv);
	return err;
}

static void rx_del_rule(struct mlx5e_priv *priv,
			struct mlx5_accel_esp_xfrm_attrs *attrs,
			struct mlx5e_ipsec_rule *ipsec_rule)
{
	mlx5_del_flow_rules(ipsec_rule->rule);
	ipsec_rule->rule = NULL;

	mlx5_modify_header_dealloc(priv->mdev, ipsec_rule->set_modify_hdr);
	ipsec_rule->set_modify_hdr = NULL;

	rx_ft_put(priv, attrs->is_ipv6 ? ACCEL_FS_ESP6 : ACCEL_FS_ESP4);
}

static void tx_del_rule(struct mlx5e_priv *priv,
			struct mlx5e_ipsec_rule *ipsec_rule)
{
	mlx5_del_flow_rules(ipsec_rule->rule);
	ipsec_rule->rule = NULL;

	tx_ft_put(priv);
}

int mlx5e_accel_ipsec_fs_add_rule(struct mlx5e_priv *priv,
				  struct mlx5_accel_esp_xfrm_attrs *attrs,
				  u32 ipsec_obj_id,
				  struct mlx5e_ipsec_rule *ipsec_rule)
{
	if (!priv->ipsec->rx_fs)
		return -EOPNOTSUPP;

	if (attrs->action == MLX5_ACCEL_ESP_ACTION_DECRYPT)
		return rx_add_rule(priv, attrs, ipsec_obj_id, ipsec_rule);
	else
		return tx_add_rule(priv, attrs, ipsec_obj_id, ipsec_rule);
}

void mlx5e_accel_ipsec_fs_del_rule(struct mlx5e_priv *priv,
				   struct mlx5_accel_esp_xfrm_attrs *attrs,
				   struct mlx5e_ipsec_rule *ipsec_rule)
{
	if (!priv->ipsec->rx_fs)
		return;

	if (attrs->action == MLX5_ACCEL_ESP_ACTION_DECRYPT)
		rx_del_rule(priv, attrs, ipsec_rule);
	else
		tx_del_rule(priv, ipsec_rule);
}

void mlx5e_accel_ipsec_fs_cleanup(struct mlx5e_ipsec *ipsec)
{
	struct mlx5e_accel_fs_esp_prot *fs_prot;
	struct mlx5e_accel_fs_esp *accel_esp;
	enum accel_fs_esp_type i;

	if (!ipsec->rx_fs)
		return;

	mutex_destroy(&ipsec->tx_fs->mutex);
	WARN_ON(ipsec->tx_fs->refcnt);
	kfree(ipsec->tx_fs);

	accel_esp = ipsec->rx_fs;
	for (i = 0; i < ACCEL_FS_ESP_NUM_TYPES; i++) {
		fs_prot = &accel_esp->fs_prot[i];
		mutex_destroy(&fs_prot->prot_mutex);
		WARN_ON(fs_prot->refcnt);
	}
	kfree(ipsec->rx_fs);
}

int mlx5e_accel_ipsec_fs_init(struct mlx5e_ipsec *ipsec)
{
	struct mlx5e_accel_fs_esp_prot *fs_prot;
	struct mlx5e_accel_fs_esp *accel_esp;
	struct mlx5_flow_namespace *ns;
	enum accel_fs_esp_type i;
	int err = -ENOMEM;

	ns = mlx5_get_flow_namespace(ipsec->mdev,
				     MLX5_FLOW_NAMESPACE_EGRESS_KERNEL);
	if (!ns)
		return -EOPNOTSUPP;

	ipsec->tx_fs = kzalloc(sizeof(*ipsec->tx_fs), GFP_KERNEL);
	if (!ipsec->tx_fs)
		return -ENOMEM;

	ipsec->rx_fs = kzalloc(sizeof(*ipsec->rx_fs), GFP_KERNEL);
	if (!ipsec->rx_fs)
		goto err_rx;

	mutex_init(&ipsec->tx_fs->mutex);
	ipsec->tx_fs->ns = ns;

	accel_esp = ipsec->rx_fs;
	for (i = 0; i < ACCEL_FS_ESP_NUM_TYPES; i++) {
		fs_prot = &accel_esp->fs_prot[i];
		mutex_init(&fs_prot->prot_mutex);
	}

	return 0;

err_rx:
	kfree(ipsec->tx_fs);
	return err;
}
