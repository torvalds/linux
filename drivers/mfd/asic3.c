/*
 * driver/mfd/asic3.c
 *
 * Compaq ASIC3 support.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Copyright 2001 Compaq Computer Corporation.
 * Copyright 2004-2005 Phil Blundell
 * Copyright 2007-2008 OpenedHand Ltd.
 *
 * Authors: Phil Blundell <pb@handhelds.org>,
 *	    Samuel Ortiz <sameo@openedhand.com>
 *
 */

#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>

#include <linux/mfd/asic3.h>

struct asic3 {
	void __iomem *mapping;
	unsigned int bus_shift;
	unsigned int irq_nr;
	unsigned int irq_base;
	spinlock_t lock;
	u16 irq_bothedge[4];
	struct gpio_chip gpio;
	struct device *dev;
};

static int asic3_gpio_get(struct gpio_chip *chip, unsigned offset);

static inline void asic3_write_register(struct asic3 *asic,
				 unsigned int reg, u32 value)
{
	iowrite16(value, asic->mapping +
		  (reg >> asic->bus_shift));
}

static inline u32 asic3_read_register(struct asic3 *asic,
			       unsigned int reg)
{
	return ioread16(asic->mapping +
			(reg >> asic->bus_shift));
}

void asic3_set_register(struct asic3 *asic, u32 reg, u32 bits, bool set)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&asic->lock, flags);
	val = asic3_read_register(asic, reg);
	if (set)
		val |= bits;
	else
		val &= ~bits;
	asic3_write_register(asic, reg, val);
	spin_unlock_irqrestore(&asic->lock, flags);
}

/* IRQs */
#define MAX_ASIC_ISR_LOOPS    20
#define ASIC3_GPIO_BASE_INCR \
	(ASIC3_GPIO_B_BASE - ASIC3_GPIO_A_BASE)

static void asic3_irq_flip_edge(struct asic3 *asic,
				u32 base, int bit)
{
	u16 edge;
	unsigned long flags;

	spin_lock_irqsave(&asic->lock, flags);
	edge = asic3_read_register(asic,
				   base + ASIC3_GPIO_EDGE_TRIGGER);
	edge ^= bit;
	asic3_write_register(asic,
			     base + ASIC3_GPIO_EDGE_TRIGGER, edge);
	spin_unlock_irqrestore(&asic->lock, flags);
}

static void asic3_irq_demux(unsigned int irq, struct irq_desc *desc)
{
	int iter, i;
	unsigned long flags;
	struct asic3 *asic;

	desc->chip->ack(irq);

	asic = desc->handler_data;

	for (iter = 0 ; iter < MAX_ASIC_ISR_LOOPS; iter++) {
		u32 status;
		int bank;

		spin_lock_irqsave(&asic->lock, flags);
		status = asic3_read_register(asic,
					     ASIC3_OFFSET(INTR, P_INT_STAT));
		spin_unlock_irqrestore(&asic->lock, flags);

		/* Check all ten register bits */
		if ((status & 0x3ff) == 0)
			break;

		/* Handle GPIO IRQs */
		for (bank = 0; bank < ASIC3_NUM_GPIO_BANKS; bank++) {
			if (status & (1 << bank)) {
				unsigned long base, istat;

				base = ASIC3_GPIO_A_BASE
				       + bank * ASIC3_GPIO_BASE_INCR;

				spin_lock_irqsave(&asic->lock, flags);
				istat = asic3_read_register(asic,
							    base +
							    ASIC3_GPIO_INT_STATUS);
				/* Clearing IntStatus */
				asic3_write_register(asic,
						     base +
						     ASIC3_GPIO_INT_STATUS, 0);
				spin_unlock_irqrestore(&asic->lock, flags);

				for (i = 0; i < ASIC3_GPIOS_PER_BANK; i++) {
					int bit = (1 << i);
					unsigned int irqnr;

					if (!(istat & bit))
						continue;

					irqnr = asic->irq_base +
						(ASIC3_GPIOS_PER_BANK * bank)
						+ i;
					desc = irq_to_desc(irqnr);
					desc->handle_irq(irqnr, desc);
					if (asic->irq_bothedge[bank] & bit)
						asic3_irq_flip_edge(asic, base,
								    bit);
				}
			}
		}

		/* Handle remaining IRQs in the status register */
		for (i = ASIC3_NUM_GPIOS; i < ASIC3_NR_IRQS; i++) {
			/* They start at bit 4 and go up */
			if (status & (1 << (i - ASIC3_NUM_GPIOS + 4))) {
				desc = irq_to_desc(asic->irq_base + i);
				desc->handle_irq(asic->irq_base + i,
						 desc);
			}
		}
	}

	if (iter >= MAX_ASIC_ISR_LOOPS)
		dev_err(asic->dev, "interrupt processing overrun\n");
}

