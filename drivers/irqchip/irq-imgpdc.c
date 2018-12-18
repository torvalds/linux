// SPDX-License-Identifier: GPL-2.0
/*
 * IMG PowerDown Controller (PDC)
 *
 * Copyright 2010-2013 Imagination Technologies Ltd.
 *
 * Exposes the syswake and PDC peripheral wake interrupts to the system.
 *
 */

#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

/* PDC interrupt register numbers */

#define PDC_IRQ_STATUS			0x310
#define PDC_IRQ_ENABLE			0x314
#define PDC_IRQ_CLEAR			0x318
#define PDC_IRQ_ROUTE			0x31c
#define PDC_SYS_WAKE_BASE		0x330
#define PDC_SYS_WAKE_STRIDE		0x8
#define PDC_SYS_WAKE_CONFIG_BASE	0x334
#define PDC_SYS_WAKE_CONFIG_STRIDE	0x8

/* PDC interrupt register field masks */

#define PDC_IRQ_SYS3			0x08
#define PDC_IRQ_SYS2			0x04
#define PDC_IRQ_SYS1			0x02
#define PDC_IRQ_SYS0			0x01
#define PDC_IRQ_ROUTE_WU_EN_SYS3	0x08000000
#define PDC_IRQ_ROUTE_WU_EN_SYS2	0x04000000
#define PDC_IRQ_ROUTE_WU_EN_SYS1	0x02000000
#define PDC_IRQ_ROUTE_WU_EN_SYS0	0x01000000
#define PDC_IRQ_ROUTE_WU_EN_WD		0x00040000
#define PDC_IRQ_ROUTE_WU_EN_IR		0x00020000
#define PDC_IRQ_ROUTE_WU_EN_RTC		0x00010000
#define PDC_IRQ_ROUTE_EXT_EN_SYS3	0x00000800
#define PDC_IRQ_ROUTE_EXT_EN_SYS2	0x00000400
#define PDC_IRQ_ROUTE_EXT_EN_SYS1	0x00000200
#define PDC_IRQ_ROUTE_EXT_EN_SYS0	0x00000100
#define PDC_IRQ_ROUTE_EXT_EN_WD		0x00000004
#define PDC_IRQ_ROUTE_EXT_EN_IR		0x00000002
#define PDC_IRQ_ROUTE_EXT_EN_RTC	0x00000001
#define PDC_SYS_WAKE_RESET		0x00000010
#define PDC_SYS_WAKE_INT_MODE		0x0000000e
#define PDC_SYS_WAKE_INT_MODE_SHIFT	1
#define PDC_SYS_WAKE_PIN_VAL		0x00000001

/* PDC interrupt constants */

#define PDC_SYS_WAKE_INT_LOW		0x0
#define PDC_SYS_WAKE_INT_HIGH		0x1
#define PDC_SYS_WAKE_INT_DOWN		0x2
#define PDC_SYS_WAKE_INT_UP		0x3
#define PDC_SYS_WAKE_INT_CHANGE		0x6
#define PDC_SYS_WAKE_INT_NONE		0x4

/**
 * struct pdc_intc_priv - private pdc interrupt data.
 * @nr_perips:		Number of peripheral interrupt signals.
 * @nr_syswakes:	Number of syswake signals.
 * @perip_irqs:		List of peripheral IRQ numbers handled.
 * @syswake_irq:	Shared PDC syswake IRQ number.
 * @domain:		IRQ domain for PDC peripheral and syswake IRQs.
 * @pdc_base:		Base of PDC registers.
 * @irq_route:		Cached version of PDC_IRQ_ROUTE register.
 * @lock:		Lock to protect the PDC syswake registers and the cached
 *			values of those registers in this struct.
 */
struct pdc_intc_priv {
	unsigned int		nr_perips;
	unsigned int		nr_syswakes;
	unsigned int		*perip_irqs;
	unsigned int		syswake_irq;
	struct irq_domain	*domain;
	void __iomem		*pdc_base;

	u32			irq_route;
	raw_spinlock_t		lock;
};

static void pdc_write(struct pdc_intc_priv *priv, unsigned int reg_offs,
		      unsigned int data)
{
	iowrite32(data, priv->pdc_base + reg_offs);
}

static unsigned int pdc_read(struct pdc_intc_priv *priv,
			     unsigned int reg_offs)
{
	return ioread32(priv->pdc_base + reg_offs);
}

/* Generic IRQ callbacks */

#define SYS0_HWIRQ	8

static unsigned int hwirq_is_syswake(irq_hw_number_t hw)
{
	return hw >= SYS0_HWIRQ;
}

static unsigned int hwirq_to_syswake(irq_hw_number_t hw)
{
	return hw - SYS0_HWIRQ;
}

