/*
 *  linux/arch/$(ARCH)/platform/$(PLATFORM)/config.c
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

#include <asm/dbg.h>
#include <stdarg.h>
#include <linux/config.h>
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


void BSP_sched_init(irqreturn_t (*timer_routine)(int, void *, struct pt_regs *))
{

#ifdef CONFIG_XCOPILOT_BUGS
  /*
   * The only thing I know is that CLK32 is not available on Xcopilot
   * I have little idea about what frequency SYSCLK has on Xcopilot. 
   * The values for prescaler and compare registers were simply 
   * taken from the original source
   */

  /* Restart mode, Enable int, SYSCLK, Enable timer */
  TCTL2 = TCTL_OM | TCTL_IRQEN | TCTL_CLKSOURCE_SYSCLK | TCTL_TEN;
  /* Set prescaler */
  TPRER2 = 2;
  /* Set compare register */
  TCMP2 = 0xd7e4;
#else
  /* Restart mode, Enable int, 32KHz, Enable timer */
  TCTL2 = TCTL_OM | TCTL_IRQEN | TCTL_CLKSOURCE_32KHZ | TCTL_TEN;
  /* Set prescaler (Divide 32KHz by 32)*/
  TPRER2 = 31;
  /* Set compare register  32Khz / 32 / 10 = 100 */
  TCMP2 = 10;
#endif
                                                                    
  request_irq(TMR2_IRQ_NUM, timer_routine, IRQ_FLG_LOCK, "timer", NULL);
}

void BSP_tick(void)
{
  /* Reset Timer2 */
  TSTAT2 &= 0;
}

unsigned long BSP_gettimeoffset (void)
{
  return 0;
}

void BSP_gettod (int *yearp, int *monp, int *dayp,
		   int *hourp, int *minp, int *secp)
{
}

int BSP_hwclk(int op, struct hwclk_time *t)
{
  if (!op) {
    /* read */
  } else {
    /* write */
  }
  return 0;
}

int BSP_set_clock_mmss (unsigned long nowtime)
{
#if 0
  short real_seconds = nowtime % 60, real_minutes = (nowtime / 60) % 60;

  tod->second1 = real_seconds / 10;
  tod->second2 = real_seconds % 10;
  tod->minute1 = real_minutes / 10;
  tod->minute2 = real_minutes % 10;
#endif
  return 0;
}

void BSP_reset (void)
{
  local_irq_disable();
  asm volatile ("moveal #0x10c00000, %a0;\n\t"
		"moveb #0, 0xFFFFF300;\n\t"
		"moveal 0(%a0), %sp;\n\t"
		"moveal 4(%a0), %a0;\n\t"
		"jmp (%a0);");
}

void config_BSP(char *command, int len)
{
  printk(KERN_INFO "\n68328 support D. Jeff Dionne <jeff@uclinux.org>\n");
  printk(KERN_INFO "68328 support Kenneth Albanowski <kjahds@kjshds.com>\n");
  printk(KERN_INFO "68328/Pilot support Bernhard Kuhn <kuhn@lpr.e-technik.tu-muenchen.de>\n");

  mach_sched_init      = BSP_sched_init;
  mach_tick            = BSP_tick;
  mach_gettimeoffset   = BSP_gettimeoffset;
  mach_gettod          = BSP_gettod;
  mach_hwclk           = NULL;
  mach_set_clock_mmss  = NULL;
  mach_reset           = BSP_reset;
  *command = '\0';
}
