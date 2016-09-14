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
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>

#include <linux/mfd/asic3.h>
#include <linux/mfd/core.h>
#include <linux/mfd/ds1wm.h>
#include <linux/mfd/tmio.h>

enum {
	ASIC3_CLOCK_SPI,
	ASIC3_CLOCK_OWM,
	ASIC3_CLOCK_PWM0,
	ASIC3_CLOCK_PWM1,
	ASIC3_CLOCK_LED0,
	ASIC3_CLOCK_LED1,
	ASIC3_CLOCK_LED2,
	ASIC3_CLOCK_SD_HOST,
	ASIC3_CLOCK_SD_BUS,
	ASIC3_CLOCK_SMBUS,
	ASIC3_CLOCK_EX0,
	ASIC3_CLOCK_EX1,
};

struct asic3_clk {
	int enabled;
	unsigned int cdex;
	unsigned long rate;
};

#define INIT_CDEX(_name, _rate)	\
	[ASIC3_CLOCK_##_name] = {		\
		.cdex = CLOCK_CDEX_##_name,	\
		.rate = _rate,			\
	}

static struct asic3_clk asic3_clk_init[] __initdata = {
	INIT_CDEX(SPI, 0),
	INIT_CDEX(OWM, 5000000),
	INIT_CDEX(PWM0, 0),
	INIT_CDEX(PWM1, 0),
	INIT_CDEX(LED0, 0),
	INIT_CDEX(LED1, 0),
	INIT_CDEX(LED2, 0),
	INIT_CDEX(SD_HOST, 24576000),
	INIT_CDEX(SD_BUS, 12288000),
	INIT_CDEX(SMBUS, 0),
	INIT_CDEX(EX0, 32768),
	INIT_CDEX(EX1, 24576000),
};

struct asic3 {
	void __iomem *mapping;
	unsigned int bus_shift;
	unsigned int irq_nr;
	unsigned int irq_base;
	spinlock_t lock;
	u16 irq_bothedge[4];
	struct gpio_chip gpio;
	struct device *dev;
	void __iomem *tmio_cnf;

	struct asic3_clk clocks[ARRAY_SIZE(asic3_clk_init)];
};

static int asic3_gpio_get(struct gpio_chip *chip, unsigned offset);

void asic3_write_register(struct asic3 *asic, unsigned int reg, u32 value)
{
	iowrite16(value, asic->mapping +
		  (reg >> asic->bus_shift));
}
EXPORT_SYMBOL_GPL(asic3_write_register);

u32 asic3_read_register(struct asic3 *asic, unsigned int reg)
{
	return ioread16(asic->mapping +
			(reg >> asic->bus_shift));
}
EXPORT_SYMBOL_GPL(asic3_read_register);

static void asic3_set_register(struct asic3 *asic, u32 reg, u32 bits, bool set)
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

