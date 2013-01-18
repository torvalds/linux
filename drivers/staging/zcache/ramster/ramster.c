/*
 * ramster.c
 *
 * Copyright (c) 2010-2012, Dan Magenheimer, Oracle Corp.
 *
 * RAMster implements peer-to-peer transcendent memory, allowing a "cluster" of
 * kernels to dynamically pool their RAM so that a RAM-hungry workload on one
 * machine can temporarily and transparently utilize RAM on another machine
 * which is presumably idle or running a non-RAM-hungry workload.
 *
 * RAMster combines a clustering and messaging foundation based on the ocfs2
 * cluster layer with the in-kernel compression implementation of zcache, and
 * adds code to glue them together.  When a page is "put" to RAMster, it is
 * compressed and stored locally.  Periodically, a thread will "remotify" these
 * pages by sending them via messages to a remote machine.  When the page is
 * later needed as indicated by a page fault, a "get" is issued.  If the data
 * is local, it is uncompressed and the fault is resolved.  If the data is
 * remote, a message is sent to fetch the data and the faulting thread sleeps;
 * when the data arrives, the thread awakens, the data is decompressed and
 * the fault is resolved.

 * As of V5, clusters up to eight nodes are supported; each node can remotify
 * pages to one specified node, so clusters can be configured as clients to
 * a "memory server".  Some simple policy is in place that will need to be
 * refined over time.  Larger clusters and fault-resistant protocols can also
 * be added over time.
 */

#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/highmem.h>
#include <linux/list.h>
#include <linux/lzo.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/frontswap.h>
#include "../tmem.h"
#include "../zcache.h"
#include "../zbud.h"
#include "ramster.h"
#include "ramster_nodemanager.h"
#include "tcp.h"

#define RAMSTER_TESTING

#ifndef CONFIG_SYSFS
#error "ramster needs sysfs to define cluster nodes to use"
#endif

static bool use_cleancache __read_mostly;
static bool use_frontswap __read_mostly;
static bool use_frontswap_exclusive_gets __read_mostly;

/* These must be sysfs not debugfs as they are checked/used by userland!! */
static unsigned long ramster_interface_revision __read_mostly =
	R2NM_API_VERSION; /* interface revision must match userspace! */
static unsigned long ramster_pers_remotify_enable __read_mostly;
static unsigned long ramster_eph_remotify_enable __read_mostly;
static atomic_t ramster_remote_pers_pages = ATOMIC_INIT(0);
#define MANUAL_NODES 8
static bool ramster_nodes_manual_up[MANUAL_NODES] __read_mostly;
static int ramster_remote_target_nodenum __read_mostly = -1;

/* these counters are made available via debugfs */
static long ramster_flnodes;
static atomic_t ramster_flnodes_atomic = ATOMIC_INIT(0);
static unsigned long ramster_flnodes_max;
static long ramster_foreign_eph_pages;
static atomic_t ramster_foreign_eph_pages_atomic = ATOMIC_INIT(0);
static unsigned long ramster_foreign_eph_pages_max;
static long ramster_foreign_pers_pages;
static atomic_t ramster_foreign_pers_pages_atomic = ATOMIC_INIT(0);
static unsigned long ramster_foreign_pers_pages_max;
static unsigned long ramster_eph_pages_remoted;
static unsigned long ramster_pers_pages_remoted;
static unsigned long ramster_eph_pages_remote_failed;
static unsigned long ramster_pers_pages_remote_failed;
static unsigned long ramster_remote_eph_pages_succ_get;
static unsigned long ramster_remote_pers_pages_succ_get;
static unsigned long ramster_remote_eph_pages_unsucc_get;
static unsigned long ramster_remote_pers_pages_unsucc_get;
static unsigned long ramster_pers_pages_remote_nomem;
static unsigned long ramster_remote_objects_flushed;
static unsigned long ramster_remote_object_flushes_failed;
static unsigned long ramster_remote_pages_flushed;
static unsigned long ramster_remote_page_flushes_failed;
/* FIXME frontswap selfshrinking knobs in debugfs? */

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#define	zdfs	debugfs_create_size_t
#define	zdfs64	debugfs_create_u64
static int __init ramster_debugfs_init(void)
{
	struct dentry *root = debugfs_create_dir("ramster", NULL);
	if (root == NULL)
		return -ENXIO;

	zdfs("eph_pages_remoted", S_IRUGO, root, &ramster_eph_pages_remoted);
	zdfs("pers_pages_remoted", S_IRUGO, root, &ramster_pers_pages_remoted);
	zdfs("eph_pages_remote_failed", S_IRUGO, root,
			&ramster_eph_pages_remote_failed);
	zdfs("pers_pages_remote_failed", S_IRUGO, root,
			&ramster_pers_pages_remote_failed);
	zdfs("remote_eph_pages_succ_get", S_IRUGO, root,
			&ramster_remote_eph_pages_succ_get);
	zdfs("remote_pers_pages_succ_get", S_IRUGO, root,
			&ramster_remote_pers_pages_succ_get);
	zdfs("remote_eph_pages_unsucc_get", S_IRUGO, root,
			&ramster_remote_eph_pages_unsucc_get);
	zdfs("remote_pers_pages_unsucc_get", S_IRUGO, root,
			&ramster_remote_pers_pages_unsucc_get);
	zdfs("pers_pages_remote_nomem", S_IRUGO, root,
			&ramster_pers_pages_remote_nomem);
	zdfs("remote_objects_flushed", S_IRUGO, root,
			&ramster_remote_objects_flushed);
	zdfs("remote_pages_flushed", S_IRUGO, root,
			&ramster_remote_pages_flushed);
	zdfs("remote_object_flushes_failed", S_IRUGO, root,
			&ramster_remote_object_flushes_failed);
	zdfs("remote_page_flushes_failed", S_IRUGO, root,
			&ramster_remote_page_flushes_failed);
	zdfs("foreign_eph_pages", S_IRUGO, root,
			&ramster_foreign_eph_pages);
	zdfs("foreign_eph_pages_max", S_IRUGO, root,
			&ramster_foreign_eph_pages_max);
	zdfs("foreign_pers_pages", S_IRUGO, root,
			&ramster_foreign_pers_pages);
	zdfs("foreign_pers_pages_max", S_IRUGO, root,
			&ramster_foreign_pers_pages_max);
	return 0;
}
#undef	zdebugfs
#undef	zdfs64
#endif

