/*
 *  Registration of Cobalt LCD platform device.
 *
 *  Copyright (C) 2008  Yoichi Yuasa <yuasa@linux-mips.org>
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>

static struct resource cobalt_lcd_resource __initdata = {
	.start	= 0x1f000000,
	.end	= 0x1f00001f,
	.flags	= IORESOURCE_MEM,
};

static __init int cobalt_lcd_add(void)
{
	struct platform_device *pdev;
	int retval;

	pdev = platform_device_alloc("cobalt-lcd", -1);
	if (!pdev)
		return -ENOMEM;

	retval = platform_device_add_resources(pdev, &cobalt_lcd_resource, 1);
	if (retval)
		goto err_free_device;

	retval = platform_device_add(pdev);
	if (retval)
		goto err_free_device;

	return 0;

err_free_device:
	platform_device_put(pdev);

	return retval;
}
device_initcall(cobalt_lcd_add);
