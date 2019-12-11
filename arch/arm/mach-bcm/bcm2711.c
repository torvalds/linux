// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Stefan Wahren
 */

#include <linux/of_address.h>

#include <asm/mach/arch.h>

#include "platsmp.h"

static const char * const bcm2711_compat[] = {
#ifdef CONFIG_ARCH_MULTI_V7
	"brcm,bcm2711",
#endif
};

DT_MACHINE_START(BCM2711, "BCM2711")
#ifdef CONFIG_ZONE_DMA
	.dma_zone_size	= SZ_1G,
#endif
	.dt_compat = bcm2711_compat,
	.smp = smp_ops(bcm2836_smp_ops),
MACHINE_END
