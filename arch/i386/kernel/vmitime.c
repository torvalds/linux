/*
 * VMI paravirtual timer support routines.
 *
 * Copyright (C) 2005, VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to dhecht@vmware.com
 *
 */

/*
 * Portions of this code from arch/i386/kernel/timers/timer_tsc.c.
 * Portions of the CONFIG_NO_IDLE_HZ code from arch/s390/kernel/time.c.
 * See comments there for proper credits.
 */

#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/rcupdate.h>
#include <linux/clocksource.h>

#include <asm/timer.h>
#include <asm/io.h>
#include <asm/apic.h>
#include <asm/div64.h>
#include <asm/timer.h>
#include <asm/desc.h>

#include <asm/vmi.h>
#include <asm/vmi_time.h>

#include <mach_timer.h>
#include <io_ports.h>

#ifdef CONFIG_X86_LOCAL_APIC
#define VMI_ALARM_WIRING VMI_ALARM_WIRED_LVTT
#else
#define VMI_ALARM_WIRING VMI_ALARM_WIRED_IRQ0
#endif

/* Cached VMI operations */
struct vmi_timer_ops vmi_timer_ops;

#ifdef CONFIG_NO_IDLE_HZ

/* /proc/sys/kernel/hz_timer state. */
int sysctl_hz_timer;

/* Some stats */
static DEFINE_PER_CPU(unsigned long, vmi_idle_no_hz_irqs);
static DEFINE_PER_CPU(unsigned long, vmi_idle_no_hz_jiffies);
static DEFINE_PER_CPU(unsigned long, idle_start_jiffies);

#endif /* CONFIG_NO_IDLE_HZ */

/* Number of alarms per second. By default this is CONFIG_VMI_ALARM_HZ. */
static int alarm_hz = CONFIG_VMI_ALARM_HZ;

/* Cache of the value get_cycle_frequency / HZ. */
static signed long long cycles_per_jiffy;

/* Cache of the value get_cycle_frequency / alarm_hz. */
static signed long long cycles_per_alarm;

/* The number of cycles accounted for by the 'jiffies'/'xtime' count.
 * Protected by xtime_lock. */
static unsigned long long real_cycles_accounted_system;

/* The number of cycles accounted for by update_process_times(), per cpu. */
static DEFINE_PER_CPU(unsigned long long, process_times_cycles_accounted_cpu);

/* The number of stolen cycles accounted, per cpu. */
static DEFINE_PER_CPU(unsigned long long, stolen_cycles_accounted_cpu);

/* Clock source. */
static cycle_t read_real_cycles(void)
{
	return vmi_timer_ops.get_cycle_counter(VMI_CYCLES_REAL);
}

static cycle_t read_available_cycles(void)
{
	return vmi_timer_ops.get_cycle_counter(VMI_CYCLES_AVAILABLE);
}

#if 0
static cycle_t read_stolen_cycles(void)
{
	return vmi_timer_ops.get_cycle_counter(VMI_CYCLES_STOLEN);
}
#endif  /*  0  */

static struct clocksource clocksource_vmi = {
	.name			= "vmi-timer",
	.rating			= 450,
	.read			= read_real_cycles,
	.mask			= CLOCKSOURCE_MASK(64),
	.mult			= 0, /* to be set */
	.shift			= 22,
	.flags			= CLOCK_SOURCE_IS_CONTINUOUS,
};


/* Timer interrupt handler. */
static irqreturn_t vmi_timer_interrupt(int irq, void *dev_id);

static struct irqaction vmi_timer_irq  = {
	.handler = vmi_timer_interrupt,
	.flags = IRQF_DISABLED,
	.mask = CPU_MASK_NONE,
	.name = "VMI-alarm",
};

/* Alarm rate */
static int __init vmi_timer_alarm_rate_setup(char* str)
{
	int alarm_rate;
	if (get_option(&str, &alarm_rate) == 1 && alarm_rate > 0) {
		alarm_hz = alarm_rate;
		printk(KERN_WARNING "VMI timer alarm HZ set to %d\n", alarm_hz);
	}
	return 1;
}
__setup("vmi_timer_alarm_hz=", vmi_timer_alarm_rate_setup);


/* Initialization */
static void vmi_get_wallclock_ts(struct timespec *ts)
{
	unsigned long long wallclock;
	wallclock = vmi_timer_ops.get_wallclock(); // nsec units
	ts->tv_nsec = do_div(wallclock, 1000000000);
	ts->tv_sec = wallclock;
}

