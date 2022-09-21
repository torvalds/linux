// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/mlx5/device.h>
#include <linux/mlx5/mlx5_ifc.h>
#include <linux/xarray.h>

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

	struct rhash_head hash;
	u32 fs_id;
	union mlx5e_macsec_rule *macsec_rule;
	struct rcu_head rcu_head;
};

struct mlx5e_macsec_rx_sc;
struct mlx5e_macsec_rx_sc_xarray_element {
	u32 fs_id;
	struct mlx5e_macsec_rx_sc *rx_sc;
};

struct mlx5e_macsec_rx_sc {
	bool active;
	sci_t sci;
	struct mlx5e_macsec_sa *rx_sa[MACSEC_NUM_AN];
	struct list_head rx_sc_list_element;
	struct mlx5e_macsec_rx_sc_xarray_element *sc_xarray_element;
	struct metadata_dst *md_dst;
	struct rcu_head rcu_head;
};

static const struct rhashtable_params rhash_sci = {
	.key_len = sizeof_field(struct mlx5e_macsec_sa, sci),
	.key_offset = offsetof(struct mlx5e_macsec_sa, sci),
	.head_offset = offsetof(struct mlx5e_macsec_sa, hash),
	.automatic_shrinking = true,
	.min_size = 1,
};

struct mlx5e_macsec_device {
	const struct net_device *netdev;
	struct mlx5e_macsec_sa *tx_sa[MACSEC_NUM_AN];
	struct list_head macsec_rx_sc_list_head;
	unsigned char *dev_addr;
	struct list_head macsec_device_list_element;
};

struct mlx5e_macsec {
	struct list_head macsec_device_list_head;
	int num_of_devices;
	struct mlx5e_macsec_fs *macsec_fs;
	struct mutex lock; /* Protects mlx5e_macsec internal contexts */

	/* Global PD for MACsec object ASO context */
	u32 aso_pdn;

	/* Tx sci -> fs id mapping handling */
	struct rhashtable sci_hash;      /* sci -> mlx5e_macsec_sa */

	/* Rx fs_id -> rx_sc mapping */
	struct xarray sc_xarray;

	struct mlx5_core_dev *mdev;

	/* Stats manage */
	struct mlx5e_macsec_stats stats;
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
				      bool is_tx,
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
	MLX5_SET(macsec_aso, aso_ctx, mode_parameter, attrs->next_pn);

