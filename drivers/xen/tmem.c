/*
 * Xen implementation for transcendent memory (tmem)
 *
 * Copyright (C) 2009-2011 Oracle Corp.  All rights reserved.
 * Author: Dan Magenheimer
 */

#define pr_fmt(fmt) "xen:" KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/cleancache.h>
#include <linux/frontswap.h>

#include <xen/xen.h>
#include <xen/interface/xen.h>
#include <xen/page.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>
#include <xen/tmem.h>

#ifndef CONFIG_XEN_TMEM_MODULE
bool __read_mostly tmem_enabled = false;

static int __init enable_tmem(char *s)
{
	tmem_enabled = true;
	return 1;
}
__setup("tmem", enable_tmem);
#endif

#ifdef CONFIG_CLEANCACHE
static bool cleancache __read_mostly = true;
module_param(cleancache, bool, S_IRUGO);
static bool selfballooning __read_mostly = true;
module_param(selfballooning, bool, S_IRUGO);
#endif /* CONFIG_CLEANCACHE */

#ifdef CONFIG_FRONTSWAP
static bool frontswap __read_mostly = true;
module_param(frontswap, bool, S_IRUGO);
#else /* CONFIG_FRONTSWAP */
#define frontswap (0)
#endif /* CONFIG_FRONTSWAP */

#ifdef CONFIG_XEN_SELFBALLOONING
static bool selfshrinking __read_mostly = true;
module_param(selfshrinking, bool, S_IRUGO);
#endif /* CONFIG_XEN_SELFBALLOONING */

#define TMEM_CONTROL               0
#define TMEM_NEW_POOL              1
#define TMEM_DESTROY_POOL          2
#define TMEM_NEW_PAGE              3
#define TMEM_PUT_PAGE              4
#define TMEM_GET_PAGE              5
#define TMEM_FLUSH_PAGE            6
#define TMEM_FLUSH_OBJECT          7
#define TMEM_READ                  8
#define TMEM_WRITE                 9
#define TMEM_XCHG                 10

/* Bits for HYPERVISOR_tmem_op(TMEM_NEW_POOL) */
#define TMEM_POOL_PERSIST          1
#define TMEM_POOL_SHARED           2
#define TMEM_POOL_PAGESIZE_SHIFT   4
#define TMEM_VERSION_SHIFT        24


struct tmem_pool_uuid {
	u64 uuid_lo;
	u64 uuid_hi;
};

struct tmem_oid {
	u64 oid[3];
};

#define TMEM_POOL_PRIVATE_UUID	{ 0, 0 }

/* flags for tmem_ops.new_pool */
#define TMEM_POOL_PERSIST          1
#define TMEM_POOL_SHARED           2

/* xen tmem foundation ops/hypercalls */

static inline int xen_tmem_op(u32 tmem_cmd, u32 tmem_pool, struct tmem_oid oid,
	u32 index, unsigned long gmfn, u32 tmem_offset, u32 pfn_offset, u32 len)
{
	struct tmem_op op;
	int rc = 0;

	op.cmd = tmem_cmd;
	op.pool_id = tmem_pool;
	op.u.gen.oid[0] = oid.oid[0];
	op.u.gen.oid[1] = oid.oid[1];
	op.u.gen.oid[2] = oid.oid[2];
	op.u.gen.index = index;
	op.u.gen.tmem_offset = tmem_offset;
	op.u.gen.pfn_offset = pfn_offset;
	op.u.gen.len = len;
	set_xen_guest_handle(op.u.gen.gmfn, (void *)gmfn);
	rc = HYPERVISOR_tmem_op(&op);
	return rc;
}

static int xen_tmem_new_pool(struct tmem_pool_uuid uuid,
				u32 flags, unsigned long pagesize)
{
	struct tmem_op op;
	int rc = 0, pageshift;

	for (pageshift = 0; pagesize != 1; pageshift++)
		pagesize >>= 1;
	flags |= (pageshift - 12) << TMEM_POOL_PAGESIZE_SHIFT;
	flags |= TMEM_SPEC_VERSION << TMEM_VERSION_SHIFT;
	op.cmd = TMEM_NEW_POOL;
	op.u.new.uuid[0] = uuid.uuid_lo;
	op.u.new.uuid[1] = uuid.uuid_hi;
	op.u.new.flags = flags;
	rc = HYPERVISOR_tmem_op(&op);
	return rc;
}

/* xen generic tmem ops */

