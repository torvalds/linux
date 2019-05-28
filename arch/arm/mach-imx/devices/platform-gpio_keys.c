// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc. All Rights Reserved.
 */
#include <linux/sizes.h>

#include "../hardware.h"
#include "devices-common.h"

struct platform_device *__init imx_add_gpio_keys(
		const struct gpio_keys_platform_data *pdata)
{
	return imx_add_platform_device("gpio-keys", -1, NULL,
		 0, pdata, sizeof(*pdata));
}
