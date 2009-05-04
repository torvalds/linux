/*
 * arch/arm/mach-ixp23xx/include/mach/system.h
 *
 * Copyright (C) 2003 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <mach/hardware.h>
#include <asm/mach-types.h>

static inline void arch_idle(void)
{
#if 0
	if (!hlt_counter)
		cpu_do_idle();
#endif
}

static inline void arch_reset(char mode, const char *cmd)
{
	/* First try machine specific support */
	if (machine_is_ixdp2351()) {
		*IXDP2351_CPLD_RESET1_REG = IXDP2351_CPLD_RESET1_MAGIC;
		(void) *IXDP2351_CPLD_RESET1_REG;
		*IXDP2351_CPLD_RESET1_REG = IXDP2351_CPLD_RESET1_ENABLE;
	}

	/* Use on-chip reset capability */
	*IXP23XX_RESET0 |= IXP23XX_RST_ALL;
}
