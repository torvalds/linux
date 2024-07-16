// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for IDT/Renesas 79RC3243x Interrupt Controller.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define IDT_PIC_NR_IRQS		32

#define IDT_PIC_IRQ_PEND		0x00
#define IDT_PIC_IRQ_MASK		0x08

struct idt_pic_data {
	void __iomem *base;
	struct irq_domain *irq_domain;
	struct irq_chip_generic *gc;
};

static void idt_irq_dispatch(struct irq_desc *desc)
{
	struct idt_pic_data *idtpic = irq_desc_get_handler_data(desc);
	struct irq_chip *host_chip = irq_desc_get_chip(desc);
	u32 pending, hwirq;

	chained_irq_enter(host_chip, desc);

	pending = irq_reg_readl(idtpic->gc, IDT_PIC_IRQ_PEND);
	pending &= ~idtpic->gc->mask_cache;
	while (pending) {
		hwirq = __fls(pending);
		generic_handle_domain_irq(idtpic->irq_domain, hwirq);
		pending &= ~(1 << hwirq);
	}

	chained_irq_exit(host_chip, desc);
}

static int idt_pic_init(struct device_node *of_node, struct device_node *parent)
{
	struct irq_domain *domain;
	struct idt_pic_data *idtpic;
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	unsigned int parent_irq;
	int ret = 0;

	idtpic = kzalloc(sizeof(*idtpic), GFP_KERNEL);
	if (!idtpic) {
		ret = -ENOMEM;
		goto out_err;
	}

	parent_irq = irq_of_parse_and_map(of_node, 0);
	if (!parent_irq) {
		pr_err("Failed to map parent IRQ!\n");
		ret = -EINVAL;
		goto out_free;
	}

	idtpic->base = of_iomap(of_node, 0);
	if (!idtpic->base) {
		pr_err("Failed to map base address!\n");
		ret = -ENOMEM;
		goto out_unmap_irq;
	}

	domain = irq_domain_add_linear(of_node, IDT_PIC_NR_IRQS,
				       &irq_generic_chip_ops, NULL);
	if (!domain) {
		pr_err("Failed to add irqdomain!\n");
		ret = -ENOMEM;
		goto out_iounmap;
	}
	idtpic->irq_domain = domain;

	ret = irq_alloc_domain_generic_chips(domain, 32, 1, "IDTPIC",
					     handle_level_irq, 0,
					     IRQ_NOPROBE | IRQ_LEVEL, 0);
	if (ret)
		goto out_domain_remove;

	gc = irq_get_domain_generic_chip(domain, 0);
	gc->reg_base = idtpic->base;
	gc->private = idtpic;

	ct = gc->chip_types;
	ct->regs.mask = IDT_PIC_IRQ_MASK;
	ct->chip.irq_mask = irq_gc_mask_set_bit;
	ct->chip.irq_unmask = irq_gc_mask_clr_bit;
	idtpic->gc = gc;

	/* Mask interrupts. */
	writel(0xffffffff, idtpic->base + IDT_PIC_IRQ_MASK);
	gc->mask_cache = 0xffffffff;

	irq_set_chained_handler_and_data(parent_irq,
					 idt_irq_dispatch, idtpic);

	return 0;

out_domain_remove:
	irq_domain_remove(domain);
out_iounmap:
	iounmap(idtpic->base);
out_unmap_irq:
	irq_dispose_mapping(parent_irq);
out_free:
	kfree(idtpic);
out_err:
	pr_err("Failed to initialize! (errno = %d)\n", ret);
	return ret;
}

IRQCHIP_DECLARE(idt_pic, "idt,32434-pic", idt_pic_init);
