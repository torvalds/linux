/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. */

#ifndef _MLX5_IB_UMR_H
#define _MLX5_IB_UMR_H

#include "mlx5_ib.h"


#define MLX5_MAX_UMR_SHIFT 16
#define MLX5_MAX_UMR_PAGES (1 << MLX5_MAX_UMR_SHIFT)

int mlx5r_umr_resource_init(struct mlx5_ib_dev *dev);
void mlx5r_umr_resource_cleanup(struct mlx5_ib_dev *dev);

static inline bool mlx5r_umr_can_load_pas(struct mlx5_ib_dev *dev,
					  size_t length)
{
	/*
	 * umr_check_mkey_mask() rejects MLX5_MKEY_MASK_PAGE_SIZE which is
	 * always set if MLX5_IB_SEND_UMR_UPDATE_TRANSLATION (aka
	 * MLX5_IB_UPD_XLT_ADDR and MLX5_IB_UPD_XLT_ENABLE) is set. Thus, a mkey
	 * can never be enabled without this capability. Simplify this weird
	 * quirky hardware by just saying it can't use PAS lists with UMR at
	 * all.
	 */
	if (MLX5_CAP_GEN(dev->mdev, umr_modify_entity_size_disabled))
		return false;

	/*
	 * length is the size of the MR in bytes when mlx5_ib_update_xlt() is
	 * used.
	 */
	if (!MLX5_CAP_GEN(dev->mdev, umr_extended_translation_offset) &&
	    length >= MLX5_MAX_UMR_PAGES * PAGE_SIZE)
		return false;
	return true;
}

/*
 * true if an existing MR can be reconfigured to new access_flags using UMR.
 * Older HW cannot use UMR to update certain elements of the MKC. See
 * get_umr_update_access_mask() and umr_check_mkey_mask()
 */
static inline bool mlx5r_umr_can_reconfig(struct mlx5_ib_dev *dev,
					  unsigned int current_access_flags,
					  unsigned int target_access_flags)
{
	unsigned int diffs = current_access_flags ^ target_access_flags;

	if ((diffs & IB_ACCESS_REMOTE_ATOMIC) &&
	    MLX5_CAP_GEN(dev->mdev, atomic) &&
	    MLX5_CAP_GEN(dev->mdev, umr_modify_atomic_disabled))
		return false;

	if ((diffs & IB_ACCESS_RELAXED_ORDERING) &&
	    MLX5_CAP_GEN(dev->mdev, relaxed_ordering_write) &&
	    !MLX5_CAP_GEN(dev->mdev, relaxed_ordering_write_umr))
		return false;

	if ((diffs & IB_ACCESS_RELAXED_ORDERING) &&
	    MLX5_CAP_GEN(dev->mdev, relaxed_ordering_read) &&
	    !MLX5_CAP_GEN(dev->mdev, relaxed_ordering_read_umr))
		return false;

	return true;
}

#endif /* _MLX5_IB_UMR_H */
