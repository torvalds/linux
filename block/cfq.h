#ifndef _CFQ_H
#define _CFQ_H
#include "blk-cgroup.h"

#ifdef CONFIG_CFQ_GROUP_IOSCHED
static inline void cfq_blkiocg_update_io_add_stats(struct blkio_group *blkg,
	struct blkio_group *curr_blkg, bool direction, bool sync)
{
	blkiocg_update_io_add_stats(blkg, curr_blkg, direction, sync);
}

static inline void cfq_blkiocg_update_dequeue_stats(struct blkio_group *blkg,
			unsigned long dequeue)
{
	blkiocg_update_dequeue_stats(blkg, dequeue);
}

static inline void cfq_blkiocg_update_timeslice_used(struct blkio_group *blkg,
			unsigned long time)
{
	blkiocg_update_timeslice_used(blkg, time);
}

static inline void cfq_blkiocg_set_start_empty_time(struct blkio_group *blkg)
{
	blkiocg_set_start_empty_time(blkg);
}

static inline void cfq_blkiocg_update_io_remove_stats(struct blkio_group *blkg,
				bool direction, bool sync)
{
	blkiocg_update_io_remove_stats(blkg, direction, sync);
}

static inline void cfq_blkiocg_update_io_merged_stats(struct blkio_group *blkg,
		bool direction, bool sync)
{
	blkiocg_update_io_merged_stats(blkg, direction, sync);
}

static inline void cfq_blkiocg_update_idle_time_stats(struct blkio_group *blkg)
{
	blkiocg_update_idle_time_stats(blkg);
}

static inline void
cfq_blkiocg_update_avg_queue_size_stats(struct blkio_group *blkg)
{
	blkiocg_update_avg_queue_size_stats(blkg);
}

static inline void
cfq_blkiocg_update_set_idle_time_stats(struct blkio_group *blkg)
{
	blkiocg_update_set_idle_time_stats(blkg);
}

static inline void cfq_blkiocg_update_dispatch_stats(struct blkio_group *blkg,
				uint64_t bytes, bool direction, bool sync)
{
	blkiocg_update_dispatch_stats(blkg, bytes, direction, sync);
}

static inline void cfq_blkiocg_update_completion_stats(struct blkio_group *blkg, uint64_t start_time, uint64_t io_start_time, bool direction, bool sync)
{
	blkiocg_update_completion_stats(blkg, start_time, io_start_time,
				direction, sync);
}

static inline void cfq_blkiocg_add_blkio_group(struct blkio_cgroup *blkcg,
			struct blkio_group *blkg, void *key, dev_t dev) {
	blkiocg_add_blkio_group(blkcg, blkg, key, dev);
}

static inline int cfq_blkiocg_del_blkio_group(struct blkio_group *blkg)
{
	return blkiocg_del_blkio_group(blkg);
}

#else /* CFQ_GROUP_IOSCHED */
static inline void cfq_blkiocg_update_io_add_stats(struct blkio_group *blkg,
	struct blkio_group *curr_blkg, bool direction, bool sync) {}

static inline void cfq_blkiocg_update_dequeue_stats(struct blkio_group *blkg,
			unsigned long dequeue) {}

static inline void cfq_blkiocg_update_timeslice_used(struct blkio_group *blkg,
			unsigned long time) {}
static inline void cfq_blkiocg_set_start_empty_time(struct blkio_group *blkg) {}
static inline void cfq_blkiocg_update_io_remove_stats(struct blkio_group *blkg,
				bool direction, bool sync) {}
static inline void cfq_blkiocg_update_io_merged_stats(struct blkio_group *blkg,
		bool direction, bool sync) {}
static inline void cfq_blkiocg_update_idle_time_stats(struct blkio_group *blkg)
{
}
static inline void
cfq_blkiocg_update_avg_queue_size_stats(struct blkio_group *blkg) {}

static inline void
cfq_blkiocg_update_set_idle_time_stats(struct blkio_group *blkg) {}

static inline void cfq_blkiocg_update_dispatch_stats(struct blkio_group *blkg,
				uint64_t bytes, bool direction, bool sync) {}
static inline void cfq_blkiocg_update_completion_stats(struct blkio_group *blkg, uint64_t start_time, uint64_t io_start_time, bool direction, bool sync) {}

static inline void cfq_blkiocg_add_blkio_group(struct blkio_cgroup *blkcg,
			struct blkio_group *blkg, void *key, dev_t dev) {}
static inline int cfq_blkiocg_del_blkio_group(struct blkio_group *blkg)
{
	return 0;
}

#endif /* CFQ_GROUP_IOSCHED */
#endif
