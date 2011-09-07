/*
 * omap_hwmod_2xxx_interconnect_data.c - common interconnect data for OMAP2xxx
 *
 * Copyright (C) 2009-2011 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * XXX handle crossbar/shared link difference for L3?
 * XXX these should be marked initdata for multi-OMAP kernels
 */
#include <asm/sizes.h>

#include <plat/omap_hwmod.h>
#include <plat/serial.h>

#include "omap_hwmod_common_data.h"

struct omap_hwmod_addr_space omap2xxx_uart1_addr_space[] = {
	{
		.pa_start	= OMAP2_UART1_BASE,
		.pa_end		= OMAP2_UART1_BASE + SZ_8K - 1,
		.flags		= ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
	{ }
};

struct omap_hwmod_addr_space omap2xxx_uart2_addr_space[] = {
	{
		.pa_start	= OMAP2_UART2_BASE,
		.pa_end		= OMAP2_UART2_BASE + SZ_1K - 1,
		.flags		= ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
	{ }
};

struct omap_hwmod_addr_space omap2xxx_uart3_addr_space[] = {
	{
		.pa_start	= OMAP2_UART3_BASE,
		.pa_end		= OMAP2_UART3_BASE + SZ_1K - 1,
		.flags		= ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
	{ }
};

struct omap_hwmod_addr_space omap2xxx_timer2_addrs[] = {
	{
		.pa_start	= 0x4802a000,
		.pa_end		= 0x4802a000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

struct omap_hwmod_addr_space omap2xxx_timer3_addrs[] = {
	{
		.pa_start	= 0x48078000,
		.pa_end		= 0x48078000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

struct omap_hwmod_addr_space omap2xxx_timer4_addrs[] = {
	{
		.pa_start	= 0x4807a000,
		.pa_end		= 0x4807a000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

struct omap_hwmod_addr_space omap2xxx_timer5_addrs[] = {
	{
		.pa_start	= 0x4807c000,
		.pa_end		= 0x4807c000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

struct omap_hwmod_addr_space omap2xxx_timer6_addrs[] = {
	{
		.pa_start	= 0x4807e000,
		.pa_end		= 0x4807e000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

struct omap_hwmod_addr_space omap2xxx_timer7_addrs[] = {
	{
		.pa_start	= 0x48080000,
		.pa_end		= 0x48080000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

struct omap_hwmod_addr_space omap2xxx_timer8_addrs[] = {
	{
		.pa_start	= 0x48082000,
		.pa_end		= 0x48082000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

struct omap_hwmod_addr_space omap2xxx_timer9_addrs[] = {
	{
		.pa_start	= 0x48084000,
		.pa_end		= 0x48084000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

struct omap_hwmod_addr_space omap2xxx_mcbsp2_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x48076000,
		.pa_end		= 0x480760ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};


