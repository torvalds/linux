/*
 * AM33XX CM functions
 *
 * Copyright (C) 2011-2012 Texas Instruments Incorporated - http://www.ti.com/
 * Vaibhav Hiremath <hvaibhav@ti.com>
 *
 * Reference taken from from OMAP4 cminst44xx.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>

#include "clockdomain.h"
#include "cm.h"
#include "cm33xx.h"
#include "cm-regbits-34xx.h"
#include "cm-regbits-33xx.h"
#include "prm33xx.h"

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

/* Private functions */

/* Read a register in a CM instance */
static inline u32 am33xx_cm_read_reg(u16 inst, u16 idx)
{
	return readl_relaxed(cm_base.va + inst + idx);
}

/* Write into a register in a CM */
static inline void am33xx_cm_write_reg(u32 val, u16 inst, u16 idx)
{
	writel_relaxed(val, cm_base.va + inst + idx);
}

/* Read-modify-write a register in CM */
static inline u32 am33xx_cm_rmw_reg_bits(u32 mask, u32 bits, s16 inst, s16 idx)
{
	u32 v;

	v = am33xx_cm_read_reg(inst, idx);
	v &= ~mask;
	v |= bits;
	am33xx_cm_write_reg(v, inst, idx);

	return v;
}

/**
 * _clkctrl_idlest - read a CM_*_CLKCTRL register; mask & shift IDLEST bitfield
 * @inst: CM instance register offset (*_INST macro)
 * @clkctrl_offs: Module clock control register offset (*_CLKCTRL macro)
 *
 * Return the IDLEST bitfield of a CM_*_CLKCTRL register, shifted down to
 * bit 0.
 */
static u32 _clkctrl_idlest(u16 inst, u16 clkctrl_offs)
{
	u32 v = am33xx_cm_read_reg(inst, clkctrl_offs);
	v &= AM33XX_IDLEST_MASK;
	v >>= AM33XX_IDLEST_SHIFT;
	return v;
}

/**
 * _is_module_ready - can module registers be accessed without causing an abort?
 * @inst: CM instance register offset (*_INST macro)
 * @clkctrl_offs: Module clock control register offset (*_CLKCTRL macro)
 *
 * Returns true if the module's CM_*_CLKCTRL.IDLEST bitfield is either
 * *FUNCTIONAL or *INTERFACE_IDLE; false otherwise.
 */
static bool _is_module_ready(u16 inst, u16 clkctrl_offs)
{
	u32 v;

	v = _clkctrl_idlest(inst, clkctrl_offs);

	return (v == CLKCTRL_IDLEST_FUNCTIONAL ||
		v == CLKCTRL_IDLEST_INTERFACE_IDLE) ? true : false;
}

/**
 * _clktrctrl_write - write @c to a CM_CLKSTCTRL.CLKTRCTRL register bitfield
 * @c: CLKTRCTRL register bitfield (LSB = bit 0, i.e., unshifted)
 * @inst: CM instance register offset (*_INST macro)
 * @cdoffs: Clockdomain register offset (*_CDOFFS macro)
 *
 * @c must be the unshifted value for CLKTRCTRL - i.e., this function
 * will handle the shift itself.
 */
static void _clktrctrl_write(u8 c, u16 inst, u16 cdoffs)
{
	u32 v;

	v = am33xx_cm_read_reg(inst, cdoffs);
	v &= ~AM33XX_CLKTRCTRL_MASK;
	v |= c << AM33XX_CLKTRCTRL_SHIFT;
	am33xx_cm_write_reg(v, inst, cdoffs);
}

/* Public functions */

/**
 * am33xx_cm_is_clkdm_in_hwsup - is a clockdomain in hwsup idle mode?
 * @inst: CM instance register offset (*_INST macro)
 * @cdoffs: Clockdomain register offset (*_CDOFFS macro)
 *
 * Returns true if the clockdomain referred to by (@inst, @cdoffs)
 * is in hardware-supervised idle mode, or 0 otherwise.
 */
static bool am33xx_cm_is_clkdm_in_hwsup(u16 inst, u16 cdoffs)
{
	u32 v;

	v = am33xx_cm_read_reg(inst, cdoffs);
	v &= AM33XX_CLKTRCTRL_MASK;
	v >>= AM33XX_CLKTRCTRL_SHIFT;

	return (v == OMAP34XX_CLKSTCTRL_ENABLE_AUTO) ? true : false;
}

