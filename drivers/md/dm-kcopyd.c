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
#include <asm/atomic.h>
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
#include <linux/device-mapper.h>
#include <linux/dm-kcopyd.h>

#include "dm.h"

/*-----------------------------------------------------------------
 * Each kcopyd client has its own little pool of preallocated
 * pages for kcopyd io.
 *---------------------------------------------------------------*/
struct dm_kcopyd_client {
	spinlock_t lock;
	struct page_list *pages;
	unsigned int nr_pages;
	unsigned int nr_free_pages;

	struct dm_io_client *io_client;

	wait_queue_head_t destroyq;
	atomic_t nr_jobs;

	mempool_t *job_pool;

	struct workqueue_struct *kcopyd_wq;
	struct work_struct kcopyd_work;

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

static void wake(struct dm_kcopyd_client *kc)
{
	queue_work(kc->kcopyd_wq, &kc->kcopyd_work);
}

static struct page_list *alloc_pl(void)
{
	struct page_list *pl;

	pl = kmalloc(sizeof(*pl), GFP_KERNEL);
	if (!pl)
		return NULL;

	pl->page = alloc_page(GFP_KERNEL);
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

static int kcopyd_get_pages(struct dm_kcopyd_client *kc,
			    unsigned int nr, struct page_list **pages)
{
	struct page_list *pl;

	spin_lock(&kc->lock);
	if (kc->nr_free_pages < nr) {
		spin_unlock(&kc->lock);
		return -ENOMEM;
	}

	kc->nr_free_pages -= nr;
	for (*pages = pl = kc->pages; --nr; pl = pl->next)
		;

	kc->pages = pl->next;
	pl->next = NULL;

	spin_unlock(&kc->lock);

	return 0;
}

static void kcopyd_put_pages(struct dm_kcopyd_client *kc, struct page_list *pl)
{
	struct page_list *cursor;

	spin_lock(&kc->lock);
	for (cursor = pl; cursor->next; cursor = cursor->next)
		kc->nr_free_pages++;

	kc->nr_free_pages++;
	cursor->next = kc->pages;
	kc->pages = pl;
	spin_unlock(&kc->lock);
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

static int client_alloc_pages(struct dm_kcopyd_client *kc, unsigned int nr)
{
	unsigned int i;
	struct page_list *pl = NULL, *next;

	for (i = 0; i < nr; i++) {
		next = alloc_pl();
		if (!next) {
			if (pl)
				drop_pages(pl);
			return -ENOMEM;
		}
		next->next = pl;
		pl = next;
	}

	kcopyd_put_pages(kc, pl);
	kc->nr_pages += nr;
	return 0;
}

static void client_free_pages(struct dm_kcopyd_client *kc)
{
	BUG_ON(kc->nr_free_pages != kc->nr_pages);
	drop_pages(kc->pages);
	kc->pages = NULL;
	kc->nr_free_pages = kc->nr_pages = 0;
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

	sector_t offset;
	unsigned int nr_pages;
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
};

/* FIXME: this should scale with the number of pages */
#define MIN_JOBS 512

static struct kmem_cache *_job_cache;

int __init dm_kcopyd_init(void)
{
	_job_cache = KMEM_CACHE(kcopyd_job, 0);
	if (!_job_cache)
		return -ENOMEM;

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
static struct kcopyd_job *pop(struct list_head *jobs,
			      struct dm_kcopyd_client *kc)
{
	struct kcopyd_job *job = NULL;
	unsigned long flags;

	spin_lock_irqsave(&kc->job_lock, flags);

	if (!list_empty(jobs)) {
		job = list_entry(jobs->next, struct kcopyd_job, list);
		list_del(&job->list);
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

	kcopyd_put_pages(kc, job->pages);
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

	if (error) {
		if (job->rw == WRITE)
			job->write_err |= error;
		else
			job->read_err = 1;

		if (!test_bit(DM_KCOPYD_IGNORE_ERROR, &job->flags)) {
			push(&kc->complete_jobs, job);
			wake(kc);
			return;
		}
	}

	if (job->rw == WRITE)
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
		.bi_rw = job->rw | (1 << BIO_RW_SYNCIO) | (1 << BIO_RW_UNPLUG),
		.mem.type = DM_IO_PAGE_LIST,
		.mem.ptr.pl = job->pages,
		.mem.offset = job->offset,
		.notify.fn = complete_io,
		.notify.context = job,
		.client = job->kc->io_client,
	};

	if (job->rw == READ)
		r = dm_io(&io_req, 1, &job->source, NULL);
	else
		r = dm_io(&io_req, job->num_dests, job->dests, NULL);

	return r;
}

static int run_pages_job(struct kcopyd_job *job)
{
	int r;

	job->nr_pages = dm_div_up(job->dests[0].count + job->offset,
				  PAGE_SIZE >> 9);
	r = kcopyd_get_pages(job->kc, job->nr_pages, &job->pages);
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
			if (job->rw == WRITE)
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

	/*
	 * The order that these are called is *very* important.
	 * complete jobs can free some pages for pages jobs.
	 * Pages jobs when successful will jump onto the io jobs
	 * list.  io jobs call wake when they complete and it all
	 * starts again.
	 */
	process_jobs(&kc->complete_jobs, kc, run_complete_job);
	process_jobs(&kc->pages_jobs, kc, run_pages_job);
	process_jobs(&kc->io_jobs, kc, run_io_job);
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
	push(&kc->pages_jobs, job);
	wake(kc);
}

#define SUB_JOB_SIZE 128
static void segment_complete(int read_err, unsigned long write_err,
			     void *context)
{
	/* FIXME: tidy this function */
	sector_t progress = 0;
	sector_t count = 0;
	struct kcopyd_job *job = (struct kcopyd_job *) context;

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
		struct kcopyd_job *sub_job = mempool_alloc(job->kc->job_pool,
							   GFP_NOIO);

		*sub_job = *job;
		sub_job->source.sector += progress;
		sub_job->source.count = count;

		for (i = 0; i < job->num_dests; i++) {
			sub_job->dests[i].sector += progress;
			sub_job->dests[i].count = count;
		}

		sub_job->fn = segment_complete;
		sub_job->context = job;
		dispatch_job(sub_job);

	} else if (atomic_dec_and_test(&job->sub_jobs)) {

		/*
		 * To avoid a race we must keep the job around
		 * until after the notify function has completed.
		 * Otherwise the client may try and stop the job
		 * after we've completed.
		 */
		job->fn(read_err, write_err, job->context);
		mempool_free(job, job->kc->job_pool);
	}
}

/*
 * Create some little jobs that will do the move between
 * them.
 */
#define SPLIT_COUNT 8
static void split_job(struct kcopyd_job *job)
{
	int i;

	atomic_set(&job->sub_jobs, SPLIT_COUNT);
	for (i = 0; i < SPLIT_COUNT; i++)
		segment_complete(0, 0u, job);
}

int dm_kcopyd_copy(struct dm_kcopyd_client *kc, struct dm_io_region *from,
		   unsigned int num_dests, struct dm_io_region *dests,
		   unsigned int flags, dm_kcopyd_notify_fn fn, void *context)
{
	struct kcopyd_job *job;

	/*
	 * Allocate a new job.
	 */
	job = mempool_alloc(kc->job_pool, GFP_NOIO);

	/*
	 * set up for the read.
	 */
	job->kc = kc;
	job->flags = flags;
	job->read_err = 0;
	job->write_err = 0;
	job->rw = READ;

	job->source = *from;

	job->num_dests = num_dests;
	memcpy(&job->dests, dests, sizeof(*dests) * num_dests);

	job->offset = 0;
	job->nr_pages = 0;
	job->pages = NULL;

	job->fn = fn;
	job->context = context;

	if (job->source.count < SUB_JOB_SIZE)
		dispatch_job(job);

	else {
		mutex_init(&job->lock);
		job->progress = 0;
		split_job(job);
	}

	return 0;
}
EXPORT_SYMBOL(dm_kcopyd_copy);

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
int dm_kcopyd_client_create(unsigned int nr_pages,
			    struct dm_kcopyd_client **result)
{
	int r = -ENOMEM;
	struct dm_kcopyd_client *kc;

	kc = kmalloc(sizeof(*kc), GFP_KERNEL);
	if (!kc)
		return -ENOMEM;

	spin_lock_init(&kc->lock);
	spin_lock_init(&kc->job_lock);
	INIT_LIST_HEAD(&kc->complete_jobs);
	INIT_LIST_HEAD(&kc->io_jobs);
	INIT_LIST_HEAD(&kc->pages_jobs);

	kc->job_pool = mempool_create_slab_pool(MIN_JOBS, _job_cache);
	if (!kc->job_pool)
		goto bad_slab;

	INIT_WORK(&kc->kcopyd_work, do_work);
	kc->kcopyd_wq = create_singlethread_workqueue("kcopyd");
	if (!kc->kcopyd_wq)
		goto bad_workqueue;

	kc->pages = NULL;
	kc->nr_pages = kc->nr_free_pages = 0;
	r = client_alloc_pages(kc, nr_pages);
	if (r)
		goto bad_client_pages;

	kc->io_client = dm_io_client_create(nr_pages);
	if (IS_ERR(kc->io_client)) {
		r = PTR_ERR(kc->io_client);
		goto bad_io_client;
	}

	init_waitqueue_head(&kc->destroyq);
	atomic_set(&kc->nr_jobs, 0);

	*result = kc;
	return 0;

bad_io_client:
	client_free_pages(kc);
bad_client_pages:
	destroy_workqueue(kc->kcopyd_wq);
bad_workqueue:
	mempool_destroy(kc->job_pool);
bad_slab:
	kfree(kc);

	return r;
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
