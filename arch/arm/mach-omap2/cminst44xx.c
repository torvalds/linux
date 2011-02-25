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
#include "cm-regbits-34xx.h"
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

u32 omap4_cminst_set_inst_reg_bits(u32 bits, u8 part, s16 inst, s16 idx)
{
	return omap4_cminst_rmw_inst_reg_bits(bits, bits, part, inst, idx);
}

u32 omap4_cminst_clear_inst_reg_bits(u32 bits, u8 part, s16 inst, s16 idx)
{
	return omap4_cminst_rmw_inst_reg_bits(bits, 0x0, part, inst, idx);
}

u32 omap4_cminst_read_inst_reg_bits(u8 part, u16 inst, s16 idx, u32 mask)
{
	u32 v;

	v = omap4_cminst_read_inst_reg(part, inst, idx);
	v &= mask;
	v >>= __ffs(mask);

	return v;
}

/*
 *
 */

/**
 * _clktrctrl_write - write @c to a CM_CLKSTCTRL.CLKTRCTRL register bitfield
 * @c: CLKTRCTRL register bitfield (LSB = bit 0, i.e., unshifted)
 * @part: PRCM partition ID that the CM_CLKSTCTRL register exists in
 * @inst: CM instance register offset (*_INST macro)
 * @cdoffs: Clockdomain register offset (*_CDOFFS macro)
 *
 * @c must be the unshifted value for CLKTRCTRL - i.e., this function
 * will handle the shift itself.
 */
static void _clktrctrl_write(u8 c, u8 part, s16 inst, u16 cdoffs)
{
	u32 v;

	v = omap4_cminst_read_inst_reg(part, inst, cdoffs + OMAP4_CM_CLKSTCTRL);
	v &= ~OMAP4430_CLKTRCTRL_MASK;
	v |= c << OMAP4430_CLKTRCTRL_SHIFT;
	omap4_cminst_write_inst_reg(v, part, inst, cdoffs + OMAP4_CM_CLKSTCTRL);
}

/**
 * omap4_cminst_is_clkdm_in_hwsup - is a clockdomain in hwsup idle mode?
 * @part: PRCM partition ID that the CM_CLKSTCTRL register exists in
 * @inst: CM instance register offset (*_INST macro)
 * @cdoffs: Clockdomain register offset (*_CDOFFS macro)
 *
 * Returns true if the clockdomain referred to by (@part, @inst, @cdoffs)
 * is in hardware-supervised idle mode, or 0 otherwise.
 */
bool omap4_cminst_is_clkdm_in_hwsup(u8 part, s16 inst, u16 cdoffs)
{
	u32 v;

	v = omap4_cminst_read_inst_reg(part, inst, cdoffs + OMAP4_CM_CLKSTCTRL);
	v &= OMAP4430_CLKTRCTRL_MASK;
	v >>= OMAP4430_CLKTRCTRL_SHIFT;

	return (v == OMAP34XX_CLKSTCTRL_ENABLE_AUTO) ? true : false;
}

/**
 * omap4_cminst_clkdm_enable_hwsup - put a clockdomain in hwsup-idle mode
 * @part: PRCM partition ID that the clockdomain registers exist in
 * @inst: CM instance register offset (*_INST macro)
 * @cdoffs: Clockdomain register offset (*_CDOFFS macro)
 *
 * Put a clockdomain referred to by (@part, @inst, @cdoffs) into
 * hardware-supervised idle mode.  No return value.
 */
void omap4_cminst_clkdm_enable_hwsup(u8 part, s16 inst, u16 cdoffs)
{
	_clktrctrl_write(OMAP34XX_CLKSTCTRL_ENABLE_AUTO, part, inst, cdoffs);
}

/**
 * omap4_cminst_clkdm_disable_hwsup - put a clockdomain in swsup-idle mode
 * @part: PRCM partition ID that the clockdomain registers exist in
 * @inst: CM instance register offset (*_INST macro)
 * @cdoffs: Clockdomain register offset (*_CDOFFS macro)
 *
 * Put a clockdomain referred to by (@part, @inst, @cdoffs) into
 * software-supervised idle mode, i.e., controlled manually by the
 * Linux OMAP clockdomain code.  No return value.
 */
void omap4_cminst_clkdm_disable_hwsup(u8 part, s16 inst, u16 cdoffs)
{
	_clktrctrl_write(OMAP34XX_CLKSTCTRL_DISABLE_AUTO, part, inst, cdoffs);
}

/**
 * omap4_cminst_clkdm_force_sleep - try to put a clockdomain into idle
 * @part: PRCM partition ID that the clockdomain registers exist in
 * @inst: CM instance register offset (*_INST macro)
 * @cdoffs: Clockdomain register offset (*_CDOFFS macro)
 *
 * Put a clockdomain referred to by (@part, @inst, @cdoffs) into idle
 * No return value.
 */
void omap4_cminst_clkdm_force_sleep(u8 part, s16 inst, u16 cdoffs)
{
	_clktrctrl_write(OMAP34XX_CLKSTCTRL_FORCE_SLEEP, part, inst, cdoffs);
}

/**
 * omap4_cminst_clkdm_force_sleep - try to take a clockdomain out of idle
 * @part: PRCM partition ID that the clockdomain registers exist in
 * @inst: CM instance register offset (*_INST macro)
 * @cdoffs: Clockdomain register offset (*_CDOFFS macro)
 *
 * Take a clockdomain referred to by (@part, @inst, @cdoffs) out of idle,
 * waking it up.  No return value.
 */
void omap4_cminst_clkdm_force_wakeup(u8 part, s16 inst, u16 cdoffs)
{
	_clktrctrl_write(OMAP34XX_CLKSTCTRL_FORCE_WAKEUP, part, inst, cdoffs);
}

/*
 *
 */

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

