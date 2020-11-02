/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IRQDOMAIN_H
#define _ASM_IRQDOMAIN_H

#include <linux/irqdomain.h>
#include <asm/hw_irq.h>

#ifdef CONFIG_X86_LOCAL_APIC
enum {
	/* Allocate contiguous CPU vectors */
	X86_IRQ_ALLOC_CONTIGUOUS_VECTORS		= 0x1,
	X86_IRQ_ALLOC_LEGACY				= 0x2,
};

extern struct irq_domain *x86_vector_domain;

extern void init_irq_alloc_info(struct irq_alloc_info *info,
				const struct cpumask *mask);
extern void copy_irq_alloc_info(struct irq_alloc_info *dst,
				struct irq_alloc_info *src);
#endif /* CONFIG_X86_LOCAL_APIC */

#ifdef CONFIG_X86_IO_APIC
struct device_node;
struct irq_data;

enum ioapic_domain_type {
	IOAPIC_DOMAIN_INVALID,
	IOAPIC_DOMAIN_LEGACY,
	IOAPIC_DOMAIN_STRICT,
	IOAPIC_DOMAIN_DYNAMIC,
};

struct ioapic_domain_cfg {
	enum ioapic_domain_type		type;
	const struct irq_domain_ops	*ops;
	struct device_node		*dev;
};

extern const struct irq_domain_ops mp_ioapic_irqdomain_ops;

extern int mp_irqdomain_alloc(struct irq_domain *domain, unsigned int virq,
			      unsigned int nr_irqs, void *arg);
extern void mp_irqdomain_free(struct irq_domain *domain, unsigned int virq,
			      unsigned int nr_irqs);
extern int mp_irqdomain_activate(struct irq_domain *domain,
				 struct irq_data *irq_data, bool reserve);
extern void mp_irqdomain_deactivate(struct irq_domain *domain,
				    struct irq_data *irq_data);
extern int mp_irqdomain_ioapic_idx(struct irq_domain *domain);
#endif /* CONFIG_X86_IO_APIC */

#ifdef CONFIG_PCI_MSI
void x86_create_pci_msi_domain(void);
struct irq_domain *native_create_pci_msi_domain(void);
extern struct irq_domain *x86_pci_msi_default_domain;
#else
static inline void x86_create_pci_msi_domain(void) { }
#define native_create_pci_msi_domain	NULL
#define x86_pci_msi_default_domain	NULL
#endif

#endif
