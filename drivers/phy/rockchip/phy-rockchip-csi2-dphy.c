// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip MIPI CSI2 DPHY driver
 *
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>
#include <linux/phy/phy.h>
#include "phy-rockchip-csi2-dphy-common.h"
#include "phy-rockchip-samsung-dcphy.h"

static struct rkmodule_csi_dphy_param rk3588_dcphy_param = {
	.vendor = PHY_VENDOR_SAMSUNG,
	.lp_vol_ref = 3,
	.lp_hys_sw = {3, 0, 0, 0},
	.lp_escclk_pol_sel = {1, 0, 0, 0},
	.skew_data_cal_clk = {0, 3, 3, 3},
	.clk_hs_term_sel = 2,
	.data_hs_term_sel = {2, 2, 2, 2},
	.reserved = {0},
};

struct sensor_async_subdev {
	struct v4l2_async_subdev asd;
	struct v4l2_mbus_config mbus;
	int lanes;
};

static LIST_HEAD(csi2dphy_device_list);

static inline struct csi2_dphy *to_csi2_dphy(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct csi2_dphy, sd);
}

static struct v4l2_subdev *get_remote_sensor(struct v4l2_subdev *sd)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;
	struct csi2_dphy *dphy = to_csi2_dphy(sd);

	if (dphy->num_sensors == 0)
		return NULL;
	local = &sd->entity.pads[CSI2_DPHY_RX_PAD_SINK];
	remote = media_entity_remote_pad(local);
	if (!remote) {
		v4l2_warn(sd, "No link between dphy and sensor\n");
		return NULL;
	}

	sensor_me = media_entity_remote_pad(local)->entity;
	return media_entity_to_v4l2_subdev(sensor_me);
}

static struct csi2_sensor *sd_to_sensor(struct csi2_dphy *dphy,
					   struct v4l2_subdev *sd)
{
	int i;

	for (i = 0; i < dphy->num_sensors; ++i)
		if (dphy->sensors[i].sd == sd)
			return &dphy->sensors[i];

	return NULL;
}

static int csi2_dphy_get_sensor_data_rate(struct v4l2_subdev *sd)
{
	struct csi2_dphy *dphy = to_csi2_dphy(sd);
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct v4l2_ctrl *link_freq;
	struct v4l2_querymenu qm = { .id = V4L2_CID_LINK_FREQ, };
	int ret = 0;

	if (!sensor_sd)
		return -ENODEV;

	link_freq = v4l2_ctrl_find(sensor_sd->ctrl_handler, V4L2_CID_LINK_FREQ);
	if (!link_freq) {
		v4l2_warn(sd, "No pixel rate control in subdev\n");
		return -EPIPE;
	}

	qm.index = v4l2_ctrl_g_ctrl(link_freq);
	ret = v4l2_querymenu(sensor_sd->ctrl_handler, &qm);
	if (ret < 0) {
		v4l2_err(sd, "Failed to get menu item\n");
		return ret;
	}

	if (!qm.value) {
		v4l2_err(sd, "Invalid link_freq\n");
		return -EINVAL;
	}
	dphy->data_rate_mbps = qm.value * 2;
	do_div(dphy->data_rate_mbps, 1000 * 1000);
	v4l2_info(sd, "dphy%d, data_rate_mbps %lld\n",
		  dphy->phy_index, dphy->data_rate_mbps);
	return 0;
}

