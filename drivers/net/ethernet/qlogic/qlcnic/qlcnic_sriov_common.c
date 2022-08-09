// SPDX-License-Identifier: GPL-2.0-only
/*
 * QLogic qlcnic NIC Driver
 * Copyright (c) 2009-2013 QLogic Corporation
 */

#include <linux/types.h>

#include "qlcnic_sriov.h"
#include "qlcnic.h"
#include "qlcnic_83xx_hw.h"

#define QLC_BC_COMMAND	0
#define QLC_BC_RESPONSE	1

#define QLC_MBOX_RESP_TIMEOUT		(10 * HZ)
#define QLC_MBOX_CH_FREE_TIMEOUT	(10 * HZ)

#define QLC_BC_MSG		0
#define QLC_BC_CFREE		1
#define QLC_BC_FLR		2
#define QLC_BC_HDR_SZ		16
#define QLC_BC_PAYLOAD_SZ	(1024 - QLC_BC_HDR_SZ)

#define QLC_DEFAULT_RCV_DESCRIPTORS_SRIOV_VF		2048
#define QLC_DEFAULT_JUMBO_RCV_DESCRIPTORS_SRIOV_VF	512

#define QLC_83XX_VF_RESET_FAIL_THRESH	8
#define QLC_BC_CMD_MAX_RETRY_CNT	5

static void qlcnic_sriov_handle_async_issue_cmd(struct work_struct *work);
static void qlcnic_sriov_vf_free_mac_list(struct qlcnic_adapter *);
static int qlcnic_sriov_alloc_bc_mbx_args(struct qlcnic_cmd_args *, u32);
static void qlcnic_sriov_vf_poll_dev_state(struct work_struct *);
static void qlcnic_sriov_vf_cancel_fw_work(struct qlcnic_adapter *);
static void qlcnic_sriov_cleanup_transaction(struct qlcnic_bc_trans *);
static int qlcnic_sriov_issue_cmd(struct qlcnic_adapter *,
				  struct qlcnic_cmd_args *);
static int qlcnic_sriov_channel_cfg_cmd(struct qlcnic_adapter *, u8);
static void qlcnic_sriov_process_bc_cmd(struct work_struct *);
static int qlcnic_sriov_vf_shutdown(struct pci_dev *);
static int qlcnic_sriov_vf_resume(struct qlcnic_adapter *);
static int qlcnic_sriov_async_issue_cmd(struct qlcnic_adapter *,
					struct qlcnic_cmd_args *);

static struct qlcnic_hardware_ops qlcnic_sriov_vf_hw_ops = {
	.read_crb			= qlcnic_83xx_read_crb,
	.write_crb			= qlcnic_83xx_write_crb,
	.read_reg			= qlcnic_83xx_rd_reg_indirect,
	.write_reg			= qlcnic_83xx_wrt_reg_indirect,
	.get_mac_address		= qlcnic_83xx_get_mac_address,
	.setup_intr			= qlcnic_83xx_setup_intr,
	.alloc_mbx_args			= qlcnic_83xx_alloc_mbx_args,
	.mbx_cmd			= qlcnic_sriov_issue_cmd,
	.get_func_no			= qlcnic_83xx_get_func_no,
	.api_lock			= qlcnic_83xx_cam_lock,
	.api_unlock			= qlcnic_83xx_cam_unlock,
	.process_lb_rcv_ring_diag	= qlcnic_83xx_process_rcv_ring_diag,
	.create_rx_ctx			= qlcnic_83xx_create_rx_ctx,
	.create_tx_ctx			= qlcnic_83xx_create_tx_ctx,
	.del_rx_ctx			= qlcnic_83xx_del_rx_ctx,
	.del_tx_ctx			= qlcnic_83xx_del_tx_ctx,
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
	.free_mac_list			= qlcnic_sriov_vf_free_mac_list,
	.enable_sds_intr		= qlcnic_83xx_enable_sds_intr,
	.disable_sds_intr		= qlcnic_83xx_disable_sds_intr,
	.encap_rx_offload               = qlcnic_83xx_encap_rx_offload,
	.encap_tx_offload               = qlcnic_83xx_encap_tx_offload,
};

static struct qlcnic_nic_template qlcnic_sriov_vf_ops = {
	.config_bridged_mode	= qlcnic_config_bridged_mode,
	.config_led		= qlcnic_config_led,
	.cancel_idc_work        = qlcnic_sriov_vf_cancel_fw_work,
	.napi_add		= qlcnic_83xx_napi_add,
	.napi_del		= qlcnic_83xx_napi_del,
	.shutdown		= qlcnic_sriov_vf_shutdown,
	.resume			= qlcnic_sriov_vf_resume,
	.config_ipaddr		= qlcnic_83xx_config_ipaddr,
	.clear_legacy_intr	= qlcnic_83xx_clear_legacy_intr,
};

static const struct qlcnic_mailbox_metadata qlcnic_sriov_bc_mbx_tbl[] = {
	{QLCNIC_BC_CMD_CHANNEL_INIT, 2, 2},
	{QLCNIC_BC_CMD_CHANNEL_TERM, 2, 2},
	{QLCNIC_BC_CMD_GET_ACL, 3, 14},
	{QLCNIC_BC_CMD_CFG_GUEST_VLAN, 2, 2},
};

static inline bool qlcnic_sriov_bc_msg_check(u32 val)
{
	return (val & (1 << QLC_BC_MSG)) ? true : false;
}

static inline bool qlcnic_sriov_channel_free_check(u32 val)
{
	return (val & (1 << QLC_BC_CFREE)) ? true : false;
}

static inline bool qlcnic_sriov_flr_check(u32 val)
{
	return (val & (1 << QLC_BC_FLR)) ? true : false;
}

static inline u8 qlcnic_sriov_target_func_id(u32 val)
{
	return (val >> 4) & 0xff;
}

static int qlcnic_sriov_virtid_fn(struct qlcnic_adapter *adapter, int vf_id)
{
	struct pci_dev *dev = adapter->pdev;
	int pos;
	u16 stride, offset;

	if (qlcnic_sriov_vf_check(adapter))
		return 0;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_SRIOV);
	if (!pos)
		return 0;
	pci_read_config_word(dev, pos + PCI_SRIOV_VF_OFFSET, &offset);
	pci_read_config_word(dev, pos + PCI_SRIOV_VF_STRIDE, &stride);

	return (dev->devfn + offset + stride * vf_id) & 0xff;
}

int qlcnic_sriov_init(struct qlcnic_adapter *adapter, int num_vfs)
{
	struct qlcnic_sriov *sriov;
	struct qlcnic_back_channel *bc;
	struct workqueue_struct *wq;
	struct qlcnic_vport *vp;
	struct qlcnic_vf_info *vf;
	int err, i;

	if (!qlcnic_sriov_enable_check(adapter))
		return -EIO;

	sriov  = kzalloc(sizeof(struct qlcnic_sriov), GFP_KERNEL);
	if (!sriov)
		return -ENOMEM;

	adapter->ahw->sriov = sriov;
	sriov->num_vfs = num_vfs;
	bc = &sriov->bc;
	sriov->vf_info = kcalloc(num_vfs, sizeof(struct qlcnic_vf_info),
				 GFP_KERNEL);
	if (!sriov->vf_info) {
		err = -ENOMEM;
		goto qlcnic_free_sriov;
	}

	wq = create_singlethread_workqueue("bc-trans");
	if (wq == NULL) {
		err = -ENOMEM;
		dev_err(&adapter->pdev->dev,
			"Cannot create bc-trans workqueue\n");
		goto qlcnic_free_vf_info;
	}

	bc->bc_trans_wq = wq;

	wq = create_singlethread_workqueue("async");
	if (wq == NULL) {
		err = -ENOMEM;
		dev_err(&adapter->pdev->dev, "Cannot create async workqueue\n");
		goto qlcnic_destroy_trans_wq;
	}

	bc->bc_async_wq =  wq;
	INIT_LIST_HEAD(&bc->async_cmd_list);
	INIT_WORK(&bc->vf_async_work, qlcnic_sriov_handle_async_issue_cmd);
	spin_lock_init(&bc->queue_lock);
	bc->adapter = adapter;

	for (i = 0; i < num_vfs; i++) {
		vf = &sriov->vf_info[i];
		vf->adapter = adapter;
		vf->pci_func = qlcnic_sriov_virtid_fn(adapter, i);
		mutex_init(&vf->send_cmd_lock);
		spin_lock_init(&vf->vlan_list_lock);
		INIT_LIST_HEAD(&vf->rcv_act.wait_list);
		INIT_LIST_HEAD(&vf->rcv_pend.wait_list);
		spin_lock_init(&vf->rcv_act.lock);
		spin_lock_init(&vf->rcv_pend.lock);
		init_completion(&vf->ch_free_cmpl);

		INIT_WORK(&vf->trans_work, qlcnic_sriov_process_bc_cmd);

		if (qlcnic_sriov_pf_check(adapter)) {
			vp = kzalloc(sizeof(struct qlcnic_vport), GFP_KERNEL);
			if (!vp) {
				err = -ENOMEM;
				goto qlcnic_destroy_async_wq;
			}
			sriov->vf_info[i].vp = vp;
			vp->vlan_mode = QLC_GUEST_VLAN_MODE;
			vp->max_tx_bw = MAX_BW;
			vp->min_tx_bw = MIN_BW;
			vp->spoofchk = false;
			eth_random_addr(vp->mac);
			dev_info(&adapter->pdev->dev,
				 "MAC Address %pM is configured for VF %d\n",
				 vp->mac, i);
		}
	}

	return 0;

qlcnic_destroy_async_wq:
	destroy_workqueue(bc->bc_async_wq);

qlcnic_destroy_trans_wq:
	destroy_workqueue(bc->bc_trans_wq);

qlcnic_free_vf_info:
	kfree(sriov->vf_info);

qlcnic_free_sriov:
	kfree(adapter->ahw->sriov);
	return err;
}

