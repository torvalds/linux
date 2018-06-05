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

#ifndef _CIF_ISP10_ISP_H
#define _CIF_ISP10_ISP_H

#include <media/v4l2-common.h>
#include <media/videobuf2-core.h>
#include <media/rk-isp10-ioctl.h>
#include <media/v4l2-controls_rockchip.h>

/*
 * ISP device struct
 */
#define CIF_ISP10_META_INFO_NUM                2

enum cif_isp10_pix_fmt;

enum cif_isp10_pix_fmt_quantization {
	CIF_ISP10_QUANTIZATION_DEFAULT = 0,
	CIF_ISP10_QUANTIZATION_FULL_RANGE = 1,
	CIF_ISP10_QUANTIZATION_LIM_RANGE = 2
};

struct cif_isp10_isp_meta_info {
	unsigned int write_id;
	unsigned int read_id;
	unsigned int read_cnt;
	unsigned int read_max;
	unsigned int frame_id[CIF_ISP10_META_INFO_NUM];
	struct timeval vs_t[CIF_ISP10_META_INFO_NUM];
	struct timeval fi_t[CIF_ISP10_META_INFO_NUM];
};

struct cif_isp10_isp_cfgs_log {
	unsigned int s_frame_id[3];
	unsigned int new_id;
	unsigned int curr_id;
};

struct cif_isp10_isp_other_cfgs {
	struct cif_isp10_isp_cfgs_log log[CIFISP_MODULE_MAX];
	struct cifisp_isp_other_cfg cfgs[3];
	unsigned int module_updates;
	unsigned int module_actives;
};

struct cif_isp10_isp_meas_cfgs {
	struct cif_isp10_isp_cfgs_log log[CIFISP_MODULE_MAX];
	struct cifisp_isp_meas_cfg cfgs[3];
	unsigned int module_updates;
	unsigned int module_actives;
};

struct cif_isp10_isp_meas_stats {
	unsigned int g_frame_id;
	struct cifisp_stat_buffer stat;
};

struct cif_isp10_isp_dev {
	/*
	 * Purpose of mutex is to protect and serialize use
	 * of isp data structure and CIF API calls.
	 */
	struct mutex mutex;
	/* Current ISP parameters */
	spinlock_t config_lock;
	struct cif_isp10_isp_other_cfgs other_cfgs;
	struct cif_isp10_isp_meas_cfgs meas_cfgs;
	struct cif_isp10_isp_meas_stats meas_stats;

	bool cif_ism_cropping;

	enum cif_isp10_pix_fmt_quantization quantization;

	/* input resolution needed for LSC param check */
	unsigned int input_width;
	unsigned int input_height;
	unsigned int active_lsc_width;
	unsigned int active_lsc_height;

	/* ISP statistics related */
	spinlock_t irq_lock;
	/* ISP statistics related */
	spinlock_t req_lock;
	struct list_head stat;
	void __iomem *base_addr;    /* registers base address */

	bool streamon;
	unsigned int v_blanking_us;

	unsigned int frame_id;
	unsigned int frame_id_setexp;
	unsigned int active_meas;
	unsigned int meas_send_alone;

	bool awb_meas_ready;
	bool afm_meas_ready;
	bool aec_meas_ready;
	bool hst_meas_ready;

	struct timeval vs_t;	/* updated each frame */
	struct timeval fi_t;	/* updated each frame */
	struct workqueue_struct *readout_wq;
	struct cif_isp10_isp_meta_info meta_info;

	unsigned int *dev_id;

	struct vb2_queue vb2_vidq;
};

enum cif_isp10_isp_readout_cmd {
	CIF_ISP10_ISP_READOUT_MEAS = 0,
	CIF_ISP10_ISP_READOUT_META = 1,
};

struct cif_isp10_isp_readout_work {
	struct work_struct work;
	struct cif_isp10_isp_dev *isp_dev;

	unsigned int frame_id;
	unsigned int active_meas;
	struct timeval vs_t;
	struct timeval fi_t;
	enum cif_isp10_isp_readout_cmd readout;
	struct vb2_buffer *vb;
	unsigned int stream_id;
	struct cifisp_isp_metadata *isp_metadata;
};

int register_cifisp_device(
	struct cif_isp10_isp_dev *isp_dev,
	struct video_device *vdev_cifisp,
	struct v4l2_device *v4l2_dev,
	void __iomem *cif_reg_baseaddress);
void unregister_cifisp_device(struct video_device *vdev_cifisp);
void cifisp_configure_isp(
	struct cif_isp10_isp_dev *isp_dev,
	enum cif_isp10_pix_fmt in_pix_fmt,
	enum cif_isp10_pix_fmt_quantization quantization);
void cifisp_disable_isp(struct cif_isp10_isp_dev *isp_dev);
int cifisp_isp_isr(struct cif_isp10_isp_dev *isp_dev, u32 isp_mis);
void cifisp_clr_readout_wq(struct cif_isp10_isp_dev *isp_dev);
void cifisp_v_start(struct cif_isp10_isp_dev *isp_dev,
	const struct timeval *timestamp);
void cifisp_frame_in(
	struct cif_isp10_isp_dev *isp_dev,
	const struct timeval *fi_t);
void cifisp_frame_id_reset(
	struct cif_isp10_isp_dev *isp_dev);
void cifisp_isp_readout_work(struct work_struct *work);

#endif
