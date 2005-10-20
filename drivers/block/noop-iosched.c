/*
 * elevator noop
 */
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/init.h>

/*
 * See if we can find a request that this buffer can be coalesced with.
 */
static int elevator_noop_merge(request_queue_t *q, struct request **req,
			       struct bio *bio)
{
	int ret;

	ret = elv_try_last_merge(q, bio);
	if (ret != ELEVATOR_NO_MERGE)
		*req = q->last_merge;

	return ret;
}

static void elevator_noop_merge_requests(request_queue_t *q, struct request *req,
					 struct request *next)
{
	list_del_init(&next->queuelist);
}

static void elevator_noop_add_request(request_queue_t *q, struct request *rq)
{
	elv_dispatch_add_tail(q, rq);

	/*
	 * new merges must not precede this barrier
	 */
	if (rq->flags & REQ_HARDBARRIER)
		q->last_merge = NULL;
	else if (!q->last_merge)
		q->last_merge = rq;
}

static int elevator_noop_dispatch(request_queue_t *q, int force)
{
	return 0;
}

static struct elevator_type elevator_noop = {
	.ops = {
		.elevator_merge_fn		= elevator_noop_merge,
		.elevator_merge_req_fn		= elevator_noop_merge_requests,
		.elevator_dispatch_fn		= elevator_noop_dispatch,
		.elevator_add_req_fn		= elevator_noop_add_request,
	},
	.elevator_name = "noop",
	.elevator_owner = THIS_MODULE,
};

static int __init noop_init(void)
{
	return elv_register(&elevator_noop);
}

static void __exit noop_exit(void)
{
	elv_unregister(&elevator_noop);
}

module_init(noop_init);
module_exit(noop_exit);


MODULE_AUTHOR("Jens Axboe");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("No-op IO scheduler");
