/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Ming Hsiu Tsai <minghsiu.tsai@mediatek.com>
 *         Rick Chang <rick.chang@mediatek.com>
 *         Xia Jiang <xia.jiang@mediatek.com>
 */

#ifndef _MTK_JPEG_CORE_H
#define _MTK_JPEG_CORE_H

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-v4l2.h>

#include "mtk_jpeg_dec_hw.h"

#define MTK_JPEG_NAME		"mtk-jpeg"

#define MTK_JPEG_FMT_FLAG_OUTPUT	BIT(0)
#define MTK_JPEG_FMT_FLAG_CAPTURE	BIT(1)

#define MTK_JPEG_MIN_WIDTH	32U
#define MTK_JPEG_MIN_HEIGHT	32U
#define MTK_JPEG_MAX_WIDTH	65535U
#define MTK_JPEG_MAX_HEIGHT	65535U

#define MTK_JPEG_DEFAULT_SIZEIMAGE	(1 * 1024 * 1024)

#define MTK_JPEG_HW_TIMEOUT_MSEC 1000

#define MTK_JPEG_MAX_EXIF_SIZE	(64 * 1024)

#define MTK_JPEG_ADDR_MASK GENMASK(1, 0)

/**
 * enum mtk_jpeg_ctx_state - states of the context state machine
 * @MTK_JPEG_INIT:		current state is initialized
 * @MTK_JPEG_RUNNING:		current state is running
 * @MTK_JPEG_SOURCE_CHANGE:	current state is source resolution change
 */
enum mtk_jpeg_ctx_state {
	MTK_JPEG_INIT = 0,
	MTK_JPEG_RUNNING,
	MTK_JPEG_SOURCE_CHANGE,
};

/**
 * struct mtk_jpeg_variant - mtk jpeg driver variant
 * @clks:			clock names
 * @num_clks:			numbers of clock
 * @formats:			jpeg driver's internal color format
 * @num_formats:		number of formats
 * @qops:			the callback of jpeg vb2_ops
 * @irq_handler:		jpeg irq handler callback
 * @hw_reset:			jpeg hardware reset callback
 * @m2m_ops:			the callback of jpeg v4l2_m2m_ops
 * @dev_name:			jpeg device name
 * @ioctl_ops:			the callback of jpeg v4l2_ioctl_ops
 * @out_q_default_fourcc:	output queue default fourcc
 * @cap_q_default_fourcc:	capture queue default fourcc
 * @multi_core:		mark jpeg hw is multi_core or not
 * @jpeg_worker:		jpeg dec or enc worker
 * @support_34bit:	flag to check support for 34-bit DMA address
 */
struct mtk_jpeg_variant {
	struct clk_bulk_data *clks;
	int num_clks;
	struct mtk_jpeg_fmt *formats;
	int num_formats;
	const struct vb2_ops *qops;
	irqreturn_t (*irq_handler)(int irq, void *priv);
	void (*hw_reset)(void __iomem *base);
	const struct v4l2_m2m_ops *m2m_ops;
	const char *dev_name;
	const struct v4l2_ioctl_ops *ioctl_ops;
	u32 out_q_default_fourcc;
	u32 cap_q_default_fourcc;
	bool multi_core;
	void (*jpeg_worker)(struct work_struct *work);
	bool support_34bit;
};

struct mtk_jpeg_src_buf {
	u32 frame_num;
	struct vb2_v4l2_buffer b;
	struct list_head list;
	u32 bs_size;
	struct mtk_jpeg_dec_param dec_param;

	struct mtk_jpeg_ctx *curr_ctx;
};

enum mtk_jpeg_hw_state {
	MTK_JPEG_HW_IDLE = 0,
	MTK_JPEG_HW_BUSY = 1,
};

struct mtk_jpeg_hw_param {
	struct vb2_v4l2_buffer *src_buffer;
	struct vb2_v4l2_buffer *dst_buffer;
	struct mtk_jpeg_ctx *curr_ctx;
};

