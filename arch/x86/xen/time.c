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
#include <linux/kernel_stat.h>
#include <linux/math64.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/pvclock_gtod.h>

#include <asm/pvclock.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>

#include <xen/events.h>
#include <xen/features.h>
#include <xen/interface/xen.h>
#include <xen/interface/vcpu.h>

#include "xen-ops.h"

/* Xen may fire a timer up to this many ns early */
#define TIMER_SLOP	100000
#define NS_PER_TICK	(1000000000LL / HZ)

/* runstate info updated by Xen */
static DEFINE_PER_CPU(struct vcpu_runstate_info, xen_runstate);

/* snapshots of runstate info */
static DEFINE_PER_CPU(struct vcpu_runstate_info, xen_runstate_snapshot);

/* unused ns of stolen time */
static DEFINE_PER_CPU(u64, xen_residual_stolen);

/* return an consistent snapshot of 64-bit time/counter value */
static u64 get64(const u64 *p)
{
	u64 ret;

	if (BITS_PER_LONG < 64) {
		u32 *p32 = (u32 *)p;
		u32 h, l;

		/*
		 * Read high then low, and then make sure high is
		 * still the same; this will only loop if low wraps
		 * and carries into high.
		 * XXX some clean way to make this endian-proof?
		 */
		do {
			h = p32[1];
			barrier();
			l = p32[0];
			barrier();
		} while (p32[1] != h);

		ret = (((u64)h) << 32) | l;
	} else
		ret = *p;

	return ret;
}

/*
 * Runstate accounting
 */
static void get_runstate_snapshot(struct vcpu_runstate_info *res)
{
	u64 state_time;
	struct vcpu_runstate_info *state;

	BUG_ON(preemptible());

	state = &__get_cpu_var(xen_runstate);

	/*
	 * The runstate info is always updated by the hypervisor on
	 * the current CPU, so there's no need to use anything
	 * stronger than a compiler barrier when fetching it.
	 */
	do {
		state_time = get64(&state->state_entry_time);
		barrier();
		*res = *state;
		barrier();
	} while (get64(&state->state_entry_time) != state_time);
}

/* return true when a vcpu could run but has no real cpu to run on */
bool xen_vcpu_stolen(int vcpu)
{
	return per_cpu(xen_runstate, vcpu).state == RUNSTATE_runnable;
}

void xen_setup_runstate_info(int cpu)
{
	struct vcpu_register_runstate_memory_area area;

	area.addr.v = &per_cpu(xen_runstate, cpu);

	if (HYPERVISOR_vcpu_op(VCPUOP_register_runstate_memory_area,
			       cpu, &area))
		BUG();
}

static void do_stolen_accounting(void)
{
	struct vcpu_runstate_info state;
	struct vcpu_runstate_info *snap;
	s64 runnable, offline, stolen;
	cputime_t ticks;

	get_runstate_snapshot(&state);

	WARN_ON(state.state != RUNSTATE_running);

	snap = &__get_cpu_var(xen_runstate_snapshot);

	/* work out how much time the VCPU has not been runn*ing*  */
	runnable = state.time[RUNSTATE_runnable] - snap->time[RUNSTATE_runnable];
	offline = state.time[RUNSTATE_offline] - snap->time[RUNSTATE_offline];

	*snap = state;

	/* Add the appropriate number of ticks of stolen time,
	   including any left-overs from last time. */
	stolen = runnable + offline + __this_cpu_read(xen_residual_stolen);

	if (stolen < 0)
		stolen = 0;

	ticks = iter_div_u64_rem(stolen, NS_PER_TICK, &stolen);
	__this_cpu_write(xen_residual_stolen, stolen);
	account_steal_ticks(ticks);
}

/* Get the TSC speed from Xen */
static unsigned long xen_tsc_khz(void)
{
	struct pvclock_vcpu_time_info *info =
		&HYPERVISOR_shared_info->vcpu_info[0].time;

	return pvclock_tsc_khz(info);
}

cycle_t xen_clocksource_read(void)
{
        struct pvclock_vcpu_time_info *src;
	cycle_t ret;

	preempt_disable_notrace();
	src = &__get_cpu_var(xen_vcpu)->time;
	ret = pvclock_clocksource_read(src);
	preempt_enable_notrace();
	return ret;
}

static cycle_t xen_clocksource_get_cycles(struct clocksource *cs)
{
	return xen_clocksource_read();
}

