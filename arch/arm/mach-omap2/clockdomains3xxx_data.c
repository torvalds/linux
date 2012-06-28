/*
 * OMAP3xxx clockdomains
 *
 * Copyright (C) 2008-2011 Texas Instruments, Inc.
 * Copyright (C) 2008-2010 Nokia Corporation
 *
 * Paul Walmsley, Jouni Högander
 *
 * This file contains clockdomains and clockdomain wakeup/sleep
 * dependencies for the OMAP3xxx chips.  Some notes:
 *
 * A useful validation rule for struct clockdomain: Any clockdomain
 * referenced by a wkdep_srcs or sleepdep_srcs array must have a
 * dep_bit assigned.  So wkdep_srcs/sleepdep_srcs are really just
 * software-controllable dependencies.  Non-software-controllable
 * dependencies do exist, but they are not encoded below (yet).
 *
 * The overly-specific dep_bit names are due to a bit name collision
 * with CM_FCLKEN_{DSP,IVA2}.  The DSP/IVA2 PM_WKDEP and CM_SLEEPDEP shift
 * value are the same for all powerdomains: 2
 *
 * XXX should dep_bit be a mask, so we can test to see if it is 0 as a
 * sanity check?
 * XXX encode hardware fixed wakeup dependencies -- esp. for 3430 CORE
 */

/*
 * To-Do List
 * -> Port the Sleep/Wakeup dependencies for the domains
 *    from the Power domain framework
 */

#include <linux/kernel.h>
#include <linux/io.h>

#include "clockdomain.h"
#include "prm2xxx_3xxx.h"
#include "cm2xxx_3xxx.h"
#include "cm-regbits-34xx.h"
#include "prm-regbits-34xx.h"

/*
 * Clockdomain dependencies for wkdeps/sleepdeps
 *
 * XXX Hardware dependencies (e.g., dependencies that cannot be
 * changed in software) are not included here yet, but should be.
 */

/* OMAP3-specific possible dependencies */

/*
 * 3430ES1 PM_WKDEP_GFX: adds IVA2, removes CORE
 * 3430ES2 PM_WKDEP_SGX: adds IVA2, removes CORE
 */
static struct clkdm_dep gfx_sgx_3xxx_wkdeps[] = {
	{ .clkdm_name = "iva2_clkdm" },
	{ .clkdm_name = "mpu_clkdm" },
	{ .clkdm_name = "wkup_clkdm" },
	{ NULL },
};

static struct clkdm_dep gfx_sgx_am35x_wkdeps[] = {
	{ .clkdm_name = "mpu_clkdm" },
	{ .clkdm_name = "wkup_clkdm" },
	{ NULL },
};

/* 3430: PM_WKDEP_PER: CORE, IVA2, MPU, WKUP */
static struct clkdm_dep per_wkdeps[] = {
	{ .clkdm_name = "core_l3_clkdm" },
	{ .clkdm_name = "core_l4_clkdm" },
	{ .clkdm_name = "iva2_clkdm" },
	{ .clkdm_name = "mpu_clkdm" },
	{ .clkdm_name = "wkup_clkdm" },
	{ NULL },
};

static struct clkdm_dep per_am35x_wkdeps[] = {
	{ .clkdm_name = "core_l3_clkdm" },
	{ .clkdm_name = "core_l4_clkdm" },
	{ .clkdm_name = "mpu_clkdm" },
	{ .clkdm_name = "wkup_clkdm" },
	{ NULL },
};

/* 3430ES2: PM_WKDEP_USBHOST: CORE, IVA2, MPU, WKUP */
static struct clkdm_dep usbhost_wkdeps[] = {
	{ .clkdm_name = "core_l3_clkdm" },
	{ .clkdm_name = "core_l4_clkdm" },
	{ .clkdm_name = "iva2_clkdm" },
	{ .clkdm_name = "mpu_clkdm" },
	{ .clkdm_name = "wkup_clkdm" },
	{ NULL },
};

static struct clkdm_dep usbhost_am35x_wkdeps[] = {
	{ .clkdm_name = "core_l3_clkdm" },
	{ .clkdm_name = "core_l4_clkdm" },
	{ .clkdm_name = "mpu_clkdm" },
	{ .clkdm_name = "wkup_clkdm" },
	{ NULL },
};

