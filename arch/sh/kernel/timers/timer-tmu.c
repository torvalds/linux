/*
 * arch/sh/kernel/timers/timer-tmu.c - TMU Timer Support
 *
 *  Copyright (C) 2005  Paul Mundt
 *
 * TMU handling code hacked out of arch/sh/kernel/time.c
 *
 *  Copyright (C) 1999  Tetsuya Okada & Niibe Yutaka
 *  Copyright (C) 2000  Philipp Rumpf <prumpf@tux.org>
 *  Copyright (C) 2002, 2003, 2004  Paul Mundt
 *  Copyright (C) 2002  M. R. Brown  <mrbrown@linux-sh.org>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/seqlock.h>
#include <asm/timer.h>
#include <asm/rtc.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/clock.h>

#define TMU_TOCR_INIT	0x00
#define TMU0_TCR_INIT	0x0020
#define TMU_TSTR_INIT	1

#define TMU0_TCR_CALIB	0x0000

static unsigned long tmu_timer_get_offset(void)
{
	int count;
	static int count_p = 0x7fffffff;    /* for the first call after boot */
	static unsigned long jiffies_p = 0;

	/*
	 * cache volatile jiffies temporarily; we have IRQs turned off.
	 */
	unsigned long jiffies_t;

	/* timer count may underflow right here */
	count = ctrl_inl(TMU0_TCNT);	/* read the latched count */

	jiffies_t = jiffies;

	/*
	 * avoiding timer inconsistencies (they are rare, but they happen)...
	 * there is one kind of problem that must be avoided here:
	 *  1. the timer counter underflows
	 */

	if (jiffies_t == jiffies_p) {
		if (count > count_p) {
			/* the nutcase */
			if (ctrl_inw(TMU0_TCR) & 0x100) { /* Check UNF bit */
				count -= LATCH;
			} else {
				printk("%s (): hardware timer problem?\n",
				       __FUNCTION__);
			}
		}
	} else
		jiffies_p = jiffies_t;

	count_p = count;

	count = ((LATCH-1) - count) * TICK_SIZE;
	count = (count + LATCH/2) / LATCH;

	return count;
}

static irqreturn_t tmu_timer_interrupt(int irq, void *dummy)
{
	unsigned long timer_status;

	/* Clear UNF bit */
	timer_status = ctrl_inw(TMU0_TCR);
	timer_status &= ~0x100;
	ctrl_outw(timer_status, TMU0_TCR);

	/*
	 * Here we are in the timer irq handler. We just have irqs locally
	 * disabled but we don't know if the timer_bh is running on the other
	 * CPU. We need to avoid to SMP race with it. NOTE: we don' t need
	 * the irq version of write_lock because as just said we have irq
	 * locally disabled. -arca
	 */
	write_seqlock(&xtime_lock);
	handle_timer_tick();
	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction tmu_irq = {
	.name		= "timer",
	.handler	= tmu_timer_interrupt,
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.mask		= CPU_MASK_NONE,
};

static void tmu_clk_init(struct clk *clk)
{
	u8 divisor = TMU0_TCR_INIT & 0x7;
	ctrl_outw(TMU0_TCR_INIT, TMU0_TCR);
	clk->rate = clk->parent->rate / (4 << (divisor << 1));
}

static void tmu_clk_recalc(struct clk *clk)
{
	u8 divisor = ctrl_inw(TMU0_TCR) & 0x7;
	clk->rate = clk->parent->rate / (4 << (divisor << 1));
}

static struct clk_ops tmu_clk_ops = {
	.init		= tmu_clk_init,
	.recalc		= tmu_clk_recalc,
};

static struct clk tmu0_clk = {
	.name		= "tmu0_clk",
	.ops		= &tmu_clk_ops,
};

static int tmu_timer_start(void)
{
	ctrl_outb(TMU_TSTR_INIT, TMU_TSTR);
	return 0;
}

static int tmu_timer_stop(void)
{
	ctrl_outb(0, TMU_TSTR);
	return 0;
}

static int tmu_timer_init(void)
{
	unsigned long interval;

	setup_irq(CONFIG_SH_TIMER_IRQ, &tmu_irq);

	tmu0_clk.parent = clk_get(NULL, "module_clk");

	/* Start TMU0 */
	tmu_timer_stop();
#if !defined(CONFIG_CPU_SUBTYPE_SH7300) && \
    !defined(CONFIG_CPU_SUBTYPE_SH7760) && \
    !defined(CONFIG_CPU_SUBTYPE_SH7785)
	ctrl_outb(TMU_TOCR_INIT, TMU_TOCR);
#endif

	clk_register(&tmu0_clk);
	clk_enable(&tmu0_clk);

	interval = (clk_get_rate(&tmu0_clk) + HZ / 2) / HZ;
	printk(KERN_INFO "Interval = %ld\n", interval);

	ctrl_outl(interval, TMU0_TCOR);
	ctrl_outl(interval, TMU0_TCNT);

	tmu_timer_start();

	return 0;
}

struct sys_timer_ops tmu_timer_ops = {
	.init		= tmu_timer_init,
	.start		= tmu_timer_start,
	.stop		= tmu_timer_stop,
#ifndef CONFIG_GENERIC_TIME
	.get_offset	= tmu_timer_get_offset,
#endif
};

struct sys_timer tmu_timer = {
	.name	= "tmu",
	.ops	= &tmu_timer_ops,
};
