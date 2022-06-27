// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/refcount.h>
#include <linux/idr.h>

#include "spectrum.h"
#include "reg.h"

struct mlxsw_sp_pgt {
	struct idr pgt_idr;
	u16 end_index; /* Exclusive. */
	struct mutex lock; /* Protects PGT. */
};

int mlxsw_sp_pgt_mid_alloc(struct mlxsw_sp *mlxsw_sp, u16 *p_mid)
{
	int index, err = 0;

	mutex_lock(&mlxsw_sp->pgt->lock);
	index = idr_alloc(&mlxsw_sp->pgt->pgt_idr, NULL, 0,
			  mlxsw_sp->pgt->end_index, GFP_KERNEL);

	if (index < 0) {
		err = index;
		goto err_idr_alloc;
	}

	*p_mid = index;
	mutex_unlock(&mlxsw_sp->pgt->lock);
	return 0;

err_idr_alloc:
	mutex_unlock(&mlxsw_sp->pgt->lock);
	return err;
}

void mlxsw_sp_pgt_mid_free(struct mlxsw_sp *mlxsw_sp, u16 mid_base)
{
	mutex_lock(&mlxsw_sp->pgt->lock);
	WARN_ON(idr_remove(&mlxsw_sp->pgt->pgt_idr, mid_base));
	mutex_unlock(&mlxsw_sp->pgt->lock);
}

int
mlxsw_sp_pgt_mid_alloc_range(struct mlxsw_sp *mlxsw_sp, u16 mid_base, u16 count)
{
	unsigned int idr_cursor;
	int i, err;

	mutex_lock(&mlxsw_sp->pgt->lock);

	/* This function is supposed to be called several times as part of
	 * driver init, in specific order. Verify that the mid_index is the
	 * first free index in the idr, to be able to free the indexes in case
	 * of error.
	 */
	idr_cursor = idr_get_cursor(&mlxsw_sp->pgt->pgt_idr);
	if (WARN_ON(idr_cursor != mid_base)) {
		err = -EINVAL;
		goto err_idr_cursor;
	}

	for (i = 0; i < count; i++) {
		err = idr_alloc_cyclic(&mlxsw_sp->pgt->pgt_idr, NULL,
				       mid_base, mid_base + count, GFP_KERNEL);
		if (err < 0)
			goto err_idr_alloc_cyclic;
	}

	mutex_unlock(&mlxsw_sp->pgt->lock);
	return 0;

err_idr_alloc_cyclic:
	for (i--; i >= 0; i--)
		idr_remove(&mlxsw_sp->pgt->pgt_idr, mid_base + i);
err_idr_cursor:
	mutex_unlock(&mlxsw_sp->pgt->lock);
	return err;
}

void
mlxsw_sp_pgt_mid_free_range(struct mlxsw_sp *mlxsw_sp, u16 mid_base, u16 count)
{
	struct idr *pgt_idr = &mlxsw_sp->pgt->pgt_idr;
	int i;

	mutex_lock(&mlxsw_sp->pgt->lock);

	for (i = 0; i < count; i++)
		WARN_ON_ONCE(idr_remove(pgt_idr, mid_base + i));

	mutex_unlock(&mlxsw_sp->pgt->lock);
}

int mlxsw_sp_pgt_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_pgt *pgt;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, PGT_SIZE))
		return -EIO;

	pgt = kzalloc(sizeof(*mlxsw_sp->pgt), GFP_KERNEL);
	if (!pgt)
		return -ENOMEM;

	idr_init(&pgt->pgt_idr);
	pgt->end_index = MLXSW_CORE_RES_GET(mlxsw_sp->core, PGT_SIZE);
	mutex_init(&pgt->lock);
	mlxsw_sp->pgt = pgt;
	return 0;
}

void mlxsw_sp_pgt_fini(struct mlxsw_sp *mlxsw_sp)
{
	mutex_destroy(&mlxsw_sp->pgt->lock);
	WARN_ON(!idr_is_empty(&mlxsw_sp->pgt->pgt_idr));
	idr_destroy(&mlxsw_sp->pgt->pgt_idr);
	kfree(mlxsw_sp->pgt);
}
