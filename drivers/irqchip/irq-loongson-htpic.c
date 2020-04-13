// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2020, Jiaxun Yang <jiaxun.yang@flygoat.com>
 *  Loongson HTPIC IRQ support
 */

#include <linux/init.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/syscore_ops.h>

#include <asm/i8259.h>

#define HTPIC_MAX_PARENT_IRQ	4
#define HTINT_NUM_VECTORS	8
#define HTINT_EN_OFF		0x20

struct loongson_htpic {
	void __iomem *base;
	struct irq_domain *domain;
};

static struct loongson_htpic *htpic;

static void htpic_irq_dispatch(struct irq_desc *desc)
{
	struct loongson_htpic *priv = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	uint32_t pending;

	chained_irq_enter(chip, desc);
	pending = readl(priv->base);
	/* Ack all IRQs at once, otherwise IRQ flood might happen */
	writel(pending, priv->base);

	if (!pending)
		spurious_interrupt();

	while (pending) {
		int bit = __ffs(pending);

		if (unlikely(bit > 15)) {
			spurious_interrupt();
			break;
		}

		generic_handle_irq(irq_linear_revmap(priv->domain, bit));
		pending &= ~BIT(bit);
	}
	chained_irq_exit(chip, desc);
}

static void htpic_reg_init(void)
{
	int i;

	for (i = 0; i < HTINT_NUM_VECTORS; i++) {
		uint32_t val;

		/* Disable all HT Vectors */
		writel(0x0, htpic->base + HTINT_EN_OFF + i * 0x4);
		val = readl(htpic->base + i * 0x4);
		/* Ack all possible pending IRQs */
		writel(GENMASK(31, 0), htpic->base + i * 0x4);
	}

	/* Enable 16 vectors for PIC */
	writel(0xffff, htpic->base + HTINT_EN_OFF);
}

static void htpic_resume(void)
{
	htpic_reg_init();
}

struct syscore_ops htpic_syscore_ops = {
	.resume		= htpic_resume,
};

int __init htpic_of_init(struct device_node *node, struct device_node *parent)
{
	unsigned int parent_irq[4];
	int i, err;
	int num_parents = 0;

	if (htpic) {
		pr_err("loongson-htpic: Only one HTPIC is allowed in the system\n");
		return -ENODEV;
	}

	htpic = kzalloc(sizeof(*htpic), GFP_KERNEL);
	if (!htpic) {
		err = -ENOMEM;
		goto out_free;
	}

	htpic->base = of_iomap(node, 0);
	if (!htpic->base) {
		err = -ENODEV;
		goto out_free;
	}

	htpic->domain = __init_i8259_irqs(node);
	if (!htpic->domain) {
		pr_err("loongson-htpic: Failed to initialize i8259 IRQs\n");
		err = -ENOMEM;
		goto out_iounmap;
	}

	/* Interrupt may come from any of the 4 interrupt line */
	for (i = 0; i < HTPIC_MAX_PARENT_IRQ; i++) {
		parent_irq[i] = irq_of_parse_and_map(node, i);
		if (parent_irq[i] <= 0)
			break;

		num_parents++;
	}

	if (!num_parents) {
		pr_err("loongson-htpic: Failed to get parent irqs\n");
		err = -ENODEV;
		goto out_remove_domain;
	}

	htpic_reg_init();

	for (i = 0; i < num_parents; i++) {
		irq_set_chained_handler_and_data(parent_irq[i],
						htpic_irq_dispatch, htpic);
	}

	register_syscore_ops(&htpic_syscore_ops);

	return 0;

out_remove_domain:
	irq_domain_remove(htpic->domain);
out_iounmap:
	iounmap(htpic->base);
out_free:
	kfree(htpic);
	return err;
}

IRQCHIP_DECLARE(loongson_htpic, "loongson,htpic-1.0", htpic_of_init);
