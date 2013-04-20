/*
 * OMAP3xxx CM module functions
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

#include "soc.h"
#include "iomap.h"
#include "common.h"
#include "prm2xxx_3xxx.h"
#include "cm.h"
#include "cm3xxx.h"
#include "cm-regbits-34xx.h"
#include "clockdomain.h"

static const u8 omap3xxx_cm_idlest_offs[] = {
	CM_IDLEST1, CM_IDLEST2, OMAP2430_CM_IDLEST3
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

bool omap3xxx_cm_is_clkdm_in_hwsup(s16 module, u32 mask)
{
	u32 v;

	v = omap2_cm_read_mod_reg(module, OMAP2_CM_CLKSTCTRL);
	v &= mask;
	v >>= __ffs(mask);

	return (v == OMAP34XX_CLKSTCTRL_ENABLE_AUTO) ? 1 : 0;
}

void omap3xxx_cm_clkdm_enable_hwsup(s16 module, u32 mask)
{
	_write_clktrctrl(OMAP34XX_CLKSTCTRL_ENABLE_AUTO, module, mask);
}

void omap3xxx_cm_clkdm_disable_hwsup(s16 module, u32 mask)
{
	_write_clktrctrl(OMAP34XX_CLKSTCTRL_DISABLE_AUTO, module, mask);
}

void omap3xxx_cm_clkdm_force_sleep(s16 module, u32 mask)
{
	_write_clktrctrl(OMAP34XX_CLKSTCTRL_FORCE_SLEEP, module, mask);
}

void omap3xxx_cm_clkdm_force_wakeup(s16 module, u32 mask)
{
	_write_clktrctrl(OMAP34XX_CLKSTCTRL_FORCE_WAKEUP, module, mask);
}

/*
 *
 */

/**
 * omap3xxx_cm_wait_module_ready - wait for a module to leave idle or standby
 * @prcm_mod: PRCM module offset
 * @idlest_id: CM_IDLESTx register ID (i.e., x = 1, 2, 3)
 * @idlest_shift: shift of the bit in the CM_IDLEST* register to check
 *
 * Wait for the PRCM to indicate that the module identified by
 * (@prcm_mod, @idlest_id, @idlest_shift) is clocked.  Return 0 upon
 * success or -EBUSY if the module doesn't enable in time.
 */
int omap3xxx_cm_wait_module_ready(s16 prcm_mod, u8 idlest_id, u8 idlest_shift)
{
	int ena = 0, i = 0;
	u8 cm_idlest_reg;
	u32 mask;

	if (!idlest_id || (idlest_id > ARRAY_SIZE(omap3xxx_cm_idlest_offs)))
		return -EINVAL;

	cm_idlest_reg = omap3xxx_cm_idlest_offs[idlest_id - 1];

	mask = 1 << idlest_shift;
	ena = 0;

	omap_test_timeout(((omap2_cm_read_mod_reg(prcm_mod, cm_idlest_reg) &
			    mask) == ena), MAX_MODULE_READY_TIME, i);

	return (i < MAX_MODULE_READY_TIME) ? 0 : -EBUSY;
}

/**
 * omap3xxx_cm_split_idlest_reg - split CM_IDLEST reg addr into its components
 * @idlest_reg: CM_IDLEST* virtual address
 * @prcm_inst: pointer to an s16 to return the PRCM instance offset
 * @idlest_reg_id: pointer to a u8 to return the CM_IDLESTx register ID
 *
 * XXX This function is only needed until absolute register addresses are
 * removed from the OMAP struct clk records.
 */
int omap3xxx_cm_split_idlest_reg(void __iomem *idlest_reg, s16 *prcm_inst,
				 u8 *idlest_reg_id)
{
	unsigned long offs;
	u8 idlest_offs;
	int i;

	if (idlest_reg < (cm_base + OMAP3430_IVA2_MOD) ||
	    idlest_reg > (cm_base + 0x1ffff))
		return -EINVAL;

	idlest_offs = (unsigned long)idlest_reg & 0xff;
	for (i = 0; i < ARRAY_SIZE(omap3xxx_cm_idlest_offs); i++) {
		if (idlest_offs == omap3xxx_cm_idlest_offs[i]) {
			*idlest_reg_id = i + 1;
			break;
		}
	}

	if (i == ARRAY_SIZE(omap3xxx_cm_idlest_offs))
		return -EINVAL;

	offs = idlest_reg - cm_base;
	offs &= 0xff00;
	*prcm_inst = offs;

	return 0;
}

