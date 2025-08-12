// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/bio.h>

#include "cache.h"
#include "backing_dev.h"
#include "cache_dev.h"
#include "dm_pcache.h"

static void writeback_ctx_end(struct pcache_cache *cache, int ret)
{
	if (ret && !cache->writeback_ctx.ret) {
		pcache_dev_err(CACHE_TO_PCACHE(cache), "writeback error: %d", ret);
		cache->writeback_ctx.ret = ret;
	}

	if (!atomic_dec_and_test(&cache->writeback_ctx.pending))
		return;

	if (!cache->writeback_ctx.ret) {
		backing_dev_flush(cache->backing_dev);

		mutex_lock(&cache->dirty_tail_lock);
		cache_pos_advance(&cache->dirty_tail, cache->writeback_ctx.advance);
		cache_encode_dirty_tail(cache);
		mutex_unlock(&cache->dirty_tail_lock);
	}
	queue_delayed_work(cache_get_wq(cache), &cache->writeback_work, 0);
}

static void writeback_end_req(struct pcache_backing_dev_req *backing_req, int ret)
{
	struct pcache_cache *cache = backing_req->priv_data;

	mutex_lock(&cache->writeback_lock);
	writeback_ctx_end(cache, ret);
	mutex_unlock(&cache->writeback_lock);
}

static inline bool is_cache_clean(struct pcache_cache *cache, struct pcache_cache_pos *dirty_tail)
{
	struct dm_pcache *pcache = CACHE_TO_PCACHE(cache);
	struct pcache_cache_kset_onmedia *kset_onmedia;
	u32 to_copy;
	void *addr;
	int ret;

	addr = cache_pos_addr(dirty_tail);
	kset_onmedia = (struct pcache_cache_kset_onmedia *)cache->wb_kset_onmedia_buf;

	to_copy = min(PCACHE_KSET_ONMEDIA_SIZE_MAX, PCACHE_SEG_SIZE - dirty_tail->seg_off);
	ret = copy_mc_to_kernel(kset_onmedia, addr, to_copy);
	if (ret) {
		pcache_dev_err(pcache, "error to read kset: %d", ret);
		return true;
	}

	/* Check if the magic number matches the expected value */
	if (kset_onmedia->magic != PCACHE_KSET_MAGIC) {
		pcache_dev_debug(pcache, "dirty_tail: %u:%u magic: %llx, not expected: %llx\n",
				dirty_tail->cache_seg->cache_seg_id, dirty_tail->seg_off,
				kset_onmedia->magic, PCACHE_KSET_MAGIC);
		return true;
	}

	/* Verify the CRC checksum for data integrity */
	if (kset_onmedia->crc != cache_kset_crc(kset_onmedia)) {
		pcache_dev_debug(pcache, "dirty_tail: %u:%u crc: %x, not expected: %x\n",
				dirty_tail->cache_seg->cache_seg_id, dirty_tail->seg_off,
				cache_kset_crc(kset_onmedia), kset_onmedia->crc);
		return true;
	}

	return false;
}

void cache_writeback_exit(struct pcache_cache *cache)
{
	cancel_delayed_work_sync(&cache->writeback_work);
	backing_dev_flush(cache->backing_dev);
	cache_tree_exit(&cache->writeback_key_tree);
}

int cache_writeback_init(struct pcache_cache *cache)
{
	int ret;

	ret = cache_tree_init(cache, &cache->writeback_key_tree, 1);
	if (ret)
		goto err;

	atomic_set(&cache->writeback_ctx.pending, 0);

	/* Queue delayed work to start writeback handling */
	queue_delayed_work(cache_get_wq(cache), &cache->writeback_work, 0);

	return 0;
err:
	return ret;
}

static void cache_key_writeback(struct pcache_cache *cache, struct pcache_cache_key *key)
{
	struct pcache_backing_dev_req *writeback_req;
	struct pcache_backing_dev_req_opts writeback_req_opts = { 0 };
	struct pcache_cache_pos *pos;
	void *addr;
	u32 seg_remain, req_len, done = 0;

	if (cache_key_clean(key))
		return;

	pos = &key->cache_pos;

	seg_remain = cache_seg_remain(pos);
	BUG_ON(seg_remain < key->len);
next_req:
	addr = cache_pos_addr(pos) + done;
	req_len = backing_dev_req_coalesced_max_len(addr, key->len - done);

	writeback_req_opts.type = BACKING_DEV_REQ_TYPE_KMEM;
	writeback_req_opts.gfp_mask = GFP_NOIO;
	writeback_req_opts.end_fn = writeback_end_req;
	writeback_req_opts.priv_data = cache;

	writeback_req_opts.kmem.data = addr;
	writeback_req_opts.kmem.opf = REQ_OP_WRITE;
	writeback_req_opts.kmem.len = req_len;
	writeback_req_opts.kmem.backing_off = key->off + done;

	writeback_req = backing_dev_req_create(cache->backing_dev, &writeback_req_opts);

	atomic_inc(&cache->writeback_ctx.pending);
	backing_dev_req_submit(writeback_req, true);

	done += req_len;
	if (done < key->len)
		goto next_req;
}

