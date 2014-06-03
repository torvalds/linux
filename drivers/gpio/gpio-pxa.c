/*
 *  linux/arch/arm/plat-pxa/gpio.c
 *
 *  Generic PXA GPIO handling
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio-pxa.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/syscore_ops.h>
#include <linux/slab.h>

#include <mach/irqs.h>

/*
 * We handle the GPIOs by banks, each bank covers up to 32 GPIOs with
 * one set of registers. The register offsets are organized below:
 *
 *           GPLR    GPDR    GPSR    GPCR    GRER    GFER    GEDR
 * BANK 0 - 0x0000  0x000C  0x0018  0x0024  0x0030  0x003C  0x0048
 * BANK 1 - 0x0004  0x0010  0x001C  0x0028  0x0034  0x0040  0x004C
 * BANK 2 - 0x0008  0x0014  0x0020  0x002C  0x0038  0x0044  0x0050
 *
 * BANK 3 - 0x0100  0x010C  0x0118  0x0124  0x0130  0x013C  0x0148
 * BANK 4 - 0x0104  0x0110  0x011C  0x0128  0x0134  0x0140  0x014C
 * BANK 5 - 0x0108  0x0114  0x0120  0x012C  0x0138  0x0144  0x0150
 *
 * NOTE:
 *   BANK 3 is only available on PXA27x and later processors.
 *   BANK 4 and 5 are only available on PXA935
 */

#define GPLR_OFFSET	0x00
#define GPDR_OFFSET	0x0C
#define GPSR_OFFSET	0x18
#define GPCR_OFFSET	0x24
#define GRER_OFFSET	0x30
#define GFER_OFFSET	0x3C
#define GEDR_OFFSET	0x48
#define GAFR_OFFSET	0x54
#define ED_MASK_OFFSET	0x9C	/* GPIO edge detection for AP side */

#define BANK_OFF(n)	(((n) < 3) ? (n) << 2 : 0x100 + (((n) - 3) << 2))

int pxa_last_gpio;
static int irq_base;

#ifdef CONFIG_OF
static struct irq_domain *domain;
static struct device_node *pxa_gpio_of_node;
#endif

struct pxa_gpio_chip {
	struct gpio_chip chip;
	void __iomem	*regbase;
	char label[10];

	unsigned long	irq_mask;
	unsigned long	irq_edge_rise;
	unsigned long	irq_edge_fall;
	int (*set_wake)(unsigned int gpio, unsigned int on);

#ifdef CONFIG_PM
	unsigned long	saved_gplr;
	unsigned long	saved_gpdr;
	unsigned long	saved_grer;
	unsigned long	saved_gfer;
#endif
};

enum pxa_gpio_type {
	PXA25X_GPIO = 0,
	PXA26X_GPIO,
	PXA27X_GPIO,
	PXA3XX_GPIO,
	PXA93X_GPIO,
	MMP_GPIO = 0x10,
	MMP2_GPIO,
};

struct pxa_gpio_id {
	enum pxa_gpio_type	type;
	int			gpio_nums;
};

static DEFINE_SPINLOCK(gpio_lock);
static struct pxa_gpio_chip *pxa_gpio_chips;
static enum pxa_gpio_type gpio_type;
static void __iomem *gpio_reg_base;

static struct pxa_gpio_id pxa25x_id = {
	.type		= PXA25X_GPIO,
	.gpio_nums	= 85,
};

static struct pxa_gpio_id pxa26x_id = {
	.type		= PXA26X_GPIO,
	.gpio_nums	= 90,
};

static struct pxa_gpio_id pxa27x_id = {
	.type		= PXA27X_GPIO,
	.gpio_nums	= 121,
};

static struct pxa_gpio_id pxa3xx_id = {
	.type		= PXA3XX_GPIO,
	.gpio_nums	= 128,
};

static struct pxa_gpio_id pxa93x_id = {
	.type		= PXA93X_GPIO,
	.gpio_nums	= 192,
};

static struct pxa_gpio_id mmp_id = {
	.type		= MMP_GPIO,
	.gpio_nums	= 128,
};

static struct pxa_gpio_id mmp2_id = {
	.type		= MMP2_GPIO,
	.gpio_nums	= 192,
};

