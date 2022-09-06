// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/mlx5/device.h>
#include <linux/mlx5/mlx5_ifc.h>

#include "en.h"
#include "lib/mlx5.h"
#include "en_accel/macsec.h"
#include "en_accel/macsec_fs.h"

#define MLX5_MACSEC_ASO_INC_SN  0x2
#define MLX5_MACSEC_ASO_REG_C_4_5 0x2

struct mlx5e_macsec_sa {
	bool active;
	u8  assoc_num;
	u32 macsec_obj_id;
	u32 enc_key_id;
	u32 next_pn;
	sci_t sci;

	struct mlx5e_macsec_tx_rule *tx_rule;
};

struct mlx5e_macsec {
	struct mlx5e_macsec_fs *macsec_fs;
	struct mlx5e_macsec_sa *tx_sa[MACSEC_NUM_AN];
	struct mutex lock; /* Protects mlx5e_macsec internal contexts */

	/* Global PD for MACsec object ASO context */
	u32 aso_pdn;

	struct mlx5_core_dev *mdev;
};

struct mlx5_macsec_obj_attrs {
	u32 aso_pdn;
	u32 next_pn;
	__be64 sci;
	u32 enc_key_id;
	bool encrypt;
};

static int mlx5e_macsec_create_object(struct mlx5_core_dev *mdev,
				      struct mlx5_macsec_obj_attrs *attrs,
				      u32 *macsec_obj_id)
{
	u32 in[MLX5_ST_SZ_DW(create_macsec_obj_in)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	void *aso_ctx;
	void *obj;
	int err;

	obj = MLX5_ADDR_OF(create_macsec_obj_in, in, macsec_object);
	aso_ctx = MLX5_ADDR_OF(macsec_offload_obj, obj, macsec_aso);

	MLX5_SET(macsec_offload_obj, obj, confidentiality_en, attrs->encrypt);
	MLX5_SET(macsec_offload_obj, obj, dekn, attrs->enc_key_id);
	MLX5_SET64(macsec_offload_obj, obj, sci, (__force u64)(attrs->sci));
	MLX5_SET(macsec_offload_obj, obj, aso_return_reg, MLX5_MACSEC_ASO_REG_C_4_5);
	MLX5_SET(macsec_offload_obj, obj, macsec_aso_access_pd, attrs->aso_pdn);

	MLX5_SET(macsec_aso, aso_ctx, valid, 0x1);
	MLX5_SET(macsec_aso, aso_ctx, mode, MLX5_MACSEC_ASO_INC_SN);
	MLX5_SET(macsec_aso, aso_ctx, mode_parameter, attrs->next_pn);

	/* general object fields set */
	MLX5_SET(general_obj_in_cmd_hdr, in, opcode, MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type, MLX5_GENERAL_OBJECT_TYPES_MACSEC);

	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (err) {
		mlx5_core_err(mdev,
			      "MACsec offload: Failed to create MACsec object (err = %d)\n",
			      err);
		return err;
	}

	*macsec_obj_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);

	return err;
}

static void mlx5e_macsec_destroy_object(struct mlx5_core_dev *mdev, u32 macsec_obj_id)
{
	u32 in[MLX5_ST_SZ_DW(general_obj_in_cmd_hdr)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode, MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type, MLX5_GENERAL_OBJECT_TYPES_MACSEC);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, macsec_obj_id);

	mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

static void mlx5e_macsec_cleanup_sa(struct mlx5e_macsec *macsec, struct mlx5e_macsec_sa *sa)
{

	if (!sa->tx_rule)
		return;

	mlx5e_macsec_fs_del_rule(macsec->macsec_fs, sa->tx_rule,
				 MLX5_ACCEL_MACSEC_ACTION_ENCRYPT);
	mlx5e_macsec_destroy_object(macsec->mdev, sa->macsec_obj_id);
	sa->tx_rule = NULL;
}

static int mlx5e_macsec_init_sa(struct macsec_context *ctx,
				struct mlx5e_macsec_sa *sa,
				bool encrypt)
{
	struct mlx5e_priv *priv = netdev_priv(ctx->netdev);
	struct mlx5e_macsec *macsec = priv->macsec;
	struct mlx5_macsec_rule_attrs rule_attrs;
	struct mlx5e_macsec_tx_rule *tx_rule;
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_macsec_obj_attrs obj_attrs;
	int err;

	obj_attrs.next_pn = sa->next_pn;
	obj_attrs.sci = cpu_to_be64((__force u64)sa->sci);
	obj_attrs.enc_key_id = sa->enc_key_id;
	obj_attrs.encrypt = encrypt;
	obj_attrs.aso_pdn = macsec->aso_pdn;

	err = mlx5e_macsec_create_object(mdev, &obj_attrs, &sa->macsec_obj_id);
	if (err)
		return err;

	rule_attrs.macsec_obj_id = sa->macsec_obj_id;
	rule_attrs.action = MLX5_ACCEL_MACSEC_ACTION_ENCRYPT;

	tx_rule = mlx5e_macsec_fs_add_rule(macsec->macsec_fs, ctx, &rule_attrs);
	if (IS_ERR_OR_NULL(tx_rule))
		goto destroy_macsec_object;

	sa->tx_rule = tx_rule;

	return 0;

destroy_macsec_object:
	mlx5e_macsec_destroy_object(mdev, sa->macsec_obj_id);

	return err;
}

