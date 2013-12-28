/*
 * omap_hwmod_3xxx_data.c - hardware modules present on the OMAP3xxx chips
 *
 * Copyright (C) 2009-2011 Nokia Corporation
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
#include <plat/serial.h>
#include <plat/l3_3xxx.h>
#include <plat/l4_3xxx.h>
#include <plat/i2c.h>
#include <plat/gpio.h>
#include <plat/mmc.h>
#include <plat/mcbsp.h>
#include <plat/mcspi.h>
#include <plat/dmtimer.h>

#include "omap_hwmod_common_data.h"

#include "smartreflex.h"
#include "prm-regbits-34xx.h"
#include "cm-regbits-34xx.h"
#include "wd_timer.h"
#include <mach/am35xx.h>

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
static struct omap_hwmod omap3xxx_wd_timer2_hwmod;
static struct omap_hwmod omap3430es1_dss_core_hwmod;
static struct omap_hwmod omap3xxx_dss_core_hwmod;
static struct omap_hwmod omap3xxx_dss_dispc_hwmod;
static struct omap_hwmod omap3xxx_dss_dsi1_hwmod;
static struct omap_hwmod omap3xxx_dss_rfbi_hwmod;
static struct omap_hwmod omap3xxx_dss_venc_hwmod;
static struct omap_hwmod omap3xxx_i2c1_hwmod;
static struct omap_hwmod omap3xxx_i2c2_hwmod;
static struct omap_hwmod omap3xxx_i2c3_hwmod;
static struct omap_hwmod omap3xxx_gpio1_hwmod;
static struct omap_hwmod omap3xxx_gpio2_hwmod;
static struct omap_hwmod omap3xxx_gpio3_hwmod;
static struct omap_hwmod omap3xxx_gpio4_hwmod;
static struct omap_hwmod omap3xxx_gpio5_hwmod;
static struct omap_hwmod omap3xxx_gpio6_hwmod;
static struct omap_hwmod omap34xx_sr1_hwmod;
static struct omap_hwmod omap34xx_sr2_hwmod;
static struct omap_hwmod omap34xx_mcspi1;
static struct omap_hwmod omap34xx_mcspi2;
static struct omap_hwmod omap34xx_mcspi3;
static struct omap_hwmod omap34xx_mcspi4;
static struct omap_hwmod omap3xxx_mmc1_hwmod;
static struct omap_hwmod omap3xxx_mmc2_hwmod;
static struct omap_hwmod omap3xxx_mmc3_hwmod;
static struct omap_hwmod am35xx_usbhsotg_hwmod;

static struct omap_hwmod omap3xxx_dma_system_hwmod;

static struct omap_hwmod omap3xxx_mcbsp1_hwmod;
static struct omap_hwmod omap3xxx_mcbsp2_hwmod;
static struct omap_hwmod omap3xxx_mcbsp3_hwmod;
static struct omap_hwmod omap3xxx_mcbsp4_hwmod;
static struct omap_hwmod omap3xxx_mcbsp5_hwmod;
static struct omap_hwmod omap3xxx_mcbsp2_sidetone_hwmod;
static struct omap_hwmod omap3xxx_mcbsp3_sidetone_hwmod;
static struct omap_hwmod omap3xxx_usb_host_hs_hwmod;
static struct omap_hwmod omap3xxx_usb_tll_hs_hwmod;

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

/* L3 taret configuration and error log registers */
static struct omap_hwmod_irq_info omap3xxx_l3_main_irqs[] = {
	{ .irq = INT_34XX_L3_DBG_IRQ },
	{ .irq = INT_34XX_L3_APP_IRQ },
	{ .irq = -1 }
};

static struct omap_hwmod_addr_space omap3xxx_l3_main_addrs[] = {
	{
		.pa_start       = 0x68000000,
		.pa_end         = 0x6800ffff,
		.flags          = ADDR_TYPE_RT,
	},
	{ }
};

/* MPU -> L3 interface */
static struct omap_hwmod_ocp_if omap3xxx_mpu__l3_main = {
	.master   = &omap3xxx_mpu_hwmod,
	.slave    = &omap3xxx_l3_main_hwmod,
	.addr     = omap3xxx_l3_main_addrs,
	.user	= OCP_USER_MPU,
};

/* Slave interfaces on the L3 interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l3_main_slaves[] = {
	&omap3xxx_mpu__l3_main,
};

/* DSS -> l3 */
static struct omap_hwmod_ocp_if omap3xxx_dss__l3 = {
	.master		= &omap3xxx_dss_core_hwmod,
	.slave		= &omap3xxx_l3_main_hwmod,
	.fw = {
		.omap2 = {
			.l3_perm_bit  = OMAP3_L3_CORE_FW_INIT_ID_DSS,
			.flags	= OMAP_FIREWALL_L3,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
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
	.mpu_irqs	= omap3xxx_l3_main_irqs,
	.masters	= omap3xxx_l3_main_masters,
	.masters_cnt	= ARRAY_SIZE(omap3xxx_l3_main_masters),
	.slaves		= omap3xxx_l3_main_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_l3_main_slaves),
	.flags		= HWMOD_NO_IDLEST,
};

static struct omap_hwmod omap3xxx_l4_wkup_hwmod;
static struct omap_hwmod omap3xxx_uart1_hwmod;
static struct omap_hwmod omap3xxx_uart2_hwmod;
static struct omap_hwmod omap3xxx_uart3_hwmod;
static struct omap_hwmod omap3xxx_uart4_hwmod;
static struct omap_hwmod am35xx_uart4_hwmod;
static struct omap_hwmod omap3xxx_usbhsotg_hwmod;

/* l3_core -> usbhsotg interface */
static struct omap_hwmod_ocp_if omap3xxx_usbhsotg__l3 = {
	.master		= &omap3xxx_usbhsotg_hwmod,
	.slave		= &omap3xxx_l3_main_hwmod,
	.clk		= "core_l3_ick",
	.user		= OCP_USER_MPU,
};

/* l3_core -> am35xx_usbhsotg interface */
static struct omap_hwmod_ocp_if am35xx_usbhsotg__l3 = {
	.master		= &am35xx_usbhsotg_hwmod,
	.slave		= &omap3xxx_l3_main_hwmod,
	.clk		= "core_l3_ick",
	.user		= OCP_USER_MPU,
};
/* L4_CORE -> L4_WKUP interface */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__l4_wkup = {
	.master	= &omap3xxx_l4_core_hwmod,
	.slave	= &omap3xxx_l4_wkup_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> MMC1 interface */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__mmc1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_mmc1_hwmod,
	.clk		= "mmchs1_ick",
	.addr		= omap2430_mmc1_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
	.flags		= OMAP_FIREWALL_L4
};

/* L4 CORE -> MMC2 interface */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__mmc2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_mmc2_hwmod,
	.clk		= "mmchs2_ick",
	.addr		= omap2430_mmc2_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
	.flags		= OMAP_FIREWALL_L4
};

