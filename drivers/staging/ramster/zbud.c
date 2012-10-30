/*
 * zbud.c - Compression buddies allocator
 *
 * Copyright (c) 2010-2012, Dan Magenheimer, Oracle Corp.
 *
 * Compression buddies ("zbud") provides for efficiently packing two
 * (or, possibly in the future, more) compressed pages ("zpages") into
 * a single "raw" pageframe and for tracking both zpages and pageframes
 * so that whole pageframes can be easily reclaimed in LRU-like order.
 * It is designed to be used in conjunction with transcendent memory
 * ("tmem"); for example separate LRU lists are maintained for persistent
 * vs. ephemeral pages.
 *
 * A zbudpage is an overlay for a struct page and thus each zbudpage
 * refers to a physical pageframe of RAM.  When the caller passes a
 * struct page from the kernel's page allocator, zbud "transforms" it
 * to a zbudpage which sets/uses a different set of fields than the
 * struct-page and thus must "untransform" it back by reinitializing
 * certain fields before the struct-page can be freed.  The fields
 * of a zbudpage include a page lock for controlling access to the
 * corresponding pageframe, and there is a size field for each zpage.
 * Each zbudpage also lives on two linked lists: a "budlist" which is
 * used to support efficient buddying of zpages; and an "lru" which
 * is used for reclaiming pageframes in approximately least-recently-used
 * order.
 *
 * A zbudpageframe is a pageframe divided up into aligned 64-byte "chunks"
 * which contain the compressed data for zero, one, or two zbuds.  Contained
 * with the compressed data is a tmem_handle which is a key to allow
 * the same data to be found via the tmem interface so the zpage can
 * be invalidated (for ephemeral pages) or repatriated to the swap cache
 * (for persistent pages).  The contents of a zbudpageframe must never
 * be accessed without holding the page lock for the corresponding
 * zbudpage and, to accomodate highmem machines, the contents may
 * only be examined or changes when kmapped.  Thus, when in use, a
 * kmapped zbudpageframe is referred to in the zbud code as "void *zbpg".
 *
 * Note that the term "zbud" refers to the combination of a zpage and
 * a tmem_handle that is stored as one of possibly two "buddied" zpages;
 * it also generically refers to this allocator... sorry for any confusion.
 *
 * A zbudref is a pointer to a struct zbudpage (which can be cast to a
 * struct page), with the LSB either cleared or set to indicate, respectively,
 * the first or second zpage in the zbudpageframe. Since a zbudref can be
 * cast to a pointer, it is used as the tmem "pampd" pointer and uniquely
 * references a stored tmem page and so is the only zbud data structure
 * externally visible to zbud.c/zbud.h.
 *
 * Since we wish to reclaim entire pageframes but zpages may be randomly
 * added and deleted to any given pageframe, we approximate LRU by
 * promoting a pageframe to MRU when a zpage is added to it, but
 * leaving it at the current place in the list when a zpage is deleted
 * from it.  As a side effect, zpages that are difficult to buddy (e.g.
 * very large paages) will be reclaimed faster than average, which seems
 * reasonable.
 *
 * In the current implementation, no more than two zpages may be stored in
 * any pageframe and no zpage ever crosses a pageframe boundary.  While
 * other zpage allocation mechanisms may allow greater density, this two
 * zpage-per-pageframe limit both ensures simple reclaim of pageframes
 * (including garbage collection of references to the contents of those
 * pageframes from tmem data structures) AND avoids the need for compaction.
 * With additional complexity, zbud could be modified to support storing
 * up to three zpages per pageframe or, to handle larger average zpages,
 * up to three zpages per pair of pageframes, but it is not clear if the
 * additional complexity would be worth it.  So consider it an exercise
 * for future developers.
 *
 * Note also that zbud does no page allocation or freeing.  This is so
 * that the caller has complete control over and, for accounting, visibility
 * into if/when pages are allocated and freed.
 *
 * Finally, note that zbud limits the size of zpages it can store; the
 * caller must check the zpage size with zbud_max_buddy_size before
 * storing it, else BUGs will result.  User beware.
 */

#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/pagemap.h>
#include <linux/atomic.h>
#include <linux/bug.h>
#include "tmem.h"
#include "zcache.h"
#include "zbud.h"

/*
 * We need to ensure that a struct zbudpage is never larger than a
 * struct page.  This is checked with a BUG_ON in zbud_init.
 *
 * The unevictable field indicates that a zbud is being added to the
 * zbudpage.  Since this is a two-phase process (due to tmem locking),
 * this field locks the zbudpage against eviction when a zbud match
 * or creation is in process.  Since this addition process may occur
 * in parallel for two zbuds in one zbudpage, the field is a counter
 * that must not exceed two.
 */
struct zbudpage {
	union {
		struct page page;
		struct {
			unsigned long space_for_flags;
			struct {
				unsigned zbud0_size:12;
				unsigned zbud1_size:12;
				unsigned unevictable:2;
			};
			struct list_head budlist;
			struct list_head lru;
		};
	};
};

struct zbudref {
	union {
		struct zbudpage *zbudpage;
		unsigned long zbudref;
	};
};

