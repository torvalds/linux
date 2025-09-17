// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#include <linux/mlx5/device.h>
#include <net/psp.h>
#include <linux/psp.h>
#include "mlx5_core.h"
#include "psp.h"
#include "lib/crypto.h"
#include "en_accel/psp.h"
#include "fs_core.h"

struct mlx5e_psp_tx {
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	struct mlx5_flow_handle *rule;
	struct mutex mutex; /* Protect PSP TX steering */
	u32 refcnt;
};

struct mlx5e_psp_fs {
	struct mlx5_core_dev *mdev;
	struct mlx5e_psp_tx *tx_fs;
	struct mlx5e_flow_steering *fs;
};

static void setup_fte_udp_psp(struct mlx5_flow_spec *spec, u16 udp_port)
{
	spec->match_criteria_enable |= MLX5_MATCH_OUTER_HEADERS;
	MLX5_SET(fte_match_set_lyr_2_4, spec->match_criteria, udp_dport, 0xffff);
	MLX5_SET(fte_match_set_lyr_2_4, spec->match_value, udp_dport, udp_port);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, spec->match_criteria, ip_protocol);
	MLX5_SET(fte_match_set_lyr_2_4, spec->match_value, ip_protocol, IPPROTO_UDP);
}

static int accel_psp_fs_tx_create_ft_table(struct mlx5e_psp_fs *fs)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_core_dev *mdev = fs->mdev;
	struct mlx5_flow_act flow_act = {};
	u32 *in, *mc, *outer_headers_c;
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	struct mlx5e_psp_tx *tx_fs;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	int err = 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!spec || !in) {
		err = -ENOMEM;
		goto out;
	}

	ft_attr.max_fte = 1;
#define MLX5E_PSP_PRIO 0
	ft_attr.prio = MLX5E_PSP_PRIO;
#define MLX5E_PSP_LEVEL 0
	ft_attr.level = MLX5E_PSP_LEVEL;
	ft_attr.autogroup.max_num_groups = 1;

	tx_fs = fs->tx_fs;
	ft = mlx5_create_flow_table(tx_fs->ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		mlx5_core_err(mdev, "PSP: fail to add psp tx flow table, err = %d\n", err);
		goto out;
	}

	mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	outer_headers_c = MLX5_ADDR_OF(fte_match_param, mc, outer_headers);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, ip_protocol);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, udp_dport);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	fg = mlx5_create_flow_group(ft, in);
	if (IS_ERR(fg)) {
		err = PTR_ERR(fg);
		mlx5_core_err(mdev, "PSP: fail to add psp tx flow group, err = %d\n", err);
		goto err_create_fg;
	}

	setup_fte_udp_psp(spec, PSP_DEFAULT_UDP_PORT);
	flow_act.crypto.type = MLX5_FLOW_CONTEXT_ENCRYPT_DECRYPT_TYPE_PSP;
	flow_act.flags |= FLOW_ACT_NO_APPEND;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_ALLOW |
			  MLX5_FLOW_CONTEXT_ACTION_CRYPTO_ENCRYPT;
	rule = mlx5_add_flow_rules(ft, spec, &flow_act, NULL, 0);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "PSP: fail to add psp tx flow rule, err = %d\n", err);
		goto err_add_flow_rule;
	}

	tx_fs->ft = ft;
	tx_fs->fg = fg;
	tx_fs->rule = rule;
	goto out;

err_add_flow_rule:
	mlx5_destroy_flow_group(fg);
err_create_fg:
	mlx5_destroy_flow_table(ft);
out:
	kvfree(in);
	kvfree(spec);
	return err;
}

static void accel_psp_fs_tx_destroy(struct mlx5e_psp_tx *tx_fs)
{
	if (!tx_fs->ft)
		return;

	mlx5_del_flow_rules(tx_fs->rule);
	mlx5_destroy_flow_group(tx_fs->fg);
	mlx5_destroy_flow_table(tx_fs->ft);
}

static int accel_psp_fs_tx_ft_get(struct mlx5e_psp_fs *fs)
{
	struct mlx5e_psp_tx *tx_fs = fs->tx_fs;
	int err = 0;

	mutex_lock(&tx_fs->mutex);
	if (tx_fs->refcnt++)
		goto out;

	err = accel_psp_fs_tx_create_ft_table(fs);
	if (err)
		tx_fs->refcnt--;
out:
	mutex_unlock(&tx_fs->mutex);
	return err;
}

static void accel_psp_fs_tx_ft_put(struct mlx5e_psp_fs *fs)
{
	struct mlx5e_psp_tx *tx_fs = fs->tx_fs;

	mutex_lock(&tx_fs->mutex);
	if (--tx_fs->refcnt)
		goto out;

	accel_psp_fs_tx_destroy(tx_fs);
out:
	mutex_unlock(&tx_fs->mutex);
}

