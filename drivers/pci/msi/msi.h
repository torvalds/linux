/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/pci.h>
#include <linux/msi.h>

#define msix_table_size(flags)	((flags & PCI_MSIX_FLAGS_QSIZE) + 1)

extern int pci_msi_setup_msi_irqs(struct pci_dev *dev, int nvec, int type);
extern void pci_msi_teardown_msi_irqs(struct pci_dev *dev);

#ifdef CONFIG_PCI_MSI_ARCH_FALLBACKS
extern int pci_msi_legacy_setup_msi_irqs(struct pci_dev *dev, int nvec, int type);
extern void pci_msi_legacy_teardown_msi_irqs(struct pci_dev *dev);
#else
static inline int pci_msi_legacy_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	WARN_ON_ONCE(1);
	return -ENODEV;
}

static inline void pci_msi_legacy_teardown_msi_irqs(struct pci_dev *dev)
{
	WARN_ON_ONCE(1);
}
#endif

/*
 * PCI 2.3 does not specify mask bits for each MSI interrupt.  Attempting to
 * mask all MSI interrupts by clearing the MSI enable bit does not work
 * reliably as devices without an INTx disable bit will then generate a
 * level IRQ which will never be cleared.
 */
static inline __attribute_const__ u32 msi_multi_mask(struct msi_desc *desc)
{
	/* Don't shift by >= width of type */
	if (desc->pci.msi_attrib.multi_cap >= 5)
		return 0xffffffff;
	return (1 << (1 << desc->pci.msi_attrib.multi_cap)) - 1;
}
