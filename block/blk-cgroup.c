/*
 * Common Block IO controller cgroup interface
 *
 * Based on ideas and code from CFQ, CFS and BFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 *
 * Copyright (C) 2009 Vivek Goyal <vgoyal@redhat.com>
 * 	              Nauman Rafique <nauman@google.com>
 */
#include <linux/ioprio.h>
#include <linux/seq_file.h>
#include <linux/kdev_t.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/blkdev.h>
#include "blk-cgroup.h"

#define MAX_KEY_LEN 100

static DEFINE_SPINLOCK(blkio_list_lock);
static LIST_HEAD(blkio_list);

struct blkio_cgroup blkio_root_cgroup = { .weight = 2*BLKIO_WEIGHT_DEFAULT };
EXPORT_SYMBOL_GPL(blkio_root_cgroup);

static struct cgroup_subsys_state *blkiocg_create(struct cgroup_subsys *,
						  struct cgroup *);
static int blkiocg_can_attach(struct cgroup_subsys *, struct cgroup *,
			      struct task_struct *, bool);
static void blkiocg_attach(struct cgroup_subsys *, struct cgroup *,
			   struct cgroup *, struct task_struct *, bool);
static void blkiocg_destroy(struct cgroup_subsys *, struct cgroup *);
static int blkiocg_populate(struct cgroup_subsys *, struct cgroup *);

struct cgroup_subsys blkio_subsys = {
	.name = "blkio",
	.create = blkiocg_create,
	.can_attach = blkiocg_can_attach,
	.attach = blkiocg_attach,
	.destroy = blkiocg_destroy,
	.populate = blkiocg_populate,
#ifdef CONFIG_BLK_CGROUP
	/* note: blkio_subsys_id is otherwise defined in blk-cgroup.h */
	.subsys_id = blkio_subsys_id,
#endif
	.use_id = 1,
	.module = THIS_MODULE,
};
EXPORT_SYMBOL_GPL(blkio_subsys);

struct blkio_cgroup *cgroup_to_blkio_cgroup(struct cgroup *cgroup)
{
	return container_of(cgroup_subsys_state(cgroup, blkio_subsys_id),
			    struct blkio_cgroup, css);
}
EXPORT_SYMBOL_GPL(cgroup_to_blkio_cgroup);

void blkio_group_init(struct blkio_group *blkg)
{
	spin_lock_init(&blkg->stats_lock);
}
EXPORT_SYMBOL_GPL(blkio_group_init);

/*
 * Add to the appropriate stat variable depending on the request type.
 * This should be called with the blkg->stats_lock held.
 */
static void blkio_add_stat(uint64_t *stat, uint64_t add, bool direction,
				bool sync)
{
	if (direction)
		stat[BLKIO_STAT_WRITE] += add;
	else
		stat[BLKIO_STAT_READ] += add;
	if (sync)
		stat[BLKIO_STAT_SYNC] += add;
	else
		stat[BLKIO_STAT_ASYNC] += add;
}

/*
 * Decrements the appropriate stat variable if non-zero depending on the
 * request type. Panics on value being zero.
 * This should be called with the blkg->stats_lock held.
 */
static void blkio_check_and_dec_stat(uint64_t *stat, bool direction, bool sync)
{
	if (direction) {
		BUG_ON(stat[BLKIO_STAT_WRITE] == 0);
		stat[BLKIO_STAT_WRITE]--;
	} else {
		BUG_ON(stat[BLKIO_STAT_READ] == 0);
		stat[BLKIO_STAT_READ]--;
	}
	if (sync) {
		BUG_ON(stat[BLKIO_STAT_SYNC] == 0);
		stat[BLKIO_STAT_SYNC]--;
	} else {
		BUG_ON(stat[BLKIO_STAT_ASYNC] == 0);
		stat[BLKIO_STAT_ASYNC]--;
	}
}

