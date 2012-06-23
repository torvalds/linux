/*
 *	An async IO implementation for Linux
 *	Written by Benjamin LaHaise <bcrl@kvack.org>
 *
 *	Implements an efficient asynchronous io interface.
 *
 *	Copyright 2000, 2001, 2002 Red Hat, Inc.  All Rights Reserved.
 *
 *	See ../COPYING for licensing terms.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/aio_abi.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/backing-dev.h>
#include <linux/uio.h>

#define DEBUG 0

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/mmu_context.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/aio.h>
#include <linux/highmem.h>
#include <linux/workqueue.h>
#include <linux/security.h>
#include <linux/eventfd.h>
#include <linux/blkdev.h>
#include <linux/compat.h>

#include <asm/kmap_types.h>
#include <asm/uaccess.h>

#if DEBUG > 1
#define dprintk		printk
#else
#define dprintk(x...)	do { ; } while (0)
#endif

/*------ sysctl variables----*/
static DEFINE_SPINLOCK(aio_nr_lock);
unsigned long aio_nr;		/* current system wide number of aio requests */
unsigned long aio_max_nr = 0x10000; /* system wide maximum number of aio requests */
/*----end sysctl variables---*/

static struct kmem_cache	*kiocb_cachep;
static struct kmem_cache	*kioctx_cachep;

static struct workqueue_struct *aio_wq;

/* Used for rare fput completion. */
static void aio_fput_routine(struct work_struct *);
static DECLARE_WORK(fput_work, aio_fput_routine);

static DEFINE_SPINLOCK(fput_lock);
static LIST_HEAD(fput_head);

static void aio_kick_handler(struct work_struct *);
static void aio_queue_work(struct kioctx *);

/* aio_setup
 *	Creates the slab caches used by the aio routines, panic on
 *	failure as this is done early during the boot sequence.
 */
static int __init aio_setup(void)
{
	kiocb_cachep = KMEM_CACHE(kiocb, SLAB_HWCACHE_ALIGN|SLAB_PANIC);
	kioctx_cachep = KMEM_CACHE(kioctx,SLAB_HWCACHE_ALIGN|SLAB_PANIC);

	aio_wq = alloc_workqueue("aio", 0, 1);	/* used to limit concurrency */
	BUG_ON(!aio_wq);

	pr_debug("aio_setup: sizeof(struct page) = %d\n", (int)sizeof(struct page));

	return 0;
}
__initcall(aio_setup);

static void aio_free_ring(struct kioctx *ctx)
{
	struct aio_ring_info *info = &ctx->ring_info;
	long i;

	for (i=0; i<info->nr_pages; i++)
		put_page(info->ring_pages[i]);

	if (info->mmap_size) {
		down_write(&ctx->mm->mmap_sem);
		do_munmap(ctx->mm, info->mmap_base, info->mmap_size);
		up_write(&ctx->mm->mmap_sem);
	}

	if (info->ring_pages && info->ring_pages != info->internal_pages)
		kfree(info->ring_pages);
	info->ring_pages = NULL;
	info->nr = 0;
}

static int aio_setup_ring(struct kioctx *ctx)
{
	struct aio_ring *ring;
	struct aio_ring_info *info = &ctx->ring_info;
	unsigned nr_events = ctx->max_reqs;
	unsigned long size;
	int nr_pages;

	/* Compensate for the ring buffer's head/tail overlap entry */
	nr_events += 2;	/* 1 is required, 2 for good luck */

	size = sizeof(struct aio_ring);
	size += sizeof(struct io_event) * nr_events;
	nr_pages = (size + PAGE_SIZE-1) >> PAGE_SHIFT;

	if (nr_pages < 0)
		return -EINVAL;

	nr_events = (PAGE_SIZE * nr_pages - sizeof(struct aio_ring)) / sizeof(struct io_event);

	info->nr = 0;
	info->ring_pages = info->internal_pages;
	if (nr_pages > AIO_RING_PAGES) {
		info->ring_pages = kcalloc(nr_pages, sizeof(struct page *), GFP_KERNEL);
		if (!info->ring_pages)
			return -ENOMEM;
	}

	info->mmap_size = nr_pages * PAGE_SIZE;
	dprintk("attempting mmap of %lu bytes\n", info->mmap_size);
	down_write(&ctx->mm->mmap_sem);
	info->mmap_base = do_mmap(NULL, 0, info->mmap_size, 
				  PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE,
				  0);
	if (IS_ERR((void *)info->mmap_base)) {
		up_write(&ctx->mm->mmap_sem);
		info->mmap_size = 0;
		aio_free_ring(ctx);
		return -EAGAIN;
	}

	dprintk("mmap address: 0x%08lx\n", info->mmap_base);
	info->nr_pages = get_user_pages(current, ctx->mm,
					info->mmap_base, nr_pages, 
					1, 0, info->ring_pages, NULL);
	up_write(&ctx->mm->mmap_sem);

	if (unlikely(info->nr_pages != nr_pages)) {
		aio_free_ring(ctx);
		return -EAGAIN;
	}

	ctx->user_id = info->mmap_base;

	info->nr = nr_events;		/* trusted copy */

	ring = kmap_atomic(info->ring_pages[0], KM_USER0);
	ring->nr = nr_events;	/* user copy */
	ring->id = ctx->user_id;
	ring->head = ring->tail = 0;
	ring->magic = AIO_RING_MAGIC;
	ring->compat_features = AIO_RING_COMPAT_FEATURES;
	ring->incompat_features = AIO_RING_INCOMPAT_FEATURES;
	ring->header_length = sizeof(struct aio_ring);
	kunmap_atomic(ring, KM_USER0);

	return 0;
}


/* aio_ring_event: returns a pointer to the event at the given index from
 * kmap_atomic(, km).  Release the pointer with put_aio_ring_event();
 */
#define AIO_EVENTS_PER_PAGE	(PAGE_SIZE / sizeof(struct io_event))
#define AIO_EVENTS_FIRST_PAGE	((PAGE_SIZE - sizeof(struct aio_ring)) / sizeof(struct io_event))
#define AIO_EVENTS_OFFSET	(AIO_EVENTS_PER_PAGE - AIO_EVENTS_FIRST_PAGE)

#define aio_ring_event(info, nr, km) ({					\
	unsigned pos = (nr) + AIO_EVENTS_OFFSET;			\
	struct io_event *__event;					\
	__event = kmap_atomic(						\
			(info)->ring_pages[pos / AIO_EVENTS_PER_PAGE], km); \
	__event += pos % AIO_EVENTS_PER_PAGE;				\
	__event;							\
})

#define put_aio_ring_event(event, km) do {	\
	struct io_event *__event = (event);	\
	(void)__event;				\
	kunmap_atomic((void *)((unsigned long)__event & PAGE_MASK), km); \
} while(0)

static void ctx_rcu_free(struct rcu_head *head)
{
	struct kioctx *ctx = container_of(head, struct kioctx, rcu_head);
	unsigned nr_events = ctx->max_reqs;

	kmem_cache_free(kioctx_cachep, ctx);

	if (nr_events) {
		spin_lock(&aio_nr_lock);
		BUG_ON(aio_nr - nr_events > aio_nr);
		aio_nr -= nr_events;
		spin_unlock(&aio_nr_lock);
	}
}

/* __put_ioctx
 *	Called when the last user of an aio context has gone away,
 *	and the struct needs to be freed.
 */
