/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	Mantis PCI bridge driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

*/

#ifndef __MANTIS_PCI_H
#define __MANTIS_PCI_H

extern int mantis_pci_init(struct mantis_pci *mantis);
extern void mantis_pci_exit(struct mantis_pci *mantis);

#endif /* __MANTIS_PCI_H */
