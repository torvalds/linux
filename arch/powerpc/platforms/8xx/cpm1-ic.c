// SPDX-License-Identifier: GPL-2.0
/*
 * Interrupt controller for the
 * Communication Processor Module.
 * Copyright (c) 1997 Dan error_act (dmalek@jlc.net)
 */
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>
#include <asm/cpm1.h>

static cpic8xx_t __iomem *cpic_reg;

static struct irq_domain *cpm_pic_host;

static void cpm_mask_irq(struct irq_data *d)
{
	unsigned int cpm_vec = (unsigned int)irqd_to_hwirq(d);

	clrbits32(&cpic_reg->cpic_cimr, (1 << cpm_vec));
}

static void cpm_unmask_irq(struct irq_data *d)
{
	unsigned int cpm_vec = (unsigned int)irqd_to_hwirq(d);

	setbits32(&cpic_reg->cpic_cimr, (1 << cpm_vec));
}

static void cpm_end_irq(struct irq_data *d)
{
	unsigned int cpm_vec = (unsigned int)irqd_to_hwirq(d);

	out_be32(&cpic_reg->cpic_cisr, (1 << cpm_vec));
}

static struct irq_chip cpm_pic = {
	.name = "CPM PIC",
	.irq_mask = cpm_mask_irq,
	.irq_unmask = cpm_unmask_irq,
	.irq_eoi = cpm_end_irq,
};

int cpm_get_irq(void)
{
	int cpm_vec;

	/*
	 * Get the vector by setting the ACK bit and then reading
	 * the register.
	 */
	out_be16(&cpic_reg->cpic_civr, 1);
	cpm_vec = in_be16(&cpic_reg->cpic_civr);
	cpm_vec >>= 11;

	return irq_linear_revmap(cpm_pic_host, cpm_vec);
}

static int cpm_pic_host_map(struct irq_domain *h, unsigned int virq,
			    irq_hw_number_t hw)
{
	pr_debug("cpm_pic_host_map(%d, 0x%lx)\n", virq, hw);

	irq_set_status_flags(virq, IRQ_LEVEL);
	irq_set_chip_and_handler(virq, &cpm_pic, handle_fasteoi_irq);
	return 0;
}

/*
 * The CPM can generate the error interrupt when there is a race condition
 * between generating and masking interrupts.  All we have to do is ACK it
 * and return.  This is a no-op function so we don't need any special
 * tests in the interrupt handler.
 */
static irqreturn_t cpm_error_interrupt(int irq, void *dev)
{
	return IRQ_HANDLED;
}

static const struct irq_domain_ops cpm_pic_host_ops = {
	.map = cpm_pic_host_map,
};

unsigned int __init cpm_pic_init(void)
{
	struct device_node *np = NULL;
	struct resource res;
	unsigned int sirq = 0, hwirq, eirq;
	int ret;

	pr_debug("cpm_pic_init\n");

	np = of_find_compatible_node(NULL, NULL, "fsl,cpm1-pic");
	if (np == NULL)
		np = of_find_compatible_node(NULL, "cpm-pic", "CPM");
	if (np == NULL) {
		printk(KERN_ERR "CPM PIC init: can not find cpm-pic node\n");
		return sirq;
	}

	ret = of_address_to_resource(np, 0, &res);
	if (ret)
		goto end;

	cpic_reg = ioremap(res.start, resource_size(&res));
	if (cpic_reg == NULL)
		goto end;

	sirq = irq_of_parse_and_map(np, 0);
	if (!sirq)
		goto end;

	/* Initialize the CPM interrupt controller. */
	hwirq = (unsigned int)virq_to_hw(sirq);
	out_be32(&cpic_reg->cpic_cicr,
	    (CICR_SCD_SCC4 | CICR_SCC_SCC3 | CICR_SCB_SCC2 | CICR_SCA_SCC1) |
		((hwirq/2) << 13) | CICR_HP_MASK);

	out_be32(&cpic_reg->cpic_cimr, 0);

	cpm_pic_host = irq_domain_add_linear(np, 64, &cpm_pic_host_ops, NULL);
	if (cpm_pic_host == NULL) {
		printk(KERN_ERR "CPM2 PIC: failed to allocate irq host!\n");
		sirq = 0;
		goto end;
	}

	/* Install our own error handler. */
	np = of_find_compatible_node(NULL, NULL, "fsl,cpm1");
	if (np == NULL)
		np = of_find_node_by_type(NULL, "cpm");
	if (np == NULL) {
		printk(KERN_ERR "CPM PIC init: can not find cpm node\n");
		goto end;
	}

	eirq = irq_of_parse_and_map(np, 0);
	if (!eirq)
		goto end;

	if (request_irq(eirq, cpm_error_interrupt, IRQF_NO_THREAD, "error",
			NULL))
		printk(KERN_ERR "Could not allocate CPM error IRQ!");

	setbits32(&cpic_reg->cpic_cicr, CICR_IEN);

end:
	of_node_put(np);
	return sirq;
}
