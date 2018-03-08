/*
 * OMAP2xxx CM module functions
 *
 * Copyright (C) 2009 Nokia Corporation
 * Copyright (C) 2008-2010, 2012 Texas Instruments, Inc.
 * Paul Walmsley
 * Rajendra Nayak <rnayak@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>

#include "prm2xxx.h"
#include "cm.h"
#include "cm2xxx.h"
#include "cm-regbits-24xx.h"
#include "clockdomain.h"

/* CM_AUTOIDLE_PLL.AUTO_* bit values for DPLLs */
#define DPLL_AUTOIDLE_DISABLE				0x0
#define OMAP2XXX_DPLL_AUTOIDLE_LOW_POWER_STOP		0x3

/* CM_AUTOIDLE_PLL.AUTO_* bit values for APLLs (OMAP2xxx only) */
#define OMAP2XXX_APLL_AUTOIDLE_DISABLE			0x0
#define OMAP2XXX_APLL_AUTOIDLE_LOW_POWER_STOP		0x3

/* CM_IDLEST_PLL bit value offset for APLLs (OMAP2xxx only) */
#define EN_APLL_LOCKED					3

static const u8 omap2xxx_cm_idlest_offs[] = {
	CM_IDLEST1, CM_IDLEST2, OMAP2430_CM_IDLEST3, OMAP24XX_CM_IDLEST4
};

/*
 *
 */

static void _write_clktrctrl(u8 c, s16 module, u32 mask)
{
	u32 v;

	v = omap2_cm_read_mod_reg(module, OMAP2_CM_CLKSTCTRL);
	v &= ~mask;
	v |= c << __ffs(mask);
	omap2_cm_write_mod_reg(v, module, OMAP2_CM_CLKSTCTRL);
}

static bool omap2xxx_cm_is_clkdm_in_hwsup(s16 module, u32 mask)
{
	u32 v;

	v = omap2_cm_read_mod_reg(module, OMAP2_CM_CLKSTCTRL);
	v &= mask;
	v >>= __ffs(mask);

	return (v == OMAP24XX_CLKSTCTRL_ENABLE_AUTO) ? 1 : 0;
}

static void omap2xxx_cm_clkdm_enable_hwsup(s16 module, u32 mask)
{
	_write_clktrctrl(OMAP24XX_CLKSTCTRL_ENABLE_AUTO, module, mask);
}

static void omap2xxx_cm_clkdm_disable_hwsup(s16 module, u32 mask)
{
	_write_clktrctrl(OMAP24XX_CLKSTCTRL_DISABLE_AUTO, module, mask);
}

/*
 * DPLL autoidle control
 */

static void _omap2xxx_set_dpll_autoidle(u8 m)
{
	u32 v;

	v = omap2_cm_read_mod_reg(PLL_MOD, CM_AUTOIDLE);
	v &= ~OMAP24XX_AUTO_DPLL_MASK;
	v |= m << OMAP24XX_AUTO_DPLL_SHIFT;
	omap2_cm_write_mod_reg(v, PLL_MOD, CM_AUTOIDLE);
}

void omap2xxx_cm_set_dpll_disable_autoidle(void)
{
	_omap2xxx_set_dpll_autoidle(OMAP2XXX_DPLL_AUTOIDLE_LOW_POWER_STOP);
}

void omap2xxx_cm_set_dpll_auto_low_power_stop(void)
{
	_omap2xxx_set_dpll_autoidle(DPLL_AUTOIDLE_DISABLE);
}

/*
 * APLL control
 */

static void _omap2xxx_set_apll_autoidle(u8 m, u32 mask)
{
	u32 v;

	v = omap2_cm_read_mod_reg(PLL_MOD, CM_AUTOIDLE);
	v &= ~mask;
	v |= m << __ffs(mask);
	omap2_cm_write_mod_reg(v, PLL_MOD, CM_AUTOIDLE);
}

void omap2xxx_cm_set_apll54_disable_autoidle(void)
{
	_omap2xxx_set_apll_autoidle(OMAP2XXX_APLL_AUTOIDLE_LOW_POWER_STOP,
				    OMAP24XX_AUTO_54M_MASK);
}

void omap2xxx_cm_set_apll54_auto_low_power_stop(void)
{
	_omap2xxx_set_apll_autoidle(OMAP2XXX_APLL_AUTOIDLE_DISABLE,
				    OMAP24XX_AUTO_54M_MASK);
}

void omap2xxx_cm_set_apll96_disable_autoidle(void)
{
	_omap2xxx_set_apll_autoidle(OMAP2XXX_APLL_AUTOIDLE_LOW_POWER_STOP,
				    OMAP24XX_AUTO_96M_MASK);
}

void omap2xxx_cm_set_apll96_auto_low_power_stop(void)
{
	_omap2xxx_set_apll_autoidle(OMAP2XXX_APLL_AUTOIDLE_DISABLE,
				    OMAP24XX_AUTO_96M_MASK);
}

