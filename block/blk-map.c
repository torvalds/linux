// SPDX-License-Identifier: GPL-2.0
/*
 * Functions related to mapping data to requests
 */
#include <linux/kernel.h>
#include <linux/sched/task_stack.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/uio.h>

#include "blk.h"

/*
 * Append a bio to a passthrough request.  Only works if the bio can be merged
 * into the request based on the driver constraints.
 */
int blk_rq_append_bio(struct request *rq, struct bio **bio)
{
	struct bio *orig_bio = *bio;

	blk_queue_bounce(rq->q, bio);

	if (!rq->bio) {
		blk_rq_bio_prep(rq->q, rq, *bio);
	} else {
		if (!ll_back_merge_fn(rq->q, rq, *bio)) {
			if (orig_bio != *bio) {
				bio_put(*bio);
				*bio = orig_bio;
			}
			return -EINVAL;
		}

		rq->biotail->bi_next = *bio;
		rq->biotail = *bio;
		rq->__data_len += (*bio)->bi_iter.bi_size;
	}

	return 0;
}
EXPORT_SYMBOL(blk_rq_append_bio);

static int __blk_rq_unmap_user(struct bio *bio)
{
	int ret = 0;

	if (bio) {
		if (bio_flagged(bio, BIO_USER_MAPPED))
			bio_unmap_user(bio);
		else
			ret = bio_uncopy_user(bio);
	}

	return ret;
}

static int __blk_rq_map_user_iov(struct request *rq,
		struct rq_map_data *map_data, struct iov_iter *iter,
		gfp_t gfp_mask, bool copy)
{
	struct request_queue *q = rq->q;
	struct bio *bio, *orig_bio;
	int ret;

	if (copy)
		bio = bio_copy_user_iov(q, map_data, iter, gfp_mask);
	else
		bio = bio_map_user_iov(q, iter, gfp_mask);

	if (IS_ERR(bio))
		return PTR_ERR(bio);

	bio->bi_opf &= ~REQ_OP_MASK;
	bio->bi_opf |= req_op(rq);

	orig_bio = bio;

	/*
	 * We link the bounce buffer in and could have to traverse it
	 * later so we have to get a ref to prevent it from being freed
	 */
	ret = blk_rq_append_bio(rq, &bio);
	if (ret) {
		__blk_rq_unmap_user(orig_bio);
		return ret;
	}
	bio_get(bio);

	return 0;
}

/**
 * blk_rq_map_user_iov - map user data to a request, for passthrough requests
 * @q:		request queue where request should be inserted
 * @rq:		request to map data to
 * @map_data:   pointer to the rq_map_data holding pages (if necessary)
 * @iter:	iovec iterator
 * @gfp_mask:	memory allocation flags
 *
 * Description:
 *    Data will be mapped directly for zero copy I/O, if possible. Otherwise
 *    a kernel bounce buffer is used.
 *
 *    A matching blk_rq_unmap_user() must be issued at the end of I/O, while
 *    still in process context.
 *
 *    Note: The mapped bio may need to be bounced through blk_queue_bounce()
 *    before being submitted to the device, as pages mapped may be out of
 *    reach. It's the callers responsibility to make sure this happens. The
 *    original bio must be passed back in to blk_rq_unmap_user() for proper
 *    unmapping.
 */
