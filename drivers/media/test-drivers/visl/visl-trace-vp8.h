/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_VISL_TRACE_VP8_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _VISL_TRACE_VP8_H_

#include <linux/tracepoint.h>
#include "visl.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM visl_vp8_controls

DECLARE_EVENT_CLASS(v4l2_ctrl_vp8_entropy_tmpl,
	TP_PROTO(const struct v4l2_ctrl_vp8_frame *f),
	TP_ARGS(f),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_vp8_frame, f)),
	TP_fast_assign(__entry->f = *f;),
	TP_printk("\nentropy.coeff_probs {%s}\n"
		  "entropy.y_mode_probs %s\n"
		  "entropy.uv_mode_probs %s\n"
		  "entropy.mv_probs {%s}",
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->f.entropy.coeff_probs,
				   sizeof(__entry->f.entropy.coeff_probs),
				   false),
		  __print_array(__entry->f.entropy.y_mode_probs,
				ARRAY_SIZE(__entry->f.entropy.y_mode_probs),
				sizeof(__entry->f.entropy.y_mode_probs[0])),
		  __print_array(__entry->f.entropy.uv_mode_probs,
				ARRAY_SIZE(__entry->f.entropy.uv_mode_probs),
				sizeof(__entry->f.entropy.uv_mode_probs[0])),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->f.entropy.mv_probs,
				   sizeof(__entry->f.entropy.mv_probs),
				   false)
		  )
)

