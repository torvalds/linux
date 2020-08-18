// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2019 Mellanox Technologies.

#include "mlx5_core.h"
#include "lib/mlx5.h"

int mlx5_create_encryption_key(struct mlx5_core_dev *mdev,
			       void *key, u32 sz_bytes,
			       u32 key_type, u32 *p_key_id)
{
	u32 in[MLX5_ST_SZ_DW(create_encryption_key_in)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	u32 sz_bits = sz_bytes * BITS_PER_BYTE;
	u8  general_obj_key_size;
	u64 general_obj_types;
	void *obj, *key_p;
	int err;

	obj = MLX5_ADDR_OF(create_encryption_key_in, in, encryption_key_object);
	key_p = MLX5_ADDR_OF(encryption_key_obj, obj, key);

	general_obj_types = MLX5_CAP_GEN_64(mdev, general_obj_types);
	if (!(general_obj_types &
	      MLX5_HCA_CAP_GENERAL_OBJECT_TYPES_ENCRYPTION_KEY))
		return -EINVAL;

	switch (sz_bits) {
	case 128:
		general_obj_key_size =
			MLX5_GENERAL_OBJECT_TYPE_ENCRYPTION_KEY_KEY_SIZE_128;
		key_p += sz_bytes;
		break;
	case 256:
		general_obj_key_size =
			MLX5_GENERAL_OBJECT_TYPE_ENCRYPTION_KEY_KEY_SIZE_256;
		break;
	default:
		return -EINVAL;
	}

	memcpy(key_p, key, sz_bytes);

	MLX5_SET(encryption_key_obj, obj, key_size, general_obj_key_size);
	MLX5_SET(encryption_key_obj, obj, key_type, key_type);
	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_ENCRYPTION_KEY);
	MLX5_SET(encryption_key_obj, obj, pd, mdev->mlx5e_res.pdn);

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
