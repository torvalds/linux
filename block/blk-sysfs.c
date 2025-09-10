// SPDX-License-Identifier: GPL-2.0
/*
 * Functions related to sysfs handling
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/blktrace_api.h>
#include <linux/debugfs.h>

#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-debugfs.h"
#include "blk-mq-sched.h"
#include "blk-rq-qos.h"
#include "blk-wbt.h"
#include "blk-cgroup.h"
#include "blk-throttle.h"

struct queue_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct gendisk *disk, char *page);
	ssize_t (*show_limit)(struct gendisk *disk, char *page);

	ssize_t (*store)(struct gendisk *disk, const char *page, size_t count);
	int (*store_limit)(struct gendisk *disk, const char *page,
			size_t count, struct queue_limits *lim);
};

static ssize_t
queue_var_show(unsigned long var, char *page)
{
	return sysfs_emit(page, "%lu\n", var);
}

static ssize_t
queue_var_store(unsigned long *var, const char *page, size_t count)
{
	int err;
	unsigned long v;

	err = kstrtoul(page, 10, &v);
	if (err || v > UINT_MAX)
		return -EINVAL;

	*var = v;

	return count;
}

static ssize_t queue_requests_show(struct gendisk *disk, char *page)
{
	ssize_t ret;

	mutex_lock(&disk->queue->elevator_lock);
	ret = queue_var_show(disk->queue->nr_requests, page);
	mutex_unlock(&disk->queue->elevator_lock);
	return ret;
}

static ssize_t
queue_requests_store(struct gendisk *disk, const char *page, size_t count)
{
	struct request_queue *q = disk->queue;
	struct blk_mq_tag_set *set = q->tag_set;
	struct elevator_tags *et = NULL;
	unsigned int memflags;
	unsigned long nr;
	int ret;

	ret = queue_var_store(&nr, page, count);
	if (ret < 0)
		return ret;

	/*
	 * Serialize updating nr_requests with concurrent queue_requests_store()
	 * and switching elevator.
	 */
	down_write(&set->update_nr_hwq_lock);

	if (nr == q->nr_requests)
		goto unlock;

	if (nr < BLKDEV_MIN_RQ)
		nr = BLKDEV_MIN_RQ;

	/*
	 * Switching elevator is protected by update_nr_hwq_lock:
	 *  - read lock is held from elevator sysfs attribute;
	 *  - write lock is held from updating nr_hw_queues;
	 * Hence it's safe to access q->elevator here with write lock held.
	 */
	if (nr <= set->reserved_tags ||
	    (q->elevator && nr > MAX_SCHED_RQ) ||
	    (!q->elevator && nr > set->queue_depth)) {
		ret = -EINVAL;
		goto unlock;
	}

	if (!blk_mq_is_shared_tags(set->flags) && q->elevator &&
	    nr > q->elevator->et->nr_requests) {
		/*
		 * Tags will grow, allocate memory before freezing queue to
		 * prevent deadlock.
		 */
		et = blk_mq_alloc_sched_tags(set, q->nr_hw_queues, nr);
		if (!et) {
			ret = -ENOMEM;
			goto unlock;
		}
	}

	memflags = blk_mq_freeze_queue(q);
	mutex_lock(&q->elevator_lock);
	et = blk_mq_update_nr_requests(q, et, nr);
	mutex_unlock(&q->elevator_lock);
	blk_mq_unfreeze_queue(q, memflags);

	if (et)
		blk_mq_free_sched_tags(et, set);

unlock:
	up_write(&set->update_nr_hwq_lock);
	return ret;
}

static ssize_t queue_ra_show(struct gendisk *disk, char *page)
{
	ssize_t ret;

	mutex_lock(&disk->queue->limits_lock);
	ret = queue_var_show(disk->bdi->ra_pages << (PAGE_SHIFT - 10), page);
	mutex_unlock(&disk->queue->limits_lock);

	return ret;
}

static ssize_t
queue_ra_store(struct gendisk *disk, const char *page, size_t count)
{
	unsigned long ra_kb;
	ssize_t ret;
	unsigned int memflags;
	struct request_queue *q = disk->queue;

	ret = queue_var_store(&ra_kb, page, count);
	if (ret < 0)
		return ret;
	/*
	 * ->ra_pages is protected by ->limits_lock because it is usually
	 * calculated from the queue limits by queue_limits_commit_update.
	 */
	mutex_lock(&q->limits_lock);
	memflags = blk_mq_freeze_queue(q);
	disk->bdi->ra_pages = ra_kb >> (PAGE_SHIFT - 10);
	mutex_unlock(&q->limits_lock);
	blk_mq_unfreeze_queue(q, memflags);

	return ret;
}

