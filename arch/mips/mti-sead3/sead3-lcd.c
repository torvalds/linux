/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/init.h>
#include <linux/platform_device.h>

static struct resource __initdata sead3_lcd_resource = {
		.start	= 0x1f000400,
		.end	= 0x1f00041f,
		.flags	= IORESOURCE_MEM,
};

static __init int sead3_lcd_add(void)
{
	struct platform_device *pdev;
	int retval;

	/* SEAD-3 and Cobalt platforms use same display type. */
	pdev = platform_device_alloc("cobalt-lcd", -1);
	if (!pdev)
		return -ENOMEM;

	retval = platform_device_add_resources(pdev, &sead3_lcd_resource, 1);
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

device_initcall(sead3_lcd_add);
