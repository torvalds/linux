/*
 * drivers/i2c/busses/i2c-ixp4xx.c
 *
 * Intel's IXP4xx XScale NPU chipsets (IXP420, 421, 422, 425) do not have
 * an on board I2C controller but provide 16 GPIO pins that are often
 * used to create an I2C bus. This driver provides an i2c_adapter 
 * interface that plugs in under algo_bit and drives the GPIO pins
 * as instructed by the alogorithm driver.
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright (c) 2003-2004 MontaVista Software Inc.
 *
 * This file is licensed under the terms of the GNU General Public 
 * License version 2. This program is licensed "as is" without any 
 * warranty of any kind, whether express or implied.
 *
 * NOTE: Since different platforms will use different GPIO pins for
 *       I2C, this driver uses an IXP4xx-specific platform_data
 *       pointer to pass the GPIO numbers to the driver. This 
 *       allows us to support all the different IXP4xx platforms
 *       w/o having to put #ifdefs in this driver.
 *
 *       See arch/arm/mach-ixp4xx/ixdp425.c for an example of building a 
 *       device list and filling in the ixp4xx_i2c_pins data structure 
 *       that is passed as the platform_data to this driver.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

#include <asm/hardware.h>	/* Pick up IXP4xx-specific bits */

static inline int ixp4xx_scl_pin(void *data)
{
	return ((struct ixp4xx_i2c_pins*)data)->scl_pin;
}

static inline int ixp4xx_sda_pin(void *data)
{
	return ((struct ixp4xx_i2c_pins*)data)->sda_pin;
}

static void ixp4xx_bit_setscl(void *data, int val)
{
	gpio_line_set(ixp4xx_scl_pin(data), 0);
	gpio_line_config(ixp4xx_scl_pin(data),
		val ? IXP4XX_GPIO_IN : IXP4XX_GPIO_OUT );
}

static void ixp4xx_bit_setsda(void *data, int val)
{
	gpio_line_set(ixp4xx_sda_pin(data), 0);
	gpio_line_config(ixp4xx_sda_pin(data),
		val ? IXP4XX_GPIO_IN : IXP4XX_GPIO_OUT );
}

static int ixp4xx_bit_getscl(void *data)
{
	int scl;

	gpio_line_config(ixp4xx_scl_pin(data), IXP4XX_GPIO_IN );
	gpio_line_get(ixp4xx_scl_pin(data), &scl);

	return scl;
}	

static int ixp4xx_bit_getsda(void *data)
{
	int sda;

	gpio_line_config(ixp4xx_sda_pin(data), IXP4XX_GPIO_IN );
	gpio_line_get(ixp4xx_sda_pin(data), &sda);

	return sda;
}	

struct ixp4xx_i2c_data {
	struct ixp4xx_i2c_pins *gpio_pins;
	struct i2c_adapter adapter;
	struct i2c_algo_bit_data algo_data;
};

static int ixp4xx_i2c_remove(struct platform_device *plat_dev)
{
	struct ixp4xx_i2c_data *drv_data = platform_get_drvdata(plat_dev);

	platform_set_drvdata(plat_dev, NULL);

	i2c_del_adapter(&drv_data->adapter);

	kfree(drv_data);

	return 0;
}

static int ixp4xx_i2c_probe(struct platform_device *plat_dev)
{
	int err;
	struct ixp4xx_i2c_pins *gpio = plat_dev->dev.platform_data;
	struct ixp4xx_i2c_data *drv_data = 
		kzalloc(sizeof(struct ixp4xx_i2c_data), GFP_KERNEL);

	if(!drv_data)
		return -ENOMEM;

	drv_data->gpio_pins = gpio;

	/*
	 * We could make a lot of these structures static, but
	 * certain platforms may have multiple GPIO-based I2C
	 * buses for various device domains, so we need per-device
	 * algo_data->data. 
	 */
	drv_data->algo_data.data = gpio;
	drv_data->algo_data.setsda = ixp4xx_bit_setsda;
	drv_data->algo_data.setscl = ixp4xx_bit_setscl;
	drv_data->algo_data.getsda = ixp4xx_bit_getsda;
	drv_data->algo_data.getscl = ixp4xx_bit_getscl;
	drv_data->algo_data.udelay = 10;
	drv_data->algo_data.timeout = 100;

	drv_data->adapter.id = I2C_HW_B_IXP4XX;
	drv_data->adapter.class = I2C_CLASS_HWMON;
	strlcpy(drv_data->adapter.name, plat_dev->dev.driver->name,
		I2C_NAME_SIZE);
	drv_data->adapter.algo_data = &drv_data->algo_data;

	drv_data->adapter.dev.parent = &plat_dev->dev;

	gpio_line_config(gpio->scl_pin, IXP4XX_GPIO_IN);
	gpio_line_config(gpio->sda_pin, IXP4XX_GPIO_IN);
	gpio_line_set(gpio->scl_pin, 0);
	gpio_line_set(gpio->sda_pin, 0);

	err = i2c_bit_add_bus(&drv_data->adapter);
	if (err) {
		printk(KERN_ERR "ERROR: Could not install %s\n", plat_dev->dev.bus_id);

		kfree(drv_data);
		return err;
	}

	platform_set_drvdata(plat_dev, drv_data);

	return 0;
}

static struct platform_driver ixp4xx_i2c_driver = {
	.probe		= ixp4xx_i2c_probe,
	.remove		= ixp4xx_i2c_remove,
	.driver		= {
		.name	= "IXP4XX-I2C",
		.owner	= THIS_MODULE,
	},
};

static int __init ixp4xx_i2c_init(void)
{
	return platform_driver_register(&ixp4xx_i2c_driver);
}

static void __exit ixp4xx_i2c_exit(void)
{
	platform_driver_unregister(&ixp4xx_i2c_driver);
}

module_init(ixp4xx_i2c_init);
module_exit(ixp4xx_i2c_exit);

MODULE_DESCRIPTION("GPIO-based I2C adapter for IXP4xx systems");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Deepak Saxena <dsaxena@plexity.net>");

