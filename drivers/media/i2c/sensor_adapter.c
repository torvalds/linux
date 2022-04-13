// SPDX-License-Identifier: GPL-2.0
/*
 * sensor driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 *
 */
//#define DEBUG
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <linux/pinctrl/consumer.h>
#include <linux/rk-preisp.h>
#include <linux/of_graph.h>
#include "../platform/rockchip/cif/rkcif-externel.h"

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x0)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define MIPI_FREQ_360M			360000000

#define SENSOR_XVCLK_FREQ_24M		24000000

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define SENSOR_NAME			"sensor"
#define MAX_SENSOR_NUM			8
#define MAX_MIPICLK_NUM			5

struct sensor_crop {
	bool is_enable;
	u32 top;
	u32 left;
	u32 width;
	u32 height;
};

struct sensor_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u64 mipi_freq;
	u32 mclk;
	u32 bpp;
	struct rkmodule_hdr_cfg hdr_cfg;
	u32 vc[PAD_MAX];
};

struct sensor {
	struct i2c_client	*client;
	struct clk		*clks[MAX_MIPICLK_NUM];
	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;

	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	struct sensor_mode	*cur_mode;
	struct rkmodule_bus_config bus_config;
	struct sensor_crop	crop;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	enum rkmodule_sync_mode sync_mode;
	u8			i2cdev;
	bool			is_link;
};

static struct sensor *g_sensor[MAX_SENSOR_NUM];
static u8 cam_idx;

static s64 link_freq_menu_items[] = {
	MIPI_FREQ_360M,
};

static const char * const mipi_clks[] = {
	"clk_mipi0",
	"clk_mipi1",
	"clk_mipi2",
	"clk_mipi3",
	"clk_mipi4",
};

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

#define to_sensor(sd) container_of(sd, struct sensor, subdev)

/*
 * The width and height must be configured to be
 * the same as the current output resolution of the sensor.
 * The input width of the isp needs to be 16 aligned.
 * The input height of the isp needs to be 8 aligned.
 * If the width or height does not meet the alignment rules,
 * you can configure the cropping parameters with the following function to
 * crop out the appropriate resolution.
 * struct v4l2_subdev_pad_ops {
 *	.get_selection
 * }
 */
static struct sensor_mode supported_modes[] = {
	{
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.width = 2688,
		.height = 1520,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.mipi_freq = MIPI_FREQ_360M,
		.bpp = 10,
		.mclk = SENSOR_XVCLK_FREQ_24M,
		.hdr_cfg = {
			.hdr_mode = NO_HDR,
			.esp = {
				.mode = HDR_NORMAL_VC,
			},
		},
	},
};

static int sensor_write_reg(struct i2c_client *client, u16 reg,
			    u32 reg_len, u32 val, u32 val_len)
{
	u32 buf_i, val_i;
	u8 buf[8];
	u8 *val_p;
	u8 *reg_p;
	__be32 val_be;
	__be32 reg_be;

	if (reg_len > 4 || val_len > 4)
		return -EINVAL;

	reg_be = cpu_to_be32(reg);
	reg_p = (u8 *)&reg_be;
	for (buf_i = 0; buf_i < reg_len; buf_i++)
		buf[buf_i] = reg_p[4 - reg_len + buf_i];

	val_be = cpu_to_be32(val);
	val_p = (u8 *)&val_be;
	buf_i = reg_len;
	val_i = 4 - val_len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, reg_len + val_len) != reg_len + val_len)
		return -EIO;

	return 0;
}

static int sensor_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct sensor *sensor = to_sensor(sd);

	mutex_lock(&sensor->mutex);

	//application set resolution
	sensor->cur_mode->bus_fmt = fmt->format.code;
	sensor->cur_mode->width = fmt->format.width;
	sensor->cur_mode->height = fmt->format.height;

	mutex_unlock(&sensor->mutex);

	return 0;
}

static int sensor_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct sensor *sensor = to_sensor(sd);
	const struct sensor_mode *mode = sensor->cur_mode;

	//vicap or other device to get resolution configuration
	mutex_lock(&sensor->mutex);
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = mode->bus_fmt;
	fmt->format.field = V4L2_FIELD_NONE;
	mutex_unlock(&sensor->mutex);

	return 0;
}

static int sensor_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct sensor *sensor = to_sensor(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = sensor->cur_mode->bus_fmt;

	return 0;
}

