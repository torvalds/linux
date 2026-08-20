// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#include <linux/mlx5/device.h>
#include <net/psp.h>
#include <linux/psp.h>
#include "mlx5_core.h"
#include "psp.h"
#include "lib/crypto.h"
#include "en_accel/psp.h"
#include "fs_core.h"

enum accel_fs_psp_type {
	ACCEL_FS_PSP4,
	ACCEL_FS_PSP6,
	ACCEL_FS_PSP_NUM_TYPES,
};

enum accel_psp_syndrome {
	PSP_OK = 0,
	PSP_ICV_FAIL,
	PSP_BAD_TRAILER,
};

struct mlx5e_psp_tx_table {
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	struct mlx5_flow_handle *rule;
};

struct mlx5e_psp_rx_check_table {
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *drop_group;
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_handle *auth_fail_rule;
	struct mlx5_flow_handle *err_rule;
	struct mlx5_flow_handle *bad_rule;
};

struct mlx5e_psp_rx_decrypt_table {
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *miss_group;
	struct mlx5_flow_handle *miss_rule;
	struct mlx5_modify_hdr *rx_modify_hdr;
	struct mlx5_flow_handle *rule;
};

struct mlx5e_psp_rx_table {
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *miss_group;
	struct mlx5_flow_handle *miss_rule;
	struct mlx5_flow_handle *udp_rules[ACCEL_FS_PSP_NUM_TYPES];
};

struct mlx5e_psp_fs {
	struct mlx5_core_dev *mdev;
	struct mlx5_fc *tx_counter;
	struct mlx5e_psp_tx_table tx;

	/* Rx */
	struct mlx5e_flow_steering *fs;
	struct mlx5_fc *rx_counter;
	struct mlx5_fc *rx_auth_fail_counter;
	struct mlx5_fc *rx_err_counter;
	struct mlx5_fc *rx_bad_counter;

	struct mlx5e_psp_rx_decrypt_table decrypt[ACCEL_FS_PSP_NUM_TYPES];
	struct mlx5e_psp_rx_check_table check;
	struct mlx5e_psp_rx_table rx;
};

/* PSP RX flow steering */
static enum mlx5_traffic_types fs_psp2tt(enum accel_fs_psp_type i)
{
	if (i == ACCEL_FS_PSP4)
		return MLX5_TT_IPV4_UDP;

	return MLX5_TT_IPV6_UDP;
}

static int accel_psp_fs_create_ft(struct mlx5e_psp_fs *fs,
				  struct mlx5_flow_table_attr *ft_attr,
				  struct mlx5_flow_table **ft)
{
	struct mlx5_flow_namespace *ns = mlx5e_fs_get_ns(fs->fs, false);
	int err = 0;

	*ft = mlx5_create_auto_grouped_flow_table(ns, ft_attr);
	if (IS_ERR(*ft)) {
		err = PTR_ERR(*ft);
		*ft = NULL;
	}

	return err;
}

static void accel_psp_fs_destroy_ft(struct mlx5_flow_table **table)
{
	if (*table) {
		mlx5_destroy_flow_table(*table);
		*table = NULL;
	}
}

static void accel_psp_fs_del_flow_rule(struct mlx5_flow_handle **rule)
{
	if (*rule) {
		mlx5_del_flow_rules(*rule);
		*rule = NULL;
	}
}

static int accel_psp_fs_create_miss_group(struct mlx5_flow_table *ft,
					  struct mlx5_flow_group **group)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	u32 *in = kvzalloc(inlen, GFP_KERNEL);
	int err = 0;

	if (!in)
		return -ENOMEM;

	MLX5_SET(create_flow_group_in, in, start_flow_index, ft->max_fte - 1);
	MLX5_SET(create_flow_group_in, in, end_flow_index, ft->max_fte - 1);
	*group = mlx5_create_flow_group(ft, in);
	if (IS_ERR(*group)) {
		err = PTR_ERR(*group);
		*group = NULL;
	}
	kvfree(in);

	return err;
}

static void accel_psp_fs_destroy_flow_group(struct mlx5_flow_group **group)
{
	if (*group) {
		mlx5_destroy_flow_group(*group);
		*group = NULL;
	}
}

static int accel_psp_fs_create_counter(struct mlx5_core_dev *dev,
				       struct mlx5_fc **counter)
{
	*counter = mlx5_fc_create(dev, false);
	if (IS_ERR(*counter)) {
		int err = PTR_ERR(*counter);

		*counter = NULL;
		return err;
	}

	return 0;
}

static void accel_psp_fs_destroy_counter(struct mlx5_core_dev *dev,
					 struct mlx5_fc **counter)
{
	if (*counter) {
		mlx5_fc_destroy(dev, *counter);
		*counter = NULL;
	}
}

