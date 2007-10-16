/* $Id: time.c,v 1.5 2004/09/29 06:12:46 starvik Exp $
 *
 *  linux/arch/cris/arch-v10/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *  Copyright (C) 1999-2002 Axis Communications AB
 *
 */

#include <linux/timex.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/swap.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <asm/arch/svinto.h>
#include <asm/types.h>
#include <asm/signal.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/rtc.h>

/* define this if you need to use print_timestamp */
/* it will make jiffies at 96 hz instead of 100 hz though */
#undef USE_CASCADE_TIMERS

extern void update_xtime_from_cmos(void);
extern int set_rtc_mmss(unsigned long nowtime);
extern int setup_irq(int, struct irqaction *);
extern int have_rtc;

unsigned long get_ns_in_jiffie(void)
{
	unsigned char timer_count, t1;
	unsigned short presc_count;
	unsigned long ns;
	unsigned long flags;

	local_irq_save(flags);
	timer_count = *R_TIMER0_DATA;
	presc_count = *R_TIM_PRESC_STATUS;  
	/* presc_count might be wrapped */
	t1 = *R_TIMER0_DATA;

	if (timer_count != t1){
		/* it wrapped, read prescaler again...  */
		presc_count = *R_TIM_PRESC_STATUS;
		timer_count = t1;
	}
	local_irq_restore(flags);
	if (presc_count >= PRESCALE_VALUE/2 ){
		presc_count =  PRESCALE_VALUE - presc_count + PRESCALE_VALUE/2;
	} else {
		presc_count =  PRESCALE_VALUE - presc_count - PRESCALE_VALUE/2;
	}

	ns = ( (TIMER0_DIV - timer_count) * ((1000000000/HZ)/TIMER0_DIV )) + 
	     ( (presc_count) * (1000000000/PRESCALE_FREQ));
	return ns;
}

unsigned long do_slow_gettimeoffset(void)
{
	unsigned long count, t1;
	unsigned long usec_count = 0;
	unsigned short presc_count;

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

#ifndef CONFIG_SVINTO_SIM
	/* Not available in the xsim simulator. */
	count = *R_TIMER0_DATA;
	presc_count = *R_TIM_PRESC_STATUS;  
	/* presc_count might be wrapped */
	t1 = *R_TIMER0_DATA;
	if (count != t1){
		/* it wrapped, read prescaler again...  */
		presc_count = *R_TIM_PRESC_STATUS;
		count = t1;
	}
#else
	count = 0;
	presc_count = 0;
#endif

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
	if (presc_count >= PRESCALE_VALUE/2 ){
		presc_count =  PRESCALE_VALUE - presc_count + PRESCALE_VALUE/2;
	} else {
		presc_count =  PRESCALE_VALUE - presc_count - PRESCALE_VALUE/2;
	}
	/* Convert timer value to usec */
	usec_count += ( (TIMER0_DIV - count) * (1000000/HZ)/TIMER0_DIV ) +
	              (( (presc_count) * (1000000000/PRESCALE_FREQ))/1000);

	return usec_count;
}

/* Excerpt from the Etrax100 HSDD about the built-in watchdog:
 *
 * 3.10.4 Watchdog timer

 * When the watchdog timer is started, it generates an NMI if the watchdog
 * isn't restarted or stopped within 0.1 s. If it still isn't restarted or
 * stopped after an additional 3.3 ms, the watchdog resets the chip.
 * The watchdog timer is stopped after reset. The watchdog timer is controlled
 * by the R_WATCHDOG register. The R_WATCHDOG register contains an enable bit
 * and a 3-bit key value. The effect of writing to the R_WATCHDOG register is
 * described in the table below:
 * 
 *   Watchdog    Value written:
 *   state:      To enable:  To key:      Operation:
 *   --------    ----------  -------      ----------
 *   stopped         0         X          No effect.
 *   stopped         1       key_val      Start watchdog with key = key_val.
 *   started         0       ~key         Stop watchdog
 *   started         1       ~key         Restart watchdog with key = ~key.
 *   started         X       new_key_val  Change key to new_key_val.
 * 
 * Note: '~' is the bitwise NOT operator.
 * 
 */

