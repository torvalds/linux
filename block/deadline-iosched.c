/*
 *  Deadline i/o scheduler.
 *
 *  Copyright (C) 2002 Jens Axboe <axboe@suse.de>
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/hash.h>
#include <linux/rbtree.h>

/*
 * See Documentation/block/deadline-iosched.txt
 */
static const int read_expire = HZ / 2;  /* max time before a read is submitted. */
static const int write_expire = 5 * HZ; /* ditto for writes, these limits are SOFT! */
static const int writes_starved = 2;    /* max times reads can starve a write */
static const int fifo_batch = 16;       /* # of sequential requests treated as one
				     by the above parameters. For throughput. */

static const int deadline_hash_shift = 5;
#define DL_HASH_BLOCK(sec)	((sec) >> 3)
#define DL_HASH_FN(sec)		(hash_long(DL_HASH_BLOCK((sec)), deadline_hash_shift))
#define DL_HASH_ENTRIES		(1 << deadline_hash_shift)
#define rq_hash_key(rq)		((rq)->sector + (rq)->nr_sectors)
#define ON_HASH(drq)		(!hlist_unhashed(&(drq)->hash))

struct deadline_data {
	/*
	 * run time data
	 */

	/*
	 * requests (deadline_rq s) are present on both sort_list and fifo_list
	 */
	struct rb_root sort_list[2];	
	struct list_head fifo_list[2];
	
	/*
	 * next in sort order. read, write or both are NULL
	 */
	struct deadline_rq *next_drq[2];
	struct hlist_head *hash;	/* request hash */
	unsigned int batching;		/* number of sequential requests made */
	sector_t last_sector;		/* head position */
	unsigned int starved;		/* times reads have starved writes */

	/*
	 * settings that change how the i/o scheduler behaves
	 */
	int fifo_expire[2];
	int fifo_batch;
	int writes_starved;
	int front_merges;

	mempool_t *drq_pool;
};

/*
 * pre-request data.
 */
struct deadline_rq {
	/*
	 * rbtree index, key is the starting offset
	 */
	struct rb_node rb_node;
	sector_t rb_key;

	struct request *request;

	/*
	 * request hash, key is the ending offset (for back merge lookup)
	 */
	struct hlist_node hash;

	/*
	 * expire fifo
	 */
	struct list_head fifo;
	unsigned long expires;
};

static void deadline_move_request(struct deadline_data *dd, struct deadline_rq *drq);

static kmem_cache_t *drq_pool;

#define RQ_DATA(rq)	((struct deadline_rq *) (rq)->elevator_private)

/*
 * the back merge hash support functions
 */
static inline void __deadline_del_drq_hash(struct deadline_rq *drq)
{
	hlist_del_init(&drq->hash);
}

static inline void deadline_del_drq_hash(struct deadline_rq *drq)
{
	if (ON_HASH(drq))
		__deadline_del_drq_hash(drq);
}

static inline void
deadline_add_drq_hash(struct deadline_data *dd, struct deadline_rq *drq)
{
	struct request *rq = drq->request;

	BUG_ON(ON_HASH(drq));

	hlist_add_head(&drq->hash, &dd->hash[DL_HASH_FN(rq_hash_key(rq))]);
}

/*
 * move hot entry to front of chain
 */
static inline void
deadline_hot_drq_hash(struct deadline_data *dd, struct deadline_rq *drq)
{
	struct request *rq = drq->request;
	struct hlist_head *head = &dd->hash[DL_HASH_FN(rq_hash_key(rq))];

	if (ON_HASH(drq) && &drq->hash != head->first) {
		hlist_del(&drq->hash);
		hlist_add_head(&drq->hash, head);
	}
}

static struct request *
deadline_find_drq_hash(struct deadline_data *dd, sector_t offset)
{
	struct hlist_head *hash_list = &dd->hash[DL_HASH_FN(offset)];
	struct hlist_node *entry, *next;
	struct deadline_rq *drq;

	hlist_for_each_entry_safe(drq, entry, next, hash_list, hash) {
		struct request *__rq = drq->request;

		BUG_ON(!ON_HASH(drq));

		if (!rq_mergeable(__rq)) {
			__deadline_del_drq_hash(drq);
			continue;
		}

		if (rq_hash_key(__rq) == offset)
			return __rq;
	}

	return NULL;
}