#ifdef CONFIG_DEBUG_BLK_CGROUP
void blkiocg_update_set_active_queue_stats(struct blkio_group *blkg)
{
	unsigned long flags;
	struct blkio_group_stats *stats;

	spin_lock_irqsave(&blkg->stats_lock, flags);
	stats = &blkg->stats;
	stats->avg_queue_size_sum +=
			stats->stat_arr[BLKIO_STAT_QUEUED][BLKIO_STAT_READ] +
			stats->stat_arr[BLKIO_STAT_QUEUED][BLKIO_STAT_WRITE];
	stats->avg_queue_size_samples++;
	spin_unlock_irqrestore(&blkg->stats_lock, flags);
}
EXPORT_SYMBOL_GPL(blkiocg_update_set_active_queue_stats);
#endif

void blkiocg_update_request_add_stats(struct blkio_group *blkg,
			struct blkio_group *curr_blkg, bool direction,
			bool sync)
{
	unsigned long flags;

	spin_lock_irqsave(&blkg->stats_lock, flags);
	blkio_add_stat(blkg->stats.stat_arr[BLKIO_STAT_QUEUED], 1, direction,
			sync);
	spin_unlock_irqrestore(&blkg->stats_lock, flags);
}
EXPORT_SYMBOL_GPL(blkiocg_update_request_add_stats);

void blkiocg_update_request_remove_stats(struct blkio_group *blkg,
						bool direction, bool sync)
{
	unsigned long flags;

	spin_lock_irqsave(&blkg->stats_lock, flags);
	blkio_check_and_dec_stat(blkg->stats.stat_arr[BLKIO_STAT_QUEUED],
					direction, sync);
	spin_unlock_irqrestore(&blkg->stats_lock, flags);
}
EXPORT_SYMBOL_GPL(blkiocg_update_request_remove_stats);

void blkiocg_update_timeslice_used(struct blkio_group *blkg, unsigned long time)
{
	unsigned long flags;

	spin_lock_irqsave(&blkg->stats_lock, flags);
	blkg->stats.time += time;
	spin_unlock_irqrestore(&blkg->stats_lock, flags);
}
EXPORT_SYMBOL_GPL(blkiocg_update_timeslice_used);

void blkiocg_update_dispatch_stats(struct blkio_group *blkg,
				uint64_t bytes, bool direction, bool sync)
{
	struct blkio_group_stats *stats;
	unsigned long flags;

	spin_lock_irqsave(&blkg->stats_lock, flags);
	stats = &blkg->stats;
	stats->sectors += bytes >> 9;
	blkio_add_stat(stats->stat_arr[BLKIO_STAT_SERVICED], 1, direction,
			sync);
	blkio_add_stat(stats->stat_arr[BLKIO_STAT_SERVICE_BYTES], bytes,
			direction, sync);
	spin_unlock_irqrestore(&blkg->stats_lock, flags);
}
EXPORT_SYMBOL_GPL(blkiocg_update_dispatch_stats);

void blkiocg_update_completion_stats(struct blkio_group *blkg,
	uint64_t start_time, uint64_t io_start_time, bool direction, bool sync)
{
	struct blkio_group_stats *stats;
	unsigned long flags;
	unsigned long long now = sched_clock();

	spin_lock_irqsave(&blkg->stats_lock, flags);
	stats = &blkg->stats;
	if (time_after64(now, io_start_time))
		blkio_add_stat(stats->stat_arr[BLKIO_STAT_SERVICE_TIME],
				now - io_start_time, direction, sync);
	if (time_after64(io_start_time, start_time))
		blkio_add_stat(stats->stat_arr[BLKIO_STAT_WAIT_TIME],
				io_start_time - start_time, direction, sync);
	spin_unlock_irqrestore(&blkg->stats_lock, flags);
}
EXPORT_SYMBOL_GPL(blkiocg_update_completion_stats);

void blkiocg_update_io_merged_stats(struct blkio_group *blkg, bool direction,
					bool sync)
{
	unsigned long flags;

	spin_lock_irqsave(&blkg->stats_lock, flags);
	blkio_add_stat(blkg->stats.stat_arr[BLKIO_STAT_MERGED], 1, direction,
			sync);
	spin_unlock_irqrestore(&blkg->stats_lock, flags);
}
EXPORT_SYMBOL_GPL(blkiocg_update_io_merged_stats);

void blkiocg_add_blkio_group(struct blkio_cgroup *blkcg,
			struct blkio_group *blkg, void *key, dev_t dev)
{
	unsigned long flags;

