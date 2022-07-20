// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2019 Mellanox Technologies.

#include "mlx5_core.h"
#include "lib/crypto.h"

#define MLX5_CRYPTO_DEK_POOLS_NUM (MLX5_ACCEL_OBJ_TYPE_KEY_NUM - 1)
#define type2idx(type) ((type) - 1)

enum {
	MLX5_CRYPTO_DEK_ALL_TYPE = BIT(0),
};

struct mlx5_crypto_dek_pool {
	struct mlx5_core_dev *mdev;
	u32 key_purpose;
	int num_deks; /* the total number of keys in this pool */
	int avail_deks; /* the number of available keys in this pool */
	int in_use_deks; /* the number of being used keys in this pool */
	struct mutex lock; /* protect the following lists, and the bulks */
	struct list_head partial_list; /* some of keys are available */
	struct list_head full_list; /* no available keys */
};

struct mlx5_crypto_dek_bulk {
	struct mlx5_core_dev *mdev;
	int base_obj_id;
	int avail_start; /* the bit to start search */
	int num_deks; /* the total number of keys in a bulk */
	int avail_deks; /* the number of keys available, with need_sync bit 0 */
	int in_use_deks; /* the number of keys being used, with in_use bit 1 */
	struct list_head entry;

	/* 0: not being used by any user, 1: otherwise */
	unsigned long *in_use;

	/* The bits are set when they are used, and initialized to 0 */
	unsigned long *need_sync;
};

struct mlx5_crypto_dek_priv {
	struct mlx5_core_dev *mdev;
	int log_dek_obj_range;
};

struct mlx5_crypto_dek {
	struct mlx5_crypto_dek_bulk *bulk;
	u32 obj_id;
};

u32 mlx5_crypto_dek_get_id(struct mlx5_crypto_dek *dek)
{
	return dek->obj_id;
}

static int mlx5_crypto_dek_get_key_sz(struct mlx5_core_dev *mdev,
				      u32 sz_bytes, u8 *key_sz_p)
{
	u32 sz_bits = sz_bytes * BITS_PER_BYTE;

	switch (sz_bits) {
	case 128:
		*key_sz_p = MLX5_GENERAL_OBJECT_TYPE_ENCRYPTION_KEY_KEY_SIZE_128;
		break;
	case 256:
		*key_sz_p = MLX5_GENERAL_OBJECT_TYPE_ENCRYPTION_KEY_KEY_SIZE_256;
		break;
	default:
		mlx5_core_err(mdev, "Crypto offload error, invalid key size (%u bits)\n",
			      sz_bits);
		return -EINVAL;
	}

	return 0;
}

static int mlx5_crypto_dek_fill_key(struct mlx5_core_dev *mdev, u8 *key_obj,
				    const void *key, u32 sz_bytes)
{
	void *dst;
	u8 key_sz;
	int err;

	err = mlx5_crypto_dek_get_key_sz(mdev, sz_bytes, &key_sz);
	if (err)
		return err;

	MLX5_SET(encryption_key_obj, key_obj, key_size, key_sz);

	if (sz_bytes == 16)
		/* For key size of 128b the MSBs are reserved. */
		dst = MLX5_ADDR_OF(encryption_key_obj, key_obj, key[1]);
	else
		dst = MLX5_ADDR_OF(encryption_key_obj, key_obj, key);

	memcpy(dst, key, sz_bytes);

	return 0;
}

static int mlx5_crypto_cmd_sync_crypto(struct mlx5_core_dev *mdev,
				       int crypto_type)
{
	u32 in[MLX5_ST_SZ_DW(sync_crypto_in)] = {};
	int err;

	mlx5_core_dbg(mdev,
		      "Execute SYNC_CRYPTO command with crypto_type(0x%x)\n",
		      crypto_type);

	MLX5_SET(sync_crypto_in, in, opcode, MLX5_CMD_OP_SYNC_CRYPTO);
	MLX5_SET(sync_crypto_in, in, crypto_type, crypto_type);

	err = mlx5_cmd_exec_in(mdev, sync_crypto, in);
	if (err)
		mlx5_core_err(mdev,
			      "Failed to exec sync crypto, type=%d, err=%d\n",
			      crypto_type, err);

	return err;
}