static int sensor_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index > 1)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int sensor_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct sensor *sensor = to_sensor(sd);
	const struct sensor_mode *mode = sensor->cur_mode;

	mutex_lock(&sensor->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&sensor->mutex);

	return 0;
}

static int sensor_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct sensor *sensor = to_sensor(sd);
	u32 val = 0;
	u32 lane_num = sensor->bus_config.bus.lanes;

	val = 1 << (lane_num - 1) |
	      V4L2_MBUS_CSI2_CHANNEL_0 |
	      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	config->type = sensor->bus_config.bus.bus_type;
	config->flags = val;

	return 0;
}

static void sensor_get_module_inf(struct sensor *sensor,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, SENSOR_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, sensor->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, sensor->len_name, sizeof(inf->base.lens));
}

int rkcam_sensor_enable_mclk(u8 i2cdev, u32 mclk_index, u32 mclk_rate)
{
	struct sensor *sensor;
	struct device *dev;
	int ret = 0;
	int i = 0;

	for (i = 0; i < MAX_SENSOR_NUM; i++) {
		sensor = g_sensor[i];
		if (sensor->module_index == i2cdev)
			break;
	}

	if (i == MAX_SENSOR_NUM)
		return -EINVAL;

	dev = &sensor->client->dev;

	ret = clk_set_rate(sensor->clks[mclk_index], mclk_rate);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate\n");
	if (clk_get_rate(sensor->clks[mclk_index]) != sensor->cur_mode->mclk)
		dev_warn(dev, "xvclk mismatched, %lu\n", clk_get_rate(sensor->clks[mclk_index]));
	ret = clk_prepare_enable(sensor->clks[mclk_index]);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	sensor->cur_mode->mclk = clk_get_rate(sensor->clks[mclk_index]);
	return 0;
}
EXPORT_SYMBOL(rkcam_sensor_enable_mclk);

int rkcam_sensor_disable_mclk(u8 i2cdev, u32 mclk_index)
{
	struct sensor *sensor;
	int i = 0;

	for (i = 0; i < MAX_SENSOR_NUM; i++) {
		sensor = g_sensor[i];
		if (sensor->module_index == i2cdev)
			break;
	}

	if (i == MAX_SENSOR_NUM)
		return -EINVAL;

	clk_disable_unprepare(sensor->clks[mclk_index]);
	return 0;
}
EXPORT_SYMBOL(rkcam_sensor_disable_mclk);

static int sensor_config_link_freq(struct sensor *sensor, s64 link_freq)
{
	u32 pixel_rate = 0;
	struct sensor_mode *mode = sensor->cur_mode;

	link_freq_menu_items[0] = link_freq;
	__v4l2_ctrl_modify_range(sensor->link_freq, 0, 0, 1, link_freq_menu_items[0]);
	mode->mipi_freq = link_freq;
	pixel_rate = (u32)mode->mipi_freq / mode->bpp * 2 *
		     sensor->bus_config.bus.lanes;
	__v4l2_ctrl_modify_range(sensor->pixel_rate, pixel_rate, pixel_rate, 1, pixel_rate);
	return 0;
}

enum rk_isp_bus_type_e {
	ISP_BUS_TYPE_I2C = 0,
	ISP_BUS_TYPE_SPI,
	ISP_BUS_TYPE_UNKNOWN,
};

struct rkcam_bus_callbakck_s {
	u32 (*prkcam_write_i2c_data)(u8 i2cdev, u8 dev_addr,
				     u32 reg_addr, u32 reg_bytes,
				     u32 data, u32 data_bytes);
	u32 (*prkcam_write_spi_data)(u32 spidev, u32 spi_csn,
				     u32 dev_addr, u32 dev_addr_bytes,
				     u32 reg_addr, u32 reg_addr_bytes,
				     u32 data, u32 data_bytes);
	u32 (*prkcam_s_stream)(u32 dev, bool on);
};

static struct rkcam_bus_callbakck_s g_rkcam_bus_callback[MAX_SENSOR_NUM];

