/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Ming Hsiu Tsai <minghsiu.tsai@mediatek.com>
 *         Rick Chang <rick.chang@mediatek.com>
 */

#ifndef _MTK_JPEG_CORE_H
#define _MTK_JPEG_CORE_H

#include <linux/interrupt.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>

#define MTK_JPEG_NAME		"mtk-jpeg"

#define MTK_JPEG_FMT_FLAG_DEC_OUTPUT	BIT(0)
#define MTK_JPEG_FMT_FLAG_DEC_CAPTURE	BIT(1)

#define MTK_JPEG_FMT_TYPE_OUTPUT	1
#define MTK_JPEG_FMT_TYPE_CAPTURE	2

#define MTK_JPEG_MIN_WIDTH	32
#define MTK_JPEG_MIN_HEIGHT	32
#define MTK_JPEG_MAX_WIDTH	8192
#define MTK_JPEG_MAX_HEIGHT	8192

#define MTK_JPEG_DEFAULT_SIZEIMAGE	(1 * 1024 * 1024)

enum mtk_jpeg_ctx_state {
	MTK_JPEG_INIT = 0,
	MTK_JPEG_RUNNING,
	MTK_JPEG_SOURCE_CHANGE,
};

/**
 * struct mt_jpeg - JPEG IP abstraction
 * @lock:		the mutex protecting this structure
 * @hw_lock:		spinlock protecting the hw device resource
 * @workqueue:		decode work queue
 * @dev:		JPEG device
 * @v4l2_dev:		v4l2 device for mem2mem mode
 * @m2m_dev:		v4l2 mem2mem device data
 * @alloc_ctx:		videobuf2 memory allocator's context
 * @dec_vdev:		video device node for decoder mem2mem mode
 * @dec_reg_base:	JPEG registers mapping
 * @clk_jdec:		JPEG hw working clock
 * @clk_jdec_smi:	JPEG SMI bus clock
 * @larb:		SMI device
 */
struct mtk_jpeg_dev {
	struct mutex		lock;
	spinlock_t		hw_lock;
	struct workqueue_struct	*workqueue;
	struct device		*dev;
	struct v4l2_device	v4l2_dev;
	struct v4l2_m2m_dev	*m2m_dev;
	void			*alloc_ctx;
	struct video_device	*dec_vdev;
	void __iomem		*dec_reg_base;
	struct clk		*clk_jdec;
	struct clk		*clk_jdec_smi;
	struct device		*larb;
};

/**
 * struct jpeg_fmt - driver's internal color format data
 * @fourcc:	the fourcc code, 0 if not applicable
 * @h_sample:	horizontal sample count of plane in 4 * 4 pixel image
 * @v_sample:	vertical sample count of plane in 4 * 4 pixel image
 * @colplanes:	number of color planes (1 for packed formats)
 * @h_align:	horizontal alignment order (align to 2^h_align)
 * @v_align:	vertical alignment order (align to 2^v_align)
 * @flags:	flags describing format applicability
 */
struct mtk_jpeg_fmt {
	u32	fourcc;
	int	h_sample[VIDEO_MAX_PLANES];
	int	v_sample[VIDEO_MAX_PLANES];
	int	colplanes;
	int	h_align;
	int	v_align;
	u32	flags;
};

/**
 * mtk_jpeg_q_data - parameters of one queue
 * @fmt:	  driver-specific format of this queue
 * @w:		  image width
 * @h:		  image height
 * @bytesperline: distance in bytes between the leftmost pixels in two adjacent
 *                lines
 * @sizeimage:	  image buffer size in bytes
 */
struct mtk_jpeg_q_data {
	struct mtk_jpeg_fmt	*fmt;
	u32			w;
	u32			h;
	u32			bytesperline[VIDEO_MAX_PLANES];
	u32			sizeimage[VIDEO_MAX_PLANES];
};

/**
 * mtk_jpeg_ctx - the device context data
 * @jpeg:		JPEG IP device for this context
 * @out_q:		source (output) queue information
 * @cap_q:		destination (capture) queue queue information
 * @fh:			V4L2 file handle
 * @dec_param		parameters for HW decoding
 * @state:		state of the context
 * @header_valid:	set if header has been parsed and valid
 * @colorspace: enum v4l2_colorspace; supplemental to pixelformat
 * @ycbcr_enc: enum v4l2_ycbcr_encoding, Y'CbCr encoding
 * @quantization: enum v4l2_quantization, colorspace quantization
 * @xfer_func: enum v4l2_xfer_func, colorspace transfer function
 */
struct mtk_jpeg_ctx {
	struct mtk_jpeg_dev		*jpeg;
	struct mtk_jpeg_q_data		out_q;
	struct mtk_jpeg_q_data		cap_q;
	struct v4l2_fh			fh;
	enum mtk_jpeg_ctx_state		state;

	enum v4l2_colorspace colorspace;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_quantization quantization;
	enum v4l2_xfer_func xfer_func;
};

#endif /* _MTK_JPEG_CORE_H */
