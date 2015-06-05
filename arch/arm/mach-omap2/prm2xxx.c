/*
 * OMAP2xxx PRM module functions
 *
 * Copyright (C) 2010-2012 Texas Instruments, Inc.
 * Copyright (C) 2010 Nokia Corporation
 * Benoît Cousson
 * Paul Walmsley
 * Rajendra Nayak <rnayak@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>

#include "powerdomain.h"
#include "clockdomain.h"
#include "prm2xxx.h"
#include "cm2xxx_3xxx.h"
#include "prm-regbits-24xx.h"

/*
 * OMAP24xx PM_PWSTCTRL_*.POWERSTATE and PM_PWSTST_*.LASTSTATEENTERED bits -
 * these are reversed from the bits used on OMAP3+
 */
#define OMAP24XX_PWRDM_POWER_ON			0x0
#define OMAP24XX_PWRDM_POWER_RET		0x1
#define OMAP24XX_PWRDM_POWER_OFF		0x3

/*
 * omap2xxx_prm_reset_src_map - map from bits in the PRM_RSTST_WKUP
 *   hardware register (which are specific to the OMAP2xxx SoCs) to
 *   reset source ID bit shifts (which is an OMAP SoC-independent
 *   enumeration)
 */
static struct prm_reset_src_map omap2xxx_prm_reset_src_map[] = {
	{ OMAP_GLOBALCOLD_RST_SHIFT, OMAP_GLOBAL_COLD_RST_SRC_ID_SHIFT },
	{ OMAP_GLOBALWARM_RST_SHIFT, OMAP_GLOBAL_WARM_RST_SRC_ID_SHIFT },
	{ OMAP24XX_SECU_VIOL_RST_SHIFT, OMAP_SECU_VIOL_RST_SRC_ID_SHIFT },
	{ OMAP24XX_MPU_WD_RST_SHIFT, OMAP_MPU_WD_RST_SRC_ID_SHIFT },
	{ OMAP24XX_SECU_WD_RST_SHIFT, OMAP_SECU_WD_RST_SRC_ID_SHIFT },
	{ OMAP24XX_EXTWMPU_RST_SHIFT, OMAP_EXTWARM_RST_SRC_ID_SHIFT },
	{ -1, -1 },
};

/**
 * omap2xxx_prm_read_reset_sources - return the last SoC reset source
 *
 * Return a u32 representing the last reset sources of the SoC.  The
 * returned reset source bits are standardized across OMAP SoCs.
 */
static u32 omap2xxx_prm_read_reset_sources(void)
{
	struct prm_reset_src_map *p;
	u32 r = 0;
	u32 v;

	v = omap2_prm_read_mod_reg(WKUP_MOD, OMAP2_RM_RSTST);

	p = omap2xxx_prm_reset_src_map;
	while (p->reg_shift >= 0 && p->std_shift >= 0) {
		if (v & (1 << p->reg_shift))
			r |= 1 << p->std_shift;
		p++;
	}

	return r;
}

/**
 * omap2xxx_pwrst_to_common_pwrst - convert OMAP2xxx pwrst to common pwrst
 * @omap2xxx_pwrst: OMAP2xxx hardware power state to convert
 *
 * Return the common power state bits corresponding to the OMAP2xxx
 * hardware power state bits @omap2xxx_pwrst, or -EINVAL upon error.
 */
static int omap2xxx_pwrst_to_common_pwrst(u8 omap2xxx_pwrst)
{
	u8 pwrst;

	switch (omap2xxx_pwrst) {
	case OMAP24XX_PWRDM_POWER_OFF:
		pwrst = PWRDM_POWER_OFF;
		break;
	case OMAP24XX_PWRDM_POWER_RET:
		pwrst = PWRDM_POWER_RET;
		break;
	case OMAP24XX_PWRDM_POWER_ON:
		pwrst = PWRDM_POWER_ON;
		break;
	default:
		return -EINVAL;
	}

	return pwrst;
}

/**
 * omap2xxx_prm_dpll_reset - use DPLL reset to reboot the OMAP SoC
 *
 * Set the DPLL reset bit, which should reboot the SoC.  This is the
 * recommended way to restart the SoC.  No return value.
 */
static void omap2xxx_prm_dpll_reset(void)
{
	omap2_prm_set_mod_reg_bits(OMAP_RST_DPLL3_MASK, WKUP_MOD,
				   OMAP2_RM_RSTCTRL);
	/* OCP barrier */
	omap2_prm_read_mod_reg(WKUP_MOD, OMAP2_RM_RSTCTRL);
}

/**
 * omap2xxx_prm_clear_mod_irqs - clear wakeup status bits for a module
 * @module: PRM module to clear wakeups from
 * @regs: register offset to clear
 * @wkst_mask: wakeup status mask to clear
 *
 * Clears wakeup status bits for a given module, so that the device can
 * re-enter idle.
 */
