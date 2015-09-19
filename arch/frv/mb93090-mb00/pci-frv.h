/*
 *	Low-Level PCI Access for FRV machines.
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 */

#include <asm/sections.h>

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

extern unsigned int __nongpreldata pci_probe;

/* pci-frv.c */

void pcibios_resource_survey(void);

/* pci-vdk.c */

extern struct pci_ops *__nongpreldata pci_root_ops;

/* pci-irq.c */
extern unsigned int pcibios_irq_mask;

void pcibios_irq_init(void);
void pcibios_fixup_irqs(void);
void pcibios_enable_irq(struct pci_dev *dev);
