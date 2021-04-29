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

#ifndef _RKISP_H
#define _RKISP_H

#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/platform_device.h>
#include <linux/rk-camera-module.h>
#include <media/v4l2-fwnode.h>
#include "common.h"
#include "isp_ispp.h"

#define CIF_ISP_INPUT_W_MAX		4416
#define CIF_ISP_INPUT_H_MAX		3312
#define CIF_ISP_INPUT_W_MAX_V12		3264
#define CIF_ISP_INPUT_H_MAX_V12		2448
#define CIF_ISP_INPUT_W_MAX_V13		1920
#define CIF_ISP_INPUT_H_MAX_V13		1080
#define CIF_ISP_INPUT_W_MAX_V21		4096
#define CIF_ISP_INPUT_H_MAX_V21		3072
#define CIF_ISP_INPUT_W_MIN		208
#define CIF_ISP_INPUT_H_MIN		128
#define CIF_ISP_OUTPUT_W_MAX		CIF_ISP_INPUT_W_MAX
#define CIF_ISP_OUTPUT_H_MAX		CIF_ISP_INPUT_H_MAX
#define CIF_ISP_OUTPUT_W_MIN		CIF_ISP_INPUT_W_MIN
#define CIF_ISP_OUTPUT_H_MIN		CIF_ISP_INPUT_H_MIN
#define CIF_ISP_ADD_DATA_VC_MAX		3

struct rkisp_stream;

/*
 * struct ispsd_in_fmt - ISP intput-pad format
 *
 * Translate mbus_code to hardware format values
 *
 * @bus_width: used for parallel
 */
struct ispsd_in_fmt {
	u32 mbus_code;
	u8 name[16];
	u8 fmt_type;
	u32 mipi_dt;
	u32 yuv_seq;
	enum rkisp_fmt_raw_pat_type bayer_pat;
	u8 bus_width;
};

struct ispsd_out_fmt {
	u32 mbus_code;
	u8 fmt_type;
};

struct rkisp_ie_config {
	unsigned int effect;
};

enum rkisp_isp_pad {
	RKISP_ISP_PAD_SINK,
	RKISP_ISP_PAD_SINK_PARAMS,
	RKISP_ISP_PAD_SOURCE_PATH,
	RKISP_ISP_PAD_SOURCE_STATS,
	RKISP_ISP_PAD_MAX
};

/*
 * struct rkisp_isp_subdev - ISP sub-device
 *
 * See Cropping regions of ISP in rkisp.c for details
 * @in_frm: input size, equal to sensor size
 * @in_fmt: intput format
 * @in_crop: crop for sink pad
 * @out_fmt: output format
 * @out_crop: output size
 *
 * @dphy_errctrl_disabled: if dphy errctrl is disabled(avoid endless interrupt)
 * @frm_sync_seq: frame sequence, to sync frame_id between video devices.
 * @quantization: output quantization
 */
struct rkisp_isp_subdev {
	struct v4l2_subdev sd;
	struct media_pad pads[RKISP_ISP_PAD_MAX];
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_mbus_framefmt in_frm;
	struct ispsd_in_fmt in_fmt;
	struct v4l2_rect in_crop;
	struct ispsd_out_fmt out_fmt;
	struct v4l2_rect out_crop;
	bool dphy_errctrl_disabled;
	atomic_t frm_sync_seq;
	enum v4l2_quantization quantization;
	enum v4l2_colorspace colorspace;
	u64 frm_timestamp;
	struct frame_debug_info dbg;
};

struct rkisp_emd_data {
	struct kfifo mipi_kfifo;
	unsigned int data_len;
	unsigned int frame_id;
};

int rkisp_register_isp_subdev(struct rkisp_device *isp_dev,
			       struct v4l2_device *v4l2_dev);

void rkisp_unregister_isp_subdev(struct rkisp_device *isp_dev);

#ifdef CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP
void rkisp_chk_tb_over(struct rkisp_device *isp_dev);
#else
static inline void rkisp_chk_tb_over(struct rkisp_device *isp_dev) {}
#endif

void rkisp_mipi_isr(unsigned int mipi_mis, struct rkisp_device *dev);

void rkisp_mipi_v13_isr(unsigned int err1, unsigned int err2,
			       unsigned int err3, struct rkisp_device *dev);

void rkisp_isp_isr(unsigned int isp_mis, unsigned int isp3a_mis,
		   struct rkisp_device *dev);

irqreturn_t rkisp_vs_isr_handler(int irq, void *ctx);

int rkisp_align_sensor_resolution(struct rkisp_device *dev,
				  struct v4l2_rect *crop, bool user);

struct media_pad *rkisp_media_entity_remote_pad(struct media_pad *pad);

int rkisp_update_sensor_info(struct rkisp_device *dev);

u32 rkisp_mbus_pixelcode_to_v4l2(u32 pixelcode);

void rkisp_isp_queue_event_sof(struct rkisp_isp_subdev *isp);

void rkisp_check_idle(struct rkisp_device *dev, u32 irq);

void rkisp_trigger_read_back(struct rkisp_device *dev, u8 dma2frm, u32 mode, bool is_try);

static inline
struct ispsd_out_fmt *rkisp_get_ispsd_out_fmt(struct rkisp_isp_subdev *isp_sdev)
{
	return &isp_sdev->out_fmt;
}

static inline
struct ispsd_in_fmt *rkisp_get_ispsd_in_fmt(struct rkisp_isp_subdev *isp_sdev)
{
	return &isp_sdev->in_fmt;
}

static inline
struct v4l2_rect *rkisp_get_isp_sd_win(struct rkisp_isp_subdev *isp_sdev)
{
	return &isp_sdev->out_crop;
}

#endif /* _RKISP_H */
