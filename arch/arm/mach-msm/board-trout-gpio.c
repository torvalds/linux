/*
 * linux/arch/arm/mach-msm/gpio.c
 *
 * Copyright (C) 2005 HP Labs
 * Copyright (C) 2008 Google, Inc.
 * Copyright (C) 2009 Pavel Machek <pavel@ucw.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>

#include "board-trout.h"

static uint8_t trout_int_mask[2] = {
	[0] = 0xff, /* mask all interrupts */
	[1] = 0xff,
};
static uint8_t trout_sleep_int_mask[] = {
	[0] = 0xff,
	[1] = 0xff,
};

struct msm_gpio_chip {
	struct gpio_chip	chip;
	void __iomem		*reg;	/* Base of register bank */
	u8			shadow;
};

#define to_msm_gpio_chip(c) container_of(c, struct msm_gpio_chip, chip)

static int msm_gpiolib_get(struct gpio_chip *chip, unsigned offset)
{
	struct msm_gpio_chip *msm_gpio = to_msm_gpio_chip(chip);
	unsigned mask = 1 << offset;

	return !!(readb(msm_gpio->reg) & mask);
}

static void msm_gpiolib_set(struct gpio_chip *chip, unsigned offset, int val)
{
	struct msm_gpio_chip *msm_gpio = to_msm_gpio_chip(chip);
	unsigned mask = 1 << offset;

	if (val)
		msm_gpio->shadow |= mask;
	else
		msm_gpio->shadow &= ~mask;

	writeb(msm_gpio->shadow, msm_gpio->reg);
}

static int msm_gpiolib_direction_input(struct gpio_chip *chip,
					unsigned offset)
{
	msm_gpiolib_set(chip, offset, 0);
	return 0;
}

static int msm_gpiolib_direction_output(struct gpio_chip *chip,
					 unsigned offset, int val)
{
	msm_gpiolib_set(chip, offset, val);
	return 0;
}

static int trout_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct msm_gpio_chip *msm_gpio = to_msm_gpio_chip(chip);

	return TROUT_GPIO_TO_INT(offset + chip->base);
}

#define TROUT_GPIO_BANK(name, reg_num, base_gpio, shadow_val)		\
	{								\
		.chip = {						\
			.label		  = name,			\
			.direction_input  = msm_gpiolib_direction_input,\
			.direction_output = msm_gpiolib_direction_output, \
			.get		  = msm_gpiolib_get,		\
			.set		  = msm_gpiolib_set,		\
			.to_irq		  = trout_gpio_to_irq,		\
			.base		  = base_gpio,			\
			.ngpio		  = 8,				\
		},							\
		.reg = (void *) reg_num + TROUT_CPLD_BASE,		\
		.shadow = shadow_val,					\
	}

static struct msm_gpio_chip msm_gpio_banks[] = {
#if defined(CONFIG_MSM_DEBUG_UART1)
	/* H2W pins <-> UART1 */
	TROUT_GPIO_BANK("MISC2", 0x00,   TROUT_GPIO_MISC2_BASE, 0x40),
#else
	/* H2W pins <-> UART3, Bluetooth <-> UART1 */
	TROUT_GPIO_BANK("MISC2", 0x00,   TROUT_GPIO_MISC2_BASE, 0x80),
#endif
	/* I2C pull */
	TROUT_GPIO_BANK("MISC3", 0x02,   TROUT_GPIO_MISC3_BASE, 0x04),
	TROUT_GPIO_BANK("MISC4", 0x04,   TROUT_GPIO_MISC4_BASE, 0),
	/* mmdi 32k en */
	TROUT_GPIO_BANK("MISC5", 0x06,   TROUT_GPIO_MISC5_BASE, 0x04),
	TROUT_GPIO_BANK("INT2", 0x08,    TROUT_GPIO_INT2_BASE,  0),
	TROUT_GPIO_BANK("MISC1", 0x0a,   TROUT_GPIO_MISC1_BASE, 0),
	TROUT_GPIO_BANK("VIRTUAL", 0x12, TROUT_GPIO_VIRTUAL_BASE, 0),
};

static void trout_gpio_irq_ack(struct irq_data *d)
{
	int bank = TROUT_INT_TO_BANK(d->irq);
	uint8_t mask = TROUT_INT_TO_MASK(d->irq);
	int reg = TROUT_BANK_TO_STAT_REG(bank);
	/*printk(KERN_INFO "trout_gpio_irq_ack irq %d\n", d->irq);*/
	writeb(mask, TROUT_CPLD_BASE + reg);
}

