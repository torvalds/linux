// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/blk_types.h>

#include "cache.h"
#include "cache_dev.h"
#include "backing_dev.h"
#include "dm_pcache.h"

struct kmem_cache *key_cache;

static inline struct pcache_cache_info *get_cache_info_addr(struct pcache_cache *cache)
{
	return cache->cache_info_addr + cache->info_index;
}

static void cache_info_write(struct pcache_cache *cache)
{
	struct pcache_cache_info *cache_info = &cache->cache_info;

	cache_info->header.seq++;
	cache_info->header.crc = pcache_meta_crc(&cache_info->header,
						sizeof(struct pcache_cache_info));

	memcpy_flushcache(get_cache_info_addr(cache), cache_info,
			sizeof(struct pcache_cache_info));

	cache->info_index = (cache->info_index + 1) % PCACHE_META_INDEX_MAX;
}

static void cache_info_init_default(struct pcache_cache *cache);
static int cache_info_init(struct pcache_cache *cache, struct pcache_cache_options *opts)
{
	struct dm_pcache *pcache = CACHE_TO_PCACHE(cache);
	struct pcache_cache_info *cache_info_addr;

	cache_info_addr = pcache_meta_find_latest(&cache->cache_info_addr->header,
						sizeof(struct pcache_cache_info),
						PCACHE_CACHE_INFO_SIZE,
						&cache->cache_info);
	if (IS_ERR(cache_info_addr))
		return PTR_ERR(cache_info_addr);

	if (cache_info_addr) {
		if (opts->data_crc !=
				(cache->cache_info.flags & PCACHE_CACHE_FLAGS_DATA_CRC)) {
			pcache_dev_err(pcache, "invalid option for data_crc: %s, expected: %s",
					opts->data_crc ? "true" : "false",
					cache->cache_info.flags & PCACHE_CACHE_FLAGS_DATA_CRC ? "true" : "false");
			return -EINVAL;
		}

		return 0;
	}

	/* init cache_info for new cache */
	cache_info_init_default(cache);
	cache_mode_set(cache, opts->cache_mode);
	if (opts->data_crc)
		cache->cache_info.flags |= PCACHE_CACHE_FLAGS_DATA_CRC;

	return 0;
}

static void cache_info_set_gc_percent(struct pcache_cache_info *cache_info, u8 percent)
{
	cache_info->flags &= ~PCACHE_CACHE_FLAGS_GC_PERCENT_MASK;
	cache_info->flags |= FIELD_PREP(PCACHE_CACHE_FLAGS_GC_PERCENT_MASK, percent);
}

int pcache_cache_set_gc_percent(struct pcache_cache *cache, u8 percent)
{
	if (percent > PCACHE_CACHE_GC_PERCENT_MAX || percent < PCACHE_CACHE_GC_PERCENT_MIN)
		return -EINVAL;

	mutex_lock(&cache->cache_info_lock);
	cache_info_set_gc_percent(&cache->cache_info, percent);

	cache_info_write(cache);
	mutex_unlock(&cache->cache_info_lock);

	return 0;
}

void cache_pos_encode(struct pcache_cache *cache,
			     struct pcache_cache_pos_onmedia *pos_onmedia_base,
			     struct pcache_cache_pos *pos, u64 seq, u32 *index)
{
	struct pcache_cache_pos_onmedia pos_onmedia;
	struct pcache_cache_pos_onmedia *pos_onmedia_addr = pos_onmedia_base + *index;

	pos_onmedia.cache_seg_id = pos->cache_seg->cache_seg_id;
	pos_onmedia.seg_off = pos->seg_off;
	pos_onmedia.header.seq = seq;
	pos_onmedia.header.crc = cache_pos_onmedia_crc(&pos_onmedia);

	memcpy_flushcache(pos_onmedia_addr, &pos_onmedia, sizeof(struct pcache_cache_pos_onmedia));
	pmem_wmb();

	*index = (*index + 1) % PCACHE_META_INDEX_MAX;
}

int cache_pos_decode(struct pcache_cache *cache,
			    struct pcache_cache_pos_onmedia *pos_onmedia,
			    struct pcache_cache_pos *pos, u64 *seq, u32 *index)
{
	struct pcache_cache_pos_onmedia latest, *latest_addr;

	latest_addr = pcache_meta_find_latest(&pos_onmedia->header,
					sizeof(struct pcache_cache_pos_onmedia),
					sizeof(struct pcache_cache_pos_onmedia),
					&latest);
	if (IS_ERR(latest_addr))
		return PTR_ERR(latest_addr);

	if (!latest_addr)
		return -EIO;

	pos->cache_seg = &cache->segments[latest.cache_seg_id];
	pos->seg_off = latest.seg_off;
	*seq = latest.header.seq;
	*index = (latest_addr - pos_onmedia);

	return 0;
}

static inline void cache_info_set_seg_id(struct pcache_cache *cache, u32 seg_id)
{
	cache->cache_info.seg_id = seg_id;
}

