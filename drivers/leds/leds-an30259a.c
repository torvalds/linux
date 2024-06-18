// SPDX-License-Identifier: GPL-2.0+
//
// Driver for Panasonic AN30259A 3-channel LED driver
//
// Copyright (c) 2018 Simon Shields <simon@lineageos.org>
//
// Datasheet:
// https://www.alliedelec.com/m/d/a9d2b3ee87c2d1a535a41dd747b1c247.pdf

#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regmap.h>

#define AN30259A_MAX_LEDS 3

#define AN30259A_REG_SRESET 0x00
#define AN30259A_LED_SRESET BIT(0)

/* LED power registers */
#define AN30259A_REG_LED_ON 0x01
#define AN30259A_LED_EN(x) BIT((x) - 1)
#define AN30259A_LED_SLOPE(x) BIT(((x) - 1) + 4)

#define AN30259A_REG_LEDCC(x) (0x03 + ((x) - 1))

/* slope control registers */
#define AN30259A_REG_SLOPE(x) (0x06 + ((x) - 1))
#define AN30259A_LED_SLOPETIME1(x) (x)
#define AN30259A_LED_SLOPETIME2(x) ((x) << 4)

#define AN30259A_REG_LEDCNT1(x) (0x09 + (4 * ((x) - 1)))
#define AN30259A_LED_DUTYMAX(x) ((x) << 4)
#define AN30259A_LED_DUTYMID(x) (x)

#define AN30259A_REG_LEDCNT2(x) (0x0A + (4 * ((x) - 1)))
#define AN30259A_LED_DELAY(x) ((x) << 4)
#define AN30259A_LED_DUTYMIN(x) (x)

/* detention time control (length of each slope step) */
#define AN30259A_REG_LEDCNT3(x) (0x0B + (4 * ((x) - 1)))
#define AN30259A_LED_DT1(x) (x)
#define AN30259A_LED_DT2(x) ((x) << 4)

#define AN30259A_REG_LEDCNT4(x) (0x0C + (4 * ((x) - 1)))
#define AN30259A_LED_DT3(x) (x)
#define AN30259A_LED_DT4(x) ((x) << 4)

#define AN30259A_REG_MAX 0x14

#define AN30259A_BLINK_MAX_TIME 7500 /* ms */
#define AN30259A_SLOPE_RESOLUTION 500 /* ms */

#define AN30259A_NAME "an30259a"

struct an30259a;

struct an30259a_led {
	struct an30259a *chip;
	struct fwnode_handle *fwnode;
	struct led_classdev cdev;
	u32 num;
	enum led_default_state default_state;
	bool sloping;
};

struct an30259a {
	struct mutex mutex; /* held when writing to registers */
	struct i2c_client *client;
	struct an30259a_led leds[AN30259A_MAX_LEDS];
	struct regmap *regmap;
	int num_leds;
};

static int an30259a_brightness_set(struct led_classdev *cdev,
				   enum led_brightness brightness)
{
	struct an30259a_led *led;
	int ret;
	unsigned int led_on;

	led = container_of(cdev, struct an30259a_led, cdev);
	mutex_lock(&led->chip->mutex);

	ret = regmap_read(led->chip->regmap, AN30259A_REG_LED_ON, &led_on);
	if (ret)
		goto error;

	switch (brightness) {
	case LED_OFF:
		led_on &= ~AN30259A_LED_EN(led->num);
		led_on &= ~AN30259A_LED_SLOPE(led->num);
		led->sloping = false;
		break;
	default:
		led_on |= AN30259A_LED_EN(led->num);
		if (led->sloping)
			led_on |= AN30259A_LED_SLOPE(led->num);
		ret = regmap_write(led->chip->regmap,
				   AN30259A_REG_LEDCNT1(led->num),
				   AN30259A_LED_DUTYMAX(0xf) |
				   AN30259A_LED_DUTYMID(0xf));
		if (ret)
			goto error;
		break;
	}

	ret = regmap_write(led->chip->regmap, AN30259A_REG_LED_ON, led_on);
	if (ret)
		goto error;

	ret = regmap_write(led->chip->regmap, AN30259A_REG_LEDCC(led->num),
			   brightness);

error:
	mutex_unlock(&led->chip->mutex);

	return ret;
}

