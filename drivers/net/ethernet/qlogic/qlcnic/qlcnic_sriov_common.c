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

#define QLC_BC_COMMAND	0
#define QLC_BC_RESPONSE	1

#define QLC_MBOX_RESP_TIMEOUT		(10 * HZ)
#define QLC_MBOX_CH_FREE_TIMEOUT	(10 * HZ)

#define QLC_BC_MSG		0
#define QLC_BC_CFREE		1
#define QLC_BC_HDR_SZ		16
#define QLC_BC_PAYLOAD_SZ	(1024 - QLC_BC_HDR_SZ)

#define QLC_DEFAULT_RCV_DESCRIPTORS_SRIOV_VF		2048
#define QLC_DEFAULT_JUMBO_RCV_DESCRIPTORS_SRIOV_VF	512

static int qlcnic_sriov_vf_mbx_op(struct qlcnic_adapter *,
				  struct qlcnic_cmd_args *);

static struct qlcnic_hardware_ops qlcnic_sriov_vf_hw_ops = {
	.read_crb			= qlcnic_83xx_read_crb,
	.write_crb			= qlcnic_83xx_write_crb,
	.read_reg			= qlcnic_83xx_rd_reg_indirect,
	.write_reg			= qlcnic_83xx_wrt_reg_indirect,
	.get_mac_address		= qlcnic_83xx_get_mac_address,
	.setup_intr			= qlcnic_83xx_setup_intr,
	.alloc_mbx_args			= qlcnic_83xx_alloc_mbx_args,
	.mbx_cmd			= qlcnic_sriov_vf_mbx_op,
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

static const struct qlcnic_mailbox_metadata qlcnic_sriov_bc_mbx_tbl[] = {
	{QLCNIC_BC_CMD_CHANNEL_INIT, 2, 2},
	{QLCNIC_BC_CMD_CHANNEL_TERM, 2, 2},
};

static inline bool qlcnic_sriov_bc_msg_check(u32 val)
{
	return (val & (1 << QLC_BC_MSG)) ? true : false;
}

static inline bool qlcnic_sriov_channel_free_check(u32 val)
{
	return (val & (1 << QLC_BC_CFREE)) ? true : false;
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
	sriov->vf_info = kzalloc(sizeof(struct qlcnic_vf_info) *
				 num_vfs, GFP_KERNEL);
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
	INIT_LIST_HEAD(&bc->async_list);

	for (i = 0; i < num_vfs; i++) {
		vf = &sriov->vf_info[i];
		vf->adapter = adapter;
		vf->pci_func = qlcnic_sriov_virtid_fn(adapter, i);
		mutex_init(&vf->send_cmd_lock);
		INIT_LIST_HEAD(&vf->rcv_act.wait_list);
		INIT_LIST_HEAD(&vf->rcv_pend.wait_list);
		spin_lock_init(&vf->rcv_act.lock);
		spin_lock_init(&vf->rcv_pend.lock);
		init_completion(&vf->ch_free_cmpl);

		if (qlcnic_sriov_pf_check(adapter)) {
			vp = kzalloc(sizeof(struct qlcnic_vport), GFP_KERNEL);
			if (!vp) {
				err = -ENOMEM;
				goto qlcnic_destroy_async_wq;
			}
			sriov->vf_info[i].vp = vp;
			random_ether_addr(vp->mac);
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

void __qlcnic_sriov_cleanup(struct qlcnic_adapter *adapter)
{
	struct qlcnic_sriov *sriov = adapter->ahw->sriov;
	struct qlcnic_back_channel *bc = &sriov->bc;
	int i;

	if (!qlcnic_sriov_enable_check(adapter))
		return;

	qlcnic_sriov_cleanup_async_list(bc);
	destroy_workqueue(bc->bc_async_wq);
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
	if (qlcnic_sriov_pf_check(adapter))
		qlcnic_sriov_pf_cleanup(adapter);

	if (qlcnic_sriov_vf_check(adapter))
		qlcnic_sriov_vf_cleanup(adapter);
}

static int qlcnic_sriov_post_bc_msg(struct qlcnic_adapter *adapter, u32 *hdr,
				    u32 *pay, u8 pci_func, u8 size)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	unsigned long flags;
	u32 rsp, mbx_val, fw_data, rsp_num, mbx_cmd, val;
	u16 opcode;
	u8 mbx_err_code;
	int i, j;

	opcode = ((struct qlcnic_bc_hdr *)hdr)->cmd_op;

	if (!test_bit(QLC_83XX_MBX_READY, &adapter->ahw->idc.status)) {
		dev_info(&adapter->pdev->dev,
			 "Mailbox cmd attempted, 0x%x\n", opcode);
		dev_info(&adapter->pdev->dev, "Mailbox detached\n");
		return 0;
	}

	spin_lock_irqsave(&ahw->mbx_lock, flags);

	mbx_val = QLCRDX(ahw, QLCNIC_HOST_MBX_CTRL);
	if (mbx_val) {
		QLCDB(adapter, DRV, "Mailbox cmd attempted, 0x%x\n", opcode);
		spin_unlock_irqrestore(&ahw->mbx_lock, flags);
		return QLCNIC_RCODE_TIMEOUT;
	}
	/* Fill in mailbox registers */
	val = size + (sizeof(struct qlcnic_bc_hdr) / sizeof(u32));
	mbx_cmd = 0x31 | (val << 16) | (adapter->ahw->fw_hal_version << 29);

	writel(mbx_cmd, QLCNIC_MBX_HOST(ahw, 0));
	mbx_cmd = 0x1 | (1 << 4);

	if (qlcnic_sriov_pf_check(adapter))
		mbx_cmd |= (pci_func << 5);

	writel(mbx_cmd, QLCNIC_MBX_HOST(ahw, 1));
	for (i = 2, j = 0; j < (sizeof(struct qlcnic_bc_hdr) / sizeof(u32));
			i++, j++) {
		writel(*(hdr++), QLCNIC_MBX_HOST(ahw, i));
	}
	for (j = 0; j < size; j++, i++)
		writel(*(pay++), QLCNIC_MBX_HOST(ahw, i));

	/* Signal FW about the impending command */
	QLCWRX(ahw, QLCNIC_HOST_MBX_CTRL, QLCNIC_SET_OWNER);

	/* Waiting for the mailbox cmd to complete and while waiting here
	 * some AEN might arrive. If more than 5 seconds expire we can
	 * assume something is wrong.
	 */
poll:
	rsp = qlcnic_83xx_mbx_poll(adapter);
	if (rsp != QLCNIC_RCODE_TIMEOUT) {
		/* Get the FW response data */
		fw_data = readl(QLCNIC_MBX_FW(ahw, 0));
		if (fw_data &  QLCNIC_MBX_ASYNC_EVENT) {
			qlcnic_83xx_process_aen(adapter);
			mbx_val = QLCRDX(ahw, QLCNIC_HOST_MBX_CTRL);
			if (mbx_val)
				goto poll;
		}
		mbx_err_code = QLCNIC_MBX_STATUS(fw_data);
		rsp_num = QLCNIC_MBX_NUM_REGS(fw_data);
		opcode = QLCNIC_MBX_RSP(fw_data);

		switch (mbx_err_code) {
		case QLCNIC_MBX_RSP_OK:
		case QLCNIC_MBX_PORT_RSP_OK:
			rsp = QLCNIC_RCODE_SUCCESS;
			break;
		default:
			if (opcode == QLCNIC_CMD_CONFIG_MAC_VLAN) {
				rsp = qlcnic_83xx_mac_rcode(adapter);
				if (!rsp)
					goto out;
			}
			dev_err(&adapter->pdev->dev,
				"MBX command 0x%x failed with err:0x%x\n",
				opcode, mbx_err_code);
			rsp = mbx_err_code;
			break;
		}
		goto out;
	}

	dev_err(&adapter->pdev->dev, "MBX command 0x%x timed out\n",
		QLCNIC_MBX_RSP(mbx_cmd));
	rsp = QLCNIC_RCODE_TIMEOUT;
out:
	/* clear fw mbx control register */
	QLCWRX(ahw, QLCNIC_FW_MBX_CTRL, QLCNIC_CLR_OWNER);
	spin_unlock_irqrestore(&adapter->ahw->mbx_lock, flags);
	return rsp;
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

static int qlcnic_sriov_vf_init_driver(struct qlcnic_adapter *adapter)
{
	struct qlcnic_info nic_info;
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	int err;

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

static int qlcnic_sriov_setup_vf(struct qlcnic_adapter *adapter,
				 int pci_using_dac)
{
	int err;

	INIT_LIST_HEAD(&adapter->vf_mc_list);
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

	err = qlcnic_sriov_cfg_bc_intr(adapter, 1);
	if (err)
		goto err_out_cleanup_sriov;

	err = qlcnic_sriov_channel_cfg_cmd(adapter, QLCNIC_BC_CMD_CHANNEL_INIT);
	if (err)
		goto err_out_disable_bc_intr;

	err = qlcnic_sriov_vf_init_driver(adapter);
	if (err)
		goto err_out_send_channel_term;

	err = qlcnic_setup_netdev(adapter, adapter->netdev, pci_using_dac);
	if (err)
		goto err_out_send_channel_term;

	pci_set_drvdata(adapter->pdev, adapter);
	dev_info(&adapter->pdev->dev, "%s: XGbE port initialized\n",
		 adapter->netdev->name);
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
	*hdr = kzalloc(sizeof(struct qlcnic_bc_hdr) * size, GFP_ATOMIC);
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
			memset(mbx->req.arg, 0, sizeof(u32) * mbx->req.num);
			memset(mbx->rsp.arg, 0, sizeof(u32) * mbx->rsp.num);
			mbx->req.arg[0] = (type | (mbx->req.num << 16) |
					   (3 << 29));
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
		remainder = (trans->rsp_pay_size) % (bc_pay_sz);
		num_frags = (trans->rsp_pay_size) / (bc_pay_sz);
		if (remainder)
			num_frags++;
		cmd->req.num = trans->req_pay_size / 4;
		cmd->rsp.num = trans->rsp_pay_size / 4;
		hdr = trans->rsp_hdr;
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
	INIT_WORK(&vf->trans_work, func);
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
	u32 fw_mbx;
	u8 i, max = 2, hdr_size, j;

	hdr_size = (sizeof(struct qlcnic_bc_hdr) / sizeof(u32));
	max = (size / sizeof(u32)) + hdr_size;

	fw_mbx = readl(QLCNIC_MBX_FW(ahw, 0));
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
	u32 pay_size, hdr_size;
	u32 *hdr, *pay;
	int ret;
	u8 pci_func = trans->func_id;

	if (__qlcnic_sriov_issue_bc_post(vf))
		return -EBUSY;

	if (type == QLC_BC_COMMAND) {
		hdr = (u32 *)(trans->req_hdr + trans->curr_req_frag);
		pay = (u32 *)(trans->req_pay + trans->curr_req_frag);
		hdr_size = (sizeof(struct qlcnic_bc_hdr) / sizeof(u32));
		pay_size = qlcnic_sriov_get_bc_paysize(trans->req_pay_size,
						       trans->curr_req_frag);
		pay_size = (pay_size / sizeof(u32));
	} else {
		hdr = (u32 *)(trans->rsp_hdr + trans->curr_rsp_frag);
		pay = (u32 *)(trans->rsp_pay + trans->curr_rsp_frag);
		hdr_size = (sizeof(struct qlcnic_bc_hdr) / sizeof(u32));
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
	int err;
	bool flag = true;

	while (flag) {
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

static int qlcnic_sriov_add_act_list(struct qlcnic_sriov *sriov,
				     struct qlcnic_vf_info *vf,
				     struct qlcnic_bc_trans *trans)
{
	struct qlcnic_trans_list *t_list = &vf->rcv_act;

	spin_lock(&t_list->lock);
	t_list->count++;
	list_add_tail(&trans->list, &t_list->wait_list);
	if (t_list->count == 1)
		qlcnic_sriov_schedule_bc_cmd(sriov, vf,
					     qlcnic_sriov_process_bc_cmd);
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

	if (!test_bit(QLC_BC_VF_STATE, &vf->state) &&
	    hdr->op_type != QLC_BC_CMD &&
	    hdr->cmd_op != QLCNIC_BC_CMD_CHANNEL_INIT)
		return;

	if (hdr->frag_num > 1) {
		qlcnic_sriov_handle_pending_trans(sriov, vf, hdr);
		return;
	}

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

	err = qlcnic_83xx_mbx_op(adapter, &cmd);

	if (err != QLCNIC_RCODE_SUCCESS) {
		dev_err(&adapter->pdev->dev,
			"Failed to %s bc events, err=%d\n",
			(enable ? "enable" : "disable"), err);
	}

	qlcnic_free_mbx_args(&cmd);
	return err;
}

static int qlcnic_sriov_vf_mbx_op(struct qlcnic_adapter *adapter,
				  struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_bc_trans *trans;
	int err;
	u32 rsp_data, opcode, mbx_err_code, rsp;
	u16 seq = ++adapter->ahw->sriov->bc.trans_counter;

	if (qlcnic_sriov_alloc_bc_trans(&trans))
		return -ENOMEM;

	if (qlcnic_sriov_prepare_bc_hdr(trans, cmd, seq, QLC_BC_COMMAND))
		return -ENOMEM;

	if (!test_bit(QLC_83XX_MBX_READY, &adapter->ahw->idc.status)) {
		rsp = -EIO;
		QLCDB(adapter, DRV, "MBX not Ready!(cmd 0x%x) for VF 0x%x\n",
		      QLCNIC_MBX_RSP(cmd->req.arg[0]), adapter->ahw->pci_func);
		goto err_out;
	}

	err = qlcnic_sriov_send_bc_cmd(adapter, trans, adapter->ahw->pci_func);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"MBX command 0x%x timed out for VF %d\n",
			(cmd->req.arg[0] & 0xffff), adapter->ahw->pci_func);
		rsp = QLCNIC_RCODE_TIMEOUT;
		goto err_out;
	}

	rsp_data = cmd->rsp.arg[0];
	mbx_err_code = QLCNIC_MBX_STATUS(rsp_data);
	opcode = QLCNIC_MBX_RSP(cmd->req.arg[0]);

	if ((mbx_err_code == QLCNIC_MBX_RSP_OK) ||
	    (mbx_err_code == QLCNIC_MBX_PORT_RSP_OK)) {
		rsp = QLCNIC_RCODE_SUCCESS;
	} else {
		rsp = mbx_err_code;
		if (!rsp)
			rsp = 1;
		dev_err(&adapter->pdev->dev,
			"MBX command 0x%x failed with err:0x%x for VF %d\n",
			opcode, mbx_err_code, adapter->ahw->pci_func);
	}

err_out:
	qlcnic_sriov_cleanup_transaction(trans);
	return rsp;
}

int qlcnic_sriov_channel_cfg_cmd(struct qlcnic_adapter *adapter, u8 cmd_op)
{
	struct qlcnic_cmd_args cmd;
	struct qlcnic_vf_info *vf = &adapter->ahw->sriov->vf_info[0];
	int ret;

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

void qlcnic_vf_add_mc_list(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_mac_list_s *cur;
	struct list_head *head, tmp_list;

	INIT_LIST_HEAD(&tmp_list);
	head = &adapter->vf_mc_list;
	netif_addr_lock_bh(netdev);

	while (!list_empty(head)) {
		cur = list_entry(head->next, struct qlcnic_mac_list_s, list);
		list_move(&cur->list, &tmp_list);
	}

	netif_addr_unlock_bh(netdev);

	while (!list_empty(&tmp_list)) {
		cur = list_entry((&tmp_list)->next,
				 struct qlcnic_mac_list_s, list);
		qlcnic_nic_add_mac(adapter, cur->mac_addr);
		list_del(&cur->list);
		kfree(cur);
	}
}

void qlcnic_sriov_cleanup_async_list(struct qlcnic_back_channel *bc)
{
	struct list_head *head = &bc->async_list;
	struct qlcnic_async_work_list *entry;

	while (!list_empty(head)) {
		entry = list_entry(head->next, struct qlcnic_async_work_list,
				   list);
		cancel_work_sync(&entry->work);
		list_del(&entry->list);
		kfree(entry);
	}
}

static void qlcnic_sriov_vf_set_multi(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);

	if (!test_bit(__QLCNIC_FW_ATTACHED, &adapter->state))
		return;

	__qlcnic_set_multi(netdev);
}

static void qlcnic_sriov_handle_async_multi(struct work_struct *work)
{
	struct qlcnic_async_work_list *entry;
	struct net_device *netdev;

	entry = container_of(work, struct qlcnic_async_work_list, work);
	netdev = (struct net_device *)entry->ptr;

	qlcnic_sriov_vf_set_multi(netdev);
	return;
}

static struct qlcnic_async_work_list *
qlcnic_sriov_get_free_node_async_work(struct qlcnic_back_channel *bc)
{
	struct list_head *node;
	struct qlcnic_async_work_list *entry = NULL;
	u8 empty = 0;

	list_for_each(node, &bc->async_list) {
		entry = list_entry(node, struct qlcnic_async_work_list, list);
		if (!work_pending(&entry->work)) {
			empty = 1;
			break;
		}
	}

	if (!empty) {
		entry = kzalloc(sizeof(struct qlcnic_async_work_list),
				GFP_ATOMIC);
		if (entry == NULL)
			return NULL;
		list_add_tail(&entry->list, &bc->async_list);
	}

	return entry;
}

static void qlcnic_sriov_schedule_bc_async_work(struct qlcnic_back_channel *bc,
						work_func_t func, void *data)
{
	struct qlcnic_async_work_list *entry = NULL;

	entry = qlcnic_sriov_get_free_node_async_work(bc);
	if (!entry)
		return;

	entry->ptr = data;
	INIT_WORK(&entry->work, func);
	queue_work(bc->bc_async_wq, &entry->work);
}

void qlcnic_sriov_vf_schedule_multi(struct net_device *netdev)
{

	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_back_channel *bc = &adapter->ahw->sriov->bc;

	qlcnic_sriov_schedule_bc_async_work(bc, qlcnic_sriov_handle_async_multi,
					    netdev);
}