static int rockchip_csi2_dphy_attach_hw(struct csi2_dphy *dphy, int csi_idx, int index)
{
	struct csi2_dphy_hw *dphy_hw;
	struct samsung_mipi_dcphy *dcphy_hw;
	struct v4l2_subdev *sensor_sd = get_remote_sensor(&dphy->sd);
	struct csi2_sensor *sensor = NULL;
	int lanes = 2;

	if (sensor_sd) {
		sensor = sd_to_sensor(dphy, sensor_sd);
		lanes = sensor->lanes;
	}

	if (dphy->drv_data->chip_id == CHIP_ID_RK3568 ||
	    dphy->drv_data->chip_id == CHIP_ID_RV1106) {
		dphy_hw = dphy->dphy_hw_group[0];
		mutex_lock(&dphy_hw->mutex);
		dphy_hw->dphy_dev[dphy_hw->dphy_dev_num] = dphy;
		dphy_hw->dphy_dev_num++;
		switch (dphy->phy_index) {
		case 0:
			dphy->lane_mode = PHY_FULL_MODE;
			dphy_hw->lane_mode = LANE_MODE_FULL;
			break;
		case 1:
			dphy->lane_mode = PHY_SPLIT_01;
			dphy_hw->lane_mode = LANE_MODE_SPLIT;
			break;
		case 2:
			dphy->lane_mode = PHY_SPLIT_23;
			dphy_hw->lane_mode = LANE_MODE_SPLIT;
			break;
		default:
			dphy->lane_mode = PHY_FULL_MODE;
			dphy_hw->lane_mode = LANE_MODE_FULL;
			break;
		}
		dphy->dphy_hw = dphy_hw;
		dphy->phy_hw[index] = (void *)dphy_hw;
		dphy->csi_info.dphy_vendor[index] = PHY_VENDOR_INNO;
		mutex_unlock(&dphy_hw->mutex);
	} else if (dphy->drv_data->chip_id == CHIP_ID_RK3588) {
		if (csi_idx < 2) {
			dcphy_hw = dphy->samsung_phy_group[csi_idx];
			mutex_lock(&dcphy_hw->mutex);
			dcphy_hw->dphy_dev[dcphy_hw->dphy_dev_num] = dphy;
			dcphy_hw->dphy_dev_num++;
			mutex_unlock(&dcphy_hw->mutex);
			dphy->samsung_phy = dcphy_hw;
			dphy->phy_hw[index] = (void *)dcphy_hw;
			dphy->dphy_param = rk3588_dcphy_param;
			dphy->csi_info.dphy_vendor[index] = PHY_VENDOR_SAMSUNG;
		} else {
			dphy_hw = dphy->dphy_hw_group[(csi_idx - 2) / 2];
			mutex_lock(&dphy_hw->mutex);
			if (csi_idx == 2 || csi_idx == 4) {
				if (lanes == 4) {
					dphy->lane_mode = PHY_FULL_MODE;
					dphy_hw->lane_mode = LANE_MODE_FULL;
					if (csi_idx == 2)
						dphy->phy_index = 0;
					else
						dphy->phy_index = 3;
				} else {
					dphy->lane_mode = PHY_SPLIT_01;
					dphy_hw->lane_mode = LANE_MODE_SPLIT;
					if (csi_idx == 2)
						dphy->phy_index = 1;
					else
						dphy->phy_index = 4;
				}
			} else if (csi_idx == 3 || csi_idx == 5) {
				if (lanes == 4) {
					dev_info(dphy->dev, "%s csi host%d only support PHY_SPLIT_23\n",
						 __func__, csi_idx);
					mutex_unlock(&dphy_hw->mutex);
					return -EINVAL;
				}
				dphy->lane_mode = PHY_SPLIT_23;
				dphy_hw->lane_mode = LANE_MODE_SPLIT;
				if (csi_idx == 3)
					dphy->phy_index = 2;
				else
					dphy->phy_index = 5;
			}
			dphy_hw->dphy_dev[dphy_hw->dphy_dev_num] = dphy;
			dphy_hw->dphy_dev_num++;
			dphy->dphy_hw = dphy_hw;
			dphy->phy_hw[index] = (void *)dphy_hw;
			dphy->csi_info.dphy_vendor[index] = PHY_VENDOR_INNO;
			mutex_unlock(&dphy_hw->mutex);
		}
	} else {
		dphy_hw = dphy->dphy_hw_group[csi_idx / 2];
		mutex_lock(&dphy_hw->mutex);
		if (csi_idx == 0 || csi_idx == 2) {
			if (lanes == 4) {
				dphy->lane_mode = PHY_FULL_MODE;
				dphy_hw->lane_mode = LANE_MODE_FULL;
				if (csi_idx == 0)
					dphy->phy_index = 0;
				else
					dphy->phy_index = 3;
			} else {
				dphy->lane_mode = PHY_SPLIT_01;
				dphy_hw->lane_mode = LANE_MODE_SPLIT;
				if (csi_idx == 0)
					dphy->phy_index = 1;
				else
					dphy->phy_index = 4;
			}
		} else if (csi_idx == 1 || csi_idx == 3) {
			if (lanes == 4) {
				dev_info(dphy->dev, "%s csi host%d only support PHY_SPLIT_23\n",
					 __func__, csi_idx);
				mutex_unlock(&dphy_hw->mutex);
				return -EINVAL;
			}
			dphy->lane_mode = PHY_SPLIT_23;
			dphy_hw->lane_mode = LANE_MODE_SPLIT;
			if (csi_idx == 1)
				dphy->phy_index = 2;
			else
				dphy->phy_index = 5;
		} else {
			dev_info(dphy->dev, "%s error csi host%d\n",
				 __func__, csi_idx);
			mutex_unlock(&dphy_hw->mutex);
			return -EINVAL;
		}
		dphy_hw->dphy_dev[dphy_hw->dphy_dev_num] = dphy;
		dphy_hw->dphy_dev_num++;
		dphy->phy_hw[index] = (void *)dphy_hw;
		dphy->csi_info.dphy_vendor[index] = PHY_VENDOR_INNO;
		mutex_unlock(&dphy_hw->mutex);
	}

	return 0;
}

static void rockchip_csi2_samsung_phy_remove_dphy_dev(struct csi2_dphy *dphy,
						   struct samsung_mipi_dcphy *dcphy_hw)
{
	int i = 0;
	bool is_find_dev = false;
	struct csi2_dphy *csi2_dphy = NULL;

	for (i = 0; i < dcphy_hw->dphy_dev_num; i++) {
		csi2_dphy = dcphy_hw->dphy_dev[i];
		if (csi2_dphy &&
		    csi2_dphy->phy_index == dphy->phy_index)
			is_find_dev = true;
		if (is_find_dev) {
			if (i < dcphy_hw->dphy_dev_num - 1)
				dcphy_hw->dphy_dev[i] = dcphy_hw->dphy_dev[i + 1];
			else
				dcphy_hw->dphy_dev[i] = NULL;
		}
	}
	if (is_find_dev)
		dcphy_hw->dphy_dev_num--;
}

