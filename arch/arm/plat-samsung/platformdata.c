/* linux/arch/arm/plat-samsung/platformdata.c
 *
 * Copyright 2010 Ben Dooks <ben-linux <at> fluff.org>
 *
 * Helper for platform data setting
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/platform_device.h>

#include <plat/devs.h>

void __init *s3c_set_platdata(void *pd, size_t pdsize,
			      struct platform_device *pdev)
{
	void *npd;

	if (!pd) {
		/* too early to use dev_name(), may not be registered */
		printk(KERN_ERR "%s: no platform data supplied\n", pdev->name);
		return NULL;
	}

	npd = kmemdup(pd, pdsize, GFP_KERNEL);
	if (!npd) {
		printk(KERN_ERR "%s: cannot clone platform data\n", pdev->name);
		return NULL;
	}

	pdev->dev.platform_data = npd;
	return npd;
}
