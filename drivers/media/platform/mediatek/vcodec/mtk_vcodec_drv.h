/* SPDX-License-Identifier: GPL-2.0 */
/*
* Copyright (c) 2016 MediaTek Inc.
* Author: PC Chen <pc.chen@mediatek.com>
*         Tiffany Lin <tiffany.lin@mediatek.com>
*/

#ifndef _MTK_VCODEC_DRV_H_
#define _MTK_VCODEC_DRV_H_

#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>

#include "mtk_vcodec_util.h"
#include "vdec_msg_queue.h"

#define MTK_VCODEC_DRV_NAME	"mtk_vcodec_drv"
#define MTK_VCODEC_DEC_NAME	"mtk-vcodec-dec"
#define MTK_VCODEC_ENC_NAME	"mtk-vcodec-enc"
#define MTK_PLATFORM_STR	"platform:mt8173"

#define MTK_VCODEC_MAX_PLANES	3
#define MTK_V4L2_BENCHMARK	0
#define WAIT_INTR_TIMEOUT_MS	1000
#define IS_VDEC_LAT_ARCH(hw_arch) ((hw_arch) >= MTK_VDEC_LAT_SINGLE_CORE)

/*
 * enum mtk_hw_reg_idx - MTK hw register base index
 */
enum mtk_hw_reg_idx {
	VDEC_SYS,
	VDEC_MISC,
	VDEC_LD,
	VDEC_TOP,
	VDEC_CM,
	VDEC_AD,
	VDEC_AV,
	VDEC_PP,
	VDEC_HWD,
	VDEC_HWQ,
	VDEC_HWB,
	VDEC_HWG,
	NUM_MAX_VDEC_REG_BASE,
	/* h264 encoder */
	VENC_SYS = NUM_MAX_VDEC_REG_BASE,
	/* vp8 encoder */
	VENC_LT_SYS,
	NUM_MAX_VCODEC_REG_BASE
};

/*
 * enum mtk_instance_type - The type of an MTK Vcodec instance.
 */
enum mtk_instance_type {
	MTK_INST_DECODER		= 0,
	MTK_INST_ENCODER		= 1,
};

/**
 * enum mtk_instance_state - The state of an MTK Vcodec instance.
 * @MTK_STATE_FREE: default state when instance is created
 * @MTK_STATE_INIT: vcodec instance is initialized
 * @MTK_STATE_HEADER: vdec had sps/pps header parsed or venc
 *			had sps/pps header encoded
 * @MTK_STATE_FLUSH: vdec is flushing. Only used by decoder
 * @MTK_STATE_ABORT: vcodec should be aborted
 */