	MLX5_SET(macsec_aso, aso_ctx, valid, 0x1);
	if (is_tx)
		MLX5_SET(macsec_aso, aso_ctx, mode, MLX5_MACSEC_ASO_INC_SN);

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

static void mlx5e_macsec_cleanup_sa(struct mlx5e_macsec *macsec,
				    struct mlx5e_macsec_sa *sa,
				    bool is_tx)
{
	int action =  (is_tx) ?  MLX5_ACCEL_MACSEC_ACTION_ENCRYPT :
				 MLX5_ACCEL_MACSEC_ACTION_DECRYPT;

	if ((is_tx) && sa->fs_id) {
		/* Make sure ongoing datapath readers sees a valid SA */
		rhashtable_remove_fast(&macsec->sci_hash, &sa->hash, rhash_sci);
		sa->fs_id = 0;
	}

	if (!sa->macsec_rule)
		return;

	mlx5e_macsec_fs_del_rule(macsec->macsec_fs, sa->macsec_rule, action);
	mlx5e_macsec_destroy_object(macsec->mdev, sa->macsec_obj_id);
	sa->macsec_rule = NULL;
}

static int mlx5e_macsec_init_sa(struct macsec_context *ctx,
				struct mlx5e_macsec_sa *sa,
				bool encrypt,
				bool is_tx)
{
	struct mlx5e_priv *priv = netdev_priv(ctx->netdev);
	struct mlx5e_macsec *macsec = priv->macsec;
	struct mlx5_macsec_rule_attrs rule_attrs;
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_macsec_obj_attrs obj_attrs;
	union mlx5e_macsec_rule *macsec_rule;
	int err;

	obj_attrs.next_pn = sa->next_pn;
	obj_attrs.sci = cpu_to_be64((__force u64)sa->sci);
	obj_attrs.enc_key_id = sa->enc_key_id;
	obj_attrs.encrypt = encrypt;
	obj_attrs.aso_pdn = macsec->aso_pdn;

	err = mlx5e_macsec_create_object(mdev, &obj_attrs, is_tx, &sa->macsec_obj_id);
	if (err)
		return err;

	rule_attrs.macsec_obj_id = sa->macsec_obj_id;
	rule_attrs.sci = sa->sci;
	rule_attrs.assoc_num = sa->assoc_num;
	rule_attrs.action = (is_tx) ? MLX5_ACCEL_MACSEC_ACTION_ENCRYPT :
				      MLX5_ACCEL_MACSEC_ACTION_DECRYPT;

	macsec_rule = mlx5e_macsec_fs_add_rule(macsec->macsec_fs, ctx, &rule_attrs, &sa->fs_id);
	if (!macsec_rule) {
		err = -ENOMEM;
		goto destroy_macsec_object;
	}

	sa->macsec_rule = macsec_rule;

	if (is_tx) {
		err = rhashtable_insert_fast(&macsec->sci_hash, &sa->hash, rhash_sci);
		if (err)
			goto destroy_macsec_object_and_rule;
	}

	return 0;

destroy_macsec_object_and_rule:
	mlx5e_macsec_cleanup_sa(macsec, sa, is_tx);
destroy_macsec_object:
	mlx5e_macsec_destroy_object(mdev, sa->macsec_obj_id);

	return err;
}

static struct mlx5e_macsec_rx_sc *
mlx5e_macsec_get_rx_sc_from_sc_list(const struct list_head *list, sci_t sci)
{
	struct mlx5e_macsec_rx_sc *iter;

	list_for_each_entry_rcu(iter, list, rx_sc_list_element) {
		if (iter->sci == sci)
			return iter;
	}

	return NULL;
}

static int mlx5e_macsec_update_rx_sa(struct mlx5e_macsec *macsec,
				     struct mlx5e_macsec_sa *rx_sa,
				     bool active)
{
	struct mlx5_core_dev *mdev = macsec->mdev;
	struct mlx5_macsec_obj_attrs attrs;
	int err = 0;

	if (rx_sa->active != active)
		return 0;

	rx_sa->active = active;
	if (!active) {
		mlx5e_macsec_cleanup_sa(macsec, rx_sa, false);
		return 0;
	}

	attrs.sci = rx_sa->sci;
	attrs.enc_key_id = rx_sa->enc_key_id;
	err = mlx5e_macsec_create_object(mdev, &attrs, false, &rx_sa->macsec_obj_id);
	if (err)
		return err;

	return 0;
}

static bool mlx5e_macsec_secy_features_validate(struct macsec_context *ctx)
{
	const struct net_device *netdev = ctx->netdev;
	const struct macsec_secy *secy = ctx->secy;

	if (secy->validate_frames != MACSEC_VALIDATE_STRICT) {
		netdev_err(netdev,
			   "MACsec offload is supported only when validate_frame is in strict mode\n");
		return false;
	}

	if (secy->icv_len != MACSEC_DEFAULT_ICV_LEN) {
		netdev_err(netdev, "MACsec offload is supported only when icv_len is %d\n",
			   MACSEC_DEFAULT_ICV_LEN);
		return false;
	}

	if (!secy->protect_frames) {
		netdev_err(netdev,
			   "MACsec offload is supported only when protect_frames is set\n");
		return false;
	}

	if (secy->xpn) {
		netdev_err(netdev, "MACsec offload: xpn is not supported\n");
		return false;
	}

	if (secy->replay_protect) {
		netdev_err(netdev, "MACsec offload: replay protection is not supported\n");
		return false;
	}

	return true;
}

static struct mlx5e_macsec_device *
mlx5e_macsec_get_macsec_device_context(const struct mlx5e_macsec *macsec,
				       const struct macsec_context *ctx)
{
	struct mlx5e_macsec_device *iter;
	const struct list_head *list;

	list = &macsec->macsec_device_list_head;
	list_for_each_entry_rcu(iter, list, macsec_device_list_element) {
		if (iter->netdev == ctx->secy->netdev)
			return iter;
	}

	return NULL;
}

static int mlx5e_macsec_add_txsa(struct macsec_context *ctx)
{
	const struct macsec_tx_sc *tx_sc = &ctx->secy->tx_sc;
	const struct macsec_tx_sa *ctx_tx_sa = ctx->sa.tx_sa;
	struct mlx5e_priv *priv = netdev_priv(ctx->netdev);
	const struct macsec_secy *secy = ctx->secy;
	struct mlx5e_macsec_device *macsec_device;
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 assoc_num = ctx->sa.assoc_num;
	struct mlx5e_macsec_sa *tx_sa;
	struct mlx5e_macsec *macsec;
	int err = 0;

	if (ctx->prepare)
		return 0;

	mutex_lock(&priv->macsec->lock);

	macsec = priv->macsec;
	macsec_device = mlx5e_macsec_get_macsec_device_context(macsec, ctx);
	if (!macsec_device) {
		netdev_err(ctx->netdev, "MACsec offload: Failed to find device context\n");
		err = -EEXIST;
		goto out;
	}

	if (macsec_device->tx_sa[assoc_num]) {
		netdev_err(ctx->netdev, "MACsec offload tx_sa: %d already exist\n", assoc_num);
		err = -EEXIST;
		goto out;
	}

	tx_sa = kzalloc(sizeof(*tx_sa), GFP_KERNEL);
	if (!tx_sa) {
		err = -ENOMEM;
		goto out;
	}

	tx_sa->active = ctx_tx_sa->active;
	tx_sa->next_pn = ctx_tx_sa->next_pn_halves.lower;
	tx_sa->sci = secy->sci;
	tx_sa->assoc_num = assoc_num;
	err = mlx5_create_encryption_key(mdev, ctx->sa.key, secy->key_len,
					 MLX5_ACCEL_OBJ_MACSEC_KEY,
					 &tx_sa->enc_key_id);
	if (err)
		goto destroy_sa;

	macsec_device->tx_sa[assoc_num] = tx_sa;
	if (!secy->operational ||
	    assoc_num != tx_sc->encoding_sa ||
	    !tx_sa->active)
		goto out;

	err = mlx5e_macsec_init_sa(ctx, tx_sa, tx_sc->encrypt, true);
	if (err)
		goto destroy_encryption_key;

	mutex_unlock(&macsec->lock);

	return 0;

destroy_encryption_key:
	macsec_device->tx_sa[assoc_num] = NULL;
	mlx5_destroy_encryption_key(mdev, tx_sa->enc_key_id);
destroy_sa:
	kfree(tx_sa);
out:
	mutex_unlock(&macsec->lock);

	return err;
}

static int mlx5e_macsec_upd_txsa(struct macsec_context *ctx)
{
	const struct macsec_tx_sc *tx_sc = &ctx->secy->tx_sc;
	const struct macsec_tx_sa *ctx_tx_sa = ctx->sa.tx_sa;
	struct mlx5e_priv *priv = netdev_priv(ctx->netdev);
	struct mlx5e_macsec_device *macsec_device;
	u8 assoc_num = ctx->sa.assoc_num;
	struct mlx5e_macsec_sa *tx_sa;
	struct mlx5e_macsec *macsec;
	struct net_device *netdev;
	int err = 0;

	if (ctx->prepare)
		return 0;

	mutex_lock(&priv->macsec->lock);

	macsec = priv->macsec;
	netdev = ctx->netdev;
	macsec_device = mlx5e_macsec_get_macsec_device_context(macsec, ctx);
	if (!macsec_device) {
		netdev_err(netdev, "MACsec offload: Failed to find device context\n");
		err = -EINVAL;
		goto out;
	}

	tx_sa = macsec_device->tx_sa[assoc_num];
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
		err = mlx5e_macsec_init_sa(ctx, tx_sa, tx_sc->encrypt, true);
		if (err)
			goto out;
	} else {
		if (!tx_sa->macsec_rule) {
			err = -EINVAL;
			goto out;
		}

		mlx5e_macsec_cleanup_sa(macsec, tx_sa, true);
	}

