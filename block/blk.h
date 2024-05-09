/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BLK_INTERNAL_H
#define BLK_INTERNAL_H

#include <linux/blk-crypto.h>
#include <linux/memblock.h>	/* for max_pfn/max_low_pfn */
#include <linux/sched/sysctl.h>
#include <linux/timekeeping.h>
#include <xen/xen.h>
#include "blk-crypto-internal.h"

struct elevator_type;

/* Max future timer expiry for timeouts */
#define BLK_MAX_TIMEOUT		(5 * HZ)

extern struct dentry *blk_debugfs_root;

struct blk_flush_queue {
	spinlock_t		mq_flush_lock;
	unsigned int		flush_pending_idx:1;
	unsigned int		flush_running_idx:1;
	blk_status_t 		rq_status;
	unsigned long		flush_pending_since;
	struct list_head	flush_queue[2];
	unsigned long		flush_data_in_flight;
	struct request		*flush_rq;
};

bool is_flush_rq(struct request *req);

struct blk_flush_queue *blk_alloc_flush_queue(int node, int cmd_size,
					      gfp_t flags);
void blk_free_flush_queue(struct blk_flush_queue *q);

void blk_freeze_queue(struct request_queue *q);
void __blk_mq_unfreeze_queue(struct request_queue *q, bool force_atomic);
void blk_queue_start_drain(struct request_queue *q);
int __bio_queue_enter(struct request_queue *q, struct bio *bio);
void submit_bio_noacct_nocheck(struct bio *bio);

static inline bool blk_try_enter_queue(struct request_queue *q, bool pm)
{
	rcu_read_lock();
	if (!percpu_ref_tryget_live_rcu(&q->q_usage_counter))
		goto fail;

	/*
	 * The code that increments the pm_only counter must ensure that the
	 * counter is globally visible before the queue is unfrozen.
	 */
	if (blk_queue_pm_only(q) &&
	    (!pm || queue_rpm_status(q) == RPM_SUSPENDED))
		goto fail_put;

	rcu_read_unlock();
	return true;

fail_put:
	blk_queue_exit(q);
fail:
	rcu_read_unlock();
	return false;
}

static inline int bio_queue_enter(struct bio *bio)
{
	struct request_queue *q = bdev_get_queue(bio->bi_bdev);

	if (blk_try_enter_queue(q, false))
		return 0;
	return __bio_queue_enter(q, bio);
}

static inline void blk_wait_io(struct completion *done)
{
	/* Prevent hang_check timer from firing at us during very long I/O */
	unsigned long timeout = sysctl_hung_task_timeout_secs * HZ / 2;

	if (timeout)
		while (!wait_for_completion_io_timeout(done, timeout))
			;
	else
		wait_for_completion_io(done);
}

#define BIO_INLINE_VECS 4
struct bio_vec *bvec_alloc(mempool_t *pool, unsigned short *nr_vecs,
		gfp_t gfp_mask);
void bvec_free(mempool_t *pool, struct bio_vec *bv, unsigned short nr_vecs);

bool bvec_try_merge_hw_page(struct request_queue *q, struct bio_vec *bv,
		struct page *page, unsigned len, unsigned offset,
		bool *same_page);

static inline bool biovec_phys_mergeable(struct request_queue *q,
		struct bio_vec *vec1, struct bio_vec *vec2)
{
	unsigned long mask = queue_segment_boundary(q);
	phys_addr_t addr1 = page_to_phys(vec1->bv_page) + vec1->bv_offset;
	phys_addr_t addr2 = page_to_phys(vec2->bv_page) + vec2->bv_offset;

	/*
	 * Merging adjacent physical pages may not work correctly under KMSAN
	 * if their metadata pages aren't adjacent. Just disable merging.
	 */
	if (IS_ENABLED(CONFIG_KMSAN))
		return false;

