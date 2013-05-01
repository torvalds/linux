#ifndef _ASM_X86_PVCLOCK_H
#define _ASM_X86_PVCLOCK_H

#include <linux/clocksource.h>
#include <asm/pvclock-abi.h>

/* some helper functions for xen and kvm pv clock sources */
cycle_t pvclock_clocksource_read(struct pvclock_vcpu_time_info *src);
u8 pvclock_read_flags(struct pvclock_vcpu_time_info *src);
void pvclock_set_flags(u8 flags);
unsigned long pvclock_tsc_khz(struct pvclock_vcpu_time_info *src);
void pvclock_read_wallclock(struct pvclock_wall_clock *wall,
			    struct pvclock_vcpu_time_info *vcpu,
			    struct timespec *ts);
void pvclock_resume(void);

/*
 * Scale a 64-bit delta by scaling and multiplying by a 32-bit fraction,
 * yielding a 64-bit result.
 */
static inline u64 pvclock_scale_delta(u64 delta, u32 mul_frac, int shift)
{
	u64 product;
#ifdef __i386__
	u32 tmp1, tmp2;
#else
	ulong tmp;
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
		"mulq %[mul_frac] ; shrd $32, %[hi], %[lo]"
		: [lo]"=a"(product),
		  [hi]"=d"(tmp)
		: "0"(delta),
		  [mul_frac]"rm"((u64)mul_frac));
#else
#error implement me!
#endif

	return product;
}

static __always_inline
u64 pvclock_get_nsec_offset(const struct pvclock_vcpu_time_info *src)
{
	u64 delta = __native_read_tsc() - src->tsc_timestamp;
	return pvclock_scale_delta(delta, src->tsc_to_system_mul,
				   src->tsc_shift);
}

static __always_inline
unsigned __pvclock_read_cycles(const struct pvclock_vcpu_time_info *src,
			       cycle_t *cycles, u8 *flags)
{
	unsigned version;
	cycle_t ret, offset;
	u8 ret_flags;

	version = src->version;
	/* Note: emulated platforms which do not advertise SSE2 support
	 * result in kvmclock not using the necessary RDTSC barriers.
	 * Without barriers, it is possible that RDTSC instruction reads from
	 * the time stamp counter outside rdtsc_barrier protected section
	 * below, resulting in violation of monotonicity.
	 */
	rdtsc_barrier();
	offset = pvclock_get_nsec_offset(src);
	ret = src->system_time + offset;
	ret_flags = src->flags;
	rdtsc_barrier();

	*cycles = ret;
	*flags = ret_flags;
	return version;
}

struct pvclock_vsyscall_time_info {
	struct pvclock_vcpu_time_info pvti;
	u32 migrate_count;
} __attribute__((__aligned__(SMP_CACHE_BYTES)));

#define PVTI_SIZE sizeof(struct pvclock_vsyscall_time_info)
#define PVCLOCK_VSYSCALL_NR_PAGES (((NR_CPUS-1)/(PAGE_SIZE/PVTI_SIZE))+1)

int __init pvclock_init_vsyscall(struct pvclock_vsyscall_time_info *i,
				 int size);
struct pvclock_vcpu_time_info *pvclock_get_vsyscall_time_info(int cpu);

#endif /* _ASM_X86_PVCLOCK_H */
