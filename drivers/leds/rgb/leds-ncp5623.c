// SPDX-License-Identifier: GPL-2.0-only
/*
 * NCP5623 Multi-LED Driver
 *
 * Author: Abdel Alkuor <alkuor@gmail.com>
 * Datasheet: https://www.onsemi.com/pdf/datasheet/ncp5623-d.pdf
 */

#include <linux/i2c.h>
#include <linux/module.h>

#include <linux/led-class-multicolor.h>

#define NCP5623_FUNCTION_OFFSET		0x5
#define NCP5623_REG(x)			((x) << NCP5623_FUNCTION_OFFSET)

#define NCP5623_SHUTDOWN_REG		NCP5623_REG(0x0)
#define NCP5623_ILED_REG		NCP5623_REG(0x1)
#define NCP5623_PWM_REG(index)		NCP5623_REG(0x2 + (index))
#define NCP5623_UPWARD_STEP_REG		NCP5623_REG(0x5)
#define NCP5623_DOWNWARD_STEP_REG	NCP5623_REG(0x6)
#define NCP5623_DIMMING_TIME_REG	NCP5623_REG(0x7)

#define NCP5623_MAX_BRIGHTNESS		0x1f
#define NCP5623_MAX_DIM_TIME_MS		240
#define NCP5623_DIM_STEP_MS		8

struct ncp5623 {
	struct i2c_client *client;
	struct led_classdev_mc mc_dev;
	struct mutex lock;

	int current_brightness;
	unsigned long delay;
};

static int ncp5623_write(struct i2c_client *client, u8 reg, u8 data)
{
	return i2c_smbus_write_byte_data(client, reg | data, 0);
}

static int ncp5623_brightness_set(struct led_classdev *cdev,
				  enum led_brightness brightness)
{
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(cdev);
	struct ncp5623 *ncp = container_of(mc_cdev, struct ncp5623, mc_dev);
	int ret;

	guard(mutex)(&ncp->lock);

	if (ncp->delay && time_is_after_jiffies(ncp->delay))
		return -EBUSY;

	ncp->delay = 0;

	for (int i = 0; i < mc_cdev->num_colors; i++) {
		ret = ncp5623_write(ncp->client,
				    NCP5623_PWM_REG(mc_cdev->subled_info[i].channel),
				    min(mc_cdev->subled_info[i].intensity,
					NCP5623_MAX_BRIGHTNESS));
		if (ret)
			return ret;
	}

	ret = ncp5623_write(ncp->client, NCP5623_DIMMING_TIME_REG, 0);
	if (ret)
		return ret;

	ret = ncp5623_write(ncp->client, NCP5623_ILED_REG, brightness);
	if (ret)
		return ret;

	ncp->current_brightness = brightness;

	return 0;
}

static int ncp5623_pattern_set(struct led_classdev *cdev,
			       struct led_pattern *pattern,
			       u32 len, int repeat)
{
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(cdev);
	struct ncp5623 *ncp = container_of(mc_cdev, struct ncp5623, mc_dev);
	int brightness_diff;
	u8 reg;
	int ret;

	guard(mutex)(&ncp->lock);

	if (ncp->delay && time_is_after_jiffies(ncp->delay))
		return -EBUSY;

	ncp->delay = 0;

	if (pattern[0].delta_t > NCP5623_MAX_DIM_TIME_MS ||
	   (pattern[0].delta_t % NCP5623_DIM_STEP_MS) != 0)
		return -EINVAL;

	brightness_diff = pattern[0].brightness - ncp->current_brightness;

	if (brightness_diff == 0)
		return 0;

	if (pattern[0].delta_t) {
		if (brightness_diff > 0)
			reg = NCP5623_UPWARD_STEP_REG;
		else
			reg = NCP5623_DOWNWARD_STEP_REG;
	} else {
		reg = NCP5623_ILED_REG;
	}

	ret = ncp5623_write(ncp->client, reg,
			    min(pattern[0].brightness, NCP5623_MAX_BRIGHTNESS));
	if (ret)
		return ret;

	ret = ncp5623_write(ncp->client,
			    NCP5623_DIMMING_TIME_REG,
			    pattern[0].delta_t / NCP5623_DIM_STEP_MS);
	if (ret)
		return ret;

	/*
	 * During testing, when the brightness difference is 1, for some
	 * unknown reason, the time factor it takes to change to the new
	 * value is the longest time possible. Otherwise, the time factor
	 * is simply the brightness difference.
	 *
	 * For example:
	 * current_brightness = 20 and new_brightness = 21 then the time it
	 * takes to set the new brightness increments to the maximum possible
	 * brightness from 20 then from 0 to 21.
	 * time_factor = max_brightness - 20 + 21
	 */
	if (abs(brightness_diff) == 1)
		ncp->delay = NCP5623_MAX_BRIGHTNESS + brightness_diff;
	else
		ncp->delay = abs(brightness_diff);

	ncp->delay = msecs_to_jiffies(ncp->delay * pattern[0].delta_t) + jiffies;

	ncp->current_brightness = pattern[0].brightness;

	return 0;
}