#define QUEUE_SYSFS_LIMIT_SHOW(_field)					\
static ssize_t queue_##_field##_show(struct gendisk *disk, char *page)	\
{									\
	return queue_var_show(disk->queue->limits._field, page);	\
}

QUEUE_SYSFS_LIMIT_SHOW(max_segments)
QUEUE_SYSFS_LIMIT_SHOW(max_discard_segments)
QUEUE_SYSFS_LIMIT_SHOW(max_integrity_segments)
QUEUE_SYSFS_LIMIT_SHOW(max_segment_size)
QUEUE_SYSFS_LIMIT_SHOW(max_write_streams)
QUEUE_SYSFS_LIMIT_SHOW(write_stream_granularity)
QUEUE_SYSFS_LIMIT_SHOW(logical_block_size)
QUEUE_SYSFS_LIMIT_SHOW(physical_block_size)
QUEUE_SYSFS_LIMIT_SHOW(chunk_sectors)
QUEUE_SYSFS_LIMIT_SHOW(io_min)
QUEUE_SYSFS_LIMIT_SHOW(io_opt)
QUEUE_SYSFS_LIMIT_SHOW(discard_granularity)
QUEUE_SYSFS_LIMIT_SHOW(zone_write_granularity)
QUEUE_SYSFS_LIMIT_SHOW(virt_boundary_mask)
QUEUE_SYSFS_LIMIT_SHOW(dma_alignment)
QUEUE_SYSFS_LIMIT_SHOW(max_open_zones)
QUEUE_SYSFS_LIMIT_SHOW(max_active_zones)
QUEUE_SYSFS_LIMIT_SHOW(atomic_write_unit_min)
QUEUE_SYSFS_LIMIT_SHOW(atomic_write_unit_max)

#define QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_BYTES(_field)			\
static ssize_t queue_##_field##_show(struct gendisk *disk, char *page)	\
{									\
	return sysfs_emit(page, "%llu\n",				\
		(unsigned long long)disk->queue->limits._field <<	\
			SECTOR_SHIFT);					\
}

QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_BYTES(max_discard_sectors)
QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_BYTES(max_hw_discard_sectors)
QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_BYTES(max_write_zeroes_sectors)
QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_BYTES(max_hw_wzeroes_unmap_sectors)
QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_BYTES(max_wzeroes_unmap_sectors)
QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_BYTES(atomic_write_max_sectors)
QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_BYTES(atomic_write_boundary_sectors)
QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_BYTES(max_zone_append_sectors)

#define QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_KB(_field)			\
static ssize_t queue_##_field##_show(struct gendisk *disk, char *page)	\
{									\
	return queue_var_show(disk->queue->limits._field >> 1, page);	\
}

QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_KB(max_sectors)
QUEUE_SYSFS_LIMIT_SHOW_SECTORS_TO_KB(max_hw_sectors)

#define QUEUE_SYSFS_SHOW_CONST(_name, _val)				\
static ssize_t queue_##_name##_show(struct gendisk *disk, char *page)	\
{									\
	return sysfs_emit(page, "%d\n", _val);				\
}

/* deprecated fields */
QUEUE_SYSFS_SHOW_CONST(discard_zeroes_data, 0)
QUEUE_SYSFS_SHOW_CONST(write_same_max, 0)
QUEUE_SYSFS_SHOW_CONST(poll_delay, -1)

static int queue_max_discard_sectors_store(struct gendisk *disk,
		const char *page, size_t count, struct queue_limits *lim)
{
	unsigned long max_discard_bytes;
	ssize_t ret;

	ret = queue_var_store(&max_discard_bytes, page, count);
	if (ret < 0)
		return ret;

	if (max_discard_bytes & (disk->queue->limits.discard_granularity - 1))
		return -EINVAL;

	if ((max_discard_bytes >> SECTOR_SHIFT) > UINT_MAX)
		return -EINVAL;

	lim->max_user_discard_sectors = max_discard_bytes >> SECTOR_SHIFT;
	return 0;
}

static int queue_max_wzeroes_unmap_sectors_store(struct gendisk *disk,
		const char *page, size_t count, struct queue_limits *lim)
{
	unsigned long max_zeroes_bytes, max_hw_zeroes_bytes;
	ssize_t ret;

	ret = queue_var_store(&max_zeroes_bytes, page, count);
	if (ret < 0)
		return ret;

	max_hw_zeroes_bytes = lim->max_hw_wzeroes_unmap_sectors << SECTOR_SHIFT;
	if (max_zeroes_bytes != 0 && max_zeroes_bytes != max_hw_zeroes_bytes)
		return -EINVAL;

	lim->max_user_wzeroes_unmap_sectors = max_zeroes_bytes >> SECTOR_SHIFT;
	return 0;
}

