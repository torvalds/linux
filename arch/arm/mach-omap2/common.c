/*
 * linux/arch/arm/mach-omap2/common.c
 *
 * Code common to all OMAP2+ machines.
 *
 * Copyright (C) 2009 Texas Instruments
 * Copyright (C) 2010 Nokia Corporation
 * Tony Lindgren <tony@atomide.com>
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <plat/common.h>
#include <plat/board.h>
#include <plat/mux.h>

#include <plat/clock.h>

#include "sdrc.h"
#include "control.h"

/* Global address base setup code */

#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)

static void __init __omap2_set_globals(struct omap_globals *omap2_globals)
{
	omap2_set_globals_tap(omap2_globals);
	omap2_set_globals_sdrc(omap2_globals);
	omap2_set_globals_control(omap2_globals);
	omap2_set_globals_prcm(omap2_globals);
}

#endif

#if defined(CONFIG_ARCH_OMAP2420)

static struct omap_globals omap242x_globals = {
	.class	= OMAP242X_CLASS,
	.tap	= OMAP2_L4_IO_ADDRESS(0x48014000),
	.sdrc	= OMAP2420_SDRC_BASE,
	.sms	= OMAP2420_SMS_BASE,
	.ctrl	= OMAP242X_CTRL_BASE,
	.prm	= OMAP2420_PRM_BASE,
	.cm	= OMAP2420_CM_BASE,
	.uart1_phys	= OMAP2_UART1_BASE,
	.uart2_phys	= OMAP2_UART2_BASE,
	.uart3_phys	= OMAP2_UART3_BASE,
};

void __init omap2_set_globals_242x(void)
{
	__omap2_set_globals(&omap242x_globals);
}
#endif

#if defined(CONFIG_ARCH_OMAP2430)

static struct omap_globals omap243x_globals = {
	.class	= OMAP243X_CLASS,
	.tap	= OMAP2_L4_IO_ADDRESS(0x4900a000),
	.sdrc	= OMAP243X_SDRC_BASE,
	.sms	= OMAP243X_SMS_BASE,
	.ctrl	= OMAP243X_CTRL_BASE,
	.prm	= OMAP2430_PRM_BASE,
	.cm	= OMAP2430_CM_BASE,
	.uart1_phys	= OMAP2_UART1_BASE,
	.uart2_phys	= OMAP2_UART2_BASE,
	.uart3_phys	= OMAP2_UART3_BASE,
};

void __init omap2_set_globals_243x(void)
{
	__omap2_set_globals(&omap243x_globals);
}
#endif

#if defined(CONFIG_ARCH_OMAP3)

static struct omap_globals omap3_globals = {
	.class	= OMAP343X_CLASS,
	.tap	= OMAP2_L4_IO_ADDRESS(0x4830A000),
	.sdrc	= OMAP343X_SDRC_BASE,
	.sms	= OMAP343X_SMS_BASE,
	.ctrl	= OMAP343X_CTRL_BASE,
	.prm	= OMAP3430_PRM_BASE,
	.cm	= OMAP3430_CM_BASE,
	.uart1_phys	= OMAP3_UART1_BASE,
	.uart2_phys	= OMAP3_UART2_BASE,
	.uart3_phys	= OMAP3_UART3_BASE,
	.uart4_phys	= OMAP3_UART4_BASE,	/* Only on 3630 */
};

void __init omap2_set_globals_3xxx(void)
{
	__omap2_set_globals(&omap3_globals);
}

void __init omap3_map_io(void)
{
	omap2_set_globals_3xxx();
	omap34xx_map_common_io();
}
#endif

#if defined(CONFIG_ARCH_OMAP4)
static struct omap_globals omap4_globals = {
	.class	= OMAP443X_CLASS,
	.tap	= OMAP2_L4_IO_ADDRESS(OMAP443X_SCM_BASE),
	.ctrl	= OMAP443X_SCM_BASE,
	.ctrl_pad	= OMAP443X_CTRL_BASE,
	.prm	= OMAP4430_PRM_BASE,
	.cm	= OMAP4430_CM_BASE,
	.cm2	= OMAP4430_CM2_BASE,
	.uart1_phys	= OMAP4_UART1_BASE,
	.uart2_phys	= OMAP4_UART2_BASE,
	.uart3_phys	= OMAP4_UART3_BASE,
	.uart4_phys	= OMAP4_UART4_BASE,
};

void __init omap2_set_globals_443x(void)
{
	omap2_set_globals_tap(&omap4_globals);
	omap2_set_globals_control(&omap4_globals);
	omap2_set_globals_prcm(&omap4_globals);
}
#endif

