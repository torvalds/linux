// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/plat-pxa/gpio.c
 *
 *  Generic PXA GPIO handling
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 */
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/gpio-pxa.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/syscore_ops.h>
#include <linux/slab.h>

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
 * BANK 6 - 0x0200  0x020C  0x0218  0x0224  0x0230  0x023C  0x0248
 *
 * NOTE:
 *   BANK 3 is only available on PXA27x and later processors.
 *   BANK 4 and 5 are only available on PXA935, PXA1928
 *   BANK 6 is only available on PXA1928
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

#define BANK_OFF(n)	(((n) / 3) << 8) + (((n) % 3) << 2)

int pxa_last_gpio;
static int irq_base;

struct pxa_gpio_bank {
	void __iomem	*regbase;
	unsigned long	irq_mask;
	unsigned long	irq_edge_rise;
	unsigned long	irq_edge_fall;

#ifdef CONFIG_PM
	unsigned long	saved_gplr;
	unsigned long	saved_gpdr;
	unsigned long	saved_grer;
	unsigned long	saved_gfer;
#endif
};

struct pxa_gpio_chip {
	struct device *dev;
	struct gpio_chip chip;
	struct pxa_gpio_bank *banks;
	struct irq_domain *irqdomain;

	int irq0;
	int irq1;
	int (*set_wake)(unsigned int gpio, unsigned int on);
};

enum pxa_gpio_type {
	PXA25X_GPIO = 0,
	PXA26X_GPIO,
	PXA27X_GPIO,
	PXA3XX_GPIO,
	PXA93X_GPIO,
	MMP_GPIO = 0x10,
	MMP2_GPIO,
	PXA1928_GPIO,
};

struct pxa_gpio_id {
	enum pxa_gpio_type	type;
	int			gpio_nums;
};

static DEFINE_SPINLOCK(gpio_lock);
static struct pxa_gpio_chip *pxa_gpio_chip;
static enum pxa_gpio_type gpio_type;

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

static struct pxa_gpio_id pxa1928_id = {
	.type		= PXA1928_GPIO,
	.gpio_nums	= 224,
};

#define for_each_gpio_bank(i, b, pc)					\
	for (i = 0, b = pc->banks; i <= pxa_last_gpio; i += 32, b++)

static inline struct pxa_gpio_chip *chip_to_pxachip(struct gpio_chip *c)
{
	struct pxa_gpio_chip *pxa_chip = gpiochip_get_data(c);

	return pxa_chip;
}

static inline void __iomem *gpio_bank_base(struct gpio_chip *c, int gpio)
{
	struct pxa_gpio_chip *p = gpiochip_get_data(c);
	struct pxa_gpio_bank *bank = p->banks + (gpio / 32);

	return bank->regbase;
}

static inline struct pxa_gpio_bank *gpio_to_pxabank(struct gpio_chip *c,
						    unsigned gpio)
{
	return chip_to_pxachip(c)->banks + gpio / 32;
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
static inline int __gpio_is_occupied(struct pxa_gpio_chip *pchip, unsigned gpio)
{
	void __iomem *base;
	unsigned long gafr = 0, gpdr = 0;
	int ret, af = 0, dir = 0;

	base = gpio_bank_base(&pchip->chip, gpio);
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

int pxa_irq_to_gpio(int irq)
{
	struct pxa_gpio_chip *pchip = pxa_gpio_chip;
	int irq_gpio0;

	irq_gpio0 = irq_find_mapping(pchip->irqdomain, 0);
	if (irq_gpio0 > 0)
		return irq - irq_gpio0;

	return irq_gpio0;
}

static bool pxa_gpio_has_pinctrl(void)
{
	switch (gpio_type) {
	case PXA3XX_GPIO:
	case MMP2_GPIO:
		return false;

	default:
		return true;
	}
}

static int pxa_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct pxa_gpio_chip *pchip = chip_to_pxachip(chip);

	return irq_find_mapping(pchip->irqdomain, offset);
}

static int pxa_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	void __iomem *base = gpio_bank_base(chip, offset);
	uint32_t value, mask = GPIO_bit(offset);
	unsigned long flags;
	int ret;

	if (pxa_gpio_has_pinctrl()) {
		ret = pinctrl_gpio_direction_input(chip->base + offset);
		if (ret)
			return ret;
	}

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
	void __iomem *base = gpio_bank_base(chip, offset);
	uint32_t tmp, mask = GPIO_bit(offset);
	unsigned long flags;
	int ret;

	writel_relaxed(mask, base + (value ? GPSR_OFFSET : GPCR_OFFSET));

	if (pxa_gpio_has_pinctrl()) {
		ret = pinctrl_gpio_direction_output(chip->base + offset);
		if (ret)
			return ret;
	}

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
	void __iomem *base = gpio_bank_base(chip, offset);
	u32 gplr = readl_relaxed(base + GPLR_OFFSET);

	return !!(gplr & GPIO_bit(offset));
}