/**
 * am33xx_cm_clkdm_enable_hwsup - put a clockdomain in hwsup-idle mode
 * @inst: CM instance register offset (*_INST macro)
 * @cdoffs: Clockdomain register offset (*_CDOFFS macro)
 *
 * Put a clockdomain referred to by (@inst, @cdoffs) into
 * hardware-supervised idle mode.  No return value.
 */
static void am33xx_cm_clkdm_enable_hwsup(u16 inst, u16 cdoffs)
{
	_clktrctrl_write(OMAP34XX_CLKSTCTRL_ENABLE_AUTO, inst, cdoffs);
}

/**
 * am33xx_cm_clkdm_disable_hwsup - put a clockdomain in swsup-idle mode
 * @inst: CM instance register offset (*_INST macro)
 * @cdoffs: Clockdomain register offset (*_CDOFFS macro)
 *
 * Put a clockdomain referred to by (@inst, @cdoffs) into
 * software-supervised idle mode, i.e., controlled manually by the
 * Linux OMAP clockdomain code.  No return value.
 */
static void am33xx_cm_clkdm_disable_hwsup(u16 inst, u16 cdoffs)
{
	_clktrctrl_write(OMAP34XX_CLKSTCTRL_DISABLE_AUTO, inst, cdoffs);
}

/**
 * am33xx_cm_clkdm_force_sleep - try to put a clockdomain into idle
 * @inst: CM instance register offset (*_INST macro)
 * @cdoffs: Clockdomain register offset (*_CDOFFS macro)
 *
 * Put a clockdomain referred to by (@inst, @cdoffs) into idle
 * No return value.
 */
static void am33xx_cm_clkdm_force_sleep(u16 inst, u16 cdoffs)
{
	_clktrctrl_write(OMAP34XX_CLKSTCTRL_FORCE_SLEEP, inst, cdoffs);
}

/**
 * am33xx_cm_clkdm_force_wakeup - try to take a clockdomain out of idle
 * @inst: CM instance register offset (*_INST macro)
 * @cdoffs: Clockdomain register offset (*_CDOFFS macro)
 *
 * Take a clockdomain referred to by (@inst, @cdoffs) out of idle,
 * waking it up.  No return value.
 */
static void am33xx_cm_clkdm_force_wakeup(u16 inst, u16 cdoffs)
{
	_clktrctrl_write(OMAP34XX_CLKSTCTRL_FORCE_WAKEUP, inst, cdoffs);
}

/*
 *
 */

/**
 * am33xx_cm_wait_module_ready - wait for a module to be in 'func' state
 * @part: PRCM partition, ignored for AM33xx
 * @inst: CM instance register offset (*_INST macro)
 * @clkctrl_offs: Module clock control register offset (*_CLKCTRL macro)
 * @bit_shift: bit shift for the register, ignored for AM33xx
 *
 * Wait for the module IDLEST to be functional. If the idle state is in any
 * the non functional state (trans, idle or disabled), module and thus the
 * sysconfig cannot be accessed and will probably lead to an "imprecise
 * external abort"
 */
static int am33xx_cm_wait_module_ready(u8 part, s16 inst, u16 clkctrl_offs,
				       u8 bit_shift)
{
	int i = 0;

	omap_test_timeout(_is_module_ready(inst, clkctrl_offs),
			  MAX_MODULE_READY_TIME, i);

	return (i < MAX_MODULE_READY_TIME) ? 0 : -EBUSY;
}

/**
 * am33xx_cm_wait_module_idle - wait for a module to be in 'disabled'
 * state
 * @part: CM partition, ignored for AM33xx
 * @inst: CM instance register offset (*_INST macro)
 * @clkctrl_offs: Module clock control register offset (*_CLKCTRL macro)
 * @bit_shift: bit shift for the register, ignored for AM33xx
 *
 * Wait for the module IDLEST to be disabled. Some PRCM transition,
 * like reset assertion or parent clock de-activation must wait the
 * module to be fully disabled.
 */
static int am33xx_cm_wait_module_idle(u8 part, s16 inst, u16 clkctrl_offs,
				      u8 bit_shift)
{
	int i = 0;

	omap_test_timeout((_clkctrl_idlest(inst, clkctrl_offs) ==
				CLKCTRL_IDLEST_DISABLED),
				MAX_MODULE_READY_TIME, i);

	return (i < MAX_MODULE_READY_TIME) ? 0 : -EBUSY;
}

