#ifndef __IRQ_CONTROLLER__
#define __IRQ_CONTROLLER__

struct irq_domain {
	int (*xlate)(struct irq_domain *h, const u32 *intspec, u32 intsize,
			u32 *out_hwirq, u32 *out_type);
	void *priv;
	struct device_node *controller;
	struct list_head l;
};

#endif
