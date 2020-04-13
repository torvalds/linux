// SPDX-License-Identifier: GPL-2.0
/*
 * Xen time implementation.
 *
 * This is implemented in terms of a clocksource driver which uses
 * the hypervisor clock as a nanosecond timebase, and a clockevent
 * driver which uses the hypervisor's timer mechanism.
 *
 * Jeremy Fitzhardinge <jeremy@xensource.com>, XenSource Inc, 2007
 */
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/pvclock_gtod.h>
#include <linux/timekeeper_internal.h>

#include <asm/pvclock.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>

#include <xen/events.h>
#include <xen/features.h>
#include <xen/interface/xen.h>
#include <xen/interface/vcpu.h>

#include "xen-ops.h"

/* Minimum amount of time until next clock event fires */
#define TIMER_SLOP	100000

static u64 xen_sched_clock_offset __read_mostly;

/* Get the TSC speed from Xen */
static unsigned long xen_tsc_khz(void)
{
	struct pvclock_vcpu_time_info *info =
		&HYPERVISOR_shared_info->vcpu_info[0].time;

	return pvclock_tsc_khz(info);
}

static u64 xen_clocksource_read(void)
{
        struct pvclock_vcpu_time_info *src;
	u64 ret;

	preempt_disable_notrace();
	src = &__this_cpu_read(xen_vcpu)->time;
	ret = pvclock_clocksource_read(src);
	preempt_enable_notrace();
	return ret;
}

static u64 xen_clocksource_get_cycles(struct clocksource *cs)
{
	return xen_clocksource_read();
}

static u64 xen_sched_clock(void)
{
	return xen_clocksource_read() - xen_sched_clock_offset;
}

static void xen_read_wallclock(struct timespec64 *ts)
{
	struct shared_info *s = HYPERVISOR_shared_info;
	struct pvclock_wall_clock *wall_clock = &(s->wc);
        struct pvclock_vcpu_time_info *vcpu_time;

	vcpu_time = &get_cpu_var(xen_vcpu)->time;
	pvclock_read_wallclock(wall_clock, vcpu_time, ts);
	put_cpu_var(xen_vcpu);
}

static void xen_get_wallclock(struct timespec64 *now)
{
	xen_read_wallclock(now);
}

static int xen_set_wallclock(const struct timespec64 *now)
{
	return -ENODEV;
}

static int xen_pvclock_gtod_notify(struct notifier_block *nb,
				   unsigned long was_set, void *priv)
{
	/* Protected by the calling core code serialization */
	static struct timespec64 next_sync;

	struct xen_platform_op op;
	struct timespec64 now;
	struct timekeeper *tk = priv;
	static bool settime64_supported = true;
	int ret;

	now.tv_sec = tk->xtime_sec;
	now.tv_nsec = (long)(tk->tkr_mono.xtime_nsec >> tk->tkr_mono.shift);

	/*
	 * We only take the expensive HV call when the clock was set
	 * or when the 11 minutes RTC synchronization time elapsed.
	 */
	if (!was_set && timespec64_compare(&now, &next_sync) < 0)
		return NOTIFY_OK;

again:
	if (settime64_supported) {
		op.cmd = XENPF_settime64;
		op.u.settime64.mbz = 0;
		op.u.settime64.secs = now.tv_sec;
		op.u.settime64.nsecs = now.tv_nsec;
		op.u.settime64.system_time = xen_clocksource_read();
	} else {
		op.cmd = XENPF_settime32;
		op.u.settime32.secs = now.tv_sec;
		op.u.settime32.nsecs = now.tv_nsec;
		op.u.settime32.system_time = xen_clocksource_read();
	}

	ret = HYPERVISOR_platform_op(&op);

	if (ret == -ENOSYS && settime64_supported) {
		settime64_supported = false;
		goto again;
	}
	if (ret < 0)
		return NOTIFY_BAD;

	/*
	 * Move the next drift compensation time 11 minutes
	 * ahead. That's emulating the sync_cmos_clock() update for
	 * the hardware RTC.
	 */
	next_sync = now;
	next_sync.tv_sec += 11 * 60;

	return NOTIFY_OK;
}