static LIST_HEAD(ramster_rem_op_list);
static DEFINE_SPINLOCK(ramster_rem_op_list_lock);
static DEFINE_PER_CPU(struct ramster_preload, ramster_preloads);

static DEFINE_PER_CPU(unsigned char *, ramster_remoteputmem1);
static DEFINE_PER_CPU(unsigned char *, ramster_remoteputmem2);

static struct kmem_cache *ramster_flnode_cache __read_mostly;

static struct flushlist_node *ramster_flnode_alloc(struct tmem_pool *pool)
{
	struct flushlist_node *flnode = NULL;
	struct ramster_preload *kp;

	kp = &__get_cpu_var(ramster_preloads);
	flnode = kp->flnode;
	BUG_ON(flnode == NULL);
	kp->flnode = NULL;
	ramster_flnodes = atomic_inc_return(&ramster_flnodes_atomic);
	if (ramster_flnodes > ramster_flnodes_max)
		ramster_flnodes_max = ramster_flnodes;
	return flnode;
}

/* the "flush list" asynchronously collects pages to remotely flush */
#define FLUSH_ENTIRE_OBJECT ((uint32_t)-1)
static void ramster_flnode_free(struct flushlist_node *flnode,
				struct tmem_pool *pool)
{
	int flnodes;

	flnodes = atomic_dec_return(&ramster_flnodes_atomic);
	BUG_ON(flnodes < 0);
	kmem_cache_free(ramster_flnode_cache, flnode);
}

int ramster_do_preload_flnode(struct tmem_pool *pool)
{
	struct ramster_preload *kp;
	struct flushlist_node *flnode;
	int ret = -ENOMEM;

	BUG_ON(!irqs_disabled());
	if (unlikely(ramster_flnode_cache == NULL))
		BUG();
	kp = &__get_cpu_var(ramster_preloads);
	flnode = kmem_cache_alloc(ramster_flnode_cache, GFP_ATOMIC);
	if (unlikely(flnode == NULL) && kp->flnode == NULL)
		BUG();  /* FIXME handle more gracefully, but how??? */
	else if (kp->flnode == NULL)
		kp->flnode = flnode;
	else
		kmem_cache_free(ramster_flnode_cache, flnode);
	return ret;
}

/*
 * Called by the message handler after a (still compressed) page has been
 * fetched from the remote machine in response to an "is_remote" tmem_get
 * or persistent tmem_localify.  For a tmem_get, "extra" is the address of
 * the page that is to be filled to successfully resolve the tmem_get; for
 * a (persistent) tmem_localify, "extra" is NULL (as the data is placed only
 * in the local zcache).  "data" points to "size" bytes of (compressed) data
 * passed in the message.  In the case of a persistent remote get, if
 * pre-allocation was successful (see ramster_repatriate_preload), the page
 * is placed into both local zcache and at "extra".
 */
