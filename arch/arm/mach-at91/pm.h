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

#include <mach/at91_ramc.h>
#ifdef CONFIG_ARCH_AT91RM9200
#include <mach/at91rm9200_sdramc.h>

/*
 * The AT91RM9200 goes into self-refresh mode with this command, and will
 * terminate self-refresh automatically on the next SDRAM access.
 *
 * Self-refresh mode is exited as soon as a memory access is made, but we don't
 * know for sure when that happens. However, we need to restore the low-power
 * mode if it was enabled before going idle. Restoring low-power mode while
 * still in self-refresh is "not recommended", but seems to work.
 */

static inline void at91rm9200_standby(void)
{
	u32 lpr = at91_ramc_read(0, AT91RM9200_SDRAMC_LPR);

	asm volatile(
		"b    1f\n\t"
		".align    5\n\t"
		"1:  mcr    p15, 0, %0, c7, c10, 4\n\t"
		"    str    %0, [%1, %2]\n\t"
		"    str    %3, [%1, %4]\n\t"
		"    mcr    p15, 0, %0, c7, c0, 4\n\t"
		"    str    %5, [%1, %2]"
		:
		: "r" (0), "r" (AT91_BASE_SYS), "r" (AT91RM9200_SDRAMC_LPR),
		  "r" (1), "r" (AT91RM9200_SDRAMC_SRR),
		  "r" (lpr));
}

#define at91_standby at91rm9200_standby

#elif defined(CONFIG_ARCH_AT91SAM9G45)

/* We manage both DDRAM/SDRAM controllers, we need more than one value to
 * remember.
 */
static inline void at91sam9g45_standby(void)
{
	/* Those two values allow us to delay self-refresh activation
	 * to the maximum. */
	u32 lpr0, lpr1;
	u32 saved_lpr0, saved_lpr1;

	saved_lpr1 = at91_ramc_read(1, AT91_DDRSDRC_LPR);
	lpr1 = saved_lpr1 & ~AT91_DDRSDRC_LPCB;
	lpr1 |= AT91_DDRSDRC_LPCB_SELF_REFRESH;

	saved_lpr0 = at91_ramc_read(0, AT91_DDRSDRC_LPR);
	lpr0 = saved_lpr0 & ~AT91_DDRSDRC_LPCB;
	lpr0 |= AT91_DDRSDRC_LPCB_SELF_REFRESH;

	/* self-refresh mode now */
	at91_ramc_write(0, AT91_DDRSDRC_LPR, lpr0);
	at91_ramc_write(1, AT91_DDRSDRC_LPR, lpr1);

	cpu_do_idle();

	at91_ramc_write(0, AT91_DDRSDRC_LPR, saved_lpr0);
	at91_ramc_write(1, AT91_DDRSDRC_LPR, saved_lpr1);
}

#define at91_standby at91sam9g45_standby

#else

#ifdef CONFIG_ARCH_AT91SAM9263
/*
 * FIXME either or both the SDRAM controllers (EB0, EB1) might be in use;
 * handle those cases both here and in the Suspend-To-RAM support.
 */
#warning Assuming EB1 SDRAM controller is *NOT* used
#endif

static inline void at91sam9_standby(void)
{
	u32 saved_lpr, lpr;

	saved_lpr = at91_ramc_read(0, AT91_SDRAMC_LPR);

	lpr = saved_lpr & ~AT91_SDRAMC_LPCB;
	at91_ramc_write(0, AT91_SDRAMC_LPR, lpr |
			AT91_SDRAMC_LPCB_SELF_REFRESH);

	cpu_do_idle();

	at91_ramc_write(0, AT91_SDRAMC_LPR, saved_lpr);
}

#define at91_standby at91sam9_standby

#endif

#endif
