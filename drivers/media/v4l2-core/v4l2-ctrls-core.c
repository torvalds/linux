// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * V4L2 controls framework core implementation.
 *
 * Copyright (C) 2010-2021  Hans Verkuil <hverkuil-cisco@xs4all.nl>
 */

#include <linux/export.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>

#include "v4l2-ctrls-priv.h"

static const union v4l2_ctrl_ptr ptr_null;

static void fill_event(struct v4l2_event *ev, struct v4l2_ctrl *ctrl,
		       u32 changes)
{
	memset(ev, 0, sizeof(*ev));
	ev->type = V4L2_EVENT_CTRL;
	ev->id = ctrl->id;
	ev->u.ctrl.changes = changes;
	ev->u.ctrl.type = ctrl->type;
	ev->u.ctrl.flags = user_flags(ctrl);
	if (ctrl->is_ptr)
		ev->u.ctrl.value64 = 0;
	else
		ev->u.ctrl.value64 = *ctrl->p_cur.p_s64;
	ev->u.ctrl.minimum = ctrl->minimum;
	ev->u.ctrl.maximum = ctrl->maximum;
	if (ctrl->type == V4L2_CTRL_TYPE_MENU
	    || ctrl->type == V4L2_CTRL_TYPE_INTEGER_MENU)
		ev->u.ctrl.step = 1;
	else
		ev->u.ctrl.step = ctrl->step;
	ev->u.ctrl.default_value = ctrl->default_value;
}

void send_initial_event(struct v4l2_fh *fh, struct v4l2_ctrl *ctrl)
{
	struct v4l2_event ev;
	u32 changes = V4L2_EVENT_CTRL_CH_FLAGS;

	if (!(ctrl->flags & V4L2_CTRL_FLAG_WRITE_ONLY))
		changes |= V4L2_EVENT_CTRL_CH_VALUE;
	fill_event(&ev, ctrl, changes);
	v4l2_event_queue_fh(fh, &ev);
}

void send_event(struct v4l2_fh *fh, struct v4l2_ctrl *ctrl, u32 changes)
{
	struct v4l2_event ev;
	struct v4l2_subscribed_event *sev;

	if (list_empty(&ctrl->ev_subs))
		return;
	fill_event(&ev, ctrl, changes);

	list_for_each_entry(sev, &ctrl->ev_subs, node)
		if (sev->fh != fh ||
		    (sev->flags & V4L2_EVENT_SUB_FL_ALLOW_FEEDBACK))
			v4l2_event_queue_fh(sev->fh, &ev);
}

static bool std_equal(const struct v4l2_ctrl *ctrl, u32 idx,
		      union v4l2_ctrl_ptr ptr1,
		      union v4l2_ctrl_ptr ptr2)
{
	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_BUTTON:
		return false;
	case V4L2_CTRL_TYPE_STRING:
		idx *= ctrl->elem_size;
		/* strings are always 0-terminated */
		return !strcmp(ptr1.p_char + idx, ptr2.p_char + idx);
	case V4L2_CTRL_TYPE_INTEGER64:
		return ptr1.p_s64[idx] == ptr2.p_s64[idx];
	case V4L2_CTRL_TYPE_U8:
		return ptr1.p_u8[idx] == ptr2.p_u8[idx];
	case V4L2_CTRL_TYPE_U16:
		return ptr1.p_u16[idx] == ptr2.p_u16[idx];
	case V4L2_CTRL_TYPE_U32:
		return ptr1.p_u32[idx] == ptr2.p_u32[idx];
	default:
		if (ctrl->is_int)
			return ptr1.p_s32[idx] == ptr2.p_s32[idx];
		idx *= ctrl->elem_size;
		return !memcmp(ptr1.p_const + idx, ptr2.p_const + idx,
			       ctrl->elem_size);
	}
}

/* Default intra MPEG-2 quantisation coefficients, from the specification. */
static const u8 mpeg2_intra_quant_matrix[64] = {
	8,  16, 16, 19, 16, 19, 22, 22,
	22, 22, 22, 22, 26, 24, 26, 27,
	27, 27, 26, 26, 26, 26, 27, 27,
	27, 29, 29, 29, 34, 34, 34, 29,
	29, 29, 27, 27, 29, 29, 32, 32,
	34, 34, 37, 38, 37, 35, 35, 34,
	35, 38, 38, 40, 40, 40, 48, 48,
	46, 46, 56, 56, 58, 69, 69, 83
};

static void std_init_compound(const struct v4l2_ctrl *ctrl, u32 idx,
			      union v4l2_ctrl_ptr ptr)
{
	struct v4l2_ctrl_mpeg2_sequence *p_mpeg2_sequence;
	struct v4l2_ctrl_mpeg2_picture *p_mpeg2_picture;
	struct v4l2_ctrl_mpeg2_quantisation *p_mpeg2_quant;
	struct v4l2_ctrl_vp8_frame *p_vp8_frame;
	struct v4l2_ctrl_vp9_frame *p_vp9_frame;
	struct v4l2_ctrl_fwht_params *p_fwht_params;
	struct v4l2_ctrl_h264_scaling_matrix *p_h264_scaling_matrix;
	void *p = ptr.p + idx * ctrl->elem_size;

	if (ctrl->p_def.p_const)
		memcpy(p, ctrl->p_def.p_const, ctrl->elem_size);
	else
		memset(p, 0, ctrl->elem_size);

	switch ((u32)ctrl->type) {
	case V4L2_CTRL_TYPE_MPEG2_SEQUENCE:
		p_mpeg2_sequence = p;

		/* 4:2:0 */
		p_mpeg2_sequence->chroma_format = 1;
		break;
	case V4L2_CTRL_TYPE_MPEG2_PICTURE:
		p_mpeg2_picture = p;

		/* interlaced top field */
		p_mpeg2_picture->picture_structure = V4L2_MPEG2_PIC_TOP_FIELD;
		p_mpeg2_picture->picture_coding_type =
					V4L2_MPEG2_PIC_CODING_TYPE_I;
		break;
	case V4L2_CTRL_TYPE_MPEG2_QUANTISATION:
		p_mpeg2_quant = p;

		memcpy(p_mpeg2_quant->intra_quantiser_matrix,
		       mpeg2_intra_quant_matrix,
		       ARRAY_SIZE(mpeg2_intra_quant_matrix));
		/*
		 * The default non-intra MPEG-2 quantisation
		 * coefficients are all 16, as per the specification.
		 */
		memset(p_mpeg2_quant->non_intra_quantiser_matrix, 16,
		       sizeof(p_mpeg2_quant->non_intra_quantiser_matrix));
		break;
	case V4L2_CTRL_TYPE_VP8_FRAME:
		p_vp8_frame = p;
		p_vp8_frame->num_dct_parts = 1;
		break;
	case V4L2_CTRL_TYPE_VP9_FRAME:
		p_vp9_frame = p;
		p_vp9_frame->profile = 0;
		p_vp9_frame->bit_depth = 8;
		p_vp9_frame->flags |= V4L2_VP9_FRAME_FLAG_X_SUBSAMPLING |
			V4L2_VP9_FRAME_FLAG_Y_SUBSAMPLING;
		break;
	case V4L2_CTRL_TYPE_FWHT_PARAMS:
		p_fwht_params = p;
		p_fwht_params->version = V4L2_FWHT_VERSION;
		p_fwht_params->width = 1280;
		p_fwht_params->height = 720;
		p_fwht_params->flags = V4L2_FWHT_FL_PIXENC_YUV |
			(2 << V4L2_FWHT_FL_COMPONENTS_NUM_OFFSET);
		break;
	case V4L2_CTRL_TYPE_H264_SCALING_MATRIX:
		p_h264_scaling_matrix = p;
		/*
		 * The default (flat) H.264 scaling matrix when none are
		 * specified in the bitstream, this is according to formulas
		 *  (7-8) and (7-9) of the specification.
		 */
		memset(p_h264_scaling_matrix, 16, sizeof(*p_h264_scaling_matrix));
		break;
	}
}

static void std_init(const struct v4l2_ctrl *ctrl, u32 idx,
		     union v4l2_ctrl_ptr ptr)
{
	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_STRING:
		idx *= ctrl->elem_size;
		memset(ptr.p_char + idx, ' ', ctrl->minimum);
		ptr.p_char[idx + ctrl->minimum] = '\0';
		break;
	case V4L2_CTRL_TYPE_INTEGER64:
		ptr.p_s64[idx] = ctrl->default_value;
		break;
	case V4L2_CTRL_TYPE_INTEGER:
	case V4L2_CTRL_TYPE_INTEGER_MENU:
	case V4L2_CTRL_TYPE_MENU:
	case V4L2_CTRL_TYPE_BITMASK:
	case V4L2_CTRL_TYPE_BOOLEAN:
		ptr.p_s32[idx] = ctrl->default_value;
		break;
	case V4L2_CTRL_TYPE_BUTTON:
	case V4L2_CTRL_TYPE_CTRL_CLASS:
		ptr.p_s32[idx] = 0;
		break;
	case V4L2_CTRL_TYPE_U8:
		ptr.p_u8[idx] = ctrl->default_value;
		break;
	case V4L2_CTRL_TYPE_U16:
		ptr.p_u16[idx] = ctrl->default_value;
		break;
	case V4L2_CTRL_TYPE_U32:
		ptr.p_u32[idx] = ctrl->default_value;
		break;
	default:
		std_init_compound(ctrl, idx, ptr);
		break;
	}
}

