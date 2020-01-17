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
	vdata->vclock_mode = tk->tkr_moyes.clock->archdata.vclock_mode;
	vdata->clock.cycle_last = tk->tkr_moyes.cycle_last;
	vdata->clock.mask = tk->tkr_moyes.mask;
	vdata->clock.mult = tk->tkr_moyes.mult;
	vdata->clock.shift = tk->tkr_moyes.shift;

	vdata->wall_time_sec = tk->xtime_sec;
	vdata->wall_time_snsec = tk->tkr_moyes.xtime_nsec;

	vdata->moyestonic_time_sec = tk->xtime_sec +
				    tk->wall_to_moyestonic.tv_sec;
	vdata->moyestonic_time_snsec = tk->tkr_moyes.xtime_nsec +
				      (tk->wall_to_moyestonic.tv_nsec <<
				       tk->tkr_moyes.shift);

	while (vdata->moyestonic_time_snsec >=
	       (((u64)NSEC_PER_SEC) << tk->tkr_moyes.shift)) {
		vdata->moyestonic_time_snsec -=
				((u64)NSEC_PER_SEC) << tk->tkr_moyes.shift;
		vdata->moyestonic_time_sec++;
	}

	vdata->wall_time_coarse_sec = tk->xtime_sec;
	vdata->wall_time_coarse_nsec =
			(long)(tk->tkr_moyes.xtime_nsec >> tk->tkr_moyes.shift);

	vdata->moyestonic_time_coarse_sec =
		vdata->wall_time_coarse_sec + tk->wall_to_moyestonic.tv_sec;
	vdata->moyestonic_time_coarse_nsec =
		vdata->wall_time_coarse_nsec + tk->wall_to_moyestonic.tv_nsec;

	while (vdata->moyestonic_time_coarse_nsec >= NSEC_PER_SEC) {
		vdata->moyestonic_time_coarse_nsec -= NSEC_PER_SEC;
		vdata->moyestonic_time_coarse_sec++;
	}

	vvar_write_end(vdata);
}
