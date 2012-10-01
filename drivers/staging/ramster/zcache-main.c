/*
 * zcache.c
 *
 * Copyright (c) 2010-2012, Dan Magenheimer, Oracle Corp.
 * Copyright (c) 2010,2011, Nitin Gupta
 *
 * Zcache provides an in-kernel "host implementation" for transcendent memory
 * ("tmem") and, thus indirectly, for cleancache and frontswap.  Zcache uses
 * lzo1x compression to improve density and an embedded allocator called
 * "zbud" which "buddies" two compressed pages semi-optimally in each physical
 * pageframe.  Zbud is integrally tied into tmem to allow pageframes to
 * be "reclaimed" efficiently.
 */

#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/highmem.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/math64.h>
#include <linux/crypto.h>

#include <linux/cleancache.h>
#include <linux/frontswap.h>
#include "tmem.h"
#include "zcache.h"
#include "zbud.h"
#include "ramster.h"
#ifdef CONFIG_RAMSTER
static int ramster_enabled;
#else
#define ramster_enabled 0
#endif

#ifndef __PG_WAS_ACTIVE
static inline bool PageWasActive(struct page *page)
{
	return true;
}

static inline void SetPageWasActive(struct page *page)
{
}
#endif

#ifdef FRONTSWAP_HAS_EXCLUSIVE_GETS
static bool frontswap_has_exclusive_gets __read_mostly = true;
#else
static bool frontswap_has_exclusive_gets __read_mostly;
static inline void frontswap_tmem_exclusive_gets(bool b)
{
}
#endif

static int zcache_enabled __read_mostly;
static int disable_cleancache __read_mostly;
static int disable_frontswap __read_mostly;
static int disable_frontswap_ignore_nonactive __read_mostly;
static int disable_cleancache_ignore_nonactive __read_mostly;
static char *namestr __read_mostly = "zcache";

#define ZCACHE_GFP_MASK \
	(__GFP_FS | __GFP_NORETRY | __GFP_NOWARN | __GFP_NOMEMALLOC)

MODULE_LICENSE("GPL");

/* crypto API for zcache  */
#define ZCACHE_COMP_NAME_SZ CRYPTO_MAX_ALG_NAME
static char zcache_comp_name[ZCACHE_COMP_NAME_SZ] __read_mostly;
static struct crypto_comp * __percpu *zcache_comp_pcpu_tfms __read_mostly;

enum comp_op {
	ZCACHE_COMPOP_COMPRESS,
	ZCACHE_COMPOP_DECOMPRESS
};

static inline int zcache_comp_op(enum comp_op op,
				const u8 *src, unsigned int slen,
				u8 *dst, unsigned int *dlen)
{
	struct crypto_comp *tfm;
	int ret = -1;

	BUG_ON(!zcache_comp_pcpu_tfms);
	tfm = *per_cpu_ptr(zcache_comp_pcpu_tfms, get_cpu());
	BUG_ON(!tfm);
	switch (op) {
	case ZCACHE_COMPOP_COMPRESS:
		ret = crypto_comp_compress(tfm, src, slen, dst, dlen);
		break;
	case ZCACHE_COMPOP_DECOMPRESS:
		ret = crypto_comp_decompress(tfm, src, slen, dst, dlen);
		break;
	default:
		ret = -EINVAL;
	}
	put_cpu();
	return ret;
}

/*
 * policy parameters
 */

/*
 * byte count defining poor compression; pages with greater zsize will be
 * rejected
 */
static unsigned int zbud_max_zsize __read_mostly = (PAGE_SIZE / 8) * 7;
/*
 * byte count defining poor *mean* compression; pages with greater zsize
 * will be rejected until sufficient better-compressed pages are accepted
 * driving the mean below this threshold
 */
static unsigned int zbud_max_mean_zsize __read_mostly = (PAGE_SIZE / 8) * 5;

/*
 * for now, used named slabs so can easily track usage; later can
 * either just use kmalloc, or perhaps add a slab-like allocator
 * to more carefully manage total memory utilization
 */
static struct kmem_cache *zcache_objnode_cache;
static struct kmem_cache *zcache_obj_cache;

static DEFINE_PER_CPU(struct zcache_preload, zcache_preloads) = { 0, };

/* we try to keep these statistics SMP-consistent */
static long zcache_obj_count;
static atomic_t zcache_obj_atomic = ATOMIC_INIT(0);
static long zcache_obj_count_max;
static long zcache_objnode_count;
static atomic_t zcache_objnode_atomic = ATOMIC_INIT(0);
static long zcache_objnode_count_max;
static u64 zcache_eph_zbytes;
static atomic_long_t zcache_eph_zbytes_atomic = ATOMIC_INIT(0);
static u64 zcache_eph_zbytes_max;
static u64 zcache_pers_zbytes;
static atomic_long_t zcache_pers_zbytes_atomic = ATOMIC_INIT(0);
static u64 zcache_pers_zbytes_max;
static long zcache_eph_pageframes;
static atomic_t zcache_eph_pageframes_atomic = ATOMIC_INIT(0);
static long zcache_eph_pageframes_max;
static long zcache_pers_pageframes;
static atomic_t zcache_pers_pageframes_atomic = ATOMIC_INIT(0);
static long zcache_pers_pageframes_max;
static long zcache_pageframes_alloced;
static atomic_t zcache_pageframes_alloced_atomic = ATOMIC_INIT(0);
static long zcache_pageframes_freed;
static atomic_t zcache_pageframes_freed_atomic = ATOMIC_INIT(0);
static long zcache_eph_zpages;
static atomic_t zcache_eph_zpages_atomic = ATOMIC_INIT(0);
static long zcache_eph_zpages_max;
static long zcache_pers_zpages;
static atomic_t zcache_pers_zpages_atomic = ATOMIC_INIT(0);
static long zcache_pers_zpages_max;