static void __put_ioctx(struct kioctx *ctx)
{
	BUG_ON(ctx->reqs_active);

	cancel_delayed_work(&ctx->wq);
	cancel_work_sync(&ctx->wq.work);
	aio_free_ring(ctx);
	mmdrop(ctx->mm);
	ctx->mm = NULL;
	pr_debug("__put_ioctx: freeing %p\n", ctx);
	call_rcu(&ctx->rcu_head, ctx_rcu_free);
}

static inline int try_get_ioctx(struct kioctx *kioctx)
{
	return atomic_inc_not_zero(&kioctx->users);
}

static inline void put_ioctx(struct kioctx *kioctx)
{
	BUG_ON(atomic_read(&kioctx->users) <= 0);
	if (unlikely(atomic_dec_and_test(&kioctx->users)))
		__put_ioctx(kioctx);
}

/* ioctx_alloc
 *	Allocates and initializes an ioctx.  Returns an ERR_PTR if it failed.
 */
static struct kioctx *ioctx_alloc(unsigned nr_events)
{
	struct mm_struct *mm;
	struct kioctx *ctx;
	int did_sync = 0;

	/* Prevent overflows */
	if ((nr_events > (0x10000000U / sizeof(struct io_event))) ||
	    (nr_events > (0x10000000U / sizeof(struct kiocb)))) {
		pr_debug("ENOMEM: nr_events too high\n");
		return ERR_PTR(-EINVAL);
	}

	if ((unsigned long)nr_events > aio_max_nr)
		return ERR_PTR(-EAGAIN);

	ctx = kmem_cache_zalloc(kioctx_cachep, GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->max_reqs = nr_events;
	mm = ctx->mm = current->mm;
	atomic_inc(&mm->mm_count);

	atomic_set(&ctx->users, 2);
	spin_lock_init(&ctx->ctx_lock);
	spin_lock_init(&ctx->ring_info.ring_lock);
	init_waitqueue_head(&ctx->wait);

	INIT_LIST_HEAD(&ctx->active_reqs);
	INIT_LIST_HEAD(&ctx->run_list);
	INIT_DELAYED_WORK(&ctx->wq, aio_kick_handler);

	if (aio_setup_ring(ctx) < 0)
		goto out_freectx;

	/* limit the number of system wide aios */
	do {
		spin_lock_bh(&aio_nr_lock);
		if (aio_nr + nr_events > aio_max_nr ||
		    aio_nr + nr_events < aio_nr)
			ctx->max_reqs = 0;
		else
			aio_nr += ctx->max_reqs;
		spin_unlock_bh(&aio_nr_lock);
		if (ctx->max_reqs || did_sync)
			break;

		/* wait for rcu callbacks to have completed before giving up */
		synchronize_rcu();
		did_sync = 1;
		ctx->max_reqs = nr_events;
	} while (1);

	if (ctx->max_reqs == 0)
		goto out_cleanup;

	/* now link into global list. */
	spin_lock(&mm->ioctx_lock);
	hlist_add_head_rcu(&ctx->list, &mm->ioctx_list);
	spin_unlock(&mm->ioctx_lock);

	dprintk("aio: allocated ioctx %p[%ld]: mm=%p mask=0x%x\n",
		ctx, ctx->user_id, current->mm, ctx->ring_info.nr);
	return ctx;

out_cleanup:
	__put_ioctx(ctx);
	return ERR_PTR(-EAGAIN);

out_freectx:
	mmdrop(mm);
	kmem_cache_free(kioctx_cachep, ctx);
	ctx = ERR_PTR(-ENOMEM);

	dprintk("aio: error allocating ioctx %p\n", ctx);
	return ctx;
}

/* aio_cancel_all
 *	Cancels all outstanding aio requests on an aio context.  Used 
 *	when the processes owning a context have all exited to encourage 
 *	the rapid destruction of the kioctx.
 */
static void aio_cancel_all(struct kioctx *ctx)
{
	int (*cancel)(struct kiocb *, struct io_event *);
	struct io_event res;
	spin_lock_irq(&ctx->ctx_lock);
	ctx->dead = 1;
	while (!list_empty(&ctx->active_reqs)) {
		struct list_head *pos = ctx->active_reqs.next;
		struct kiocb *iocb = list_kiocb(pos);
		list_del_init(&iocb->ki_list);
		cancel = iocb->ki_cancel;
		kiocbSetCancelled(iocb);
		if (cancel) {
			iocb->ki_users++;
			spin_unlock_irq(&ctx->ctx_lock);
			cancel(iocb, &res);
			spin_lock_irq(&ctx->ctx_lock);
		}
	}
	spin_unlock_irq(&ctx->ctx_lock);
}

static void wait_for_all_aios(struct kioctx *ctx)
{
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);

	spin_lock_irq(&ctx->ctx_lock);
	if (!ctx->reqs_active)
		goto out;

	add_wait_queue(&ctx->wait, &wait);
	set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	while (ctx->reqs_active) {
		spin_unlock_irq(&ctx->ctx_lock);
		io_schedule();
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		spin_lock_irq(&ctx->ctx_lock);
	}
	__set_task_state(tsk, TASK_RUNNING);
	remove_wait_queue(&ctx->wait, &wait);

out:
	spin_unlock_irq(&ctx->ctx_lock);
}

/* wait_on_sync_kiocb:
 *	Waits on the given sync kiocb to complete.
 */
ssize_t wait_on_sync_kiocb(struct kiocb *iocb)
{
	while (iocb->ki_users) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (!iocb->ki_users)
			break;
		io_schedule();
	}
	__set_current_state(TASK_RUNNING);
	return iocb->ki_user_data;
}
EXPORT_SYMBOL(wait_on_sync_kiocb);

/* exit_aio: called when the last user of mm goes away.  At this point, 
 * there is no way for any new requests to be submited or any of the 
 * io_* syscalls to be called on the context.  However, there may be 
 * outstanding requests which hold references to the context; as they 
 * go away, they will call put_ioctx and release any pinned memory
 * associated with the request (held via struct page * references).
 */
void exit_aio(struct mm_struct *mm)
{
	struct kioctx *ctx;

	while (!hlist_empty(&mm->ioctx_list)) {
		ctx = hlist_entry(mm->ioctx_list.first, struct kioctx, list);
		hlist_del_rcu(&ctx->list);

		aio_cancel_all(ctx);

		wait_for_all_aios(ctx);
		/*
		 * Ensure we don't leave the ctx on the aio_wq
		 */
		cancel_work_sync(&ctx->wq.work);

		if (1 != atomic_read(&ctx->users))
			printk(KERN_DEBUG
				"exit_aio:ioctx still alive: %d %d %d\n",
				atomic_read(&ctx->users), ctx->dead,
				ctx->reqs_active);
		put_ioctx(ctx);
	}
}

/* aio_get_req
 *	Allocate a slot for an aio request.  Increments the users count
 * of the kioctx so that the kioctx stays around until all requests are
 * complete.  Returns NULL if no requests are free.
 *
 * Returns with kiocb->users set to 2.  The io submit code path holds
 * an extra reference while submitting the i/o.
 * This prevents races between the aio code path referencing the
 * req (after submitting it) and aio_complete() freeing the req.
 */
static struct kiocb *__aio_get_req(struct kioctx *ctx)
{
	struct kiocb *req = NULL;
	struct aio_ring *ring;
	int okay = 0;

	req = kmem_cache_alloc(kiocb_cachep, GFP_KERNEL);
	if (unlikely(!req))
		return NULL;

