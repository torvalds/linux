/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _PCACHE_CACHE_H
#define _PCACHE_CACHE_H

#include "segment.h"

/* Garbage collection thresholds */
#define PCACHE_CACHE_GC_PERCENT_MIN       0                   /* Minimum GC percentage */
#define PCACHE_CACHE_GC_PERCENT_MAX       90                  /* Maximum GC percentage */
#define PCACHE_CACHE_GC_PERCENT_DEFAULT   70                  /* Default GC percentage */

#define PCACHE_CACHE_SUBTREE_SIZE		(4 * PCACHE_MB)     /* 4MB total tree size */
#define PCACHE_CACHE_SUBTREE_SIZE_MASK		0x3FFFFF            /* Mask for tree size */
#define PCACHE_CACHE_SUBTREE_SIZE_SHIFT		22                  /* Bit shift for tree size */

/* Maximum number of keys per key set */
#define PCACHE_KSET_KEYS_MAX		128
#define PCACHE_CACHE_SEGS_MAX		(1024 * 1024)	/* maximum cache size for each device is 16T */
#define PCACHE_KSET_ONMEDIA_SIZE_MAX	struct_size_t(struct pcache_cache_kset_onmedia, data, PCACHE_KSET_KEYS_MAX)
#define PCACHE_KSET_SIZE		(sizeof(struct pcache_cache_kset) + sizeof(struct pcache_cache_key_onmedia) * PCACHE_KSET_KEYS_MAX)

/* Maximum number of keys to clean in one round of clean_work */
#define PCACHE_CLEAN_KEYS_MAX             10

/* Writeback and garbage collection intervals in jiffies */
#define PCACHE_CACHE_WRITEBACK_INTERVAL   (5 * HZ)
#define PCACHE_CACHE_GC_INTERVAL          (5 * HZ)

/* Macro to get the cache key structure from an rb_node pointer */
#define CACHE_KEY(node)                (container_of(node, struct pcache_cache_key, rb_node))

struct pcache_cache_pos_onmedia {
	struct pcache_meta_header header;
	__u32 cache_seg_id;
	__u32 seg_off;
};

/* Offset and size definitions for cache segment control */
#define PCACHE_CACHE_SEG_CTRL_OFF     (PCACHE_SEG_INFO_SIZE * PCACHE_META_INDEX_MAX)
#define PCACHE_CACHE_SEG_CTRL_SIZE    (4 * PCACHE_KB)

struct pcache_cache_seg_gen {
	struct pcache_meta_header header;
	__u64 gen;
};

/* Control structure for cache segments */
struct pcache_cache_seg_ctrl {
	struct pcache_cache_seg_gen gen[PCACHE_META_INDEX_MAX];
	__u64	res[64];
};

#define PCACHE_CACHE_FLAGS_DATA_CRC			BIT(0)
#define PCACHE_CACHE_FLAGS_INIT_DONE			BIT(1)

#define PCACHE_CACHE_FLAGS_CACHE_MODE_MASK		GENMASK(5, 2)
#define PCACHE_CACHE_MODE_WRITEBACK			0
#define PCACHE_CACHE_MODE_WRITETHROUGH			1
#define PCACHE_CACHE_MODE_WRITEAROUND			2
#define PCACHE_CACHE_MODE_WRITEONLY			3

#define PCACHE_CACHE_FLAGS_GC_PERCENT_MASK		GENMASK(12, 6)

struct pcache_cache_info {
	struct pcache_meta_header header;
	__u32 seg_id;
	__u32 n_segs;
	__u32 flags;
	__u32 reserved;
};

struct pcache_cache_pos {
	struct pcache_cache_segment *cache_seg;
	u32 seg_off;
};

struct pcache_cache_segment {
	struct pcache_cache	*cache;
	u32			cache_seg_id;   /* Index in cache->segments */
	struct pcache_segment	segment;
	atomic_t		refs;

	struct pcache_segment_info cache_seg_info;
	struct mutex		info_lock;
	u32			info_index;

