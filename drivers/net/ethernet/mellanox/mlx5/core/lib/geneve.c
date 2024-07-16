// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include <linux/kernel.h>
#include "mlx5_core.h"
#include "geneve.h"

struct mlx5_geneve {
	struct mlx5_core_dev *mdev;
	__be16 opt_class;
	u8 opt_type;
	u32 obj_id;
	struct mutex sync_lock; /* protect GENEVE obj operations */
	u32 refcount;
};

static int mlx5_geneve_tlv_option_create(struct mlx5_core_dev *mdev,
					 __be16 class,
					 u8 type,
					 u8 len)
{
	u32 in[MLX5_ST_SZ_DW(create_geneve_tlv_option_in)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {};
	u64 general_obj_types;
	void *hdr, *opt;
	u16 obj_id;
	int err;

	general_obj_types = MLX5_CAP_GEN_64(mdev, general_obj_types);
	if (!(general_obj_types & MLX5_GENERAL_OBJ_TYPES_CAP_GENEVE_TLV_OPT))
		return -EINVAL;

	hdr = MLX5_ADDR_OF(create_geneve_tlv_option_in, in, hdr);
	opt = MLX5_ADDR_OF(create_geneve_tlv_option_in, in, geneve_tlv_opt);

	MLX5_SET(general_obj_in_cmd_hdr, hdr, opcode, MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, hdr, obj_type, MLX5_OBJ_TYPE_GENEVE_TLV_OPT);

	MLX5_SET(geneve_tlv_option, opt, option_class, be16_to_cpu(class));
	MLX5_SET(geneve_tlv_option, opt, option_type, type);
	MLX5_SET(geneve_tlv_option, opt, option_data_length, len);

	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	obj_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);
	return obj_id;
}

static void mlx5_geneve_tlv_option_destroy(struct mlx5_core_dev *mdev, u16 obj_id)
{
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)] = {};
	u32 in[MLX5_ST_SZ_DW(general_obj_in_cmd_hdr)] = {};

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode, MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type, MLX5_OBJ_TYPE_GENEVE_TLV_OPT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, obj_id);

	mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

int mlx5_geneve_tlv_option_add(struct mlx5_geneve *geneve, struct geneve_opt *opt)
{
	int res = 0;

	if (IS_ERR_OR_NULL(geneve))
		return -EOPNOTSUPP;

	mutex_lock(&geneve->sync_lock);

	if (geneve->refcount) {
		if (geneve->opt_class == opt->opt_class &&
		    geneve->opt_type == opt->type) {
			/* We already have TLV options obj allocated */
			geneve->refcount++;
		} else {
			/* TLV options obj allocated, but its params
			 * do not match the new request.
			 * We support only one such object.
			 */
			mlx5_core_warn(geneve->mdev,
				       "Won't create Geneve TLV opt object with class:type:len = 0x%x:0x%x:%d (another class:type already exists)\n",
				       be16_to_cpu(opt->opt_class),
				       opt->type,
				       opt->length);
			res = -EOPNOTSUPP;
			goto unlock;
		}
	} else {
		/* We don't have any TLV options obj allocated */

		res = mlx5_geneve_tlv_option_create(geneve->mdev,
						    opt->opt_class,
						    opt->type,
						    opt->length);
		if (res < 0) {
			mlx5_core_warn(geneve->mdev,
				       "Failed creating Geneve TLV opt object class:type:len = 0x%x:0x%x:%d (err=%d)\n",
				       be16_to_cpu(opt->opt_class),
				       opt->type, opt->length, res);
			goto unlock;
		}
		geneve->opt_class = opt->opt_class;
		geneve->opt_type = opt->type;
		geneve->obj_id = res;
		geneve->refcount++;
		res = 0;
	}

unlock:
	mutex_unlock(&geneve->sync_lock);
	return res;
}

void mlx5_geneve_tlv_option_del(struct mlx5_geneve *geneve)
{
	if (IS_ERR_OR_NULL(geneve))
		return;

	mutex_lock(&geneve->sync_lock);
	if (--geneve->refcount == 0) {
		/* We've just removed the last user of Geneve option.
		 * Now delete the object in FW.
		 */
		mlx5_geneve_tlv_option_destroy(geneve->mdev, geneve->obj_id);

		geneve->opt_class = 0;
		geneve->opt_type = 0;
		geneve->obj_id = 0;
	}
	mutex_unlock(&geneve->sync_lock);
}

struct mlx5_geneve *mlx5_geneve_create(struct mlx5_core_dev *mdev)
{
	struct mlx5_geneve *geneve =
		kzalloc(sizeof(*geneve), GFP_KERNEL);

	if (!geneve)
		return ERR_PTR(-ENOMEM);
	geneve->mdev = mdev;
	mutex_init(&geneve->sync_lock);

	return geneve;
}

void mlx5_geneve_destroy(struct mlx5_geneve *geneve)
{
	if (IS_ERR_OR_NULL(geneve))
		return;

	/* Lockless since we are unloading */
	if (geneve->refcount)
		mlx5_geneve_tlv_option_destroy(geneve->mdev, geneve->obj_id);

	kfree(geneve);
}