static void std_log(const struct v4l2_ctrl *ctrl)
{
	union v4l2_ctrl_ptr ptr = ctrl->p_cur;

	if (ctrl->is_array) {
		unsigned i;

		for (i = 0; i < ctrl->nr_of_dims; i++)
			pr_cont("[%u]", ctrl->dims[i]);
		pr_cont(" ");
	}

	switch (ctrl->type) {
	case V4L2_CTRL_TYPE_INTEGER:
		pr_cont("%d", *ptr.p_s32);
		break;
	case V4L2_CTRL_TYPE_BOOLEAN:
		pr_cont("%s", *ptr.p_s32 ? "true" : "false");
		break;
	case V4L2_CTRL_TYPE_MENU:
		pr_cont("%s", ctrl->qmenu[*ptr.p_s32]);
		break;
	case V4L2_CTRL_TYPE_INTEGER_MENU:
		pr_cont("%lld", ctrl->qmenu_int[*ptr.p_s32]);
		break;
	case V4L2_CTRL_TYPE_BITMASK:
		pr_cont("0x%08x", *ptr.p_s32);
		break;
	case V4L2_CTRL_TYPE_INTEGER64:
		pr_cont("%lld", *ptr.p_s64);
		break;
	case V4L2_CTRL_TYPE_STRING:
		pr_cont("%s", ptr.p_char);
		break;
	case V4L2_CTRL_TYPE_U8:
		pr_cont("%u", (unsigned)*ptr.p_u8);
		break;
	case V4L2_CTRL_TYPE_U16:
		pr_cont("%u", (unsigned)*ptr.p_u16);
		break;
	case V4L2_CTRL_TYPE_U32:
		pr_cont("%u", (unsigned)*ptr.p_u32);
		break;
	case V4L2_CTRL_TYPE_H264_SPS:
		pr_cont("H264_SPS");
		break;
	case V4L2_CTRL_TYPE_H264_PPS:
		pr_cont("H264_PPS");
		break;
	case V4L2_CTRL_TYPE_H264_SCALING_MATRIX:
		pr_cont("H264_SCALING_MATRIX");
		break;
	case V4L2_CTRL_TYPE_H264_SLICE_PARAMS:
		pr_cont("H264_SLICE_PARAMS");
		break;
	case V4L2_CTRL_TYPE_H264_DECODE_PARAMS:
		pr_cont("H264_DECODE_PARAMS");
		break;
	case V4L2_CTRL_TYPE_H264_PRED_WEIGHTS:
		pr_cont("H264_PRED_WEIGHTS");
		break;
	case V4L2_CTRL_TYPE_FWHT_PARAMS:
		pr_cont("FWHT_PARAMS");
		break;
	case V4L2_CTRL_TYPE_VP8_FRAME:
		pr_cont("VP8_FRAME");
		break;
	case V4L2_CTRL_TYPE_HDR10_CLL_INFO:
		pr_cont("HDR10_CLL_INFO");
		break;
	case V4L2_CTRL_TYPE_HDR10_MASTERING_DISPLAY:
		pr_cont("HDR10_MASTERING_DISPLAY");
		break;
	case V4L2_CTRL_TYPE_MPEG2_QUANTISATION:
		pr_cont("MPEG2_QUANTISATION");
		break;
	case V4L2_CTRL_TYPE_MPEG2_SEQUENCE:
		pr_cont("MPEG2_SEQUENCE");
		break;
	case V4L2_CTRL_TYPE_MPEG2_PICTURE:
		pr_cont("MPEG2_PICTURE");
		break;
	case V4L2_CTRL_TYPE_VP9_COMPRESSED_HDR:
		pr_cont("VP9_COMPRESSED_HDR");
		break;
	case V4L2_CTRL_TYPE_VP9_FRAME:
		pr_cont("VP9_FRAME");
		break;
	default:
		pr_cont("unknown type %d", ctrl->type);
		break;
	}
}

/*
 * Round towards the closest legal value. Be careful when we are
 * close to the maximum range of the control type to prevent
 * wrap-arounds.
 */
#define ROUND_TO_RANGE(val, offset_type, ctrl)			\
({								\
	offset_type offset;					\
	if ((ctrl)->maximum >= 0 &&				\
	    val >= (ctrl)->maximum - (s32)((ctrl)->step / 2))	\
		val = (ctrl)->maximum;				\
	else							\
		val += (s32)((ctrl)->step / 2);			\
	val = clamp_t(typeof(val), val,				\
		      (ctrl)->minimum, (ctrl)->maximum);	\
	offset = (val) - (ctrl)->minimum;			\
	offset = (ctrl)->step * (offset / (u32)(ctrl)->step);	\
	val = (ctrl)->minimum + offset;				\
	0;							\
})

/* Validate a new control */

#define zero_padding(s) \
	memset(&(s).padding, 0, sizeof((s).padding))
#define zero_reserved(s) \
	memset(&(s).reserved, 0, sizeof((s).reserved))

static int
validate_vp9_lf_params(struct v4l2_vp9_loop_filter *lf)
{
	unsigned int i;

	if (lf->flags & ~(V4L2_VP9_LOOP_FILTER_FLAG_DELTA_ENABLED |
			  V4L2_VP9_LOOP_FILTER_FLAG_DELTA_UPDATE))
		return -EINVAL;

	/* That all values are in the accepted range. */
	if (lf->level > GENMASK(5, 0))
		return -EINVAL;

	if (lf->sharpness > GENMASK(2, 0))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(lf->ref_deltas); i++)
		if (lf->ref_deltas[i] < -63 || lf->ref_deltas[i] > 63)
			return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(lf->mode_deltas); i++)
		if (lf->mode_deltas[i] < -63 || lf->mode_deltas[i] > 63)
			return -EINVAL;

	zero_reserved(*lf);
	return 0;
}

static int
validate_vp9_quant_params(struct v4l2_vp9_quantization *quant)
{
	if (quant->delta_q_y_dc < -15 || quant->delta_q_y_dc > 15 ||
	    quant->delta_q_uv_dc < -15 || quant->delta_q_uv_dc > 15 ||
	    quant->delta_q_uv_ac < -15 || quant->delta_q_uv_ac > 15)
		return -EINVAL;

	zero_reserved(*quant);
	return 0;
}

static int
validate_vp9_seg_params(struct v4l2_vp9_segmentation *seg)
{
	unsigned int i, j;

	if (seg->flags & ~(V4L2_VP9_SEGMENTATION_FLAG_ENABLED |
			   V4L2_VP9_SEGMENTATION_FLAG_UPDATE_MAP |
			   V4L2_VP9_SEGMENTATION_FLAG_TEMPORAL_UPDATE |
			   V4L2_VP9_SEGMENTATION_FLAG_UPDATE_DATA |
			   V4L2_VP9_SEGMENTATION_FLAG_ABS_OR_DELTA_UPDATE))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(seg->feature_enabled); i++) {
		if (seg->feature_enabled[i] &
		    ~V4L2_VP9_SEGMENT_FEATURE_ENABLED_MASK)
			return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(seg->feature_data); i++) {
		static const int range[] = { 255, 63, 3, 0 };

		for (j = 0; j < ARRAY_SIZE(seg->feature_data[j]); j++) {
			if (seg->feature_data[i][j] < -range[j] ||
			    seg->feature_data[i][j] > range[j])
				return -EINVAL;
		}
	}

	zero_reserved(*seg);
	return 0;
}

static int
validate_vp9_compressed_hdr(struct v4l2_ctrl_vp9_compressed_hdr *hdr)
{
	if (hdr->tx_mode > V4L2_VP9_TX_MODE_SELECT)
		return -EINVAL;

	return 0;
}

static int
validate_vp9_frame(struct v4l2_ctrl_vp9_frame *frame)
{
	int ret;

	/* Make sure we're not passed invalid flags. */
	if (frame->flags & ~(V4L2_VP9_FRAME_FLAG_KEY_FRAME |
		  V4L2_VP9_FRAME_FLAG_SHOW_FRAME |
		  V4L2_VP9_FRAME_FLAG_ERROR_RESILIENT |
		  V4L2_VP9_FRAME_FLAG_INTRA_ONLY |
		  V4L2_VP9_FRAME_FLAG_ALLOW_HIGH_PREC_MV |
		  V4L2_VP9_FRAME_FLAG_REFRESH_FRAME_CTX |
		  V4L2_VP9_FRAME_FLAG_PARALLEL_DEC_MODE |
		  V4L2_VP9_FRAME_FLAG_X_SUBSAMPLING |
		  V4L2_VP9_FRAME_FLAG_Y_SUBSAMPLING |
		  V4L2_VP9_FRAME_FLAG_COLOR_RANGE_FULL_SWING))
		return -EINVAL;

	if (frame->flags & V4L2_VP9_FRAME_FLAG_ERROR_RESILIENT &&
	    frame->flags & V4L2_VP9_FRAME_FLAG_REFRESH_FRAME_CTX)
		return -EINVAL;

	if (frame->profile > V4L2_VP9_PROFILE_MAX)
		return -EINVAL;

	if (frame->reset_frame_context > V4L2_VP9_RESET_FRAME_CTX_ALL)
		return -EINVAL;

	if (frame->frame_context_idx >= V4L2_VP9_NUM_FRAME_CTX)
		return -EINVAL;

	/*
	 * Profiles 0 and 1 only support 8-bit depth, profiles 2 and 3 only 10
	 * and 12 bit depths.
	 */
	if ((frame->profile < 2 && frame->bit_depth != 8) ||
	    (frame->profile >= 2 &&
	     (frame->bit_depth != 10 && frame->bit_depth != 12)))
		return -EINVAL;

	/* Profile 0 and 2 only accept YUV 4:2:0. */
	if ((frame->profile == 0 || frame->profile == 2) &&
	    (!(frame->flags & V4L2_VP9_FRAME_FLAG_X_SUBSAMPLING) ||
	     !(frame->flags & V4L2_VP9_FRAME_FLAG_Y_SUBSAMPLING)))
		return -EINVAL;

	/* Profile 1 and 3 only accept YUV 4:2:2, 4:4:0 and 4:4:4. */
	if ((frame->profile == 1 || frame->profile == 3) &&
	    ((frame->flags & V4L2_VP9_FRAME_FLAG_X_SUBSAMPLING) &&
	     (frame->flags & V4L2_VP9_FRAME_FLAG_Y_SUBSAMPLING)))
		return -EINVAL;

	if (frame->interpolation_filter > V4L2_VP9_INTERP_FILTER_SWITCHABLE)
		return -EINVAL;

	/*
	 * According to the spec, tile_cols_log2 shall be less than or equal
	 * to 6.
	 */
	if (frame->tile_cols_log2 > 6)
		return -EINVAL;

	if (frame->reference_mode > V4L2_VP9_REFERENCE_MODE_SELECT)
		return -EINVAL;

	ret = validate_vp9_lf_params(&frame->lf);
	if (ret)
		return ret;

	ret = validate_vp9_quant_params(&frame->quant);
	if (ret)
		return ret;

	ret = validate_vp9_seg_params(&frame->seg);
	if (ret)
		return ret;

	zero_reserved(*frame);
	return 0;
}

/*
 * Compound controls validation requires setting unused fields/flags to zero
 * in order to properly detect unchanged controls with std_equal's memcmp.
 */
