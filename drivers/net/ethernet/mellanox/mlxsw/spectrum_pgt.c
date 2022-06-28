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
	bool smpe_index_valid;
};

struct mlxsw_sp_pgt_entry {
	struct list_head ports_list;
	u16 index;
	u16 smpe_index;
};

struct mlxsw_sp_pgt_entry_port {
	struct list_head list; /* Member of 'ports_list'. */
	u16 local_port;
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

static struct mlxsw_sp_pgt_entry_port *
mlxsw_sp_pgt_entry_port_lookup(struct mlxsw_sp_pgt_entry *pgt_entry,
			       u16 local_port)
{
	struct mlxsw_sp_pgt_entry_port *pgt_entry_port;

	list_for_each_entry(pgt_entry_port, &pgt_entry->ports_list, list) {
		if (pgt_entry_port->local_port == local_port)
			return pgt_entry_port;
	}

	return NULL;
}

static struct mlxsw_sp_pgt_entry *
mlxsw_sp_pgt_entry_create(struct mlxsw_sp_pgt *pgt, u16 mid, u16 smpe)
{
	struct mlxsw_sp_pgt_entry *pgt_entry;
	void *ret;
	int err;

	pgt_entry = kzalloc(sizeof(*pgt_entry), GFP_KERNEL);
	if (!pgt_entry)
		return ERR_PTR(-ENOMEM);

	ret = idr_replace(&pgt->pgt_idr, pgt_entry, mid);
	if (IS_ERR(ret)) {
		err = PTR_ERR(ret);
		goto err_idr_replace;
	}

	INIT_LIST_HEAD(&pgt_entry->ports_list);
	pgt_entry->index = mid;
	pgt_entry->smpe_index = smpe;
	return pgt_entry;

err_idr_replace:
	kfree(pgt_entry);
	return ERR_PTR(err);
}

static void mlxsw_sp_pgt_entry_destroy(struct mlxsw_sp_pgt *pgt,
				       struct mlxsw_sp_pgt_entry *pgt_entry)
{
	WARN_ON(!list_empty(&pgt_entry->ports_list));

	pgt_entry = idr_replace(&pgt->pgt_idr, NULL, pgt_entry->index);
	if (WARN_ON(IS_ERR(pgt_entry)))
		return;

	kfree(pgt_entry);
}

static struct mlxsw_sp_pgt_entry *
mlxsw_sp_pgt_entry_get(struct mlxsw_sp_pgt *pgt, u16 mid, u16 smpe)
{
	struct mlxsw_sp_pgt_entry *pgt_entry;

	pgt_entry = idr_find(&pgt->pgt_idr, mid);
	if (pgt_entry)
		return pgt_entry;

	return mlxsw_sp_pgt_entry_create(pgt, mid, smpe);
}

static void mlxsw_sp_pgt_entry_put(struct mlxsw_sp_pgt *pgt, u16 mid)
{
	struct mlxsw_sp_pgt_entry *pgt_entry;

	pgt_entry = idr_find(&pgt->pgt_idr, mid);
	if (WARN_ON(!pgt_entry))
		return;

	if (list_empty(&pgt_entry->ports_list))
		mlxsw_sp_pgt_entry_destroy(pgt, pgt_entry);
}

static void mlxsw_sp_pgt_smid2_port_set(char *smid2_pl, u16 local_port,
					bool member)
{
	mlxsw_reg_smid2_port_set(smid2_pl, local_port, member);
	mlxsw_reg_smid2_port_mask_set(smid2_pl, local_port, 1);
}

static int
mlxsw_sp_pgt_entry_port_write(struct mlxsw_sp *mlxsw_sp,
			      const struct mlxsw_sp_pgt_entry *pgt_entry,
			      u16 local_port, bool member)
{
	bool smpe_index_valid;
	char *smid2_pl;
	u16 smpe;
	int err;

	smid2_pl = kmalloc(MLXSW_REG_SMID2_LEN, GFP_KERNEL);
	if (!smid2_pl)
		return -ENOMEM;

	smpe_index_valid = mlxsw_sp->ubridge ? mlxsw_sp->pgt->smpe_index_valid :
			   false;
	smpe = mlxsw_sp->ubridge ? pgt_entry->smpe_index : 0;

	mlxsw_reg_smid2_pack(smid2_pl, pgt_entry->index, 0, 0, smpe_index_valid,
			     smpe);

	mlxsw_sp_pgt_smid2_port_set(smid2_pl, local_port, member);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(smid2), smid2_pl);

