/*
 * QLogic qlcnic NIC Driver
 * Copyright (c) 2009-2013 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

#include "qlcnic.h"
#include "qlcnic_sriov.h"
#include <linux/if_vlan.h>
#include <linux/ipv6.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>

#define QLCNIC_MAX_TX_QUEUES		1
#define RSS_HASHTYPE_IP_TCP		0x3
#define QLC_83XX_FW_MBX_CMD		0

static const struct qlcnic_mailbox_metadata qlcnic_83xx_mbx_tbl[] = {
	{QLCNIC_CMD_CONFIGURE_IP_ADDR, 6, 1},
	{QLCNIC_CMD_CONFIG_INTRPT, 18, 34},
	{QLCNIC_CMD_CREATE_RX_CTX, 136, 27},
	{QLCNIC_CMD_DESTROY_RX_CTX, 2, 1},
	{QLCNIC_CMD_CREATE_TX_CTX, 54, 18},
	{QLCNIC_CMD_DESTROY_TX_CTX, 2, 1},
	{QLCNIC_CMD_CONFIGURE_MAC_LEARNING, 2, 1},
	{QLCNIC_CMD_INTRPT_TEST, 22, 12},
	{QLCNIC_CMD_SET_MTU, 3, 1},
	{QLCNIC_CMD_READ_PHY, 4, 2},
	{QLCNIC_CMD_WRITE_PHY, 5, 1},
	{QLCNIC_CMD_READ_HW_REG, 4, 1},
	{QLCNIC_CMD_GET_FLOW_CTL, 4, 2},
	{QLCNIC_CMD_SET_FLOW_CTL, 4, 1},
	{QLCNIC_CMD_READ_MAX_MTU, 4, 2},
	{QLCNIC_CMD_READ_MAX_LRO, 4, 2},
	{QLCNIC_CMD_MAC_ADDRESS, 4, 3},
	{QLCNIC_CMD_GET_PCI_INFO, 1, 66},
	{QLCNIC_CMD_GET_NIC_INFO, 2, 19},
	{QLCNIC_CMD_SET_NIC_INFO, 32, 1},
	{QLCNIC_CMD_GET_ESWITCH_CAPABILITY, 4, 3},
	{QLCNIC_CMD_TOGGLE_ESWITCH, 4, 1},
	{QLCNIC_CMD_GET_ESWITCH_STATUS, 4, 3},
	{QLCNIC_CMD_SET_PORTMIRRORING, 4, 1},
	{QLCNIC_CMD_CONFIGURE_ESWITCH, 4, 1},
	{QLCNIC_CMD_GET_ESWITCH_PORT_CONFIG, 4, 3},
	{QLCNIC_CMD_GET_ESWITCH_STATS, 5, 1},
	{QLCNIC_CMD_CONFIG_PORT, 4, 1},
	{QLCNIC_CMD_TEMP_SIZE, 1, 4},
	{QLCNIC_CMD_GET_TEMP_HDR, 5, 5},
	{QLCNIC_CMD_GET_LINK_EVENT, 2, 1},
	{QLCNIC_CMD_CONFIG_MAC_VLAN, 4, 3},
	{QLCNIC_CMD_CONFIG_INTR_COAL, 6, 1},
	{QLCNIC_CMD_CONFIGURE_RSS, 14, 1},
	{QLCNIC_CMD_CONFIGURE_LED, 2, 1},
	{QLCNIC_CMD_CONFIGURE_MAC_RX_MODE, 2, 1},
	{QLCNIC_CMD_CONFIGURE_HW_LRO, 2, 1},
	{QLCNIC_CMD_GET_STATISTICS, 2, 80},
	{QLCNIC_CMD_SET_PORT_CONFIG, 2, 1},
	{QLCNIC_CMD_GET_PORT_CONFIG, 2, 2},
	{QLCNIC_CMD_GET_LINK_STATUS, 2, 4},
	{QLCNIC_CMD_IDC_ACK, 5, 1},
	{QLCNIC_CMD_INIT_NIC_FUNC, 2, 1},
	{QLCNIC_CMD_STOP_NIC_FUNC, 2, 1},
	{QLCNIC_CMD_SET_LED_CONFIG, 5, 1},
	{QLCNIC_CMD_GET_LED_CONFIG, 1, 5},
	{QLCNIC_CMD_83XX_SET_DRV_VER, 4, 1},
	{QLCNIC_CMD_ADD_RCV_RINGS, 130, 26},
	{QLCNIC_CMD_CONFIG_VPORT, 4, 4},
	{QLCNIC_CMD_BC_EVENT_SETUP, 2, 1},
};

const u32 qlcnic_83xx_ext_reg_tbl[] = {
	0x38CC,		/* Global Reset */
	0x38F0,		/* Wildcard */
	0x38FC,		/* Informant */
	0x3038,		/* Host MBX ctrl */
	0x303C,		/* FW MBX ctrl */
	0x355C,		/* BOOT LOADER ADDRESS REG */
	0x3560,		/* BOOT LOADER SIZE REG */
	0x3564,		/* FW IMAGE ADDR REG */
	0x1000,		/* MBX intr enable */
	0x1200,		/* Default Intr mask */
	0x1204,		/* Default Interrupt ID */
	0x3780,		/* QLC_83XX_IDC_MAJ_VERSION */
	0x3784,		/* QLC_83XX_IDC_DEV_STATE */
	0x3788,		/* QLC_83XX_IDC_DRV_PRESENCE */
	0x378C,		/* QLC_83XX_IDC_DRV_ACK */
	0x3790,		/* QLC_83XX_IDC_CTRL */
	0x3794,		/* QLC_83XX_IDC_DRV_AUDIT */
	0x3798,		/* QLC_83XX_IDC_MIN_VERSION */
	0x379C,		/* QLC_83XX_RECOVER_DRV_LOCK */
	0x37A0,		/* QLC_83XX_IDC_PF_0 */
	0x37A4,		/* QLC_83XX_IDC_PF_1 */
	0x37A8,		/* QLC_83XX_IDC_PF_2 */
	0x37AC,		/* QLC_83XX_IDC_PF_3 */
	0x37B0,		/* QLC_83XX_IDC_PF_4 */
	0x37B4,		/* QLC_83XX_IDC_PF_5 */
	0x37B8,		/* QLC_83XX_IDC_PF_6 */
	0x37BC,		/* QLC_83XX_IDC_PF_7 */
	0x37C0,		/* QLC_83XX_IDC_PF_8 */
	0x37C4,		/* QLC_83XX_IDC_PF_9 */
	0x37C8,		/* QLC_83XX_IDC_PF_10 */
	0x37CC,		/* QLC_83XX_IDC_PF_11 */
	0x37D0,		/* QLC_83XX_IDC_PF_12 */
	0x37D4,		/* QLC_83XX_IDC_PF_13 */
	0x37D8,		/* QLC_83XX_IDC_PF_14 */
	0x37DC,		/* QLC_83XX_IDC_PF_15 */
	0x37E0,		/* QLC_83XX_IDC_DEV_PARTITION_INFO_1 */
	0x37E4,		/* QLC_83XX_IDC_DEV_PARTITION_INFO_2 */
	0x37F0,		/* QLC_83XX_DRV_OP_MODE */
	0x37F4,		/* QLC_83XX_VNIC_STATE */
	0x3868,		/* QLC_83XX_DRV_LOCK */
	0x386C,		/* QLC_83XX_DRV_UNLOCK */
	0x3504,		/* QLC_83XX_DRV_LOCK_ID */
	0x34A4,		/* QLC_83XX_ASIC_TEMP */
};

const u32 qlcnic_83xx_reg_tbl[] = {
	0x34A8,		/* PEG_HALT_STAT1 */
	0x34AC,		/* PEG_HALT_STAT2 */
	0x34B0,		/* FW_HEARTBEAT */
	0x3500,		/* FLASH LOCK_ID */
	0x3528,		/* FW_CAPABILITIES */
	0x3538,		/* Driver active, DRV_REG0 */
	0x3540,		/* Device state, DRV_REG1 */
	0x3544,		/* Driver state, DRV_REG2 */
	0x3548,		/* Driver scratch, DRV_REG3 */
	0x354C,		/* Device partiton info, DRV_REG4 */
	0x3524,		/* Driver IDC ver, DRV_REG5 */
	0x3550,		/* FW_VER_MAJOR */
	0x3554,		/* FW_VER_MINOR */
	0x3558,		/* FW_VER_SUB */
	0x359C,		/* NPAR STATE */
	0x35FC,		/* FW_IMG_VALID */
	0x3650,		/* CMD_PEG_STATE */
	0x373C,		/* RCV_PEG_STATE */
	0x37B4,		/* ASIC TEMP */
	0x356C,		/* FW API */
	0x3570,		/* DRV OP MODE */
	0x3850,		/* FLASH LOCK */
	0x3854,		/* FLASH UNLOCK */
};

static struct qlcnic_hardware_ops qlcnic_83xx_hw_ops = {
	.read_crb			= qlcnic_83xx_read_crb,
	.write_crb			= qlcnic_83xx_write_crb,
	.read_reg			= qlcnic_83xx_rd_reg_indirect,
	.write_reg			= qlcnic_83xx_wrt_reg_indirect,
	.get_mac_address		= qlcnic_83xx_get_mac_address,
	.setup_intr			= qlcnic_83xx_setup_intr,
	.alloc_mbx_args			= qlcnic_83xx_alloc_mbx_args,
	.mbx_cmd			= qlcnic_83xx_issue_cmd,
	.get_func_no			= qlcnic_83xx_get_func_no,
	.api_lock			= qlcnic_83xx_cam_lock,
	.api_unlock			= qlcnic_83xx_cam_unlock,
	.add_sysfs			= qlcnic_83xx_add_sysfs,
	.remove_sysfs			= qlcnic_83xx_remove_sysfs,
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
	.set_mac_filter_count		= qlcnic_83xx_set_mac_filter_count,
	.free_mac_list			= qlcnic_82xx_free_mac_list,
};

static struct qlcnic_nic_template qlcnic_83xx_ops = {
	.config_bridged_mode	= qlcnic_config_bridged_mode,
	.config_led		= qlcnic_config_led,
	.request_reset          = qlcnic_83xx_idc_request_reset,
	.cancel_idc_work        = qlcnic_83xx_idc_exit,
	.napi_add		= qlcnic_83xx_napi_add,
	.napi_del		= qlcnic_83xx_napi_del,
	.config_ipaddr		= qlcnic_83xx_config_ipaddr,
	.clear_legacy_intr	= qlcnic_83xx_clear_legacy_intr,
	.shutdown		= qlcnic_83xx_shutdown,
	.resume			= qlcnic_83xx_resume,
};

void qlcnic_83xx_register_map(struct qlcnic_hardware_context *ahw)
{
	ahw->hw_ops		= &qlcnic_83xx_hw_ops;
	ahw->reg_tbl		= (u32 *)qlcnic_83xx_reg_tbl;
	ahw->ext_reg_tbl	= (u32 *)qlcnic_83xx_ext_reg_tbl;
}

int qlcnic_83xx_get_fw_version(struct qlcnic_adapter *adapter)
{
	u32 fw_major, fw_minor, fw_build;
	struct pci_dev *pdev = adapter->pdev;

	fw_major = QLC_SHARED_REG_RD32(adapter, QLCNIC_FW_VERSION_MAJOR);
	fw_minor = QLC_SHARED_REG_RD32(adapter, QLCNIC_FW_VERSION_MINOR);
	fw_build = QLC_SHARED_REG_RD32(adapter, QLCNIC_FW_VERSION_SUB);
	adapter->fw_version = QLCNIC_VERSION_CODE(fw_major, fw_minor, fw_build);

	dev_info(&pdev->dev, "Driver v%s, firmware version %d.%d.%d\n",
		 QLCNIC_LINUX_VERSIONID, fw_major, fw_minor, fw_build);

	return adapter->fw_version;
}

static int __qlcnic_set_win_base(struct qlcnic_adapter *adapter, u32 addr)
{
	void __iomem *base;
	u32 val;

	base = adapter->ahw->pci_base0 +
	       QLC_83XX_CRB_WIN_FUNC(adapter->ahw->pci_func);
	writel(addr, base);
	val = readl(base);
	if (val != addr)
		return -EIO;

	return 0;
}

int qlcnic_83xx_rd_reg_indirect(struct qlcnic_adapter *adapter, ulong addr,
				int *err)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	*err = __qlcnic_set_win_base(adapter, (u32) addr);
	if (!*err) {
		return QLCRDX(ahw, QLCNIC_WILDCARD);
	} else {
		dev_err(&adapter->pdev->dev,
			"%s failed, addr = 0x%lx\n", __func__, addr);
		return -EIO;
	}
}

int qlcnic_83xx_wrt_reg_indirect(struct qlcnic_adapter *adapter, ulong addr,
				 u32 data)
{
	int err;
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	err = __qlcnic_set_win_base(adapter, (u32) addr);
	if (!err) {
		QLCWRX(ahw, QLCNIC_WILDCARD, data);
		return 0;
	} else {
		dev_err(&adapter->pdev->dev,
			"%s failed, addr = 0x%x data = 0x%x\n",
			__func__, (int)addr, data);
		return err;
	}
}

int qlcnic_83xx_setup_intr(struct qlcnic_adapter *adapter, u8 num_intr, int txq)
{
	int err, i, num_msix;
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	if (!num_intr)
		num_intr = QLCNIC_DEF_NUM_STS_DESC_RINGS;
	num_msix = rounddown_pow_of_two(min_t(int, num_online_cpus(),
					      num_intr));
	/* account for AEN interrupt MSI-X based interrupts */
	num_msix += 1;

	if (!(adapter->flags & QLCNIC_TX_INTR_SHARED))
		num_msix += adapter->max_drv_tx_rings;

	err = qlcnic_enable_msix(adapter, num_msix);
	if (err == -ENOMEM)
		return err;
	if (adapter->flags & QLCNIC_MSIX_ENABLED)
		num_msix = adapter->ahw->num_msix;
	else {
		if (qlcnic_sriov_vf_check(adapter))
			return -EINVAL;
		num_msix = 1;
	}
	/* setup interrupt mapping table for fw */
	ahw->intr_tbl = vzalloc(num_msix *
				sizeof(struct qlcnic_intrpt_config));
	if (!ahw->intr_tbl)
		return -ENOMEM;
	if (!(adapter->flags & QLCNIC_MSIX_ENABLED)) {
		/* MSI-X enablement failed, use legacy interrupt */
		adapter->tgt_status_reg = ahw->pci_base0 + QLC_83XX_INTX_PTR;
		adapter->tgt_mask_reg = ahw->pci_base0 + QLC_83XX_INTX_MASK;
		adapter->isr_int_vec = ahw->pci_base0 + QLC_83XX_INTX_TRGR;
		adapter->msix_entries[0].vector = adapter->pdev->irq;
		dev_info(&adapter->pdev->dev, "using legacy interrupt\n");
	}

	for (i = 0; i < num_msix; i++) {
		if (adapter->flags & QLCNIC_MSIX_ENABLED)
			ahw->intr_tbl[i].type = QLCNIC_INTRPT_MSIX;
		else
			ahw->intr_tbl[i].type = QLCNIC_INTRPT_INTX;
		ahw->intr_tbl[i].id = i;
		ahw->intr_tbl[i].src = 0;
	}
	return 0;
}

inline void qlcnic_83xx_clear_legacy_intr_mask(struct qlcnic_adapter *adapter)
{
	writel(0, adapter->tgt_mask_reg);
}

inline void qlcnic_83xx_set_legacy_intr_mask(struct qlcnic_adapter *adapter)
{
	writel(1, adapter->tgt_mask_reg);
}

/* Enable MSI-x and INT-x interrupts */
void qlcnic_83xx_enable_intr(struct qlcnic_adapter *adapter,
			     struct qlcnic_host_sds_ring *sds_ring)
{
	writel(0, sds_ring->crb_intr_mask);
}

/* Disable MSI-x and INT-x interrupts */
void qlcnic_83xx_disable_intr(struct qlcnic_adapter *adapter,
			      struct qlcnic_host_sds_ring *sds_ring)
{
	writel(1, sds_ring->crb_intr_mask);
}

inline void qlcnic_83xx_enable_legacy_msix_mbx_intr(struct qlcnic_adapter
						    *adapter)
{
	u32 mask;

	/* Mailbox in MSI-x mode and Legacy Interrupt share the same
	 * source register. We could be here before contexts are created
	 * and sds_ring->crb_intr_mask has not been initialized, calculate
	 * BAR offset for Interrupt Source Register
	 */
	mask = QLCRDX(adapter->ahw, QLCNIC_DEF_INT_MASK);
	writel(0, adapter->ahw->pci_base0 + mask);
}

void qlcnic_83xx_disable_mbx_intr(struct qlcnic_adapter *adapter)
{
	u32 mask;

	mask = QLCRDX(adapter->ahw, QLCNIC_DEF_INT_MASK);
	writel(1, adapter->ahw->pci_base0 + mask);
	QLCWRX(adapter->ahw, QLCNIC_MBX_INTR_ENBL, 0);
}

static inline void qlcnic_83xx_get_mbx_data(struct qlcnic_adapter *adapter,
				     struct qlcnic_cmd_args *cmd)
{
	int i;

	if (cmd->op_type == QLC_83XX_MBX_POST_BC_OP)
		return;

	for (i = 0; i < cmd->rsp.num; i++)
		cmd->rsp.arg[i] = readl(QLCNIC_MBX_FW(adapter->ahw, i));
}

irqreturn_t qlcnic_83xx_clear_legacy_intr(struct qlcnic_adapter *adapter)
{
	u32 intr_val;
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	int retries = 0;

	intr_val = readl(adapter->tgt_status_reg);

	if (!QLC_83XX_VALID_INTX_BIT31(intr_val))
		return IRQ_NONE;

	if (QLC_83XX_INTX_FUNC(intr_val) != adapter->ahw->pci_func) {
		adapter->stats.spurious_intr++;
		return IRQ_NONE;
	}
	/* The barrier is required to ensure writes to the registers */
	wmb();

	/* clear the interrupt trigger control register */
	writel(0, adapter->isr_int_vec);
	intr_val = readl(adapter->isr_int_vec);
	do {
		intr_val = readl(adapter->tgt_status_reg);
		if (QLC_83XX_INTX_FUNC(intr_val) != ahw->pci_func)
			break;
		retries++;
	} while (QLC_83XX_VALID_INTX_BIT30(intr_val) &&
		 (retries < QLC_83XX_LEGACY_INTX_MAX_RETRY));

	return IRQ_HANDLED;
}

