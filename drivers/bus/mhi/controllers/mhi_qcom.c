// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/memblock.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mhi.h>
#include <linux/mhi_misc.h>
#include "mhi_qcom.h"

#define MHI_CHANNEL_CONFIG_UL(ch_num, ch_name, elems, ev_ring, ee,	\
			      dbmode, lpm, poll, offload, modeswitch,	\
			      ch_type)					\
	{								\
		.dir = DMA_TO_DEVICE,					\
		.num = ch_num,						\
		.name = ch_name,					\
		.num_elements = elems,					\
		.event_ring = ev_ring,					\
		.ee_mask = BIT(ee),					\
		.pollcfg = poll,					\
		.doorbell = dbmode,					\
		.lpm_notify = lpm,					\
		.offload_channel = offload,				\
		.doorbell_mode_switch = modeswitch,			\
		.wake_capable = false,					\
		.auto_queue = false,					\
		.local_elements = 0,					\
		.type = ch_type,					\
	}

#define MHI_CHANNEL_CONFIG_DL(ch_num, ch_name, elems, ev_ring, ee,	\
			      dbmode, lpm, poll, offload, modeswitch,	\
			      wake, autoq, local_el, ch_type)		\
	{								\
		.dir = DMA_FROM_DEVICE,					\
		.num = ch_num,						\
		.name = ch_name,					\
		.num_elements = elems,					\
		.event_ring = ev_ring,					\
		.ee_mask = BIT(ee),					\
		.pollcfg = poll,					\
		.doorbell = dbmode,					\
		.lpm_notify = lpm,					\
		.offload_channel = offload,				\
		.doorbell_mode_switch = modeswitch,			\
		.wake_capable = wake,					\
		.auto_queue = autoq,					\
		.local_elements = local_el,				\
		.type = ch_type,					\
	}

#define MHI_EVENT_CONFIG(ev_ring, ev_irq, type, num_elems, int_mod,	\
			 prio, dbmode, hw, cl_manage, offload, ch_num)	\
	{								\
		.num_elements = num_elems,				\
		.irq_moderation_ms = int_mod,				\
		.irq = ev_irq,						\
		.priority = prio,					\
		.mode = dbmode,						\
		.data_type = type,					\
		.hardware_event = hw,					\
		.client_managed = cl_manage,				\
		.offload_channel = offload,				\
		.channel = ch_num,					\
	}

