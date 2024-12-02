// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2016 Broadcom

#include <linux/of_platform.h>

#include <asm/mach/arch.h>

static const char * const bcm23550_dt_compat[] = {
	"brcm,bcm23550",
	NULL,
};

DT_MACHINE_START(BCM23550_DT, "BCM23550 Broadcom Application Processor")
	.dt_compat = bcm23550_dt_compat,
MACHINE_END