int blk_rq_map_user_iov(struct request_queue *q, struct request *rq,
			struct rq_map_data *map_data,
			const struct iov_iter *iter, gfp_t gfp_mask)
{
	bool copy = false;
	unsigned long align = q->dma_pad_mask | queue_dma_alignment(q);
	struct bio *bio = NULL;
	struct iov_iter i;
	int ret = -EINVAL;

	if (!iter_is_iovec(iter))
		goto fail;

	if (map_data)
		copy = true;
	else if (iov_iter_alignment(iter) & align)
		copy = true;
	else if (queue_virt_boundary(q))
		copy = queue_virt_boundary(q) & iov_iter_gap_alignment(iter);

	i = *iter;
	do {
		ret =__blk_rq_map_user_iov(rq, map_data, &i, gfp_mask, copy);
		if (ret)
			goto unmap_rq;
		if (!bio)
			bio = rq->bio;
	} while (iov_iter_count(&i));

	if (!bio_flagged(bio, BIO_USER_MAPPED))
		rq->rq_flags |= RQF_COPY_USER;
	return 0;

unmap_rq:
	blk_rq_unmap_user(bio);
fail:
	rq->bio = NULL;
	return ret;
}
EXPORT_SYMBOL(blk_rq_map_user_iov);

int blk_rq_map_user(struct request_queue *q, struct request *rq,
		    struct rq_map_data *map_data, void __user *ubuf,
		    unsigned long len, gfp_t gfp_mask)
{
	struct iovec iov;
	struct iov_iter i;
	int ret = import_single_range(rq_data_dir(rq), ubuf, len, &iov, &i);

	if (unlikely(ret < 0))
		return ret;

	return blk_rq_map_user_iov(q, rq, map_data, &i, gfp_mask);
}
EXPORT_SYMBOL(blk_rq_map_user);

/**
 * blk_rq_unmap_user - unmap a request with user data
 * @bio:	       start of bio list
 *
 * Description:
 *    Unmap a rq previously mapped by blk_rq_map_user(). The caller must
 *    supply the original rq->bio from the blk_rq_map_user() return, since
 *    the I/O completion may have changed rq->bio.
 */
int blk_rq_unmap_user(struct bio *bio)
{
	struct bio *mapped_bio;
	int ret = 0, ret2;

	while (bio) {
		mapped_bio = bio;
		if (unlikely(bio_flagged(bio, BIO_BOUNCED)))
			mapped_bio = bio->bi_private;

		ret2 = __blk_rq_unmap_user(mapped_bio);
		if (ret2 && !ret)
			ret = ret2;

		mapped_bio = bio;
		bio = bio->bi_next;
		bio_put(mapped_bio);
	}

	return ret;
}
EXPORT_SYMBOL(blk_rq_unmap_user);

/**
 * blk_rq_map_kern - map kernel data to a request, for passthrough requests
 * @q:		request queue where request should be inserted
 * @rq:		request to fill
 * @kbuf:	the kernel buffer
 * @len:	length of user data
 * @gfp_mask:	memory allocation flags
 *
 * Description:
 *    Data will be mapped directly if possible. Otherwise a bounce
 *    buffer is used. Can be called multiple times to append multiple
 *    buffers.
 */
int blk_rq_map_kern(struct request_queue *q, struct request *rq, void *kbuf,
		    unsigned int len, gfp_t gfp_mask)
{
	int reading = rq_data_dir(rq) == READ;
	unsigned long addr = (unsigned long) kbuf;
	int do_copy = 0;
	struct bio *bio, *orig_bio;
	int ret;

	if (len > (queue_max_hw_sectors(q) << 9))
		return -EINVAL;
	if (!len || !kbuf)
		return -EINVAL;

	do_copy = !blk_rq_aligned(q, addr, len) || object_is_on_stack(kbuf);
	if (do_copy)
		bio = bio_copy_kern(q, kbuf, len, gfp_mask, reading);
	else
		bio = bio_map_kern(q, kbuf, len, gfp_mask);

	if (IS_ERR(bio))
		return PTR_ERR(bio);

	bio->bi_opf &= ~REQ_OP_MASK;
	bio->bi_opf |= req_op(rq);

	if (do_copy)
		rq->rq_flags |= RQF_COPY_USER;

	orig_bio = bio;
	ret = blk_rq_append_bio(rq, &bio);
	if (unlikely(ret)) {
		/* request is too big */
		bio_put(orig_bio);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(blk_rq_map_kern);
