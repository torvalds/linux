/*
 * Copyright (c) 2014, Mellanox Technologies inc.  All rights reserved.
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

#include <linux/pci.h>
#include <linux/mlx5/driver.h>
#include "mlx5_core.h"
#ifdef CONFIG_MLX5_CORE_EN
#include "eswitch.h"
#endif

static void enable_vfs(struct mlx5_core_dev *dev, int num_vfs)
{
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;
	int err;
	int vf;

	for (vf = 1; vf <= num_vfs; vf++) {
		err = mlx5_core_enable_hca(dev, vf);
		if (err) {
			mlx5_core_warn(dev, "failed to enable VF %d\n", vf - 1);
		} else {
			sriov->vfs_ctx[vf - 1].enabled = 1;
			mlx5_core_dbg(dev, "successfully enabled VF %d\n", vf - 1);
		}
	}
}

static void disable_vfs(struct mlx5_core_dev *dev, int num_vfs)
{
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;
	int vf;

	for (vf = 1; vf <= num_vfs; vf++) {
		if (sriov->vfs_ctx[vf - 1].enabled) {
			if (mlx5_core_disable_hca(dev, vf))
				mlx5_core_warn(dev, "failed to disable VF %d\n", vf - 1);
			else
				sriov->vfs_ctx[vf - 1].enabled = 0;
		}
	}
}

static int mlx5_core_create_vfs(struct pci_dev *pdev, int num_vfs)
{
	struct mlx5_core_dev *dev  = pci_get_drvdata(pdev);
	int err;

	if (pci_num_vf(pdev))
		pci_disable_sriov(pdev);

	enable_vfs(dev, num_vfs);

	err = pci_enable_sriov(pdev, num_vfs);
	if (err) {
		dev_warn(&pdev->dev, "enable sriov failed %d\n", err);
		goto ex;
	}

	return 0;

ex:
	disable_vfs(dev, num_vfs);
	return err;
}

static int mlx5_core_sriov_enable(struct pci_dev *pdev, int num_vfs)
{
	struct mlx5_core_dev *dev  = pci_get_drvdata(pdev);
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;
	int err;

	kfree(sriov->vfs_ctx);
	sriov->vfs_ctx = kcalloc(num_vfs, sizeof(*sriov->vfs_ctx), GFP_ATOMIC);
	if (!sriov->vfs_ctx)
		return -ENOMEM;

	sriov->enabled_vfs = num_vfs;
	err = mlx5_core_create_vfs(pdev, num_vfs);
	if (err) {
		kfree(sriov->vfs_ctx);
		sriov->vfs_ctx = NULL;
		return err;
	}

	return 0;
}

static void mlx5_core_init_vfs(struct mlx5_core_dev *dev, int num_vfs)
{
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;

	sriov->num_vfs = num_vfs;
}

static void mlx5_core_cleanup_vfs(struct mlx5_core_dev *dev)
{
	struct mlx5_core_sriov *sriov;

	sriov = &dev->priv.sriov;
	disable_vfs(dev, sriov->num_vfs);

	if (mlx5_wait_for_vf_pages(dev))
		mlx5_core_warn(dev, "timeout claiming VFs pages\n");

	sriov->num_vfs = 0;
}

int mlx5_core_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct mlx5_core_dev *dev  = pci_get_drvdata(pdev);
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;
	int err;

	mlx5_core_dbg(dev, "requested num_vfs %d\n", num_vfs);
	if (!mlx5_core_is_pf(dev))
		return -EPERM;

	mlx5_core_cleanup_vfs(dev);

	if (!num_vfs) {
#ifdef CONFIG_MLX5_CORE_EN
		mlx5_eswitch_disable_sriov(dev->priv.eswitch);
#endif
		kfree(sriov->vfs_ctx);
		sriov->vfs_ctx = NULL;
		if (!pci_vfs_assigned(pdev))
			pci_disable_sriov(pdev);
		else
			mlx5_core_info(dev, "unloading PF driver while leaving orphan VFs\n");
		return 0;
	}

	err = mlx5_core_sriov_enable(pdev, num_vfs);
	if (err) {
		mlx5_core_warn(dev, "mlx5_core_sriov_enable failed %d\n", err);
		return err;
	}

	mlx5_core_init_vfs(dev, num_vfs);
#ifdef CONFIG_MLX5_CORE_EN
	mlx5_eswitch_enable_sriov(dev->priv.eswitch, num_vfs, SRIOV_LEGACY);
#endif

	return num_vfs;
}

static int sync_required(struct pci_dev *pdev)
{
	struct mlx5_core_dev *dev  = pci_get_drvdata(pdev);
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;
	int cur_vfs = pci_num_vf(pdev);

	if (cur_vfs != sriov->num_vfs) {
		mlx5_core_warn(dev, "current VFs %d, registered %d - sync needed\n",
			       cur_vfs, sriov->num_vfs);
		return 1;
	}

	return 0;
}

int mlx5_sriov_init(struct mlx5_core_dev *dev)
{
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;
	struct pci_dev *pdev = dev->pdev;
	int cur_vfs;

	if (!mlx5_core_is_pf(dev))
		return 0;

	if (!sync_required(dev->pdev))
		return 0;

	cur_vfs = pci_num_vf(pdev);
	sriov->vfs_ctx = kcalloc(cur_vfs, sizeof(*sriov->vfs_ctx), GFP_KERNEL);
	if (!sriov->vfs_ctx)
		return -ENOMEM;

	sriov->enabled_vfs = cur_vfs;

	mlx5_core_init_vfs(dev, cur_vfs);
#ifdef CONFIG_MLX5_CORE_EN
	if (cur_vfs)
		mlx5_eswitch_enable_sriov(dev->priv.eswitch, cur_vfs,
					  SRIOV_LEGACY);
#endif

	enable_vfs(dev, cur_vfs);

	return 0;
}

int mlx5_sriov_cleanup(struct mlx5_core_dev *dev)
{
	struct pci_dev *pdev = dev->pdev;
	int err;

	if (!mlx5_core_is_pf(dev))
		return 0;

	err = mlx5_core_sriov_configure(pdev, 0);
	if (err)
		return err;

	return 0;
}
