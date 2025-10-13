/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#ifndef _MTK_VCODEC_ENC_DRV_H_
#define _MTK_VCODEC_ENC_DRV_H_

#include "../common/mtk_vcodec_cmn_drv.h"
#include "../common/mtk_vcodec_dbgfs.h"
#include "../common/mtk_vcodec_fw_priv.h"
#include "../common/mtk_vcodec_util.h"

#define MTK_VCODEC_ENC_NAME	"mtk-vcodec-enc"

#define MTK_ENC_CTX_IS_EXT(ctx) ((ctx)->dev->venc_pdata->uses_ext)
#define MTK_ENC_IOVA_IS_34BIT(ctx) ((ctx)->dev->venc_pdata->uses_34bit)

/**
 * struct mtk_vcodec_enc_pdata - compatible data for each IC
 *
 * @uses_ext: whether the encoder uses the extended firmware messaging format
 * @min_bitrate: minimum supported encoding bitrate
 * @max_bitrate: maximum supported encoding bitrate
 * @capture_formats: array of supported capture formats
 * @num_capture_formats: number of entries in capture_formats
 * @output_formats: array of supported output formats
 * @num_output_formats: number of entries in output_formats
 * @core_id: stand for h264 or vp8 encode index
 * @uses_34bit: whether the encoder uses 34-bit iova
 */
struct mtk_vcodec_enc_pdata {
	bool uses_ext;
	u64 min_bitrate;
	u64 max_bitrate;
	const struct mtk_video_fmt *capture_formats;
	size_t num_capture_formats;
	const struct mtk_video_fmt *output_formats;
	size_t num_output_formats;
	u8 core_id;
	bool uses_34bit;
};

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
 * @dev: pointer to the mtk_vcodec_enc_dev of the device
 * @list: link to ctx_list of mtk_vcodec_enc_dev
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
 * @vpu_inst: vpu instance pointer.
 */
struct mtk_vcodec_enc_ctx {
	enum mtk_instance_type type;
	struct mtk_vcodec_enc_dev *dev;
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
	void *vpu_inst;
};

/**
 * struct mtk_vcodec_enc_dev - driver data
 * @v4l2_dev: V4L2 device to register video devices for.
 * @vfd_enc: Video device for encoder.
 *
 * @m2m_dev_enc: m2m device for encoder.
 * @plat_dev: platform device
 * @ctx_list: list of struct mtk_vcodec_ctx
 * @curr_ctx: The context that is waiting for codec hardware
 *
 * @reg_base: Mapped address of MTK Vcodec registers.
 * @venc_pdata: encoder IC-specific data
 *
 * @fw_handler: used to communicate with the firmware.
 * @id_counter: used to identify current opened instance
 *
 * @enc_mutex: encoder hardware lock.
 * @dev_mutex: video_device lock
 * @dev_ctx_lock: the lock of context list
 * @encode_workqueue: encode work queue
 *
 * @enc_irq: h264 encoder irq resource
 * @irqlock: protect data access by irq handler and work thread
 *
 * @pm: power management control
 * @enc_capability: used to identify encode capability
 * @dbgfs: debug log related information
 */
struct mtk_vcodec_enc_dev {
	struct v4l2_device v4l2_dev;
	struct video_device *vfd_enc;

	struct v4l2_m2m_dev *m2m_dev_enc;
	struct platform_device *plat_dev;
	struct list_head ctx_list;
	struct mtk_vcodec_enc_ctx *curr_ctx;

	void __iomem *reg_base[NUM_MAX_VCODEC_REG_BASE];
	const struct mtk_vcodec_enc_pdata *venc_pdata;

	struct mtk_vcodec_fw *fw_handler;
	u64 id_counter;

	/* encoder hardware mutex lock */
	struct mutex enc_mutex;
	struct mutex dev_mutex;
	struct mutex dev_ctx_lock;
	struct workqueue_struct *encode_workqueue;

	int enc_irq;
	spinlock_t irqlock;

	struct mtk_vcodec_pm pm;
	unsigned int enc_capability;
	struct mtk_vcodec_dbgfs dbgfs;
};

static inline struct mtk_vcodec_enc_ctx *file_to_enc_ctx(struct file *filp)
{
	return container_of(file_to_v4l2_fh(filp), struct mtk_vcodec_enc_ctx, fh);
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

#define mtk_venc_err(ctx, fmt, args...)                               \
	mtk_vcodec_err((ctx)->id, (ctx)->dev->plat_dev, fmt, ##args)

#define mtk_venc_debug(ctx, fmt, args...)                              \
	mtk_vcodec_debug((ctx)->id, (ctx)->dev->plat_dev, fmt, ##args)

#define mtk_v4l2_venc_err(ctx, fmt, args...) mtk_v4l2_err((ctx)->dev->plat_dev, fmt, ##args)

#define mtk_v4l2_venc_dbg(level, ctx, fmt, args...)             \
	mtk_v4l2_debug((ctx)->dev->plat_dev, level, fmt, ##args)

#endif /* _MTK_VCODEC_ENC_DRV_H_ */