static void rockchip_csi2_inno_phy_remove_dphy_dev(struct csi2_dphy *dphy,
						   struct csi2_dphy_hw *dphy_hw)
{
	int i = 0;
	bool is_find_dev = false;
	struct csi2_dphy *csi2_dphy = NULL;

	for (i = 0; i < dphy_hw->dphy_dev_num; i++) {
		csi2_dphy = dphy_hw->dphy_dev[i];
		if (csi2_dphy &&
		    csi2_dphy->phy_index == dphy->phy_index)
			is_find_dev = true;
		if (is_find_dev) {
			if (i < dphy_hw->dphy_dev_num - 1)
				dphy_hw->dphy_dev[i] = dphy_hw->dphy_dev[i + 1];
			else
				dphy_hw->dphy_dev[i] = NULL;
		}
	}
	if (is_find_dev)
		dphy_hw->dphy_dev_num--;
}

static int rockchip_csi2_dphy_detach_hw(struct csi2_dphy *dphy, int csi_idx, int index)
{
	struct csi2_dphy_hw *dphy_hw = NULL;
	struct samsung_mipi_dcphy *dcphy_hw = NULL;

	if (dphy->drv_data->chip_id == CHIP_ID_RK3568 ||
	    dphy->drv_data->chip_id == CHIP_ID_RV1106) {
		dphy_hw = (struct csi2_dphy_hw *)dphy->phy_hw[index];
		if (!dphy_hw) {
			dev_err(dphy->dev, "%s csi_idx %d detach hw failed\n",
				__func__, csi_idx);
			return -EINVAL;
		}
		mutex_lock(&dphy_hw->mutex);
		rockchip_csi2_inno_phy_remove_dphy_dev(dphy, dphy_hw);
		mutex_unlock(&dphy_hw->mutex);
	} else if (dphy->drv_data->chip_id == CHIP_ID_RK3588) {
		if (csi_idx < 2) {
			dcphy_hw = (struct samsung_mipi_dcphy *)dphy->phy_hw[index];
			if (!dcphy_hw) {
				dev_err(dphy->dev, "%s csi_idx %d detach hw failed\n",
					__func__, csi_idx);
				return -EINVAL;
			}
			mutex_lock(&dcphy_hw->mutex);
			rockchip_csi2_samsung_phy_remove_dphy_dev(dphy, dcphy_hw);
			mutex_unlock(&dcphy_hw->mutex);
		} else {
			dphy_hw = (struct csi2_dphy_hw *)dphy->phy_hw[index];
			if (!dphy_hw) {
				dev_err(dphy->dev, "%s csi_idx %d detach hw failed\n",
					__func__, csi_idx);
				return -EINVAL;
			}
			mutex_lock(&dphy_hw->mutex);
			rockchip_csi2_inno_phy_remove_dphy_dev(dphy, dphy_hw);
			mutex_unlock(&dphy_hw->mutex);
		}
	} else {
		dphy_hw = (struct csi2_dphy_hw *)dphy->phy_hw[index];
		if (!dphy_hw) {
			dev_err(dphy->dev, "%s csi_idx %d detach hw failed\n",
				__func__, csi_idx);
			return -EINVAL;
		}
		mutex_lock(&dphy_hw->mutex);
		rockchip_csi2_inno_phy_remove_dphy_dev(dphy, dphy_hw);
		mutex_unlock(&dphy_hw->mutex);
	}

	return 0;
}

