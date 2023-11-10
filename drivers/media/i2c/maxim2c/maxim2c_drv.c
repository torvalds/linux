// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim Dual GMSL2/GMSL1 to CSI-2 Deserializer driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * Author: Cai Wenzhong <cwz@rock-chips.com>
 *
 * V2.00.00 maxim serdes dual GMSL2/GMSL1 driver framework.
 *     1. local deserializer support: max96716/max96718
 *     2. remote serializer support: max9295/max96715/max96717
 *     3. support deserializer and serializer auto adaptive
 *     4. support deserializer output test pattern
 *     5. support remote serializer channel management
 *     6. support remote serializer I2c address mapping
 *     7. support remote serializer hot plug detection and recovery
 *
 * V2.00.01
 *     1. MIPI TXPHY add tunnel mode support
 *     2. MIPI TXPHY mode only support 2x4Lanes and 2x2Lanes
 *
 */
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/compat.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/workqueue.h>
#include <linux/rk-camera-module.h>
#include <linux/gpio/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "maxim2c_api.h"

#define DRIVER_VERSION			KERNEL_VERSION(2, 0x00, 0x01)

#define MAXIM2C_XVCLK_FREQ		25000000

static int maxim2c_check_local_chipid(maxim2c_t *maxim2c)
{
	struct i2c_client *client = maxim2c->client;
	struct device *dev = &client->dev;
	int ret = 0, loop = 0;
	u8 chipid = 0;

	for (loop = 0; loop < 5; loop++) {
		if (loop != 0) {
			dev_info(dev, "check local chipid retry (%d)", loop);
			msleep(10);
		}

		ret = maxim2c_i2c_read_byte(client,
				MAXIM2C_REG_CHIP_ID, MAXIM2C_I2C_REG_ADDR_16BITS,
				&chipid);
		if (ret == 0) {
			if (chipid == maxim2c->chipid) {
				if (chipid == MAX96716_CHIP_ID) {
					dev_info(dev, "MAX96716 is Detected\n");
					return 0;
				}

				if (chipid == MAX96718_CHIP_ID) {
					dev_info(dev, "MAX96718 is Detected\n");
					return 0;
				}
			} else {
				// if chipid is unexpected, retry
				dev_err(dev, "Unexpected maxim chipid = %02x\n", chipid);
			}
		}
	}

	dev_err(dev, "maxim check chipid error, ret(%d)\n", ret);

	return -ENODEV;
}

static irqreturn_t maxim2c_hot_plug_detect_irq_handler(int irq, void *dev_id)
{
	struct maxim2c *maxim2c = dev_id;
	struct device *dev = &maxim2c->client->dev;
	int lock_gpio_level = 0;

	mutex_lock(&maxim2c->mutex);
	if (maxim2c->streaming) {
		lock_gpio_level = gpiod_get_value_cansleep(maxim2c->lock_gpio);
		if (lock_gpio_level == 0) {
			dev_info(dev, "serializer hot plug out\n");

			maxim2c->hot_plug_state = MAXIM2C_HOT_PLUG_OUT;
		} else {
			dev_info(dev, "serializer hot plug in\n");

			maxim2c->hot_plug_state = MAXIM2C_HOT_PLUG_IN;
		}

		queue_delayed_work(maxim2c->hot_plug_work.state_check_wq,
					&maxim2c->hot_plug_work.state_d_work,
					msecs_to_jiffies(100));
	}
	mutex_unlock(&maxim2c->mutex);

	return IRQ_HANDLED;
}

