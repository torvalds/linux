// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2017, Mellanox Technologies inc. All rights reserved. */

#include "mlx5_core.h"
#include "ipsec.h"
#include "lib/mlx5.h"

struct mlx5_ipsec_sa_ctx {
	struct rhash_head hash;
	u32 enc_key_id;
	u32 ipsec_obj_id;
	/* hw ctx */
	struct mlx5_core_dev *dev;
	struct mlx5_ipsec_esp_xfrm *mxfrm;
};

struct mlx5_ipsec_esp_xfrm {
	/* reference counter of SA ctx */
	struct mlx5_ipsec_sa_ctx *sa_ctx;
	struct mlx5_accel_esp_xfrm accel_xfrm;
};

u32 mlx5_ipsec_device_caps(struct mlx5_core_dev *mdev)
{
	u32 caps;

	if (!MLX5_CAP_GEN(mdev, ipsec_offload))
		return 0;

	if (!MLX5_CAP_GEN(mdev, log_max_dek))
		return 0;

	if (!(MLX5_CAP_GEN_64(mdev, general_obj_types) &
	    MLX5_HCA_CAP_GENERAL_OBJECT_TYPES_IPSEC))
		return 0;

	if (!MLX5_CAP_IPSEC(mdev, ipsec_crypto_offload) ||
	    !MLX5_CAP_ETH(mdev, insert_trailer))
		return 0;

	if (!MLX5_CAP_FLOWTABLE_NIC_TX(mdev, ipsec_encrypt) ||
	    !MLX5_CAP_FLOWTABLE_NIC_RX(mdev, ipsec_decrypt))
		return 0;

	caps = MLX5_ACCEL_IPSEC_CAP_DEVICE | MLX5_ACCEL_IPSEC_CAP_IPV6 |
	       MLX5_ACCEL_IPSEC_CAP_LSO;

	if (MLX5_CAP_IPSEC(mdev, ipsec_crypto_esp_aes_gcm_128_encrypt) &&
	    MLX5_CAP_IPSEC(mdev, ipsec_crypto_esp_aes_gcm_128_decrypt))
		caps |= MLX5_ACCEL_IPSEC_CAP_ESP;

	if (MLX5_CAP_IPSEC(mdev, ipsec_esn))
		caps |= MLX5_ACCEL_IPSEC_CAP_ESN;

	/* We can accommodate up to 2^24 different IPsec objects
	 * because we use up to 24 bit in flow table metadata
	 * to hold the IPsec Object unique handle.
	 */
	WARN_ON_ONCE(MLX5_CAP_IPSEC(mdev, log_max_ipsec_offload) > 24);
	return caps;
}
EXPORT_SYMBOL_GPL(mlx5_ipsec_device_caps);

struct mlx5_accel_esp_xfrm *
mlx5_accel_esp_create_xfrm(struct mlx5_core_dev *mdev,
			   const struct mlx5_accel_esp_xfrm_attrs *attrs)
{
	struct mlx5_ipsec_esp_xfrm *mxfrm;

	mxfrm = kzalloc(sizeof(*mxfrm), GFP_KERNEL);
	if (!mxfrm)
		return ERR_PTR(-ENOMEM);

	memcpy(&mxfrm->accel_xfrm.attrs, attrs,
	       sizeof(mxfrm->accel_xfrm.attrs));

	mxfrm->accel_xfrm.mdev = mdev;
	return &mxfrm->accel_xfrm;
}

void mlx5_accel_esp_destroy_xfrm(struct mlx5_accel_esp_xfrm *xfrm)
{
	struct mlx5_ipsec_esp_xfrm *mxfrm = container_of(xfrm, struct mlx5_ipsec_esp_xfrm,
							 accel_xfrm);

	kfree(mxfrm);
}

struct mlx5_ipsec_obj_attrs {
	const struct aes_gcm_keymat *aes_gcm;
	u32 accel_flags;
	u32 esn_msb;
	u32 enc_key_id;
};

