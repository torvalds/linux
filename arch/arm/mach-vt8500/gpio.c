/* linux/arch/arm/mach-vt8500/gpio.c
 *
 * Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>

#include "devices.h"

#define to_vt8500(__chip) container_of(__chip, struct vt8500_gpio_chip, chip)

#define ENABLE_REGS	0x0
#define DIRECTION_REGS	0x20
#define OUTVALUE_REGS	0x40
#define INVALUE_REGS	0x60

#define EXT_REGOFF	0x1c

static void __iomem *regbase;

struct vt8500_gpio_chip {
	struct gpio_chip	chip;
	unsigned int		shift;
	unsigned int		regoff;
};

static int gpio_to_irq_map[8];

static int vt8500_muxed_gpio_request(struct gpio_chip *chip,
				     unsigned offset)
{
	struct vt8500_gpio_chip *vt8500_chip = to_vt8500(chip);
	unsigned val = readl(regbase + ENABLE_REGS + vt8500_chip->regoff);

	val |= (1 << vt8500_chip->shift << offset);
	writel(val, regbase + ENABLE_REGS + vt8500_chip->regoff);

	return 0;
}

static void vt8500_muxed_gpio_free(struct gpio_chip *chip,
				   unsigned offset)
{
	struct vt8500_gpio_chip *vt8500_chip = to_vt8500(chip);
	unsigned val = readl(regbase + ENABLE_REGS + vt8500_chip->regoff);

	val &= ~(1 << vt8500_chip->shift << offset);
	writel(val, regbase + ENABLE_REGS + vt8500_chip->regoff);
}

static int vt8500_muxed_gpio_direction_input(struct gpio_chip *chip,
				       unsigned offset)
{
	struct vt8500_gpio_chip *vt8500_chip = to_vt8500(chip);
	unsigned val = readl(regbase + DIRECTION_REGS + vt8500_chip->regoff);

	val &= ~(1 << vt8500_chip->shift << offset);
	writel(val, regbase + DIRECTION_REGS + vt8500_chip->regoff);

	return 0;
}

static int vt8500_muxed_gpio_direction_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	struct vt8500_gpio_chip *vt8500_chip = to_vt8500(chip);
	unsigned val = readl(regbase + DIRECTION_REGS + vt8500_chip->regoff);

	val |= (1 << vt8500_chip->shift << offset);
	writel(val, regbase + DIRECTION_REGS + vt8500_chip->regoff);

	if (value) {
		val = readl(regbase + OUTVALUE_REGS + vt8500_chip->regoff);
		val |= (1 << vt8500_chip->shift << offset);
		writel(val, regbase + OUTVALUE_REGS + vt8500_chip->regoff);
	}
	return 0;
}

static int vt8500_muxed_gpio_get_value(struct gpio_chip *chip,
				       unsigned offset)
{
	struct vt8500_gpio_chip *vt8500_chip = to_vt8500(chip);

	return (readl(regbase + INVALUE_REGS + vt8500_chip->regoff)
		>> vt8500_chip->shift >> offset) & 1;
}

static void vt8500_muxed_gpio_set_value(struct gpio_chip *chip,
					unsigned offset, int value)
{
	struct vt8500_gpio_chip *vt8500_chip = to_vt8500(chip);
	unsigned val = readl(regbase + INVALUE_REGS + vt8500_chip->regoff);

	if (value)
		val |= (1 << vt8500_chip->shift << offset);
	else
		val &= ~(1 << vt8500_chip->shift << offset);

	writel(val, regbase + INVALUE_REGS + vt8500_chip->regoff);
}

#define VT8500_GPIO_BANK(__name, __shift, __off, __base, __num)		\
{									\
	.chip = {							\
		.label			= __name,			\
		.request		= vt8500_muxed_gpio_request,	\
		.free			= vt8500_muxed_gpio_free,	\
		.direction_input  = vt8500_muxed_gpio_direction_input,	\
		.direction_output = vt8500_muxed_gpio_direction_output,	\
		.get			= vt8500_muxed_gpio_get_value,	\
		.set			= vt8500_muxed_gpio_set_value,	\
		.can_sleep		= 0,				\
		.base			= __base,			\
		.ngpio			= __num,			\
	},								\
	.shift		= __shift,					\
	.regoff		= __off,					\
}

static struct vt8500_gpio_chip vt8500_muxed_gpios[] = {
	VT8500_GPIO_BANK("uart0",	0,	0x0,	8,	4),
	VT8500_GPIO_BANK("uart1",	4,	0x0,	12,	4),
	VT8500_GPIO_BANK("spi0",	8,	0x0,	16,	4),
	VT8500_GPIO_BANK("spi1",	12,	0x0,	20,	4),
	VT8500_GPIO_BANK("spi2",	16,	0x0,	24,	4),
	VT8500_GPIO_BANK("pwmout",	24,	0x0,	28,	2),

	VT8500_GPIO_BANK("sdmmc",	0,	0x4,	30,	11),
	VT8500_GPIO_BANK("ms",		16,	0x4,	41,	7),
	VT8500_GPIO_BANK("i2c0",	24,	0x4,	48,	2),
	VT8500_GPIO_BANK("i2c1",	26,	0x4,	50,	2),

	VT8500_GPIO_BANK("mii",		0,	0x8,	52,	20),
	VT8500_GPIO_BANK("see",		20,	0x8,	72,	4),
	VT8500_GPIO_BANK("ide",		24,	0x8,	76,	7),

	VT8500_GPIO_BANK("ccir",	0,	0xc,	83,	19),

	VT8500_GPIO_BANK("ts",		8,	0x10,	102,	11),

	VT8500_GPIO_BANK("lcd",		0,	0x14,	113,	23),
};

static int vt8500_gpio_direction_input(struct gpio_chip *chip,
				       unsigned offset)
{
	unsigned val = readl(regbase + DIRECTION_REGS + EXT_REGOFF);

	val &= ~(1 << offset);
	writel(val, regbase + DIRECTION_REGS + EXT_REGOFF);
	return 0;
}

static int vt8500_gpio_direction_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	unsigned val = readl(regbase + DIRECTION_REGS + EXT_REGOFF);

	val |= (1 << offset);
	writel(val, regbase + DIRECTION_REGS + EXT_REGOFF);

	if (value) {
		val = readl(regbase + OUTVALUE_REGS + EXT_REGOFF);
		val |= (1 << offset);
		writel(val, regbase + OUTVALUE_REGS + EXT_REGOFF);
	}
	return 0;
}

static int vt8500_gpio_get_value(struct gpio_chip *chip,
				       unsigned offset)
{
	return (readl(regbase + INVALUE_REGS + EXT_REGOFF) >> offset) & 1;
}

static void vt8500_gpio_set_value(struct gpio_chip *chip,
					unsigned offset, int value)
{
	unsigned val = readl(regbase + OUTVALUE_REGS + EXT_REGOFF);

	if (value)
		val |= (1 << offset);
	else
		val &= ~(1 << offset);

	writel(val, regbase + OUTVALUE_REGS + EXT_REGOFF);
}

static int vt8500_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	if (offset > 7)
		return -EINVAL;

	return gpio_to_irq_map[offset];
}

static struct gpio_chip vt8500_external_gpios = {
	.label			= "extgpio",
	.direction_input	= vt8500_gpio_direction_input,
	.direction_output	= vt8500_gpio_direction_output,
	.get			= vt8500_gpio_get_value,
	.set			= vt8500_gpio_set_value,
	.to_irq			= vt8500_gpio_to_irq,
	.can_sleep		= 0,
	.base			= 0,
	.ngpio			= 8,
};

void __init vt8500_gpio_init(void)
{
	int i;

	for (i = 0; i < 8; i++)
		gpio_to_irq_map[i] = wmt_gpio_ext_irq[i];

	regbase = ioremap(wmt_gpio_base, SZ_64K);
	if (!regbase) {
		printk(KERN_ERR "Failed to map MMIO registers for GPIO\n");
		return;
	}

	gpiochip_add(&vt8500_external_gpios);

	for (i = 0; i < ARRAY_SIZE(vt8500_muxed_gpios); i++)
		gpiochip_add(&vt8500_muxed_gpios[i].chip);
}
