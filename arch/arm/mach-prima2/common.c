/*
 * Defines machines for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/irqchip.h>
#include <asm/sizes.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include "common.h"

static struct of_device_id sirfsoc_of_bus_ids[] __initdata = {
	{ .compatible = "simple-bus", },
	{},
};

void __init sirfsoc_mach_init(void)
{
	of_platform_bus_probe(NULL, sirfsoc_of_bus_ids, NULL);
}

void __init sirfsoc_init_late(void)
{
	sirfsoc_pm_init();
}

static __init void sirfsoc_init_time(void)
{
	/* initialize clocking early, we want to set the OS timer */
	sirfsoc_of_clk_init();
	clocksource_of_init();
}

static __init void sirfsoc_map_io(void)
{
	sirfsoc_map_lluart();
	sirfsoc_map_scu();
}

#ifdef CONFIG_ARCH_ATLAS6
static const char *atlas6_dt_match[] __initdata = {
	"sirf,atlas6",
	NULL
};

DT_MACHINE_START(ATLAS6_DT, "Generic ATLAS6 (Flattened Device Tree)")
	/* Maintainer: Barry Song <baohua.song@csr.com> */
	.nr_irqs	= 128,
	.map_io         = sirfsoc_map_io,
	.init_irq	= irqchip_init,
	.init_time	= sirfsoc_init_time,
	.init_machine	= sirfsoc_mach_init,
	.init_late	= sirfsoc_init_late,
	.dt_compat      = atlas6_dt_match,
	.restart	= sirfsoc_restart,
MACHINE_END
#endif

#ifdef CONFIG_ARCH_PRIMA2
static const char *prima2_dt_match[] __initdata = {
	"sirf,prima2",
	NULL
};

DT_MACHINE_START(PRIMA2_DT, "Generic PRIMA2 (Flattened Device Tree)")
	/* Maintainer: Barry Song <baohua.song@csr.com> */
	.nr_irqs	= 128,
	.map_io         = sirfsoc_map_io,
	.init_irq	= irqchip_init,
	.init_time	= sirfsoc_init_time,
	.dma_zone_size	= SZ_256M,
	.init_machine	= sirfsoc_mach_init,
	.init_late	= sirfsoc_init_late,
	.dt_compat      = prima2_dt_match,
	.restart	= sirfsoc_restart,
MACHINE_END
#endif

#ifdef CONFIG_ARCH_MARCO
static const char *marco_dt_match[] __initdata = {
	"sirf,marco",
	NULL
};

DT_MACHINE_START(MARCO_DT, "Generic MARCO (Flattened Device Tree)")
	/* Maintainer: Barry Song <baohua.song@csr.com> */
	.smp            = smp_ops(sirfsoc_smp_ops),
	.map_io         = sirfsoc_map_io,
	.init_irq	= irqchip_init,
	.init_time	= sirfsoc_init_time,
	.init_machine	= sirfsoc_mach_init,
	.init_late	= sirfsoc_init_late,
	.dt_compat      = marco_dt_match,
	.restart	= sirfsoc_restart,
MACHINE_END
#endif
