/*
 * arch/sh/kernel/time_64.c
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003 - 2007  Paul Mundt
 * Copyright (C) 2003  Richard Curnow
 *
 *    Original TMU/RTC code taken from sh version.
 *    Copyright (C) 1999  Tetsuya Okada & Niibe Yutaka
 *      Some code taken from i386 version.
 *      Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/errno.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/profile.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/bcd.h>
#include <linux/timex.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <asm/cpu/registers.h>	 /* required by inline __asm__ stmt. */
#include <asm/cpu/irq.h>
#include <asm/addrspace.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/delay.h>

#define TMU_TOCR_INIT	0x00
#define TMU0_TCR_INIT	0x0020
#define TMU_TSTR_INIT	1
#define TMU_TSTR_OFF	0

/* Real Time Clock */
#define	RTC_BLOCK_OFF	0x01040000
#define RTC_BASE	PHYS_PERIPHERAL_BLOCK + RTC_BLOCK_OFF
#define RTC_RCR1_CIE	0x10	/* Carry Interrupt Enable */
#define RTC_RCR1	(rtc_base + 0x38)

/* Clock, Power and Reset Controller */
#define	CPRC_BLOCK_OFF	0x01010000
#define CPRC_BASE	PHYS_PERIPHERAL_BLOCK + CPRC_BLOCK_OFF

#define FRQCR		(cprc_base+0x0)
#define WTCSR		(cprc_base+0x0018)
#define STBCR		(cprc_base+0x0030)

/* Time Management Unit */
#define	TMU_BLOCK_OFF	0x01020000
#define TMU_BASE	PHYS_PERIPHERAL_BLOCK + TMU_BLOCK_OFF
#define TMU0_BASE	tmu_base + 0x8 + (0xc * 0x0)
#define TMU1_BASE	tmu_base + 0x8 + (0xc * 0x1)
#define TMU2_BASE	tmu_base + 0x8 + (0xc * 0x2)

#define TMU_TOCR	tmu_base+0x0	/* Byte access */
#define TMU_TSTR	tmu_base+0x4	/* Byte access */

#define TMU0_TCOR	TMU0_BASE+0x0	/* Long access */
#define TMU0_TCNT	TMU0_BASE+0x4	/* Long access */
#define TMU0_TCR	TMU0_BASE+0x8	/* Word access */

#define TICK_SIZE (tick_nsec / 1000)

static unsigned long tmu_base, rtc_base;
unsigned long cprc_base;

/* Variables to allow interpolation of time of day to resolution better than a
 * jiffy. */

/* This is effectively protected by xtime_lock */
static unsigned long ctc_last_interrupt;
static unsigned long long usecs_per_jiffy = 1000000/HZ; /* Approximation */

#define CTC_JIFFY_SCALE_SHIFT 40

/* 2**CTC_JIFFY_SCALE_SHIFT / ctc_ticks_per_jiffy */
static unsigned long long scaled_recip_ctc_ticks_per_jiffy;

/* Estimate number of microseconds that have elapsed since the last timer tick,
   by scaling the delta that has occurred in the CTC register.

   WARNING WARNING WARNING : This algorithm relies on the CTC decrementing at
   the CPU clock rate.  If the CPU sleeps, the CTC stops counting.  Bear this
   in mind if enabling SLEEP_WORKS in process.c.  In that case, this algorithm
   probably needs to use TMU.TCNT0 instead.  This will work even if the CPU is
   sleeping, though will be coarser.

   FIXME : What if usecs_per_tick is moving around too much, e.g. if an adjtime
   is running or if the freq or tick arguments of adjtimex are modified after
   we have calibrated the scaling factor?  This will result in either a jump at
   the end of a tick period, or a wrap backwards at the start of the next one,
   if the application is reading the time of day often enough.  I think we
   ought to do better than this.  For this reason, usecs_per_jiffy is left
   separated out in the calculation below.  This allows some future hook into
   the adjtime-related stuff in kernel/timer.c to remove this hazard.

*/