	tx_sa->active = ctx_tx_sa->active;
out:
	mutex_unlock(&macsec->lock);

	return err;
}

static int mlx5e_macsec_del_txsa(struct macsec_context *ctx)
{
	struct mlx5e_priv *priv = netdev_priv(ctx->netdev);
	struct mlx5e_macsec_device *macsec_device;
	u8 assoc_num = ctx->sa.assoc_num;
	struct mlx5e_macsec_sa *tx_sa;
	struct mlx5e_macsec *macsec;
	int err = 0;

	if (ctx->prepare)
		return 0;

	mutex_lock(&priv->macsec->lock);
	macsec = priv->macsec;
	macsec_device = mlx5e_macsec_get_macsec_device_context(macsec, ctx);
	if (!macsec_device) {
		netdev_err(ctx->netdev, "MACsec offload: Failed to find device context\n");
		err = -EINVAL;
		goto out;
	}

	tx_sa = macsec_device->tx_sa[assoc_num];
	if (!tx_sa) {
		netdev_err(ctx->netdev, "MACsec offload: TX sa 0x%x doesn't exist\n", assoc_num);
		err = -EEXIST;
		goto out;
	}

	mlx5e_macsec_cleanup_sa(macsec, tx_sa, true);
	mlx5_destroy_encryption_key(macsec->mdev, tx_sa->enc_key_id);
	kfree_rcu(tx_sa);
	macsec_device->tx_sa[assoc_num] = NULL;

out:
	mutex_unlock(&macsec->lock);

	return err;
}

static u32 mlx5e_macsec_get_sa_from_hashtable(struct rhashtable *sci_hash, sci_t *sci)
{
	struct mlx5e_macsec_sa *macsec_sa;
	u32 fs_id = 0;

	rcu_read_lock();
	macsec_sa = rhashtable_lookup(sci_hash, sci, rhash_sci);
	if (macsec_sa)
		fs_id = macsec_sa->fs_id;
	rcu_read_unlock();

	return fs_id;
}

