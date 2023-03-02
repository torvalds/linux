/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_VISL_TRACE_MPEG2_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _VISL_TRACE_MPEG2_H_

#include <linux/tracepoint.h>
#include "visl.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM visl_mpeg2_controls

DECLARE_EVENT_CLASS(v4l2_ctrl_mpeg2_seq_tmpl,
	TP_PROTO(const struct v4l2_ctrl_mpeg2_sequence *s),
	TP_ARGS(s),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_mpeg2_sequence, s)),
	TP_fast_assign(__entry->s = *s;),
	TP_printk("\nhorizontal_size %u\nvertical_size %u\nvbv_buffer_size %u\n"
		  "profile_and_level_indication %u\nchroma_format %u\nflags %s\n",
		  __entry->s.horizontal_size,
		  __entry->s.vertical_size,
		  __entry->s.vbv_buffer_size,
		  __entry->s.profile_and_level_indication,
		  __entry->s.chroma_format,
		  __print_flags(__entry->s.flags, "|",
		  {V4L2_MPEG2_SEQ_FLAG_PROGRESSIVE, "PROGRESSIVE"})
	)
);

DECLARE_EVENT_CLASS(v4l2_ctrl_mpeg2_pic_tmpl,
	TP_PROTO(const struct v4l2_ctrl_mpeg2_picture *p),
	TP_ARGS(p),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_mpeg2_picture, p)),
	TP_fast_assign(__entry->p = *p;),
	TP_printk("\nbackward_ref_ts %llu\nforward_ref_ts %llu\nflags %s\nf_code {%s}\n"
		  "picture_coding_type: %u\npicture_structure %u\nintra_dc_precision %u\n",
		  __entry->p.backward_ref_ts,
		  __entry->p.forward_ref_ts,
		  __print_flags(__entry->p.flags, "|",
		  {V4L2_MPEG2_PIC_FLAG_TOP_FIELD_FIRST, "TOP_FIELD_FIRST"},
		  {V4L2_MPEG2_PIC_FLAG_FRAME_PRED_DCT, "FRAME_PRED_DCT"},
		  {V4L2_MPEG2_PIC_FLAG_CONCEALMENT_MV, "CONCEALMENT_MV"},
		  {V4L2_MPEG2_PIC_FLAG_Q_SCALE_TYPE, "Q_SCALE_TYPE"},
		  {V4L2_MPEG2_PIC_FLAG_INTRA_VLC, "INTA_VLC"},
		  {V4L2_MPEG2_PIC_FLAG_ALT_SCAN, "ALT_SCAN"},
		  {V4L2_MPEG2_PIC_FLAG_REPEAT_FIRST, "REPEAT_FIRST"},
		  {V4L2_MPEG2_PIC_FLAG_PROGRESSIVE, "PROGRESSIVE"}),
		  __print_hex_dump("", DUMP_PREFIX_NONE, 32, 1,
				   __entry->p.f_code,
				   sizeof(__entry->p.f_code),
				   false),
		  __entry->p.picture_coding_type,
		  __entry->p.picture_structure,
		  __entry->p.intra_dc_precision
	)
);

DECLARE_EVENT_CLASS(v4l2_ctrl_mpeg2_quant_tmpl,
	TP_PROTO(const struct v4l2_ctrl_mpeg2_quantisation *q),
	TP_ARGS(q),
	TP_STRUCT__entry(__field_struct(struct v4l2_ctrl_mpeg2_quantisation, q)),
	TP_fast_assign(__entry->q = *q;),
	TP_printk("\nintra_quantiser_matrix %s\nnon_intra_quantiser_matrix %s\n"
		  "chroma_intra_quantiser_matrix %s\nchroma_non_intra_quantiser_matrix %s\n",
		  __print_array(__entry->q.intra_quantiser_matrix,
				ARRAY_SIZE(__entry->q.intra_quantiser_matrix),
				sizeof(__entry->q.intra_quantiser_matrix[0])),
		  __print_array(__entry->q.non_intra_quantiser_matrix,
				ARRAY_SIZE(__entry->q.non_intra_quantiser_matrix),
				sizeof(__entry->q.non_intra_quantiser_matrix[0])),
		  __print_array(__entry->q.chroma_intra_quantiser_matrix,
				ARRAY_SIZE(__entry->q.chroma_intra_quantiser_matrix),
				sizeof(__entry->q.chroma_intra_quantiser_matrix[0])),
		  __print_array(__entry->q.chroma_non_intra_quantiser_matrix,
				ARRAY_SIZE(__entry->q.chroma_non_intra_quantiser_matrix),
				sizeof(__entry->q.chroma_non_intra_quantiser_matrix[0]))
		  )
)

DEFINE_EVENT(v4l2_ctrl_mpeg2_seq_tmpl, v4l2_ctrl_mpeg2_sequence,
	TP_PROTO(const struct v4l2_ctrl_mpeg2_sequence *s),
	TP_ARGS(s)
);

DEFINE_EVENT(v4l2_ctrl_mpeg2_pic_tmpl, v4l2_ctrl_mpeg2_picture,
	TP_PROTO(const struct v4l2_ctrl_mpeg2_picture *p),
	TP_ARGS(p)
);

DEFINE_EVENT(v4l2_ctrl_mpeg2_quant_tmpl, v4l2_ctrl_mpeg2_quantisation,
	TP_PROTO(const struct v4l2_ctrl_mpeg2_quantisation *q),
	TP_ARGS(q)
);

#endif

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/media/test-drivers/visl
#define TRACE_INCLUDE_FILE visl-trace-mpeg2
#include <trace/define_trace.h>