/* Enable an APLL if off */
static int _omap2xxx_apll_enable(u8 enable_bit, u8 status_bit)
{
	u32 v, m;

	m = EN_APLL_LOCKED << enable_bit;

	v = omap2_cm_read_mod_reg(PLL_MOD, CM_CLKEN);
	if (v & m)
		return 0;   /* apll already enabled */

	v |= m;
	omap2_cm_write_mod_reg(v, PLL_MOD, CM_CLKEN);

	omap2xxx_cm_wait_module_ready(0, PLL_MOD, 1, status_bit);

	/*
	 * REVISIT: Should we return an error code if
	 * omap2xxx_cm_wait_module_ready() fails?
	 */
	return 0;
}

/* Stop APLL */
static void _omap2xxx_apll_disable(u8 enable_bit)
{
	u32 v;

	v = omap2_cm_read_mod_reg(PLL_MOD, CM_CLKEN);
	v &= ~(EN_APLL_LOCKED << enable_bit);
	omap2_cm_write_mod_reg(v, PLL_MOD, CM_CLKEN);
}

/* Enable an APLL if off */
int omap2xxx_cm_apll54_enable(void)
{
	return _omap2xxx_apll_enable(OMAP24XX_EN_54M_PLL_SHIFT,
				     OMAP24XX_ST_54M_APLL_SHIFT);
}

/* Enable an APLL if off */
int omap2xxx_cm_apll96_enable(void)
{
	return _omap2xxx_apll_enable(OMAP24XX_EN_96M_PLL_SHIFT,
				     OMAP24XX_ST_96M_APLL_SHIFT);
}

/* Stop APLL */
void omap2xxx_cm_apll54_disable(void)
{
	_omap2xxx_apll_disable(OMAP24XX_EN_54M_PLL_SHIFT);
}

/* Stop APLL */
void omap2xxx_cm_apll96_disable(void)
{
	_omap2xxx_apll_disable(OMAP24XX_EN_96M_PLL_SHIFT);
}

/**
 * omap2xxx_cm_split_idlest_reg - split CM_IDLEST reg addr into its components
 * @idlest_reg: CM_IDLEST* virtual address
 * @prcm_inst: pointer to an s16 to return the PRCM instance offset
 * @idlest_reg_id: pointer to a u8 to return the CM_IDLESTx register ID
 *
 * XXX This function is only needed until absolute register addresses are
 * removed from the OMAP struct clk records.
 */
static int omap2xxx_cm_split_idlest_reg(struct clk_omap_reg *idlest_reg,
					s16 *prcm_inst,
					u8 *idlest_reg_id)
{
	unsigned long offs;
	u8 idlest_offs;
	int i;

	idlest_offs = idlest_reg->offset & 0xff;
	for (i = 0; i < ARRAY_SIZE(omap2xxx_cm_idlest_offs); i++) {
		if (idlest_offs == omap2xxx_cm_idlest_offs[i]) {
			*idlest_reg_id = i + 1;
			break;
		}
	}

	if (i == ARRAY_SIZE(omap2xxx_cm_idlest_offs))
		return -EINVAL;

	offs = idlest_reg->offset;
	offs &= 0xff00;
	*prcm_inst = offs;

	return 0;
}

/*
 *
 */

/**
 * omap2xxx_cm_wait_module_ready - wait for a module to leave idle or standby
 * @part: PRCM partition, ignored for OMAP2
 * @prcm_mod: PRCM module offset
 * @idlest_id: CM_IDLESTx register ID (i.e., x = 1, 2, 3)
 * @idlest_shift: shift of the bit in the CM_IDLEST* register to check
 *
 * Wait for the PRCM to indicate that the module identified by
 * (@prcm_mod, @idlest_id, @idlest_shift) is clocked.  Return 0 upon
 * success or -EBUSY if the module doesn't enable in time.
 */
int omap2xxx_cm_wait_module_ready(u8 part, s16 prcm_mod, u16 idlest_id,
				  u8 idlest_shift)
{
	int ena = 0, i = 0;
	u8 cm_idlest_reg;
	u32 mask;

	if (!idlest_id || (idlest_id > ARRAY_SIZE(omap2xxx_cm_idlest_offs)))
		return -EINVAL;

	cm_idlest_reg = omap2xxx_cm_idlest_offs[idlest_id - 1];

	mask = 1 << idlest_shift;
	ena = mask;

	omap_test_timeout(((omap2_cm_read_mod_reg(prcm_mod, cm_idlest_reg) &
			    mask) == ena), MAX_MODULE_READY_TIME, i);

	return (i < MAX_MODULE_READY_TIME) ? 0 : -EBUSY;
}

/* Clockdomain low-level functions */

static void omap2xxx_clkdm_allow_idle(struct clockdomain *clkdm)
{
	omap2xxx_cm_clkdm_enable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
				       clkdm->clktrctrl_mask);
}

static void omap2xxx_clkdm_deny_idle(struct clockdomain *clkdm)
{
	omap2xxx_cm_clkdm_disable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
					clkdm->clktrctrl_mask);
}

