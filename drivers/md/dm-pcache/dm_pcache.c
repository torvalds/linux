// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/bio.h>

#include "../dm-core.h"
#include "cache_dev.h"
#include "backing_dev.h"
#include "cache.h"
#include "dm_pcache.h"

void pcache_defer_reqs_kick(struct dm_pcache *pcache)
{
	struct pcache_cache *cache = &pcache->cache;

	spin_lock(&cache->seg_map_lock);
	if (!cache->cache_full)
		queue_work(pcache->task_wq, &pcache->defered_req_work);
	spin_unlock(&cache->seg_map_lock);
}

static void defer_req(struct pcache_request *pcache_req)
{
	struct dm_pcache *pcache = pcache_req->pcache;

	BUG_ON(!list_empty(&pcache_req->list_node));

	spin_lock(&pcache->defered_req_list_lock);
	list_add(&pcache_req->list_node, &pcache->defered_req_list);
	pcache_defer_reqs_kick(pcache);
	spin_unlock(&pcache->defered_req_list_lock);
}

static void defered_req_fn(struct work_struct *work)
{
	struct dm_pcache *pcache = container_of(work, struct dm_pcache, defered_req_work);
	struct pcache_request *pcache_req;
	LIST_HEAD(tmp_list);
	int ret;

	if (pcache_is_stopping(pcache))
		return;

	spin_lock(&pcache->defered_req_list_lock);
	list_splice_init(&pcache->defered_req_list, &tmp_list);
	spin_unlock(&pcache->defered_req_list_lock);

	while (!list_empty(&tmp_list)) {
		pcache_req = list_first_entry(&tmp_list,
					    struct pcache_request, list_node);
		list_del_init(&pcache_req->list_node);
		pcache_req->ret = 0;
		ret = pcache_cache_handle_req(&pcache->cache, pcache_req);
		if (ret == -EBUSY)
			defer_req(pcache_req);
		else
			pcache_req_put(pcache_req, ret);
	}
}

void pcache_req_get(struct pcache_request *pcache_req)
{
	kref_get(&pcache_req->ref);
}

static void end_req(struct kref *ref)
{
	struct pcache_request *pcache_req = container_of(ref, struct pcache_request, ref);
	struct dm_pcache *pcache = pcache_req->pcache;
	struct bio *bio = pcache_req->bio;
	int ret = pcache_req->ret;

	if (ret == -EBUSY) {
		pcache_req_get(pcache_req);
		defer_req(pcache_req);
	} else {
		bio->bi_status = errno_to_blk_status(ret);
		bio_endio(bio);

		if (atomic_dec_and_test(&pcache->inflight_reqs))
			wake_up(&pcache->inflight_wq);
	}
}

void pcache_req_put(struct pcache_request *pcache_req, int ret)
{
	/* Set the return status if it is not already set */
	if (ret && !pcache_req->ret)
		pcache_req->ret = ret;

	kref_put(&pcache_req->ref, end_req);
}

static bool at_least_one_arg(struct dm_arg_set *as, char **error)
{
	if (!as->argc) {
		*error = "Insufficient args";
		return false;
	}

	return true;
}

static int parse_cache_dev(struct dm_pcache *pcache, struct dm_arg_set *as,
				char **error)
{
	int ret;

	if (!at_least_one_arg(as, error))
		return -EINVAL;
	ret = dm_get_device(pcache->ti, dm_shift_arg(as),
			  BLK_OPEN_READ | BLK_OPEN_WRITE,
			  &pcache->cache_dev.dm_dev);
	if (ret) {
		*error = "Error opening cache device";
		return ret;
	}

	return 0;
}

static int parse_backing_dev(struct dm_pcache *pcache, struct dm_arg_set *as,
				char **error)
{
	int ret;

	if (!at_least_one_arg(as, error))
		return -EINVAL;

	ret = dm_get_device(pcache->ti, dm_shift_arg(as),
			  BLK_OPEN_READ | BLK_OPEN_WRITE,
			  &pcache->backing_dev.dm_dev);
	if (ret) {
		*error = "Error opening backing device";
		return ret;
	}

	return 0;
}

static void pcache_init_opts(struct pcache_cache_options *opts)
{
	opts->cache_mode = PCACHE_CACHE_MODE_WRITEBACK;
	opts->data_crc = false;
}

