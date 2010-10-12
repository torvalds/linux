/*
 * omap_hwmod_3xxx_data.c - hardware modules present on the OMAP3xxx chips
 *
 * Copyright (C) 2009-2010 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The data in this file should be completely autogeneratable from
 * the TI hardware database or other technical documentation.
 *
 * XXX these should be marked initdata for multi-OMAP kernels
 */
#include <plat/omap_hwmod.h>
#include <mach/irqs.h>
#include <plat/cpu.h>
#include <plat/dma.h>

#include "omap_hwmod_common_data.h"

#include "prm-regbits-34xx.h"

/*
 * OMAP3xxx hardware module integration data
 *
 * ALl of the data in this section should be autogeneratable from the
 * TI hardware database or other technical documentation.  Data that
 * is driver-specific or driver-kernel integration-specific belongs
 * elsewhere.
 */

static struct omap_hwmod omap3xxx_mpu_hwmod;
static struct omap_hwmod omap3xxx_iva_hwmod;
static struct omap_hwmod omap3xxx_l3_main_hwmod;
static struct omap_hwmod omap3xxx_l4_core_hwmod;
static struct omap_hwmod omap3xxx_l4_per_hwmod;

/* L3 -> L4_CORE interface */
static struct omap_hwmod_ocp_if omap3xxx_l3_main__l4_core = {
	.master	= &omap3xxx_l3_main_hwmod,
	.slave	= &omap3xxx_l4_core_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L3 -> L4_PER interface */
static struct omap_hwmod_ocp_if omap3xxx_l3_main__l4_per = {
	.master = &omap3xxx_l3_main_hwmod,
	.slave	= &omap3xxx_l4_per_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};

/* MPU -> L3 interface */
static struct omap_hwmod_ocp_if omap3xxx_mpu__l3_main = {
	.master = &omap3xxx_mpu_hwmod,
	.slave	= &omap3xxx_l3_main_hwmod,
	.user	= OCP_USER_MPU,
};

/* Slave interfaces on the L3 interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l3_main_slaves[] = {
	&omap3xxx_mpu__l3_main,
};

/* Master interfaces on the L3 interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l3_main_masters[] = {
	&omap3xxx_l3_main__l4_core,
	&omap3xxx_l3_main__l4_per,
};

/* L3 */
static struct omap_hwmod omap3xxx_l3_main_hwmod = {
	.name		= "l3_main",
	.class		= &l3_hwmod_class,
	.masters	= omap3xxx_l3_main_masters,
	.masters_cnt	= ARRAY_SIZE(omap3xxx_l3_main_masters),
	.slaves		= omap3xxx_l3_main_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_l3_main_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.flags		= HWMOD_NO_IDLEST,
};

static struct omap_hwmod omap3xxx_l4_wkup_hwmod;

/* L4_CORE -> L4_WKUP interface */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__l4_wkup = {
	.master	= &omap3xxx_l4_core_hwmod,
	.slave	= &omap3xxx_l4_wkup_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};

/* Slave interfaces on the L4_CORE interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l4_core_slaves[] = {
	&omap3xxx_l3_main__l4_core,
};

/* Master interfaces on the L4_CORE interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l4_core_masters[] = {
	&omap3xxx_l4_core__l4_wkup,
};

/* L4 CORE */
static struct omap_hwmod omap3xxx_l4_core_hwmod = {
	.name		= "l4_core",
	.class		= &l4_hwmod_class,
	.masters	= omap3xxx_l4_core_masters,
	.masters_cnt	= ARRAY_SIZE(omap3xxx_l4_core_masters),
	.slaves		= omap3xxx_l4_core_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_l4_core_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.flags		= HWMOD_NO_IDLEST,
};

/* Slave interfaces on the L4_PER interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l4_per_slaves[] = {
	&omap3xxx_l3_main__l4_per,
};

/* Master interfaces on the L4_PER interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l4_per_masters[] = {
};

/* L4 PER */
static struct omap_hwmod omap3xxx_l4_per_hwmod = {
	.name		= "l4_per",
	.class		= &l4_hwmod_class,
	.masters	= omap3xxx_l4_per_masters,
	.masters_cnt	= ARRAY_SIZE(omap3xxx_l4_per_masters),
	.slaves		= omap3xxx_l4_per_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_l4_per_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.flags		= HWMOD_NO_IDLEST,
};

/* Slave interfaces on the L4_WKUP interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l4_wkup_slaves[] = {
	&omap3xxx_l4_core__l4_wkup,
};

/* Master interfaces on the L4_WKUP interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l4_wkup_masters[] = {
};

/* L4 WKUP */
static struct omap_hwmod omap3xxx_l4_wkup_hwmod = {
	.name		= "l4_wkup",
	.class		= &l4_hwmod_class,
	.masters	= omap3xxx_l4_wkup_masters,
	.masters_cnt	= ARRAY_SIZE(omap3xxx_l4_wkup_masters),
	.slaves		= omap3xxx_l4_wkup_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_l4_wkup_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.flags		= HWMOD_NO_IDLEST,
};

/* Master interfaces on the MPU device */
static struct omap_hwmod_ocp_if *omap3xxx_mpu_masters[] = {
	&omap3xxx_mpu__l3_main,
};

/* MPU */
static struct omap_hwmod omap3xxx_mpu_hwmod = {
	.name		= "mpu",
	.class		= &mpu_hwmod_class,
	.main_clk	= "arm_fck",
	.masters	= omap3xxx_mpu_masters,
	.masters_cnt	= ARRAY_SIZE(omap3xxx_mpu_masters),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

/*
 * IVA2_2 interface data
 */

/* IVA2 <- L3 interface */
static struct omap_hwmod_ocp_if omap3xxx_l3__iva = {
	.master		= &omap3xxx_l3_main_hwmod,
	.slave		= &omap3xxx_iva_hwmod,
	.clk		= "iva2_ck",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if *omap3xxx_iva_masters[] = {
	&omap3xxx_l3__iva,
};

/*
 * IVA2 (IVA2)
 */

static struct omap_hwmod omap3xxx_iva_hwmod = {
	.name		= "iva",
	.class		= &iva_hwmod_class,
	.masters	= omap3xxx_iva_masters,
	.masters_cnt	= ARRAY_SIZE(omap3xxx_iva_masters),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
};

static __initdata struct omap_hwmod *omap3xxx_hwmods[] = {
	&omap3xxx_l3_main_hwmod,
	&omap3xxx_l4_core_hwmod,
	&omap3xxx_l4_per_hwmod,
	&omap3xxx_l4_wkup_hwmod,
	&omap3xxx_mpu_hwmod,
	&omap3xxx_iva_hwmod,
	NULL,
};

int __init omap3xxx_hwmod_init(void)
{
	return omap_hwmod_init(omap3xxx_hwmods);
}