static struct notifier_block xen_pvclock_gtod_notifier = {
	.notifier_call = xen_pvclock_gtod_notify,
};

static int xen_cs_enable(struct clocksource *cs)
{
	vclocks_set_used(VDSO_CLOCKMODE_PVCLOCK);
	return 0;
}

static struct clocksource xen_clocksource __read_mostly = {
	.name	= "xen",
	.rating	= 400,
	.read	= xen_clocksource_get_cycles,
	.mask	= CLOCKSOURCE_MASK(64),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
	.enable = xen_cs_enable,
};

/*
   Xen clockevent implementation

   Xen has two clockevent implementations:

   The old timer_op one works with all released versions of Xen prior
   to version 3.0.4.  This version of the hypervisor provides a
   single-shot timer with nanosecond resolution.  However, sharing the
   same event channel is a 100Hz tick which is delivered while the
   vcpu is running.  We don't care about or use this tick, but it will
   cause the core time code to think the timer fired too soon, and
   will end up resetting it each time.  It could be filtered, but
   doing so has complications when the ktime clocksource is not yet
   the xen clocksource (ie, at boot time).

   The new vcpu_op-based timer interface allows the tick timer period
   to be changed or turned off.  The tick timer is not useful as a
   periodic timer because events are only delivered to running vcpus.
   The one-shot timer can report when a timeout is in the past, so
   set_next_event is capable of returning -ETIME when appropriate.
   This interface is used when available.
*/


/*
  Get a hypervisor absolute time.  In theory we could maintain an
  offset between the kernel's time and the hypervisor's time, and
  apply that to a kernel's absolute timeout.  Unfortunately the
  hypervisor and kernel times can drift even if the kernel is using
  the Xen clocksource, because ntp can warp the kernel's clocksource.
*/
static s64 get_abs_timeout(unsigned long delta)
{
	return xen_clocksource_read() + delta;
}

static int xen_timerop_shutdown(struct clock_event_device *evt)
{
	/* cancel timeout */
	HYPERVISOR_set_timer_op(0);

	return 0;
}

static int xen_timerop_set_next_event(unsigned long delta,
				      struct clock_event_device *evt)
{
	WARN_ON(!clockevent_state_oneshot(evt));

	if (HYPERVISOR_set_timer_op(get_abs_timeout(delta)) < 0)
		BUG();

	/* We may have missed the deadline, but there's no real way of
	   knowing for sure.  If the event was in the past, then we'll
	   get an immediate interrupt. */

	return 0;
}

static struct clock_event_device xen_timerop_clockevent __ro_after_init = {
	.name			= "xen",
	.features		= CLOCK_EVT_FEAT_ONESHOT,

	.max_delta_ns		= 0xffffffff,
	.max_delta_ticks	= 0xffffffff,
	.min_delta_ns		= TIMER_SLOP,
	.min_delta_ticks	= TIMER_SLOP,

	.mult			= 1,
	.shift			= 0,
	.rating			= 500,

	.set_state_shutdown	= xen_timerop_shutdown,
	.set_next_event		= xen_timerop_set_next_event,
};

static int xen_vcpuop_shutdown(struct clock_event_device *evt)
{
	int cpu = smp_processor_id();

	if (HYPERVISOR_vcpu_op(VCPUOP_stop_singleshot_timer, xen_vcpu_nr(cpu),
			       NULL) ||
	    HYPERVISOR_vcpu_op(VCPUOP_stop_periodic_timer, xen_vcpu_nr(cpu),
			       NULL))
		BUG();

	return 0;
}

