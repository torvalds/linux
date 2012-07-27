/*
 * OMAP2/3 clockdomain common data
 *
 * Copyright (C) 2008-2011 Texas Instruments, Inc.
 * Copyright (C) 2008-2010 Nokia Corporation
 *
 * Paul Walmsley, Jouni Högander
 *
 * This file contains clockdomains and clockdomain wakeup/sleep
 * dependencies for the OMAP2/3 chips.  Some notes:
 *
 * A useful validation rule for struct clockdomain: Any clockdomain
 * referenced by a wkdep_srcs or sleepdep_srcs array must have a
 * dep_bit assigned.  So wkdep_srcs/sleepdep_srcs are really just
 * software-controllable dependencies.  Non-software-controllable
 * dependencies do exist, but they are not encoded below (yet).
 *
 * 24xx does not support programmable sleep dependencies (SLEEPDEP)
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
#include "cm-regbits-24xx.h"
#include "cm-regbits-34xx.h"
#include "cm-regbits-44xx.h"
#include "prm-regbits-24xx.h"
#include "prm-regbits-34xx.h"

/*
 * Clockdomain dependencies for wkdeps/sleepdeps
 *
 * XXX Hardware dependencies (e.g., dependencies that cannot be
 * changed in software) are not included here yet, but should be.
 */

/* Wakeup dependency source arrays */

/* 2xxx-specific possible dependencies */

/* 2xxx PM_WKDEP_GFX: CORE, MPU, WKUP */
struct clkdm_dep gfx_24xx_wkdeps[] = {
	{ .clkdm_name = "core_l3_clkdm" },
	{ .clkdm_name = "core_l4_clkdm" },
	{ .clkdm_name = "mpu_clkdm" },
	{ .clkdm_name = "wkup_clkdm" },
	{ NULL },
};

/* 2xxx PM_WKDEP_DSP: CORE, MPU, WKUP */
struct clkdm_dep dsp_24xx_wkdeps[] = {
	{ .clkdm_name = "core_l3_clkdm" },
	{ .clkdm_name = "core_l4_clkdm" },
	{ .clkdm_name = "mpu_clkdm" },
	{ .clkdm_name = "wkup_clkdm" },
	{ NULL },
};


/*
 * OMAP2/3-common clockdomains
 *
 * Even though the 2420 has a single PRCM module from the
 * interconnect's perspective, internally it does appear to have
 * separate PRM and CM clockdomains.  The usual test case is
 * sys_clkout/sys_clkout2.
 */

/* This is an implicit clockdomain - it is never defined as such in TRM */
struct clockdomain wkup_common_clkdm = {
	.name		= "wkup_clkdm",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.dep_bit	= OMAP_EN_WKUP_SHIFT,
	.flags		= CLKDM_ACTIVE_WITH_MPU,
};