	spinlock_t		gen_lock;
	u64			gen;
	u64			gen_seq;
	u32			gen_index;

	struct pcache_cache_seg_ctrl *cache_seg_ctrl;
};

/* rbtree for cache entries */
struct pcache_cache_subtree {
	struct rb_root root;
	spinlock_t tree_lock;
};

struct pcache_cache_tree {
	struct pcache_cache		*cache;
	u32				n_subtrees;
	mempool_t			key_pool;
	struct pcache_cache_subtree	*subtrees;
};

extern struct kmem_cache *key_cache;

struct pcache_cache_key {
	struct pcache_cache_tree	*cache_tree;
	struct pcache_cache_subtree	*cache_subtree;
	struct kref			ref;
	struct rb_node			rb_node;
	struct list_head		list_node;
	u64				off;
	u32				len;
	u32				flags;
	struct pcache_cache_pos		cache_pos;
	u64				seg_gen;
};

#define PCACHE_CACHE_KEY_FLAGS_EMPTY		BIT(0)
#define PCACHE_CACHE_KEY_FLAGS_CLEAN		BIT(1)

struct pcache_cache_key_onmedia {
	__u64 off;
	__u32 len;
	__u32 flags;
	__u32 cache_seg_id;
	__u32 cache_seg_off;
	__u64 seg_gen;
	__u32 data_crc;
	__u32 reserved;
};

struct pcache_cache_kset_onmedia {
	__u32 crc;
	union {
		__u32 key_num;
		__u32 next_cache_seg_id;
	};
	__u64 magic;
	__u64 flags;
	struct pcache_cache_key_onmedia data[];
};

struct pcache_cache {
	struct pcache_backing_dev	*backing_dev;
	struct pcache_cache_dev		*cache_dev;
	struct pcache_cache_ctrl	*cache_ctrl;
	u64				dev_size;

	struct pcache_cache_data_head __percpu *data_heads;

	spinlock_t		key_head_lock;
	struct pcache_cache_pos	key_head;
	u32			n_ksets;
	struct pcache_cache_kset	*ksets;

	struct mutex		key_tail_lock;
	struct pcache_cache_pos	key_tail;
	u64			key_tail_seq;
	u32			key_tail_index;

	struct mutex		dirty_tail_lock;
	struct pcache_cache_pos	dirty_tail;
	u64			dirty_tail_seq;
	u32			dirty_tail_index;

	struct pcache_cache_tree	req_key_tree;
	struct work_struct	clean_work;

	struct mutex		writeback_lock;
	char wb_kset_onmedia_buf[PCACHE_KSET_ONMEDIA_SIZE_MAX];
	struct pcache_cache_tree	writeback_key_tree;
	struct delayed_work	writeback_work;
	struct {
		atomic_t pending;
		u32 advance;
		int ret;
	} writeback_ctx;

	char gc_kset_onmedia_buf[PCACHE_KSET_ONMEDIA_SIZE_MAX];
	struct delayed_work	gc_work;
	atomic_t		gc_errors;

	struct mutex			cache_info_lock;
	struct pcache_cache_info	cache_info;
	struct pcache_cache_info	*cache_info_addr;
	u32				info_index;

	u32			n_segs;
	unsigned long		*seg_map;
	u32			last_cache_seg;
	bool			cache_full;
	spinlock_t		seg_map_lock;
	struct pcache_cache_segment *segments;
};

struct workqueue_struct *cache_get_wq(struct pcache_cache *cache);

struct dm_pcache;
struct pcache_cache_options {
	u32	cache_mode:4;
	u32	data_crc:1;
};
int pcache_cache_start(struct dm_pcache *pcache);
void pcache_cache_stop(struct dm_pcache *pcache);

struct pcache_cache_ctrl {
	/* Updated by gc_thread */
	struct pcache_cache_pos_onmedia key_tail_pos[PCACHE_META_INDEX_MAX];

	/* Updated by writeback_thread */
	struct pcache_cache_pos_onmedia dirty_tail_pos[PCACHE_META_INDEX_MAX];
};