static inline void qlcnic_83xx_notify_mbx_response(struct qlcnic_mailbox *mbx)
{
	atomic_set(&mbx->rsp_status, QLC_83XX_MBX_RESPONSE_ARRIVED);
	complete(&mbx->completion);
}

static void qlcnic_83xx_poll_process_aen(struct qlcnic_adapter *adapter)
{
	u32 resp, event, rsp_status = QLC_83XX_MBX_RESPONSE_ARRIVED;
	struct qlcnic_mailbox *mbx = adapter->ahw->mailbox;
	unsigned long flags;

	spin_lock_irqsave(&mbx->aen_lock, flags);
	resp = QLCRDX(adapter->ahw, QLCNIC_FW_MBX_CTRL);
	if (!(resp & QLCNIC_SET_OWNER))
		goto out;

	event = readl(QLCNIC_MBX_FW(adapter->ahw, 0));
	if (event &  QLCNIC_MBX_ASYNC_EVENT) {
		__qlcnic_83xx_process_aen(adapter);
	} else {
		if (atomic_read(&mbx->rsp_status) != rsp_status)
			qlcnic_83xx_notify_mbx_response(mbx);
	}
out:
	qlcnic_83xx_enable_legacy_msix_mbx_intr(adapter);
	spin_unlock_irqrestore(&mbx->aen_lock, flags);
}

irqreturn_t qlcnic_83xx_intr(int irq, void *data)
{
	struct qlcnic_adapter *adapter = data;
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	if (qlcnic_83xx_clear_legacy_intr(adapter) == IRQ_NONE)
		return IRQ_NONE;

	qlcnic_83xx_poll_process_aen(adapter);

	if (ahw->diag_test == QLCNIC_INTERRUPT_TEST) {
		ahw->diag_cnt++;
		qlcnic_83xx_enable_legacy_msix_mbx_intr(adapter);
		return IRQ_HANDLED;
	}

	if (!test_bit(__QLCNIC_DEV_UP, &adapter->state)) {
		qlcnic_83xx_enable_legacy_msix_mbx_intr(adapter);
	} else {
		sds_ring = &adapter->recv_ctx->sds_rings[0];
		napi_schedule(&sds_ring->napi);
	}

	return IRQ_HANDLED;
}

irqreturn_t qlcnic_83xx_tmp_intr(int irq, void *data)
{
	struct qlcnic_host_sds_ring *sds_ring = data;
	struct qlcnic_adapter *adapter = sds_ring->adapter;

	if (adapter->flags & QLCNIC_MSIX_ENABLED)
		goto done;

	if (adapter->nic_ops->clear_legacy_intr(adapter) == IRQ_NONE)
		return IRQ_NONE;

done:
	adapter->ahw->diag_cnt++;
	qlcnic_83xx_enable_intr(adapter, sds_ring);

	return IRQ_HANDLED;
}

void qlcnic_83xx_free_mbx_intr(struct qlcnic_adapter *adapter)
{
	u32 num_msix;

	if (!(adapter->flags & QLCNIC_MSIX_ENABLED))
		qlcnic_83xx_set_legacy_intr_mask(adapter);

	qlcnic_83xx_disable_mbx_intr(adapter);

	if (adapter->flags & QLCNIC_MSIX_ENABLED)
		num_msix = adapter->ahw->num_msix - 1;
	else
		num_msix = 0;

	msleep(20);
	synchronize_irq(adapter->msix_entries[num_msix].vector);
	free_irq(adapter->msix_entries[num_msix].vector, adapter);
}

int qlcnic_83xx_setup_mbx_intr(struct qlcnic_adapter *adapter)
{
	irq_handler_t handler;
	u32 val;
	int err = 0;
	unsigned long flags = 0;

	if (!(adapter->flags & QLCNIC_MSI_ENABLED) &&
	    !(adapter->flags & QLCNIC_MSIX_ENABLED))
		flags |= IRQF_SHARED;

	if (adapter->flags & QLCNIC_MSIX_ENABLED) {
		handler = qlcnic_83xx_handle_aen;
		val = adapter->msix_entries[adapter->ahw->num_msix - 1].vector;
		err = request_irq(val, handler, flags, "qlcnic-MB", adapter);
		if (err) {
			dev_err(&adapter->pdev->dev,
				"failed to register MBX interrupt\n");
			return err;
		}
	} else {
		handler = qlcnic_83xx_intr;
		val = adapter->msix_entries[0].vector;
		err = request_irq(val, handler, flags, "qlcnic", adapter);
		if (err) {
			dev_err(&adapter->pdev->dev,
				"failed to register INTx interrupt\n");
			return err;
		}
		qlcnic_83xx_clear_legacy_intr_mask(adapter);
	}

	/* Enable mailbox interrupt */
	qlcnic_83xx_enable_mbx_interrupt(adapter);

	return err;
}

void qlcnic_83xx_get_func_no(struct qlcnic_adapter *adapter)
{
	u32 val = QLCRDX(adapter->ahw, QLCNIC_INFORMANT);
	adapter->ahw->pci_func = (val >> 24) & 0xff;
}

int qlcnic_83xx_cam_lock(struct qlcnic_adapter *adapter)
{
	void __iomem *addr;
	u32 val, limit = 0;

	struct qlcnic_hardware_context *ahw = adapter->ahw;

	addr = ahw->pci_base0 + QLC_83XX_SEM_LOCK_FUNC(ahw->pci_func);
	do {
		val = readl(addr);
		if (val) {
			/* write the function number to register */
			QLC_SHARED_REG_WR32(adapter, QLCNIC_FLASH_LOCK_OWNER,
					    ahw->pci_func);
			return 0;
		}
		usleep_range(1000, 2000);
	} while (++limit <= QLCNIC_PCIE_SEM_TIMEOUT);

	return -EIO;
}

void qlcnic_83xx_cam_unlock(struct qlcnic_adapter *adapter)
{
	void __iomem *addr;
	u32 val;
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	addr = ahw->pci_base0 + QLC_83XX_SEM_UNLOCK_FUNC(ahw->pci_func);
	val = readl(addr);
}

void qlcnic_83xx_read_crb(struct qlcnic_adapter *adapter, char *buf,
			  loff_t offset, size_t size)
{
	int ret = 0;
	u32 data;

	if (qlcnic_api_lock(adapter)) {
		dev_err(&adapter->pdev->dev,
			"%s: failed to acquire lock. addr offset 0x%x\n",
			__func__, (u32)offset);
		return;
	}

	data = QLCRD32(adapter, (u32) offset, &ret);
	qlcnic_api_unlock(adapter);

	if (ret == -EIO) {
		dev_err(&adapter->pdev->dev,
			"%s: failed. addr offset 0x%x\n",
			__func__, (u32)offset);
		return;
	}
	memcpy(buf, &data, size);
}

void qlcnic_83xx_write_crb(struct qlcnic_adapter *adapter, char *buf,
			   loff_t offset, size_t size)
{
	u32 data;

	memcpy(&data, buf, size);
	qlcnic_83xx_wrt_reg_indirect(adapter, (u32) offset, data);
}

int qlcnic_83xx_get_port_info(struct qlcnic_adapter *adapter)
{
	int status;

	status = qlcnic_83xx_get_port_config(adapter);
	if (status) {
		dev_err(&adapter->pdev->dev,
			"Get Port Info failed\n");
	} else {
		if (QLC_83XX_SFP_10G_CAPABLE(adapter->ahw->port_config))
			adapter->ahw->port_type = QLCNIC_XGBE;
		else
			adapter->ahw->port_type = QLCNIC_GBE;

		if (QLC_83XX_AUTONEG(adapter->ahw->port_config))
			adapter->ahw->link_autoneg = AUTONEG_ENABLE;
	}
	return status;
}

void qlcnic_83xx_set_mac_filter_count(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	u16 act_pci_fn = ahw->act_pci_func;
	u16 count;

	ahw->max_mc_count = QLC_83XX_MAX_MC_COUNT;
	if (act_pci_fn <= 2)
		count = (QLC_83XX_MAX_UC_COUNT - QLC_83XX_MAX_MC_COUNT) /
			 act_pci_fn;
	else
		count = (QLC_83XX_LB_MAX_FILTERS - QLC_83XX_MAX_MC_COUNT) /
			 act_pci_fn;
	ahw->max_uc_count = count;
}

void qlcnic_83xx_enable_mbx_interrupt(struct qlcnic_adapter *adapter)
{
	u32 val;

	if (adapter->flags & QLCNIC_MSIX_ENABLED)
		val = BIT_2 | ((adapter->ahw->num_msix - 1) << 8);
	else
		val = BIT_2;

	QLCWRX(adapter->ahw, QLCNIC_MBX_INTR_ENBL, val);
	qlcnic_83xx_enable_legacy_msix_mbx_intr(adapter);
}

void qlcnic_83xx_check_vf(struct qlcnic_adapter *adapter,
			  const struct pci_device_id *ent)
{
	u32 op_mode, priv_level;
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	ahw->fw_hal_version = 2;
	qlcnic_get_func_no(adapter);

	if (qlcnic_sriov_vf_check(adapter)) {
		qlcnic_sriov_vf_set_ops(adapter);
		return;
	}

	/* Determine function privilege level */
	op_mode = QLCRDX(adapter->ahw, QLC_83XX_DRV_OP_MODE);
	if (op_mode == QLC_83XX_DEFAULT_OPMODE)
		priv_level = QLCNIC_MGMT_FUNC;
	else
		priv_level = QLC_83XX_GET_FUNC_PRIVILEGE(op_mode,
							 ahw->pci_func);

	if (priv_level == QLCNIC_NON_PRIV_FUNC) {
		ahw->op_mode = QLCNIC_NON_PRIV_FUNC;
		dev_info(&adapter->pdev->dev,
			 "HAL Version: %d Non Privileged function\n",
			 ahw->fw_hal_version);
		adapter->nic_ops = &qlcnic_vf_ops;
	} else {
		if (pci_find_ext_capability(adapter->pdev,
					    PCI_EXT_CAP_ID_SRIOV))
			set_bit(__QLCNIC_SRIOV_CAPABLE, &adapter->state);
		adapter->nic_ops = &qlcnic_83xx_ops;
	}
}

static void qlcnic_83xx_handle_link_aen(struct qlcnic_adapter *adapter,
					u32 data[]);
static void qlcnic_83xx_handle_idc_comp_aen(struct qlcnic_adapter *adapter,
					    u32 data[]);

void qlcnic_dump_mbx(struct qlcnic_adapter *adapter,
		     struct qlcnic_cmd_args *cmd)
{
	int i;

	if (cmd->op_type == QLC_83XX_MBX_POST_BC_OP)
		return;

	dev_info(&adapter->pdev->dev,
		 "Host MBX regs(%d)\n", cmd->req.num);
	for (i = 0; i < cmd->req.num; i++) {
		if (i && !(i % 8))
			pr_info("\n");
		pr_info("%08x ", cmd->req.arg[i]);
	}
	pr_info("\n");
	dev_info(&adapter->pdev->dev,
		 "FW MBX regs(%d)\n", cmd->rsp.num);
	for (i = 0; i < cmd->rsp.num; i++) {
		if (i && !(i % 8))
			pr_info("\n");
		pr_info("%08x ", cmd->rsp.arg[i]);
	}
	pr_info("\n");
}

static inline void
qlcnic_83xx_poll_for_mbx_completion(struct qlcnic_adapter *adapter,
				    struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	int opcode = LSW(cmd->req.arg[0]);
	unsigned long max_loops;

	max_loops = cmd->total_cmds * QLC_83XX_MBX_CMD_LOOP;

	for (; max_loops; max_loops--) {
		if (atomic_read(&cmd->rsp_status) ==
		    QLC_83XX_MBX_RESPONSE_ARRIVED)
			return;

		udelay(1);
	}

	dev_err(&adapter->pdev->dev,
		"%s: Mailbox command timed out, cmd_op=0x%x, cmd_type=0x%x, pci_func=0x%x, op_mode=0x%x\n",
		__func__, opcode, cmd->type, ahw->pci_func, ahw->op_mode);
	flush_workqueue(ahw->mailbox->work_q);
	return;
}

int qlcnic_83xx_issue_cmd(struct qlcnic_adapter *adapter,
			  struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_mailbox *mbx = adapter->ahw->mailbox;
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	int cmd_type, err, opcode;
	unsigned long timeout;

	opcode = LSW(cmd->req.arg[0]);
	cmd_type = cmd->type;
	err = mbx->ops->enqueue_cmd(adapter, cmd, &timeout);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"%s: Mailbox not available, cmd_op=0x%x, cmd_context=0x%x, pci_func=0x%x, op_mode=0x%x\n",
			__func__, opcode, cmd->type, ahw->pci_func,
			ahw->op_mode);
		return err;
	}

	switch (cmd_type) {
	case QLC_83XX_MBX_CMD_WAIT:
		if (!wait_for_completion_timeout(&cmd->completion, timeout)) {
			dev_err(&adapter->pdev->dev,
				"%s: Mailbox command timed out, cmd_op=0x%x, cmd_type=0x%x, pci_func=0x%x, op_mode=0x%x\n",
				__func__, opcode, cmd_type, ahw->pci_func,
				ahw->op_mode);
			flush_workqueue(mbx->work_q);
		}
		break;
	case QLC_83XX_MBX_CMD_NO_WAIT:
		return 0;
	case QLC_83XX_MBX_CMD_BUSY_WAIT:
		qlcnic_83xx_poll_for_mbx_completion(adapter, cmd);
		break;
	default:
		dev_err(&adapter->pdev->dev,
			"%s: Invalid mailbox command, cmd_op=0x%x, cmd_type=0x%x, pci_func=0x%x, op_mode=0x%x\n",
			__func__, opcode, cmd_type, ahw->pci_func,
			ahw->op_mode);
		qlcnic_83xx_detach_mailbox_work(adapter);
	}

	return cmd->rsp_opcode;
}

int qlcnic_83xx_alloc_mbx_args(struct qlcnic_cmd_args *mbx,
			       struct qlcnic_adapter *adapter, u32 type)
{
	int i, size;
	u32 temp;
	const struct qlcnic_mailbox_metadata *mbx_tbl;

	memset(mbx, 0, sizeof(struct qlcnic_cmd_args));
	mbx_tbl = qlcnic_83xx_mbx_tbl;
	size = ARRAY_SIZE(qlcnic_83xx_mbx_tbl);
	for (i = 0; i < size; i++) {
		if (type == mbx_tbl[i].cmd) {
			mbx->op_type = QLC_83XX_FW_MBX_CMD;
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
			temp = adapter->ahw->fw_hal_version << 29;
			mbx->req.arg[0] = (type | (mbx->req.num << 16) | temp);
			mbx->cmd_op = type;
			return 0;
		}
	}
	return -EINVAL;
}

void qlcnic_83xx_idc_aen_work(struct work_struct *work)
{
	struct qlcnic_adapter *adapter;
	struct qlcnic_cmd_args cmd;
	int i, err = 0;

	adapter = container_of(work, struct qlcnic_adapter, idc_aen_work.work);
	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_IDC_ACK);
	if (err)
		return;

	for (i = 1; i < QLC_83XX_MBX_AEN_CNT; i++)
		cmd.req.arg[i] = adapter->ahw->mbox_aen[i];

	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err)
		dev_info(&adapter->pdev->dev,
			 "%s: Mailbox IDC ACK failed.\n", __func__);
	qlcnic_free_mbx_args(&cmd);
}

static void qlcnic_83xx_handle_idc_comp_aen(struct qlcnic_adapter *adapter,
					    u32 data[])
{
	dev_dbg(&adapter->pdev->dev, "Completion AEN:0x%x.\n",
		QLCNIC_MBX_RSP(data[0]));
	clear_bit(QLC_83XX_IDC_COMP_AEN, &adapter->ahw->idc.status);
	return;
}

void __qlcnic_83xx_process_aen(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	u32 event[QLC_83XX_MBX_AEN_CNT];
	int i;

	for (i = 0; i < QLC_83XX_MBX_AEN_CNT; i++)
		event[i] = readl(QLCNIC_MBX_FW(ahw, i));

	switch (QLCNIC_MBX_RSP(event[0])) {

	case QLCNIC_MBX_LINK_EVENT:
		qlcnic_83xx_handle_link_aen(adapter, event);
		break;
	case QLCNIC_MBX_COMP_EVENT:
		qlcnic_83xx_handle_idc_comp_aen(adapter, event);
		break;
	case QLCNIC_MBX_REQUEST_EVENT:
		for (i = 0; i < QLC_83XX_MBX_AEN_CNT; i++)
			adapter->ahw->mbox_aen[i] = QLCNIC_MBX_RSP(event[i]);
		queue_delayed_work(adapter->qlcnic_wq,
				   &adapter->idc_aen_work, 0);
		break;
	case QLCNIC_MBX_TIME_EXTEND_EVENT:
		ahw->extend_lb_time = event[1] >> 8 & 0xf;
		break;
	case QLCNIC_MBX_BC_EVENT:
		qlcnic_sriov_handle_bc_event(adapter, event[1]);
		break;
	case QLCNIC_MBX_SFP_INSERT_EVENT:
		dev_info(&adapter->pdev->dev, "SFP+ Insert AEN:0x%x.\n",
			 QLCNIC_MBX_RSP(event[0]));
		break;
	case QLCNIC_MBX_SFP_REMOVE_EVENT:
		dev_info(&adapter->pdev->dev, "SFP Removed AEN:0x%x.\n",
			 QLCNIC_MBX_RSP(event[0]));
		break;
	default:
		dev_dbg(&adapter->pdev->dev, "Unsupported AEN:0x%x.\n",
			QLCNIC_MBX_RSP(event[0]));
		break;
	}

	QLCWRX(ahw, QLCNIC_FW_MBX_CTRL, QLCNIC_CLR_OWNER);
}

