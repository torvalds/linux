/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2022 Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_ISP_STATS_V32_H
#define _RKISP_ISP_STATS_V32_H

#include <linux/rkisp1-config.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include "common.h"

#define ISP32_RD_STATS_BUF_SIZE 0x10000

struct rkisp_isp_stats_vdev;
struct rkisp_stats_ops_v32 {
	int (*get_rawawb_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp32_isp_stat_buffer *pbuf);
	int (*get_rawaf_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			      struct rkisp32_isp_stat_buffer *pbuf);
	int (*get_rawae0_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			       struct rkisp32_isp_stat_buffer *pbuf);
	int (*get_rawhst0_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp32_isp_stat_buffer *pbuf);
	int (*get_rawae1_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp32_isp_stat_buffer *pbuf);
	int (*get_rawhst1_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp32_isp_stat_buffer *pbuf);
	int (*get_rawae2_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			       struct rkisp32_isp_stat_buffer *pbuf);
	int (*get_rawhst2_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp32_isp_stat_buffer *pbuf);
	int (*get_rawae3_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp32_isp_stat_buffer *pbuf);
	int (*get_rawhst3_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
				struct rkisp32_isp_stat_buffer *pbuf);
	int (*get_bls_stats)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp32_isp_stat_buffer *pbuf);
	int (*get_dhaz_stats)(struct rkisp_isp_stats_vdev *stats_vdev,
			      struct rkisp32_isp_stat_buffer *pbuf);
	int (*get_vsm_stats)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp32_isp_stat_buffer *pbuf);
};

#if IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V32)
void rkisp_stats_first_ddr_config_v32(struct rkisp_isp_stats_vdev *stats_vdev);
void rkisp_stats_next_ddr_config_v32(struct rkisp_isp_stats_vdev *stats_vdev);
void rkisp_init_stats_vdev_v32(struct rkisp_isp_stats_vdev *stats_vdev);
void rkisp_uninit_stats_vdev_v32(struct rkisp_isp_stats_vdev *stats_vdev);
#else
static inline void rkisp_stats_first_ddr_config_v32(struct rkisp_isp_stats_vdev *stats_vdev) {}
static inline void rkisp_stats_next_ddr_config_v32(struct rkisp_isp_stats_vdev *stats_vdev) {}
static inline void rkisp_init_stats_vdev_v32(struct rkisp_isp_stats_vdev *stats_vdev) {}
static inline void rkisp_uninit_stats_vdev_v32(struct rkisp_isp_stats_vdev *stats_vdev) {}
#endif

#endif /* _RKISP_ISP_STATS_V32_H */