#define CHUNK_SHIFT	6
#define CHUNK_SIZE	(1 << CHUNK_SHIFT)
#define CHUNK_MASK	(~(CHUNK_SIZE-1))
#define NCHUNKS		(PAGE_SIZE >> CHUNK_SHIFT)
#define MAX_CHUNK	(NCHUNKS-1)

/*
 * The following functions deal with the difference between struct
 * page and struct zbudpage.  Note the hack of using the pageflags
 * from struct page; this is to avoid duplicating all the complex
 * pageflag macros.
 */
static inline void zbudpage_spin_lock(struct zbudpage *zbudpage)
{
	struct page *page = (struct page *)zbudpage;

	while (unlikely(test_and_set_bit_lock(PG_locked, &page->flags))) {
		do {
			cpu_relax();
		} while (test_bit(PG_locked, &page->flags));
	}
}

static inline void zbudpage_spin_unlock(struct zbudpage *zbudpage)
{
	struct page *page = (struct page *)zbudpage;

	clear_bit(PG_locked, &page->flags);
}

static inline int zbudpage_spin_trylock(struct zbudpage *zbudpage)
{
	return trylock_page((struct page *)zbudpage);
}

static inline int zbudpage_is_locked(struct zbudpage *zbudpage)
{
	return PageLocked((struct page *)zbudpage);
}

static inline void *kmap_zbudpage_atomic(struct zbudpage *zbudpage)
{
	return kmap_atomic((struct page *)zbudpage);
}

/*
 * A dying zbudpage is an ephemeral page in the process of being evicted.
 * Any data contained in the zbudpage is invalid and we are just waiting for
 * the tmem pampds to be invalidated before freeing the page
 */
static inline int zbudpage_is_dying(struct zbudpage *zbudpage)
{
	struct page *page = (struct page *)zbudpage;

	return test_bit(PG_reclaim, &page->flags);
}

static inline void zbudpage_set_dying(struct zbudpage *zbudpage)
{
	struct page *page = (struct page *)zbudpage;

	set_bit(PG_reclaim, &page->flags);
}

static inline void zbudpage_clear_dying(struct zbudpage *zbudpage)
{
	struct page *page = (struct page *)zbudpage;

	clear_bit(PG_reclaim, &page->flags);
}

/*
 * A zombie zbudpage is a persistent page in the process of being evicted.
 * The data contained in the zbudpage is valid and we are just waiting for
 * the tmem pampds to be invalidated before freeing the page
 */
static inline int zbudpage_is_zombie(struct zbudpage *zbudpage)
{
	struct page *page = (struct page *)zbudpage;

	return test_bit(PG_dirty, &page->flags);
}

static inline void zbudpage_set_zombie(struct zbudpage *zbudpage)
{
	struct page *page = (struct page *)zbudpage;

	set_bit(PG_dirty, &page->flags);
}

static inline void zbudpage_clear_zombie(struct zbudpage *zbudpage)
{
	struct page *page = (struct page *)zbudpage;

	clear_bit(PG_dirty, &page->flags);
}

static inline void kunmap_zbudpage_atomic(void *zbpg)
{
	kunmap_atomic(zbpg);
}

/*
 * zbud "translation" and helper functions
 */

static inline struct zbudpage *zbudref_to_zbudpage(struct zbudref *zref)
{
	unsigned long zbud = (unsigned long)zref;
	zbud &= ~1UL;
	return (struct zbudpage *)zbud;
}

static inline struct zbudref *zbudpage_to_zbudref(struct zbudpage *zbudpage,
							unsigned budnum)
{
	unsigned long zbud = (unsigned long)zbudpage;
	BUG_ON(budnum > 1);
	zbud |= budnum;
	return (struct zbudref *)zbud;
}

static inline int zbudref_budnum(struct zbudref *zbudref)
{
	unsigned long zbud = (unsigned long)zbudref;
	return zbud & 1UL;
}

static inline unsigned zbud_max_size(void)
{
	return MAX_CHUNK << CHUNK_SHIFT;
}

static inline unsigned zbud_size_to_chunks(unsigned size)
{
	BUG_ON(size == 0 || size > zbud_max_size());
	return (size + CHUNK_SIZE - 1) >> CHUNK_SHIFT;
}

/* can only be used between kmap_zbudpage_atomic/kunmap_zbudpage_atomic! */
static inline char *zbud_data(void *zbpg,
			unsigned budnum, unsigned size)
{
	char *p;

	BUG_ON(size == 0 || size > zbud_max_size());
	p = (char *)zbpg;
	if (budnum == 1)
		p += PAGE_SIZE - ((size + CHUNK_SIZE - 1) & CHUNK_MASK);
	return p;
}

/*
 * These are all informative and exposed through debugfs... except for
 * the arrays... anyone know how to do that?  To avoid confusion for
 * debugfs viewers, some of these should also be atomic_long_t, but
 * I don't know how to expose atomics via debugfs either...
 */
