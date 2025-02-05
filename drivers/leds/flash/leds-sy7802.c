// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Silergy SY7802 flash LED driver with an I2C interface
 *
 * Copyright 2024 André Apitzsch <git@apitzsch.eu>
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/led-class-flash.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#define SY7802_MAX_LEDS 2
#define SY7802_LED_JOINT 2

#define SY7802_REG_ENABLE		0x10
#define SY7802_REG_TORCH_BRIGHTNESS	0xa0
#define SY7802_REG_FLASH_BRIGHTNESS	0xb0
#define SY7802_REG_FLASH_DURATION	0xc0
#define SY7802_REG_FLAGS		0xd0
#define SY7802_REG_CONFIG_1		0xe0
#define SY7802_REG_CONFIG_2		0xf0
#define SY7802_REG_VIN_MONITOR		0x80
#define SY7802_REG_LAST_FLASH		0x81
#define SY7802_REG_VLED_MONITOR		0x30
#define SY7802_REG_ADC_DELAY		0x31
#define SY7802_REG_DEV_ID		0xff

#define SY7802_MODE_OFF		0
#define SY7802_MODE_TORCH	2
#define SY7802_MODE_FLASH	3
#define SY7802_MODE_MASK	GENMASK(1, 0)

#define SY7802_LEDS_SHIFT	3
#define SY7802_LEDS_MASK(_id)	(BIT(_id) << SY7802_LEDS_SHIFT)
#define SY7802_LEDS_MASK_ALL	(SY7802_LEDS_MASK(0) | SY7802_LEDS_MASK(1))

#define SY7802_TORCH_CURRENT_SHIFT	3
#define SY7802_TORCH_CURRENT_MASK(_id) \
	(GENMASK(2, 0) << (SY7802_TORCH_CURRENT_SHIFT * (_id)))
#define SY7802_TORCH_CURRENT_MASK_ALL \
	(SY7802_TORCH_CURRENT_MASK(0) | SY7802_TORCH_CURRENT_MASK(1))

#define SY7802_FLASH_CURRENT_SHIFT	4
#define SY7802_FLASH_CURRENT_MASK(_id) \
	(GENMASK(3, 0) << (SY7802_FLASH_CURRENT_SHIFT * (_id)))
#define SY7802_FLASH_CURRENT_MASK_ALL \
	(SY7802_FLASH_CURRENT_MASK(0) | SY7802_FLASH_CURRENT_MASK(1))

#define SY7802_TIMEOUT_DEFAULT_US	512000U
#define SY7802_TIMEOUT_MIN_US		32000U
#define SY7802_TIMEOUT_MAX_US		1024000U
#define SY7802_TIMEOUT_STEPSIZE_US	32000U

#define SY7802_TORCH_BRIGHTNESS_MAX 8

#define SY7802_FLASH_BRIGHTNESS_DEFAULT	14
#define SY7802_FLASH_BRIGHTNESS_MIN	0
#define SY7802_FLASH_BRIGHTNESS_MAX	15
#define SY7802_FLASH_BRIGHTNESS_STEP	1

#define SY7802_FLAG_TIMEOUT			BIT(0)
#define SY7802_FLAG_THERMAL_SHUTDOWN		BIT(1)
#define SY7802_FLAG_LED_FAULT			BIT(2)
#define SY7802_FLAG_TX1_INTERRUPT		BIT(3)
#define SY7802_FLAG_TX2_INTERRUPT		BIT(4)
#define SY7802_FLAG_LED_THERMAL_FAULT		BIT(5)
#define SY7802_FLAG_FLASH_INPUT_VOLTAGE_LOW	BIT(6)
#define SY7802_FLAG_INPUT_VOLTAGE_LOW		BIT(7)

#define SY7802_CHIP_ID	0x51