static int xen_tmem_put_page(u32 pool_id, struct tmem_oid oid,
			     u32 index, struct page *page)
{
	return xen_tmem_op(TMEM_PUT_PAGE, pool_id, oid, index,
			   xen_page_to_gfn(page), 0, 0, 0);
}

static int xen_tmem_get_page(u32 pool_id, struct tmem_oid oid,
			     u32 index, struct page *page)
{
	return xen_tmem_op(TMEM_GET_PAGE, pool_id, oid, index,
			   xen_page_to_gfn(page), 0, 0, 0);
}

static int xen_tmem_flush_page(u32 pool_id, struct tmem_oid oid, u32 index)
{
	return xen_tmem_op(TMEM_FLUSH_PAGE, pool_id, oid, index,
		0, 0, 0, 0);
}

static int xen_tmem_flush_object(u32 pool_id, struct tmem_oid oid)
{
	return xen_tmem_op(TMEM_FLUSH_OBJECT, pool_id, oid, 0, 0, 0, 0, 0);
}


#ifdef CONFIG_CLEANCACHE
static int xen_tmem_destroy_pool(u32 pool_id)
{
	struct tmem_oid oid = { { 0 } };

	return xen_tmem_op(TMEM_DESTROY_POOL, pool_id, oid, 0, 0, 0, 0, 0);
}

/* cleancache ops */

static void tmem_cleancache_put_page(int pool, struct cleancache_filekey key,
				     pgoff_t index, struct page *page)
{
	u32 ind = (u32) index;
	struct tmem_oid oid = *(struct tmem_oid *)&key;

	if (pool < 0)
		return;
	if (ind != index)
		return;
	mb(); /* ensure page is quiescent; tmem may address it with an alias */
	(void)xen_tmem_put_page((u32)pool, oid, ind, page);
}

static int tmem_cleancache_get_page(int pool, struct cleancache_filekey key,
				    pgoff_t index, struct page *page)
{
	u32 ind = (u32) index;
	struct tmem_oid oid = *(struct tmem_oid *)&key;
	int ret;

	/* translate return values to linux semantics */
	if (pool < 0)
		return -1;
	if (ind != index)
		return -1;
	ret = xen_tmem_get_page((u32)pool, oid, ind, page);
	if (ret == 1)
		return 0;
	else
		return -1;
}

static void tmem_cleancache_flush_page(int pool, struct cleancache_filekey key,
				       pgoff_t index)
{
	u32 ind = (u32) index;
	struct tmem_oid oid = *(struct tmem_oid *)&key;

	if (pool < 0)
		return;
	if (ind != index)
		return;
	(void)xen_tmem_flush_page((u32)pool, oid, ind);
}

static void tmem_cleancache_flush_inode(int pool, struct cleancache_filekey key)
{
	struct tmem_oid oid = *(struct tmem_oid *)&key;

	if (pool < 0)
		return;
	(void)xen_tmem_flush_object((u32)pool, oid);
}

static void tmem_cleancache_flush_fs(int pool)
{
	if (pool < 0)
		return;
	(void)xen_tmem_destroy_pool((u32)pool);
}

static int tmem_cleancache_init_fs(size_t pagesize)
{
	struct tmem_pool_uuid uuid_private = TMEM_POOL_PRIVATE_UUID;

	return xen_tmem_new_pool(uuid_private, 0, pagesize);
}

static int tmem_cleancache_init_shared_fs(char *uuid, size_t pagesize)
{
	struct tmem_pool_uuid shared_uuid;

	shared_uuid.uuid_lo = *(u64 *)uuid;
	shared_uuid.uuid_hi = *(u64 *)(&uuid[8]);
	return xen_tmem_new_pool(shared_uuid, TMEM_POOL_SHARED, pagesize);
}

static struct cleancache_ops tmem_cleancache_ops = {
	.put_page = tmem_cleancache_put_page,
	.get_page = tmem_cleancache_get_page,
	.invalidate_page = tmem_cleancache_flush_page,
	.invalidate_inode = tmem_cleancache_flush_inode,
	.invalidate_fs = tmem_cleancache_flush_fs,
	.init_shared_fs = tmem_cleancache_init_shared_fs,
	.init_fs = tmem_cleancache_init_fs
};
#endif

#ifdef CONFIG_FRONTSWAP
/* frontswap tmem operations */

/* a single tmem poolid is used for all frontswap "types" (swapfiles) */
static int tmem_frontswap_poolid;

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