	spin_lock_irqsave(&blkcg->lock, flags);
	rcu_assign_pointer(blkg->key, key);
	blkg->blkcg_id = css_id(&blkcg->css);
	hlist_add_head_rcu(&blkg->blkcg_node, &blkcg->blkg_list);
	spin_unlock_irqrestore(&blkcg->lock, flags);
#ifdef CONFIG_DEBUG_BLK_CGROUP
	/* Need to take css reference ? */
	cgroup_path(blkcg->css.cgroup, blkg->path, sizeof(blkg->path));
#endif
	blkg->dev = dev;
}
EXPORT_SYMBOL_GPL(blkiocg_add_blkio_group);

static void __blkiocg_del_blkio_group(struct blkio_group *blkg)
{
	hlist_del_init_rcu(&blkg->blkcg_node);
	blkg->blkcg_id = 0;
}

/*
 * returns 0 if blkio_group was still on cgroup list. Otherwise returns 1
 * indicating that blk_group was unhashed by the time we got to it.
 */
int blkiocg_del_blkio_group(struct blkio_group *blkg)
{
	struct blkio_cgroup *blkcg;
	unsigned long flags;
	struct cgroup_subsys_state *css;
	int ret = 1;

	rcu_read_lock();
	css = css_lookup(&blkio_subsys, blkg->blkcg_id);
	if (!css)
		goto out;

	blkcg = container_of(css, struct blkio_cgroup, css);
	spin_lock_irqsave(&blkcg->lock, flags);
	if (!hlist_unhashed(&blkg->blkcg_node)) {
		__blkiocg_del_blkio_group(blkg);
		ret = 0;
	}
	spin_unlock_irqrestore(&blkcg->lock, flags);
out:
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(blkiocg_del_blkio_group);

/* called under rcu_read_lock(). */
struct blkio_group *blkiocg_lookup_group(struct blkio_cgroup *blkcg, void *key)
{
	struct blkio_group *blkg;
	struct hlist_node *n;
	void *__key;

	hlist_for_each_entry_rcu(blkg, n, &blkcg->blkg_list, blkcg_node) {
		__key = blkg->key;
		if (__key == key)
			return blkg;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(blkiocg_lookup_group);

#define SHOW_FUNCTION(__VAR)						\
static u64 blkiocg_##__VAR##_read(struct cgroup *cgroup,		\
				       struct cftype *cftype)		\
{									\
	struct blkio_cgroup *blkcg;					\
									\
	blkcg = cgroup_to_blkio_cgroup(cgroup);				\
	return (u64)blkcg->__VAR;					\
}

SHOW_FUNCTION(weight);
#undef SHOW_FUNCTION

static int
blkiocg_weight_write(struct cgroup *cgroup, struct cftype *cftype, u64 val)
{
	struct blkio_cgroup *blkcg;
	struct blkio_group *blkg;
	struct hlist_node *n;
	struct blkio_policy_type *blkiop;

	if (val < BLKIO_WEIGHT_MIN || val > BLKIO_WEIGHT_MAX)
		return -EINVAL;

	blkcg = cgroup_to_blkio_cgroup(cgroup);
	spin_lock(&blkio_list_lock);
	spin_lock_irq(&blkcg->lock);
	blkcg->weight = (unsigned int)val;
	hlist_for_each_entry(blkg, n, &blkcg->blkg_list, blkcg_node) {
		list_for_each_entry(blkiop, &blkio_list, list)
			blkiop->ops.blkio_update_group_weight_fn(blkg,
					blkcg->weight);
	}
	spin_unlock_irq(&blkcg->lock);
	spin_unlock(&blkio_list_lock);
	return 0;
}

static int
blkiocg_reset_stats(struct cgroup *cgroup, struct cftype *cftype, u64 val)
{
	struct blkio_cgroup *blkcg;
	struct blkio_group *blkg;
	struct hlist_node *n;
	uint64_t queued[BLKIO_STAT_TOTAL];
	int i;

	blkcg = cgroup_to_blkio_cgroup(cgroup);
	spin_lock_irq(&blkcg->lock);
	hlist_for_each_entry(blkg, n, &blkcg->blkg_list, blkcg_node) {
		spin_lock(&blkg->stats_lock);
		for (i = 0; i < BLKIO_STAT_TOTAL; i++)
			queued[i] = blkg->stats.stat_arr[BLKIO_STAT_QUEUED][i];
		memset(&blkg->stats, 0, sizeof(struct blkio_group_stats));
		for (i = 0; i < BLKIO_STAT_TOTAL; i++)
			blkg->stats.stat_arr[BLKIO_STAT_QUEUED][i] = queued[i];
		spin_unlock(&blkg->stats_lock);
	}
	spin_unlock_irq(&blkcg->lock);
	return 0;
}

static void blkio_get_key_name(enum stat_sub_type type, dev_t dev, char *str,
				int chars_left, bool diskname_only)
{
	snprintf(str, chars_left, "%d:%d", MAJOR(dev), MINOR(dev));
	chars_left -= strlen(str);
	if (chars_left <= 0) {
		printk(KERN_WARNING
			"Possibly incorrect cgroup stat display format");
		return;
	}
	if (diskname_only)
		return;
	switch (type) {
	case BLKIO_STAT_READ:
		strlcat(str, " Read", chars_left);
		break;
	case BLKIO_STAT_WRITE:
		strlcat(str, " Write", chars_left);
		break;
	case BLKIO_STAT_SYNC:
		strlcat(str, " Sync", chars_left);
		break;
	case BLKIO_STAT_ASYNC:
		strlcat(str, " Async", chars_left);
		break;
	case BLKIO_STAT_TOTAL:
		strlcat(str, " Total", chars_left);
		break;
	default:
		strlcat(str, " Invalid", chars_left);
	}
}

static uint64_t blkio_fill_stat(char *str, int chars_left, uint64_t val,
				struct cgroup_map_cb *cb, dev_t dev)
{
	blkio_get_key_name(0, dev, str, chars_left, true);
	cb->fill(cb, str, val);
	return val;
}

/* This should be called with blkg->stats_lock held */
static uint64_t blkio_get_stat(struct blkio_group *blkg,
		struct cgroup_map_cb *cb, dev_t dev, enum stat_type type)
{
	uint64_t disk_total;
	char key_str[MAX_KEY_LEN];
	enum stat_sub_type sub_type;

	if (type == BLKIO_STAT_TIME)
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1,
					blkg->stats.time, cb, dev);
	if (type == BLKIO_STAT_SECTORS)
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1,
					blkg->stats.sectors, cb, dev);
#ifdef CONFIG_DEBUG_BLK_CGROUP
	if (type == BLKIO_STAT_AVG_QUEUE_SIZE) {
		uint64_t sum = blkg->stats.avg_queue_size_sum;
		uint64_t samples = blkg->stats.avg_queue_size_samples;
		if (samples)
			do_div(sum, samples);
		else
			sum = 0;
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1, sum, cb, dev);
	}
	if (type == BLKIO_STAT_DEQUEUE)
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1,
					blkg->stats.dequeue, cb, dev);
