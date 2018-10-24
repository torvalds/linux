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
#include <linux/blk-mq.h>
#include <linux/blk-cgroup.h>

#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-debugfs.h"
#include "blk-wbt.h"

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

static ssize_t queue_requests_show(struct request_queue *q, char *page)
{
	return queue_var_show(q->nr_requests, (page));
}

static ssize_t
queue_requests_store(struct request_queue *q, const char *page, size_t count)
{
	unsigned long nr;
	int ret, err;

	if (!q->request_fn && !q->mq_ops)
		return -EINVAL;

	ret = queue_var_store(&nr, page, count);
	if (ret < 0)
		return ret;

	if (nr < BLKDEV_MIN_RQ)
		nr = BLKDEV_MIN_RQ;

	if (q->request_fn)
		err = blk_update_nr_requests(q, nr);
	else
		err = blk_mq_update_nr_requests(q, nr);

	if (err)
		return err;

	return ret;
}

static ssize_t queue_ra_show(struct request_queue *q, char *page)
{
	unsigned long ra_kb = q->backing_dev_info->ra_pages <<
					(PAGE_SHIFT - 10);

	return queue_var_show(ra_kb, (page));
}

static ssize_t
queue_ra_store(struct request_queue *q, const char *page, size_t count)
{
	unsigned long ra_kb;
	ssize_t ret = queue_var_store(&ra_kb, page, count);

	if (ret < 0)
		return ret;

	q->backing_dev_info->ra_pages = ra_kb >> (PAGE_SHIFT - 10);

	return ret;
}

static ssize_t queue_max_sectors_show(struct request_queue *q, char *page)
{
	int max_sectors_kb = queue_max_sectors(q) >> 1;

	return queue_var_show(max_sectors_kb, (page));
}

static ssize_t queue_max_segments_show(struct request_queue *q, char *page)
{
	return queue_var_show(queue_max_segments(q), (page));
}

static ssize_t queue_max_discard_segments_show(struct request_queue *q,
		char *page)
{
	return queue_var_show(queue_max_discard_segments(q), (page));
}

static ssize_t queue_max_integrity_segments_show(struct request_queue *q, char *page)
{
	return queue_var_show(q->limits.max_integrity_segments, (page));
}

static ssize_t queue_max_segment_size_show(struct request_queue *q, char *page)
{
	if (blk_queue_cluster(q))
		return queue_var_show(queue_max_segment_size(q), (page));

	return queue_var_show(PAGE_SIZE, (page));
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
	unsigned long max_discard;
	ssize_t ret = queue_var_store(&max_discard, page, count);

	if (ret < 0)
		return ret;

	if (max_discard & (q->limits.discard_granularity - 1))
		return -EINVAL;

	max_discard >>= 9;
	if (max_discard > UINT_MAX)
		return -EINVAL;

	if (max_discard > q->limits.max_hw_discard_sectors)
		max_discard = q->limits.max_hw_discard_sectors;

	q->limits.max_discard_sectors = max_discard;
	return ret;
}

static ssize_t queue_discard_zeroes_data_show(struct request_queue *q, char *page)
{
	return queue_var_show(0, page);
}

static ssize_t queue_write_same_max_show(struct request_queue *q, char *page)
{
	return sprintf(page, "%llu\n",
		(unsigned long long)q->limits.max_write_same_sectors << 9);
}

static ssize_t queue_write_zeroes_max_show(struct request_queue *q, char *page)
{
	return sprintf(page, "%llu\n",
		(unsigned long long)q->limits.max_write_zeroes_sectors << 9);
}

static ssize_t
queue_max_sectors_store(struct request_queue *q, const char *page, size_t count)
{
	unsigned long max_sectors_kb,
		max_hw_sectors_kb = queue_max_hw_sectors(q) >> 1,
			page_kb = 1 << (PAGE_SHIFT - 10);
	ssize_t ret = queue_var_store(&max_sectors_kb, page, count);

	if (ret < 0)
		return ret;

	max_hw_sectors_kb = min_not_zero(max_hw_sectors_kb, (unsigned long)
					 q->limits.max_dev_sectors >> 1);

	if (max_sectors_kb > max_hw_sectors_kb || max_sectors_kb < page_kb)
		return -EINVAL;

	spin_lock_irq(q->queue_lock);
	q->limits.max_sectors = max_sectors_kb << 1;
	q->backing_dev_info->io_pages = max_sectors_kb >> (PAGE_SHIFT - 10);
	spin_unlock_irq(q->queue_lock);

	return ret;
}

