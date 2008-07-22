/*
 * File:	portdrv_core.c
 * Purpose:	PCI Express Port Bus Driver's Core Functions
 *
 * Copyright (C) 2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pm.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/pcieport_if.h>

#include "portdrv.h"

extern int pcie_mch_quirk;	/* MSI-quirk Indicator */

static int pcie_port_probe_service(struct device *dev)
{
	struct pcie_device *pciedev;
	struct pcie_port_service_driver *driver;
	int status;

	if (!dev || !dev->driver)
		return -ENODEV;

 	driver = to_service_driver(dev->driver);
	if (!driver || !driver->probe)
		return -ENODEV;

	pciedev = to_pcie_device(dev);
	status = driver->probe(pciedev, driver->id_table);
	if (!status) {
		dev_printk(KERN_DEBUG, dev, "service driver %s loaded\n",
			driver->name);
		get_device(dev);
	}
	return status;
}

static int pcie_port_remove_service(struct device *dev)
{
	struct pcie_device *pciedev;
	struct pcie_port_service_driver *driver;

	if (!dev || !dev->driver)
		return 0;

	pciedev = to_pcie_device(dev);
 	driver = to_service_driver(dev->driver);
	if (driver && driver->remove) { 
		dev_printk(KERN_DEBUG, dev, "unloading service driver %s\n",
			driver->name);
		driver->remove(pciedev);
		put_device(dev);
	}
	return 0;
}

static void pcie_port_shutdown_service(struct device *dev) {}

static int pcie_port_suspend_service(struct device *dev, pm_message_t state)
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

static int pcie_port_resume_service(struct device *dev)
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

/*
 * release_pcie_device
 *	
 *	Being invoked automatically when device is being removed 
 *	in response to device_unregister(dev) call.
 *	Release all resources being claimed.
 */
static void release_pcie_device(struct device *dev)
{
	dev_printk(KERN_DEBUG, dev, "free port service\n");
	kfree(to_pcie_device(dev));			
}

static int is_msi_quirked(struct pci_dev *dev)
{
	int port_type, quirk = 0;
	u16 reg16;

	pci_read_config_word(dev, 
		pci_find_capability(dev, PCI_CAP_ID_EXP) + 
		PCIE_CAPABILITIES_REG, &reg16);
	port_type = (reg16 >> 4) & PORT_TYPE_MASK;
	switch(port_type) {
	case PCIE_RC_PORT:
		if (pcie_mch_quirk == 1)
			quirk = 1;
		break;
	case PCIE_SW_UPSTREAM_PORT:
	case PCIE_SW_DOWNSTREAM_PORT:
	default:
		break;	
	}
	return quirk;
}
	
static int assign_interrupt_mode(struct pci_dev *dev, int *vectors, int mask)
{
	int i, pos, nvec, status = -EINVAL;
	int interrupt_mode = PCIE_PORT_INTx_MODE;

	/* Set INTx as default */
	for (i = 0, nvec = 0; i < PCIE_PORT_DEVICE_MAXSERVICES; i++) {
		if (mask & (1 << i)) 
			nvec++;
		vectors[i] = dev->irq;
	}
	
	/* Check MSI quirk */
	if (is_msi_quirked(dev))
		return interrupt_mode;

	/* Select MSI-X over MSI if supported */		
	pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);
	if (pos) {
		struct msix_entry msix_entries[PCIE_PORT_DEVICE_MAXSERVICES] = 
			{{0, 0}, {0, 1}, {0, 2}, {0, 3}};
		dev_info(&dev->dev, "found MSI-X capability\n");
		status = pci_enable_msix(dev, msix_entries, nvec);
		if (!status) {
			int j = 0;

			interrupt_mode = PCIE_PORT_MSIX_MODE;
			for (i = 0; i < PCIE_PORT_DEVICE_MAXSERVICES; i++) {
				if (mask & (1 << i)) 
					vectors[i] = msix_entries[j++].vector;
			}
		}
	} 
	if (status) {
		pos = pci_find_capability(dev, PCI_CAP_ID_MSI);
		if (pos) {
			dev_info(&dev->dev, "found MSI capability\n");
			status = pci_enable_msi(dev);
			if (!status) {
				interrupt_mode = PCIE_PORT_MSI_MODE;
				for (i = 0;i < PCIE_PORT_DEVICE_MAXSERVICES;i++)
					vectors[i] = dev->irq;
			}
		}
	} 
	return interrupt_mode;
}

