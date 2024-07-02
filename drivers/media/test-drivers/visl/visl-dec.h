/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Contains the virtual decoder logic. The functions here control the
 * tracing/TPG on a per-frame basis
 */

#ifndef _VISL_DEC_H_
#define _VISL_DEC_H_

#include "visl.h"

struct visl_fwht_run {
	const struct v4l2_ctrl_fwht_params *params;
};

struct visl_mpeg2_run {
	const struct v4l2_ctrl_mpeg2_sequence *seq;
	const struct v4l2_ctrl_mpeg2_picture *pic;
	const struct v4l2_ctrl_mpeg2_quantisation *quant;
};

struct visl_vp8_run {
	const struct v4l2_ctrl_vp8_frame *frame;
};

struct visl_vp9_run {
	const struct v4l2_ctrl_vp9_frame *frame;
	const struct v4l2_ctrl_vp9_compressed_hdr *probs;
};

struct visl_h264_run {
	const struct v4l2_ctrl_h264_sps *sps;
	const struct v4l2_ctrl_h264_pps *pps;
	const struct v4l2_ctrl_h264_scaling_matrix *sm;
	const struct v4l2_ctrl_h264_slice_params *spram;
	const struct v4l2_ctrl_h264_decode_params *dpram;
	const struct v4l2_ctrl_h264_pred_weights *pwht;
};

struct visl_hevc_run {
	const struct v4l2_ctrl_hevc_sps *sps;
	const struct v4l2_ctrl_hevc_pps *pps;
	const struct v4l2_ctrl_hevc_slice_params *spram;
	const struct v4l2_ctrl_hevc_scaling_matrix *sm;
	const struct v4l2_ctrl_hevc_decode_params *dpram;
};

struct visl_av1_run {
	const struct v4l2_ctrl_av1_sequence *seq;
	const struct v4l2_ctrl_av1_frame *frame;
	const struct v4l2_ctrl_av1_tile_group_entry *tge;
	const struct v4l2_ctrl_av1_film_grain *grain;
};

struct visl_run {
	struct vb2_v4l2_buffer	*src;
	struct vb2_v4l2_buffer	*dst;

	union {
		struct visl_fwht_run	fwht;
		struct visl_mpeg2_run	mpeg2;
		struct visl_vp8_run	vp8;
		struct visl_vp9_run	vp9;
		struct visl_h264_run	h264;
		struct visl_hevc_run	hevc;
		struct visl_av1_run	av1;
	};
};

int visl_dec_start(struct visl_ctx *ctx);
int visl_dec_stop(struct visl_ctx *ctx);
int visl_job_ready(void *priv);
void visl_device_run(void *priv);

#endif /* _VISL_DEC_H_ */
