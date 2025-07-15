// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES.

#include "en.h"
#include "pcie_cong_event.h"

struct mlx5e_pcie_cong_thresh {
	u16 inbound_high;
	u16 inbound_low;
	u16 outbound_high;
	u16 outbound_low;
};

struct mlx5e_pcie_cong_event {
	u64 obj_id;

	struct mlx5e_priv *priv;
};

/* In units of 0.01 % */
static const struct mlx5e_pcie_cong_thresh default_thresh_config = {
	.inbound_high = 9000,
	.inbound_low = 7500,
	.outbound_high = 9000,
	.outbound_low = 7500,
};

static int
mlx5_cmd_pcie_cong_event_set(struct mlx5_core_dev *dev,
			     const struct mlx5e_pcie_cong_thresh *config,
			     u64 *obj_id)
{
	u32 in[MLX5_ST_SZ_DW(pcie_cong_event_cmd_in)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	void *cong_obj;
	void *hdr;
	int err;

	hdr = MLX5_ADDR_OF(pcie_cong_event_cmd_in, in, hdr);
	cong_obj = MLX5_ADDR_OF(pcie_cong_event_cmd_in, in, cong_obj);

	MLX5_SET(general_obj_in_cmd_hdr, hdr, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);

	MLX5_SET(general_obj_in_cmd_hdr, hdr, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_PCIE_CONG_EVENT);

	MLX5_SET(pcie_cong_event_obj, cong_obj, inbound_event_en, 1);
	MLX5_SET(pcie_cong_event_obj, cong_obj, outbound_event_en, 1);

	MLX5_SET(pcie_cong_event_obj, cong_obj,
		 inbound_cong_high_threshold, config->inbound_high);
	MLX5_SET(pcie_cong_event_obj, cong_obj,
		 inbound_cong_low_threshold, config->inbound_low);

	MLX5_SET(pcie_cong_event_obj, cong_obj,
		 outbound_cong_high_threshold, config->outbound_high);
	MLX5_SET(pcie_cong_event_obj, cong_obj,
		 outbound_cong_low_threshold, config->outbound_low);

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	*obj_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);

	mlx5_core_dbg(dev, "PCIe congestion event (obj_id=%llu) created. Config: in: [%u, %u], out: [%u, %u]\n",
		      *obj_id,
		      config->inbound_high, config->inbound_low,
		      config->outbound_high, config->outbound_low);

	return 0;
}

static int mlx5_cmd_pcie_cong_event_destroy(struct mlx5_core_dev *dev,
					    u64 obj_id)
{
	u32 in[MLX5_ST_SZ_DW(pcie_cong_event_cmd_in)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	void *hdr;

	hdr = MLX5_ADDR_OF(pcie_cong_event_cmd_in, in, hdr);
	MLX5_SET(general_obj_in_cmd_hdr, hdr, opcode,
		 MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, hdr, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_PCIE_CONG_EVENT);
	MLX5_SET(general_obj_in_cmd_hdr, hdr, obj_id, obj_id);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5e_pcie_cong_event_init(struct mlx5e_priv *priv)
{
	struct mlx5e_pcie_cong_event *cong_event;
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	if (!mlx5_pcie_cong_event_supported(mdev))
		return 0;

	cong_event = kvzalloc_node(sizeof(*cong_event), GFP_KERNEL,
				   mdev->priv.numa_node);
	if (!cong_event)
		return -ENOMEM;

	cong_event->priv = priv;

	err = mlx5_cmd_pcie_cong_event_set(mdev, &default_thresh_config,
					   &cong_event->obj_id);
	if (err) {
		mlx5_core_warn(mdev, "Error creating a PCIe congestion event object\n");
		goto err_free;
	}

	priv->cong_event = cong_event;

	return 0;

err_free:
	kvfree(cong_event);

	return err;
}

void mlx5e_pcie_cong_event_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5e_pcie_cong_event *cong_event = priv->cong_event;
	struct mlx5_core_dev *mdev = priv->mdev;

	if (!cong_event)
		return;

	priv->cong_event = NULL;

	if (mlx5_cmd_pcie_cong_event_destroy(mdev, cong_event->obj_id))
		mlx5_core_warn(mdev, "Error destroying PCIe congestion event (obj_id=%llu)\n",
			       cong_event->obj_id);

	kvfree(cong_event);
}
