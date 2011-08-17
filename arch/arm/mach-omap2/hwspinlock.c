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

#include <plat/omap_hwmod.h>
#include <plat/omap_device.h>

struct omap_device_pm_latency omap_spinlock_latency[] = {
	{
		.deactivate_func = omap_device_idle_hwmods,
		.activate_func   = omap_device_enable_hwmods,
		.flags = OMAP_DEVICE_LATENCY_AUTO_ADJUST,
	}
};

int __init hwspinlocks_init(void)
{
	int retval = 0;
	struct omap_hwmod *oh;
	struct omap_device *od;
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

	od = omap_device_build(dev_name, 0, oh, NULL, 0,
				omap_spinlock_latency,
				ARRAY_SIZE(omap_spinlock_latency), false);
	if (IS_ERR(od)) {
		pr_err("Can't build omap_device for %s:%s\n", dev_name,
								oh_name);
		retval = PTR_ERR(od);
	}

	return retval;
}
/* early board code might need to reserve specific hwspinlock instances */
postcore_initcall(hwspinlocks_init);
