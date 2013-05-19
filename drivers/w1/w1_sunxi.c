#include <linux/device.h>
#include <linux/module.h>
#include <linux/w1-gpio.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <plat/sys_config.h>

static int gpio = -1;
module_param(gpio, int, 0444);
MODULE_PARM_DESC(gpio, "w1 gpio pin number");

static struct w1_gpio_platform_data w1_gpio_pdata = {
	.pin = -1,
	.is_open_drain = 0,
};

static struct platform_device w1_device = {
	.name = "w1-gpio",
	.id = -1,
	.dev.platform_data = &w1_gpio_pdata,
};

static int __init w1_sunxi_init(void)
{
	int ret;
	if (!gpio_is_valid(gpio)) {
		ret =
		    script_parser_fetch("w1_para", "gpio", &gpio, sizeof(int));
		if (ret || !gpio_is_valid(gpio)) {
			pr_err("invalid gpio pin : %d\n", gpio);
			return -EINVAL;
		}
	}
	w1_gpio_pdata.pin = gpio;
	return platform_device_register(&w1_device);
}

static void __exit w1_sunxi_exit(void)
{
	platform_device_unregister(&w1_device);
}

module_init(w1_sunxi_init);
module_exit(w1_sunxi_exit);

MODULE_DESCRIPTION("GPIO w1 sunxi platform device");
MODULE_AUTHOR("Damien Nicolet <zardam@gmail.com>");
MODULE_LICENSE("GPL");