static inline int asic3_irq_to_bank(struct asic3 *asic, int irq)
{
	int n;

	n = (irq - asic->irq_base) >> 4;

	return (n * (ASIC3_GPIO_B_BASE - ASIC3_GPIO_A_BASE));
}

static inline int asic3_irq_to_index(struct asic3 *asic, int irq)
{
	return (irq - asic->irq_base) & 0xf;
}

static void asic3_mask_gpio_irq(unsigned int irq)
{
	struct asic3 *asic = get_irq_chip_data(irq);
	u32 val, bank, index;
	unsigned long flags;

	bank = asic3_irq_to_bank(asic, irq);
	index = asic3_irq_to_index(asic, irq);

	spin_lock_irqsave(&asic->lock, flags);
	val = asic3_read_register(asic, bank + ASIC3_GPIO_MASK);
	val |= 1 << index;
	asic3_write_register(asic, bank + ASIC3_GPIO_MASK, val);
	spin_unlock_irqrestore(&asic->lock, flags);
}

static void asic3_mask_irq(unsigned int irq)
{
	struct asic3 *asic = get_irq_chip_data(irq);
	int regval;
	unsigned long flags;

	spin_lock_irqsave(&asic->lock, flags);
	regval = asic3_read_register(asic,
				     ASIC3_INTR_BASE +
				     ASIC3_INTR_INT_MASK);

	regval &= ~(ASIC3_INTMASK_MASK0 <<
		    (irq - (asic->irq_base + ASIC3_NUM_GPIOS)));

	asic3_write_register(asic,
			     ASIC3_INTR_BASE +
			     ASIC3_INTR_INT_MASK,
			     regval);
	spin_unlock_irqrestore(&asic->lock, flags);
}

static void asic3_unmask_gpio_irq(unsigned int irq)
{
	struct asic3 *asic = get_irq_chip_data(irq);
	u32 val, bank, index;
	unsigned long flags;

	bank = asic3_irq_to_bank(asic, irq);
	index = asic3_irq_to_index(asic, irq);

	spin_lock_irqsave(&asic->lock, flags);
	val = asic3_read_register(asic, bank + ASIC3_GPIO_MASK);
	val &= ~(1 << index);
	asic3_write_register(asic, bank + ASIC3_GPIO_MASK, val);
	spin_unlock_irqrestore(&asic->lock, flags);
}

static void asic3_unmask_irq(unsigned int irq)
{
	struct asic3 *asic = get_irq_chip_data(irq);
	int regval;
	unsigned long flags;

	spin_lock_irqsave(&asic->lock, flags);
	regval = asic3_read_register(asic,
				     ASIC3_INTR_BASE +
				     ASIC3_INTR_INT_MASK);

	regval |= (ASIC3_INTMASK_MASK0 <<
		   (irq - (asic->irq_base + ASIC3_NUM_GPIOS)));

	asic3_write_register(asic,
			     ASIC3_INTR_BASE +
			     ASIC3_INTR_INT_MASK,
			     regval);
	spin_unlock_irqrestore(&asic->lock, flags);
}

