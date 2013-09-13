/*
 *  Registration of Cobalt MTD device.
 *
 *  Copyright (C) 2006  Yoichi Yuasa <yuasa@linux-mips.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>

static struct mtd_partition cobalt_mtd_partitions[] = {
	{
		.name	= "firmware",
		.offset = 0x0,
		.size	= 0x80000,
	},
};

static struct physmap_flash_data cobalt_flash_data = {
	.width		= 1,
	.nr_parts	= 1,
	.parts		= cobalt_mtd_partitions,
};

static struct resource cobalt_mtd_resource = {
	.start	= 0x1fc00000,
	.end	= 0x1fc7ffff,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device cobalt_mtd = {
	.name		= "physmap-flash",
	.dev		= {
		.platform_data	= &cobalt_flash_data,
	},
	.num_resources	= 1,
	.resource	= &cobalt_mtd_resource,
};

static int __init cobalt_mtd_init(void)
{
	platform_device_register(&cobalt_mtd);

	return 0;
}

module_init(cobalt_mtd_init);
