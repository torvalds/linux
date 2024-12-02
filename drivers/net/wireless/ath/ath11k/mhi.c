// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/msi.h>
#include <linux/pci.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/ioport.h>

#include "core.h"
#include "debug.h"
#include "mhi.h"
#include "pci.h"
#include "pcic.h"

#define MHI_TIMEOUT_DEFAULT_MS	20000
#define RDDM_DUMP_SIZE	0x420000

static struct mhi_channel_config ath11k_mhi_channels_qca6390[] = {
	{
		.num = 0,
		.name = "LOOPBACK",
		.num_elements = 32,
		.event_ring = 0,
		.dir = DMA_TO_DEVICE,
		.ee_mask = 0x4,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
	},
	{
		.num = 1,
		.name = "LOOPBACK",
		.num_elements = 32,
		.event_ring = 0,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = 0x4,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
	},
	{
		.num = 20,
		.name = "IPCR",
		.num_elements = 64,
		.event_ring = 1,
		.dir = DMA_TO_DEVICE,
		.ee_mask = 0x4,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
	},
	{
		.num = 21,
		.name = "IPCR",
		.num_elements = 64,
		.event_ring = 1,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = 0x4,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = true,
	},
};

static struct mhi_event_config ath11k_mhi_events_qca6390[] = {
	{
		.num_elements = 32,
		.irq_moderation_ms = 0,
		.irq = 1,
		.mode = MHI_DB_BRST_DISABLE,
		.data_type = MHI_ER_CTRL,
		.hardware_event = false,
		.client_managed = false,
		.offload_channel = false,
	},
	{
		.num_elements = 256,
		.irq_moderation_ms = 1,
		.irq = 2,
		.mode = MHI_DB_BRST_DISABLE,
		.priority = 1,
		.hardware_event = false,
		.client_managed = false,
		.offload_channel = false,
	},
};

static struct mhi_controller_config ath11k_mhi_config_qca6390 = {
	.max_channels = 128,
	.timeout_ms = 2000,
	.use_bounce_buf = false,
	.buf_len = 8192,
	.num_channels = ARRAY_SIZE(ath11k_mhi_channels_qca6390),
	.ch_cfg = ath11k_mhi_channels_qca6390,
	.num_events = ARRAY_SIZE(ath11k_mhi_events_qca6390),
	.event_cfg = ath11k_mhi_events_qca6390,
};

static struct mhi_channel_config ath11k_mhi_channels_qcn9074[] = {
	{
		.num = 0,
		.name = "LOOPBACK",
		.num_elements = 32,
		.event_ring = 1,
		.dir = DMA_TO_DEVICE,
		.ee_mask = 0x14,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
	},
	{
		.num = 1,
		.name = "LOOPBACK",
		.num_elements = 32,
		.event_ring = 1,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = 0x14,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
	},
	{
		.num = 20,
		.name = "IPCR",
		.num_elements = 32,
		.event_ring = 1,
		.dir = DMA_TO_DEVICE,
		.ee_mask = 0x14,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
	},
	{
		.num = 21,
		.name = "IPCR",
		.num_elements = 32,
		.event_ring = 1,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = 0x14,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = true,
	},
};

static struct mhi_event_config ath11k_mhi_events_qcn9074[] = {
	{
		.num_elements = 32,
		.irq_moderation_ms = 0,
		.irq = 1,
		.data_type = MHI_ER_CTRL,
		.mode = MHI_DB_BRST_DISABLE,
		.hardware_event = false,
		.client_managed = false,
		.offload_channel = false,
	},
	{
		.num_elements = 256,
		.irq_moderation_ms = 1,
		.irq = 2,
		.mode = MHI_DB_BRST_DISABLE,
		.priority = 1,
		.hardware_event = false,
		.client_managed = false,
		.offload_channel = false,
	},
};