struct pcache_cache_data_head {
	struct pcache_cache_pos head_pos;
};

static inline u16 pcache_cache_get_gc_percent(struct pcache_cache *cache)
{
	return FIELD_GET(PCACHE_CACHE_FLAGS_GC_PERCENT_MASK, cache->cache_info.flags);
}

int pcache_cache_set_gc_percent(struct pcache_cache *cache, u8 percent);

/* cache key */
struct pcache_cache_key *cache_key_alloc(struct pcache_cache_tree *cache_tree, gfp_t gfp_mask);
void cache_key_init(struct pcache_cache_tree *cache_tree, struct pcache_cache_key *key);
void cache_key_get(struct pcache_cache_key *key);
void cache_key_put(struct pcache_cache_key *key);
int cache_key_append(struct pcache_cache *cache, struct pcache_cache_key *key, bool force_close);
void cache_key_insert(struct pcache_cache_tree *cache_tree, struct pcache_cache_key *key, bool fixup);
int cache_key_decode(struct pcache_cache *cache,
			struct pcache_cache_key_onmedia *key_onmedia,
			struct pcache_cache_key *key);
void cache_pos_advance(struct pcache_cache_pos *pos, u32 len);

#define PCACHE_KSET_FLAGS_LAST		BIT(0)
#define PCACHE_KSET_MAGIC		0x676894a64e164f1aULL

struct pcache_cache_kset {
	struct pcache_cache *cache;
	spinlock_t        kset_lock;
	struct delayed_work flush_work;
	struct pcache_cache_kset_onmedia kset_onmedia;
};

extern struct pcache_cache_kset_onmedia pcache_empty_kset;

#define SUBTREE_WALK_RET_OK		0
#define SUBTREE_WALK_RET_ERR		1
#define SUBTREE_WALK_RET_NEED_KEY	2
#define SUBTREE_WALK_RET_NEED_REQ	3
#define SUBTREE_WALK_RET_RESEARCH	4

struct pcache_cache_subtree_walk_ctx {
	struct pcache_cache_tree *cache_tree;
	struct rb_node *start_node;
	struct pcache_request *pcache_req;
	struct pcache_cache_key *key;
	u32	req_done;
	int	ret;

	/* pre-allocated key and backing_dev_req */
	struct pcache_cache_key		*pre_alloc_key;
	struct pcache_backing_dev_req	*pre_alloc_req;

	struct list_head *delete_key_list;
	struct list_head *submit_req_list;

	/*
	 *	  |--------|		key_tmp
	 * |====|			key
	 */
	int (*before)(struct pcache_cache_key *key, struct pcache_cache_key *key_tmp,
			struct pcache_cache_subtree_walk_ctx *ctx);

	/*
	 * |----------|			key_tmp
	 *		|=====|		key
	 */
	int (*after)(struct pcache_cache_key *key, struct pcache_cache_key *key_tmp,
			struct pcache_cache_subtree_walk_ctx *ctx);

	/*
	 *     |----------------|	key_tmp
	 * |===========|		key
	 */
	int (*overlap_tail)(struct pcache_cache_key *key, struct pcache_cache_key *key_tmp,
			struct pcache_cache_subtree_walk_ctx *ctx);

	/*
	 * |--------|			key_tmp
	 *   |==========|		key
	 */
	int (*overlap_head)(struct pcache_cache_key *key, struct pcache_cache_key *key_tmp,
			struct pcache_cache_subtree_walk_ctx *ctx);

	/*
	 *    |----|			key_tmp
	 * |==========|			key
	 */
	int (*overlap_contain)(struct pcache_cache_key *key, struct pcache_cache_key *key_tmp,
			struct pcache_cache_subtree_walk_ctx *ctx);

	/*
	 * |-----------|		key_tmp
	 *   |====|			key
	 */
	int (*overlap_contained)(struct pcache_cache_key *key, struct pcache_cache_key *key_tmp,
			struct pcache_cache_subtree_walk_ctx *ctx);

