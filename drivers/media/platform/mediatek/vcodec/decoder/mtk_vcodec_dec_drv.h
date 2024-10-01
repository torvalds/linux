/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#ifndef _MTK_VCODEC_DEC_DRV_H_
#define _MTK_VCODEC_DEC_DRV_H_

#include "../common/mtk_vcodec_cmn_drv.h"
#include "../common/mtk_vcodec_dbgfs.h"
#include "../common/mtk_vcodec_fw_priv.h"
#include "../common/mtk_vcodec_util.h"
#include "vdec_msg_queue.h"

#define MTK_VCODEC_DEC_NAME	"mtk-vcodec-dec"

#define IS_VDEC_LAT_ARCH(hw_arch) ((hw_arch) >= MTK_VDEC_LAT_SINGLE_CORE)
#define IS_VDEC_INNER_RACING(capability) ((capability) & MTK_VCODEC_INNER_RACING)

enum mtk_vcodec_dec_chip_name {
	MTK_VDEC_INVAL = 0,
	MTK_VDEC_MT8173 = 8173,
	MTK_VDEC_MT8183 = 8183,
	MTK_VDEC_MT8186 = 8186,
	MTK_VDEC_MT8188 = 8188,
	MTK_VDEC_MT8192 = 8192,
	MTK_VDEC_MT8195 = 8195,
};

/*
 * enum mtk_vdec_format_types - Structure used to get supported
 *		  format types according to decoder capability
 */
enum mtk_vdec_format_types {
	MTK_VDEC_FORMAT_MM21 = 0x20,
	MTK_VDEC_FORMAT_MT21C = 0x40,
	MTK_VDEC_FORMAT_H264_SLICE = 0x100,
	MTK_VDEC_FORMAT_VP8_FRAME = 0x200,
	MTK_VDEC_FORMAT_VP9_FRAME = 0x400,
	MTK_VDEC_FORMAT_AV1_FRAME = 0x800,
	MTK_VDEC_FORMAT_HEVC_FRAME = 0x1000,
	MTK_VCODEC_INNER_RACING = 0x20000,
	MTK_VDEC_IS_SUPPORT_10BIT = 0x40000,
};

/*
 * enum mtk_vdec_hw_count - Supported hardware count
 */
enum mtk_vdec_hw_count {
	MTK_VDEC_NO_HW = 0,
	MTK_VDEC_ONE_CORE,
	MTK_VDEC_ONE_LAT_ONE_CORE,
	MTK_VDEC_MAX_HW_COUNT,
};

/*
 * enum mtk_vdec_hw_arch - Used to separate different hardware architecture
 */
enum mtk_vdec_hw_arch {
	MTK_VDEC_PURE_SINGLE_CORE,
	MTK_VDEC_LAT_SINGLE_CORE,
};

/**
 * struct vdec_pic_info  - picture size information
 * @pic_w: picture width
 * @pic_h: picture height
 * @buf_w: picture buffer width (64 aligned up from pic_w)
 * @buf_h: picture buffer height (64 aligned up from pic_h)
 * @fb_sz: bitstream size of each plane
 * E.g. suppose picture size is 176x144,
 *      buffer size will be aligned to 176x160.
 * @cap_fourcc: fourcc number(may change on a resolution change)
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

/**
 * struct mtk_vcodec_dec_pdata - compatible data for each IC
 * @init_vdec_params: init vdec params
 * @ctrls_setup: init vcodec dec ctrls
 * @worker: worker to start a decode job
 * @flush_decoder: function that flushes the decoder
 * @get_cap_buffer: get capture buffer from capture queue
 * @cap_to_disp: put capture buffer to disp list for lat and core arch
 * @vdec_vb2_ops: struct vb2_ops
 *
 * @vdec_formats: supported video decoder formats
 * @num_formats: count of video decoder formats
 * @default_out_fmt: default output buffer format
 * @default_cap_fmt: default capture buffer format
 *
 * @hw_arch: hardware arch is used to separate pure_sin_core and lat_sin_core
 *
 * @is_subdev_supported: whether support parent-node architecture(subdev)
 * @uses_stateless_api: whether the decoder uses the stateless API with requests
 */
struct mtk_vcodec_dec_pdata {
	void (*init_vdec_params)(struct mtk_vcodec_dec_ctx *ctx);
	int (*ctrls_setup)(struct mtk_vcodec_dec_ctx *ctx);
	void (*worker)(struct work_struct *work);
	int (*flush_decoder)(struct mtk_vcodec_dec_ctx *ctx);
	struct vdec_fb *(*get_cap_buffer)(struct mtk_vcodec_dec_ctx *ctx);
	void (*cap_to_disp)(struct mtk_vcodec_dec_ctx *ctx, int error,
			    struct media_request *src_buf_req);

	const struct vb2_ops *vdec_vb2_ops;

	const struct mtk_video_fmt *vdec_formats;
	const int *num_formats;
	const struct mtk_video_fmt *default_out_fmt;
	const struct mtk_video_fmt *default_cap_fmt;