static int mlx5e_macsec_add_txsa(struct macsec_context *ctx)
{
	const struct macsec_tx_sc *tx_sc = &ctx->secy->tx_sc;
	const struct macsec_tx_sa *ctx_tx_sa = ctx->sa.tx_sa;
	struct mlx5e_priv *priv = netdev_priv(ctx->netdev);
	const struct macsec_secy *secy = ctx->secy;
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 assoc_num = ctx->sa.assoc_num;
	struct mlx5e_macsec_sa *tx_sa;
	struct mlx5e_macsec *macsec;
	int err = 0;

	if (ctx->prepare)
		return 0;

	mutex_lock(&priv->macsec->lock);

	macsec = priv->macsec;

	if (macsec->tx_sa[assoc_num]) {
		netdev_err(ctx->netdev, "MACsec offload tx_sa: %d already exist\n", assoc_num);
		err = -EEXIST;
		goto out;
	}

	tx_sa = kzalloc(sizeof(*tx_sa), GFP_KERNEL);
	if (!tx_sa) {
		err = -ENOMEM;
		goto out;
	}

	macsec->tx_sa[assoc_num] = tx_sa;

	tx_sa->active = ctx_tx_sa->active;
	tx_sa->next_pn = ctx_tx_sa->next_pn_halves.lower;
	tx_sa->sci = secy->sci;
	tx_sa->assoc_num = assoc_num;

	err = mlx5_create_encryption_key(mdev, ctx->sa.key, secy->key_len,
					 MLX5_ACCEL_OBJ_MACSEC_KEY,
					 &tx_sa->enc_key_id);
	if (err)
		goto destroy_sa;

	if (!secy->operational ||
	    assoc_num != tx_sc->encoding_sa ||
	    !tx_sa->active)
		goto out;

	err = mlx5e_macsec_init_sa(ctx, tx_sa, tx_sc->encrypt);
	if (err)
		goto destroy_encryption_key;

	mutex_unlock(&macsec->lock);

	return 0;

destroy_encryption_key:
	mlx5_destroy_encryption_key(mdev, tx_sa->enc_key_id);
destroy_sa:
	kfree(tx_sa);
	macsec->tx_sa[assoc_num] = NULL;
out:
	mutex_unlock(&macsec->lock);

	return err;
}

static int mlx5e_macsec_upd_txsa(struct macsec_context *ctx)
{
	const struct macsec_tx_sc *tx_sc = &ctx->secy->tx_sc;
	const struct macsec_tx_sa *ctx_tx_sa = ctx->sa.tx_sa;
	struct mlx5e_priv *priv = netdev_priv(ctx->netdev);
	u8 assoc_num = ctx->sa.assoc_num;
	struct mlx5e_macsec_sa *tx_sa;
	struct mlx5e_macsec *macsec;
	struct net_device *netdev;
	int err = 0;

	if (ctx->prepare)
		return 0;

	mutex_lock(&priv->macsec->lock);

	macsec = priv->macsec;
	tx_sa = macsec->tx_sa[assoc_num];
	netdev = ctx->netdev;

	if (!tx_sa) {
		netdev_err(netdev, "MACsec offload: TX sa 0x%x doesn't exist\n", assoc_num);

		err = -EEXIST;
		goto out;
	}

	if (tx_sa->next_pn != ctx_tx_sa->next_pn_halves.lower) {
		netdev_err(netdev, "MACsec offload: update TX sa %d PN isn't supported\n",
			   assoc_num);
		err = -EINVAL;
		goto out;
	}

	if (tx_sa->active == ctx_tx_sa->active)
		goto out;

	if (tx_sa->assoc_num != tx_sc->encoding_sa)
		goto out;

	if (ctx_tx_sa->active) {
		err = mlx5e_macsec_init_sa(ctx, tx_sa, tx_sc->encrypt);
		if (err)
			goto out;
	} else {
		if (!tx_sa->tx_rule)
			return -EINVAL;

		mlx5e_macsec_cleanup_sa(macsec, tx_sa);
	}

	tx_sa->active = ctx_tx_sa->active;
out:
	mutex_unlock(&macsec->lock);

	return err;
}

