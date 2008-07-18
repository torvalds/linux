/*
 * File:	portdrv_bus.c
 * Purpose:	PCI Express Port Bus Driver's Bus Overloading Functions
 *
 * Copyright (C) 2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pm.h>

#include <linux/pcieport_if.h>
#include "portdrv.h"

static int pcie_port_bus_match(struct device *dev, struct device_driver *drv);
static int pcie_port_bus_suspend(struct device *dev, pm_message_t state);
static int pcie_port_bus_resume(struct device *dev);

struct bus_type pcie_port_bus_type = {
	.name 		= "pci_express",
	.match 		= pcie_port_bus_match,
	.suspend	= pcie_port_bus_suspend,
	.resume		= pcie_port_bus_resume, 
};
EXPORT_SYMBOL_GPL(pcie_port_bus_type);

static int pcie_port_bus_match(struct device *dev, struct device_driver *drv)
{
	struct pcie_device *pciedev;
	struct pcie_port_service_driver *driver;

	if (drv->bus != &pcie_port_bus_type || dev->bus != &pcie_port_bus_type)
		return 0;
	
	pciedev = to_pcie_device(dev);
	driver = to_service_driver(drv);
	if (   (driver->id_table->vendor != PCI_ANY_ID && 
		driver->id_table->vendor != pciedev->id.vendor) ||
	       (driver->id_table->device != PCI_ANY_ID &&
		driver->id_table->device != pciedev->id.device) ||	
	       (driver->id_table->port_type != PCIE_ANY_PORT &&
		driver->id_table->port_type != pciedev->id.port_type) ||
		driver->id_table->service_type != pciedev->id.service_type )
		return 0;

	return 1;
}

static int pcie_port_bus_suspend(struct device *dev, pm_message_t state)
{
	struct pcie_device *pciedev;
	struct pcie_port_service_driver *driver;

	if (!dev || !dev->driver)
		return 0;

	pciedev = to_pcie_device(dev);
 	driver = to_service_driver(dev->driver);
	if (driver && driver->suspend)
		driver->suspend(pciedev, state);
	return 0;
}

static int pcie_port_bus_resume(struct device *dev)
{
	struct pcie_device *pciedev;
	struct pcie_port_service_driver *driver;

	if (!dev || !dev->driver)
		return 0;

	pciedev = to_pcie_device(dev);
 	driver = to_service_driver(dev->driver);
	if (driver && driver->resume)
		driver->resume(pciedev);
	return 0;
}