enum mtk_jpegenc_hw_id {
	MTK_JPEGENC_HW0,
	MTK_JPEGENC_HW1,
	MTK_JPEGENC_HW_MAX,
};

enum mtk_jpegdec_hw_id {
	MTK_JPEGDEC_HW0,
	MTK_JPEGDEC_HW1,
	MTK_JPEGDEC_HW2,
	MTK_JPEGDEC_HW_MAX,
};

/**
 * struct mtk_jpegenc_clk - Structure used to store vcodec clock information
 * @clks:		JPEG encode clock
 * @clk_num:		JPEG encode clock numbers
 */
struct mtk_jpegenc_clk {
	struct clk_bulk_data *clks;
	int clk_num;
};

/**
 * struct mtk_jpegdec_clk - Structure used to store vcodec clock information
 * @clks:		JPEG decode clock
 * @clk_num:		JPEG decode clock numbers
 */
struct mtk_jpegdec_clk {
	struct clk_bulk_data *clks;
	int clk_num;
};

/**
 * struct mtk_jpegenc_comp_dev - JPEG COREX abstraction
 * @dev:		JPEG device
 * @plat_dev:		platform device data
 * @reg_base:		JPEG registers mapping
 * @master_dev:		mtk_jpeg_dev device
 * @venc_clk:		jpeg encode clock
 * @jpegenc_irq:	jpeg encode irq num
 * @job_timeout_work:	encode timeout workqueue
 * @hw_param:		jpeg encode hw parameters
 * @hw_state:		record hw state
 * @hw_lock:		spinlock protecting the hw device resource
 */
struct mtk_jpegenc_comp_dev {
	struct device *dev;
	struct platform_device *plat_dev;
	void __iomem *reg_base;
	struct mtk_jpeg_dev *master_dev;
	struct mtk_jpegenc_clk venc_clk;
	int jpegenc_irq;
	struct delayed_work job_timeout_work;
	struct mtk_jpeg_hw_param hw_param;
	enum mtk_jpeg_hw_state hw_state;
	/* spinlock protecting the hw device resource */
	spinlock_t hw_lock;
};

/**
 * struct mtk_jpegdec_comp_dev - JPEG COREX abstraction
 * @dev:			JPEG device
 * @plat_dev:			platform device data
 * @reg_base:			JPEG registers mapping
 * @master_dev:			mtk_jpeg_dev device
 * @jdec_clk:			mtk_jpegdec_clk
 * @jpegdec_irq:		jpeg decode irq num
 * @job_timeout_work:		decode timeout workqueue
 * @hw_param:			jpeg decode hw parameters
 * @hw_state:			record hw state
 * @hw_lock:			spinlock protecting hw
 */
struct mtk_jpegdec_comp_dev {
	struct device *dev;
	struct platform_device *plat_dev;
	void __iomem *reg_base;
	struct mtk_jpeg_dev *master_dev;
	struct mtk_jpegdec_clk jdec_clk;
	int jpegdec_irq;
	struct delayed_work job_timeout_work;
	struct mtk_jpeg_hw_param hw_param;
	enum mtk_jpeg_hw_state hw_state;
	/* spinlock protecting the hw device resource */
	spinlock_t hw_lock;
};

/**
 * struct mtk_jpeg_dev - JPEG IP abstraction
 * @lock:		the mutex protecting this structure
 * @hw_lock:		spinlock protecting the hw device resource
 * @workqueue:		decode work queue
 * @dev:		JPEG device
 * @v4l2_dev:		v4l2 device for mem2mem mode
 * @m2m_dev:		v4l2 mem2mem device data
 * @alloc_ctx:		videobuf2 memory allocator's context
 * @vdev:		video device node for jpeg mem2mem mode
 * @reg_base:		JPEG registers mapping
 * @job_timeout_work:	IRQ timeout structure
 * @variant:		driver variant to be used
 * @reg_encbase:	jpg encode register base addr
 * @enc_hw_dev:	jpg encode hardware device
 * @hw_wq:		jpg wait queue
 * @hw_rdy:		jpg hw ready flag
 * @reg_decbase:	jpg decode register base addr
 * @dec_hw_dev:	jpg decode hardware device
 * @hw_index:		jpg hw index
 */
