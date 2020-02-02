// SPDX-License-Identifier: GPL-2.0-only
/*
 * leds-bd2802.c - RGB LED Driver
 *
 * Copyright (C) 2009 Samsung Electronics
 * Kim Kyuwon <q1.kim@samsung.com>
 *
 * Datasheet: http://www.rohm.com/products/databook/driver/pdf/bd2802gu-e.pdf
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/leds.h>
#include <linux/leds-bd2802.h>
#include <linux/slab.h>
#include <linux/pm.h>

#define LED_CTL(rgb2en, rgb1en) ((rgb2en) << 4 | ((rgb1en) << 0))

#define BD2802_LED_OFFSET		0xa
#define BD2802_COLOR_OFFSET		0x3

#define BD2802_REG_CLKSETUP		0x00
#define BD2802_REG_CONTROL		0x01
#define BD2802_REG_HOURSETUP		0x02
#define BD2802_REG_CURRENT1SETUP	0x03
#define BD2802_REG_CURRENT2SETUP	0x04
#define BD2802_REG_WAVEPATTERN		0x05

#define BD2802_CURRENT_032		0x10 /* 3.2mA */
#define BD2802_CURRENT_000		0x00 /* 0.0mA */

#define BD2802_PATTERN_FULL		0x07
#define BD2802_PATTERN_HALF		0x03

enum led_ids {
	LED1,
	LED2,
	LED_NUM,
};

enum led_colors {
	RED,
	GREEN,
	BLUE,
};

enum led_bits {
	BD2802_OFF,
	BD2802_BLINK,
	BD2802_ON,
};

/*
 * State '0' : 'off'
 * State '1' : 'blink'
 * State '2' : 'on'.
 */
struct led_state {
	unsigned r:2;
	unsigned g:2;
	unsigned b:2;
};

struct bd2802_led {
	struct bd2802_led_platform_data	*pdata;
	struct i2c_client		*client;
	struct gpio_desc		*reset;
	struct rw_semaphore		rwsem;

	struct led_state		led[2];

	/*
	 * Making led_classdev as array is not recommended, because array
	 * members prevent using 'container_of' macro. So repetitive works
	 * are needed.
	 */
	struct led_classdev		cdev_led1r;
	struct led_classdev		cdev_led1g;
	struct led_classdev		cdev_led1b;
	struct led_classdev		cdev_led2r;
	struct led_classdev		cdev_led2g;
	struct led_classdev		cdev_led2b;

	/*
	 * Advanced Configuration Function(ADF) mode:
	 * In ADF mode, user can set registers of BD2802GU directly,
	 * therefore BD2802GU doesn't enter reset state.
	 */
	int				adf_on;

	enum led_ids			led_id;
	enum led_colors			color;
	enum led_bits			state;

	/* General attributes of RGB LEDs */
	int				wave_pattern;
	int				rgb_current;
};


/*--------------------------------------------------------------*/
/*	BD2802GU helper functions					*/
/*--------------------------------------------------------------*/

static inline int bd2802_is_rgb_off(struct bd2802_led *led, enum led_ids id,
							enum led_colors color)
{
	switch (color) {
	case RED:
		return !led->led[id].r;
	case GREEN:
		return !led->led[id].g;
	case BLUE:
		return !led->led[id].b;
	default:
		dev_err(&led->client->dev, "%s: Invalid color\n", __func__);
		return -EINVAL;
	}
}

static inline int bd2802_is_led_off(struct bd2802_led *led, enum led_ids id)
{
	if (led->led[id].r || led->led[id].g || led->led[id].b)
		return 0;

	return 1;
}

static inline int bd2802_is_all_off(struct bd2802_led *led)
{
	int i;

	for (i = 0; i < LED_NUM; i++)
		if (!bd2802_is_led_off(led, i))
			return 0;

	return 1;
}

static inline u8 bd2802_get_base_offset(enum led_ids id, enum led_colors color)
{
	return id * BD2802_LED_OFFSET + color * BD2802_COLOR_OFFSET;
}

static inline u8 bd2802_get_reg_addr(enum led_ids id, enum led_colors color,
								u8 reg_offset)
{
	return reg_offset + bd2802_get_base_offset(id, color);
}


/*--------------------------------------------------------------*/
/*	BD2802GU core functions					*/
/*--------------------------------------------------------------*/

static int bd2802_write_byte(struct i2c_client *client, u8 reg, u8 val)
{
	int ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret >= 0)
		return 0;

	dev_err(&client->dev, "%s: reg 0x%x, val 0x%x, err %d\n",
						__func__, reg, val, ret);

	return ret;
}

