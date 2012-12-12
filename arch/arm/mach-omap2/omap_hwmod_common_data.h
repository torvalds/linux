/*
 * omap_hwmod_common_data.h - OMAP hwmod common macros and declarations
 *
 * Copyright (C) 2010-2011 Nokia Corporation
 * Copyright (C) 2010-2012 Texas Instruments, Inc.
 * Paul Walmsley
 * Benoît Cousson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ARCH_ARM_MACH_OMAP2_OMAP_HWMOD_COMMON_DATA_H
#define __ARCH_ARM_MACH_OMAP2_OMAP_HWMOD_COMMON_DATA_H

#include <plat/omap_hwmod.h>

#include "common.h"
#include "display.h"

/* Common address space across OMAP2xxx */
extern struct omap_hwmod_addr_space omap2xxx_mcbsp2_addrs[];

/* Common address space across OMAP2xxx/3xxx */
extern struct omap_hwmod_addr_space omap2_i2c1_addr_space[];
extern struct omap_hwmod_addr_space omap2_i2c2_addr_space[];
extern struct omap_hwmod_addr_space omap2_dss_addrs[];
extern struct omap_hwmod_addr_space omap2_dss_dispc_addrs[];
extern struct omap_hwmod_addr_space omap2_dss_rfbi_addrs[];
extern struct omap_hwmod_addr_space omap2_dss_venc_addrs[];
extern struct omap_hwmod_addr_space omap2_timer10_addrs[];
extern struct omap_hwmod_addr_space omap2_timer11_addrs[];
extern struct omap_hwmod_addr_space omap2430_mmc1_addr_space[];
extern struct omap_hwmod_addr_space omap2430_mmc2_addr_space[];
extern struct omap_hwmod_addr_space omap2_mcspi1_addr_space[];
extern struct omap_hwmod_addr_space omap2_mcspi2_addr_space[];
extern struct omap_hwmod_addr_space omap2430_mcspi3_addr_space[];
extern struct omap_hwmod_addr_space omap2_dma_system_addrs[];
extern struct omap_hwmod_addr_space omap2_mailbox_addrs[];
extern struct omap_hwmod_addr_space omap2_mcbsp1_addrs[];
extern struct omap_hwmod_addr_space omap2_hdq1w_addr_space[];

/* Common IP block data across OMAP2xxx */
extern struct omap_hwmod_irq_info omap2xxx_timer12_mpu_irqs[];
extern struct omap_hwmod_dma_info omap2xxx_dss_sdma_chs[];
extern struct omap_gpio_dev_attr omap2xxx_gpio_dev_attr;
extern struct omap_hwmod omap2xxx_l3_main_hwmod;
extern struct omap_hwmod omap2xxx_l4_core_hwmod;
extern struct omap_hwmod omap2xxx_l4_wkup_hwmod;
extern struct omap_hwmod omap2xxx_mpu_hwmod;
extern struct omap_hwmod omap2xxx_iva_hwmod;
extern struct omap_hwmod omap2xxx_timer1_hwmod;
extern struct omap_hwmod omap2xxx_timer2_hwmod;
extern struct omap_hwmod omap2xxx_timer3_hwmod;
extern struct omap_hwmod omap2xxx_timer4_hwmod;
extern struct omap_hwmod omap2xxx_timer5_hwmod;
extern struct omap_hwmod omap2xxx_timer6_hwmod;
extern struct omap_hwmod omap2xxx_timer7_hwmod;
extern struct omap_hwmod omap2xxx_timer8_hwmod;
extern struct omap_hwmod omap2xxx_timer9_hwmod;
extern struct omap_hwmod omap2xxx_timer10_hwmod;
extern struct omap_hwmod omap2xxx_timer11_hwmod;
extern struct omap_hwmod omap2xxx_timer12_hwmod;
extern struct omap_hwmod omap2xxx_wd_timer2_hwmod;
extern struct omap_hwmod omap2xxx_uart1_hwmod;
extern struct omap_hwmod omap2xxx_uart2_hwmod;
extern struct omap_hwmod omap2xxx_uart3_hwmod;
extern struct omap_hwmod omap2xxx_dss_core_hwmod;
extern struct omap_hwmod omap2xxx_dss_dispc_hwmod;
extern struct omap_hwmod omap2xxx_dss_rfbi_hwmod;
extern struct omap_hwmod omap2xxx_dss_venc_hwmod;
extern struct omap_hwmod omap2xxx_gpio1_hwmod;
extern struct omap_hwmod omap2xxx_gpio2_hwmod;
extern struct omap_hwmod omap2xxx_gpio3_hwmod;
extern struct omap_hwmod omap2xxx_gpio4_hwmod;
extern struct omap_hwmod omap2xxx_mcspi1_hwmod;
extern struct omap_hwmod omap2xxx_mcspi2_hwmod;
extern struct omap_hwmod omap2xxx_counter_32k_hwmod;
extern struct omap_hwmod omap2xxx_gpmc_hwmod;
extern struct omap_hwmod omap2xxx_rng_hwmod;