void qlcnic_sriov_cleanup_list(struct qlcnic_trans_list *t_list)
{
	struct qlcnic_bc_trans *trans;
	struct qlcnic_cmd_args cmd;
	unsigned long flags;

	spin_lock_irqsave(&t_list->lock, flags);

	while (!list_empty(&t_list->wait_list)) {
		trans = list_first_entry(&t_list->wait_list,
					 struct qlcnic_bc_trans, list);
		list_del(&trans->list);
		t_list->count--;
		cmd.req.arg = (u32 *)trans->req_pay;
		cmd.rsp.arg = (u32 *)trans->rsp_pay;
		qlcnic_free_mbx_args(&cmd);
		qlcnic_sriov_cleanup_transaction(trans);
	}

	spin_unlock_irqrestore(&t_list->lock, flags);
}

void __qlcnic_sriov_cleanup(struct qlcnic_adapter *adapter)
{
	struct qlcnic_sriov *sriov = adapter->ahw->sriov;
	struct qlcnic_back_channel *bc = &sriov->bc;
	struct qlcnic_vf_info *vf;
	int i;

	if (!qlcnic_sriov_enable_check(adapter))
		return;

	qlcnic_sriov_cleanup_async_list(bc);
	destroy_workqueue(bc->bc_async_wq);

	for (i = 0; i < sriov->num_vfs; i++) {
		vf = &sriov->vf_info[i];
		qlcnic_sriov_cleanup_list(&vf->rcv_pend);
		cancel_work_sync(&vf->trans_work);
		qlcnic_sriov_cleanup_list(&vf->rcv_act);
	}

	destroy_workqueue(bc->bc_trans_wq);

	for (i = 0; i < sriov->num_vfs; i++)
		kfree(sriov->vf_info[i].vp);

	kfree(sriov->vf_info);
	kfree(adapter->ahw->sriov);
}

static void qlcnic_sriov_vf_cleanup(struct qlcnic_adapter *adapter)
{
	qlcnic_sriov_channel_cfg_cmd(adapter, QLCNIC_BC_CMD_CHANNEL_TERM);
	qlcnic_sriov_cfg_bc_intr(adapter, 0);
	__qlcnic_sriov_cleanup(adapter);
}

void qlcnic_sriov_cleanup(struct qlcnic_adapter *adapter)
{
	if (!test_bit(__QLCNIC_SRIOV_ENABLE, &adapter->state))
		return;

	qlcnic_sriov_free_vlans(adapter);

	if (qlcnic_sriov_pf_check(adapter))
		qlcnic_sriov_pf_cleanup(adapter);

	if (qlcnic_sriov_vf_check(adapter))
		qlcnic_sriov_vf_cleanup(adapter);
}

static int qlcnic_sriov_post_bc_msg(struct qlcnic_adapter *adapter, u32 *hdr,
				    u32 *pay, u8 pci_func, u8 size)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlcnic_mailbox *mbx = ahw->mailbox;
	struct qlcnic_cmd_args cmd;
	unsigned long timeout;
	int err;

	memset(&cmd, 0, sizeof(struct qlcnic_cmd_args));
	cmd.hdr = hdr;
	cmd.pay = pay;
	cmd.pay_size = size;
	cmd.func_num = pci_func;
	cmd.op_type = QLC_83XX_MBX_POST_BC_OP;
	cmd.cmd_op = ((struct qlcnic_bc_hdr *)hdr)->cmd_op;

	err = mbx->ops->enqueue_cmd(adapter, &cmd, &timeout);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"%s: Mailbox not available, cmd_op=0x%x, cmd_type=0x%x, pci_func=0x%x, op_mode=0x%x\n",
			__func__, cmd.cmd_op, cmd.type, ahw->pci_func,
			ahw->op_mode);
		return err;
	}

	if (!wait_for_completion_timeout(&cmd.completion, timeout)) {
		dev_err(&adapter->pdev->dev,
			"%s: Mailbox command timed out, cmd_op=0x%x, cmd_type=0x%x, pci_func=0x%x, op_mode=0x%x\n",
			__func__, cmd.cmd_op, cmd.type, ahw->pci_func,
			ahw->op_mode);
		flush_workqueue(mbx->work_q);
	}

	return cmd.rsp_opcode;
}

static void qlcnic_sriov_vf_cfg_buff_desc(struct qlcnic_adapter *adapter)
{
	adapter->num_rxd = QLC_DEFAULT_RCV_DESCRIPTORS_SRIOV_VF;
	adapter->max_rxd = MAX_RCV_DESCRIPTORS_10G;
	adapter->num_jumbo_rxd = QLC_DEFAULT_JUMBO_RCV_DESCRIPTORS_SRIOV_VF;
	adapter->max_jumbo_rxd = MAX_JUMBO_RCV_DESCRIPTORS_10G;
	adapter->num_txd = MAX_CMD_DESCRIPTORS;
	adapter->max_rds_rings = MAX_RDS_RINGS;
}

int qlcnic_sriov_get_vf_vport_info(struct qlcnic_adapter *adapter,
				   struct qlcnic_info *npar_info, u16 vport_id)
{
	struct device *dev = &adapter->pdev->dev;
	struct qlcnic_cmd_args cmd;
	int err;
	u32 status;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_GET_NIC_INFO);
	if (err)
		return err;

	cmd.req.arg[1] = vport_id << 16 | 0x1;
	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"Failed to get vport info, err=%d\n", err);
		qlcnic_free_mbx_args(&cmd);
		return err;
	}

	status = cmd.rsp.arg[2] & 0xffff;
	if (status & BIT_0)
		npar_info->min_tx_bw = MSW(cmd.rsp.arg[2]);
	if (status & BIT_1)
		npar_info->max_tx_bw = LSW(cmd.rsp.arg[3]);
	if (status & BIT_2)
		npar_info->max_tx_ques = MSW(cmd.rsp.arg[3]);
	if (status & BIT_3)
		npar_info->max_tx_mac_filters = LSW(cmd.rsp.arg[4]);
	if (status & BIT_4)
		npar_info->max_rx_mcast_mac_filters = MSW(cmd.rsp.arg[4]);
	if (status & BIT_5)
		npar_info->max_rx_ucast_mac_filters = LSW(cmd.rsp.arg[5]);
	if (status & BIT_6)
		npar_info->max_rx_ip_addr = MSW(cmd.rsp.arg[5]);
	if (status & BIT_7)
		npar_info->max_rx_lro_flow = LSW(cmd.rsp.arg[6]);
	if (status & BIT_8)
		npar_info->max_rx_status_rings = MSW(cmd.rsp.arg[6]);
	if (status & BIT_9)
		npar_info->max_rx_buf_rings = LSW(cmd.rsp.arg[7]);

	npar_info->max_rx_ques = MSW(cmd.rsp.arg[7]);
	npar_info->max_tx_vlan_keys = LSW(cmd.rsp.arg[8]);
	npar_info->max_local_ipv6_addrs = MSW(cmd.rsp.arg[8]);
	npar_info->max_remote_ipv6_addrs = LSW(cmd.rsp.arg[9]);

	dev_info(dev, "\n\tmin_tx_bw: %d, max_tx_bw: %d max_tx_ques: %d,\n"
		 "\tmax_tx_mac_filters: %d max_rx_mcast_mac_filters: %d,\n"
		 "\tmax_rx_ucast_mac_filters: 0x%x, max_rx_ip_addr: %d,\n"
		 "\tmax_rx_lro_flow: %d max_rx_status_rings: %d,\n"
		 "\tmax_rx_buf_rings: %d, max_rx_ques: %d, max_tx_vlan_keys %d\n"
		 "\tlocal_ipv6_addr: %d, remote_ipv6_addr: %d\n",
		 npar_info->min_tx_bw, npar_info->max_tx_bw,
		 npar_info->max_tx_ques, npar_info->max_tx_mac_filters,
		 npar_info->max_rx_mcast_mac_filters,
		 npar_info->max_rx_ucast_mac_filters, npar_info->max_rx_ip_addr,
		 npar_info->max_rx_lro_flow, npar_info->max_rx_status_rings,
		 npar_info->max_rx_buf_rings, npar_info->max_rx_ques,
		 npar_info->max_tx_vlan_keys, npar_info->max_local_ipv6_addrs,
		 npar_info->max_remote_ipv6_addrs);

	qlcnic_free_mbx_args(&cmd);
	return err;
}

static int qlcnic_sriov_set_pvid_mode(struct qlcnic_adapter *adapter,
				      struct qlcnic_cmd_args *cmd)
{
	adapter->rx_pvid = MSW(cmd->rsp.arg[1]) & 0xffff;
	adapter->flags &= ~QLCNIC_TAGGING_ENABLED;
	return 0;
}

static int qlcnic_sriov_set_guest_vlan_mode(struct qlcnic_adapter *adapter,
					    struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_sriov *sriov = adapter->ahw->sriov;
	int i, num_vlans, ret;
	u16 *vlans;

	if (sriov->allowed_vlans)
		return 0;

	sriov->any_vlan = cmd->rsp.arg[2] & 0xf;
	sriov->num_allowed_vlans = cmd->rsp.arg[2] >> 16;
	dev_info(&adapter->pdev->dev, "Number of allowed Guest VLANs = %d\n",
		 sriov->num_allowed_vlans);

	ret = qlcnic_sriov_alloc_vlans(adapter);
	if (ret)
		return ret;

	if (!sriov->any_vlan)
		return 0;

	num_vlans = sriov->num_allowed_vlans;
	sriov->allowed_vlans = kcalloc(num_vlans, sizeof(u16), GFP_KERNEL);
	if (!sriov->allowed_vlans)
		return -ENOMEM;

	vlans = (u16 *)&cmd->rsp.arg[3];
	for (i = 0; i < num_vlans; i++)
		sriov->allowed_vlans[i] = vlans[i];

	return 0;
}