	if (addr1 + vec1->bv_len != addr2)
		return false;
	if (xen_domain() && !xen_biovec_phys_mergeable(vec1, vec2->bv_page))
		return false;
	if ((addr1 | mask) != ((addr2 + vec2->bv_len - 1) | mask))
		return false;
	return true;
}

static inline bool __bvec_gap_to_prev(const struct queue_limits *lim,
		struct bio_vec *bprv, unsigned int offset)
{
	return (offset & lim->virt_boundary_mask) ||
		((bprv->bv_offset + bprv->bv_len) & lim->virt_boundary_mask);
}

/*
 * Check if adding a bio_vec after bprv with offset would create a gap in
 * the SG list. Most drivers don't care about this, but some do.
 */
static inline bool bvec_gap_to_prev(const struct queue_limits *lim,
		struct bio_vec *bprv, unsigned int offset)
{
	if (!lim->virt_boundary_mask)
		return false;
	return __bvec_gap_to_prev(lim, bprv, offset);
}

static inline bool rq_mergeable(struct request *rq)
{
	if (blk_rq_is_passthrough(rq))
		return false;

	if (req_op(rq) == REQ_OP_FLUSH)
		return false;

	if (req_op(rq) == REQ_OP_WRITE_ZEROES)
		return false;

	if (req_op(rq) == REQ_OP_ZONE_APPEND)
		return false;

	if (rq->cmd_flags & REQ_NOMERGE_FLAGS)
		return false;
	if (rq->rq_flags & RQF_NOMERGE_FLAGS)
		return false;

	return true;
}

/*
 * There are two different ways to handle DISCARD merges:
 *  1) If max_discard_segments > 1, the driver treats every bio as a range and
 *     send the bios to controller together. The ranges don't need to be
 *     contiguous.
 *  2) Otherwise, the request will be normal read/write requests.  The ranges
 *     need to be contiguous.
 */
static inline bool blk_discard_mergable(struct request *req)
{
	if (req_op(req) == REQ_OP_DISCARD &&
	    queue_max_discard_segments(req->q) > 1)
		return true;
	return false;
}

static inline unsigned int blk_rq_get_max_segments(struct request *rq)
{
	if (req_op(rq) == REQ_OP_DISCARD)
		return queue_max_discard_segments(rq->q);
	return queue_max_segments(rq->q);
}

static inline unsigned int blk_queue_get_max_sectors(struct request_queue *q,
						     enum req_op op)
{
	if (unlikely(op == REQ_OP_DISCARD || op == REQ_OP_SECURE_ERASE))
		return min(q->limits.max_discard_sectors,
			   UINT_MAX >> SECTOR_SHIFT);

	if (unlikely(op == REQ_OP_WRITE_ZEROES))
		return q->limits.max_write_zeroes_sectors;

	return q->limits.max_sectors;
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

	return bvec_gap_to_prev(&req->q->limits,
				&bip->bip_vec[bip->bip_vcnt - 1],
				bip_next->bip_vec[0].bv_offset);
}

static inline bool integrity_req_gap_front_merge(struct request *req,
		struct bio *bio)
{
	struct bio_integrity_payload *bip = bio_integrity(bio);
	struct bio_integrity_payload *bip_next = bio_integrity(req->bio);

	return bvec_gap_to_prev(&req->q->limits,
				&bip->bip_vec[bip->bip_vcnt - 1],
				bip_next->bip_vec[0].bv_offset);
}

extern const struct attribute_group blk_integrity_attr_group;
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
#endif /* CONFIG_BLK_DEV_INTEGRITY */

unsigned long blk_rq_timeout(unsigned long timeout);
void blk_add_timer(struct request *req);

bool blk_attempt_plug_merge(struct request_queue *q, struct bio *bio,
		unsigned int nr_segs);
bool blk_bio_list_merge(struct request_queue *q, struct list_head *list,
			struct bio *bio, unsigned int nr_segs);

/*
 * Plug flush limits
 */
#define BLK_MAX_REQUEST_COUNT	32
#define BLK_PLUG_FLUSH_SIZE	(128 * 1024)