static void qlcnic_83xx_process_aen(struct qlcnic_adapter *adapter)
{
	u32 resp, event, rsp_status = QLC_83XX_MBX_RESPONSE_ARRIVED;
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlcnic_mailbox *mbx = ahw->mailbox;
	unsigned long flags;

	spin_lock_irqsave(&mbx->aen_lock, flags);
	resp = QLCRDX(ahw, QLCNIC_FW_MBX_CTRL);
	if (resp & QLCNIC_SET_OWNER) {
		event = readl(QLCNIC_MBX_FW(ahw, 0));
		if (event &  QLCNIC_MBX_ASYNC_EVENT) {
			__qlcnic_83xx_process_aen(adapter);
		} else {
			if (atomic_read(&mbx->rsp_status) != rsp_status)
				qlcnic_83xx_notify_mbx_response(mbx);
		}
	}
	spin_unlock_irqrestore(&mbx->aen_lock, flags);
}

static void qlcnic_83xx_mbx_poll_work(struct work_struct *work)
{
	struct qlcnic_adapter *adapter;

	adapter = container_of(work, struct qlcnic_adapter, mbx_poll_work.work);

	if (!test_bit(__QLCNIC_MBX_POLL_ENABLE, &adapter->state))
		return;

	qlcnic_83xx_process_aen(adapter);
	queue_delayed_work(adapter->qlcnic_wq, &adapter->mbx_poll_work,
			   (HZ / 10));
}

void qlcnic_83xx_enable_mbx_poll(struct qlcnic_adapter *adapter)
{
	if (test_and_set_bit(__QLCNIC_MBX_POLL_ENABLE, &adapter->state))
		return;

	INIT_DELAYED_WORK(&adapter->mbx_poll_work, qlcnic_83xx_mbx_poll_work);
	queue_delayed_work(adapter->qlcnic_wq, &adapter->mbx_poll_work, 0);
}

void qlcnic_83xx_disable_mbx_poll(struct qlcnic_adapter *adapter)
{
	if (!test_and_clear_bit(__QLCNIC_MBX_POLL_ENABLE, &adapter->state))
		return;
	cancel_delayed_work_sync(&adapter->mbx_poll_work);
}

static int qlcnic_83xx_add_rings(struct qlcnic_adapter *adapter)
{
	int index, i, err, sds_mbx_size;
	u32 *buf, intrpt_id, intr_mask;
	u16 context_id;
	u8 num_sds;
	struct qlcnic_cmd_args cmd;
	struct qlcnic_host_sds_ring *sds;
	struct qlcnic_sds_mbx sds_mbx;
	struct qlcnic_add_rings_mbx_out *mbx_out;
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	sds_mbx_size = sizeof(struct qlcnic_sds_mbx);
	context_id = recv_ctx->context_id;
	num_sds = (adapter->max_sds_rings - QLCNIC_MAX_RING_SETS);
	ahw->hw_ops->alloc_mbx_args(&cmd, adapter,
				    QLCNIC_CMD_ADD_RCV_RINGS);
	cmd.req.arg[1] = 0 | (num_sds << 8) | (context_id << 16);

	/* set up status rings, mbx 2-81 */
	index = 2;
	for (i = 8; i < adapter->max_sds_rings; i++) {
		memset(&sds_mbx, 0, sds_mbx_size);
		sds = &recv_ctx->sds_rings[i];
		sds->consumer = 0;
		memset(sds->desc_head, 0, STATUS_DESC_RINGSIZE(sds));
		sds_mbx.phy_addr_low = LSD(sds->phys_addr);
		sds_mbx.phy_addr_high = MSD(sds->phys_addr);
		sds_mbx.sds_ring_size = sds->num_desc;

		if (adapter->flags & QLCNIC_MSIX_ENABLED)
			intrpt_id = ahw->intr_tbl[i].id;
		else
			intrpt_id = QLCRDX(ahw, QLCNIC_DEF_INT_ID);

		if (adapter->ahw->diag_test != QLCNIC_LOOPBACK_TEST)
			sds_mbx.intrpt_id = intrpt_id;
		else
			sds_mbx.intrpt_id = 0xffff;
		sds_mbx.intrpt_val = 0;
		buf = &cmd.req.arg[index];
		memcpy(buf, &sds_mbx, sds_mbx_size);
		index += sds_mbx_size / sizeof(u32);
	}

	/* send the mailbox command */
	err = ahw->hw_ops->mbx_cmd(adapter, &cmd);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"Failed to add rings %d\n", err);
		goto out;
	}

	mbx_out = (struct qlcnic_add_rings_mbx_out *)&cmd.rsp.arg[1];
	index = 0;
	/* status descriptor ring */
	for (i = 8; i < adapter->max_sds_rings; i++) {
		sds = &recv_ctx->sds_rings[i];
		sds->crb_sts_consumer = ahw->pci_base0 +
					mbx_out->host_csmr[index];
		if (adapter->flags & QLCNIC_MSIX_ENABLED)
			intr_mask = ahw->intr_tbl[i].src;
		else
			intr_mask = QLCRDX(ahw, QLCNIC_DEF_INT_MASK);

		sds->crb_intr_mask = ahw->pci_base0 + intr_mask;
		index++;
	}
out:
	qlcnic_free_mbx_args(&cmd);
	return err;
}

void qlcnic_83xx_del_rx_ctx(struct qlcnic_adapter *adapter)
{
	int err;
	u32 temp = 0;
	struct qlcnic_cmd_args cmd;
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;

	if (qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_DESTROY_RX_CTX))
		return;

	if (qlcnic_sriov_pf_check(adapter) || qlcnic_sriov_vf_check(adapter))
		cmd.req.arg[0] |= (0x3 << 29);

	if (qlcnic_sriov_pf_check(adapter))
		qlcnic_pf_set_interface_id_del_rx_ctx(adapter, &temp);

	cmd.req.arg[1] = recv_ctx->context_id | temp;
	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err)
		dev_err(&adapter->pdev->dev,
			"Failed to destroy rx ctx in firmware\n");

	recv_ctx->state = QLCNIC_HOST_CTX_STATE_FREED;
	qlcnic_free_mbx_args(&cmd);
}

int qlcnic_83xx_create_rx_ctx(struct qlcnic_adapter *adapter)
{
	int i, err, index, sds_mbx_size, rds_mbx_size;
	u8 num_sds, num_rds;
	u32 *buf, intrpt_id, intr_mask, cap = 0;
	struct qlcnic_host_sds_ring *sds;
	struct qlcnic_host_rds_ring *rds;
	struct qlcnic_sds_mbx sds_mbx;
	struct qlcnic_rds_mbx rds_mbx;
	struct qlcnic_cmd_args cmd;
	struct qlcnic_rcv_mbx_out *mbx_out;
	struct qlcnic_recv_context *recv_ctx = adapter->recv_ctx;
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	num_rds = adapter->max_rds_rings;

	if (adapter->max_sds_rings <= QLCNIC_MAX_RING_SETS)
		num_sds = adapter->max_sds_rings;
	else
		num_sds = QLCNIC_MAX_RING_SETS;

	sds_mbx_size = sizeof(struct qlcnic_sds_mbx);
	rds_mbx_size = sizeof(struct qlcnic_rds_mbx);
	cap = QLCNIC_CAP0_LEGACY_CONTEXT;

	if (adapter->flags & QLCNIC_FW_LRO_MSS_CAP)
		cap |= QLC_83XX_FW_CAP_LRO_MSS;

	/* set mailbox hdr and capabilities */
	err = qlcnic_alloc_mbx_args(&cmd, adapter,
				    QLCNIC_CMD_CREATE_RX_CTX);
	if (err)
		return err;

	if (qlcnic_sriov_pf_check(adapter) || qlcnic_sriov_vf_check(adapter))
		cmd.req.arg[0] |= (0x3 << 29);

	cmd.req.arg[1] = cap;
	cmd.req.arg[5] = 1 | (num_rds << 5) | (num_sds << 8) |
			 (QLC_83XX_HOST_RDS_MODE_UNIQUE << 16);

	if (qlcnic_sriov_pf_check(adapter))
		qlcnic_pf_set_interface_id_create_rx_ctx(adapter,
							 &cmd.req.arg[6]);
	/* set up status rings, mbx 8-57/87 */
	index = QLC_83XX_HOST_SDS_MBX_IDX;
	for (i = 0; i < num_sds; i++) {
		memset(&sds_mbx, 0, sds_mbx_size);
		sds = &recv_ctx->sds_rings[i];
		sds->consumer = 0;
		memset(sds->desc_head, 0, STATUS_DESC_RINGSIZE(sds));
		sds_mbx.phy_addr_low = LSD(sds->phys_addr);
		sds_mbx.phy_addr_high = MSD(sds->phys_addr);
		sds_mbx.sds_ring_size = sds->num_desc;
		if (adapter->flags & QLCNIC_MSIX_ENABLED)
			intrpt_id = ahw->intr_tbl[i].id;
		else
			intrpt_id = QLCRDX(ahw, QLCNIC_DEF_INT_ID);
		if (adapter->ahw->diag_test != QLCNIC_LOOPBACK_TEST)
			sds_mbx.intrpt_id = intrpt_id;
		else
			sds_mbx.intrpt_id = 0xffff;
		sds_mbx.intrpt_val = 0;
		buf = &cmd.req.arg[index];
		memcpy(buf, &sds_mbx, sds_mbx_size);
		index += sds_mbx_size / sizeof(u32);
	}
	/* set up receive rings, mbx 88-111/135 */
	index = QLCNIC_HOST_RDS_MBX_IDX;
	rds = &recv_ctx->rds_rings[0];
	rds->producer = 0;
	memset(&rds_mbx, 0, rds_mbx_size);
	rds_mbx.phy_addr_reg_low = LSD(rds->phys_addr);
	rds_mbx.phy_addr_reg_high = MSD(rds->phys_addr);
	rds_mbx.reg_ring_sz = rds->dma_size;
	rds_mbx.reg_ring_len = rds->num_desc;
	/* Jumbo ring */
	rds = &recv_ctx->rds_rings[1];
	rds->producer = 0;
	rds_mbx.phy_addr_jmb_low = LSD(rds->phys_addr);
	rds_mbx.phy_addr_jmb_high = MSD(rds->phys_addr);
	rds_mbx.jmb_ring_sz = rds->dma_size;
	rds_mbx.jmb_ring_len = rds->num_desc;
	buf = &cmd.req.arg[index];
	memcpy(buf, &rds_mbx, rds_mbx_size);

	/* send the mailbox command */
	err = ahw->hw_ops->mbx_cmd(adapter, &cmd);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"Failed to create Rx ctx in firmware%d\n", err);
		goto out;
	}
	mbx_out = (struct qlcnic_rcv_mbx_out *)&cmd.rsp.arg[1];
	recv_ctx->context_id = mbx_out->ctx_id;
	recv_ctx->state = mbx_out->state;
	recv_ctx->virt_port = mbx_out->vport_id;
	dev_info(&adapter->pdev->dev, "Rx Context[%d] Created, state:0x%x\n",
		 recv_ctx->context_id, recv_ctx->state);
	/* Receive descriptor ring */
	/* Standard ring */
	rds = &recv_ctx->rds_rings[0];
	rds->crb_rcv_producer = ahw->pci_base0 +
				mbx_out->host_prod[0].reg_buf;
	/* Jumbo ring */
	rds = &recv_ctx->rds_rings[1];
	rds->crb_rcv_producer = ahw->pci_base0 +
				mbx_out->host_prod[0].jmb_buf;
	/* status descriptor ring */
	for (i = 0; i < num_sds; i++) {
		sds = &recv_ctx->sds_rings[i];
		sds->crb_sts_consumer = ahw->pci_base0 +
					mbx_out->host_csmr[i];
		if (adapter->flags & QLCNIC_MSIX_ENABLED)
			intr_mask = ahw->intr_tbl[i].src;
		else
			intr_mask = QLCRDX(ahw, QLCNIC_DEF_INT_MASK);
		sds->crb_intr_mask = ahw->pci_base0 + intr_mask;
	}

	if (adapter->max_sds_rings > QLCNIC_MAX_RING_SETS)
		err = qlcnic_83xx_add_rings(adapter);
out:
	qlcnic_free_mbx_args(&cmd);
	return err;
}

void qlcnic_83xx_del_tx_ctx(struct qlcnic_adapter *adapter,
			    struct qlcnic_host_tx_ring *tx_ring)
{
	struct qlcnic_cmd_args cmd;
	u32 temp = 0;

	if (qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_DESTROY_TX_CTX))
		return;

	if (qlcnic_sriov_pf_check(adapter) || qlcnic_sriov_vf_check(adapter))
		cmd.req.arg[0] |= (0x3 << 29);

	if (qlcnic_sriov_pf_check(adapter))
		qlcnic_pf_set_interface_id_del_tx_ctx(adapter, &temp);

	cmd.req.arg[1] = tx_ring->ctx_id | temp;
	if (qlcnic_issue_cmd(adapter, &cmd))
		dev_err(&adapter->pdev->dev,
			"Failed to destroy tx ctx in firmware\n");
	qlcnic_free_mbx_args(&cmd);
}

int qlcnic_83xx_create_tx_ctx(struct qlcnic_adapter *adapter,
			      struct qlcnic_host_tx_ring *tx, int ring)
{
	int err;
	u16 msix_id;
	u32 *buf, intr_mask, temp = 0;
	struct qlcnic_cmd_args cmd;
	struct qlcnic_tx_mbx mbx;
	struct qlcnic_tx_mbx_out *mbx_out;
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	u32 msix_vector;

	/* Reset host resources */
	tx->producer = 0;
	tx->sw_consumer = 0;
	*(tx->hw_consumer) = 0;

	memset(&mbx, 0, sizeof(struct qlcnic_tx_mbx));

	/* setup mailbox inbox registerss */
	mbx.phys_addr_low = LSD(tx->phys_addr);
	mbx.phys_addr_high = MSD(tx->phys_addr);
	mbx.cnsmr_index_low = LSD(tx->hw_cons_phys_addr);
	mbx.cnsmr_index_high = MSD(tx->hw_cons_phys_addr);
	mbx.size = tx->num_desc;
	if (adapter->flags & QLCNIC_MSIX_ENABLED) {
		if (!(adapter->flags & QLCNIC_TX_INTR_SHARED))
			msix_vector = adapter->max_sds_rings + ring;
		else
			msix_vector = adapter->max_sds_rings - 1;
		msix_id = ahw->intr_tbl[msix_vector].id;
	} else {
		msix_id = QLCRDX(ahw, QLCNIC_DEF_INT_ID);
	}

	if (adapter->ahw->diag_test != QLCNIC_LOOPBACK_TEST)
		mbx.intr_id = msix_id;
	else
		mbx.intr_id = 0xffff;
	mbx.src = 0;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_CREATE_TX_CTX);
	if (err)
		return err;

	if (qlcnic_sriov_pf_check(adapter) || qlcnic_sriov_vf_check(adapter))
		cmd.req.arg[0] |= (0x3 << 29);

	if (qlcnic_sriov_pf_check(adapter))
		qlcnic_pf_set_interface_id_create_tx_ctx(adapter, &temp);

	cmd.req.arg[1] = QLCNIC_CAP0_LEGACY_CONTEXT;
	cmd.req.arg[5] = QLCNIC_MAX_TX_QUEUES | temp;
	buf = &cmd.req.arg[6];
	memcpy(buf, &mbx, sizeof(struct qlcnic_tx_mbx));
	/* send the mailbox command*/
	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"Failed to create Tx ctx in firmware 0x%x\n", err);
		goto out;
	}
	mbx_out = (struct qlcnic_tx_mbx_out *)&cmd.rsp.arg[2];
	tx->crb_cmd_producer = ahw->pci_base0 + mbx_out->host_prod;
	tx->ctx_id = mbx_out->ctx_id;
	if ((adapter->flags & QLCNIC_MSIX_ENABLED) &&
	    !(adapter->flags & QLCNIC_TX_INTR_SHARED)) {
		intr_mask = ahw->intr_tbl[adapter->max_sds_rings + ring].src;
		tx->crb_intr_mask = ahw->pci_base0 + intr_mask;
	}
	dev_info(&adapter->pdev->dev, "Tx Context[0x%x] Created, state:0x%x\n",
		 tx->ctx_id, mbx_out->state);
out:
	qlcnic_free_mbx_args(&cmd);
	return err;
}

static int qlcnic_83xx_diag_alloc_res(struct net_device *netdev, int test,
				      int num_sds_ring)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_host_sds_ring *sds_ring;
	struct qlcnic_host_rds_ring *rds_ring;
	u16 adapter_state = adapter->is_up;
	u8 ring;
	int ret;

	netif_device_detach(netdev);

	if (netif_running(netdev))
		__qlcnic_down(adapter, netdev);

	qlcnic_detach(adapter);

	adapter->max_sds_rings = 1;
	adapter->ahw->diag_test = test;
	adapter->ahw->linkup = 0;

	ret = qlcnic_attach(adapter);
	if (ret) {
		netif_device_attach(netdev);
		return ret;
	}

	ret = qlcnic_fw_create_ctx(adapter);
	if (ret) {
		qlcnic_detach(adapter);
		if (adapter_state == QLCNIC_ADAPTER_UP_MAGIC) {
			adapter->max_sds_rings = num_sds_ring;
			qlcnic_attach(adapter);
		}
		netif_device_attach(netdev);
		return ret;
	}

	for (ring = 0; ring < adapter->max_rds_rings; ring++) {
		rds_ring = &adapter->recv_ctx->rds_rings[ring];
		qlcnic_post_rx_buffers(adapter, rds_ring, ring);
	}

	if (adapter->ahw->diag_test == QLCNIC_INTERRUPT_TEST) {
		for (ring = 0; ring < adapter->max_sds_rings; ring++) {
			sds_ring = &adapter->recv_ctx->sds_rings[ring];
			qlcnic_83xx_enable_intr(adapter, sds_ring);
		}
	}

	if (adapter->ahw->diag_test == QLCNIC_LOOPBACK_TEST) {
		/* disable and free mailbox interrupt */
		if (!(adapter->flags & QLCNIC_MSIX_ENABLED)) {
			qlcnic_83xx_enable_mbx_poll(adapter);
			qlcnic_83xx_free_mbx_intr(adapter);
		}
		adapter->ahw->loopback_state = 0;
		adapter->ahw->hw_ops->setup_link_event(adapter, 1);
	}

	set_bit(__QLCNIC_DEV_UP, &adapter->state);
	return 0;
}