/*
 * rb tree support functions
 */
#define RB_EMPTY(root)	((root)->rb_node == NULL)
#define ON_RB(node)	(rb_parent(node) != node)
#define RB_CLEAR(node)	(rb_set_parent(node, node))
#define rb_entry_drq(node)	rb_entry((node), struct deadline_rq, rb_node)
#define DRQ_RB_ROOT(dd, drq)	(&(dd)->sort_list[rq_data_dir((drq)->request)])
#define rq_rb_key(rq)		(rq)->sector

static struct deadline_rq *
__deadline_add_drq_rb(struct deadline_data *dd, struct deadline_rq *drq)
{
	struct rb_node **p = &DRQ_RB_ROOT(dd, drq)->rb_node;
	struct rb_node *parent = NULL;
	struct deadline_rq *__drq;

	while (*p) {
		parent = *p;
		__drq = rb_entry_drq(parent);

		if (drq->rb_key < __drq->rb_key)
			p = &(*p)->rb_left;
		else if (drq->rb_key > __drq->rb_key)
			p = &(*p)->rb_right;
		else
			return __drq;
	}

	rb_link_node(&drq->rb_node, parent, p);
	return NULL;
}

static void
deadline_add_drq_rb(struct deadline_data *dd, struct deadline_rq *drq)
{
	struct deadline_rq *__alias;

	drq->rb_key = rq_rb_key(drq->request);

retry:
	__alias = __deadline_add_drq_rb(dd, drq);
	if (!__alias) {
		rb_insert_color(&drq->rb_node, DRQ_RB_ROOT(dd, drq));
		return;
	}

	deadline_move_request(dd, __alias);
	goto retry;
}

static inline void
deadline_del_drq_rb(struct deadline_data *dd, struct deadline_rq *drq)
{
	const int data_dir = rq_data_dir(drq->request);

	if (dd->next_drq[data_dir] == drq) {
		struct rb_node *rbnext = rb_next(&drq->rb_node);

		dd->next_drq[data_dir] = NULL;
		if (rbnext)
			dd->next_drq[data_dir] = rb_entry_drq(rbnext);
	}

	BUG_ON(!ON_RB(&drq->rb_node));
	rb_erase(&drq->rb_node, DRQ_RB_ROOT(dd, drq));
	RB_CLEAR(&drq->rb_node);
}

static struct request *
deadline_find_drq_rb(struct deadline_data *dd, sector_t sector, int data_dir)
{
	struct rb_node *n = dd->sort_list[data_dir].rb_node;
	struct deadline_rq *drq;

	while (n) {
		drq = rb_entry_drq(n);

		if (sector < drq->rb_key)
			n = n->rb_left;
		else if (sector > drq->rb_key)
			n = n->rb_right;
		else
			return drq->request;
	}

	return NULL;
}

/*
 * deadline_find_first_drq finds the first (lowest sector numbered) request
 * for the specified data_dir. Used to sweep back to the start of the disk
 * (1-way elevator) after we process the last (highest sector) request.
 */
static struct deadline_rq *
deadline_find_first_drq(struct deadline_data *dd, int data_dir)
{
	struct rb_node *n = dd->sort_list[data_dir].rb_node;

	for (;;) {
		if (n->rb_left == NULL)
			return rb_entry_drq(n);
		
		n = n->rb_left;
	}
}

/*
 * add drq to rbtree and fifo
 */
static void
deadline_add_request(struct request_queue *q, struct request *rq)
{
	struct deadline_data *dd = q->elevator->elevator_data;
	struct deadline_rq *drq = RQ_DATA(rq);

	const int data_dir = rq_data_dir(drq->request);

	deadline_add_drq_rb(dd, drq);
	/*
	 * set expire time (only used for reads) and add to fifo list
	 */
	drq->expires = jiffies + dd->fifo_expire[data_dir];
	list_add_tail(&drq->fifo, &dd->fifo_list[data_dir]);

	if (rq_mergeable(rq))
		deadline_add_drq_hash(dd, drq);
}