static unsigned long usecs_since_tick(void)
{
	unsigned long long current_ctc;
	long ctc_ticks_since_interrupt;
	unsigned long long ull_ctc_ticks_since_interrupt;
	unsigned long result;

	unsigned long long mul1_out;
	unsigned long long mul1_out_high;
	unsigned long long mul2_out_low, mul2_out_high;

	/* Read CTC register */
	asm ("getcon cr62, %0" : "=r" (current_ctc));
	/* Note, the CTC counts down on each CPU clock, not up.
	   Note(2), use long type to get correct wraparound arithmetic when
	   the counter crosses zero. */
	ctc_ticks_since_interrupt = (long) ctc_last_interrupt - (long) current_ctc;
	ull_ctc_ticks_since_interrupt = (unsigned long long) ctc_ticks_since_interrupt;

	/* Inline assembly to do 32x32x32->64 multiplier */
	asm volatile ("mulu.l %1, %2, %0" :
	     "=r" (mul1_out) :
	     "r" (ull_ctc_ticks_since_interrupt), "r" (usecs_per_jiffy));

	mul1_out_high = mul1_out >> 32;

	asm volatile ("mulu.l %1, %2, %0" :
	     "=r" (mul2_out_low) :
	     "r" (mul1_out), "r" (scaled_recip_ctc_ticks_per_jiffy));

#if 1
	asm volatile ("mulu.l %1, %2, %0" :
	     "=r" (mul2_out_high) :
	     "r" (mul1_out_high), "r" (scaled_recip_ctc_ticks_per_jiffy));
#endif

	result = (unsigned long) (((mul2_out_high << 32) + mul2_out_low) >> CTC_JIFFY_SCALE_SHIFT);

	return result;
}

void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;
	unsigned long seq;
	unsigned long usec, sec;

	do {
		seq = read_seqbegin_irqsave(&xtime_lock, flags);
		usec = usecs_since_tick();
		sec = xtime.tv_sec;
		usec += xtime.tv_nsec / 1000;
	} while (read_seqretry_irqrestore(&xtime_lock, seq, flags));

	while (usec >= 1000000) {
		usec -= 1000000;
		sec++;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}
EXPORT_SYMBOL(do_gettimeofday);

int do_settimeofday(struct timespec *tv)
{
	time_t wtm_sec, sec = tv->tv_sec;
	long wtm_nsec, nsec = tv->tv_nsec;

	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irq(&xtime_lock);
	/*
	 * This is revolting. We need to set "xtime" correctly. However, the
	 * value in this location is the value at the most recent update of
	 * wall time.  Discover what correction gettimeofday() would have
	 * made, and then undo it!
	 */
	nsec -= 1000 * usecs_since_tick();

	wtm_sec  = wall_to_monotonic.tv_sec + (xtime.tv_sec - sec);
	wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - nsec);

	set_normalized_timespec(&xtime, sec, nsec);
	set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

	ntp_clear();
	write_sequnlock_irq(&xtime_lock);
	clock_was_set();

	return 0;
}
EXPORT_SYMBOL(do_settimeofday);

/* Dummy RTC ops */
static void null_rtc_get_time(struct timespec *tv)
{
	tv->tv_sec = mktime(2000, 1, 1, 0, 0, 0);
	tv->tv_nsec = 0;
}

static int null_rtc_set_time(const time_t secs)
{
	return 0;
}

void (*rtc_sh_get_time)(struct timespec *) = null_rtc_get_time;
int (*rtc_sh_set_time)(const time_t) = null_rtc_set_time;