/* L4 CORE -> MMC3 interface */
static struct omap_hwmod_addr_space omap3xxx_mmc3_addr_space[] = {
	{
		.pa_start	= 0x480ad000,
		.pa_end		= 0x480ad1ff,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_core__mmc3 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_mmc3_hwmod,
	.clk		= "mmchs3_ick",
	.addr		= omap3xxx_mmc3_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
	.flags		= OMAP_FIREWALL_L4
};

/* L4 CORE -> UART1 interface */
static struct omap_hwmod_addr_space omap3xxx_uart1_addr_space[] = {
	{
		.pa_start	= OMAP3_UART1_BASE,
		.pa_end		= OMAP3_UART1_BASE + SZ_8K - 1,
		.flags		= ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3_l4_core__uart1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_uart1_hwmod,
	.clk		= "uart1_ick",
	.addr		= omap3xxx_uart1_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> UART2 interface */
static struct omap_hwmod_addr_space omap3xxx_uart2_addr_space[] = {
	{
		.pa_start	= OMAP3_UART2_BASE,
		.pa_end		= OMAP3_UART2_BASE + SZ_1K - 1,
		.flags		= ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3_l4_core__uart2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_uart2_hwmod,
	.clk		= "uart2_ick",
	.addr		= omap3xxx_uart2_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 PER -> UART3 interface */
static struct omap_hwmod_addr_space omap3xxx_uart3_addr_space[] = {
	{
		.pa_start	= OMAP3_UART3_BASE,
		.pa_end		= OMAP3_UART3_BASE + SZ_1K - 1,
		.flags		= ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3_l4_per__uart3 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_uart3_hwmod,
	.clk		= "uart3_ick",
	.addr		= omap3xxx_uart3_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 PER -> UART4 interface */
static struct omap_hwmod_addr_space omap3xxx_uart4_addr_space[] = {
	{
		.pa_start	= OMAP3_UART4_BASE,
		.pa_end		= OMAP3_UART4_BASE + SZ_1K - 1,
		.flags		= ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3_l4_per__uart4 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_uart4_hwmod,
	.clk		= "uart4_ick",
	.addr		= omap3xxx_uart4_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* AM35xx: L4 CORE -> UART4 interface */
static struct omap_hwmod_addr_space am35xx_uart4_addr_space[] = {
	{
		.pa_start       = OMAP3_UART4_AM35XX_BASE,
		.pa_end         = OMAP3_UART4_AM35XX_BASE + SZ_1K - 1,
		.flags          = ADDR_MAP_ON_INIT | ADDR_TYPE_RT,
	},
};

static struct omap_hwmod_ocp_if am35xx_l4_core__uart4 = {
	.master         = &omap3xxx_l4_core_hwmod,
	.slave          = &am35xx_uart4_hwmod,
	.clk            = "uart4_ick",
	.addr           = am35xx_uart4_addr_space,
	.user           = OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> I2C1 interface */
static struct omap_hwmod_ocp_if omap3_l4_core__i2c1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_i2c1_hwmod,
	.clk		= "i2c1_ick",
	.addr		= omap2_i2c1_addr_space,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_I2C1_REGION,
			.l4_prot_group = 7,
			.flags	= OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> I2C2 interface */
static struct omap_hwmod_ocp_if omap3_l4_core__i2c2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_i2c2_hwmod,
	.clk		= "i2c2_ick",
	.addr		= omap2_i2c2_addr_space,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_I2C2_REGION,
			.l4_prot_group = 7,
			.flags = OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> I2C3 interface */
static struct omap_hwmod_addr_space omap3xxx_i2c3_addr_space[] = {
	{
		.pa_start	= 0x48060000,
		.pa_end		= 0x48060000 + SZ_128 - 1,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3_l4_core__i2c3 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_i2c3_hwmod,
	.clk		= "i2c3_ick",
	.addr		= omap3xxx_i2c3_addr_space,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_I2C3_REGION,
			.l4_prot_group = 7,
			.flags = OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_irq_info omap3_smartreflex_mpu_irqs[] = {
	{ .irq = 18},
	{ .irq = -1 }
};

static struct omap_hwmod_irq_info omap3_smartreflex_core_irqs[] = {
	{ .irq = 19},
	{ .irq = -1 }
};

/* L4 CORE -> SR1 interface */
static struct omap_hwmod_addr_space omap3_sr1_addr_space[] = {
	{
		.pa_start	= OMAP34XX_SR1_BASE,
		.pa_end		= OMAP34XX_SR1_BASE + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3_l4_core__sr1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_sr1_hwmod,
	.clk		= "sr_l4_ick",
	.addr		= omap3_sr1_addr_space,
	.user		= OCP_USER_MPU,
};

/* L4 CORE -> SR1 interface */
static struct omap_hwmod_addr_space omap3_sr2_addr_space[] = {
	{
		.pa_start	= OMAP34XX_SR2_BASE,
		.pa_end		= OMAP34XX_SR2_BASE + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3_l4_core__sr2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_sr2_hwmod,
	.clk		= "sr_l4_ick",
	.addr		= omap3_sr2_addr_space,
	.user		= OCP_USER_MPU,
};

/*
* usbhsotg interface data
*/

static struct omap_hwmod_addr_space omap3xxx_usbhsotg_addrs[] = {
	{
		.pa_start	= OMAP34XX_HSUSB_OTG_BASE,
		.pa_end		= OMAP34XX_HSUSB_OTG_BASE + SZ_4K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_core -> usbhsotg  */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__usbhsotg = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_usbhsotg_hwmod,
	.clk		= "l4_ick",
	.addr		= omap3xxx_usbhsotg_addrs,
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if *omap3xxx_usbhsotg_masters[] = {
	&omap3xxx_usbhsotg__l3,
};

static struct omap_hwmod_ocp_if *omap3xxx_usbhsotg_slaves[] = {
	&omap3xxx_l4_core__usbhsotg,
};

static struct omap_hwmod_addr_space am35xx_usbhsotg_addrs[] = {
	{
		.pa_start	= AM35XX_IPSS_USBOTGSS_BASE,
		.pa_end		= AM35XX_IPSS_USBOTGSS_BASE + SZ_4K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_core -> usbhsotg  */
static struct omap_hwmod_ocp_if am35xx_l4_core__usbhsotg = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &am35xx_usbhsotg_hwmod,
	.clk		= "l4_ick",
	.addr		= am35xx_usbhsotg_addrs,
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if *am35xx_usbhsotg_masters[] = {
	&am35xx_usbhsotg__l3,
};

static struct omap_hwmod_ocp_if *am35xx_usbhsotg_slaves[] = {
	&am35xx_l4_core__usbhsotg,
};
/* Slave interfaces on the L4_CORE interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l4_core_slaves[] = {
	&omap3xxx_l3_main__l4_core,
};

/* L4 CORE */
static struct omap_hwmod omap3xxx_l4_core_hwmod = {
	.name		= "l4_core",
	.class		= &l4_hwmod_class,
	.slaves		= omap3xxx_l4_core_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_l4_core_slaves),
	.flags		= HWMOD_NO_IDLEST,
};

/* Slave interfaces on the L4_PER interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l4_per_slaves[] = {
	&omap3xxx_l3_main__l4_per,
};

/* L4 PER */
static struct omap_hwmod omap3xxx_l4_per_hwmod = {
	.name		= "l4_per",
	.class		= &l4_hwmod_class,
	.slaves		= omap3xxx_l4_per_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_l4_per_slaves),
	.flags		= HWMOD_NO_IDLEST,
};

/* Slave interfaces on the L4_WKUP interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l4_wkup_slaves[] = {
	&omap3xxx_l4_core__l4_wkup,
};

/* L4 WKUP */
static struct omap_hwmod omap3xxx_l4_wkup_hwmod = {
	.name		= "l4_wkup",
	.class		= &l4_hwmod_class,
	.slaves		= omap3xxx_l4_wkup_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_l4_wkup_slaves),
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
};

/* timer class */
static struct omap_hwmod_class_sysconfig omap3xxx_timer_1ms_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_CLOCKACTIVITY |
				SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
				SYSC_HAS_EMUFREE | SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_timer_1ms_hwmod_class = {
	.name = "timer",
	.sysc = &omap3xxx_timer_1ms_sysc,
	.rev = OMAP_TIMER_IP_VERSION_1,
};

static struct omap_hwmod_class_sysconfig omap3xxx_timer_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_ENAWAKEUP |
			   SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_timer_hwmod_class = {
	.name = "timer",
	.sysc = &omap3xxx_timer_sysc,
	.rev =  OMAP_TIMER_IP_VERSION_1,
};

/* secure timers dev attribute */
static struct omap_timer_capability_dev_attr capability_secure_dev_attr = {
	.timer_capability       = OMAP_TIMER_SECURE,
};

/* always-on timers dev attribute */
static struct omap_timer_capability_dev_attr capability_alwon_dev_attr = {
	.timer_capability       = OMAP_TIMER_ALWON,
};

/* pwm timers dev attribute */
static struct omap_timer_capability_dev_attr capability_pwm_dev_attr = {
	.timer_capability       = OMAP_TIMER_HAS_PWM,
};

/* timer1 */
static struct omap_hwmod omap3xxx_timer1_hwmod;

static struct omap_hwmod_addr_space omap3xxx_timer1_addrs[] = {
	{
		.pa_start	= 0x48318000,
		.pa_end		= 0x48318000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_wkup -> timer1 */
static struct omap_hwmod_ocp_if omap3xxx_l4_wkup__timer1 = {
	.master		= &omap3xxx_l4_wkup_hwmod,
	.slave		= &omap3xxx_timer1_hwmod,
	.clk		= "gpt1_ick",
	.addr		= omap3xxx_timer1_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer1 slave port */
static struct omap_hwmod_ocp_if *omap3xxx_timer1_slaves[] = {
	&omap3xxx_l4_wkup__timer1,
};

/* timer1 hwmod */
static struct omap_hwmod omap3xxx_timer1_hwmod = {
	.name		= "timer1",
	.mpu_irqs	= omap2_timer1_mpu_irqs,
	.main_clk	= "gpt1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT1_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT1_SHIFT,
		},
	},
	.dev_attr	= &capability_alwon_dev_attr,
	.slaves		= omap3xxx_timer1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_timer1_slaves),
	.class		= &omap3xxx_timer_1ms_hwmod_class,
};

/* timer2 */
static struct omap_hwmod omap3xxx_timer2_hwmod;

static struct omap_hwmod_addr_space omap3xxx_timer2_addrs[] = {
	{
		.pa_start	= 0x49032000,
		.pa_end		= 0x49032000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> timer2 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer2 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer2_hwmod,
	.clk		= "gpt2_ick",
	.addr		= omap3xxx_timer2_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer2 slave port */
static struct omap_hwmod_ocp_if *omap3xxx_timer2_slaves[] = {
	&omap3xxx_l4_per__timer2,
};

/* timer2 hwmod */
static struct omap_hwmod omap3xxx_timer2_hwmod = {
	.name		= "timer2",
	.mpu_irqs	= omap2_timer2_mpu_irqs,
	.main_clk	= "gpt2_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT2_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT2_SHIFT,
		},
	},
	.dev_attr	= &capability_alwon_dev_attr,
	.slaves		= omap3xxx_timer2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_timer2_slaves),
	.class		= &omap3xxx_timer_1ms_hwmod_class,
};

/* timer3 */
static struct omap_hwmod omap3xxx_timer3_hwmod;

static struct omap_hwmod_addr_space omap3xxx_timer3_addrs[] = {
	{
		.pa_start	= 0x49034000,
		.pa_end		= 0x49034000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> timer3 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer3 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer3_hwmod,
	.clk		= "gpt3_ick",
	.addr		= omap3xxx_timer3_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer3 slave port */
static struct omap_hwmod_ocp_if *omap3xxx_timer3_slaves[] = {
	&omap3xxx_l4_per__timer3,
};

/* timer3 hwmod */
static struct omap_hwmod omap3xxx_timer3_hwmod = {
	.name		= "timer3",
	.mpu_irqs	= omap2_timer3_mpu_irqs,
	.main_clk	= "gpt3_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT3_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT3_SHIFT,
		},
	},
	.dev_attr	= &capability_alwon_dev_attr,
	.slaves		= omap3xxx_timer3_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_timer3_slaves),
	.class		= &omap3xxx_timer_hwmod_class,
};

/* timer4 */
static struct omap_hwmod omap3xxx_timer4_hwmod;

static struct omap_hwmod_addr_space omap3xxx_timer4_addrs[] = {
	{
		.pa_start	= 0x49036000,
		.pa_end		= 0x49036000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> timer4 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer4 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer4_hwmod,
	.clk		= "gpt4_ick",
	.addr		= omap3xxx_timer4_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer4 slave port */
static struct omap_hwmod_ocp_if *omap3xxx_timer4_slaves[] = {
	&omap3xxx_l4_per__timer4,
};

/* timer4 hwmod */
static struct omap_hwmod omap3xxx_timer4_hwmod = {
	.name		= "timer4",
	.mpu_irqs	= omap2_timer4_mpu_irqs,
	.main_clk	= "gpt4_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT4_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT4_SHIFT,
		},
	},
	.dev_attr	= &capability_alwon_dev_attr,
	.slaves		= omap3xxx_timer4_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_timer4_slaves),
	.class		= &omap3xxx_timer_hwmod_class,
};

/* timer5 */
static struct omap_hwmod omap3xxx_timer5_hwmod;

static struct omap_hwmod_addr_space omap3xxx_timer5_addrs[] = {
	{
		.pa_start	= 0x49038000,
		.pa_end		= 0x49038000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> timer5 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer5 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer5_hwmod,
	.clk		= "gpt5_ick",
	.addr		= omap3xxx_timer5_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer5 slave port */
static struct omap_hwmod_ocp_if *omap3xxx_timer5_slaves[] = {
	&omap3xxx_l4_per__timer5,
};

/* timer5 hwmod */
static struct omap_hwmod omap3xxx_timer5_hwmod = {
	.name		= "timer5",
	.mpu_irqs	= omap2_timer5_mpu_irqs,
	.main_clk	= "gpt5_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT5_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT5_SHIFT,
		},
	},
	.dev_attr	= &capability_alwon_dev_attr,
	.slaves		= omap3xxx_timer5_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_timer5_slaves),
	.class		= &omap3xxx_timer_hwmod_class,
};

/* timer6 */
static struct omap_hwmod omap3xxx_timer6_hwmod;

static struct omap_hwmod_addr_space omap3xxx_timer6_addrs[] = {
	{
		.pa_start	= 0x4903A000,
		.pa_end		= 0x4903A000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> timer6 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer6 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer6_hwmod,
	.clk		= "gpt6_ick",
	.addr		= omap3xxx_timer6_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer6 slave port */
static struct omap_hwmod_ocp_if *omap3xxx_timer6_slaves[] = {
	&omap3xxx_l4_per__timer6,
};

/* timer6 hwmod */
static struct omap_hwmod omap3xxx_timer6_hwmod = {
	.name		= "timer6",
	.mpu_irqs	= omap2_timer6_mpu_irqs,
	.main_clk	= "gpt6_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT6_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT6_SHIFT,
		},
	},
	.dev_attr	= &capability_alwon_dev_attr,
	.slaves		= omap3xxx_timer6_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_timer6_slaves),
	.class		= &omap3xxx_timer_hwmod_class,
};

/* timer7 */
static struct omap_hwmod omap3xxx_timer7_hwmod;

static struct omap_hwmod_addr_space omap3xxx_timer7_addrs[] = {
	{
		.pa_start	= 0x4903C000,
		.pa_end		= 0x4903C000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> timer7 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer7 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer7_hwmod,
	.clk		= "gpt7_ick",
	.addr		= omap3xxx_timer7_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer7 slave port */
static struct omap_hwmod_ocp_if *omap3xxx_timer7_slaves[] = {
	&omap3xxx_l4_per__timer7,
};

/* timer7 hwmod */
static struct omap_hwmod omap3xxx_timer7_hwmod = {
	.name		= "timer7",
	.mpu_irqs	= omap2_timer7_mpu_irqs,
	.main_clk	= "gpt7_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT7_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT7_SHIFT,
		},
	},
	.dev_attr	= &capability_alwon_dev_attr,
	.slaves		= omap3xxx_timer7_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_timer7_slaves),
	.class		= &omap3xxx_timer_hwmod_class,
};

/* timer8 */
static struct omap_hwmod omap3xxx_timer8_hwmod;

static struct omap_hwmod_addr_space omap3xxx_timer8_addrs[] = {
	{
		.pa_start	= 0x4903E000,
		.pa_end		= 0x4903E000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> timer8 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer8 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer8_hwmod,
	.clk		= "gpt8_ick",
	.addr		= omap3xxx_timer8_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer8 slave port */
static struct omap_hwmod_ocp_if *omap3xxx_timer8_slaves[] = {
	&omap3xxx_l4_per__timer8,
};

/* timer8 hwmod */
static struct omap_hwmod omap3xxx_timer8_hwmod = {
	.name		= "timer8",
	.mpu_irqs	= omap2_timer8_mpu_irqs,
	.main_clk	= "gpt8_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT8_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT8_SHIFT,
		},
	},
	.dev_attr	= &capability_pwm_dev_attr,
	.slaves		= omap3xxx_timer8_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_timer8_slaves),
	.class		= &omap3xxx_timer_hwmod_class,
};

/* timer9 */
static struct omap_hwmod omap3xxx_timer9_hwmod;

static struct omap_hwmod_addr_space omap3xxx_timer9_addrs[] = {
	{
		.pa_start	= 0x49040000,
		.pa_end		= 0x49040000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> timer9 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__timer9 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_timer9_hwmod,
	.clk		= "gpt9_ick",
	.addr		= omap3xxx_timer9_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer9 slave port */
static struct omap_hwmod_ocp_if *omap3xxx_timer9_slaves[] = {
	&omap3xxx_l4_per__timer9,
};

/* timer9 hwmod */
static struct omap_hwmod omap3xxx_timer9_hwmod = {
	.name		= "timer9",
	.mpu_irqs	= omap2_timer9_mpu_irqs,
	.main_clk	= "gpt9_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT9_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT9_SHIFT,
		},
	},
	.dev_attr	= &capability_pwm_dev_attr,
	.slaves		= omap3xxx_timer9_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_timer9_slaves),
	.class		= &omap3xxx_timer_hwmod_class,
};

/* timer10 */
static struct omap_hwmod omap3xxx_timer10_hwmod;

/* l4_core -> timer10 */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__timer10 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_timer10_hwmod,
	.clk		= "gpt10_ick",
	.addr		= omap2_timer10_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer10 slave port */
static struct omap_hwmod_ocp_if *omap3xxx_timer10_slaves[] = {
	&omap3xxx_l4_core__timer10,
};

/* timer10 hwmod */
static struct omap_hwmod omap3xxx_timer10_hwmod = {
	.name		= "timer10",
	.mpu_irqs	= omap2_timer10_mpu_irqs,
	.main_clk	= "gpt10_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT10_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT10_SHIFT,
		},
	},
	.dev_attr	= &capability_pwm_dev_attr,
	.slaves		= omap3xxx_timer10_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_timer10_slaves),
	.class		= &omap3xxx_timer_1ms_hwmod_class,
};

/* timer11 */
static struct omap_hwmod omap3xxx_timer11_hwmod;

/* l4_core -> timer11 */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__timer11 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_timer11_hwmod,
	.clk		= "gpt11_ick",
	.addr		= omap2_timer11_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer11 slave port */
static struct omap_hwmod_ocp_if *omap3xxx_timer11_slaves[] = {
	&omap3xxx_l4_core__timer11,
};

/* timer11 hwmod */
static struct omap_hwmod omap3xxx_timer11_hwmod = {
	.name		= "timer11",
	.mpu_irqs	= omap2_timer11_mpu_irqs,
	.main_clk	= "gpt11_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT11_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT11_SHIFT,
		},
	},
	.dev_attr	= &capability_pwm_dev_attr,
	.slaves		= omap3xxx_timer11_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_timer11_slaves),
	.class		= &omap3xxx_timer_hwmod_class,
};

/* timer12*/
static struct omap_hwmod omap3xxx_timer12_hwmod;
static struct omap_hwmod_irq_info omap3xxx_timer12_mpu_irqs[] = {
	{ .irq = 95, },
	{ .irq = -1 }
};

static struct omap_hwmod_addr_space omap3xxx_timer12_addrs[] = {
	{
		.pa_start	= 0x48304000,
		.pa_end		= 0x48304000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_core -> timer12 */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__timer12 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_timer12_hwmod,
	.clk		= "gpt12_ick",
	.addr		= omap3xxx_timer12_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer12 slave port */
static struct omap_hwmod_ocp_if *omap3xxx_timer12_slaves[] = {
	&omap3xxx_l4_core__timer12,
};

/* timer12 hwmod */
static struct omap_hwmod omap3xxx_timer12_hwmod = {
	.name		= "timer12",
	.mpu_irqs	= omap3xxx_timer12_mpu_irqs,
	.main_clk	= "gpt12_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPT12_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPT12_SHIFT,
		},
	},
	.dev_attr	= &capability_secure_dev_attr,
	.slaves		= omap3xxx_timer12_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_timer12_slaves),
	.class		= &omap3xxx_timer_hwmod_class,
};