/* 3430 PM_WKDEP_MPU: CORE, IVA2, DSS, PER */
static struct clkdm_dep mpu_3xxx_wkdeps[] = {
	{ .clkdm_name = "core_l3_clkdm" },
	{ .clkdm_name = "core_l4_clkdm" },
	{ .clkdm_name = "iva2_clkdm" },
	{ .clkdm_name = "dss_clkdm" },
	{ .clkdm_name = "per_clkdm" },
	{ NULL },
};

static struct clkdm_dep mpu_am35x_wkdeps[] = {
	{ .clkdm_name = "core_l3_clkdm" },
	{ .clkdm_name = "core_l4_clkdm" },
	{ .clkdm_name = "dss_clkdm" },
	{ .clkdm_name = "per_clkdm" },
	{ NULL },
};

/* 3430 PM_WKDEP_IVA2: CORE, MPU, WKUP, DSS, PER */
static struct clkdm_dep iva2_wkdeps[] = {
	{ .clkdm_name = "core_l3_clkdm" },
	{ .clkdm_name = "core_l4_clkdm" },
	{ .clkdm_name = "mpu_clkdm" },
	{ .clkdm_name = "wkup_clkdm" },
	{ .clkdm_name = "dss_clkdm" },
	{ .clkdm_name = "per_clkdm" },
	{ NULL },
};

/* 3430 PM_WKDEP_CAM: IVA2, MPU, WKUP */
static struct clkdm_dep cam_wkdeps[] = {
	{ .clkdm_name = "iva2_clkdm" },
	{ .clkdm_name = "mpu_clkdm" },
	{ .clkdm_name = "wkup_clkdm" },
	{ NULL },
};

/* 3430 PM_WKDEP_DSS: IVA2, MPU, WKUP */
static struct clkdm_dep dss_wkdeps[] = {
	{ .clkdm_name = "iva2_clkdm" },
	{ .clkdm_name = "mpu_clkdm" },
	{ .clkdm_name = "wkup_clkdm" },
	{ NULL },
};

static struct clkdm_dep dss_am35x_wkdeps[] = {
	{ .clkdm_name = "mpu_clkdm" },
	{ .clkdm_name = "wkup_clkdm" },
	{ NULL },
};

/* 3430: PM_WKDEP_NEON: MPU */
static struct clkdm_dep neon_wkdeps[] = {
	{ .clkdm_name = "mpu_clkdm" },
	{ NULL },
};

/* Sleep dependency source arrays for OMAP3-specific clkdms */

/* 3430: CM_SLEEPDEP_DSS: MPU, IVA */
static struct clkdm_dep dss_sleepdeps[] = {
	{ .clkdm_name = "mpu_clkdm" },
	{ .clkdm_name = "iva2_clkdm" },
	{ NULL },
};

static struct clkdm_dep dss_am35x_sleepdeps[] = {
	{ .clkdm_name = "mpu_clkdm" },
	{ NULL },
};

/* 3430: CM_SLEEPDEP_PER: MPU, IVA */
static struct clkdm_dep per_sleepdeps[] = {
	{ .clkdm_name = "mpu_clkdm" },
	{ .clkdm_name = "iva2_clkdm" },
	{ NULL },
};

static struct clkdm_dep per_am35x_sleepdeps[] = {
	{ .clkdm_name = "mpu_clkdm" },
	{ NULL },
};

/* 3430ES2: CM_SLEEPDEP_USBHOST: MPU, IVA */
static struct clkdm_dep usbhost_sleepdeps[] = {
	{ .clkdm_name = "mpu_clkdm" },
	{ .clkdm_name = "iva2_clkdm" },
	{ NULL },
};

static struct clkdm_dep usbhost_am35x_sleepdeps[] = {
	{ .clkdm_name = "mpu_clkdm" },
	{ NULL },
};

/* 3430: CM_SLEEPDEP_CAM: MPU */
static struct clkdm_dep cam_sleepdeps[] = {
	{ .clkdm_name = "mpu_clkdm" },
	{ NULL },
};

/*
 * 3430ES1: CM_SLEEPDEP_GFX: MPU
 * 3430ES2: CM_SLEEPDEP_SGX: MPU
 * These can share data since they will never be present simultaneously
 * on the same device.
 */
static struct clkdm_dep gfx_sgx_sleepdeps[] = {
	{ .clkdm_name = "mpu_clkdm" },
	{ NULL },
};

/*
 * OMAP3 clockdomains
 */

