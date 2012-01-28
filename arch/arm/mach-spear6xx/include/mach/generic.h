/*
 * arch/arm/mach-spear6xx/include/mach/generic.h
 *
 * SPEAr6XX machine family specific generic header file
 *
 * Copyright (C) 2009 ST Microelectronics
 * Rajeev Kumar<rajeev-dlh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MACH_GENERIC_H
#define __MACH_GENERIC_H

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>

/*
 * Each GPT has 2 timer channels
 * Following GPT channels will be used as clock source and clockevent
 */
#define SPEAR_GPT0_BASE		SPEAR6XX_CPU_TMR_BASE
#define SPEAR_GPT0_CHAN0_IRQ	IRQ_CPU_GPT1_1
#define SPEAR_GPT0_CHAN1_IRQ	IRQ_CPU_GPT1_2

/* Add spear6xx family device structure declarations here */
extern struct amba_device gpio_device[];
extern struct amba_device uart_device[];
extern struct sys_timer spear6xx_timer;

/* Add spear6xx family function declarations here */
void __init spear_setup_timer(void);
void __init spear6xx_map_io(void);
void __init spear6xx_init_irq(void);
void __init spear6xx_init(void);
void __init spear600_init(void);
void __init spear6xx_clk_init(void);

void spear_restart(char, const char *);

/* Add spear600 machine device structure declarations here */

#endif /* __MACH_GENERIC_H */