static const struct reg_default sy7802_regmap_defs[] = {
	{ SY7802_REG_ENABLE, SY7802_LEDS_MASK_ALL },
	{ SY7802_REG_TORCH_BRIGHTNESS, 0x92 },
	{ SY7802_REG_FLASH_BRIGHTNESS, SY7802_FLASH_BRIGHTNESS_DEFAULT |
		SY7802_FLASH_BRIGHTNESS_DEFAULT << SY7802_FLASH_CURRENT_SHIFT },
	{ SY7802_REG_FLASH_DURATION, 0x6f },
	{ SY7802_REG_FLAGS, 0x0 },
	{ SY7802_REG_CONFIG_1, 0x68 },
	{ SY7802_REG_CONFIG_2, 0xf0 },
};

struct sy7802_led {
	struct led_classdev_flash flash;
	struct sy7802 *chip;
	u8 led_id;
};

struct sy7802 {
	struct device *dev;
	struct regmap *regmap;
	struct mutex mutex;

	struct gpio_desc *enable_gpio;
	struct regulator *vin_regulator;

	unsigned int fled_strobe_used;
	unsigned int fled_torch_used;
	unsigned int leds_active;
	int num_leds;
	struct sy7802_led leds[] __counted_by(num_leds);
};

static int sy7802_torch_brightness_set(struct led_classdev *lcdev, enum led_brightness brightness)
{
	struct sy7802_led *led = container_of(lcdev, struct sy7802_led, flash.led_cdev);
	struct sy7802 *chip = led->chip;
	u32 fled_torch_used_tmp;
	u32 led_enable_mask;
	u32 enable_mask;
	u32 torch_mask;
	u32 val;
	int ret;

	mutex_lock(&chip->mutex);

	if (chip->fled_strobe_used) {
		dev_warn(chip->dev, "Cannot set torch brightness whilst strobe is enabled\n");
		ret = -EBUSY;
		goto unlock;
	}

	if (brightness)
		fled_torch_used_tmp = chip->fled_torch_used | BIT(led->led_id);
	else
		fled_torch_used_tmp = chip->fled_torch_used & ~BIT(led->led_id);

	led_enable_mask = led->led_id == SY7802_LED_JOINT ?
			  SY7802_LEDS_MASK_ALL :
			  SY7802_LEDS_MASK(led->led_id);

	val = brightness ? led_enable_mask : SY7802_MODE_OFF;
	if (fled_torch_used_tmp)
		val |= SY7802_MODE_TORCH;

	/* Disable torch to apply brightness */
	ret = regmap_update_bits(chip->regmap, SY7802_REG_ENABLE, SY7802_MODE_MASK,
				 SY7802_MODE_OFF);
	if (ret)
		goto unlock;

	torch_mask = led->led_id == SY7802_LED_JOINT ?
		     SY7802_TORCH_CURRENT_MASK_ALL :
		     SY7802_TORCH_CURRENT_MASK(led->led_id);

	/* Register expects brightness between 0 and MAX_BRIGHTNESS - 1 */
	if (brightness)
		brightness -= 1;

	brightness |= (brightness << SY7802_TORCH_CURRENT_SHIFT);

	ret = regmap_update_bits(chip->regmap, SY7802_REG_TORCH_BRIGHTNESS, torch_mask, brightness);
	if (ret)
		goto unlock;

	enable_mask = SY7802_MODE_MASK | led_enable_mask;
	ret = regmap_update_bits(chip->regmap, SY7802_REG_ENABLE, enable_mask, val);
	if (ret)
		goto unlock;

	chip->fled_torch_used = fled_torch_used_tmp;

unlock:
	mutex_unlock(&chip->mutex);
	return ret;
}

