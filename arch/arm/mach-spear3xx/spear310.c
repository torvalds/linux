/*
 * arch/arm/mach-spear3xx/spear310.c
 *
 * SPEAr310 machine source file
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/ptrace.h>
#include <asm/irq.h>
#include <mach/generic.h>
#include <mach/spear.h>

/* Add spear310 specific devices here */

void __init spear310_init(void)
{
	/* call spear3xx family common init function */
	spear3xx_init();
}
