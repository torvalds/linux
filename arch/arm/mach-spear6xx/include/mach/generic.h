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

void __init spear_setup_of_timer(void);
void spear_restart(char, const char *);
void __init spear6xx_clk_init(void);

#endif /* __MACH_GENERIC_H */