static struct mhi_controller_config ath11k_mhi_config_qcn9074 = {
	.max_channels = 30,
	.timeout_ms = 10000,
	.use_bounce_buf = false,
	.buf_len = 0,
	.num_channels = ARRAY_SIZE(ath11k_mhi_channels_qcn9074),
	.ch_cfg = ath11k_mhi_channels_qcn9074,
	.num_events = ARRAY_SIZE(ath11k_mhi_events_qcn9074),
	.event_cfg = ath11k_mhi_events_qcn9074,
};

void ath11k_mhi_set_mhictrl_reset(struct ath11k_base *ab)
{
	u32 val;

	val = ath11k_pcic_read32(ab, MHISTATUS);

	ath11k_dbg(ab, ATH11K_DBG_PCI, "MHISTATUS 0x%x\n", val);

	/* Observed on QCA6390 that after SOC_GLOBAL_RESET, MHISTATUS
	 * has SYSERR bit set and thus need to set MHICTRL_RESET
	 * to clear SYSERR.
	 */
	ath11k_pcic_write32(ab, MHICTRL, MHICTRL_RESET_MASK);

	mdelay(10);
}

static void ath11k_mhi_reset_txvecdb(struct ath11k_base *ab)
{
	ath11k_pcic_write32(ab, PCIE_TXVECDB, 0);
}

static void ath11k_mhi_reset_txvecstatus(struct ath11k_base *ab)
{
	ath11k_pcic_write32(ab, PCIE_TXVECSTATUS, 0);
}

static void ath11k_mhi_reset_rxvecdb(struct ath11k_base *ab)
{
	ath11k_pcic_write32(ab, PCIE_RXVECDB, 0);
}

static void ath11k_mhi_reset_rxvecstatus(struct ath11k_base *ab)
{
	ath11k_pcic_write32(ab, PCIE_RXVECSTATUS, 0);
}

void ath11k_mhi_clear_vector(struct ath11k_base *ab)
{
	ath11k_mhi_reset_txvecdb(ab);
	ath11k_mhi_reset_txvecstatus(ab);
	ath11k_mhi_reset_rxvecdb(ab);
	ath11k_mhi_reset_rxvecstatus(ab);
}

static int ath11k_mhi_get_msi(struct ath11k_pci *ab_pci)
{
	struct ath11k_base *ab = ab_pci->ab;
	u32 user_base_data, base_vector;
	int ret, num_vectors, i;
	int *irq;
	unsigned int msi_data;

	ret = ath11k_pcic_get_user_msi_assignment(ab, "MHI", &num_vectors,
						  &user_base_data, &base_vector);
	if (ret)
		return ret;

	ath11k_dbg(ab, ATH11K_DBG_PCI, "Number of assigned MSI for MHI is %d, base vector is %d\n",
		   num_vectors, base_vector);

	irq = kcalloc(num_vectors, sizeof(int), GFP_KERNEL);
	if (!irq)
		return -ENOMEM;

	for (i = 0; i < num_vectors; i++) {
		msi_data = base_vector;

		if (test_bit(ATH11K_FLAG_MULTI_MSI_VECTORS, &ab->dev_flags))
			msi_data += i;

		irq[i] = ath11k_pci_get_msi_irq(ab, msi_data);
	}

	ab_pci->mhi_ctrl->irq = irq;
	ab_pci->mhi_ctrl->nr_irqs = num_vectors;

	return 0;
}

static int ath11k_mhi_op_runtime_get(struct mhi_controller *mhi_cntrl)
{
	return 0;
}

static void ath11k_mhi_op_runtime_put(struct mhi_controller *mhi_cntrl)
{
}

