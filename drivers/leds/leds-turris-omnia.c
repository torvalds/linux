// SPDX-License-Identifier: GPL-2.0
/*
 * CZ.NIC's Turris Omnia LEDs driver
 *
 * 2020, 2023 by Marek Behún <kabel@kernel.org>
 */

#include <linux/i2c.h>
#include <linux/led-class-multicolor.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include "leds.h"

#define OMNIA_BOARD_LEDS	12
#define OMNIA_LED_NUM_CHANNELS	3

/* MCU controller commands at I2C address 0x2a */
#define OMNIA_MCU_I2C_ADDR		0x2a

#define CMD_GET_STATUS_WORD		0x01
#define STS_FEATURES_SUPPORTED		BIT(2)

#define CMD_GET_FEATURES		0x10
#define FEAT_LED_GAMMA_CORRECTION	BIT(5)

/* LED controller commands at I2C address 0x2b */
#define CMD_LED_MODE			0x03
#define CMD_LED_MODE_LED(l)		((l) & 0x0f)
#define CMD_LED_MODE_USER		0x10

#define CMD_LED_STATE			0x04
#define CMD_LED_STATE_LED(l)		((l) & 0x0f)
#define CMD_LED_STATE_ON		0x10

#define CMD_LED_COLOR			0x05
#define CMD_LED_SET_BRIGHTNESS		0x07
#define CMD_LED_GET_BRIGHTNESS		0x08

#define CMD_SET_GAMMA_CORRECTION	0x30
#define CMD_GET_GAMMA_CORRECTION	0x31

struct omnia_led {
	struct led_classdev_mc mc_cdev;
	struct mc_subled subled_info[OMNIA_LED_NUM_CHANNELS];
	u8 cached_channels[OMNIA_LED_NUM_CHANNELS];
	bool on, hwtrig;
	int reg;
};

#define to_omnia_led(l)		container_of(l, struct omnia_led, mc_cdev)

struct omnia_leds {
	struct i2c_client *client;
	struct mutex lock;
	bool has_gamma_correction;
	struct omnia_led leds[];
};

static int omnia_cmd_write_u8(const struct i2c_client *client, u8 cmd, u8 val)
{
	u8 buf[2] = { cmd, val };

	return i2c_master_send(client, buf, sizeof(buf));
}

static int omnia_cmd_read_raw(struct i2c_adapter *adapter, u8 addr, u8 cmd,
			      void *reply, size_t len)
{
	struct i2c_msg msgs[2];
	int ret;

	msgs[0].addr = addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &cmd;
	msgs[1].addr = addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = reply;

	ret = i2c_transfer(adapter, msgs, ARRAY_SIZE(msgs));
	if (likely(ret == ARRAY_SIZE(msgs)))
		return len;
	else if (ret < 0)
		return ret;
	else
		return -EIO;
}

static int omnia_cmd_read_u8(const struct i2c_client *client, u8 cmd)
{
	u8 reply;
	int ret;

	ret = omnia_cmd_read_raw(client->adapter, client->addr, cmd, &reply, 1);
	if (ret < 0)
		return ret;

	return reply;
}

static int omnia_led_send_color_cmd(const struct i2c_client *client,
				    struct omnia_led *led)
{
	char cmd[5];
	int ret;

	cmd[0] = CMD_LED_COLOR;
	cmd[1] = led->reg;
	cmd[2] = led->subled_info[0].brightness;
	cmd[3] = led->subled_info[1].brightness;
	cmd[4] = led->subled_info[2].brightness;

	/* Send the color change command */
	ret = i2c_master_send(client, cmd, 5);
	if (ret < 0)
		return ret;

	/* Cache the RGB channel brightnesses */
	for (int i = 0; i < OMNIA_LED_NUM_CHANNELS; ++i)
		led->cached_channels[i] = led->subled_info[i].brightness;

	return 0;
}

/* Determine if the computed RGB channels are different from the cached ones */
static bool omnia_led_channels_changed(struct omnia_led *led)
{
	for (int i = 0; i < OMNIA_LED_NUM_CHANNELS; ++i)
		if (led->subled_info[i].brightness != led->cached_channels[i])
			return true;

	return false;
}

