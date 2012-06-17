#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/i2c-gpio.h>
#include <linux/platform_device.h>
#include <plat/gpio-nomadik.h>
#include <plat/pincfg.h>

/*
 * There are two busses in the 8815NHK.
 * They could, in theory, be driven by the hardware component, but we
 * use bit-bang through GPIO by now, to keep things simple
 */

/* I2C0 connected to the STw4811 power management chip */
static struct i2c_gpio_platform_data nhk8815_i2c_data0 = {
	/* keep defaults for timeouts; pins are push-pull bidirectional */
	.scl_pin = 62,
	.sda_pin = 63,
};

/* I2C1 connected to various sensors */
static struct i2c_gpio_platform_data nhk8815_i2c_data1 = {
	/* keep defaults for timeouts; pins are push-pull bidirectional */
	.scl_pin = 53,
	.sda_pin = 54,
};

/* I2C2 connected to the USB portions of the STw4811 only */
static struct i2c_gpio_platform_data nhk8815_i2c_data2 = {
	/* keep defaults for timeouts; pins are push-pull bidirectional */
	.scl_pin = 73,
	.sda_pin = 74,
};

static struct platform_device nhk8815_i2c_dev0 = {
	.name	= "i2c-gpio",
	.id	= 0,
	.dev	= {
		.platform_data = &nhk8815_i2c_data0,
	},
};

static struct platform_device nhk8815_i2c_dev1 = {
	.name	= "i2c-gpio",
	.id	= 1,
	.dev	= {
		.platform_data = &nhk8815_i2c_data1,
	},
};

static struct platform_device nhk8815_i2c_dev2 = {
	.name	= "i2c-gpio",
	.id	= 2,
	.dev	= {
		.platform_data = &nhk8815_i2c_data2,
	},
};

static pin_cfg_t cpu8815_pins_i2c[] = {
	PIN_CFG_INPUT(62, GPIO, PULLUP),
	PIN_CFG_INPUT(63, GPIO, PULLUP),
	PIN_CFG_INPUT(53, GPIO, PULLUP),
	PIN_CFG_INPUT(54, GPIO, PULLUP),
	PIN_CFG_INPUT(73, GPIO, PULLUP),
	PIN_CFG_INPUT(74, GPIO, PULLUP),
};

static int __init nhk8815_i2c_init(void)
{
	nmk_config_pins(cpu8815_pins_i2c, ARRAY_SIZE(cpu8815_pins_i2c));
	platform_device_register(&nhk8815_i2c_dev0);
	platform_device_register(&nhk8815_i2c_dev1);
	platform_device_register(&nhk8815_i2c_dev2);

	return 0;
}

static void __exit nhk8815_i2c_exit(void)
{
	platform_device_unregister(&nhk8815_i2c_dev0);
	platform_device_unregister(&nhk8815_i2c_dev1);
	platform_device_unregister(&nhk8815_i2c_dev2);
	return;
}

module_init(nhk8815_i2c_init);
module_exit(nhk8815_i2c_exit);
