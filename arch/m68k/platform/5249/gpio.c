/*
 * Coldfire generic GPIO support
 *
 * (C) Copyright 2009, Steven King <sfking@fdwdc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
*/

#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfgpio.h>

struct mcf_gpio_chip mcf_gpio_chips[] = {
	MCFGPS(GPIO0, 0, 32, MCFSIM2_GPIOENABLE, MCFSIM2_GPIOWRITE, MCFSIM2_GPIOREAD),
	MCFGPS(GPIO1, 32, 32, MCFSIM2_GPIO1ENABLE, MCFSIM2_GPIO1WRITE, MCFSIM2_GPIO1READ),
};

unsigned int mcf_gpio_chips_size = ARRAY_SIZE(mcf_gpio_chips);