static int mlx5e_macsec_add_rxsc(struct macsec_context *ctx)
{
	struct mlx5e_macsec_rx_sc_xarray_element *sc_xarray_element;
	struct mlx5e_priv *priv = netdev_priv(ctx->netdev);
	const struct macsec_rx_sc *ctx_rx_sc = ctx->rx_sc;
	struct mlx5e_macsec_device *macsec_device;
	struct mlx5e_macsec_rx_sc *rx_sc;
	struct list_head *rx_sc_list;
	struct mlx5e_macsec *macsec;
	int err = 0;

	if (ctx->prepare)
		return 0;

	mutex_lock(&priv->macsec->lock);
	macsec = priv->macsec;
	macsec_device = mlx5e_macsec_get_macsec_device_context(macsec, ctx);
	if (!macsec_device) {
		netdev_err(ctx->netdev, "MACsec offload: Failed to find device context\n");
		err = -EINVAL;
		goto out;
	}

	rx_sc_list = &macsec_device->macsec_rx_sc_list_head;
	rx_sc = mlx5e_macsec_get_rx_sc_from_sc_list(rx_sc_list, ctx_rx_sc->sci);
	if (rx_sc) {
		netdev_err(ctx->netdev, "MACsec offload: rx_sc (sci %lld) already exists\n",
			   ctx_rx_sc->sci);
		err = -EEXIST;
		goto out;
	}

	rx_sc = kzalloc(sizeof(*rx_sc), GFP_KERNEL);
	if (!rx_sc) {
		err = -ENOMEM;
		goto out;
	}

	sc_xarray_element = kzalloc(sizeof(*sc_xarray_element), GFP_KERNEL);
	if (!sc_xarray_element) {
		err = -ENOMEM;
		goto destroy_rx_sc;
	}

	sc_xarray_element->rx_sc = rx_sc;
	err = xa_alloc(&macsec->sc_xarray, &sc_xarray_element->fs_id, sc_xarray_element,
		       XA_LIMIT(1, USHRT_MAX), GFP_KERNEL);
	if (err)
		goto destroy_sc_xarray_elemenet;

	rx_sc->md_dst = metadata_dst_alloc(0, METADATA_MACSEC, GFP_KERNEL);
	if (!rx_sc->md_dst) {
		err = -ENOMEM;
		goto erase_xa_alloc;
	}

	rx_sc->sci = ctx_rx_sc->sci;
	rx_sc->active = ctx_rx_sc->active;
	list_add_rcu(&rx_sc->rx_sc_list_element, rx_sc_list);

	rx_sc->sc_xarray_element = sc_xarray_element;
	rx_sc->md_dst->u.macsec_info.sci = rx_sc->sci;
	mutex_unlock(&macsec->lock);

	return 0;

erase_xa_alloc:
	xa_erase(&macsec->sc_xarray, sc_xarray_element->fs_id);
destroy_sc_xarray_elemenet:
	kfree(sc_xarray_element);
destroy_rx_sc:
	kfree(rx_sc);

out:
	mutex_unlock(&macsec->lock);

	return err;
}

static int mlx5e_macsec_upd_rxsc(struct macsec_context *ctx)
{
	struct mlx5e_priv *priv = netdev_priv(ctx->netdev);
	const struct macsec_rx_sc *ctx_rx_sc = ctx->rx_sc;
	struct mlx5e_macsec_device *macsec_device;
	struct mlx5e_macsec_rx_sc *rx_sc;
	struct mlx5e_macsec_sa *rx_sa;
	struct mlx5e_macsec *macsec;
	struct list_head *list;
	int i;
	int err = 0;

	if (ctx->prepare)
		return 0;

	mutex_lock(&priv->macsec->lock);

	macsec = priv->macsec;
	macsec_device = mlx5e_macsec_get_macsec_device_context(macsec, ctx);
	if (!macsec_device) {
		netdev_err(ctx->netdev, "MACsec offload: Failed to find device context\n");
		err = -EINVAL;
		goto out;
	}

	list = &macsec_device->macsec_rx_sc_list_head;
	rx_sc = mlx5e_macsec_get_rx_sc_from_sc_list(list, ctx_rx_sc->sci);
	if (!rx_sc) {
		err = -EINVAL;
		goto out;
	}

	rx_sc->active = ctx_rx_sc->active;
	if (rx_sc->active == ctx_rx_sc->active)
		goto out;

	for (i = 0; i < MACSEC_NUM_AN; ++i) {
		rx_sa = rx_sc->rx_sa[i];
		if (!rx_sa)
			continue;

		err = mlx5e_macsec_update_rx_sa(macsec, rx_sa, rx_sa->active && ctx_rx_sc->active);
		if (err)
			goto out;
	}

out:
	mutex_unlock(&macsec->lock);

	return err;
}

static int mlx5e_macsec_del_rxsc(struct macsec_context *ctx)
{
	struct mlx5e_priv *priv = netdev_priv(ctx->netdev);
	struct mlx5e_macsec_device *macsec_device;
	struct mlx5e_macsec_rx_sc *rx_sc;
	struct mlx5e_macsec_sa *rx_sa;
	struct mlx5e_macsec *macsec;
	struct list_head *list;
	int err = 0;
	int i;

	if (ctx->prepare)
		return 0;

	mutex_lock(&priv->macsec->lock);

	macsec = priv->macsec;
	macsec_device = mlx5e_macsec_get_macsec_device_context(macsec, ctx);
	if (!macsec_device) {
		netdev_err(ctx->netdev, "MACsec offload: Failed to find device context\n");
		err = -EINVAL;
		goto out;
	}

	list = &macsec_device->macsec_rx_sc_list_head;
	rx_sc = mlx5e_macsec_get_rx_sc_from_sc_list(list, ctx->rx_sc->sci);
	if (!rx_sc) {
		netdev_err(ctx->netdev,
			   "MACsec offload rx_sc sci %lld doesn't exist\n",
			   ctx->sa.rx_sa->sc->sci);
		err = -EINVAL;
		goto out;
	}

	for (i = 0; i < MACSEC_NUM_AN; ++i) {
		rx_sa = rx_sc->rx_sa[i];
		if (!rx_sa)
			continue;

		mlx5e_macsec_cleanup_sa(macsec, rx_sa, false);
		mlx5_destroy_encryption_key(macsec->mdev, rx_sa->enc_key_id);

		kfree(rx_sa);
		rx_sc->rx_sa[i] = NULL;
	}

/*
 * At this point the relevant MACsec offload Rx rule already removed at
 * mlx5e_macsec_cleanup_sa need to wait for datapath to finish current
 * Rx related data propagating using xa_erase which uses rcu to sync,
 * once fs_id is erased then this rx_sc is hidden from datapath.
 */
	list_del_rcu(&rx_sc->rx_sc_list_element);
	xa_erase(&macsec->sc_xarray, rx_sc->sc_xarray_element->fs_id);
	metadata_dst_free(rx_sc->md_dst);
	kfree(rx_sc->sc_xarray_element);

	kfree_rcu(rx_sc);

out:
	mutex_unlock(&macsec->lock);

	return err;
}