static void xen_read_wallclock(struct timespec *ts)
{
	struct shared_info *s = HYPERVISOR_shared_info;
	struct pvclock_wall_clock *wall_clock = &(s->wc);
        struct pvclock_vcpu_time_info *vcpu_time;

	vcpu_time = &get_cpu_var(xen_vcpu)->time;
	pvclock_read_wallclock(wall_clock, vcpu_time, ts);
	put_cpu_var(xen_vcpu);
}

static void xen_get_wallclock(struct timespec *now)
{
	xen_read_wallclock(now);
}

static int xen_set_wallclock(const struct timespec *now)
{
	return -1;
}

static int xen_pvclock_gtod_notify(struct notifier_block *nb,
				   unsigned long was_set, void *priv)
{
	/* Protected by the calling core code serialization */
	static struct timespec next_sync;

	struct xen_platform_op op;
	struct timespec now;

	now = __current_kernel_time();

	/*
	 * We only take the expensive HV call when the clock was set
	 * or when the 11 minutes RTC synchronization time elapsed.
	 */
	if (!was_set && timespec_compare(&now, &next_sync) < 0)
		return NOTIFY_OK;

	op.cmd = XENPF_settime;
	op.u.settime.secs = now.tv_sec;
	op.u.settime.nsecs = now.tv_nsec;
	op.u.settime.system_time = xen_clocksource_read();

	(void)HYPERVISOR_dom0_op(&op);

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

static struct clocksource xen_clocksource __read_mostly = {
	.name = "xen",
	.rating = 400,
	.read = xen_clocksource_get_cycles,
	.mask = ~0,
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
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

static void xen_timerop_set_mode(enum clock_event_mode mode,
				 struct clock_event_device *evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		/* unsupported */
		WARN_ON(1);
		break;

	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_RESUME:
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		HYPERVISOR_set_timer_op(0);  /* cancel timeout */
		break;
	}
}

static int xen_timerop_set_next_event(unsigned long delta,
				      struct clock_event_device *evt)
{
	WARN_ON(evt->mode != CLOCK_EVT_MODE_ONESHOT);

	if (HYPERVISOR_set_timer_op(get_abs_timeout(delta)) < 0)
		BUG();

	/* We may have missed the deadline, but there's no real way of
	   knowing for sure.  If the event was in the past, then we'll
	   get an immediate interrupt. */

	return 0;
}

static const struct clock_event_device xen_timerop_clockevent = {
	.name = "xen",
	.features = CLOCK_EVT_FEAT_ONESHOT,

	.max_delta_ns = 0xffffffff,
	.min_delta_ns = TIMER_SLOP,

	.mult = 1,
	.shift = 0,
	.rating = 500,

	.set_mode = xen_timerop_set_mode,
	.set_next_event = xen_timerop_set_next_event,
};



static void xen_vcpuop_set_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	int cpu = smp_processor_id();

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		WARN_ON(1);	/* unsupported */
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		if (HYPERVISOR_vcpu_op(VCPUOP_stop_periodic_timer, cpu, NULL))
			BUG();
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		if (HYPERVISOR_vcpu_op(VCPUOP_stop_singleshot_timer, cpu, NULL) ||
		    HYPERVISOR_vcpu_op(VCPUOP_stop_periodic_timer, cpu, NULL))
			BUG();
		break;
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static int xen_vcpuop_set_next_event(unsigned long delta,
				     struct clock_event_device *evt)
{
	int cpu = smp_processor_id();
	struct vcpu_set_singleshot_timer single;
	int ret;

	WARN_ON(evt->mode != CLOCK_EVT_MODE_ONESHOT);

	single.timeout_abs_ns = get_abs_timeout(delta);
	single.flags = VCPU_SSHOTTMR_future;

	ret = HYPERVISOR_vcpu_op(VCPUOP_set_singleshot_timer, cpu, &single);

	BUG_ON(ret != 0 && ret != -ETIME);

	return ret;
}

static const struct clock_event_device xen_vcpuop_clockevent = {
	.name = "xen",
	.features = CLOCK_EVT_FEAT_ONESHOT,

	.max_delta_ns = 0xffffffff,
	.min_delta_ns = TIMER_SLOP,

	.mult = 1,
	.shift = 0,
	.rating = 500,

	.set_mode = xen_vcpuop_set_mode,
	.set_next_event = xen_vcpuop_set_next_event,
};

static const struct clock_event_device *xen_clockevent =
	&xen_timerop_clockevent;

struct xen_clock_event_device {
	struct clock_event_device evt;
	char *name;
};
static DEFINE_PER_CPU(struct xen_clock_event_device, xen_clock_events) = { .evt.irq = -1 };

static irqreturn_t xen_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &__get_cpu_var(xen_clock_events).evt;
	irqreturn_t ret;

	ret = IRQ_NONE;
	if (evt->event_handler) {
		evt->event_handler(evt);
		ret = IRQ_HANDLED;
	}

	do_stolen_accounting();

	return ret;
}

