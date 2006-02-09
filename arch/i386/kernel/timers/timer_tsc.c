/*
 * This code largely moved from arch/i386/kernel/time.c.
 * See comments there for proper credits.
 *
 * 2004-06-25    Jesper Juhl
 *      moved mark_offset_tsc below cpufreq_delayed_get to avoid gcc 3.4
 *      failing to inline.
 */

#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/timex.h>
#include <linux/errno.h>
#include <linux/cpufreq.h>
#include <linux/string.h>
#include <linux/jiffies.h>

#include <asm/timer.h>
#include <asm/io.h>
/* processor.h for distable_tsc flag */
#include <asm/processor.h>

#include "io_ports.h"
#include "mach_timer.h"

#include <asm/hpet.h>
#include <asm/i8253.h>

#ifdef CONFIG_HPET_TIMER
static unsigned long hpet_usec_quotient;
static unsigned long hpet_last;
static struct timer_opts timer_tsc;
#endif

static inline void cpufreq_delayed_get(void);

int tsc_disable __devinitdata = 0;

static int use_tsc;
/* Number of usecs that the last interrupt was delayed */
static int delay_at_last_interrupt;

static unsigned long last_tsc_low; /* lsb 32 bits of Time Stamp Counter */
static unsigned long last_tsc_high; /* msb 32 bits of Time Stamp Counter */
static unsigned long long monotonic_base;
static seqlock_t monotonic_lock = SEQLOCK_UNLOCKED;

/* Avoid compensating for lost ticks before TSCs are synched */
static int detect_lost_ticks;
static int __init start_lost_tick_compensation(void)
{
	detect_lost_ticks = 1;
	return 0;
}
late_initcall(start_lost_tick_compensation);

/* convert from cycles(64bits) => nanoseconds (64bits)
 *  basic equation:
 *		ns = cycles / (freq / ns_per_sec)
 *		ns = cycles * (ns_per_sec / freq)
 *		ns = cycles * (10^9 / (cpu_khz * 10^3))
 *		ns = cycles * (10^6 / cpu_khz)
 *
 *	Then we use scaling math (suggested by george@mvista.com) to get:
 *		ns = cycles * (10^6 * SC / cpu_khz) / SC
 *		ns = cycles * cyc2ns_scale / SC
 *
 *	And since SC is a constant power of two, we can convert the div
 *  into a shift.
 *
 *  We can use khz divisor instead of mhz to keep a better percision, since
 *  cyc2ns_scale is limited to 10^6 * 2^10, which fits in 32 bits.
 *  (mathieu.desnoyers@polymtl.ca)
 *
 *			-johnstul@us.ibm.com "math is hard, lets go shopping!"
 */
static unsigned long cyc2ns_scale; 
#define CYC2NS_SCALE_FACTOR 10 /* 2^10, carefully chosen */

static inline void set_cyc2ns_scale(unsigned long cpu_khz)
{
	cyc2ns_scale = (1000000 << CYC2NS_SCALE_FACTOR)/cpu_khz;
}

static inline unsigned long long cycles_2_ns(unsigned long long cyc)
{
	return (cyc * cyc2ns_scale) >> CYC2NS_SCALE_FACTOR;
}

static int count2; /* counter for mark_offset_tsc() */

/* Cached *multiplier* to convert TSC counts to microseconds.
 * (see the equation below).
 * Equal to 2^32 * (1 / (clocks per usec) ).
 * Initialized in time_init.
 */
static unsigned long fast_gettimeoffset_quotient;

static unsigned long get_offset_tsc(void)
{
	register unsigned long eax, edx;

	/* Read the Time Stamp Counter */

	rdtsc(eax,edx);

	/* .. relative to previous jiffy (32 bits is enough) */
	eax -= last_tsc_low;	/* tsc_low delta */

	/*
         * Time offset = (tsc_low delta) * fast_gettimeoffset_quotient
         *             = (tsc_low delta) * (usecs_per_clock)
         *             = (tsc_low delta) * (usecs_per_jiffy / clocks_per_jiffy)
	 *
	 * Using a mull instead of a divl saves up to 31 clock cycles
	 * in the critical path.
         */

	__asm__("mull %2"
		:"=a" (eax), "=d" (edx)
		:"rm" (fast_gettimeoffset_quotient),
		 "0" (eax));

	/* our adjusted time offset in microseconds */
	return delay_at_last_interrupt + edx;
}

