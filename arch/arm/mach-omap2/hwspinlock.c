/*
 * OMAP hardware spinlock device initialization
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com
 *
 * Contact: Simon Que <sque@ti.com>
 *          Hari Kanigeri <h-kanigeri2@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/hwspinlock.h>

#include "soc.h"
#include "omap_hwmod.h"
#include "omap_device.h"

static struct hwspinlock_pdata omap_hwspinlock_pdata __initdata = {
	.base_id = 0,
};

static int __init hwspinlocks_init(void)
{
	int retval = 0;
	struct omap_hwmod *oh;
	struct platform_device *pdev;
	const char *oh_name = "spinlock";
	const char *dev_name = "omap_hwspinlock";

	/*
	 * Hwmod lookup will fail in case our platform doesn't support the
	 * hardware spinlock module, so it is safe to run this initcall
	 * on all omaps
	 */
	oh = omap_hwmod_lookup(oh_name);
	if (oh == NULL)
		return -EINVAL;

	pdev = omap_device_build(dev_name, 0, oh, &omap_hwspinlock_pdata,
				sizeof(struct hwspinlock_pdata));
	if (IS_ERR(pdev)) {
		pr_err("Can't build omap_device for %s:%s\n", dev_name,
								oh_name);
		retval = PTR_ERR(pdev);
	}

	return retval;
}
/* early board code might need to reserve specific hwspinlock instances */
omap_postcore_initcall(hwspinlocks_init);