int ramster_localify(int pool_id, struct tmem_oid *oidp, uint32_t index,
			char *data, unsigned int size, void *extra)
{
	int ret = -ENOENT;
	unsigned long flags;
	struct tmem_pool *pool;
	bool eph, delete = false;
	void *pampd, *saved_hb;
	struct tmem_obj *obj;

	pool = zcache_get_pool_by_id(LOCAL_CLIENT, pool_id);
	if (unlikely(pool == NULL))
		/* pool doesn't exist anymore */
		goto out;
	eph = is_ephemeral(pool);
	local_irq_save(flags);  /* FIXME: maybe only disable softirqs? */
	pampd = tmem_localify_get_pampd(pool, oidp, index, &obj, &saved_hb);
	if (pampd == NULL) {
		/* hmmm... must have been a flush while waiting */
#ifdef RAMSTER_TESTING
		pr_err("UNTESTED pampd==NULL in ramster_localify\n");
#endif
		if (eph)
			ramster_remote_eph_pages_unsucc_get++;
		else
			ramster_remote_pers_pages_unsucc_get++;
		obj = NULL;
		goto finish;
	} else if (unlikely(!pampd_is_remote(pampd))) {
		/* hmmm... must have been a dup put while waiting */
#ifdef RAMSTER_TESTING
		pr_err("UNTESTED dup while waiting in ramster_localify\n");
#endif
		if (eph)
			ramster_remote_eph_pages_unsucc_get++;
		else
			ramster_remote_pers_pages_unsucc_get++;
		obj = NULL;
		pampd = NULL;
		ret = -EEXIST;
		goto finish;
	} else if (size == 0) {
		/* no remote data, delete the local is_remote pampd */
		pampd = NULL;
		if (eph)
			ramster_remote_eph_pages_unsucc_get++;
		else
			BUG();
		delete = true;
		goto finish;
	}
	if (pampd_is_intransit(pampd)) {
		/*
		 *  a pampd is marked intransit if it is remote and space has
		 *  been allocated for it locally (note, only happens for
		 *  persistent pages, in which case the remote copy is freed)
		 */
		BUG_ON(eph);
		pampd = pampd_mask_intransit_and_remote(pampd);
		zbud_copy_to_zbud(pampd, data, size);
	} else {
		/*
		 * setting pampd to NULL tells tmem_localify_finish to leave
		 * pampd alone... meaning it is left pointing to the
		 * remote copy
		 */
		pampd = NULL;
		obj = NULL;
	}
	/*
	 * but in all cases, we decompress direct-to-memory to complete
	 * the remotify and return success
	 */
	BUG_ON(extra == NULL);
	zcache_decompress_to_page(data, size, (struct page *)extra);
	if (eph)
		ramster_remote_eph_pages_succ_get++;
	else
		ramster_remote_pers_pages_succ_get++;
	ret = 0;
finish:
	tmem_localify_finish(obj, index, pampd, saved_hb, delete);
	zcache_put_pool(pool);
	local_irq_restore(flags);
out:
	return ret;
}

void ramster_pampd_new_obj(struct tmem_obj *obj)
{
	obj->extra = NULL;
}

void ramster_pampd_free_obj(struct tmem_pool *pool, struct tmem_obj *obj,
				bool pool_destroy)
{
	struct flushlist_node *flnode;

	BUG_ON(preemptible());
	if (obj->extra == NULL)
		return;
	if (pool_destroy && is_ephemeral(pool))
		/* FIXME don't bother with remote eph data for now */
		return;
	BUG_ON(!pampd_is_remote(obj->extra));
	flnode = ramster_flnode_alloc(pool);
	flnode->xh.client_id = pampd_remote_node(obj->extra);
	flnode->xh.pool_id = pool->pool_id;
	flnode->xh.oid = obj->oid;
	flnode->xh.index = FLUSH_ENTIRE_OBJECT;
	flnode->rem_op.op = RAMSTER_REMOTIFY_FLUSH_OBJ;
	spin_lock(&ramster_rem_op_list_lock);
	list_add(&flnode->rem_op.list, &ramster_rem_op_list);
	spin_unlock(&ramster_rem_op_list_lock);
}

/*
 * Called on a remote persistent tmem_get to attempt to preallocate
 * local storage for the data contained in the remote persistent page.
 * If successfully preallocated, returns the pampd, marked as remote and
 * in_transit.  Else returns NULL.  Note that the appropriate tmem data
 * structure must be locked.
 */
