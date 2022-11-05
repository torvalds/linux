/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_VISL_TRACE_VP9_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _VISL_TRACE_VP9_H_

#include <linux/tracepoint.h>
#include "visl.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM visl_vp9_controls

DECLARE_EVENT_CLASS(v4l2_ctrl_vp9_frame_tmpl,
	TP_PROTO(const struct v4l2_ctrl_vp9_frame *f),
	TP_ARGS(f),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_vp9_frame, f)),
	TP_fast_assign(__entry->f = *f;),
	TP_printk("\nlf.ref_deltas %s\n"
		  "lf.mode_deltas %s\n"
		  "lf.level %u\n"
		  "lf.sharpness %u\n"
		  "lf.flags %s\n"
		  "quant.base_q_idx %u\n"
		  "quant.delta_q_y_dc %d\n"
		  "quant.delta_q_uv_dc %d\n"
		  "quant.delta_q_uv_ac %d\n"
		  "seg.feature_data {%s}\n"
		  "seg.feature_enabled %s\n"
		  "seg.tree_probs %s\n"
		  "seg.pred_probs %s\n"
		  "seg.flags %s\n"
		  "flags %s\n"
		  "compressed_header_size %u\n"
		  "uncompressed_header_size %u\n"
		  "frame_width_minus_1 %u\n"
		  "frame_height_minus_1 %u\n"
		  "render_width_minus_1 %u\n"
		  "render_height_minus_1 %u\n"
		  "last_frame_ts %llu\n"
		  "golden_frame_ts %llu\n"
		  "alt_frame_ts %llu\n"
		  "ref_frame_sign_bias %s\n"
		  "reset_frame_context %s\n"
		  "frame_context_idx %u\n"
		  "profile %u\n"
		  "bit_depth %u\n"
		  "interpolation_filter %s\n"
		  "tile_cols_log2 %u\n"
		  "tile_rows_log_2 %u\n"
		  "reference_mode %s\n",
		  __print_array(__entry->f.lf.ref_deltas,
				ARRAY_SIZE(__entry->f.lf.ref_deltas),
				sizeof(__entry->f.lf.ref_deltas[0])),
		  __print_array(__entry->f.lf.mode_deltas,
				ARRAY_SIZE(__entry->f.lf.mode_deltas),
				sizeof(__entry->f.lf.mode_deltas[0])),
		  __entry->f.lf.level,
		  __entry->f.lf.sharpness,
		  __print_flags(__entry->f.lf.flags, "|",
		  {V4L2_VP9_LOOP_FILTER_FLAG_DELTA_ENABLED, "DELTA_ENABLED"},
		  {V4L2_VP9_LOOP_FILTER_FLAG_DELTA_UPDATE, "DELTA_UPDATE"}),
		  __entry->f.quant.base_q_idx,
		  __entry->f.quant.delta_q_y_dc,
		  __entry->f.quant.delta_q_uv_dc,
		  __entry->f.quant.delta_q_uv_ac,
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->f.seg.feature_data,
				   sizeof(__entry->f.seg.feature_data),
				   false),
		  __print_array(__entry->f.seg.feature_enabled,
				ARRAY_SIZE(__entry->f.seg.feature_enabled),
				sizeof(__entry->f.seg.feature_enabled[0])),
		  __print_array(__entry->f.seg.tree_probs,
				ARRAY_SIZE(__entry->f.seg.tree_probs),
				sizeof(__entry->f.seg.tree_probs[0])),
		  __print_array(__entry->f.seg.pred_probs,
				ARRAY_SIZE(__entry->f.seg.pred_probs),
				sizeof(__entry->f.seg.pred_probs[0])),
		  __print_flags(__entry->f.seg.flags, "|",
		  {V4L2_VP9_SEGMENTATION_FLAG_ENABLED, "ENABLED"},
		  {V4L2_VP9_SEGMENTATION_FLAG_UPDATE_MAP, "UPDATE_MAP"},
		  {V4L2_VP9_SEGMENTATION_FLAG_TEMPORAL_UPDATE, "TEMPORAL_UPDATE"},
		  {V4L2_VP9_SEGMENTATION_FLAG_UPDATE_DATA, "UPDATE_DATA"},
		  {V4L2_VP9_SEGMENTATION_FLAG_ABS_OR_DELTA_UPDATE, "ABS_OR_DELTA_UPDATE"}),
		  __print_flags(__entry->f.flags, "|",
		  {V4L2_VP9_FRAME_FLAG_KEY_FRAME, "KEY_FRAME"},
		  {V4L2_VP9_FRAME_FLAG_SHOW_FRAME, "SHOW_FRAME"},
		  {V4L2_VP9_FRAME_FLAG_ERROR_RESILIENT, "ERROR_RESILIENT"},
		  {V4L2_VP9_FRAME_FLAG_INTRA_ONLY, "INTRA_ONLY"},
		  {V4L2_VP9_FRAME_FLAG_ALLOW_HIGH_PREC_MV, "ALLOW_HIGH_PREC_MV"},
		  {V4L2_VP9_FRAME_FLAG_REFRESH_FRAME_CTX, "REFRESH_FRAME_CTX"},
		  {V4L2_VP9_FRAME_FLAG_PARALLEL_DEC_MODE, "PARALLEL_DEC_MODE"},
		  {V4L2_VP9_FRAME_FLAG_X_SUBSAMPLING, "X_SUBSAMPLING"},
		  {V4L2_VP9_FRAME_FLAG_Y_SUBSAMPLING, "Y_SUBSAMPLING"},
		  {V4L2_VP9_FRAME_FLAG_COLOR_RANGE_FULL_SWING, "COLOR_RANGE_FULL_SWING"}),
		  __entry->f.compressed_header_size,
		  __entry->f.uncompressed_header_size,
		  __entry->f.frame_width_minus_1,
		  __entry->f.frame_height_minus_1,
		  __entry->f.render_width_minus_1,
		  __entry->f.render_height_minus_1,
		  __entry->f.last_frame_ts,
		  __entry->f.golden_frame_ts,
		  __entry->f.alt_frame_ts,
		  __print_symbolic(__entry->f.ref_frame_sign_bias,
		  {V4L2_VP9_SIGN_BIAS_LAST, "SIGN_BIAS_LAST"},
		  {V4L2_VP9_SIGN_BIAS_GOLDEN, "SIGN_BIAS_GOLDEN"},
		  {V4L2_VP9_SIGN_BIAS_ALT, "SIGN_BIAS_ALT"}),
		  __print_symbolic(__entry->f.reset_frame_context,
		  {V4L2_VP9_RESET_FRAME_CTX_NONE, "RESET_FRAME_CTX_NONE"},
		  {V4L2_VP9_RESET_FRAME_CTX_SPEC, "RESET_FRAME_CTX_SPEC"},
		  {V4L2_VP9_RESET_FRAME_CTX_ALL, "RESET_FRAME_CTX_ALL"}),
		  __entry->f.frame_context_idx,
		  __entry->f.profile,
		  __entry->f.bit_depth,
		  __print_symbolic(__entry->f.interpolation_filter,
		  {V4L2_VP9_INTERP_FILTER_EIGHTTAP, "INTERP_FILTER_EIGHTTAP"},
		  {V4L2_VP9_INTERP_FILTER_EIGHTTAP_SMOOTH, "INTERP_FILTER_EIGHTTAP_SMOOTH"},
		  {V4L2_VP9_INTERP_FILTER_EIGHTTAP_SHARP, "INTERP_FILTER_EIGHTTAP_SHARP"},
		  {V4L2_VP9_INTERP_FILTER_BILINEAR, "INTERP_FILTER_BILINEAR"},
		  {V4L2_VP9_INTERP_FILTER_SWITCHABLE, "INTERP_FILTER_SWITCHABLE"}),
		  __entry->f.tile_cols_log2,
		  __entry->f.tile_rows_log2,
		  __print_symbolic(__entry->f.reference_mode,
		  {V4L2_VP9_REFERENCE_MODE_SINGLE_REFERENCE, "REFERENCE_MODE_SINGLE_REFERENCE"},
		  {V4L2_VP9_REFERENCE_MODE_COMPOUND_REFERENCE, "REFERENCE_MODE_COMPOUND_REFERENCE"},
		  {V4L2_VP9_REFERENCE_MODE_SELECT, "REFERENCE_MODE_SELECT"}))
);