static int
queue_max_sectors_store(struct gendisk *disk, const char *page, size_t count,
		struct queue_limits *lim)
{
	unsigned long max_sectors_kb;
	ssize_t ret;

	ret = queue_var_store(&max_sectors_kb, page, count);
	if (ret < 0)
		return ret;

	lim->max_user_sectors = max_sectors_kb << 1;
	return 0;
}

static ssize_t queue_feature_store(struct gendisk *disk, const char *page,
		size_t count, struct queue_limits *lim, blk_features_t feature)
{
	unsigned long val;
	ssize_t ret;

	ret = queue_var_store(&val, page, count);
	if (ret < 0)
		return ret;

	if (val)
		lim->features |= feature;
	else
		lim->features &= ~feature;
	return 0;
}

#define QUEUE_SYSFS_FEATURE(_name, _feature)				\
static ssize_t queue_##_name##_show(struct gendisk *disk, char *page)	\
{									\
	return sysfs_emit(page, "%u\n",					\
		!!(disk->queue->limits.features & _feature));		\
}									\
static int queue_##_name##_store(struct gendisk *disk,			\
		const char *page, size_t count, struct queue_limits *lim) \
{									\
	return queue_feature_store(disk, page, count, lim, _feature);	\
}

QUEUE_SYSFS_FEATURE(rotational, BLK_FEAT_ROTATIONAL)
QUEUE_SYSFS_FEATURE(add_random, BLK_FEAT_ADD_RANDOM)
QUEUE_SYSFS_FEATURE(iostats, BLK_FEAT_IO_STAT)
QUEUE_SYSFS_FEATURE(stable_writes, BLK_FEAT_STABLE_WRITES);

#define QUEUE_SYSFS_FEATURE_SHOW(_name, _feature)			\
static ssize_t queue_##_name##_show(struct gendisk *disk, char *page)	\
{									\
	return sysfs_emit(page, "%u\n",					\
		!!(disk->queue->limits.features & _feature));		\
}

QUEUE_SYSFS_FEATURE_SHOW(fua, BLK_FEAT_FUA);
QUEUE_SYSFS_FEATURE_SHOW(dax, BLK_FEAT_DAX);

static ssize_t queue_poll_show(struct gendisk *disk, char *page)
{
	if (queue_is_mq(disk->queue))
		return sysfs_emit(page, "%u\n", blk_mq_can_poll(disk->queue));

	return sysfs_emit(page, "%u\n",
			!!(disk->queue->limits.features & BLK_FEAT_POLL));
}

static ssize_t queue_zoned_show(struct gendisk *disk, char *page)
{
	if (blk_queue_is_zoned(disk->queue))
		return sysfs_emit(page, "host-managed\n");
	return sysfs_emit(page, "none\n");
}

static ssize_t queue_nr_zones_show(struct gendisk *disk, char *page)
{
	return queue_var_show(disk_nr_zones(disk), page);
}

static ssize_t queue_iostats_passthrough_show(struct gendisk *disk, char *page)
{
	return queue_var_show(!!blk_queue_passthrough_stat(disk->queue), page);
}

static int queue_iostats_passthrough_store(struct gendisk *disk,
		const char *page, size_t count, struct queue_limits *lim)
{
	unsigned long ios;
	ssize_t ret;

	ret = queue_var_store(&ios, page, count);
	if (ret < 0)
		return ret;

	if (ios)
		lim->flags |= BLK_FLAG_IOSTATS_PASSTHROUGH;
	else
		lim->flags &= ~BLK_FLAG_IOSTATS_PASSTHROUGH;
	return 0;
}

static ssize_t queue_nomerges_show(struct gendisk *disk, char *page)
{
	return queue_var_show((blk_queue_nomerges(disk->queue) << 1) |
			       blk_queue_noxmerges(disk->queue), page);
}

static ssize_t queue_nomerges_store(struct gendisk *disk, const char *page,
				    size_t count)
{
	unsigned long nm;
	unsigned int memflags;
	struct request_queue *q = disk->queue;
	ssize_t ret = queue_var_store(&nm, page, count);

	if (ret < 0)
		return ret;

	memflags = blk_mq_freeze_queue(q);
	blk_queue_flag_clear(QUEUE_FLAG_NOMERGES, q);
	blk_queue_flag_clear(QUEUE_FLAG_NOXMERGES, q);
	if (nm == 2)
		blk_queue_flag_set(QUEUE_FLAG_NOMERGES, q);
	else if (nm)
		blk_queue_flag_set(QUEUE_FLAG_NOXMERGES, q);
	blk_mq_unfreeze_queue(q, memflags);

	return ret;
}