static int cache_init(struct dm_pcache *pcache)
{
	struct pcache_cache *cache = &pcache->cache;
	struct pcache_backing_dev *backing_dev = &pcache->backing_dev;
	struct pcache_cache_dev *cache_dev = &pcache->cache_dev;
	int ret;

	cache->segments = kvcalloc(cache_dev->seg_num, sizeof(struct pcache_cache_segment), GFP_KERNEL);
	if (!cache->segments) {
		ret = -ENOMEM;
		goto err;
	}

	cache->seg_map = kvcalloc(BITS_TO_LONGS(cache_dev->seg_num), sizeof(unsigned long), GFP_KERNEL);
	if (!cache->seg_map) {
		ret = -ENOMEM;
		goto free_segments;
	}

	cache->backing_dev = backing_dev;
	cache->cache_dev = &pcache->cache_dev;
	cache->n_segs = cache_dev->seg_num;
	atomic_set(&cache->gc_errors, 0);
	spin_lock_init(&cache->seg_map_lock);
	spin_lock_init(&cache->key_head_lock);

	mutex_init(&cache->cache_info_lock);
	mutex_init(&cache->key_tail_lock);
	mutex_init(&cache->dirty_tail_lock);
	mutex_init(&cache->writeback_lock);

	INIT_DELAYED_WORK(&cache->writeback_work, cache_writeback_fn);
	INIT_DELAYED_WORK(&cache->gc_work, pcache_cache_gc_fn);
	INIT_WORK(&cache->clean_work, clean_fn);

	return 0;

free_segments:
	kvfree(cache->segments);
err:
	return ret;
}

static void cache_exit(struct pcache_cache *cache)
{
	kvfree(cache->seg_map);
	kvfree(cache->segments);
}

static void cache_info_init_default(struct pcache_cache *cache)
{
	struct pcache_cache_info *cache_info = &cache->cache_info;

	memset(cache_info, 0, sizeof(*cache_info));
	cache_info->n_segs = cache->cache_dev->seg_num;
	cache_info_set_gc_percent(cache_info, PCACHE_CACHE_GC_PERCENT_DEFAULT);
}

static int cache_tail_init(struct pcache_cache *cache)
{
	struct dm_pcache *pcache = CACHE_TO_PCACHE(cache);
	bool new_cache = !(cache->cache_info.flags & PCACHE_CACHE_FLAGS_INIT_DONE);

	if (new_cache) {
		__set_bit(0, cache->seg_map);

		cache->key_head.cache_seg = &cache->segments[0];
		cache->key_head.seg_off = 0;
		cache_pos_copy(&cache->key_tail, &cache->key_head);
		cache_pos_copy(&cache->dirty_tail, &cache->key_head);

		cache_encode_dirty_tail(cache);
		cache_encode_key_tail(cache);
	} else {
		if (cache_decode_key_tail(cache) || cache_decode_dirty_tail(cache)) {
			pcache_dev_err(pcache, "Corrupted key tail or dirty tail.\n");
			return -EIO;
		}
	}

	return 0;
}

static int get_seg_id(struct pcache_cache *cache,
		      struct pcache_cache_segment *prev_cache_seg,
		      bool new_cache, u32 *seg_id)
{
	struct dm_pcache *pcache = CACHE_TO_PCACHE(cache);
	struct pcache_cache_dev *cache_dev = cache->cache_dev;
	int ret;

	if (new_cache) {
		ret = cache_dev_get_empty_segment_id(cache_dev, seg_id);
		if (ret) {
			pcache_dev_err(pcache, "no available segment\n");
			goto err;
		}

		if (prev_cache_seg)
			cache_seg_set_next_seg(prev_cache_seg, *seg_id);
		else
			cache_info_set_seg_id(cache, *seg_id);
	} else {
		if (prev_cache_seg) {
			struct pcache_segment_info *prev_seg_info;

			prev_seg_info = &prev_cache_seg->cache_seg_info;
			if (!segment_info_has_next(prev_seg_info)) {
				ret = -EFAULT;
				goto err;
			}
			*seg_id = prev_cache_seg->cache_seg_info.next_seg;
		} else {
			*seg_id = cache->cache_info.seg_id;
		}
	}
	return 0;
err:
	return ret;
}

static int cache_segs_init(struct pcache_cache *cache)
{
	struct pcache_cache_segment *prev_cache_seg = NULL;
	struct pcache_cache_info *cache_info = &cache->cache_info;
	bool new_cache = !(cache->cache_info.flags & PCACHE_CACHE_FLAGS_INIT_DONE);
	u32 seg_id;
	int ret;
	u32 i;

	for (i = 0; i < cache_info->n_segs; i++) {
		ret = get_seg_id(cache, prev_cache_seg, new_cache, &seg_id);
		if (ret)
			goto err;

		ret = cache_seg_init(cache, seg_id, i, new_cache);
		if (ret)
			goto err;

		prev_cache_seg = &cache->segments[i];
	}
	return 0;
err:
	return ret;
}