/* l4_wkup -> wd_timer2 */
static struct omap_hwmod_addr_space omap3xxx_wd_timer2_addrs[] = {
	{
		.pa_start	= 0x48314000,
		.pa_end		= 0x4831407f,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_wkup__wd_timer2 = {
	.master		= &omap3xxx_l4_wkup_hwmod,
	.slave		= &omap3xxx_wd_timer2_hwmod,
	.clk		= "wdt2_ick",
	.addr		= omap3xxx_wd_timer2_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/*
 * 'wd_timer' class
 * 32-bit watchdog upward counter that generates a pulse on the reset pin on
 * overflow condition
 */

static struct omap_hwmod_class_sysconfig omap3xxx_wd_timer_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_EMUFREE |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE | SYSC_HAS_CLOCKACTIVITY |
			   SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

/* I2C common */
static struct omap_hwmod_class_sysconfig i2c_sysc = {
	.rev_offs	= 0x00,
	.sysc_offs	= 0x20,
	.syss_offs	= 0x10,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.clockact	= CLOCKACT_TEST_ICLK,
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_wd_timer_hwmod_class = {
	.name		= "wd_timer",
	.sysc		= &omap3xxx_wd_timer_sysc,
	.pre_shutdown	= &omap2_wd_timer_disable
};

/* wd_timer2 */
static struct omap_hwmod_ocp_if *omap3xxx_wd_timer2_slaves[] = {
	&omap3xxx_l4_wkup__wd_timer2,
};

static struct omap_hwmod omap3xxx_wd_timer2_hwmod = {
	.name		= "wd_timer2",
	.class		= &omap3xxx_wd_timer_hwmod_class,
	.main_clk	= "wdt2_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_WDT2_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_WDT2_SHIFT,
		},
	},
	.slaves		= omap3xxx_wd_timer2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_wd_timer2_slaves),
	/*
	 * XXX: Use software supervised mode, HW supervised smartidle seems to
	 * block CORE power domain idle transitions. Maybe a HW bug in wdt2?
	 */
	.flags		= HWMOD_SWSUP_SIDLE,
};

/* UART1 */

static struct omap_hwmod_ocp_if *omap3xxx_uart1_slaves[] = {
	&omap3_l4_core__uart1,
};

static struct omap_hwmod omap3xxx_uart1_hwmod = {
	.name		= "uart1",
	.mpu_irqs	= omap2_uart1_mpu_irqs,
	.sdma_reqs	= omap2_uart1_sdma_reqs,
	.main_clk	= "uart1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_UART1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_UART1_SHIFT,
		},
	},
	.slaves		= omap3xxx_uart1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_uart1_slaves),
	.class		= &omap2_uart_class,
};

/* UART2 */

static struct omap_hwmod_ocp_if *omap3xxx_uart2_slaves[] = {
	&omap3_l4_core__uart2,
};

static struct omap_hwmod omap3xxx_uart2_hwmod = {
	.name		= "uart2",
	.mpu_irqs	= omap2_uart2_mpu_irqs,
	.sdma_reqs	= omap2_uart2_sdma_reqs,
	.main_clk	= "uart2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_UART2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_UART2_SHIFT,
		},
	},
	.slaves		= omap3xxx_uart2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_uart2_slaves),
	.class		= &omap2_uart_class,
};

/* UART3 */

static struct omap_hwmod_ocp_if *omap3xxx_uart3_slaves[] = {
	&omap3_l4_per__uart3,
};

static struct omap_hwmod omap3xxx_uart3_hwmod = {
	.name		= "uart3",
	.mpu_irqs	= omap2_uart3_mpu_irqs,
	.sdma_reqs	= omap2_uart3_sdma_reqs,
	.main_clk	= "uart3_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_UART3_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_UART3_SHIFT,
		},
	},
	.slaves		= omap3xxx_uart3_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_uart3_slaves),
	.class		= &omap2_uart_class,
};

/* UART4 */

static struct omap_hwmod_irq_info uart4_mpu_irqs[] = {
	{ .irq = INT_36XX_UART4_IRQ, },
	{ .irq = -1 }
};

static struct omap_hwmod_dma_info uart4_sdma_reqs[] = {
	{ .name = "rx",	.dma_req = OMAP36XX_DMA_UART4_RX, },
	{ .name = "tx",	.dma_req = OMAP36XX_DMA_UART4_TX, },
	{ .dma_req = -1 }
};

static struct omap_hwmod_ocp_if *omap3xxx_uart4_slaves[] = {
	&omap3_l4_per__uart4,
};

static struct omap_hwmod omap3xxx_uart4_hwmod = {
	.name		= "uart4",
	.mpu_irqs	= uart4_mpu_irqs,
	.sdma_reqs	= uart4_sdma_reqs,
	.main_clk	= "uart4_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = OMAP3430_PER_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3630_EN_UART4_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3630_EN_UART4_SHIFT,
		},
	},
	.slaves		= omap3xxx_uart4_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_uart4_slaves),
	.class		= &omap2_uart_class,
};

static struct omap_hwmod_irq_info am35xx_uart4_mpu_irqs[] = {
	{ .irq = INT_35XX_UART4_IRQ, },
};

static struct omap_hwmod_dma_info am35xx_uart4_sdma_reqs[] = {
	{ .name = "rx", .dma_req = AM35XX_DMA_UART4_RX, },
	{ .name = "tx", .dma_req = AM35XX_DMA_UART4_TX, },
};

static struct omap_hwmod_ocp_if *am35xx_uart4_slaves[] = {
	&am35xx_l4_core__uart4,
};

static struct omap_hwmod am35xx_uart4_hwmod = {
	.name           = "uart4",
	.mpu_irqs       = am35xx_uart4_mpu_irqs,
	.sdma_reqs      = am35xx_uart4_sdma_reqs,
	.main_clk       = "uart4_fck",
	.prcm           = {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_UART4_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_UART4_SHIFT,
		},
	},
	.slaves         = am35xx_uart4_slaves,
	.slaves_cnt     = ARRAY_SIZE(am35xx_uart4_slaves),
	.class          = &omap2_uart_class,
};


static struct omap_hwmod_class i2c_class = {
	.name	= "i2c",
	.sysc	= &i2c_sysc,
	.rev	= OMAP_I2C_IP_VERSION_1,
	.reset	= &omap_i2c_reset,
};

static struct omap_hwmod_dma_info omap3xxx_dss_sdma_chs[] = {
	{ .name = "dispc", .dma_req = 5 },
	{ .name = "dsi1", .dma_req = 74 },
	{ .dma_req = -1 }
};

/* dss */
/* dss master ports */
static struct omap_hwmod_ocp_if *omap3xxx_dss_masters[] = {
	&omap3xxx_dss__l3,
};

/* l4_core -> dss */
static struct omap_hwmod_ocp_if omap3430es1_l4_core__dss = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3430es1_dss_core_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2_dss_addrs,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3ES1_L4_CORE_FW_DSS_CORE_REGION,
			.l4_prot_group = OMAP3_L4_CORE_FW_DSS_PROT_GROUP,
			.flags	= OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if omap3xxx_l4_core__dss = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_dss_core_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2_dss_addrs,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_DSS_CORE_REGION,
			.l4_prot_group = OMAP3_L4_CORE_FW_DSS_PROT_GROUP,
			.flags	= OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dss slave ports */
static struct omap_hwmod_ocp_if *omap3430es1_dss_slaves[] = {
	&omap3430es1_l4_core__dss,
};

static struct omap_hwmod_ocp_if *omap3xxx_dss_slaves[] = {
	&omap3xxx_l4_core__dss,
};

static struct omap_hwmod_opt_clk dss_opt_clks[] = {
	/*
	 * The DSS HW needs all DSS clocks enabled during reset. The dss_core
	 * driver does not use these clocks.
	 */
	{ .role = "sys_clk", .clk = "dss2_alwon_fck" },
	{ .role = "tv_clk", .clk = "dss_tv_fck" },
	/* required only on OMAP3430 */
	{ .role = "tv_dac_clk", .clk = "dss_96m_fck" },
};

static struct omap_hwmod omap3430es1_dss_core_hwmod = {
	.name		= "dss_core",
	.class		= &omap2_dss_hwmod_class,
	.main_clk	= "dss1_alwon_fck", /* instead of dss_fck */
	.sdma_reqs	= omap3xxx_dss_sdma_chs,
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_DSS1_SHIFT,
			.module_offs = OMAP3430_DSS_MOD,
			.idlest_reg_id = 1,
			.idlest_stdby_bit = OMAP3430ES1_ST_DSS_SHIFT,
		},
	},
	.opt_clks	= dss_opt_clks,
	.opt_clks_cnt = ARRAY_SIZE(dss_opt_clks),
	.slaves		= omap3430es1_dss_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3430es1_dss_slaves),
	.masters	= omap3xxx_dss_masters,
	.masters_cnt	= ARRAY_SIZE(omap3xxx_dss_masters),
	.flags		= HWMOD_NO_IDLEST | HWMOD_CONTROL_OPT_CLKS_IN_RESET,
};

static struct omap_hwmod omap3xxx_dss_core_hwmod = {
	.name		= "dss_core",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.class		= &omap2_dss_hwmod_class,
	.main_clk	= "dss1_alwon_fck", /* instead of dss_fck */
	.sdma_reqs	= omap3xxx_dss_sdma_chs,
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_DSS1_SHIFT,
			.module_offs = OMAP3430_DSS_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430ES2_ST_DSS_IDLE_SHIFT,
			.idlest_stdby_bit = OMAP3430ES2_ST_DSS_STDBY_SHIFT,
		},
	},
	.opt_clks	= dss_opt_clks,
	.opt_clks_cnt = ARRAY_SIZE(dss_opt_clks),
	.slaves		= omap3xxx_dss_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_dss_slaves),
	.masters	= omap3xxx_dss_masters,
	.masters_cnt	= ARRAY_SIZE(omap3xxx_dss_masters),
};

/*
 * 'dispc' class
 * display controller
 */

static struct omap_hwmod_class_sysconfig omap3_dispc_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_MIDLEMODE |
			   SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE |
			   SYSC_HAS_ENAWAKEUP),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			   MSTANDBY_FORCE | MSTANDBY_NO | MSTANDBY_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3_dispc_hwmod_class = {
	.name	= "dispc",
	.sysc	= &omap3_dispc_sysc,
};

/* l4_core -> dss_dispc */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__dss_dispc = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_dss_dispc_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2_dss_dispc_addrs,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_DSS_DISPC_REGION,
			.l4_prot_group = OMAP3_L4_CORE_FW_DSS_PROT_GROUP,
			.flags	= OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dss_dispc slave ports */
static struct omap_hwmod_ocp_if *omap3xxx_dss_dispc_slaves[] = {
	&omap3xxx_l4_core__dss_dispc,
};

static struct omap_hwmod omap3xxx_dss_dispc_hwmod = {
	.name		= "dss_dispc",
	.class		= &omap3_dispc_hwmod_class,
	.mpu_irqs	= omap2_dispc_irqs,
	.main_clk	= "dss1_alwon_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_DSS1_SHIFT,
			.module_offs = OMAP3430_DSS_MOD,
		},
	},
	.slaves		= omap3xxx_dss_dispc_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_dss_dispc_slaves),
	.flags		= HWMOD_NO_IDLEST,
	.dev_attr	= &omap2_3_dss_dispc_dev_attr
};

