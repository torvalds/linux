/*
 * omap_hwmod_2430_data.c - hardware modules present on the OMAP2430 chips
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
#include <plat/omap_hwmod.h>
#include <mach/irqs.h>
#include <plat/cpu.h>
#include <plat/dma.h>
#include <plat/serial.h>
#include <plat/i2c.h>
#include <plat/gpio.h>
#include <plat/mcbsp.h>
#include <plat/mcspi.h>
#include <plat/dmtimer.h>
#include <plat/mmc.h>
#include <plat/l3_2xxx.h>

#include "omap_hwmod_common_data.h"

#include "prm-regbits-24xx.h"
#include "cm-regbits-24xx.h"
#include "wd_timer.h"

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
static struct omap_hwmod omap2430_dss_core_hwmod;
static struct omap_hwmod omap2430_dss_dispc_hwmod;
static struct omap_hwmod omap2430_dss_rfbi_hwmod;
static struct omap_hwmod omap2430_dss_venc_hwmod;
static struct omap_hwmod omap2430_wd_timer2_hwmod;
static struct omap_hwmod omap2430_gpio1_hwmod;
static struct omap_hwmod omap2430_gpio2_hwmod;
static struct omap_hwmod omap2430_gpio3_hwmod;
static struct omap_hwmod omap2430_gpio4_hwmod;
static struct omap_hwmod omap2430_gpio5_hwmod;
static struct omap_hwmod omap2430_dma_system_hwmod;
static struct omap_hwmod omap2430_mcbsp1_hwmod;
static struct omap_hwmod omap2430_mcbsp2_hwmod;
static struct omap_hwmod omap2430_mcbsp3_hwmod;
static struct omap_hwmod omap2430_mcbsp4_hwmod;
static struct omap_hwmod omap2430_mcbsp5_hwmod;
static struct omap_hwmod omap2430_mcspi1_hwmod;
static struct omap_hwmod omap2430_mcspi2_hwmod;
static struct omap_hwmod omap2430_mcspi3_hwmod;
static struct omap_hwmod omap2430_mmc1_hwmod;
static struct omap_hwmod omap2430_mmc2_hwmod;

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

/* DSS -> l3 */
static struct omap_hwmod_ocp_if omap2430_dss__l3 = {
	.master		= &omap2430_dss_core_hwmod,
	.slave		= &omap2430_l3_main_hwmod,
	.fw = {
		.omap2 = {
			.l3_perm_bit  = OMAP2_L3_CORE_FW_CONNID_DSS,
			.flags	= OMAP_FIREWALL_L3,
		}
	},
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
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
	.flags		= HWMOD_NO_IDLEST,
};

static struct omap_hwmod omap2430_l4_wkup_hwmod;
static struct omap_hwmod omap2430_uart1_hwmod;
static struct omap_hwmod omap2430_uart2_hwmod;
static struct omap_hwmod omap2430_uart3_hwmod;
static struct omap_hwmod omap2430_i2c1_hwmod;
static struct omap_hwmod omap2430_i2c2_hwmod;

static struct omap_hwmod omap2430_usbhsotg_hwmod;

/* l3_core -> usbhsotg  interface */
static struct omap_hwmod_ocp_if omap2430_usbhsotg__l3 = {
	.master		= &omap2430_usbhsotg_hwmod,
	.slave		= &omap2430_l3_main_hwmod,
	.clk		= "core_l3_ck",
	.user		= OCP_USER_MPU,
};

/* L4 CORE -> I2C1 interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__i2c1 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_i2c1_hwmod,
	.clk		= "i2c1_ick",
	.addr		= omap2_i2c1_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> I2C2 interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__i2c2 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_i2c2_hwmod,
	.clk		= "i2c2_ick",
	.addr		= omap2_i2c2_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4_CORE -> L4_WKUP interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__l4_wkup = {
	.master	= &omap2430_l4_core_hwmod,
	.slave	= &omap2430_l4_wkup_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> UART1 interface */
static struct omap_hwmod_ocp_if omap2_l4_core__uart1 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_uart1_hwmod,
	.clk		= "uart1_ick",
	.addr		= omap2xxx_uart1_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> UART2 interface */
static struct omap_hwmod_ocp_if omap2_l4_core__uart2 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_uart2_hwmod,
	.clk		= "uart2_ick",
	.addr		= omap2xxx_uart2_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 PER -> UART3 interface */
