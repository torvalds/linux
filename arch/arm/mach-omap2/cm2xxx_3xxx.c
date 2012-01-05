/*
 * OMAP2/3 CM module functions
 *
 * Copyright (C) 2009 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>

#include "common.h"

#include "cm.h"
#include "cm2xxx_3xxx.h"
#include "cm-regbits-24xx.h"
#include "cm-regbits-34xx.h"

/* CM_AUTOIDLE_PLL.AUTO_* bit values for DPLLs */
#define DPLL_AUTOIDLE_DISABLE				0x0
#define OMAP2XXX_DPLL_AUTOIDLE_LOW_POWER_STOP		0x3

/* CM_AUTOIDLE_PLL.AUTO_* bit values for APLLs (OMAP2xxx only) */
#define OMAP2XXX_APLL_AUTOIDLE_DISABLE			0x0
#define OMAP2XXX_APLL_AUTOIDLE_LOW_POWER_STOP		0x3

static const u8 cm_idlest_offs[] = {
	CM_IDLEST1, CM_IDLEST2, OMAP2430_CM_IDLEST3
};

u32 omap2_cm_read_mod_reg(s16 module, u16 idx)
{
	return __raw_readl(cm_base + module + idx);
}

void omap2_cm_write_mod_reg(u32 val, s16 module, u16 idx)
{
	__raw_writel(val, cm_base + module + idx);
}

/* Read-modify-write a register in a CM module. Caller must lock */
u32 omap2_cm_rmw_mod_reg_bits(u32 mask, u32 bits, s16 module, s16 idx)
{
	u32 v;

	v = omap2_cm_read_mod_reg(module, idx);
	v &= ~mask;
	v |= bits;
	omap2_cm_write_mod_reg(v, module, idx);

	return v;
}

u32 omap2_cm_set_mod_reg_bits(u32 bits, s16 module, s16 idx)
{
	return omap2_cm_rmw_mod_reg_bits(bits, bits, module, idx);
}

u32 omap2_cm_clear_mod_reg_bits(u32 bits, s16 module, s16 idx)
{
	return omap2_cm_rmw_mod_reg_bits(bits, 0x0, module, idx);
}

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

bool omap2_cm_is_clkdm_in_hwsup(s16 module, u32 mask)
{
	u32 v;
	bool ret = 0;

	BUG_ON(!cpu_is_omap24xx() && !cpu_is_omap34xx());

	v = omap2_cm_read_mod_reg(module, OMAP2_CM_CLKSTCTRL);
	v &= mask;
	v >>= __ffs(mask);

	if (cpu_is_omap24xx())
		ret = (v == OMAP24XX_CLKSTCTRL_ENABLE_AUTO) ? 1 : 0;
	else
		ret = (v == OMAP34XX_CLKSTCTRL_ENABLE_AUTO) ? 1 : 0;

	return ret;
}

void omap2xxx_cm_clkdm_enable_hwsup(s16 module, u32 mask)
{
	_write_clktrctrl(OMAP24XX_CLKSTCTRL_ENABLE_AUTO, module, mask);
}

void omap2xxx_cm_clkdm_disable_hwsup(s16 module, u32 mask)
{
	_write_clktrctrl(OMAP24XX_CLKSTCTRL_DISABLE_AUTO, module, mask);
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
 * APLL autoidle control
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

/*
 *
 */

/**
 * omap2_cm_wait_idlest_ready - wait for a module to leave idle or standby
 * @prcm_mod: PRCM module offset
 * @idlest_id: CM_IDLESTx register ID (i.e., x = 1, 2, 3)
 * @idlest_shift: shift of the bit in the CM_IDLEST* register to check
 *
 * XXX document
 */
int omap2_cm_wait_module_ready(s16 prcm_mod, u8 idlest_id, u8 idlest_shift)
{
	int ena = 0, i = 0;
	u8 cm_idlest_reg;
	u32 mask;

	if (!idlest_id || (idlest_id > ARRAY_SIZE(cm_idlest_offs)))
		return -EINVAL;

	cm_idlest_reg = cm_idlest_offs[idlest_id - 1];

	mask = 1 << idlest_shift;

	if (cpu_is_omap24xx())
		ena = mask;
	else if (cpu_is_omap34xx())
		ena = 0;
	else
		BUG();

	omap_test_timeout(((omap2_cm_read_mod_reg(prcm_mod, cm_idlest_reg) & mask) == ena),
			  MAX_MODULE_READY_TIME, i);

	return (i < MAX_MODULE_READY_TIME) ? 0 : -EBUSY;
}

/*
 * Context save/restore code - OMAP3 only
 */
#ifdef CONFIG_ARCH_OMAP3
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
#endif