static void maxim2c_lock_irq_init(maxim2c_t *maxim2c)
{
	struct device *dev = &maxim2c->client->dev;
	int ret = 0;

	if (!IS_ERR(maxim2c->lock_gpio)) {
		maxim2c->hot_plug_irq = gpiod_to_irq(maxim2c->lock_gpio);
		if (maxim2c->hot_plug_irq < 0) {
			dev_err(dev, "failed to get hot plug irq\n");
		} else {
			ret = devm_request_threaded_irq(dev,
					maxim2c->hot_plug_irq,
					NULL,
					maxim2c_hot_plug_detect_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"maxim2c_hot_plug",
					maxim2c);
			if (ret) {
				dev_err(dev, "failed to request hot plug irq (%d)\n", ret);
				maxim2c->hot_plug_irq = -1;
			} else {
				disable_irq(maxim2c->hot_plug_irq);
			}
		}
	}
}

static void maxim2c_hot_plug_state_check_work(struct work_struct *work)
{
	struct maxim2c_hot_plug_work *hot_plug_work =
		container_of(work, struct maxim2c_hot_plug_work, state_d_work.work);
	struct maxim2c *maxim2c =
		container_of(hot_plug_work, struct maxim2c, hot_plug_work);
	struct device *dev = &maxim2c->client->dev;
	u8 curr_lock_state = 0, last_lock_state = 0, link_lock_change = 0;
	u8 link_enable_mask = 0, link_id = 0;

	dev_dbg(dev, "%s\n", __func__);

	mutex_lock(&maxim2c->mutex);
	if (maxim2c->streaming == 0) {
		mutex_unlock(&maxim2c->mutex);
		return;
	}

	link_enable_mask = maxim2c->gmsl_link.link_enable_mask;
	last_lock_state = maxim2c->link_lock_state;
	if ((maxim2c->hot_plug_state == MAXIM2C_HOT_PLUG_OUT)
			&& (last_lock_state == link_enable_mask)) {
		maxim2c_link_select_remote_control(maxim2c, 0);
	}

	curr_lock_state = maxim2c_link_get_lock_state(maxim2c, link_enable_mask);
	link_lock_change = (last_lock_state ^ curr_lock_state);
	if (link_lock_change) {
		dev_dbg(dev, "lock state: current = 0x%02x, last = 0x%02x\n",
			curr_lock_state, last_lock_state);

		maxim2c->link_lock_state = curr_lock_state;
	}

	if (link_lock_change & MAXIM2C_LINK_MASK_A) {
		link_id = MAXIM2C_LINK_ID_A;

		if (curr_lock_state & MAXIM2C_LINK_MASK_A) {
			dev_info(dev, "Link A plug in\n");

			if (maxim2c->hot_plug_irq > 0)
				disable_irq(maxim2c->hot_plug_irq);

			maxim2c_remote_devices_init(maxim2c, MAXIM2C_LINK_MASK_A);

			if (maxim2c->hot_plug_irq > 0)
				enable_irq(maxim2c->hot_plug_irq);

			maxim2c_video_pipe_linkid_enable(maxim2c, link_id, true);
		} else {
			dev_info(dev, "Link A plug out\n");

			maxim2c_video_pipe_linkid_enable(maxim2c, link_id, false);
		}
	}

	if (link_lock_change & MAXIM2C_LINK_MASK_B) {
		link_id = MAXIM2C_LINK_ID_B;

		if (curr_lock_state & MAXIM2C_LINK_MASK_B) {
			dev_info(dev, "Link B plug in\n");

			if (maxim2c->hot_plug_irq > 0)
				disable_irq(maxim2c->hot_plug_irq);

			maxim2c_remote_devices_init(maxim2c, MAXIM2C_LINK_MASK_B);

			if (maxim2c->hot_plug_irq > 0)
				enable_irq(maxim2c->hot_plug_irq);

			maxim2c_video_pipe_linkid_enable(maxim2c, link_id, true);
		} else {
			dev_info(dev, "Link B plug out\n");

			maxim2c_video_pipe_linkid_enable(maxim2c, link_id, false);
		}
	}

	if (curr_lock_state == link_enable_mask) {
		// remote control mask enable
		maxim2c_link_select_remote_control(maxim2c, link_enable_mask);
	} else {
		queue_delayed_work(maxim2c->hot_plug_work.state_check_wq,
				&maxim2c->hot_plug_work.state_d_work,
				msecs_to_jiffies(200));
	}

	mutex_unlock(&maxim2c->mutex);
}