static ssize_t queue_rq_affinity_show(struct gendisk *disk, char *page)
{
	bool set = test_bit(QUEUE_FLAG_SAME_COMP, &disk->queue->queue_flags);
	bool force = test_bit(QUEUE_FLAG_SAME_FORCE, &disk->queue->queue_flags);

	return queue_var_show(set << force, page);
}

static ssize_t
queue_rq_affinity_store(struct gendisk *disk, const char *page, size_t count)
{
	ssize_t ret = -EINVAL;
#ifdef CONFIG_SMP
	struct request_queue *q = disk->queue;
	unsigned long val;
	unsigned int memflags;

	ret = queue_var_store(&val, page, count);
	if (ret < 0)
		return ret;

	/*
	 * Here we update two queue flags each using atomic bitops, although
	 * updating two flags isn't atomic it should be harmless as those flags
	 * are accessed individually using atomic test_bit operation. So we
	 * don't grab any lock while updating these flags.
	 */
	memflags = blk_mq_freeze_queue(q);
	if (val == 2) {
		blk_queue_flag_set(QUEUE_FLAG_SAME_COMP, q);
		blk_queue_flag_set(QUEUE_FLAG_SAME_FORCE, q);
	} else if (val == 1) {
		blk_queue_flag_set(QUEUE_FLAG_SAME_COMP, q);
		blk_queue_flag_clear(QUEUE_FLAG_SAME_FORCE, q);
	} else if (val == 0) {
		blk_queue_flag_clear(QUEUE_FLAG_SAME_COMP, q);
		blk_queue_flag_clear(QUEUE_FLAG_SAME_FORCE, q);
	}
	blk_mq_unfreeze_queue(q, memflags);
#endif
	return ret;
}

static ssize_t queue_poll_delay_store(struct gendisk *disk, const char *page,
				size_t count)
{
	return count;
}

static ssize_t queue_poll_store(struct gendisk *disk, const char *page,
				size_t count)
{
	unsigned int memflags;
	ssize_t ret = count;
	struct request_queue *q = disk->queue;

	memflags = blk_mq_freeze_queue(q);
	if (!(q->limits.features & BLK_FEAT_POLL)) {
		ret = -EINVAL;
		goto out;
	}

	pr_info_ratelimited("writes to the poll attribute are ignored.\n");
	pr_info_ratelimited("please use driver specific parameters instead.\n");
out:
	blk_mq_unfreeze_queue(q, memflags);
	return ret;
}

static ssize_t queue_io_timeout_show(struct gendisk *disk, char *page)
{
	return sysfs_emit(page, "%u\n",
			jiffies_to_msecs(READ_ONCE(disk->queue->rq_timeout)));
}

static ssize_t queue_io_timeout_store(struct gendisk *disk, const char *page,
				  size_t count)
{
	unsigned int val, memflags;
	int err;
	struct request_queue *q = disk->queue;

	err = kstrtou32(page, 10, &val);
	if (err || val == 0)
		return -EINVAL;

	memflags = blk_mq_freeze_queue(q);
	blk_queue_rq_timeout(q, msecs_to_jiffies(val));
	blk_mq_unfreeze_queue(q, memflags);

	return count;
}

static ssize_t queue_wc_show(struct gendisk *disk, char *page)
{
	if (blk_queue_write_cache(disk->queue))
		return sysfs_emit(page, "write back\n");
	return sysfs_emit(page, "write through\n");
}

static int queue_wc_store(struct gendisk *disk, const char *page,
		size_t count, struct queue_limits *lim)
{
	bool disable;

	if (!strncmp(page, "write back", 10)) {
		disable = false;
	} else if (!strncmp(page, "write through", 13) ||
		   !strncmp(page, "none", 4)) {
		disable = true;
	} else {
		return -EINVAL;
	}

	if (disable)
		lim->flags |= BLK_FLAG_WRITE_CACHE_DISABLED;
	else
		lim->flags &= ~BLK_FLAG_WRITE_CACHE_DISABLED;
	return 0;
}

#define QUEUE_RO_ENTRY(_prefix, _name)			\
static struct queue_sysfs_entry _prefix##_entry = {	\
	.attr	= { .name = _name, .mode = 0444 },	\
	.show	= _prefix##_show,			\
};

#define QUEUE_RW_ENTRY(_prefix, _name)			\
static struct queue_sysfs_entry _prefix##_entry = {	\
	.attr	= { .name = _name, .mode = 0644 },	\
	.show	= _prefix##_show,			\
	.store	= _prefix##_store,			\
};