static void qlcnic_83xx_diag_free_res(struct net_device *netdev,
					int max_sds_rings)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_host_sds_ring *sds_ring;
	int ring, err;

	clear_bit(__QLCNIC_DEV_UP, &adapter->state);
	if (adapter->ahw->diag_test == QLCNIC_INTERRUPT_TEST) {
		for (ring = 0; ring < adapter->max_sds_rings; ring++) {
			sds_ring = &adapter->recv_ctx->sds_rings[ring];
			qlcnic_83xx_disable_intr(adapter, sds_ring);
			if (!(adapter->flags & QLCNIC_MSIX_ENABLED))
				qlcnic_83xx_enable_mbx_poll(adapter);
		}
	}

	qlcnic_fw_destroy_ctx(adapter);
	qlcnic_detach(adapter);

	if (adapter->ahw->diag_test == QLCNIC_LOOPBACK_TEST) {
		if (!(adapter->flags & QLCNIC_MSIX_ENABLED)) {
			err = qlcnic_83xx_setup_mbx_intr(adapter);
			qlcnic_83xx_disable_mbx_poll(adapter);
			if (err) {
				dev_err(&adapter->pdev->dev,
					"%s: failed to setup mbx interrupt\n",
					__func__);
				goto out;
			}
		}
	}
	adapter->ahw->diag_test = 0;
	adapter->max_sds_rings = max_sds_rings;

	if (qlcnic_attach(adapter))
		goto out;

	if (netif_running(netdev))
		__qlcnic_up(adapter, netdev);

	if (adapter->ahw->diag_test == QLCNIC_INTERRUPT_TEST &&
	    !(adapter->flags & QLCNIC_MSIX_ENABLED))
		qlcnic_83xx_disable_mbx_poll(adapter);
out:
	netif_device_attach(netdev);
}

int qlcnic_83xx_config_led(struct qlcnic_adapter *adapter, u32 state,
			   u32 beacon)
{
	struct qlcnic_cmd_args cmd;
	u32 mbx_in;
	int i, status = 0;

	if (state) {
		/* Get LED configuration */
		status = qlcnic_alloc_mbx_args(&cmd, adapter,
					       QLCNIC_CMD_GET_LED_CONFIG);
		if (status)
			return status;

		status = qlcnic_issue_cmd(adapter, &cmd);
		if (status) {
			dev_err(&adapter->pdev->dev,
				"Get led config failed.\n");
			goto mbx_err;
		} else {
			for (i = 0; i < 4; i++)
				adapter->ahw->mbox_reg[i] = cmd.rsp.arg[i+1];
		}
		qlcnic_free_mbx_args(&cmd);
		/* Set LED Configuration */
		mbx_in = (LSW(QLC_83XX_LED_CONFIG) << 16) |
			  LSW(QLC_83XX_LED_CONFIG);
		status = qlcnic_alloc_mbx_args(&cmd, adapter,
					       QLCNIC_CMD_SET_LED_CONFIG);
		if (status)
			return status;

		cmd.req.arg[1] = mbx_in;
		cmd.req.arg[2] = mbx_in;
		cmd.req.arg[3] = mbx_in;
		if (beacon)
			cmd.req.arg[4] = QLC_83XX_ENABLE_BEACON;
		status = qlcnic_issue_cmd(adapter, &cmd);
		if (status) {
			dev_err(&adapter->pdev->dev,
				"Set led config failed.\n");
		}
mbx_err:
		qlcnic_free_mbx_args(&cmd);
		return status;

	} else {
		/* Restoring default LED configuration */
		status = qlcnic_alloc_mbx_args(&cmd, adapter,
					       QLCNIC_CMD_SET_LED_CONFIG);
		if (status)
			return status;

		cmd.req.arg[1] = adapter->ahw->mbox_reg[0];
		cmd.req.arg[2] = adapter->ahw->mbox_reg[1];
		cmd.req.arg[3] = adapter->ahw->mbox_reg[2];
		if (beacon)
			cmd.req.arg[4] = adapter->ahw->mbox_reg[3];
		status = qlcnic_issue_cmd(adapter, &cmd);
		if (status)
			dev_err(&adapter->pdev->dev,
				"Restoring led config failed.\n");
		qlcnic_free_mbx_args(&cmd);
		return status;
	}
}

int  qlcnic_83xx_set_led(struct net_device *netdev,
			 enum ethtool_phys_id_state state)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	int err = -EIO, active = 1;

	if (adapter->ahw->op_mode == QLCNIC_NON_PRIV_FUNC) {
		netdev_warn(netdev,
			    "LED test is not supported in non-privileged mode\n");
		return -EOPNOTSUPP;
	}

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		if (test_and_set_bit(__QLCNIC_LED_ENABLE, &adapter->state))
			return -EBUSY;

		if (test_bit(__QLCNIC_RESETTING, &adapter->state))
			break;

		err = qlcnic_83xx_config_led(adapter, active, 0);
		if (err)
			netdev_err(netdev, "Failed to set LED blink state\n");
		break;
	case ETHTOOL_ID_INACTIVE:
		active = 0;

		if (test_bit(__QLCNIC_RESETTING, &adapter->state))
			break;

		err = qlcnic_83xx_config_led(adapter, active, 0);
		if (err)
			netdev_err(netdev, "Failed to reset LED blink state\n");
		break;

	default:
		return -EINVAL;
	}

	if (!active || err)
		clear_bit(__QLCNIC_LED_ENABLE, &adapter->state);

	return err;
}

void qlcnic_83xx_register_nic_idc_func(struct qlcnic_adapter *adapter,
				       int enable)
{
	struct qlcnic_cmd_args cmd;
	int status;

	if (qlcnic_sriov_vf_check(adapter))
		return;

	if (enable) {
		status = qlcnic_alloc_mbx_args(&cmd, adapter,
					       QLCNIC_CMD_INIT_NIC_FUNC);
		if (status)
			return;

		cmd.req.arg[1] = BIT_0 | BIT_31;
	} else {
		status = qlcnic_alloc_mbx_args(&cmd, adapter,
					       QLCNIC_CMD_STOP_NIC_FUNC);
		if (status)
			return;

		cmd.req.arg[1] = BIT_0 | BIT_31;
	}
	status = qlcnic_issue_cmd(adapter, &cmd);
	if (status)
		dev_err(&adapter->pdev->dev,
			"Failed to %s in NIC IDC function event.\n",
			(enable ? "register" : "unregister"));

	qlcnic_free_mbx_args(&cmd);
}

int qlcnic_83xx_set_port_config(struct qlcnic_adapter *adapter)
{
	struct qlcnic_cmd_args cmd;
	int err;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_SET_PORT_CONFIG);
	if (err)
		return err;

	cmd.req.arg[1] = adapter->ahw->port_config;
	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err)
		dev_info(&adapter->pdev->dev, "Set Port Config failed.\n");
	qlcnic_free_mbx_args(&cmd);
	return err;
}

int qlcnic_83xx_get_port_config(struct qlcnic_adapter *adapter)
{
	struct qlcnic_cmd_args cmd;
	int err;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_GET_PORT_CONFIG);
	if (err)
		return err;

	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err)
		dev_info(&adapter->pdev->dev, "Get Port config failed\n");
	else
		adapter->ahw->port_config = cmd.rsp.arg[1];
	qlcnic_free_mbx_args(&cmd);
	return err;
}

int qlcnic_83xx_setup_link_event(struct qlcnic_adapter *adapter, int enable)
{
	int err;
	u32 temp;
	struct qlcnic_cmd_args cmd;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_GET_LINK_EVENT);
	if (err)
		return err;

	temp = adapter->recv_ctx->context_id << 16;
	cmd.req.arg[1] = (enable ? 1 : 0) | BIT_8 | temp;
	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err)
		dev_info(&adapter->pdev->dev,
			 "Setup linkevent mailbox failed\n");
	qlcnic_free_mbx_args(&cmd);
	return err;
}

static void qlcnic_83xx_set_interface_id_promisc(struct qlcnic_adapter *adapter,
						 u32 *interface_id)
{
	if (qlcnic_sriov_pf_check(adapter)) {
		qlcnic_pf_set_interface_id_promisc(adapter, interface_id);
	} else {
		if (!qlcnic_sriov_vf_check(adapter))
			*interface_id = adapter->recv_ctx->context_id << 16;
	}
}

int qlcnic_83xx_nic_set_promisc(struct qlcnic_adapter *adapter, u32 mode)
{
	struct qlcnic_cmd_args *cmd = NULL;
	u32 temp = 0;
	int err;

	if (adapter->recv_ctx->state == QLCNIC_HOST_CTX_STATE_FREED)
		return -EIO;

	cmd = kzalloc(sizeof(*cmd), GFP_ATOMIC);
	if (!cmd)
		return -ENOMEM;

	err = qlcnic_alloc_mbx_args(cmd, adapter,
				    QLCNIC_CMD_CONFIGURE_MAC_RX_MODE);
	if (err)
		goto out;

	cmd->type = QLC_83XX_MBX_CMD_NO_WAIT;
	qlcnic_83xx_set_interface_id_promisc(adapter, &temp);
	cmd->req.arg[1] = (mode ? 1 : 0) | temp;
	err = qlcnic_issue_cmd(adapter, cmd);
	if (!err)
		return err;

	qlcnic_free_mbx_args(cmd);

out:
	kfree(cmd);
	return err;
}

int qlcnic_83xx_loopback_test(struct net_device *netdev, u8 mode)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	int ret = 0, loop = 0, max_sds_rings = adapter->max_sds_rings;

	if (ahw->op_mode == QLCNIC_NON_PRIV_FUNC) {
		netdev_warn(netdev,
			    "Loopback test not supported in non privileged mode\n");
		return -ENOTSUPP;
	}

	if (test_bit(__QLCNIC_RESETTING, &adapter->state)) {
		netdev_info(netdev, "Device is resetting\n");
		return -EBUSY;
	}

	if (qlcnic_get_diag_lock(adapter)) {
		netdev_info(netdev, "Device is in diagnostics mode\n");
		return -EBUSY;
	}

	netdev_info(netdev, "%s loopback test in progress\n",
		    mode == QLCNIC_ILB_MODE ? "internal" : "external");

	ret = qlcnic_83xx_diag_alloc_res(netdev, QLCNIC_LOOPBACK_TEST,
					 max_sds_rings);
	if (ret)
		goto fail_diag_alloc;

	ret = qlcnic_83xx_set_lb_mode(adapter, mode);
	if (ret)
		goto free_diag_res;

	/* Poll for link up event before running traffic */
	do {
		msleep(QLC_83XX_LB_MSLEEP_COUNT);

		if (test_bit(__QLCNIC_RESETTING, &adapter->state)) {
			netdev_info(netdev,
				    "Device is resetting, free LB test resources\n");
			ret = -EBUSY;
			goto free_diag_res;
		}
		if (loop++ > QLC_83XX_LB_WAIT_COUNT) {
			netdev_info(netdev,
				    "Firmware didn't sent link up event to loopback request\n");
			ret = -ETIMEDOUT;
			qlcnic_83xx_clear_lb_mode(adapter, mode);
			goto free_diag_res;
		}
	} while ((adapter->ahw->linkup && ahw->has_link_events) != 1);

	/* Make sure carrier is off and queue is stopped during loopback */
	if (netif_running(netdev)) {
		netif_carrier_off(netdev);
		netif_tx_stop_all_queues(netdev);
	}

	ret = qlcnic_do_lb_test(adapter, mode);

	qlcnic_83xx_clear_lb_mode(adapter, mode);

free_diag_res:
	qlcnic_83xx_diag_free_res(netdev, max_sds_rings);

fail_diag_alloc:
	adapter->max_sds_rings = max_sds_rings;
	qlcnic_release_diag_lock(adapter);
	return ret;
}

static void qlcnic_extend_lb_idc_cmpltn_wait(struct qlcnic_adapter *adapter,
					     u32 *max_wait_count)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	int temp;

	netdev_info(adapter->netdev, "Recieved loopback IDC time extend event for 0x%x seconds\n",
		    ahw->extend_lb_time);
	temp = ahw->extend_lb_time * 1000;
	*max_wait_count += temp / QLC_83XX_LB_MSLEEP_COUNT;
	ahw->extend_lb_time = 0;
}

int qlcnic_83xx_set_lb_mode(struct qlcnic_adapter *adapter, u8 mode)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct net_device *netdev = adapter->netdev;
	u32 config, max_wait_count;
	int status = 0, loop = 0;

	ahw->extend_lb_time = 0;
	max_wait_count = QLC_83XX_LB_WAIT_COUNT;
	status = qlcnic_83xx_get_port_config(adapter);
	if (status)
		return status;

	config = ahw->port_config;

	/* Check if port is already in loopback mode */
	if ((config & QLC_83XX_CFG_LOOPBACK_HSS) ||
	    (config & QLC_83XX_CFG_LOOPBACK_EXT)) {
		netdev_err(netdev,
			   "Port already in Loopback mode.\n");
		return -EINPROGRESS;
	}

	set_bit(QLC_83XX_IDC_COMP_AEN, &ahw->idc.status);

	if (mode == QLCNIC_ILB_MODE)
		ahw->port_config |= QLC_83XX_CFG_LOOPBACK_HSS;
	if (mode == QLCNIC_ELB_MODE)
		ahw->port_config |= QLC_83XX_CFG_LOOPBACK_EXT;

	status = qlcnic_83xx_set_port_config(adapter);
	if (status) {
		netdev_err(netdev,
			   "Failed to Set Loopback Mode = 0x%x.\n",
			   ahw->port_config);
		ahw->port_config = config;
		clear_bit(QLC_83XX_IDC_COMP_AEN, &ahw->idc.status);
		return status;
	}

	/* Wait for Link and IDC Completion AEN */
	do {
		msleep(QLC_83XX_LB_MSLEEP_COUNT);

		if (test_bit(__QLCNIC_RESETTING, &adapter->state)) {
			netdev_info(netdev,
				    "Device is resetting, free LB test resources\n");
			clear_bit(QLC_83XX_IDC_COMP_AEN, &ahw->idc.status);
			return -EBUSY;
		}

		if (ahw->extend_lb_time)
			qlcnic_extend_lb_idc_cmpltn_wait(adapter,
							 &max_wait_count);

		if (loop++ > max_wait_count) {
			netdev_err(netdev, "%s: Did not receive loopback IDC completion AEN\n",
				   __func__);
			clear_bit(QLC_83XX_IDC_COMP_AEN, &ahw->idc.status);
			qlcnic_83xx_clear_lb_mode(adapter, mode);
			return -ETIMEDOUT;
		}
	} while (test_bit(QLC_83XX_IDC_COMP_AEN, &ahw->idc.status));

	qlcnic_sre_macaddr_change(adapter, adapter->mac_addr, 0,
				  QLCNIC_MAC_ADD);
	return status;
}

int qlcnic_83xx_clear_lb_mode(struct qlcnic_adapter *adapter, u8 mode)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	u32 config = ahw->port_config, max_wait_count;
	struct net_device *netdev = adapter->netdev;
	int status = 0, loop = 0;

	ahw->extend_lb_time = 0;
	max_wait_count = QLC_83XX_LB_WAIT_COUNT;
	set_bit(QLC_83XX_IDC_COMP_AEN, &ahw->idc.status);
	if (mode == QLCNIC_ILB_MODE)
		ahw->port_config &= ~QLC_83XX_CFG_LOOPBACK_HSS;
	if (mode == QLCNIC_ELB_MODE)
		ahw->port_config &= ~QLC_83XX_CFG_LOOPBACK_EXT;

	status = qlcnic_83xx_set_port_config(adapter);
	if (status) {
		netdev_err(netdev,
			   "Failed to Clear Loopback Mode = 0x%x.\n",
			   ahw->port_config);
		ahw->port_config = config;
		clear_bit(QLC_83XX_IDC_COMP_AEN, &ahw->idc.status);
		return status;
	}

	/* Wait for Link and IDC Completion AEN */
	do {
		msleep(QLC_83XX_LB_MSLEEP_COUNT);

		if (test_bit(__QLCNIC_RESETTING, &adapter->state)) {
			netdev_info(netdev,
				    "Device is resetting, free LB test resources\n");
			clear_bit(QLC_83XX_IDC_COMP_AEN, &ahw->idc.status);
			return -EBUSY;
		}

		if (ahw->extend_lb_time)
			qlcnic_extend_lb_idc_cmpltn_wait(adapter,
							 &max_wait_count);

		if (loop++ > max_wait_count) {
			netdev_err(netdev, "%s: Did not receive loopback IDC completion AEN\n",
				   __func__);
			clear_bit(QLC_83XX_IDC_COMP_AEN, &ahw->idc.status);
			return -ETIMEDOUT;
		}
	} while (test_bit(QLC_83XX_IDC_COMP_AEN, &ahw->idc.status));

	qlcnic_sre_macaddr_change(adapter, adapter->mac_addr, 0,
				  QLCNIC_MAC_DEL);
	return status;
}

static void qlcnic_83xx_set_interface_id_ipaddr(struct qlcnic_adapter *adapter,
						u32 *interface_id)
{
	if (qlcnic_sriov_pf_check(adapter)) {
		qlcnic_pf_set_interface_id_ipaddr(adapter, interface_id);
	} else {
		if (!qlcnic_sriov_vf_check(adapter))
			*interface_id = adapter->recv_ctx->context_id << 16;
	}
}