static char *ath11k_mhi_op_callback_to_str(enum mhi_callback reason)
{
	switch (reason) {
	case MHI_CB_IDLE:
		return "MHI_CB_IDLE";
	case MHI_CB_PENDING_DATA:
		return "MHI_CB_PENDING_DATA";
	case MHI_CB_LPM_ENTER:
		return "MHI_CB_LPM_ENTER";
	case MHI_CB_LPM_EXIT:
		return "MHI_CB_LPM_EXIT";
	case MHI_CB_EE_RDDM:
		return "MHI_CB_EE_RDDM";
	case MHI_CB_EE_MISSION_MODE:
		return "MHI_CB_EE_MISSION_MODE";
	case MHI_CB_SYS_ERROR:
		return "MHI_CB_SYS_ERROR";
	case MHI_CB_FATAL_ERROR:
		return "MHI_CB_FATAL_ERROR";
	case MHI_CB_BW_REQ:
		return "MHI_CB_BW_REQ";
	default:
		return "UNKNOWN";
	}
};

static void ath11k_mhi_op_status_cb(struct mhi_controller *mhi_cntrl,
				    enum mhi_callback cb)
{
	struct ath11k_base *ab = dev_get_drvdata(mhi_cntrl->cntrl_dev);

	ath11k_dbg(ab, ATH11K_DBG_BOOT, "mhi notify status reason %s\n",
		   ath11k_mhi_op_callback_to_str(cb));

	switch (cb) {
	case MHI_CB_SYS_ERROR:
		ath11k_warn(ab, "firmware crashed: MHI_CB_SYS_ERROR\n");
		break;
	case MHI_CB_EE_RDDM:
		if (!(test_bit(ATH11K_FLAG_UNREGISTERING, &ab->dev_flags)))
			queue_work(ab->workqueue_aux, &ab->reset_work);
		break;
	default:
		break;
	}
}

static int ath11k_mhi_op_read_reg(struct mhi_controller *mhi_cntrl,
				  void __iomem *addr,
				  u32 *out)
{
	*out = readl(addr);

	return 0;
}

static void ath11k_mhi_op_write_reg(struct mhi_controller *mhi_cntrl,
				    void __iomem *addr,
				    u32 val)
{
	writel(val, addr);
}

static int ath11k_mhi_read_addr_from_dt(struct mhi_controller *mhi_ctrl)
{
	struct device_node *np;
	struct resource res;
	int ret;

	np = of_find_node_by_type(NULL, "memory");
	if (!np)
		return -ENOENT;

	ret = of_address_to_resource(np, 0, &res);
	of_node_put(np);
	if (ret)
		return ret;

	mhi_ctrl->iova_start = res.start + 0x1000000;
	mhi_ctrl->iova_stop = res.end;

	return 0;
}