static int ncp5623_pattern_clear(struct led_classdev *led_cdev)
{
	return 0;
}

static int ncp5623_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct fwnode_handle *mc_node, *led_node;
	struct led_init_data init_data = { };
	int num_subleds = 0;
	struct ncp5623 *ncp;
	struct mc_subled *subled_info;
	u32 color_index;
	u32 reg;
	int ret;

	ncp = devm_kzalloc(dev, sizeof(*ncp), GFP_KERNEL);
	if (!ncp)
		return -ENOMEM;

	ncp->client = client;

	mc_node = device_get_named_child_node(dev, "multi-led");
	if (!mc_node)
		return -EINVAL;

	fwnode_for_each_child_node(mc_node, led_node)
		num_subleds++;

	subled_info = devm_kcalloc(dev, num_subleds, sizeof(*subled_info), GFP_KERNEL);
	if (!subled_info) {
		ret = -ENOMEM;
		goto release_mc_node;
	}

	fwnode_for_each_available_child_node(mc_node, led_node) {
		ret = fwnode_property_read_u32(led_node, "color", &color_index);
		if (ret)
			goto release_led_node;

		ret = fwnode_property_read_u32(led_node, "reg", &reg);
		if (ret)
			goto release_led_node;

		subled_info[ncp->mc_dev.num_colors].channel = reg;
		subled_info[ncp->mc_dev.num_colors++].color_index = color_index;
	}

	init_data.fwnode = mc_node;

	ncp->mc_dev.led_cdev.max_brightness = NCP5623_MAX_BRIGHTNESS;
	ncp->mc_dev.subled_info = subled_info;
	ncp->mc_dev.led_cdev.brightness_set_blocking = ncp5623_brightness_set;
	ncp->mc_dev.led_cdev.pattern_set = ncp5623_pattern_set;
	ncp->mc_dev.led_cdev.pattern_clear = ncp5623_pattern_clear;
	ncp->mc_dev.led_cdev.default_trigger = "pattern";

	mutex_init(&ncp->lock);
	i2c_set_clientdata(client, ncp);

	ret = led_classdev_multicolor_register_ext(dev, &ncp->mc_dev, &init_data);
	if (ret)
		goto destroy_lock;

	return 0;

destroy_lock:
	mutex_destroy(&ncp->lock);

release_mc_node:
	fwnode_handle_put(mc_node);

	return ret;

release_led_node:
	fwnode_handle_put(led_node);
	goto release_mc_node;
}

static void ncp5623_remove(struct i2c_client *client)
{
	struct ncp5623 *ncp = i2c_get_clientdata(client);

	mutex_lock(&ncp->lock);
	ncp->delay = 0;
	mutex_unlock(&ncp->lock);

	ncp5623_write(client, NCP5623_DIMMING_TIME_REG, 0);
	led_classdev_multicolor_unregister(&ncp->mc_dev);
	mutex_destroy(&ncp->lock);
}

static void ncp5623_shutdown(struct i2c_client *client)
{
	struct ncp5623 *ncp = i2c_get_clientdata(client);

	if (!(ncp->mc_dev.led_cdev.flags & LED_RETAIN_AT_SHUTDOWN))
		ncp5623_write(client, NCP5623_SHUTDOWN_REG, 0);

	mutex_destroy(&ncp->lock);
}

static const struct of_device_id ncp5623_id[] = {
	{ .compatible = "onnn,ncp5623" },
	{ }
};
MODULE_DEVICE_TABLE(of, ncp5623_id);

static struct i2c_driver ncp5623_i2c_driver = {
	.driver	= {
		.name = "ncp5623",
		.of_match_table = ncp5623_id,
	},
	.probe = ncp5623_probe,
	.remove = ncp5623_remove,
	.shutdown = ncp5623_shutdown,
};

module_i2c_driver(ncp5623_i2c_driver);

MODULE_AUTHOR("Abdel Alkuor <alkuor@gmail.com>");
MODULE_DESCRIPTION("NCP5623 Multi-LED driver");
MODULE_LICENSE("GPL");
