/*
 * OMAP4 CM instance functions
 *
 * Copyright (C) 2009 Nokia Corporation
 * Copyright (C) 2011 Texas Instruments, Inc.
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

#include "iomap.h"
#include "common.h"
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
#include "prcm-common.h"

/*
 * CLKCTRL_IDLEST_*: possible values for the CM_*_CLKCTRL.IDLEST bitfield:
 *
 *   0x0 func:     Module is fully functional, including OCP
 *   0x1 trans:    Module is performing transition: wakeup, or sleep, or sleep
 *                 abortion
 *   0x2 idle:     Module is in Idle mode (only OCP part). It is functional if
 *                 using separate functional clock
 *   0x3 disabled: Module is disabled and cannot be accessed
 *
 */
#define CLKCTRL_IDLEST_FUNCTIONAL		0x0
#define CLKCTRL_IDLEST_INTRANSITION		0x1
#define CLKCTRL_IDLEST_INTERFACE_IDLE		0x2
#define CLKCTRL_IDLEST_DISABLED			0x3

static void __iomem *_cm_bases[OMAP4_MAX_PRCM_PARTITIONS];

/**
 * omap_cm_base_init - Populates the cm partitions
 *
 * Populates the base addresses of the _cm_bases
 * array used for read/write of cm module registers.
 */
void omap_cm_base_init(void)
{
	_cm_bases[OMAP4430_PRM_PARTITION] = prm_base;
	_cm_bases[OMAP4430_CM1_PARTITION] = cm_base;
	_cm_bases[OMAP4430_CM2_PARTITION] = cm2_base;
	_cm_bases[OMAP4430_PRCM_MPU_PARTITION] = prcm_mpu_base;
}

/* Private functions */

/**
 * _clkctrl_idlest - read a CM_*_CLKCTRL register; mask & shift IDLEST bitfield
 * @part: PRCM partition ID that the CM_CLKCTRL register exists in
 * @inst: CM instance register offset (*_INST macro)
 * @cdoffs: Clockdomain register offset (*_CDOFFS macro)
 * @clkctrl_offs: Module clock control register offset (*_CLKCTRL macro)
 *
 * Return the IDLEST bitfield of a CM_*_CLKCTRL register, shifted down to
 * bit 0.
 */
static u32 _clkctrl_idlest(u8 part, u16 inst, s16 cdoffs, u16 clkctrl_offs)
{
	u32 v = omap4_cminst_read_inst_reg(part, inst, clkctrl_offs);
	v &= OMAP4430_IDLEST_MASK;
	v >>= OMAP4430_IDLEST_SHIFT;
	return v;
}

/**
 * _is_module_ready - can module registers be accessed without causing an abort?
 * @part: PRCM partition ID that the CM_CLKCTRL register exists in
 * @inst: CM instance register offset (*_INST macro)
 * @cdoffs: Clockdomain register offset (*_CDOFFS macro)
 * @clkctrl_offs: Module clock control register offset (*_CLKCTRL macro)
 *
 * Returns true if the module's CM_*_CLKCTRL.IDLEST bitfield is either
 * *FUNCTIONAL or *INTERFACE_IDLE; false otherwise.
 */
static bool _is_module_ready(u8 part, u16 inst, s16 cdoffs, u16 clkctrl_offs)
{
	u32 v;

	v = _clkctrl_idlest(part, inst, cdoffs, clkctrl_offs);

	return (v == CLKCTRL_IDLEST_FUNCTIONAL ||
		v == CLKCTRL_IDLEST_INTERFACE_IDLE) ? true : false;
}

/* Public functions */

/* Read a register in a CM instance */
u32 omap4_cminst_read_inst_reg(u8 part, s16 inst, u16 idx)
{
	BUG_ON(part >= OMAP4_MAX_PRCM_PARTITIONS ||
	       part == OMAP4430_INVALID_PRCM_PARTITION ||
	       !_cm_bases[part]);
	return __raw_readl(_cm_bases[part] + inst + idx);
}

