/*
 * arch/arm/mach-spear6xx/spear600.c
 *
 * SPEAr600 machine source file
 *
 * Copyright (C) 2009 ST Microelectronics
 * Rajeev Kumar<rajeev-dlh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/ptrace.h>
#include <asm/irq.h>
#include <mach/generic.h>
#include <mach/spear.h>

/* Add spear600 specific devices here */

void __init spear600_init(void)
{
	/* call spear6xx family common init function */
	spear6xx_init();
}