	req->ki_flags = 0;
	req->ki_users = 2;
	req->ki_key = 0;
	req->ki_ctx = ctx;
	req->ki_cancel = NULL;
	req->ki_retry = NULL;
	req->ki_dtor = NULL;
	req->private = NULL;
	req->ki_iovec = NULL;
	INIT_LIST_HEAD(&req->ki_run_list);
	req->ki_eventfd = NULL;

	/* Check if the completion queue has enough free space to
	 * accept an event from this io.
	 */
	spin_lock_irq(&ctx->ctx_lock);
	ring = kmap_atomic(ctx->ring_info.ring_pages[0], KM_USER0);
	if (ctx->reqs_active < aio_ring_avail(&ctx->ring_info, ring)) {
		list_add(&req->ki_list, &ctx->active_reqs);
		ctx->reqs_active++;
		okay = 1;
	}
	kunmap_atomic(ring, KM_USER0);
	spin_unlock_irq(&ctx->ctx_lock);

	if (!okay) {
		kmem_cache_free(kiocb_cachep, req);
		req = NULL;
	}

	return req;
}

static inline struct kiocb *aio_get_req(struct kioctx *ctx)
{
	struct kiocb *req;
	/* Handle a potential starvation case -- should be exceedingly rare as 
	 * requests will be stuck on fput_head only if the aio_fput_routine is 
	 * delayed and the requests were the last user of the struct file.
	 */
	req = __aio_get_req(ctx);
	if (unlikely(NULL == req)) {
		aio_fput_routine(NULL);
		req = __aio_get_req(ctx);
	}
	return req;
}

static inline void really_put_req(struct kioctx *ctx, struct kiocb *req)
{
	assert_spin_locked(&ctx->ctx_lock);

	if (req->ki_eventfd != NULL)
		eventfd_ctx_put(req->ki_eventfd);
	if (req->ki_dtor)
		req->ki_dtor(req);
	if (req->ki_iovec != &req->ki_inline_vec)
		kfree(req->ki_iovec);
	kmem_cache_free(kiocb_cachep, req);
	ctx->reqs_active--;

	if (unlikely(!ctx->reqs_active && ctx->dead))
		wake_up_all(&ctx->wait);
}

static void aio_fput_routine(struct work_struct *data)
{
	spin_lock_irq(&fput_lock);
	while (likely(!list_empty(&fput_head))) {
		struct kiocb *req = list_kiocb(fput_head.next);
		struct kioctx *ctx = req->ki_ctx;

		list_del(&req->ki_list);
		spin_unlock_irq(&fput_lock);

		/* Complete the fput(s) */
		if (req->ki_filp != NULL)
			fput(req->ki_filp);

		/* Link the iocb into the context's free list */
		rcu_read_lock();
		spin_lock_irq(&ctx->ctx_lock);
		really_put_req(ctx, req);
		/*
		 * at that point ctx might've been killed, but actual
		 * freeing is RCU'd
		 */
		spin_unlock_irq(&ctx->ctx_lock);
		rcu_read_unlock();

		spin_lock_irq(&fput_lock);
	}
	spin_unlock_irq(&fput_lock);
}

/* __aio_put_req
 *	Returns true if this put was the last user of the request.
 */
static int __aio_put_req(struct kioctx *ctx, struct kiocb *req)
{
	dprintk(KERN_DEBUG "aio_put(%p): f_count=%ld\n",
		req, atomic_long_read(&req->ki_filp->f_count));

	assert_spin_locked(&ctx->ctx_lock);

	req->ki_users--;
	BUG_ON(req->ki_users < 0);
	if (likely(req->ki_users))
		return 0;
	list_del(&req->ki_list);		/* remove from active_reqs */
	req->ki_cancel = NULL;
	req->ki_retry = NULL;

	/*
	 * Try to optimize the aio and eventfd file* puts, by avoiding to
	 * schedule work in case it is not final fput() time. In normal cases,
	 * we would not be holding the last reference to the file*, so
	 * this function will be executed w/out any aio kthread wakeup.
	 */
	if (unlikely(!fput_atomic(req->ki_filp))) {
		spin_lock(&fput_lock);
		list_add(&req->ki_list, &fput_head);
		spin_unlock(&fput_lock);
		schedule_work(&fput_work);
	} else {
		req->ki_filp = NULL;
		really_put_req(ctx, req);
	}
	return 1;
}

/* aio_put_req
 *	Returns true if this put was the last user of the kiocb,
 *	false if the request is still in use.
 */
int aio_put_req(struct kiocb *req)
{
	struct kioctx *ctx = req->ki_ctx;
	int ret;
	spin_lock_irq(&ctx->ctx_lock);
	ret = __aio_put_req(ctx, req);
	spin_unlock_irq(&ctx->ctx_lock);
	return ret;
}
EXPORT_SYMBOL(aio_put_req);

static struct kioctx *lookup_ioctx(unsigned long ctx_id)
{
	struct mm_struct *mm = current->mm;
	struct kioctx *ctx, *ret = NULL;
	struct hlist_node *n;

	rcu_read_lock();

	hlist_for_each_entry_rcu(ctx, n, &mm->ioctx_list, list) {
		/*
		 * RCU protects us against accessing freed memory but
		 * we have to be careful not to get a reference when the
		 * reference count already dropped to 0 (ctx->dead test
		 * is unreliable because of races).
		 */
		if (ctx->user_id == ctx_id && !ctx->dead && try_get_ioctx(ctx)){
			ret = ctx;
			break;
		}
	}

	rcu_read_unlock();
	return ret;
}

/*
 * Queue up a kiocb to be retried. Assumes that the kiocb
 * has already been marked as kicked, and places it on
 * the retry run list for the corresponding ioctx, if it
 * isn't already queued. Returns 1 if it actually queued
 * the kiocb (to tell the caller to activate the work
 * queue to process it), or 0, if it found that it was
 * already queued.
 */
static inline int __queue_kicked_iocb(struct kiocb *iocb)
{
	struct kioctx *ctx = iocb->ki_ctx;

	assert_spin_locked(&ctx->ctx_lock);

	if (list_empty(&iocb->ki_run_list)) {
		list_add_tail(&iocb->ki_run_list,
			&ctx->run_list);
		return 1;
	}
	return 0;
}

/* aio_run_iocb
 *	This is the core aio execution routine. It is
 *	invoked both for initial i/o submission and
 *	subsequent retries via the aio_kick_handler.
 *	Expects to be invoked with iocb->ki_ctx->lock
 *	already held. The lock is released and reacquired
 *	as needed during processing.
 *
 * Calls the iocb retry method (already setup for the
 * iocb on initial submission) for operation specific
 * handling, but takes care of most of common retry
 * execution details for a given iocb. The retry method
 * needs to be non-blocking as far as possible, to avoid
 * holding up other iocbs waiting to be serviced by the
 * retry kernel thread.
 *
 * The trickier parts in this code have to do with
 * ensuring that only one retry instance is in progress
 * for a given iocb at any time. Providing that guarantee
 * simplifies the coding of individual aio operations as
 * it avoids various potential races.
 */