/*
 * remove rq from rbtree, fifo, and hash
 */
static void deadline_remove_request(request_queue_t *q, struct request *rq)
{
	struct deadline_rq *drq = RQ_DATA(rq);
	struct deadline_data *dd = q->elevator->elevator_data;

	list_del_init(&drq->fifo);
	deadline_del_drq_rb(dd, drq);
	deadline_del_drq_hash(drq);
}

static int
deadline_merge(request_queue_t *q, struct request **req, struct bio *bio)
{
	struct deadline_data *dd = q->elevator->elevator_data;
	struct request *__rq;
	int ret;

	/*
	 * see if the merge hash can satisfy a back merge
	 */
	__rq = deadline_find_drq_hash(dd, bio->bi_sector);
	if (__rq) {
		BUG_ON(__rq->sector + __rq->nr_sectors != bio->bi_sector);

		if (elv_rq_merge_ok(__rq, bio)) {
			ret = ELEVATOR_BACK_MERGE;
			goto out;
		}
	}

	/*
	 * check for front merge
	 */
	if (dd->front_merges) {
		sector_t rb_key = bio->bi_sector + bio_sectors(bio);

		__rq = deadline_find_drq_rb(dd, rb_key, bio_data_dir(bio));
		if (__rq) {
			BUG_ON(rb_key != rq_rb_key(__rq));

			if (elv_rq_merge_ok(__rq, bio)) {
				ret = ELEVATOR_FRONT_MERGE;
				goto out;
			}
		}
	}

	return ELEVATOR_NO_MERGE;
out:
	if (ret)
		deadline_hot_drq_hash(dd, RQ_DATA(__rq));
	*req = __rq;
	return ret;
}

static void deadline_merged_request(request_queue_t *q, struct request *req)
{
	struct deadline_data *dd = q->elevator->elevator_data;
	struct deadline_rq *drq = RQ_DATA(req);

	/*
	 * hash always needs to be repositioned, key is end sector
	 */
	deadline_del_drq_hash(drq);
	deadline_add_drq_hash(dd, drq);

	/*
	 * if the merge was a front merge, we need to reposition request
	 */
	if (rq_rb_key(req) != drq->rb_key) {
		deadline_del_drq_rb(dd, drq);
		deadline_add_drq_rb(dd, drq);
	}
}

static void
deadline_merged_requests(request_queue_t *q, struct request *req,
			 struct request *next)
{
	struct deadline_data *dd = q->elevator->elevator_data;
	struct deadline_rq *drq = RQ_DATA(req);
	struct deadline_rq *dnext = RQ_DATA(next);

	BUG_ON(!drq);
	BUG_ON(!dnext);

	/*
	 * reposition drq (this is the merged request) in hash, and in rbtree
	 * in case of a front merge
	 */
	deadline_del_drq_hash(drq);
	deadline_add_drq_hash(dd, drq);

	if (rq_rb_key(req) != drq->rb_key) {
		deadline_del_drq_rb(dd, drq);
		deadline_add_drq_rb(dd, drq);
	}

	/*
	 * if dnext expires before drq, assign its expire time to drq
	 * and move into dnext position (dnext will be deleted) in fifo
	 */
	if (!list_empty(&drq->fifo) && !list_empty(&dnext->fifo)) {
		if (time_before(dnext->expires, drq->expires)) {
			list_move(&drq->fifo, &dnext->fifo);
			drq->expires = dnext->expires;
		}
	}

	/*
	 * kill knowledge of next, this one is a goner
	 */
	deadline_remove_request(q, next);
}

/*
 * move request from sort list to dispatch queue.
 */
static inline void
deadline_move_to_dispatch(struct deadline_data *dd, struct deadline_rq *drq)
{
	request_queue_t *q = drq->request->q;

	deadline_remove_request(q, drq->request);
	elv_dispatch_add_tail(q, drq->request);
}

/*
 * move an entry to dispatch queue
 */
