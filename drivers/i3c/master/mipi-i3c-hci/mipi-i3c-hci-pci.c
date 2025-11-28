// SPDX-License-Identifier: GPL-2.0
/*
 * PCI glue code for MIPI I3C HCI driver
 *
 * Copyright (C) 2024 Intel Corporation
 *
 * Author: Jarkko Nikula <jarkko.nikula@linux.intel.com>
 */
#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/idr.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>

struct mipi_i3c_hci_pci {
	struct pci_dev *pci;
	struct platform_device *pdev;
	const struct mipi_i3c_hci_pci_info *info;
	void *private;
};

struct mipi_i3c_hci_pci_info {
	int (*init)(struct mipi_i3c_hci_pci *hci);
	void (*exit)(struct mipi_i3c_hci_pci *hci);
};

static DEFINE_IDA(mipi_i3c_hci_pci_ida);

#define INTEL_PRIV_OFFSET		0x2b0
#define INTEL_PRIV_SIZE			0x28
#define INTEL_RESETS			0x04
#define INTEL_RESETS_RESET		BIT(0)
#define INTEL_RESETS_RESET_DONE		BIT(1)
#define INTEL_RESETS_TIMEOUT_US		(10 * USEC_PER_MSEC)

#define INTEL_ACTIVELTR			0x0c
#define INTEL_IDLELTR			0x10

#define INTEL_LTR_REQ			BIT(15)
#define INTEL_LTR_SCALE_MASK		GENMASK(11, 10)
#define INTEL_LTR_SCALE_1US		FIELD_PREP(INTEL_LTR_SCALE_MASK, 2)
#define INTEL_LTR_SCALE_32US		FIELD_PREP(INTEL_LTR_SCALE_MASK, 3)
#define INTEL_LTR_VALUE_MASK		GENMASK(9, 0)

struct intel_host {
	void __iomem	*priv;
	u32		active_ltr;
	u32		idle_ltr;
	struct dentry	*debugfs_root;
};

static void intel_cache_ltr(struct intel_host *host)
{
	host->active_ltr = readl(host->priv + INTEL_ACTIVELTR);
	host->idle_ltr = readl(host->priv + INTEL_IDLELTR);
}

static void intel_ltr_set(struct device *dev, s32 val)
{
	struct mipi_i3c_hci_pci *hci = dev_get_drvdata(dev);
	struct intel_host *host = hci->private;
	u32 ltr;

	/*
	 * Program latency tolerance (LTR) accordingly what has been asked
	 * by the PM QoS layer or disable it in case we were passed
	 * negative value or PM_QOS_LATENCY_ANY.
	 */
	ltr = readl(host->priv + INTEL_ACTIVELTR);

	if (val == PM_QOS_LATENCY_ANY || val < 0) {
		ltr &= ~INTEL_LTR_REQ;
	} else {
		ltr |= INTEL_LTR_REQ;
		ltr &= ~INTEL_LTR_SCALE_MASK;
		ltr &= ~INTEL_LTR_VALUE_MASK;

		if (val > INTEL_LTR_VALUE_MASK) {
			val >>= 5;
			if (val > INTEL_LTR_VALUE_MASK)
				val = INTEL_LTR_VALUE_MASK;
			ltr |= INTEL_LTR_SCALE_32US | val;
		} else {
			ltr |= INTEL_LTR_SCALE_1US | val;
		}
	}

	if (ltr == host->active_ltr)
		return;

	writel(ltr, host->priv + INTEL_ACTIVELTR);
	writel(ltr, host->priv + INTEL_IDLELTR);

	/* Cache the values into intel_host structure */
	intel_cache_ltr(host);
}

static void intel_ltr_expose(struct device *dev)
{
	dev->power.set_latency_tolerance = intel_ltr_set;
	dev_pm_qos_expose_latency_tolerance(dev);
}

static void intel_ltr_hide(struct device *dev)
{
	dev_pm_qos_hide_latency_tolerance(dev);
	dev->power.set_latency_tolerance = NULL;
}

static void intel_add_debugfs(struct mipi_i3c_hci_pci *hci)
{
	struct dentry *dir = debugfs_create_dir(dev_name(&hci->pci->dev), NULL);
	struct intel_host *host = hci->private;

	intel_cache_ltr(host);

	host->debugfs_root = dir;
	debugfs_create_x32("active_ltr", 0444, dir, &host->active_ltr);
	debugfs_create_x32("idle_ltr", 0444, dir, &host->idle_ltr);
}

static void intel_remove_debugfs(struct mipi_i3c_hci_pci *hci)
{
	struct intel_host *host = hci->private;

	debugfs_remove_recursive(host->debugfs_root);
}

static void intel_reset(void __iomem *priv)
{
	u32 reg;

	/* Assert reset, wait for completion and release reset */
	writel(0, priv + INTEL_RESETS);
	readl_poll_timeout(priv + INTEL_RESETS, reg,
			   reg & INTEL_RESETS_RESET_DONE, 0,
			   INTEL_RESETS_TIMEOUT_US);
	writel(INTEL_RESETS_RESET, priv + INTEL_RESETS);
}

static void __iomem *intel_priv(struct pci_dev *pci)
{
	resource_size_t base = pci_resource_start(pci, 0);

	return devm_ioremap(&pci->dev, base + INTEL_PRIV_OFFSET, INTEL_PRIV_SIZE);
}