static void bd2802_update_state(struct bd2802_led *led, enum led_ids id,
				enum led_colors color, enum led_bits led_bit)
{
	int i;
	u8 value;

	for (i = 0; i < LED_NUM; i++) {
		if (i == id) {
			switch (color) {
			case RED:
				led->led[i].r = led_bit;
				break;
			case GREEN:
				led->led[i].g = led_bit;
				break;
			case BLUE:
				led->led[i].b = led_bit;
				break;
			default:
				dev_err(&led->client->dev,
					"%s: Invalid color\n", __func__);
				return;
			}
		}
	}

	if (led_bit == BD2802_BLINK || led_bit == BD2802_ON)
		return;

	if (!bd2802_is_led_off(led, id))
		return;

	if (bd2802_is_all_off(led) && !led->adf_on) {
		gpiod_set_value(led->reset, 1);
		return;
	}

	/*
	 * In this case, other led is turned on, and current led is turned
	 * off. So set RGB LED Control register to stop the current RGB LED
	 */
	value = (id == LED1) ? LED_CTL(1, 0) : LED_CTL(0, 1);
	bd2802_write_byte(led->client, BD2802_REG_CONTROL, value);
}

static void bd2802_configure(struct bd2802_led *led)
{
	struct bd2802_led_platform_data *pdata = led->pdata;
	u8 reg;

	reg = bd2802_get_reg_addr(LED1, RED, BD2802_REG_HOURSETUP);
	bd2802_write_byte(led->client, reg, pdata->rgb_time);

	reg = bd2802_get_reg_addr(LED2, RED, BD2802_REG_HOURSETUP);
	bd2802_write_byte(led->client, reg, pdata->rgb_time);
}

static void bd2802_reset_cancel(struct bd2802_led *led)
{
	gpiod_set_value(led->reset, 0);
	udelay(100);
	bd2802_configure(led);
}

static void bd2802_enable(struct bd2802_led *led, enum led_ids id)
{
	enum led_ids other_led = (id == LED1) ? LED2 : LED1;
	u8 value, other_led_on;

	other_led_on = !bd2802_is_led_off(led, other_led);
	if (id == LED1)
		value = LED_CTL(other_led_on, 1);
	else
		value = LED_CTL(1 , other_led_on);

	bd2802_write_byte(led->client, BD2802_REG_CONTROL, value);
}

static void bd2802_set_on(struct bd2802_led *led, enum led_ids id,
							enum led_colors color)
{
	u8 reg;

	if (bd2802_is_all_off(led) && !led->adf_on)
		bd2802_reset_cancel(led);

	reg = bd2802_get_reg_addr(id, color, BD2802_REG_CURRENT1SETUP);
	bd2802_write_byte(led->client, reg, led->rgb_current);
	reg = bd2802_get_reg_addr(id, color, BD2802_REG_CURRENT2SETUP);
	bd2802_write_byte(led->client, reg, BD2802_CURRENT_000);
	reg = bd2802_get_reg_addr(id, color, BD2802_REG_WAVEPATTERN);
	bd2802_write_byte(led->client, reg, BD2802_PATTERN_FULL);

	bd2802_enable(led, id);
	bd2802_update_state(led, id, color, BD2802_ON);
}

static void bd2802_set_blink(struct bd2802_led *led, enum led_ids id,
							enum led_colors color)
{
	u8 reg;

	if (bd2802_is_all_off(led) && !led->adf_on)
		bd2802_reset_cancel(led);

	reg = bd2802_get_reg_addr(id, color, BD2802_REG_CURRENT1SETUP);
	bd2802_write_byte(led->client, reg, BD2802_CURRENT_000);
	reg = bd2802_get_reg_addr(id, color, BD2802_REG_CURRENT2SETUP);
	bd2802_write_byte(led->client, reg, led->rgb_current);
	reg = bd2802_get_reg_addr(id, color, BD2802_REG_WAVEPATTERN);
	bd2802_write_byte(led->client, reg, led->wave_pattern);

	bd2802_enable(led, id);
	bd2802_update_state(led, id, color, BD2802_BLINK);
}

static void bd2802_turn_on(struct bd2802_led *led, enum led_ids id,
				enum led_colors color, enum led_bits led_bit)
{
	if (led_bit == BD2802_OFF) {
		dev_err(&led->client->dev,
					"Only 'blink' and 'on' are allowed\n");
		return;
	}

