/*
 * arch/arm/mach-spear3xx/generic.h
 *
 * SPEAr3XX machine family generic header file
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MACH_GENERIC_H
#define __MACH_GENERIC_H

#include <linux/amba/pl08x.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>

/* spear3xx declarations */
/*
 * Each GPT has 2 timer channels
 * Following GPT channels will be used as clock source and clockevent
 */
#define SPEAR_GPT0_BASE		SPEAR3XX_ML1_TMR_BASE
#define SPEAR_GPT0_CHAN0_IRQ	SPEAR3XX_IRQ_CPU_GPT1_1
#define SPEAR_GPT0_CHAN1_IRQ	SPEAR3XX_IRQ_CPU_GPT1_2

/* Add spear3xx family device structure declarations here */
extern struct sys_timer spear3xx_timer;
extern struct pl022_ssp_controller pl022_plat_data;
extern struct pl08x_platform_data pl080_plat_data;

/* Add spear3xx family function declarations here */
void __init spear_setup_timer(void);
void __init spear3xx_clk_init(void);
void __init spear3xx_map_io(void);
void __init spear3xx_dt_init_irq(void);

void spear_restart(char, const char *);

#endif /* __MACH_GENERIC_H */