void *ramster_pampd_repatriate_preload(void *pampd, struct tmem_pool *pool,
					struct tmem_oid *oidp, uint32_t index,
					bool *intransit)
{
	int clen = pampd_remote_size(pampd), c;
	void *ret_pampd = NULL;
	unsigned long flags;
	struct tmem_handle th;

	BUG_ON(!pampd_is_remote(pampd));
	BUG_ON(is_ephemeral(pool));
	if (use_frontswap_exclusive_gets)
		/* don't need local storage */
		goto out;
	if (pampd_is_intransit(pampd)) {
		/*
		 * to avoid multiple allocations (and maybe a memory leak)
		 * don't preallocate if already in the process of being
		 * repatriated
		 */
		*intransit = true;
		goto out;
	}
	*intransit = false;
	local_irq_save(flags);
	th.client_id = pampd_remote_node(pampd);
	th.pool_id = pool->pool_id;
	th.oid = *oidp;
	th.index = index;
	ret_pampd = zcache_pampd_create(NULL, clen, true, false, &th);
	if (ret_pampd != NULL) {
		/*
		 *  a pampd is marked intransit if it is remote and space has
		 *  been allocated for it locally (note, only happens for
		 *  persistent pages, in which case the remote copy is freed)
		 */
		ret_pampd = pampd_mark_intransit(ret_pampd);
		c = atomic_dec_return(&ramster_remote_pers_pages);
		WARN_ON_ONCE(c < 0);
	} else {
		ramster_pers_pages_remote_nomem++;
	}
	local_irq_restore(flags);
out:
	return ret_pampd;
}

/*
 * Called on a remote tmem_get to invoke a message to fetch the page.
 * Might sleep so no tmem locks can be held.  "extra" is passed
 * all the way through the round-trip messaging to ramster_localify.
 */
int ramster_pampd_repatriate(void *fake_pampd, void *real_pampd,
				struct tmem_pool *pool,
				struct tmem_oid *oid, uint32_t index,
				bool free, void *extra)
{
	struct tmem_xhandle xh;
	int ret;

	if (pampd_is_intransit(real_pampd))
		/* have local space pre-reserved, so free remote copy */
		free = true;
	xh = tmem_xhandle_fill(LOCAL_CLIENT, pool, oid, index);
	/* unreliable request/response for now */
	ret = r2net_remote_async_get(&xh, free,
					pampd_remote_node(fake_pampd),
					pampd_remote_size(fake_pampd),
					pampd_remote_cksum(fake_pampd),
					extra);
	return ret;
}

bool ramster_pampd_is_remote(void *pampd)
{
	return pampd_is_remote(pampd);
}

int ramster_pampd_replace_in_obj(void *new_pampd, struct tmem_obj *obj)
{
	int ret = -1;

	if (new_pampd != NULL) {
		if (obj->extra == NULL)
			obj->extra = new_pampd;
		/* enforce that all remote pages in an object reside
		 * in the same node! */
		else if (pampd_remote_node(new_pampd) !=
				pampd_remote_node((void *)(obj->extra)))
			BUG();
		ret = 0;
	}
	return ret;
}

void *ramster_pampd_free(void *pampd, struct tmem_pool *pool,
			      struct tmem_oid *oid, uint32_t index, bool acct)
{
	bool eph = is_ephemeral(pool);
	void *local_pampd = NULL;
	int c;

	BUG_ON(preemptible());
	BUG_ON(!pampd_is_remote(pampd));
	WARN_ON(acct == false);
	if (oid == NULL) {
		/*
		 * a NULL oid means to ignore this pampd free
		 * as the remote freeing will be handled elsewhere
		 */
	} else if (eph) {
		/* FIXME remote flush optional but probably good idea */
	} else if (pampd_is_intransit(pampd)) {
		/* did a pers remote get_and_free, so just free local */
		local_pampd = pampd_mask_intransit_and_remote(pampd);
	} else {
		struct flushlist_node *flnode =
			ramster_flnode_alloc(pool);

		flnode->xh.client_id = pampd_remote_node(pampd);
		flnode->xh.pool_id = pool->pool_id;
		flnode->xh.oid = *oid;
		flnode->xh.index = index;
		flnode->rem_op.op = RAMSTER_REMOTIFY_FLUSH_PAGE;
		spin_lock(&ramster_rem_op_list_lock);
		list_add(&flnode->rem_op.list, &ramster_rem_op_list);
		spin_unlock(&ramster_rem_op_list_lock);
		c = atomic_dec_return(&ramster_remote_pers_pages);
		WARN_ON_ONCE(c < 0);
	}
	return local_pampd;
}

