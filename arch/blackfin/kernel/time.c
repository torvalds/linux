/*
 * arch/blackfin/kernel/time.c
 *
 * This file contains the Blackfin-specific time handling details.
 * Most of the stuff is located in the machine specific files.
 *
 * Copyright 2004-2008 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/profile.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/sched.h>

#include <asm/blackfin.h>
#include <asm/time.h>
#include <asm/gptimers.h>

/* This is an NTP setting */
#define	TICK_SIZE (tick_nsec / 1000)

static struct irqaction bfin_timer_irq = {
	.name = "Blackfin Timer Tick",
};

#if defined(CONFIG_IPIPE)
void __init setup_system_timer0(void)
{
	/* Power down the core timer, just to play safe. */
	bfin_write_TCNTL(0);

	disable_gptimers(TIMER0bit);
	set_gptimer_status(0, TIMER_STATUS_TRUN0);
	while (get_gptimer_status(0) & TIMER_STATUS_TRUN0)
		udelay(10);

	set_gptimer_config(0, 0x59); /* IRQ enable, periodic, PWM_OUT, SCLKed, OUT PAD disabled */
	set_gptimer_period(TIMER0_id, get_sclk() / HZ);
	set_gptimer_pwidth(TIMER0_id, 1);
	SSYNC();
	enable_gptimers(TIMER0bit);
}
#else
void __init setup_core_timer(void)
{
	u32 tcount;

	/* power up the timer, but don't enable it just yet */
	bfin_write_TCNTL(TMPWR);
	CSYNC();

	/* the TSCALE prescaler counter */
	bfin_write_TSCALE(TIME_SCALE - 1);

	tcount = ((get_cclk() / (HZ * TIME_SCALE)) - 1);
	bfin_write_TPERIOD(tcount);
	bfin_write_TCOUNT(tcount);

	/* now enable the timer */
	CSYNC();

	bfin_write_TCNTL(TAUTORLD | TMREN | TMPWR);
}
#endif

static void __init
time_sched_init(irqreturn_t(*timer_routine) (int, void *))
{
#if defined(CONFIG_IPIPE)
	setup_system_timer0();
	bfin_timer_irq.handler = timer_routine;
	setup_irq(IRQ_TIMER0, &bfin_timer_irq);
#else
	setup_core_timer();
	bfin_timer_irq.handler = timer_routine;
	setup_irq(IRQ_CORETMR, &bfin_timer_irq);
#endif
}

#ifdef CONFIG_ARCH_USES_GETTIMEOFFSET
/*
 * Should return useconds since last timer tick
 */
u32 arch_gettimeoffset(void)
{
	unsigned long offset;
	unsigned long clocks_per_jiffy;

#if defined(CONFIG_IPIPE)
	clocks_per_jiffy = bfin_read_TIMER0_PERIOD();
	offset = bfin_read_TIMER0_COUNTER() / \
		(((clocks_per_jiffy + 1) * HZ) / USEC_PER_SEC);

	if ((get_gptimer_status(0) & TIMER_STATUS_TIMIL0) && offset < (100000 / HZ / 2))
		offset += (USEC_PER_SEC / HZ);
#else
	clocks_per_jiffy = bfin_read_TPERIOD();
	offset = (clocks_per_jiffy - bfin_read_TCOUNT()) / \
		(((clocks_per_jiffy + 1) * HZ) / USEC_PER_SEC);

	/* Check if we just wrapped the counters and maybe missed a tick */
	if ((bfin_read_ILAT() & (1 << IRQ_CORETMR))
		&& (offset < (100000 / HZ / 2)))
		offset += (USEC_PER_SEC / HZ);
#endif
	return offset;
}
#endif

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "xtime_update()" routine every clocktick
 */
#ifdef CONFIG_CORE_TIMER_IRQ_L1
__attribute__((l1_text))
#endif
irqreturn_t timer_interrupt(int irq, void *dummy)
{
	xtime_update(1);

#ifdef CONFIG_IPIPE
	update_root_process_times(get_irq_regs());
#else
	update_process_times(user_mode(get_irq_regs()));
#endif
	profile_tick(CPU_PROFILING);

	return IRQ_HANDLED;
}

void read_persistent_clock(struct timespec *ts)
{
	time_t secs_since_1970 = (365 * 37 + 9) * 24 * 60 * 60;	/* 1 Jan 2007 */
	ts->tv_sec = secs_since_1970;
	ts->tv_nsec = 0;
}

void __init time_init(void)
{
#ifdef CONFIG_RTC_DRV_BFIN
	/* [#2663] hack to filter junk RTC values that would cause
	 * userspace to have to deal with time values greater than
	 * 2^31 seconds (which uClibc cannot cope with yet)
	 */
	if ((bfin_read_RTC_STAT() & 0xC0000000) == 0xC0000000) {
		printk(KERN_NOTICE "bfin-rtc: invalid date; resetting\n");
		bfin_write_RTC_STAT(0);
	}
#endif

	time_sched_init(timer_interrupt);
}