/* but for the rest of these, counting races are ok */
static unsigned long zcache_flush_total;
static unsigned long zcache_flush_found;
static unsigned long zcache_flobj_total;
static unsigned long zcache_flobj_found;
static unsigned long zcache_failed_eph_puts;
static unsigned long zcache_failed_pers_puts;
static unsigned long zcache_failed_getfreepages;
static unsigned long zcache_failed_alloc;
static unsigned long zcache_put_to_flush;
static unsigned long zcache_compress_poor;
static unsigned long zcache_mean_compress_poor;
static unsigned long zcache_eph_ate_tail;
static unsigned long zcache_eph_ate_tail_failed;
static unsigned long zcache_pers_ate_eph;
static unsigned long zcache_pers_ate_eph_failed;
static unsigned long zcache_evicted_eph_zpages;
static unsigned long zcache_evicted_eph_pageframes;
static unsigned long zcache_last_active_file_pageframes;
static unsigned long zcache_last_inactive_file_pageframes;
static unsigned long zcache_last_active_anon_pageframes;
static unsigned long zcache_last_inactive_anon_pageframes;
static unsigned long zcache_eph_nonactive_puts_ignored;
static unsigned long zcache_pers_nonactive_puts_ignored;

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#define	zdfs	debugfs_create_size_t
#define	zdfs64	debugfs_create_u64
static int zcache_debugfs_init(void)
{
	struct dentry *root = debugfs_create_dir("zcache", NULL);
	if (root == NULL)
		return -ENXIO;

	zdfs("obj_count", S_IRUGO, root, &zcache_obj_count);
	zdfs("obj_count_max", S_IRUGO, root, &zcache_obj_count_max);
	zdfs("objnode_count", S_IRUGO, root, &zcache_objnode_count);
	zdfs("objnode_count_max", S_IRUGO, root, &zcache_objnode_count_max);
	zdfs("flush_total", S_IRUGO, root, &zcache_flush_total);
	zdfs("flush_found", S_IRUGO, root, &zcache_flush_found);
	zdfs("flobj_total", S_IRUGO, root, &zcache_flobj_total);
	zdfs("flobj_found", S_IRUGO, root, &zcache_flobj_found);
	zdfs("failed_eph_puts", S_IRUGO, root, &zcache_failed_eph_puts);
	zdfs("failed_pers_puts", S_IRUGO, root, &zcache_failed_pers_puts);
	zdfs("failed_get_free_pages", S_IRUGO, root,
				&zcache_failed_getfreepages);
	zdfs("failed_alloc", S_IRUGO, root, &zcache_failed_alloc);
	zdfs("put_to_flush", S_IRUGO, root, &zcache_put_to_flush);
	zdfs("compress_poor", S_IRUGO, root, &zcache_compress_poor);
	zdfs("mean_compress_poor", S_IRUGO, root, &zcache_mean_compress_poor);
	zdfs("eph_ate_tail", S_IRUGO, root, &zcache_eph_ate_tail);
	zdfs("eph_ate_tail_failed", S_IRUGO, root, &zcache_eph_ate_tail_failed);
	zdfs("pers_ate_eph", S_IRUGO, root, &zcache_pers_ate_eph);
	zdfs("pers_ate_eph_failed", S_IRUGO, root, &zcache_pers_ate_eph_failed);
	zdfs("evicted_eph_zpages", S_IRUGO, root, &zcache_evicted_eph_zpages);
	zdfs("evicted_eph_pageframes", S_IRUGO, root,
				&zcache_evicted_eph_pageframes);
	zdfs("eph_pageframes", S_IRUGO, root, &zcache_eph_pageframes);
	zdfs("eph_pageframes_max", S_IRUGO, root, &zcache_eph_pageframes_max);
	zdfs("pers_pageframes", S_IRUGO, root, &zcache_pers_pageframes);
	zdfs("pers_pageframes_max", S_IRUGO, root, &zcache_pers_pageframes_max);
	zdfs("eph_zpages", S_IRUGO, root, &zcache_eph_zpages);
	zdfs("eph_zpages_max", S_IRUGO, root, &zcache_eph_zpages_max);
	zdfs("pers_zpages", S_IRUGO, root, &zcache_pers_zpages);
	zdfs("pers_zpages_max", S_IRUGO, root, &zcache_pers_zpages_max);
	zdfs("last_active_file_pageframes", S_IRUGO, root,
				&zcache_last_active_file_pageframes);
	zdfs("last_inactive_file_pageframes", S_IRUGO, root,
				&zcache_last_inactive_file_pageframes);
	zdfs("last_active_anon_pageframes", S_IRUGO, root,
				&zcache_last_active_anon_pageframes);
	zdfs("last_inactive_anon_pageframes", S_IRUGO, root,
				&zcache_last_inactive_anon_pageframes);
	zdfs("eph_nonactive_puts_ignored", S_IRUGO, root,
				&zcache_eph_nonactive_puts_ignored);
	zdfs("pers_nonactive_puts_ignored", S_IRUGO, root,
				&zcache_pers_nonactive_puts_ignored);
	zdfs64("eph_zbytes", S_IRUGO, root, &zcache_eph_zbytes);
	zdfs64("eph_zbytes_max", S_IRUGO, root, &zcache_eph_zbytes_max);
	zdfs64("pers_zbytes", S_IRUGO, root, &zcache_pers_zbytes);
	zdfs64("pers_zbytes_max", S_IRUGO, root, &zcache_pers_zbytes_max);
	return 0;
}
#undef	zdebugfs
#undef	zdfs64
#endif

#define ZCACHE_DEBUG
#ifdef ZCACHE_DEBUG
/* developers can call this in case of ooms, e.g. to find memory leaks */
void zcache_dump(void)
{
	pr_info("zcache: obj_count=%lu\n", zcache_obj_count);
	pr_info("zcache: obj_count_max=%lu\n", zcache_obj_count_max);
	pr_info("zcache: objnode_count=%lu\n", zcache_objnode_count);
	pr_info("zcache: objnode_count_max=%lu\n", zcache_objnode_count_max);
	pr_info("zcache: flush_total=%lu\n", zcache_flush_total);
	pr_info("zcache: flush_found=%lu\n", zcache_flush_found);
	pr_info("zcache: flobj_total=%lu\n", zcache_flobj_total);
	pr_info("zcache: flobj_found=%lu\n", zcache_flobj_found);
	pr_info("zcache: failed_eph_puts=%lu\n", zcache_failed_eph_puts);
	pr_info("zcache: failed_pers_puts=%lu\n", zcache_failed_pers_puts);
	pr_info("zcache: failed_get_free_pages=%lu\n",
				zcache_failed_getfreepages);
	pr_info("zcache: failed_alloc=%lu\n", zcache_failed_alloc);
	pr_info("zcache: put_to_flush=%lu\n", zcache_put_to_flush);
	pr_info("zcache: compress_poor=%lu\n", zcache_compress_poor);
	pr_info("zcache: mean_compress_poor=%lu\n",
				zcache_mean_compress_poor);
	pr_info("zcache: eph_ate_tail=%lu\n", zcache_eph_ate_tail);
	pr_info("zcache: eph_ate_tail_failed=%lu\n",
				zcache_eph_ate_tail_failed);
	pr_info("zcache: pers_ate_eph=%lu\n", zcache_pers_ate_eph);
	pr_info("zcache: pers_ate_eph_failed=%lu\n",
				zcache_pers_ate_eph_failed);
	pr_info("zcache: evicted_eph_zpages=%lu\n", zcache_evicted_eph_zpages);
	pr_info("zcache: evicted_eph_pageframes=%lu\n",
				zcache_evicted_eph_pageframes);
	pr_info("zcache: eph_pageframes=%lu\n", zcache_eph_pageframes);
	pr_info("zcache: eph_pageframes_max=%lu\n", zcache_eph_pageframes_max);
	pr_info("zcache: pers_pageframes=%lu\n", zcache_pers_pageframes);
	pr_info("zcache: pers_pageframes_max=%lu\n",
				zcache_pers_pageframes_max);
	pr_info("zcache: eph_zpages=%lu\n", zcache_eph_zpages);
	pr_info("zcache: eph_zpages_max=%lu\n", zcache_eph_zpages_max);
	pr_info("zcache: pers_zpages=%lu\n", zcache_pers_zpages);
	pr_info("zcache: pers_zpages_max=%lu\n", zcache_pers_zpages_max);
	pr_info("zcache: eph_zbytes=%llu\n",
				(unsigned long long)zcache_eph_zbytes);
	pr_info("zcache: eph_zbytes_max=%llu\n",
				(unsigned long long)zcache_eph_zbytes_max);
	pr_info("zcache: pers_zbytes=%llu\n",
				(unsigned long long)zcache_pers_zbytes);
	pr_info("zcache: pers_zbytes_max=%llu\n",
			(unsigned long long)zcache_pers_zbytes_max);
}
#endif

/*
 * zcache core code starts here
 */

static struct zcache_client zcache_host;
static struct zcache_client zcache_clients[MAX_CLIENTS];

static inline bool is_local_client(struct zcache_client *cli)
{
	return cli == &zcache_host;
}

static struct zcache_client *zcache_get_client_by_id(uint16_t cli_id)
{
	struct zcache_client *cli = &zcache_host;

	if (cli_id != LOCAL_CLIENT) {
		if (cli_id >= MAX_CLIENTS)
			goto out;
		cli = &zcache_clients[cli_id];
	}
out:
	return cli;
}

/*
 * Tmem operations assume the poolid implies the invoking client.
 * Zcache only has one client (the kernel itself): LOCAL_CLIENT.
 * RAMster has each client numbered by cluster node, and a KVM version
 * of zcache would have one client per guest and each client might
 * have a poolid==N.
 */
