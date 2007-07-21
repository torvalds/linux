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

#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>

#include <xen/events.h>
#include <xen/interface/xen.h>
#include <xen/interface/vcpu.h>

#include "xen-ops.h"

#define XEN_SHIFT 22

/* Xen may fire a timer up to this many ns early */
#define TIMER_SLOP	100000
#define NS_PER_TICK	(1000000000LL / HZ)

static cycle_t xen_clocksource_read(void);

/* These are perodically updated in shared_info, and then copied here. */
struct shadow_time_info {
	u64 tsc_timestamp;     /* TSC at last update of time vals.  */
	u64 system_timestamp;  /* Time, in nanosecs, since boot.    */
	u32 tsc_to_nsec_mul;
	int tsc_shift;
	u32 version;
};

static DEFINE_PER_CPU(struct shadow_time_info, shadow_time);

/* runstate info updated by Xen */
static DEFINE_PER_CPU(struct vcpu_runstate_info, runstate);

/* snapshots of runstate info */
static DEFINE_PER_CPU(struct vcpu_runstate_info, runstate_snapshot);

/* unused ns of stolen and blocked time */
static DEFINE_PER_CPU(u64, residual_stolen);
static DEFINE_PER_CPU(u64, residual_blocked);

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

	state = &__get_cpu_var(runstate);

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

static void setup_runstate_info(int cpu)
{
	struct vcpu_register_runstate_memory_area area;

	area.addr.v = &per_cpu(runstate, cpu);

	if (HYPERVISOR_vcpu_op(VCPUOP_register_runstate_memory_area,
			       cpu, &area))
		BUG();
}

static void do_stolen_accounting(void)
{
	struct vcpu_runstate_info state;
	struct vcpu_runstate_info *snap;
	s64 blocked, runnable, offline, stolen;
	cputime_t ticks;

	get_runstate_snapshot(&state);

	WARN_ON(state.state != RUNSTATE_running);

	snap = &__get_cpu_var(runstate_snapshot);

	/* work out how much time the VCPU has not been runn*ing*  */
	blocked = state.time[RUNSTATE_blocked] - snap->time[RUNSTATE_blocked];
	runnable = state.time[RUNSTATE_runnable] - snap->time[RUNSTATE_runnable];
	offline = state.time[RUNSTATE_offline] - snap->time[RUNSTATE_offline];

	*snap = state;

	/* Add the appropriate number of ticks of stolen time,
	   including any left-overs from last time.  Passing NULL to
	   account_steal_time accounts the time as stolen. */
	stolen = runnable + offline + __get_cpu_var(residual_stolen);

	if (stolen < 0)
		stolen = 0;

	ticks = 0;
	while (stolen >= NS_PER_TICK) {
		ticks++;
		stolen -= NS_PER_TICK;
	}
	__get_cpu_var(residual_stolen) = stolen;
	account_steal_time(NULL, ticks);

	/* Add the appropriate number of ticks of blocked time,
	   including any left-overs from last time.  Passing idle to
	   account_steal_time accounts the time as idle/wait. */
	blocked += __get_cpu_var(residual_blocked);

	if (blocked < 0)
		blocked = 0;

	ticks = 0;
	while (blocked >= NS_PER_TICK) {
		ticks++;
		blocked -= NS_PER_TICK;
	}
	__get_cpu_var(residual_blocked) = blocked;
	account_steal_time(idle_task(smp_processor_id()), ticks);
}

/*
 * Xen sched_clock implementation.  Returns the number of unstolen
 * nanoseconds, which is nanoseconds the VCPU spent in RUNNING+BLOCKED
 * states.
 */
unsigned long long xen_sched_clock(void)
{
	struct vcpu_runstate_info state;
	cycle_t now;
	u64 ret;
	s64 offset;

	/*
	 * Ideally sched_clock should be called on a per-cpu basis
	 * anyway, so preempt should already be disabled, but that's
	 * not current practice at the moment.
	 */
	preempt_disable();

	now = xen_clocksource_read();

	get_runstate_snapshot(&state);

	WARN_ON(state.state != RUNSTATE_running);

	offset = now - state.state_entry_time;
	if (offset < 0)
		offset = 0;

	ret = state.time[RUNSTATE_blocked] +
		state.time[RUNSTATE_running] +
		offset;

	preempt_enable();

	return ret;
}


/* Get the CPU speed from Xen */
unsigned long xen_cpu_khz(void)
{
	u64 cpu_khz = 1000000ULL << 32;
	const struct vcpu_time_info *info =
		&HYPERVISOR_shared_info->vcpu_info[0].time;

	do_div(cpu_khz, info->tsc_to_system_mul);
	if (info->tsc_shift < 0)
		cpu_khz <<= -info->tsc_shift;
	else
		cpu_khz >>= info->tsc_shift;

	return cpu_khz;
}

/*
 * Reads a consistent set of time-base values from Xen, into a shadow data
 * area.
 */
static unsigned get_time_values_from_xen(void)
{
	struct vcpu_time_info   *src;
	struct shadow_time_info *dst;

	/* src is shared memory with the hypervisor, so we need to
	   make sure we get a consistent snapshot, even in the face of
	   being preempted. */
	src = &__get_cpu_var(xen_vcpu)->time;
	dst = &__get_cpu_var(shadow_time);

	do {
		dst->version = src->version;
		rmb();		/* fetch version before data */
		dst->tsc_timestamp     = src->tsc_timestamp;
		dst->system_timestamp  = src->system_time;
		dst->tsc_to_nsec_mul   = src->tsc_to_system_mul;
		dst->tsc_shift         = src->tsc_shift;
		rmb();		/* test version after fetching data */
	} while ((src->version & 1) | (dst->version ^ src->version));

	return dst->version;
}

