/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISPP_DEV_H
#define _RKISPP_DEV_H

#include "ispp.h"
#include "params.h"
#include "stream.h"
#include "stats.h"

#define DRIVER_NAME			"rkispp"
#define II_VDEV_NAME DRIVER_NAME	"_input_image"
#define MB_VDEV_NAME DRIVER_NAME	"_m_bypass"
#define S0_VDEV_NAME DRIVER_NAME	"_scale0"
#define S1_VDEV_NAME DRIVER_NAME	"_scale1"
#define S2_VDEV_NAME DRIVER_NAME	"_scale2"

#define ISPP_MAX_BUS_CLK 4

enum rkispp_ver {
	ISPP_V10 = 0x00,
};

enum rkispp_input {
	INP_INVAL = 0,
	INP_ISP,
	INP_DDR,
};

struct rkispp_device {
	struct device *dev;
	int irq;
	int clks_num;
	struct clk *clks[ISPP_MAX_BUS_CLK];
	void __iomem *base_addr;
	struct iommu_domain *domain;
	struct vb2_alloc_ctx *alloc_ctx;

	struct media_device media_dev;
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler ctrl_handler;

	struct rkispp_subdev ispp_sdev;
	struct rkispp_stream_vdev stream_vdev;
	struct rkispp_params_vdev params_vdev;
	struct rkispp_stats_vdev stats_vdev;

	enum rkispp_ver	ispp_ver;
	/* mutex to serialize the calls from user */
	struct mutex apilock;
	/* mutex to serialize the calls of iq */
	struct mutex iqlock;
	/* lock for fec and ispp irq */
	spinlock_t irq_lock;
	enum rkispp_input inp;
	u32 isp_mode;
	wait_queue_head_t sync_onoff;
	bool stream_sync;
};
#endif
