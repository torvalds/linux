/* time.c: FRV arch-specific time handling
 *
 * Copyright (C) 2003-5 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from arch/m68k/kernel/time.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/profile.h>
#include <linux/irq.h>
#include <linux/mm.h>

#include <asm/io.h>
#include <asm/timer-regs.h>
#include <asm/mb-regs.h>
#include <asm/mb86943a.h>

#include <linux/timex.h>

#define TICK_SIZE (tick_nsec / 1000)

unsigned long __nongprelbss __clkin_clock_speed_HZ;
unsigned long __nongprelbss __ext_bus_clock_speed_HZ;
unsigned long __nongprelbss __res_bus_clock_speed_HZ;
unsigned long __nongprelbss __sdram_clock_speed_HZ;
unsigned long __nongprelbss __core_bus_clock_speed_HZ;
unsigned long __nongprelbss __core_clock_speed_HZ;
unsigned long __nongprelbss __dsu_clock_speed_HZ;
unsigned long __nongprelbss __serial_clock_speed_HZ;
unsigned long __delay_loops_MHz;

static irqreturn_t timer_interrupt(int irq, void *dummy);

static struct irqaction timer_irq  = {
	.handler = timer_interrupt,
	.flags = IRQF_DISABLED,
	.mask = CPU_MASK_NONE,
	.name = "timer",
};

static inline int set_rtc_mmss(unsigned long nowtime)
{
	return -1;
}

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
static irqreturn_t timer_interrupt(int irq, void *dummy)
{
	/* last time the cmos clock got updated */
	static long last_rtc_update = 0;

	/*
	 * Here we are in the timer irq handler. We just have irqs locally
	 * disabled but we don't know if the timer_bh is running on the other
	 * CPU. We need to avoid to SMP race with it. NOTE: we don' t need
	 * the irq version of write_lock because as just said we have irq
	 * locally disabled. -arca
	 */
	write_seqlock(&xtime_lock);

	do_timer(1);
	update_process_times(user_mode(get_irq_regs()));
	profile_tick(CPU_PROFILING);

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if (ntp_synced() &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    (xtime.tv_nsec / 1000) >= 500000 - ((unsigned) TICK_SIZE) / 2 &&
	    (xtime.tv_nsec / 1000) <= 500000 + ((unsigned) TICK_SIZE) / 2
	    ) {
		if (set_rtc_mmss(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
	}

#ifdef CONFIG_HEARTBEAT
	static unsigned short n;
	n++;
	__set_LEDS(n);
#endif /* CONFIG_HEARTBEAT */

	write_sequnlock(&xtime_lock);
	return IRQ_HANDLED;
}

void time_divisor_init(void)
{
	unsigned short base, pre, prediv;

	/* set the scheduling timer going */
	pre = 1;
	prediv = 4;
	base = __res_bus_clock_speed_HZ / pre / HZ / (1 << prediv);

	__set_TPRV(pre);
	__set_TxCKSL_DATA(0, prediv);
	__set_TCTR(TCTR_SC_CTR0 | TCTR_RL_RW_LH8 | TCTR_MODE_2);
	__set_TCSR_DATA(0, base & 0xff);
	__set_TCSR_DATA(0, base >> 8);
}

void time_init(void)
{
	unsigned int year, mon, day, hour, min, sec;

	extern void arch_gettod(int *year, int *mon, int *day, int *hour, int *min, int *sec);

	/* FIX by dqg : Set to zero for platforms that don't have tod */
	/* without this time is undefined and can overflow time_t, causing  */
	/* very stange errors */
	year = 1980;
	mon = day = 1;
	hour = min = sec = 0;
	arch_gettod (&year, &mon, &day, &hour, &min, &sec);

	if ((year += 1900) < 1970)
		year += 100;
	xtime.tv_sec = mktime(year, mon, day, hour, min, sec);
	xtime.tv_nsec = 0;

	/* install scheduling interrupt handler */
	setup_irq(IRQ_CPU_TIMER0, &timer_irq);

	time_divisor_init();
}

/*
 * Scheduler clock - returns current time in nanosec units.
 */
unsigned long long sched_clock(void)
{
	return jiffies_64 * (1000000000 / HZ);
}