static void accel_psp_fs_rx_ft_destroy(struct mlx5e_psp_rx_table *rx)
{
	int i;

	for (i = 0; i < ACCEL_FS_PSP_NUM_TYPES; i++)
		accel_psp_fs_del_flow_rule(&rx->udp_rules[i]);
	accel_psp_fs_del_flow_rule(&rx->miss_rule);
	accel_psp_fs_destroy_flow_group(&rx->miss_group);
	accel_psp_fs_destroy_ft(&rx->ft);
}

static int accel_psp_fs_rx_ft_create(struct mlx5e_psp_fs *fs,
				     struct mlx5e_psp_rx_table *rx)
{
	struct mlx5_ttc_table *ttc = mlx5e_fs_get_ttc(fs->fs, false);
	struct mlx5_flow_destination dest[2] = {};
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_core_dev *mdev = fs->mdev;
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	int i, err = 0;

	spec = kzalloc_obj(*spec);
	if (!spec)
		return -ENOMEM;

	ft_attr.max_fte = 1 + ACCEL_FS_PSP_NUM_TYPES;
	ft_attr.level = MLX5E_ACCEL_FS_PSP_RX_FT_LEVEL;
	ft_attr.prio = MLX5E_NIC_PRIO;
	ft_attr.autogroup.num_reserved_entries = 1;
	err = accel_psp_fs_create_ft(fs, &ft_attr, &rx->ft);
	if (err) {
		mlx5_core_err(mdev, "fail to create psp rx ft err=%d\n", err);
		goto out_err;
	}

	err = accel_psp_fs_create_miss_group(rx->ft, &rx->miss_group);
	if (err) {
		mlx5_core_err(mdev, "fail to create psp rx miss_group err=%d\n",
			      err);
		goto out_err;
	}

	/* Add miss rule */
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
		MLX5_FLOW_CONTEXT_ACTION_COUNT;
	flow_act.flags = FLOW_ACT_IGNORE_FLOW_LEVEL;
	dest[0].type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest[0].ft = mlx5_get_ttc_flow_table(ttc);
	dest[1].type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
	dest[1].counter = fs->rx_counter;
	rule = mlx5_add_flow_rules(rx->ft, NULL, &flow_act, dest, 2);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "fail to create psp rx rule, err=%d\n",
			      err);
		goto out_err;
	}
	rx->miss_rule = rule;

	/* Add UDP v4/v6 rules */
	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 outer_headers.ip_version);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, spec->match_criteria,
			 ip_protocol);
	MLX5_SET(fte_match_set_lyr_2_4, spec->match_value, ip_protocol,
		 IPPROTO_UDP);
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
		MLX5_FLOW_CONTEXT_ACTION_COUNT;
	flow_act.flags = 0;
	for (i = 0; i < ACCEL_FS_PSP_NUM_TYPES; i++) {
		int version = i == ACCEL_FS_PSP4 ? 4 : 6;

		MLX5_SET(fte_match_param, spec->match_value,
			 outer_headers.ip_version, version);
		dest[0] = mlx5_ttc_get_default_dest(ttc, fs_psp2tt(i));
		dest[1].type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
		dest[1].counter = fs->rx_counter;
		rule = mlx5_add_flow_rules(rx->ft, spec, &flow_act, dest,
					   2);
		if (IS_ERR(rule)) {
			err = PTR_ERR(rule);
			mlx5_core_err(mdev,
				      "fail to create psp rx UDP%d rule err=%d\n",
				      version, err);
			goto out_err;
		}
		rx->udp_rules[i] = rule;
	}
	goto out_spec;

out_err:
	accel_psp_fs_rx_ft_destroy(rx);
out_spec:
	kvfree(spec);
	return err;
}

static
void accel_psp_fs_rx_check_ft_destroy(struct mlx5e_psp_rx_check_table *check)
{
	accel_psp_fs_del_flow_rule(&check->bad_rule);
	accel_psp_fs_del_flow_rule(&check->err_rule);
	accel_psp_fs_del_flow_rule(&check->auth_fail_rule);
	accel_psp_fs_del_flow_rule(&check->rule);
	accel_psp_fs_destroy_flow_group(&check->drop_group);
	accel_psp_fs_destroy_ft(&check->ft);
}

static void accel_psp_setup_syndrome_match(struct mlx5_flow_spec *spec,
					   enum accel_psp_syndrome syndrome)
{
	void *misc_params_2;

	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS_2;
	misc_params_2 = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters_2);
	MLX5_SET_TO_ONES(fte_match_set_misc2, misc_params_2, psp_syndrome);
	misc_params_2 = MLX5_ADDR_OF(fte_match_param, spec->match_value, misc_parameters_2);
	MLX5_SET(fte_match_set_misc2, misc_params_2, psp_syndrome, syndrome);
}

