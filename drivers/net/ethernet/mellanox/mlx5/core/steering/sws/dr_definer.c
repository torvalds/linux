// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "dr_types.h"
#include "dr_ste.h"

struct dr_definer_object {
	u32 id;
	u16 format_id;
	u8 dw_selectors[MLX5_IFC_DEFINER_DW_SELECTORS_NUM];
	u8 byte_selectors[MLX5_IFC_DEFINER_BYTE_SELECTORS_NUM];
	u8 match_mask[DR_STE_SIZE_MATCH_TAG];
	refcount_t refcount;
};

static bool dr_definer_compare(struct dr_definer_object *definer,
			       u16 format_id, u8 *dw_selectors,
			       u8 *byte_selectors, u8 *match_mask)
{
	int i;

	if (definer->format_id != format_id)
		return false;

	for (i = 0; i < MLX5_IFC_DEFINER_DW_SELECTORS_NUM; i++)
		if (definer->dw_selectors[i] != dw_selectors[i])
			return false;

	for (i = 0; i < MLX5_IFC_DEFINER_BYTE_SELECTORS_NUM; i++)
		if (definer->byte_selectors[i] != byte_selectors[i])
			return false;

	if (memcmp(definer->match_mask, match_mask, DR_STE_SIZE_MATCH_TAG))
		return false;

	return true;
}

static struct dr_definer_object *
dr_definer_find_obj(struct mlx5dr_domain *dmn, u16 format_id,
		    u8 *dw_selectors, u8 *byte_selectors, u8 *match_mask)
{
	struct dr_definer_object *definer_obj;
	unsigned long id;

	xa_for_each(&dmn->definers_xa, id, definer_obj) {
		if (dr_definer_compare(definer_obj, format_id,
				       dw_selectors, byte_selectors,
				       match_mask))
			return definer_obj;
	}

	return NULL;
}

static struct dr_definer_object *
dr_definer_create_obj(struct mlx5dr_domain *dmn, u16 format_id,
		      u8 *dw_selectors, u8 *byte_selectors, u8 *match_mask)
{
	struct dr_definer_object *definer_obj;
	int ret = 0;

	definer_obj = kzalloc(sizeof(*definer_obj), GFP_KERNEL);
	if (!definer_obj)
		return NULL;

	ret = mlx5dr_cmd_create_definer(dmn->mdev,
					format_id,
					dw_selectors,
					byte_selectors,
					match_mask,
					&definer_obj->id);
	if (ret)
		goto err_free_definer_obj;

	/* Definer ID can have 32 bits, but STE format
	 * supports only definers with 8 bit IDs.
	 */
	if (definer_obj->id > 0xff) {
		mlx5dr_err(dmn, "Unsupported definer ID (%d)\n", definer_obj->id);
		goto err_destroy_definer;
	}

	definer_obj->format_id = format_id;
	memcpy(definer_obj->dw_selectors, dw_selectors, sizeof(definer_obj->dw_selectors));
	memcpy(definer_obj->byte_selectors, byte_selectors, sizeof(definer_obj->byte_selectors));
	memcpy(definer_obj->match_mask, match_mask, sizeof(definer_obj->match_mask));

	refcount_set(&definer_obj->refcount, 1);

	ret = xa_insert(&dmn->definers_xa, definer_obj->id, definer_obj, GFP_KERNEL);
	if (ret) {
		mlx5dr_dbg(dmn, "Couldn't insert new definer into xarray (%d)\n", ret);
		goto err_destroy_definer;
	}

	return definer_obj;

err_destroy_definer:
	mlx5dr_cmd_destroy_definer(dmn->mdev, definer_obj->id);
err_free_definer_obj:
	kfree(definer_obj);

	return NULL;
}

static void dr_definer_destroy_obj(struct mlx5dr_domain *dmn,
				   struct dr_definer_object *definer_obj)
{
	mlx5dr_cmd_destroy_definer(dmn->mdev, definer_obj->id);
	xa_erase(&dmn->definers_xa, definer_obj->id);
	kfree(definer_obj);
}

int mlx5dr_definer_get(struct mlx5dr_domain *dmn, u16 format_id,
		       u8 *dw_selectors, u8 *byte_selectors,
		       u8 *match_mask, u32 *definer_id)
{
	struct dr_definer_object *definer_obj;
	int ret = 0;

	definer_obj = dr_definer_find_obj(dmn, format_id, dw_selectors,
					  byte_selectors, match_mask);
	if (!definer_obj) {
		definer_obj = dr_definer_create_obj(dmn, format_id,
						    dw_selectors, byte_selectors,
						    match_mask);
		if (!definer_obj)
			return -ENOMEM;
	} else {
		refcount_inc(&definer_obj->refcount);
	}

	*definer_id = definer_obj->id;

	return ret;
}

void mlx5dr_definer_put(struct mlx5dr_domain *dmn, u32 definer_id)
{
	struct dr_definer_object *definer_obj;

	definer_obj = xa_load(&dmn->definers_xa, definer_id);
	if (!definer_obj) {
		mlx5dr_err(dmn, "Definer ID %d not found\n", definer_id);
		return;
	}

	if (refcount_dec_and_test(&definer_obj->refcount))
		dr_definer_destroy_obj(dmn, definer_obj);
}
