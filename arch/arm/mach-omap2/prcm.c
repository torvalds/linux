/*
 * linux/arch/arm/mach-omap2/prcm.c
 *
 * OMAP 24xx Power Reset and Clock Management (PRCM) functions
 *
 * Copyright (C) 2005 Nokia Corporation
 *
 * Written by Tony Lindgren <tony.lindgren@nokia.com>
 *
 * Some pieces of code Copyright (C) 2005 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>

#include <mach/common.h>
#include <mach/prcm.h>

#include "clock.h"
#include "prm.h"
#include "prm-regbits-24xx.h"

static void __iomem *prm_base;
static void __iomem *cm_base;

#define MAX_MODULE_ENABLE_WAIT		100000

u32 omap_prcm_get_reset_sources(void)
{
	/* XXX This presumably needs modification for 34XX */
	return prm_read_mod_reg(WKUP_MOD, RM_RSTST) & 0x7f;
}
EXPORT_SYMBOL(omap_prcm_get_reset_sources);

/* Resets clock rates and reboots the system. Only called from system.h */
void omap_prcm_arch_reset(char mode)
{
	s16 prcm_offs;
	omap2_clk_prepare_for_reboot();

	if (cpu_is_omap24xx())
		prcm_offs = WKUP_MOD;
	else if (cpu_is_omap34xx())
		prcm_offs = OMAP3430_GR_MOD;
	else
		WARN_ON(1);

	prm_set_mod_reg_bits(OMAP_RST_DPLL3, prcm_offs, RM_RSTCTRL);
}

static inline u32 __omap_prcm_read(void __iomem *base, s16 module, u16 reg)
{
	BUG_ON(!base);
	return __raw_readl(base + module + reg);
}

static inline void __omap_prcm_write(u32 value, void __iomem *base,
						s16 module, u16 reg)
{
	BUG_ON(!base);
	__raw_writel(value, base + module + reg);
}

/* Read a register in a PRM module */
u32 prm_read_mod_reg(s16 module, u16 idx)
{
	return __omap_prcm_read(prm_base, module, idx);
}
EXPORT_SYMBOL(prm_read_mod_reg);

/* Write into a register in a PRM module */
void prm_write_mod_reg(u32 val, s16 module, u16 idx)
{
	__omap_prcm_write(val, prm_base, module, idx);
}
EXPORT_SYMBOL(prm_write_mod_reg);

/* Read-modify-write a register in a PRM module. Caller must lock */
u32 prm_rmw_mod_reg_bits(u32 mask, u32 bits, s16 module, s16 idx)
{
	u32 v;

	v = prm_read_mod_reg(module, idx);
	v &= ~mask;
	v |= bits;
	prm_write_mod_reg(v, module, idx);

	return v;
}
EXPORT_SYMBOL(prm_rmw_mod_reg_bits);

/* Read a register in a CM module */
u32 cm_read_mod_reg(s16 module, u16 idx)
{
	return __omap_prcm_read(cm_base, module, idx);
}
EXPORT_SYMBOL(cm_read_mod_reg);

/* Write into a register in a CM module */
void cm_write_mod_reg(u32 val, s16 module, u16 idx)
{
	__omap_prcm_write(val, cm_base, module, idx);
}
EXPORT_SYMBOL(cm_write_mod_reg);

/* Read-modify-write a register in a CM module. Caller must lock */
u32 cm_rmw_mod_reg_bits(u32 mask, u32 bits, s16 module, s16 idx)
{
	u32 v;

	v = cm_read_mod_reg(module, idx);
	v &= ~mask;
	v |= bits;
	cm_write_mod_reg(v, module, idx);

	return v;
}
EXPORT_SYMBOL(cm_rmw_mod_reg_bits);

/**
 * omap2_cm_wait_idlest - wait for IDLEST bit to indicate module readiness
 * @reg: physical address of module IDLEST register
 * @mask: value to mask against to determine if the module is active
 * @name: name of the clock (for printk)
 *
 * Returns 1 if the module indicated readiness in time, or 0 if it
 * failed to enable in roughly MAX_MODULE_ENABLE_WAIT microseconds.
 */
int omap2_cm_wait_idlest(void __iomem *reg, u32 mask, const char *name)
{
	int i = 0;
	int ena = 0;

	/*
	 * 24xx uses 0 to indicate not ready, and 1 to indicate ready.
	 * 34xx reverses this, just to keep us on our toes
	 */
	if (cpu_is_omap24xx())
		ena = mask;
	else if (cpu_is_omap34xx())
		ena = 0;
	else
		BUG();

	/* Wait for lock */
	while (((__raw_readl(reg) & mask) != ena) &&
	       (i++ < MAX_MODULE_ENABLE_WAIT))
		udelay(1);

	if (i < MAX_MODULE_ENABLE_WAIT)
		pr_debug("cm: Module associated with clock %s ready after %d "
			 "loops\n", name, i);
	else
		pr_err("cm: Module associated with clock %s didn't enable in "
		       "%d tries\n", name, MAX_MODULE_ENABLE_WAIT);

	return (i < MAX_MODULE_ENABLE_WAIT) ? 1 : 0;
};

void __init omap2_set_globals_prcm(struct omap_globals *omap2_globals)
{
	prm_base = omap2_globals->prm;
	cm_base = omap2_globals->cm;
}
