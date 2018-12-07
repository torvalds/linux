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

#ifndef _CIF_ISP10_IMG_SRC_H
#define _CIF_ISP10_IMG_SRC_H

struct cif_isp10_img_src;
struct cif_isp10_strm_fmt_desc;
struct cif_isp10_strm_fmt;
struct cif_isp10_csi_config;
enum cif_isp10_pix_fmt;

struct cif_isp10_frm_intrvl;
struct cif_isp10_img_src;
struct pltfrm_soc_cfg;

struct cif_isp10_img_src_ctrl {
	unsigned int id;
	int val;
};

struct cif_isp10_img_src_ext_ctrl {
	int cnt;
	unsigned int class;
	struct cif_isp10_img_src_ctrl *ctrls;
};

struct cif_isp10_img_src *cif_isp10_img_src_to_img_src(
	CIF_ISP10_PLTFRM_DEVICE dev,
	struct pltfrm_soc_cfg *soc_cfg);

int cif_isp10_img_src_s_streaming(
	struct cif_isp10_img_src *img_src,
	bool enable);

int cif_isp10_img_src_s_power(
	struct cif_isp10_img_src *img_src,
	bool on);

int cif_isp10_img_src_enum_strm_fmts(
	struct cif_isp10_img_src *img_src,
	u32 index,
	struct cif_isp10_strm_fmt_desc *strm_fmt_desc);

int cif_isp10_img_src_s_strm_fmt(
	struct cif_isp10_img_src *img_src,
	struct cif_isp10_strm_fmt *strm_fmt);

int cif_isp10_img_src_g_ctrl(
	struct cif_isp10_img_src *img_src,
	int id,
	int *val);

int cif_isp10_img_src_s_ctrl(
	struct cif_isp10_img_src *img_src,
	int id,
	int val);

const char *cif_isp10_img_src_g_name(
	struct cif_isp10_img_src *img_src);

int cif_isp10_img_src_s_ext_ctrls(
	struct cif_isp10_img_src *img_src,
	struct cif_isp10_img_src_ext_ctrl *ctrls);

long cif_isp10_img_src_ioctl(
	struct cif_isp10_img_src *img_src,
	unsigned int cmd,
	void *arg);

void *cif_isp10_img_src_g_img_src(
	struct cif_isp10_img_src *img_src);

int cif_isp10_img_src_s_frame_interval(
	struct cif_isp10_img_src *img_src,
	struct cif_isp10_frm_intrvl *frm_intrvl);

int cif_isp10_img_src_g_frame_interval(
	struct cif_isp10_img_src *img_src,
	struct cif_isp10_frm_intrvl *frm_intrvl);

int cif_isp10_img_src_enum_frame_size(
	struct cif_isp10_img_src *img_src,
	void *fse);

#endif
