// SPDX-License-Identifier: GPL-2.0+
/*
 * Freescale vf610 GPIO support through PORT and GPIO
 *
 * Copyright (c) 2014 Toradex AG.
 *
 * Author: Stefan Agner <stefan@agner.ch>.
 */
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>

#define VF610_GPIO_PER_PORT		32

struct fsl_gpio_soc_data {
	/* SoCs has a Port Data Direction Register (PDDR) */
	bool have_paddr;
};

struct vf610_gpio_port {
	struct gpio_chip gc;
	void __iomem *base;
	void __iomem *gpio_base;
	const struct fsl_gpio_soc_data *sdata;
	u8 irqc[VF610_GPIO_PER_PORT];
	struct clk *clk_port;
	struct clk *clk_gpio;
	int irq;
};

#define GPIO_PDOR		0x00
#define GPIO_PSOR		0x04
#define GPIO_PCOR		0x08
#define GPIO_PTOR		0x0c
#define GPIO_PDIR		0x10
#define GPIO_PDDR		0x14

#define PORT_PCR(n)		((n) * 0x4)
#define PORT_PCR_IRQC_OFFSET	16

#define PORT_ISFR		0xa0
#define PORT_DFER		0xc0
#define PORT_DFCR		0xc4
#define PORT_DFWR		0xc8

#define PORT_INT_OFF		0x0
#define PORT_INT_LOGIC_ZERO	0x8
#define PORT_INT_RISING_EDGE	0x9
#define PORT_INT_FALLING_EDGE	0xa
#define PORT_INT_EITHER_EDGE	0xb
#define PORT_INT_LOGIC_ONE	0xc

static struct irq_chip vf610_gpio_irq_chip;

static const struct fsl_gpio_soc_data imx_data = {
	.have_paddr = true,
};

static const struct of_device_id vf610_gpio_dt_ids[] = {
	{ .compatible = "fsl,vf610-gpio",	.data = NULL, },
	{ .compatible = "fsl,imx7ulp-gpio",	.data = &imx_data, },
	{ /* sentinel */ }
};

static inline void vf610_gpio_writel(u32 val, void __iomem *reg)
{
	writel_relaxed(val, reg);
}

static inline u32 vf610_gpio_readl(void __iomem *reg)
{
	return readl_relaxed(reg);
}

static int vf610_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct vf610_gpio_port *port = gpiochip_get_data(gc);
	unsigned long mask = BIT(gpio);
	void __iomem *addr;

	if (port->sdata && port->sdata->have_paddr) {
		mask &= vf610_gpio_readl(port->gpio_base + GPIO_PDDR);
		addr = mask ? port->gpio_base + GPIO_PDOR :
			      port->gpio_base + GPIO_PDIR;
		return !!(vf610_gpio_readl(addr) & BIT(gpio));
	} else {
		return !!(vf610_gpio_readl(port->gpio_base + GPIO_PDIR)
					   & BIT(gpio));
	}
}

static void vf610_gpio_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct vf610_gpio_port *port = gpiochip_get_data(gc);
	unsigned long mask = BIT(gpio);

	if (val)
		vf610_gpio_writel(mask, port->gpio_base + GPIO_PSOR);
	else
		vf610_gpio_writel(mask, port->gpio_base + GPIO_PCOR);
}

static int vf610_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	struct vf610_gpio_port *port = gpiochip_get_data(chip);
	unsigned long mask = BIT(gpio);
	u32 val;

	if (port->sdata && port->sdata->have_paddr) {
		val = vf610_gpio_readl(port->gpio_base + GPIO_PDDR);
		val &= ~mask;
		vf610_gpio_writel(val, port->gpio_base + GPIO_PDDR);
	}

	return pinctrl_gpio_direction_input(chip->base + gpio);
}

static int vf610_gpio_direction_output(struct gpio_chip *chip, unsigned gpio,
				       int value)
{
	struct vf610_gpio_port *port = gpiochip_get_data(chip);
	unsigned long mask = BIT(gpio);

	if (port->sdata && port->sdata->have_paddr)
		vf610_gpio_writel(mask, port->gpio_base + GPIO_PDDR);

	vf610_gpio_set(chip, gpio, value);

	return pinctrl_gpio_direction_output(chip->base + gpio);
}

static void vf610_gpio_irq_handler(struct irq_desc *desc)
{
	struct vf610_gpio_port *port =
		gpiochip_get_data(irq_desc_get_handler_data(desc));
	struct irq_chip *chip = irq_desc_get_chip(desc);
	int pin;
	unsigned long irq_isfr;

	chained_irq_enter(chip, desc);

	irq_isfr = vf610_gpio_readl(port->base + PORT_ISFR);

	for_each_set_bit(pin, &irq_isfr, VF610_GPIO_PER_PORT) {
		vf610_gpio_writel(BIT(pin), port->base + PORT_ISFR);

		generic_handle_irq(irq_find_mapping(port->gc.irq.domain, pin));
	}

	chained_irq_exit(chip, desc);
}

static void vf610_gpio_irq_ack(struct irq_data *d)
{
	struct vf610_gpio_port *port =
		gpiochip_get_data(irq_data_get_irq_chip_data(d));
	int gpio = d->hwirq;

	vf610_gpio_writel(BIT(gpio), port->base + PORT_ISFR);
}