/* Write into a register in a CM instance */
void omap4_cminst_write_inst_reg(u32 val, u8 part, s16 inst, u16 idx)
{
	BUG_ON(part >= OMAP4_MAX_PRCM_PARTITIONS ||
	       part == OMAP4430_INVALID_PRCM_PARTITION ||
	       !_cm_bases[part]);
	__raw_writel(val, _cm_bases[part] + inst + idx);
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
 * omap4_cminst_wait_module_ready - wait for a module to be in 'func' state
 * @part: PRCM partition ID that the CM_CLKCTRL register exists in
 * @inst: CM instance register offset (*_INST macro)
 * @cdoffs: Clockdomain register offset (*_CDOFFS macro)
 * @clkctrl_offs: Module clock control register offset (*_CLKCTRL macro)
 *
 * Wait for the module IDLEST to be functional. If the idle state is in any
 * the non functional state (trans, idle or disabled), module and thus the
 * sysconfig cannot be accessed and will probably lead to an "imprecise
 * external abort"
 */
int omap4_cminst_wait_module_ready(u8 part, u16 inst, s16 cdoffs,
				   u16 clkctrl_offs)
{
	int i = 0;

	if (!clkctrl_offs)
		return 0;

	omap_test_timeout(_is_module_ready(part, inst, cdoffs, clkctrl_offs),
			  MAX_MODULE_READY_TIME, i);

	return (i < MAX_MODULE_READY_TIME) ? 0 : -EBUSY;
}

/**
 * omap4_cminst_wait_module_idle - wait for a module to be in 'disabled'
 * state
 * @part: PRCM partition ID that the CM_CLKCTRL register exists in
 * @inst: CM instance register offset (*_INST macro)
 * @cdoffs: Clockdomain register offset (*_CDOFFS macro)
 * @clkctrl_offs: Module clock control register offset (*_CLKCTRL macro)
 *
 * Wait for the module IDLEST to be disabled. Some PRCM transition,
 * like reset assertion or parent clock de-activation must wait the
 * module to be fully disabled.
 */
int omap4_cminst_wait_module_idle(u8 part, u16 inst, s16 cdoffs, u16 clkctrl_offs)
{
	int i = 0;

	if (!clkctrl_offs)
		return 0;

	omap_test_timeout((_clkctrl_idlest(part, inst, cdoffs, clkctrl_offs) ==
			   CLKCTRL_IDLEST_DISABLED),
			  MAX_MODULE_DISABLE_TIME, i);

	return (i < MAX_MODULE_DISABLE_TIME) ? 0 : -EBUSY;
}

/**
 * omap4_cminst_module_enable - Enable the modulemode inside CLKCTRL
 * @mode: Module mode (SW or HW)
 * @part: PRCM partition ID that the CM_CLKCTRL register exists in
 * @inst: CM instance register offset (*_INST macro)
 * @cdoffs: Clockdomain register offset (*_CDOFFS macro)
 * @clkctrl_offs: Module clock control register offset (*_CLKCTRL macro)
 *
 * No return value.
 */
void omap4_cminst_module_enable(u8 mode, u8 part, u16 inst, s16 cdoffs,
			    u16 clkctrl_offs)
{
	u32 v;

	v = omap4_cminst_read_inst_reg(part, inst, clkctrl_offs);
	v &= ~OMAP4430_MODULEMODE_MASK;
	v |= mode << OMAP4430_MODULEMODE_SHIFT;
	omap4_cminst_write_inst_reg(v, part, inst, clkctrl_offs);
}

/**
 * omap4_cminst_module_disable - Disable the module inside CLKCTRL
 * @part: PRCM partition ID that the CM_CLKCTRL register exists in
 * @inst: CM instance register offset (*_INST macro)
 * @cdoffs: Clockdomain register offset (*_CDOFFS macro)
 * @clkctrl_offs: Module clock control register offset (*_CLKCTRL macro)
 *
 * No return value.
 */
void omap4_cminst_module_disable(u8 part, u16 inst, s16 cdoffs,
			     u16 clkctrl_offs)
{
	u32 v;

	v = omap4_cminst_read_inst_reg(part, inst, clkctrl_offs);
	v &= ~OMAP4430_MODULEMODE_MASK;
	omap4_cminst_write_inst_reg(v, part, inst, clkctrl_offs);
}
