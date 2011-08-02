/*
 * zcache.c
 *
 * Copyright (c) 2010,2011, Dan Magenheimer, Oracle Corp.
 * Copyright (c) 2010,2011, Nitin Gupta
 *
 * Zcache provides an in-kernel "host implementation" for transcendent memory
 * and, thus indirectly, for cleancache and frontswap.  Zcache includes two
 * page-accessible memory [1] interfaces, both utilizing lzo1x compression:
 * 1) "compression buddies" ("zbud") is used for ephemeral pages
 * 2) xvmalloc is used for persistent pages.
 * Xvmalloc (based on the TLSF allocator) has very low fragmentation
 * so maximizes space efficiency, while zbud allows pairs (and potentially,
 * in the future, more than a pair of) compressed pages to be closely linked
 * so that reclaiming can be done via the kernel's physical-page-oriented
 * "shrinker" interface.
 *
 * [1] For a definition of page-accessible memory (aka PAM), see:
 *   http://marc.info/?l=linux-mm&m=127811271605009
 */

#include <linux/cpu.h>
#include <linux/highmem.h>
#include <linux/list.h>
#include <linux/lzo.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include "tmem.h"

#include "../zram/xvmalloc.h" /* if built in drivers/staging */

#if (!defined(CONFIG_CLEANCACHE) && !defined(CONFIG_FRONTSWAP))
#error "zcache is useless without CONFIG_CLEANCACHE or CONFIG_FRONTSWAP"
#endif
#ifdef CONFIG_CLEANCACHE
#include <linux/cleancache.h>
#endif
#ifdef CONFIG_FRONTSWAP
#include <linux/frontswap.h>
#endif

#if 0
/* this is more aggressive but may cause other problems? */
#define ZCACHE_GFP_MASK	(GFP_ATOMIC | __GFP_NORETRY | __GFP_NOWARN)
#else
#define ZCACHE_GFP_MASK \
	(__GFP_FS | __GFP_NORETRY | __GFP_NOWARN | __GFP_NOMEMALLOC)
#endif

#define MAX_POOLS_PER_CLIENT 16

#define MAX_CLIENTS 16
#define LOCAL_CLIENT ((uint16_t)-1)
struct zcache_client {
	struct tmem_pool *tmem_pools[MAX_POOLS_PER_CLIENT];
	struct xv_pool *xvpool;
	bool allocated;
	atomic_t refcount;
};

static struct zcache_client zcache_host;
static struct zcache_client zcache_clients[MAX_CLIENTS];

static inline uint16_t get_client_id_from_client(struct zcache_client *cli)
{
	BUG_ON(cli == NULL);
	if (cli == &zcache_host)
		return LOCAL_CLIENT;
	return cli - &zcache_clients[0];
}

static inline bool is_local_client(struct zcache_client *cli)
{
	return cli == &zcache_host;
}

/**********
 * Compression buddies ("zbud") provides for packing two (or, possibly
 * in the future, more) compressed ephemeral pages into a single "raw"
 * (physical) page and tracking them with data structures so that
 * the raw pages can be easily reclaimed.
 *
 * A zbud page ("zbpg") is an aligned page containing a list_head,
 * a lock, and two "zbud headers".  The remainder of the physical
 * page is divided up into aligned 64-byte "chunks" which contain
 * the compressed data for zero, one, or two zbuds.  Each zbpg
 * resides on: (1) an "unused list" if it has no zbuds; (2) a
 * "buddied" list if it is fully populated  with two zbuds; or
 * (3) one of PAGE_SIZE/64 "unbuddied" lists indexed by how many chunks
 * the one unbuddied zbud uses.  The data inside a zbpg cannot be
 * read or written unless the zbpg's lock is held.
 */

#define ZBH_SENTINEL  0x43214321
#define ZBPG_SENTINEL  0xdeadbeef

#define ZBUD_MAX_BUDS 2

struct zbud_hdr {
	uint16_t client_id;
	uint16_t pool_id;
	struct tmem_oid oid;
	uint32_t index;
	uint16_t size; /* compressed size in bytes, zero means unused */
	DECL_SENTINEL
};

struct zbud_page {
	struct list_head bud_list;
	spinlock_t lock;
	struct zbud_hdr buddy[ZBUD_MAX_BUDS];
	DECL_SENTINEL
	/* followed by NUM_CHUNK aligned CHUNK_SIZE-byte chunks */
};

#define CHUNK_SHIFT	6
#define CHUNK_SIZE	(1 << CHUNK_SHIFT)
#define CHUNK_MASK	(~(CHUNK_SIZE-1))
#define NCHUNKS		(((PAGE_SIZE - sizeof(struct zbud_page)) & \
				CHUNK_MASK) >> CHUNK_SHIFT)
#define MAX_CHUNK	(NCHUNKS-1)

static struct {
	struct list_head list;
	unsigned count;
} zbud_unbuddied[NCHUNKS];
/* list N contains pages with N chunks USED and NCHUNKS-N unused */
/* element 0 is never used but optimizing that isn't worth it */
static unsigned long zbud_cumul_chunk_counts[NCHUNKS];

struct list_head zbud_buddied_list;
static unsigned long zcache_zbud_buddied_count;

/* protects the buddied list and all unbuddied lists */
static DEFINE_SPINLOCK(zbud_budlists_spinlock);

static LIST_HEAD(zbpg_unused_list);
static unsigned long zcache_zbpg_unused_list_count;

/* protects the unused page list */
static DEFINE_SPINLOCK(zbpg_unused_list_spinlock);

static atomic_t zcache_zbud_curr_raw_pages;
static atomic_t zcache_zbud_curr_zpages;
static unsigned long zcache_zbud_curr_zbytes;
static unsigned long zcache_zbud_cumul_zpages;
static unsigned long zcache_zbud_cumul_zbytes;
static unsigned long zcache_compress_poor;
static unsigned long zcache_mean_compress_poor;

/* forward references */
static void *zcache_get_free_page(void);
static void zcache_free_page(void *p);

/*
 * zbud helper functions
 */

static inline unsigned zbud_max_buddy_size(void)
{
	return MAX_CHUNK << CHUNK_SHIFT;
}

static inline unsigned zbud_size_to_chunks(unsigned size)
{
	BUG_ON(size == 0 || size > zbud_max_buddy_size());
	return (size + CHUNK_SIZE - 1) >> CHUNK_SHIFT;
}

static inline int zbud_budnum(struct zbud_hdr *zh)
{
	unsigned offset = (unsigned long)zh & (PAGE_SIZE - 1);
	struct zbud_page *zbpg = NULL;
	unsigned budnum = -1U;
	int i;

	for (i = 0; i < ZBUD_MAX_BUDS; i++)
		if (offset == offsetof(typeof(*zbpg), buddy[i])) {
			budnum = i;
			break;
		}
	BUG_ON(budnum == -1U);
	return budnum;
}

static char *zbud_data(struct zbud_hdr *zh, unsigned size)
{
	struct zbud_page *zbpg;
	char *p;
	unsigned budnum;

	ASSERT_SENTINEL(zh, ZBH);
	budnum = zbud_budnum(zh);
	BUG_ON(size == 0 || size > zbud_max_buddy_size());
	zbpg = container_of(zh, struct zbud_page, buddy[budnum]);
	ASSERT_SPINLOCK(&zbpg->lock);
	p = (char *)zbpg;
	if (budnum == 0)
		p += ((sizeof(struct zbud_page) + CHUNK_SIZE - 1) &
							CHUNK_MASK);
	else if (budnum == 1)
		p += PAGE_SIZE - ((size + CHUNK_SIZE - 1) & CHUNK_MASK);
	return p;
}

/*
 * zbud raw page management
 */

static struct zbud_page *zbud_alloc_raw_page(void)
{
	struct zbud_page *zbpg = NULL;
	struct zbud_hdr *zh0, *zh1;
	bool recycled = 0;

	/* if any pages on the zbpg list, use one */
	spin_lock(&zbpg_unused_list_spinlock);
	if (!list_empty(&zbpg_unused_list)) {
		zbpg = list_first_entry(&zbpg_unused_list,
				struct zbud_page, bud_list);
		list_del_init(&zbpg->bud_list);
		zcache_zbpg_unused_list_count--;
		recycled = 1;
	}
	spin_unlock(&zbpg_unused_list_spinlock);
	if (zbpg == NULL)
		/* none on zbpg list, try to get a kernel page */
		zbpg = zcache_get_free_page();
	if (likely(zbpg != NULL)) {
		INIT_LIST_HEAD(&zbpg->bud_list);
		zh0 = &zbpg->buddy[0]; zh1 = &zbpg->buddy[1];
		spin_lock_init(&zbpg->lock);
		if (recycled) {
			ASSERT_INVERTED_SENTINEL(zbpg, ZBPG);
			SET_SENTINEL(zbpg, ZBPG);
			BUG_ON(zh0->size != 0 || tmem_oid_valid(&zh0->oid));
			BUG_ON(zh1->size != 0 || tmem_oid_valid(&zh1->oid));
		} else {
			atomic_inc(&zcache_zbud_curr_raw_pages);
			INIT_LIST_HEAD(&zbpg->bud_list);
			SET_SENTINEL(zbpg, ZBPG);
			zh0->size = 0; zh1->size = 0;
			tmem_oid_set_invalid(&zh0->oid);
			tmem_oid_set_invalid(&zh1->oid);
		}
	}
	return zbpg;
}

