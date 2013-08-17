#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/time.h>

#include "fimc-is-time.h"

#ifdef MEASURE_TIME
#ifdef INTERNAL_TIME

void measure_init(struct fimc_is_time *time, u32 instance, u32 group_id, u32 frames)
{
	time->instance = instance;
	time->group_id = group_id;
	time->frames = frames;
	time->time_count = 0;
	time->time1_min = 0;
	time->time1_max = 0;
	time->time1_avg = 0;
	time->time2_min = 0;
	time->time2_max = 0;
	time->time2_avg = 0;
	time->time3_min = 0;
	time->time3_max = 0;
	time->time3_avg = 0;
	time->time4_cur = 0;
	time->time4_old = 0;
	time->time4_avg = 0;
}

void measure_internal_time(
	struct fimc_is_time *time,
	struct timeval *time_queued,
	struct timeval *time_shot,
	struct timeval *time_shotdone,
	struct timeval *time_dequeued)
{
	u32 temp1, temp2, temp3;

	temp1 = (time_shot->tv_sec - time_queued->tv_sec)*1000000 +
		(time_shot->tv_usec - time_queued->tv_usec);
	temp2 = (time_shotdone->tv_sec - time_shot->tv_sec)*1000000 +
		(time_shotdone->tv_usec - time_shot->tv_usec);
	temp3 = (time_dequeued->tv_sec - time_shotdone->tv_sec)*1000000 +
		(time_dequeued->tv_usec - time_shotdone->tv_usec);

	if (!time->time_count) {
		time->time1_min = temp1;
		time->time1_max = temp1;
		time->time2_min = temp2;
		time->time2_max = temp2;
		time->time3_min = temp3;
		time->time3_max = temp3;
	} else {
		if (time->time1_min > temp1)
			time->time1_min = temp1;

		if (time->time1_max < temp1)
			time->time1_max = temp1;

		if (time->time2_min > temp2)
			time->time2_min = temp2;

		if (time->time2_max < temp2)
			time->time2_max = temp2;

		if (time->time3_min > temp3)
			time->time3_min = temp3;

		if (time->time3_max < temp3)
			time->time3_max = temp3;
	}

	time->time1_avg += temp1;
	time->time2_avg += temp2;
	time->time3_avg += temp3;

	time->time4_cur = time_queued->tv_sec*1000000 + time_queued->tv_usec;
	time->time4_avg += (time->time4_cur - time->time4_old);
	time->time4_old = time->time4_cur;

	time->time_count++;

	if (time->time_count % time->frames)
		return;

	pr_info("I%dG%d t1(%05d,%05d,%05d), t2(%05d,%05d,%05d), t3(%05d,%05d,%05d) : %d(%dfps)",
		time->instance, time->group_id,
		temp1, time->time1_max, time->time1_avg / time->time_count,
		temp2, time->time2_max, time->time2_avg / time->time_count,
		temp3, time->time3_max, time->time3_avg / time->time_count,
		time->time4_avg / time->frames,
		(1000000 * time->frames) / time->time4_avg);

	time->time_count = 0;
	time->time1_avg = 0;
	time->time2_avg = 0;
	time->time3_avg = 0;
	time->time4_avg = 0;
}

#endif
#endif