static int intel_i3c_init(struct mipi_i3c_hci_pci *hci)
{
	struct intel_host *host = devm_kzalloc(&hci->pci->dev, sizeof(*host), GFP_KERNEL);
	void __iomem *priv = intel_priv(hci->pci);

	if (!host || !priv)
		return -ENOMEM;

	dma_set_mask_and_coherent(&hci->pci->dev, DMA_BIT_MASK(64));

	hci->pci->d3cold_delay = 0;

	hci->private = host;
	host->priv = priv;

	intel_reset(priv);

	intel_ltr_expose(&hci->pci->dev);
	intel_add_debugfs(hci);

	return 0;
}

static void intel_i3c_exit(struct mipi_i3c_hci_pci *hci)
{
	intel_remove_debugfs(hci);
	intel_ltr_hide(&hci->pci->dev);
}

static const struct mipi_i3c_hci_pci_info intel_info = {
	.init = intel_i3c_init,
	.exit = intel_i3c_exit,
};

static int mipi_i3c_hci_pci_probe(struct pci_dev *pci,
				  const struct pci_device_id *id)
{
	struct mipi_i3c_hci_pci *hci;
	struct resource res[2];
	int dev_id, ret;

	hci = devm_kzalloc(&pci->dev, sizeof(*hci), GFP_KERNEL);
	if (!hci)
		return -ENOMEM;

	hci->pci = pci;

	ret = pcim_enable_device(pci);
	if (ret)
		return ret;

	pci_set_master(pci);

	memset(&res, 0, sizeof(res));

	res[0].flags = IORESOURCE_MEM;
	res[0].start = pci_resource_start(pci, 0);
	res[0].end = pci_resource_end(pci, 0);

	res[1].flags = IORESOURCE_IRQ;
	res[1].start = pci->irq;
	res[1].end = pci->irq;

	dev_id = ida_alloc(&mipi_i3c_hci_pci_ida, GFP_KERNEL);
	if (dev_id < 0)
		return dev_id;

	hci->pdev = platform_device_alloc("mipi-i3c-hci", dev_id);
	if (!hci->pdev)
		return -ENOMEM;

	hci->pdev->dev.parent = &pci->dev;
	device_set_node(&hci->pdev->dev, dev_fwnode(&pci->dev));

	ret = platform_device_add_resources(hci->pdev, res, ARRAY_SIZE(res));
	if (ret)
		goto err;

	hci->info = (const struct mipi_i3c_hci_pci_info *)id->driver_data;
	if (hci->info && hci->info->init) {
		ret = hci->info->init(hci);
		if (ret)
			goto err;
	}

	ret = platform_device_add(hci->pdev);
	if (ret)
		goto err_exit;

	pci_set_drvdata(pci, hci);

	return 0;

err_exit:
	if (hci->info && hci->info->exit)
		hci->info->exit(hci);
err:
	platform_device_put(hci->pdev);
	ida_free(&mipi_i3c_hci_pci_ida, dev_id);
	return ret;
}

static void mipi_i3c_hci_pci_remove(struct pci_dev *pci)
{
	struct mipi_i3c_hci_pci *hci = pci_get_drvdata(pci);
	struct platform_device *pdev = hci->pdev;
	int dev_id = pdev->id;

	if (hci->info && hci->info->exit)
		hci->info->exit(hci);

	platform_device_unregister(pdev);
	ida_free(&mipi_i3c_hci_pci_ida, dev_id);
}

static const struct pci_device_id mipi_i3c_hci_pci_devices[] = {
	/* Wildcat Lake-U */
	{ PCI_VDEVICE(INTEL, 0x4d7c), (kernel_ulong_t)&intel_info},
	{ PCI_VDEVICE(INTEL, 0x4d6f), (kernel_ulong_t)&intel_info},
	/* Panther Lake-H */
	{ PCI_VDEVICE(INTEL, 0xe37c), (kernel_ulong_t)&intel_info},
	{ PCI_VDEVICE(INTEL, 0xe36f), (kernel_ulong_t)&intel_info},
	/* Panther Lake-P */
	{ PCI_VDEVICE(INTEL, 0xe47c), (kernel_ulong_t)&intel_info},
	{ PCI_VDEVICE(INTEL, 0xe46f), (kernel_ulong_t)&intel_info},
	/* Nova Lake-S */
	{ PCI_VDEVICE(INTEL, 0x6e2c), (kernel_ulong_t)&intel_info},
	{ PCI_VDEVICE(INTEL, 0x6e2d), (kernel_ulong_t)&intel_info},
	{ },
};
MODULE_DEVICE_TABLE(pci, mipi_i3c_hci_pci_devices);

static struct pci_driver mipi_i3c_hci_pci_driver = {
	.name = "mipi_i3c_hci_pci",
	.id_table = mipi_i3c_hci_pci_devices,
	.probe = mipi_i3c_hci_pci_probe,
	.remove = mipi_i3c_hci_pci_remove,
};

module_pci_driver(mipi_i3c_hci_pci_driver);

MODULE_AUTHOR("Jarkko Nikula <jarkko.nikula@intel.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MIPI I3C HCI driver on PCI bus");
