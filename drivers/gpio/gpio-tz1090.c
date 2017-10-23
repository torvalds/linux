/*
 * Toumaz Xenif TZ1090 GPIO handling.
 *
 * Copyright (C) 2008-2013 Imagination Technologies Ltd.
 *
 *  Based on ARM PXA code and others.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/export.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>
#include <asm/global_lock.h>

/* Register offsets from bank base address */
#define REG_GPIO_DIR		0x00
#define REG_GPIO_IRQ_PLRT	0x20
#define REG_GPIO_IRQ_TYPE	0x30
#define REG_GPIO_IRQ_EN		0x40
#define REG_GPIO_IRQ_STS	0x50
#define REG_GPIO_BIT_EN		0x60
#define REG_GPIO_DIN		0x70
#define REG_GPIO_DOUT		0x80

/* REG_GPIO_IRQ_PLRT */
#define REG_GPIO_IRQ_PLRT_LOW	0
#define REG_GPIO_IRQ_PLRT_HIGH	1

/* REG_GPIO_IRQ_TYPE */
#define REG_GPIO_IRQ_TYPE_LEVEL	0
#define REG_GPIO_IRQ_TYPE_EDGE	1

/**
 * struct tz1090_gpio_bank - GPIO bank private data
 * @chip:	Generic GPIO chip for GPIO bank
 * @domain:	IRQ domain for GPIO bank (may be NULL)
 * @reg:	Base of registers, offset for this GPIO bank
 * @irq:	IRQ number for GPIO bank
 * @label:	Debug GPIO bank label, used for storage of chip->label
 *
 * This is the main private data for a GPIO bank. It encapsulates a gpio_chip,
 * and the callbacks for the gpio_chip can access the private data with the
 * to_bank() macro below.
 */
struct tz1090_gpio_bank {
	struct gpio_chip chip;
	struct irq_domain *domain;
	void __iomem *reg;
	int irq;
	char label[16];
};

/**
 * struct tz1090_gpio - Overall GPIO device private data
 * @dev:	Device (from platform device)
 * @reg:	Base of GPIO registers
 *
 * Represents the overall GPIO device. This structure is actually only
 * temporary, and used during init.
 */
struct tz1090_gpio {
	struct device *dev;
	void __iomem *reg;
};

/**
 * struct tz1090_gpio_bank_info - Temporary registration info for GPIO bank
 * @priv:	Overall GPIO device private data
 * @node:	Device tree node specific to this GPIO bank
 * @index:	Index of bank in range 0-2
 */
struct tz1090_gpio_bank_info {
	struct tz1090_gpio *priv;
	struct device_node *node;
	unsigned int index;
};

/* Convenience register accessors */
static inline void tz1090_gpio_write(struct tz1090_gpio_bank *bank,
			      unsigned int reg_offs, u32 data)
{
	iowrite32(data, bank->reg + reg_offs);
}

static inline u32 tz1090_gpio_read(struct tz1090_gpio_bank *bank,
			    unsigned int reg_offs)
{
	return ioread32(bank->reg + reg_offs);
}

/* caller must hold LOCK2 */
static inline void _tz1090_gpio_clear_bit(struct tz1090_gpio_bank *bank,
					  unsigned int reg_offs,
					  unsigned int offset)
{
	u32 value;

	value = tz1090_gpio_read(bank, reg_offs);
	value &= ~BIT(offset);
	tz1090_gpio_write(bank, reg_offs, value);
}

static void tz1090_gpio_clear_bit(struct tz1090_gpio_bank *bank,
				  unsigned int reg_offs,
				  unsigned int offset)
{
	int lstat;

	__global_lock2(lstat);
	_tz1090_gpio_clear_bit(bank, reg_offs, offset);
	__global_unlock2(lstat);
}

/* caller must hold LOCK2 */
static inline void _tz1090_gpio_set_bit(struct tz1090_gpio_bank *bank,
					unsigned int reg_offs,
					unsigned int offset)
{
	u32 value;

	value = tz1090_gpio_read(bank, reg_offs);
	value |= BIT(offset);
	tz1090_gpio_write(bank, reg_offs, value);
}

