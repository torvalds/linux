/*
 * Functions related to segment and merge handling
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/scatterlist.h>

#include "blk.h"

void blk_recalc_rq_sectors(struct request *rq, int nsect)
{
	if (blk_fs_request(rq)) {
		rq->hard_sector += nsect;
		rq->hard_nr_sectors -= nsect;

		/*
		 * Move the I/O submission pointers ahead if required.
		 */
		if ((rq->nr_sectors >= rq->hard_nr_sectors) &&
		    (rq->sector <= rq->hard_sector)) {
			rq->sector = rq->hard_sector;
			rq->nr_sectors = rq->hard_nr_sectors;
			rq->hard_cur_sectors = bio_cur_sectors(rq->bio);
			rq->current_nr_sectors = rq->hard_cur_sectors;
			rq->buffer = bio_data(rq->bio);
		}

		/*
		 * if total number of sectors is less than the first segment
		 * size, something has gone terribly wrong
		 */
		if (rq->nr_sectors < rq->current_nr_sectors) {
			printk(KERN_ERR "blk: request botched\n");
			rq->nr_sectors = rq->current_nr_sectors;
		}
	}
}

void blk_recalc_rq_segments(struct request *rq)
{
	int nr_phys_segs;
	int nr_hw_segs;
	unsigned int phys_size;
	unsigned int hw_size;
	struct bio_vec *bv, *bvprv = NULL;
	int seg_size;
	int hw_seg_size;
	int cluster;
	struct req_iterator iter;
	int high, highprv = 1;
	struct request_queue *q = rq->q;

	if (!rq->bio)
		return;

	cluster = q->queue_flags & (1 << QUEUE_FLAG_CLUSTER);
	hw_seg_size = seg_size = 0;
	phys_size = hw_size = nr_phys_segs = nr_hw_segs = 0;
	rq_for_each_segment(bv, rq, iter) {
		/*
		 * the trick here is making sure that a high page is never
		 * considered part of another segment, since that might
		 * change with the bounce page.
		 */
		high = page_to_pfn(bv->bv_page) > q->bounce_pfn;
		if (high || highprv)
			goto new_hw_segment;
		if (cluster) {
			if (seg_size + bv->bv_len > q->max_segment_size)
				goto new_segment;
			if (!BIOVEC_PHYS_MERGEABLE(bvprv, bv))
				goto new_segment;
			if (!BIOVEC_SEG_BOUNDARY(q, bvprv, bv))
				goto new_segment;
			if (BIOVEC_VIRT_OVERSIZE(hw_seg_size + bv->bv_len))
				goto new_hw_segment;

			seg_size += bv->bv_len;
			hw_seg_size += bv->bv_len;
			bvprv = bv;
			continue;
		}
new_segment:
		if (BIOVEC_VIRT_MERGEABLE(bvprv, bv) &&
		    !BIOVEC_VIRT_OVERSIZE(hw_seg_size + bv->bv_len))
			hw_seg_size += bv->bv_len;
		else {
new_hw_segment:
			if (nr_hw_segs == 1 &&
			    hw_seg_size > rq->bio->bi_hw_front_size)
				rq->bio->bi_hw_front_size = hw_seg_size;
			hw_seg_size = BIOVEC_VIRT_START_SIZE(bv) + bv->bv_len;
			nr_hw_segs++;
		}

		nr_phys_segs++;
		bvprv = bv;
		seg_size = bv->bv_len;
		highprv = high;
	}

	if (nr_hw_segs == 1 &&
	    hw_seg_size > rq->bio->bi_hw_front_size)
		rq->bio->bi_hw_front_size = hw_seg_size;
	if (hw_seg_size > rq->biotail->bi_hw_back_size)
		rq->biotail->bi_hw_back_size = hw_seg_size;
	rq->nr_phys_segments = nr_phys_segs;
	rq->nr_hw_segments = nr_hw_segs;
}

