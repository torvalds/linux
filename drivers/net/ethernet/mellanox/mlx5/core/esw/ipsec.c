// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/mlx5/device.h>
#include <linux/mlx5/vport.h>
#include "mlx5_core.h"
#include "eswitch.h"

static int esw_ipsec_vf_query_generic(struct mlx5_core_dev *dev, u16 vport_num, bool *result)
{
	int query_sz = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	void *hca_cap, *query_cap;
	int err;

	if (!MLX5_CAP_GEN(dev, vhca_resource_manager))
		return -EOPNOTSUPP;

	if (!mlx5_esw_ipsec_vf_offload_supported(dev)) {
		*result = false;
		return 0;
	}

	query_cap = kvzalloc(query_sz, GFP_KERNEL);
	if (!query_cap)
		return -ENOMEM;

	err = mlx5_vport_get_other_func_general_cap(dev, vport_num, query_cap);
	if (err)
		goto free;

	hca_cap = MLX5_ADDR_OF(query_hca_cap_out, query_cap, capability);
	*result = MLX5_GET(cmd_hca_cap, hca_cap, ipsec_offload);
free:
	kvfree(query_cap);
	return err;
}

enum esw_vport_ipsec_offload {
	MLX5_ESW_VPORT_IPSEC_CRYPTO_OFFLOAD,
	MLX5_ESW_VPORT_IPSEC_PACKET_OFFLOAD,
};

int mlx5_esw_ipsec_vf_offload_get(struct mlx5_core_dev *dev, struct mlx5_vport *vport)
{
	int query_sz = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	void *hca_cap, *query_cap;
	bool ipsec_enabled;
	int err;

	/* Querying IPsec caps only makes sense when generic ipsec_offload
	 * HCA cap is enabled
	 */
	err = esw_ipsec_vf_query_generic(dev, vport->vport, &ipsec_enabled);
	if (err)
		return err;

	if (!ipsec_enabled) {
		vport->info.ipsec_crypto_enabled = false;
		vport->info.ipsec_packet_enabled = false;
		return 0;
	}

	query_cap = kvzalloc(query_sz, GFP_KERNEL);
	if (!query_cap)
		return -ENOMEM;

	err = mlx5_vport_get_other_func_cap(dev, vport->vport, query_cap, MLX5_CAP_IPSEC);
	if (err)
		goto free;

	hca_cap = MLX5_ADDR_OF(query_hca_cap_out, query_cap, capability);
	vport->info.ipsec_crypto_enabled =
		MLX5_GET(ipsec_cap, hca_cap, ipsec_crypto_offload);
	vport->info.ipsec_packet_enabled =
		MLX5_GET(ipsec_cap, hca_cap, ipsec_full_offload);
free:
	kvfree(query_cap);
	return err;
}

static int esw_ipsec_vf_set_generic(struct mlx5_core_dev *dev, u16 vport_num, bool ipsec_ofld)
{
	int query_sz = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	int set_sz = MLX5_ST_SZ_BYTES(set_hca_cap_in);
	void *hca_cap, *query_cap, *cap;
	int ret;

	if (!MLX5_CAP_GEN(dev, vhca_resource_manager))
		return -EOPNOTSUPP;

	query_cap = kvzalloc(query_sz, GFP_KERNEL);
	hca_cap = kvzalloc(set_sz, GFP_KERNEL);
	if (!hca_cap || !query_cap) {
		ret = -ENOMEM;
		goto free;
	}

	ret = mlx5_vport_get_other_func_general_cap(dev, vport_num, query_cap);
	if (ret)
		goto free;

	cap = MLX5_ADDR_OF(set_hca_cap_in, hca_cap, capability);
	memcpy(cap, MLX5_ADDR_OF(query_hca_cap_out, query_cap, capability),
	       MLX5_UN_SZ_BYTES(hca_cap_union));
	MLX5_SET(cmd_hca_cap, cap, ipsec_offload, ipsec_ofld);

	MLX5_SET(set_hca_cap_in, hca_cap, opcode, MLX5_CMD_OP_SET_HCA_CAP);
	MLX5_SET(set_hca_cap_in, hca_cap, other_function, 1);
	MLX5_SET(set_hca_cap_in, hca_cap, function_id, vport_num);

	MLX5_SET(set_hca_cap_in, hca_cap, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE << 1);
	ret = mlx5_cmd_exec_in(dev, set_hca_cap, hca_cap);
free:
	kvfree(hca_cap);
	kvfree(query_cap);
	return ret;
}

