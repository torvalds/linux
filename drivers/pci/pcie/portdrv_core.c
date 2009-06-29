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

#include "../pci.h"
#include "portdrv.h"

/**
 * release_pcie_device - free PCI Express port service device structure
 * @dev: Port service device to release
 *
 * Invoked automatically when device is being removed in response to
 * device_unregister(dev).  Release all resources being claimed.
 */
static void release_pcie_device(struct device *dev)
{
	kfree(to_pcie_device(dev));			
}

/**
 * pcie_port_msix_add_entry - add entry to given array of MSI-X entries
 * @entries: Array of MSI-X entries
 * @new_entry: Index of the entry to add to the array
 * @nr_entries: Number of entries aleady in the array
 *
 * Return value: Position of the added entry in the array
 */
static int pcie_port_msix_add_entry(
	struct msix_entry *entries, int new_entry, int nr_entries)
{
	int j;

	for (j = 0; j < nr_entries; j++)
		if (entries[j].entry == new_entry)
			return j;

	entries[j].entry = new_entry;
	return j;
}

/**
 * pcie_port_enable_msix - try to set up MSI-X as interrupt mode for given port
 * @dev: PCI Express port to handle
 * @vectors: Array of interrupt vectors to populate
 * @mask: Bitmask of port capabilities returned by get_port_device_capability()
 *
 * Return value: 0 on success, error code on failure
 */
static int pcie_port_enable_msix(struct pci_dev *dev, int *vectors, int mask)
{
	struct msix_entry *msix_entries;
	int idx[PCIE_PORT_DEVICE_MAXSERVICES];
	int nr_entries, status, pos, i, nvec;
	u16 reg16;
	u32 reg32;

	nr_entries = pci_msix_table_size(dev);
	if (!nr_entries)
		return -EINVAL;
	if (nr_entries > PCIE_PORT_MAX_MSIX_ENTRIES)
		nr_entries = PCIE_PORT_MAX_MSIX_ENTRIES;

	msix_entries = kzalloc(sizeof(*msix_entries) * nr_entries, GFP_KERNEL);
	if (!msix_entries)
		return -ENOMEM;

	/*
	 * Allocate as many entries as the port wants, so that we can check
	 * which of them will be useful.  Moreover, if nr_entries is correctly
	 * equal to the number of entries this port actually uses, we'll happily
	 * go through without any tricks.
	 */
	for (i = 0; i < nr_entries; i++)
		msix_entries[i].entry = i;

	status = pci_enable_msix(dev, msix_entries, nr_entries);
	if (status)
		goto Exit;

	for (i = 0; i < PCIE_PORT_DEVICE_MAXSERVICES; i++)
		idx[i] = -1;
	status = -EIO;
	nvec = 0;

	if (mask & (PCIE_PORT_SERVICE_PME | PCIE_PORT_SERVICE_HP)) {
		int entry;

		/*
		 * The code below follows the PCI Express Base Specification 2.0
		 * stating in Section 6.1.6 that "PME and Hot-Plug Event
		 * interrupts (when both are implemented) always share the same
		 * MSI or MSI-X vector, as indicated by the Interrupt Message
		 * Number field in the PCI Express Capabilities register", where
		 * according to Section 7.8.2 of the specification "For MSI-X,
		 * the value in this field indicates which MSI-X Table entry is
		 * used to generate the interrupt message."
		 */
		pos = pci_find_capability(dev, PCI_CAP_ID_EXP);
		pci_read_config_word(dev, pos + PCIE_CAPABILITIES_REG, &reg16);
		entry = (reg16 >> 9) & PCIE_PORT_MSI_VECTOR_MASK;
		if (entry >= nr_entries)
			goto Error;

		i = pcie_port_msix_add_entry(msix_entries, entry, nvec);
		if (i == nvec)
			nvec++;

		idx[PCIE_PORT_SERVICE_PME_SHIFT] = i;
		idx[PCIE_PORT_SERVICE_HP_SHIFT] = i;
	}

	if (mask & PCIE_PORT_SERVICE_AER) {
		int entry;

		/*
		 * The code below follows Section 7.10.10 of the PCI Express
		 * Base Specification 2.0 stating that bits 31-27 of the Root
		 * Error Status Register contain a value indicating which of the
		 * MSI/MSI-X vectors assigned to the port is going to be used
		 * for AER, where "For MSI-X, the value in this register
		 * indicates which MSI-X Table entry is used to generate the
		 * interrupt message."
		 */
		pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
		pci_read_config_dword(dev, pos + PCI_ERR_ROOT_STATUS, &reg32);
		entry = reg32 >> 27;
		if (entry >= nr_entries)
			goto Error;

		i = pcie_port_msix_add_entry(msix_entries, entry, nvec);
		if (i == nvec)
			nvec++;

		idx[PCIE_PORT_SERVICE_AER_SHIFT] = i;
	}

	/*
	 * If nvec is equal to the allocated number of entries, we can just use
	 * what we have.  Otherwise, the port has some extra entries not for the
	 * services we know and we need to work around that.
	 */
	if (nvec == nr_entries) {
		status = 0;
	} else {
		/* Drop the temporary MSI-X setup */
		pci_disable_msix(dev);

		/* Now allocate the MSI-X vectors for real */
		status = pci_enable_msix(dev, msix_entries, nvec);
		if (status)
			goto Exit;
	}

	for (i = 0; i < PCIE_PORT_DEVICE_MAXSERVICES; i++)
		vectors[i] = idx[i] >= 0 ? msix_entries[idx[i]].vector : -1;

 Exit:
	kfree(msix_entries);
	return status;

 Error:
	pci_disable_msix(dev);
	goto Exit;
}