void qlcnic_83xx_config_ipaddr(struct qlcnic_adapter *adapter, __be32 ip,
			       int mode)
{
	int err;
	u32 temp = 0, temp_ip;
	struct qlcnic_cmd_args cmd;

	err = qlcnic_alloc_mbx_args(&cmd, adapter,
				    QLCNIC_CMD_CONFIGURE_IP_ADDR);
	if (err)
		return;

	qlcnic_83xx_set_interface_id_ipaddr(adapter, &temp);

	if (mode == QLCNIC_IP_UP)
		cmd.req.arg[1] = 1 | temp;
	else
		cmd.req.arg[1] = 2 | temp;

	/*
	 * Adapter needs IP address in network byte order.
	 * But hardware mailbox registers go through writel(), hence IP address
	 * gets swapped on big endian architecture.
	 * To negate swapping of writel() on big endian architecture
	 * use swab32(value).
	 */

	temp_ip = swab32(ntohl(ip));
	memcpy(&cmd.req.arg[2], &temp_ip, sizeof(u32));
	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err != QLCNIC_RCODE_SUCCESS)
		dev_err(&adapter->netdev->dev,
			"could not notify %s IP 0x%x request\n",
			(mode == QLCNIC_IP_UP) ? "Add" : "Remove", ip);

	qlcnic_free_mbx_args(&cmd);
}

int qlcnic_83xx_config_hw_lro(struct qlcnic_adapter *adapter, int mode)
{
	int err;
	u32 temp, arg1;
	struct qlcnic_cmd_args cmd;
	int lro_bit_mask;

	lro_bit_mask = (mode ? (BIT_0 | BIT_1 | BIT_2 | BIT_3) : 0);

	if (adapter->recv_ctx->state == QLCNIC_HOST_CTX_STATE_FREED)
		return 0;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_CONFIGURE_HW_LRO);
	if (err)
		return err;

	temp = adapter->recv_ctx->context_id << 16;
	arg1 = lro_bit_mask | temp;
	cmd.req.arg[1] = arg1;

	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err)
		dev_info(&adapter->pdev->dev, "LRO config failed\n");
	qlcnic_free_mbx_args(&cmd);

	return err;
}

int qlcnic_83xx_config_rss(struct qlcnic_adapter *adapter, int enable)
{
	int err;
	u32 word;
	struct qlcnic_cmd_args cmd;
	const u64 key[] = { 0xbeac01fa6a42b73bULL, 0x8030f20c77cb2da3ULL,
			    0xae7b30b4d0ca2bcbULL, 0x43a38fb04167253dULL,
			    0x255b0ec26d5a56daULL };

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_CONFIGURE_RSS);
	if (err)
		return err;
	/*
	 * RSS request:
	 * bits 3-0: Rsvd
	 *      5-4: hash_type_ipv4
	 *	7-6: hash_type_ipv6
	 *	  8: enable
	 *        9: use indirection table
	 *    16-31: indirection table mask
	 */
	word =  ((u32)(RSS_HASHTYPE_IP_TCP & 0x3) << 4) |
		((u32)(RSS_HASHTYPE_IP_TCP & 0x3) << 6) |
		((u32)(enable & 0x1) << 8) |
		((0x7ULL) << 16);
	cmd.req.arg[1] = (adapter->recv_ctx->context_id);
	cmd.req.arg[2] = word;
	memcpy(&cmd.req.arg[4], key, sizeof(key));

	err = qlcnic_issue_cmd(adapter, &cmd);

	if (err)
		dev_info(&adapter->pdev->dev, "RSS config failed\n");
	qlcnic_free_mbx_args(&cmd);

	return err;

}

static void qlcnic_83xx_set_interface_id_macaddr(struct qlcnic_adapter *adapter,
						 u32 *interface_id)
{
	if (qlcnic_sriov_pf_check(adapter)) {
		qlcnic_pf_set_interface_id_macaddr(adapter, interface_id);
	} else {
		if (!qlcnic_sriov_vf_check(adapter))
			*interface_id = adapter->recv_ctx->context_id << 16;
	}
}

int qlcnic_83xx_sre_macaddr_change(struct qlcnic_adapter *adapter, u8 *addr,
				   u16 vlan_id, u8 op)
{
	struct qlcnic_cmd_args *cmd = NULL;
	struct qlcnic_macvlan_mbx mv;
	u32 *buf, temp = 0;
	int err;

	if (adapter->recv_ctx->state == QLCNIC_HOST_CTX_STATE_FREED)
		return -EIO;

	cmd = kzalloc(sizeof(*cmd), GFP_ATOMIC);
	if (!cmd)
		return -ENOMEM;

	err = qlcnic_alloc_mbx_args(cmd, adapter, QLCNIC_CMD_CONFIG_MAC_VLAN);
	if (err)
		goto out;

	cmd->type = QLC_83XX_MBX_CMD_NO_WAIT;

	if (vlan_id)
		op = (op == QLCNIC_MAC_ADD || op == QLCNIC_MAC_VLAN_ADD) ?
		     QLCNIC_MAC_VLAN_ADD : QLCNIC_MAC_VLAN_DEL;

	cmd->req.arg[1] = op | (1 << 8);
	qlcnic_83xx_set_interface_id_macaddr(adapter, &temp);
	cmd->req.arg[1] |= temp;
	mv.vlan = vlan_id;
	mv.mac_addr0 = addr[0];
	mv.mac_addr1 = addr[1];
	mv.mac_addr2 = addr[2];
	mv.mac_addr3 = addr[3];
	mv.mac_addr4 = addr[4];
	mv.mac_addr5 = addr[5];
	buf = &cmd->req.arg[2];
	memcpy(buf, &mv, sizeof(struct qlcnic_macvlan_mbx));
	err = qlcnic_issue_cmd(adapter, cmd);
	if (!err)
		return err;

	qlcnic_free_mbx_args(cmd);
out:
	kfree(cmd);
	return err;
}

void qlcnic_83xx_change_l2_filter(struct qlcnic_adapter *adapter, u64 *addr,
				  u16 vlan_id)
{
	u8 mac[ETH_ALEN];
	memcpy(&mac, addr, ETH_ALEN);
	qlcnic_83xx_sre_macaddr_change(adapter, mac, vlan_id, QLCNIC_MAC_ADD);
}

void qlcnic_83xx_configure_mac(struct qlcnic_adapter *adapter, u8 *mac,
			       u8 type, struct qlcnic_cmd_args *cmd)
{
	switch (type) {
	case QLCNIC_SET_STATION_MAC:
	case QLCNIC_SET_FAC_DEF_MAC:
		memcpy(&cmd->req.arg[2], mac, sizeof(u32));
		memcpy(&cmd->req.arg[3], &mac[4], sizeof(u16));
		break;
	}
	cmd->req.arg[1] = type;
}

int qlcnic_83xx_get_mac_address(struct qlcnic_adapter *adapter, u8 *mac,
				u8 function)
{
	int err, i;
	struct qlcnic_cmd_args cmd;
	u32 mac_low, mac_high;

	function = 0;
	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_MAC_ADDRESS);
	if (err)
		return err;

	qlcnic_83xx_configure_mac(adapter, mac, QLCNIC_GET_CURRENT_MAC, &cmd);
	err = qlcnic_issue_cmd(adapter, &cmd);

	if (err == QLCNIC_RCODE_SUCCESS) {
		mac_low = cmd.rsp.arg[1];
		mac_high = cmd.rsp.arg[2];

		for (i = 0; i < 2; i++)
			mac[i] = (u8) (mac_high >> ((1 - i) * 8));
		for (i = 2; i < 6; i++)
			mac[i] = (u8) (mac_low >> ((5 - i) * 8));
	} else {
		dev_err(&adapter->pdev->dev, "Failed to get mac address%d\n",
			err);
		err = -EIO;
	}
	qlcnic_free_mbx_args(&cmd);
	return err;
}

void qlcnic_83xx_config_intr_coal(struct qlcnic_adapter *adapter)
{
	int err;
	u16 temp;
	struct qlcnic_cmd_args cmd;
	struct qlcnic_nic_intr_coalesce *coal = &adapter->ahw->coal;

	if (adapter->recv_ctx->state == QLCNIC_HOST_CTX_STATE_FREED)
		return;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_CONFIG_INTR_COAL);
	if (err)
		return;

	if (coal->type == QLCNIC_INTR_COAL_TYPE_RX) {
		temp = adapter->recv_ctx->context_id;
		cmd.req.arg[1] = QLCNIC_INTR_COAL_TYPE_RX | temp << 16;
		temp = coal->rx_time_us;
		cmd.req.arg[2] = coal->rx_packets | temp << 16;
	} else if (coal->type == QLCNIC_INTR_COAL_TYPE_TX) {
		temp = adapter->tx_ring->ctx_id;
		cmd.req.arg[1] = QLCNIC_INTR_COAL_TYPE_TX | temp << 16;
		temp = coal->tx_time_us;
		cmd.req.arg[2] = coal->tx_packets | temp << 16;
	}
	cmd.req.arg[3] = coal->flag;
	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err != QLCNIC_RCODE_SUCCESS)
		dev_info(&adapter->pdev->dev,
			 "Failed to send interrupt coalescence parameters\n");
	qlcnic_free_mbx_args(&cmd);
}

static void qlcnic_83xx_handle_link_aen(struct qlcnic_adapter *adapter,
					u32 data[])
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	u8 link_status, duplex;
	/* link speed */
	link_status = LSB(data[3]) & 1;
	if (link_status) {
		ahw->link_speed = MSW(data[2]);
		duplex = LSB(MSW(data[3]));
		if (duplex)
			ahw->link_duplex = DUPLEX_FULL;
		else
			ahw->link_duplex = DUPLEX_HALF;
	} else {
		ahw->link_speed = SPEED_UNKNOWN;
		ahw->link_duplex = DUPLEX_UNKNOWN;
	}

	ahw->link_autoneg = MSB(MSW(data[3]));
	ahw->module_type = MSB(LSW(data[3]));
	ahw->has_link_events = 1;
	qlcnic_advert_link_change(adapter, link_status);
}

irqreturn_t qlcnic_83xx_handle_aen(int irq, void *data)
{
	struct qlcnic_adapter *adapter = data;
	struct qlcnic_mailbox *mbx;
	u32 mask, resp, event;
	unsigned long flags;

	mbx = adapter->ahw->mailbox;
	spin_lock_irqsave(&mbx->aen_lock, flags);
	resp = QLCRDX(adapter->ahw, QLCNIC_FW_MBX_CTRL);
	if (!(resp & QLCNIC_SET_OWNER))
		goto out;

	event = readl(QLCNIC_MBX_FW(adapter->ahw, 0));
	if (event &  QLCNIC_MBX_ASYNC_EVENT)
		__qlcnic_83xx_process_aen(adapter);
	else
		qlcnic_83xx_notify_mbx_response(mbx);

out:
	mask = QLCRDX(adapter->ahw, QLCNIC_DEF_INT_MASK);
	writel(0, adapter->ahw->pci_base0 + mask);
	spin_unlock_irqrestore(&mbx->aen_lock, flags);
	return IRQ_HANDLED;
}

int qlcnic_enable_eswitch(struct qlcnic_adapter *adapter, u8 port, u8 enable)
{
	int err = -EIO;
	struct qlcnic_cmd_args cmd;

	if (adapter->ahw->op_mode != QLCNIC_MGMT_FUNC) {
		dev_err(&adapter->pdev->dev,
			"%s: Error, invoked by non management func\n",
			__func__);
		return err;
	}

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_TOGGLE_ESWITCH);
	if (err)
		return err;

	cmd.req.arg[1] = (port & 0xf) | BIT_4;
	err = qlcnic_issue_cmd(adapter, &cmd);

	if (err != QLCNIC_RCODE_SUCCESS) {
		dev_err(&adapter->pdev->dev, "Failed to enable eswitch%d\n",
			err);
		err = -EIO;
	}
	qlcnic_free_mbx_args(&cmd);

	return err;

}

int qlcnic_83xx_set_nic_info(struct qlcnic_adapter *adapter,
			     struct qlcnic_info *nic)
{
	int i, err = -EIO;
	struct qlcnic_cmd_args cmd;

	if (adapter->ahw->op_mode != QLCNIC_MGMT_FUNC) {
		dev_err(&adapter->pdev->dev,
			"%s: Error, invoked by non management func\n",
			__func__);
		return err;
	}

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_SET_NIC_INFO);
	if (err)
		return err;

	cmd.req.arg[1] = (nic->pci_func << 16);
	cmd.req.arg[2] = 0x1 << 16;
	cmd.req.arg[3] = nic->phys_port | (nic->switch_mode << 16);
	cmd.req.arg[4] = nic->capabilities;
	cmd.req.arg[5] = (nic->max_mac_filters & 0xFF) | ((nic->max_mtu) << 16);
	cmd.req.arg[6] = (nic->max_tx_ques) | ((nic->max_rx_ques) << 16);
	cmd.req.arg[7] = (nic->min_tx_bw) | ((nic->max_tx_bw) << 16);
	for (i = 8; i < 32; i++)
		cmd.req.arg[i] = 0;

	err = qlcnic_issue_cmd(adapter, &cmd);

	if (err != QLCNIC_RCODE_SUCCESS) {
		dev_err(&adapter->pdev->dev, "Failed to set nic info%d\n",
			err);
		err = -EIO;
	}

	qlcnic_free_mbx_args(&cmd);

	return err;
}

int qlcnic_83xx_get_nic_info(struct qlcnic_adapter *adapter,
			     struct qlcnic_info *npar_info, u8 func_id)
{
	int err;
	u32 temp;
	u8 op = 0;
	struct qlcnic_cmd_args cmd;
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_GET_NIC_INFO);
	if (err)
		return err;

	if (func_id != ahw->pci_func) {
		temp = func_id << 16;
		cmd.req.arg[1] = op | BIT_31 | temp;
	} else {
		cmd.req.arg[1] = ahw->pci_func << 16;
	}
	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err) {
		dev_info(&adapter->pdev->dev,
			 "Failed to get nic info %d\n", err);
		goto out;
	}

	npar_info->op_type = cmd.rsp.arg[1];
	npar_info->pci_func = cmd.rsp.arg[2] & 0xFFFF;
	npar_info->op_mode = (cmd.rsp.arg[2] & 0xFFFF0000) >> 16;
	npar_info->phys_port = cmd.rsp.arg[3] & 0xFFFF;
	npar_info->switch_mode = (cmd.rsp.arg[3] & 0xFFFF0000) >> 16;
	npar_info->capabilities = cmd.rsp.arg[4];
	npar_info->max_mac_filters = cmd.rsp.arg[5] & 0xFF;
	npar_info->max_mtu = (cmd.rsp.arg[5] & 0xFFFF0000) >> 16;
	npar_info->max_tx_ques = cmd.rsp.arg[6] & 0xFFFF;
	npar_info->max_rx_ques = (cmd.rsp.arg[6] & 0xFFFF0000) >> 16;
	npar_info->min_tx_bw = cmd.rsp.arg[7] & 0xFFFF;
	npar_info->max_tx_bw = (cmd.rsp.arg[7] & 0xFFFF0000) >> 16;
	if (cmd.rsp.arg[8] & 0x1)
		npar_info->max_bw_reg_offset = (cmd.rsp.arg[8] & 0x7FFE) >> 1;
	if (cmd.rsp.arg[8] & 0x10000) {
		temp = (cmd.rsp.arg[8] & 0x7FFE0000) >> 17;
		npar_info->max_linkspeed_reg_offset = temp;
	}
	if (npar_info->capabilities & QLCNIC_FW_CAPABILITY_MORE_CAPS)
		memcpy(ahw->extra_capability, &cmd.rsp.arg[16],
		       sizeof(ahw->extra_capability));

out:
	qlcnic_free_mbx_args(&cmd);
	return err;
}

int qlcnic_83xx_get_pci_info(struct qlcnic_adapter *adapter,
			     struct qlcnic_pci_info *pci_info)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct device *dev = &adapter->pdev->dev;
	struct qlcnic_cmd_args cmd;
	int i, err = 0, j = 0;
	u32 temp;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_GET_PCI_INFO);
	if (err)
		return err;

	err = qlcnic_issue_cmd(adapter, &cmd);

	ahw->act_pci_func = 0;
	if (err == QLCNIC_RCODE_SUCCESS) {
		ahw->max_pci_func = cmd.rsp.arg[1] & 0xFF;
		for (i = 2, j = 0; j < QLCNIC_MAX_PCI_FUNC; j++, pci_info++) {
			pci_info->id = cmd.rsp.arg[i] & 0xFFFF;
			pci_info->active = (cmd.rsp.arg[i] & 0xFFFF0000) >> 16;
			i++;
			pci_info->type = cmd.rsp.arg[i] & 0xFFFF;
			if (pci_info->type == QLCNIC_TYPE_NIC)
				ahw->act_pci_func++;
			temp = (cmd.rsp.arg[i] & 0xFFFF0000) >> 16;
			pci_info->default_port = temp;
			i++;
			pci_info->tx_min_bw = cmd.rsp.arg[i] & 0xFFFF;
			temp = (cmd.rsp.arg[i] & 0xFFFF0000) >> 16;
			pci_info->tx_max_bw = temp;
			i = i + 2;
			memcpy(pci_info->mac, &cmd.rsp.arg[i], ETH_ALEN - 2);
			i++;
			memcpy(pci_info->mac + sizeof(u32), &cmd.rsp.arg[i], 2);
			i = i + 3;
			if (ahw->op_mode == QLCNIC_MGMT_FUNC)
				dev_info(dev, "id = %d active = %d type = %d\n"
					 "\tport = %d min bw = %d max bw = %d\n"
					 "\tmac_addr =  %pM\n", pci_info->id,
					 pci_info->active, pci_info->type,
					 pci_info->default_port,
					 pci_info->tx_min_bw,
					 pci_info->tx_max_bw, pci_info->mac);
		}
		if (ahw->op_mode == QLCNIC_MGMT_FUNC)
			dev_info(dev, "Max vNIC functions = %d, active vNIC functions = %d\n",
				 ahw->max_pci_func, ahw->act_pci_func);

	} else {
		dev_err(dev, "Failed to get PCI Info, error = %d\n", err);
		err = -EIO;
	}

	qlcnic_free_mbx_args(&cmd);

	return err;
}

