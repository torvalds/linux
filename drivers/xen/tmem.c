/*
 * Xen implementation for transcendent memory (tmem)
 *
 * Copyright (C) 2009-2010 Oracle Corp.  All rights reserved.
 * Author: Dan Magenheimer
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/cleancache.h>

#include <xen/xen.h>
#include <xen/interface/xen.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/page.h>
#include <asm/xen/hypervisor.h>

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
			     u32 index, unsigned long pfn)
{
	unsigned long gmfn = xen_pv_domain() ? pfn_to_mfn(pfn) : pfn;

	return xen_tmem_op(TMEM_PUT_PAGE, pool_id, oid, index,
		gmfn, 0, 0, 0);
}

static int xen_tmem_get_page(u32 pool_id, struct tmem_oid oid,
			     u32 index, unsigned long pfn)
{
	unsigned long gmfn = xen_pv_domain() ? pfn_to_mfn(pfn) : pfn;

	return xen_tmem_op(TMEM_GET_PAGE, pool_id, oid, index,
		gmfn, 0, 0, 0);
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

static int xen_tmem_destroy_pool(u32 pool_id)
{
	struct tmem_oid oid = { { 0 } };

	return xen_tmem_op(TMEM_DESTROY_POOL, pool_id, oid, 0, 0, 0, 0, 0);
}

int tmem_enabled;

static int __init enable_tmem(char *s)
{
	tmem_enabled = 1;
	return 1;
}

__setup("tmem", enable_tmem);

/* cleancache ops */

static void tmem_cleancache_put_page(int pool, struct cleancache_filekey key,
				     pgoff_t index, struct page *page)
{
	u32 ind = (u32) index;
	struct tmem_oid oid = *(struct tmem_oid *)&key;
	unsigned long pfn = page_to_pfn(page);

	if (pool < 0)
		return;
	if (ind != index)
		return;
	mb(); /* ensure page is quiescent; tmem may address it with an alias */
	(void)xen_tmem_put_page((u32)pool, oid, ind, pfn);
}

static int tmem_cleancache_get_page(int pool, struct cleancache_filekey key,
				    pgoff_t index, struct page *page)
{
	u32 ind = (u32) index;
	struct tmem_oid oid = *(struct tmem_oid *)&key;
	unsigned long pfn = page_to_pfn(page);
	int ret;

	/* translate return values to linux semantics */
	if (pool < 0)
		return -1;
	if (ind != index)
		return -1;
	ret = xen_tmem_get_page((u32)pool, oid, ind, pfn);
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

static int use_cleancache = 1;

static int __init no_cleancache(char *s)
{
	use_cleancache = 0;
	return 1;
}

__setup("nocleancache", no_cleancache);

static struct cleancache_ops tmem_cleancache_ops = {
	.put_page = tmem_cleancache_put_page,
	.get_page = tmem_cleancache_get_page,
	.flush_page = tmem_cleancache_flush_page,
	.flush_inode = tmem_cleancache_flush_inode,
	.flush_fs = tmem_cleancache_flush_fs,
	.init_shared_fs = tmem_cleancache_init_shared_fs,
	.init_fs = tmem_cleancache_init_fs
};

static int __init xen_tmem_init(void)
{
	struct cleancache_ops old_ops;

	if (!xen_domain())
		return 0;
#ifdef CONFIG_CLEANCACHE
	BUG_ON(sizeof(struct cleancache_filekey) != sizeof(struct tmem_oid));
	if (tmem_enabled && use_cleancache) {
		char *s = "";
		old_ops = cleancache_register_ops(&tmem_cleancache_ops);
		if (old_ops.init_fs != NULL)
			s = " (WARNING: cleancache_ops overridden)";
		printk(KERN_INFO "cleancache enabled, RAM provided by "
				 "Xen Transcendent Memory%s\n", s);
	}
#endif
	return 0;
}

module_init(xen_tmem_init)
