/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BLK_INTERNAL_H
#define BLK_INTERNAL_H

#include <linux/idr.h>
#include <linux/blk-mq.h>
#include <linux/part_stat.h>
#include <linux/blk-crypto.h>
#include <linux/memblock.h>	/* for max_pfn/max_low_pfn */
#include <xen/xen.h>
#include "blk-crypto-internal.h"
#include "blk-mq.h"
#include "blk-mq-sched.h"

/* Max future timer expiry for timeouts */
#define BLK_MAX_TIMEOUT		(5 * HZ)

extern struct dentry *blk_debugfs_root;

struct blk_flush_queue {
	unsigned int		flush_pending_idx:1;
	unsigned int		flush_running_idx:1;
	blk_status_t 		rq_status;
	unsigned long		flush_pending_since;
	struct list_head	flush_queue[2];
	struct list_head	flush_data_in_flight;
	struct request		*flush_rq;

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

struct blk_flush_queue *blk_alloc_flush_queue(int node, int cmd_size,
					      gfp_t flags);
void blk_free_flush_queue(struct blk_flush_queue *q);

void blk_freeze_queue(struct request_queue *q);

#define BIO_INLINE_VECS 4
struct bio_vec *bvec_alloc(mempool_t *pool, unsigned short *nr_vecs,
		gfp_t gfp_mask);
void bvec_free(mempool_t *pool, struct bio_vec *bv, unsigned short nr_vecs);

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

#ifdef CONFIG_BLK_DEV_INTEGRITY
void blk_flush_integrity(void);
bool __bio_integrity_endio(struct bio *);
void bio_integrity_free(struct bio *bio);
static inline bool bio_integrity_endio(struct bio *bio)
{
	if (bio_integrity(bio))
		return __bio_integrity_endio(bio);
	return true;
}

bool blk_integrity_merge_rq(struct request_queue *, struct request *,
		struct request *);
bool blk_integrity_merge_bio(struct request_queue *, struct request *,
		struct bio *);

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

void blk_integrity_add(struct gendisk *);
void blk_integrity_del(struct gendisk *);
#else /* CONFIG_BLK_DEV_INTEGRITY */
static inline bool blk_integrity_merge_rq(struct request_queue *rq,
		struct request *r1, struct request *r2)
{
	return true;
}
static inline bool blk_integrity_merge_bio(struct request_queue *rq,
		struct request *r, struct bio *b)
{
	return true;
}
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
static inline void bio_integrity_free(struct bio *bio)
{
}
static inline void blk_integrity_add(struct gendisk *disk)
{
}
static inline void blk_integrity_del(struct gendisk *disk)
{
}
#endif /* CONFIG_BLK_DEV_INTEGRITY */

unsigned long blk_rq_timeout(unsigned long timeout);
void blk_add_timer(struct request *req);

bool blk_attempt_plug_merge(struct request_queue *q, struct bio *bio,
		unsigned int nr_segs, struct request **same_queue_rq);
bool blk_bio_list_merge(struct request_queue *q, struct list_head *list,
			struct bio *bio, unsigned int nr_segs);

void blk_account_io_start(struct request *req);
void blk_account_io_done(struct request *req, u64 now);

/*
 * Internal elevator interface
 */
#define ELV_ON_HASH(rq) ((rq)->rq_flags & RQF_HASHED)

void blk_insert_flush(struct request *rq);

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

ssize_t part_size_show(struct device *dev, struct device_attribute *attr,
		char *buf);
ssize_t part_stat_show(struct device *dev, struct device_attribute *attr,
		char *buf);
ssize_t part_inflight_show(struct device *dev, struct device_attribute *attr,
		char *buf);
ssize_t part_fail_show(struct device *dev, struct device_attribute *attr,
		char *buf);
ssize_t part_fail_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count);
ssize_t part_timeout_show(struct device *, struct device_attribute *, char *);
ssize_t part_timeout_store(struct device *, struct device_attribute *,
				const char *, size_t);

void __blk_queue_split(struct bio **bio, unsigned int *nr_segs);
int ll_back_merge_fn(struct request *req, struct bio *bio,
		unsigned int nr_segs);
