// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */

#include <linux/netdevice.h>
#include "en.h"
#include "en/fs.h"
#include "ipsec.h"
#include "fs_core.h"

#define NUM_IPSEC_FTE BIT(15)

struct mlx5e_ipsec_ft {
	struct mutex mutex; /* Protect changes to this struct */
	struct mlx5_flow_table *sa;
	struct mlx5_flow_table *status;
	u32 refcnt;
};

struct mlx5e_ipsec_miss {
	struct mlx5_flow_group *group;
	struct mlx5_flow_handle *rule;
};

struct mlx5e_ipsec_rx {
	struct mlx5e_ipsec_ft ft;
	struct mlx5e_ipsec_miss sa;
	struct mlx5e_ipsec_rule status;
};

struct mlx5e_ipsec_tx {
	struct mlx5e_ipsec_ft ft;
	struct mlx5_flow_namespace *ns;
};

/* IPsec RX flow steering */
static enum mlx5_traffic_types family2tt(u32 family)
{
	if (family == AF_INET)
		return MLX5_TT_IPV4_IPSEC_ESP;
	return MLX5_TT_IPV6_IPSEC_ESP;
}

static struct mlx5_flow_table *ipsec_ft_create(struct mlx5_flow_namespace *ns,
					       int level, int prio,
					       int max_num_groups)
{
	struct mlx5_flow_table_attr ft_attr = {};

	ft_attr.autogroup.num_reserved_entries = 1;
	ft_attr.autogroup.max_num_groups = max_num_groups;
	ft_attr.max_fte = NUM_IPSEC_FTE;
	ft_attr.level = level;
	ft_attr.prio = prio;

	return mlx5_create_auto_grouped_flow_table(ns, &ft_attr);
}

static int ipsec_status_rule(struct mlx5_core_dev *mdev,
			     struct mlx5e_ipsec_rx *rx,
			     struct mlx5_flow_destination *dest)
{
	u8 action[MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)] = {};
	struct mlx5_flow_act flow_act = {};
	struct mlx5_modify_hdr *modify_hdr;
	struct mlx5_flow_handle *fte;
	struct mlx5_flow_spec *spec;
	int err;

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
		mlx5_core_err(mdev,
			      "fail to alloc ipsec copy modify_header_id err=%d\n", err);
		goto out_spec;
	}

	/* create fte */
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_MOD_HDR |
			  MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	flow_act.modify_hdr = modify_hdr;
	fte = mlx5_add_flow_rules(rx->ft.status, spec, &flow_act, dest, 1);
	if (IS_ERR(fte)) {
		err = PTR_ERR(fte);
		mlx5_core_err(mdev, "fail to add ipsec rx err copy rule err=%d\n", err);
		goto out;
	}

	kvfree(spec);
	rx->status.rule = fte;
	rx->status.modify_hdr = modify_hdr;
	return 0;

out:
	mlx5_modify_header_dealloc(mdev, modify_hdr);
out_spec:
	kvfree(spec);
	return err;
}

static int ipsec_miss_create(struct mlx5_core_dev *mdev,
			     struct mlx5_flow_table *ft,
			     struct mlx5e_ipsec_miss *miss,
			     struct mlx5_flow_destination *dest)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_spec *spec;
	u32 *flow_group_in;
	int err = 0;

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!flow_group_in || !spec) {
		err = -ENOMEM;
		goto out;
	}

	/* Create miss_group */
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, ft->max_fte - 1);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, ft->max_fte - 1);
	miss->group = mlx5_create_flow_group(ft, flow_group_in);
	if (IS_ERR(miss->group)) {
		err = PTR_ERR(miss->group);
		mlx5_core_err(mdev, "fail to create IPsec miss_group err=%d\n",
			      err);
		goto out;
	}

	/* Create miss rule */
	miss->rule = mlx5_add_flow_rules(ft, spec, &flow_act, dest, 1);
	if (IS_ERR(miss->rule)) {
		mlx5_destroy_flow_group(miss->group);
		err = PTR_ERR(miss->rule);
		mlx5_core_err(mdev, "fail to create IPsec miss_rule err=%d\n",
			      err);
		goto out;
	}
