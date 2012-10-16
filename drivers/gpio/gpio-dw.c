/*
 * Designware GPIO support functions
 *
 * Copyright (C) 2012 Altera
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/platform_data/gpio-dw.h>

#define GPIO_INT_EN_REG_OFFSET 		(0x30)
#define GPIO_INT_MASK_REG_OFFSET 	(0x34)
#define GPIO_INT_TYPE_LEVEL_REG_OFFSET 	(0x38)
#define GPIO_INT_POLARITY_REG_OFFSET 	(0x3c)
#define GPIO_INT_STATUS_REG_OFFSET 	(0x40)
#define GPIO_PORT_A_EOI_REG_OFFSET 	(0x4c)

#define GPIO_DDR_OFFSET_PORT(p) 	(0x4  + ((p) * 0xc))
#define DW_GPIO_EXT(p) 			(0x50 + ((p) * 0x4))
#define DW_GPIO_DR(p) 			(0x0  + ((p) * 0xc))

#define CHIP_BASE (-1)

struct gpio_bank {
	u32 irq;
	u32 virtual_irq_start;
	spinlock_t lock;
	u32 width;
	u32 porta_width;
	u32 portb_width;
	u32 portc_width;
	u32 portd_width;
	void __iomem *regs;
	struct gpio_chip chip;
	struct device *dev;
};

static void dw_gpio_irq_disable(struct irq_data *d)
{
	struct gpio_bank *bank = irq_data_get_irq_chip_data(d);
	u32 gpio_irq_number = d->irq - bank->virtual_irq_start;
	unsigned long flags;
	u32 port_inten;

	spin_lock_irqsave(&bank->lock, flags);
	port_inten = readl(bank->regs + GPIO_INT_EN_REG_OFFSET);
	port_inten &= ~(1 << gpio_irq_number);
	writel(port_inten, bank->regs + GPIO_INT_EN_REG_OFFSET);
	spin_unlock_irqrestore(&bank->lock, flags);
}

static void dw_gpio_irq_enable(struct irq_data *d) {
	struct gpio_bank *bank = irq_data_get_irq_chip_data(d);
	u32 gpio_irq_number = d->irq - bank->virtual_irq_start;
	unsigned long flags;
	u32 port_inten;

	spin_lock_irqsave(&bank->lock, flags);
	port_inten = readl(bank->regs + GPIO_INT_EN_REG_OFFSET);
	port_inten |= (1 << gpio_irq_number);
	writel(port_inten, bank->regs + GPIO_INT_EN_REG_OFFSET);
	spin_unlock_irqrestore(&bank->lock, flags);
}

static void dw_gpio_irq_unmask(struct irq_data *d) {
	struct gpio_bank *bank = irq_data_get_irq_chip_data(d);
	u32 gpio_irq_number = d->irq - bank->virtual_irq_start;
	unsigned long flags;
	u32 intmask;

	spin_lock_irqsave(&bank->lock, flags);
	intmask = readl(bank->regs + GPIO_INT_MASK_REG_OFFSET);
	intmask &= ~(1 << gpio_irq_number);
	writel(intmask, bank->regs + GPIO_INT_MASK_REG_OFFSET);
	spin_unlock_irqrestore(&bank->lock, flags);
}

static void dw_gpio_irq_mask(struct irq_data *d) {
	struct gpio_bank *bank = irq_data_get_irq_chip_data(d);
	u32 gpio_irq_number = d->irq - bank->virtual_irq_start;
	unsigned long flags;
	u32 intmask;

	spin_lock_irqsave(&bank->lock, flags);
	intmask = readl(bank->regs + GPIO_INT_MASK_REG_OFFSET);
	intmask |= (1 << gpio_irq_number);
	writel(intmask, bank->regs + GPIO_INT_MASK_REG_OFFSET);
	spin_unlock_irqrestore(&bank->lock, flags);
}

static void dw_gpio_irq_ack(struct irq_data *d) {
	struct gpio_bank *bank = irq_data_get_irq_chip_data(d);
	u32 gpio_irq_number = d->irq - bank->virtual_irq_start;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&bank->lock, flags);
	val = readl(bank->regs + GPIO_PORT_A_EOI_REG_OFFSET);
	val |= (1 << gpio_irq_number);
	writel(val, bank->regs + GPIO_PORT_A_EOI_REG_OFFSET);
	spin_unlock_irqrestore(&bank->lock, flags);
}

static int dw_gpio_irq_set_type(struct irq_data *d,
				unsigned int type)
{
	struct gpio_bank *bank = irq_data_get_irq_chip_data(d);
	u32 gpio_irq_number = d->irq - bank->virtual_irq_start;
	u32 level, polarity;
	u32 intmask;
	unsigned long flags;

	spin_lock_irqsave(&bank->lock, flags);

	intmask = readl(bank->regs + GPIO_INT_MASK_REG_OFFSET);
	writel(~(1 << gpio_irq_number) & (intmask),
		bank->regs + GPIO_INT_MASK_REG_OFFSET);

	level = readl(bank->regs + GPIO_INT_TYPE_LEVEL_REG_OFFSET);
	polarity = readl(bank->regs + GPIO_INT_POLARITY_REG_OFFSET);

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		level 		|= (1 << gpio_irq_number);
		polarity 	|= (1 << gpio_irq_number);
		break;

	case IRQ_TYPE_EDGE_FALLING:
		level 		|= (1 << gpio_irq_number);
		polarity 	&= ~(1 << gpio_irq_number);
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		level 		&= ~(1 << gpio_irq_number);
		polarity 	|= (1 << gpio_irq_number);
		break;

	case IRQ_TYPE_LEVEL_LOW:
		level	 	&= ~(1 << gpio_irq_number);
		polarity 	&= ~(1 << gpio_irq_number);
		break;

	default:
		writel(intmask, bank->regs + GPIO_INT_MASK_REG_OFFSET);
		return -EINVAL;
	}

	writel(level, bank->regs + GPIO_INT_TYPE_LEVEL_REG_OFFSET);
	writel(polarity, bank->regs + GPIO_INT_POLARITY_REG_OFFSET);
	writel(intmask, bank->regs + GPIO_INT_MASK_REG_OFFSET);
	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static struct irq_chip gpio_irq_chip = {
	.name		= "GPIO",
	.irq_enable	= dw_gpio_irq_enable,
	.irq_disable	= dw_gpio_irq_disable,
	.irq_ack	= dw_gpio_irq_ack,
	.irq_mask	= dw_gpio_irq_mask,
	.irq_unmask	= dw_gpio_irq_unmask,
	.irq_set_type	= dw_gpio_irq_set_type,
};

enum DW_GPIO_PORT {
	PORTA = 0,
	PORTB = 1,
	PORTC = 2,
	PORTD = 3,
	PORT_INVALID,
};

static int get_port(struct gpio_bank *bank, unsigned offset) {
	int port_offset = offset;
	port_offset -= bank->porta_width;
	if (port_offset < 0)
		return PORTA;

	port_offset -= bank->portb_width;
	if (port_offset < 0)
		return PORTB;

	port_offset -= bank->portc_width;
	if (port_offset < 0)
		return PORTC;

	port_offset -= bank->portd_width;
	if (port_offset < 0)
		return PORTD;

	dev_warn(bank->dev, "Invalid offset specified\n");
	return PORT_INVALID;
}

static int dw_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_bank *bank = container_of(chip, struct gpio_bank, chip);
	enum DW_GPIO_PORT port = get_port(bank, offset);

	if(port == PORT_INVALID)
		return 0;

	return (readl(bank->regs + DW_GPIO_EXT(port)) >> offset) & 1;
}

static void dw_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct gpio_bank *bank = container_of(chip, struct gpio_bank, chip);
	enum DW_GPIO_PORT port = get_port(bank, offset);
	unsigned long flags;
	u32 data_reg;

	if(port == PORT_INVALID)
		return;

	spin_lock_irqsave(&bank->lock, flags);
	data_reg = readl(bank->regs + DW_GPIO_DR(port));
	data_reg = (data_reg & ~(1<<offset)) | (!!value << offset);
	writel(data_reg, bank->regs + DW_GPIO_DR(port));
	spin_unlock_irqrestore(&bank->lock, flags);
}

static int dw_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_bank *bank = container_of(chip, struct gpio_bank, chip);
	unsigned long flags;
	u32 gpio_ddr;
	enum DW_GPIO_PORT port = get_port(bank, offset);

	if(port == PORT_INVALID)
		return -EINVAL;

	spin_lock_irqsave(&bank->lock, flags);
	/* Set pin as input, assumes software controlled IP */
	gpio_ddr = readl(bank->regs + GPIO_DDR_OFFSET_PORT(port));
	gpio_ddr &= ~(1 << offset);
	writel(gpio_ddr, bank->regs + GPIO_DDR_OFFSET_PORT(port));
	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static int dw_gpio_direction_output(struct gpio_chip *chip,
		unsigned offset, int value)
{
	struct gpio_bank *bank = container_of(chip, struct gpio_bank, chip);
	unsigned long flags;
	u32 gpio_ddr;
	enum DW_GPIO_PORT port = get_port(bank, offset);

	if(port == PORT_INVALID)
		return -EINVAL;

	dw_gpio_set(chip, offset, value);

	spin_lock_irqsave(&bank->lock, flags);
	/* Set pin as output, assumes software controlled IP */
	gpio_ddr = readl(bank->regs + GPIO_DDR_OFFSET_PORT(port));
	gpio_ddr |= (1 << offset);
	writel(gpio_ddr, bank->regs + GPIO_DDR_OFFSET_PORT(port));
	spin_unlock_irqrestore(&bank->lock, flags);

	return 0;
}

