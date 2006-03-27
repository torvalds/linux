/*
 * Copyright (C) 2002 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 *
 * Kcopyd provides a simple interface for copying an area of one
 * block-device to one or more other block-devices, with an asynchronous
 * completion notification.
 */

#include <asm/types.h>
#include <asm/atomic.h>

#include <linux/blkdev.h>
#include <linux/config.h>
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

#include "kcopyd.h"

static struct workqueue_struct *_kcopyd_wq;
static struct work_struct _kcopyd_work;

static inline void wake(void)
{
	queue_work(_kcopyd_wq, &_kcopyd_work);
}

/*-----------------------------------------------------------------
 * Each kcopyd client has its own little pool of preallocated
 * pages for kcopyd io.
 *---------------------------------------------------------------*/
struct kcopyd_client {
	struct list_head list;

	spinlock_t lock;
	struct page_list *pages;
	unsigned int nr_pages;
	unsigned int nr_free_pages;

	wait_queue_head_t destroyq;
	atomic_t nr_jobs;
};

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

static int kcopyd_get_pages(struct kcopyd_client *kc,
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

static void kcopyd_put_pages(struct kcopyd_client *kc, struct page_list *pl)
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

static int client_alloc_pages(struct kcopyd_client *kc, unsigned int nr)
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

static void client_free_pages(struct kcopyd_client *kc)
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
	struct kcopyd_client *kc;
	struct list_head list;
	unsigned long flags;

	/*
	 * Error state of the job.
	 */
	int read_err;
	unsigned int write_err;

	/*
	 * Either READ or WRITE
	 */
	int rw;
	struct io_region source;

	/*
	 * The destinations for the transfer.
	 */
	unsigned int num_dests;
	struct io_region dests[KCOPYD_MAX_REGIONS];

	sector_t offset;
	unsigned int nr_pages;
	struct page_list *pages;

	/*
	 * Set this to ensure you are notified when the job has
	 * completed.  'context' is for callback to use.
	 */
	kcopyd_notify_fn fn;
	void *context;

	/*
	 * These fields are only used if the job has been split
	 * into more manageable parts.
	 */
	struct semaphore lock;
	atomic_t sub_jobs;
	sector_t progress;
};

/* FIXME: this should scale with the number of pages */
#define MIN_JOBS 512

static kmem_cache_t *_job_cache;
static mempool_t *_job_pool;

/*
 * We maintain three lists of jobs:
 *
 * i)   jobs waiting for pages
 * ii)  jobs that have pages, and are waiting for the io to be issued.
 * iii) jobs that have completed.
 *
 * All three of these are protected by job_lock.
 */
static DEFINE_SPINLOCK(_job_lock);

static LIST_HEAD(_complete_jobs);
static LIST_HEAD(_io_jobs);
static LIST_HEAD(_pages_jobs);

static int jobs_init(void)
{
	_job_cache = kmem_cache_create("kcopyd-jobs",
				       sizeof(struct kcopyd_job),
				       __alignof__(struct kcopyd_job),
				       0, NULL, NULL);
	if (!_job_cache)
		return -ENOMEM;

	_job_pool = mempool_create_slab_pool(MIN_JOBS, _job_cache);
	if (!_job_pool) {
		kmem_cache_destroy(_job_cache);
		return -ENOMEM;
	}

	return 0;
}

static void jobs_exit(void)
{
	BUG_ON(!list_empty(&_complete_jobs));
	BUG_ON(!list_empty(&_io_jobs));
	BUG_ON(!list_empty(&_pages_jobs));

	mempool_destroy(_job_pool);
	kmem_cache_destroy(_job_cache);
	_job_pool = NULL;
	_job_cache = NULL;
}

/*
 * Functions to push and pop a job onto the head of a given job
 * list.
 */
static inline struct kcopyd_job *pop(struct list_head *jobs)
{
	struct kcopyd_job *job = NULL;
	unsigned long flags;

	spin_lock_irqsave(&_job_lock, flags);

	if (!list_empty(jobs)) {
		job = list_entry(jobs->next, struct kcopyd_job, list);
		list_del(&job->list);
	}
	spin_unlock_irqrestore(&_job_lock, flags);

	return job;
}