static int qlcnic_sriov_get_vf_acl(struct qlcnic_adapter *adapter)
{
	struct qlcnic_sriov *sriov = adapter->ahw->sriov;
	struct qlcnic_cmd_args cmd;
	int ret = 0;

	memset(&cmd, 0, sizeof(cmd));
	ret = qlcnic_sriov_alloc_bc_mbx_args(&cmd, QLCNIC_BC_CMD_GET_ACL);
	if (ret)
		return ret;

	ret = qlcnic_issue_cmd(adapter, &cmd);
	if (ret) {
		dev_err(&adapter->pdev->dev, "Failed to get ACL, err=%d\n",
			ret);
	} else {
		sriov->vlan_mode = cmd.rsp.arg[1] & 0x3;
		switch (sriov->vlan_mode) {
		case QLC_GUEST_VLAN_MODE:
			ret = qlcnic_sriov_set_guest_vlan_mode(adapter, &cmd);
			break;
		case QLC_PVID_MODE:
			ret = qlcnic_sriov_set_pvid_mode(adapter, &cmd);
			break;
		}
	}

	qlcnic_free_mbx_args(&cmd);
	return ret;
}

static int qlcnic_sriov_vf_init_driver(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlcnic_info nic_info;
	int err;

	err = qlcnic_sriov_get_vf_vport_info(adapter, &nic_info, 0);
	if (err)
		return err;

	ahw->max_mc_count = nic_info.max_rx_mcast_mac_filters;

	err = qlcnic_get_nic_info(adapter, &nic_info, ahw->pci_func);
	if (err)
		return -EIO;

	if (qlcnic_83xx_get_port_info(adapter))
		return -EIO;

	qlcnic_sriov_vf_cfg_buff_desc(adapter);
	adapter->flags |= QLCNIC_ADAPTER_INITIALIZED;
	dev_info(&adapter->pdev->dev, "HAL Version: %d\n",
		 adapter->ahw->fw_hal_version);

	ahw->physical_port = (u8) nic_info.phys_port;
	ahw->switch_mode = nic_info.switch_mode;
	ahw->max_mtu = nic_info.max_mtu;
	ahw->op_mode = nic_info.op_mode;
	ahw->capabilities = nic_info.capabilities;
	return 0;
}

static int qlcnic_sriov_setup_vf(struct qlcnic_adapter *adapter)
{
	int err;

	adapter->flags |= QLCNIC_VLAN_FILTERING;
	adapter->ahw->total_nic_func = 1;
	INIT_LIST_HEAD(&adapter->vf_mc_list);
	if (!qlcnic_use_msi_x && !!qlcnic_use_msi)
		dev_warn(&adapter->pdev->dev,
			 "Device does not support MSI interrupts\n");

	/* compute and set default and max tx/sds rings */
	qlcnic_set_tx_ring_count(adapter, QLCNIC_SINGLE_RING);
	qlcnic_set_sds_ring_count(adapter, QLCNIC_SINGLE_RING);

	err = qlcnic_setup_intr(adapter);
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

	err = qlcnic_sriov_cfg_bc_intr(adapter, 1);
	if (err)
		goto err_out_cleanup_sriov;

	err = qlcnic_sriov_channel_cfg_cmd(adapter, QLCNIC_BC_CMD_CHANNEL_INIT);
	if (err)
		goto err_out_disable_bc_intr;

	err = qlcnic_sriov_vf_init_driver(adapter);
	if (err)
		goto err_out_send_channel_term;

	err = qlcnic_sriov_get_vf_acl(adapter);
	if (err)
		goto err_out_send_channel_term;

	err = qlcnic_setup_netdev(adapter, adapter->netdev);
	if (err)
		goto err_out_send_channel_term;

	pci_set_drvdata(adapter->pdev, adapter);
	dev_info(&adapter->pdev->dev, "%s: XGbE port initialized\n",
		 adapter->netdev->name);

	qlcnic_schedule_work(adapter, qlcnic_sriov_vf_poll_dev_state,
			     adapter->ahw->idc.delay);
	return 0;

err_out_send_channel_term:
	qlcnic_sriov_channel_cfg_cmd(adapter, QLCNIC_BC_CMD_CHANNEL_TERM);

err_out_disable_bc_intr:
	qlcnic_sriov_cfg_bc_intr(adapter, 0);

err_out_cleanup_sriov:
	__qlcnic_sriov_cleanup(adapter);

err_out_disable_mbx_intr:
	qlcnic_83xx_free_mbx_intr(adapter);

err_out_disable_msi:
	qlcnic_teardown_intr(adapter);
	return err;
}

static int qlcnic_sriov_check_dev_ready(struct qlcnic_adapter *adapter)
{
	u32 state;

	do {
		msleep(20);
		if (++adapter->fw_fail_cnt > QLC_BC_CMD_MAX_RETRY_CNT)
			return -EIO;
		state = QLCRDX(adapter->ahw, QLC_83XX_IDC_DEV_STATE);
	} while (state != QLC_83XX_IDC_DEV_READY);

	return 0;
}

int qlcnic_sriov_vf_init(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	int err;

	set_bit(QLC_83XX_MODULE_LOADED, &ahw->idc.status);
	ahw->idc.delay = QLC_83XX_IDC_FW_POLL_DELAY;
	ahw->reset_context = 0;
	adapter->fw_fail_cnt = 0;
	ahw->msix_supported = 1;
	adapter->need_fw_reset = 0;
	adapter->flags |= QLCNIC_TX_INTR_SHARED;

	err = qlcnic_sriov_check_dev_ready(adapter);
	if (err)
		return err;

	err = qlcnic_sriov_setup_vf(adapter);
	if (err)
		return err;

	if (qlcnic_read_mac_addr(adapter))
		dev_warn(&adapter->pdev->dev, "failed to read mac addr\n");

	INIT_DELAYED_WORK(&adapter->idc_aen_work, qlcnic_83xx_idc_aen_work);

	clear_bit(__QLCNIC_RESETTING, &adapter->state);
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

static u32 qlcnic_sriov_get_bc_paysize(u32 real_pay_size, u8 curr_frag)
{
	u32 pay_size;

	pay_size = real_pay_size / ((curr_frag + 1) * QLC_BC_PAYLOAD_SZ);

	if (pay_size)
		pay_size = QLC_BC_PAYLOAD_SZ;
	else
		pay_size = real_pay_size % QLC_BC_PAYLOAD_SZ;

	return pay_size;
}

int qlcnic_sriov_func_to_index(struct qlcnic_adapter *adapter, u8 pci_func)
{
	struct qlcnic_vf_info *vf_info = adapter->ahw->sriov->vf_info;
	u8 i;

	if (qlcnic_sriov_vf_check(adapter))
		return 0;

	for (i = 0; i < adapter->ahw->sriov->num_vfs; i++) {
		if (vf_info[i].pci_func == pci_func)
			return i;
	}

	return -EINVAL;
}

static inline int qlcnic_sriov_alloc_bc_trans(struct qlcnic_bc_trans **trans)
{
	*trans = kzalloc(sizeof(struct qlcnic_bc_trans), GFP_ATOMIC);
	if (!*trans)
		return -ENOMEM;

	init_completion(&(*trans)->resp_cmpl);
	return 0;
}

static inline int qlcnic_sriov_alloc_bc_msg(struct qlcnic_bc_hdr **hdr,
					    u32 size)
{
	*hdr = kcalloc(size, sizeof(struct qlcnic_bc_hdr), GFP_ATOMIC);
	if (!*hdr)
		return -ENOMEM;

	return 0;
}

static int qlcnic_sriov_alloc_bc_mbx_args(struct qlcnic_cmd_args *mbx, u32 type)
{
	const struct qlcnic_mailbox_metadata *mbx_tbl;
	int i, size;

	mbx_tbl = qlcnic_sriov_bc_mbx_tbl;
	size = ARRAY_SIZE(qlcnic_sriov_bc_mbx_tbl);

	for (i = 0; i < size; i++) {
		if (type == mbx_tbl[i].cmd) {
			mbx->op_type = QLC_BC_CMD;
			mbx->req.num = mbx_tbl[i].in_args;
			mbx->rsp.num = mbx_tbl[i].out_args;
			mbx->req.arg = kcalloc(mbx->req.num, sizeof(u32),
					       GFP_ATOMIC);
			if (!mbx->req.arg)
				return -ENOMEM;
			mbx->rsp.arg = kcalloc(mbx->rsp.num, sizeof(u32),
					       GFP_ATOMIC);
			if (!mbx->rsp.arg) {
				kfree(mbx->req.arg);
				mbx->req.arg = NULL;
				return -ENOMEM;
			}
			mbx->req.arg[0] = (type | (mbx->req.num << 16) |
					   (3 << 29));
			mbx->rsp.arg[0] = (type & 0xffff) | mbx->rsp.num << 16;
			return 0;
		}
	}
	return -EINVAL;
}

static int qlcnic_sriov_prepare_bc_hdr(struct qlcnic_bc_trans *trans,
				       struct qlcnic_cmd_args *cmd,
				       u16 seq, u8 msg_type)
{
	struct qlcnic_bc_hdr *hdr;
	int i;
	u32 num_regs, bc_pay_sz;
	u16 remainder;
	u8 cmd_op, num_frags, t_num_frags;

	bc_pay_sz = QLC_BC_PAYLOAD_SZ;
	if (msg_type == QLC_BC_COMMAND) {
		trans->req_pay = (struct qlcnic_bc_payload *)cmd->req.arg;
		trans->rsp_pay = (struct qlcnic_bc_payload *)cmd->rsp.arg;
		num_regs = cmd->req.num;
		trans->req_pay_size = (num_regs * 4);
		num_regs = cmd->rsp.num;
		trans->rsp_pay_size = (num_regs * 4);
		cmd_op = cmd->req.arg[0] & 0xff;
		remainder = (trans->req_pay_size) % (bc_pay_sz);
		num_frags = (trans->req_pay_size) / (bc_pay_sz);
		if (remainder)
			num_frags++;
		t_num_frags = num_frags;
		if (qlcnic_sriov_alloc_bc_msg(&trans->req_hdr, num_frags))
			return -ENOMEM;
		remainder = (trans->rsp_pay_size) % (bc_pay_sz);
		num_frags = (trans->rsp_pay_size) / (bc_pay_sz);
		if (remainder)
			num_frags++;
		if (qlcnic_sriov_alloc_bc_msg(&trans->rsp_hdr, num_frags))
			return -ENOMEM;
		num_frags  = t_num_frags;
		hdr = trans->req_hdr;
	}  else {
		cmd->req.arg = (u32 *)trans->req_pay;
		cmd->rsp.arg = (u32 *)trans->rsp_pay;
		cmd_op = cmd->req.arg[0] & 0xff;
		cmd->cmd_op = cmd_op;
		remainder = (trans->rsp_pay_size) % (bc_pay_sz);
		num_frags = (trans->rsp_pay_size) / (bc_pay_sz);
		if (remainder)
			num_frags++;
		cmd->req.num = trans->req_pay_size / 4;
		cmd->rsp.num = trans->rsp_pay_size / 4;
		hdr = trans->rsp_hdr;
		cmd->op_type = trans->req_hdr->op_type;
	}

	trans->trans_id = seq;
	trans->cmd_id = cmd_op;
	for (i = 0; i < num_frags; i++) {
		hdr[i].version = 2;
		hdr[i].msg_type = msg_type;
		hdr[i].op_type = cmd->op_type;
		hdr[i].num_cmds = 1;
		hdr[i].num_frags = num_frags;
		hdr[i].frag_num = i + 1;
		hdr[i].cmd_op = cmd_op;
		hdr[i].seq_id = seq;
	}
	return 0;
}

static void qlcnic_sriov_cleanup_transaction(struct qlcnic_bc_trans *trans)
{
	if (!trans)
		return;
	kfree(trans->req_hdr);
	kfree(trans->rsp_hdr);
	kfree(trans);
}

static int qlcnic_sriov_clear_trans(struct qlcnic_vf_info *vf,
				    struct qlcnic_bc_trans *trans, u8 type)
{
	struct qlcnic_trans_list *t_list;
	unsigned long flags;
	int ret = 0;

	if (type == QLC_BC_RESPONSE) {
		t_list = &vf->rcv_act;
		spin_lock_irqsave(&t_list->lock, flags);
		t_list->count--;
		list_del(&trans->list);
		if (t_list->count > 0)
			ret = 1;
		spin_unlock_irqrestore(&t_list->lock, flags);
	}
	if (type == QLC_BC_COMMAND) {
		while (test_and_set_bit(QLC_BC_VF_SEND, &vf->state))
			msleep(100);
		vf->send_cmd = NULL;
		clear_bit(QLC_BC_VF_SEND, &vf->state);
	}
	return ret;
}

static void qlcnic_sriov_schedule_bc_cmd(struct qlcnic_sriov *sriov,
					 struct qlcnic_vf_info *vf,
					 work_func_t func)
{
	if (test_bit(QLC_BC_VF_FLR, &vf->state) ||
	    vf->adapter->need_fw_reset)
		return;

	queue_work(sriov->bc.bc_trans_wq, &vf->trans_work);
}

static inline void qlcnic_sriov_wait_for_resp(struct qlcnic_bc_trans *trans)
{
	struct completion *cmpl = &trans->resp_cmpl;

	if (wait_for_completion_timeout(cmpl, QLC_MBOX_RESP_TIMEOUT))
		trans->trans_state = QLC_END;
	else
		trans->trans_state = QLC_ABORT;

	return;
}

static void qlcnic_sriov_handle_multi_frags(struct qlcnic_bc_trans *trans,
					    u8 type)
{
	if (type == QLC_BC_RESPONSE) {
		trans->curr_rsp_frag++;
		if (trans->curr_rsp_frag < trans->rsp_hdr->num_frags)
			trans->trans_state = QLC_INIT;
		else
			trans->trans_state = QLC_END;
	} else {
		trans->curr_req_frag++;
		if (trans->curr_req_frag < trans->req_hdr->num_frags)
			trans->trans_state = QLC_INIT;
		else
			trans->trans_state = QLC_WAIT_FOR_RESP;
	}
}

static void qlcnic_sriov_wait_for_channel_free(struct qlcnic_bc_trans *trans,
					       u8 type)
{
	struct qlcnic_vf_info *vf = trans->vf;
	struct completion *cmpl = &vf->ch_free_cmpl;

	if (!wait_for_completion_timeout(cmpl, QLC_MBOX_CH_FREE_TIMEOUT)) {
		trans->trans_state = QLC_ABORT;
		return;
	}

	clear_bit(QLC_BC_VF_CHANNEL, &vf->state);
	qlcnic_sriov_handle_multi_frags(trans, type);
}

static void qlcnic_sriov_pull_bc_msg(struct qlcnic_adapter *adapter,
				     u32 *hdr, u32 *pay, u32 size)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	u8 i, max = 2, hdr_size, j;

	hdr_size = (sizeof(struct qlcnic_bc_hdr) / sizeof(u32));
	max = (size / sizeof(u32)) + hdr_size;

	for (i = 2, j = 0; j < hdr_size; i++, j++)
		*(hdr++) = readl(QLCNIC_MBX_FW(ahw, i));
	for (; j < max; i++, j++)
		*(pay++) = readl(QLCNIC_MBX_FW(ahw, i));
}