static void cache_wb_tree_writeback(struct pcache_cache *cache, u32 advance)
{
	struct pcache_cache_tree *cache_tree = &cache->writeback_key_tree;
	struct pcache_cache_subtree *cache_subtree;
	struct rb_node *node;
	struct pcache_cache_key *key;
	u32 i;

	cache->writeback_ctx.ret = 0;
	cache->writeback_ctx.advance = advance;
	atomic_set(&cache->writeback_ctx.pending, 1);

	for (i = 0; i < cache_tree->n_subtrees; i++) {
		cache_subtree = &cache_tree->subtrees[i];

		node = rb_first(&cache_subtree->root);
		while (node) {
			key = CACHE_KEY(node);
			node = rb_next(node);

			cache_key_writeback(cache, key);
			cache_key_delete(key);
		}
	}
	writeback_ctx_end(cache, 0);
}

static int cache_kset_insert_tree(struct pcache_cache *cache, struct pcache_cache_kset_onmedia *kset_onmedia)
{
	struct pcache_cache_key_onmedia *key_onmedia;
	struct pcache_cache_subtree *cache_subtree;
	struct pcache_cache_key *key;
	int ret;
	u32 i;

	/* Iterate through all keys in the kset and write each back to storage */
	for (i = 0; i < kset_onmedia->key_num; i++) {
		key_onmedia = &kset_onmedia->data[i];

		key = cache_key_alloc(&cache->writeback_key_tree, GFP_NOIO);
		ret = cache_key_decode(cache, key_onmedia, key);
		if (ret) {
			cache_key_put(key);
			goto clear_tree;
		}

		cache_subtree = get_subtree(&cache->writeback_key_tree, key->off);
		spin_lock(&cache_subtree->tree_lock);
		cache_key_insert(&cache->writeback_key_tree, key, true);
		spin_unlock(&cache_subtree->tree_lock);
	}

	return 0;
clear_tree:
	cache_tree_clear(&cache->writeback_key_tree);
	return ret;
}

static void last_kset_writeback(struct pcache_cache *cache,
		struct pcache_cache_kset_onmedia *last_kset_onmedia)
{
	struct dm_pcache *pcache = CACHE_TO_PCACHE(cache);
	struct pcache_cache_segment *next_seg;

	pcache_dev_debug(pcache, "last kset, next: %u\n", last_kset_onmedia->next_cache_seg_id);

	next_seg = &cache->segments[last_kset_onmedia->next_cache_seg_id];

	mutex_lock(&cache->dirty_tail_lock);
	cache->dirty_tail.cache_seg = next_seg;
	cache->dirty_tail.seg_off = 0;
	cache_encode_dirty_tail(cache);
	mutex_unlock(&cache->dirty_tail_lock);
}

void cache_writeback_fn(struct work_struct *work)
{
	struct pcache_cache *cache = container_of(work, struct pcache_cache, writeback_work.work);
	struct dm_pcache *pcache = CACHE_TO_PCACHE(cache);
	struct pcache_cache_pos dirty_tail;
	struct pcache_cache_kset_onmedia *kset_onmedia;
	u32 delay;
	int ret;

	mutex_lock(&cache->writeback_lock);
	if (atomic_read(&cache->writeback_ctx.pending))
		goto unlock;

	if (pcache_is_stopping(pcache))
		goto unlock;

	kset_onmedia = (struct pcache_cache_kset_onmedia *)cache->wb_kset_onmedia_buf;

	mutex_lock(&cache->dirty_tail_lock);
	cache_pos_copy(&dirty_tail, &cache->dirty_tail);
	mutex_unlock(&cache->dirty_tail_lock);

	if (is_cache_clean(cache, &dirty_tail)) {
		delay = PCACHE_CACHE_WRITEBACK_INTERVAL;
		goto queue_work;
	}

	if (kset_onmedia->flags & PCACHE_KSET_FLAGS_LAST) {
		last_kset_writeback(cache, kset_onmedia);
		delay = 0;
		goto queue_work;
	}

	ret = cache_kset_insert_tree(cache, kset_onmedia);
	if (ret) {
		delay = PCACHE_CACHE_WRITEBACK_INTERVAL;
		goto queue_work;
	}

	cache_wb_tree_writeback(cache, get_kset_onmedia_size(kset_onmedia));
	delay = 0;
queue_work:
	queue_delayed_work(cache_get_wq(cache), &cache->writeback_work, delay);
unlock:
	mutex_unlock(&cache->writeback_lock);
}