static void tz1090_gpio_set_bit(struct tz1090_gpio_bank *bank,
				unsigned int reg_offs,
				unsigned int offset)
{
	int lstat;

	__global_lock2(lstat);
	_tz1090_gpio_set_bit(bank, reg_offs, offset);
	__global_unlock2(lstat);
}

/* caller must hold LOCK2 */
static inline void _tz1090_gpio_mod_bit(struct tz1090_gpio_bank *bank,
					unsigned int reg_offs,
					unsigned int offset,
					bool val)
{
	u32 value;

	value = tz1090_gpio_read(bank, reg_offs);
	value &= ~BIT(offset);
	if (val)
		value |= BIT(offset);
	tz1090_gpio_write(bank, reg_offs, value);
}

static void tz1090_gpio_mod_bit(struct tz1090_gpio_bank *bank,
				unsigned int reg_offs,
				unsigned int offset,
				bool val)
{
	int lstat;

	__global_lock2(lstat);
	_tz1090_gpio_mod_bit(bank, reg_offs, offset, val);
	__global_unlock2(lstat);
}

static inline int tz1090_gpio_read_bit(struct tz1090_gpio_bank *bank,
				       unsigned int reg_offs,
				       unsigned int offset)
{
	return tz1090_gpio_read(bank, reg_offs) & BIT(offset);
}

/* GPIO chip callbacks */

static int tz1090_gpio_direction_input(struct gpio_chip *chip,
				       unsigned int offset)
{
	struct tz1090_gpio_bank *bank = gpiochip_get_data(chip);
	tz1090_gpio_set_bit(bank, REG_GPIO_DIR, offset);

	return 0;
}

static int tz1090_gpio_direction_output(struct gpio_chip *chip,
					unsigned int offset, int output_value)
{
	struct tz1090_gpio_bank *bank = gpiochip_get_data(chip);
	int lstat;

	__global_lock2(lstat);
	_tz1090_gpio_mod_bit(bank, REG_GPIO_DOUT, offset, output_value);
	_tz1090_gpio_clear_bit(bank, REG_GPIO_DIR, offset);
	__global_unlock2(lstat);

	return 0;
}

/*
 * Return GPIO level
 */
static int tz1090_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct tz1090_gpio_bank *bank = gpiochip_get_data(chip);

	return !!tz1090_gpio_read_bit(bank, REG_GPIO_DIN, offset);
}

/*
 * Set output GPIO level
 */
static void tz1090_gpio_set(struct gpio_chip *chip, unsigned int offset,
			    int output_value)
{
	struct tz1090_gpio_bank *bank = gpiochip_get_data(chip);

	tz1090_gpio_mod_bit(bank, REG_GPIO_DOUT, offset, output_value);
}

static int tz1090_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	struct tz1090_gpio_bank *bank = gpiochip_get_data(chip);
	int ret;

	ret = pinctrl_request_gpio(chip->base + offset);
	if (ret)
		return ret;

	tz1090_gpio_set_bit(bank, REG_GPIO_DIR, offset);
	tz1090_gpio_set_bit(bank, REG_GPIO_BIT_EN, offset);

	return 0;
}

static void tz1090_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	struct tz1090_gpio_bank *bank = gpiochip_get_data(chip);

	pinctrl_free_gpio(chip->base + offset);

	tz1090_gpio_clear_bit(bank, REG_GPIO_BIT_EN, offset);
}

static int tz1090_gpio_to_irq(struct gpio_chip *chip, unsigned int offset)
{
	struct tz1090_gpio_bank *bank = gpiochip_get_data(chip);

	if (!bank->domain)
		return -EINVAL;

	return irq_create_mapping(bank->domain, offset);
}

/* IRQ chip handlers */

/* Get TZ1090 GPIO chip from irq data provided to generic IRQ callbacks */
static inline struct tz1090_gpio_bank *irqd_to_gpio_bank(struct irq_data *data)
{
	return (struct tz1090_gpio_bank *)data->domain->host_data;
}

