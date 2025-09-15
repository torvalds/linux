// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2023-2025 SpacemiT (Hangzhou) Technology Co. Ltd
 * Copyright (C) 2025 Yixun Lan <dlan@gentoo.org>
 */

#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>

/* register offset */
#define SPACEMIT_GPLR		0x00 /* port level - R */
#define SPACEMIT_GPDR		0x0c /* port direction - R/W */
#define SPACEMIT_GPSR		0x18 /* port set - W */
#define SPACEMIT_GPCR		0x24 /* port clear - W */
#define SPACEMIT_GRER		0x30 /* port rising edge R/W */
#define SPACEMIT_GFER		0x3c /* port falling edge R/W */
#define SPACEMIT_GEDR		0x48 /* edge detect status - R/W1C */
#define SPACEMIT_GSDR		0x54 /* (set) direction - W */
#define SPACEMIT_GCDR		0x60 /* (clear) direction - W */
#define SPACEMIT_GSRER		0x6c /* (set) rising edge detect enable - W */
#define SPACEMIT_GCRER		0x78 /* (clear) rising edge detect enable - W */
#define SPACEMIT_GSFER		0x84 /* (set) falling edge detect enable - W */
#define SPACEMIT_GCFER		0x90 /* (clear) falling edge detect enable - W */
#define SPACEMIT_GAPMASK	0x9c /* interrupt mask , 0 disable, 1 enable - R/W */

#define SPACEMIT_NR_BANKS		4
#define SPACEMIT_NR_GPIOS_PER_BANK	32

#define to_spacemit_gpio_bank(x) container_of((x), struct spacemit_gpio_bank, gc)

struct spacemit_gpio;

struct spacemit_gpio_bank {
	struct gpio_chip gc;
	struct spacemit_gpio *sg;
	void __iomem *base;
	u32 irq_mask;
	u32 irq_rising_edge;
	u32 irq_falling_edge;
};

struct spacemit_gpio {
	struct device *dev;
	struct spacemit_gpio_bank sgb[SPACEMIT_NR_BANKS];
};

static u32 spacemit_gpio_bank_index(struct spacemit_gpio_bank *gb)
{
	return (u32)(gb - gb->sg->sgb);
}

static irqreturn_t spacemit_gpio_irq_handler(int irq, void *dev_id)
{
	struct spacemit_gpio_bank *gb = dev_id;
	unsigned long pending;
	u32 n, gedr;

	gedr = readl(gb->base + SPACEMIT_GEDR);
	if (!gedr)
		return IRQ_NONE;
	writel(gedr, gb->base + SPACEMIT_GEDR);

	pending = gedr & gb->irq_mask;
	if (!pending)
		return IRQ_NONE;

	for_each_set_bit(n, &pending, BITS_PER_LONG)
		handle_nested_irq(irq_find_mapping(gb->gc.irq.domain, n));

	return IRQ_HANDLED;
}

static void spacemit_gpio_irq_ack(struct irq_data *d)
{
	struct spacemit_gpio_bank *gb = irq_data_get_irq_chip_data(d);

	writel(BIT(irqd_to_hwirq(d)), gb->base + SPACEMIT_GEDR);
}

static void spacemit_gpio_irq_mask(struct irq_data *d)
{
	struct spacemit_gpio_bank *gb = irq_data_get_irq_chip_data(d);
	u32 bit = BIT(irqd_to_hwirq(d));

	gb->irq_mask &= ~bit;
	writel(gb->irq_mask, gb->base + SPACEMIT_GAPMASK);

	if (bit & gb->irq_rising_edge)
		writel(bit, gb->base + SPACEMIT_GCRER);

	if (bit & gb->irq_falling_edge)
		writel(bit, gb->base + SPACEMIT_GCFER);
}

static void spacemit_gpio_irq_unmask(struct irq_data *d)
{
	struct spacemit_gpio_bank *gb = irq_data_get_irq_chip_data(d);
	u32 bit = BIT(irqd_to_hwirq(d));

	gb->irq_mask |= bit;

	if (bit & gb->irq_rising_edge)
		writel(bit, gb->base + SPACEMIT_GSRER);

	if (bit & gb->irq_falling_edge)
		writel(bit, gb->base + SPACEMIT_GSFER);

	writel(gb->irq_mask, gb->base + SPACEMIT_GAPMASK);
}

static int spacemit_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct spacemit_gpio_bank *gb = irq_data_get_irq_chip_data(d);
	u32 bit = BIT(irqd_to_hwirq(d));

	if (type & IRQ_TYPE_EDGE_RISING) {
		gb->irq_rising_edge |= bit;
		writel(bit, gb->base + SPACEMIT_GSRER);
	} else {
		gb->irq_rising_edge &= ~bit;
		writel(bit, gb->base + SPACEMIT_GCRER);
	}

	if (type & IRQ_TYPE_EDGE_FALLING) {
		gb->irq_falling_edge |= bit;
		writel(bit, gb->base + SPACEMIT_GSFER);
	} else {
		gb->irq_falling_edge &= ~bit;
		writel(bit, gb->base + SPACEMIT_GCFER);
	}

	return 0;
}

static void spacemit_gpio_irq_print_chip(struct irq_data *data, struct seq_file *p)
{
	struct spacemit_gpio_bank *gb = irq_data_get_irq_chip_data(data);

	seq_printf(p, "%s-%d", dev_name(gb->gc.parent), spacemit_gpio_bank_index(gb));
}