static int __qlcnic_sriov_issue_bc_post(struct qlcnic_vf_info *vf)
{
	int ret = -EBUSY;
	u32 timeout = 10000;

	do {
		if (!test_and_set_bit(QLC_BC_VF_CHANNEL, &vf->state)) {
			ret = 0;
			break;
		}
		mdelay(1);
	} while (--timeout);

	return ret;
}

static int qlcnic_sriov_issue_bc_post(struct qlcnic_bc_trans *trans, u8 type)
{
	struct qlcnic_vf_info *vf = trans->vf;
	u32 pay_size;
	u32 *hdr, *pay;
	int ret;
	u8 pci_func = trans->func_id;

	if (__qlcnic_sriov_issue_bc_post(vf))
		return -EBUSY;

	if (type == QLC_BC_COMMAND) {
		hdr = (u32 *)(trans->req_hdr + trans->curr_req_frag);
		pay = (u32 *)(trans->req_pay + trans->curr_req_frag);
		pay_size = qlcnic_sriov_get_bc_paysize(trans->req_pay_size,
						       trans->curr_req_frag);
		pay_size = (pay_size / sizeof(u32));
	} else {
		hdr = (u32 *)(trans->rsp_hdr + trans->curr_rsp_frag);
		pay = (u32 *)(trans->rsp_pay + trans->curr_rsp_frag);
		pay_size = qlcnic_sriov_get_bc_paysize(trans->rsp_pay_size,
						       trans->curr_rsp_frag);
		pay_size = (pay_size / sizeof(u32));
	}

	ret = qlcnic_sriov_post_bc_msg(vf->adapter, hdr, pay,
				       pci_func, pay_size);
	return ret;
}

static int __qlcnic_sriov_send_bc_msg(struct qlcnic_bc_trans *trans,
				      struct qlcnic_vf_info *vf, u8 type)
{
	bool flag = true;
	int err = -EIO;

	while (flag) {
		if (test_bit(QLC_BC_VF_FLR, &vf->state) ||
		    vf->adapter->need_fw_reset)
			trans->trans_state = QLC_ABORT;

		switch (trans->trans_state) {
		case QLC_INIT:
			trans->trans_state = QLC_WAIT_FOR_CHANNEL_FREE;
			if (qlcnic_sriov_issue_bc_post(trans, type))
				trans->trans_state = QLC_ABORT;
			break;
		case QLC_WAIT_FOR_CHANNEL_FREE:
			qlcnic_sriov_wait_for_channel_free(trans, type);
			break;
		case QLC_WAIT_FOR_RESP:
			qlcnic_sriov_wait_for_resp(trans);
			break;
		case QLC_END:
			err = 0;
			flag = false;
			break;
		case QLC_ABORT:
			err = -EIO;
			flag = false;
			clear_bit(QLC_BC_VF_CHANNEL, &vf->state);
			break;
		default:
			err = -EIO;
			flag = false;
		}
	}
	return err;
}

static int qlcnic_sriov_send_bc_cmd(struct qlcnic_adapter *adapter,
				    struct qlcnic_bc_trans *trans, int pci_func)
{
	struct qlcnic_vf_info *vf;
	int err, index = qlcnic_sriov_func_to_index(adapter, pci_func);

	if (index < 0)
		return -EIO;

	vf = &adapter->ahw->sriov->vf_info[index];
	trans->vf = vf;
	trans->func_id = pci_func;

	if (!test_bit(QLC_BC_VF_STATE, &vf->state)) {
		if (qlcnic_sriov_pf_check(adapter))
			return -EIO;
		if (qlcnic_sriov_vf_check(adapter) &&
		    trans->cmd_id != QLCNIC_BC_CMD_CHANNEL_INIT)
			return -EIO;
	}

	mutex_lock(&vf->send_cmd_lock);
	vf->send_cmd = trans;
	err = __qlcnic_sriov_send_bc_msg(trans, vf, QLC_BC_COMMAND);
	qlcnic_sriov_clear_trans(vf, trans, QLC_BC_COMMAND);
	mutex_unlock(&vf->send_cmd_lock);
	return err;
}

static void __qlcnic_sriov_process_bc_cmd(struct qlcnic_adapter *adapter,
					  struct qlcnic_bc_trans *trans,
					  struct qlcnic_cmd_args *cmd)
{
#ifdef CONFIG_QLCNIC_SRIOV
	if (qlcnic_sriov_pf_check(adapter)) {
		qlcnic_sriov_pf_process_bc_cmd(adapter, trans, cmd);
		return;
	}
#endif
	cmd->rsp.arg[0] |= (0x9 << 25);
	return;
}

static void qlcnic_sriov_process_bc_cmd(struct work_struct *work)
{
	struct qlcnic_vf_info *vf = container_of(work, struct qlcnic_vf_info,
						 trans_work);
	struct qlcnic_bc_trans *trans = NULL;
	struct qlcnic_adapter *adapter  = vf->adapter;
	struct qlcnic_cmd_args cmd;
	u8 req;

	if (adapter->need_fw_reset)
		return;

	if (test_bit(QLC_BC_VF_FLR, &vf->state))
		return;

	memset(&cmd, 0, sizeof(struct qlcnic_cmd_args));
	trans = list_first_entry(&vf->rcv_act.wait_list,
				 struct qlcnic_bc_trans, list);
	adapter = vf->adapter;

	if (qlcnic_sriov_prepare_bc_hdr(trans, &cmd, trans->req_hdr->seq_id,
					QLC_BC_RESPONSE))
		goto cleanup_trans;

	__qlcnic_sriov_process_bc_cmd(adapter, trans, &cmd);
	trans->trans_state = QLC_INIT;
	__qlcnic_sriov_send_bc_msg(trans, vf, QLC_BC_RESPONSE);

cleanup_trans:
	qlcnic_free_mbx_args(&cmd);
	req = qlcnic_sriov_clear_trans(vf, trans, QLC_BC_RESPONSE);
	qlcnic_sriov_cleanup_transaction(trans);
	if (req)
		qlcnic_sriov_schedule_bc_cmd(adapter->ahw->sriov, vf,
					     qlcnic_sriov_process_bc_cmd);
}

