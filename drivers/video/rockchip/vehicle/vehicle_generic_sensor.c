// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/video/rockchip/video/vehicle_generic_sensor.c
 *
 * Copyright (C) 2020 Rockchip Electronics Co.Ltd
 * Authors:
 *      Zhiqin Wei <wzq@rock-chips.com>
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include "vehicle_ad.h"
#include "vehicle_ad_7181.h"
#include "vehicle_ad_tp2825.h"
#include "vehicle_ad_gc2145.h"
#include "vehicle_ad_nvp6324.h"
#include "vehicle_ad_nvp6188.h"
#include "vehicle_ad_max96714.h"
#include <linux/moduleparam.h>
#include "../../../../drivers/media/i2c/jaguar1_drv/jaguar1_v4l2.h"
#include "../../../../drivers/media/i2c/nvp6188.h"
#include "../../../../drivers/media/i2c/max96714.h"

struct vehicle_sensor_ops {
	const char *name;
	int (*sensor_init)(struct vehicle_ad_dev *ad);
	int (*sensor_deinit)(void);
	int (*sensor_stream)(struct vehicle_ad_dev *ad, int value);
	int (*sensor_get_cfg)(struct vehicle_cfg **cfg);
	void (*sensor_check_cif_error)(struct vehicle_ad_dev *ad, int last_line);
	int (*sensor_check_id_cb)(struct vehicle_ad_dev *ad);
	void (*sensor_set_channel)(struct vehicle_ad_dev *ad, int channel);
	int (*sensor_mod_init)(void);
};
static struct vehicle_sensor_ops *sensor_cb;

static struct vehicle_sensor_ops sensor_cb_series[] = {
	{
		.name = "adv7181",
#ifdef CONFIG_VIDEO_REVERSE_AD7181
		.sensor_init = adv7181_ad_init,
		.sensor_deinit = adv7181_ad_deinit,
		.sensor_stream = adv7181_stream,
		.sensor_get_cfg = adv7181_ad_get_cfg,
		.sensor_check_cif_error = adv7181_ad_check_cif_error,
		.sensor_check_id_cb = adv7181_check_id,
		.sensor_set_channel = adv7181_channel_set
#endif
	},
	{
		.name = "tp2825",
#ifdef CONFIG_VIDEO_REVERSE_TP2825
		.sensor_init = tp2825_ad_init,
		.sensor_deinit = tp2825_ad_deinit,
		.sensor_stream = tp2825_stream,
		.sensor_get_cfg = tp2825_ad_get_cfg,
		.sensor_check_cif_error = tp2825_ad_check_cif_error,
		.sensor_check_id_cb = tp2825_check_id,
		.sensor_set_channel = tp2825_channel_set
#endif
	},
	{
		.name = "gc2145",
#ifdef CONFIG_VIDEO_REVERSE_GC2145
		.sensor_init = gc2145_ad_init,
		.sensor_deinit = gc2145_ad_deinit,
		.sensor_stream = gc2145_stream,
		.sensor_get_cfg = gc2145_ad_get_cfg,
		.sensor_check_cif_error = gc2145_ad_check_cif_error,
		.sensor_check_id_cb = gc2145_check_id,
		.sensor_set_channel = gc2145_channel_set,
#endif
	},
	{
		.name = "nvp6324",
#ifdef CONFIG_VIDEO_REVERSE_NVP6324
		.sensor_init = nvp6324_ad_init,
		.sensor_deinit = nvp6324_ad_deinit,
		.sensor_stream = nvp6324_stream,
		.sensor_get_cfg = nvp6324_ad_get_cfg,
		.sensor_check_cif_error = nvp6324_ad_check_cif_error,
		.sensor_check_id_cb = nvp6324_check_id,
		.sensor_set_channel = nvp6324_channel_set,
#ifdef CONFIG_VIDEO_NVP6324
		.sensor_mod_init = nvp6324_sensor_mod_init
#endif
#endif
	},
	{
		.name = "max96714",
#ifdef CONFIG_VIDEO_REVERSE_MAX96714
		.sensor_init = max96714_ad_init,
		.sensor_deinit = max96714_ad_deinit,
		.sensor_stream = max96714_stream,
		.sensor_get_cfg = max96714_ad_get_cfg,
		.sensor_check_cif_error = max96714_ad_check_cif_error,
		.sensor_check_id_cb = max96714_check_id,
		.sensor_set_channel = max96714_channel_set,
#ifdef CONFIG_VIDEO_MAX96714
		.sensor_mod_init = max96714_sensor_mod_init
#endif
#endif
	},
	{
		.name = "nvp6188",
#ifdef CONFIG_VIDEO_REVERSE_NVP6188
		.sensor_init = nvp6188_ad_init,
		.sensor_deinit = nvp6188_ad_deinit,
		.sensor_stream = nvp6188_stream,
		.sensor_get_cfg = nvp6188_ad_get_cfg,
		.sensor_check_cif_error = nvp6188_ad_check_cif_error,
		.sensor_check_id_cb = nvp6188_check_id,
		.sensor_set_channel = nvp6188_channel_set,
#ifdef CONFIG_VIDEO_NVP6188
		.sensor_mod_init = nvp6188_sensor_mod_init
#endif
#endif
	}
};

