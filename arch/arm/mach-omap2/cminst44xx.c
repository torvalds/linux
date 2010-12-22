/*
 * OMAP4 CM instance functions
 *
 * Copyright (C) 2009 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This is needed since CM instances can be in the PRM, PRCM_MPU, CM1,
 * or CM2 hardware modules.  For example, the EMU_CM CM instance is in
 * the PRM hardware module.  What a mess...
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>

#include <plat/common.h>

#include "cm.h"
#include "cm1_44xx.h"
#include "cm2_44xx.h"
#include "cm44xx.h"
#include "cminst44xx.h"
#include "cm-regbits-44xx.h"
#include "prcm44xx.h"
#include "prm44xx.h"
#include "prcm_mpu44xx.h"

static u32 _cm_bases[OMAP4_MAX_PRCM_PARTITIONS] = {
	[OMAP4430_INVALID_PRCM_PARTITION]	= 0,
	[OMAP4430_PRM_PARTITION]		= OMAP4430_PRM_BASE,
	[OMAP4430_CM1_PARTITION]		= OMAP4430_CM1_BASE,
	[OMAP4430_CM2_PARTITION]		= OMAP4430_CM2_BASE,
	[OMAP4430_SCRM_PARTITION]		= 0,
	[OMAP4430_PRCM_MPU_PARTITION]		= OMAP4430_PRCM_MPU_BASE,
};

/* Read a register in a CM instance */
u32 omap4_cminst_read_inst_reg(u8 part, s16 inst, u16 idx)
{
	BUG_ON(part >= OMAP4_MAX_PRCM_PARTITIONS ||
	       part == OMAP4430_INVALID_PRCM_PARTITION ||
	       !_cm_bases[part]);
	return __raw_readl(OMAP2_L4_IO_ADDRESS(_cm_bases[part] + inst + idx));
}

/* Write into a register in a CM instance */
void omap4_cminst_write_inst_reg(u32 val, u8 part, s16 inst, u16 idx)
{
	BUG_ON(part >= OMAP4_MAX_PRCM_PARTITIONS ||
	       part == OMAP4430_INVALID_PRCM_PARTITION ||
	       !_cm_bases[part]);
	__raw_writel(val, OMAP2_L4_IO_ADDRESS(_cm_bases[part] + inst + idx));
}

/* Read-modify-write a register in CM1. Caller must lock */
u32 omap4_cminst_rmw_inst_reg_bits(u32 mask, u32 bits, u8 part, s16 inst,
				   s16 idx)
{
	u32 v;

	v = omap4_cminst_read_inst_reg(part, inst, idx);
	v &= ~mask;
	v |= bits;
	omap4_cminst_write_inst_reg(v, part, inst, idx);

	return v;
}


/**
 * omap4_cm_wait_module_ready - wait for a module to be in 'func' state
 * @clkctrl_reg: CLKCTRL module address
 *
 * Wait for the module IDLEST to be functional. If the idle state is in any
 * the non functional state (trans, idle or disabled), module and thus the
 * sysconfig cannot be accessed and will probably lead to an "imprecise
 * external abort"
 *
 * Module idle state:
 *   0x0 func:     Module is fully functional, including OCP
 *   0x1 trans:    Module is performing transition: wakeup, or sleep, or sleep
 *                 abortion
 *   0x2 idle:     Module is in Idle mode (only OCP part). It is functional if
 *                 using separate functional clock
 *   0x3 disabled: Module is disabled and cannot be accessed
 *
 */
int omap4_cm_wait_module_ready(void __iomem *clkctrl_reg)
{
	int i = 0;

	if (!clkctrl_reg)
		return 0;

	omap_test_timeout((
		((__raw_readl(clkctrl_reg) & OMAP4430_IDLEST_MASK) == 0) ||
		 (((__raw_readl(clkctrl_reg) & OMAP4430_IDLEST_MASK) >>
		  OMAP4430_IDLEST_SHIFT) == 0x2)),
		MAX_MODULE_READY_TIME, i);

	return (i < MAX_MODULE_READY_TIME) ? 0 : -EBUSY;
}