static int get_port_device_capability(struct pci_dev *dev)
{
	int services = 0, pos;
	u16 reg16;
	u32 reg32;

	pos = pci_find_capability(dev, PCI_CAP_ID_EXP);
	pci_read_config_word(dev, pos + PCIE_CAPABILITIES_REG, &reg16);
	/* Hot-Plug Capable */
	if (reg16 & PORT_TO_SLOT_MASK) {
		pci_read_config_dword(dev, 
			pos + PCIE_SLOT_CAPABILITIES_REG, &reg32);
		if (reg32 & SLOT_HP_CAPABLE_MASK)
			services |= PCIE_PORT_SERVICE_HP;
	} 
	/* PME Capable - root port capability */
	if (((reg16 >> 4) & PORT_TYPE_MASK) == PCIE_RC_PORT)
		services |= PCIE_PORT_SERVICE_PME;
	
	pos = PCI_CFG_SPACE_SIZE;
	while (pos) {
		pci_read_config_dword(dev, pos, &reg32);
		switch (reg32 & 0xffff) {
		case PCI_EXT_CAP_ID_ERR:
			services |= PCIE_PORT_SERVICE_AER;
			pos = reg32 >> 20;
			break;
		case PCI_EXT_CAP_ID_VC:
			services |= PCIE_PORT_SERVICE_VC;
			pos = reg32 >> 20;
			break;
		default:
			pos = 0;
			break;
		}
	}

	return services;
}

static void pcie_device_init(struct pci_dev *parent, struct pcie_device *dev, 
	int port_type, int service_type, int irq, int irq_mode)
{
	struct device *device;

	dev->port = parent;
	dev->interrupt_mode = irq_mode;
	dev->irq = irq;
	dev->id.vendor = parent->vendor;
	dev->id.device = parent->device;
	dev->id.port_type = port_type;
	dev->id.service_type = (1 << service_type);

	/* Initialize generic device interface */
	device = &dev->device;
	memset(device, 0, sizeof(struct device));
	device->bus = &pcie_port_bus_type;
	device->driver = NULL;
	device->driver_data = NULL;
	device->release = release_pcie_device;	/* callback to free pcie dev */
	snprintf(device->bus_id, sizeof(device->bus_id), "%s:pcie%02x",
		 pci_name(parent), get_descriptor_id(port_type, service_type));
	device->parent = &parent->dev;
}

static struct pcie_device* alloc_pcie_device(struct pci_dev *parent,
	int port_type, int service_type, int irq, int irq_mode)
{
	struct pcie_device *device;

	device = kzalloc(sizeof(struct pcie_device), GFP_KERNEL);
	if (!device)
		return NULL;

	pcie_device_init(parent, device, port_type, service_type, irq,irq_mode);
	dev_printk(KERN_DEBUG, &device->device, "allocate port service\n");
	return device;
}

int pcie_port_device_probe(struct pci_dev *dev)
{
	int pos, type;
	u16 reg;

	if (!(pos = pci_find_capability(dev, PCI_CAP_ID_EXP)))
		return -ENODEV;

	pci_read_config_word(dev, pos + PCIE_CAPABILITIES_REG, &reg);
	type = (reg >> 4) & PORT_TYPE_MASK;
	if (	type == PCIE_RC_PORT || type == PCIE_SW_UPSTREAM_PORT ||
		type == PCIE_SW_DOWNSTREAM_PORT )
		return 0;

	return -ENODEV;
}