static void pxa_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	void __iomem *base = gpio_bank_base(chip, offset);

	writel_relaxed(GPIO_bit(offset),
		       base + (value ? GPSR_OFFSET : GPCR_OFFSET));
}

#ifdef CONFIG_OF_GPIO
static int pxa_gpio_of_xlate(struct gpio_chip *gc,
			     const struct of_phandle_args *gpiospec,
			     u32 *flags)
{
	if (gpiospec->args[0] > pxa_last_gpio)
		return -EINVAL;

	if (flags)
		*flags = gpiospec->args[1];

	return gpiospec->args[0];
}
#endif

static int pxa_init_gpio_chip(struct pxa_gpio_chip *pchip, int ngpio,
			      struct device_node *np, void __iomem *regbase)
{
	int i, gpio, nbanks = DIV_ROUND_UP(ngpio, 32);
	struct pxa_gpio_bank *bank;

	pchip->banks = devm_kcalloc(pchip->dev, nbanks, sizeof(*pchip->banks),
				    GFP_KERNEL);
	if (!pchip->banks)
		return -ENOMEM;

	pchip->chip.label = "gpio-pxa";
	pchip->chip.direction_input  = pxa_gpio_direction_input;
	pchip->chip.direction_output = pxa_gpio_direction_output;
	pchip->chip.get = pxa_gpio_get;
	pchip->chip.set = pxa_gpio_set;
	pchip->chip.to_irq = pxa_gpio_to_irq;
	pchip->chip.ngpio = ngpio;

	if (pxa_gpio_has_pinctrl()) {
		pchip->chip.request = gpiochip_generic_request;
		pchip->chip.free = gpiochip_generic_free;
	}

#ifdef CONFIG_OF_GPIO
	pchip->chip.of_node = np;
	pchip->chip.of_xlate = pxa_gpio_of_xlate;
	pchip->chip.of_gpio_n_cells = 2;
#endif

	for (i = 0, gpio = 0; i < nbanks; i++, gpio += 32) {
		bank = pchip->banks + i;
		bank->regbase = regbase + BANK_OFF(i);
	}

	return gpiochip_add_data(&pchip->chip, pchip);
}

/* Update only those GRERx and GFERx edge detection register bits if those
 * bits are set in c->irq_mask
 */