void ramster_count_foreign_pages(bool eph, int count)
{
	int c;

	BUG_ON(count != 1 && count != -1);
	if (eph) {
		if (count > 0) {
			c = atomic_inc_return(
					&ramster_foreign_eph_pages_atomic);
			if (c > ramster_foreign_eph_pages_max)
				ramster_foreign_eph_pages_max = c;
		} else {
			c = atomic_dec_return(&ramster_foreign_eph_pages_atomic);
			WARN_ON_ONCE(c < 0);
		}
		ramster_foreign_eph_pages = c;
	} else {
		if (count > 0) {
			c = atomic_inc_return(
					&ramster_foreign_pers_pages_atomic);
			if (c > ramster_foreign_pers_pages_max)
				ramster_foreign_pers_pages_max = c;
		} else {
			c = atomic_dec_return(
					&ramster_foreign_pers_pages_atomic);
			WARN_ON_ONCE(c < 0);
		}
		ramster_foreign_pers_pages = c;
	}
}

/*
 * For now, just push over a few pages every few seconds to
 * ensure that it basically works
 */
static struct workqueue_struct *ramster_remotify_workqueue;
static void ramster_remotify_process(struct work_struct *work);
static DECLARE_DELAYED_WORK(ramster_remotify_worker,
		ramster_remotify_process);

static void ramster_remotify_queue_delayed_work(unsigned long delay)
{
	if (!queue_delayed_work(ramster_remotify_workqueue,
				&ramster_remotify_worker, delay))
		pr_err("ramster_remotify: bad workqueue\n");
}

static void ramster_remote_flush_page(struct flushlist_node *flnode)
{
	struct tmem_xhandle *xh;
	int remotenode, ret;

	preempt_disable();
	xh = &flnode->xh;
	remotenode = flnode->xh.client_id;
	ret = r2net_remote_flush(xh, remotenode);
	if (ret >= 0)
		ramster_remote_pages_flushed++;
	else
		ramster_remote_page_flushes_failed++;
	preempt_enable_no_resched();
	ramster_flnode_free(flnode, NULL);
}

static void ramster_remote_flush_object(struct flushlist_node *flnode)
{
	struct tmem_xhandle *xh;
	int remotenode, ret;

	preempt_disable();
	xh = &flnode->xh;
	remotenode = flnode->xh.client_id;
	ret = r2net_remote_flush_object(xh, remotenode);
	if (ret >= 0)
		ramster_remote_objects_flushed++;
	else
		ramster_remote_object_flushes_failed++;
	preempt_enable_no_resched();
	ramster_flnode_free(flnode, NULL);
}

int ramster_remotify_pageframe(bool eph)
{
	struct tmem_xhandle xh;
	unsigned int size;
	int remotenode, ret, zbuds;
	struct tmem_pool *pool;
	unsigned long flags;
	unsigned char cksum;
	char *p;
	int i, j;
	unsigned char *tmpmem[2];
	struct tmem_handle th[2];
	unsigned int zsize[2];

	tmpmem[0] = __get_cpu_var(ramster_remoteputmem1);
	tmpmem[1] = __get_cpu_var(ramster_remoteputmem2);
	local_bh_disable();
	zbuds = zbud_make_zombie_lru(&th[0], &tmpmem[0], &zsize[0], eph);
	/* now OK to release lock set in caller */
	local_bh_enable();
	if (zbuds == 0)
		goto out;
	BUG_ON(zbuds > 2);
	for (i = 0; i < zbuds; i++) {
		xh.client_id = th[i].client_id;
		xh.pool_id = th[i].pool_id;
		xh.oid = th[i].oid;
		xh.index = th[i].index;
		size = zsize[i];
		BUG_ON(size == 0 || size > zbud_max_buddy_size());
		for (p = tmpmem[i], cksum = 0, j = 0; j < size; j++)
			cksum += *p++;
		ret = r2net_remote_put(&xh, tmpmem[i], size, eph, &remotenode);
		if (ret != 0) {
		/*
		 * This is some form of a memory leak... if the remote put
		 * fails, there will never be another attempt to remotify
		 * this page.  But since we've dropped the zv pointer,
		 * the page may have been freed or the data replaced
		 * so we can't just "put it back" in the remote op list.
		 * Even if we could, not sure where to put it in the list
		 * because there may be flushes that must be strictly
		 * ordered vs the put.  So leave this as a FIXME for now.
		 * But count them so we know if it becomes a problem.
		 */
			if (eph)
				ramster_eph_pages_remote_failed++;
			else
				ramster_pers_pages_remote_failed++;
			break;
		} else {
			if (!eph)
				atomic_inc(&ramster_remote_pers_pages);
		}
		if (eph)
			ramster_eph_pages_remoted++;
		else
			ramster_pers_pages_remoted++;
		/*
		 * data was successfully remoted so change the local version to
		 * point to the remote node where it landed
		 */
		local_bh_disable();
		pool = zcache_get_pool_by_id(LOCAL_CLIENT, xh.pool_id);
		local_irq_save(flags);
		(void)tmem_replace(pool, &xh.oid, xh.index,
				pampd_make_remote(remotenode, size, cksum));
		local_irq_restore(flags);
		zcache_put_pool(pool);
		local_bh_enable();
	}
out:
	return zbuds;
}

