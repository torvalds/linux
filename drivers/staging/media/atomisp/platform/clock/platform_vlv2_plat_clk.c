/*
 * platform_vlv2_plat_clk.c - VLV2 platform clock driver
 * Copyright (C) 2013 Intel Corporation
 *
 * Author: Asutosh Pathak <asutosh.pathak@intel.com>
 * Author: Chandra Sekhar Anagani <chandra.sekhar.anagani@intel.com>
 * Author: Sergio Aguirre <sergio.a.aguirre.rodriguez@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/printk.h>

static int __init vlv2_plat_clk_init(void)
{
	struct platform_device *pdev;

	pdev = platform_device_register_simple("vlv2_plat_clk", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		pr_err("platform_vlv2_plat_clk:register failed: %ld\n",
			PTR_ERR(pdev));
		return PTR_ERR(pdev);
	}

	return 0;
}

device_initcall(vlv2_plat_clk_init);
