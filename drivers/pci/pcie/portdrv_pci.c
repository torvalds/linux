/*
 * File:	portdrv_pci.c
 * Purpose:	PCI Express Port Bus Driver
 *
 * Copyright (C) 2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pm.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pcieport_if.h>
#include <linux/aer.h>

#include "portdrv.h"
#include "aer/aerdrv.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.0"
#define DRIVER_AUTHOR "tom.l.nguyen@intel.com"
#define DRIVER_DESC "PCIE Port Bus Driver"
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/* global data */
static const char device_name[] = "pcieport-driver";

static int pcie_portdrv_save_config(struct pci_dev *dev)
{
	return pci_save_state(dev);
}

static int pcie_portdrv_restore_config(struct pci_dev *dev)
{
	int retval;

	pci_restore_state(dev);
	retval = pci_enable_device(dev);
	if (retval)
		return retval;
	pci_set_master(dev);
	return 0;
}

#ifdef CONFIG_PM
static int pcie_portdrv_suspend(struct pci_dev *dev, pm_message_t state)
{
	int ret = pcie_port_device_suspend(dev, state);

	if (!ret)
		ret = pcie_portdrv_save_config(dev);
	return ret;
}

static int pcie_portdrv_resume(struct pci_dev *dev)
{
	pcie_portdrv_restore_config(dev);
	return pcie_port_device_resume(dev);
}
#else
#define pcie_portdrv_suspend NULL
#define pcie_portdrv_resume NULL
#endif

/*
 * pcie_portdrv_probe - Probe PCI-Express port devices
 * @dev: PCI-Express port device being probed
 *
 * If detected invokes the pcie_port_device_register() method for 
 * this port device.
 *
 */
static int __devinit pcie_portdrv_probe (struct pci_dev *dev, 
				const struct pci_device_id *id )
{
	int			status;

	status = pcie_port_device_probe(dev);
	if (status)
		return status;

	if (pci_enable_device(dev) < 0) 
		return -ENODEV;
	
	pci_set_master(dev);
        if (!dev->irq && dev->pin) {
		printk(KERN_WARNING 
		"%s->Dev[%04x:%04x] has invalid IRQ. Check vendor BIOS\n", 
		__func__, dev->vendor, dev->device);
	}
	if (pcie_port_device_register(dev)) {
		pci_disable_device(dev);
		return -ENOMEM;
	}

	pcie_portdrv_save_config(dev);

	pci_enable_pcie_error_reporting(dev);

	return 0;
}

static void pcie_portdrv_remove (struct pci_dev *dev)
{
	pcie_port_device_remove(dev);
	kfree(pci_get_drvdata(dev));
}

static int error_detected_iter(struct device *device, void *data)
{
	struct pcie_device *pcie_device;
	struct pcie_port_service_driver *driver;
	struct aer_broadcast_data *result_data;
	pci_ers_result_t status;

	result_data = (struct aer_broadcast_data *) data;

	if (device->bus == &pcie_port_bus_type && device->driver) {
		driver = to_service_driver(device->driver);
		if (!driver ||
			!driver->err_handler ||
			!driver->err_handler->error_detected)
			return 0;

		pcie_device = to_pcie_device(device);

		/* Forward error detected message to service drivers */
		status = driver->err_handler->error_detected(
			pcie_device->port,
			result_data->state);
		result_data->result =
			merge_result(result_data->result, status);
	}

	return 0;
}

static pci_ers_result_t pcie_portdrv_error_detected(struct pci_dev *dev,
					enum pci_channel_state error)
{
	struct aer_broadcast_data result_data =
			{error, PCI_ERS_RESULT_CAN_RECOVER};
	int retval;

	/* can not fail */
	retval = device_for_each_child(&dev->dev, &result_data, error_detected_iter);

	return result_data.result;
}

static int mmio_enabled_iter(struct device *device, void *data)
{
	struct pcie_device *pcie_device;
	struct pcie_port_service_driver *driver;
	pci_ers_result_t status, *result;

	result = (pci_ers_result_t *) data;

	if (device->bus == &pcie_port_bus_type && device->driver) {
		driver = to_service_driver(device->driver);
		if (driver &&
			driver->err_handler &&
			driver->err_handler->mmio_enabled) {
			pcie_device = to_pcie_device(device);

			/* Forward error message to service drivers */
			status = driver->err_handler->mmio_enabled(
					pcie_device->port);
			*result = merge_result(*result, status);
		}
	}

	return 0;
}

