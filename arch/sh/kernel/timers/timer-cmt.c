/*
 * arch/sh/kernel/timers/timer-cmt.c - CMT Timer Support
 *
 *  Copyright (C) 2005  Yoshinori Sato
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/seqlock.h>
#include <asm/timer.h>
#include <asm/rtc.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/clock.h>

#if defined(CONFIG_CPU_SUBTYPE_SH7619)
#define CMT_CMSTR	0xf84a0070
#define CMT_CMCSR_0	0xf84a0072
#define CMT_CMCNT_0	0xf84a0074
#define CMT_CMCOR_0	0xf84a0076
#define CMT_CMCSR_1	0xf84a0078
#define CMT_CMCNT_1	0xf84a007a
#define CMT_CMCOR_1	0xf84a007c

#define STBCR3		0xf80a0000
#define cmt_clock_enable() do {	ctrl_outb(ctrl_inb(STBCR3) & ~0x10, STBCR3); } while(0)
#define CMT_CMCSR_INIT	0x0040
#define CMT_CMCSR_CALIB	0x0000
#elif defined(CONFIG_CPU_SUBTYPE_SH7206)
#define CMT_CMSTR	0xfffec000
#define CMT_CMCSR_0	0xfffec002
#define CMT_CMCNT_0	0xfffec004
#define CMT_CMCOR_0	0xfffec006

#define STBCR4		0xfffe040c
#define cmt_clock_enable() do {	ctrl_outb(ctrl_inb(STBCR4) & ~0x04, STBCR4); } while(0)
#define CMT_CMCSR_INIT	0x0040
#define CMT_CMCSR_CALIB	0x0000
#else
#error "Unknown CPU SUBTYPE"
#endif

static DEFINE_SPINLOCK(cmt0_lock);

static unsigned long cmt_timer_get_offset(void)
{
	int count;
	unsigned long flags;

	static unsigned short count_p = 0xffff;    /* for the first call after boot */
	static unsigned long jiffies_p = 0;

	/*
	 * cache volatile jiffies temporarily; we have IRQs turned off.
	 */
	unsigned long jiffies_t;

	spin_lock_irqsave(&cmt0_lock, flags);
	/* timer count may underflow right here */
	count =  ctrl_inw(CMT_CMCOR_0);
	count -= ctrl_inw(CMT_CMCNT_0);

	jiffies_t = jiffies;

	/*
	 * avoiding timer inconsistencies (they are rare, but they happen)...
	 * there is one kind of problem that must be avoided here:
	 *  1. the timer counter underflows
	 */

	if (jiffies_t == jiffies_p) {
		if (count > count_p) {
			/* the nutcase */
			if (ctrl_inw(CMT_CMCSR_0) & 0x80) { /* Check CMF bit */
				count -= LATCH;
			} else {
				printk("%s (): hardware timer problem?\n",
				       __FUNCTION__);
			}
		}
	} else
		jiffies_p = jiffies_t;

	count_p = count;
	spin_unlock_irqrestore(&cmt0_lock, flags);

	count = ((LATCH-1) - count) * TICK_SIZE;
	count = (count + LATCH/2) / LATCH;

	return count;
}

static irqreturn_t cmt_timer_interrupt(int irq, void *dev_id,
				       struct pt_regs *regs)
{
	unsigned long timer_status;

	/* Clear CMF bit */
	timer_status = ctrl_inw(CMT_CMCSR_0);
	timer_status &= ~0x80;
	ctrl_outw(timer_status, CMT_CMCSR_0);

