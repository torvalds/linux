/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Purpose:	PCI Express Port Bus Driver's Internal Data Structures
 *
 * Copyright (C) 2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#ifndef _PORTDRV_H_
#define _PORTDRV_H_

#include <linux/compiler.h>

extern bool pcie_ports_native;

/* Service Type */
#define PCIE_PORT_SERVICE_PME_SHIFT	0	/* Power Management Event */
#define PCIE_PORT_SERVICE_PME		(1 << PCIE_PORT_SERVICE_PME_SHIFT)
#define PCIE_PORT_SERVICE_AER_SHIFT	1	/* Advanced Error Reporting */
#define PCIE_PORT_SERVICE_AER		(1 << PCIE_PORT_SERVICE_AER_SHIFT)
#define PCIE_PORT_SERVICE_HP_SHIFT	2	/* Native Hotplug */
#define PCIE_PORT_SERVICE_HP		(1 << PCIE_PORT_SERVICE_HP_SHIFT)
#define PCIE_PORT_SERVICE_DPC_SHIFT	3	/* Downstream Port Containment */
#define PCIE_PORT_SERVICE_DPC		(1 << PCIE_PORT_SERVICE_DPC_SHIFT)

#define PCIE_PORT_DEVICE_MAXSERVICES   4

/* Port Type */
#define PCIE_ANY_PORT			(~0)

struct pcie_device {
	int		irq;	    /* Service IRQ/MSI/MSI-X Vector */
	struct pci_dev *port;	    /* Root/Upstream/Downstream Port */
	u32		service;    /* Port service this device represents */
	void		*priv_data; /* Service Private Data */
	struct device	device;     /* Generic Device Interface */
};
#define to_pcie_device(d) container_of(d, struct pcie_device, device)

static inline void set_service_data(struct pcie_device *dev, void *data)
{
	dev->priv_data = data;
}

static inline void *get_service_data(struct pcie_device *dev)
{
	return dev->priv_data;
}

struct pcie_port_service_driver {
	const char *name;
	int (*probe) (struct pcie_device *dev);
	void (*remove) (struct pcie_device *dev);
	int (*suspend) (struct pcie_device *dev);
	int (*resume) (struct pcie_device *dev);

	/* Device driver may resume normal operations */
	void (*error_resume)(struct pci_dev *dev);

	/* Link Reset Capability - AER service driver specific */
	pci_ers_result_t (*reset_link) (struct pci_dev *dev);

	int port_type;  /* Type of the port this driver can handle */
	u32 service;    /* Port service this device represents */

	struct device_driver driver;
};
#define to_service_driver(d) \
	container_of(d, struct pcie_port_service_driver, driver)

int pcie_port_service_register(struct pcie_port_service_driver *new);
void pcie_port_service_unregister(struct pcie_port_service_driver *new);

/*
 * The PCIe Capability Interrupt Message Number (PCIe r3.1, sec 7.8.2) must
 * be one of the first 32 MSI-X entries.  Per PCI r3.0, sec 6.8.3.1, MSI
 * supports a maximum of 32 vectors per function.
 */
#define PCIE_PORT_MAX_MSI_ENTRIES	32

#define get_descriptor_id(type, service) (((type - 4) << 8) | service)

extern struct bus_type pcie_port_bus_type;
int pcie_port_device_register(struct pci_dev *dev);
#ifdef CONFIG_PM
int pcie_port_device_suspend(struct device *dev);
int pcie_port_device_resume(struct device *dev);
#endif
void pcie_port_device_remove(struct pci_dev *dev);
int __must_check pcie_port_bus_register(void);
void pcie_port_bus_unregister(void);

struct pci_dev;

#ifdef CONFIG_PCIE_PME
extern bool pcie_pme_msi_disabled;

static inline void pcie_pme_disable_msi(void)
{
	pcie_pme_msi_disabled = true;
}

static inline bool pcie_pme_no_msi(void)
{
	return pcie_pme_msi_disabled;
}

void pcie_pme_interrupt_enable(struct pci_dev *dev, bool enable);
#else /* !CONFIG_PCIE_PME */
static inline void pcie_pme_disable_msi(void) {}
static inline bool pcie_pme_no_msi(void) { return false; }
static inline void pcie_pme_interrupt_enable(struct pci_dev *dev, bool en) {}
#endif /* !CONFIG_PCIE_PME */

#endif /* _PORTDRV_H_ */
