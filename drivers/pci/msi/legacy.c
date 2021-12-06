// SPDX-License-Identifier: GPL-2.0
/*
 * PCI Message Signaled Interrupt (MSI).
 *
 * Legacy architecture specific setup and teardown mechanism.
 */
#include <linux/msi.h>
#include <linux/pci.h>

/* Arch hooks */
int __weak arch_setup_msi_irq(struct pci_dev *dev, struct msi_desc *desc)
{
	return -EINVAL;
}

void __weak arch_teardown_msi_irq(unsigned int irq)
{
}

int __weak arch_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	struct msi_desc *desc;
	int ret;

	/*
	 * If an architecture wants to support multiple MSI, it needs to
	 * override arch_setup_msi_irqs()
	 */
	if (type == PCI_CAP_ID_MSI && nvec > 1)
		return 1;

	for_each_pci_msi_entry(desc, dev) {
		ret = arch_setup_msi_irq(dev, desc);
		if (ret)
			return ret < 0 ? ret : -ENOSPC;
	}

	return 0;
}

void __weak arch_teardown_msi_irqs(struct pci_dev *dev)
{
	struct msi_desc *desc;
	int i;

	for_each_pci_msi_entry(desc, dev) {
		if (desc->irq) {
			for (i = 0; i < desc->nvec_used; i++)
				arch_teardown_msi_irq(desc->irq + i);
		}
	}
}
