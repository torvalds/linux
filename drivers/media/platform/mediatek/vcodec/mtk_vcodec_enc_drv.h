/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#ifndef _MTK_VCODEC_ENC_DRV_H_
#define _MTK_VCODEC_ENC_DRV_H_

#include "mtk_vcodec_cmn_drv.h"
#include "mtk_vcodec_fw_priv.h"

/*
 * enum mtk_encode_param - General encoding parameters type
 */
enum mtk_encode_param {
	MTK_ENCODE_PARAM_NONE = 0,
	MTK_ENCODE_PARAM_BITRATE = (1 << 0),
	MTK_ENCODE_PARAM_FRAMERATE = (1 << 1),
	MTK_ENCODE_PARAM_INTRA_PERIOD = (1 << 2),
	MTK_ENCODE_PARAM_FORCE_INTRA = (1 << 3),
	MTK_ENCODE_PARAM_GOP_SIZE = (1 << 4),
};

/**
 * struct mtk_enc_params - General encoding parameters
 * @bitrate: target bitrate in bits per second
 * @num_b_frame: number of b frames between p-frame
 * @rc_frame: frame based rate control
 * @rc_mb: macroblock based rate control
 * @seq_hdr_mode: H.264 sequence header is encoded separately or joined
 *		  with the first frame
 * @intra_period: I frame period
 * @gop_size: group of picture size, it's used as the intra frame period
 * @framerate_num: frame rate numerator. ex: framerate_num=30 and
 *		   framerate_denom=1 means FPS is 30
 * @framerate_denom: frame rate denominator. ex: framerate_num=30 and
 *		     framerate_denom=1 means FPS is 30
 * @h264_max_qp: Max value for H.264 quantization parameter
 * @h264_profile: V4L2 defined H.264 profile
 * @h264_level: V4L2 defined H.264 level
 * @force_intra: force/insert intra frame
 */
struct mtk_enc_params {
	unsigned int	bitrate;
	unsigned int	num_b_frame;
	unsigned int	rc_frame;
	unsigned int	rc_mb;
	unsigned int	seq_hdr_mode;
	unsigned int	intra_period;
	unsigned int	gop_size;
	unsigned int	framerate_num;
	unsigned int	framerate_denom;
	unsigned int	h264_max_qp;
	unsigned int	h264_profile;
	unsigned int	h264_level;
	unsigned int	force_intra;
};

/**
 * struct mtk_vcodec_enc_ctx - Context (instance) private data.
 *
 * @type: type of encoder instance
 * @dev: pointer to the mtk_vcodec_dev of the device
 * @list: link to ctx_list of mtk_vcodec_dev
 *
 * @fh: struct v4l2_fh
 * @m2m_ctx: pointer to the v4l2_m2m_ctx of the context
 * @q_data: store information of input and output queue of the context
 * @id: index of the context that this structure describes
 * @state: state of the context
 * @param_change: indicate encode parameter type
 * @enc_params: encoding parameters
 *
 * @enc_if: hooked encoder driver interface
 * @drv_handle: driver handle for specific decode/encode instance
 *
 * @int_cond: variable used by the waitqueue
 * @int_type: type of the last interrupt
 * @queue: waitqueue that can be used to wait for this context to finish
 * @irq_status: irq status
 *
 * @ctrl_hdl: handler for v4l2 framework
 * @encode_work: worker for the encoding
 * @empty_flush_buf: a fake size-0 capture buffer that indicates flush. Used for encoder.
 * @is_flushing: set to true if flushing is in progress.
 *
 * @colorspace: enum v4l2_colorspace; supplemental to pixelformat
 * @ycbcr_enc: enum v4l2_ycbcr_encoding, Y'CbCr encoding
 * @quantization: enum v4l2_quantization, colorspace quantization
 * @xfer_func: enum v4l2_xfer_func, colorspace transfer function
 *
 * @q_mutex: vb2_queue mutex.
 */
struct mtk_vcodec_enc_ctx {
	enum mtk_instance_type type;
	struct mtk_vcodec_dev *dev;
	struct list_head list;

	struct v4l2_fh fh;
	struct v4l2_m2m_ctx *m2m_ctx;
	struct mtk_q_data q_data[2];
	int id;
	enum mtk_instance_state state;
	enum mtk_encode_param param_change;
	struct mtk_enc_params enc_params;

	const struct venc_common_if *enc_if;
	void *drv_handle;

	int int_cond[MTK_VDEC_HW_MAX];
	int int_type[MTK_VDEC_HW_MAX];
	wait_queue_head_t queue[MTK_VDEC_HW_MAX];
	unsigned int irq_status;

	struct v4l2_ctrl_handler ctrl_hdl;
	struct work_struct encode_work;
	struct v4l2_m2m_buffer empty_flush_buf;
	bool is_flushing;

	enum v4l2_colorspace colorspace;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_quantization quantization;
	enum v4l2_xfer_func xfer_func;

	struct mutex q_mutex;
};

static inline struct mtk_vcodec_enc_ctx *fh_to_enc_ctx(struct v4l2_fh *fh)
{
	return container_of(fh, struct mtk_vcodec_enc_ctx, fh);
}

static inline struct mtk_vcodec_enc_ctx *ctrl_to_enc_ctx(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct mtk_vcodec_enc_ctx, ctrl_hdl);
}

/* Wake up context wait_queue */
static inline void
wake_up_enc_ctx(struct mtk_vcodec_enc_ctx *ctx, unsigned int reason, unsigned int hw_id)
{
	ctx->int_cond[hw_id] = 1;
	ctx->int_type[hw_id] = reason;
	wake_up_interruptible(&ctx->queue[hw_id]);
}

#endif /* _MTK_VCODEC_ENC_DRV_H_ */
