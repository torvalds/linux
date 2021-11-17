/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 */

#ifndef __MESON_VDEC_HELPERS_H_
#define __MESON_VDEC_HELPERS_H_

#include "vdec.h"

/**
 * amvdec_set_canvases() - Map VB2 buffers to canvases
 *
 * @sess: current session
 * @reg_base: Registry bases of where to write the canvas indexes
 * @reg_num: number of contiguous registers after each reg_base (including it)
 */
int amvdec_set_canvases(struct amvdec_session *sess,
			u32 reg_base[], u32 reg_num[]);

/* Helpers to read/write to the various IPs (DOS, PARSER) */
u32 amvdec_read_dos(struct amvdec_core *core, u32 reg);
void amvdec_write_dos(struct amvdec_core *core, u32 reg, u32 val);
void amvdec_write_dos_bits(struct amvdec_core *core, u32 reg, u32 val);
void amvdec_clear_dos_bits(struct amvdec_core *core, u32 reg, u32 val);
u32 amvdec_read_parser(struct amvdec_core *core, u32 reg);
void amvdec_write_parser(struct amvdec_core *core, u32 reg, u32 val);

u32 amvdec_am21c_body_size(u32 width, u32 height);
u32 amvdec_am21c_head_size(u32 width, u32 height);
u32 amvdec_am21c_size(u32 width, u32 height);

/**
 * amvdec_dst_buf_done_idx() - Signal that a buffer is done decoding
 *
 * @sess: current session
 * @buf_idx: hardware buffer index
 * @offset: VIFIFO bitstream offset corresponding to the buffer
 * @field: V4L2 interlaced field
 */
void amvdec_dst_buf_done_idx(struct amvdec_session *sess, u32 buf_idx,
			     u32 offset, u32 field);
void amvdec_dst_buf_done(struct amvdec_session *sess,
			 struct vb2_v4l2_buffer *vbuf, u32 field);
void amvdec_dst_buf_done_offset(struct amvdec_session *sess,
				struct vb2_v4l2_buffer *vbuf,
				u32 offset, u32 field, bool allow_drop);

/**
 * amvdec_add_ts() - Add a timestamp to the list
 *
 * @sess: current session
 * @ts: timestamp to add
 * @offset: offset in the VIFIFO where the associated packet was written
 * @flags the vb2_v4l2_buffer flags
 */
void amvdec_add_ts(struct amvdec_session *sess, u64 ts,
		   struct v4l2_timecode tc, u32 offset, u32 flags);
void amvdec_remove_ts(struct amvdec_session *sess, u64 ts);

/**
 * amvdec_set_par_from_dar() - Set Pixel Aspect Ratio from Display Aspect Ratio
 *
 * @sess: current session
 * @dar_num: numerator of the DAR
 * @dar_den: denominator of the DAR
 */
void amvdec_set_par_from_dar(struct amvdec_session *sess,
			     u32 dar_num, u32 dar_den);

/**
 * amvdec_src_change() - Notify new resolution/DPB size to the core
 *
 * @sess: current session
 * @width: picture width detected by the hardware
 * @height: picture height detected by the hardware
 * @dpb_size: Decoded Picture Buffer size (= amount of buffers for decoding)
 */
void amvdec_src_change(struct amvdec_session *sess, u32 width,
		       u32 height, u32 dpb_size);

/**
 * amvdec_abort() - Abort the current decoding session
 *
 * @sess: current session
 */
void amvdec_abort(struct amvdec_session *sess);
#endif