/* Clockdomain low-level operations */

static int omap3xxx_clkdm_add_sleepdep(struct clockdomain *clkdm1,
				       struct clockdomain *clkdm2)
{
	omap2_cm_set_mod_reg_bits((1 << clkdm2->dep_bit),
				  clkdm1->pwrdm.ptr->prcm_offs,
				  OMAP3430_CM_SLEEPDEP);
	return 0;
}

static int omap3xxx_clkdm_del_sleepdep(struct clockdomain *clkdm1,
				       struct clockdomain *clkdm2)
{
	omap2_cm_clear_mod_reg_bits((1 << clkdm2->dep_bit),
				    clkdm1->pwrdm.ptr->prcm_offs,
				    OMAP3430_CM_SLEEPDEP);
	return 0;
}

static int omap3xxx_clkdm_read_sleepdep(struct clockdomain *clkdm1,
					struct clockdomain *clkdm2)
{
	return omap2_cm_read_mod_bits_shift(clkdm1->pwrdm.ptr->prcm_offs,
					    OMAP3430_CM_SLEEPDEP,
					    (1 << clkdm2->dep_bit));
}

static int omap3xxx_clkdm_clear_all_sleepdeps(struct clockdomain *clkdm)
{
	struct clkdm_dep *cd;
	u32 mask = 0;

	for (cd = clkdm->sleepdep_srcs; cd && cd->clkdm_name; cd++) {
		if (!cd->clkdm)
			continue; /* only happens if data is erroneous */

		mask |= 1 << cd->clkdm->dep_bit;
		cd->sleepdep_usecount = 0;
	}
	omap2_cm_clear_mod_reg_bits(mask, clkdm->pwrdm.ptr->prcm_offs,
				    OMAP3430_CM_SLEEPDEP);
	return 0;
}

static int omap3xxx_clkdm_sleep(struct clockdomain *clkdm)
{
	omap3xxx_cm_clkdm_force_sleep(clkdm->pwrdm.ptr->prcm_offs,
				      clkdm->clktrctrl_mask);
	return 0;
}

static int omap3xxx_clkdm_wakeup(struct clockdomain *clkdm)
{
	omap3xxx_cm_clkdm_force_wakeup(clkdm->pwrdm.ptr->prcm_offs,
				       clkdm->clktrctrl_mask);
	return 0;
}

static void omap3xxx_clkdm_allow_idle(struct clockdomain *clkdm)
{
	if (clkdm->usecount > 0)
		clkdm_add_autodeps(clkdm);

	omap3xxx_cm_clkdm_enable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
				       clkdm->clktrctrl_mask);
}

static void omap3xxx_clkdm_deny_idle(struct clockdomain *clkdm)
{
	omap3xxx_cm_clkdm_disable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
					clkdm->clktrctrl_mask);

	if (clkdm->usecount > 0)
		clkdm_del_autodeps(clkdm);
}

static int omap3xxx_clkdm_clk_enable(struct clockdomain *clkdm)
{
	bool hwsup = false;

	if (!clkdm->clktrctrl_mask)
		return 0;

	/*
	 * The CLKDM_MISSING_IDLE_REPORTING flag documentation has
	 * more details on the unpleasant problem this is working
	 * around
	 */
	if ((clkdm->flags & CLKDM_MISSING_IDLE_REPORTING) &&
	    (clkdm->flags & CLKDM_CAN_FORCE_WAKEUP)) {
		omap3xxx_clkdm_wakeup(clkdm);
		return 0;
	}

	hwsup = omap3xxx_cm_is_clkdm_in_hwsup(clkdm->pwrdm.ptr->prcm_offs,
					      clkdm->clktrctrl_mask);

	if (hwsup) {
		/* Disable HW transitions when we are changing deps */
		omap3xxx_cm_clkdm_disable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
						clkdm->clktrctrl_mask);
		clkdm_add_autodeps(clkdm);
		omap3xxx_cm_clkdm_enable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
					       clkdm->clktrctrl_mask);
	} else {
		if (clkdm->flags & CLKDM_CAN_FORCE_WAKEUP)
			omap3xxx_clkdm_wakeup(clkdm);
	}

	return 0;
}

