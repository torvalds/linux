/*
 * Zen IO scheduler
 * Primarily based on Noop, deadline, and SIO IO schedulers.
 *
 * Copyright (C) 2012 Brandon Berhent <bbedward@gmail.com>
 *
 * FCFS, dispatches are back-inserted, deadlines ensure fairness.
 * Should work best with devices where there is no travel delay.
 */
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>

enum zen_data_dir { ASYNC, SYNC };

static const int sync_expire  = HZ / 4;    /* max time before a sync is submitted. */
static const int async_expire = 2 * HZ;    /* ditto for async, these limits are SOFT! */
static const int fifo_batch = 1;

struct zen_data {
	/* Runtime Data */
	/* Requests are only present on fifo_list */
	struct list_head fifo_list[2];

        unsigned int batching;          /* number of sequential requests made */

	/* tunables */
	int fifo_expire[2];
	int fifo_batch;
};

static inline struct zen_data *
zen_get_data(struct request_queue *q) {
	return q->elevator->elevator_data;
}

static void zen_dispatch(struct zen_data *, struct request *);

static void
zen_merged_requests(struct request_queue *q, struct request *req,
                    struct request *next)
{
	/*
	 * if next expires before rq, assign its expire time to arq
	 * and move into next position (next will be deleted) in fifo
	 */
	if (!list_empty(&req->queuelist) && !list_empty(&next->queuelist)) {
		if (time_before(rq_fifo_time(next), rq_fifo_time(req))) {
			list_move(&req->queuelist, &next->queuelist);
			rq_set_fifo_time(req, rq_fifo_time(next));
		}
	}

	/* next request is gone */
	rq_fifo_clear(next);
}

static void zen_add_request(struct request_queue *q, struct request *rq)
{
	struct zen_data *zdata = zen_get_data(q);
	const int dir = rq_data_dir(rq);

	if (zdata->fifo_expire[dir]) {
		rq_set_fifo_time(rq, jiffies + zdata->fifo_expire[dir]);
		list_add_tail(&rq->queuelist, &zdata->fifo_list[dir]);
	}
}

static void zen_dispatch(struct zen_data *zdata, struct request *rq)
{
	/* Remove request from list and dispatch it */
	rq_fifo_clear(rq);
	elv_dispatch_add_tail(rq->q, rq);

	/* Increment # of sequential requests */
	zdata->batching++;
}

/*
 * get the first expired request in direction ddir
 */
static struct request *
zen_expired_request(struct zen_data *zdata, int ddir)
{
        struct request *rq;

        if (list_empty(&zdata->fifo_list[ddir]))
                return NULL;

        rq = rq_entry_fifo(zdata->fifo_list[ddir].next);
        if (time_after(jiffies, rq_fifo_time(rq)))
                return rq;

        return NULL;
}

/*
 * zen_check_fifo returns 0 if there are no expired requests on the fifo,
 * otherwise it returns the next expired request
 */
static struct request *
zen_check_fifo(struct zen_data *zdata)
{
        struct request *rq_sync = zen_expired_request(zdata, SYNC);
        struct request *rq_async = zen_expired_request(zdata, ASYNC);

        if (rq_async && rq_sync) {
	if (time_after(rq_fifo_time(rq_async), rq_fifo_time(rq_sync)))
			return rq_sync;
        } else if (rq_sync) {
                return rq_sync;
	} else if (rq_async) {
		return rq_async;
	}

        return 0;
}

static struct request *
zen_choose_request(struct zen_data *zdata)
{
        /*
         * Retrieve request from available fifo list.
         * Synchronous requests have priority over asynchronous.
         */
        if (!list_empty(&zdata->fifo_list[SYNC]))
                return rq_entry_fifo(zdata->fifo_list[SYNC].next);
        if (!list_empty(&zdata->fifo_list[ASYNC]))
                return rq_entry_fifo(zdata->fifo_list[ASYNC].next);

        return NULL;
}

static int zen_dispatch_requests(struct request_queue *q, int force)
{
	struct zen_data *zdata = zen_get_data(q);
	struct request *rq = NULL;

	/* Check for and issue expired requests */
	if (zdata->batching > zdata->fifo_batch) {
		zdata->batching = 0;
		rq = zen_check_fifo(zdata);
	}

	if (!rq) {
		rq = zen_choose_request(zdata);
		if (!rq)
			return 0;
	}

	zen_dispatch(zdata, rq);

	return 1;
}

