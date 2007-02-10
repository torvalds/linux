/***************************************************************************/

/*
 *  linux/arch/m68knommu/platform/68328/config.c
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

#include <asm/dbg.h>
#include <stdarg.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/interrupt.h>
#include <asm/current.h>

#include <asm/setup.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#include <asm/MC68328.h>

/***************************************************************************/

void m68328_timer_init(irq_handler_t timer_routine);
void m68328_timer_tick(void);
unsigned long m68328_timer_gettimeoffset(void);
void m68328_timer_gettod(int *year, int *mon, int *day, int *hour, int *min, int *sec);

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

void config_BSP(char *command, int len)
{
  printk(KERN_INFO "\n68328 support D. Jeff Dionne <jeff@uclinux.org>\n");
  printk(KERN_INFO "68328 support Kenneth Albanowski <kjahds@kjshds.com>\n");
  printk(KERN_INFO "68328/Pilot support Bernhard Kuhn <kuhn@lpr.e-technik.tu-muenchen.de>\n");

  mach_sched_init      = m68328_timer_init;
  mach_tick            = m68328_timer_tick;
  mach_gettimeoffset   = m68328_timer_gettimeoffset;
  mach_gettod          = m68328_timer_gettod;
  mach_hwclk           = NULL;
  mach_set_clock_mmss  = NULL;
  mach_reset           = m68328_reset;
  *command = '\0';
}

/***************************************************************************/