DECLARE_EVENT_CLASS(v4l2_ctrl_vp9_compressed_hdr_tmpl,
	TP_PROTO(const struct v4l2_ctrl_vp9_compressed_hdr *h),
	TP_ARGS(h),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_vp9_compressed_hdr, h)),
	TP_fast_assign(__entry->h = *h;),
	TP_printk("\ntx_mode %s\n"
		  "tx8 {%s}\n"
		  "tx16 {%s}\n"
		  "tx32 {%s}\n"
		  "skip %s\n"
		  "inter_mode {%s}\n"
		  "interp_filter {%s}\n"
		  "is_inter %s\n"
		  "comp_mode %s\n"
		  "single_ref {%s}\n"
		  "comp_ref %s\n"
		  "y_mode {%s}\n"
		  "uv_mode {%s}\n"
		  "partition {%s}\n",
		  __print_symbolic(__entry->h.tx_mode,
		  {V4L2_VP9_TX_MODE_ONLY_4X4, "TX_MODE_ONLY_4X4"},
		  {V4L2_VP9_TX_MODE_ALLOW_8X8, "TX_MODE_ALLOW_8X8"},
		  {V4L2_VP9_TX_MODE_ALLOW_16X16, "TX_MODE_ALLOW_16X16"},
		  {V4L2_VP9_TX_MODE_ALLOW_32X32, "TX_MODE_ALLOW_32X32"},
		  {V4L2_VP9_TX_MODE_SELECT, "TX_MODE_SELECT"}),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->h.tx8,
				   sizeof(__entry->h.tx8),
				   false),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->h.tx16,
				   sizeof(__entry->h.tx16),
				   false),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->h.tx32,
				   sizeof(__entry->h.tx32),
				   false),
		  __print_array(__entry->h.skip,
				ARRAY_SIZE(__entry->h.skip),
				sizeof(__entry->h.skip[0])),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->h.inter_mode,
				   sizeof(__entry->h.inter_mode),
				   false),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->h.interp_filter,
				   sizeof(__entry->h.interp_filter),
				   false),
		  __print_array(__entry->h.is_inter,
				ARRAY_SIZE(__entry->h.is_inter),
				sizeof(__entry->h.is_inter[0])),
		  __print_array(__entry->h.comp_mode,
				ARRAY_SIZE(__entry->h.comp_mode),
				sizeof(__entry->h.comp_mode[0])),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->h.single_ref,
				   sizeof(__entry->h.single_ref),
				   false),
		  __print_array(__entry->h.comp_ref,
				ARRAY_SIZE(__entry->h.comp_ref),
				sizeof(__entry->h.comp_ref[0])),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->h.y_mode,
				   sizeof(__entry->h.y_mode),
				   false),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->h.uv_mode,
				   sizeof(__entry->h.uv_mode),
				   false),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->h.partition,
				   sizeof(__entry->h.partition),
				   false)
	)
);