/*
 * Scale a 64-bit delta by scaling and multiplying by a 32-bit fraction,
 * yielding a 64-bit result.
 */
static inline u64 scale_delta(u64 delta, u32 mul_frac, int shift)
{
	u64 product;
#ifdef __i386__
	u32 tmp1, tmp2;
#endif

	if (shift < 0)
		delta >>= -shift;
	else
		delta <<= shift;

#ifdef __i386__
	__asm__ (
		"mul  %5       ; "
		"mov  %4,%%eax ; "
		"mov  %%edx,%4 ; "
		"mul  %5       ; "
		"xor  %5,%5    ; "
		"add  %4,%%eax ; "
		"adc  %5,%%edx ; "
		: "=A" (product), "=r" (tmp1), "=r" (tmp2)
		: "a" ((u32)delta), "1" ((u32)(delta >> 32)), "2" (mul_frac) );
#elif __x86_64__
	__asm__ (
		"mul %%rdx ; shrd $32,%%rdx,%%rax"
		: "=a" (product) : "0" (delta), "d" ((u64)mul_frac) );
#else
#error implement me!
#endif

	return product;
}

static u64 get_nsec_offset(struct shadow_time_info *shadow)
{
	u64 now, delta;
	now = native_read_tsc();
	delta = now - shadow->tsc_timestamp;
	return scale_delta(delta, shadow->tsc_to_nsec_mul, shadow->tsc_shift);
}

static cycle_t xen_clocksource_read(void)
{
	struct shadow_time_info *shadow = &get_cpu_var(shadow_time);
	cycle_t ret;
	unsigned version;

	do {
		version = get_time_values_from_xen();
		barrier();
		ret = shadow->system_timestamp + get_nsec_offset(shadow);
		barrier();
	} while (version != __get_cpu_var(xen_vcpu)->time.version);

	put_cpu_var(shadow_time);

	return ret;
}

static void xen_read_wallclock(struct timespec *ts)
{
	const struct shared_info *s = HYPERVISOR_shared_info;
	u32 version;
	u64 delta;
	struct timespec now;

	/* get wallclock at system boot */
	do {
		version = s->wc_version;
		rmb();		/* fetch version before time */
		now.tv_sec  = s->wc_sec;
		now.tv_nsec = s->wc_nsec;
		rmb();		/* fetch time before checking version */
	} while ((s->wc_version & 1) | (version ^ s->wc_version));

	delta = xen_clocksource_read();	/* time since system boot */
	delta += now.tv_sec * (u64)NSEC_PER_SEC + now.tv_nsec;

	now.tv_nsec = do_div(delta, NSEC_PER_SEC);
	now.tv_sec = delta;

	set_normalized_timespec(ts, now.tv_sec, now.tv_nsec);
}

unsigned long xen_get_wallclock(void)
{
	struct timespec ts;

	xen_read_wallclock(&ts);

	return ts.tv_sec;
}

int xen_set_wallclock(unsigned long now)
{
	/* do nothing for domU */
	return -1;
}

static struct clocksource xen_clocksource __read_mostly = {
	.name = "xen",
	.rating = 400,
	.read = xen_clocksource_read,
	.mask = ~0,
	.mult = 1<<XEN_SHIFT,		/* time directly in nanoseconds */
	.shift = XEN_SHIFT,
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
static DEFINE_PER_CPU(struct clock_event_device, xen_clock_events);

static irqreturn_t xen_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &__get_cpu_var(xen_clock_events);
	irqreturn_t ret;

	ret = IRQ_NONE;
	if (evt->event_handler) {
		evt->event_handler(evt);
		ret = IRQ_HANDLED;
	}

	do_stolen_accounting();

	return ret;
}

void xen_setup_timer(int cpu)
{
	const char *name;
	struct clock_event_device *evt;
	int irq;

	printk(KERN_INFO "installing Xen timer for CPU %d\n", cpu);

	name = kasprintf(GFP_KERNEL, "timer%d", cpu);
	if (!name)
		name = "<timer kasprintf failed>";

	irq = bind_virq_to_irqhandler(VIRQ_TIMER, cpu, xen_timer_interrupt,
				      IRQF_DISABLED|IRQF_PERCPU|IRQF_NOBALANCING,
				      name, NULL);

	evt = &per_cpu(xen_clock_events, cpu);
	memcpy(evt, xen_clockevent, sizeof(*evt));

	evt->cpumask = cpumask_of_cpu(cpu);
	evt->irq = irq;

	setup_runstate_info(cpu);
}

void xen_setup_cpu_clockevents(void)
{
	BUG_ON(preemptible());

	clockevents_register_device(&__get_cpu_var(xen_clock_events));
}

__init void xen_time_init(void)
{
	int cpu = smp_processor_id();

	get_time_values_from_xen();

	clocksource_register(&xen_clocksource);

	if (HYPERVISOR_vcpu_op(VCPUOP_stop_periodic_timer, cpu, NULL) == 0) {
		/* Successfully turned off 100Hz tick, so we have the
		   vcpuop-based timer interface */
		printk(KERN_DEBUG "Xen: using vcpuop timer interface\n");
		xen_clockevent = &xen_vcpuop_clockevent;
	}

	/* Set initial system time with full resolution */
	xen_read_wallclock(&xtime);
	set_normalized_timespec(&wall_to_monotonic,
				-xtime.tv_sec, -xtime.tv_nsec);

	tsc_disable = 0;

	xen_setup_timer(cpu);
	xen_setup_cpu_clockevents();
}
