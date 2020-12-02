// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MHI PCI driver - MHI over PCI controller driver
 *
 * This module is a generic driver for registering MHI-over-PCI devices,
 * such as PCIe QCOM modems.
 *
 * Copyright (C) 2020 Linaro Ltd <loic.poulain@linaro.org>
 */

#include <linux/device.h>
#include <linux/mhi.h>
#include <linux/module.h>
#include <linux/pci.h>

#define MHI_PCI_DEFAULT_BAR_NUM 0

/**
 * struct mhi_pci_dev_info - MHI PCI device specific information
 * @config: MHI controller configuration
 * @name: name of the PCI module
 * @fw: firmware path (if any)
 * @edl: emergency download mode firmware path (if any)
 * @bar_num: PCI base address register to use for MHI MMIO register space
 * @dma_data_width: DMA transfer word size (32 or 64 bits)
 */
struct mhi_pci_dev_info {
	const struct mhi_controller_config *config;
	const char *name;
	const char *fw;
	const char *edl;
	unsigned int bar_num;
	unsigned int dma_data_width;
};

#define MHI_CHANNEL_CONFIG_UL(ch_num, ch_name, el_count, ev_ring) \
	{						\
		.num = ch_num,				\
		.name = ch_name,			\
		.num_elements = el_count,		\
		.event_ring = ev_ring,			\
		.dir = DMA_TO_DEVICE,			\
		.ee_mask = BIT(MHI_EE_AMSS),		\
		.pollcfg = 0,				\
		.doorbell = MHI_DB_BRST_DISABLE,	\
		.lpm_notify = false,			\
		.offload_channel = false,		\
		.doorbell_mode_switch = false,		\
	}						\

#define MHI_CHANNEL_CONFIG_DL(ch_num, ch_name, el_count, ev_ring) \
	{						\
		.num = ch_num,				\
		.name = ch_name,			\
		.num_elements = el_count,		\
		.event_ring = ev_ring,			\
		.dir = DMA_FROM_DEVICE,			\
		.ee_mask = BIT(MHI_EE_AMSS),		\
		.pollcfg = 0,				\
		.doorbell = MHI_DB_BRST_DISABLE,	\
		.lpm_notify = false,			\
		.offload_channel = false,		\
		.doorbell_mode_switch = false,		\
	}

#define MHI_EVENT_CONFIG_CTRL(ev_ring)		\
	{					\
		.num_elements = 64,		\
		.irq_moderation_ms = 0,		\
		.irq = (ev_ring) + 1,		\
		.priority = 1,			\
		.mode = MHI_DB_BRST_DISABLE,	\
		.data_type = MHI_ER_CTRL,	\
		.hardware_event = false,	\
		.client_managed = false,	\
		.offload_channel = false,	\
	}

#define MHI_EVENT_CONFIG_DATA(ev_ring)		\
	{					\
		.num_elements = 128,		\
		.irq_moderation_ms = 5,		\
		.irq = (ev_ring) + 1,		\
		.priority = 1,			\
		.mode = MHI_DB_BRST_DISABLE,	\
		.data_type = MHI_ER_DATA,	\
		.hardware_event = false,	\
		.client_managed = false,	\
		.offload_channel = false,	\
	}

#define MHI_EVENT_CONFIG_HW_DATA(ev_ring, ch_num) \
	{					\
		.num_elements = 128,		\
		.irq_moderation_ms = 5,		\
		.irq = (ev_ring) + 1,		\
		.priority = 1,			\
		.mode = MHI_DB_BRST_DISABLE,	\
		.data_type = MHI_ER_DATA,	\
		.hardware_event = true,		\
		.client_managed = false,	\
		.offload_channel = false,	\
		.channel = ch_num,		\
	}

static const struct mhi_channel_config modem_qcom_v1_mhi_channels[] = {
	MHI_CHANNEL_CONFIG_UL(12, "MBIM", 4, 0),
	MHI_CHANNEL_CONFIG_DL(13, "MBIM", 4, 0),
	MHI_CHANNEL_CONFIG_UL(14, "QMI", 4, 0),
	MHI_CHANNEL_CONFIG_DL(15, "QMI", 4, 0),
	MHI_CHANNEL_CONFIG_UL(20, "IPCR", 8, 0),
	MHI_CHANNEL_CONFIG_DL(21, "IPCR", 8, 0),
	MHI_CHANNEL_CONFIG_UL(100, "IP_HW0", 128, 1),
	MHI_CHANNEL_CONFIG_DL(101, "IP_HW0", 128, 2),
};