static inline void update_edge_detect(struct pxa_gpio_bank *c)
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
	struct pxa_gpio_chip *pchip = irq_data_get_irq_chip_data(d);
	unsigned int gpio = irqd_to_hwirq(d);
	struct pxa_gpio_bank *c = gpio_to_pxabank(&pchip->chip, gpio);
	unsigned long gpdr, mask = GPIO_bit(gpio);

	if (type == IRQ_TYPE_PROBE) {
		/* Don't mess with enabled GPIOs using preconfigured edges or
		 * GPIOs set to alternate function or to output during probe
		 */
		if ((c->irq_edge_rise | c->irq_edge_fall) & GPIO_bit(gpio))
			return 0;

		if (__gpio_is_occupied(pchip, gpio))
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

static irqreturn_t pxa_gpio_demux_handler(int in_irq, void *d)
{
	int loop, gpio, n, handled = 0;
	unsigned long gedr;
	struct pxa_gpio_chip *pchip = d;
	struct pxa_gpio_bank *c;

	do {
		loop = 0;
		for_each_gpio_bank(gpio, c, pchip) {
			gedr = readl_relaxed(c->regbase + GEDR_OFFSET);
			gedr = gedr & c->irq_mask;
			writel_relaxed(gedr, c->regbase + GEDR_OFFSET);

			for_each_set_bit(n, &gedr, BITS_PER_LONG) {
				loop = 1;

				generic_handle_irq(
					irq_find_mapping(pchip->irqdomain,
							 gpio + n));
			}
		}
		handled += loop;
	} while (loop);

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static irqreturn_t pxa_gpio_direct_handler(int in_irq, void *d)
{
	struct pxa_gpio_chip *pchip = d;

	if (in_irq == pchip->irq0) {
		generic_handle_irq(irq_find_mapping(pchip->irqdomain, 0));
	} else if (in_irq == pchip->irq1) {
		generic_handle_irq(irq_find_mapping(pchip->irqdomain, 1));
	} else {
		pr_err("%s() unknown irq %d\n", __func__, in_irq);
		return IRQ_NONE;
	}
	return IRQ_HANDLED;
}

static void pxa_ack_muxed_gpio(struct irq_data *d)
{
	struct pxa_gpio_chip *pchip = irq_data_get_irq_chip_data(d);
	unsigned int gpio = irqd_to_hwirq(d);
	void __iomem *base = gpio_bank_base(&pchip->chip, gpio);

	writel_relaxed(GPIO_bit(gpio), base + GEDR_OFFSET);
}

static void pxa_mask_muxed_gpio(struct irq_data *d)
{
	struct pxa_gpio_chip *pchip = irq_data_get_irq_chip_data(d);
	unsigned int gpio = irqd_to_hwirq(d);
	struct pxa_gpio_bank *b = gpio_to_pxabank(&pchip->chip, gpio);
	void __iomem *base = gpio_bank_base(&pchip->chip, gpio);
	uint32_t grer, gfer;

	b->irq_mask &= ~GPIO_bit(gpio);

	grer = readl_relaxed(base + GRER_OFFSET) & ~GPIO_bit(gpio);
	gfer = readl_relaxed(base + GFER_OFFSET) & ~GPIO_bit(gpio);
	writel_relaxed(grer, base + GRER_OFFSET);
	writel_relaxed(gfer, base + GFER_OFFSET);
}

static int pxa_gpio_set_wake(struct irq_data *d, unsigned int on)
{
	struct pxa_gpio_chip *pchip = irq_data_get_irq_chip_data(d);
	unsigned int gpio = irqd_to_hwirq(d);

	if (pchip->set_wake)
		return pchip->set_wake(gpio, on);
	else
		return 0;
}

static void pxa_unmask_muxed_gpio(struct irq_data *d)
{
	struct pxa_gpio_chip *pchip = irq_data_get_irq_chip_data(d);
	unsigned int gpio = irqd_to_hwirq(d);
	struct pxa_gpio_bank *c = gpio_to_pxabank(&pchip->chip, gpio);

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
	case PXA1928_GPIO:
		gpio_type = pxa_id->type;
		count = pxa_id->gpio_nums - 1;
		break;
	default:
		count = -EINVAL;
		break;
	}
	return count;
}

static int pxa_irq_domain_map(struct irq_domain *d, unsigned int irq,
			      irq_hw_number_t hw)
{
	irq_set_chip_and_handler(irq, &pxa_muxed_gpio_chip,
				 handle_edge_irq);
	irq_set_chip_data(irq, d->host_data);
	irq_set_noprobe(irq);
	return 0;
}

static const struct irq_domain_ops pxa_irq_domain_ops = {
	.map	= pxa_irq_domain_map,
	.xlate	= irq_domain_xlate_twocell,
};

#ifdef CONFIG_OF
static const struct of_device_id pxa_gpio_dt_ids[] = {
	{ .compatible = "intel,pxa25x-gpio",	.data = &pxa25x_id, },
	{ .compatible = "intel,pxa26x-gpio",	.data = &pxa26x_id, },
	{ .compatible = "intel,pxa27x-gpio",	.data = &pxa27x_id, },
	{ .compatible = "intel,pxa3xx-gpio",	.data = &pxa3xx_id, },
	{ .compatible = "marvell,pxa93x-gpio",	.data = &pxa93x_id, },
	{ .compatible = "marvell,mmp-gpio",	.data = &mmp_id, },
	{ .compatible = "marvell,mmp2-gpio",	.data = &mmp2_id, },
	{ .compatible = "marvell,pxa1928-gpio",	.data = &pxa1928_id, },
	{}
};

static int pxa_gpio_probe_dt(struct platform_device *pdev,
			     struct pxa_gpio_chip *pchip)
{
	int nr_gpios;
	const struct pxa_gpio_id *gpio_id;

	gpio_id = of_device_get_match_data(&pdev->dev);
	gpio_type = gpio_id->type;

	nr_gpios = gpio_id->gpio_nums;
	pxa_last_gpio = nr_gpios - 1;

	irq_base = devm_irq_alloc_descs(&pdev->dev, -1, 0, nr_gpios, 0);
	if (irq_base < 0) {
		dev_err(&pdev->dev, "Failed to allocate IRQ numbers\n");
		return irq_base;
	}
	return irq_base;
}
#else
#define pxa_gpio_probe_dt(pdev, pchip)		(-1)
#endif

static int pxa_gpio_probe(struct platform_device *pdev)
{
	struct pxa_gpio_chip *pchip;
	struct pxa_gpio_bank *c;
	struct clk *clk;
	struct pxa_gpio_platform_data *info;
	void __iomem *gpio_reg_base;
	int gpio, ret;
	int irq0 = 0, irq1 = 0, irq_mux;

	pchip = devm_kzalloc(&pdev->dev, sizeof(*pchip), GFP_KERNEL);
	if (!pchip)
		return -ENOMEM;
	pchip->dev = &pdev->dev;

	info = dev_get_platdata(&pdev->dev);
	if (info) {
		irq_base = info->irq_base;
		if (irq_base <= 0)
			return -EINVAL;
		pxa_last_gpio = pxa_gpio_nums(pdev);
		pchip->set_wake = info->gpio_set_wake;
	} else {
		irq_base = pxa_gpio_probe_dt(pdev, pchip);
		if (irq_base < 0)
			return -EINVAL;
	}

	if (!pxa_last_gpio)
		return -EINVAL;

	pchip->irqdomain = irq_domain_add_legacy(pdev->dev.of_node,
						 pxa_last_gpio + 1, irq_base,
						 0, &pxa_irq_domain_ops, pchip);
	if (!pchip->irqdomain)
		return -ENOMEM;

	irq0 = platform_get_irq_byname(pdev, "gpio0");
	irq1 = platform_get_irq_byname(pdev, "gpio1");
	irq_mux = platform_get_irq_byname(pdev, "gpio_mux");
	if ((irq0 > 0 && irq1 <= 0) || (irq0 <= 0 && irq1 > 0)
		|| (irq_mux <= 0))
		return -EINVAL;

	pchip->irq0 = irq0;
	pchip->irq1 = irq1;

	gpio_reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (!gpio_reg_base)
		return -EINVAL;

	clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Error %ld to get gpio clock\n",
			PTR_ERR(clk));
		return PTR_ERR(clk);
	}
	ret = clk_prepare_enable(clk);
	if (ret) {
		clk_put(clk);
		return ret;
	}

	/* Initialize GPIO chips */
	ret = pxa_init_gpio_chip(pchip, pxa_last_gpio + 1, pdev->dev.of_node,
				 gpio_reg_base);
	if (ret) {
		clk_put(clk);
		return ret;
	}

	/* clear all GPIO edge detects */
	for_each_gpio_bank(gpio, c, pchip) {
		writel_relaxed(0, c->regbase + GFER_OFFSET);
		writel_relaxed(0, c->regbase + GRER_OFFSET);
		writel_relaxed(~0, c->regbase + GEDR_OFFSET);
		/* unmask GPIO edge detect for AP side */
		if (gpio_is_mmp_type(gpio_type))
			writel_relaxed(~0, c->regbase + ED_MASK_OFFSET);
	}

	if (irq0 > 0) {
		ret = devm_request_irq(&pdev->dev,
				       irq0, pxa_gpio_direct_handler, 0,
				       "gpio-0", pchip);
		if (ret)
			dev_err(&pdev->dev, "request of gpio0 irq failed: %d\n",
				ret);
	}
	if (irq1 > 0) {
		ret = devm_request_irq(&pdev->dev,
				       irq1, pxa_gpio_direct_handler, 0,
				       "gpio-1", pchip);
		if (ret)
			dev_err(&pdev->dev, "request of gpio1 irq failed: %d\n",
				ret);
	}
	ret = devm_request_irq(&pdev->dev,
			       irq_mux, pxa_gpio_demux_handler, 0,
				       "gpio-mux", pchip);
	if (ret)
		dev_err(&pdev->dev, "request of gpio-mux irq failed: %d\n",
				ret);

	pxa_gpio_chip = pchip;

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
	{ "pxa1928-gpio",	(unsigned long)&pxa1928_id },
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