static void qlcnic_sriov_handle_bc_resp(struct qlcnic_bc_hdr *hdr,
					struct qlcnic_vf_info *vf)
{
	struct qlcnic_bc_trans *trans;
	u32 pay_size;

	if (test_and_set_bit(QLC_BC_VF_SEND, &vf->state))
		return;

	trans = vf->send_cmd;

	if (trans == NULL)
		goto clear_send;

	if (trans->trans_id != hdr->seq_id)
		goto clear_send;

	pay_size = qlcnic_sriov_get_bc_paysize(trans->rsp_pay_size,
					       trans->curr_rsp_frag);
	qlcnic_sriov_pull_bc_msg(vf->adapter,
				 (u32 *)(trans->rsp_hdr + trans->curr_rsp_frag),
				 (u32 *)(trans->rsp_pay + trans->curr_rsp_frag),
				 pay_size);
	if (++trans->curr_rsp_frag < trans->rsp_hdr->num_frags)
		goto clear_send;

	complete(&trans->resp_cmpl);

clear_send:
	clear_bit(QLC_BC_VF_SEND, &vf->state);
}

int __qlcnic_sriov_add_act_list(struct qlcnic_sriov *sriov,
				struct qlcnic_vf_info *vf,
				struct qlcnic_bc_trans *trans)
{
	struct qlcnic_trans_list *t_list = &vf->rcv_act;

	t_list->count++;
	list_add_tail(&trans->list, &t_list->wait_list);
	if (t_list->count == 1)
		qlcnic_sriov_schedule_bc_cmd(sriov, vf,
					     qlcnic_sriov_process_bc_cmd);
	return 0;
}

static int qlcnic_sriov_add_act_list(struct qlcnic_sriov *sriov,
				     struct qlcnic_vf_info *vf,
				     struct qlcnic_bc_trans *trans)
{
	struct qlcnic_trans_list *t_list = &vf->rcv_act;

	spin_lock(&t_list->lock);

	__qlcnic_sriov_add_act_list(sriov, vf, trans);

	spin_unlock(&t_list->lock);
	return 0;
}

static void qlcnic_sriov_handle_pending_trans(struct qlcnic_sriov *sriov,
					      struct qlcnic_vf_info *vf,
					      struct qlcnic_bc_hdr *hdr)
{
	struct qlcnic_bc_trans *trans = NULL;
	struct list_head *node;
	u32 pay_size, curr_frag;
	u8 found = 0, active = 0;

	spin_lock(&vf->rcv_pend.lock);
	if (vf->rcv_pend.count > 0) {
		list_for_each(node, &vf->rcv_pend.wait_list) {
			trans = list_entry(node, struct qlcnic_bc_trans, list);
			if (trans->trans_id == hdr->seq_id) {
				found = 1;
				break;
			}
		}
	}

	if (found) {
		curr_frag = trans->curr_req_frag;
		pay_size = qlcnic_sriov_get_bc_paysize(trans->req_pay_size,
						       curr_frag);
		qlcnic_sriov_pull_bc_msg(vf->adapter,
					 (u32 *)(trans->req_hdr + curr_frag),
					 (u32 *)(trans->req_pay + curr_frag),
					 pay_size);
		trans->curr_req_frag++;
		if (trans->curr_req_frag >= hdr->num_frags) {
			vf->rcv_pend.count--;
			list_del(&trans->list);
			active = 1;
		}
	}
	spin_unlock(&vf->rcv_pend.lock);

	if (active)
		if (qlcnic_sriov_add_act_list(sriov, vf, trans))
			qlcnic_sriov_cleanup_transaction(trans);

	return;
}

static void qlcnic_sriov_handle_bc_cmd(struct qlcnic_sriov *sriov,
				       struct qlcnic_bc_hdr *hdr,
				       struct qlcnic_vf_info *vf)
{
	struct qlcnic_bc_trans *trans;
	struct qlcnic_adapter *adapter = vf->adapter;
	struct qlcnic_cmd_args cmd;
	u32 pay_size;
	int err;
	u8 cmd_op;

	if (adapter->need_fw_reset)
		return;

	if (!test_bit(QLC_BC_VF_STATE, &vf->state) &&
	    hdr->op_type != QLC_BC_CMD &&
	    hdr->cmd_op != QLCNIC_BC_CMD_CHANNEL_INIT)
		return;

	if (hdr->frag_num > 1) {
		qlcnic_sriov_handle_pending_trans(sriov, vf, hdr);
		return;
	}

	memset(&cmd, 0, sizeof(struct qlcnic_cmd_args));
	cmd_op = hdr->cmd_op;
	if (qlcnic_sriov_alloc_bc_trans(&trans))
		return;

	if (hdr->op_type == QLC_BC_CMD)
		err = qlcnic_sriov_alloc_bc_mbx_args(&cmd, cmd_op);
	else
		err = qlcnic_alloc_mbx_args(&cmd, adapter, cmd_op);

	if (err) {
		qlcnic_sriov_cleanup_transaction(trans);
		return;
	}

	cmd.op_type = hdr->op_type;
	if (qlcnic_sriov_prepare_bc_hdr(trans, &cmd, hdr->seq_id,
					QLC_BC_COMMAND)) {
		qlcnic_free_mbx_args(&cmd);
		qlcnic_sriov_cleanup_transaction(trans);
		return;
	}

	pay_size = qlcnic_sriov_get_bc_paysize(trans->req_pay_size,
					 trans->curr_req_frag);
	qlcnic_sriov_pull_bc_msg(vf->adapter,
				 (u32 *)(trans->req_hdr + trans->curr_req_frag),
				 (u32 *)(trans->req_pay + trans->curr_req_frag),
				 pay_size);
	trans->func_id = vf->pci_func;
	trans->vf = vf;
	trans->trans_id = hdr->seq_id;
	trans->curr_req_frag++;

	if (qlcnic_sriov_soft_flr_check(adapter, trans, vf))
		return;

	if (trans->curr_req_frag == trans->req_hdr->num_frags) {
		if (qlcnic_sriov_add_act_list(sriov, vf, trans)) {
			qlcnic_free_mbx_args(&cmd);
			qlcnic_sriov_cleanup_transaction(trans);
		}
	} else {
		spin_lock(&vf->rcv_pend.lock);
		list_add_tail(&trans->list, &vf->rcv_pend.wait_list);
		vf->rcv_pend.count++;
		spin_unlock(&vf->rcv_pend.lock);
	}
}

static void qlcnic_sriov_handle_msg_event(struct qlcnic_sriov *sriov,
					  struct qlcnic_vf_info *vf)
{
	struct qlcnic_bc_hdr hdr;
	u32 *ptr = (u32 *)&hdr;
	u8 msg_type, i;

	for (i = 2; i < 6; i++)
		ptr[i - 2] = readl(QLCNIC_MBX_FW(vf->adapter->ahw, i));
	msg_type = hdr.msg_type;

	switch (msg_type) {
	case QLC_BC_COMMAND:
		qlcnic_sriov_handle_bc_cmd(sriov, &hdr, vf);
		break;
	case QLC_BC_RESPONSE:
		qlcnic_sriov_handle_bc_resp(&hdr, vf);
		break;
	}
}

static void qlcnic_sriov_handle_flr_event(struct qlcnic_sriov *sriov,
					  struct qlcnic_vf_info *vf)
{
	struct qlcnic_adapter *adapter = vf->adapter;

	if (qlcnic_sriov_pf_check(adapter))
		qlcnic_sriov_pf_handle_flr(sriov, vf);
	else
		dev_err(&adapter->pdev->dev,
			"Invalid event to VF. VF should not get FLR event\n");
}

void qlcnic_sriov_handle_bc_event(struct qlcnic_adapter *adapter, u32 event)
{
	struct qlcnic_vf_info *vf;
	struct qlcnic_sriov *sriov;
	int index;
	u8 pci_func;

	sriov = adapter->ahw->sriov;
	pci_func = qlcnic_sriov_target_func_id(event);
	index = qlcnic_sriov_func_to_index(adapter, pci_func);

	if (index < 0)
		return;

	vf = &sriov->vf_info[index];
	vf->pci_func = pci_func;

	if (qlcnic_sriov_channel_free_check(event))
		complete(&vf->ch_free_cmpl);

	if (qlcnic_sriov_flr_check(event)) {
		qlcnic_sriov_handle_flr_event(sriov, vf);
		return;
	}

	if (qlcnic_sriov_bc_msg_check(event))
		qlcnic_sriov_handle_msg_event(sriov, vf);
}

int qlcnic_sriov_cfg_bc_intr(struct qlcnic_adapter *adapter, u8 enable)
{
	struct qlcnic_cmd_args cmd;
	int err;

	if (!test_bit(__QLCNIC_SRIOV_ENABLE, &adapter->state))
		return 0;

	if (qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_BC_EVENT_SETUP))
		return -ENOMEM;

	if (enable)
		cmd.req.arg[1] = (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7);

	err = qlcnic_83xx_issue_cmd(adapter, &cmd);

	if (err != QLCNIC_RCODE_SUCCESS) {
		dev_err(&adapter->pdev->dev,
			"Failed to %s bc events, err=%d\n",
			(enable ? "enable" : "disable"), err);
	}

	qlcnic_free_mbx_args(&cmd);
	return err;
}

static int qlcnic_sriov_retry_bc_cmd(struct qlcnic_adapter *adapter,
				     struct qlcnic_bc_trans *trans)
{
	u8 max = QLC_BC_CMD_MAX_RETRY_CNT;
	u32 state;

