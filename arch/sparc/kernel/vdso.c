/*
 *  Copyright (C) 2001 Andrea Arcangeli <andrea@suse.de> SuSE
 *  Copyright 2003 Andi Kleen, SuSE Labs.
 *
 *  Thanks to hpa@transmeta.com for some useful hint.
 *  Special thanks to Ingo Molnar for his early experience with
 *  a different vsyscall implementation for Linux/IA32 and for the name.
 */

#include <linux/seqlock.h>
#include <linux/time.h>
#include <linux/timekeeper_internal.h>

#include <asm/vvar.h>

void update_vsyscall_tz(void)
{
	if (unlikely(vvar_data == NULL))
		return;

	vvar_data->tz_minuteswest = sys_tz.tz_minuteswest;
	vvar_data->tz_dsttime = sys_tz.tz_dsttime;
}

void update_vsyscall(struct timekeeper *tk)
{
	struct vvar_data *vdata = vvar_data;

	if (unlikely(vdata == NULL))
		return;

	vvar_write_begin(vdata);
	vdata->vclock_mode = tk->tkr_mono.clock->archdata.vclock_mode;
	vdata->clock.cycle_last = tk->tkr_mono.cycle_last;
	vdata->clock.mask = tk->tkr_mono.mask;
	vdata->clock.mult = tk->tkr_mono.mult;
	vdata->clock.shift = tk->tkr_mono.shift;

	vdata->wall_time_sec = tk->xtime_sec;
	vdata->wall_time_snsec = tk->tkr_mono.xtime_nsec;

	vdata->monotonic_time_sec = tk->xtime_sec +
				    tk->wall_to_monotonic.tv_sec;
	vdata->monotonic_time_snsec = tk->tkr_mono.xtime_nsec +
				      (tk->wall_to_monotonic.tv_nsec <<
				       tk->tkr_mono.shift);

	while (vdata->monotonic_time_snsec >=
	       (((u64)NSEC_PER_SEC) << tk->tkr_mono.shift)) {
		vdata->monotonic_time_snsec -=
				((u64)NSEC_PER_SEC) << tk->tkr_mono.shift;
		vdata->monotonic_time_sec++;
	}

	vdata->wall_time_coarse_sec = tk->xtime_sec;
	vdata->wall_time_coarse_nsec =
			(long)(tk->tkr_mono.xtime_nsec >> tk->tkr_mono.shift);

	vdata->monotonic_time_coarse_sec =
		vdata->wall_time_coarse_sec + tk->wall_to_monotonic.tv_sec;
	vdata->monotonic_time_coarse_nsec =
		vdata->wall_time_coarse_nsec + tk->wall_to_monotonic.tv_nsec;

	while (vdata->monotonic_time_coarse_nsec >= NSEC_PER_SEC) {
		vdata->monotonic_time_coarse_nsec -= NSEC_PER_SEC;
		vdata->monotonic_time_coarse_sec++;
	}

	vvar_write_end(vdata);
}