	int (*walk_finally)(struct pcache_cache_subtree_walk_ctx *ctx, int ret);
	bool (*walk_done)(struct pcache_cache_subtree_walk_ctx *ctx);
};

int cache_subtree_walk(struct pcache_cache_subtree_walk_ctx *ctx);
struct rb_node *cache_subtree_search(struct pcache_cache_subtree *cache_subtree, struct pcache_cache_key *key,
				  struct rb_node **parentp, struct rb_node ***newp,
				  struct list_head *delete_key_list);
int cache_kset_close(struct pcache_cache *cache, struct pcache_cache_kset *kset);
void clean_fn(struct work_struct *work);
void kset_flush_fn(struct work_struct *work);
int cache_replay(struct pcache_cache *cache);
int cache_tree_init(struct pcache_cache *cache, struct pcache_cache_tree *cache_tree, u32 n_subtrees);
void cache_tree_clear(struct pcache_cache_tree *cache_tree);
void cache_tree_exit(struct pcache_cache_tree *cache_tree);

/* cache segments */
struct pcache_cache_segment *get_cache_segment(struct pcache_cache *cache);
int cache_seg_init(struct pcache_cache *cache, u32 seg_id, u32 cache_seg_id,
		   bool new_cache);
void cache_seg_get(struct pcache_cache_segment *cache_seg);
void cache_seg_put(struct pcache_cache_segment *cache_seg);
void cache_seg_set_next_seg(struct pcache_cache_segment *cache_seg, u32 seg_id);

/* cache request*/
int pcache_cache_flush(struct pcache_cache *cache);
void miss_read_end_work_fn(struct work_struct *work);
int pcache_cache_handle_req(struct pcache_cache *cache, struct pcache_request *pcache_req);

/* gc */
void pcache_cache_gc_fn(struct work_struct *work);

/* writeback */
void cache_writeback_exit(struct pcache_cache *cache);
int cache_writeback_init(struct pcache_cache *cache);
void cache_writeback_fn(struct work_struct *work);

/* inline functions */
static inline struct pcache_cache_subtree *get_subtree(struct pcache_cache_tree *cache_tree, u64 off)
{
	if (cache_tree->n_subtrees == 1)
		return &cache_tree->subtrees[0];

	return &cache_tree->subtrees[off >> PCACHE_CACHE_SUBTREE_SIZE_SHIFT];
}

static inline void *cache_pos_addr(struct pcache_cache_pos *pos)
{
	return (pos->cache_seg->segment.data + pos->seg_off);
}

static inline void *get_key_head_addr(struct pcache_cache *cache)
{
	return cache_pos_addr(&cache->key_head);
}

static inline u32 get_kset_id(struct pcache_cache *cache, u64 off)
{
	u32 kset_id;

	div_u64_rem(off >> PCACHE_CACHE_SUBTREE_SIZE_SHIFT, cache->n_ksets, &kset_id);

	return kset_id;
}

static inline struct pcache_cache_kset *get_kset(struct pcache_cache *cache, u32 kset_id)
{
	return (void *)cache->ksets + PCACHE_KSET_SIZE * kset_id;
}

static inline struct pcache_cache_data_head *get_data_head(struct pcache_cache *cache)
{
	return this_cpu_ptr(cache->data_heads);
}

static inline bool cache_key_empty(struct pcache_cache_key *key)
{
	return key->flags & PCACHE_CACHE_KEY_FLAGS_EMPTY;
}

static inline bool cache_key_clean(struct pcache_cache_key *key)
{
	return key->flags & PCACHE_CACHE_KEY_FLAGS_CLEAN;
}

static inline void cache_pos_copy(struct pcache_cache_pos *dst, struct pcache_cache_pos *src)
{
	memcpy(dst, src, sizeof(struct pcache_cache_pos));
}

/**
 * cache_seg_is_ctrl_seg - Checks if a cache segment is a cache ctrl segment.
 * @cache_seg_id: ID of the cache segment.
 *
 * Returns true if the cache segment ID corresponds to a cache ctrl segment.
 *
 * Note: We extend the segment control of the first cache segment
 * (cache segment ID 0) to serve as the cache control (pcache_cache_ctrl)
 * for the entire PCACHE cache. This function determines whether the given
 * cache segment is the one storing the pcache_cache_ctrl information.
 */