static int cache_init_req_keys(struct pcache_cache *cache, u32 n_paral)
{
	struct dm_pcache *pcache = CACHE_TO_PCACHE(cache);
	u32 n_subtrees;
	int ret;
	u32 i, cpu;

	/* Calculate number of cache trees based on the device size */
	n_subtrees = DIV_ROUND_UP(cache->dev_size << SECTOR_SHIFT, PCACHE_CACHE_SUBTREE_SIZE);
	ret = cache_tree_init(cache, &cache->req_key_tree, n_subtrees);
	if (ret)
		goto err;

	cache->n_ksets = n_paral;
	cache->ksets = kvcalloc(cache->n_ksets, PCACHE_KSET_SIZE, GFP_KERNEL);
	if (!cache->ksets) {
		ret = -ENOMEM;
		goto req_tree_exit;
	}

	/*
	 * Initialize each kset with a spinlock and delayed work for flushing.
	 * Each kset is associated with one queue to ensure independent handling
	 * of cache keys across multiple queues, maximizing multiqueue concurrency.
	 */
	for (i = 0; i < cache->n_ksets; i++) {
		struct pcache_cache_kset *kset = get_kset(cache, i);

		kset->cache = cache;
		spin_lock_init(&kset->kset_lock);
		INIT_DELAYED_WORK(&kset->flush_work, kset_flush_fn);
	}

	cache->data_heads = alloc_percpu(struct pcache_cache_data_head);
	if (!cache->data_heads) {
		ret = -ENOMEM;
		goto free_kset;
	}

	for_each_possible_cpu(cpu) {
		struct pcache_cache_data_head *h =
			per_cpu_ptr(cache->data_heads, cpu);
		h->head_pos.cache_seg = NULL;
	}

	/*
	 * Replay persisted cache keys using cache_replay.
	 * This function loads and replays cache keys from previously stored
	 * ksets, allowing the cache to restore its state after a restart.
	 */
	ret = cache_replay(cache);
	if (ret) {
		pcache_dev_err(pcache, "failed to replay keys\n");
		goto free_heads;
	}

	return 0;

free_heads:
	free_percpu(cache->data_heads);
free_kset:
	kvfree(cache->ksets);
req_tree_exit:
	cache_tree_exit(&cache->req_key_tree);
err:
	return ret;
}

static void cache_destroy_req_keys(struct pcache_cache *cache)
{
	u32 i;

	for (i = 0; i < cache->n_ksets; i++) {
		struct pcache_cache_kset *kset = get_kset(cache, i);

		cancel_delayed_work_sync(&kset->flush_work);
	}

	free_percpu(cache->data_heads);
	kvfree(cache->ksets);
	cache_tree_exit(&cache->req_key_tree);
}

int pcache_cache_start(struct dm_pcache *pcache)
{
	struct pcache_backing_dev *backing_dev = &pcache->backing_dev;
	struct pcache_cache *cache = &pcache->cache;
	struct pcache_cache_options *opts = &pcache->opts;
	int ret;

	ret = cache_init(pcache);
	if (ret)
		return ret;

	cache->cache_info_addr = CACHE_DEV_CACHE_INFO(cache->cache_dev);
	cache->cache_ctrl = CACHE_DEV_CACHE_CTRL(cache->cache_dev);
	backing_dev->cache = cache;
	cache->dev_size = backing_dev->dev_size;

	ret = cache_info_init(cache, opts);
	if (ret)
		goto cache_exit;

	ret = cache_segs_init(cache);
	if (ret)
		goto cache_exit;

	ret = cache_tail_init(cache);
	if (ret)
		goto cache_exit;

	ret = cache_init_req_keys(cache, num_online_cpus());
	if (ret)
		goto cache_exit;

	ret = cache_writeback_init(cache);
	if (ret)
		goto destroy_keys;

	cache->cache_info.flags |= PCACHE_CACHE_FLAGS_INIT_DONE;
	cache_info_write(cache);
	queue_delayed_work(cache_get_wq(cache), &cache->gc_work, 0);

	return 0;

destroy_keys:
	cache_destroy_req_keys(cache);
cache_exit:
	cache_exit(cache);

	return ret;
}

void pcache_cache_stop(struct dm_pcache *pcache)
{
	struct pcache_cache *cache = &pcache->cache;

	pcache_cache_flush(cache);

	cancel_delayed_work_sync(&cache->gc_work);
	flush_work(&cache->clean_work);
	cache_writeback_exit(cache);

	if (cache->req_key_tree.n_subtrees)
		cache_destroy_req_keys(cache);

	cache_exit(cache);
}

struct workqueue_struct *cache_get_wq(struct pcache_cache *cache)
{
	struct dm_pcache *pcache = CACHE_TO_PCACHE(cache);

	return pcache->task_wq;
}

int pcache_cache_init(void)
{
	key_cache = KMEM_CACHE(pcache_cache_key, 0);
	if (!key_cache)
		return -ENOMEM;

	return 0;
}

void pcache_cache_exit(void)
{
	kmem_cache_destroy(key_cache);
}
