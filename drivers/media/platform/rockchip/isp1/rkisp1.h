/*
 * Rockchip isp1 driver
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _RKISP1_H
#define _RKISP1_H

#include <linux/platform_device.h>
#include <media/v4l2-fwnode.h>
#include "common.h"

struct rkisp1_stream;

/*
 * struct ispsd_in_fmt - ISP intput-pad format
 *
 * Translate mbus_code to hardware format values
 *
 * @bus_width: used for parallel
 */
struct ispsd_in_fmt {
	u32 mbus_code;
	u8 fmt_type;
	u32 mipi_dt;
	u32 yuv_seq;
	enum rkisp1_fmt_raw_pat_type bayer_pat;
	u8 bus_width;
};

struct ispsd_out_fmt {
	u32 mbus_code;
	u8 fmt_type;
};

struct rkisp1_ie_config {
	unsigned int effect;
};

enum rkisp1_isp_pad {
	RKISP1_ISP_PAD_SINK,
	RKISP1_ISP_PAD_SINK_PARAMS,
	RKISP1_ISP_PAD_SOURCE_PATH,
	RKISP1_ISP_PAD_SOURCE_STATS,
	RKISP1_ISP_PAD_MAX
};

/*
 * struct rkisp1_isp_subdev - ISP sub-device
 *
 * See Cropping regions of ISP in rkisp1.c for details
 * @in_frm: input size, don't have to be equal to sensor size
 * @in_fmt: intput format
 * @in_crop: crop for sink pad
 * @out_fmt: output format
 * @out_crop: output size
 *
 * @dphy_errctrl_disabled: if dphy errctrl is disabled(avoid endless interrupt)
 * @frm_sync_seq: frame sequence, to sync frame_id between video devices.
 * @quantization: output quantization
 */
struct rkisp1_isp_subdev {
	struct v4l2_subdev sd;
	struct media_pad pads[RKISP1_ISP_PAD_MAX];
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_mbus_framefmt in_frm;
	struct ispsd_in_fmt in_fmt;
	struct v4l2_rect in_crop;
	struct ispsd_out_fmt out_fmt;
	struct v4l2_rect out_crop;
	bool dphy_errctrl_disabled;
	atomic_t frm_sync_seq;
	enum v4l2_quantization quantization;
};

int rkisp1_register_isp_subdev(struct rkisp1_device *isp_dev,
			       struct v4l2_device *v4l2_dev);

void rkisp1_unregister_isp_subdev(struct rkisp1_device *isp_dev);

void rkisp1_mipi_isr(unsigned int mipi_mis, struct rkisp1_device *dev);

void rkisp1_mipi_v13_isr(unsigned int err1, unsigned int err2,
			       unsigned int err3, struct rkisp1_device *dev);

void rkisp1_isp_isr(unsigned int isp_mis, struct rkisp1_device *dev);

static inline
struct ispsd_out_fmt *rkisp1_get_ispsd_out_fmt(struct rkisp1_isp_subdev *isp_sdev)
{
	return &isp_sdev->out_fmt;
}

static inline
struct ispsd_in_fmt *rkisp1_get_ispsd_in_fmt(struct rkisp1_isp_subdev *isp_sdev)
{
	return &isp_sdev->in_fmt;
}

static inline
struct v4l2_rect *rkisp1_get_isp_sd_win(struct rkisp1_isp_subdev *isp_sdev)
{
	return &isp_sdev->out_crop;
}

#endif /* _RKISP1_H */