enum mtk_instance_state {
	MTK_STATE_FREE = 0,
	MTK_STATE_INIT = 1,
	MTK_STATE_HEADER = 2,
	MTK_STATE_FLUSH = 3,
	MTK_STATE_ABORT = 4,
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

enum mtk_fmt_type {
	MTK_FMT_DEC = 0,
	MTK_FMT_ENC = 1,
	MTK_FMT_FRAME = 2,
};

/*
 * enum mtk_vdec_hw_id - Hardware index used to separate
 *                         different hardware
 */
enum mtk_vdec_hw_id {
	MTK_VDEC_CORE,
	MTK_VDEC_LAT0,
	MTK_VDEC_LAT1,
	MTK_VDEC_HW_MAX,
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
 * struct mtk_video_fmt - Structure used to store information about pixelformats
 */
struct mtk_video_fmt {
	u32	fourcc;
	enum mtk_fmt_type	type;
	u32	num_planes;
	u32	flags;
};

/*
 * struct mtk_codec_framesizes - Structure used to store information about
 *							framesizes
 */
struct mtk_codec_framesizes {
	u32	fourcc;
	struct	v4l2_frmsize_stepwise	stepwise;
};

/*
 * enum mtk_q_type - Type of queue
 */
enum mtk_q_type {
	MTK_Q_DATA_SRC = 0,
	MTK_Q_DATA_DST = 1,
};

/*
 * struct mtk_q_data - Structure used to store information about queue
 */
struct mtk_q_data {
	unsigned int	visible_width;
	unsigned int	visible_height;
	unsigned int	coded_width;
	unsigned int	coded_height;
	enum v4l2_field	field;
	unsigned int	bytesperline[MTK_VCODEC_MAX_PLANES];
	unsigned int	sizeimage[MTK_VCODEC_MAX_PLANES];
	const struct mtk_video_fmt	*fmt;
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

/*
 * struct mtk_vcodec_clk_info - Structure used to store clock name
 */
struct mtk_vcodec_clk_info {
	const char	*clk_name;
	struct clk	*vcodec_clk;
};

/*
 * struct mtk_vcodec_clk - Structure used to store vcodec clock information
 */
struct mtk_vcodec_clk {
	struct mtk_vcodec_clk_info	*clk_info;
	int	clk_num;
};

/*
 * struct mtk_vcodec_pm - Power management data structure
 */
struct mtk_vcodec_pm {
	struct mtk_vcodec_clk	vdec_clk;
	struct mtk_vcodec_clk	venc_clk;
	struct device	*dev;
};

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

/**
 * struct mtk_vcodec_ctx - Context (instance) private data.
 *
 * @type: type of the instance - decoder or encoder
 * @dev: pointer to the mtk_vcodec_dev of the device
 * @list: link to ctx_list of mtk_vcodec_dev
 * @fh: struct v4l2_fh
 * @m2m_ctx: pointer to the v4l2_m2m_ctx of the context
 * @q_data: store information of input and output queue
 *	    of the context
 * @id: index of the context that this structure describes
 * @state: state of the context
 * @param_change: indicate encode parameter type
 * @enc_params: encoding parameters
 * @dec_if: hooked decoder driver interface
 * @enc_if: hoooked encoder driver interface
 * @drv_handle: driver handle for specific decode/encode instance
 *
 * @picinfo: store picture info after header parsing
 * @dpb_size: store dpb count after header parsing
 * @int_cond: variable used by the waitqueue
 * @int_type: type of the last interrupt
 * @queue: waitqueue that can be used to wait for this context to
 *	   finish
 * @irq_status: irq status
 *
 * @ctrl_hdl: handler for v4l2 framework
 * @decode_work: worker for the decoding
 * @encode_work: worker for the encoding
 * @last_decoded_picinfo: pic information get from latest decode
 * @empty_flush_buf: a fake size-0 capture buffer that indicates flush. Only
 *		     to be used with encoder and stateful decoder.
 * @is_flushing: set to true if flushing is in progress.
 * @current_codec: current set input codec, in V4L2 pixel format
 *
 * @colorspace: enum v4l2_colorspace; supplemental to pixelformat
 * @ycbcr_enc: enum v4l2_ycbcr_encoding, Y'CbCr encoding
 * @quantization: enum v4l2_quantization, colorspace quantization
 * @xfer_func: enum v4l2_xfer_func, colorspace transfer function
 * @decoded_frame_cnt: number of decoded frames
 * @lock: protect variables accessed by V4L2 threads and worker thread such as
 *	  mtk_video_dec_buf.
 * @hw_id: hardware index used to identify different hardware.
 *
 * @msg_queue: msg queue used to store lat buffer information.
 */
struct mtk_vcodec_ctx {
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

	const struct vdec_common_if *dec_if;
	const struct venc_common_if *enc_if;
	void *drv_handle;

	struct vdec_pic_info picinfo;
	int dpb_size;

	int int_cond[MTK_VDEC_HW_MAX];
	int int_type[MTK_VDEC_HW_MAX];
	wait_queue_head_t queue[MTK_VDEC_HW_MAX];
	unsigned int irq_status;

	struct v4l2_ctrl_handler ctrl_hdl;
	struct work_struct decode_work;
	struct work_struct encode_work;
	struct vdec_pic_info last_decoded_picinfo;
	struct v4l2_m2m_buffer empty_flush_buf;
	bool is_flushing;

