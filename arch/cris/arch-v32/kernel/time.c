/* $Id: time.c,v 1.19 2005/04/29 05:40:09 starvik Exp $
 *
 *  linux/arch/cris/arch-v32/kernel/time.c
 *
 *  Copyright (C) 2003 Axis Communications AB
 *
 */

#include <linux/timex.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/swap.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/threads.h>
#include <asm/types.h>
#include <asm/signal.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/rtc.h>
#include <asm/irq.h>

#include <asm/arch/hwregs/reg_map.h>
#include <asm/arch/hwregs/reg_rdwr.h>
#include <asm/arch/hwregs/timer_defs.h>
#include <asm/arch/hwregs/intr_vect_defs.h>

/* Watchdog defines */
#define ETRAX_WD_KEY_MASK 0x7F /* key is 7 bit */
#define ETRAX_WD_HZ       763 /* watchdog counts at 763 Hz */
#define ETRAX_WD_CNT      ((2*ETRAX_WD_HZ)/HZ + 1) /* Number of 763 counts before watchdog bites */

unsigned long timer_regs[NR_CPUS] =
{
  regi_timer,
#ifdef CONFIG_SMP
  regi_timer2
#endif
};

extern void update_xtime_from_cmos(void);
extern int set_rtc_mmss(unsigned long nowtime);
extern int setup_irq(int, struct irqaction *);
extern int have_rtc;

unsigned long get_ns_in_jiffie(void)
{
	reg_timer_r_tmr0_data data;
	unsigned long ns;

	data = REG_RD(timer, regi_timer, r_tmr0_data);
	ns = (TIMER0_DIV - data) * 10;
	return ns;
}

unsigned long do_slow_gettimeoffset(void)
{
	unsigned long count;
	unsigned long usec_count = 0;

	static unsigned long count_p = TIMER0_DIV;/* for the first call after boot */
	static unsigned long jiffies_p = 0;

	/*
	 * cache volatile jiffies temporarily; we have IRQs turned off.
	 */
	unsigned long jiffies_t;

	/* The timer interrupt comes from Etrax timer 0. In order to get
	 * better precision, we check the current value. It might have
	 * underflowed already though.
	 */

	count = REG_RD(timer, regi_timer, r_tmr0_data);
 	jiffies_t = jiffies;

	/*
	 * avoiding timer inconsistencies (they are rare, but they happen)...
	 * there are one problem that must be avoided here:
	 *  1. the timer counter underflows
	 */
	if( jiffies_t == jiffies_p ) {
		if( count > count_p ) {
			/* Timer wrapped, use new count and prescale
			 * increase the time corresponding to one jiffie
			 */
			usec_count = 1000000/HZ;
		}
	} else
		jiffies_p = jiffies_t;
        count_p = count;
	/* Convert timer value to usec */
	/* 100 MHz timer, divide by 100 to get usec */
	usec_count +=  (TIMER0_DIV - count) / 100;
	return usec_count;
}

/* From timer MDS describing the hardware watchdog:
 * 4.3.1 Watchdog Operation
 * The watchdog timer is an 8-bit timer with a configurable start value.
 * Once started the watchdog counts downwards with a frequency of 763 Hz
 * (100/131072 MHz). When the watchdog counts down to 1, it generates an
 * NMI (Non Maskable Interrupt), and when it counts down to 0, it resets the
 * chip.
 */
/* This gives us 1.3 ms to do something useful when the NMI comes */

/* right now, starting the watchdog is the same as resetting it */
#define start_watchdog reset_watchdog

#if defined(CONFIG_ETRAX_WATCHDOG)
static short int watchdog_key = 42;  /* arbitrary 7 bit number */
#endif

/* number of pages to consider "out of memory". it is normal that the memory
 * is used though, so put this really low.
 */

#define WATCHDOG_MIN_FREE_PAGES 8

void
reset_watchdog(void)
{
#if defined(CONFIG_ETRAX_WATCHDOG)
	reg_timer_rw_wd_ctrl wd_ctrl = { 0 };

	/* only keep watchdog happy as long as we have memory left! */
	if(nr_free_pages() > WATCHDOG_MIN_FREE_PAGES) {
		/* reset the watchdog with the inverse of the old key */
		watchdog_key ^= ETRAX_WD_KEY_MASK; /* invert key, which is 7 bits */
		wd_ctrl.cnt = ETRAX_WD_CNT;
		wd_ctrl.cmd = regk_timer_start;
		wd_ctrl.key = watchdog_key;
		REG_WR(timer, regi_timer, rw_wd_ctrl, wd_ctrl);
	}
#endif
}

/* stop the watchdog - we still need the correct key */

void
stop_watchdog(void)
{
#if defined(CONFIG_ETRAX_WATCHDOG)
	reg_timer_rw_wd_ctrl wd_ctrl = { 0 };
	watchdog_key ^= ETRAX_WD_KEY_MASK; /* invert key, which is 7 bits */
	wd_ctrl.cnt = ETRAX_WD_CNT;
	wd_ctrl.cmd = regk_timer_stop;
	wd_ctrl.key = watchdog_key;
	REG_WR(timer, regi_timer, rw_wd_ctrl, wd_ctrl);
#endif
}

extern void show_registers(struct pt_regs *regs);

void
handle_watchdog_bite(struct pt_regs* regs)
{
#if defined(CONFIG_ETRAX_WATCHDOG)
	extern int cause_of_death;

	raw_printk("Watchdog bite\n");

	/* Check if forced restart or unexpected watchdog */
	if (cause_of_death == 0xbedead) {
		while(1);
	}

	/* Unexpected watchdog, stop the watchdog and dump registers*/
	stop_watchdog();
	raw_printk("Oops: bitten by watchdog\n");
        show_registers(regs);
#ifndef CONFIG_ETRAX_WATCHDOG_NICE_DOGGY
	reset_watchdog();
#endif
	while(1) /* nothing */;
#endif
}

