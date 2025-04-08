// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2025 Beijing WangXun Technology Co., Ltd. */

#include <linux/etherdevice.h>
#include <linux/pci.h>

#include "wx_type.h"
#include "wx_mbx.h"
#include "wx_sriov.h"

static void wx_vf_configuration(struct pci_dev *pdev, int event_mask)
{
	bool enable = !!WX_VF_ENABLE_CHECK(event_mask);
	struct wx *wx = pci_get_drvdata(pdev);
	u32 vfn = WX_VF_NUM_GET(event_mask);

	if (enable)
		eth_zero_addr(wx->vfinfo[vfn].vf_mac_addr);
}

static int wx_alloc_vf_macvlans(struct wx *wx, u8 num_vfs)
{
	struct vf_macvlans *mv_list;
	int num_vf_macvlans, i;

	/* Initialize list of VF macvlans */
	INIT_LIST_HEAD(&wx->vf_mvs.mvlist);

	num_vf_macvlans = wx->mac.num_rar_entries -
			  (WX_MAX_PF_MACVLANS + 1 + num_vfs);
	if (!num_vf_macvlans)
		return -EINVAL;

	mv_list = kcalloc(num_vf_macvlans, sizeof(struct vf_macvlans),
			  GFP_KERNEL);
	if (!mv_list)
		return -ENOMEM;

	for (i = 0; i < num_vf_macvlans; i++) {
		mv_list[i].vf = -1;
		mv_list[i].free = true;
		list_add(&mv_list[i].mvlist, &wx->vf_mvs.mvlist);
	}
	wx->mv_list = mv_list;

	return 0;
}

static void wx_sriov_clear_data(struct wx *wx)
{
	/* set num VFs to 0 to prevent access to vfinfo */
	wx->num_vfs = 0;

	/* free VF control structures */
	kfree(wx->vfinfo);
	wx->vfinfo = NULL;

	/* free macvlan list */
	kfree(wx->mv_list);
	wx->mv_list = NULL;

	/* set default pool back to 0 */
	wr32m(wx, WX_PSR_VM_CTL, WX_PSR_VM_CTL_POOL_MASK, 0);
	wx->ring_feature[RING_F_VMDQ].offset = 0;

	clear_bit(WX_FLAG_SRIOV_ENABLED, wx->flags);
	/* Disable VMDq flag so device will be set in NM mode */
	if (wx->ring_feature[RING_F_VMDQ].limit == 1)
		clear_bit(WX_FLAG_VMDQ_ENABLED, wx->flags);
}

static int __wx_enable_sriov(struct wx *wx, u8 num_vfs)
{
	int i, ret = 0;
	u32 value = 0;

	set_bit(WX_FLAG_SRIOV_ENABLED, wx->flags);
	wx_err(wx, "SR-IOV enabled with %d VFs\n", num_vfs);

	/* Enable VMDq flag so device will be set in VM mode */
	set_bit(WX_FLAG_VMDQ_ENABLED, wx->flags);
	if (!wx->ring_feature[RING_F_VMDQ].limit)
		wx->ring_feature[RING_F_VMDQ].limit = 1;
	wx->ring_feature[RING_F_VMDQ].offset = num_vfs;

	wx->vfinfo = kcalloc(num_vfs, sizeof(struct vf_data_storage),
			     GFP_KERNEL);
	if (!wx->vfinfo)
		return -ENOMEM;

	ret = wx_alloc_vf_macvlans(wx, num_vfs);
	if (ret)
		return ret;

	/* Initialize default switching mode VEB */
	wr32m(wx, WX_PSR_CTL, WX_PSR_CTL_SW_EN, WX_PSR_CTL_SW_EN);

	for (i = 0; i < num_vfs; i++) {
		/* enable spoof checking for all VFs */
		wx->vfinfo[i].spoofchk_enabled = true;
		wx->vfinfo[i].link_enable = true;
		/* untrust all VFs */
		wx->vfinfo[i].trusted = false;
		/* set the default xcast mode */
		wx->vfinfo[i].xcast_mode = WXVF_XCAST_MODE_NONE;
	}

	if (wx->mac.type == wx_mac_em) {
		value = WX_CFG_PORT_CTL_NUM_VT_8;
	} else {
		if (num_vfs < 32)
			value = WX_CFG_PORT_CTL_NUM_VT_32;
		else
			value = WX_CFG_PORT_CTL_NUM_VT_64;
	}
	wr32m(wx, WX_CFG_PORT_CTL,
	      WX_CFG_PORT_CTL_NUM_VT_MASK,
	      value);

	return ret;
}

static void wx_sriov_reinit(struct wx *wx)
{
	rtnl_lock();
	wx->setup_tc(wx->netdev, netdev_get_num_tc(wx->netdev));
	rtnl_unlock();
}

void wx_disable_sriov(struct wx *wx)
{
	if (!pci_vfs_assigned(wx->pdev))
		pci_disable_sriov(wx->pdev);
	else
		wx_err(wx, "Unloading driver while VFs are assigned.\n");

	/* clear flags and free allloced data */
	wx_sriov_clear_data(wx);
}
EXPORT_SYMBOL(wx_disable_sriov);

static int wx_pci_sriov_enable(struct pci_dev *dev,
			       int num_vfs)
{
	struct wx *wx = pci_get_drvdata(dev);
	int err = 0, i;

	err = __wx_enable_sriov(wx, num_vfs);
	if (err)
		return err;

	wx->num_vfs = num_vfs;
	for (i = 0; i < wx->num_vfs; i++)
		wx_vf_configuration(dev, (i | WX_VF_ENABLE));

	/* reset before enabling SRIOV to avoid mailbox issues */
	wx_sriov_reinit(wx);

	err = pci_enable_sriov(dev, num_vfs);
	if (err) {
		wx_err(wx, "Failed to enable PCI sriov: %d\n", err);
		goto err_out;
	}

	return num_vfs;
err_out:
	wx_sriov_clear_data(wx);
	return err;
}

static void wx_pci_sriov_disable(struct pci_dev *dev)
{
	struct wx *wx = pci_get_drvdata(dev);

	wx_disable_sriov(wx);
	wx_sriov_reinit(wx);
}

int wx_pci_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct wx *wx = pci_get_drvdata(pdev);
	int err;

	if (!num_vfs) {
		if (!pci_vfs_assigned(pdev)) {
			wx_pci_sriov_disable(pdev);
			return 0;
		}

		wx_err(wx, "can't free VFs because some are assigned to VMs.\n");
		return -EBUSY;
	}

	err = wx_pci_sriov_enable(pdev, num_vfs);
	if (err)
		return err;

	return num_vfs;
}
EXPORT_SYMBOL(wx_pci_sriov_configure);