/* returns 0 if the page was successfully put into frontswap, -1 if not */
static int tmem_frontswap_store(unsigned type, pgoff_t offset,
				   struct page *page)
{
	u64 ind64 = (u64)offset;
	u32 ind = (u32)offset;
	int pool = tmem_frontswap_poolid;
	int ret;

	if (pool < 0)
		return -1;
	if (ind64 != ind)
		return -1;
	mb(); /* ensure page is quiescent; tmem may address it with an alias */
	ret = xen_tmem_put_page(pool, oswiz(type, ind), iswiz(ind), page);
	/* translate Xen tmem return values to linux semantics */
	if (ret == 1)
		return 0;
	else
		return -1;
}

/*
 * returns 0 if the page was successfully gotten from frontswap, -1 if
 * was not present (should never happen!)
 */
static int tmem_frontswap_load(unsigned type, pgoff_t offset,
				   struct page *page)
{
	u64 ind64 = (u64)offset;
	u32 ind = (u32)offset;
	int pool = tmem_frontswap_poolid;
	int ret;

	if (pool < 0)
		return -1;
	if (ind64 != ind)
		return -1;
	ret = xen_tmem_get_page(pool, oswiz(type, ind), iswiz(ind), page);
	/* translate Xen tmem return values to linux semantics */
	if (ret == 1)
		return 0;
	else
		return -1;
}

/* flush a single page from frontswap */
static void tmem_frontswap_flush_page(unsigned type, pgoff_t offset)
{
	u64 ind64 = (u64)offset;
	u32 ind = (u32)offset;
	int pool = tmem_frontswap_poolid;

	if (pool < 0)
		return;
	if (ind64 != ind)
		return;
	(void) xen_tmem_flush_page(pool, oswiz(type, ind), iswiz(ind));
}

/* flush all pages from the passed swaptype */
static void tmem_frontswap_flush_area(unsigned type)
{
	int pool = tmem_frontswap_poolid;
	int ind;

	if (pool < 0)
		return;
	for (ind = SWIZ_MASK; ind >= 0; ind--)
		(void)xen_tmem_flush_object(pool, oswiz(type, ind));
}

static void tmem_frontswap_init(unsigned ignored)
{
	struct tmem_pool_uuid private = TMEM_POOL_PRIVATE_UUID;

	/* a single tmem poolid is used for all frontswap "types" (swapfiles) */
	if (tmem_frontswap_poolid < 0)
		tmem_frontswap_poolid =
		    xen_tmem_new_pool(private, TMEM_POOL_PERSIST, PAGE_SIZE);
}

static struct frontswap_ops tmem_frontswap_ops = {
	.store = tmem_frontswap_store,
	.load = tmem_frontswap_load,
	.invalidate_page = tmem_frontswap_flush_page,
	.invalidate_area = tmem_frontswap_flush_area,
	.init = tmem_frontswap_init
};
#endif

static int __init xen_tmem_init(void)
{
	if (!xen_domain())
		return 0;
#ifdef CONFIG_FRONTSWAP
	if (tmem_enabled && frontswap) {
		char *s = "";

		tmem_frontswap_poolid = -1;
		frontswap_register_ops(&tmem_frontswap_ops);
		pr_info("frontswap enabled, RAM provided by Xen Transcendent Memory%s\n",
			s);
	}
#endif
#ifdef CONFIG_CLEANCACHE
	BUILD_BUG_ON(sizeof(struct cleancache_filekey) != sizeof(struct tmem_oid));
	if (tmem_enabled && cleancache) {
		int err;

		err = cleancache_register_ops(&tmem_cleancache_ops);
		if (err)
			pr_warn("xen-tmem: failed to enable cleancache: %d\n",
				err);
		else
			pr_info("cleancache enabled, RAM provided by "
				"Xen Transcendent Memory\n");
	}
#endif
#ifdef CONFIG_XEN_SELFBALLOONING
	/*
	 * There is no point of driving pages to the swap system if they
	 * aren't going anywhere in tmem universe.
	 */
	if (!frontswap) {
		selfshrinking = false;
		selfballooning = false;
	}
	xen_selfballoon_init(selfballooning, selfshrinking);
#endif
	return 0;
}

module_init(xen_tmem_init)
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dan Magenheimer <dan.magenheimer@oracle.com>");
MODULE_DESCRIPTION("Shim to Xen transcendent memory");