static unsigned long zbud_eph_pageframes;
static unsigned long zbud_pers_pageframes;
static unsigned long zbud_eph_zpages;
static unsigned long zbud_pers_zpages;
static u64 zbud_eph_zbytes;
static u64 zbud_pers_zbytes;
static unsigned long zbud_eph_evicted_pageframes;
static unsigned long zbud_pers_evicted_pageframes;
static unsigned long zbud_eph_cumul_zpages;
static unsigned long zbud_pers_cumul_zpages;
static u64 zbud_eph_cumul_zbytes;
static u64 zbud_pers_cumul_zbytes;
static unsigned long zbud_eph_cumul_chunk_counts[NCHUNKS];
static unsigned long zbud_pers_cumul_chunk_counts[NCHUNKS];
static unsigned long zbud_eph_buddied_count;
static unsigned long zbud_pers_buddied_count;
static unsigned long zbud_eph_unbuddied_count;
static unsigned long zbud_pers_unbuddied_count;
static unsigned long zbud_eph_zombie_count;
static unsigned long zbud_pers_zombie_count;
static atomic_t zbud_eph_zombie_atomic;
static atomic_t zbud_pers_zombie_atomic;

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#define	zdfs	debugfs_create_size_t
#define	zdfs64	debugfs_create_u64
static int zbud_debugfs_init(void)
{
	struct dentry *root = debugfs_create_dir("zbud", NULL);
	if (root == NULL)
		return -ENXIO;

	/*
	 * would be nice to dump the sizes of the unbuddied
	 * arrays, like was done with sysfs, but it doesn't
	 * look like debugfs is flexible enough to do that
	 */
	zdfs64("eph_zbytes", S_IRUGO, root, &zbud_eph_zbytes);
	zdfs64("eph_cumul_zbytes", S_IRUGO, root, &zbud_eph_cumul_zbytes);
	zdfs64("pers_zbytes", S_IRUGO, root, &zbud_pers_zbytes);
	zdfs64("pers_cumul_zbytes", S_IRUGO, root, &zbud_pers_cumul_zbytes);
	zdfs("eph_cumul_zpages", S_IRUGO, root, &zbud_eph_cumul_zpages);
	zdfs("eph_evicted_pageframes", S_IRUGO, root,
				&zbud_eph_evicted_pageframes);
	zdfs("eph_zpages", S_IRUGO, root, &zbud_eph_zpages);
	zdfs("eph_pageframes", S_IRUGO, root, &zbud_eph_pageframes);
	zdfs("eph_buddied_count", S_IRUGO, root, &zbud_eph_buddied_count);
	zdfs("eph_unbuddied_count", S_IRUGO, root, &zbud_eph_unbuddied_count);
	zdfs("pers_cumul_zpages", S_IRUGO, root, &zbud_pers_cumul_zpages);
	zdfs("pers_evicted_pageframes", S_IRUGO, root,
				&zbud_pers_evicted_pageframes);
	zdfs("pers_zpages", S_IRUGO, root, &zbud_pers_zpages);
	zdfs("pers_pageframes", S_IRUGO, root, &zbud_pers_pageframes);
	zdfs("pers_buddied_count", S_IRUGO, root, &zbud_pers_buddied_count);
	zdfs("pers_unbuddied_count", S_IRUGO, root, &zbud_pers_unbuddied_count);
	zdfs("pers_zombie_count", S_IRUGO, root, &zbud_pers_zombie_count);
	return 0;
}
#undef	zdfs
#undef	zdfs64
#endif

/* protects the buddied list and all unbuddied lists */
static DEFINE_SPINLOCK(zbud_eph_lists_lock);
static DEFINE_SPINLOCK(zbud_pers_lists_lock);

struct zbud_unbuddied {
	struct list_head list;
	unsigned count;
};

/* list N contains pages with N chunks USED and NCHUNKS-N unused */
/* element 0 is never used but optimizing that isn't worth it */
static struct zbud_unbuddied zbud_eph_unbuddied[NCHUNKS];
static struct zbud_unbuddied zbud_pers_unbuddied[NCHUNKS];
static LIST_HEAD(zbud_eph_lru_list);
static LIST_HEAD(zbud_pers_lru_list);
static LIST_HEAD(zbud_eph_buddied_list);
static LIST_HEAD(zbud_pers_buddied_list);
static LIST_HEAD(zbud_eph_zombie_list);
static LIST_HEAD(zbud_pers_zombie_list);

/*
 * Given a struct page, transform it to a zbudpage so that it can be
 * used by zbud and initialize fields as necessary.
 */
static inline struct zbudpage *zbud_init_zbudpage(struct page *page, bool eph)
{
	struct zbudpage *zbudpage = (struct zbudpage *)page;

	BUG_ON(page == NULL);
	INIT_LIST_HEAD(&zbudpage->budlist);
	INIT_LIST_HEAD(&zbudpage->lru);
	zbudpage->zbud0_size = 0;
	zbudpage->zbud1_size = 0;
	zbudpage->unevictable = 0;
	if (eph)
		zbud_eph_pageframes++;
	else
		zbud_pers_pageframes++;
	return zbudpage;
}