static irq_hw_number_t syswake_to_hwirq(unsigned int syswake)
{
	return SYS0_HWIRQ + syswake;
}

static struct pdc_intc_priv *irqd_to_priv(struct irq_data *data)
{
	return (struct pdc_intc_priv *)data->domain->host_data;
}

/*
 * perip_irq_mask() and perip_irq_unmask() use IRQ_ROUTE which also contains
 * wake bits, therefore we cannot use the generic irqchip mask callbacks as they
 * cache the mask.
 */

static void perip_irq_mask(struct irq_data *data)
{
	struct pdc_intc_priv *priv = irqd_to_priv(data);

	raw_spin_lock(&priv->lock);
	priv->irq_route &= ~data->mask;
	pdc_write(priv, PDC_IRQ_ROUTE, priv->irq_route);
	raw_spin_unlock(&priv->lock);
}

static void perip_irq_unmask(struct irq_data *data)
{
	struct pdc_intc_priv *priv = irqd_to_priv(data);

	raw_spin_lock(&priv->lock);
	priv->irq_route |= data->mask;
	pdc_write(priv, PDC_IRQ_ROUTE, priv->irq_route);
	raw_spin_unlock(&priv->lock);
}

static int syswake_irq_set_type(struct irq_data *data, unsigned int flow_type)
{
	struct pdc_intc_priv *priv = irqd_to_priv(data);
	unsigned int syswake = hwirq_to_syswake(data->hwirq);
	unsigned int irq_mode;
	unsigned int soc_sys_wake_regoff, soc_sys_wake;

	/* translate to syswake IRQ mode */
	switch (flow_type) {
	case IRQ_TYPE_EDGE_BOTH:
		irq_mode = PDC_SYS_WAKE_INT_CHANGE;
		break;
	case IRQ_TYPE_EDGE_RISING:
		irq_mode = PDC_SYS_WAKE_INT_UP;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		irq_mode = PDC_SYS_WAKE_INT_DOWN;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		irq_mode = PDC_SYS_WAKE_INT_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		irq_mode = PDC_SYS_WAKE_INT_LOW;
		break;
	default:
		return -EINVAL;
	}

	raw_spin_lock(&priv->lock);

	/* set the IRQ mode */
	soc_sys_wake_regoff = PDC_SYS_WAKE_BASE + syswake*PDC_SYS_WAKE_STRIDE;
	soc_sys_wake = pdc_read(priv, soc_sys_wake_regoff);
	soc_sys_wake &= ~PDC_SYS_WAKE_INT_MODE;
	soc_sys_wake |= irq_mode << PDC_SYS_WAKE_INT_MODE_SHIFT;
	pdc_write(priv, soc_sys_wake_regoff, soc_sys_wake);

	/* and update the handler */
	irq_setup_alt_chip(data, flow_type);

	raw_spin_unlock(&priv->lock);

	return 0;
}

/* applies to both peripheral and syswake interrupts */
static int pdc_irq_set_wake(struct irq_data *data, unsigned int on)
{
	struct pdc_intc_priv *priv = irqd_to_priv(data);
	irq_hw_number_t hw = data->hwirq;
	unsigned int mask = (1 << 16) << hw;
	unsigned int dst_irq;

	raw_spin_lock(&priv->lock);
	if (on)
		priv->irq_route |= mask;
	else
		priv->irq_route &= ~mask;
	pdc_write(priv, PDC_IRQ_ROUTE, priv->irq_route);
	raw_spin_unlock(&priv->lock);

	/* control the destination IRQ wakeup too for standby mode */
	if (hwirq_is_syswake(hw))
		dst_irq = priv->syswake_irq;
	else
		dst_irq = priv->perip_irqs[hw];
	irq_set_irq_wake(dst_irq, on);

	return 0;
}

static void pdc_intc_perip_isr(struct irq_desc *desc)
{
	unsigned int irq = irq_desc_get_irq(desc);
	struct pdc_intc_priv *priv;
	unsigned int i, irq_no;

	priv = (struct pdc_intc_priv *)irq_desc_get_handler_data(desc);

	/* find the peripheral number */
	for (i = 0; i < priv->nr_perips; ++i)
		if (irq == priv->perip_irqs[i])
			goto found;

	/* should never get here */
	return;
found:

	/* pass on the interrupt */
	irq_no = irq_linear_revmap(priv->domain, i);
	generic_handle_irq(irq_no);
}

static void pdc_intc_syswake_isr(struct irq_desc *desc)
{
	struct pdc_intc_priv *priv;
	unsigned int syswake, irq_no;
	unsigned int status;

	priv = (struct pdc_intc_priv *)irq_desc_get_handler_data(desc);

	status = pdc_read(priv, PDC_IRQ_STATUS) &
		 pdc_read(priv, PDC_IRQ_ENABLE);
	status &= (1 << priv->nr_syswakes) - 1;

	for (syswake = 0; status; status >>= 1, ++syswake) {
		/* Has this sys_wake triggered? */
		if (!(status & 1))
			continue;

		irq_no = irq_linear_revmap(priv->domain,
					   syswake_to_hwirq(syswake));
		generic_handle_irq(irq_no);
	}
}

