/*
 * leds-att1272.c -  LED Driver
 *
 * Copyright (C) 2011 Rockchips
 * deng dalong <ddl@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Datasheet: http://www.rohm.com/products/databook/driver/pdf/att1272gu-e.pdf
 *
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/leds.h>
#include <linux/leds-att1272.h>

static int debug;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define dprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	printk(KERN_WARNING"leds-att1272: " fmt , ## arg); } while (0)

#define LEDS_ATT1272_TR(format, ...) printk(KERN_ERR format, ## __VA_ARGS__)
#define LEDS_ATT1272_DG(format, ...) dprintk(0, format, ## __VA_ARGS__)


struct att1272_led {
	struct att1272_led_platform_data	*pdata;
	struct i2c_client		*client;
	struct rw_semaphore		rwsem;
	struct work_struct		work;

	/*
	 * Making led_classdev as array is not recommended, because array
	 * members prevent using 'container_of' macro. So repetitive works
	 * are needed.
	 */
	struct led_classdev		cdev_led;

	/* General attributes of LEDs */
	int flash_safety_timeout;
	int flash_safety_timeout_offset;
	int movie_mode_current;
	int movie_mode_current_offset;
	int flash_to_movie_mode_ratio;
	int flash_to_movie_mode_ratio_offset;
	int flout_config;
	int flout_config_offset;
};

static int att1272_write_byte(struct i2c_client *client, u8 reg, u8 val)
{
    int err,cnt;
	struct att1272_led *led = i2c_get_clientdata(client);
    u8 buf[2];
    struct i2c_msg msg[1];

	gpio_set_value(led->pdata->en_gpio, 0);
    buf[0] = reg & 0xFF;
    buf[1] = val;

    msg->addr = client->addr;
    msg->flags = client->flags;
    msg->buf = buf;
    msg->len = sizeof(buf);
    msg->scl_rate = 100000;         /* ddl@rock-chips.com : 100kHz */
    msg->read_type = 0;               /* fpga i2c:0==I2C_NORMAL : direct use number not enum for don't want include spi_fpga.h */

    cnt = 1;
    err = -EAGAIN;
	gpio_set_value(led->pdata->en_gpio, 1);
    while ((cnt-- > 0) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */
        err = i2c_transfer(client->adapter, msg, 1);

        if (err >= 0) {
			err = 0;
            goto att1272_write_byte_end;
        } else {
        	LEDS_ATT1272_TR("\n write reg(0x%x, val:0x%x) failed, try to write again!\n",reg, val);
	        udelay(10);
        }
    }

att1272_write_byte_end:
	gpio_set_value(led->pdata->en_gpio, 0);
	return err;
}

#define att1272_CONTROL_ATTR(attr_name, name_str,reg_addr, attr_together)			\
static ssize_t att1272_show_##attr_name(struct device *dev,		\
	struct device_attribute *attr, char *buf)			\
{									\
	struct att1272_led *led = i2c_get_clientdata(to_i2c_client(dev));\
	ssize_t ret;							\
	LEDS_ATT1272_DG("%s enter\n",__FUNCTION__);    \
	down_read(&led->rwsem);						\
	ret = sprintf(buf, "0x%02x\n", led->attr_name);			\
	up_read(&led->rwsem);						\
	return ret;							\
}									\
static ssize_t att1272_store_##attr_name(struct device *dev,		\
	struct device_attribute *attr, const char *buf, size_t count)	\
{									\
	struct att1272_led *led = i2c_get_clientdata(to_i2c_client(dev));\
	unsigned long val,val_w;						\
	int ret;							\
	if (!count)							\
		return -EINVAL;						\
	ret = strict_strtoul(buf, 16, &val);				\
	if (ret)							\
		return ret;						\
	down_write(&led->rwsem);					\
	val_w = (val<<led->attr_name##_offset);			\
	val_w |= (led->attr_together<<led->attr_together##_offset);\
    LEDS_ATT1272_DG("%s enter, val:0x%x\n",__FUNCTION__,val_w);    \
	if (att1272_write_byte(led->client, reg_addr, (u8) val) == 0) { \
		led->attr_name = val;						\
	}											\
	up_write(&led->rwsem);						\
	return count;							\
}									\
static struct device_attribute att1272_##attr_name##_attr = {		\
	.attr = {							\
		.name = name_str,					\
		.mode = 0644,						\
		.owner = THIS_MODULE					\
	},								\
	.show = att1272_show_##attr_name,				\
	.store = att1272_store_##attr_name,				\
};

att1272_CONTROL_ATTR(flash_safety_timeout, "flash_safety_timeout",0x00,movie_mode_current);
att1272_CONTROL_ATTR(flash_to_movie_mode_ratio, "flash_to_movie_mode_ratio",0x01,flout_config);
att1272_CONTROL_ATTR(flout_config, "flout_config",0x01,flash_to_movie_mode_ratio);

static struct device_attribute *att1272_attributes[] = {
	&att1272_flash_safety_timeout_attr,
	&att1272_flash_to_movie_mode_ratio_attr,
	&att1272_flout_config_attr,
};

static void att1272_led_work(struct work_struct *work)
{
	struct att1272_led *led = container_of(work, struct att1272_led, work);

	att1272_write_byte(led->client, 0x00, (u8)(led->movie_mode_current<<led->movie_mode_current_offset));
	LEDS_ATT1272_DG();
}

static void att1272_set_led_brightness(struct led_classdev *led_cdev,
					enum led_brightness value)
{
	struct att1272_led *led =
		container_of(led_cdev, struct att1272_led, cdev_led);

