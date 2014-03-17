/*
 *  Copyright (C) 2001 Andrea Arcangeli <andrea@suse.de> SuSE
 *  Copyright 2003 Andi Kleen, SuSE Labs.
 *
 *  Modified for x86 32 bit architecture by
 *  Stefani Seibold <stefani@seibold.net>
 *  sponsored by Rohde & Schwarz GmbH & Co. KG Munich/Germany
 *
 *  Thanks to hpa@transmeta.com for some useful hint.
 *  Special thanks to Ingo Molnar for his early experience with
 *  a different vsyscall implementation for Linux/IA32 and for the name.
 *
 */

#include <linux/timekeeper_internal.h>
#include <asm/vgtod.h>
#include <asm/vvar.h>

DEFINE_VVAR(struct vsyscall_gtod_data, vsyscall_gtod_data);

void update_vsyscall_tz(void)
{
	vsyscall_gtod_data.tz_minuteswest = sys_tz.tz_minuteswest;
	vsyscall_gtod_data.tz_dsttime = sys_tz.tz_dsttime;
}

void update_vsyscall(struct timekeeper *tk)
{
	struct vsyscall_gtod_data *vdata = &vsyscall_gtod_data;

	gtod_write_begin(vdata);

	/* copy vsyscall data */
	vdata->vclock_mode	= tk->clock->archdata.vclock_mode;
	vdata->cycle_last	= tk->clock->cycle_last;
	vdata->mask		= tk->clock->mask;
	vdata->mult		= tk->mult;
	vdata->shift		= tk->shift;

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

	vdata->wall_time_coarse_sec	= tk->xtime_sec;
	vdata->wall_time_coarse_nsec	= (long)(tk->xtime_nsec >> tk->shift);

	vdata->monotonic_time_coarse_sec =
		vdata->wall_time_coarse_sec + tk->wall_to_monotonic.tv_sec;
	vdata->monotonic_time_coarse_nsec =
		vdata->wall_time_coarse_nsec + tk->wall_to_monotonic.tv_nsec;

	while (vdata->monotonic_time_coarse_nsec >= NSEC_PER_SEC) {
		vdata->monotonic_time_coarse_nsec -= NSEC_PER_SEC;
		vdata->monotonic_time_coarse_sec++;
	}

	gtod_write_end(vdata);
}
