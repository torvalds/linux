/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BLK_INTERNAL_H
#define BLK_INTERNAL_H

#include <linux/idr.h>
#include <linux/blk-mq.h>
#include <xen/xen.h>
#include "blk-mq.h"
#include "blk-mq-sched.h"

/* Max future timer expiry for timeouts */
#define BLK_MAX_TIMEOUT		(5 * HZ)

#ifdef CONFIG_DEBUG_FS
extern struct dentry *blk_debugfs_root;
#endif

struct blk_flush_queue {
	unsigned int		flush_queue_delayed:1;
	unsigned int		flush_pending_idx:1;
	unsigned int		flush_running_idx:1;
	blk_status_t 		rq_status;
	unsigned long		flush_pending_since;
	struct list_head	flush_queue[2];
	struct list_head	flush_data_in_flight;
	struct request		*flush_rq;

	/*
	 * flush_rq shares tag with this rq, both can't be active
	 * at the same time
	 */
	struct request		*orig_rq;
	spinlock_t		mq_flush_lock;
};

extern struct kmem_cache *blk_requestq_cachep;
extern struct kobj_type blk_queue_ktype;
extern struct ida blk_queue_ida;

static inline struct blk_flush_queue *
blk_get_flush_queue(struct request_queue *q, struct blk_mq_ctx *ctx)
{
	return blk_mq_map_queue(q, REQ_OP_FLUSH, ctx)->fq;
}

static inline void __blk_get_queue(struct request_queue *q)
{
	kobject_get(&q->kobj);
}

static inline bool
is_flush_rq(struct request *req, struct blk_mq_hw_ctx *hctx)
{
	return hctx->fq->flush_rq == req;
}

struct blk_flush_queue *blk_alloc_flush_queue(struct request_queue *q,
		int node, int cmd_size, gfp_t flags);
void blk_free_flush_queue(struct blk_flush_queue *q);

void blk_freeze_queue(struct request_queue *q);

static inline void blk_queue_enter_live(struct request_queue *q)
{
	/*
	 * Given that running in generic_make_request() context
	 * guarantees that a live reference against q_usage_counter has
	 * been established, further references under that same context
	 * need not check that the queue has been frozen (marked dead).
	 */
	percpu_ref_get(&q->q_usage_counter);
}

static inline bool biovec_phys_mergeable(struct request_queue *q,
		struct bio_vec *vec1, struct bio_vec *vec2)
{
	unsigned long mask = queue_segment_boundary(q);
	phys_addr_t addr1 = page_to_phys(vec1->bv_page) + vec1->bv_offset;
	phys_addr_t addr2 = page_to_phys(vec2->bv_page) + vec2->bv_offset;

	if (addr1 + vec1->bv_len != addr2)
		return false;
	if (xen_domain() && !xen_biovec_phys_mergeable(vec1, vec2->bv_page))
		return false;
	if ((addr1 | mask) != ((addr2 + vec2->bv_len - 1) | mask))
		return false;
	return true;
}

static inline bool __bvec_gap_to_prev(struct request_queue *q,
		struct bio_vec *bprv, unsigned int offset)
{
	return (offset & queue_virt_boundary(q)) ||
		((bprv->bv_offset + bprv->bv_len) & queue_virt_boundary(q));
}

/*
 * Check if adding a bio_vec after bprv with offset would create a gap in
 * the SG list. Most drivers don't care about this, but some do.
 */
static inline bool bvec_gap_to_prev(struct request_queue *q,
		struct bio_vec *bprv, unsigned int offset)
{
	if (!queue_virt_boundary(q))
		return false;
	return __bvec_gap_to_prev(q, bprv, offset);
}

static inline void blk_rq_bio_prep(struct request *rq, struct bio *bio,
		unsigned int nr_segs)
{
	rq->nr_phys_segments = nr_segs;
	rq->__data_len = bio->bi_iter.bi_size;
	rq->bio = rq->biotail = bio;
	rq->ioprio = bio_prio(bio);

	if (bio->bi_disk)
		rq->rq_disk = bio->bi_disk;
}

#ifdef CONFIG_BLK_DEV_INTEGRITY
void blk_flush_integrity(void);
bool __bio_integrity_endio(struct bio *);
static inline bool bio_integrity_endio(struct bio *bio)
{
	if (bio_integrity(bio))
		return __bio_integrity_endio(bio);
	return true;
}

