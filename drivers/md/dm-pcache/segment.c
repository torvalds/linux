// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/dax.h>

#include "pcache_internal.h"
#include "cache_dev.h"
#include "segment.h"

int segment_copy_to_bio(struct pcache_segment *segment,
		u32 data_off, u32 data_len, struct bio *bio, u32 bio_off)
{
	struct iov_iter iter;
	size_t copied;
	void *src;

	iov_iter_bvec(&iter, ITER_DEST, &bio->bi_io_vec[bio->bi_iter.bi_idx],
			bio_segments(bio), bio->bi_iter.bi_size);
	iter.iov_offset = bio->bi_iter.bi_bvec_done;
	if (bio_off)
		iov_iter_advance(&iter, bio_off);

	src = segment->data + data_off;
	copied = _copy_mc_to_iter(src, data_len, &iter);
	if (copied != data_len)
		return -EIO;

	return 0;
}

int segment_copy_from_bio(struct pcache_segment *segment,
		u32 data_off, u32 data_len, struct bio *bio, u32 bio_off)
{
	struct iov_iter iter;
	size_t copied;
	void *dst;

	iov_iter_bvec(&iter, ITER_SOURCE, &bio->bi_io_vec[bio->bi_iter.bi_idx],
			bio_segments(bio), bio->bi_iter.bi_size);
	iter.iov_offset = bio->bi_iter.bi_bvec_done;
	if (bio_off)
		iov_iter_advance(&iter, bio_off);

	dst = segment->data + data_off;
	copied = _copy_from_iter_flushcache(dst, data_len, &iter);
	if (copied != data_len)
		return -EIO;
	pmem_wmb();

	return 0;
}

void pcache_segment_init(struct pcache_cache_dev *cache_dev, struct pcache_segment *segment,
		      struct pcache_segment_init_options *options)
{
	segment->seg_info = options->seg_info;
	segment_info_set_type(segment->seg_info, options->type);

	segment->cache_dev = cache_dev;
	segment->seg_id = options->seg_id;
	segment->data_size = PCACHE_SEG_SIZE - options->data_off;
	segment->data = CACHE_DEV_SEGMENT(cache_dev, options->seg_id) + options->data_off;
}