static int omap3xxx_clkdm_clk_disable(struct clockdomain *clkdm)
{
	bool hwsup = false;

	if (!clkdm->clktrctrl_mask)
		return 0;

	/*
	 * The CLKDM_MISSING_IDLE_REPORTING flag documentation has
	 * more details on the unpleasant problem this is working
	 * around
	 */
	if (clkdm->flags & CLKDM_MISSING_IDLE_REPORTING &&
	    !(clkdm->flags & CLKDM_CAN_FORCE_SLEEP)) {
		omap3xxx_cm_clkdm_enable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
					       clkdm->clktrctrl_mask);
		return 0;
	}

	hwsup = omap3xxx_cm_is_clkdm_in_hwsup(clkdm->pwrdm.ptr->prcm_offs,
					      clkdm->clktrctrl_mask);

	if (hwsup) {
		/* Disable HW transitions when we are changing deps */
		omap3xxx_cm_clkdm_disable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
						clkdm->clktrctrl_mask);
		clkdm_del_autodeps(clkdm);
		omap3xxx_cm_clkdm_enable_hwsup(clkdm->pwrdm.ptr->prcm_offs,
					       clkdm->clktrctrl_mask);
	} else {
		if (clkdm->flags & CLKDM_CAN_FORCE_SLEEP)
			omap3xxx_clkdm_sleep(clkdm);
	}

	return 0;
}

struct clkdm_ops omap3_clkdm_operations = {
	.clkdm_add_wkdep	= omap2_clkdm_add_wkdep,
	.clkdm_del_wkdep	= omap2_clkdm_del_wkdep,
	.clkdm_read_wkdep	= omap2_clkdm_read_wkdep,
	.clkdm_clear_all_wkdeps	= omap2_clkdm_clear_all_wkdeps,
	.clkdm_add_sleepdep	= omap3xxx_clkdm_add_sleepdep,
	.clkdm_del_sleepdep	= omap3xxx_clkdm_del_sleepdep,
	.clkdm_read_sleepdep	= omap3xxx_clkdm_read_sleepdep,
	.clkdm_clear_all_sleepdeps	= omap3xxx_clkdm_clear_all_sleepdeps,
	.clkdm_sleep		= omap3xxx_clkdm_sleep,
	.clkdm_wakeup		= omap3xxx_clkdm_wakeup,
	.clkdm_allow_idle	= omap3xxx_clkdm_allow_idle,
	.clkdm_deny_idle	= omap3xxx_clkdm_deny_idle,
	.clkdm_clk_enable	= omap3xxx_clkdm_clk_enable,
	.clkdm_clk_disable	= omap3xxx_clkdm_clk_disable,
};

/*
 * Context save/restore code - OMAP3 only
 */
struct omap3_cm_regs {
	u32 iva2_cm_clksel1;
	u32 iva2_cm_clksel2;
	u32 cm_sysconfig;
	u32 sgx_cm_clksel;
	u32 dss_cm_clksel;
	u32 cam_cm_clksel;
	u32 per_cm_clksel;
	u32 emu_cm_clksel;
	u32 emu_cm_clkstctrl;
	u32 pll_cm_autoidle;
	u32 pll_cm_autoidle2;
	u32 pll_cm_clksel4;
	u32 pll_cm_clksel5;
	u32 pll_cm_clken2;
	u32 cm_polctrl;
	u32 iva2_cm_fclken;
	u32 iva2_cm_clken_pll;
	u32 core_cm_fclken1;
	u32 core_cm_fclken3;
	u32 sgx_cm_fclken;
	u32 wkup_cm_fclken;
	u32 dss_cm_fclken;
	u32 cam_cm_fclken;
	u32 per_cm_fclken;
	u32 usbhost_cm_fclken;
	u32 core_cm_iclken1;
	u32 core_cm_iclken2;
	u32 core_cm_iclken3;
	u32 sgx_cm_iclken;
	u32 wkup_cm_iclken;
	u32 dss_cm_iclken;
	u32 cam_cm_iclken;
	u32 per_cm_iclken;
	u32 usbhost_cm_iclken;
	u32 iva2_cm_autoidle2;
	u32 mpu_cm_autoidle2;
	u32 iva2_cm_clkstctrl;
	u32 mpu_cm_clkstctrl;
	u32 core_cm_clkstctrl;
	u32 sgx_cm_clkstctrl;
	u32 dss_cm_clkstctrl;
	u32 cam_cm_clkstctrl;
	u32 per_cm_clkstctrl;
	u32 neon_cm_clkstctrl;
	u32 usbhost_cm_clkstctrl;
	u32 core_cm_autoidle1;
	u32 core_cm_autoidle2;
	u32 core_cm_autoidle3;
	u32 wkup_cm_autoidle;
	u32 dss_cm_autoidle;
	u32 cam_cm_autoidle;
	u32 per_cm_autoidle;
	u32 usbhost_cm_autoidle;
	u32 sgx_cm_sleepdep;
	u32 dss_cm_sleepdep;
	u32 cam_cm_sleepdep;
	u32 per_cm_sleepdep;
	u32 usbhost_cm_sleepdep;
	u32 cm_clkout_ctrl;
};