static void zbud_free_raw_page(struct zbud_page *zbpg)
{
	struct zbud_hdr *zh0 = &zbpg->buddy[0], *zh1 = &zbpg->buddy[1];

	ASSERT_SENTINEL(zbpg, ZBPG);
	BUG_ON(!list_empty(&zbpg->bud_list));
	ASSERT_SPINLOCK(&zbpg->lock);
	BUG_ON(zh0->size != 0 || tmem_oid_valid(&zh0->oid));
	BUG_ON(zh1->size != 0 || tmem_oid_valid(&zh1->oid));
	INVERT_SENTINEL(zbpg, ZBPG);
	spin_unlock(&zbpg->lock);
	spin_lock(&zbpg_unused_list_spinlock);
	list_add(&zbpg->bud_list, &zbpg_unused_list);
	zcache_zbpg_unused_list_count++;
	spin_unlock(&zbpg_unused_list_spinlock);
}

/*
 * core zbud handling routines
 */

static unsigned zbud_free(struct zbud_hdr *zh)
{
	unsigned size;

	ASSERT_SENTINEL(zh, ZBH);
	BUG_ON(!tmem_oid_valid(&zh->oid));
	size = zh->size;
	BUG_ON(zh->size == 0 || zh->size > zbud_max_buddy_size());
	zh->size = 0;
	tmem_oid_set_invalid(&zh->oid);
	INVERT_SENTINEL(zh, ZBH);
	zcache_zbud_curr_zbytes -= size;
	atomic_dec(&zcache_zbud_curr_zpages);
	return size;
}

static void zbud_free_and_delist(struct zbud_hdr *zh)
{
	unsigned chunks;
	struct zbud_hdr *zh_other;
	unsigned budnum = zbud_budnum(zh), size;
	struct zbud_page *zbpg =
		container_of(zh, struct zbud_page, buddy[budnum]);

	spin_lock(&zbpg->lock);
	if (list_empty(&zbpg->bud_list)) {
		/* ignore zombie page... see zbud_evict_pages() */
		spin_unlock(&zbpg->lock);
		return;
	}
	size = zbud_free(zh);
	ASSERT_SPINLOCK(&zbpg->lock);
	zh_other = &zbpg->buddy[(budnum == 0) ? 1 : 0];
	if (zh_other->size == 0) { /* was unbuddied: unlist and free */
		chunks = zbud_size_to_chunks(size) ;
		spin_lock(&zbud_budlists_spinlock);
		BUG_ON(list_empty(&zbud_unbuddied[chunks].list));
		list_del_init(&zbpg->bud_list);
		zbud_unbuddied[chunks].count--;
		spin_unlock(&zbud_budlists_spinlock);
		zbud_free_raw_page(zbpg);
	} else { /* was buddied: move remaining buddy to unbuddied list */
		chunks = zbud_size_to_chunks(zh_other->size) ;
		spin_lock(&zbud_budlists_spinlock);
		list_del_init(&zbpg->bud_list);
		zcache_zbud_buddied_count--;
		list_add_tail(&zbpg->bud_list, &zbud_unbuddied[chunks].list);
		zbud_unbuddied[chunks].count++;
		spin_unlock(&zbud_budlists_spinlock);
		spin_unlock(&zbpg->lock);
	}
}

static struct zbud_hdr *zbud_create(uint16_t client_id, uint16_t pool_id,
					struct tmem_oid *oid,
					uint32_t index, struct page *page,
					void *cdata, unsigned size)
{
	struct zbud_hdr *zh0, *zh1, *zh = NULL;
	struct zbud_page *zbpg = NULL, *ztmp;
	unsigned nchunks;
	char *to;
	int i, found_good_buddy = 0;

	nchunks = zbud_size_to_chunks(size) ;
	for (i = MAX_CHUNK - nchunks + 1; i > 0; i--) {
		spin_lock(&zbud_budlists_spinlock);
		if (!list_empty(&zbud_unbuddied[i].list)) {
			list_for_each_entry_safe(zbpg, ztmp,
				    &zbud_unbuddied[i].list, bud_list) {
				if (spin_trylock(&zbpg->lock)) {
					found_good_buddy = i;
					goto found_unbuddied;
				}
			}
		}
		spin_unlock(&zbud_budlists_spinlock);
	}
	/* didn't find a good buddy, try allocating a new page */
	zbpg = zbud_alloc_raw_page();
	if (unlikely(zbpg == NULL))
		goto out;
	/* ok, have a page, now compress the data before taking locks */
	spin_lock(&zbpg->lock);
	spin_lock(&zbud_budlists_spinlock);
	list_add_tail(&zbpg->bud_list, &zbud_unbuddied[nchunks].list);
	zbud_unbuddied[nchunks].count++;
	zh = &zbpg->buddy[0];
	goto init_zh;

found_unbuddied:
	ASSERT_SPINLOCK(&zbpg->lock);
	zh0 = &zbpg->buddy[0]; zh1 = &zbpg->buddy[1];
	BUG_ON(!((zh0->size == 0) ^ (zh1->size == 0)));
	if (zh0->size != 0) { /* buddy0 in use, buddy1 is vacant */
		ASSERT_SENTINEL(zh0, ZBH);
		zh = zh1;
	} else if (zh1->size != 0) { /* buddy1 in use, buddy0 is vacant */
		ASSERT_SENTINEL(zh1, ZBH);
		zh = zh0;
	} else
		BUG();
	list_del_init(&zbpg->bud_list);
	zbud_unbuddied[found_good_buddy].count--;
	list_add_tail(&zbpg->bud_list, &zbud_buddied_list);
	zcache_zbud_buddied_count++;

init_zh:
	SET_SENTINEL(zh, ZBH);
	zh->size = size;
	zh->index = index;
	zh->oid = *oid;
	zh->pool_id = pool_id;
	zh->client_id = client_id;
	/* can wait to copy the data until the list locks are dropped */
	spin_unlock(&zbud_budlists_spinlock);

	to = zbud_data(zh, size);
	memcpy(to, cdata, size);
	spin_unlock(&zbpg->lock);
	zbud_cumul_chunk_counts[nchunks]++;
	atomic_inc(&zcache_zbud_curr_zpages);
	zcache_zbud_cumul_zpages++;
	zcache_zbud_curr_zbytes += size;
	zcache_zbud_cumul_zbytes += size;
out:
	return zh;
}

static int zbud_decompress(struct page *page, struct zbud_hdr *zh)
{
	struct zbud_page *zbpg;
	unsigned budnum = zbud_budnum(zh);
	size_t out_len = PAGE_SIZE;
	char *to_va, *from_va;
	unsigned size;
	int ret = 0;

	zbpg = container_of(zh, struct zbud_page, buddy[budnum]);
	spin_lock(&zbpg->lock);
	if (list_empty(&zbpg->bud_list)) {
		/* ignore zombie page... see zbud_evict_pages() */
		ret = -EINVAL;
		goto out;
	}
	ASSERT_SENTINEL(zh, ZBH);
	BUG_ON(zh->size == 0 || zh->size > zbud_max_buddy_size());
	to_va = kmap_atomic(page, KM_USER0);
	size = zh->size;
	from_va = zbud_data(zh, size);
	ret = lzo1x_decompress_safe(from_va, size, to_va, &out_len);
	BUG_ON(ret != LZO_E_OK);
	BUG_ON(out_len != PAGE_SIZE);
	kunmap_atomic(to_va, KM_USER0);
out:
	spin_unlock(&zbpg->lock);
	return ret;
}

/*
 * The following routines handle shrinking of ephemeral pages by evicting
 * pages "least valuable" first.
 */

static unsigned long zcache_evicted_raw_pages;
static unsigned long zcache_evicted_buddied_pages;
static unsigned long zcache_evicted_unbuddied_pages;

static struct tmem_pool *zcache_get_pool_by_id(uint16_t cli_id,
						uint16_t poolid);
static void zcache_put_pool(struct tmem_pool *pool);

/*
 * Flush and free all zbuds in a zbpg, then free the pageframe
 */
