/***************************************************************************/

/*
 *  linux/arch/m68knommu/platform/68328/timers.c
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

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/setup.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#include <asm/MC68VZ328.h>

/***************************************************************************/

#if defined(CONFIG_DRAGEN2)
/* with a 33.16 MHz clock, this will give usec resolution to the time functions */
#define CLOCK_SOURCE	TCTL_CLKSOURCE_SYSCLK
#define CLOCK_PRE	7
#define TICKS_PER_JIFFY	41450

#elif defined(CONFIG_XCOPILOT_BUGS)
/*
 * The only thing I know is that CLK32 is not available on Xcopilot
 * I have little idea about what frequency SYSCLK has on Xcopilot.
 * The values for prescaler and compare registers were simply
 * taken from the original source
 */
#define CLOCK_SOURCE	TCTL_CLKSOURCE_SYSCLK
#define CLOCK_PRE	2
#define TICKS_PER_JIFFY	0xd7e4

#else
/* default to using the 32Khz clock */
#define CLOCK_SOURCE	TCTL_CLKSOURCE_32KHZ
#define CLOCK_PRE	31
#define TICKS_PER_JIFFY	10
#endif

/***************************************************************************/

void m68328_timer_init(irqreturn_t (*timer_routine) (int, void *, struct pt_regs *))
{
	/* disable timer 1 */
	TCTL = 0;

	/* set ISR */
	if (request_irq(TMR_IRQ_NUM, timer_routine, IRQ_FLG_LOCK, "timer", NULL)) 
		panic("Unable to attach timer interrupt\n");

	/* Restart mode, Enable int, Set clock source */
	TCTL = TCTL_OM | TCTL_IRQEN | CLOCK_SOURCE;
	TPRER = CLOCK_PRE;
	TCMP = TICKS_PER_JIFFY;

	/* Enable timer 1 */
	TCTL |= TCTL_TEN;
}

/***************************************************************************/

void m68328_timer_tick(void)
{
	/* Reset Timer1 */
	TSTAT &= 0;
}
/***************************************************************************/

unsigned long m68328_timer_gettimeoffset(void)
{
	unsigned long ticks = TCN, offset = 0;

	/* check for pending interrupt */
	if (ticks < (TICKS_PER_JIFFY >> 1) && (ISR & (1 << TMR_IRQ_NUM)))
		offset = 1000000 / HZ;
	ticks = (ticks * 1000000 / HZ) / TICKS_PER_JIFFY;
	return ticks + offset;
}

/***************************************************************************/

void m68328_timer_gettod(int *year, int *mon, int *day, int *hour, int *min, int *sec)
{
	long now = RTCTIME;

	*year = *mon = *day = 1;
	*hour = (now >> 24) % 24;
	*min = (now >> 16) % 60;
	*sec = now % 60;
}

/***************************************************************************/
