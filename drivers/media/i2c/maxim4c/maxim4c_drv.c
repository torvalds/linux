// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim Quad GMSL2/GMSL1 to CSI-2 Deserializer driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * Author: Cai Wenzhong <cwz@rock-chips.com>
 *
 * V2.0.00 maxim serdes qual GMSL2/GMSL1 driver framework.
 *     1. local deserializer support: max96712/max96722
 *     2. remote serializer support: max9295/max96715/max96717
 *     3. support deserializer and serializer auto adaptive
 *     4. support deserializer output test pattern
 *     5. support remote serializer I2c address mapping
 *     6. support remote serializer hot plug detection and recovery
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

#include "maxim4c_api.h"

#define DRIVER_VERSION			KERNEL_VERSION(2, 0x00, 0x00)

#define MAXIM4C_XVCLK_FREQ		25000000

/* device compatible */
#define MAXIM4C_MAX96712_COMPAT		"maxim4c,max96712"
#define MAXIM4C_MAX96722_COMPAT		"maxim4c,max96722"

static int maxim4c_check_local_chipid(maxim4c_t *maxim4c)
{
	struct i2c_client *client = maxim4c->client;
	struct device *dev = &client->dev;
	int ret = 0, loop = 0;
	u8 chipid = 0;

	for (loop = 0; loop < 5; loop++) {
		ret = maxim4c_i2c_read_byte(client,
				MAXIM4C_REG_CHIP_ID, MAXIM4C_I2C_REG_ADDR_16BITS,
				&chipid);
		if (ret == 0) {
			if (chipid == maxim4c->chipid) {
				if (chipid == MAX96712_CHIP_ID) {
					dev_info(dev, "MAX96712 is Detected\n");
					return 0;
				}

				if (chipid == MAX96722_CHIP_ID) {
					dev_info(dev, "MAX96722 is Detected\n");
					return 0;
				}
			} else {
				dev_err(dev, "Unexpected maxim chipid(%02x)\n", chipid);
				return -ENODEV;
			}
		}

		dev_info(dev, "retry (%d) to check local chipid", loop + 1);
		msleep(10);
	}

	dev_err(dev, "maxim check chipid error, ret(%d)\n", ret);

	return -ENODEV;
}

static irqreturn_t maxim4c_hot_plug_detect_irq_handler(int irq, void *dev_id)
{
	struct maxim4c *maxim4c = dev_id;
	struct device *dev = &maxim4c->client->dev;
	int lock_gpio_level = 0;

	mutex_lock(&maxim4c->mutex);
	if (maxim4c->streaming) {
		lock_gpio_level = gpiod_get_value_cansleep(maxim4c->lock_gpio);
		if (lock_gpio_level == 0) {
			dev_info(dev, "serializer hot plug out\n");

			maxim4c->hot_plug_state = MAXIM4C_HOT_PLUG_OUT;
		} else {
			dev_info(dev, "serializer hot plug in\n");

			maxim4c->hot_plug_state = MAXIM4C_HOT_PLUG_IN;
		}

		queue_delayed_work(maxim4c->hot_plug_work.state_check_wq,
					&maxim4c->hot_plug_work.state_d_work,
					msecs_to_jiffies(50));
	}
	mutex_unlock(&maxim4c->mutex);

	return IRQ_HANDLED;
}

static void maxim4c_lock_irq_init(maxim4c_t *maxim4c)
{
	struct device *dev = &maxim4c->client->dev;
	int ret = 0;

	if (!IS_ERR(maxim4c->lock_gpio)) {
		maxim4c->hot_plug_irq = gpiod_to_irq(maxim4c->lock_gpio);
		if (maxim4c->hot_plug_irq < 0) {
			dev_err(dev, "failed to get hot plug irq\n");
		} else {
			ret = devm_request_threaded_irq(dev,
					maxim4c->hot_plug_irq,
					NULL,
					maxim4c_hot_plug_detect_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"maxim4c_hot_plug",
					maxim4c);
			if (ret) {
				dev_err(dev, "failed to request hot plug irq (%d)\n", ret);
				maxim4c->hot_plug_irq = -1;
			} else {
				disable_irq(maxim4c->hot_plug_irq);
			}
		}
	}
}

