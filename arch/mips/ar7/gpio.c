/*
 * Copyright (C) 2007 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2007 Eugene Konev <ejka@openwrt.org>
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

#include <linux/module.h>

#include <asm/mach-ar7/gpio.h>

static const char *ar7_gpio_list[AR7_GPIO_MAX];

int gpio_request(unsigned gpio, const char *label)
{
	if (gpio >= AR7_GPIO_MAX)
		return -EINVAL;

	if (ar7_gpio_list[gpio])
		return -EBUSY;

	if (label)
		ar7_gpio_list[gpio] = label;
	else
		ar7_gpio_list[gpio] = "busy";

	return 0;
}
EXPORT_SYMBOL(gpio_request);

void gpio_free(unsigned gpio)
{
	BUG_ON(!ar7_gpio_list[gpio]);
	ar7_gpio_list[gpio] = NULL;
}
EXPORT_SYMBOL(gpio_free);