static void tz1090_gpio_irq_polarity(struct tz1090_gpio_bank *bank,
				     unsigned int offset, unsigned int polarity)
{
	tz1090_gpio_mod_bit(bank, REG_GPIO_IRQ_PLRT, offset, polarity);
}

static void tz1090_gpio_irq_type(struct tz1090_gpio_bank *bank,
				 unsigned int offset, unsigned int type)
{
	tz1090_gpio_mod_bit(bank, REG_GPIO_IRQ_TYPE, offset, type);
}

/* set polarity to trigger on next edge, whether rising or falling */
static void tz1090_gpio_irq_next_edge(struct tz1090_gpio_bank *bank,
				      unsigned int offset)
{
	unsigned int value_p, value_i;
	int lstat;

	/*
	 * Set the GPIO's interrupt polarity to the opposite of the current
	 * input value so that the next edge triggers an interrupt.
	 */
	__global_lock2(lstat);
	value_i = ~tz1090_gpio_read(bank, REG_GPIO_DIN);
	value_p = tz1090_gpio_read(bank, REG_GPIO_IRQ_PLRT);
	value_p &= ~BIT(offset);
	value_p |= value_i & BIT(offset);
	tz1090_gpio_write(bank, REG_GPIO_IRQ_PLRT, value_p);
	__global_unlock2(lstat);
}

static unsigned int gpio_startup_irq(struct irq_data *data)
{
	/*
	 * This warning indicates that the type of the irq hasn't been set
	 * before enabling the irq. This would normally be done by passing some
	 * trigger flags to request_irq().
	 */
	WARN(irqd_get_trigger_type(data) == IRQ_TYPE_NONE,
		"irq type not set before enabling gpio irq %d", data->irq);

	irq_gc_ack_clr_bit(data);
	irq_gc_mask_set_bit(data);
	return 0;
}

static int gpio_set_irq_type(struct irq_data *data, unsigned int flow_type)
{
	struct tz1090_gpio_bank *bank = irqd_to_gpio_bank(data);
	unsigned int type;
	unsigned int polarity;

	switch (flow_type) {
	case IRQ_TYPE_EDGE_BOTH:
		type = REG_GPIO_IRQ_TYPE_EDGE;
		polarity = REG_GPIO_IRQ_PLRT_LOW;
		break;
	case IRQ_TYPE_EDGE_RISING:
		type = REG_GPIO_IRQ_TYPE_EDGE;
		polarity = REG_GPIO_IRQ_PLRT_HIGH;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		type = REG_GPIO_IRQ_TYPE_EDGE;
		polarity = REG_GPIO_IRQ_PLRT_LOW;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		type = REG_GPIO_IRQ_TYPE_LEVEL;
		polarity = REG_GPIO_IRQ_PLRT_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		type = REG_GPIO_IRQ_TYPE_LEVEL;
		polarity = REG_GPIO_IRQ_PLRT_LOW;
		break;
	default:
		return -EINVAL;
	}

	tz1090_gpio_irq_type(bank, data->hwirq, type);
	irq_setup_alt_chip(data, flow_type);

	if (flow_type == IRQ_TYPE_EDGE_BOTH)
		tz1090_gpio_irq_next_edge(bank, data->hwirq);
	else
		tz1090_gpio_irq_polarity(bank, data->hwirq, polarity);

	return 0;
}

#ifdef CONFIG_SUSPEND
static int gpio_set_irq_wake(struct irq_data *data, unsigned int on)
{
	struct tz1090_gpio_bank *bank = irqd_to_gpio_bank(data);

#ifdef CONFIG_PM_DEBUG
	pr_info("irq_wake irq%d state:%d\n", data->irq, on);
#endif

	/* wake on gpio block interrupt */
	return irq_set_irq_wake(bank->irq, on);
}
#else
#define gpio_set_irq_wake NULL
#endif