/**
 * am33xx_cm_module_enable - Enable the modulemode inside CLKCTRL
 * @mode: Module mode (SW or HW)
 * @part: CM partition, ignored for AM33xx
 * @inst: CM instance register offset (*_INST macro)
 * @clkctrl_offs: Module clock control register offset (*_CLKCTRL macro)
 *
 * No return value.
 */
static void am33xx_cm_module_enable(u8 mode, u8 part, u16 inst,
				    u16 clkctrl_offs)
{
	u32 v;

	v = am33xx_cm_read_reg(inst, clkctrl_offs);
	v &= ~AM33XX_MODULEMODE_MASK;
	v |= mode << AM33XX_MODULEMODE_SHIFT;
	am33xx_cm_write_reg(v, inst, clkctrl_offs);
}

/**
 * am33xx_cm_module_disable - Disable the module inside CLKCTRL
 * @part: CM partition, ignored for AM33xx
 * @inst: CM instance register offset (*_INST macro)
 * @clkctrl_offs: Module clock control register offset (*_CLKCTRL macro)
 *
 * No return value.
 */
static void am33xx_cm_module_disable(u8 part, u16 inst, u16 clkctrl_offs)
{
	u32 v;

	v = am33xx_cm_read_reg(inst, clkctrl_offs);
	v &= ~AM33XX_MODULEMODE_MASK;
	am33xx_cm_write_reg(v, inst, clkctrl_offs);
}

/*
 * Clockdomain low-level functions
 */

static int am33xx_clkdm_sleep(struct clockdomain *clkdm)
{
	am33xx_cm_clkdm_force_sleep(clkdm->cm_inst, clkdm->clkdm_offs);
	return 0;
}

static int am33xx_clkdm_wakeup(struct clockdomain *clkdm)
{
	am33xx_cm_clkdm_force_wakeup(clkdm->cm_inst, clkdm->clkdm_offs);
	return 0;
}

static void am33xx_clkdm_allow_idle(struct clockdomain *clkdm)
{
	am33xx_cm_clkdm_enable_hwsup(clkdm->cm_inst, clkdm->clkdm_offs);
}

static void am33xx_clkdm_deny_idle(struct clockdomain *clkdm)
{
	am33xx_cm_clkdm_disable_hwsup(clkdm->cm_inst, clkdm->clkdm_offs);
}

static int am33xx_clkdm_clk_enable(struct clockdomain *clkdm)
{
	if (clkdm->flags & CLKDM_CAN_FORCE_WAKEUP)
		return am33xx_clkdm_wakeup(clkdm);

	return 0;
}

static int am33xx_clkdm_clk_disable(struct clockdomain *clkdm)
{
	bool hwsup = false;

	hwsup = am33xx_cm_is_clkdm_in_hwsup(clkdm->cm_inst, clkdm->clkdm_offs);

	if (!hwsup && (clkdm->flags & CLKDM_CAN_FORCE_SLEEP))
		am33xx_clkdm_sleep(clkdm);

	return 0;
}

struct clkdm_ops am33xx_clkdm_operations = {
	.clkdm_sleep		= am33xx_clkdm_sleep,
	.clkdm_wakeup		= am33xx_clkdm_wakeup,
	.clkdm_allow_idle	= am33xx_clkdm_allow_idle,
	.clkdm_deny_idle	= am33xx_clkdm_deny_idle,
	.clkdm_clk_enable	= am33xx_clkdm_clk_enable,
	.clkdm_clk_disable	= am33xx_clkdm_clk_disable,
};

static struct cm_ll_data am33xx_cm_ll_data = {
	.wait_module_ready	= &am33xx_cm_wait_module_ready,
	.wait_module_idle	= &am33xx_cm_wait_module_idle,
	.module_enable		= &am33xx_cm_module_enable,
	.module_disable		= &am33xx_cm_module_disable,
};

int __init am33xx_cm_init(const struct omap_prcm_init_data *data)
{
	return cm_register(&am33xx_cm_ll_data);
}

static void __exit am33xx_cm_exit(void)
{
	cm_unregister(&am33xx_cm_ll_data);
}
__exitcall(am33xx_cm_exit);