out:
	kvfree(flow_group_in);
	kvfree(spec);
	return err;
}

static void rx_destroy(struct mlx5_core_dev *mdev, struct mlx5e_ipsec_rx *rx)
{
	mlx5_del_flow_rules(rx->sa.rule);
	mlx5_destroy_flow_group(rx->sa.group);
	mlx5_destroy_flow_table(rx->ft.sa);

	mlx5_del_flow_rules(rx->status.rule);
	mlx5_modify_header_dealloc(mdev, rx->status.modify_hdr);
	mlx5_destroy_flow_table(rx->ft.status);
}

static int rx_create(struct mlx5_core_dev *mdev, struct mlx5e_ipsec *ipsec,
		     struct mlx5e_ipsec_rx *rx, u32 family)
{
	struct mlx5_flow_namespace *ns = mlx5e_fs_get_ns(ipsec->fs, false);
	struct mlx5_ttc_table *ttc = mlx5e_fs_get_ttc(ipsec->fs, false);
	struct mlx5_flow_destination dest;
	struct mlx5_flow_table *ft;
	int err;

	ft = ipsec_ft_create(ns, MLX5E_ACCEL_FS_ESP_FT_ERR_LEVEL,
			     MLX5E_NIC_PRIO, 1);
	if (IS_ERR(ft))
		return PTR_ERR(ft);

	rx->ft.status = ft;

	dest = mlx5_ttc_get_default_dest(ttc, family2tt(family));
	err = ipsec_status_rule(mdev, rx, &dest);
	if (err)
		goto err_add;

	/* Create FT */
	ft = ipsec_ft_create(ns, MLX5E_ACCEL_FS_ESP_FT_LEVEL, MLX5E_NIC_PRIO,
			     1);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		goto err_fs_ft;
	}
	rx->ft.sa = ft;

	err = ipsec_miss_create(mdev, rx->ft.sa, &rx->sa, &dest);
	if (err)
		goto err_fs;

	return 0;

err_fs:
	mlx5_destroy_flow_table(rx->ft.sa);
err_fs_ft:
	mlx5_del_flow_rules(rx->status.rule);
	mlx5_modify_header_dealloc(mdev, rx->status.modify_hdr);
err_add:
	mlx5_destroy_flow_table(rx->ft.status);
	return err;
}

static struct mlx5e_ipsec_rx *rx_ft_get(struct mlx5_core_dev *mdev,
					struct mlx5e_ipsec *ipsec, u32 family)
{
	struct mlx5_ttc_table *ttc = mlx5e_fs_get_ttc(ipsec->fs, false);
	struct mlx5_flow_destination dest = {};
	struct mlx5e_ipsec_rx *rx;
	int err = 0;

	if (family == AF_INET)
		rx = ipsec->rx_ipv4;
	else
		rx = ipsec->rx_ipv6;

	mutex_lock(&rx->ft.mutex);
	if (rx->ft.refcnt)
		goto skip;

	/* create FT */
	err = rx_create(mdev, ipsec, rx, family);
	if (err)
		goto out;

	/* connect */
	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = rx->ft.sa;
	mlx5_ttc_fwd_dest(ttc, family2tt(family), &dest);

skip:
	rx->ft.refcnt++;
out:
	mutex_unlock(&rx->ft.mutex);
	if (err)
		return ERR_PTR(err);
	return rx;
}

static void rx_ft_put(struct mlx5_core_dev *mdev, struct mlx5e_ipsec *ipsec,
		      u32 family)
{
	struct mlx5_ttc_table *ttc = mlx5e_fs_get_ttc(ipsec->fs, false);
	struct mlx5e_ipsec_rx *rx;

	if (family == AF_INET)
		rx = ipsec->rx_ipv4;
	else
		rx = ipsec->rx_ipv6;

	mutex_lock(&rx->ft.mutex);
	rx->ft.refcnt--;
	if (rx->ft.refcnt)
		goto out;

	/* disconnect */
	mlx5_ttc_fwd_default_dest(ttc, family2tt(family));

	/* remove FT */
	rx_destroy(mdev, rx);

out:
	mutex_unlock(&rx->ft.mutex);
}