unsigned long vmi_get_wallclock(void)
{
	struct timespec ts;
	vmi_get_wallclock_ts(&ts);
	return ts.tv_sec;
}

int vmi_set_wallclock(unsigned long now)
{
	return -1;
}

unsigned long long vmi_get_sched_cycles(void)
{
	return read_available_cycles();
}

unsigned long vmi_cpu_khz(void)
{
	unsigned long long khz;

	khz = vmi_timer_ops.get_cycle_frequency();
	(void)do_div(khz, 1000);
	return khz;
}

void __init vmi_time_init(void)
{
	unsigned long long cycles_per_sec, cycles_per_msec;
	unsigned long flags;

	local_irq_save(flags);
	setup_irq(0, &vmi_timer_irq);
#ifdef CONFIG_X86_LOCAL_APIC
	set_intr_gate(LOCAL_TIMER_VECTOR, apic_vmi_timer_interrupt);
#endif

	real_cycles_accounted_system = read_real_cycles();
	per_cpu(process_times_cycles_accounted_cpu, 0) = read_available_cycles();

	cycles_per_sec = vmi_timer_ops.get_cycle_frequency();
	cycles_per_jiffy = cycles_per_sec;
	(void)do_div(cycles_per_jiffy, HZ);
	cycles_per_alarm = cycles_per_sec;
	(void)do_div(cycles_per_alarm, alarm_hz);
	cycles_per_msec = cycles_per_sec;
	(void)do_div(cycles_per_msec, 1000);

	printk(KERN_WARNING "VMI timer cycles/sec = %llu ; cycles/jiffy = %llu ;"
	       "cycles/alarm = %llu\n", cycles_per_sec, cycles_per_jiffy,
	       cycles_per_alarm);

	clocksource_vmi.mult = clocksource_khz2mult(cycles_per_msec,
						    clocksource_vmi.shift);
	if (clocksource_register(&clocksource_vmi))
		printk(KERN_WARNING "Error registering VMITIME clocksource.");

	/* Disable PIT. */
	outb_p(0x3a, PIT_MODE); /* binary, mode 5, LSB/MSB, ch 0 */

	/* schedule the alarm. do this in phase with process_times_cycles_accounted_cpu
	 * reduce the latency calling update_process_times. */
	vmi_timer_ops.set_alarm(
		      VMI_ALARM_WIRED_IRQ0 | VMI_ALARM_IS_PERIODIC | VMI_CYCLES_AVAILABLE,
		      per_cpu(process_times_cycles_accounted_cpu, 0) + cycles_per_alarm,
		      cycles_per_alarm);

	local_irq_restore(flags);
}

#ifdef CONFIG_X86_LOCAL_APIC

void __init vmi_timer_setup_boot_alarm(void)
{
	local_irq_disable();

	/* Route the interrupt to the correct vector. */
	apic_write_around(APIC_LVTT, LOCAL_TIMER_VECTOR);

	/* Cancel the IRQ0 wired alarm, and setup the LVTT alarm. */
	vmi_timer_ops.cancel_alarm(VMI_CYCLES_AVAILABLE);
	vmi_timer_ops.set_alarm(
		      VMI_ALARM_WIRED_LVTT | VMI_ALARM_IS_PERIODIC | VMI_CYCLES_AVAILABLE,
		      per_cpu(process_times_cycles_accounted_cpu, 0) + cycles_per_alarm,
		      cycles_per_alarm);
	local_irq_enable();
}

/* Initialize the time accounting variables for an AP on an SMP system.
 * Also, set the local alarm for the AP. */
void __devinit vmi_timer_setup_secondary_alarm(void)
{
	int cpu = smp_processor_id();

	/* Route the interrupt to the correct vector. */
	apic_write_around(APIC_LVTT, LOCAL_TIMER_VECTOR);

	per_cpu(process_times_cycles_accounted_cpu, cpu) = read_available_cycles();

	vmi_timer_ops.set_alarm(
		      VMI_ALARM_WIRED_LVTT | VMI_ALARM_IS_PERIODIC | VMI_CYCLES_AVAILABLE,
		      per_cpu(process_times_cycles_accounted_cpu, cpu) + cycles_per_alarm,
		      cycles_per_alarm);
}

#endif

