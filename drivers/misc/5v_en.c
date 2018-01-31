/* SPDX-License-Identifier: GPL-2.0 */
#include <dt-bindings/gpio/gpio.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/pwm.h>
#include <linux/pwm_backlight.h>
#include <linux/slab.h>


static struct of_device_id five_v_en_of_match[] = {
	{ .compatible = "5v_en" },
	{ }
};

MODULE_DEVICE_TABLE(of, five_v_en_of_match);


static int five_v_en_probe(struct platform_device *pdev)
{
        struct device_node *node = pdev->dev.of_node;
        enum of_gpio_flags flags;
        int gpio;
        int ret;
        int en_value;

        //printk("func: %s\n", __func__); 
        if (!node)
                return -ENODEV;

        gpio = of_get_named_gpio_flags(node, "5ven,pin", 0, &flags);
        en_value = (flags == GPIO_ACTIVE_HIGH)? 1:0;
        //gpio =  of_get_named_gpio(node, "gpios", 0);
        if(!gpio_is_valid(gpio)){
		dev_err(&pdev->dev, "invalid 5v gpio%d\n", gpio);
	}

	ret = devm_gpio_request(&pdev->dev, gpio, "otg_5v_gpio");
	if (ret) {
		dev_err(&pdev->dev,
			"failed to request GPIO%d for otg_drv\n",
			gpio);
		return -EINVAL;
	}
	gpio_direction_output(gpio, en_value);

        //printk("func: %s\n", __func__); 
        return 0;
}

static int five_v_en_remove(struct platform_device *pdev)
{
        //printk("func: %s\n", __func__); 
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int five_v_en_suspend(struct device *dev)
{
        //printk("func: %s\n", __func__); 
	return 0;
}

static int five_v_en_resume(struct device *dev)
{
        //printk("func: %s\n", __func__); 
	return 0;
}
#endif

static const struct dev_pm_ops five_v_en_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	.suspend = five_v_en_suspend,
	.resume = five_v_en_resume,
	.poweroff = five_v_en_suspend,
	.restore = five_v_en_resume,
#endif
};

static struct platform_driver five_v_en_driver = {
	.driver		= {
		.name		= "five_v_en",
		.owner		= THIS_MODULE,
		.pm		= &five_v_en_pm_ops,
		.of_match_table	= of_match_ptr(five_v_en_of_match),
	},
	.probe		= five_v_en_probe,
	.remove		= five_v_en_remove,
};

module_platform_driver(five_v_en_driver);

MODULE_DESCRIPTION("5v power for otg and hdmi Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:five_v_en");
