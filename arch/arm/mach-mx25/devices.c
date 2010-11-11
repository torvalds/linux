/*
 * Copyright 2009 Sascha Hauer, <kernel@pengutronix.de>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <mach/mx25.h>
#include <mach/irqs.h>

static struct resource mx25_csi_resources[] = {
	{
		.start	= MX25_CSI_BASE_ADDR,
		.end	= MX25_CSI_BASE_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= MX25_INT_CSI,
		.flags	= IORESOURCE_IRQ
	},
};

struct platform_device mx25_csi_device = {
	.name	= "mx2-camera",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(mx25_csi_resources),
	.resource	= mx25_csi_resources,
	.dev		= {
		.coherent_dma_mask = 0xffffffff,
	},
};
