/*
 * Copyright (C) 2002 Sistina Software (UK) Limited.
 * Copyright (C) 2006 Red Hat GmbH
 *
 * This file is released under the GPL.
 *
 * Kcopyd provides a simple interface for copying an area of one
 * block-device to one or more other block-devices, with an asynchronous
 * completion notification.
 */

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/device-mapper.h>
#include <linux/dm-kcopyd.h>

#include "dm-core.h"

#define SUB_JOB_SIZE	128
#define SPLIT_COUNT	8
#define MIN_JOBS	8
#define RESERVE_PAGES	(DIV_ROUND_UP(SUB_JOB_SIZE << SECTOR_SHIFT, PAGE_SIZE))

/*-----------------------------------------------------------------
 * Each kcopyd client has its own little pool of preallocated
 * pages for kcopyd io.
 *---------------------------------------------------------------*/
struct dm_kcopyd_client {
	struct page_list *pages;
	unsigned nr_reserved_pages;
	unsigned nr_free_pages;

	struct dm_io_client *io_client;

	wait_queue_head_t destroyq;
	atomic_t nr_jobs;

	mempool_t *job_pool;

	struct workqueue_struct *kcopyd_wq;
	struct work_struct kcopyd_work;

	struct dm_kcopyd_throttle *throttle;

/*
 * We maintain three lists of jobs:
 *
 * i)   jobs waiting for pages
 * ii)  jobs that have pages, and are waiting for the io to be issued.
 * iii) jobs that have completed.
 *
 * All three of these are protected by job_lock.
 */
	spinlock_t job_lock;
	struct list_head complete_jobs;
	struct list_head io_jobs;
	struct list_head pages_jobs;
};

static struct page_list zero_page_list;

static DEFINE_SPINLOCK(throttle_spinlock);

/*
 * IO/IDLE accounting slowly decays after (1 << ACCOUNT_INTERVAL_SHIFT) period.
 * When total_period >= (1 << ACCOUNT_INTERVAL_SHIFT) the counters are divided
 * by 2.
 */
#define ACCOUNT_INTERVAL_SHIFT		SHIFT_HZ

/*
 * Sleep this number of milliseconds.
 *
 * The value was decided experimentally.
 * Smaller values seem to cause an increased copy rate above the limit.
 * The reason for this is unknown but possibly due to jiffies rounding errors
 * or read/write cache inside the disk.
 */
#define SLEEP_MSEC			100

/*
 * Maximum number of sleep events. There is a theoretical livelock if more
 * kcopyd clients do work simultaneously which this limit avoids.
 */
#define MAX_SLEEPS			10

static void io_job_start(struct dm_kcopyd_throttle *t)
{
	unsigned throttle, now, difference;
	int slept = 0, skew;

	if (unlikely(!t))
		return;

try_again:
	spin_lock_irq(&throttle_spinlock);

	throttle = ACCESS_ONCE(t->throttle);

	if (likely(throttle >= 100))
		goto skip_limit;

	now = jiffies;
	difference = now - t->last_jiffies;
	t->last_jiffies = now;
	if (t->num_io_jobs)
		t->io_period += difference;
	t->total_period += difference;

	/*
	 * Maintain sane values if we got a temporary overflow.
	 */
	if (unlikely(t->io_period > t->total_period))
		t->io_period = t->total_period;

	if (unlikely(t->total_period >= (1 << ACCOUNT_INTERVAL_SHIFT))) {
		int shift = fls(t->total_period >> ACCOUNT_INTERVAL_SHIFT);
		t->total_period >>= shift;
		t->io_period >>= shift;
	}

	skew = t->io_period - throttle * t->total_period / 100;

	if (unlikely(skew > 0) && slept < MAX_SLEEPS) {
		slept++;
		spin_unlock_irq(&throttle_spinlock);
		msleep(SLEEP_MSEC);
		goto try_again;
	}

skip_limit:
	t->num_io_jobs++;

	spin_unlock_irq(&throttle_spinlock);
}

