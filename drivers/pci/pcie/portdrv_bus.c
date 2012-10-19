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

struct bus_type pcie_port_bus_type = {
	.name 		= "pci_express",
	.match 		= pcie_port_bus_match,
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

	if (driver->service != pciedev->service)
		return 0;

	if ((driver->port_type != PCIE_ANY_PORT) &&
	    (driver->port_type != pci_pcie_type(pciedev->port)))
		return 0;

	return 1;
}

int pcie_port_bus_register(void)
{
	return bus_register(&pcie_port_bus_type);
}

void pcie_port_bus_unregister(void)
{
	bus_unregister(&pcie_port_bus_type);
}