#define for_each_gpio_chip(i, c)			\
	for (i = 0, c = &pxa_gpio_chips[0]; i <= pxa_last_gpio; i += 32, c++)

static inline void __iomem *gpio_chip_base(struct gpio_chip *c)
{
	return container_of(c, struct pxa_gpio_chip, chip)->regbase;
}

static inline struct pxa_gpio_chip *gpio_to_pxachip(unsigned gpio)
{
	return &pxa_gpio_chips[gpio_to_bank(gpio)];
}

static inline int gpio_is_pxa_type(int type)
{
	return (type & MMP_GPIO) == 0;
}

static inline int gpio_is_mmp_type(int type)
{
	return (type & MMP_GPIO) != 0;
}

/* GPIO86/87/88/89 on PXA26x have their direction bits in PXA_GPDR(2 inverted,
 * as well as their Alternate Function value being '1' for GPIO in GAFRx.
 */
static inline int __gpio_is_inverted(int gpio)
{
	if ((gpio_type == PXA26X_GPIO) && (gpio > 85))
		return 1;
	return 0;
}

/*
 * On PXA25x and PXA27x, GAFRx and GPDRx together decide the alternate
 * function of a GPIO, and GPDRx cannot be altered once configured. It
 * is attributed as "occupied" here (I know this terminology isn't
 * accurate, you are welcome to propose a better one :-)
 */
static inline int __gpio_is_occupied(unsigned gpio)
{
	struct pxa_gpio_chip *pxachip;
	void __iomem *base;
	unsigned long gafr = 0, gpdr = 0;
	int ret, af = 0, dir = 0;

	pxachip = gpio_to_pxachip(gpio);
	base = gpio_chip_base(&pxachip->chip);
	gpdr = readl_relaxed(base + GPDR_OFFSET);

	switch (gpio_type) {
	case PXA25X_GPIO:
	case PXA26X_GPIO:
	case PXA27X_GPIO:
		gafr = readl_relaxed(base + GAFR_OFFSET);
		af = (gafr >> ((gpio & 0xf) * 2)) & 0x3;
		dir = gpdr & GPIO_bit(gpio);

		if (__gpio_is_inverted(gpio))
			ret = (af != 1) || (dir == 0);
		else
			ret = (af != 0) || (dir != 0);
		break;
	default:
		ret = gpdr & GPIO_bit(gpio);
		break;
	}
	return ret;
}

static int pxa_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	return chip->base + offset + irq_base;
}

int pxa_irq_to_gpio(int irq)
{
	return irq - irq_base;
}

static int pxa_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	void __iomem *base = gpio_chip_base(chip);
	uint32_t value, mask = 1 << offset;
	unsigned long flags;

	spin_lock_irqsave(&gpio_lock, flags);

	value = readl_relaxed(base + GPDR_OFFSET);
	if (__gpio_is_inverted(chip->base + offset))
		value |= mask;
	else
		value &= ~mask;
	writel_relaxed(value, base + GPDR_OFFSET);

	spin_unlock_irqrestore(&gpio_lock, flags);
	return 0;
}

static int pxa_gpio_direction_output(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	void __iomem *base = gpio_chip_base(chip);
	uint32_t tmp, mask = 1 << offset;
	unsigned long flags;

	writel_relaxed(mask, base + (value ? GPSR_OFFSET : GPCR_OFFSET));

	spin_lock_irqsave(&gpio_lock, flags);

	tmp = readl_relaxed(base + GPDR_OFFSET);
	if (__gpio_is_inverted(chip->base + offset))
		tmp &= ~mask;
	else
		tmp |= mask;
	writel_relaxed(tmp, base + GPDR_OFFSET);

	spin_unlock_irqrestore(&gpio_lock, flags);
	return 0;
}

static int pxa_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	u32 gplr = readl_relaxed(gpio_chip_base(chip) + GPLR_OFFSET);
	return !!(gplr & (1 << offset));
}

static void pxa_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	writel_relaxed(1 << offset, gpio_chip_base(chip) +
				(value ? GPSR_OFFSET : GPCR_OFFSET));
}

