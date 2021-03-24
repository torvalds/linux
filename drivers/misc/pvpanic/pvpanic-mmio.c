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

#include <uapi/misc/pvpanic.h>

#include "pvpanic.h"

MODULE_AUTHOR("Hu Tao <hutao@cn.fujitsu.com>");
MODULE_DESCRIPTION("pvpanic-mmio device driver");
MODULE_LICENSE("GPL");

static void __iomem *base;
static unsigned int capability = PVPANIC_PANICKED | PVPANIC_CRASH_LOADED;
static unsigned int events;

static ssize_t capability_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%x\n", capability);
}
static DEVICE_ATTR_RO(capability);

static ssize_t events_show(struct device *dev,  struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%x\n", events);
}

static ssize_t events_store(struct device *dev,  struct device_attribute *attr,
			    const char *buf, size_t count)
{
	unsigned int tmp;
	int err;

	err = kstrtouint(buf, 16, &tmp);
	if (err)
		return err;

	if ((tmp & capability) != tmp)
		return -EINVAL;

	events = tmp;

	pvpanic_set_events(events);

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
	struct resource *res;

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

	/* initlize capability by RDPT */
	capability &= ioread8(base);
	events = capability;

	pvpanic_probe(base, capability);

	return 0;
}

static int pvpanic_mmio_remove(struct platform_device *pdev)
{

	pvpanic_remove();

	return 0;
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
	.remove = pvpanic_mmio_remove,
};
module_platform_driver(pvpanic_mmio_driver);
