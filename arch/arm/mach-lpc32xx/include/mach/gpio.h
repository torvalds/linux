/*
 * arch/arm/mach-lpc32xx/include/mach/gpio.h
 *
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2010 NXP Semiconductors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __ASM_ARCH_GPIO_H
#define __ASM_ARCH_GPIO_H

#include <asm-generic/gpio.h>

/*
 * Note!
 * Muxed GP pins need to be setup to the GP state in the board level
 * code prior to using this driver.
 * GPI pins : 28xP3 group
 * GPO pins : 24xP3 group
 * GPIO pins: 8xP0 group, 24xP1 group, 13xP2 group, 6xP3 group
 */

#define LPC32XX_GPIO_P0_MAX 8
#define LPC32XX_GPIO_P1_MAX 24
#define LPC32XX_GPIO_P2_MAX 13
#define LPC32XX_GPIO_P3_MAX 6
#define LPC32XX_GPI_P3_MAX 28
#define LPC32XX_GPO_P3_MAX 24

#define LPC32XX_GPIO_P0_GRP 0
#define LPC32XX_GPIO_P1_GRP (LPC32XX_GPIO_P0_GRP + LPC32XX_GPIO_P0_MAX)
#define LPC32XX_GPIO_P2_GRP (LPC32XX_GPIO_P1_GRP + LPC32XX_GPIO_P1_MAX)
#define LPC32XX_GPIO_P3_GRP (LPC32XX_GPIO_P2_GRP + LPC32XX_GPIO_P2_MAX)
#define LPC32XX_GPI_P3_GRP (LPC32XX_GPIO_P3_GRP + LPC32XX_GPIO_P3_MAX)
#define LPC32XX_GPO_P3_GRP (LPC32XX_GPI_P3_GRP + LPC32XX_GPI_P3_MAX)

/*
 * A specific GPIO can be selected with this macro
 * ie, GPIO_05 can be selected with LPC32XX_GPIO(LPC32XX_GPIO_P3_GRP, 5)
 * See the LPC32x0 User's guide for GPIO group numbers
 */
#define LPC32XX_GPIO(x, y) ((x) + (y))

static inline int gpio_get_value(unsigned gpio)
{
	return __gpio_get_value(gpio);
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	__gpio_set_value(gpio, value);
}

static inline int gpio_cansleep(unsigned gpio)
{
	return __gpio_cansleep(gpio);
}

static inline int gpio_to_irq(unsigned gpio)
{
	return __gpio_to_irq(gpio);
}

#endif
