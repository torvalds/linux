// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "fs_core.h"
#include "lib/ipsec_fs_roce.h"
#include "mlx5_core.h"

struct mlx5_ipsec_miss {
	struct mlx5_flow_group *group;
	struct mlx5_flow_handle *rule;
};

struct mlx5_ipsec_rx_roce {
	struct mlx5_flow_group *g;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_handle *rule;
	struct mlx5_ipsec_miss roce_miss;

	struct mlx5_flow_table *ft_rdma;
	struct mlx5_flow_namespace *ns_rdma;
};

struct mlx5_ipsec_tx_roce {
	struct mlx5_flow_group *g;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_namespace *ns;
};

struct mlx5_ipsec_fs {
	struct mlx5_ipsec_rx_roce ipv4_rx;
	struct mlx5_ipsec_rx_roce ipv6_rx;
	struct mlx5_ipsec_tx_roce tx;
};

static void ipsec_fs_roce_setup_udp_dport(struct mlx5_flow_spec *spec,
					  u16 dport)
{
	spec->match_criteria_enable |= MLX5_MATCH_OUTER_HEADERS;
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_protocol);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_protocol, IPPROTO_UDP);
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.udp_dport);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.udp_dport, dport);
}

static int
ipsec_fs_roce_rx_rule_setup(struct mlx5_core_dev *mdev,
			    struct mlx5_flow_destination *default_dst,
			    struct mlx5_ipsec_rx_roce *roce)
{
	struct mlx5_flow_destination dst = {};
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	int err = 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	ipsec_fs_roce_setup_udp_dport(spec, ROCE_V2_UDP_DPORT);

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	dst.type = MLX5_FLOW_DESTINATION_TYPE_TABLE_TYPE;
	dst.ft = roce->ft_rdma;
	rule = mlx5_add_flow_rules(roce->ft, spec, &flow_act, &dst, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Fail to add RX RoCE IPsec rule err=%d\n",
			      err);
		goto fail_add_rule;
	}

	roce->rule = rule;

	memset(spec, 0, sizeof(*spec));
	rule = mlx5_add_flow_rules(roce->ft, spec, &flow_act, default_dst, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Fail to add RX RoCE IPsec miss rule err=%d\n",
			      err);
		goto fail_add_default_rule;
	}

	roce->roce_miss.rule = rule;

	kvfree(spec);
	return 0;

fail_add_default_rule:
	mlx5_del_flow_rules(roce->rule);
fail_add_rule:
	kvfree(spec);
	return err;
}

static int ipsec_fs_roce_tx_rule_setup(struct mlx5_core_dev *mdev,
				       struct mlx5_ipsec_tx_roce *roce,
				       struct mlx5_flow_table *pol_ft)
{
	struct mlx5_flow_destination dst = {};
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *rule;
	int err = 0;

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	dst.type = MLX5_FLOW_DESTINATION_TYPE_TABLE_TYPE;
	dst.ft = pol_ft;
	rule = mlx5_add_flow_rules(roce->ft, NULL, &flow_act, &dst,
				   1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Fail to add TX RoCE IPsec rule err=%d\n",
			      err);
		goto out;
	}
	roce->rule = rule;

out:
	return err;
}

void mlx5_ipsec_fs_roce_tx_destroy(struct mlx5_ipsec_fs *ipsec_roce)
{
	struct mlx5_ipsec_tx_roce *tx_roce;

	if (!ipsec_roce)
		return;

	tx_roce = &ipsec_roce->tx;

	mlx5_del_flow_rules(tx_roce->rule);
	mlx5_destroy_flow_group(tx_roce->g);
	mlx5_destroy_flow_table(tx_roce->ft);
}

#define MLX5_TX_ROCE_GROUP_SIZE BIT(0)

int mlx5_ipsec_fs_roce_tx_create(struct mlx5_core_dev *mdev,
				 struct mlx5_ipsec_fs *ipsec_roce,
				 struct mlx5_flow_table *pol_ft)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_ipsec_tx_roce *roce;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *g;
	int ix = 0;
	int err;
	u32 *in;

	if (!ipsec_roce)
		return 0;

	roce = &ipsec_roce->tx;

	in = kvzalloc(MLX5_ST_SZ_BYTES(create_flow_group_in), GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	ft_attr.max_fte = 1;
	ft = mlx5_create_flow_table(roce->ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		mlx5_core_err(mdev, "Fail to create RoCE IPsec tx ft err=%d\n", err);
		goto free_in;
	}

	roce->ft = ft;

	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5_TX_ROCE_GROUP_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	g = mlx5_create_flow_group(ft, in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		mlx5_core_err(mdev, "Fail to create RoCE IPsec tx group err=%d\n", err);
		goto destroy_table;
	}
	roce->g = g;

	err = ipsec_fs_roce_tx_rule_setup(mdev, roce, pol_ft);
	if (err) {
		mlx5_core_err(mdev, "Fail to create RoCE IPsec tx rules err=%d\n", err);
		goto destroy_group;
	}

	kvfree(in);
	return 0;

destroy_group:
	mlx5_destroy_flow_group(roce->g);
destroy_table:
	mlx5_destroy_flow_table(ft);
free_in:
	kvfree(in);
	return err;
}

struct mlx5_flow_table *mlx5_ipsec_fs_roce_ft_get(struct mlx5_ipsec_fs *ipsec_roce, u32 family)
{
	struct mlx5_ipsec_rx_roce *rx_roce;