static int omap2xxx_prm_clear_mod_irqs(s16 module, u8 regs, u32 wkst_mask)
{
	u32 wkst;

	wkst = omap2_prm_read_mod_reg(module, regs);
	wkst &= wkst_mask;
	omap2_prm_write_mod_reg(wkst, module, regs);
	return 0;
}

int omap2xxx_clkdm_sleep(struct clockdomain *clkdm)
{
	omap2_prm_set_mod_reg_bits(OMAP24XX_FORCESTATE_MASK,
				   clkdm->pwrdm.ptr->prcm_offs,
				   OMAP2_PM_PWSTCTRL);
	return 0;
}

int omap2xxx_clkdm_wakeup(struct clockdomain *clkdm)
{
	omap2_prm_clear_mod_reg_bits(OMAP24XX_FORCESTATE_MASK,
				     clkdm->pwrdm.ptr->prcm_offs,
				     OMAP2_PM_PWSTCTRL);
	return 0;
}

static int omap2xxx_pwrdm_set_next_pwrst(struct powerdomain *pwrdm, u8 pwrst)
{
	u8 omap24xx_pwrst;

	switch (pwrst) {
	case PWRDM_POWER_OFF:
		omap24xx_pwrst = OMAP24XX_PWRDM_POWER_OFF;
		break;
	case PWRDM_POWER_RET:
		omap24xx_pwrst = OMAP24XX_PWRDM_POWER_RET;
		break;
	case PWRDM_POWER_ON:
		omap24xx_pwrst = OMAP24XX_PWRDM_POWER_ON;
		break;
	default:
		return -EINVAL;
	}

	omap2_prm_rmw_mod_reg_bits(OMAP_POWERSTATE_MASK,
				   (omap24xx_pwrst << OMAP_POWERSTATE_SHIFT),
				   pwrdm->prcm_offs, OMAP2_PM_PWSTCTRL);
	return 0;
}

static int omap2xxx_pwrdm_read_next_pwrst(struct powerdomain *pwrdm)
{
	u8 omap2xxx_pwrst;

	omap2xxx_pwrst = omap2_prm_read_mod_bits_shift(pwrdm->prcm_offs,
						       OMAP2_PM_PWSTCTRL,
						       OMAP_POWERSTATE_MASK);

	return omap2xxx_pwrst_to_common_pwrst(omap2xxx_pwrst);
}

static int omap2xxx_pwrdm_read_pwrst(struct powerdomain *pwrdm)
{
	u8 omap2xxx_pwrst;

	omap2xxx_pwrst = omap2_prm_read_mod_bits_shift(pwrdm->prcm_offs,
						       OMAP2_PM_PWSTST,
						       OMAP_POWERSTATEST_MASK);

	return omap2xxx_pwrst_to_common_pwrst(omap2xxx_pwrst);
}

struct pwrdm_ops omap2_pwrdm_operations = {
	.pwrdm_set_next_pwrst	= omap2xxx_pwrdm_set_next_pwrst,
	.pwrdm_read_next_pwrst	= omap2xxx_pwrdm_read_next_pwrst,
	.pwrdm_read_pwrst	= omap2xxx_pwrdm_read_pwrst,
	.pwrdm_set_logic_retst	= omap2_pwrdm_set_logic_retst,
	.pwrdm_set_mem_onst	= omap2_pwrdm_set_mem_onst,
	.pwrdm_set_mem_retst	= omap2_pwrdm_set_mem_retst,
	.pwrdm_read_mem_pwrst	= omap2_pwrdm_read_mem_pwrst,
	.pwrdm_read_mem_retst	= omap2_pwrdm_read_mem_retst,
	.pwrdm_wait_transition	= omap2_pwrdm_wait_transition,
};

/*
 *
 */

static struct prm_ll_data omap2xxx_prm_ll_data = {
	.read_reset_sources = &omap2xxx_prm_read_reset_sources,
	.assert_hardreset = &omap2_prm_assert_hardreset,
	.deassert_hardreset = &omap2_prm_deassert_hardreset,
	.is_hardreset_asserted = &omap2_prm_is_hardreset_asserted,
	.reset_system = &omap2xxx_prm_dpll_reset,
	.clear_mod_irqs = &omap2xxx_prm_clear_mod_irqs,
};

int __init omap2xxx_prm_init(const struct omap_prcm_init_data *data)
{
	return prm_register(&omap2xxx_prm_ll_data);
}

static void __exit omap2xxx_prm_exit(void)
{
	prm_unregister(&omap2xxx_prm_ll_data);
}
__exitcall(omap2xxx_prm_exit);
