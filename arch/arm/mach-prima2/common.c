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

static void __init sirfsoc_init_late(void)
{
	sirfsoc_pm_init();
}

static __init void sirfsoc_map_io(void)
{
	sirfsoc_map_lluart();
	sirfsoc_map_scu();
}

#ifdef CONFIG_ARCH_ATLAS6
static const char *atlas6_dt_match[] __initconst = {
	"sirf,atlas6",
	NULL
};

DT_MACHINE_START(ATLAS6_DT, "Generic ATLAS6 (Flattened Device Tree)")
	/* Maintainer: Barry Song <baohua.song@csr.com> */
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.map_io         = sirfsoc_map_io,
	.init_late	= sirfsoc_init_late,
	.dt_compat      = atlas6_dt_match,
MACHINE_END
#endif

#ifdef CONFIG_ARCH_PRIMA2
static const char *prima2_dt_match[] __initconst = {
	"sirf,prima2",
	NULL
};

DT_MACHINE_START(PRIMA2_DT, "Generic PRIMA2 (Flattened Device Tree)")
	/* Maintainer: Barry Song <baohua.song@csr.com> */
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.map_io         = sirfsoc_map_io,
	.dma_zone_size	= SZ_256M,
	.init_late	= sirfsoc_init_late,
	.dt_compat      = prima2_dt_match,
MACHINE_END
#endif

#ifdef CONFIG_ARCH_MARCO
static const char *marco_dt_match[] __initconst = {
	"sirf,marco",
	NULL
};

DT_MACHINE_START(MARCO_DT, "Generic MARCO (Flattened Device Tree)")
	/* Maintainer: Barry Song <baohua.song@csr.com> */
	.l2c_aux_val	= 0,
	.l2c_aux_mask	= ~0,
	.smp            = smp_ops(sirfsoc_smp_ops),
	.map_io         = sirfsoc_map_io,
	.init_late	= sirfsoc_init_late,
	.dt_compat      = marco_dt_match,
MACHINE_END
#endif