void blk_recount_segments(struct request_queue *q, struct bio *bio)
{
	struct request rq;
	struct bio *nxt = bio->bi_next;
	rq.q = q;
	rq.bio = rq.biotail = bio;
	bio->bi_next = NULL;
	blk_recalc_rq_segments(&rq);
	bio->bi_next = nxt;
	bio->bi_phys_segments = rq.nr_phys_segments;
	bio->bi_hw_segments = rq.nr_hw_segments;
	bio->bi_flags |= (1 << BIO_SEG_VALID);
}
EXPORT_SYMBOL(blk_recount_segments);

static int blk_phys_contig_segment(struct request_queue *q, struct bio *bio,
				   struct bio *nxt)
{
	if (!(q->queue_flags & (1 << QUEUE_FLAG_CLUSTER)))
		return 0;

	if (!BIOVEC_PHYS_MERGEABLE(__BVEC_END(bio), __BVEC_START(nxt)))
		return 0;
	if (bio->bi_size + nxt->bi_size > q->max_segment_size)
		return 0;

	/*
	 * bio and nxt are contigous in memory, check if the queue allows
	 * these two to be merged into one
	 */
	if (BIO_SEG_BOUNDARY(q, bio, nxt))
		return 1;

	return 0;
}

static int blk_hw_contig_segment(struct request_queue *q, struct bio *bio,
				 struct bio *nxt)
{
	if (unlikely(!bio_flagged(bio, BIO_SEG_VALID)))
		blk_recount_segments(q, bio);
	if (unlikely(!bio_flagged(nxt, BIO_SEG_VALID)))
		blk_recount_segments(q, nxt);
	if (!BIOVEC_VIRT_MERGEABLE(__BVEC_END(bio), __BVEC_START(nxt)) ||
	    BIOVEC_VIRT_OVERSIZE(bio->bi_hw_back_size + nxt->bi_hw_front_size))
		return 0;
	if (bio->bi_hw_back_size + nxt->bi_hw_front_size > q->max_segment_size)
		return 0;

	return 1;
}

/*
 * map a request to scatterlist, return number of sg entries setup. Caller
 * must make sure sg can hold rq->nr_phys_segments entries
 */
int blk_rq_map_sg(struct request_queue *q, struct request *rq,
		  struct scatterlist *sglist)
{
	struct bio_vec *bvec, *bvprv;
	struct req_iterator iter;
	struct scatterlist *sg;
	int nsegs, cluster;

	nsegs = 0;
	cluster = q->queue_flags & (1 << QUEUE_FLAG_CLUSTER);

	/*
	 * for each bio in rq
	 */
	bvprv = NULL;
	sg = NULL;
	rq_for_each_segment(bvec, rq, iter) {
		int nbytes = bvec->bv_len;

		if (bvprv && cluster) {
			if (sg->length + nbytes > q->max_segment_size)
				goto new_segment;

			if (!BIOVEC_PHYS_MERGEABLE(bvprv, bvec))
				goto new_segment;
			if (!BIOVEC_SEG_BOUNDARY(q, bvprv, bvec))
				goto new_segment;

			sg->length += nbytes;
		} else {
new_segment:
			if (!sg)
				sg = sglist;
			else {
				/*
				 * If the driver previously mapped a shorter
				 * list, we could see a termination bit
				 * prematurely unless it fully inits the sg
				 * table on each mapping. We KNOW that there
				 * must be more entries here or the driver
				 * would be buggy, so force clear the
				 * termination bit to avoid doing a full
				 * sg_init_table() in drivers for each command.
				 */
				sg->page_link &= ~0x02;
				sg = sg_next(sg);
			}

			sg_set_page(sg, bvec->bv_page, nbytes, bvec->bv_offset);
			nsegs++;
		}
		bvprv = bvec;
	} /* segments in rq */

	if (q->dma_drain_size && q->dma_drain_needed(rq)) {
		if (rq->cmd_flags & REQ_RW)
			memset(q->dma_drain_buffer, 0, q->dma_drain_size);

		sg->page_link &= ~0x02;
		sg = sg_next(sg);
		sg_set_page(sg, virt_to_page(q->dma_drain_buffer),
			    q->dma_drain_size,
			    ((unsigned long)q->dma_drain_buffer) &
			    (PAGE_SIZE - 1));
		nsegs++;
		rq->extra_len += q->dma_drain_size;
	}