	if (led_bit == BD2802_BLINK)
		bd2802_set_blink(led, id, color);
	else
		bd2802_set_on(led, id, color);
}

static void bd2802_turn_off(struct bd2802_led *led, enum led_ids id,
							enum led_colors color)
{
	u8 reg;

	if (bd2802_is_rgb_off(led, id, color))
		return;

	reg = bd2802_get_reg_addr(id, color, BD2802_REG_CURRENT1SETUP);
	bd2802_write_byte(led->client, reg, BD2802_CURRENT_000);
	reg = bd2802_get_reg_addr(id, color, BD2802_REG_CURRENT2SETUP);
	bd2802_write_byte(led->client, reg, BD2802_CURRENT_000);

	bd2802_update_state(led, id, color, BD2802_OFF);
}

#define BD2802_SET_REGISTER(reg_addr, reg_name)				\
static ssize_t bd2802_store_reg##reg_addr(struct device *dev,		\
	struct device_attribute *attr, const char *buf, size_t count)	\
{									\
	struct bd2802_led *led = i2c_get_clientdata(to_i2c_client(dev));\
	unsigned long val;						\
	int ret;							\
	if (!count)							\
		return -EINVAL;						\
	ret = kstrtoul(buf, 16, &val);					\
	if (ret)							\
		return ret;						\
	down_write(&led->rwsem);					\
	bd2802_write_byte(led->client, reg_addr, (u8) val);		\
	up_write(&led->rwsem);						\
	return count;							\
}									\
static struct device_attribute bd2802_reg##reg_addr##_attr = {		\
	.attr = {.name = reg_name, .mode = 0644},			\
	.store = bd2802_store_reg##reg_addr,				\
};

BD2802_SET_REGISTER(0x00, "0x00");
BD2802_SET_REGISTER(0x01, "0x01");
BD2802_SET_REGISTER(0x02, "0x02");
BD2802_SET_REGISTER(0x03, "0x03");
BD2802_SET_REGISTER(0x04, "0x04");
BD2802_SET_REGISTER(0x05, "0x05");
BD2802_SET_REGISTER(0x06, "0x06");
BD2802_SET_REGISTER(0x07, "0x07");
BD2802_SET_REGISTER(0x08, "0x08");
BD2802_SET_REGISTER(0x09, "0x09");
BD2802_SET_REGISTER(0x0a, "0x0a");
BD2802_SET_REGISTER(0x0b, "0x0b");
BD2802_SET_REGISTER(0x0c, "0x0c");
BD2802_SET_REGISTER(0x0d, "0x0d");
BD2802_SET_REGISTER(0x0e, "0x0e");
BD2802_SET_REGISTER(0x0f, "0x0f");
BD2802_SET_REGISTER(0x10, "0x10");
BD2802_SET_REGISTER(0x11, "0x11");
BD2802_SET_REGISTER(0x12, "0x12");
BD2802_SET_REGISTER(0x13, "0x13");
BD2802_SET_REGISTER(0x14, "0x14");
BD2802_SET_REGISTER(0x15, "0x15");

static struct device_attribute *bd2802_addr_attributes[] = {
	&bd2802_reg0x00_attr,
	&bd2802_reg0x01_attr,
	&bd2802_reg0x02_attr,
	&bd2802_reg0x03_attr,
	&bd2802_reg0x04_attr,
	&bd2802_reg0x05_attr,
	&bd2802_reg0x06_attr,
	&bd2802_reg0x07_attr,
	&bd2802_reg0x08_attr,
	&bd2802_reg0x09_attr,
	&bd2802_reg0x0a_attr,
	&bd2802_reg0x0b_attr,
	&bd2802_reg0x0c_attr,
	&bd2802_reg0x0d_attr,
	&bd2802_reg0x0e_attr,
	&bd2802_reg0x0f_attr,
	&bd2802_reg0x10_attr,
	&bd2802_reg0x11_attr,
	&bd2802_reg0x12_attr,
	&bd2802_reg0x13_attr,
	&bd2802_reg0x14_attr,
	&bd2802_reg0x15_attr,
};

static void bd2802_enable_adv_conf(struct bd2802_led *led)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(bd2802_addr_attributes); i++) {
		ret = device_create_file(&led->client->dev,
						bd2802_addr_attributes[i]);
		if (ret) {
			dev_err(&led->client->dev, "failed: sysfs file %s\n",
					bd2802_addr_attributes[i]->attr.name);
			goto failed_remove_files;
		}
	}

	if (bd2802_is_all_off(led))
		bd2802_reset_cancel(led);

	led->adf_on = 1;

	return;

