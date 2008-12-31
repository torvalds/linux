/*
 * UWB Multi-interface Controller device management.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * This file is released under the GNU GPL v2.
 */
#include <linux/kernel.h>
#include <linux/uwb/umc.h>
#define D_LOCAL 0
#include <linux/uwb/debug.h>

static void umc_device_release(struct device *dev)
{
	struct umc_dev *umc = to_umc_dev(dev);

	kfree(umc);
}

/**
 * umc_device_create - allocate a child UMC device
 * @parent: parent of the new UMC device.
 * @n:      index of the new device.
 *
 * The new UMC device will have a bus ID of the parent with '-n'
 * appended.
 */
struct umc_dev *umc_device_create(struct device *parent, int n)
{
	struct umc_dev *umc;

	umc = kzalloc(sizeof(struct umc_dev), GFP_KERNEL);
	if (umc) {
		snprintf(umc->dev.bus_id, sizeof(umc->dev.bus_id), "%s-%d",
			 parent->bus_id, n);
		umc->dev.parent  = parent;
		umc->dev.bus     = &umc_bus_type;
		umc->dev.release = umc_device_release;

		umc->dev.dma_mask = parent->dma_mask;
	}
	return umc;
}
EXPORT_SYMBOL_GPL(umc_device_create);

/**
 * umc_device_register - register a UMC device
 * @umc: pointer to the UMC device
 *
 * The memory resource for the UMC device is acquired and the device
 * registered with the system.
 */
int umc_device_register(struct umc_dev *umc)
{
	int err;

	d_fnstart(3, &umc->dev, "(umc_dev %p)\n", umc);

	err = request_resource(umc->resource.parent, &umc->resource);
	if (err < 0) {
		dev_err(&umc->dev, "can't allocate resource range "
			"%016Lx to %016Lx: %d\n",
			(unsigned long long)umc->resource.start,
			(unsigned long long)umc->resource.end,
			err);
		goto error_request_resource;
	}

	err = device_register(&umc->dev);
	if (err < 0)
		goto error_device_register;
	d_fnend(3, &umc->dev, "(umc_dev %p) = 0\n", umc);
	return 0;

error_device_register:
	release_resource(&umc->resource);
error_request_resource:
	d_fnend(3, &umc->dev, "(umc_dev %p) = %d\n", umc, err);
	return err;
}
EXPORT_SYMBOL_GPL(umc_device_register);

/**
 * umc_device_unregister - unregister a UMC device
 * @umc: pointer to the UMC device
 *
 * First we unregister the device, make sure the driver can do it's
 * resource release thing and then we try to release any left over
 * resources. We take a ref to the device, to make sure it doesn't
 * dissapear under our feet.
 */
void umc_device_unregister(struct umc_dev *umc)
{
	struct device *dev;
	if (!umc)
		return;
	dev = get_device(&umc->dev);
	d_fnstart(3, dev, "(umc_dev %p)\n", umc);
	device_unregister(&umc->dev);
	release_resource(&umc->resource);
	d_fnend(3, dev, "(umc_dev %p) = void\n", umc);
	put_device(dev);
}
EXPORT_SYMBOL_GPL(umc_device_unregister);
