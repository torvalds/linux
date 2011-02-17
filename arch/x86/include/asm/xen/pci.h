#ifndef _ASM_X86_XEN_PCI_H
#define _ASM_X86_XEN_PCI_H

#if defined(CONFIG_PCI_XEN)
extern int __init pci_xen_init(void);
extern int __init pci_xen_hvm_init(void);
#define pci_xen 1
#else
#define pci_xen 0
#define pci_xen_init (0)
static inline int pci_xen_hvm_init(void)
{
	return -1;
}
#endif
#if defined(CONFIG_XEN_DOM0)
void __init xen_setup_pirqs(void);
#else
static inline void __init xen_setup_pirqs(void)
{
}
#endif

#if defined(CONFIG_PCI_MSI)
#if defined(CONFIG_PCI_XEN)
/* The drivers/pci/xen-pcifront.c sets this structure to
 * its own functions.
 */
struct xen_pci_frontend_ops {
	int (*enable_msi)(struct pci_dev *dev, int vectors[]);
	void (*disable_msi)(struct pci_dev *dev);
	int (*enable_msix)(struct pci_dev *dev, int vectors[], int nvec);
	void (*disable_msix)(struct pci_dev *dev);
};

extern struct xen_pci_frontend_ops *xen_pci_frontend;

static inline int xen_pci_frontend_enable_msi(struct pci_dev *dev,
					      int vectors[])
{
	if (xen_pci_frontend && xen_pci_frontend->enable_msi)
		return xen_pci_frontend->enable_msi(dev, vectors);
	return -ENODEV;
}
static inline void xen_pci_frontend_disable_msi(struct pci_dev *dev)
{
	if (xen_pci_frontend && xen_pci_frontend->disable_msi)
			xen_pci_frontend->disable_msi(dev);
}
static inline int xen_pci_frontend_enable_msix(struct pci_dev *dev,
					       int vectors[], int nvec)
{
	if (xen_pci_frontend && xen_pci_frontend->enable_msix)
		return xen_pci_frontend->enable_msix(dev, vectors, nvec);
	return -ENODEV;
}
static inline void xen_pci_frontend_disable_msix(struct pci_dev *dev)
{
	if (xen_pci_frontend && xen_pci_frontend->disable_msix)
			xen_pci_frontend->disable_msix(dev);
}
#endif /* CONFIG_PCI_XEN */
#endif /* CONFIG_PCI_MSI */

#endif	/* _ASM_X86_XEN_PCI_H */
