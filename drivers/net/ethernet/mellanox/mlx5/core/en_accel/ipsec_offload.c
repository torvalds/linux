// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2017, Mellanox Technologies inc. All rights reserved. */

#include "mlx5_core.h"
#include "ipsec.h"
#include "lib/mlx5.h"

u32 mlx5_ipsec_device_caps(struct mlx5_core_dev *mdev)
{
	u32 caps = 0;

	if (!MLX5_CAP_GEN(mdev, ipsec_offload))
		return 0;

	if (!MLX5_CAP_GEN(mdev, log_max_dek))
		return 0;

	if (!(MLX5_CAP_GEN_64(mdev, general_obj_types) &
	    MLX5_HCA_CAP_GENERAL_OBJECT_TYPES_IPSEC))
		return 0;

	if (!MLX5_CAP_FLOWTABLE_NIC_TX(mdev, ipsec_encrypt) ||
	    !MLX5_CAP_FLOWTABLE_NIC_RX(mdev, ipsec_decrypt))
		return 0;

	if (!MLX5_CAP_IPSEC(mdev, ipsec_crypto_esp_aes_gcm_128_encrypt) ||
	    !MLX5_CAP_IPSEC(mdev, ipsec_crypto_esp_aes_gcm_128_decrypt))
		return 0;

	if (MLX5_CAP_IPSEC(mdev, ipsec_crypto_offload) &&
	    MLX5_CAP_ETH(mdev, insert_trailer) && MLX5_CAP_ETH(mdev, swp))
		caps |= MLX5_IPSEC_CAP_CRYPTO;

	if (!caps)
		return 0;

	if (MLX5_CAP_IPSEC(mdev, ipsec_esn))
		caps |= MLX5_IPSEC_CAP_ESN;

	/* We can accommodate up to 2^24 different IPsec objects
	 * because we use up to 24 bit in flow table metadata
	 * to hold the IPsec Object unique handle.
	 */
	WARN_ON_ONCE(MLX5_CAP_IPSEC(mdev, log_max_ipsec_offload) > 24);
	return caps;
}
EXPORT_SYMBOL_GPL(mlx5_ipsec_device_caps);

static int mlx5_create_ipsec_obj(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5_accel_esp_xfrm_attrs *attrs = &sa_entry->attrs;
	struct mlx5_core_dev *mdev = mlx5e_ipsec_sa2dev(sa_entry);
	struct aes_gcm_keymat *aes_gcm = &attrs->keymat.aes_gcm;
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	u32 in[MLX5_ST_SZ_DW(create_ipsec_obj_in)] = {};
	void *obj, *salt_p, *salt_iv_p;
	int err;

	obj = MLX5_ADDR_OF(create_ipsec_obj_in, in, ipsec_object);

	/* salt and seq_iv */
	salt_p = MLX5_ADDR_OF(ipsec_obj, obj, salt);
	memcpy(salt_p, &aes_gcm->salt, sizeof(aes_gcm->salt));

	MLX5_SET(ipsec_obj, obj, icv_length, MLX5_IPSEC_OBJECT_ICV_LEN_16B);
	salt_iv_p = MLX5_ADDR_OF(ipsec_obj, obj, implicit_iv);
	memcpy(salt_iv_p, &aes_gcm->seq_iv, sizeof(aes_gcm->seq_iv));
	/* esn */
	if (attrs->flags & MLX5_ACCEL_ESP_FLAGS_ESN_TRIGGERED) {
		MLX5_SET(ipsec_obj, obj, esn_en, 1);
		MLX5_SET(ipsec_obj, obj, esn_msb, attrs->esn);
		if (attrs->flags & MLX5_ACCEL_ESP_FLAGS_ESN_STATE_OVERLAP)
			MLX5_SET(ipsec_obj, obj, esn_overlap, 1);
	}

	MLX5_SET(ipsec_obj, obj, dekn, sa_entry->enc_key_id);

	/* general object fields set */
	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_IPSEC);

	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (!err)
		sa_entry->ipsec_obj_id =
			MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);

	return err;
}

static void mlx5_destroy_ipsec_obj(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5_core_dev *mdev = mlx5e_ipsec_sa2dev(sa_entry);
	u32 in[MLX5_ST_SZ_DW(general_obj_in_cmd_hdr)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_IPSEC);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, sa_entry->ipsec_obj_id);

	mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