#endif

	for (sub_type = BLKIO_STAT_READ; sub_type < BLKIO_STAT_TOTAL;
			sub_type++) {
		blkio_get_key_name(sub_type, dev, key_str, MAX_KEY_LEN, false);
		cb->fill(cb, key_str, blkg->stats.stat_arr[type][sub_type]);
	}
	disk_total = blkg->stats.stat_arr[type][BLKIO_STAT_READ] +
			blkg->stats.stat_arr[type][BLKIO_STAT_WRITE];
	blkio_get_key_name(BLKIO_STAT_TOTAL, dev, key_str, MAX_KEY_LEN, false);
	cb->fill(cb, key_str, disk_total);
	return disk_total;
}

#define SHOW_FUNCTION_PER_GROUP(__VAR, type, show_total)		\
static int blkiocg_##__VAR##_read(struct cgroup *cgroup,		\
		struct cftype *cftype, struct cgroup_map_cb *cb)	\
{									\
	struct blkio_cgroup *blkcg;					\
	struct blkio_group *blkg;					\
	struct hlist_node *n;						\
	uint64_t cgroup_total = 0;					\
									\
	if (!cgroup_lock_live_group(cgroup))				\
		return -ENODEV;						\
									\
	blkcg = cgroup_to_blkio_cgroup(cgroup);				\
	rcu_read_lock();						\
	hlist_for_each_entry_rcu(blkg, n, &blkcg->blkg_list, blkcg_node) {\
		if (blkg->dev) {					\
			spin_lock_irq(&blkg->stats_lock);		\
			cgroup_total += blkio_get_stat(blkg, cb,	\
						blkg->dev, type);	\
			spin_unlock_irq(&blkg->stats_lock);		\
		}							\
	}								\
	if (show_total)							\
		cb->fill(cb, "Total", cgroup_total);			\
	rcu_read_unlock();						\
	cgroup_unlock();						\
	return 0;							\
}

