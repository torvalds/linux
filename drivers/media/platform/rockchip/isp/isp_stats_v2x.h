/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_ISP_STATS_V2X_H
#define _RKISP_ISP_STATS_V2X_H

#include <linux/rkisp1-config.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include "common.h"

//#define RKISP_RD_STATS_FROM_DDR
#define RKISP_RD_STATS_BUF_SIZE		0x35000

struct rkisp_isp_stats_vdev;
struct rkisp_stats_v2x_ops {
	void (*get_siawb_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp_isp2x_stat_buffer *pbuf);
	void (*get_rawawb_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp_isp2x_stat_buffer *pbuf);

	void (*get_siaf_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp_isp2x_stat_buffer *pbuf);
	void (*get_rawaf_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp_isp2x_stat_buffer *pbuf);

	void (*get_yuvae_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp_isp2x_stat_buffer *pbuf);
	void (*get_sihst_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp_isp2x_stat_buffer *pbuf);

	void (*get_rawaebig1_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp_isp2x_stat_buffer *pbuf);
	void (*get_rawhstbig1_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp_isp2x_stat_buffer *pbuf);

	void (*get_rawaebig2_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp_isp2x_stat_buffer *pbuf);
	void (*get_rawhstbig2_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp_isp2x_stat_buffer *pbuf);

	void (*get_rawaebig3_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp_isp2x_stat_buffer *pbuf);
	void (*get_rawhstbig3_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp_isp2x_stat_buffer *pbuf);

	void (*get_rawaelite_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp_isp2x_stat_buffer *pbuf);
	void (*get_rawhstlite_meas)(struct rkisp_isp_stats_vdev *stats_vdev,
			     struct rkisp_isp2x_stat_buffer *pbuf);
};

void rkisp_stats_first_ddr_config_v2x(struct rkisp_isp_stats_vdev *stats_vdev);
void rkisp_init_stats_vdev_v2x(struct rkisp_isp_stats_vdev *stats_vdev);
void rkisp_uninit_stats_vdev_v2x(struct rkisp_isp_stats_vdev *stats_vdev);

#endif /* _RKISP_ISP_STATS_V2X_H */
