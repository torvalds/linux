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

#ifndef _RKISP_DEV_H
#define _RKISP_DEV_H

#include "capture.h"
#include "csi.h"
#include "dmarx.h"
#include "bridge.h"
#include "rkisp.h"
#include "isp_params.h"
#include "isp_stats.h"
#include "isp_mipi_luma.h"

#define DRIVER_NAME "rkisp"
#define ISP_VDEV_NAME DRIVER_NAME  "_ispdev"
#define SP_VDEV_NAME DRIVER_NAME   "_selfpath"
#define MP_VDEV_NAME DRIVER_NAME   "_mainpath"
#define DMA_VDEV_NAME DRIVER_NAME  "_dmapath"
#define RAW_VDEV_NAME DRIVER_NAME  "_rawpath"
#define DMATX0_VDEV_NAME DRIVER_NAME "_rawwr0"
#define DMATX1_VDEV_NAME DRIVER_NAME "_rawwr1"
#define DMATX2_VDEV_NAME DRIVER_NAME "_rawwr2"
#define DMATX3_VDEV_NAME DRIVER_NAME "_rawwr3"
#define DMARX0_VDEV_NAME DRIVER_NAME "_rawrd0_m"
#define DMARX1_VDEV_NAME DRIVER_NAME "_rawrd1_l"
#define DMARX2_VDEV_NAME DRIVER_NAME "_rawrd2_s"

#define GRP_ID_SENSOR			BIT(0)
#define GRP_ID_MIPIPHY			BIT(1)
#define GRP_ID_ISP			BIT(2)
#define GRP_ID_ISP_MP			BIT(3)
#define GRP_ID_ISP_SP			BIT(4)
#define GRP_ID_ISP_DMARX		BIT(5)
#define GRP_ID_ISP_BRIDGE		BIT(6)
#define GRP_ID_CSI			BIT(7)

#define RKISP_MAX_BUS_CLK		8
#define RKISP_MAX_SENSOR		2
#define RKISP_MAX_PIPELINE		4

#define RKISP_MEDIA_BUS_FMT_MASK	0xF000
#define RKISP_MEDIA_BUS_FMT_BAYER	0x3000

#define RKISP_CONTI_ERR_MAX		50

/* ISP_V10_1 for only support MP */
enum rkisp_isp_ver {
	ISP_V10 = 0x00,
	ISP_V10_1 = 0x01,
	ISP_V11 = 0x10,
	ISP_V12 = 0x20,
	ISP_V13 = 0x30,
	ISP_V20 = 0x40,
};

enum rkisp_isp_state {
	ISP_FRAME_END = BIT(0),
	ISP_FRAME_IN = BIT(1),
	ISP_FRAME_VS = BIT(2),

	ISP_STOP = BIT(8),
	ISP_START = BIT(9),
	ISP_ERROR = BIT(10),
};

enum rkisp_isp_inp {
	INP_INVAL = 0,
	INP_RAWRD0 = BIT(0),
	INP_RAWRD1 = BIT(1),
	INP_RAWRD2 = BIT(2),
	INP_CSI = BIT(4),
	INP_DVP = BIT(5),
	INP_DMARX_ISP = BIT(6),
	INP_LVDS = BIT(7),
};

/*
 * struct rkisp_pipeline - An ISP hardware pipeline
 *
 * Capture device call other devices via pipeline
 *
 * @num_subdevs: number of linked subdevs
 * @power_cnt: pipeline power count
 * @stream_cnt: stream power count
 */
struct rkisp_pipeline {
	struct media_pipeline pipe;
	int num_subdevs;
	atomic_t power_cnt;
	atomic_t stream_cnt;
	struct v4l2_subdev *subdevs[RKISP_MAX_PIPELINE];
	int (*open)(struct rkisp_pipeline *p,
		    struct media_entity *me, bool prepare);
	int (*close)(struct rkisp_pipeline *p);
	int (*set_stream)(struct rkisp_pipeline *p, bool on);
};

