/* SPDX-License-Identifier: GPL-2.0+ */
#if !defined(_VISL_TRACE_HEVC_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _VISL_TRACE_HEVC_H_

#include <linux/tracepoint.h>
#include "visl.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM visl_hevc_controls

DECLARE_EVENT_CLASS(v4l2_ctrl_hevc_sps_tmpl,
	TP_PROTO(const struct v4l2_ctrl_hevc_sps *s),
	TP_ARGS(s),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_hevc_sps, s)),
	TP_fast_assign(__entry->s = *s),
	TP_printk("\nvideo_parameter_set_id %u\n"
		  "seq_parameter_set_id %u\n"
		  "pic_width_in_luma_samples %u\n"
		  "pic_height_in_luma_samples %u\n"
		  "bit_depth_luma_minus8 %u\n"
		  "bit_depth_chroma_minus8 %u\n"
		  "log2_max_pic_order_cnt_lsb_minus4 %u\n"
		  "sps_max_dec_pic_buffering_minus1 %u\n"
		  "sps_max_num_reorder_pics %u\n"
		  "sps_max_latency_increase_plus1 %u\n"
		  "log2_min_luma_coding_block_size_minus3 %u\n"
		  "log2_diff_max_min_luma_coding_block_size %u\n"
		  "log2_min_luma_transform_block_size_minus2 %u\n"
		  "log2_diff_max_min_luma_transform_block_size %u\n"
		  "max_transform_hierarchy_depth_inter %u\n"
		  "max_transform_hierarchy_depth_intra %u\n"
		  "pcm_sample_bit_depth_luma_minus1 %u\n"
		  "pcm_sample_bit_depth_chroma_minus1 %u\n"
		  "log2_min_pcm_luma_coding_block_size_minus3 %u\n"
		  "log2_diff_max_min_pcm_luma_coding_block_size %u\n"
		  "num_short_term_ref_pic_sets %u\n"
		  "num_long_term_ref_pics_sps %u\n"
		  "chroma_format_idc %u\n"
		  "sps_max_sub_layers_minus1 %u\n"
		  "flags %s",
		  __entry->s.video_parameter_set_id,
		  __entry->s.seq_parameter_set_id,
		  __entry->s.pic_width_in_luma_samples,
		  __entry->s.pic_height_in_luma_samples,
		  __entry->s.bit_depth_luma_minus8,
		  __entry->s.bit_depth_chroma_minus8,
		  __entry->s.log2_max_pic_order_cnt_lsb_minus4,
		  __entry->s.sps_max_dec_pic_buffering_minus1,
		  __entry->s.sps_max_num_reorder_pics,
		  __entry->s.sps_max_latency_increase_plus1,
		  __entry->s.log2_min_luma_coding_block_size_minus3,
		  __entry->s.log2_diff_max_min_luma_coding_block_size,
		  __entry->s.log2_min_luma_transform_block_size_minus2,
		  __entry->s.log2_diff_max_min_luma_transform_block_size,
		  __entry->s.max_transform_hierarchy_depth_inter,
		  __entry->s.max_transform_hierarchy_depth_intra,
		  __entry->s.pcm_sample_bit_depth_luma_minus1,
		  __entry->s.pcm_sample_bit_depth_chroma_minus1,
		  __entry->s.log2_min_pcm_luma_coding_block_size_minus3,
		  __entry->s.log2_diff_max_min_pcm_luma_coding_block_size,
		  __entry->s.num_short_term_ref_pic_sets,
		  __entry->s.num_long_term_ref_pics_sps,
		  __entry->s.chroma_format_idc,
		  __entry->s.sps_max_sub_layers_minus1,
		  __print_flags(__entry->s.flags, "|",
		  {V4L2_HEVC_SPS_FLAG_SEPARATE_COLOUR_PLANE, "SEPARATE_COLOUR_PLANE"},
		  {V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED, "SCALING_LIST_ENABLED"},
		  {V4L2_HEVC_SPS_FLAG_AMP_ENABLED, "AMP_ENABLED"},
		  {V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET, "SAMPLE_ADAPTIVE_OFFSET"},
		  {V4L2_HEVC_SPS_FLAG_PCM_ENABLED, "PCM_ENABLED"},
		  {V4L2_HEVC_SPS_FLAG_PCM_LOOP_FILTER_DISABLED, "V4L2_HEVC_SPS_FLAG_PCM_LOOP_FILTER_DISABLED"},
		  {V4L2_HEVC_SPS_FLAG_LONG_TERM_REF_PICS_PRESENT, "LONG_TERM_REF_PICS_PRESENT"},
		  {V4L2_HEVC_SPS_FLAG_SPS_TEMPORAL_MVP_ENABLED, "TEMPORAL_MVP_ENABLED"},
		  {V4L2_HEVC_SPS_FLAG_STRONG_INTRA_SMOOTHING_ENABLED, "STRONG_INTRA_SMOOTHING_ENABLED"}
	))

);