int vehicle_generic_sensor_write(struct vehicle_ad_dev *ad, char reg, char *pval)
{
	struct i2c_msg msg;
	int ret;

	char *tx_buf = kmalloc(2, GFP_KERNEL);

	if (!tx_buf)
		return -ENOMEM;

	memcpy(tx_buf, &reg, 1);
	memcpy(tx_buf+1, (char *)pval, 1);

	msg.addr = ad->i2c_add;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = (char *)tx_buf;
//	msg.scl_rate = ad->i2c_rate;

	ret = i2c_transfer(ad->adapter, &msg, 1);
	kfree(tx_buf);

	return (ret == 1) ? 4 : ret;
}

int vehicle_sensor_write(struct vehicle_ad_dev *ad, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	//SENSOR_DG("write reg(0x%x val:0x%x)!\n", reg, val);
	buf[0] = reg & 0xFF;
	buf[1] = val;

	msg.addr = ad->i2c_add;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(ad->adapter, &msg, 1);
	if (ret >= 0)
		return 0;

	VEHICLE_DGERR("write reg(0x%x val:0x%x) failed !\n", reg, val);
	return ret;
}

int vehicle_generic_sensor_read(struct vehicle_ad_dev *ad, char reg)
{
	struct i2c_msg msgs[2];
	int ret;
	char reg_buf[2];
	char pval;

	memcpy(reg_buf, &reg, 1);

	msgs[0].addr =	ad->i2c_add;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = reg_buf;
//	msgs[0].scl_rate = ad->i2c_rate;

	msgs[1].addr = ad->i2c_add;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = &pval;
//	msgs[1].scl_rate = ad->i2c_rate;

	ret = i2c_transfer(ad->adapter, msgs, 2);
	if (ret)
		return ret;

	return pval;
}

/* sensor register read */
int vehicle_sensor_read(struct vehicle_ad_dev *ad, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[1];
	int ret;

	buf[0] = reg & 0xFF;

	msg[0].addr = ad->i2c_add;
	msg[0].flags = 0;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	msg[1].addr = ad->i2c_add;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = 1;

	ret = i2c_transfer(ad->adapter, msg, 2);
	if (ret >= 0) {
		*val = buf[0];
		return 0;
	}

	dev_err(ad->dev,
		"read reg:0x%x failed !\n", reg);

	return ret;
}

int vehicle_ad_stream(struct vehicle_ad_dev *ad, int val)
{
	int ret = 0;

	if (sensor_cb && sensor_cb->sensor_stream) {
		ret = sensor_cb->sensor_stream(ad, val);
		if (ret < 0)
			VEHICLE_DGERR("%s sensor_init failed!\n", ad->ad_name);
	}

	return ret;
}