static unsigned long long monotonic_clock_tsc(void)
{
	unsigned long long last_offset, this_offset, base;
	unsigned seq;
	
	/* atomically read monotonic base & last_offset */
	do {
		seq = read_seqbegin(&monotonic_lock);
		last_offset = ((unsigned long long)last_tsc_high<<32)|last_tsc_low;
		base = monotonic_base;
	} while (read_seqretry(&monotonic_lock, seq));

	/* Read the Time Stamp Counter */
	rdtscll(this_offset);

	/* return the value in ns */
	return base + cycles_2_ns(this_offset - last_offset);
}

/*
 * Scheduler clock - returns current time in nanosec units.
 */
unsigned long long sched_clock(void)
{
	unsigned long long this_offset;

	/*
	 * In the NUMA case we dont use the TSC as they are not
	 * synchronized across all CPUs.
	 */
#ifndef CONFIG_NUMA
	if (!use_tsc)
#endif
		/* no locking but a rare wrong value is not a big deal */
		return jiffies_64 * (1000000000 / HZ);

	/* Read the Time Stamp Counter */
	rdtscll(this_offset);

	/* return the value in ns */
	return cycles_2_ns(this_offset);
}

static void delay_tsc(unsigned long loops)
{
	unsigned long bclock, now;
	
	rdtscl(bclock);
	do
	{
		rep_nop();
		rdtscl(now);
	} while ((now-bclock) < loops);
}

#ifdef CONFIG_HPET_TIMER
static void mark_offset_tsc_hpet(void)
{
	unsigned long long this_offset, last_offset;
 	unsigned long offset, temp, hpet_current;

	write_seqlock(&monotonic_lock);
	last_offset = ((unsigned long long)last_tsc_high<<32)|last_tsc_low;
	/*
	 * It is important that these two operations happen almost at
	 * the same time. We do the RDTSC stuff first, since it's
	 * faster. To avoid any inconsistencies, we need interrupts
	 * disabled locally.
	 */
	/*
	 * Interrupts are just disabled locally since the timer irq
	 * has the SA_INTERRUPT flag set. -arca
	 */
	/* read Pentium cycle counter */

	hpet_current = hpet_readl(HPET_COUNTER);
	rdtsc(last_tsc_low, last_tsc_high);

	/* lost tick compensation */
	offset = hpet_readl(HPET_T0_CMP) - hpet_tick;
	if (unlikely(((offset - hpet_last) > hpet_tick) && (hpet_last != 0))
					&& detect_lost_ticks) {
		int lost_ticks = (offset - hpet_last) / hpet_tick;
		jiffies_64 += lost_ticks;
	}
	hpet_last = hpet_current;

	/* update the monotonic base value */
	this_offset = ((unsigned long long)last_tsc_high<<32)|last_tsc_low;
	monotonic_base += cycles_2_ns(this_offset - last_offset);
	write_sequnlock(&monotonic_lock);

	/* calculate delay_at_last_interrupt */
	/*
	 * Time offset = (hpet delta) * ( usecs per HPET clock )
	 *             = (hpet delta) * ( usecs per tick / HPET clocks per tick)
	 *             = (hpet delta) * ( hpet_usec_quotient ) / (2^32)
	 * Where,
	 * hpet_usec_quotient = (2^32 * usecs per tick)/HPET clocks per tick
	 */
	delay_at_last_interrupt = hpet_current - offset;
	ASM_MUL64_REG(temp, delay_at_last_interrupt,
			hpet_usec_quotient, delay_at_last_interrupt);
}
#endif


#ifdef CONFIG_CPU_FREQ
#include <linux/workqueue.h>

static unsigned int cpufreq_delayed_issched = 0;
static unsigned int cpufreq_init = 0;
static struct work_struct cpufreq_delayed_get_work;

static void handle_cpufreq_delayed_get(void *v)
{
	unsigned int cpu;
	for_each_online_cpu(cpu) {
		cpufreq_get(cpu);
	}
	cpufreq_delayed_issched = 0;
}