DECLARE_EVENT_CLASS(v4l2_ctrl_hevc_pps_tmpl,
	TP_PROTO(const struct v4l2_ctrl_hevc_pps *p),
	TP_ARGS(p),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_hevc_pps, p)),
	TP_fast_assign(__entry->p = *p),
	TP_printk("\npic_parameter_set_id %u\n"
		  "num_extra_slice_header_bits %u\n"
		  "num_ref_idx_l0_default_active_minus1 %u\n"
		  "num_ref_idx_l1_default_active_minus1 %u\n"
		  "init_qp_minus26 %d\n"
		  "diff_cu_qp_delta_depth %u\n"
		  "pps_cb_qp_offset %d\n"
		  "pps_cr_qp_offset %d\n"
		  "num_tile_columns_minus1 %d\n"
		  "num_tile_rows_minus1 %d\n"
		  "column_width_minus1 %s\n"
		  "row_height_minus1 %s\n"
		  "pps_beta_offset_div2 %d\n"
		  "pps_tc_offset_div2 %d\n"
		  "log2_parallel_merge_level_minus2 %u\n"
		  "flags %s",
		  __entry->p.pic_parameter_set_id,
		  __entry->p.num_extra_slice_header_bits,
		  __entry->p.num_ref_idx_l0_default_active_minus1,
		  __entry->p.num_ref_idx_l1_default_active_minus1,
		  __entry->p.init_qp_minus26,
		  __entry->p.diff_cu_qp_delta_depth,
		  __entry->p.pps_cb_qp_offset,
		  __entry->p.pps_cr_qp_offset,
		  __entry->p.num_tile_columns_minus1,
		  __entry->p.num_tile_rows_minus1,
		  __print_array(__entry->p.column_width_minus1,
				ARRAY_SIZE(__entry->p.column_width_minus1),
				sizeof(__entry->p.column_width_minus1[0])),
		  __print_array(__entry->p.row_height_minus1,
				ARRAY_SIZE(__entry->p.row_height_minus1),
				sizeof(__entry->p.row_height_minus1[0])),
		  __entry->p.pps_beta_offset_div2,
		  __entry->p.pps_tc_offset_div2,
		  __entry->p.log2_parallel_merge_level_minus2,
		  __print_flags(__entry->p.flags, "|",
		  {V4L2_HEVC_PPS_FLAG_DEPENDENT_SLICE_SEGMENT_ENABLED, "DEPENDENT_SLICE_SEGMENT_ENABLED"},
		  {V4L2_HEVC_PPS_FLAG_OUTPUT_FLAG_PRESENT, "OUTPUT_FLAG_PRESENT"},
		  {V4L2_HEVC_PPS_FLAG_SIGN_DATA_HIDING_ENABLED, "SIGN_DATA_HIDING_ENABLED"},
		  {V4L2_HEVC_PPS_FLAG_CABAC_INIT_PRESENT, "CABAC_INIT_PRESENT"},
		  {V4L2_HEVC_PPS_FLAG_CONSTRAINED_INTRA_PRED, "CONSTRAINED_INTRA_PRED"},
		  {V4L2_HEVC_PPS_FLAG_CU_QP_DELTA_ENABLED, "CU_QP_DELTA_ENABLED"},
		  {V4L2_HEVC_PPS_FLAG_PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT, "PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT"},
		  {V4L2_HEVC_PPS_FLAG_WEIGHTED_PRED, "WEIGHTED_PRED"},
		  {V4L2_HEVC_PPS_FLAG_WEIGHTED_BIPRED, "WEIGHTED_BIPRED"},
		  {V4L2_HEVC_PPS_FLAG_TRANSQUANT_BYPASS_ENABLED, "TRANSQUANT_BYPASS_ENABLED"},
		  {V4L2_HEVC_PPS_FLAG_TILES_ENABLED, "TILES_ENABLED"},
		  {V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED, "ENTROPY_CODING_SYNC_ENABLED"},
		  {V4L2_HEVC_PPS_FLAG_LOOP_FILTER_ACROSS_TILES_ENABLED, "LOOP_FILTER_ACROSS_TILES_ENABLED"},
		  {V4L2_HEVC_PPS_FLAG_PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED, "PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED"},
		  {V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_OVERRIDE_ENABLED, "DEBLOCKING_FILTER_OVERRIDE_ENABLED"},
		  {V4L2_HEVC_PPS_FLAG_PPS_DISABLE_DEBLOCKING_FILTER, "DISABLE_DEBLOCKING_FILTER"},
		  {V4L2_HEVC_PPS_FLAG_LISTS_MODIFICATION_PRESENT, "LISTS_MODIFICATION_PRESENT"},
		  {V4L2_HEVC_PPS_FLAG_SLICE_SEGMENT_HEADER_EXTENSION_PRESENT, "SLICE_SEGMENT_HEADER_EXTENSION_PRESENT"},
		  {V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT, "DEBLOCKING_FILTER_CONTROL_PRESENT"},
		  {V4L2_HEVC_PPS_FLAG_UNIFORM_SPACING, "UNIFORM_SPACING"}
	))

);