static struct omap_hwmod_ocp_if omap2_l4_core__uart3 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_uart3_hwmod,
	.clk		= "uart3_ick",
	.addr		= omap2xxx_uart3_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/*
* usbhsotg interface data
*/
static struct omap_hwmod_addr_space omap2430_usbhsotg_addrs[] = {
	{
		.pa_start	= OMAP243X_HS_BASE,
		.pa_end		= OMAP243X_HS_BASE + SZ_4K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/*  l4_core ->usbhsotg  interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__usbhsotg = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_usbhsotg_hwmod,
	.clk		= "usb_l4_ick",
	.addr		= omap2430_usbhsotg_addrs,
	.user		= OCP_USER_MPU,
};

static struct omap_hwmod_ocp_if *omap2430_usbhsotg_masters[] = {
	&omap2430_usbhsotg__l3,
};

static struct omap_hwmod_ocp_if *omap2430_usbhsotg_slaves[] = {
	&omap2430_l4_core__usbhsotg,
};

/* L4 CORE -> MMC1 interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__mmc1 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mmc1_hwmod,
	.clk		= "mmchs1_ick",
	.addr		= omap2430_mmc1_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 CORE -> MMC2 interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__mmc2 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mmc2_hwmod,
	.clk		= "mmchs2_ick",
	.addr		= omap2430_mmc2_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* Slave interfaces on the L4_CORE interconnect */
static struct omap_hwmod_ocp_if *omap2430_l4_core_slaves[] = {
	&omap2430_l3_main__l4_core,
};

/* Master interfaces on the L4_CORE interconnect */
static struct omap_hwmod_ocp_if *omap2430_l4_core_masters[] = {
	&omap2430_l4_core__l4_wkup,
	&omap2430_l4_core__mmc1,
	&omap2430_l4_core__mmc2,
};

/* L4 CORE */
static struct omap_hwmod omap2430_l4_core_hwmod = {
	.name		= "l4_core",
	.class		= &l4_hwmod_class,
	.masters	= omap2430_l4_core_masters,
	.masters_cnt	= ARRAY_SIZE(omap2430_l4_core_masters),
	.slaves		= omap2430_l4_core_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_l4_core_slaves),
	.flags		= HWMOD_NO_IDLEST,
};

/* Slave interfaces on the L4_WKUP interconnect */
static struct omap_hwmod_ocp_if *omap2430_l4_wkup_slaves[] = {
	&omap2430_l4_core__l4_wkup,
	&omap2_l4_core__uart1,
	&omap2_l4_core__uart2,
	&omap2_l4_core__uart3,
};

/* Master interfaces on the L4_WKUP interconnect */
static struct omap_hwmod_ocp_if *omap2430_l4_wkup_masters[] = {
};

/* l4 core -> mcspi1 interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcspi1 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mcspi1_hwmod,
	.clk		= "mcspi1_ick",
	.addr		= omap2_mcspi1_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi2 interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcspi2 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mcspi2_hwmod,
	.clk		= "mcspi2_ick",
	.addr		= omap2_mcspi2_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4 core -> mcspi3 interface */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcspi3 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mcspi3_hwmod,
	.clk		= "mcspi3_ick",
	.addr		= omap2430_mcspi3_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L4 WKUP */
static struct omap_hwmod omap2430_l4_wkup_hwmod = {
	.name		= "l4_wkup",
	.class		= &l4_hwmod_class,
	.masters	= omap2430_l4_wkup_masters,
	.masters_cnt	= ARRAY_SIZE(omap2430_l4_wkup_masters),
	.slaves		= omap2430_l4_wkup_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_l4_wkup_slaves),
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
static struct omap_hwmod omap2430_timer1_hwmod;

static struct omap_hwmod_addr_space omap2430_timer1_addrs[] = {
	{
		.pa_start	= 0x49018000,
		.pa_end		= 0x49018000 + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_wkup -> timer1 */
static struct omap_hwmod_ocp_if omap2430_l4_wkup__timer1 = {
	.master		= &omap2430_l4_wkup_hwmod,
	.slave		= &omap2430_timer1_hwmod,
	.clk		= "gpt1_ick",
	.addr		= omap2430_timer1_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer1 slave port */
static struct omap_hwmod_ocp_if *omap2430_timer1_slaves[] = {
	&omap2430_l4_wkup__timer1,
};

/* timer1 hwmod */
static struct omap_hwmod omap2430_timer1_hwmod = {
	.name		= "timer1",
	.mpu_irqs	= omap2_timer1_mpu_irqs,
	.main_clk	= "gpt1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT1_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT1_SHIFT,
		},
	},
	.dev_attr	= &capability_alwon_dev_attr,
	.slaves		= omap2430_timer1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_timer1_slaves),
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer2 */
static struct omap_hwmod omap2430_timer2_hwmod;

/* l4_core -> timer2 */
static struct omap_hwmod_ocp_if omap2430_l4_core__timer2 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_timer2_hwmod,
	.clk		= "gpt2_ick",
	.addr		= omap2xxx_timer2_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer2 slave port */
static struct omap_hwmod_ocp_if *omap2430_timer2_slaves[] = {
	&omap2430_l4_core__timer2,
};

/* timer2 hwmod */
static struct omap_hwmod omap2430_timer2_hwmod = {
	.name		= "timer2",
	.mpu_irqs	= omap2_timer2_mpu_irqs,
	.main_clk	= "gpt2_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT2_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT2_SHIFT,
		},
	},
	.dev_attr	= &capability_alwon_dev_attr,
	.slaves		= omap2430_timer2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_timer2_slaves),
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer3 */
static struct omap_hwmod omap2430_timer3_hwmod;

/* l4_core -> timer3 */
static struct omap_hwmod_ocp_if omap2430_l4_core__timer3 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_timer3_hwmod,
	.clk		= "gpt3_ick",
	.addr		= omap2xxx_timer3_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer3 slave port */
static struct omap_hwmod_ocp_if *omap2430_timer3_slaves[] = {
	&omap2430_l4_core__timer3,
};

/* timer3 hwmod */
static struct omap_hwmod omap2430_timer3_hwmod = {
	.name		= "timer3",
	.mpu_irqs	= omap2_timer3_mpu_irqs,
	.main_clk	= "gpt3_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT3_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT3_SHIFT,
		},
	},
	.dev_attr	= &capability_alwon_dev_attr,
	.slaves		= omap2430_timer3_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_timer3_slaves),
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer4 */
static struct omap_hwmod omap2430_timer4_hwmod;

/* l4_core -> timer4 */
static struct omap_hwmod_ocp_if omap2430_l4_core__timer4 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_timer4_hwmod,
	.clk		= "gpt4_ick",
	.addr		= omap2xxx_timer4_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer4 slave port */
static struct omap_hwmod_ocp_if *omap2430_timer4_slaves[] = {
	&omap2430_l4_core__timer4,
};

/* timer4 hwmod */
static struct omap_hwmod omap2430_timer4_hwmod = {
	.name		= "timer4",
	.mpu_irqs	= omap2_timer4_mpu_irqs,
	.main_clk	= "gpt4_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT4_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT4_SHIFT,
		},
	},
	.dev_attr	= &capability_alwon_dev_attr,
	.slaves		= omap2430_timer4_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_timer4_slaves),
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer5 */
static struct omap_hwmod omap2430_timer5_hwmod;

/* l4_core -> timer5 */
static struct omap_hwmod_ocp_if omap2430_l4_core__timer5 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_timer5_hwmod,
	.clk		= "gpt5_ick",
	.addr		= omap2xxx_timer5_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer5 slave port */
static struct omap_hwmod_ocp_if *omap2430_timer5_slaves[] = {
	&omap2430_l4_core__timer5,
};

/* timer5 hwmod */
static struct omap_hwmod omap2430_timer5_hwmod = {
	.name		= "timer5",
	.mpu_irqs	= omap2_timer5_mpu_irqs,
	.main_clk	= "gpt5_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT5_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT5_SHIFT,
		},
	},
	.dev_attr	= &capability_alwon_dev_attr,
	.slaves		= omap2430_timer5_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_timer5_slaves),
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer6 */
static struct omap_hwmod omap2430_timer6_hwmod;

/* l4_core -> timer6 */
static struct omap_hwmod_ocp_if omap2430_l4_core__timer6 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_timer6_hwmod,
	.clk		= "gpt6_ick",
	.addr		= omap2xxx_timer6_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer6 slave port */
static struct omap_hwmod_ocp_if *omap2430_timer6_slaves[] = {
	&omap2430_l4_core__timer6,
};

/* timer6 hwmod */
static struct omap_hwmod omap2430_timer6_hwmod = {
	.name		= "timer6",
	.mpu_irqs	= omap2_timer6_mpu_irqs,
	.main_clk	= "gpt6_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT6_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT6_SHIFT,
		},
	},
	.dev_attr	= &capability_alwon_dev_attr,
	.slaves		= omap2430_timer6_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_timer6_slaves),
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer7 */
static struct omap_hwmod omap2430_timer7_hwmod;

/* l4_core -> timer7 */
static struct omap_hwmod_ocp_if omap2430_l4_core__timer7 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_timer7_hwmod,
	.clk		= "gpt7_ick",
	.addr		= omap2xxx_timer7_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer7 slave port */
static struct omap_hwmod_ocp_if *omap2430_timer7_slaves[] = {
	&omap2430_l4_core__timer7,
};