static void pdc_intc_setup(struct pdc_intc_priv *priv)
{
	int i;
	unsigned int soc_sys_wake_regoff;
	unsigned int soc_sys_wake;

	/*
	 * Mask all syswake interrupts before routing, or we could receive an
	 * interrupt before we're ready to handle it.
	 */
	pdc_write(priv, PDC_IRQ_ENABLE, 0);

	/*
	 * Enable routing of all syswakes
	 * Disable all wake sources
	 */
	priv->irq_route = ((PDC_IRQ_ROUTE_EXT_EN_SYS0 << priv->nr_syswakes) -
				PDC_IRQ_ROUTE_EXT_EN_SYS0);
	pdc_write(priv, PDC_IRQ_ROUTE, priv->irq_route);

	/* Initialise syswake IRQ */
	for (i = 0; i < priv->nr_syswakes; ++i) {
		/* set the IRQ mode to none */
		soc_sys_wake_regoff = PDC_SYS_WAKE_BASE + i*PDC_SYS_WAKE_STRIDE;
		soc_sys_wake = PDC_SYS_WAKE_INT_NONE
				<< PDC_SYS_WAKE_INT_MODE_SHIFT;
		pdc_write(priv, soc_sys_wake_regoff, soc_sys_wake);
	}
}

static int pdc_intc_probe(struct platform_device *pdev)
{
	struct pdc_intc_priv *priv;
	struct device_node *node = pdev->dev.of_node;
	struct resource *res_regs;
	struct irq_chip_generic *gc;
	unsigned int i;
	int irq, ret;
	u32 val;

	if (!node)
		return -ENOENT;

	/* Get registers */
	res_regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res_regs == NULL) {
		dev_err(&pdev->dev, "cannot find registers resource\n");
		return -ENOENT;
	}

	/* Allocate driver data */
	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "cannot allocate device data\n");
		return -ENOMEM;
	}
	raw_spin_lock_init(&priv->lock);
	platform_set_drvdata(pdev, priv);

	/* Ioremap the registers */
	priv->pdc_base = devm_ioremap(&pdev->dev, res_regs->start,
				      resource_size(res_regs));
	if (!priv->pdc_base)
		return -EIO;

	/* Get number of peripherals */
	ret = of_property_read_u32(node, "num-perips", &val);
	if (ret) {
		dev_err(&pdev->dev, "No num-perips node property found\n");
		return -EINVAL;
	}
	if (val > SYS0_HWIRQ) {
		dev_err(&pdev->dev, "num-perips (%u) out of range\n", val);
		return -EINVAL;
	}
	priv->nr_perips = val;

	/* Get number of syswakes */
	ret = of_property_read_u32(node, "num-syswakes", &val);
	if (ret) {
		dev_err(&pdev->dev, "No num-syswakes node property found\n");
		return -EINVAL;
	}
	if (val > SYS0_HWIRQ) {
		dev_err(&pdev->dev, "num-syswakes (%u) out of range\n", val);
		return -EINVAL;
	}
	priv->nr_syswakes = val;

	/* Get peripheral IRQ numbers */
	priv->perip_irqs = devm_kcalloc(&pdev->dev, 4, priv->nr_perips,
					GFP_KERNEL);
	if (!priv->perip_irqs) {
		dev_err(&pdev->dev, "cannot allocate perip IRQ list\n");
		return -ENOMEM;
	}
	for (i = 0; i < priv->nr_perips; ++i) {
		irq = platform_get_irq(pdev, 1 + i);
		if (irq < 0) {
			dev_err(&pdev->dev, "cannot find perip IRQ #%u\n", i);
			return irq;
		}
		priv->perip_irqs[i] = irq;
	}
	/* check if too many were provided */
	if (platform_get_irq(pdev, 1 + i) >= 0) {
		dev_err(&pdev->dev, "surplus perip IRQs detected\n");
		return -EINVAL;
	}

	/* Get syswake IRQ number */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "cannot find syswake IRQ\n");
		return irq;
	}
	priv->syswake_irq = irq;

	/* Set up an IRQ domain */
	priv->domain = irq_domain_add_linear(node, 16, &irq_generic_chip_ops,
					     priv);
	if (unlikely(!priv->domain)) {
		dev_err(&pdev->dev, "cannot add IRQ domain\n");
		return -ENOMEM;
	}

	/*
	 * Set up 2 generic irq chips with 2 chip types.
	 * The first one for peripheral irqs (only 1 chip type used)
	 * The second one for syswake irqs (edge and level chip types)
	 */
	ret = irq_alloc_domain_generic_chips(priv->domain, 8, 2, "pdc",
					     handle_level_irq, 0, 0,
					     IRQ_GC_INIT_NESTED_LOCK);
	if (ret)
		goto err_generic;

	/* peripheral interrupt chip */

	gc = irq_get_domain_generic_chip(priv->domain, 0);
	gc->unused	= ~(BIT(priv->nr_perips) - 1);
	gc->reg_base	= priv->pdc_base;
	/*
	 * IRQ_ROUTE contains wake bits, so we can't use the generic versions as
	 * they cache the mask
	 */
	gc->chip_types[0].regs.mask		= PDC_IRQ_ROUTE;
	gc->chip_types[0].chip.irq_mask		= perip_irq_mask;
	gc->chip_types[0].chip.irq_unmask	= perip_irq_unmask;
	gc->chip_types[0].chip.irq_set_wake	= pdc_irq_set_wake;

	/* syswake interrupt chip */

	gc = irq_get_domain_generic_chip(priv->domain, 8);
	gc->unused	= ~(BIT(priv->nr_syswakes) - 1);
	gc->reg_base	= priv->pdc_base;

	/* edge interrupts */
	gc->chip_types[0].type			= IRQ_TYPE_EDGE_BOTH;
	gc->chip_types[0].handler		= handle_edge_irq;
	gc->chip_types[0].regs.ack		= PDC_IRQ_CLEAR;
	gc->chip_types[0].regs.mask		= PDC_IRQ_ENABLE;
	gc->chip_types[0].chip.irq_ack		= irq_gc_ack_set_bit;
	gc->chip_types[0].chip.irq_mask		= irq_gc_mask_clr_bit;
	gc->chip_types[0].chip.irq_unmask	= irq_gc_mask_set_bit;
	gc->chip_types[0].chip.irq_set_type	= syswake_irq_set_type;
	gc->chip_types[0].chip.irq_set_wake	= pdc_irq_set_wake;
	/* for standby we pass on to the shared syswake IRQ */
	gc->chip_types[0].chip.flags		= IRQCHIP_MASK_ON_SUSPEND;

	/* level interrupts */
	gc->chip_types[1].type			= IRQ_TYPE_LEVEL_MASK;
	gc->chip_types[1].handler		= handle_level_irq;
	gc->chip_types[1].regs.ack		= PDC_IRQ_CLEAR;
	gc->chip_types[1].regs.mask		= PDC_IRQ_ENABLE;
	gc->chip_types[1].chip.irq_ack		= irq_gc_ack_set_bit;
	gc->chip_types[1].chip.irq_mask		= irq_gc_mask_clr_bit;
	gc->chip_types[1].chip.irq_unmask	= irq_gc_mask_set_bit;
	gc->chip_types[1].chip.irq_set_type	= syswake_irq_set_type;
	gc->chip_types[1].chip.irq_set_wake	= pdc_irq_set_wake;
	/* for standby we pass on to the shared syswake IRQ */
	gc->chip_types[1].chip.flags		= IRQCHIP_MASK_ON_SUSPEND;

	/* Set up the hardware to enable interrupt routing */
	pdc_intc_setup(priv);

	/* Setup chained handlers for the peripheral IRQs */
	for (i = 0; i < priv->nr_perips; ++i) {
		irq = priv->perip_irqs[i];
		irq_set_chained_handler_and_data(irq, pdc_intc_perip_isr,
						 priv);
	}

	/* Setup chained handler for the syswake IRQ */
	irq_set_chained_handler_and_data(priv->syswake_irq,
					 pdc_intc_syswake_isr, priv);

	dev_info(&pdev->dev,
		 "PDC IRQ controller initialised (%u perip IRQs, %u syswake IRQs)\n",
		 priv->nr_perips,
		 priv->nr_syswakes);

	return 0;
err_generic:
	irq_domain_remove(priv->domain);
	return ret;
}

static int pdc_intc_remove(struct platform_device *pdev)
{
	struct pdc_intc_priv *priv = platform_get_drvdata(pdev);

	irq_domain_remove(priv->domain);
	return 0;
}

static const struct of_device_id pdc_intc_match[] = {
	{ .compatible = "img,pdc-intc" },
	{}
};

static struct platform_driver pdc_intc_driver = {
	.driver = {
		.name		= "pdc-intc",
		.of_match_table	= pdc_intc_match,
	},
	.probe = pdc_intc_probe,
	.remove = pdc_intc_remove,
};

static int __init pdc_intc_init(void)
{
	return platform_driver_register(&pdc_intc_driver);
}
core_initcall(pdc_intc_init);