static void *zen_init_queue(struct request_queue *q)
{
	struct zen_data *zdata;

	zdata = kmalloc_node(sizeof(*zdata), GFP_KERNEL, q->node);
	if (!zdata)
		return NULL;
	INIT_LIST_HEAD(&zdata->fifo_list[SYNC]);
	INIT_LIST_HEAD(&zdata->fifo_list[ASYNC]);
	zdata->fifo_expire[SYNC] = sync_expire;
	zdata->fifo_expire[ASYNC] = async_expire;
	zdata->fifo_batch = fifo_batch;
	return zdata;
}

static void zen_exit_queue(struct elevator_queue *e)
{
	struct zen_data *zdata = e->elevator_data;

	BUG_ON(!list_empty(&zdata->fifo_list[SYNC]));
	BUG_ON(!list_empty(&zdata->fifo_list[ASYNC]));
	kfree(zdata);
}

/* Sysfs */
static ssize_t
zen_var_show(int var, char *page)
{
	return sprintf(page, "%d\n", var);
}

static ssize_t
zen_var_store(int *var, const char *page, size_t count)
{
	*var = simple_strtol(page, NULL, 10);
	return count;
}

#define SHOW_FUNCTION(__FUNC, __VAR, __CONV) \
static ssize_t __FUNC(struct elevator_queue *e, char *page) \
{ \
	struct zen_data *zdata = e->elevator_data; \
	int __data = __VAR; \
	if (__CONV) \
		__data = jiffies_to_msecs(__data); \
		return zen_var_show(__data, (page)); \
}
SHOW_FUNCTION(zen_sync_expire_show, zdata->fifo_expire[SYNC], 1);
SHOW_FUNCTION(zen_async_expire_show, zdata->fifo_expire[ASYNC], 1);
SHOW_FUNCTION(zen_fifo_batch_show, zdata->fifo_batch, 0);
#undef SHOW_FUNCTION

#define STORE_FUNCTION(__FUNC, __PTR, MIN, MAX, __CONV) \
static ssize_t __FUNC(struct elevator_queue *e, const char *page, size_t count) \
{ \
	struct zen_data *zdata = e->elevator_data; \
	int __data; \
	int ret = zen_var_store(&__data, (page), count); \
	if (__data < (MIN)) \
		__data = (MIN); \
	else if (__data > (MAX)) \
		__data = (MAX); \
	if (__CONV) \
		*(__PTR) = msecs_to_jiffies(__data); \
	else \
		*(__PTR) = __data; \
	return ret; \
}
STORE_FUNCTION(zen_sync_expire_store, &zdata->fifo_expire[SYNC], 0, INT_MAX, 1);
STORE_FUNCTION(zen_async_expire_store, &zdata->fifo_expire[ASYNC], 0, INT_MAX, 1);
STORE_FUNCTION(zen_fifo_batch_store, &zdata->fifo_batch, 0, INT_MAX, 0);
#undef STORE_FUNCTION

#define DD_ATTR(name) \
        __ATTR(name, S_IRUGO|S_IWUSR, zen_##name##_show, \
                                      zen_##name##_store)

static struct elv_fs_entry zen_attrs[] = {
        DD_ATTR(sync_expire),
        DD_ATTR(async_expire),
        DD_ATTR(fifo_batch),
        __ATTR_NULL
};

static struct elevator_type iosched_zen = {
	.ops = {
		.elevator_merge_req_fn		= zen_merged_requests,
		.elevator_dispatch_fn		= zen_dispatch_requests,
		.elevator_add_req_fn		= zen_add_request,
		.elevator_former_req_fn         = elv_rb_former_request,
		.elevator_latter_req_fn         = elv_rb_latter_request,
		.elevator_init_fn		= zen_init_queue,
		.elevator_exit_fn		= zen_exit_queue,
	},
	.elevator_attrs = zen_attrs,
	.elevator_name = "zen",
	.elevator_owner = THIS_MODULE,
};

static int __init zen_init(void)
{
	elv_register(&iosched_zen);

	return 0;
}

static void __exit zen_exit(void)
{
	elv_unregister(&iosched_zen);
}

module_init(zen_init);
module_exit(zen_exit);


MODULE_AUTHOR("Brandon Berhent");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Zen IO scheduler");
MODULE_VERSION("1.0");
