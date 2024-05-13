/* SPDX-License-Identifier: GPL-2.0 */
/*
 * gpio-au1300.h -- GPIO control for Au1300 GPIC and compatibles.
 *
 * Copyright (c) 2009-2011 Manuel Lauss <manuel.lauss@googlemail.com>
 */

#ifndef _GPIO_AU1300_H_
#define _GPIO_AU1300_H_

#include <asm/addrspace.h>
#include <asm/io.h>
#include <asm/mach-au1x00/au1000.h>

struct gpio;
struct gpio_chip;

/* with the current GPIC design, up to 128 GPIOs are possible.
 * The only implementation so far is in the Au1300, which has 75 externally
 * available GPIOs.
 */
#define AU1300_GPIO_BASE	0
#define AU1300_GPIO_NUM		75
#define AU1300_GPIO_MAX		(AU1300_GPIO_BASE + AU1300_GPIO_NUM - 1)

#define AU1300_GPIC_ADDR	\
	(void __iomem *)KSEG1ADDR(AU1300_GPIC_PHYS_ADDR)

static inline int au1300_gpio_get_value(unsigned int gpio)
{
	void __iomem *roff = AU1300_GPIC_ADDR;
	int bit;

	gpio -= AU1300_GPIO_BASE;
	roff += GPIC_GPIO_BANKOFF(gpio);
	bit = GPIC_GPIO_TO_BIT(gpio);
	return __raw_readl(roff + AU1300_GPIC_PINVAL) & bit;
}

static inline int au1300_gpio_direction_input(unsigned int gpio)
{
	void __iomem *roff = AU1300_GPIC_ADDR;
	unsigned long bit;

	gpio -= AU1300_GPIO_BASE;

	roff += GPIC_GPIO_BANKOFF(gpio);
	bit = GPIC_GPIO_TO_BIT(gpio);
	__raw_writel(bit, roff + AU1300_GPIC_DEVCLR);
	wmb();

	return 0;
}

static inline int au1300_gpio_set_value(unsigned int gpio, int v)
{
	void __iomem *roff = AU1300_GPIC_ADDR;
	unsigned long bit;

	gpio -= AU1300_GPIO_BASE;

	roff += GPIC_GPIO_BANKOFF(gpio);
	bit = GPIC_GPIO_TO_BIT(gpio);
	__raw_writel(bit, roff + (v ? AU1300_GPIC_PINVAL
				    : AU1300_GPIC_PINVALCLR));
	wmb();

	return 0;
}

static inline int au1300_gpio_direction_output(unsigned int gpio, int v)
{
	/* hw switches to output automatically */
	return au1300_gpio_set_value(gpio, v);
}

static inline int au1300_gpio_to_irq(unsigned int gpio)
{
	return AU1300_FIRST_INT + (gpio - AU1300_GPIO_BASE);
}

static inline int au1300_irq_to_gpio(unsigned int irq)
{
	return (irq - AU1300_FIRST_INT) + AU1300_GPIO_BASE;
}

static inline int au1300_gpio_is_valid(unsigned int gpio)
{
	int ret;

	switch (alchemy_get_cputype()) {
	case ALCHEMY_CPU_AU1300:
		ret = ((gpio >= AU1300_GPIO_BASE) && (gpio <= AU1300_GPIO_MAX));
		break;
	default:
		ret = 0;
	}
	return ret;
}

/* hardware remembers gpio 0-63 levels on powerup */
static inline int au1300_gpio_getinitlvl(unsigned int gpio)
{
	void __iomem *roff = AU1300_GPIC_ADDR;
	unsigned long v;

	if (unlikely(gpio > 63))
		return 0;
	else if (gpio > 31) {
		gpio -= 32;
		roff += 4;
	}

	v = __raw_readl(roff + AU1300_GPIC_RSTVAL);
	return (v >> gpio) & 1;
}

#endif /* _GPIO_AU1300_H_ */