static int sy7802_flash_brightness_set(struct led_classdev_flash *fl_cdev, u32 brightness)
{
	struct sy7802_led *led = container_of(fl_cdev, struct sy7802_led, flash);
	struct led_flash_setting *s = &fl_cdev->brightness;
	u32 val = (brightness - s->min) / s->step;
	struct sy7802 *chip = led->chip;
	u32 flash_mask;
	int ret;

	val |= (val << SY7802_FLASH_CURRENT_SHIFT);
	flash_mask = led->led_id == SY7802_LED_JOINT ?
		     SY7802_FLASH_CURRENT_MASK_ALL :
		     SY7802_FLASH_CURRENT_MASK(led->led_id);

	mutex_lock(&chip->mutex);
	ret = regmap_update_bits(chip->regmap, SY7802_REG_FLASH_BRIGHTNESS, flash_mask, val);
	mutex_unlock(&chip->mutex);

	return ret;
}

static int sy7802_strobe_set(struct led_classdev_flash *fl_cdev, bool state)
{
	struct sy7802_led *led = container_of(fl_cdev, struct sy7802_led, flash);
	struct sy7802 *chip = led->chip;
	u32 fled_strobe_used_tmp;
	u32 led_enable_mask;
	u32 enable_mask;
	u32 val;
	int ret;

	mutex_lock(&chip->mutex);

	if (chip->fled_torch_used) {
		dev_warn(chip->dev, "Cannot set strobe brightness whilst torch is enabled\n");
		ret = -EBUSY;
		goto unlock;
	}

	if (state)
		fled_strobe_used_tmp = chip->fled_strobe_used | BIT(led->led_id);
	else
		fled_strobe_used_tmp = chip->fled_strobe_used & ~BIT(led->led_id);

	led_enable_mask = led->led_id == SY7802_LED_JOINT ?
			  SY7802_LEDS_MASK_ALL :
			  SY7802_LEDS_MASK(led->led_id);

	val = state ? led_enable_mask : SY7802_MODE_OFF;
	if (fled_strobe_used_tmp)
		val |= SY7802_MODE_FLASH;

	enable_mask = SY7802_MODE_MASK | led_enable_mask;
	ret = regmap_update_bits(chip->regmap, SY7802_REG_ENABLE, enable_mask, val);

	if (ret)
		goto unlock;

	chip->fled_strobe_used = fled_strobe_used_tmp;

unlock:
	mutex_unlock(&chip->mutex);
	return ret;
}

static int sy7802_strobe_get(struct led_classdev_flash *fl_cdev, bool *state)
{
	struct sy7802_led *led = container_of(fl_cdev, struct sy7802_led, flash);
	struct sy7802 *chip = led->chip;

	mutex_lock(&chip->mutex);
	*state = !!(chip->fled_strobe_used & BIT(led->led_id));
	mutex_unlock(&chip->mutex);

	return 0;
}

static int sy7802_timeout_set(struct led_classdev_flash *fl_cdev, u32 timeout)
{
	struct sy7802_led *led = container_of(fl_cdev, struct sy7802_led, flash);
	struct led_flash_setting *s = &fl_cdev->timeout;
	u32 val = (timeout - s->min) / s->step;
	struct sy7802 *chip = led->chip;

	return regmap_write(chip->regmap, SY7802_REG_FLASH_DURATION, val);
}

static int sy7802_fault_get(struct led_classdev_flash *fl_cdev, u32 *fault)
{
	struct sy7802_led *led = container_of(fl_cdev, struct sy7802_led, flash);
	struct sy7802 *chip = led->chip;
	u32 val, led_faults = 0;
	int ret;

	/* NOTE: reading register clears fault status */
	ret = regmap_read(chip->regmap, SY7802_REG_FLAGS, &val);
	if (ret)
		return ret;

	if (val & (SY7802_FLAG_FLASH_INPUT_VOLTAGE_LOW | SY7802_FLAG_INPUT_VOLTAGE_LOW))
		led_faults |= LED_FAULT_INPUT_VOLTAGE;

	if (val & SY7802_FLAG_THERMAL_SHUTDOWN)
		led_faults |= LED_FAULT_OVER_TEMPERATURE;

	if (val & SY7802_FLAG_TIMEOUT)
		led_faults |= LED_FAULT_TIMEOUT;

	*fault = led_faults;
	return 0;
}