	kfree(smid2_pl);

	return err;
}

static struct mlxsw_sp_pgt_entry_port *
mlxsw_sp_pgt_entry_port_create(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_pgt_entry *pgt_entry,
			       u16 local_port)
{
	struct mlxsw_sp_pgt_entry_port *pgt_entry_port;
	int err;

	pgt_entry_port = kzalloc(sizeof(*pgt_entry_port), GFP_KERNEL);
	if (!pgt_entry_port)
		return ERR_PTR(-ENOMEM);

	err = mlxsw_sp_pgt_entry_port_write(mlxsw_sp, pgt_entry, local_port,
					    true);
	if (err)
		goto err_pgt_entry_port_write;

	pgt_entry_port->local_port = local_port;
	list_add(&pgt_entry_port->list, &pgt_entry->ports_list);

	return pgt_entry_port;

err_pgt_entry_port_write:
	kfree(pgt_entry_port);
	return ERR_PTR(err);
}

static void
mlxsw_sp_pgt_entry_port_destroy(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_pgt_entry *pgt_entry,
				struct mlxsw_sp_pgt_entry_port *pgt_entry_port)

{
	list_del(&pgt_entry_port->list);
	mlxsw_sp_pgt_entry_port_write(mlxsw_sp, pgt_entry,
				      pgt_entry_port->local_port, false);
	kfree(pgt_entry_port);
}

static int mlxsw_sp_pgt_entry_port_add(struct mlxsw_sp *mlxsw_sp, u16 mid,
				       u16 smpe, u16 local_port)
{
	struct mlxsw_sp_pgt_entry_port *pgt_entry_port;
	struct mlxsw_sp_pgt_entry *pgt_entry;
	int err;

	mutex_lock(&mlxsw_sp->pgt->lock);

	pgt_entry = mlxsw_sp_pgt_entry_get(mlxsw_sp->pgt, mid, smpe);
	if (IS_ERR(pgt_entry)) {
		err = PTR_ERR(pgt_entry);
		goto err_pgt_entry_get;
	}

	pgt_entry_port = mlxsw_sp_pgt_entry_port_create(mlxsw_sp, pgt_entry,
							local_port);
	if (IS_ERR(pgt_entry_port)) {
		err = PTR_ERR(pgt_entry_port);
		goto err_pgt_entry_port_get;
	}

	mutex_unlock(&mlxsw_sp->pgt->lock);
	return 0;

err_pgt_entry_port_get:
	mlxsw_sp_pgt_entry_put(mlxsw_sp->pgt, mid);
err_pgt_entry_get:
	mutex_unlock(&mlxsw_sp->pgt->lock);
	return err;
}

static void mlxsw_sp_pgt_entry_port_del(struct mlxsw_sp *mlxsw_sp,
					u16 mid, u16 smpe, u16 local_port)
{
	struct mlxsw_sp_pgt_entry_port *pgt_entry_port;
	struct mlxsw_sp_pgt_entry *pgt_entry;

	mutex_lock(&mlxsw_sp->pgt->lock);

	pgt_entry = idr_find(&mlxsw_sp->pgt->pgt_idr, mid);
	if (!pgt_entry)
		goto out;

	pgt_entry_port = mlxsw_sp_pgt_entry_port_lookup(pgt_entry, local_port);
	if (!pgt_entry_port)
		goto out;

	mlxsw_sp_pgt_entry_port_destroy(mlxsw_sp, pgt_entry, pgt_entry_port);
	mlxsw_sp_pgt_entry_put(mlxsw_sp->pgt, mid);

out:
	mutex_unlock(&mlxsw_sp->pgt->lock);
}

int mlxsw_sp_pgt_entry_port_set(struct mlxsw_sp *mlxsw_sp, u16 mid,
				u16 smpe, u16 local_port, bool member)
{
	if (member)
		return mlxsw_sp_pgt_entry_port_add(mlxsw_sp, mid, smpe,
						   local_port);

	mlxsw_sp_pgt_entry_port_del(mlxsw_sp, mid, smpe, local_port);
	return 0;
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
	pgt->smpe_index_valid = mlxsw_sp->pgt_smpe_index_valid;
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