static void maxim4c_hot_plug_state_check_work(struct work_struct *work)
{
	struct maxim4c_hot_plug_work *hot_plug_work =
		container_of(work, struct maxim4c_hot_plug_work, state_d_work.work);
	struct maxim4c *maxim4c =
		container_of(hot_plug_work, struct maxim4c, hot_plug_work);
	struct device *dev = &maxim4c->client->dev;
	u8 curr_lock_state = 0, last_lock_state = 0, link_lock_change = 0;
	u8 link_enable_mask = 0, link_id = 0;

	dev_dbg(dev, "%s\n", __func__);

	mutex_lock(&maxim4c->mutex);
	if (maxim4c->streaming == 0) {
		mutex_unlock(&maxim4c->mutex);
		return;
	}

	link_enable_mask = maxim4c->gmsl_link.link_enable_mask;
	last_lock_state = maxim4c->link_lock_state;
	if ((maxim4c->hot_plug_state == MAXIM4C_HOT_PLUG_OUT)
			&& (last_lock_state == link_enable_mask)) {
		maxim4c_link_select_remote_control(maxim4c, 0);
	}

	curr_lock_state = maxim4c_link_get_lock_state(maxim4c, link_enable_mask);
	link_lock_change = (last_lock_state ^ curr_lock_state);
	if (link_lock_change) {
		dev_dbg(dev, "lock state: current = 0x%02x, last = 0x%02x\n",
			curr_lock_state, last_lock_state);

		maxim4c->link_lock_state = curr_lock_state;
	}

	if (link_lock_change & MAXIM4C_LINK_MASK_A) {
		link_id = MAXIM4C_LINK_ID_A;

		if (curr_lock_state & MAXIM4C_LINK_MASK_A) {
			dev_info(dev, "link A plug in\n");

			maxim4c_remote_devices_init(maxim4c, MAXIM4C_LINK_MASK_A);

			maxim4c_video_pipe_linkid_enable(maxim4c, link_id, true);
		} else {
			dev_info(dev, "link A plug out\n");

			maxim4c_video_pipe_linkid_enable(maxim4c, link_id, false);
		}
	}

	if (link_lock_change & MAXIM4C_LINK_MASK_B) {
		link_id = MAXIM4C_LINK_ID_B;

		if (curr_lock_state & MAXIM4C_LINK_MASK_B) {
			dev_info(dev, "link B plug in\n");

			maxim4c_remote_devices_init(maxim4c, MAXIM4C_LINK_MASK_B);

			maxim4c_video_pipe_linkid_enable(maxim4c, link_id, true);
		} else {
			dev_info(dev, "link B plug out\n");

			maxim4c_video_pipe_linkid_enable(maxim4c, link_id, false);
		}
	}

	if (link_lock_change & MAXIM4C_LINK_MASK_C) {
		link_id = MAXIM4C_LINK_ID_C;

		if (curr_lock_state & MAXIM4C_LINK_MASK_C) {
			dev_info(dev, "link C plug in\n");

			maxim4c_remote_devices_init(maxim4c, MAXIM4C_LINK_MASK_C);

			maxim4c_video_pipe_linkid_enable(maxim4c, link_id, true);
		} else {
			dev_info(dev, "link C plug out\n");

			maxim4c_video_pipe_linkid_enable(maxim4c, link_id, false);
		}
	}

	if (link_lock_change & MAXIM4C_LINK_MASK_D) {
		link_id = MAXIM4C_LINK_ID_D;

		if (curr_lock_state & MAXIM4C_LINK_MASK_D) {
			dev_info(dev, "link D plug in\n");

			maxim4c_remote_devices_init(maxim4c, MAXIM4C_LINK_MASK_D);

			maxim4c_video_pipe_linkid_enable(maxim4c, link_id, true);
		} else {
			dev_info(dev, "link D plug out\n");

			maxim4c_video_pipe_linkid_enable(maxim4c, link_id, false);
		}
	}

	if (curr_lock_state == link_enable_mask) {
		// remote control mask enable
		maxim4c_link_select_remote_control(maxim4c, link_enable_mask);
	} else {
		queue_delayed_work(maxim4c->hot_plug_work.state_check_wq,
				&maxim4c->hot_plug_work.state_d_work,
				msecs_to_jiffies(100));
	}

	mutex_unlock(&maxim4c->mutex);
}