static struct omap3_cm_regs cm_context;

void omap3_cm_save_context(void)
{
	cm_context.iva2_cm_clksel1 =
		omap2_cm_read_mod_reg(OMAP3430_IVA2_MOD, CM_CLKSEL1);
	cm_context.iva2_cm_clksel2 =
		omap2_cm_read_mod_reg(OMAP3430_IVA2_MOD, CM_CLKSEL2);
	cm_context.cm_sysconfig = __raw_readl(OMAP3430_CM_SYSCONFIG);
	cm_context.sgx_cm_clksel =
		omap2_cm_read_mod_reg(OMAP3430ES2_SGX_MOD, CM_CLKSEL);
	cm_context.dss_cm_clksel =
		omap2_cm_read_mod_reg(OMAP3430_DSS_MOD, CM_CLKSEL);
	cm_context.cam_cm_clksel =
		omap2_cm_read_mod_reg(OMAP3430_CAM_MOD, CM_CLKSEL);
	cm_context.per_cm_clksel =
		omap2_cm_read_mod_reg(OMAP3430_PER_MOD, CM_CLKSEL);
	cm_context.emu_cm_clksel =
		omap2_cm_read_mod_reg(OMAP3430_EMU_MOD, CM_CLKSEL1);
	cm_context.emu_cm_clkstctrl =
		omap2_cm_read_mod_reg(OMAP3430_EMU_MOD, OMAP2_CM_CLKSTCTRL);
	/*
	 * As per erratum i671, ROM code does not respect the PER DPLL
	 * programming scheme if CM_AUTOIDLE_PLL.AUTO_PERIPH_DPLL == 1.
	 * In this case, even though this register has been saved in
	 * scratchpad contents, we need to restore AUTO_PERIPH_DPLL
	 * by ourselves. So, we need to save it anyway.
	 */
	cm_context.pll_cm_autoidle =
		omap2_cm_read_mod_reg(PLL_MOD, CM_AUTOIDLE);
	cm_context.pll_cm_autoidle2 =
		omap2_cm_read_mod_reg(PLL_MOD, CM_AUTOIDLE2);
	cm_context.pll_cm_clksel4 =
		omap2_cm_read_mod_reg(PLL_MOD, OMAP3430ES2_CM_CLKSEL4);
	cm_context.pll_cm_clksel5 =
		omap2_cm_read_mod_reg(PLL_MOD, OMAP3430ES2_CM_CLKSEL5);
	cm_context.pll_cm_clken2 =
		omap2_cm_read_mod_reg(PLL_MOD, OMAP3430ES2_CM_CLKEN2);
	cm_context.cm_polctrl = __raw_readl(OMAP3430_CM_POLCTRL);
	cm_context.iva2_cm_fclken =
		omap2_cm_read_mod_reg(OMAP3430_IVA2_MOD, CM_FCLKEN);
	cm_context.iva2_cm_clken_pll =
		omap2_cm_read_mod_reg(OMAP3430_IVA2_MOD, OMAP3430_CM_CLKEN_PLL);
	cm_context.core_cm_fclken1 =
		omap2_cm_read_mod_reg(CORE_MOD, CM_FCLKEN1);
	cm_context.core_cm_fclken3 =
		omap2_cm_read_mod_reg(CORE_MOD, OMAP3430ES2_CM_FCLKEN3);
	cm_context.sgx_cm_fclken =
		omap2_cm_read_mod_reg(OMAP3430ES2_SGX_MOD, CM_FCLKEN);
	cm_context.wkup_cm_fclken =
		omap2_cm_read_mod_reg(WKUP_MOD, CM_FCLKEN);
	cm_context.dss_cm_fclken =
		omap2_cm_read_mod_reg(OMAP3430_DSS_MOD, CM_FCLKEN);
	cm_context.cam_cm_fclken =
		omap2_cm_read_mod_reg(OMAP3430_CAM_MOD, CM_FCLKEN);
	cm_context.per_cm_fclken =
		omap2_cm_read_mod_reg(OMAP3430_PER_MOD, CM_FCLKEN);
	cm_context.usbhost_cm_fclken =
		omap2_cm_read_mod_reg(OMAP3430ES2_USBHOST_MOD, CM_FCLKEN);
	cm_context.core_cm_iclken1 =
		omap2_cm_read_mod_reg(CORE_MOD, CM_ICLKEN1);
	cm_context.core_cm_iclken2 =
		omap2_cm_read_mod_reg(CORE_MOD, CM_ICLKEN2);
	cm_context.core_cm_iclken3 =
		omap2_cm_read_mod_reg(CORE_MOD, CM_ICLKEN3);
	cm_context.sgx_cm_iclken =
		omap2_cm_read_mod_reg(OMAP3430ES2_SGX_MOD, CM_ICLKEN);
	cm_context.wkup_cm_iclken =
		omap2_cm_read_mod_reg(WKUP_MOD, CM_ICLKEN);
	cm_context.dss_cm_iclken =
		omap2_cm_read_mod_reg(OMAP3430_DSS_MOD, CM_ICLKEN);
	cm_context.cam_cm_iclken =
		omap2_cm_read_mod_reg(OMAP3430_CAM_MOD, CM_ICLKEN);
	cm_context.per_cm_iclken =
		omap2_cm_read_mod_reg(OMAP3430_PER_MOD, CM_ICLKEN);
	cm_context.usbhost_cm_iclken =
		omap2_cm_read_mod_reg(OMAP3430ES2_USBHOST_MOD, CM_ICLKEN);
	cm_context.iva2_cm_autoidle2 =
		omap2_cm_read_mod_reg(OMAP3430_IVA2_MOD, CM_AUTOIDLE2);
	cm_context.mpu_cm_autoidle2 =
		omap2_cm_read_mod_reg(MPU_MOD, CM_AUTOIDLE2);
	cm_context.iva2_cm_clkstctrl =
		omap2_cm_read_mod_reg(OMAP3430_IVA2_MOD, OMAP2_CM_CLKSTCTRL);
	cm_context.mpu_cm_clkstctrl =
		omap2_cm_read_mod_reg(MPU_MOD, OMAP2_CM_CLKSTCTRL);
	cm_context.core_cm_clkstctrl =
		omap2_cm_read_mod_reg(CORE_MOD, OMAP2_CM_CLKSTCTRL);
	cm_context.sgx_cm_clkstctrl =
		omap2_cm_read_mod_reg(OMAP3430ES2_SGX_MOD, OMAP2_CM_CLKSTCTRL);
	cm_context.dss_cm_clkstctrl =
		omap2_cm_read_mod_reg(OMAP3430_DSS_MOD, OMAP2_CM_CLKSTCTRL);
	cm_context.cam_cm_clkstctrl =
		omap2_cm_read_mod_reg(OMAP3430_CAM_MOD, OMAP2_CM_CLKSTCTRL);
	cm_context.per_cm_clkstctrl =
		omap2_cm_read_mod_reg(OMAP3430_PER_MOD, OMAP2_CM_CLKSTCTRL);
	cm_context.neon_cm_clkstctrl =
		omap2_cm_read_mod_reg(OMAP3430_NEON_MOD, OMAP2_CM_CLKSTCTRL);
	cm_context.usbhost_cm_clkstctrl =
		omap2_cm_read_mod_reg(OMAP3430ES2_USBHOST_MOD,
				      OMAP2_CM_CLKSTCTRL);
	cm_context.core_cm_autoidle1 =
		omap2_cm_read_mod_reg(CORE_MOD, CM_AUTOIDLE1);
	cm_context.core_cm_autoidle2 =
		omap2_cm_read_mod_reg(CORE_MOD, CM_AUTOIDLE2);
	cm_context.core_cm_autoidle3 =
		omap2_cm_read_mod_reg(CORE_MOD, CM_AUTOIDLE3);
	cm_context.wkup_cm_autoidle =
		omap2_cm_read_mod_reg(WKUP_MOD, CM_AUTOIDLE);
	cm_context.dss_cm_autoidle =
		omap2_cm_read_mod_reg(OMAP3430_DSS_MOD, CM_AUTOIDLE);
	cm_context.cam_cm_autoidle =
		omap2_cm_read_mod_reg(OMAP3430_CAM_MOD, CM_AUTOIDLE);
	cm_context.per_cm_autoidle =
		omap2_cm_read_mod_reg(OMAP3430_PER_MOD, CM_AUTOIDLE);
	cm_context.usbhost_cm_autoidle =
		omap2_cm_read_mod_reg(OMAP3430ES2_USBHOST_MOD, CM_AUTOIDLE);
	cm_context.sgx_cm_sleepdep =
		omap2_cm_read_mod_reg(OMAP3430ES2_SGX_MOD,
				      OMAP3430_CM_SLEEPDEP);
	cm_context.dss_cm_sleepdep =
		omap2_cm_read_mod_reg(OMAP3430_DSS_MOD, OMAP3430_CM_SLEEPDEP);
	cm_context.cam_cm_sleepdep =
		omap2_cm_read_mod_reg(OMAP3430_CAM_MOD, OMAP3430_CM_SLEEPDEP);
	cm_context.per_cm_sleepdep =
		omap2_cm_read_mod_reg(OMAP3430_PER_MOD, OMAP3430_CM_SLEEPDEP);
	cm_context.usbhost_cm_sleepdep =
		omap2_cm_read_mod_reg(OMAP3430ES2_USBHOST_MOD,
				      OMAP3430_CM_SLEEPDEP);
	cm_context.cm_clkout_ctrl =
		omap2_cm_read_mod_reg(OMAP3430_CCR_MOD,
				      OMAP3_CM_CLKOUT_CTRL_OFFSET);
}