/*
 * 'dsi' class
 * display serial interface controller
 */

static struct omap_hwmod_class omap3xxx_dsi_hwmod_class = {
	.name = "dsi",
};

static struct omap_hwmod_irq_info omap3xxx_dsi1_irqs[] = {
	{ .irq = 25 },
	{ .irq = -1 }
};

/* dss_dsi1 */
static struct omap_hwmod_addr_space omap3xxx_dss_dsi1_addrs[] = {
	{
		.pa_start	= 0x4804FC00,
		.pa_end		= 0x4804FFFF,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_core -> dss_dsi1 */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__dss_dsi1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_dss_dsi1_hwmod,
	.clk		= "dss_ick",
	.addr		= omap3xxx_dss_dsi1_addrs,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_DSS_DSI_REGION,
			.l4_prot_group = OMAP3_L4_CORE_FW_DSS_PROT_GROUP,
			.flags	= OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dss_dsi1 slave ports */
static struct omap_hwmod_ocp_if *omap3xxx_dss_dsi1_slaves[] = {
	&omap3xxx_l4_core__dss_dsi1,
};

static struct omap_hwmod_opt_clk dss_dsi1_opt_clks[] = {
	{ .role = "sys_clk", .clk = "dss2_alwon_fck" },
};

static struct omap_hwmod omap3xxx_dss_dsi1_hwmod = {
	.name		= "dss_dsi1",
	.class		= &omap3xxx_dsi_hwmod_class,
	.mpu_irqs	= omap3xxx_dsi1_irqs,
	.main_clk	= "dss1_alwon_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_DSS1_SHIFT,
			.module_offs = OMAP3430_DSS_MOD,
		},
	},
	.opt_clks	= dss_dsi1_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(dss_dsi1_opt_clks),
	.slaves		= omap3xxx_dss_dsi1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_dss_dsi1_slaves),
	.flags		= HWMOD_NO_IDLEST,
};

/* l4_core -> dss_rfbi */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__dss_rfbi = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_dss_rfbi_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2_dss_rfbi_addrs,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_DSS_RFBI_REGION,
			.l4_prot_group = OMAP3_L4_CORE_FW_DSS_PROT_GROUP ,
			.flags	= OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dss_rfbi slave ports */
static struct omap_hwmod_ocp_if *omap3xxx_dss_rfbi_slaves[] = {
	&omap3xxx_l4_core__dss_rfbi,
};

static struct omap_hwmod_opt_clk dss_rfbi_opt_clks[] = {
	{ .role = "ick", .clk = "dss_ick" },
};

static struct omap_hwmod omap3xxx_dss_rfbi_hwmod = {
	.name		= "dss_rfbi",
	.class		= &omap2_rfbi_hwmod_class,
	.main_clk	= "dss1_alwon_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_DSS1_SHIFT,
			.module_offs = OMAP3430_DSS_MOD,
		},
	},
	.opt_clks	= dss_rfbi_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(dss_rfbi_opt_clks),
	.slaves		= omap3xxx_dss_rfbi_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_dss_rfbi_slaves),
	.flags		= HWMOD_NO_IDLEST,
};

/* l4_core -> dss_venc */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__dss_venc = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_dss_venc_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2_dss_venc_addrs,
	.fw = {
		.omap2 = {
			.l4_fw_region  = OMAP3_L4_CORE_FW_DSS_VENC_REGION,
			.l4_prot_group = OMAP3_L4_CORE_FW_DSS_PROT_GROUP,
			.flags	= OMAP_FIREWALL_L4,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dss_venc slave ports */
static struct omap_hwmod_ocp_if *omap3xxx_dss_venc_slaves[] = {
	&omap3xxx_l4_core__dss_venc,
};

static struct omap_hwmod_opt_clk dss_venc_opt_clks[] = {
	/* required only on OMAP3430 */
	{ .role = "tv_dac_clk", .clk = "dss_96m_fck" },
};

static struct omap_hwmod omap3xxx_dss_venc_hwmod = {
	.name		= "dss_venc",
	.class		= &omap2_venc_hwmod_class,
	.main_clk	= "dss_tv_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_DSS1_SHIFT,
			.module_offs = OMAP3430_DSS_MOD,
		},
	},
	.opt_clks	= dss_venc_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(dss_venc_opt_clks),
	.slaves		= omap3xxx_dss_venc_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_dss_venc_slaves),
	.flags		= HWMOD_NO_IDLEST,
};

/* I2C1 */

static struct omap_i2c_dev_attr i2c1_dev_attr = {
	.fifo_depth	= 8, /* bytes */
	.flags		= OMAP_I2C_FLAG_APPLY_ERRATA_I207 |
			  OMAP_I2C_FLAG_RESET_REGS_POSTIDLE |
			  OMAP_I2C_FLAG_BUS_SHIFT_2,
};

static struct omap_hwmod_ocp_if *omap3xxx_i2c1_slaves[] = {
	&omap3_l4_core__i2c1,
};

static struct omap_hwmod omap3xxx_i2c1_hwmod = {
	.name		= "i2c1",
	.flags		= HWMOD_16BIT_REG | HWMOD_SET_DEFAULT_CLOCKACT,
	.mpu_irqs	= omap2_i2c1_mpu_irqs,
	.sdma_reqs	= omap2_i2c1_sdma_reqs,
	.main_clk	= "i2c1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_I2C1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_I2C1_SHIFT,
		},
	},
	.slaves		= omap3xxx_i2c1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_i2c1_slaves),
	.class		= &i2c_class,
	.dev_attr	= &i2c1_dev_attr,
};

/* I2C2 */

static struct omap_i2c_dev_attr i2c2_dev_attr = {
	.fifo_depth	= 8, /* bytes */
	.flags = OMAP_I2C_FLAG_APPLY_ERRATA_I207 |
		 OMAP_I2C_FLAG_RESET_REGS_POSTIDLE |
		 OMAP_I2C_FLAG_BUS_SHIFT_2,
};

static struct omap_hwmod_ocp_if *omap3xxx_i2c2_slaves[] = {
	&omap3_l4_core__i2c2,
};

static struct omap_hwmod omap3xxx_i2c2_hwmod = {
	.name		= "i2c2",
	.flags		= HWMOD_16BIT_REG | HWMOD_SET_DEFAULT_CLOCKACT,
	.mpu_irqs	= omap2_i2c2_mpu_irqs,
	.sdma_reqs	= omap2_i2c2_sdma_reqs,
	.main_clk	= "i2c2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_I2C2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_I2C2_SHIFT,
		},
	},
	.slaves		= omap3xxx_i2c2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_i2c2_slaves),
	.class		= &i2c_class,
	.dev_attr	= &i2c2_dev_attr,
};

/* I2C3 */

static struct omap_i2c_dev_attr i2c3_dev_attr = {
	.fifo_depth	= 64, /* bytes */
	.flags = OMAP_I2C_FLAG_APPLY_ERRATA_I207 |
		 OMAP_I2C_FLAG_RESET_REGS_POSTIDLE |
		 OMAP_I2C_FLAG_BUS_SHIFT_2,
};

static struct omap_hwmod_irq_info i2c3_mpu_irqs[] = {
	{ .irq = INT_34XX_I2C3_IRQ, },
	{ .irq = -1 }
};

static struct omap_hwmod_dma_info i2c3_sdma_reqs[] = {
	{ .name = "tx", .dma_req = OMAP34XX_DMA_I2C3_TX },
	{ .name = "rx", .dma_req = OMAP34XX_DMA_I2C3_RX },
	{ .dma_req = -1 }
};

static struct omap_hwmod_ocp_if *omap3xxx_i2c3_slaves[] = {
	&omap3_l4_core__i2c3,
};

static struct omap_hwmod omap3xxx_i2c3_hwmod = {
	.name		= "i2c3",
	.flags		= HWMOD_16BIT_REG | HWMOD_SET_DEFAULT_CLOCKACT,
	.mpu_irqs	= i2c3_mpu_irqs,
	.sdma_reqs	= i2c3_sdma_reqs,
	.main_clk	= "i2c3_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_I2C3_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_I2C3_SHIFT,
		},
	},
	.slaves		= omap3xxx_i2c3_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_i2c3_slaves),
	.class		= &i2c_class,
	.dev_attr	= &i2c3_dev_attr,
};

/* l4_wkup -> gpio1 */
static struct omap_hwmod_addr_space omap3xxx_gpio1_addrs[] = {
	{
		.pa_start	= 0x48310000,
		.pa_end		= 0x483101ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_wkup__gpio1 = {
	.master		= &omap3xxx_l4_wkup_hwmod,
	.slave		= &omap3xxx_gpio1_hwmod,
	.addr		= omap3xxx_gpio1_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_per -> gpio2 */
static struct omap_hwmod_addr_space omap3xxx_gpio2_addrs[] = {
	{
		.pa_start	= 0x49050000,
		.pa_end		= 0x490501ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_per__gpio2 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_gpio2_hwmod,
	.addr		= omap3xxx_gpio2_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_per -> gpio3 */
static struct omap_hwmod_addr_space omap3xxx_gpio3_addrs[] = {
	{
		.pa_start	= 0x49052000,
		.pa_end		= 0x490521ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_per__gpio3 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_gpio3_hwmod,
	.addr		= omap3xxx_gpio3_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_per -> gpio4 */
static struct omap_hwmod_addr_space omap3xxx_gpio4_addrs[] = {
	{
		.pa_start	= 0x49054000,
		.pa_end		= 0x490541ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_per__gpio4 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_gpio4_hwmod,
	.addr		= omap3xxx_gpio4_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_per -> gpio5 */
static struct omap_hwmod_addr_space omap3xxx_gpio5_addrs[] = {
	{
		.pa_start	= 0x49056000,
		.pa_end		= 0x490561ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_per__gpio5 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_gpio5_hwmod,
	.addr		= omap3xxx_gpio5_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_per -> gpio6 */
static struct omap_hwmod_addr_space omap3xxx_gpio6_addrs[] = {
	{
		.pa_start	= 0x49058000,
		.pa_end		= 0x490581ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap3xxx_l4_per__gpio6 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_gpio6_hwmod,
	.addr		= omap3xxx_gpio6_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/*
 * 'gpio' class
 * general purpose io module
 */

static struct omap_hwmod_class_sysconfig omap3xxx_gpio_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_ENAWAKEUP | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE |
			   SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_gpio_hwmod_class = {
	.name = "gpio",
	.sysc = &omap3xxx_gpio_sysc,
	.rev = 1,
};

/* gpio_dev_attr*/
static struct omap_gpio_dev_attr gpio_dev_attr = {
	.bank_width = 32,
	.dbck_flag = true,
};

/* gpio1 */
static struct omap_hwmod_opt_clk gpio1_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio1_dbck", },
};

static struct omap_hwmod_ocp_if *omap3xxx_gpio1_slaves[] = {
	&omap3xxx_l4_wkup__gpio1,
};

static struct omap_hwmod omap3xxx_gpio1_hwmod = {
	.name		= "gpio1",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2_gpio1_irqs,
	.main_clk	= "gpio1_ick",
	.opt_clks	= gpio1_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio1_opt_clks),
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPIO1_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPIO1_SHIFT,
		},
	},
	.slaves		= omap3xxx_gpio1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_gpio1_slaves),
	.class		= &omap3xxx_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
};

/* gpio2 */
static struct omap_hwmod_opt_clk gpio2_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio2_dbck", },
};

static struct omap_hwmod_ocp_if *omap3xxx_gpio2_slaves[] = {
	&omap3xxx_l4_per__gpio2,
};

static struct omap_hwmod omap3xxx_gpio2_hwmod = {
	.name		= "gpio2",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2_gpio2_irqs,
	.main_clk	= "gpio2_ick",
	.opt_clks	= gpio2_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio2_opt_clks),
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPIO2_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPIO2_SHIFT,
		},
	},
	.slaves		= omap3xxx_gpio2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_gpio2_slaves),
	.class		= &omap3xxx_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
};

/* gpio3 */
static struct omap_hwmod_opt_clk gpio3_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio3_dbck", },
};

static struct omap_hwmod_ocp_if *omap3xxx_gpio3_slaves[] = {
	&omap3xxx_l4_per__gpio3,
};

static struct omap_hwmod omap3xxx_gpio3_hwmod = {
	.name		= "gpio3",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2_gpio3_irqs,
	.main_clk	= "gpio3_ick",
	.opt_clks	= gpio3_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio3_opt_clks),
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPIO3_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPIO3_SHIFT,
		},
	},
	.slaves		= omap3xxx_gpio3_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_gpio3_slaves),
	.class		= &omap3xxx_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
};

/* gpio4 */
static struct omap_hwmod_opt_clk gpio4_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio4_dbck", },
};

static struct omap_hwmod_ocp_if *omap3xxx_gpio4_slaves[] = {
	&omap3xxx_l4_per__gpio4,
};

static struct omap_hwmod omap3xxx_gpio4_hwmod = {
	.name		= "gpio4",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2_gpio4_irqs,
	.main_clk	= "gpio4_ick",
	.opt_clks	= gpio4_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio4_opt_clks),
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPIO4_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPIO4_SHIFT,
		},
	},
	.slaves		= omap3xxx_gpio4_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_gpio4_slaves),
	.class		= &omap3xxx_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
};

