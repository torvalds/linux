// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Registration of Cobalt LCD platform device.
 *
 *  Copyright (C) 2008  Yoichi Yuasa <yuasa@linux-mips.org>
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
