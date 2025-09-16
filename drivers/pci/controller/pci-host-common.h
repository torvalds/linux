/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common library for PCI host controller drivers
 *
 * Copyright (C) 2014 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#ifndef _PCI_HOST_COMMON_H
#define _PCI_HOST_COMMON_H

struct pci_ecam_ops;

int pci_host_common_probe(struct platform_device *pdev);
int pci_host_common_init(struct platform_device *pdev,
			 const struct pci_ecam_ops *ops);
void pci_host_common_remove(struct platform_device *pdev);

struct pci_config_window *pci_host_common_ecam_create(struct device *dev,
	struct pci_host_bridge *bridge, const struct pci_ecam_ops *ops);
#endif