static void zbud_evict_zbpg(struct zbud_page *zbpg)
{
	struct zbud_hdr *zh;
	int i, j;
	uint32_t pool_id[ZBUD_MAX_BUDS], client_id[ZBUD_MAX_BUDS];
	uint32_t index[ZBUD_MAX_BUDS];
	struct tmem_oid oid[ZBUD_MAX_BUDS];
	struct tmem_pool *pool;

	ASSERT_SPINLOCK(&zbpg->lock);
	BUG_ON(!list_empty(&zbpg->bud_list));
	for (i = 0, j = 0; i < ZBUD_MAX_BUDS; i++) {
		zh = &zbpg->buddy[i];
		if (zh->size) {
			client_id[j] = zh->client_id;
			pool_id[j] = zh->pool_id;
			oid[j] = zh->oid;
			index[j] = zh->index;
			j++;
			zbud_free(zh);
		}
	}
	spin_unlock(&zbpg->lock);
	for (i = 0; i < j; i++) {
		pool = zcache_get_pool_by_id(client_id[i], pool_id[i]);
		if (pool != NULL) {
			tmem_flush_page(pool, &oid[i], index[i]);
			zcache_put_pool(pool);
		}
	}
	ASSERT_SENTINEL(zbpg, ZBPG);
	spin_lock(&zbpg->lock);
	zbud_free_raw_page(zbpg);
}

/*
 * Free nr pages.  This code is funky because we want to hold the locks
 * protecting various lists for as short a time as possible, and in some
 * circumstances the list may change asynchronously when the list lock is
 * not held.  In some cases we also trylock not only to avoid waiting on a
 * page in use by another cpu, but also to avoid potential deadlock due to
 * lock inversion.
 */
static void zbud_evict_pages(int nr)
{
	struct zbud_page *zbpg;
	int i;

	/* first try freeing any pages on unused list */
retry_unused_list:
	spin_lock_bh(&zbpg_unused_list_spinlock);
	if (!list_empty(&zbpg_unused_list)) {
		/* can't walk list here, since it may change when unlocked */
		zbpg = list_first_entry(&zbpg_unused_list,
				struct zbud_page, bud_list);
		list_del_init(&zbpg->bud_list);
		zcache_zbpg_unused_list_count--;
		atomic_dec(&zcache_zbud_curr_raw_pages);
		spin_unlock_bh(&zbpg_unused_list_spinlock);
		zcache_free_page(zbpg);
		zcache_evicted_raw_pages++;
		if (--nr <= 0)
			goto out;
		goto retry_unused_list;
	}
	spin_unlock_bh(&zbpg_unused_list_spinlock);

	/* now try freeing unbuddied pages, starting with least space avail */
	for (i = 0; i < MAX_CHUNK; i++) {
retry_unbud_list_i:
		spin_lock_bh(&zbud_budlists_spinlock);
		if (list_empty(&zbud_unbuddied[i].list)) {
			spin_unlock_bh(&zbud_budlists_spinlock);
			continue;
		}
		list_for_each_entry(zbpg, &zbud_unbuddied[i].list, bud_list) {
			if (unlikely(!spin_trylock(&zbpg->lock)))
				continue;
			list_del_init(&zbpg->bud_list);
			zbud_unbuddied[i].count--;
			spin_unlock(&zbud_budlists_spinlock);
			zcache_evicted_unbuddied_pages++;
			/* want budlists unlocked when doing zbpg eviction */
			zbud_evict_zbpg(zbpg);
			local_bh_enable();
			if (--nr <= 0)
				goto out;
			goto retry_unbud_list_i;
		}
		spin_unlock_bh(&zbud_budlists_spinlock);
	}

	/* as a last resort, free buddied pages */
retry_bud_list:
	spin_lock_bh(&zbud_budlists_spinlock);
	if (list_empty(&zbud_buddied_list)) {
		spin_unlock_bh(&zbud_budlists_spinlock);
		goto out;
	}
	list_for_each_entry(zbpg, &zbud_buddied_list, bud_list) {
		if (unlikely(!spin_trylock(&zbpg->lock)))
			continue;
		list_del_init(&zbpg->bud_list);
		zcache_zbud_buddied_count--;
		spin_unlock(&zbud_budlists_spinlock);
		zcache_evicted_buddied_pages++;
		/* want budlists unlocked when doing zbpg eviction */
		zbud_evict_zbpg(zbpg);
		local_bh_enable();
		if (--nr <= 0)
			goto out;
		goto retry_bud_list;
	}
	spin_unlock_bh(&zbud_budlists_spinlock);
out:
	return;
}

static void zbud_init(void)
{
	int i;

	INIT_LIST_HEAD(&zbud_buddied_list);
	zcache_zbud_buddied_count = 0;
	for (i = 0; i < NCHUNKS; i++) {
		INIT_LIST_HEAD(&zbud_unbuddied[i].list);
		zbud_unbuddied[i].count = 0;
	}
}

#ifdef CONFIG_SYSFS
/*
 * These sysfs routines show a nice distribution of how many zbpg's are
 * currently (and have ever been placed) in each unbuddied list.  It's fun
 * to watch but can probably go away before final merge.
 */
static int zbud_show_unbuddied_list_counts(char *buf)
{
	int i;
	char *p = buf;

	for (i = 0; i < NCHUNKS; i++)
		p += sprintf(p, "%u ", zbud_unbuddied[i].count);
	return p - buf;
}

static int zbud_show_cumul_chunk_counts(char *buf)
{
	unsigned long i, chunks = 0, total_chunks = 0, sum_total_chunks = 0;
	unsigned long total_chunks_lte_21 = 0, total_chunks_lte_32 = 0;
	unsigned long total_chunks_lte_42 = 0;
	char *p = buf;

	for (i = 0; i < NCHUNKS; i++) {
		p += sprintf(p, "%lu ", zbud_cumul_chunk_counts[i]);
		chunks += zbud_cumul_chunk_counts[i];
		total_chunks += zbud_cumul_chunk_counts[i];
		sum_total_chunks += i * zbud_cumul_chunk_counts[i];
		if (i == 21)
			total_chunks_lte_21 = total_chunks;
		if (i == 32)
			total_chunks_lte_32 = total_chunks;
		if (i == 42)
			total_chunks_lte_42 = total_chunks;
	}
	p += sprintf(p, "<=21:%lu <=32:%lu <=42:%lu, mean:%lu\n",
		total_chunks_lte_21, total_chunks_lte_32, total_chunks_lte_42,
		chunks == 0 ? 0 : sum_total_chunks / chunks);
	return p - buf;
}
#endif

/**********
 * This "zv" PAM implementation combines the TLSF-based xvMalloc
 * with lzo1x compression to maximize the amount of data that can
 * be packed into a physical page.
 *
 * Zv represents a PAM page with the index and object (plus a "size" value
 * necessary for decompression) immediately preceding the compressed data.
 */

#define ZVH_SENTINEL  0x43214321

struct zv_hdr {
	uint32_t pool_id;
	struct tmem_oid oid;
	uint32_t index;
	DECL_SENTINEL
};

/* rudimentary policy limits */
/* total number of persistent pages may not exceed this percentage */
static unsigned int zv_page_count_policy_percent = 75;
/*
 * byte count defining poor compression; pages with greater zsize will be
 * rejected
 */
static unsigned int zv_max_zsize = (PAGE_SIZE / 8) * 7;
/*
 * byte count defining poor *mean* compression; pages with greater zsize
 * will be rejected until sufficient better-compressed pages are accepted
 * driving the man below this threshold
 */
static unsigned int zv_max_mean_zsize = (PAGE_SIZE / 8) * 5;

static unsigned long zv_curr_dist_counts[NCHUNKS];
static unsigned long zv_cumul_dist_counts[NCHUNKS];

static struct zv_hdr *zv_create(struct xv_pool *xvpool, uint32_t pool_id,
				struct tmem_oid *oid, uint32_t index,
				void *cdata, unsigned clen)
{
	struct page *page;
	struct zv_hdr *zv = NULL;
	uint32_t offset;
	int alloc_size = clen + sizeof(struct zv_hdr);
	int chunks = (alloc_size + (CHUNK_SIZE - 1)) >> CHUNK_SHIFT;
	int ret;

	BUG_ON(!irqs_disabled());
	BUG_ON(chunks >= NCHUNKS);
	ret = xv_malloc(xvpool, alloc_size,
			&page, &offset, ZCACHE_GFP_MASK);
	if (unlikely(ret))
		goto out;
	zv_curr_dist_counts[chunks]++;
	zv_cumul_dist_counts[chunks]++;
	zv = kmap_atomic(page, KM_USER0) + offset;
	zv->index = index;
	zv->oid = *oid;
	zv->pool_id = pool_id;
	SET_SENTINEL(zv, ZVH);
	memcpy((char *)zv + sizeof(struct zv_hdr), cdata, clen);
	kunmap_atomic(zv, KM_USER0);
out:
	return zv;
}

static void zv_free(struct xv_pool *xvpool, struct zv_hdr *zv)
{
	unsigned long flags;
	struct page *page;
	uint32_t offset;
	uint16_t size = xv_get_object_size(zv);
	int chunks = (size + (CHUNK_SIZE - 1)) >> CHUNK_SHIFT;

	ASSERT_SENTINEL(zv, ZVH);
	BUG_ON(chunks >= NCHUNKS);
	zv_curr_dist_counts[chunks]--;
	size -= sizeof(*zv);
	BUG_ON(size == 0);
	INVERT_SENTINEL(zv, ZVH);
	page = virt_to_page(zv);
	offset = (unsigned long)zv & ~PAGE_MASK;
	local_irq_save(flags);
	xv_free(xvpool, page, offset);
	local_irq_restore(flags);
}

