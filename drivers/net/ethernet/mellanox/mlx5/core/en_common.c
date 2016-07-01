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

/* mlx5e global resources should be placed in this file.
 * Global resources are common to all the netdevices crated on the same nic.
 */

static int mlx5e_create_mkey(struct mlx5_core_dev *mdev, u32 pdn,
			     struct mlx5_core_mkey *mkey)
{
	struct mlx5_create_mkey_mbox_in *in;
	int err;

	in = mlx5_vzalloc(sizeof(*in));
	if (!in)
		return -ENOMEM;

	in->seg.flags = MLX5_PERM_LOCAL_WRITE |
			MLX5_PERM_LOCAL_READ  |
			MLX5_ACCESS_MODE_PA;
	in->seg.flags_pd = cpu_to_be32(pdn | MLX5_MKEY_LEN64);
	in->seg.qpn_mkey7_0 = cpu_to_be32(0xffffff << 8);

	err = mlx5_core_create_mkey(mdev, mkey, in, sizeof(*in), NULL, NULL,
				    NULL);

	kvfree(in);

	return err;
}

int mlx5e_create_mdev_resources(struct mlx5_core_dev *mdev)
{
	struct mlx5e_resources *res = &mdev->mlx5e_res;
	int err;

	err = mlx5_alloc_map_uar(mdev, &res->cq_uar, false);
	if (err) {
		mlx5_core_err(mdev, "alloc_map uar failed, %d\n", err);
		return err;
	}

	err = mlx5_core_alloc_pd(mdev, &res->pdn);
	if (err) {
		mlx5_core_err(mdev, "alloc pd failed, %d\n", err);
		goto err_unmap_free_uar;
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

	return 0;

err_dealloc_transport_domain:
	mlx5_core_dealloc_transport_domain(mdev, res->td.tdn);
err_dealloc_pd:
	mlx5_core_dealloc_pd(mdev, res->pdn);
err_unmap_free_uar:
	mlx5_unmap_free_uar(mdev, &res->cq_uar);

	return err;
}

void mlx5e_destroy_mdev_resources(struct mlx5_core_dev *mdev)
{
	struct mlx5e_resources *res = &mdev->mlx5e_res;

	mlx5_core_destroy_mkey(mdev, &res->mkey);
	mlx5_core_dealloc_transport_domain(mdev, res->td.tdn);
	mlx5_core_dealloc_pd(mdev, res->pdn);
	mlx5_unmap_free_uar(mdev, &res->cq_uar);
}
