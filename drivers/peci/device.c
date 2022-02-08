// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2018-2021 Intel Corporation

#include <linux/peci.h>
#include <linux/slab.h>

#include "internal.h"

/*
 * PECI device can be removed using sysfs, but the removal can also happen as
 * a result of controller being removed.
 * Mutex is used to protect PECI device from being double-deleted.
 */
static DEFINE_MUTEX(peci_device_del_lock);

static int peci_detect(struct peci_controller *controller, u8 addr)
{
	/*
	 * PECI Ping is a command encoded by tx_len = 0, rx_len = 0.
	 * We expect correct Write FCS if the device at the target address
	 * is able to respond.
	 */
	struct peci_request req = { 0 };
	int ret;

	mutex_lock(&controller->bus_lock);
	ret = controller->ops->xfer(controller, addr, &req);
	mutex_unlock(&controller->bus_lock);

	return ret;
}

static bool peci_addr_valid(u8 addr)
{
	return addr >= PECI_BASE_ADDR && addr < PECI_BASE_ADDR + PECI_DEVICE_NUM_MAX;
}

static int peci_dev_exists(struct device *dev, void *data)
{
	struct peci_device *device = to_peci_device(dev);
	u8 *addr = data;

	if (device->addr == *addr)
		return -EBUSY;

	return 0;
}

int peci_device_create(struct peci_controller *controller, u8 addr)
{
	struct peci_device *device;
	int ret;

	if (!peci_addr_valid(addr))
		return -EINVAL;

	/* Check if we have already detected this device before. */
	ret = device_for_each_child(&controller->dev, &addr, peci_dev_exists);
	if (ret)
		return 0;

	ret = peci_detect(controller, addr);
	if (ret) {
		/*
		 * Device not present or host state doesn't allow successful
		 * detection at this time.
		 */
		if (ret == -EIO || ret == -ETIMEDOUT)
			return 0;

		return ret;
	}

	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (!device)
		return -ENOMEM;

	device_initialize(&device->dev);

	device->addr = addr;
	device->dev.parent = &controller->dev;
	device->dev.bus = &peci_bus_type;
	device->dev.type = &peci_device_type;

	ret = dev_set_name(&device->dev, "%d-%02x", controller->id, device->addr);
	if (ret)
		goto err_put;

	ret = device_add(&device->dev);
	if (ret)
		goto err_put;

	return 0;

err_put:
	put_device(&device->dev);

	return ret;
}

void peci_device_destroy(struct peci_device *device)
{
	mutex_lock(&peci_device_del_lock);
	if (!device->deleted) {
		device_unregister(&device->dev);
		device->deleted = true;
	}
	mutex_unlock(&peci_device_del_lock);
}

static void peci_device_release(struct device *dev)
{
	struct peci_device *device = to_peci_device(dev);

	kfree(device);
}

struct device_type peci_device_type = {
	.release	= peci_device_release,
};