/* last time the cmos clock got updated */
static long last_rtc_update = 0;

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */

//static unsigned short myjiff; /* used by our debug routine print_timestamp */

extern void cris_do_profile(struct pt_regs *regs);

static inline irqreturn_t
timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	reg_timer_r_masked_intr masked_intr;
	reg_timer_rw_ack_intr ack_intr = { 0 };

	/* Check if the timer interrupt is for us (a tmr0 int) */
	masked_intr = REG_RD(timer, timer_regs[cpu], r_masked_intr);
	if (!masked_intr.tmr0)
		return IRQ_NONE;

	/* acknowledge the timer irq */
	ack_intr.tmr0 = 1;
	REG_WR(timer, timer_regs[cpu], rw_ack_intr, ack_intr);

	/* reset watchdog otherwise it resets us! */
	reset_watchdog();

        /* Update statistics. */
	update_process_times(user_mode(regs));

	cris_do_profile(regs); /* Save profiling information */

	/* The master CPU is responsible for the time keeping. */
	if (cpu != 0)
		return IRQ_HANDLED;

	/* call the real timer interrupt handler */
	do_timer(1);

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 *
	 * The division here is not time critical since it will run once in
	 * 11 minutes
	 */
	if ((time_status & STA_UNSYNC) == 0 &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    (xtime.tv_nsec / 1000) >= 500000 - (tick_nsec / 1000) / 2 &&
	    (xtime.tv_nsec / 1000) <= 500000 + (tick_nsec / 1000) / 2) {
		if (set_rtc_mmss(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
	}
        return IRQ_HANDLED;
}

/* timer is IRQF_SHARED so drivers can add stuff to the timer irq chain
 * it needs to be IRQF_DISABLED to make the jiffies update work properly
 */

static struct irqaction irq_timer  = {
	.mask = timer_interrupt,
	.flags = IRQF_SHARED | IRQF_DISABLED,
	.mask = CPU_MASK_NONE,
	.name = "timer"
};

void __init
cris_timer_init(void)
{
	int cpu = smp_processor_id();
  	reg_timer_rw_tmr0_ctrl tmr0_ctrl = { 0 };
       	reg_timer_rw_tmr0_div tmr0_div = TIMER0_DIV;
	reg_timer_rw_intr_mask timer_intr_mask;

	/* Setup the etrax timers
	 * Base frequency is 100MHz, divider 1000000 -> 100 HZ
	 * We use timer0, so timer1 is free.
	 * The trig timer is used by the fasttimer API if enabled.
	 */

       	tmr0_ctrl.op = regk_timer_ld;
	tmr0_ctrl.freq = regk_timer_f100;
       	REG_WR(timer, timer_regs[cpu], rw_tmr0_div, tmr0_div);
       	REG_WR(timer, timer_regs[cpu], rw_tmr0_ctrl, tmr0_ctrl); /* Load */
       	tmr0_ctrl.op = regk_timer_run;
       	REG_WR(timer, timer_regs[cpu], rw_tmr0_ctrl, tmr0_ctrl); /* Start */

       	/* enable the timer irq */
       	timer_intr_mask = REG_RD(timer, timer_regs[cpu], rw_intr_mask);
       	timer_intr_mask.tmr0 = 1;
       	REG_WR(timer, timer_regs[cpu], rw_intr_mask, timer_intr_mask);
}

void __init
time_init(void)
{
	reg_intr_vect_rw_mask intr_mask;

	/* probe for the RTC and read it if it exists
	 * Before the RTC can be probed the loops_per_usec variable needs
	 * to be initialized to make usleep work. A better value for
	 * loops_per_usec is calculated by the kernel later once the
	 * clock has started.
	 */
	loops_per_usec = 50;

	if(RTC_INIT() < 0) {
		/* no RTC, start at 1980 */
		xtime.tv_sec = 0;
		xtime.tv_nsec = 0;
		have_rtc = 0;
	} else {
		/* get the current time */
		have_rtc = 1;
		update_xtime_from_cmos();
	}

	/*
	 * Initialize wall_to_monotonic such that adding it to xtime will yield zero, the
	 * tv_nsec field must be normalized (i.e., 0 <= nsec < NSEC_PER_SEC).
	 */
	set_normalized_timespec(&wall_to_monotonic, -xtime.tv_sec, -xtime.tv_nsec);

	/* Start CPU local timer */
	cris_timer_init();

	/* enable the timer irq in global config */
	intr_mask = REG_RD(intr_vect, regi_irq, rw_mask);
	intr_mask.timer = 1;
	REG_WR(intr_vect, regi_irq, rw_mask, intr_mask);

	/* now actually register the timer irq handler that calls timer_interrupt() */

	setup_irq(TIMER_INTR_VECT, &irq_timer);

	/* enable watchdog if we should use one */

#if defined(CONFIG_ETRAX_WATCHDOG)
	printk("Enabling watchdog...\n");
	start_watchdog();

	/* If we use the hardware watchdog, we want to trap it as an NMI
	   and dump registers before it resets us.  For this to happen, we
	   must set the "m" NMI enable flag (which once set, is unset only
	   when an NMI is taken).

	   The same goes for the external NMI, but that doesn't have any
	   driver or infrastructure support yet.  */
        {
          unsigned long flags;
          local_save_flags(flags);
          flags |= (1<<30); /* NMI M flag is at bit 30 */
          local_irq_restore(flags);
        }
#endif
}
