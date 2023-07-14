/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_VISL_TRACE_H264_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _VISL_TRACE_H264_H_

#include <linux/tracepoint.h>
#include "visl.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM visl_h264_controls

DECLARE_EVENT_CLASS(v4l2_ctrl_h264_sps_tmpl,
	TP_PROTO(const struct v4l2_ctrl_h264_sps *s),
	TP_ARGS(s),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_h264_sps, s)),
	TP_fast_assign(__entry->s = *s),
	TP_printk("\nprofile_idc %u\n"
		  "constraint_set_flags %s\n"
		  "level_idc %u\n"
		  "seq_parameter_set_id %u\n"
		  "chroma_format_idc %u\n"
		  "bit_depth_luma_minus8 %u\n"
		  "bit_depth_chroma_minus8 %u\n"
		  "log2_max_frame_num_minus4 %u\n"
		  "pic_order_cnt_type %u\n"
		  "log2_max_pic_order_cnt_lsb_minus4 %u\n"
		  "max_num_ref_frames %u\n"
		  "num_ref_frames_in_pic_order_cnt_cycle %u\n"
		  "offset_for_ref_frame %s\n"
		  "offset_for_non_ref_pic %d\n"
		  "offset_for_top_to_bottom_field %d\n"
		  "pic_width_in_mbs_minus1 %u\n"
		  "pic_height_in_map_units_minus1 %u\n"
		  "flags %s",
		  __entry->s.profile_idc,
		  __print_flags(__entry->s.constraint_set_flags, "|",
		  {V4L2_H264_SPS_CONSTRAINT_SET0_FLAG, "CONSTRAINT_SET0_FLAG"},
		  {V4L2_H264_SPS_CONSTRAINT_SET1_FLAG, "CONSTRAINT_SET1_FLAG"},
		  {V4L2_H264_SPS_CONSTRAINT_SET2_FLAG, "CONSTRAINT_SET2_FLAG"},
		  {V4L2_H264_SPS_CONSTRAINT_SET3_FLAG, "CONSTRAINT_SET3_FLAG"},
		  {V4L2_H264_SPS_CONSTRAINT_SET4_FLAG, "CONSTRAINT_SET4_FLAG"},
		  {V4L2_H264_SPS_CONSTRAINT_SET5_FLAG, "CONSTRAINT_SET5_FLAG"}),
		  __entry->s.level_idc,
		  __entry->s.seq_parameter_set_id,
		  __entry->s.chroma_format_idc,
		  __entry->s.bit_depth_luma_minus8,
		  __entry->s.bit_depth_chroma_minus8,
		  __entry->s.log2_max_frame_num_minus4,
		  __entry->s.pic_order_cnt_type,
		  __entry->s.log2_max_pic_order_cnt_lsb_minus4,
		  __entry->s.max_num_ref_frames,
		  __entry->s.num_ref_frames_in_pic_order_cnt_cycle,
		  __print_array(__entry->s.offset_for_ref_frame,
				ARRAY_SIZE(__entry->s.offset_for_ref_frame),
				sizeof(__entry->s.offset_for_ref_frame[0])),
		  __entry->s.offset_for_non_ref_pic,
		  __entry->s.offset_for_top_to_bottom_field,
		  __entry->s.pic_width_in_mbs_minus1,
		  __entry->s.pic_height_in_map_units_minus1,
		  __print_flags(__entry->s.flags, "|",
		  {V4L2_H264_SPS_FLAG_SEPARATE_COLOUR_PLANE, "SEPARATE_COLOUR_PLANE"},
		  {V4L2_H264_SPS_FLAG_QPPRIME_Y_ZERO_TRANSFORM_BYPASS, "QPPRIME_Y_ZERO_TRANSFORM_BYPASS"},
		  {V4L2_H264_SPS_FLAG_DELTA_PIC_ORDER_ALWAYS_ZERO, "DELTA_PIC_ORDER_ALWAYS_ZERO"},
		  {V4L2_H264_SPS_FLAG_GAPS_IN_FRAME_NUM_VALUE_ALLOWED, "GAPS_IN_FRAME_NUM_VALUE_ALLOWED"},
		  {V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY, "FRAME_MBS_ONLY"},
		  {V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD, "MB_ADAPTIVE_FRAME_FIELD"},
		  {V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE, "DIRECT_8X8_INFERENCE"}
		  ))
);

