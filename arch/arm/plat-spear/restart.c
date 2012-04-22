/*
 * arch/arm/plat-spear/restart.c
 *
 * SPEAr platform specific restart functions
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/io.h>
#include <asm/system_misc.h>
#include <asm/hardware/sp810.h>
#include <mach/spear.h>
#include <mach/generic.h>

void spear_restart(char mode, const char *cmd)
{
	if (mode == 's') {
		/* software reset, Jump into ROM at address 0 */
		soft_restart(0);
	} else {
		/* hardware reset, Use on-chip reset capability */
		sysctl_soft_reset((void __iomem *)VA_SPEAR_SYS_CTRL_BASE);
	}
}