static int asic3_gpio_irq_type(unsigned int irq, unsigned int type)
{
	struct asic3 *asic = get_irq_chip_data(irq);
	u32 bank, index;
	u16 trigger, level, edge, bit;
	unsigned long flags;

	bank = asic3_irq_to_bank(asic, irq);
	index = asic3_irq_to_index(asic, irq);
	bit = 1<<index;

	spin_lock_irqsave(&asic->lock, flags);
	level = asic3_read_register(asic,
				    bank + ASIC3_GPIO_LEVEL_TRIGGER);
	edge = asic3_read_register(asic,
				   bank + ASIC3_GPIO_EDGE_TRIGGER);
	trigger = asic3_read_register(asic,
				      bank + ASIC3_GPIO_TRIGGER_TYPE);
	asic->irq_bothedge[(irq - asic->irq_base) >> 4] &= ~bit;

	if (type == IRQ_TYPE_EDGE_RISING) {
		trigger |= bit;
		edge |= bit;
	} else if (type == IRQ_TYPE_EDGE_FALLING) {
		trigger |= bit;
		edge &= ~bit;
	} else if (type == IRQ_TYPE_EDGE_BOTH) {
		trigger |= bit;
		if (asic3_gpio_get(&asic->gpio, irq - asic->irq_base))
			edge &= ~bit;
		else
			edge |= bit;
		asic->irq_bothedge[(irq - asic->irq_base) >> 4] |= bit;
	} else if (type == IRQ_TYPE_LEVEL_LOW) {
		trigger &= ~bit;
		level &= ~bit;
	} else if (type == IRQ_TYPE_LEVEL_HIGH) {
		trigger &= ~bit;
		level |= bit;
	} else {
		/*
		 * if type == IRQ_TYPE_NONE, we should mask interrupts, but
		 * be careful to not unmask them if mask was also called.
		 * Probably need internal state for mask.
		 */
		dev_notice(asic->dev, "irq type not changed\n");
	}
	asic3_write_register(asic, bank + ASIC3_GPIO_LEVEL_TRIGGER,
			     level);
	asic3_write_register(asic, bank + ASIC3_GPIO_EDGE_TRIGGER,
			     edge);
	asic3_write_register(asic, bank + ASIC3_GPIO_TRIGGER_TYPE,
			     trigger);
	spin_unlock_irqrestore(&asic->lock, flags);
	return 0;
}

static struct irq_chip asic3_gpio_irq_chip = {
	.name		= "ASIC3-GPIO",
	.ack		= asic3_mask_gpio_irq,
	.mask		= asic3_mask_gpio_irq,
	.unmask		= asic3_unmask_gpio_irq,
	.set_type	= asic3_gpio_irq_type,
};

static struct irq_chip asic3_irq_chip = {
	.name		= "ASIC3",
	.ack		= asic3_mask_irq,
	.mask		= asic3_mask_irq,
	.unmask		= asic3_unmask_irq,
};