static ssize_t aio_run_iocb(struct kiocb *iocb)
{
	struct kioctx	*ctx = iocb->ki_ctx;
	ssize_t (*retry)(struct kiocb *);
	ssize_t ret;

	if (!(retry = iocb->ki_retry)) {
		printk("aio_run_iocb: iocb->ki_retry = NULL\n");
		return 0;
	}

	/*
	 * We don't want the next retry iteration for this
	 * operation to start until this one has returned and
	 * updated the iocb state. However, wait_queue functions
	 * can trigger a kick_iocb from interrupt context in the
	 * meantime, indicating that data is available for the next
	 * iteration. We want to remember that and enable the
	 * next retry iteration _after_ we are through with
	 * this one.
	 *
	 * So, in order to be able to register a "kick", but
	 * prevent it from being queued now, we clear the kick
	 * flag, but make the kick code *think* that the iocb is
	 * still on the run list until we are actually done.
	 * When we are done with this iteration, we check if
	 * the iocb was kicked in the meantime and if so, queue
	 * it up afresh.
	 */

	kiocbClearKicked(iocb);

	/*
	 * This is so that aio_complete knows it doesn't need to
	 * pull the iocb off the run list (We can't just call
	 * INIT_LIST_HEAD because we don't want a kick_iocb to
	 * queue this on the run list yet)
	 */
	iocb->ki_run_list.next = iocb->ki_run_list.prev = NULL;
	spin_unlock_irq(&ctx->ctx_lock);

	/* Quit retrying if the i/o has been cancelled */
	if (kiocbIsCancelled(iocb)) {
		ret = -EINTR;
		aio_complete(iocb, ret, 0);
		/* must not access the iocb after this */
		goto out;
	}

	/*
	 * Now we are all set to call the retry method in async
	 * context.
	 */
	ret = retry(iocb);

	if (ret != -EIOCBRETRY && ret != -EIOCBQUEUED) {
		/*
		 * There's no easy way to restart the syscall since other AIO's
		 * may be already running. Just fail this IO with EINTR.
		 */
		if (unlikely(ret == -ERESTARTSYS || ret == -ERESTARTNOINTR ||
			     ret == -ERESTARTNOHAND || ret == -ERESTART_RESTARTBLOCK))
			ret = -EINTR;
		aio_complete(iocb, ret, 0);
	}
out:
	spin_lock_irq(&ctx->ctx_lock);

	if (-EIOCBRETRY == ret) {
		/*
		 * OK, now that we are done with this iteration
		 * and know that there is more left to go,
		 * this is where we let go so that a subsequent
		 * "kick" can start the next iteration
		 */

		/* will make __queue_kicked_iocb succeed from here on */
		INIT_LIST_HEAD(&iocb->ki_run_list);
		/* we must queue the next iteration ourselves, if it
		 * has already been kicked */
		if (kiocbIsKicked(iocb)) {
			__queue_kicked_iocb(iocb);

			/*
			 * __queue_kicked_iocb will always return 1 here, because
			 * iocb->ki_run_list is empty at this point so it should
			 * be safe to unconditionally queue the context into the
			 * work queue.
			 */
			aio_queue_work(ctx);
		}
	}
	return ret;
}

/*
 * __aio_run_iocbs:
 * 	Process all pending retries queued on the ioctx
 * 	run list.
 * Assumes it is operating within the aio issuer's mm
 * context.
 */
static int __aio_run_iocbs(struct kioctx *ctx)
{
	struct kiocb *iocb;
	struct list_head run_list;

	assert_spin_locked(&ctx->ctx_lock);

	list_replace_init(&ctx->run_list, &run_list);
	while (!list_empty(&run_list)) {
		iocb = list_entry(run_list.next, struct kiocb,
			ki_run_list);
		list_del(&iocb->ki_run_list);
		/*
		 * Hold an extra reference while retrying i/o.
		 */
		iocb->ki_users++;       /* grab extra reference */
		aio_run_iocb(iocb);
		__aio_put_req(ctx, iocb);
 	}
	if (!list_empty(&ctx->run_list))
		return 1;
	return 0;
}

static void aio_queue_work(struct kioctx * ctx)
{
	unsigned long timeout;
	/*
	 * if someone is waiting, get the work started right
	 * away, otherwise, use a longer delay
	 */
	smp_mb();
	if (waitqueue_active(&ctx->wait))
		timeout = 1;
	else
		timeout = HZ/10;
	queue_delayed_work(aio_wq, &ctx->wq, timeout);
}

/*
 * aio_run_all_iocbs:
 *	Process all pending retries queued on the ioctx
 *	run list, and keep running them until the list
 *	stays empty.
 * Assumes it is operating within the aio issuer's mm context.
 */
static inline void aio_run_all_iocbs(struct kioctx *ctx)
{
	spin_lock_irq(&ctx->ctx_lock);
	while (__aio_run_iocbs(ctx))
		;
	spin_unlock_irq(&ctx->ctx_lock);
}

/*
 * aio_kick_handler:
 * 	Work queue handler triggered to process pending
 * 	retries on an ioctx. Takes on the aio issuer's
 *	mm context before running the iocbs, so that
 *	copy_xxx_user operates on the issuer's address
 *      space.
 * Run on aiod's context.
 */
static void aio_kick_handler(struct work_struct *work)
{
	struct kioctx *ctx = container_of(work, struct kioctx, wq.work);
	mm_segment_t oldfs = get_fs();
	struct mm_struct *mm;
	int requeue;

	set_fs(USER_DS);
	use_mm(ctx->mm);
	spin_lock_irq(&ctx->ctx_lock);
	requeue =__aio_run_iocbs(ctx);
	mm = ctx->mm;
	spin_unlock_irq(&ctx->ctx_lock);
 	unuse_mm(mm);
	set_fs(oldfs);
	/*
	 * we're in a worker thread already, don't use queue_delayed_work,
	 */
	if (requeue)
		queue_delayed_work(aio_wq, &ctx->wq, 0);
}


/*
 * Called by kick_iocb to queue the kiocb for retry
 * and if required activate the aio work queue to process
 * it
 */
static void try_queue_kicked_iocb(struct kiocb *iocb)
{
 	struct kioctx	*ctx = iocb->ki_ctx;
	unsigned long flags;
	int run = 0;

	spin_lock_irqsave(&ctx->ctx_lock, flags);
	/* set this inside the lock so that we can't race with aio_run_iocb()
	 * testing it and putting the iocb on the run list under the lock */
	if (!kiocbTryKick(iocb))
		run = __queue_kicked_iocb(iocb);
	spin_unlock_irqrestore(&ctx->ctx_lock, flags);
	if (run)
		aio_queue_work(ctx);
}

/*
 * kick_iocb:
 *      Called typically from a wait queue callback context
 *      to trigger a retry of the iocb.
 *      The retry is usually executed by aio workqueue
 *      threads (See aio_kick_handler).
 */
void kick_iocb(struct kiocb *iocb)
{
	/* sync iocbs are easy: they can only ever be executing from a 
	 * single context. */
	if (is_sync_kiocb(iocb)) {
		kiocbSetKicked(iocb);
	        wake_up_process(iocb->ki_obj.tsk);
		return;
	}

	try_queue_kicked_iocb(iocb);
}
EXPORT_SYMBOL(kick_iocb);

/* aio_complete
 *	Called when the io request on the given iocb is complete.
 *	Returns true if this is the last user of the request.  The 
 *	only other user of the request can be the cancellation code.
 */