static int std_validate_compound(const struct v4l2_ctrl *ctrl, u32 idx,
				 union v4l2_ctrl_ptr ptr)
{
	struct v4l2_ctrl_mpeg2_sequence *p_mpeg2_sequence;
	struct v4l2_ctrl_mpeg2_picture *p_mpeg2_picture;
	struct v4l2_ctrl_vp8_frame *p_vp8_frame;
	struct v4l2_ctrl_fwht_params *p_fwht_params;
	struct v4l2_ctrl_h264_sps *p_h264_sps;
	struct v4l2_ctrl_h264_pps *p_h264_pps;
	struct v4l2_ctrl_h264_pred_weights *p_h264_pred_weights;
	struct v4l2_ctrl_h264_slice_params *p_h264_slice_params;
	struct v4l2_ctrl_h264_decode_params *p_h264_dec_params;
	struct v4l2_ctrl_hevc_sps *p_hevc_sps;
	struct v4l2_ctrl_hevc_pps *p_hevc_pps;
	struct v4l2_ctrl_hevc_slice_params *p_hevc_slice_params;
	struct v4l2_ctrl_hdr10_mastering_display *p_hdr10_mastering;
	struct v4l2_ctrl_hevc_decode_params *p_hevc_decode_params;
	struct v4l2_area *area;
	void *p = ptr.p + idx * ctrl->elem_size;
	unsigned int i;