static int xen_vcpuop_set_oneshot(struct clock_event_device *evt)
{
	int cpu = smp_processor_id();

	if (HYPERVISOR_vcpu_op(VCPUOP_stop_periodic_timer, xen_vcpu_nr(cpu),
			       NULL))
		BUG();

	return 0;
}

static int xen_vcpuop_set_next_event(unsigned long delta,
				     struct clock_event_device *evt)
{
	int cpu = smp_processor_id();
	struct vcpu_set_singleshot_timer single;
	int ret;

	WARN_ON(!clockevent_state_oneshot(evt));

	single.timeout_abs_ns = get_abs_timeout(delta);
	/* Get an event anyway, even if the timeout is already expired */
	single.flags = 0;

	ret = HYPERVISOR_vcpu_op(VCPUOP_set_singleshot_timer, xen_vcpu_nr(cpu),
				 &single);
	BUG_ON(ret != 0);

	return ret;
}

static struct clock_event_device xen_vcpuop_clockevent __ro_after_init = {
	.name = "xen",
	.features = CLOCK_EVT_FEAT_ONESHOT,

	.max_delta_ns = 0xffffffff,
	.max_delta_ticks = 0xffffffff,
	.min_delta_ns = TIMER_SLOP,
	.min_delta_ticks = TIMER_SLOP,

	.mult = 1,
	.shift = 0,
	.rating = 500,

	.set_state_shutdown = xen_vcpuop_shutdown,
	.set_state_oneshot = xen_vcpuop_set_oneshot,
	.set_next_event = xen_vcpuop_set_next_event,
};

static const struct clock_event_device *xen_clockevent =
	&xen_timerop_clockevent;

struct xen_clock_event_device {
	struct clock_event_device evt;
	char name[16];
};
static DEFINE_PER_CPU(struct xen_clock_event_device, xen_clock_events) = { .evt.irq = -1 };

static irqreturn_t xen_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = this_cpu_ptr(&xen_clock_events.evt);
	irqreturn_t ret;

	ret = IRQ_NONE;
	if (evt->event_handler) {
		evt->event_handler(evt);
		ret = IRQ_HANDLED;
	}

	return ret;
}

void xen_teardown_timer(int cpu)
{
	struct clock_event_device *evt;
	evt = &per_cpu(xen_clock_events, cpu).evt;

	if (evt->irq >= 0) {
		unbind_from_irqhandler(evt->irq, NULL);
		evt->irq = -1;
	}
}

void xen_setup_timer(int cpu)
{
	struct xen_clock_event_device *xevt = &per_cpu(xen_clock_events, cpu);
	struct clock_event_device *evt = &xevt->evt;
	int irq;

	WARN(evt->irq >= 0, "IRQ%d for CPU%d is already allocated\n", evt->irq, cpu);
	if (evt->irq >= 0)
		xen_teardown_timer(cpu);

	printk(KERN_INFO "installing Xen timer for CPU %d\n", cpu);

	snprintf(xevt->name, sizeof(xevt->name), "timer%d", cpu);

	irq = bind_virq_to_irqhandler(VIRQ_TIMER, cpu, xen_timer_interrupt,
				      IRQF_PERCPU|IRQF_NOBALANCING|IRQF_TIMER|
				      IRQF_FORCE_RESUME|IRQF_EARLY_RESUME,
				      xevt->name, NULL);
	(void)xen_set_irq_priority(irq, XEN_IRQ_PRIORITY_MAX);

	memcpy(evt, xen_clockevent, sizeof(*evt));

	evt->cpumask = cpumask_of(cpu);
	evt->irq = irq;
}


void xen_setup_cpu_clockevents(void)
{
	clockevents_register_device(this_cpu_ptr(&xen_clock_events.evt));
}

void xen_timer_resume(void)
{
	int cpu;

	if (xen_clockevent != &xen_vcpuop_clockevent)
		return;

	for_each_online_cpu(cpu) {
		if (HYPERVISOR_vcpu_op(VCPUOP_stop_periodic_timer,
				       xen_vcpu_nr(cpu), NULL))
			BUG();
	}
}