static void zcache_do_remotify_flushes(void)
{
	struct ramster_remotify_hdr *rem_op;
	union remotify_list_node *u;

	while (1) {
		spin_lock(&ramster_rem_op_list_lock);
		if (list_empty(&ramster_rem_op_list)) {
			spin_unlock(&ramster_rem_op_list_lock);
			goto out;
		}
		rem_op = list_first_entry(&ramster_rem_op_list,
				struct ramster_remotify_hdr, list);
		list_del_init(&rem_op->list);
		spin_unlock(&ramster_rem_op_list_lock);
		u = (union remotify_list_node *)rem_op;
		switch (rem_op->op) {
		case RAMSTER_REMOTIFY_FLUSH_PAGE:
			ramster_remote_flush_page((struct flushlist_node *)u);
			break;
		case RAMSTER_REMOTIFY_FLUSH_OBJ:
			ramster_remote_flush_object((struct flushlist_node *)u);
			break;
		default:
			BUG();
		}
	}
out:
	return;
}

static void ramster_remotify_process(struct work_struct *work)
{
	static bool remotify_in_progress;
	int i;

	BUG_ON(irqs_disabled());
	if (remotify_in_progress)
		goto requeue;
	if (ramster_remote_target_nodenum == -1)
		goto requeue;
	remotify_in_progress = true;
	if (use_cleancache && ramster_eph_remotify_enable) {
		for (i = 0; i < 100; i++) {
			zcache_do_remotify_flushes();
			(void)ramster_remotify_pageframe(true);
		}
	}
	if (use_frontswap && ramster_pers_remotify_enable) {
		for (i = 0; i < 100; i++) {
			zcache_do_remotify_flushes();
			(void)ramster_remotify_pageframe(false);
		}
	}
	remotify_in_progress = false;
requeue:
	ramster_remotify_queue_delayed_work(HZ);
}

void __init ramster_remotify_init(void)
{
	unsigned long n = 60UL;
	ramster_remotify_workqueue =
		create_singlethread_workqueue("ramster_remotify");
	ramster_remotify_queue_delayed_work(n * HZ);
}

static ssize_t ramster_manual_node_up_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	int i;
	char *p = buf;
	for (i = 0; i < MANUAL_NODES; i++)
		if (ramster_nodes_manual_up[i])
			p += sprintf(p, "%d ", i);
	p += sprintf(p, "\n");
	return p - buf;
}

static ssize_t ramster_manual_node_up_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int err;
	unsigned long node_num;

	err = kstrtoul(buf, 10, &node_num);
	if (err) {
		pr_err("ramster: bad strtoul?\n");
		return -EINVAL;
	}
	if (node_num >= MANUAL_NODES) {
		pr_err("ramster: bad node_num=%lu?\n", node_num);
		return -EINVAL;
	}
	if (ramster_nodes_manual_up[node_num]) {
		pr_err("ramster: node %d already up, ignoring\n",
							(int)node_num);
	} else {
		ramster_nodes_manual_up[node_num] = true;
		r2net_hb_node_up_manual((int)node_num);
	}
	return count;
}

static struct kobj_attribute ramster_manual_node_up_attr = {
	.attr = { .name = "manual_node_up", .mode = 0644 },
	.show = ramster_manual_node_up_show,
	.store = ramster_manual_node_up_store,
};

static ssize_t ramster_remote_target_nodenum_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	if (ramster_remote_target_nodenum == -1UL)
		return sprintf(buf, "unset\n");
	else
		return sprintf(buf, "%d\n", ramster_remote_target_nodenum);
}