	if (!ipsec_roce)
		return NULL;

	rx_roce = (family == AF_INET) ? &ipsec_roce->ipv4_rx :
					&ipsec_roce->ipv6_rx;

	return rx_roce->ft;
}

void mlx5_ipsec_fs_roce_rx_destroy(struct mlx5_ipsec_fs *ipsec_roce, u32 family)
{
	struct mlx5_ipsec_rx_roce *rx_roce;

	if (!ipsec_roce)
		return;

	rx_roce = (family == AF_INET) ? &ipsec_roce->ipv4_rx :
					&ipsec_roce->ipv6_rx;

	mlx5_del_flow_rules(rx_roce->roce_miss.rule);
	mlx5_del_flow_rules(rx_roce->rule);
	mlx5_destroy_flow_table(rx_roce->ft_rdma);
	mlx5_destroy_flow_group(rx_roce->roce_miss.group);
	mlx5_destroy_flow_group(rx_roce->g);
	mlx5_destroy_flow_table(rx_roce->ft);
}

#define MLX5_RX_ROCE_GROUP_SIZE BIT(0)

int mlx5_ipsec_fs_roce_rx_create(struct mlx5_core_dev *mdev,
				 struct mlx5_ipsec_fs *ipsec_roce,
				 struct mlx5_flow_namespace *ns,
				 struct mlx5_flow_destination *default_dst,
				 u32 family, u32 level, u32 prio)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_ipsec_rx_roce *roce;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *g;
	void *outer_headers_c;
	int ix = 0;
	u32 *in;
	int err;
	u8 *mc;

	if (!ipsec_roce)
		return 0;

	roce = (family == AF_INET) ? &ipsec_roce->ipv4_rx :
				     &ipsec_roce->ipv6_rx;

	ft_attr.max_fte = 2;
	ft_attr.level = level;
	ft_attr.prio = prio;
	ft = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		mlx5_core_err(mdev, "Fail to create RoCE IPsec rx ft at nic err=%d\n", err);
		return err;
	}

	roce->ft = ft;

	in = kvzalloc(MLX5_ST_SZ_BYTES(create_flow_group_in), GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto fail_nomem;
	}

	mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	outer_headers_c = MLX5_ADDR_OF(fte_match_param, mc, outer_headers);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, ip_protocol);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, udp_dport);

	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5_RX_ROCE_GROUP_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	g = mlx5_create_flow_group(ft, in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		mlx5_core_err(mdev, "Fail to create RoCE IPsec rx group at nic err=%d\n", err);
		goto fail_group;
	}
	roce->g = g;

	memset(in, 0, MLX5_ST_SZ_BYTES(create_flow_group_in));
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5_RX_ROCE_GROUP_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	g = mlx5_create_flow_group(ft, in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		mlx5_core_err(mdev, "Fail to create RoCE IPsec rx miss group at nic err=%d\n", err);
		goto fail_mgroup;
	}
	roce->roce_miss.group = g;

	memset(&ft_attr, 0, sizeof(ft_attr));
	if (family == AF_INET)
		ft_attr.level = 1;
	ft = mlx5_create_flow_table(roce->ns_rdma, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		mlx5_core_err(mdev, "Fail to create RoCE IPsec rx ft at rdma err=%d\n", err);
		goto fail_rdma_table;
	}

	roce->ft_rdma = ft;

	err = ipsec_fs_roce_rx_rule_setup(mdev, default_dst, roce);
	if (err) {
		mlx5_core_err(mdev, "Fail to create RoCE IPsec rx rules err=%d\n", err);
		goto fail_setup_rule;
	}

	kvfree(in);
	return 0;

fail_setup_rule:
	mlx5_destroy_flow_table(roce->ft_rdma);
fail_rdma_table:
	mlx5_destroy_flow_group(roce->roce_miss.group);
fail_mgroup:
	mlx5_destroy_flow_group(roce->g);
fail_group:
	kvfree(in);
fail_nomem:
	mlx5_destroy_flow_table(roce->ft);
	return err;
}

void mlx5_ipsec_fs_roce_cleanup(struct mlx5_ipsec_fs *ipsec_roce)
{
	kfree(ipsec_roce);
}

struct mlx5_ipsec_fs *mlx5_ipsec_fs_roce_init(struct mlx5_core_dev *mdev)
{
	struct mlx5_ipsec_fs *roce_ipsec;
	struct mlx5_flow_namespace *ns;

	ns = mlx5_get_flow_namespace(mdev, MLX5_FLOW_NAMESPACE_RDMA_RX_IPSEC);
	if (!ns) {
		mlx5_core_err(mdev, "Failed to get RoCE rx ns\n");
		return NULL;
	}

	roce_ipsec = kzalloc(sizeof(*roce_ipsec), GFP_KERNEL);
	if (!roce_ipsec)
		return NULL;

	roce_ipsec->ipv4_rx.ns_rdma = ns;
	roce_ipsec->ipv6_rx.ns_rdma = ns;

	ns = mlx5_get_flow_namespace(mdev, MLX5_FLOW_NAMESPACE_RDMA_TX_IPSEC);
	if (!ns) {
		mlx5_core_err(mdev, "Failed to get RoCE tx ns\n");
		goto err_tx;
	}

	roce_ipsec->tx.ns = ns;

	return roce_ipsec;

err_tx:
	kfree(roce_ipsec);
	return NULL;
}
