/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_VISL_TRACE_AV1_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _VISL_TRACE_AV1_H_

#include <linux/tracepoint.h>
#include "visl.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM visl_av1_controls

DECLARE_EVENT_CLASS(v4l2_ctrl_av1_seq_tmpl,
	TP_PROTO(const struct v4l2_ctrl_av1_sequence *s),
	TP_ARGS(s),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_av1_sequence, s)),
	TP_fast_assign(__entry->s = *s;),
	TP_printk("\nflags %s\nseq_profile: %u\norder_hint_bits: %u\nbit_depth: %u\n"
		  "max_frame_width_minus_1: %u\nmax_frame_height_minus_1: %u\n",
		  __print_flags(__entry->s.flags, "|",
		  {V4L2_AV1_SEQUENCE_FLAG_STILL_PICTURE, "STILL_PICTURE"},
		  {V4L2_AV1_SEQUENCE_FLAG_USE_128X128_SUPERBLOCK, "USE_128X128_SUPERBLOCK"},
		  {V4L2_AV1_SEQUENCE_FLAG_ENABLE_FILTER_INTRA, "ENABLE_FILTER_INTRA"},
		  {V4L2_AV1_SEQUENCE_FLAG_ENABLE_INTRA_EDGE_FILTER, "ENABLE_INTRA_EDGE_FILTER"},
		  {V4L2_AV1_SEQUENCE_FLAG_ENABLE_INTERINTRA_COMPOUND, "ENABLE_INTERINTRA_COMPOUND"},
		  {V4L2_AV1_SEQUENCE_FLAG_ENABLE_MASKED_COMPOUND, "ENABLE_MASKED_COMPOUND"},
		  {V4L2_AV1_SEQUENCE_FLAG_ENABLE_WARPED_MOTION, "ENABLE_WARPED_MOTION"},
		  {V4L2_AV1_SEQUENCE_FLAG_ENABLE_DUAL_FILTER, "ENABLE_DUAL_FILTER"},
		  {V4L2_AV1_SEQUENCE_FLAG_ENABLE_ORDER_HINT, "ENABLE_ORDER_HINT"},
		  {V4L2_AV1_SEQUENCE_FLAG_ENABLE_JNT_COMP, "ENABLE_JNT_COMP"},
		  {V4L2_AV1_SEQUENCE_FLAG_ENABLE_REF_FRAME_MVS, "ENABLE_REF_FRAME_MVS"},
		  {V4L2_AV1_SEQUENCE_FLAG_ENABLE_SUPERRES, "ENABLE_SUPERRES"},
		  {V4L2_AV1_SEQUENCE_FLAG_ENABLE_CDEF, "ENABLE_CDEF"},
		  {V4L2_AV1_SEQUENCE_FLAG_ENABLE_RESTORATION, "ENABLE_RESTORATION"},
		  {V4L2_AV1_SEQUENCE_FLAG_MONO_CHROME, "MONO_CHROME"},
		  {V4L2_AV1_SEQUENCE_FLAG_COLOR_RANGE, "COLOR_RANGE"},
		  {V4L2_AV1_SEQUENCE_FLAG_SUBSAMPLING_X, "SUBSAMPLING_X"},
		  {V4L2_AV1_SEQUENCE_FLAG_SUBSAMPLING_Y, "SUBSAMPLING_Y"},
		  {V4L2_AV1_SEQUENCE_FLAG_FILM_GRAIN_PARAMS_PRESENT, "FILM_GRAIN_PARAMS_PRESENT"},
		  {V4L2_AV1_SEQUENCE_FLAG_SEPARATE_UV_DELTA_Q, "SEPARATE_UV_DELTA_Q"}),
		  __entry->s.seq_profile,
		  __entry->s.order_hint_bits,
		  __entry->s.bit_depth,
		  __entry->s.max_frame_width_minus_1,
		  __entry->s.max_frame_height_minus_1
	)
);