/* gpio5 */
static struct omap_hwmod_irq_info omap3xxx_gpio5_irqs[] = {
	{ .irq = 33 }, /* INT_34XX_GPIO_BANK5 */
	{ .irq = -1 }
};

static struct omap_hwmod_opt_clk gpio5_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio5_dbck", },
};

static struct omap_hwmod_ocp_if *omap3xxx_gpio5_slaves[] = {
	&omap3xxx_l4_per__gpio5,
};

static struct omap_hwmod omap3xxx_gpio5_hwmod = {
	.name		= "gpio5",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap3xxx_gpio5_irqs,
	.main_clk	= "gpio5_ick",
	.opt_clks	= gpio5_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio5_opt_clks),
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPIO5_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPIO5_SHIFT,
		},
	},
	.slaves		= omap3xxx_gpio5_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_gpio5_slaves),
	.class		= &omap3xxx_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
};

/* gpio6 */
static struct omap_hwmod_irq_info omap3xxx_gpio6_irqs[] = {
	{ .irq = 34 }, /* INT_34XX_GPIO_BANK6 */
	{ .irq = -1 }
};

static struct omap_hwmod_opt_clk gpio6_opt_clks[] = {
	{ .role = "dbclk", .clk = "gpio6_dbck", },
};

static struct omap_hwmod_ocp_if *omap3xxx_gpio6_slaves[] = {
	&omap3xxx_l4_per__gpio6,
};

static struct omap_hwmod omap3xxx_gpio6_hwmod = {
	.name		= "gpio6",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap3xxx_gpio6_irqs,
	.main_clk	= "gpio6_ick",
	.opt_clks	= gpio6_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(gpio6_opt_clks),
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_GPIO6_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_GPIO6_SHIFT,
		},
	},
	.slaves		= omap3xxx_gpio6_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_gpio6_slaves),
	.class		= &omap3xxx_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
};

/* dma_system -> L3 */
static struct omap_hwmod_ocp_if omap3xxx_dma_system__l3 = {
	.master		= &omap3xxx_dma_system_hwmod,
	.slave		= &omap3xxx_l3_main_hwmod,
	.clk		= "core_l3_ick",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dma attributes */
static struct omap_dma_dev_attr dma_dev_attr = {
	.dev_caps  = RESERVE_CHANNEL | DMA_LINKED_LCH | GLOBAL_PRIORITY |
				IS_CSSA_32 | IS_CDSA_32 | IS_RW_PRIORITY,
	.lch_count = 32,
};

static struct omap_hwmod_class_sysconfig omap3xxx_dma_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x002c,
	.syss_offs	= 0x0028,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_MIDLEMODE | SYSC_HAS_CLOCKACTIVITY |
			   SYSC_HAS_EMUFREE | SYSC_HAS_AUTOIDLE |
			   SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			   MSTANDBY_FORCE | MSTANDBY_NO | MSTANDBY_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_dma_hwmod_class = {
	.name = "dma",
	.sysc = &omap3xxx_dma_sysc,
};

/* dma_system */
static struct omap_hwmod_addr_space omap3xxx_dma_system_addrs[] = {
	{
		.pa_start	= 0x48056000,
		.pa_end		= 0x48056fff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* dma_system master ports */
static struct omap_hwmod_ocp_if *omap3xxx_dma_system_masters[] = {
	&omap3xxx_dma_system__l3,
};

/* l4_cfg -> dma_system */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__dma_system = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_dma_system_hwmod,
	.clk		= "core_l4_ick",
	.addr		= omap3xxx_dma_system_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dma_system slave ports */
static struct omap_hwmod_ocp_if *omap3xxx_dma_system_slaves[] = {
	&omap3xxx_l4_core__dma_system,
};

static struct omap_hwmod omap3xxx_dma_system_hwmod = {
	.name		= "dma",
	.class		= &omap3xxx_dma_hwmod_class,
	.mpu_irqs	= omap2_dma_system_irqs,
	.main_clk	= "core_l3_ick",
	.prcm = {
		.omap2 = {
			.module_offs		= CORE_MOD,
			.prcm_reg_id		= 1,
			.module_bit		= OMAP3430_ST_SDMA_SHIFT,
			.idlest_reg_id		= 1,
			.idlest_idle_bit	= OMAP3430_ST_SDMA_SHIFT,
		},
	},
	.slaves		= omap3xxx_dma_system_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_dma_system_slaves),
	.masters	= omap3xxx_dma_system_masters,
	.masters_cnt	= ARRAY_SIZE(omap3xxx_dma_system_masters),
	.dev_attr	= &dma_dev_attr,
	.flags		= HWMOD_NO_IDLEST,
};

/*
 * 'mcbsp' class
 * multi channel buffered serial port controller
 */

static struct omap_hwmod_class_sysconfig omap3xxx_mcbsp_sysc = {
	.sysc_offs	= 0x008c,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_ENAWAKEUP |
			   SYSC_HAS_SIDLEMODE | SYSC_HAS_SOFTRESET),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
	.clockact	= 0x2,
};

static struct omap_hwmod_class omap3xxx_mcbsp_hwmod_class = {
	.name = "mcbsp",
	.sysc = &omap3xxx_mcbsp_sysc,
	.rev  = MCBSP_CONFIG_TYPE3,
};

/* mcbsp1 */
static struct omap_hwmod_irq_info omap3xxx_mcbsp1_irqs[] = {
	{ .name = "irq", .irq = 16 },
	{ .name = "tx", .irq = 59 },
	{ .name = "rx", .irq = 60 },
	{ .irq = -1 }
};

static struct omap_hwmod_addr_space omap3xxx_mcbsp1_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x48074000,
		.pa_end		= 0x480740ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_core -> mcbsp1 */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__mcbsp1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_mcbsp1_hwmod,
	.clk		= "mcbsp1_ick",
	.addr		= omap3xxx_mcbsp1_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mcbsp1 slave ports */
static struct omap_hwmod_ocp_if *omap3xxx_mcbsp1_slaves[] = {
	&omap3xxx_l4_core__mcbsp1,
};

static struct omap_hwmod omap3xxx_mcbsp1_hwmod = {
	.name		= "mcbsp1",
	.class		= &omap3xxx_mcbsp_hwmod_class,
	.mpu_irqs	= omap3xxx_mcbsp1_irqs,
	.sdma_reqs	= omap2_mcbsp1_sdma_reqs,
	.main_clk	= "mcbsp1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCBSP1_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCBSP1_SHIFT,
		},
	},
	.slaves		= omap3xxx_mcbsp1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_mcbsp1_slaves),
};

/* mcbsp2 */
static struct omap_hwmod_irq_info omap3xxx_mcbsp2_irqs[] = {
	{ .name = "irq", .irq = 17 },
	{ .name = "tx", .irq = 62 },
	{ .name = "rx", .irq = 63 },
	{ .irq = -1 }
};

static struct omap_hwmod_addr_space omap3xxx_mcbsp2_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x49022000,
		.pa_end		= 0x490220ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> mcbsp2 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__mcbsp2 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_mcbsp2_hwmod,
	.clk		= "mcbsp2_ick",
	.addr		= omap3xxx_mcbsp2_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mcbsp2 slave ports */
static struct omap_hwmod_ocp_if *omap3xxx_mcbsp2_slaves[] = {
	&omap3xxx_l4_per__mcbsp2,
};

static struct omap_mcbsp_dev_attr omap34xx_mcbsp2_dev_attr = {
	.sidetone	= "mcbsp2_sidetone",
};

static struct omap_hwmod omap3xxx_mcbsp2_hwmod = {
	.name		= "mcbsp2",
	.class		= &omap3xxx_mcbsp_hwmod_class,
	.mpu_irqs	= omap3xxx_mcbsp2_irqs,
	.sdma_reqs	= omap2_mcbsp2_sdma_reqs,
	.main_clk	= "mcbsp2_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCBSP2_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCBSP2_SHIFT,
		},
	},
	.slaves		= omap3xxx_mcbsp2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_mcbsp2_slaves),
	.dev_attr	= &omap34xx_mcbsp2_dev_attr,
};

/* mcbsp3 */
static struct omap_hwmod_irq_info omap3xxx_mcbsp3_irqs[] = {
	{ .name = "irq", .irq = 22 },
	{ .name = "tx", .irq = 89 },
	{ .name = "rx", .irq = 90 },
	{ .irq = -1 }
};

static struct omap_hwmod_addr_space omap3xxx_mcbsp3_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x49024000,
		.pa_end		= 0x490240ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> mcbsp3 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__mcbsp3 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_mcbsp3_hwmod,
	.clk		= "mcbsp3_ick",
	.addr		= omap3xxx_mcbsp3_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mcbsp3 slave ports */
static struct omap_hwmod_ocp_if *omap3xxx_mcbsp3_slaves[] = {
	&omap3xxx_l4_per__mcbsp3,
};

static struct omap_mcbsp_dev_attr omap34xx_mcbsp3_dev_attr = {
	.sidetone       = "mcbsp3_sidetone",
};

static struct omap_hwmod omap3xxx_mcbsp3_hwmod = {
	.name		= "mcbsp3",
	.class		= &omap3xxx_mcbsp_hwmod_class,
	.mpu_irqs	= omap3xxx_mcbsp3_irqs,
	.sdma_reqs	= omap2_mcbsp3_sdma_reqs,
	.main_clk	= "mcbsp3_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCBSP3_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCBSP3_SHIFT,
		},
	},
	.slaves		= omap3xxx_mcbsp3_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_mcbsp3_slaves),
	.dev_attr	= &omap34xx_mcbsp3_dev_attr,
};

/* mcbsp4 */
static struct omap_hwmod_irq_info omap3xxx_mcbsp4_irqs[] = {
	{ .name = "irq", .irq = 23 },
	{ .name = "tx", .irq = 54 },
	{ .name = "rx", .irq = 55 },
	{ .irq = -1 }
};

static struct omap_hwmod_dma_info omap3xxx_mcbsp4_sdma_chs[] = {
	{ .name = "rx", .dma_req = 20 },
	{ .name = "tx", .dma_req = 19 },
	{ .dma_req = -1 }
};

static struct omap_hwmod_addr_space omap3xxx_mcbsp4_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x49026000,
		.pa_end		= 0x490260ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> mcbsp4 */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__mcbsp4 = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_mcbsp4_hwmod,
	.clk		= "mcbsp4_ick",
	.addr		= omap3xxx_mcbsp4_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mcbsp4 slave ports */
static struct omap_hwmod_ocp_if *omap3xxx_mcbsp4_slaves[] = {
	&omap3xxx_l4_per__mcbsp4,
};

static struct omap_hwmod omap3xxx_mcbsp4_hwmod = {
	.name		= "mcbsp4",
	.class		= &omap3xxx_mcbsp_hwmod_class,
	.mpu_irqs	= omap3xxx_mcbsp4_irqs,
	.sdma_reqs	= omap3xxx_mcbsp4_sdma_chs,
	.main_clk	= "mcbsp4_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCBSP4_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCBSP4_SHIFT,
		},
	},
	.slaves		= omap3xxx_mcbsp4_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_mcbsp4_slaves),
};

/* mcbsp5 */
static struct omap_hwmod_irq_info omap3xxx_mcbsp5_irqs[] = {
	{ .name = "irq", .irq = 27 },
	{ .name = "tx", .irq = 81 },
	{ .name = "rx", .irq = 82 },
	{ .irq = -1 }
};

static struct omap_hwmod_dma_info omap3xxx_mcbsp5_sdma_chs[] = {
	{ .name = "rx", .dma_req = 22 },
	{ .name = "tx", .dma_req = 21 },
	{ .dma_req = -1 }
};

static struct omap_hwmod_addr_space omap3xxx_mcbsp5_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x48096000,
		.pa_end		= 0x480960ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_core -> mcbsp5 */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__mcbsp5 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_mcbsp5_hwmod,
	.clk		= "mcbsp5_ick",
	.addr		= omap3xxx_mcbsp5_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mcbsp5 slave ports */
static struct omap_hwmod_ocp_if *omap3xxx_mcbsp5_slaves[] = {
	&omap3xxx_l4_core__mcbsp5,
};

static struct omap_hwmod omap3xxx_mcbsp5_hwmod = {
	.name		= "mcbsp5",
	.class		= &omap3xxx_mcbsp_hwmod_class,
	.mpu_irqs	= omap3xxx_mcbsp5_irqs,
	.sdma_reqs	= omap3xxx_mcbsp5_sdma_chs,
	.main_clk	= "mcbsp5_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCBSP5_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCBSP5_SHIFT,
		},
	},
	.slaves		= omap3xxx_mcbsp5_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_mcbsp5_slaves),
};
/* 'mcbsp sidetone' class */

