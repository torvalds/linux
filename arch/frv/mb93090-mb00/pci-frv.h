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

#define PCI_PROBE_BIOS		0x0001
#define PCI_PROBE_CONF1		0x0002
#define PCI_PROBE_CONF2		0x0004
#define PCI_NO_SORT		0x0100
#define PCI_BIOS_SORT		0x0200
#define PCI_NO_CHECKS		0x0400
#define PCI_ASSIGN_ROMS		0x1000
#define PCI_BIOS_IRQ_SCAN	0x2000
#define PCI_ASSIGN_ALL_BUSSES	0x4000

extern unsigned int __nongpreldata pci_probe;

/* pci-frv.c */

extern unsigned int pcibios_max_latency;

void pcibios_resource_survey(void);
int pcibios_enable_resources(struct pci_dev *, int);

/* pci-vdk.c */

extern int __nongpreldata pcibios_last_bus;
extern struct pci_bus *__nongpreldata pci_root_bus;
extern struct pci_ops *__nongpreldata pci_root_ops;

/* pci-irq.c */
extern unsigned int pcibios_irq_mask;

void pcibios_irq_init(void);
void pcibios_fixup_irqs(void);
void pcibios_enable_irq(struct pci_dev *dev);
