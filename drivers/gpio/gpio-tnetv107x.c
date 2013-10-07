/*
 * Texas Instruments TNETV107X GPIO Controller
 *
 * Copyright (C) 2010 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/platform_data/gpio-davinci.h>

#include <mach/common.h>
#include <mach/tnetv107x.h>

struct tnetv107x_gpio_regs {
	u32	idver;
	u32	data_in[3];
	u32	data_out[3];
	u32	direction[3];
	u32	enable[3];
};

#define gpio_reg_index(gpio)	((gpio) >> 5)
#define gpio_reg_bit(gpio)	BIT((gpio) & 0x1f)

#define gpio_reg_rmw(reg, mask, val)	\
	__raw_writel((__raw_readl(reg) & ~(mask)) | (val), (reg))

#define gpio_reg_set_bit(reg, gpio)	\
	gpio_reg_rmw((reg) + gpio_reg_index(gpio), 0, gpio_reg_bit(gpio))

#define gpio_reg_clear_bit(reg, gpio)	\
	gpio_reg_rmw((reg) + gpio_reg_index(gpio), gpio_reg_bit(gpio), 0)

#define gpio_reg_get_bit(reg, gpio)	\
	(__raw_readl((reg) + gpio_reg_index(gpio)) & gpio_reg_bit(gpio))

#define chip2controller(chip)		\
	container_of(chip, struct davinci_gpio_controller, chip)

#define TNETV107X_GPIO_CTLRS	DIV_ROUND_UP(TNETV107X_N_GPIO, 32)

static struct davinci_gpio_controller chips[TNETV107X_GPIO_CTLRS];

static int tnetv107x_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct davinci_gpio_controller *ctlr = chip2controller(chip);
	struct tnetv107x_gpio_regs __iomem *regs = ctlr->regs;
	unsigned gpio = chip->base + offset;
	unsigned long flags;

	spin_lock_irqsave(&ctlr->lock, flags);

	gpio_reg_set_bit(regs->enable, gpio);

	spin_unlock_irqrestore(&ctlr->lock, flags);

	return 0;
}

static void tnetv107x_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct davinci_gpio_controller *ctlr = chip2controller(chip);
	struct tnetv107x_gpio_regs __iomem *regs = ctlr->regs;
	unsigned gpio = chip->base + offset;
	unsigned long flags;

	spin_lock_irqsave(&ctlr->lock, flags);

	gpio_reg_clear_bit(regs->enable, gpio);

	spin_unlock_irqrestore(&ctlr->lock, flags);
}

static int tnetv107x_gpio_dir_in(struct gpio_chip *chip, unsigned offset)
{
	struct davinci_gpio_controller *ctlr = chip2controller(chip);
	struct tnetv107x_gpio_regs __iomem *regs = ctlr->regs;
	unsigned gpio = chip->base + offset;
	unsigned long flags;

	spin_lock_irqsave(&ctlr->lock, flags);

	gpio_reg_set_bit(regs->direction, gpio);

	spin_unlock_irqrestore(&ctlr->lock, flags);

	return 0;
}

static int tnetv107x_gpio_dir_out(struct gpio_chip *chip,
		unsigned offset, int value)
{
	struct davinci_gpio_controller *ctlr = chip2controller(chip);
	struct tnetv107x_gpio_regs __iomem *regs = ctlr->regs;
	unsigned gpio = chip->base + offset;
	unsigned long flags;

	spin_lock_irqsave(&ctlr->lock, flags);

	if (value)
		gpio_reg_set_bit(regs->data_out, gpio);
	else
		gpio_reg_clear_bit(regs->data_out, gpio);

	gpio_reg_clear_bit(regs->direction, gpio);

	spin_unlock_irqrestore(&ctlr->lock, flags);

	return 0;
}

static int tnetv107x_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct davinci_gpio_controller *ctlr = chip2controller(chip);
	struct tnetv107x_gpio_regs __iomem *regs = ctlr->regs;
	unsigned gpio = chip->base + offset;
	int ret;

	ret = gpio_reg_get_bit(regs->data_in, gpio);

	return ret ? 1 : 0;
}

static void tnetv107x_gpio_set(struct gpio_chip *chip,
		unsigned offset, int value)
{
	struct davinci_gpio_controller *ctlr = chip2controller(chip);
	struct tnetv107x_gpio_regs __iomem *regs = ctlr->regs;
	unsigned gpio = chip->base + offset;
	unsigned long flags;

	spin_lock_irqsave(&ctlr->lock, flags);

	if (value)
		gpio_reg_set_bit(regs->data_out, gpio);
	else
		gpio_reg_clear_bit(regs->data_out, gpio);

	spin_unlock_irqrestore(&ctlr->lock, flags);
}

static int __init tnetv107x_gpio_setup(void)
{
	int i, base;
	unsigned ngpio;
	struct davinci_soc_info *soc_info = &davinci_soc_info;
	struct tnetv107x_gpio_regs *regs;
	struct davinci_gpio_controller *ctlr;

	if (soc_info->gpio_type != GPIO_TYPE_TNETV107X)
		return 0;

	ngpio = soc_info->gpio_num;
	if (ngpio == 0) {
		pr_err("GPIO setup:  how many GPIOs?\n");
		return -EINVAL;
	}

	if (WARN_ON(TNETV107X_N_GPIO < ngpio))
		ngpio = TNETV107X_N_GPIO;

	regs = ioremap(soc_info->gpio_base, SZ_4K);
	if (WARN_ON(!regs))
		return -EINVAL;

	for (i = 0, base = 0; base < ngpio; i++, base += 32) {
		ctlr = &chips[i];

		ctlr->chip.label	= "tnetv107x";
		ctlr->chip.can_sleep	= 0;
		ctlr->chip.base		= base;
		ctlr->chip.ngpio	= ngpio - base;
		if (ctlr->chip.ngpio > 32)
			ctlr->chip.ngpio = 32;

		ctlr->chip.request		= tnetv107x_gpio_request;
		ctlr->chip.free			= tnetv107x_gpio_free;
		ctlr->chip.direction_input	= tnetv107x_gpio_dir_in;
		ctlr->chip.get			= tnetv107x_gpio_get;
		ctlr->chip.direction_output	= tnetv107x_gpio_dir_out;
		ctlr->chip.set			= tnetv107x_gpio_set;

		spin_lock_init(&ctlr->lock);

		ctlr->regs	= regs;
		ctlr->set_data	= &regs->data_out[i];
		ctlr->clr_data	= &regs->data_out[i];
		ctlr->in_data	= &regs->data_in[i];

		gpiochip_add(&ctlr->chip);
	}

	soc_info->gpio_ctlrs = chips;
	soc_info->gpio_ctlrs_num = DIV_ROUND_UP(ngpio, 32);
	return 0;
}
pure_initcall(tnetv107x_gpio_setup);