	switch ((u32)ctrl->type) {
	case V4L2_CTRL_TYPE_MPEG2_SEQUENCE:
		p_mpeg2_sequence = p;

		switch (p_mpeg2_sequence->chroma_format) {
		case 1: /* 4:2:0 */
		case 2: /* 4:2:2 */
		case 3: /* 4:4:4 */
			break;
		default:
			return -EINVAL;
		}
		break;

	case V4L2_CTRL_TYPE_MPEG2_PICTURE:
		p_mpeg2_picture = p;

		switch (p_mpeg2_picture->intra_dc_precision) {
		case 0: /* 8 bits */
		case 1: /* 9 bits */
		case 2: /* 10 bits */
		case 3: /* 11 bits */
			break;
		default:
			return -EINVAL;
		}

		switch (p_mpeg2_picture->picture_structure) {
		case V4L2_MPEG2_PIC_TOP_FIELD:
		case V4L2_MPEG2_PIC_BOTTOM_FIELD:
		case V4L2_MPEG2_PIC_FRAME:
			break;
		default:
			return -EINVAL;
		}

		switch (p_mpeg2_picture->picture_coding_type) {
		case V4L2_MPEG2_PIC_CODING_TYPE_I:
		case V4L2_MPEG2_PIC_CODING_TYPE_P:
		case V4L2_MPEG2_PIC_CODING_TYPE_B:
			break;
		default:
			return -EINVAL;
		}
		zero_reserved(*p_mpeg2_picture);
		break;

	case V4L2_CTRL_TYPE_MPEG2_QUANTISATION:
		break;

	case V4L2_CTRL_TYPE_FWHT_PARAMS:
		p_fwht_params = p;
		if (p_fwht_params->version < V4L2_FWHT_VERSION)
			return -EINVAL;
		if (!p_fwht_params->width || !p_fwht_params->height)
			return -EINVAL;
		break;

	case V4L2_CTRL_TYPE_H264_SPS:
		p_h264_sps = p;

		/* Some syntax elements are only conditionally valid */
		if (p_h264_sps->pic_order_cnt_type != 0) {
			p_h264_sps->log2_max_pic_order_cnt_lsb_minus4 = 0;
		} else if (p_h264_sps->pic_order_cnt_type != 1) {
			p_h264_sps->num_ref_frames_in_pic_order_cnt_cycle = 0;
			p_h264_sps->offset_for_non_ref_pic = 0;
			p_h264_sps->offset_for_top_to_bottom_field = 0;
			memset(&p_h264_sps->offset_for_ref_frame, 0,
			       sizeof(p_h264_sps->offset_for_ref_frame));
		}

		if (!V4L2_H264_SPS_HAS_CHROMA_FORMAT(p_h264_sps)) {
			p_h264_sps->chroma_format_idc = 1;
			p_h264_sps->bit_depth_luma_minus8 = 0;
			p_h264_sps->bit_depth_chroma_minus8 = 0;

			p_h264_sps->flags &=
				~V4L2_H264_SPS_FLAG_QPPRIME_Y_ZERO_TRANSFORM_BYPASS;

			if (p_h264_sps->chroma_format_idc < 3)
				p_h264_sps->flags &=
					~V4L2_H264_SPS_FLAG_SEPARATE_COLOUR_PLANE;
		}

		if (p_h264_sps->flags & V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY)
			p_h264_sps->flags &=
				~V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD;

		/*
		 * Chroma 4:2:2 format require at least High 4:2:2 profile.
		 *
		 * The H264 specification and well-known parser implementations
		 * use profile-idc values directly, as that is clearer and
		 * less ambiguous. We do the same here.
		 */
		if (p_h264_sps->profile_idc < 122 &&
		    p_h264_sps->chroma_format_idc > 1)
			return -EINVAL;
		/* Chroma 4:4:4 format require at least High 4:2:2 profile */
		if (p_h264_sps->profile_idc < 244 &&
		    p_h264_sps->chroma_format_idc > 2)
			return -EINVAL;
		if (p_h264_sps->chroma_format_idc > 3)
			return -EINVAL;

		if (p_h264_sps->bit_depth_luma_minus8 > 6)
			return -EINVAL;
		if (p_h264_sps->bit_depth_chroma_minus8 > 6)
			return -EINVAL;
		if (p_h264_sps->log2_max_frame_num_minus4 > 12)
			return -EINVAL;
		if (p_h264_sps->pic_order_cnt_type > 2)
			return -EINVAL;
		if (p_h264_sps->log2_max_pic_order_cnt_lsb_minus4 > 12)
			return -EINVAL;
		if (p_h264_sps->max_num_ref_frames > V4L2_H264_REF_LIST_LEN)
			return -EINVAL;
		break;

	case V4L2_CTRL_TYPE_H264_PPS:
		p_h264_pps = p;

		if (p_h264_pps->num_slice_groups_minus1 > 7)
			return -EINVAL;
		if (p_h264_pps->num_ref_idx_l0_default_active_minus1 >
		    (V4L2_H264_REF_LIST_LEN - 1))
			return -EINVAL;
		if (p_h264_pps->num_ref_idx_l1_default_active_minus1 >
		    (V4L2_H264_REF_LIST_LEN - 1))
			return -EINVAL;
		if (p_h264_pps->weighted_bipred_idc > 2)
			return -EINVAL;
		/*
		 * pic_init_qp_minus26 shall be in the range of
		 * -(26 + QpBdOffset_y) to +25, inclusive,
		 *  where QpBdOffset_y is 6 * bit_depth_luma_minus8
		 */
		if (p_h264_pps->pic_init_qp_minus26 < -62 ||
		    p_h264_pps->pic_init_qp_minus26 > 25)
			return -EINVAL;
		if (p_h264_pps->pic_init_qs_minus26 < -26 ||
		    p_h264_pps->pic_init_qs_minus26 > 25)
			return -EINVAL;
		if (p_h264_pps->chroma_qp_index_offset < -12 ||
		    p_h264_pps->chroma_qp_index_offset > 12)
			return -EINVAL;
		if (p_h264_pps->second_chroma_qp_index_offset < -12 ||
		    p_h264_pps->second_chroma_qp_index_offset > 12)
			return -EINVAL;
		break;

	case V4L2_CTRL_TYPE_H264_SCALING_MATRIX:
		break;

	case V4L2_CTRL_TYPE_H264_PRED_WEIGHTS:
		p_h264_pred_weights = p;

		if (p_h264_pred_weights->luma_log2_weight_denom > 7)
			return -EINVAL;
		if (p_h264_pred_weights->chroma_log2_weight_denom > 7)
			return -EINVAL;
		break;

	case V4L2_CTRL_TYPE_H264_SLICE_PARAMS:
		p_h264_slice_params = p;

		if (p_h264_slice_params->slice_type != V4L2_H264_SLICE_TYPE_B)
			p_h264_slice_params->flags &=
				~V4L2_H264_SLICE_FLAG_DIRECT_SPATIAL_MV_PRED;

		if (p_h264_slice_params->colour_plane_id > 2)
			return -EINVAL;
		if (p_h264_slice_params->cabac_init_idc > 2)
			return -EINVAL;
		if (p_h264_slice_params->disable_deblocking_filter_idc > 2)
			return -EINVAL;
		if (p_h264_slice_params->slice_alpha_c0_offset_div2 < -6 ||
		    p_h264_slice_params->slice_alpha_c0_offset_div2 > 6)
			return -EINVAL;
		if (p_h264_slice_params->slice_beta_offset_div2 < -6 ||
		    p_h264_slice_params->slice_beta_offset_div2 > 6)
			return -EINVAL;

		if (p_h264_slice_params->slice_type == V4L2_H264_SLICE_TYPE_I ||
		    p_h264_slice_params->slice_type == V4L2_H264_SLICE_TYPE_SI)
			p_h264_slice_params->num_ref_idx_l0_active_minus1 = 0;
		if (p_h264_slice_params->slice_type != V4L2_H264_SLICE_TYPE_B)
			p_h264_slice_params->num_ref_idx_l1_active_minus1 = 0;

		if (p_h264_slice_params->num_ref_idx_l0_active_minus1 >
		    (V4L2_H264_REF_LIST_LEN - 1))
			return -EINVAL;
		if (p_h264_slice_params->num_ref_idx_l1_active_minus1 >
		    (V4L2_H264_REF_LIST_LEN - 1))
			return -EINVAL;
		zero_reserved(*p_h264_slice_params);
		break;

	case V4L2_CTRL_TYPE_H264_DECODE_PARAMS:
		p_h264_dec_params = p;

		if (p_h264_dec_params->nal_ref_idc > 3)
			return -EINVAL;
		for (i = 0; i < V4L2_H264_NUM_DPB_ENTRIES; i++) {
			struct v4l2_h264_dpb_entry *dpb_entry =
				&p_h264_dec_params->dpb[i];

			zero_reserved(*dpb_entry);
		}
		zero_reserved(*p_h264_dec_params);
		break;

	case V4L2_CTRL_TYPE_VP8_FRAME:
		p_vp8_frame = p;

		switch (p_vp8_frame->num_dct_parts) {
		case 1:
		case 2:
		case 4:
		case 8:
			break;
		default:
			return -EINVAL;
		}
		zero_padding(p_vp8_frame->segment);
		zero_padding(p_vp8_frame->lf);
		zero_padding(p_vp8_frame->quant);
		zero_padding(p_vp8_frame->entropy);
		zero_padding(p_vp8_frame->coder_state);
		break;

	case V4L2_CTRL_TYPE_HEVC_SPS:
		p_hevc_sps = p;

		if (!(p_hevc_sps->flags & V4L2_HEVC_SPS_FLAG_PCM_ENABLED)) {
			p_hevc_sps->pcm_sample_bit_depth_luma_minus1 = 0;
			p_hevc_sps->pcm_sample_bit_depth_chroma_minus1 = 0;
			p_hevc_sps->log2_min_pcm_luma_coding_block_size_minus3 = 0;
			p_hevc_sps->log2_diff_max_min_pcm_luma_coding_block_size = 0;
		}

		if (!(p_hevc_sps->flags &
		      V4L2_HEVC_SPS_FLAG_LONG_TERM_REF_PICS_PRESENT))
			p_hevc_sps->num_long_term_ref_pics_sps = 0;
		break;

	case V4L2_CTRL_TYPE_HEVC_PPS:
		p_hevc_pps = p;

		if (!(p_hevc_pps->flags &
		      V4L2_HEVC_PPS_FLAG_CU_QP_DELTA_ENABLED))
			p_hevc_pps->diff_cu_qp_delta_depth = 0;

		if (!(p_hevc_pps->flags & V4L2_HEVC_PPS_FLAG_TILES_ENABLED)) {
			p_hevc_pps->num_tile_columns_minus1 = 0;
			p_hevc_pps->num_tile_rows_minus1 = 0;
			memset(&p_hevc_pps->column_width_minus1, 0,
			       sizeof(p_hevc_pps->column_width_minus1));
			memset(&p_hevc_pps->row_height_minus1, 0,
			       sizeof(p_hevc_pps->row_height_minus1));

			p_hevc_pps->flags &=
				~V4L2_HEVC_PPS_FLAG_LOOP_FILTER_ACROSS_TILES_ENABLED;
		}

		if (p_hevc_pps->flags &
		    V4L2_HEVC_PPS_FLAG_PPS_DISABLE_DEBLOCKING_FILTER) {
			p_hevc_pps->pps_beta_offset_div2 = 0;
			p_hevc_pps->pps_tc_offset_div2 = 0;
		}

		zero_padding(*p_hevc_pps);
		break;

	case V4L2_CTRL_TYPE_HEVC_DECODE_PARAMS:
		p_hevc_decode_params = p;

		if (p_hevc_decode_params->num_active_dpb_entries >
		    V4L2_HEVC_DPB_ENTRIES_NUM_MAX)
			return -EINVAL;

		for (i = 0; i < p_hevc_decode_params->num_active_dpb_entries;
		     i++) {
			struct v4l2_hevc_dpb_entry *dpb_entry =
				&p_hevc_decode_params->dpb[i];

			zero_padding(*dpb_entry);
		}
		break;

	case V4L2_CTRL_TYPE_HEVC_SLICE_PARAMS:
		p_hevc_slice_params = p;

		zero_padding(p_hevc_slice_params->pred_weight_table);
		zero_padding(*p_hevc_slice_params);
		break;

	case V4L2_CTRL_TYPE_HDR10_CLL_INFO:
		break;

	case V4L2_CTRL_TYPE_HDR10_MASTERING_DISPLAY:
		p_hdr10_mastering = p;

		for (i = 0; i < 3; ++i) {
			if (p_hdr10_mastering->display_primaries_x[i] <
				V4L2_HDR10_MASTERING_PRIMARIES_X_LOW ||
			    p_hdr10_mastering->display_primaries_x[i] >
				V4L2_HDR10_MASTERING_PRIMARIES_X_HIGH ||
			    p_hdr10_mastering->display_primaries_y[i] <
				V4L2_HDR10_MASTERING_PRIMARIES_Y_LOW ||
			    p_hdr10_mastering->display_primaries_y[i] >
				V4L2_HDR10_MASTERING_PRIMARIES_Y_HIGH)
				return -EINVAL;
		}

		if (p_hdr10_mastering->white_point_x <
			V4L2_HDR10_MASTERING_WHITE_POINT_X_LOW ||
		    p_hdr10_mastering->white_point_x >
			V4L2_HDR10_MASTERING_WHITE_POINT_X_HIGH ||
		    p_hdr10_mastering->white_point_y <
			V4L2_HDR10_MASTERING_WHITE_POINT_Y_LOW ||
		    p_hdr10_mastering->white_point_y >
			V4L2_HDR10_MASTERING_WHITE_POINT_Y_HIGH)
			return -EINVAL;

		if (p_hdr10_mastering->max_display_mastering_luminance <
			V4L2_HDR10_MASTERING_MAX_LUMA_LOW ||
		    p_hdr10_mastering->max_display_mastering_luminance >
			V4L2_HDR10_MASTERING_MAX_LUMA_HIGH ||
		    p_hdr10_mastering->min_display_mastering_luminance <
			V4L2_HDR10_MASTERING_MIN_LUMA_LOW ||
		    p_hdr10_mastering->min_display_mastering_luminance >
			V4L2_HDR10_MASTERING_MIN_LUMA_HIGH)
			return -EINVAL;

		/* The following restriction comes from ITU-T Rec. H.265 spec */
		if (p_hdr10_mastering->max_display_mastering_luminance ==
			V4L2_HDR10_MASTERING_MAX_LUMA_LOW &&
		    p_hdr10_mastering->min_display_mastering_luminance ==
			V4L2_HDR10_MASTERING_MIN_LUMA_HIGH)
			return -EINVAL;

		break;

	case V4L2_CTRL_TYPE_HEVC_SCALING_MATRIX:
		break;

	case V4L2_CTRL_TYPE_VP9_COMPRESSED_HDR:
		return validate_vp9_compressed_hdr(p);

	case V4L2_CTRL_TYPE_VP9_FRAME:
		return validate_vp9_frame(p);

	case V4L2_CTRL_TYPE_AREA:
		area = p;
		if (!area->width || !area->height)
			return -EINVAL;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int std_validate(const struct v4l2_ctrl *ctrl, u32 idx,
			union v4l2_ctrl_ptr ptr)
{
	size_t len;
	u64 offset;
	s64 val;

	switch ((u32)ctrl->type) {
	case V4L2_CTRL_TYPE_INTEGER:
		return ROUND_TO_RANGE(ptr.p_s32[idx], u32, ctrl);
	case V4L2_CTRL_TYPE_INTEGER64:
		/*
		 * We can't use the ROUND_TO_RANGE define here due to
		 * the u64 divide that needs special care.
		 */
		val = ptr.p_s64[idx];
		if (ctrl->maximum >= 0 && val >= ctrl->maximum - (s64)(ctrl->step / 2))
			val = ctrl->maximum;
		else
			val += (s64)(ctrl->step / 2);
		val = clamp_t(s64, val, ctrl->minimum, ctrl->maximum);
		offset = val - ctrl->minimum;
		do_div(offset, ctrl->step);
		ptr.p_s64[idx] = ctrl->minimum + offset * ctrl->step;
		return 0;
	case V4L2_CTRL_TYPE_U8:
		return ROUND_TO_RANGE(ptr.p_u8[idx], u8, ctrl);
	case V4L2_CTRL_TYPE_U16:
		return ROUND_TO_RANGE(ptr.p_u16[idx], u16, ctrl);
	case V4L2_CTRL_TYPE_U32:
		return ROUND_TO_RANGE(ptr.p_u32[idx], u32, ctrl);

	case V4L2_CTRL_TYPE_BOOLEAN:
		ptr.p_s32[idx] = !!ptr.p_s32[idx];
		return 0;

	case V4L2_CTRL_TYPE_MENU:
	case V4L2_CTRL_TYPE_INTEGER_MENU:
		if (ptr.p_s32[idx] < ctrl->minimum || ptr.p_s32[idx] > ctrl->maximum)
			return -ERANGE;
		if (ptr.p_s32[idx] < BITS_PER_LONG_LONG &&
		    (ctrl->menu_skip_mask & BIT_ULL(ptr.p_s32[idx])))
			return -EINVAL;
		if (ctrl->type == V4L2_CTRL_TYPE_MENU &&
		    ctrl->qmenu[ptr.p_s32[idx]][0] == '\0')
			return -EINVAL;
		return 0;

	case V4L2_CTRL_TYPE_BITMASK:
		ptr.p_s32[idx] &= ctrl->maximum;
		return 0;

	case V4L2_CTRL_TYPE_BUTTON:
	case V4L2_CTRL_TYPE_CTRL_CLASS:
		ptr.p_s32[idx] = 0;
		return 0;

	case V4L2_CTRL_TYPE_STRING:
		idx *= ctrl->elem_size;
		len = strlen(ptr.p_char + idx);
		if (len < ctrl->minimum)
			return -ERANGE;
		if ((len - (u32)ctrl->minimum) % (u32)ctrl->step)
			return -ERANGE;
		return 0;

	default:
		return std_validate_compound(ctrl, idx, ptr);
	}
}

static const struct v4l2_ctrl_type_ops std_type_ops = {
	.equal = std_equal,
	.init = std_init,
	.log = std_log,
	.validate = std_validate,
};

void v4l2_ctrl_notify(struct v4l2_ctrl *ctrl, v4l2_ctrl_notify_fnc notify, void *priv)
{
	if (!ctrl)
		return;
	if (!notify) {
		ctrl->call_notify = 0;
		return;
	}
	if (WARN_ON(ctrl->handler->notify && ctrl->handler->notify != notify))
		return;
	ctrl->handler->notify = notify;
	ctrl->handler->notify_priv = priv;
	ctrl->call_notify = 1;
}
EXPORT_SYMBOL(v4l2_ctrl_notify);

/* Copy the one value to another. */
static void ptr_to_ptr(struct v4l2_ctrl *ctrl,
		       union v4l2_ctrl_ptr from, union v4l2_ctrl_ptr to)
{
	if (ctrl == NULL)
		return;
	memcpy(to.p, from.p_const, ctrl->elems * ctrl->elem_size);
}

/* Copy the new value to the current value. */
void new_to_cur(struct v4l2_fh *fh, struct v4l2_ctrl *ctrl, u32 ch_flags)
{
	bool changed;

	if (ctrl == NULL)
		return;

	/* has_changed is set by cluster_changed */
	changed = ctrl->has_changed;
	if (changed)
		ptr_to_ptr(ctrl, ctrl->p_new, ctrl->p_cur);

	if (ch_flags & V4L2_EVENT_CTRL_CH_FLAGS) {
		/* Note: CH_FLAGS is only set for auto clusters. */
		ctrl->flags &=
			~(V4L2_CTRL_FLAG_INACTIVE | V4L2_CTRL_FLAG_VOLATILE);
		if (!is_cur_manual(ctrl->cluster[0])) {
			ctrl->flags |= V4L2_CTRL_FLAG_INACTIVE;
			if (ctrl->cluster[0]->has_volatiles)
				ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
		}
		fh = NULL;
	}
	if (changed || ch_flags) {
		/* If a control was changed that was not one of the controls
		   modified by the application, then send the event to all. */
		if (!ctrl->is_new)
			fh = NULL;
		send_event(fh, ctrl,
			(changed ? V4L2_EVENT_CTRL_CH_VALUE : 0) | ch_flags);
		if (ctrl->call_notify && changed && ctrl->handler->notify)
			ctrl->handler->notify(ctrl, ctrl->handler->notify_priv);
	}
}

/* Copy the current value to the new value */
void cur_to_new(struct v4l2_ctrl *ctrl)
{
	if (ctrl == NULL)
		return;
	ptr_to_ptr(ctrl, ctrl->p_cur, ctrl->p_new);
}

/* Copy the new value to the request value */
void new_to_req(struct v4l2_ctrl_ref *ref)
{
	if (!ref)
		return;
	ptr_to_ptr(ref->ctrl, ref->ctrl->p_new, ref->p_req);
	ref->valid_p_req = true;
}

/* Copy the current value to the request value */
void cur_to_req(struct v4l2_ctrl_ref *ref)
{
	if (!ref)
		return;
	ptr_to_ptr(ref->ctrl, ref->ctrl->p_cur, ref->p_req);
	ref->valid_p_req = true;
}

/* Copy the request value to the new value */
void req_to_new(struct v4l2_ctrl_ref *ref)
{
	if (!ref)
		return;
	if (ref->valid_p_req)
		ptr_to_ptr(ref->ctrl, ref->p_req, ref->ctrl->p_new);
	else
		ptr_to_ptr(ref->ctrl, ref->ctrl->p_cur, ref->ctrl->p_new);
}

/* Control range checking */
int check_range(enum v4l2_ctrl_type type,
		s64 min, s64 max, u64 step, s64 def)
{
	switch (type) {
	case V4L2_CTRL_TYPE_BOOLEAN:
		if (step != 1 || max > 1 || min < 0)
			return -ERANGE;
		fallthrough;
	case V4L2_CTRL_TYPE_U8:
	case V4L2_CTRL_TYPE_U16:
	case V4L2_CTRL_TYPE_U32:
	case V4L2_CTRL_TYPE_INTEGER:
	case V4L2_CTRL_TYPE_INTEGER64:
		if (step == 0 || min > max || def < min || def > max)
			return -ERANGE;
		return 0;
	case V4L2_CTRL_TYPE_BITMASK:
		if (step || min || !max || (def & ~max))
			return -ERANGE;
		return 0;
	case V4L2_CTRL_TYPE_MENU:
	case V4L2_CTRL_TYPE_INTEGER_MENU:
		if (min > max || def < min || def > max)
			return -ERANGE;
		/* Note: step == menu_skip_mask for menu controls.
		   So here we check if the default value is masked out. */
		if (step && ((1 << def) & step))
			return -EINVAL;
		return 0;
	case V4L2_CTRL_TYPE_STRING:
		if (min > max || min < 0 || step < 1 || def)
			return -ERANGE;
		return 0;
	default:
		return 0;
	}
}

/* Validate a new control */
int validate_new(const struct v4l2_ctrl *ctrl, union v4l2_ctrl_ptr p_new)
{
	unsigned idx;
	int err = 0;

	for (idx = 0; !err && idx < ctrl->elems; idx++)
		err = ctrl->type_ops->validate(ctrl, idx, p_new);
	return err;
}

/* Set the handler's error code if it wasn't set earlier already */
static inline int handler_set_err(struct v4l2_ctrl_handler *hdl, int err)
{
	if (hdl->error == 0)
		hdl->error = err;
	return err;
}

/* Initialize the handler */
int v4l2_ctrl_handler_init_class(struct v4l2_ctrl_handler *hdl,
				 unsigned nr_of_controls_hint,
				 struct lock_class_key *key, const char *name)
{
	mutex_init(&hdl->_lock);
	hdl->lock = &hdl->_lock;
	lockdep_set_class_and_name(hdl->lock, key, name);
	INIT_LIST_HEAD(&hdl->ctrls);
	INIT_LIST_HEAD(&hdl->ctrl_refs);
	hdl->nr_of_buckets = 1 + nr_of_controls_hint / 8;
	hdl->buckets = kvcalloc(hdl->nr_of_buckets, sizeof(hdl->buckets[0]),
				GFP_KERNEL);
	hdl->error = hdl->buckets ? 0 : -ENOMEM;
	v4l2_ctrl_handler_init_request(hdl);
	return hdl->error;
}
EXPORT_SYMBOL(v4l2_ctrl_handler_init_class);

/* Free all controls and control refs */
void v4l2_ctrl_handler_free(struct v4l2_ctrl_handler *hdl)
{
	struct v4l2_ctrl_ref *ref, *next_ref;
	struct v4l2_ctrl *ctrl, *next_ctrl;
	struct v4l2_subscribed_event *sev, *next_sev;

	if (hdl == NULL || hdl->buckets == NULL)
		return;

	v4l2_ctrl_handler_free_request(hdl);

	mutex_lock(hdl->lock);
	/* Free all nodes */
	list_for_each_entry_safe(ref, next_ref, &hdl->ctrl_refs, node) {
		list_del(&ref->node);
		kfree(ref);
	}
	/* Free all controls owned by the handler */
	list_for_each_entry_safe(ctrl, next_ctrl, &hdl->ctrls, node) {
		list_del(&ctrl->node);
		list_for_each_entry_safe(sev, next_sev, &ctrl->ev_subs, node)
			list_del(&sev->node);
		kvfree(ctrl);
	}
	kvfree(hdl->buckets);
	hdl->buckets = NULL;
	hdl->cached = NULL;
	hdl->error = 0;
	mutex_unlock(hdl->lock);
	mutex_destroy(&hdl->_lock);
}
EXPORT_SYMBOL(v4l2_ctrl_handler_free);

/* For backwards compatibility: V4L2_CID_PRIVATE_BASE should no longer
   be used except in G_CTRL, S_CTRL, QUERYCTRL and QUERYMENU when dealing
   with applications that do not use the NEXT_CTRL flag.

   We just find the n-th private user control. It's O(N), but that should not
   be an issue in this particular case. */
static struct v4l2_ctrl_ref *find_private_ref(
		struct v4l2_ctrl_handler *hdl, u32 id)
{
	struct v4l2_ctrl_ref *ref;

	id -= V4L2_CID_PRIVATE_BASE;
	list_for_each_entry(ref, &hdl->ctrl_refs, node) {
		/* Search for private user controls that are compatible with
		   VIDIOC_G/S_CTRL. */
		if (V4L2_CTRL_ID2WHICH(ref->ctrl->id) == V4L2_CTRL_CLASS_USER &&
		    V4L2_CTRL_DRIVER_PRIV(ref->ctrl->id)) {
			if (!ref->ctrl->is_int)
				continue;
			if (id == 0)
				return ref;
			id--;
		}
	}
	return NULL;
}

/* Find a control with the given ID. */
struct v4l2_ctrl_ref *find_ref(struct v4l2_ctrl_handler *hdl, u32 id)
{
	struct v4l2_ctrl_ref *ref;
	int bucket;

	id &= V4L2_CTRL_ID_MASK;

	/* Old-style private controls need special handling */
	if (id >= V4L2_CID_PRIVATE_BASE)
		return find_private_ref(hdl, id);
	bucket = id % hdl->nr_of_buckets;

	/* Simple optimization: cache the last control found */
	if (hdl->cached && hdl->cached->ctrl->id == id)
		return hdl->cached;

	/* Not in cache, search the hash */
	ref = hdl->buckets ? hdl->buckets[bucket] : NULL;
	while (ref && ref->ctrl->id != id)
		ref = ref->next;

	if (ref)
		hdl->cached = ref; /* cache it! */
	return ref;
}

/* Find a control with the given ID. Take the handler's lock first. */
struct v4l2_ctrl_ref *find_ref_lock(struct v4l2_ctrl_handler *hdl, u32 id)
{
	struct v4l2_ctrl_ref *ref = NULL;

	if (hdl) {
		mutex_lock(hdl->lock);
		ref = find_ref(hdl, id);
		mutex_unlock(hdl->lock);
	}
	return ref;
}

/* Find a control with the given ID. */
struct v4l2_ctrl *v4l2_ctrl_find(struct v4l2_ctrl_handler *hdl, u32 id)
{
	struct v4l2_ctrl_ref *ref = find_ref_lock(hdl, id);

	return ref ? ref->ctrl : NULL;
}
EXPORT_SYMBOL(v4l2_ctrl_find);

/* Allocate a new v4l2_ctrl_ref and hook it into the handler. */
int handler_new_ref(struct v4l2_ctrl_handler *hdl,
		    struct v4l2_ctrl *ctrl,
		    struct v4l2_ctrl_ref **ctrl_ref,
		    bool from_other_dev, bool allocate_req)
{
	struct v4l2_ctrl_ref *ref;
	struct v4l2_ctrl_ref *new_ref;
	u32 id = ctrl->id;
	u32 class_ctrl = V4L2_CTRL_ID2WHICH(id) | 1;
	int bucket = id % hdl->nr_of_buckets;	/* which bucket to use */
	unsigned int size_extra_req = 0;

	if (ctrl_ref)
		*ctrl_ref = NULL;

	/*
	 * Automatically add the control class if it is not yet present and
	 * the new control is not a compound control.
	 */
	if (ctrl->type < V4L2_CTRL_COMPOUND_TYPES &&
	    id != class_ctrl && find_ref_lock(hdl, class_ctrl) == NULL)
		if (!v4l2_ctrl_new_std(hdl, NULL, class_ctrl, 0, 0, 0, 0))
			return hdl->error;

	if (hdl->error)
		return hdl->error;

	if (allocate_req)
		size_extra_req = ctrl->elems * ctrl->elem_size;
	new_ref = kzalloc(sizeof(*new_ref) + size_extra_req, GFP_KERNEL);
	if (!new_ref)
		return handler_set_err(hdl, -ENOMEM);
	new_ref->ctrl = ctrl;
	new_ref->from_other_dev = from_other_dev;
	if (size_extra_req)
		new_ref->p_req.p = &new_ref[1];

	INIT_LIST_HEAD(&new_ref->node);

	mutex_lock(hdl->lock);

	/* Add immediately at the end of the list if the list is empty, or if
	   the last element in the list has a lower ID.
	   This ensures that when elements are added in ascending order the
	   insertion is an O(1) operation. */
	if (list_empty(&hdl->ctrl_refs) || id > node2id(hdl->ctrl_refs.prev)) {
		list_add_tail(&new_ref->node, &hdl->ctrl_refs);
		goto insert_in_hash;
	}

	/* Find insert position in sorted list */
	list_for_each_entry(ref, &hdl->ctrl_refs, node) {
		if (ref->ctrl->id < id)
			continue;
		/* Don't add duplicates */
		if (ref->ctrl->id == id) {
			kfree(new_ref);
			goto unlock;
		}
		list_add(&new_ref->node, ref->node.prev);
		break;
	}

insert_in_hash:
	/* Insert the control node in the hash */
	new_ref->next = hdl->buckets[bucket];
	hdl->buckets[bucket] = new_ref;
	if (ctrl_ref)
		*ctrl_ref = new_ref;
	if (ctrl->handler == hdl) {
		/* By default each control starts in a cluster of its own.
		 * new_ref->ctrl is basically a cluster array with one
		 * element, so that's perfect to use as the cluster pointer.
		 * But only do this for the handler that owns the control.
		 */
		ctrl->cluster = &new_ref->ctrl;
		ctrl->ncontrols = 1;
	}

unlock:
	mutex_unlock(hdl->lock);
	return 0;
}

/* Add a new control */
static struct v4l2_ctrl *v4l2_ctrl_new(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops,
			const struct v4l2_ctrl_type_ops *type_ops,
			u32 id, const char *name, enum v4l2_ctrl_type type,
			s64 min, s64 max, u64 step, s64 def,
			const u32 dims[V4L2_CTRL_MAX_DIMS], u32 elem_size,
			u32 flags, const char * const *qmenu,
			const s64 *qmenu_int, const union v4l2_ctrl_ptr p_def,
			void *priv)
{
	struct v4l2_ctrl *ctrl;
	unsigned sz_extra;
	unsigned nr_of_dims = 0;
	unsigned elems = 1;
	bool is_array;
	unsigned tot_ctrl_size;
	unsigned idx;
	void *data;
	int err;

	if (hdl->error)
		return NULL;

	while (dims && dims[nr_of_dims]) {
		elems *= dims[nr_of_dims];
		nr_of_dims++;
		if (nr_of_dims == V4L2_CTRL_MAX_DIMS)
			break;
	}
	is_array = nr_of_dims > 0;

	/* Prefill elem_size for all types handled by std_type_ops */
	switch ((u32)type) {
	case V4L2_CTRL_TYPE_INTEGER64:
		elem_size = sizeof(s64);
		break;
	case V4L2_CTRL_TYPE_STRING:
		elem_size = max + 1;
		break;
	case V4L2_CTRL_TYPE_U8:
		elem_size = sizeof(u8);
		break;
	case V4L2_CTRL_TYPE_U16:
		elem_size = sizeof(u16);
		break;
	case V4L2_CTRL_TYPE_U32:
		elem_size = sizeof(u32);
		break;
	case V4L2_CTRL_TYPE_MPEG2_SEQUENCE:
		elem_size = sizeof(struct v4l2_ctrl_mpeg2_sequence);
		break;
	case V4L2_CTRL_TYPE_MPEG2_PICTURE:
		elem_size = sizeof(struct v4l2_ctrl_mpeg2_picture);
		break;
	case V4L2_CTRL_TYPE_MPEG2_QUANTISATION:
		elem_size = sizeof(struct v4l2_ctrl_mpeg2_quantisation);
		break;
	case V4L2_CTRL_TYPE_FWHT_PARAMS:
		elem_size = sizeof(struct v4l2_ctrl_fwht_params);
		break;
	case V4L2_CTRL_TYPE_H264_SPS:
		elem_size = sizeof(struct v4l2_ctrl_h264_sps);
		break;
	case V4L2_CTRL_TYPE_H264_PPS:
		elem_size = sizeof(struct v4l2_ctrl_h264_pps);
		break;
	case V4L2_CTRL_TYPE_H264_SCALING_MATRIX:
		elem_size = sizeof(struct v4l2_ctrl_h264_scaling_matrix);
		break;
	case V4L2_CTRL_TYPE_H264_SLICE_PARAMS:
		elem_size = sizeof(struct v4l2_ctrl_h264_slice_params);
		break;
	case V4L2_CTRL_TYPE_H264_DECODE_PARAMS:
		elem_size = sizeof(struct v4l2_ctrl_h264_decode_params);
		break;
	case V4L2_CTRL_TYPE_H264_PRED_WEIGHTS:
		elem_size = sizeof(struct v4l2_ctrl_h264_pred_weights);
		break;
	case V4L2_CTRL_TYPE_VP8_FRAME:
		elem_size = sizeof(struct v4l2_ctrl_vp8_frame);
		break;
	case V4L2_CTRL_TYPE_HEVC_SPS:
		elem_size = sizeof(struct v4l2_ctrl_hevc_sps);
		break;
	case V4L2_CTRL_TYPE_HEVC_PPS:
		elem_size = sizeof(struct v4l2_ctrl_hevc_pps);
		break;
	case V4L2_CTRL_TYPE_HEVC_SLICE_PARAMS:
		elem_size = sizeof(struct v4l2_ctrl_hevc_slice_params);
		break;
	case V4L2_CTRL_TYPE_HEVC_SCALING_MATRIX:
		elem_size = sizeof(struct v4l2_ctrl_hevc_scaling_matrix);
		break;
	case V4L2_CTRL_TYPE_HEVC_DECODE_PARAMS:
		elem_size = sizeof(struct v4l2_ctrl_hevc_decode_params);
		break;
	case V4L2_CTRL_TYPE_HDR10_CLL_INFO:
		elem_size = sizeof(struct v4l2_ctrl_hdr10_cll_info);
		break;
	case V4L2_CTRL_TYPE_HDR10_MASTERING_DISPLAY:
		elem_size = sizeof(struct v4l2_ctrl_hdr10_mastering_display);
		break;
	case V4L2_CTRL_TYPE_VP9_COMPRESSED_HDR:
		elem_size = sizeof(struct v4l2_ctrl_vp9_compressed_hdr);
		break;
	case V4L2_CTRL_TYPE_VP9_FRAME:
		elem_size = sizeof(struct v4l2_ctrl_vp9_frame);
		break;
	case V4L2_CTRL_TYPE_AREA:
		elem_size = sizeof(struct v4l2_area);
		break;
	default:
		if (type < V4L2_CTRL_COMPOUND_TYPES)
			elem_size = sizeof(s32);
		break;
	}
	tot_ctrl_size = elem_size * elems;

	/* Sanity checks */
	if (id == 0 || name == NULL || !elem_size ||
	    id >= V4L2_CID_PRIVATE_BASE ||
	    (type == V4L2_CTRL_TYPE_MENU && qmenu == NULL) ||
	    (type == V4L2_CTRL_TYPE_INTEGER_MENU && qmenu_int == NULL)) {
		handler_set_err(hdl, -ERANGE);
		return NULL;
	}
	err = check_range(type, min, max, step, def);
	if (err) {
		handler_set_err(hdl, err);
		return NULL;
	}
	if (is_array &&
	    (type == V4L2_CTRL_TYPE_BUTTON ||
	     type == V4L2_CTRL_TYPE_CTRL_CLASS)) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}

	sz_extra = 0;
	if (type == V4L2_CTRL_TYPE_BUTTON)
		flags |= V4L2_CTRL_FLAG_WRITE_ONLY |
			V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	else if (type == V4L2_CTRL_TYPE_CTRL_CLASS)
		flags |= V4L2_CTRL_FLAG_READ_ONLY;
	else if (type == V4L2_CTRL_TYPE_INTEGER64 ||
		 type == V4L2_CTRL_TYPE_STRING ||
		 type >= V4L2_CTRL_COMPOUND_TYPES ||
		 is_array)
		sz_extra += 2 * tot_ctrl_size;

	if (type >= V4L2_CTRL_COMPOUND_TYPES && p_def.p_const)
		sz_extra += elem_size;

	ctrl = kvzalloc(sizeof(*ctrl) + sz_extra, GFP_KERNEL);
	if (ctrl == NULL) {
		handler_set_err(hdl, -ENOMEM);
		return NULL;
	}

	INIT_LIST_HEAD(&ctrl->node);
	INIT_LIST_HEAD(&ctrl->ev_subs);
	ctrl->handler = hdl;
	ctrl->ops = ops;
	ctrl->type_ops = type_ops ? type_ops : &std_type_ops;
	ctrl->id = id;
	ctrl->name = name;
	ctrl->type = type;
	ctrl->flags = flags;
	ctrl->minimum = min;
	ctrl->maximum = max;
	ctrl->step = step;
	ctrl->default_value = def;
	ctrl->is_string = !is_array && type == V4L2_CTRL_TYPE_STRING;
	ctrl->is_ptr = is_array || type >= V4L2_CTRL_COMPOUND_TYPES || ctrl->is_string;
	ctrl->is_int = !ctrl->is_ptr && type != V4L2_CTRL_TYPE_INTEGER64;
	ctrl->is_array = is_array;
	ctrl->elems = elems;
	ctrl->nr_of_dims = nr_of_dims;
	if (nr_of_dims)
		memcpy(ctrl->dims, dims, nr_of_dims * sizeof(dims[0]));
	ctrl->elem_size = elem_size;
	if (type == V4L2_CTRL_TYPE_MENU)
		ctrl->qmenu = qmenu;
	else if (type == V4L2_CTRL_TYPE_INTEGER_MENU)
		ctrl->qmenu_int = qmenu_int;
	ctrl->priv = priv;
	ctrl->cur.val = ctrl->val = def;
	data = &ctrl[1];

	if (!ctrl->is_int) {
		ctrl->p_new.p = data;
		ctrl->p_cur.p = data + tot_ctrl_size;
	} else {
		ctrl->p_new.p = &ctrl->val;
		ctrl->p_cur.p = &ctrl->cur.val;
	}

	if (type >= V4L2_CTRL_COMPOUND_TYPES && p_def.p_const) {
		ctrl->p_def.p = ctrl->p_cur.p + tot_ctrl_size;
		memcpy(ctrl->p_def.p, p_def.p_const, elem_size);
	}

	for (idx = 0; idx < elems; idx++) {
		ctrl->type_ops->init(ctrl, idx, ctrl->p_cur);
		ctrl->type_ops->init(ctrl, idx, ctrl->p_new);
	}

	if (handler_new_ref(hdl, ctrl, NULL, false, false)) {
		kvfree(ctrl);
		return NULL;
	}
	mutex_lock(hdl->lock);
	list_add_tail(&ctrl->node, &hdl->ctrls);
	mutex_unlock(hdl->lock);
	return ctrl;
}

struct v4l2_ctrl *v4l2_ctrl_new_custom(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_config *cfg, void *priv)
{
	bool is_menu;
	struct v4l2_ctrl *ctrl;
	const char *name = cfg->name;
	const char * const *qmenu = cfg->qmenu;
	const s64 *qmenu_int = cfg->qmenu_int;
	enum v4l2_ctrl_type type = cfg->type;
	u32 flags = cfg->flags;
	s64 min = cfg->min;
	s64 max = cfg->max;
	u64 step = cfg->step;
	s64 def = cfg->def;