static ssize_t ramster_remote_target_nodenum_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int err;
	unsigned long node_num;

	err = kstrtoul(buf, 10, &node_num);
	if (err) {
		pr_err("ramster: bad strtoul?\n");
		return -EINVAL;
	} else if (node_num == -1UL) {
		pr_err("ramster: disabling all remotification, "
			"data may still reside on remote nodes however\n");
		return -EINVAL;
	} else if (node_num >= MANUAL_NODES) {
		pr_err("ramster: bad node_num=%lu?\n", node_num);
		return -EINVAL;
	} else if (!ramster_nodes_manual_up[node_num]) {
		pr_err("ramster: node %d not up, ignoring setting "
			"of remotification target\n", (int)node_num);
	} else if (r2net_remote_target_node_set((int)node_num) >= 0) {
		pr_info("ramster: node %d set as remotification target\n",
				(int)node_num);
		ramster_remote_target_nodenum = (int)node_num;
	} else {
		pr_err("ramster: bad num to node node_num=%d?\n",
				(int)node_num);
		return -EINVAL;
	}
	return count;
}

static struct kobj_attribute ramster_remote_target_nodenum_attr = {
	.attr = { .name = "remote_target_nodenum", .mode = 0644 },
	.show = ramster_remote_target_nodenum_show,
	.store = ramster_remote_target_nodenum_store,
};

#define RAMSTER_SYSFS_RO(_name) \
	static ssize_t ramster_##_name##_show(struct kobject *kobj, \
				struct kobj_attribute *attr, char *buf) \
	{ \
		return sprintf(buf, "%lu\n", ramster_##_name); \
	} \
	static struct kobj_attribute ramster_##_name##_attr = { \
		.attr = { .name = __stringify(_name), .mode = 0444 }, \
		.show = ramster_##_name##_show, \
	}

#define RAMSTER_SYSFS_RW(_name) \
	static ssize_t ramster_##_name##_show(struct kobject *kobj, \
				struct kobj_attribute *attr, char *buf) \
	{ \
		return sprintf(buf, "%lu\n", ramster_##_name); \
	} \
	static ssize_t ramster_##_name##_store(struct kobject *kobj, \
		struct kobj_attribute *attr, const char *buf, size_t count) \
	{ \
		int err; \
		unsigned long enable; \
		err = kstrtoul(buf, 10, &enable); \
		if (err) \
			return -EINVAL; \
		ramster_##_name = enable; \
		return count; \
	} \
	static struct kobj_attribute ramster_##_name##_attr = { \
		.attr = { .name = __stringify(_name), .mode = 0644 }, \
		.show = ramster_##_name##_show, \
		.store = ramster_##_name##_store, \
	}