static void accel_psp_fs_cleanup_tx(struct mlx5e_psp_fs *fs)
{
	struct mlx5e_psp_tx *tx_fs = fs->tx_fs;

	if (!tx_fs)
		return;

	mutex_destroy(&tx_fs->mutex);
	WARN_ON(tx_fs->refcnt);
	kfree(tx_fs);
	fs->tx_fs = NULL;
}

static int accel_psp_fs_init_tx(struct mlx5e_psp_fs *fs)
{
	struct mlx5_flow_namespace *ns;
	struct mlx5e_psp_tx *tx_fs;

	ns = mlx5_get_flow_namespace(fs->mdev, MLX5_FLOW_NAMESPACE_EGRESS_IPSEC);
	if (!ns)
		return -EOPNOTSUPP;

	tx_fs = kzalloc(sizeof(*tx_fs), GFP_KERNEL);
	if (!tx_fs)
		return -ENOMEM;

	mutex_init(&tx_fs->mutex);
	tx_fs->ns = ns;
	fs->tx_fs = tx_fs;
	return 0;
}

void mlx5_accel_psp_fs_cleanup_tx_tables(struct mlx5e_priv *priv)
{
	if (!priv->psp)
		return;

	accel_psp_fs_tx_ft_put(priv->psp->fs);
}

int mlx5_accel_psp_fs_init_tx_tables(struct mlx5e_priv *priv)
{
	if (!priv->psp)
		return 0;

	return accel_psp_fs_tx_ft_get(priv->psp->fs);
}

static void mlx5e_accel_psp_fs_cleanup(struct mlx5e_psp_fs *fs)
{
	accel_psp_fs_cleanup_tx(fs);
	kfree(fs);
}

static struct mlx5e_psp_fs *mlx5e_accel_psp_fs_init(struct mlx5e_priv *priv)
{
	struct mlx5e_psp_fs *fs;
	int err = 0;

	fs = kzalloc(sizeof(*fs), GFP_KERNEL);
	if (!fs)
		return ERR_PTR(-ENOMEM);

	fs->mdev = priv->mdev;
	err = accel_psp_fs_init_tx(fs);
	if (err)
		goto err_tx;

	fs->fs = priv->fs;

	return fs;
err_tx:
	kfree(fs);
	return ERR_PTR(err);
}

static int
mlx5e_psp_set_config(struct psp_dev *psd, struct psp_dev_config *conf,
		     struct netlink_ext_ack *extack)
{
	return 0; /* TODO: this should actually do things to the device */
}

static int
mlx5e_psp_generate_key_spi(struct mlx5_core_dev *mdev,
			   enum mlx5_psp_gen_spi_in_key_size keysz,
			   unsigned int keysz_bytes,
			   struct psp_key_parsed *key)
{
	u32 out[MLX5_ST_SZ_DW(psp_gen_spi_out) + MLX5_ST_SZ_DW(key_spi)] = {};
	u32 in[MLX5_ST_SZ_DW(psp_gen_spi_in)] = {};
	void *outkey;
	int err;

	WARN_ON_ONCE(keysz_bytes > PSP_MAX_KEY);

	MLX5_SET(psp_gen_spi_in, in, opcode, MLX5_CMD_OP_PSP_GEN_SPI);
	MLX5_SET(psp_gen_spi_in, in, key_size, keysz);
	MLX5_SET(psp_gen_spi_in, in, num_of_spi, 1);
	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	outkey = MLX5_ADDR_OF(psp_gen_spi_out, out, key_spi);
	key->spi = cpu_to_be32(MLX5_GET(key_spi, outkey, spi));
	memcpy(key->key, MLX5_ADDR_OF(key_spi, outkey, key) + 32 - keysz_bytes,
	       keysz_bytes);

	return 0;
}

static int
mlx5e_psp_rx_spi_alloc(struct psp_dev *psd, u32 version,
		       struct psp_key_parsed *assoc,
		       struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = netdev_priv(psd->main_netdev);
	enum mlx5_psp_gen_spi_in_key_size keysz;
	u8 keysz_bytes;

	switch (version) {
	case PSP_VERSION_HDR0_AES_GCM_128:
		keysz = MLX5_PSP_GEN_SPI_IN_KEY_SIZE_128;
		keysz_bytes = 16;
		break;
	case PSP_VERSION_HDR0_AES_GCM_256:
		keysz = MLX5_PSP_GEN_SPI_IN_KEY_SIZE_256;
		keysz_bytes = 32;
		break;
	default:
		return -EINVAL;
	}

	return mlx5e_psp_generate_key_spi(priv->mdev, keysz, keysz_bytes, assoc);
}