static void tz1090_gpio_irq_handler(struct irq_desc *desc)
{
	irq_hw_number_t hw;
	unsigned int irq_stat, irq_no;
	struct tz1090_gpio_bank *bank;
	struct irq_desc *child_desc;

	bank = (struct tz1090_gpio_bank *)irq_desc_get_handler_data(desc);
	irq_stat = tz1090_gpio_read(bank, REG_GPIO_DIR) &
		   tz1090_gpio_read(bank, REG_GPIO_IRQ_STS) &
		   tz1090_gpio_read(bank, REG_GPIO_IRQ_EN) &
		   0x3FFFFFFF; /* 30 bits only */

	for (hw = 0; irq_stat; irq_stat >>= 1, ++hw) {
		if (!(irq_stat & 1))
			continue;

		irq_no = irq_linear_revmap(bank->domain, hw);
		child_desc = irq_to_desc(irq_no);

		/* Toggle edge for pin with both edges triggering enabled */
		if (irqd_get_trigger_type(&child_desc->irq_data)
				== IRQ_TYPE_EDGE_BOTH)
			tz1090_gpio_irq_next_edge(bank, hw);

		generic_handle_irq_desc(child_desc);
	}
}

static int tz1090_gpio_bank_probe(struct tz1090_gpio_bank_info *info)
{
	struct device_node *np = info->node;
	struct device *dev = info->priv->dev;
	struct tz1090_gpio_bank *bank;
	struct irq_chip_generic *gc;
	int err;

	bank = devm_kzalloc(dev, sizeof(*bank), GFP_KERNEL);
	if (!bank) {
		dev_err(dev, "unable to allocate driver data\n");
		return -ENOMEM;
	}

	/* Offset the main registers to the first register in this bank */
	bank->reg = info->priv->reg + info->index * 4;

	/* Set up GPIO chip */
	snprintf(bank->label, sizeof(bank->label), "tz1090-gpio-%u",
		 info->index);
	bank->chip.label		= bank->label;
	bank->chip.parent		= dev;
	bank->chip.direction_input	= tz1090_gpio_direction_input;
	bank->chip.direction_output	= tz1090_gpio_direction_output;
	bank->chip.get			= tz1090_gpio_get;
	bank->chip.set			= tz1090_gpio_set;
	bank->chip.free			= tz1090_gpio_free;
	bank->chip.request		= tz1090_gpio_request;
	bank->chip.to_irq		= tz1090_gpio_to_irq;
	bank->chip.of_node		= np;

	/* GPIO numbering from 0 */
	bank->chip.base			= info->index * 30;
	bank->chip.ngpio		= 30;

	/* Add the GPIO bank */
	gpiochip_add_data(&bank->chip, bank);

	/* Get the GPIO bank IRQ if provided */
	bank->irq = irq_of_parse_and_map(np, 0);

	/* The interrupt is optional (it may be used by another core on chip) */
	if (!bank->irq) {
		dev_info(dev, "IRQ not provided for bank %u, IRQs disabled\n",
			 info->index);
		return 0;
	}

	dev_info(dev, "Setting up IRQs for GPIO bank %u\n",
		 info->index);

	/*
	 * Initialise all interrupts to disabled so we don't get
	 * spurious ones on a dirty boot and hit the BUG_ON in the
	 * handler.
	 */
	tz1090_gpio_write(bank, REG_GPIO_IRQ_EN, 0);

	/* Add a virtual IRQ for each GPIO */
	bank->domain = irq_domain_add_linear(np,
					     bank->chip.ngpio,
					     &irq_generic_chip_ops,
					     bank);

	/* Set up a generic irq chip with 2 chip types (level and edge) */
	err = irq_alloc_domain_generic_chips(bank->domain, bank->chip.ngpio, 2,
					     bank->label, handle_bad_irq, 0, 0,
					     IRQ_GC_INIT_NESTED_LOCK);
	if (err) {
		dev_info(dev,
			 "irq_alloc_domain_generic_chips failed for bank %u, IRQs disabled\n",
			 info->index);
		irq_domain_remove(bank->domain);
		return 0;
	}

	gc = irq_get_domain_generic_chip(bank->domain, 0);
	gc->reg_base	= bank->reg;

	/* level chip type */
	gc->chip_types[0].type			= IRQ_TYPE_LEVEL_MASK;
	gc->chip_types[0].handler		= handle_level_irq;
	gc->chip_types[0].regs.ack		= REG_GPIO_IRQ_STS;
	gc->chip_types[0].regs.mask		= REG_GPIO_IRQ_EN;
	gc->chip_types[0].chip.irq_startup	= gpio_startup_irq;
	gc->chip_types[0].chip.irq_ack		= irq_gc_ack_clr_bit;
	gc->chip_types[0].chip.irq_mask		= irq_gc_mask_clr_bit;
	gc->chip_types[0].chip.irq_unmask	= irq_gc_mask_set_bit;
	gc->chip_types[0].chip.irq_set_type	= gpio_set_irq_type;
	gc->chip_types[0].chip.irq_set_wake	= gpio_set_irq_wake;
	gc->chip_types[0].chip.flags		= IRQCHIP_MASK_ON_SUSPEND;

	/* edge chip type */
	gc->chip_types[1].type			= IRQ_TYPE_EDGE_BOTH;
	gc->chip_types[1].handler		= handle_edge_irq;
	gc->chip_types[1].regs.ack		= REG_GPIO_IRQ_STS;
	gc->chip_types[1].regs.mask		= REG_GPIO_IRQ_EN;
	gc->chip_types[1].chip.irq_startup	= gpio_startup_irq;
	gc->chip_types[1].chip.irq_ack		= irq_gc_ack_clr_bit;
	gc->chip_types[1].chip.irq_mask		= irq_gc_mask_clr_bit;
	gc->chip_types[1].chip.irq_unmask	= irq_gc_mask_set_bit;
	gc->chip_types[1].chip.irq_set_type	= gpio_set_irq_type;
	gc->chip_types[1].chip.irq_set_wake	= gpio_set_irq_wake;
	gc->chip_types[1].chip.flags		= IRQCHIP_MASK_ON_SUSPEND;

	/* Setup chained handler for this GPIO bank */
	irq_set_chained_handler_and_data(bank->irq, tz1090_gpio_irq_handler,
					 bank);

	return 0;
}