	if (name == NULL)
		v4l2_ctrl_fill(cfg->id, &name, &type, &min, &max, &step,
								&def, &flags);

	is_menu = (type == V4L2_CTRL_TYPE_MENU ||
		   type == V4L2_CTRL_TYPE_INTEGER_MENU);
	if (is_menu)
		WARN_ON(step);
	else
		WARN_ON(cfg->menu_skip_mask);
	if (type == V4L2_CTRL_TYPE_MENU && !qmenu) {
		qmenu = v4l2_ctrl_get_menu(cfg->id);
	} else if (type == V4L2_CTRL_TYPE_INTEGER_MENU && !qmenu_int) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}

	ctrl = v4l2_ctrl_new(hdl, cfg->ops, cfg->type_ops, cfg->id, name,
			type, min, max,
			is_menu ? cfg->menu_skip_mask : step, def,
			cfg->dims, cfg->elem_size,
			flags, qmenu, qmenu_int, cfg->p_def, priv);
	if (ctrl)
		ctrl->is_private = cfg->is_private;
	return ctrl;
}
EXPORT_SYMBOL(v4l2_ctrl_new_custom);

/* Helper function for standard non-menu controls */
struct v4l2_ctrl *v4l2_ctrl_new_std(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops,
			u32 id, s64 min, s64 max, u64 step, s64 def)
{
	const char *name;
	enum v4l2_ctrl_type type;
	u32 flags;

	v4l2_ctrl_fill(id, &name, &type, &min, &max, &step, &def, &flags);
	if (type == V4L2_CTRL_TYPE_MENU ||
	    type == V4L2_CTRL_TYPE_INTEGER_MENU ||
	    type >= V4L2_CTRL_COMPOUND_TYPES) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}
	return v4l2_ctrl_new(hdl, ops, NULL, id, name, type,
			     min, max, step, def, NULL, 0,
			     flags, NULL, NULL, ptr_null, NULL);
}
EXPORT_SYMBOL(v4l2_ctrl_new_std);

