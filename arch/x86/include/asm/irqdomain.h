#ifndef _ASM_IRQDOMAIN_H
#define _ASM_IRQDOMAIN_H

#include <linux/irqdomain.h>

enum ioapic_domain_type {
	IOAPIC_DOMAIN_INVALID,
	IOAPIC_DOMAIN_LEGACY,
	IOAPIC_DOMAIN_STRICT,
	IOAPIC_DOMAIN_DYNAMIC,
};

struct device_node;
struct irq_data;

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
extern void mp_irqdomain_activate(struct irq_domain *domain,
				  struct irq_data *irq_data);
extern void mp_irqdomain_deactivate(struct irq_domain *domain,
				    struct irq_data *irq_data);
extern int mp_irqdomain_ioapic_idx(struct irq_domain *domain);

#endif