static const struct pv_time_ops xen_time_ops __initconst = {
	.sched_clock = xen_sched_clock,
	.steal_clock = xen_steal_clock,
};

static struct pvclock_vsyscall_time_info *xen_clock __read_mostly;
static u64 xen_clock_value_saved;

void xen_save_time_memory_area(void)
{
	struct vcpu_register_time_memory_area t;
	int ret;

	xen_clock_value_saved = xen_clocksource_read() - xen_sched_clock_offset;

	if (!xen_clock)
		return;

	t.addr.v = NULL;

	ret = HYPERVISOR_vcpu_op(VCPUOP_register_vcpu_time_memory_area, 0, &t);
	if (ret != 0)
		pr_notice("Cannot save secondary vcpu_time_info (err %d)",
			  ret);
	else
		clear_page(xen_clock);
}

void xen_restore_time_memory_area(void)
{
	struct vcpu_register_time_memory_area t;
	int ret;

	if (!xen_clock)
		goto out;

	t.addr.v = &xen_clock->pvti;

	ret = HYPERVISOR_vcpu_op(VCPUOP_register_vcpu_time_memory_area, 0, &t);

	/*
	 * We don't disable VDSO_CLOCKMODE_PVCLOCK entirely if it fails to
	 * register the secondary time info with Xen or if we migrated to a
	 * host without the necessary flags. On both of these cases what
	 * happens is either process seeing a zeroed out pvti or seeing no
	 * PVCLOCK_TSC_STABLE_BIT bit set. Userspace checks the latter and
	 * if 0, it discards the data in pvti and fallbacks to a system
	 * call for a reliable timestamp.
	 */
	if (ret != 0)
		pr_notice("Cannot restore secondary vcpu_time_info (err %d)",
			  ret);

out:
	/* Need pvclock_resume() before using xen_clocksource_read(). */
	pvclock_resume();
	xen_sched_clock_offset = xen_clocksource_read() - xen_clock_value_saved;
}

static void xen_setup_vsyscall_time_info(void)
{
	struct vcpu_register_time_memory_area t;
	struct pvclock_vsyscall_time_info *ti;
	int ret;

	ti = (struct pvclock_vsyscall_time_info *)get_zeroed_page(GFP_KERNEL);
	if (!ti)
		return;

	t.addr.v = &ti->pvti;

	ret = HYPERVISOR_vcpu_op(VCPUOP_register_vcpu_time_memory_area, 0, &t);
	if (ret) {
		pr_notice("xen: VDSO_CLOCKMODE_PVCLOCK not supported (err %d)\n", ret);
		free_page((unsigned long)ti);
		return;
	}

	/*
	 * If primary time info had this bit set, secondary should too since
	 * it's the same data on both just different memory regions. But we
	 * still check it in case hypervisor is buggy.
	 */
	if (!(ti->pvti.flags & PVCLOCK_TSC_STABLE_BIT)) {
		t.addr.v = NULL;
		ret = HYPERVISOR_vcpu_op(VCPUOP_register_vcpu_time_memory_area,
					 0, &t);
		if (!ret)
			free_page((unsigned long)ti);

		pr_notice("xen: VDSO_CLOCKMODE_PVCLOCK not supported (tsc unstable)\n");
		return;
	}

	xen_clock = ti;
	pvclock_set_pvti_cpu0_va(xen_clock);

	xen_clocksource.vdso_clock_mode = VDSO_CLOCKMODE_PVCLOCK;
}

