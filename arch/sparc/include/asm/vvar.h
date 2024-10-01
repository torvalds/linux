/*
 * Copyright (c) 2017 Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _ASM_SPARC_VVAR_DATA_H
#define _ASM_SPARC_VVAR_DATA_H

#include <asm/clocksource.h>
#include <asm/processor.h>
#include <asm/barrier.h>
#include <linux/time.h>
#include <linux/types.h>

struct vvar_data {
	unsigned int seq;

	int vclock_mode;
	struct { /* extract of a clocksource struct */
		u64	cycle_last;
		u64	mask;
		int	mult;
		int	shift;
	} clock;
	/* open coded 'struct timespec' */
	u64		wall_time_sec;
	u64		wall_time_snsec;
	u64		monotonic_time_snsec;
	u64		monotonic_time_sec;
	u64		monotonic_time_coarse_sec;
	u64		monotonic_time_coarse_nsec;
	u64		wall_time_coarse_sec;
	u64		wall_time_coarse_nsec;

	int		tz_minuteswest;
	int		tz_dsttime;
};

extern struct vvar_data *vvar_data;
extern int vdso_fix_stick;

static inline unsigned int vvar_read_begin(const struct vvar_data *s)
{
	unsigned int ret;

repeat:
	ret = READ_ONCE(s->seq);
	if (unlikely(ret & 1)) {
		cpu_relax();
		goto repeat;
	}
	smp_rmb(); /* Finish all reads before we return seq */
	return ret;
}

static inline int vvar_read_retry(const struct vvar_data *s,
					unsigned int start)
{
	smp_rmb(); /* Finish all reads before checking the value of seq */
	return unlikely(s->seq != start);
}

static inline void vvar_write_begin(struct vvar_data *s)
{
	++s->seq;
	smp_wmb(); /* Makes sure that increment of seq is reflected */
}

static inline void vvar_write_end(struct vvar_data *s)
{
	smp_wmb(); /* Makes the value of seq current before we increment */
	++s->seq;
}


#endif /* _ASM_SPARC_VVAR_DATA_H */