SHOW_FUNCTION_PER_GROUP(time, BLKIO_STAT_TIME, 0);
SHOW_FUNCTION_PER_GROUP(sectors, BLKIO_STAT_SECTORS, 0);
SHOW_FUNCTION_PER_GROUP(io_service_bytes, BLKIO_STAT_SERVICE_BYTES, 1);
SHOW_FUNCTION_PER_GROUP(io_serviced, BLKIO_STAT_SERVICED, 1);
SHOW_FUNCTION_PER_GROUP(io_service_time, BLKIO_STAT_SERVICE_TIME, 1);
SHOW_FUNCTION_PER_GROUP(io_wait_time, BLKIO_STAT_WAIT_TIME, 1);
SHOW_FUNCTION_PER_GROUP(io_merged, BLKIO_STAT_MERGED, 1);
SHOW_FUNCTION_PER_GROUP(io_queued, BLKIO_STAT_QUEUED, 1);
#ifdef CONFIG_DEBUG_BLK_CGROUP
SHOW_FUNCTION_PER_GROUP(dequeue, BLKIO_STAT_DEQUEUE, 0);
SHOW_FUNCTION_PER_GROUP(avg_queue_size, BLKIO_STAT_AVG_QUEUE_SIZE, 0);
#endif
#undef SHOW_FUNCTION_PER_GROUP

#ifdef CONFIG_DEBUG_BLK_CGROUP
void blkiocg_update_dequeue_stats(struct blkio_group *blkg,
			unsigned long dequeue)
{
	blkg->stats.dequeue += dequeue;
}
EXPORT_SYMBOL_GPL(blkiocg_update_dequeue_stats);
#endif

struct cftype blkio_files[] = {
	{
		.name = "weight",
		.read_u64 = blkiocg_weight_read,
		.write_u64 = blkiocg_weight_write,
	},
	{
		.name = "time",
		.read_map = blkiocg_time_read,
	},
	{
		.name = "sectors",
		.read_map = blkiocg_sectors_read,
	},
	{
		.name = "io_service_bytes",
		.read_map = blkiocg_io_service_bytes_read,
	},
	{
		.name = "io_serviced",
		.read_map = blkiocg_io_serviced_read,
	},
	{
		.name = "io_service_time",
		.read_map = blkiocg_io_service_time_read,
	},
	{
		.name = "io_wait_time",
		.read_map = blkiocg_io_wait_time_read,
	},
	{
		.name = "io_merged",
		.read_map = blkiocg_io_merged_read,
	},
	{
		.name = "io_queued",
		.read_map = blkiocg_io_queued_read,
	},
	{
		.name = "reset_stats",
		.write_u64 = blkiocg_reset_stats,
	},
#ifdef CONFIG_DEBUG_BLK_CGROUP
	{
		.name = "avg_queue_size",
		.read_map = blkiocg_avg_queue_size_read,
	},
	{
		.name = "dequeue",
		.read_map = blkiocg_dequeue_read,
	},
#endif
};

static int blkiocg_populate(struct cgroup_subsys *subsys, struct cgroup *cgroup)
{
	return cgroup_add_files(cgroup, subsys, blkio_files,
				ARRAY_SIZE(blkio_files));
}

static void blkiocg_destroy(struct cgroup_subsys *subsys, struct cgroup *cgroup)
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgroup);
	unsigned long flags;
	struct blkio_group *blkg;
	void *key;
	struct blkio_policy_type *blkiop;

	rcu_read_lock();