static int parse_cache_opts(struct dm_pcache *pcache, struct dm_arg_set *as,
			    char **error)
{
	struct pcache_cache_options *opts = &pcache->opts;
	static const struct dm_arg _args[] = {
		{0, 4, "Invalid number of cache option arguments"},
	};
	unsigned int argc;
	const char *arg;
	int ret;

	pcache_init_opts(opts);
	if (!as->argc)
		return 0;

	ret = dm_read_arg_group(_args, as, &argc, error);
	if (ret)
		return -EINVAL;

	while (argc) {
		arg = dm_shift_arg(as);
		argc--;

		if (!strcmp(arg, "cache_mode")) {
			arg = dm_shift_arg(as);
			if (!strcmp(arg, "writeback")) {
				opts->cache_mode = PCACHE_CACHE_MODE_WRITEBACK;
			} else {
				*error = "Invalid cache mode parameter";
				return -EINVAL;
			}
			argc--;
		} else if (!strcmp(arg, "data_crc")) {
			arg = dm_shift_arg(as);
			if (!strcmp(arg, "true")) {
				opts->data_crc = true;
			} else if (!strcmp(arg, "false")) {
				opts->data_crc = false;
			} else {
				*error = "Invalid data crc parameter";
				return -EINVAL;
			}
			argc--;
		} else {
			*error = "Unrecognised cache option requested";
			return -EINVAL;
		}
	}

	return 0;
}

static int pcache_start(struct dm_pcache *pcache, char **error)
{
	int ret;

	ret = cache_dev_start(pcache);
	if (ret) {
		*error = "Failed to start cache dev";
		return ret;
	}

	ret = backing_dev_start(pcache);
	if (ret) {
		*error = "Failed to start backing dev";
		goto stop_cache;
	}

	ret = pcache_cache_start(pcache);
	if (ret) {
		*error = "Failed to start pcache";
		goto stop_backing;
	}

	return 0;
stop_backing:
	backing_dev_stop(pcache);
stop_cache:
	cache_dev_stop(pcache);

	return ret;
}

static void pcache_destroy_args(struct dm_pcache *pcache)
{
	if (pcache->cache_dev.dm_dev)
		dm_put_device(pcache->ti, pcache->cache_dev.dm_dev);
	if (pcache->backing_dev.dm_dev)
		dm_put_device(pcache->ti, pcache->backing_dev.dm_dev);
}

static int pcache_parse_args(struct dm_pcache *pcache, unsigned int argc, char **argv,
				char **error)
{
	struct dm_arg_set as;
	int ret;

	as.argc = argc;
	as.argv = argv;

	/*
	 * Parse cache device
	 */
	ret = parse_cache_dev(pcache, &as, error);
	if (ret)
		return ret;
	/*
	 * Parse backing device
	 */
	ret = parse_backing_dev(pcache, &as, error);
	if (ret)
		goto out;
	/*
	 * Parse optional arguments
	 */
	ret = parse_cache_opts(pcache, &as, error);
	if (ret)
		goto out;

	return 0;
out:
	pcache_destroy_args(pcache);
	return ret;
}

static int dm_pcache_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct mapped_device *md = ti->table->md;
	struct dm_pcache *pcache;
	int ret;

	if (md->map) {
		ti->error = "Don't support table loading for live md";
		return -EOPNOTSUPP;
	}

	/* Allocate memory for the cache structure */
	pcache = kzalloc(sizeof(struct dm_pcache), GFP_KERNEL);
	if (!pcache)
		return -ENOMEM;

	pcache->task_wq = alloc_workqueue("pcache-%s-wq",  WQ_UNBOUND | WQ_MEM_RECLAIM,
					  0, md->name);
	if (!pcache->task_wq) {
		ret = -ENOMEM;
		goto free_pcache;
	}

	spin_lock_init(&pcache->defered_req_list_lock);
	INIT_LIST_HEAD(&pcache->defered_req_list);
	INIT_WORK(&pcache->defered_req_work, defered_req_fn);
	pcache->ti = ti;

	ret = pcache_parse_args(pcache, argc, argv, &ti->error);
	if (ret)
		goto destroy_wq;

	ret = pcache_start(pcache, &ti->error);
	if (ret)
		goto destroy_args;

	ti->num_flush_bios = 1;
	ti->flush_supported = true;
	ti->per_io_data_size = sizeof(struct pcache_request);
	ti->private = pcache;
	atomic_set(&pcache->inflight_reqs, 0);
	atomic_set(&pcache->state, PCACHE_STATE_RUNNING);
	init_waitqueue_head(&pcache->inflight_wq);

	return 0;
destroy_args:
	pcache_destroy_args(pcache);
destroy_wq:
	destroy_workqueue(pcache->task_wq);
free_pcache:
	kfree(pcache);

	return ret;
}

static void defer_req_stop(struct dm_pcache *pcache)
{
	struct pcache_request *pcache_req;
	LIST_HEAD(tmp_list);

	flush_work(&pcache->defered_req_work);

	spin_lock(&pcache->defered_req_list_lock);
	list_splice_init(&pcache->defered_req_list, &tmp_list);
	spin_unlock(&pcache->defered_req_list_lock);

	while (!list_empty(&tmp_list)) {
		pcache_req = list_first_entry(&tmp_list,
					    struct pcache_request, list_node);
		list_del_init(&pcache_req->list_node);
		pcache_req_put(pcache_req, -EIO);
	}
}