/*
 * Internal elevator interface
 */
#define ELV_ON_HASH(rq) ((rq)->rq_flags & RQF_HASHED)

bool blk_insert_flush(struct request *rq);

int elevator_switch(struct request_queue *q, struct elevator_type *new_e);
void elevator_disable(struct request_queue *q);
void elevator_exit(struct request_queue *q);
int elv_register_queue(struct request_queue *q, bool uevent);
void elv_unregister_queue(struct request_queue *q);

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

static inline bool bio_may_exceed_limits(struct bio *bio,
					 const struct queue_limits *lim)
{
	switch (bio_op(bio)) {
	case REQ_OP_DISCARD:
	case REQ_OP_SECURE_ERASE:
	case REQ_OP_WRITE_ZEROES:
		return true; /* non-trivial splitting decisions */
	default:
		break;
	}

	/*
	 * All drivers must accept single-segments bios that are <= PAGE_SIZE.
	 * This is a quick and dirty check that relies on the fact that
	 * bi_io_vec[0] is always valid if a bio has data.  The check might
	 * lead to occasional false negatives when bios are cloned, but compared
	 * to the performance impact of cloned bios themselves the loop below
	 * doesn't matter anyway.
	 */
	return lim->chunk_sectors || bio->bi_vcnt != 1 ||
		bio->bi_io_vec->bv_len + bio->bi_io_vec->bv_offset > PAGE_SIZE;
}

struct bio *__bio_split_to_limits(struct bio *bio,
				  const struct queue_limits *lim,
				  unsigned int *nr_segs);
int ll_back_merge_fn(struct request *req, struct bio *bio,
		unsigned int nr_segs);
bool blk_attempt_req_merge(struct request_queue *q, struct request *rq,
				struct request *next);
unsigned int blk_recalc_rq_segments(struct request *rq);
bool blk_rq_merge_ok(struct request *rq, struct bio *bio);
enum elv_merge blk_try_merge(struct request *rq, struct bio *bio);

int blk_set_default_limits(struct queue_limits *lim);
int blk_dev_init(void);

/*
 * Contribute to IO statistics IFF:
 *
 *	a) it's attached to a gendisk, and
 *	b) the queue had IO stats enabled when this request was started
 */
static inline bool blk_do_io_stat(struct request *rq)
{
	return (rq->rq_flags & RQF_IO_STAT) && !blk_rq_is_passthrough(rq);
}

void update_io_ticks(struct block_device *part, unsigned long now, bool end);
unsigned int part_in_flight(struct block_device *part);

static inline void req_set_nomerge(struct request_queue *q, struct request *req)
{
	req->cmd_flags |= REQ_NOMERGE;
	if (req == q->last_merge)
		q->last_merge = NULL;
}

/*
 * Internal io_context interface
 */
struct io_cq *ioc_find_get_icq(struct request_queue *q);
struct io_cq *ioc_lookup_icq(struct request_queue *q);
#ifdef CONFIG_BLK_ICQ
void ioc_clear_queue(struct request_queue *q);
#else
static inline void ioc_clear_queue(struct request_queue *q)
{
}
#endif /* CONFIG_BLK_ICQ */

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

struct bio *__blk_queue_bounce(struct bio *bio, struct request_queue *q);

static inline bool blk_queue_may_bounce(struct request_queue *q)
{
	return IS_ENABLED(CONFIG_BOUNCE) &&
		q->limits.bounce == BLK_BOUNCE_HIGH &&
		max_low_pfn >= max_pfn;
}

static inline struct bio *blk_queue_bounce(struct bio *bio,
		struct request_queue *q)
{
	if (unlikely(blk_queue_may_bounce(q) && bio_has_data(bio)))
		return __blk_queue_bounce(bio, q);
	return bio;
}

