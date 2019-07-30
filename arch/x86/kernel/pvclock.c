// SPDX-License-Identifier: GPL-2.0-or-later
/*  paravirtual clock -- common code used by kvm/xen

*/

#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/sched.h>
#include <linux/gfp.h>
#include <linux/memblock.h>
#include <linux/nmi.h>

#include <asm/fixmap.h>
#include <asm/pvclock.h>
#include <asm/vgtod.h>

static u8 valid_flags __read_mostly = 0;
static struct pvclock_vsyscall_time_info *pvti_cpu0_va __read_mostly;

void pvclock_set_flags(u8 flags)
{
	valid_flags = flags;
}

unsigned long pvclock_tsc_khz(struct pvclock_vcpu_time_info *src)
{
	u64 pv_tsc_khz = 1000000ULL << 32;

	do_div(pv_tsc_khz, src->tsc_to_system_mul);
	if (src->tsc_shift < 0)
		pv_tsc_khz <<= -src->tsc_shift;
	else
		pv_tsc_khz >>= src->tsc_shift;
	return pv_tsc_khz;
}

void pvclock_touch_watchdogs(void)
{
	touch_softlockup_watchdog_sync();
	clocksource_touch_watchdog();
	rcu_cpu_stall_reset();
	reset_hung_task_detector();
}

static atomic64_t last_value = ATOMIC64_INIT(0);

void pvclock_resume(void)
{
	atomic64_set(&last_value, 0);
}

u8 pvclock_read_flags(struct pvclock_vcpu_time_info *src)
{
	unsigned version;
	u8 flags;

	do {
		version = pvclock_read_begin(src);
		flags = src->flags;
	} while (pvclock_read_retry(src, version));

	return flags & valid_flags;
}

u64 pvclock_clocksource_read(struct pvclock_vcpu_time_info *src)
{
	unsigned version;
	u64 ret;
	u64 last;
	u8 flags;

	do {
		version = pvclock_read_begin(src);
		ret = __pvclock_read_cycles(src, rdtsc_ordered());
		flags = src->flags;
	} while (pvclock_read_retry(src, version));

	if (unlikely((flags & PVCLOCK_GUEST_STOPPED) != 0)) {
		src->flags &= ~PVCLOCK_GUEST_STOPPED;
		pvclock_touch_watchdogs();
	}

	if ((valid_flags & PVCLOCK_TSC_STABLE_BIT) &&
		(flags & PVCLOCK_TSC_STABLE_BIT))
		return ret;

	/*
	 * Assumption here is that last_value, a global accumulator, always goes
	 * forward. If we are less than that, we should not be much smaller.
	 * We assume there is an error marging we're inside, and then the correction
	 * does not sacrifice accuracy.
	 *
	 * For reads: global may have changed between test and return,
	 * but this means someone else updated poked the clock at a later time.
	 * We just need to make sure we are not seeing a backwards event.
	 *
	 * For updates: last_value = ret is not enough, since two vcpus could be
	 * updating at the same time, and one of them could be slightly behind,
	 * making the assumption that last_value always go forward fail to hold.
	 */
	last = atomic64_read(&last_value);
	do {
		if (ret < last)
			return last;
		last = atomic64_cmpxchg(&last_value, last, ret);
	} while (unlikely(last != ret));

	return ret;
}

void pvclock_read_wallclock(struct pvclock_wall_clock *wall_clock,
			    struct pvclock_vcpu_time_info *vcpu_time,
			    struct timespec64 *ts)
{
	u32 version;
	u64 delta;
	struct timespec64 now;

	/* get wallclock at system boot */
	do {
		version = wall_clock->version;
		rmb();		/* fetch version before time */
		/*
		 * Note: wall_clock->sec is a u32 value, so it can
		 * only store dates between 1970 and 2106. To allow
		 * times beyond that, we need to create a new hypercall
		 * interface with an extended pvclock_wall_clock structure
		 * like ARM has.
		 */
		now.tv_sec  = wall_clock->sec;
		now.tv_nsec = wall_clock->nsec;
		rmb();		/* fetch time before checking version */
	} while ((wall_clock->version & 1) || (version != wall_clock->version));

	delta = pvclock_clocksource_read(vcpu_time);	/* time since system boot */
	delta += now.tv_sec * NSEC_PER_SEC + now.tv_nsec;

	now.tv_nsec = do_div(delta, NSEC_PER_SEC);
	now.tv_sec = delta;

	set_normalized_timespec64(ts, now.tv_sec, now.tv_nsec);
}

void pvclock_set_pvti_cpu0_va(struct pvclock_vsyscall_time_info *pvti)
{
	WARN_ON(vclock_was_used(VCLOCK_PVCLOCK));
	pvti_cpu0_va = pvti;
}

struct pvclock_vsyscall_time_info *pvclock_get_pvti_cpu0_va(void)
{
	return pvti_cpu0_va;
}
EXPORT_SYMBOL_GPL(pvclock_get_pvti_cpu0_va);
