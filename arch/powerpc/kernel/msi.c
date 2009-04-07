/*
 * Copyright 2006-2007, Michael Ellerman, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/msi.h>
#include <linux/pci.h>

#include <asm/machdep.h>

int arch_msi_check_device(struct pci_dev* dev, int nvec, int type)
{
	if (!ppc_md.setup_msi_irqs || !ppc_md.teardown_msi_irqs) {
		pr_debug("msi: Platform doesn't provide MSI callbacks.\n");
		return -ENOSYS;
	}

	/* PowerPC doesn't support multiple MSI yet */
	if (type == PCI_CAP_ID_MSI && nvec > 1)
		return 1;

	if (ppc_md.msi_check_device) {
		pr_debug("msi: Using platform check routine.\n");
		return ppc_md.msi_check_device(dev, nvec, type);
	}

        return 0;
}

int arch_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	return ppc_md.setup_msi_irqs(dev, nvec, type);
}

void arch_teardown_msi_irqs(struct pci_dev *dev)
{
	ppc_md.teardown_msi_irqs(dev);
}
