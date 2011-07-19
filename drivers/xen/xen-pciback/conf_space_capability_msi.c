/*
 * PCI Backend -- Configuration overlay for MSI capability
 */
#include <linux/pci.h>
#include <linux/slab.h>
#include "conf_space.h"
#include "conf_space_capability.h"
#include <xen/interface/io/pciif.h>
#include <xen/events.h>
#include "pciback.h"

int pciback_enable_msi(struct pciback_device *pdev,
		struct pci_dev *dev, struct xen_pci_op *op)
{
	struct pciback_dev_data *dev_data;
	int otherend = pdev->xdev->otherend_id;
	int status;

	status = pci_enable_msi(dev);

	if (status) {
		printk(KERN_ERR "error enable msi for guest %x status %x\n",
			otherend, status);
		op->value = 0;
		return XEN_PCI_ERR_op_failed;
	}

	/* The value the guest needs is actually the IDT vector, not the
	 * the local domain's IRQ number. */

	op->value = dev->irq ? xen_pirq_from_irq(dev->irq) : 0;
	dev_data = pci_get_drvdata(dev);
	if (dev_data)
		dev_data->ack_intr = 0;
	return 0;
}

int pciback_disable_msi(struct pciback_device *pdev,
		struct pci_dev *dev, struct xen_pci_op *op)
{
	struct pciback_dev_data *dev_data;
	pci_disable_msi(dev);

	op->value = dev->irq ? xen_pirq_from_irq(dev->irq) : 0;
	dev_data = pci_get_drvdata(dev);
	if (dev_data)
		dev_data->ack_intr = 1;
	return 0;
}

int pciback_enable_msix(struct pciback_device *pdev,
		struct pci_dev *dev, struct xen_pci_op *op)
{
	struct pciback_dev_data *dev_data;
	int i, result;
	struct msix_entry *entries;

	if (op->value > SH_INFO_MAX_VEC)
		return -EINVAL;

	entries = kmalloc(op->value * sizeof(*entries), GFP_KERNEL);
	if (entries == NULL)
		return -ENOMEM;

	for (i = 0; i < op->value; i++) {
		entries[i].entry = op->msix_entries[i].entry;
		entries[i].vector = op->msix_entries[i].vector;
	}

	result = pci_enable_msix(dev, entries, op->value);

	if (result == 0) {
		for (i = 0; i < op->value; i++) {
			op->msix_entries[i].entry = entries[i].entry;
			if (entries[i].vector)
				op->msix_entries[i].vector =
					xen_pirq_from_irq(entries[i].vector);
		}
	} else {
		printk(KERN_WARNING "pciback: %s: failed to enable MSI-X: err %d!\n",
			pci_name(dev), result);
	}
	kfree(entries);

	op->value = result;
	dev_data = pci_get_drvdata(dev);
	if (dev_data)
		dev_data->ack_intr = 0;

	return result;
}

int pciback_disable_msix(struct pciback_device *pdev,
		struct pci_dev *dev, struct xen_pci_op *op)
{
	struct pciback_dev_data *dev_data;

	pci_disable_msix(dev);

	/*
	 * SR-IOV devices (which don't have any legacy IRQ) have
	 * an undefined IRQ value of zero.
	 */
	op->value = dev->irq ? xen_pirq_from_irq(dev->irq) : 0;
	dev_data = pci_get_drvdata(dev);
	if (dev_data)
		dev_data->ack_intr = 1;

	return 0;
}

