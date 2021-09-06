/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_ISP_STATS_V3X_H
#define _RKISP_ISP_STATS_V3X_H

#include <linux/rkisp1-config.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include "common.h"

#define ISP3X_RD_STATS_BUF_SIZE 0x10000

struct rkisp_isp_stats_vdev;
struct rkisp_stats_ops_v3x {
	int (*get_rawawb_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp3x_isp_stat_buffer *pbuf, u32 id);
	int (*get_rawaf_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			      struct rkisp3x_isp_stat_buffer *pbuf, u32 id);
	int (*get_rawae0_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			       struct rkisp3x_isp_stat_buffer *pbuf, u32 id);
	int (*get_rawhst0_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp3x_isp_stat_buffer *pbuf, u32 id);
	int (*get_rawae1_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp3x_isp_stat_buffer *pbuf, u32 id);
	int (*get_rawhst1_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp3x_isp_stat_buffer *pbuf, u32 id);
	int (*get_rawae2_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			       struct rkisp3x_isp_stat_buffer *pbuf, u32 id);
	int (*get_rawhst2_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp3x_isp_stat_buffer *pbuf, u32 id);
	int (*get_rawae3_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp3x_isp_stat_buffer *pbuf, u32 id);
	int (*get_rawhst3_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp3x_isp_stat_buffer *pbuf, u32 id);
	int (*get_bls_stats)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp3x_isp_stat_buffer *pbuf, u32 id);
	int (*get_dhaz_stats)(struct rkisp_isp_stats_vdev *stats_vdev,
			      struct rkisp3x_isp_stat_buffer *pbuf, u32 id);
};

#if IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V30)
void rkisp_stats_first_ddr_config_v3x(struct rkisp_isp_stats_vdev *stats_vdev);
void rkisp_init_stats_vdev_v3x(struct rkisp_isp_stats_vdev *stats_vdev);
void rkisp_uninit_stats_vdev_v3x(struct rkisp_isp_stats_vdev *stats_vdev);
#else
static inline void rkisp_stats_first_ddr_config_v3x(struct rkisp_isp_stats_vdev *stats_vdev) {}
static inline void rkisp_init_stats_vdev_v3x(struct rkisp_isp_stats_vdev *stats_vdev) {}
static inline void rkisp_uninit_stats_vdev_v3x(struct rkisp_isp_stats_vdev *stats_vdev) {}
#endif

#endif /* _RKISP_ISP_STATS_V3X_H */