int maxim2c_hot_plug_detect_work_start(maxim2c_t *maxim2c)
{
	struct device *dev = &maxim2c->client->dev;
	u8 link_lock_state = 0, link_enable_mask = 0;

	link_lock_state = maxim2c->link_lock_state;
	link_enable_mask = maxim2c->gmsl_link.link_enable_mask;

	if (link_lock_state != link_enable_mask) {
		dev_info(dev, "%s: link_lock = 0x%02x, link_mask = 0x%02x\n",
			__func__, link_lock_state, link_enable_mask);

		maxim2c->hot_plug_state = MAXIM2C_HOT_PLUG_OUT;

		queue_delayed_work(maxim2c->hot_plug_work.state_check_wq,
				&maxim2c->hot_plug_work.state_d_work,
				msecs_to_jiffies(200));
	}

	return 0;
}

static int maxim2c_lock_state_work_init(maxim2c_t *maxim2c)
{
	struct device *dev = &maxim2c->client->dev;

	INIT_DELAYED_WORK(&maxim2c->hot_plug_work.state_d_work,
			maxim2c_hot_plug_state_check_work);
	maxim2c->hot_plug_work.state_check_wq =
		create_singlethread_workqueue("maxim2c work queue");
	if (maxim2c->hot_plug_work.state_check_wq == NULL) {
		dev_err(dev, "failed to create hot plug work queue\n");
		return -ENOMEM;
	}

	return 0;
}

static int maxim2c_lock_state_work_deinit(maxim2c_t *maxim2c)
{
	if (maxim2c->hot_plug_work.state_check_wq) {
		cancel_delayed_work_sync(&maxim2c->hot_plug_work.state_d_work);
		destroy_workqueue(maxim2c->hot_plug_work.state_check_wq);
		maxim2c->hot_plug_work.state_check_wq = NULL;
	}

	return 0;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 maxim2c_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, MAXIM2C_XVCLK_FREQ / 1000 / 1000);
}

static int maxim2c_local_device_power_on(maxim2c_t *maxim2c)
{
	struct device *dev = &maxim2c->client->dev;

	if (!IS_ERR(maxim2c->pwdn_gpio)) {
		dev_info(dev, "local device pwdn gpio on\n");

		gpiod_set_value_cansleep(maxim2c->pwdn_gpio, 1);

		usleep_range(20000, 20100);
	}

	return 0;
}

static void maxim2c_local_device_power_off(maxim2c_t *maxim2c)
{
	struct device *dev = &maxim2c->client->dev;

	if (!IS_ERR(maxim2c->pwdn_gpio)) {
		dev_info(dev, "local device pwdn gpio off\n");

		gpiod_set_value_cansleep(maxim2c->pwdn_gpio, 0);
	}
}

static int maxim2c_remote_device_power_on(maxim2c_t *maxim2c)
{
	struct device *dev = &maxim2c->client->dev;

	// remote PoC enable
	if (!IS_ERR(maxim2c->pocen_gpio)) {
		dev_info(dev, "remote device pocen gpio on\n");

		gpiod_set_value_cansleep(maxim2c->pocen_gpio, 1);
		usleep_range(5000, 10000);
	}

	return 0;
}

static int maxim2c_remote_device_power_off(maxim2c_t *maxim2c)
{
	struct device *dev = &maxim2c->client->dev;

	// remote PoC enable
	if (!IS_ERR(maxim2c->pocen_gpio)) {
		dev_info(dev, "remote device pocen gpio off\n");

		gpiod_set_value_cansleep(maxim2c->pocen_gpio, 0);
	}

	return 0;
}