int aio_complete(struct kiocb *iocb, long res, long res2)
{
	struct kioctx	*ctx = iocb->ki_ctx;
	struct aio_ring_info	*info;
	struct aio_ring	*ring;
	struct io_event	*event;
	unsigned long	flags;
	unsigned long	tail;
	int		ret;

	/*
	 * Special case handling for sync iocbs:
	 *  - events go directly into the iocb for fast handling
	 *  - the sync task with the iocb in its stack holds the single iocb
	 *    ref, no other paths have a way to get another ref
	 *  - the sync task helpfully left a reference to itself in the iocb
	 */
	if (is_sync_kiocb(iocb)) {
		BUG_ON(iocb->ki_users != 1);
		iocb->ki_user_data = res;
		iocb->ki_users = 0;
		wake_up_process(iocb->ki_obj.tsk);
		return 1;
	}

	info = &ctx->ring_info;

	/* add a completion event to the ring buffer.
	 * must be done holding ctx->ctx_lock to prevent
	 * other code from messing with the tail
	 * pointer since we might be called from irq
	 * context.
	 */
	spin_lock_irqsave(&ctx->ctx_lock, flags);

	if (iocb->ki_run_list.prev && !list_empty(&iocb->ki_run_list))
		list_del_init(&iocb->ki_run_list);

	/*
	 * cancelled requests don't get events, userland was given one
	 * when the event got cancelled.
	 */
	if (kiocbIsCancelled(iocb))
		goto put_rq;

	ring = kmap_atomic(info->ring_pages[0], KM_IRQ1);

	tail = info->tail;
	event = aio_ring_event(info, tail, KM_IRQ0);
	if (++tail >= info->nr)
		tail = 0;

	event->obj = (u64)(unsigned long)iocb->ki_obj.user;
	event->data = iocb->ki_user_data;
	event->res = res;
	event->res2 = res2;

	dprintk("aio_complete: %p[%lu]: %p: %p %Lx %lx %lx\n",
		ctx, tail, iocb, iocb->ki_obj.user, iocb->ki_user_data,
		res, res2);

	/* after flagging the request as done, we
	 * must never even look at it again
	 */
	smp_wmb();	/* make event visible before updating tail */

	info->tail = tail;
	ring->tail = tail;

	put_aio_ring_event(event, KM_IRQ0);
	kunmap_atomic(ring, KM_IRQ1);

	pr_debug("added to ring %p at [%lu]\n", iocb, tail);

	/*
	 * Check if the user asked us to deliver the result through an
	 * eventfd. The eventfd_signal() function is safe to be called
	 * from IRQ context.
	 */
	if (iocb->ki_eventfd != NULL)
		eventfd_signal(iocb->ki_eventfd, 1);

put_rq:
	/* everything turned out well, dispose of the aiocb. */
	ret = __aio_put_req(ctx, iocb);

	/*
	 * We have to order our ring_info tail store above and test
	 * of the wait list below outside the wait lock.  This is
	 * like in wake_up_bit() where clearing a bit has to be
	 * ordered with the unlocked test.
	 */
	smp_mb();

	if (waitqueue_active(&ctx->wait))
		wake_up(&ctx->wait);

	spin_unlock_irqrestore(&ctx->ctx_lock, flags);
	return ret;
}
EXPORT_SYMBOL(aio_complete);

/* aio_read_evt
 *	Pull an event off of the ioctx's event ring.  Returns the number of 
 *	events fetched (0 or 1 ;-)
 *	FIXME: make this use cmpxchg.
 *	TODO: make the ringbuffer user mmap()able (requires FIXME).
 */
static int aio_read_evt(struct kioctx *ioctx, struct io_event *ent)
{
	struct aio_ring_info *info = &ioctx->ring_info;
	struct aio_ring *ring;
	unsigned long head;
	int ret = 0;

	ring = kmap_atomic(info->ring_pages[0], KM_USER0);
	dprintk("in aio_read_evt h%lu t%lu m%lu\n",
		 (unsigned long)ring->head, (unsigned long)ring->tail,
		 (unsigned long)ring->nr);

	if (ring->head == ring->tail)
		goto out;

	spin_lock(&info->ring_lock);

	head = ring->head % info->nr;
	if (head != ring->tail) {
		struct io_event *evp = aio_ring_event(info, head, KM_USER1);
		*ent = *evp;
		head = (head + 1) % info->nr;
		smp_mb(); /* finish reading the event before updatng the head */
		ring->head = head;
		ret = 1;
		put_aio_ring_event(evp, KM_USER1);
	}
	spin_unlock(&info->ring_lock);

out:
	kunmap_atomic(ring, KM_USER0);
	dprintk("leaving aio_read_evt: %d  h%lu t%lu\n", ret,
		 (unsigned long)ring->head, (unsigned long)ring->tail);
	return ret;
}

struct aio_timeout {
	struct timer_list	timer;
	int			timed_out;
	struct task_struct	*p;
};

static void timeout_func(unsigned long data)
{
	struct aio_timeout *to = (struct aio_timeout *)data;

	to->timed_out = 1;
	wake_up_process(to->p);
}

static inline void init_timeout(struct aio_timeout *to)
{
	setup_timer_on_stack(&to->timer, timeout_func, (unsigned long) to);
	to->timed_out = 0;
	to->p = current;
}

static inline void set_timeout(long start_jiffies, struct aio_timeout *to,
			       const struct timespec *ts)
{
	to->timer.expires = start_jiffies + timespec_to_jiffies(ts);
	if (time_after(to->timer.expires, jiffies))
		add_timer(&to->timer);
	else
		to->timed_out = 1;
}

static inline void clear_timeout(struct aio_timeout *to)
{
	del_singleshot_timer_sync(&to->timer);
}

static int read_events(struct kioctx *ctx,
			long min_nr, long nr,
			struct io_event __user *event,
			struct timespec __user *timeout)
{
	long			start_jiffies = jiffies;
	struct task_struct	*tsk = current;
	DECLARE_WAITQUEUE(wait, tsk);
	int			ret;
	int			i = 0;
	struct io_event		ent;
	struct aio_timeout	to;
	int			retry = 0;

	/* needed to zero any padding within an entry (there shouldn't be 
	 * any, but C is fun!
	 */
	memset(&ent, 0, sizeof(ent));
retry:
	ret = 0;
	while (likely(i < nr)) {
		ret = aio_read_evt(ctx, &ent);
		if (unlikely(ret <= 0))
			break;

		dprintk("read event: %Lx %Lx %Lx %Lx\n",
			ent.data, ent.obj, ent.res, ent.res2);

		/* Could we split the check in two? */
		ret = -EFAULT;
		if (unlikely(copy_to_user(event, &ent, sizeof(ent)))) {
			dprintk("aio: lost an event due to EFAULT.\n");
			break;
		}
		ret = 0;

		/* Good, event copied to userland, update counts. */
		event ++;
		i ++;
	}

	if (min_nr <= i)
		return i;
	if (ret)
		return ret;

	/* End fast path */

	/* racey check, but it gets redone */
	if (!retry && unlikely(!list_empty(&ctx->run_list))) {
		retry = 1;
		aio_run_all_iocbs(ctx);
		goto retry;
	}

	init_timeout(&to);
	if (timeout) {
		struct timespec	ts;
		ret = -EFAULT;
		if (unlikely(copy_from_user(&ts, timeout, sizeof(ts))))
			goto out;

		set_timeout(start_jiffies, &to, &ts);
	}