static int accel_psp_add_drop_rule(struct mlx5_flow_table *ft,
				   struct mlx5_flow_spec *spec,
				   struct mlx5_fc *counter,
				   struct mlx5_flow_handle **rule)
{
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_act flow_act = {};
	int err = 0;

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_DROP |
			  MLX5_FLOW_CONTEXT_ACTION_COUNT;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
	dest.counter = counter;
	*rule = mlx5_add_flow_rules(ft, spec, &flow_act, &dest, 1);
	if (IS_ERR(*rule)) {
		err = PTR_ERR(*rule);
		*rule = NULL;
	}
	return err;
}

static
int accel_psp_fs_rx_check_ft_create(struct mlx5e_psp_fs *fs,
				    struct mlx5e_psp_rx_check_table *check)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_destination dest = {};
	struct mlx5_core_dev *mdev = fs->mdev;
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *fte;
	struct mlx5_flow_spec *spec;
	int err = 0;

	spec = kzalloc_obj(*spec);
	if (!spec)
		return -ENOMEM;

	ft_attr.max_fte = 4;
	ft_attr.autogroup.num_reserved_entries = 1;
	ft_attr.autogroup.max_num_groups = 2;
	ft_attr.level = MLX5E_ACCEL_FS_PSP_ERR_FT_LEVEL;
	ft_attr.prio = MLX5E_NIC_PRIO;
	err = accel_psp_fs_create_ft(fs, &ft_attr, &check->ft);
	if (err) {
		mlx5_core_err(fs->mdev,
			      "fail to create psp rx check ft err=%d\n", err);
		goto out_err;
	}

	err = accel_psp_fs_create_miss_group(check->ft, &check->drop_group);
	if (err) {
		mlx5_core_err(fs->mdev,
			      "fail to create psp rx check drop group err=%d\n",
			      err);
		goto out_err;
	}

	accel_psp_setup_syndrome_match(spec, PSP_OK);
	/* create fte */
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = fs->rx.ft;
	fte = mlx5_add_flow_rules(check->ft, spec, &flow_act, &dest, 1);
	if (IS_ERR(fte)) {
		err = PTR_ERR(fte);
		mlx5_core_err(mdev, "fail to add psp rx check ok rule err=%d\n",
			      err);
		goto out_err;
	}
	check->rule = fte;

	/* add auth fail drop rule */
	memset(spec, 0, sizeof(*spec));
	accel_psp_setup_syndrome_match(spec, PSP_ICV_FAIL);
	err = accel_psp_add_drop_rule(check->ft, spec,
				      fs->rx_auth_fail_counter,
				      &check->auth_fail_rule);
	if (err) {
		mlx5_core_err(mdev,
			      "fail to add psp rx check auth fail drop rule err=%d\n",
			      err);
		goto out_err;
	}

	/* add framing drop rule */
	memset(spec, 0, sizeof(*spec));
	accel_psp_setup_syndrome_match(spec, PSP_BAD_TRAILER);
	err = accel_psp_add_drop_rule(check->ft, spec, fs->rx_err_counter,
				      &check->err_rule);
	if (err) {
		mlx5_core_err(mdev,
			      "fail to add psp rx check framing drop rule err=%d\n",
			      err);
		goto out_err;
	}

	/* add misc. errors drop rule */
	memset(spec, 0, sizeof(*spec));
	err = accel_psp_add_drop_rule(check->ft, spec, fs->rx_bad_counter,
				      &check->bad_rule);
	if (err) {
		mlx5_core_err(mdev,
			      "fail to add psp rx check misc. err drop rule err=%d\n",
			      err);
		goto out_err;
	}

	goto out_spec;

out_err:
	accel_psp_fs_rx_check_ft_destroy(check);
out_spec:
	kfree(spec);
	return err;
}

static void
accel_psp_fs_rx_decrypt_ft_destroy(struct mlx5e_psp_fs *fs,
				   struct mlx5e_psp_rx_decrypt_table *decrypt)
{
	accel_psp_fs_del_flow_rule(&decrypt->rule);
	if (decrypt->rx_modify_hdr) {
		mlx5_modify_header_dealloc(fs->mdev, decrypt->rx_modify_hdr);
		decrypt->rx_modify_hdr = NULL;
	}
	accel_psp_fs_del_flow_rule(&decrypt->miss_rule);
	accel_psp_fs_destroy_flow_group(&decrypt->miss_group);
	accel_psp_fs_destroy_ft(&decrypt->ft);
}

static void setup_fte_udp_psp(struct mlx5_flow_spec *spec, u16 udp_port)
{
	spec->match_criteria_enable |= MLX5_MATCH_OUTER_HEADERS;
	MLX5_SET(fte_match_set_lyr_2_4, spec->match_criteria, udp_dport, 0xffff);
	MLX5_SET(fte_match_set_lyr_2_4, spec->match_value, udp_dport, udp_port);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, spec->match_criteria, ip_protocol);
	MLX5_SET(fte_match_set_lyr_2_4, spec->match_value, ip_protocol, IPPROTO_UDP);
}