DECLARE_EVENT_CLASS(v4l2_ctrl_h264_pps_tmpl,
	TP_PROTO(const struct v4l2_ctrl_h264_pps *p),
	TP_ARGS(p),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_h264_pps, p)),
	TP_fast_assign(__entry->p = *p),
	TP_printk("\npic_parameter_set_id %u\n"
		  "seq_parameter_set_id %u\n"
		  "num_slice_groups_minus1 %u\n"
		  "num_ref_idx_l0_default_active_minus1 %u\n"
		  "num_ref_idx_l1_default_active_minus1 %u\n"
		  "weighted_bipred_idc %u\n"
		  "pic_init_qp_minus26 %d\n"
		  "pic_init_qs_minus26 %d\n"
		  "chroma_qp_index_offset %d\n"
		  "second_chroma_qp_index_offset %d\n"
		  "flags %s",
		  __entry->p.pic_parameter_set_id,
		  __entry->p.seq_parameter_set_id,
		  __entry->p.num_slice_groups_minus1,
		  __entry->p.num_ref_idx_l0_default_active_minus1,
		  __entry->p.num_ref_idx_l1_default_active_minus1,
		  __entry->p.weighted_bipred_idc,
		  __entry->p.pic_init_qp_minus26,
		  __entry->p.pic_init_qs_minus26,
		  __entry->p.chroma_qp_index_offset,
		  __entry->p.second_chroma_qp_index_offset,
		  __print_flags(__entry->p.flags, "|",
		  {V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE, "ENTROPY_CODING_MODE"},
		  {V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT, "BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT"},
		  {V4L2_H264_PPS_FLAG_WEIGHTED_PRED, "WEIGHTED_PRED"},
		  {V4L2_H264_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT, "DEBLOCKING_FILTER_CONTROL_PRESENT"},
		  {V4L2_H264_PPS_FLAG_CONSTRAINED_INTRA_PRED, "CONSTRAINED_INTRA_PRED"},
		  {V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT, "REDUNDANT_PIC_CNT_PRESENT"},
		  {V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE, "TRANSFORM_8X8_MODE"},
		  {V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT, "SCALING_MATRIX_PRESENT"}
		  ))
);

DECLARE_EVENT_CLASS(v4l2_ctrl_h264_scaling_matrix_tmpl,
	TP_PROTO(const struct v4l2_ctrl_h264_scaling_matrix *s),
	TP_ARGS(s),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_h264_scaling_matrix, s)),
	TP_fast_assign(__entry->s = *s),
	TP_printk("\nscaling_list_4x4 {%s}\nscaling_list_8x8 {%s}",
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->s.scaling_list_4x4,
				   sizeof(__entry->s.scaling_list_4x4),
				   false),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->s.scaling_list_8x8,
				   sizeof(__entry->s.scaling_list_8x8),
				   false)
	)
);

DECLARE_EVENT_CLASS(v4l2_ctrl_h264_pred_weights_tmpl,
	TP_PROTO(const struct v4l2_ctrl_h264_pred_weights *p),
	TP_ARGS(p),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_h264_pred_weights, p)),
	TP_fast_assign(__entry->p = *p),
	TP_printk("\nluma_log2_weight_denom %u\n"
		  "chroma_log2_weight_denom %u\n"
		  "weight_factor[0].luma_weight %s\n"
		  "weight_factor[0].luma_offset %s\n"
		  "weight_factor[0].chroma_weight {%s}\n"
		  "weight_factor[0].chroma_offset {%s}\n"
		  "weight_factor[1].luma_weight %s\n"
		  "weight_factor[1].luma_offset %s\n"
		  "weight_factor[1].chroma_weight {%s}\n"
		  "weight_factor[1].chroma_offset {%s}\n",
		  __entry->p.luma_log2_weight_denom,
		  __entry->p.chroma_log2_weight_denom,
		  __print_array(__entry->p.weight_factors[0].luma_weight,
				ARRAY_SIZE(__entry->p.weight_factors[0].luma_weight),
				sizeof(__entry->p.weight_factors[0].luma_weight[0])),
		  __print_array(__entry->p.weight_factors[0].luma_offset,
				ARRAY_SIZE(__entry->p.weight_factors[0].luma_offset),
				sizeof(__entry->p.weight_factors[0].luma_offset[0])),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->p.weight_factors[0].chroma_weight,
				   sizeof(__entry->p.weight_factors[0].chroma_weight),
				   false),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->p.weight_factors[0].chroma_offset,
				   sizeof(__entry->p.weight_factors[0].chroma_offset),
				   false),
		  __print_array(__entry->p.weight_factors[1].luma_weight,
				ARRAY_SIZE(__entry->p.weight_factors[1].luma_weight),
				sizeof(__entry->p.weight_factors[1].luma_weight[0])),
		  __print_array(__entry->p.weight_factors[1].luma_offset,
				ARRAY_SIZE(__entry->p.weight_factors[1].luma_offset),
				sizeof(__entry->p.weight_factors[1].luma_offset[0])),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->p.weight_factors[1].chroma_weight,
				   sizeof(__entry->p.weight_factors[1].chroma_weight),
				   false),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->p.weight_factors[1].chroma_offset,
				   sizeof(__entry->p.weight_factors[1].chroma_offset),
				   false)
	)
);

