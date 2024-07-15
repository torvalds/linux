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
	ssize_t (*show)(struct request_queue *, char *);
	ssize_t (*store)(struct request_queue *, const char *, size_t);
};

static ssize_t
queue_var_show(unsigned long var, char *page)
{
	return sprintf(page, "%lu\n", var);
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

static ssize_t queue_requests_show(struct request_queue *q, char *page)
{
	return queue_var_show(q->nr_requests, page);
}

static ssize_t
queue_requests_store(struct request_queue *q, const char *page, size_t count)
{
	unsigned long nr;
	int ret, err;

	if (!queue_is_mq(q))
		return -EINVAL;

	ret = queue_var_store(&nr, page, count);
	if (ret < 0)
		return ret;

	if (nr < BLKDEV_MIN_RQ)
		nr = BLKDEV_MIN_RQ;

	err = blk_mq_update_nr_requests(q, nr);
	if (err)
		return err;

	return ret;
}

static ssize_t queue_ra_show(struct request_queue *q, char *page)
{
	unsigned long ra_kb;

	if (!q->disk)
		return -EINVAL;
	ra_kb = q->disk->bdi->ra_pages << (PAGE_SHIFT - 10);
	return queue_var_show(ra_kb, page);
}

static ssize_t
queue_ra_store(struct request_queue *q, const char *page, size_t count)
{
	unsigned long ra_kb;
	ssize_t ret;

	if (!q->disk)
		return -EINVAL;
	ret = queue_var_store(&ra_kb, page, count);
	if (ret < 0)
		return ret;
	q->disk->bdi->ra_pages = ra_kb >> (PAGE_SHIFT - 10);
	return ret;
}

static ssize_t queue_max_sectors_show(struct request_queue *q, char *page)
{
	int max_sectors_kb = queue_max_sectors(q) >> 1;

	return queue_var_show(max_sectors_kb, page);
}

static ssize_t queue_max_segments_show(struct request_queue *q, char *page)
{
	return queue_var_show(queue_max_segments(q), page);
}

static ssize_t queue_max_discard_segments_show(struct request_queue *q,
		char *page)
{
	return queue_var_show(queue_max_discard_segments(q), page);
}

static ssize_t queue_max_integrity_segments_show(struct request_queue *q, char *page)
{
	return queue_var_show(q->limits.max_integrity_segments, page);
}

static ssize_t queue_max_segment_size_show(struct request_queue *q, char *page)
{
	return queue_var_show(queue_max_segment_size(q), page);
}

static ssize_t queue_logical_block_size_show(struct request_queue *q, char *page)
{
	return queue_var_show(queue_logical_block_size(q), page);
}

static ssize_t queue_physical_block_size_show(struct request_queue *q, char *page)
{
	return queue_var_show(queue_physical_block_size(q), page);
}

static ssize_t queue_chunk_sectors_show(struct request_queue *q, char *page)
{
	return queue_var_show(q->limits.chunk_sectors, page);
}

static ssize_t queue_io_min_show(struct request_queue *q, char *page)
{
	return queue_var_show(queue_io_min(q), page);
}

static ssize_t queue_io_opt_show(struct request_queue *q, char *page)
{
	return queue_var_show(queue_io_opt(q), page);
}

static ssize_t queue_discard_granularity_show(struct request_queue *q, char *page)
{
	return queue_var_show(q->limits.discard_granularity, page);
}

static ssize_t queue_discard_max_hw_show(struct request_queue *q, char *page)
{

	return sprintf(page, "%llu\n",
		(unsigned long long)q->limits.max_hw_discard_sectors << 9);
}

static ssize_t queue_discard_max_show(struct request_queue *q, char *page)
{
	return sprintf(page, "%llu\n",
		       (unsigned long long)q->limits.max_discard_sectors << 9);
}

static ssize_t queue_discard_max_store(struct request_queue *q,
				       const char *page, size_t count)
{
	unsigned long max_discard_bytes;
	struct queue_limits lim;
	ssize_t ret;
	int err;

	ret = queue_var_store(&max_discard_bytes, page, count);
	if (ret < 0)
		return ret;

	if (max_discard_bytes & (q->limits.discard_granularity - 1))
		return -EINVAL;

	if ((max_discard_bytes >> SECTOR_SHIFT) > UINT_MAX)
		return -EINVAL;

	blk_mq_freeze_queue(q);
	lim = queue_limits_start_update(q);
	lim.max_user_discard_sectors = max_discard_bytes >> SECTOR_SHIFT;
	err = queue_limits_commit_update(q, &lim);
	blk_mq_unfreeze_queue(q);

	if (err)
		return err;
	return ret;
}

static ssize_t queue_discard_zeroes_data_show(struct request_queue *q, char *page)
{
	return queue_var_show(0, page);
}

static ssize_t queue_write_same_max_show(struct request_queue *q, char *page)
{
	return queue_var_show(0, page);
}

static ssize_t queue_write_zeroes_max_show(struct request_queue *q, char *page)
{
	return sprintf(page, "%llu\n",
		(unsigned long long)q->limits.max_write_zeroes_sectors << 9);
}

static ssize_t queue_zone_write_granularity_show(struct request_queue *q,
						 char *page)
{
	return queue_var_show(queue_zone_write_granularity(q), page);
}

static ssize_t queue_zone_append_max_show(struct request_queue *q, char *page)
{
	unsigned long long max_sectors = queue_max_zone_append_sectors(q);

	return sprintf(page, "%llu\n", max_sectors << SECTOR_SHIFT);
}

static ssize_t
queue_max_sectors_store(struct request_queue *q, const char *page, size_t count)
{
	unsigned long max_sectors_kb;
	struct queue_limits lim;
	ssize_t ret;
	int err;

	ret = queue_var_store(&max_sectors_kb, page, count);
	if (ret < 0)
		return ret;

	blk_mq_freeze_queue(q);
	lim = queue_limits_start_update(q);
	lim.max_user_sectors = max_sectors_kb << 1;
	err = queue_limits_commit_update(q, &lim);
	blk_mq_unfreeze_queue(q);
	if (err)
		return err;
	return ret;
}

static ssize_t queue_max_hw_sectors_show(struct request_queue *q, char *page)
{
	int max_hw_sectors_kb = queue_max_hw_sectors(q) >> 1;

	return queue_var_show(max_hw_sectors_kb, page);
}

static ssize_t queue_virt_boundary_mask_show(struct request_queue *q, char *page)
{
	return queue_var_show(q->limits.virt_boundary_mask, page);
}

static ssize_t queue_dma_alignment_show(struct request_queue *q, char *page)
{
	return queue_var_show(queue_dma_alignment(q), page);
}

#define QUEUE_SYSFS_BIT_FNS(name, flag, neg)				\
static ssize_t								\
queue_##name##_show(struct request_queue *q, char *page)		\
{									\
	int bit;							\
	bit = test_bit(QUEUE_FLAG_##flag, &q->queue_flags);		\
	return queue_var_show(neg ? !bit : bit, page);			\
}									\
static ssize_t								\
queue_##name##_store(struct request_queue *q, const char *page, size_t count) \
{									\
	unsigned long val;						\
	ssize_t ret;							\
	ret = queue_var_store(&val, page, count);			\
	if (ret < 0)							\
		 return ret;						\
	if (neg)							\
		val = !val;						\
									\
	if (val)							\
		blk_queue_flag_set(QUEUE_FLAG_##flag, q);		\
	else								\
		blk_queue_flag_clear(QUEUE_FLAG_##flag, q);		\
	return ret;							\
}

QUEUE_SYSFS_BIT_FNS(nonrot, NONROT, 1);
QUEUE_SYSFS_BIT_FNS(random, ADD_RANDOM, 0);
QUEUE_SYSFS_BIT_FNS(iostats, IO_STAT, 0);
QUEUE_SYSFS_BIT_FNS(stable_writes, STABLE_WRITES, 0);
#undef QUEUE_SYSFS_BIT_FNS

static ssize_t queue_zoned_show(struct request_queue *q, char *page)
{
	if (blk_queue_is_zoned(q))
		return sprintf(page, "host-managed\n");
	return sprintf(page, "none\n");
}

static ssize_t queue_nr_zones_show(struct request_queue *q, char *page)
{
	return queue_var_show(disk_nr_zones(q->disk), page);
}

static ssize_t queue_max_open_zones_show(struct request_queue *q, char *page)
{
	return queue_var_show(bdev_max_open_zones(q->disk->part0), page);
}

static ssize_t queue_max_active_zones_show(struct request_queue *q, char *page)
{
	return queue_var_show(bdev_max_active_zones(q->disk->part0), page);
}

static ssize_t queue_nomerges_show(struct request_queue *q, char *page)
{
	return queue_var_show((blk_queue_nomerges(q) << 1) |
			       blk_queue_noxmerges(q), page);
}

static ssize_t queue_nomerges_store(struct request_queue *q, const char *page,
				    size_t count)
{
	unsigned long nm;
	ssize_t ret = queue_var_store(&nm, page, count);

	if (ret < 0)
		return ret;

	blk_queue_flag_clear(QUEUE_FLAG_NOMERGES, q);
	blk_queue_flag_clear(QUEUE_FLAG_NOXMERGES, q);
	if (nm == 2)
		blk_queue_flag_set(QUEUE_FLAG_NOMERGES, q);
	else if (nm)
		blk_queue_flag_set(QUEUE_FLAG_NOXMERGES, q);

	return ret;
}

static ssize_t queue_rq_affinity_show(struct request_queue *q, char *page)
{
	bool set = test_bit(QUEUE_FLAG_SAME_COMP, &q->queue_flags);
	bool force = test_bit(QUEUE_FLAG_SAME_FORCE, &q->queue_flags);

	return queue_var_show(set << force, page);
}

static ssize_t
queue_rq_affinity_store(struct request_queue *q, const char *page, size_t count)
{
	ssize_t ret = -EINVAL;
#ifdef CONFIG_SMP
	unsigned long val;

	ret = queue_var_store(&val, page, count);
	if (ret < 0)
		return ret;

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
#endif
	return ret;
}

static ssize_t queue_poll_delay_show(struct request_queue *q, char *page)
{
	return sprintf(page, "%d\n", -1);
}

static ssize_t queue_poll_delay_store(struct request_queue *q, const char *page,
				size_t count)
{
	return count;
}

static ssize_t queue_poll_show(struct request_queue *q, char *page)
{
	return queue_var_show(test_bit(QUEUE_FLAG_POLL, &q->queue_flags), page);
}

static ssize_t queue_poll_store(struct request_queue *q, const char *page,
				size_t count)
{
	if (!test_bit(QUEUE_FLAG_POLL, &q->queue_flags))
		return -EINVAL;
	pr_info_ratelimited("writes to the poll attribute are ignored.\n");
	pr_info_ratelimited("please use driver specific parameters instead.\n");
	return count;
}

static ssize_t queue_io_timeout_show(struct request_queue *q, char *page)
{
	return sprintf(page, "%u\n", jiffies_to_msecs(q->rq_timeout));
}

static ssize_t queue_io_timeout_store(struct request_queue *q, const char *page,
				  size_t count)
{
	unsigned int val;
	int err;

	err = kstrtou32(page, 10, &val);
	if (err || val == 0)
		return -EINVAL;

	blk_queue_rq_timeout(q, msecs_to_jiffies(val));

	return count;
}

static ssize_t queue_wc_show(struct request_queue *q, char *page)
{
	if (test_bit(QUEUE_FLAG_WC, &q->queue_flags))
		return sprintf(page, "write back\n");

	return sprintf(page, "write through\n");
}

static ssize_t queue_wc_store(struct request_queue *q, const char *page,
			      size_t count)
{
	if (!strncmp(page, "write back", 10)) {
		if (!test_bit(QUEUE_FLAG_HW_WC, &q->queue_flags))
			return -EINVAL;
		blk_queue_flag_set(QUEUE_FLAG_WC, q);
	} else if (!strncmp(page, "write through", 13) ||
		 !strncmp(page, "none", 4)) {
		blk_queue_flag_clear(QUEUE_FLAG_WC, q);
	} else {
		return -EINVAL;
	}

	return count;
}

static ssize_t queue_fua_show(struct request_queue *q, char *page)
{
	return sprintf(page, "%u\n", test_bit(QUEUE_FLAG_FUA, &q->queue_flags));
}

static ssize_t queue_dax_show(struct request_queue *q, char *page)
{
	return queue_var_show(blk_queue_dax(q), page);
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

QUEUE_RW_ENTRY(queue_requests, "nr_requests");
QUEUE_RW_ENTRY(queue_ra, "read_ahead_kb");
QUEUE_RW_ENTRY(queue_max_sectors, "max_sectors_kb");
QUEUE_RO_ENTRY(queue_max_hw_sectors, "max_hw_sectors_kb");
QUEUE_RO_ENTRY(queue_max_segments, "max_segments");
QUEUE_RO_ENTRY(queue_max_integrity_segments, "max_integrity_segments");
QUEUE_RO_ENTRY(queue_max_segment_size, "max_segment_size");
QUEUE_RW_ENTRY(elv_iosched, "scheduler");

QUEUE_RO_ENTRY(queue_logical_block_size, "logical_block_size");
QUEUE_RO_ENTRY(queue_physical_block_size, "physical_block_size");
QUEUE_RO_ENTRY(queue_chunk_sectors, "chunk_sectors");
QUEUE_RO_ENTRY(queue_io_min, "minimum_io_size");
QUEUE_RO_ENTRY(queue_io_opt, "optimal_io_size");

QUEUE_RO_ENTRY(queue_max_discard_segments, "max_discard_segments");
QUEUE_RO_ENTRY(queue_discard_granularity, "discard_granularity");
QUEUE_RO_ENTRY(queue_discard_max_hw, "discard_max_hw_bytes");
QUEUE_RW_ENTRY(queue_discard_max, "discard_max_bytes");
QUEUE_RO_ENTRY(queue_discard_zeroes_data, "discard_zeroes_data");

QUEUE_RO_ENTRY(queue_write_same_max, "write_same_max_bytes");
QUEUE_RO_ENTRY(queue_write_zeroes_max, "write_zeroes_max_bytes");
QUEUE_RO_ENTRY(queue_zone_append_max, "zone_append_max_bytes");
QUEUE_RO_ENTRY(queue_zone_write_granularity, "zone_write_granularity");

QUEUE_RO_ENTRY(queue_zoned, "zoned");
QUEUE_RO_ENTRY(queue_nr_zones, "nr_zones");
QUEUE_RO_ENTRY(queue_max_open_zones, "max_open_zones");
QUEUE_RO_ENTRY(queue_max_active_zones, "max_active_zones");

QUEUE_RW_ENTRY(queue_nomerges, "nomerges");
QUEUE_RW_ENTRY(queue_rq_affinity, "rq_affinity");
QUEUE_RW_ENTRY(queue_poll, "io_poll");
QUEUE_RW_ENTRY(queue_poll_delay, "io_poll_delay");
QUEUE_RW_ENTRY(queue_wc, "write_cache");
QUEUE_RO_ENTRY(queue_fua, "fua");
QUEUE_RO_ENTRY(queue_dax, "dax");
QUEUE_RW_ENTRY(queue_io_timeout, "io_timeout");
QUEUE_RO_ENTRY(queue_virt_boundary_mask, "virt_boundary_mask");
QUEUE_RO_ENTRY(queue_dma_alignment, "dma_alignment");

/* legacy alias for logical_block_size: */
static struct queue_sysfs_entry queue_hw_sector_size_entry = {
	.attr = {.name = "hw_sector_size", .mode = 0444 },
	.show = queue_logical_block_size_show,
};

QUEUE_RW_ENTRY(queue_nonrot, "rotational");
QUEUE_RW_ENTRY(queue_iostats, "iostats");
QUEUE_RW_ENTRY(queue_random, "add_random");
QUEUE_RW_ENTRY(queue_stable_writes, "stable_writes");

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

static ssize_t queue_wb_lat_show(struct request_queue *q, char *page)
{
	if (!wbt_rq_qos(q))
		return -EINVAL;

	if (wbt_disabled(q))
		return sprintf(page, "0\n");

	return sprintf(page, "%llu\n", div_u64(wbt_get_min_lat(q), 1000));
}

static ssize_t queue_wb_lat_store(struct request_queue *q, const char *page,
				  size_t count)
{
	struct rq_qos *rqos;
	ssize_t ret;
	s64 val;

	ret = queue_var_store64(&val, page);
	if (ret < 0)
		return ret;
	if (val < -1)
		return -EINVAL;

	rqos = wbt_rq_qos(q);
	if (!rqos) {
		ret = wbt_init(q->disk);
		if (ret)
			return ret;
	}

	if (val == -1)
		val = wbt_default_latency_nsec(q);
	else if (val >= 0)
		val *= 1000ULL;

	if (wbt_get_min_lat(q) == val)
		return count;

	/*
	 * Ensure that the queue is idled, in case the latency update
	 * ends up either enabling or disabling wbt completely. We can't
	 * have IO inflight if that happens.
	 */
	blk_mq_freeze_queue(q);
	blk_mq_quiesce_queue(q);

	wbt_set_min_lat(q, val);

	blk_mq_unquiesce_queue(q);
	blk_mq_unfreeze_queue(q);

	return count;
}

QUEUE_RW_ENTRY(queue_wb_lat, "wbt_lat_usec");
#endif

/* Common attributes for bio-based and request-based queues. */
static struct attribute *queue_attrs[] = {
	&queue_ra_entry.attr,
	&queue_max_hw_sectors_entry.attr,
	&queue_max_sectors_entry.attr,
	&queue_max_segments_entry.attr,
	&queue_max_discard_segments_entry.attr,
	&queue_max_integrity_segments_entry.attr,
	&queue_max_segment_size_entry.attr,
	&queue_hw_sector_size_entry.attr,
	&queue_logical_block_size_entry.attr,
	&queue_physical_block_size_entry.attr,
	&queue_chunk_sectors_entry.attr,
	&queue_io_min_entry.attr,
	&queue_io_opt_entry.attr,
	&queue_discard_granularity_entry.attr,
	&queue_discard_max_entry.attr,
	&queue_discard_max_hw_entry.attr,
	&queue_discard_zeroes_data_entry.attr,
	&queue_write_same_max_entry.attr,
	&queue_write_zeroes_max_entry.attr,
	&queue_zone_append_max_entry.attr,
	&queue_zone_write_granularity_entry.attr,
	&queue_nonrot_entry.attr,
	&queue_zoned_entry.attr,
	&queue_nr_zones_entry.attr,
	&queue_max_open_zones_entry.attr,
	&queue_max_active_zones_entry.attr,
	&queue_nomerges_entry.attr,
	&queue_iostats_entry.attr,
	&queue_stable_writes_entry.attr,
	&queue_random_entry.attr,
	&queue_poll_entry.attr,
	&queue_wc_entry.attr,
	&queue_fua_entry.attr,
	&queue_dax_entry.attr,
	&queue_poll_delay_entry.attr,
	&queue_virt_boundary_mask_entry.attr,
	&queue_dma_alignment_entry.attr,
	NULL,
};

/* Request-based queue attributes that are not relevant for bio-based queues. */
static struct attribute *blk_mq_queue_attrs[] = {
	&queue_requests_entry.attr,
	&elv_iosched_entry.attr,
	&queue_rq_affinity_entry.attr,
	&queue_io_timeout_entry.attr,
#ifdef CONFIG_BLK_WBT
	&queue_wb_lat_entry.attr,
#endif
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
	struct request_queue *q = disk->queue;
	ssize_t res;

	if (!entry->show)
		return -EIO;
	mutex_lock(&q->sysfs_lock);
	res = entry->show(q, page);
	mutex_unlock(&q->sysfs_lock);
	return res;
}

static ssize_t
queue_attr_store(struct kobject *kobj, struct attribute *attr,
		    const char *page, size_t length)
{
	struct queue_sysfs_entry *entry = to_queue(attr);
	struct gendisk *disk = container_of(kobj, struct gendisk, queue_kobj);
	struct request_queue *q = disk->queue;
	ssize_t res;

	if (!entry->store)
		return -EIO;

	mutex_lock(&q->sysfs_lock);
	res = entry->store(q, page, length);
	mutex_unlock(&q->sysfs_lock);
	return res;
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

static const struct kobj_type blk_queue_ktype = {
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

	mutex_lock(&q->sysfs_dir_lock);
	kobject_init(&disk->queue_kobj, &blk_queue_ktype);
	ret = kobject_add(&disk->queue_kobj, &disk_to_dev(disk)->kobj, "queue");
	if (ret < 0)
		goto out_put_queue_kobj;

	if (queue_is_mq(q)) {
		ret = blk_mq_sysfs_register(disk);
		if (ret)
			goto out_put_queue_kobj;
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

	if (q->elevator) {
		ret = elv_register_queue(q, false);
		if (ret)
			goto out_unregister_ia_ranges;
	}

	ret = blk_crypto_sysfs_register(disk);
	if (ret)
		goto out_elv_unregister;

	blk_queue_flag_set(QUEUE_FLAG_REGISTERED, q);
	wbt_enable_default(disk);

	/* Now everything is ready and send out KOBJ_ADD uevent */
	kobject_uevent(&disk->queue_kobj, KOBJ_ADD);
	if (q->elevator)
		kobject_uevent(&q->elevator->kobj, KOBJ_ADD);
	mutex_unlock(&q->sysfs_lock);
	mutex_unlock(&q->sysfs_dir_lock);

	/*
	 * SCSI probing may synchronously create and destroy a lot of
	 * request_queues for non-existent devices.  Shutting down a fully
	 * functional queue takes measureable wallclock time as RCU grace
	 * periods are involved.  To avoid excessive latency in these
	 * cases, a request_queue starts out in a degraded mode which is
	 * faster to shut down and is made fully functional here as
	 * request_queues for non-existent devices never get registered.
	 */
	if (!blk_queue_init_done(q)) {
		blk_queue_flag_set(QUEUE_FLAG_INIT_DONE, q);
		percpu_ref_switch_to_percpu(&q->q_usage_counter);
	}

	return ret;

out_elv_unregister:
	elv_unregister_queue(q);
out_unregister_ia_ranges:
	disk_unregister_independent_access_ranges(disk);
out_debugfs_remove:
	blk_debugfs_remove(disk);
	mutex_unlock(&q->sysfs_lock);
out_put_queue_kobj:
	kobject_put(&disk->queue_kobj);
	mutex_unlock(&q->sysfs_dir_lock);
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

	mutex_lock(&q->sysfs_dir_lock);
	/*
	 * Remove the sysfs attributes before unregistering the queue data
	 * structures that can be modified through sysfs.
	 */
	if (queue_is_mq(q))
		blk_mq_sysfs_unregister(disk);
	blk_crypto_sysfs_unregister(disk);

	mutex_lock(&q->sysfs_lock);
	elv_unregister_queue(q);
	disk_unregister_independent_access_ranges(disk);
	mutex_unlock(&q->sysfs_lock);

	/* Now that we've deleted all child objects, we can delete the queue. */
	kobject_uevent(&disk->queue_kobj, KOBJ_REMOVE);
	kobject_del(&disk->queue_kobj);
	mutex_unlock(&q->sysfs_dir_lock);

	blk_debugfs_remove(disk);
}