static int esw_ipsec_vf_set_bytype(struct mlx5_core_dev *dev, struct mlx5_vport *vport,
				   bool enable, enum esw_vport_ipsec_offload type)
{
	int query_sz = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	int set_sz = MLX5_ST_SZ_BYTES(set_hca_cap_in);
	void *hca_cap, *query_cap, *cap;
	int ret;

	if (!MLX5_CAP_GEN(dev, vhca_resource_manager))
		return -EOPNOTSUPP;

	query_cap = kvzalloc(query_sz, GFP_KERNEL);
	hca_cap = kvzalloc(set_sz, GFP_KERNEL);
	if (!hca_cap || !query_cap) {
		ret = -ENOMEM;
		goto free;
	}

	ret = mlx5_vport_get_other_func_cap(dev, vport->vport, query_cap, MLX5_CAP_IPSEC);
	if (ret)
		goto free;

	cap = MLX5_ADDR_OF(set_hca_cap_in, hca_cap, capability);
	memcpy(cap, MLX5_ADDR_OF(query_hca_cap_out, query_cap, capability),
	       MLX5_UN_SZ_BYTES(hca_cap_union));

	switch (type) {
	case MLX5_ESW_VPORT_IPSEC_CRYPTO_OFFLOAD:
		MLX5_SET(ipsec_cap, cap, ipsec_crypto_offload, enable);
		break;
	case MLX5_ESW_VPORT_IPSEC_PACKET_OFFLOAD:
		MLX5_SET(ipsec_cap, cap, ipsec_full_offload, enable);
		break;
	default:
		ret = -EOPNOTSUPP;
		goto free;
	}

	MLX5_SET(set_hca_cap_in, hca_cap, opcode, MLX5_CMD_OP_SET_HCA_CAP);
	MLX5_SET(set_hca_cap_in, hca_cap, other_function, 1);
	MLX5_SET(set_hca_cap_in, hca_cap, function_id, vport->vport);

	MLX5_SET(set_hca_cap_in, hca_cap, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_IPSEC << 1);
	ret = mlx5_cmd_exec_in(dev, set_hca_cap, hca_cap);
free:
	kvfree(hca_cap);
	kvfree(query_cap);
	return ret;
}

static int esw_ipsec_vf_crypto_aux_caps_set(struct mlx5_core_dev *dev, u16 vport_num, bool enable)
{
	int query_sz = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	int set_sz = MLX5_ST_SZ_BYTES(set_hca_cap_in);
	struct mlx5_eswitch *esw = dev->priv.eswitch;
	void *hca_cap, *query_cap, *cap;
	int ret;

	query_cap = kvzalloc(query_sz, GFP_KERNEL);
	hca_cap = kvzalloc(set_sz, GFP_KERNEL);
	if (!hca_cap || !query_cap) {
		ret = -ENOMEM;
		goto free;
	}

	ret = mlx5_vport_get_other_func_cap(dev, vport_num, query_cap, MLX5_CAP_ETHERNET_OFFLOADS);
	if (ret)
		goto free;

	cap = MLX5_ADDR_OF(set_hca_cap_in, hca_cap, capability);
	memcpy(cap, MLX5_ADDR_OF(query_hca_cap_out, query_cap, capability),
	       MLX5_UN_SZ_BYTES(hca_cap_union));
	MLX5_SET(per_protocol_networking_offload_caps, cap, insert_trailer, enable);
	MLX5_SET(set_hca_cap_in, hca_cap, opcode, MLX5_CMD_OP_SET_HCA_CAP);
	MLX5_SET(set_hca_cap_in, hca_cap, other_function, 1);
	MLX5_SET(set_hca_cap_in, hca_cap, function_id, vport_num);
	MLX5_SET(set_hca_cap_in, hca_cap, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_ETHERNET_OFFLOADS << 1);
	ret = mlx5_cmd_exec_in(esw->dev, set_hca_cap, hca_cap);
free:
	kvfree(hca_cap);
	kvfree(query_cap);
	return ret;
}