static int mlx5_crypto_create_dek_bulk(struct mlx5_core_dev *mdev,
				       u32 key_purpose, int log_obj_range,
				       u32 *obj_id)
{
	u32 in[MLX5_ST_SZ_DW(create_encryption_key_in)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	void *obj, *param;
	int err;

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_ENCRYPTION_KEY);
	param = MLX5_ADDR_OF(general_obj_in_cmd_hdr, in, op_param);
	MLX5_SET(general_obj_create_param, param, log_obj_range, log_obj_range);

	obj = MLX5_ADDR_OF(create_encryption_key_in, in, encryption_key_object);
	MLX5_SET(encryption_key_obj, obj, key_purpose, key_purpose);
	MLX5_SET(encryption_key_obj, obj, pd, mdev->mlx5e_res.hw_objs.pdn);

	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	*obj_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);
	mlx5_core_dbg(mdev, "DEK objects created, bulk=%d, obj_id=%d\n",
		      1 << log_obj_range, *obj_id);

	return 0;
}

static int mlx5_crypto_modify_dek_key(struct mlx5_core_dev *mdev,
				      const void *key, u32 sz_bytes, u32 key_purpose,
				      u32 obj_id, u32 obj_offset)
{
	u32 in[MLX5_ST_SZ_DW(modify_encryption_key_in)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	void *obj, *param;
	int err;

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_MODIFY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_ENCRYPTION_KEY);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, obj_id);

	param = MLX5_ADDR_OF(general_obj_in_cmd_hdr, in, op_param);
	MLX5_SET(general_obj_query_param, param, obj_offset, obj_offset);

	obj = MLX5_ADDR_OF(modify_encryption_key_in, in, encryption_key_object);
	MLX5_SET64(encryption_key_obj, obj, modify_field_select, 1);
	MLX5_SET(encryption_key_obj, obj, key_purpose, key_purpose);
	MLX5_SET(encryption_key_obj, obj, pd, mdev->mlx5e_res.hw_objs.pdn);

	err = mlx5_crypto_dek_fill_key(mdev, obj, key, sz_bytes);
	if (err)
		return err;

	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));

	/* avoid leaking key on the stack */
	memzero_explicit(in, sizeof(in));

	return err;
}

static int mlx5_crypto_create_dek_key(struct mlx5_core_dev *mdev,
				      const void *key, u32 sz_bytes,
				      u32 key_purpose, u32 *p_key_id)
{
	u32 in[MLX5_ST_SZ_DW(create_encryption_key_in)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	u64 general_obj_types;
	void *obj;
	int err;

	general_obj_types = MLX5_CAP_GEN_64(mdev, general_obj_types);
	if (!(general_obj_types &
	      MLX5_HCA_CAP_GENERAL_OBJECT_TYPES_ENCRYPTION_KEY))
		return -EINVAL;

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_ENCRYPTION_KEY);

	obj = MLX5_ADDR_OF(create_encryption_key_in, in, encryption_key_object);
	MLX5_SET(encryption_key_obj, obj, key_purpose, key_purpose);
	MLX5_SET(encryption_key_obj, obj, pd, mdev->mlx5e_res.hw_objs.pdn);

	err = mlx5_crypto_dek_fill_key(mdev, obj, key, sz_bytes);
	if (err)
		return err;

	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*p_key_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);

	/* avoid leaking key on the stack */
	memzero_explicit(in, sizeof(in));

	return err;
}

static void mlx5_crypto_destroy_dek_key(struct mlx5_core_dev *mdev, u32 key_id)
{
	u32 in[MLX5_ST_SZ_DW(general_obj_in_cmd_hdr)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_ENCRYPTION_KEY);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, key_id);

	mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

int mlx5_create_encryption_key(struct mlx5_core_dev *mdev,
			       const void *key, u32 sz_bytes,
			       u32 key_type, u32 *p_key_id)
{
	return mlx5_crypto_create_dek_key(mdev, key, sz_bytes, key_type, p_key_id);
}

void mlx5_destroy_encryption_key(struct mlx5_core_dev *mdev, u32 key_id)
{
	mlx5_crypto_destroy_dek_key(mdev, key_id);
}

static struct mlx5_crypto_dek_bulk *
mlx5_crypto_dek_bulk_create(struct mlx5_crypto_dek_pool *pool)
{
	struct mlx5_crypto_dek_priv *dek_priv = pool->mdev->mlx5e_res.dek_priv;
	struct mlx5_core_dev *mdev = pool->mdev;
	struct mlx5_crypto_dek_bulk *bulk;
	int num_deks, base_obj_id;
	int err;

	bulk = kzalloc(sizeof(*bulk), GFP_KERNEL);
	if (!bulk)
		return ERR_PTR(-ENOMEM);

	num_deks = 1 << dek_priv->log_dek_obj_range;
	bulk->need_sync = bitmap_zalloc(num_deks, GFP_KERNEL);
	if (!bulk->need_sync) {
		err = -ENOMEM;
		goto err_out;
	}

	bulk->in_use = bitmap_zalloc(num_deks, GFP_KERNEL);
	if (!bulk->in_use) {
		err = -ENOMEM;
		goto err_out;
	}

	err = mlx5_crypto_create_dek_bulk(mdev, pool->key_purpose,
					  dek_priv->log_dek_obj_range,
					  &base_obj_id);
	if (err)
		goto err_out;

	bulk->base_obj_id = base_obj_id;
	bulk->num_deks = num_deks;
	bulk->avail_deks = num_deks;
	bulk->mdev = mdev;

	return bulk;

err_out:
	bitmap_free(bulk->in_use);
	bitmap_free(bulk->need_sync);
	kfree(bulk);
	return ERR_PTR(err);
}

