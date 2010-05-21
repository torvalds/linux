/*
 *  LCD / Backlight control code for Sharp SL-6000x (tosa)
 *
 *  Copyright (c) 2005		Dirk Opfer
 *  Copyright (c) 2007,2008	Dmitry Baryshkov
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/slab.h>

#include <asm/mach/sharpsl_param.h>

#include <mach/tosa.h>

#define COMADJ_DEFAULT	97

#define DAC_CH1		0
#define DAC_CH2		1

struct tosa_bl_data {
	struct i2c_client *i2c;
	struct backlight_device *bl;

	int comadj;
};

static void tosa_bl_set_backlight(struct tosa_bl_data *data, int brightness)
{
	struct spi_device *spi = data->i2c->dev.platform_data;

	i2c_smbus_write_byte_data(data->i2c, DAC_CH1, data->comadj);

	/* SetBacklightDuty */
	i2c_smbus_write_byte_data(data->i2c, DAC_CH2, (u8)(brightness & 0xff));

	/* SetBacklightVR */
	gpio_set_value(TOSA_GPIO_BL_C20MA, brightness & 0x100);

	tosa_bl_enable(spi, brightness);
}

static int tosa_bl_update_status(struct backlight_device *dev)
{
	struct backlight_properties *props = &dev->props;
	struct tosa_bl_data *data = dev_get_drvdata(&dev->dev);
	int power = max(props->power, props->fb_blank);
	int brightness = props->brightness;

	if (power)
		brightness = 0;

	tosa_bl_set_backlight(data, brightness);

	return 0;
}

static int tosa_bl_get_brightness(struct backlight_device *dev)
{
	struct backlight_properties *props = &dev->props;

	return props->brightness;
}

static const struct backlight_ops bl_ops = {
	.get_brightness		= tosa_bl_get_brightness,
	.update_status		= tosa_bl_update_status,
};

static int __devinit tosa_bl_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct backlight_properties props;
	struct tosa_bl_data *data = kzalloc(sizeof(struct tosa_bl_data), GFP_KERNEL);
	int ret = 0;
	if (!data)
		return -ENOMEM;

	data->comadj = sharpsl_param.comadj == -1 ? COMADJ_DEFAULT : sharpsl_param.comadj;

	ret = gpio_request(TOSA_GPIO_BL_C20MA, "backlight");
	if (ret) {
		dev_dbg(&data->bl->dev, "Unable to request gpio!\n");
		goto err_gpio_bl;
	}
	ret = gpio_direction_output(TOSA_GPIO_BL_C20MA, 0);
	if (ret)
		goto err_gpio_dir;

	i2c_set_clientdata(client, data);
	data->i2c = client;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = 512 - 1;
	data->bl = backlight_device_register("tosa-bl", &client->dev, data,
					     &bl_ops, &props);
	if (IS_ERR(data->bl)) {
		ret = PTR_ERR(data->bl);
		goto err_reg;
	}

	data->bl->props.brightness = 69;
	data->bl->props.power = FB_BLANK_UNBLANK;

	backlight_update_status(data->bl);

	return 0;

err_reg:
	data->bl = NULL;
	i2c_set_clientdata(client, NULL);
err_gpio_dir:
	gpio_free(TOSA_GPIO_BL_C20MA);
err_gpio_bl:
	kfree(data);
	return ret;
}

static int __devexit tosa_bl_remove(struct i2c_client *client)
{
	struct tosa_bl_data *data = i2c_get_clientdata(client);

	backlight_device_unregister(data->bl);
	data->bl = NULL;
	i2c_set_clientdata(client, NULL);

	gpio_free(TOSA_GPIO_BL_C20MA);

	kfree(data);

	return 0;
}

#ifdef CONFIG_PM
static int tosa_bl_suspend(struct i2c_client *client, pm_message_t pm)
{
	struct tosa_bl_data *data = i2c_get_clientdata(client);

	tosa_bl_set_backlight(data, 0);

	return 0;
}

static int tosa_bl_resume(struct i2c_client *client)
{
	struct tosa_bl_data *data = i2c_get_clientdata(client);

	backlight_update_status(data->bl);
	return 0;
}
#else
#define tosa_bl_suspend NULL
#define tosa_bl_resume NULL
#endif

static const struct i2c_device_id tosa_bl_id[] = {
	{ "tosa-bl", 0 },
	{ },
};


static struct i2c_driver tosa_bl_driver = {
	.driver = {
		.name		= "tosa-bl",
		.owner		= THIS_MODULE,
	},
	.probe		= tosa_bl_probe,
	.remove		= __devexit_p(tosa_bl_remove),
	.suspend	= tosa_bl_suspend,
	.resume		= tosa_bl_resume,
	.id_table	= tosa_bl_id,
};

static int __init tosa_bl_init(void)
{
	return i2c_add_driver(&tosa_bl_driver);
}

static void __exit tosa_bl_exit(void)
{
	i2c_del_driver(&tosa_bl_driver);
}

module_init(tosa_bl_init);
module_exit(tosa_bl_exit);

MODULE_AUTHOR("Dmitry Baryshkov");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("LCD/Backlight control for Sharp SL-6000 PDA");

