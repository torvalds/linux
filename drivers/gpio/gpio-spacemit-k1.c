// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2023-2025 SpacemiT (Hangzhou) Technology Co. Ltd
 * Copyright (C) 2025 Yixun Lan <dlan@gentoo.org>
 */

#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/generic.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>

#define SPACEMIT_NR_BANKS		4
#define SPACEMIT_NR_GPIOS_PER_BANK	32

#define to_spacemit_gpio_bank(x) container_of((x), struct spacemit_gpio_bank, gc)
#define to_spacemit_gpio_regs(gb) ((gb)->sg->data->offsets)

enum spacemit_gpio_registers {
	SPACEMIT_GPLR,		/* port level - R */
	SPACEMIT_GPDR,		/* port direction - R/W */
	SPACEMIT_GPSR,		/* port set - W */
	SPACEMIT_GPCR,		/* port clear - W */
	SPACEMIT_GRER,		/* port rising edge R/W */
	SPACEMIT_GFER,		/* port falling edge R/W */
	SPACEMIT_GEDR,		/* edge detect status - R/W1C */
	SPACEMIT_GSDR,		/* (set) direction - W */
	SPACEMIT_GCDR,		/* (clear) direction - W */
	SPACEMIT_GSRER,		/* (set) rising edge detect enable - W */
	SPACEMIT_GCRER,		/* (clear) rising edge detect enable - W */
	SPACEMIT_GSFER,		/* (set) falling edge detect enable - W */
	SPACEMIT_GCFER,		/* (clear) falling edge detect enable - W */
	SPACEMIT_GAPMASK,	/* interrupt mask , 0 disable, 1 enable - R/W */
	SPACEMIT_GCPMASK,	/* interrupt mask for K3 */
};

struct spacemit_gpio;

struct spacemit_gpio_data {
	const unsigned int *offsets;
	u32 bank_offsets[SPACEMIT_NR_BANKS];
};

struct spacemit_gpio_bank {
	struct gpio_generic_chip chip;
	struct spacemit_gpio *sg;
	void __iomem *base;
	u32 irq_mask;
	u32 irq_rising_edge;
	u32 irq_falling_edge;
};

struct spacemit_gpio {
	struct device *dev;
	const struct spacemit_gpio_data *data;
	struct spacemit_gpio_bank sgb[SPACEMIT_NR_BANKS];
};

static u32 spacemit_gpio_read(struct spacemit_gpio_bank *gb,
			      enum spacemit_gpio_registers reg)
{
	return readl(gb->base + to_spacemit_gpio_regs(gb)[reg]);
}

static void spacemit_gpio_write(struct spacemit_gpio_bank *gb,
				enum spacemit_gpio_registers reg, u32 val)
{
	writel(val, gb->base + to_spacemit_gpio_regs(gb)[reg]);
}

static u32 spacemit_gpio_bank_index(struct spacemit_gpio_bank *gb)
{
	return (u32)(gb - gb->sg->sgb);
}

static irqreturn_t spacemit_gpio_irq_handler(int irq, void *dev_id)
{
	struct spacemit_gpio_bank *gb = dev_id;
	unsigned long pending;
	u32 n, gedr;

	gedr = spacemit_gpio_read(gb, SPACEMIT_GEDR);
	if (!gedr)
		return IRQ_NONE;
	spacemit_gpio_write(gb, SPACEMIT_GEDR, gedr);

	pending = gedr & gb->irq_mask;
	if (!pending)
		return IRQ_NONE;

	for_each_set_bit(n, &pending, BITS_PER_LONG)
		handle_nested_irq(irq_find_mapping(gb->chip.gc.irq.domain, n));

	return IRQ_HANDLED;
}

static void spacemit_gpio_irq_ack(struct irq_data *d)
{
	struct spacemit_gpio_bank *gb = irq_data_get_irq_chip_data(d);

	spacemit_gpio_write(gb, SPACEMIT_GEDR, BIT(irqd_to_hwirq(d)));
}

