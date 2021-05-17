// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2005 Nokia Corporation
 * Author: Paul Mundt <paul.mundt@nokia.com>
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - https://www.ti.com/
 *
 * Modified from the original mach-omap/omap2/board-generic.c did by Paul
 * to support the OMAP2+ device tree boards with an unique board file.
 */
#include <linux/io.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/irqdomain.h>
#include <linux/clocksource.h>

#include <asm/setup.h>
#include <asm/mach/arch.h>
#include <asm/system_info.h>

#include "common.h"

static const struct of_device_id omap_dt_match_table[] __initconst = {
	{ .compatible = "simple-bus", },
	{ .compatible = "ti,omap-infra", },
	{ }
};

static void __init __maybe_unused omap_generic_init(void)
{
	pdata_quirks_init(omap_dt_match_table);
	omap_soc_device_init();
}

/* Clocks are needed early, see drivers/clocksource for the rest */
static void __init __maybe_unused omap_init_time_of(void)
{
	omap_clk_init();
	timer_probe();
}

/* Used by am437x for ARM timer in non-SMP configurations */
#if !defined(CONFIG_SMP) && defined(CONFIG_GENERIC_CLOCKEVENTS_BROADCAST)
void tick_broadcast(const struct cpumask *mask)
{
}
#endif

#ifdef CONFIG_SOC_OMAP2420
static const char *const omap242x_boards_compat[] __initconst = {
	"ti,omap2420",
	NULL,
};

DT_MACHINE_START(OMAP242X_DT, "Generic OMAP2420 (Flattened Device Tree)")
	.reserve	= omap_reserve,
	.map_io		= omap242x_map_io,
	.init_early	= omap2420_init_early,
	.init_machine	= omap_generic_init,
	.init_time	= omap_init_time_of,
	.dt_compat	= omap242x_boards_compat,
	.restart	= omap2xxx_restart,
MACHINE_END
#endif

#ifdef CONFIG_SOC_OMAP2430
static const char *const omap243x_boards_compat[] __initconst = {
	"ti,omap2430",
	NULL,
};

DT_MACHINE_START(OMAP243X_DT, "Generic OMAP2430 (Flattened Device Tree)")
	.reserve	= omap_reserve,
	.map_io		= omap243x_map_io,
	.init_early	= omap2430_init_early,
	.init_machine	= omap_generic_init,
	.init_time	= omap_init_time_of,
	.dt_compat	= omap243x_boards_compat,
	.restart	= omap2xxx_restart,
MACHINE_END
#endif

#ifdef CONFIG_ARCH_OMAP3
/* Some boards need board name for legacy userspace in /proc/cpuinfo */
static const char *const n900_boards_compat[] __initconst = {
	"nokia,omap3-n900",
	NULL,
};

/* Set system_rev from atags */
static void __init rx51_set_system_rev(const struct tag *tags)
{
	const struct tag *tag;

	if (tags->hdr.tag != ATAG_CORE)
		return;

	for_each_tag(tag, tags) {
		if (tag->hdr.tag == ATAG_REVISION) {
			system_rev = tag->u.revision.rev;
			break;
		}
	}
}

/* Legacy userspace on Nokia N900 needs ATAGS exported in /proc/atags,
 * save them while the data is still not overwritten
 */
static void __init rx51_reserve(void)
{
	const struct tag *tags = (const struct tag *)(PAGE_OFFSET + 0x100);

	save_atags(tags);
	rx51_set_system_rev(tags);
	omap_reserve();
}

DT_MACHINE_START(OMAP3_N900_DT, "Nokia RX-51 board")
	.reserve	= rx51_reserve,
	.map_io		= omap3_map_io,
	.init_early	= omap3430_init_early,
	.init_machine	= omap_generic_init,
	.init_late	= omap3_init_late,
	.init_time	= omap_init_time_of,
	.dt_compat	= n900_boards_compat,
	.restart	= omap3xxx_restart,
MACHINE_END

/* Generic omap3 boards, most boards can use these */
static const char *const omap3_boards_compat[] __initconst = {
	"ti,omap3430",
	"ti,omap3",
	NULL,
};