#define RAMSTER_SYSFS_RO_ATOMIC(_name) \
	static ssize_t ramster_##_name##_show(struct kobject *kobj, \
				struct kobj_attribute *attr, char *buf) \
	{ \
	    return sprintf(buf, "%d\n", atomic_read(&ramster_##_name)); \
	} \
	static struct kobj_attribute ramster_##_name##_attr = { \
		.attr = { .name = __stringify(_name), .mode = 0444 }, \
		.show = ramster_##_name##_show, \
	}

RAMSTER_SYSFS_RO(interface_revision);
RAMSTER_SYSFS_RO_ATOMIC(remote_pers_pages);
RAMSTER_SYSFS_RW(pers_remotify_enable);
RAMSTER_SYSFS_RW(eph_remotify_enable);

static struct attribute *ramster_attrs[] = {
	&ramster_interface_revision_attr.attr,
	&ramster_remote_pers_pages_attr.attr,
	&ramster_manual_node_up_attr.attr,
	&ramster_remote_target_nodenum_attr.attr,
	&ramster_pers_remotify_enable_attr.attr,
	&ramster_eph_remotify_enable_attr.attr,
	NULL,
};

static struct attribute_group ramster_attr_group = {
	.attrs = ramster_attrs,
	.name = "ramster",
};

/*
 * frontswap selfshrinking
 */

/* In HZ, controls frequency of worker invocation. */
static unsigned int selfshrink_interval __read_mostly = 5;
/* Enable/disable with sysfs. */
static bool frontswap_selfshrinking __read_mostly;

static void selfshrink_process(struct work_struct *work);
static DECLARE_DELAYED_WORK(selfshrink_worker, selfshrink_process);

/* Enable/disable with kernel boot option. */
static bool use_frontswap_selfshrink __initdata = true;

/*
 * The default values for the following parameters were deemed reasonable
 * by experimentation, may be workload-dependent, and can all be
 * adjusted via sysfs.
 */

/* Control rate for frontswap shrinking. Higher hysteresis is slower. */
static unsigned int frontswap_hysteresis __read_mostly = 20;

/*
 * Number of selfshrink worker invocations to wait before observing that
 * frontswap selfshrinking should commence. Note that selfshrinking does
 * not use a separate worker thread.
 */
static unsigned int frontswap_inertia __read_mostly = 3;

/* Countdown to next invocation of frontswap_shrink() */
static unsigned long frontswap_inertia_counter;

/*
 * Invoked by the selfshrink worker thread, uses current number of pages
 * in frontswap (frontswap_curr_pages()), previous status, and control
 * values (hysteresis and inertia) to determine if frontswap should be
 * shrunk and what the new frontswap size should be.  Note that
 * frontswap_shrink is essentially a partial swapoff that immediately
 * transfers pages from the "swap device" (frontswap) back into kernel
 * RAM; despite the name, frontswap "shrinking" is very different from
 * the "shrinker" interface used by the kernel MM subsystem to reclaim
 * memory.
 */
static void frontswap_selfshrink(void)
{
	static unsigned long cur_frontswap_pages;
	static unsigned long last_frontswap_pages;
	static unsigned long tgt_frontswap_pages;

	last_frontswap_pages = cur_frontswap_pages;
	cur_frontswap_pages = frontswap_curr_pages();
	if (!cur_frontswap_pages ||
			(cur_frontswap_pages > last_frontswap_pages)) {
		frontswap_inertia_counter = frontswap_inertia;
		return;
	}
	if (frontswap_inertia_counter && --frontswap_inertia_counter)
		return;
	if (cur_frontswap_pages <= frontswap_hysteresis)
		tgt_frontswap_pages = 0;
	else
		tgt_frontswap_pages = cur_frontswap_pages -
			(cur_frontswap_pages / frontswap_hysteresis);
	frontswap_shrink(tgt_frontswap_pages);
}

static int __init ramster_nofrontswap_selfshrink_setup(char *s)
{
	use_frontswap_selfshrink = false;
	return 1;
}

__setup("noselfshrink", ramster_nofrontswap_selfshrink_setup);

static void selfshrink_process(struct work_struct *work)
{
	if (frontswap_selfshrinking && frontswap_enabled) {
		frontswap_selfshrink();
		schedule_delayed_work(&selfshrink_worker,
			selfshrink_interval * HZ);
	}
}

void ramster_cpu_up(int cpu)
{
	unsigned char *p1 = kzalloc(PAGE_SIZE, GFP_KERNEL | __GFP_REPEAT);
	unsigned char *p2 = kzalloc(PAGE_SIZE, GFP_KERNEL | __GFP_REPEAT);
	BUG_ON(!p1 || !p2);
	per_cpu(ramster_remoteputmem1, cpu) = p1;
	per_cpu(ramster_remoteputmem2, cpu) = p2;
}

void ramster_cpu_down(int cpu)
{
	struct ramster_preload *kp;

	kfree(per_cpu(ramster_remoteputmem1, cpu));
	per_cpu(ramster_remoteputmem1, cpu) = NULL;
	kfree(per_cpu(ramster_remoteputmem2, cpu));
	per_cpu(ramster_remoteputmem2, cpu) = NULL;
	kp = &per_cpu(ramster_preloads, cpu);
	if (kp->flnode) {
		kmem_cache_free(ramster_flnode_cache, kp->flnode);
		kp->flnode = NULL;
	}
}

void ramster_register_pamops(struct tmem_pamops *pamops)
{
	pamops->free_obj = ramster_pampd_free_obj;
	pamops->new_obj = ramster_pampd_new_obj;
	pamops->replace_in_obj = ramster_pampd_replace_in_obj;
	pamops->is_remote = ramster_pampd_is_remote;
	pamops->repatriate = ramster_pampd_repatriate;
	pamops->repatriate_preload = ramster_pampd_repatriate_preload;
}

void __init ramster_init(bool cleancache, bool frontswap,
				bool frontswap_exclusive_gets)
{
	int ret = 0;

	if (cleancache)
		use_cleancache = true;
	if (frontswap)
		use_frontswap = true;
	if (frontswap_exclusive_gets)
		use_frontswap_exclusive_gets = true;
	ramster_debugfs_init();
	ret = sysfs_create_group(mm_kobj, &ramster_attr_group);
	if (ret)
		pr_err("ramster: can't create sysfs for ramster\n");
	(void)r2net_register_handlers();
	INIT_LIST_HEAD(&ramster_rem_op_list);
	ramster_flnode_cache = kmem_cache_create("ramster_flnode",
				sizeof(struct flushlist_node), 0, 0, NULL);
	frontswap_selfshrinking = use_frontswap_selfshrink;
	if (frontswap_selfshrinking) {
		pr_info("ramster: Initializing frontswap selfshrink driver.\n");
		schedule_delayed_work(&selfshrink_worker,
					selfshrink_interval * HZ);
	}
	ramster_remotify_init();
}
