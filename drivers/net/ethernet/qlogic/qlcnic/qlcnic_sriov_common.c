/*
 * QLogic qlcnic NIC Driver
 * Copyright (c) 2009-2013 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

#include "qlcnic_sriov.h"
#include "qlcnic.h"
#include "qlcnic_83xx_hw.h"
#include <linux/types.h>

static struct qlcnic_hardware_ops qlcnic_sriov_vf_hw_ops = {
	.read_crb			= qlcnic_83xx_read_crb,
	.write_crb			= qlcnic_83xx_write_crb,
	.read_reg			= qlcnic_83xx_rd_reg_indirect,
	.write_reg			= qlcnic_83xx_wrt_reg_indirect,
	.get_mac_address		= qlcnic_83xx_get_mac_address,
	.setup_intr			= qlcnic_83xx_setup_intr,
	.alloc_mbx_args			= qlcnic_83xx_alloc_mbx_args,
	.get_func_no			= qlcnic_83xx_get_func_no,
	.api_lock			= qlcnic_83xx_cam_lock,
	.api_unlock			= qlcnic_83xx_cam_unlock,
	.process_lb_rcv_ring_diag	= qlcnic_83xx_process_rcv_ring_diag,
	.create_rx_ctx			= qlcnic_83xx_create_rx_ctx,
	.create_tx_ctx			= qlcnic_83xx_create_tx_ctx,
	.setup_link_event		= qlcnic_83xx_setup_link_event,
	.get_nic_info			= qlcnic_83xx_get_nic_info,
	.get_pci_info			= qlcnic_83xx_get_pci_info,
	.set_nic_info			= qlcnic_83xx_set_nic_info,
	.change_macvlan			= qlcnic_83xx_sre_macaddr_change,
	.napi_enable			= qlcnic_83xx_napi_enable,
	.napi_disable			= qlcnic_83xx_napi_disable,
	.config_intr_coal		= qlcnic_83xx_config_intr_coal,
	.config_rss			= qlcnic_83xx_config_rss,
	.config_hw_lro			= qlcnic_83xx_config_hw_lro,
	.config_promisc_mode		= qlcnic_83xx_nic_set_promisc,
	.change_l2_filter		= qlcnic_83xx_change_l2_filter,
	.get_board_info			= qlcnic_83xx_get_port_info,
};

static struct qlcnic_nic_template qlcnic_sriov_vf_ops = {
	.config_bridged_mode	= qlcnic_config_bridged_mode,
	.config_led		= qlcnic_config_led,
	.cancel_idc_work        = qlcnic_83xx_idc_exit,
	.napi_add		= qlcnic_83xx_napi_add,
	.napi_del		= qlcnic_83xx_napi_del,
	.config_ipaddr		= qlcnic_83xx_config_ipaddr,
	.clear_legacy_intr	= qlcnic_83xx_clear_legacy_intr,
};

int qlcnic_sriov_init(struct qlcnic_adapter *adapter, int num_vfs)
{
	struct qlcnic_sriov *sriov;

	if (!qlcnic_sriov_enable_check(adapter))
		return -EIO;

	sriov  = kzalloc(sizeof(struct qlcnic_sriov), GFP_KERNEL);
	if (!sriov)
		return -ENOMEM;

	adapter->ahw->sriov = sriov;
	sriov->num_vfs = num_vfs;
	return 0;
}

void __qlcnic_sriov_cleanup(struct qlcnic_adapter *adapter)
{
	if (!qlcnic_sriov_enable_check(adapter))
		return;

	kfree(adapter->ahw->sriov);
}

static void qlcnic_sriov_vf_cleanup(struct qlcnic_adapter *adapter)
{
	__qlcnic_sriov_cleanup(adapter);
}

void qlcnic_sriov_cleanup(struct qlcnic_adapter *adapter)
{
	if (qlcnic_sriov_pf_check(adapter))
		qlcnic_sriov_pf_cleanup(adapter);

	if (qlcnic_sriov_vf_check(adapter))
		qlcnic_sriov_vf_cleanup(adapter);
}

static int qlcnic_sriov_setup_vf(struct qlcnic_adapter *adapter,
				 int pci_using_dac)
{
	int err;

	if (!qlcnic_use_msi_x && !!qlcnic_use_msi)
		dev_warn(&adapter->pdev->dev,
			 "83xx adapter do not support MSI interrupts\n");

	err = qlcnic_setup_intr(adapter, 1);
	if (err) {
		dev_err(&adapter->pdev->dev, "Failed to setup interrupt\n");
		goto err_out_disable_msi;
	}

	err = qlcnic_83xx_setup_mbx_intr(adapter);
	if (err)
		goto err_out_disable_msi;

	err = qlcnic_sriov_init(adapter, 1);
	if (err)
		goto err_out_disable_mbx_intr;

	err = qlcnic_setup_netdev(adapter, adapter->netdev, pci_using_dac);
	if (err)
		goto err_out_cleanup_sriov;

	pci_set_drvdata(adapter->pdev, adapter);
	dev_info(&adapter->pdev->dev, "%s: XGbE port initialized\n",
		 adapter->netdev->name);
	return 0;

err_out_cleanup_sriov:
	__qlcnic_sriov_cleanup(adapter);

err_out_disable_mbx_intr:
	qlcnic_83xx_free_mbx_intr(adapter);

err_out_disable_msi:
	qlcnic_teardown_intr(adapter);
	return err;
}

int qlcnic_sriov_vf_init(struct qlcnic_adapter *adapter, int pci_using_dac)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	spin_lock_init(&ahw->mbx_lock);
	set_bit(QLC_83XX_MBX_READY, &adapter->ahw->idc.status);
	ahw->msix_supported = 1;
	adapter->flags |= QLCNIC_TX_INTR_SHARED;

	if (qlcnic_sriov_setup_vf(adapter, pci_using_dac))
		return -EIO;

	if (qlcnic_read_mac_addr(adapter))
		dev_warn(&adapter->pdev->dev, "failed to read mac addr\n");

	set_bit(QLC_83XX_MODULE_LOADED, &adapter->ahw->idc.status);
	adapter->ahw->idc.delay = QLC_83XX_IDC_FW_POLL_DELAY;
	adapter->ahw->reset_context = 0;
	adapter->fw_fail_cnt = 0;
	clear_bit(__QLCNIC_RESETTING, &adapter->state);
	adapter->need_fw_reset = 0;
	return 0;
}

void qlcnic_sriov_vf_set_ops(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	ahw->op_mode = QLCNIC_SRIOV_VF_FUNC;
	dev_info(&adapter->pdev->dev,
		 "HAL Version: %d Non Privileged SRIOV function\n",
		 ahw->fw_hal_version);
	adapter->nic_ops = &qlcnic_sriov_vf_ops;
	set_bit(__QLCNIC_SRIOV_ENABLE, &adapter->state);
	return;
}

void qlcnic_sriov_vf_register_map(struct qlcnic_hardware_context *ahw)
{
	ahw->hw_ops		= &qlcnic_sriov_vf_hw_ops;
	ahw->reg_tbl		= (u32 *)qlcnic_83xx_reg_tbl;
	ahw->ext_reg_tbl	= (u32 *)qlcnic_83xx_ext_reg_tbl;
}