static int maxim4c_lock_state_work_init(maxim4c_t *maxim4c)
{
	struct device *dev = &maxim4c->client->dev;

	INIT_DELAYED_WORK(&maxim4c->hot_plug_work.state_d_work,
			maxim4c_hot_plug_state_check_work);
	maxim4c->hot_plug_work.state_check_wq =
		create_singlethread_workqueue("maxim4c work queue");
	if (maxim4c->hot_plug_work.state_check_wq == NULL) {
		dev_err(dev, "failed to create hot plug work queue\n");
		return -ENOMEM;
	}

	return 0;
}

static int maxim4c_lock_state_work_deinit(maxim4c_t *maxim4c)
{
	if (maxim4c->hot_plug_work.state_check_wq) {
		cancel_delayed_work_sync(&maxim4c->hot_plug_work.state_d_work);
		destroy_workqueue(maxim4c->hot_plug_work.state_check_wq);
		maxim4c->hot_plug_work.state_check_wq = NULL;
	}

	return 0;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 maxim4c_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, MAXIM4C_XVCLK_FREQ / 1000 / 1000);
}

static int maxim4c_local_device_power_on(maxim4c_t *maxim4c)
{
	struct device *dev = &maxim4c->client->dev;

	if (!IS_ERR(maxim4c->power_gpio)) {
		dev_info(dev, "local device power gpio on\n");

		gpiod_set_value_cansleep(maxim4c->power_gpio, 1);

		usleep_range(5000, 10000);
	}

	return 0;
}

static void maxim4c_local_device_power_off(maxim4c_t *maxim4c)
{
	struct device *dev = &maxim4c->client->dev;

	if (!IS_ERR(maxim4c->power_gpio)) {
		dev_info(dev, "local device power gpio off\n");

		gpiod_set_value_cansleep(maxim4c->power_gpio, 0);
	}
}

static int maxim4c_remote_device_power_on(maxim4c_t *maxim4c)
{
	struct device *dev = &maxim4c->client->dev;

	// remote PoC enable
	if (!IS_ERR(maxim4c->pocen_gpio)) {
		dev_info(dev, "remote device poc gpio on\n");

		gpiod_set_value_cansleep(maxim4c->pocen_gpio, 1);
		usleep_range(5000, 10000);
	}

	return 0;
}

static int maxim4c_remote_device_power_off(maxim4c_t *maxim4c)
{
	struct device *dev = &maxim4c->client->dev;

	// remote PoC enable
	if (!IS_ERR(maxim4c->pocen_gpio)) {
		dev_info(dev, "remote device poc gpio off\n");

		gpiod_set_value_cansleep(maxim4c->pocen_gpio, 0);
	}

	return 0;
}

static int maxim4c_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct maxim4c *maxim4c = v4l2_get_subdevdata(sd);

	return maxim4c_remote_device_power_on(maxim4c);
}

static int maxim4c_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct maxim4c *maxim4c = v4l2_get_subdevdata(sd);

	return maxim4c_remote_device_power_off(maxim4c);
}

static const struct dev_pm_ops maxim4c_pm_ops = {
	SET_RUNTIME_PM_OPS(
		maxim4c_runtime_suspend, maxim4c_runtime_resume, NULL)
};

static void maxim4c_module_data_init(maxim4c_t *maxim4c)
{
	maxim4c_link_data_init(maxim4c);
	maxim4c_video_pipe_data_init(maxim4c);
	maxim4c_mipi_txphy_data_init(maxim4c);
}

static int maxim4c_extra_init_seq_parse(maxim4c_t *maxim4c, struct device_node *node)
{
	struct device *dev = &maxim4c->client->dev;
	struct device_node *init_seq_node = NULL;
	struct maxim4c_i2c_init_seq *init_seq = NULL;

	init_seq_node = of_get_child_by_name(node, "extra-init-sequence");
	if (!IS_ERR_OR_NULL(init_seq_node)) {
		if (!of_device_is_available(init_seq_node)) {
			dev_info(dev, "%pOF is disabled\n", init_seq_node);

			return 0;
		}

		dev_info(dev, "load extra-init-sequence\n");

		init_seq = &maxim4c->extra_init_seq;
		maxim4c_i2c_load_init_seq(dev,
				init_seq_node, init_seq);

		of_node_put(init_seq_node);
	}

	return 0;
}

