#ifndef _POWERNV_H
#define _POWERNV_H

#ifdef CONFIG_SMP
extern void pnv_smp_init(void);
#else
static inline void pnv_smp_init(void) { }
#endif

struct pci_dev;

#ifdef CONFIG_PCI
extern void pnv_pci_init(void);
extern void pnv_pci_shutdown(void);
extern int pnv_pci_dma_set_mask(struct pci_dev *pdev, u64 dma_mask);
#else
static inline void pnv_pci_init(void) { }
static inline void pnv_pci_shutdown(void) { }

static inline int pnv_pci_dma_set_mask(struct pci_dev *pdev, u64 dma_mask)
{
	return -ENODEV;
}
#endif

extern void pnv_lpc_init(void);

bool cpu_core_split_required(void);

#endif /* _POWERNV_H */
