/*
 * MinnowBoard Linux platform driver
 * Copyright (c) 2013, Intel Corporation.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Author: Darren Hart <dvhart@linux.intel.com>
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/minnowboard.h>
#include "minnowboard-gpio.h"

static struct gpio expansion_gpios[] = {
	{ GPIO_PCH0, GPIOF_DIR_IN | GPIOF_EXPORT | GPIOF_EXPORT_CHANGEABLE,
	  "minnow_gpio_pch0" },
	{ GPIO_PCH1, GPIOF_DIR_IN | GPIOF_EXPORT | GPIOF_EXPORT_CHANGEABLE,
	  "minnow_gpio_pch1" },
	{ GPIO_PCH2, GPIOF_DIR_IN | GPIOF_EXPORT | GPIOF_EXPORT_CHANGEABLE,
	  "minnow_gpio_pch2" },
	{ GPIO_PCH3, GPIOF_DIR_IN | GPIOF_EXPORT | GPIOF_EXPORT_CHANGEABLE,
	  "minnow_gpio_pch3" },
	{ GPIO_PCH4, GPIOF_DIR_IN | GPIOF_EXPORT | GPIOF_EXPORT_CHANGEABLE,
	  "minnow_gpio_pch4" },
	{ GPIO_PCH5, GPIOF_DIR_IN | GPIOF_EXPORT | GPIOF_EXPORT_CHANGEABLE,
	  "minnow_gpio_pch5" },
	{ GPIO_PCH6, GPIOF_DIR_IN | GPIOF_EXPORT | GPIOF_EXPORT_CHANGEABLE,
	  "minnow_gpio_pch6" },
	{ GPIO_PCH7, GPIOF_DIR_IN | GPIOF_EXPORT | GPIOF_EXPORT_CHANGEABLE,
	  "minnow_gpio_pch7" },
};

static struct gpio expansion_aux_gpios[] = {
	{ GPIO_AUX0, GPIOF_DIR_IN | GPIOF_EXPORT | GPIOF_EXPORT_CHANGEABLE,
	  "minnow_gpio_aux0" },
	{ GPIO_AUX1, GPIOF_DIR_IN | GPIOF_EXPORT | GPIOF_EXPORT_CHANGEABLE,
	  "minnow_gpio_aux1" },
	{ GPIO_AUX2, GPIOF_DIR_IN | GPIOF_EXPORT | GPIOF_EXPORT_CHANGEABLE,
	  "minnow_gpio_aux2" },
	{ GPIO_AUX3, GPIOF_DIR_IN | GPIOF_EXPORT | GPIOF_EXPORT_CHANGEABLE,
	  "minnow_gpio_aux3" },
	{ GPIO_AUX4, GPIOF_DIR_IN | GPIOF_EXPORT | GPIOF_EXPORT_CHANGEABLE,
	  "minnow_gpio_aux4" },
};

static int __init minnow_gpio_module_init(void)
{
	int err;

	err = -ENODEV;
	if (!minnow_detect())
		goto out;

	/* Auxillary Expansion GPIOs */
	if (!minnow_lvds_detect()) {
		pr_debug("LVDS_DETECT not asserted, configuring Aux GPIO lines\n");
		err = gpio_request_array(expansion_aux_gpios,
					 ARRAY_SIZE(expansion_aux_gpios));
		if (err) {
			pr_err("Failed to request expansion aux GPIO lines\n");
			goto out;
		}
	} else {
		pr_debug("LVDS_DETECT asserted, ignoring aux GPIO lines\n");
	}

	/* Expansion GPIOs */
	err = gpio_request_array(expansion_gpios, ARRAY_SIZE(expansion_gpios));
	if (err) {
		pr_err("Failed to request expansion GPIO lines\n");
		if (minnow_lvds_detect())
			gpio_free_array(expansion_aux_gpios,
					ARRAY_SIZE(expansion_aux_gpios));
		goto out;
	}

 out:
	return err;
}

static void __exit minnow_gpio_module_exit(void)
{
	if (minnow_lvds_detect())
		gpio_free_array(expansion_aux_gpios,
				ARRAY_SIZE(expansion_aux_gpios));
	gpio_free_array(expansion_gpios, ARRAY_SIZE(expansion_gpios));
}

module_init(minnow_gpio_module_init);
module_exit(minnow_gpio_module_exit);

MODULE_LICENSE("GPL");