/* Update system wide (real) time accounting (e.g. jiffies, xtime). */
static void vmi_account_real_cycles(unsigned long long cur_real_cycles)
{
	long long cycles_not_accounted;

	write_seqlock(&xtime_lock);

	cycles_not_accounted = cur_real_cycles - real_cycles_accounted_system;
	while (cycles_not_accounted >= cycles_per_jiffy) {
		/* systems wide jiffies. */
		do_timer(1);

		cycles_not_accounted -= cycles_per_jiffy;
		real_cycles_accounted_system += cycles_per_jiffy;
	}

	write_sequnlock(&xtime_lock);
}

/* Update per-cpu process times. */
static void vmi_account_process_times_cycles(struct pt_regs *regs, int cpu,
					     unsigned long long cur_process_times_cycles)
{
	long long cycles_not_accounted;
	cycles_not_accounted = cur_process_times_cycles -
		per_cpu(process_times_cycles_accounted_cpu, cpu);

	while (cycles_not_accounted >= cycles_per_jiffy) {
		/* Account time to the current process.  This includes
		 * calling into the scheduler to decrement the timeslice
		 * and possibly reschedule.*/
		update_process_times(user_mode(regs));
		/* XXX handle /proc/profile multiplier.  */
		profile_tick(CPU_PROFILING);

		cycles_not_accounted -= cycles_per_jiffy;
		per_cpu(process_times_cycles_accounted_cpu, cpu) += cycles_per_jiffy;
	}
}

#ifdef CONFIG_NO_IDLE_HZ
/* Update per-cpu idle times.  Used when a no-hz halt is ended. */
static void vmi_account_no_hz_idle_cycles(int cpu,
					  unsigned long long cur_process_times_cycles)
{
	long long cycles_not_accounted;
	unsigned long no_idle_hz_jiffies = 0;

	cycles_not_accounted = cur_process_times_cycles -
		per_cpu(process_times_cycles_accounted_cpu, cpu);

	while (cycles_not_accounted >= cycles_per_jiffy) {
		no_idle_hz_jiffies++;
		cycles_not_accounted -= cycles_per_jiffy;
		per_cpu(process_times_cycles_accounted_cpu, cpu) += cycles_per_jiffy;
	}
	/* Account time to the idle process. */
	account_steal_time(idle_task(cpu), jiffies_to_cputime(no_idle_hz_jiffies));
}
#endif

/* Update per-cpu stolen time. */
static void vmi_account_stolen_cycles(int cpu,
				      unsigned long long cur_real_cycles,
				      unsigned long long cur_avail_cycles)
{
	long long stolen_cycles_not_accounted;
	unsigned long stolen_jiffies = 0;

	if (cur_real_cycles < cur_avail_cycles)
		return;

	stolen_cycles_not_accounted = cur_real_cycles - cur_avail_cycles -
		per_cpu(stolen_cycles_accounted_cpu, cpu);

	while (stolen_cycles_not_accounted >= cycles_per_jiffy) {
		stolen_jiffies++;
		stolen_cycles_not_accounted -= cycles_per_jiffy;
		per_cpu(stolen_cycles_accounted_cpu, cpu) += cycles_per_jiffy;
	}
	/* HACK: pass NULL to force time onto cpustat->steal. */
	account_steal_time(NULL, jiffies_to_cputime(stolen_jiffies));
}

/* Body of either IRQ0 interrupt handler (UP no local-APIC) or
 * local-APIC LVTT interrupt handler (UP & local-APIC or SMP). */
static void vmi_local_timer_interrupt(int cpu)
{
	unsigned long long cur_real_cycles, cur_process_times_cycles;

	cur_real_cycles = read_real_cycles();
	cur_process_times_cycles = read_available_cycles();
	/* Update system wide (real) time state (xtime, jiffies). */
	vmi_account_real_cycles(cur_real_cycles);
	/* Update per-cpu process times. */
	vmi_account_process_times_cycles(get_irq_regs(), cpu, cur_process_times_cycles);
        /* Update time stolen from this cpu by the hypervisor. */
	vmi_account_stolen_cycles(cpu, cur_real_cycles, cur_process_times_cycles);
}

#ifdef CONFIG_NO_IDLE_HZ