	enum mtk_vdec_hw_arch hw_arch;

	bool is_subdev_supported;
	bool uses_stateless_api;
};

/**
 * struct mtk_vcodec_dec_ctx - Context (instance) private data.
 *
 * @type: type of decoder instance
 * @dev: pointer to the mtk_vcodec_dec_dev of the device
 * @list: link to ctx_list of mtk_vcodec_dec_dev
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
 * @vpu_inst: vpu instance pointer.
 *
 * @is_10bit_bitstream: set to true if it's 10bit bitstream
 */
struct mtk_vcodec_dec_ctx {
	enum mtk_instance_type type;
	struct mtk_vcodec_dec_dev *dev;
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
	void *vpu_inst;

	bool is_10bit_bitstream;
};

/**
 * struct mtk_vcodec_dec_dev - driver data
 * @v4l2_dev: V4L2 device to register video devices for.
 * @vfd_dec: Video device for decoder
 * @mdev_dec: Media device for decoder
 *
 * @m2m_dev_dec: m2m device for decoder
 * @plat_dev: platform device
 * @ctx_list: list of struct mtk_vcodec_ctx
 * @curr_ctx: The context that is waiting for codec hardware
 *
 * @reg_base: Mapped address of MTK Vcodec registers.
 * @vdec_pdata: decoder IC-specific data
 * @vdecsys_regmap: VDEC_SYS register space passed through syscon
 *
 * @fw_handler: used to communicate with the firmware.
 * @id_counter: used to identify current opened instance
 *
 * @dec_mutex: decoder hardware lock
 * @dev_mutex: video_device lock
 * @dev_ctx_lock: the lock of context list
 * @decode_workqueue: decode work queue
 *
 * @irqlock: protect data access by irq handler and work thread
 * @dec_irq: decoder irq resource
 *
 * @pm: power management control
 * @dec_capability: used to identify decode capability, ex: 4k
 *
 * @core_workqueue: queue used for core hardware decode
 *
 * @subdev_dev: subdev hardware device
 * @subdev_prob_done: check whether all used hw device is prob done
 * @subdev_bitmap: used to record hardware is ready or not
 *
 * @dec_active_cnt: used to mark whether need to record register value
 * @vdec_racing_info: record register value
 * @dec_racing_info_mutex: mutex lock used for inner racing mode
 * @dbgfs: debug log related information
 *
 * @chip_name: used to distinguish platforms and select the correct codec configuration values
 */
struct mtk_vcodec_dec_dev {
	struct v4l2_device v4l2_dev;
	struct video_device *vfd_dec;
	struct media_device mdev_dec;

	struct v4l2_m2m_dev *m2m_dev_dec;
	struct platform_device *plat_dev;
	struct list_head ctx_list;
	struct mtk_vcodec_dec_ctx *curr_ctx;

	void __iomem *reg_base[NUM_MAX_VCODEC_REG_BASE];
	const struct mtk_vcodec_dec_pdata *vdec_pdata;
	struct regmap *vdecsys_regmap;

	struct mtk_vcodec_fw *fw_handler;
	u64 id_counter;

	/* decoder hardware mutex lock */
	struct mutex dec_mutex[MTK_VDEC_HW_MAX];
	struct mutex dev_mutex;
	struct mutex dev_ctx_lock;
	struct workqueue_struct *decode_workqueue;

	spinlock_t irqlock;
	int dec_irq;

	struct mtk_vcodec_pm pm;
	unsigned int dec_capability;

	struct workqueue_struct *core_workqueue;

	void *subdev_dev[MTK_VDEC_HW_MAX];
	int (*subdev_prob_done)(struct mtk_vcodec_dec_dev *vdec_dev);
	DECLARE_BITMAP(subdev_bitmap, MTK_VDEC_HW_MAX);

	atomic_t dec_active_cnt;
	u32 vdec_racing_info[132];
	/* Protects access to vdec_racing_info data */
	struct mutex dec_racing_info_mutex;
	struct mtk_vcodec_dbgfs dbgfs;

	enum mtk_vcodec_dec_chip_name chip_name;
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

#define mtk_vdec_err(ctx, fmt, args...)                               \
	mtk_vcodec_err((ctx)->id, (ctx)->dev->plat_dev, fmt, ##args)

#define mtk_vdec_debug(ctx, fmt, args...)                             \
	mtk_vcodec_debug((ctx)->id, (ctx)->dev->plat_dev, fmt, ##args)

#define mtk_v4l2_vdec_err(ctx, fmt, args...) mtk_v4l2_err((ctx)->dev->plat_dev, fmt, ##args)

#define mtk_v4l2_vdec_dbg(level, ctx, fmt, args...)             \
	mtk_v4l2_debug((ctx)->dev->plat_dev, level, fmt, ##args)

#endif /* _MTK_VCODEC_DEC_DRV_H_ */