DT_MACHINE_START(OMAP3_DT, "Generic OMAP3 (Flattened Device Tree)")
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= omap3430_init_early,
	.init_machine	= omap_generic_init,
	.init_late	= omap3_init_late,
	.init_time	= omap_init_time_of,
	.dt_compat	= omap3_boards_compat,
	.restart	= omap3xxx_restart,
MACHINE_END

static const char *const omap36xx_boards_compat[] __initconst = {
	"ti,omap3630",
	"ti,omap36xx",
	NULL,
};

DT_MACHINE_START(OMAP36XX_DT, "Generic OMAP36xx (Flattened Device Tree)")
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= omap3630_init_early,
	.init_machine	= omap_generic_init,
	.init_late	= omap3_init_late,
	.init_time	= omap_init_time_of,
	.dt_compat	= omap36xx_boards_compat,
	.restart	= omap3xxx_restart,
MACHINE_END

static const char *const omap3_gp_boards_compat[] __initconst = {
	"ti,omap3-beagle",
	"timll,omap3-devkit8000",
	NULL,
};

DT_MACHINE_START(OMAP3_GP_DT, "Generic OMAP3-GP (Flattened Device Tree)")
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= omap3430_init_early,
	.init_machine	= omap_generic_init,
	.init_late	= omap3_init_late,
	.init_time	= omap_init_time_of,
	.dt_compat	= omap3_gp_boards_compat,
	.restart	= omap3xxx_restart,
MACHINE_END

static const char *const am3517_boards_compat[] __initconst = {
	"ti,am3517",
	NULL,
};

DT_MACHINE_START(AM3517_DT, "Generic AM3517 (Flattened Device Tree)")
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= am35xx_init_early,
	.init_machine	= omap_generic_init,
	.init_late	= omap3_init_late,
	.init_time	= omap_init_time_of,
	.dt_compat	= am3517_boards_compat,
	.restart	= omap3xxx_restart,
MACHINE_END
#endif

#ifdef CONFIG_SOC_TI81XX
static const char *const ti814x_boards_compat[] __initconst = {
	"ti,dm8148",
	"ti,dm814",
	NULL,
};

DT_MACHINE_START(TI814X_DT, "Generic ti814x (Flattened Device Tree)")
	.reserve	= omap_reserve,
	.map_io		= ti81xx_map_io,
	.init_early	= ti814x_init_early,
	.init_machine	= omap_generic_init,
	.init_late	= ti81xx_init_late,
	.init_time	= omap_init_time_of,
	.dt_compat	= ti814x_boards_compat,
	.restart	= ti81xx_restart,
MACHINE_END

static const char *const ti816x_boards_compat[] __initconst = {
	"ti,dm8168",
	"ti,dm816",
	NULL,
};

DT_MACHINE_START(TI816X_DT, "Generic ti816x (Flattened Device Tree)")
	.reserve	= omap_reserve,
	.map_io		= ti81xx_map_io,
	.init_early	= ti816x_init_early,
	.init_machine	= omap_generic_init,
	.init_late	= ti81xx_init_late,
	.init_time	= omap_init_time_of,
	.dt_compat	= ti816x_boards_compat,
	.restart	= ti81xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_SOC_AM33XX
static const char *const am33xx_boards_compat[] __initconst = {
	"ti,am33xx",
	NULL,
};

DT_MACHINE_START(AM33XX_DT, "Generic AM33XX (Flattened Device Tree)")
	.reserve	= omap_reserve,
	.map_io		= am33xx_map_io,
	.init_early	= am33xx_init_early,
	.init_machine	= omap_generic_init,
	.init_late	= am33xx_init_late,
	.init_time	= omap_init_time_of,
	.dt_compat	= am33xx_boards_compat,
	.restart	= am33xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_ARCH_OMAP4
static const char *const omap4_boards_compat[] __initconst = {
	"ti,omap4460",
	"ti,omap4430",
	"ti,omap4",
	NULL,
};

