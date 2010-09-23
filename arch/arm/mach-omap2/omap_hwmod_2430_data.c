/*
 * omap_hwmod_2430_data.c - hardware modules present on the OMAP2430 chips
 *
 * Copyright (C) 2009-2010 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * XXX handle crossbar/shared link difference for L3?
 * XXX these should be marked initdata for multi-OMAP kernels
 */
#include <plat/omap_hwmod.h>
#include <mach/irqs.h>
#include <plat/cpu.h>
#include <plat/dma.h>

#include "omap_hwmod_common_data.h"

#include "prm-regbits-24xx.h"
#include "cm-regbits-24xx.h"

/*
 * OMAP2430 hardware module integration data
 *
 * ALl of the data in this section should be autogeneratable from the
 * TI hardware database or other technical documentation.  Data that
 * is driver-specific or driver-kernel integration-specific belongs
 * elsewhere.
 */

static struct omap_hwmod omap2430_mpu_hwmod;
static struct omap_hwmod omap2430_iva_hwmod;
static struct omap_hwmod omap2430_l3_main_hwmod;
static struct omap_hwmod omap2430_l4_core_hwmod;
static struct omap_hwmod omap2430_wd_timer2_hwmod;

/* L3 -> L4_CORE interface */
static struct omap_hwmod_ocp_if omap2430_l3_main__l4_core = {
	.master	= &omap2430_l3_main_hwmod,
	.slave	= &omap2430_l4_core_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};

/* MPU -> L3 interface */
static struct omap_hwmod_ocp_if omap2430_mpu__l3_main = {
	.master = &omap2430_mpu_hwmod,
	.slave	= &omap2430_l3_main_hwmod,
	.user	= OCP_USER_MPU,
};

/* Slave interfaces on the L3 interconnect */
static struct omap_hwmod_ocp_if *omap2430_l3_main_slaves[] = {
	&omap2430_mpu__l3_main,
};

/* Master interfaces on the L3 interconnect */
static struct omap_hwmod_ocp_if *omap2430_l3_main_masters[] = {
	&omap2430_l3_main__l4_core,
};

/* L3 */
static struct omap_hwmod omap2430_l3_main_hwmod = {
	.name		= "l3_main",
	.class		= &l3_hwmod_class,
	.masters	= omap2430_l3_main_masters,
	.masters_cnt	= ARRAY_SIZE(omap2430_l3_main_masters),
	.slaves		= omap2430_l3_main_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_l3_main_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
	.flags		= HWMOD_NO_IDLEST,
};

static struct omap_hwmod omap2430_l4_wkup_hwmod;

/* L4_CORE -> L4_WKUP interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__l4_wkup = {
	.master	= &omap2430_l4_core_hwmod,
	.slave	= &omap2430_l4_wkup_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};

/* Slave interfaces on the L4_CORE interconnect */
static struct omap_hwmod_ocp_if *omap2430_l4_core_slaves[] = {
	&omap2430_l3_main__l4_core,
};

/* Master interfaces on the L4_CORE interconnect */
static struct omap_hwmod_ocp_if *omap2430_l4_core_masters[] = {
	&omap2430_l4_core__l4_wkup,
};

/* L4 CORE */
static struct omap_hwmod omap2430_l4_core_hwmod = {
	.name		= "l4_core",
	.class		= &l4_hwmod_class,
	.masters	= omap2430_l4_core_masters,
	.masters_cnt	= ARRAY_SIZE(omap2430_l4_core_masters),
	.slaves		= omap2430_l4_core_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_l4_core_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
	.flags		= HWMOD_NO_IDLEST,
};

/* Slave interfaces on the L4_WKUP interconnect */
static struct omap_hwmod_ocp_if *omap2430_l4_wkup_slaves[] = {
	&omap2430_l4_core__l4_wkup,
};

/* Master interfaces on the L4_WKUP interconnect */
static struct omap_hwmod_ocp_if *omap2430_l4_wkup_masters[] = {
};

