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
#include <linux/gpio/generic.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#define VF610_GPIO_PER_PORT		32

struct fsl_gpio_soc_data {
	/* SoCs has a Port Data Direction Register (PDDR) */
	bool have_paddr;
	bool have_dual_base;
};

struct vf610_gpio_port {
	struct gpio_generic_chip chip;
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

#define IMX8ULP_GPIO_BASE_OFF	0x40
#define IMX8ULP_BASE_OFF	0x80

static const struct fsl_gpio_soc_data vf610_data = {
	.have_dual_base = true,
};

static const struct fsl_gpio_soc_data imx_data = {
	.have_paddr = true,
	.have_dual_base = true,
};

static const struct fsl_gpio_soc_data imx8ulp_data = {
	.have_paddr = true,
};

static const struct of_device_id vf610_gpio_dt_ids[] = {
	{ .compatible = "fsl,vf610-gpio",	.data = &vf610_data },
	{ .compatible = "fsl,imx7ulp-gpio",	.data = &imx_data, },
	{ .compatible = "fsl,imx8ulp-gpio",	.data = &imx8ulp_data, },
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

		generic_handle_domain_irq(port->chip.gc.irq.domain, pin);
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
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct vf610_gpio_port *port = gpiochip_get_data(gc);
	irq_hw_number_t gpio_num = irqd_to_hwirq(d);
	void __iomem *pcr_base = port->base + PORT_PCR(gpio_num);

	vf610_gpio_writel(0, pcr_base);
	gpiochip_disable_irq(gc, gpio_num);
}

static void vf610_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct vf610_gpio_port *port = gpiochip_get_data(gc);
	irq_hw_number_t gpio_num = irqd_to_hwirq(d);
	void __iomem *pcr_base = port->base + PORT_PCR(gpio_num);

	gpiochip_enable_irq(gc, gpio_num);
	vf610_gpio_writel(port->irqc[gpio_num] << PORT_PCR_IRQC_OFFSET,
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

static const struct irq_chip vf610_irqchip = {
	.name = "gpio-vf610",
	.irq_ack = vf610_gpio_irq_ack,
	.irq_mask = vf610_gpio_irq_mask,
	.irq_unmask = vf610_gpio_irq_unmask,
	.irq_set_type = vf610_gpio_irq_set_type,
	.irq_set_wake = vf610_gpio_irq_set_wake,
	.flags = IRQCHIP_IMMUTABLE | IRQCHIP_MASK_ON_SUSPEND
			| IRQCHIP_ENABLE_WAKEUP_ON_SUSPEND,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static void vf610_gpio_disable_clk(void *data)
{
	clk_disable_unprepare(data);
}

static int vf610_gpio_probe(struct platform_device *pdev)
{
	struct gpio_generic_chip_config config;
	struct device *dev = &pdev->dev;
	struct vf610_gpio_port *port;
	struct gpio_chip *gc;
	struct gpio_irq_chip *girq;
	unsigned long flags;
	int i;
	int ret;
	bool dual_base;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->sdata = device_get_match_data(dev);

	dual_base = port->sdata->have_dual_base;

	/*
	 * Handle legacy compatible combinations which used two reg values
	 * for the i.MX8ULP and i.MX93.
	 */
	if (device_is_compatible(dev, "fsl,imx7ulp-gpio") &&
	    (device_is_compatible(dev, "fsl,imx93-gpio") ||
	    (device_is_compatible(dev, "fsl,imx8ulp-gpio"))))
		dual_base = true;

	if (dual_base) {
		port->base = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(port->base))
			return PTR_ERR(port->base);

		port->gpio_base = devm_platform_ioremap_resource(pdev, 1);
		if (IS_ERR(port->gpio_base))
			return PTR_ERR(port->gpio_base);
	} else {
		port->base = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(port->base))
			return PTR_ERR(port->base);

		port->gpio_base = port->base + IMX8ULP_GPIO_BASE_OFF;
		port->base = port->base + IMX8ULP_BASE_OFF;
	}

	port->irq = platform_get_irq(pdev, 0);
	if (port->irq < 0)
		return port->irq;

	port->clk_port = devm_clk_get(dev, "port");
	ret = PTR_ERR_OR_ZERO(port->clk_port);
	if (!ret) {
		ret = clk_prepare_enable(port->clk_port);
		if (ret)
			return ret;
		ret = devm_add_action_or_reset(dev, vf610_gpio_disable_clk,
					       port->clk_port);
		if (ret)
			return ret;
	} else if (ret == -EPROBE_DEFER) {
		/*
		 * Percolate deferrals, for anything else,
		 * just live without the clocking.
		 */
		return ret;
	}

	port->clk_gpio = devm_clk_get(dev, "gpio");
	ret = PTR_ERR_OR_ZERO(port->clk_gpio);
	if (!ret) {
		ret = clk_prepare_enable(port->clk_gpio);
		if (ret)
			return ret;
		ret = devm_add_action_or_reset(dev, vf610_gpio_disable_clk,
					       port->clk_gpio);
		if (ret)
			return ret;
	} else if (ret == -EPROBE_DEFER) {
		return ret;
	}

	gc = &port->chip.gc;
	flags = GPIO_GENERIC_PINCTRL_BACKEND;
	/*
	 * We only read the output register for current value on output
	 * lines if the direction register is available so we can switch
	 * direction.
	 */
	if (port->sdata->have_paddr)
		flags |= GPIO_GENERIC_READ_OUTPUT_REG_SET;

	config = (struct gpio_generic_chip_config) {
		.dev = dev,
		.sz = 4,
		.dat = port->gpio_base + GPIO_PDIR,
		.set = port->gpio_base + GPIO_PDOR,
		.dirout = port->sdata->have_paddr ?
				port->gpio_base + GPIO_PDDR : NULL,
		.flags = flags,
	};

	ret = gpio_generic_chip_init(&port->chip, &config);
	if (ret)
		return dev_err_probe(dev, ret, "unable to init generic GPIO\n");
	gc->label = dev_name(dev);
	gc->base = -1;

	/* Mask all GPIO interrupts */
	for (i = 0; i < gc->ngpio; i++)
		vf610_gpio_writel(0, port->base + PORT_PCR(i));

	/* Clear the interrupt status register for all GPIO's */
	vf610_gpio_writel(~0, port->base + PORT_ISFR);

	girq = &gc->irq;
	gpio_irq_chip_set_chip(girq, &vf610_irqchip);
	girq->parent_handler = vf610_gpio_irq_handler;
	girq->num_parents = 1;
	girq->parents = devm_kcalloc(&pdev->dev, 1,
				     sizeof(*girq->parents),
				     GFP_KERNEL);
	if (!girq->parents)
		return -ENOMEM;
	girq->parents[0] = port->irq;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_edge_irq;

	return devm_gpiochip_add_data(dev, gc, port);
}

static struct platform_driver vf610_gpio_driver = {
	.driver		= {
		.name	= "gpio-vf610",
		.of_match_table = vf610_gpio_dt_ids,
	},
	.probe		= vf610_gpio_probe,
};

module_platform_driver(vf610_gpio_driver);
MODULE_DESCRIPTION("VF610 GPIO driver");
MODULE_LICENSE("GPL");