static struct omap_hwmod_class_sysconfig omap3xxx_mcbsp_sidetone_sysc = {
	.sysc_offs	= 0x0010,
	.sysc_flags	= SYSC_HAS_AUTOIDLE,
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_mcbsp_sidetone_hwmod_class = {
	.name = "mcbsp_sidetone",
	.sysc = &omap3xxx_mcbsp_sidetone_sysc,
};

/* mcbsp2_sidetone */
static struct omap_hwmod_irq_info omap3xxx_mcbsp2_sidetone_irqs[] = {
	{ .name = "irq", .irq = 4 },
	{ .irq = -1 }
};

static struct omap_hwmod_addr_space omap3xxx_mcbsp2_sidetone_addrs[] = {
	{
		.name		= "sidetone",
		.pa_start	= 0x49028000,
		.pa_end		= 0x490280ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> mcbsp2_sidetone */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__mcbsp2_sidetone = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_mcbsp2_sidetone_hwmod,
	.clk		= "mcbsp2_ick",
	.addr		= omap3xxx_mcbsp2_sidetone_addrs,
	.user		= OCP_USER_MPU,
};

/* mcbsp2_sidetone slave ports */
static struct omap_hwmod_ocp_if *omap3xxx_mcbsp2_sidetone_slaves[] = {
	&omap3xxx_l4_per__mcbsp2_sidetone,
};

static struct omap_hwmod omap3xxx_mcbsp2_sidetone_hwmod = {
	.name		= "mcbsp2_sidetone",
	.class		= &omap3xxx_mcbsp_sidetone_hwmod_class,
	.mpu_irqs	= omap3xxx_mcbsp2_sidetone_irqs,
	.main_clk	= "mcbsp2_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			 .module_bit = OMAP3430_EN_MCBSP2_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCBSP2_SHIFT,
		},
	},
	.slaves		= omap3xxx_mcbsp2_sidetone_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_mcbsp2_sidetone_slaves),
};

/* mcbsp3_sidetone */
static struct omap_hwmod_irq_info omap3xxx_mcbsp3_sidetone_irqs[] = {
	{ .name = "irq", .irq = 5 },
	{ .irq = -1 }
};

static struct omap_hwmod_addr_space omap3xxx_mcbsp3_sidetone_addrs[] = {
	{
		.name		= "sidetone",
		.pa_start	= 0x4902A000,
		.pa_end		= 0x4902A0ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_per -> mcbsp3_sidetone */
static struct omap_hwmod_ocp_if omap3xxx_l4_per__mcbsp3_sidetone = {
	.master		= &omap3xxx_l4_per_hwmod,
	.slave		= &omap3xxx_mcbsp3_sidetone_hwmod,
	.clk		= "mcbsp3_ick",
	.addr		= omap3xxx_mcbsp3_sidetone_addrs,
	.user		= OCP_USER_MPU,
};

/* mcbsp3_sidetone slave ports */
static struct omap_hwmod_ocp_if *omap3xxx_mcbsp3_sidetone_slaves[] = {
	&omap3xxx_l4_per__mcbsp3_sidetone,
};

static struct omap_hwmod omap3xxx_mcbsp3_sidetone_hwmod = {
	.name		= "mcbsp3_sidetone",
	.class		= &omap3xxx_mcbsp_sidetone_hwmod_class,
	.mpu_irqs	= omap3xxx_mcbsp3_sidetone_irqs,
	.main_clk	= "mcbsp3_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCBSP3_SHIFT,
			.module_offs = OMAP3430_PER_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCBSP3_SHIFT,
		},
	},
	.slaves		= omap3xxx_mcbsp3_sidetone_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_mcbsp3_sidetone_slaves),
};


/* SR common */
static struct omap_hwmod_sysc_fields omap34xx_sr_sysc_fields = {
	.clkact_shift	= 20,
};

static struct omap_hwmod_class_sysconfig omap34xx_sr_sysc = {
	.sysc_offs	= 0x24,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_NO_CACHE),
	.clockact	= CLOCKACT_TEST_ICLK,
	.sysc_fields	= &omap34xx_sr_sysc_fields,
};

static struct omap_hwmod_class omap34xx_smartreflex_hwmod_class = {
	.name = "smartreflex",
	.sysc = &omap34xx_sr_sysc,
	.rev  = 1,
};

static struct omap_hwmod_sysc_fields omap36xx_sr_sysc_fields = {
	.sidle_shift	= 24,
	.enwkup_shift	= 26
};

static struct omap_hwmod_class_sysconfig omap36xx_sr_sysc = {
	.sysc_offs	= 0x38,
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_ENAWAKEUP |
			SYSC_NO_CACHE),
	.sysc_fields	= &omap36xx_sr_sysc_fields,
};

static struct omap_hwmod_class omap36xx_smartreflex_hwmod_class = {
	.name = "smartreflex",
	.sysc = &omap36xx_sr_sysc,
	.rev  = 2,
};

/* SR1 */
static struct omap_smartreflex_dev_attr sr1_dev_attr = {
	.sensor_voltdm_name   = "mpu_iva",
};

static struct omap_hwmod_ocp_if *omap3_sr1_slaves[] = {
	&omap3_l4_core__sr1,
};

static struct omap_hwmod omap34xx_sr1_hwmod = {
	.name		= "sr1_hwmod",
	.class		= &omap34xx_smartreflex_hwmod_class,
	.main_clk	= "sr1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_SR1_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_SR1_SHIFT,
		},
	},
	.slaves		= omap3_sr1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3_sr1_slaves),
	.dev_attr	= &sr1_dev_attr,
	.mpu_irqs	= omap3_smartreflex_mpu_irqs,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

static struct omap_hwmod omap36xx_sr1_hwmod = {
	.name		= "sr1_hwmod",
	.class		= &omap36xx_smartreflex_hwmod_class,
	.main_clk	= "sr1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_SR1_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_SR1_SHIFT,
		},
	},
	.slaves		= omap3_sr1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3_sr1_slaves),
	.dev_attr	= &sr1_dev_attr,
	.mpu_irqs	= omap3_smartreflex_mpu_irqs,
};

/* SR2 */
static struct omap_smartreflex_dev_attr sr2_dev_attr = {
	.sensor_voltdm_name	= "core",
};

static struct omap_hwmod_ocp_if *omap3_sr2_slaves[] = {
	&omap3_l4_core__sr2,
};

static struct omap_hwmod omap34xx_sr2_hwmod = {
	.name		= "sr2_hwmod",
	.class		= &omap34xx_smartreflex_hwmod_class,
	.main_clk	= "sr2_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_SR2_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_SR2_SHIFT,
		},
	},
	.slaves		= omap3_sr2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3_sr2_slaves),
	.dev_attr	= &sr2_dev_attr,
	.mpu_irqs	= omap3_smartreflex_core_irqs,
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
};

static struct omap_hwmod omap36xx_sr2_hwmod = {
	.name		= "sr2_hwmod",
	.class		= &omap36xx_smartreflex_hwmod_class,
	.main_clk	= "sr2_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_SR2_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_EN_SR2_SHIFT,
		},
	},
	.slaves		= omap3_sr2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3_sr2_slaves),
	.dev_attr	= &sr2_dev_attr,
	.mpu_irqs	= omap3_smartreflex_core_irqs,
};

/*
 * 'mailbox' class
 * mailbox module allowing communication between the on-chip processors
 * using a queued mailbox-interrupt mechanism.
 */

static struct omap_hwmod_class_sysconfig omap3xxx_mailbox_sysc = {
	.rev_offs	= 0x000,
	.sysc_offs	= 0x010,
	.syss_offs	= 0x014,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
				SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_mailbox_hwmod_class = {
	.name = "mailbox",
	.sysc = &omap3xxx_mailbox_sysc,
};

static struct omap_hwmod omap3xxx_mailbox_hwmod;
static struct omap_hwmod_irq_info omap3xxx_mailbox_irqs[] = {
	{ .irq = 26 },
	{ .irq = -1 }
};

static struct omap_hwmod_addr_space omap3xxx_mailbox_addrs[] = {
	{
		.pa_start	= 0x48094000,
		.pa_end		= 0x480941ff,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

/* l4_core -> mailbox */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__mailbox = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_mailbox_hwmod,
	.addr		= omap3xxx_mailbox_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mailbox slave ports */
static struct omap_hwmod_ocp_if *omap3xxx_mailbox_slaves[] = {
	&omap3xxx_l4_core__mailbox,
};

static struct omap_hwmod omap3xxx_mailbox_hwmod = {
	.name		= "mailbox",
	.class		= &omap3xxx_mailbox_hwmod_class,
	.mpu_irqs	= omap3xxx_mailbox_irqs,
	.main_clk	= "mailboxes_ick",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MAILBOXES_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MAILBOXES_SHIFT,
		},
	},
	.slaves		= omap3xxx_mailbox_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_mailbox_slaves),
};

/* l4 core -> mcspi1 interface */
static struct omap_hwmod_ocp_if omap34xx_l4_core__mcspi1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_mcspi1,
	.clk		= "mcspi1_ick",
	.addr		= omap2_mcspi1_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi2 interface */
static struct omap_hwmod_ocp_if omap34xx_l4_core__mcspi2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_mcspi2,
	.clk		= "mcspi2_ick",
	.addr		= omap2_mcspi2_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi3 interface */
static struct omap_hwmod_ocp_if omap34xx_l4_core__mcspi3 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_mcspi3,
	.clk		= "mcspi3_ick",
	.addr		= omap2430_mcspi3_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi4 interface */
static struct omap_hwmod_addr_space omap34xx_mcspi4_addr_space[] = {
	{
		.pa_start	= 0x480ba000,
		.pa_end		= 0x480ba0ff,
		.flags		= ADDR_TYPE_RT,
	},
	{ }
};

static struct omap_hwmod_ocp_if omap34xx_l4_core__mcspi4 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_mcspi4,
	.clk		= "mcspi4_ick",
	.addr		= omap34xx_mcspi4_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/*
 * 'mcspi' class
 * multichannel serial port interface (mcspi) / master/slave synchronous serial
 * bus
 */

static struct omap_hwmod_class_sysconfig omap34xx_mcspi_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
				SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
				SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap34xx_mcspi_class = {
	.name = "mcspi",
	.sysc = &omap34xx_mcspi_sysc,
	.rev = OMAP3_MCSPI_REV,
};

/* mcspi1 */
static struct omap_hwmod_ocp_if *omap34xx_mcspi1_slaves[] = {
	&omap34xx_l4_core__mcspi1,
};

static struct omap2_mcspi_dev_attr omap_mcspi1_dev_attr = {
	.num_chipselect = 4,
};

static struct omap_hwmod omap34xx_mcspi1 = {
	.name		= "mcspi1",
	.mpu_irqs	= omap2_mcspi1_mpu_irqs,
	.sdma_reqs	= omap2_mcspi1_sdma_reqs,
	.main_clk	= "mcspi1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCSPI1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCSPI1_SHIFT,
		},
	},
	.slaves		= omap34xx_mcspi1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap34xx_mcspi1_slaves),
	.class		= &omap34xx_mcspi_class,
	.dev_attr       = &omap_mcspi1_dev_attr,
};

/* mcspi2 */
static struct omap_hwmod_ocp_if *omap34xx_mcspi2_slaves[] = {
	&omap34xx_l4_core__mcspi2,
};

static struct omap2_mcspi_dev_attr omap_mcspi2_dev_attr = {
	.num_chipselect = 2,
};

static struct omap_hwmod omap34xx_mcspi2 = {
	.name		= "mcspi2",
	.mpu_irqs	= omap2_mcspi2_mpu_irqs,
	.sdma_reqs	= omap2_mcspi2_sdma_reqs,
	.main_clk	= "mcspi2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCSPI2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCSPI2_SHIFT,
		},
	},
	.slaves		= omap34xx_mcspi2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap34xx_mcspi2_slaves),
	.class		= &omap34xx_mcspi_class,
	.dev_attr       = &omap_mcspi2_dev_attr,
};

/* mcspi3 */
static struct omap_hwmod_irq_info omap34xx_mcspi3_mpu_irqs[] = {
	{ .name = "irq", .irq = 91 }, /* 91 */
	{ .irq = -1 }
};

static struct omap_hwmod_dma_info omap34xx_mcspi3_sdma_reqs[] = {
	{ .name = "tx0", .dma_req = 15 },
	{ .name = "rx0", .dma_req = 16 },
	{ .name = "tx1", .dma_req = 23 },
	{ .name = "rx1", .dma_req = 24 },
	{ .dma_req = -1 }
};

static struct omap_hwmod_ocp_if *omap34xx_mcspi3_slaves[] = {
	&omap34xx_l4_core__mcspi3,
};

static struct omap2_mcspi_dev_attr omap_mcspi3_dev_attr = {
	.num_chipselect = 2,
};

static struct omap_hwmod omap34xx_mcspi3 = {
	.name		= "mcspi3",
	.mpu_irqs	= omap34xx_mcspi3_mpu_irqs,
	.sdma_reqs	= omap34xx_mcspi3_sdma_reqs,
	.main_clk	= "mcspi3_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCSPI3_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCSPI3_SHIFT,
		},
	},
	.slaves		= omap34xx_mcspi3_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap34xx_mcspi3_slaves),
	.class		= &omap34xx_mcspi_class,
	.dev_attr       = &omap_mcspi3_dev_attr,
};

/* SPI4 */
static struct omap_hwmod_irq_info omap34xx_mcspi4_mpu_irqs[] = {
	{ .name = "irq", .irq = INT_34XX_SPI4_IRQ }, /* 48 */
	{ .irq = -1 }
};