struct psp_key {
	u32 id;
};

static int mlx5e_psp_assoc_add(struct psp_dev *psd, struct psp_assoc *pas,
			       struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = netdev_priv(psd->main_netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct psp_key_parsed *tx = &pas->tx;
	struct mlx5e_psp *psp = priv->psp;
	struct psp_key *nkey;
	int err;

	mdev = priv->mdev;
	nkey = (struct psp_key *)pas->drv_data;

	err = mlx5_create_encryption_key(mdev, tx->key,
					 psp_key_size(pas->version),
					 MLX5_ACCEL_OBJ_PSP_KEY,
					 &nkey->id);
	if (err) {
		mlx5_core_err(mdev, "Failed to create encryption key (err = %d)\n", err);
		return err;
	}

	atomic_inc(&psp->tx_key_cnt);
	return 0;
}

static void mlx5e_psp_assoc_del(struct psp_dev *psd, struct psp_assoc *pas)
{
	struct mlx5e_priv *priv = netdev_priv(psd->main_netdev);
	struct mlx5e_psp *psp = priv->psp;
	struct psp_key *nkey;

	nkey = (struct psp_key *)pas->drv_data;
	mlx5_destroy_encryption_key(priv->mdev, nkey->id);
	atomic_dec(&psp->tx_key_cnt);
}

static struct psp_dev_ops mlx5_psp_ops = {
	.set_config   = mlx5e_psp_set_config,
	.rx_spi_alloc = mlx5e_psp_rx_spi_alloc,
	.tx_key_add   = mlx5e_psp_assoc_add,
	.tx_key_del   = mlx5e_psp_assoc_del,
};

void mlx5e_psp_unregister(struct mlx5e_priv *priv)
{
	if (!priv->psp || !priv->psp->psp)
		return;

	psp_dev_unregister(priv->psp->psp);
}

void mlx5e_psp_register(struct mlx5e_priv *priv)
{
	/* FW Caps missing */
	if (!priv->psp)
		return;

	priv->psp->caps.assoc_drv_spc = sizeof(u32);
	priv->psp->caps.versions = 1 << PSP_VERSION_HDR0_AES_GCM_128;
	if (MLX5_CAP_PSP(priv->mdev, psp_crypto_esp_aes_gcm_256_encrypt) &&
	    MLX5_CAP_PSP(priv->mdev, psp_crypto_esp_aes_gcm_256_decrypt))
		priv->psp->caps.versions |= 1 << PSP_VERSION_HDR0_AES_GCM_256;

	priv->psp->psp = psp_dev_create(priv->netdev, &mlx5_psp_ops,
					&priv->psp->caps, NULL);
	if (IS_ERR(priv->psp->psp))
		mlx5_core_err(priv->mdev, "PSP failed to register due to %pe\n",
			      priv->psp->psp);
}

int mlx5e_psp_init(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_psp_fs *fs;
	struct mlx5e_psp *psp;
	int err;

	if (!mlx5_is_psp_device(mdev)) {
		mlx5_core_dbg(mdev, "PSP offload not supported\n");
		return -EOPNOTSUPP;
	}

	if (!MLX5_CAP_ETH(mdev, swp)) {
		mlx5_core_dbg(mdev, "SWP not supported\n");
		return -EOPNOTSUPP;
	}

	if (!MLX5_CAP_ETH(mdev, swp_csum)) {
		mlx5_core_dbg(mdev, "SWP checksum not supported\n");
		return -EOPNOTSUPP;
	}

	if (!MLX5_CAP_ETH(mdev, swp_csum_l4_partial)) {
		mlx5_core_dbg(mdev, "SWP L4 partial checksum not supported\n");
		return -EOPNOTSUPP;
	}

	if (!MLX5_CAP_ETH(mdev, swp_lso)) {
		mlx5_core_dbg(mdev, "PSP LSO not supported\n");
		return -EOPNOTSUPP;
	}

	psp = kzalloc(sizeof(*psp), GFP_KERNEL);
	if (!psp)
		return -ENOMEM;

	priv->psp = psp;
	fs = mlx5e_accel_psp_fs_init(priv);
	if (IS_ERR(fs)) {
		err = PTR_ERR(fs);
		goto out_err;
	}

	psp->fs = fs;

	mlx5_core_dbg(priv->mdev, "PSP attached to netdevice\n");
	return 0;

out_err:
	priv->psp = NULL;
	kfree(psp);
	return err;
}

void mlx5e_psp_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5e_psp *psp = priv->psp;

	if (!psp)
		return;

	WARN_ON(atomic_read(&psp->tx_key_cnt));
	mlx5e_accel_psp_fs_cleanup(psp->fs);
	priv->psp = NULL;
	kfree(psp);
}
