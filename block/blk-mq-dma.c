// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Christoph Hellwig
 */
#include <linux/blk-mq-dma.h>
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

/*
 * The IOVA-based DMA API wants to be able to coalesce at the minimal IOMMU page
 * size granularity (which is guaranteed to be <= PAGE_SIZE and usually 4k), so
 * we need to ensure our segments are aligned to this as well.
 *
 * Note that there is no point in using the slightly more complicated IOVA based
 * path for single segment mappings.
 */
static inline bool blk_can_dma_map_iova(struct request *req,
		struct device *dma_dev)
{
	return !((queue_virt_boundary(req->q) + 1) &
		dma_get_merge_boundary(dma_dev));
}

static bool blk_dma_map_bus(struct blk_dma_iter *iter, struct phys_vec *vec)
{
	iter->addr = pci_p2pdma_bus_addr_map(&iter->p2pdma, vec->paddr);
	iter->len = vec->len;
	return true;
}

static bool blk_dma_map_direct(struct request *req, struct device *dma_dev,
		struct blk_dma_iter *iter, struct phys_vec *vec)
{
	iter->addr = dma_map_page(dma_dev, phys_to_page(vec->paddr),
			offset_in_page(vec->paddr), vec->len, rq_dma_dir(req));
	if (dma_mapping_error(dma_dev, iter->addr)) {
		iter->status = BLK_STS_RESOURCE;
		return false;
	}
	iter->len = vec->len;
	return true;
}

static bool blk_rq_dma_map_iova(struct request *req, struct device *dma_dev,
		struct dma_iova_state *state, struct blk_dma_iter *iter,
		struct phys_vec *vec)
{
	enum dma_data_direction dir = rq_dma_dir(req);
	unsigned int mapped = 0;
	int error;

	iter->addr = state->addr;
	iter->len = dma_iova_size(state);

	do {
		error = dma_iova_link(dma_dev, state, vec->paddr, mapped,
				vec->len, dir, 0);
		if (error)
			break;
		mapped += vec->len;
	} while (blk_map_iter_next(req, &iter->iter, vec));

	error = dma_iova_sync(dma_dev, state, 0, mapped);
	if (error) {
		iter->status = errno_to_blk_status(error);
		return false;
	}

	return true;
}

/**
 * blk_rq_dma_map_iter_start - map the first DMA segment for a request
 * @req:	request to map
 * @dma_dev:	device to map to
 * @state:	DMA IOVA state
 * @iter:	block layer DMA iterator
 *
 * Start DMA mapping @req to @dma_dev.  @state and @iter are provided by the
 * caller and don't need to be initialized.  @state needs to be stored for use
 * at unmap time, @iter is only needed at map time.
 *
 * Returns %false if there is no segment to map, including due to an error, or
 * %true ft it did map a segment.
 *
 * If a segment was mapped, the DMA address for it is returned in @iter.addr and
 * the length in @iter.len.  If no segment was mapped the status code is
 * returned in @iter.status.
 *
 * The caller can call blk_rq_dma_map_coalesce() to check if further segments
 * need to be mapped after this, or go straight to blk_rq_dma_map_iter_next()
 * to try to map the following segments.
 */
bool blk_rq_dma_map_iter_start(struct request *req, struct device *dma_dev,
		struct dma_iova_state *state, struct blk_dma_iter *iter)
{
	unsigned int total_len = blk_rq_payload_bytes(req);
	struct phys_vec vec;

	iter->iter.bio = req->bio;
	iter->iter.iter = req->bio->bi_iter;
	memset(&iter->p2pdma, 0, sizeof(iter->p2pdma));
	iter->status = BLK_STS_OK;

	/*
	 * Grab the first segment ASAP because we'll need it to check for P2P
	 * transfers.
	 */
	if (!blk_map_iter_next(req, &iter->iter, &vec))
		return false;

	if (IS_ENABLED(CONFIG_PCI_P2PDMA) && (req->cmd_flags & REQ_P2PDMA)) {
		switch (pci_p2pdma_state(&iter->p2pdma, dma_dev,
					 phys_to_page(vec.paddr))) {
		case PCI_P2PDMA_MAP_BUS_ADDR:
			return blk_dma_map_bus(iter, &vec);
		case PCI_P2PDMA_MAP_THRU_HOST_BRIDGE:
			/*
			 * P2P transfers through the host bridge are treated the
			 * same as non-P2P transfers below and during unmap.
			 */
			req->cmd_flags &= ~REQ_P2PDMA;
			break;
		default:
			iter->status = BLK_STS_INVAL;
			return false;
		}
	}

	if (blk_can_dma_map_iova(req, dma_dev) &&
	    dma_iova_try_alloc(dma_dev, state, vec.paddr, total_len))
		return blk_rq_dma_map_iova(req, dma_dev, state, iter, &vec);
	return blk_dma_map_direct(req, dma_dev, iter, &vec);
}
EXPORT_SYMBOL_GPL(blk_rq_dma_map_iter_start);

/**
 * blk_rq_dma_map_iter_next - map the next DMA segment for a request
 * @req:	request to map
 * @dma_dev:	device to map to
 * @state:	DMA IOVA state
 * @iter:	block layer DMA iterator
 *
 * Iterate to the next mapping after a previous call to
 * blk_rq_dma_map_iter_start().  See there for a detailed description of the
 * arguments.
 *
 * Returns %false if there is no segment to map, including due to an error, or
 * %true ft it did map a segment.
 *
 * If a segment was mapped, the DMA address for it is returned in @iter.addr and
 * the length in @iter.len.  If no segment was mapped the status code is
 * returned in @iter.status.
 */
bool blk_rq_dma_map_iter_next(struct request *req, struct device *dma_dev,
		struct dma_iova_state *state, struct blk_dma_iter *iter)
{
	struct phys_vec vec;

	if (!blk_map_iter_next(req, &iter->iter, &vec))
		return false;

	if (iter->p2pdma.map == PCI_P2PDMA_MAP_BUS_ADDR)
		return blk_dma_map_bus(iter, &vec);
	return blk_dma_map_direct(req, dma_dev, iter, &vec);
}
EXPORT_SYMBOL_GPL(blk_rq_dma_map_iter_next);

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