static const struct led_flash_ops sy7802_flash_ops = {
	.flash_brightness_set = sy7802_flash_brightness_set,
	.strobe_set = sy7802_strobe_set,
	.strobe_get = sy7802_strobe_get,
	.timeout_set = sy7802_timeout_set,
	.fault_get = sy7802_fault_get,
};

static void sy7802_init_flash_brightness(struct led_classdev_flash *fl_cdev)
{
	struct led_flash_setting *s;

	/* Init flash brightness setting */
	s = &fl_cdev->brightness;
	s->min = SY7802_FLASH_BRIGHTNESS_MIN;
	s->max = SY7802_FLASH_BRIGHTNESS_MAX;
	s->step = SY7802_FLASH_BRIGHTNESS_STEP;
	s->val = SY7802_FLASH_BRIGHTNESS_DEFAULT;
}

static void sy7802_init_flash_timeout(struct led_classdev_flash *fl_cdev)
{
	struct led_flash_setting *s;

	/* Init flash timeout setting */
	s = &fl_cdev->timeout;
	s->min = SY7802_TIMEOUT_MIN_US;
	s->max = SY7802_TIMEOUT_MAX_US;
	s->step = SY7802_TIMEOUT_STEPSIZE_US;
	s->val = SY7802_TIMEOUT_DEFAULT_US;
}

static int sy7802_led_register(struct device *dev, struct sy7802_led *led,
			       struct device_node *np)
{
	struct led_init_data init_data = {};
	int ret;

	init_data.fwnode = of_fwnode_handle(np);

	ret = devm_led_classdev_flash_register_ext(dev, &led->flash, &init_data);
	if (ret) {
		dev_err(dev, "Couldn't register flash %d\n", led->led_id);
		return ret;
	}

	return 0;
}

static int sy7802_init_flash_properties(struct device *dev, struct sy7802_led *led,
					struct device_node *np)
{
	struct led_classdev_flash *flash = &led->flash;
	struct led_classdev *lcdev = &flash->led_cdev;
	u32 sources[SY7802_MAX_LEDS];
	int i, num, ret;

	num = of_property_count_u32_elems(np, "led-sources");
	if (num < 1) {
		dev_err(dev, "Not specified or wrong number of led-sources\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_array(np, "led-sources", sources, num);
	if (ret)
		return ret;

	for (i = 0; i < num; i++) {
		if (sources[i] >= SY7802_MAX_LEDS)
			return -EINVAL;
		if (led->chip->leds_active & BIT(sources[i]))
			return -EINVAL;
		led->chip->leds_active |= BIT(sources[i]);
	}

	/* If both channels are specified in 'led-sources', joint flash output mode is used */
	led->led_id = num == 2 ? SY7802_LED_JOINT : sources[0];

	lcdev->max_brightness = SY7802_TORCH_BRIGHTNESS_MAX;
	lcdev->brightness_set_blocking = sy7802_torch_brightness_set;
	lcdev->flags |= LED_DEV_CAP_FLASH;

	flash->ops = &sy7802_flash_ops;

	sy7802_init_flash_brightness(flash);
	sy7802_init_flash_timeout(flash);

	return 0;
}

static int sy7802_chip_check(struct sy7802 *chip)
{
	struct device *dev = chip->dev;
	u32 chipid;
	int ret;

	ret = regmap_read(chip->regmap, SY7802_REG_DEV_ID, &chipid);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read chip ID\n");

	if (chipid != SY7802_CHIP_ID)
		return dev_err_probe(dev, -ENODEV, "Unsupported chip detected: %x\n", chipid);

	return 0;
}

static void sy7802_enable(struct sy7802 *chip)
{
	gpiod_set_value_cansleep(chip->enable_gpio, 1);
	usleep_range(200, 300);
}

static void sy7802_disable(struct sy7802 *chip)
{
	gpiod_set_value_cansleep(chip->enable_gpio, 0);
}

static int sy7802_probe_dt(struct sy7802 *chip)
{
	struct device_node *np = dev_of_node(chip->dev);
	int child_num;
	int ret;

	regmap_write(chip->regmap, SY7802_REG_ENABLE, SY7802_MODE_OFF);
	regmap_write(chip->regmap, SY7802_REG_TORCH_BRIGHTNESS, LED_OFF);

	child_num = 0;
	for_each_available_child_of_node_scoped(np, child) {
		struct sy7802_led *led = chip->leds + child_num;

		led->chip = chip;
		led->led_id = child_num;

		ret = sy7802_init_flash_properties(chip->dev, led, child);
		if (ret)
			return ret;

		ret = sy7802_led_register(chip->dev, led, child);
		if (ret)
			return ret;

		child_num++;
	}
	return 0;
}

static void sy7802_chip_disable_action(void *data)
{
	struct sy7802 *chip = data;

	sy7802_disable(chip);
}

static void sy7802_regulator_disable_action(void *data)
{
	struct sy7802 *chip = data;

	regulator_disable(chip->vin_regulator);
}

static const struct regmap_config sy7802_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,
	.cache_type = REGCACHE_MAPLE,
	.reg_defaults = sy7802_regmap_defs,
	.num_reg_defaults = ARRAY_SIZE(sy7802_regmap_defs),
};

static int sy7802_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct sy7802 *chip;
	size_t count;
	int ret;

