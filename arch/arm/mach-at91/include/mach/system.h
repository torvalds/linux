/*
 * arch/arm/mach-at91/include/mach/system.h
 *
 *  Copyright (C) 2003 SAN People
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <mach/hardware.h>
#include <mach/at91_st.h>
#include <mach/at91_dbgu.h>
#include <mach/at91_pmc.h>

static inline void arch_idle(void)
{
	/*
	 * Disable the processor clock.  The processor will be automatically
	 * re-enabled by an interrupt or by a reset.
	 */
#ifdef AT91_PS
	at91_sys_write(AT91_PS_CR, AT91_PS_CR_CPU);
#else
	at91_sys_write(AT91_PMC_SCDR, AT91_PMC_PCK);
#endif
#ifndef CONFIG_CPU_ARM920T
	/*
	 * Set the processor (CP15) into 'Wait for Interrupt' mode.
	 * Post-RM9200 processors need this in conjunction with the above
	 * to save power when idle.
	 */
	cpu_do_idle();
#endif
}

void (*at91_arch_reset)(void);

static inline void arch_reset(char mode, const char *cmd)
{
	/* call the CPU-specific reset function */
	if (at91_arch_reset)
		(at91_arch_reset)();
}

#endif