int qlcnic_83xx_config_intrpt(struct qlcnic_adapter *adapter, bool op_type)
{
	int i, index, err;
	u8 max_ints;
	u32 val, temp, type;
	struct qlcnic_cmd_args cmd;

	max_ints = adapter->ahw->num_msix - 1;
	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_CONFIG_INTRPT);
	if (err)
		return err;

	cmd.req.arg[1] = max_ints;

	if (qlcnic_sriov_vf_check(adapter))
		cmd.req.arg[1] |= (adapter->ahw->pci_func << 8) | BIT_16;

	for (i = 0, index = 2; i < max_ints; i++) {
		type = op_type ? QLCNIC_INTRPT_ADD : QLCNIC_INTRPT_DEL;
		val = type | (adapter->ahw->intr_tbl[i].type << 4);
		if (adapter->ahw->intr_tbl[i].type == QLCNIC_INTRPT_MSIX)
			val |= (adapter->ahw->intr_tbl[i].id << 16);
		cmd.req.arg[index++] = val;
	}
	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"Failed to configure interrupts 0x%x\n", err);
		goto out;
	}

	max_ints = cmd.rsp.arg[1];
	for (i = 0, index = 2; i < max_ints; i++, index += 2) {
		val = cmd.rsp.arg[index];
		if (LSB(val)) {
			dev_info(&adapter->pdev->dev,
				 "Can't configure interrupt %d\n",
				 adapter->ahw->intr_tbl[i].id);
			continue;
		}
		if (op_type) {
			adapter->ahw->intr_tbl[i].id = MSW(val);
			adapter->ahw->intr_tbl[i].enabled = 1;
			temp = cmd.rsp.arg[index + 1];
			adapter->ahw->intr_tbl[i].src = temp;
		} else {
			adapter->ahw->intr_tbl[i].id = i;
			adapter->ahw->intr_tbl[i].enabled = 0;
			adapter->ahw->intr_tbl[i].src = 0;
		}
	}
out:
	qlcnic_free_mbx_args(&cmd);
	return err;
}

int qlcnic_83xx_lock_flash(struct qlcnic_adapter *adapter)
{
	int id, timeout = 0;
	u32 status = 0;

	while (status == 0) {
		status = QLC_SHARED_REG_RD32(adapter, QLCNIC_FLASH_LOCK);
		if (status)
			break;

		if (++timeout >= QLC_83XX_FLASH_LOCK_TIMEOUT) {
			id = QLC_SHARED_REG_RD32(adapter,
						 QLCNIC_FLASH_LOCK_OWNER);
			dev_err(&adapter->pdev->dev,
				"%s: failed, lock held by %d\n", __func__, id);
			return -EIO;
		}
		usleep_range(1000, 2000);
	}

	QLC_SHARED_REG_WR32(adapter, QLCNIC_FLASH_LOCK_OWNER, adapter->portnum);
	return 0;
}

void qlcnic_83xx_unlock_flash(struct qlcnic_adapter *adapter)
{
	QLC_SHARED_REG_RD32(adapter, QLCNIC_FLASH_UNLOCK);
	QLC_SHARED_REG_WR32(adapter, QLCNIC_FLASH_LOCK_OWNER, 0xFF);
}

int qlcnic_83xx_lockless_flash_read32(struct qlcnic_adapter *adapter,
				      u32 flash_addr, u8 *p_data,
				      int count)
{
	u32 word, range, flash_offset, addr = flash_addr, ret;
	ulong indirect_add, direct_window;
	int i, err = 0;

	flash_offset = addr & (QLCNIC_FLASH_SECTOR_SIZE - 1);
	if (addr & 0x3) {
		dev_err(&adapter->pdev->dev, "Illegal addr = 0x%x\n", addr);
		return -EIO;
	}

	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_DIRECT_WINDOW,
				     (addr));

	range = flash_offset + (count * sizeof(u32));
	/* Check if data is spread across multiple sectors */
	if (range > (QLCNIC_FLASH_SECTOR_SIZE - 1)) {

		/* Multi sector read */
		for (i = 0; i < count; i++) {
			indirect_add = QLC_83XX_FLASH_DIRECT_DATA(addr);
			ret = QLCRD32(adapter, indirect_add, &err);
			if (err == -EIO)
				return err;

			word = ret;
			*(u32 *)p_data  = word;
			p_data = p_data + 4;
			addr = addr + 4;
			flash_offset = flash_offset + 4;

			if (flash_offset > (QLCNIC_FLASH_SECTOR_SIZE - 1)) {
				direct_window = QLC_83XX_FLASH_DIRECT_WINDOW;
				/* This write is needed once for each sector */
				qlcnic_83xx_wrt_reg_indirect(adapter,
							     direct_window,
							     (addr));
				flash_offset = 0;
			}
		}
	} else {
		/* Single sector read */
		for (i = 0; i < count; i++) {
			indirect_add = QLC_83XX_FLASH_DIRECT_DATA(addr);
			ret = QLCRD32(adapter, indirect_add, &err);
			if (err == -EIO)
				return err;

			word = ret;
			*(u32 *)p_data  = word;
			p_data = p_data + 4;
			addr = addr + 4;
		}
	}

	return 0;
}

static int qlcnic_83xx_poll_flash_status_reg(struct qlcnic_adapter *adapter)
{
	u32 status;
	int retries = QLC_83XX_FLASH_READ_RETRY_COUNT;
	int err = 0;

	do {
		status = QLCRD32(adapter, QLC_83XX_FLASH_STATUS, &err);
		if (err == -EIO)
			return err;

		if ((status & QLC_83XX_FLASH_STATUS_READY) ==
		    QLC_83XX_FLASH_STATUS_READY)
			break;

		msleep(QLC_83XX_FLASH_STATUS_REG_POLL_DELAY);
	} while (--retries);

	if (!retries)
		return -EIO;

	return 0;
}

int qlcnic_83xx_enable_flash_write(struct qlcnic_adapter *adapter)
{
	int ret;
	u32 cmd;
	cmd = adapter->ahw->fdt.write_statusreg_cmd;
	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_ADDR,
				     (QLC_83XX_FLASH_FDT_WRITE_DEF_SIG | cmd));
	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_WRDATA,
				     adapter->ahw->fdt.write_enable_bits);
	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_CONTROL,
				     QLC_83XX_FLASH_SECOND_ERASE_MS_VAL);
	ret = qlcnic_83xx_poll_flash_status_reg(adapter);
	if (ret)
		return -EIO;

	return 0;
}

int qlcnic_83xx_disable_flash_write(struct qlcnic_adapter *adapter)
{
	int ret;

	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_ADDR,
				     (QLC_83XX_FLASH_FDT_WRITE_DEF_SIG |
				     adapter->ahw->fdt.write_statusreg_cmd));
	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_WRDATA,
				     adapter->ahw->fdt.write_disable_bits);
	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_CONTROL,
				     QLC_83XX_FLASH_SECOND_ERASE_MS_VAL);
	ret = qlcnic_83xx_poll_flash_status_reg(adapter);
	if (ret)
		return -EIO;

	return 0;
}

int qlcnic_83xx_read_flash_mfg_id(struct qlcnic_adapter *adapter)
{
	int ret, err = 0;
	u32 mfg_id;

	if (qlcnic_83xx_lock_flash(adapter))
		return -EIO;

	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_ADDR,
				     QLC_83XX_FLASH_FDT_READ_MFG_ID_VAL);
	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_CONTROL,
				     QLC_83XX_FLASH_READ_CTRL);
	ret = qlcnic_83xx_poll_flash_status_reg(adapter);
	if (ret) {
		qlcnic_83xx_unlock_flash(adapter);
		return -EIO;
	}

	mfg_id = QLCRD32(adapter, QLC_83XX_FLASH_RDDATA, &err);
	if (err == -EIO) {
		qlcnic_83xx_unlock_flash(adapter);
		return err;
	}

	adapter->flash_mfg_id = (mfg_id & 0xFF);
	qlcnic_83xx_unlock_flash(adapter);

	return 0;
}

int qlcnic_83xx_read_flash_descriptor_table(struct qlcnic_adapter *adapter)
{
	int count, fdt_size, ret = 0;

	fdt_size = sizeof(struct qlcnic_fdt);
	count = fdt_size / sizeof(u32);

	if (qlcnic_83xx_lock_flash(adapter))
		return -EIO;

	memset(&adapter->ahw->fdt, 0, fdt_size);
	ret = qlcnic_83xx_lockless_flash_read32(adapter, QLCNIC_FDT_LOCATION,
						(u8 *)&adapter->ahw->fdt,
						count);

	qlcnic_83xx_unlock_flash(adapter);
	return ret;
}

int qlcnic_83xx_erase_flash_sector(struct qlcnic_adapter *adapter,
				   u32 sector_start_addr)
{
	u32 reversed_addr, addr1, addr2, cmd;
	int ret = -EIO;

	if (qlcnic_83xx_lock_flash(adapter) != 0)
		return -EIO;

	if (adapter->ahw->fdt.mfg_id == adapter->flash_mfg_id) {
		ret = qlcnic_83xx_enable_flash_write(adapter);
		if (ret) {
			qlcnic_83xx_unlock_flash(adapter);
			dev_err(&adapter->pdev->dev,
				"%s failed at %d\n",
				__func__, __LINE__);
			return ret;
		}
	}

	ret = qlcnic_83xx_poll_flash_status_reg(adapter);
	if (ret) {
		qlcnic_83xx_unlock_flash(adapter);
		dev_err(&adapter->pdev->dev,
			"%s: failed at %d\n", __func__, __LINE__);
		return -EIO;
	}

	addr1 = (sector_start_addr & 0xFF) << 16;
	addr2 = (sector_start_addr & 0xFF0000) >> 16;
	reversed_addr = addr1 | addr2;

	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_WRDATA,
				     reversed_addr);
	cmd = QLC_83XX_FLASH_FDT_ERASE_DEF_SIG | adapter->ahw->fdt.erase_cmd;
	if (adapter->ahw->fdt.mfg_id == adapter->flash_mfg_id)
		qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_ADDR, cmd);
	else
		qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_ADDR,
					     QLC_83XX_FLASH_OEM_ERASE_SIG);
	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_CONTROL,
				     QLC_83XX_FLASH_LAST_ERASE_MS_VAL);

	ret = qlcnic_83xx_poll_flash_status_reg(adapter);
	if (ret) {
		qlcnic_83xx_unlock_flash(adapter);
		dev_err(&adapter->pdev->dev,
			"%s: failed at %d\n", __func__, __LINE__);
		return -EIO;
	}

	if (adapter->ahw->fdt.mfg_id == adapter->flash_mfg_id) {
		ret = qlcnic_83xx_disable_flash_write(adapter);
		if (ret) {
			qlcnic_83xx_unlock_flash(adapter);
			dev_err(&adapter->pdev->dev,
				"%s: failed at %d\n", __func__, __LINE__);
			return ret;
		}
	}

	qlcnic_83xx_unlock_flash(adapter);

	return 0;
}

int qlcnic_83xx_flash_write32(struct qlcnic_adapter *adapter, u32 addr,
			      u32 *p_data)
{
	int ret = -EIO;
	u32 addr1 = 0x00800000 | (addr >> 2);

	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_ADDR, addr1);
	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_WRDATA, *p_data);
	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_CONTROL,
				     QLC_83XX_FLASH_LAST_ERASE_MS_VAL);
	ret = qlcnic_83xx_poll_flash_status_reg(adapter);
	if (ret) {
		dev_err(&adapter->pdev->dev,
			"%s: failed at %d\n", __func__, __LINE__);
		return -EIO;
	}

	return 0;
}

int qlcnic_83xx_flash_bulk_write(struct qlcnic_adapter *adapter, u32 addr,
				 u32 *p_data, int count)
{
	u32 temp;
	int ret = -EIO, err = 0;

	if ((count < QLC_83XX_FLASH_WRITE_MIN) ||
	    (count > QLC_83XX_FLASH_WRITE_MAX)) {
		dev_err(&adapter->pdev->dev,
			"%s: Invalid word count\n", __func__);
		return -EIO;
	}

	temp = QLCRD32(adapter, QLC_83XX_FLASH_SPI_CONTROL, &err);
	if (err == -EIO)
		return err;

	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_SPI_CONTROL,
				     (temp | QLC_83XX_FLASH_SPI_CTRL));
	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_ADDR,
				     QLC_83XX_FLASH_ADDR_TEMP_VAL);

	/* First DWORD write */
	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_WRDATA, *p_data++);
	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_CONTROL,
				     QLC_83XX_FLASH_FIRST_MS_PATTERN);
	ret = qlcnic_83xx_poll_flash_status_reg(adapter);
	if (ret) {
		dev_err(&adapter->pdev->dev,
			"%s: failed at %d\n", __func__, __LINE__);
		return -EIO;
	}

	count--;
	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_ADDR,
				     QLC_83XX_FLASH_ADDR_SECOND_TEMP_VAL);
	/* Second to N-1 DWORD writes */
	while (count != 1) {
		qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_WRDATA,
					     *p_data++);
		qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_CONTROL,
					     QLC_83XX_FLASH_SECOND_MS_PATTERN);
		ret = qlcnic_83xx_poll_flash_status_reg(adapter);
		if (ret) {
			dev_err(&adapter->pdev->dev,
				"%s: failed at %d\n", __func__, __LINE__);
			return -EIO;
		}
		count--;
	}

	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_ADDR,
				     QLC_83XX_FLASH_ADDR_TEMP_VAL |
				     (addr >> 2));
	/* Last DWORD write */
	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_WRDATA, *p_data++);
	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_CONTROL,
				     QLC_83XX_FLASH_LAST_MS_PATTERN);
	ret = qlcnic_83xx_poll_flash_status_reg(adapter);
	if (ret) {
		dev_err(&adapter->pdev->dev,
			"%s: failed at %d\n", __func__, __LINE__);
		return -EIO;
	}

	ret = QLCRD32(adapter, QLC_83XX_FLASH_SPI_STATUS, &err);
	if (err == -EIO)
		return err;

	if ((ret & QLC_83XX_FLASH_SPI_CTRL) == QLC_83XX_FLASH_SPI_CTRL) {
		dev_err(&adapter->pdev->dev, "%s: failed at %d\n",
			__func__, __LINE__);
		/* Operation failed, clear error bit */
		temp = QLCRD32(adapter, QLC_83XX_FLASH_SPI_CONTROL, &err);
		if (err == -EIO)
			return err;

		qlcnic_83xx_wrt_reg_indirect(adapter,
					     QLC_83XX_FLASH_SPI_CONTROL,
					     (temp | QLC_83XX_FLASH_SPI_CTRL));
	}

	return 0;
}

static void qlcnic_83xx_recover_driver_lock(struct qlcnic_adapter *adapter)
{
	u32 val, id;

	val = QLCRDX(adapter->ahw, QLC_83XX_RECOVER_DRV_LOCK);

	/* Check if recovery need to be performed by the calling function */
	if ((val & QLC_83XX_DRV_LOCK_RECOVERY_STATUS_MASK) == 0) {
		val = val & ~0x3F;
		val = val | ((adapter->portnum << 2) |
			     QLC_83XX_NEED_DRV_LOCK_RECOVERY);
		QLCWRX(adapter->ahw, QLC_83XX_RECOVER_DRV_LOCK, val);
		dev_info(&adapter->pdev->dev,
			 "%s: lock recovery initiated\n", __func__);
		msleep(QLC_83XX_DRV_LOCK_RECOVERY_DELAY);
		val = QLCRDX(adapter->ahw, QLC_83XX_RECOVER_DRV_LOCK);
		id = ((val >> 2) & 0xF);
		if (id == adapter->portnum) {
			val = val & ~QLC_83XX_DRV_LOCK_RECOVERY_STATUS_MASK;
			val = val | QLC_83XX_DRV_LOCK_RECOVERY_IN_PROGRESS;
			QLCWRX(adapter->ahw, QLC_83XX_RECOVER_DRV_LOCK, val);
			/* Force release the lock */
			QLCRDX(adapter->ahw, QLC_83XX_DRV_UNLOCK);
			/* Clear recovery bits */
			val = val & ~0x3F;
			QLCWRX(adapter->ahw, QLC_83XX_RECOVER_DRV_LOCK, val);
			dev_info(&adapter->pdev->dev,
				 "%s: lock recovery completed\n", __func__);
		} else {
			dev_info(&adapter->pdev->dev,
				 "%s: func %d to resume lock recovery process\n",
				 __func__, id);
		}
	} else {
		dev_info(&adapter->pdev->dev,
			 "%s: lock recovery initiated by other functions\n",
			 __func__);
	}
}

int qlcnic_83xx_lock_driver(struct qlcnic_adapter *adapter)
{
	u32 lock_alive_counter, val, id, i = 0, status = 0, temp = 0;
	int max_attempt = 0;

	while (status == 0) {
		status = QLCRDX(adapter->ahw, QLC_83XX_DRV_LOCK);
		if (status)
			break;

		msleep(QLC_83XX_DRV_LOCK_WAIT_DELAY);
		i++;

		if (i == 1)
			temp = QLCRDX(adapter->ahw, QLC_83XX_DRV_LOCK_ID);

		if (i == QLC_83XX_DRV_LOCK_WAIT_COUNTER) {
			val = QLCRDX(adapter->ahw, QLC_83XX_DRV_LOCK_ID);
			if (val == temp) {
				id = val & 0xFF;
				dev_info(&adapter->pdev->dev,
					 "%s: lock to be recovered from %d\n",
					 __func__, id);
				qlcnic_83xx_recover_driver_lock(adapter);
				i = 0;
				max_attempt++;
			} else {
				dev_err(&adapter->pdev->dev,
					"%s: failed to get lock\n", __func__);
				return -EIO;
			}
		}

		/* Force exit from while loop after few attempts */
		if (max_attempt == QLC_83XX_MAX_DRV_LOCK_RECOVERY_ATTEMPT) {
			dev_err(&adapter->pdev->dev,
				"%s: failed to get lock\n", __func__);
			return -EIO;
		}
	}

	val = QLCRDX(adapter->ahw, QLC_83XX_DRV_LOCK_ID);
	lock_alive_counter = val >> 8;
	lock_alive_counter++;
	val = lock_alive_counter << 8 | adapter->portnum;
	QLCWRX(adapter->ahw, QLC_83XX_DRV_LOCK_ID, val);

	return 0;
}

