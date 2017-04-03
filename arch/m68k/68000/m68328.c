/***************************************************************************/

/*
 *  m68328.c - 68328 specific config
 *
 *  Copyright (C) 1993 Hamish Macdonald
 *  Copyright (C) 1999 D. Jeff Dionne
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
#include <asm/MC68328.h>
#if defined(CONFIG_PILOT) || defined(CONFIG_INIT_LCD)
#include "bootlogo.h"
#endif

/***************************************************************************/

int m68328_hwclk(int set, struct rtc_time *t);

/***************************************************************************/

void m68328_reset (void)
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
  pr_info("68328 support D. Jeff Dionne <jeff@uclinux.org>\n");
  pr_info("68328 support Kenneth Albanowski <kjahds@kjshds.com>\n");
  pr_info("68328/Pilot support Bernhard Kuhn <kuhn@lpr.e-technik.tu-muenchen.de>\n");

  mach_hwclk = m68328_hwclk;
  mach_reset = m68328_reset;
}

/***************************************************************************/