static void io_job_finish(struct dm_kcopyd_throttle *t)
{
	unsigned long flags;

	if (unlikely(!t))
		return;

	spin_lock_irqsave(&throttle_spinlock, flags);

	t->num_io_jobs--;

	if (likely(ACCESS_ONCE(t->throttle) >= 100))
		goto skip_limit;

	if (!t->num_io_jobs) {
		unsigned now, difference;

		now = jiffies;
		difference = now - t->last_jiffies;
		t->last_jiffies = now;

		t->io_period += difference;
		t->total_period += difference;

		/*
		 * Maintain sane values if we got a temporary overflow.
		 */
		if (unlikely(t->io_period > t->total_period))
			t->io_period = t->total_period;
	}

skip_limit:
	spin_unlock_irqrestore(&throttle_spinlock, flags);
}


static void wake(struct dm_kcopyd_client *kc)
{
	queue_work(kc->kcopyd_wq, &kc->kcopyd_work);
}

/*
 * Obtain one page for the use of kcopyd.
 */
static struct page_list *alloc_pl(gfp_t gfp)
{
	struct page_list *pl;

	pl = kmalloc(sizeof(*pl), gfp);
	if (!pl)
		return NULL;

	pl->page = alloc_page(gfp);
	if (!pl->page) {
		kfree(pl);
		return NULL;
	}

	return pl;
}

static void free_pl(struct page_list *pl)
{
	__free_page(pl->page);
	kfree(pl);
}

/*
 * Add the provided pages to a client's free page list, releasing
 * back to the system any beyond the reserved_pages limit.
 */
static void kcopyd_put_pages(struct dm_kcopyd_client *kc, struct page_list *pl)
{
	struct page_list *next;

	do {
		next = pl->next;

		if (kc->nr_free_pages >= kc->nr_reserved_pages)
			free_pl(pl);
		else {
			pl->next = kc->pages;
			kc->pages = pl;
			kc->nr_free_pages++;
		}

		pl = next;
	} while (pl);
}

static int kcopyd_get_pages(struct dm_kcopyd_client *kc,
			    unsigned int nr, struct page_list **pages)
{
	struct page_list *pl;

	*pages = NULL;

	do {
		pl = alloc_pl(__GFP_NOWARN | __GFP_NORETRY | __GFP_KSWAPD_RECLAIM);
		if (unlikely(!pl)) {
			/* Use reserved pages */
			pl = kc->pages;
			if (unlikely(!pl))
				goto out_of_memory;
			kc->pages = pl->next;
			kc->nr_free_pages--;
		}
		pl->next = *pages;
		*pages = pl;
	} while (--nr);

	return 0;

out_of_memory:
	if (*pages)
		kcopyd_put_pages(kc, *pages);
	return -ENOMEM;
}

/*
 * These three functions resize the page pool.
 */
static void drop_pages(struct page_list *pl)
{
	struct page_list *next;

	while (pl) {
		next = pl->next;
		free_pl(pl);
		pl = next;
	}
}

/*
 * Allocate and reserve nr_pages for the use of a specific client.
 */
static int client_reserve_pages(struct dm_kcopyd_client *kc, unsigned nr_pages)
{
	unsigned i;
	struct page_list *pl = NULL, *next;

	for (i = 0; i < nr_pages; i++) {
		next = alloc_pl(GFP_KERNEL);
		if (!next) {
			if (pl)
				drop_pages(pl);
			return -ENOMEM;
		}
		next->next = pl;
		pl = next;
	}

	kc->nr_reserved_pages += nr_pages;
	kcopyd_put_pages(kc, pl);

	return 0;
}

static void client_free_pages(struct dm_kcopyd_client *kc)
{
	BUG_ON(kc->nr_free_pages != kc->nr_reserved_pages);
	drop_pages(kc->pages);
	kc->pages = NULL;
	kc->nr_free_pages = kc->nr_reserved_pages = 0;
}

/*-----------------------------------------------------------------
 * kcopyd_jobs need to be allocated by the *clients* of kcopyd,
 * for this reason we use a mempool to prevent the client from
 * ever having to do io (which could cause a deadlock).
 *---------------------------------------------------------------*/
struct kcopyd_job {
	struct dm_kcopyd_client *kc;
	struct list_head list;
	unsigned long flags;

	/*
	 * Error state of the job.
	 */
	int read_err;
	unsigned long write_err;

	/*
	 * Either READ or WRITE
	 */
	int rw;
	struct dm_io_region source;

	/*
	 * The destinations for the transfer.
	 */
	unsigned int num_dests;
	struct dm_io_region dests[DM_KCOPYD_MAX_REGIONS];

	struct page_list *pages;