	state = QLCRDX(adapter->ahw, QLC_83XX_IDC_DEV_STATE);
	if (state == QLC_83XX_IDC_DEV_READY) {
		msleep(20);
		clear_bit(QLC_BC_VF_CHANNEL, &trans->vf->state);
		trans->trans_state = QLC_INIT;
		if (++adapter->fw_fail_cnt > max)
			return -EIO;
		else
			return 0;
	}

	return -EIO;
}

static int __qlcnic_sriov_issue_cmd(struct qlcnic_adapter *adapter,
				  struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlcnic_mailbox *mbx = ahw->mailbox;
	struct device *dev = &adapter->pdev->dev;
	struct qlcnic_bc_trans *trans;
	int err;
	u32 rsp_data, opcode, mbx_err_code, rsp;
	u16 seq = ++adapter->ahw->sriov->bc.trans_counter;
	u8 func = ahw->pci_func;

	rsp = qlcnic_sriov_alloc_bc_trans(&trans);
	if (rsp)
		goto free_cmd;

	rsp = qlcnic_sriov_prepare_bc_hdr(trans, cmd, seq, QLC_BC_COMMAND);
	if (rsp)
		goto cleanup_transaction;

retry:
	if (!test_bit(QLC_83XX_MBX_READY, &mbx->status)) {
		rsp = -EIO;
		QLCDB(adapter, DRV, "MBX not Ready!(cmd 0x%x) for VF 0x%x\n",
		      QLCNIC_MBX_RSP(cmd->req.arg[0]), func);
		goto err_out;
	}

	err = qlcnic_sriov_send_bc_cmd(adapter, trans, func);
	if (err) {
		dev_err(dev, "MBX command 0x%x timed out for VF %d\n",
			(cmd->req.arg[0] & 0xffff), func);
		rsp = QLCNIC_RCODE_TIMEOUT;

		/* After adapter reset PF driver may take some time to
		 * respond to VF's request. Retry request till maximum retries.
		 */
		if ((trans->req_hdr->cmd_op == QLCNIC_BC_CMD_CHANNEL_INIT) &&
		    !qlcnic_sriov_retry_bc_cmd(adapter, trans))
			goto retry;

		goto err_out;
	}

	rsp_data = cmd->rsp.arg[0];
	mbx_err_code = QLCNIC_MBX_STATUS(rsp_data);
	opcode = QLCNIC_MBX_RSP(cmd->req.arg[0]);

	if ((mbx_err_code == QLCNIC_MBX_RSP_OK) ||
	    (mbx_err_code == QLCNIC_MBX_PORT_RSP_OK)) {
		rsp = QLCNIC_RCODE_SUCCESS;
	} else {
		if (cmd->type == QLC_83XX_MBX_CMD_NO_WAIT) {
			rsp = QLCNIC_RCODE_SUCCESS;
		} else {
			rsp = mbx_err_code;
			if (!rsp)
				rsp = 1;

			dev_err(dev,
				"MBX command 0x%x failed with err:0x%x for VF %d\n",
				opcode, mbx_err_code, func);
		}
	}

err_out:
	if (rsp == QLCNIC_RCODE_TIMEOUT) {
		ahw->reset_context = 1;
		adapter->need_fw_reset = 1;
		clear_bit(QLC_83XX_MBX_READY, &mbx->status);
	}

cleanup_transaction:
	qlcnic_sriov_cleanup_transaction(trans);

free_cmd:
	if (cmd->type == QLC_83XX_MBX_CMD_NO_WAIT) {
		qlcnic_free_mbx_args(cmd);
		kfree(cmd);
	}

	return rsp;
}


static int qlcnic_sriov_issue_cmd(struct qlcnic_adapter *adapter,
				  struct qlcnic_cmd_args *cmd)
{
	if (cmd->type == QLC_83XX_MBX_CMD_NO_WAIT)
		return qlcnic_sriov_async_issue_cmd(adapter, cmd);
	else
		return __qlcnic_sriov_issue_cmd(adapter, cmd);
}

static int qlcnic_sriov_channel_cfg_cmd(struct qlcnic_adapter *adapter, u8 cmd_op)
{
	struct qlcnic_cmd_args cmd;
	struct qlcnic_vf_info *vf = &adapter->ahw->sriov->vf_info[0];
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	if (qlcnic_sriov_alloc_bc_mbx_args(&cmd, cmd_op))
		return -ENOMEM;

	ret = qlcnic_issue_cmd(adapter, &cmd);
	if (ret) {
		dev_err(&adapter->pdev->dev,
			"Failed bc channel %s %d\n", cmd_op ? "term" : "init",
			ret);
		goto out;
	}

	cmd_op = (cmd.rsp.arg[0] & 0xff);
	if (cmd.rsp.arg[0] >> 25 == 2)
		return 2;
	if (cmd_op == QLCNIC_BC_CMD_CHANNEL_INIT)
		set_bit(QLC_BC_VF_STATE, &vf->state);
	else
		clear_bit(QLC_BC_VF_STATE, &vf->state);

out:
	qlcnic_free_mbx_args(&cmd);
	return ret;
}

static void qlcnic_vf_add_mc_list(struct net_device *netdev, const u8 *mac,
				  enum qlcnic_mac_type mac_type)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_sriov *sriov = adapter->ahw->sriov;
	struct qlcnic_vf_info *vf;
	u16 vlan_id;
	int i;

	vf = &adapter->ahw->sriov->vf_info[0];

	if (!qlcnic_sriov_check_any_vlan(vf)) {
		qlcnic_nic_add_mac(adapter, mac, 0, mac_type);
	} else {
		spin_lock(&vf->vlan_list_lock);
		for (i = 0; i < sriov->num_allowed_vlans; i++) {
			vlan_id = vf->sriov_vlans[i];
			if (vlan_id)
				qlcnic_nic_add_mac(adapter, mac, vlan_id,
						   mac_type);
		}
		spin_unlock(&vf->vlan_list_lock);
		if (qlcnic_84xx_check(adapter))
			qlcnic_nic_add_mac(adapter, mac, 0, mac_type);
	}
}

void qlcnic_sriov_cleanup_async_list(struct qlcnic_back_channel *bc)
{
	struct list_head *head = &bc->async_cmd_list;
	struct qlcnic_async_cmd *entry;

	flush_workqueue(bc->bc_async_wq);
	cancel_work_sync(&bc->vf_async_work);

	spin_lock(&bc->queue_lock);
	while (!list_empty(head)) {
		entry = list_entry(head->next, struct qlcnic_async_cmd,
				   list);
		list_del(&entry->list);
		kfree(entry->cmd);
		kfree(entry);
	}
	spin_unlock(&bc->queue_lock);
}

void qlcnic_sriov_vf_set_multi(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	static const u8 bcast_addr[ETH_ALEN] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	};
	struct netdev_hw_addr *ha;
	u32 mode = VPORT_MISS_MODE_DROP;

	if (!test_bit(__QLCNIC_FW_ATTACHED, &adapter->state))
		return;

	if (netdev->flags & IFF_PROMISC) {
		if (!(adapter->flags & QLCNIC_PROMISC_DISABLED))
			mode = VPORT_MISS_MODE_ACCEPT_ALL;
	} else if ((netdev->flags & IFF_ALLMULTI) ||
		   (netdev_mc_count(netdev) > ahw->max_mc_count)) {
		mode = VPORT_MISS_MODE_ACCEPT_MULTI;
	} else {
		qlcnic_vf_add_mc_list(netdev, bcast_addr, QLCNIC_BROADCAST_MAC);
		if (!netdev_mc_empty(netdev)) {
			qlcnic_flush_mcast_mac(adapter);
			netdev_for_each_mc_addr(ha, netdev)
				qlcnic_vf_add_mc_list(netdev, ha->addr,
						      QLCNIC_MULTICAST_MAC);
		}
	}

	/* configure unicast MAC address, if there is not sufficient space
	 * to store all the unicast addresses then enable promiscuous mode
	 */
	if (netdev_uc_count(netdev) > ahw->max_uc_count) {
		mode = VPORT_MISS_MODE_ACCEPT_ALL;
	} else if (!netdev_uc_empty(netdev)) {
		netdev_for_each_uc_addr(ha, netdev)
			qlcnic_vf_add_mc_list(netdev, ha->addr,
					      QLCNIC_UNICAST_MAC);
	}

	if (adapter->pdev->is_virtfn) {
		if (mode == VPORT_MISS_MODE_ACCEPT_ALL &&
		    !adapter->fdb_mac_learn) {
			qlcnic_alloc_lb_filters_mem(adapter);
			adapter->drv_mac_learn = true;
			adapter->rx_mac_learn = true;
		} else {
			adapter->drv_mac_learn = false;
			adapter->rx_mac_learn = false;
		}
	}

	qlcnic_nic_set_promisc(adapter, mode);
}

static void qlcnic_sriov_handle_async_issue_cmd(struct work_struct *work)
{
	struct qlcnic_async_cmd *entry, *tmp;
	struct qlcnic_back_channel *bc;
	struct qlcnic_cmd_args *cmd;
	struct list_head *head;
	LIST_HEAD(del_list);

	bc = container_of(work, struct qlcnic_back_channel, vf_async_work);
	head = &bc->async_cmd_list;

	spin_lock(&bc->queue_lock);
	list_splice_init(head, &del_list);
	spin_unlock(&bc->queue_lock);

	list_for_each_entry_safe(entry, tmp, &del_list, list) {
		list_del(&entry->list);
		cmd = entry->cmd;
		__qlcnic_sriov_issue_cmd(bc->adapter, cmd);
		kfree(entry);
	}

	if (!list_empty(head))
		queue_work(bc->bc_async_wq, &bc->vf_async_work);

	return;
}

static struct qlcnic_async_cmd *
qlcnic_sriov_alloc_async_cmd(struct qlcnic_back_channel *bc,
			     struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_async_cmd *entry = NULL;

	entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return NULL;

	entry->cmd = cmd;

	spin_lock(&bc->queue_lock);
	list_add_tail(&entry->list, &bc->async_cmd_list);
	spin_unlock(&bc->queue_lock);

	return entry;
}