static void spacemit_gpio_irq_mask(struct irq_data *d)
{
	struct spacemit_gpio_bank *gb = irq_data_get_irq_chip_data(d);
	u32 bit = BIT(irqd_to_hwirq(d));

	gb->irq_mask &= ~bit;
	spacemit_gpio_write(gb, SPACEMIT_GAPMASK, gb->irq_mask);

	if (bit & gb->irq_rising_edge)
		spacemit_gpio_write(gb, SPACEMIT_GCRER, bit);

	if (bit & gb->irq_falling_edge)
		spacemit_gpio_write(gb, SPACEMIT_GCFER, bit);
}

static void spacemit_gpio_irq_unmask(struct irq_data *d)
{
	struct spacemit_gpio_bank *gb = irq_data_get_irq_chip_data(d);
	u32 bit = BIT(irqd_to_hwirq(d));

	gb->irq_mask |= bit;

	if (bit & gb->irq_rising_edge)
		spacemit_gpio_write(gb, SPACEMIT_GSRER, bit);

	if (bit & gb->irq_falling_edge)
		spacemit_gpio_write(gb, SPACEMIT_GSFER, bit);

	spacemit_gpio_write(gb, SPACEMIT_GAPMASK, gb->irq_mask);
}

static int spacemit_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct spacemit_gpio_bank *gb = irq_data_get_irq_chip_data(d);
	u32 bit = BIT(irqd_to_hwirq(d));

	if (type & IRQ_TYPE_EDGE_RISING) {
		gb->irq_rising_edge |= bit;
		spacemit_gpio_write(gb, SPACEMIT_GSRER, bit);
	} else {
		gb->irq_rising_edge &= ~bit;
		spacemit_gpio_write(gb, SPACEMIT_GCRER, bit);
	}

	if (type & IRQ_TYPE_EDGE_FALLING) {
		gb->irq_falling_edge |= bit;
		spacemit_gpio_write(gb, SPACEMIT_GSFER, bit);
	} else {
		gb->irq_falling_edge &= ~bit;
		spacemit_gpio_write(gb, SPACEMIT_GCFER, bit);
	}

	return 0;
}

static void spacemit_gpio_irq_print_chip(struct irq_data *data, struct seq_file *p)
{
	struct spacemit_gpio_bank *gb = irq_data_get_irq_chip_data(data);

	seq_printf(p, "%s-%d", dev_name(gb->chip.gc.parent), spacemit_gpio_bank_index(gb));
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

	return (gc == &sg->sgb[i].chip.gc);
}