static int maxim2c_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct maxim2c *maxim2c = v4l2_get_subdevdata(sd);
	int ret = 0;

#if MAXIM2C_LOCAL_DES_ON_OFF_EN
	ret |= maxim2c_local_device_power_on(maxim2c);
#endif /* MAXIM2C_LOCAL_DES_ON_OFF_EN */

	ret |= maxim2c_remote_device_power_on(maxim2c);

	return ret;
}

static int maxim2c_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct maxim2c *maxim2c = v4l2_get_subdevdata(sd);
	int ret = 0;

	ret |= maxim2c_remote_device_power_off(maxim2c);

#if MAXIM2C_LOCAL_DES_ON_OFF_EN
	maxim2c_local_device_power_off(maxim2c);
#endif /* MAXIM2C_LOCAL_DES_ON_OFF_EN */

	return ret;
}

static const struct dev_pm_ops maxim2c_pm_ops = {
	SET_RUNTIME_PM_OPS(
		maxim2c_runtime_suspend, maxim2c_runtime_resume, NULL)
};

static void maxim2c_module_data_init(maxim2c_t *maxim2c)
{
	maxim2c_link_data_init(maxim2c);
	maxim2c_video_pipe_data_init(maxim2c);
	maxim2c_mipi_txphy_data_init(maxim2c);
}

static int maxim2c_extra_init_seq_parse(maxim2c_t *maxim2c, struct device_node *node)
{
	struct device *dev = &maxim2c->client->dev;
	struct device_node *init_seq_node = NULL;
	struct maxim2c_i2c_init_seq *init_seq = NULL;

	init_seq_node = of_get_child_by_name(node, "extra-init-sequence");
	if (IS_ERR_OR_NULL(init_seq_node)) {
		dev_dbg(dev, "%pOF no child node extra-init-sequence\n", node);
		return 0;
	}

	if (!of_device_is_available(init_seq_node)) {
		dev_dbg(dev, "%pOF is disabled\n", init_seq_node);

		of_node_put(init_seq_node);
		return 0;
	}

	dev_info(dev, "load extra-init-sequence\n");

	init_seq = &maxim2c->extra_init_seq;
	maxim2c_i2c_load_init_seq(dev,
			init_seq_node, init_seq);

	of_node_put(init_seq_node);

	return 0;
}

static int maxim2c_module_parse_dt(maxim2c_t *maxim2c)
{
	struct device *dev = &maxim2c->client->dev;
	struct device_node *node = NULL;

	// maxim serdes local
	node = of_get_child_by_name(dev->of_node, "serdes-local-device");
	if (IS_ERR_OR_NULL(node)) {
		dev_err(dev, "%pOF has no child node: serdes-local-device\n",
				dev->of_node);

		return -ENODEV;
	}

	if (!of_device_is_available(node)) {
		dev_info(dev, "%pOF is disabled\n", node);

		of_node_put(node);
		return -ENODEV;
	}

	/* gmsl link parse dt */
	maxim2c_link_parse_dt(maxim2c, node);

	/* video pipe parse dt */
	maxim2c_video_pipe_parse_dt(maxim2c, node);

	/* mipi txphy parse dt */
	maxim2c_mipi_txphy_parse_dt(maxim2c, node);

	/* extra init seq parse dt */
	maxim2c_extra_init_seq_parse(maxim2c, node);

	of_node_put(node);

	return 0;
}

static int maxim2c_run_extra_init_seq(maxim2c_t *maxim2c)
{
	struct i2c_client *client = maxim2c->client;
	struct device *dev = &client->dev;
	int ret = 0;

	ret = maxim2c_i2c_run_init_seq(client,
			&maxim2c->extra_init_seq);
	if (ret) {
		dev_err(dev, "extra init sequence error\n");
		return ret;
	}

	return 0;
}