static int mlx5e_macsec_add_rxsa(struct macsec_context *ctx)
{
	const struct macsec_rx_sa *ctx_rx_sa = ctx->sa.rx_sa;
	struct mlx5e_priv *priv = netdev_priv(ctx->netdev);
	struct mlx5e_macsec_device *macsec_device;
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 assoc_num = ctx->sa.assoc_num;
	struct mlx5e_macsec_rx_sc *rx_sc;
	sci_t sci = ctx_rx_sa->sc->sci;
	struct mlx5e_macsec_sa *rx_sa;
	struct mlx5e_macsec *macsec;
	struct list_head *list;
	int err = 0;

	if (ctx->prepare)
		return 0;

	mutex_lock(&priv->macsec->lock);

	macsec = priv->macsec;
	macsec_device = mlx5e_macsec_get_macsec_device_context(macsec, ctx);
	if (!macsec_device) {
		netdev_err(ctx->netdev, "MACsec offload: Failed to find device context\n");
		err = -EINVAL;
		goto out;
	}

	list = &macsec_device->macsec_rx_sc_list_head;
	rx_sc = mlx5e_macsec_get_rx_sc_from_sc_list(list, sci);
	if (!rx_sc) {
		netdev_err(ctx->netdev,
			   "MACsec offload rx_sc sci %lld doesn't exist\n",
			   ctx->sa.rx_sa->sc->sci);
		err = -EINVAL;
		goto out;
	}

	if (rx_sc->rx_sa[assoc_num]) {
		netdev_err(ctx->netdev,
			   "MACsec offload rx_sc sci %lld rx_sa %d already exist\n",
			   sci, assoc_num);
		err = -EEXIST;
		goto out;
	}

	rx_sa = kzalloc(sizeof(*rx_sa), GFP_KERNEL);
	if (!rx_sa) {
		err = -ENOMEM;
		goto out;
	}

	rx_sa->active = ctx_rx_sa->active;
	rx_sa->next_pn = ctx_rx_sa->next_pn;
	rx_sa->sci = sci;
	rx_sa->assoc_num = assoc_num;
	rx_sa->fs_id = rx_sc->sc_xarray_element->fs_id;

	err = mlx5_create_encryption_key(mdev, ctx->sa.key, ctx->secy->key_len,
					 MLX5_ACCEL_OBJ_MACSEC_KEY,
					 &rx_sa->enc_key_id);
	if (err)
		goto destroy_sa;

	rx_sc->rx_sa[assoc_num] = rx_sa;
	if (!rx_sa->active)
		goto out;

	//TODO - add support for both authentication and encryption flows
	err = mlx5e_macsec_init_sa(ctx, rx_sa, true, false);
	if (err)
		goto destroy_encryption_key;

	goto out;

destroy_encryption_key:
	rx_sc->rx_sa[assoc_num] = NULL;
	mlx5_destroy_encryption_key(mdev, rx_sa->enc_key_id);
destroy_sa:
	kfree(rx_sa);
out:
	mutex_unlock(&macsec->lock);

	return err;
}

static int mlx5e_macsec_upd_rxsa(struct macsec_context *ctx)
{
	const struct macsec_rx_sa *ctx_rx_sa = ctx->sa.rx_sa;
	struct mlx5e_priv *priv = netdev_priv(ctx->netdev);
	struct mlx5e_macsec_device *macsec_device;
	u8 assoc_num = ctx->sa.assoc_num;
	struct mlx5e_macsec_rx_sc *rx_sc;
	sci_t sci = ctx_rx_sa->sc->sci;
	struct mlx5e_macsec_sa *rx_sa;
	struct mlx5e_macsec *macsec;
	struct list_head *list;
	int err = 0;

	if (ctx->prepare)
		return 0;

	mutex_lock(&priv->macsec->lock);

	macsec = priv->macsec;
	macsec_device = mlx5e_macsec_get_macsec_device_context(macsec, ctx);
	if (!macsec_device) {
		netdev_err(ctx->netdev, "MACsec offload: Failed to find device context\n");
		err = -EINVAL;
		goto out;
	}

	list = &macsec_device->macsec_rx_sc_list_head;
	rx_sc = mlx5e_macsec_get_rx_sc_from_sc_list(list, sci);
	if (!rx_sc) {
		netdev_err(ctx->netdev,
			   "MACsec offload rx_sc sci %lld doesn't exist\n",
			   ctx->sa.rx_sa->sc->sci);
		err = -EINVAL;
		goto out;
	}

	rx_sa = rx_sc->rx_sa[assoc_num];
	if (rx_sa) {
		netdev_err(ctx->netdev,
			   "MACsec offload rx_sc sci %lld rx_sa %d already exist\n",
			   sci, assoc_num);
		err = -EEXIST;
		goto out;
	}

	if (rx_sa->next_pn != ctx_rx_sa->next_pn_halves.lower) {
		netdev_err(ctx->netdev,
			   "MACsec offload update RX sa %d PN isn't supported\n",
			   assoc_num);
		err = -EINVAL;
		goto out;
	}

	err = mlx5e_macsec_update_rx_sa(macsec, rx_sa, ctx_rx_sa->active);
out:
	mutex_unlock(&macsec->lock);

	return err;
}

