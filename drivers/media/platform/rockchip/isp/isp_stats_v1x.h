/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_ISP_STATS_V1X_H
#define _RKISP_ISP_STATS_V1X_H

#include <linux/rkisp1-config.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include "common.h"

struct rkisp_isp_stats_vdev;

struct rkisp_stats_v1x_ops {
	void (*get_awb_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp1_stat_buffer *pbuf);
	void (*get_aec_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp1_stat_buffer *pbuf);
	void (*get_afc_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp1_stat_buffer *pbuf);
	void (*get_hst_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp1_stat_buffer *pbuf);
	void (*get_bls_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp1_stat_buffer *pbuf);
	void (*get_emb_data)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp1_stat_buffer *pbuf);
};

struct rkisp_stats_v1x_config {
	const int ae_mean_max;
	const int hist_bin_n_max;
};

#if IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V1X)
void rkisp_init_stats_vdev_v1x(struct rkisp_isp_stats_vdev *stats_vdev);
void rkisp_uninit_stats_vdev_v1x(struct rkisp_isp_stats_vdev *stats_vdev);
#else
static inline void rkisp_init_stats_vdev_v1x(struct rkisp_isp_stats_vdev *stats_vdev) {}
static inline void rkisp_uninit_stats_vdev_v1x(struct rkisp_isp_stats_vdev *stats_vdev) {}
#endif

#endif /* _RKISP_ISP_STATS_V1X_H */