static int
accel_psp_fs_rx_decrypt_ft_create(struct mlx5e_psp_fs *fs,
				  struct mlx5e_psp_rx_decrypt_table *decrypt,
				  struct mlx5_flow_destination *default_dest)
{
	u8 action[MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)] = {};
	struct mlx5_modify_hdr *modify_hdr = NULL;
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_destination dest = {};
	struct mlx5_core_dev *mdev = fs->mdev;
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	int err = 0;

	spec = kvzalloc_obj(*spec);
	if (!spec)
		return -ENOMEM;

	/* Create FT */
	ft_attr.max_fte = 2;
	ft_attr.level = MLX5E_ACCEL_FS_PSP_FT_LEVEL;
	ft_attr.autogroup.num_reserved_entries = 1;
	ft_attr.autogroup.max_num_groups = 1;
	ft_attr.prio = MLX5E_NIC_PRIO;
	err = accel_psp_fs_create_ft(fs, &ft_attr, &decrypt->ft);
	if (err) {
		mlx5_core_err(mdev, "fail to create psp rx decrypt ft err=%d\n",
			      err);
		goto out_err;
	}

	/* Create miss_group */
	err = accel_psp_fs_create_miss_group(decrypt->ft, &decrypt->miss_group);
	if (err) {
		mlx5_core_err(mdev,
			      "fail to create psp rx decrypt miss_group err=%d\n",
			      err);
		goto out_err;
	}

	/* Create miss rule */
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	rule = mlx5_add_flow_rules(decrypt->ft, spec, &flow_act, default_dest,
				   1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev,
			      "fail to create psp rx decrypt miss_rule err=%d\n",
			      err);
		goto out_err;
	}
	decrypt->miss_rule = rule;

	/* Add PSP RX decrypt rule */
	setup_fte_udp_psp(spec, PSP_DEFAULT_UDP_PORT);
	flow_act.crypto.type = MLX5_FLOW_CONTEXT_ENCRYPT_DECRYPT_TYPE_PSP;
	/* Set bit[31, 30] PSP marker */
#define MLX5E_PSP_MARKER_BIT (BIT(30) | BIT(31))
	MLX5_SET(set_action_in, action, action_type, MLX5_ACTION_TYPE_SET);
	MLX5_SET(set_action_in, action, field, MLX5_ACTION_IN_FIELD_METADATA_REG_B);
	MLX5_SET(set_action_in, action, data, MLX5E_PSP_MARKER_BIT);
	MLX5_SET(set_action_in, action, offset, 0);
	MLX5_SET(set_action_in, action, length, 32);

	modify_hdr = mlx5_modify_header_alloc(mdev, MLX5_FLOW_NAMESPACE_KERNEL, 1, action);
	if (IS_ERR(modify_hdr)) {
		err = PTR_ERR(modify_hdr);
		mlx5_core_err(mdev, "fail to alloc psp set modify_header_id err=%d\n", err);
		modify_hdr = NULL;
		goto out_err;
	}
	decrypt->rx_modify_hdr = modify_hdr;

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
			  MLX5_FLOW_CONTEXT_ACTION_CRYPTO_DECRYPT |
			  MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
	flow_act.modify_hdr = modify_hdr;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = fs->check.ft;
	rule = mlx5_add_flow_rules(decrypt->ft, spec, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "fail to add psp rx decrypt rule, err=%d\n",
			      err);
		goto out_err;
	}

	decrypt->rule = rule;
	goto out_spec;

out_err:
	accel_psp_fs_rx_decrypt_ft_destroy(fs, decrypt);
out_spec:
	kvfree(spec);
	return err;
}

static void accel_psp_fs_rx_destroy(struct mlx5e_psp_fs *fs)
{
	struct mlx5_ttc_table *ttc = mlx5e_fs_get_ttc(fs->fs, false);
	int i;

	/* disconnect */
	for (i = 0; i < ACCEL_FS_PSP_NUM_TYPES; i++) {
		mlx5_ttc_fwd_default_dest(ttc, fs_psp2tt(i));
		accel_psp_fs_rx_decrypt_ft_destroy(fs, &fs->decrypt[i]);
	}
	accel_psp_fs_rx_check_ft_destroy(&fs->check);
	accel_psp_fs_rx_ft_destroy(&fs->rx);
}

static int accel_psp_fs_rx_create(struct mlx5e_psp_fs *fs,
				  struct netlink_ext_ack *extack)
{
	struct mlx5_ttc_table *ttc = mlx5e_fs_get_ttc(fs->fs, false);
	int i, err;

	err = accel_psp_fs_rx_ft_create(fs, &fs->rx);
	if (err) {
		NL_SET_ERR_MSG(extack, "Failed creating RX steering table");
		return err;
	}

