/*
 * OMAP2+ specific gpio initialization
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Author:
 *	Charulatha V <charu@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include <plat/omap_hwmod.h>
#include <plat/omap_device.h>

static struct omap_device_pm_latency omap_gpio_latency[] = {
	[0] = {
		.deactivate_func = omap_device_idle_hwmods,
		.activate_func   = omap_device_enable_hwmods,
		.flags		 = OMAP_DEVICE_LATENCY_AUTO_ADJUST,
	},
};

static int omap2_gpio_dev_init(struct omap_hwmod *oh, void *unused)
{
	struct omap_device *od;
	struct omap_gpio_platform_data *pdata;
	struct omap_gpio_dev_attr *dev_attr;
	char *name = "omap_gpio";
	int id;

	/*
	 * extract the device id from name field available in the
	 * hwmod database and use the same for constructing ids for
	 * gpio devices.
	 * CAUTION: Make sure the name in the hwmod database does
	 * not change. If changed, make corresponding change here
	 * or make use of static variable mechanism to handle this.
	 */
	sscanf(oh->name, "gpio%d", &id);

	pdata = kzalloc(sizeof(struct omap_gpio_platform_data), GFP_KERNEL);
	if (!pdata) {
		pr_err("gpio%d: Memory allocation failed\n", id);
		return -ENOMEM;
	}

	dev_attr = (struct omap_gpio_dev_attr *)oh->dev_attr;
	pdata->bank_width = dev_attr->bank_width;
	pdata->dbck_flag = dev_attr->dbck_flag;
	pdata->virtual_irq_start = IH_GPIO_BASE + 32 * (id - 1);

	switch (oh->class->rev) {
	case 0:
	case 1:
		pdata->bank_type = METHOD_GPIO_24XX;
		break;
	case 2:
		pdata->bank_type = METHOD_GPIO_44XX;
		break;
	default:
		WARN(1, "Invalid gpio bank_type\n");
		kfree(pdata);
		return -EINVAL;
	}

	od = omap_device_build(name, id - 1, oh, pdata,
				sizeof(*pdata),	omap_gpio_latency,
				ARRAY_SIZE(omap_gpio_latency),
				false);
	kfree(pdata);

	if (IS_ERR(od)) {
		WARN(1, "Can't build omap_device for %s:%s.\n",
					name, oh->name);
		return PTR_ERR(od);
	}

	gpio_bank_count++;
	return 0;
}

/*
 * gpio_init needs to be done before
 * machine_init functions access gpio APIs.
 * Hence gpio_init is a postcore_initcall.
 */
static int __init omap2_gpio_init(void)
{
	return omap_hwmod_for_each_by_class("gpio", omap2_gpio_dev_init,
						NULL);
}
postcore_initcall(omap2_gpio_init);
