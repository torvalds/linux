/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "en.h"
#include "lib/crypto.h"

/* mlx5e global resources should be placed in this file.
 * Global resources are common to all the netdevices created on the same nic.
 */

void mlx5e_mkey_set_relaxed_ordering(struct mlx5_core_dev *mdev, void *mkc)
{
	bool ro_write = MLX5_CAP_GEN(mdev, relaxed_ordering_write);
	bool ro_read = MLX5_CAP_GEN(mdev, relaxed_ordering_read) ||
		       (pcie_relaxed_ordering_enabled(mdev->pdev) &&
			MLX5_CAP_GEN(mdev, relaxed_ordering_read_pci_enabled));

	MLX5_SET(mkc, mkc, relaxed_ordering_read, ro_read);
	MLX5_SET(mkc, mkc, relaxed_ordering_write, ro_write);
}

int mlx5e_create_mkey(struct mlx5_core_dev *mdev, u32 pdn, u32 *mkey)
{
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	void *mkc;
	u32 *in;
	int err;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_PA);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, lr, 1);
	mlx5e_mkey_set_relaxed_ordering(mdev, mkc);
	MLX5_SET(mkc, mkc, pd, pdn);
	MLX5_SET(mkc, mkc, length64, 1);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);

	err = mlx5_core_create_mkey(mdev, mkey, in, inlen);

	kvfree(in);
	return err;
}

int mlx5e_create_mdev_resources(struct mlx5_core_dev *mdev)
{
	struct mlx5e_hw_objs *res = &mdev->mlx5e_res.hw_objs;
	int err;

	err = mlx5_core_alloc_pd(mdev, &res->pdn);
	if (err) {
		mlx5_core_err(mdev, "alloc pd failed, %d\n", err);
		return err;
	}

	err = mlx5_core_alloc_transport_domain(mdev, &res->td.tdn);
	if (err) {
		mlx5_core_err(mdev, "alloc td failed, %d\n", err);
		goto err_dealloc_pd;
	}

	err = mlx5e_create_mkey(mdev, res->pdn, &res->mkey);
	if (err) {
		mlx5_core_err(mdev, "create mkey failed, %d\n", err);
		goto err_dealloc_transport_domain;
	}

	err = mlx5_alloc_bfreg(mdev, &res->bfreg, false, false);
	if (err) {
		mlx5_core_err(mdev, "alloc bfreg failed, %d\n", err);
		goto err_destroy_mkey;
	}

	INIT_LIST_HEAD(&res->td.tirs_list);
	mutex_init(&res->td.list_lock);

	mdev->mlx5e_res.dek_priv = mlx5_crypto_dek_init(mdev);
	if (IS_ERR(mdev->mlx5e_res.dek_priv)) {
		mlx5_core_err(mdev, "crypto dek init failed, %ld\n",
			      PTR_ERR(mdev->mlx5e_res.dek_priv));
		mdev->mlx5e_res.dek_priv = NULL;
	}

	return 0;

err_destroy_mkey:
	mlx5_core_destroy_mkey(mdev, res->mkey);
err_dealloc_transport_domain:
	mlx5_core_dealloc_transport_domain(mdev, res->td.tdn);
err_dealloc_pd:
	mlx5_core_dealloc_pd(mdev, res->pdn);
	return err;
}

void mlx5e_destroy_mdev_resources(struct mlx5_core_dev *mdev)
{
	struct mlx5e_hw_objs *res = &mdev->mlx5e_res.hw_objs;

	mlx5_crypto_dek_cleanup(mdev->mlx5e_res.dek_priv);
	mdev->mlx5e_res.dek_priv = NULL;
	mlx5_free_bfreg(mdev, &res->bfreg);
	mlx5_core_destroy_mkey(mdev, res->mkey);
	mlx5_core_dealloc_transport_domain(mdev, res->td.tdn);
	mlx5_core_dealloc_pd(mdev, res->pdn);
	memset(res, 0, sizeof(*res));
}

int mlx5e_refresh_tirs(struct mlx5e_priv *priv, bool enable_uc_lb,
		       bool enable_mc_lb)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_tir *tir;
	u8 lb_flags = 0;
	int err  = 0;
	u32 tirn = 0;
	int inlen;
	void *in;

	inlen = MLX5_ST_SZ_BYTES(modify_tir_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	if (enable_uc_lb)
		lb_flags = MLX5_TIRC_SELF_LB_BLOCK_BLOCK_UNICAST;

	if (enable_mc_lb)
		lb_flags |= MLX5_TIRC_SELF_LB_BLOCK_BLOCK_MULTICAST;

	if (lb_flags)
		MLX5_SET(modify_tir_in, in, ctx.self_lb_block, lb_flags);

	MLX5_SET(modify_tir_in, in, bitmask.self_lb_en, 1);

	mutex_lock(&mdev->mlx5e_res.hw_objs.td.list_lock);
	list_for_each_entry(tir, &mdev->mlx5e_res.hw_objs.td.tirs_list, list) {
		tirn = tir->tirn;
		err = mlx5_core_modify_tir(mdev, tirn, in);
		if (err)
			break;
	}
	mutex_unlock(&mdev->mlx5e_res.hw_objs.td.list_lock);

	kvfree(in);
	if (err)
		netdev_err(priv->netdev, "refresh tir(0x%x) failed, %d\n", tirn, err);

	return err;
}