/* timer7 hwmod */
static struct omap_hwmod omap2430_timer7_hwmod = {
	.name		= "timer7",
	.mpu_irqs	= omap2_timer7_mpu_irqs,
	.main_clk	= "gpt7_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT7_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT7_SHIFT,
		},
	},
	.dev_attr	= &capability_alwon_dev_attr,
	.slaves		= omap2430_timer7_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_timer7_slaves),
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer8 */
static struct omap_hwmod omap2430_timer8_hwmod;

/* l4_core -> timer8 */
static struct omap_hwmod_ocp_if omap2430_l4_core__timer8 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_timer8_hwmod,
	.clk		= "gpt8_ick",
	.addr		= omap2xxx_timer8_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer8 slave port */
static struct omap_hwmod_ocp_if *omap2430_timer8_slaves[] = {
	&omap2430_l4_core__timer8,
};

/* timer8 hwmod */
static struct omap_hwmod omap2430_timer8_hwmod = {
	.name		= "timer8",
	.mpu_irqs	= omap2_timer8_mpu_irqs,
	.main_clk	= "gpt8_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT8_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT8_SHIFT,
		},
	},
	.dev_attr	= &capability_alwon_dev_attr,
	.slaves		= omap2430_timer8_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_timer8_slaves),
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer9 */
static struct omap_hwmod omap2430_timer9_hwmod;

/* l4_core -> timer9 */
static struct omap_hwmod_ocp_if omap2430_l4_core__timer9 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_timer9_hwmod,
	.clk		= "gpt9_ick",
	.addr		= omap2xxx_timer9_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer9 slave port */
static struct omap_hwmod_ocp_if *omap2430_timer9_slaves[] = {
	&omap2430_l4_core__timer9,
};

/* timer9 hwmod */
static struct omap_hwmod omap2430_timer9_hwmod = {
	.name		= "timer9",
	.mpu_irqs	= omap2_timer9_mpu_irqs,
	.main_clk	= "gpt9_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT9_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT9_SHIFT,
		},
	},
	.dev_attr	= &capability_pwm_dev_attr,
	.slaves		= omap2430_timer9_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_timer9_slaves),
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer10 */
static struct omap_hwmod omap2430_timer10_hwmod;

/* l4_core -> timer10 */
static struct omap_hwmod_ocp_if omap2430_l4_core__timer10 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_timer10_hwmod,
	.clk		= "gpt10_ick",
	.addr		= omap2_timer10_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer10 slave port */
static struct omap_hwmod_ocp_if *omap2430_timer10_slaves[] = {
	&omap2430_l4_core__timer10,
};

/* timer10 hwmod */
static struct omap_hwmod omap2430_timer10_hwmod = {
	.name		= "timer10",
	.mpu_irqs	= omap2_timer10_mpu_irqs,
	.main_clk	= "gpt10_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT10_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT10_SHIFT,
		},
	},
	.dev_attr	= &capability_pwm_dev_attr,
	.slaves		= omap2430_timer10_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_timer10_slaves),
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer11 */
static struct omap_hwmod omap2430_timer11_hwmod;

/* l4_core -> timer11 */
static struct omap_hwmod_ocp_if omap2430_l4_core__timer11 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_timer11_hwmod,
	.clk		= "gpt11_ick",
	.addr		= omap2_timer11_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer11 slave port */
static struct omap_hwmod_ocp_if *omap2430_timer11_slaves[] = {
	&omap2430_l4_core__timer11,
};

/* timer11 hwmod */
static struct omap_hwmod omap2430_timer11_hwmod = {
	.name		= "timer11",
	.mpu_irqs	= omap2_timer11_mpu_irqs,
	.main_clk	= "gpt11_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT11_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT11_SHIFT,
		},
	},
	.dev_attr	= &capability_pwm_dev_attr,
	.slaves		= omap2430_timer11_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_timer11_slaves),
	.class		= &omap2xxx_timer_hwmod_class,
};

/* timer12 */
static struct omap_hwmod omap2430_timer12_hwmod;

/* l4_core -> timer12 */
static struct omap_hwmod_ocp_if omap2430_l4_core__timer12 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_timer12_hwmod,
	.clk		= "gpt12_ick",
	.addr		= omap2xxx_timer12_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* timer12 slave port */
static struct omap_hwmod_ocp_if *omap2430_timer12_slaves[] = {
	&omap2430_l4_core__timer12,
};

/* timer12 hwmod */
static struct omap_hwmod omap2430_timer12_hwmod = {
	.name		= "timer12",
	.mpu_irqs	= omap2xxx_timer12_mpu_irqs,
	.main_clk	= "gpt12_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPT12_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPT12_SHIFT,
		},
	},
	.dev_attr	= &capability_pwm_dev_attr,
	.slaves		= omap2430_timer12_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_timer12_slaves),
	.class		= &omap2xxx_timer_hwmod_class,
};

/* l4_wkup -> wd_timer2 */
static struct omap_hwmod_addr_space omap2430_wd_timer2_addrs[] = {
	{
		.pa_start	= 0x49016000,
		.pa_end		= 0x4901607f,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap2430_l4_wkup__wd_timer2 = {
	.master		= &omap2430_l4_wkup_hwmod,
	.slave		= &omap2430_wd_timer2_hwmod,
	.clk		= "mpu_wdt_ick",
	.addr		= omap2430_wd_timer2_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* wd_timer2 */
static struct omap_hwmod_ocp_if *omap2430_wd_timer2_slaves[] = {
	&omap2430_l4_wkup__wd_timer2,
};

static struct omap_hwmod omap2430_wd_timer2_hwmod = {
	.name		= "wd_timer2",
	.class		= &omap2xxx_wd_timer_hwmod_class,
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
};

/* UART1 */

static struct omap_hwmod_ocp_if *omap2430_uart1_slaves[] = {
	&omap2_l4_core__uart1,
};

static struct omap_hwmod omap2430_uart1_hwmod = {
	.name		= "uart1",
	.mpu_irqs	= omap2_uart1_mpu_irqs,
	.sdma_reqs	= omap2_uart1_sdma_reqs,
	.main_clk	= "uart1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_UART1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_EN_UART1_SHIFT,
		},
	},
	.slaves		= omap2430_uart1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_uart1_slaves),
	.class		= &omap2_uart_class,
};

/* UART2 */

static struct omap_hwmod_ocp_if *omap2430_uart2_slaves[] = {
	&omap2_l4_core__uart2,
};

static struct omap_hwmod omap2430_uart2_hwmod = {
	.name		= "uart2",
	.mpu_irqs	= omap2_uart2_mpu_irqs,
	.sdma_reqs	= omap2_uart2_sdma_reqs,
	.main_clk	= "uart2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_UART2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_EN_UART2_SHIFT,
		},
	},
	.slaves		= omap2430_uart2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_uart2_slaves),
	.class		= &omap2_uart_class,
};

/* UART3 */

static struct omap_hwmod_ocp_if *omap2430_uart3_slaves[] = {
	&omap2_l4_core__uart3,
};