static int maxim2c_module_hw_previnit(maxim2c_t *maxim2c)
{
	struct i2c_client *client = maxim2c->client;
	int ret = 0;

	// Disable data transmission through video pipe.
	ret = maxim2c_i2c_update_byte(client,
			0x0002, MAXIM2C_I2C_REG_ADDR_16BITS,
			0xF0, 0x00);
	if (ret)
		return ret;

	// Video Pipe Y/Z Disable
	ret = maxim2c_i2c_update_byte(client,
			0x0160, MAXIM2C_I2C_REG_ADDR_16BITS,
			BIT(1) | BIT(0), 0);
	if (ret)
		return ret;

	// MIPI CSI output disable.
	ret = maxim2c_i2c_write_byte(client,
			0x0313, MAXIM2C_I2C_REG_ADDR_16BITS,
			0x00);
	if (ret)
		return ret;

	// MIPI TXPHY standby
	ret = maxim2c_i2c_update_byte(client,
			0x0332, MAXIM2C_I2C_REG_ADDR_16BITS,
			0xF0, 0x00);
	if (ret)
		return ret;

	return 0;
}

static int maxim2c_module_hw_postinit(maxim2c_t *maxim2c)
{
	struct i2c_client *client = maxim2c->client;
	int ret = 0;

	// video pipe disable all
	ret |= maxim2c_i2c_write_byte(client,
			0x0160, MAXIM2C_I2C_REG_ADDR_16BITS,
			0);

	// remote control disable all
	ret |= maxim2c_link_select_remote_control(maxim2c, 0);

	// Enable data transmission through video pipe.
	ret |= maxim2c_i2c_update_byte(client,
			0x0002, MAXIM2C_I2C_REG_ADDR_16BITS,
			0xF0, 0xF0);

	return ret;
}

