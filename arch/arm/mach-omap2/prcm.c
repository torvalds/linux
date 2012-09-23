/*
 * linux/arch/arm/mach-omap2/prcm.c
 *
 * OMAP 24xx Power Reset and Clock Management (PRCM) functions
 *
 * Copyright (C) 2005 Nokia Corporation
 *
 * Written by Tony Lindgren <tony.lindgren@nokia.com>
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Rajendra Nayak <rnayak@ti.com>
 *
 * Some pieces of code Copyright (C) 2005 Texas Instruments, Inc.
 * Upgraded with OMAP4 support by Abhijit Pagare <abhijitpagare@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/export.h>

#include "common.h"
#include <plat/prcm.h>
#include <plat/irqs.h>

#include "clock.h"
#include "clock2xxx.h"
#include "cm2xxx_3xxx.h"
#include "prm2xxx_3xxx.h"
#include "prm44xx.h"
#include "prminst44xx.h"
#include "cminst44xx.h"
#include "prm-regbits-24xx.h"
#include "prm-regbits-44xx.h"
#include "control.h"

void __iomem *prm_base;
void __iomem *cm_base;
void __iomem *cm2_base;
void __iomem *prcm_mpu_base;

#define MAX_MODULE_ENABLE_WAIT		100000

u32 omap_prcm_get_reset_sources(void)
{
	/* XXX This presumably needs modification for 34XX */
	if (cpu_is_omap24xx() || cpu_is_omap34xx())
		return omap2_prm_read_mod_reg(WKUP_MOD, OMAP2_RM_RSTST) & 0x7f;
	if (cpu_is_omap44xx())
		return omap2_prm_read_mod_reg(WKUP_MOD, OMAP4_RM_RSTST) & 0x7f;

	return 0;
}
EXPORT_SYMBOL(omap_prcm_get_reset_sources);

/* Resets clock rates and reboots the system. Only called from system.h */
void omap_prcm_restart(char mode, const char *cmd)
{
	s16 prcm_offs = 0;

	if (cpu_is_omap24xx()) {
		omap2xxx_clk_prepare_for_reboot();

		prcm_offs = WKUP_MOD;
	} else if (cpu_is_omap34xx()) {
		prcm_offs = OMAP3430_GR_MOD;
		omap3_ctrl_write_boot_mode((cmd ? (u8)*cmd : 0));
	} else if (cpu_is_omap44xx()) {
		omap4_prminst_global_warm_sw_reset(); /* never returns */
	} else {
		WARN_ON(1);
	}

	/*
	 * As per Errata i520, in some cases, user will not be able to
	 * access DDR memory after warm-reset.
	 * This situation occurs while the warm-reset happens during a read
	 * access to DDR memory. In that particular condition, DDR memory
	 * does not respond to a corrupted read command due to the warm
	 * reset occurrence but SDRC is waiting for read completion.
	 * SDRC is not sensitive to the warm reset, but the interconnect is
	 * reset on the fly, thus causing a misalignment between SDRC logic,
	 * interconnect logic and DDR memory state.
	 * WORKAROUND:
	 * Steps to perform before a Warm reset is trigged:
	 * 1. enable self-refresh on idle request
	 * 2. put SDRC in idle
	 * 3. wait until SDRC goes to idle
	 * 4. generate SW reset (Global SW reset)
	 *
	 * Steps to be performed after warm reset occurs (in bootloader):
	 * if HW warm reset is the source, apply below steps before any
	 * accesses to SDRAM:
	 * 1. Reset SMS and SDRC and wait till reset is complete
	 * 2. Re-initialize SMS, SDRC and memory
	 *
	 * NOTE: Above work around is required only if arch reset is implemented
	 * using Global SW reset(GLOBAL_SW_RST). DPLL3 reset does not need
	 * the WA since it resets SDRC as well as part of cold reset.
	 */

	/* XXX should be moved to some OMAP2/3 specific code */
	omap2_prm_set_mod_reg_bits(OMAP_RST_DPLL3_MASK, prcm_offs,
				   OMAP2_RM_RSTCTRL);
	omap2_prm_read_mod_reg(prcm_offs, OMAP2_RM_RSTCTRL); /* OCP barrier */
}

/**
 * omap2_cm_wait_idlest - wait for IDLEST bit to indicate module readiness
 * @reg: physical address of module IDLEST register
 * @mask: value to mask against to determine if the module is active
 * @idlest: idle state indicator (0 or 1) for the clock
 * @name: name of the clock (for printk)
 *
 * Returns 1 if the module indicated readiness in time, or 0 if it
 * failed to enable in roughly MAX_MODULE_ENABLE_WAIT microseconds.
 *
 * XXX This function is deprecated.  It should be removed once the
 * hwmod conversion is complete.
 */
int omap2_cm_wait_idlest(void __iomem *reg, u32 mask, u8 idlest,
				const char *name)
{
	int i = 0;
	int ena = 0;

	if (idlest)
		ena = 0;
	else
		ena = mask;

	/* Wait for lock */
	omap_test_timeout(((__raw_readl(reg) & mask) == ena),
			  MAX_MODULE_ENABLE_WAIT, i);

	if (i < MAX_MODULE_ENABLE_WAIT)
		pr_debug("cm: Module associated with clock %s ready after %d loops\n",
			 name, i);
	else
		pr_err("cm: Module associated with clock %s didn't enable in %d tries\n",
		       name, MAX_MODULE_ENABLE_WAIT);

	return (i < MAX_MODULE_ENABLE_WAIT) ? 1 : 0;
};

void __init omap2_set_globals_prcm(struct omap_globals *omap2_globals)
{
	if (omap2_globals->prm)
		prm_base = omap2_globals->prm;
	if (omap2_globals->cm)
		cm_base = omap2_globals->cm;
	if (omap2_globals->cm2)
		cm2_base = omap2_globals->cm2;
	if (omap2_globals->prcm_mpu)
		prcm_mpu_base = omap2_globals->prcm_mpu;

	if (cpu_is_omap44xx() || soc_is_omap54xx()) {
		omap_prm_base_init();
		omap_cm_base_init();
	}
}

/*
 * Stubbed functions so that common files continue to build when
 * custom builds are used
 * XXX These are temporary and should be removed at the earliest possible
 * opportunity
 */
int __weak omap4_cminst_wait_module_idle(u8 part, u16 inst, s16 cdoffs,
					u16 clkctrl_offs)
{
	return 0;
}

void __weak omap4_cminst_module_enable(u8 mode, u8 part, u16 inst,
				s16 cdoffs, u16 clkctrl_offs)
{
}

void __weak omap4_cminst_module_disable(u8 part, u16 inst, s16 cdoffs,
				 u16 clkctrl_offs)
{
}