static int __init asic3_irq_probe(struct platform_device *pdev)
{
	struct asic3 *asic = platform_get_drvdata(pdev);
	unsigned long clksel = 0;
	unsigned int irq, irq_base;
	int ret;

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;
	asic->irq_nr = ret;

	/* turn on clock to IRQ controller */
	clksel |= CLOCK_SEL_CX;
	asic3_write_register(asic, ASIC3_OFFSET(CLOCK, SEL),
			     clksel);

	irq_base = asic->irq_base;

	for (irq = irq_base; irq < irq_base + ASIC3_NR_IRQS; irq++) {
		if (irq < asic->irq_base + ASIC3_NUM_GPIOS)
			set_irq_chip(irq, &asic3_gpio_irq_chip);
		else
			set_irq_chip(irq, &asic3_irq_chip);

		set_irq_chip_data(irq, asic);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	asic3_write_register(asic, ASIC3_OFFSET(INTR, INT_MASK),
			     ASIC3_INTMASK_GINTMASK);

	set_irq_chained_handler(asic->irq_nr, asic3_irq_demux);
	set_irq_type(asic->irq_nr, IRQ_TYPE_EDGE_RISING);
	set_irq_data(asic->irq_nr, asic);

	return 0;
}

static void asic3_irq_remove(struct platform_device *pdev)
{
	struct asic3 *asic = platform_get_drvdata(pdev);
	unsigned int irq, irq_base;

	irq_base = asic->irq_base;

	for (irq = irq_base; irq < irq_base + ASIC3_NR_IRQS; irq++) {
		set_irq_flags(irq, 0);
		set_irq_handler(irq, NULL);
		set_irq_chip(irq, NULL);
		set_irq_chip_data(irq, NULL);
	}
	set_irq_chained_handler(asic->irq_nr, NULL);
}

/* GPIOs */
static int asic3_gpio_direction(struct gpio_chip *chip,
				unsigned offset, int out)
{
	u32 mask = ASIC3_GPIO_TO_MASK(offset), out_reg;
	unsigned int gpio_base;
	unsigned long flags;
	struct asic3 *asic;

	asic = container_of(chip, struct asic3, gpio);
	gpio_base = ASIC3_GPIO_TO_BASE(offset);

	if (gpio_base > ASIC3_GPIO_D_BASE) {
		dev_err(asic->dev, "Invalid base (0x%x) for gpio %d\n",
			gpio_base, offset);
		return -EINVAL;
	}

	spin_lock_irqsave(&asic->lock, flags);

	out_reg = asic3_read_register(asic, gpio_base + ASIC3_GPIO_DIRECTION);

	/* Input is 0, Output is 1 */
	if (out)
		out_reg |= mask;
	else
		out_reg &= ~mask;

	asic3_write_register(asic, gpio_base + ASIC3_GPIO_DIRECTION, out_reg);

	spin_unlock_irqrestore(&asic->lock, flags);

	return 0;

}

static int asic3_gpio_direction_input(struct gpio_chip *chip,
				      unsigned offset)
{
	return asic3_gpio_direction(chip, offset, 0);
}

static int asic3_gpio_direction_output(struct gpio_chip *chip,
				       unsigned offset, int value)
{
	return asic3_gpio_direction(chip, offset, 1);
}

static int asic3_gpio_get(struct gpio_chip *chip,
			  unsigned offset)
{
	unsigned int gpio_base;
	u32 mask = ASIC3_GPIO_TO_MASK(offset);
	struct asic3 *asic;

	asic = container_of(chip, struct asic3, gpio);
	gpio_base = ASIC3_GPIO_TO_BASE(offset);

	if (gpio_base > ASIC3_GPIO_D_BASE) {
		dev_err(asic->dev, "Invalid base (0x%x) for gpio %d\n",
			gpio_base, offset);
		return -EINVAL;
	}

	return asic3_read_register(asic, gpio_base + ASIC3_GPIO_STATUS) & mask;
}

static void asic3_gpio_set(struct gpio_chip *chip,
			   unsigned offset, int value)
{
	u32 mask, out_reg;
	unsigned int gpio_base;
	unsigned long flags;
	struct asic3 *asic;

	asic = container_of(chip, struct asic3, gpio);
	gpio_base = ASIC3_GPIO_TO_BASE(offset);

	if (gpio_base > ASIC3_GPIO_D_BASE) {
		dev_err(asic->dev, "Invalid base (0x%x) for gpio %d\n",
			gpio_base, offset);
		return;
	}

	mask = ASIC3_GPIO_TO_MASK(offset);

	spin_lock_irqsave(&asic->lock, flags);

	out_reg = asic3_read_register(asic, gpio_base + ASIC3_GPIO_OUT);

	if (value)
		out_reg |= mask;
	else
		out_reg &= ~mask;

	asic3_write_register(asic, gpio_base + ASIC3_GPIO_OUT, out_reg);

	spin_unlock_irqrestore(&asic->lock, flags);

	return;
}

static __init int asic3_gpio_probe(struct platform_device *pdev,
				   u16 *gpio_config, int num)
{
	struct asic3 *asic = platform_get_drvdata(pdev);
	u16 alt_reg[ASIC3_NUM_GPIO_BANKS];
	u16 out_reg[ASIC3_NUM_GPIO_BANKS];
	u16 dir_reg[ASIC3_NUM_GPIO_BANKS];
	int i;

	memset(alt_reg, 0, ASIC3_NUM_GPIO_BANKS * sizeof(u16));
	memset(out_reg, 0, ASIC3_NUM_GPIO_BANKS * sizeof(u16));
	memset(dir_reg, 0, ASIC3_NUM_GPIO_BANKS * sizeof(u16));

	/* Enable all GPIOs */
	asic3_write_register(asic, ASIC3_GPIO_OFFSET(A, MASK), 0xffff);
	asic3_write_register(asic, ASIC3_GPIO_OFFSET(B, MASK), 0xffff);
	asic3_write_register(asic, ASIC3_GPIO_OFFSET(C, MASK), 0xffff);
	asic3_write_register(asic, ASIC3_GPIO_OFFSET(D, MASK), 0xffff);

	for (i = 0; i < num; i++) {
		u8 alt, pin, dir, init, bank_num, bit_num;
		u16 config = gpio_config[i];

		pin = ASIC3_CONFIG_GPIO_PIN(config);
		alt = ASIC3_CONFIG_GPIO_ALT(config);
		dir = ASIC3_CONFIG_GPIO_DIR(config);
		init = ASIC3_CONFIG_GPIO_INIT(config);

		bank_num = ASIC3_GPIO_TO_BANK(pin);
		bit_num = ASIC3_GPIO_TO_BIT(pin);

		alt_reg[bank_num] |= (alt << bit_num);
		out_reg[bank_num] |= (init << bit_num);
		dir_reg[bank_num] |= (dir << bit_num);
	}

	for (i = 0; i < ASIC3_NUM_GPIO_BANKS; i++) {
		asic3_write_register(asic,
				     ASIC3_BANK_TO_BASE(i) +
				     ASIC3_GPIO_DIRECTION,
				     dir_reg[i]);
		asic3_write_register(asic,
				     ASIC3_BANK_TO_BASE(i) + ASIC3_GPIO_OUT,
				     out_reg[i]);
		asic3_write_register(asic,
				     ASIC3_BANK_TO_BASE(i) +
				     ASIC3_GPIO_ALT_FUNCTION,
				     alt_reg[i]);
	}

	return gpiochip_add(&asic->gpio);
}

static int asic3_gpio_remove(struct platform_device *pdev)
{
	struct asic3 *asic = platform_get_drvdata(pdev);

	return gpiochip_remove(&asic->gpio);
}


/* Core */
static int __init asic3_probe(struct platform_device *pdev)
{
	struct asic3_platform_data *pdata = pdev->dev.platform_data;
	struct asic3 *asic;
	struct resource *mem;
	unsigned long clksel;
	int map_size;
	int ret = 0;

	asic = kzalloc(sizeof(struct asic3), GFP_KERNEL);
	if (asic == NULL) {
		printk(KERN_ERR "kzalloc failed\n");
		return -ENOMEM;
	}

	spin_lock_init(&asic->lock);
	platform_set_drvdata(pdev, asic);
	asic->dev = &pdev->dev;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		ret = -ENOMEM;
		dev_err(asic->dev, "no MEM resource\n");
		goto out_free;
	}

	map_size = mem->end - mem->start + 1;
	asic->mapping = ioremap(mem->start, map_size);
	if (!asic->mapping) {
		ret = -ENOMEM;
		dev_err(asic->dev, "Couldn't ioremap\n");
		goto out_free;
	}

	asic->irq_base = pdata->irq_base;

	/* calculate bus shift from mem resource */
	asic->bus_shift = 2 - (map_size >> 12);

	clksel = 0;
	asic3_write_register(asic, ASIC3_OFFSET(CLOCK, SEL), clksel);

	ret = asic3_irq_probe(pdev);
	if (ret < 0) {
		dev_err(asic->dev, "Couldn't probe IRQs\n");
		goto out_unmap;
	}

	asic->gpio.base = pdata->gpio_base;
	asic->gpio.ngpio = ASIC3_NUM_GPIOS;
	asic->gpio.get = asic3_gpio_get;
	asic->gpio.set = asic3_gpio_set;
	asic->gpio.direction_input = asic3_gpio_direction_input;
	asic->gpio.direction_output = asic3_gpio_direction_output;

	ret = asic3_gpio_probe(pdev,
			       pdata->gpio_config,
			       pdata->gpio_config_num);
	if (ret < 0) {
		dev_err(asic->dev, "GPIO probe failed\n");
		goto out_irq;
	}

	dev_info(asic->dev, "ASIC3 Core driver\n");

	return 0;

 out_irq:
	asic3_irq_remove(pdev);

 out_unmap:
	iounmap(asic->mapping);

 out_free:
	kfree(asic);

	return ret;
}

static int asic3_remove(struct platform_device *pdev)
{
	int ret;
	struct asic3 *asic = platform_get_drvdata(pdev);

	ret = asic3_gpio_remove(pdev);
	if (ret < 0)
		return ret;
	asic3_irq_remove(pdev);

	asic3_write_register(asic, ASIC3_OFFSET(CLOCK, SEL), 0);

	iounmap(asic->mapping);

	kfree(asic);

	return 0;
}

static void asic3_shutdown(struct platform_device *pdev)
{
}

static struct platform_driver asic3_device_driver = {
	.driver		= {
		.name	= "asic3",
	},
	.remove		= __devexit_p(asic3_remove),
	.shutdown	= asic3_shutdown,
};

static int __init asic3_init(void)
{
	int retval = 0;
	retval = platform_driver_probe(&asic3_device_driver, asic3_probe);
	return retval;
}

subsys_initcall(asic3_init);