/* IPsec TX flow steering */
static int tx_create(struct mlx5_core_dev *mdev, struct mlx5e_ipsec_tx *tx)
{
	struct mlx5_flow_table *ft;

	ft = ipsec_ft_create(tx->ns, 0, 0, 1);
	if (IS_ERR(ft))
		return PTR_ERR(ft);

	tx->ft.sa = ft;
	return 0;
}

static struct mlx5e_ipsec_tx *tx_ft_get(struct mlx5_core_dev *mdev,
					struct mlx5e_ipsec *ipsec)
{
	struct mlx5e_ipsec_tx *tx = ipsec->tx;
	int err = 0;

	mutex_lock(&tx->ft.mutex);
	if (tx->ft.refcnt)
		goto skip;

	err = tx_create(mdev, tx);
	if (err)
		goto out;
skip:
	tx->ft.refcnt++;
out:
	mutex_unlock(&tx->ft.mutex);
	if (err)
		return ERR_PTR(err);
	return tx;
}

static void tx_ft_put(struct mlx5e_ipsec *ipsec)
{
	struct mlx5e_ipsec_tx *tx = ipsec->tx;

	mutex_lock(&tx->ft.mutex);
	tx->ft.refcnt--;
	if (tx->ft.refcnt)
		goto out;

	mlx5_destroy_flow_table(tx->ft.sa);
out:
	mutex_unlock(&tx->ft.mutex);
}

static void setup_fte_addr4(struct mlx5_flow_spec *spec, __be32 *saddr,
			    __be32 *daddr)
{
	spec->match_criteria_enable |= MLX5_MATCH_OUTER_HEADERS;

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_version);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_version, 4);

	memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
			    outer_headers.src_ipv4_src_ipv6.ipv4_layout.ipv4), saddr, 4);
	memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
			    outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4), daddr, 4);
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 outer_headers.src_ipv4_src_ipv6.ipv4_layout.ipv4);
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4);
}

static void setup_fte_addr6(struct mlx5_flow_spec *spec, __be32 *saddr,
			    __be32 *daddr)
{
	spec->match_criteria_enable |= MLX5_MATCH_OUTER_HEADERS;

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_version);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_version, 6);

	memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
			    outer_headers.src_ipv4_src_ipv6.ipv6_layout.ipv6), saddr, 16);
	memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
			    outer_headers.dst_ipv4_dst_ipv6.ipv6_layout.ipv6), daddr, 16);
	memset(MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
			    outer_headers.src_ipv4_src_ipv6.ipv6_layout.ipv6), 0xff, 16);
	memset(MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
			    outer_headers.dst_ipv4_dst_ipv6.ipv6_layout.ipv6), 0xff, 16);
}

static void setup_fte_esp(struct mlx5_flow_spec *spec)
{
	/* ESP header */
	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS;

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_protocol);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_protocol, IPPROTO_ESP);
}

static void setup_fte_spi(struct mlx5_flow_spec *spec, u32 spi)
{
	/* SPI number */
	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS;

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, misc_parameters.outer_esp_spi);
	MLX5_SET(fte_match_param, spec->match_value, misc_parameters.outer_esp_spi, spi);
}

static void setup_fte_no_frags(struct mlx5_flow_spec *spec)
{
	/* Non fragmented */
	spec->match_criteria_enable |= MLX5_MATCH_OUTER_HEADERS;

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.frag);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.frag, 0);
}

static void setup_fte_reg_a(struct mlx5_flow_spec *spec)
{
	/* Add IPsec indicator in metadata_reg_a */
	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS_2;

	MLX5_SET(fte_match_param, spec->match_criteria,
		 misc_parameters_2.metadata_reg_a, MLX5_ETH_WQE_FT_META_IPSEC);
	MLX5_SET(fte_match_param, spec->match_value,
		 misc_parameters_2.metadata_reg_a, MLX5_ETH_WQE_FT_META_IPSEC);
}

