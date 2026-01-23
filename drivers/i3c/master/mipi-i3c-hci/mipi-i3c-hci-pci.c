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
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_data/mipi-i3c-hci.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/pm_runtime.h>

/*
 * There can up to 15 instances, but implementations have at most 2 at this
 * time.
 */
#define INST_MAX 2

struct mipi_i3c_hci_pci {
	struct pci_dev *pci;
	void __iomem *base;
	const struct mipi_i3c_hci_pci_info *info;
	void *private;
};

struct mipi_i3c_hci_pci_info {
	int (*init)(struct mipi_i3c_hci_pci *hci);
	void (*exit)(struct mipi_i3c_hci_pci *hci);
	const char *name;
	int id[INST_MAX];
	u32 instance_offset[INST_MAX];
	int instance_count;
};

#define INTEL_PRIV_OFFSET		0x2b0
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

static int intel_i3c_init(struct mipi_i3c_hci_pci *hci)
{
	struct intel_host *host = devm_kzalloc(&hci->pci->dev, sizeof(*host), GFP_KERNEL);
	void __iomem *priv = hci->base + INTEL_PRIV_OFFSET;

	if (!host)
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

static const struct mipi_i3c_hci_pci_info intel_mi_1_info = {
	.init = intel_i3c_init,
	.exit = intel_i3c_exit,
	.name = "intel-lpss-i3c",
	.id = {0, 1},
	.instance_offset = {0, 0x400},
	.instance_count = 2,
};

static const struct mipi_i3c_hci_pci_info intel_mi_2_info = {
	.init = intel_i3c_init,
	.exit = intel_i3c_exit,
	.name = "intel-lpss-i3c",
	.id = {2, 3},
	.instance_offset = {0, 0x400},
	.instance_count = 2,
};

static const struct mipi_i3c_hci_pci_info intel_si_2_info = {
	.init = intel_i3c_init,
	.exit = intel_i3c_exit,
	.name = "intel-lpss-i3c",
	.id = {2},
	.instance_offset = {0},
	.instance_count = 1,
};

static void mipi_i3c_hci_pci_rpm_allow(struct device *dev)
{
	pm_runtime_put(dev);
	pm_runtime_allow(dev);
}

static void mipi_i3c_hci_pci_rpm_forbid(struct device *dev)
{
	pm_runtime_forbid(dev);
	pm_runtime_get_sync(dev);
}

struct mipi_i3c_hci_pci_cell_data {
	struct mipi_i3c_hci_platform_data pdata;
	struct resource res;
};

static void mipi_i3c_hci_pci_setup_cell(struct mipi_i3c_hci_pci *hci, int idx,
					struct mipi_i3c_hci_pci_cell_data *data,
					struct mfd_cell *cell)
{
	data->pdata.base_regs = hci->base + hci->info->instance_offset[idx];

	data->res = DEFINE_RES_IRQ(0);

	cell->name = hci->info->name;
	cell->id = hci->info->id[idx];
	cell->platform_data = &data->pdata;
	cell->pdata_size = sizeof(data->pdata);
	cell->num_resources = 1;
	cell->resources = &data->res;
}

#define mipi_i3c_hci_pci_alloc(h, x) kcalloc((h)->info->instance_count, sizeof(*(x)), GFP_KERNEL)

static int mipi_i3c_hci_pci_add_instances(struct mipi_i3c_hci_pci *hci)
{
	struct mipi_i3c_hci_pci_cell_data *data __free(kfree) = mipi_i3c_hci_pci_alloc(hci, data);
	struct mfd_cell *cells __free(kfree) = mipi_i3c_hci_pci_alloc(hci, cells);
	int irq = pci_irq_vector(hci->pci, 0);
	int nr = hci->info->instance_count;

	if (!cells || !data)
		return -ENOMEM;

	for (int i = 0; i < nr; i++)
		mipi_i3c_hci_pci_setup_cell(hci, i, data + i, cells + i);

	return mfd_add_devices(&hci->pci->dev, 0, cells, nr, NULL, irq, NULL);
}

static int mipi_i3c_hci_pci_probe(struct pci_dev *pci,
				  const struct pci_device_id *id)
{
	struct mipi_i3c_hci_pci *hci;
	int ret;

	hci = devm_kzalloc(&pci->dev, sizeof(*hci), GFP_KERNEL);
	if (!hci)
		return -ENOMEM;

	hci->pci = pci;

	ret = pcim_enable_device(pci);
	if (ret)
		return ret;

	pci_set_master(pci);

	hci->base = pcim_iomap_region(pci, 0, pci_name(pci));
	if (IS_ERR(hci->base))
		return PTR_ERR(hci->base);

	ret = pci_alloc_irq_vectors(pci, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		return ret;

	hci->info = (const struct mipi_i3c_hci_pci_info *)id->driver_data;

	ret = hci->info->init ? hci->info->init(hci) : 0;
	if (ret)
		return ret;

	ret = mipi_i3c_hci_pci_add_instances(hci);
	if (ret)
		goto err_exit;

	pci_set_drvdata(pci, hci);

	mipi_i3c_hci_pci_rpm_allow(&pci->dev);

	return 0;

err_exit:
	if (hci->info->exit)
		hci->info->exit(hci);
	return ret;
}

static void mipi_i3c_hci_pci_remove(struct pci_dev *pci)
{
	struct mipi_i3c_hci_pci *hci = pci_get_drvdata(pci);

	if (hci->info->exit)
		hci->info->exit(hci);

	mipi_i3c_hci_pci_rpm_forbid(&pci->dev);

	mfd_remove_devices(&pci->dev);
}

/* PM ops must exist for PCI to put a device to a low power state */
static const struct dev_pm_ops mipi_i3c_hci_pci_pm_ops = {
};

static const struct pci_device_id mipi_i3c_hci_pci_devices[] = {
	/* Wildcat Lake-U */
	{ PCI_VDEVICE(INTEL, 0x4d7c), (kernel_ulong_t)&intel_mi_1_info},
	{ PCI_VDEVICE(INTEL, 0x4d6f), (kernel_ulong_t)&intel_si_2_info},
	/* Panther Lake-H */
	{ PCI_VDEVICE(INTEL, 0xe37c), (kernel_ulong_t)&intel_mi_1_info},
	{ PCI_VDEVICE(INTEL, 0xe36f), (kernel_ulong_t)&intel_si_2_info},
	/* Panther Lake-P */
	{ PCI_VDEVICE(INTEL, 0xe47c), (kernel_ulong_t)&intel_mi_1_info},
	{ PCI_VDEVICE(INTEL, 0xe46f), (kernel_ulong_t)&intel_si_2_info},
	/* Nova Lake-S */
	{ PCI_VDEVICE(INTEL, 0x6e2c), (kernel_ulong_t)&intel_mi_1_info},
	{ PCI_VDEVICE(INTEL, 0x6e2d), (kernel_ulong_t)&intel_mi_2_info},
	{ },
};
MODULE_DEVICE_TABLE(pci, mipi_i3c_hci_pci_devices);

static struct pci_driver mipi_i3c_hci_pci_driver = {
	.name = "mipi_i3c_hci_pci",
	.id_table = mipi_i3c_hci_pci_devices,
	.probe = mipi_i3c_hci_pci_probe,
	.remove = mipi_i3c_hci_pci_remove,
	.driver = {
		.pm = pm_ptr(&mipi_i3c_hci_pci_pm_ops)
	},
};

module_pci_driver(mipi_i3c_hci_pci_driver);

MODULE_AUTHOR("Jarkko Nikula <jarkko.nikula@intel.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MIPI I3C HCI driver on PCI bus");