#ifdef CONFIG_OF_GPIO
static int pxa_gpio_of_xlate(struct gpio_chip *gc,
			     const struct of_phandle_args *gpiospec,
			     u32 *flags)
{
	if (gpiospec->args[0] > pxa_last_gpio)
		return -EINVAL;

	if (gc != &pxa_gpio_chips[gpiospec->args[0] / 32].chip)
		return -EINVAL;

	if (flags)
		*flags = gpiospec->args[1];

	return gpiospec->args[0] % 32;
}
#endif

static int pxa_init_gpio_chip(int gpio_end,
					int (*set_wake)(unsigned int, unsigned int))
{
	int i, gpio, nbanks = gpio_to_bank(gpio_end) + 1;
	struct pxa_gpio_chip *chips;

	chips = kzalloc(nbanks * sizeof(struct pxa_gpio_chip), GFP_KERNEL);
	if (chips == NULL) {
		pr_err("%s: failed to allocate GPIO chips\n", __func__);
		return -ENOMEM;
	}

	for (i = 0, gpio = 0; i < nbanks; i++, gpio += 32) {
		struct gpio_chip *c = &chips[i].chip;

		sprintf(chips[i].label, "gpio-%d", i);
		chips[i].regbase = gpio_reg_base + BANK_OFF(i);
		chips[i].set_wake = set_wake;

		c->base  = gpio;
		c->label = chips[i].label;

		c->direction_input  = pxa_gpio_direction_input;
		c->direction_output = pxa_gpio_direction_output;
		c->get = pxa_gpio_get;
		c->set = pxa_gpio_set;
		c->to_irq = pxa_gpio_to_irq;
#ifdef CONFIG_OF_GPIO
		c->of_node = pxa_gpio_of_node;
		c->of_xlate = pxa_gpio_of_xlate;
		c->of_gpio_n_cells = 2;
#endif

		/* number of GPIOs on last bank may be less than 32 */
		c->ngpio = (gpio + 31 > gpio_end) ? (gpio_end - gpio + 1) : 32;
		gpiochip_add(c);
	}
	pxa_gpio_chips = chips;
	return 0;
}

/* Update only those GRERx and GFERx edge detection register bits if those
 * bits are set in c->irq_mask
 */
static inline void update_edge_detect(struct pxa_gpio_chip *c)
{
	uint32_t grer, gfer;

	grer = readl_relaxed(c->regbase + GRER_OFFSET) & ~c->irq_mask;
	gfer = readl_relaxed(c->regbase + GFER_OFFSET) & ~c->irq_mask;
	grer |= c->irq_edge_rise & c->irq_mask;
	gfer |= c->irq_edge_fall & c->irq_mask;
	writel_relaxed(grer, c->regbase + GRER_OFFSET);
	writel_relaxed(gfer, c->regbase + GFER_OFFSET);
}

static int pxa_gpio_irq_type(struct irq_data *d, unsigned int type)
{
	struct pxa_gpio_chip *c;
	int gpio = pxa_irq_to_gpio(d->irq);
	unsigned long gpdr, mask = GPIO_bit(gpio);

	c = gpio_to_pxachip(gpio);

	if (type == IRQ_TYPE_PROBE) {
		/* Don't mess with enabled GPIOs using preconfigured edges or
		 * GPIOs set to alternate function or to output during probe
		 */
		if ((c->irq_edge_rise | c->irq_edge_fall) & GPIO_bit(gpio))
			return 0;

		if (__gpio_is_occupied(gpio))
			return 0;

		type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;
	}

	gpdr = readl_relaxed(c->regbase + GPDR_OFFSET);

	if (__gpio_is_inverted(gpio))
		writel_relaxed(gpdr | mask,  c->regbase + GPDR_OFFSET);
	else
		writel_relaxed(gpdr & ~mask, c->regbase + GPDR_OFFSET);

	if (type & IRQ_TYPE_EDGE_RISING)
		c->irq_edge_rise |= mask;
	else
		c->irq_edge_rise &= ~mask;

	if (type & IRQ_TYPE_EDGE_FALLING)
		c->irq_edge_fall |= mask;
	else
		c->irq_edge_fall &= ~mask;

	update_edge_detect(c);

	pr_debug("%s: IRQ%d (GPIO%d) - edge%s%s\n", __func__, d->irq, gpio,
		((type & IRQ_TYPE_EDGE_RISING)  ? " rising"  : ""),
		((type & IRQ_TYPE_EDGE_FALLING) ? " falling" : ""));
	return 0;
}

