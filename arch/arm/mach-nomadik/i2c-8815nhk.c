#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/i2c-gpio.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>

/*
 * There are two busses in the 8815NHK.
 * They could, in theory, be driven by the hardware component, but we
 * use bit-bang through GPIO by now, to keep things simple
 */

static struct i2c_gpio_platform_data nhk8815_i2c_data0 = {
	/* keep defaults for timeouts; pins are push-pull bidirectional */
	.scl_pin = 62,
	.sda_pin = 63,
};

static struct i2c_gpio_platform_data nhk8815_i2c_data1 = {
	/* keep defaults for timeouts; pins are push-pull bidirectional */
	.scl_pin = 53,
	.sda_pin = 54,
};

/* first bus: GPIO XX and YY */
static struct platform_device nhk8815_i2c_dev0 = {
	.name	= "i2c-gpio",
	.id	= 0,
	.dev	= {
		.platform_data = &nhk8815_i2c_data0,
	},
};
/* second bus: GPIO XX and YY */
static struct platform_device nhk8815_i2c_dev1 = {
	.name	= "i2c-gpio",
	.id	= 1,
	.dev	= {
		.platform_data = &nhk8815_i2c_data1,
	},
};

static int __init nhk8815_i2c_init(void)
{
	nmk_gpio_set_mode(nhk8815_i2c_data0.scl_pin, NMK_GPIO_ALT_GPIO);
	nmk_gpio_set_mode(nhk8815_i2c_data0.sda_pin, NMK_GPIO_ALT_GPIO);
	platform_device_register(&nhk8815_i2c_dev0);

	nmk_gpio_set_mode(nhk8815_i2c_data1.scl_pin, NMK_GPIO_ALT_GPIO);
	nmk_gpio_set_mode(nhk8815_i2c_data1.sda_pin, NMK_GPIO_ALT_GPIO);
	platform_device_register(&nhk8815_i2c_dev1);

	return 0;
}

static void __exit nhk8815_i2c_exit(void)
{
	platform_device_unregister(&nhk8815_i2c_dev0);
	platform_device_unregister(&nhk8815_i2c_dev1);
	return;
}

module_init(nhk8815_i2c_init);
module_exit(nhk8815_i2c_exit);
