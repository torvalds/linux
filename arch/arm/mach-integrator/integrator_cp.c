/*
 *  linux/arch/arm/mach-integrator/integrator_cp.c
 *
 *  Copyright (C) 2003 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */
#include <linux/kernel.h>
#include <linux/amba/mmci.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/sched_clock.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "hardware.h"
#include "cm.h"
#include "common.h"

/* Base address to the core module header */
static struct regmap *cm_map;
/* Base address to the CP controller */
static void __iomem *intcp_con_base;

#define CM_COUNTER_OFFSET 0x28

/*
 * Logical      Physical
 * f1400000	14000000	Interrupt controller
 * f1600000	16000000	UART 0
 * fca00000	ca000000	SIC
 */

static struct map_desc intcp_io_desc[] __initdata __maybe_unused = {
	{
		.virtual	= IO_ADDRESS(INTEGRATOR_IC_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_IC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_UART0_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_UART0_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}, {
		.virtual	= IO_ADDRESS(INTEGRATOR_CP_SIC_BASE),
		.pfn		= __phys_to_pfn(INTEGRATOR_CP_SIC_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE
	}
};

static void __init intcp_map_io(void)
{
	iotable_init(intcp_io_desc, ARRAY_SIZE(intcp_io_desc));
}

/*
 * It seems that the card insertion interrupt remains active after
 * we've acknowledged it.  We therefore ignore the interrupt, and
 * rely on reading it from the SIC.  This also means that we must
 * clear the latched interrupt.
 */
static unsigned int mmc_status(struct device *dev)
{
	unsigned int status = readl(__io_address(0xca000000 + 4));
	writel(8, intcp_con_base + 8);

	return status & 8;
}

static struct mmci_platform_data mmc_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= mmc_status,
	.gpio_wp	= -1,
	.gpio_cd	= -1,
};

static u64 notrace intcp_read_sched_clock(void)
{
	unsigned int val;

	/* MMIO so discard return code */
	regmap_read(cm_map, CM_COUNTER_OFFSET, &val);
	return val;
}

static void __init intcp_init_early(void)
{
	cm_map = syscon_regmap_lookup_by_compatible("arm,core-module-integrator");
	if (IS_ERR(cm_map))
		return;
	sched_clock_register(intcp_read_sched_clock, 32, 24000000);
}

static void __init intcp_init_irq_of(void)
{
	cm_init();
	irqchip_init();
}

/*
 * For the Device Tree, add in the UART, MMC and CLCD specifics as AUXDATA
 * and enforce the bus names since these are used for clock lookups.
 */
static struct of_dev_auxdata intcp_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("arm,primecell", INTEGRATOR_CP_MMC_BASE,
		"mmci", &mmc_data),
	{ /* sentinel */ },
};

static const struct of_device_id intcp_syscon_match[] = {
	{ .compatible = "arm,integrator-cp-syscon"},
	{ },
};

static void __init intcp_init_of(void)
{
	struct device_node *cpcon;

	cpcon = of_find_matching_node(NULL, intcp_syscon_match);
	if (!cpcon)
		return;

	intcp_con_base = of_iomap(cpcon, 0);
	if (!intcp_con_base)
		return;

	of_platform_default_populate(NULL, intcp_auxdata_lookup, NULL);
}

static const char * intcp_dt_board_compat[] = {
	"arm,integrator-cp",
	NULL,
};

DT_MACHINE_START(INTEGRATOR_CP_DT, "ARM Integrator/CP (Device Tree)")
	.reserve	= integrator_reserve,
	.map_io		= intcp_map_io,
	.init_early	= intcp_init_early,
	.init_irq	= intcp_init_irq_of,
	.init_machine	= intcp_init_of,
	.dt_compat      = intcp_dt_board_compat,
MACHINE_END