failed_remove_files:
	for (i--; i >= 0; i--)
		device_remove_file(&led->client->dev,
						bd2802_addr_attributes[i]);
}

static void bd2802_disable_adv_conf(struct bd2802_led *led)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(bd2802_addr_attributes); i++)
		device_remove_file(&led->client->dev,
						bd2802_addr_attributes[i]);

	if (bd2802_is_all_off(led))
		gpiod_set_value(led->reset, 1);

	led->adf_on = 0;
}

static ssize_t bd2802_show_adv_conf(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct bd2802_led *led = i2c_get_clientdata(to_i2c_client(dev));
	ssize_t ret;

	down_read(&led->rwsem);
	if (led->adf_on)
		ret = sprintf(buf, "on\n");
	else
		ret = sprintf(buf, "off\n");
	up_read(&led->rwsem);

	return ret;
}

static ssize_t bd2802_store_adv_conf(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct bd2802_led *led = i2c_get_clientdata(to_i2c_client(dev));

	if (!count)
		return -EINVAL;

	down_write(&led->rwsem);
	if (!led->adf_on && !strncmp(buf, "on", 2))
		bd2802_enable_adv_conf(led);
	else if (led->adf_on && !strncmp(buf, "off", 3))
		bd2802_disable_adv_conf(led);
	up_write(&led->rwsem);

	return count;
}

static struct device_attribute bd2802_adv_conf_attr = {
	.attr = {
		.name = "advanced_configuration",
		.mode = 0644,
	},
	.show = bd2802_show_adv_conf,
	.store = bd2802_store_adv_conf,
};

#define BD2802_CONTROL_ATTR(attr_name, name_str)			\
static ssize_t bd2802_show_##attr_name(struct device *dev,		\
	struct device_attribute *attr, char *buf)			\
{									\
	struct bd2802_led *led = i2c_get_clientdata(to_i2c_client(dev));\
	ssize_t ret;							\
	down_read(&led->rwsem);						\
	ret = sprintf(buf, "0x%02x\n", led->attr_name);			\
	up_read(&led->rwsem);						\
	return ret;							\
}									\
static ssize_t bd2802_store_##attr_name(struct device *dev,		\
	struct device_attribute *attr, const char *buf, size_t count)	\
{									\
	struct bd2802_led *led = i2c_get_clientdata(to_i2c_client(dev));\
	unsigned long val;						\
	int ret;							\
	if (!count)							\
		return -EINVAL;						\
	ret = kstrtoul(buf, 16, &val);					\
	if (ret)							\
		return ret;						\
	down_write(&led->rwsem);					\
	led->attr_name = val;						\
	up_write(&led->rwsem);						\
	return count;							\
}									\
static struct device_attribute bd2802_##attr_name##_attr = {		\
	.attr = {							\
		.name = name_str,					\
		.mode = 0644,						\
	},								\
	.show = bd2802_show_##attr_name,				\
	.store = bd2802_store_##attr_name,				\
};

BD2802_CONTROL_ATTR(wave_pattern, "wave_pattern");
BD2802_CONTROL_ATTR(rgb_current, "rgb_current");

static struct device_attribute *bd2802_attributes[] = {
	&bd2802_adv_conf_attr,
	&bd2802_wave_pattern_attr,
	&bd2802_rgb_current_attr,
};