static void asic3_irq_demux(struct irq_desc *desc)
{
	struct asic3 *asic = irq_desc_get_handler_data(desc);
	struct irq_data *data = irq_desc_get_irq_data(desc);
	int iter, i;
	unsigned long flags;

	data->chip->irq_ack(data);

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
					generic_handle_irq(irqnr);
					if (asic->irq_bothedge[bank] & bit)
						asic3_irq_flip_edge(asic, base,
								    bit);
				}
			}
		}

		/* Handle remaining IRQs in the status register */
		for (i = ASIC3_NUM_GPIOS; i < ASIC3_NR_IRQS; i++) {
			/* They start at bit 4 and go up */
			if (status & (1 << (i - ASIC3_NUM_GPIOS + 4)))
				generic_handle_irq(asic->irq_base + i);
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

static void asic3_mask_gpio_irq(struct irq_data *data)
{
	struct asic3 *asic = irq_data_get_irq_chip_data(data);
	u32 val, bank, index;
	unsigned long flags;

	bank = asic3_irq_to_bank(asic, data->irq);
	index = asic3_irq_to_index(asic, data->irq);

	spin_lock_irqsave(&asic->lock, flags);
	val = asic3_read_register(asic, bank + ASIC3_GPIO_MASK);
	val |= 1 << index;
	asic3_write_register(asic, bank + ASIC3_GPIO_MASK, val);
	spin_unlock_irqrestore(&asic->lock, flags);
}

static void asic3_mask_irq(struct irq_data *data)
{
	struct asic3 *asic = irq_data_get_irq_chip_data(data);
	int regval;
	unsigned long flags;

	spin_lock_irqsave(&asic->lock, flags);
	regval = asic3_read_register(asic,
				     ASIC3_INTR_BASE +
				     ASIC3_INTR_INT_MASK);

	regval &= ~(ASIC3_INTMASK_MASK0 <<
		    (data->irq - (asic->irq_base + ASIC3_NUM_GPIOS)));

	asic3_write_register(asic,
			     ASIC3_INTR_BASE +
			     ASIC3_INTR_INT_MASK,
			     regval);
	spin_unlock_irqrestore(&asic->lock, flags);
}

static void asic3_unmask_gpio_irq(struct irq_data *data)
{
	struct asic3 *asic = irq_data_get_irq_chip_data(data);
	u32 val, bank, index;
	unsigned long flags;

	bank = asic3_irq_to_bank(asic, data->irq);
	index = asic3_irq_to_index(asic, data->irq);

	spin_lock_irqsave(&asic->lock, flags);
	val = asic3_read_register(asic, bank + ASIC3_GPIO_MASK);
	val &= ~(1 << index);
	asic3_write_register(asic, bank + ASIC3_GPIO_MASK, val);
	spin_unlock_irqrestore(&asic->lock, flags);
}

static void asic3_unmask_irq(struct irq_data *data)
{
	struct asic3 *asic = irq_data_get_irq_chip_data(data);
	int regval;
	unsigned long flags;

	spin_lock_irqsave(&asic->lock, flags);
	regval = asic3_read_register(asic,
				     ASIC3_INTR_BASE +
				     ASIC3_INTR_INT_MASK);

	regval |= (ASIC3_INTMASK_MASK0 <<
		   (data->irq - (asic->irq_base + ASIC3_NUM_GPIOS)));

	asic3_write_register(asic,
			     ASIC3_INTR_BASE +
			     ASIC3_INTR_INT_MASK,
			     regval);
	spin_unlock_irqrestore(&asic->lock, flags);
}

static int asic3_gpio_irq_type(struct irq_data *data, unsigned int type)
{
	struct asic3 *asic = irq_data_get_irq_chip_data(data);
	u32 bank, index;
	u16 trigger, level, edge, bit;
	unsigned long flags;

	bank = asic3_irq_to_bank(asic, data->irq);
	index = asic3_irq_to_index(asic, data->irq);
	bit = 1<<index;

	spin_lock_irqsave(&asic->lock, flags);
	level = asic3_read_register(asic,
				    bank + ASIC3_GPIO_LEVEL_TRIGGER);
	edge = asic3_read_register(asic,
				   bank + ASIC3_GPIO_EDGE_TRIGGER);
	trigger = asic3_read_register(asic,
				      bank + ASIC3_GPIO_TRIGGER_TYPE);
	asic->irq_bothedge[(data->irq - asic->irq_base) >> 4] &= ~bit;

	if (type == IRQ_TYPE_EDGE_RISING) {
		trigger |= bit;
		edge |= bit;
	} else if (type == IRQ_TYPE_EDGE_FALLING) {
		trigger |= bit;
		edge &= ~bit;
	} else if (type == IRQ_TYPE_EDGE_BOTH) {
		trigger |= bit;
		if (asic3_gpio_get(&asic->gpio, data->irq - asic->irq_base))
			edge &= ~bit;
		else
			edge |= bit;
		asic->irq_bothedge[(data->irq - asic->irq_base) >> 4] |= bit;
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

static int asic3_gpio_irq_set_wake(struct irq_data *data, unsigned int on)
{
	struct asic3 *asic = irq_data_get_irq_chip_data(data);
	u32 bank, index;
	u16 bit;

	bank = asic3_irq_to_bank(asic, data->irq);
	index = asic3_irq_to_index(asic, data->irq);
	bit = 1<<index;

	asic3_set_register(asic, bank + ASIC3_GPIO_SLEEP_MASK, bit, !on);

	return 0;
}

static struct irq_chip asic3_gpio_irq_chip = {
	.name		= "ASIC3-GPIO",
	.irq_ack	= asic3_mask_gpio_irq,
	.irq_mask	= asic3_mask_gpio_irq,
	.irq_unmask	= asic3_unmask_gpio_irq,
	.irq_set_type	= asic3_gpio_irq_type,
	.irq_set_wake	= asic3_gpio_irq_set_wake,
};

static struct irq_chip asic3_irq_chip = {
	.name		= "ASIC3",
	.irq_ack	= asic3_mask_irq,
	.irq_mask	= asic3_mask_irq,
	.irq_unmask	= asic3_unmask_irq,
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
			irq_set_chip(irq, &asic3_gpio_irq_chip);
		else
			irq_set_chip(irq, &asic3_irq_chip);

		irq_set_chip_data(irq, asic);
		irq_set_handler(irq, handle_level_irq);
		irq_clear_status_flags(irq, IRQ_NOREQUEST | IRQ_NOPROBE);
	}

	asic3_write_register(asic, ASIC3_OFFSET(INTR, INT_MASK),
			     ASIC3_INTMASK_GINTMASK);

	irq_set_chained_handler_and_data(asic->irq_nr, asic3_irq_demux, asic);
	irq_set_irq_type(asic->irq_nr, IRQ_TYPE_EDGE_RISING);

	return 0;
}

static void asic3_irq_remove(struct platform_device *pdev)
{
	struct asic3 *asic = platform_get_drvdata(pdev);
	unsigned int irq, irq_base;

	irq_base = asic->irq_base;

	for (irq = irq_base; irq < irq_base + ASIC3_NR_IRQS; irq++) {
		irq_set_status_flags(irq, IRQ_NOREQUEST | IRQ_NOPROBE);
		irq_set_chip_and_handler(irq, NULL, NULL);
		irq_set_chip_data(irq, NULL);
	}
	irq_set_chained_handler(asic->irq_nr, NULL);
}

/* GPIOs */
static int asic3_gpio_direction(struct gpio_chip *chip,
				unsigned offset, int out)
{
	u32 mask = ASIC3_GPIO_TO_MASK(offset), out_reg;
	unsigned int gpio_base;
	unsigned long flags;
	struct asic3 *asic;

	asic = gpiochip_get_data(chip);
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

	asic = gpiochip_get_data(chip);
	gpio_base = ASIC3_GPIO_TO_BASE(offset);

	if (gpio_base > ASIC3_GPIO_D_BASE) {
		dev_err(asic->dev, "Invalid base (0x%x) for gpio %d\n",
			gpio_base, offset);
		return -EINVAL;
	}

	return !!(asic3_read_register(asic,
				      gpio_base + ASIC3_GPIO_STATUS) & mask);
}

static void asic3_gpio_set(struct gpio_chip *chip,
			   unsigned offset, int value)
{
	u32 mask, out_reg;
	unsigned int gpio_base;
	unsigned long flags;
	struct asic3 *asic;

	asic = gpiochip_get_data(chip);
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
}

static int asic3_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct asic3 *asic = gpiochip_get_data(chip);

	return asic->irq_base + offset;
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

	return gpiochip_add_data(&asic->gpio, asic);
}

static int asic3_gpio_remove(struct platform_device *pdev)
{
	struct asic3 *asic = platform_get_drvdata(pdev);

	gpiochip_remove(&asic->gpio);
	return 0;
}

static void asic3_clk_enable(struct asic3 *asic, struct asic3_clk *clk)
{
	unsigned long flags;
	u32 cdex;

	spin_lock_irqsave(&asic->lock, flags);
	if (clk->enabled++ == 0) {
		cdex = asic3_read_register(asic, ASIC3_OFFSET(CLOCK, CDEX));
		cdex |= clk->cdex;
		asic3_write_register(asic, ASIC3_OFFSET(CLOCK, CDEX), cdex);
	}
	spin_unlock_irqrestore(&asic->lock, flags);
}

static void asic3_clk_disable(struct asic3 *asic, struct asic3_clk *clk)
{
	unsigned long flags;
	u32 cdex;

	WARN_ON(clk->enabled == 0);

	spin_lock_irqsave(&asic->lock, flags);
	if (--clk->enabled == 0) {
		cdex = asic3_read_register(asic, ASIC3_OFFSET(CLOCK, CDEX));
		cdex &= ~clk->cdex;
		asic3_write_register(asic, ASIC3_OFFSET(CLOCK, CDEX), cdex);
	}
	spin_unlock_irqrestore(&asic->lock, flags);
}

/* MFD cells (SPI, PWM, LED, DS1WM, MMC) */
static struct ds1wm_driver_data ds1wm_pdata = {
	.active_high = 1,
	.reset_recover_delay = 1,
};

static struct resource ds1wm_resources[] = {
	{
		.start = ASIC3_OWM_BASE,
		.end   = ASIC3_OWM_BASE + 0x13,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = ASIC3_IRQ_OWM,
		.end   = ASIC3_IRQ_OWM,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	},
};

static int ds1wm_enable(struct platform_device *pdev)
{
	struct asic3 *asic = dev_get_drvdata(pdev->dev.parent);

	/* Turn on external clocks and the OWM clock */
	asic3_clk_enable(asic, &asic->clocks[ASIC3_CLOCK_EX0]);
	asic3_clk_enable(asic, &asic->clocks[ASIC3_CLOCK_EX1]);
	asic3_clk_enable(asic, &asic->clocks[ASIC3_CLOCK_OWM]);
	usleep_range(1000, 5000);

	/* Reset and enable DS1WM */
	asic3_set_register(asic, ASIC3_OFFSET(EXTCF, RESET),
			   ASIC3_EXTCF_OWM_RESET, 1);
	usleep_range(1000, 5000);
	asic3_set_register(asic, ASIC3_OFFSET(EXTCF, RESET),
			   ASIC3_EXTCF_OWM_RESET, 0);
	usleep_range(1000, 5000);
	asic3_set_register(asic, ASIC3_OFFSET(EXTCF, SELECT),
			   ASIC3_EXTCF_OWM_EN, 1);
	usleep_range(1000, 5000);

	return 0;
}

static int ds1wm_disable(struct platform_device *pdev)
{
	struct asic3 *asic = dev_get_drvdata(pdev->dev.parent);

	asic3_set_register(asic, ASIC3_OFFSET(EXTCF, SELECT),
			   ASIC3_EXTCF_OWM_EN, 0);

	asic3_clk_disable(asic, &asic->clocks[ASIC3_CLOCK_OWM]);
	asic3_clk_disable(asic, &asic->clocks[ASIC3_CLOCK_EX0]);
	asic3_clk_disable(asic, &asic->clocks[ASIC3_CLOCK_EX1]);

	return 0;
}

static const struct mfd_cell asic3_cell_ds1wm = {
	.name          = "ds1wm",
	.enable        = ds1wm_enable,
	.disable       = ds1wm_disable,
	.platform_data = &ds1wm_pdata,
	.pdata_size    = sizeof(ds1wm_pdata),
	.num_resources = ARRAY_SIZE(ds1wm_resources),
	.resources     = ds1wm_resources,
};

static void asic3_mmc_pwr(struct platform_device *pdev, int state)
{
	struct asic3 *asic = dev_get_drvdata(pdev->dev.parent);

	tmio_core_mmc_pwr(asic->tmio_cnf, 1 - asic->bus_shift, state);
}

static void asic3_mmc_clk_div(struct platform_device *pdev, int state)
{
	struct asic3 *asic = dev_get_drvdata(pdev->dev.parent);

	tmio_core_mmc_clk_div(asic->tmio_cnf, 1 - asic->bus_shift, state);
}

static struct tmio_mmc_data asic3_mmc_data = {
	.hclk           = 24576000,
	.set_pwr        = asic3_mmc_pwr,
	.set_clk_div    = asic3_mmc_clk_div,
};

static struct resource asic3_mmc_resources[] = {
	{
		.start = ASIC3_SD_CTRL_BASE,
		.end   = ASIC3_SD_CTRL_BASE + 0x3ff,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = 0,
		.end   = 0,
		.flags = IORESOURCE_IRQ,
	},
};

static int asic3_mmc_enable(struct platform_device *pdev)
{
	struct asic3 *asic = dev_get_drvdata(pdev->dev.parent);

	/* Not sure if it must be done bit by bit, but leaving as-is */
	asic3_set_register(asic, ASIC3_OFFSET(SDHWCTRL, SDCONF),
			   ASIC3_SDHWCTRL_LEVCD, 1);
	asic3_set_register(asic, ASIC3_OFFSET(SDHWCTRL, SDCONF),
			   ASIC3_SDHWCTRL_LEVWP, 1);
	asic3_set_register(asic, ASIC3_OFFSET(SDHWCTRL, SDCONF),
			   ASIC3_SDHWCTRL_SUSPEND, 0);
	asic3_set_register(asic, ASIC3_OFFSET(SDHWCTRL, SDCONF),
			   ASIC3_SDHWCTRL_PCLR, 0);

	asic3_clk_enable(asic, &asic->clocks[ASIC3_CLOCK_EX0]);
	/* CLK32 used for card detection and for interruption detection
	 * when HCLK is stopped.
	 */
	asic3_clk_enable(asic, &asic->clocks[ASIC3_CLOCK_EX1]);
	usleep_range(1000, 5000);

	/* HCLK 24.576 MHz, BCLK 12.288 MHz: */
	asic3_write_register(asic, ASIC3_OFFSET(CLOCK, SEL),
		CLOCK_SEL_CX | CLOCK_SEL_SD_HCLK_SEL);

	asic3_clk_enable(asic, &asic->clocks[ASIC3_CLOCK_SD_HOST]);
	asic3_clk_enable(asic, &asic->clocks[ASIC3_CLOCK_SD_BUS]);
	usleep_range(1000, 5000);

	asic3_set_register(asic, ASIC3_OFFSET(EXTCF, SELECT),
			   ASIC3_EXTCF_SD_MEM_ENABLE, 1);

	/* Enable SD card slot 3.3V power supply */
	asic3_set_register(asic, ASIC3_OFFSET(SDHWCTRL, SDCONF),
			   ASIC3_SDHWCTRL_SDPWR, 1);

	/* ASIC3_SD_CTRL_BASE assumes 32-bit addressing, TMIO is 16-bit */
	tmio_core_mmc_enable(asic->tmio_cnf, 1 - asic->bus_shift,
			     ASIC3_SD_CTRL_BASE >> 1);

	return 0;
}

static int asic3_mmc_disable(struct platform_device *pdev)
{
	struct asic3 *asic = dev_get_drvdata(pdev->dev.parent);

	/* Put in suspend mode */
	asic3_set_register(asic, ASIC3_OFFSET(SDHWCTRL, SDCONF),
			   ASIC3_SDHWCTRL_SUSPEND, 1);

	/* Disable clocks */
	asic3_clk_disable(asic, &asic->clocks[ASIC3_CLOCK_SD_HOST]);
	asic3_clk_disable(asic, &asic->clocks[ASIC3_CLOCK_SD_BUS]);
	asic3_clk_disable(asic, &asic->clocks[ASIC3_CLOCK_EX0]);
	asic3_clk_disable(asic, &asic->clocks[ASIC3_CLOCK_EX1]);
	return 0;
}

static const struct mfd_cell asic3_cell_mmc = {
	.name          = "tmio-mmc",
	.enable        = asic3_mmc_enable,
	.disable       = asic3_mmc_disable,
	.suspend       = asic3_mmc_disable,
	.resume        = asic3_mmc_enable,
	.platform_data = &asic3_mmc_data,
	.pdata_size    = sizeof(asic3_mmc_data),
	.num_resources = ARRAY_SIZE(asic3_mmc_resources),
	.resources     = asic3_mmc_resources,
};

static const int clock_ledn[ASIC3_NUM_LEDS] = {
	[0] = ASIC3_CLOCK_LED0,
	[1] = ASIC3_CLOCK_LED1,
	[2] = ASIC3_CLOCK_LED2,
};

static int asic3_leds_enable(struct platform_device *pdev)
{
	const struct mfd_cell *cell = mfd_get_cell(pdev);
	struct asic3 *asic = dev_get_drvdata(pdev->dev.parent);

	asic3_clk_enable(asic, &asic->clocks[clock_ledn[cell->id]]);

	return 0;
}

static int asic3_leds_disable(struct platform_device *pdev)
{
	const struct mfd_cell *cell = mfd_get_cell(pdev);
	struct asic3 *asic = dev_get_drvdata(pdev->dev.parent);

	asic3_clk_disable(asic, &asic->clocks[clock_ledn[cell->id]]);

	return 0;
}

static int asic3_leds_suspend(struct platform_device *pdev)
{
	const struct mfd_cell *cell = mfd_get_cell(pdev);
	struct asic3 *asic = dev_get_drvdata(pdev->dev.parent);

	while (asic3_gpio_get(&asic->gpio, ASIC3_GPIO(C, cell->id)) != 0)
		usleep_range(1000, 5000);

	asic3_clk_disable(asic, &asic->clocks[clock_ledn[cell->id]]);

	return 0;
}

static struct mfd_cell asic3_cell_leds[ASIC3_NUM_LEDS] = {
	[0] = {
		.name          = "leds-asic3",
		.id            = 0,
		.enable        = asic3_leds_enable,
		.disable       = asic3_leds_disable,
		.suspend       = asic3_leds_suspend,
		.resume        = asic3_leds_enable,
	},
	[1] = {
		.name          = "leds-asic3",
		.id            = 1,
		.enable        = asic3_leds_enable,
		.disable       = asic3_leds_disable,
		.suspend       = asic3_leds_suspend,
		.resume        = asic3_leds_enable,
	},
	[2] = {
		.name          = "leds-asic3",
		.id            = 2,
		.enable        = asic3_leds_enable,
		.disable       = asic3_leds_disable,
		.suspend       = asic3_leds_suspend,
		.resume        = asic3_leds_enable,
	},
};

static int __init asic3_mfd_probe(struct platform_device *pdev,
				  struct asic3_platform_data *pdata,
				  struct resource *mem)
{
	struct asic3 *asic = platform_get_drvdata(pdev);
	struct resource *mem_sdio;
	int irq, ret;

	mem_sdio = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!mem_sdio)
		dev_dbg(asic->dev, "no SDIO MEM resource\n");

	irq = platform_get_irq(pdev, 1);
	if (irq < 0)
		dev_dbg(asic->dev, "no SDIO IRQ resource\n");

	/* DS1WM */
	asic3_set_register(asic, ASIC3_OFFSET(EXTCF, SELECT),
			   ASIC3_EXTCF_OWM_SMB, 0);

	ds1wm_resources[0].start >>= asic->bus_shift;
	ds1wm_resources[0].end   >>= asic->bus_shift;

	/* MMC */
	if (mem_sdio) {
		asic->tmio_cnf = ioremap((ASIC3_SD_CONFIG_BASE >>
					  asic->bus_shift) + mem_sdio->start,
				 ASIC3_SD_CONFIG_SIZE >> asic->bus_shift);
		if (!asic->tmio_cnf) {
			ret = -ENOMEM;
			dev_dbg(asic->dev, "Couldn't ioremap SD_CONFIG\n");
			goto out;
		}
	}
	asic3_mmc_resources[0].start >>= asic->bus_shift;
	asic3_mmc_resources[0].end   >>= asic->bus_shift;

	if (pdata->clock_rate) {
		ds1wm_pdata.clock_rate = pdata->clock_rate;
		ret = mfd_add_devices(&pdev->dev, pdev->id,
			&asic3_cell_ds1wm, 1, mem, asic->irq_base, NULL);
		if (ret < 0)
			goto out;
	}

	if (mem_sdio && (irq >= 0)) {
		ret = mfd_add_devices(&pdev->dev, pdev->id,
			&asic3_cell_mmc, 1, mem_sdio, irq, NULL);
		if (ret < 0)
			goto out;
	}

	ret = 0;
	if (pdata->leds) {
		int i;

		for (i = 0; i < ASIC3_NUM_LEDS; ++i) {
			asic3_cell_leds[i].platform_data = &pdata->leds[i];
			asic3_cell_leds[i].pdata_size = sizeof(pdata->leds[i]);
		}
		ret = mfd_add_devices(&pdev->dev, 0,
			asic3_cell_leds, ASIC3_NUM_LEDS, NULL, 0, NULL);
	}

 out:
	return ret;
}

