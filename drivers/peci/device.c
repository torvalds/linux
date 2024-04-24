// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2018-2021 Intel Corporation

#include <linux/bitfield.h>
#include <linux/peci.h>
#include <linux/peci-cpu.h>
#include <linux/slab.h>

#include "internal.h"

/*
 * PECI device can be removed using sysfs, but the removal can also happen as
 * a result of controller being removed.
 * Mutex is used to protect PECI device from being double-deleted.
 */
static DEFINE_MUTEX(peci_device_del_lock);

#define REVISION_NUM_MASK GENMASK(15, 8)
static int peci_get_revision(struct peci_device *device, u8 *revision)
{
	struct peci_request *req;
	u64 dib;

	req = peci_xfer_get_dib(device);
	if (IS_ERR(req))
		return PTR_ERR(req);

	/*
	 * PECI device may be in a state where it is unable to return a proper
	 * DIB, in which case it returns 0 as DIB value.
	 * Let's treat this as an error to avoid carrying on with the detection
	 * using invalid revision.
	 */
	dib = peci_request_dib_read(req);
	if (dib == 0) {
		peci_request_free(req);
		return -EIO;
	}

	*revision = FIELD_GET(REVISION_NUM_MASK, dib);

	peci_request_free(req);

	return 0;
}

static int peci_get_cpu_id(struct peci_device *device, u32 *cpu_id)
{
	struct peci_request *req;
	int ret;

	req = peci_xfer_pkg_cfg_readl(device, PECI_PCS_PKG_ID, PECI_PKG_ID_CPU_ID);
	if (IS_ERR(req))
		return PTR_ERR(req);

	ret = peci_request_status(req);
	if (ret)
		goto out_req_free;

	*cpu_id = peci_request_data_readl(req);
out_req_free:
	peci_request_free(req);

	return ret;
}

static unsigned int peci_x86_cpu_family(unsigned int sig)
{
	unsigned int x86;

	x86 = (sig >> 8) & 0xf;

	if (x86 == 0xf)
		x86 += (sig >> 20) & 0xff;

	return x86;
}

static unsigned int peci_x86_cpu_model(unsigned int sig)
{
	unsigned int fam, model;

	fam = peci_x86_cpu_family(sig);

	model = (sig >> 4) & 0xf;

	if (fam >= 0x6)
		model += ((sig >> 16) & 0xf) << 4;

	return model;
}

static int peci_device_info_init(struct peci_device *device)
{
	u8 revision;
	u32 cpu_id;
	int ret;

	ret = peci_get_cpu_id(device, &cpu_id);
	if (ret)
		return ret;

	device->info.family = peci_x86_cpu_family(cpu_id);
	device->info.model = peci_x86_cpu_model(cpu_id);

	ret = peci_get_revision(device, &revision);
	if (ret)
		return ret;
	device->info.peci_revision = revision;

	device->info.socket_id = device->addr - PECI_BASE_ADDR;

	return 0;
}

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

	ret = peci_device_info_init(device);
	if (ret)
		goto err_put;

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

int __peci_driver_register(struct peci_driver *driver, struct module *owner,
			   const char *mod_name)
{
	driver->driver.bus = &peci_bus_type;
	driver->driver.owner = owner;
	driver->driver.mod_name = mod_name;

	if (!driver->probe) {
		pr_err("peci: trying to register driver without probe callback\n");
		return -EINVAL;
	}

	if (!driver->id_table) {
		pr_err("peci: trying to register driver without device id table\n");
		return -EINVAL;
	}

	return driver_register(&driver->driver);
}
EXPORT_SYMBOL_NS_GPL(__peci_driver_register, PECI);

void peci_driver_unregister(struct peci_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_NS_GPL(peci_driver_unregister, PECI);

static void peci_device_release(struct device *dev)
{
	struct peci_device *device = to_peci_device(dev);

	kfree(device);
}

const struct device_type peci_device_type = {
	.groups		= peci_device_groups,
	.release	= peci_device_release,
};