static int mlx5e_macsec_del_rxsa(struct macsec_context *ctx)
{
	struct mlx5e_priv *priv = netdev_priv(ctx->netdev);
	struct mlx5e_macsec_device *macsec_device;
	sci_t sci = ctx->sa.rx_sa->sc->sci;
	struct mlx5e_macsec_rx_sc *rx_sc;
	u8 assoc_num = ctx->sa.assoc_num;
	struct mlx5e_macsec_sa *rx_sa;
	struct mlx5e_macsec *macsec;
	struct list_head *list;
	int err = 0;

	if (ctx->prepare)
		return 0;

	mutex_lock(&priv->macsec->lock);

	macsec = priv->macsec;
	macsec_device = mlx5e_macsec_get_macsec_device_context(macsec, ctx);
	if (!macsec_device) {
		netdev_err(ctx->netdev, "MACsec offload: Failed to find device context\n");
		err = -EINVAL;
		goto out;
	}

	list = &macsec_device->macsec_rx_sc_list_head;
	rx_sc = mlx5e_macsec_get_rx_sc_from_sc_list(list, sci);
	if (!rx_sc) {
		netdev_err(ctx->netdev,
			   "MACsec offload rx_sc sci %lld doesn't exist\n",
			   ctx->sa.rx_sa->sc->sci);
		err = -EINVAL;
		goto out;
	}

	rx_sa = rx_sc->rx_sa[assoc_num];
	if (rx_sa) {
		netdev_err(ctx->netdev,
			   "MACsec offload rx_sc sci %lld rx_sa %d already exist\n",
			   sci, assoc_num);
		err = -EEXIST;
		goto out;
	}

	mlx5e_macsec_cleanup_sa(macsec, rx_sa, false);
	mlx5_destroy_encryption_key(macsec->mdev, rx_sa->enc_key_id);
	kfree(rx_sa);
	rx_sc->rx_sa[assoc_num] = NULL;

out:
	mutex_unlock(&macsec->lock);

	return err;
}

static int mlx5e_macsec_add_secy(struct macsec_context *ctx)
{
	struct mlx5e_priv *priv = netdev_priv(ctx->netdev);
	const struct net_device *dev = ctx->secy->netdev;
	const struct net_device *netdev = ctx->netdev;
	struct mlx5e_macsec_device *macsec_device;
	struct mlx5e_macsec *macsec;
	int err = 0;

	if (ctx->prepare)
		return 0;

	if (!mlx5e_macsec_secy_features_validate(ctx))
		return -EINVAL;

	mutex_lock(&priv->macsec->lock);
	macsec = priv->macsec;
	if (mlx5e_macsec_get_macsec_device_context(macsec, ctx)) {
		netdev_err(netdev, "MACsec offload: MACsec net_device already exist\n");
		goto out;
	}

	if (macsec->num_of_devices >= MLX5_MACSEC_NUM_OF_SUPPORTED_INTERFACES) {
		netdev_err(netdev, "Currently, only %d MACsec offload devices can be set\n",
			   MLX5_MACSEC_NUM_OF_SUPPORTED_INTERFACES);
		err = -EBUSY;
		goto out;
	}

	macsec_device = kzalloc(sizeof(*macsec_device), GFP_KERNEL);
	if (!macsec_device) {
		err = -ENOMEM;
		goto out;
	}

	macsec_device->dev_addr = kmemdup(dev->dev_addr, dev->addr_len, GFP_KERNEL);
	if (!macsec_device->dev_addr) {
		kfree(macsec_device);
		err = -ENOMEM;
		goto out;
	}

	macsec_device->netdev = dev;

	INIT_LIST_HEAD_RCU(&macsec_device->macsec_rx_sc_list_head);
	list_add_rcu(&macsec_device->macsec_device_list_element, &macsec->macsec_device_list_head);

	++macsec->num_of_devices;
out:
	mutex_unlock(&macsec->lock);

	return err;
}

static int macsec_upd_secy_hw_address(struct macsec_context *ctx,
				      struct mlx5e_macsec_device *macsec_device)
{
	struct mlx5e_priv *priv = netdev_priv(ctx->netdev);
	const struct net_device *dev = ctx->secy->netdev;
	struct mlx5e_macsec *macsec = priv->macsec;
	struct mlx5e_macsec_rx_sc *rx_sc, *tmp;
	struct mlx5e_macsec_sa *rx_sa;
	struct list_head *list;
	int i, err = 0;