static const struct mhi_event_config modem_qcom_v1_mhi_events[] = {
	/* first ring is control+data ring */
	MHI_EVENT_CONFIG_CTRL(0),
	/* Hardware channels request dedicated hardware event rings */
	MHI_EVENT_CONFIG_HW_DATA(1, 100),
	MHI_EVENT_CONFIG_HW_DATA(2, 101)
};

static const struct mhi_controller_config modem_qcom_v1_mhiv_config = {
	.max_channels = 128,
	.timeout_ms = 5000,
	.num_channels = ARRAY_SIZE(modem_qcom_v1_mhi_channels),
	.ch_cfg = modem_qcom_v1_mhi_channels,
	.num_events = ARRAY_SIZE(modem_qcom_v1_mhi_events),
	.event_cfg = modem_qcom_v1_mhi_events,
};

static const struct mhi_pci_dev_info mhi_qcom_sdx55_info = {
	.name = "qcom-sdx55m",
	.fw = "qcom/sdx55m/sbl1.mbn",
	.edl = "qcom/sdx55m/edl.mbn",
	.config = &modem_qcom_v1_mhiv_config,
	.bar_num = MHI_PCI_DEFAULT_BAR_NUM,
	.dma_data_width = 32
};

static const struct pci_device_id mhi_pci_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_QCOM, 0x0306),
		.driver_data = (kernel_ulong_t) &mhi_qcom_sdx55_info },
	{  }
};
MODULE_DEVICE_TABLE(pci, mhi_pci_id_table);

static int mhi_pci_read_reg(struct mhi_controller *mhi_cntrl,
			    void __iomem *addr, u32 *out)
{
	*out = readl(addr);
	return 0;
}

static void mhi_pci_write_reg(struct mhi_controller *mhi_cntrl,
			      void __iomem *addr, u32 val)
{
	writel(val, addr);
}

static void mhi_pci_status_cb(struct mhi_controller *mhi_cntrl,
			      enum mhi_callback cb)
{
	/* Nothing to do for now */
}

static int mhi_pci_claim(struct mhi_controller *mhi_cntrl,
			 unsigned int bar_num, u64 dma_mask)
{
	struct pci_dev *pdev = to_pci_dev(mhi_cntrl->cntrl_dev);
	int err;

	err = pci_assign_resource(pdev, bar_num);
	if (err)
		return err;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "failed to enable pci device: %d\n", err);
		return err;
	}

	err = pcim_iomap_regions(pdev, 1 << bar_num, pci_name(pdev));
	if (err) {
		dev_err(&pdev->dev, "failed to map pci region: %d\n", err);
		return err;
	}
	mhi_cntrl->regs = pcim_iomap_table(pdev)[bar_num];

	err = pci_set_dma_mask(pdev, dma_mask);
	if (err) {
		dev_err(&pdev->dev, "Cannot set proper DMA mask\n");
		return err;
	}

	err = pci_set_consistent_dma_mask(pdev, dma_mask);
	if (err) {
		dev_err(&pdev->dev, "set consistent dma mask failed\n");
		return err;
	}

	pci_set_master(pdev);

	return 0;
}

static int mhi_pci_get_irqs(struct mhi_controller *mhi_cntrl,
			    const struct mhi_controller_config *mhi_cntrl_config)
{
	struct pci_dev *pdev = to_pci_dev(mhi_cntrl->cntrl_dev);
	int nr_vectors, i;
	int *irq;

	/*
	 * Alloc one MSI vector for BHI + one vector per event ring, ideally...
	 * No explicit pci_free_irq_vectors required, done by pcim_release.
	 */
	mhi_cntrl->nr_irqs = 1 + mhi_cntrl_config->num_events;

	nr_vectors = pci_alloc_irq_vectors(pdev, 1, mhi_cntrl->nr_irqs, PCI_IRQ_MSI);
	if (nr_vectors < 0) {
		dev_err(&pdev->dev, "Error allocating MSI vectors %d\n",
			nr_vectors);
		return nr_vectors;
	}

	if (nr_vectors < mhi_cntrl->nr_irqs) {
		dev_warn(&pdev->dev, "Not enough MSI vectors (%d/%d), use shared MSI\n",
			 nr_vectors, mhi_cntrl_config->num_events);
	}

	irq = devm_kcalloc(&pdev->dev, mhi_cntrl->nr_irqs, sizeof(int), GFP_KERNEL);
	if (!irq)
		return -ENOMEM;

	for (i = 0; i < mhi_cntrl->nr_irqs; i++) {
		int vector = i >= nr_vectors ? (nr_vectors - 1) : i;

		irq[i] = pci_irq_vector(pdev, vector);
	}

	mhi_cntrl->irq = irq;

	return 0;
}