static int mlx5e_macsec_del_txsa(struct macsec_context *ctx)
{
	struct mlx5e_priv *priv = netdev_priv(ctx->netdev);
	u8 assoc_num = ctx->sa.assoc_num;
	struct mlx5e_macsec_sa *tx_sa;
	struct mlx5e_macsec *macsec;
	int err = 0;

	if (ctx->prepare)
		return 0;

	mutex_lock(&priv->macsec->lock);

	macsec = priv->macsec;
	tx_sa = macsec->tx_sa[ctx->sa.assoc_num];

	if (!tx_sa) {
		netdev_err(ctx->netdev, "MACsec offload: TX sa 0x%x doesn't exist\n", assoc_num);
		err = -EEXIST;
		goto out;
	}

	mlx5e_macsec_cleanup_sa(macsec, tx_sa);
	mlx5_destroy_encryption_key(macsec->mdev, tx_sa->enc_key_id);
	kfree(tx_sa);
	macsec->tx_sa[assoc_num] = NULL;

out:
	mutex_unlock(&macsec->lock);

	return err;
}

static bool mlx5e_is_macsec_device(const struct mlx5_core_dev *mdev)
{
	if (!(MLX5_CAP_GEN_64(mdev, general_obj_types) &
	    MLX5_GENERAL_OBJ_TYPES_CAP_MACSEC_OFFLOAD))
		return false;

	if (!MLX5_CAP_GEN(mdev, log_max_dek))
		return false;

	if (!MLX5_CAP_MACSEC(mdev, log_max_macsec_offload))
		return false;

	if (!MLX5_CAP_FLOWTABLE_NIC_RX(mdev, macsec_decrypt) ||
	    !MLX5_CAP_FLOWTABLE_NIC_RX(mdev, reformat_remove_macsec))
		return false;

	if (!MLX5_CAP_FLOWTABLE_NIC_TX(mdev, macsec_encrypt) ||
	    !MLX5_CAP_FLOWTABLE_NIC_TX(mdev, reformat_add_macsec))
		return false;

	if (!MLX5_CAP_MACSEC(mdev, macsec_crypto_esp_aes_gcm_128_encrypt) &&
	    !MLX5_CAP_MACSEC(mdev, macsec_crypto_esp_aes_gcm_256_encrypt))
		return false;

	if (!MLX5_CAP_MACSEC(mdev, macsec_crypto_esp_aes_gcm_128_decrypt) &&
	    !MLX5_CAP_MACSEC(mdev, macsec_crypto_esp_aes_gcm_256_decrypt))
		return false;

	return true;
}

static const struct macsec_ops macsec_offload_ops = {
	.mdo_add_txsa = mlx5e_macsec_add_txsa,
	.mdo_upd_txsa = mlx5e_macsec_upd_txsa,
	.mdo_del_txsa = mlx5e_macsec_del_txsa,
};

void mlx5e_macsec_build_netdev(struct mlx5e_priv *priv)
{
	struct net_device *netdev = priv->netdev;

	if (!mlx5e_is_macsec_device(priv->mdev))
		return;

	/* Enable MACsec */
	mlx5_core_dbg(priv->mdev, "mlx5e: MACsec acceleration enabled\n");
	netdev->macsec_ops = &macsec_offload_ops;
	netdev->features |= NETIF_F_HW_MACSEC;
	netif_keep_dst(netdev);
}

int mlx5e_macsec_init(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_macsec *macsec = NULL;
	struct mlx5e_macsec_fs *macsec_fs;
	int err;

	if (!mlx5e_is_macsec_device(priv->mdev)) {
		mlx5_core_dbg(mdev, "Not a MACsec offload device\n");
		return 0;
	}

	macsec = kzalloc(sizeof(*macsec), GFP_KERNEL);
	if (!macsec)
		return -ENOMEM;

	mutex_init(&macsec->lock);

	err = mlx5_core_alloc_pd(mdev, &macsec->aso_pdn);
	if (err) {
		mlx5_core_err(mdev,
			      "MACsec offload: Failed to alloc pd for MACsec ASO, err=%d\n",
			      err);
		goto err_pd;
	}

	priv->macsec = macsec;

	macsec->mdev = mdev;

	macsec_fs = mlx5e_macsec_fs_init(mdev, priv->netdev);
	if (IS_ERR_OR_NULL(macsec_fs))
		goto err_out;

	macsec->macsec_fs = macsec_fs;

	mlx5_core_dbg(mdev, "MACsec attached to netdevice\n");

	return 0;

err_out:
	mlx5_core_dealloc_pd(priv->mdev, macsec->aso_pdn);
err_pd:
	kfree(macsec);
	priv->macsec = NULL;
	return err;
}

void mlx5e_macsec_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5e_macsec *macsec = priv->macsec;

	if (!macsec)
		return;

	mlx5e_macsec_fs_cleanup(macsec->macsec_fs);

	priv->macsec = NULL;

	mlx5_core_dealloc_pd(priv->mdev, macsec->aso_pdn);

	mutex_destroy(&macsec->lock);

	kfree(macsec);
}