static void qlcnic_sriov_schedule_async_cmd(struct qlcnic_back_channel *bc,
					    struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_async_cmd *entry = NULL;

	entry = qlcnic_sriov_alloc_async_cmd(bc, cmd);
	if (!entry) {
		qlcnic_free_mbx_args(cmd);
		kfree(cmd);
		return;
	}

	queue_work(bc->bc_async_wq, &bc->vf_async_work);
}

static int qlcnic_sriov_async_issue_cmd(struct qlcnic_adapter *adapter,
					struct qlcnic_cmd_args *cmd)
{

	struct qlcnic_back_channel *bc = &adapter->ahw->sriov->bc;

	if (adapter->need_fw_reset)
		return -EIO;

	qlcnic_sriov_schedule_async_cmd(bc, cmd);

	return 0;
}

static int qlcnic_sriov_vf_reinit_driver(struct qlcnic_adapter *adapter)
{
	int err;

	adapter->need_fw_reset = 0;
	qlcnic_83xx_reinit_mbx_work(adapter->ahw->mailbox);
	qlcnic_83xx_enable_mbx_interrupt(adapter);

	err = qlcnic_sriov_cfg_bc_intr(adapter, 1);
	if (err)
		return err;

	err = qlcnic_sriov_channel_cfg_cmd(adapter, QLCNIC_BC_CMD_CHANNEL_INIT);
	if (err)
		goto err_out_cleanup_bc_intr;

	err = qlcnic_sriov_vf_init_driver(adapter);
	if (err)
		goto err_out_term_channel;

	return 0;

err_out_term_channel:
	qlcnic_sriov_channel_cfg_cmd(adapter, QLCNIC_BC_CMD_CHANNEL_TERM);

err_out_cleanup_bc_intr:
	qlcnic_sriov_cfg_bc_intr(adapter, 0);
	return err;
}

static void qlcnic_sriov_vf_attach(struct qlcnic_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	if (netif_running(netdev)) {
		if (!qlcnic_up(adapter, netdev))
			qlcnic_restore_indev_addr(netdev, NETDEV_UP);
	}

	netif_device_attach(netdev);
}

static void qlcnic_sriov_vf_detach(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlcnic_intrpt_config *intr_tbl = ahw->intr_tbl;
	struct net_device *netdev = adapter->netdev;
	u8 i, max_ints = ahw->num_msix - 1;

	netif_device_detach(netdev);
	qlcnic_83xx_detach_mailbox_work(adapter);
	qlcnic_83xx_disable_mbx_intr(adapter);

	if (netif_running(netdev))
		qlcnic_down(adapter, netdev);

	for (i = 0; i < max_ints; i++) {
		intr_tbl[i].id = i;
		intr_tbl[i].enabled = 0;
		intr_tbl[i].src = 0;
	}
	ahw->reset_context = 0;
}

static int qlcnic_sriov_vf_handle_dev_ready(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct device *dev = &adapter->pdev->dev;
	struct qlc_83xx_idc *idc = &ahw->idc;
	u8 func = ahw->pci_func;
	u32 state;

	if ((idc->prev_state == QLC_83XX_IDC_DEV_NEED_RESET) ||
	    (idc->prev_state == QLC_83XX_IDC_DEV_INIT)) {
		if (!qlcnic_sriov_vf_reinit_driver(adapter)) {
			qlcnic_sriov_vf_attach(adapter);
			adapter->fw_fail_cnt = 0;
			dev_info(dev,
				 "%s: Reinitialization of VF 0x%x done after FW reset\n",
				 __func__, func);
		} else {
			dev_err(dev,
				"%s: Reinitialization of VF 0x%x failed after FW reset\n",
				__func__, func);
			state = QLCRDX(ahw, QLC_83XX_IDC_DEV_STATE);
			dev_info(dev, "Current state 0x%x after FW reset\n",
				 state);
		}
	}

	return 0;
}

static int qlcnic_sriov_vf_handle_context_reset(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlcnic_mailbox *mbx = ahw->mailbox;
	struct device *dev = &adapter->pdev->dev;
	struct qlc_83xx_idc *idc = &ahw->idc;
	u8 func = ahw->pci_func;
	u32 state;

	adapter->reset_ctx_cnt++;

	/* Skip the context reset and check if FW is hung */
	if (adapter->reset_ctx_cnt < 3) {
		adapter->need_fw_reset = 1;
		clear_bit(QLC_83XX_MBX_READY, &mbx->status);
		dev_info(dev,
			 "Resetting context, wait here to check if FW is in failed state\n");
		return 0;
	}

	/* Check if number of resets exceed the threshold.
	 * If it exceeds the threshold just fail the VF.
	 */
	if (adapter->reset_ctx_cnt > QLC_83XX_VF_RESET_FAIL_THRESH) {
		clear_bit(QLC_83XX_MODULE_LOADED, &idc->status);
		adapter->tx_timeo_cnt = 0;
		adapter->fw_fail_cnt = 0;
		adapter->reset_ctx_cnt = 0;
		qlcnic_sriov_vf_detach(adapter);
		dev_err(dev,
			"Device context resets have exceeded the threshold, device interface will be shutdown\n");
		return -EIO;
	}

	dev_info(dev, "Resetting context of VF 0x%x\n", func);
	dev_info(dev, "%s: Context reset count %d for VF 0x%x\n",
		 __func__, adapter->reset_ctx_cnt, func);
	set_bit(__QLCNIC_RESETTING, &adapter->state);
	adapter->need_fw_reset = 1;
	clear_bit(QLC_83XX_MBX_READY, &mbx->status);
	qlcnic_sriov_vf_detach(adapter);
	adapter->need_fw_reset = 0;

	if (!qlcnic_sriov_vf_reinit_driver(adapter)) {
		qlcnic_sriov_vf_attach(adapter);
		adapter->tx_timeo_cnt = 0;
		adapter->reset_ctx_cnt = 0;
		adapter->fw_fail_cnt = 0;
		dev_info(dev, "Done resetting context for VF 0x%x\n", func);
	} else {
		dev_err(dev, "%s: Reinitialization of VF 0x%x failed\n",
			__func__, func);
		state = QLCRDX(ahw, QLC_83XX_IDC_DEV_STATE);
		dev_info(dev, "%s: Current state 0x%x\n", __func__, state);
	}

	return 0;
}

static int qlcnic_sriov_vf_idc_ready_state(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	int ret = 0;

	if (ahw->idc.prev_state != QLC_83XX_IDC_DEV_READY)
		ret = qlcnic_sriov_vf_handle_dev_ready(adapter);
	else if (ahw->reset_context)
		ret = qlcnic_sriov_vf_handle_context_reset(adapter);

	clear_bit(__QLCNIC_RESETTING, &adapter->state);
	return ret;
}

static int qlcnic_sriov_vf_idc_failed_state(struct qlcnic_adapter *adapter)
{
	struct qlc_83xx_idc *idc = &adapter->ahw->idc;

	dev_err(&adapter->pdev->dev, "Device is in failed state\n");
	if (idc->prev_state == QLC_83XX_IDC_DEV_READY)
		qlcnic_sriov_vf_detach(adapter);

	clear_bit(QLC_83XX_MODULE_LOADED, &idc->status);
	clear_bit(__QLCNIC_RESETTING, &adapter->state);
	return -EIO;
}

static int
qlcnic_sriov_vf_idc_need_quiescent_state(struct qlcnic_adapter *adapter)
{
	struct qlcnic_mailbox *mbx = adapter->ahw->mailbox;
	struct qlc_83xx_idc *idc = &adapter->ahw->idc;

	dev_info(&adapter->pdev->dev, "Device is in quiescent state\n");
	if (idc->prev_state == QLC_83XX_IDC_DEV_READY) {
		set_bit(__QLCNIC_RESETTING, &adapter->state);
		adapter->tx_timeo_cnt = 0;
		adapter->reset_ctx_cnt = 0;
		clear_bit(QLC_83XX_MBX_READY, &mbx->status);
		qlcnic_sriov_vf_detach(adapter);
	}

	return 0;
}

static int qlcnic_sriov_vf_idc_init_reset_state(struct qlcnic_adapter *adapter)
{
	struct qlcnic_mailbox *mbx = adapter->ahw->mailbox;
	struct qlc_83xx_idc *idc = &adapter->ahw->idc;
	u8 func = adapter->ahw->pci_func;

	if (idc->prev_state == QLC_83XX_IDC_DEV_READY) {
		dev_err(&adapter->pdev->dev,
			"Firmware hang detected by VF 0x%x\n", func);
		set_bit(__QLCNIC_RESETTING, &adapter->state);
		adapter->tx_timeo_cnt = 0;
		adapter->reset_ctx_cnt = 0;
		clear_bit(QLC_83XX_MBX_READY, &mbx->status);
		qlcnic_sriov_vf_detach(adapter);
	}
	return 0;
}

static int qlcnic_sriov_vf_idc_unknown_state(struct qlcnic_adapter *adapter)
{
	dev_err(&adapter->pdev->dev, "%s: Device in unknown state\n", __func__);
	return 0;
}

static void qlcnic_sriov_vf_periodic_tasks(struct qlcnic_adapter *adapter)
{
	if (adapter->fhash.fnum)
		qlcnic_prune_lb_filters(adapter);
}

static void qlcnic_sriov_vf_poll_dev_state(struct work_struct *work)
{
	struct qlcnic_adapter *adapter;
	struct qlc_83xx_idc *idc;
	int ret = 0;

	adapter = container_of(work, struct qlcnic_adapter, fw_work.work);
	idc = &adapter->ahw->idc;
	idc->curr_state = QLCRDX(adapter->ahw, QLC_83XX_IDC_DEV_STATE);

	switch (idc->curr_state) {
	case QLC_83XX_IDC_DEV_READY:
		ret = qlcnic_sriov_vf_idc_ready_state(adapter);
		break;
	case QLC_83XX_IDC_DEV_NEED_RESET:
	case QLC_83XX_IDC_DEV_INIT:
		ret = qlcnic_sriov_vf_idc_init_reset_state(adapter);
		break;
	case QLC_83XX_IDC_DEV_NEED_QUISCENT:
		ret = qlcnic_sriov_vf_idc_need_quiescent_state(adapter);
		break;
	case QLC_83XX_IDC_DEV_FAILED:
		ret = qlcnic_sriov_vf_idc_failed_state(adapter);
		break;
	case QLC_83XX_IDC_DEV_QUISCENT:
		break;
	default:
		ret = qlcnic_sriov_vf_idc_unknown_state(adapter);
	}

	idc->prev_state = idc->curr_state;
	qlcnic_sriov_vf_periodic_tasks(adapter);

	if (!ret && test_bit(QLC_83XX_MODULE_LOADED, &idc->status))
		qlcnic_schedule_work(adapter, qlcnic_sriov_vf_poll_dev_state,
				     idc->delay);
}