#define QUEUE_LIM_RO_ENTRY(_prefix, _name)			\
static struct queue_sysfs_entry _prefix##_entry = {	\
	.attr		= { .name = _name, .mode = 0444 },	\
	.show_limit	= _prefix##_show,			\
}

#define QUEUE_LIM_RW_ENTRY(_prefix, _name)			\
static struct queue_sysfs_entry _prefix##_entry = {	\
	.attr		= { .name = _name, .mode = 0644 },	\
	.show_limit	= _prefix##_show,			\
	.store_limit	= _prefix##_store,			\
}

QUEUE_RW_ENTRY(queue_requests, "nr_requests");
QUEUE_RW_ENTRY(queue_ra, "read_ahead_kb");
QUEUE_LIM_RW_ENTRY(queue_max_sectors, "max_sectors_kb");
QUEUE_LIM_RO_ENTRY(queue_max_hw_sectors, "max_hw_sectors_kb");
QUEUE_LIM_RO_ENTRY(queue_max_segments, "max_segments");
QUEUE_LIM_RO_ENTRY(queue_max_integrity_segments, "max_integrity_segments");
QUEUE_LIM_RO_ENTRY(queue_max_segment_size, "max_segment_size");
QUEUE_LIM_RO_ENTRY(queue_max_write_streams, "max_write_streams");
QUEUE_LIM_RO_ENTRY(queue_write_stream_granularity, "write_stream_granularity");
QUEUE_RW_ENTRY(elv_iosched, "scheduler");

QUEUE_LIM_RO_ENTRY(queue_logical_block_size, "logical_block_size");
QUEUE_LIM_RO_ENTRY(queue_physical_block_size, "physical_block_size");
QUEUE_LIM_RO_ENTRY(queue_chunk_sectors, "chunk_sectors");
QUEUE_LIM_RO_ENTRY(queue_io_min, "minimum_io_size");
QUEUE_LIM_RO_ENTRY(queue_io_opt, "optimal_io_size");

QUEUE_LIM_RO_ENTRY(queue_max_discard_segments, "max_discard_segments");
QUEUE_LIM_RO_ENTRY(queue_discard_granularity, "discard_granularity");
QUEUE_LIM_RO_ENTRY(queue_max_hw_discard_sectors, "discard_max_hw_bytes");
QUEUE_LIM_RW_ENTRY(queue_max_discard_sectors, "discard_max_bytes");
QUEUE_RO_ENTRY(queue_discard_zeroes_data, "discard_zeroes_data");

QUEUE_LIM_RO_ENTRY(queue_atomic_write_max_sectors, "atomic_write_max_bytes");
QUEUE_LIM_RO_ENTRY(queue_atomic_write_boundary_sectors,
		"atomic_write_boundary_bytes");
QUEUE_LIM_RO_ENTRY(queue_atomic_write_unit_max, "atomic_write_unit_max_bytes");
QUEUE_LIM_RO_ENTRY(queue_atomic_write_unit_min, "atomic_write_unit_min_bytes");

QUEUE_RO_ENTRY(queue_write_same_max, "write_same_max_bytes");
QUEUE_LIM_RO_ENTRY(queue_max_write_zeroes_sectors, "write_zeroes_max_bytes");
QUEUE_LIM_RO_ENTRY(queue_max_hw_wzeroes_unmap_sectors,
		"write_zeroes_unmap_max_hw_bytes");
QUEUE_LIM_RW_ENTRY(queue_max_wzeroes_unmap_sectors,
		"write_zeroes_unmap_max_bytes");
QUEUE_LIM_RO_ENTRY(queue_max_zone_append_sectors, "zone_append_max_bytes");
QUEUE_LIM_RO_ENTRY(queue_zone_write_granularity, "zone_write_granularity");

QUEUE_LIM_RO_ENTRY(queue_zoned, "zoned");
QUEUE_RO_ENTRY(queue_nr_zones, "nr_zones");
QUEUE_LIM_RO_ENTRY(queue_max_open_zones, "max_open_zones");
QUEUE_LIM_RO_ENTRY(queue_max_active_zones, "max_active_zones");

QUEUE_RW_ENTRY(queue_nomerges, "nomerges");
QUEUE_LIM_RW_ENTRY(queue_iostats_passthrough, "iostats_passthrough");
QUEUE_RW_ENTRY(queue_rq_affinity, "rq_affinity");
QUEUE_RW_ENTRY(queue_poll, "io_poll");
QUEUE_RW_ENTRY(queue_poll_delay, "io_poll_delay");
QUEUE_LIM_RW_ENTRY(queue_wc, "write_cache");
QUEUE_LIM_RO_ENTRY(queue_fua, "fua");
QUEUE_LIM_RO_ENTRY(queue_dax, "dax");
QUEUE_RW_ENTRY(queue_io_timeout, "io_timeout");
QUEUE_LIM_RO_ENTRY(queue_virt_boundary_mask, "virt_boundary_mask");
QUEUE_LIM_RO_ENTRY(queue_dma_alignment, "dma_alignment");