static struct clockdomain mpu_3xxx_clkdm = {
	.name		= "mpu_clkdm",
	.pwrdm		= { .name = "mpu_pwrdm" },
	.flags		= CLKDM_CAN_HWSUP | CLKDM_CAN_FORCE_WAKEUP,
	.dep_bit	= OMAP3430_EN_MPU_SHIFT,
	.wkdep_srcs	= mpu_3xxx_wkdeps,
	.clktrctrl_mask = OMAP3430_CLKTRCTRL_MPU_MASK,
};

static struct clockdomain mpu_am35x_clkdm = {
	.name		= "mpu_clkdm",
	.pwrdm		= { .name = "mpu_pwrdm" },
	.flags		= CLKDM_CAN_HWSUP | CLKDM_CAN_FORCE_WAKEUP,
	.dep_bit	= OMAP3430_EN_MPU_SHIFT,
	.wkdep_srcs	= mpu_am35x_wkdeps,
	.clktrctrl_mask = OMAP3430_CLKTRCTRL_MPU_MASK,
};

static struct clockdomain neon_clkdm = {
	.name		= "neon_clkdm",
	.pwrdm		= { .name = "neon_pwrdm" },
	.flags		= CLKDM_CAN_HWSUP_SWSUP,
	.wkdep_srcs	= neon_wkdeps,
	.clktrctrl_mask = OMAP3430_CLKTRCTRL_NEON_MASK,
};

static struct clockdomain iva2_clkdm = {
	.name		= "iva2_clkdm",
	.pwrdm		= { .name = "iva2_pwrdm" },
	.flags		= CLKDM_CAN_HWSUP_SWSUP,
	.dep_bit	= OMAP3430_PM_WKDEP_MPU_EN_IVA2_SHIFT,
	.wkdep_srcs	= iva2_wkdeps,
	.clktrctrl_mask = OMAP3430_CLKTRCTRL_IVA2_MASK,
};

static struct clockdomain gfx_3430es1_clkdm = {
	.name		= "gfx_clkdm",
	.pwrdm		= { .name = "gfx_pwrdm" },
	.flags		= CLKDM_CAN_HWSUP_SWSUP,
	.wkdep_srcs	= gfx_sgx_3xxx_wkdeps,
	.sleepdep_srcs	= gfx_sgx_sleepdeps,
	.clktrctrl_mask = OMAP3430ES1_CLKTRCTRL_GFX_MASK,
};

static struct clockdomain sgx_clkdm = {
	.name		= "sgx_clkdm",
	.pwrdm		= { .name = "sgx_pwrdm" },
	.flags		= CLKDM_CAN_HWSUP_SWSUP,
	.wkdep_srcs	= gfx_sgx_3xxx_wkdeps,
	.sleepdep_srcs	= gfx_sgx_sleepdeps,
	.clktrctrl_mask = OMAP3430ES2_CLKTRCTRL_SGX_MASK,
};

static struct clockdomain sgx_am35x_clkdm = {
	.name		= "sgx_clkdm",
	.pwrdm		= { .name = "sgx_pwrdm" },
	.flags		= CLKDM_CAN_HWSUP_SWSUP,
	.wkdep_srcs	= gfx_sgx_am35x_wkdeps,
	.sleepdep_srcs	= gfx_sgx_sleepdeps,
	.clktrctrl_mask = OMAP3430ES2_CLKTRCTRL_SGX_MASK,
};

/*
 * The die-to-die clockdomain was documented in the 34xx ES1 TRM, but
 * then that information was removed from the 34xx ES2+ TRM.  It is
 * unclear whether the core is still there, but the clockdomain logic
 * is there, and must be programmed to an appropriate state if the
 * CORE clockdomain is to become inactive.
 */
static struct clockdomain d2d_clkdm = {
	.name		= "d2d_clkdm",
	.pwrdm		= { .name = "core_pwrdm" },
	.flags		= CLKDM_CAN_HWSUP_SWSUP,
	.clktrctrl_mask = OMAP3430ES1_CLKTRCTRL_D2D_MASK,
};

/*
 * XXX add usecounting for clkdm dependencies, otherwise the presence
 * of a single dep bit for core_l3_3xxx_clkdm and core_l4_3xxx_clkdm
 * could cause trouble
 */