static void
deadline_move_request(struct deadline_data *dd, struct deadline_rq *drq)
{
	const int data_dir = rq_data_dir(drq->request);
	struct rb_node *rbnext = rb_next(&drq->rb_node);

	dd->next_drq[READ] = NULL;
	dd->next_drq[WRITE] = NULL;

	if (rbnext)
		dd->next_drq[data_dir] = rb_entry_drq(rbnext);
	
	dd->last_sector = drq->request->sector + drq->request->nr_sectors;

	/*
	 * take it off the sort and fifo list, move
	 * to dispatch queue
	 */
	deadline_move_to_dispatch(dd, drq);
}

#define list_entry_fifo(ptr)	list_entry((ptr), struct deadline_rq, fifo)

/*
 * deadline_check_fifo returns 0 if there are no expired reads on the fifo,
 * 1 otherwise. Requires !list_empty(&dd->fifo_list[data_dir])
 */
static inline int deadline_check_fifo(struct deadline_data *dd, int ddir)
{
	struct deadline_rq *drq = list_entry_fifo(dd->fifo_list[ddir].next);

	/*
	 * drq is expired!
	 */
	if (time_after(jiffies, drq->expires))
		return 1;

	return 0;
}

/*
 * deadline_dispatch_requests selects the best request according to
 * read/write expire, fifo_batch, etc
 */
static int deadline_dispatch_requests(request_queue_t *q, int force)
{
	struct deadline_data *dd = q->elevator->elevator_data;
	const int reads = !list_empty(&dd->fifo_list[READ]);
	const int writes = !list_empty(&dd->fifo_list[WRITE]);
	struct deadline_rq *drq;
	int data_dir;

	/*
	 * batches are currently reads XOR writes
	 */
	if (dd->next_drq[WRITE])
		drq = dd->next_drq[WRITE];
	else
		drq = dd->next_drq[READ];

	if (drq) {
		/* we have a "next request" */
		
		if (dd->last_sector != drq->request->sector)
			/* end the batch on a non sequential request */
			dd->batching += dd->fifo_batch;
		
		if (dd->batching < dd->fifo_batch)
			/* we are still entitled to batch */
			goto dispatch_request;
	}

	/*
	 * at this point we are not running a batch. select the appropriate
	 * data direction (read / write)
	 */

	if (reads) {
		BUG_ON(RB_EMPTY(&dd->sort_list[READ]));

		if (writes && (dd->starved++ >= dd->writes_starved))
			goto dispatch_writes;

		data_dir = READ;

		goto dispatch_find_request;
	}

	/*
	 * there are either no reads or writes have been starved
	 */

	if (writes) {
dispatch_writes:
		BUG_ON(RB_EMPTY(&dd->sort_list[WRITE]));

		dd->starved = 0;

		data_dir = WRITE;

		goto dispatch_find_request;
	}

	return 0;

dispatch_find_request:
	/*
	 * we are not running a batch, find best request for selected data_dir
	 */
	if (deadline_check_fifo(dd, data_dir)) {
		/* An expired request exists - satisfy it */
		dd->batching = 0;
		drq = list_entry_fifo(dd->fifo_list[data_dir].next);
		
	} else if (dd->next_drq[data_dir]) {
		/*
		 * The last req was the same dir and we have a next request in
		 * sort order. No expired requests so continue on from here.
		 */
		drq = dd->next_drq[data_dir];
	} else {
		/*
		 * The last req was the other direction or we have run out of
		 * higher-sectored requests. Go back to the lowest sectored
		 * request (1 way elevator) and start a new batch.
		 */
		dd->batching = 0;
		drq = deadline_find_first_drq(dd, data_dir);
	}

dispatch_request:
	/*
	 * drq is the selected appropriate request.
	 */
	dd->batching++;
	deadline_move_request(dd, drq);

	return 1;
}

static int deadline_queue_empty(request_queue_t *q)
{
	struct deadline_data *dd = q->elevator->elevator_data;

	return list_empty(&dd->fifo_list[WRITE])
		&& list_empty(&dd->fifo_list[READ]);
}

