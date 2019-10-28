// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014-2016 Qualcomm Atheros, Inc.
 */

#include <linux/device.h>
#include "wil_platform.h"

int __init wil_platform_modinit(void)
{
	return 0;
}

void wil_platform_modexit(void)
{
}

/**
 * wil_platform_init() - wil6210 platform module init
 *
 * The function must be called before all other functions in this module.
 * It returns a handle which is used with the rest of the API
 *
 */
void *wil_platform_init(struct device *dev, struct wil_platform_ops *ops,
			const struct wil_platform_rops *rops, void *wil_handle)
{
	void *handle = ops; /* to return some non-NULL for 'void' impl. */

	if (!ops) {
		dev_err(dev,
			"Invalid parameter. Cannot init platform module\n");
		return NULL;
	}

	/* platform specific init functions should be called here */

	return handle;
}