	while (likely(i < nr)) {
		add_wait_queue_exclusive(&ctx->wait, &wait);
		do {
			set_task_state(tsk, TASK_INTERRUPTIBLE);
			ret = aio_read_evt(ctx, &ent);
			if (ret)
				break;
			if (min_nr <= i)
				break;
			if (unlikely(ctx->dead)) {
				ret = -EINVAL;
				break;
			}
			if (to.timed_out)	/* Only check after read evt */
				break;
			/* Try to only show up in io wait if there are ops
			 *  in flight */
			if (ctx->reqs_active)
				io_schedule();
			else
				schedule();
			if (signal_pending(tsk)) {
				ret = -EINTR;
				break;
			}
			/*ret = aio_read_evt(ctx, &ent);*/
		} while (1) ;

		set_task_state(tsk, TASK_RUNNING);
		remove_wait_queue(&ctx->wait, &wait);

		if (unlikely(ret <= 0))
			break;

		ret = -EFAULT;
		if (unlikely(copy_to_user(event, &ent, sizeof(ent)))) {
			dprintk("aio: lost an event due to EFAULT.\n");
			break;
		}

		/* Good, event copied to userland, update counts. */
		event ++;
		i ++;
	}

	if (timeout)
		clear_timeout(&to);
out:
	destroy_timer_on_stack(&to.timer);
	return i ? i : ret;
}

/* Take an ioctx and remove it from the list of ioctx's.  Protects 
 * against races with itself via ->dead.
 */
static void io_destroy(struct kioctx *ioctx)
{
	struct mm_struct *mm = current->mm;
	int was_dead;

	/* delete the entry from the list is someone else hasn't already */
	spin_lock(&mm->ioctx_lock);
	was_dead = ioctx->dead;
	ioctx->dead = 1;
	hlist_del_rcu(&ioctx->list);
	spin_unlock(&mm->ioctx_lock);

	dprintk("aio_release(%p)\n", ioctx);
	if (likely(!was_dead))
		put_ioctx(ioctx);	/* twice for the list */

	aio_cancel_all(ioctx);
	wait_for_all_aios(ioctx);

	/*
	 * Wake up any waiters.  The setting of ctx->dead must be seen
	 * by other CPUs at this point.  Right now, we rely on the
	 * locking done by the above calls to ensure this consistency.
	 */
	wake_up_all(&ioctx->wait);
	put_ioctx(ioctx);	/* once for the lookup */
}

/* sys_io_setup:
 *	Create an aio_context capable of receiving at least nr_events.
 *	ctxp must not point to an aio_context that already exists, and
 *	must be initialized to 0 prior to the call.  On successful
 *	creation of the aio_context, *ctxp is filled in with the resulting 
 *	handle.  May fail with -EINVAL if *ctxp is not initialized,
 *	if the specified nr_events exceeds internal limits.  May fail 
 *	with -EAGAIN if the specified nr_events exceeds the user's limit 
 *	of available events.  May fail with -ENOMEM if insufficient kernel
 *	resources are available.  May fail with -EFAULT if an invalid
 *	pointer is passed for ctxp.  Will fail with -ENOSYS if not
 *	implemented.
 */
SYSCALL_DEFINE2(io_setup, unsigned, nr_events, aio_context_t __user *, ctxp)
{
	struct kioctx *ioctx = NULL;
	unsigned long ctx;
	long ret;

	ret = get_user(ctx, ctxp);
	if (unlikely(ret))
		goto out;

	ret = -EINVAL;
	if (unlikely(ctx || nr_events == 0)) {
		pr_debug("EINVAL: io_setup: ctx %lu nr_events %u\n",
		         ctx, nr_events);
		goto out;
	}

	ioctx = ioctx_alloc(nr_events);
	ret = PTR_ERR(ioctx);
	if (!IS_ERR(ioctx)) {
		ret = put_user(ioctx->user_id, ctxp);
		if (!ret) {
			put_ioctx(ioctx);
			return 0;
		}
		io_destroy(ioctx);
	}

out:
	return ret;
}

/* sys_io_destroy:
 *	Destroy the aio_context specified.  May cancel any outstanding 
 *	AIOs and block on completion.  Will fail with -ENOSYS if not
 *	implemented.  May fail with -EINVAL if the context pointed to
 *	is invalid.
 */
SYSCALL_DEFINE1(io_destroy, aio_context_t, ctx)
{
	struct kioctx *ioctx = lookup_ioctx(ctx);
	if (likely(NULL != ioctx)) {
		io_destroy(ioctx);
		return 0;
	}
	pr_debug("EINVAL: io_destroy: invalid context id\n");
	return -EINVAL;
}

static void aio_advance_iovec(struct kiocb *iocb, ssize_t ret)
{
	struct iovec *iov = &iocb->ki_iovec[iocb->ki_cur_seg];

	BUG_ON(ret <= 0);

	while (iocb->ki_cur_seg < iocb->ki_nr_segs && ret > 0) {
		ssize_t this = min((ssize_t)iov->iov_len, ret);
		iov->iov_base += this;
		iov->iov_len -= this;
		iocb->ki_left -= this;
		ret -= this;
		if (iov->iov_len == 0) {
			iocb->ki_cur_seg++;
			iov++;
		}
	}

	/* the caller should not have done more io than what fit in
	 * the remaining iovecs */
	BUG_ON(ret > 0 && iocb->ki_left == 0);
}

static ssize_t aio_rw_vect_retry(struct kiocb *iocb)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	ssize_t (*rw_op)(struct kiocb *, const struct iovec *,
			 unsigned long, loff_t);
	ssize_t ret = 0;
	unsigned short opcode;

	if ((iocb->ki_opcode == IOCB_CMD_PREADV) ||
		(iocb->ki_opcode == IOCB_CMD_PREAD)) {
		rw_op = file->f_op->aio_read;
		opcode = IOCB_CMD_PREADV;
	} else {
		rw_op = file->f_op->aio_write;
		opcode = IOCB_CMD_PWRITEV;
	}

	/* This matches the pread()/pwrite() logic */
	if (iocb->ki_pos < 0)
		return -EINVAL;

	do {
		ret = rw_op(iocb, &iocb->ki_iovec[iocb->ki_cur_seg],
			    iocb->ki_nr_segs - iocb->ki_cur_seg,
			    iocb->ki_pos);
		if (ret > 0)
			aio_advance_iovec(iocb, ret);

	/* retry all partial writes.  retry partial reads as long as its a
	 * regular file. */
	} while (ret > 0 && iocb->ki_left > 0 &&
		 (opcode == IOCB_CMD_PWRITEV ||
		  (!S_ISFIFO(inode->i_mode) && !S_ISSOCK(inode->i_mode))));

	/* This means we must have transferred all that we could */
	/* No need to retry anymore */
	if ((ret == 0) || (iocb->ki_left == 0))
		ret = iocb->ki_nbytes - iocb->ki_left;

	/* If we managed to write some out we return that, rather than
	 * the eventual error. */
	if (opcode == IOCB_CMD_PWRITEV
	    && ret < 0 && ret != -EIOCBQUEUED && ret != -EIOCBRETRY
	    && iocb->ki_nbytes - iocb->ki_left)
		ret = iocb->ki_nbytes - iocb->ki_left;

	return ret;
}

static ssize_t aio_fdsync(struct kiocb *iocb)
{
	struct file *file = iocb->ki_filp;
	ssize_t ret = -EINVAL;

	if (file->f_op->aio_fsync)
		ret = file->f_op->aio_fsync(iocb, 1);
	return ret;
}

static ssize_t aio_fsync(struct kiocb *iocb)
{
	struct file *file = iocb->ki_filp;
	ssize_t ret = -EINVAL;

	if (file->f_op->aio_fsync)
		ret = file->f_op->aio_fsync(iocb, 0);
	return ret;
}