/* L4 WKUP */
static struct omap_hwmod omap2430_l4_wkup_hwmod = {
	.name		= "l4_wkup",
	.class		= &l4_hwmod_class,
	.masters	= omap2430_l4_wkup_masters,
	.masters_cnt	= ARRAY_SIZE(omap2430_l4_wkup_masters),
	.slaves		= omap2430_l4_wkup_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_l4_wkup_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
	.flags		= HWMOD_NO_IDLEST,
};

/* Master interfaces on the MPU device */
static struct omap_hwmod_ocp_if *omap2430_mpu_masters[] = {
	&omap2430_mpu__l3_main,
};

/* MPU */
static struct omap_hwmod omap2430_mpu_hwmod = {
	.name		= "mpu",
	.class		= &mpu_hwmod_class,
	.main_clk	= "mpu_ck",
	.masters	= omap2430_mpu_masters,
	.masters_cnt	= ARRAY_SIZE(omap2430_mpu_masters),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/*
 * IVA2_1 interface data
 */

/* IVA2 <- L3 interface */
static struct omap_hwmod_ocp_if omap2430_l3__iva = {
	.master		= &omap2430_l3_main_hwmod,
	.slave		= &omap2430_iva_hwmod,
	.clk		= "dsp_fck",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if *omap2430_iva_masters[] = {
	&omap2430_l3__iva,
};

/*
 * IVA2 (IVA2)
 */

static struct omap_hwmod omap2430_iva_hwmod = {
	.name		= "iva",
	.class		= &iva_hwmod_class,
	.masters	= omap2430_iva_masters,
	.masters_cnt	= ARRAY_SIZE(omap2430_iva_masters),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430)
};

/* l4_wkup -> wd_timer2 */
static struct omap_hwmod_addr_space omap2430_wd_timer2_addrs[] = {
	{
		.pa_start	= 0x49016000,
		.pa_end		= 0x4901607f,
		.flags		= ADDR_TYPE_RT
	},
};

static struct omap_hwmod_ocp_if omap2430_l4_wkup__wd_timer2 = {
	.master		= &omap2430_l4_wkup_hwmod,
	.slave		= &omap2430_wd_timer2_hwmod,
	.clk		= "mpu_wdt_ick",
	.addr		= omap2430_wd_timer2_addrs,
	.addr_cnt	= ARRAY_SIZE(omap2430_wd_timer2_addrs),
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/*
 * 'wd_timer' class
 * 32-bit watchdog upward counter that generates a pulse on the reset pin on
 * overflow condition
 */

static struct omap_hwmod_class_sysconfig omap2430_wd_timer_sysc = {
	.rev_offs	= 0x0,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_EMUFREE | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap2430_wd_timer_hwmod_class = {
	.name = "wd_timer",
	.sysc = &omap2430_wd_timer_sysc,
};

/* wd_timer2 */
static struct omap_hwmod_ocp_if *omap2430_wd_timer2_slaves[] = {
	&omap2430_l4_wkup__wd_timer2,
};

static struct omap_hwmod omap2430_wd_timer2_hwmod = {
	.name		= "wd_timer2",
	.class		= &omap2430_wd_timer_hwmod_class,
	.main_clk	= "mpu_wdt_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_MPU_WDT_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MPU_WDT_SHIFT,
		},
	},
	.slaves		= omap2430_wd_timer2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_wd_timer2_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static __initdata struct omap_hwmod *omap2430_hwmods[] = {
	&omap2430_l3_main_hwmod,
	&omap2430_l4_core_hwmod,
	&omap2430_l4_wkup_hwmod,
	&omap2430_mpu_hwmod,
	&omap2430_iva_hwmod,
	&omap2430_wd_timer2_hwmod,
	NULL,
};

int __init omap2430_hwmod_init(void)
{
	return omap_hwmod_init(omap2430_hwmods);
}