static int rkcam_register_bus_callback(int sensor_id,
				 enum rk_isp_bus_type_e bus_type,
				 struct rkcam_bus_callbakck_s *bus_callbaclk)
{
	if (bus_type == ISP_BUS_TYPE_I2C) {
		g_rkcam_bus_callback[sensor_id].prkcam_write_i2c_data = bus_callbaclk->prkcam_write_i2c_data;
	} else if (bus_type == ISP_BUS_TYPE_SPI) {
		g_rkcam_bus_callback[sensor_id].prkcam_write_spi_data = bus_callbaclk->prkcam_write_spi_data;
	} else {
		dev_err(&g_sensor[sensor_id]->client->dev,
			"sensor[%d] error bus type %d\n", sensor_id, bus_type);
		return -EFAULT;
	}
	if (bus_callbaclk->prkcam_s_stream)
		g_rkcam_bus_callback[sensor_id].prkcam_s_stream = bus_callbaclk->prkcam_s_stream;

	return 0;
}

struct rkcam_export_func_s {
	int (*p_rkcam_register_bus_callback)(int sensor_id,
					     enum rk_isp_bus_type_e bus_type,
					     struct rkcam_bus_callbakck_s *bus_callbaclk);
};

struct rkcam_export_func_s g_rkcam_export_func = {
	.p_rkcam_register_bus_callback = rkcam_register_bus_callback,
};
EXPORT_SYMBOL(g_rkcam_export_func);


static void sensor_get_remote_dev(struct media_entity *sensor_entity,
				  struct video_device **video)
{
	struct media_graph graph;
	struct media_device *mdev = sensor_entity->graph_obj.mdev;
	struct media_entity *entity;
	int ret = 0;

	mutex_lock(&mdev->graph_mutex);
	ret = media_graph_walk_init(&graph, mdev);
	if (ret) {
		mutex_unlock(&mdev->graph_mutex);
		return;
	}

	media_graph_walk_start(&graph, sensor_entity);
	while ((entity = media_graph_walk_next(&graph))) {
		if (strcmp(entity->name, "stream_cif_mipi_id0") == 0)
			break;
	}
	mutex_unlock(&mdev->graph_mutex);
	media_graph_walk_cleanup(&graph);

	if (entity)
		*video = media_entity_to_video_device(entity);
	else
		*video = NULL;
}

static int sensor_sync_dev_pipeline(u8 dev_num)
{
	struct sensor *sensor = NULL;
	struct video_device *vdev = NULL;
	int i = 0;
	int disconnect_num = 0;
	int ret = 0;

	for (i = 0; i < cam_idx; i++) {
		sensor = g_sensor[i];
		if (!sensor)
			continue;
		if (!sensor->is_link) {
			sensor_get_remote_dev(&sensor->subdev.entity, &vdev);
			if (vdev != NULL) {
				rkcif_sditf_disconnect(vdev);
				disconnect_num++;
				dev_info(&sensor->client->dev, "cam%d disconnect with isp\n", sensor->module_index);
			}
		}
	}
	if (sensor == NULL) {
		ret = -EFAULT;
	} else if (dev_num != (cam_idx - disconnect_num)) {
		dev_err(&sensor->client->dev, "failed to sync i2cdev, dev_num not match\n");
		ret = -EINVAL;
	}
	return ret;
}