/**
 * assign_interrupt_mode - choose interrupt mode for PCI Express port services
 *                         (INTx, MSI-X, MSI) and set up vectors
 * @dev: PCI Express port to handle
 * @vectors: Array of interrupt vectors to populate
 * @mask: Bitmask of port capabilities returned by get_port_device_capability()
 *
 * Return value: Interrupt mode associated with the port
 */
static int assign_interrupt_mode(struct pci_dev *dev, int *vectors, int mask)
{
	struct pcie_port_data *port_data = pci_get_drvdata(dev);
	int irq, interrupt_mode = PCIE_PORT_NO_IRQ;
	int i;

	/* Check MSI quirk */
	if (port_data->port_type == PCIE_RC_PORT && pcie_mch_quirk)
		goto Fallback;

	/* Try to use MSI-X if supported */
	if (!pcie_port_enable_msix(dev, vectors, mask))
		return PCIE_PORT_MSIX_MODE;

	/* We're not going to use MSI-X, so try MSI and fall back to INTx */
	if (!pci_enable_msi(dev))
		interrupt_mode = PCIE_PORT_MSI_MODE;

 Fallback:
	if (interrupt_mode == PCIE_PORT_NO_IRQ && dev->pin)
		interrupt_mode = PCIE_PORT_INTx_MODE;

	irq = interrupt_mode != PCIE_PORT_NO_IRQ ? dev->irq : -1;
	for (i = 0; i < PCIE_PORT_DEVICE_MAXSERVICES; i++)
		vectors[i] = irq;

	vectors[PCIE_PORT_SERVICE_VC_SHIFT] = -1;

	return interrupt_mode;
}

/**
 * get_port_device_capability - discover capabilities of a PCI Express port
 * @dev: PCI Express port to examine
 *
 * The capabilities are read from the port's PCI Express configuration registers
 * as described in PCI Express Base Specification 1.0a sections 7.8.2, 7.8.9 and
 * 7.9 - 7.11.
 *
 * Return value: Bitmask of discovered port capabilities
 */
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
	/* AER capable */
	if (pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR))
		services |= PCIE_PORT_SERVICE_AER;
	/* VC support */
	if (pci_find_ext_capability(dev, PCI_EXT_CAP_ID_VC))
		services |= PCIE_PORT_SERVICE_VC;

	return services;
}