static int an30259a_blink_set(struct led_classdev *cdev,
			      unsigned long *delay_off, unsigned long *delay_on)
{
	struct an30259a_led *led;
	int ret, num;
	unsigned int led_on;
	unsigned long off = *delay_off, on = *delay_on;

	led = container_of(cdev, struct an30259a_led, cdev);

	mutex_lock(&led->chip->mutex);
	num = led->num;

	/* slope time can only be a multiple of 500ms. */
	if (off % AN30259A_SLOPE_RESOLUTION || on % AN30259A_SLOPE_RESOLUTION) {
		ret = -EINVAL;
		goto error;
	}

	/* up to a maximum of 7500ms. */
	if (off > AN30259A_BLINK_MAX_TIME || on > AN30259A_BLINK_MAX_TIME) {
		ret = -EINVAL;
		goto error;
	}

	/* if no blink specified, default to 1 Hz. */
	if (!off && !on) {
		*delay_off = off = 500;
		*delay_on = on = 500;
	}

	/* convert into values the HW will understand. */
	off /= AN30259A_SLOPE_RESOLUTION;
	on /= AN30259A_SLOPE_RESOLUTION;

	/* duty min should be zero (=off), delay should be zero. */
	ret = regmap_write(led->chip->regmap, AN30259A_REG_LEDCNT2(num),
			   AN30259A_LED_DELAY(0) | AN30259A_LED_DUTYMIN(0));
	if (ret)
		goto error;

	/* reset detention time (no "breathing" effect). */
	ret = regmap_write(led->chip->regmap, AN30259A_REG_LEDCNT3(num),
			   AN30259A_LED_DT1(0) | AN30259A_LED_DT2(0));
	if (ret)
		goto error;
	ret = regmap_write(led->chip->regmap, AN30259A_REG_LEDCNT4(num),
			   AN30259A_LED_DT3(0) | AN30259A_LED_DT4(0));
	if (ret)
		goto error;

	/* slope time controls on/off cycle length. */
	ret = regmap_write(led->chip->regmap, AN30259A_REG_SLOPE(num),
			   AN30259A_LED_SLOPETIME1(off) |
			   AN30259A_LED_SLOPETIME2(on));
	if (ret)
		goto error;

	/* Finally, enable slope mode. */
	ret = regmap_read(led->chip->regmap, AN30259A_REG_LED_ON, &led_on);
	if (ret)
		goto error;

	led_on |= AN30259A_LED_SLOPE(num) | AN30259A_LED_EN(led->num);

	ret = regmap_write(led->chip->regmap, AN30259A_REG_LED_ON, led_on);

	if (!ret)
		led->sloping = true;
error:
	mutex_unlock(&led->chip->mutex);

	return ret;
}

static int an30259a_dt_init(struct i2c_client *client,
			    struct an30259a *chip)
{
	struct device_node *np = dev_of_node(&client->dev), *child;
	int count, ret;
	int i = 0;
	struct an30259a_led *led;

	count = of_get_available_child_count(np);
	if (!count || count > AN30259A_MAX_LEDS)
		return -EINVAL;

	for_each_available_child_of_node(np, child) {
		u32 source;

		ret = of_property_read_u32(child, "reg", &source);
		if (ret != 0 || !source || source > AN30259A_MAX_LEDS) {
			dev_err(&client->dev, "Couldn't read LED address: %d\n",
				ret);
			count--;
			continue;
		}

		led = &chip->leds[i];

		led->num = source;
		led->chip = chip;
		led->fwnode = of_fwnode_handle(child);
		led->default_state = led_init_default_state_get(led->fwnode);

		i++;
	}

	if (!count)
		return -EINVAL;

	chip->num_leds = i;

	return 0;
}

static const struct regmap_config an30259a_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = AN30259A_REG_MAX,
};

static void an30259a_init_default_state(struct an30259a_led *led)
{
	struct an30259a *chip = led->chip;
	int led_on, err;

	switch (led->default_state) {
	case LEDS_DEFSTATE_ON:
		led->cdev.brightness = LED_FULL;
		break;
	case LEDS_DEFSTATE_KEEP:
		err = regmap_read(chip->regmap, AN30259A_REG_LED_ON, &led_on);
		if (err)
			break;

		if (!(led_on & AN30259A_LED_EN(led->num))) {
			led->cdev.brightness = LED_OFF;
			break;
		}
		regmap_read(chip->regmap, AN30259A_REG_LEDCC(led->num),
			    &led->cdev.brightness);
		break;
	default:
		led->cdev.brightness = LED_OFF;
	}

	an30259a_brightness_set(&led->cdev, led->cdev.brightness);
}

static int an30259a_probe(struct i2c_client *client)
{
	struct an30259a *chip;
	int i, err;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	err = an30259a_dt_init(client, chip);
	if (err < 0)
		return err;

	err = devm_mutex_init(&client->dev, &chip->mutex);
	if (err)
		return err;

	chip->client = client;
	i2c_set_clientdata(client, chip);

	chip->regmap = devm_regmap_init_i2c(client, &an30259a_regmap_config);

	if (IS_ERR(chip->regmap)) {
		err = PTR_ERR(chip->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			err);
		goto exit;
	}

	for (i = 0; i < chip->num_leds; i++) {
		struct led_init_data init_data = {};

		an30259a_init_default_state(&chip->leds[i]);
		chip->leds[i].cdev.brightness_set_blocking =
			an30259a_brightness_set;
		chip->leds[i].cdev.blink_set = an30259a_blink_set;

		init_data.fwnode = chip->leds[i].fwnode;
		init_data.devicename = AN30259A_NAME;
		init_data.default_label = ":";

		err = devm_led_classdev_register_ext(&client->dev,
						 &chip->leds[i].cdev,
						 &init_data);
		if (err < 0)
			goto exit;
	}
	return 0;

exit:
	return err;
}

static const struct of_device_id an30259a_match_table[] = {
	{ .compatible = "panasonic,an30259a", },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, an30259a_match_table);

static const struct i2c_device_id an30259a_id[] = {
	{ "an30259a", 0 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(i2c, an30259a_id);

static struct i2c_driver an30259a_driver = {
	.driver = {
		.name = "leds-an30259a",
		.of_match_table = an30259a_match_table,
	},
	.probe = an30259a_probe,
	.id_table = an30259a_id,
};

module_i2c_driver(an30259a_driver);

MODULE_AUTHOR("Simon Shields <simon@lineageos.org>");
MODULE_DESCRIPTION("AN30259A LED driver");
MODULE_LICENSE("GPL v2");