struct tmem_pool *zcache_get_pool_by_id(uint16_t cli_id, uint16_t poolid)
{
	struct tmem_pool *pool = NULL;
	struct zcache_client *cli = NULL;

	cli = zcache_get_client_by_id(cli_id);
	if (cli == NULL)
		goto out;
	if (!is_local_client(cli))
		atomic_inc(&cli->refcount);
	if (poolid < MAX_POOLS_PER_CLIENT) {
		pool = cli->tmem_pools[poolid];
		if (pool != NULL)
			atomic_inc(&pool->refcount);
	}
out:
	return pool;
}

void zcache_put_pool(struct tmem_pool *pool)
{
	struct zcache_client *cli = NULL;

	if (pool == NULL)
		BUG();
	cli = pool->client;
	atomic_dec(&pool->refcount);
	if (!is_local_client(cli))
		atomic_dec(&cli->refcount);
}

int zcache_new_client(uint16_t cli_id)
{
	struct zcache_client *cli;
	int ret = -1;

	cli = zcache_get_client_by_id(cli_id);
	if (cli == NULL)
		goto out;
	if (cli->allocated)
		goto out;
	cli->allocated = 1;
	ret = 0;
out:
	return ret;
}

/*
 * zcache implementation for tmem host ops
 */

static struct tmem_objnode *zcache_objnode_alloc(struct tmem_pool *pool)
{
	struct tmem_objnode *objnode = NULL;
	struct zcache_preload *kp;
	int i;

	kp = &__get_cpu_var(zcache_preloads);
	for (i = 0; i < ARRAY_SIZE(kp->objnodes); i++) {
		objnode = kp->objnodes[i];
		if (objnode != NULL) {
			kp->objnodes[i] = NULL;
			break;
		}
	}
	BUG_ON(objnode == NULL);
	zcache_objnode_count = atomic_inc_return(&zcache_objnode_atomic);
	if (zcache_objnode_count > zcache_objnode_count_max)
		zcache_objnode_count_max = zcache_objnode_count;
	return objnode;
}

static void zcache_objnode_free(struct tmem_objnode *objnode,
					struct tmem_pool *pool)
{
	zcache_objnode_count =
		atomic_dec_return(&zcache_objnode_atomic);
	BUG_ON(zcache_objnode_count < 0);
	kmem_cache_free(zcache_objnode_cache, objnode);
}

static struct tmem_obj *zcache_obj_alloc(struct tmem_pool *pool)
{
	struct tmem_obj *obj = NULL;
	struct zcache_preload *kp;

	kp = &__get_cpu_var(zcache_preloads);
	obj = kp->obj;
	BUG_ON(obj == NULL);
	kp->obj = NULL;
	zcache_obj_count = atomic_inc_return(&zcache_obj_atomic);
	if (zcache_obj_count > zcache_obj_count_max)
		zcache_obj_count_max = zcache_obj_count;
	return obj;
}

static void zcache_obj_free(struct tmem_obj *obj, struct tmem_pool *pool)
{
	zcache_obj_count =
		atomic_dec_return(&zcache_obj_atomic);
	BUG_ON(zcache_obj_count < 0);
	kmem_cache_free(zcache_obj_cache, obj);
}

static struct tmem_hostops zcache_hostops = {
	.obj_alloc = zcache_obj_alloc,
	.obj_free = zcache_obj_free,
	.objnode_alloc = zcache_objnode_alloc,
	.objnode_free = zcache_objnode_free,
};

static struct page *zcache_alloc_page(void)
{
	struct page *page = alloc_page(ZCACHE_GFP_MASK);

	if (page != NULL)
		zcache_pageframes_alloced =
			atomic_inc_return(&zcache_pageframes_alloced_atomic);
	return page;
}

#ifdef FRONTSWAP_HAS_UNUSE
static void zcache_unacct_page(void)
{
	zcache_pageframes_freed =
		atomic_inc_return(&zcache_pageframes_freed_atomic);
}
#endif

static void zcache_free_page(struct page *page)
{
	long curr_pageframes;
	static long max_pageframes, min_pageframes;

	if (page == NULL)
		BUG();
	__free_page(page);
	zcache_pageframes_freed =
		atomic_inc_return(&zcache_pageframes_freed_atomic);
	curr_pageframes = zcache_pageframes_alloced -
			atomic_read(&zcache_pageframes_freed_atomic) -
			atomic_read(&zcache_eph_pageframes_atomic) -
			atomic_read(&zcache_pers_pageframes_atomic);
	if (curr_pageframes > max_pageframes)
		max_pageframes = curr_pageframes;
	if (curr_pageframes < min_pageframes)
		min_pageframes = curr_pageframes;
#ifdef ZCACHE_DEBUG
	if (curr_pageframes > 2L || curr_pageframes < -2L) {
		/* pr_info here */
	}
#endif
}

/*
 * zcache implementations for PAM page descriptor ops
 */

/* forward reference */
static void zcache_compress(struct page *from,
				void **out_va, unsigned *out_len);

static struct page *zcache_evict_eph_pageframe(void);

static void *zcache_pampd_eph_create(char *data, size_t size, bool raw,
					struct tmem_handle *th)
{
	void *pampd = NULL, *cdata = data;
	unsigned clen = size;
	struct page *page = (struct page *)(data), *newpage;

	if (!raw) {
		zcache_compress(page, &cdata, &clen);
		if (clen > zbud_max_buddy_size()) {
			zcache_compress_poor++;
			goto out;
		}
	} else {
		BUG_ON(clen > zbud_max_buddy_size());
	}

	/* look for space via an existing match first */
	pampd = (void *)zbud_match_prep(th, true, cdata, clen);
	if (pampd != NULL)
		goto got_pampd;

	/* no match, now we need to find (or free up) a full page */
	newpage = zcache_alloc_page();
	if (newpage != NULL)
		goto create_in_new_page;

	zcache_failed_getfreepages++;
	/* can't allocate a page, evict an ephemeral page via LRU */
	newpage = zcache_evict_eph_pageframe();
	if (newpage == NULL) {
		zcache_eph_ate_tail_failed++;
		goto out;
	}
	zcache_eph_ate_tail++;

create_in_new_page:
	pampd = (void *)zbud_create_prep(th, true, cdata, clen, newpage);
	BUG_ON(pampd == NULL);
	zcache_eph_pageframes =
		atomic_inc_return(&zcache_eph_pageframes_atomic);
	if (zcache_eph_pageframes > zcache_eph_pageframes_max)
		zcache_eph_pageframes_max = zcache_eph_pageframes;

got_pampd:
	zcache_eph_zbytes =
		atomic_long_add_return(clen, &zcache_eph_zbytes_atomic);
	if (zcache_eph_zbytes > zcache_eph_zbytes_max)
		zcache_eph_zbytes_max = zcache_eph_zbytes;
	zcache_eph_zpages = atomic_inc_return(&zcache_eph_zpages_atomic);
	if (zcache_eph_zpages > zcache_eph_zpages_max)
		zcache_eph_zpages_max = zcache_eph_zpages;
	if (ramster_enabled && raw)
		ramster_count_foreign_pages(true, 1);
out:
	return pampd;
}

static void *zcache_pampd_pers_create(char *data, size_t size, bool raw,
					struct tmem_handle *th)
{
	void *pampd = NULL, *cdata = data;
	unsigned clen = size;
	struct page *page = (struct page *)(data), *newpage;
	unsigned long zbud_mean_zsize;
	unsigned long curr_pers_zpages, total_zsize;