static inline bool cache_seg_is_ctrl_seg(u32 cache_seg_id)
{
	return (cache_seg_id == 0);
}

/**
 * cache_key_cutfront - Cuts a specified length from the front of a cache key.
 * @key: Pointer to pcache_cache_key structure.
 * @cut_len: Length to cut from the front.
 *
 * Advances the cache key position by cut_len and adjusts offset and length accordingly.
 */
static inline void cache_key_cutfront(struct pcache_cache_key *key, u32 cut_len)
{
	if (key->cache_pos.cache_seg)
		cache_pos_advance(&key->cache_pos, cut_len);

	key->off += cut_len;
	key->len -= cut_len;
}

/**
 * cache_key_cutback - Cuts a specified length from the back of a cache key.
 * @key: Pointer to pcache_cache_key structure.
 * @cut_len: Length to cut from the back.
 *
 * Reduces the length of the cache key by cut_len.
 */
static inline void cache_key_cutback(struct pcache_cache_key *key, u32 cut_len)
{
	key->len -= cut_len;
}

static inline void cache_key_delete(struct pcache_cache_key *key)
{
	struct pcache_cache_subtree *cache_subtree;

	cache_subtree = key->cache_subtree;
	BUG_ON(!cache_subtree);

	rb_erase(&key->rb_node, &cache_subtree->root);
	key->flags = 0;
	cache_key_put(key);
}

static inline bool cache_data_crc_on(struct pcache_cache *cache)
{
	return (cache->cache_info.flags & PCACHE_CACHE_FLAGS_DATA_CRC);
}

static inline u32 cache_mode_get(struct pcache_cache *cache)
{
	return FIELD_GET(PCACHE_CACHE_FLAGS_CACHE_MODE_MASK, cache->cache_info.flags);
}

static inline void cache_mode_set(struct pcache_cache *cache, u32 cache_mode)
{
	cache->cache_info.flags &= ~PCACHE_CACHE_FLAGS_CACHE_MODE_MASK;
	cache->cache_info.flags |= FIELD_PREP(PCACHE_CACHE_FLAGS_CACHE_MODE_MASK, cache_mode);
}

/**
 * cache_key_data_crc - Calculates CRC for data in a cache key.
 * @key: Pointer to the pcache_cache_key structure.
 *
 * Returns the CRC-32 checksum of the data within the cache key's position.
 */
static inline u32 cache_key_data_crc(struct pcache_cache_key *key)
{
	void *data;

	data = cache_pos_addr(&key->cache_pos);

	return crc32c(PCACHE_CRC_SEED, data, key->len);
}

static inline u32 cache_kset_crc(struct pcache_cache_kset_onmedia *kset_onmedia)
{
	u32 crc_size;

	if (kset_onmedia->flags & PCACHE_KSET_FLAGS_LAST)
		crc_size = sizeof(struct pcache_cache_kset_onmedia) - 4;
	else
		crc_size = struct_size(kset_onmedia, data, kset_onmedia->key_num) - 4;

	return crc32c(PCACHE_CRC_SEED, (void *)kset_onmedia + 4, crc_size);
}

static inline u32 get_kset_onmedia_size(struct pcache_cache_kset_onmedia *kset_onmedia)
{
	return struct_size_t(struct pcache_cache_kset_onmedia, data, kset_onmedia->key_num);
}

/**
 * cache_seg_remain - Computes remaining space in a cache segment.
 * @pos: Pointer to pcache_cache_pos structure.
 *
 * Returns the amount of remaining space in the segment data starting from
 * the current position offset.
 */
static inline u32 cache_seg_remain(struct pcache_cache_pos *pos)
{
	struct pcache_cache_segment *cache_seg;
	struct pcache_segment *segment;
	u32 seg_remain;

	cache_seg = pos->cache_seg;
	segment = &cache_seg->segment;
	seg_remain = segment->data_size - pos->seg_off;

	return seg_remain;
}