static struct mlx5_crypto_dek_bulk *
mlx5_crypto_dek_pool_add_bulk(struct mlx5_crypto_dek_pool *pool)
{
	struct mlx5_crypto_dek_bulk *bulk;

	bulk = mlx5_crypto_dek_bulk_create(pool);
	if (IS_ERR(bulk))
		return bulk;

	pool->avail_deks += bulk->num_deks;
	pool->num_deks += bulk->num_deks;
	list_add(&bulk->entry, &pool->partial_list);

	return bulk;
}

static void mlx5_crypto_dek_bulk_free(struct mlx5_crypto_dek_bulk *bulk)
{
	mlx5_crypto_destroy_dek_key(bulk->mdev, bulk->base_obj_id);
	bitmap_free(bulk->need_sync);
	bitmap_free(bulk->in_use);
	kfree(bulk);
}

static void mlx5_crypto_dek_pool_remove_bulk(struct mlx5_crypto_dek_pool *pool,
					     struct mlx5_crypto_dek_bulk *bulk)
{
	pool->num_deks -= bulk->num_deks;
	pool->avail_deks -= bulk->avail_deks;
	pool->in_use_deks -= bulk->in_use_deks;
	list_del(&bulk->entry);
	mlx5_crypto_dek_bulk_free(bulk);
}

static struct mlx5_crypto_dek_bulk *
mlx5_crypto_dek_pool_pop(struct mlx5_crypto_dek_pool *pool, u32 *obj_offset)
{
	struct mlx5_crypto_dek_bulk *bulk;
	int pos;

	mutex_lock(&pool->lock);
	bulk = list_first_entry_or_null(&pool->partial_list,
					struct mlx5_crypto_dek_bulk, entry);

	if (bulk) {
		pos = find_next_zero_bit(bulk->need_sync, bulk->num_deks,
					 bulk->avail_start);
		if (pos == bulk->num_deks) {
			mlx5_core_err(pool->mdev, "Wrong DEK bulk avail_start.\n");
			pos = find_first_zero_bit(bulk->need_sync, bulk->num_deks);
		}
		WARN_ON(pos == bulk->num_deks);
	} else {
		bulk = mlx5_crypto_dek_pool_add_bulk(pool);
		if (IS_ERR(bulk))
			goto out;
		pos = 0;
	}

	*obj_offset = pos;
	bitmap_set(bulk->need_sync, pos, 1);
	bitmap_set(bulk->in_use, pos, 1);
	bulk->in_use_deks++;
	bulk->avail_deks--;
	if (!bulk->avail_deks) {
		list_move(&bulk->entry, &pool->full_list);
		bulk->avail_start = bulk->num_deks;
	} else {
		bulk->avail_start = pos + 1;
	}
	pool->avail_deks--;
	pool->in_use_deks++;

out:
	mutex_unlock(&pool->lock);
	return bulk;
}

static int mlx5_crypto_dek_pool_push(struct mlx5_crypto_dek_pool *pool,
				     struct mlx5_crypto_dek *dek)
{
	struct mlx5_crypto_dek_bulk *bulk = dek->bulk;
	int obj_offset;
	bool old_val;
	int err = 0;

	mutex_lock(&pool->lock);
	obj_offset = dek->obj_id - bulk->base_obj_id;
	old_val = test_and_clear_bit(obj_offset, bulk->in_use);
	WARN_ON_ONCE(!old_val);
	if (!old_val) {
		err = -ENOENT;
		goto out_free;
	}
	pool->in_use_deks--;
	bulk->in_use_deks--;
	if (!bulk->avail_deks && !bulk->in_use_deks)
		mlx5_crypto_dek_pool_remove_bulk(pool, bulk);

out_free:
	mutex_unlock(&pool->lock);
	kfree(dek);
	return err;
}

