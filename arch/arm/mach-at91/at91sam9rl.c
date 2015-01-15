/*
 * arch/arm/mach-at91/at91sam9rl.c
 *
 *  Copyright (C) 2005 SAN People
 *  Copyright (C) 2007 Atmel Corporation
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <asm/system_misc.h>
#include <asm/irq.h>
#include <mach/cpu.h>
#include <mach/at91_dbgu.h>
#include <mach/hardware.h>

#include "soc.h"
#include "generic.h"

/* --------------------------------------------------------------------
 *  AT91SAM9RL processor initialization
 * -------------------------------------------------------------------- */

static void __init at91sam9rl_map_io(void)
{
	unsigned long sram_size;

	switch (at91_soc_initdata.cidr & AT91_CIDR_SRAMSIZ) {
		case AT91_CIDR_SRAMSIZ_32K:
			sram_size = 2 * SZ_16K;
			break;
		case AT91_CIDR_SRAMSIZ_16K:
		default:
			sram_size = SZ_16K;
	}

	/* Map SRAM */
	at91_init_sram(0, AT91SAM9RL_SRAM_BASE, sram_size);
}

static void __init at91sam9rl_initialize(void)
{
	arm_pm_idle = at91sam9_idle;

	at91_sysirq_mask_rtc(AT91SAM9RL_BASE_RTC);
	at91_sysirq_mask_rtt(AT91SAM9RL_BASE_RTT);
}

AT91_SOC_START(at91sam9rl)
	.map_io = at91sam9rl_map_io,
	.init = at91sam9rl_initialize,
AT91_SOC_END
