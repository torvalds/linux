/* linux/arch/arm/plat-s3c24xx/time.c
 *
 * Copyright (C) 2003-2005 Simtec Electronics
 *	Ben Dooks, <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/err.h>
#include <linux/clk.h>

#include <asm/system.h>
#include <asm/leds.h>
#include <asm/mach-types.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <mach/map.h>
#include <asm/plat-s3c/regs-timer.h>
#include <mach/regs-irq.h>
#include <asm/mach/time.h>

#include <asm/plat-s3c24xx/clock.h>
#include <asm/plat-s3c24xx/cpu.h>

static unsigned long timer_startval;
static unsigned long timer_usec_ticks;

#define TIMER_USEC_SHIFT 16

/* we use the shifted arithmetic to work out the ratio of timer ticks
 * to usecs, as often the peripheral clock is not a nice even multiple
 * of 1MHz.
 *
 * shift of 14 and 15 are too low for the 12MHz, 16 seems to be ok
 * for the current HZ value of 200 without producing overflows.
 *
 * Original patch by Dimitry Andric, updated by Ben Dooks
*/


/* timer_mask_usec_ticks
 *
 * given a clock and divisor, make the value to pass into timer_ticks_to_usec
 * to scale the ticks into usecs
*/

static inline unsigned long
timer_mask_usec_ticks(unsigned long scaler, unsigned long pclk)
{
	unsigned long den = pclk / 1000;

	return ((1000 << TIMER_USEC_SHIFT) * scaler + (den >> 1)) / den;
}

/* timer_ticks_to_usec
 *
 * convert timer ticks to usec.
*/

static inline unsigned long timer_ticks_to_usec(unsigned long ticks)
{
	unsigned long res;

	res = ticks * timer_usec_ticks;
	res += 1 << (TIMER_USEC_SHIFT - 4);	/* round up slightly */

	return res >> TIMER_USEC_SHIFT;
}

/***
 * Returns microsecond  since last clock interrupt.  Note that interrupts
 * will have been disabled by do_gettimeoffset()
 * IRQs are disabled before entering here from do_gettimeofday()
 */

#define SRCPND_TIMER4 (1<<(IRQ_TIMER4 - IRQ_EINT0))

static unsigned long s3c2410_gettimeoffset (void)
{
	unsigned long tdone;
	unsigned long irqpend;
	unsigned long tval;

	/* work out how many ticks have gone since last timer interrupt */

        tval =  __raw_readl(S3C2410_TCNTO(4));
	tdone = timer_startval - tval;

	/* check to see if there is an interrupt pending */

	irqpend = __raw_readl(S3C2410_SRCPND);
	if (irqpend & SRCPND_TIMER4) {
		/* re-read the timer, and try and fix up for the missed
		 * interrupt. Note, the interrupt may go off before the
		 * timer has re-loaded from wrapping.
		 */

		tval =  __raw_readl(S3C2410_TCNTO(4));
		tdone = timer_startval - tval;

		if (tval != 0)
			tdone += timer_startval;
	}

	return timer_ticks_to_usec(tdone);
}


/*
 * IRQ handler for the timer
 */
static irqreturn_t
s3c2410_timer_interrupt(int irq, void *dev_id)
{
	timer_tick();
	return IRQ_HANDLED;
}

static struct irqaction s3c2410_timer_irq = {
	.name		= "S3C2410 Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= s3c2410_timer_interrupt,
};

#define use_tclk1_12() ( \
	machine_is_bast()	|| \
	machine_is_vr1000()	|| \
	machine_is_anubis()	|| \
	machine_is_osiris() )

/*
 * Set up timer interrupt, and return the current time in seconds.
 *
 * Currently we only use timer4, as it is the only timer which has no
 * other function that can be exploited externally
 */
static void s3c2410_timer_setup (void)
{
	unsigned long tcon;
	unsigned long tcnt;
	unsigned long tcfg1;
	unsigned long tcfg0;

	tcnt = 0xffff;  /* default value for tcnt */

	/* read the current timer configuration bits */

	tcon = __raw_readl(S3C2410_TCON);
	tcfg1 = __raw_readl(S3C2410_TCFG1);
	tcfg0 = __raw_readl(S3C2410_TCFG0);

	/* configure the system for whichever machine is in use */

	if (use_tclk1_12()) {
		/* timer is at 12MHz, scaler is 1 */
		timer_usec_ticks = timer_mask_usec_ticks(1, 12000000);
		tcnt = 12000000 / HZ;

		tcfg1 &= ~S3C2410_TCFG1_MUX4_MASK;
		tcfg1 |= S3C2410_TCFG1_MUX4_TCLK1;
	} else {
		unsigned long pclk;
		struct clk *clk;

		/* for the h1940 (and others), we use the pclk from the core
		 * to generate the timer values. since values around 50 to
		 * 70MHz are not values we can directly generate the timer
		 * value from, we need to pre-scale and divide before using it.
		 *
		 * for instance, using 50.7MHz and dividing by 6 gives 8.45MHz
		 * (8.45 ticks per usec)
		 */

		/* this is used as default if no other timer can be found */

		clk = clk_get(NULL, "timers");
		if (IS_ERR(clk))
			panic("failed to get clock for system timer");

		clk_enable(clk);

		pclk = clk_get_rate(clk);

		/* configure clock tick */

		timer_usec_ticks = timer_mask_usec_ticks(6, pclk);

		tcfg1 &= ~S3C2410_TCFG1_MUX4_MASK;
		tcfg1 |= S3C2410_TCFG1_MUX4_DIV2;

		tcfg0 &= ~S3C2410_TCFG_PRESCALER1_MASK;
		tcfg0 |= ((6 - 1) / 2) << S3C2410_TCFG_PRESCALER1_SHIFT;

		tcnt = (pclk / 6) / HZ;
	}

	/* timers reload after counting zero, so reduce the count by 1 */

	tcnt--;

	printk("timer tcon=%08lx, tcnt %04lx, tcfg %08lx,%08lx, usec %08lx\n",
	       tcon, tcnt, tcfg0, tcfg1, timer_usec_ticks);

	/* check to see if timer is within 16bit range... */
	if (tcnt > 0xffff) {
		panic("setup_timer: HZ is too small, cannot configure timer!");
		return;
	}

	__raw_writel(tcfg1, S3C2410_TCFG1);
	__raw_writel(tcfg0, S3C2410_TCFG0);

	timer_startval = tcnt;
	__raw_writel(tcnt, S3C2410_TCNTB(4));

	/* ensure timer is stopped... */

	tcon &= ~(7<<20);
	tcon |= S3C2410_TCON_T4RELOAD;
	tcon |= S3C2410_TCON_T4MANUALUPD;

	__raw_writel(tcon, S3C2410_TCON);
	__raw_writel(tcnt, S3C2410_TCNTB(4));
	__raw_writel(tcnt, S3C2410_TCMPB(4));

	/* start the timer running */
	tcon |= S3C2410_TCON_T4START;
	tcon &= ~S3C2410_TCON_T4MANUALUPD;
	__raw_writel(tcon, S3C2410_TCON);
}

static void __init s3c2410_timer_init (void)
{
	s3c2410_timer_setup();
	setup_irq(IRQ_TIMER4, &s3c2410_timer_irq);
}

struct sys_timer s3c24xx_timer = {
	.init		= s3c2410_timer_init,
	.offset		= s3c2410_gettimeoffset,
	.resume		= s3c2410_timer_setup
};
