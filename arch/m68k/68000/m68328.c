/***************************************************************************/

/*
 *  m68328.c - 68328/68EZ328/68VZ328 specific config
 *
 *  Copyright (C) 1993 Hamish Macdonald
 *  Copyright (C) 1999 D. Jeff Dionne
 *  Copyright (C) 2001 Georges Menie, Ken Desmet
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * VZ Support/Fixes             Evan Stawnyczy <e@lineo.ca>
 */

/***************************************************************************/

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/rtc.h>
#include <asm/machdep.h>

#if defined(CONFIG_INIT_LCD) && defined(CONFIG_M68VZ328)
#include "bootlogo-vz.h"
#elif defined(CONFIG_PILOT) || defined(CONFIG_INIT_LCD)
#include "bootlogo.h"
#endif

#include "m68328.h"

/***************************************************************************/

static void m68328_reset(void)
{
  local_irq_disable();
  asm volatile ("moveal #0x10c00000, %a0;\n\t"
		"moveb #0, 0xFFFFF300;\n\t"
		"moveal 0(%a0), %sp;\n\t"
		"moveal 4(%a0), %a0;\n\t"
		"jmp (%a0);");
}

/***************************************************************************/

void __init config_BSP(char *command, int len)
{
	mach_sched_init = hw_timer_init;
	mach_hwclk = m68328_hwclk;
	mach_reset = m68328_reset;

#if   defined(CONFIG_PILOT) && defined(CONFIG_M68328)
	mach_sched_init = NULL;
#elif defined(CONFIG_UCSIMM)
	init_ucsimm(command, len);
#elif defined(CONFIG_UCDIMM)
	init_ucsimm(command, len);
#elif defined(CONFIG_DRAGEN2)
	init_dragen2(command, len);
#endif
}

/***************************************************************************/