/* "Transform" a zbudpage back to a struct page suitable to free. */
static inline struct page *zbud_unuse_zbudpage(struct zbudpage *zbudpage,
								bool eph)
{
	struct page *page = (struct page *)zbudpage;

	BUG_ON(!list_empty(&zbudpage->budlist));
	BUG_ON(!list_empty(&zbudpage->lru));
	BUG_ON(zbudpage->zbud0_size != 0);
	BUG_ON(zbudpage->zbud1_size != 0);
	BUG_ON(!PageLocked(page));
	BUG_ON(zbudpage->unevictable != 0);
	BUG_ON(zbudpage_is_dying(zbudpage));
	BUG_ON(zbudpage_is_zombie(zbudpage));
	if (eph)
		zbud_eph_pageframes--;
	else
		zbud_pers_pageframes--;
	zbudpage_spin_unlock(zbudpage);
	reset_page_mapcount(page);
	init_page_count(page);
	page->index = 0;
	return page;
}

/* Mark a zbud as unused and do accounting */
static inline void zbud_unuse_zbud(struct zbudpage *zbudpage,
					int budnum, bool eph)
{
	unsigned size;

	BUG_ON(!zbudpage_is_locked(zbudpage));
	if (budnum == 0) {
		size = zbudpage->zbud0_size;
		zbudpage->zbud0_size = 0;
	} else {
		size = zbudpage->zbud1_size;
		zbudpage->zbud1_size = 0;
	}
	if (eph) {
		zbud_eph_zbytes -= size;
		zbud_eph_zpages--;
	} else {
		zbud_pers_zbytes -= size;
		zbud_pers_zpages--;
	}
}

/*
 * Given a zbudpage/budnum/size, a tmem handle, and a kmapped pointer
 * to some data, set up the zbud appropriately including data copying
 * and accounting.  Note that if cdata is NULL, the data copying is
 * skipped.  (This is useful for lazy writes such as for RAMster.)
 */
static void zbud_init_zbud(struct zbudpage *zbudpage, struct tmem_handle *th,
				bool eph, void *cdata,
				unsigned budnum, unsigned size)
{
	char *to;
	void *zbpg;
	struct tmem_handle *to_th;
	unsigned nchunks = zbud_size_to_chunks(size);

	BUG_ON(!zbudpage_is_locked(zbudpage));
	zbpg = kmap_zbudpage_atomic(zbudpage);
	to = zbud_data(zbpg, budnum, size);
	to_th = (struct tmem_handle *)to;
	to_th->index = th->index;
	to_th->oid = th->oid;
	to_th->pool_id = th->pool_id;
	to_th->client_id = th->client_id;
	to += sizeof(struct tmem_handle);
	if (cdata != NULL)
		memcpy(to, cdata, size - sizeof(struct tmem_handle));
	kunmap_zbudpage_atomic(zbpg);
	if (budnum == 0)
		zbudpage->zbud0_size = size;
	else
		zbudpage->zbud1_size = size;
	if (eph) {
		zbud_eph_cumul_chunk_counts[nchunks]++;
		zbud_eph_zpages++;
		zbud_eph_cumul_zpages++;
		zbud_eph_zbytes += size;
		zbud_eph_cumul_zbytes += size;
	} else {
		zbud_pers_cumul_chunk_counts[nchunks]++;
		zbud_pers_zpages++;
		zbud_pers_cumul_zpages++;
		zbud_pers_zbytes += size;
		zbud_pers_cumul_zbytes += size;
	}
}

/*
 * Given a locked dying zbudpage, read out the tmem handles from the data,
 * unlock the page, then use the handles to tell tmem to flush out its
 * references
 */
static void zbud_evict_tmem(struct zbudpage *zbudpage)
{
	int i, j;
	uint32_t pool_id[2], client_id[2];
	uint32_t index[2];
	struct tmem_oid oid[2];
	struct tmem_pool *pool;
	void *zbpg;
	struct tmem_handle *th;
	unsigned size;

	/* read out the tmem handles from the data and set aside */
	zbpg = kmap_zbudpage_atomic(zbudpage);
	for (i = 0, j = 0; i < 2; i++) {
		size = (i == 0) ? zbudpage->zbud0_size : zbudpage->zbud1_size;
		if (size) {
			th = (struct tmem_handle *)zbud_data(zbpg, i, size);
			client_id[j] = th->client_id;
			pool_id[j] = th->pool_id;
			oid[j] = th->oid;
			index[j] = th->index;
			j++;
			zbud_unuse_zbud(zbudpage, i, true);
		}
	}
	kunmap_zbudpage_atomic(zbpg);
	zbudpage_spin_unlock(zbudpage);
	/* zbudpage is now an unlocked dying... tell tmem to flush pointers */
	for (i = 0; i < j; i++) {
		pool = zcache_get_pool_by_id(client_id[i], pool_id[i]);
		if (pool != NULL) {
			tmem_flush_page(pool, &oid[i], index[i]);
			zcache_put_pool(pool);
		}
	}
}

/*
 * Externally callable zbud handling routines.
 */

/*
 * Return the maximum size compressed page that can be stored (secretly
 * setting aside space for the tmem handle.
 */
unsigned int zbud_max_buddy_size(void)
{
	return zbud_max_size() - sizeof(struct tmem_handle);
}

/*
 * Given a zbud reference, free the corresponding zbud from all lists,
 * mark it as unused, do accounting, and if the freeing of the zbud
 * frees up an entire pageframe, return it to the caller (else NULL).
 */