	list = &macsec_device->macsec_rx_sc_list_head;
	list_for_each_entry_safe(rx_sc, tmp, list, rx_sc_list_element) {
		for (i = 0; i < MACSEC_NUM_AN; ++i) {
			rx_sa = rx_sc->rx_sa[i];
			if (!rx_sa || !rx_sa->macsec_rule)
				continue;

			mlx5e_macsec_cleanup_sa(macsec, rx_sa, false);
		}
	}

	list_for_each_entry_safe(rx_sc, tmp, list, rx_sc_list_element) {
		for (i = 0; i < MACSEC_NUM_AN; ++i) {
			rx_sa = rx_sc->rx_sa[i];
			if (!rx_sa)
				continue;

			if (rx_sa->active) {
				err = mlx5e_macsec_init_sa(ctx, rx_sa, false, false);
				if (err)
					goto out;
			}
		}
	}

	memcpy(macsec_device->dev_addr, dev->dev_addr, dev->addr_len);
out:
	return err;
}

/* this function is called from 2 macsec ops functions:
 *  macsec_set_mac_address – MAC address was changed, therefore we need to destroy
 *  and create new Tx contexts(macsec object + steering).
 *  macsec_changelink – in this case the tx SC or SecY may be changed, therefore need to
 *  destroy Tx and Rx contexts(macsec object + steering)
 */
static int mlx5e_macsec_upd_secy(struct macsec_context *ctx)
{
	const struct macsec_tx_sc *tx_sc = &ctx->secy->tx_sc;
	struct mlx5e_priv *priv = netdev_priv(ctx->netdev);
	const struct net_device *dev = ctx->secy->netdev;
	struct mlx5e_macsec_device *macsec_device;
	struct mlx5e_macsec_sa *tx_sa;
	struct mlx5e_macsec *macsec;
	int i, err = 0;

	if (ctx->prepare)
		return 0;

	if (!mlx5e_macsec_secy_features_validate(ctx))
		return -EINVAL;

	mutex_lock(&priv->macsec->lock);

	macsec = priv->macsec;
	macsec_device = mlx5e_macsec_get_macsec_device_context(macsec, ctx);
	if (!macsec_device) {
		netdev_err(ctx->netdev, "MACsec offload: Failed to find device context\n");
		err = -EINVAL;
		goto out;
	}

	/* if the dev_addr hasn't change, it mean the callback is from macsec_changelink */
	if (!memcmp(macsec_device->dev_addr, dev->dev_addr, dev->addr_len)) {
		err = macsec_upd_secy_hw_address(ctx, macsec_device);
		if (err)
			goto out;
	}

	for (i = 0; i < MACSEC_NUM_AN; ++i) {
		tx_sa = macsec_device->tx_sa[i];
		if (!tx_sa)
			continue;

		mlx5e_macsec_cleanup_sa(macsec, tx_sa, true);
	}

	for (i = 0; i < MACSEC_NUM_AN; ++i) {
		tx_sa = macsec_device->tx_sa[i];
		if (!tx_sa)
			continue;

		if (tx_sa->assoc_num == tx_sc->encoding_sa && tx_sa->active) {
			err = mlx5e_macsec_init_sa(ctx, tx_sa, tx_sc->encrypt, true);
			if (err)
				goto out;
		}
	}

out:
	mutex_unlock(&macsec->lock);

	return err;
}

static int mlx5e_macsec_del_secy(struct macsec_context *ctx)
{
	struct mlx5e_priv *priv = netdev_priv(ctx->netdev);
	struct mlx5e_macsec_device *macsec_device;
	struct mlx5e_macsec_rx_sc *rx_sc, *tmp;
	struct mlx5e_macsec_sa *rx_sa;
	struct mlx5e_macsec_sa *tx_sa;
	struct mlx5e_macsec *macsec;
	struct list_head *list;
	int err = 0;
	int i;

	if (ctx->prepare)
		return 0;

	mutex_lock(&priv->macsec->lock);
	macsec = priv->macsec;
	macsec_device = mlx5e_macsec_get_macsec_device_context(macsec, ctx);
	if (!macsec_device) {
		netdev_err(ctx->netdev, "MACsec offload: Failed to find device context\n");
		err = -EINVAL;

		goto out;
	}

	for (i = 0; i < MACSEC_NUM_AN; ++i) {
		tx_sa = macsec_device->tx_sa[i];
		if (!tx_sa)
			continue;

		mlx5e_macsec_cleanup_sa(macsec, tx_sa, true);
		mlx5_destroy_encryption_key(macsec->mdev, tx_sa->enc_key_id);
		kfree(tx_sa);
		macsec_device->tx_sa[i] = NULL;
	}

	list = &macsec_device->macsec_rx_sc_list_head;
	list_for_each_entry_safe(rx_sc, tmp, list, rx_sc_list_element) {
		for (i = 0; i < MACSEC_NUM_AN; ++i) {
			rx_sa = rx_sc->rx_sa[i];
			if (!rx_sa)
				continue;

			mlx5e_macsec_cleanup_sa(macsec, rx_sa, false);
			mlx5_destroy_encryption_key(macsec->mdev, rx_sa->enc_key_id);
			kfree(rx_sa);
			rx_sc->rx_sa[i] = NULL;
		}

		list_del_rcu(&rx_sc->rx_sc_list_element);

		kfree_rcu(rx_sc);
	}

	kfree(macsec_device->dev_addr);
	macsec_device->dev_addr = NULL;

	list_del_rcu(&macsec_device->macsec_device_list_element);
	--macsec->num_of_devices;

out:
	mutex_unlock(&macsec->lock);

	return err;
}