/* Helper function for standard menu controls */
struct v4l2_ctrl *v4l2_ctrl_new_std_menu(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops,
			u32 id, u8 _max, u64 mask, u8 _def)
{
	const char * const *qmenu = NULL;
	const s64 *qmenu_int = NULL;
	unsigned int qmenu_int_len = 0;
	const char *name;
	enum v4l2_ctrl_type type;
	s64 min;
	s64 max = _max;
	s64 def = _def;
	u64 step;
	u32 flags;

	v4l2_ctrl_fill(id, &name, &type, &min, &max, &step, &def, &flags);

	if (type == V4L2_CTRL_TYPE_MENU)
		qmenu = v4l2_ctrl_get_menu(id);
	else if (type == V4L2_CTRL_TYPE_INTEGER_MENU)
		qmenu_int = v4l2_ctrl_get_int_menu(id, &qmenu_int_len);

	if ((!qmenu && !qmenu_int) || (qmenu_int && max > qmenu_int_len)) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}
	return v4l2_ctrl_new(hdl, ops, NULL, id, name, type,
			     0, max, mask, def, NULL, 0,
			     flags, qmenu, qmenu_int, ptr_null, NULL);
}
EXPORT_SYMBOL(v4l2_ctrl_new_std_menu);

/* Helper function for standard menu controls with driver defined menu */
struct v4l2_ctrl *v4l2_ctrl_new_std_menu_items(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops, u32 id, u8 _max,
			u64 mask, u8 _def, const char * const *qmenu)
{
	enum v4l2_ctrl_type type;
	const char *name;
	u32 flags;
	u64 step;
	s64 min;
	s64 max = _max;
	s64 def = _def;

	/* v4l2_ctrl_new_std_menu_items() should only be called for
	 * standard controls without a standard menu.
	 */
	if (v4l2_ctrl_get_menu(id)) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}

	v4l2_ctrl_fill(id, &name, &type, &min, &max, &step, &def, &flags);
	if (type != V4L2_CTRL_TYPE_MENU || qmenu == NULL) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}
	return v4l2_ctrl_new(hdl, ops, NULL, id, name, type,
			     0, max, mask, def, NULL, 0,
			     flags, qmenu, NULL, ptr_null, NULL);

}
EXPORT_SYMBOL(v4l2_ctrl_new_std_menu_items);

