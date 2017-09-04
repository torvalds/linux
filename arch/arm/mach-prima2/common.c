/*
 * Defines machines for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/sizes.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include "common.h"

static void __init __maybe_unused sirfsoc_init_late(void)
{
	sirfsoc_pm_init();
}

#ifdef CONFIG_ARCH_ATLAS6
static const char *const atlas6_dt_match[] __initconst = {
	"sirf,atlas6",
	NULL
};

DT_MACHINE_START(ATLAS6_DT, "Generic ATLAS6 (Flattened Device Tree)")
	/* Maintainer: Barry Song <baohua.song@csr.com> */
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.init_late	= sirfsoc_init_late,
	.dt_compat      = atlas6_dt_match,
MACHINE_END
#endif

#ifdef CONFIG_ARCH_PRIMA2
static const char *const prima2_dt_match[] __initconst = {
	"sirf,prima2",
	NULL
};

DT_MACHINE_START(PRIMA2_DT, "Generic PRIMA2 (Flattened Device Tree)")
	/* Maintainer: Barry Song <baohua.song@csr.com> */
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.dma_zone_size	= SZ_256M,
	.init_late	= sirfsoc_init_late,
	.dt_compat      = prima2_dt_match,
MACHINE_END
#endif

#ifdef CONFIG_ARCH_ATLAS7
static const char *const atlas7_dt_match[] __initconst = {
	"sirf,atlas7",
	NULL
};

DT_MACHINE_START(ATLAS7_DT, "Generic ATLAS7 (Flattened Device Tree)")
	/* Maintainer: Barry Song <baohua.song@csr.com> */
	.smp            = smp_ops(sirfsoc_smp_ops),
	.dt_compat      = atlas7_dt_match,
MACHINE_END
#endif
