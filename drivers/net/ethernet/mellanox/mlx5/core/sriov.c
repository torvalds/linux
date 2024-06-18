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
#include <linux/mlx5/vport.h>
#include "mlx5_core.h"
#include "mlx5_irq.h"
#include "eswitch.h"

static int sriov_restore_guids(struct mlx5_core_dev *dev, int vf, u16 func_id)
{
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;
	struct mlx5_hca_vport_context *in;
	int err = 0;

	/* Restore sriov guid and policy settings */
	if (sriov->vfs_ctx[vf].node_guid ||
	    sriov->vfs_ctx[vf].port_guid ||
	    sriov->vfs_ctx[vf].policy != MLX5_POLICY_INVALID) {
		in = kzalloc(sizeof(*in), GFP_KERNEL);
		if (!in)
			return -ENOMEM;

		in->node_guid = sriov->vfs_ctx[vf].node_guid;
		in->port_guid = sriov->vfs_ctx[vf].port_guid;
		in->policy = sriov->vfs_ctx[vf].policy;
		in->field_select =
			!!(in->port_guid) * MLX5_HCA_VPORT_SEL_PORT_GUID |
			!!(in->node_guid) * MLX5_HCA_VPORT_SEL_NODE_GUID |
			!!(in->policy) * MLX5_HCA_VPORT_SEL_STATE_POLICY;

		err = mlx5_core_modify_hca_vport_context(dev, 1, 1, func_id, in);
		if (err)
			mlx5_core_warn(dev, "modify vport context failed, unable to restore VF %d settings\n", vf);

		kfree(in);
	}

	return err;
}

static int mlx5_device_enable_sriov(struct mlx5_core_dev *dev, int num_vfs)
{
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;
	int err, vf, num_msix_count;
	int vport_num;

	err = mlx5_eswitch_enable(dev->priv.eswitch, num_vfs);
	if (err) {
		mlx5_core_warn(dev,
			       "failed to enable eswitch SRIOV (%d)\n", err);
		return err;
	}

	num_msix_count = mlx5_get_default_msix_vec_count(dev, num_vfs);
	for (vf = 0; vf < num_vfs; vf++) {
		/* Notify the VF before its enablement to let it set
		 * some stuff.
		 */
		blocking_notifier_call_chain(&sriov->vfs_ctx[vf].notifier,
					     MLX5_PF_NOTIFY_ENABLE_VF, dev);
		err = mlx5_core_enable_hca(dev, vf + 1);
		if (err) {
			mlx5_core_warn(dev, "failed to enable VF %d (%d)\n", vf, err);
			continue;
		}

		err = mlx5_set_msix_vec_count(dev, vf + 1, num_msix_count);
		if (err) {
			mlx5_core_warn(dev,
				       "failed to set MSI-X vector counts VF %d, err %d\n",
				       vf, err);
			continue;
		}

		sriov->vfs_ctx[vf].enabled = 1;
		if (MLX5_CAP_GEN(dev, port_type) == MLX5_CAP_PORT_TYPE_IB) {
			vport_num = mlx5_core_ec_sriov_enabled(dev) ?
					mlx5_core_ec_vf_vport_base(dev) + vf
					: vf + 1;
			err = sriov_restore_guids(dev, vf, vport_num);
			if (err) {
				mlx5_core_warn(dev,
					       "failed to restore VF %d settings, err %d\n",
					       vf, err);
				continue;
			}
		}
		mlx5_core_dbg(dev, "successfully enabled VF* %d\n", vf);
	}

	return 0;
}

