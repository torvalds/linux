/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 * Copyright (C) 2008-2011 Florian Fainelli <florian@openwrt.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/gpio/driver.h>

#include <bcm63xx_cpu.h>
#include <bcm63xx_gpio.h>
#include <bcm63xx_io.h>
#include <bcm63xx_regs.h>

static u32 gpio_out_low_reg;

static void bcm63xx_gpio_out_low_reg_init(void)
{
	switch (bcm63xx_get_cpu_id()) {
	case BCM6345_CPU_ID:
		gpio_out_low_reg = GPIO_DATA_LO_REG_6345;
		break;
	default:
		gpio_out_low_reg = GPIO_DATA_LO_REG;
		break;
	}
}

static DEFINE_SPINLOCK(bcm63xx_gpio_lock);
static u32 gpio_out_low, gpio_out_high;

static void bcm63xx_gpio_set(struct gpio_chip *chip,
			     unsigned gpio, int val)
{
	u32 reg;
	u32 mask;
	u32 *v;
	unsigned long flags;

	if (gpio >= chip->ngpio)
		BUG();

	if (gpio < 32) {
		reg = gpio_out_low_reg;
		mask = 1 << gpio;
		v = &gpio_out_low;
	} else {
		reg = GPIO_DATA_HI_REG;
		mask = 1 << (gpio - 32);
		v = &gpio_out_high;
	}

	spin_lock_irqsave(&bcm63xx_gpio_lock, flags);
	if (val)
		*v |= mask;
	else
		*v &= ~mask;
	bcm_gpio_writel(*v, reg);
	spin_unlock_irqrestore(&bcm63xx_gpio_lock, flags);
}

static int bcm63xx_gpio_get(struct gpio_chip *chip, unsigned gpio)
{
	u32 reg;
	u32 mask;

	if (gpio >= chip->ngpio)
		BUG();

	if (gpio < 32) {
		reg = gpio_out_low_reg;
		mask = 1 << gpio;
	} else {
		reg = GPIO_DATA_HI_REG;
		mask = 1 << (gpio - 32);
	}

	return !!(bcm_gpio_readl(reg) & mask);
}

static int bcm63xx_gpio_set_direction(struct gpio_chip *chip,
				      unsigned gpio, int dir)
{
	u32 reg;
	u32 mask;
	u32 tmp;
	unsigned long flags;

	if (gpio >= chip->ngpio)
		BUG();

	if (gpio < 32) {
		reg = GPIO_CTL_LO_REG;
		mask = 1 << gpio;
	} else {
		reg = GPIO_CTL_HI_REG;
		mask = 1 << (gpio - 32);
	}

	spin_lock_irqsave(&bcm63xx_gpio_lock, flags);
	tmp = bcm_gpio_readl(reg);
	if (dir == BCM63XX_GPIO_DIR_IN)
		tmp &= ~mask;
	else
		tmp |= mask;
	bcm_gpio_writel(tmp, reg);
	spin_unlock_irqrestore(&bcm63xx_gpio_lock, flags);

	return 0;
}

static int bcm63xx_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	return bcm63xx_gpio_set_direction(chip, gpio, BCM63XX_GPIO_DIR_IN);
}

static int bcm63xx_gpio_direction_output(struct gpio_chip *chip,
					 unsigned gpio, int value)
{
	bcm63xx_gpio_set(chip, gpio, value);
	return bcm63xx_gpio_set_direction(chip, gpio, BCM63XX_GPIO_DIR_OUT);
}


static struct gpio_chip bcm63xx_gpio_chip = {
	.label			= "bcm63xx-gpio",
	.direction_input	= bcm63xx_gpio_direction_input,
	.direction_output	= bcm63xx_gpio_direction_output,
	.get			= bcm63xx_gpio_get,
	.set			= bcm63xx_gpio_set,
	.base			= 0,
};

int __init bcm63xx_gpio_init(void)
{
	bcm63xx_gpio_out_low_reg_init();

	gpio_out_low = bcm_gpio_readl(gpio_out_low_reg);
	if (!BCMCPU_IS_6345())
		gpio_out_high = bcm_gpio_readl(GPIO_DATA_HI_REG);
	bcm63xx_gpio_chip.ngpio = bcm63xx_gpio_count();
	pr_info("registering %d GPIOs\n", bcm63xx_gpio_chip.ngpio);

	return gpiochip_add_data(&bcm63xx_gpio_chip, NULL);
}