static void asic3_mfd_remove(struct platform_device *pdev)
{
	struct asic3 *asic = platform_get_drvdata(pdev);

	mfd_remove_devices(&pdev->dev);
	iounmap(asic->tmio_cnf);
}

/* Core */
static int __init asic3_probe(struct platform_device *pdev)
{
	struct asic3_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct asic3 *asic;
	struct resource *mem;
	unsigned long clksel;
	int ret = 0;

	asic = devm_kzalloc(&pdev->dev,
			    sizeof(struct asic3), GFP_KERNEL);
	if (!asic)
		return -ENOMEM;

	spin_lock_init(&asic->lock);
	platform_set_drvdata(pdev, asic);
	asic->dev = &pdev->dev;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(asic->dev, "no MEM resource\n");
		return -ENOMEM;
	}

	asic->mapping = ioremap(mem->start, resource_size(mem));
	if (!asic->mapping) {
		dev_err(asic->dev, "Couldn't ioremap\n");
		return -ENOMEM;
	}

	asic->irq_base = pdata->irq_base;

	/* calculate bus shift from mem resource */
	asic->bus_shift = 2 - (resource_size(mem) >> 12);

	clksel = 0;
	asic3_write_register(asic, ASIC3_OFFSET(CLOCK, SEL), clksel);

	ret = asic3_irq_probe(pdev);
	if (ret < 0) {
		dev_err(asic->dev, "Couldn't probe IRQs\n");
		goto out_unmap;
	}

	asic->gpio.label = "asic3";
	asic->gpio.base = pdata->gpio_base;
	asic->gpio.ngpio = ASIC3_NUM_GPIOS;
	asic->gpio.get = asic3_gpio_get;
	asic->gpio.set = asic3_gpio_set;
	asic->gpio.direction_input = asic3_gpio_direction_input;
	asic->gpio.direction_output = asic3_gpio_direction_output;
	asic->gpio.to_irq = asic3_gpio_to_irq;

	ret = asic3_gpio_probe(pdev,
			       pdata->gpio_config,
			       pdata->gpio_config_num);
	if (ret < 0) {
		dev_err(asic->dev, "GPIO probe failed\n");
		goto out_irq;
	}

	/* Making a per-device copy is only needed for the
	 * theoretical case of multiple ASIC3s on one board:
	 */
	memcpy(asic->clocks, asic3_clk_init, sizeof(asic3_clk_init));

	asic3_mfd_probe(pdev, pdata, mem);

	asic3_set_register(asic, ASIC3_OFFSET(EXTCF, SELECT),
		(ASIC3_EXTCF_CF0_BUF_EN|ASIC3_EXTCF_CF0_PWAIT_EN), 1);

	dev_info(asic->dev, "ASIC3 Core driver\n");

	return 0;

 out_irq:
	asic3_irq_remove(pdev);

 out_unmap:
	iounmap(asic->mapping);

	return ret;
}

static int asic3_remove(struct platform_device *pdev)
{
	int ret;
	struct asic3 *asic = platform_get_drvdata(pdev);

	asic3_set_register(asic, ASIC3_OFFSET(EXTCF, SELECT),
		(ASIC3_EXTCF_CF0_BUF_EN|ASIC3_EXTCF_CF0_PWAIT_EN), 0);

	asic3_mfd_remove(pdev);

	ret = asic3_gpio_remove(pdev);
	if (ret < 0)
		return ret;
	asic3_irq_remove(pdev);

	asic3_write_register(asic, ASIC3_OFFSET(CLOCK, SEL), 0);

	iounmap(asic->mapping);

	return 0;
}

static void asic3_shutdown(struct platform_device *pdev)
{
}

static struct platform_driver asic3_device_driver = {
	.driver		= {
		.name	= "asic3",
	},
	.remove		= asic3_remove,
	.shutdown	= asic3_shutdown,
};

static int __init asic3_init(void)
{
	int retval = 0;

	retval = platform_driver_probe(&asic3_device_driver, asic3_probe);

	return retval;
}

subsys_initcall(asic3_init);