/* if we notice lost ticks, schedule a call to cpufreq_get() as it tries
 * to verify the CPU frequency the timing core thinks the CPU is running
 * at is still correct.
 */
static inline void cpufreq_delayed_get(void) 
{
	if (cpufreq_init && !cpufreq_delayed_issched) {
		cpufreq_delayed_issched = 1;
		printk(KERN_DEBUG "Losing some ticks... checking if CPU frequency changed.\n");
		schedule_work(&cpufreq_delayed_get_work);
	}
}

/* If the CPU frequency is scaled, TSC-based delays will need a different
 * loops_per_jiffy value to function properly.
 */

static unsigned int  ref_freq = 0;
static unsigned long loops_per_jiffy_ref = 0;

#ifndef CONFIG_SMP
static unsigned long fast_gettimeoffset_ref = 0;
static unsigned int cpu_khz_ref = 0;
#endif

static int
time_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
		       void *data)
{
	struct cpufreq_freqs *freq = data;

	if (val != CPUFREQ_RESUMECHANGE)
		write_seqlock_irq(&xtime_lock);
	if (!ref_freq) {
		ref_freq = freq->old;
		loops_per_jiffy_ref = cpu_data[freq->cpu].loops_per_jiffy;
#ifndef CONFIG_SMP
		fast_gettimeoffset_ref = fast_gettimeoffset_quotient;
		cpu_khz_ref = cpu_khz;
#endif
	}

	if ((val == CPUFREQ_PRECHANGE  && freq->old < freq->new) ||
	    (val == CPUFREQ_POSTCHANGE && freq->old > freq->new) ||
	    (val == CPUFREQ_RESUMECHANGE)) {
		if (!(freq->flags & CPUFREQ_CONST_LOOPS))
			cpu_data[freq->cpu].loops_per_jiffy = cpufreq_scale(loops_per_jiffy_ref, ref_freq, freq->new);
#ifndef CONFIG_SMP
		if (cpu_khz)
			cpu_khz = cpufreq_scale(cpu_khz_ref, ref_freq, freq->new);
		if (use_tsc) {
			if (!(freq->flags & CPUFREQ_CONST_LOOPS)) {
				fast_gettimeoffset_quotient = cpufreq_scale(fast_gettimeoffset_ref, freq->new, ref_freq);
				set_cyc2ns_scale(cpu_khz);
			}
		}
#endif
	}

	if (val != CPUFREQ_RESUMECHANGE)
		write_sequnlock_irq(&xtime_lock);

	return 0;
}

static struct notifier_block time_cpufreq_notifier_block = {
	.notifier_call	= time_cpufreq_notifier
};


static int __init cpufreq_tsc(void)
{
	int ret;
	INIT_WORK(&cpufreq_delayed_get_work, handle_cpufreq_delayed_get, NULL);
	ret = cpufreq_register_notifier(&time_cpufreq_notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);
	if (!ret)
		cpufreq_init = 1;
	return ret;
}
core_initcall(cpufreq_tsc);

#else /* CONFIG_CPU_FREQ */
static inline void cpufreq_delayed_get(void) { return; }
#endif 

int recalibrate_cpu_khz(void)
{
#ifndef CONFIG_SMP
	unsigned int cpu_khz_old = cpu_khz;

	if (cpu_has_tsc) {
		local_irq_disable();
		init_cpu_khz();
		local_irq_enable();
		cpu_data[0].loops_per_jiffy =
		    cpufreq_scale(cpu_data[0].loops_per_jiffy,
			          cpu_khz_old,
				  cpu_khz);
		return 0;
	} else
		return -ENODEV;
#else
	return -ENODEV;
#endif
}
EXPORT_SYMBOL(recalibrate_cpu_khz);