static int csi2_dphy_update_sensor_mbus(struct v4l2_subdev *sd)
{
	struct csi2_dphy *dphy = to_csi2_dphy(sd);
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct csi2_sensor *sensor;
	struct v4l2_mbus_config mbus;
	int ret = 0;

	if (!sensor_sd)
		return -ENODEV;
	sensor = sd_to_sensor(dphy, sensor_sd);
	if (!sensor)
		return -ENODEV;

	ret = v4l2_subdev_call(sensor_sd, pad, get_mbus_config, 0, &mbus);
	if (ret)
		return ret;

	sensor->mbus = mbus;
	switch (mbus.flags & V4L2_MBUS_CSI2_LANES) {
	case V4L2_MBUS_CSI2_1_LANE:
		sensor->lanes = 1;
		break;
	case V4L2_MBUS_CSI2_2_LANE:
		sensor->lanes = 2;
		break;
	case V4L2_MBUS_CSI2_3_LANE:
		sensor->lanes = 3;
		break;
	case V4L2_MBUS_CSI2_4_LANE:
		sensor->lanes = 4;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int csi2_dphy_update_config(struct v4l2_subdev *sd)
{
	struct csi2_dphy *dphy = to_csi2_dphy(sd);
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct rkmodule_csi_dphy_param dphy_param;
	struct rkmodule_bus_config bus_config;
	int csi_idx = 0;
	int ret = 0;
	int i = 0;

	for (i = 0; i < dphy->csi_info.csi_num; i++) {
		if (dphy->drv_data->chip_id != CHIP_ID_RK3568 &&
		    dphy->drv_data->chip_id != CHIP_ID_RV1106) {
			csi_idx = dphy->csi_info.csi_idx[i];
			rockchip_csi2_dphy_attach_hw(dphy, csi_idx, i);
		}
		if (dphy->csi_info.dphy_vendor[i] == PHY_VENDOR_INNO) {
			ret = v4l2_subdev_call(sensor_sd, core, ioctl,
					       RKMODULE_GET_BUS_CONFIG, &bus_config);
			if (!ret) {
				dev_info(dphy->dev, "phy_mode %d,lane %d\n",
					bus_config.bus.phy_mode, bus_config.bus.lanes);
				if (bus_config.bus.phy_mode == PHY_FULL_MODE) {
					if (dphy->phy_index % 3 == 2) {
						dev_err(dphy->dev, "%s dphy%d only use for PHY_SPLIT_23\n",
							__func__, dphy->phy_index);
						return -EINVAL;
					}
					dphy->lane_mode = PHY_FULL_MODE;
					dphy->dphy_hw->lane_mode = LANE_MODE_FULL;
				} else if (bus_config.bus.phy_mode == PHY_SPLIT_01) {
					if (dphy->phy_index % 3 == 2) {
						dev_err(dphy->dev, "%s dphy%d only use for PHY_SPLIT_23\n",
							__func__, dphy->phy_index);
						return -EINVAL;
					}
					dphy->lane_mode = PHY_SPLIT_01;
					dphy->dphy_hw->lane_mode = LANE_MODE_SPLIT;
				} else if (bus_config.bus.phy_mode == PHY_SPLIT_23) {
					if (dphy->phy_index % 3 != 2) {
						dev_err(dphy->dev, "%s dphy%d not support PHY_SPLIT_23\n",
							__func__, dphy->phy_index);
						return -EINVAL;
					}
					dphy->lane_mode = PHY_SPLIT_23;
					dphy->dphy_hw->lane_mode = LANE_MODE_SPLIT;
				}
			}
		}
	}
	ret = v4l2_subdev_call(sensor_sd, core, ioctl,
			       RKMODULE_GET_CSI_DPHY_PARAM,
			       &dphy_param);
	if (!ret)
		dphy->dphy_param = dphy_param;
	return 0;
}

static int csi2_dphy_s_stream_start(struct v4l2_subdev *sd)
{
	struct csi2_dphy *dphy = to_csi2_dphy(sd);
	int i = 0;

	for (i = 0; i < dphy->csi_info.csi_num; i++) {
		if (dphy->csi_info.dphy_vendor[i] == PHY_VENDOR_SAMSUNG) {
			dphy->samsung_phy = (struct samsung_mipi_dcphy *)dphy->phy_hw[i];
			if (dphy->samsung_phy && dphy->samsung_phy->stream_on)
				dphy->samsung_phy->stream_on(dphy, sd);
		} else {
			dphy->dphy_hw = (struct csi2_dphy_hw *)dphy->phy_hw[i];
			if (dphy->dphy_hw && dphy->dphy_hw->stream_on)
				dphy->dphy_hw->stream_on(dphy, sd);
		}
	}

	dphy->is_streaming = true;

	return 0;
}

static int csi2_dphy_s_stream_stop(struct v4l2_subdev *sd)
{
	struct csi2_dphy *dphy = to_csi2_dphy(sd);
	int i = 0;

	for (i = 0; i < dphy->csi_info.csi_num; i++) {
		if (dphy->csi_info.dphy_vendor[i] == PHY_VENDOR_SAMSUNG) {
			dphy->samsung_phy = (struct samsung_mipi_dcphy *)dphy->phy_hw[i];
			if (dphy->samsung_phy && dphy->samsung_phy->stream_off)
				dphy->samsung_phy->stream_off(dphy, sd);
		} else {
			dphy->dphy_hw = (struct csi2_dphy_hw *)dphy->phy_hw[i];
			if (dphy->dphy_hw && dphy->dphy_hw->stream_off)
				dphy->dphy_hw->stream_off(dphy, sd);
		}
		if (dphy->drv_data->chip_id != CHIP_ID_RK3568 &&
		    dphy->drv_data->chip_id != CHIP_ID_RV1106)
			rockchip_csi2_dphy_detach_hw(dphy, dphy->csi_info.csi_idx[i], i);
	}

	dphy->is_streaming = false;

	dev_info(dphy->dev, "%s stream stop, dphy%d\n",
		 __func__, dphy->phy_index);

	return 0;
}

static int csi2_dphy_enable_clk(struct csi2_dphy *dphy)
{
	struct csi2_dphy_hw *hw = NULL;
	struct samsung_mipi_dcphy *samsung_phy = NULL;
	int ret;
	int i = 0;

	for (i = 0; i < dphy->csi_info.csi_num; i++) {
		if (dphy->csi_info.dphy_vendor[i] == PHY_VENDOR_SAMSUNG) {
			samsung_phy = (struct samsung_mipi_dcphy *)dphy->phy_hw[i];
			if (samsung_phy)
				clk_prepare_enable(samsung_phy->pclk);
		} else {
			hw = (struct csi2_dphy_hw *)dphy->phy_hw[i];
			if (hw) {
				ret = clk_bulk_prepare_enable(hw->num_clks, hw->clks_bulk);
				if (ret) {
					dev_err(hw->dev, "failed to enable clks\n");
					return ret;
				}
			}
		}
	}
	return 0;
}

static void csi2_dphy_disable_clk(struct csi2_dphy *dphy)
{
	struct csi2_dphy_hw *hw = NULL;
	struct samsung_mipi_dcphy *samsung_phy = NULL;
	int i = 0;

	for (i = 0; i < dphy->csi_info.csi_num; i++) {
		if (dphy->csi_info.dphy_vendor[i] == PHY_VENDOR_SAMSUNG) {
			samsung_phy = (struct samsung_mipi_dcphy *)dphy->phy_hw[i];
			if (samsung_phy)
				clk_disable_unprepare(samsung_phy->pclk);
		} else {
			hw = (struct csi2_dphy_hw *)dphy->phy_hw[i];
			if (hw)
				clk_bulk_disable_unprepare(hw->num_clks, hw->clks_bulk);
		}
	}
}

static int csi2_dphy_s_stream(struct v4l2_subdev *sd, int on)
{
	struct csi2_dphy *dphy = to_csi2_dphy(sd);
	int ret = 0;

	mutex_lock(&dphy->mutex);
	if (on) {
		if (dphy->is_streaming) {
			mutex_unlock(&dphy->mutex);
			return 0;
		}

		ret = csi2_dphy_get_sensor_data_rate(sd);
		if (ret < 0) {
			mutex_unlock(&dphy->mutex);
			return ret;
		}

		csi2_dphy_update_sensor_mbus(sd);
		ret = csi2_dphy_update_config(sd);
		if (ret < 0) {
			mutex_unlock(&dphy->mutex);
			return ret;
		}

		ret = csi2_dphy_enable_clk(dphy);
		if (ret) {
			mutex_unlock(&dphy->mutex);
			return ret;
		}
		ret = csi2_dphy_s_stream_start(sd);
	} else {
		if (!dphy->is_streaming) {
			mutex_unlock(&dphy->mutex);
			return 0;
		}
		ret = csi2_dphy_s_stream_stop(sd);
		csi2_dphy_disable_clk(dphy);
	}
	mutex_unlock(&dphy->mutex);

	dev_info(dphy->dev, "%s stream on:%d, dphy%d, ret %d\n",
		 __func__, on, dphy->phy_index, ret);

	return ret;
}

static int csi2_dphy_g_frame_interval(struct v4l2_subdev *sd,
					    struct v4l2_subdev_frame_interval *fi)
{
	struct v4l2_subdev *sensor = get_remote_sensor(sd);

	if (sensor)
		return v4l2_subdev_call(sensor, video, g_frame_interval, fi);

	return -EINVAL;
}

static int csi2_dphy_g_mbus_config(struct v4l2_subdev *sd,
				   unsigned int pad_id,
				   struct v4l2_mbus_config *config)
{
	struct csi2_dphy *dphy = to_csi2_dphy(sd);
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct csi2_sensor *sensor;

	if (!sensor_sd)
		return -ENODEV;
	sensor = sd_to_sensor(dphy, sensor_sd);
	if (!sensor)
		return -ENODEV;
	csi2_dphy_update_sensor_mbus(sd);
	*config = sensor->mbus;

	return 0;
}

static int csi2_dphy_s_power(struct v4l2_subdev *sd, int on)
{
	struct csi2_dphy *dphy = to_csi2_dphy(sd);

	if (on)
		return pm_runtime_get_sync(dphy->dev);
	else
		return pm_runtime_put(dphy->dev);
}

static __maybe_unused int csi2_dphy_runtime_suspend(struct device *dev)
{
	struct media_entity *me = dev_get_drvdata(dev);
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(me);
	struct csi2_dphy *dphy = to_csi2_dphy(sd);

	if (dphy->is_streaming) {
		csi2_dphy_s_stream(sd, 0);
		dphy->is_streaming = false;
	}

	return 0;
}

static __maybe_unused int csi2_dphy_runtime_resume(struct device *dev)
{
	return 0;
}

/* dphy accepts all fmt/size from sensor */
static int csi2_dphy_get_set_fmt(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_format *fmt)
{
	struct csi2_dphy *dphy = to_csi2_dphy(sd);
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct csi2_sensor *sensor;
	int ret;
	/*
	 * Do not allow format changes and just relay whatever
	 * set currently in the sensor.
	 */
	if (!sensor_sd)
		return -ENODEV;
	sensor = sd_to_sensor(dphy, sensor_sd);
	if (!sensor)
		return -ENODEV;
	ret = v4l2_subdev_call(sensor_sd, pad, get_fmt, NULL, fmt);
	if (!ret && fmt->pad == 0 && fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		sensor->format = fmt->format;
	return ret;
}

static int csi2_dphy_get_selection(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_selection *sel)
{
	struct v4l2_subdev *sensor = get_remote_sensor(sd);

	return v4l2_subdev_call(sensor, pad, get_selection, NULL, sel);
}

static long rkcif_csi2_dphy_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct csi2_dphy *dphy = to_csi2_dphy(sd);
	long ret = 0;
	int i = 0;
	int on = 0;

	switch (cmd) {
	case RKCIF_CMD_SET_CSI_IDX:
		if (dphy->drv_data->chip_id != CHIP_ID_RK3568 &&
		    dphy->drv_data->chip_id != CHIP_ID_RV1106)
			dphy->csi_info = *((struct rkcif_csi_info *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		for (i = 0; i < dphy->csi_info.csi_num; i++) {
			if (dphy->csi_info.dphy_vendor[i] == PHY_VENDOR_INNO) {
				dphy->dphy_hw = (struct csi2_dphy_hw *)dphy->phy_hw[i];
				if (!dphy->dphy_hw ||
				    !dphy->dphy_hw->quick_stream_off ||
				    !dphy->dphy_hw->quick_stream_on) {
					ret = -EINVAL;
					break;
				}
				on = *(int *)arg;
				if (on)
					dphy->dphy_hw->quick_stream_on(dphy, sd);
				else
					dphy->dphy_hw->quick_stream_off(dphy, sd);
			}
		}
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long rkcif_csi2_dphy_compat_ioctl32(struct v4l2_subdev *sd,
				      unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkcif_csi_info csi_info = {0};
	long ret;

	switch (cmd) {
	case RKCIF_CMD_SET_CSI_IDX:
		if (copy_from_user(&csi_info, up, sizeof(struct rkcif_csi_info)))
			return -EFAULT;

		ret = rkcif_csi2_dphy_ioctl(sd, cmd, &csi_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static const struct v4l2_subdev_core_ops csi2_dphy_core_ops = {
	.s_power = csi2_dphy_s_power,
	.ioctl = rkcif_csi2_dphy_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = rkcif_csi2_dphy_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops csi2_dphy_video_ops = {
	.g_frame_interval = csi2_dphy_g_frame_interval,
	.s_stream = csi2_dphy_s_stream,
};

static const struct v4l2_subdev_pad_ops csi2_dphy_subdev_pad_ops = {
	.set_fmt = csi2_dphy_get_set_fmt,
	.get_fmt = csi2_dphy_get_set_fmt,
	.get_selection = csi2_dphy_get_selection,
	.get_mbus_config = csi2_dphy_g_mbus_config,
};

static const struct v4l2_subdev_ops csi2_dphy_subdev_ops = {
	.core = &csi2_dphy_core_ops,
	.video = &csi2_dphy_video_ops,
	.pad = &csi2_dphy_subdev_pad_ops,
};

/* The .bound() notifier callback when a match is found */
static int
rockchip_csi2_dphy_notifier_bound(struct v4l2_async_notifier *notifier,
					   struct v4l2_subdev *sd,
					   struct v4l2_async_subdev *asd)
{
	struct csi2_dphy *dphy = container_of(notifier,
					      struct csi2_dphy,
					      notifier);
	struct sensor_async_subdev *s_asd = container_of(asd,
					struct sensor_async_subdev, asd);
	struct csi2_sensor *sensor;
	unsigned int pad, ret;

	if (dphy->num_sensors == ARRAY_SIZE(dphy->sensors))
		return -EBUSY;

	sensor = &dphy->sensors[dphy->num_sensors++];
	sensor->lanes = s_asd->lanes;
	sensor->mbus = s_asd->mbus;
	sensor->sd = sd;

	dev_info(dphy->dev, "dphy%d matches %s:bus type %d\n",
		 dphy->phy_index, sd->name, s_asd->mbus.type);

	for (pad = 0; pad < sensor->sd->entity.num_pads; pad++)
		if (sensor->sd->entity.pads[pad].flags & MEDIA_PAD_FL_SOURCE)
			break;

	if (pad == sensor->sd->entity.num_pads) {
		dev_err(dphy->dev,
			"failed to find src pad for %s\n",
			sensor->sd->name);

		return -ENXIO;
	}

	ret = media_create_pad_link(
			&sensor->sd->entity, pad,
			&dphy->sd.entity, CSI2_DPHY_RX_PAD_SINK,
			dphy->num_sensors != 1 ? 0 : MEDIA_LNK_FL_ENABLED);
	if (ret) {
		dev_err(dphy->dev,
			"failed to create link for %s\n",
			sensor->sd->name);
		return ret;
	}

	return 0;
}

/* The .unbind callback */
static void
rockchip_csi2_dphy_notifier_unbind(struct v4l2_async_notifier *notifier,
				  struct v4l2_subdev *sd,
				  struct v4l2_async_subdev *asd)
{
	struct csi2_dphy *dphy = container_of(notifier,
						  struct csi2_dphy,
						  notifier);
	struct csi2_sensor *sensor = sd_to_sensor(dphy, sd);

	if (sensor)
		sensor->sd = NULL;
}

static const struct
v4l2_async_notifier_operations rockchip_csi2_dphy_async_ops = {
	.bound = rockchip_csi2_dphy_notifier_bound,
	.unbind = rockchip_csi2_dphy_notifier_unbind,
};

static int rockchip_csi2_dphy_fwnode_parse(struct device *dev,
					  struct v4l2_fwnode_endpoint *vep,
					  struct v4l2_async_subdev *asd)
{
	struct sensor_async_subdev *s_asd =
			container_of(asd, struct sensor_async_subdev, asd);
	struct v4l2_mbus_config *config = &s_asd->mbus;

	if (vep->base.port != 0) {
		dev_err(dev, "The PHY has only port 0\n");
		return -EINVAL;
	}

	if (vep->bus_type == V4L2_MBUS_CSI2_DPHY ||
	    vep->bus_type == V4L2_MBUS_CSI2_CPHY) {
		config->type = vep->bus_type;
		config->flags = vep->bus.mipi_csi2.flags;
		s_asd->lanes = vep->bus.mipi_csi2.num_data_lanes;
	} else if (vep->bus_type == V4L2_MBUS_CCP2) {
		config->type = V4L2_MBUS_CCP2;
		s_asd->lanes = vep->bus.mipi_csi1.data_lane;
	} else {
		dev_err(dev, "Only CSI2 type is currently supported\n");
		return -EINVAL;
	}

	switch (s_asd->lanes) {
	case 1:
		config->flags |= V4L2_MBUS_CSI2_1_LANE;
		break;
	case 2:
		config->flags |= V4L2_MBUS_CSI2_2_LANE;
		break;
	case 3:
		config->flags |= V4L2_MBUS_CSI2_3_LANE;
		break;
	case 4:
		config->flags |= V4L2_MBUS_CSI2_4_LANE;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rockchip_csi2dphy_media_init(struct csi2_dphy *dphy)
{
	int ret;

	dphy->pads[CSI2_DPHY_RX_PAD_SOURCE].flags =
		MEDIA_PAD_FL_SOURCE | MEDIA_PAD_FL_MUST_CONNECT;
	dphy->pads[CSI2_DPHY_RX_PAD_SINK].flags =
		MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;
	dphy->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	ret = media_entity_pads_init(&dphy->sd.entity,
				CSI2_DPHY_RX_PADS_NUM, dphy->pads);
	if (ret < 0)
		return ret;

	v4l2_async_notifier_init(&dphy->notifier);

	ret = v4l2_async_notifier_parse_fwnode_endpoints_by_port(
		dphy->dev, &dphy->notifier,
		sizeof(struct sensor_async_subdev), 0,
		rockchip_csi2_dphy_fwnode_parse);
	if (ret < 0)
		return ret;

	dphy->sd.subdev_notifier = &dphy->notifier;
	dphy->notifier.ops = &rockchip_csi2_dphy_async_ops;
	ret = v4l2_async_subdev_notifier_register(&dphy->sd, &dphy->notifier);
	if (ret) {
		dev_err(dphy->dev,
			"failed to register async notifier : %d\n", ret);
		v4l2_async_notifier_cleanup(&dphy->notifier);
		return ret;
	}

	return v4l2_async_register_subdev(&dphy->sd);
}

static struct dphy_drv_data rk3568_dphy_drv_data = {
	.dev_name = "csi2dphy",
	.chip_id = CHIP_ID_RK3568,
	.num_inno_phy = 1,
	.num_samsung_phy = 0,
};

static struct dphy_drv_data rk3588_dphy_drv_data = {
	.dev_name = "csi2dphy",
	.chip_id = CHIP_ID_RK3588,
	.num_inno_phy = 2,
	.num_samsung_phy = 2,
};

static struct dphy_drv_data rv1106_dphy_drv_data = {
	.dev_name = "csi2dphy",
	.chip_id = CHIP_ID_RV1106,
	.num_inno_phy = 1,
	.num_samsung_phy = 0,
};

static struct dphy_drv_data rk3562_dphy_drv_data = {
	.dev_name = "csi2dphy",
	.chip_id = CHIP_ID_RK3562,
	.num_inno_phy = 2,
	.num_samsung_phy = 0,
};

static const struct of_device_id rockchip_csi2_dphy_match_id[] = {
	{
		.compatible = "rockchip,rk3568-csi2-dphy",
		.data = &rk3568_dphy_drv_data,
	},
	{
		.compatible = "rockchip,rk3588-csi2-dphy",
		.data = &rk3588_dphy_drv_data,
	},
	{
		.compatible = "rockchip,rv1106-csi2-dphy",
		.data = &rv1106_dphy_drv_data,
	},
	{
		.compatible = "rockchip,rk3562-csi2-dphy",
		.data = &rk3562_dphy_drv_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_csi2_dphy_match_id);

static int rockchip_csi2_dphy_get_samsung_phy_hw(struct csi2_dphy *dphy)
{
	struct phy *dcphy;
	struct device *dev = dphy->dev;
	struct samsung_mipi_dcphy *dcphy_hw;
	char phy_name[32];
	int i = 0;
	int ret = 0;

	for (i = 0; i < dphy->drv_data->num_samsung_phy; i++) {
		sprintf(phy_name, "dcphy%d", i);
		dcphy = devm_phy_optional_get(dev, phy_name);
		if (IS_ERR(dcphy)) {
			ret = PTR_ERR(dcphy);
			dev_err(dphy->dev, "failed to get mipi dcphy: %d\n", ret);
			return ret;
		}
		dcphy_hw = phy_get_drvdata(dcphy);
		dphy->samsung_phy_group[i] = dcphy_hw;
	}
	return 0;
}

static int rockchip_csi2_dphy_get_inno_phy_hw(struct csi2_dphy *dphy)
{
	struct platform_device *plat_dev;
	struct device *dev = dphy->dev;
	struct csi2_dphy_hw *dphy_hw;
	struct device_node *np;
	int i = 0;

	for (i = 0; i < dphy->drv_data->num_inno_phy; i++) {
		np = of_parse_phandle(dev->of_node, "rockchip,hw", i);
		if (!np || !of_device_is_available(np)) {
			dev_err(dphy->dev,
				"failed to get dphy%d hw node\n", dphy->phy_index);
			return -ENODEV;
		}
		plat_dev = of_find_device_by_node(np);
		of_node_put(np);
		if (!plat_dev) {
			dev_err(dphy->dev,
				"failed to get dphy%d hw from node\n",
				dphy->phy_index);
			return -ENODEV;
		}
		dphy_hw = platform_get_drvdata(plat_dev);
		if (!dphy_hw) {
			dev_err(dphy->dev,
				"failed attach dphy%d hw\n",
				dphy->phy_index);
			return -EINVAL;
		}
		dphy->dphy_hw_group[i] = dphy_hw;
	}
	return 0;
}

static int rockchip_csi2_dphy_get_hw(struct csi2_dphy *dphy)
{
	int ret = 0;

	if (dphy->drv_data->chip_id == CHIP_ID_RK3588) {
		ret = rockchip_csi2_dphy_get_samsung_phy_hw(dphy);
		if (ret)
			return ret;
		ret = rockchip_csi2_dphy_get_inno_phy_hw(dphy);
	} else {
		ret = rockchip_csi2_dphy_get_inno_phy_hw(dphy);
	}
	return ret;
}

static int rockchip_csi2_dphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *of_id;
	struct csi2_dphy *csi2dphy;
	struct v4l2_subdev *sd;
	const struct dphy_drv_data *drv_data;
	int ret;

	csi2dphy = devm_kzalloc(dev, sizeof(*csi2dphy), GFP_KERNEL);
	if (!csi2dphy)
		return -ENOMEM;
	csi2dphy->dev = dev;

	of_id = of_match_device(rockchip_csi2_dphy_match_id, dev);
	if (!of_id)
		return -EINVAL;
	drv_data = of_id->data;
	csi2dphy->drv_data = drv_data;

	csi2dphy->phy_index = of_alias_get_id(dev->of_node, drv_data->dev_name);
	if (csi2dphy->phy_index < 0 || csi2dphy->phy_index >= PHY_MAX)
		csi2dphy->phy_index = 0;

	ret = rockchip_csi2_dphy_get_hw(csi2dphy);
	if (ret)
		return -EINVAL;
	if (csi2dphy->drv_data->chip_id == CHIP_ID_RK3568 ||
	    csi2dphy->drv_data->chip_id == CHIP_ID_RV1106) {
		csi2dphy->csi_info.csi_num = 1;
		csi2dphy->csi_info.dphy_vendor[0] = PHY_VENDOR_INNO;
		rockchip_csi2_dphy_attach_hw(csi2dphy, 0, 0);
	} else {
		csi2dphy->csi_info.csi_num = 0;
	}
	sd = &csi2dphy->sd;
	mutex_init(&csi2dphy->mutex);
	v4l2_subdev_init(sd, &csi2_dphy_subdev_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name),
		 "rockchip-csi2-dphy%d", csi2dphy->phy_index);
	sd->dev = dev;

	platform_set_drvdata(pdev, &sd->entity);

	ret = rockchip_csi2dphy_media_init(csi2dphy);
	if (ret < 0)
		goto detach_hw;

	pm_runtime_enable(&pdev->dev);

	dev_info(dev, "csi2 dphy%d probe successfully!\n", csi2dphy->phy_index);

	return 0;

detach_hw:
	mutex_destroy(&csi2dphy->mutex);
	return -EINVAL;
}

static int rockchip_csi2_dphy_remove(struct platform_device *pdev)
{
	struct media_entity *me = platform_get_drvdata(pdev);
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(me);
	struct csi2_dphy *dphy = to_csi2_dphy(sd);
	int i = 0;

	for (i = 0; i < dphy->csi_info.csi_num; i++)
		rockchip_csi2_dphy_detach_hw(dphy, dphy->csi_info.csi_idx[i], i);
	media_entity_cleanup(&sd->entity);

	pm_runtime_disable(&pdev->dev);
	mutex_destroy(&dphy->mutex);
	return 0;
}

static const struct dev_pm_ops rockchip_csi2_dphy_pm_ops = {
	SET_RUNTIME_PM_OPS(csi2_dphy_runtime_suspend,
			   csi2_dphy_runtime_resume, NULL)
};

struct platform_driver rockchip_csi2_dphy_driver = {
	.probe = rockchip_csi2_dphy_probe,
	.remove = rockchip_csi2_dphy_remove,
	.driver = {
		.name = "rockchip-csi2-dphy",
		.pm = &rockchip_csi2_dphy_pm_ops,
		.of_match_table = rockchip_csi2_dphy_match_id,
	},
};

int rockchip_csi2_dphy_init(void)
{
	return platform_driver_register(&rockchip_csi2_dphy_driver);
}

#if defined(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP) && !defined(CONFIG_INITCALL_ASYNC)
subsys_initcall(rockchip_csi2_dphy_init);
#else
#if !defined(CONFIG_VIDEO_REVERSE_IMAGE)
module_platform_driver(rockchip_csi2_dphy_driver);
#endif
#endif

MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("Rockchip MIPI CSI2 DPHY driver");
MODULE_LICENSE("GPL v2");
