/*
 * arch/sh/kernel/timers/timer-mtu2.c - MTU2 Timer Support
 *
 *  Copyright (C) 2005  Paul Mundt
 *
 * Based off of arch/sh/kernel/timers/timer-tmu.c
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
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/clock.h>

/*
 * We use channel 1 for our lowly system timer. Channel 2 would be the other
 * likely candidate, but we leave it alone as it has higher divisors that
 * would be of more use to other more interesting applications.
 *
 * TODO: Presently we only implement a 16-bit single-channel system timer.
 * However, we can implement channel cascade if we go the overflow route and
 * get away with using 2 MTU2 channels as a 32-bit timer.
 */
#define MTU2_TSTR	0xfffe4280
#define MTU2_TCR_1	0xfffe4380
#define MTU2_TMDR_1	0xfffe4381
#define MTU2_TIOR_1	0xfffe4382
#define MTU2_TIER_1	0xfffe4384
#define MTU2_TSR_1	0xfffe4385
#define MTU2_TCNT_1	0xfffe4386	/* 16-bit counter */
#define MTU2_TGRA_1	0xfffe438a

#define STBCR3		0xfffe0408

#define MTU2_TSTR_CST1	(1 << 1)	/* Counter Start 1 */

#define MTU2_TSR_TGFA	(1 << 0)	/* GRA compare match */

#define MTU2_TIER_TGIEA	(1 << 0)	/* GRA compare match  interrupt enable */

#define MTU2_TCR_INIT	0x22

#define MTU2_TCR_CALIB  0x00

static unsigned long mtu2_timer_get_offset(void)
{
	int count;
	static int count_p = 0x7fff;	/* for the first call after boot */
	static unsigned long jiffies_p = 0;

	/*
	 * cache volatile jiffies temporarily; we have IRQs turned off.
	 */
	unsigned long jiffies_t;

	/* timer count may underflow right here */
	count = ctrl_inw(MTU2_TCNT_1);	/* read the latched count */

	jiffies_t = jiffies;

	/*
	 * avoiding timer inconsistencies (they are rare, but they happen)...
	 * there is one kind of problem that must be avoided here:
	 *  1. the timer counter underflows
	 */

	if (jiffies_t == jiffies_p) {
		if (count > count_p) {
			if (ctrl_inb(MTU2_TSR_1) & MTU2_TSR_TGFA) {
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

static irqreturn_t mtu2_timer_interrupt(int irq, void *dev_id)
{
	unsigned long timer_status;

	/* Clear TGFA bit */
	timer_status = ctrl_inb(MTU2_TSR_1);
	timer_status &= ~MTU2_TSR_TGFA;
	ctrl_outb(timer_status, MTU2_TSR_1);

	/* Do timer tick */
	write_seqlock(&xtime_lock);
	handle_timer_tick();
	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction mtu2_irq = {
	.name		= "timer",
	.handler	= mtu2_timer_interrupt,
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.mask		= CPU_MASK_NONE,
};

static unsigned int divisors[] = { 1, 4, 16, 64, 1, 1, 256 };

static void mtu2_clk_init(struct clk *clk)
{
	u8 idx = MTU2_TCR_INIT & 0x7;

	clk->rate = clk->parent->rate / divisors[idx];
	/* Start TCNT counting */
	ctrl_outb(ctrl_inb(MTU2_TSTR) | MTU2_TSTR_CST1, MTU2_TSTR);

}

static void mtu2_clk_recalc(struct clk *clk)
{
	u8 idx = ctrl_inb(MTU2_TCR_1) & 0x7;
	clk->rate = clk->parent->rate / divisors[idx];
}

static struct clk_ops mtu2_clk_ops = {
	.init		= mtu2_clk_init,
	.recalc		= mtu2_clk_recalc,
};

static struct clk mtu2_clk1 = {
	.name		= "mtu2_clk1",
	.ops		= &mtu2_clk_ops,
};

static int mtu2_timer_start(void)
{
	ctrl_outb(ctrl_inb(MTU2_TSTR) | MTU2_TSTR_CST1, MTU2_TSTR);
	return 0;
}

static int mtu2_timer_stop(void)
{
	ctrl_outb(ctrl_inb(MTU2_TSTR) & ~MTU2_TSTR_CST1, MTU2_TSTR);
	return 0;
}

static int mtu2_timer_init(void)
{
	u8 tmp;
	unsigned long interval;

	setup_irq(CONFIG_SH_TIMER_IRQ, &mtu2_irq);

	mtu2_clk1.parent = clk_get("module_clk");

	ctrl_outb(ctrl_inb(STBCR3) & (~0x20), STBCR3);

	/* Normal operation */
	ctrl_outb(0, MTU2_TMDR_1);
	ctrl_outb(MTU2_TCR_INIT, MTU2_TCR_1);
	ctrl_outb(0x01, MTU2_TIOR_1);

	/* Enable underflow interrupt */
	ctrl_outb(ctrl_inb(MTU2_TIER_1) | MTU2_TIER_TGIEA, MTU2_TIER_1);

	interval = CONFIG_SH_PCLK_FREQ / 16 / HZ;
	printk(KERN_INFO "Interval = %ld\n", interval);

	ctrl_outw(interval, MTU2_TGRA_1);
	ctrl_outw(0, MTU2_TCNT_1);

	clk_register(&mtu2_clk1);
	clk_enable(&mtu2_clk1);

	return 0;
}

struct sys_timer_ops mtu2_timer_ops = {
	.init		= mtu2_timer_init,
	.start		= mtu2_timer_start,
	.stop		= mtu2_timer_stop,
#ifndef CONFIG_GENERIC_TIME
	.get_offset	= mtu2_timer_get_offset,
#endif
};

struct sys_timer mtu2_timer = {
	.name	= "mtu2",
	.ops	= &mtu2_timer_ops,
};