int mlx5_ipsec_create_sa_ctx(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct aes_gcm_keymat *aes_gcm = &sa_entry->attrs.keymat.aes_gcm;
	struct mlx5_core_dev *mdev = mlx5e_ipsec_sa2dev(sa_entry);
	int err;

	/* key */
	err = mlx5_create_encryption_key(mdev, aes_gcm->aes_key,
					 aes_gcm->key_len / BITS_PER_BYTE,
					 MLX5_ACCEL_OBJ_IPSEC_KEY,
					 &sa_entry->enc_key_id);
	if (err) {
		mlx5_core_dbg(mdev, "Failed to create encryption key (err = %d)\n", err);
		return err;
	}

	err = mlx5_create_ipsec_obj(sa_entry);
	if (err) {
		mlx5_core_dbg(mdev, "Failed to create IPsec object (err = %d)\n", err);
		goto err_enc_key;
	}

	return 0;

err_enc_key:
	mlx5_destroy_encryption_key(mdev, sa_entry->enc_key_id);
	return err;
}

void mlx5_ipsec_free_sa_ctx(struct mlx5e_ipsec_sa_entry *sa_entry)
{
	struct mlx5_core_dev *mdev = mlx5e_ipsec_sa2dev(sa_entry);

	mlx5_destroy_ipsec_obj(sa_entry);
	mlx5_destroy_encryption_key(mdev, sa_entry->enc_key_id);
}

static int mlx5_modify_ipsec_obj(struct mlx5e_ipsec_sa_entry *sa_entry,
				 const struct mlx5_accel_esp_xfrm_attrs *attrs)
{
	struct mlx5_core_dev *mdev = mlx5e_ipsec_sa2dev(sa_entry);
	u32 in[MLX5_ST_SZ_DW(modify_ipsec_obj_in)] = {};
	u32 out[MLX5_ST_SZ_DW(query_ipsec_obj_out)];
	u64 modify_field_select = 0;
	u64 general_obj_types;
	void *obj;
	int err;

	if (!(attrs->flags & MLX5_ACCEL_ESP_FLAGS_ESN_TRIGGERED))
		return 0;

	general_obj_types = MLX5_CAP_GEN_64(mdev, general_obj_types);
	if (!(general_obj_types & MLX5_HCA_CAP_GENERAL_OBJECT_TYPES_IPSEC))
		return -EINVAL;

	/* general object fields set */
	MLX5_SET(general_obj_in_cmd_hdr, in, opcode, MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type, MLX5_GENERAL_OBJECT_TYPES_IPSEC);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, sa_entry->ipsec_obj_id);
	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (err) {
		mlx5_core_err(mdev, "Query IPsec object failed (Object id %d), err = %d\n",
			      sa_entry->ipsec_obj_id, err);
		return err;
	}

	obj = MLX5_ADDR_OF(query_ipsec_obj_out, out, ipsec_object);
	modify_field_select = MLX5_GET64(ipsec_obj, obj, modify_field_select);

	/* esn */
	if (!(modify_field_select & MLX5_MODIFY_IPSEC_BITMASK_ESN_OVERLAP) ||
	    !(modify_field_select & MLX5_MODIFY_IPSEC_BITMASK_ESN_MSB))
		return -EOPNOTSUPP;

	obj = MLX5_ADDR_OF(modify_ipsec_obj_in, in, ipsec_object);
	MLX5_SET(ipsec_obj, obj, esn_msb, attrs->esn);
	if (attrs->flags & MLX5_ACCEL_ESP_FLAGS_ESN_STATE_OVERLAP)
		MLX5_SET(ipsec_obj, obj, esn_overlap, 1);

	/* general object fields set */
	MLX5_SET(general_obj_in_cmd_hdr, in, opcode, MLX5_CMD_OP_MODIFY_GENERAL_OBJECT);

	return mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

void mlx5_accel_esp_modify_xfrm(struct mlx5e_ipsec_sa_entry *sa_entry,
				const struct mlx5_accel_esp_xfrm_attrs *attrs)
{
	int err;

	err = mlx5_modify_ipsec_obj(sa_entry, attrs);
	if (err)
		return;

	memcpy(&sa_entry->attrs, attrs, sizeof(sa_entry->attrs));
}