/* legacy alias for logical_block_size: */
static struct queue_sysfs_entry queue_hw_sector_size_entry = {
	.attr		= {.name = "hw_sector_size", .mode = 0444 },
	.show_limit	= queue_logical_block_size_show,
};

QUEUE_LIM_RW_ENTRY(queue_rotational, "rotational");
QUEUE_LIM_RW_ENTRY(queue_iostats, "iostats");
QUEUE_LIM_RW_ENTRY(queue_add_random, "add_random");
QUEUE_LIM_RW_ENTRY(queue_stable_writes, "stable_writes");

#ifdef CONFIG_BLK_WBT
static ssize_t queue_var_store64(s64 *var, const char *page)
{
	int err;
	s64 v;

	err = kstrtos64(page, 10, &v);
	if (err < 0)
		return err;

	*var = v;
	return 0;
}

static ssize_t queue_wb_lat_show(struct gendisk *disk, char *page)
{
	ssize_t ret;
	struct request_queue *q = disk->queue;

	mutex_lock(&disk->rqos_state_mutex);
	if (!wbt_rq_qos(q)) {
		ret = -EINVAL;
		goto out;
	}

	if (wbt_disabled(q)) {
		ret = sysfs_emit(page, "0\n");
		goto out;
	}

	ret = sysfs_emit(page, "%llu\n", div_u64(wbt_get_min_lat(q), 1000));
out:
	mutex_unlock(&disk->rqos_state_mutex);
	return ret;
}

static ssize_t queue_wb_lat_store(struct gendisk *disk, const char *page,
				  size_t count)
{
	struct request_queue *q = disk->queue;
	struct rq_qos *rqos;
	ssize_t ret;
	s64 val;
	unsigned int memflags;

	ret = queue_var_store64(&val, page);
	if (ret < 0)
		return ret;
	if (val < -1)
		return -EINVAL;

	/*
	 * Ensure that the queue is idled, in case the latency update
	 * ends up either enabling or disabling wbt completely. We can't
	 * have IO inflight if that happens.
	 */
	memflags = blk_mq_freeze_queue(q);

	rqos = wbt_rq_qos(q);
	if (!rqos) {
		ret = wbt_init(disk);
		if (ret)
			goto out;
	}

	ret = count;
	if (val == -1)
		val = wbt_default_latency_nsec(q);
	else if (val >= 0)
		val *= 1000ULL;

	if (wbt_get_min_lat(q) == val)
		goto out;

	blk_mq_quiesce_queue(q);

	mutex_lock(&disk->rqos_state_mutex);
	wbt_set_min_lat(q, val);
	mutex_unlock(&disk->rqos_state_mutex);

	blk_mq_unquiesce_queue(q);
out:
	blk_mq_unfreeze_queue(q, memflags);

	return ret;
}

QUEUE_RW_ENTRY(queue_wb_lat, "wbt_lat_usec");
#endif

/* Common attributes for bio-based and request-based queues. */
static struct attribute *queue_attrs[] = {
	/*
	 * Attributes which are protected with q->limits_lock.
	 */
	&queue_max_hw_sectors_entry.attr,
	&queue_max_sectors_entry.attr,
	&queue_max_segments_entry.attr,
	&queue_max_discard_segments_entry.attr,
	&queue_max_integrity_segments_entry.attr,
	&queue_max_segment_size_entry.attr,
	&queue_max_write_streams_entry.attr,
	&queue_write_stream_granularity_entry.attr,
	&queue_hw_sector_size_entry.attr,
	&queue_logical_block_size_entry.attr,
	&queue_physical_block_size_entry.attr,
	&queue_chunk_sectors_entry.attr,
	&queue_io_min_entry.attr,
	&queue_io_opt_entry.attr,
	&queue_discard_granularity_entry.attr,
	&queue_max_discard_sectors_entry.attr,
	&queue_max_hw_discard_sectors_entry.attr,
	&queue_atomic_write_max_sectors_entry.attr,
	&queue_atomic_write_boundary_sectors_entry.attr,
	&queue_atomic_write_unit_min_entry.attr,
	&queue_atomic_write_unit_max_entry.attr,
	&queue_max_write_zeroes_sectors_entry.attr,
	&queue_max_hw_wzeroes_unmap_sectors_entry.attr,
	&queue_max_wzeroes_unmap_sectors_entry.attr,
	&queue_max_zone_append_sectors_entry.attr,
	&queue_zone_write_granularity_entry.attr,
	&queue_rotational_entry.attr,
	&queue_zoned_entry.attr,
	&queue_max_open_zones_entry.attr,
	&queue_max_active_zones_entry.attr,
	&queue_iostats_passthrough_entry.attr,
	&queue_iostats_entry.attr,
	&queue_stable_writes_entry.attr,
	&queue_add_random_entry.attr,
	&queue_wc_entry.attr,
	&queue_fua_entry.attr,
	&queue_dax_entry.attr,
	&queue_virt_boundary_mask_entry.attr,
	&queue_dma_alignment_entry.attr,
	&queue_ra_entry.attr,