static int omnia_led_brightness_set_blocking(struct led_classdev *cdev,
					     enum led_brightness brightness)
{
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(cdev);
	struct omnia_leds *leds = dev_get_drvdata(cdev->dev->parent);
	struct omnia_led *led = to_omnia_led(mc_cdev);
	int err = 0;

	mutex_lock(&leds->lock);

	/*
	 * Only recalculate RGB brightnesses from intensities if brightness is
	 * non-zero (if it is zero and the LED is in HW blinking mode, we use
	 * max_brightness as brightness). Otherwise we won't be using them and
	 * we can save ourselves some software divisions (Omnia's CPU does not
	 * implement the division instruction).
	 */
	if (brightness || led->hwtrig) {
		led_mc_calc_color_components(mc_cdev, brightness ?:
						      cdev->max_brightness);

		/*
		 * Send color command only if brightness is non-zero and the RGB
		 * channel brightnesses changed.
		 */
		if (omnia_led_channels_changed(led))
			err = omnia_led_send_color_cmd(leds->client, led);
	}

	/*
	 * Send on/off state change only if (bool)brightness changed and the LED
	 * is not being blinked by HW.
	 */
	if (!err && !led->hwtrig && !brightness != !led->on) {
		u8 state = CMD_LED_STATE_LED(led->reg);

		if (brightness)
			state |= CMD_LED_STATE_ON;

		err = omnia_cmd_write_u8(leds->client, CMD_LED_STATE, state);
		if (!err)
			led->on = !!brightness;
	}

	mutex_unlock(&leds->lock);

	return err;
}

static struct led_hw_trigger_type omnia_hw_trigger_type;

static int omnia_hwtrig_activate(struct led_classdev *cdev)
{
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(cdev);
	struct omnia_leds *leds = dev_get_drvdata(cdev->dev->parent);
	struct omnia_led *led = to_omnia_led(mc_cdev);
	int err = 0;

	mutex_lock(&leds->lock);

	if (!led->on) {
		/*
		 * If the LED is off (brightness was set to 0), the last
		 * configured color was not necessarily sent to the MCU.
		 * Recompute with max_brightness and send if needed.
		 */
		led_mc_calc_color_components(mc_cdev, cdev->max_brightness);

		if (omnia_led_channels_changed(led))
			err = omnia_led_send_color_cmd(leds->client, led);
	}

	if (!err) {
		/* Put the LED into MCU controlled mode */
		err = omnia_cmd_write_u8(leds->client, CMD_LED_MODE,
					 CMD_LED_MODE_LED(led->reg));
		if (!err)
			led->hwtrig = true;
	}

	mutex_unlock(&leds->lock);

	return err;
}

static void omnia_hwtrig_deactivate(struct led_classdev *cdev)
{
	struct omnia_leds *leds = dev_get_drvdata(cdev->dev->parent);
	struct omnia_led *led = to_omnia_led(lcdev_to_mccdev(cdev));
	int err;

	mutex_lock(&leds->lock);

	led->hwtrig = false;

	/* Put the LED into software mode */
	err = omnia_cmd_write_u8(leds->client, CMD_LED_MODE,
				 CMD_LED_MODE_LED(led->reg) |
				 CMD_LED_MODE_USER);

	mutex_unlock(&leds->lock);

	if (err < 0)
		dev_err(cdev->dev, "Cannot put LED to software mode: %i\n",
			err);
}

static struct led_trigger omnia_hw_trigger = {
	.name		= "omnia-mcu",
	.activate	= omnia_hwtrig_activate,
	.deactivate	= omnia_hwtrig_deactivate,
	.trigger_type	= &omnia_hw_trigger_type,
};

static int omnia_led_register(struct i2c_client *client, struct omnia_led *led,
			      struct device_node *np)
{
	struct led_init_data init_data = {};
	struct device *dev = &client->dev;
	struct led_classdev *cdev;
	int ret, color;

	ret = of_property_read_u32(np, "reg", &led->reg);
	if (ret || led->reg >= OMNIA_BOARD_LEDS) {
		dev_warn(dev,
			 "Node %pOF: must contain 'reg' property with values between 0 and %i\n",
			 np, OMNIA_BOARD_LEDS - 1);
		return 0;
	}

	ret = of_property_read_u32(np, "color", &color);
	if (ret || color != LED_COLOR_ID_RGB) {
		dev_warn(dev,
			 "Node %pOF: must contain 'color' property with value LED_COLOR_ID_RGB\n",
			 np);
		return 0;
	}