	/*
	 * Set this to ensure you are notified when the job has
	 * completed.  'context' is for callback to use.
	 */
	dm_kcopyd_notify_fn fn;
	void *context;

	/*
	 * These fields are only used if the job has been split
	 * into more manageable parts.
	 */
	struct mutex lock;
	atomic_t sub_jobs;
	sector_t progress;
	sector_t write_offset;

	struct kcopyd_job *master_job;
};

static struct kmem_cache *_job_cache;

int __init dm_kcopyd_init(void)
{
	_job_cache = kmem_cache_create("kcopyd_job",
				sizeof(struct kcopyd_job) * (SPLIT_COUNT + 1),
				__alignof__(struct kcopyd_job), 0, NULL);
	if (!_job_cache)
		return -ENOMEM;

	zero_page_list.next = &zero_page_list;
	zero_page_list.page = ZERO_PAGE(0);

	return 0;
}

void dm_kcopyd_exit(void)
{
	kmem_cache_destroy(_job_cache);
	_job_cache = NULL;
}

/*
 * Functions to push and pop a job onto the head of a given job
 * list.
 */
static struct kcopyd_job *pop_io_job(struct list_head *jobs,
				     struct dm_kcopyd_client *kc)
{
	struct kcopyd_job *job;

	/*
	 * For I/O jobs, pop any read, any write without sequential write
	 * constraint and sequential writes that are at the right position.
	 */
	list_for_each_entry(job, jobs, list) {
		if (job->rw == READ || !test_bit(DM_KCOPYD_WRITE_SEQ, &job->flags)) {
			list_del(&job->list);
			return job;
		}

		if (job->write_offset == job->master_job->write_offset) {
			job->master_job->write_offset += job->source.count;
			list_del(&job->list);
			return job;
		}
	}

	return NULL;
}

static struct kcopyd_job *pop(struct list_head *jobs,
			      struct dm_kcopyd_client *kc)
{
	struct kcopyd_job *job = NULL;
	unsigned long flags;

	spin_lock_irqsave(&kc->job_lock, flags);

	if (!list_empty(jobs)) {
		if (jobs == &kc->io_jobs)
			job = pop_io_job(jobs, kc);
		else {
			job = list_entry(jobs->next, struct kcopyd_job, list);
			list_del(&job->list);
		}
	}
	spin_unlock_irqrestore(&kc->job_lock, flags);

	return job;
}

static void push(struct list_head *jobs, struct kcopyd_job *job)
{
	unsigned long flags;
	struct dm_kcopyd_client *kc = job->kc;

	spin_lock_irqsave(&kc->job_lock, flags);
	list_add_tail(&job->list, jobs);
	spin_unlock_irqrestore(&kc->job_lock, flags);
}


static void push_head(struct list_head *jobs, struct kcopyd_job *job)
{
	unsigned long flags;
	struct dm_kcopyd_client *kc = job->kc;

	spin_lock_irqsave(&kc->job_lock, flags);
	list_add(&job->list, jobs);
	spin_unlock_irqrestore(&kc->job_lock, flags);
}

/*
 * These three functions process 1 item from the corresponding
 * job list.
 *
 * They return:
 * < 0: error
 *   0: success
 * > 0: can't process yet.
 */
static int run_complete_job(struct kcopyd_job *job)
{
	void *context = job->context;
	int read_err = job->read_err;
	unsigned long write_err = job->write_err;
	dm_kcopyd_notify_fn fn = job->fn;
	struct dm_kcopyd_client *kc = job->kc;

	if (job->pages && job->pages != &zero_page_list)
		kcopyd_put_pages(kc, job->pages);
	/*
	 * If this is the master job, the sub jobs have already
	 * completed so we can free everything.
	 */
	if (job->master_job == job)
		mempool_free(job, kc->job_pool);
	fn(read_err, write_err, context);

	if (atomic_dec_and_test(&kc->nr_jobs))
		wake_up(&kc->destroyq);

	return 0;
}

static void complete_io(unsigned long error, void *context)
{
	struct kcopyd_job *job = (struct kcopyd_job *) context;
	struct dm_kcopyd_client *kc = job->kc;

	io_job_finish(kc->throttle);

	if (error) {
		if (op_is_write(job->rw))
			job->write_err |= error;
		else
			job->read_err = 1;

		if (!test_bit(DM_KCOPYD_IGNORE_ERROR, &job->flags)) {
			push(&kc->complete_jobs, job);
			wake(kc);
			return;
		}
	}

	if (op_is_write(job->rw))
		push(&kc->complete_jobs, job);

	else {
		job->rw = WRITE;
		push(&kc->io_jobs, job);
	}

	wake(kc);
}