int ath11k_mhi_register(struct ath11k_pci *ab_pci)
{
	struct ath11k_base *ab = ab_pci->ab;
	struct mhi_controller *mhi_ctrl;
	struct mhi_controller_config *ath11k_mhi_config;
	int ret;

	mhi_ctrl = mhi_alloc_controller();
	if (!mhi_ctrl)
		return -ENOMEM;

	ath11k_core_create_firmware_path(ab, ATH11K_AMSS_FILE,
					 ab_pci->amss_path,
					 sizeof(ab_pci->amss_path));

	ab_pci->mhi_ctrl = mhi_ctrl;
	mhi_ctrl->cntrl_dev = ab->dev;
	mhi_ctrl->fw_image = ab_pci->amss_path;
	mhi_ctrl->regs = ab->mem;
	mhi_ctrl->reg_len = ab->mem_len;

	ret = ath11k_mhi_get_msi(ab_pci);
	if (ret) {
		ath11k_err(ab, "failed to get msi for mhi\n");
		goto free_controller;
	}

	if (!test_bit(ATH11K_FLAG_MULTI_MSI_VECTORS, &ab->dev_flags))
		mhi_ctrl->irq_flags = IRQF_SHARED | IRQF_NOBALANCING;

	if (test_bit(ATH11K_FLAG_FIXED_MEM_RGN, &ab->dev_flags)) {
		ret = ath11k_mhi_read_addr_from_dt(mhi_ctrl);
		if (ret < 0)
			goto free_controller;
	} else {
		mhi_ctrl->iova_start = 0;
		mhi_ctrl->iova_stop = 0xFFFFFFFF;
	}

	mhi_ctrl->rddm_size = RDDM_DUMP_SIZE;
	mhi_ctrl->sbl_size = SZ_512K;
	mhi_ctrl->seg_len = SZ_512K;
	mhi_ctrl->fbc_download = true;
	mhi_ctrl->runtime_get = ath11k_mhi_op_runtime_get;
	mhi_ctrl->runtime_put = ath11k_mhi_op_runtime_put;
	mhi_ctrl->status_cb = ath11k_mhi_op_status_cb;
	mhi_ctrl->read_reg = ath11k_mhi_op_read_reg;
	mhi_ctrl->write_reg = ath11k_mhi_op_write_reg;

	switch (ab->hw_rev) {
	case ATH11K_HW_QCN9074_HW10:
		ath11k_mhi_config = &ath11k_mhi_config_qcn9074;
		break;
	case ATH11K_HW_QCA6390_HW20:
	case ATH11K_HW_WCN6855_HW20:
	case ATH11K_HW_WCN6855_HW21:
		ath11k_mhi_config = &ath11k_mhi_config_qca6390;
		break;
	default:
		ath11k_err(ab, "failed assign mhi_config for unknown hw rev %d\n",
			   ab->hw_rev);
		ret = -EINVAL;
		goto free_controller;
	}

	ret = mhi_register_controller(mhi_ctrl, ath11k_mhi_config);
	if (ret) {
		ath11k_err(ab, "failed to register to mhi bus, err = %d\n", ret);
		goto free_controller;
	}

	return 0;

free_controller:
	mhi_free_controller(mhi_ctrl);
	ab_pci->mhi_ctrl = NULL;
	return ret;
}

void ath11k_mhi_unregister(struct ath11k_pci *ab_pci)
{
	struct mhi_controller *mhi_ctrl = ab_pci->mhi_ctrl;

	mhi_unregister_controller(mhi_ctrl);
	kfree(mhi_ctrl->irq);
	mhi_free_controller(mhi_ctrl);
}

int ath11k_mhi_start(struct ath11k_pci *ab_pci)
{
	struct ath11k_base *ab = ab_pci->ab;
	int ret;

	ab_pci->mhi_ctrl->timeout_ms = MHI_TIMEOUT_DEFAULT_MS;

	ret = mhi_prepare_for_power_up(ab_pci->mhi_ctrl);
	if (ret) {
		ath11k_warn(ab, "failed to prepare mhi: %d", ret);
		return ret;
	}

	ret = mhi_sync_power_up(ab_pci->mhi_ctrl);
	if (ret) {
		ath11k_warn(ab, "failed to power up mhi: %d", ret);
		return ret;
	}

	return 0;
}

void ath11k_mhi_stop(struct ath11k_pci *ab_pci)
{
	mhi_power_down(ab_pci->mhi_ctrl, true);
	mhi_unprepare_after_power_down(ab_pci->mhi_ctrl);
}

int ath11k_mhi_suspend(struct ath11k_pci *ab_pci)
{
	struct ath11k_base *ab = ab_pci->ab;
	int ret;

	ret = mhi_pm_suspend(ab_pci->mhi_ctrl);
	if (ret) {
		ath11k_warn(ab, "failed to suspend mhi: %d", ret);
		return ret;
	}

	return 0;
}

int ath11k_mhi_resume(struct ath11k_pci *ab_pci)
{
	struct ath11k_base *ab = ab_pci->ab;
	int ret;

	/* Do force MHI resume as some devices like QCA6390, WCN6855
	 * are not in M3 state but they are functional. So just ignore
	 * the MHI state while resuming.
	 */
	ret = mhi_pm_resume_force(ab_pci->mhi_ctrl);
	if (ret) {
		ath11k_warn(ab, "failed to resume mhi: %d", ret);
		return ret;
	}

	return 0;
}
