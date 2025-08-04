/*
 * J-Core SoC AIC driver
 *
 * Copyright (C) 2015-2016 Smart Energy Instruments, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/irq.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define JCORE_AIC_MAX_HWIRQ	127
#define JCORE_AIC1_MIN_HWIRQ	16
#define JCORE_AIC2_MIN_HWIRQ	64

#define JCORE_AIC1_INTPRI_REG	8

static struct irq_chip jcore_aic;

/*
 * The J-Core AIC1 and AIC2 are cpu-local interrupt controllers and do
 * not distinguish or use distinct irq number ranges for per-cpu event
 * interrupts (timer, IPI). Since information to determine whether a
 * particular irq number should be treated as per-cpu is not available
 * at mapping time, we use a wrapper handler function which chooses
 * the right handler at runtime based on whether IRQF_PERCPU was used
 * when requesting the irq.
 */

static void handle_jcore_irq(struct irq_desc *desc)
{
	if (irqd_is_per_cpu(irq_desc_get_irq_data(desc)))
		handle_percpu_devid_irq(desc);
	else
		handle_simple_irq(desc);
}

static int jcore_aic_irqdomain_map(struct irq_domain *d, unsigned int irq,
				   irq_hw_number_t hwirq)
{
	struct irq_chip *aic = d->host_data;

	irq_set_chip_and_handler(irq, aic, handle_jcore_irq);

	return 0;
}

static const struct irq_domain_ops jcore_aic_irqdomain_ops = {
	.map = jcore_aic_irqdomain_map,
	.xlate = irq_domain_xlate_onecell,
};

static void noop(struct irq_data *data)
{
}

static int __init aic_irq_of_init(struct device_node *node,
				  struct device_node *parent)
{
	unsigned min_irq = JCORE_AIC2_MIN_HWIRQ;
	unsigned dom_sz = JCORE_AIC_MAX_HWIRQ+1;
	struct irq_domain *domain;
	int ret;

	pr_info("Initializing J-Core AIC\n");

	/* AIC1 needs priority initialization to receive interrupts. */
	if (of_device_is_compatible(node, "jcore,aic1")) {
		unsigned cpu;

		for_each_present_cpu(cpu) {
			void __iomem *base = of_iomap(node, cpu);

			if (!base) {
				pr_err("Unable to map AIC for cpu %u\n", cpu);
				return -ENOMEM;
			}
			__raw_writel(0xffffffff, base + JCORE_AIC1_INTPRI_REG);
			iounmap(base);
		}
		min_irq = JCORE_AIC1_MIN_HWIRQ;
	}

	/*
	 * The irq chip framework requires either mask/unmask or enable/disable
	 * function pointers to be provided, but the hardware does not have any
	 * such mechanism; the only interrupt masking is at the cpu level and
	 * it affects all interrupts. We provide dummy mask/unmask. The hardware
	 * handles all interrupt control and clears pending status when the cpu
	 * accepts the interrupt.
	 */
	jcore_aic.irq_mask = noop;
	jcore_aic.irq_unmask = noop;
	jcore_aic.name = "AIC";

	ret = irq_alloc_descs(-1, min_irq, dom_sz - min_irq,
			      of_node_to_nid(node));

	if (ret < 0)
		return ret;

	domain = irq_domain_create_legacy(of_fwnode_handle(node), dom_sz - min_irq, min_irq,
					  min_irq, &jcore_aic_irqdomain_ops, &jcore_aic);
	if (!domain)
		return -ENOMEM;

	return 0;
}

IRQCHIP_DECLARE(jcore_aic2, "jcore,aic2", aic_irq_of_init);
IRQCHIP_DECLARE(jcore_aic1, "jcore,aic1", aic_irq_of_init);