static int omap2xxx_clkdm_clk_enable(struct clockdomain *clkdm)
{
	bool hwsup = false;

	if (!clkdm->clktrctrl_mask)
		return 0;

	hwsup = omap2xxx_cm_is_clkdm_in_hwsup(clkdm->pwrdm.ptr->prcm_offs,
					      clkdm->clktrctrl_mask);
	if (!hwsup && clkdm->flags & CLKDM_CAN_FORCE_WAKEUP)
		omap2xxx_clkdm_wakeup(clkdm);

	return 0;
}

static int omap2xxx_clkdm_clk_disable(struct clockdomain *clkdm)
{
	bool hwsup = false;

	if (!clkdm->clktrctrl_mask)
		return 0;

	hwsup = omap2xxx_cm_is_clkdm_in_hwsup(clkdm->pwrdm.ptr->prcm_offs,
					      clkdm->clktrctrl_mask);

	if (!hwsup && clkdm->flags & CLKDM_CAN_FORCE_SLEEP)
		omap2xxx_clkdm_sleep(clkdm);

	return 0;
}

struct clkdm_ops omap2_clkdm_operations = {
	.clkdm_add_wkdep	= omap2_clkdm_add_wkdep,
	.clkdm_del_wkdep	= omap2_clkdm_del_wkdep,
	.clkdm_read_wkdep	= omap2_clkdm_read_wkdep,
	.clkdm_clear_all_wkdeps	= omap2_clkdm_clear_all_wkdeps,
	.clkdm_sleep		= omap2xxx_clkdm_sleep,
	.clkdm_wakeup		= omap2xxx_clkdm_wakeup,
	.clkdm_allow_idle	= omap2xxx_clkdm_allow_idle,
	.clkdm_deny_idle	= omap2xxx_clkdm_deny_idle,
	.clkdm_clk_enable	= omap2xxx_clkdm_clk_enable,
	.clkdm_clk_disable	= omap2xxx_clkdm_clk_disable,
};

int omap2xxx_cm_fclks_active(void)
{
	u32 f1, f2;

	f1 = omap2_cm_read_mod_reg(CORE_MOD, CM_FCLKEN1);
	f2 = omap2_cm_read_mod_reg(CORE_MOD, OMAP24XX_CM_FCLKEN2);

	return (f1 | f2) ? 1 : 0;
}

int omap2xxx_cm_mpu_retention_allowed(void)
{
	u32 l;

	/* Check for MMC, UART2, UART1, McSPI2, McSPI1 and DSS1. */
	l = omap2_cm_read_mod_reg(CORE_MOD, CM_FCLKEN1);
	if (l & (OMAP2420_EN_MMC_MASK | OMAP24XX_EN_UART2_MASK |
		 OMAP24XX_EN_UART1_MASK | OMAP24XX_EN_MCSPI2_MASK |
		 OMAP24XX_EN_MCSPI1_MASK | OMAP24XX_EN_DSS1_MASK))
		return 0;
	/* Check for UART3. */
	l = omap2_cm_read_mod_reg(CORE_MOD, OMAP24XX_CM_FCLKEN2);
	if (l & OMAP24XX_EN_UART3_MASK)
		return 0;

	return 1;
}

u32 omap2xxx_cm_get_core_clk_src(void)
{
	u32 v;

	v = omap2_cm_read_mod_reg(PLL_MOD, CM_CLKSEL2);
	v &= OMAP24XX_CORE_CLK_SRC_MASK;

	return v;
}

u32 omap2xxx_cm_get_core_pll_config(void)
{
	return omap2_cm_read_mod_reg(PLL_MOD, CM_CLKSEL2);
}

void omap2xxx_cm_set_mod_dividers(u32 mpu, u32 dsp, u32 gfx, u32 core, u32 mdm)
{
	u32 tmp;

	omap2_cm_write_mod_reg(mpu, MPU_MOD, CM_CLKSEL);
	omap2_cm_write_mod_reg(dsp, OMAP24XX_DSP_MOD, CM_CLKSEL);
	omap2_cm_write_mod_reg(gfx, GFX_MOD, CM_CLKSEL);
	tmp = omap2_cm_read_mod_reg(CORE_MOD, CM_CLKSEL1) &
		OMAP24XX_CLKSEL_DSS2_MASK;
	omap2_cm_write_mod_reg(core | tmp, CORE_MOD, CM_CLKSEL1);
	if (mdm)
		omap2_cm_write_mod_reg(mdm, OMAP2430_MDM_MOD, CM_CLKSEL);
}

/*
 *
 */

static const struct cm_ll_data omap2xxx_cm_ll_data = {
	.split_idlest_reg	= &omap2xxx_cm_split_idlest_reg,
	.wait_module_ready	= &omap2xxx_cm_wait_module_ready,
};

int __init omap2xxx_cm_init(const struct omap_prcm_init_data *data)
{
	return cm_register(&omap2xxx_cm_ll_data);
}

static void __exit omap2xxx_cm_exit(void)
{
	cm_unregister(&omap2xxx_cm_ll_data);
}
__exitcall(omap2xxx_cm_exit);