remove_entry:
	spin_lock_irqsave(&blkcg->lock, flags);

	if (hlist_empty(&blkcg->blkg_list)) {
		spin_unlock_irqrestore(&blkcg->lock, flags);
		goto done;
	}

	blkg = hlist_entry(blkcg->blkg_list.first, struct blkio_group,
				blkcg_node);
	key = rcu_dereference(blkg->key);
	__blkiocg_del_blkio_group(blkg);

	spin_unlock_irqrestore(&blkcg->lock, flags);

	/*
	 * This blkio_group is being unlinked as associated cgroup is going
	 * away. Let all the IO controlling policies know about this event.
	 *
	 * Currently this is static call to one io controlling policy. Once
	 * we have more policies in place, we need some dynamic registration
	 * of callback function.
	 */
	spin_lock(&blkio_list_lock);
	list_for_each_entry(blkiop, &blkio_list, list)
		blkiop->ops.blkio_unlink_group_fn(key, blkg);
	spin_unlock(&blkio_list_lock);
	goto remove_entry;
done:
	free_css_id(&blkio_subsys, &blkcg->css);
	rcu_read_unlock();
	if (blkcg != &blkio_root_cgroup)
		kfree(blkcg);
}

static struct cgroup_subsys_state *
blkiocg_create(struct cgroup_subsys *subsys, struct cgroup *cgroup)
{
	struct blkio_cgroup *blkcg, *parent_blkcg;

	if (!cgroup->parent) {
		blkcg = &blkio_root_cgroup;
		goto done;
	}

	/* Currently we do not support hierarchy deeper than two level (0,1) */
	parent_blkcg = cgroup_to_blkio_cgroup(cgroup->parent);
	if (css_depth(&parent_blkcg->css) > 0)
		return ERR_PTR(-EINVAL);

	blkcg = kzalloc(sizeof(*blkcg), GFP_KERNEL);
	if (!blkcg)
		return ERR_PTR(-ENOMEM);

	blkcg->weight = BLKIO_WEIGHT_DEFAULT;
done:
	spin_lock_init(&blkcg->lock);
	INIT_HLIST_HEAD(&blkcg->blkg_list);

	return &blkcg->css;
}

/*
 * We cannot support shared io contexts, as we have no mean to support
 * two tasks with the same ioc in two different groups without major rework
 * of the main cic data structures.  For now we allow a task to change
 * its cgroup only if it's the only owner of its ioc.
 */
static int blkiocg_can_attach(struct cgroup_subsys *subsys,
				struct cgroup *cgroup, struct task_struct *tsk,
				bool threadgroup)
{
	struct io_context *ioc;
	int ret = 0;

	/* task_lock() is needed to avoid races with exit_io_context() */
	task_lock(tsk);
	ioc = tsk->io_context;
	if (ioc && atomic_read(&ioc->nr_tasks) > 1)
		ret = -EINVAL;
	task_unlock(tsk);

	return ret;
}

static void blkiocg_attach(struct cgroup_subsys *subsys, struct cgroup *cgroup,
				struct cgroup *prev, struct task_struct *tsk,
				bool threadgroup)
{
	struct io_context *ioc;

	task_lock(tsk);
	ioc = tsk->io_context;
	if (ioc)
		ioc->cgroup_changed = 1;
	task_unlock(tsk);
}

void blkio_policy_register(struct blkio_policy_type *blkiop)
{
	spin_lock(&blkio_list_lock);
	list_add_tail(&blkiop->list, &blkio_list);
	spin_unlock(&blkio_list_lock);
}
EXPORT_SYMBOL_GPL(blkio_policy_register);

void blkio_policy_unregister(struct blkio_policy_type *blkiop)
{
	spin_lock(&blkio_list_lock);
	list_del_init(&blkiop->list);
	spin_unlock(&blkio_list_lock);
}
EXPORT_SYMBOL_GPL(blkio_policy_unregister);

static int __init init_cgroup_blkio(void)
{
	return cgroup_load_subsys(&blkio_subsys);
}

static void __exit exit_cgroup_blkio(void)
{
	cgroup_unload_subsys(&blkio_subsys);
}

module_init(init_cgroup_blkio);
module_exit(exit_cgroup_blkio);
MODULE_LICENSE("GPL");
