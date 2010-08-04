/*
 * drivers/pci/pcie/pme/pcie_pme.h
 *
 * PCI Express Root Port PME signaling support
 *
 * Copyright (C) 2009 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 */

#ifndef _PCIE_PME_H_
#define _PCIE_PME_H_

struct pcie_device;

#ifdef CONFIG_ACPI
extern int pcie_pme_acpi_setup(struct pcie_device *srv);

static inline int pcie_pme_platform_notify(struct pcie_device *srv)
{
	return pcie_pme_acpi_setup(srv);
}
#else /* !CONFIG_ACPI */
static inline int pcie_pme_platform_notify(struct pcie_device *srv)
{
	return 0;
}
#endif /* !CONFIG_ACPI */

#endif
