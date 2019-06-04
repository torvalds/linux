// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Samsung Electronics.
 * Kyungmin Park <kyungmin.park@samsung.com>
 * Tomasz Figa <t.figa@samsung.com>
 */

#include <linux/kernel.h>
#include <linux/suspend.h>

#include <asm/firmware.h>

static const struct firmware_ops default_firmware_ops;

const struct firmware_ops *firmware_ops = &default_firmware_ops;