static ssize_t aio_setup_vectored_rw(int type, struct kiocb *kiocb, bool compat)
{
	ssize_t ret;

#ifdef CONFIG_COMPAT
	if (compat)
		ret = compat_rw_copy_check_uvector(type,
				(struct compat_iovec __user *)kiocb->ki_buf,
				kiocb->ki_nbytes, 1, &kiocb->ki_inline_vec,
				&kiocb->ki_iovec);
	else
#endif
		ret = rw_copy_check_uvector(type,
				(struct iovec __user *)kiocb->ki_buf,
				kiocb->ki_nbytes, 1, &kiocb->ki_inline_vec,
				&kiocb->ki_iovec);
	if (ret < 0)
		goto out;

	ret = rw_verify_area(type, kiocb->ki_filp, &kiocb->ki_pos, ret);
	if (ret < 0)
		goto out;

	kiocb->ki_nr_segs = kiocb->ki_nbytes;
	kiocb->ki_cur_seg = 0;
	/* ki_nbytes/left now reflect bytes instead of segs */
	kiocb->ki_nbytes = ret;
	kiocb->ki_left = ret;

	ret = 0;
out:
	return ret;
}

static ssize_t aio_setup_single_vector(int type, struct file * file, struct kiocb *kiocb)
{
	int bytes;

	bytes = rw_verify_area(type, file, &kiocb->ki_pos, kiocb->ki_left);
	if (bytes < 0)
		return bytes;

	kiocb->ki_iovec = &kiocb->ki_inline_vec;
	kiocb->ki_iovec->iov_base = kiocb->ki_buf;
	kiocb->ki_iovec->iov_len = bytes;
	kiocb->ki_nr_segs = 1;
	kiocb->ki_cur_seg = 0;
	return 0;
}

/*
 * aio_setup_iocb:
 *	Performs the initial checks and aio retry method
 *	setup for the kiocb at the time of io submission.
 */
static ssize_t aio_setup_iocb(struct kiocb *kiocb, bool compat)
{
	struct file *file = kiocb->ki_filp;
	ssize_t ret = 0;

	switch (kiocb->ki_opcode) {
	case IOCB_CMD_PREAD:
		ret = -EBADF;
		if (unlikely(!(file->f_mode & FMODE_READ)))
			break;
		ret = -EFAULT;
		if (unlikely(!access_ok(VERIFY_WRITE, kiocb->ki_buf,
			kiocb->ki_left)))
			break;
		ret = aio_setup_single_vector(READ, file, kiocb);
		if (ret)
			break;
		ret = -EINVAL;
		if (file->f_op->aio_read)
			kiocb->ki_retry = aio_rw_vect_retry;
		break;
	case IOCB_CMD_PWRITE:
		ret = -EBADF;
		if (unlikely(!(file->f_mode & FMODE_WRITE)))
			break;
		ret = -EFAULT;
		if (unlikely(!access_ok(VERIFY_READ, kiocb->ki_buf,
			kiocb->ki_left)))
			break;
		ret = aio_setup_single_vector(WRITE, file, kiocb);
		if (ret)
			break;
		ret = -EINVAL;
		if (file->f_op->aio_write)
			kiocb->ki_retry = aio_rw_vect_retry;
		break;
	case IOCB_CMD_PREADV:
		ret = -EBADF;
		if (unlikely(!(file->f_mode & FMODE_READ)))
			break;
		ret = aio_setup_vectored_rw(READ, kiocb, compat);
		if (ret)
			break;
		ret = -EINVAL;
		if (file->f_op->aio_read)
			kiocb->ki_retry = aio_rw_vect_retry;
		break;
	case IOCB_CMD_PWRITEV:
		ret = -EBADF;
		if (unlikely(!(file->f_mode & FMODE_WRITE)))
			break;
		ret = aio_setup_vectored_rw(WRITE, kiocb, compat);
		if (ret)
			break;
		ret = -EINVAL;
		if (file->f_op->aio_write)
			kiocb->ki_retry = aio_rw_vect_retry;
		break;
	case IOCB_CMD_FDSYNC:
		ret = -EINVAL;
		if (file->f_op->aio_fsync)
			kiocb->ki_retry = aio_fdsync;
		break;
	case IOCB_CMD_FSYNC:
		ret = -EINVAL;
		if (file->f_op->aio_fsync)
			kiocb->ki_retry = aio_fsync;
		break;
	default:
		dprintk("EINVAL: io_submit: no operation provided\n");
		ret = -EINVAL;
	}

	if (!kiocb->ki_retry)
		return ret;

	return 0;
}

static int io_submit_one(struct kioctx *ctx, struct iocb __user *user_iocb,
			 struct iocb *iocb, bool compat)
{
	struct kiocb *req;
	struct file *file;
	ssize_t ret;

	/* enforce forwards compatibility on users */
	if (unlikely(iocb->aio_reserved1 || iocb->aio_reserved2)) {
		pr_debug("EINVAL: io_submit: reserve field set\n");
		return -EINVAL;
	}

	/* prevent overflows */
	if (unlikely(
	    (iocb->aio_buf != (unsigned long)iocb->aio_buf) ||
	    (iocb->aio_nbytes != (size_t)iocb->aio_nbytes) ||
	    ((ssize_t)iocb->aio_nbytes < 0)
	   )) {
		pr_debug("EINVAL: io_submit: overflow check\n");
		return -EINVAL;
	}

	file = fget(iocb->aio_fildes);
	if (unlikely(!file))
		return -EBADF;

	req = aio_get_req(ctx);		/* returns with 2 references to req */
	if (unlikely(!req)) {
		fput(file);
		return -EAGAIN;
	}
	req->ki_filp = file;
	if (iocb->aio_flags & IOCB_FLAG_RESFD) {
		/*
		 * If the IOCB_FLAG_RESFD flag of aio_flags is set, get an
		 * instance of the file* now. The file descriptor must be
		 * an eventfd() fd, and will be signaled for each completed
		 * event using the eventfd_signal() function.
		 */
		req->ki_eventfd = eventfd_ctx_fdget((int) iocb->aio_resfd);
		if (IS_ERR(req->ki_eventfd)) {
			ret = PTR_ERR(req->ki_eventfd);
			req->ki_eventfd = NULL;
			goto out_put_req;
		}
	}

	ret = put_user(req->ki_key, &user_iocb->aio_key);
	if (unlikely(ret)) {
		dprintk("EFAULT: aio_key\n");
		goto out_put_req;
	}

	req->ki_obj.user = user_iocb;
	req->ki_user_data = iocb->aio_data;
	req->ki_pos = iocb->aio_offset;

	req->ki_buf = (char __user *)(unsigned long)iocb->aio_buf;
	req->ki_left = req->ki_nbytes = iocb->aio_nbytes;
	req->ki_opcode = iocb->aio_lio_opcode;

	ret = aio_setup_iocb(req, compat);

	if (ret)
		goto out_put_req;