static int setup_modify_header(struct mlx5_core_dev *mdev, u32 val, u8 dir,
			       struct mlx5_flow_act *flow_act)
{
	u8 action[MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)] = {};
	enum mlx5_flow_namespace_type ns_type;
	struct mlx5_modify_hdr *modify_hdr;

	MLX5_SET(set_action_in, action, action_type, MLX5_ACTION_TYPE_SET);
	switch (dir) {
	case XFRM_DEV_OFFLOAD_IN:
		MLX5_SET(set_action_in, action, field,
			 MLX5_ACTION_IN_FIELD_METADATA_REG_B);
		ns_type = MLX5_FLOW_NAMESPACE_KERNEL;
		break;
	default:
		return -EINVAL;
	}

	MLX5_SET(set_action_in, action, data, val);
	MLX5_SET(set_action_in, action, offset, 0);
	MLX5_SET(set_action_in, action, length, 32);

	modify_hdr = mlx5_modify_header_alloc(mdev, ns_type, 1, action);
	if (IS_ERR(modify_hdr)) {
		mlx5_core_err(mdev, "Failed to allocate modify_header %ld\n",
			      PTR_ERR(modify_hdr));
		return PTR_ERR(modify_hdr);
	}

	flow_act->modify_hdr = modify_hdr;
	flow_act->action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
	return 0;
}

static int rx_add_rule(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5e_ipsec_rule *ipsec_rule = &sa_entry->ipsec_rule;
	struct mlx5_accel_esp_xfrm_attrs *attrs = &sa_entry->attrs;
	struct mlx5_core_dev *mdev = mlx5e_ipsec_sa2dev(sa_entry);
	struct mlx5e_ipsec *ipsec = sa_entry->ipsec;
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	struct mlx5e_ipsec_rx *rx;
	int err;

	rx = rx_ft_get(mdev, ipsec, attrs->family);
	if (IS_ERR(rx))
		return PTR_ERR(rx);

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		err = -ENOMEM;
		goto err_alloc;
	}

	if (attrs->family == AF_INET)
		setup_fte_addr4(spec, &attrs->saddr.a4, &attrs->daddr.a4);
	else
		setup_fte_addr6(spec, attrs->saddr.a6, attrs->daddr.a6);

	setup_fte_spi(spec, attrs->spi);
	setup_fte_esp(spec);
	setup_fte_no_frags(spec);

	err = setup_modify_header(mdev, sa_entry->ipsec_obj_id | BIT(31),
				  XFRM_DEV_OFFLOAD_IN, &flow_act);
	if (err)
		goto err_mod_header;

	flow_act.crypto.type = MLX5_FLOW_CONTEXT_ENCRYPT_DECRYPT_TYPE_IPSEC;
	flow_act.crypto.obj_id = sa_entry->ipsec_obj_id;
	flow_act.flags |= FLOW_ACT_NO_APPEND;
	flow_act.action |= MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
			   MLX5_FLOW_CONTEXT_ACTION_CRYPTO_DECRYPT;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = rx->ft.status;
	rule = mlx5_add_flow_rules(rx->ft.sa, spec, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "fail to add RX ipsec rule err=%d\n", err);
		goto err_add_flow;
	}
	kvfree(spec);

	ipsec_rule->rule = rule;
	ipsec_rule->modify_hdr = flow_act.modify_hdr;
	return 0;

err_add_flow:
	mlx5_modify_header_dealloc(mdev, flow_act.modify_hdr);
err_mod_header:
	kvfree(spec);
err_alloc:
	rx_ft_put(mdev, ipsec, attrs->family);
	return err;
}

