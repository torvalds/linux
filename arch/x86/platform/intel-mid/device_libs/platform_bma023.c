// SPDX-License-Identifier: GPL-2.0-only
/*
 * platform_bma023.c: bma023 platform data initialization file
 *
 * (C) Copyright 2013 Intel Corporation
 */

#include <asm/intel-mid.h>

static const struct devs_id bma023_dev_id __initconst = {
	.name = "bma023",
	.type = SFI_DEV_TYPE_I2C,
	.delay = 1,
};

sfi_device(bma023_dev_id);