/*
 * Request io on as many buffer heads as we can currently get for
 * a particular job.
 */
static int run_io_job(struct kcopyd_job *job)
{
	int r;
	struct dm_io_request io_req = {
		.bi_op = job->rw,
		.bi_op_flags = 0,
		.mem.type = DM_IO_PAGE_LIST,
		.mem.ptr.pl = job->pages,
		.mem.offset = 0,
		.notify.fn = complete_io,
		.notify.context = job,
		.client = job->kc->io_client,
	};

	/*
	 * If we need to write sequentially and some reads or writes failed,
	 * no point in continuing.
	 */
	if (test_bit(DM_KCOPYD_WRITE_SEQ, &job->flags) &&
	    job->master_job->write_err)
		return -EIO;

	io_job_start(job->kc->throttle);

	if (job->rw == READ)
		r = dm_io(&io_req, 1, &job->source, NULL);
	else
		r = dm_io(&io_req, job->num_dests, job->dests, NULL);

	return r;
}

static int run_pages_job(struct kcopyd_job *job)
{
	int r;
	unsigned nr_pages = dm_div_up(job->dests[0].count, PAGE_SIZE >> 9);

	r = kcopyd_get_pages(job->kc, nr_pages, &job->pages);
	if (!r) {
		/* this job is ready for io */
		push(&job->kc->io_jobs, job);
		return 0;
	}

	if (r == -ENOMEM)
		/* can't complete now */
		return 1;

	return r;
}

/*
 * Run through a list for as long as possible.  Returns the count
 * of successful jobs.
 */
static int process_jobs(struct list_head *jobs, struct dm_kcopyd_client *kc,
			int (*fn) (struct kcopyd_job *))
{
	struct kcopyd_job *job;
	int r, count = 0;

	while ((job = pop(jobs, kc))) {

		r = fn(job);

		if (r < 0) {
			/* error this rogue job */
			if (op_is_write(job->rw))
				job->write_err = (unsigned long) -1L;
			else
				job->read_err = 1;
			push(&kc->complete_jobs, job);
			break;
		}

		if (r > 0) {
			/*
			 * We couldn't service this job ATM, so
			 * push this job back onto the list.
			 */
			push_head(jobs, job);
			break;
		}

		count++;
	}

	return count;
}

/*
 * kcopyd does this every time it's woken up.
 */
static void do_work(struct work_struct *work)
{
	struct dm_kcopyd_client *kc = container_of(work,
					struct dm_kcopyd_client, kcopyd_work);
	struct blk_plug plug;

	/*
	 * The order that these are called is *very* important.
	 * complete jobs can free some pages for pages jobs.
	 * Pages jobs when successful will jump onto the io jobs
	 * list.  io jobs call wake when they complete and it all
	 * starts again.
	 */
	blk_start_plug(&plug);
	process_jobs(&kc->complete_jobs, kc, run_complete_job);
	process_jobs(&kc->pages_jobs, kc, run_pages_job);
	process_jobs(&kc->io_jobs, kc, run_io_job);
	blk_finish_plug(&plug);
}

/*
 * If we are copying a small region we just dispatch a single job
 * to do the copy, otherwise the io has to be split up into many
 * jobs.
 */
static void dispatch_job(struct kcopyd_job *job)
{
	struct dm_kcopyd_client *kc = job->kc;
	atomic_inc(&kc->nr_jobs);
	if (unlikely(!job->source.count))
		push(&kc->complete_jobs, job);
	else if (job->pages == &zero_page_list)
		push(&kc->io_jobs, job);
	else
		push(&kc->pages_jobs, job);
	wake(kc);
}