	spin_lock_irq(&ctx->ctx_lock);
	/*
	 * We could have raced with io_destroy() and are currently holding a
	 * reference to ctx which should be destroyed. We cannot submit IO
	 * since ctx gets freed as soon as io_submit() puts its reference.  The
	 * check here is reliable: io_destroy() sets ctx->dead before waiting
	 * for outstanding IO and the barrier between these two is realized by
	 * unlock of mm->ioctx_lock and lock of ctx->ctx_lock.  Analogously we
	 * increment ctx->reqs_active before checking for ctx->dead and the
	 * barrier is realized by unlock and lock of ctx->ctx_lock. Thus if we
	 * don't see ctx->dead set here, io_destroy() waits for our IO to
	 * finish.
	 */
	if (ctx->dead) {
		spin_unlock_irq(&ctx->ctx_lock);
		ret = -EINVAL;
		goto out_put_req;
	}
	aio_run_iocb(req);
	if (!list_empty(&ctx->run_list)) {
		/* drain the run list */
		while (__aio_run_iocbs(ctx))
			;
	}
	spin_unlock_irq(&ctx->ctx_lock);

	aio_put_req(req);	/* drop extra ref to req */
	return 0;

out_put_req:
	aio_put_req(req);	/* drop extra ref to req */
	aio_put_req(req);	/* drop i/o ref to req */
	return ret;
}

long do_io_submit(aio_context_t ctx_id, long nr,
		  struct iocb __user *__user *iocbpp, bool compat)
{
	struct kioctx *ctx;
	long ret = 0;
	int i;
	struct blk_plug plug;

	if (unlikely(nr < 0))
		return -EINVAL;

	if (unlikely(nr > LONG_MAX/sizeof(*iocbpp)))
		nr = LONG_MAX/sizeof(*iocbpp);

	if (unlikely(!access_ok(VERIFY_READ, iocbpp, (nr*sizeof(*iocbpp)))))
		return -EFAULT;

	ctx = lookup_ioctx(ctx_id);
	if (unlikely(!ctx)) {
		pr_debug("EINVAL: io_submit: invalid context id\n");
		return -EINVAL;
	}

	blk_start_plug(&plug);

	/*
	 * AKPM: should this return a partial result if some of the IOs were
	 * successfully submitted?
	 */
	for (i=0; i<nr; i++) {
		struct iocb __user *user_iocb;
		struct iocb tmp;

		if (unlikely(__get_user(user_iocb, iocbpp + i))) {
			ret = -EFAULT;
			break;
		}

		if (unlikely(copy_from_user(&tmp, user_iocb, sizeof(tmp)))) {
			ret = -EFAULT;
			break;
		}

		ret = io_submit_one(ctx, user_iocb, &tmp, compat);
		if (ret)
			break;
	}
	blk_finish_plug(&plug);

	put_ioctx(ctx);
	return i ? i : ret;
}

/* sys_io_submit:
 *	Queue the nr iocbs pointed to by iocbpp for processing.  Returns
 *	the number of iocbs queued.  May return -EINVAL if the aio_context
 *	specified by ctx_id is invalid, if nr is < 0, if the iocb at
 *	*iocbpp[0] is not properly initialized, if the operation specified
 *	is invalid for the file descriptor in the iocb.  May fail with
 *	-EFAULT if any of the data structures point to invalid data.  May
 *	fail with -EBADF if the file descriptor specified in the first
 *	iocb is invalid.  May fail with -EAGAIN if insufficient resources
 *	are available to queue any iocbs.  Will return 0 if nr is 0.  Will
 *	fail with -ENOSYS if not implemented.
 */
SYSCALL_DEFINE3(io_submit, aio_context_t, ctx_id, long, nr,
		struct iocb __user * __user *, iocbpp)
{
	return do_io_submit(ctx_id, nr, iocbpp, 0);
}

/* lookup_kiocb
 *	Finds a given iocb for cancellation.
 */
static struct kiocb *lookup_kiocb(struct kioctx *ctx, struct iocb __user *iocb,
				  u32 key)
{
	struct list_head *pos;

	assert_spin_locked(&ctx->ctx_lock);

	/* TODO: use a hash or array, this sucks. */
	list_for_each(pos, &ctx->active_reqs) {
		struct kiocb *kiocb = list_kiocb(pos);
		if (kiocb->ki_obj.user == iocb && kiocb->ki_key == key)
			return kiocb;
	}
	return NULL;
}

/* sys_io_cancel:
 *	Attempts to cancel an iocb previously passed to io_submit.  If
 *	the operation is successfully cancelled, the resulting event is
 *	copied into the memory pointed to by result without being placed
 *	into the completion queue and 0 is returned.  May fail with
 *	-EFAULT if any of the data structures pointed to are invalid.
 *	May fail with -EINVAL if aio_context specified by ctx_id is
 *	invalid.  May fail with -EAGAIN if the iocb specified was not
 *	cancelled.  Will fail with -ENOSYS if not implemented.
 */
SYSCALL_DEFINE3(io_cancel, aio_context_t, ctx_id, struct iocb __user *, iocb,
		struct io_event __user *, result)
{
	int (*cancel)(struct kiocb *iocb, struct io_event *res);
	struct kioctx *ctx;
	struct kiocb *kiocb;
	u32 key;
	int ret;

	ret = get_user(key, &iocb->aio_key);
	if (unlikely(ret))
		return -EFAULT;

	ctx = lookup_ioctx(ctx_id);
	if (unlikely(!ctx))
		return -EINVAL;

	spin_lock_irq(&ctx->ctx_lock);
	ret = -EAGAIN;
	kiocb = lookup_kiocb(ctx, iocb, key);
	if (kiocb && kiocb->ki_cancel) {
		cancel = kiocb->ki_cancel;
		kiocb->ki_users ++;
		kiocbSetCancelled(kiocb);
	} else
		cancel = NULL;
	spin_unlock_irq(&ctx->ctx_lock);

	if (NULL != cancel) {
		struct io_event tmp;
		pr_debug("calling cancel\n");
		memset(&tmp, 0, sizeof(tmp));
		tmp.obj = (u64)(unsigned long)kiocb->ki_obj.user;
		tmp.data = kiocb->ki_user_data;
		ret = cancel(kiocb, &tmp);
		if (!ret) {
			/* Cancellation succeeded -- copy the result
			 * into the user's buffer.
			 */
			if (copy_to_user(result, &tmp, sizeof(tmp)))
				ret = -EFAULT;
		}
	} else
		ret = -EINVAL;

	put_ioctx(ctx);

	return ret;
}

/* io_getevents:
 *	Attempts to read at least min_nr events and up to nr events from
 *	the completion queue for the aio_context specified by ctx_id. If
 *	it succeeds, the number of read events is returned. May fail with
 *	-EINVAL if ctx_id is invalid, if min_nr is out of range, if nr is
 *	out of range, if timeout is out of range.  May fail with -EFAULT
 *	if any of the memory specified is invalid.  May return 0 or
 *	< min_nr if the timeout specified by timeout has elapsed
 *	before sufficient events are available, where timeout == NULL
 *	specifies an infinite timeout. Note that the timeout pointed to by
 *	timeout is relative and will be updated if not NULL and the
 *	operation blocks. Will fail with -ENOSYS if not implemented.
 */
SYSCALL_DEFINE5(io_getevents, aio_context_t, ctx_id,
		long, min_nr,
		long, nr,
		struct io_event __user *, events,
		struct timespec __user *, timeout)
{
	struct kioctx *ioctx = lookup_ioctx(ctx_id);
	long ret = -EINVAL;

	if (likely(ioctx)) {
		if (likely(min_nr <= nr && min_nr >= 0))
			ret = read_events(ioctx, min_nr, nr, events, timeout);
		put_ioctx(ioctx);
	}

	asmlinkage_protect(5, ret, ctx_id, min_nr, nr, events, timeout);
	return ret;
}