static struct omap_hwmod omap2430_uart3_hwmod = {
	.name		= "uart3",
	.mpu_irqs	= omap2_uart3_mpu_irqs,
	.sdma_reqs	= omap2_uart3_sdma_reqs,
	.main_clk	= "uart3_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 2,
			.module_bit = OMAP24XX_EN_UART3_SHIFT,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP24XX_EN_UART3_SHIFT,
		},
	},
	.slaves		= omap2430_uart3_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_uart3_slaves),
	.class		= &omap2_uart_class,
};

/* dss */
/* dss master ports */
static struct omap_hwmod_ocp_if *omap2430_dss_masters[] = {
	&omap2430_dss__l3,
};

/* l4_core -> dss */
static struct omap_hwmod_ocp_if omap2430_l4_core__dss = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_dss_core_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2_dss_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dss slave ports */
static struct omap_hwmod_ocp_if *omap2430_dss_slaves[] = {
	&omap2430_l4_core__dss,
};

static struct omap_hwmod_opt_clk dss_opt_clks[] = {
	/*
	 * The DSS HW needs all DSS clocks enabled during reset. The dss_core
	 * driver does not use these clocks.
	 */
	{ .role = "tv_clk", .clk = "dss_54m_fck" },
	{ .role = "sys_clk", .clk = "dss2_fck" },
};

static struct omap_hwmod omap2430_dss_core_hwmod = {
	.name		= "dss_core",
	.class		= &omap2_dss_hwmod_class,
	.main_clk	= "dss1_fck", /* instead of dss_fck */
	.sdma_reqs	= omap2xxx_dss_sdma_chs,
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_DSS1_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_stdby_bit = OMAP24XX_ST_DSS_SHIFT,
		},
	},
	.opt_clks	= dss_opt_clks,
	.opt_clks_cnt = ARRAY_SIZE(dss_opt_clks),
	.slaves		= omap2430_dss_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_dss_slaves),
	.masters	= omap2430_dss_masters,
	.masters_cnt	= ARRAY_SIZE(omap2430_dss_masters),
	.flags		= HWMOD_NO_IDLEST | HWMOD_CONTROL_OPT_CLKS_IN_RESET,
};

/* l4_core -> dss_dispc */
static struct omap_hwmod_ocp_if omap2430_l4_core__dss_dispc = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_dss_dispc_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2_dss_dispc_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dss_dispc slave ports */
static struct omap_hwmod_ocp_if *omap2430_dss_dispc_slaves[] = {
	&omap2430_l4_core__dss_dispc,
};

static struct omap_hwmod omap2430_dss_dispc_hwmod = {
	.name		= "dss_dispc",
	.class		= &omap2_dispc_hwmod_class,
	.mpu_irqs	= omap2_dispc_irqs,
	.main_clk	= "dss1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_DSS1_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_stdby_bit = OMAP24XX_ST_DSS_SHIFT,
		},
	},
	.slaves		= omap2430_dss_dispc_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_dss_dispc_slaves),
	.flags		= HWMOD_NO_IDLEST,
	.dev_attr	= &omap2_3_dss_dispc_dev_attr
};

/* l4_core -> dss_rfbi */
static struct omap_hwmod_ocp_if omap2430_l4_core__dss_rfbi = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_dss_rfbi_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2_dss_rfbi_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dss_rfbi slave ports */
static struct omap_hwmod_ocp_if *omap2430_dss_rfbi_slaves[] = {
	&omap2430_l4_core__dss_rfbi,
};

static struct omap_hwmod_opt_clk dss_rfbi_opt_clks[] = {
	{ .role = "ick", .clk = "dss_ick" },
};

static struct omap_hwmod omap2430_dss_rfbi_hwmod = {
	.name		= "dss_rfbi",
	.class		= &omap2_rfbi_hwmod_class,
	.main_clk	= "dss1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_DSS1_SHIFT,
			.module_offs = CORE_MOD,
		},
	},
	.opt_clks	= dss_rfbi_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(dss_rfbi_opt_clks),
	.slaves		= omap2430_dss_rfbi_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_dss_rfbi_slaves),
	.flags		= HWMOD_NO_IDLEST,
};

/* l4_core -> dss_venc */
static struct omap_hwmod_ocp_if omap2430_l4_core__dss_venc = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_dss_venc_hwmod,
	.clk		= "dss_ick",
	.addr		= omap2_dss_venc_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dss_venc slave ports */
static struct omap_hwmod_ocp_if *omap2430_dss_venc_slaves[] = {
	&omap2430_l4_core__dss_venc,
};

static struct omap_hwmod omap2430_dss_venc_hwmod = {
	.name		= "dss_venc",
	.class		= &omap2_venc_hwmod_class,
	.main_clk	= "dss_54m_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_DSS1_SHIFT,
			.module_offs = CORE_MOD,
		},
	},
	.slaves		= omap2430_dss_venc_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_dss_venc_slaves),
	.flags		= HWMOD_NO_IDLEST,
};

/* I2C common */
static struct omap_hwmod_class_sysconfig i2c_sysc = {
	.rev_offs	= 0x00,
	.sysc_offs	= 0x20,
	.syss_offs	= 0x10,
	.sysc_flags	= (SYSC_HAS_SOFTRESET | SYSC_HAS_AUTOIDLE |
			   SYSS_HAS_RESET_STATUS),
	.sysc_fields	= &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class i2c_class = {
	.name		= "i2c",
	.sysc		= &i2c_sysc,
	.rev		= OMAP_I2C_IP_VERSION_1,
	.reset		= &omap_i2c_reset,
};

static struct omap_i2c_dev_attr i2c_dev_attr = {
	.fifo_depth	= 8, /* bytes */
	.flags		= OMAP_I2C_FLAG_APPLY_ERRATA_I207 |
			  OMAP_I2C_FLAG_BUS_SHIFT_2 |
			  OMAP_I2C_FLAG_FORCE_19200_INT_CLK,
};

/* I2C1 */

static struct omap_hwmod_ocp_if *omap2430_i2c1_slaves[] = {
	&omap2430_l4_core__i2c1,
};

static struct omap_hwmod omap2430_i2c1_hwmod = {
	.name		= "i2c1",
	.flags		= HWMOD_16BIT_REG,
	.mpu_irqs	= omap2_i2c1_mpu_irqs,
	.sdma_reqs	= omap2_i2c1_sdma_reqs,
	.main_clk	= "i2chs1_fck",
	.prcm		= {
		.omap2 = {
			/*
			 * NOTE: The CM_FCLKEN* and CM_ICLKEN* for
			 * I2CHS IP's do not follow the usual pattern.
			 * prcm_reg_id alone cannot be used to program
			 * the iclk and fclk. Needs to be handled using
			 * additional flags when clk handling is moved
			 * to hwmod framework.
			 */
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP2430_EN_I2CHS1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP2430_ST_I2CHS1_SHIFT,
		},
	},
	.slaves		= omap2430_i2c1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_i2c1_slaves),
	.class		= &i2c_class,
	.dev_attr	= &i2c_dev_attr,
};

/* I2C2 */