static void segment_complete(int read_err, unsigned long write_err,
			     void *context)
{
	/* FIXME: tidy this function */
	sector_t progress = 0;
	sector_t count = 0;
	struct kcopyd_job *sub_job = (struct kcopyd_job *) context;
	struct kcopyd_job *job = sub_job->master_job;
	struct dm_kcopyd_client *kc = job->kc;

	mutex_lock(&job->lock);

	/* update the error */
	if (read_err)
		job->read_err = 1;

	if (write_err)
		job->write_err |= write_err;

	/*
	 * Only dispatch more work if there hasn't been an error.
	 */
	if ((!job->read_err && !job->write_err) ||
	    test_bit(DM_KCOPYD_IGNORE_ERROR, &job->flags)) {
		/* get the next chunk of work */
		progress = job->progress;
		count = job->source.count - progress;
		if (count) {
			if (count > SUB_JOB_SIZE)
				count = SUB_JOB_SIZE;

			job->progress += count;
		}
	}
	mutex_unlock(&job->lock);

	if (count) {
		int i;

		*sub_job = *job;
		sub_job->write_offset = progress;
		sub_job->source.sector += progress;
		sub_job->source.count = count;

		for (i = 0; i < job->num_dests; i++) {
			sub_job->dests[i].sector += progress;
			sub_job->dests[i].count = count;
		}

		sub_job->fn = segment_complete;
		sub_job->context = sub_job;
		dispatch_job(sub_job);

	} else if (atomic_dec_and_test(&job->sub_jobs)) {

		/*
		 * Queue the completion callback to the kcopyd thread.
		 *
		 * Some callers assume that all the completions are called
		 * from a single thread and don't race with each other.
		 *
		 * We must not call the callback directly here because this
		 * code may not be executing in the thread.
		 */
		push(&kc->complete_jobs, job);
		wake(kc);
	}
}

/*
 * Create some sub jobs to share the work between them.
 */
static void split_job(struct kcopyd_job *master_job)
{
	int i;

	atomic_inc(&master_job->kc->nr_jobs);

	atomic_set(&master_job->sub_jobs, SPLIT_COUNT);
	for (i = 0; i < SPLIT_COUNT; i++) {
		master_job[i + 1].master_job = master_job;
		segment_complete(0, 0u, &master_job[i + 1]);
	}
}

int dm_kcopyd_copy(struct dm_kcopyd_client *kc, struct dm_io_region *from,
		   unsigned int num_dests, struct dm_io_region *dests,
		   unsigned int flags, dm_kcopyd_notify_fn fn, void *context)
{
	struct kcopyd_job *job;
	int i;

	/*
	 * Allocate an array of jobs consisting of one master job
	 * followed by SPLIT_COUNT sub jobs.
	 */
	job = mempool_alloc(kc->job_pool, GFP_NOIO);

	/*
	 * set up for the read.
	 */
	job->kc = kc;
	job->flags = flags;
	job->read_err = 0;
	job->write_err = 0;

	job->num_dests = num_dests;
	memcpy(&job->dests, dests, sizeof(*dests) * num_dests);

	/*
	 * If one of the destination is a host-managed zoned block device,
	 * we need to write sequentially. If one of the destination is a
	 * host-aware device, then leave it to the caller to choose what to do.
	 */
	if (!test_bit(DM_KCOPYD_WRITE_SEQ, &job->flags)) {
		for (i = 0; i < job->num_dests; i++) {
			if (bdev_zoned_model(dests[i].bdev) == BLK_ZONED_HM) {
				set_bit(DM_KCOPYD_WRITE_SEQ, &job->flags);
				break;
			}
		}
	}

	/*
	 * If we need to write sequentially, errors cannot be ignored.
	 */
	if (test_bit(DM_KCOPYD_WRITE_SEQ, &job->flags) &&
	    test_bit(DM_KCOPYD_IGNORE_ERROR, &job->flags))
		clear_bit(DM_KCOPYD_IGNORE_ERROR, &job->flags);

	if (from) {
		job->source = *from;
		job->pages = NULL;
		job->rw = READ;
	} else {
		memset(&job->source, 0, sizeof job->source);
		job->source.count = job->dests[0].count;
		job->pages = &zero_page_list;

		/*
		 * Use WRITE ZEROES to optimize zeroing if all dests support it.
		 */
		job->rw = REQ_OP_WRITE_ZEROES;
		for (i = 0; i < job->num_dests; i++)
			if (!bdev_write_zeroes_sectors(job->dests[i].bdev)) {
				job->rw = WRITE;
				break;
			}
	}

	job->fn = fn;
	job->context = context;
	job->master_job = job;
	job->write_offset = 0;

	if (job->source.count <= SUB_JOB_SIZE)
		dispatch_job(job);
	else {
		mutex_init(&job->lock);
		job->progress = 0;
		split_job(job);
	}

	return 0;
}
EXPORT_SYMBOL(dm_kcopyd_copy);

