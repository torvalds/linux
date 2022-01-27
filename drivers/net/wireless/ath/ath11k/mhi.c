// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Copyright (c) 2020 The Linux Foundation. All rights reserved. */

#include <linux/msi.h>
#include <linux/pci.h>

#include "core.h"
#include "debug.h"
#include "mhi.h"

#define MHI_TIMEOUT_DEFAULT_MS	90000

static struct mhi_channel_config ath11k_mhi_channels[] = {
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
		.auto_start = false,
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
		.auto_start = false,
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
		.auto_start = true,
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
		.auto_start = true,
	},
};

static struct mhi_event_config ath11k_mhi_events[] = {
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

static struct mhi_controller_config ath11k_mhi_config = {
	.max_channels = 128,
	.timeout_ms = 2000,
	.use_bounce_buf = false,
	.buf_len = 0,
	.num_channels = ARRAY_SIZE(ath11k_mhi_channels),
	.ch_cfg = ath11k_mhi_channels,
	.num_events = ARRAY_SIZE(ath11k_mhi_events),
	.event_cfg = ath11k_mhi_events,
};

void ath11k_mhi_set_mhictrl_reset(struct ath11k_base *ab)
{
	u32 val;

	val = ath11k_pci_read32(ab, MHISTATUS);

	ath11k_dbg(ab, ATH11K_DBG_PCI, "MHISTATUS 0x%x\n", val);

	/* Observed on QCA6390 that after SOC_GLOBAL_RESET, MHISTATUS
	 * has SYSERR bit set and thus need to set MHICTRL_RESET
	 * to clear SYSERR.
	 */
	ath11k_pci_write32(ab, MHICTRL, MHICTRL_RESET_MASK);

	mdelay(10);
}

static void ath11k_mhi_reset_txvecdb(struct ath11k_base *ab)
{
	ath11k_pci_write32(ab, PCIE_TXVECDB, 0);
}

static void ath11k_mhi_reset_txvecstatus(struct ath11k_base *ab)
{
	ath11k_pci_write32(ab, PCIE_TXVECSTATUS, 0);
}

static void ath11k_mhi_reset_rxvecdb(struct ath11k_base *ab)
{
	ath11k_pci_write32(ab, PCIE_RXVECDB, 0);
}

static void ath11k_mhi_reset_rxvecstatus(struct ath11k_base *ab)
{
	ath11k_pci_write32(ab, PCIE_RXVECSTATUS, 0);
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

	ret = ath11k_pci_get_user_msi_assignment(ab_pci,
						 "MHI", &num_vectors,
						 &user_base_data, &base_vector);
	if (ret)
		return ret;

	ath11k_dbg(ab, ATH11K_DBG_PCI, "Number of assigned MSI for MHI is %d, base vector is %d\n",
		   num_vectors, base_vector);

	irq = kcalloc(num_vectors, sizeof(int), GFP_KERNEL);
	if (!irq)
		return -ENOMEM;

	for (i = 0; i < num_vectors; i++)
		irq[i] = ath11k_pci_get_msi_irq(ab->dev,
						base_vector + i);

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

static void ath11k_mhi_op_status_cb(struct mhi_controller *mhi_cntrl,
				    enum mhi_callback cb)
{
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

int ath11k_mhi_register(struct ath11k_pci *ab_pci)
{
	struct ath11k_base *ab = ab_pci->ab;
	struct mhi_controller *mhi_ctrl;
	int ret;

	mhi_ctrl = kzalloc(sizeof(*mhi_ctrl), GFP_KERNEL);
	if (!mhi_ctrl)
		return -ENOMEM;

	ath11k_core_create_firmware_path(ab, ATH11K_AMSS_FILE,
					 ab_pci->amss_path,
					 sizeof(ab_pci->amss_path));

	ab_pci->mhi_ctrl = mhi_ctrl;
	mhi_ctrl->cntrl_dev = ab->dev;
	mhi_ctrl->fw_image = ab_pci->amss_path;
	mhi_ctrl->regs = ab->mem;

	ret = ath11k_mhi_get_msi(ab_pci);
	if (ret) {
		ath11k_err(ab, "failed to get msi for mhi\n");
		kfree(mhi_ctrl);
		return ret;
	}

	mhi_ctrl->iova_start = 0;
	mhi_ctrl->iova_stop = 0xffffffff;
	mhi_ctrl->sbl_size = SZ_512K;
	mhi_ctrl->seg_len = SZ_512K;
	mhi_ctrl->fbc_download = true;
	mhi_ctrl->runtime_get = ath11k_mhi_op_runtime_get;
	mhi_ctrl->runtime_put = ath11k_mhi_op_runtime_put;
	mhi_ctrl->status_cb = ath11k_mhi_op_status_cb;
	mhi_ctrl->read_reg = ath11k_mhi_op_read_reg;
	mhi_ctrl->write_reg = ath11k_mhi_op_write_reg;

	ret = mhi_register_controller(mhi_ctrl, &ath11k_mhi_config);
	if (ret) {
		ath11k_err(ab, "failed to register to mhi bus, err = %d\n", ret);
		kfree(mhi_ctrl);
		return ret;
	}

	return 0;
}

void ath11k_mhi_unregister(struct ath11k_pci *ab_pci)
{
	struct mhi_controller *mhi_ctrl = ab_pci->mhi_ctrl;

	mhi_unregister_controller(mhi_ctrl);
	kfree(mhi_ctrl->irq);
}

static char *ath11k_mhi_state_to_str(enum ath11k_mhi_state mhi_state)
{
	switch (mhi_state) {
	case ATH11K_MHI_INIT:
		return "INIT";
	case ATH11K_MHI_DEINIT:
		return "DEINIT";
	case ATH11K_MHI_POWER_ON:
		return "POWER_ON";
	case ATH11K_MHI_POWER_OFF:
		return "POWER_OFF";
	case ATH11K_MHI_FORCE_POWER_OFF:
		return "FORCE_POWER_OFF";
	case ATH11K_MHI_SUSPEND:
		return "SUSPEND";
	case ATH11K_MHI_RESUME:
		return "RESUME";
	case ATH11K_MHI_TRIGGER_RDDM:
		return "TRIGGER_RDDM";
	case ATH11K_MHI_RDDM_DONE:
		return "RDDM_DONE";
	default:
		return "UNKNOWN";
	}
};

static void ath11k_mhi_set_state_bit(struct ath11k_pci *ab_pci,
				     enum ath11k_mhi_state mhi_state)
{
	struct ath11k_base *ab = ab_pci->ab;

	switch (mhi_state) {
	case ATH11K_MHI_INIT:
		set_bit(ATH11K_MHI_INIT, &ab_pci->mhi_state);
		break;
	case ATH11K_MHI_DEINIT:
		clear_bit(ATH11K_MHI_INIT, &ab_pci->mhi_state);
		break;
	case ATH11K_MHI_POWER_ON:
		set_bit(ATH11K_MHI_POWER_ON, &ab_pci->mhi_state);
		break;
	case ATH11K_MHI_POWER_OFF:
	case ATH11K_MHI_FORCE_POWER_OFF:
		clear_bit(ATH11K_MHI_POWER_ON, &ab_pci->mhi_state);
		clear_bit(ATH11K_MHI_TRIGGER_RDDM, &ab_pci->mhi_state);
		clear_bit(ATH11K_MHI_RDDM_DONE, &ab_pci->mhi_state);
		break;
	case ATH11K_MHI_SUSPEND:
		set_bit(ATH11K_MHI_SUSPEND, &ab_pci->mhi_state);
		break;
	case ATH11K_MHI_RESUME:
		clear_bit(ATH11K_MHI_SUSPEND, &ab_pci->mhi_state);
		break;
	case ATH11K_MHI_TRIGGER_RDDM:
		set_bit(ATH11K_MHI_TRIGGER_RDDM, &ab_pci->mhi_state);
		break;
	case ATH11K_MHI_RDDM_DONE:
		set_bit(ATH11K_MHI_RDDM_DONE, &ab_pci->mhi_state);
		break;
	default:
		ath11k_err(ab, "unhandled mhi state (%d)\n", mhi_state);
	}
}

static int ath11k_mhi_check_state_bit(struct ath11k_pci *ab_pci,
				      enum ath11k_mhi_state mhi_state)
{
	struct ath11k_base *ab = ab_pci->ab;

	switch (mhi_state) {
	case ATH11K_MHI_INIT:
		if (!test_bit(ATH11K_MHI_INIT, &ab_pci->mhi_state))
			return 0;
		break;
	case ATH11K_MHI_DEINIT:
	case ATH11K_MHI_POWER_ON:
		if (test_bit(ATH11K_MHI_INIT, &ab_pci->mhi_state) &&
		    !test_bit(ATH11K_MHI_POWER_ON, &ab_pci->mhi_state))
			return 0;
		break;
	case ATH11K_MHI_FORCE_POWER_OFF:
		if (test_bit(ATH11K_MHI_POWER_ON, &ab_pci->mhi_state))
			return 0;
		break;
	case ATH11K_MHI_POWER_OFF:
	case ATH11K_MHI_SUSPEND:
		if (test_bit(ATH11K_MHI_POWER_ON, &ab_pci->mhi_state) &&
		    !test_bit(ATH11K_MHI_SUSPEND, &ab_pci->mhi_state))
			return 0;
		break;
	case ATH11K_MHI_RESUME:
		if (test_bit(ATH11K_MHI_SUSPEND, &ab_pci->mhi_state))
			return 0;
		break;
	case ATH11K_MHI_TRIGGER_RDDM:
		if (test_bit(ATH11K_MHI_POWER_ON, &ab_pci->mhi_state) &&
		    !test_bit(ATH11K_MHI_TRIGGER_RDDM, &ab_pci->mhi_state))
			return 0;
		break;
	case ATH11K_MHI_RDDM_DONE:
		return 0;
	default:
		ath11k_err(ab, "unhandled mhi state: %s(%d)\n",
			   ath11k_mhi_state_to_str(mhi_state), mhi_state);
	}

	ath11k_err(ab, "failed to set mhi state %s(%d) in current mhi state (0x%lx)\n",
		   ath11k_mhi_state_to_str(mhi_state), mhi_state,
		   ab_pci->mhi_state);

	return -EINVAL;
}

static int ath11k_mhi_set_state(struct ath11k_pci *ab_pci,
				enum ath11k_mhi_state mhi_state)
{
	struct ath11k_base *ab = ab_pci->ab;
	int ret;

	ret = ath11k_mhi_check_state_bit(ab_pci, mhi_state);
	if (ret)
		goto out;

	ath11k_dbg(ab, ATH11K_DBG_PCI, "setting mhi state: %s(%d)\n",
		   ath11k_mhi_state_to_str(mhi_state), mhi_state);

	switch (mhi_state) {
	case ATH11K_MHI_INIT:
		ret = mhi_prepare_for_power_up(ab_pci->mhi_ctrl);
		break;
	case ATH11K_MHI_DEINIT:
		mhi_unprepare_after_power_down(ab_pci->mhi_ctrl);
		ret = 0;
		break;
	case ATH11K_MHI_POWER_ON:
		ret = mhi_sync_power_up(ab_pci->mhi_ctrl);
		break;
	case ATH11K_MHI_POWER_OFF:
		mhi_power_down(ab_pci->mhi_ctrl, true);
		ret = 0;
		break;
	case ATH11K_MHI_FORCE_POWER_OFF:
		mhi_power_down(ab_pci->mhi_ctrl, false);
		ret = 0;
		break;
	case ATH11K_MHI_SUSPEND:
		break;
	case ATH11K_MHI_RESUME:
		break;
	case ATH11K_MHI_TRIGGER_RDDM:
		ret = mhi_force_rddm_mode(ab_pci->mhi_ctrl);
		break;
	case ATH11K_MHI_RDDM_DONE:
		break;
	default:
		ath11k_err(ab, "unhandled MHI state (%d)\n", mhi_state);
		ret = -EINVAL;
	}

	if (ret)
		goto out;

	ath11k_mhi_set_state_bit(ab_pci, mhi_state);

	return 0;

out:
	ath11k_err(ab, "failed to set mhi state: %s(%d)\n",
		   ath11k_mhi_state_to_str(mhi_state), mhi_state);
	return ret;
}

int ath11k_mhi_start(struct ath11k_pci *ab_pci)
{
	int ret;

	ab_pci->mhi_ctrl->timeout_ms = MHI_TIMEOUT_DEFAULT_MS;

	ret = ath11k_mhi_set_state(ab_pci, ATH11K_MHI_INIT);
	if (ret)
		goto out;

	ret = ath11k_mhi_set_state(ab_pci, ATH11K_MHI_POWER_ON);
	if (ret)
		goto out;

	return 0;

out:
	return ret;
}

void ath11k_mhi_stop(struct ath11k_pci *ab_pci)
{
	ath11k_mhi_set_state(ab_pci, ATH11K_MHI_POWER_OFF);
	ath11k_mhi_set_state(ab_pci, ATH11K_MHI_DEINIT);
}

