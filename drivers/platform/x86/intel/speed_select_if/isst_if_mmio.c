// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Speed Select Interface: MMIO Interface
 * Copyright (c) 2019, Intel Corporation.
 * All rights reserved.
 *
 * Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>
#include <uapi/linux/isst_if.h>

#include "isst_if_common.h"

struct isst_mmio_range {
	int beg;
	int end;
	int size;
};

static struct isst_mmio_range mmio_range_devid_0[] = {
	{0x04, 0x14, 0x18},
	{0x20, 0xD0, 0xD4},
};

static struct isst_mmio_range mmio_range_devid_1[] = {
	{0x04, 0x14, 0x18},
	{0x20, 0x11C, 0x120},
};

struct isst_if_device {
	void __iomem *punit_mmio;
	u32 range_0[5];
	u32 range_1[64];
	struct isst_mmio_range *mmio_range;
	struct mutex mutex;
};

static long isst_if_mmio_rd_wr(u8 *cmd_ptr, int *write_only, int resume)
{
	struct isst_if_device *punit_dev;
	struct isst_if_io_reg *io_reg;
	struct pci_dev *pdev;

	io_reg = (struct isst_if_io_reg *)cmd_ptr;

	if (io_reg->reg % 4)
		return -EINVAL;

	if (io_reg->read_write && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	pdev = isst_if_get_pci_dev(io_reg->logical_cpu, 0, 0, 1);
	if (!pdev)
		return -EINVAL;

	punit_dev = pci_get_drvdata(pdev);
	if (!punit_dev)
		return -EINVAL;

	if (io_reg->reg < punit_dev->mmio_range[0].beg ||
	    io_reg->reg > punit_dev->mmio_range[1].end)
		return -EINVAL;

	/*
	 * Ensure that operation is complete on a PCI device to avoid read
	 * write race by using per PCI device mutex.
	 */
	mutex_lock(&punit_dev->mutex);
	if (io_reg->read_write) {
		writel(io_reg->value, punit_dev->punit_mmio+io_reg->reg);
		*write_only = 1;
	} else {
		io_reg->value = readl(punit_dev->punit_mmio+io_reg->reg);
		*write_only = 0;
	}
	mutex_unlock(&punit_dev->mutex);

	return 0;
}

static const struct pci_device_id isst_if_ids[] = {
	{ PCI_DEVICE_DATA(INTEL, RAPL_PRIO_DEVID_0, &mmio_range_devid_0)},
	{ PCI_DEVICE_DATA(INTEL, RAPL_PRIO_DEVID_1, &mmio_range_devid_1)},
	{ 0 },
};
MODULE_DEVICE_TABLE(pci, isst_if_ids);

static int isst_if_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct isst_if_device *punit_dev;
	struct isst_if_cmd_cb cb;
	u32 mmio_base, pcu_base;
	struct resource r;
	u64 base_addr;
	int ret;

	punit_dev = devm_kzalloc(&pdev->dev, sizeof(*punit_dev), GFP_KERNEL);
	if (!punit_dev)
		return -ENOMEM;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pci_read_config_dword(pdev, 0xD0, &mmio_base);
	if (ret)
		return pcibios_err_to_errno(ret);

	ret = pci_read_config_dword(pdev, 0xFC, &pcu_base);
	if (ret)
		return pcibios_err_to_errno(ret);

	pcu_base &= GENMASK(10, 0);
	base_addr = (u64)mmio_base << 23 | (u64) pcu_base << 12;

	punit_dev->mmio_range = (struct isst_mmio_range *) ent->driver_data;

	r = DEFINE_RES_MEM(base_addr, punit_dev->mmio_range[1].size);
	punit_dev->punit_mmio = devm_ioremap_resource(&pdev->dev, &r);
	if (IS_ERR(punit_dev->punit_mmio))
		return PTR_ERR(punit_dev->punit_mmio);

	mutex_init(&punit_dev->mutex);
	pci_set_drvdata(pdev, punit_dev);

	memset(&cb, 0, sizeof(cb));
	cb.cmd_size = sizeof(struct isst_if_io_reg);
	cb.offset = offsetof(struct isst_if_io_regs, io_reg);
	cb.cmd_callback = isst_if_mmio_rd_wr;
	cb.owner = THIS_MODULE;
	ret = isst_if_cdev_register(ISST_IF_DEV_MMIO, &cb);
	if (ret)
		mutex_destroy(&punit_dev->mutex);

	return ret;
}

static void isst_if_remove(struct pci_dev *pdev)
{
	struct isst_if_device *punit_dev;

	punit_dev = pci_get_drvdata(pdev);
	isst_if_cdev_unregister(ISST_IF_DEV_MMIO);
	mutex_destroy(&punit_dev->mutex);
}

static int __maybe_unused isst_if_suspend(struct device *device)
{
	struct isst_if_device *punit_dev = dev_get_drvdata(device);
	int i;

	for (i = 0; i < ARRAY_SIZE(punit_dev->range_0); ++i)
		punit_dev->range_0[i] = readl(punit_dev->punit_mmio +
						punit_dev->mmio_range[0].beg + 4 * i);
	for (i = 0; i < ARRAY_SIZE(punit_dev->range_1); ++i) {
		u32 addr;

		addr = punit_dev->mmio_range[1].beg + 4 * i;
		if (addr > punit_dev->mmio_range[1].end)
			break;
		punit_dev->range_1[i] = readl(punit_dev->punit_mmio + addr);
	}

	return 0;
}

static int __maybe_unused isst_if_resume(struct device *device)
{
	struct isst_if_device *punit_dev = dev_get_drvdata(device);
	int i;

	for (i = 0; i < ARRAY_SIZE(punit_dev->range_0); ++i)
		writel(punit_dev->range_0[i], punit_dev->punit_mmio +
						punit_dev->mmio_range[0].beg + 4 * i);
	for (i = 0; i < ARRAY_SIZE(punit_dev->range_1); ++i) {
		u32 addr;

		addr = punit_dev->mmio_range[1].beg + 4 * i;
		if (addr > punit_dev->mmio_range[1].end)
			break;

		writel(punit_dev->range_1[i], punit_dev->punit_mmio + addr);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(isst_if_pm_ops, isst_if_suspend, isst_if_resume);

static struct pci_driver isst_if_pci_driver = {
	.name			= "isst_if_pci",
	.id_table		= isst_if_ids,
	.probe			= isst_if_probe,
	.remove			= isst_if_remove,
	.driver.pm		= &isst_if_pm_ops,
};

module_pci_driver(isst_if_pci_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel speed select interface mmio driver");