static void tz1090_gpio_register_banks(struct tz1090_gpio *priv)
{
	struct device_node *np = priv->dev->of_node;
	struct device_node *node;

	for_each_available_child_of_node(np, node) {
		struct tz1090_gpio_bank_info info;
		u32 addr;
		int ret;

		ret = of_property_read_u32(node, "reg", &addr);
		if (ret) {
			dev_err(priv->dev, "invalid reg on %pOF\n", node);
			continue;
		}
		if (addr >= 3) {
			dev_err(priv->dev, "index %u in %pOF out of range\n",
				addr, node);
			continue;
		}

		info.index = addr;
		info.node = of_node_get(node);
		info.priv = priv;

		ret = tz1090_gpio_bank_probe(&info);
		if (ret) {
			dev_err(priv->dev, "failure registering %pOF\n", node);
			of_node_put(node);
			continue;
		}
	}
}

static int tz1090_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res_regs;
	struct tz1090_gpio priv;

	if (!np) {
		dev_err(&pdev->dev, "must be instantiated via devicetree\n");
		return -ENOENT;
	}

	res_regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_regs) {
		dev_err(&pdev->dev, "cannot find registers resource\n");
		return -ENOENT;
	}

	priv.dev = &pdev->dev;

	/* Ioremap the registers */
	priv.reg = devm_ioremap(&pdev->dev, res_regs->start,
				resource_size(res_regs));
	if (!priv.reg) {
		dev_err(&pdev->dev, "unable to ioremap registers\n");
		return -ENOMEM;
	}

	/* Look for banks */
	tz1090_gpio_register_banks(&priv);

	return 0;
}

static struct of_device_id tz1090_gpio_of_match[] = {
	{ .compatible = "img,tz1090-gpio" },
	{ },
};

static struct platform_driver tz1090_gpio_driver = {
	.driver = {
		.name		= "tz1090-gpio",
		.of_match_table	= tz1090_gpio_of_match,
	},
	.probe		= tz1090_gpio_probe,
};

static int __init tz1090_gpio_init(void)
{
	return platform_driver_register(&tz1090_gpio_driver);
}
subsys_initcall(tz1090_gpio_init);