struct mlx5_crypto_dek *mlx5_crypto_dek_create(struct mlx5_crypto_dek_pool *dek_pool,
					       const void *key, u32 sz_bytes)
{
	struct mlx5_crypto_dek_priv *dek_priv = dek_pool->mdev->mlx5e_res.dek_priv;
	struct mlx5_core_dev *mdev = dek_pool->mdev;
	u32 key_purpose = dek_pool->key_purpose;
	struct mlx5_crypto_dek_bulk *bulk;
	struct mlx5_crypto_dek *dek;
	int obj_offset;
	int err;

	dek = kzalloc(sizeof(*dek), GFP_KERNEL);
	if (!dek)
		return ERR_PTR(-ENOMEM);

	if (!dek_priv) {
		err = mlx5_crypto_create_dek_key(mdev, key, sz_bytes,
						 key_purpose, &dek->obj_id);
		goto out;
	}

	bulk = mlx5_crypto_dek_pool_pop(dek_pool, &obj_offset);
	if (IS_ERR(bulk)) {
		err = PTR_ERR(bulk);
		goto out;
	}

	dek->bulk = bulk;
	dek->obj_id = bulk->base_obj_id + obj_offset;
	err = mlx5_crypto_modify_dek_key(mdev, key, sz_bytes, key_purpose,
					 bulk->base_obj_id, obj_offset);
	if (err) {
		mlx5_crypto_dek_pool_push(dek_pool, dek);
		return ERR_PTR(err);
	}

out:
	if (err) {
		kfree(dek);
		return ERR_PTR(err);
	}

	return dek;
}

void mlx5_crypto_dek_destroy(struct mlx5_crypto_dek_pool *dek_pool,
			     struct mlx5_crypto_dek *dek)
{
	struct mlx5_crypto_dek_priv *dek_priv = dek_pool->mdev->mlx5e_res.dek_priv;
	struct mlx5_core_dev *mdev = dek_pool->mdev;

	if (!dek_priv) {
		mlx5_crypto_destroy_dek_key(mdev, dek->obj_id);
		kfree(dek);
	} else {
		mlx5_crypto_dek_pool_push(dek_pool, dek);
	}
}

struct mlx5_crypto_dek_pool *
mlx5_crypto_dek_pool_create(struct mlx5_core_dev *mdev, int key_purpose)
{
	struct mlx5_crypto_dek_pool *pool;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	pool->mdev = mdev;
	pool->key_purpose = key_purpose;

	mutex_init(&pool->lock);
	INIT_LIST_HEAD(&pool->partial_list);
	INIT_LIST_HEAD(&pool->full_list);

	return pool;
}

void mlx5_crypto_dek_pool_destroy(struct mlx5_crypto_dek_pool *pool)
{
	struct mlx5_crypto_dek_bulk *bulk, *tmp;

	list_for_each_entry_safe(bulk, tmp, &pool->full_list, entry)
		mlx5_crypto_dek_pool_remove_bulk(pool, bulk);

	list_for_each_entry_safe(bulk, tmp, &pool->partial_list, entry)
		mlx5_crypto_dek_pool_remove_bulk(pool, bulk);

	mutex_destroy(&pool->lock);

	kfree(pool);
}

void mlx5_crypto_dek_cleanup(struct mlx5_crypto_dek_priv *dek_priv)
{
	if (!dek_priv)
		return;

	kfree(dek_priv);
}

struct mlx5_crypto_dek_priv *mlx5_crypto_dek_init(struct mlx5_core_dev *mdev)
{
	struct mlx5_crypto_dek_priv *dek_priv;
	int err;

	if (!MLX5_CAP_CRYPTO(mdev, log_dek_max_alloc))
		return NULL;

	dek_priv = kzalloc(sizeof(*dek_priv), GFP_KERNEL);
	if (!dek_priv)
		return ERR_PTR(-ENOMEM);

	dek_priv->mdev = mdev;
	dek_priv->log_dek_obj_range = min_t(int, 12,
					    MLX5_CAP_CRYPTO(mdev, log_dek_max_alloc));

	/* sync all types of objects */
	err = mlx5_crypto_cmd_sync_crypto(mdev, MLX5_CRYPTO_DEK_ALL_TYPE);
	if (err)
		goto err_sync_crypto;

	mlx5_core_dbg(mdev, "Crypto DEK enabled, %d deks per alloc (max %d), total %d\n",
		      1 << dek_priv->log_dek_obj_range,
		      1 << MLX5_CAP_CRYPTO(mdev, log_dek_max_alloc),
		      1 << MLX5_CAP_CRYPTO(mdev, log_max_num_deks));

	return dek_priv;

err_sync_crypto:
	kfree(dek_priv);
	return ERR_PTR(err);
}
