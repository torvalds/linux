// SPDX-License-Identifier: GPL-2.0
/*
 * V4L2 H264 helpers.
 *
 * Copyright (C) 2019 Collabora, Ltd.
 *
 * Author: Boris Brezillon <boris.brezillon@collabora.com>
 */

#include <linux/module.h>
#include <linux/sort.h>

#include <media/v4l2-h264.h>

/**
 * v4l2_h264_init_reflist_builder() - Initialize a P/B0/B1 reference list
 *				      builder
 *
 * @b: the builder context to initialize
 * @dec_params: decode parameters control
 * @slice_params: first slice parameters control
 * @sps: SPS control
 * @dpb: DPB to use when creating the reference list
 */
void
v4l2_h264_init_reflist_builder(struct v4l2_h264_reflist_builder *b,
		const struct v4l2_ctrl_h264_decode_params *dec_params,
		const struct v4l2_ctrl_h264_slice_params *slice_params,
		const struct v4l2_ctrl_h264_sps *sps,
		const struct v4l2_h264_dpb_entry dpb[V4L2_H264_NUM_DPB_ENTRIES])
{
	int cur_frame_num, max_frame_num;
	unsigned int i;

	max_frame_num = 1 << (sps->log2_max_frame_num_minus4 + 4);
	cur_frame_num = slice_params->frame_num;

	memset(b, 0, sizeof(*b));
	if (!(slice_params->flags & V4L2_H264_SLICE_FLAG_FIELD_PIC))
		b->cur_pic_order_count = min(dec_params->bottom_field_order_cnt,
					     dec_params->top_field_order_cnt);
	else if (slice_params->flags & V4L2_H264_SLICE_FLAG_BOTTOM_FIELD)
		b->cur_pic_order_count = dec_params->bottom_field_order_cnt;
	else
		b->cur_pic_order_count = dec_params->top_field_order_cnt;

	for (i = 0; i < V4L2_H264_NUM_DPB_ENTRIES; i++) {
		u32 pic_order_count;

		if (!(dpb[i].flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE))
			continue;

		b->refs[i].pic_num = dpb[i].pic_num;
		if (dpb[i].flags & V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM)
			b->refs[i].longterm = true;

		/*
		 * Handle frame_num wraparound as described in section
		 * '8.2.4.1 Decoding process for picture numbers' of the spec.
		 * TODO: This logic will have to be adjusted when we start
		 * supporting interlaced content.
		 */
		if (dpb[i].frame_num > cur_frame_num)
			b->refs[i].frame_num = (int)dpb[i].frame_num -
					       max_frame_num;
		else
			b->refs[i].frame_num = dpb[i].frame_num;

		if (!(dpb[i].flags & V4L2_H264_DPB_ENTRY_FLAG_FIELD))
			pic_order_count = min(dpb[i].top_field_order_cnt,
					      dpb[i].bottom_field_order_cnt);
		else if (dpb[i].flags & V4L2_H264_DPB_ENTRY_FLAG_BOTTOM_FIELD)
			pic_order_count = dpb[i].bottom_field_order_cnt;
		else
			pic_order_count = dpb[i].top_field_order_cnt;

		b->refs[i].pic_order_count = pic_order_count;
		b->unordered_reflist[b->num_valid] = i;
		b->num_valid++;
	}

	for (i = b->num_valid; i < ARRAY_SIZE(b->unordered_reflist); i++)
		b->unordered_reflist[i] = i;
}
EXPORT_SYMBOL_GPL(v4l2_h264_init_reflist_builder);

static int v4l2_h264_p_ref_list_cmp(const void *ptra, const void *ptrb,
				    const void *data)
{
	const struct v4l2_h264_reflist_builder *builder = data;
	u8 idxa, idxb;

	idxa = *((u8 *)ptra);
	idxb = *((u8 *)ptrb);

	if (WARN_ON(idxa >= V4L2_H264_NUM_DPB_ENTRIES ||
		    idxb >= V4L2_H264_NUM_DPB_ENTRIES))
		return 1;

	if (builder->refs[idxa].longterm != builder->refs[idxb].longterm) {
		/* Short term pics first. */
		if (!builder->refs[idxa].longterm)
			return -1;
		else
			return 1;
	}

	/*
	 * Short term pics in descending pic num order, long term ones in
	 * ascending order.
	 */
	if (!builder->refs[idxa].longterm)
		return builder->refs[idxb].frame_num <
		       builder->refs[idxa].frame_num ?
		       -1 : 1;

	return builder->refs[idxa].pic_num < builder->refs[idxb].pic_num ?
	       -1 : 1;
}