static void qlcnic_sriov_vf_cancel_fw_work(struct qlcnic_adapter *adapter)
{
	while (test_and_set_bit(__QLCNIC_RESETTING, &adapter->state))
		msleep(20);

	clear_bit(QLC_83XX_MODULE_LOADED, &adapter->ahw->idc.status);
	clear_bit(__QLCNIC_RESETTING, &adapter->state);
	cancel_delayed_work_sync(&adapter->fw_work);
}

static int qlcnic_sriov_check_vlan_id(struct qlcnic_sriov *sriov,
				      struct qlcnic_vf_info *vf, u16 vlan_id)
{
	int i, err = -EINVAL;

	if (!vf->sriov_vlans)
		return err;

	spin_lock_bh(&vf->vlan_list_lock);

	for (i = 0; i < sriov->num_allowed_vlans; i++) {
		if (vf->sriov_vlans[i] == vlan_id) {
			err = 0;
			break;
		}
	}

	spin_unlock_bh(&vf->vlan_list_lock);
	return err;
}

static int qlcnic_sriov_validate_num_vlans(struct qlcnic_sriov *sriov,
					   struct qlcnic_vf_info *vf)
{
	int err = 0;

	spin_lock_bh(&vf->vlan_list_lock);

	if (vf->num_vlan >= sriov->num_allowed_vlans)
		err = -EINVAL;

	spin_unlock_bh(&vf->vlan_list_lock);
	return err;
}

static int qlcnic_sriov_validate_vlan_cfg(struct qlcnic_adapter *adapter,
					  u16 vid, u8 enable)
{
	struct qlcnic_sriov *sriov = adapter->ahw->sriov;
	struct qlcnic_vf_info *vf;
	bool vlan_exist;
	u8 allowed = 0;
	int i;

	vf = &adapter->ahw->sriov->vf_info[0];
	vlan_exist = qlcnic_sriov_check_any_vlan(vf);
	if (sriov->vlan_mode != QLC_GUEST_VLAN_MODE)
		return -EINVAL;

	if (enable) {
		if (qlcnic_83xx_vf_check(adapter) && vlan_exist)
			return -EINVAL;

		if (qlcnic_sriov_validate_num_vlans(sriov, vf))
			return -EINVAL;

		if (sriov->any_vlan) {
			for (i = 0; i < sriov->num_allowed_vlans; i++) {
				if (sriov->allowed_vlans[i] == vid)
					allowed = 1;
			}

			if (!allowed)
				return -EINVAL;
		}
	} else {
		if (!vlan_exist || qlcnic_sriov_check_vlan_id(sriov, vf, vid))
			return -EINVAL;
	}

	return 0;
}

static void qlcnic_sriov_vlan_operation(struct qlcnic_vf_info *vf, u16 vlan_id,
					enum qlcnic_vlan_operations opcode)
{
	struct qlcnic_adapter *adapter = vf->adapter;
	struct qlcnic_sriov *sriov;

	sriov = adapter->ahw->sriov;

	if (!vf->sriov_vlans)
		return;

	spin_lock_bh(&vf->vlan_list_lock);

	switch (opcode) {
	case QLC_VLAN_ADD:
		qlcnic_sriov_add_vlan_id(sriov, vf, vlan_id);
		break;
	case QLC_VLAN_DELETE:
		qlcnic_sriov_del_vlan_id(sriov, vf, vlan_id);
		break;
	default:
		netdev_err(adapter->netdev, "Invalid VLAN operation\n");
	}

	spin_unlock_bh(&vf->vlan_list_lock);
	return;
}

int qlcnic_sriov_cfg_vf_guest_vlan(struct qlcnic_adapter *adapter,
				   u16 vid, u8 enable)
{
	struct qlcnic_sriov *sriov = adapter->ahw->sriov;
	struct net_device *netdev = adapter->netdev;
	struct qlcnic_vf_info *vf;
	struct qlcnic_cmd_args cmd;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	if (vid == 0)
		return 0;

	vf = &adapter->ahw->sriov->vf_info[0];
	ret = qlcnic_sriov_validate_vlan_cfg(adapter, vid, enable);
	if (ret)
		return ret;

	ret = qlcnic_sriov_alloc_bc_mbx_args(&cmd,
					     QLCNIC_BC_CMD_CFG_GUEST_VLAN);
	if (ret)
		return ret;

	cmd.req.arg[1] = (enable & 1) | vid << 16;

	qlcnic_sriov_cleanup_async_list(&sriov->bc);
	ret = qlcnic_issue_cmd(adapter, &cmd);
	if (ret) {
		dev_err(&adapter->pdev->dev,
			"Failed to configure guest VLAN, err=%d\n", ret);
	} else {
		netif_addr_lock_bh(netdev);
		qlcnic_free_mac_list(adapter);
		netif_addr_unlock_bh(netdev);

		if (enable)
			qlcnic_sriov_vlan_operation(vf, vid, QLC_VLAN_ADD);
		else
			qlcnic_sriov_vlan_operation(vf, vid, QLC_VLAN_DELETE);

		netif_addr_lock_bh(netdev);
		qlcnic_set_multi(netdev);
		netif_addr_unlock_bh(netdev);
	}

	qlcnic_free_mbx_args(&cmd);
	return ret;
}

static void qlcnic_sriov_vf_free_mac_list(struct qlcnic_adapter *adapter)
{
	struct list_head *head = &adapter->mac_list;
	struct qlcnic_mac_vlan_list *cur;

	while (!list_empty(head)) {
		cur = list_entry(head->next, struct qlcnic_mac_vlan_list, list);
		qlcnic_sre_macaddr_change(adapter, cur->mac_addr, cur->vlan_id,
					  QLCNIC_MAC_DEL);
		list_del(&cur->list);
		kfree(cur);
	}
}


static int qlcnic_sriov_vf_shutdown(struct pci_dev *pdev)
{
	struct qlcnic_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;

	netif_device_detach(netdev);
	qlcnic_cancel_idc_work(adapter);

	if (netif_running(netdev))
		qlcnic_down(adapter, netdev);

	qlcnic_sriov_channel_cfg_cmd(adapter, QLCNIC_BC_CMD_CHANNEL_TERM);
	qlcnic_sriov_cfg_bc_intr(adapter, 0);
	qlcnic_83xx_disable_mbx_intr(adapter);
	cancel_delayed_work_sync(&adapter->idc_aen_work);

	return pci_save_state(pdev);
}

static int qlcnic_sriov_vf_resume(struct qlcnic_adapter *adapter)
{
	struct qlc_83xx_idc *idc = &adapter->ahw->idc;
	struct net_device *netdev = adapter->netdev;
	int err;

	set_bit(QLC_83XX_MODULE_LOADED, &idc->status);
	qlcnic_83xx_enable_mbx_interrupt(adapter);
	err = qlcnic_sriov_cfg_bc_intr(adapter, 1);
	if (err)
		return err;

	err = qlcnic_sriov_channel_cfg_cmd(adapter, QLCNIC_BC_CMD_CHANNEL_INIT);
	if (!err) {
		if (netif_running(netdev)) {
			err = qlcnic_up(adapter, netdev);
			if (!err)
				qlcnic_restore_indev_addr(netdev, NETDEV_UP);
		}
	}

	netif_device_attach(netdev);
	qlcnic_schedule_work(adapter, qlcnic_sriov_vf_poll_dev_state,
			     idc->delay);
	return err;
}

int qlcnic_sriov_alloc_vlans(struct qlcnic_adapter *adapter)
{
	struct qlcnic_sriov *sriov = adapter->ahw->sriov;
	struct qlcnic_vf_info *vf;
	int i;

	for (i = 0; i < sriov->num_vfs; i++) {
		vf = &sriov->vf_info[i];
		vf->sriov_vlans = kcalloc(sriov->num_allowed_vlans,
					  sizeof(*vf->sriov_vlans), GFP_KERNEL);
		if (!vf->sriov_vlans)
			return -ENOMEM;
	}

	return 0;
}

void qlcnic_sriov_free_vlans(struct qlcnic_adapter *adapter)
{
	struct qlcnic_sriov *sriov = adapter->ahw->sriov;
	struct qlcnic_vf_info *vf;
	int i;

	for (i = 0; i < sriov->num_vfs; i++) {
		vf = &sriov->vf_info[i];
		kfree(vf->sriov_vlans);
		vf->sriov_vlans = NULL;
	}
}

void qlcnic_sriov_add_vlan_id(struct qlcnic_sriov *sriov,
			      struct qlcnic_vf_info *vf, u16 vlan_id)
{
	int i;

	for (i = 0; i < sriov->num_allowed_vlans; i++) {
		if (!vf->sriov_vlans[i]) {
			vf->sriov_vlans[i] = vlan_id;
			vf->num_vlan++;
			return;
		}
	}
}

void qlcnic_sriov_del_vlan_id(struct qlcnic_sriov *sriov,
			      struct qlcnic_vf_info *vf, u16 vlan_id)
{
	int i;

	for (i = 0; i < sriov->num_allowed_vlans; i++) {
		if (vf->sriov_vlans[i] == vlan_id) {
			vf->sriov_vlans[i] = 0;
			vf->num_vlan--;
			return;
		}
	}
}

bool qlcnic_sriov_check_any_vlan(struct qlcnic_vf_info *vf)
{
	bool err = false;

	spin_lock_bh(&vf->vlan_list_lock);

	if (vf->num_vlan)
		err = true;

	spin_unlock_bh(&vf->vlan_list_lock);
	return err;
}
