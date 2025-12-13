/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _PCACHE_SEGMENT_H
#define _PCACHE_SEGMENT_H

#include <linux/bio.h>
#include <linux/bitfield.h>

#include "pcache_internal.h"

struct pcache_segment_info {
	struct pcache_meta_header	header;
	__u32			flags;
	__u32			next_seg;
};

#define PCACHE_SEG_INFO_FLAGS_HAS_NEXT		BIT(0)

#define PCACHE_SEG_INFO_FLAGS_TYPE_MASK         GENMASK(4, 1)
#define PCACHE_SEGMENT_TYPE_CACHE_DATA		1

static inline bool segment_info_has_next(struct pcache_segment_info *seg_info)
{
	return (seg_info->flags & PCACHE_SEG_INFO_FLAGS_HAS_NEXT);
}

static inline void segment_info_set_type(struct pcache_segment_info *seg_info, u8 type)
{
	seg_info->flags &= ~PCACHE_SEG_INFO_FLAGS_TYPE_MASK;
	seg_info->flags |= FIELD_PREP(PCACHE_SEG_INFO_FLAGS_TYPE_MASK, type);
}

static inline u8 segment_info_get_type(struct pcache_segment_info *seg_info)
{
	return FIELD_GET(PCACHE_SEG_INFO_FLAGS_TYPE_MASK, seg_info->flags);
}

struct pcache_segment_pos {
	struct pcache_segment	*segment;	/* Segment associated with the position */
	u32			off;		/* Offset within the segment */
};

struct pcache_segment_init_options {
	u8			type;
	u32			seg_id;
	u32			data_off;

	struct pcache_segment_info	*seg_info;
};

struct pcache_segment {
	struct pcache_cache_dev	*cache_dev;

	void			*data;
	u32			data_size;
	u32			seg_id;

	struct pcache_segment_info	*seg_info;
};

int segment_copy_to_bio(struct pcache_segment *segment,
		      u32 data_off, u32 data_len, struct bio *bio, u32 bio_off);
int segment_copy_from_bio(struct pcache_segment *segment,
			u32 data_off, u32 data_len, struct bio *bio, u32 bio_off);

static inline void segment_pos_advance(struct pcache_segment_pos *seg_pos, u32 len)
{
	BUG_ON(seg_pos->off + len > seg_pos->segment->data_size);

	seg_pos->off += len;
}

void pcache_segment_init(struct pcache_cache_dev *cache_dev, struct pcache_segment *segment,
		      struct pcache_segment_init_options *options);
#endif /* _PCACHE_SEGMENT_H */
