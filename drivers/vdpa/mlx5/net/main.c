// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Ltd. */

#include <linux/module.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/device.h>
#include "mlx5_vdpa_ifc.h"
#include "mlx5_vnet.h"

MODULE_AUTHOR("Eli Cohen <eli@mellanox.com>");
MODULE_DESCRIPTION("Mellanox VDPA driver");
MODULE_LICENSE("Dual BSD/GPL");

static bool required_caps_supported(struct mlx5_core_dev *mdev)
{
	u8 event_mode;
	u64 got;

	got = MLX5_CAP_GEN_64(mdev, general_obj_types);

	if (!(got & MLX5_GENERAL_OBJ_TYPES_CAP_VIRTIO_NET_Q))
		return false;

	event_mode = MLX5_CAP_DEV_VDPA_EMULATION(mdev, event_mode);
	if (!(event_mode & MLX5_VIRTIO_Q_EVENT_MODE_QP_MODE))
		return false;

	if (!MLX5_CAP_DEV_VDPA_EMULATION(mdev, eth_frame_offload_type))
		return false;

	return true;
}

static void *mlx5_vdpa_add(struct mlx5_core_dev *mdev)
{
	struct mlx5_vdpa_dev *vdev;

	if (mlx5_core_is_pf(mdev))
		return NULL;

	if (!required_caps_supported(mdev)) {
		dev_info(mdev->device, "virtio net emulation not supported\n");
		return NULL;
	}
	vdev = mlx5_vdpa_add_dev(mdev);
	if (IS_ERR(vdev))
		return NULL;

	return vdev;
}

static void mlx5_vdpa_remove(struct mlx5_core_dev *mdev, void *context)
{
	struct mlx5_vdpa_dev *vdev = context;

	mlx5_vdpa_remove_dev(vdev);
}

static struct mlx5_interface mlx5_vdpa_interface = {
	.add = mlx5_vdpa_add,
	.remove = mlx5_vdpa_remove,
	.protocol = MLX5_INTERFACE_PROTOCOL_VDPA,
};

static int __init mlx5_vdpa_init(void)
{
	return mlx5_register_interface(&mlx5_vdpa_interface);
}

static void __exit mlx5_vdpa_exit(void)
{
	mlx5_unregister_interface(&mlx5_vdpa_interface);
}

module_init(mlx5_vdpa_init);
module_exit(mlx5_vdpa_exit);