static inline bool integrity_req_gap_back_merge(struct request *req,
		struct bio *next)
{
	struct bio_integrity_payload *bip = bio_integrity(req->bio);
	struct bio_integrity_payload *bip_next = bio_integrity(next);

	return bvec_gap_to_prev(req->q, &bip->bip_vec[bip->bip_vcnt - 1],
				bip_next->bip_vec[0].bv_offset);
}

static inline bool integrity_req_gap_front_merge(struct request *req,
		struct bio *bio)
{
	struct bio_integrity_payload *bip = bio_integrity(bio);
	struct bio_integrity_payload *bip_next = bio_integrity(req->bio);

	return bvec_gap_to_prev(req->q, &bip->bip_vec[bip->bip_vcnt - 1],
				bip_next->bip_vec[0].bv_offset);
}
#else /* CONFIG_BLK_DEV_INTEGRITY */
static inline bool integrity_req_gap_back_merge(struct request *req,
		struct bio *next)
{
	return false;
}
static inline bool integrity_req_gap_front_merge(struct request *req,
		struct bio *bio)
{
	return false;
}

static inline void blk_flush_integrity(void)
{
}
static inline bool bio_integrity_endio(struct bio *bio)
{
	return true;
}
#endif /* CONFIG_BLK_DEV_INTEGRITY */

unsigned long blk_rq_timeout(unsigned long timeout);
void blk_add_timer(struct request *req);

bool bio_attempt_front_merge(struct request *req, struct bio *bio,
		unsigned int nr_segs);
bool bio_attempt_back_merge(struct request *req, struct bio *bio,
		unsigned int nr_segs);
bool bio_attempt_discard_merge(struct request_queue *q, struct request *req,
		struct bio *bio);
bool blk_attempt_plug_merge(struct request_queue *q, struct bio *bio,
		unsigned int nr_segs, struct request **same_queue_rq);

void blk_account_io_start(struct request *req, bool new_io);
void blk_account_io_completion(struct request *req, unsigned int bytes);
void blk_account_io_done(struct request *req, u64 now);

/*
 * Internal elevator interface
 */
#define ELV_ON_HASH(rq) ((rq)->rq_flags & RQF_HASHED)

void blk_insert_flush(struct request *rq);

void elevator_init_mq(struct request_queue *q);
int elevator_switch_mq(struct request_queue *q,
			      struct elevator_type *new_e);
void __elevator_exit(struct request_queue *, struct elevator_queue *);
int elv_register_queue(struct request_queue *q, bool uevent);
void elv_unregister_queue(struct request_queue *q);

static inline void elevator_exit(struct request_queue *q,
		struct elevator_queue *e)
{
	lockdep_assert_held(&q->sysfs_lock);

	blk_mq_sched_free_requests(q);
	__elevator_exit(q, e);
}

struct hd_struct *__disk_get_part(struct gendisk *disk, int partno);

#ifdef CONFIG_FAIL_IO_TIMEOUT
int blk_should_fake_timeout(struct request_queue *);
ssize_t part_timeout_show(struct device *, struct device_attribute *, char *);
ssize_t part_timeout_store(struct device *, struct device_attribute *,
				const char *, size_t);
#else
static inline int blk_should_fake_timeout(struct request_queue *q)
{
	return 0;
}
#endif

void __blk_queue_split(struct request_queue *q, struct bio **bio,
		unsigned int *nr_segs);
int ll_back_merge_fn(struct request *req, struct bio *bio,
		unsigned int nr_segs);
int ll_front_merge_fn(struct request *req,  struct bio *bio,
		unsigned int nr_segs);
struct request *attempt_back_merge(struct request_queue *q, struct request *rq);
struct request *attempt_front_merge(struct request_queue *q, struct request *rq);
int blk_attempt_req_merge(struct request_queue *q, struct request *rq,
				struct request *next);
unsigned int blk_recalc_rq_segments(struct request *rq);
void blk_rq_set_mixed_merge(struct request *rq);
bool blk_rq_merge_ok(struct request *rq, struct bio *bio);
enum elv_merge blk_try_merge(struct request *rq, struct bio *bio);

int blk_dev_init(void);

/*
 * Contribute to IO statistics IFF:
 *
 *	a) it's attached to a gendisk, and
 *	b) the queue had IO stats enabled when this request was started, and
 *	c) it's a file system request
 */