static long sensor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	void __user *up;
	struct sensor *sensor = to_sensor(sd);
	struct rkmodule_hdr_cfg *hdr;
	struct rkmodule_bus_config *bus_config;
	struct rkmodule_reg *reg_s;
	long ret = 0;
	s64 link_freq = 0;
	int i = 0;
	u32 *preg_addr = NULL;
	u32 *preg_value = NULL;
	u32 *preg_addr_bytes = NULL;
	u32 *preg_value_bytes = NULL;
	u32 lens = 0;
	u8 dev_num = 0;
	u32 stream = 0;
	u32 *sync_mode = NULL;
	struct rkmodule_mclk_data *mclk;
	struct rkmodule_dev_info *dev_info;
	struct rkmodule_csi_dphy_param *dphy_param;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		sensor_get_module_inf(sensor, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		*hdr = sensor->cur_mode->hdr_cfg;
		dev_info(&sensor->client->dev,
			 "sensor get hdr esp_mode %d, hdr_mode %d\n",
			 hdr->esp.mode,
			 hdr->hdr_mode);
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		sensor->cur_mode->hdr_cfg = *hdr;
		dev_info(&sensor->client->dev,
			 "sensor set hdr esp_mode %d, hdr_mode %d\n",
			 hdr->esp.mode,
			 hdr->hdr_mode);
		break;
	case RKMODULE_SET_LINK_FREQ:
		link_freq = *(s64 *)arg;
		ret = sensor_config_link_freq(sensor, link_freq);
		dev_info(&sensor->client->dev,
			 "sensor set link_freq %llu\n",
			 link_freq);
		break;
	case RKMODULE_SET_BUS_CONFIG:
		bus_config = (struct rkmodule_bus_config *)arg;
		sensor->bus_config = *bus_config;
		dev_info(&sensor->client->dev,
			 "sensor set bus config, phy_mode %d, lanes %d\n",
			 bus_config->bus.phy_mode, bus_config->bus.lanes);
		break;
	case RKMODULE_GET_BUS_CONFIG:
		bus_config = (struct rkmodule_bus_config *)arg;
		bus_config->bus.bus_type = sensor->bus_config.bus.bus_type;
		bus_config->bus.lanes = sensor->bus_config.bus.lanes;
		bus_config->bus.phy_mode = sensor->bus_config.bus.phy_mode;
		dev_info(&sensor->client->dev,
			 "sensor get bus config, phy_mode %d, lanes %d\n",
			 bus_config->bus.phy_mode, bus_config->bus.lanes);
		break;
	case RKMODULE_SET_REGISTER:
		reg_s = (struct rkmodule_reg *)arg;
		if (reg_s->num_regs == 0) {
			dev_err(&sensor->client->dev, "sensor reg array num %d\n", reg_s->num_regs);
			return -EINVAL;
		}

		dev_dbg(&sensor->client->dev, "sensor reg array num %d\n",
			 reg_s->num_regs);
		lens = sizeof(u32) * reg_s->num_regs;
		preg_addr = kzalloc(lens, GFP_KERNEL);
		if (!preg_addr)
			return -EFAULT;
		up = (void __user *)reg_s->preg_addr;
		ret = copy_from_user(preg_addr, up, lens);
		if (ret) {
			ret = -EFAULT;
			goto end_set_reg;
		}
		preg_value = kzalloc(lens, GFP_KERNEL);
		if (!preg_value) {
			ret = -EFAULT;
			goto end_set_reg;
		}
		up = (void __user *)reg_s->preg_value;
		ret = copy_from_user(preg_value, up, lens);
		if (ret) {
			ret = -EFAULT;
			goto end_set_reg;
		}
		preg_addr_bytes = kzalloc(lens, GFP_KERNEL);
		if (!preg_addr_bytes) {
			ret = -EFAULT;
			goto end_set_reg;
		}
		up = (void __user *)reg_s->preg_addr_bytes;
		ret = copy_from_user(preg_addr_bytes, up, lens);
		if (ret) {
			ret = -EFAULT;
			goto end_set_reg;
		}
		preg_value_bytes = kzalloc(lens, GFP_KERNEL);
		if (!preg_value_bytes) {
			ret = -EFAULT;
			goto end_set_reg;
		}
		up = (void __user *)reg_s->preg_value_bytes;
		ret = copy_from_user(preg_value_bytes, up, lens);
		if (ret) {
			ret = -EFAULT;
			goto end_set_reg;
		}
		for (i = 0; i < reg_s->num_regs; i++) {
			dev_dbg(&sensor->client->dev, "sensor reg 0x%x, reg_bytes %u, val 0x%x, val_bytes %u\n",
				preg_addr[i], preg_addr_bytes[i], preg_value[i], preg_value_bytes[i]);
			if (g_rkcam_bus_callback[sensor->i2cdev].prkcam_write_i2c_data) {
				ret = g_rkcam_bus_callback[sensor->i2cdev].prkcam_write_i2c_data(sensor->i2cdev,
					0,
					(u32)preg_addr[i], (u32)preg_addr_bytes[i],
					(u32)preg_value[i], (u32)preg_value_bytes[i]);
				if (ret)
					dev_err(&sensor->client->dev, "failed to write sensor reg\n");
			} else {
				ret = sensor_write_reg(sensor->client,
						       (u32)preg_addr[i],
						       (u32)preg_addr_bytes[i],
						       (u32)preg_value[i],
						       (u32)preg_value_bytes[i]);
				if (ret)
					dev_err(&sensor->client->dev, "failed to write sensor by sensor_write_reg\n");
			}
		}
end_set_reg:
		kfree(preg_addr);
		kfree(preg_value);
		kfree(preg_addr_bytes);
		kfree(preg_value_bytes);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		if (g_rkcam_bus_callback[sensor->i2cdev].prkcam_s_stream) {
			stream = *(u32 *)arg;
			ret = g_rkcam_bus_callback[sensor->i2cdev].prkcam_s_stream(sensor->i2cdev, !!stream);
			if (ret)
				dev_err(&sensor->client->dev, "failed to set quick stream\n");
			else
				dev_info(&sensor->client->dev, "success to set quick stream\n");
		} else {
			dev_err(&sensor->client->dev,
				"The callback function of sensor s_stream is not exist\n");
		}
		break;
	case RKMODULE_SYNC_I2CDEV:
		sensor->i2cdev = *(u8 *)arg;
		sensor->is_link = true;
		dev_info(&sensor->client->dev,
			 "sensor sync i2cdev, dev_index %d\n",
			 sensor->i2cdev);
		break;
	case RKMODULE_SYNC_I2CDEV_COMPLETE:
		dev_num = *(u8 *)arg;
		ret = sensor_sync_dev_pipeline(dev_num);
		dev_info(&sensor->client->dev,
			 "sensor sync i2cdev complete, dev_num %d\n",
			 dev_num);
		break;
	case RKMODULE_GET_SYNC_MODE:
		sync_mode = (u32 *)arg;
		*sync_mode = sensor->sync_mode;
		dev_info(&sensor->client->dev,
			 "sensor get sync_mode %d\n",
			 *sync_mode);
		break;
	case RKMODULE_SET_SYNC_MODE:
		sync_mode = (u32 *)arg;
		sensor->sync_mode = *sync_mode;
		dev_info(&sensor->client->dev,
			 "sensor set sync_mode %d\n",
			 *sync_mode);
		break;
	case RKMODULE_SET_MCLK:
		mclk = (struct rkmodule_mclk_data *)arg;
		if (mclk->enable)
			rkcam_sensor_enable_mclk(0, mclk->mclk_index, mclk->mclk_rate);
		else
			rkcam_sensor_disable_mclk(0, mclk->mclk_index);

		dev_info(&sensor->client->dev,
			 "sensor set mclk, enable %u, index %u, rate %u\n",
			 mclk->enable, mclk->mclk_index, mclk->mclk_rate);
		break;
	case RKMODULE_SET_DEV_INFO:
		dev_info = (struct rkmodule_dev_info *)arg;
		if (dev_info->i2c_dev.slave_addr)
			sensor->client->addr = dev_info->i2c_dev.slave_addr;
		dev_info(&sensor->client->dev,
			 "sensor set dev info ,slave addr 0x%x\n",
			 dev_info->i2c_dev.slave_addr);
		break;
	case RKMODULE_SET_CSI_DPHY_PARAM:
		dphy_param = (struct rkmodule_csi_dphy_param *)arg;
		if (dphy_param->vendor == rk3588_dcphy_param.vendor)
			rk3588_dcphy_param = *dphy_param;
		dev_dbg(&sensor->client->dev,
			"sensor set dphy param\n");
		break;
	case RKMODULE_GET_CSI_DPHY_PARAM:
		dphy_param = (struct rkmodule_csi_dphy_param *)arg;
		if (dphy_param->vendor == rk3588_dcphy_param.vendor)
			*dphy_param = rk3588_dcphy_param;
		dev_dbg(&sensor->client->dev,
			"sensor get dphy param\n");
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long sensor_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_hdr_cfg *hdr;
	struct rkmodule_bus_config *bus_config;
	struct rkmodule_reg *reg_s;
	long ret;
	u32 stream = 0;
	s64 link_freq = 0;
	u8 i2cdev = 0;
	u8 dev_num = 0;
	u32 *sync_mode = NULL;
	struct rkmodule_mclk_data *mclk;
	struct rkmodule_dev_info *dev_info;
	struct rkmodule_csi_dphy_param *dphy_param;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sensor_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sensor_ioctl(sd, cmd, hdr);
		if (!ret) {
			ret = copy_to_user(up, hdr, sizeof(*hdr));
			if (ret)
				ret = -EFAULT;
		}
		kfree(hdr);
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(hdr, up, sizeof(*hdr));
		if (!ret)
			ret = sensor_ioctl(sd, cmd, hdr);
		else
			ret = -EFAULT;
		kfree(hdr);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = sensor_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;

		break;
	case RKMODULE_SET_LINK_FREQ:
		ret = copy_from_user(&link_freq, up, sizeof(s64));
		if (!ret)
			ret = sensor_ioctl(sd, cmd, &link_freq);
		else
			ret = -EFAULT;
		break;
	case RKMODULE_GET_BUS_CONFIG:
		bus_config = kzalloc(sizeof(*bus_config), GFP_KERNEL);
		if (!bus_config) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sensor_ioctl(sd, cmd, bus_config);
		if (!ret) {
			ret = copy_to_user(up, bus_config, sizeof(*bus_config));
			if (ret)
				ret = -EFAULT;
		}
		kfree(bus_config);
		break;
	case RKMODULE_SET_BUS_CONFIG:
		bus_config = kzalloc(sizeof(*bus_config), GFP_KERNEL);
		if (!bus_config) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(bus_config, up, sizeof(*bus_config));
		if (!ret)
			ret = sensor_ioctl(sd, cmd, bus_config);
		else
			ret = -EFAULT;
		kfree(bus_config);
		break;
	case RKMODULE_SET_REGISTER:
		reg_s = kzalloc(sizeof(*reg_s), GFP_KERNEL);
		if (!reg_s) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(reg_s, up, sizeof(*reg_s));
		if (!ret) {
			ret = sensor_ioctl(sd, cmd, reg_s);
			kfree(reg_s);
		} else {
			kfree(reg_s);
			ret = -EFAULT;
		}
		break;
	case RKMODULE_SYNC_I2CDEV:
		ret = copy_from_user(&i2cdev, up, sizeof(u8));
		if (!ret)
			ret = sensor_ioctl(sd, cmd, &i2cdev);
		else
			ret = -EFAULT;

		break;
	case RKMODULE_SYNC_I2CDEV_COMPLETE:
		ret = copy_from_user(&dev_num, up, sizeof(u8));
		if (!ret)
			ret = sensor_ioctl(sd, cmd, &dev_num);
		else
			ret = -EFAULT;

		break;
	case RKMODULE_GET_SYNC_MODE:
		ret = sensor_ioctl(sd, cmd, &sync_mode);
		if (!ret) {
			ret = copy_to_user(up, &sync_mode, sizeof(u32));
			if (ret)
				ret = -EFAULT;
		}
		break;
	case RKMODULE_SET_SYNC_MODE:
		ret = copy_from_user(&sync_mode, up, sizeof(u32));
		if (!ret)
			ret = sensor_ioctl(sd, cmd, &sync_mode);
		else
			ret = -EFAULT;
		break;
	case RKMODULE_SET_MCLK:
		mclk = kzalloc(sizeof(*mclk), GFP_KERNEL);
		if (!mclk) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(mclk, up, sizeof(*mclk));
		if (!ret)
			ret = sensor_ioctl(sd, cmd, mclk);
		else
			ret = -EFAULT;
		kfree(mclk);
		break;
	case RKMODULE_SET_DEV_INFO:
		dev_info = kzalloc(sizeof(*dev_info), GFP_KERNEL);
		if (!dev_info) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(dev_info, up, sizeof(*dev_info));
		if (!ret)
			ret = sensor_ioctl(sd, cmd, dev_info);
		else
			ret = -EFAULT;
		kfree(dev_info);
		break;
	case RKMODULE_SET_CSI_DPHY_PARAM:
		dphy_param = kzalloc(sizeof(*dphy_param), GFP_KERNEL);
		if (!dphy_param) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(dphy_param, up, sizeof(*dphy_param));
		if (!ret)
			ret = sensor_ioctl(sd, cmd, dphy_param);
		else
			ret = -EFAULT;
		kfree(dphy_param);
		break;
	case RKMODULE_GET_CSI_DPHY_PARAM:
		dphy_param = kzalloc(sizeof(*dphy_param), GFP_KERNEL);
		if (!dphy_param) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sensor_ioctl(sd, cmd, dphy_param);
		if (!ret) {
			ret = copy_to_user(up, dphy_param, sizeof(*dphy_param));
			if (ret)
				ret = -EFAULT;
		}
		kfree(dphy_param);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __sensor_start_stream(struct sensor *sensor)
{
	/* user to write sensor setting or
	 * may control by aiq callback to set sensor setting by customer driver
	 */
	return 0;
}

static int __sensor_stop_stream(struct sensor *sensor)
{
	/* user to write sensor setting or
	 * may control by aiq callback to set sensor setting by customer driver
	 */
	return 0;
}

static int sensor_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sensor *sensor = to_sensor(sd);
	struct i2c_client *client = sensor->client;
	int ret = 0;

	mutex_lock(&sensor->mutex);
	on = !!on;
	if (on == sensor->streaming)
		goto unlock_and_return;

	if (on) {

		ret = __sensor_start_stream(sensor);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__sensor_stop_stream(sensor);
	}

	sensor->streaming = on;

unlock_and_return:
	mutex_unlock(&sensor->mutex);

	return ret;
}

