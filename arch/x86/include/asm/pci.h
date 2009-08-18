#ifndef _ASM_X86_PCI_H
#define _ASM_X86_PCI_H

#include <linux/mm.h> /* for struct page */
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/scatterlist.h>
#include <asm/io.h>

#ifdef __KERNEL__

struct pci_sysdata {
	int		domain;		/* PCI domain */
	int		node;		/* NUMA node */
#ifdef CONFIG_X86_64
	void		*iommu;		/* IOMMU private data */
#endif
};

extern int pci_routeirq;
extern int noioapicquirk;
extern int noioapicreroute;

/* scan a bus after allocating a pci_sysdata for it */
extern struct pci_bus *pci_scan_bus_on_node(int busno, struct pci_ops *ops,
					    int node);
extern struct pci_bus *pci_scan_bus_with_sysdata(int busno);

static inline int pci_domain_nr(struct pci_bus *bus)
{
	struct pci_sysdata *sd = bus->sysdata;
	return sd->domain;
}

static inline int pci_proc_domain(struct pci_bus *bus)
{
	return pci_domain_nr(bus);
}


/* Can be used to override the logic in pci_scan_bus for skipping
   already-configured bus numbers - to be used for buggy BIOSes
   or architectures with incomplete PCI setup by the loader */

#ifdef CONFIG_PCI
extern unsigned int pcibios_assign_all_busses(void);
#else
#define pcibios_assign_all_busses()	0
#endif
#define pcibios_scan_all_fns(a, b)	0

extern unsigned long pci_mem_start;
#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		(pci_mem_start)

#define PCIBIOS_MIN_CARDBUS_IO	0x4000

void pcibios_config_init(void);
struct pci_bus *pcibios_scan_root(int bus);

void pcibios_set_master(struct pci_dev *dev);
void pcibios_penalize_isa_irq(int irq, int active);
struct irq_routing_table *pcibios_get_irq_routing_table(void);
int pcibios_set_irq_routing(struct pci_dev *dev, int pin, int irq);


#define HAVE_PCI_MMAP
extern int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			       enum pci_mmap_state mmap_state,
			       int write_combine);


#ifdef CONFIG_PCI
extern void early_quirks(void);
static inline void pci_dma_burst_advice(struct pci_dev *pdev,
					enum pci_dma_burst_strategy *strat,
					unsigned long *strategy_parameter)
{
	*strat = PCI_DMA_BURST_INFINITY;
	*strategy_parameter = ~0UL;
}
#else
static inline void early_quirks(void) { }
#endif

extern void pci_iommu_alloc(void);

/* MSI arch hook */
#define arch_setup_msi_irqs arch_setup_msi_irqs

#define PCI_DMA_BUS_IS_PHYS (dma_ops->is_phys)

#if defined(CONFIG_X86_64) || defined(CONFIG_DMA_API_DEBUG)

#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)       \
	        dma_addr_t ADDR_NAME;
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME)         \
	        __u32 LEN_NAME;
#define pci_unmap_addr(PTR, ADDR_NAME)                  \
	        ((PTR)->ADDR_NAME)
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL)         \
	        (((PTR)->ADDR_NAME) = (VAL))
#define pci_unmap_len(PTR, LEN_NAME)                    \
	        ((PTR)->LEN_NAME)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL)           \
	        (((PTR)->LEN_NAME) = (VAL))

#else

#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)       dma_addr_t ADDR_NAME[0];
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME) unsigned LEN_NAME[0];
#define pci_unmap_addr(PTR, ADDR_NAME)  sizeof((PTR)->ADDR_NAME)
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL) \
	        do { break; } while (pci_unmap_addr(PTR, ADDR_NAME))
#define pci_unmap_len(PTR, LEN_NAME)            sizeof((PTR)->LEN_NAME)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL) \
	        do { break; } while (pci_unmap_len(PTR, LEN_NAME))

#endif

#endif  /* __KERNEL__ */

#ifdef CONFIG_X86_64
#include "pci_64.h"
#endif

/* implement the pci_ DMA API in terms of the generic device dma_ one */
#include <asm-generic/pci-dma-compat.h>

/* generic pci stuff */
#include <asm-generic/pci.h>
#define PCIBIOS_MAX_MEM_32 0xffffffff

#ifdef CONFIG_NUMA
/* Returns the node based on pci bus */
static inline int __pcibus_to_node(const struct pci_bus *bus)
{
	const struct pci_sysdata *sd = bus->sysdata;

	return sd->node;
}

static inline const struct cpumask *
cpumask_of_pcibus(const struct pci_bus *bus)
{
	return cpumask_of_node(__pcibus_to_node(bus));
}
#endif

#endif /* _ASM_X86_PCI_H */