	if (data == NULL) {
		BUG_ON(!ramster_enabled);
		goto create_pampd;
	}
	curr_pers_zpages = zcache_pers_zpages;
/* FIXME CONFIG_RAMSTER... subtract atomic remote_pers_pages here? */
	if (!raw)
		zcache_compress(page, &cdata, &clen);
	/* reject if compression is too poor */
	if (clen > zbud_max_zsize) {
		zcache_compress_poor++;
		goto out;
	}
	/* reject if mean compression is too poor */
	if ((clen > zbud_max_mean_zsize) && (curr_pers_zpages > 0)) {
		total_zsize = zcache_pers_zbytes;
		if ((long)total_zsize < 0)
			total_zsize = 0;
		zbud_mean_zsize = div_u64(total_zsize,
					curr_pers_zpages);
		if (zbud_mean_zsize > zbud_max_mean_zsize) {
			zcache_mean_compress_poor++;
			goto out;
		}
	}

create_pampd:
	/* look for space via an existing match first */
	pampd = (void *)zbud_match_prep(th, false, cdata, clen);
	if (pampd != NULL)
		goto got_pampd;

	/* no match, now we need to find (or free up) a full page */
	newpage = zcache_alloc_page();
	if (newpage != NULL)
		goto create_in_new_page;
	/*
	 * FIXME do the following only if eph is oversized?
	 * if (zcache_eph_pageframes >
	 * (global_page_state(NR_LRU_BASE + LRU_ACTIVE_FILE) +
	 * global_page_state(NR_LRU_BASE + LRU_INACTIVE_FILE)))
	 */
	zcache_failed_getfreepages++;
	/* can't allocate a page, evict an ephemeral page via LRU */
	newpage = zcache_evict_eph_pageframe();
	if (newpage == NULL) {
		zcache_pers_ate_eph_failed++;
		goto out;
	}
	zcache_pers_ate_eph++;

create_in_new_page:
	pampd = (void *)zbud_create_prep(th, false, cdata, clen, newpage);
	BUG_ON(pampd == NULL);
	zcache_pers_pageframes =
		atomic_inc_return(&zcache_pers_pageframes_atomic);
	if (zcache_pers_pageframes > zcache_pers_pageframes_max)
		zcache_pers_pageframes_max = zcache_pers_pageframes;

got_pampd:
	zcache_pers_zpages = atomic_inc_return(&zcache_pers_zpages_atomic);
	if (zcache_pers_zpages > zcache_pers_zpages_max)
		zcache_pers_zpages_max = zcache_pers_zpages;
	zcache_pers_zbytes =
		atomic_long_add_return(clen, &zcache_pers_zbytes_atomic);
	if (zcache_pers_zbytes > zcache_pers_zbytes_max)
		zcache_pers_zbytes_max = zcache_pers_zbytes;
	if (ramster_enabled && raw)
		ramster_count_foreign_pages(false, 1);
out:
	return pampd;
}

/*
 * This is called directly from zcache_put_page to pre-allocate space
 * to store a zpage.
 */
void *zcache_pampd_create(char *data, unsigned int size, bool raw,
					int eph, struct tmem_handle *th)
{
	void *pampd = NULL;
	struct zcache_preload *kp;
	struct tmem_objnode *objnode;
	struct tmem_obj *obj;
	int i;

	BUG_ON(!irqs_disabled());
	/* pre-allocate per-cpu metadata */
	BUG_ON(zcache_objnode_cache == NULL);
	BUG_ON(zcache_obj_cache == NULL);
	kp = &__get_cpu_var(zcache_preloads);
	for (i = 0; i < ARRAY_SIZE(kp->objnodes); i++) {
		objnode = kp->objnodes[i];
		if (objnode == NULL) {
			objnode = kmem_cache_alloc(zcache_objnode_cache,
							ZCACHE_GFP_MASK);
			if (unlikely(objnode == NULL)) {
				zcache_failed_alloc++;
				goto out;
			}
			kp->objnodes[i] = objnode;
		}
	}
	if (kp->obj == NULL) {
		obj = kmem_cache_alloc(zcache_obj_cache, ZCACHE_GFP_MASK);
		kp->obj = obj;
	}
	if (unlikely(kp->obj == NULL)) {
		zcache_failed_alloc++;
		goto out;
	}
	/*
	 * ok, have all the metadata pre-allocated, now do the data
	 * but since how we allocate the data is dependent on ephemeral
	 * or persistent, we split the call here to different sub-functions
	 */
	if (eph)
		pampd = zcache_pampd_eph_create(data, size, raw, th);
	else
		pampd = zcache_pampd_pers_create(data, size, raw, th);
out:
	return pampd;
}

/*
 * This is a pamops called via tmem_put and is necessary to "finish"
 * a pampd creation.
 */
void zcache_pampd_create_finish(void *pampd, bool eph)
{
	zbud_create_finish((struct zbudref *)pampd, eph);
}

/*
 * This is passed as a function parameter to zbud_decompress so that
 * zbud need not be familiar with the details of crypto. It assumes that
 * the bytes from_va and to_va through from_va+size-1 and to_va+size-1 are
 * kmapped.  It must be successful, else there is a logic bug somewhere.
 */
static void zcache_decompress(char *from_va, unsigned int size, char *to_va)
{
	int ret;
	unsigned int outlen = PAGE_SIZE;

	ret = zcache_comp_op(ZCACHE_COMPOP_DECOMPRESS, from_va, size,
				to_va, &outlen);
	BUG_ON(ret);
	BUG_ON(outlen != PAGE_SIZE);
}

/*
 * Decompress from the kernel va to a pageframe
 */
void zcache_decompress_to_page(char *from_va, unsigned int size,
					struct page *to_page)
{
	char *to_va = kmap_atomic(to_page);
	zcache_decompress(from_va, size, to_va);
	kunmap_atomic(to_va);
}

/*
 * fill the pageframe corresponding to the struct page with the data
 * from the passed pampd
 */
static int zcache_pampd_get_data(char *data, size_t *sizep, bool raw,
					void *pampd, struct tmem_pool *pool,
					struct tmem_oid *oid, uint32_t index)
{
	int ret;
	bool eph = !is_persistent(pool);

	BUG_ON(preemptible());
	BUG_ON(eph);	/* fix later if shared pools get implemented */
	BUG_ON(pampd_is_remote(pampd));
	if (raw)
		ret = zbud_copy_from_zbud(data, (struct zbudref *)pampd,
						sizep, eph);
	else {
		ret = zbud_decompress((struct page *)(data),
					(struct zbudref *)pampd, false,
					zcache_decompress);
		*sizep = PAGE_SIZE;
	}
	return ret;
}

/*
 * fill the pageframe corresponding to the struct page with the data
 * from the passed pampd
 */
static int zcache_pampd_get_data_and_free(char *data, size_t *sizep, bool raw,
					void *pampd, struct tmem_pool *pool,
					struct tmem_oid *oid, uint32_t index)
{
	int ret;
	bool eph = !is_persistent(pool);
	struct page *page = NULL;
	unsigned int zsize, zpages;

	BUG_ON(preemptible());
	BUG_ON(pampd_is_remote(pampd));
	if (raw)
		ret = zbud_copy_from_zbud(data, (struct zbudref *)pampd,
						sizep, eph);
	else {
		ret = zbud_decompress((struct page *)(data),
					(struct zbudref *)pampd, eph,
					zcache_decompress);
		*sizep = PAGE_SIZE;
	}
	page = zbud_free_and_delist((struct zbudref *)pampd, eph,
					&zsize, &zpages);
	if (eph) {
		if (page)
			zcache_eph_pageframes =
			    atomic_dec_return(&zcache_eph_pageframes_atomic);
		zcache_eph_zpages =
		    atomic_sub_return(zpages, &zcache_eph_zpages_atomic);
		zcache_eph_zbytes =
		    atomic_long_sub_return(zsize, &zcache_eph_zbytes_atomic);
	} else {
		if (page)
			zcache_pers_pageframes =
			    atomic_dec_return(&zcache_pers_pageframes_atomic);
		zcache_pers_zpages =
		    atomic_sub_return(zpages, &zcache_pers_zpages_atomic);
		zcache_pers_zbytes =
		    atomic_long_sub_return(zsize, &zcache_pers_zbytes_atomic);
	}
	if (!is_local_client(pool->client))
		ramster_count_foreign_pages(eph, -1);
	if (page)
		zcache_free_page(page);
	return ret;
}

