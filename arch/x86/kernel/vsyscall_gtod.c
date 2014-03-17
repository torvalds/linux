/*
 *  Copyright (C) 2001 Andrea Arcangeli <andrea@suse.de> SuSE
 *  Copyright 2003 Andi Kleen, SuSE Labs.
 *
 *  Modified for x86 32 bit architecture by
 *  Stefani Seibold <stefani@seibold.net>
 *
 *  Thanks to hpa@transmeta.com for some useful hint.
 *  Special thanks to Ingo Molnar for his early experience with
 *  a different vsyscall implementation for Linux/IA32 and for the name.
 *
 */

#include <linux/timekeeper_internal.h>
#include <asm/vgtod.h>

DEFINE_VVAR(struct vsyscall_gtod_data, vsyscall_gtod_data);

void update_vsyscall_tz(void)
{
	vsyscall_gtod_data.sys_tz = sys_tz;
}

void update_vsyscall(struct timekeeper *tk)
{
	struct vsyscall_gtod_data *vdata = &vsyscall_gtod_data;

	write_seqcount_begin(&vdata->seq);

	/* copy vsyscall data */
	vdata->clock.vclock_mode	= tk->clock->archdata.vclock_mode;
	vdata->clock.cycle_last		= tk->clock->cycle_last;
	vdata->clock.mask		= tk->clock->mask;
	vdata->clock.mult		= tk->mult;
	vdata->clock.shift		= tk->shift;

	vdata->wall_time_sec		= tk->xtime_sec;
	vdata->wall_time_snsec		= tk->xtime_nsec;

	vdata->monotonic_time_sec	= tk->xtime_sec
					+ tk->wall_to_monotonic.tv_sec;
	vdata->monotonic_time_snsec	= tk->xtime_nsec
					+ (tk->wall_to_monotonic.tv_nsec
						<< tk->shift);
	while (vdata->monotonic_time_snsec >=
					(((u64)NSEC_PER_SEC) << tk->shift)) {
		vdata->monotonic_time_snsec -=
					((u64)NSEC_PER_SEC) << tk->shift;
		vdata->monotonic_time_sec++;
	}

	vdata->wall_time_coarse.tv_sec	= tk->xtime_sec;
	vdata->wall_time_coarse.tv_nsec	= (long)(tk->xtime_nsec >> tk->shift);

	vdata->monotonic_time_coarse	= timespec_add(vdata->wall_time_coarse,
							tk->wall_to_monotonic);

	write_seqcount_end(&vdata->seq);
}