/* Helper function for standard compound controls */
struct v4l2_ctrl *v4l2_ctrl_new_std_compound(struct v4l2_ctrl_handler *hdl,
				const struct v4l2_ctrl_ops *ops, u32 id,
				const union v4l2_ctrl_ptr p_def)
{
	const char *name;
	enum v4l2_ctrl_type type;
	u32 flags;
	s64 min, max, step, def;

	v4l2_ctrl_fill(id, &name, &type, &min, &max, &step, &def, &flags);
	if (type < V4L2_CTRL_COMPOUND_TYPES) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}
	return v4l2_ctrl_new(hdl, ops, NULL, id, name, type,
			     min, max, step, def, NULL, 0,
			     flags, NULL, NULL, p_def, NULL);
}
EXPORT_SYMBOL(v4l2_ctrl_new_std_compound);

/* Helper function for standard integer menu controls */
struct v4l2_ctrl *v4l2_ctrl_new_int_menu(struct v4l2_ctrl_handler *hdl,
			const struct v4l2_ctrl_ops *ops,
			u32 id, u8 _max, u8 _def, const s64 *qmenu_int)
{
	const char *name;
	enum v4l2_ctrl_type type;
	s64 min;
	u64 step;
	s64 max = _max;
	s64 def = _def;
	u32 flags;

	v4l2_ctrl_fill(id, &name, &type, &min, &max, &step, &def, &flags);
	if (type != V4L2_CTRL_TYPE_INTEGER_MENU) {
		handler_set_err(hdl, -EINVAL);
		return NULL;
	}
	return v4l2_ctrl_new(hdl, ops, NULL, id, name, type,
			     0, max, 0, def, NULL, 0,
			     flags, NULL, qmenu_int, ptr_null, NULL);
}
EXPORT_SYMBOL(v4l2_ctrl_new_int_menu);

/* Add the controls from another handler to our own. */
int v4l2_ctrl_add_handler(struct v4l2_ctrl_handler *hdl,
			  struct v4l2_ctrl_handler *add,
			  bool (*filter)(const struct v4l2_ctrl *ctrl),
			  bool from_other_dev)
{
	struct v4l2_ctrl_ref *ref;
	int ret = 0;

	/* Do nothing if either handler is NULL or if they are the same */
	if (!hdl || !add || hdl == add)
		return 0;
	if (hdl->error)
		return hdl->error;
	mutex_lock(add->lock);
	list_for_each_entry(ref, &add->ctrl_refs, node) {
		struct v4l2_ctrl *ctrl = ref->ctrl;

		/* Skip handler-private controls. */
		if (ctrl->is_private)
			continue;
		/* And control classes */
		if (ctrl->type == V4L2_CTRL_TYPE_CTRL_CLASS)
			continue;
		/* Filter any unwanted controls */
		if (filter && !filter(ctrl))
			continue;
		ret = handler_new_ref(hdl, ctrl, NULL, from_other_dev, false);
		if (ret)
			break;
	}
	mutex_unlock(add->lock);
	return ret;
}
EXPORT_SYMBOL(v4l2_ctrl_add_handler);

bool v4l2_ctrl_radio_filter(const struct v4l2_ctrl *ctrl)
{
	if (V4L2_CTRL_ID2WHICH(ctrl->id) == V4L2_CTRL_CLASS_FM_TX)
		return true;
	if (V4L2_CTRL_ID2WHICH(ctrl->id) == V4L2_CTRL_CLASS_FM_RX)
		return true;
	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
	case V4L2_CID_AUDIO_LOUDNESS:
		return true;
	default:
		break;
	}
	return false;
}
EXPORT_SYMBOL(v4l2_ctrl_radio_filter);

/* Cluster controls */
void v4l2_ctrl_cluster(unsigned ncontrols, struct v4l2_ctrl **controls)
{
	bool has_volatiles = false;
	int i;

	/* The first control is the master control and it must not be NULL */
	if (WARN_ON(ncontrols == 0 || controls[0] == NULL))
		return;

	for (i = 0; i < ncontrols; i++) {
		if (controls[i]) {
			controls[i]->cluster = controls;
			controls[i]->ncontrols = ncontrols;
			if (controls[i]->flags & V4L2_CTRL_FLAG_VOLATILE)
				has_volatiles = true;
		}
	}
	controls[0]->has_volatiles = has_volatiles;
}
EXPORT_SYMBOL(v4l2_ctrl_cluster);

void v4l2_ctrl_auto_cluster(unsigned ncontrols, struct v4l2_ctrl **controls,
			    u8 manual_val, bool set_volatile)
{
	struct v4l2_ctrl *master = controls[0];
	u32 flag = 0;
	int i;

	v4l2_ctrl_cluster(ncontrols, controls);
	WARN_ON(ncontrols <= 1);
	WARN_ON(manual_val < master->minimum || manual_val > master->maximum);
	WARN_ON(set_volatile && !has_op(master, g_volatile_ctrl));
	master->is_auto = true;
	master->has_volatiles = set_volatile;
	master->manual_mode_value = manual_val;
	master->flags |= V4L2_CTRL_FLAG_UPDATE;

	if (!is_cur_manual(master))
		flag = V4L2_CTRL_FLAG_INACTIVE |
			(set_volatile ? V4L2_CTRL_FLAG_VOLATILE : 0);

	for (i = 1; i < ncontrols; i++)
		if (controls[i])
			controls[i]->flags |= flag;
}
EXPORT_SYMBOL(v4l2_ctrl_auto_cluster);