static struct request *
deadline_former_request(request_queue_t *q, struct request *rq)
{
	struct deadline_rq *drq = RQ_DATA(rq);
	struct rb_node *rbprev = rb_prev(&drq->rb_node);

	if (rbprev)
		return rb_entry_drq(rbprev)->request;

	return NULL;
}

static struct request *
deadline_latter_request(request_queue_t *q, struct request *rq)
{
	struct deadline_rq *drq = RQ_DATA(rq);
	struct rb_node *rbnext = rb_next(&drq->rb_node);

	if (rbnext)
		return rb_entry_drq(rbnext)->request;

	return NULL;
}

static void deadline_exit_queue(elevator_t *e)
{
	struct deadline_data *dd = e->elevator_data;

	BUG_ON(!list_empty(&dd->fifo_list[READ]));
	BUG_ON(!list_empty(&dd->fifo_list[WRITE]));

	mempool_destroy(dd->drq_pool);
	kfree(dd->hash);
	kfree(dd);
}

/*
 * initialize elevator private data (deadline_data), and alloc a drq for
 * each request on the free lists
 */
static void *deadline_init_queue(request_queue_t *q, elevator_t *e)
{
	struct deadline_data *dd;
	int i;

	if (!drq_pool)
		return NULL;

	dd = kmalloc_node(sizeof(*dd), GFP_KERNEL, q->node);
	if (!dd)
		return NULL;
	memset(dd, 0, sizeof(*dd));

	dd->hash = kmalloc_node(sizeof(struct hlist_head)*DL_HASH_ENTRIES,
				GFP_KERNEL, q->node);
	if (!dd->hash) {
		kfree(dd);
		return NULL;
	}

	dd->drq_pool = mempool_create_node(BLKDEV_MIN_RQ, mempool_alloc_slab,
					mempool_free_slab, drq_pool, q->node);
	if (!dd->drq_pool) {
		kfree(dd->hash);
		kfree(dd);
		return NULL;
	}

	for (i = 0; i < DL_HASH_ENTRIES; i++)
		INIT_HLIST_HEAD(&dd->hash[i]);

	INIT_LIST_HEAD(&dd->fifo_list[READ]);
	INIT_LIST_HEAD(&dd->fifo_list[WRITE]);
	dd->sort_list[READ] = RB_ROOT;
	dd->sort_list[WRITE] = RB_ROOT;
	dd->fifo_expire[READ] = read_expire;
	dd->fifo_expire[WRITE] = write_expire;
	dd->writes_starved = writes_starved;
	dd->front_merges = 1;
	dd->fifo_batch = fifo_batch;
	return dd;
}

static void deadline_put_request(request_queue_t *q, struct request *rq)
{
	struct deadline_data *dd = q->elevator->elevator_data;
	struct deadline_rq *drq = RQ_DATA(rq);

	mempool_free(drq, dd->drq_pool);
	rq->elevator_private = NULL;
}

static int
deadline_set_request(request_queue_t *q, struct request *rq, struct bio *bio,
		     gfp_t gfp_mask)
{
	struct deadline_data *dd = q->elevator->elevator_data;
	struct deadline_rq *drq;

	drq = mempool_alloc(dd->drq_pool, gfp_mask);
	if (drq) {
		memset(drq, 0, sizeof(*drq));
		RB_CLEAR(&drq->rb_node);
		drq->request = rq;

		INIT_HLIST_NODE(&drq->hash);

		INIT_LIST_HEAD(&drq->fifo);

		rq->elevator_private = drq;
		return 0;
	}

	return 1;
}

/*
 * sysfs parts below
 */

static ssize_t
deadline_var_show(int var, char *page)
{
	return sprintf(page, "%d\n", var);
}

static ssize_t
deadline_var_store(int *var, const char *page, size_t count)
{
	char *p = (char *) page;

	*var = simple_strtol(p, &p, 10);
	return count;
}