DECLARE_EVENT_CLASS(v4l2_ctrl_h264_slice_params_tmpl,
	TP_PROTO(const struct v4l2_ctrl_h264_slice_params *s),
	TP_ARGS(s),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_h264_slice_params, s)),
	TP_fast_assign(__entry->s = *s),
	TP_printk("\nheader_bit_size %u\n"
		  "first_mb_in_slice %u\n"
		  "slice_type %s\n"
		  "colour_plane_id %u\n"
		  "redundant_pic_cnt %u\n"
		  "cabac_init_idc %u\n"
		  "slice_qp_delta %d\n"
		  "slice_qs_delta %d\n"
		  "disable_deblocking_filter_idc %u\n"
		  "slice_alpha_c0_offset_div2 %u\n"
		  "slice_beta_offset_div2 %u\n"
		  "num_ref_idx_l0_active_minus1 %u\n"
		  "num_ref_idx_l1_active_minus1 %u\n"
		  "flags %s",
		  __entry->s.header_bit_size,
		  __entry->s.first_mb_in_slice,
		  __print_symbolic(__entry->s.slice_type,
		  {V4L2_H264_SLICE_TYPE_P, "P"},
		  {V4L2_H264_SLICE_TYPE_B, "B"},
		  {V4L2_H264_SLICE_TYPE_I, "I"},
		  {V4L2_H264_SLICE_TYPE_SP, "SP"},
		  {V4L2_H264_SLICE_TYPE_SI, "SI"}),
		  __entry->s.colour_plane_id,
		  __entry->s.redundant_pic_cnt,
		  __entry->s.cabac_init_idc,
		  __entry->s.slice_qp_delta,
		  __entry->s.slice_qs_delta,
		  __entry->s.disable_deblocking_filter_idc,
		  __entry->s.slice_alpha_c0_offset_div2,
		  __entry->s.slice_beta_offset_div2,
		  __entry->s.num_ref_idx_l0_active_minus1,
		  __entry->s.num_ref_idx_l1_active_minus1,
		  __print_flags(__entry->s.flags, "|",
		  {V4L2_H264_SLICE_FLAG_DIRECT_SPATIAL_MV_PRED, "DIRECT_SPATIAL_MV_PRED"},
		  {V4L2_H264_SLICE_FLAG_SP_FOR_SWITCH, "SP_FOR_SWITCH"})
	)
);

DECLARE_EVENT_CLASS(v4l2_h264_reference_tmpl,
	TP_PROTO(const struct v4l2_h264_reference *r, int i),
	TP_ARGS(r, i),
	TP_STRUCT__entry(__field_struct(struct v4l2_h264_reference, r)
			 __field(int, i)),
	TP_fast_assign(__entry->r = *r; __entry->i = i;),
	TP_printk("[%d]: fields %s index %u",
		  __entry->i,
		  __print_flags(__entry->r.fields, "|",
		  {V4L2_H264_TOP_FIELD_REF, "TOP_FIELD_REF"},
		  {V4L2_H264_BOTTOM_FIELD_REF, "BOTTOM_FIELD_REF"},
		  {V4L2_H264_FRAME_REF, "FRAME_REF"}),
		  __entry->r.index
	)
);

DECLARE_EVENT_CLASS(v4l2_ctrl_h264_decode_params_tmpl,
	TP_PROTO(const struct v4l2_ctrl_h264_decode_params *d),
	TP_ARGS(d),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_h264_decode_params, d)),
	TP_fast_assign(__entry->d = *d),
	TP_printk("\nnal_ref_idc %u\n"
		  "frame_num %u\n"
		  "top_field_order_cnt %d\n"
		  "bottom_field_order_cnt %d\n"
		  "idr_pic_id %u\n"
		  "pic_order_cnt_lsb %u\n"
		  "delta_pic_order_cnt_bottom %d\n"
		  "delta_pic_order_cnt0 %d\n"
		  "delta_pic_order_cnt1 %d\n"
		  "dec_ref_pic_marking_bit_size %u\n"
		  "pic_order_cnt_bit_size %u\n"
		  "slice_group_change_cycle %u\n"
		  "flags %s\n",
		  __entry->d.nal_ref_idc,
		  __entry->d.frame_num,
		  __entry->d.top_field_order_cnt,
		  __entry->d.bottom_field_order_cnt,
		  __entry->d.idr_pic_id,
		  __entry->d.pic_order_cnt_lsb,
		  __entry->d.delta_pic_order_cnt_bottom,
		  __entry->d.delta_pic_order_cnt0,
		  __entry->d.delta_pic_order_cnt1,
		  __entry->d.dec_ref_pic_marking_bit_size,
		  __entry->d.pic_order_cnt_bit_size,
		  __entry->d.slice_group_change_cycle,
		  __print_flags(__entry->d.flags, "|",
		  {V4L2_H264_DECODE_PARAM_FLAG_IDR_PIC, "IDR_PIC"},
		  {V4L2_H264_DECODE_PARAM_FLAG_FIELD_PIC, "FIELD_PIC"},
		  {V4L2_H264_DECODE_PARAM_FLAG_BOTTOM_FIELD, "BOTTOM_FIELD"},
		  {V4L2_H264_DECODE_PARAM_FLAG_PFRAME, "PFRAME"},
		  {V4L2_H264_DECODE_PARAM_FLAG_BFRAME, "BFRAME"})
	)
);