DECLARE_EVENT_CLASS(v4l2_ctrl_vp9_compressed_coef_tmpl,
	TP_PROTO(const struct v4l2_ctrl_vp9_compressed_hdr *h),
	TP_ARGS(h),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_vp9_compressed_hdr, h)),
	TP_fast_assign(__entry->h = *h;),
	TP_printk("\n coef {%s}",
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->h.coef,
				   sizeof(__entry->h.coef),
				   false)
	)
);

DECLARE_EVENT_CLASS(v4l2_vp9_mv_probs_tmpl,
	TP_PROTO(const struct v4l2_vp9_mv_probs *p),
	TP_ARGS(p),
	TP_STRUCT__entry(__field_struct(struct v4l2_vp9_mv_probs, p)),
	TP_fast_assign(__entry->p = *p;),
	TP_printk("\n joint %s\n"
		  "sign %s\n"
		  "classes {%s}\n"
		  "class0_bit %s\n"
		  "bits {%s}\n"
		  "class0_fr {%s}\n"
		  "fr {%s}\n"
		  "class0_hp %s\n"
		  "hp %s\n",
		  __print_array(__entry->p.joint,
				ARRAY_SIZE(__entry->p.joint),
				sizeof(__entry->p.joint[0])),
		  __print_array(__entry->p.sign,
				ARRAY_SIZE(__entry->p.sign),
				sizeof(__entry->p.sign[0])),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->p.classes,
				   sizeof(__entry->p.classes),
				   false),
		  __print_array(__entry->p.class0_bit,
				ARRAY_SIZE(__entry->p.class0_bit),
				sizeof(__entry->p.class0_bit[0])),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->p.bits,
				   sizeof(__entry->p.bits),
				   false),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->p.class0_fr,
				   sizeof(__entry->p.class0_fr),
				   false),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->p.fr,
				   sizeof(__entry->p.fr),
				   false),
		  __print_array(__entry->p.class0_hp,
				ARRAY_SIZE(__entry->p.class0_hp),
				sizeof(__entry->p.class0_hp[0])),
		  __print_array(__entry->p.hp,
				ARRAY_SIZE(__entry->p.hp),
				sizeof(__entry->p.hp[0]))
	)
);

DEFINE_EVENT(v4l2_ctrl_vp9_frame_tmpl, v4l2_ctrl_vp9_frame,
	TP_PROTO(const struct v4l2_ctrl_vp9_frame *f),
	TP_ARGS(f)
);

DEFINE_EVENT(v4l2_ctrl_vp9_compressed_hdr_tmpl, v4l2_ctrl_vp9_compressed_hdr,
	TP_PROTO(const struct v4l2_ctrl_vp9_compressed_hdr *h),
	TP_ARGS(h)
);

DEFINE_EVENT(v4l2_ctrl_vp9_compressed_coef_tmpl, v4l2_ctrl_vp9_compressed_coeff,
	TP_PROTO(const struct v4l2_ctrl_vp9_compressed_hdr *h),
	TP_ARGS(h)
);


DEFINE_EVENT(v4l2_vp9_mv_probs_tmpl, v4l2_vp9_mv_probs,
	TP_PROTO(const struct v4l2_vp9_mv_probs *p),
	TP_ARGS(p)
);

#endif

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/media/test-drivers/visl
#define TRACE_INCLUDE_FILE visl-trace-vp9
#include <trace/define_trace.h>
