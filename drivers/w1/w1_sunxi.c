#include <linux/device.h>
#include <linux/module.h>
#include <linux/w1-gpio.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <plat/sys_config.h>

static int gpio = -1;
module_param(gpio, int, 0444);
MODULE_PARM_DESC(gpio, "w1 gpio pin number");

static struct platform_device *w1_device;

static int __init w1_sunxi_init(void)
{
	int ret = 0;
	struct w1_gpio_platform_data w1_gpio_pdata = {
		.pin = gpio,
		.is_open_drain = 0,
	};

	if (!gpio_is_valid(w1_gpio_pdata.pin)) {
		ret =
		    script_parser_fetch("w1_para", "gpio", &gpio, sizeof(int));
		if (ret || !gpio_is_valid(gpio)) {
			pr_err("invalid gpio pin in fex configuration : %d\n",
			       gpio);
			return -EINVAL;
		}
		w1_gpio_pdata.pin = gpio;
	}

	w1_device = platform_device_alloc("w1-gpio", 0);
	if (!w1_device)
		return -ENOMEM;

	ret =
	    platform_device_add_data(w1_device, &w1_gpio_pdata,
				     sizeof(struct w1_gpio_platform_data));
	if (ret)
		goto err;

	ret = platform_device_add(w1_device);
	if (ret)
		goto err;

	return 0;

err:
	platform_device_put(w1_device);
	return ret;
}

static void __exit w1_sunxi_exit(void)
{
	platform_device_unregister(w1_device);
}

module_init(w1_sunxi_init);
module_exit(w1_sunxi_exit);

MODULE_DESCRIPTION("GPIO w1 sunxi platform device");
MODULE_AUTHOR("Damien Nicolet <zardam@gmail.com>");
MODULE_LICENSE("GPL");