	err = accel_psp_fs_rx_check_ft_create(fs, &fs->check);
	if (err) {
		NL_SET_ERR_MSG(extack,
			       "Failed creating RX check steering table");
		goto err_ft;
	}

	for (i = 0; i < ACCEL_FS_PSP_NUM_TYPES; i++) {
		struct mlx5_flow_destination dest;

		dest = mlx5_ttc_get_default_dest(ttc, fs_psp2tt(i));
		err = accel_psp_fs_rx_decrypt_ft_create(fs, &fs->decrypt[i],
							&dest);
		if (err) {
			NL_SET_ERR_MSG(extack,
				       "Failed creating RX decrypt steering table");
			goto err_decrypt_ft;
		}

		dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
		dest.ft = fs->decrypt[i].ft;
		mlx5_ttc_fwd_dest(ttc, fs_psp2tt(i), &dest);
	}

	return 0;

err_decrypt_ft:
	while (--i >= 0) {
		mlx5_ttc_fwd_default_dest(ttc, fs_psp2tt(i));
		accel_psp_fs_rx_decrypt_ft_destroy(fs, &fs->decrypt[i]);
	}
	accel_psp_fs_rx_check_ft_destroy(&fs->check);
err_ft:
	accel_psp_fs_rx_ft_destroy(&fs->rx);
	return err;
}

static void accel_psp_fs_rx_cleanup(struct mlx5e_psp_fs *fs)
{
	accel_psp_fs_destroy_counter(fs->mdev, &fs->rx_bad_counter);
	accel_psp_fs_destroy_counter(fs->mdev, &fs->rx_err_counter);
	accel_psp_fs_destroy_counter(fs->mdev, &fs->rx_auth_fail_counter);
	accel_psp_fs_destroy_counter(fs->mdev, &fs->rx_counter);
}

static int accel_psp_fs_rx_init(struct mlx5e_psp_fs *fs)
{
	struct mlx5_core_dev *mdev = fs->mdev;
	int err;

	err = accel_psp_fs_create_counter(mdev, &fs->rx_counter);
	if (err) {
		mlx5_core_warn(mdev,
			       "fail to create psp rx flow counter err=%d\n",
			       err);
		goto out_err;
	}

	err = accel_psp_fs_create_counter(mdev, &fs->rx_auth_fail_counter);
	if (err) {
		mlx5_core_warn(mdev,
			       "fail to create psp rx auth fail flow counter err=%d\n",
			       err);
		goto out_err;
	}

	err = accel_psp_fs_create_counter(mdev, &fs->rx_err_counter);
	if (err) {
		mlx5_core_warn(mdev,
			       "fail to create psp rx error flow counter err=%d\n",
			       err);
		goto out_err;
	}

	err = accel_psp_fs_create_counter(mdev, &fs->rx_bad_counter);
	if (err) {
		mlx5_core_warn(mdev,
			       "fail to create psp rx bad flow counter err=%d\n",
			       err);
		goto out_err;
	}

	return 0;

out_err:
	accel_psp_fs_rx_cleanup(fs);
	return err;
}

void mlx5_accel_psp_fs_cleanup_rx_tables(struct mlx5e_priv *priv)
{
	if (!priv->psp)
		return;

	netdev_lock(priv->netdev);
	accel_psp_fs_rx_destroy(priv->psp->fs);
	netdev_unlock(priv->netdev);
}

static int accel_psp_fs_tx_ft_create(struct mlx5e_psp_fs *fs,
				     struct mlx5e_psp_tx_table *tx)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_destination dest = {};
	struct mlx5_core_dev *mdev = fs->mdev;
	struct mlx5_flow_act flow_act = {};
	u32 *in, *mc, *outer_headers_c;
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	int err = 0;

	spec = kvzalloc_obj(*spec);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!spec || !in) {
		err = -ENOMEM;
		goto out;
	}

	ft_attr.max_fte = 1;
#define MLX5E_PSP_PRIO 0
	ft_attr.prio = MLX5E_PSP_PRIO;
#define MLX5E_PSP_LEVEL 0
	ft_attr.level = MLX5E_PSP_LEVEL;
	ft_attr.autogroup.max_num_groups = 1;

	ft = mlx5_create_flow_table(tx->ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		mlx5_core_err(mdev, "PSP: fail to add psp tx flow table, err = %d\n", err);
		goto out;
	}

	mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	outer_headers_c = MLX5_ADDR_OF(fte_match_param, mc, outer_headers);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, ip_protocol);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, udp_dport);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	fg = mlx5_create_flow_group(ft, in);
	if (IS_ERR(fg)) {
		err = PTR_ERR(fg);
		mlx5_core_err(mdev, "PSP: fail to add psp tx flow group, err = %d\n", err);
		goto err_create_fg;
	}

	setup_fte_udp_psp(spec, PSP_DEFAULT_UDP_PORT);
	flow_act.crypto.type = MLX5_FLOW_CONTEXT_ENCRYPT_DECRYPT_TYPE_PSP;
	flow_act.flags |= FLOW_ACT_NO_APPEND;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_ALLOW |
			  MLX5_FLOW_CONTEXT_ACTION_CRYPTO_ENCRYPT |
			  MLX5_FLOW_CONTEXT_ACTION_COUNT;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
	dest.counter = fs->tx_counter;
	rule = mlx5_add_flow_rules(ft, spec, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "PSP: fail to add psp tx flow rule, err = %d\n", err);
		goto err_add_flow_rule;
	}

	tx->ft = ft;
	tx->fg = fg;
	tx->rule = rule;
	goto out;