static int sensor_s_power(struct v4l2_subdev *sd, int on)
{
	struct sensor *sensor = to_sensor(sd);
	struct i2c_client *client = sensor->client;
	int ret = 0;

	mutex_lock(&sensor->mutex);

	/* If the power state is not modified - no work to do. */
	if (sensor->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		sensor->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		sensor->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&sensor->mutex);

	return ret;
}

static int __sensor_power_on(struct sensor *sensor)
{

	//todo
	//call  sensor power on
	return 0;
}

static void __sensor_power_off(struct sensor *sensor)
{
	//todo
	//call sensor power off
}

static int sensor_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sensor *sensor = to_sensor(sd);

	return __sensor_power_on(sensor);
}

static int sensor_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sensor *sensor = to_sensor(sd);

	__sensor_power_off(sensor);

	return 0;
}

static int sensor_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct sensor *sensor = to_sensor(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		if (sensor->crop.is_enable &&
		    (sensor->crop.left + sensor->crop.width) <= sensor->cur_mode->width &&
		    (sensor->crop.top + sensor->crop.height) <= sensor->cur_mode->height) {
			sel->r.left = sensor->crop.left;
			sel->r.width = sensor->crop.width;
			sel->r.top = sensor->crop.top;
			sel->r.height = sensor->crop.height;
			dev_dbg(&sensor->client->dev,
				"%s left %d, width %d, top %d, height %d\n",
				__func__,
				sensor->crop.left, sensor->crop.width,
				sensor->crop.top, sensor->crop.height);
		} else {
			sel->r.left = 0;
			sel->r.width = sensor->cur_mode->width;
			sel->r.top = 0;
			sel->r.height = sensor->cur_mode->height;
		}
		return 0;
	}
	dev_err(&sensor->client->dev,
		"%s failed\n", __func__);
	return -EINVAL;
}

