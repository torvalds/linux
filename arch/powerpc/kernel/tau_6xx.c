// SPDX-License-Identifier: GPL-2.0
/*
 * temp.c	Thermal management for cpu's with Thermal Assist Units
 *
 * Written by Troy Benjegerdes <hozer@drgw.net>
 *
 * TODO:
 * dynamic power management to limit peak CPU temp (using ICTC)
 * calibration???
 *
 * Silly, crazy ideas: use cpu load (from scheduler) and ICTC to extend battery
 * life in portables, and add a 'performance/watt' metric somewhere in /proc
 */

#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/reg.h>
#include <asm/nvram.h>
#include <asm/cache.h>
#include <asm/8xx_immap.h>
#include <asm/machdep.h>

static struct tau_temp
{
	int interrupts;
	unsigned char low;
	unsigned char high;
	unsigned char grew;
} tau[NR_CPUS];

struct timer_list tau_timer;

#undef DEBUG

/* TODO: put these in a /proc interface, with some sanity checks, and maybe
 * dynamic adjustment to minimize # of interrupts */
/* configurable values for step size and how much to expand the window when
 * we get an interrupt. These are based on the limit that was out of range */
#define step_size		2	/* step size when temp goes out of range */
#define window_expand		1	/* expand the window by this much */
/* configurable values for shrinking the window */
#define shrink_timer	2*HZ	/* period between shrinking the window */
#define min_window	2	/* minimum window size, degrees C */

void set_thresholds(unsigned long cpu)
{
#ifdef CONFIG_TAU_INT
	/*
	 * setup THRM1,
	 * threshold, valid bit, enable interrupts, interrupt when below threshold
	 */
	mtspr(SPRN_THRM1, THRM1_THRES(tau[cpu].low) | THRM1_V | THRM1_TIE | THRM1_TID);

	/* setup THRM2,
	 * threshold, valid bit, enable interrupts, interrupt when above threshold
	 */
	mtspr (SPRN_THRM2, THRM1_THRES(tau[cpu].high) | THRM1_V | THRM1_TIE);
#else
	/* same thing but don't enable interrupts */
	mtspr(SPRN_THRM1, THRM1_THRES(tau[cpu].low) | THRM1_V | THRM1_TID);
	mtspr(SPRN_THRM2, THRM1_THRES(tau[cpu].high) | THRM1_V);
#endif
}

void TAUupdate(int cpu)
{
	unsigned thrm;

#ifdef DEBUG
	printk("TAUupdate ");
#endif

	/* if both thresholds are crossed, the step_sizes cancel out
	 * and the window winds up getting expanded twice. */
	if((thrm = mfspr(SPRN_THRM1)) & THRM1_TIV){ /* is valid? */
		if(thrm & THRM1_TIN){ /* crossed low threshold */
			if (tau[cpu].low >= step_size){
				tau[cpu].low -= step_size;
				tau[cpu].high -= (step_size - window_expand);
			}
			tau[cpu].grew = 1;
#ifdef DEBUG
			printk("low threshold crossed ");
#endif
		}
	}
	if((thrm = mfspr(SPRN_THRM2)) & THRM1_TIV){ /* is valid? */
		if(thrm & THRM1_TIN){ /* crossed high threshold */
			if (tau[cpu].high <= 127-step_size){
				tau[cpu].low += (step_size - window_expand);
				tau[cpu].high += step_size;
			}
			tau[cpu].grew = 1;
#ifdef DEBUG
			printk("high threshold crossed ");
#endif
		}
	}

#ifdef DEBUG
	printk("grew = %d\n", tau[cpu].grew);
#endif

#ifndef CONFIG_TAU_INT /* tau_timeout will do this if not using interrupts */
	set_thresholds(cpu);
#endif

}

#ifdef CONFIG_TAU_INT
/*
 * TAU interrupts - called when we have a thermal assist unit interrupt
 * with interrupts disabled
 */