struct page *zbud_free_and_delist(struct zbudref *zref, bool eph,
				  unsigned int *zsize, unsigned int *zpages)
{
	unsigned long budnum = zbudref_budnum(zref);
	struct zbudpage *zbudpage = zbudref_to_zbudpage(zref);
	struct page *page = NULL;
	unsigned chunks, bud_size, other_bud_size;
	spinlock_t *lists_lock =
		eph ? &zbud_eph_lists_lock : &zbud_pers_lists_lock;
	struct zbud_unbuddied *unbud =
		eph ? zbud_eph_unbuddied : zbud_pers_unbuddied;


	spin_lock(lists_lock);
	zbudpage_spin_lock(zbudpage);
	if (zbudpage_is_dying(zbudpage)) {
		/* ignore dying zbudpage... see zbud_evict_pageframe_lru() */
		zbudpage_spin_unlock(zbudpage);
		spin_unlock(lists_lock);
		*zpages = 0;
		*zsize = 0;
		goto out;
	}
	if (budnum == 0) {
		bud_size = zbudpage->zbud0_size;
		other_bud_size = zbudpage->zbud1_size;
	} else {
		bud_size = zbudpage->zbud1_size;
		other_bud_size = zbudpage->zbud0_size;
	}
	*zsize = bud_size - sizeof(struct tmem_handle);
	*zpages = 1;
	zbud_unuse_zbud(zbudpage, budnum, eph);
	if (other_bud_size == 0) { /* was unbuddied: unlist and free */
		chunks = zbud_size_to_chunks(bud_size) ;
		if (zbudpage_is_zombie(zbudpage)) {
			if (eph)
				zbud_pers_zombie_count =
				  atomic_dec_return(&zbud_eph_zombie_atomic);
			else
				zbud_pers_zombie_count =
				  atomic_dec_return(&zbud_pers_zombie_atomic);
			zbudpage_clear_zombie(zbudpage);
		} else {
			BUG_ON(list_empty(&unbud[chunks].list));
			list_del_init(&zbudpage->budlist);
			unbud[chunks].count--;
		}
		list_del_init(&zbudpage->lru);
		spin_unlock(lists_lock);
		if (eph)
			zbud_eph_unbuddied_count--;
		else
			zbud_pers_unbuddied_count--;
		page = zbud_unuse_zbudpage(zbudpage, eph);
	} else { /* was buddied: move remaining buddy to unbuddied list */
		chunks = zbud_size_to_chunks(other_bud_size) ;
		if (!zbudpage_is_zombie(zbudpage)) {
			list_del_init(&zbudpage->budlist);
			list_add_tail(&zbudpage->budlist, &unbud[chunks].list);
			unbud[chunks].count++;
		}
		if (eph) {
			zbud_eph_buddied_count--;
			zbud_eph_unbuddied_count++;
		} else {
			zbud_pers_unbuddied_count++;
			zbud_pers_buddied_count--;
		}
		/* don't mess with lru, no need to move it */
		zbudpage_spin_unlock(zbudpage);
		spin_unlock(lists_lock);
	}
out:
	return page;
}

/*
 * Given a tmem handle, and a kmapped pointer to compressed data of
 * the given size, try to find an unbuddied zbudpage in which to
 * create a zbud. If found, put it there, mark the zbudpage unevictable,
 * and return a zbudref to it.  Else return NULL.
 */
struct zbudref *zbud_match_prep(struct tmem_handle *th, bool eph,
				void *cdata, unsigned size)
{
	struct zbudpage *zbudpage = NULL, *zbudpage2;
	unsigned long budnum = 0UL;
	unsigned nchunks;
	int i, found_good_buddy = 0;
	spinlock_t *lists_lock =
		eph ? &zbud_eph_lists_lock : &zbud_pers_lists_lock;
	struct zbud_unbuddied *unbud =
		eph ? zbud_eph_unbuddied : zbud_pers_unbuddied;

	size += sizeof(struct tmem_handle);
	nchunks = zbud_size_to_chunks(size);
	for (i = MAX_CHUNK - nchunks + 1; i > 0; i--) {
		spin_lock(lists_lock);
		if (!list_empty(&unbud[i].list)) {
			list_for_each_entry_safe(zbudpage, zbudpage2,
				    &unbud[i].list, budlist) {
				if (zbudpage_spin_trylock(zbudpage)) {
					found_good_buddy = i;
					goto found_unbuddied;
				}
			}
		}
		spin_unlock(lists_lock);
	}
	zbudpage = NULL;
	goto out;

found_unbuddied:
	BUG_ON(!zbudpage_is_locked(zbudpage));
	BUG_ON(!((zbudpage->zbud0_size == 0) ^ (zbudpage->zbud1_size == 0)));
	if (zbudpage->zbud0_size == 0)
		budnum = 0UL;
	else if (zbudpage->zbud1_size == 0)
		budnum = 1UL;
	list_del_init(&zbudpage->budlist);
	if (eph) {
		list_add_tail(&zbudpage->budlist, &zbud_eph_buddied_list);
		unbud[found_good_buddy].count--;
		zbud_eph_unbuddied_count--;
		zbud_eph_buddied_count++;
		/* "promote" raw zbudpage to most-recently-used */
		list_del_init(&zbudpage->lru);
		list_add_tail(&zbudpage->lru, &zbud_eph_lru_list);
	} else {
		list_add_tail(&zbudpage->budlist, &zbud_pers_buddied_list);
		unbud[found_good_buddy].count--;
		zbud_pers_unbuddied_count--;
		zbud_pers_buddied_count++;
		/* "promote" raw zbudpage to most-recently-used */
		list_del_init(&zbudpage->lru);
		list_add_tail(&zbudpage->lru, &zbud_pers_lru_list);
	}
	zbud_init_zbud(zbudpage, th, eph, cdata, budnum, size);
	zbudpage->unevictable++;
	BUG_ON(zbudpage->unevictable == 3);
	zbudpage_spin_unlock(zbudpage);
	spin_unlock(lists_lock);
out:
	return zbudpage_to_zbudref(zbudpage, budnum);

}