static inline void push(struct list_head *jobs, struct kcopyd_job *job)
{
	unsigned long flags;

	spin_lock_irqsave(&_job_lock, flags);
	list_add_tail(&job->list, jobs);
	spin_unlock_irqrestore(&_job_lock, flags);
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
	unsigned int write_err = job->write_err;
	kcopyd_notify_fn fn = job->fn;
	struct kcopyd_client *kc = job->kc;

	kcopyd_put_pages(kc, job->pages);
	mempool_free(job, _job_pool);
	fn(read_err, write_err, context);

	if (atomic_dec_and_test(&kc->nr_jobs))
		wake_up(&kc->destroyq);

	return 0;
}

static void complete_io(unsigned long error, void *context)
{
	struct kcopyd_job *job = (struct kcopyd_job *) context;

	if (error) {
		if (job->rw == WRITE)
			job->write_err &= error;
		else
			job->read_err = 1;

		if (!test_bit(KCOPYD_IGNORE_ERROR, &job->flags)) {
			push(&_complete_jobs, job);
			wake();
			return;
		}
	}

	if (job->rw == WRITE)
		push(&_complete_jobs, job);

	else {
		job->rw = WRITE;
		push(&_io_jobs, job);
	}

	wake();
}

/*
 * Request io on as many buffer heads as we can currently get for
 * a particular job.
 */