DECLARE_EVENT_CLASS(v4l2_ctrl_hevc_slice_params_tmpl,
	TP_PROTO(const struct v4l2_ctrl_hevc_slice_params *s),
	TP_ARGS(s),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_hevc_slice_params, s)),
	TP_fast_assign(__entry->s = *s),
	TP_printk("\nbit_size %u\n"
		  "data_byte_offset %u\n"
		  "num_entry_point_offsets %u\n"
		  "nal_unit_type %u\n"
		  "nuh_temporal_id_plus1 %u\n"
		  "slice_type %u\n"
		  "colour_plane_id %u\n"
		  "slice_pic_order_cnt %d\n"
		  "num_ref_idx_l0_active_minus1 %u\n"
		  "num_ref_idx_l1_active_minus1 %u\n"
		  "collocated_ref_idx %u\n"
		  "five_minus_max_num_merge_cand %u\n"
		  "slice_qp_delta %d\n"
		  "slice_cb_qp_offset %d\n"
		  "slice_cr_qp_offset %d\n"
		  "slice_act_y_qp_offset %d\n"
		  "slice_act_cb_qp_offset %d\n"
		  "slice_act_cr_qp_offset %d\n"
		  "slice_beta_offset_div2 %d\n"
		  "slice_tc_offset_div2 %d\n"
		  "pic_struct %u\n"
		  "slice_segment_addr %u\n"
		  "ref_idx_l0 %s\n"
		  "ref_idx_l1 %s\n"
		  "short_term_ref_pic_set_size %u\n"
		  "long_term_ref_pic_set_size %u\n"
		  "flags %s",
		  __entry->s.bit_size,
		  __entry->s.data_byte_offset,
		  __entry->s.num_entry_point_offsets,
		  __entry->s.nal_unit_type,
		  __entry->s.nuh_temporal_id_plus1,
		  __entry->s.slice_type,
		  __entry->s.colour_plane_id,
		  __entry->s.slice_pic_order_cnt,
		  __entry->s.num_ref_idx_l0_active_minus1,
		  __entry->s.num_ref_idx_l1_active_minus1,
		  __entry->s.collocated_ref_idx,
		  __entry->s.five_minus_max_num_merge_cand,
		  __entry->s.slice_qp_delta,
		  __entry->s.slice_cb_qp_offset,
		  __entry->s.slice_cr_qp_offset,
		  __entry->s.slice_act_y_qp_offset,
		  __entry->s.slice_act_cb_qp_offset,
		  __entry->s.slice_act_cr_qp_offset,
		  __entry->s.slice_beta_offset_div2,
		  __entry->s.slice_tc_offset_div2,
		  __entry->s.pic_struct,
		  __entry->s.slice_segment_addr,
		  __print_array(__entry->s.ref_idx_l0,
				ARRAY_SIZE(__entry->s.ref_idx_l0),
				sizeof(__entry->s.ref_idx_l0[0])),
		  __print_array(__entry->s.ref_idx_l1,
				ARRAY_SIZE(__entry->s.ref_idx_l1),
				sizeof(__entry->s.ref_idx_l1[0])),
		  __entry->s.short_term_ref_pic_set_size,
		  __entry->s.long_term_ref_pic_set_size,
		  __print_flags(__entry->s.flags, "|",
		  {V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_LUMA, "SLICE_SAO_LUMA"},
		  {V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_CHROMA, "SLICE_SAO_CHROMA"},
		  {V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_TEMPORAL_MVP_ENABLED, "SLICE_TEMPORAL_MVP_ENABLED"},
		  {V4L2_HEVC_SLICE_PARAMS_FLAG_MVD_L1_ZERO, "MVD_L1_ZERO"},
		  {V4L2_HEVC_SLICE_PARAMS_FLAG_CABAC_INIT, "CABAC_INIT"},
		  {V4L2_HEVC_SLICE_PARAMS_FLAG_COLLOCATED_FROM_L0, "COLLOCATED_FROM_L0"},
		  {V4L2_HEVC_SLICE_PARAMS_FLAG_USE_INTEGER_MV, "USE_INTEGER_MV"},
		  {V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_DEBLOCKING_FILTER_DISABLED, "SLICE_DEBLOCKING_FILTER_DISABLED"},
		  {V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED, "SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED"},
		  {V4L2_HEVC_SLICE_PARAMS_FLAG_DEPENDENT_SLICE_SEGMENT, "DEPENDENT_SLICE_SEGMENT"}

	))
);

