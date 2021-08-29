// SPDX-License-Identifier: GPL-2.0+
/*
 *  Pvpanic MMIO Device Support
 *
 *  Copyright (C) 2013 Fujitsu.
 *  Copyright (C) 2018 ZTE.
 *  Copyright (C) 2021 Oracle.
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/slab.h>

#include <uapi/misc/pvpanic.h>

#include "pvpanic.h"

MODULE_AUTHOR("Hu Tao <hutao@cn.fujitsu.com>");
MODULE_DESCRIPTION("pvpanic-mmio device driver");
MODULE_LICENSE("GPL");

static ssize_t capability_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct pvpanic_instance *pi = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%x\n", pi->capability);
}
static DEVICE_ATTR_RO(capability);

static ssize_t events_show(struct device *dev,  struct device_attribute *attr, char *buf)
{
	struct pvpanic_instance *pi = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%x\n", pi->events);
}

static ssize_t events_store(struct device *dev,  struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct pvpanic_instance *pi = dev_get_drvdata(dev);
	unsigned int tmp;
	int err;

	err = kstrtouint(buf, 16, &tmp);
	if (err)
		return err;

	if ((tmp & pi->capability) != tmp)
		return -EINVAL;

	pi->events = tmp;

	return count;
}
static DEVICE_ATTR_RW(events);

static struct attribute *pvpanic_mmio_dev_attrs[] = {
	&dev_attr_capability.attr,
	&dev_attr_events.attr,
	NULL
};
ATTRIBUTE_GROUPS(pvpanic_mmio_dev);

static int pvpanic_mmio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pvpanic_instance *pi;
	struct resource *res;
	void __iomem *base;

	res = platform_get_mem_or_io(pdev, 0);
	if (!res)
		return -EINVAL;

	switch (resource_type(res)) {
	case IORESOURCE_IO:
		base = devm_ioport_map(dev, res->start, resource_size(res));
		if (!base)
			return -ENOMEM;
		break;
	case IORESOURCE_MEM:
		base = devm_ioremap_resource(dev, res);
		if (IS_ERR(base))
			return PTR_ERR(base);
		break;
	default:
		return -EINVAL;
	}

	pi = devm_kmalloc(dev, sizeof(*pi), GFP_KERNEL);
	if (!pi)
		return -ENOMEM;

	pi->base = base;
	pi->capability = PVPANIC_PANICKED | PVPANIC_CRASH_LOADED;

	/* initialize capability by RDPT */
	pi->capability &= ioread8(base);
	pi->events = pi->capability;

	return devm_pvpanic_probe(dev, pi);
}

static const struct of_device_id pvpanic_mmio_match[] = {
	{ .compatible = "qemu,pvpanic-mmio", },
	{}
};
MODULE_DEVICE_TABLE(of, pvpanic_mmio_match);

static const struct acpi_device_id pvpanic_device_ids[] = {
	{ "QEMU0001", 0 },
	{ "", 0 }
};
MODULE_DEVICE_TABLE(acpi, pvpanic_device_ids);

static struct platform_driver pvpanic_mmio_driver = {
	.driver = {
		.name = "pvpanic-mmio",
		.of_match_table = pvpanic_mmio_match,
		.acpi_match_table = pvpanic_device_ids,
		.dev_groups = pvpanic_mmio_dev_groups,
	},
	.probe = pvpanic_mmio_probe,
};
module_platform_driver(pvpanic_mmio_driver);
