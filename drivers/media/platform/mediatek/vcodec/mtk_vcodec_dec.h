/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: PC Chen <pc.chen@mediatek.com>
 *         Tiffany Lin <tiffany.lin@mediatek.com>
 */

#ifndef _MTK_VCODEC_DEC_H_
#define _MTK_VCODEC_DEC_H_

#include <media/videobuf2-core.h>
#include <media/v4l2-mem2mem.h>

#define VCODEC_DEC_ALIGNED_64 64
#define VCODEC_CAPABILITY_4K_DISABLED	0x10
#define VCODEC_DEC_4K_CODED_WIDTH	4096U
#define VCODEC_DEC_4K_CODED_HEIGHT	2304U
#define MTK_VDEC_MAX_W	2048U
#define MTK_VDEC_MAX_H	1088U
#define MTK_VDEC_MIN_W	64U
#define MTK_VDEC_MIN_H	64U

#define MTK_VDEC_IRQ_STATUS_DEC_SUCCESS        0x10000

/**
 * struct vdec_fb  - decoder frame buffer
 * @base_y	: Y plane memory info
 * @base_c	: C plane memory info
 * @status      : frame buffer status (vdec_fb_status)
 */
struct vdec_fb {
	struct mtk_vcodec_mem	base_y;
	struct mtk_vcodec_mem	base_c;
	unsigned int	status;
};

/**
 * struct mtk_video_dec_buf - Private data related to each VB2 buffer.
 * @m2m_buf:	M2M buffer
 * @list:	link list
 * @used:	Capture buffer contain decoded frame data and keep in
 *			codec data structure
 * @queued_in_vb2:	Capture buffer is queue in vb2
 * @queued_in_v4l2:	Capture buffer is in v4l2 driver, but not in vb2
 *			queue yet
 * @error:		An unrecoverable error occurs on this buffer.
 * @frame_buffer:	Decode status, and buffer information of Capture buffer
 * @bs_buffer:	Output buffer info
 *
 * Note : These status information help us track and debug buffer state
 */
struct mtk_video_dec_buf {
	struct v4l2_m2m_buffer	m2m_buf;

	bool	used;
	bool	queued_in_vb2;
	bool	queued_in_v4l2;
	bool	error;

	union {
		struct vdec_fb	frame_buffer;
		struct mtk_vcodec_mem	bs_buffer;
	};
};

extern const struct v4l2_ioctl_ops mtk_vdec_ioctl_ops;
extern const struct v4l2_m2m_ops mtk_vdec_m2m_ops;
extern const struct media_device_ops mtk_vcodec_media_ops;
extern const struct mtk_vcodec_dec_pdata mtk_vdec_8173_pdata;
extern const struct mtk_vcodec_dec_pdata mtk_vdec_8183_pdata;
extern const struct mtk_vcodec_dec_pdata mtk_lat_sig_core_pdata;
extern const struct mtk_vcodec_dec_pdata mtk_vdec_single_core_pdata;


/*
 * mtk_vdec_lock/mtk_vdec_unlock are for ctx instance to
 * get/release lock before/after access decoder hw.
 * mtk_vdec_lock get decoder hw lock and set curr_ctx
 * to ctx instance that get lock
 */
void mtk_vdec_unlock(struct mtk_vcodec_ctx *ctx);
void mtk_vdec_lock(struct mtk_vcodec_ctx *ctx);
int mtk_vcodec_dec_queue_init(void *priv, struct vb2_queue *src_vq,
			   struct vb2_queue *dst_vq);
void mtk_vcodec_dec_set_default_params(struct mtk_vcodec_ctx *ctx);
void mtk_vcodec_dec_release(struct mtk_vcodec_ctx *ctx);

/*
 * VB2 ops
 */
int vb2ops_vdec_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
			    unsigned int *nplanes, unsigned int sizes[],
			    struct device *alloc_devs[]);
int vb2ops_vdec_buf_prepare(struct vb2_buffer *vb);
void vb2ops_vdec_buf_finish(struct vb2_buffer *vb);
int vb2ops_vdec_buf_init(struct vb2_buffer *vb);
int vb2ops_vdec_start_streaming(struct vb2_queue *q, unsigned int count);
void vb2ops_vdec_stop_streaming(struct vb2_queue *q);


#endif /* _MTK_VCODEC_DEC_H_ */