static ssize_t queue_max_hw_sectors_show(struct request_queue *q, char *page)
{
	int max_hw_sectors_kb = queue_max_hw_sectors(q) >> 1;

	return queue_var_show(max_hw_sectors_kb, (page));
}

#define QUEUE_SYSFS_BIT_FNS(name, flag, neg)				\
static ssize_t								\
queue_show_##name(struct request_queue *q, char *page)			\
{									\
	int bit;							\
	bit = test_bit(QUEUE_FLAG_##flag, &q->queue_flags);		\
	return queue_var_show(neg ? !bit : bit, page);			\
}									\
static ssize_t								\
queue_store_##name(struct request_queue *q, const char *page, size_t count) \
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
#undef QUEUE_SYSFS_BIT_FNS

static ssize_t queue_zoned_show(struct request_queue *q, char *page)
{
	switch (blk_queue_zoned_model(q)) {
	case BLK_ZONED_HA:
		return sprintf(page, "host-aware\n");
	case BLK_ZONED_HM:
		return sprintf(page, "host-managed\n");
	default:
		return sprintf(page, "none\n");
	}
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

	spin_lock_irq(q->queue_lock);
	queue_flag_clear(QUEUE_FLAG_NOMERGES, q);
	queue_flag_clear(QUEUE_FLAG_NOXMERGES, q);
	if (nm == 2)
		queue_flag_set(QUEUE_FLAG_NOMERGES, q);
	else if (nm)
		queue_flag_set(QUEUE_FLAG_NOXMERGES, q);
	spin_unlock_irq(q->queue_lock);

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

	spin_lock_irq(q->queue_lock);
	if (val == 2) {
		queue_flag_set(QUEUE_FLAG_SAME_COMP, q);
		queue_flag_set(QUEUE_FLAG_SAME_FORCE, q);
	} else if (val == 1) {
		queue_flag_set(QUEUE_FLAG_SAME_COMP, q);
		queue_flag_clear(QUEUE_FLAG_SAME_FORCE, q);
	} else if (val == 0) {
		queue_flag_clear(QUEUE_FLAG_SAME_COMP, q);
		queue_flag_clear(QUEUE_FLAG_SAME_FORCE, q);
	}
	spin_unlock_irq(q->queue_lock);
#endif
	return ret;
}

static ssize_t queue_poll_delay_show(struct request_queue *q, char *page)
{
	int val;

	if (q->poll_nsec == -1)
		val = -1;
	else
		val = q->poll_nsec / 1000;

	return sprintf(page, "%d\n", val);
}

static ssize_t queue_poll_delay_store(struct request_queue *q, const char *page,
				size_t count)
{
	int err, val;

	if (!q->mq_ops || !q->mq_ops->poll)
		return -EINVAL;

	err = kstrtoint(page, 10, &val);
	if (err < 0)
		return err;

	if (val == -1)
		q->poll_nsec = -1;
	else
		q->poll_nsec = val * 1000;

	return count;
}

static ssize_t queue_poll_show(struct request_queue *q, char *page)
{
	return queue_var_show(test_bit(QUEUE_FLAG_POLL, &q->queue_flags), page);
}

static ssize_t queue_poll_store(struct request_queue *q, const char *page,
				size_t count)
{
	unsigned long poll_on;
	ssize_t ret;

	if (!q->mq_ops || !q->mq_ops->poll)
		return -EINVAL;

	ret = queue_var_store(&poll_on, page, count);
	if (ret < 0)
		return ret;

	if (poll_on)
		blk_queue_flag_set(QUEUE_FLAG_POLL, q);
	else
		blk_queue_flag_clear(QUEUE_FLAG_POLL, q);

	return ret;
}

static ssize_t queue_wb_lat_show(struct request_queue *q, char *page)
{
	if (!wbt_rq_qos(q))
		return -EINVAL;

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
		ret = wbt_init(q);
		if (ret)
			return ret;
	}

	if (val == -1)
		val = wbt_default_latency_nsec(q);
	else if (val >= 0)
		val *= 1000ULL;

	/*
	 * Ensure that the queue is idled, in case the latency update
	 * ends up either enabling or disabling wbt completely. We can't
	 * have IO inflight if that happens.
	 */
	if (q->mq_ops) {
		blk_mq_freeze_queue(q);
		blk_mq_quiesce_queue(q);
	} else
		blk_queue_bypass_start(q);

	wbt_set_min_lat(q, val);
	wbt_update_limits(q);

	if (q->mq_ops) {
		blk_mq_unquiesce_queue(q);
		blk_mq_unfreeze_queue(q);
	} else
		blk_queue_bypass_end(q);

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
	int set = -1;

	if (!strncmp(page, "write back", 10))
		set = 1;
	else if (!strncmp(page, "write through", 13) ||
		 !strncmp(page, "none", 4))
		set = 0;

	if (set == -1)
		return -EINVAL;

	if (set)
		blk_queue_flag_set(QUEUE_FLAG_WC, q);
	else
		blk_queue_flag_clear(QUEUE_FLAG_WC, q);

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

static struct queue_sysfs_entry queue_requests_entry = {
	.attr = {.name = "nr_requests", .mode = 0644 },
	.show = queue_requests_show,
	.store = queue_requests_store,
};

static struct queue_sysfs_entry queue_ra_entry = {
	.attr = {.name = "read_ahead_kb", .mode = 0644 },
	.show = queue_ra_show,
	.store = queue_ra_store,
};

static struct queue_sysfs_entry queue_max_sectors_entry = {
	.attr = {.name = "max_sectors_kb", .mode = 0644 },
	.show = queue_max_sectors_show,
	.store = queue_max_sectors_store,
};

static struct queue_sysfs_entry queue_max_hw_sectors_entry = {
	.attr = {.name = "max_hw_sectors_kb", .mode = 0444 },
	.show = queue_max_hw_sectors_show,
};

static struct queue_sysfs_entry queue_max_segments_entry = {
	.attr = {.name = "max_segments", .mode = 0444 },
	.show = queue_max_segments_show,
};

static struct queue_sysfs_entry queue_max_discard_segments_entry = {
	.attr = {.name = "max_discard_segments", .mode = 0444 },
	.show = queue_max_discard_segments_show,
};

static struct queue_sysfs_entry queue_max_integrity_segments_entry = {
	.attr = {.name = "max_integrity_segments", .mode = 0444 },
	.show = queue_max_integrity_segments_show,
};

static struct queue_sysfs_entry queue_max_segment_size_entry = {
	.attr = {.name = "max_segment_size", .mode = 0444 },
	.show = queue_max_segment_size_show,
};

static struct queue_sysfs_entry queue_iosched_entry = {
	.attr = {.name = "scheduler", .mode = 0644 },
	.show = elv_iosched_show,
	.store = elv_iosched_store,
};

static struct queue_sysfs_entry queue_hw_sector_size_entry = {
	.attr = {.name = "hw_sector_size", .mode = 0444 },
	.show = queue_logical_block_size_show,
};

static struct queue_sysfs_entry queue_logical_block_size_entry = {
	.attr = {.name = "logical_block_size", .mode = 0444 },
	.show = queue_logical_block_size_show,
};

static struct queue_sysfs_entry queue_physical_block_size_entry = {
	.attr = {.name = "physical_block_size", .mode = 0444 },
	.show = queue_physical_block_size_show,
};

static struct queue_sysfs_entry queue_chunk_sectors_entry = {
	.attr = {.name = "chunk_sectors", .mode = 0444 },
	.show = queue_chunk_sectors_show,
};

static struct queue_sysfs_entry queue_io_min_entry = {
	.attr = {.name = "minimum_io_size", .mode = 0444 },
	.show = queue_io_min_show,
};

static struct queue_sysfs_entry queue_io_opt_entry = {
	.attr = {.name = "optimal_io_size", .mode = 0444 },
	.show = queue_io_opt_show,
};

static struct queue_sysfs_entry queue_discard_granularity_entry = {
	.attr = {.name = "discard_granularity", .mode = 0444 },
	.show = queue_discard_granularity_show,
};

static struct queue_sysfs_entry queue_discard_max_hw_entry = {
	.attr = {.name = "discard_max_hw_bytes", .mode = 0444 },
	.show = queue_discard_max_hw_show,
};

static struct queue_sysfs_entry queue_discard_max_entry = {
	.attr = {.name = "discard_max_bytes", .mode = 0644 },
	.show = queue_discard_max_show,
	.store = queue_discard_max_store,
};

static struct queue_sysfs_entry queue_discard_zeroes_data_entry = {
	.attr = {.name = "discard_zeroes_data", .mode = 0444 },
	.show = queue_discard_zeroes_data_show,
};

static struct queue_sysfs_entry queue_write_same_max_entry = {
	.attr = {.name = "write_same_max_bytes", .mode = 0444 },
	.show = queue_write_same_max_show,
};

static struct queue_sysfs_entry queue_write_zeroes_max_entry = {
	.attr = {.name = "write_zeroes_max_bytes", .mode = 0444 },
	.show = queue_write_zeroes_max_show,
};

static struct queue_sysfs_entry queue_nonrot_entry = {
	.attr = {.name = "rotational", .mode = 0644 },
	.show = queue_show_nonrot,
	.store = queue_store_nonrot,
};

static struct queue_sysfs_entry queue_zoned_entry = {
	.attr = {.name = "zoned", .mode = 0444 },
	.show = queue_zoned_show,
};

static struct queue_sysfs_entry queue_nomerges_entry = {
	.attr = {.name = "nomerges", .mode = 0644 },
	.show = queue_nomerges_show,
	.store = queue_nomerges_store,
};

static struct queue_sysfs_entry queue_rq_affinity_entry = {
	.attr = {.name = "rq_affinity", .mode = 0644 },
	.show = queue_rq_affinity_show,
	.store = queue_rq_affinity_store,
};

static struct queue_sysfs_entry queue_iostats_entry = {
	.attr = {.name = "iostats", .mode = 0644 },
	.show = queue_show_iostats,
	.store = queue_store_iostats,
};

static struct queue_sysfs_entry queue_random_entry = {
	.attr = {.name = "add_random", .mode = 0644 },
	.show = queue_show_random,
	.store = queue_store_random,
};

static struct queue_sysfs_entry queue_poll_entry = {
	.attr = {.name = "io_poll", .mode = 0644 },
	.show = queue_poll_show,
	.store = queue_poll_store,
};

static struct queue_sysfs_entry queue_poll_delay_entry = {
	.attr = {.name = "io_poll_delay", .mode = 0644 },
	.show = queue_poll_delay_show,
	.store = queue_poll_delay_store,
};

static struct queue_sysfs_entry queue_wc_entry = {
	.attr = {.name = "write_cache", .mode = 0644 },
	.show = queue_wc_show,
	.store = queue_wc_store,
};

static struct queue_sysfs_entry queue_fua_entry = {
	.attr = {.name = "fua", .mode = 0444 },
	.show = queue_fua_show,
};

static struct queue_sysfs_entry queue_dax_entry = {
	.attr = {.name = "dax", .mode = 0444 },
	.show = queue_dax_show,
};

static struct queue_sysfs_entry queue_wb_lat_entry = {
	.attr = {.name = "wbt_lat_usec", .mode = 0644 },
	.show = queue_wb_lat_show,
	.store = queue_wb_lat_store,
};

#ifdef CONFIG_BLK_DEV_THROTTLING_LOW
static struct queue_sysfs_entry throtl_sample_time_entry = {
	.attr = {.name = "throttle_sample_time", .mode = 0644 },
	.show = blk_throtl_sample_time_show,
	.store = blk_throtl_sample_time_store,
};
#endif

static struct attribute *default_attrs[] = {
	&queue_requests_entry.attr,
	&queue_ra_entry.attr,
	&queue_max_hw_sectors_entry.attr,
	&queue_max_sectors_entry.attr,
	&queue_max_segments_entry.attr,
	&queue_max_discard_segments_entry.attr,
	&queue_max_integrity_segments_entry.attr,
	&queue_max_segment_size_entry.attr,
	&queue_iosched_entry.attr,
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
	&queue_nonrot_entry.attr,
	&queue_zoned_entry.attr,
	&queue_nomerges_entry.attr,
	&queue_rq_affinity_entry.attr,
	&queue_iostats_entry.attr,
	&queue_random_entry.attr,
	&queue_poll_entry.attr,
	&queue_wc_entry.attr,
	&queue_fua_entry.attr,
	&queue_dax_entry.attr,
	&queue_wb_lat_entry.attr,
	&queue_poll_delay_entry.attr,
#ifdef CONFIG_BLK_DEV_THROTTLING_LOW
	&throtl_sample_time_entry.attr,
#endif
	NULL,
};

#define to_queue(atr) container_of((atr), struct queue_sysfs_entry, attr)

static ssize_t
queue_attr_show(struct kobject *kobj, struct attribute *attr, char *page)
{
	struct queue_sysfs_entry *entry = to_queue(attr);
	struct request_queue *q =
		container_of(kobj, struct request_queue, kobj);
	ssize_t res;

	if (!entry->show)
		return -EIO;
	mutex_lock(&q->sysfs_lock);
	if (blk_queue_dying(q)) {
		mutex_unlock(&q->sysfs_lock);
		return -ENOENT;
	}
	res = entry->show(q, page);
	mutex_unlock(&q->sysfs_lock);
	return res;
}

static ssize_t
queue_attr_store(struct kobject *kobj, struct attribute *attr,
		    const char *page, size_t length)
{
	struct queue_sysfs_entry *entry = to_queue(attr);
	struct request_queue *q;
	ssize_t res;

	if (!entry->store)
		return -EIO;

	q = container_of(kobj, struct request_queue, kobj);
	mutex_lock(&q->sysfs_lock);
	if (blk_queue_dying(q)) {
		mutex_unlock(&q->sysfs_lock);
		return -ENOENT;
	}
	res = entry->store(q, page, length);
	mutex_unlock(&q->sysfs_lock);
	return res;
}

static void blk_free_queue_rcu(struct rcu_head *rcu_head)
{
	struct request_queue *q = container_of(rcu_head, struct request_queue,
					       rcu_head);
	kmem_cache_free(blk_requestq_cachep, q);
}

/**
 * __blk_release_queue - release a request queue when it is no longer needed
 * @work: pointer to the release_work member of the request queue to be released
 *
 * Description:
 *     blk_release_queue is the counterpart of blk_init_queue(). It should be
 *     called when a request queue is being released; typically when a block
 *     device is being de-registered. Its primary task it to free the queue
 *     itself.
 *
 * Notes:
 *     The low level driver must have finished any outstanding requests first
 *     via blk_cleanup_queue().
 *
 *     Although blk_release_queue() may be called with preemption disabled,
 *     __blk_release_queue() may sleep.
 */
static void __blk_release_queue(struct work_struct *work)
{
	struct request_queue *q = container_of(work, typeof(*q), release_work);

	if (test_bit(QUEUE_FLAG_POLL_STATS, &q->queue_flags))
		blk_stat_remove_callback(q, q->poll_cb);
	blk_stat_free_callback(q->poll_cb);

	if (!blk_queue_dead(q)) {
		/*
		 * Last reference was dropped without having called
		 * blk_cleanup_queue().
		 */
		WARN_ONCE(blk_queue_init_done(q),
			  "request queue %p has been registered but blk_cleanup_queue() has not been called for that queue\n",
			  q);
		blk_exit_queue(q);
	}

	WARN(blk_queue_root_blkg(q),
	     "request queue %p is being released but it has not yet been removed from the blkcg controller\n",
	     q);

	blk_free_queue_stats(q->stats);

	if (q->mq_ops)
		cancel_delayed_work_sync(&q->requeue_work);

	blk_exit_rl(q, &q->root_rl);

	if (q->queue_tags)
		__blk_queue_free_tags(q);

	if (!q->mq_ops) {
		if (q->exit_rq_fn)
			q->exit_rq_fn(q, q->fq->flush_rq);
		blk_free_flush_queue(q->fq);
	} else {
		blk_mq_release(q);
	}

	blk_trace_shutdown(q);

	if (q->mq_ops)
		blk_mq_debugfs_unregister(q);

	bioset_exit(&q->bio_split);

	ida_simple_remove(&blk_queue_ida, q->id);
	call_rcu(&q->rcu_head, blk_free_queue_rcu);
}

static void blk_release_queue(struct kobject *kobj)
{
	struct request_queue *q =
		container_of(kobj, struct request_queue, kobj);

	INIT_WORK(&q->release_work, __blk_release_queue);
	schedule_work(&q->release_work);
}

static const struct sysfs_ops queue_sysfs_ops = {
	.show	= queue_attr_show,
	.store	= queue_attr_store,
};

struct kobj_type blk_queue_ktype = {
	.sysfs_ops	= &queue_sysfs_ops,
	.default_attrs	= default_attrs,
	.release	= blk_release_queue,
};

/**
 * blk_register_queue - register a block layer queue with sysfs
 * @disk: Disk of which the request queue should be registered with sysfs.
 */
int blk_register_queue(struct gendisk *disk)
{
	int ret;
	struct device *dev = disk_to_dev(disk);
	struct request_queue *q = disk->queue;

	if (WARN_ON(!q))
		return -ENXIO;

	WARN_ONCE(test_bit(QUEUE_FLAG_REGISTERED, &q->queue_flags),
		  "%s is registering an already registered queue\n",
		  kobject_name(&dev->kobj));
	queue_flag_set_unlocked(QUEUE_FLAG_REGISTERED, q);

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
		queue_flag_set_unlocked(QUEUE_FLAG_INIT_DONE, q);
		percpu_ref_switch_to_percpu(&q->q_usage_counter);
		blk_queue_bypass_end(q);
	}

	ret = blk_trace_init_sysfs(dev);
	if (ret)
		return ret;

	/* Prevent changes through sysfs until registration is completed. */
	mutex_lock(&q->sysfs_lock);

	ret = kobject_add(&q->kobj, kobject_get(&dev->kobj), "%s", "queue");
	if (ret < 0) {
		blk_trace_remove_sysfs(dev);
		goto unlock;
	}

	if (q->mq_ops) {
		__blk_mq_register_dev(dev, q);
		blk_mq_debugfs_register(q);
	}

	kobject_uevent(&q->kobj, KOBJ_ADD);

	wbt_enable_default(q);

	blk_throtl_register_queue(q);

	if (q->request_fn || (q->mq_ops && q->elevator)) {
		ret = elv_register_queue(q);
		if (ret) {
			mutex_unlock(&q->sysfs_lock);
			kobject_uevent(&q->kobj, KOBJ_REMOVE);
			kobject_del(&q->kobj);
			blk_trace_remove_sysfs(dev);
			kobject_put(&dev->kobj);
			return ret;
		}
	}
	ret = 0;
unlock:
	mutex_unlock(&q->sysfs_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(blk_register_queue);

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
	if (!test_bit(QUEUE_FLAG_REGISTERED, &q->queue_flags))
		return;

	/*
	 * Since sysfs_remove_dir() prevents adding new directory entries
	 * before removal of existing entries starts, protect against
	 * concurrent elv_iosched_store() calls.
	 */
	mutex_lock(&q->sysfs_lock);

	blk_queue_flag_clear(QUEUE_FLAG_REGISTERED, q);

	/*
	 * Remove the sysfs attributes before unregistering the queue data
	 * structures that can be modified through sysfs.
	 */
	if (q->mq_ops)
		blk_mq_unregister_dev(disk_to_dev(disk), q);
	mutex_unlock(&q->sysfs_lock);

	kobject_uevent(&q->kobj, KOBJ_REMOVE);
	kobject_del(&q->kobj);
	blk_trace_remove_sysfs(disk_to_dev(disk));

	mutex_lock(&q->sysfs_lock);
	if (q->request_fn || (q->mq_ops && q->elevator))
		elv_unregister_queue(q);
	mutex_unlock(&q->sysfs_lock);

	kobject_put(&disk_to_dev(disk)->kobj);
}