static void __init xen_time_init(void)
{
	struct pvclock_vcpu_time_info *pvti;
	int cpu = smp_processor_id();
	struct timespec64 tp;

	/* As Dom0 is never moved, no penalty on using TSC there */
	if (xen_initial_domain())
		xen_clocksource.rating = 275;

	clocksource_register_hz(&xen_clocksource, NSEC_PER_SEC);

	if (HYPERVISOR_vcpu_op(VCPUOP_stop_periodic_timer, xen_vcpu_nr(cpu),
			       NULL) == 0) {
		/* Successfully turned off 100Hz tick, so we have the
		   vcpuop-based timer interface */
		printk(KERN_DEBUG "Xen: using vcpuop timer interface\n");
		xen_clockevent = &xen_vcpuop_clockevent;
	}

	/* Set initial system time with full resolution */
	xen_read_wallclock(&tp);
	do_settimeofday64(&tp);

	setup_force_cpu_cap(X86_FEATURE_TSC);

	/*
	 * We check ahead on the primary time info if this
	 * bit is supported hence speeding up Xen clocksource.
	 */
	pvti = &__this_cpu_read(xen_vcpu)->time;
	if (pvti->flags & PVCLOCK_TSC_STABLE_BIT) {
		pvclock_set_flags(PVCLOCK_TSC_STABLE_BIT);
		xen_setup_vsyscall_time_info();
	}

	xen_setup_runstate_info(cpu);
	xen_setup_timer(cpu);
	xen_setup_cpu_clockevents();

	xen_time_setup_guest();

	if (xen_initial_domain())
		pvclock_gtod_register_notifier(&xen_pvclock_gtod_notifier);
}

void __init xen_init_time_ops(void)
{
	xen_sched_clock_offset = xen_clocksource_read();
	pv_ops.time = xen_time_ops;

	x86_init.timers.timer_init = xen_time_init;
	x86_init.timers.setup_percpu_clockev = x86_init_noop;
	x86_cpuinit.setup_percpu_clockev = x86_init_noop;

	x86_platform.calibrate_tsc = xen_tsc_khz;
	x86_platform.get_wallclock = xen_get_wallclock;
	/* Dom0 uses the native method to set the hardware RTC. */
	if (!xen_initial_domain())
		x86_platform.set_wallclock = xen_set_wallclock;
}

#ifdef CONFIG_XEN_PVHVM
static void xen_hvm_setup_cpu_clockevents(void)
{
	int cpu = smp_processor_id();
	xen_setup_runstate_info(cpu);
	/*
	 * xen_setup_timer(cpu) - snprintf is bad in atomic context. Hence
	 * doing it xen_hvm_cpu_notify (which gets called by smp_init during
	 * early bootup and also during CPU hotplug events).
	 */
	xen_setup_cpu_clockevents();
}

void __init xen_hvm_init_time_ops(void)
{
	/*
	 * vector callback is needed otherwise we cannot receive interrupts
	 * on cpu > 0 and at this point we don't know how many cpus are
	 * available.
	 */
	if (!xen_have_vector_callback)
		return;

	if (!xen_feature(XENFEAT_hvm_safe_pvclock)) {
		pr_info("Xen doesn't support pvclock on HVM, disable pv timer");
		return;
	}

	xen_sched_clock_offset = xen_clocksource_read();
	pv_ops.time = xen_time_ops;
	x86_init.timers.setup_percpu_clockev = xen_time_init;
	x86_cpuinit.setup_percpu_clockev = xen_hvm_setup_cpu_clockevents;

	x86_platform.calibrate_tsc = xen_tsc_khz;
	x86_platform.get_wallclock = xen_get_wallclock;
	x86_platform.set_wallclock = xen_set_wallclock;
}
#endif

/* Kernel parameter to specify Xen timer slop */
static int __init parse_xen_timer_slop(char *ptr)
{
	unsigned long slop = memparse(ptr, NULL);

	xen_timerop_clockevent.min_delta_ns = slop;
	xen_timerop_clockevent.min_delta_ticks = slop;
	xen_vcpuop_clockevent.min_delta_ns = slop;
	xen_vcpuop_clockevent.min_delta_ticks = slop;

	return 0;
}
early_param("xen_timer_slop", parse_xen_timer_slop);