static struct omap_hwmod_ocp_if *omap2430_i2c2_slaves[] = {
	&omap2430_l4_core__i2c2,
};

static struct omap_hwmod omap2430_i2c2_hwmod = {
	.name		= "i2c2",
	.flags		= HWMOD_16BIT_REG,
	.mpu_irqs	= omap2_i2c2_mpu_irqs,
	.sdma_reqs	= omap2_i2c2_sdma_reqs,
	.main_clk	= "i2chs2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP2430_EN_I2CHS2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP2430_ST_I2CHS2_SHIFT,
		},
	},
	.slaves		= omap2430_i2c2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_i2c2_slaves),
	.class		= &i2c_class,
	.dev_attr	= &i2c_dev_attr,
};

/* l4_wkup -> gpio1 */
static struct omap_hwmod_addr_space omap2430_gpio1_addr_space[] = {
	{
		.pa_start	= 0x4900C000,
		.pa_end		= 0x4900C1ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap2430_l4_wkup__gpio1 = {
	.master		= &omap2430_l4_wkup_hwmod,
	.slave		= &omap2430_gpio1_hwmod,
	.clk		= "gpios_ick",
	.addr		= omap2430_gpio1_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> gpio2 */
static struct omap_hwmod_addr_space omap2430_gpio2_addr_space[] = {
	{
		.pa_start	= 0x4900E000,
		.pa_end		= 0x4900E1ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap2430_l4_wkup__gpio2 = {
	.master		= &omap2430_l4_wkup_hwmod,
	.slave		= &omap2430_gpio2_hwmod,
	.clk		= "gpios_ick",
	.addr		= omap2430_gpio2_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> gpio3 */
static struct omap_hwmod_addr_space omap2430_gpio3_addr_space[] = {
	{
		.pa_start	= 0x49010000,
		.pa_end		= 0x490101ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap2430_l4_wkup__gpio3 = {
	.master		= &omap2430_l4_wkup_hwmod,
	.slave		= &omap2430_gpio3_hwmod,
	.clk		= "gpios_ick",
	.addr		= omap2430_gpio3_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_wkup -> gpio4 */
static struct omap_hwmod_addr_space omap2430_gpio4_addr_space[] = {
	{
		.pa_start	= 0x49012000,
		.pa_end		= 0x490121ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap2430_l4_wkup__gpio4 = {
	.master		= &omap2430_l4_wkup_hwmod,
	.slave		= &omap2430_gpio4_hwmod,
	.clk		= "gpios_ick",
	.addr		= omap2430_gpio4_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l4_core -> gpio5 */
static struct omap_hwmod_addr_space omap2430_gpio5_addr_space[] = {
	{
		.pa_start	= 0x480B6000,
		.pa_end		= 0x480B61ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

static struct omap_hwmod_ocp_if omap2430_l4_core__gpio5 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_gpio5_hwmod,
	.clk		= "gpio5_ick",
	.addr		= omap2430_gpio5_addr_space,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* gpio dev_attr */
static struct omap_gpio_dev_attr gpio_dev_attr = {
	.bank_width = 32,
	.dbck_flag = false,
};

/* gpio1 */
static struct omap_hwmod_ocp_if *omap2430_gpio1_slaves[] = {
	&omap2430_l4_wkup__gpio1,
};

static struct omap_hwmod omap2430_gpio1_hwmod = {
	.name		= "gpio1",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2_gpio1_irqs,
	.main_clk	= "gpios_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPIOS_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_EN_GPIOS_SHIFT,
		},
	},
	.slaves		= omap2430_gpio1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_gpio1_slaves),
	.class		= &omap2xxx_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
};

/* gpio2 */
static struct omap_hwmod_ocp_if *omap2430_gpio2_slaves[] = {
	&omap2430_l4_wkup__gpio2,
};

static struct omap_hwmod omap2430_gpio2_hwmod = {
	.name		= "gpio2",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2_gpio2_irqs,
	.main_clk	= "gpios_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPIOS_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPIOS_SHIFT,
		},
	},
	.slaves		= omap2430_gpio2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_gpio2_slaves),
	.class		= &omap2xxx_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
};

/* gpio3 */
static struct omap_hwmod_ocp_if *omap2430_gpio3_slaves[] = {
	&omap2430_l4_wkup__gpio3,
};

static struct omap_hwmod omap2430_gpio3_hwmod = {
	.name		= "gpio3",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2_gpio3_irqs,
	.main_clk	= "gpios_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPIOS_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPIOS_SHIFT,
		},
	},
	.slaves		= omap2430_gpio3_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_gpio3_slaves),
	.class		= &omap2xxx_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
};

/* gpio4 */
static struct omap_hwmod_ocp_if *omap2430_gpio4_slaves[] = {
	&omap2430_l4_wkup__gpio4,
};

static struct omap_hwmod omap2430_gpio4_hwmod = {
	.name		= "gpio4",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2_gpio4_irqs,
	.main_clk	= "gpios_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_GPIOS_SHIFT,
			.module_offs = WKUP_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_GPIOS_SHIFT,
		},
	},
	.slaves		= omap2430_gpio4_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_gpio4_slaves),
	.class		= &omap2xxx_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
};

/* gpio5 */
static struct omap_hwmod_irq_info omap243x_gpio5_irqs[] = {
	{ .irq = 33 }, /* INT_24XX_GPIO_BANK5 */
	{ .irq = -1 }
};

static struct omap_hwmod_ocp_if *omap2430_gpio5_slaves[] = {
	&omap2430_l4_core__gpio5,
};

static struct omap_hwmod omap2430_gpio5_hwmod = {
	.name		= "gpio5",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap243x_gpio5_irqs,
	.main_clk	= "gpio5_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 2,
			.module_bit = OMAP2430_EN_GPIO5_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_GPIO5_SHIFT,
		},
	},
	.slaves		= omap2430_gpio5_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_gpio5_slaves),
	.class		= &omap2xxx_gpio_hwmod_class,
	.dev_attr	= &gpio_dev_attr,
};

/* dma attributes */
static struct omap_dma_dev_attr dma_dev_attr = {
	.dev_caps  = RESERVE_CHANNEL | DMA_LINKED_LCH | GLOBAL_PRIORITY |
				IS_CSSA_32 | IS_CDSA_32 | IS_RW_PRIORITY,
	.lch_count = 32,
};

