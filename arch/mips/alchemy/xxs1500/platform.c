/*
 * XXS1500 board platform device registration
 *
 * Copyright (C) 2009 Manuel Lauss
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

#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/mach-au1x00/au1000.h>

static struct resource xxs1500_pcmcia_res[] = {
	{
		.name	= "pcmcia-io",
		.flags	= IORESOURCE_MEM,
		.start	= AU1000_PCMCIA_IO_PHYS_ADDR,
		.end	= AU1000_PCMCIA_IO_PHYS_ADDR + 0x000400000 - 1,
	},
	{
		.name	= "pcmcia-attr",
		.flags	= IORESOURCE_MEM,
		.start	= AU1000_PCMCIA_ATTR_PHYS_ADDR,
		.end	= AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x000400000 - 1,
	},
	{
		.name	= "pcmcia-mem",
		.flags	= IORESOURCE_MEM,
		.start	= AU1000_PCMCIA_MEM_PHYS_ADDR,
		.end	= AU1000_PCMCIA_MEM_PHYS_ADDR + 0x000400000 - 1,
	},
};

static struct platform_device xxs1500_pcmcia_dev = {
	.name		= "xxs1500_pcmcia",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(xxs1500_pcmcia_res),
	.resource	= xxs1500_pcmcia_res,
};

static struct platform_device *xxs1500_devs[] __initdata = {
	&xxs1500_pcmcia_dev,
};

static int __init xxs1500_dev_init(void)
{
	return platform_add_devices(xxs1500_devs,
				    ARRAY_SIZE(xxs1500_devs));
}
device_initcall(xxs1500_dev_init);