static inline bool blk_do_io_stat(struct request *rq)
{
	return rq->rq_disk &&
	       (rq->rq_flags & RQF_IO_STAT) &&
		!blk_rq_is_passthrough(rq);
}

static inline void req_set_nomerge(struct request_queue *q, struct request *req)
{
	req->cmd_flags |= REQ_NOMERGE;
	if (req == q->last_merge)
		q->last_merge = NULL;
}

/*
 * The max size one bio can handle is UINT_MAX becasue bvec_iter.bi_size
 * is defined as 'unsigned int', meantime it has to aligned to with logical
 * block size which is the minimum accepted unit by hardware.
 */
static inline unsigned int bio_allowed_max_sectors(struct request_queue *q)
{
	return round_down(UINT_MAX, queue_logical_block_size(q)) >> 9;
}

/*
 * Internal io_context interface
 */
void get_io_context(struct io_context *ioc);
struct io_cq *ioc_lookup_icq(struct io_context *ioc, struct request_queue *q);
struct io_cq *ioc_create_icq(struct io_context *ioc, struct request_queue *q,
			     gfp_t gfp_mask);
void ioc_clear_queue(struct request_queue *q);

int create_task_io_context(struct task_struct *task, gfp_t gfp_mask, int node);

/**
 * create_io_context - try to create task->io_context
 * @gfp_mask: allocation mask
 * @node: allocation node
 *
 * If %current->io_context is %NULL, allocate a new io_context and install
 * it.  Returns the current %current->io_context which may be %NULL if
 * allocation failed.
 *
 * Note that this function can't be called with IRQ disabled because
 * task_lock which protects %current->io_context is IRQ-unsafe.
 */
static inline struct io_context *create_io_context(gfp_t gfp_mask, int node)
{
	WARN_ON_ONCE(irqs_disabled());
	if (unlikely(!current->io_context))
		create_task_io_context(current, gfp_mask, node);
	return current->io_context;
}

/*
 * Internal throttling interface
 */
#ifdef CONFIG_BLK_DEV_THROTTLING
extern void blk_throtl_drain(struct request_queue *q);
extern int blk_throtl_init(struct request_queue *q);
extern void blk_throtl_exit(struct request_queue *q);
extern void blk_throtl_register_queue(struct request_queue *q);
#else /* CONFIG_BLK_DEV_THROTTLING */
static inline void blk_throtl_drain(struct request_queue *q) { }
static inline int blk_throtl_init(struct request_queue *q) { return 0; }
static inline void blk_throtl_exit(struct request_queue *q) { }
static inline void blk_throtl_register_queue(struct request_queue *q) { }
#endif /* CONFIG_BLK_DEV_THROTTLING */
#ifdef CONFIG_BLK_DEV_THROTTLING_LOW
extern ssize_t blk_throtl_sample_time_show(struct request_queue *q, char *page);
extern ssize_t blk_throtl_sample_time_store(struct request_queue *q,
	const char *page, size_t count);
extern void blk_throtl_bio_endio(struct bio *bio);
extern void blk_throtl_stat_add(struct request *rq, u64 time);
#else
static inline void blk_throtl_bio_endio(struct bio *bio) { }
static inline void blk_throtl_stat_add(struct request *rq, u64 time) { }
#endif

#ifdef CONFIG_BOUNCE
extern int init_emergency_isa_pool(void);
extern void blk_queue_bounce(struct request_queue *q, struct bio **bio);
#else
static inline int init_emergency_isa_pool(void)
{
	return 0;
}
static inline void blk_queue_bounce(struct request_queue *q, struct bio **bio)
{
}
#endif /* CONFIG_BOUNCE */

#ifdef CONFIG_BLK_CGROUP_IOLATENCY
extern int blk_iolatency_init(struct request_queue *q);
#else
static inline int blk_iolatency_init(struct request_queue *q) { return 0; }
#endif

struct bio *blk_next_bio(struct bio *bio, unsigned int nr_pages, gfp_t gfp);

#ifdef CONFIG_BLK_DEV_ZONED
void blk_queue_free_zone_bitmaps(struct request_queue *q);
#else
static inline void blk_queue_free_zone_bitmaps(struct request_queue *q) {}
#endif

#endif /* BLK_INTERNAL_H */