/*
 * free the pampd and remove it from any zcache lists
 * pampd must no longer be pointed to from any tmem data structures!
 */
static void zcache_pampd_free(void *pampd, struct tmem_pool *pool,
			      struct tmem_oid *oid, uint32_t index, bool acct)
{
	struct page *page = NULL;
	unsigned int zsize, zpages;

	BUG_ON(preemptible());
	if (pampd_is_remote(pampd)) {
		BUG_ON(!ramster_enabled);
		pampd = ramster_pampd_free(pampd, pool, oid, index, acct);
		if (pampd == NULL)
			return;
	}
	if (is_ephemeral(pool)) {
		page = zbud_free_and_delist((struct zbudref *)pampd,
						true, &zsize, &zpages);
		if (page)
			zcache_eph_pageframes =
			    atomic_dec_return(&zcache_eph_pageframes_atomic);
		zcache_eph_zpages =
		    atomic_sub_return(zpages, &zcache_eph_zpages_atomic);
		zcache_eph_zbytes =
		    atomic_long_sub_return(zsize, &zcache_eph_zbytes_atomic);
		/* FIXME CONFIG_RAMSTER... check acct parameter? */
	} else {
		page = zbud_free_and_delist((struct zbudref *)pampd,
						false, &zsize, &zpages);
		if (page)
			zcache_pers_pageframes =
			    atomic_dec_return(&zcache_pers_pageframes_atomic);
		zcache_pers_zpages =
		     atomic_sub_return(zpages, &zcache_pers_zpages_atomic);
		zcache_pers_zbytes =
		    atomic_long_sub_return(zsize, &zcache_pers_zbytes_atomic);
	}
	if (!is_local_client(pool->client))
		ramster_count_foreign_pages(is_ephemeral(pool), -1);
	if (page)
		zcache_free_page(page);
}

static struct tmem_pamops zcache_pamops = {
	.create_finish = zcache_pampd_create_finish,
	.get_data = zcache_pampd_get_data,
	.get_data_and_free = zcache_pampd_get_data_and_free,
	.free = zcache_pampd_free,
};

/*
 * zcache compression/decompression and related per-cpu stuff
 */

static DEFINE_PER_CPU(unsigned char *, zcache_dstmem);
#define ZCACHE_DSTMEM_ORDER 1

static void zcache_compress(struct page *from, void **out_va, unsigned *out_len)
{
	int ret;
	unsigned char *dmem = __get_cpu_var(zcache_dstmem);
	char *from_va;

	BUG_ON(!irqs_disabled());
	/* no buffer or no compressor so can't compress */
	BUG_ON(dmem == NULL);
	*out_len = PAGE_SIZE << ZCACHE_DSTMEM_ORDER;
	from_va = kmap_atomic(from);
	mb();
	ret = zcache_comp_op(ZCACHE_COMPOP_COMPRESS, from_va, PAGE_SIZE, dmem,
				out_len);
	BUG_ON(ret);
	*out_va = dmem;
	kunmap_atomic(from_va);
}

static int zcache_comp_cpu_up(int cpu)
{
	struct crypto_comp *tfm;

	tfm = crypto_alloc_comp(zcache_comp_name, 0, 0);
	if (IS_ERR(tfm))
		return NOTIFY_BAD;
	*per_cpu_ptr(zcache_comp_pcpu_tfms, cpu) = tfm;
	return NOTIFY_OK;
}

static void zcache_comp_cpu_down(int cpu)
{
	struct crypto_comp *tfm;

	tfm = *per_cpu_ptr(zcache_comp_pcpu_tfms, cpu);
	crypto_free_comp(tfm);
	*per_cpu_ptr(zcache_comp_pcpu_tfms, cpu) = NULL;
}

