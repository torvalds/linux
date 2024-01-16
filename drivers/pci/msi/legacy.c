// SPDX-License-Identifier: GPL-2.0
/*
 * PCI Message Signaled Interrupt (MSI).
 *
 * Legacy architecture specific setup and teardown mechanism.
 */
#include "msi.h"

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

	msi_for_each_desc(desc, &dev->dev, MSI_DESC_NOTASSOCIATED) {
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

	msi_for_each_desc(desc, &dev->dev, MSI_DESC_ASSOCIATED) {
		for (i = 0; i < desc->nvec_used; i++)
			arch_teardown_msi_irq(desc->irq + i);
	}
}

static int pci_msi_setup_check_result(struct pci_dev *dev, int type, int ret)
{
	struct msi_desc *desc;
	int avail = 0;

	if (type != PCI_CAP_ID_MSIX || ret >= 0)
		return ret;

	/* Scan the MSI descriptors for successfully allocated ones. */
	msi_for_each_desc(desc, &dev->dev, MSI_DESC_ASSOCIATED)
		avail++;

	return avail ? avail : ret;
}

int pci_msi_legacy_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	int ret = arch_setup_msi_irqs(dev, nvec, type);

	ret = pci_msi_setup_check_result(dev, type, ret);
	if (!ret)
		ret = msi_device_populate_sysfs(&dev->dev);
	return ret;
}

void pci_msi_legacy_teardown_msi_irqs(struct pci_dev *dev)
{
	msi_device_destroy_sysfs(&dev->dev);
	arch_teardown_msi_irqs(dev);
}