/**
 * pcie_device_init - initialize PCI Express port service device
 * @dev: Port service device to initialize
 * @parent: PCI Express port to associate the service device with
 * @port_type: Type of the port
 * @service_type: Type of service to associate with the service device
 * @irq: Interrupt vector to associate with the service device
 */
static void pcie_device_init(struct pci_dev *parent, struct pcie_device *dev, 
	int service_type, int irq)
{
	struct pcie_port_data *port_data = pci_get_drvdata(parent);
	struct device *device;
	int port_type = port_data->port_type;

	dev->port = parent;
	dev->irq = irq;
	dev->service = service_type;

	/* Initialize generic device interface */
	device = &dev->device;
	memset(device, 0, sizeof(struct device));
	device->bus = &pcie_port_bus_type;
	device->driver = NULL;
	dev_set_drvdata(device, NULL);
	device->release = release_pcie_device;	/* callback to free pcie dev */
	dev_set_name(device, "%s:pcie%02x",
		 pci_name(parent), get_descriptor_id(port_type, service_type));
	device->parent = &parent->dev;
}

/**
 * alloc_pcie_device - allocate PCI Express port service device structure
 * @parent: PCI Express port to associate the service device with
 * @port_type: Type of the port
 * @service_type: Type of service to associate with the service device
 * @irq: Interrupt vector to associate with the service device
 */
static struct pcie_device* alloc_pcie_device(struct pci_dev *parent,
	int service_type, int irq)
{
	struct pcie_device *device;

	device = kzalloc(sizeof(struct pcie_device), GFP_KERNEL);
	if (!device)
		return NULL;

	pcie_device_init(parent, device, service_type, irq);
	return device;
}

/**
 * pcie_port_device_probe - check if device is a PCI Express port
 * @dev: Device to check
 */
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

/**
 * pcie_port_device_register - register PCI Express port
 * @dev: PCI Express port to register
 *
 * Allocate the port extension structure and register services associated with
 * the port.
 */
int pcie_port_device_register(struct pci_dev *dev)
{
	struct pcie_port_data *port_data;
	int status, capabilities, irq_mode, i, nr_serv;
	int vectors[PCIE_PORT_DEVICE_MAXSERVICES];
	u16 reg16;

	port_data = kzalloc(sizeof(*port_data), GFP_KERNEL);
	if (!port_data)
		return -ENOMEM;
	pci_set_drvdata(dev, port_data);

	/* Get port type */
	pci_read_config_word(dev,
		pci_find_capability(dev, PCI_CAP_ID_EXP) +
		PCIE_CAPABILITIES_REG, &reg16);
	port_data->port_type = (reg16 >> 4) & PORT_TYPE_MASK;

	capabilities = get_port_device_capability(dev);
	/* Root ports are capable of generating PME too */
	if (port_data->port_type == PCIE_RC_PORT)
		capabilities |= PCIE_PORT_SERVICE_PME;

	irq_mode = assign_interrupt_mode(dev, vectors, capabilities);
	if (irq_mode == PCIE_PORT_NO_IRQ) {
		/*
		 * Don't use service devices that require interrupts if there is
		 * no way to generate them.
		 */
		if (!(capabilities & PCIE_PORT_SERVICE_VC)) {
			status = -ENODEV;
			goto Error;
		}
		capabilities = PCIE_PORT_SERVICE_VC;
	}
	port_data->port_irq_mode = irq_mode;

	status = pci_enable_device(dev);
	if (status)
		goto Error;
	pci_set_master(dev);

	/* Allocate child services if any */
	for (i = 0, nr_serv = 0; i < PCIE_PORT_DEVICE_MAXSERVICES; i++) {
		struct pcie_device *child;
		int service = 1 << i;

		if (!(capabilities & service))
			continue;

		child = alloc_pcie_device(dev, service, vectors[i]);
		if (!child)
			continue;

		status = device_register(&child->device);
		if (status) {
			kfree(child);
			continue;
		}

		get_device(&child->device);
		nr_serv++;
	}
	if (!nr_serv) {
		pci_disable_device(dev);
		status = -ENODEV;
		goto Error;
	}

	return 0;

 Error:
	kfree(port_data);
	return status;
}