static int mlx5_create_ipsec_obj(struct mlx5_core_dev *mdev,
				 struct mlx5_ipsec_obj_attrs *attrs,
				 u32 *ipsec_id)
{
	const struct aes_gcm_keymat *aes_gcm = attrs->aes_gcm;
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	u32 in[MLX5_ST_SZ_DW(create_ipsec_obj_in)] = {};
	void *obj, *salt_p, *salt_iv_p;
	int err;

	obj = MLX5_ADDR_OF(create_ipsec_obj_in, in, ipsec_object);

	/* salt and seq_iv */
	salt_p = MLX5_ADDR_OF(ipsec_obj, obj, salt);
	memcpy(salt_p, &aes_gcm->salt, sizeof(aes_gcm->salt));

	switch (aes_gcm->icv_len) {
	case 64:
		MLX5_SET(ipsec_obj, obj, icv_length,
			 MLX5_IPSEC_OBJECT_ICV_LEN_8B);
		break;
	case 96:
		MLX5_SET(ipsec_obj, obj, icv_length,
			 MLX5_IPSEC_OBJECT_ICV_LEN_12B);
		break;
	case 128:
		MLX5_SET(ipsec_obj, obj, icv_length,
			 MLX5_IPSEC_OBJECT_ICV_LEN_16B);
		break;
	default:
		return -EINVAL;
	}
	salt_iv_p = MLX5_ADDR_OF(ipsec_obj, obj, implicit_iv);
	memcpy(salt_iv_p, &aes_gcm->seq_iv, sizeof(aes_gcm->seq_iv));
	/* esn */
	if (attrs->accel_flags & MLX5_ACCEL_ESP_FLAGS_ESN_TRIGGERED) {
		MLX5_SET(ipsec_obj, obj, esn_en, 1);
		MLX5_SET(ipsec_obj, obj, esn_msb, attrs->esn_msb);
		if (attrs->accel_flags & MLX5_ACCEL_ESP_FLAGS_ESN_STATE_OVERLAP)
			MLX5_SET(ipsec_obj, obj, esn_overlap, 1);
	}

	MLX5_SET(ipsec_obj, obj, dekn, attrs->enc_key_id);

	/* general object fields set */
	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_IPSEC);

	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*ipsec_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);

	return err;
}

static void mlx5_destroy_ipsec_obj(struct mlx5_core_dev *mdev, u32 ipsec_id)
{
	u32 in[MLX5_ST_SZ_DW(general_obj_in_cmd_hdr)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_IPSEC);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, ipsec_id);

	mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

static void *mlx5_ipsec_offload_create_sa_ctx(struct mlx5_core_dev *mdev,
					      struct mlx5_accel_esp_xfrm *accel_xfrm,
					      const __be32 saddr[4], const __be32 daddr[4],
					      const __be32 spi, bool is_ipv6, u32 *hw_handle)
{
	struct mlx5_accel_esp_xfrm_attrs *xfrm_attrs = &accel_xfrm->attrs;
	struct aes_gcm_keymat *aes_gcm = &xfrm_attrs->keymat.aes_gcm;
	struct mlx5_ipsec_obj_attrs ipsec_attrs = {};
	struct mlx5_ipsec_esp_xfrm *mxfrm;
	struct mlx5_ipsec_sa_ctx *sa_ctx;
	int err;

	/* alloc SA context */
	sa_ctx = kzalloc(sizeof(*sa_ctx), GFP_KERNEL);
	if (!sa_ctx)
		return ERR_PTR(-ENOMEM);

	sa_ctx->dev = mdev;

	mxfrm = container_of(accel_xfrm, struct mlx5_ipsec_esp_xfrm, accel_xfrm);
	sa_ctx->mxfrm = mxfrm;

	/* key */
	err = mlx5_create_encryption_key(mdev, aes_gcm->aes_key,
					 aes_gcm->key_len / BITS_PER_BYTE,
					 MLX5_ACCEL_OBJ_IPSEC_KEY,
					 &sa_ctx->enc_key_id);
	if (err) {
		mlx5_core_dbg(mdev, "Failed to create encryption key (err = %d)\n", err);
		goto err_sa_ctx;
	}

	ipsec_attrs.aes_gcm = aes_gcm;
	ipsec_attrs.accel_flags = accel_xfrm->attrs.flags;
	ipsec_attrs.esn_msb = accel_xfrm->attrs.esn;
	ipsec_attrs.enc_key_id = sa_ctx->enc_key_id;
	err = mlx5_create_ipsec_obj(mdev, &ipsec_attrs,
				    &sa_ctx->ipsec_obj_id);
	if (err) {
		mlx5_core_dbg(mdev, "Failed to create IPsec object (err = %d)\n", err);
		goto err_enc_key;
	}

	*hw_handle = sa_ctx->ipsec_obj_id;
	mxfrm->sa_ctx = sa_ctx;

	return sa_ctx;

err_enc_key:
	mlx5_destroy_encryption_key(mdev, sa_ctx->enc_key_id);
err_sa_ctx:
	kfree(sa_ctx);
	return ERR_PTR(err);
}