static pci_ers_result_t pcie_portdrv_mmio_enabled(struct pci_dev *dev)
{
	pci_ers_result_t status = PCI_ERS_RESULT_RECOVERED;
	int retval;

	/* get true return value from &status */
	retval = device_for_each_child(&dev->dev, &status, mmio_enabled_iter);
	return status;
}

static int slot_reset_iter(struct device *device, void *data)
{
	struct pcie_device *pcie_device;
	struct pcie_port_service_driver *driver;
	pci_ers_result_t status, *result;

	result = (pci_ers_result_t *) data;

	if (device->bus == &pcie_port_bus_type && device->driver) {
		driver = to_service_driver(device->driver);
		if (driver &&
			driver->err_handler &&
			driver->err_handler->slot_reset) {
			pcie_device = to_pcie_device(device);

			/* Forward error message to service drivers */
			status = driver->err_handler->slot_reset(
					pcie_device->port);
			*result = merge_result(*result, status);
		}
	}

	return 0;
}

static pci_ers_result_t pcie_portdrv_slot_reset(struct pci_dev *dev)
{
	pci_ers_result_t status = PCI_ERS_RESULT_NONE;
	int retval;

	/* If fatal, restore cfg space for possible link reset at upstream */
	if (dev->error_state == pci_channel_io_frozen) {
		pcie_portdrv_restore_config(dev);
		pci_enable_pcie_error_reporting(dev);
	}

	/* get true return value from &status */
	retval = device_for_each_child(&dev->dev, &status, slot_reset_iter);

	return status;
}

static int resume_iter(struct device *device, void *data)
{
	struct pcie_device *pcie_device;
	struct pcie_port_service_driver *driver;

	if (device->bus == &pcie_port_bus_type && device->driver) {
		driver = to_service_driver(device->driver);
		if (driver &&
			driver->err_handler &&
			driver->err_handler->resume) {
			pcie_device = to_pcie_device(device);

			/* Forward error message to service drivers */
			driver->err_handler->resume(pcie_device->port);
		}
	}

	return 0;
}

static void pcie_portdrv_err_resume(struct pci_dev *dev)
{
	int retval;
	/* nothing to do with error value, if it ever happens */
	retval = device_for_each_child(&dev->dev, NULL, resume_iter);
}

/*
 * LINUX Device Driver Model
 */
static const struct pci_device_id port_pci_ids[] = { {
	/* handle any PCI-Express port */
	PCI_DEVICE_CLASS(((PCI_CLASS_BRIDGE_PCI << 8) | 0x00), ~0),
	}, { /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE(pci, port_pci_ids);

static struct pci_error_handlers pcie_portdrv_err_handler = {
		.error_detected = pcie_portdrv_error_detected,
		.mmio_enabled = pcie_portdrv_mmio_enabled,
		.slot_reset = pcie_portdrv_slot_reset,
		.resume = pcie_portdrv_err_resume,
};

static struct pci_driver pcie_portdriver = {
	.name		= (char *)device_name,
	.id_table	= &port_pci_ids[0],

	.probe		= pcie_portdrv_probe,
	.remove		= pcie_portdrv_remove,

	.suspend	= pcie_portdrv_suspend,
	.resume		= pcie_portdrv_resume,

	.err_handler 	= &pcie_portdrv_err_handler,
};

static int __init pcie_portdrv_init(void)
{
	int retval;

	retval = pcie_port_bus_register();
	if (retval) {
		printk(KERN_WARNING "PCIE: bus_register error: %d\n", retval);
		goto out;
	}
	retval = pci_register_driver(&pcie_portdriver);
	if (retval)
		pcie_port_bus_unregister();
 out:
	return retval;
}

static void __exit pcie_portdrv_exit(void) 
{
	pci_unregister_driver(&pcie_portdriver);
	pcie_port_bus_unregister();
}

module_init(pcie_portdrv_init);
module_exit(pcie_portdrv_exit);
