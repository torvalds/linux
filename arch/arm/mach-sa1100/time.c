/*
 * linux/arch/arm/mach-sa1100/time.c
 *
 * Copyright (C) 1998 Deborah Wallach.
 * Twiddles  (C) 1999 	Hugo Fiennes <hugo@empeg.com>
 * 
 * 2000/03/29 (C) Nicolas Pitre <nico@cam.org>
 *	Rewritten: big cleanup, much simpler, better HZ accuracy.
 *
 */
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/signal.h>

#include <asm/mach/time.h>
#include <asm/hardware.h>

#define RTC_DEF_DIVIDER		(32768 - 1)
#define RTC_DEF_TRIM            0

static unsigned long __init sa1100_get_rtc_time(void)
{
	/*
	 * According to the manual we should be able to let RTTR be zero
	 * and then a default diviser for a 32.768KHz clock is used.
	 * Apparently this doesn't work, at least for my SA1110 rev 5.
	 * If the clock divider is uninitialized then reset it to the
	 * default value to get the 1Hz clock.
	 */
	if (RTTR == 0) {
		RTTR = RTC_DEF_DIVIDER + (RTC_DEF_TRIM << 16);
		printk(KERN_WARNING "Warning: uninitialized Real Time Clock\n");
		/* The current RTC value probably doesn't make sense either */
		RCNR = 0;
		return 0;
	}
	return RCNR;
}

static int sa1100_set_rtc(void)
{
	unsigned long current_time = xtime.tv_sec;

	if (RTSR & RTSR_ALE) {
		/* make sure not to forward the clock over an alarm */
		unsigned long alarm = RTAR;
		if (current_time >= alarm && alarm >= RCNR)
			return -ERESTARTSYS;
	}
	RCNR = current_time;
	return 0;
}

/* IRQs are disabled before entering here from do_gettimeofday() */
static unsigned long sa1100_gettimeoffset (void)
{
	unsigned long ticks_to_match, elapsed, usec;

	/* Get ticks before next timer match */
	ticks_to_match = OSMR0 - OSCR;

	/* We need elapsed ticks since last match */
	elapsed = LATCH - ticks_to_match;

	/* Now convert them to usec */
	usec = (unsigned long)(elapsed * (tick_nsec / 1000))/LATCH;

	return usec;
}

#ifdef CONFIG_NO_IDLE_HZ
static unsigned long initial_match;
static int match_posponed;
#endif

static irqreturn_t
sa1100_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned int next_match;

	write_seqlock(&xtime_lock);

#ifdef CONFIG_NO_IDLE_HZ
	if (match_posponed) {
		match_posponed = 0;
		OSMR0 = initial_match;
	}
#endif

	/*
	 * Loop until we get ahead of the free running timer.
	 * This ensures an exact clock tick count and time accuracy.
	 * Since IRQs are disabled at this point, coherence between
	 * lost_ticks(updated in do_timer()) and the match reg value is
	 * ensured, hence we can use do_gettimeofday() from interrupt
	 * handlers.
	 */
	do {
		timer_tick(regs);
		OSSR = OSSR_M0;  /* Clear match on timer 0 */
		next_match = (OSMR0 += LATCH);
	} while ((signed long)(next_match - OSCR) <= 0);

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction sa1100_timer_irq = {
	.name		= "SA11xx Timer Tick",
	.flags		= SA_INTERRUPT | SA_TIMER,
	.handler	= sa1100_timer_interrupt,
};

static void __init sa1100_timer_init(void)
{
	struct timespec tv;

	set_rtc = sa1100_set_rtc;

	tv.tv_nsec = 0;
	tv.tv_sec = sa1100_get_rtc_time();
	do_settimeofday(&tv);

	OIER = 0;		/* disable any timer interrupts */
	OSCR = LATCH*2;		/* push OSCR out of the way */
	OSMR0 = LATCH;		/* set initial match */
	OSSR = 0xf;		/* clear status on all timers */
	setup_irq(IRQ_OST0, &sa1100_timer_irq);
	OIER = OIER_E0;		/* enable match on timer 0 to cause interrupts */
	OSCR = 0;		/* initialize free-running timer */
}

#ifdef CONFIG_NO_IDLE_HZ
static int sa1100_dyn_tick_enable_disable(void)
{
	/* nothing to do */
	return 0;
}

static void sa1100_dyn_tick_reprogram(unsigned long ticks)
{
	if (ticks > 1) {
		initial_match = OSMR0;
		OSMR0 = initial_match + ticks * LATCH;
		match_posponed = 1;
	}
}

static irqreturn_t
sa1100_dyn_tick_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	if (match_posponed) {
		match_posponed = 0;
		OSMR0 = initial_match;
		if ((signed long)(initial_match - OSCR) <= 0)
			return sa1100_timer_interrupt(irq, dev_id, regs);
	}
	return IRQ_NONE;
}

static struct dyn_tick_timer sa1100_dyn_tick = {
	.enable		= sa1100_dyn_tick_enable_disable,
	.disable	= sa1100_dyn_tick_enable_disable,
	.reprogram	= sa1100_dyn_tick_reprogram,
	.handler	= sa1100_dyn_tick_handler,
};
#endif

#ifdef CONFIG_PM
unsigned long osmr[4], oier;

static void sa1100_timer_suspend(void)
{
	osmr[0] = OSMR0;
	osmr[1] = OSMR1;
	osmr[2] = OSMR2;
	osmr[3] = OSMR3;
	oier = OIER;
}

static void sa1100_timer_resume(void)
{
	OSSR = 0x0f;
	OSMR0 = osmr[0];
	OSMR1 = osmr[1];
	OSMR2 = osmr[2];
	OSMR3 = osmr[3];
	OIER = oier;

	/*
	 * OSMR0 is the system timer: make sure OSCR is sufficiently behind
	 */
	OSCR = OSMR0 - LATCH;
}
#else
#define sa1100_timer_suspend NULL
#define sa1100_timer_resume NULL
#endif

struct sys_timer sa1100_timer = {
	.init		= sa1100_timer_init,
	.suspend	= sa1100_timer_suspend,
	.resume		= sa1100_timer_resume,
	.offset		= sa1100_gettimeoffset,
#ifdef CONFIG_NO_IDLE_HZ
	.dyn_tick	= &sa1100_dyn_tick,
#endif
};
