/*
 * Copyright (C) 2007-2009 Florian Fainelli <florian@openwrt.org>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __AR7_GPIO_H__
#define __AR7_GPIO_H__

#include <asm/mach-ar7/ar7.h>

#define AR7_GPIO_MAX 32
#define NR_BUILTIN_GPIO AR7_GPIO_MAX

#define gpio_to_irq(gpio)	-1

#define gpio_get_value __gpio_get_value
#define gpio_set_value __gpio_set_value

#define gpio_cansleep __gpio_cansleep

/* Board specific GPIO functions */
int ar7_gpio_enable(unsigned gpio);
int ar7_gpio_disable(unsigned gpio);

#include <asm-generic/gpio.h>

#endif