	if (sg)
		sg_mark_end(sg);

	return nsegs;
}
EXPORT_SYMBOL(blk_rq_map_sg);

static inline int ll_new_mergeable(struct request_queue *q,
				   struct request *req,
				   struct bio *bio)
{
	int nr_phys_segs = bio_phys_segments(q, bio);

	if (req->nr_phys_segments + nr_phys_segs > q->max_phys_segments) {
		req->cmd_flags |= REQ_NOMERGE;
		if (req == q->last_merge)
			q->last_merge = NULL;
		return 0;
	}

	/*
	 * A hw segment is just getting larger, bump just the phys
	 * counter.
	 */
	req->nr_phys_segments += nr_phys_segs;
	return 1;
}

static inline int ll_new_hw_segment(struct request_queue *q,
				    struct request *req,
				    struct bio *bio)
{
	int nr_hw_segs = bio_hw_segments(q, bio);
	int nr_phys_segs = bio_phys_segments(q, bio);

	if (req->nr_hw_segments + nr_hw_segs > q->max_hw_segments
	    || req->nr_phys_segments + nr_phys_segs > q->max_phys_segments) {
		req->cmd_flags |= REQ_NOMERGE;
		if (req == q->last_merge)
			q->last_merge = NULL;
		return 0;
	}

	/*
	 * This will form the start of a new hw segment.  Bump both
	 * counters.
	 */
	req->nr_hw_segments += nr_hw_segs;
	req->nr_phys_segments += nr_phys_segs;
	return 1;
}

int ll_back_merge_fn(struct request_queue *q, struct request *req,
		     struct bio *bio)
{
	unsigned short max_sectors;
	int len;

	if (unlikely(blk_pc_request(req)))
		max_sectors = q->max_hw_sectors;
	else
		max_sectors = q->max_sectors;

	if (req->nr_sectors + bio_sectors(bio) > max_sectors) {
		req->cmd_flags |= REQ_NOMERGE;
		if (req == q->last_merge)
			q->last_merge = NULL;
		return 0;
	}
	if (unlikely(!bio_flagged(req->biotail, BIO_SEG_VALID)))
		blk_recount_segments(q, req->biotail);
	if (unlikely(!bio_flagged(bio, BIO_SEG_VALID)))
		blk_recount_segments(q, bio);
	len = req->biotail->bi_hw_back_size + bio->bi_hw_front_size;
	if (BIOVEC_VIRT_MERGEABLE(__BVEC_END(req->biotail), __BVEC_START(bio))
	    && !BIOVEC_VIRT_OVERSIZE(len)) {
		int mergeable =  ll_new_mergeable(q, req, bio);

		if (mergeable) {
			if (req->nr_hw_segments == 1)
				req->bio->bi_hw_front_size = len;
			if (bio->bi_hw_segments == 1)
				bio->bi_hw_back_size = len;
		}
		return mergeable;
	}

	return ll_new_hw_segment(q, req, bio);
}

int ll_front_merge_fn(struct request_queue *q, struct request *req,
		      struct bio *bio)
{
	unsigned short max_sectors;
	int len;

	if (unlikely(blk_pc_request(req)))
		max_sectors = q->max_hw_sectors;
	else
		max_sectors = q->max_sectors;


	if (req->nr_sectors + bio_sectors(bio) > max_sectors) {
		req->cmd_flags |= REQ_NOMERGE;
		if (req == q->last_merge)
			q->last_merge = NULL;
		return 0;
	}
	len = bio->bi_hw_back_size + req->bio->bi_hw_front_size;
	if (unlikely(!bio_flagged(bio, BIO_SEG_VALID)))
		blk_recount_segments(q, bio);
	if (unlikely(!bio_flagged(req->bio, BIO_SEG_VALID)))
		blk_recount_segments(q, req->bio);
	if (BIOVEC_VIRT_MERGEABLE(__BVEC_END(bio), __BVEC_START(req->bio)) &&
	    !BIOVEC_VIRT_OVERSIZE(len)) {
		int mergeable =  ll_new_mergeable(q, req, bio);

		if (mergeable) {
			if (bio->bi_hw_segments == 1)
				bio->bi_hw_front_size = len;
			if (req->nr_hw_segments == 1)
				req->biotail->bi_hw_back_size = len;
		}
		return mergeable;
	}