static int zcache_cpu_notifier(struct notifier_block *nb,
				unsigned long action, void *pcpu)
{
	int ret, i, cpu = (long)pcpu;
	struct zcache_preload *kp;

	switch (action) {
	case CPU_UP_PREPARE:
		ret = zcache_comp_cpu_up(cpu);
		if (ret != NOTIFY_OK) {
			pr_err("%s: can't allocate compressor xform\n",
				namestr);
			return ret;
		}
		per_cpu(zcache_dstmem, cpu) = (void *)__get_free_pages(
			GFP_KERNEL | __GFP_REPEAT, ZCACHE_DSTMEM_ORDER);
		if (ramster_enabled)
			ramster_cpu_up(cpu);
		break;
	case CPU_DEAD:
	case CPU_UP_CANCELED:
		zcache_comp_cpu_down(cpu);
		free_pages((unsigned long)per_cpu(zcache_dstmem, cpu),
			ZCACHE_DSTMEM_ORDER);
		per_cpu(zcache_dstmem, cpu) = NULL;
		kp = &per_cpu(zcache_preloads, cpu);
		for (i = 0; i < ARRAY_SIZE(kp->objnodes); i++) {
			if (kp->objnodes[i])
				kmem_cache_free(zcache_objnode_cache,
						kp->objnodes[i]);
		}
		if (kp->obj) {
			kmem_cache_free(zcache_obj_cache, kp->obj);
			kp->obj = NULL;
		}
		if (ramster_enabled)
			ramster_cpu_down(cpu);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block zcache_cpu_notifier_block = {
	.notifier_call = zcache_cpu_notifier
};

/*
 * The following code interacts with the zbud eviction and zbud
 * zombify code to access LRU pages
 */

static struct page *zcache_evict_eph_pageframe(void)
{
	struct page *page;
	unsigned int zsize = 0, zpages = 0;

	page = zbud_evict_pageframe_lru(&zsize, &zpages);
	if (page == NULL)
		goto out;
	zcache_eph_zbytes = atomic_long_sub_return(zsize,
					&zcache_eph_zbytes_atomic);
	zcache_eph_zpages = atomic_sub_return(zpages,
					&zcache_eph_zpages_atomic);
	zcache_evicted_eph_zpages++;
	zcache_eph_pageframes =
		atomic_dec_return(&zcache_eph_pageframes_atomic);
	zcache_evicted_eph_pageframes++;
out:
	return page;
}

#ifdef FRONTSWAP_HAS_UNUSE
static void unswiz(struct tmem_oid oid, u32 index,
				unsigned *type, pgoff_t *offset);

/*
 *  Choose an LRU persistent pageframe and attempt to "unuse" it by
 *  calling frontswap_unuse on both zpages.
 *
 *  This is work-in-progress.
 */

static int zcache_frontswap_unuse(void)
{
	struct tmem_handle th[2];
	int ret = -ENOMEM;
	int nzbuds, unuse_ret;
	unsigned type;
	struct page *newpage1 = NULL, *newpage2 = NULL;
	struct page *evictpage1 = NULL, *evictpage2 = NULL;
	pgoff_t offset;

	newpage1 = alloc_page(ZCACHE_GFP_MASK);
	newpage2 = alloc_page(ZCACHE_GFP_MASK);
	if (newpage1 == NULL)
		evictpage1 = zcache_evict_eph_pageframe();
	if (newpage2 == NULL)
		evictpage2 = zcache_evict_eph_pageframe();
	if (evictpage1 == NULL || evictpage2 == NULL)
		goto free_and_out;
	/* ok, we have two pages pre-allocated */
	nzbuds = zbud_make_zombie_lru(&th[0], NULL, NULL, false);
	if (nzbuds == 0) {
		ret = -ENOENT;
		goto free_and_out;
	}
	unswiz(th[0].oid, th[0].index, &type, &offset);
	unuse_ret = frontswap_unuse(type, offset,
				newpage1 != NULL ? newpage1 : evictpage1,
				ZCACHE_GFP_MASK);
	if (unuse_ret != 0)
		goto free_and_out;
	else if (evictpage1 != NULL)
		zcache_unacct_page();
	newpage1 = NULL;
	evictpage1 = NULL;
	if (nzbuds == 2) {
		unswiz(th[1].oid, th[1].index, &type, &offset);
		unuse_ret = frontswap_unuse(type, offset,
				newpage2 != NULL ? newpage2 : evictpage2,
				ZCACHE_GFP_MASK);
		if (unuse_ret != 0) {
			goto free_and_out;
		} else if (evictpage2 != NULL) {
			zcache_unacct_page();
		}
	}
	ret = 0;
	goto out;

free_and_out:
	if (newpage1 != NULL)
		__free_page(newpage1);
	if (newpage2 != NULL)
		__free_page(newpage2);
	if (evictpage1 != NULL)
		zcache_free_page(evictpage1);
	if (evictpage2 != NULL)
		zcache_free_page(evictpage2);
out:
	return ret;
}
#endif

/*
 * When zcache is disabled ("frozen"), pools can be created and destroyed,
 * but all puts (and thus all other operations that require memory allocation)
 * must fail.  If zcache is unfrozen, accepts puts, then frozen again,
 * data consistency requires all puts while frozen to be converted into
 * flushes.
 */
static bool zcache_freeze;

/*
 * This zcache shrinker interface reduces the number of ephemeral pageframes
 * used by zcache to approximately the same as the total number of LRU_FILE
 * pageframes in use.
 */
static int shrink_zcache_memory(struct shrinker *shrink,
				struct shrink_control *sc)
{
	static bool in_progress;
	int ret = -1;
	int nr = sc->nr_to_scan;
	int nr_evict = 0;
	int nr_unuse = 0;
	struct page *page;
#ifdef FRONTSWAP_HAS_UNUSE
	int unuse_ret;
#endif

	if (nr <= 0)
		goto skip_evict;

	/* don't allow more than one eviction thread at a time */
	if (in_progress)
		goto skip_evict;

	in_progress = true;

	/* we are going to ignore nr, and target a different value */
	zcache_last_active_file_pageframes =
		global_page_state(NR_LRU_BASE + LRU_ACTIVE_FILE);
	zcache_last_inactive_file_pageframes =
		global_page_state(NR_LRU_BASE + LRU_INACTIVE_FILE);
	nr_evict = zcache_eph_pageframes - zcache_last_active_file_pageframes +
		zcache_last_inactive_file_pageframes;
	while (nr_evict-- > 0) {
		page = zcache_evict_eph_pageframe();
		if (page == NULL)
			break;
		zcache_free_page(page);
	}

	zcache_last_active_anon_pageframes =
		global_page_state(NR_LRU_BASE + LRU_ACTIVE_ANON);
	zcache_last_inactive_anon_pageframes =
		global_page_state(NR_LRU_BASE + LRU_INACTIVE_ANON);
	nr_unuse = zcache_pers_pageframes - zcache_last_active_anon_pageframes +
		zcache_last_inactive_anon_pageframes;
#ifdef FRONTSWAP_HAS_UNUSE
	/* rate limit for testing */
	if (nr_unuse > 32)
		nr_unuse = 32;
	while (nr_unuse-- > 0) {
		unuse_ret = zcache_frontswap_unuse();
		if (unuse_ret == -ENOMEM)
			break;
	}
#endif
	in_progress = false;

skip_evict:
	/* resample: has changed, but maybe not all the way yet */
	zcache_last_active_file_pageframes =
		global_page_state(NR_LRU_BASE + LRU_ACTIVE_FILE);
	zcache_last_inactive_file_pageframes =
		global_page_state(NR_LRU_BASE + LRU_INACTIVE_FILE);
	ret = zcache_eph_pageframes - zcache_last_active_file_pageframes +
		zcache_last_inactive_file_pageframes;
	if (ret < 0)
		ret = 0;
	return ret;
}

static struct shrinker zcache_shrinker = {
	.shrink = shrink_zcache_memory,
	.seeks = DEFAULT_SEEKS,
};

/*
 * zcache shims between cleancache/frontswap ops and tmem
 */

/* FIXME rename these core routines to zcache_tmemput etc? */
int zcache_put_page(int cli_id, int pool_id, struct tmem_oid *oidp,
				uint32_t index, void *page,
				unsigned int size, bool raw, int ephemeral)
{
	struct tmem_pool *pool;
	struct tmem_handle th;
	int ret = -1;
	void *pampd = NULL;

	BUG_ON(!irqs_disabled());
	pool = zcache_get_pool_by_id(cli_id, pool_id);
	if (unlikely(pool == NULL))
		goto out;
	if (!zcache_freeze) {
		ret = 0;
		th.client_id = cli_id;
		th.pool_id = pool_id;
		th.oid = *oidp;
		th.index = index;
		pampd = zcache_pampd_create((char *)page, size, raw,
				ephemeral, &th);
		if (pampd == NULL) {
			ret = -ENOMEM;
			if (ephemeral)
				zcache_failed_eph_puts++;
			else
				zcache_failed_pers_puts++;
		} else {
			if (ramster_enabled)
				ramster_do_preload_flnode(pool);
			ret = tmem_put(pool, oidp, index, 0, pampd);
			if (ret < 0)
				BUG();
		}
		zcache_put_pool(pool);
	} else {
		zcache_put_to_flush++;
		if (ramster_enabled)
			ramster_do_preload_flnode(pool);
		if (atomic_read(&pool->obj_count) > 0)
			/* the put fails whether the flush succeeds or not */
			(void)tmem_flush_page(pool, oidp, index);
		zcache_put_pool(pool);
	}
out:
	return ret;
}

int zcache_get_page(int cli_id, int pool_id, struct tmem_oid *oidp,
				uint32_t index, void *page,
				size_t *sizep, bool raw, int get_and_free)
{
	struct tmem_pool *pool;
	int ret = -1;
	bool eph;

	if (!raw) {
		BUG_ON(irqs_disabled());
		BUG_ON(in_softirq());
	}
	pool = zcache_get_pool_by_id(cli_id, pool_id);
	eph = is_ephemeral(pool);
	if (likely(pool != NULL)) {
		if (atomic_read(&pool->obj_count) > 0)
			ret = tmem_get(pool, oidp, index, (char *)(page),
					sizep, raw, get_and_free);
		zcache_put_pool(pool);
	}
	WARN_ONCE((!is_ephemeral(pool) && (ret != 0)),
			"zcache_get fails on persistent pool, "
			"bad things are very likely to happen soon\n");
#ifdef RAMSTER_TESTING
	if (ret != 0 && ret != -1 && !(ret == -EINVAL && is_ephemeral(pool)))
		pr_err("TESTING zcache_get tmem_get returns ret=%d\n", ret);
#endif
	return ret;
}

int zcache_flush_page(int cli_id, int pool_id,
				struct tmem_oid *oidp, uint32_t index)
{
	struct tmem_pool *pool;
	int ret = -1;
	unsigned long flags;

	local_irq_save(flags);
	zcache_flush_total++;
	pool = zcache_get_pool_by_id(cli_id, pool_id);
	if (ramster_enabled)
		ramster_do_preload_flnode(pool);
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

int zcache_flush_object(int cli_id, int pool_id,
				struct tmem_oid *oidp)
{
	struct tmem_pool *pool;
	int ret = -1;
	unsigned long flags;

	local_irq_save(flags);
	zcache_flobj_total++;
	pool = zcache_get_pool_by_id(cli_id, pool_id);
	if (ramster_enabled)
		ramster_do_preload_flnode(pool);
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

static int zcache_client_destroy_pool(int cli_id, int pool_id)
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
	if (cli_id == LOCAL_CLIENT)
		pr_info("%s: destroyed local pool id=%d\n", namestr, pool_id);
	else
		pr_info("%s: destroyed pool id=%d, client=%d\n",
				namestr, pool_id, cli_id);
out:
	return ret;
}

int zcache_new_pool(uint16_t cli_id, uint32_t flags)
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
	pool = kmalloc(sizeof(struct tmem_pool), GFP_ATOMIC);
	if (pool == NULL) {
		pr_info("%s: pool creation failed: out of memory\n", namestr);
		goto out;
	}

	for (poolid = 0; poolid < MAX_POOLS_PER_CLIENT; poolid++)
		if (cli->tmem_pools[poolid] == NULL)
			break;
	if (poolid >= MAX_POOLS_PER_CLIENT) {
		pr_info("%s: pool creation failed: max exceeded\n", namestr);
		kfree(pool);
		poolid = -1;
		goto out;
	}
	atomic_set(&pool->refcount, 0);
	pool->client = cli;
	pool->pool_id = poolid;
	tmem_new_pool(pool, flags);
	cli->tmem_pools[poolid] = pool;
	if (cli_id == LOCAL_CLIENT)
		pr_info("%s: created %s local tmem pool, id=%d\n", namestr,
			flags & TMEM_POOL_PERSIST ? "persistent" : "ephemeral",
			poolid);
	else
		pr_info("%s: created %s tmem pool, id=%d, client=%d\n", namestr,
			flags & TMEM_POOL_PERSIST ? "persistent" : "ephemeral",
			poolid, cli_id);
out:
	if (cli != NULL)
		atomic_dec(&cli->refcount);
	return poolid;
}

static int zcache_local_new_pool(uint32_t flags)
{
	return zcache_new_pool(LOCAL_CLIENT, flags);
}

int zcache_autocreate_pool(unsigned int cli_id, unsigned int pool_id, bool eph)
{
	struct tmem_pool *pool;
	struct zcache_client *cli;
	uint32_t flags = eph ? 0 : TMEM_POOL_PERSIST;
	int ret = -1;

	BUG_ON(!ramster_enabled);
	if (cli_id == LOCAL_CLIENT)
		goto out;
	if (pool_id >= MAX_POOLS_PER_CLIENT)
		goto out;
	if (cli_id >= MAX_CLIENTS)
		goto out;

	cli = &zcache_clients[cli_id];
	if ((eph && disable_cleancache) || (!eph && disable_frontswap)) {
		pr_err("zcache_autocreate_pool: pool type disabled\n");
		goto out;
	}
	if (!cli->allocated) {
		if (zcache_new_client(cli_id)) {
			pr_err("zcache_autocreate_pool: can't create client\n");
			goto out;
		}
		cli = &zcache_clients[cli_id];
	}
	atomic_inc(&cli->refcount);
	pool = cli->tmem_pools[pool_id];
	if (pool != NULL) {
		if (pool->persistent && eph) {
			pr_err("zcache_autocreate_pool: type mismatch\n");
			goto out;
		}
		ret = 0;
		goto out;
	}
	pool = kmalloc(sizeof(struct tmem_pool), GFP_KERNEL);
	if (pool == NULL) {
		pr_info("%s: pool creation failed: out of memory\n", namestr);
		goto out;
	}
	atomic_set(&pool->refcount, 0);
	pool->client = cli;
	pool->pool_id = pool_id;
	tmem_new_pool(pool, flags);
	cli->tmem_pools[pool_id] = pool;
	pr_info("%s: AUTOcreated %s tmem poolid=%d, for remote client=%d\n",
		namestr, flags & TMEM_POOL_PERSIST ? "persistent" : "ephemeral",
		pool_id, cli_id);
	ret = 0;
out:
	if (cli != NULL)
		atomic_dec(&cli->refcount);
	return ret;
}

/**********
 * Two kernel functionalities currently can be layered on top of tmem.
 * These are "cleancache" which is used as a second-chance cache for clean
 * page cache pages; and "frontswap" which is used for swap pages
 * to avoid writes to disk.  A generic "shim" is provided here for each
 * to translate in-kernel semantics to zcache semantics.
 */

static void zcache_cleancache_put_page(int pool_id,
					struct cleancache_filekey key,
					pgoff_t index, struct page *page)
{
	u32 ind = (u32) index;
	struct tmem_oid oid = *(struct tmem_oid *)&key;

	if (!disable_cleancache_ignore_nonactive && !PageWasActive(page)) {
		zcache_eph_nonactive_puts_ignored++;
		return;
	}
	if (likely(ind == index))
		(void)zcache_put_page(LOCAL_CLIENT, pool_id, &oid, index,
					page, PAGE_SIZE, false, 1);
}

static int zcache_cleancache_get_page(int pool_id,
					struct cleancache_filekey key,
					pgoff_t index, struct page *page)
{
	u32 ind = (u32) index;
	struct tmem_oid oid = *(struct tmem_oid *)&key;
	size_t size;
	int ret = -1;

	if (likely(ind == index)) {
		ret = zcache_get_page(LOCAL_CLIENT, pool_id, &oid, index,
					page, &size, false, 0);
		BUG_ON(ret >= 0 && size != PAGE_SIZE);
		if (ret == 0)
			SetPageWasActive(page);
	}
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
		(void)zcache_client_destroy_pool(LOCAL_CLIENT, pool_id);
}

static int zcache_cleancache_init_fs(size_t pagesize)
{
	BUG_ON(sizeof(struct cleancache_filekey) !=
				sizeof(struct tmem_oid));
	BUG_ON(pagesize != PAGE_SIZE);
	return zcache_local_new_pool(0);
}

static int zcache_cleancache_init_shared_fs(char *uuid, size_t pagesize)
{
	/* shared pools are unsupported and map to private */
	BUG_ON(sizeof(struct cleancache_filekey) !=
				sizeof(struct tmem_oid));
	BUG_ON(pagesize != PAGE_SIZE);
	return zcache_local_new_pool(0);
}

static struct cleancache_ops zcache_cleancache_ops = {
	.put_page = zcache_cleancache_put_page,
	.get_page = zcache_cleancache_get_page,
	.invalidate_page = zcache_cleancache_flush_page,
	.invalidate_inode = zcache_cleancache_flush_inode,
	.invalidate_fs = zcache_cleancache_flush_fs,
	.init_shared_fs = zcache_cleancache_init_shared_fs,
	.init_fs = zcache_cleancache_init_fs
};

struct cleancache_ops zcache_cleancache_register_ops(void)
{
	struct cleancache_ops old_ops =
		cleancache_register_ops(&zcache_cleancache_ops);

	return old_ops;
}

/* a single tmem poolid is used for all frontswap "types" (swapfiles) */
static int zcache_frontswap_poolid __read_mostly = -1;

/*
 * Swizzling increases objects per swaptype, increasing tmem concurrency
 * for heavy swaploads.  Later, larger nr_cpus -> larger SWIZ_BITS
 * Setting SWIZ_BITS to 27 basically reconstructs the swap entry from
 * frontswap_get_page(), but has side-effects. Hence using 8.
 */
#define SWIZ_BITS		8
#define SWIZ_MASK		((1 << SWIZ_BITS) - 1)
#define _oswiz(_type, _ind)	((_type << SWIZ_BITS) | (_ind & SWIZ_MASK))
#define iswiz(_ind)		(_ind >> SWIZ_BITS)

static inline struct tmem_oid oswiz(unsigned type, u32 ind)
{
	struct tmem_oid oid = { .oid = { 0 } };
	oid.oid[0] = _oswiz(type, ind);
	return oid;
}

#ifdef FRONTSWAP_HAS_UNUSE
static void unswiz(struct tmem_oid oid, u32 index,
				unsigned *type, pgoff_t *offset)
{
	*type = (unsigned)(oid.oid[0] >> SWIZ_BITS);
	*offset = (pgoff_t)((index << SWIZ_BITS) |
			(oid.oid[0] & SWIZ_MASK));
}
#endif

static int zcache_frontswap_put_page(unsigned type, pgoff_t offset,
					struct page *page)
{
	u64 ind64 = (u64)offset;
	u32 ind = (u32)offset;
	struct tmem_oid oid = oswiz(type, ind);
	int ret = -1;
	unsigned long flags;

	BUG_ON(!PageLocked(page));
	if (!disable_frontswap_ignore_nonactive && !PageWasActive(page)) {
		zcache_pers_nonactive_puts_ignored++;
		ret = -ERANGE;
		goto out;
	}
	if (likely(ind64 == ind)) {
		local_irq_save(flags);
		ret = zcache_put_page(LOCAL_CLIENT, zcache_frontswap_poolid,
					&oid, iswiz(ind),
					page, PAGE_SIZE, false, 0);
		local_irq_restore(flags);
	}
out:
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
	size_t size;
	int ret = -1, get_and_free;

	if (frontswap_has_exclusive_gets)
		get_and_free = 1;
	else
		get_and_free = -1;
	BUG_ON(!PageLocked(page));
	if (likely(ind64 == ind)) {
		ret = zcache_get_page(LOCAL_CLIENT, zcache_frontswap_poolid,
					&oid, iswiz(ind),
					page, &size, false, get_and_free);
		BUG_ON(ret >= 0 && size != PAGE_SIZE);
	}
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
			zcache_local_new_pool(TMEM_POOL_PERSIST);
}

static struct frontswap_ops zcache_frontswap_ops = {
	.store = zcache_frontswap_put_page,
	.load = zcache_frontswap_get_page,
	.invalidate_page = zcache_frontswap_flush_page,
	.invalidate_area = zcache_frontswap_flush_area,
	.init = zcache_frontswap_init
};

struct frontswap_ops zcache_frontswap_register_ops(void)
{
	struct frontswap_ops old_ops =
		frontswap_register_ops(&zcache_frontswap_ops);

	return old_ops;
}

/*
 * zcache initialization
 * NOTE FOR NOW zcache or ramster MUST BE PROVIDED AS A KERNEL BOOT PARAMETER
 * OR NOTHING HAPPENS!
 */

static int __init enable_zcache(char *s)
{
	zcache_enabled = 1;
	return 1;
}
__setup("zcache", enable_zcache);

static int __init enable_ramster(char *s)
{
	zcache_enabled = 1;
#ifdef CONFIG_RAMSTER
	ramster_enabled = 1;
#endif
	return 1;
}
__setup("ramster", enable_ramster);

/* allow independent dynamic disabling of cleancache and frontswap */

static int __init no_cleancache(char *s)
{
	disable_cleancache = 1;
	return 1;
}

__setup("nocleancache", no_cleancache);

static int __init no_frontswap(char *s)
{
	disable_frontswap = 1;
	return 1;
}

__setup("nofrontswap", no_frontswap);

static int __init no_frontswap_exclusive_gets(char *s)
{
	frontswap_has_exclusive_gets = false;
	return 1;
}

__setup("nofrontswapexclusivegets", no_frontswap_exclusive_gets);

static int __init no_frontswap_ignore_nonactive(char *s)
{
	disable_frontswap_ignore_nonactive = 1;
	return 1;
}

__setup("nofrontswapignorenonactive", no_frontswap_ignore_nonactive);

static int __init no_cleancache_ignore_nonactive(char *s)
{
	disable_cleancache_ignore_nonactive = 1;
	return 1;
}

__setup("nocleancacheignorenonactive", no_cleancache_ignore_nonactive);

static int __init enable_zcache_compressor(char *s)
{
	strncpy(zcache_comp_name, s, ZCACHE_COMP_NAME_SZ);
	zcache_enabled = 1;
	return 1;
}
__setup("zcache=", enable_zcache_compressor);


static int __init zcache_comp_init(void)
{
	int ret = 0;

	/* check crypto algorithm */
	if (*zcache_comp_name != '\0') {
		ret = crypto_has_comp(zcache_comp_name, 0, 0);
		if (!ret)
			pr_info("zcache: %s not supported\n",
					zcache_comp_name);
	}
	if (!ret)
		strcpy(zcache_comp_name, "lzo");
	ret = crypto_has_comp(zcache_comp_name, 0, 0);
	if (!ret) {
		ret = 1;
		goto out;
	}
	pr_info("zcache: using %s compressor\n", zcache_comp_name);

	/* alloc percpu transforms */
	ret = 0;
	zcache_comp_pcpu_tfms = alloc_percpu(struct crypto_comp *);
	if (!zcache_comp_pcpu_tfms)
		ret = 1;
out:
	return ret;
}

static int __init zcache_init(void)
{
	int ret = 0;

	if (ramster_enabled) {
		namestr = "ramster";
		ramster_register_pamops(&zcache_pamops);
	}
#ifdef CONFIG_DEBUG_FS
	zcache_debugfs_init();
#endif
	if (zcache_enabled) {
		unsigned int cpu;

		tmem_register_hostops(&zcache_hostops);
		tmem_register_pamops(&zcache_pamops);
		ret = register_cpu_notifier(&zcache_cpu_notifier_block);
		if (ret) {
			pr_err("%s: can't register cpu notifier\n", namestr);
			goto out;
		}
		ret = zcache_comp_init();
		if (ret) {
			pr_err("%s: compressor initialization failed\n",
				namestr);
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
		pr_err("%s: can't create client\n", namestr);
		goto out;
	}
	zbud_init();
	if (zcache_enabled && !disable_cleancache) {
		struct cleancache_ops old_ops;

		register_shrinker(&zcache_shrinker);
		old_ops = zcache_cleancache_register_ops();
		pr_info("%s: cleancache enabled using kernel transcendent "
			"memory and compression buddies\n", namestr);
#ifdef ZCACHE_DEBUG
		pr_info("%s: cleancache: ignorenonactive = %d\n",
			namestr, !disable_cleancache_ignore_nonactive);
#endif
		if (old_ops.init_fs != NULL)
			pr_warn("%s: cleancache_ops overridden\n", namestr);
	}
	if (zcache_enabled && !disable_frontswap) {
		struct frontswap_ops old_ops;

		old_ops = zcache_frontswap_register_ops();
		if (frontswap_has_exclusive_gets)
			frontswap_tmem_exclusive_gets(true);
		pr_info("%s: frontswap enabled using kernel transcendent "
			"memory and compression buddies\n", namestr);
#ifdef ZCACHE_DEBUG
		pr_info("%s: frontswap: excl gets = %d active only = %d\n",
			namestr, frontswap_has_exclusive_gets,
			!disable_frontswap_ignore_nonactive);
#endif
		if (old_ops.init != NULL)
			pr_warn("%s: frontswap_ops overridden\n", namestr);
	}
	if (ramster_enabled)
		ramster_init(!disable_cleancache, !disable_frontswap,
				frontswap_has_exclusive_gets);
out:
	return ret;
}

late_initcall(zcache_init);