err_add_flow_rule:
	mlx5_destroy_flow_group(fg);
err_create_fg:
	mlx5_destroy_flow_table(ft);
out:
	kvfree(in);
	kvfree(spec);
	return err;
}

static void accel_psp_fs_tx_ft_destroy(struct mlx5e_psp_tx_table *tx)
{
	accel_psp_fs_del_flow_rule(&tx->rule);
	accel_psp_fs_destroy_flow_group(&tx->fg);
	accel_psp_fs_destroy_ft(&tx->ft);
}

static void accel_psp_fs_tx_cleanup(struct mlx5e_psp_fs *fs)
{
	accel_psp_fs_destroy_counter(fs->mdev, &fs->tx_counter);
}

static int accel_psp_fs_tx_init(struct mlx5e_psp_fs *fs)
{
	struct mlx5_core_dev *mdev = fs->mdev;
	int err;

	fs->tx.ns = mlx5_get_flow_namespace(mdev,
					    MLX5_FLOW_NAMESPACE_EGRESS_IPSEC);
	if (!fs->tx.ns)
		return -EOPNOTSUPP;

	err = accel_psp_fs_create_counter(mdev, &fs->tx_counter);
	if (err) {
		mlx5_core_warn(mdev,
			       "fail to create psp tx flow counter err=%d\n",
			       err);
		return err;
	}
	return 0;
}

static void
mlx5e_accel_psp_fs_get_stats_fill(struct mlx5e_priv *priv,
				  struct mlx5e_psp_stats *stats)
{
	struct mlx5e_psp_fs *fs = priv->psp->fs;
	struct mlx5_core_dev *mdev = priv->mdev;

	if (fs->tx_counter)
		mlx5_fc_query(mdev, fs->tx_counter, &stats->psp_tx_pkts,
			      &stats->psp_tx_bytes);

	if (fs->rx_counter)
		mlx5_fc_query(mdev, fs->rx_counter, &stats->psp_rx_pkts,
			      &stats->psp_rx_bytes);

	if (fs->rx_auth_fail_counter)
		mlx5_fc_query(mdev, fs->rx_auth_fail_counter,
			      &stats->psp_rx_pkts_auth_fail,
			      &stats->psp_rx_bytes_auth_fail);

	if (fs->rx_err_counter)
		mlx5_fc_query(mdev, fs->rx_err_counter,
			      &stats->psp_rx_pkts_frame_err,
			      &stats->psp_rx_bytes_frame_err);

	if (fs->rx_bad_counter)
		mlx5_fc_query(mdev, fs->rx_bad_counter,
			      &stats->psp_rx_pkts_drop,
			      &stats->psp_rx_bytes_drop);
}

void mlx5_accel_psp_fs_cleanup_tx_tables(struct mlx5e_priv *priv)
{
	if (!priv->psp)
		return;

	netdev_lock(priv->netdev);
	accel_psp_fs_tx_ft_destroy(&priv->psp->fs->tx);
	netdev_unlock(priv->netdev);
}

static void mlx5e_accel_psp_fs_cleanup(struct mlx5e_psp_fs *fs)
{
	accel_psp_fs_rx_cleanup(fs);
	accel_psp_fs_tx_cleanup(fs);
	kfree(fs);
}

static struct mlx5e_psp_fs *mlx5e_accel_psp_fs_init(struct mlx5e_priv *priv)
{
	struct mlx5e_psp_fs *fs;
	int err = 0;

	fs = kzalloc_obj(*fs);
	if (!fs)
		return ERR_PTR(-ENOMEM);

	fs->mdev = priv->mdev;
	err = accel_psp_fs_tx_init(fs);
	if (err)
		goto err_tx;

	fs->fs = priv->fs;
	err = accel_psp_fs_rx_init(fs);
	if (err)
		goto err_rx;

	return fs;

err_rx:
	accel_psp_fs_tx_cleanup(fs);
err_tx:
	kfree(fs);
	return ERR_PTR(err);
}

static int accel_psp_fs_create(struct mlx5e_priv *priv,
			       struct netlink_ext_ack *extack)
{
	int err;

	err = accel_psp_fs_rx_create(priv->psp->fs, extack);
	if (err)
		return err;