static struct irq_chip spacemit_gpio_chip = {
	.name		= "k1-gpio-irqchip",
	.irq_ack	= spacemit_gpio_irq_ack,
	.irq_mask	= spacemit_gpio_irq_mask,
	.irq_unmask	= spacemit_gpio_irq_unmask,
	.irq_set_type	= spacemit_gpio_irq_set_type,
	.irq_print_chip	= spacemit_gpio_irq_print_chip,
	.flags		= IRQCHIP_IMMUTABLE | IRQCHIP_SKIP_SET_WAKE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static bool spacemit_of_node_instance_match(struct gpio_chip *gc, unsigned int i)
{
	struct spacemit_gpio_bank *gb = gpiochip_get_data(gc);
	struct spacemit_gpio *sg = gb->sg;

	if (i >= SPACEMIT_NR_BANKS)
		return false;

	return (gc == &sg->sgb[i].gc);
}

static int spacemit_gpio_add_bank(struct spacemit_gpio *sg,
				  void __iomem *regs,
				  int index, int irq)
{
	struct spacemit_gpio_bank *gb = &sg->sgb[index];
	struct gpio_chip *gc = &gb->gc;
	struct device *dev = sg->dev;
	struct gpio_irq_chip *girq;
	void __iomem *dat, *set, *clr, *dirin, *dirout;
	int ret, bank_base[] = { 0x0, 0x4, 0x8, 0x100 };

	gb->base = regs + bank_base[index];

	dat	= gb->base + SPACEMIT_GPLR;
	set	= gb->base + SPACEMIT_GPSR;
	clr	= gb->base + SPACEMIT_GPCR;
	dirin	= gb->base + SPACEMIT_GCDR;
	dirout	= gb->base + SPACEMIT_GSDR;

	/* This registers 32 GPIO lines per bank */
	ret = bgpio_init(gc, dev, 4, dat, set, clr, dirout, dirin,
			 BGPIOF_UNREADABLE_REG_SET | BGPIOF_UNREADABLE_REG_DIR);
	if (ret)
		return dev_err_probe(dev, ret, "failed to init gpio chip\n");

	gb->sg = sg;

	gc->label		= dev_name(dev);
	gc->request		= gpiochip_generic_request;
	gc->free		= gpiochip_generic_free;
	gc->ngpio		= SPACEMIT_NR_GPIOS_PER_BANK;
	gc->base		= -1;
	gc->of_gpio_n_cells	= 3;
	gc->of_node_instance_match = spacemit_of_node_instance_match;

	girq			= &gc->irq;
	girq->threaded		= true;
	girq->handler		= handle_simple_irq;

	gpio_irq_chip_set_chip(girq, &spacemit_gpio_chip);

	/* Disable Interrupt */
	writel(0, gb->base + SPACEMIT_GAPMASK);
	/* Disable Edge Detection Settings */
	writel(0x0, gb->base + SPACEMIT_GRER);
	writel(0x0, gb->base + SPACEMIT_GFER);
	/* Clear Interrupt */
	writel(0xffffffff, gb->base + SPACEMIT_GCRER);
	writel(0xffffffff, gb->base + SPACEMIT_GCFER);

	ret = devm_request_threaded_irq(dev, irq, NULL,
					spacemit_gpio_irq_handler,
					IRQF_ONESHOT | IRQF_SHARED,
					gb->gc.label, gb);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to register IRQ\n");

	ret = devm_gpiochip_add_data(dev, gc, gb);
	if (ret)
		return ret;

	/* Distuingish IRQ domain, for selecting threecells mode */
	irq_domain_update_bus_token(girq->domain, DOMAIN_BUS_WIRED);

	return 0;
}

static int spacemit_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spacemit_gpio *sg;
	struct clk *core_clk, *bus_clk;
	void __iomem *regs;
	int i, irq, ret;

	sg = devm_kzalloc(dev, sizeof(*sg), GFP_KERNEL);
	if (!sg)
		return -ENOMEM;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	sg->dev	= dev;

	core_clk = devm_clk_get_enabled(dev, "core");
	if (IS_ERR(core_clk))
		return dev_err_probe(dev, PTR_ERR(core_clk), "failed to get clock\n");

	bus_clk = devm_clk_get_enabled(dev, "bus");
	if (IS_ERR(bus_clk))
		return dev_err_probe(dev, PTR_ERR(bus_clk), "failed to get bus clock\n");

	for (i = 0; i < SPACEMIT_NR_BANKS; i++) {
		ret = spacemit_gpio_add_bank(sg, regs, i, irq);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct of_device_id spacemit_gpio_dt_ids[] = {
	{ .compatible = "spacemit,k1-gpio" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, spacemit_gpio_dt_ids);

static struct platform_driver spacemit_gpio_driver = {
	.probe		= spacemit_gpio_probe,
	.driver		= {
		.name	= "k1-gpio",
		.of_match_table = spacemit_gpio_dt_ids,
	},
};
module_platform_driver(spacemit_gpio_driver);

MODULE_AUTHOR("Yixun Lan <dlan@gentoo.org>");
MODULE_DESCRIPTION("GPIO driver for SpacemiT K1 SoC");
MODULE_LICENSE("GPL");
