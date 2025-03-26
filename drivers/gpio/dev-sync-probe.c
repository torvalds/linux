// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Common code for drivers creating fake platform devices.
 *
 * Provides synchronous device creation: waits for probe completion and
 * returns the probe success or error status to the device creator.
 *
 * Copyright (C) 2021 Bartosz Golaszewski <brgl@bgdev.pl>
 * Copyright (C) 2025 Koichiro Den <koichiro.den@canonical.com>
 */

#include <linux/device.h>
#include <linux/slab.h>

#include "dev-sync-probe.h"

static int dev_sync_probe_notifier_call(struct notifier_block *nb,
					unsigned long action, void *data)
{
	struct dev_sync_probe_data *pdata;
	struct device *dev = data;

	pdata = container_of(nb, struct dev_sync_probe_data, bus_notifier);
	if (!device_match_name(dev, pdata->name))
		return NOTIFY_DONE;

	switch (action) {
	case BUS_NOTIFY_BOUND_DRIVER:
		pdata->driver_bound = true;
		break;
	case BUS_NOTIFY_DRIVER_NOT_BOUND:
		pdata->driver_bound = false;
		break;
	default:
		return NOTIFY_DONE;
	}

	complete(&pdata->probe_completion);
	return NOTIFY_OK;
}

void dev_sync_probe_init(struct dev_sync_probe_data *data)
{
	memset(data, 0, sizeof(*data));
	init_completion(&data->probe_completion);
	data->bus_notifier.notifier_call = dev_sync_probe_notifier_call;
}
EXPORT_SYMBOL_GPL(dev_sync_probe_init);

int dev_sync_probe_register(struct dev_sync_probe_data *data,
			    struct platform_device_info *pdevinfo)
{
	struct platform_device *pdev;
	char *name;

	name = kasprintf(GFP_KERNEL, "%s.%d", pdevinfo->name, pdevinfo->id);
	if (!name)
		return -ENOMEM;

	data->driver_bound = false;
	data->name = name;
	reinit_completion(&data->probe_completion);
	bus_register_notifier(&platform_bus_type, &data->bus_notifier);

	pdev = platform_device_register_full(pdevinfo);
	if (IS_ERR(pdev)) {
		bus_unregister_notifier(&platform_bus_type, &data->bus_notifier);
		kfree(data->name);
		return PTR_ERR(pdev);
	}

	wait_for_completion(&data->probe_completion);
	bus_unregister_notifier(&platform_bus_type, &data->bus_notifier);

	if (!data->driver_bound) {
		platform_device_unregister(pdev);
		kfree(data->name);
		return -ENXIO;
	}

	data->pdev = pdev;
	return 0;
}
EXPORT_SYMBOL_GPL(dev_sync_probe_register);

void dev_sync_probe_unregister(struct dev_sync_probe_data *data)
{
	platform_device_unregister(data->pdev);
	kfree(data->name);
	data->pdev = NULL;
}
EXPORT_SYMBOL_GPL(dev_sync_probe_unregister);

MODULE_AUTHOR("Bartosz Golaszewski <brgl@bgdev.pl>");
MODULE_AUTHOR("Koichiro Den <koichiro.den@canonical.com>");
MODULE_DESCRIPTION("Utilities for synchronous fake device creation");
MODULE_LICENSE("GPL");