static int sensor_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct sensor *sensor = to_sensor(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sensor->crop.top = sel->r.top;
		sensor->crop.left = sel->r.left;
		sensor->crop.width = sel->r.width;
		sensor->crop.height = sel->r.height;
		sensor->crop.is_enable = true;
		dev_info(&sensor->client->dev,
			"%s left %d, width %d, top %d, height %d\n",
			__func__,
			sensor->crop.left, sensor->crop.width,
			sensor->crop.top, sensor->crop.height);
		return 0;
	}
	dev_err(&sensor->client->dev,
		"sensor_get_selection failed\n");
	return -EINVAL;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int sensor_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sensor *sensor = to_sensor(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct sensor_mode *def_mode = sensor->cur_mode;

	mutex_lock(&sensor->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&sensor->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int sensor_enum_frame_interval(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_frame_interval_enum *fie)
{
	struct sensor *sensor = to_sensor(sd);

	if (fie->index > 1)
		return -EINVAL;

	fie->code = sensor->cur_mode->bus_fmt;
	fie->width = sensor->cur_mode->width;
	fie->height = sensor->cur_mode->height;
	fie->interval = sensor->cur_mode->max_fps;
	fie->reserved[0] = sensor->cur_mode->hdr_cfg.hdr_mode;
	return 0;
}

static const struct dev_pm_ops sensor_pm_ops = {
	SET_RUNTIME_PM_OPS(sensor_runtime_suspend,
			   sensor_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops sensor_internal_ops = {
	.open = sensor_open,
};
#endif

static const struct v4l2_subdev_core_ops sensor_core_ops = {
	.s_power = sensor_s_power,
	.ioctl = sensor_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sensor_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sensor_video_ops = {
	.s_stream = sensor_s_stream,
	.g_frame_interval = sensor_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops sensor_pad_ops = {
	.enum_mbus_code = sensor_enum_mbus_code,
	.enum_frame_size = sensor_enum_frame_sizes,
	.enum_frame_interval = sensor_enum_frame_interval,
	.get_fmt = sensor_get_fmt,
	.set_fmt = sensor_set_fmt,
	.get_selection = sensor_get_selection,
	.set_selection = sensor_set_selection,
	.get_mbus_config = sensor_g_mbus_config,
};

static const struct v4l2_subdev_ops sensor_subdev_ops = {
	.core	= &sensor_core_ops,
	.video	= &sensor_video_ops,
	.pad	= &sensor_pad_ops,
};

static int sensor_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sensor *sensor = container_of(ctrl->handler,
					     struct sensor, ctrl_handler);
	struct i2c_client *client = sensor->client;
	int ret = 0;

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		//todo
		break;
	case V4L2_CID_VFLIP:
		//todo
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sensor_ctrl_ops = {
	.s_ctrl = sensor_set_ctrl,
};

static int sensor_initialize_controls(struct sensor *sensor)
{
	const struct sensor_mode *mode;
	struct v4l2_ctrl_handler *handler;
	u64 pixel_rate = 0;
	int ret;
	u32 h_blank = 0;
	u32 vblank_def = 0;

	handler = &sensor->ctrl_handler;
	mode = sensor->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 4);
	if (ret)
		return ret;
	handler->lock = &sensor->mutex;

	sensor->link_freq = v4l2_ctrl_new_int_menu(handler,
				NULL, V4L2_CID_LINK_FREQ,
				0, 0, link_freq_menu_items);
	pixel_rate = (u32)mode->mipi_freq / mode->bpp * 2 *
		     sensor->bus_config.bus.lanes;
	sensor->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
		V4L2_CID_PIXEL_RATE, 0, pixel_rate,
		1, pixel_rate);

	h_blank = 600;
	sensor->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (sensor->hblank)
		sensor->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = 100;
	sensor->vblank = v4l2_ctrl_new_std(handler, &sensor_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				vblank_def,
				1, vblank_def);

	v4l2_ctrl_new_std(handler, &sensor_ctrl_ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &sensor_ctrl_ops, V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&sensor->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	sensor->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int sensor_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct v4l2_subdev *sd;
	struct sensor *sensor;
	char facing[2];
	int ret;
	int i;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;
	g_sensor[cam_idx] = sensor;
	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &sensor->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &sensor->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &sensor->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &sensor->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}
	sensor->client = client;
	sensor->cur_mode = &supported_modes[0];
	for (i = 0; i < MAX_MIPICLK_NUM; i++) {
		struct clk *clk = devm_clk_get(dev, mipi_clks[i]);

		if (IS_ERR(clk)) {
			dev_err(dev, "failed to get %s\n", mipi_clks[i]);
			return PTR_ERR(clk);
		}
		sensor->clks[i] = clk;
	}

	sensor->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(sensor->pinctrl)) {
		sensor->pins_default =
			pinctrl_lookup_state(sensor->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(sensor->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		sensor->pins_sleep =
			pinctrl_lookup_state(sensor->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(sensor->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}
	mutex_init(&sensor->mutex);
	sensor->bus_config.bus.lanes = 2;
	sensor->bus_config.bus.bus_type = V4L2_MBUS_CSI2_DPHY;
	sensor->is_link = false;
	sensor->sync_mode = NO_SYNC_MODE;
	sensor->crop.is_enable = false;
	sd = &sensor->subdev;
	v4l2_i2c_subdev_init(sd, client, &sensor_subdev_ops);
	ret = sensor_initialize_controls(sensor);
	if (ret)
		goto err_destroy_mutex;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &sensor_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sensor->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(sensor->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 sensor->module_index, facing,
		 SENSOR_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);
	cam_idx++;

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__sensor_power_off(sensor);
err_destroy_mutex:
	mutex_destroy(&sensor->mutex);

	return ret;
}

static int sensor_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sensor *sensor = to_sensor(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
	mutex_destroy(&sensor->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__sensor_power_off(sensor);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id sensor_of_match[] = {
	{ .compatible = "sensor,adapter" },
	{},
};
MODULE_DEVICE_TABLE(of, sensor_of_match);
#endif

static const struct i2c_device_id sensor_match_id[] = {
	{ "sensor,adapter", 0 },
	{ },
};

static struct i2c_driver sensor_i2c_driver = {
	.driver = {
		.name = SENSOR_NAME,
		.pm = &sensor_pm_ops,
		.of_match_table = of_match_ptr(sensor_of_match),
	},
	.probe		= &sensor_probe,
	.remove		= &sensor_remove,
	.id_table	= sensor_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&sensor_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&sensor_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("sensor adapter driver");
MODULE_LICENSE("GPL v2");
