// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <drm/drm_print.h>
#include <linux/pci.h>

#include "aie.h"
#include "aie4_msg_priv.h"
#include "aie4_pci.h"
#include "amdxdna_mailbox.h"
#include "amdxdna_mailbox_helper.h"
#include "amdxdna_pci_drv.h"

static int aie4_destroy_vfs(struct amdxdna_dev_hdl *ndev)
{
	DECLARE_AIE_MSG(aie4_msg_destroy_vfs, AIE4_MSG_OP_DESTROY_VFS);
	int ret;

	ret = aie_send_mgmt_msg_wait(&ndev->aie, &msg);
	if (ret)
		XDNA_ERR(ndev->aie.xdna, "destroy vfs op failed: %d", ret);

	return ret;
}

static int aie4_create_vfs(struct amdxdna_dev_hdl *ndev, int num_vfs)
{
	DECLARE_AIE_MSG(aie4_msg_create_vfs, AIE4_MSG_OP_CREATE_VFS);
	int ret;

	req.vf_cnt = num_vfs;
	ret = aie_send_mgmt_msg_wait(&ndev->aie, &msg);
	if (ret)
		XDNA_ERR(ndev->aie.xdna, "create vfs op failed: %d", ret);

	return ret;
}

int aie4_sriov_stop(struct amdxdna_dev_hdl *ndev)
{
	struct amdxdna_dev *xdna = ndev->aie.xdna;
	struct pci_dev *pdev = to_pci_dev(xdna->ddev.dev);
	int ret;

	if (!pci_num_vf(pdev))
		return 0;

	ret = pci_vfs_assigned(pdev);
	if (ret) {
		XDNA_ERR(xdna, "VFs are still assigned to VMs");
		return -EPERM;
	}

	pci_disable_sriov(pdev);
	return aie4_destroy_vfs(ndev);
}

static int aie4_sriov_start(struct amdxdna_dev_hdl *ndev, int num_vfs)
{
	struct amdxdna_dev *xdna = ndev->aie.xdna;
	struct pci_dev *pdev = to_pci_dev(xdna->ddev.dev);
	int ret;

	ret = aie4_create_vfs(ndev, num_vfs);
	if (ret)
		return ret;

	ret = pci_enable_sriov(pdev, num_vfs);
	if (ret) {
		XDNA_ERR(xdna, "configure VFs failed, ret: %d", ret);
		aie4_destroy_vfs(ndev);
		return ret;
	}

	return num_vfs;
}

int aie4_sriov_configure(struct amdxdna_dev *xdna, int num_vfs)
{
	struct amdxdna_dev_hdl *ndev = xdna->dev_handle;

	drm_WARN_ON(&xdna->ddev, !mutex_is_locked(&xdna->dev_lock));

	return (num_vfs) ? aie4_sriov_start(ndev, num_vfs) : aie4_sriov_stop(ndev);
}