static struct omap_hwmod_dma_info omap34xx_mcspi4_sdma_reqs[] = {
	{ .name = "tx0", .dma_req = 70 }, /* DMA_SPI4_TX0 */
	{ .name = "rx0", .dma_req = 71 }, /* DMA_SPI4_RX0 */
	{ .dma_req = -1 }
};

static struct omap_hwmod_ocp_if *omap34xx_mcspi4_slaves[] = {
	&omap34xx_l4_core__mcspi4,
};

static struct omap2_mcspi_dev_attr omap_mcspi4_dev_attr = {
	.num_chipselect = 1,
};

static struct omap_hwmod omap34xx_mcspi4 = {
	.name		= "mcspi4",
	.mpu_irqs	= omap34xx_mcspi4_mpu_irqs,
	.sdma_reqs	= omap34xx_mcspi4_sdma_reqs,
	.main_clk	= "mcspi4_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MCSPI4_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MCSPI4_SHIFT,
		},
	},
	.slaves		= omap34xx_mcspi4_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap34xx_mcspi4_slaves),
	.class		= &omap34xx_mcspi_class,
	.dev_attr       = &omap_mcspi4_dev_attr,
};

/*
 * usbhsotg
 */
static struct omap_hwmod_class_sysconfig omap3xxx_usbhsotg_sysc = {
	.rev_offs	= 0x0400,
	.sysc_offs	= 0x0404,
	.syss_offs	= 0x0408,
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_MIDLEMODE|
			  SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			  SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			  MSTANDBY_FORCE | MSTANDBY_NO | MSTANDBY_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class usbotg_class = {
	.name = "usbotg",
	.sysc = &omap3xxx_usbhsotg_sysc,
};
/* usb_otg_hs */
static struct omap_hwmod_irq_info omap3xxx_usbhsotg_mpu_irqs[] = {

	{ .name = "mc", .irq = 92 },
	{ .name = "dma", .irq = 93 },
	{ .irq = -1 }
};

static struct omap_hwmod omap3xxx_usbhsotg_hwmod = {
	.name		= "usb_otg_hs",
	.mpu_irqs	= omap3xxx_usbhsotg_mpu_irqs,
	.main_clk	= "hsotgusb_ick",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_HSOTGUSB_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430ES2_ST_HSOTGUSB_IDLE_SHIFT,
			.idlest_stdby_bit = OMAP3430ES2_ST_HSOTGUSB_STDBY_SHIFT
		},
	},
	.masters	= omap3xxx_usbhsotg_masters,
	.masters_cnt	= ARRAY_SIZE(omap3xxx_usbhsotg_masters),
	.slaves		= omap3xxx_usbhsotg_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_usbhsotg_slaves),
	.class		= &usbotg_class,

	/*
	 * Erratum ID: i479  idle_req / idle_ack mechanism potentially
	 * broken when autoidle is enabled
	 * workaround is to disable the autoidle bit at module level.
	 */
	.flags		= HWMOD_NO_OCP_AUTOIDLE | HWMOD_SWSUP_SIDLE
				| HWMOD_SWSUP_MSTANDBY,
};

/* usb_otg_hs */
static struct omap_hwmod_irq_info am35xx_usbhsotg_mpu_irqs[] = {

	{ .name = "mc", .irq = 71 },
	{ .irq = -1 }
};

static struct omap_hwmod_class am35xx_usbotg_class = {
	.name = "am35xx_usbotg",
	.sysc = NULL,
};

static struct omap_hwmod am35xx_usbhsotg_hwmod = {
	.name		= "am35x_otg_hs",
	.mpu_irqs	= am35xx_usbhsotg_mpu_irqs,
	.main_clk	= NULL,
	.prcm = {
		.omap2 = {
		},
	},
	.masters	= am35xx_usbhsotg_masters,
	.masters_cnt	= ARRAY_SIZE(am35xx_usbhsotg_masters),
	.slaves		= am35xx_usbhsotg_slaves,
	.slaves_cnt	= ARRAY_SIZE(am35xx_usbhsotg_slaves),
	.class		= &am35xx_usbotg_class,
};

/* MMC/SD/SDIO common */

static struct omap_hwmod_class_sysconfig omap34xx_mmc_sysc = {
	.rev_offs	= 0x1fc,
	.sysc_offs	= 0x10,
	.syss_offs	= 0x14,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap34xx_mmc_class = {
	.name = "mmc",
	.sysc = &omap34xx_mmc_sysc,
};

/* MMC/SD/SDIO1 */

static struct omap_hwmod_irq_info omap34xx_mmc1_mpu_irqs[] = {
	{ .irq = 83, },
	{ .irq = -1 }
};

static struct omap_hwmod_dma_info omap34xx_mmc1_sdma_reqs[] = {
	{ .name = "tx",	.dma_req = 61, },
	{ .name = "rx",	.dma_req = 62, },
	{ .dma_req = -1 }
};

static struct omap_hwmod_opt_clk omap34xx_mmc1_opt_clks[] = {
	{ .role = "dbck", .clk = "omap_32k_fck", },
};

static struct omap_hwmod_ocp_if *omap3xxx_mmc1_slaves[] = {
	&omap3xxx_l4_core__mmc1,
};

static struct omap_mmc_dev_attr mmc1_dev_attr = {
	.flags = OMAP_HSMMC_SUPPORTS_DUAL_VOLT,
};

/* See 35xx errata 2.1.1.128 in SPRZ278F */
static struct omap_mmc_dev_attr mmc1_pre_es3_dev_attr = {
	.flags = (OMAP_HSMMC_SUPPORTS_DUAL_VOLT |
		  OMAP_HSMMC_BROKEN_MULTIBLOCK_READ),
};

static struct omap_hwmod omap3xxx_pre_es3_mmc1_hwmod = {
	.name		= "mmc1",
	.mpu_irqs	= omap34xx_mmc1_mpu_irqs,
	.sdma_reqs	= omap34xx_mmc1_sdma_reqs,
	.opt_clks	= omap34xx_mmc1_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap34xx_mmc1_opt_clks),
	.main_clk	= "mmchs1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MMC1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MMC1_SHIFT,
		},
	},
	.dev_attr	= &mmc1_pre_es3_dev_attr,
	.slaves		= omap3xxx_mmc1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_mmc1_slaves),
	.class		= &omap34xx_mmc_class,
};

static struct omap_hwmod omap3xxx_es3plus_mmc1_hwmod = {
	.name		= "mmc1",
	.mpu_irqs	= omap34xx_mmc1_mpu_irqs,
	.sdma_reqs	= omap34xx_mmc1_sdma_reqs,
	.opt_clks	= omap34xx_mmc1_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap34xx_mmc1_opt_clks),
	.main_clk	= "mmchs1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MMC1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MMC1_SHIFT,
		},
	},
	.dev_attr	= &mmc1_dev_attr,
	.slaves		= omap3xxx_mmc1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_mmc1_slaves),
	.class		= &omap34xx_mmc_class,
};

/* MMC/SD/SDIO2 */

static struct omap_hwmod_irq_info omap34xx_mmc2_mpu_irqs[] = {
	{ .irq = INT_24XX_MMC2_IRQ, },
	{ .irq = -1 }
};

static struct omap_hwmod_dma_info omap34xx_mmc2_sdma_reqs[] = {
	{ .name = "tx",	.dma_req = 47, },
	{ .name = "rx",	.dma_req = 48, },
	{ .dma_req = -1 }
};

static struct omap_hwmod_opt_clk omap34xx_mmc2_opt_clks[] = {
	{ .role = "dbck", .clk = "omap_32k_fck", },
};

static struct omap_hwmod_ocp_if *omap3xxx_mmc2_slaves[] = {
	&omap3xxx_l4_core__mmc2,
};

/* See 35xx errata 2.1.1.128 in SPRZ278F */
static struct omap_mmc_dev_attr mmc2_pre_es3_dev_attr = {
	.flags = OMAP_HSMMC_BROKEN_MULTIBLOCK_READ,
};

static struct omap_hwmod omap3xxx_pre_es3_mmc2_hwmod = {
	.name		= "mmc2",
	.mpu_irqs	= omap34xx_mmc2_mpu_irqs,
	.sdma_reqs	= omap34xx_mmc2_sdma_reqs,
	.opt_clks	= omap34xx_mmc2_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap34xx_mmc2_opt_clks),
	.main_clk	= "mmchs2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MMC2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MMC2_SHIFT,
		},
	},
	.dev_attr	= &mmc2_pre_es3_dev_attr,
	.slaves		= omap3xxx_mmc2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_mmc2_slaves),
	.class		= &omap34xx_mmc_class,
};

static struct omap_hwmod omap3xxx_es3plus_mmc2_hwmod = {
	.name		= "mmc2",
	.mpu_irqs	= omap34xx_mmc2_mpu_irqs,
	.sdma_reqs	= omap34xx_mmc2_sdma_reqs,
	.opt_clks	= omap34xx_mmc2_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap34xx_mmc2_opt_clks),
	.main_clk	= "mmchs2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MMC2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MMC2_SHIFT,
		},
	},
	.slaves		= omap3xxx_mmc2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_mmc2_slaves),
	.class		= &omap34xx_mmc_class,
};

/* MMC/SD/SDIO3 */

static struct omap_hwmod_irq_info omap34xx_mmc3_mpu_irqs[] = {
	{ .irq = 94, },
	{ .irq = -1 }
};

static struct omap_hwmod_dma_info omap34xx_mmc3_sdma_reqs[] = {
	{ .name = "tx",	.dma_req = 77, },
	{ .name = "rx",	.dma_req = 78, },
	{ .dma_req = -1 }
};

static struct omap_hwmod_opt_clk omap34xx_mmc3_opt_clks[] = {
	{ .role = "dbck", .clk = "omap_32k_fck", },
};

static struct omap_hwmod_ocp_if *omap3xxx_mmc3_slaves[] = {
	&omap3xxx_l4_core__mmc3,
};

static struct omap_hwmod omap3xxx_mmc3_hwmod = {
	.name		= "mmc3",
	.mpu_irqs	= omap34xx_mmc3_mpu_irqs,
	.sdma_reqs	= omap34xx_mmc3_sdma_reqs,
	.opt_clks	= omap34xx_mmc3_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap34xx_mmc3_opt_clks),
	.main_clk	= "mmchs3_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP3430_EN_MMC3_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430_ST_MMC3_SHIFT,
		},
	},
	.slaves		= omap3xxx_mmc3_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_mmc3_slaves),
	.class		= &omap34xx_mmc_class,
};

/*
 * 'usb_host_hs' class
 * high-speed multi-port usb host controller
 */
static struct omap_hwmod_ocp_if omap3xxx_usb_host_hs__l3_main_2 = {
	.master		= &omap3xxx_usb_host_hs_hwmod,
	.slave		= &omap3xxx_l3_main_hwmod,
	.clk		= "core_l3_ick",
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_class_sysconfig omap3xxx_usb_host_hs_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_MIDLEMODE | SYSC_HAS_CLOCKACTIVITY |
			   SYSC_HAS_SIDLEMODE | SYSC_HAS_ENAWAKEUP |
			   SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE |
			   SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART |
			   MSTANDBY_FORCE | MSTANDBY_NO | MSTANDBY_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_usb_host_hs_hwmod_class = {
	.name = "usb_host_hs",
	.sysc = &omap3xxx_usb_host_hs_sysc,
};

static struct omap_hwmod_ocp_if *omap3xxx_usb_host_hs_masters[] = {
	&omap3xxx_usb_host_hs__l3_main_2,
};

static struct omap_hwmod_addr_space omap3xxx_usb_host_hs_addrs[] = {
	{
		.name		= "uhh",
		.pa_start	= 0x48064000,
		.pa_end		= 0x480643ff,
		.flags		= ADDR_TYPE_RT
	},
	{
		.name		= "ohci",
		.pa_start	= 0x48064400,
		.pa_end		= 0x480647ff,
	},
	{
		.name		= "ehci",
		.pa_start	= 0x48064800,
		.pa_end		= 0x48064cff,
	},
	{}
};

static struct omap_hwmod_ocp_if omap3xxx_l4_core__usb_host_hs = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_usb_host_hs_hwmod,
	.clk		= "usbhost_ick",
	.addr		= omap3xxx_usb_host_hs_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if *omap3xxx_usb_host_hs_slaves[] = {
	&omap3xxx_l4_core__usb_host_hs,
};

static struct omap_hwmod_opt_clk omap3xxx_usb_host_hs_opt_clks[] = {
	  { .role = "ehci_logic_fck", .clk = "usbhost_120m_fck", },
};

static struct omap_hwmod_irq_info omap3xxx_usb_host_hs_irqs[] = {
	{ .name = "ohci-irq", .irq = 76 },
	{ .name = "ehci-irq", .irq = 77 },
	{ .irq = -1 }
};

static struct omap_hwmod omap3xxx_usb_host_hs_hwmod = {
	.name		= "usb_host_hs",
	.class		= &omap3xxx_usb_host_hs_hwmod_class,
	.clkdm_name	= "l3_init_clkdm",
	.mpu_irqs	= omap3xxx_usb_host_hs_irqs,
	.main_clk	= "usbhost_48m_fck",
	.prcm = {
		.omap2 = {
			.module_offs = OMAP3430ES2_USBHOST_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP3430ES2_EN_USBHOST1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP3430ES2_ST_USBHOST_IDLE_SHIFT,
			.idlest_stdby_bit = OMAP3430ES2_ST_USBHOST_STDBY_SHIFT,
		},
	},
	.opt_clks	= omap3xxx_usb_host_hs_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap3xxx_usb_host_hs_opt_clks),
	.slaves		= omap3xxx_usb_host_hs_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_usb_host_hs_slaves),
	.masters	= omap3xxx_usb_host_hs_masters,
	.masters_cnt	= ARRAY_SIZE(omap3xxx_usb_host_hs_masters),

	/*
	 * Errata: USBHOST Configured In Smart-Idle Can Lead To a Deadlock
	 * id: i660
	 *
	 * Description:
	 * In the following configuration :
	 * - USBHOST module is set to smart-idle mode
	 * - PRCM asserts idle_req to the USBHOST module ( This typically
	 *   happens when the system is going to a low power mode : all ports
	 *   have been suspended, the master part of the USBHOST module has
	 *   entered the standby state, and SW has cut the functional clocks)
	 * - an USBHOST interrupt occurs before the module is able to answer
	 *   idle_ack, typically a remote wakeup IRQ.
	 * Then the USB HOST module will enter a deadlock situation where it
	 * is no more accessible nor functional.
	 *
	 * Workaround:
	 * Don't use smart idle; use only force idle, hence HWMOD_SWSUP_SIDLE
	 */

	/*
	 * Errata: USB host EHCI may stall when entering smart-standby mode
	 * Id: i571
	 *
	 * Description:
	 * When the USBHOST module is set to smart-standby mode, and when it is
	 * ready to enter the standby state (i.e. all ports are suspended and
	 * all attached devices are in suspend mode), then it can wrongly assert
	 * the Mstandby signal too early while there are still some residual OCP
	 * transactions ongoing. If this condition occurs, the internal state
	 * machine may go to an undefined state and the USB link may be stuck
	 * upon the next resume.
	 *
	 * Workaround:
	 * Don't use smart standby; use only force standby,
	 * hence HWMOD_SWSUP_MSTANDBY
	 */

	.flags		= HWMOD_SWSUP_SIDLE | HWMOD_SWSUP_MSTANDBY,
};