DECLARE_EVENT_CLASS(v4l2_hevc_pred_weight_table_tmpl,
	TP_PROTO(const struct v4l2_hevc_pred_weight_table *p),
	TP_ARGS(p),
	TP_STRUCT__entry(__field_struct(struct v4l2_hevc_pred_weight_table, p)),
	TP_fast_assign(__entry->p = *p),
	TP_printk("\ndelta_luma_weight_l0 %s\n"
		  "luma_offset_l0 %s\n"
		  "delta_chroma_weight_l0 {%s}\n"
		  "chroma_offset_l0 {%s}\n"
		  "delta_luma_weight_l1 %s\n"
		  "luma_offset_l1 %s\n"
		  "delta_chroma_weight_l1 {%s}\n"
		  "chroma_offset_l1 {%s}\n"
		  "luma_log2_weight_denom %d\n"
		  "delta_chroma_log2_weight_denom %d\n",
		  __print_array(__entry->p.delta_luma_weight_l0,
				ARRAY_SIZE(__entry->p.delta_luma_weight_l0),
				sizeof(__entry->p.delta_luma_weight_l0[0])),
		  __print_array(__entry->p.luma_offset_l0,
				ARRAY_SIZE(__entry->p.luma_offset_l0),
				sizeof(__entry->p.luma_offset_l0[0])),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->p.delta_chroma_weight_l0,
				   sizeof(__entry->p.delta_chroma_weight_l0),
				   false),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->p.chroma_offset_l0,
				   sizeof(__entry->p.chroma_offset_l0),
				   false),
		  __print_array(__entry->p.delta_luma_weight_l1,
				ARRAY_SIZE(__entry->p.delta_luma_weight_l1),
				sizeof(__entry->p.delta_luma_weight_l1[0])),
		  __print_array(__entry->p.luma_offset_l1,
				ARRAY_SIZE(__entry->p.luma_offset_l1),
				sizeof(__entry->p.luma_offset_l1[0])),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->p.delta_chroma_weight_l1,
				   sizeof(__entry->p.delta_chroma_weight_l1),
				   false),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->p.chroma_offset_l1,
				   sizeof(__entry->p.chroma_offset_l1),
				   false),
		__entry->p.luma_log2_weight_denom,
		__entry->p.delta_chroma_log2_weight_denom

	))