	led->movie_mode_current = led->cdev_led.max_brightness - (value>led->cdev_led.max_brightness)?led->cdev_led.max_brightness:value;
	schedule_work(&led->work);
}
static int att1272_set_led_blink(struct led_classdev *led_cdev,
		unsigned long *delay_on, unsigned long *delay_off)
{
	struct att1272_led *led =
		container_of(led_cdev, struct att1272_led, cdev_led);
	if (*delay_on == 0 || *delay_off == 0)
		return -EINVAL;

	schedule_work(&led->work);
	return 0;
}

static int att1272_register_led_classdev(struct att1272_led *led)
{
	int ret;
	struct att1272_led_platform_data *pdata = led->client->dev.platform_data;

	INIT_WORK(&led->work, att1272_led_work);

	led->cdev_led.name = pdata->name;
	led->cdev_led.brightness = LED_OFF;
	led->cdev_led.max_brightness = 15;
	led->cdev_led.brightness_set = att1272_set_led_brightness;
	led->cdev_led.blink_set = att1272_set_led_blink;

	ret = led_classdev_register(&led->client->dev, &led->cdev_led);
	if (ret < 0) {
		LEDS_ATT1272_TR("couldn't register LED %s\n",
							led->cdev_led.name);
		goto failed_unregister_led;
	}


	return 0;

failed_unregister_led:

	return ret;
}

static void att1272_unregister_led_classdev(struct att1272_led *led)
{
	cancel_work_sync(&led->work);
	led_classdev_unregister(&led->cdev_led);
}

static int __devinit att1272_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct att1272_led *led;
	struct att1272_led_platform_data *pdata;
	int ret, i;

	LEDS_ATT1272_DG("%s enter: client->addr:0x%x\n",__FUNCTION__,client->addr);

	led = kzalloc(sizeof(struct att1272_led), GFP_KERNEL);
	if (!led) {
		LEDS_ATT1272_TR("failed to allocate driver data\n");
		return -ENOMEM;
	}

	led->client = client;
	pdata = led->pdata = client->dev.platform_data;
	i2c_set_clientdata(client, led);

	/* Configure EN GPIO  */
	if (gpio_request(pdata->en_gpio, "ATT1272_EN") < 0) {
		LEDS_ATT1272_TR("request en_gpio(%d) is failed!\n",pdata->en_gpio);
		goto failed_free;
	}
	gpio_direction_output(pdata->en_gpio, 0);

	/* Detect att1272 */
	printk("att1272 i2c write.....\n");
	ret = att1272_write_byte(client, 0x01, 0x30);
	ret |= att1272_write_byte(client, 0x00, 0x00);
	if (ret < 0) {
		LEDS_ATT1272_TR("failed to detect device\n");
		goto failed_free;
	}

	/* Default attributes */
	led->flash_safety_timeout = 0x00;
	led->flash_safety_timeout_offset = 0x00;
	led->movie_mode_current = 0x00;
	led->movie_mode_current_offset = 0x04;
	led->flash_to_movie_mode_ratio = 0x03;
	led->flash_to_movie_mode_ratio_offset = 0x03;
	led->flout_config = 0x00;
	led->flout_config_offset = 0x04;

	init_rwsem(&led->rwsem);

	for (i = 0; i < ARRAY_SIZE(att1272_attributes); i++) {
		ret = device_create_file(&led->client->dev,
						att1272_attributes[i]);
		if (ret) {
			LEDS_ATT1272_TR("failed: sysfs file %s\n",
					att1272_attributes[i]->attr.name);
			goto failed_unregister_dev_file;
		}
	}

	ret = att1272_register_led_classdev(led);
	if (ret < 0)
		goto failed_unregister_dev_file;

	return 0;

failed_unregister_dev_file:
	for (i--; i >= 0; i--)
		device_remove_file(&led->client->dev, att1272_attributes[i]);
failed_free:
	i2c_set_clientdata(client, NULL);
	kfree(led);

	return ret;
}

static int __exit att1272_remove(struct i2c_client *client)
{
	struct att1272_led *led = i2c_get_clientdata(client);
	int i;

	gpio_set_value(led->pdata->en_gpio, 0);
	att1272_unregister_led_classdev(led);

	for (i = 0; i < ARRAY_SIZE(att1272_attributes); i++)
		device_remove_file(&led->client->dev, att1272_attributes[i]);
	i2c_set_clientdata(client, NULL);
	kfree(led);

	return 0;
}

static int att1272_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct att1272_led *led = i2c_get_clientdata(client);

	return 0;
}

static int att1272_resume(struct i2c_client *client)
{
	struct att1272_led *led = i2c_get_clientdata(client);

	return 0;
}

static const struct i2c_device_id att1272_id[] = {
	{ "att1272", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, att1272_id);

static struct i2c_driver att1272_i2c_driver = {
	.driver	= {
		.name	= "att1272",
	},
	.probe		= att1272_probe,
	.remove		= __exit_p(att1272_remove),
	.suspend	= att1272_suspend,
	.resume		= att1272_resume,
	.id_table	= att1272_id,
};

static int __init att1272_init(void)
{
	return i2c_add_driver(&att1272_i2c_driver);
}
module_init(att1272_init);

static void __exit att1272_exit(void)
{
	i2c_del_driver(&att1272_i2c_driver);
}
module_exit(att1272_exit);

MODULE_AUTHOR("deng dalong <ddl@rock-chips.com>");
MODULE_DESCRIPTION("att1272 LED driver");
MODULE_LICENSE("GPL v2");