static struct clockdomain core_l3_3xxx_clkdm = {
	.name		= "core_l3_clkdm",
	.pwrdm		= { .name = "core_pwrdm" },
	.flags		= CLKDM_CAN_HWSUP,
	.dep_bit	= OMAP3430_EN_CORE_SHIFT,
	.clktrctrl_mask = OMAP3430_CLKTRCTRL_L3_MASK,
};

/*
 * XXX add usecounting for clkdm dependencies, otherwise the presence
 * of a single dep bit for core_l3_3xxx_clkdm and core_l4_3xxx_clkdm
 * could cause trouble
 */
static struct clockdomain core_l4_3xxx_clkdm = {
	.name		= "core_l4_clkdm",
	.pwrdm		= { .name = "core_pwrdm" },
	.flags		= CLKDM_CAN_HWSUP,
	.dep_bit	= OMAP3430_EN_CORE_SHIFT,
	.clktrctrl_mask = OMAP3430_CLKTRCTRL_L4_MASK,
};

/* Another case of bit name collisions between several registers: EN_DSS */
static struct clockdomain dss_3xxx_clkdm = {
	.name		= "dss_clkdm",
	.pwrdm		= { .name = "dss_pwrdm" },
	.flags		= CLKDM_CAN_HWSUP_SWSUP,
	.dep_bit	= OMAP3430_PM_WKDEP_MPU_EN_DSS_SHIFT,
	.wkdep_srcs	= dss_wkdeps,
	.sleepdep_srcs	= dss_sleepdeps,
	.clktrctrl_mask = OMAP3430_CLKTRCTRL_DSS_MASK,
};

static struct clockdomain dss_am35x_clkdm = {
	.name		= "dss_clkdm",
	.pwrdm		= { .name = "dss_pwrdm" },
	.flags		= CLKDM_CAN_HWSUP_SWSUP,
	.dep_bit	= OMAP3430_PM_WKDEP_MPU_EN_DSS_SHIFT,
	.wkdep_srcs	= dss_am35x_wkdeps,
	.sleepdep_srcs	= dss_am35x_sleepdeps,
	.clktrctrl_mask = OMAP3430_CLKTRCTRL_DSS_MASK,
};

static struct clockdomain cam_clkdm = {
	.name		= "cam_clkdm",
	.pwrdm		= { .name = "cam_pwrdm" },
	.flags		= CLKDM_CAN_HWSUP_SWSUP,
	.wkdep_srcs	= cam_wkdeps,
	.sleepdep_srcs	= cam_sleepdeps,
	.clktrctrl_mask = OMAP3430_CLKTRCTRL_CAM_MASK,
};

static struct clockdomain usbhost_clkdm = {
	.name		= "usbhost_clkdm",
	.pwrdm		= { .name = "usbhost_pwrdm" },
	.flags		= CLKDM_CAN_HWSUP_SWSUP,
	.wkdep_srcs	= usbhost_wkdeps,
	.sleepdep_srcs	= usbhost_sleepdeps,
	.clktrctrl_mask = OMAP3430ES2_CLKTRCTRL_USBHOST_MASK,
};

static struct clockdomain usbhost_am35x_clkdm = {
	.name		= "usbhost_clkdm",
	.pwrdm		= { .name = "core_pwrdm" },
	.flags		= CLKDM_CAN_HWSUP_SWSUP,
	.wkdep_srcs	= usbhost_am35x_wkdeps,
	.sleepdep_srcs	= usbhost_am35x_sleepdeps,
	.clktrctrl_mask = OMAP3430ES2_CLKTRCTRL_USBHOST_MASK,
};

static struct clockdomain per_clkdm = {
	.name		= "per_clkdm",
	.pwrdm		= { .name = "per_pwrdm" },
	.flags		= CLKDM_CAN_HWSUP_SWSUP,
	.dep_bit	= OMAP3430_EN_PER_SHIFT,
	.wkdep_srcs	= per_wkdeps,
	.sleepdep_srcs	= per_sleepdeps,
	.clktrctrl_mask = OMAP3430_CLKTRCTRL_PER_MASK,
};

static struct clockdomain per_am35x_clkdm = {
	.name		= "per_clkdm",
	.pwrdm		= { .name = "per_pwrdm" },
	.flags		= CLKDM_CAN_HWSUP_SWSUP,
	.dep_bit	= OMAP3430_EN_PER_SHIFT,
	.wkdep_srcs	= per_am35x_wkdeps,
	.sleepdep_srcs	= per_am35x_sleepdeps,
	.clktrctrl_mask = OMAP3430_CLKTRCTRL_PER_MASK,
};