	/*
	 * Attributes which don't require locking.
	 */
	&queue_discard_zeroes_data_entry.attr,
	&queue_write_same_max_entry.attr,
	&queue_nr_zones_entry.attr,
	&queue_nomerges_entry.attr,
	&queue_poll_entry.attr,
	&queue_poll_delay_entry.attr,

	NULL,
};

/* Request-based queue attributes that are not relevant for bio-based queues. */
static struct attribute *blk_mq_queue_attrs[] = {
	/*
	 * Attributes which require some form of locking other than
	 * q->sysfs_lock.
	 */
	&elv_iosched_entry.attr,
	&queue_requests_entry.attr,
#ifdef CONFIG_BLK_WBT
	&queue_wb_lat_entry.attr,
#endif
	/*
	 * Attributes which don't require locking.
	 */
	&queue_rq_affinity_entry.attr,
	&queue_io_timeout_entry.attr,

	NULL,
};

static umode_t queue_attr_visible(struct kobject *kobj, struct attribute *attr,
				int n)
{
	struct gendisk *disk = container_of(kobj, struct gendisk, queue_kobj);
	struct request_queue *q = disk->queue;

	if ((attr == &queue_max_open_zones_entry.attr ||
	     attr == &queue_max_active_zones_entry.attr) &&
	    !blk_queue_is_zoned(q))
		return 0;

	return attr->mode;
}

static umode_t blk_mq_queue_attr_visible(struct kobject *kobj,
					 struct attribute *attr, int n)
{
	struct gendisk *disk = container_of(kobj, struct gendisk, queue_kobj);
	struct request_queue *q = disk->queue;

	if (!queue_is_mq(q))
		return 0;

	if (attr == &queue_io_timeout_entry.attr && !q->mq_ops->timeout)
		return 0;

	return attr->mode;
}

static struct attribute_group queue_attr_group = {
	.attrs = queue_attrs,
	.is_visible = queue_attr_visible,
};

static struct attribute_group blk_mq_queue_attr_group = {
	.attrs = blk_mq_queue_attrs,
	.is_visible = blk_mq_queue_attr_visible,
};

#define to_queue(atr) container_of((atr), struct queue_sysfs_entry, attr)

static ssize_t
queue_attr_show(struct kobject *kobj, struct attribute *attr, char *page)
{
	struct queue_sysfs_entry *entry = to_queue(attr);
	struct gendisk *disk = container_of(kobj, struct gendisk, queue_kobj);

	if (!entry->show && !entry->show_limit)
		return -EIO;

	if (entry->show_limit) {
		ssize_t res;

		mutex_lock(&disk->queue->limits_lock);
		res = entry->show_limit(disk, page);
		mutex_unlock(&disk->queue->limits_lock);
		return res;
	}

	return entry->show(disk, page);
}

static ssize_t
queue_attr_store(struct kobject *kobj, struct attribute *attr,
		    const char *page, size_t length)
{
	struct queue_sysfs_entry *entry = to_queue(attr);
	struct gendisk *disk = container_of(kobj, struct gendisk, queue_kobj);
	struct request_queue *q = disk->queue;

	if (!entry->store_limit && !entry->store)
		return -EIO;

	if (entry->store_limit) {
		ssize_t res;

		struct queue_limits lim = queue_limits_start_update(q);

		res = entry->store_limit(disk, page, length, &lim);
		if (res < 0) {
			queue_limits_cancel_update(q);
			return res;
		}

		res = queue_limits_commit_update_frozen(q, &lim);
		if (res)
			return res;
		return length;
	}

	return entry->store(disk, page, length);
}

static const struct sysfs_ops queue_sysfs_ops = {
	.show	= queue_attr_show,
	.store	= queue_attr_store,
};

static const struct attribute_group *blk_queue_attr_groups[] = {
	&queue_attr_group,
	&blk_mq_queue_attr_group,
	NULL
};

static void blk_queue_release(struct kobject *kobj)
{
	/* nothing to do here, all data is associated with the parent gendisk */
}