int pcie_port_device_register(struct pci_dev *dev)
{
	struct pcie_port_device_ext *p_ext;
	int status, type, capabilities, irq_mode, i;
	int vectors[PCIE_PORT_DEVICE_MAXSERVICES];
	u16 reg16;

	/* Allocate port device extension */
	if (!(p_ext = kmalloc(sizeof(struct pcie_port_device_ext), GFP_KERNEL)))
		return -ENOMEM;

	pci_set_drvdata(dev, p_ext);

	/* Get port type */
	pci_read_config_word(dev,
		pci_find_capability(dev, PCI_CAP_ID_EXP) +
		PCIE_CAPABILITIES_REG, &reg16);
	type = (reg16 >> 4) & PORT_TYPE_MASK;

	/* Now get port services */
	capabilities = get_port_device_capability(dev);
	irq_mode = assign_interrupt_mode(dev, vectors, capabilities);
	p_ext->interrupt_mode = irq_mode;

	/* Allocate child services if any */
	for (i = 0; i < PCIE_PORT_DEVICE_MAXSERVICES; i++) {
		struct pcie_device *child;

		if (capabilities & (1 << i)) {
			child = alloc_pcie_device(
				dev, 		/* parent */
				type,		/* port type */
				i,		/* service type */
				vectors[i],	/* irq */
				irq_mode	/* interrupt mode */);
			if (child) {
				status = device_register(&child->device);
				if (status) {
					kfree(child);
					continue;
				}
				get_device(&child->device);
			}
		}
	}
	return 0;
}

#ifdef CONFIG_PM
static int suspend_iter(struct device *dev, void *data)
{
	struct pcie_port_service_driver *service_driver;
	pm_message_t state = * (pm_message_t *) data;

 	if ((dev->bus == &pcie_port_bus_type) &&
 	    (dev->driver)) {
 		service_driver = to_service_driver(dev->driver);
 		if (service_driver->suspend)
 			service_driver->suspend(to_pcie_device(dev), state);
  	}
	return 0;
}

int pcie_port_device_suspend(struct pci_dev *dev, pm_message_t state)
{
	return device_for_each_child(&dev->dev, &state, suspend_iter);
}

static int resume_iter(struct device *dev, void *data)
{
	struct pcie_port_service_driver *service_driver;

	if ((dev->bus == &pcie_port_bus_type) &&
	    (dev->driver)) {
		service_driver = to_service_driver(dev->driver);
		if (service_driver->resume)
			service_driver->resume(to_pcie_device(dev));
	}
	return 0;
}

int pcie_port_device_resume(struct pci_dev *dev)
{
	return device_for_each_child(&dev->dev, NULL, resume_iter);
}
#endif

static int remove_iter(struct device *dev, void *data)
{
	struct pcie_port_service_driver *service_driver;

	if (dev->bus == &pcie_port_bus_type) {
		if (dev->driver) {
			service_driver = to_service_driver(dev->driver);
			if (service_driver->remove)
				service_driver->remove(to_pcie_device(dev));
		}
		*(unsigned long*)data = (unsigned long)dev;
		return 1;
	}
	return 0;
}

void pcie_port_device_remove(struct pci_dev *dev)
{
	struct device *device;
	unsigned long device_addr;
	int interrupt_mode = PCIE_PORT_INTx_MODE;
	int status;

	do {
		status = device_for_each_child(&dev->dev, &device_addr, remove_iter);
		if (status) {
			device = (struct device*)device_addr;
			interrupt_mode = (to_pcie_device(device))->interrupt_mode;
			put_device(device);
			device_unregister(device);
		}
	} while (status);
	/* Switch to INTx by default if MSI enabled */
	if (interrupt_mode == PCIE_PORT_MSIX_MODE)
		pci_disable_msix(dev);
	else if (interrupt_mode == PCIE_PORT_MSI_MODE)
		pci_disable_msi(dev);
}

int pcie_port_bus_register(void)
{
	return bus_register(&pcie_port_bus_type);
}

void pcie_port_bus_unregister(void)
{
	bus_unregister(&pcie_port_bus_type);
}

int pcie_port_service_register(struct pcie_port_service_driver *new)
{
	new->driver.name = (char *)new->name;
	new->driver.bus = &pcie_port_bus_type;
	new->driver.probe = pcie_port_probe_service;
	new->driver.remove = pcie_port_remove_service;
	new->driver.shutdown = pcie_port_shutdown_service;
	new->driver.suspend = pcie_port_suspend_service;
	new->driver.resume = pcie_port_resume_service;

	return driver_register(&new->driver);
}

void pcie_port_service_unregister(struct pcie_port_service_driver *new)
{
	driver_unregister(&new->driver);
}

EXPORT_SYMBOL(pcie_port_service_register);
EXPORT_SYMBOL(pcie_port_service_unregister);