static void zv_decompress(struct page *page, struct zv_hdr *zv)
{
	size_t clen = PAGE_SIZE;
	char *to_va;
	unsigned size;
	int ret;

	ASSERT_SENTINEL(zv, ZVH);
	size = xv_get_object_size(zv) - sizeof(*zv);
	BUG_ON(size == 0);
	to_va = kmap_atomic(page, KM_USER0);
	ret = lzo1x_decompress_safe((char *)zv + sizeof(*zv),
					size, to_va, &clen);
	kunmap_atomic(to_va, KM_USER0);
	BUG_ON(ret != LZO_E_OK);
	BUG_ON(clen != PAGE_SIZE);
}

#ifdef CONFIG_SYSFS
/*
 * show a distribution of compression stats for zv pages.
 */

static int zv_curr_dist_counts_show(char *buf)
{
	unsigned long i, n, chunks = 0, sum_total_chunks = 0;
	char *p = buf;

	for (i = 0; i < NCHUNKS; i++) {
		n = zv_curr_dist_counts[i];
		p += sprintf(p, "%lu ", n);
		chunks += n;
		sum_total_chunks += i * n;
	}
	p += sprintf(p, "mean:%lu\n",
		chunks == 0 ? 0 : sum_total_chunks / chunks);
	return p - buf;
}

static int zv_cumul_dist_counts_show(char *buf)
{
	unsigned long i, n, chunks = 0, sum_total_chunks = 0;
	char *p = buf;

	for (i = 0; i < NCHUNKS; i++) {
		n = zv_cumul_dist_counts[i];
		p += sprintf(p, "%lu ", n);
		chunks += n;
		sum_total_chunks += i * n;
	}
	p += sprintf(p, "mean:%lu\n",
		chunks == 0 ? 0 : sum_total_chunks / chunks);
	return p - buf;
}

/*
 * setting zv_max_zsize via sysfs causes all persistent (e.g. swap)
 * pages that don't compress to less than this value (including metadata
 * overhead) to be rejected.  We don't allow the value to get too close
 * to PAGE_SIZE.
 */
static ssize_t zv_max_zsize_show(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    char *buf)
{
	return sprintf(buf, "%u\n", zv_max_zsize);
}

static ssize_t zv_max_zsize_store(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    const char *buf, size_t count)
{
	unsigned long val;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	err = strict_strtoul(buf, 10, &val);
	if (err || (val == 0) || (val > (PAGE_SIZE / 8) * 7))
		return -EINVAL;
	zv_max_zsize = val;
	return count;
}

/*
 * setting zv_max_mean_zsize via sysfs causes all persistent (e.g. swap)
 * pages that don't compress to less than this value (including metadata
 * overhead) to be rejected UNLESS the mean compression is also smaller
 * than this value.  In other words, we are load-balancing-by-zsize the
 * accepted pages.  Again, we don't allow the value to get too close
 * to PAGE_SIZE.
 */
static ssize_t zv_max_mean_zsize_show(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    char *buf)
{
	return sprintf(buf, "%u\n", zv_max_mean_zsize);
}

static ssize_t zv_max_mean_zsize_store(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    const char *buf, size_t count)
{
	unsigned long val;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	err = strict_strtoul(buf, 10, &val);
	if (err || (val == 0) || (val > (PAGE_SIZE / 8) * 7))
		return -EINVAL;
	zv_max_mean_zsize = val;
	return count;
}

/*
 * setting zv_page_count_policy_percent via sysfs sets an upper bound of
 * persistent (e.g. swap) pages that will be retained according to:
 *     (zv_page_count_policy_percent * totalram_pages) / 100)
 * when that limit is reached, further puts will be rejected (until
 * some pages have been flushed).  Note that, due to compression,
 * this number may exceed 100; it defaults to 75 and we set an
 * arbitary limit of 150.  A poor choice will almost certainly result
 * in OOM's, so this value should only be changed prudently.
 */
static ssize_t zv_page_count_policy_percent_show(struct kobject *kobj,
						 struct kobj_attribute *attr,
						 char *buf)
{
	return sprintf(buf, "%u\n", zv_page_count_policy_percent);
}

static ssize_t zv_page_count_policy_percent_store(struct kobject *kobj,
						  struct kobj_attribute *attr,
						  const char *buf, size_t count)
{
	unsigned long val;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	err = strict_strtoul(buf, 10, &val);
	if (err || (val == 0) || (val > 150))
		return -EINVAL;
	zv_page_count_policy_percent = val;
	return count;
}

static struct kobj_attribute zcache_zv_max_zsize_attr = {
		.attr = { .name = "zv_max_zsize", .mode = 0644 },
		.show = zv_max_zsize_show,
		.store = zv_max_zsize_store,
};

static struct kobj_attribute zcache_zv_max_mean_zsize_attr = {
		.attr = { .name = "zv_max_mean_zsize", .mode = 0644 },
		.show = zv_max_mean_zsize_show,
		.store = zv_max_mean_zsize_store,
};

static struct kobj_attribute zcache_zv_page_count_policy_percent_attr = {
		.attr = { .name = "zv_page_count_policy_percent",
			  .mode = 0644 },
		.show = zv_page_count_policy_percent_show,
		.store = zv_page_count_policy_percent_store,
};
#endif

/*
 * zcache core code starts here
 */

/* useful stats not collected by cleancache or frontswap */
static unsigned long zcache_flush_total;
static unsigned long zcache_flush_found;
static unsigned long zcache_flobj_total;
static unsigned long zcache_flobj_found;
static unsigned long zcache_failed_eph_puts;
static unsigned long zcache_failed_pers_puts;

/*
 * Tmem operations assume the poolid implies the invoking client.
 * Zcache only has one client (the kernel itself): LOCAL_CLIENT.
 * RAMster has each client numbered by cluster node, and a KVM version
 * of zcache would have one client per guest and each client might
 * have a poolid==N.
 */
static struct tmem_pool *zcache_get_pool_by_id(uint16_t cli_id, uint16_t poolid)
{
	struct tmem_pool *pool = NULL;
	struct zcache_client *cli = NULL;

	if (cli_id == LOCAL_CLIENT)
		cli = &zcache_host;
	else {
		if (cli_id >= MAX_CLIENTS)
			goto out;
		cli = &zcache_clients[cli_id];
		if (cli == NULL)
			goto out;
		atomic_inc(&cli->refcount);
	}
	if (poolid < MAX_POOLS_PER_CLIENT) {
		pool = cli->tmem_pools[poolid];
		if (pool != NULL)
			atomic_inc(&pool->refcount);
	}
out:
	return pool;
}

static void zcache_put_pool(struct tmem_pool *pool)
{
	struct zcache_client *cli = NULL;

	if (pool == NULL)
		BUG();
	cli = pool->client;
	atomic_dec(&pool->refcount);
	atomic_dec(&cli->refcount);
}

int zcache_new_client(uint16_t cli_id)
{
	struct zcache_client *cli = NULL;
	int ret = -1;

	if (cli_id == LOCAL_CLIENT)
		cli = &zcache_host;
	else if ((unsigned int)cli_id < MAX_CLIENTS)
		cli = &zcache_clients[cli_id];
	if (cli == NULL)
		goto out;
	if (cli->allocated)
		goto out;
	cli->allocated = 1;
#ifdef CONFIG_FRONTSWAP
	cli->xvpool = xv_create_pool();
	if (cli->xvpool == NULL)
		goto out;
#endif
	ret = 0;
out:
	return ret;
}

/* counters for debugging */
static unsigned long zcache_failed_get_free_pages;
static unsigned long zcache_failed_alloc;
static unsigned long zcache_put_to_flush;
static unsigned long zcache_aborted_preload;
static unsigned long zcache_aborted_shrink;

/*
 * Ensure that memory allocation requests in zcache don't result
 * in direct reclaim requests via the shrinker, which would cause
 * an infinite loop.  Maybe a GFP flag would be better?
 */
static DEFINE_SPINLOCK(zcache_direct_reclaim_lock);

/*
 * for now, used named slabs so can easily track usage; later can
 * either just use kmalloc, or perhaps add a slab-like allocator
 * to more carefully manage total memory utilization
 */
static struct kmem_cache *zcache_objnode_cache;
static struct kmem_cache *zcache_obj_cache;
static atomic_t zcache_curr_obj_count = ATOMIC_INIT(0);
static unsigned long zcache_curr_obj_count_max;
static atomic_t zcache_curr_objnode_count = ATOMIC_INIT(0);
static unsigned long zcache_curr_objnode_count_max;

/*
 * to avoid memory allocation recursion (e.g. due to direct reclaim), we
 * preload all necessary data structures so the hostops callbacks never
 * actually do a malloc
 */