/*
 * Given a tmem handle, and a kmapped pointer to compressed data of
 * the given size, and a newly allocated struct page, create an unevictable
 * zbud in that new page and return a zbudref to it.
 */
struct zbudref *zbud_create_prep(struct tmem_handle *th, bool eph,
					void *cdata, unsigned size,
					struct page *newpage)
{
	struct zbudpage *zbudpage;
	unsigned long budnum = 0;
	unsigned nchunks;
	spinlock_t *lists_lock =
		eph ? &zbud_eph_lists_lock : &zbud_pers_lists_lock;
	struct zbud_unbuddied *unbud =
		eph ? zbud_eph_unbuddied : zbud_pers_unbuddied;

#if 0
	/* this may be worth it later to support decompress-in-place? */
	static unsigned long counter;
	budnum = counter++ & 1;	/* alternate using zbud0 and zbud1 */
#endif

	if (size  > zbud_max_buddy_size())
		return NULL;
	if (newpage == NULL)
		return NULL;

	size += sizeof(struct tmem_handle);
	nchunks = zbud_size_to_chunks(size) ;
	spin_lock(lists_lock);
	zbudpage = zbud_init_zbudpage(newpage, eph);
	zbudpage_spin_lock(zbudpage);
	list_add_tail(&zbudpage->budlist, &unbud[nchunks].list);
	if (eph) {
		list_add_tail(&zbudpage->lru, &zbud_eph_lru_list);
		zbud_eph_unbuddied_count++;
	} else {
		list_add_tail(&zbudpage->lru, &zbud_pers_lru_list);
		zbud_pers_unbuddied_count++;
	}
	unbud[nchunks].count++;
	zbud_init_zbud(zbudpage, th, eph, cdata, budnum, size);
	zbudpage->unevictable++;
	BUG_ON(zbudpage->unevictable == 3);
	zbudpage_spin_unlock(zbudpage);
	spin_unlock(lists_lock);
	return zbudpage_to_zbudref(zbudpage, budnum);
}

/*
 * Finish creation of a zbud by, assuming another zbud isn't being created
 * in parallel, marking it evictable.
 */
void zbud_create_finish(struct zbudref *zref, bool eph)
{
	struct zbudpage *zbudpage = zbudref_to_zbudpage(zref);
	spinlock_t *lists_lock =
		eph ? &zbud_eph_lists_lock : &zbud_pers_lists_lock;

	spin_lock(lists_lock);
	zbudpage_spin_lock(zbudpage);
	BUG_ON(zbudpage_is_dying(zbudpage));
	zbudpage->unevictable--;
	BUG_ON((int)zbudpage->unevictable < 0);
	zbudpage_spin_unlock(zbudpage);
	spin_unlock(lists_lock);
}

/*
 * Given a zbudref and a struct page, decompress the data from
 * the zbud into the physical page represented by the struct page
 * by upcalling to zcache_decompress
 */
int zbud_decompress(struct page *data_page, struct zbudref *zref, bool eph,
			void (*decompress)(char *, unsigned int, char *))
{
	struct zbudpage *zbudpage = zbudref_to_zbudpage(zref);
	unsigned long budnum = zbudref_budnum(zref);
	void *zbpg;
	char *to_va, *from_va;
	unsigned size;
	int ret = -1;
	spinlock_t *lists_lock =
		eph ? &zbud_eph_lists_lock : &zbud_pers_lists_lock;

	spin_lock(lists_lock);
	zbudpage_spin_lock(zbudpage);
	if (zbudpage_is_dying(zbudpage)) {
		/* ignore dying zbudpage... see zbud_evict_pageframe_lru() */
		goto out;
	}
	zbpg = kmap_zbudpage_atomic(zbudpage);
	to_va = kmap_atomic(data_page);
	if (budnum == 0)
		size = zbudpage->zbud0_size;
	else
		size = zbudpage->zbud1_size;
	BUG_ON(size == 0 || size > zbud_max_size());
	from_va = zbud_data(zbpg, budnum, size);
	from_va += sizeof(struct tmem_handle);
	size -= sizeof(struct tmem_handle);
	decompress(from_va, size, to_va);
	kunmap_atomic(to_va);
	kunmap_zbudpage_atomic(zbpg);
	ret = 0;
out:
	zbudpage_spin_unlock(zbudpage);
	spin_unlock(lists_lock);
	return ret;
}

