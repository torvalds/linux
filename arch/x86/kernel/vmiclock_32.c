/*
 * VMI paravirtual timer support routines.
 *
 * Copyright (C) 2007, VMware, Inc.
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
 */

#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/cpumask.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>

#include <asm/vmi.h>
#include <asm/vmi_time.h>
#include <asm/apicdef.h>
#include <asm/apic.h>
#include <asm/timer.h>
#include <asm/i8253.h>
#include <asm/irq_vectors.h>

#define VMI_ONESHOT  (VMI_ALARM_IS_ONESHOT  | VMI_CYCLES_REAL | vmi_get_alarm_wiring())
#define VMI_PERIODIC (VMI_ALARM_IS_PERIODIC | VMI_CYCLES_REAL | vmi_get_alarm_wiring())

static DEFINE_PER_CPU(struct clock_event_device, local_events);

static inline u32 vmi_counter(u32 flags)
{
	/* Given VMI_ONESHOT or VMI_PERIODIC, return the corresponding
	 * cycle counter. */
	return flags & VMI_ALARM_COUNTER_MASK;
}

/* paravirt_ops.get_wallclock = vmi_get_wallclock */
unsigned long vmi_get_wallclock(void)
{
	unsigned long long wallclock;
	wallclock = vmi_timer_ops.get_wallclock(); // nsec
	(void)do_div(wallclock, 1000000000);       // sec

	return wallclock;
}

/* paravirt_ops.set_wallclock = vmi_set_wallclock */
int vmi_set_wallclock(unsigned long now)
{
	return 0;
}

/* paravirt_ops.sched_clock = vmi_sched_clock */
unsigned long long vmi_sched_clock(void)
{
	return cycles_2_ns(vmi_timer_ops.get_cycle_counter(VMI_CYCLES_AVAILABLE));
}

/* paravirt_ops.get_tsc_khz = vmi_tsc_khz */
unsigned long vmi_tsc_khz(void)
{
	unsigned long long khz;
	khz = vmi_timer_ops.get_cycle_frequency();
	(void)do_div(khz, 1000);
	return khz;
}

static inline unsigned int vmi_get_timer_vector(void)
{
#ifdef CONFIG_X86_IO_APIC
	return FIRST_DEVICE_VECTOR;
#else
	return FIRST_EXTERNAL_VECTOR;
#endif
}

/** vmi clockchip */
#ifdef CONFIG_X86_LOCAL_APIC
static unsigned int startup_timer_irq(unsigned int irq)
{
	unsigned long val = apic_read(APIC_LVTT);
	apic_write(APIC_LVTT, vmi_get_timer_vector());

	return (val & APIC_SEND_PENDING);
}

static void mask_timer_irq(unsigned int irq)
{
	unsigned long val = apic_read(APIC_LVTT);
	apic_write(APIC_LVTT, val | APIC_LVT_MASKED);
}

static void unmask_timer_irq(unsigned int irq)
{
	unsigned long val = apic_read(APIC_LVTT);
	apic_write(APIC_LVTT, val & ~APIC_LVT_MASKED);
}

static void ack_timer_irq(unsigned int irq)
{
	ack_APIC_irq();
}

static struct irq_chip vmi_chip __read_mostly = {
	.name 		= "VMI-LOCAL",
	.startup 	= startup_timer_irq,
	.mask	 	= mask_timer_irq,
	.unmask	 	= unmask_timer_irq,
	.ack 		= ack_timer_irq
};
#endif

/** vmi clockevent */
#define VMI_ALARM_WIRED_IRQ0    0x00000000
#define VMI_ALARM_WIRED_LVTT    0x00010000
static int vmi_wiring = VMI_ALARM_WIRED_IRQ0;

static inline int vmi_get_alarm_wiring(void)
{
	return vmi_wiring;
}

static void vmi_timer_set_mode(enum clock_event_mode mode,
			       struct clock_event_device *evt)
{
	cycle_t now, cycles_per_hz;
	BUG_ON(!irqs_disabled());