/* Common interface data across OMAP2xxx */
extern struct omap_hwmod_ocp_if omap2xxx_l3_main__l4_core;
extern struct omap_hwmod_ocp_if omap2xxx_mpu__l3_main;
extern struct omap_hwmod_ocp_if omap2xxx_dss__l3;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__l4_wkup;
extern struct omap_hwmod_ocp_if omap2_l4_core__uart1;
extern struct omap_hwmod_ocp_if omap2_l4_core__uart2;
extern struct omap_hwmod_ocp_if omap2_l4_core__uart3;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__mcspi1;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__mcspi2;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__timer2;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__timer3;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__timer4;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__timer5;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__timer6;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__timer7;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__timer8;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__timer9;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__timer10;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__timer11;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__timer12;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__dss;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__dss_dispc;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__dss_rfbi;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__dss_venc;
extern struct omap_hwmod_ocp_if omap2xxx_l4_core__rng;

/* Common IP block data */
extern struct omap_hwmod_dma_info omap2_uart1_sdma_reqs[];
extern struct omap_hwmod_dma_info omap2_uart2_sdma_reqs[];
extern struct omap_hwmod_dma_info omap2_uart3_sdma_reqs[];
extern struct omap_hwmod_dma_info omap2_i2c1_sdma_reqs[];
extern struct omap_hwmod_dma_info omap2_i2c2_sdma_reqs[];
extern struct omap_hwmod_dma_info omap2_mcspi1_sdma_reqs[];
extern struct omap_hwmod_dma_info omap2_mcspi2_sdma_reqs[];
extern struct omap_hwmod_dma_info omap2_mcbsp1_sdma_reqs[];
extern struct omap_hwmod_dma_info omap2_mcbsp2_sdma_reqs[];

/* Common IP block data on OMAP2430/OMAP3 */
extern struct omap_hwmod_dma_info omap2_mcbsp3_sdma_reqs[];

/* Common IP block data across OMAP2/3 */
extern struct omap_hwmod_irq_info omap2_timer1_mpu_irqs[];
extern struct omap_hwmod_irq_info omap2_timer2_mpu_irqs[];
extern struct omap_hwmod_irq_info omap2_timer3_mpu_irqs[];
extern struct omap_hwmod_irq_info omap2_timer4_mpu_irqs[];
extern struct omap_hwmod_irq_info omap2_timer5_mpu_irqs[];
extern struct omap_hwmod_irq_info omap2_timer6_mpu_irqs[];
extern struct omap_hwmod_irq_info omap2_timer7_mpu_irqs[];
extern struct omap_hwmod_irq_info omap2_timer8_mpu_irqs[];
extern struct omap_hwmod_irq_info omap2_timer9_mpu_irqs[];
extern struct omap_hwmod_irq_info omap2_timer10_mpu_irqs[];
extern struct omap_hwmod_irq_info omap2_timer11_mpu_irqs[];
extern struct omap_hwmod_irq_info omap2_uart1_mpu_irqs[];
extern struct omap_hwmod_irq_info omap2_uart2_mpu_irqs[];
extern struct omap_hwmod_irq_info omap2_uart3_mpu_irqs[];
extern struct omap_hwmod_irq_info omap2_dispc_irqs[];
extern struct omap_hwmod_irq_info omap2_i2c1_mpu_irqs[];
extern struct omap_hwmod_irq_info omap2_i2c2_mpu_irqs[];
extern struct omap_hwmod_irq_info omap2_gpio1_irqs[];
extern struct omap_hwmod_irq_info omap2_gpio2_irqs[];
extern struct omap_hwmod_irq_info omap2_gpio3_irqs[];
extern struct omap_hwmod_irq_info omap2_gpio4_irqs[];
extern struct omap_hwmod_irq_info omap2_dma_system_irqs[];
extern struct omap_hwmod_irq_info omap2_mcspi1_mpu_irqs[];
extern struct omap_hwmod_irq_info omap2_mcspi2_mpu_irqs[];
extern struct omap_hwmod_addr_space omap2xxx_timer12_addrs[];
extern struct omap_hwmod_irq_info omap2_hdq1w_mpu_irqs[];

/* OMAP hwmod classes - forward declarations */
extern struct omap_hwmod_class l3_hwmod_class;
extern struct omap_hwmod_class l4_hwmod_class;
extern struct omap_hwmod_class mpu_hwmod_class;
extern struct omap_hwmod_class iva_hwmod_class;
extern struct omap_hwmod_class omap2_uart_class;
extern struct omap_hwmod_class omap2_dss_hwmod_class;
extern struct omap_hwmod_class omap2_dispc_hwmod_class;
extern struct omap_hwmod_class omap2_rfbi_hwmod_class;
extern struct omap_hwmod_class omap2_venc_hwmod_class;
extern struct omap_hwmod_class_sysconfig omap2_hdq1w_sysc;
extern struct omap_hwmod_class omap2_hdq1w_class;

extern struct omap_hwmod_class omap2xxx_timer_hwmod_class;
extern struct omap_hwmod_class omap2xxx_wd_timer_hwmod_class;
extern struct omap_hwmod_class omap2xxx_gpio_hwmod_class;
extern struct omap_hwmod_class omap2xxx_dma_hwmod_class;
extern struct omap_hwmod_class omap2xxx_mailbox_hwmod_class;
extern struct omap_hwmod_class omap2xxx_mcspi_class;

extern struct omap_dss_dispc_dev_attr omap2_3_dss_dispc_dev_attr;

#endif