#ifdef CONFIG_BLK_DEV_ZONED
void disk_free_zone_bitmaps(struct gendisk *disk);
int blkdev_report_zones_ioctl(struct block_device *bdev, unsigned int cmd,
		unsigned long arg);
int blkdev_zone_mgmt_ioctl(struct block_device *bdev, blk_mode_t mode,
		unsigned int cmd, unsigned long arg);
#else /* CONFIG_BLK_DEV_ZONED */
static inline void disk_free_zone_bitmaps(struct gendisk *disk) {}
static inline int blkdev_report_zones_ioctl(struct block_device *bdev,
		unsigned int cmd, unsigned long arg)
{
	return -ENOTTY;
}
static inline int blkdev_zone_mgmt_ioctl(struct block_device *bdev,
		blk_mode_t mode, unsigned int cmd, unsigned long arg)
{
	return -ENOTTY;
}
#endif /* CONFIG_BLK_DEV_ZONED */

struct block_device *bdev_alloc(struct gendisk *disk, u8 partno);
void bdev_add(struct block_device *bdev, dev_t dev);

int blk_alloc_ext_minor(void);
void blk_free_ext_minor(unsigned int minor);
#define ADDPART_FLAG_NONE	0
#define ADDPART_FLAG_RAID	1
#define ADDPART_FLAG_WHOLEDISK	2
int bdev_add_partition(struct gendisk *disk, int partno, sector_t start,
		sector_t length);
int bdev_del_partition(struct gendisk *disk, int partno);
int bdev_resize_partition(struct gendisk *disk, int partno, sector_t start,
		sector_t length);
void drop_partition(struct block_device *part);

void bdev_set_nr_sectors(struct block_device *bdev, sector_t sectors);

struct gendisk *__alloc_disk_node(struct request_queue *q, int node_id,
		struct lock_class_key *lkclass);

int bio_add_hw_page(struct request_queue *q, struct bio *bio,
		struct page *page, unsigned int len, unsigned int offset,
		unsigned int max_sectors, bool *same_page);

/*
 * Clean up a page appropriately, where the page may be pinned, may have a
 * ref taken on it or neither.
 */
static inline void bio_release_page(struct bio *bio, struct page *page)
{
	if (bio_flagged(bio, BIO_PAGE_PINNED))
		unpin_user_page(page);
}

struct request_queue *blk_alloc_queue(struct queue_limits *lim, int node_id);

int disk_scan_partitions(struct gendisk *disk, blk_mode_t mode);

int disk_alloc_events(struct gendisk *disk);
void disk_add_events(struct gendisk *disk);
void disk_del_events(struct gendisk *disk);
void disk_release_events(struct gendisk *disk);
void disk_block_events(struct gendisk *disk);
void disk_unblock_events(struct gendisk *disk);
void disk_flush_events(struct gendisk *disk, unsigned int mask);
extern struct device_attribute dev_attr_events;
extern struct device_attribute dev_attr_events_async;
extern struct device_attribute dev_attr_events_poll_msecs;

extern struct attribute_group blk_trace_attr_group;

blk_mode_t file_to_blk_mode(struct file *file);
int truncate_bdev_range(struct block_device *bdev, blk_mode_t mode,
		loff_t lstart, loff_t lend);
long blkdev_ioctl(struct file *file, unsigned cmd, unsigned long arg);
long compat_blkdev_ioctl(struct file *file, unsigned cmd, unsigned long arg);

extern const struct address_space_operations def_blk_aops;

int disk_register_independent_access_ranges(struct gendisk *disk);
void disk_unregister_independent_access_ranges(struct gendisk *disk);

#ifdef CONFIG_FAIL_MAKE_REQUEST
bool should_fail_request(struct block_device *part, unsigned int bytes);
#else /* CONFIG_FAIL_MAKE_REQUEST */
static inline bool should_fail_request(struct block_device *part,
					unsigned int bytes)
{
	return false;
}
#endif /* CONFIG_FAIL_MAKE_REQUEST */