struct zcache_preload {
	void *page;
	struct tmem_obj *obj;
	int nr;
	struct tmem_objnode *objnodes[OBJNODE_TREE_MAX_PATH];
};
static DEFINE_PER_CPU(struct zcache_preload, zcache_preloads) = { 0, };

static int zcache_do_preload(struct tmem_pool *pool)
{
	struct zcache_preload *kp;
	struct tmem_objnode *objnode;
	struct tmem_obj *obj;
	void *page;
	int ret = -ENOMEM;

	if (unlikely(zcache_objnode_cache == NULL))
		goto out;
	if (unlikely(zcache_obj_cache == NULL))
		goto out;
	if (!spin_trylock(&zcache_direct_reclaim_lock)) {
		zcache_aborted_preload++;
		goto out;
	}
	preempt_disable();
	kp = &__get_cpu_var(zcache_preloads);
	while (kp->nr < ARRAY_SIZE(kp->objnodes)) {
		preempt_enable_no_resched();
		objnode = kmem_cache_alloc(zcache_objnode_cache,
				ZCACHE_GFP_MASK);
		if (unlikely(objnode == NULL)) {
			zcache_failed_alloc++;
			goto unlock_out;
		}
		preempt_disable();
		kp = &__get_cpu_var(zcache_preloads);
		if (kp->nr < ARRAY_SIZE(kp->objnodes))
			kp->objnodes[kp->nr++] = objnode;
		else
			kmem_cache_free(zcache_objnode_cache, objnode);
	}
	preempt_enable_no_resched();
	obj = kmem_cache_alloc(zcache_obj_cache, ZCACHE_GFP_MASK);
	if (unlikely(obj == NULL)) {
		zcache_failed_alloc++;
		goto unlock_out;
	}
	page = (void *)__get_free_page(ZCACHE_GFP_MASK);
	if (unlikely(page == NULL)) {
		zcache_failed_get_free_pages++;
		kmem_cache_free(zcache_obj_cache, obj);
		goto unlock_out;
	}
	preempt_disable();
	kp = &__get_cpu_var(zcache_preloads);
	if (kp->obj == NULL)
		kp->obj = obj;
	else
		kmem_cache_free(zcache_obj_cache, obj);
	if (kp->page == NULL)
		kp->page = page;
	else
		free_page((unsigned long)page);
	ret = 0;
unlock_out:
	spin_unlock(&zcache_direct_reclaim_lock);
out:
	return ret;
}

static void *zcache_get_free_page(void)
{
	struct zcache_preload *kp;
	void *page;

	kp = &__get_cpu_var(zcache_preloads);
	page = kp->page;
	BUG_ON(page == NULL);
	kp->page = NULL;
	return page;
}

static void zcache_free_page(void *p)
{
	free_page((unsigned long)p);
}

/*
 * zcache implementation for tmem host ops
 */

static struct tmem_objnode *zcache_objnode_alloc(struct tmem_pool *pool)
{
	struct tmem_objnode *objnode = NULL;
	unsigned long count;
	struct zcache_preload *kp;

	kp = &__get_cpu_var(zcache_preloads);
	if (kp->nr <= 0)
		goto out;
	objnode = kp->objnodes[kp->nr - 1];
	BUG_ON(objnode == NULL);
	kp->objnodes[kp->nr - 1] = NULL;
	kp->nr--;
	count = atomic_inc_return(&zcache_curr_objnode_count);
	if (count > zcache_curr_objnode_count_max)
		zcache_curr_objnode_count_max = count;
out:
	return objnode;
}

static void zcache_objnode_free(struct tmem_objnode *objnode,
					struct tmem_pool *pool)
{
	atomic_dec(&zcache_curr_objnode_count);
	BUG_ON(atomic_read(&zcache_curr_objnode_count) < 0);
	kmem_cache_free(zcache_objnode_cache, objnode);
}

static struct tmem_obj *zcache_obj_alloc(struct tmem_pool *pool)
{
	struct tmem_obj *obj = NULL;
	unsigned long count;
	struct zcache_preload *kp;

	kp = &__get_cpu_var(zcache_preloads);
	obj = kp->obj;
	BUG_ON(obj == NULL);
	kp->obj = NULL;
	count = atomic_inc_return(&zcache_curr_obj_count);
	if (count > zcache_curr_obj_count_max)
		zcache_curr_obj_count_max = count;
	return obj;
}

static void zcache_obj_free(struct tmem_obj *obj, struct tmem_pool *pool)
{
	atomic_dec(&zcache_curr_obj_count);
	BUG_ON(atomic_read(&zcache_curr_obj_count) < 0);
	kmem_cache_free(zcache_obj_cache, obj);
}

static struct tmem_hostops zcache_hostops = {
	.obj_alloc = zcache_obj_alloc,
	.obj_free = zcache_obj_free,
	.objnode_alloc = zcache_objnode_alloc,
	.objnode_free = zcache_objnode_free,
};

/*
 * zcache implementations for PAM page descriptor ops
 */

static atomic_t zcache_curr_eph_pampd_count = ATOMIC_INIT(0);
static unsigned long zcache_curr_eph_pampd_count_max;
static atomic_t zcache_curr_pers_pampd_count = ATOMIC_INIT(0);
static unsigned long zcache_curr_pers_pampd_count_max;

/* forward reference */
static int zcache_compress(struct page *from, void **out_va, size_t *out_len);

static void *zcache_pampd_create(char *data, size_t size, bool raw, int eph,
				struct tmem_pool *pool, struct tmem_oid *oid,
				 uint32_t index)
{
	void *pampd = NULL, *cdata;
	size_t clen;
	int ret;
	unsigned long count;
	struct page *page = virt_to_page(data);
	struct zcache_client *cli = pool->client;
	uint16_t client_id = get_client_id_from_client(cli);
	unsigned long zv_mean_zsize;
	unsigned long curr_pers_pampd_count;

	if (eph) {
		ret = zcache_compress(page, &cdata, &clen);
		if (ret == 0)
			goto out;
		if (clen == 0 || clen > zbud_max_buddy_size()) {
			zcache_compress_poor++;
			goto out;
		}
		pampd = (void *)zbud_create(client_id, pool->pool_id, oid,
						index, page, cdata, clen);
		if (pampd != NULL) {
			count = atomic_inc_return(&zcache_curr_eph_pampd_count);
			if (count > zcache_curr_eph_pampd_count_max)
				zcache_curr_eph_pampd_count_max = count;
		}
	} else {
		curr_pers_pampd_count =
			atomic_read(&zcache_curr_pers_pampd_count);
		if (curr_pers_pampd_count >
		    (zv_page_count_policy_percent * totalram_pages) / 100)
			goto out;
		ret = zcache_compress(page, &cdata, &clen);
		if (ret == 0)
			goto out;
		/* reject if compression is too poor */
		if (clen > zv_max_zsize) {
			zcache_compress_poor++;
			goto out;
		}
		/* reject if mean compression is too poor */
		if ((clen > zv_max_mean_zsize) && (curr_pers_pampd_count > 0)) {
			zv_mean_zsize = xv_get_total_size_bytes(cli->xvpool) /
						curr_pers_pampd_count;
			if (zv_mean_zsize > zv_max_mean_zsize) {
				zcache_mean_compress_poor++;
				goto out;
			}
		}
		pampd = (void *)zv_create(cli->xvpool, pool->pool_id,
						oid, index, cdata, clen);
		if (pampd == NULL)
			goto out;
		count = atomic_inc_return(&zcache_curr_pers_pampd_count);
		if (count > zcache_curr_pers_pampd_count_max)
			zcache_curr_pers_pampd_count_max = count;
	}
out:
	return pampd;
}

/*
 * fill the pageframe corresponding to the struct page with the data
 * from the passed pampd
 */
static int zcache_pampd_get_data(char *data, size_t *bufsize, bool raw,
					void *pampd, struct tmem_pool *pool,
					struct tmem_oid *oid, uint32_t index)
{
	int ret = 0;

	BUG_ON(is_ephemeral(pool));
	zv_decompress(virt_to_page(data), pampd);
	return ret;
}

/*
 * fill the pageframe corresponding to the struct page with the data
 * from the passed pampd
 */
static int zcache_pampd_get_data_and_free(char *data, size_t *bufsize, bool raw,
					void *pampd, struct tmem_pool *pool,
					struct tmem_oid *oid, uint32_t index)
{
	int ret = 0;

	BUG_ON(!is_ephemeral(pool));
	zbud_decompress(virt_to_page(data), pampd);
	zbud_free_and_delist((struct zbud_hdr *)pampd);
	atomic_dec(&zcache_curr_eph_pampd_count);
	return ret;
}

/*
 * free the pampd and remove it from any zcache lists
 * pampd must no longer be pointed to from any tmem data structures!
 */
static void zcache_pampd_free(void *pampd, struct tmem_pool *pool,
				struct tmem_oid *oid, uint32_t index)
{
	struct zcache_client *cli = pool->client;