/*
 * struct rkisp_sensor_info - Sensor infomations
 * @mbus: media bus configuration
 */
struct rkisp_sensor_info {
	struct v4l2_subdev *sd;
	struct v4l2_mbus_config mbus;
	struct v4l2_subdev_frame_interval fi;
	struct v4l2_subdev_format fmt[CSI_PAD_MAX - 1];
	struct v4l2_subdev_pad_config cfg;
};

/* struct rkisp_hdr - hdr configured
 * @op_mode: hdr optional mode
 * @esp_mode: hdr especial mode
 * @index: hdr dma index
 * @refcnt: open counter
 * @q_tx: dmatx buf list
 * @q_rx: dmarx buf list
 * @rx_cur_buf: rawrd current buf
 * @dummy_buf: hdr dma internal buf
 */
struct rkisp_hdr {
	u8 op_mode;
	u8 esp_mode;
	u8 index[HDR_DMA_MAX];
	atomic_t refcnt;
	struct v4l2_subdev *sensor;
	struct list_head q_tx[HDR_DMA_MAX];
	struct list_head q_rx[HDR_DMA_MAX];
	struct rkisp_dummy_buffer *rx_cur_buf[HDR_DMA_MAX];
	struct rkisp_dummy_buffer dummy_buf[HDR_DMA_MAX][HDR_MAX_DUMMY_BUF];
};

/*
 * struct rkisp_device - ISP platform device
 * @base_addr: base register address
 * @active_sensor: sensor in-use, set when streaming on
 * @isp_sdev: ISP sub-device
 * @cap_dev: image capture device
 * @stats_vdev: ISP statistics output device
 * @params_vdev: ISP input parameters device
 * @dmarx_dev: image input device
 * @csi_dev: mipi csi device
 * @br_dev: bridge of isp and ispp device
 */
struct rkisp_device {
	struct list_head list;
	struct regmap *grf;
	void __iomem *base_addr;
	int irq;
	struct device *dev;
	struct clk *clks[RKISP_MAX_BUS_CLK];
	int num_clks;
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct media_device media_dev;
	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *subdevs[RKISP_SD_MAX];
	struct rkisp_sensor_info *active_sensor;
	struct rkisp_sensor_info sensors[RKISP_MAX_SENSOR];
	int num_sensors;
	struct rkisp_isp_subdev isp_sdev;
	struct rkisp_capture_device cap_dev;
	struct rkisp_isp_stats_vdev stats_vdev;
	struct rkisp_isp_params_vdev params_vdev;
	struct rkisp_dmarx_device dmarx_dev;
	struct rkisp_csi_device csi_dev;
	struct rkisp_bridge_device br_dev;
	struct rkisp_luma_vdev luma_vdev;
	struct rkisp_pipeline pipe;
	struct iommu_domain *domain;
	enum rkisp_isp_ver isp_ver;
	const unsigned int *clk_rate_tbl;
	int num_clk_rate_tbl;
	struct rkisp_emd_data emd_data_fifo[RKISP_EMDDATA_FIFO_MAX];
	unsigned int emd_data_idx;
	unsigned int emd_vc;
	unsigned int emd_dt;
	int vs_irq;
	int mipi_irq;
	struct gpio_desc *vs_irq_gpio;
	struct rkisp_hdr hdr;
	enum rkisp_isp_state isp_state;
	unsigned int isp_err_cnt;
	unsigned int isp_inp;
	struct mutex apilock; /* mutex to serialize the calls of stream */
	struct mutex iqlock; /* mutex to serialize the calls of iq */
	wait_queue_head_t sync_onoff;

	const struct isp_match_data *match_data;
	struct platform_device *pdev;
	phys_addr_t resmem_pa;
	size_t resmem_size;
	bool is_thunderboot;
};

int rkisp_register_irq(struct rkisp_device *isp_dev);

#endif