	led->subled_info[0].color_index = LED_COLOR_ID_RED;
	led->subled_info[1].color_index = LED_COLOR_ID_GREEN;
	led->subled_info[2].color_index = LED_COLOR_ID_BLUE;

	/* Initial color is white */
	for (int i = 0; i < OMNIA_LED_NUM_CHANNELS; ++i) {
		led->subled_info[i].intensity = 255;
		led->subled_info[i].brightness = 255;
		led->subled_info[i].channel = i;
	}

	led->mc_cdev.subled_info = led->subled_info;
	led->mc_cdev.num_colors = OMNIA_LED_NUM_CHANNELS;

	init_data.fwnode = &np->fwnode;

	cdev = &led->mc_cdev.led_cdev;
	cdev->max_brightness = 255;
	cdev->brightness_set_blocking = omnia_led_brightness_set_blocking;
	cdev->trigger_type = &omnia_hw_trigger_type;
	/*
	 * Use the omnia-mcu trigger as the default trigger. It may be rewritten
	 * by LED class from the linux,default-trigger property.
	 */
	cdev->default_trigger = omnia_hw_trigger.name;

	/* put the LED into software mode */
	ret = omnia_cmd_write_u8(client, CMD_LED_MODE,
				 CMD_LED_MODE_LED(led->reg) |
				 CMD_LED_MODE_USER);
	if (ret < 0) {
		dev_err(dev, "Cannot set LED %pOF to software mode: %i\n", np,
			ret);
		return ret;
	}

	/* disable the LED */
	ret = omnia_cmd_write_u8(client, CMD_LED_STATE,
				 CMD_LED_STATE_LED(led->reg));
	if (ret < 0) {
		dev_err(dev, "Cannot set LED %pOF brightness: %i\n", np, ret);
		return ret;
	}

	/* Set initial color and cache it */
	ret = omnia_led_send_color_cmd(client, led);
	if (ret < 0) {
		dev_err(dev, "Cannot set LED %pOF initial color: %i\n", np,
			ret);
		return ret;
	}

	ret = devm_led_classdev_multicolor_register_ext(dev, &led->mc_cdev,
							&init_data);
	if (ret < 0) {
		dev_err(dev, "Cannot register LED %pOF: %i\n", np, ret);
		return ret;
	}

	return 1;
}

/*
 * On the front panel of the Turris Omnia router there is also a button which
 * can be used to control the intensity of all the LEDs at once, so that if they
 * are too bright, user can dim them.
 * The microcontroller cycles between 8 levels of this global brightness (from
 * 100% to 0%), but this setting can have any integer value between 0 and 100.
 * It is therefore convenient to be able to change this setting from software.
 * We expose this setting via a sysfs attribute file called "brightness". This
 * file lives in the device directory of the LED controller, not an individual
 * LED, so it should not confuse users.
 */
static ssize_t brightness_show(struct device *dev, struct device_attribute *a,
			       char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	int ret;

	ret = omnia_cmd_read_u8(client, CMD_LED_GET_BRIGHTNESS);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%d\n", ret);
}

static ssize_t brightness_store(struct device *dev, struct device_attribute *a,
				const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long brightness;
	int ret;

	if (kstrtoul(buf, 10, &brightness))
		return -EINVAL;

	if (brightness > 100)
		return -EINVAL;

	ret = omnia_cmd_write_u8(client, CMD_LED_SET_BRIGHTNESS, brightness);

	return ret < 0 ? ret : count;
}
static DEVICE_ATTR_RW(brightness);

static ssize_t gamma_correction_show(struct device *dev,
				     struct device_attribute *a, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct omnia_leds *leds = i2c_get_clientdata(client);
	int ret;

	if (leds->has_gamma_correction) {
		ret = omnia_cmd_read_u8(client, CMD_GET_GAMMA_CORRECTION);
		if (ret < 0)
			return ret;
	} else {
		ret = 0;
	}

	return sysfs_emit(buf, "%d\n", !!ret);
}

static ssize_t gamma_correction_store(struct device *dev,
				      struct device_attribute *a,
				      const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct omnia_leds *leds = i2c_get_clientdata(client);
	bool val;
	int ret;

	if (!leds->has_gamma_correction)
		return -EOPNOTSUPP;

	if (kstrtobool(buf, &val) < 0)
		return -EINVAL;

	ret = omnia_cmd_write_u8(client, CMD_SET_GAMMA_CORRECTION, val);

	return ret < 0 ? ret : count;
}
static DEVICE_ATTR_RW(gamma_correction);