int dm_kcopyd_zero(struct dm_kcopyd_client *kc,
		   unsigned num_dests, struct dm_io_region *dests,
		   unsigned flags, dm_kcopyd_notify_fn fn, void *context)
{
	return dm_kcopyd_copy(kc, NULL, num_dests, dests, flags, fn, context);
}
EXPORT_SYMBOL(dm_kcopyd_zero);

void *dm_kcopyd_prepare_callback(struct dm_kcopyd_client *kc,
				 dm_kcopyd_notify_fn fn, void *context)
{
	struct kcopyd_job *job;

	job = mempool_alloc(kc->job_pool, GFP_NOIO);

	memset(job, 0, sizeof(struct kcopyd_job));
	job->kc = kc;
	job->fn = fn;
	job->context = context;
	job->master_job = job;

	atomic_inc(&kc->nr_jobs);

	return job;
}
EXPORT_SYMBOL(dm_kcopyd_prepare_callback);

void dm_kcopyd_do_callback(void *j, int read_err, unsigned long write_err)
{
	struct kcopyd_job *job = j;
	struct dm_kcopyd_client *kc = job->kc;

	job->read_err = read_err;
	job->write_err = write_err;

	push(&kc->complete_jobs, job);
	wake(kc);
}
EXPORT_SYMBOL(dm_kcopyd_do_callback);

/*
 * Cancels a kcopyd job, eg. someone might be deactivating a
 * mirror.
 */
#if 0
int kcopyd_cancel(struct kcopyd_job *job, int block)
{
	/* FIXME: finish */
	return -1;
}
#endif  /*  0  */

/*-----------------------------------------------------------------
 * Client setup
 *---------------------------------------------------------------*/
struct dm_kcopyd_client *dm_kcopyd_client_create(struct dm_kcopyd_throttle *throttle)
{
	int r = -ENOMEM;
	struct dm_kcopyd_client *kc;

	kc = kmalloc(sizeof(*kc), GFP_KERNEL);
	if (!kc)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&kc->job_lock);
	INIT_LIST_HEAD(&kc->complete_jobs);
	INIT_LIST_HEAD(&kc->io_jobs);
	INIT_LIST_HEAD(&kc->pages_jobs);
	kc->throttle = throttle;

	kc->job_pool = mempool_create_slab_pool(MIN_JOBS, _job_cache);
	if (!kc->job_pool)
		goto bad_slab;

	INIT_WORK(&kc->kcopyd_work, do_work);
	kc->kcopyd_wq = alloc_workqueue("kcopyd", WQ_MEM_RECLAIM, 0);
	if (!kc->kcopyd_wq)
		goto bad_workqueue;

	kc->pages = NULL;
	kc->nr_reserved_pages = kc->nr_free_pages = 0;
	r = client_reserve_pages(kc, RESERVE_PAGES);
	if (r)
		goto bad_client_pages;

	kc->io_client = dm_io_client_create();
	if (IS_ERR(kc->io_client)) {
		r = PTR_ERR(kc->io_client);
		goto bad_io_client;
	}

	init_waitqueue_head(&kc->destroyq);
	atomic_set(&kc->nr_jobs, 0);

	return kc;

bad_io_client:
	client_free_pages(kc);
bad_client_pages:
	destroy_workqueue(kc->kcopyd_wq);
bad_workqueue:
	mempool_destroy(kc->job_pool);
bad_slab:
	kfree(kc);

	return ERR_PTR(r);
}
EXPORT_SYMBOL(dm_kcopyd_client_create);

void dm_kcopyd_client_destroy(struct dm_kcopyd_client *kc)
{
	/* Wait for completion of all jobs submitted by this client. */
	wait_event(kc->destroyq, !atomic_read(&kc->nr_jobs));

	BUG_ON(!list_empty(&kc->complete_jobs));
	BUG_ON(!list_empty(&kc->io_jobs));
	BUG_ON(!list_empty(&kc->pages_jobs));
	destroy_workqueue(kc->kcopyd_wq);
	dm_io_client_destroy(kc->io_client);
	client_free_pages(kc);
	mempool_destroy(kc->job_pool);
	kfree(kc);
}
EXPORT_SYMBOL(dm_kcopyd_client_destroy);