DECLARE_EVENT_CLASS(v4l2_ctrl_vp8_frame_tmpl,
	TP_PROTO(const struct v4l2_ctrl_vp8_frame *f),
	TP_ARGS(f),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_vp8_frame, f)),
	TP_fast_assign(__entry->f = *f;),
	TP_printk("\nsegment.quant_update %s\n"
		  "segment.lf_update %s\n"
		  "segment.segment_probs %s\n"
		  "segment.flags %s\n"
		  "lf.ref_frm_delta %s\n"
		  "lf.mb_mode_delta %s\n"
		  "lf.sharpness_level %u\n"
		  "lf.level %u\n"
		  "lf.flags %s\n"
		  "quant.y_ac_qi %u\n"
		  "quant.y_dc_delta %d\n"
		  "quant.y2_dc_delta %d\n"
		  "quant.y2_ac_delta %d\n"
		  "quant.uv_dc_delta %d\n"
		  "quant.uv_ac_delta %d\n"
		  "coder_state.range %u\n"
		  "coder_state.value %u\n"
		  "coder_state.bit_count %u\n"
		  "width %u\n"
		  "height %u\n"
		  "horizontal_scale %u\n"
		  "vertical_scale %u\n"
		  "version %u\n"
		  "prob_skip_false %u\n"
		  "prob_intra %u\n"
		  "prob_last %u\n"
		  "prob_gf %u\n"
		  "num_dct_parts %u\n"
		  "first_part_size %u\n"
		  "first_part_header_bits %u\n"
		  "dct_part_sizes %s\n"
		  "last_frame_ts %llu\n"
		  "golden_frame_ts %llu\n"
		  "alt_frame_ts %llu\n"
		  "flags %s",
		  __print_array(__entry->f.segment.quant_update,
				ARRAY_SIZE(__entry->f.segment.quant_update),
				sizeof(__entry->f.segment.quant_update[0])),
		  __print_array(__entry->f.segment.lf_update,
				ARRAY_SIZE(__entry->f.segment.lf_update),
				sizeof(__entry->f.segment.lf_update[0])),
		  __print_array(__entry->f.segment.segment_probs,
				ARRAY_SIZE(__entry->f.segment.segment_probs),
				sizeof(__entry->f.segment.segment_probs[0])),
		  __print_flags(__entry->f.segment.flags, "|",
		  {V4L2_VP8_SEGMENT_FLAG_ENABLED, "SEGMENT_ENABLED"},
		  {V4L2_VP8_SEGMENT_FLAG_UPDATE_MAP, "SEGMENT_UPDATE_MAP"},
		  {V4L2_VP8_SEGMENT_FLAG_UPDATE_FEATURE_DATA, "SEGMENT_UPDATE_FEATURE_DATA"},
		  {V4L2_VP8_SEGMENT_FLAG_DELTA_VALUE_MODE, "SEGMENT_DELTA_VALUE_MODE"}),
		  __print_array(__entry->f.lf.ref_frm_delta,
				ARRAY_SIZE(__entry->f.lf.ref_frm_delta),
				sizeof(__entry->f.lf.ref_frm_delta[0])),
		  __print_array(__entry->f.lf.mb_mode_delta,
				ARRAY_SIZE(__entry->f.lf.mb_mode_delta),
				sizeof(__entry->f.lf.mb_mode_delta[0])),
		  __entry->f.lf.sharpness_level,
		  __entry->f.lf.level,
		  __print_flags(__entry->f.lf.flags, "|",
		  {V4L2_VP8_LF_ADJ_ENABLE, "LF_ADJ_ENABLED"},
		  {V4L2_VP8_LF_DELTA_UPDATE, "LF_DELTA_UPDATE"},
		  {V4L2_VP8_LF_FILTER_TYPE_SIMPLE, "LF_FILTER_TYPE_SIMPLE"}),
		  __entry->f.quant.y_ac_qi,
		  __entry->f.quant.y_dc_delta,
		  __entry->f.quant.y2_dc_delta,
		  __entry->f.quant.y2_ac_delta,
		  __entry->f.quant.uv_dc_delta,
		  __entry->f.quant.uv_ac_delta,
		  __entry->f.coder_state.range,
		  __entry->f.coder_state.value,
		  __entry->f.coder_state.bit_count,
		  __entry->f.width,
		  __entry->f.height,
		  __entry->f.horizontal_scale,
		  __entry->f.vertical_scale,
		  __entry->f.version,
		  __entry->f.prob_skip_false,
		  __entry->f.prob_intra,
		  __entry->f.prob_last,
		  __entry->f.prob_gf,
		  __entry->f.num_dct_parts,
		  __entry->f.first_part_size,
		  __entry->f.first_part_header_bits,
		  __print_array(__entry->f.dct_part_sizes,
				ARRAY_SIZE(__entry->f.dct_part_sizes),
				sizeof(__entry->f.dct_part_sizes[0])),
		  __entry->f.last_frame_ts,
		  __entry->f.golden_frame_ts,
		  __entry->f.alt_frame_ts,
		  __print_flags(__entry->f.flags, "|",
		  {V4L2_VP8_FRAME_FLAG_KEY_FRAME, "KEY_FRAME"},
		  {V4L2_VP8_FRAME_FLAG_EXPERIMENTAL, "EXPERIMENTAL"},
		  {V4L2_VP8_FRAME_FLAG_SHOW_FRAME, "SHOW_FRAME"},
		  {V4L2_VP8_FRAME_FLAG_MB_NO_SKIP_COEFF, "MB_NO_SKIP_COEFF"},
		  {V4L2_VP8_FRAME_FLAG_SIGN_BIAS_GOLDEN, "SIGN_BIAS_GOLDEN"},
		  {V4L2_VP8_FRAME_FLAG_SIGN_BIAS_ALT, "SIGN_BIAS_ALT"})
		  )
);

DEFINE_EVENT(v4l2_ctrl_vp8_frame_tmpl, v4l2_ctrl_vp8_frame,
	TP_PROTO(const struct v4l2_ctrl_vp8_frame *f),
	TP_ARGS(f)
);

DEFINE_EVENT(v4l2_ctrl_vp8_entropy_tmpl, v4l2_ctrl_vp8_entropy,
	TP_PROTO(const struct v4l2_ctrl_vp8_frame *f),
	TP_ARGS(f)
);

#endif

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/media/test-drivers/visl
#define TRACE_INCLUDE_FILE visl-trace-vp8
#include <trace/define_trace.h>
