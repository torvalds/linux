// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2019 Mellanox Technologies

#include <linux/mlx5/driver.h>
#include <linux/mlx5/device.h>

#include "mlx5_core.h"
#include "lib/mlx5.h"

struct mlx5_dm {
	/* protect access to icm bitmask */
	spinlock_t lock;
	unsigned long *steering_sw_icm_alloc_blocks;
	unsigned long *header_modify_sw_icm_alloc_blocks;
	unsigned long *header_modify_pattern_sw_icm_alloc_blocks;
};

struct mlx5_dm *mlx5_dm_create(struct mlx5_core_dev *dev)
{
	u64 header_modify_pattern_icm_blocks = 0;
	u64 header_modify_icm_blocks = 0;
	u64 steering_icm_blocks = 0;
	struct mlx5_dm *dm;
	bool support_v2;

	if (!(MLX5_CAP_GEN_64(dev, general_obj_types) & MLX5_GENERAL_OBJ_TYPES_CAP_SW_ICM))
		return NULL;

	dm = kzalloc(sizeof(*dm), GFP_KERNEL);
	if (!dm)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&dm->lock);

	if (MLX5_CAP64_DEV_MEM(dev, steering_sw_icm_start_address)) {
		steering_icm_blocks =
			BIT(MLX5_CAP_DEV_MEM(dev, log_steering_sw_icm_size) -
			    MLX5_LOG_SW_ICM_BLOCK_SIZE(dev));

		dm->steering_sw_icm_alloc_blocks =
			kcalloc(BITS_TO_LONGS(steering_icm_blocks),
				sizeof(unsigned long), GFP_KERNEL);
		if (!dm->steering_sw_icm_alloc_blocks)
			goto err_steering;
	}

	if (MLX5_CAP64_DEV_MEM(dev, header_modify_sw_icm_start_address)) {
		header_modify_icm_blocks =
			BIT(MLX5_CAP_DEV_MEM(dev, log_header_modify_sw_icm_size) -
			    MLX5_LOG_SW_ICM_BLOCK_SIZE(dev));

		dm->header_modify_sw_icm_alloc_blocks =
			kcalloc(BITS_TO_LONGS(header_modify_icm_blocks),
				sizeof(unsigned long), GFP_KERNEL);
		if (!dm->header_modify_sw_icm_alloc_blocks)
			goto err_modify_hdr;
	}

	support_v2 = MLX5_CAP_FLOWTABLE_NIC_RX(dev, sw_owner_v2) &&
		     MLX5_CAP_FLOWTABLE_NIC_TX(dev, sw_owner_v2) &&
		     MLX5_CAP64_DEV_MEM(dev, header_modify_pattern_sw_icm_start_address);

	if (support_v2) {
		header_modify_pattern_icm_blocks =
			BIT(MLX5_CAP_DEV_MEM(dev, log_header_modify_pattern_sw_icm_size) -
			    MLX5_LOG_SW_ICM_BLOCK_SIZE(dev));

		dm->header_modify_pattern_sw_icm_alloc_blocks =
			kcalloc(BITS_TO_LONGS(header_modify_pattern_icm_blocks),
				sizeof(unsigned long), GFP_KERNEL);
		if (!dm->header_modify_pattern_sw_icm_alloc_blocks)
			goto err_pattern;
	}

	return dm;

err_pattern:
	kfree(dm->header_modify_sw_icm_alloc_blocks);

err_modify_hdr:
	kfree(dm->steering_sw_icm_alloc_blocks);

err_steering:
	kfree(dm);

	return ERR_PTR(-ENOMEM);
}

void mlx5_dm_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_dm *dm = dev->dm;

	if (!dev->dm)
		return;

	if (dm->steering_sw_icm_alloc_blocks) {
		WARN_ON(!bitmap_empty(dm->steering_sw_icm_alloc_blocks,
				      BIT(MLX5_CAP_DEV_MEM(dev, log_steering_sw_icm_size) -
					  MLX5_LOG_SW_ICM_BLOCK_SIZE(dev))));
		kfree(dm->steering_sw_icm_alloc_blocks);
	}

	if (dm->header_modify_sw_icm_alloc_blocks) {
		WARN_ON(!bitmap_empty(dm->header_modify_sw_icm_alloc_blocks,
				      BIT(MLX5_CAP_DEV_MEM(dev,
							   log_header_modify_sw_icm_size) -
				      MLX5_LOG_SW_ICM_BLOCK_SIZE(dev))));
		kfree(dm->header_modify_sw_icm_alloc_blocks);
	}

	if (dm->header_modify_pattern_sw_icm_alloc_blocks) {
		WARN_ON(!bitmap_empty(dm->header_modify_pattern_sw_icm_alloc_blocks,
				      BIT(MLX5_CAP_DEV_MEM(dev,
							   log_header_modify_pattern_sw_icm_size) -
					  MLX5_LOG_SW_ICM_BLOCK_SIZE(dev))));
		kfree(dm->header_modify_pattern_sw_icm_alloc_blocks);
	}

	kfree(dm);
}