static struct attribute *omnia_led_controller_attrs[] = {
	&dev_attr_brightness.attr,
	&dev_attr_gamma_correction.attr,
	NULL,
};
ATTRIBUTE_GROUPS(omnia_led_controller);

static int omnia_mcu_get_features(const struct i2c_client *client)
{
	u16 reply;
	int err;

	err = omnia_cmd_read_raw(client->adapter, OMNIA_MCU_I2C_ADDR,
				 CMD_GET_STATUS_WORD, &reply, sizeof(reply));
	if (err < 0)
		return err;

	/* Check whether MCU firmware supports the CMD_GET_FEAUTRES command */
	if (!(le16_to_cpu(reply) & STS_FEATURES_SUPPORTED))
		return 0;

	err = omnia_cmd_read_raw(client->adapter, OMNIA_MCU_I2C_ADDR,
				 CMD_GET_FEATURES, &reply, sizeof(reply));
	if (err < 0)
		return err;

	return le16_to_cpu(reply);
}

static int omnia_leds_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *np = dev_of_node(dev), *child;
	struct omnia_leds *leds;
	struct omnia_led *led;
	int ret, count;

	count = of_get_available_child_count(np);
	if (!count) {
		dev_err(dev, "LEDs are not defined in device tree!\n");
		return -ENODEV;
	} else if (count > OMNIA_BOARD_LEDS) {
		dev_err(dev, "Too many LEDs defined in device tree!\n");
		return -EINVAL;
	}

	leds = devm_kzalloc(dev, struct_size(leds, leds, count), GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	leds->client = client;
	i2c_set_clientdata(client, leds);

	ret = omnia_mcu_get_features(client);
	if (ret < 0) {
		dev_err(dev, "Cannot determine MCU supported features: %d\n",
			ret);
		return ret;
	}

	leds->has_gamma_correction = ret & FEAT_LED_GAMMA_CORRECTION;
	if (!leds->has_gamma_correction) {
		dev_info(dev,
			 "Your board's MCU firmware does not support the LED gamma correction feature.\n");
		dev_info(dev,
			 "Consider upgrading MCU firmware with the omnia-mcutool utility.\n");
	}

	mutex_init(&leds->lock);

	ret = devm_led_trigger_register(dev, &omnia_hw_trigger);
	if (ret < 0) {
		dev_err(dev, "Cannot register private LED trigger: %d\n", ret);
		return ret;
	}

	led = &leds->leds[0];
	for_each_available_child_of_node(np, child) {
		ret = omnia_led_register(client, led, child);
		if (ret < 0) {
			of_node_put(child);
			return ret;
		}

		led += ret;
	}

	return 0;
}

static void omnia_leds_remove(struct i2c_client *client)
{
	u8 buf[5];

	/* put all LEDs into default (HW triggered) mode */
	omnia_cmd_write_u8(client, CMD_LED_MODE,
			   CMD_LED_MODE_LED(OMNIA_BOARD_LEDS));

	/* set all LEDs color to [255, 255, 255] */
	buf[0] = CMD_LED_COLOR;
	buf[1] = OMNIA_BOARD_LEDS;
	buf[2] = 255;
	buf[3] = 255;
	buf[4] = 255;

	i2c_master_send(client, buf, 5);
}

static const struct of_device_id of_omnia_leds_match[] = {
	{ .compatible = "cznic,turris-omnia-leds", },
	{},
};

static const struct i2c_device_id omnia_id[] = {
	{ "omnia", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, omnia_id);

static struct i2c_driver omnia_leds_driver = {
	.probe		= omnia_leds_probe,
	.remove		= omnia_leds_remove,
	.id_table	= omnia_id,
	.driver		= {
		.name	= "leds-turris-omnia",
		.of_match_table = of_omnia_leds_match,
		.dev_groups = omnia_led_controller_groups,
	},
};

module_i2c_driver(omnia_leds_driver);

MODULE_AUTHOR("Marek Behun <kabel@kernel.org>");
MODULE_DESCRIPTION("CZ.NIC's Turris Omnia LEDs");
MODULE_LICENSE("GPL v2");