void xen_teardown_timer(int cpu)
{
	struct clock_event_device *evt;
	BUG_ON(cpu == 0);
	evt = &per_cpu(xen_clock_events, cpu).evt;

	if (evt->irq >= 0) {
		unbind_from_irqhandler(evt->irq, NULL);
		evt->irq = -1;
		kfree(per_cpu(xen_clock_events, cpu).name);
		per_cpu(xen_clock_events, cpu).name = NULL;
	}
}

void xen_setup_timer(int cpu)
{
	char *name;
	struct clock_event_device *evt;
	int irq;

	evt = &per_cpu(xen_clock_events, cpu).evt;
	WARN(evt->irq >= 0, "IRQ%d for CPU%d is already allocated\n", evt->irq, cpu);
	if (evt->irq >= 0)
		xen_teardown_timer(cpu);

	printk(KERN_INFO "installing Xen timer for CPU %d\n", cpu);

	name = kasprintf(GFP_KERNEL, "timer%d", cpu);
	if (!name)
		name = "<timer kasprintf failed>";

	irq = bind_virq_to_irqhandler(VIRQ_TIMER, cpu, xen_timer_interrupt,
				      IRQF_DISABLED|IRQF_PERCPU|
				      IRQF_NOBALANCING|IRQF_TIMER|
				      IRQF_FORCE_RESUME,
				      name, NULL);

	memcpy(evt, xen_clockevent, sizeof(*evt));

	evt->cpumask = cpumask_of(cpu);
	evt->irq = irq;
	per_cpu(xen_clock_events, cpu).name = name;
}


void xen_setup_cpu_clockevents(void)
{
	BUG_ON(preemptible());

	clockevents_register_device(&__get_cpu_var(xen_clock_events).evt);
}

void xen_timer_resume(void)
{
	int cpu;

	pvclock_resume();

	if (xen_clockevent != &xen_vcpuop_clockevent)
		return;

	for_each_online_cpu(cpu) {
		if (HYPERVISOR_vcpu_op(VCPUOP_stop_periodic_timer, cpu, NULL))
			BUG();
	}
}

static const struct pv_time_ops xen_time_ops __initconst = {
	.sched_clock = xen_clocksource_read,
};

static void __init xen_time_init(void)
{
	int cpu = smp_processor_id();
	struct timespec tp;

	clocksource_register_hz(&xen_clocksource, NSEC_PER_SEC);

	if (HYPERVISOR_vcpu_op(VCPUOP_stop_periodic_timer, cpu, NULL) == 0) {
		/* Successfully turned off 100Hz tick, so we have the
		   vcpuop-based timer interface */
		printk(KERN_DEBUG "Xen: using vcpuop timer interface\n");
		xen_clockevent = &xen_vcpuop_clockevent;
	}

	/* Set initial system time with full resolution */
	xen_read_wallclock(&tp);
	do_settimeofday(&tp);

	setup_force_cpu_cap(X86_FEATURE_TSC);

	xen_setup_runstate_info(cpu);
	xen_setup_timer(cpu);
	xen_setup_cpu_clockevents();

	if (xen_initial_domain())
		pvclock_gtod_register_notifier(&xen_pvclock_gtod_notifier);
}

void __init xen_init_time_ops(void)
{
	pv_time_ops = xen_time_ops;

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
	/* vector callback is needed otherwise we cannot receive interrupts
	 * on cpu > 0 and at this point we don't know how many cpus are
	 * available */
	if (!xen_have_vector_callback)
		return;
	if (!xen_feature(XENFEAT_hvm_safe_pvclock)) {
		printk(KERN_INFO "Xen doesn't support pvclock on HVM,"
				"disable pv timer\n");
		return;
	}

	pv_time_ops = xen_time_ops;
	x86_init.timers.setup_percpu_clockev = xen_time_init;
	x86_cpuinit.setup_percpu_clockev = xen_hvm_setup_cpu_clockevents;

	x86_platform.calibrate_tsc = xen_tsc_khz;
	x86_platform.get_wallclock = xen_get_wallclock;
	x86_platform.set_wallclock = xen_set_wallclock;
}
#endif