	if (is_ephemeral(pool)) {
		zbud_free_and_delist((struct zbud_hdr *)pampd);
		atomic_dec(&zcache_curr_eph_pampd_count);
		BUG_ON(atomic_read(&zcache_curr_eph_pampd_count) < 0);
	} else {
		zv_free(cli->xvpool, (struct zv_hdr *)pampd);
		atomic_dec(&zcache_curr_pers_pampd_count);
		BUG_ON(atomic_read(&zcache_curr_pers_pampd_count) < 0);
	}
}

static void zcache_pampd_free_obj(struct tmem_pool *pool, struct tmem_obj *obj)
{
}

static void zcache_pampd_new_obj(struct tmem_obj *obj)
{
}

static int zcache_pampd_replace_in_obj(void *pampd, struct tmem_obj *obj)
{
	return -1;
}

static bool zcache_pampd_is_remote(void *pampd)
{
	return 0;
}

static struct tmem_pamops zcache_pamops = {
	.create = zcache_pampd_create,
	.get_data = zcache_pampd_get_data,
	.get_data_and_free = zcache_pampd_get_data_and_free,
	.free = zcache_pampd_free,
	.free_obj = zcache_pampd_free_obj,
	.new_obj = zcache_pampd_new_obj,
	.replace_in_obj = zcache_pampd_replace_in_obj,
	.is_remote = zcache_pampd_is_remote,
};

/*
 * zcache compression/decompression and related per-cpu stuff
 */

#define LZO_WORKMEM_BYTES LZO1X_1_MEM_COMPRESS
#define LZO_DSTMEM_PAGE_ORDER 1
static DEFINE_PER_CPU(unsigned char *, zcache_workmem);
static DEFINE_PER_CPU(unsigned char *, zcache_dstmem);

static int zcache_compress(struct page *from, void **out_va, size_t *out_len)
{
	int ret = 0;
	unsigned char *dmem = __get_cpu_var(zcache_dstmem);
	unsigned char *wmem = __get_cpu_var(zcache_workmem);
	char *from_va;

	BUG_ON(!irqs_disabled());
	if (unlikely(dmem == NULL || wmem == NULL))
		goto out;  /* no buffer, so can't compress */
	from_va = kmap_atomic(from, KM_USER0);
	mb();
	ret = lzo1x_1_compress(from_va, PAGE_SIZE, dmem, out_len, wmem);
	BUG_ON(ret != LZO_E_OK);
	*out_va = dmem;
	kunmap_atomic(from_va, KM_USER0);
	ret = 1;
out:
	return ret;
}


static int zcache_cpu_notifier(struct notifier_block *nb,
				unsigned long action, void *pcpu)
{
	int cpu = (long)pcpu;
	struct zcache_preload *kp;

	switch (action) {
	case CPU_UP_PREPARE:
		per_cpu(zcache_dstmem, cpu) = (void *)__get_free_pages(
			GFP_KERNEL | __GFP_REPEAT,
			LZO_DSTMEM_PAGE_ORDER),
		per_cpu(zcache_workmem, cpu) =
			kzalloc(LZO1X_MEM_COMPRESS,
				GFP_KERNEL | __GFP_REPEAT);
		break;
	case CPU_DEAD:
	case CPU_UP_CANCELED:
		free_pages((unsigned long)per_cpu(zcache_dstmem, cpu),
				LZO_DSTMEM_PAGE_ORDER);
		per_cpu(zcache_dstmem, cpu) = NULL;
		kfree(per_cpu(zcache_workmem, cpu));
		per_cpu(zcache_workmem, cpu) = NULL;
		kp = &per_cpu(zcache_preloads, cpu);
		while (kp->nr) {
			kmem_cache_free(zcache_objnode_cache,
					kp->objnodes[kp->nr - 1]);
			kp->objnodes[kp->nr - 1] = NULL;
			kp->nr--;
		}
		kmem_cache_free(zcache_obj_cache, kp->obj);
		free_page((unsigned long)kp->page);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block zcache_cpu_notifier_block = {
	.notifier_call = zcache_cpu_notifier
};

#ifdef CONFIG_SYSFS
#define ZCACHE_SYSFS_RO(_name) \
	static ssize_t zcache_##_name##_show(struct kobject *kobj, \
				struct kobj_attribute *attr, char *buf) \
	{ \
		return sprintf(buf, "%lu\n", zcache_##_name); \
	} \
	static struct kobj_attribute zcache_##_name##_attr = { \
		.attr = { .name = __stringify(_name), .mode = 0444 }, \
		.show = zcache_##_name##_show, \
	}

#define ZCACHE_SYSFS_RO_ATOMIC(_name) \
	static ssize_t zcache_##_name##_show(struct kobject *kobj, \
				struct kobj_attribute *attr, char *buf) \
	{ \
	    return sprintf(buf, "%d\n", atomic_read(&zcache_##_name)); \
	} \
	static struct kobj_attribute zcache_##_name##_attr = { \
		.attr = { .name = __stringify(_name), .mode = 0444 }, \
		.show = zcache_##_name##_show, \
	}

#define ZCACHE_SYSFS_RO_CUSTOM(_name, _func) \
	static ssize_t zcache_##_name##_show(struct kobject *kobj, \
				struct kobj_attribute *attr, char *buf) \
	{ \
	    return _func(buf); \
	} \
	static struct kobj_attribute zcache_##_name##_attr = { \
		.attr = { .name = __stringify(_name), .mode = 0444 }, \
		.show = zcache_##_name##_show, \
	}

ZCACHE_SYSFS_RO(curr_obj_count_max);
ZCACHE_SYSFS_RO(curr_objnode_count_max);
ZCACHE_SYSFS_RO(flush_total);
ZCACHE_SYSFS_RO(flush_found);
ZCACHE_SYSFS_RO(flobj_total);
ZCACHE_SYSFS_RO(flobj_found);
ZCACHE_SYSFS_RO(failed_eph_puts);
ZCACHE_SYSFS_RO(failed_pers_puts);
ZCACHE_SYSFS_RO(zbud_curr_zbytes);
ZCACHE_SYSFS_RO(zbud_cumul_zpages);
ZCACHE_SYSFS_RO(zbud_cumul_zbytes);
ZCACHE_SYSFS_RO(zbud_buddied_count);
ZCACHE_SYSFS_RO(zbpg_unused_list_count);
ZCACHE_SYSFS_RO(evicted_raw_pages);
ZCACHE_SYSFS_RO(evicted_unbuddied_pages);
ZCACHE_SYSFS_RO(evicted_buddied_pages);
ZCACHE_SYSFS_RO(failed_get_free_pages);
ZCACHE_SYSFS_RO(failed_alloc);
ZCACHE_SYSFS_RO(put_to_flush);
ZCACHE_SYSFS_RO(aborted_preload);
ZCACHE_SYSFS_RO(aborted_shrink);
ZCACHE_SYSFS_RO(compress_poor);
ZCACHE_SYSFS_RO(mean_compress_poor);
ZCACHE_SYSFS_RO_ATOMIC(zbud_curr_raw_pages);
ZCACHE_SYSFS_RO_ATOMIC(zbud_curr_zpages);
ZCACHE_SYSFS_RO_ATOMIC(curr_obj_count);
ZCACHE_SYSFS_RO_ATOMIC(curr_objnode_count);
ZCACHE_SYSFS_RO_CUSTOM(zbud_unbuddied_list_counts,
			zbud_show_unbuddied_list_counts);
ZCACHE_SYSFS_RO_CUSTOM(zbud_cumul_chunk_counts,
			zbud_show_cumul_chunk_counts);
ZCACHE_SYSFS_RO_CUSTOM(zv_curr_dist_counts,
			zv_curr_dist_counts_show);
ZCACHE_SYSFS_RO_CUSTOM(zv_cumul_dist_counts,
			zv_cumul_dist_counts_show);

static struct attribute *zcache_attrs[] = {
	&zcache_curr_obj_count_attr.attr,
	&zcache_curr_obj_count_max_attr.attr,
	&zcache_curr_objnode_count_attr.attr,
	&zcache_curr_objnode_count_max_attr.attr,
	&zcache_flush_total_attr.attr,
	&zcache_flobj_total_attr.attr,
	&zcache_flush_found_attr.attr,
	&zcache_flobj_found_attr.attr,
	&zcache_failed_eph_puts_attr.attr,
	&zcache_failed_pers_puts_attr.attr,
	&zcache_compress_poor_attr.attr,
	&zcache_mean_compress_poor_attr.attr,
	&zcache_zbud_curr_raw_pages_attr.attr,
	&zcache_zbud_curr_zpages_attr.attr,
	&zcache_zbud_curr_zbytes_attr.attr,
	&zcache_zbud_cumul_zpages_attr.attr,
	&zcache_zbud_cumul_zbytes_attr.attr,
	&zcache_zbud_buddied_count_attr.attr,
	&zcache_zbpg_unused_list_count_attr.attr,
	&zcache_evicted_raw_pages_attr.attr,
	&zcache_evicted_unbuddied_pages_attr.attr,
	&zcache_evicted_buddied_pages_attr.attr,
	&zcache_failed_get_free_pages_attr.attr,
	&zcache_failed_alloc_attr.attr,
	&zcache_put_to_flush_attr.attr,
	&zcache_aborted_preload_attr.attr,
	&zcache_aborted_shrink_attr.attr,
	&zcache_zbud_unbuddied_list_counts_attr.attr,
	&zcache_zbud_cumul_chunk_counts_attr.attr,
	&zcache_zv_curr_dist_counts_attr.attr,
	&zcache_zv_cumul_dist_counts_attr.attr,
	&zcache_zv_max_zsize_attr.attr,
	&zcache_zv_max_mean_zsize_attr.attr,
	&zcache_zv_page_count_policy_percent_attr.attr,
	NULL,
};