static int spacemit_gpio_add_bank(struct spacemit_gpio *sg,
				  void __iomem *regs,
				  int index, int irq)
{
	struct spacemit_gpio_bank *gb = &sg->sgb[index];
	struct gpio_generic_chip_config config;
	struct gpio_chip *gc = &gb->chip.gc;
	struct device *dev = sg->dev;
	struct gpio_irq_chip *girq;
	void __iomem *dat, *set, *clr, *dirout;
	int ret;

	gb->base = regs + sg->data->bank_offsets[index];
	gb->sg = sg;

	dat	= gb->base + to_spacemit_gpio_regs(gb)[SPACEMIT_GPLR];
	set	= gb->base + to_spacemit_gpio_regs(gb)[SPACEMIT_GPSR];
	clr	= gb->base + to_spacemit_gpio_regs(gb)[SPACEMIT_GPCR];
	dirout	= gb->base + to_spacemit_gpio_regs(gb)[SPACEMIT_GPDR];

	config = (struct gpio_generic_chip_config) {
		.dev = dev,
		.sz = 4,
		.dat = dat,
		.set = set,
		.clr = clr,
		.dirout = dirout,
		.flags = GPIO_GENERIC_UNREADABLE_REG_SET,
	};

	/* This registers 32 GPIO lines per bank */
	ret = gpio_generic_chip_init(&gb->chip, &config);
	if (ret)
		return dev_err_probe(dev, ret, "failed to init gpio chip\n");

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
	spacemit_gpio_write(gb, SPACEMIT_GAPMASK, 0);
	/* Disable Edge Detection Settings */
	spacemit_gpio_write(gb, SPACEMIT_GRER, 0x0);
	spacemit_gpio_write(gb, SPACEMIT_GFER, 0x0);
	/* Clear Interrupt */
	spacemit_gpio_write(gb, SPACEMIT_GCRER, 0xffffffff);
	spacemit_gpio_write(gb, SPACEMIT_GCFER, 0xffffffff);

	ret = devm_request_threaded_irq(dev, irq, NULL,
					spacemit_gpio_irq_handler,
					IRQF_ONESHOT | IRQF_SHARED,
					gb->chip.gc.label, gb);
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

	sg->data = of_device_get_match_data(dev);
	if (!sg->data)
		return dev_err_probe(dev, -EINVAL, "No available compatible data.");

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

static const unsigned int spacemit_gpio_k1_offsets[] = {
	[SPACEMIT_GPLR] = 0x00,
	[SPACEMIT_GPDR] = 0x0c,
	[SPACEMIT_GPSR] = 0x18,
	[SPACEMIT_GPCR] = 0x24,
	[SPACEMIT_GRER] = 0x30,
	[SPACEMIT_GFER] = 0x3c,
	[SPACEMIT_GEDR] = 0x48,
	[SPACEMIT_GSDR] = 0x54,
	[SPACEMIT_GCDR] = 0x60,
	[SPACEMIT_GSRER] = 0x6c,
	[SPACEMIT_GCRER] = 0x78,
	[SPACEMIT_GSFER] = 0x84,
	[SPACEMIT_GCFER] = 0x90,
	[SPACEMIT_GAPMASK] = 0x9c,
	[SPACEMIT_GCPMASK] = 0xA8,
};

static const unsigned int spacemit_gpio_k3_offsets[] = {
	[SPACEMIT_GPLR] = 0x0,
	[SPACEMIT_GPDR] = 0x4,
	[SPACEMIT_GPSR] = 0x8,
	[SPACEMIT_GPCR] = 0xc,
	[SPACEMIT_GRER] = 0x10,
	[SPACEMIT_GFER] = 0x14,
	[SPACEMIT_GEDR] = 0x18,
	[SPACEMIT_GSDR] = 0x1c,
	[SPACEMIT_GCDR] = 0x20,
	[SPACEMIT_GSRER] = 0x24,
	[SPACEMIT_GCRER] = 0x28,
	[SPACEMIT_GSFER] = 0x2c,
	[SPACEMIT_GCFER] = 0x30,
	[SPACEMIT_GAPMASK] = 0x34,
	[SPACEMIT_GCPMASK] = 0x38,
};

static const struct spacemit_gpio_data k1_gpio_data = {
	.offsets = spacemit_gpio_k1_offsets,
	.bank_offsets = { 0x0, 0x4, 0x8, 0x100 },
};

static const struct spacemit_gpio_data k3_gpio_data = {
	.offsets = spacemit_gpio_k3_offsets,
	.bank_offsets = { 0x0, 0x40, 0x80, 0x100 },
};

static const struct of_device_id spacemit_gpio_dt_ids[] = {
	{ .compatible = "spacemit,k1-gpio", .data = &k1_gpio_data },
	{ .compatible = "spacemit,k3-gpio", .data = &k3_gpio_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, spacemit_gpio_dt_ids);

static struct platform_driver spacemit_gpio_driver = {
	.probe		= spacemit_gpio_probe,
	.driver		= {
		.name	= "spacemit-gpio",
		.of_match_table = spacemit_gpio_dt_ids,
	},
};
module_platform_driver(spacemit_gpio_driver);

MODULE_AUTHOR("Yixun Lan <dlan@gentoo.org>");
MODULE_DESCRIPTION("GPIO driver for SpacemiT K1/K3 SoC");
MODULE_LICENSE("GPL");
