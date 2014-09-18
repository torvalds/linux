/*
 * platform_bma023.c: bma023 platform data initilization file
 *
 * (C) Copyright 2013 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <asm/intel-mid.h>

static const struct devs_id bma023_dev_id __initconst = {
	.name = "bma023",
	.type = SFI_DEV_TYPE_I2C,
	.delay = 1,
};

sfi_device(bma023_dev_id);
