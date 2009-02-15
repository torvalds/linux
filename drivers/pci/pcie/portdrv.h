/*
 * File:	portdrv.h
 * Purpose:	PCI Express Port Bus Driver's Internal Data Structures
 *
 * Copyright (C) 2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#ifndef _PORTDRV_H_
#define _PORTDRV_H_

#include <linux/compiler.h>

#if !defined(PCI_CAP_ID_PME)
#define PCI_CAP_ID_PME			1
#endif

#if !defined(PCI_CAP_ID_EXP)
#define PCI_CAP_ID_EXP			0x10
#endif

#define PORT_TYPE_MASK			0xf
#define PORT_TO_SLOT_MASK		0x100
#define SLOT_HP_CAPABLE_MASK		0x40
#define PCIE_CAPABILITIES_REG		0x2
#define PCIE_SLOT_CAPABILITIES_REG	0x14
#define PCIE_PORT_DEVICE_MAXSERVICES	4
#define PCIE_PORT_MSI_VECTOR_MASK	0x1f
/*
 * According to the PCI Express Base Specification 2.0, the indices of the MSI-X
 * table entires used by port services must not exceed 31
 */
#define PCIE_PORT_MAX_MSIX_ENTRIES	32

#define get_descriptor_id(type, service) (((type - 4) << 4) | service)

extern struct bus_type pcie_port_bus_type;
extern int pcie_port_device_probe(struct pci_dev *dev);
extern int pcie_port_device_register(struct pci_dev *dev);
#ifdef CONFIG_PM
extern int pcie_port_device_suspend(struct device *dev);
extern int pcie_port_device_resume(struct device *dev);
#endif
extern void pcie_port_device_remove(struct pci_dev *dev);
extern int __must_check pcie_port_bus_register(void);
extern void pcie_port_bus_unregister(void);

#endif /* _PORTDRV_H_ */
