/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_VGTOD_H
#define _ASM_X86_VGTOD_H

#include <linux/compiler.h>
#include <linux/clocksource.h>

#include <uapi/linux/time.h>

#ifdef BUILD_VDSO32_64
typedef u64 gtod_long_t;
#else
typedef unsigned long gtod_long_t;
#endif

struct vgtod_ts {
	u64		sec;
	u64		nsec;
};

#define VGTOD_BASES	(CLOCK_MONOTONIC_COARSE + 1)
#define VGTOD_HRES	(BIT(CLOCK_REALTIME) | BIT(CLOCK_MONOTONIC))
#define VGTOD_COARSE	(BIT(CLOCK_REALTIME_COARSE) | BIT(CLOCK_MONOTONIC_COARSE))

/*
 * vsyscall_gtod_data will be accessed by 32 and 64 bit code at the same time
 * so be carefull by modifying this structure.
 */
struct vsyscall_gtod_data {
	unsigned int	seq;

	int		vclock_mode;
	u64		cycle_last;
	u64		mask;
	u32		mult;
	u32		shift;

	struct vgtod_ts	basetime[VGTOD_BASES];

	int		tz_minuteswest;
	int		tz_dsttime;
};
extern struct vsyscall_gtod_data vsyscall_gtod_data;

extern int vclocks_used;
static inline bool vclock_was_used(int vclock)
{
	return READ_ONCE(vclocks_used) & (1 << vclock);
}

static inline unsigned int gtod_read_begin(const struct vsyscall_gtod_data *s)
{
	unsigned int ret;

repeat:
	ret = READ_ONCE(s->seq);
	if (unlikely(ret & 1)) {
		cpu_relax();
		goto repeat;
	}
	smp_rmb();
	return ret;
}

static inline int gtod_read_retry(const struct vsyscall_gtod_data *s,
				  unsigned int start)
{
	smp_rmb();
	return unlikely(s->seq != start);
}

static inline void gtod_write_begin(struct vsyscall_gtod_data *s)
{
	++s->seq;
	smp_wmb();
}

static inline void gtod_write_end(struct vsyscall_gtod_data *s)
{
	smp_wmb();
	++s->seq;
}

#ifdef CONFIG_X86_64

#define VGETCPU_CPU_MASK 0xfff

static inline unsigned int __getcpu(void)
{
	unsigned int p;

	/*
	 * Load per CPU data from GDT.  LSL is faster than RDTSCP and
	 * works on all CPUs.  This is volatile so that it orders
	 * correctly wrt barrier() and to keep gcc from cleverly
	 * hoisting it out of the calling function.
	 *
	 * If RDPID is available, use it.
	 */
	alternative_io ("lsl %[seg],%[p]",
			".byte 0xf3,0x0f,0xc7,0xf8", /* RDPID %eax/rax */
			X86_FEATURE_RDPID,
			[p] "=a" (p), [seg] "r" (__PER_CPU_SEG));

	return p;
}

#endif /* CONFIG_X86_64 */

#endif /* _ASM_X86_VGTOD_H */