int vehicle_parse_sensor(struct vehicle_ad_dev *ad)
{
	struct device *dev = ad->dev;
	struct device_node *node = NULL;
	struct device_node *cp = NULL;
	enum of_gpio_flags flags;
	const char *status = NULL;
	int i;
	int ret = 0;

	if (of_property_read_u32(dev->of_node, "ad,fix-format",
				 &ad->fix_format))
		VEHICLE_DGERR("get fix-format failed!\n");

	if (of_property_read_u32(dev->of_node, "vehicle,rotate-mirror",
				 &ad->cfg.rotate_mirror))
		VEHICLE_DGERR("get rotate-mirror failed!\n");

	node = of_parse_phandle(dev->of_node, "rockchip,cif-sensor", 0);
	if (!node) {
		VEHICLE_DGERR("get cif-sensor dts failed\n");
		return -ENODEV;
	}

	for_each_child_of_node(node, cp) {
		of_property_read_string(cp, "status", &status);
		if (status && !strcmp(status, "disabled"))
			continue;
		VEHICLE_DG("status: %s\n", status);

//		if (of_property_read_u32(cp, "i2c_rata", &ad->i2c_rate))
//			SENSOR_DG("Get %s i2c_rata failed!\n", cp->name);
		if (of_property_read_u32(cp, "i2c_chl", &ad->i2c_chl))
			VEHICLE_DGERR("Get %s i2c_chl failed!", cp->name);
		if (of_property_read_u32(cp, "ad_chl", &ad->ad_chl))
			VEHICLE_DGERR("Get %s ad_chl failed!", cp->name);

		if (ad->ad_chl > 4 || ad->ad_chl < 0) {
			VEHICLE_DGERR("error, ad_chl %d !\n", ad->ad_chl);
			ad->ad_chl = 0;
		}
		if (of_property_read_u32(cp, "mclk_rate", &ad->mclk_rate))
			VEHICLE_DGERR("Get %s mclk_rate failed!\n", cp->name);

		if (of_property_read_u32(cp, "rst_active", &ad->rst_active))
			VEHICLE_DGERR("Get %s rst_active failed!", cp->name);

		ad->reset = of_get_named_gpio_flags(cp, "reset-gpios",
							0, &flags);

		if (of_property_read_u32(cp, "pwr_active", &ad->pwr_active))
			VEHICLE_DGERR("Get %s pwr_active failed!\n", cp->name);

		if (of_property_read_u32(cp, "pwdn_active", &ad->pwdn_active))
			VEHICLE_DGERR("Get %s pwdn_active failed!\n", cp->name);

		ad->power = of_get_named_gpio_flags(cp, "power-gpios",
						    0, &flags);
		ad->powerdown = of_get_named_gpio_flags(cp,
							"powerdown-gpios",
							0, &flags);
		ad->reset = of_get_named_gpio_flags(cp, "reset-gpios",
						0, &flags);

		if (of_property_read_u32(cp, "i2c_add", &ad->i2c_add))
			VEHICLE_DGERR("Get %s i2c_add failed!\n", cp->name);

		ad->i2c_add = (ad->i2c_add >> 1);

		if (of_property_read_u32(cp, "resolution", &ad->resolution))
			VEHICLE_DGERR("Get %s resolution failed!\n", cp->name);

		of_property_read_u32_array(cp,
				"rockchip,camera-module-defrect0",
				(unsigned int *)&ad->defrects[0], 6);
		of_property_read_u32_array(cp,
				"rockchip,camera-module-defrect1",
				(unsigned int *)&ad->defrects[1], 6);
		of_property_read_u32_array(cp,
				"rockchip,camera-module-defrect2",
				(unsigned int *)&ad->defrects[2], 6);
		of_property_read_u32_array(cp,
				"rockchip,camera-module-defrect3",
				(unsigned int *)&ad->defrects[3], 6);

		of_property_read_string(cp,
				"rockchip,camera-module-interface0",
				&ad->defrects[0].interface);
		of_property_read_string(cp,
				"rockchip,camera-module-interface1",
				&ad->defrects[1].interface);
		of_property_read_string(cp,
				"rockchip,camera-module-interface2",
				&ad->defrects[2].interface);
		of_property_read_string(cp,
				"rockchip,camera-module-interface3",
				&ad->defrects[3].interface);

		ad->ad_name = cp->name;
		for (i = 0; i < ARRAY_SIZE(sensor_cb_series); i++) {
			if (!strcmp(ad->ad_name, sensor_cb_series[i].name))
				sensor_cb = sensor_cb_series + i;
		}

		VEHICLE_DG("%s: ad_chl=%d,,ad_addr=%x,fix_for=%d\n", ad->ad_name,
		    ad->ad_chl, ad->i2c_add, ad->fix_format);
		VEHICLE_DG("gpio power:%d, active:%d\n", ad->power, ad->pwr_active);
		VEHICLE_DG("gpio powerdown:%d, active:%d\n",
		    ad->powerdown, ad->pwdn_active);
		break;
	}

	if (!ad->ad_name)
		ret = -EINVAL;

	return ret;
}

void vehicle_ad_channel_set(struct vehicle_ad_dev *ad, int channel)
{
	if (sensor_cb->sensor_set_channel)
		sensor_cb->sensor_set_channel(ad, channel);
}

int vehicle_ad_init(struct vehicle_ad_dev *ad)
{
	int ret = 0;
	//WARN_ON(1);
	VEHICLE_DGERR("%s(%d) ad_name:%s!", __func__, __LINE__, ad->ad_name);

	if (sensor_cb->sensor_init) {
		ret = sensor_cb->sensor_init(ad);
		if (ret < 0) {
			VEHICLE_DGERR("%s sensor_init failed!\n", ad->ad_name);
			goto end;
		}
	} else {
		VEHICLE_DGERR("%s sensor_init is NULL!\n", ad->ad_name);
		ret = -1;
		goto end;
	}

	if (sensor_cb->sensor_check_id_cb) {
		ret = sensor_cb->sensor_check_id_cb(ad);
		if (ret < 0)
			VEHICLE_DGERR("%s check id failed!\n", ad->ad_name);
	}

end:
	return ret;
}

int vehicle_ad_deinit(void)
{
	int ret = 0;

	if (sensor_cb->sensor_deinit)
		ret = sensor_cb->sensor_deinit();
	else
		ret = -EINVAL;

	return ret;
}

int vehicle_to_v4l2_drv_init(void)
{
	int ret = 0;

	VEHICLE_DG("%s(%d) enter!", __func__, __LINE__);
	if (sensor_cb && sensor_cb->sensor_mod_init)
		ret = sensor_cb->sensor_mod_init();
	else
		ret = -EINVAL;

	return ret;
}

struct vehicle_cfg *vehicle_ad_get_vehicle_cfg(void)
{
	struct vehicle_cfg *cfg = NULL;

	if (sensor_cb->sensor_get_cfg)
		sensor_cb->sensor_get_cfg(&cfg);

	return cfg;
}

void vehicle_ad_check_cif_error(struct vehicle_ad_dev *ad, int last_line)
{
	if (sensor_cb->sensor_get_cfg)
		sensor_cb->sensor_check_cif_error(ad, last_line);
}
