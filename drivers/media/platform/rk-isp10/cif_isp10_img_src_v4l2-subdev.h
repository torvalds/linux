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

#ifndef _CIF_ISP10_IMG_SRC_V4L2_SUBDEV_H
#define _CIF_ISP10_IMG_SRC_V4L2_SUBDEV_H

#define CIF_ISP10_IMG_SRC_V4L2_I2C_SUBDEV  "v4l2-i2c-subdev"

void *cif_isp10_img_src_v4l2_i2c_subdev_to_img_src(
	struct device *dev,
	struct pltfrm_soc_cfg *soc_cfg);

int cif_isp10_img_src_v4l2_subdev_s_streaming(
	void *img_src,
	bool enable);

int cif_isp10_img_src_v4l2_subdev_s_power(
	void *img_src,
	bool on);

int cif_isp10_img_src_v4l2_subdev_enum_strm_fmts(
	void *img_src,
	u32 index,
	struct cif_isp10_strm_fmt_desc *strm_fmt_desc);

int cif_isp10_img_src_v4l2_subdev_s_strm_fmt(
	void *img_src,
	struct cif_isp10_strm_fmt *strm_fmt);

int cif_isp10_img_src_v4l2_subdev_g_ctrl(
	void *img_src,
	int id,
	int *val);

int cif_isp10_img_src_v4l2_subdev_s_ctrl(
	void *img_src,
	int id,
	int val);

const char *cif_isp10_img_src_v4l2_subdev_g_name(
	void *img_src);

int cif_isp10_img_src_v4l2_subdev_s_ext_ctrls(
	void *img_src,
	struct cif_isp10_img_src_ext_ctrl *ctrl);

long cif_isp10_img_src_v4l2_subdev_ioctl(
	void *img_src,
	unsigned int cmd,
	void *arg);

int cif_isp10_img_src_v4l2_subdev_s_frame_interval(
	void *img_src,
	struct cif_isp10_frm_intrvl *frm_intrvl);

int cif_isp10_img_src_v4l2_subdev_g_frame_interval(
	void *img_src,
	struct cif_isp10_frm_intrvl *frm_intrvl);

#endif