static int maxim4c_module_parse_dt(maxim4c_t *maxim4c)
{
	struct device *dev = &maxim4c->client->dev;
	struct device_node *node = NULL;

	// maxim serdes local
	node = of_get_child_by_name(dev->of_node, "serdes-local-device");
	if (IS_ERR_OR_NULL(node))
		return -ENODEV;

	if (!of_device_is_available(node)) {
		dev_info(dev, "%pOF is disabled\n", node);

		of_node_put(node);
		return -ENODEV;
	}

	/* gmsl link parse dt */
	maxim4c_link_parse_dt(maxim4c, node);

	/* video pipe parse dt */
	maxim4c_video_pipe_parse_dt(maxim4c, node);

	/* mipi txphy parse dt */
	maxim4c_mipi_txphy_parse_dt(maxim4c, node);

	/* extra init seq parse dt */
	maxim4c_extra_init_seq_parse(maxim4c, node);

	of_node_put(node);

	return 0;
}

static int maxim4c_run_extra_init_seq(maxim4c_t *maxim4c)
{
	struct i2c_client *client = maxim4c->client;
	struct device *dev = &client->dev;
	int ret = 0;

	ret = maxim4c_i2c_run_init_seq(client,
			&maxim4c->extra_init_seq);
	if (ret) {
		dev_err(dev, "extra init sequence error\n");
		return ret;
	}

	return 0;
}

static int maxim4c_module_hw_previnit(maxim4c_t *maxim4c)
{
	struct i2c_client *client = maxim4c->client;
	int ret = 0;

	// All links disable at beginning.
	ret = maxim4c_i2c_write_byte(client,
			0x0006, MAXIM4C_I2C_REG_ADDR_16BITS,
			0xF0);
	if (ret)
		return ret;

	// MIPI CSI output disable.
	ret = maxim4c_i2c_write_byte(client,
			0x040B, MAXIM4C_I2C_REG_ADDR_16BITS,
			0x00);
	if (ret)
		return ret;

	// MIPI TXPHY standby
	ret = maxim4c_i2c_update_byte(client,
			0x08A2, MAXIM4C_I2C_REG_ADDR_16BITS,
			0xF0, 0x00);
	if (ret)
		return ret;

	return 0;
}

static int maxim4c_module_hw_postinit(maxim4c_t *maxim4c)
{
	struct i2c_client *client = maxim4c->client;
	int ret = 0;

	// video pipe disable all
	ret |= maxim4c_i2c_write_byte(client,
			0x00F4, MAXIM4C_I2C_REG_ADDR_16BITS,
			0);

	// remote control disable all
	ret |= maxim4c_link_select_remote_control(maxim4c, 0);

	return ret;
}

static int maxim4c_module_hw_init(maxim4c_t *maxim4c)
{
	int ret = 0;

	ret = maxim4c_module_hw_previnit(maxim4c);
	if (ret)
		return ret;

	ret = maxim4c_link_hw_init(maxim4c);
	if (ret)
		return ret;

	ret = maxim4c_video_pipe_hw_init(maxim4c);
	if (ret)
		return ret;

	ret = maxim4c_mipi_txphy_hw_init(maxim4c);
	if (ret)
		return ret;

	ret = maxim4c_run_extra_init_seq(maxim4c);
	if (ret)
		return ret;

	ret = maxim4c_module_hw_postinit(maxim4c);
	if (ret)
		return ret;

	return 0;
}

