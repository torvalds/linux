/*
 *  Cobalt buttons platform device.
 *
 *  Copyright (C) 2007  Yoichi Yuasa <yuasa@linux-mips.org>
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

#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/init.h>

static struct resource cobalt_buttons_resource __initdata = {
	.start	= 0x1d000000,
	.end	= 0x1d000003,
	.flags	= IORESOURCE_MEM,
};

static __init int cobalt_add_buttons(void)
{
	struct platform_device *pd;
	int error;

	pd = platform_device_alloc("Cobalt buttons", -1);
	if (!pd)
		return -ENOMEM;

	error = platform_device_add_resources(pd, &cobalt_buttons_resource, 1);
	if (error)
		goto err_free_device;

	error = platform_device_add(pd);
	if (error)
		goto err_free_device;

	return 0;

 err_free_device:
	platform_device_put(pd);
	return error;
}
device_initcall(cobalt_add_buttons);
