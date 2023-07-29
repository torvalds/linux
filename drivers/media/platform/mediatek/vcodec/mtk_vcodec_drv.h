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

#include "mtk_vcodec_dbgfs.h"
#include "mtk_vcodec_dec_drv.h"
#include "mtk_vcodec_enc_drv.h"
#include "mtk_vcodec_util.h"
#include "vdec_msg_queue.h"

#define MTK_VCODEC_DEC_NAME	"mtk-vcodec-dec"
#define MTK_VCODEC_ENC_NAME	"mtk-vcodec-enc"

#define MTK_V4L2_BENCHMARK	0
#define WAIT_INTR_TIMEOUT_MS	1000
#define IS_VDEC_LAT_ARCH(hw_arch) ((hw_arch) >= MTK_VDEC_LAT_SINGLE_CORE)
#define IS_VDEC_INNER_RACING(capability) ((capability) & MTK_VCODEC_INNER_RACING)

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
 * enum mtk_vdec_hw_count - Supported hardware count
 */
enum mtk_vdec_hw_count {
	MTK_VDEC_NO_HW = 0,
	MTK_VDEC_ONE_CORE,
	MTK_VDEC_ONE_LAT_ONE_CORE,
	MTK_VDEC_MAX_HW_COUNT,
};

/*
 * enum mtk_q_type - Type of queue
 */
enum mtk_q_type {
	MTK_Q_DATA_SRC = 0,
	MTK_Q_DATA_DST = 1,
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

/*
 * enum mtk_vdec_hw_arch - Used to separate different hardware architecture
 */
enum mtk_vdec_hw_arch {
	MTK_VDEC_PURE_SINGLE_CORE,
	MTK_VDEC_LAT_SINGLE_CORE,
};

/*
 * struct mtk_vdec_format_types - Structure used to get supported
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

	struct vb2_ops *vdec_vb2_ops;

	const struct mtk_video_fmt *vdec_formats;
	const int *num_formats;
	const struct mtk_video_fmt *default_out_fmt;
	const struct mtk_video_fmt *default_cap_fmt;

	enum mtk_vdec_hw_arch hw_arch;

	bool is_subdev_supported;
	bool uses_stateless_api;
};

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
	unsigned long min_bitrate;
	unsigned long max_bitrate;
	const struct mtk_video_fmt *capture_formats;
	size_t num_capture_formats;
	const struct mtk_video_fmt *output_formats;
	size_t num_output_formats;
	int core_id;
	bool uses_34bit;
};

#define MTK_ENC_CTX_IS_EXT(ctx) ((ctx)->dev->venc_pdata->uses_ext)
#define MTK_ENC_IOVA_IS_34BIT(ctx) ((ctx)->dev->venc_pdata->uses_34bit)

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
 * @ctx_list: list of struct mtk_vcodec_dec_ctx
 * @irqlock: protect data access by irq handler and work thread
 * @curr_ctx: The context that is waiting for codec hardware
 * @curr_enc_ctx: The encoder context that is waiting for codec hardware
 *
 * @reg_base: Mapped address of MTK Vcodec registers.
 * @vdec_pdata: decoder IC-specific data
 * @venc_pdata: encoder IC-specific data
 * @vdecsys_regmap: VDEC_SYS register space passed through syscon
 *
 * @fw_handler: used to communicate with the firmware.
 * @id_counter: used to identify current opened instance
 *
 * @decode_workqueue: decode work queue
 * @encode_workqueue: encode work queue
 *
 * @dev_mutex: video_device lock
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
 *
 * @subdev_dev: subdev hardware device
 * @subdev_prob_done: check whether all used hw device is prob done
 * @subdev_bitmap: used to record hardware is ready or not
 *
 * @dec_active_cnt: used to mark whether need to record register value
 * @vdec_racing_info: record register value
 * @dec_racing_info_mutex: mutex lock used for inner racing mode
 * @dbgfs: debug log related information
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
	struct mtk_vcodec_dec_ctx *curr_ctx;
	struct mtk_vcodec_enc_ctx *curr_enc_ctx;
	void __iomem *reg_base[NUM_MAX_VCODEC_REG_BASE];
	const struct mtk_vcodec_dec_pdata *vdec_pdata;
	const struct mtk_vcodec_enc_pdata *venc_pdata;
	struct regmap *vdecsys_regmap;

	struct mtk_vcodec_fw *fw_handler;

	unsigned long id_counter;

	struct workqueue_struct *decode_workqueue;
	struct workqueue_struct *encode_workqueue;
	struct mutex dev_mutex;

	int dec_irq;
	int enc_irq;

	/* decoder hardware mutex lock */
	struct mutex dec_mutex[MTK_VDEC_HW_MAX];
	struct mutex enc_mutex;

	struct mtk_vcodec_pm pm;
	unsigned int dec_capability;
	unsigned int enc_capability;

	struct workqueue_struct *core_workqueue;

	void *subdev_dev[MTK_VDEC_HW_MAX];
	int (*subdev_prob_done)(struct mtk_vcodec_dev *vdec_dev);
	DECLARE_BITMAP(subdev_bitmap, MTK_VDEC_HW_MAX);

	atomic_t dec_active_cnt;
	u32 vdec_racing_info[132];
	/* Protects access to vdec_racing_info data */
	struct mutex dec_racing_info_mutex;

	struct mtk_vcodec_dbgfs dbgfs;
};

#endif /* _MTK_VCODEC_DRV_H_ */