/*
 * Given a zbudref and a kernel pointer, copy the data from
 * the zbud to the kernel pointer.
 */
int zbud_copy_from_zbud(char *to_va, struct zbudref *zref,
				size_t *sizep, bool eph)
{
	struct zbudpage *zbudpage = zbudref_to_zbudpage(zref);
	unsigned long budnum = zbudref_budnum(zref);
	void *zbpg;
	char *from_va;
	unsigned size;
	int ret = -1;
	spinlock_t *lists_lock =
		eph ? &zbud_eph_lists_lock : &zbud_pers_lists_lock;

	spin_lock(lists_lock);
	zbudpage_spin_lock(zbudpage);
	if (zbudpage_is_dying(zbudpage)) {
		/* ignore dying zbudpage... see zbud_evict_pageframe_lru() */
		goto out;
	}
	zbpg = kmap_zbudpage_atomic(zbudpage);
	if (budnum == 0)
		size = zbudpage->zbud0_size;
	else
		size = zbudpage->zbud1_size;
	BUG_ON(size == 0 || size > zbud_max_size());
	from_va = zbud_data(zbpg, budnum, size);
	from_va += sizeof(struct tmem_handle);
	size -= sizeof(struct tmem_handle);
	*sizep = size;
	memcpy(to_va, from_va, size);

	kunmap_zbudpage_atomic(zbpg);
	ret = 0;
out:
	zbudpage_spin_unlock(zbudpage);
	spin_unlock(lists_lock);
	return ret;
}

/*
 * Given a zbudref and a kernel pointer, copy the data from
 * the kernel pointer to the zbud.
 */
int zbud_copy_to_zbud(struct zbudref *zref, char *from_va, bool eph)
{
	struct zbudpage *zbudpage = zbudref_to_zbudpage(zref);
	unsigned long budnum = zbudref_budnum(zref);
	void *zbpg;
	char *to_va;
	unsigned size;
	int ret = -1;
	spinlock_t *lists_lock =
		eph ? &zbud_eph_lists_lock : &zbud_pers_lists_lock;

	spin_lock(lists_lock);
	zbudpage_spin_lock(zbudpage);
	if (zbudpage_is_dying(zbudpage)) {
		/* ignore dying zbudpage... see zbud_evict_pageframe_lru() */
		goto out;
	}
	zbpg = kmap_zbudpage_atomic(zbudpage);
	if (budnum == 0)
		size = zbudpage->zbud0_size;
	else
		size = zbudpage->zbud1_size;
	BUG_ON(size == 0 || size > zbud_max_size());
	to_va = zbud_data(zbpg, budnum, size);
	to_va += sizeof(struct tmem_handle);
	size -= sizeof(struct tmem_handle);
	memcpy(to_va, from_va, size);

	kunmap_zbudpage_atomic(zbpg);
	ret = 0;
out:
	zbudpage_spin_unlock(zbudpage);
	spin_unlock(lists_lock);
	return ret;
}

/*
 * Choose an ephemeral LRU zbudpage that is evictable (not locked), ensure
 * there are no references to it remaining, and return the now unused
 * (and re-init'ed) struct page and the total amount of compressed
 * data that was evicted.
 */
struct page *zbud_evict_pageframe_lru(unsigned int *zsize, unsigned int *zpages)
{
	struct zbudpage *zbudpage = NULL, *zbudpage2;
	struct zbud_unbuddied *unbud = zbud_eph_unbuddied;
	struct page *page = NULL;
	bool irqs_disabled = irqs_disabled();

	/*
	 * Since this can be called indirectly from cleancache_put, which
	 * has interrupts disabled, as well as frontswap_put, which does not,
	 * we need to be able to handle both cases, even though it is ugly.
	 */
	if (irqs_disabled)
		spin_lock(&zbud_eph_lists_lock);
	else
		spin_lock_bh(&zbud_eph_lists_lock);
	*zsize = 0;
	if (list_empty(&zbud_eph_lru_list))
		goto unlock_out;
	list_for_each_entry_safe(zbudpage, zbudpage2, &zbud_eph_lru_list, lru) {
		/* skip a locked zbudpage */
		if (unlikely(!zbudpage_spin_trylock(zbudpage)))
			continue;
		/* skip an unevictable zbudpage */
		if (unlikely(zbudpage->unevictable != 0)) {
			zbudpage_spin_unlock(zbudpage);
			continue;
		}
		/* got a locked evictable page */
		goto evict_page;

	}
unlock_out:
	/* no unlocked evictable pages, give up */
	if (irqs_disabled)
		spin_unlock(&zbud_eph_lists_lock);
	else
		spin_unlock_bh(&zbud_eph_lists_lock);
	goto out;

evict_page:
	list_del_init(&zbudpage->budlist);
	list_del_init(&zbudpage->lru);
	zbudpage_set_dying(zbudpage);
	/*
	 * the zbudpage is now "dying" and attempts to read, write,
	 * or delete data from it will be ignored
	 */
	if (zbudpage->zbud0_size != 0 && zbudpage->zbud1_size !=  0) {
		*zsize = zbudpage->zbud0_size + zbudpage->zbud1_size -
				(2 * sizeof(struct tmem_handle));
		*zpages = 2;
	} else if (zbudpage->zbud0_size != 0) {
		unbud[zbud_size_to_chunks(zbudpage->zbud0_size)].count--;
		*zsize = zbudpage->zbud0_size - sizeof(struct tmem_handle);
		*zpages = 1;
	} else if (zbudpage->zbud1_size != 0) {
		unbud[zbud_size_to_chunks(zbudpage->zbud1_size)].count--;
		*zsize = zbudpage->zbud1_size - sizeof(struct tmem_handle);
		*zpages = 1;
	} else {
		BUG();
	}
	spin_unlock(&zbud_eph_lists_lock);
	zbud_eph_evicted_pageframes++;
	if (*zpages == 1)
		zbud_eph_unbuddied_count--;
	else
		zbud_eph_buddied_count--;
	zbud_evict_tmem(zbudpage);
	zbudpage_spin_lock(zbudpage);
	zbudpage_clear_dying(zbudpage);
	page = zbud_unuse_zbudpage(zbudpage, true);
	if (!irqs_disabled)
		local_bh_enable();
out:
	return page;
}