/*
 * Optimized request reference counting. Ideally we'd make timeouts be more
 * clever, as that's the only reason we need references at all... But until
 * this happens, this is faster than using refcount_t. Also see:
 *
 * abc54d634334 ("io_uring: switch to atomic_t for io_kiocb reference count")
 */
#define req_ref_zero_or_close_to_overflow(req)	\
	((unsigned int) atomic_read(&(req->ref)) + 127u <= 127u)

static inline bool req_ref_inc_not_zero(struct request *req)
{
	return atomic_inc_not_zero(&req->ref);
}

static inline bool req_ref_put_and_test(struct request *req)
{
	WARN_ON_ONCE(req_ref_zero_or_close_to_overflow(req));
	return atomic_dec_and_test(&req->ref);
}

static inline void req_ref_set(struct request *req, int value)
{
	atomic_set(&req->ref, value);
}

static inline int req_ref_read(struct request *req)
{
	return atomic_read(&req->ref);
}

static inline u64 blk_time_get_ns(void)
{
	struct blk_plug *plug = current->plug;

	if (!plug || !in_task())
		return ktime_get_ns();

	/*
	 * 0 could very well be a valid time, but rather than flag "this is
	 * a valid timestamp" separately, just accept that we'll do an extra
	 * ktime_get_ns() if we just happen to get 0 as the current time.
	 */
	if (!plug->cur_ktime) {
		plug->cur_ktime = ktime_get_ns();
		current->flags |= PF_BLOCK_TS;
	}
	return plug->cur_ktime;
}

static inline ktime_t blk_time_get(void)
{
	return ns_to_ktime(blk_time_get_ns());
}

/*
 * From most significant bit:
 * 1 bit: reserved for other usage, see below
 * 12 bits: original size of bio
 * 51 bits: issue time of bio
 */
#define BIO_ISSUE_RES_BITS      1
#define BIO_ISSUE_SIZE_BITS     12
#define BIO_ISSUE_RES_SHIFT     (64 - BIO_ISSUE_RES_BITS)
#define BIO_ISSUE_SIZE_SHIFT    (BIO_ISSUE_RES_SHIFT - BIO_ISSUE_SIZE_BITS)
#define BIO_ISSUE_TIME_MASK     ((1ULL << BIO_ISSUE_SIZE_SHIFT) - 1)
#define BIO_ISSUE_SIZE_MASK     \
	(((1ULL << BIO_ISSUE_SIZE_BITS) - 1) << BIO_ISSUE_SIZE_SHIFT)
#define BIO_ISSUE_RES_MASK      (~((1ULL << BIO_ISSUE_RES_SHIFT) - 1))

/* Reserved bit for blk-throtl */
#define BIO_ISSUE_THROTL_SKIP_LATENCY (1ULL << 63)

static inline u64 __bio_issue_time(u64 time)
{
	return time & BIO_ISSUE_TIME_MASK;
}

static inline u64 bio_issue_time(struct bio_issue *issue)
{
	return __bio_issue_time(issue->value);
}

static inline sector_t bio_issue_size(struct bio_issue *issue)
{
	return ((issue->value & BIO_ISSUE_SIZE_MASK) >> BIO_ISSUE_SIZE_SHIFT);
}

static inline void bio_issue_init(struct bio_issue *issue,
				       sector_t size)
{
	size &= (1ULL << BIO_ISSUE_SIZE_BITS) - 1;
	issue->value = ((issue->value & BIO_ISSUE_RES_MASK) |
			(blk_time_get_ns() & BIO_ISSUE_TIME_MASK) |
			((u64)size << BIO_ISSUE_SIZE_SHIFT));
}

void bdev_release(struct file *bdev_file);
int bdev_open(struct block_device *bdev, blk_mode_t mode, void *holder,
	      const struct blk_holder_ops *hops, struct file *bdev_file);
int bdev_permission(dev_t dev, blk_mode_t mode, void *holder);

#endif /* BLK_INTERNAL_H */