int maxim2c_module_hw_init(maxim2c_t *maxim2c)
{
	struct device *dev = &maxim2c->client->dev;
	int ret = 0;

	ret = maxim2c_module_hw_previnit(maxim2c);
	if (ret) {
		dev_err(dev, "%s: hw prev init error\n", __func__);

		return ret;
	}

	ret = maxim2c_link_hw_init(maxim2c);
	if (ret) {
		dev_err(dev, "%s: hw link init error\n", __func__);
		return ret;
	}

	ret = maxim2c_video_pipe_hw_init(maxim2c);
	if (ret) {
		dev_err(dev, "%s: hw pipe init error\n", __func__);
		return ret;
	}

	ret = maxim2c_mipi_txphy_hw_init(maxim2c);
	if (ret) {
		dev_err(dev, "%s: hw txphy init error\n", __func__);
		return ret;
	}

	ret = maxim2c_run_extra_init_seq(maxim2c);
	if (ret) {
		dev_err(dev, "%s: run extra init seq error\n", __func__);
		return ret;
	}

	ret = maxim2c_module_hw_postinit(maxim2c);
	if (ret) {
		dev_err(dev, "%s: hw post init error\n", __func__);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(maxim2c_module_hw_init);

static int maxim2c_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	maxim2c_t *maxim2c = NULL;
	u32 chip_id;
	int ret = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x", DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8, DRIVER_VERSION & 0x00ff);

	chip_id = (uintptr_t)of_device_get_match_data(dev);
	if (chip_id == MAX96716_CHIP_ID) {
		dev_info(dev, "maxim2c driver for max96716\n");
	} else if (chip_id == MAX96718_CHIP_ID) {
		dev_info(dev, "maxim2c driver for max96718\n");
	} else {
		dev_err(dev, "maxim2c driver unknown chip\n");
		return -EINVAL;
	}

	maxim2c = devm_kzalloc(dev, sizeof(*maxim2c), GFP_KERNEL);
	if (!maxim2c) {
		dev_err(dev, "maxim2c probe no memory error\n");
		return -ENOMEM;
	}

	maxim2c->client = client;
	maxim2c->chipid = chip_id;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &maxim2c->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &maxim2c->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &maxim2c->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &maxim2c->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	maxim2c->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(maxim2c->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios, maybe no use\n");
	else
		usleep_range(1000, 1100);

	maxim2c->pocen_gpio = devm_gpiod_get(dev, "pocen", GPIOD_OUT_LOW);
	if (IS_ERR(maxim2c->pocen_gpio))
		dev_warn(dev, "Failed to get pocen-gpios\n");

	maxim2c->lock_gpio = devm_gpiod_get(dev, "lock", GPIOD_IN);
	if (IS_ERR(maxim2c->lock_gpio))
		dev_warn(dev, "Failed to get lock-gpios\n");

	mutex_init(&maxim2c->mutex);

	ret = maxim2c_local_device_power_on(maxim2c);
	if (ret)
		goto err_destroy_mutex;

	ret = maxim2c_check_local_chipid(maxim2c);
	if (ret)
		goto err_power_off;

	// client->dev->driver_data = subdev
	// subdev->dev->driver_data = maxim2c
	ret = maxim2c_v4l2_subdev_init(maxim2c);
	if (ret) {
		dev_err(dev, "maxim2c probe v4l2 subdev init error\n");
		goto err_power_off;
	}

#if MAXIM2C_TEST_PATTERN
	ret = maxim2c_pattern_data_init(maxim2c);
	if (ret)
		goto err_power_off;

#if (MAXIM2C_LOCAL_DES_ON_OFF_EN == 0)
	ret = maxim2c_pattern_hw_init(maxim2c);
	if (ret)
		goto err_power_off;
#endif /* MAXIM2C_LOCAL_DES_ON_OFF_EN */

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;
#endif /* MAXIM2C_TEST_PATTERN */

	maxim2c_module_data_init(maxim2c);
	maxim2c_module_parse_dt(maxim2c);

#if (MAXIM2C_LOCAL_DES_ON_OFF_EN == 0)
	ret = maxim2c_module_hw_init(maxim2c);
	if (ret)
		goto err_subdev_deinit;
#endif /* MAXIM2C_LOCAL_DES_ON_OFF_EN */

	ret = maxim2c_remote_mfd_add_devices(maxim2c);
	if (ret)
		goto err_subdev_deinit;

	maxim2c_lock_irq_init(maxim2c);
	maxim2c_lock_state_work_init(maxim2c);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_subdev_deinit:
	maxim2c_v4l2_subdev_deinit(maxim2c);
err_power_off:
	maxim2c_local_device_power_off(maxim2c);
err_destroy_mutex:
	mutex_destroy(&maxim2c->mutex);

	return ret;
}

static int maxim2c_remove(struct i2c_client *client)
{
	maxim2c_t *maxim2c = i2c_get_clientdata(client);

	maxim2c_lock_state_work_deinit(maxim2c);

	maxim2c_v4l2_subdev_deinit(maxim2c);

	mutex_destroy(&maxim2c->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		maxim2c_local_device_power_off(maxim2c);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

static const struct of_device_id maxim2c_of_match[] = {
	{
		.compatible = "maxim2c,max96716",
		.data = (const void *)MAX96716_CHIP_ID
	}, {
		.compatible = "maxim2c,max96718",
		.data = (const void *)MAX96718_CHIP_ID
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, maxim2c_of_match);

static struct i2c_driver maxim2c_i2c_driver = {
	.driver = {
		.name = MAXIM2C_NAME,
		.pm = &maxim2c_pm_ops,
		.of_match_table = of_match_ptr(maxim2c_of_match),
	},
	.probe		= &maxim2c_probe,
	.remove		= &maxim2c_remove,
};

module_i2c_driver(maxim2c_i2c_driver);

MODULE_AUTHOR("Cai Wenzhong <cwz@rock-chips.com>");
MODULE_DESCRIPTION("Maxim dual gmsl deserializer driver");
MODULE_LICENSE("GPL");
