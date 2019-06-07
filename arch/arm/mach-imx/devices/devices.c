// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2008 Sascha Hauer, kernel@pengutronix.de
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>

#include "../common.h"
#include "devices-common.h"

struct device mxc_aips_bus = {
	.init_name	= "mxc_aips",
};

struct device mxc_ahb_bus = {
	.init_name	= "mxc_ahb",
};

int __init mxc_device_init(void)
{
	int ret;

	ret = device_register(&mxc_aips_bus);
	if (ret < 0)
		goto done;

	ret = device_register(&mxc_ahb_bus);

done:
	return ret;
}