	switch (mode) {
	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_RESUME:
		break;
	case CLOCK_EVT_MODE_PERIODIC:
		cycles_per_hz = vmi_timer_ops.get_cycle_frequency();
		(void)do_div(cycles_per_hz, HZ);
		now = vmi_timer_ops.get_cycle_counter(vmi_counter(VMI_PERIODIC));
		vmi_timer_ops.set_alarm(VMI_PERIODIC, now, cycles_per_hz);
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		switch (evt->mode) {
		case CLOCK_EVT_MODE_ONESHOT:
			vmi_timer_ops.cancel_alarm(VMI_ONESHOT);
			break;
		case CLOCK_EVT_MODE_PERIODIC:
			vmi_timer_ops.cancel_alarm(VMI_PERIODIC);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static int vmi_timer_next_event(unsigned long delta,
				struct clock_event_device *evt)
{
	/* Unfortunately, set_next_event interface only passes relative
	 * expiry, but we want absolute expiry.  It'd be better if were
	 * were passed an aboslute expiry, since a bunch of time may
	 * have been stolen between the time the delta is computed and
	 * when we set the alarm below. */
	cycle_t now = vmi_timer_ops.get_cycle_counter(vmi_counter(VMI_ONESHOT));

	BUG_ON(evt->mode != CLOCK_EVT_MODE_ONESHOT);
	vmi_timer_ops.set_alarm(VMI_ONESHOT, now + delta, 0);
	return 0;
}

static struct clock_event_device vmi_clockevent = {
	.name		= "vmi-timer",
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.shift		= 22,
	.set_mode	= vmi_timer_set_mode,
	.set_next_event = vmi_timer_next_event,
	.rating         = 1000,
	.irq		= 0,
};

static irqreturn_t vmi_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &__get_cpu_var(local_events);
	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static struct irqaction vmi_clock_action  = {
	.name 		= "vmi-timer",
	.handler 	= vmi_timer_interrupt,
	.flags 		= IRQF_DISABLED | IRQF_NOBALANCING | IRQF_TIMER,
};

static void __devinit vmi_time_init_clockevent(void)
{
	cycle_t cycles_per_msec;
	struct clock_event_device *evt;

	int cpu = smp_processor_id();
	evt = &__get_cpu_var(local_events);

	/* Use cycles_per_msec since div_sc params are 32-bits. */
	cycles_per_msec = vmi_timer_ops.get_cycle_frequency();
	(void)do_div(cycles_per_msec, 1000);

	memcpy(evt, &vmi_clockevent, sizeof(*evt));
	/* Must pick .shift such that .mult fits in 32-bits.  Choosing
	 * .shift to be 22 allows 2^(32-22) cycles per nano-seconds
	 * before overflow. */
	evt->mult = div_sc(cycles_per_msec, NSEC_PER_MSEC, evt->shift);
	/* Upper bound is clockevent's use of ulong for cycle deltas. */
	evt->max_delta_ns = clockevent_delta2ns(ULONG_MAX, evt);
	evt->min_delta_ns = clockevent_delta2ns(1, evt);
	evt->cpumask = cpumask_of(cpu);

	printk(KERN_WARNING "vmi: registering clock event %s. mult=%lu shift=%u\n",
	       evt->name, evt->mult, evt->shift);
	clockevents_register_device(evt);
}

void __init vmi_time_init(void)
{
	unsigned int cpu;
	/* Disable PIT: BIOSes start PIT CH0 with 18.2hz peridic. */
	outb_pit(0x3a, PIT_MODE); /* binary, mode 5, LSB/MSB, ch 0 */

	vmi_time_init_clockevent();
	setup_irq(0, &vmi_clock_action);
	for_each_possible_cpu(cpu)
		per_cpu(vector_irq, cpu)[vmi_get_timer_vector()] = 0;
}

#ifdef CONFIG_X86_LOCAL_APIC
void __devinit vmi_time_bsp_init(void)
{
	/*
	 * On APIC systems, we want local timers to fire on each cpu.  We do
	 * this by programming LVTT to deliver timer events to the IRQ handler
	 * for IRQ-0, since we can't re-use the APIC local timer handler
	 * without interfering with that code.
	 */
	clockevents_notify(CLOCK_EVT_NOTIFY_SUSPEND, NULL);
	local_irq_disable();
#ifdef CONFIG_SMP
	/*
	 * XXX handle_percpu_irq only defined for SMP; we need to switch over
	 * to using it, since this is a local interrupt, which each CPU must
	 * handle individually without locking out or dropping simultaneous
	 * local timers on other CPUs.  We also don't want to trigger the
	 * quirk workaround code for interrupts which gets invoked from
	 * handle_percpu_irq via eoi, so we use our own IRQ chip.
	 */
	set_irq_chip_and_handler_name(0, &vmi_chip, handle_percpu_irq, "lvtt");
#else
	set_irq_chip_and_handler_name(0, &vmi_chip, handle_edge_irq, "lvtt");
#endif
	vmi_wiring = VMI_ALARM_WIRED_LVTT;
	apic_write(APIC_LVTT, vmi_get_timer_vector());
	local_irq_enable();
	clockevents_notify(CLOCK_EVT_NOTIFY_RESUME, NULL);
}

void __devinit vmi_time_ap_init(void)
{
	vmi_time_init_clockevent();
	apic_write(APIC_LVTT, vmi_get_timer_vector());
}
#endif

/** vmi clocksource */
static struct clocksource clocksource_vmi;

static cycle_t read_real_cycles(struct clocksource *cs)
{
	cycle_t ret = (cycle_t)vmi_timer_ops.get_cycle_counter(VMI_CYCLES_REAL);
	return max(ret, clocksource_vmi.cycle_last);
}

static struct clocksource clocksource_vmi = {
	.name			= "vmi-timer",
	.rating			= 450,
	.read			= read_real_cycles,
	.mask			= CLOCKSOURCE_MASK(64),
	.mult			= 0, /* to be set */
	.shift			= 22,
	.flags			= CLOCK_SOURCE_IS_CONTINUOUS,
};

static int __init init_vmi_clocksource(void)
{
	cycle_t cycles_per_msec;

	if (!vmi_timer_ops.get_cycle_frequency)
		return 0;
	/* Use khz2mult rather than hz2mult since hz arg is only 32-bits. */
	cycles_per_msec = vmi_timer_ops.get_cycle_frequency();
	(void)do_div(cycles_per_msec, 1000);

	/* Note that clocksource.{mult, shift} converts in the opposite direction
	 * as clockevents.  */
	clocksource_vmi.mult = clocksource_khz2mult(cycles_per_msec,
						    clocksource_vmi.shift);

	printk(KERN_WARNING "vmi: registering clock source khz=%lld\n", cycles_per_msec);
	return clocksource_register(&clocksource_vmi);

}
module_init(init_vmi_clocksource);