DT_MACHINE_START(OMAP4_DT, "Generic OMAP4 (Flattened Device Tree)")
	.l2c_aux_val	= OMAP_L2C_AUX_CTRL,
	.l2c_aux_mask	= 0xcf9fffff,
	.l2c_write_sec	= omap4_l2c310_write_sec,
	.reserve	= omap_reserve,
	.smp		= smp_ops(omap4_smp_ops),
	.map_io		= omap4_map_io,
	.init_early	= omap4430_init_early,
	.init_irq	= omap_gic_of_init,
	.init_machine	= omap_generic_init,
	.init_late	= omap4430_init_late,
	.init_time	= omap_init_time_of,
	.dt_compat	= omap4_boards_compat,
	.restart	= omap44xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_SOC_OMAP5
static const char *const omap5_boards_compat[] __initconst = {
	"ti,omap5432",
	"ti,omap5430",
	"ti,omap5",
	NULL,
};

DT_MACHINE_START(OMAP5_DT, "Generic OMAP5 (Flattened Device Tree)")
#if defined(CONFIG_ZONE_DMA) && defined(CONFIG_ARM_LPAE)
	.dma_zone_size	= SZ_2G,
#endif
	.reserve	= omap_reserve,
	.smp		= smp_ops(omap4_smp_ops),
	.map_io		= omap5_map_io,
	.init_early	= omap5_init_early,
	.init_irq	= omap_gic_of_init,
	.init_machine	= omap_generic_init,
	.init_late	= omap5_init_late,
	.init_time	= omap5_realtime_timer_init,
	.dt_compat	= omap5_boards_compat,
	.restart	= omap44xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_SOC_AM43XX
static const char *const am43_boards_compat[] __initconst = {
	"ti,am4372",
	"ti,am43",
	NULL,
};

DT_MACHINE_START(AM43_DT, "Generic AM43 (Flattened Device Tree)")
	.l2c_aux_val	= OMAP_L2C_AUX_CTRL,
	.l2c_aux_mask	= 0xcf9fffff,
	.l2c_write_sec	= omap4_l2c310_write_sec,
	.map_io		= am33xx_map_io,
	.init_early	= am43xx_init_early,
	.init_late	= am43xx_init_late,
	.init_irq	= omap_gic_of_init,
	.init_machine	= omap_generic_init,
	.init_time	= omap_init_time_of,
	.dt_compat	= am43_boards_compat,
	.restart	= omap44xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_SOC_DRA7XX
static const char *const dra74x_boards_compat[] __initconst = {
	"ti,dra762",
	"ti,am5728",
	"ti,am5726",
	"ti,dra742",
	"ti,dra7",
	NULL,
};

DT_MACHINE_START(DRA74X_DT, "Generic DRA74X (Flattened Device Tree)")
#if defined(CONFIG_ZONE_DMA) && defined(CONFIG_ARM_LPAE)
	.dma_zone_size	= SZ_2G,
#endif
	.reserve	= omap_reserve,
	.smp		= smp_ops(omap4_smp_ops),
	.map_io		= dra7xx_map_io,
	.init_early	= dra7xx_init_early,
	.init_late	= dra7xx_init_late,
	.init_irq	= omap_gic_of_init,
	.init_machine	= omap_generic_init,
	.init_time	= omap5_realtime_timer_init,
	.dt_compat	= dra74x_boards_compat,
	.restart	= omap44xx_restart,
MACHINE_END

static const char *const dra72x_boards_compat[] __initconst = {
	"ti,am5718",
	"ti,am5716",
	"ti,dra722",
	"ti,dra718",
	NULL,
};

DT_MACHINE_START(DRA72X_DT, "Generic DRA72X (Flattened Device Tree)")
#if defined(CONFIG_ZONE_DMA) && defined(CONFIG_ARM_LPAE)
	.dma_zone_size	= SZ_2G,
#endif
	.reserve	= omap_reserve,
	.map_io		= dra7xx_map_io,
	.init_early	= dra7xx_init_early,
	.init_late	= dra7xx_init_late,
	.init_irq	= omap_gic_of_init,
	.init_machine	= omap_generic_init,
	.init_time	= omap5_realtime_timer_init,
	.dt_compat	= dra72x_boards_compat,
	.restart	= omap44xx_restart,
MACHINE_END
#endif