static struct attribute_group zcache_attr_group = {
	.attrs = zcache_attrs,
	.name = "zcache",
};

#endif /* CONFIG_SYSFS */
/*
 * When zcache is disabled ("frozen"), pools can be created and destroyed,
 * but all puts (and thus all other operations that require memory allocation)
 * must fail.  If zcache is unfrozen, accepts puts, then frozen again,
 * data consistency requires all puts while frozen to be converted into
 * flushes.
 */
static bool zcache_freeze;

/*
 * zcache shrinker interface (only useful for ephemeral pages, so zbud only)
 */
static int shrink_zcache_memory(struct shrinker *shrink,
				struct shrink_control *sc)
{
	int ret = -1;
	int nr = sc->nr_to_scan;
	gfp_t gfp_mask = sc->gfp_mask;

	if (nr >= 0) {
		if (!(gfp_mask & __GFP_FS))
			/* does this case really need to be skipped? */
			goto out;
		if (spin_trylock(&zcache_direct_reclaim_lock)) {
			zbud_evict_pages(nr);
			spin_unlock(&zcache_direct_reclaim_lock);
		} else
			zcache_aborted_shrink++;
	}
	ret = (int)atomic_read(&zcache_zbud_curr_raw_pages);
out:
	return ret;
}

static struct shrinker zcache_shrinker = {
	.shrink = shrink_zcache_memory,
	.seeks = DEFAULT_SEEKS,
};

/*
 * zcache shims between cleancache/frontswap ops and tmem
 */

static int zcache_put_page(int cli_id, int pool_id, struct tmem_oid *oidp,
				uint32_t index, struct page *page)
{
	struct tmem_pool *pool;
	int ret = -1;

	BUG_ON(!irqs_disabled());
	pool = zcache_get_pool_by_id(cli_id, pool_id);
	if (unlikely(pool == NULL))
		goto out;
	if (!zcache_freeze && zcache_do_preload(pool) == 0) {
		/* preload does preempt_disable on success */
		ret = tmem_put(pool, oidp, index, page_address(page),
				PAGE_SIZE, 0, is_ephemeral(pool));
		if (ret < 0) {
			if (is_ephemeral(pool))
				zcache_failed_eph_puts++;
			else
				zcache_failed_pers_puts++;
		}
		zcache_put_pool(pool);
		preempt_enable_no_resched();
	} else {
		zcache_put_to_flush++;
		if (atomic_read(&pool->obj_count) > 0)
			/* the put fails whether the flush succeeds or not */
			(void)tmem_flush_page(pool, oidp, index);
		zcache_put_pool(pool);
	}
out:
	return ret;
}

static int zcache_get_page(int cli_id, int pool_id, struct tmem_oid *oidp,
				uint32_t index, struct page *page)
{
	struct tmem_pool *pool;
	int ret = -1;
	unsigned long flags;
	size_t size = PAGE_SIZE;

	local_irq_save(flags);
	pool = zcache_get_pool_by_id(cli_id, pool_id);
	if (likely(pool != NULL)) {
		if (atomic_read(&pool->obj_count) > 0)
			ret = tmem_get(pool, oidp, index, page_address(page),
					&size, 0, is_ephemeral(pool));
		zcache_put_pool(pool);
	}
	local_irq_restore(flags);
	return ret;
}

static int zcache_flush_page(int cli_id, int pool_id,
				struct tmem_oid *oidp, uint32_t index)
{
	struct tmem_pool *pool;
	int ret = -1;
	unsigned long flags;

	local_irq_save(flags);
	zcache_flush_total++;
	pool = zcache_get_pool_by_id(cli_id, pool_id);
	if (likely(pool != NULL)) {
		if (atomic_read(&pool->obj_count) > 0)
			ret = tmem_flush_page(pool, oidp, index);
		zcache_put_pool(pool);
	}
	if (ret >= 0)
		zcache_flush_found++;
	local_irq_restore(flags);
	return ret;
}

static int zcache_flush_object(int cli_id, int pool_id,
				struct tmem_oid *oidp)
{
	struct tmem_pool *pool;
	int ret = -1;
	unsigned long flags;

	local_irq_save(flags);
	zcache_flobj_total++;
	pool = zcache_get_pool_by_id(cli_id, pool_id);
	if (likely(pool != NULL)) {
		if (atomic_read(&pool->obj_count) > 0)
			ret = tmem_flush_object(pool, oidp);
		zcache_put_pool(pool);
	}
	if (ret >= 0)
		zcache_flobj_found++;
	local_irq_restore(flags);
	return ret;
}

static int zcache_destroy_pool(int cli_id, int pool_id)
{
	struct tmem_pool *pool = NULL;
	struct zcache_client *cli = NULL;
	int ret = -1;

	if (pool_id < 0)
		goto out;
	if (cli_id == LOCAL_CLIENT)
		cli = &zcache_host;
	else if ((unsigned int)cli_id < MAX_CLIENTS)
		cli = &zcache_clients[cli_id];
	if (cli == NULL)
		goto out;
	atomic_inc(&cli->refcount);
	pool = cli->tmem_pools[pool_id];
	if (pool == NULL)
		goto out;
	cli->tmem_pools[pool_id] = NULL;
	/* wait for pool activity on other cpus to quiesce */
	while (atomic_read(&pool->refcount) != 0)
		;
	atomic_dec(&cli->refcount);
	local_bh_disable();
	ret = tmem_destroy_pool(pool);
	local_bh_enable();
	kfree(pool);
	pr_info("zcache: destroyed pool id=%d, cli_id=%d\n",
			pool_id, cli_id);
out:
	return ret;
}

static int zcache_new_pool(uint16_t cli_id, uint32_t flags)
{
	int poolid = -1;
	struct tmem_pool *pool;
	struct zcache_client *cli = NULL;

	if (cli_id == LOCAL_CLIENT)
		cli = &zcache_host;
	else if ((unsigned int)cli_id < MAX_CLIENTS)
		cli = &zcache_clients[cli_id];
	if (cli == NULL)
		goto out;
	atomic_inc(&cli->refcount);
	pool = kmalloc(sizeof(struct tmem_pool), GFP_KERNEL);
	if (pool == NULL) {
		pr_info("zcache: pool creation failed: out of memory\n");
		goto out;
	}

	for (poolid = 0; poolid < MAX_POOLS_PER_CLIENT; poolid++)
		if (cli->tmem_pools[poolid] == NULL)
			break;
	if (poolid >= MAX_POOLS_PER_CLIENT) {
		pr_info("zcache: pool creation failed: max exceeded\n");
		kfree(pool);
		poolid = -1;
		goto out;
	}
	atomic_set(&pool->refcount, 0);
	pool->client = cli;
	pool->pool_id = poolid;
	tmem_new_pool(pool, flags);
	cli->tmem_pools[poolid] = pool;
	pr_info("zcache: created %s tmem pool, id=%d, client=%d\n",
		flags & TMEM_POOL_PERSIST ? "persistent" : "ephemeral",
		poolid, cli_id);
out:
	if (cli != NULL)
		atomic_dec(&cli->refcount);
	return poolid;
}

/**********
 * Two kernel functionalities currently can be layered on top of tmem.
 * These are "cleancache" which is used as a second-chance cache for clean
 * page cache pages; and "frontswap" which is used for swap pages
 * to avoid writes to disk.  A generic "shim" is provided here for each
 * to translate in-kernel semantics to zcache semantics.
 */

#ifdef CONFIG_CLEANCACHE
static void zcache_cleancache_put_page(int pool_id,
					struct cleancache_filekey key,
					pgoff_t index, struct page *page)
{
	u32 ind = (u32) index;
	struct tmem_oid oid = *(struct tmem_oid *)&key;

	if (likely(ind == index))
		(void)zcache_put_page(LOCAL_CLIENT, pool_id, &oid, index, page);
}

static int zcache_cleancache_get_page(int pool_id,
					struct cleancache_filekey key,
					pgoff_t index, struct page *page)
{
	u32 ind = (u32) index;
	struct tmem_oid oid = *(struct tmem_oid *)&key;
	int ret = -1;

	if (likely(ind == index))
		ret = zcache_get_page(LOCAL_CLIENT, pool_id, &oid, index, page);
	return ret;
}