static void trout_gpio_irq_mask(struct irq_data *d)
{
	unsigned long flags;
	uint8_t reg_val;
	int bank = TROUT_INT_TO_BANK(d->irq);
	uint8_t mask = TROUT_INT_TO_MASK(d->irq);
	int reg = TROUT_BANK_TO_MASK_REG(bank);

	local_irq_save(flags);
	reg_val = trout_int_mask[bank] |= mask;
	/*printk(KERN_INFO "trout_gpio_irq_mask irq %d => %d:%02x\n",
	       d->irq, bank, reg_val);*/
	writeb(reg_val, TROUT_CPLD_BASE + reg);
	local_irq_restore(flags);
}

static void trout_gpio_irq_unmask(struct irq_data *d)
{
	unsigned long flags;
	uint8_t reg_val;
	int bank = TROUT_INT_TO_BANK(d->irq);
	uint8_t mask = TROUT_INT_TO_MASK(d->irq);
	int reg = TROUT_BANK_TO_MASK_REG(bank);

	local_irq_save(flags);
	reg_val = trout_int_mask[bank] &= ~mask;
	/*printk(KERN_INFO "trout_gpio_irq_unmask irq %d => %d:%02x\n",
	       d->irq, bank, reg_val);*/
	writeb(reg_val, TROUT_CPLD_BASE + reg);
	local_irq_restore(flags);
}

int trout_gpio_irq_set_wake(struct irq_data *d, unsigned int on)
{
	unsigned long flags;
	int bank = TROUT_INT_TO_BANK(d->irq);
	uint8_t mask = TROUT_INT_TO_MASK(d->irq);

	local_irq_save(flags);
	if(on)
		trout_sleep_int_mask[bank] &= ~mask;
	else
		trout_sleep_int_mask[bank] |= mask;
	local_irq_restore(flags);
	return 0;
}

static void trout_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	int j, m;
	unsigned v;
	int bank;
	int stat_reg;
	int int_base = TROUT_INT_START;
	uint8_t int_mask;

	for (bank = 0; bank < 2; bank++) {
		stat_reg = TROUT_BANK_TO_STAT_REG(bank);
		v = readb(TROUT_CPLD_BASE + stat_reg);
		int_mask = trout_int_mask[bank];
		if (v & int_mask) {
			writeb(v & int_mask, TROUT_CPLD_BASE + stat_reg);
			printk(KERN_ERR "trout_gpio_irq_handler: got masked "
			       "interrupt: %d:%02x\n", bank, v & int_mask);
		}
		v &= ~int_mask;
		while (v) {
			m = v & -v;
			j = fls(m) - 1;
			/*printk(KERN_INFO "msm_gpio_irq_handler %d:%02x %02x b"
			       "it %d irq %d\n", bank, v, m, j, int_base + j);*/
			v &= ~m;
			generic_handle_irq(int_base + j);
		}
		int_base += TROUT_INT_BANK0_COUNT;
	}
	desc->irq_data.chip->irq_ack(&desc->irq_data);
}

static struct irq_chip trout_gpio_irq_chip = {
	.name          = "troutgpio",
	.irq_ack       = trout_gpio_irq_ack,
	.irq_mask      = trout_gpio_irq_mask,
	.irq_unmask    = trout_gpio_irq_unmask,
	.irq_set_wake  = trout_gpio_irq_set_wake,
};

/*
 * Called from the processor-specific init to enable GPIO pin support.
 */
int __init trout_init_gpio(void)
{
	int i;
	for(i = TROUT_INT_START; i <= TROUT_INT_END; i++) {
		set_irq_chip(i, &trout_gpio_irq_chip);
		set_irq_handler(i, handle_edge_irq);
		set_irq_flags(i, IRQF_VALID);
	}

	for (i = 0; i < ARRAY_SIZE(msm_gpio_banks); i++)
		gpiochip_add(&msm_gpio_banks[i].chip);

	set_irq_type(MSM_GPIO_TO_INT(17), IRQF_TRIGGER_HIGH);
	set_irq_chained_handler(MSM_GPIO_TO_INT(17), trout_gpio_irq_handler);
	set_irq_wake(MSM_GPIO_TO_INT(17), 1);

	return 0;
}

postcore_initcall(trout_init_gpio);

