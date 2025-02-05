// SPDX-License-Identifier: GPL-2.0
/*
 * CZ.NIC's Turris Omnia LEDs driver
 *
 * 2020, 2023, 2024 by Marek Behún <kabel@kernel.org>
 */

#include <linux/i2c.h>
#include <linux/led-class-multicolor.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/turris-omnia-mcu-interface.h>

#define OMNIA_BOARD_LEDS	12
#define OMNIA_LED_NUM_CHANNELS	3

/* MCU controller I2C address 0x2a, needed for detecting MCU features */
#define OMNIA_MCU_I2C_ADDR	0x2a

/**
 * struct omnia_led - per-LED part of driver private data structure
 * @mc_cdev:		multi-color LED class device
 * @subled_info:	per-channel information
 * @cached_channels:	cached values of per-channel brightness that was sent to the MCU
 * @on:			whether the LED was set on
 * @hwtrig:		whether the LED blinking was offloaded to the MCU
 * @reg:		LED identifier to the MCU
 */
struct omnia_led {
	struct led_classdev_mc mc_cdev;
	struct mc_subled subled_info[OMNIA_LED_NUM_CHANNELS];
	u8 cached_channels[OMNIA_LED_NUM_CHANNELS];
	bool on, hwtrig;
	int reg;
};

#define to_omnia_led(l)		container_of(l, struct omnia_led, mc_cdev)

/**
 * struct omnia_leds - driver private data structure
 * @client:			I2C client device
 * @lock:			mutex to protect cached state
 * @has_gamma_correction:	whether the MCU firmware supports gamma correction
 * @brightness_knode:		kernel node of the "brightness" device sysfs attribute (this is the
 *				driver specific global brightness, not the LED classdev brightness)
 * @leds:			flexible array of per-LED data
 */
struct omnia_leds {
	struct i2c_client *client;
	struct mutex lock;
	bool has_gamma_correction;
	struct kernfs_node *brightness_knode;
	struct omnia_led leds[];
};

static int omnia_cmd_set_color(const struct i2c_client *client, u8 led, u8 r, u8 g, u8 b)
{
	u8 buf[5] = { OMNIA_CMD_LED_COLOR, led, r, g, b };

	return omnia_cmd_write(client, buf, sizeof(buf));
}

static int omnia_led_send_color_cmd(const struct i2c_client *client,
				    struct omnia_led *led)
{
	int ret;

	/* Send the color change command */
	ret = omnia_cmd_set_color(client, led->reg, led->subled_info[0].brightness,
				  led->subled_info[1].brightness, led->subled_info[2].brightness);
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
		u8 state = OMNIA_CMD_LED_STATE_LED(led->reg);

		if (brightness)
			state |= OMNIA_CMD_LED_STATE_ON;

		err = omnia_cmd_write_u8(leds->client, OMNIA_CMD_LED_STATE, state);
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
		err = omnia_cmd_write_u8(leds->client, OMNIA_CMD_LED_MODE,
					 OMNIA_CMD_LED_MODE_LED(led->reg));
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
	err = omnia_cmd_write_u8(leds->client, OMNIA_CMD_LED_MODE,
				 OMNIA_CMD_LED_MODE_LED(led->reg) | OMNIA_CMD_LED_MODE_USER);

	mutex_unlock(&leds->lock);

	if (err)
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

	/* Put the LED into software mode */
	ret = omnia_cmd_write_u8(client, OMNIA_CMD_LED_MODE, OMNIA_CMD_LED_MODE_LED(led->reg) |
							     OMNIA_CMD_LED_MODE_USER);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot set LED %pOF to software mode\n", np);

	/* Disable the LED */
	ret = omnia_cmd_write_u8(client, OMNIA_CMD_LED_STATE, OMNIA_CMD_LED_STATE_LED(led->reg));
	if (ret)
		return dev_err_probe(dev, ret, "Cannot set LED %pOF brightness\n", np);

	/* Set initial color and cache it */
	ret = omnia_led_send_color_cmd(client, led);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Cannot set LED %pOF initial color\n", np);

	ret = devm_led_classdev_multicolor_register_ext(dev, &led->mc_cdev,
							&init_data);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Cannot register LED %pOF\n", np);

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
	u8 reply;
	int err;

	err = omnia_cmd_read_u8(client, OMNIA_CMD_GET_BRIGHTNESS, &reply);
	if (err < 0)
		return err;

	return sysfs_emit(buf, "%d\n", reply);
}

static ssize_t brightness_store(struct device *dev, struct device_attribute *a,
				const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long brightness;
	int err;

	if (kstrtoul(buf, 10, &brightness))
		return -EINVAL;

	if (brightness > 100)
		return -EINVAL;

	err = omnia_cmd_write_u8(client, OMNIA_CMD_SET_BRIGHTNESS, brightness);

	return err ?: count;
}
static DEVICE_ATTR_RW(brightness);

static ssize_t gamma_correction_show(struct device *dev,
				     struct device_attribute *a, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct omnia_leds *leds = i2c_get_clientdata(client);
	u8 reply = 0;
	int err;

	if (leds->has_gamma_correction) {
		err = omnia_cmd_read_u8(client, OMNIA_CMD_GET_GAMMA_CORRECTION, &reply);
		if (err < 0)
			return err;
	}

	return sysfs_emit(buf, "%d\n", !!reply);
}

