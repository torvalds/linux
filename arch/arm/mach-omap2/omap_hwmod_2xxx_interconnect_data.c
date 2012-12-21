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

#include "omap_hwmod.h"
#include "l3_2xxx.h"
#include "l4_2xxx.h"
#include "serial.h"

#include "omap_hwmod_common_data.h"

static struct omap_hwmod_addr_space omap2xxx_uart1_addr_space[] = {
	{
		.pa_start	= OMAP2_UART1_BASE,
		.pa_end		= OMAP2_UART1_BASE + SZ_8K - 1,
		.flags		= ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_addr_space omap2xxx_uart2_addr_space[] = {
	{
		.pa_start	= OMAP2_UART2_BASE,
		.pa_end		= OMAP2_UART2_BASE + SZ_1K - 1,
		.flags		= ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_addr_space omap2xxx_uart3_addr_space[] = {
	{
		.pa_start	= OMAP2_UART3_BASE,
		.pa_end		= OMAP2_UART3_BASE + SZ_1K - 1,
		.flags		= ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_addr_space omap2xxx_timer2_addrs[] = {
	{
		.pa_start	= 0x4802a000,
		.pa_end		= 0x4802a000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_addr_space omap2xxx_timer3_addrs[] = {
	{
		.pa_start	= 0x48078000,
		.pa_end		= 0x48078000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_addr_space omap2xxx_timer4_addrs[] = {
	{
		.pa_start	= 0x4807a000,
		.pa_end		= 0x4807a000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_addr_space omap2xxx_timer5_addrs[] = {
	{
		.pa_start	= 0x4807c000,
		.pa_end		= 0x4807c000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_addr_space omap2xxx_timer6_addrs[] = {
	{
		.pa_start	= 0x4807e000,
		.pa_end		= 0x4807e000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_addr_space omap2xxx_timer7_addrs[] = {
	{
		.pa_start	= 0x48080000,
		.pa_end		= 0x48080000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_addr_space omap2xxx_timer8_addrs[] = {
	{
		.pa_start	= 0x48082000,
		.pa_end		= 0x48082000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_addr_space omap2xxx_timer9_addrs[] = {
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

static struct omap_hwmod_addr_space omap2_rng_addr_space[] = {
	{
		.pa_start	= 0x480a0000,
		.pa_end		= 0x480a004f,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_addr_space omap2xxx_sham_addrs[] = {
	{
		.pa_start	= 0x480a4000,
		.pa_end		= 0x480a4000 + 0x64 - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_addr_space omap2xxx_aes_addrs[] = {
	{
		.pa_start	= 0x480a6000,
		.pa_end		= 0x480a6000 + 0x50 - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/*
 * Common interconnect data
 */

/* L3 -> L4_CORE interface */
struct omap_hwmod_ocp_if omap2xxx_l3_main__l4_core = {
	.master	= &omap2xxx_l3_main_hwmod,
	.slave	= &omap2xxx_l4_core_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};

/* MPU -> L3 interface */
struct omap_hwmod_ocp_if omap2xxx_mpu__l3_main = {
	.master = &omap2xxx_mpu_hwmod,
	.slave	= &omap2xxx_l3_main_hwmod,
	.user	= OCP_USER_MPU,
};

/* DSS -> l3 */
struct omap_hwmod_ocp_if omap2xxx_dss__l3 = {
	.master		= &omap2xxx_dss_core_hwmod,
	.slave		= &omap2xxx_l3_main_hwmod,
	.fw = {
		.omap2 = {
			.l3_perm_bit  = OMAP2_L3_CORE_FW_CONNID_DSS,
			.flags	= OMAP_FIREWALL_L3,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4_CORE -> L4_WKUP interface */
struct omap_hwmod_ocp_if omap2xxx_l4_core__l4_wkup = {
	.master	= &omap2xxx_l4_core_hwmod,
	.slave	= &omap2xxx_l4_wkup_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> UART1 interface */
struct omap_hwmod_ocp_if omap2_l4_core__uart1 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_uart1_hwmod,
	.clk		= "uart1_ick",
	.addr		= omap2xxx_uart1_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> UART2 interface */
struct omap_hwmod_ocp_if omap2_l4_core__uart2 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_uart2_hwmod,
	.clk		= "uart2_ick",
	.addr		= omap2xxx_uart2_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 PER -> UART3 interface */
struct omap_hwmod_ocp_if omap2_l4_core__uart3 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_uart3_hwmod,
	.clk		= "uart3_ick",
	.addr		= omap2xxx_uart3_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi1 interface */
struct omap_hwmod_ocp_if omap2xxx_l4_core__mcspi1 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_mcspi1_hwmod,
	.clk		= "mcspi1_ick",
	.addr		= omap2_mcspi1_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi2 interface */
struct omap_hwmod_ocp_if omap2xxx_l4_core__mcspi2 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_mcspi2_hwmod,
	.clk		= "mcspi2_ick",
	.addr		= omap2_mcspi2_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> timer2 */
struct omap_hwmod_ocp_if omap2xxx_l4_core__timer2 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_timer2_hwmod,
	.clk		= "gpt2_ick",
	.addr		= omap2xxx_timer2_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> timer3 */
struct omap_hwmod_ocp_if omap2xxx_l4_core__timer3 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_timer3_hwmod,
	.clk		= "gpt3_ick",
	.addr		= omap2xxx_timer3_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> timer4 */
struct omap_hwmod_ocp_if omap2xxx_l4_core__timer4 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_timer4_hwmod,
	.clk		= "gpt4_ick",
	.addr		= omap2xxx_timer4_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> timer5 */
struct omap_hwmod_ocp_if omap2xxx_l4_core__timer5 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_timer5_hwmod,
	.clk		= "gpt5_ick",
	.addr		= omap2xxx_timer5_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> timer6 */
struct omap_hwmod_ocp_if omap2xxx_l4_core__timer6 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_timer6_hwmod,
	.clk		= "gpt6_ick",
	.addr		= omap2xxx_timer6_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> timer7 */
struct omap_hwmod_ocp_if omap2xxx_l4_core__timer7 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_timer7_hwmod,
	.clk		= "gpt7_ick",
	.addr		= omap2xxx_timer7_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> timer8 */
struct omap_hwmod_ocp_if omap2xxx_l4_core__timer8 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_timer8_hwmod,
	.clk		= "gpt8_ick",
	.addr		= omap2xxx_timer8_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> timer9 */
struct omap_hwmod_ocp_if omap2xxx_l4_core__timer9 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_timer9_hwmod,
	.clk		= "gpt9_ick",
	.addr		= omap2xxx_timer9_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> timer10 */
struct omap_hwmod_ocp_if omap2xxx_l4_core__timer10 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_timer10_hwmod,
	.clk		= "gpt10_ick",
	.addr		= omap2_timer10_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> timer11 */
struct omap_hwmod_ocp_if omap2xxx_l4_core__timer11 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_timer11_hwmod,
	.clk		= "gpt11_ick",
	.addr		= omap2_timer11_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> timer12 */
struct omap_hwmod_ocp_if omap2xxx_l4_core__timer12 = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_timer12_hwmod,
	.clk		= "gpt12_ick",
	.addr		= omap2xxx_timer12_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> dss */
struct omap_hwmod_ocp_if omap2xxx_l4_core__dss = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_dss_core_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2_dss_addrs,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP2420_L4_CORE_FW_DSS_CORE_REGION,
			.flags	= OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> dss_dispc */
struct omap_hwmod_ocp_if omap2xxx_l4_core__dss_dispc = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_dss_dispc_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2_dss_dispc_addrs,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP2420_L4_CORE_FW_DSS_DISPC_REGION,
			.flags	= OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> dss_rfbi */
struct omap_hwmod_ocp_if omap2xxx_l4_core__dss_rfbi = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_dss_rfbi_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2_dss_rfbi_addrs,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP2420_L4_CORE_FW_DSS_CORE_REGION,
			.flags	= OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> dss_venc */
struct omap_hwmod_ocp_if omap2xxx_l4_core__dss_venc = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_dss_venc_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2_dss_venc_addrs,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP2420_L4_CORE_FW_DSS_VENC_REGION,
			.flags	= OMAP_FIREWALL_L4,
		}
	},
	.flags		= OCPIF_SWSUP_IDLE,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> rng */
struct omap_hwmod_ocp_if omap2xxx_l4_core__rng = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_rng_hwmod,
	.clk		= "rng_ick",
	.addr		= omap2_rng_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> sham interface */
struct omap_hwmod_ocp_if omap2xxx_l4_core__sham = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_sham_hwmod,
	.clk		= "sha_ick",
	.addr		= omap2xxx_sham_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> aes interface */
struct omap_hwmod_ocp_if omap2xxx_l4_core__aes = {
	.master		= &omap2xxx_l4_core_hwmod,
	.slave		= &omap2xxx_aes_hwmod,
	.clk		= "aes_ick",
	.addr		= omap2xxx_aes_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};