DECLARE_EVENT_CLASS(v4l2_ctrl_hevc_scaling_matrix_tmpl,
	TP_PROTO(const struct v4l2_ctrl_hevc_scaling_matrix *s),
	TP_ARGS(s),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_hevc_scaling_matrix, s)),
	TP_fast_assign(__entry->s = *s),
	TP_printk("\nscaling_list_4x4 {%s}\n"
		  "scaling_list_8x8 {%s}\n"
		  "scaling_list_16x16 {%s}\n"
		  "scaling_list_32x32 {%s}\n"
		  "scaling_list_dc_coef_16x16 %s\n"
		  "scaling_list_dc_coef_32x32 %s\n",
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->s.scaling_list_4x4,
				   sizeof(__entry->s.scaling_list_4x4),
				   false),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->s.scaling_list_8x8,
				   sizeof(__entry->s.scaling_list_8x8),
				   false),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->s.scaling_list_16x16,
				   sizeof(__entry->s.scaling_list_16x16),
				   false),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->s.scaling_list_32x32,
				   sizeof(__entry->s.scaling_list_32x32),
				   false),
		  __print_array(__entry->s.scaling_list_dc_coef_16x16,
				ARRAY_SIZE(__entry->s.scaling_list_dc_coef_16x16),
				sizeof(__entry->s.scaling_list_dc_coef_16x16[0])),
		  __print_array(__entry->s.scaling_list_dc_coef_32x32,
				ARRAY_SIZE(__entry->s.scaling_list_dc_coef_32x32),
				sizeof(__entry->s.scaling_list_dc_coef_32x32[0]))
	))