	u32 current_codec;

	enum v4l2_colorspace colorspace;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_quantization quantization;
	enum v4l2_xfer_func xfer_func;

	int decoded_frame_cnt;
	struct mutex lock;
	int hw_id;

	struct vdec_msg_queue msg_queue;
};

enum mtk_chip {
	MTK_MT8173,
	MTK_MT8183,
	MTK_MT8192,
	MTK_MT8195,
};

/*
 * enum mtk_vdec_hw_arch - Used to separate different hardware architecture
 */
enum mtk_vdec_hw_arch {
	MTK_VDEC_PURE_SINGLE_CORE,
	MTK_VDEC_LAT_SINGLE_CORE,
};

/**
 * struct mtk_vcodec_dec_pdata - compatible data for each IC
 * @init_vdec_params: init vdec params
 * @ctrls_setup: init vcodec dec ctrls
 * @worker: worker to start a decode job
 * @flush_decoder: function that flushes the decoder
 *
 * @vdec_vb2_ops: struct vb2_ops
 *
 * @vdec_formats: supported video decoder formats
 * @num_formats: count of video decoder formats
 * @default_out_fmt: default output buffer format
 * @default_cap_fmt: default capture buffer format
 *
 * @vdec_framesizes: supported video decoder frame sizes
 * @num_framesizes: count of video decoder frame sizes
 *
 * @chip: chip this decoder is compatible with
 * @hw_arch: hardware arch is used to separate pure_sin_core and lat_sin_core
 *
 * @is_subdev_supported: whether support parent-node architecture(subdev)
 * @uses_stateless_api: whether the decoder uses the stateless API with requests
 */

struct mtk_vcodec_dec_pdata {
	void (*init_vdec_params)(struct mtk_vcodec_ctx *ctx);
	int (*ctrls_setup)(struct mtk_vcodec_ctx *ctx);
	void (*worker)(struct work_struct *work);
	int (*flush_decoder)(struct mtk_vcodec_ctx *ctx);

	struct vb2_ops *vdec_vb2_ops;

	const struct mtk_video_fmt *vdec_formats;
	const int num_formats;
	const struct mtk_video_fmt *default_out_fmt;
	const struct mtk_video_fmt *default_cap_fmt;

	const struct mtk_codec_framesizes *vdec_framesizes;
	const int num_framesizes;

	enum mtk_chip chip;
	enum mtk_vdec_hw_arch hw_arch;

	bool is_subdev_supported;
	bool uses_stateless_api;
};

/**
 * struct mtk_vcodec_enc_pdata - compatible data for each IC
 *
 * @chip: chip this encoder is compatible with
 *
 * @uses_ext: whether the encoder uses the extended firmware messaging format
 * @min_bitrate: minimum supported encoding bitrate
 * @max_bitrate: maximum supported encoding bitrate
 * @capture_formats: array of supported capture formats
 * @num_capture_formats: number of entries in capture_formats
 * @output_formats: array of supported output formats
 * @num_output_formats: number of entries in output_formats
 * @core_id: stand for h264 or vp8 encode index
 */
struct mtk_vcodec_enc_pdata {
	enum mtk_chip chip;

