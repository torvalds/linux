/*
 * comedi_pci.h
 * header file for Comedi PCI drivers
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _COMEDI_PCI_H
#define _COMEDI_PCI_H

#include <linux/pci.h>

#include "comedidev.h"

/*
 * PCI Vendor IDs not in <linux/pci_ids.h>
 */
#define PCI_VENDOR_ID_KOLTER		0x1001
#define PCI_VENDOR_ID_ICP		0x104c
#define PCI_VENDOR_ID_DT		0x1116
#define PCI_VENDOR_ID_IOTECH		0x1616
#define PCI_VENDOR_ID_CONTEC		0x1221
#define PCI_VENDOR_ID_RTD		0x1435
#define PCI_VENDOR_ID_HUMUSOFT		0x186c

struct pci_dev *comedi_to_pci_dev(struct comedi_device *);

int comedi_pci_enable(struct comedi_device *);
void comedi_pci_disable(struct comedi_device *);
void comedi_pci_detach(struct comedi_device *);

int comedi_pci_auto_config(struct pci_dev *, struct comedi_driver *,
			   unsigned long context);
void comedi_pci_auto_unconfig(struct pci_dev *);

int comedi_pci_driver_register(struct comedi_driver *, struct pci_driver *);
void comedi_pci_driver_unregister(struct comedi_driver *, struct pci_driver *);

/**
 * module_comedi_pci_driver() - Helper macro for registering a comedi PCI driver
 * @__comedi_driver: comedi_driver struct
 * @__pci_driver: pci_driver struct
 *
 * Helper macro for comedi PCI drivers which do not do anything special
 * in module init/exit. This eliminates a lot of boilerplate. Each
 * module may only use this macro once, and calling it replaces
 * module_init() and module_exit()
 */
#define module_comedi_pci_driver(__comedi_driver, __pci_driver) \
	module_driver(__comedi_driver, comedi_pci_driver_register, \
			comedi_pci_driver_unregister, &(__pci_driver))

#endif /* _COMEDI_PCI_H */
