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
#include <mach/cpu.h>
#include <mach/at91_dbgu.h>
#include <mach/hardware.h>

#include "soc.h"
#include "generic.h"

/* --------------------------------------------------------------------
 *  AT91SAM9RL processor initialization
 * -------------------------------------------------------------------- */

static void __init at91sam9rl_initialize(void)
{
	arm_pm_idle = at91sam9_idle;
}

AT91_SOC_START(at91sam9rl)
	.init = at91sam9rl_initialize,
AT91_SOC_END
