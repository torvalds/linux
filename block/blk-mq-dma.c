// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Christoph Hellwig
 */
#include "blk.h"

struct phys_vec {
	phys_addr_t	paddr;
	u32		len;
};

static bool blk_map_iter_next(struct request *req, struct req_iterator *iter,
			      struct phys_vec *vec)
{
	unsigned int max_size;
	struct bio_vec bv;

	if (req->rq_flags & RQF_SPECIAL_PAYLOAD) {
		if (!iter->bio)
			return false;
		vec->paddr = bvec_phys(&req->special_vec);
		vec->len = req->special_vec.bv_len;
		iter->bio = NULL;
		return true;
	}

	if (!iter->iter.bi_size)
		return false;

	bv = mp_bvec_iter_bvec(iter->bio->bi_io_vec, iter->iter);
	vec->paddr = bvec_phys(&bv);
	max_size = get_max_segment_size(&req->q->limits, vec->paddr, UINT_MAX);
	bv.bv_len = min(bv.bv_len, max_size);
	bio_advance_iter_single(iter->bio, &iter->iter, bv.bv_len);

	/*
	 * If we are entirely done with this bi_io_vec entry, check if the next
	 * one could be merged into it.  This typically happens when moving to
	 * the next bio, but some callers also don't pack bvecs tight.
	 */
	while (!iter->iter.bi_size || !iter->iter.bi_bvec_done) {
		struct bio_vec next;

		if (!iter->iter.bi_size) {
			if (!iter->bio->bi_next)
				break;
			iter->bio = iter->bio->bi_next;
			iter->iter = iter->bio->bi_iter;
		}

		next = mp_bvec_iter_bvec(iter->bio->bi_io_vec, iter->iter);
		if (bv.bv_len + next.bv_len > max_size ||
		    !biovec_phys_mergeable(req->q, &bv, &next))
			break;

		bv.bv_len += next.bv_len;
		bio_advance_iter_single(iter->bio, &iter->iter, next.bv_len);
	}

	vec->len = bv.bv_len;
	return true;
}

static inline struct scatterlist *
blk_next_sg(struct scatterlist **sg, struct scatterlist *sglist)
{
	if (!*sg)
		return sglist;

	/*
	 * If the driver previously mapped a shorter list, we could see a
	 * termination bit prematurely unless it fully inits the sg table
	 * on each mapping. We KNOW that there must be more entries here
	 * or the driver would be buggy, so force clear the termination bit
	 * to avoid doing a full sg_init_table() in drivers for each command.
	 */
	sg_unmark_end(*sg);
	return sg_next(*sg);
}

/*
 * Map a request to scatterlist, return number of sg entries setup. Caller
 * must make sure sg can hold rq->nr_phys_segments entries.
 */
int __blk_rq_map_sg(struct request *rq, struct scatterlist *sglist,
		    struct scatterlist **last_sg)
{
	struct req_iterator iter = {
		.bio	= rq->bio,
	};
	struct phys_vec vec;
	int nsegs = 0;

	/* the internal flush request may not have bio attached */
	if (iter.bio)
		iter.iter = iter.bio->bi_iter;

	while (blk_map_iter_next(rq, &iter, &vec)) {
		*last_sg = blk_next_sg(last_sg, sglist);
		sg_set_page(*last_sg, phys_to_page(vec.paddr), vec.len,
				offset_in_page(vec.paddr));
		nsegs++;
	}

	if (*last_sg)
		sg_mark_end(*last_sg);

	/*
	 * Something must have been wrong if the figured number of
	 * segment is bigger than number of req's physical segments
	 */
	WARN_ON(nsegs > blk_rq_nr_phys_segments(rq));

	return nsegs;
}
EXPORT_SYMBOL(__blk_rq_map_sg);