static int mhi_pci_runtime_get(struct mhi_controller *mhi_cntrl)
{
	/* no PM for now */
	return 0;
}

static void mhi_pci_runtime_put(struct mhi_controller *mhi_cntrl)
{
	/* no PM for now */
}

static int mhi_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	const struct mhi_pci_dev_info *info = (struct mhi_pci_dev_info *) id->driver_data;
	const struct mhi_controller_config *mhi_cntrl_config;
	struct mhi_controller *mhi_cntrl;
	int err;

	dev_dbg(&pdev->dev, "MHI PCI device found: %s\n", info->name);

	mhi_cntrl = mhi_alloc_controller();
	if (!mhi_cntrl)
		return -ENOMEM;

	mhi_cntrl_config = info->config;
	mhi_cntrl->cntrl_dev = &pdev->dev;
	mhi_cntrl->iova_start = 0;
	mhi_cntrl->iova_stop = (dma_addr_t)DMA_BIT_MASK(info->dma_data_width);
	mhi_cntrl->fw_image = info->fw;
	mhi_cntrl->edl_image = info->edl;

	mhi_cntrl->read_reg = mhi_pci_read_reg;
	mhi_cntrl->write_reg = mhi_pci_write_reg;
	mhi_cntrl->status_cb = mhi_pci_status_cb;
	mhi_cntrl->runtime_get = mhi_pci_runtime_get;
	mhi_cntrl->runtime_put = mhi_pci_runtime_put;

	err = mhi_pci_claim(mhi_cntrl, info->bar_num, DMA_BIT_MASK(info->dma_data_width));
	if (err)
		goto err_release;

	err = mhi_pci_get_irqs(mhi_cntrl, mhi_cntrl_config);
	if (err)
		goto err_release;

	pci_set_drvdata(pdev, mhi_cntrl);

	err = mhi_register_controller(mhi_cntrl, mhi_cntrl_config);
	if (err)
		goto err_release;

	/* MHI bus does not power up the controller by default */
	err = mhi_prepare_for_power_up(mhi_cntrl);
	if (err) {
		dev_err(&pdev->dev, "failed to prepare MHI controller\n");
		goto err_unregister;
	}

	err = mhi_sync_power_up(mhi_cntrl);
	if (err) {
		dev_err(&pdev->dev, "failed to power up MHI controller\n");
		goto err_unprepare;
	}

	return 0;

err_unprepare:
	mhi_unprepare_after_power_down(mhi_cntrl);
err_unregister:
	mhi_unregister_controller(mhi_cntrl);
err_release:
	mhi_free_controller(mhi_cntrl);

	return err;
}

static void mhi_pci_remove(struct pci_dev *pdev)
{
	struct mhi_controller *mhi_cntrl = pci_get_drvdata(pdev);

	mhi_power_down(mhi_cntrl, true);
	mhi_unprepare_after_power_down(mhi_cntrl);
	mhi_unregister_controller(mhi_cntrl);
	mhi_free_controller(mhi_cntrl);
}

static struct pci_driver mhi_pci_driver = {
	.name		= "mhi-pci-generic",
	.id_table	= mhi_pci_id_table,
	.probe		= mhi_pci_probe,
	.remove		= mhi_pci_remove
};
module_pci_driver(mhi_pci_driver);

MODULE_AUTHOR("Loic Poulain <loic.poulain@linaro.org>");
MODULE_DESCRIPTION("Modem Host Interface (MHI) PCI controller driver");
MODULE_LICENSE("GPL");