static int dw_gpio_to_irq(struct gpio_chip *chip, unsigned offset) {
	struct gpio_bank *bank = container_of(chip, struct gpio_bank, chip);

	return bank->virtual_irq_start + offset;
}

static void gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct gpio_bank *bank = irq_get_handler_data(irq);
	unsigned long status;

	int i;
	chip->irq_mask(&desc->irq_data);

	status = readl(bank->regs +
		GPIO_INT_STATUS_REG_OFFSET);

	for_each_set_bit(i, &status, bank->porta_width) {
		generic_handle_irq(bank->virtual_irq_start + i);
	}
	writel(status, bank->regs + GPIO_PORT_A_EOI_REG_OFFSET);
	chip->irq_eoi(irq_desc_get_irq_data(desc));
	chip->irq_unmask(&desc->irq_data);
}

static struct lock_class_key gpio_lock_class;

static void __devinit dw_gpio_chip_init(struct gpio_bank *bank, u32 gpio_base) {
	int i;

	bank->chip.direction_input	= dw_gpio_direction_input;
	bank->chip.direction_output	= dw_gpio_direction_output;
	bank->chip.get			= dw_gpio_get;
	bank->chip.set			= dw_gpio_set;
	bank->chip.to_irq		= dw_gpio_to_irq;
	bank->chip.owner		= THIS_MODULE;
	bank->chip.base			= gpio_base;
	bank->chip.ngpio		= bank->width;

	gpiochip_add(&bank->chip);

	for (i = bank->virtual_irq_start;
		i < bank->virtual_irq_start + bank->porta_width; i++) {
		irq_set_lockdep_class(i, &gpio_lock_class);
		irq_set_chip_data(i, bank);
		irq_set_chip(i, &gpio_irq_chip);
		irq_set_handler(i, handle_simple_irq);
		set_irq_flags(i, IRQF_VALID);
	}
	irq_set_chained_handler(bank->irq, gpio_irq_handler);
	irq_set_handler_data(bank->irq, bank);
}