/*
 * 'usb_tll_hs' class
 * usb_tll_hs module is the adapter on the usb_host_hs ports
 */
static struct omap_hwmod_class_sysconfig omap3xxx_usb_tll_hs_sysc = {
	.rev_offs	= 0x0000,
	.sysc_offs	= 0x0010,
	.syss_offs	= 0x0014,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap3xxx_usb_tll_hs_hwmod_class = {
	.name = "usb_tll_hs",
	.sysc = &omap3xxx_usb_tll_hs_sysc,
};

static struct omap_hwmod_irq_info omap3xxx_usb_tll_hs_irqs[] = {
	{ .name = "tll-irq", .irq = 78 },
	{ .irq = -1 }
};

static struct omap_hwmod_addr_space omap3xxx_usb_tll_hs_addrs[] = {
	{
		.name		= "tll",
		.pa_start	= 0x48062000,
		.pa_end		= 0x48062fff,
		.flags		= ADDR_TYPE_RT
	},
	{}
};

static struct omap_hwmod_ocp_if omap3xxx_l4_core__usb_tll_hs = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap3xxx_usb_tll_hs_hwmod,
	.clk		= "usbtll_ick",
	.addr		= omap3xxx_usb_tll_hs_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

static struct omap_hwmod_ocp_if *omap3xxx_usb_tll_hs_slaves[] = {
	&omap3xxx_l4_core__usb_tll_hs,
};

static struct omap_hwmod omap3xxx_usb_tll_hs_hwmod = {
	.name		= "usb_tll_hs",
	.class		= &omap3xxx_usb_tll_hs_hwmod_class,
	.clkdm_name	= "l3_init_clkdm",
	.mpu_irqs	= omap3xxx_usb_tll_hs_irqs,
	.main_clk	= "usbtll_fck",
	.prcm = {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 3,
			.module_bit = OMAP3430ES2_EN_USBTLL_SHIFT,
			.idlest_reg_id = 3,
			.idlest_idle_bit = OMAP3430ES2_ST_USBTLL_SHIFT,
		},
	},
	.slaves		= omap3xxx_usb_tll_hs_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_usb_tll_hs_slaves),
};

static __initdata struct omap_hwmod *omap3xxx_hwmods[] = {
	&omap3xxx_l3_main_hwmod,
	&omap3xxx_l4_core_hwmod,
	&omap3xxx_l4_per_hwmod,
	&omap3xxx_l4_wkup_hwmod,
	&omap3xxx_mmc3_hwmod,
	&omap3xxx_mpu_hwmod,

	&omap3xxx_timer1_hwmod,
	&omap3xxx_timer2_hwmod,
	&omap3xxx_timer3_hwmod,
	&omap3xxx_timer4_hwmod,
	&omap3xxx_timer5_hwmod,
	&omap3xxx_timer6_hwmod,
	&omap3xxx_timer7_hwmod,
	&omap3xxx_timer8_hwmod,
	&omap3xxx_timer9_hwmod,
	&omap3xxx_timer10_hwmod,
	&omap3xxx_timer11_hwmod,

	&omap3xxx_wd_timer2_hwmod,
	&omap3xxx_uart1_hwmod,
	&omap3xxx_uart2_hwmod,
	&omap3xxx_uart3_hwmod,

	/* i2c class */
	&omap3xxx_i2c1_hwmod,
	&omap3xxx_i2c2_hwmod,
	&omap3xxx_i2c3_hwmod,

	/* gpio class */
	&omap3xxx_gpio1_hwmod,
	&omap3xxx_gpio2_hwmod,
	&omap3xxx_gpio3_hwmod,
	&omap3xxx_gpio4_hwmod,
	&omap3xxx_gpio5_hwmod,
	&omap3xxx_gpio6_hwmod,

	/* dma_system class*/
	&omap3xxx_dma_system_hwmod,

	/* mcbsp class */
	&omap3xxx_mcbsp1_hwmod,
	&omap3xxx_mcbsp2_hwmod,
	&omap3xxx_mcbsp3_hwmod,
	&omap3xxx_mcbsp4_hwmod,
	&omap3xxx_mcbsp5_hwmod,
	&omap3xxx_mcbsp2_sidetone_hwmod,
	&omap3xxx_mcbsp3_sidetone_hwmod,


	/* mcspi class */
	&omap34xx_mcspi1,
	&omap34xx_mcspi2,
	&omap34xx_mcspi3,
	&omap34xx_mcspi4,

	NULL,
};

/* GP-only hwmods */
static __initdata struct omap_hwmod *omap3xxx_gp_hwmods[] = {
	&omap3xxx_timer12_hwmod,
	NULL
};

/* 3430ES1-only hwmods */
static __initdata struct omap_hwmod *omap3430es1_hwmods[] = {
	&omap3430es1_dss_core_hwmod,
	NULL
};

/* 3430ES2+-only hwmods */
static __initdata struct omap_hwmod *omap3430es2plus_hwmods[] = {
	&omap3xxx_dss_core_hwmod,
	&omap3xxx_usbhsotg_hwmod,
	&omap3xxx_usb_host_hs_hwmod,
	&omap3xxx_usb_tll_hs_hwmod,
	NULL
};

/* <= 3430ES3-only hwmods */
static struct omap_hwmod *omap3430_pre_es3_hwmods[] __initdata = {
	&omap3xxx_pre_es3_mmc1_hwmod,
	&omap3xxx_pre_es3_mmc2_hwmod,
	NULL
};

/* 3430ES3+-only hwmods */
static struct omap_hwmod *omap3430_es3plus_hwmods[] __initdata = {
	&omap3xxx_es3plus_mmc1_hwmod,
	&omap3xxx_es3plus_mmc2_hwmod,
	NULL
};

/* 34xx-only hwmods (all ES revisions) */
static __initdata struct omap_hwmod *omap34xx_hwmods[] = {
	&omap3xxx_iva_hwmod,
	&omap34xx_sr1_hwmod,
	&omap34xx_sr2_hwmod,
	&omap3xxx_mailbox_hwmod,
	NULL
};

/* 36xx-only hwmods (all ES revisions) */
static __initdata struct omap_hwmod *omap36xx_hwmods[] = {
	&omap3xxx_iva_hwmod,
	&omap3xxx_uart4_hwmod,
	&omap3xxx_dss_core_hwmod,
	&omap36xx_sr1_hwmod,
	&omap36xx_sr2_hwmod,
	&omap3xxx_usbhsotg_hwmod,
	&omap3xxx_mailbox_hwmod,
	&omap3xxx_usb_host_hs_hwmod,
	&omap3xxx_usb_tll_hs_hwmod,
	&omap3xxx_es3plus_mmc1_hwmod,
	&omap3xxx_es3plus_mmc2_hwmod,
	NULL
};

static __initdata struct omap_hwmod *am35xx_hwmods[] = {
	&omap3xxx_dss_core_hwmod, /* XXX ??? */
	&am35xx_usbhsotg_hwmod,
	&am35xx_uart4_hwmod,
	&omap3xxx_usb_host_hs_hwmod,
	&omap3xxx_usb_tll_hs_hwmod,
	&omap3xxx_es3plus_mmc1_hwmod,
	&omap3xxx_es3plus_mmc2_hwmod,
	NULL
};

static __initdata struct omap_hwmod *omap3xxx_dss_hwmods[] = {
	/* dss class */
	&omap3xxx_dss_dispc_hwmod,
	&omap3xxx_dss_dsi1_hwmod,
	&omap3xxx_dss_rfbi_hwmod,
	&omap3xxx_dss_venc_hwmod,
	NULL
};

int __init omap3xxx_hwmod_init(void)
{
	int r;
	struct omap_hwmod **h = NULL;
	unsigned int rev;

	/* Register hwmods common to all OMAP3 */
	r = omap_hwmod_register(omap3xxx_hwmods);
	if (r < 0)
		return r;

	/* Register GP-only hwmods. */
	if (omap_type() == OMAP2_DEVICE_TYPE_GP) {
		r = omap_hwmod_register(omap3xxx_gp_hwmods);
		if (r < 0)
			return r;
	}

	rev = omap_rev();

	/*
	 * Register hwmods common to individual OMAP3 families, all
	 * silicon revisions (e.g., 34xx, or AM3505/3517, or 36xx)
	 * All possible revisions should be included in this conditional.
	 */
	if (rev == OMAP3430_REV_ES1_0 || rev == OMAP3430_REV_ES2_0 ||
	    rev == OMAP3430_REV_ES2_1 || rev == OMAP3430_REV_ES3_0 ||
	    rev == OMAP3430_REV_ES3_1 || rev == OMAP3430_REV_ES3_1_2) {
		h = omap34xx_hwmods;
	} else if (rev == OMAP3517_REV_ES1_0 || rev == OMAP3517_REV_ES1_1) {
		h = am35xx_hwmods;
	} else if (rev == OMAP3630_REV_ES1_0 || rev == OMAP3630_REV_ES1_1 ||
		   rev == OMAP3630_REV_ES1_2) {
		h = omap36xx_hwmods;
	} else {
		WARN(1, "OMAP3 hwmod family init: unknown chip type\n");
		return -EINVAL;
	};

	r = omap_hwmod_register(h);
	if (r < 0)
		return r;

	/*
	 * Register hwmods specific to certain ES levels of a
	 * particular family of silicon (e.g., 34xx ES1.0)
	 */
	h = NULL;
	if (rev == OMAP3430_REV_ES1_0) {
		h = omap3430es1_hwmods;
	} else if (rev == OMAP3430_REV_ES2_0 || rev == OMAP3430_REV_ES2_1 ||
		   rev == OMAP3430_REV_ES3_0 || rev == OMAP3430_REV_ES3_1 ||
		   rev == OMAP3430_REV_ES3_1_2) {
		h = omap3430es2plus_hwmods;
	};

	if (h) {
		r = omap_hwmod_register(h);
		if (r < 0)
			return r;
	}

	h = NULL;
	if (rev == OMAP3430_REV_ES1_0 || rev == OMAP3430_REV_ES2_0 ||
	    rev == OMAP3430_REV_ES2_1) {
		h = omap3430_pre_es3_hwmods;
	} else if (rev == OMAP3430_REV_ES3_0 || rev == OMAP3430_REV_ES3_1 ||
		   rev == OMAP3430_REV_ES3_1_2) {
		h = omap3430_es3plus_hwmods;
	};

	if (h)
		r = omap_hwmod_register(h);
	if (r < 0)
		return r;

	/*
	 * DSS code presumes that dss_core hwmod is handled first,
	 * _before_ any other DSS related hwmods so register common
	 * DSS hwmods last to ensure that dss_core is already registered.
	 * Otherwise some change things may happen, for ex. if dispc
	 * is handled before dss_core and DSS is enabled in bootloader
	 * DIPSC will be reset with outputs enabled which sometimes leads
	 * to unrecoverable L3 error.
	 * XXX The long-term fix to this is to ensure modules are set up
	 * in dependency order in the hwmod core code.
	 */
	r = omap_hwmod_register(omap3xxx_dss_hwmods);

	return r;
}