/* last time the RTC clock got updated */
static long last_rtc_update;

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
static inline void do_timer_interrupt(void)
{
	unsigned long long current_ctc;

	if (current->pid)
		profile_tick(CPU_PROFILING);

	/*
	 * Here we are in the timer irq handler. We just have irqs locally
	 * disabled but we don't know if the timer_bh is running on the other
	 * CPU. We need to avoid to SMP race with it. NOTE: we don' t need
	 * the irq version of write_lock because as just said we have irq
	 * locally disabled. -arca
	 */
	write_seqlock(&xtime_lock);
	asm ("getcon cr62, %0" : "=r" (current_ctc));
	ctc_last_interrupt = (unsigned long) current_ctc;

	do_timer(1);

#ifdef CONFIG_HEARTBEAT
	if (sh_mv.mv_heartbeat != NULL)
		sh_mv.mv_heartbeat();
#endif

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * RTC clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	if (ntp_synced() &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    (xtime.tv_nsec / 1000) >= 500000 - ((unsigned) TICK_SIZE) / 2 &&
	    (xtime.tv_nsec / 1000) <= 500000 + ((unsigned) TICK_SIZE) / 2) {
		if (rtc_sh_set_time(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			/* do it again in 60 s */
			last_rtc_update = xtime.tv_sec - 600;
	}
	write_sequnlock(&xtime_lock);

#ifndef CONFIG_SMP
	update_process_times(user_mode(get_irq_regs()));
#endif
}

/*
 * This is the same as the above, except we _also_ save the current
 * Time Stamp Counter value at the time of the timer interrupt, so that
 * we later on can estimate the time of day more exactly.
 */
static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	unsigned long timer_status;

	/* Clear UNF bit */
	timer_status = ctrl_inw(TMU0_TCR);
	timer_status &= ~0x100;
	ctrl_outw(timer_status, TMU0_TCR);

	do_timer_interrupt();

	return IRQ_HANDLED;
}


static __init unsigned int get_cpu_hz(void)
{
	unsigned int count;
	unsigned long __dummy;
	unsigned long ctc_val_init, ctc_val;

	/*
	** Regardless the toolchain, force the compiler to use the
	** arbitrary register r3 as a clock tick counter.
	** NOTE: r3 must be in accordance with sh64_rtc_interrupt()
	*/
	register unsigned long long  __rtc_irq_flag __asm__ ("r3");

	local_irq_enable();
	do {} while (ctrl_inb(rtc_base) != 0);
	ctrl_outb(RTC_RCR1_CIE, RTC_RCR1); /* Enable carry interrupt */

	/*
	 * r3 is arbitrary. CDC does not support "=z".
	 */
	ctc_val_init = 0xffffffff;
	ctc_val = ctc_val_init;

	asm volatile("gettr	tr0, %1\n\t"
		     "putcon	%0, " __CTC "\n\t"
		     "and	%2, r63, %2\n\t"
		     "pta	$+4, tr0\n\t"
		     "beq/l	%2, r63, tr0\n\t"
		     "ptabs	%1, tr0\n\t"
		     "getcon	" __CTC ", %0\n\t"
		: "=r"(ctc_val), "=r" (__dummy), "=r" (__rtc_irq_flag)
		: "0" (0));
	local_irq_disable();
	/*
	 * SH-3:
	 * CPU clock = 4 stages * loop
	 * tst    rm,rm      if id ex
	 * bt/s   1b            if id ex
	 * add    #1,rd            if id ex
         *                            (if) pipe line stole
	 * tst    rm,rm                  if id ex
         * ....
	 *
	 *
	 * SH-4:
	 * CPU clock = 6 stages * loop
	 * I don't know why.
         * ....
	 *
	 * SH-5:
	 * Use CTC register to count.  This approach returns the right value
	 * even if the I-cache is disabled (e.g. whilst debugging.)
	 *
	 */

	count = ctc_val_init - ctc_val; /* CTC counts down */

	/*
	 * This really is count by the number of clock cycles
         * by the ratio between a complete R64CNT
         * wrap-around (128) and CUI interrupt being raised (64).
	 */
	return count*2;
}

static irqreturn_t sh64_rtc_interrupt(int irq, void *dev_id)
{
	struct pt_regs *regs = get_irq_regs();

	ctrl_outb(0, RTC_RCR1);	/* Disable Carry Interrupts */
	regs->regs[3] = 1;	/* Using r3 */

	return IRQ_HANDLED;
}

static struct irqaction irq0  = {
	.handler = timer_interrupt,
	.flags = IRQF_DISABLED,
	.mask = CPU_MASK_NONE,
	.name = "timer",
};
static struct irqaction irq1  = {
	.handler = sh64_rtc_interrupt,
	.flags = IRQF_DISABLED,
	.mask = CPU_MASK_NONE,
	.name = "rtc",
};

void __init time_init(void)
{
	unsigned int cpu_clock, master_clock, bus_clock, module_clock;
	unsigned long interval;
	unsigned long frqcr, ifc, pfc;
	static int ifc_table[] = { 2, 4, 6, 8, 10, 12, 16, 24 };
#define bfc_table ifc_table	/* Same */
#define pfc_table ifc_table	/* Same */

	tmu_base = onchip_remap(TMU_BASE, 1024, "TMU");
	if (!tmu_base) {
		panic("Unable to remap TMU\n");
	}

	rtc_base = onchip_remap(RTC_BASE, 1024, "RTC");
	if (!rtc_base) {
		panic("Unable to remap RTC\n");
	}

	cprc_base = onchip_remap(CPRC_BASE, 1024, "CPRC");
	if (!cprc_base) {
		panic("Unable to remap CPRC\n");
	}

	rtc_sh_get_time(&xtime);

	setup_irq(TIMER_IRQ, &irq0);
	setup_irq(RTC_IRQ, &irq1);

	/* Check how fast it is.. */
	cpu_clock = get_cpu_hz();

	/* Note careful order of operations to maintain reasonable precision and avoid overflow. */
	scaled_recip_ctc_ticks_per_jiffy = ((1ULL << CTC_JIFFY_SCALE_SHIFT) / (unsigned long long)(cpu_clock / HZ));

	free_irq(RTC_IRQ, NULL);

	printk("CPU clock: %d.%02dMHz\n",
	       (cpu_clock / 1000000), (cpu_clock % 1000000)/10000);
	{
		unsigned short bfc;
		frqcr = ctrl_inl(FRQCR);
		ifc  = ifc_table[(frqcr>> 6) & 0x0007];
		bfc  = bfc_table[(frqcr>> 3) & 0x0007];
		pfc  = pfc_table[(frqcr>> 12) & 0x0007];
		master_clock = cpu_clock * ifc;
		bus_clock = master_clock/bfc;
	}

	printk("Bus clock: %d.%02dMHz\n",
	       (bus_clock/1000000), (bus_clock % 1000000)/10000);
	module_clock = master_clock/pfc;
	printk("Module clock: %d.%02dMHz\n",
	       (module_clock/1000000), (module_clock % 1000000)/10000);
	interval = (module_clock/(HZ*4));

	printk("Interval = %ld\n", interval);

	current_cpu_data.cpu_clock    = cpu_clock;
	current_cpu_data.master_clock = master_clock;
	current_cpu_data.bus_clock    = bus_clock;
	current_cpu_data.module_clock = module_clock;

	/* Start TMU0 */
	ctrl_outb(TMU_TSTR_OFF, TMU_TSTR);
	ctrl_outb(TMU_TOCR_INIT, TMU_TOCR);
	ctrl_outw(TMU0_TCR_INIT, TMU0_TCR);
	ctrl_outl(interval, TMU0_TCOR);
	ctrl_outl(interval, TMU0_TCNT);
	ctrl_outb(TMU_TSTR_INIT, TMU_TSTR);
}

void enter_deep_standby(void)
{
	/* Disable watchdog timer */
	ctrl_outl(0xa5000000, WTCSR);
	/* Configure deep standby on sleep */
	ctrl_outl(0x03, STBCR);

#ifdef CONFIG_SH_ALPHANUMERIC
	{
		extern void mach_alphanum(int position, unsigned char value);
		extern void mach_alphanum_brightness(int setting);
		char halted[] = "Halted. ";
		int i;
		mach_alphanum_brightness(6); /* dimmest setting above off */
		for (i=0; i<8; i++) {
			mach_alphanum(i, halted[i]);
		}
		asm __volatile__ ("synco");
	}
#endif

	asm __volatile__ ("sleep");
	asm __volatile__ ("synci");
	asm __volatile__ ("nop");
	asm __volatile__ ("nop");
	asm __volatile__ ("nop");
	asm __volatile__ ("nop");
	panic("Unexpected wakeup!\n");
}

static struct resource rtc_resources[] = {
	[0] = {
		/* RTC base, filled in by rtc_init */
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		/* Period IRQ */
		.start	= IRQ_PRI,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* Carry IRQ */
		.start	= IRQ_CUI,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		/* Alarm IRQ */
		.start	= IRQ_ATI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device rtc_device = {
	.name		= "sh-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rtc_resources),
	.resource	= rtc_resources,
};

static int __init rtc_init(void)
{
	rtc_resources[0].start	= rtc_base;
	rtc_resources[0].end	= rtc_resources[0].start + 0x58 - 1;

	return platform_device_register(&rtc_device);
}
device_initcall(rtc_init);