static void mlx5_ipsec_offload_delete_sa_ctx(void *context)
{
	struct mlx5_ipsec_sa_ctx *sa_ctx = (struct mlx5_ipsec_sa_ctx *)context;

	mlx5_destroy_ipsec_obj(sa_ctx->dev, sa_ctx->ipsec_obj_id);
	mlx5_destroy_encryption_key(sa_ctx->dev, sa_ctx->enc_key_id);
	kfree(sa_ctx);
}

static int mlx5_modify_ipsec_obj(struct mlx5_core_dev *mdev,
				 struct mlx5_ipsec_obj_attrs *attrs,
				 u32 ipsec_id)
{
	u32 in[MLX5_ST_SZ_DW(modify_ipsec_obj_in)] = {};
	u32 out[MLX5_ST_SZ_DW(query_ipsec_obj_out)];
	u64 modify_field_select = 0;
	u64 general_obj_types;
	void *obj;
	int err;

	if (!(attrs->accel_flags & MLX5_ACCEL_ESP_FLAGS_ESN_TRIGGERED))
		return 0;

	general_obj_types = MLX5_CAP_GEN_64(mdev, general_obj_types);
	if (!(general_obj_types & MLX5_HCA_CAP_GENERAL_OBJECT_TYPES_IPSEC))
		return -EINVAL;

	/* general object fields set */
	MLX5_SET(general_obj_in_cmd_hdr, in, opcode, MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type, MLX5_GENERAL_OBJECT_TYPES_IPSEC);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, ipsec_id);
	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (err) {
		mlx5_core_err(mdev, "Query IPsec object failed (Object id %d), err = %d\n",
			      ipsec_id, err);
		return err;
	}

	obj = MLX5_ADDR_OF(query_ipsec_obj_out, out, ipsec_object);
	modify_field_select = MLX5_GET64(ipsec_obj, obj, modify_field_select);

	/* esn */
	if (!(modify_field_select & MLX5_MODIFY_IPSEC_BITMASK_ESN_OVERLAP) ||
	    !(modify_field_select & MLX5_MODIFY_IPSEC_BITMASK_ESN_MSB))
		return -EOPNOTSUPP;

	obj = MLX5_ADDR_OF(modify_ipsec_obj_in, in, ipsec_object);
	MLX5_SET(ipsec_obj, obj, esn_msb, attrs->esn_msb);
	if (attrs->accel_flags & MLX5_ACCEL_ESP_FLAGS_ESN_STATE_OVERLAP)
		MLX5_SET(ipsec_obj, obj, esn_overlap, 1);

	/* general object fields set */
	MLX5_SET(general_obj_in_cmd_hdr, in, opcode, MLX5_CMD_OP_MODIFY_GENERAL_OBJECT);

	return mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

void mlx5_accel_esp_modify_xfrm(struct mlx5_accel_esp_xfrm *xfrm,
				const struct mlx5_accel_esp_xfrm_attrs *attrs)
{
	struct mlx5_ipsec_obj_attrs ipsec_attrs = {};
	struct mlx5_core_dev *mdev = xfrm->mdev;
	struct mlx5_ipsec_esp_xfrm *mxfrm;
	int err;

	mxfrm = container_of(xfrm, struct mlx5_ipsec_esp_xfrm, accel_xfrm);

	/* need to add find and replace in ipsec_rhash_sa the sa_ctx */
	/* modify device with new hw_sa */
	ipsec_attrs.accel_flags = attrs->flags;
	ipsec_attrs.esn_msb = attrs->esn;
	err = mlx5_modify_ipsec_obj(mdev,
				    &ipsec_attrs,
				    mxfrm->sa_ctx->ipsec_obj_id);

	if (err)
		return;

	memcpy(&xfrm->attrs, attrs, sizeof(xfrm->attrs));
}

void *mlx5_accel_esp_create_hw_context(struct mlx5_core_dev *mdev,
				       struct mlx5_accel_esp_xfrm *xfrm,
				       u32 *sa_handle)
{
	__be32 saddr[4] = {}, daddr[4] = {};

	if (!xfrm->attrs.is_ipv6) {
		saddr[3] = xfrm->attrs.saddr.a4;
		daddr[3] = xfrm->attrs.daddr.a4;
	} else {
		memcpy(saddr, xfrm->attrs.saddr.a6, sizeof(saddr));
		memcpy(daddr, xfrm->attrs.daddr.a6, sizeof(daddr));
	}

	return mlx5_ipsec_offload_create_sa_ctx(mdev, xfrm, saddr, daddr,
						xfrm->attrs.spi,
						xfrm->attrs.is_ipv6, sa_handle);
}

void mlx5_accel_esp_free_hw_context(struct mlx5_core_dev *mdev, void *context)
{
	mlx5_ipsec_offload_delete_sa_ctx(context);
}
