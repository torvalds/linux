// SPDX-License-Identifier: GPL-2.0+
/*
 *  Pvpanic Device Support
 *
 *  Copyright (C) 2013 Fujitsu.
 *  Copyright (C) 2018 ZTE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <uapi/misc/pvpanic.h>

static void __iomem *base;

MODULE_AUTHOR("Hu Tao <hutao@cn.fujitsu.com>");
MODULE_DESCRIPTION("pvpanic device driver");
MODULE_LICENSE("GPL");

static void
pvpanic_send_event(unsigned int event)
{
	iowrite8(event, base);
}

static int
pvpanic_panic_notify(struct notifier_block *nb, unsigned long code,
		     void *unused)
{
	unsigned int event = PVPANIC_PANICKED;

	if (kexec_crash_loaded())
		event = PVPANIC_CRASH_LOADED;

	pvpanic_send_event(event);

	return NOTIFY_DONE;
}

static struct notifier_block pvpanic_panic_nb = {
	.notifier_call = pvpanic_panic_notify,
	.priority = 1, /* let this called before broken drm_fb_helper */
};

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

	atomic_notifier_chain_register(&panic_notifier_list,
				       &pvpanic_panic_nb);

	return 0;
}

static int pvpanic_mmio_remove(struct platform_device *pdev)
{

	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &pvpanic_panic_nb);

	return 0;
}

static const struct of_device_id pvpanic_mmio_match[] = {
	{ .compatible = "qemu,pvpanic-mmio", },
	{}
};

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
	},
	.probe = pvpanic_mmio_probe,
	.remove = pvpanic_mmio_remove,
};
module_platform_driver(pvpanic_mmio_driver);