void TAUException(struct pt_regs * regs)
{
	int cpu = smp_processor_id();

	irq_enter();
	tau[cpu].interrupts++;

	TAUupdate(cpu);

	irq_exit();
}
#endif /* CONFIG_TAU_INT */

static void tau_timeout(void * info)
{
	int cpu;
	unsigned long flags;
	int size;
	int shrink;

	/* disabling interrupts *should* be okay */
	local_irq_save(flags);
	cpu = smp_processor_id();

#ifndef CONFIG_TAU_INT
	TAUupdate(cpu);
#endif

	size = tau[cpu].high - tau[cpu].low;
	if (size > min_window && ! tau[cpu].grew) {
		/* do an exponential shrink of half the amount currently over size */
		shrink = (2 + size - min_window) / 4;
		if (shrink) {
			tau[cpu].low += shrink;
			tau[cpu].high -= shrink;
		} else { /* size must have been min_window + 1 */
			tau[cpu].low += 1;
#if 1 /* debug */
			if ((tau[cpu].high - tau[cpu].low) != min_window){
				printk(KERN_ERR "temp.c: line %d, logic error\n", __LINE__);
			}
#endif
		}
	}

	tau[cpu].grew = 0;

	set_thresholds(cpu);

	/*
	 * Do the enable every time, since otherwise a bunch of (relatively)
	 * complex sleep code needs to be added. One mtspr every time
	 * tau_timeout is called is probably not a big deal.
	 *
	 * Enable thermal sensor and set up sample interval timer
	 * need 20 us to do the compare.. until a nice 'cpu_speed' function
	 * call is implemented, just assume a 500 mhz clock. It doesn't really
	 * matter if we take too long for a compare since it's all interrupt
	 * driven anyway.
	 *
	 * use a extra long time.. (60 us @ 500 mhz)
	 */
	mtspr(SPRN_THRM3, THRM3_SITV(500*60) | THRM3_E);

	local_irq_restore(flags);
}

static void tau_timeout_smp(struct timer_list *unused)
{

	/* schedule ourselves to be run again */
	mod_timer(&tau_timer, jiffies + shrink_timer) ;
	on_each_cpu(tau_timeout, NULL, 0);
}

/*
 * setup the TAU
 *
 * Set things up to use THRM1 as a temperature lower bound, and THRM2 as an upper bound.
 * Start off at zero
 */

int tau_initialized = 0;

void __init TAU_init_smp(void * info)
{
	unsigned long cpu = smp_processor_id();

	/* set these to a reasonable value and let the timer shrink the
	 * window */
	tau[cpu].low = 5;
	tau[cpu].high = 120;

	set_thresholds(cpu);
}

int __init TAU_init(void)
{
	/* We assume in SMP that if one CPU has TAU support, they
	 * all have it --BenH
	 */
	if (!cpu_has_feature(CPU_FTR_TAU)) {
		printk("Thermal assist unit not available\n");
		tau_initialized = 0;
		return 1;
	}


	/* first, set up the window shrinking timer */
	timer_setup(&tau_timer, tau_timeout_smp, 0);
	tau_timer.expires = jiffies + shrink_timer;
	add_timer(&tau_timer);

	on_each_cpu(TAU_init_smp, NULL, 0);

	printk("Thermal assist unit ");
#ifdef CONFIG_TAU_INT
	printk("using interrupts, ");
#else
	printk("using timers, ");
#endif
	printk("shrink_timer: %d jiffies\n", shrink_timer);
	tau_initialized = 1;

	return 0;
}

__initcall(TAU_init);

/*
 * return current temp
 */

u32 cpu_temp_both(unsigned long cpu)
{
	return ((tau[cpu].high << 16) | tau[cpu].low);
}

int cpu_temp(unsigned long cpu)
{
	return ((tau[cpu].high + tau[cpu].low) / 2);
}

int tau_interrupts(unsigned long cpu)
{
	return (tau[cpu].interrupts);
}