/* dma_system -> L3 */
static struct omap_hwmod_ocp_if omap2430_dma_system__l3 = {
	.master		= &omap2430_dma_system_hwmod,
	.slave		= &omap2430_l3_main_hwmod,
	.clk		= "core_l3_ck",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dma_system master ports */
static struct omap_hwmod_ocp_if *omap2430_dma_system_masters[] = {
	&omap2430_dma_system__l3,
};

/* l4_core -> dma_system */
static struct omap_hwmod_ocp_if omap2430_l4_core__dma_system = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_dma_system_hwmod,
	.clk		= "sdma_ick",
	.addr		= omap2_dma_system_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* dma_system slave ports */
static struct omap_hwmod_ocp_if *omap2430_dma_system_slaves[] = {
	&omap2430_l4_core__dma_system,
};

static struct omap_hwmod omap2430_dma_system_hwmod = {
	.name		= "dma",
	.class		= &omap2xxx_dma_hwmod_class,
	.mpu_irqs	= omap2_dma_system_irqs,
	.main_clk	= "core_l3_ck",
	.slaves		= omap2430_dma_system_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_dma_system_slaves),
	.masters	= omap2430_dma_system_masters,
	.masters_cnt	= ARRAY_SIZE(omap2430_dma_system_masters),
	.dev_attr	= &dma_dev_attr,
	.flags		= HWMOD_NO_IDLEST,
};

/* mailbox */
static struct omap_hwmod omap2430_mailbox_hwmod;
static struct omap_hwmod_irq_info omap2430_mailbox_irqs[] = {
	{ .irq = 26 },
	{ .irq = -1 }
};

/* l4_core -> mailbox */
static struct omap_hwmod_ocp_if omap2430_l4_core__mailbox = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mailbox_hwmod,
	.addr		= omap2_mailbox_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mailbox slave ports */
static struct omap_hwmod_ocp_if *omap2430_mailbox_slaves[] = {
	&omap2430_l4_core__mailbox,
};

static struct omap_hwmod omap2430_mailbox_hwmod = {
	.name		= "mailbox",
	.class		= &omap2xxx_mailbox_hwmod_class,
	.mpu_irqs	= omap2430_mailbox_irqs,
	.main_clk	= "mailboxes_ick",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_MAILBOXES_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MAILBOXES_SHIFT,
		},
	},
	.slaves		= omap2430_mailbox_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mailbox_slaves),
};

/* mcspi1 */
static struct omap_hwmod_ocp_if *omap2430_mcspi1_slaves[] = {
	&omap2430_l4_core__mcspi1,
};

static struct omap2_mcspi_dev_attr omap_mcspi1_dev_attr = {
	.num_chipselect = 4,
};

static struct omap_hwmod omap2430_mcspi1_hwmod = {
	.name		= "mcspi1_hwmod",
	.mpu_irqs	= omap2_mcspi1_mpu_irqs,
	.sdma_reqs	= omap2_mcspi1_sdma_reqs,
	.main_clk	= "mcspi1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_MCSPI1_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MCSPI1_SHIFT,
		},
	},
	.slaves		= omap2430_mcspi1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mcspi1_slaves),
	.class		= &omap2xxx_mcspi_class,
	.dev_attr	= &omap_mcspi1_dev_attr,
};

/* mcspi2 */
static struct omap_hwmod_ocp_if *omap2430_mcspi2_slaves[] = {
	&omap2430_l4_core__mcspi2,
};

static struct omap2_mcspi_dev_attr omap_mcspi2_dev_attr = {
	.num_chipselect = 2,
};

static struct omap_hwmod omap2430_mcspi2_hwmod = {
	.name		= "mcspi2_hwmod",
	.mpu_irqs	= omap2_mcspi2_mpu_irqs,
	.sdma_reqs	= omap2_mcspi2_sdma_reqs,
	.main_clk	= "mcspi2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_MCSPI2_SHIFT,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MCSPI2_SHIFT,
		},
	},
	.slaves		= omap2430_mcspi2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mcspi2_slaves),
	.class		= &omap2xxx_mcspi_class,
	.dev_attr	= &omap_mcspi2_dev_attr,
};

/* mcspi3 */
static struct omap_hwmod_irq_info omap2430_mcspi3_mpu_irqs[] = {
	{ .irq = 91 },
	{ .irq = -1 }
};

static struct omap_hwmod_dma_info omap2430_mcspi3_sdma_reqs[] = {
	{ .name = "tx0", .dma_req = 15 }, /* DMA_SPI3_TX0 */
	{ .name = "rx0", .dma_req = 16 }, /* DMA_SPI3_RX0 */
	{ .name = "tx1", .dma_req = 23 }, /* DMA_SPI3_TX1 */
	{ .name = "rx1", .dma_req = 24 }, /* DMA_SPI3_RX1 */
	{ .dma_req = -1 }
};

static struct omap_hwmod_ocp_if *omap2430_mcspi3_slaves[] = {
	&omap2430_l4_core__mcspi3,
};

static struct omap2_mcspi_dev_attr omap_mcspi3_dev_attr = {
	.num_chipselect = 2,
};

static struct omap_hwmod omap2430_mcspi3_hwmod = {
	.name		= "mcspi3_hwmod",
	.mpu_irqs	= omap2430_mcspi3_mpu_irqs,
	.sdma_reqs	= omap2430_mcspi3_sdma_reqs,
	.main_clk	= "mcspi3_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 2,
			.module_bit = OMAP2430_EN_MCSPI3_SHIFT,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MCSPI3_SHIFT,
		},
	},
	.slaves		= omap2430_mcspi3_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mcspi3_slaves),
	.class		= &omap2xxx_mcspi_class,
	.dev_attr	= &omap_mcspi3_dev_attr,
};

/*
 * usbhsotg
 */
static struct omap_hwmod_class_sysconfig omap2430_usbhsotg_sysc = {
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
	.sysc = &omap2430_usbhsotg_sysc,
};

/* usb_otg_hs */
static struct omap_hwmod_irq_info omap2430_usbhsotg_mpu_irqs[] = {

	{ .name = "mc", .irq = 92 },
	{ .name = "dma", .irq = 93 },
	{ .irq = -1 }
};

static struct omap_hwmod omap2430_usbhsotg_hwmod = {
	.name		= "usb_otg_hs",
	.mpu_irqs	= omap2430_usbhsotg_mpu_irqs,
	.main_clk	= "usbhs_ick",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP2430_EN_USBHS_MASK,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP2430_ST_USBHS_SHIFT,
		},
	},
	.masters	= omap2430_usbhsotg_masters,
	.masters_cnt	= ARRAY_SIZE(omap2430_usbhsotg_masters),
	.slaves		= omap2430_usbhsotg_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_usbhsotg_slaves),
	.class		= &usbotg_class,
	/*
	 * Erratum ID: i479  idle_req / idle_ack mechanism potentially
	 * broken when autoidle is enabled
	 * workaround is to disable the autoidle bit at module level.
	 */
	.flags		= HWMOD_NO_OCP_AUTOIDLE | HWMOD_SWSUP_SIDLE
				| HWMOD_SWSUP_MSTANDBY,
};

/*
 * 'mcbsp' class
 * multi channel buffered serial port controller
 */