	err = accel_psp_fs_tx_ft_create(priv->psp->fs, &priv->psp->fs->tx);
	if (err) {
		NL_SET_ERR_MSG(extack, "Failed creating TX steering table");
		accel_psp_fs_rx_destroy(priv->psp->fs);
	}
	return err;
}

static void accel_psp_fs_destroy(struct mlx5e_priv *priv)
{
	accel_psp_fs_tx_ft_destroy(&priv->psp->fs->tx);
	accel_psp_fs_rx_destroy(priv->psp->fs);
}

static int
mlx5e_psp_set_config(struct psp_dev *psd, struct psp_dev_config *conf,
		     struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = netdev_priv(psd->main_netdev);
	bool psp_enabled = psd->config.versions;
	bool enable_psp = conf->versions;
	int err = 0;

	netdev_lock(priv->netdev);
	if (!psp_enabled && enable_psp)
		err = accel_psp_fs_create(priv, extack);
	else if (psp_enabled && !enable_psp)
		accel_psp_fs_destroy(priv);
	netdev_unlock(priv->netdev);
	return err;
}

static int
mlx5e_psp_generate_key_spi(struct mlx5_core_dev *mdev,
			   enum mlx5_psp_gen_spi_in_key_size keysz,
			   unsigned int keysz_bytes,
			   struct psp_key_parsed *key)
{
	u32 out[MLX5_ST_SZ_DW(psp_gen_spi_out) + MLX5_ST_SZ_DW(key_spi)] = {};
	u32 in[MLX5_ST_SZ_DW(psp_gen_spi_in)] = {};
	void *outkey;
	int err;

	WARN_ON_ONCE(keysz_bytes > PSP_MAX_KEY);

	MLX5_SET(psp_gen_spi_in, in, opcode, MLX5_CMD_OP_PSP_GEN_SPI);
	MLX5_SET(psp_gen_spi_in, in, key_size, keysz);
	MLX5_SET(psp_gen_spi_in, in, num_of_spi, 1);
	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	outkey = MLX5_ADDR_OF(psp_gen_spi_out, out, key_spi);
	key->spi = cpu_to_be32(MLX5_GET(key_spi, outkey, spi));
	memcpy(key->key, MLX5_ADDR_OF(key_spi, outkey, key) + 32 - keysz_bytes,
	       keysz_bytes);

	return 0;
}

static int
mlx5e_psp_rx_spi_alloc(struct psp_dev *psd, u32 version,
		       struct psp_key_parsed *assoc,
		       struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = netdev_priv(psd->main_netdev);
	enum mlx5_psp_gen_spi_in_key_size keysz;
	u8 keysz_bytes;

	switch (version) {
	case PSP_VERSION_HDR0_AES_GCM_128:
		keysz = MLX5_PSP_GEN_SPI_IN_KEY_SIZE_128;
		keysz_bytes = 16;
		break;
	case PSP_VERSION_HDR0_AES_GCM_256:
		keysz = MLX5_PSP_GEN_SPI_IN_KEY_SIZE_256;
		keysz_bytes = 32;
		break;
	default:
		return -EINVAL;
	}

	return mlx5e_psp_generate_key_spi(priv->mdev, keysz, keysz_bytes, assoc);
}

struct psp_key {
	u32 id;
};