static void dm_pcache_dtr(struct dm_target *ti)
{
	struct dm_pcache *pcache;

	pcache = ti->private;
	atomic_set(&pcache->state, PCACHE_STATE_STOPPING);
	defer_req_stop(pcache);

	wait_event(pcache->inflight_wq,
			atomic_read(&pcache->inflight_reqs) == 0);

	pcache_cache_stop(pcache);
	backing_dev_stop(pcache);
	cache_dev_stop(pcache);

	pcache_destroy_args(pcache);
	drain_workqueue(pcache->task_wq);
	destroy_workqueue(pcache->task_wq);

	kfree(pcache);
}

static int dm_pcache_map_bio(struct dm_target *ti, struct bio *bio)
{
	struct pcache_request *pcache_req = dm_per_bio_data(bio, sizeof(struct pcache_request));
	struct dm_pcache *pcache = ti->private;
	int ret;

	pcache_req->pcache = pcache;
	kref_init(&pcache_req->ref);
	pcache_req->ret = 0;
	pcache_req->bio = bio;
	pcache_req->off = (u64)bio->bi_iter.bi_sector << SECTOR_SHIFT;
	pcache_req->data_len = bio->bi_iter.bi_size;
	INIT_LIST_HEAD(&pcache_req->list_node);
	atomic_inc(&pcache->inflight_reqs);

	ret = pcache_cache_handle_req(&pcache->cache, pcache_req);
	if (ret == -EBUSY)
		defer_req(pcache_req);
	else
		pcache_req_put(pcache_req, ret);

	return DM_MAPIO_SUBMITTED;
}

static void dm_pcache_status(struct dm_target *ti, status_type_t type,
			     unsigned int status_flags, char *result,
			     unsigned int maxlen)
{
	struct dm_pcache *pcache = ti->private;
	struct pcache_cache_dev *cache_dev = &pcache->cache_dev;
	struct pcache_backing_dev *backing_dev = &pcache->backing_dev;
	struct pcache_cache *cache = &pcache->cache;
	unsigned int sz = 0;

	switch (type) {
	case STATUSTYPE_INFO:
		DMEMIT("%x %u %u %u %u %x %u:%u %u:%u %u:%u",
		       cache_dev->sb_flags,
		       cache_dev->seg_num,
		       cache->n_segs,
		       bitmap_weight(cache->seg_map, cache->n_segs),
		       pcache_cache_get_gc_percent(cache),
		       cache->cache_info.flags,
		       cache->key_head.cache_seg->cache_seg_id,
		       cache->key_head.seg_off,
		       cache->dirty_tail.cache_seg->cache_seg_id,
		       cache->dirty_tail.seg_off,
		       cache->key_tail.cache_seg->cache_seg_id,
		       cache->key_tail.seg_off);
		break;
	case STATUSTYPE_TABLE:
		DMEMIT("%s %s 4 cache_mode writeback crc %s",
		       cache_dev->dm_dev->name,
		       backing_dev->dm_dev->name,
		       cache_data_crc_on(cache) ? "true" : "false");
		break;
	case STATUSTYPE_IMA:
		*result = '\0';
		break;
	}
}

static int dm_pcache_message(struct dm_target *ti, unsigned int argc,
			     char **argv, char *result, unsigned int maxlen)
{
	struct dm_pcache *pcache = ti->private;
	unsigned long val;

	if (argc != 2)
		goto err;

	if (!strcasecmp(argv[0], "gc_percent")) {
		if (kstrtoul(argv[1], 10, &val))
			goto err;

		return pcache_cache_set_gc_percent(&pcache->cache, val);
	}
err:
	return -EINVAL;
}

static struct target_type dm_pcache_target = {
	.name		= "pcache",
	.version	= {0, 1, 0},
	.module		= THIS_MODULE,
	.features	= DM_TARGET_SINGLETON,
	.ctr		= dm_pcache_ctr,
	.dtr		= dm_pcache_dtr,
	.map		= dm_pcache_map_bio,
	.status		= dm_pcache_status,
	.message	= dm_pcache_message,
};

static int __init dm_pcache_init(void)
{
	int ret;

	ret = pcache_backing_init();
	if (ret)
		goto err;

	ret = pcache_cache_init();
	if (ret)
		goto backing_exit;

	ret = dm_register_target(&dm_pcache_target);
	if (ret)
		goto cache_exit;
	return 0;

cache_exit:
	pcache_cache_exit();
backing_exit:
	pcache_backing_exit();
err:
	return ret;
}
module_init(dm_pcache_init);

static void __exit dm_pcache_exit(void)
{
	dm_unregister_target(&dm_pcache_target);
	pcache_cache_exit();
	pcache_backing_exit();
}
module_exit(dm_pcache_exit);

MODULE_DESCRIPTION("dm-pcache Persistent Cache for block device");
MODULE_AUTHOR("Dongsheng Yang <dongsheng.yang@linux.dev>");
MODULE_LICENSE("GPL");