static struct omap_hwmod_class_sysconfig omap2430_mcbsp_sysc = {
	.rev_offs	= 0x007C,
	.sysc_offs	= 0x008C,
	.sysc_flags	= (SYSC_HAS_SOFTRESET),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap2430_mcbsp_hwmod_class = {
	.name = "mcbsp",
	.sysc = &omap2430_mcbsp_sysc,
	.rev  = MCBSP_CONFIG_TYPE2,
};

/* mcbsp1 */
static struct omap_hwmod_irq_info omap2430_mcbsp1_irqs[] = {
	{ .name = "tx",		.irq = 59 },
	{ .name = "rx",		.irq = 60 },
	{ .name = "ovr",	.irq = 61 },
	{ .name = "common",	.irq = 64 },
	{ .irq = -1 }
};

/* l4_core -> mcbsp1 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp1 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mcbsp1_hwmod,
	.clk		= "mcbsp1_ick",
	.addr		= omap2_mcbsp1_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mcbsp1 slave ports */
static struct omap_hwmod_ocp_if *omap2430_mcbsp1_slaves[] = {
	&omap2430_l4_core__mcbsp1,
};

static struct omap_hwmod omap2430_mcbsp1_hwmod = {
	.name		= "mcbsp1",
	.class		= &omap2430_mcbsp_hwmod_class,
	.mpu_irqs	= omap2430_mcbsp1_irqs,
	.sdma_reqs	= omap2_mcbsp1_sdma_reqs,
	.main_clk	= "mcbsp1_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_MCBSP1_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MCBSP1_SHIFT,
		},
	},
	.slaves		= omap2430_mcbsp1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mcbsp1_slaves),
};

/* mcbsp2 */
static struct omap_hwmod_irq_info omap2430_mcbsp2_irqs[] = {
	{ .name = "tx",		.irq = 62 },
	{ .name = "rx",		.irq = 63 },
	{ .name = "common",	.irq = 16 },
	{ .irq = -1 }
};

/* l4_core -> mcbsp2 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp2 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mcbsp2_hwmod,
	.clk		= "mcbsp2_ick",
	.addr		= omap2xxx_mcbsp2_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mcbsp2 slave ports */
static struct omap_hwmod_ocp_if *omap2430_mcbsp2_slaves[] = {
	&omap2430_l4_core__mcbsp2,
};

static struct omap_hwmod omap2430_mcbsp2_hwmod = {
	.name		= "mcbsp2",
	.class		= &omap2430_mcbsp_hwmod_class,
	.mpu_irqs	= omap2430_mcbsp2_irqs,
	.sdma_reqs	= omap2_mcbsp2_sdma_reqs,
	.main_clk	= "mcbsp2_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP24XX_EN_MCBSP2_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 1,
			.idlest_idle_bit = OMAP24XX_ST_MCBSP2_SHIFT,
		},
	},
	.slaves		= omap2430_mcbsp2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mcbsp2_slaves),
};

/* mcbsp3 */
static struct omap_hwmod_irq_info omap2430_mcbsp3_irqs[] = {
	{ .name = "tx",		.irq = 89 },
	{ .name = "rx",		.irq = 90 },
	{ .name = "common",	.irq = 17 },
	{ .irq = -1 }
};

static struct omap_hwmod_addr_space omap2430_mcbsp3_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x4808C000,
		.pa_end		= 0x4808C0ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_core -> mcbsp3 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp3 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mcbsp3_hwmod,
	.clk		= "mcbsp3_ick",
	.addr		= omap2430_mcbsp3_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mcbsp3 slave ports */
static struct omap_hwmod_ocp_if *omap2430_mcbsp3_slaves[] = {
	&omap2430_l4_core__mcbsp3,
};

static struct omap_hwmod omap2430_mcbsp3_hwmod = {
	.name		= "mcbsp3",
	.class		= &omap2430_mcbsp_hwmod_class,
	.mpu_irqs	= omap2430_mcbsp3_irqs,
	.sdma_reqs	= omap2_mcbsp3_sdma_reqs,
	.main_clk	= "mcbsp3_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP2430_EN_MCBSP3_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MCBSP3_SHIFT,
		},
	},
	.slaves		= omap2430_mcbsp3_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mcbsp3_slaves),
};

/* mcbsp4 */
static struct omap_hwmod_irq_info omap2430_mcbsp4_irqs[] = {
	{ .name = "tx",		.irq = 54 },
	{ .name = "rx",		.irq = 55 },
	{ .name = "common",	.irq = 18 },
	{ .irq = -1 }
};

static struct omap_hwmod_dma_info omap2430_mcbsp4_sdma_chs[] = {
	{ .name = "rx", .dma_req = 20 },
	{ .name = "tx", .dma_req = 19 },
	{ .dma_req = -1 }
};

static struct omap_hwmod_addr_space omap2430_mcbsp4_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x4808E000,
		.pa_end		= 0x4808E0ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_core -> mcbsp4 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp4 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mcbsp4_hwmod,
	.clk		= "mcbsp4_ick",
	.addr		= omap2430_mcbsp4_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mcbsp4 slave ports */
static struct omap_hwmod_ocp_if *omap2430_mcbsp4_slaves[] = {
	&omap2430_l4_core__mcbsp4,
};

static struct omap_hwmod omap2430_mcbsp4_hwmod = {
	.name		= "mcbsp4",
	.class		= &omap2430_mcbsp_hwmod_class,
	.mpu_irqs	= omap2430_mcbsp4_irqs,
	.sdma_reqs	= omap2430_mcbsp4_sdma_chs,
	.main_clk	= "mcbsp4_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP2430_EN_MCBSP4_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MCBSP4_SHIFT,
		},
	},
	.slaves		= omap2430_mcbsp4_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mcbsp4_slaves),
};

/* mcbsp5 */
static struct omap_hwmod_irq_info omap2430_mcbsp5_irqs[] = {
	{ .name = "tx",		.irq = 81 },
	{ .name = "rx",		.irq = 82 },
	{ .name = "common",	.irq = 19 },
	{ .irq = -1 }
};

static struct omap_hwmod_dma_info omap2430_mcbsp5_sdma_chs[] = {
	{ .name = "rx", .dma_req = 22 },
	{ .name = "tx", .dma_req = 21 },
	{ .dma_req = -1 }
};

static struct omap_hwmod_addr_space omap2430_mcbsp5_addrs[] = {
	{
		.name		= "mpu",
		.pa_start	= 0x48096000,
		.pa_end		= 0x480960ff,
		.flags		= ADDR_TYPE_RT
	},
	{ }
};

/* l4_core -> mcbsp5 */
static struct omap_hwmod_ocp_if omap2430_l4_core__mcbsp5 = {
	.master		= &omap2430_l4_core_hwmod,
	.slave		= &omap2430_mcbsp5_hwmod,
	.clk		= "mcbsp5_ick",
	.addr		= omap2430_mcbsp5_addrs,
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* mcbsp5 slave ports */
static struct omap_hwmod_ocp_if *omap2430_mcbsp5_slaves[] = {
	&omap2430_l4_core__mcbsp5,
};

static struct omap_hwmod omap2430_mcbsp5_hwmod = {
	.name		= "mcbsp5",
	.class		= &omap2430_mcbsp_hwmod_class,
	.mpu_irqs	= omap2430_mcbsp5_irqs,
	.sdma_reqs	= omap2430_mcbsp5_sdma_chs,
	.main_clk	= "mcbsp5_fck",
	.prcm		= {
		.omap2 = {
			.prcm_reg_id = 1,
			.module_bit = OMAP2430_EN_MCBSP5_SHIFT,
			.module_offs = CORE_MOD,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MCBSP5_SHIFT,
		},
	},
	.slaves		= omap2430_mcbsp5_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mcbsp5_slaves),
};

