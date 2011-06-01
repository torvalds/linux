#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <mach/gpio.h>
#include <linux/wakelock.h>

#include "gpio_wave.h"

struct wave_data {
	struct delayed_work d_work;
	int Htime;
	int Ltime;
	unsigned int gpio;
	int cur_value;
	int last_value;
	struct device *dev;  
};

static struct wake_lock w_lock;

static void gpio_wave_dwork_handle(struct work_struct *work)
{
	struct wave_data *data = (struct wave_data *)container_of(work, struct wave_data, d_work.work);
	
	int delay_time = data->cur_value ? data->Ltime : data->Htime;
	data->cur_value = !data->cur_value;
	gpio_set_value(data->gpio, data->cur_value);
	schedule_delayed_work(&(data->d_work), msecs_to_jiffies(delay_time));
}


static int gpio_wave_probe(struct platform_device *pdev)
{
	int ret;
	struct wave_data *data;
	struct gpio_wave_platform_data *pdata = pdev->dev.platform_data;

	data = kmalloc(sizeof(struct wave_data), GFP_KERNEL);
	if (!data) {
		printk("func %s, line %d, malloc fail\n", __func__, __LINE__);
		return -ENOMEM;
	}

	data->dev = &pdev->dev;
	platform_set_drvdata(pdev, data);
	
	if (pdata) {
		int dtime = pdata->Dvalue ? pdata->Htime : pdata->Ltime;
		data->gpio = pdata->gpio;
		data->cur_value = pdata->Dvalue;
		data->last_value = pdata->Dvalue;
		data->Htime = pdata->Htime;
		data->Ltime = pdata->Ltime;
		
		ret = gpio_request(data->gpio, NULL);
		if (ret) {
			printk("func %s, line %d, gpio request err\n", __func__, __LINE__);
			return ret;
		}
		gpio_direction_output(data->gpio, data->cur_value);
		gpio_set_value(data->gpio, data->cur_value);
		wake_lock_init(&w_lock, WAKE_LOCK_SUSPEND, "gpio_wave");
		INIT_DELAYED_WORK(&(data->d_work), gpio_wave_dwork_handle);
		wake_lock(&w_lock);
		schedule_delayed_work(&(data->d_work), msecs_to_jiffies(dtime));
	}
	else {
		kfree(data);
	}
	
	return 0;
}

static int gpio_wave_remove(struct platform_device *pdev)
{
	struct wave_data *data = platform_get_drvdata(pdev);
	gpio_free(data->gpio);
	kfree(data);
	return 0;
}

static struct platform_driver gpio_wave_driver = {
	.probe		= gpio_wave_probe,
	.remove 	= gpio_wave_remove,
	.driver 	= {
		.owner	= THIS_MODULE,
		.name	= "gpio_wave",
	},
};

static int __init gpio_wave_init(void)
{
	return platform_driver_register(&gpio_wave_driver);
}

static void __exit gpio_wave_exit(void)
{
	platform_driver_unregister(&gpio_wave_driver);
}


module_init(gpio_wave_init);
module_exit(gpio_wave_exit);

MODULE_DESCRIPTION("Driver for gpio wave");
MODULE_AUTHOR("lyx, lyx@rock-chips.com");
MODULE_LICENSE("GPL");

