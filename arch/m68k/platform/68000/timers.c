/***************************************************************************/

/*
 *  timers.c - Generic hardware timer support.
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

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clocksource.h>
#include <linux/rtc.h>
#include <asm/setup.h>
#include <asm/pgtable.h>
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

static u32 m68328_tick_cnt;
static irq_handler_t timer_interrupt;

/***************************************************************************/

static irqreturn_t hw_tick(int irq, void *dummy)
{
	/* Reset Timer1 */
	TSTAT &= 0;

	m68328_tick_cnt += TICKS_PER_JIFFY;
	return timer_interrupt(irq, dummy);
}

/***************************************************************************/

static struct irqaction m68328_timer_irq = {
	.name	 = "timer",
	.flags	 = IRQF_DISABLED | IRQF_TIMER,
	.handler = hw_tick,
};

/***************************************************************************/

static cycle_t m68328_read_clk(struct clocksource *cs)
{
	unsigned long flags;
	u32 cycles;

	local_irq_save(flags);
	cycles = m68328_tick_cnt + TCN;
	local_irq_restore(flags);

	return cycles;
}

/***************************************************************************/

static struct clocksource m68328_clk = {
	.name	= "timer",
	.rating	= 250,
	.read	= m68328_read_clk,
	.mask	= CLOCKSOURCE_MASK(32),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

/***************************************************************************/

void hw_timer_init(irq_handler_t handler)
{
	/* disable timer 1 */
	TCTL = 0;

	/* set ISR */
	setup_irq(TMR_IRQ_NUM, &m68328_timer_irq);

	/* Restart mode, Enable int, Set clock source */
	TCTL = TCTL_OM | TCTL_IRQEN | CLOCK_SOURCE;
	TPRER = CLOCK_PRE;
	TCMP = TICKS_PER_JIFFY;

	/* Enable timer 1 */
	TCTL |= TCTL_TEN;
	clocksource_register_hz(&m68328_clk, TICKS_PER_JIFFY*HZ);
	timer_interrupt = handler;
}

/***************************************************************************/

int m68328_hwclk(int set, struct rtc_time *t)
{
	if (!set) {
		long now = RTCTIME;
		t->tm_year = t->tm_mon = t->tm_mday = 1;
		t->tm_hour = (now >> 24) % 24;
		t->tm_min = (now >> 16) % 60;
		t->tm_sec = now % 60;
	}

	return 0;
}

/***************************************************************************/