/* right now, starting the watchdog is the same as resetting it */
#define start_watchdog reset_watchdog

#if defined(CONFIG_ETRAX_WATCHDOG) && !defined(CONFIG_SVINTO_SIM)
static int watchdog_key = 0;  /* arbitrary number */
#endif

/* number of pages to consider "out of memory". it is normal that the memory
 * is used though, so put this really low.
 */

#define WATCHDOG_MIN_FREE_PAGES 8

void
reset_watchdog(void)
{
#if defined(CONFIG_ETRAX_WATCHDOG) && !defined(CONFIG_SVINTO_SIM)
	/* only keep watchdog happy as long as we have memory left! */
	if(nr_free_pages() > WATCHDOG_MIN_FREE_PAGES) {
		/* reset the watchdog with the inverse of the old key */
		watchdog_key ^= 0x7; /* invert key, which is 3 bits */
		*R_WATCHDOG = IO_FIELD(R_WATCHDOG, key, watchdog_key) |
			IO_STATE(R_WATCHDOG, enable, start);
	}
#endif
}

/* stop the watchdog - we still need the correct key */

void 
stop_watchdog(void)
{
#if defined(CONFIG_ETRAX_WATCHDOG) && !defined(CONFIG_SVINTO_SIM)
	watchdog_key ^= 0x7; /* invert key, which is 3 bits */
	*R_WATCHDOG = IO_FIELD(R_WATCHDOG, key, watchdog_key) |
		IO_STATE(R_WATCHDOG, enable, stop);
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
	/* acknowledge the timer irq */

#ifdef USE_CASCADE_TIMERS
	*R_TIMER_CTRL =
		IO_FIELD( R_TIMER_CTRL, timerdiv1, 0) |
		IO_FIELD( R_TIMER_CTRL, timerdiv0, 0) |
		IO_STATE( R_TIMER_CTRL, i1, clr) |
		IO_STATE( R_TIMER_CTRL, tm1, run) |
		IO_STATE( R_TIMER_CTRL, clksel1, cascade0) |
		IO_STATE( R_TIMER_CTRL, i0, clr) |
		IO_STATE( R_TIMER_CTRL, tm0, run) |
		IO_STATE( R_TIMER_CTRL, clksel0, c6250kHz);
#else
	*R_TIMER_CTRL = r_timer_ctrl_shadow | 
		IO_STATE(R_TIMER_CTRL, i0, clr);
#endif

	/* reset watchdog otherwise it resets us! */

	reset_watchdog();
	
	/* call the real timer interrupt handler */

	do_timer(1);
	
        cris_do_profile(regs); /* Save profiling information */

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 *
	 * The division here is not time critical since it will run once in 
	 * 11 minutes
	 */
	if (ntp_synced() &&
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

static struct irqaction irq2  = {
	.handler = timer_interrupt,
	.flags = IRQF_SHARED | IRQF_DISABLED,
	.mask = CPU_MASK_NONE,
	.name = "timer",
};

void __init
time_init(void)
{	
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

	/* Setup the etrax timers
	 * Base frequency is 25000 hz, divider 250 -> 100 HZ
	 * In normal mode, we use timer0, so timer1 is free. In cascade
	 * mode (which we sometimes use for debugging) both timers are used.
	 * Remember that linux/timex.h contains #defines that rely on the
	 * timer settings below (hz and divide factor) !!!
	 */
	
#ifdef USE_CASCADE_TIMERS
	*R_TIMER_CTRL =
		IO_FIELD( R_TIMER_CTRL, timerdiv1, 0) |
		IO_FIELD( R_TIMER_CTRL, timerdiv0, 0) |
		IO_STATE( R_TIMER_CTRL, i1, nop) |
		IO_STATE( R_TIMER_CTRL, tm1, stop_ld) |
		IO_STATE( R_TIMER_CTRL, clksel1, cascade0) |
		IO_STATE( R_TIMER_CTRL, i0, nop) |
		IO_STATE( R_TIMER_CTRL, tm0, stop_ld) |
		IO_STATE( R_TIMER_CTRL, clksel0, c6250kHz);
	
	*R_TIMER_CTRL = r_timer_ctrl_shadow = 
		IO_FIELD( R_TIMER_CTRL, timerdiv1, 0) |
		IO_FIELD( R_TIMER_CTRL, timerdiv0, 0) |
		IO_STATE( R_TIMER_CTRL, i1, nop) |
		IO_STATE( R_TIMER_CTRL, tm1, run) |
		IO_STATE( R_TIMER_CTRL, clksel1, cascade0) |
		IO_STATE( R_TIMER_CTRL, i0, nop) |
		IO_STATE( R_TIMER_CTRL, tm0, run) |
		IO_STATE( R_TIMER_CTRL, clksel0, c6250kHz);
#else
	*R_TIMER_CTRL = 
		IO_FIELD(R_TIMER_CTRL, timerdiv1, 192)      | 
		IO_FIELD(R_TIMER_CTRL, timerdiv0, TIMER0_DIV)      |
		IO_STATE(R_TIMER_CTRL, i1,        nop)      | 
		IO_STATE(R_TIMER_CTRL, tm1,       stop_ld)  |
		IO_STATE(R_TIMER_CTRL, clksel1,   c19k2Hz)  |
		IO_STATE(R_TIMER_CTRL, i0,        nop)      |
		IO_STATE(R_TIMER_CTRL, tm0,       stop_ld)  |
		IO_STATE(R_TIMER_CTRL, clksel0,   flexible);
	
	*R_TIMER_CTRL = r_timer_ctrl_shadow =
		IO_FIELD(R_TIMER_CTRL, timerdiv1, 192)      | 
		IO_FIELD(R_TIMER_CTRL, timerdiv0, TIMER0_DIV)      |
		IO_STATE(R_TIMER_CTRL, i1,        nop)      |
		IO_STATE(R_TIMER_CTRL, tm1,       run)      |
		IO_STATE(R_TIMER_CTRL, clksel1,   c19k2Hz)  |
		IO_STATE(R_TIMER_CTRL, i0,        nop)      |
		IO_STATE(R_TIMER_CTRL, tm0,       run)      |
		IO_STATE(R_TIMER_CTRL, clksel0,   flexible);

	*R_TIMER_PRESCALE = PRESCALE_VALUE;
#endif

	*R_IRQ_MASK0_SET =
		IO_STATE(R_IRQ_MASK0_SET, timer0, set); /* unmask the timer irq */
	
	/* now actually register the timer irq handler that calls timer_interrupt() */
	
	setup_irq(2, &irq2); /* irq 2 is the timer0 irq in etrax */

	/* enable watchdog if we should use one */

#if defined(CONFIG_ETRAX_WATCHDOG) && !defined(CONFIG_SVINTO_SIM)
	printk("Enabling watchdog...\n");
	start_watchdog();

	/* If we use the hardware watchdog, we want to trap it as an NMI
	   and dump registers before it resets us.  For this to happen, we
	   must set the "m" NMI enable flag (which once set, is unset only
	   when an NMI is taken).

	   The same goes for the external NMI, but that doesn't have any
	   driver or infrastructure support yet.  */
	asm ("setf m");

	*R_IRQ_MASK0_SET =
		IO_STATE(R_IRQ_MASK0_SET, watchdog_nmi, set);
	*R_VECT_MASK_SET =
		IO_STATE(R_VECT_MASK_SET, nmi, set);
#endif
}