static void mark_offset_tsc(void)
{
	unsigned long lost,delay;
	unsigned long delta = last_tsc_low;
	int count;
	int countmp;
	static int count1 = 0;
	unsigned long long this_offset, last_offset;
	static int lost_count = 0;

	write_seqlock(&monotonic_lock);
	last_offset = ((unsigned long long)last_tsc_high<<32)|last_tsc_low;
	/*
	 * It is important that these two operations happen almost at
	 * the same time. We do the RDTSC stuff first, since it's
	 * faster. To avoid any inconsistencies, we need interrupts
	 * disabled locally.
	 */

	/*
	 * Interrupts are just disabled locally since the timer irq
	 * has the SA_INTERRUPT flag set. -arca
	 */

	/* read Pentium cycle counter */

	rdtsc(last_tsc_low, last_tsc_high);

	spin_lock(&i8253_lock);
	outb_p(0x00, PIT_MODE);     /* latch the count ASAP */

	count = inb_p(PIT_CH0);    /* read the latched count */
	count |= inb(PIT_CH0) << 8;

	/*
	 * VIA686a test code... reset the latch if count > max + 1
	 * from timer_pit.c - cjb
	 */
	if (count > LATCH) {
		outb_p(0x34, PIT_MODE);
		outb_p(LATCH & 0xff, PIT_CH0);
		outb(LATCH >> 8, PIT_CH0);
		count = LATCH - 1;
	}

	spin_unlock(&i8253_lock);

	if (pit_latch_buggy) {
		/* get center value of last 3 time lutch */
		if ((count2 >= count && count >= count1)
		    || (count1 >= count && count >= count2)) {
			count2 = count1; count1 = count;
		} else if ((count1 >= count2 && count2 >= count)
			   || (count >= count2 && count2 >= count1)) {
			countmp = count;count = count2;
			count2 = count1;count1 = countmp;
		} else {
			count2 = count1; count1 = count; count = count1;
		}
	}

	/* lost tick compensation */
	delta = last_tsc_low - delta;
	{
		register unsigned long eax, edx;
		eax = delta;
		__asm__("mull %2"
		:"=a" (eax), "=d" (edx)
		:"rm" (fast_gettimeoffset_quotient),
		 "0" (eax));
		delta = edx;
	}
	delta += delay_at_last_interrupt;
	lost = delta/(1000000/HZ);
	delay = delta%(1000000/HZ);
	if (lost >= 2 && detect_lost_ticks) {
		jiffies_64 += lost-1;

		/* sanity check to ensure we're not always losing ticks */
		if (lost_count++ > 100) {
			printk(KERN_WARNING "Losing too many ticks!\n");
			printk(KERN_WARNING "TSC cannot be used as a timesource.  \n");
			printk(KERN_WARNING "Possible reasons for this are:\n");
			printk(KERN_WARNING "  You're running with Speedstep,\n");
			printk(KERN_WARNING "  You don't have DMA enabled for your hard disk (see hdparm),\n");
			printk(KERN_WARNING "  Incorrect TSC synchronization on an SMP system (see dmesg).\n");
			printk(KERN_WARNING "Falling back to a sane timesource now.\n");

			clock_fallback();
		}
		/* ... but give the TSC a fair chance */
		if (lost_count > 25)
			cpufreq_delayed_get();
	} else
		lost_count = 0;
	/* update the monotonic base value */
	this_offset = ((unsigned long long)last_tsc_high<<32)|last_tsc_low;
	monotonic_base += cycles_2_ns(this_offset - last_offset);
	write_sequnlock(&monotonic_lock);

	/* calculate delay_at_last_interrupt */
	count = ((LATCH-1) - count) * TICK_SIZE;
	delay_at_last_interrupt = (count + LATCH/2) / LATCH;

	/* catch corner case where tick rollover occured
	 * between tsc and pit reads (as noted when
	 * usec delta is > 90% # of usecs/tick)
	 */
	if (lost && abs(delay - delay_at_last_interrupt) > (900000/HZ))
		jiffies_64++;
}