static int mlx5e_psp_assoc_add(struct psp_dev *psd, struct psp_assoc *pas,
			       struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = netdev_priv(psd->main_netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct psp_key_parsed *tx = &pas->tx;
	struct mlx5e_psp *psp = priv->psp;
	struct psp_key *nkey;
	int err;

	mdev = priv->mdev;
	nkey = (struct psp_key *)pas->drv_data;

	err = mlx5_create_encryption_key(mdev, tx->key,
					 psp_key_size(pas->version),
					 MLX5_ACCEL_OBJ_PSP_KEY,
					 &nkey->id);
	if (err) {
		mlx5_core_err(mdev, "Failed to create encryption key (err = %d)\n", err);
		return err;
	}

	atomic_inc(&psp->tx_key_cnt);
	return 0;
}

static void mlx5e_psp_assoc_del(struct psp_dev *psd, struct psp_assoc *pas)
{
	struct mlx5e_priv *priv = netdev_priv(psd->main_netdev);
	struct mlx5e_psp *psp = priv->psp;
	struct psp_key *nkey;

	nkey = (struct psp_key *)pas->drv_data;
	mlx5_destroy_encryption_key(priv->mdev, nkey->id);
	atomic_dec(&psp->tx_key_cnt);
}

static int mlx5e_psp_rotate_key(struct mlx5_core_dev *mdev)
{
	u32 in[MLX5_ST_SZ_DW(psp_rotate_key_in)] = {};
	u32 out[MLX5_ST_SZ_DW(psp_rotate_key_out)];

	MLX5_SET(psp_rotate_key_in, in, opcode,
		 MLX5_CMD_OP_PSP_ROTATE_KEY);

	return mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

static int
mlx5e_psp_key_rotate(struct psp_dev *psd, struct netlink_ext_ack *exack)
{
	struct mlx5e_priv *priv = netdev_priv(psd->main_netdev);

	/* no support for protecting against external rotations */
	psd->generation = 0;

	return mlx5e_psp_rotate_key(priv->mdev);
}

static void
mlx5e_psp_get_stats(struct psp_dev *psd, struct psp_dev_stats *stats)
{
	struct mlx5e_priv *priv = netdev_priv(psd->main_netdev);
	struct mlx5e_psp_stats nstats;

	mlx5e_accel_psp_fs_get_stats_fill(priv, &nstats);
	stats->rx_packets = nstats.psp_rx_pkts;
	stats->rx_bytes = nstats.psp_rx_bytes;
	stats->rx_auth_fail = nstats.psp_rx_pkts_auth_fail;
	stats->rx_error = nstats.psp_rx_pkts_frame_err;
	stats->rx_bad = nstats.psp_rx_pkts_drop;
	stats->tx_packets = nstats.psp_tx_pkts;
	stats->tx_bytes = nstats.psp_tx_bytes;
	stats->tx_error = atomic_read(&priv->psp->tx_drop);
}

static struct psp_dev_ops mlx5_psp_ops = {
	.set_config   = mlx5e_psp_set_config,
	.rx_spi_alloc = mlx5e_psp_rx_spi_alloc,
	.tx_key_add   = mlx5e_psp_assoc_add,
	.tx_key_del   = mlx5e_psp_assoc_del,
	.key_rotate   = mlx5e_psp_key_rotate,
	.get_stats    = mlx5e_psp_get_stats,
};

void mlx5e_psp_unregister(struct mlx5e_priv *priv)
{
	struct mlx5e_psp *psp = priv->psp;

	if (!psp || !psp->psd)
		return;

	psp_dev_unregister(psp->psd);
	psp->psd = NULL;
}

int mlx5e_psp_register(struct mlx5e_priv *priv)
{
	struct mlx5e_psp *psp = priv->psp;
	struct psp_dev *psd;

	/* FW Caps missing */
	if (!priv->psp)
		return 0;

	psp->caps.assoc_drv_spc = sizeof(u32);
	psp->caps.versions = 1 << PSP_VERSION_HDR0_AES_GCM_128;
	if (MLX5_CAP_PSP(priv->mdev, psp_crypto_esp_aes_gcm_256_encrypt) &&
	    MLX5_CAP_PSP(priv->mdev, psp_crypto_esp_aes_gcm_256_decrypt))
		psp->caps.versions |= 1 << PSP_VERSION_HDR0_AES_GCM_256;

	psd = psp_dev_create(priv->netdev, &mlx5_psp_ops, &psp->caps, NULL);
	if (IS_ERR(psd)) {
		mlx5_core_err(priv->mdev, "PSP failed to register due to %pe\n",
			      psd);
		return PTR_ERR(psd);
	}
	psp->psd = psd;

	return 0;
}

int mlx5e_psp_init(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_psp_fs *fs;
	struct mlx5e_psp *psp;
	int err;

	if (!mlx5_is_psp_device(mdev)) {
		mlx5_core_dbg(mdev, "PSP offload not supported\n");
		return 0;
	}

	if (!MLX5_CAP_ETH(mdev, swp)) {
		mlx5_core_dbg(mdev, "SWP not supported\n");
		return 0;
	}

	if (!MLX5_CAP_ETH(mdev, swp_csum)) {
		mlx5_core_dbg(mdev, "SWP checksum not supported\n");
		return 0;
	}

	if (!MLX5_CAP_ETH(mdev, swp_csum_l4_partial)) {
		mlx5_core_dbg(mdev, "SWP L4 partial checksum not supported\n");
		return 0;
	}

	if (!MLX5_CAP_ETH(mdev, swp_lso)) {
		mlx5_core_dbg(mdev, "PSP LSO not supported\n");
		return 0;
	}

	psp = kzalloc_obj(*psp);
	if (!psp)
		return -ENOMEM;

	fs = mlx5e_accel_psp_fs_init(priv);
	if (IS_ERR(fs)) {
		err = PTR_ERR(fs);
		kfree(psp);
		return err;
	}

	psp->fs = fs;
	priv->psp = psp;

	mlx5_core_dbg(priv->mdev, "PSP attached to netdevice\n");
	return 0;
}

void mlx5e_psp_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5e_psp *psp = priv->psp;

	if (!psp)
		return;

	WARN_ON(atomic_read(&psp->tx_key_cnt));
	mlx5e_accel_psp_fs_cleanup(psp->fs);
	priv->psp = NULL;
	kfree(psp);
}
