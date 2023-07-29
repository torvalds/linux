/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#ifndef _MTK_VCODEC_COM_DRV_H_
#define _MTK_VCODEC_COM_DRV_H_

#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>

#define MTK_VCODEC_MAX_PLANES	3

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

enum mtk_fmt_type {
	MTK_FMT_DEC = 0,
	MTK_FMT_ENC = 1,
	MTK_FMT_FRAME = 2,
};

/*
 * struct mtk_video_fmt - Structure used to store information about pixelformats
 */
struct mtk_video_fmt {
	u32	fourcc;
	enum mtk_fmt_type	type;
	u32	num_planes;
	u32	flags;
	struct v4l2_frmsize_stepwise frmsize;
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

/*
 * enum mtk_instance_type - The type of an MTK Vcodec instance.
 */
enum mtk_instance_type {
	MTK_INST_DECODER		= 0,
	MTK_INST_ENCODER		= 1,
};

#endif /* _MTK_VCODEC_COM_DRV_H_ */