static void pxa_gpio_demux_handler(unsigned int irq, struct irq_desc *desc)
{
	struct pxa_gpio_chip *c;
	int loop, gpio, gpio_base, n;
	unsigned long gedr;
	struct irq_chip *chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);

	do {
		loop = 0;
		for_each_gpio_chip(gpio, c) {
			gpio_base = c->chip.base;

			gedr = readl_relaxed(c->regbase + GEDR_OFFSET);
			gedr = gedr & c->irq_mask;
			writel_relaxed(gedr, c->regbase + GEDR_OFFSET);

			for_each_set_bit(n, &gedr, BITS_PER_LONG) {
				loop = 1;

				generic_handle_irq(gpio_to_irq(gpio_base + n));
			}
		}
	} while (loop);

	chained_irq_exit(chip, desc);
}

static void pxa_ack_muxed_gpio(struct irq_data *d)
{
	int gpio = pxa_irq_to_gpio(d->irq);
	struct pxa_gpio_chip *c = gpio_to_pxachip(gpio);

	writel_relaxed(GPIO_bit(gpio), c->regbase + GEDR_OFFSET);
}

static void pxa_mask_muxed_gpio(struct irq_data *d)
{
	int gpio = pxa_irq_to_gpio(d->irq);
	struct pxa_gpio_chip *c = gpio_to_pxachip(gpio);
	uint32_t grer, gfer;

	c->irq_mask &= ~GPIO_bit(gpio);

	grer = readl_relaxed(c->regbase + GRER_OFFSET) & ~GPIO_bit(gpio);
	gfer = readl_relaxed(c->regbase + GFER_OFFSET) & ~GPIO_bit(gpio);
	writel_relaxed(grer, c->regbase + GRER_OFFSET);
	writel_relaxed(gfer, c->regbase + GFER_OFFSET);
}

static int pxa_gpio_set_wake(struct irq_data *d, unsigned int on)
{
	int gpio = pxa_irq_to_gpio(d->irq);
	struct pxa_gpio_chip *c = gpio_to_pxachip(gpio);

	if (c->set_wake)
		return c->set_wake(gpio, on);
	else
		return 0;
}

static void pxa_unmask_muxed_gpio(struct irq_data *d)
{
	int gpio = pxa_irq_to_gpio(d->irq);
	struct pxa_gpio_chip *c = gpio_to_pxachip(gpio);

	c->irq_mask |= GPIO_bit(gpio);
	update_edge_detect(c);
}

static struct irq_chip pxa_muxed_gpio_chip = {
	.name		= "GPIO",
	.irq_ack	= pxa_ack_muxed_gpio,
	.irq_mask	= pxa_mask_muxed_gpio,
	.irq_unmask	= pxa_unmask_muxed_gpio,
	.irq_set_type	= pxa_gpio_irq_type,
	.irq_set_wake	= pxa_gpio_set_wake,
};

static int pxa_gpio_nums(struct platform_device *pdev)
{
	const struct platform_device_id *id = platform_get_device_id(pdev);
	struct pxa_gpio_id *pxa_id = (struct pxa_gpio_id *)id->driver_data;
	int count = 0;

	switch (pxa_id->type) {
	case PXA25X_GPIO:
	case PXA26X_GPIO:
	case PXA27X_GPIO:
	case PXA3XX_GPIO:
	case PXA93X_GPIO:
	case MMP_GPIO:
	case MMP2_GPIO:
		gpio_type = pxa_id->type;
		count = pxa_id->gpio_nums - 1;
		break;
	default:
		count = -EINVAL;
		break;
	}
	return count;
}

