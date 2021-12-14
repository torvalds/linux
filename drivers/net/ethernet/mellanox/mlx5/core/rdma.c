// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies */

#include <linux/mlx5/vport.h>
#include <rdma/ib_verbs.h>
#include <net/addrconf.h>

#include "lib/mlx5.h"
#include "eswitch.h"
#include "fs_core.h"
#include "rdma.h"

static void mlx5_rdma_disable_roce_steering(struct mlx5_core_dev *dev)
{
	struct mlx5_core_roce *roce = &dev->priv.roce;

	mlx5_del_flow_rules(roce->allow_rule);
	mlx5_destroy_flow_group(roce->fg);
	mlx5_destroy_flow_table(roce->ft);
}

static int mlx5_rdma_enable_roce_steering(struct mlx5_core_dev *dev)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_core_roce *roce = &dev->priv.roce;
	struct mlx5_flow_handle *flow_rule = NULL;
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_namespace *ns = NULL;
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_spec *spec;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	void *match_criteria;
	u32 *flow_group_in;
	void *misc;
	int err;

	if (!(MLX5_CAP_FLOWTABLE_RDMA_RX(dev, ft_support) &&
	      MLX5_CAP_FLOWTABLE_RDMA_RX(dev, table_miss_action_domain)))
		return -EOPNOTSUPP;

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!flow_group_in)
		return -ENOMEM;
	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		kvfree(flow_group_in);
		return -ENOMEM;
	}

	ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_RDMA_RX_KERNEL);
	if (!ns) {
		mlx5_core_err(dev, "Failed to get RDMA RX namespace");
		err = -EOPNOTSUPP;
		goto free;
	}

	ft_attr.max_fte = 1;
	ft = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(ft)) {
		mlx5_core_err(dev, "Failed to create RDMA RX flow table");
		err = PTR_ERR(ft);
		goto free;
	}

	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_MISC_PARAMETERS);
	match_criteria = MLX5_ADDR_OF(create_flow_group_in, flow_group_in,
				      match_criteria);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria,
			 misc_parameters.source_port);

	fg = mlx5_create_flow_group(ft, flow_group_in);
	if (IS_ERR(fg)) {
		err = PTR_ERR(fg);
		mlx5_core_err(dev, "Failed to create RDMA RX flow group err(%d)\n", err);
		goto destroy_flow_table;
	}

	spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS;
	misc = MLX5_ADDR_OF(fte_match_param, spec->match_value,
			    misc_parameters);
	MLX5_SET(fte_match_set_misc, misc, source_port,
		 dev->priv.eswitch->manager_vport);
	misc = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
			    misc_parameters);
	MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_port);

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_ALLOW;
	flow_rule = mlx5_add_flow_rules(ft, spec, &flow_act, NULL, 0);
	if (IS_ERR(flow_rule)) {
		err = PTR_ERR(flow_rule);
		mlx5_core_err(dev, "Failed to add RoCE allow rule, err=%d\n",
			      err);
		goto destroy_flow_group;
	}

	kvfree(spec);
	kvfree(flow_group_in);
	roce->ft = ft;
	roce->fg = fg;
	roce->allow_rule = flow_rule;

	return 0;

destroy_flow_group:
	mlx5_destroy_flow_group(fg);
destroy_flow_table:
	mlx5_destroy_flow_table(ft);
free:
	kvfree(spec);
	kvfree(flow_group_in);
	return err;
}

static void mlx5_rdma_del_roce_addr(struct mlx5_core_dev *dev)
{
	mlx5_core_roce_gid_set(dev, 0, 0, 0,
			       NULL, NULL, false, 0, 1);
}

static void mlx5_rdma_make_default_gid(struct mlx5_core_dev *dev, union ib_gid *gid)
{
	u8 hw_id[ETH_ALEN];

	mlx5_query_mac_address(dev, hw_id);
	gid->global.subnet_prefix = cpu_to_be64(0xfe80000000000000LL);
	addrconf_addr_eui48(&gid->raw[8], hw_id);
}

static int mlx5_rdma_add_roce_addr(struct mlx5_core_dev *dev)
{
	union ib_gid gid;
	u8 mac[ETH_ALEN];

	mlx5_rdma_make_default_gid(dev, &gid);
	return mlx5_core_roce_gid_set(dev, 0,
				      MLX5_ROCE_VERSION_1,
				      0, gid.raw, mac,
				      false, 0, 1);
}

void mlx5_rdma_disable_roce(struct mlx5_core_dev *dev)
{
	struct mlx5_core_roce *roce = &dev->priv.roce;

	if (!roce->ft)
		return;

	mlx5_rdma_disable_roce_steering(dev);
	mlx5_rdma_del_roce_addr(dev);
	mlx5_nic_vport_disable_roce(dev);
}

void mlx5_rdma_enable_roce(struct mlx5_core_dev *dev)
{
	int err;

	if (!MLX5_CAP_GEN(dev, roce))
		return;

	err = mlx5_nic_vport_enable_roce(dev);
	if (err) {
		mlx5_core_err(dev, "Failed to enable RoCE: %d\n", err);
		return;
	}

	err = mlx5_rdma_add_roce_addr(dev);
	if (err) {
		mlx5_core_err(dev, "Failed to add RoCE address: %d\n", err);
		goto disable_roce;
	}

	err = mlx5_rdma_enable_roce_steering(dev);
	if (err) {
		mlx5_core_err(dev, "Failed to enable RoCE steering: %d\n", err);
		goto del_roce_addr;
	}

	return;

del_roce_addr:
	mlx5_rdma_del_roce_addr(dev);
disable_roce:
	mlx5_nic_vport_disable_roce(dev);
	return;
}