	/*
	 * Here we are in the timer irq handler. We just have irqs locally
	 * disabled but we don't know if the timer_bh is running on the other
	 * CPU. We need to avoid to SMP race with it. NOTE: we don' t need
	 * the irq version of write_lock because as just said we have irq
	 * locally disabled. -arca
	 */
	write_seqlock(&xtime_lock);
	handle_timer_tick(regs);
	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction cmt_irq = {
	.name		= "timer",
	.handler	= cmt_timer_interrupt,
	.flags		= SA_INTERRUPT,
	.mask		= CPU_MASK_NONE,
};

/*
 * Hah!  We'll see if this works (switching from usecs to nsecs).
 */
static unsigned long cmt_timer_get_frequency(void)
{
	u32 freq;
	struct timespec ts1, ts2;
	unsigned long diff_nsec;
	unsigned long factor;

	/* Setup the timer:  We don't want to generate interrupts, just
	 * have it count down at its natural rate.
	 */
	
	ctrl_outw(ctrl_inw(CMT_CMSTR) & ~0x01, CMT_CMSTR);
	ctrl_outw(CMT_CMCSR_CALIB, CMT_CMCSR_0);
	ctrl_outw(0xffff, CMT_CMCOR_0);
	ctrl_outw(0xffff, CMT_CMCNT_0);

	rtc_sh_get_time(&ts2);

	do {
		rtc_sh_get_time(&ts1);
	} while (ts1.tv_nsec == ts2.tv_nsec && ts1.tv_sec == ts2.tv_sec);

	/* actually start the timer */
	ctrl_outw(ctrl_inw(CMT_CMSTR) | 0x01, CMT_CMSTR);

	do {
		rtc_sh_get_time(&ts2);
	} while (ts1.tv_nsec == ts2.tv_nsec && ts1.tv_sec == ts2.tv_sec);

	freq = 0xffff - ctrl_inw(CMT_CMCNT_0);
	if (ts2.tv_nsec < ts1.tv_nsec) {
		ts2.tv_nsec += 1000000000;
		ts2.tv_sec--;
	}

	diff_nsec = (ts2.tv_sec - ts1.tv_sec) * 1000000000 + (ts2.tv_nsec - ts1.tv_nsec);

	/* this should work well if the RTC has a precision of n Hz, where
	 * n is an integer.  I don't think we have to worry about the other
	 * cases. */
	factor = (1000000000 + diff_nsec/2) / diff_nsec;

	if (factor * diff_nsec > 1100000000 ||
	    factor * diff_nsec <  900000000)
		panic("weird RTC (diff_nsec %ld)", diff_nsec);

	return freq * factor;
}

static void cmt_clk_init(struct clk *clk)
{
	u8 divisor = CMT_CMCSR_INIT & 0x3;
	ctrl_inw(CMT_CMCSR_0);
	ctrl_outw(CMT_CMCSR_INIT, CMT_CMCSR_0);
	clk->parent = clk_get("module_clk");
	clk->rate = clk->parent->rate / (8 << (divisor << 1));
}

static void cmt_clk_recalc(struct clk *clk)
{
	u8 divisor = ctrl_inw(CMT_CMCSR_0) & 0x3;
	clk->rate = clk->parent->rate / (8 << (divisor << 1));
}

static struct clk_ops cmt_clk_ops = {
	.init		= cmt_clk_init,
	.recalc		= cmt_clk_recalc,
};

static struct clk cmt0_clk = {
	.name		= "cmt0_clk",
	.ops		= &cmt_clk_ops,
};

static int cmt_timer_start(void)
{
	ctrl_outw(ctrl_inw(CMT_CMSTR) | 0x01, CMT_CMSTR);
	return 0;
}

static int cmt_timer_stop(void)
{
	ctrl_outw(ctrl_inw(CMT_CMSTR) & ~0x01, CMT_CMSTR);
	return 0;
}

static int cmt_timer_init(void)
{
	unsigned long interval;

	cmt_clock_enable();

	setup_irq(TIMER_IRQ, &cmt_irq);

	cmt0_clk.parent = clk_get("module_clk");

	cmt_timer_stop();

	interval = cmt0_clk.parent->rate / 8 / HZ;
	printk(KERN_INFO "Interval = %ld\n", interval);

	ctrl_outw(interval, CMT_CMCOR_0);

	clk_register(&cmt0_clk);
	clk_enable(&cmt0_clk);

	cmt_timer_start();

	return 0;
}

struct sys_timer_ops cmt_timer_ops = {
	.init		= cmt_timer_init,
	.start		= cmt_timer_start,
	.stop		= cmt_timer_stop,
	.get_frequency	= cmt_timer_get_frequency,
	.get_offset	= cmt_timer_get_offset,
};

struct sys_timer cmt_timer = {
	.name	= "cmt",
	.ops	= &cmt_timer_ops,
};

