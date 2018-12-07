/*
 *************************************************************************
 * Rockchip driver for CIF ISP 1.0
 * (Based on Intel driver for sofiaxxx)
 *
 * Copyright (C) 2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *************************************************************************
 */

#ifndef _CIF_ISP10_IMG_SRC_OPS_H
#define _CIF_ISP10_IMG_SRC_OPS_H
#include <linux/platform_data/rk_isp10_platform.h>
#include "cif_isp10_img_src_v4l2-subdev.h"

struct cif_isp10_img_src_ops {
	void * (*to_img_src)(
		CIF_ISP10_PLTFRM_DEVICE dev,
		struct pltfrm_soc_cfg *soc_cfg);
	int (*s_streaming)(
		void *img_src,
		bool enable);
	int (*s_power)(
		void *img_src,
		bool on);
	int (*enum_strm_fmts)(
		void *img_src,
		u32 index,
		struct cif_isp10_strm_fmt_desc *strm_fmt_desc);
	int (*s_strm_fmt)(
		void *img_src,
		struct cif_isp10_strm_fmt *strm_fmt);
	int (*g_ctrl)(
		void *img_src,
		int id,
		int *val);
	const char * (*g_name)(
		void *img_src);
	int (*s_ctrl)(
		void *img_src,
		int id,
		int val);
	int (*s_ext_ctrls)(
		void *img_src,
	    struct cif_isp10_img_src_ext_ctrl *ctrl);
	long (*ioctl)(
		void *img_src,
		unsigned int cmd,
		void *arg);
	int (*s_frame_interval)(
		void *img_src,
		struct cif_isp10_frm_intrvl *frm_intrvl);
	int (*g_frame_interval)(
		void *img_src,
		struct cif_isp10_frm_intrvl *frm_intrvl);
	int (*enum_frame_size)(
		void *img_src,
		void *fse);
};

const struct {
	const char *device_type;
	struct cif_isp10_img_src_ops ops;
} cif_isp10_img_src_ops[] = {
	{
		.device_type = CIF_ISP10_IMG_SRC_V4L2_I2C_SUBDEV,
		.ops = {
			.to_img_src =
				cif_isp10_img_src_v4l2_i2c_subdev_to_img_src,
			.s_streaming =
				cif_isp10_img_src_v4l2_subdev_s_streaming,
			.s_power =
				cif_isp10_img_src_v4l2_subdev_s_power,
			.enum_strm_fmts =
				cif_isp10_img_src_v4l2_subdev_enum_strm_fmts,
			.s_strm_fmt =
				cif_isp10_img_src_v4l2_subdev_s_strm_fmt,
			.g_ctrl =
				cif_isp10_img_src_v4l2_subdev_g_ctrl,
			.g_name =
				cif_isp10_img_src_v4l2_subdev_g_name,
			.s_ctrl =
				cif_isp10_img_src_v4l2_subdev_s_ctrl,
			.s_ext_ctrls =
				cif_isp10_img_src_v4l2_subdev_s_ext_ctrls,
			.ioctl =
				cif_isp10_img_src_v4l2_subdev_ioctl,
			.s_frame_interval =
				cif_isp10_img_src_v4l2_subdev_s_frame_interval,
			.g_frame_interval =
				cif_isp10_img_src_v4l2_subdev_g_frame_interval,
			.enum_frame_size =
				cif_isp10_img_src_v4l2_subdev_enum_frame_size,
		}
	},
};

#endif