void omap3_cm_restore_context(void)
{
	omap2_cm_write_mod_reg(cm_context.iva2_cm_clksel1, OMAP3430_IVA2_MOD,
			       CM_CLKSEL1);
	omap2_cm_write_mod_reg(cm_context.iva2_cm_clksel2, OMAP3430_IVA2_MOD,
			       CM_CLKSEL2);
	__raw_writel(cm_context.cm_sysconfig, OMAP3430_CM_SYSCONFIG);
	omap2_cm_write_mod_reg(cm_context.sgx_cm_clksel, OMAP3430ES2_SGX_MOD,
			       CM_CLKSEL);
	omap2_cm_write_mod_reg(cm_context.dss_cm_clksel, OMAP3430_DSS_MOD,
			       CM_CLKSEL);
	omap2_cm_write_mod_reg(cm_context.cam_cm_clksel, OMAP3430_CAM_MOD,
			       CM_CLKSEL);
	omap2_cm_write_mod_reg(cm_context.per_cm_clksel, OMAP3430_PER_MOD,
			       CM_CLKSEL);
	omap2_cm_write_mod_reg(cm_context.emu_cm_clksel, OMAP3430_EMU_MOD,
			       CM_CLKSEL1);
	omap2_cm_write_mod_reg(cm_context.emu_cm_clkstctrl, OMAP3430_EMU_MOD,
			       OMAP2_CM_CLKSTCTRL);
	/*
	 * As per erratum i671, ROM code does not respect the PER DPLL
	 * programming scheme if CM_AUTOIDLE_PLL.AUTO_PERIPH_DPLL == 1.
	 * In this case, we need to restore AUTO_PERIPH_DPLL by ourselves.
	 */
	omap2_cm_write_mod_reg(cm_context.pll_cm_autoidle, PLL_MOD,
			       CM_AUTOIDLE);
	omap2_cm_write_mod_reg(cm_context.pll_cm_autoidle2, PLL_MOD,
			       CM_AUTOIDLE2);
	omap2_cm_write_mod_reg(cm_context.pll_cm_clksel4, PLL_MOD,
			       OMAP3430ES2_CM_CLKSEL4);
	omap2_cm_write_mod_reg(cm_context.pll_cm_clksel5, PLL_MOD,
			       OMAP3430ES2_CM_CLKSEL5);
	omap2_cm_write_mod_reg(cm_context.pll_cm_clken2, PLL_MOD,
			       OMAP3430ES2_CM_CLKEN2);
	__raw_writel(cm_context.cm_polctrl, OMAP3430_CM_POLCTRL);
	omap2_cm_write_mod_reg(cm_context.iva2_cm_fclken, OMAP3430_IVA2_MOD,
			       CM_FCLKEN);
	omap2_cm_write_mod_reg(cm_context.iva2_cm_clken_pll, OMAP3430_IVA2_MOD,
			       OMAP3430_CM_CLKEN_PLL);
	omap2_cm_write_mod_reg(cm_context.core_cm_fclken1, CORE_MOD,
			       CM_FCLKEN1);
	omap2_cm_write_mod_reg(cm_context.core_cm_fclken3, CORE_MOD,
			       OMAP3430ES2_CM_FCLKEN3);
	omap2_cm_write_mod_reg(cm_context.sgx_cm_fclken, OMAP3430ES2_SGX_MOD,
			       CM_FCLKEN);
	omap2_cm_write_mod_reg(cm_context.wkup_cm_fclken, WKUP_MOD, CM_FCLKEN);
	omap2_cm_write_mod_reg(cm_context.dss_cm_fclken, OMAP3430_DSS_MOD,
			       CM_FCLKEN);
	omap2_cm_write_mod_reg(cm_context.cam_cm_fclken, OMAP3430_CAM_MOD,
			       CM_FCLKEN);
	omap2_cm_write_mod_reg(cm_context.per_cm_fclken, OMAP3430_PER_MOD,
			       CM_FCLKEN);
	omap2_cm_write_mod_reg(cm_context.usbhost_cm_fclken,
			       OMAP3430ES2_USBHOST_MOD, CM_FCLKEN);
	omap2_cm_write_mod_reg(cm_context.core_cm_iclken1, CORE_MOD,
			       CM_ICLKEN1);
	omap2_cm_write_mod_reg(cm_context.core_cm_iclken2, CORE_MOD,
			       CM_ICLKEN2);
	omap2_cm_write_mod_reg(cm_context.core_cm_iclken3, CORE_MOD,
			       CM_ICLKEN3);
	omap2_cm_write_mod_reg(cm_context.sgx_cm_iclken, OMAP3430ES2_SGX_MOD,
			       CM_ICLKEN);
	omap2_cm_write_mod_reg(cm_context.wkup_cm_iclken, WKUP_MOD, CM_ICLKEN);
	omap2_cm_write_mod_reg(cm_context.dss_cm_iclken, OMAP3430_DSS_MOD,
			       CM_ICLKEN);
	omap2_cm_write_mod_reg(cm_context.cam_cm_iclken, OMAP3430_CAM_MOD,
			       CM_ICLKEN);
	omap2_cm_write_mod_reg(cm_context.per_cm_iclken, OMAP3430_PER_MOD,
			       CM_ICLKEN);
	omap2_cm_write_mod_reg(cm_context.usbhost_cm_iclken,
			       OMAP3430ES2_USBHOST_MOD, CM_ICLKEN);
	omap2_cm_write_mod_reg(cm_context.iva2_cm_autoidle2, OMAP3430_IVA2_MOD,
			       CM_AUTOIDLE2);
	omap2_cm_write_mod_reg(cm_context.mpu_cm_autoidle2, MPU_MOD,
			       CM_AUTOIDLE2);
	omap2_cm_write_mod_reg(cm_context.iva2_cm_clkstctrl, OMAP3430_IVA2_MOD,
			       OMAP2_CM_CLKSTCTRL);
	omap2_cm_write_mod_reg(cm_context.mpu_cm_clkstctrl, MPU_MOD,
			       OMAP2_CM_CLKSTCTRL);
	omap2_cm_write_mod_reg(cm_context.core_cm_clkstctrl, CORE_MOD,
			       OMAP2_CM_CLKSTCTRL);
	omap2_cm_write_mod_reg(cm_context.sgx_cm_clkstctrl, OMAP3430ES2_SGX_MOD,
			       OMAP2_CM_CLKSTCTRL);
	omap2_cm_write_mod_reg(cm_context.dss_cm_clkstctrl, OMAP3430_DSS_MOD,
			       OMAP2_CM_CLKSTCTRL);
	omap2_cm_write_mod_reg(cm_context.cam_cm_clkstctrl, OMAP3430_CAM_MOD,
			       OMAP2_CM_CLKSTCTRL);
	omap2_cm_write_mod_reg(cm_context.per_cm_clkstctrl, OMAP3430_PER_MOD,
			       OMAP2_CM_CLKSTCTRL);
	omap2_cm_write_mod_reg(cm_context.neon_cm_clkstctrl, OMAP3430_NEON_MOD,
			       OMAP2_CM_CLKSTCTRL);
	omap2_cm_write_mod_reg(cm_context.usbhost_cm_clkstctrl,
			       OMAP3430ES2_USBHOST_MOD, OMAP2_CM_CLKSTCTRL);
	omap2_cm_write_mod_reg(cm_context.core_cm_autoidle1, CORE_MOD,
			       CM_AUTOIDLE1);
	omap2_cm_write_mod_reg(cm_context.core_cm_autoidle2, CORE_MOD,
			       CM_AUTOIDLE2);
	omap2_cm_write_mod_reg(cm_context.core_cm_autoidle3, CORE_MOD,
			       CM_AUTOIDLE3);
	omap2_cm_write_mod_reg(cm_context.wkup_cm_autoidle, WKUP_MOD,
			       CM_AUTOIDLE);
	omap2_cm_write_mod_reg(cm_context.dss_cm_autoidle, OMAP3430_DSS_MOD,
			       CM_AUTOIDLE);
	omap2_cm_write_mod_reg(cm_context.cam_cm_autoidle, OMAP3430_CAM_MOD,
			       CM_AUTOIDLE);
	omap2_cm_write_mod_reg(cm_context.per_cm_autoidle, OMAP3430_PER_MOD,
			       CM_AUTOIDLE);
	omap2_cm_write_mod_reg(cm_context.usbhost_cm_autoidle,
			       OMAP3430ES2_USBHOST_MOD, CM_AUTOIDLE);
	omap2_cm_write_mod_reg(cm_context.sgx_cm_sleepdep, OMAP3430ES2_SGX_MOD,
			       OMAP3430_CM_SLEEPDEP);
	omap2_cm_write_mod_reg(cm_context.dss_cm_sleepdep, OMAP3430_DSS_MOD,
			       OMAP3430_CM_SLEEPDEP);
	omap2_cm_write_mod_reg(cm_context.cam_cm_sleepdep, OMAP3430_CAM_MOD,
			       OMAP3430_CM_SLEEPDEP);
	omap2_cm_write_mod_reg(cm_context.per_cm_sleepdep, OMAP3430_PER_MOD,
			       OMAP3430_CM_SLEEPDEP);
	omap2_cm_write_mod_reg(cm_context.usbhost_cm_sleepdep,
			       OMAP3430ES2_USBHOST_MOD, OMAP3430_CM_SLEEPDEP);
	omap2_cm_write_mod_reg(cm_context.cm_clkout_ctrl, OMAP3430_CCR_MOD,
			       OMAP3_CM_CLKOUT_CTRL_OFFSET);
}

/*
 *
 */

static struct cm_ll_data omap3xxx_cm_ll_data = {
	.split_idlest_reg	= &omap3xxx_cm_split_idlest_reg,
	.wait_module_ready	= &omap3xxx_cm_wait_module_ready,
};

int __init omap3xxx_cm_init(void)
{
	if (!cpu_is_omap34xx())
		return 0;

	return cm_register(&omap3xxx_cm_ll_data);
}

static void __exit omap3xxx_cm_exit(void)
{
	if (!cpu_is_omap34xx())
		return;

	/* Should never happen */
	WARN(cm_unregister(&omap3xxx_cm_ll_data),
	     "%s: cm_ll_data function pointer mismatch\n", __func__);
}
__exitcall(omap3xxx_cm_exit);