DECLARE_EVENT_CLASS(v4l2_ctrl_av1_tge_tmpl,
	TP_PROTO(const struct v4l2_ctrl_av1_tile_group_entry *t),
	TP_ARGS(t),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_av1_tile_group_entry, t)),
	TP_fast_assign(__entry->t = *t;),
	TP_printk("\ntile_offset: %u\n tile_size: %u\n tile_row: %u\ntile_col: %u\n",
		  __entry->t.tile_offset,
		  __entry->t.tile_size,
		  __entry->t.tile_row,
		  __entry->t.tile_col
	)
);

DECLARE_EVENT_CLASS(v4l2_ctrl_av1_frame_tmpl,
	TP_PROTO(const struct v4l2_ctrl_av1_frame *f),
	TP_ARGS(f),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_av1_frame, f)),
	TP_fast_assign(__entry->f = *f;),
	TP_printk("\ntile_info.flags: %s\ntile_info.context_update_tile_id: %u\n"
		  "tile_info.tile_cols: %u\ntile_info.tile_rows: %u\n"
		  "tile_info.mi_col_starts: %s\ntile_info.mi_row_starts: %s\n"
		  "tile_info.width_in_sbs_minus_1: %s\ntile_info.height_in_sbs_minus_1: %s\n"
		  "tile_info.tile_size_bytes: %u\nquantization.flags: %s\n"
		  "quantization.base_q_idx: %u\nquantization.delta_q_y_dc: %d\n"
		  "quantization.delta_q_u_dc: %d\nquantization.delta_q_u_ac: %d\n"
		  "quantization.delta_q_v_dc: %d\nquantization.delta_q_v_ac: %d\n"
		  "quantization.qm_y: %u\nquantization.qm_u: %u\nquantization.qm_v: %u\n"
		  "quantization.delta_q_res: %u\nsuperres_denom: %u\nsegmentation.flags: %s\n"
		  "segmentation.last_active_seg_id: %u\nsegmentation.feature_enabled:%s\n"
		  "loop_filter.flags: %s\nloop_filter.level: %s\nloop_filter.sharpness: %u\n"
		  "loop_filter.ref_deltas: %s\nloop_filter.mode_deltas: %s\n"
		  "loop_filter.delta_lf_res: %u\ncdef.damping_minus_3: %u\ncdef.bits: %u\n"
		  "cdef.y_pri_strength: %s\ncdef.y_sec_strength: %s\n"
		  "cdef.uv_pri_strength: %s\ncdef.uv_sec_strength:%s\nskip_mode_frame: %s\n"
		  "primary_ref_frame: %u\nloop_restoration.flags: %s\n"
		  "loop_restoration.lr_unit_shift: %u\nloop_restoration.lr_uv_shift: %u\n"
		  "loop_restoration.frame_restoration_type: %s\n"
		  "loop_restoration.loop_restoration_size: %s\nflags: %s\norder_hint: %u\n"
		  "upscaled_width: %u\nframe_width_minus_1: %u\nframe_height_minus_1: %u\n"
		  "render_width_minus_1: %u\nrender_height_minus_1: %u\ncurrent_frame_id: %u\n"
		  "buffer_removal_time: %s\norder_hints: %s\nreference_frame_ts: %s\n"
		  "ref_frame_idx: %s\nrefresh_frame_flags: %u\n",
		  __print_flags(__entry->f.tile_info.flags, "|",
		  {V4L2_AV1_TILE_INFO_FLAG_UNIFORM_TILE_SPACING, "UNIFORM_TILE_SPACING"}),
		  __entry->f.tile_info.context_update_tile_id,
		  __entry->f.tile_info.tile_cols,
		  __entry->f.tile_info.tile_rows,
		  __print_array(__entry->f.tile_info.mi_col_starts,
				ARRAY_SIZE(__entry->f.tile_info.mi_col_starts),
				sizeof(__entry->f.tile_info.mi_col_starts[0])),
		  __print_array(__entry->f.tile_info.mi_row_starts,
				ARRAY_SIZE(__entry->f.tile_info.mi_row_starts),
				sizeof(__entry->f.tile_info.mi_row_starts[0])),
		  __print_array(__entry->f.tile_info.width_in_sbs_minus_1,
				ARRAY_SIZE(__entry->f.tile_info.width_in_sbs_minus_1),
				sizeof(__entry->f.tile_info.width_in_sbs_minus_1[0])),
		  __print_array(__entry->f.tile_info.height_in_sbs_minus_1,
				ARRAY_SIZE(__entry->f.tile_info.height_in_sbs_minus_1),
				sizeof(__entry->f.tile_info.height_in_sbs_minus_1[0])),
		  __entry->f.tile_info.tile_size_bytes,
		  __print_flags(__entry->f.quantization.flags, "|",
		  {V4L2_AV1_QUANTIZATION_FLAG_DIFF_UV_DELTA, "DIFF_UV_DELTA"},
		  {V4L2_AV1_QUANTIZATION_FLAG_USING_QMATRIX, "USING_QMATRIX"},
		  {V4L2_AV1_QUANTIZATION_FLAG_DELTA_Q_PRESENT, "DELTA_Q_PRESENT"}),
		  __entry->f.quantization.base_q_idx,
		  __entry->f.quantization.delta_q_y_dc,
		  __entry->f.quantization.delta_q_u_dc,
		  __entry->f.quantization.delta_q_u_ac,
		  __entry->f.quantization.delta_q_v_dc,
		  __entry->f.quantization.delta_q_v_ac,
		  __entry->f.quantization.qm_y,
		  __entry->f.quantization.qm_u,
		  __entry->f.quantization.qm_v,
		  __entry->f.quantization.delta_q_res,
		  __entry->f.superres_denom,
		  __print_flags(__entry->f.segmentation.flags, "|",
		  {V4L2_AV1_SEGMENTATION_FLAG_ENABLED, "ENABLED"},
		  {V4L2_AV1_SEGMENTATION_FLAG_UPDATE_MAP, "UPDATE_MAP"},
		  {V4L2_AV1_SEGMENTATION_FLAG_TEMPORAL_UPDATE, "TEMPORAL_UPDATE"},
		  {V4L2_AV1_SEGMENTATION_FLAG_UPDATE_DATA, "UPDATE_DATA"},
		  {V4L2_AV1_SEGMENTATION_FLAG_SEG_ID_PRE_SKIP, "SEG_ID_PRE_SKIP"}),
		  __entry->f.segmentation.last_active_seg_id,
		  __print_array(__entry->f.segmentation.feature_enabled,
				ARRAY_SIZE(__entry->f.segmentation.feature_enabled),
				sizeof(__entry->f.segmentation.feature_enabled[0])),
		  __print_flags(__entry->f.loop_filter.flags, "|",
		  {V4L2_AV1_LOOP_FILTER_FLAG_DELTA_ENABLED, "DELTA_ENABLED"},
		  {V4L2_AV1_LOOP_FILTER_FLAG_DELTA_UPDATE, "DELTA_UPDATE"},
		  {V4L2_AV1_LOOP_FILTER_FLAG_DELTA_LF_PRESENT, "DELTA_LF_PRESENT"},
		  {V4L2_AV1_LOOP_FILTER_FLAG_DELTA_LF_MULTI, "DELTA_LF_MULTI"}),
		  __print_array(__entry->f.loop_filter.level,
				ARRAY_SIZE(__entry->f.loop_filter.level),
				sizeof(__entry->f.loop_filter.level[0])),
		  __entry->f.loop_filter.sharpness,
		  __print_array(__entry->f.loop_filter.ref_deltas,
				ARRAY_SIZE(__entry->f.loop_filter.ref_deltas),
				sizeof(__entry->f.loop_filter.ref_deltas[0])),
		  __print_array(__entry->f.loop_filter.mode_deltas,
				ARRAY_SIZE(__entry->f.loop_filter.mode_deltas),
				sizeof(__entry->f.loop_filter.mode_deltas[0])),
		  __entry->f.loop_filter.delta_lf_res,
		  __entry->f.cdef.damping_minus_3,
		  __entry->f.cdef.bits,
		  __print_array(__entry->f.cdef.y_pri_strength,
				ARRAY_SIZE(__entry->f.cdef.y_pri_strength),
				sizeof(__entry->f.cdef.y_pri_strength[0])),
		  __print_array(__entry->f.cdef.y_sec_strength,
				ARRAY_SIZE(__entry->f.cdef.y_sec_strength),
				sizeof(__entry->f.cdef.y_sec_strength[0])),
		  __print_array(__entry->f.cdef.uv_pri_strength,
				ARRAY_SIZE(__entry->f.cdef.uv_pri_strength),
				sizeof(__entry->f.cdef.uv_pri_strength[0])),
		  __print_array(__entry->f.cdef.uv_sec_strength,
				ARRAY_SIZE(__entry->f.cdef.uv_sec_strength),
				sizeof(__entry->f.cdef.uv_sec_strength[0])),
		  __print_array(__entry->f.skip_mode_frame,
				ARRAY_SIZE(__entry->f.skip_mode_frame),
				sizeof(__entry->f.skip_mode_frame[0])),
		  __entry->f.primary_ref_frame,
		  __print_flags(__entry->f.loop_restoration.flags, "|",
		  {V4L2_AV1_LOOP_RESTORATION_FLAG_USES_LR, "USES_LR"},
		  {V4L2_AV1_LOOP_RESTORATION_FLAG_USES_CHROMA_LR, "USES_CHROMA_LR"}),
		  __entry->f.loop_restoration.lr_unit_shift,
		  __entry->f.loop_restoration.lr_uv_shift,
		  __print_array(__entry->f.loop_restoration.frame_restoration_type,
				ARRAY_SIZE(__entry->f.loop_restoration.frame_restoration_type),
				sizeof(__entry->f.loop_restoration.frame_restoration_type[0])),
		  __print_array(__entry->f.loop_restoration.loop_restoration_size,
				ARRAY_SIZE(__entry->f.loop_restoration.loop_restoration_size),
				sizeof(__entry->f.loop_restoration.loop_restoration_size[0])),
		  __print_flags(__entry->f.flags, "|",
		  {V4L2_AV1_FRAME_FLAG_SHOW_FRAME, "SHOW_FRAME"},
		  {V4L2_AV1_FRAME_FLAG_SHOWABLE_FRAME, "SHOWABLE_FRAME"},
		  {V4L2_AV1_FRAME_FLAG_ERROR_RESILIENT_MODE, "ERROR_RESILIENT_MODE"},
		  {V4L2_AV1_FRAME_FLAG_DISABLE_CDF_UPDATE, "DISABLE_CDF_UPDATE"},
		  {V4L2_AV1_FRAME_FLAG_ALLOW_SCREEN_CONTENT_TOOLS, "ALLOW_SCREEN_CONTENT_TOOLS"},
		  {V4L2_AV1_FRAME_FLAG_FORCE_INTEGER_MV, "FORCE_INTEGER_MV"},
		  {V4L2_AV1_FRAME_FLAG_ALLOW_INTRABC, "ALLOW_INTRABC"},
		  {V4L2_AV1_FRAME_FLAG_USE_SUPERRES, "USE_SUPERRES"},
		  {V4L2_AV1_FRAME_FLAG_ALLOW_HIGH_PRECISION_MV, "ALLOW_HIGH_PRECISION_MV"},
		  {V4L2_AV1_FRAME_FLAG_IS_MOTION_MODE_SWITCHABLE, "IS_MOTION_MODE_SWITCHABLE"},
		  {V4L2_AV1_FRAME_FLAG_USE_REF_FRAME_MVS, "USE_REF_FRAME_MVS"},
		  {V4L2_AV1_FRAME_FLAG_DISABLE_FRAME_END_UPDATE_CDF,
		   "DISABLE_FRAME_END_UPDATE_CDF"},
		  {V4L2_AV1_FRAME_FLAG_ALLOW_WARPED_MOTION, "ALLOW_WARPED_MOTION"},
		  {V4L2_AV1_FRAME_FLAG_REFERENCE_SELECT, "REFERENCE_SELECT"},
		  {V4L2_AV1_FRAME_FLAG_REDUCED_TX_SET, "REDUCED_TX_SET"},
		  {V4L2_AV1_FRAME_FLAG_SKIP_MODE_ALLOWED, "SKIP_MODE_ALLOWED"},
		  {V4L2_AV1_FRAME_FLAG_SKIP_MODE_PRESENT, "SKIP_MODE_PRESENT"},
		  {V4L2_AV1_FRAME_FLAG_FRAME_SIZE_OVERRIDE, "FRAME_SIZE_OVERRIDE"},
		  {V4L2_AV1_FRAME_FLAG_BUFFER_REMOVAL_TIME_PRESENT, "BUFFER_REMOVAL_TIME_PRESENT"},
		  {V4L2_AV1_FRAME_FLAG_FRAME_REFS_SHORT_SIGNALING, "FRAME_REFS_SHORT_SIGNALING"}),
		  __entry->f.order_hint,
		  __entry->f.upscaled_width,
		  __entry->f.frame_width_minus_1,
		  __entry->f.frame_height_minus_1,
		  __entry->f.render_width_minus_1,
		  __entry->f.render_height_minus_1,
		  __entry->f.current_frame_id,
		  __print_array(__entry->f.buffer_removal_time,
				ARRAY_SIZE(__entry->f.buffer_removal_time),
				sizeof(__entry->f.buffer_removal_time[0])),
		  __print_array(__entry->f.order_hints,
				ARRAY_SIZE(__entry->f.order_hints),
				sizeof(__entry->f.order_hints[0])),
		  __print_array(__entry->f.reference_frame_ts,
				ARRAY_SIZE(__entry->f.reference_frame_ts),
				sizeof(__entry->f.reference_frame_ts[0])),
		  __print_array(__entry->f.ref_frame_idx,
				ARRAY_SIZE(__entry->f.ref_frame_idx),
				sizeof(__entry->f.ref_frame_idx[0])),
		  __entry->f.refresh_frame_flags
	)
);


