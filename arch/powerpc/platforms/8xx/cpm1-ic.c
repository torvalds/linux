// SPDX-License-Identifier: GPL-2.0
/*
 * Interrupt controller for the
 * Communication Processor Module.
 * Copyright (c) 1997 Dan error_act (dmalek@jlc.net)
 */
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/platform_device.h>
#include <asm/cpm1.h>

struct cpm_pic_data {
	cpic8xx_t __iomem *reg;
	struct irq_domain *host;
};

static void cpm_mask_irq(struct irq_data *d)
{
	struct cpm_pic_data *data = irq_data_get_irq_chip_data(d);
	unsigned int cpm_vec = (unsigned int)irqd_to_hwirq(d);

	clrbits32(&data->reg->cpic_cimr, (1 << cpm_vec));
}

static void cpm_unmask_irq(struct irq_data *d)
{
	struct cpm_pic_data *data = irq_data_get_irq_chip_data(d);
	unsigned int cpm_vec = (unsigned int)irqd_to_hwirq(d);

	setbits32(&data->reg->cpic_cimr, (1 << cpm_vec));
}

static void cpm_end_irq(struct irq_data *d)
{
	struct cpm_pic_data *data = irq_data_get_irq_chip_data(d);
	unsigned int cpm_vec = (unsigned int)irqd_to_hwirq(d);

	out_be32(&data->reg->cpic_cisr, (1 << cpm_vec));
}

static struct irq_chip cpm_pic = {
	.name = "CPM PIC",
	.irq_mask = cpm_mask_irq,
	.irq_unmask = cpm_unmask_irq,
	.irq_eoi = cpm_end_irq,
};

static int cpm_get_irq(struct irq_desc *desc)
{
	struct cpm_pic_data *data = irq_desc_get_handler_data(desc);
	int cpm_vec;

	/*
	 * Get the vector by setting the ACK bit and then reading
	 * the register.
	 */
	out_be16(&data->reg->cpic_civr, 1);
	cpm_vec = in_be16(&data->reg->cpic_civr);
	cpm_vec >>= 11;

	return irq_find_mapping(data->host, cpm_vec);
}

static void cpm_cascade(struct irq_desc *desc)
{
	generic_handle_irq(cpm_get_irq(desc));
}

static int cpm_pic_host_map(struct irq_domain *h, unsigned int virq,
			    irq_hw_number_t hw)
{
	irq_set_chip_data(virq, h->host_data);
	irq_set_status_flags(virq, IRQ_LEVEL);
	irq_set_chip_and_handler(virq, &cpm_pic, handle_fasteoi_irq);
	return 0;
}

static const struct irq_domain_ops cpm_pic_host_ops = {
	.map = cpm_pic_host_map,
};

static int cpm_pic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	int irq;
	struct cpm_pic_data *data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->reg = devm_ioremap(dev, res->start, resource_size(res));
	if (!data->reg)
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	/* Initialize the CPM interrupt controller. */
	out_be32(&data->reg->cpic_cicr,
		 (CICR_SCD_SCC4 | CICR_SCC_SCC3 | CICR_SCB_SCC2 | CICR_SCA_SCC1) |
		 ((virq_to_hw(irq) / 2) << 13) | CICR_HP_MASK);

	out_be32(&data->reg->cpic_cimr, 0);

	data->host = irq_domain_create_linear(dev_fwnode(dev), 64, &cpm_pic_host_ops, data);
	if (!data->host)
		return -ENODEV;

	irq_set_handler_data(irq, data);
	irq_set_chained_handler(irq, cpm_cascade);

	setbits32(&data->reg->cpic_cicr, CICR_IEN);

	return 0;
}

static const struct of_device_id cpm_pic_match[] = {
	{
		.compatible = "fsl,cpm1-pic",
	}, {
		.type = "cpm-pic",
		.compatible = "CPM",
	}, {},
};

static struct platform_driver cpm_pic_driver = {
	.driver	= {
		.name		= "cpm-pic",
		.of_match_table	= cpm_pic_match,
	},
	.probe	= cpm_pic_probe,
};

static int __init cpm_pic_init(void)
{
	return platform_driver_register(&cpm_pic_driver);
}
arch_initcall(cpm_pic_init);

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

static int cpm_error_probe(struct platform_device *pdev)
{
	int irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	return request_irq(irq, cpm_error_interrupt, IRQF_NO_THREAD, "error", NULL);
}

static const struct of_device_id cpm_error_ids[] = {
	{ .compatible = "fsl,cpm1" },
	{ .type = "cpm" },
	{},
};

static struct platform_driver cpm_error_driver = {
	.driver	= {
		.name		= "cpm-error",
		.of_match_table	= cpm_error_ids,
	},
	.probe	= cpm_error_probe,
};

static int __init cpm_error_init(void)
{
	return platform_driver_register(&cpm_error_driver);
}
subsys_initcall(cpm_error_init);