static int tx_add_rule(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5_accel_esp_xfrm_attrs *attrs = &sa_entry->attrs;
	struct mlx5_core_dev *mdev = mlx5e_ipsec_sa2dev(sa_entry);
	struct mlx5e_ipsec *ipsec = sa_entry->ipsec;
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	struct mlx5e_ipsec_tx *tx;
	int err = 0;

	tx = tx_ft_get(mdev, ipsec);
	if (IS_ERR(tx))
		return PTR_ERR(tx);

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		err = -ENOMEM;
		goto out;
	}

	if (attrs->family == AF_INET)
		setup_fte_addr4(spec, &attrs->saddr.a4, &attrs->daddr.a4);
	else
		setup_fte_addr6(spec, attrs->saddr.a6, attrs->daddr.a6);

	setup_fte_spi(spec, attrs->spi);
	setup_fte_esp(spec);
	setup_fte_no_frags(spec);
	setup_fte_reg_a(spec);

	flow_act.crypto.type = MLX5_FLOW_CONTEXT_ENCRYPT_DECRYPT_TYPE_IPSEC;
	flow_act.crypto.obj_id = sa_entry->ipsec_obj_id;
	flow_act.flags |= FLOW_ACT_NO_APPEND;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_ALLOW |
			  MLX5_FLOW_CONTEXT_ACTION_CRYPTO_ENCRYPT;
	rule = mlx5_add_flow_rules(tx->ft.sa, spec, &flow_act, NULL, 0);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "fail to add TX ipsec rule err=%d\n", err);
		goto out;
	}

	sa_entry->ipsec_rule.rule = rule;

out:
	kvfree(spec);
	if (err)
		tx_ft_put(ipsec);
	return err;
}

int mlx5e_accel_ipsec_fs_add_rule(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	if (sa_entry->attrs.dir == XFRM_DEV_OFFLOAD_OUT)
		return tx_add_rule(sa_entry);

	return rx_add_rule(sa_entry);
}

void mlx5e_accel_ipsec_fs_del_rule(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5e_ipsec_rule *ipsec_rule = &sa_entry->ipsec_rule;
	struct mlx5_core_dev *mdev = mlx5e_ipsec_sa2dev(sa_entry);

	mlx5_del_flow_rules(ipsec_rule->rule);

	if (sa_entry->attrs.dir == XFRM_DEV_OFFLOAD_OUT) {
		tx_ft_put(sa_entry->ipsec);
		return;
	}

	mlx5_modify_header_dealloc(mdev, ipsec_rule->modify_hdr);
	rx_ft_put(mdev, sa_entry->ipsec, sa_entry->attrs.family);
}

void mlx5e_accel_ipsec_fs_cleanup(struct mlx5e_ipsec *ipsec)
{
	if (!ipsec->tx)
		return;

	mutex_destroy(&ipsec->tx->ft.mutex);
	WARN_ON(ipsec->tx->ft.refcnt);
	kfree(ipsec->tx);

	mutex_destroy(&ipsec->rx_ipv4->ft.mutex);
	WARN_ON(ipsec->rx_ipv4->ft.refcnt);
	kfree(ipsec->rx_ipv4);

	mutex_destroy(&ipsec->rx_ipv6->ft.mutex);
	WARN_ON(ipsec->rx_ipv6->ft.refcnt);
	kfree(ipsec->rx_ipv6);
}

int mlx5e_accel_ipsec_fs_init(struct mlx5e_ipsec *ipsec)
{
	struct mlx5_flow_namespace *ns;
	int err = -ENOMEM;

	ns = mlx5_get_flow_namespace(ipsec->mdev,
				     MLX5_FLOW_NAMESPACE_EGRESS_IPSEC);
	if (!ns)
		return -EOPNOTSUPP;

	ipsec->tx = kzalloc(sizeof(*ipsec->tx), GFP_KERNEL);
	if (!ipsec->tx)
		return -ENOMEM;

	ipsec->rx_ipv4 = kzalloc(sizeof(*ipsec->rx_ipv4), GFP_KERNEL);
	if (!ipsec->rx_ipv4)
		goto err_rx_ipv4;

	ipsec->rx_ipv6 = kzalloc(sizeof(*ipsec->rx_ipv6), GFP_KERNEL);
	if (!ipsec->rx_ipv6)
		goto err_rx_ipv6;

	mutex_init(&ipsec->tx->ft.mutex);
	mutex_init(&ipsec->rx_ipv4->ft.mutex);
	mutex_init(&ipsec->rx_ipv6->ft.mutex);
	ipsec->tx->ns = ns;

	return 0;

err_rx_ipv6:
	kfree(ipsec->rx_ipv4);
err_rx_ipv4:
	kfree(ipsec->tx);
	return err;
}