DECLARE_EVENT_CLASS(v4l2_ctrl_av1_film_grain_tmpl,
	TP_PROTO(const struct v4l2_ctrl_av1_film_grain *f),
	TP_ARGS(f),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_av1_film_grain, f)),
	TP_fast_assign(__entry->f = *f;),
	TP_printk("\nflags %s\ncr_mult: %u\ngrain_seed: %u\n"
		  "film_grain_params_ref_idx: %u\nnum_y_points: %u\npoint_y_value: %s\n"
		  "point_y_scaling: %s\nnum_cb_points: %u\npoint_cb_value: %s\n"
		  "point_cb_scaling: %s\nnum_cr_points: %u\npoint_cr_value: %s\n"
		  "point_cr_scaling: %s\ngrain_scaling_minus_8: %u\nar_coeff_lag: %u\n"
		  "ar_coeffs_y_plus_128: %s\nar_coeffs_cb_plus_128: %s\n"
		  "ar_coeffs_cr_plus_128: %s\nar_coeff_shift_minus_6: %u\n"
		  "grain_scale_shift: %u\ncb_mult: %u\ncb_luma_mult: %u\ncr_luma_mult: %u\n"
		  "cb_offset: %u\ncr_offset: %u\n",
		  __print_flags(__entry->f.flags, "|",
		  {V4L2_AV1_FILM_GRAIN_FLAG_APPLY_GRAIN, "APPLY_GRAIN"},
		  {V4L2_AV1_FILM_GRAIN_FLAG_UPDATE_GRAIN, "UPDATE_GRAIN"},
		  {V4L2_AV1_FILM_GRAIN_FLAG_CHROMA_SCALING_FROM_LUMA, "CHROMA_SCALING_FROM_LUMA"},
		  {V4L2_AV1_FILM_GRAIN_FLAG_OVERLAP, "OVERLAP"},
		  {V4L2_AV1_FILM_GRAIN_FLAG_CLIP_TO_RESTRICTED_RANGE, "CLIP_TO_RESTRICTED_RANGE"}),
		  __entry->f.cr_mult,
		  __entry->f.grain_seed,
		  __entry->f.film_grain_params_ref_idx,
		  __entry->f.num_y_points,
		  __print_array(__entry->f.point_y_value,
				ARRAY_SIZE(__entry->f.point_y_value),
				sizeof(__entry->f.point_y_value[0])),
		  __print_array(__entry->f.point_y_scaling,
				ARRAY_SIZE(__entry->f.point_y_scaling),
				sizeof(__entry->f.point_y_scaling[0])),
		  __entry->f.num_cb_points,
		  __print_array(__entry->f.point_cb_value,
				ARRAY_SIZE(__entry->f.point_cb_value),
				sizeof(__entry->f.point_cb_value[0])),
		  __print_array(__entry->f.point_cb_scaling,
				ARRAY_SIZE(__entry->f.point_cb_scaling),
				sizeof(__entry->f.point_cb_scaling[0])),
		  __entry->f.num_cr_points,
		  __print_array(__entry->f.point_cr_value,
				ARRAY_SIZE(__entry->f.point_cr_value),
				sizeof(__entry->f.point_cr_value[0])),
		  __print_array(__entry->f.point_cr_scaling,
				ARRAY_SIZE(__entry->f.point_cr_scaling),
				sizeof(__entry->f.point_cr_scaling[0])),
		  __entry->f.grain_scaling_minus_8,
		  __entry->f.ar_coeff_lag,
		  __print_array(__entry->f.ar_coeffs_y_plus_128,
				ARRAY_SIZE(__entry->f.ar_coeffs_y_plus_128),
				sizeof(__entry->f.ar_coeffs_y_plus_128[0])),
		  __print_array(__entry->f.ar_coeffs_cb_plus_128,
				ARRAY_SIZE(__entry->f.ar_coeffs_cb_plus_128),
				sizeof(__entry->f.ar_coeffs_cb_plus_128[0])),
		  __print_array(__entry->f.ar_coeffs_cr_plus_128,
				ARRAY_SIZE(__entry->f.ar_coeffs_cr_plus_128),
				sizeof(__entry->f.ar_coeffs_cr_plus_128[0])),
		  __entry->f.ar_coeff_shift_minus_6,
		  __entry->f.grain_scale_shift,
		  __entry->f.cb_mult,
		  __entry->f.cb_luma_mult,
		  __entry->f.cr_luma_mult,
		  __entry->f.cb_offset,
		  __entry->f.cr_offset
	)
)

DEFINE_EVENT(v4l2_ctrl_av1_seq_tmpl, v4l2_ctrl_av1_sequence,
	TP_PROTO(const struct v4l2_ctrl_av1_sequence *s),
	TP_ARGS(s)
);

DEFINE_EVENT(v4l2_ctrl_av1_frame_tmpl, v4l2_ctrl_av1_frame,
	TP_PROTO(const struct v4l2_ctrl_av1_frame *f),
	TP_ARGS(f)
);

DEFINE_EVENT(v4l2_ctrl_av1_tge_tmpl, v4l2_ctrl_av1_tile_group_entry,
	TP_PROTO(const struct v4l2_ctrl_av1_tile_group_entry *t),
	TP_ARGS(t)
);

DEFINE_EVENT(v4l2_ctrl_av1_film_grain_tmpl, v4l2_ctrl_av1_film_grain,
	TP_PROTO(const struct v4l2_ctrl_av1_film_grain *f),
	TP_ARGS(f)
);

#endif

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/media/test-drivers/visl
#define TRACE_INCLUDE_FILE visl-trace-av1
#include <trace/define_trace.h>