int mlx5_dm_sw_icm_alloc(struct mlx5_core_dev *dev, enum mlx5_sw_icm_type type,
			 u64 length, u32 log_alignment, u16 uid,
			 phys_addr_t *addr, u32 *obj_id)
{
	u32 num_blocks = DIV_ROUND_UP_ULL(length, MLX5_SW_ICM_BLOCK_SIZE(dev));
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {};
	u32 in[MLX5_ST_SZ_DW(create_sw_icm_in)] = {};
	struct mlx5_dm *dm = dev->dm;
	unsigned long *block_map;
	u64 icm_start_addr;
	u32 log_icm_size;
	u64 align_mask;
	u32 max_blocks;
	u64 block_idx;
	void *sw_icm;
	int ret;

	if (!dev->dm)
		return -EOPNOTSUPP;

	if (!length || (length & (length - 1)) ||
	    length & (MLX5_SW_ICM_BLOCK_SIZE(dev) - 1))
		return -EINVAL;

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type, MLX5_OBJ_TYPE_SW_ICM);
	MLX5_SET(general_obj_in_cmd_hdr, in, uid, uid);

	switch (type) {
	case MLX5_SW_ICM_TYPE_STEERING:
		icm_start_addr = MLX5_CAP64_DEV_MEM(dev, steering_sw_icm_start_address);
		log_icm_size = MLX5_CAP_DEV_MEM(dev, log_steering_sw_icm_size);
		block_map = dm->steering_sw_icm_alloc_blocks;
		break;
	case MLX5_SW_ICM_TYPE_HEADER_MODIFY:
		icm_start_addr = MLX5_CAP64_DEV_MEM(dev, header_modify_sw_icm_start_address);
		log_icm_size = MLX5_CAP_DEV_MEM(dev,
						log_header_modify_sw_icm_size);
		block_map = dm->header_modify_sw_icm_alloc_blocks;
		break;
	case MLX5_SW_ICM_TYPE_HEADER_MODIFY_PATTERN:
		icm_start_addr = MLX5_CAP64_DEV_MEM(dev,
						    header_modify_pattern_sw_icm_start_address);
		log_icm_size = MLX5_CAP_DEV_MEM(dev,
						log_header_modify_pattern_sw_icm_size);
		block_map = dm->header_modify_pattern_sw_icm_alloc_blocks;
		break;
	default:
		return -EINVAL;
	}

	if (!block_map)
		return -EOPNOTSUPP;

	max_blocks = BIT(log_icm_size - MLX5_LOG_SW_ICM_BLOCK_SIZE(dev));

	if (log_alignment < MLX5_LOG_SW_ICM_BLOCK_SIZE(dev))
		log_alignment = MLX5_LOG_SW_ICM_BLOCK_SIZE(dev);
	align_mask = BIT(log_alignment - MLX5_LOG_SW_ICM_BLOCK_SIZE(dev)) - 1;

	spin_lock(&dm->lock);
	block_idx = bitmap_find_next_zero_area(block_map, max_blocks, 0,
					       num_blocks, align_mask);

	if (block_idx < max_blocks)
		bitmap_set(block_map,
			   block_idx, num_blocks);

	spin_unlock(&dm->lock);

	if (block_idx >= max_blocks)
		return -ENOMEM;

	sw_icm = MLX5_ADDR_OF(create_sw_icm_in, in, sw_icm);
	icm_start_addr += block_idx << MLX5_LOG_SW_ICM_BLOCK_SIZE(dev);
	MLX5_SET64(sw_icm, sw_icm, sw_icm_start_addr,
		   icm_start_addr);
	MLX5_SET(sw_icm, sw_icm, log_sw_icm_size, ilog2(length));

	ret = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (ret) {
		spin_lock(&dm->lock);
		bitmap_clear(block_map,
			     block_idx, num_blocks);
		spin_unlock(&dm->lock);

		return ret;
	}

	*addr = icm_start_addr;
	*obj_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_dm_sw_icm_alloc);

int mlx5_dm_sw_icm_dealloc(struct mlx5_core_dev *dev, enum mlx5_sw_icm_type type,
			   u64 length, u16 uid, phys_addr_t addr, u32 obj_id)
{
	u32 num_blocks = DIV_ROUND_UP_ULL(length, MLX5_SW_ICM_BLOCK_SIZE(dev));
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {};
	u32 in[MLX5_ST_SZ_DW(general_obj_in_cmd_hdr)] = {};
	struct mlx5_dm *dm = dev->dm;
	unsigned long *block_map;
	u64 icm_start_addr;
	u64 start_idx;
	int err;

	if (!dev->dm)
		return -EOPNOTSUPP;

	switch (type) {
	case MLX5_SW_ICM_TYPE_STEERING:
		icm_start_addr = MLX5_CAP64_DEV_MEM(dev, steering_sw_icm_start_address);
		block_map = dm->steering_sw_icm_alloc_blocks;
		break;
	case MLX5_SW_ICM_TYPE_HEADER_MODIFY:
		icm_start_addr = MLX5_CAP64_DEV_MEM(dev, header_modify_sw_icm_start_address);
		block_map = dm->header_modify_sw_icm_alloc_blocks;
		break;
	case MLX5_SW_ICM_TYPE_HEADER_MODIFY_PATTERN:
		icm_start_addr = MLX5_CAP64_DEV_MEM(dev,
						    header_modify_pattern_sw_icm_start_address);
		block_map = dm->header_modify_pattern_sw_icm_alloc_blocks;
		break;
	default:
		return -EINVAL;
	}

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type, MLX5_OBJ_TYPE_SW_ICM);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, obj_id);
	MLX5_SET(general_obj_in_cmd_hdr, in, uid, uid);

	err =  mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	start_idx = (addr - icm_start_addr) >> MLX5_LOG_SW_ICM_BLOCK_SIZE(dev);
	spin_lock(&dm->lock);
	bitmap_clear(block_map,
		     start_idx, num_blocks);
	spin_unlock(&dm->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_dm_sw_icm_dealloc);