	count = device_get_child_node_count(dev);
	if (!count || count > SY7802_MAX_LEDS)
		return dev_err_probe(dev, -EINVAL, "Invalid amount of LED nodes %zu\n", count);

	chip = devm_kzalloc(dev, struct_size(chip, leds, count), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->num_leds = count;

	chip->dev = dev;
	i2c_set_clientdata(client, chip);

	chip->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	ret = PTR_ERR_OR_ZERO(chip->enable_gpio);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request enable gpio\n");

	chip->vin_regulator = devm_regulator_get(dev, "vin");
	ret = PTR_ERR_OR_ZERO(chip->vin_regulator);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request regulator\n");

	ret = regulator_enable(chip->vin_regulator);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable regulator\n");

	ret = devm_add_action_or_reset(dev, sy7802_regulator_disable_action, chip);
	if (ret)
		return ret;

	ret = devm_mutex_init(dev, &chip->mutex);
	if (ret)
		return ret;

	mutex_lock(&chip->mutex);

	chip->regmap = devm_regmap_init_i2c(client, &sy7802_regmap_config);
	if (IS_ERR(chip->regmap)) {
		ret = PTR_ERR(chip->regmap);
		dev_err_probe(dev, ret, "Failed to allocate register map\n");
		goto error;
	}

	ret = sy7802_probe_dt(chip);
	if (ret < 0)
		goto error;

	sy7802_enable(chip);

	ret = devm_add_action_or_reset(dev, sy7802_chip_disable_action, chip);
	if (ret)
		goto error;

	ret = sy7802_chip_check(chip);

error:
	mutex_unlock(&chip->mutex);
	return ret;
}

static const struct of_device_id __maybe_unused sy7802_leds_match[] = {
	{ .compatible = "silergy,sy7802", },
	{}
};
MODULE_DEVICE_TABLE(of, sy7802_leds_match);

static struct i2c_driver sy7802_driver = {
	.driver = {
		.name = "sy7802",
		.of_match_table = of_match_ptr(sy7802_leds_match),
	},
	.probe = sy7802_probe,
};
module_i2c_driver(sy7802_driver);

MODULE_AUTHOR("André Apitzsch <git@apitzsch.eu>");
MODULE_DESCRIPTION("Silergy SY7802 flash LED driver");
MODULE_LICENSE("GPL");