static void
mlx5_device_disable_sriov(struct mlx5_core_dev *dev, int num_vfs, bool clear_vf, bool num_vf_change)
{
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;
	bool wait_for_ec_vf_pages = true;
	bool wait_for_vf_pages = true;
	int err;
	int vf;

	for (vf = num_vfs - 1; vf >= 0; vf--) {
		if (!sriov->vfs_ctx[vf].enabled)
			continue;
		/* Notify the VF before its disablement to let it clean
		 * some resources.
		 */
		blocking_notifier_call_chain(&sriov->vfs_ctx[vf].notifier,
					     MLX5_PF_NOTIFY_DISABLE_VF, dev);
		err = mlx5_core_disable_hca(dev, vf + 1);
		if (err) {
			mlx5_core_warn(dev, "failed to disable VF %d\n", vf);
			continue;
		}
		sriov->vfs_ctx[vf].enabled = 0;
	}

	mlx5_eswitch_disable_sriov(dev->priv.eswitch, clear_vf);

	/* There are a number of scenarios when SRIOV is being disabled:
	 *     1. VFs or ECVFs had been created, and now set back to 0 (num_vf_change == true).
	 *		- If EC SRIOV is enabled then this flow is happening on the
	 *		  embedded platform, wait for only EC VF pages.
	 *		- If EC SRIOV is not enabled this flow is happening on non-embedded
	 *		  platform, wait for the VF pages.
	 *
	 *     2. The driver is being unloaded. In this case wait for all pages.
	 */
	if (num_vf_change) {
		if (mlx5_core_ec_sriov_enabled(dev))
			wait_for_vf_pages = false;
		else
			wait_for_ec_vf_pages = false;
	}

	if (wait_for_ec_vf_pages && mlx5_wait_for_pages(dev, &dev->priv.page_counters[MLX5_EC_VF]))
		mlx5_core_warn(dev, "timeout reclaiming EC VFs pages\n");

	/* For ECPFs, skip waiting for host VF pages until ECPF is destroyed */
	if (mlx5_core_is_ecpf(dev))
		return;

	if (wait_for_vf_pages && mlx5_wait_for_pages(dev, &dev->priv.page_counters[MLX5_VF]))
		mlx5_core_warn(dev, "timeout reclaiming VFs pages\n");
}

static int mlx5_sriov_enable(struct pci_dev *pdev, int num_vfs)
{
	struct mlx5_core_dev *dev  = pci_get_drvdata(pdev);
	struct devlink *devlink = priv_to_devlink(dev);
	int err;

	devl_lock(devlink);
	err = mlx5_device_enable_sriov(dev, num_vfs);
	devl_unlock(devlink);
	if (err) {
		mlx5_core_warn(dev, "mlx5_device_enable_sriov failed : %d\n", err);
		return err;
	}

	err = pci_enable_sriov(pdev, num_vfs);
	if (err) {
		mlx5_core_warn(dev, "pci_enable_sriov failed : %d\n", err);
		mlx5_device_disable_sriov(dev, num_vfs, true, true);
	}
	return err;
}

void mlx5_sriov_disable(struct pci_dev *pdev, bool num_vf_change)
{
	struct mlx5_core_dev *dev  = pci_get_drvdata(pdev);
	struct devlink *devlink = priv_to_devlink(dev);
	int num_vfs = pci_num_vf(dev->pdev);

	pci_disable_sriov(pdev);
	devl_lock(devlink);
	mlx5_device_disable_sriov(dev, num_vfs, true, num_vf_change);
	devl_unlock(devlink);
}

int mlx5_core_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct mlx5_core_dev *dev  = pci_get_drvdata(pdev);
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;
	int err = 0;

	mlx5_core_dbg(dev, "requested num_vfs %d\n", num_vfs);

	if (num_vfs)
		err = mlx5_sriov_enable(pdev, num_vfs);
	else
		mlx5_sriov_disable(pdev, true);

	if (!err)
		sriov->num_vfs = num_vfs;
	return err ? err : num_vfs;
}

int mlx5_core_sriov_set_msix_vec_count(struct pci_dev *vf, int msix_vec_count)
{
	struct pci_dev *pf = pci_physfn(vf);
	struct mlx5_core_sriov *sriov;
	struct mlx5_core_dev *dev;
	int num_vf_msix, id;

	dev = pci_get_drvdata(pf);
	num_vf_msix = MLX5_CAP_GEN_MAX(dev, num_total_dynamic_vf_msix);
	if (!num_vf_msix)
		return -EOPNOTSUPP;

	if (!msix_vec_count)
		msix_vec_count =
			mlx5_get_default_msix_vec_count(dev, pci_num_vf(pf));

	sriov = &dev->priv.sriov;
	id = pci_iov_vf_id(vf);
	if (id < 0 || !sriov->vfs_ctx[id].enabled)
		return -EINVAL;

	return mlx5_set_msix_vec_count(dev, id + 1, msix_vec_count);
}

