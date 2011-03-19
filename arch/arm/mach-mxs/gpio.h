/*
 * Copyright 2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#ifndef __MXS_GPIO_H__
#define __MXS_GPIO_H__

struct mxs_gpio_port {
	void __iomem *base;
	int id;
	int irq;
	int irq_high;
	int virtual_irq_start;
	struct gpio_chip chip;
};

int mxs_gpio_init(struct mxs_gpio_port*, int);

#endif /* __MXS_GPIO_H__ */