const struct kobj_type blk_queue_ktype = {
	.default_groups = blk_queue_attr_groups,
	.sysfs_ops	= &queue_sysfs_ops,
	.release	= blk_queue_release,
};

static void blk_debugfs_remove(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;

	mutex_lock(&q->debugfs_mutex);
	blk_trace_shutdown(q);
	debugfs_remove_recursive(q->debugfs_dir);
	q->debugfs_dir = NULL;
	q->sched_debugfs_dir = NULL;
	q->rqos_debugfs_dir = NULL;
	mutex_unlock(&q->debugfs_mutex);
}

/**
 * blk_register_queue - register a block layer queue with sysfs
 * @disk: Disk of which the request queue should be registered with sysfs.
 */
int blk_register_queue(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;
	int ret;

	ret = kobject_add(&disk->queue_kobj, &disk_to_dev(disk)->kobj, "queue");
	if (ret < 0)
		return ret;

	if (queue_is_mq(q)) {
		ret = blk_mq_sysfs_register(disk);
		if (ret)
			goto out_del_queue_kobj;
	}
	mutex_lock(&q->sysfs_lock);

	mutex_lock(&q->debugfs_mutex);
	q->debugfs_dir = debugfs_create_dir(disk->disk_name, blk_debugfs_root);
	if (queue_is_mq(q))
		blk_mq_debugfs_register(q);
	mutex_unlock(&q->debugfs_mutex);

	ret = disk_register_independent_access_ranges(disk);
	if (ret)
		goto out_debugfs_remove;

	ret = blk_crypto_sysfs_register(disk);
	if (ret)
		goto out_unregister_ia_ranges;

	if (queue_is_mq(q))
		elevator_set_default(q);

	blk_queue_flag_set(QUEUE_FLAG_REGISTERED, q);
	wbt_enable_default(disk);

	/* Now everything is ready and send out KOBJ_ADD uevent */
	kobject_uevent(&disk->queue_kobj, KOBJ_ADD);
	if (q->elevator)
		kobject_uevent(&q->elevator->kobj, KOBJ_ADD);
	mutex_unlock(&q->sysfs_lock);

	/*
	 * SCSI probing may synchronously create and destroy a lot of
	 * request_queues for non-existent devices.  Shutting down a fully
	 * functional queue takes measureable wallclock time as RCU grace
	 * periods are involved.  To avoid excessive latency in these
	 * cases, a request_queue starts out in a degraded mode which is
	 * faster to shut down and is made fully functional here as
	 * request_queues for non-existent devices never get registered.
	 */
	blk_queue_flag_set(QUEUE_FLAG_INIT_DONE, q);
	percpu_ref_switch_to_percpu(&q->q_usage_counter);

	return ret;

out_unregister_ia_ranges:
	disk_unregister_independent_access_ranges(disk);
out_debugfs_remove:
	blk_debugfs_remove(disk);
	mutex_unlock(&q->sysfs_lock);
	if (queue_is_mq(q))
		blk_mq_sysfs_unregister(disk);
out_del_queue_kobj:
	kobject_del(&disk->queue_kobj);
	return ret;
}

/**
 * blk_unregister_queue - counterpart of blk_register_queue()
 * @disk: Disk of which the request queue should be unregistered from sysfs.
 *
 * Note: the caller is responsible for guaranteeing that this function is called
 * after blk_register_queue() has finished.
 */
void blk_unregister_queue(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;

	if (WARN_ON(!q))
		return;

	/* Return early if disk->queue was never registered. */
	if (!blk_queue_registered(q))
		return;

	/*
	 * Since sysfs_remove_dir() prevents adding new directory entries
	 * before removal of existing entries starts, protect against
	 * concurrent elv_iosched_store() calls.
	 */
	mutex_lock(&q->sysfs_lock);
	blk_queue_flag_clear(QUEUE_FLAG_REGISTERED, q);
	mutex_unlock(&q->sysfs_lock);

	/*
	 * Remove the sysfs attributes before unregistering the queue data
	 * structures that can be modified through sysfs.
	 */
	if (queue_is_mq(q))
		blk_mq_sysfs_unregister(disk);
	blk_crypto_sysfs_unregister(disk);

	mutex_lock(&q->sysfs_lock);
	disk_unregister_independent_access_ranges(disk);
	mutex_unlock(&q->sysfs_lock);

	/* Now that we've deleted all child objects, we can delete the queue. */
	kobject_uevent(&disk->queue_kobj, KOBJ_REMOVE);
	kobject_del(&disk->queue_kobj);

	if (queue_is_mq(q))
		elevator_set_none(q);

	blk_debugfs_remove(disk);
}
