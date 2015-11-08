/*
 *  config.c - non-mmu 68360 platform initialization code
 *
 *  Copyright (c) 2000 Michael Leslie <mleslie@lineo.com>
 *  Copyright (C) 1993 Hamish Macdonald
 *  Copyright (C) 1999 D. Jeff Dionne <jeff@uclinux.org>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <stdarg.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/setup.h>
#include <asm/pgtable.h>
#include <asm/machdep.h>
#include <asm/m68360.h>

#ifdef CONFIG_UCQUICC
#include <asm/bootstd.h>
#endif

extern void m360_cpm_reset(void);

// Mask to select if the PLL prescaler is enabled.
#define MCU_PREEN   ((unsigned short)(0x0001 << 13))

#if defined(CONFIG_UCQUICC)
#define OSCILLATOR  (unsigned long int)33000000
#endif

static irq_handler_t timer_interrupt;
unsigned long int system_clock;

extern QUICC *pquicc;

/* TODO  DON"T Hard Code this */
/* calculate properly using the right PLL and prescaller */
// unsigned int system_clock = 33000000l;
extern unsigned long int system_clock; //In kernel setup.c


static irqreturn_t hw_tick(int irq, void *dummy)
{
  /* Reset Timer1 */
  /* TSTAT &= 0; */

  pquicc->timer_ter1 = 0x0002; /* clear timer event */

  return timer_interrupt(irq, dummy);
}

static struct irqaction m68360_timer_irq = {
	.name	 = "timer",
	.flags	 = IRQF_TIMER,
	.handler = hw_tick,
};

void hw_timer_init(irq_handler_t handler)
{
  unsigned char prescaler;
  unsigned short tgcr_save;

#if 0
  /* Restart mode, Enable int, 32KHz, Enable timer */
  TCTL = TCTL_OM | TCTL_IRQEN | TCTL_CLKSOURCE_32KHZ | TCTL_TEN;
  /* Set prescaler (Divide 32KHz by 32)*/
  TPRER = 31;
  /* Set compare register  32Khz / 32 / 10 = 100 */
  TCMP = 10;                                                              

  request_irq(IRQ_MACHSPEC | 1, timer_routine, 0, "timer", NULL);
#endif

  /* General purpose quicc timers: MC68360UM p7-20 */

  /* Set up timer 1 (in [1..4]) to do 100Hz */
  tgcr_save = pquicc->timer_tgcr & 0xfff0;
  pquicc->timer_tgcr  = tgcr_save; /* stop and reset timer 1 */
  /* pquicc->timer_tgcr |= 0x4444; */ /* halt timers when FREEZE (ie bdm freeze) */

  prescaler = 8;
  pquicc->timer_tmr1 = 0x001a | /* or=1, frr=1, iclk=01b */
                           (unsigned short)((prescaler - 1) << 8);
    
  pquicc->timer_tcn1 = 0x0000; /* initial count */
  /* calculate interval for 100Hz based on the _system_clock: */
  pquicc->timer_trr1 = (system_clock/ prescaler) / HZ; /* reference count */

  pquicc->timer_ter1 = 0x0003; /* clear timer events */

  timer_interrupt = handler;

  /* enable timer 1 interrupt in CIMR */
  setup_irq(CPMVEC_TIMER1, &m68360_timer_irq);

  /* Start timer 1: */
  tgcr_save = (pquicc->timer_tgcr & 0xfff0) | 0x0001;
  pquicc->timer_tgcr  = tgcr_save;
}

void BSP_reset (void)
{
  local_irq_disable();
  asm volatile (
    "moveal #_start, %a0;\n"
    "moveb #0, 0xFFFFF300;\n"
    "moveal 0(%a0), %sp;\n"
    "moveal 4(%a0), %a0;\n"
    "jmp (%a0);\n"
    );
}

unsigned char *scc1_hwaddr;
static int errno;

#if defined (CONFIG_UCQUICC)
_bsc0(char *, getserialnum)
_bsc1(unsigned char *, gethwaddr, int, a)
_bsc1(char *, getbenv, char *, a)
#endif


void __init config_BSP(char *command, int len)
{
  unsigned char *p;

  m360_cpm_reset();

  /* Calculate the real system clock value. */
  {
     unsigned int local_pllcr = (unsigned int)(pquicc->sim_pllcr);
     if( local_pllcr & MCU_PREEN ) // If the prescaler is dividing by 128
     {
         int mf = (int)(pquicc->sim_pllcr & 0x0fff);
         system_clock = (OSCILLATOR / 128) * (mf + 1);
     }
     else
     {
         int mf = (int)(pquicc->sim_pllcr & 0x0fff);
         system_clock = (OSCILLATOR) * (mf + 1);
     }
  }

  printk(KERN_INFO "\n68360 QUICC support (C) 2000 Lineo Inc.\n");

#if defined(CONFIG_UCQUICC) && 0
  printk(KERN_INFO "uCquicc serial string [%s]\n",getserialnum());
  p = scc1_hwaddr = gethwaddr(0);
  printk(KERN_INFO "uCquicc hwaddr %pM\n", p);

  p = getbenv("APPEND");
  if (p)
    strcpy(p,command);
  else
    command[0] = 0;
#else
  scc1_hwaddr = "\00\01\02\03\04\05";
#endif
 
  mach_reset = BSP_reset;
}