	bool uses_ext;
	unsigned long min_bitrate;
	unsigned long max_bitrate;
	const struct mtk_video_fmt *capture_formats;
	size_t num_capture_formats;
	const struct mtk_video_fmt *output_formats;
	size_t num_output_formats;
	int core_id;
};

#define MTK_ENC_CTX_IS_EXT(ctx) ((ctx)->dev->venc_pdata->uses_ext)

/**
 * struct mtk_vcodec_dev - driver data
 * @v4l2_dev: V4L2 device to register video devices for.
 * @vfd_dec: Video device for decoder
 * @mdev_dec: Media device for decoder
 * @vfd_enc: Video device for encoder.
 *
 * @m2m_dev_dec: m2m device for decoder
 * @m2m_dev_enc: m2m device for encoder.
 * @plat_dev: platform device
 * @ctx_list: list of struct mtk_vcodec_ctx
 * @irqlock: protect data access by irq handler and work thread
 * @curr_ctx: The context that is waiting for codec hardware
 *
 * @reg_base: Mapped address of MTK Vcodec registers.
 * @vdec_pdata: decoder IC-specific data
 * @venc_pdata: encoder IC-specific data
 *
 * @fw_handler: used to communicate with the firmware.
 * @id_counter: used to identify current opened instance
 *
 * @decode_workqueue: decode work queue
 * @encode_workqueue: encode work queue
 *
 * @int_cond: used to identify interrupt condition happen
 * @int_type: used to identify what kind of interrupt condition happen
 * @dev_mutex: video_device lock
 * @queue: waitqueue for waiting for completion of device commands
 *
 * @dec_irq: decoder irq resource
 * @enc_irq: h264 encoder irq resource
 *
 * @dec_mutex: decoder hardware lock
 * @enc_mutex: encoder hardware lock.
 *
 * @pm: power management control
 * @dec_capability: used to identify decode capability, ex: 4k
 * @enc_capability: used to identify encode capability
 *
 * @core_workqueue: queue used for core hardware decode
 * @msg_queue_core_ctx: msg queue context used for core workqueue
 *
 * @subdev_dev: subdev hardware device
 * @subdev_prob_done: check whether all used hw device is prob done
 * @subdev_bitmap: used to record hardware is ready or not
 */
struct mtk_vcodec_dev {
	struct v4l2_device v4l2_dev;
	struct video_device *vfd_dec;
	struct media_device mdev_dec;
	struct video_device *vfd_enc;

	struct v4l2_m2m_dev *m2m_dev_dec;
	struct v4l2_m2m_dev *m2m_dev_enc;
	struct platform_device *plat_dev;
	struct list_head ctx_list;
	spinlock_t irqlock;
	struct mtk_vcodec_ctx *curr_ctx;
	void __iomem *reg_base[NUM_MAX_VCODEC_REG_BASE];
	const struct mtk_vcodec_dec_pdata *vdec_pdata;
	const struct mtk_vcodec_enc_pdata *venc_pdata;

	struct mtk_vcodec_fw *fw_handler;

	unsigned long id_counter;

	struct workqueue_struct *decode_workqueue;
	struct workqueue_struct *encode_workqueue;
	int int_cond;
	int int_type;
	struct mutex dev_mutex;
	wait_queue_head_t queue;

	int dec_irq;
	int enc_irq;

	/* decoder hardware mutex lock */
	struct mutex dec_mutex[MTK_VDEC_HW_MAX];
	struct mutex enc_mutex;

	struct mtk_vcodec_pm pm;
	unsigned int dec_capability;
	unsigned int enc_capability;

	struct workqueue_struct *core_workqueue;
	struct vdec_msg_queue_ctx msg_queue_core_ctx;

	void *subdev_dev[MTK_VDEC_HW_MAX];
	int (*subdev_prob_done)(struct mtk_vcodec_dev *vdec_dev);
	DECLARE_BITMAP(subdev_bitmap, MTK_VDEC_HW_MAX);
};

static inline struct mtk_vcodec_ctx *fh_to_ctx(struct v4l2_fh *fh)
{
	return container_of(fh, struct mtk_vcodec_ctx, fh);
}

static inline struct mtk_vcodec_ctx *ctrl_to_ctx(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct mtk_vcodec_ctx, ctrl_hdl);
}

/* Wake up context wait_queue */
static inline void
wake_up_ctx(struct mtk_vcodec_ctx *ctx, unsigned int reason, unsigned int hw_id)
{
	ctx->int_cond[hw_id] = 1;
	ctx->int_type[hw_id] = reason;
	wake_up_interruptible(&ctx->queue[hw_id]);
}

#endif /* _MTK_VCODEC_DRV_H_ */