static int v4l2_h264_b0_ref_list_cmp(const void *ptra, const void *ptrb,
				     const void *data)
{
	const struct v4l2_h264_reflist_builder *builder = data;
	s32 poca, pocb;
	u8 idxa, idxb;

	idxa = *((u8 *)ptra);
	idxb = *((u8 *)ptrb);

	if (WARN_ON(idxa >= V4L2_H264_NUM_DPB_ENTRIES ||
		    idxb >= V4L2_H264_NUM_DPB_ENTRIES))
		return 1;

	if (builder->refs[idxa].longterm != builder->refs[idxb].longterm) {
		/* Short term pics first. */
		if (!builder->refs[idxa].longterm)
			return -1;
		else
			return 1;
	}

	/* Long term pics in ascending pic num order. */
	if (builder->refs[idxa].longterm)
		return builder->refs[idxa].pic_num <
		       builder->refs[idxb].pic_num ?
		       -1 : 1;

	poca = builder->refs[idxa].pic_order_count;
	pocb = builder->refs[idxb].pic_order_count;

	/*
	 * Short term pics with POC < cur POC first in POC descending order
	 * followed by short term pics with POC > cur POC in POC ascending
	 * order.
	 */
	if ((poca < builder->cur_pic_order_count) !=
	     (pocb < builder->cur_pic_order_count))
		return poca < pocb ? -1 : 1;
	else if (poca < builder->cur_pic_order_count)
		return pocb < poca ? -1 : 1;

	return poca < pocb ? -1 : 1;
}

static int v4l2_h264_b1_ref_list_cmp(const void *ptra, const void *ptrb,
				     const void *data)
{
	const struct v4l2_h264_reflist_builder *builder = data;
	s32 poca, pocb;
	u8 idxa, idxb;

	idxa = *((u8 *)ptra);
	idxb = *((u8 *)ptrb);

	if (WARN_ON(idxa >= V4L2_H264_NUM_DPB_ENTRIES ||
		    idxb >= V4L2_H264_NUM_DPB_ENTRIES))
		return 1;

	if (builder->refs[idxa].longterm != builder->refs[idxb].longterm) {
		/* Short term pics first. */
		if (!builder->refs[idxa].longterm)
			return -1;
		else
			return 1;
	}

	/* Long term pics in ascending pic num order. */
	if (builder->refs[idxa].longterm)
		return builder->refs[idxa].pic_num <
		       builder->refs[idxb].pic_num ?
		       -1 : 1;

	poca = builder->refs[idxa].pic_order_count;
	pocb = builder->refs[idxb].pic_order_count;

	/*
	 * Short term pics with POC > cur POC first in POC ascending order
	 * followed by short term pics with POC < cur POC in POC descending
	 * order.
	 */
	if ((poca < builder->cur_pic_order_count) !=
	    (pocb < builder->cur_pic_order_count))
		return pocb < poca ? -1 : 1;
	else if (poca < builder->cur_pic_order_count)
		return pocb < poca ? -1 : 1;

	return poca < pocb ? -1 : 1;
}

/**
 * v4l2_h264_build_p_ref_list() - Build the P reference list
 *
 * @builder: reference list builder context
 * @reflist: 16-bytes array used to store the P reference list. Each entry
 *	     is an index in the DPB
 *
 * This functions builds the P reference lists. This procedure is describe in
 * section '8.2.4 Decoding process for reference picture lists construction'
 * of the H264 spec. This function can be used by H264 decoder drivers that
 * need to pass a P reference list to the hardware.
 */
void
v4l2_h264_build_p_ref_list(const struct v4l2_h264_reflist_builder *builder,
			   u8 *reflist)
{
	memcpy(reflist, builder->unordered_reflist,
	       sizeof(builder->unordered_reflist[0]) * builder->num_valid);
	sort_r(reflist, builder->num_valid, sizeof(*reflist),
	       v4l2_h264_p_ref_list_cmp, NULL, builder);
}
EXPORT_SYMBOL_GPL(v4l2_h264_build_p_ref_list);

/**
 * v4l2_h264_build_b_ref_lists() - Build the B0/B1 reference lists
 *
 * @builder: reference list builder context
 * @b0_reflist: 16-bytes array used to store the B0 reference list. Each entry
 *		is an index in the DPB
 * @b1_reflist: 16-bytes array used to store the B1 reference list. Each entry
 *		is an index in the DPB
 *
 * This functions builds the B0/B1 reference lists. This procedure is described
 * in section '8.2.4 Decoding process for reference picture lists construction'
 * of the H264 spec. This function can be used by H264 decoder drivers that
 * need to pass B0/B1 reference lists to the hardware.
 */
void
v4l2_h264_build_b_ref_lists(const struct v4l2_h264_reflist_builder *builder,
			    u8 *b0_reflist, u8 *b1_reflist)
{
	memcpy(b0_reflist, builder->unordered_reflist,
	       sizeof(builder->unordered_reflist[0]) * builder->num_valid);
	sort_r(b0_reflist, builder->num_valid, sizeof(*b0_reflist),
	       v4l2_h264_b0_ref_list_cmp, NULL, builder);

	memcpy(b1_reflist, builder->unordered_reflist,
	       sizeof(builder->unordered_reflist[0]) * builder->num_valid);
	sort_r(b1_reflist, builder->num_valid, sizeof(*b1_reflist),
	       v4l2_h264_b1_ref_list_cmp, NULL, builder);

	if (builder->num_valid > 1 &&
	    !memcmp(b1_reflist, b0_reflist, builder->num_valid))
		swap(b1_reflist[0], b1_reflist[1]);
}
EXPORT_SYMBOL_GPL(v4l2_h264_build_b_ref_lists);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("V4L2 H264 Helpers");
MODULE_AUTHOR("Boris Brezillon <boris.brezillon@collabora.com>");
