/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/pci.h>
#include <linux/msi.h>

#define msix_table_size(flags)	((flags & PCI_MSIX_FLAGS_QSIZE) + 1)

int pci_msi_setup_msi_irqs(struct pci_dev *dev, int nvec, int type);
void pci_msi_teardown_msi_irqs(struct pci_dev *dev);

/* Mask/unmask helpers */
void pci_msi_update_mask(struct msi_desc *desc, u32 clear, u32 set);

static inline void pci_msi_mask(struct msi_desc *desc, u32 mask)
{
	pci_msi_update_mask(desc, 0, mask);
}

static inline void pci_msi_unmask(struct msi_desc *desc, u32 mask)
{
	pci_msi_update_mask(desc, mask, 0);
}

static inline void __iomem *pci_msix_desc_addr(struct msi_desc *desc)
{
	return desc->pci.mask_base + desc->msi_index * PCI_MSIX_ENTRY_SIZE;
}

/*
 * This internal function does not flush PCI writes to the device.  All
 * users must ensure that they read from the device before either assuming
 * that the device state is up to date, or returning out of this file.
 * It does not affect the msi_desc::msix_ctrl cache either. Use with care!
 */
static inline void pci_msix_write_vector_ctrl(struct msi_desc *desc, u32 ctrl)
{
	void __iomem *desc_addr = pci_msix_desc_addr(desc);

	if (desc->pci.msi_attrib.can_mask)
		writel(ctrl, desc_addr + PCI_MSIX_ENTRY_VECTOR_CTRL);
}

static inline void pci_msix_mask(struct msi_desc *desc)
{
	desc->pci.msix_ctrl |= PCI_MSIX_ENTRY_CTRL_MASKBIT;
	pci_msix_write_vector_ctrl(desc, desc->pci.msix_ctrl);
	/* Flush write to device */
	readl(desc->pci.mask_base);
}

static inline void pci_msix_unmask(struct msi_desc *desc)
{
	desc->pci.msix_ctrl &= ~PCI_MSIX_ENTRY_CTRL_MASKBIT;
	pci_msix_write_vector_ctrl(desc, desc->pci.msix_ctrl);
}

static inline void __pci_msi_mask_desc(struct msi_desc *desc, u32 mask)
{
	if (desc->pci.msi_attrib.is_msix)
		pci_msix_mask(desc);
	else
		pci_msi_mask(desc, mask);
}

static inline void __pci_msi_unmask_desc(struct msi_desc *desc, u32 mask)
{
	if (desc->pci.msi_attrib.is_msix)
		pci_msix_unmask(desc);
	else
		pci_msi_unmask(desc, mask);
}

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

void msix_prepare_msi_desc(struct pci_dev *dev, struct msi_desc *desc);

/* Subsystem variables */
extern bool pci_msi_enable;

/* MSI internal functions invoked from the public APIs */
void pci_msi_shutdown(struct pci_dev *dev);
void pci_msix_shutdown(struct pci_dev *dev);
void pci_free_msi_irqs(struct pci_dev *dev);
int __pci_enable_msi_range(struct pci_dev *dev, int minvec, int maxvec, struct irq_affinity *affd);
int __pci_enable_msix_range(struct pci_dev *dev, struct msix_entry *entries, int minvec,
			    int maxvec,  struct irq_affinity *affd, int flags);
void __pci_restore_msi_state(struct pci_dev *dev);
void __pci_restore_msix_state(struct pci_dev *dev);

/* irq_domain related functionality */

enum support_mode {
	ALLOW_LEGACY,
	DENY_LEGACY,
};

bool pci_msi_domain_supports(struct pci_dev *dev, unsigned int feature_mask, enum support_mode mode);
bool pci_setup_msi_device_domain(struct pci_dev *pdev, unsigned int hwsize);
bool pci_setup_msix_device_domain(struct pci_dev *pdev, unsigned int hwsize);

/* Legacy (!IRQDOMAIN) fallbacks */

#ifdef CONFIG_PCI_MSI_ARCH_FALLBACKS
int pci_msi_legacy_setup_msi_irqs(struct pci_dev *dev, int nvec, int type);
void pci_msi_legacy_teardown_msi_irqs(struct pci_dev *dev);
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
