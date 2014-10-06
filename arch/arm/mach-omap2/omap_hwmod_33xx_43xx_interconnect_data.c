/*
 *
 * Copyright (C) 2013 Texas Instruments Incorporated
 *
 * Interconnects common for AM335x and AM43x
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/sizes.h>
#include "omap_hwmod.h"
#include "omap_hwmod_33xx_43xx_common_data.h"

/* mpu -> l3 main */
struct omap_hwmod_ocp_if am33xx_mpu__l3_main = {
	.master		= &am33xx_mpu_hwmod,
	.slave		= &am33xx_l3_main_hwmod,
	.clk		= "dpll_mpu_m2_ck",
	.user		= OCP_USER_MPU,
};

/* l3 main -> l3 s */
struct omap_hwmod_ocp_if am33xx_l3_main__l3_s = {
	.master		= &am33xx_l3_main_hwmod,
	.slave		= &am33xx_l3_s_hwmod,
	.clk		= "l3s_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l3 s -> l4 per/ls */
struct omap_hwmod_ocp_if am33xx_l3_s__l4_ls = {
	.master		= &am33xx_l3_s_hwmod,
	.slave		= &am33xx_l4_ls_hwmod,
	.clk		= "l3s_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l3 s -> l4 wkup */
struct omap_hwmod_ocp_if am33xx_l3_s__l4_wkup = {
	.master		= &am33xx_l3_s_hwmod,
	.slave		= &am33xx_l4_wkup_hwmod,
	.clk		= "l3s_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l3 main -> l3 instr */
struct omap_hwmod_ocp_if am33xx_l3_main__l3_instr = {
	.master		= &am33xx_l3_main_hwmod,
	.slave		= &am33xx_l3_instr_hwmod,
	.clk		= "l3s_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mpu -> prcm */
struct omap_hwmod_ocp_if am33xx_mpu__prcm = {
	.master		= &am33xx_mpu_hwmod,
	.slave		= &am33xx_prcm_hwmod,
	.clk		= "dpll_mpu_m2_ck",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l3 s -> l3 main*/
struct omap_hwmod_ocp_if am33xx_l3_s__l3_main = {
	.master		= &am33xx_l3_s_hwmod,
	.slave		= &am33xx_l3_main_hwmod,
	.clk		= "l3s_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* pru-icss -> l3 main */
struct omap_hwmod_ocp_if am33xx_pruss__l3_main = {
	.master		= &am33xx_pruss_hwmod,
	.slave		= &am33xx_l3_main_hwmod,
	.clk		= "l3_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* gfx -> l3 main */
struct omap_hwmod_ocp_if am33xx_gfx__l3_main = {
	.master		= &am33xx_gfx_hwmod,
	.slave		= &am33xx_l3_main_hwmod,
	.clk		= "dpll_core_m4_ck",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l3 main -> gfx */
struct omap_hwmod_ocp_if am33xx_l3_main__gfx = {
	.master		= &am33xx_l3_main_hwmod,
	.slave		= &am33xx_gfx_hwmod,
	.clk		= "dpll_core_m4_ck",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 wkup -> rtc */
struct omap_hwmod_ocp_if am33xx_l4_wkup__rtc = {
	.master		= &am33xx_l4_wkup_hwmod,
	.slave		= &am33xx_rtc_hwmod,
	.clk		= "clkdiv32k_ick",
	.user		= OCP_USER_MPU,
};

/* l4 per/ls -> DCAN0 */
struct omap_hwmod_ocp_if am33xx_l4_per__dcan0 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_dcan0_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 per/ls -> DCAN1 */
struct omap_hwmod_ocp_if am33xx_l4_per__dcan1 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_dcan1_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 per/ls -> GPIO2 */
struct omap_hwmod_ocp_if am33xx_l4_per__gpio1 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_gpio1_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 per/ls -> gpio3 */
struct omap_hwmod_ocp_if am33xx_l4_per__gpio2 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_gpio2_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 per/ls -> gpio4 */
struct omap_hwmod_ocp_if am33xx_l4_per__gpio3 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_gpio3_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

struct omap_hwmod_ocp_if am33xx_cpgmac0__mdio = {
	.master		= &am33xx_cpgmac0_hwmod,
	.slave		= &am33xx_mdio_hwmod,
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_addr_space am33xx_elm_addr_space[] = {
	{
		.pa_start	= 0x48080000,
		.pa_end		= 0x48080000 + SZ_8K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

struct omap_hwmod_ocp_if am33xx_l4_ls__elm = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_elm_hwmod,
	.clk		= "l4ls_gclk",
	.addr		= am33xx_elm_addr_space,
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_addr_space am33xx_epwmss0_addr_space[] = {
	{
		.pa_start	= 0x48300000,
		.pa_end		= 0x48300000 + SZ_16 - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

struct omap_hwmod_ocp_if am33xx_l4_ls__epwmss0 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_epwmss0_hwmod,
	.clk		= "l4ls_gclk",
	.addr		= am33xx_epwmss0_addr_space,
	.user		= OCP_USER_MPU,
};

struct omap_hwmod_ocp_if am33xx_epwmss0__ecap0 = {
	.master		= &am33xx_epwmss0_hwmod,
	.slave		= &am33xx_ecap0_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

struct omap_hwmod_ocp_if am33xx_epwmss0__eqep0 = {
	.master		= &am33xx_epwmss0_hwmod,
	.slave		= &am33xx_eqep0_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

struct omap_hwmod_ocp_if am33xx_epwmss0__ehrpwm0 = {
	.master		= &am33xx_epwmss0_hwmod,
	.slave		= &am33xx_ehrpwm0_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};


static struct omap_hwmod_addr_space am33xx_epwmss1_addr_space[] = {
	{
		.pa_start	= 0x48302000,
		.pa_end		= 0x48302000 + SZ_16 - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

struct omap_hwmod_ocp_if am33xx_l4_ls__epwmss1 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_epwmss1_hwmod,
	.clk		= "l4ls_gclk",
	.addr		= am33xx_epwmss1_addr_space,
	.user		= OCP_USER_MPU,
};

struct omap_hwmod_ocp_if am33xx_epwmss1__ecap1 = {
	.master		= &am33xx_epwmss1_hwmod,
	.slave		= &am33xx_ecap1_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

struct omap_hwmod_ocp_if am33xx_epwmss1__eqep1 = {
	.master		= &am33xx_epwmss1_hwmod,
	.slave		= &am33xx_eqep1_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

struct omap_hwmod_ocp_if am33xx_epwmss1__ehrpwm1 = {
	.master		= &am33xx_epwmss1_hwmod,
	.slave		= &am33xx_ehrpwm1_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_addr_space am33xx_epwmss2_addr_space[] = {
	{
		.pa_start	= 0x48304000,
		.pa_end		= 0x48304000 + SZ_16 - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

struct omap_hwmod_ocp_if am33xx_l4_ls__epwmss2 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_epwmss2_hwmod,
	.clk		= "l4ls_gclk",
	.addr		= am33xx_epwmss2_addr_space,
	.user		= OCP_USER_MPU,
};

struct omap_hwmod_ocp_if am33xx_epwmss2__ecap2 = {
	.master		= &am33xx_epwmss2_hwmod,
	.slave		= &am33xx_ecap2_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

struct omap_hwmod_ocp_if am33xx_epwmss2__eqep2 = {
	.master		= &am33xx_epwmss2_hwmod,
	.slave		= &am33xx_eqep2_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

struct omap_hwmod_ocp_if am33xx_epwmss2__ehrpwm2 = {
	.master		= &am33xx_epwmss2_hwmod,
	.slave		= &am33xx_ehrpwm2_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

/* l3s cfg -> gpmc */
static struct omap_hwmod_addr_space am33xx_gpmc_addr_space[] = {
	{
		.pa_start	= 0x50000000,
		.pa_end		= 0x50000000 + SZ_8K - 1,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

struct omap_hwmod_ocp_if am33xx_l3_s__gpmc = {
	.master		= &am33xx_l3_s_hwmod,
	.slave		= &am33xx_gpmc_hwmod,
	.clk		= "l3s_gclk",
	.addr		= am33xx_gpmc_addr_space,
	.user		= OCP_USER_MPU,
};

/* i2c2 */
struct omap_hwmod_ocp_if am33xx_l4_per__i2c2 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_i2c2_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

struct omap_hwmod_ocp_if am33xx_l4_per__i2c3 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_i2c3_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

/* l4 ls -> mailbox */
struct omap_hwmod_ocp_if am33xx_l4_per__mailbox = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_mailbox_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

/* l4 ls -> spinlock */
struct omap_hwmod_ocp_if am33xx_l4_ls__spinlock = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_spinlock_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

/* l4 ls -> mcasp0 */
static struct omap_hwmod_addr_space am33xx_mcasp0_addr_space[] = {
	{
		.pa_start	= 0x48038000,
		.pa_end		= 0x48038000 + SZ_8K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

struct omap_hwmod_ocp_if am33xx_l4_ls__mcasp0 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_mcasp0_hwmod,
	.clk		= "l4ls_gclk",
	.addr		= am33xx_mcasp0_addr_space,
	.user		= OCP_USER_MPU,
};

/* l4 ls -> mcasp1 */
static struct omap_hwmod_addr_space am33xx_mcasp1_addr_space[] = {
	{
		.pa_start	= 0x4803C000,
		.pa_end		= 0x4803C000 + SZ_8K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

struct omap_hwmod_ocp_if am33xx_l4_ls__mcasp1 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_mcasp1_hwmod,
	.clk		= "l4ls_gclk",
	.addr		= am33xx_mcasp1_addr_space,
	.user		= OCP_USER_MPU,
};

/* l4 ls -> mmc0 */
static struct omap_hwmod_addr_space am33xx_mmc0_addr_space[] = {
	{
		.pa_start	= 0x48060100,
		.pa_end		= 0x48060100 + SZ_4K - 1,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

struct omap_hwmod_ocp_if am33xx_l4_ls__mmc0 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_mmc0_hwmod,
	.clk		= "l4ls_gclk",
	.addr		= am33xx_mmc0_addr_space,
	.user		= OCP_USER_MPU,
};

/* l4 ls -> mmc1 */
static struct omap_hwmod_addr_space am33xx_mmc1_addr_space[] = {
	{
		.pa_start	= 0x481d8100,
		.pa_end		= 0x481d8100 + SZ_4K - 1,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

struct omap_hwmod_ocp_if am33xx_l4_ls__mmc1 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_mmc1_hwmod,
	.clk		= "l4ls_gclk",
	.addr		= am33xx_mmc1_addr_space,
	.user		= OCP_USER_MPU,
};

/* l3 s -> mmc2 */
static struct omap_hwmod_addr_space am33xx_mmc2_addr_space[] = {
	{
		.pa_start	= 0x47810100,
		.pa_end		= 0x47810100 + SZ_64K - 1,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

struct omap_hwmod_ocp_if am33xx_l3_s__mmc2 = {
	.master		= &am33xx_l3_s_hwmod,
	.slave		= &am33xx_mmc2_hwmod,
	.clk		= "l3s_gclk",
	.addr		= am33xx_mmc2_addr_space,
	.user		= OCP_USER_MPU,
};

/* l4 ls -> mcspi0 */
struct omap_hwmod_ocp_if am33xx_l4_ls__mcspi0 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_spi0_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

/* l4 ls -> mcspi1 */
struct omap_hwmod_ocp_if am33xx_l4_ls__mcspi1 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_spi1_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

/* l4 per -> timer2 */
struct omap_hwmod_ocp_if am33xx_l4_ls__timer2 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_timer2_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

/* l4 per -> timer3 */
struct omap_hwmod_ocp_if am33xx_l4_ls__timer3 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_timer3_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

/* l4 per -> timer4 */
struct omap_hwmod_ocp_if am33xx_l4_ls__timer4 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_timer4_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

/* l4 per -> timer5 */
struct omap_hwmod_ocp_if am33xx_l4_ls__timer5 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_timer5_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

/* l4 per -> timer6 */
struct omap_hwmod_ocp_if am33xx_l4_ls__timer6 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_timer6_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

/* l4 per -> timer7 */
struct omap_hwmod_ocp_if am33xx_l4_ls__timer7 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_timer7_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

/* l3 main -> tpcc */
struct omap_hwmod_ocp_if am33xx_l3_main__tpcc = {
	.master		= &am33xx_l3_main_hwmod,
	.slave		= &am33xx_tpcc_hwmod,
	.clk		= "l3_gclk",
	.user		= OCP_USER_MPU,
};

/* l3 main -> tpcc0 */
static struct omap_hwmod_addr_space am33xx_tptc0_addr_space[] = {
	{
		.pa_start	= 0x49800000,
		.pa_end		= 0x49800000 + SZ_8K - 1,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

struct omap_hwmod_ocp_if am33xx_l3_main__tptc0 = {
	.master		= &am33xx_l3_main_hwmod,
	.slave		= &am33xx_tptc0_hwmod,
	.clk		= "l3_gclk",
	.addr		= am33xx_tptc0_addr_space,
	.user		= OCP_USER_MPU,
};

/* l3 main -> tpcc1 */
static struct omap_hwmod_addr_space am33xx_tptc1_addr_space[] = {
	{
		.pa_start	= 0x49900000,
		.pa_end		= 0x49900000 + SZ_8K - 1,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

struct omap_hwmod_ocp_if am33xx_l3_main__tptc1 = {
	.master		= &am33xx_l3_main_hwmod,
	.slave		= &am33xx_tptc1_hwmod,
	.clk		= "l3_gclk",
	.addr		= am33xx_tptc1_addr_space,
	.user		= OCP_USER_MPU,
};

/* l3 main -> tpcc2 */
static struct omap_hwmod_addr_space am33xx_tptc2_addr_space[] = {
	{
		.pa_start	= 0x49a00000,
		.pa_end		= 0x49a00000 + SZ_8K - 1,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

struct omap_hwmod_ocp_if am33xx_l3_main__tptc2 = {
	.master		= &am33xx_l3_main_hwmod,
	.slave		= &am33xx_tptc2_hwmod,
	.clk		= "l3_gclk",
	.addr		= am33xx_tptc2_addr_space,
	.user		= OCP_USER_MPU,
};

/* l4 ls -> uart2 */
struct omap_hwmod_ocp_if am33xx_l4_ls__uart2 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_uart2_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

/* l4 ls -> uart3 */
struct omap_hwmod_ocp_if am33xx_l4_ls__uart3 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_uart3_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

/* l4 ls -> uart4 */
struct omap_hwmod_ocp_if am33xx_l4_ls__uart4 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_uart4_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

/* l4 ls -> uart5 */
struct omap_hwmod_ocp_if am33xx_l4_ls__uart5 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_uart5_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

/* l4 ls -> uart6 */
struct omap_hwmod_ocp_if am33xx_l4_ls__uart6 = {
	.master		= &am33xx_l4_ls_hwmod,
	.slave		= &am33xx_uart6_hwmod,
	.clk		= "l4ls_gclk",
	.user		= OCP_USER_MPU,
};

/* l3 main -> ocmc */
struct omap_hwmod_ocp_if am33xx_l3_main__ocmc = {
	.master		= &am33xx_l3_main_hwmod,
	.slave		= &am33xx_ocmcram_hwmod,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l3 main -> sha0 HIB2 */
static struct omap_hwmod_addr_space am33xx_sha0_addrs[] = {
	{
		.pa_start	= 0x53100000,
		.pa_end		= 0x53100000 + SZ_512 - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

struct omap_hwmod_ocp_if am33xx_l3_main__sha0 = {
	.master		= &am33xx_l3_main_hwmod,
	.slave		= &am33xx_sha0_hwmod,
	.clk		= "sha0_fck",
	.addr		= am33xx_sha0_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l3 main -> AES0 HIB2 */
static struct omap_hwmod_addr_space am33xx_aes0_addrs[] = {
	{
		.pa_start	= 0x53500000,
		.pa_end		= 0x53500000 + SZ_1M - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

struct omap_hwmod_ocp_if am33xx_l3_main__aes0 = {
	.master		= &am33xx_l3_main_hwmod,
	.slave		= &am33xx_aes0_hwmod,
	.clk		= "aes0_fck",
	.addr		= am33xx_aes0_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};