static const struct mhi_channel_config modem_qcom_sdx65_mhi_channels[] = {
	/* SBL channels  */
	MHI_CHANNEL_CONFIG_UL(2, "SAHARA", 128, 1, MHI_EE_SBL,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(3, "SAHARA", 128, 1, MHI_EE_SBL,
			      MHI_DB_BRST_DISABLE, false, 0, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_DL(25, "BL", 32, 1, MHI_EE_SBL,
			      MHI_DB_BRST_DISABLE, false, 0, false, false,
			      false, false, 0, 0),
	/* AMSS channels */
	MHI_CHANNEL_CONFIG_UL(0, "LOOPBACK", 64, 2, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(1, "LOOPBACK", 64, 2, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(4, "DIAG", 64, 1, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(5, "DIAG", 64, 3, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(8, "QDSS", 64, 1, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(9, "QDSS", 64, 1, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(10, "EFS", 64, 1, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, 0),
	/* wake-capable */
	MHI_CHANNEL_CONFIG_DL(11, "EFS", 64, 1, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false,
			      true, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(14, "QMI0", 64, 1, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(15, "QMI0", 64, 2, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(16, "QMI1", 64, 3, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(17, "QMI1", 64, 3, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(18, "IP_CTRL", 64, 1, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, 0),
	/* auto-queue */
	MHI_CHANNEL_CONFIG_DL(19, "IP_CTRL", 64, 1, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false,
			      false, true, 0, 0),
	MHI_CHANNEL_CONFIG_UL(20, "IPCR", 32, 2, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, 0),
	/* auto-queue */
	MHI_CHANNEL_CONFIG_DL(21, "IPCR", 32, 2, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false,
			      false, true, 0, 0),
	MHI_CHANNEL_CONFIG_UL(26, "DCI", 64, 3, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(27, "DCI", 64, 3, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(32, "DUN", 64, 3, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(33, "DUN", 64, 3, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(80, "AUDIO_VOICE_0", 32, 1, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(81, "AUDIO_VOICE_0", 32, 1, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(100, "IP_HW0", 512, 6, MHI_EE_AMSS,
			      MHI_DB_BRST_ENABLE, false, 0, false, true, 0),
	MHI_CHANNEL_CONFIG_DL(101, "IP_HW0", 512, 7, MHI_EE_AMSS,
			      MHI_DB_BRST_ENABLE, false, 0, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_DL(102, "IP_HW_ADPL", 1, 8, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, true, 0, true, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_DL(103, "IP_HW_QDSS", 1, 9, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_DL(104, "IP_HW0_RSC", 512, 7, MHI_EE_AMSS,
			      MHI_DB_BRST_ENABLE, false, 0, false, false,
			      false, false, 3078,
			      MHI_CH_TYPE_INBOUND_COALESCED),
	MHI_CHANNEL_CONFIG_UL(105, "RMNET_DATA_LL", 512, 10, MHI_EE_AMSS,
			      MHI_DB_BRST_ENABLE, false, 0, false, true, 0),
	MHI_CHANNEL_CONFIG_DL(106, "RMNET_DATA_LL", 512, 10, MHI_EE_AMSS,
			      MHI_DB_BRST_ENABLE, false, 0, false, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(107, "IP_HW_MHIP_1", 1, 11, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, true, true, 0),
	MHI_CHANNEL_CONFIG_DL(108, "IP_HW_MHIP_1", 1, 12, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, true, 0, true, false,
			      false, false, 0, 0),
	MHI_CHANNEL_CONFIG_UL(109, "RMNET_CTL", 128, 13, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false, 0),
	MHI_CHANNEL_CONFIG_DL(110, "RMNET_CTL", 128, 14, MHI_EE_AMSS,
			      MHI_DB_BRST_DISABLE, false, 0, false, false,
			      false, false, 0, 0),
};

static struct mhi_event_config modem_qcom_sdx65_mhi_events[] = {
	MHI_EVENT_CONFIG(0, 1, MHI_ER_CTRL, 64, 0,
			 MHI_ER_PRIORITY_HI_NOSLEEP, MHI_DB_BRST_DISABLE, false,
			 false, false, 0),
	MHI_EVENT_CONFIG(1, 2, MHI_ER_DATA, 256, 0,
			 MHI_ER_PRIORITY_DEFAULT_NOSLEEP, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(2, 3, MHI_ER_DATA, 256, 0,
			 MHI_ER_PRIORITY_DEFAULT_NOSLEEP, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(3, 4, MHI_ER_DATA, 256, 0,
			 MHI_ER_PRIORITY_DEFAULT_NOSLEEP, MHI_DB_BRST_DISABLE,
			 false, false, false, 0),
	MHI_EVENT_CONFIG(4, 5, MHI_ER_BW_SCALE, 64, 0,
			 MHI_ER_PRIORITY_HI_SLEEP, MHI_DB_BRST_DISABLE, false,
			 false, false, 0),
	MHI_EVENT_CONFIG(5, 6, MHI_ER_TIMESYNC, 64, 0,
			 MHI_ER_PRIORITY_HI_SLEEP, MHI_DB_BRST_DISABLE, false,
			 false, false, 0),
	/* Hardware channels request dedicated hardware event rings */
	MHI_EVENT_CONFIG(6, 7, MHI_ER_DATA, 1024, 5,
			 MHI_ER_PRIORITY_DEFAULT_NOSLEEP, MHI_DB_BRST_ENABLE,
			 true, false, false, 100),
	MHI_EVENT_CONFIG(7, 7, MHI_ER_DATA, 2048, 5,
			 MHI_ER_PRIORITY_DEFAULT_NOSLEEP,
			 MHI_DB_BRST_ENABLE, true, true, false, 101),
	MHI_EVENT_CONFIG(8, 8, MHI_ER_DATA, 0, 0,
			 MHI_ER_PRIORITY_DEFAULT_NOSLEEP, MHI_DB_BRST_ENABLE,
			 true, true, true, 102),
	MHI_EVENT_CONFIG(9, 9, MHI_ER_DATA, 1024, 5,
			 MHI_ER_PRIORITY_DEFAULT_NOSLEEP, MHI_DB_BRST_DISABLE,
			 true, false, false, 103),
	MHI_EVENT_CONFIG(10, 10, MHI_ER_DATA, 1024, 1,
			 MHI_ER_PRIORITY_HI_NOSLEEP, MHI_DB_BRST_ENABLE, true,
			 false, false, 0),
	MHI_EVENT_CONFIG(11, 11, MHI_ER_DATA, 0, 0,
			 MHI_ER_PRIORITY_DEFAULT_NOSLEEP, MHI_DB_BRST_ENABLE,
			 true, true, true, 107),
	MHI_EVENT_CONFIG(12, 12, MHI_ER_DATA, 0, 0,
			 MHI_ER_PRIORITY_DEFAULT_NOSLEEP, MHI_DB_BRST_ENABLE,
			 true, true, true, 108),
	MHI_EVENT_CONFIG(13, 13, MHI_ER_DATA, 1024, 1,
			 MHI_ER_PRIORITY_HI_NOSLEEP, MHI_DB_BRST_DISABLE, true,
			 false, false, 109),
	MHI_EVENT_CONFIG(14, 15, MHI_ER_DATA, 1024, 0,
			 MHI_ER_PRIORITY_HI_NOSLEEP, MHI_DB_BRST_DISABLE, true,
			 false, false, 110),
};

static const struct mhi_controller_config modem_qcom_sdx65_mhi_config = {
	.max_channels = 128,
	.timeout_ms = 2000,
	.buf_len = 0x8000,
	.num_channels = ARRAY_SIZE(modem_qcom_sdx65_mhi_channels),
	.ch_cfg = modem_qcom_sdx65_mhi_channels,
	.num_events = ARRAY_SIZE(modem_qcom_sdx65_mhi_events),
	.event_cfg = modem_qcom_sdx65_mhi_events,
};

static const struct mhi_pci_dev_info mhi_qcom_sdx65_info = {
	.device_id = 0x0308,
	.name = "esoc0",
	.fw_image = "sdx65m/xbl.elf",
	.edl_image = "sdx65m/edl.mbn",
	.config = &modem_qcom_sdx65_mhi_config,
	.bar_num = MHI_PCI_BAR_NUM,
	.dma_data_width = 64,
	.allow_m1 = false,
	.skip_forced_suspend = true,
	.sfr_support = true,
	.timesync = true,
	.drv_support = false,
};

static const struct mhi_pci_dev_info mhi_qcom_sdx75_info = {
	.device_id = 0x0309,
	.name = "esoc0",
	.fw_image = "sdx75m/xbl.elf",
	.edl_image = "sdx75m/edl.mbn",
	.config = &modem_qcom_sdx65_mhi_config,
	.bar_num = MHI_PCI_BAR_NUM,
	.dma_data_width = 64,
	.allow_m1 = false,
	.skip_forced_suspend = true,
	.sfr_support = true,
	.timesync = true,
	.drv_support = false,
};

static const struct mhi_pci_dev_info mhi_qcom_debug_info = {
	.device_id = MHI_PCIE_DEBUG_ID,
	.name = "qcom-debug",
	.fw_image = "debug.mbn",
	.edl_image = "debug.mbn",
	.config = &modem_qcom_sdx65_mhi_config,
	.bar_num = MHI_PCI_BAR_NUM,
	.dma_data_width = 64,
	.allow_m1 = true,
	.skip_forced_suspend = true,
	.sfr_support = false,
	.timesync = false,
	.drv_support = false,
};

static const struct pci_device_id mhi_pcie_device_id[] = {
	{ PCI_DEVICE(MHI_PCIE_VENDOR_ID, 0x0308),
		.driver_data = (kernel_ulong_t) &mhi_qcom_sdx65_info },
	{ PCI_DEVICE(MHI_PCIE_VENDOR_ID, 0x0309),
		.driver_data = (kernel_ulong_t) &mhi_qcom_sdx75_info },
	{ PCI_DEVICE(MHI_PCIE_VENDOR_ID, MHI_PCIE_DEBUG_ID),
		.driver_data = (kernel_ulong_t) &mhi_qcom_debug_info },
	{  }
};
MODULE_DEVICE_TABLE(pci, mhi_pcie_device_id);

static enum mhi_debug_mode debug_mode;

const char * const mhi_debug_mode_str[MHI_DEBUG_MODE_MAX] = {
	[MHI_DEBUG_OFF] = "Debug mode OFF",
	[MHI_DEBUG_ON] = "Debug mode ON",
	[MHI_DEBUG_NO_LPM] = "Debug mode - no LPM",
};

const char * const mhi_suspend_mode_str[MHI_SUSPEND_MODE_MAX] = {
	[MHI_ACTIVE_STATE] = "Active",
	[MHI_DEFAULT_SUSPEND] = "Default",
	[MHI_FAST_LINK_OFF] = "Fast Link Off",
	[MHI_FAST_LINK_ON] = "Fast Link On",
};

static int mhi_qcom_power_up(struct mhi_controller *mhi_cntrl);

static int mhi_link_status(struct mhi_controller *mhi_cntrl)
{
	struct pci_dev *pci_dev = to_pci_dev(mhi_cntrl->cntrl_dev);
	u16 dev_id;
	int ret;

	/* try reading device IDs, a mismatch could indicate a link down */
	ret = pci_read_config_word(pci_dev, PCI_DEVICE_ID, &dev_id);

	return (ret || dev_id != pci_dev->device) ? -EIO : 0;
}

static int mhi_qcom_read_reg(struct mhi_controller *mhi_cntrl,
			     void __iomem *addr, u32 *out)
{
	u32 tmp = readl_relaxed(addr);

	if (PCI_INVALID_READ(tmp) && mhi_link_status(mhi_cntrl))
		return -EIO;

	*out = tmp;

	return 0;
}

static void mhi_qcom_write_reg(struct mhi_controller *mhi_cntrl,
			       void __iomem *addr, u32 val)
{
	writel_relaxed(val, addr);
}

static u64 mhi_qcom_time_get(struct mhi_controller *mhi_cntrl)
{
	return mhi_arch_time_get(mhi_cntrl);
}

static int mhi_qcom_lpm_disable(struct mhi_controller *mhi_cntrl)
{
	return mhi_arch_link_lpm_disable(mhi_cntrl);
}

static int mhi_qcom_lpm_enable(struct mhi_controller *mhi_cntrl)
{
	return mhi_arch_link_lpm_enable(mhi_cntrl);
}

static int mhi_debugfs_power_up(void *data, u64 val)
{
	struct mhi_controller *mhi_cntrl = data;
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	int ret;

	if (!val || mhi_priv->powered_on)
		return -EINVAL;

	MHI_CNTRL_LOG("Trigger power up from %s\n",
		      TO_MHI_DEBUG_MODE_STR(debug_mode));

	ret = mhi_qcom_power_up(mhi_cntrl);
	if (ret) {
		MHI_CNTRL_ERR("Failed to power up MHI\n");
		return ret;
	}

	mhi_priv->powered_on = true;

	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(debugfs_power_up_fops, NULL,
			 mhi_debugfs_power_up, "%llu\n");

static int mhi_debugfs_trigger_m0(void *data, u64 val)
{
	struct mhi_controller *mhi_cntrl = data;

	MHI_CNTRL_LOG("Trigger M3 Exit\n");
	pm_runtime_get(mhi_cntrl->cntrl_dev);
	pm_runtime_put(mhi_cntrl->cntrl_dev);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(debugfs_trigger_m0_fops, NULL,
			 mhi_debugfs_trigger_m0, "%llu\n");

static int mhi_debugfs_trigger_m3(void *data, u64 val)
{
	struct mhi_controller *mhi_cntrl = data;

	MHI_CNTRL_LOG("Trigger M3 Entry\n");
	pm_runtime_mark_last_busy(mhi_cntrl->cntrl_dev);
	pm_request_autosuspend(mhi_cntrl->cntrl_dev);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(debugfs_trigger_m3_fops, NULL,
			 mhi_debugfs_trigger_m3, "%llu\n");

static int mhi_debugfs_disable_pci_lpm_get(void *data, u64 *val)
{
	struct mhi_controller *mhi_cntrl = data;
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);

	*val = mhi_priv->disable_pci_lpm;

	MHI_CNTRL_LOG("PCIe low power modes (D3 hot/cold) are %s\n",
		      mhi_priv->disable_pci_lpm ? "Disabled" : "Enabled");

	return 0;
}

static int mhi_debugfs_disable_pci_lpm_set(void *data, u64 val)
{
	struct mhi_controller *mhi_cntrl = data;
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);

	mutex_lock(&mhi_cntrl->pm_mutex);
	mhi_priv->disable_pci_lpm = val ? true : false;
	mutex_unlock(&mhi_cntrl->pm_mutex);

	MHI_CNTRL_LOG("%s PCIe low power modes (D3 hot/cold)\n",
		      val ? "Disabled" : "Enabled");

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(debugfs_pci_lpm_fops, mhi_debugfs_disable_pci_lpm_get,
			 mhi_debugfs_disable_pci_lpm_set, "%llu\n");

void mhi_deinit_pci_dev(struct pci_dev *pci_dev,
			const struct mhi_pci_dev_info *dev_info)
{
	struct mhi_controller *mhi_cntrl = dev_get_drvdata(&pci_dev->dev);

	if (!mhi_cntrl)
		return;

	pm_runtime_mark_last_busy(mhi_cntrl->cntrl_dev);
	pm_runtime_dont_use_autosuspend(mhi_cntrl->cntrl_dev);
	pm_runtime_disable(mhi_cntrl->cntrl_dev);

	pci_free_irq_vectors(pci_dev);
	kfree(mhi_cntrl->irq);
	mhi_cntrl->irq = NULL;
	iounmap(mhi_cntrl->regs);
	mhi_cntrl->regs = NULL;
	mhi_cntrl->reg_len = 0;
	mhi_cntrl->nr_irqs = 0;
	pci_clear_master(pci_dev);
	pci_release_region(pci_dev, dev_info->bar_num);
	pci_disable_device(pci_dev);
}

static int mhi_init_pci_dev(struct pci_dev *pci_dev,
			    const struct mhi_pci_dev_info *dev_info)
{
	struct mhi_controller *mhi_cntrl = dev_get_drvdata(&pci_dev->dev);
	phys_addr_t base;
	int ret;
	int i;

	if (!mhi_cntrl)
		return -ENODEV;

	ret = pci_assign_resource(pci_dev, dev_info->bar_num);
	if (ret) {
		MHI_CNTRL_ERR("Error assign pci resources, ret: %d\n", ret);
		return ret;
	}

	ret = pci_enable_device(pci_dev);
	if (ret) {
		MHI_CNTRL_ERR("Error enabling device, ret: %d\n", ret);
		goto error_enable_device;
	}

	ret = pci_request_region(pci_dev, dev_info->bar_num, "mhi");
	if (ret) {
		MHI_CNTRL_ERR("Error pci_request_region, ret: %d\n", ret);
		goto error_request_region;
	}

	pci_set_master(pci_dev);

	base = pci_resource_start(pci_dev, dev_info->bar_num);
	mhi_cntrl->reg_len = pci_resource_len(pci_dev, dev_info->bar_num);
	mhi_cntrl->regs = ioremap(base, mhi_cntrl->reg_len);
	if (!mhi_cntrl->regs) {
		MHI_CNTRL_ERR("Error ioremap region\n");
		goto error_ioremap;
	}

	/* reserved MSI for BHI plus one for each event ring */
	mhi_cntrl->nr_irqs = dev_info->config->num_events + 1;

	ret = pci_alloc_irq_vectors(pci_dev, mhi_cntrl->nr_irqs,
				    mhi_cntrl->nr_irqs, PCI_IRQ_MSI);
	if (IS_ERR_VALUE((ulong)ret) || ret < mhi_cntrl->nr_irqs) {
		MHI_CNTRL_ERR("Failed to enable MSI, ret: %d\n", ret);
		goto error_req_msi;
	}

	mhi_cntrl->irq = kmalloc_array(mhi_cntrl->nr_irqs,
				       sizeof(*mhi_cntrl->irq), GFP_KERNEL);
	if (!mhi_cntrl->irq) {
		ret = -ENOMEM;
		goto error_alloc_msi_vec;
	}

	for (i = 0; i < mhi_cntrl->nr_irqs; i++) {
		mhi_cntrl->irq[i] = pci_irq_vector(pci_dev, i);
		if (mhi_cntrl->irq[i] < 0) {
			ret = mhi_cntrl->irq[i];
			goto error_get_irq_vec;
		}
	}

	/* configure runtime pm */
	pm_runtime_set_autosuspend_delay(mhi_cntrl->cntrl_dev,
					 MHI_RPM_SUSPEND_TMR_MS);
	pm_runtime_use_autosuspend(mhi_cntrl->cntrl_dev);
	pm_suspend_ignore_children(mhi_cntrl->cntrl_dev, true);

	/*
	 * pci framework will increment usage count (twice) before
	 * calling local device driver probe function.
	 * 1st pci.c pci_pm_init() calls pm_runtime_forbid
	 * 2nd pci-driver.c local_pci_probe calls pm_runtime_get_sync
	 * Framework expect pci device driver to call
	 * pm_runtime_put_noidle to decrement usage count after
	 * successful probe and call pm_runtime_allow to enable
	 * runtime suspend.
	 */
	pm_runtime_mark_last_busy(mhi_cntrl->cntrl_dev);
	pm_runtime_put_noidle(mhi_cntrl->cntrl_dev);

	return 0;

error_get_irq_vec:
	kfree(mhi_cntrl->irq);
	mhi_cntrl->irq = NULL;

error_alloc_msi_vec:
	pci_free_irq_vectors(pci_dev);

error_req_msi:
	iounmap(mhi_cntrl->regs);

error_ioremap:
	pci_clear_master(pci_dev);

error_request_region:
	pci_disable_device(pci_dev);

error_enable_device:
	pci_release_region(pci_dev, dev_info->bar_num);

	return ret;
}

static int mhi_runtime_suspend(struct device *dev)
{
	struct mhi_controller *mhi_cntrl = dev_get_drvdata(dev);
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	int ret = 0;

	MHI_CNTRL_LOG("Entered\n");

	mutex_lock(&mhi_cntrl->pm_mutex);

	if (!mhi_priv->powered_on) {
		MHI_CNTRL_LOG("Not fully powered, return success\n");
		mutex_unlock(&mhi_cntrl->pm_mutex);
		return 0;
	}

	ret = mhi_pm_suspend(mhi_cntrl);

	if (ret) {
		MHI_CNTRL_LOG("Abort due to ret: %d\n", ret);
		mhi_priv->suspend_mode = MHI_ACTIVE_STATE;
		goto exit_runtime_suspend;
	}

	mhi_priv->suspend_mode = MHI_DEFAULT_SUSPEND;

	ret = mhi_arch_link_suspend(mhi_cntrl);

	/* failed suspending link abort mhi suspend */
	if (ret) {
		MHI_CNTRL_LOG("Failed to suspend link, abort suspend\n");
		mhi_pm_resume(mhi_cntrl);
		mhi_priv->suspend_mode = MHI_ACTIVE_STATE;
	}

exit_runtime_suspend:
	mutex_unlock(&mhi_cntrl->pm_mutex);
	MHI_CNTRL_LOG("Exited with ret: %d\n", ret);

	return (ret < 0) ? -EBUSY : 0;
}

static int mhi_runtime_idle(struct device *dev)
{
	struct mhi_controller *mhi_cntrl = dev_get_drvdata(dev);

	MHI_CNTRL_LOG("Entered returning -EBUSY\n");

	/*
	 * RPM framework during runtime resume always calls
	 * rpm_idle to see if device ready to suspend.
	 * If dev.power usage_count count is 0, rpm fw will call
	 * rpm_idle cb to see if device is ready to suspend.
	 * if cb return 0, or cb not defined the framework will
	 * assume device driver is ready to suspend;
	 * therefore, fw will schedule runtime suspend.
	 * In MHI power management, MHI host shall go to
	 * runtime suspend only after entering MHI State M2, even if
	 * usage count is 0.  Return -EBUSY to disable automatic suspend.
	 */
	return -EBUSY;
}

static int mhi_runtime_resume(struct device *dev)
{
	int ret = 0;
	struct mhi_controller *mhi_cntrl = dev_get_drvdata(dev);
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);

	MHI_CNTRL_LOG("Entered\n");

	mutex_lock(&mhi_cntrl->pm_mutex);

	if (!mhi_priv->powered_on) {
		MHI_CNTRL_LOG("Not fully powered, return success\n");
		mutex_unlock(&mhi_cntrl->pm_mutex);
		return 0;
	}

	/* turn on link */
	ret = mhi_arch_link_resume(mhi_cntrl);
	if (ret)
		goto rpm_resume_exit;

	/* transition to M0 state */
	if (mhi_priv->suspend_mode == MHI_DEFAULT_SUSPEND)
		ret = mhi_pm_resume(mhi_cntrl);
	else
		ret = mhi_pm_fast_resume(mhi_cntrl, MHI_FAST_LINK_ON);

	mhi_priv->suspend_mode = MHI_ACTIVE_STATE;

rpm_resume_exit:
	mutex_unlock(&mhi_cntrl->pm_mutex);
	MHI_CNTRL_LOG("Exited with ret: %d\n", ret);

	return (ret < 0) ? -EBUSY : 0;
}

static int mhi_system_resume(struct device *dev)
{
	return mhi_runtime_resume(dev);
}

int mhi_system_suspend(struct device *dev)
{
	struct mhi_controller *mhi_cntrl = dev_get_drvdata(dev);
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	const struct mhi_pci_dev_info *dev_info = mhi_priv->dev_info;
	int ret;

	MHI_CNTRL_LOG("Entered\n");

	/* No DRV support - use regular suspends */
	if (!dev_info->drv_support)
		return mhi_runtime_suspend(dev);

	mutex_lock(&mhi_cntrl->pm_mutex);

	if (!mhi_priv->powered_on) {
		MHI_CNTRL_LOG("Not fully powered, return success\n");
		mutex_unlock(&mhi_cntrl->pm_mutex);
		return 0;
	}

	/*
	 * pci framework always makes a dummy vote to rpm
	 * framework to resume before calling system suspend
	 * hence usage count is minimum one
	 */
	if (atomic_read(&dev->power.usage_count) > 1) {
		/*
		 * clients have requested to keep link on, try
		 * fast suspend. No need to notify clients since
		 * we will not be turning off the pcie link
		 */
		ret = mhi_pm_fast_suspend(mhi_cntrl, false);
		mhi_priv->suspend_mode = MHI_FAST_LINK_ON;
	} else {
		/* try normal suspend */
		mhi_priv->suspend_mode = MHI_DEFAULT_SUSPEND;
		ret = mhi_pm_suspend(mhi_cntrl);

		/*
		 * normal suspend failed because we're busy, try
		 * fast suspend before aborting system suspend.
		 * this could happens if client has disabled
		 * device lpm but no active vote for PCIe from
		 * apps processor
		 */
		if (ret == -EBUSY) {
			ret = mhi_pm_fast_suspend(mhi_cntrl, true);
			mhi_priv->suspend_mode = MHI_FAST_LINK_ON;
		}
	}

	if (ret) {
		MHI_CNTRL_LOG("Abort due to ret: %d\n", ret);
		mhi_priv->suspend_mode = MHI_ACTIVE_STATE;
		goto exit_system_suspend;
	}

	ret = mhi_arch_link_suspend(mhi_cntrl);

	/* failed suspending link abort mhi suspend */
	if (ret) {
		MHI_CNTRL_LOG("Failed to suspend link, abort suspend\n");
		if (mhi_priv->suspend_mode == MHI_DEFAULT_SUSPEND)
			mhi_pm_resume(mhi_cntrl);
		else
			mhi_pm_fast_resume(mhi_cntrl, MHI_FAST_LINK_OFF);

		mhi_priv->suspend_mode = MHI_ACTIVE_STATE;
	}

exit_system_suspend:
	mutex_unlock(&mhi_cntrl->pm_mutex);

	MHI_CNTRL_LOG("Exited with ret: %d\n", ret);

	return ret;
}

static int mhi_suspend_noirq(struct device *dev)
{
	return 0;
}

static int mhi_resume_noirq(struct device *dev)
{
	return 0;
}

static int mhi_force_suspend(struct mhi_controller *mhi_cntrl)
{
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	int itr = DIV_ROUND_UP(mhi_cntrl->timeout_ms, 100);
	int ret = -EIO;

	MHI_CNTRL_LOG("Entered\n");

	mutex_lock(&mhi_cntrl->pm_mutex);

	for (; itr; itr--) {
		/*
		 * This function get called soon as device entered mission mode
		 * so most of the channels are still in disabled state. However,
		 * sbl channels are active and clients could be trying to close
		 * channels while we trying to suspend the link. So, we need to
		 * re-try if MHI is busy
		 */
		ret = mhi_pm_suspend(mhi_cntrl);
		if (!ret || ret != -EBUSY)
			break;

		MHI_CNTRL_LOG("MHI busy, sleeping and retry\n");
		msleep(100);
	}

	if (ret) {
		MHI_CNTRL_ERR("Force suspend ret:%d\n", ret);
		goto exit_force_suspend;
	}

	mhi_priv->suspend_mode = MHI_DEFAULT_SUSPEND;
	ret = mhi_arch_link_suspend(mhi_cntrl);

exit_force_suspend:
	mutex_unlock(&mhi_cntrl->pm_mutex);

	return ret;
}

static int mhi_qcom_power_up(struct mhi_controller *mhi_cntrl)
{
	int ret;

	/* when coming out of SSR, initial states are not valid */
	mhi_cntrl->ee = MHI_EE_MAX;
	mhi_cntrl->dev_state = MHI_STATE_RESET;

	ret = mhi_prepare_for_power_up(mhi_cntrl);
	if (ret)
		return ret;

	ret = mhi_async_power_up(mhi_cntrl);
	if (ret) {
		mhi_unprepare_after_power_down(mhi_cntrl);
		return ret;
	}

	if (mhi_cntrl->debugfs_dentry) {
		debugfs_create_file("m0", 0444, mhi_cntrl->debugfs_dentry, mhi_cntrl,
				    &debugfs_trigger_m0_fops);
		debugfs_create_file("m3", 0444, mhi_cntrl->debugfs_dentry, mhi_cntrl,
				    &debugfs_trigger_m3_fops);
		debugfs_create_file("disable_pci_lpm", 0644, mhi_cntrl->debugfs_dentry,
				    mhi_cntrl, &debugfs_pci_lpm_fops);
	}

	return ret;
}

static int mhi_runtime_get(struct mhi_controller *mhi_cntrl)
{
	return pm_runtime_get(mhi_cntrl->cntrl_dev);
}

static void mhi_runtime_put(struct mhi_controller *mhi_cntrl)
{
	pm_runtime_put_noidle(mhi_cntrl->cntrl_dev);
}

static void mhi_runtime_last_busy(struct mhi_controller *mhi_cntrl)
{
	pm_runtime_mark_last_busy(mhi_cntrl->cntrl_dev);
}

static void mhi_status_cb(struct mhi_controller *mhi_cntrl,
			  enum mhi_callback reason)
{
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	const struct mhi_pci_dev_info *dev_info = mhi_priv->dev_info;
	struct device *dev = mhi_cntrl->cntrl_dev;
	int ret;

	switch (reason) {
	case MHI_CB_IDLE:
		MHI_CNTRL_LOG("Schedule runtime suspend\n");
		pm_runtime_mark_last_busy(dev);
		pm_request_autosuspend(dev);
		break;
	case MHI_CB_EE_MISSION_MODE:
		MHI_CNTRL_LOG("Mission mode entry\n");
		if (debug_mode == MHI_DEBUG_NO_LPM) {
			mhi_arch_mission_mode_enter(mhi_cntrl);
			MHI_CNTRL_LOG("Exit due to: %s\n",
				      TO_MHI_DEBUG_MODE_STR(debug_mode));
			break;
		}
		/*
		 * we need to force a suspend so device can switch to
		 * mission mode pcie phy settings.
		 */
		if (!dev_info->skip_forced_suspend) {
			pm_runtime_get(dev);
			ret = mhi_force_suspend(mhi_cntrl);
			if (!ret) {
				MHI_CNTRL_LOG("Resume after forced suspend\n");
				mhi_runtime_resume(dev);
			}
			pm_runtime_put(dev);
		}
		mhi_arch_mission_mode_enter(mhi_cntrl);
		pm_runtime_allow(dev);
		break;
	default:
		MHI_CNTRL_LOG("Unhandled cb: 0x%x\n", reason);
	}
}

/* Setting to use this mhi_qcom_pm_domain ops will let PM framework override the
 * ops from dev->bus->pm which is pci_dev_pm_ops from pci-driver.c. This ops
 * has to take care everything device driver needed which is currently done
 * from pci_dev_pm_ops.
 */
static struct dev_pm_domain mhi_qcom_pm_domain = {
	.ops = {
		SET_SYSTEM_SLEEP_PM_OPS(mhi_system_suspend, mhi_system_resume)
		SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(mhi_suspend_noirq,
					      mhi_resume_noirq)
		SET_RUNTIME_PM_OPS(mhi_runtime_suspend,
				   mhi_runtime_resume,
				   mhi_runtime_idle)
		}
};

#ifdef CONFIG_MHI_BUS_DEBUG

#define MHI_QCOM_DEBUG_LEVEL MHI_MSG_LVL_VERBOSE

static struct dentry *mhi_qcom_debugfs;

static int mhi_qcom_debugfs_debug_mode_show(struct seq_file *m, void *d)
{
	seq_printf(m, "%s\n", TO_MHI_DEBUG_MODE_STR(debug_mode));

	return 0;
}

static ssize_t mhi_qcom_debugfs_debug_mode_write(struct file *file,
						 const char __user *ubuf,
						 size_t count, loff_t *ppos)
{
	struct seq_file *m = file->private_data;
	u32 input;

	if (kstrtou32_from_user(ubuf, count, 0, &input))
		return -EINVAL;

	if (input >= MHI_DEBUG_MODE_MAX)
		return -EINVAL;

	debug_mode = input;

	seq_printf(m, "Changed debug mode to: %s\n",
		   TO_MHI_DEBUG_MODE_STR(debug_mode));

	return count;
}

static int mhi_qcom_debugfs_debug_mode_open(struct inode *inode, struct file *p)
{
	return single_open(p, mhi_qcom_debugfs_debug_mode_show,
			   inode->i_private);
}

static const struct file_operations debugfs_debug_mode_fops = {
	.open = mhi_qcom_debugfs_debug_mode_open,
	.write = mhi_qcom_debugfs_debug_mode_write,
	.release = single_release,
	.read = seq_read,
};

void mhi_qcom_debugfs_init(void)
{
	mhi_qcom_debugfs = debugfs_create_dir("mhi_qcom", NULL);

	debugfs_create_file("debug_mode", 0644, mhi_qcom_debugfs, NULL,
			    &debugfs_debug_mode_fops);
}

void mhi_qcom_debugfs_exit(void)
{
	debugfs_remove_recursive(mhi_qcom_debugfs);
	mhi_qcom_debugfs = NULL;
}

#else

#define MHI_QCOM_DEBUG_LEVEL MHI_MSG_LVL_ERROR

static inline void mhi_qcom_debugfs_init(void)
{

}

static inline void mhi_qcom_debugfs_exit(void)
{

}

#endif

static int mhi_qcom_register_controller(struct mhi_controller *mhi_cntrl,
					struct mhi_qcom_priv *mhi_priv)
{
	const struct mhi_pci_dev_info *dev_info = mhi_priv->dev_info;
	const struct mhi_controller_config *mhi_cntrl_config = dev_info->config;
	struct pci_dev *pci_dev = to_pci_dev(mhi_cntrl->cntrl_dev);
	struct device_node *of_node = pci_dev->dev.of_node;
	struct mhi_device *mhi_dev;
	bool use_s1;
	u32 addr_win[2];
	const char *iommu_dma_type;
	int ret;

	mhi_cntrl->iova_start = 0;
	mhi_cntrl->iova_stop = DMA_BIT_MASK(dev_info->dma_data_width);

	of_node = of_parse_phandle(of_node, "qcom,iommu-group", 0);
	if (of_node) {
		use_s1 = true;

		/*
		 * s1 translation can be in bypass or fastmap mode
		 * if "qcom,iommu-dma" property is missing, we assume s1 is
		 * enabled and in default (no fastmap/atomic) mode
		 */
		ret = of_property_read_string(of_node, "qcom,iommu-dma",
					      &iommu_dma_type);
		if (!ret && !strcmp("bypass", iommu_dma_type))
			use_s1 = false;

		/*
		 * if s1 translation enabled pull iova addr from dt using
		 * iommu-dma-addr-pool property specified addresses
		 */
		if (use_s1) {
			ret = of_property_read_u32_array(of_node,
						"qcom,iommu-dma-addr-pool",
						addr_win, 2);
			if (ret) {
				of_node_put(of_node);
				return -EINVAL;
			}

			/*
			 * If S1 is enabled, set MHI_CTRL start address to 0
			 * so we can use low level mapping api to map buffers
			 * outside of smmu domain
			 */
			mhi_cntrl->iova_start = 0;
			mhi_cntrl->iova_stop = addr_win[0] + addr_win[1];
		}

		of_node_put(of_node);
	}

	/* setup power management apis */
	mhi_cntrl->status_cb = mhi_status_cb;
	mhi_cntrl->runtime_get = mhi_runtime_get;
	mhi_cntrl->runtime_put = mhi_runtime_put;
	mhi_cntrl->runtime_last_busy = mhi_runtime_last_busy;
	mhi_cntrl->read_reg = mhi_qcom_read_reg;
	mhi_cntrl->write_reg = mhi_qcom_write_reg;

	ret = mhi_register_controller(mhi_cntrl, mhi_cntrl_config);
	if (ret)
		return ret;

	mhi_cntrl->fw_image = dev_info->fw_image;
	mhi_cntrl->edl_image = dev_info->edl_image;

	mhi_controller_set_privdata(mhi_cntrl, mhi_priv);
	mhi_controller_set_loglevel(mhi_cntrl, MHI_QCOM_DEBUG_LEVEL);
	mhi_controller_set_base(mhi_cntrl,
				pci_resource_start(pci_dev, dev_info->bar_num));

	if (dev_info->sfr_support) {
		ret = mhi_controller_set_sfr_support(mhi_cntrl,
						     MHI_MAX_SFR_LEN);
		if (ret)
			goto error_register;
	}

	if (dev_info->timesync) {
		ret = mhi_controller_setup_timesync(mhi_cntrl,
						    &mhi_qcom_time_get,
						    &mhi_qcom_lpm_disable,
						    &mhi_qcom_lpm_enable);
		if (ret)
			goto error_register;
	}

	if (dev_info->drv_support)
		pci_dev->dev.pm_domain = &mhi_qcom_pm_domain;

	/* set name based on PCIe BDF format */
	mhi_dev = mhi_cntrl->mhi_dev;
	dev_set_name(&mhi_dev->dev, "mhi_%04x_%02u.%02u.%02u", pci_dev->device,
		     pci_domain_nr(pci_dev->bus), pci_dev->bus->number,
		     PCI_SLOT(pci_dev->devfn));
	mhi_dev->name = dev_name(&mhi_dev->dev);

	mhi_priv->cntrl_ipc_log = ipc_log_context_create(MHI_IPC_LOG_PAGES,
							 dev_info->name, 0);

	return 0;

error_register:
	mhi_unregister_controller(mhi_cntrl);

	return -EINVAL;
}

int mhi_qcom_pci_probe(struct pci_dev *pci_dev,
		       struct mhi_controller *mhi_cntrl,
		       struct mhi_qcom_priv *mhi_priv)
{
	const struct mhi_pci_dev_info *dev_info = mhi_priv->dev_info;
	int ret;

	dev_set_drvdata(&pci_dev->dev, mhi_cntrl);
	mhi_cntrl->cntrl_dev = &pci_dev->dev;

	ret = mhi_init_pci_dev(pci_dev, dev_info);
	if (ret)
		return ret;

	/* driver removal boolen set to true indicates initial probe */
	if (mhi_priv->driver_remove) {
		ret = mhi_qcom_register_controller(mhi_cntrl, mhi_priv);
		if (ret)
			goto error_init_pci;
	}

	mhi_priv->powered_on = true;

	ret = mhi_arch_pcie_init(mhi_cntrl);
	if (ret)
		goto error_init_pci;

	ret = dma_set_mask_and_coherent(mhi_cntrl->cntrl_dev,
					DMA_BIT_MASK(dev_info->dma_data_width));
	if (ret)
		goto error_init_pci;

	if (debug_mode) {
		if (mhi_cntrl->debugfs_dentry)
			debugfs_create_file("power_up", 0644,
					    mhi_cntrl->debugfs_dentry,
					    mhi_cntrl, &debugfs_power_up_fops);
		mhi_priv->powered_on = false;
		return 0;
	}

	/* start power up sequence */
	ret = mhi_qcom_power_up(mhi_cntrl);
	if (ret) {
		MHI_CNTRL_ERR("Failed to power up MHI\n");
		mhi_priv->powered_on = false;
		goto error_power_up;
	}

	pm_runtime_mark_last_busy(mhi_cntrl->cntrl_dev);

	return 0;

error_power_up:
	mhi_arch_pcie_deinit(mhi_cntrl);

error_init_pci:
	mhi_deinit_pci_dev(pci_dev, dev_info);

	dev_set_drvdata(&pci_dev->dev, NULL);
	mhi_cntrl->cntrl_dev = NULL;

	return ret;
}

int mhi_pci_probe(struct pci_dev *pci_dev, const struct pci_device_id *id)
{
	const struct mhi_pci_dev_info *dev_info =
				(struct mhi_pci_dev_info *) id->driver_data;
	struct mhi_controller *mhi_cntrl;
	struct mhi_qcom_priv *mhi_priv;
	u32 domain = pci_domain_nr(pci_dev->bus);
	u32 bus = pci_dev->bus->number;
	u32 dev_id = pci_dev->device;
	u32 slot = PCI_SLOT(pci_dev->devfn);
	int ret;

	/* see if we already registered */
	mhi_cntrl = mhi_bdf_to_controller(domain, bus, slot, dev_id);
	if (!mhi_cntrl) {
		mhi_cntrl = mhi_alloc_controller();
		if (!mhi_cntrl)
			return -ENOMEM;
	}

	mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	if (!mhi_priv) {
		mhi_priv = kzalloc(sizeof(*mhi_priv), GFP_KERNEL);
		if (!mhi_priv)
			return -ENOMEM;
	}

	/* set as true to initiate clean-up after first probe fails */
	mhi_priv->driver_remove = true;
	mhi_priv->dev_info = dev_info;

	ret = mhi_qcom_pci_probe(pci_dev, mhi_cntrl, mhi_priv);
	if (ret) {
		kfree(mhi_priv);
		mhi_free_controller(mhi_cntrl);
	}

	return ret;
}

void mhi_pci_remove(struct pci_dev *pci_dev)
{
	struct mhi_controller *mhi_cntrl;
	struct mhi_qcom_priv *mhi_priv;
	u32 domain = pci_domain_nr(pci_dev->bus);
	u32 bus = pci_dev->bus->number;
	u32 dev_id = pci_dev->device;
	u32 slot = PCI_SLOT(pci_dev->devfn);

	/* see if we already registered */
	mhi_cntrl = mhi_bdf_to_controller(domain, bus, slot, dev_id);
	if (!mhi_cntrl)
		return;

	mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	if (!mhi_priv)
		return;

	/* if link is in suspend, wake it up */
	pm_runtime_get_sync(mhi_cntrl->cntrl_dev);

	if (mhi_priv->powered_on) {
		MHI_CNTRL_LOG("Triggering shutdown process\n");
		mhi_power_down(mhi_cntrl, false);
		mhi_unprepare_after_power_down(mhi_cntrl);
	}
	mhi_priv->powered_on = false;

	pm_runtime_put_noidle(mhi_cntrl->cntrl_dev);

	/* allow arch driver to free memory and unregister esoc if set */
	mhi_priv->driver_remove = true;
	mhi_arch_pcie_deinit(mhi_cntrl);

	/* turn the link off */
	mhi_deinit_pci_dev(pci_dev, mhi_priv->dev_info);

	mhi_unregister_controller(mhi_cntrl);
	kfree(mhi_priv);
	mhi_free_controller(mhi_cntrl);
}

static const struct dev_pm_ops pm_ops = {
	SET_RUNTIME_PM_OPS(mhi_runtime_suspend,
			   mhi_runtime_resume,
			   mhi_runtime_idle)
	SET_SYSTEM_SLEEP_PM_OPS(mhi_system_suspend, mhi_system_resume)
};

static struct pci_driver mhi_pcie_driver = {
	.name = "mhi",
	.id_table = mhi_pcie_device_id,
	.probe = mhi_pci_probe,
	.remove = mhi_pci_remove,
	.driver = {
		.pm = &pm_ops
	}
};

static int __init mhi_qcom_init(void)
{
	int ret = 0;

	mhi_qcom_debugfs_init();
	ret = pci_register_driver(&mhi_pcie_driver);
	if (ret) {
		mhi_qcom_debugfs_exit();
		return ret;
	}

	return 0;
}

static void __exit mhi_qcom_exit(void)
{
	pci_unregister_driver(&mhi_pcie_driver);
	mhi_qcom_debugfs_exit();
}

module_init(mhi_qcom_init);
module_exit(mhi_qcom_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("MHI_CORE");
MODULE_DESCRIPTION("MHI Host Driver");