#ifdef CONFIG_PM
static int suspend_iter(struct device *dev, void *data)
{
	struct pcie_port_service_driver *service_driver;

 	if ((dev->bus == &pcie_port_bus_type) &&
 	    (dev->driver)) {
 		service_driver = to_service_driver(dev->driver);
 		if (service_driver->suspend)
 			service_driver->suspend(to_pcie_device(dev));
  	}
	return 0;
}

/**
 * pcie_port_device_suspend - suspend port services associated with a PCIe port
 * @dev: PCI Express port to handle
 */
int pcie_port_device_suspend(struct device *dev)
{
	return device_for_each_child(dev, NULL, suspend_iter);
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

/**
 * pcie_port_device_suspend - resume port services associated with a PCIe port
 * @dev: PCI Express port to handle
 */
int pcie_port_device_resume(struct device *dev)
{
	return device_for_each_child(dev, NULL, resume_iter);
}
#endif /* PM */

static int remove_iter(struct device *dev, void *data)
{
	if (dev->bus == &pcie_port_bus_type) {
		put_device(dev);
		device_unregister(dev);
	}
	return 0;
}

/**
 * pcie_port_device_remove - unregister PCI Express port service devices
 * @dev: PCI Express port the service devices to unregister are associated with
 *
 * Remove PCI Express port service devices associated with given port and
 * disable MSI-X or MSI for the port.
 */
void pcie_port_device_remove(struct pci_dev *dev)
{
	struct pcie_port_data *port_data = pci_get_drvdata(dev);

	device_for_each_child(&dev->dev, NULL, remove_iter);
	pci_disable_device(dev);

	switch (port_data->port_irq_mode) {
	case PCIE_PORT_MSIX_MODE:
		pci_disable_msix(dev);
		break;
	case PCIE_PORT_MSI_MODE:
		pci_disable_msi(dev);
		break;
	}

	kfree(port_data);
}

/**
 * pcie_port_probe_service - probe driver for given PCI Express port service
 * @dev: PCI Express port service device to probe against
 *
 * If PCI Express port service driver is registered with
 * pcie_port_service_register(), this function will be called by the driver core
 * whenever match is found between the driver and a port service device.
 */
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
	status = driver->probe(pciedev);
	if (!status) {
		dev_printk(KERN_DEBUG, dev, "service driver %s loaded\n",
			driver->name);
		get_device(dev);
	}
	return status;
}

/**
 * pcie_port_remove_service - detach driver from given PCI Express port service
 * @dev: PCI Express port service device to handle
 *
 * If PCI Express port service driver is registered with
 * pcie_port_service_register(), this function will be called by the driver core
 * when device_unregister() is called for the port service device associated
 * with the driver.
 */
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

/**
 * pcie_port_shutdown_service - shut down given PCI Express port service
 * @dev: PCI Express port service device to handle
 *
 * If PCI Express port service driver is registered with
 * pcie_port_service_register(), this function will be called by the driver core
 * when device_shutdown() is called for the port service device associated
 * with the driver.
 */
static void pcie_port_shutdown_service(struct device *dev) {}

/**
 * pcie_port_service_register - register PCI Express port service driver
 * @new: PCI Express port service driver to register
 */
int pcie_port_service_register(struct pcie_port_service_driver *new)
{
	new->driver.name = (char *)new->name;
	new->driver.bus = &pcie_port_bus_type;
	new->driver.probe = pcie_port_probe_service;
	new->driver.remove = pcie_port_remove_service;
	new->driver.shutdown = pcie_port_shutdown_service;

	return driver_register(&new->driver);
}

/**
 * pcie_port_service_unregister - unregister PCI Express port service driver
 * @drv: PCI Express port service driver to unregister
 */
void pcie_port_service_unregister(struct pcie_port_service_driver *drv)
{
	driver_unregister(&drv->driver);
}

EXPORT_SYMBOL(pcie_port_service_register);
EXPORT_SYMBOL(pcie_port_service_unregister);
