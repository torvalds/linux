/*  paravirtual clock -- common code used by kvm/xen

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <linux/kernel.h>
#include <linux/percpu.h>
#include <asm/pvclock.h>

/*
 * These are perodically updated
 *    xen: magic shared_info page
 *    kvm: gpa registered via msr
 * and then copied here.
 */
struct pvclock_shadow_time {
	u64 tsc_timestamp;     /* TSC at last update of time vals.  */
	u64 system_timestamp;  /* Time, in nanosecs, since boot.    */
	u32 tsc_to_nsec_mul;
	int tsc_shift;
	u32 version;
};

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
#elif defined(__x86_64__)
	__asm__ (
		"mul %%rdx ; shrd $32,%%rdx,%%rax"
		: "=a" (product) : "0" (delta), "d" ((u64)mul_frac) );
#else
#error implement me!
#endif

	return product;
}

static u64 pvclock_get_nsec_offset(struct pvclock_shadow_time *shadow)
{
	u64 delta = native_read_tsc() - shadow->tsc_timestamp;
	return scale_delta(delta, shadow->tsc_to_nsec_mul, shadow->tsc_shift);
}

/*
 * Reads a consistent set of time-base values from hypervisor,
 * into a shadow data area.
 */
static unsigned pvclock_get_time_values(struct pvclock_shadow_time *dst,
					struct pvclock_vcpu_time_info *src)
{
	do {
		dst->version = src->version;
		rmb();		/* fetch version before data */
		dst->tsc_timestamp     = src->tsc_timestamp;
		dst->system_timestamp  = src->system_time;
		dst->tsc_to_nsec_mul   = src->tsc_to_system_mul;
		dst->tsc_shift         = src->tsc_shift;
		rmb();		/* test version after fetching data */
	} while ((src->version & 1) || (dst->version != src->version));

	return dst->version;
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

static atomic64_t last_value = ATOMIC64_INIT(0);

void pvclock_resume(void)
{
	atomic64_set(&last_value, 0);
}

cycle_t pvclock_clocksource_read(struct pvclock_vcpu_time_info *src)
{
	struct pvclock_shadow_time shadow;
	unsigned version;
	cycle_t ret, offset;
	u64 last;

	do {
		version = pvclock_get_time_values(&shadow, src);
		barrier();
		offset = pvclock_get_nsec_offset(&shadow);
		ret = shadow.system_timestamp + offset;
		barrier();
	} while (version != src->version);

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
			    struct timespec *ts)
{
	u32 version;
	u64 delta;
	struct timespec now;

	/* get wallclock at system boot */
	do {
		version = wall_clock->version;
		rmb();		/* fetch version before time */
		now.tv_sec  = wall_clock->sec;
		now.tv_nsec = wall_clock->nsec;
		rmb();		/* fetch time before checking version */
	} while ((wall_clock->version & 1) || (version != wall_clock->version));

	delta = pvclock_clocksource_read(vcpu_time);	/* time since system boot */
	delta += now.tv_sec * (u64)NSEC_PER_SEC + now.tv_nsec;

	now.tv_nsec = do_div(delta, NSEC_PER_SEC);
	now.tv_sec = delta;

	set_normalized_timespec(ts, now.tv_sec, now.tv_nsec);
}