/*
 * Choose a persistent LRU zbudpage that is evictable (not locked), zombify it,
 * read the tmem_handle(s) out of it into the passed array, and return the
 * number of zbuds.  Caller must perform necessary tmem functions and,
 * indirectly, zbud functions to fetch any valid data and cause the
 * now-zombified zbudpage to eventually be freed.  We track the zombified
 * zbudpage count so it is possible to observe if there is a leak.
 FIXME: describe (ramster) case where data pointers are passed in for memcpy
 */
unsigned int zbud_make_zombie_lru(struct tmem_handle *th, unsigned char **data,
					unsigned int *zsize, bool eph)
{
	struct zbudpage *zbudpage = NULL, *zbudpag2;
	struct tmem_handle *thfrom;
	char *from_va;
	void *zbpg;
	unsigned size;
	int ret = 0, i;
	spinlock_t *lists_lock =
		eph ? &zbud_eph_lists_lock : &zbud_pers_lists_lock;
	struct list_head *lru_list =
		eph ? &zbud_eph_lru_list : &zbud_pers_lru_list;

	spin_lock_bh(lists_lock);
	if (list_empty(lru_list))
		goto out;
	list_for_each_entry_safe(zbudpage, zbudpag2, lru_list, lru) {
		/* skip a locked zbudpage */
		if (unlikely(!zbudpage_spin_trylock(zbudpage)))
			continue;
		/* skip an unevictable zbudpage */
		if (unlikely(zbudpage->unevictable != 0)) {
			zbudpage_spin_unlock(zbudpage);
			continue;
		}
		/* got a locked evictable page */
		goto zombify_page;
	}
	/* no unlocked evictable pages, give up */
	goto out;

zombify_page:
	/* got an unlocked evictable page, zombify it */
	list_del_init(&zbudpage->budlist);
	zbudpage_set_zombie(zbudpage);
	/* FIXME what accounting do I need to do here? */
	list_del_init(&zbudpage->lru);
	if (eph) {
		list_add_tail(&zbudpage->lru, &zbud_eph_zombie_list);
		zbud_eph_zombie_count =
				atomic_inc_return(&zbud_eph_zombie_atomic);
	} else {
		list_add_tail(&zbudpage->lru, &zbud_pers_zombie_list);
		zbud_pers_zombie_count =
				atomic_inc_return(&zbud_pers_zombie_atomic);
	}
	/* FIXME what accounting do I need to do here? */
	zbpg = kmap_zbudpage_atomic(zbudpage);
	for (i = 0; i < 2; i++) {
		size = (i == 0) ? zbudpage->zbud0_size : zbudpage->zbud1_size;
		if (size) {
			from_va = zbud_data(zbpg, i, size);
			thfrom = (struct tmem_handle *)from_va;
			from_va += sizeof(struct tmem_handle);
			size -= sizeof(struct tmem_handle);
			if (th != NULL)
				th[ret] = *thfrom;
			if (data != NULL)
				memcpy(data[ret], from_va, size);
			if (zsize != NULL)
				*zsize++ = size;
			ret++;
		}
	}
	kunmap_zbudpage_atomic(zbpg);
	zbudpage_spin_unlock(zbudpage);
out:
	spin_unlock_bh(lists_lock);
	return ret;
}

void __init zbud_init(void)
{
	int i;

#ifdef CONFIG_DEBUG_FS
	zbud_debugfs_init();
#endif
	BUG_ON((sizeof(struct tmem_handle) * 2 > CHUNK_SIZE));
	BUG_ON(sizeof(struct zbudpage) > sizeof(struct page));
	for (i = 0; i < NCHUNKS; i++) {
		INIT_LIST_HEAD(&zbud_eph_unbuddied[i].list);
		INIT_LIST_HEAD(&zbud_pers_unbuddied[i].list);
	}
}