/*
 * Obtain the current volatile values of an autocluster and mark them
 * as new.
 */
void update_from_auto_cluster(struct v4l2_ctrl *master)
{
	int i;

	for (i = 1; i < master->ncontrols; i++)
		cur_to_new(master->cluster[i]);
	if (!call_op(master, g_volatile_ctrl))
		for (i = 1; i < master->ncontrols; i++)
			if (master->cluster[i])
				master->cluster[i]->is_new = 1;
}

/*
 * Return non-zero if one or more of the controls in the cluster has a new
 * value that differs from the current value.
 */
static int cluster_changed(struct v4l2_ctrl *master)
{
	bool changed = false;
	unsigned int idx;
	int i;

	for (i = 0; i < master->ncontrols; i++) {
		struct v4l2_ctrl *ctrl = master->cluster[i];
		bool ctrl_changed = false;

		if (!ctrl)
			continue;

		if (ctrl->flags & V4L2_CTRL_FLAG_EXECUTE_ON_WRITE) {
			changed = true;
			ctrl_changed = true;
		}

		/*
		 * Set has_changed to false to avoid generating
		 * the event V4L2_EVENT_CTRL_CH_VALUE
		 */
		if (ctrl->flags & V4L2_CTRL_FLAG_VOLATILE) {
			ctrl->has_changed = false;
			continue;
		}

		for (idx = 0; !ctrl_changed && idx < ctrl->elems; idx++)
			ctrl_changed = !ctrl->type_ops->equal(ctrl, idx,
				ctrl->p_cur, ctrl->p_new);
		ctrl->has_changed = ctrl_changed;
		changed |= ctrl->has_changed;
	}
	return changed;
}

/*
 * Core function that calls try/s_ctrl and ensures that the new value is
 * copied to the current value on a set.
 * Must be called with ctrl->handler->lock held.
 */
int try_or_set_cluster(struct v4l2_fh *fh, struct v4l2_ctrl *master,
		       bool set, u32 ch_flags)
{
	bool update_flag;
	int ret;
	int i;

	/*
	 * Go through the cluster and either validate the new value or
	 * (if no new value was set), copy the current value to the new
	 * value, ensuring a consistent view for the control ops when
	 * called.
	 */
	for (i = 0; i < master->ncontrols; i++) {
		struct v4l2_ctrl *ctrl = master->cluster[i];

		if (!ctrl)
			continue;

		if (!ctrl->is_new) {
			cur_to_new(ctrl);
			continue;
		}
		/*
		 * Check again: it may have changed since the
		 * previous check in try_or_set_ext_ctrls().
		 */
		if (set && (ctrl->flags & V4L2_CTRL_FLAG_GRABBED))
			return -EBUSY;
	}

	ret = call_op(master, try_ctrl);

	/* Don't set if there is no change */
	if (ret || !set || !cluster_changed(master))
		return ret;
	ret = call_op(master, s_ctrl);
	if (ret)
		return ret;

	/* If OK, then make the new values permanent. */
	update_flag = is_cur_manual(master) != is_new_manual(master);

	for (i = 0; i < master->ncontrols; i++) {
		/*
		 * If we switch from auto to manual mode, and this cluster
		 * contains volatile controls, then all non-master controls
		 * have to be marked as changed. The 'new' value contains
		 * the volatile value (obtained by update_from_auto_cluster),
		 * which now has to become the current value.
		 */
		if (i && update_flag && is_new_manual(master) &&
		    master->has_volatiles && master->cluster[i])
			master->cluster[i]->has_changed = true;

		new_to_cur(fh, master->cluster[i], ch_flags |
			((update_flag && i > 0) ? V4L2_EVENT_CTRL_CH_FLAGS : 0));
	}
	return 0;
}

/* Activate/deactivate a control. */
void v4l2_ctrl_activate(struct v4l2_ctrl *ctrl, bool active)
{
	/* invert since the actual flag is called 'inactive' */
	bool inactive = !active;
	bool old;

	if (ctrl == NULL)
		return;

	if (inactive)
		/* set V4L2_CTRL_FLAG_INACTIVE */
		old = test_and_set_bit(4, &ctrl->flags);
	else
		/* clear V4L2_CTRL_FLAG_INACTIVE */
		old = test_and_clear_bit(4, &ctrl->flags);
	if (old != inactive)
		send_event(NULL, ctrl, V4L2_EVENT_CTRL_CH_FLAGS);
}
EXPORT_SYMBOL(v4l2_ctrl_activate);

void __v4l2_ctrl_grab(struct v4l2_ctrl *ctrl, bool grabbed)
{
	bool old;

	if (ctrl == NULL)
		return;

	lockdep_assert_held(ctrl->handler->lock);

	if (grabbed)
		/* set V4L2_CTRL_FLAG_GRABBED */
		old = test_and_set_bit(1, &ctrl->flags);
	else
		/* clear V4L2_CTRL_FLAG_GRABBED */
		old = test_and_clear_bit(1, &ctrl->flags);
	if (old != grabbed)
		send_event(NULL, ctrl, V4L2_EVENT_CTRL_CH_FLAGS);
}
EXPORT_SYMBOL(__v4l2_ctrl_grab);

/* Call s_ctrl for all controls owned by the handler */
int __v4l2_ctrl_handler_setup(struct v4l2_ctrl_handler *hdl)
{
	struct v4l2_ctrl *ctrl;
	int ret = 0;

	if (hdl == NULL)
		return 0;

	lockdep_assert_held(hdl->lock);

	list_for_each_entry(ctrl, &hdl->ctrls, node)
		ctrl->done = false;

	list_for_each_entry(ctrl, &hdl->ctrls, node) {
		struct v4l2_ctrl *master = ctrl->cluster[0];
		int i;

		/* Skip if this control was already handled by a cluster. */
		/* Skip button controls and read-only controls. */
		if (ctrl->done || ctrl->type == V4L2_CTRL_TYPE_BUTTON ||
		    (ctrl->flags & V4L2_CTRL_FLAG_READ_ONLY))
			continue;

		for (i = 0; i < master->ncontrols; i++) {
			if (master->cluster[i]) {
				cur_to_new(master->cluster[i]);
				master->cluster[i]->is_new = 1;
				master->cluster[i]->done = true;
			}
		}
		ret = call_op(master, s_ctrl);
		if (ret)
			break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(__v4l2_ctrl_handler_setup);

int v4l2_ctrl_handler_setup(struct v4l2_ctrl_handler *hdl)
{
	int ret;

	if (hdl == NULL)
		return 0;

	mutex_lock(hdl->lock);
	ret = __v4l2_ctrl_handler_setup(hdl);
	mutex_unlock(hdl->lock);

	return ret;
}
EXPORT_SYMBOL(v4l2_ctrl_handler_setup);

/* Log the control name and value */
static void log_ctrl(const struct v4l2_ctrl *ctrl,
		     const char *prefix, const char *colon)
{
	if (ctrl->flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_WRITE_ONLY))
		return;
	if (ctrl->type == V4L2_CTRL_TYPE_CTRL_CLASS)
		return;

	pr_info("%s%s%s: ", prefix, colon, ctrl->name);

	ctrl->type_ops->log(ctrl);

	if (ctrl->flags & (V4L2_CTRL_FLAG_INACTIVE |
			   V4L2_CTRL_FLAG_GRABBED |
			   V4L2_CTRL_FLAG_VOLATILE)) {
		if (ctrl->flags & V4L2_CTRL_FLAG_INACTIVE)
			pr_cont(" inactive");
		if (ctrl->flags & V4L2_CTRL_FLAG_GRABBED)
			pr_cont(" grabbed");
		if (ctrl->flags & V4L2_CTRL_FLAG_VOLATILE)
			pr_cont(" volatile");
	}
	pr_cont("\n");
}

/* Log all controls owned by the handler */
void v4l2_ctrl_handler_log_status(struct v4l2_ctrl_handler *hdl,
				  const char *prefix)
{
	struct v4l2_ctrl *ctrl;
	const char *colon = "";
	int len;

	if (!hdl)
		return;
	if (!prefix)
		prefix = "";
	len = strlen(prefix);
	if (len && prefix[len - 1] != ' ')
		colon = ": ";
	mutex_lock(hdl->lock);
	list_for_each_entry(ctrl, &hdl->ctrls, node)
		if (!(ctrl->flags & V4L2_CTRL_FLAG_DISABLED))
			log_ctrl(ctrl, prefix, colon);
	mutex_unlock(hdl->lock);
}
EXPORT_SYMBOL(v4l2_ctrl_handler_log_status);

int v4l2_ctrl_new_fwnode_properties(struct v4l2_ctrl_handler *hdl,
				    const struct v4l2_ctrl_ops *ctrl_ops,
				    const struct v4l2_fwnode_device_properties *p)
{
	if (p->orientation != V4L2_FWNODE_PROPERTY_UNSET) {
		u32 orientation_ctrl;

		switch (p->orientation) {
		case V4L2_FWNODE_ORIENTATION_FRONT:
			orientation_ctrl = V4L2_CAMERA_ORIENTATION_FRONT;
			break;
		case V4L2_FWNODE_ORIENTATION_BACK:
			orientation_ctrl = V4L2_CAMERA_ORIENTATION_BACK;
			break;
		case V4L2_FWNODE_ORIENTATION_EXTERNAL:
			orientation_ctrl = V4L2_CAMERA_ORIENTATION_EXTERNAL;
			break;
		default:
			return -EINVAL;
		}
		if (!v4l2_ctrl_new_std_menu(hdl, ctrl_ops,
					    V4L2_CID_CAMERA_ORIENTATION,
					    V4L2_CAMERA_ORIENTATION_EXTERNAL, 0,
					    orientation_ctrl))
			return hdl->error;
	}

	if (p->rotation != V4L2_FWNODE_PROPERTY_UNSET) {
		if (!v4l2_ctrl_new_std(hdl, ctrl_ops,
				       V4L2_CID_CAMERA_SENSOR_ROTATION,
				       p->rotation, p->rotation, 1,
				       p->rotation))
			return hdl->error;
	}

	return hdl->error;
}
EXPORT_SYMBOL(v4l2_ctrl_new_fwnode_properties);