static int run_io_job(struct kcopyd_job *job)
{
	int r;

	if (job->rw == READ)
		r = dm_io_async(1, &job->source, job->rw,
				job->pages,
				job->offset, complete_io, job);

	else
		r = dm_io_async(job->num_dests, job->dests, job->rw,
				job->pages,
				job->offset, complete_io, job);

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
		push(&_io_jobs, job);
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
static int process_jobs(struct list_head *jobs, int (*fn) (struct kcopyd_job *))
{
	struct kcopyd_job *job;
	int r, count = 0;

	while ((job = pop(jobs))) {

		r = fn(job);

		if (r < 0) {
			/* error this rogue job */
			if (job->rw == WRITE)
				job->write_err = (unsigned int) -1;
			else
				job->read_err = 1;
			push(&_complete_jobs, job);
			break;
		}

		if (r > 0) {
			/*
			 * We couldn't service this job ATM, so
			 * push this job back onto the list.
			 */
			push(jobs, job);
			break;
		}

		count++;
	}

	return count;
}

/*
 * kcopyd does this every time it's woken up.
 */
static void do_work(void *ignored)
{
	/*
	 * The order that these are called is *very* important.
	 * complete jobs can free some pages for pages jobs.
	 * Pages jobs when successful will jump onto the io jobs
	 * list.  io jobs call wake when they complete and it all
	 * starts again.
	 */
	process_jobs(&_complete_jobs, run_complete_job);
	process_jobs(&_pages_jobs, run_pages_job);
	process_jobs(&_io_jobs, run_io_job);
}

/*
 * If we are copying a small region we just dispatch a single job
 * to do the copy, otherwise the io has to be split up into many
 * jobs.
 */
static void dispatch_job(struct kcopyd_job *job)
{
	atomic_inc(&job->kc->nr_jobs);
	push(&_pages_jobs, job);
	wake();
}

#define SUB_JOB_SIZE 128
static void segment_complete(int read_err,
			     unsigned int write_err, void *context)
{
	/* FIXME: tidy this function */
	sector_t progress = 0;
	sector_t count = 0;
	struct kcopyd_job *job = (struct kcopyd_job *) context;

	down(&job->lock);

	/* update the error */
	if (read_err)
		job->read_err = 1;

	if (write_err)
		job->write_err &= write_err;

	/*
	 * Only dispatch more work if there hasn't been an error.
	 */
	if ((!job->read_err && !job->write_err) ||
	    test_bit(KCOPYD_IGNORE_ERROR, &job->flags)) {
		/* get the next chunk of work */
		progress = job->progress;
		count = job->source.count - progress;
		if (count) {
			if (count > SUB_JOB_SIZE)
				count = SUB_JOB_SIZE;

			job->progress += count;
		}
	}
	up(&job->lock);

	if (count) {
		int i;
		struct kcopyd_job *sub_job = mempool_alloc(_job_pool, GFP_NOIO);

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
		mempool_free(job, _job_pool);
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

int kcopyd_copy(struct kcopyd_client *kc, struct io_region *from,
		unsigned int num_dests, struct io_region *dests,
		unsigned int flags, kcopyd_notify_fn fn, void *context)
{
	struct kcopyd_job *job;

	/*
	 * Allocate a new job.
	 */
	job = mempool_alloc(_job_pool, GFP_NOIO);

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
		init_MUTEX(&job->lock);
		job->progress = 0;
		split_job(job);
	}

	return 0;
}

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
 * Unit setup
 *---------------------------------------------------------------*/
static DEFINE_MUTEX(_client_lock);
static LIST_HEAD(_clients);

static void client_add(struct kcopyd_client *kc)
{
	mutex_lock(&_client_lock);
	list_add(&kc->list, &_clients);
	mutex_unlock(&_client_lock);
}

static void client_del(struct kcopyd_client *kc)
{
	mutex_lock(&_client_lock);
	list_del(&kc->list);
	mutex_unlock(&_client_lock);
}

static DEFINE_MUTEX(kcopyd_init_lock);
static int kcopyd_clients = 0;

static int kcopyd_init(void)
{
	int r;

	mutex_lock(&kcopyd_init_lock);

	if (kcopyd_clients) {
		/* Already initialized. */
		kcopyd_clients++;
		mutex_unlock(&kcopyd_init_lock);
		return 0;
	}

	r = jobs_init();
	if (r) {
		mutex_unlock(&kcopyd_init_lock);
		return r;
	}

	_kcopyd_wq = create_singlethread_workqueue("kcopyd");
	if (!_kcopyd_wq) {
		jobs_exit();
		mutex_unlock(&kcopyd_init_lock);
		return -ENOMEM;
	}

	kcopyd_clients++;
	INIT_WORK(&_kcopyd_work, do_work, NULL);
	mutex_unlock(&kcopyd_init_lock);
	return 0;
}

static void kcopyd_exit(void)
{
	mutex_lock(&kcopyd_init_lock);
	kcopyd_clients--;
	if (!kcopyd_clients) {
		jobs_exit();
		destroy_workqueue(_kcopyd_wq);
		_kcopyd_wq = NULL;
	}
	mutex_unlock(&kcopyd_init_lock);
}

int kcopyd_client_create(unsigned int nr_pages, struct kcopyd_client **result)
{
	int r = 0;
	struct kcopyd_client *kc;

	r = kcopyd_init();
	if (r)
		return r;

	kc = kmalloc(sizeof(*kc), GFP_KERNEL);
	if (!kc) {
		kcopyd_exit();
		return -ENOMEM;
	}

	spin_lock_init(&kc->lock);
	kc->pages = NULL;
	kc->nr_pages = kc->nr_free_pages = 0;
	r = client_alloc_pages(kc, nr_pages);
	if (r) {
		kfree(kc);
		kcopyd_exit();
		return r;
	}

	r = dm_io_get(nr_pages);
	if (r) {
		client_free_pages(kc);
		kfree(kc);
		kcopyd_exit();
		return r;
	}

	init_waitqueue_head(&kc->destroyq);
	atomic_set(&kc->nr_jobs, 0);

	client_add(kc);
	*result = kc;
	return 0;
}

void kcopyd_client_destroy(struct kcopyd_client *kc)
{
	/* Wait for completion of all jobs submitted by this client. */
	wait_event(kc->destroyq, !atomic_read(&kc->nr_jobs));

	dm_io_put(kc->nr_pages);
	client_free_pages(kc);
	client_del(kc);
	kfree(kc);
	kcopyd_exit();
}

EXPORT_SYMBOL(kcopyd_client_create);
EXPORT_SYMBOL(kcopyd_client_destroy);
EXPORT_SYMBOL(kcopyd_copy);