static int __devinit dw_gpio_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct resource *mem;
	struct device_node *node = pdev->dev.of_node;
	int id;
	struct gpio_bank *bank = devm_kzalloc(&pdev->dev, sizeof(*bank),
					GFP_KERNEL);
	bank->dev = &pdev->dev;

	id = pdev->id;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (unlikely(!res)) {
		dev_err(&pdev->dev, "GPIO Bank %i has an Invalid IRQ Resource\n"
			, id);
		return -ENODEV;
	}

	bank->irq = res->start; /* IRQ base number */

	if (of_property_read_u32(node,
			"virtual_irq_start", &bank->virtual_irq_start)) {
		dev_err(&pdev->dev, "No virtual irq specified\n");
		return -EINVAL;
	}

	if (of_property_read_u32(node,
			"bank_width", &bank->width)) {
		dev_err(&pdev->dev, "Bank width not specified\n");
		return -EINVAL;
	}

	bank->porta_width = 0;
	bank->portb_width = 0;
	bank->portc_width = 0;
	bank->portd_width = 0;

	of_property_read_u32(node,
			"porta_width", &bank->porta_width);

	of_property_read_u32(node,
			"portb_width", &bank->portb_width);

	of_property_read_u32(node,
			"portc_width", &bank->portc_width);

	of_property_read_u32(node,
			"portd_width", &bank->portd_width);


	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!mem)) {
		dev_err(&pdev->dev,
			"GPIO Bank %i has an Invalid Memory Resource\n", id);
		return -ENODEV;
	}

	bank->regs = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
	if (unlikely(!bank->regs)) {
		dev_err(&pdev->dev,
			"Failed IO remap for GPIO resource\n");
		return -ENODEV;
	}

	bank->chip.of_node = of_node_get(node);
	irq_domain_add_legacy(node, bank->porta_width,
		bank->virtual_irq_start, 0, &irq_domain_simple_ops, NULL);

	dw_gpio_chip_init(bank, CHIP_BASE);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id dwgpio_match[] = {
	{.compatible = DW_GPIO_COMPATIBLE,},
	{}
};
MODULE_DEVICE_TABLE(of, dwgpio_match);
#else
#define dwgpio_match NULL
#endif

static struct platform_driver dw_gpio_driver = {
	.probe          = dw_gpio_probe,
	.driver         = {
		.owner  = THIS_MODULE,
		.name   = "dwgpio",
		.of_match_table = dwgpio_match,
	},
};

static int __init dwgpio_init(void)
{
	return platform_driver_register(&dw_gpio_driver);
}
core_initcall(dwgpio_init);