#define BD2802_CONTROL_RGBS(name, id, clr)				\
static int bd2802_set_##name##_brightness(struct led_classdev *led_cdev,\
					enum led_brightness value)	\
{									\
	struct bd2802_led *led =					\
		container_of(led_cdev, struct bd2802_led, cdev_##name);	\
	led->led_id = id;						\
	led->color = clr;						\
	if (value == LED_OFF) {						\
		led->state = BD2802_OFF;				\
		bd2802_turn_off(led, led->led_id, led->color);		\
	} else {							\
		led->state = BD2802_ON;					\
		bd2802_turn_on(led, led->led_id, led->color, BD2802_ON);\
	}								\
	return 0;							\
}									\
static int bd2802_set_##name##_blink(struct led_classdev *led_cdev,	\
		unsigned long *delay_on, unsigned long *delay_off)	\
{									\
	struct bd2802_led *led =					\
		container_of(led_cdev, struct bd2802_led, cdev_##name);	\
	if (*delay_on == 0 || *delay_off == 0)				\
		return -EINVAL;						\
	led->led_id = id;						\
	led->color = clr;						\
	led->state = BD2802_BLINK;					\
	bd2802_turn_on(led, led->led_id, led->color, BD2802_BLINK);	\
	return 0;							\
}

BD2802_CONTROL_RGBS(led1r, LED1, RED);
BD2802_CONTROL_RGBS(led1g, LED1, GREEN);
BD2802_CONTROL_RGBS(led1b, LED1, BLUE);
BD2802_CONTROL_RGBS(led2r, LED2, RED);
BD2802_CONTROL_RGBS(led2g, LED2, GREEN);
BD2802_CONTROL_RGBS(led2b, LED2, BLUE);

static int bd2802_register_led_classdev(struct bd2802_led *led)
{
	int ret;

	led->cdev_led1r.name = "led1_R";
	led->cdev_led1r.brightness = LED_OFF;
	led->cdev_led1r.brightness_set_blocking = bd2802_set_led1r_brightness;
	led->cdev_led1r.blink_set = bd2802_set_led1r_blink;

	ret = led_classdev_register(&led->client->dev, &led->cdev_led1r);
	if (ret < 0) {
		dev_err(&led->client->dev, "couldn't register LED %s\n",
							led->cdev_led1r.name);
		goto failed_unregister_led1_R;
	}

	led->cdev_led1g.name = "led1_G";
	led->cdev_led1g.brightness = LED_OFF;
	led->cdev_led1g.brightness_set_blocking = bd2802_set_led1g_brightness;
	led->cdev_led1g.blink_set = bd2802_set_led1g_blink;

	ret = led_classdev_register(&led->client->dev, &led->cdev_led1g);
	if (ret < 0) {
		dev_err(&led->client->dev, "couldn't register LED %s\n",
							led->cdev_led1g.name);
		goto failed_unregister_led1_G;
	}

	led->cdev_led1b.name = "led1_B";
	led->cdev_led1b.brightness = LED_OFF;
	led->cdev_led1b.brightness_set_blocking = bd2802_set_led1b_brightness;
	led->cdev_led1b.blink_set = bd2802_set_led1b_blink;

	ret = led_classdev_register(&led->client->dev, &led->cdev_led1b);
	if (ret < 0) {
		dev_err(&led->client->dev, "couldn't register LED %s\n",
							led->cdev_led1b.name);
		goto failed_unregister_led1_B;
	}

	led->cdev_led2r.name = "led2_R";
	led->cdev_led2r.brightness = LED_OFF;
	led->cdev_led2r.brightness_set_blocking = bd2802_set_led2r_brightness;
	led->cdev_led2r.blink_set = bd2802_set_led2r_blink;

	ret = led_classdev_register(&led->client->dev, &led->cdev_led2r);
	if (ret < 0) {
		dev_err(&led->client->dev, "couldn't register LED %s\n",
							led->cdev_led2r.name);
		goto failed_unregister_led2_R;
	}

	led->cdev_led2g.name = "led2_G";
	led->cdev_led2g.brightness = LED_OFF;
	led->cdev_led2g.brightness_set_blocking = bd2802_set_led2g_brightness;
	led->cdev_led2g.blink_set = bd2802_set_led2g_blink;

	ret = led_classdev_register(&led->client->dev, &led->cdev_led2g);
	if (ret < 0) {
		dev_err(&led->client->dev, "couldn't register LED %s\n",
							led->cdev_led2g.name);
		goto failed_unregister_led2_G;
	}

	led->cdev_led2b.name = "led2_B";
	led->cdev_led2b.brightness = LED_OFF;
	led->cdev_led2b.brightness_set_blocking = bd2802_set_led2b_brightness;
	led->cdev_led2b.blink_set = bd2802_set_led2b_blink;
	led->cdev_led2b.flags |= LED_CORE_SUSPENDRESUME;

	ret = led_classdev_register(&led->client->dev, &led->cdev_led2b);
	if (ret < 0) {
		dev_err(&led->client->dev, "couldn't register LED %s\n",
							led->cdev_led2b.name);
		goto failed_unregister_led2_B;
	}

	return 0;

failed_unregister_led2_B:
	led_classdev_unregister(&led->cdev_led2g);
failed_unregister_led2_G:
	led_classdev_unregister(&led->cdev_led2r);
failed_unregister_led2_R:
	led_classdev_unregister(&led->cdev_led1b);
failed_unregister_led1_B:
	led_classdev_unregister(&led->cdev_led1g);
failed_unregister_led1_G:
	led_classdev_unregister(&led->cdev_led1r);
failed_unregister_led1_R:

	return ret;
}

static void bd2802_unregister_led_classdev(struct bd2802_led *led)
{
	led_classdev_unregister(&led->cdev_led2b);
	led_classdev_unregister(&led->cdev_led2g);
	led_classdev_unregister(&led->cdev_led2r);
	led_classdev_unregister(&led->cdev_led1b);
	led_classdev_unregister(&led->cdev_led1g);
	led_classdev_unregister(&led->cdev_led1r);
}

static int bd2802_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct bd2802_led *led;
	struct bd2802_led_platform_data *pdata;
	int ret, i;

	led = devm_kzalloc(&client->dev, sizeof(struct bd2802_led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->client = client;
	pdata = led->pdata = dev_get_platdata(&client->dev);
	i2c_set_clientdata(client, led);

	/*
	 * Configure RESET GPIO (L: RESET, H: RESET cancel)
	 *
	 * We request the reset GPIO as OUT_LOW which means de-asserted,
	 * board files specifying this GPIO line in a machine descriptor
	 * table should take care to specify GPIO_ACTIVE_LOW for this line.
	 */
	led->reset = devm_gpiod_get(&client->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(led->reset))
		return PTR_ERR(led->reset);

	/* Tacss = min 0.1ms */
	udelay(100);

	/* Detect BD2802GU */
	ret = bd2802_write_byte(client, BD2802_REG_CLKSETUP, 0x00);
	if (ret < 0) {
		dev_err(&client->dev, "failed to detect device\n");
		return ret;
	} else
		dev_info(&client->dev, "return 0x%02x\n", ret);

	/* To save the power, reset BD2802 after detecting */
	gpiod_set_value(led->reset, 1);

	/* Default attributes */
	led->wave_pattern = BD2802_PATTERN_HALF;
	led->rgb_current = BD2802_CURRENT_032;

	init_rwsem(&led->rwsem);

	for (i = 0; i < ARRAY_SIZE(bd2802_attributes); i++) {
		ret = device_create_file(&led->client->dev,
						bd2802_attributes[i]);
		if (ret) {
			dev_err(&led->client->dev, "failed: sysfs file %s\n",
					bd2802_attributes[i]->attr.name);
			goto failed_unregister_dev_file;
		}
	}

	ret = bd2802_register_led_classdev(led);
	if (ret < 0)
		goto failed_unregister_dev_file;

	return 0;

failed_unregister_dev_file:
	for (i--; i >= 0; i--)
		device_remove_file(&led->client->dev, bd2802_attributes[i]);
	return ret;
}

static int bd2802_remove(struct i2c_client *client)
{
	struct bd2802_led *led = i2c_get_clientdata(client);
	int i;

	gpiod_set_value(led->reset, 1);
	bd2802_unregister_led_classdev(led);
	if (led->adf_on)
		bd2802_disable_adv_conf(led);
	for (i = 0; i < ARRAY_SIZE(bd2802_attributes); i++)
		device_remove_file(&led->client->dev, bd2802_attributes[i]);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static void bd2802_restore_state(struct bd2802_led *led)
{
	int i;

	for (i = 0; i < LED_NUM; i++) {
		if (led->led[i].r)
			bd2802_turn_on(led, i, RED, led->led[i].r);
		if (led->led[i].g)
			bd2802_turn_on(led, i, GREEN, led->led[i].g);
		if (led->led[i].b)
			bd2802_turn_on(led, i, BLUE, led->led[i].b);
	}
}

static int bd2802_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bd2802_led *led = i2c_get_clientdata(client);

	gpiod_set_value(led->reset, 1);

	return 0;
}

static int bd2802_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bd2802_led *led = i2c_get_clientdata(client);

	if (!bd2802_is_all_off(led) || led->adf_on) {
		bd2802_reset_cancel(led);
		bd2802_restore_state(led);
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(bd2802_pm, bd2802_suspend, bd2802_resume);

static const struct i2c_device_id bd2802_id[] = {
	{ "BD2802", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bd2802_id);

static struct i2c_driver bd2802_i2c_driver = {
	.driver	= {
		.name	= "BD2802",
		.pm	= &bd2802_pm,
	},
	.probe		= bd2802_probe,
	.remove		= bd2802_remove,
	.id_table	= bd2802_id,
};

module_i2c_driver(bd2802_i2c_driver);

MODULE_AUTHOR("Kim Kyuwon <q1.kim@samsung.com>");
MODULE_DESCRIPTION("BD2802 LED driver");
MODULE_LICENSE("GPL v2");