/* Must be called only from idle loop, with interrupts disabled. */
int vmi_stop_hz_timer(void)
{
	/* Note that cpu_set, cpu_clear are (SMP safe) atomic on x86. */

	unsigned long seq, next;
	unsigned long long real_cycles_expiry;
	int cpu = smp_processor_id();

	BUG_ON(!irqs_disabled());
	if (sysctl_hz_timer != 0)
		return 0;

	cpu_set(cpu, nohz_cpu_mask);
	smp_mb();

	if (rcu_needs_cpu(cpu) || local_softirq_pending() ||
	    (next = next_timer_interrupt(),
	     time_before_eq(next, jiffies + HZ/CONFIG_VMI_ALARM_HZ))) {
		cpu_clear(cpu, nohz_cpu_mask);
		return 0;
	}

	/* Convert jiffies to the real cycle counter. */
	do {
		seq = read_seqbegin(&xtime_lock);
		real_cycles_expiry = real_cycles_accounted_system +
			(long)(next - jiffies) * cycles_per_jiffy;
	} while (read_seqretry(&xtime_lock, seq));

	/* This cpu is going idle. Disable the periodic alarm. */
	vmi_timer_ops.cancel_alarm(VMI_CYCLES_AVAILABLE);
	per_cpu(idle_start_jiffies, cpu) = jiffies;
	/* Set the real time alarm to expire at the next event. */
	vmi_timer_ops.set_alarm(
		VMI_ALARM_WIRING | VMI_ALARM_IS_ONESHOT | VMI_CYCLES_REAL,
		real_cycles_expiry, 0);
	return 1;
}

static void vmi_reenable_hz_timer(int cpu)
{
	/* For /proc/vmi/info idle_hz stat. */
	per_cpu(vmi_idle_no_hz_jiffies, cpu) += jiffies - per_cpu(idle_start_jiffies, cpu);
	per_cpu(vmi_idle_no_hz_irqs, cpu)++;

	/* Don't bother explicitly cancelling the one-shot alarm -- at
	 * worse we will receive a spurious timer interrupt. */
	vmi_timer_ops.set_alarm(
		      VMI_ALARM_WIRING | VMI_ALARM_IS_PERIODIC | VMI_CYCLES_AVAILABLE,
		      per_cpu(process_times_cycles_accounted_cpu, cpu) + cycles_per_alarm,
		      cycles_per_alarm);
	/* Indicate this cpu is no longer nohz idle. */
	cpu_clear(cpu, nohz_cpu_mask);
}

/* Called from interrupt handlers when (local) HZ timer is disabled. */
void vmi_account_time_restart_hz_timer(void)
{
	unsigned long long cur_real_cycles, cur_process_times_cycles;
	int cpu = smp_processor_id();

	BUG_ON(!irqs_disabled());
	/* Account the time during which the HZ timer was disabled. */
	cur_real_cycles = read_real_cycles();
	cur_process_times_cycles = read_available_cycles();
	/* Update system wide (real) time state (xtime, jiffies). */
	vmi_account_real_cycles(cur_real_cycles);
	/* Update per-cpu idle times. */
	vmi_account_no_hz_idle_cycles(cpu, cur_process_times_cycles);
        /* Update time stolen from this cpu by the hypervisor. */
	vmi_account_stolen_cycles(cpu, cur_real_cycles, cur_process_times_cycles);
	/* Reenable the hz timer. */
	vmi_reenable_hz_timer(cpu);
}

#endif /* CONFIG_NO_IDLE_HZ */

/* UP (and no local-APIC) VMI-timer alarm interrupt handler.
 * Handler for IRQ0. Not used when SMP or X86_LOCAL_APIC after
 * APIC setup and setup_boot_vmi_alarm() is called.  */
static irqreturn_t vmi_timer_interrupt(int irq, void *dev_id)
{
	vmi_local_timer_interrupt(smp_processor_id());
	return IRQ_HANDLED;
}

#ifdef CONFIG_X86_LOCAL_APIC

/* SMP VMI-timer alarm interrupt handler. Handler for LVTT vector.
 * Also used in UP when CONFIG_X86_LOCAL_APIC.
 * The wrapper code is from arch/i386/kernel/apic.c#smp_apic_timer_interrupt. */
void smp_apic_vmi_timer_interrupt(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	int cpu = smp_processor_id();

	/*
	 * the NMI deadlock-detector uses this.
	 */
        per_cpu(irq_stat,cpu).apic_timer_irqs++;

	/*
	 * NOTE! We'd better ACK the irq immediately,
	 * because timer handling can be slow.
	 */
	ack_APIC_irq();

	/*
	 * update_process_times() expects us to have done irq_enter().
	 * Besides, if we don't timer interrupts ignore the global
	 * interrupt lock, which is the WrongThing (tm) to do.
	 */
	irq_enter();
	vmi_local_timer_interrupt(cpu);
	irq_exit();
	set_irq_regs(old_regs);
}

#endif  /* CONFIG_X86_LOCAL_APIC */