#define SHOW_FUNCTION(__FUNC, __VAR, __CONV)				\
static ssize_t __FUNC(elevator_t *e, char *page)			\
{									\
	struct deadline_data *dd = e->elevator_data;			\
	int __data = __VAR;						\
	if (__CONV)							\
		__data = jiffies_to_msecs(__data);			\
	return deadline_var_show(__data, (page));			\
}
SHOW_FUNCTION(deadline_read_expire_show, dd->fifo_expire[READ], 1);
SHOW_FUNCTION(deadline_write_expire_show, dd->fifo_expire[WRITE], 1);
SHOW_FUNCTION(deadline_writes_starved_show, dd->writes_starved, 0);
SHOW_FUNCTION(deadline_front_merges_show, dd->front_merges, 0);
SHOW_FUNCTION(deadline_fifo_batch_show, dd->fifo_batch, 0);
#undef SHOW_FUNCTION

#define STORE_FUNCTION(__FUNC, __PTR, MIN, MAX, __CONV)			\
static ssize_t __FUNC(elevator_t *e, const char *page, size_t count)	\
{									\
	struct deadline_data *dd = e->elevator_data;			\
	int __data;							\
	int ret = deadline_var_store(&__data, (page), count);		\
	if (__data < (MIN))						\
		__data = (MIN);						\
	else if (__data > (MAX))					\
		__data = (MAX);						\
	if (__CONV)							\
		*(__PTR) = msecs_to_jiffies(__data);			\
	else								\
		*(__PTR) = __data;					\
	return ret;							\
}
STORE_FUNCTION(deadline_read_expire_store, &dd->fifo_expire[READ], 0, INT_MAX, 1);
STORE_FUNCTION(deadline_write_expire_store, &dd->fifo_expire[WRITE], 0, INT_MAX, 1);
STORE_FUNCTION(deadline_writes_starved_store, &dd->writes_starved, INT_MIN, INT_MAX, 0);
STORE_FUNCTION(deadline_front_merges_store, &dd->front_merges, 0, 1, 0);
STORE_FUNCTION(deadline_fifo_batch_store, &dd->fifo_batch, 0, INT_MAX, 0);
#undef STORE_FUNCTION

#define DD_ATTR(name) \
	__ATTR(name, S_IRUGO|S_IWUSR, deadline_##name##_show, \
				      deadline_##name##_store)

static struct elv_fs_entry deadline_attrs[] = {
	DD_ATTR(read_expire),
	DD_ATTR(write_expire),
	DD_ATTR(writes_starved),
	DD_ATTR(front_merges),
	DD_ATTR(fifo_batch),
	__ATTR_NULL
};

static struct elevator_type iosched_deadline = {
	.ops = {
		.elevator_merge_fn = 		deadline_merge,
		.elevator_merged_fn =		deadline_merged_request,
		.elevator_merge_req_fn =	deadline_merged_requests,
		.elevator_dispatch_fn =		deadline_dispatch_requests,
		.elevator_add_req_fn =		deadline_add_request,
		.elevator_queue_empty_fn =	deadline_queue_empty,
		.elevator_former_req_fn =	deadline_former_request,
		.elevator_latter_req_fn =	deadline_latter_request,
		.elevator_set_req_fn =		deadline_set_request,
		.elevator_put_req_fn = 		deadline_put_request,
		.elevator_init_fn =		deadline_init_queue,
		.elevator_exit_fn =		deadline_exit_queue,
	},

	.elevator_attrs = deadline_attrs,
	.elevator_name = "deadline",
	.elevator_owner = THIS_MODULE,
};

static int __init deadline_init(void)
{
	int ret;

	drq_pool = kmem_cache_create("deadline_drq", sizeof(struct deadline_rq),
				     0, 0, NULL, NULL);

	if (!drq_pool)
		return -ENOMEM;

	ret = elv_register(&iosched_deadline);
	if (ret)
		kmem_cache_destroy(drq_pool);

	return ret;
}

static void __exit deadline_exit(void)
{
	kmem_cache_destroy(drq_pool);
	elv_unregister(&iosched_deadline);
}

module_init(deadline_init);
module_exit(deadline_exit);

MODULE_AUTHOR("Jens Axboe");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("deadline IO scheduler");