void qlcnic_83xx_unlock_driver(struct qlcnic_adapter *adapter)
{
	u32 val, lock_alive_counter, id;

	val = QLCRDX(adapter->ahw, QLC_83XX_DRV_LOCK_ID);
	id = val & 0xFF;
	lock_alive_counter = val >> 8;

	if (id != adapter->portnum)
		dev_err(&adapter->pdev->dev,
			"%s:Warning func %d is unlocking lock owned by %d\n",
			__func__, adapter->portnum, id);

	val = (lock_alive_counter << 8) | 0xFF;
	QLCWRX(adapter->ahw, QLC_83XX_DRV_LOCK_ID, val);
	QLCRDX(adapter->ahw, QLC_83XX_DRV_UNLOCK);
}

int qlcnic_83xx_ms_mem_write128(struct qlcnic_adapter *adapter, u64 addr,
				u32 *data, u32 count)
{
	int i, j, ret = 0;
	u32 temp;
	int err = 0;

	/* Check alignment */
	if (addr & 0xF)
		return -EIO;

	mutex_lock(&adapter->ahw->mem_lock);
	qlcnic_83xx_wrt_reg_indirect(adapter, QLCNIC_MS_ADDR_HI, 0);

	for (i = 0; i < count; i++, addr += 16) {
		if (!((ADDR_IN_RANGE(addr, QLCNIC_ADDR_QDR_NET,
				     QLCNIC_ADDR_QDR_NET_MAX)) ||
		      (ADDR_IN_RANGE(addr, QLCNIC_ADDR_DDR_NET,
				     QLCNIC_ADDR_DDR_NET_MAX)))) {
			mutex_unlock(&adapter->ahw->mem_lock);
			return -EIO;
		}

		qlcnic_83xx_wrt_reg_indirect(adapter, QLCNIC_MS_ADDR_LO, addr);
		qlcnic_83xx_wrt_reg_indirect(adapter, QLCNIC_MS_WRTDATA_LO,
					     *data++);
		qlcnic_83xx_wrt_reg_indirect(adapter, QLCNIC_MS_WRTDATA_HI,
					     *data++);
		qlcnic_83xx_wrt_reg_indirect(adapter, QLCNIC_MS_WRTDATA_ULO,
					     *data++);
		qlcnic_83xx_wrt_reg_indirect(adapter, QLCNIC_MS_WRTDATA_UHI,
					     *data++);
		qlcnic_83xx_wrt_reg_indirect(adapter, QLCNIC_MS_CTRL,
					     QLCNIC_TA_WRITE_ENABLE);
		qlcnic_83xx_wrt_reg_indirect(adapter, QLCNIC_MS_CTRL,
					     QLCNIC_TA_WRITE_START);

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			temp = QLCRD32(adapter, QLCNIC_MS_CTRL, &err);
			if (err == -EIO) {
				mutex_unlock(&adapter->ahw->mem_lock);
				return err;
			}

			if ((temp & TA_CTL_BUSY) == 0)
				break;
		}

		/* Status check failure */
		if (j >= MAX_CTL_CHECK) {
			printk_ratelimited(KERN_WARNING
					   "MS memory write failed\n");
			mutex_unlock(&adapter->ahw->mem_lock);
			return -EIO;
		}
	}

	mutex_unlock(&adapter->ahw->mem_lock);

	return ret;
}

int qlcnic_83xx_flash_read32(struct qlcnic_adapter *adapter, u32 flash_addr,
			     u8 *p_data, int count)
{
	u32 word, addr = flash_addr, ret;
	ulong  indirect_addr;
	int i, err = 0;

	if (qlcnic_83xx_lock_flash(adapter) != 0)
		return -EIO;

	if (addr & 0x3) {
		dev_err(&adapter->pdev->dev, "Illegal addr = 0x%x\n", addr);
		qlcnic_83xx_unlock_flash(adapter);
		return -EIO;
	}

	for (i = 0; i < count; i++) {
		if (qlcnic_83xx_wrt_reg_indirect(adapter,
						 QLC_83XX_FLASH_DIRECT_WINDOW,
						 (addr))) {
			qlcnic_83xx_unlock_flash(adapter);
			return -EIO;
		}

		indirect_addr = QLC_83XX_FLASH_DIRECT_DATA(addr);
		ret = QLCRD32(adapter, indirect_addr, &err);
		if (err == -EIO)
			return err;

		word = ret;
		*(u32 *)p_data  = word;
		p_data = p_data + 4;
		addr = addr + 4;
	}

	qlcnic_83xx_unlock_flash(adapter);

	return 0;
}

int qlcnic_83xx_test_link(struct qlcnic_adapter *adapter)
{
	u8 pci_func;
	int err;
	u32 config = 0, state;
	struct qlcnic_cmd_args cmd;
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	if (qlcnic_sriov_vf_check(adapter))
		pci_func = adapter->portnum;
	else
		pci_func = ahw->pci_func;

	state = readl(ahw->pci_base0 + QLC_83XX_LINK_STATE(pci_func));
	if (!QLC_83xx_FUNC_VAL(state, pci_func)) {
		dev_info(&adapter->pdev->dev, "link state down\n");
		return config;
	}

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_GET_LINK_STATUS);
	if (err)
		return err;

	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err) {
		dev_info(&adapter->pdev->dev,
			 "Get Link Status Command failed: 0x%x\n", err);
		goto out;
	} else {
		config = cmd.rsp.arg[1];
		switch (QLC_83XX_CURRENT_LINK_SPEED(config)) {
		case QLC_83XX_10M_LINK:
			ahw->link_speed = SPEED_10;
			break;
		case QLC_83XX_100M_LINK:
			ahw->link_speed = SPEED_100;
			break;
		case QLC_83XX_1G_LINK:
			ahw->link_speed = SPEED_1000;
			break;
		case QLC_83XX_10G_LINK:
			ahw->link_speed = SPEED_10000;
			break;
		default:
			ahw->link_speed = 0;
			break;
		}
		config = cmd.rsp.arg[3];
		if (QLC_83XX_SFP_PRESENT(config)) {
			switch (ahw->module_type) {
			case LINKEVENT_MODULE_OPTICAL_UNKNOWN:
			case LINKEVENT_MODULE_OPTICAL_SRLR:
			case LINKEVENT_MODULE_OPTICAL_LRM:
			case LINKEVENT_MODULE_OPTICAL_SFP_1G:
				ahw->supported_type = PORT_FIBRE;
				break;
			case LINKEVENT_MODULE_TWINAX_UNSUPPORTED_CABLE:
			case LINKEVENT_MODULE_TWINAX_UNSUPPORTED_CABLELEN:
			case LINKEVENT_MODULE_TWINAX:
				ahw->supported_type = PORT_TP;
				break;
			default:
				ahw->supported_type = PORT_OTHER;
			}
		}
		if (config & 1)
			err = 1;
	}
out:
	qlcnic_free_mbx_args(&cmd);
	return config;
}

int qlcnic_83xx_get_settings(struct qlcnic_adapter *adapter,
			     struct ethtool_cmd *ecmd)
{
	u32 config = 0;
	int status = 0;
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	/* Get port configuration info */
	status = qlcnic_83xx_get_port_info(adapter);
	/* Get Link Status related info */
	config = qlcnic_83xx_test_link(adapter);
	ahw->module_type = QLC_83XX_SFP_MODULE_TYPE(config);
	/* hard code until there is a way to get it from flash */
	ahw->board_type = QLCNIC_BRDTYPE_83XX_10G;

	if (netif_running(adapter->netdev) && ahw->has_link_events) {
		ethtool_cmd_speed_set(ecmd, ahw->link_speed);
		ecmd->duplex = ahw->link_duplex;
		ecmd->autoneg = ahw->link_autoneg;
	} else {
		ethtool_cmd_speed_set(ecmd, SPEED_UNKNOWN);
		ecmd->duplex = DUPLEX_UNKNOWN;
		ecmd->autoneg = AUTONEG_DISABLE;
	}

	if (ahw->port_type == QLCNIC_XGBE) {
		ecmd->supported = SUPPORTED_10000baseT_Full;
		ecmd->advertising = ADVERTISED_10000baseT_Full;
	} else {
		ecmd->supported = (SUPPORTED_10baseT_Half |
				   SUPPORTED_10baseT_Full |
				   SUPPORTED_100baseT_Half |
				   SUPPORTED_100baseT_Full |
				   SUPPORTED_1000baseT_Half |
				   SUPPORTED_1000baseT_Full);
		ecmd->advertising = (ADVERTISED_100baseT_Half |
				     ADVERTISED_100baseT_Full |
				     ADVERTISED_1000baseT_Half |
				     ADVERTISED_1000baseT_Full);
	}

	switch (ahw->supported_type) {
	case PORT_FIBRE:
		ecmd->supported |= SUPPORTED_FIBRE;
		ecmd->advertising |= ADVERTISED_FIBRE;
		ecmd->port = PORT_FIBRE;
		ecmd->transceiver = XCVR_EXTERNAL;
		break;
	case PORT_TP:
		ecmd->supported |= SUPPORTED_TP;
		ecmd->advertising |= ADVERTISED_TP;
		ecmd->port = PORT_TP;
		ecmd->transceiver = XCVR_INTERNAL;
		break;
	default:
		ecmd->supported |= SUPPORTED_FIBRE;
		ecmd->advertising |= ADVERTISED_FIBRE;
		ecmd->port = PORT_OTHER;
		ecmd->transceiver = XCVR_EXTERNAL;
		break;
	}
	ecmd->phy_address = ahw->physical_port;
	return status;
}

int qlcnic_83xx_set_settings(struct qlcnic_adapter *adapter,
			     struct ethtool_cmd *ecmd)
{
	int status = 0;
	u32 config = adapter->ahw->port_config;

	if (ecmd->autoneg)
		adapter->ahw->port_config |= BIT_15;

	switch (ethtool_cmd_speed(ecmd)) {
	case SPEED_10:
		adapter->ahw->port_config |= BIT_8;
		break;
	case SPEED_100:
		adapter->ahw->port_config |= BIT_9;
		break;
	case SPEED_1000:
		adapter->ahw->port_config |= BIT_10;
		break;
	case SPEED_10000:
		adapter->ahw->port_config |= BIT_11;
		break;
	default:
		return -EINVAL;
	}

	status = qlcnic_83xx_set_port_config(adapter);
	if (status) {
		dev_info(&adapter->pdev->dev,
			 "Faild to Set Link Speed and autoneg.\n");
		adapter->ahw->port_config = config;
	}
	return status;
}

static inline u64 *qlcnic_83xx_copy_stats(struct qlcnic_cmd_args *cmd,
					  u64 *data, int index)
{
	u32 low, hi;
	u64 val;

	low = cmd->rsp.arg[index];
	hi = cmd->rsp.arg[index + 1];
	val = (((u64) low) | (((u64) hi) << 32));
	*data++ = val;
	return data;
}

static u64 *qlcnic_83xx_fill_stats(struct qlcnic_adapter *adapter,
				   struct qlcnic_cmd_args *cmd, u64 *data,
				   int type, int *ret)
{
	int err, k, total_regs;

	*ret = 0;
	err = qlcnic_issue_cmd(adapter, cmd);
	if (err != QLCNIC_RCODE_SUCCESS) {
		dev_info(&adapter->pdev->dev,
			 "Error in get statistics mailbox command\n");
		*ret = -EIO;
		return data;
	}
	total_regs = cmd->rsp.num;
	switch (type) {
	case QLC_83XX_STAT_MAC:
		/* fill in MAC tx counters */
		for (k = 2; k < 28; k += 2)
			data = qlcnic_83xx_copy_stats(cmd, data, k);
		/* skip 24 bytes of reserved area */
		/* fill in MAC rx counters */
		for (k += 6; k < 60; k += 2)
			data = qlcnic_83xx_copy_stats(cmd, data, k);
		/* skip 24 bytes of reserved area */
		/* fill in MAC rx frame stats */
		for (k += 6; k < 80; k += 2)
			data = qlcnic_83xx_copy_stats(cmd, data, k);
		/* fill in eSwitch stats */
		for (; k < total_regs; k += 2)
			data = qlcnic_83xx_copy_stats(cmd, data, k);
		break;
	case QLC_83XX_STAT_RX:
		for (k = 2; k < 8; k += 2)
			data = qlcnic_83xx_copy_stats(cmd, data, k);
		/* skip 8 bytes of reserved data */
		for (k += 2; k < 24; k += 2)
			data = qlcnic_83xx_copy_stats(cmd, data, k);
		/* skip 8 bytes containing RE1FBQ error data */
		for (k += 2; k < total_regs; k += 2)
			data = qlcnic_83xx_copy_stats(cmd, data, k);
		break;
	case QLC_83XX_STAT_TX:
		for (k = 2; k < 10; k += 2)
			data = qlcnic_83xx_copy_stats(cmd, data, k);
		/* skip 8 bytes of reserved data */
		for (k += 2; k < total_regs; k += 2)
			data = qlcnic_83xx_copy_stats(cmd, data, k);
		break;
	default:
		dev_warn(&adapter->pdev->dev, "Unknown get statistics mode\n");
		*ret = -EIO;
	}
	return data;
}

void qlcnic_83xx_get_stats(struct qlcnic_adapter *adapter, u64 *data)
{
	struct qlcnic_cmd_args cmd;
	struct net_device *netdev = adapter->netdev;
	int ret = 0;

	ret = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_GET_STATISTICS);
	if (ret)
		return;
	/* Get Tx stats */
	cmd.req.arg[1] = BIT_1 | (adapter->tx_ring->ctx_id << 16);
	cmd.rsp.num = QLC_83XX_TX_STAT_REGS;
	data = qlcnic_83xx_fill_stats(adapter, &cmd, data,
				      QLC_83XX_STAT_TX, &ret);
	if (ret) {
		netdev_err(netdev, "Error getting Tx stats\n");
		goto out;
	}
	/* Get MAC stats */
	cmd.req.arg[1] = BIT_2 | (adapter->portnum << 16);
	cmd.rsp.num = QLC_83XX_MAC_STAT_REGS;
	memset(cmd.rsp.arg, 0, sizeof(u32) * cmd.rsp.num);
	data = qlcnic_83xx_fill_stats(adapter, &cmd, data,
				      QLC_83XX_STAT_MAC, &ret);
	if (ret) {
		netdev_err(netdev, "Error getting MAC stats\n");
		goto out;
	}
	/* Get Rx stats */
	cmd.req.arg[1] = adapter->recv_ctx->context_id << 16;
	cmd.rsp.num = QLC_83XX_RX_STAT_REGS;
	memset(cmd.rsp.arg, 0, sizeof(u32) * cmd.rsp.num);
	data = qlcnic_83xx_fill_stats(adapter, &cmd, data,
				      QLC_83XX_STAT_RX, &ret);
	if (ret)
		netdev_err(netdev, "Error getting Rx stats\n");
out:
	qlcnic_free_mbx_args(&cmd);
}

int qlcnic_83xx_reg_test(struct qlcnic_adapter *adapter)
{
	u32 major, minor, sub;

	major = QLC_SHARED_REG_RD32(adapter, QLCNIC_FW_VERSION_MAJOR);
	minor = QLC_SHARED_REG_RD32(adapter, QLCNIC_FW_VERSION_MINOR);
	sub = QLC_SHARED_REG_RD32(adapter, QLCNIC_FW_VERSION_SUB);

	if (adapter->fw_version != QLCNIC_VERSION_CODE(major, minor, sub)) {
		dev_info(&adapter->pdev->dev, "%s: Reg test failed\n",
			 __func__);
		return 1;
	}
	return 0;
}

int qlcnic_83xx_get_regs_len(struct qlcnic_adapter *adapter)
{
	return (ARRAY_SIZE(qlcnic_83xx_ext_reg_tbl) *
		sizeof(adapter->ahw->ext_reg_tbl)) +
		(ARRAY_SIZE(qlcnic_83xx_reg_tbl) +
		sizeof(adapter->ahw->reg_tbl));
}

int qlcnic_83xx_get_registers(struct qlcnic_adapter *adapter, u32 *regs_buff)
{
	int i, j = 0;

	for (i = QLCNIC_DEV_INFO_SIZE + 1;
	     j < ARRAY_SIZE(qlcnic_83xx_reg_tbl); i++, j++)
		regs_buff[i] = QLC_SHARED_REG_RD32(adapter, j);

	for (j = 0; j < ARRAY_SIZE(qlcnic_83xx_ext_reg_tbl); j++)
		regs_buff[i++] = QLCRDX(adapter->ahw, j);
	return i;
}

int qlcnic_83xx_interrupt_test(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlcnic_cmd_args cmd;
	u32 data;
	u16 intrpt_id, id;
	u8 val;
	int ret, max_sds_rings = adapter->max_sds_rings;

	if (test_bit(__QLCNIC_RESETTING, &adapter->state)) {
		netdev_info(netdev, "Device is resetting\n");
		return -EBUSY;
	}

	if (qlcnic_get_diag_lock(adapter)) {
		netdev_info(netdev, "Device in diagnostics mode\n");
		return -EBUSY;
	}

	ret = qlcnic_83xx_diag_alloc_res(netdev, QLCNIC_INTERRUPT_TEST,
					 max_sds_rings);
	if (ret)
		goto fail_diag_irq;

	ahw->diag_cnt = 0;
	ret = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_INTRPT_TEST);
	if (ret)
		goto fail_diag_irq;

	if (adapter->flags & QLCNIC_MSIX_ENABLED)
		intrpt_id = ahw->intr_tbl[0].id;
	else
		intrpt_id = QLCRDX(ahw, QLCNIC_DEF_INT_ID);

	cmd.req.arg[1] = 1;
	cmd.req.arg[2] = intrpt_id;
	cmd.req.arg[3] = BIT_0;

	ret = qlcnic_issue_cmd(adapter, &cmd);
	data = cmd.rsp.arg[2];
	id = LSW(data);
	val = LSB(MSW(data));
	if (id != intrpt_id)
		dev_info(&adapter->pdev->dev,
			 "Interrupt generated: 0x%x, requested:0x%x\n",
			 id, intrpt_id);
	if (val)
		dev_err(&adapter->pdev->dev,
			 "Interrupt test error: 0x%x\n", val);
	if (ret)
		goto done;

	msleep(20);
	ret = !ahw->diag_cnt;