static int __init init_tsc(char* override)
{

	/* check clock override */
	if (override[0] && strncmp(override,"tsc",3)) {
#ifdef CONFIG_HPET_TIMER
		if (is_hpet_enabled()) {
			printk(KERN_ERR "Warning: clock= override failed. Defaulting to tsc\n");
		} else
#endif
		{
			return -ENODEV;
		}
	}

	/*
	 * If we have APM enabled or the CPU clock speed is variable
	 * (CPU stops clock on HLT or slows clock to save power)
	 * then the TSC timestamps may diverge by up to 1 jiffy from
	 * 'real time' but nothing will break.
	 * The most frequent case is that the CPU is "woken" from a halt
	 * state by the timer interrupt itself, so we get 0 error. In the
	 * rare cases where a driver would "wake" the CPU and request a
	 * timestamp, the maximum error is < 1 jiffy. But timestamps are
	 * still perfectly ordered.
	 * Note that the TSC counter will be reset if APM suspends
	 * to disk; this won't break the kernel, though, 'cuz we're
	 * smart.  See arch/i386/kernel/apm.c.
	 */
 	/*
 	 *	Firstly we have to do a CPU check for chips with
 	 * 	a potentially buggy TSC. At this point we haven't run
 	 *	the ident/bugs checks so we must run this hook as it
 	 *	may turn off the TSC flag.
 	 *
 	 *	NOTE: this doesn't yet handle SMP 486 machines where only
 	 *	some CPU's have a TSC. Thats never worked and nobody has
 	 *	moaned if you have the only one in the world - you fix it!
 	 */

	count2 = LATCH; /* initialize counter for mark_offset_tsc() */

	if (cpu_has_tsc) {
		unsigned long tsc_quotient;
#ifdef CONFIG_HPET_TIMER
		if (is_hpet_enabled() && hpet_use_timer) {
			unsigned long result, remain;
			printk("Using TSC for gettimeofday\n");
			tsc_quotient = calibrate_tsc_hpet(NULL);
			timer_tsc.mark_offset = &mark_offset_tsc_hpet;
			/*
			 * Math to calculate hpet to usec multiplier
			 * Look for the comments at get_offset_tsc_hpet()
			 */
			ASM_DIV64_REG(result, remain, hpet_tick,
					0, KERNEL_TICK_USEC);
			if (remain > (hpet_tick >> 1))
				result++; /* rounding the result */

			hpet_usec_quotient = result;
		} else
#endif
		{
			tsc_quotient = calibrate_tsc();
		}

		if (tsc_quotient) {
			fast_gettimeoffset_quotient = tsc_quotient;
			use_tsc = 1;
			/*
			 *	We could be more selective here I suspect
			 *	and just enable this for the next intel chips ?
			 */
			/* report CPU clock rate in Hz.
			 * The formula is (10^6 * 2^32) / (2^32 * 1 / (clocks/us)) =
			 * clock/second. Our precision is about 100 ppm.
			 */
			{	unsigned long eax=0, edx=1000;
				__asm__("divl %2"
		       		:"=a" (cpu_khz), "=d" (edx)
        	       		:"r" (tsc_quotient),
	                	"0" (eax), "1" (edx));
				printk("Detected %u.%03u MHz processor.\n",
					cpu_khz / 1000, cpu_khz % 1000);
			}
			set_cyc2ns_scale(cpu_khz);
			return 0;
		}
	}
	return -ENODEV;
}

static int tsc_resume(void)
{
	write_seqlock(&monotonic_lock);
	/* Assume this is the last mark offset time */
	rdtsc(last_tsc_low, last_tsc_high);
#ifdef CONFIG_HPET_TIMER
	if (is_hpet_enabled() && hpet_use_timer)
		hpet_last = hpet_readl(HPET_COUNTER);
#endif
	write_sequnlock(&monotonic_lock);
	return 0;
}

#ifndef CONFIG_X86_TSC
/* disable flag for tsc.  Takes effect by clearing the TSC cpu flag
 * in cpu/common.c */
static int __init tsc_setup(char *str)
{
	tsc_disable = 1;
	return 1;
}
#else
static int __init tsc_setup(char *str)
{
	printk(KERN_WARNING "notsc: Kernel compiled with CONFIG_X86_TSC, "
				"cannot disable TSC.\n");
	return 1;
}
#endif
__setup("notsc", tsc_setup);



/************************************************************/

/* tsc timer_opts struct */
static struct timer_opts timer_tsc = {
	.name = "tsc",
	.mark_offset = mark_offset_tsc, 
	.get_offset = get_offset_tsc,
	.monotonic_clock = monotonic_clock_tsc,
	.delay = delay_tsc,
	.read_timer = read_timer_tsc,
	.resume	= tsc_resume,
};

struct init_timer_opts __initdata timer_tsc_init = {
	.init = init_tsc,
	.opts = &timer_tsc,
};