bool mlx5e_is_macsec_device(const struct mlx5_core_dev *mdev)
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

void mlx5e_macsec_get_stats_fill(struct mlx5e_macsec *macsec, void *macsec_stats)
{
	mlx5e_macsec_fs_get_stats_fill(macsec->macsec_fs, macsec_stats);
}

struct mlx5e_macsec_stats *mlx5e_macsec_get_stats(struct mlx5e_macsec *macsec)
{
	if (!macsec)
		return NULL;

	return &macsec->stats;
}

static const struct macsec_ops macsec_offload_ops = {
	.mdo_add_txsa = mlx5e_macsec_add_txsa,
	.mdo_upd_txsa = mlx5e_macsec_upd_txsa,
	.mdo_del_txsa = mlx5e_macsec_del_txsa,
	.mdo_add_rxsc = mlx5e_macsec_add_rxsc,
	.mdo_upd_rxsc = mlx5e_macsec_upd_rxsc,
	.mdo_del_rxsc = mlx5e_macsec_del_rxsc,
	.mdo_add_rxsa = mlx5e_macsec_add_rxsa,
	.mdo_upd_rxsa = mlx5e_macsec_upd_rxsa,
	.mdo_del_rxsa = mlx5e_macsec_del_rxsa,
	.mdo_add_secy = mlx5e_macsec_add_secy,
	.mdo_upd_secy = mlx5e_macsec_upd_secy,
	.mdo_del_secy = mlx5e_macsec_del_secy,
};

bool mlx5e_macsec_handle_tx_skb(struct mlx5e_macsec *macsec, struct sk_buff *skb)
{
	struct metadata_dst *md_dst = skb_metadata_dst(skb);
	u32 fs_id;

	fs_id = mlx5e_macsec_get_sa_from_hashtable(&macsec->sci_hash, &md_dst->u.macsec_info.sci);
	if (!fs_id)
		goto err_out;

	return true;

err_out:
	dev_kfree_skb_any(skb);
	return false;
}

void mlx5e_macsec_tx_build_eseg(struct mlx5e_macsec *macsec,
				struct sk_buff *skb,
				struct mlx5_wqe_eth_seg *eseg)
{
	struct metadata_dst *md_dst = skb_metadata_dst(skb);
	u32 fs_id;

	fs_id = mlx5e_macsec_get_sa_from_hashtable(&macsec->sci_hash, &md_dst->u.macsec_info.sci);
	if (!fs_id)
		return;

	eseg->flow_table_metadata = cpu_to_be32(MLX5_ETH_WQE_FT_META_MACSEC | fs_id << 2);
}

void mlx5e_macsec_offload_handle_rx_skb(struct net_device *netdev,
					struct sk_buff *skb,
					struct mlx5_cqe64 *cqe)
{
	struct mlx5e_macsec_rx_sc_xarray_element *sc_xarray_element;
	u32 macsec_meta_data = be32_to_cpu(cqe->ft_metadata);
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_macsec_rx_sc *rx_sc;
	struct mlx5e_macsec *macsec;
	u32  fs_id;

	macsec = priv->macsec;
	if (!macsec)
		return;

	fs_id = MLX5_MACSEC_METADATA_HANDLE(macsec_meta_data);

	rcu_read_lock();
	sc_xarray_element = xa_load(&macsec->sc_xarray, fs_id);
	rx_sc = sc_xarray_element->rx_sc;
	if (rx_sc) {
		dst_hold(&rx_sc->md_dst->dst);
		skb_dst_set(skb, &rx_sc->md_dst->dst);
	}

	rcu_read_unlock();
}

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

	INIT_LIST_HEAD(&macsec->macsec_device_list_head);
	mutex_init(&macsec->lock);

	err = mlx5_core_alloc_pd(mdev, &macsec->aso_pdn);
	if (err) {
		mlx5_core_err(mdev,
			      "MACsec offload: Failed to alloc pd for MACsec ASO, err=%d\n",
			      err);
		goto err_pd;
	}

	err = rhashtable_init(&macsec->sci_hash, &rhash_sci);
	if (err) {
		mlx5_core_err(mdev, "MACsec offload: Failed to init SCI hash table, err=%d\n",
			      err);
		goto err_hash;
	}

	xa_init_flags(&macsec->sc_xarray, XA_FLAGS_ALLOC1);

	priv->macsec = macsec;

	macsec->mdev = mdev;

	macsec_fs = mlx5e_macsec_fs_init(mdev, priv->netdev);
	if (!macsec_fs) {
		err = -ENOMEM;
		goto err_out;
	}

	macsec->macsec_fs = macsec_fs;

	mlx5_core_dbg(mdev, "MACsec attached to netdevice\n");

	return 0;

err_out:
	rhashtable_destroy(&macsec->sci_hash);
err_hash:
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

	rhashtable_destroy(&macsec->sci_hash);

	mutex_destroy(&macsec->lock);

	kfree(macsec);
}