/* MMC/SD/SDIO common */

static struct omap_hwmod_class_sysconfig omap2430_mmc_sysc = {
	.rev_offs	= 0x1fc,
	.sysc_offs	= 0x10,
	.syss_offs	= 0x14,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_HAS_SIDLEMODE |
			   SYSC_HAS_ENAWAKEUP | SYSC_HAS_SOFTRESET |
			   SYSC_HAS_AUTOIDLE | SYSS_HAS_RESET_STATUS),
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_fields    = &omap_hwmod_sysc_type1,
};

static struct omap_hwmod_class omap2430_mmc_class = {
	.name = "mmc",
	.sysc = &omap2430_mmc_sysc,
};

/* MMC/SD/SDIO1 */

static struct omap_hwmod_irq_info omap2430_mmc1_mpu_irqs[] = {
	{ .irq = 83 },
	{ .irq = -1 }
};

static struct omap_hwmod_dma_info omap2430_mmc1_sdma_reqs[] = {
	{ .name = "tx",	.dma_req = 61 }, /* DMA_MMC1_TX */
	{ .name = "rx",	.dma_req = 62 }, /* DMA_MMC1_RX */
	{ .dma_req = -1 }
};

static struct omap_hwmod_opt_clk omap2430_mmc1_opt_clks[] = {
	{ .role = "dbck", .clk = "mmchsdb1_fck" },
};

static struct omap_hwmod_ocp_if *omap2430_mmc1_slaves[] = {
	&omap2430_l4_core__mmc1,
};

static struct omap_mmc_dev_attr mmc1_dev_attr = {
	.flags = OMAP_HSMMC_SUPPORTS_DUAL_VOLT,
};

static struct omap_hwmod omap2430_mmc1_hwmod = {
	.name		= "mmc1",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2430_mmc1_mpu_irqs,
	.sdma_reqs	= omap2430_mmc1_sdma_reqs,
	.opt_clks	= omap2430_mmc1_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap2430_mmc1_opt_clks),
	.main_clk	= "mmchs1_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 2,
			.module_bit  = OMAP2430_EN_MMCHS1_SHIFT,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MMCHS1_SHIFT,
		},
	},
	.dev_attr	= &mmc1_dev_attr,
	.slaves		= omap2430_mmc1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mmc1_slaves),
	.class		= &omap2430_mmc_class,
};

/* MMC/SD/SDIO2 */

static struct omap_hwmod_irq_info omap2430_mmc2_mpu_irqs[] = {
	{ .irq = 86 },
	{ .irq = -1 }
};

static struct omap_hwmod_dma_info omap2430_mmc2_sdma_reqs[] = {
	{ .name = "tx",	.dma_req = 47 }, /* DMA_MMC2_TX */
	{ .name = "rx",	.dma_req = 48 }, /* DMA_MMC2_RX */
	{ .dma_req = -1 }
};

static struct omap_hwmod_opt_clk omap2430_mmc2_opt_clks[] = {
	{ .role = "dbck", .clk = "mmchsdb2_fck" },
};

static struct omap_hwmod_ocp_if *omap2430_mmc2_slaves[] = {
	&omap2430_l4_core__mmc2,
};

static struct omap_hwmod omap2430_mmc2_hwmod = {
	.name		= "mmc2",
	.flags		= HWMOD_CONTROL_OPT_CLKS_IN_RESET,
	.mpu_irqs	= omap2430_mmc2_mpu_irqs,
	.sdma_reqs	= omap2430_mmc2_sdma_reqs,
	.opt_clks	= omap2430_mmc2_opt_clks,
	.opt_clks_cnt	= ARRAY_SIZE(omap2430_mmc2_opt_clks),
	.main_clk	= "mmchs2_fck",
	.prcm		= {
		.omap2 = {
			.module_offs = CORE_MOD,
			.prcm_reg_id = 2,
			.module_bit  = OMAP2430_EN_MMCHS2_SHIFT,
			.idlest_reg_id = 2,
			.idlest_idle_bit = OMAP2430_ST_MMCHS2_SHIFT,
		},
	},
	.slaves		= omap2430_mmc2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap2430_mmc2_slaves),
	.class		= &omap2430_mmc_class,
};

static __initdata struct omap_hwmod *omap2430_hwmods[] = {
	&omap2430_l3_main_hwmod,
	&omap2430_l4_core_hwmod,
	&omap2430_l4_wkup_hwmod,
	&omap2430_mpu_hwmod,
	&omap2430_iva_hwmod,

	&omap2430_timer1_hwmod,
	&omap2430_timer2_hwmod,
	&omap2430_timer3_hwmod,
	&omap2430_timer4_hwmod,
	&omap2430_timer5_hwmod,
	&omap2430_timer6_hwmod,
	&omap2430_timer7_hwmod,
	&omap2430_timer8_hwmod,
	&omap2430_timer9_hwmod,
	&omap2430_timer10_hwmod,
	&omap2430_timer11_hwmod,
	&omap2430_timer12_hwmod,

	&omap2430_wd_timer2_hwmod,
	&omap2430_uart1_hwmod,
	&omap2430_uart2_hwmod,
	&omap2430_uart3_hwmod,
	/* dss class */
	&omap2430_dss_core_hwmod,
	&omap2430_dss_dispc_hwmod,
	&omap2430_dss_rfbi_hwmod,
	&omap2430_dss_venc_hwmod,
	/* i2c class */
	&omap2430_i2c1_hwmod,
	&omap2430_i2c2_hwmod,
	&omap2430_mmc1_hwmod,
	&omap2430_mmc2_hwmod,

	/* gpio class */
	&omap2430_gpio1_hwmod,
	&omap2430_gpio2_hwmod,
	&omap2430_gpio3_hwmod,
	&omap2430_gpio4_hwmod,
	&omap2430_gpio5_hwmod,

	/* dma_system class*/
	&omap2430_dma_system_hwmod,

	/* mcbsp class */
	&omap2430_mcbsp1_hwmod,
	&omap2430_mcbsp2_hwmod,
	&omap2430_mcbsp3_hwmod,
	&omap2430_mcbsp4_hwmod,
	&omap2430_mcbsp5_hwmod,

	/* mailbox class */
	&omap2430_mailbox_hwmod,

	/* mcspi class */
	&omap2430_mcspi1_hwmod,
	&omap2430_mcspi2_hwmod,
	&omap2430_mcspi3_hwmod,

	/* usbotg class*/
	&omap2430_usbhsotg_hwmod,

	NULL,
};

int __init omap2430_hwmod_init(void)
{
	return omap_hwmod_register(omap2430_hwmods);
}