static int vf610_gpio_irq_set_type(struct irq_data *d, u32 type)
{
	struct vf610_gpio_port *port =
		gpiochip_get_data(irq_data_get_irq_chip_data(d));
	u8 irqc;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		irqc = PORT_INT_RISING_EDGE;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		irqc = PORT_INT_FALLING_EDGE;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		irqc = PORT_INT_EITHER_EDGE;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		irqc = PORT_INT_LOGIC_ZERO;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		irqc = PORT_INT_LOGIC_ONE;
		break;
	default:
		return -EINVAL;
	}

	port->irqc[d->hwirq] = irqc;

	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(d, handle_level_irq);
	else
		irq_set_handler_locked(d, handle_edge_irq);

	return 0;
}

static void vf610_gpio_irq_mask(struct irq_data *d)
{
	struct vf610_gpio_port *port =
		gpiochip_get_data(irq_data_get_irq_chip_data(d));
	void __iomem *pcr_base = port->base + PORT_PCR(d->hwirq);

	vf610_gpio_writel(0, pcr_base);
}

static void vf610_gpio_irq_unmask(struct irq_data *d)
{
	struct vf610_gpio_port *port =
		gpiochip_get_data(irq_data_get_irq_chip_data(d));
	void __iomem *pcr_base = port->base + PORT_PCR(d->hwirq);

	vf610_gpio_writel(port->irqc[d->hwirq] << PORT_PCR_IRQC_OFFSET,
			  pcr_base);
}

static int vf610_gpio_irq_set_wake(struct irq_data *d, u32 enable)
{
	struct vf610_gpio_port *port =
		gpiochip_get_data(irq_data_get_irq_chip_data(d));

	if (enable)
		enable_irq_wake(port->irq);
	else
		disable_irq_wake(port->irq);

	return 0;
}

static struct irq_chip vf610_gpio_irq_chip = {
	.name		= "gpio-vf610",
	.irq_ack	= vf610_gpio_irq_ack,
	.irq_mask	= vf610_gpio_irq_mask,
	.irq_unmask	= vf610_gpio_irq_unmask,
	.irq_set_type	= vf610_gpio_irq_set_type,
	.irq_set_wake	= vf610_gpio_irq_set_wake,
};

static int vf610_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct vf610_gpio_port *port;
	struct resource *iores;
	struct gpio_chip *gc;
	int ret;

	port = devm_kzalloc(&pdev->dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->sdata = of_device_get_match_data(dev);
	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	port->base = devm_ioremap_resource(dev, iores);
	if (IS_ERR(port->base))
		return PTR_ERR(port->base);

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	port->gpio_base = devm_ioremap_resource(dev, iores);
	if (IS_ERR(port->gpio_base))
		return PTR_ERR(port->gpio_base);

	port->irq = platform_get_irq(pdev, 0);
	if (port->irq < 0)
		return port->irq;

	port->clk_port = devm_clk_get(&pdev->dev, "port");
	if (!IS_ERR(port->clk_port)) {
		ret = clk_prepare_enable(port->clk_port);
		if (ret)
			return ret;
	} else if (port->clk_port == ERR_PTR(-EPROBE_DEFER)) {
		/*
		 * Percolate deferrals, for anything else,
		 * just live without the clocking.
		 */
		return PTR_ERR(port->clk_port);
	}

	port->clk_gpio = devm_clk_get(&pdev->dev, "gpio");
	if (!IS_ERR(port->clk_gpio)) {
		ret = clk_prepare_enable(port->clk_gpio);
		if (ret) {
			clk_disable_unprepare(port->clk_port);
			return ret;
		}
	} else if (port->clk_gpio == ERR_PTR(-EPROBE_DEFER)) {
		clk_disable_unprepare(port->clk_port);
		return PTR_ERR(port->clk_gpio);
	}

	platform_set_drvdata(pdev, port);

	gc = &port->gc;
	gc->of_node = np;
	gc->parent = dev;
	gc->label = "vf610-gpio";
	gc->ngpio = VF610_GPIO_PER_PORT;
	gc->base = of_alias_get_id(np, "gpio") * VF610_GPIO_PER_PORT;

	gc->request = gpiochip_generic_request;
	gc->free = gpiochip_generic_free;
	gc->direction_input = vf610_gpio_direction_input;
	gc->get = vf610_gpio_get;
	gc->direction_output = vf610_gpio_direction_output;
	gc->set = vf610_gpio_set;

	ret = gpiochip_add_data(gc, port);
	if (ret < 0)
		return ret;

	/* Clear the interrupt status register for all GPIO's */
	vf610_gpio_writel(~0, port->base + PORT_ISFR);

	ret = gpiochip_irqchip_add(gc, &vf610_gpio_irq_chip, 0,
				   handle_edge_irq, IRQ_TYPE_NONE);
	if (ret) {
		dev_err(dev, "failed to add irqchip\n");
		gpiochip_remove(gc);
		return ret;
	}
	gpiochip_set_chained_irqchip(gc, &vf610_gpio_irq_chip, port->irq,
				     vf610_gpio_irq_handler);

	return 0;
}

static int vf610_gpio_remove(struct platform_device *pdev)
{
	struct vf610_gpio_port *port = platform_get_drvdata(pdev);

	gpiochip_remove(&port->gc);
	if (!IS_ERR(port->clk_port))
		clk_disable_unprepare(port->clk_port);
	if (!IS_ERR(port->clk_gpio))
		clk_disable_unprepare(port->clk_gpio);

	return 0;
}

static struct platform_driver vf610_gpio_driver = {
	.driver		= {
		.name	= "gpio-vf610",
		.of_match_table = vf610_gpio_dt_ids,
	},
	.probe		= vf610_gpio_probe,
	.remove		= vf610_gpio_remove,
};

builtin_platform_driver(vf610_gpio_driver);
