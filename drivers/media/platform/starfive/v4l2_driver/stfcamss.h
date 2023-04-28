/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2021-2023 StarFive Technology Co., Ltd.
 *
 */

#ifndef STFCAMSS_H
#define STFCAMSS_H

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/reset.h>
#include <linux/clk.h>

enum sensor_type {
	SENSOR_VIN,
	/* need replace sensor */
	SENSOR_ISP,
};

enum subdev_type {
	VIN_DEV_TYPE,
	ISP_DEV_TYPE,
};

#include "stf_common.h"
#include "stf_dvp.h"
#include "stf_csi.h"
#include "stf_csiphy.h"
#include "stf_isp.h"
#include "stf_vin.h"

#define STF_PAD_SINK   0
#define STF_PAD_SRC    1
#define STF_PADS_NUM   2

#define STF_CAMSS_SKIP_ITI

enum port_num {
	DVP_SENSOR_PORT_NUMBER = 0,
	CSI2RX_SENSOR_PORT_NUMBER
};

enum stf_clk_num {
	STFCLK_APB_FUNC = 0,
	STFCLK_PCLK,
	STFCLK_SYS_CLK,
	STFCLK_WRAPPER_CLK_C,
	STFCLK_DVP_INV,
	STFCLK_AXIWR,
	STFCLK_MIPI_RX0_PXL,
	STFCLK_PIXEL_CLK_IF0,
	STFCLK_PIXEL_CLK_IF1,
	STFCLK_PIXEL_CLK_IF2,
	STFCLK_PIXEL_CLK_IF3,
	STFCLK_M31DPHY_CFGCLK_IN,
	STFCLK_M31DPHY_REFCLK_IN,
	STFCLK_M31DPHY_TXCLKESC_LAN0,
	STFCLK_ISPCORE_2X,
	STFCLK_ISP_AXI,
	STFCLK_NUM
};

enum stf_rst_num {
	STFRST_WRAPPER_P = 0,
	STFRST_WRAPPER_C,
	STFRST_PCLK,
	STFRST_SYS_CLK,
	STFRST_AXIRD,
	STFRST_AXIWR,
	STFRST_PIXEL_CLK_IF0,
	STFRST_PIXEL_CLK_IF1,
	STFRST_PIXEL_CLK_IF2,
	STFRST_PIXEL_CLK_IF3,
	STFRST_M31DPHY_HW,
	STFRST_M31DPHY_B09_ALWAYS_ON,
	STFRST_ISP_TOP_N,
	STFRST_ISP_TOP_AXI,
	STFRST_NUM
};

struct stfcamss {
	struct stf_vin_dev *vin;  // stfcamss phy res
	struct v4l2_device v4l2_dev;
	struct media_device media_dev;
	struct media_pipeline pipe;
	struct device *dev;
	struct stf_vin2_dev *vin_dev;  // subdev
	struct stf_dvp_dev *dvp_dev;   // subdev
	struct stf_csi_dev *csi_dev;   // subdev
	struct stf_csiphy_dev *csiphy_dev;   // subdev
	struct stf_isp_dev *isp_dev;   // subdev
	struct v4l2_async_notifier notifier;
	struct clk_bulk_data *sys_clk;
	int nclks;
	struct reset_control_bulk_data *sys_rst;
	int nrsts;
	struct regmap *stf_aon_syscon;
	uint32_t aon_gp_reg;
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_entry;
	struct dentry *vin_debugfs;
#endif
};

struct stfcamss_async_subdev {
	struct v4l2_async_subdev asd;  // must be first
	enum port_num port;
	struct {
		struct dvp_cfg dvp;
		struct csi2phy_cfg csiphy;
	} interface;
};

extern struct media_entity *stfcamss_find_sensor(struct media_entity *entity);

#endif /* STFCAMSS_H */
