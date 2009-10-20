/*
 * omap_hwmod_2420.h - hardware modules present on the OMAP2420 chips
 *
 * Copyright (C) 2009 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * XXX handle crossbar/shared link difference for L3?
 *
 */
#ifndef __ARCH_ARM_PLAT_OMAP_INCLUDE_MACH_OMAP_HWMOD2420_H
#define __ARCH_ARM_PLAT_OMAP_INCLUDE_MACH_OMAP_HWMOD2420_H

#ifdef CONFIG_ARCH_OMAP2420

#include <plat/omap_hwmod.h>
#include <mach/irqs.h>
#include <plat/cpu.h>
#include <plat/dma.h>

#include "prm-regbits-24xx.h"

static struct omap_hwmod omap2420_mpu_hwmod;
static struct omap_hwmod omap2420_l3_hwmod;
static struct omap_hwmod omap2420_l4_core_hwmod;

/* L3 -> L4_CORE interface */
static struct omap_hwmod_ocp_if omap2420_l3__l4_core = {
	.master	= &omap2420_l3_hwmod,
	.slave	= &omap2420_l4_core_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};

/* MPU -> L3 interface */
static struct omap_hwmod_ocp_if omap2420_mpu__l3 = {
	.master = &omap2420_mpu_hwmod,
	.slave	= &omap2420_l3_hwmod,
	.user	= OCP_USER_MPU,
};

/* Slave interfaces on the L3 interconnect */
static struct omap_hwmod_ocp_if *omap2420_l3_slaves[] = {
	&omap2420_mpu__l3,
};

/* Master interfaces on the L3 interconnect */
static struct omap_hwmod_ocp_if *omap2420_l3_masters[] = {
	&omap2420_l3__l4_core,
};

/* L3 */
static struct omap_hwmod omap2420_l3_hwmod = {
	.name		= "l3_hwmod",
	.masters	= omap2420_l3_masters,
	.masters_cnt	= ARRAY_SIZE(omap2420_l3_masters),
	.slaves		= omap2420_l3_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2420_l3_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420)
};

static struct omap_hwmod omap2420_l4_wkup_hwmod;

/* L4_CORE -> L4_WKUP interface */
static struct omap_hwmod_ocp_if omap2420_l4_core__l4_wkup = {
	.master	= &omap2420_l4_core_hwmod,
	.slave	= &omap2420_l4_wkup_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};

/* Slave interfaces on the L4_CORE interconnect */
static struct omap_hwmod_ocp_if *omap2420_l4_core_slaves[] = {
	&omap2420_l3__l4_core,
};

/* Master interfaces on the L4_CORE interconnect */
static struct omap_hwmod_ocp_if *omap2420_l4_core_masters[] = {
	&omap2420_l4_core__l4_wkup,
};

/* L4 CORE */
static struct omap_hwmod omap2420_l4_core_hwmod = {
	.name		= "l4_core_hwmod",
	.masters	= omap2420_l4_core_masters,
	.masters_cnt	= ARRAY_SIZE(omap2420_l4_core_masters),
	.slaves		= omap2420_l4_core_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2420_l4_core_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420)
};

/* Slave interfaces on the L4_WKUP interconnect */
static struct omap_hwmod_ocp_if *omap2420_l4_wkup_slaves[] = {
	&omap2420_l4_core__l4_wkup,
};

/* Master interfaces on the L4_WKUP interconnect */
static struct omap_hwmod_ocp_if *omap2420_l4_wkup_masters[] = {
};

/* L4 WKUP */
static struct omap_hwmod omap2420_l4_wkup_hwmod = {
	.name		= "l4_wkup_hwmod",
	.masters	= omap2420_l4_wkup_masters,
	.masters_cnt	= ARRAY_SIZE(omap2420_l4_wkup_masters),
	.slaves		= omap2420_l4_wkup_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2420_l4_wkup_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420)
};

/* Master interfaces on the MPU device */
static struct omap_hwmod_ocp_if *omap2420_mpu_masters[] = {
	&omap2420_mpu__l3,
};

/* MPU */
static struct omap_hwmod omap2420_mpu_hwmod = {
	.name		= "mpu_hwmod",
	.clkdev_dev_id	= NULL,
	.clkdev_con_id	= "mpu_ck",
	.masters	= omap2420_mpu_masters,
	.masters_cnt	= ARRAY_SIZE(omap2420_mpu_masters),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static __initdata struct omap_hwmod *omap2420_hwmods[] = {
	&omap2420_l3_hwmod,
	&omap2420_l4_core_hwmod,
	&omap2420_l4_wkup_hwmod,
	&omap2420_mpu_hwmod,
	NULL,
};

#else
# define omap2420_hwmods		0
#endif

#endif