static int esw_ipsec_vf_offload_set_bytype(struct mlx5_eswitch *esw, struct mlx5_vport *vport,
					   bool enable, enum esw_vport_ipsec_offload type)
{
	struct mlx5_core_dev *dev = esw->dev;
	int err;

	if (vport->vport == MLX5_VPORT_PF)
		return -EOPNOTSUPP;

	if (type == MLX5_ESW_VPORT_IPSEC_CRYPTO_OFFLOAD) {
		err = esw_ipsec_vf_crypto_aux_caps_set(dev, vport->vport, enable);
		if (err)
			return err;
	}

	if (enable) {
		err = esw_ipsec_vf_set_generic(dev, vport->vport, enable);
		if (err)
			return err;
		err = esw_ipsec_vf_set_bytype(dev, vport, enable, type);
		if (err)
			return err;
	} else {
		err = esw_ipsec_vf_set_bytype(dev, vport, enable, type);
		if (err)
			return err;
		err = mlx5_esw_ipsec_vf_offload_get(dev, vport);
		if (err)
			return err;

		/* The generic ipsec_offload cap can be disabled only if both
		 * ipsec_crypto_offload and ipsec_full_offload aren't enabled.
		 */
		if (!vport->info.ipsec_crypto_enabled &&
		    !vport->info.ipsec_packet_enabled) {
			err = esw_ipsec_vf_set_generic(dev, vport->vport, enable);
			if (err)
				return err;
		}
	}

	switch (type) {
	case MLX5_ESW_VPORT_IPSEC_CRYPTO_OFFLOAD:
		vport->info.ipsec_crypto_enabled = enable;
		break;
	case MLX5_ESW_VPORT_IPSEC_PACKET_OFFLOAD:
		vport->info.ipsec_packet_enabled = enable;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int esw_ipsec_offload_supported(struct mlx5_core_dev *dev, u16 vport_num)
{
	int query_sz = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	void *hca_cap, *query_cap;
	int ret;

	query_cap = kvzalloc(query_sz, GFP_KERNEL);
	if (!query_cap)
		return -ENOMEM;

	ret = mlx5_vport_get_other_func_cap(dev, vport_num, query_cap, MLX5_CAP_GENERAL);
	if (ret)
		goto free;

	hca_cap = MLX5_ADDR_OF(query_hca_cap_out, query_cap, capability);
	if (!MLX5_GET(cmd_hca_cap, hca_cap, log_max_dek))
		ret = -EOPNOTSUPP;
free:
	kvfree(query_cap);
	return ret;
}

bool mlx5_esw_ipsec_vf_offload_supported(struct mlx5_core_dev *dev)
{
	/* Old firmware doesn't support ipsec_offload capability for VFs. This
	 * can be detected by checking reformat_add_esp_trasport capability -
	 * when this cap isn't supported it means firmware cannot be trusted
	 * about what it reports for ipsec_offload cap.
	 */
	return MLX5_CAP_FLOWTABLE_NIC_TX(dev, reformat_add_esp_trasport);
}

int mlx5_esw_ipsec_vf_crypto_offload_supported(struct mlx5_core_dev *dev,
					       u16 vport_num)
{
	int query_sz = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	void *hca_cap, *query_cap;
	int err;

	if (!mlx5_esw_ipsec_vf_offload_supported(dev))
		return -EOPNOTSUPP;

	err = esw_ipsec_offload_supported(dev, vport_num);
	if (err)
		return err;

	query_cap = kvzalloc(query_sz, GFP_KERNEL);
	if (!query_cap)
		return -ENOMEM;

	err = mlx5_vport_get_other_func_cap(dev, vport_num, query_cap, MLX5_CAP_ETHERNET_OFFLOADS);
	if (err)
		goto free;

	hca_cap = MLX5_ADDR_OF(query_hca_cap_out, query_cap, capability);
	if (!MLX5_GET(per_protocol_networking_offload_caps, hca_cap, swp))
		goto free;

free:
	kvfree(query_cap);
	return err;
}

int mlx5_esw_ipsec_vf_packet_offload_supported(struct mlx5_core_dev *dev,
					       u16 vport_num)
{
	int query_sz = MLX5_ST_SZ_BYTES(query_hca_cap_out);
	void *hca_cap, *query_cap;
	int ret;

	if (!mlx5_esw_ipsec_vf_offload_supported(dev))
		return -EOPNOTSUPP;

	ret = esw_ipsec_offload_supported(dev, vport_num);
	if (ret)
		return ret;

	query_cap = kvzalloc(query_sz, GFP_KERNEL);
	if (!query_cap)
		return -ENOMEM;

	ret = mlx5_vport_get_other_func_cap(dev, vport_num, query_cap, MLX5_CAP_FLOW_TABLE);
	if (ret)
		goto out;

	hca_cap = MLX5_ADDR_OF(query_hca_cap_out, query_cap, capability);
	if (!MLX5_GET(flow_table_nic_cap, hca_cap, flow_table_properties_nic_receive.decap)) {
		ret = -EOPNOTSUPP;
		goto out;
	}

out:
	kvfree(query_cap);
	return ret;
}

int mlx5_esw_ipsec_vf_crypto_offload_set(struct mlx5_eswitch *esw, struct mlx5_vport *vport,
					 bool enable)
{
	return esw_ipsec_vf_offload_set_bytype(esw, vport, enable,
					       MLX5_ESW_VPORT_IPSEC_CRYPTO_OFFLOAD);
}

int mlx5_esw_ipsec_vf_packet_offload_set(struct mlx5_eswitch *esw, struct mlx5_vport *vport,
					 bool enable)
{
	return esw_ipsec_vf_offload_set_bytype(esw, vport, enable,
					       MLX5_ESW_VPORT_IPSEC_PACKET_OFFLOAD);
}