#ifdef CONFIG_OF
static const struct of_device_id pxa_gpio_dt_ids[] = {
	{ .compatible = "intel,pxa25x-gpio",	.data = &pxa25x_id, },
	{ .compatible = "intel,pxa26x-gpio",	.data = &pxa26x_id, },
	{ .compatible = "intel,pxa27x-gpio",	.data = &pxa27x_id, },
	{ .compatible = "intel,pxa3xx-gpio",	.data = &pxa3xx_id, },
	{ .compatible = "marvell,pxa93x-gpio",	.data = &pxa93x_id, },
	{ .compatible = "marvell,mmp-gpio",	.data = &mmp_id, },
	{ .compatible = "marvell,mmp2-gpio",	.data = &mmp2_id, },
	{}
};

static int pxa_irq_domain_map(struct irq_domain *d, unsigned int irq,
			      irq_hw_number_t hw)
{
	irq_set_chip_and_handler(irq, &pxa_muxed_gpio_chip,
				 handle_edge_irq);
	set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	return 0;
}

const struct irq_domain_ops pxa_irq_domain_ops = {
	.map	= pxa_irq_domain_map,
	.xlate	= irq_domain_xlate_twocell,
};

static int pxa_gpio_probe_dt(struct platform_device *pdev)
{
	int ret = 0, nr_gpios;
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *of_id =
				of_match_device(pxa_gpio_dt_ids, &pdev->dev);
	const struct pxa_gpio_id *gpio_id;

	if (!of_id || !of_id->data) {
		dev_err(&pdev->dev, "Failed to find gpio controller\n");
		return -EFAULT;
	}
	gpio_id = of_id->data;
	gpio_type = gpio_id->type;

	nr_gpios = gpio_id->gpio_nums;
	pxa_last_gpio = nr_gpios - 1;

	irq_base = irq_alloc_descs(-1, 0, nr_gpios, 0);
	if (irq_base < 0) {
		dev_err(&pdev->dev, "Failed to allocate IRQ numbers\n");
		ret = irq_base;
		goto err;
	}
	domain = irq_domain_add_legacy(np, nr_gpios, irq_base, 0,
				       &pxa_irq_domain_ops, NULL);
	pxa_gpio_of_node = np;
	return 0;
err:
	iounmap(gpio_reg_base);
	return ret;
}
#else
#define pxa_gpio_probe_dt(pdev)		(-1)
#endif

static int pxa_gpio_probe(struct platform_device *pdev)
{
	struct pxa_gpio_chip *c;
	struct resource *res;
	struct clk *clk;
	struct pxa_gpio_platform_data *info;
	int gpio, irq, ret, use_of = 0;
	int irq0 = 0, irq1 = 0, irq_mux, gpio_offset = 0;

	info = dev_get_platdata(&pdev->dev);
	if (info) {
		irq_base = info->irq_base;
		if (irq_base <= 0)
			return -EINVAL;
		pxa_last_gpio = pxa_gpio_nums(pdev);
	} else {
		irq_base = 0;
		use_of = 1;
		ret = pxa_gpio_probe_dt(pdev);
		if (ret < 0)
			return -EINVAL;
	}

	if (!pxa_last_gpio)
		return -EINVAL;

	irq0 = platform_get_irq_byname(pdev, "gpio0");
	irq1 = platform_get_irq_byname(pdev, "gpio1");
	irq_mux = platform_get_irq_byname(pdev, "gpio_mux");
	if ((irq0 > 0 && irq1 <= 0) || (irq0 <= 0 && irq1 > 0)
		|| (irq_mux <= 0))
		return -EINVAL;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	gpio_reg_base = ioremap(res->start, resource_size(res));
	if (!gpio_reg_base)
		return -EINVAL;

	if (irq0 > 0)
		gpio_offset = 2;

	clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Error %ld to get gpio clock\n",
			PTR_ERR(clk));
		iounmap(gpio_reg_base);
		return PTR_ERR(clk);
	}
	ret = clk_prepare_enable(clk);
	if (ret) {
		clk_put(clk);
		iounmap(gpio_reg_base);
		return ret;
	}

	/* Initialize GPIO chips */
	pxa_init_gpio_chip(pxa_last_gpio, info ? info->gpio_set_wake : NULL);

	/* clear all GPIO edge detects */
	for_each_gpio_chip(gpio, c) {
		writel_relaxed(0, c->regbase + GFER_OFFSET);
		writel_relaxed(0, c->regbase + GRER_OFFSET);
		writel_relaxed(~0, c->regbase + GEDR_OFFSET);
		/* unmask GPIO edge detect for AP side */
		if (gpio_is_mmp_type(gpio_type))
			writel_relaxed(~0, c->regbase + ED_MASK_OFFSET);
	}

	if (!use_of) {
#ifdef CONFIG_ARCH_PXA
		irq = gpio_to_irq(0);
		irq_set_chip_and_handler(irq, &pxa_muxed_gpio_chip,
					 handle_edge_irq);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
		irq_set_chained_handler(IRQ_GPIO0, pxa_gpio_demux_handler);

		irq = gpio_to_irq(1);
		irq_set_chip_and_handler(irq, &pxa_muxed_gpio_chip,
					 handle_edge_irq);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
		irq_set_chained_handler(IRQ_GPIO1, pxa_gpio_demux_handler);
#endif

		for (irq  = gpio_to_irq(gpio_offset);
			irq <= gpio_to_irq(pxa_last_gpio); irq++) {
			irq_set_chip_and_handler(irq, &pxa_muxed_gpio_chip,
						 handle_edge_irq);
			set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
		}
	}

	irq_set_chained_handler(irq_mux, pxa_gpio_demux_handler);
	return 0;
}

