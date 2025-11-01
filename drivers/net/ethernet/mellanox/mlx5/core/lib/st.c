// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include <linux/mlx5/driver.h>
#include <linux/mlx5/device.h>

#include "mlx5_core.h"
#include "lib/mlx5.h"

struct mlx5_st_idx_data {
	refcount_t usecount;
	u16 tag;
};

struct mlx5_st {
	/* serialize access upon alloc/free flows */
	struct mutex lock;
	struct xa_limit index_limit;
	struct xarray idx_xa; /* key == index, value == struct mlx5_st_idx_data */
};

struct mlx5_st *mlx5_st_create(struct mlx5_core_dev *dev)
{
	struct pci_dev *pdev = dev->pdev;
	struct mlx5_st *st;
	u16 num_entries;
	int ret;

	if (!MLX5_CAP_GEN(dev, mkey_pcie_tph))
		return NULL;

#ifdef CONFIG_MLX5_SF
	if (mlx5_core_is_sf(dev))
		return dev->priv.parent_mdev->st;
#endif

	/* Checking whether the device is capable */
	if (!pdev->tph_cap)
		return NULL;

	num_entries = pcie_tph_get_st_table_size(pdev);
	/* We need a reserved entry for non TPH cases */
	if (num_entries < 2)
		return NULL;

	/* The OS doesn't support ST */
	ret = pcie_enable_tph(pdev, PCI_TPH_ST_DS_MODE);
	if (ret)
		return NULL;

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto end;

	mutex_init(&st->lock);
	xa_init_flags(&st->idx_xa, XA_FLAGS_ALLOC);
	/* entry 0 is reserved for non TPH cases */
	st->index_limit.min = MLX5_MKC_PCIE_TPH_NO_STEERING_TAG_INDEX + 1;
	st->index_limit.max = num_entries - 1;

	return st;

end:
	pcie_disable_tph(dev->pdev);
	return NULL;
}

void mlx5_st_destroy(struct mlx5_core_dev *dev)
{
	struct mlx5_st *st = dev->st;

	if (mlx5_core_is_sf(dev) || !st)
		return;

	pcie_disable_tph(dev->pdev);
	WARN_ON_ONCE(!xa_empty(&st->idx_xa));
	kfree(st);
}

int mlx5_st_alloc_index(struct mlx5_core_dev *dev, enum tph_mem_type mem_type,
			unsigned int cpu_uid, u16 *st_index)
{
	struct mlx5_st_idx_data *idx_data;
	struct mlx5_st *st = dev->st;
	unsigned long index;
	u32 xa_id;
	u16 tag;
	int ret;

	if (!st)
		return -EOPNOTSUPP;

	ret = pcie_tph_get_cpu_st(dev->pdev, mem_type, cpu_uid, &tag);
	if (ret)
		return ret;

	mutex_lock(&st->lock);

	xa_for_each(&st->idx_xa, index, idx_data) {
		if (tag == idx_data->tag) {
			refcount_inc(&idx_data->usecount);
			*st_index = index;
			goto end;
		}
	}

	idx_data = kzalloc(sizeof(*idx_data), GFP_KERNEL);
	if (!idx_data) {
		ret = -ENOMEM;
		goto end;
	}

	refcount_set(&idx_data->usecount, 1);
	idx_data->tag = tag;

	ret = xa_alloc(&st->idx_xa, &xa_id, idx_data, st->index_limit, GFP_KERNEL);
	if (ret)
		goto clean_idx_data;

	ret = pcie_tph_set_st_entry(dev->pdev, xa_id, tag);
	if (ret)
		goto clean_idx_xa;

	*st_index = xa_id;
	goto end;

clean_idx_xa:
	xa_erase(&st->idx_xa, xa_id);
clean_idx_data:
	kfree(idx_data);
end:
	mutex_unlock(&st->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(mlx5_st_alloc_index);

int mlx5_st_dealloc_index(struct mlx5_core_dev *dev, u16 st_index)
{
	struct mlx5_st_idx_data *idx_data;
	struct mlx5_st *st = dev->st;
	int ret = 0;

	if (!st)
		return -EOPNOTSUPP;

	mutex_lock(&st->lock);
	idx_data = xa_load(&st->idx_xa, st_index);
	if (WARN_ON_ONCE(!idx_data)) {
		ret = -EINVAL;
		goto end;
	}

	if (refcount_dec_and_test(&idx_data->usecount)) {
		xa_erase(&st->idx_xa, st_index);
		/* We leave PCI config space as was before, no mkey will refer to it */
	}

end:
	mutex_unlock(&st->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(mlx5_st_dealloc_index);