bool blk_attempt_req_merge(struct request_queue *q, struct request *rq,
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
 *	b) the queue had IO stats enabled when this request was started
 */
static inline bool blk_do_io_stat(struct request *rq)
{
	return rq->rq_disk && (rq->rq_flags & RQF_IO_STAT);
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
 * The max bio size which is aligned to q->limits.discard_granularity. This
 * is a hint to split large discard bio in generic block layer, then if device
 * driver needs to split the discard bio into smaller ones, their bi_size can
 * be very probably and easily aligned to discard_granularity of the device's
 * queue.
 */
static inline unsigned int bio_aligned_discard_max_sectors(
					struct request_queue *q)
{
	return round_down(UINT_MAX, q->limits.discard_granularity) >>
			SECTOR_SHIFT;
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

/*
 * Internal throttling interface
 */
#ifdef CONFIG_BLK_DEV_THROTTLING
extern int blk_throtl_init(struct request_queue *q);
extern void blk_throtl_exit(struct request_queue *q);
extern void blk_throtl_register_queue(struct request_queue *q);
bool blk_throtl_bio(struct bio *bio);
#else /* CONFIG_BLK_DEV_THROTTLING */
static inline int blk_throtl_init(struct request_queue *q) { return 0; }
static inline void blk_throtl_exit(struct request_queue *q) { }
static inline void blk_throtl_register_queue(struct request_queue *q) { }
static inline bool blk_throtl_bio(struct bio *bio) { return false; }
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

void __blk_queue_bounce(struct request_queue *q, struct bio **bio);

static inline bool blk_queue_may_bounce(struct request_queue *q)
{
	return IS_ENABLED(CONFIG_BOUNCE) &&
		q->limits.bounce == BLK_BOUNCE_HIGH &&
		max_low_pfn >= max_pfn;
}

static inline void blk_queue_bounce(struct request_queue *q, struct bio **bio)
{
	if (unlikely(blk_queue_may_bounce(q) && bio_has_data(*bio)))
		__blk_queue_bounce(q, bio);	
}

#ifdef CONFIG_BLK_CGROUP_IOLATENCY
extern int blk_iolatency_init(struct request_queue *q);
#else
static inline int blk_iolatency_init(struct request_queue *q) { return 0; }
#endif

struct bio *blk_next_bio(struct bio *bio, unsigned int nr_pages, gfp_t gfp);

#ifdef CONFIG_BLK_DEV_ZONED
void blk_queue_free_zone_bitmaps(struct request_queue *q);
void blk_queue_clear_zone_settings(struct request_queue *q);
#else
static inline void blk_queue_free_zone_bitmaps(struct request_queue *q) {}
static inline void blk_queue_clear_zone_settings(struct request_queue *q) {}
#endif

int blk_alloc_ext_minor(void);
void blk_free_ext_minor(unsigned int minor);
char *disk_name(struct gendisk *hd, int partno, char *buf);
#define ADDPART_FLAG_NONE	0
#define ADDPART_FLAG_RAID	1
#define ADDPART_FLAG_WHOLEDISK	2
int bdev_add_partition(struct block_device *bdev, int partno,
		sector_t start, sector_t length);
int bdev_del_partition(struct block_device *bdev, int partno);
int bdev_resize_partition(struct block_device *bdev, int partno,
		sector_t start, sector_t length);

int bio_add_hw_page(struct request_queue *q, struct bio *bio,
		struct page *page, unsigned int len, unsigned int offset,
		unsigned int max_sectors, bool *same_page);

struct request_queue *blk_alloc_queue(int node_id);

void disk_alloc_events(struct gendisk *disk);
void disk_add_events(struct gendisk *disk);
void disk_del_events(struct gendisk *disk);
void disk_release_events(struct gendisk *disk);
extern struct device_attribute dev_attr_events;
extern struct device_attribute dev_attr_events_async;
extern struct device_attribute dev_attr_events_poll_msecs;

#endif /* BLK_INTERNAL_H */