static const struct platform_device_id gpio_id_table[] = {
	{ "pxa25x-gpio",	(unsigned long)&pxa25x_id },
	{ "pxa26x-gpio",	(unsigned long)&pxa26x_id },
	{ "pxa27x-gpio",	(unsigned long)&pxa27x_id },
	{ "pxa3xx-gpio",	(unsigned long)&pxa3xx_id },
	{ "pxa93x-gpio",	(unsigned long)&pxa93x_id },
	{ "mmp-gpio",		(unsigned long)&mmp_id },
	{ "mmp2-gpio",		(unsigned long)&mmp2_id },
	{ },
};

static struct platform_driver pxa_gpio_driver = {
	.probe		= pxa_gpio_probe,
	.driver		= {
		.name	= "pxa-gpio",
		.of_match_table = of_match_ptr(pxa_gpio_dt_ids),
	},
	.id_table	= gpio_id_table,
};

static int __init pxa_gpio_init(void)
{
	return platform_driver_register(&pxa_gpio_driver);
}
postcore_initcall(pxa_gpio_init);

#ifdef CONFIG_PM
static int pxa_gpio_suspend(void)
{
	struct pxa_gpio_chip *c;
	int gpio;

	for_each_gpio_chip(gpio, c) {
		c->saved_gplr = readl_relaxed(c->regbase + GPLR_OFFSET);
		c->saved_gpdr = readl_relaxed(c->regbase + GPDR_OFFSET);
		c->saved_grer = readl_relaxed(c->regbase + GRER_OFFSET);
		c->saved_gfer = readl_relaxed(c->regbase + GFER_OFFSET);

		/* Clear GPIO transition detect bits */
		writel_relaxed(0xffffffff, c->regbase + GEDR_OFFSET);
	}
	return 0;
}

static void pxa_gpio_resume(void)
{
	struct pxa_gpio_chip *c;
	int gpio;

	for_each_gpio_chip(gpio, c) {
		/* restore level with set/clear */
		writel_relaxed(c->saved_gplr, c->regbase + GPSR_OFFSET);
		writel_relaxed(~c->saved_gplr, c->regbase + GPCR_OFFSET);

		writel_relaxed(c->saved_grer, c->regbase + GRER_OFFSET);
		writel_relaxed(c->saved_gfer, c->regbase + GFER_OFFSET);
		writel_relaxed(c->saved_gpdr, c->regbase + GPDR_OFFSET);
	}
}
#else
#define pxa_gpio_suspend	NULL
#define pxa_gpio_resume		NULL
#endif

struct syscore_ops pxa_gpio_syscore_ops = {
	.suspend	= pxa_gpio_suspend,
	.resume		= pxa_gpio_resume,
};

static int __init pxa_gpio_sysinit(void)
{
	register_syscore_ops(&pxa_gpio_syscore_ops);
	return 0;
}
postcore_initcall(pxa_gpio_sysinit);