int mlx5_sriov_attach(struct mlx5_core_dev *dev)
{
	if (!mlx5_core_is_pf(dev) || !pci_num_vf(dev->pdev))
		return 0;

	/* If sriov VFs exist in PCI level, enable them in device level */
	return mlx5_device_enable_sriov(dev, pci_num_vf(dev->pdev));
}

void mlx5_sriov_detach(struct mlx5_core_dev *dev)
{
	if (!mlx5_core_is_pf(dev))
		return;

	mlx5_device_disable_sriov(dev, pci_num_vf(dev->pdev), false, false);
}

static u16 mlx5_get_max_vfs(struct mlx5_core_dev *dev)
{
	u16 host_total_vfs;
	const u32 *out;

	if (mlx5_core_is_ecpf_esw_manager(dev)) {
		out = mlx5_esw_query_functions(dev);

		/* Old FW doesn't support getting total_vfs from esw func
		 * but supports getting it from pci_sriov.
		 */
		if (IS_ERR(out))
			goto done;
		host_total_vfs = MLX5_GET(query_esw_functions_out, out,
					  host_params_context.host_total_vfs);
		kvfree(out);
		return host_total_vfs;
	}

done:
	return pci_sriov_get_totalvfs(dev->pdev);
}

int mlx5_sriov_init(struct mlx5_core_dev *dev)
{
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;
	struct pci_dev *pdev = dev->pdev;
	int total_vfs, i;

	if (!mlx5_core_is_pf(dev))
		return 0;

	total_vfs = pci_sriov_get_totalvfs(pdev);
	sriov->max_vfs = mlx5_get_max_vfs(dev);
	sriov->num_vfs = pci_num_vf(pdev);
	sriov->max_ec_vfs = mlx5_core_ec_sriov_enabled(dev) ? pci_sriov_get_totalvfs(dev->pdev) : 0;
	sriov->vfs_ctx = kcalloc(total_vfs, sizeof(*sriov->vfs_ctx), GFP_KERNEL);
	if (!sriov->vfs_ctx)
		return -ENOMEM;

	for (i = 0; i < total_vfs; i++)
		BLOCKING_INIT_NOTIFIER_HEAD(&sriov->vfs_ctx[i].notifier);

	return 0;
}

void mlx5_sriov_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_core_sriov *sriov = &dev->priv.sriov;

	if (!mlx5_core_is_pf(dev))
		return;

	kfree(sriov->vfs_ctx);
}

/**
 * mlx5_sriov_blocking_notifier_unregister - Unregister a VF from
 * a notification block chain.
 *
 * @mdev: The mlx5 core device.
 * @vf_id: The VF id.
 * @nb: The notifier block to be unregistered.
 */
void mlx5_sriov_blocking_notifier_unregister(struct mlx5_core_dev *mdev,
					     int vf_id,
					     struct notifier_block *nb)
{
	struct mlx5_vf_context *vfs_ctx;
	struct mlx5_core_sriov *sriov;

	sriov = &mdev->priv.sriov;
	if (WARN_ON(vf_id < 0 || vf_id >= sriov->num_vfs))
		return;

	vfs_ctx = &sriov->vfs_ctx[vf_id];
	blocking_notifier_chain_unregister(&vfs_ctx->notifier, nb);
}
EXPORT_SYMBOL(mlx5_sriov_blocking_notifier_unregister);

/**
 * mlx5_sriov_blocking_notifier_register - Register a VF notification
 * block chain.
 *
 * @mdev: The mlx5 core device.
 * @vf_id: The VF id.
 * @nb: The notifier block to be called upon the VF events.
 *
 * Returns 0 on success or an error code.
 */
int mlx5_sriov_blocking_notifier_register(struct mlx5_core_dev *mdev,
					  int vf_id,
					  struct notifier_block *nb)
{
	struct mlx5_vf_context *vfs_ctx;
	struct mlx5_core_sriov *sriov;

	sriov = &mdev->priv.sriov;
	if (vf_id < 0 || vf_id >= sriov->num_vfs)
		return -EINVAL;

	vfs_ctx = &sriov->vfs_ctx[vf_id];
	return blocking_notifier_chain_register(&vfs_ctx->notifier, nb);
}
EXPORT_SYMBOL(mlx5_sriov_blocking_notifier_register);
