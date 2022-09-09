// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/plat-spear/restart.c
 *
 * SPEAr platform specific restart functions
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar <vireshk@kernel.org>
 */
#include <linux/io.h>
#include <linux/amba/sp810.h>
#include <linux/reboot.h>
#include <asm/system_misc.h>
#include "spear.h"
#include "generic.h"

#define SPEAR13XX_SYS_SW_RES			(VA_MISC_BASE + 0x204)
void spear_restart(enum reboot_mode mode, const char *cmd)
{
	if (mode == REBOOT_SOFT) {
		/* software reset, Jump into ROM at address 0 */
		soft_restart(0);
	} else {
		/* hardware reset, Use on-chip reset capability */
#ifdef CONFIG_ARCH_SPEAR13XX
		writel_relaxed(0x01, SPEAR13XX_SYS_SW_RES);
#endif
#if defined(CONFIG_ARCH_SPEAR3XX) || defined(CONFIG_ARCH_SPEAR6XX)
		sysctl_soft_reset((void __iomem *)VA_SPEAR_SYS_CTRL_BASE);
#endif
	}
}
