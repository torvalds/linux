/*
 * Copyright (C) 2010 Bluecherry, LLC www.bluecherrydvr.com
 * Copyright (C) 2010 Ben Collins <bcollins@bluecherry.net>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <asm/uaccess.h>

#include "solo6010.h"

static void solo_gpio_mode(struct solo6010_dev *solo_dev,
			   unsigned int port_mask, unsigned int mode)
{
	int port;
	unsigned int ret;

	ret = solo_reg_read(solo_dev, SOLO_GPIO_CONFIG_0);

	/* To set gpio */
	for (port = 0; port < 16; port++) {
		if (!((1 << port) & port_mask))
			continue;

		ret &= (~(3 << (port << 1)));
		ret |= ((mode & 3) << (port << 1));
	}

	solo_reg_write(solo_dev, SOLO_GPIO_CONFIG_0, ret);

	/* To set extended gpio - sensor */
	ret = solo_reg_read(solo_dev, SOLO_GPIO_CONFIG_1);

	for (port = 0; port < 16; port++) {
		if (!((1 << (port + 16)) & port_mask))
			continue;

		if (!mode)
			ret &= ~(1 << port);
		else
			ret |= 1 << port;
	}

	solo_reg_write(solo_dev, SOLO_GPIO_CONFIG_1, ret);
}

static void solo_gpio_set(struct solo6010_dev *solo_dev, unsigned int value)
{
	solo_reg_write(solo_dev, SOLO_GPIO_DATA_OUT,
		       solo_reg_read(solo_dev, SOLO_GPIO_DATA_OUT) | value);
}

static void solo_gpio_clear(struct solo6010_dev *solo_dev, unsigned int value)
{
	solo_reg_write(solo_dev, SOLO_GPIO_DATA_OUT,
		       solo_reg_read(solo_dev, SOLO_GPIO_DATA_OUT) & ~value);
}

static void solo_gpio_config(struct solo6010_dev *solo_dev)
{
	/* Video reset */
	solo_gpio_mode(solo_dev, 0x30, 1);
	solo_gpio_clear(solo_dev, 0x30);
	udelay(100);
	solo_gpio_set(solo_dev, 0x30);
	udelay(100);

	/* Warning: Don't touch the next line unless you're sure of what
	 * you're doing: first four gpio [0-3] are used for video. */
	solo_gpio_mode(solo_dev, 0x0f, 2);

	/* We use bit 8-15 of SOLO_GPIO_CONFIG_0 for relay purposes */
	solo_gpio_mode(solo_dev, 0xff00, 1);

	/* Initially set relay status to 0 */
	solo_gpio_clear(solo_dev, 0xff00);
}

int solo_gpio_init(struct solo6010_dev *solo_dev)
{
        solo_gpio_config(solo_dev);
        return 0;
}

void solo_gpio_exit(struct solo6010_dev *solo_dev)
{
	solo_gpio_clear(solo_dev, 0x30);
	solo_gpio_config(solo_dev);
}