DECLARE_EVENT_CLASS(v4l2_ctrl_hevc_decode_params_tmpl,
	TP_PROTO(const struct v4l2_ctrl_hevc_decode_params *d),
	TP_ARGS(d),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_hevc_decode_params, d)),
	TP_fast_assign(__entry->d = *d),
	TP_printk("\npic_order_cnt_val %d\n"
		  "short_term_ref_pic_set_size %u\n"
		  "long_term_ref_pic_set_size %u\n"
		  "num_active_dpb_entries %u\n"
		  "num_poc_st_curr_before %u\n"
		  "num_poc_st_curr_after %u\n"
		  "num_poc_lt_curr %u\n"
		  "poc_st_curr_before %s\n"
		  "poc_st_curr_after %s\n"
		  "poc_lt_curr %s\n"
		  "flags %s",
		  __entry->d.pic_order_cnt_val,
		  __entry->d.short_term_ref_pic_set_size,
		  __entry->d.long_term_ref_pic_set_size,
		  __entry->d.num_active_dpb_entries,
		  __entry->d.num_poc_st_curr_before,
		  __entry->d.num_poc_st_curr_after,
		  __entry->d.num_poc_lt_curr,
		  __print_array(__entry->d.poc_st_curr_before,
				ARRAY_SIZE(__entry->d.poc_st_curr_before),
				sizeof(__entry->d.poc_st_curr_before[0])),
		  __print_array(__entry->d.poc_st_curr_after,
				ARRAY_SIZE(__entry->d.poc_st_curr_after),
				sizeof(__entry->d.poc_st_curr_after[0])),
		  __print_array(__entry->d.poc_lt_curr,
				ARRAY_SIZE(__entry->d.poc_lt_curr),
				sizeof(__entry->d.poc_lt_curr[0])),
		  __print_flags(__entry->d.flags, "|",
		  {V4L2_HEVC_DECODE_PARAM_FLAG_IRAP_PIC, "IRAP_PIC"},
		  {V4L2_HEVC_DECODE_PARAM_FLAG_IDR_PIC, "IDR_PIC"},
		  {V4L2_HEVC_DECODE_PARAM_FLAG_NO_OUTPUT_OF_PRIOR, "NO_OUTPUT_OF_PRIOR"}
	))
);


DECLARE_EVENT_CLASS(v4l2_hevc_dpb_entry_tmpl,
	TP_PROTO(const struct v4l2_hevc_dpb_entry *e),
	TP_ARGS(e),
	TP_STRUCT__entry(__field_struct(struct v4l2_hevc_dpb_entry, e)),
	TP_fast_assign(__entry->e = *e),
	TP_printk("\ntimestamp %llu\n"
		  "flags %s\n"
		  "field_pic %u\n"
		  "pic_order_cnt_val %d\n",
		__entry->e.timestamp,
		__print_flags(__entry->e.flags, "|",
		{V4L2_HEVC_DPB_ENTRY_LONG_TERM_REFERENCE, "LONG_TERM_REFERENCE"}
		  ),
		__entry->e.field_pic,
		__entry->e.pic_order_cnt_val
	))

DEFINE_EVENT(v4l2_ctrl_hevc_sps_tmpl, v4l2_ctrl_hevc_sps,
	TP_PROTO(const struct v4l2_ctrl_hevc_sps *s),
	TP_ARGS(s)
);

DEFINE_EVENT(v4l2_ctrl_hevc_pps_tmpl, v4l2_ctrl_hevc_pps,
	TP_PROTO(const struct v4l2_ctrl_hevc_pps *p),
	TP_ARGS(p)
);

DEFINE_EVENT(v4l2_ctrl_hevc_slice_params_tmpl, v4l2_ctrl_hevc_slice_params,
	TP_PROTO(const struct v4l2_ctrl_hevc_slice_params *s),
	TP_ARGS(s)
);

DEFINE_EVENT(v4l2_hevc_pred_weight_table_tmpl, v4l2_hevc_pred_weight_table,
	TP_PROTO(const struct v4l2_hevc_pred_weight_table *p),
	TP_ARGS(p)
);

DEFINE_EVENT(v4l2_ctrl_hevc_scaling_matrix_tmpl, v4l2_ctrl_hevc_scaling_matrix,
	TP_PROTO(const struct v4l2_ctrl_hevc_scaling_matrix *s),
	TP_ARGS(s)
);

DEFINE_EVENT(v4l2_ctrl_hevc_decode_params_tmpl, v4l2_ctrl_hevc_decode_params,
	TP_PROTO(const struct v4l2_ctrl_hevc_decode_params *d),
	TP_ARGS(d)
);

DEFINE_EVENT(v4l2_hevc_dpb_entry_tmpl, v4l2_hevc_dpb_entry,
	TP_PROTO(const struct v4l2_hevc_dpb_entry *e),
	TP_ARGS(e)
);

#endif

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/media/test-drivers/visl
#define TRACE_INCLUDE_FILE visl-trace-hevc
#include <trace/define_trace.h>
