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

int vclocks_used __read_mostly;

DEFINE_VVAR(struct vsyscall_gtod_data, vsyscall_gtod_data);

void update_vsyscall_tz(void)
{
	vsyscall_gtod_data.tz_minuteswest = sys_tz.tz_minuteswest;
	vsyscall_gtod_data.tz_dsttime = sys_tz.tz_dsttime;
}

void update_vsyscall(struct timekeeper *tk)
{
	int vclock_mode = tk->tkr_mono.clock->archdata.vclock_mode;
	struct vsyscall_gtod_data *vdata = &vsyscall_gtod_data;

	/* Mark the new vclock used. */
	BUILD_BUG_ON(VCLOCK_MAX >= 32);
	WRITE_ONCE(vclocks_used, READ_ONCE(vclocks_used) | (1 << vclock_mode));

	gtod_write_begin(vdata);

	/* copy vsyscall data */
	vdata->vclock_mode	= vclock_mode;
	vdata->cycle_last	= tk->tkr_mono.cycle_last;
	vdata->mask		= tk->tkr_mono.mask;
	vdata->mult		= tk->tkr_mono.mult;
	vdata->shift		= tk->tkr_mono.shift;

	vdata->wall_time_sec		= tk->xtime_sec;
	vdata->wall_time_snsec		= tk->tkr_mono.xtime_nsec;

	vdata->monotonic_time_sec	= tk->xtime_sec
					+ tk->wall_to_monotonic.tv_sec;
	vdata->monotonic_time_snsec	= tk->tkr_mono.xtime_nsec
					+ ((u64)tk->wall_to_monotonic.tv_nsec
						<< tk->tkr_mono.shift);
	while (vdata->monotonic_time_snsec >=
					(((u64)NSEC_PER_SEC) << tk->tkr_mono.shift)) {
		vdata->monotonic_time_snsec -=
					((u64)NSEC_PER_SEC) << tk->tkr_mono.shift;
		vdata->monotonic_time_sec++;
	}

	vdata->wall_time_coarse_sec	= tk->xtime_sec;
	vdata->wall_time_coarse_nsec	= (long)(tk->tkr_mono.xtime_nsec >>
						 tk->tkr_mono.shift);

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