/*
 * Disable hw supervised mode for emu_clkdm, because emu_pwrdm is
 * switched of even if sdti is in use
 */
static struct clockdomain emu_clkdm = {
	.name		= "emu_clkdm",
	.pwrdm		= { .name = "emu_pwrdm" },
	.flags		= /* CLKDM_CAN_ENABLE_AUTO |  */CLKDM_CAN_SWSUP,
	.clktrctrl_mask = OMAP3430_CLKTRCTRL_EMU_MASK,
};

static struct clockdomain dpll1_clkdm = {
	.name		= "dpll1_clkdm",
	.pwrdm		= { .name = "dpll1_pwrdm" },
};

static struct clockdomain dpll2_clkdm = {
	.name		= "dpll2_clkdm",
	.pwrdm		= { .name = "dpll2_pwrdm" },
};

static struct clockdomain dpll3_clkdm = {
	.name		= "dpll3_clkdm",
	.pwrdm		= { .name = "dpll3_pwrdm" },
};

static struct clockdomain dpll4_clkdm = {
	.name		= "dpll4_clkdm",
	.pwrdm		= { .name = "dpll4_pwrdm" },
};

static struct clockdomain dpll5_clkdm = {
	.name		= "dpll5_clkdm",
	.pwrdm		= { .name = "dpll5_pwrdm" },
};

/*
 * Clockdomain hwsup dependencies
 */

static struct clkdm_autodep clkdm_autodeps[] = {
	{
		.clkdm = { .name = "mpu_clkdm" },
	},
	{
		.clkdm = { .name = "iva2_clkdm" },
	},
	{
		.clkdm = { .name = NULL },
	}
};

static struct clkdm_autodep clkdm_am35x_autodeps[] = {
	{
		.clkdm = { .name = "mpu_clkdm" },
	},
	{
		.clkdm = { .name = NULL },
	}
};

/*
 *
 */

static struct clockdomain *clockdomains_common[] __initdata = {
	&wkup_common_clkdm,
	&cm_common_clkdm,
	&prm_common_clkdm,
	&neon_clkdm,
	&core_l3_3xxx_clkdm,
	&core_l4_3xxx_clkdm,
	&emu_clkdm,
	&dpll1_clkdm,
	&dpll3_clkdm,
	&dpll4_clkdm,
	NULL
};

static struct clockdomain *clockdomains_omap3430[] __initdata = {
	&mpu_3xxx_clkdm,
	&iva2_clkdm,
	&d2d_clkdm,
	&dss_3xxx_clkdm,
	&cam_clkdm,
	&per_clkdm,
	&dpll2_clkdm,
	NULL
};

static struct clockdomain *clockdomains_omap3430es1[] __initdata = {
	&gfx_3430es1_clkdm,
	NULL,
};

static struct clockdomain *clockdomains_omap3430es2plus[] __initdata = {
	&sgx_clkdm,
	&dpll5_clkdm,
	&usbhost_clkdm,
	NULL,
};

static struct clockdomain *clockdomains_am35x[] __initdata = {
	&mpu_am35x_clkdm,
	&sgx_am35x_clkdm,
	&dss_am35x_clkdm,
	&per_am35x_clkdm,
	&usbhost_am35x_clkdm,
	&dpll5_clkdm,
	NULL
};

void __init omap3xxx_clockdomains_init(void)
{
	struct clockdomain **sc;
	unsigned int rev;

	if (!cpu_is_omap34xx())
		return;

	clkdm_register_platform_funcs(&omap3_clkdm_operations);
	clkdm_register_clkdms(clockdomains_common);

	rev = omap_rev();

	if (rev == AM35XX_REV_ES1_0 || rev == AM35XX_REV_ES1_1) {
		clkdm_register_clkdms(clockdomains_am35x);
		clkdm_register_autodeps(clkdm_am35x_autodeps);
	} else {
		clkdm_register_clkdms(clockdomains_omap3430);

		sc = (rev == OMAP3430_REV_ES1_0) ?
			clockdomains_omap3430es1 : clockdomains_omap3430es2plus;

		clkdm_register_clkdms(sc);
		clkdm_register_autodeps(clkdm_autodeps);
	}

	clkdm_complete_init();
}