static int maxim4c_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	maxim4c_t *maxim4c = NULL;
	const u32 *chip_id = NULL;
	int ret = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x", DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8, DRIVER_VERSION & 0x00ff);

	chip_id = of_device_get_match_data(dev);
	if (chip_id == NULL) {
		dev_err(dev, "maxim4c driver get match data error\n");
		return -EINVAL;
	}

	if (*chip_id == MAX96712_CHIP_ID) {
		dev_info(dev, "maxim4c driver for max96712");
	} else if (*chip_id == MAX96722_CHIP_ID) {
		dev_info(dev, "maxim4c driver for max96722");
	} else {
		dev_err(dev, "maxim4c driver unknown chip");
		return -EINVAL;
	}

	maxim4c = devm_kzalloc(dev, sizeof(*maxim4c), GFP_KERNEL);
	if (!maxim4c)
		return -ENOMEM;
	maxim4c->client = client;
	maxim4c->chipid = *chip_id;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &maxim4c->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &maxim4c->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &maxim4c->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &maxim4c->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	maxim4c->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(maxim4c->power_gpio))
		dev_warn(dev, "Failed to get power-gpios, maybe no use\n");

	maxim4c->pocen_gpio = devm_gpiod_get(dev, "pocen", GPIOD_OUT_LOW);
	if (IS_ERR(maxim4c->pocen_gpio))
		dev_warn(dev, "Failed to get pocen-gpios\n");

	maxim4c->lock_gpio = devm_gpiod_get(dev, "lock", GPIOD_IN);
	if (IS_ERR(maxim4c->lock_gpio))
		dev_warn(dev, "Failed to get lock-gpios\n");

	mutex_init(&maxim4c->mutex);

	ret = maxim4c_local_device_power_on(maxim4c);
	if (ret)
		goto err_destroy_mutex;

	ret = maxim4c_check_local_chipid(maxim4c);
	if (ret)
		goto err_power_off;

	// client->dev->driver_data = subdev
	// subdev->dev->driver_data = maxim4c
	ret = maxim4c_v4l2_subdev_init(maxim4c);
	if (ret)
		goto err_power_off;

#if MAXIM4C_TEST_PATTERN
	ret = maxim4c_pattern_init(maxim4c);
	if (ret)
		goto err_power_off;

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;
#endif /* MAXIM4C_TEST_PATTERN */

	maxim4c_module_data_init(maxim4c);
	maxim4c_module_parse_dt(maxim4c);

	ret = maxim4c_module_hw_init(maxim4c);
	if (ret)
		goto err_subdev_deinit;

	ret = maxim4c_remote_mfd_add_devices(maxim4c);
	if (ret)
		goto err_subdev_deinit;

	maxim4c_lock_irq_init(maxim4c);
	maxim4c_lock_state_work_init(maxim4c);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_subdev_deinit:
	maxim4c_v4l2_subdev_deinit(maxim4c);
err_power_off:
	maxim4c_local_device_power_off(maxim4c);
err_destroy_mutex:
	mutex_destroy(&maxim4c->mutex);

	return ret;
}

static int maxim4c_remove(struct i2c_client *client)
{
	maxim4c_t *maxim4c = i2c_get_clientdata(client);

	maxim4c_lock_state_work_deinit(maxim4c);

	maxim4c_v4l2_subdev_deinit(maxim4c);

	mutex_destroy(&maxim4c->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		maxim4c_local_device_power_off(maxim4c);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const u32 max96712_chip_id = MAX96712_CHIP_ID;
static const u32 max96722_chip_id = MAX96722_CHIP_ID;

static const struct of_device_id maxim4c_of_match[] = {
	{
		.compatible = MAXIM4C_MAX96712_COMPAT,
		.data = &max96712_chip_id,
	}, {
		.compatible = MAXIM4C_MAX96722_COMPAT,
		.data = &max96722_chip_id,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, maxim4c_of_match);
#endif

static const struct i2c_device_id maxim4c_match_id[] = {
	{ MAXIM4C_MAX96712_COMPAT, 0 },
	{ MAXIM4C_MAX96722_COMPAT, 0 },
	{},
};

static struct i2c_driver maxim4c_i2c_driver = {
	.driver = {
		.name = MAXIM4C_NAME,
		.pm = &maxim4c_pm_ops,
		.of_match_table = of_match_ptr(maxim4c_of_match),
	},
	.probe		= &maxim4c_probe,
	.remove		= &maxim4c_remove,
	.id_table	= maxim4c_match_id,
};

module_i2c_driver(maxim4c_i2c_driver);

MODULE_AUTHOR("Cai wenzhong <cwz@rock-chips.com>");
MODULE_DESCRIPTION("Maxim qual gmsl deserializer driver");
MODULE_LICENSE("GPL");
