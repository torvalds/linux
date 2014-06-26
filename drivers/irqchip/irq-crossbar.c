/*
 *  drivers/irqchip/irq-crossbar.c
 *
 *  Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com
 *  Author: Sricharan R <r.sricharan@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/irqchip/irq-crossbar.h>

#define IRQ_FREE	-1
#define IRQ_RESERVED	-2
#define IRQ_SKIP	-3
#define GIC_IRQ_START	32

/**
 * struct crossbar_device - crossbar device description
 * @int_max: maximum number of supported interrupts
 * @safe_map: safe default value to initialize the crossbar
 * @irq_map: array of interrupts to crossbar number mapping
 * @crossbar_base: crossbar base address
 * @register_offsets: offsets for each irq number
 * @write: register write function pointer
 */
struct crossbar_device {
	uint int_max;
	uint safe_map;
	uint *irq_map;
	void __iomem *crossbar_base;
	int *register_offsets;
	void (*write)(int, int);
};

static struct crossbar_device *cb;

static inline void crossbar_writel(int irq_no, int cb_no)
{
	writel(cb_no, cb->crossbar_base + cb->register_offsets[irq_no]);
}

static inline void crossbar_writew(int irq_no, int cb_no)
{
	writew(cb_no, cb->crossbar_base + cb->register_offsets[irq_no]);
}

static inline void crossbar_writeb(int irq_no, int cb_no)
{
	writeb(cb_no, cb->crossbar_base + cb->register_offsets[irq_no]);
}

static inline int get_prev_map_irq(int cb_no)
{
	int i;

	for (i = cb->int_max - 1; i >= 0; i--)
		if (cb->irq_map[i] == cb_no)
			return i;

	return -ENODEV;
}

static inline int allocate_free_irq(int cb_no)
{
	int i;

	for (i = cb->int_max - 1; i >= 0; i--) {
		if (cb->irq_map[i] == IRQ_FREE) {
			cb->irq_map[i] = cb_no;
			return i;
		}
	}

	return -ENODEV;
}

static int crossbar_domain_map(struct irq_domain *d, unsigned int irq,
			       irq_hw_number_t hw)
{
	cb->write(hw - GIC_IRQ_START, cb->irq_map[hw - GIC_IRQ_START]);
	return 0;
}

static void crossbar_domain_unmap(struct irq_domain *d, unsigned int irq)
{
	irq_hw_number_t hw = irq_get_irq_data(irq)->hwirq;

	if (hw > GIC_IRQ_START) {
		cb->irq_map[hw - GIC_IRQ_START] = IRQ_FREE;
		cb->write(hw - GIC_IRQ_START, cb->safe_map);
	}
}

static int crossbar_domain_xlate(struct irq_domain *d,
				 struct device_node *controller,
				 const u32 *intspec, unsigned int intsize,
				 unsigned long *out_hwirq,
				 unsigned int *out_type)
{
	int ret;

	ret = get_prev_map_irq(intspec[1]);
	if (ret >= 0)
		goto found;

	ret = allocate_free_irq(intspec[1]);

	if (ret < 0)
		return ret;

found:
	*out_hwirq = ret + GIC_IRQ_START;
	return 0;
}

static const struct irq_domain_ops routable_irq_domain_ops = {
	.map = crossbar_domain_map,
	.unmap = crossbar_domain_unmap,
	.xlate = crossbar_domain_xlate
};

static int __init crossbar_of_init(struct device_node *node)
{
	int i, size, max = 0, reserved = 0, entry;
	const __be32 *irqsr;
	int ret = -ENOMEM;

	cb = kzalloc(sizeof(*cb), GFP_KERNEL);

	if (!cb)
		return ret;

	cb->crossbar_base = of_iomap(node, 0);
	if (!cb->crossbar_base)
		goto err1;

	of_property_read_u32(node, "ti,max-irqs", &max);
	if (!max) {
		pr_err("missing 'ti,max-irqs' property\n");
		ret = -EINVAL;
		goto err2;
	}
	cb->irq_map = kcalloc(max, sizeof(int), GFP_KERNEL);
	if (!cb->irq_map)
		goto err2;

	cb->int_max = max;

	for (i = 0; i < max; i++)
		cb->irq_map[i] = IRQ_FREE;

	/* Get and mark reserved irqs */
	irqsr = of_get_property(node, "ti,irqs-reserved", &size);
	if (irqsr) {
		size /= sizeof(__be32);

		for (i = 0; i < size; i++) {
			of_property_read_u32_index(node,
						   "ti,irqs-reserved",
						   i, &entry);
			if (entry > max) {
				pr_err("Invalid reserved entry\n");
				ret = -EINVAL;
				goto err3;
			}
			cb->irq_map[entry] = IRQ_RESERVED;
		}
	}

	/* Skip irqs hardwired to bypass the crossbar */
	irqsr = of_get_property(node, "ti,irqs-skip", &size);
	if (irqsr) {
		size /= sizeof(__be32);

		for (i = 0; i < size; i++) {
			of_property_read_u32_index(node,
						   "ti,irqs-skip",
						   i, &entry);
			if (entry > max) {
				pr_err("Invalid skip entry\n");
				ret = -EINVAL;
				goto err3;
			}
			cb->irq_map[entry] = IRQ_SKIP;
		}
	}


	cb->register_offsets = kcalloc(max, sizeof(int), GFP_KERNEL);
	if (!cb->register_offsets)
		goto err3;

	of_property_read_u32(node, "ti,reg-size", &size);

	switch (size) {
	case 1:
		cb->write = crossbar_writeb;
		break;
	case 2:
		cb->write = crossbar_writew;
		break;
	case 4:
		cb->write = crossbar_writel;
		break;
	default:
		pr_err("Invalid reg-size property\n");
		ret = -EINVAL;
		goto err4;
		break;
	}

	/*
	 * Register offsets are not linear because of the
	 * reserved irqs. so find and store the offsets once.
	 */
	for (i = 0; i < max; i++) {
		if (cb->irq_map[i] == IRQ_RESERVED)
			continue;

		cb->register_offsets[i] = reserved;
		reserved += size;
	}

	of_property_read_u32(node, "ti,irqs-safe-map", &cb->safe_map);

	/* Initialize the crossbar with safe map to start with */
	for (i = 0; i < max; i++) {
		if (cb->irq_map[i] == IRQ_RESERVED ||
		    cb->irq_map[i] == IRQ_SKIP)
			continue;

		cb->write(i, cb->safe_map);
	}

	register_routable_domain_ops(&routable_irq_domain_ops);
	return 0;

err4:
	kfree(cb->register_offsets);
err3:
	kfree(cb->irq_map);
err2:
	iounmap(cb->crossbar_base);
err1:
	kfree(cb);
	return ret;
}

static const struct of_device_id crossbar_match[] __initconst = {
	{ .compatible = "ti,irq-crossbar" },
	{}
};

int __init irqcrossbar_init(void)
{
	struct device_node *np;
	np = of_find_matching_node(NULL, crossbar_match);
	if (!np)
		return -ENODEV;

	crossbar_of_init(np);
	return 0;
}
