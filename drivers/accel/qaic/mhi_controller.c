// SPDX-License-Identifier: GPL-2.0-only

/* Copyright (c) 2019-2021, The Linux Foundation. All rights reserved. */
/* Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/memblock.h>
#include <linux/mhi.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/sizes.h>

#include "mhi_controller.h"
#include "qaic.h"

#define MAX_RESET_TIME_SEC 25

static unsigned int mhi_timeout_ms = 2000; /* 2 sec default */
module_param(mhi_timeout_ms, uint, 0600);
MODULE_PARM_DESC(mhi_timeout_ms, "MHI controller timeout value");

static struct mhi_channel_config aic100_channels[] = {
	{
		.name = "QAIC_LOOPBACK",
		.num = 0,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_TO_DEVICE,
		.ee_mask = MHI_CH_EE_AMSS,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_LOOPBACK",
		.num = 1,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = MHI_CH_EE_AMSS,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_SAHARA",
		.num = 2,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_TO_DEVICE,
		.ee_mask = MHI_CH_EE_SBL,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_SAHARA",
		.num = 3,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = MHI_CH_EE_SBL,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_DIAG",
		.num = 4,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_TO_DEVICE,
		.ee_mask = MHI_CH_EE_AMSS,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_DIAG",
		.num = 5,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = MHI_CH_EE_AMSS,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_SSR",
		.num = 6,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_TO_DEVICE,
		.ee_mask = MHI_CH_EE_AMSS,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_SSR",
		.num = 7,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = MHI_CH_EE_AMSS,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_QDSS",
		.num = 8,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_TO_DEVICE,
		.ee_mask = MHI_CH_EE_AMSS,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_QDSS",
		.num = 9,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = MHI_CH_EE_AMSS,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_CONTROL",
		.num = 10,
		.num_elements = 128,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_TO_DEVICE,
		.ee_mask = MHI_CH_EE_AMSS,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_CONTROL",
		.num = 11,
		.num_elements = 128,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = MHI_CH_EE_AMSS,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_LOGGING",
		.num = 12,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_TO_DEVICE,
		.ee_mask = MHI_CH_EE_SBL,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_LOGGING",
		.num = 13,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = MHI_CH_EE_SBL,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_STATUS",
		.num = 14,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_TO_DEVICE,
		.ee_mask = MHI_CH_EE_AMSS,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_STATUS",
		.num = 15,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = MHI_CH_EE_AMSS,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_TELEMETRY",
		.num = 16,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_TO_DEVICE,
		.ee_mask = MHI_CH_EE_AMSS,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_TELEMETRY",
		.num = 17,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = MHI_CH_EE_AMSS,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_DEBUG",
		.num = 18,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_TO_DEVICE,
		.ee_mask = MHI_CH_EE_AMSS,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_DEBUG",
		.num = 19,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = MHI_CH_EE_AMSS,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_TIMESYNC",
		.num = 20,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_TO_DEVICE,
		.ee_mask = MHI_CH_EE_SBL,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.num = 21,
		.name = "QAIC_TIMESYNC",
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = MHI_CH_EE_SBL,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.name = "QAIC_TIMESYNC_PERIODIC",
		.num = 22,
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_TO_DEVICE,
		.ee_mask = MHI_CH_EE_AMSS,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
	{
		.num = 23,
		.name = "QAIC_TIMESYNC_PERIODIC",
		.num_elements = 32,
		.local_elements = 0,
		.event_ring = 0,
		.dir = DMA_FROM_DEVICE,
		.ee_mask = MHI_CH_EE_AMSS,
		.pollcfg = 0,
		.doorbell = MHI_DB_BRST_DISABLE,
		.lpm_notify = false,
		.offload_channel = false,
		.doorbell_mode_switch = false,
		.auto_queue = false,
		.wake_capable = false,
	},
};

static struct mhi_event_config aic100_events[] = {
	{
		.num_elements = 32,
		.irq_moderation_ms = 0,
		.irq = 0,
		.channel = U32_MAX,
		.priority = 1,
		.mode = MHI_DB_BRST_DISABLE,
		.data_type = MHI_ER_CTRL,
		.hardware_event = false,
		.client_managed = false,
		.offload_channel = false,
	},
};

static struct mhi_controller_config aic100_config = {
	.max_channels = 128,
	.timeout_ms = 0, /* controlled by mhi_timeout */
	.buf_len = 0,
	.num_channels = ARRAY_SIZE(aic100_channels),
	.ch_cfg = aic100_channels,
	.num_events = ARRAY_SIZE(aic100_events),
	.event_cfg = aic100_events,
	.use_bounce_buf = false,
	.m2_no_db = false,
};

static int mhi_read_reg(struct mhi_controller *mhi_cntrl, void __iomem *addr, u32 *out)
{
	u32 tmp;

	/*
	 * SOC_HW_VERSION quirk
	 * The SOC_HW_VERSION register (offset 0x224) is not reliable and
	 * may contain uninitialized values, including 0xFFFFFFFF. This could
	 * cause a false positive link down error.  Instead, intercept any
	 * reads and provide the correct value of the register.
	 */
	if (addr - mhi_cntrl->regs == 0x224) {
		*out = 0x60110200;
		return 0;
	}

	tmp = readl_relaxed(addr);
	if (tmp == U32_MAX)
		return -EIO;

	*out = tmp;

	return 0;
}

static void mhi_write_reg(struct mhi_controller *mhi_cntrl, void __iomem *addr, u32 val)
{
	writel_relaxed(val, addr);
}

static int mhi_runtime_get(struct mhi_controller *mhi_cntrl)
{
	return 0;
}

static void mhi_runtime_put(struct mhi_controller *mhi_cntrl)
{
}

static void mhi_status_cb(struct mhi_controller *mhi_cntrl, enum mhi_callback reason)
{
	struct qaic_device *qdev = pci_get_drvdata(to_pci_dev(mhi_cntrl->cntrl_dev));

	/* this event occurs in atomic context */
	if (reason == MHI_CB_FATAL_ERROR)
		pci_err(qdev->pdev, "Fatal error received from device. Attempting to recover\n");
	/* this event occurs in non-atomic context */
	if (reason == MHI_CB_SYS_ERROR)
		qaic_dev_reset_clean_local_state(qdev);
}

static int mhi_reset_and_async_power_up(struct mhi_controller *mhi_cntrl)
{
	u8 time_sec = 1;
	int current_ee;
	int ret;

	/* Reset the device to bring the device in PBL EE */
	mhi_soc_reset(mhi_cntrl);

	/*
	 * Keep checking the execution environment(EE) after every 1 second
	 * interval.
	 */
	do {
		msleep(1000);
		current_ee = mhi_get_exec_env(mhi_cntrl);
	} while (current_ee != MHI_EE_PBL && time_sec++ <= MAX_RESET_TIME_SEC);

	/* If the device is in PBL EE retry power up */
	if (current_ee == MHI_EE_PBL)
		ret = mhi_async_power_up(mhi_cntrl);
	else
		ret = -EIO;

	return ret;
}

struct mhi_controller *qaic_mhi_register_controller(struct pci_dev *pci_dev, void __iomem *mhi_bar,
						    int mhi_irq, bool shared_msi)
{
	struct mhi_controller *mhi_cntrl;
	int ret;

	mhi_cntrl = devm_kzalloc(&pci_dev->dev, sizeof(*mhi_cntrl), GFP_KERNEL);
	if (!mhi_cntrl)
		return ERR_PTR(-ENOMEM);

	mhi_cntrl->cntrl_dev = &pci_dev->dev;

	/*
	 * Covers the entire possible physical ram region. Remote side is
	 * going to calculate a size of this range, so subtract 1 to prevent
	 * rollover.
	 */
	mhi_cntrl->iova_start = 0;
	mhi_cntrl->iova_stop = PHYS_ADDR_MAX - 1;
	mhi_cntrl->status_cb = mhi_status_cb;
	mhi_cntrl->runtime_get = mhi_runtime_get;
	mhi_cntrl->runtime_put = mhi_runtime_put;
	mhi_cntrl->read_reg = mhi_read_reg;
	mhi_cntrl->write_reg = mhi_write_reg;
	mhi_cntrl->regs = mhi_bar;
	mhi_cntrl->reg_len = SZ_4K;
	mhi_cntrl->nr_irqs = 1;
	mhi_cntrl->irq = devm_kmalloc(&pci_dev->dev, sizeof(*mhi_cntrl->irq), GFP_KERNEL);

	if (!mhi_cntrl->irq)
		return ERR_PTR(-ENOMEM);

	mhi_cntrl->irq[0] = mhi_irq;

	if (shared_msi) /* MSI shared with data path, no IRQF_NO_SUSPEND */
		mhi_cntrl->irq_flags = IRQF_SHARED;

	mhi_cntrl->fw_image = "qcom/aic100/sbl.bin";

	/* use latest configured timeout */
	aic100_config.timeout_ms = mhi_timeout_ms;
	ret = mhi_register_controller(mhi_cntrl, &aic100_config);
	if (ret) {
		pci_err(pci_dev, "mhi_register_controller failed %d\n", ret);
		return ERR_PTR(ret);
	}

	ret = mhi_prepare_for_power_up(mhi_cntrl);
	if (ret) {
		pci_err(pci_dev, "mhi_prepare_for_power_up failed %d\n", ret);
		goto prepare_power_up_fail;
	}

	ret = mhi_async_power_up(mhi_cntrl);
	/*
	 * If EIO is returned it is possible that device is in SBL EE, which is
	 * undesired. SOC reset the device and try to power up again.
	 */
	if (ret == -EIO && MHI_EE_SBL == mhi_get_exec_env(mhi_cntrl)) {
		pci_err(pci_dev, "Found device in SBL at MHI init. Attempting a reset.\n");
		ret = mhi_reset_and_async_power_up(mhi_cntrl);
	}

	if (ret) {
		pci_err(pci_dev, "mhi_async_power_up failed %d\n", ret);
		goto power_up_fail;
	}

	return mhi_cntrl;

power_up_fail:
	mhi_unprepare_after_power_down(mhi_cntrl);
prepare_power_up_fail:
	mhi_unregister_controller(mhi_cntrl);
	return ERR_PTR(ret);
}

void qaic_mhi_free_controller(struct mhi_controller *mhi_cntrl, bool link_up)
{
	mhi_power_down(mhi_cntrl, link_up);
	mhi_unprepare_after_power_down(mhi_cntrl);
	mhi_unregister_controller(mhi_cntrl);
}

void qaic_mhi_start_reset(struct mhi_controller *mhi_cntrl)
{
	mhi_power_down(mhi_cntrl, true);
}

void qaic_mhi_reset_done(struct mhi_controller *mhi_cntrl)
{
	struct pci_dev *pci_dev = container_of(mhi_cntrl->cntrl_dev, struct pci_dev, dev);
	int ret;

	ret = mhi_async_power_up(mhi_cntrl);
	if (ret)
		pci_err(pci_dev, "mhi_async_power_up failed after reset %d\n", ret);
}