struct mtk_jpeg_dev {
	struct mutex		lock;
	spinlock_t		hw_lock;
	struct workqueue_struct	*workqueue;
	struct device		*dev;
	struct v4l2_device	v4l2_dev;
	struct v4l2_m2m_dev	*m2m_dev;
	void			*alloc_ctx;
	struct video_device	*vdev;
	void __iomem		*reg_base;
	struct delayed_work job_timeout_work;
	const struct mtk_jpeg_variant *variant;

	void __iomem *reg_encbase[MTK_JPEGENC_HW_MAX];
	struct mtk_jpegenc_comp_dev *enc_hw_dev[MTK_JPEGENC_HW_MAX];
	wait_queue_head_t hw_wq;
	atomic_t hw_rdy;

	void __iomem *reg_decbase[MTK_JPEGDEC_HW_MAX];
	struct mtk_jpegdec_comp_dev *dec_hw_dev[MTK_JPEGDEC_HW_MAX];
	atomic_t hw_index;
};

/**
 * struct mtk_jpeg_fmt - driver's internal color format data
 * @fourcc:	the fourcc code, 0 if not applicable
 * @hw_format:	hardware format value
 * @h_sample:	horizontal sample count of plane in 4 * 4 pixel image
 * @v_sample:	vertical sample count of plane in 4 * 4 pixel image
 * @colplanes:	number of color planes (1 for packed formats)
 * @h_align:	horizontal alignment order (align to 2^h_align)
 * @v_align:	vertical alignment order (align to 2^v_align)
 * @flags:	flags describing format applicability
 */
struct mtk_jpeg_fmt {
	u32	fourcc;
	u32	hw_format;
	int	h_sample[VIDEO_MAX_PLANES];
	int	v_sample[VIDEO_MAX_PLANES];
	int	colplanes;
	int	h_align;
	int	v_align;
	u32	flags;
};

/**
 * struct mtk_jpeg_q_data - parameters of one queue
 * @fmt:	  driver-specific format of this queue
 * @pix_mp:	  multiplanar format
 * @enc_crop_rect:	jpeg encoder crop information
 */
struct mtk_jpeg_q_data {
	struct mtk_jpeg_fmt	*fmt;
	struct v4l2_pix_format_mplane pix_mp;
	struct v4l2_rect enc_crop_rect;
};

/**
 * struct mtk_jpeg_ctx - the device context data
 * @jpeg:			JPEG IP device for this context
 * @out_q:			source (output) queue information
 * @cap_q:			destination queue information
 * @fh:				V4L2 file handle
 * @state:			state of the context
 * @enable_exif:		enable exif mode of jpeg encoder
 * @enc_quality:		jpeg encoder quality
 * @restart_interval:		jpeg encoder restart interval
 * @ctrl_hdl:			controls handler
 * @jpeg_work:			jpeg encoder workqueue
 * @total_frame_num:		encoded frame number
 * @dst_done_queue:		encoded frame buffer queue
 * @done_queue_lock:		encoded frame operation spinlock
 * @last_done_frame_num:	the last encoded frame number
 */
struct mtk_jpeg_ctx {
	struct mtk_jpeg_dev		*jpeg;
	struct mtk_jpeg_q_data		out_q;
	struct mtk_jpeg_q_data		cap_q;
	struct v4l2_fh			fh;
	enum mtk_jpeg_ctx_state		state;
	bool enable_exif;
	u8 enc_quality;
	u8 restart_interval;
	struct v4l2_ctrl_handler ctrl_hdl;

	struct work_struct jpeg_work;
	u32 total_frame_num;
	struct list_head dst_done_queue;
	/* spinlock protecting the encode done buffer */
	spinlock_t done_queue_lock;
	u32 last_done_frame_num;
};

#endif /* _MTK_JPEG_CORE_H */