done:
	qlcnic_free_mbx_args(&cmd);
	qlcnic_83xx_diag_free_res(netdev, max_sds_rings);

fail_diag_irq:
	adapter->max_sds_rings = max_sds_rings;
	qlcnic_release_diag_lock(adapter);
	return ret;
}

void qlcnic_83xx_get_pauseparam(struct qlcnic_adapter *adapter,
				struct ethtool_pauseparam *pause)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	int status = 0;
	u32 config;

	status = qlcnic_83xx_get_port_config(adapter);
	if (status) {
		dev_err(&adapter->pdev->dev,
			"%s: Get Pause Config failed\n", __func__);
		return;
	}
	config = ahw->port_config;
	if (config & QLC_83XX_CFG_STD_PAUSE) {
		if (config & QLC_83XX_CFG_STD_TX_PAUSE)
			pause->tx_pause = 1;
		if (config & QLC_83XX_CFG_STD_RX_PAUSE)
			pause->rx_pause = 1;
	}

	if (QLC_83XX_AUTONEG(config))
		pause->autoneg = 1;
}

int qlcnic_83xx_set_pauseparam(struct qlcnic_adapter *adapter,
			       struct ethtool_pauseparam *pause)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	int status = 0;
	u32 config;

	status = qlcnic_83xx_get_port_config(adapter);
	if (status) {
		dev_err(&adapter->pdev->dev,
			"%s: Get Pause Config failed.\n", __func__);
		return status;
	}
	config = ahw->port_config;

	if (ahw->port_type == QLCNIC_GBE) {
		if (pause->autoneg)
			ahw->port_config |= QLC_83XX_ENABLE_AUTONEG;
		if (!pause->autoneg)
			ahw->port_config &= ~QLC_83XX_ENABLE_AUTONEG;
	} else if ((ahw->port_type == QLCNIC_XGBE) && (pause->autoneg)) {
		return -EOPNOTSUPP;
	}

	if (!(config & QLC_83XX_CFG_STD_PAUSE))
		ahw->port_config |= QLC_83XX_CFG_STD_PAUSE;

	if (pause->rx_pause && pause->tx_pause) {
		ahw->port_config |= QLC_83XX_CFG_STD_TX_RX_PAUSE;
	} else if (pause->rx_pause && !pause->tx_pause) {
		ahw->port_config &= ~QLC_83XX_CFG_STD_TX_PAUSE;
		ahw->port_config |= QLC_83XX_CFG_STD_RX_PAUSE;
	} else if (pause->tx_pause && !pause->rx_pause) {
		ahw->port_config &= ~QLC_83XX_CFG_STD_RX_PAUSE;
		ahw->port_config |= QLC_83XX_CFG_STD_TX_PAUSE;
	} else if (!pause->rx_pause && !pause->tx_pause) {
		ahw->port_config &= ~QLC_83XX_CFG_STD_TX_RX_PAUSE;
	}
	status = qlcnic_83xx_set_port_config(adapter);
	if (status) {
		dev_err(&adapter->pdev->dev,
			"%s: Set Pause Config failed.\n", __func__);
		ahw->port_config = config;
	}
	return status;
}

static int qlcnic_83xx_read_flash_status_reg(struct qlcnic_adapter *adapter)
{
	int ret, err = 0;
	u32 temp;

	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_ADDR,
				     QLC_83XX_FLASH_OEM_READ_SIG);
	qlcnic_83xx_wrt_reg_indirect(adapter, QLC_83XX_FLASH_CONTROL,
				     QLC_83XX_FLASH_READ_CTRL);
	ret = qlcnic_83xx_poll_flash_status_reg(adapter);
	if (ret)
		return -EIO;

	temp = QLCRD32(adapter, QLC_83XX_FLASH_RDDATA, &err);
	if (err == -EIO)
		return err;

	return temp & 0xFF;
}

int qlcnic_83xx_flash_test(struct qlcnic_adapter *adapter)
{
	int status;

	status = qlcnic_83xx_read_flash_status_reg(adapter);
	if (status == -EIO) {
		dev_info(&adapter->pdev->dev, "%s: EEPROM test failed.\n",
			 __func__);
		return 1;
	}
	return 0;
}

int qlcnic_83xx_shutdown(struct pci_dev *pdev)
{
	struct qlcnic_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;
	int retval;

	netif_device_detach(netdev);
	qlcnic_cancel_idc_work(adapter);

	if (netif_running(netdev))
		qlcnic_down(adapter, netdev);

	qlcnic_83xx_disable_mbx_intr(adapter);
	cancel_delayed_work_sync(&adapter->idc_aen_work);

	retval = pci_save_state(pdev);
	if (retval)
		return retval;

	return 0;
}

int qlcnic_83xx_resume(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlc_83xx_idc *idc = &ahw->idc;
	int err = 0;

	err = qlcnic_83xx_idc_init(adapter);
	if (err)
		return err;

	if (ahw->nic_mode == QLC_83XX_VIRTUAL_NIC_MODE) {
		if (ahw->op_mode == QLCNIC_MGMT_FUNC) {
			qlcnic_83xx_set_vnic_opmode(adapter);
		} else {
			err = qlcnic_83xx_check_vnic_state(adapter);
			if (err)
				return err;
		}
	}

	err = qlcnic_83xx_idc_reattach_driver(adapter);
	if (err)
		return err;

	qlcnic_schedule_work(adapter, qlcnic_83xx_idc_poll_dev_state,
			     idc->delay);
	return err;
}

void qlcnic_83xx_reinit_mbx_work(struct qlcnic_mailbox *mbx)
{
	INIT_COMPLETION(mbx->completion);
	set_bit(QLC_83XX_MBX_READY, &mbx->status);
}

void qlcnic_83xx_free_mailbox(struct qlcnic_mailbox *mbx)
{
	destroy_workqueue(mbx->work_q);
	kfree(mbx);
}

static inline void
qlcnic_83xx_notify_cmd_completion(struct qlcnic_adapter *adapter,
				  struct qlcnic_cmd_args *cmd)
{
	atomic_set(&cmd->rsp_status, QLC_83XX_MBX_RESPONSE_ARRIVED);

	if (cmd->type == QLC_83XX_MBX_CMD_NO_WAIT) {
		qlcnic_free_mbx_args(cmd);
		kfree(cmd);
		return;
	}
	complete(&cmd->completion);
}

static inline void qlcnic_83xx_flush_mbx_queue(struct qlcnic_adapter *adapter)
{
	struct qlcnic_mailbox *mbx = adapter->ahw->mailbox;
	struct list_head *head = &mbx->cmd_q;
	struct qlcnic_cmd_args *cmd = NULL;

	spin_lock(&mbx->queue_lock);

	while (!list_empty(head)) {
		cmd = list_entry(head->next, struct qlcnic_cmd_args, list);
		dev_info(&adapter->pdev->dev, "%s: Mailbox command 0x%x\n",
			 __func__, cmd->cmd_op);
		list_del(&cmd->list);
		mbx->num_cmds--;
		qlcnic_83xx_notify_cmd_completion(adapter, cmd);
	}

	spin_unlock(&mbx->queue_lock);
}

static inline int qlcnic_83xx_check_mbx_status(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlcnic_mailbox *mbx = ahw->mailbox;
	u32 host_mbx_ctrl;

	if (!test_bit(QLC_83XX_MBX_READY, &mbx->status))
		return -EBUSY;

	host_mbx_ctrl = QLCRDX(ahw, QLCNIC_HOST_MBX_CTRL);
	if (host_mbx_ctrl) {
		clear_bit(QLC_83XX_MBX_READY, &mbx->status);
		ahw->idc.collect_dump = 1;
		return -EIO;
	}

	return 0;
}

static inline void qlcnic_83xx_signal_mbx_cmd(struct qlcnic_adapter *adapter,
					      u8 issue_cmd)
{
	if (issue_cmd)
		QLCWRX(adapter->ahw, QLCNIC_HOST_MBX_CTRL, QLCNIC_SET_OWNER);
	else
		QLCWRX(adapter->ahw, QLCNIC_FW_MBX_CTRL, QLCNIC_CLR_OWNER);
}

static inline void qlcnic_83xx_dequeue_mbx_cmd(struct qlcnic_adapter *adapter,
					       struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_mailbox *mbx = adapter->ahw->mailbox;

	spin_lock(&mbx->queue_lock);

	list_del(&cmd->list);
	mbx->num_cmds--;

	spin_unlock(&mbx->queue_lock);

	qlcnic_83xx_notify_cmd_completion(adapter, cmd);
}

static void qlcnic_83xx_encode_mbx_cmd(struct qlcnic_adapter *adapter,
				       struct qlcnic_cmd_args *cmd)
{
	u32 mbx_cmd, fw_hal_version, hdr_size, total_size, tmp;
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	int i, j;

	if (cmd->op_type != QLC_83XX_MBX_POST_BC_OP) {
		mbx_cmd = cmd->req.arg[0];
		writel(mbx_cmd, QLCNIC_MBX_HOST(ahw, 0));
		for (i = 1; i < cmd->req.num; i++)
			writel(cmd->req.arg[i], QLCNIC_MBX_HOST(ahw, i));
	} else {
		fw_hal_version = ahw->fw_hal_version;
		hdr_size = sizeof(struct qlcnic_bc_hdr) / sizeof(u32);
		total_size = cmd->pay_size + hdr_size;
		tmp = QLCNIC_CMD_BC_EVENT_SETUP | total_size << 16;
		mbx_cmd = tmp | fw_hal_version << 29;
		writel(mbx_cmd, QLCNIC_MBX_HOST(ahw, 0));

		/* Back channel specific operations bits */
		mbx_cmd = 0x1 | 1 << 4;

		if (qlcnic_sriov_pf_check(adapter))
			mbx_cmd |= cmd->func_num << 5;

		writel(mbx_cmd, QLCNIC_MBX_HOST(ahw, 1));

		for (i = 2, j = 0; j < hdr_size; i++, j++)
			writel(*(cmd->hdr++), QLCNIC_MBX_HOST(ahw, i));
		for (j = 0; j < cmd->pay_size; j++, i++)
			writel(*(cmd->pay++), QLCNIC_MBX_HOST(ahw, i));
	}
}

void qlcnic_83xx_detach_mailbox_work(struct qlcnic_adapter *adapter)
{
	struct qlcnic_mailbox *mbx = adapter->ahw->mailbox;

	clear_bit(QLC_83XX_MBX_READY, &mbx->status);
	complete(&mbx->completion);
	cancel_work_sync(&mbx->work);
	flush_workqueue(mbx->work_q);
	qlcnic_83xx_flush_mbx_queue(adapter);
}

static inline int qlcnic_83xx_enqueue_mbx_cmd(struct qlcnic_adapter *adapter,
					      struct qlcnic_cmd_args *cmd,
					      unsigned long *timeout)
{
	struct qlcnic_mailbox *mbx = adapter->ahw->mailbox;

	if (test_bit(QLC_83XX_MBX_READY, &mbx->status)) {
		atomic_set(&cmd->rsp_status, QLC_83XX_MBX_RESPONSE_WAIT);
		init_completion(&cmd->completion);
		cmd->rsp_opcode = QLC_83XX_MBX_RESPONSE_UNKNOWN;

		spin_lock(&mbx->queue_lock);

		list_add_tail(&cmd->list, &mbx->cmd_q);
		mbx->num_cmds++;
		cmd->total_cmds = mbx->num_cmds;
		*timeout = cmd->total_cmds * QLC_83XX_MBX_TIMEOUT;
		queue_work(mbx->work_q, &mbx->work);

		spin_unlock(&mbx->queue_lock);

		return 0;
	}

	return -EBUSY;
}

static inline int qlcnic_83xx_check_mac_rcode(struct qlcnic_adapter *adapter,
					      struct qlcnic_cmd_args *cmd)
{
	u8 mac_cmd_rcode;
	u32 fw_data;

	if (cmd->cmd_op == QLCNIC_CMD_CONFIG_MAC_VLAN) {
		fw_data = readl(QLCNIC_MBX_FW(adapter->ahw, 2));
		mac_cmd_rcode = (u8)fw_data;
		if (mac_cmd_rcode == QLC_83XX_NO_NIC_RESOURCE ||
		    mac_cmd_rcode == QLC_83XX_MAC_PRESENT ||
		    mac_cmd_rcode == QLC_83XX_MAC_ABSENT) {
			cmd->rsp_opcode = QLCNIC_RCODE_SUCCESS;
			return QLCNIC_RCODE_SUCCESS;
		}
	}

	return -EINVAL;
}

static void qlcnic_83xx_decode_mbx_rsp(struct qlcnic_adapter *adapter,
				       struct qlcnic_cmd_args *cmd)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct device *dev = &adapter->pdev->dev;
	u8 mbx_err_code;
	u32 fw_data;

	fw_data = readl(QLCNIC_MBX_FW(ahw, 0));
	mbx_err_code = QLCNIC_MBX_STATUS(fw_data);
	qlcnic_83xx_get_mbx_data(adapter, cmd);

	switch (mbx_err_code) {
	case QLCNIC_MBX_RSP_OK:
	case QLCNIC_MBX_PORT_RSP_OK:
		cmd->rsp_opcode = QLCNIC_RCODE_SUCCESS;
		break;
	default:
		if (!qlcnic_83xx_check_mac_rcode(adapter, cmd))
			break;

		dev_err(dev, "%s: Mailbox command failed, opcode=0x%x, cmd_type=0x%x, func=0x%x, op_mode=0x%x, error=0x%x\n",
			__func__, cmd->cmd_op, cmd->type, ahw->pci_func,
			ahw->op_mode, mbx_err_code);
		cmd->rsp_opcode = QLC_83XX_MBX_RESPONSE_FAILED;
		qlcnic_dump_mbx(adapter, cmd);
	}

	return;
}

static void qlcnic_83xx_mailbox_worker(struct work_struct *work)
{
	struct qlcnic_mailbox *mbx = container_of(work, struct qlcnic_mailbox,
						  work);
	struct qlcnic_adapter *adapter = mbx->adapter;
	struct qlcnic_mbx_ops *mbx_ops = mbx->ops;
	struct device *dev = &adapter->pdev->dev;
	atomic_t *rsp_status = &mbx->rsp_status;
	struct list_head *head = &mbx->cmd_q;
	struct qlcnic_hardware_context *ahw;
	struct qlcnic_cmd_args *cmd = NULL;

	ahw = adapter->ahw;

	while (true) {
		if (qlcnic_83xx_check_mbx_status(adapter)) {
			qlcnic_83xx_flush_mbx_queue(adapter);
			return;
		}

		atomic_set(rsp_status, QLC_83XX_MBX_RESPONSE_WAIT);

		spin_lock(&mbx->queue_lock);

		if (list_empty(head)) {
			spin_unlock(&mbx->queue_lock);
			return;
		}
		cmd = list_entry(head->next, struct qlcnic_cmd_args, list);

		spin_unlock(&mbx->queue_lock);

		mbx_ops->encode_cmd(adapter, cmd);
		mbx_ops->nofity_fw(adapter, QLC_83XX_MBX_REQUEST);

		if (wait_for_completion_timeout(&mbx->completion,
						QLC_83XX_MBX_TIMEOUT)) {
			mbx_ops->decode_resp(adapter, cmd);
			mbx_ops->nofity_fw(adapter, QLC_83XX_MBX_COMPLETION);
		} else {
			dev_err(dev, "%s: Mailbox command timeout, opcode=0x%x, cmd_type=0x%x, func=0x%x, op_mode=0x%x\n",
				__func__, cmd->cmd_op, cmd->type, ahw->pci_func,
				ahw->op_mode);
			clear_bit(QLC_83XX_MBX_READY, &mbx->status);
			qlcnic_dump_mbx(adapter, cmd);
			qlcnic_83xx_idc_request_reset(adapter,
						      QLCNIC_FORCE_FW_DUMP_KEY);
			cmd->rsp_opcode = QLCNIC_RCODE_TIMEOUT;
		}
		mbx_ops->dequeue_cmd(adapter, cmd);
	}
}

static struct qlcnic_mbx_ops qlcnic_83xx_mbx_ops = {
	.enqueue_cmd    = qlcnic_83xx_enqueue_mbx_cmd,
	.dequeue_cmd    = qlcnic_83xx_dequeue_mbx_cmd,
	.decode_resp    = qlcnic_83xx_decode_mbx_rsp,
	.encode_cmd     = qlcnic_83xx_encode_mbx_cmd,
	.nofity_fw      = qlcnic_83xx_signal_mbx_cmd,
};

int qlcnic_83xx_init_mailbox_work(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlcnic_mailbox *mbx;

	ahw->mailbox = kzalloc(sizeof(*mbx), GFP_KERNEL);
	if (!ahw->mailbox)
		return -ENOMEM;

	mbx = ahw->mailbox;
	mbx->ops = &qlcnic_83xx_mbx_ops;
	mbx->adapter = adapter;

	spin_lock_init(&mbx->queue_lock);
	spin_lock_init(&mbx->aen_lock);
	INIT_LIST_HEAD(&mbx->cmd_q);
	init_completion(&mbx->completion);

	mbx->work_q = create_singlethread_workqueue("qlcnic_mailbox");
	if (mbx->work_q == NULL) {
		kfree(mbx);
		return -ENOMEM;
	}

	INIT_WORK(&mbx->work, qlcnic_83xx_mailbox_worker);
	set_bit(QLC_83XX_MBX_READY, &mbx->status);
	return 0;
}