	return ll_new_hw_segment(q, req, bio);
}

static int ll_merge_requests_fn(struct request_queue *q, struct request *req,
				struct request *next)
{
	int total_phys_segments;
	int total_hw_segments;

	/*
	 * First check if the either of the requests are re-queued
	 * requests.  Can't merge them if they are.
	 */
	if (req->special || next->special)
		return 0;

	/*
	 * Will it become too large?
	 */
	if ((req->nr_sectors + next->nr_sectors) > q->max_sectors)
		return 0;

	total_phys_segments = req->nr_phys_segments + next->nr_phys_segments;
	if (blk_phys_contig_segment(q, req->biotail, next->bio))
		total_phys_segments--;

	if (total_phys_segments > q->max_phys_segments)
		return 0;

	total_hw_segments = req->nr_hw_segments + next->nr_hw_segments;
	if (blk_hw_contig_segment(q, req->biotail, next->bio)) {
		int len = req->biotail->bi_hw_back_size +
				next->bio->bi_hw_front_size;
		/*
		 * propagate the combined length to the end of the requests
		 */
		if (req->nr_hw_segments == 1)
			req->bio->bi_hw_front_size = len;
		if (next->nr_hw_segments == 1)
			next->biotail->bi_hw_back_size = len;
		total_hw_segments--;
	}

	if (total_hw_segments > q->max_hw_segments)
		return 0;

	/* Merge is OK... */
	req->nr_phys_segments = total_phys_segments;
	req->nr_hw_segments = total_hw_segments;
	return 1;
}

/*
 * Has to be called with the request spinlock acquired
 */
static int attempt_merge(struct request_queue *q, struct request *req,
			  struct request *next)
{
	if (!rq_mergeable(req) || !rq_mergeable(next))
		return 0;

	/*
	 * not contiguous
	 */
	if (req->sector + req->nr_sectors != next->sector)
		return 0;

	if (rq_data_dir(req) != rq_data_dir(next)
	    || req->rq_disk != next->rq_disk
	    || next->special)
		return 0;

	/*
	 * If we are allowed to merge, then append bio list
	 * from next to rq and release next. merge_requests_fn
	 * will have updated segment counts, update sector
	 * counts here.
	 */
	if (!ll_merge_requests_fn(q, req, next))
		return 0;

	/*
	 * At this point we have either done a back merge
	 * or front merge. We need the smaller start_time of
	 * the merged requests to be the current request
	 * for accounting purposes.
	 */
	if (time_after(req->start_time, next->start_time))
		req->start_time = next->start_time;

	req->biotail->bi_next = next->bio;
	req->biotail = next->biotail;

	req->nr_sectors = req->hard_nr_sectors += next->hard_nr_sectors;

	elv_merge_requests(q, req, next);

	if (req->rq_disk) {
		struct hd_struct *part
			= get_part(req->rq_disk, req->sector);
		disk_round_stats(req->rq_disk);
		req->rq_disk->in_flight--;
		if (part) {
			part_round_stats(part);
			part->in_flight--;
		}
	}

	req->ioprio = ioprio_best(req->ioprio, next->ioprio);

	__blk_put_request(q, next);
	return 1;
}

int attempt_back_merge(struct request_queue *q, struct request *rq)
{
	struct request *next = elv_latter_request(q, rq);

	if (next)
		return attempt_merge(q, rq, next);

	return 0;
}

int attempt_front_merge(struct request_queue *q, struct request *rq)
{
	struct request *prev = elv_former_request(q, rq);

	if (prev)
		return attempt_merge(q, prev, rq);

	return 0;
}
