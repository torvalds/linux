// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#include <linux/mlx5/device.h>
#include <net/psp.h>
#include <linux/psp.h>
#include "mlx5_core.h"
#include "psp.h"
#include "lib/crypto.h"
#include "en_accel/psp.h"

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

static int mlx5e_psp_assoc_add(struct psp_dev *psd, struct psp_assoc *pas,
			       struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = netdev_priv(psd->main_netdev);

	mlx5_core_dbg(priv->mdev, "PSP assoc add: rx: %u, tx: %u\n",
		      be32_to_cpu(pas->rx.spi), be32_to_cpu(pas->tx.spi));

	return -EINVAL;
}

static void mlx5e_psp_assoc_del(struct psp_dev *psd, struct psp_assoc *pas)
{
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
	struct mlx5e_psp *psp;

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
	mlx5_core_dbg(priv->mdev, "PSP attached to netdevice\n");
	return 0;
}

void mlx5e_psp_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5e_psp *psp = priv->psp;

	if (!psp)
		return;

	priv->psp = NULL;
	kfree(psp);
}