static void zcache_cleancache_flush_page(int pool_id,
					struct cleancache_filekey key,
					pgoff_t index)
{
	u32 ind = (u32) index;
	struct tmem_oid oid = *(struct tmem_oid *)&key;

	if (likely(ind == index))
		(void)zcache_flush_page(LOCAL_CLIENT, pool_id, &oid, ind);
}

static void zcache_cleancache_flush_inode(int pool_id,
					struct cleancache_filekey key)
{
	struct tmem_oid oid = *(struct tmem_oid *)&key;

	(void)zcache_flush_object(LOCAL_CLIENT, pool_id, &oid);
}

static void zcache_cleancache_flush_fs(int pool_id)
{
	if (pool_id >= 0)
		(void)zcache_destroy_pool(LOCAL_CLIENT, pool_id);
}

static int zcache_cleancache_init_fs(size_t pagesize)
{
	BUG_ON(sizeof(struct cleancache_filekey) !=
				sizeof(struct tmem_oid));
	BUG_ON(pagesize != PAGE_SIZE);
	return zcache_new_pool(LOCAL_CLIENT, 0);
}

static int zcache_cleancache_init_shared_fs(char *uuid, size_t pagesize)
{
	/* shared pools are unsupported and map to private */
	BUG_ON(sizeof(struct cleancache_filekey) !=
				sizeof(struct tmem_oid));
	BUG_ON(pagesize != PAGE_SIZE);
	return zcache_new_pool(LOCAL_CLIENT, 0);
}

static struct cleancache_ops zcache_cleancache_ops = {
	.put_page = zcache_cleancache_put_page,
	.get_page = zcache_cleancache_get_page,
	.flush_page = zcache_cleancache_flush_page,
	.flush_inode = zcache_cleancache_flush_inode,
	.flush_fs = zcache_cleancache_flush_fs,
	.init_shared_fs = zcache_cleancache_init_shared_fs,
	.init_fs = zcache_cleancache_init_fs
};

struct cleancache_ops zcache_cleancache_register_ops(void)
{
	struct cleancache_ops old_ops =
		cleancache_register_ops(&zcache_cleancache_ops);

	return old_ops;
}
#endif

#ifdef CONFIG_FRONTSWAP
/* a single tmem poolid is used for all frontswap "types" (swapfiles) */
static int zcache_frontswap_poolid = -1;

/*
 * Swizzling increases objects per swaptype, increasing tmem concurrency
 * for heavy swaploads.  Later, larger nr_cpus -> larger SWIZ_BITS
 */
#define SWIZ_BITS		4
#define SWIZ_MASK		((1 << SWIZ_BITS) - 1)
#define _oswiz(_type, _ind)	((_type << SWIZ_BITS) | (_ind & SWIZ_MASK))
#define iswiz(_ind)		(_ind >> SWIZ_BITS)

static inline struct tmem_oid oswiz(unsigned type, u32 ind)
{
	struct tmem_oid oid = { .oid = { 0 } };
	oid.oid[0] = _oswiz(type, ind);
	return oid;
}

static int zcache_frontswap_put_page(unsigned type, pgoff_t offset,
				   struct page *page)
{
	u64 ind64 = (u64)offset;
	u32 ind = (u32)offset;
	struct tmem_oid oid = oswiz(type, ind);
	int ret = -1;
	unsigned long flags;

	BUG_ON(!PageLocked(page));
	if (likely(ind64 == ind)) {
		local_irq_save(flags);
		ret = zcache_put_page(LOCAL_CLIENT, zcache_frontswap_poolid,
					&oid, iswiz(ind), page);
		local_irq_restore(flags);
	}
	return ret;
}

/* returns 0 if the page was successfully gotten from frontswap, -1 if
 * was not present (should never happen!) */
static int zcache_frontswap_get_page(unsigned type, pgoff_t offset,
				   struct page *page)
{
	u64 ind64 = (u64)offset;
	u32 ind = (u32)offset;
	struct tmem_oid oid = oswiz(type, ind);
	int ret = -1;

	BUG_ON(!PageLocked(page));
	if (likely(ind64 == ind))
		ret = zcache_get_page(LOCAL_CLIENT, zcache_frontswap_poolid,
					&oid, iswiz(ind), page);
	return ret;
}

/* flush a single page from frontswap */
static void zcache_frontswap_flush_page(unsigned type, pgoff_t offset)
{
	u64 ind64 = (u64)offset;
	u32 ind = (u32)offset;
	struct tmem_oid oid = oswiz(type, ind);

	if (likely(ind64 == ind))
		(void)zcache_flush_page(LOCAL_CLIENT, zcache_frontswap_poolid,
					&oid, iswiz(ind));
}

/* flush all pages from the passed swaptype */
static void zcache_frontswap_flush_area(unsigned type)
{
	struct tmem_oid oid;
	int ind;

	for (ind = SWIZ_MASK; ind >= 0; ind--) {
		oid = oswiz(type, ind);
		(void)zcache_flush_object(LOCAL_CLIENT,
						zcache_frontswap_poolid, &oid);
	}
}

static void zcache_frontswap_init(unsigned ignored)
{
	/* a single tmem poolid is used for all frontswap "types" (swapfiles) */
	if (zcache_frontswap_poolid < 0)
		zcache_frontswap_poolid =
			zcache_new_pool(LOCAL_CLIENT, TMEM_POOL_PERSIST);
}

static struct frontswap_ops zcache_frontswap_ops = {
	.put_page = zcache_frontswap_put_page,
	.get_page = zcache_frontswap_get_page,
	.flush_page = zcache_frontswap_flush_page,
	.flush_area = zcache_frontswap_flush_area,
	.init = zcache_frontswap_init
};

struct frontswap_ops zcache_frontswap_register_ops(void)
{
	struct frontswap_ops old_ops =
		frontswap_register_ops(&zcache_frontswap_ops);

	return old_ops;
}
#endif

/*
 * zcache initialization
 * NOTE FOR NOW zcache MUST BE PROVIDED AS A KERNEL BOOT PARAMETER OR
 * NOTHING HAPPENS!
 */

static int zcache_enabled;

static int __init enable_zcache(char *s)
{
	zcache_enabled = 1;
	return 1;
}
__setup("zcache", enable_zcache);

/* allow independent dynamic disabling of cleancache and frontswap */

static int use_cleancache = 1;

static int __init no_cleancache(char *s)
{
	use_cleancache = 0;
	return 1;
}

__setup("nocleancache", no_cleancache);

static int use_frontswap = 1;

static int __init no_frontswap(char *s)
{
	use_frontswap = 0;
	return 1;
}

__setup("nofrontswap", no_frontswap);

static int __init zcache_init(void)
{
#ifdef CONFIG_SYSFS
	int ret = 0;

	ret = sysfs_create_group(mm_kobj, &zcache_attr_group);
	if (ret) {
		pr_err("zcache: can't create sysfs\n");
		goto out;
	}
#endif /* CONFIG_SYSFS */
#if defined(CONFIG_CLEANCACHE) || defined(CONFIG_FRONTSWAP)
	if (zcache_enabled) {
		unsigned int cpu;

		tmem_register_hostops(&zcache_hostops);
		tmem_register_pamops(&zcache_pamops);
		ret = register_cpu_notifier(&zcache_cpu_notifier_block);
		if (ret) {
			pr_err("zcache: can't register cpu notifier\n");
			goto out;
		}
		for_each_online_cpu(cpu) {
			void *pcpu = (void *)(long)cpu;
			zcache_cpu_notifier(&zcache_cpu_notifier_block,
				CPU_UP_PREPARE, pcpu);
		}
	}
	zcache_objnode_cache = kmem_cache_create("zcache_objnode",
				sizeof(struct tmem_objnode), 0, 0, NULL);
	zcache_obj_cache = kmem_cache_create("zcache_obj",
				sizeof(struct tmem_obj), 0, 0, NULL);
	ret = zcache_new_client(LOCAL_CLIENT);
	if (ret) {
		pr_err("zcache: can't create client\n");
		goto out;
	}
#endif
#ifdef CONFIG_CLEANCACHE
	if (zcache_enabled && use_cleancache) {
		struct cleancache_ops old_ops;

		zbud_init();
		register_shrinker(&zcache_shrinker);
		old_ops = zcache_cleancache_register_ops();
		pr_info("zcache: cleancache enabled using kernel "
			"transcendent memory and compression buddies\n");
		if (old_ops.init_fs != NULL)
			pr_warning("zcache: cleancache_ops overridden");
	}
#endif
#ifdef CONFIG_FRONTSWAP
	if (zcache_enabled && use_frontswap) {
		struct frontswap_ops old_ops;

		old_ops = zcache_frontswap_register_ops();
		pr_info("zcache: frontswap enabled using kernel "
			"transcendent memory and xvmalloc\n");
		if (old_ops.init != NULL)
			pr_warning("ktmem: frontswap_ops overridden");
	}
#endif
out:
	return ret;
}

module_init(zcache_init)
