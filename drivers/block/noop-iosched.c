/*
 * elevator noop
 */
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/init.h>

static void elevator_noop_add_request(request_queue_t *q, struct request *rq)
{
	rq->flags |= REQ_NOMERGE;
	elv_dispatch_add_tail(q, rq);
}

static int elevator_noop_dispatch(request_queue_t *q, int force)
{
	return 0;
}

static struct elevator_type elevator_noop = {
	.ops = {
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
