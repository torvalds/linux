/***************************************************************************/

/*
 *  m68VZ328.c - 68VZ328 specific config
 *
 *  Copyright (C) 1993 Hamish Macdonald
 *  Copyright (C) 1999 D. Jeff Dionne
 *  Copyright (C) 2001 Georges Menie, Ken Desmet
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/***************************************************************************/

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/kd.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/rtc.h>
#include <linux/pgtable.h>

#include <asm/machdep.h>
#include <asm/MC68VZ328.h>
#include <asm/bootstd.h>

#ifdef CONFIG_INIT_LCD
#include "bootlogo-vz.h"
#endif

#include "m68328.h"

/***************************************************************************/
static void m68vz328_reset(void)
{
	local_irq_disable();
	asm volatile (
		"moveal #0x10c00000, %a0;\n\t"
		"moveb #0, 0xFFFFF300;\n\t"
		"moveal 0(%a0), %sp;\n\t"
		"moveal 4(%a0), %a0;\n\t"
		"jmp (%a0);\n"
	);
}

/***************************************************************************/

void __init config_BSP(char *command, int size)
{
	pr_info("68VZ328 DragonBallVZ support (c) 2001 Lineo, Inc.\n");

	mach_sched_init = hw_timer_init;
	mach_hwclk = m68328_hwclk;
	mach_reset = m68vz328_reset;

#ifdef CONFIG_UCDIMM
	init_ucsimm(command, len);
#elif defined(CONFIG_DRAGEN2)
	init_dragen2(command, len);
#endif
}

/***************************************************************************/
