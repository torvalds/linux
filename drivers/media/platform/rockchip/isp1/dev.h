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

#ifndef _RKISP1_DEV_H
#define _RKISP1_DEV_H

#include "capture.h"
#include "rkisp1.h"
#include "isp_params.h"
#include "isp_stats.h"

#define DRIVER_NAME "rkisp1"
#define ISP_VDEV_NAME DRIVER_NAME  "_ispdev"
#define SP_VDEV_NAME DRIVER_NAME   "_selfpath"
#define MP_VDEV_NAME DRIVER_NAME   "_mainpath"
#define DMA_VDEV_NAME DRIVER_NAME  "_dmapath"

#define GRP_ID_SENSOR			BIT(0)
#define GRP_ID_MIPIPHY			BIT(1)
#define GRP_ID_ISP			BIT(2)
#define GRP_ID_ISP_MP			BIT(3)
#define GRP_ID_ISP_SP			BIT(4)

#define RKISP1_MAX_BUS_CLK	8
#define RKISP1_MAX_SENSOR	2
#define RKISP1_MAX_PIPELINE	4

enum rkisp1_isp_ver {
	ISP_V10 = 0,
	ISP_V11,
	ISP_V12,
	ISP_V13
};

/*
 * struct rkisp1_pipeline - An ISP hardware pipeline
 *
 * Capture device call other devices via pipeline
 *
 * @num_subdevs: number of linked subdevs
 * @power_cnt: pipeline power count
 * @stream_cnt: stream power count
 */
struct rkisp1_pipeline {
	struct media_pipeline pipe;
	int num_subdevs;
	atomic_t power_cnt;
	atomic_t stream_cnt;
	struct v4l2_subdev *subdevs[RKISP1_MAX_PIPELINE];
	int (*open)(struct rkisp1_pipeline *p,
		    struct media_entity *me, bool prepare);
	int (*close)(struct rkisp1_pipeline *p);
	int (*set_stream)(struct rkisp1_pipeline *p, bool on);
};

/*
 * struct rkisp1_sensor_info - Sensor infomations
 * @mbus: media bus configuration
 */
struct rkisp1_sensor_info {
	struct v4l2_subdev *sd;
	struct v4l2_mbus_config mbus;
};

/*
 * struct rkisp1_device - ISP platform device
 * @base_addr: base register address
 * @active_sensor: sensor in-use, set when streaming on
 * @isp_sdev: ISP sub-device
 * @rkisp1_stream: capture video device
 * @stats_vdev: ISP statistics output device
 * @params_vdev: ISP input parameters device
 */
struct rkisp1_device {
	void __iomem *base_addr;
	int irq;
	struct device *dev;
	struct clk *clks[RKISP1_MAX_BUS_CLK];
	int num_clks;
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct media_device media_dev;
	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *subdevs[RKISP1_SD_MAX];
	struct rkisp1_sensor_info *active_sensor;
	struct rkisp1_sensor_info sensors[RKISP1_MAX_SENSOR];
	int num_sensors;
	struct rkisp1_isp_subdev isp_sdev;
	struct rkisp1_stream stream[RKISP1_MAX_STREAM];
	struct rkisp1_isp_stats_vdev stats_vdev;
	struct rkisp1_isp_params_vdev params_vdev;
	struct rkisp1_pipeline pipe;
	struct vb2_alloc_ctx *alloc_ctx;
	struct iommu_domain *domain;
	enum rkisp1_isp_ver isp_ver;
	const unsigned int *clk_rate_tbl;
	int num_clk_rate_tbl;
	atomic_t open_cnt;
};

#endif