static int __init pxa_gpio_legacy_init(void)
{
	if (of_have_populated_dt())
		return 0;

	return platform_driver_register(&pxa_gpio_driver);
}
postcore_initcall(pxa_gpio_legacy_init);

static int __init pxa_gpio_dt_init(void)
{
	if (of_have_populated_dt())
		return platform_driver_register(&pxa_gpio_driver);

	return 0;
}
device_initcall(pxa_gpio_dt_init);

#ifdef CONFIG_PM
static int pxa_gpio_suspend(void)
{
	struct pxa_gpio_chip *pchip = pxa_gpio_chip;
	struct pxa_gpio_bank *c;
	int gpio;

	if (!pchip)
		return 0;

	for_each_gpio_bank(gpio, c, pchip) {
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
	struct pxa_gpio_chip *pchip = pxa_gpio_chip;
	struct pxa_gpio_bank *c;
	int gpio;

	if (!pchip)
		return;

	for_each_gpio_bank(gpio, c, pchip) {
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

static struct syscore_ops pxa_gpio_syscore_ops = {
	.suspend	= pxa_gpio_suspend,
	.resume		= pxa_gpio_resume,
};

static int __init pxa_gpio_sysinit(void)
{
	register_syscore_ops(&pxa_gpio_syscore_ops);
	return 0;
}
postcore_initcall(pxa_gpio_sysinit);
