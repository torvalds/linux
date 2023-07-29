/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#ifndef _MTK_VCODEC_DEC_DRV_H_
#define _MTK_VCODEC_DEC_DRV_H_

#include "mtk_vcodec_cmn_drv.h"
#include "mtk_vcodec_fw_priv.h"
#include "vdec_msg_queue.h"

/**
 * struct vdec_pic_info  - picture size information
 * @pic_w: picture width
 * @pic_h: picture height
 * @buf_w: picture buffer width (64 aligned up from pic_w)
 * @buf_h: picture buffer heiht (64 aligned up from pic_h)
 * @fb_sz: bitstream size of each plane
 * E.g. suppose picture size is 176x144,
 *      buffer size will be aligned to 176x160.
 * @cap_fourcc: fourcc number(may changed when resolution change)
 * @reserved: align struct to 64-bit in order to adjust 32-bit and 64-bit os.
 */
struct vdec_pic_info {
	unsigned int pic_w;
	unsigned int pic_h;
	unsigned int buf_w;
	unsigned int buf_h;
	unsigned int fb_sz[VIDEO_MAX_PLANES];
	unsigned int cap_fourcc;
	unsigned int reserved;
};

/*
 * enum mtk_vdec_hw_id - Hardware index used to separate
 *                         different hardware
 */
enum mtk_vdec_hw_id {
	MTK_VDEC_CORE,
	MTK_VDEC_LAT0,
	MTK_VDEC_LAT1,
	MTK_VDEC_LAT_SOC,
	MTK_VDEC_HW_MAX,
};

/**
 * struct mtk_vcodec_dec_ctx - Context (instance) private data.
 *
 * @type: type of decoder instance
 * @dev: pointer to the mtk_vcodec_dev of the device
 * @list: link to ctx_list of mtk_vcodec_dev
 *
 * @fh: struct v4l2_fh
 * @m2m_ctx: pointer to the v4l2_m2m_ctx of the context
 * @q_data: store information of input and output queue of the context
 * @id: index of the context that this structure describes
 * @state: state of the context
 *
 * @dec_if: hooked decoder driver interface
 * @drv_handle: driver handle for specific decode/encode instance
 *
 * @picinfo: store picture info after header parsing
 * @dpb_size: store dpb count after header parsing
 *
 * @int_cond: variable used by the waitqueue
 * @int_type: type of the last interrupt
 * @queue: waitqueue that can be used to wait for this context to finish
 * @irq_status: irq status
 *
 * @ctrl_hdl: handler for v4l2 framework
 * @decode_work: worker for the decoding
 * @last_decoded_picinfo: pic information get from latest decode
 * @empty_flush_buf: a fake size-0 capture buffer that indicates flush. Used
 *		     for stateful decoder.
 * @is_flushing: set to true if flushing is in progress.
 *
 * @current_codec: current set input codec, in V4L2 pixel format
 * @capture_fourcc: capture queue type in V4L2 pixel format
 *
 * @colorspace: enum v4l2_colorspace; supplemental to pixelformat
 * @ycbcr_enc: enum v4l2_ycbcr_encoding, Y'CbCr encoding
 * @quantization: enum v4l2_quantization, colorspace quantization
 * @xfer_func: enum v4l2_xfer_func, colorspace transfer function
 *
 * @decoded_frame_cnt: number of decoded frames
 * @lock: protect variables accessed by V4L2 threads and worker thread such as
 *	  mtk_video_dec_buf.
 * @hw_id: hardware index used to identify different hardware.
 *
 * @msg_queue: msg queue used to store lat buffer information.
 */
struct mtk_vcodec_dec_ctx {
	enum mtk_instance_type type;
	struct mtk_vcodec_dev *dev;
	struct list_head list;

	struct v4l2_fh fh;
	struct v4l2_m2m_ctx *m2m_ctx;
	struct mtk_q_data q_data[2];
	int id;
	enum mtk_instance_state state;

	const struct vdec_common_if *dec_if;
	void *drv_handle;

	struct vdec_pic_info picinfo;
	int dpb_size;

	int int_cond[MTK_VDEC_HW_MAX];
	int int_type[MTK_VDEC_HW_MAX];
	wait_queue_head_t queue[MTK_VDEC_HW_MAX];
	unsigned int irq_status;

	struct v4l2_ctrl_handler ctrl_hdl;
	struct work_struct decode_work;
	struct vdec_pic_info last_decoded_picinfo;
	struct v4l2_m2m_buffer empty_flush_buf;
	bool is_flushing;

	u32 current_codec;
	u32 capture_fourcc;

	enum v4l2_colorspace colorspace;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_quantization quantization;
	enum v4l2_xfer_func xfer_func;

	int decoded_frame_cnt;
	struct mutex lock;
	int hw_id;

	struct vdec_msg_queue msg_queue;
};

static inline struct mtk_vcodec_dec_ctx *fh_to_dec_ctx(struct v4l2_fh *fh)
{
	return container_of(fh, struct mtk_vcodec_dec_ctx, fh);
}

static inline struct mtk_vcodec_dec_ctx *ctrl_to_dec_ctx(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct mtk_vcodec_dec_ctx, ctrl_hdl);
}

/* Wake up context wait_queue */
static inline void
wake_up_dec_ctx(struct mtk_vcodec_dec_ctx *ctx, unsigned int reason, unsigned int hw_id)
{
	ctx->int_cond[hw_id] = 1;
	ctx->int_type[hw_id] = reason;
	wake_up_interruptible(&ctx->queue[hw_id]);
}

#endif /* _MTK_VCODEC_DEC_DRV_H_ */