static ssize_t gamma_correction_store(struct device *dev,
				      struct device_attribute *a,
				      const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct omnia_leds *leds = i2c_get_clientdata(client);
	bool val;
	int err;

	if (!leds->has_gamma_correction)
		return -EOPNOTSUPP;

	if (kstrtobool(buf, &val) < 0)
		return -EINVAL;

	err = omnia_cmd_write_u8(client, OMNIA_CMD_SET_GAMMA_CORRECTION, val);

	return err ?: count;
}
static DEVICE_ATTR_RW(gamma_correction);

static struct attribute *omnia_led_controller_attrs[] = {
	&dev_attr_brightness.attr,
	&dev_attr_gamma_correction.attr,
	NULL,
};
ATTRIBUTE_GROUPS(omnia_led_controller);

static irqreturn_t omnia_brightness_changed_threaded_fn(int irq, void *data)
{
	struct omnia_leds *leds = data;

	if (unlikely(!leds->brightness_knode)) {
		/*
		 * Note that sysfs_get_dirent() may sleep. This is okay, because we are in threaded
		 * context.
		 */
		leds->brightness_knode = sysfs_get_dirent(leds->client->dev.kobj.sd, "brightness");
		if (!leds->brightness_knode)
			return IRQ_NONE;
	}

	sysfs_notify_dirent(leds->brightness_knode);

	return IRQ_HANDLED;
}

static void omnia_brightness_knode_put(void *data)
{
	struct omnia_leds *leds = data;

	if (leds->brightness_knode)
		sysfs_put(leds->brightness_knode);
}

static int omnia_request_brightness_irq(struct omnia_leds *leds)
{
	struct device *dev = &leds->client->dev;
	int ret;

	if (!leds->client->irq) {
		dev_info(dev,
			 "Brightness change interrupt supported by MCU firmware but not described in device-tree\n");

		return 0;
	}

	/*
	 * Registering the brightness_knode destructor before requesting the IRQ ensures that on
	 * removal the brightness_knode sysfs node is put only after the IRQ is freed.
	 * This is needed because the interrupt handler uses the knode.
	 */
	ret = devm_add_action(dev, omnia_brightness_knode_put, leds);
	if (ret < 0)
		return ret;

	return devm_request_threaded_irq(dev, leds->client->irq, NULL,
					 omnia_brightness_changed_threaded_fn, IRQF_ONESHOT,
					 "leds-turris-omnia", leds);
}

static int omnia_mcu_get_features(const struct i2c_client *mcu_client)
{
	u16 reply;
	int err;

	err = omnia_cmd_read_u16(mcu_client, OMNIA_CMD_GET_STATUS_WORD, &reply);
	if (err)
		return err;

	/* Check whether MCU firmware supports the OMNIA_CMD_GET_FEAUTRES command */
	if (!(reply & OMNIA_STS_FEATURES_SUPPORTED))
		return 0;

	err = omnia_cmd_read_u16(mcu_client, OMNIA_CMD_GET_FEATURES, &reply);
	if (err)
		return err;

	return reply;
}

static int omnia_match_mcu_client(struct device *dev, const void *data)
{
	struct i2c_client *client;

	client = i2c_verify_client(dev);
	if (!client)
		return 0;

	return client->addr == OMNIA_MCU_I2C_ADDR;
}

static int omnia_find_mcu_and_get_features(struct device *dev)
{
	struct device *mcu_dev;
	int ret;

	mcu_dev = device_find_child(dev->parent, NULL, omnia_match_mcu_client);
	if (!mcu_dev)
		return -ENODEV;

	ret = omnia_mcu_get_features(i2c_verify_client(mcu_dev));

	put_device(mcu_dev);

	return ret;
}

static int omnia_leds_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *np = dev_of_node(dev);
	struct omnia_leds *leds;
	struct omnia_led *led;
	int ret, count;

	count = of_get_available_child_count(np);
	if (count == 0)
		return dev_err_probe(dev, -ENODEV, "LEDs are not defined in device tree!\n");
	if (count > OMNIA_BOARD_LEDS)
		return dev_err_probe(dev, -EINVAL, "Too many LEDs defined in device tree!\n");

	leds = devm_kzalloc(dev, struct_size(leds, leds, count), GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	leds->client = client;
	i2c_set_clientdata(client, leds);

	ret = omnia_find_mcu_and_get_features(dev);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Cannot determine MCU supported features\n");

	leds->has_gamma_correction = ret & OMNIA_FEAT_LED_GAMMA_CORRECTION;

	if (ret & OMNIA_FEAT_BRIGHTNESS_INT) {
		ret = omnia_request_brightness_irq(leds);
		if (ret < 0)
			return dev_err_probe(dev, ret, "Cannot request brightness IRQ\n");
	}

	mutex_init(&leds->lock);

	ret = devm_led_trigger_register(dev, &omnia_hw_trigger);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Cannot register private LED trigger\n");

	led = &leds->leds[0];
	for_each_available_child_of_node_scoped(np, child) {
		ret = omnia_led_register(client, led, child);
		if (ret < 0)
			return ret;

		led += ret;
	}

	return 0;
}

static void omnia_leds_remove(struct i2c_client *client)
{
	/* Put all LEDs into default (HW triggered) mode */
	omnia_cmd_write_u8(client, OMNIA_CMD_LED_MODE, OMNIA_CMD_LED_MODE_LED(OMNIA_BOARD_LEDS));

	/* Set all LEDs color to [255, 255, 255] */
	omnia_cmd_set_color(client, OMNIA_BOARD_LEDS, 255, 255, 255);
}

static const struct of_device_id of_omnia_leds_match[] = {
	{ .compatible = "cznic,turris-omnia-leds", },
	{},
};
MODULE_DEVICE_TABLE(of, of_omnia_leds_match);

static const struct i2c_device_id omnia_id[] = {
	{ "omnia" },
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
