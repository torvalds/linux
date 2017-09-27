/*
 * AT91 Power Management
 *
 * Copyright (C) 2005 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __ARCH_ARM_MACH_AT91_PM
#define __ARCH_ARM_MACH_AT91_PM

#include <asm/proc-fns.h>

#include <linux/mfd/syscon/atmel-mc.h>
#include <soc/at91/at91sam9_ddrsdr.h>
#include <soc/at91/at91sam9_sdramc.h>

#define AT91_MEMCTRL_MC		0
#define AT91_MEMCTRL_SDRAMC	1
#define AT91_MEMCTRL_DDRSDR	2

#define	AT91_PM_SLOW_CLOCK	0x01
#define	AT91_PM_BACKUP		0x02

#ifndef __ASSEMBLY__
struct at91_pm_data {
	void __iomem *pmc;
	void __iomem *ramc[2];
	unsigned long uhp_udp_mask;
	unsigned int memctrl;
	unsigned int mode;
	void __iomem *shdwc;
	void __iomem *sfrbu;
	unsigned int standby_mode;
	unsigned int suspend_mode;
};
#endif

#endif