DECLARE_EVENT_CLASS(v4l2_h264_dpb_entry_tmpl,
	TP_PROTO(const struct v4l2_h264_dpb_entry *e, int i),
	TP_ARGS(e, i),
	TP_STRUCT__entry(__field_struct(struct v4l2_h264_dpb_entry, e)
			 __field(int, i)),
	TP_fast_assign(__entry->e = *e; __entry->i = i;),
	TP_printk("[%d]: reference_ts %llu, pic_num %u frame_num %u fields %s "
		  "top_field_order_cnt %d bottom_field_order_cnt %d flags %s",
		  __entry->i,
		  __entry->e.reference_ts,
		  __entry->e.pic_num,
		  __entry->e.frame_num,
		  __print_flags(__entry->e.fields, "|",
		  {V4L2_H264_TOP_FIELD_REF, "TOP_FIELD_REF"},
		  {V4L2_H264_BOTTOM_FIELD_REF, "BOTTOM_FIELD_REF"},
		  {V4L2_H264_FRAME_REF, "FRAME_REF"}),
		  __entry->e.top_field_order_cnt,
		  __entry->e.bottom_field_order_cnt,
		  __print_flags(__entry->e.flags, "|",
		  {V4L2_H264_DPB_ENTRY_FLAG_VALID, "VALID"},
		  {V4L2_H264_DPB_ENTRY_FLAG_ACTIVE, "ACTIVE"},
		  {V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM, "LONG_TERM"},
		  {V4L2_H264_DPB_ENTRY_FLAG_FIELD, "FIELD"})

	)
);

DEFINE_EVENT(v4l2_ctrl_h264_sps_tmpl, v4l2_ctrl_h264_sps,
	TP_PROTO(const struct v4l2_ctrl_h264_sps *s),
	TP_ARGS(s)
);

DEFINE_EVENT(v4l2_ctrl_h264_pps_tmpl, v4l2_ctrl_h264_pps,
	TP_PROTO(const struct v4l2_ctrl_h264_pps *p),
	TP_ARGS(p)
);

DEFINE_EVENT(v4l2_ctrl_h264_scaling_matrix_tmpl, v4l2_ctrl_h264_scaling_matrix,
	TP_PROTO(const struct v4l2_ctrl_h264_scaling_matrix *s),
	TP_ARGS(s)
);

DEFINE_EVENT(v4l2_ctrl_h264_pred_weights_tmpl, v4l2_ctrl_h264_pred_weights,
	TP_PROTO(const struct v4l2_ctrl_h264_pred_weights *p),
	TP_ARGS(p)
);

DEFINE_EVENT(v4l2_ctrl_h264_slice_params_tmpl, v4l2_ctrl_h264_slice_params,
	TP_PROTO(const struct v4l2_ctrl_h264_slice_params *s),
	TP_ARGS(s)
);

DEFINE_EVENT(v4l2_h264_reference_tmpl, v4l2_h264_ref_pic_list0,
	TP_PROTO(const struct v4l2_h264_reference *r, int i),
	TP_ARGS(r, i)
);

DEFINE_EVENT(v4l2_h264_reference_tmpl, v4l2_h264_ref_pic_list1,
	TP_PROTO(const struct v4l2_h264_reference *r, int i),
	TP_ARGS(r, i)
);

DEFINE_EVENT(v4l2_ctrl_h264_decode_params_tmpl, v4l2_ctrl_h264_decode_params,
	TP_PROTO(const struct v4l2_ctrl_h264_decode_params *d),
	TP_ARGS(d)
);

DEFINE_EVENT(v4l2_h264_dpb_entry_tmpl, v4l2_h264_dpb_entry,
	TP_PROTO(const struct v4l2_h264_dpb_entry *e, int i),
	TP_ARGS(e, i)
);

#endif

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/media/test-drivers/visl
#define TRACE_INCLUDE_FILE visl-trace-h264
#include <trace/define_trace.h>
