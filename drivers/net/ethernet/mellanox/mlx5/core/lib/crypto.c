// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2019 Mellanox Technologies.

#include "mlx5_core.h"
#include "lib/crypto.h"

struct mlx5_crypto_dek_priv {
	struct mlx5_core_dev *mdev;
	int log_dek_obj_range;
};

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

int mlx5_create_encryption_key(struct mlx5_core_dev *mdev,
			       const void *key, u32 sz_bytes,
			       u32 key_type, u32 *p_key_id)
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
	MLX5_SET(encryption_key_obj, obj, key_purpose, key_type);
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

void mlx5_destroy_encryption_key(struct mlx5_core_dev *mdev, u32 key_id)
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

void mlx5_crypto_dek_cleanup(struct mlx5_crypto_dek_priv *dek_priv)
{
	if (!dek_priv)
		return;

	kfree(dek_priv);
}

struct mlx5_crypto_dek_priv *mlx5_crypto_dek_init(struct mlx5_core_dev *mdev)
{
	struct mlx5_crypto_dek_priv *dek_priv;

	if (!MLX5_CAP_CRYPTO(mdev, log_dek_max_alloc))
		return NULL;

	dek_priv = kzalloc(sizeof(*dek_priv), GFP_KERNEL);
	if (!dek_priv)
		return ERR_PTR(-ENOMEM);

	dek_priv->mdev = mdev;
	dek_priv->log_dek_obj_range = min_t(int, 12,
					    MLX5_CAP_CRYPTO(mdev, log_dek_max_alloc));

	mlx5_core_dbg(mdev, "Crypto DEK enabled, %d deks per alloc (max %d), total %d\n",
		      1 << dek_priv->log_dek_obj_range,
		      1 << MLX5_CAP_CRYPTO(mdev, log_dek_max_alloc),
		      1 << MLX5_CAP_CRYPTO(mdev, log_max_num_deks));

	return dek_priv;
}