/**
 * cache_key_invalid - Checks if a cache key is invalid.
 * @key: Pointer to pcache_cache_key structure.
 *
 * Returns true if the cache key is invalid due to its generation being
 * less than the generation of its segment; otherwise returns false.
 *
 * When the GC (garbage collection) thread identifies a segment
 * as reclaimable, it increments the segment's generation (gen). However,
 * it does not immediately remove all related cache keys. When accessing
 * such a cache key, this function can be used to determine if the cache
 * key has already become invalid.
 */
static inline bool cache_key_invalid(struct pcache_cache_key *key)
{
	if (cache_key_empty(key))
		return false;

	return (key->seg_gen < key->cache_pos.cache_seg->gen);
}

/**
 * cache_key_lstart - Retrieves the logical start offset of a cache key.
 * @key: Pointer to pcache_cache_key structure.
 *
 * Returns the logical start offset for the cache key.
 */
static inline u64 cache_key_lstart(struct pcache_cache_key *key)
{
	return key->off;
}

/**
 * cache_key_lend - Retrieves the logical end offset of a cache key.
 * @key: Pointer to pcache_cache_key structure.
 *
 * Returns the logical end offset for the cache key.
 */
static inline u64 cache_key_lend(struct pcache_cache_key *key)
{
	return key->off + key->len;
}

static inline void cache_key_copy(struct pcache_cache_key *key_dst, struct pcache_cache_key *key_src)
{
	key_dst->off = key_src->off;
	key_dst->len = key_src->len;
	key_dst->seg_gen = key_src->seg_gen;
	key_dst->cache_tree = key_src->cache_tree;
	key_dst->cache_subtree = key_src->cache_subtree;
	key_dst->flags = key_src->flags;

	cache_pos_copy(&key_dst->cache_pos, &key_src->cache_pos);
}

/**
 * cache_pos_onmedia_crc - Calculates the CRC for an on-media cache position.
 * @pos_om: Pointer to pcache_cache_pos_onmedia structure.
 *
 * Calculates the CRC-32 checksum of the position, excluding the first 4 bytes.
 * Returns the computed CRC value.
 */
static inline u32 cache_pos_onmedia_crc(struct pcache_cache_pos_onmedia *pos_om)
{
	return pcache_meta_crc(&pos_om->header, sizeof(struct pcache_cache_pos_onmedia));
}

void cache_pos_encode(struct pcache_cache *cache,
			     struct pcache_cache_pos_onmedia *pos_onmedia,
			     struct pcache_cache_pos *pos, u64 seq, u32 *index);
int cache_pos_decode(struct pcache_cache *cache,
			    struct pcache_cache_pos_onmedia *pos_onmedia,
			    struct pcache_cache_pos *pos, u64 *seq, u32 *index);

static inline void cache_encode_key_tail(struct pcache_cache *cache)
{
	cache_pos_encode(cache, cache->cache_ctrl->key_tail_pos,
			&cache->key_tail, ++cache->key_tail_seq,
			&cache->key_tail_index);
}

static inline int cache_decode_key_tail(struct pcache_cache *cache)
{
	return cache_pos_decode(cache, cache->cache_ctrl->key_tail_pos,
				&cache->key_tail, &cache->key_tail_seq,
				&cache->key_tail_index);
}

static inline void cache_encode_dirty_tail(struct pcache_cache *cache)
{
	cache_pos_encode(cache, cache->cache_ctrl->dirty_tail_pos,
			&cache->dirty_tail, ++cache->dirty_tail_seq,
			&cache->dirty_tail_index);
}

static inline int cache_decode_dirty_tail(struct pcache_cache *cache)
{
	return cache_pos_decode(cache, cache->cache_ctrl->dirty_tail_pos,
				&cache->dirty_tail, &cache->dirty_tail_seq,
				&cache->dirty_tail_index);
}

int pcache_cache_init(void);
void pcache_cache_exit(void);
#endif /* _PCACHE_CACHE_H */
