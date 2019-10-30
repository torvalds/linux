// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, Fuzhou Rockchip Electronics Co., Ltd.
 *
 * Rockchip rm310 driver for 4g modem
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/circ_buf.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <dt-bindings/gpio/gpio.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/lte.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#define LOG(x...)   pr_info("[4g_modem]: " x)

static struct lte_data *gpdata;
static struct class *modem_class;
static int modem_status = 1;

static int modem_poweron_off(int on_off)
{
	struct lte_data *pdata = gpdata;

	if (pdata) {
		if (on_off) {
			LOG("%s: 4g modem power up.\n", __func__);
			if (pdata->vbat_gpio) {
				gpiod_direction_output(pdata->vbat_gpio, 1);
				msleep(2000);
			}
			if (pdata->power_gpio) {
				gpiod_direction_output(pdata->power_gpio, 0);
				msleep(50);
				gpiod_direction_output(pdata->power_gpio, 1);
				msleep(400);
				gpiod_direction_output(pdata->power_gpio, 0);
			}
		} else {
			LOG("%s: 4g modem power down.\n", __func__);
			if (pdata->power_gpio) {
				gpiod_direction_output(pdata->power_gpio, 0);
				msleep(100);
				gpiod_direction_output(pdata->power_gpio, 1);
				msleep(1000);
				gpiod_direction_output(pdata->power_gpio, 0);
				msleep(400);
			}
			if (pdata->vbat_gpio)
				gpiod_direction_output(pdata->vbat_gpio, 0);
		}
	}
	return 0;
}

static ssize_t modem_status_store(struct class *cls,
				  struct class_attribute *attr,
				  const char *buf, size_t count)
{
	int new_state, ret;

	ret = kstrtoint(buf, 10, &new_state);
	if (ret) {
		LOG("%s: kstrtoint error return %d\n", __func__, ret);
		return ret;
	}
	if (new_state == modem_status)
		return count;
	if (new_state == 1) {
		LOG("%s, c(%d), open modem.\n", __func__, new_state);
		modem_poweron_off(1);
	} else if (new_state == 0) {
		LOG("%s, c(%d), close modem.\n", __func__, new_state);
		modem_poweron_off(0);
	} else {
		LOG("%s, invalid parameter.\n", __func__);
	}
	modem_status = new_state;
	return count;
}

static ssize_t modem_status_show(struct class *cls,
				 struct class_attribute *attr,
				 char *buf)
{
	return sprintf(buf, "%d\n", modem_status);
}

static CLASS_ATTR_RW(modem_status);

static int modem_platdata_parse_dt(struct device *dev,
				   struct lte_data *data)
{
	struct device_node *node = dev->of_node;
	int ret;

	if (!node)
		return -ENODEV;
	memset(data, 0, sizeof(*data));
	LOG("%s: LTE modem power controlled by gpio.\n", __func__);
	data->vbat_gpio = devm_gpiod_get_optional(dev, "4G,vbat",
						  GPIOD_OUT_HIGH);
	if (IS_ERR(data->vbat_gpio)) {
		ret = PTR_ERR(data->vbat_gpio);
		dev_err(dev, "failed to request 4G,vbat GPIO: %d\n", ret);
		return ret;
	}
	data->power_gpio = devm_gpiod_get_optional(dev, "4G,power",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(data->power_gpio)) {
		ret = PTR_ERR(data->power_gpio);
		dev_err(dev, "failed to request 4G,power GPIO: %d\n", ret);
		return ret;
	}
	data->reset_gpio = devm_gpiod_get_optional(dev, "4G,reset",
						   GPIOD_OUT_LOW);
	if (IS_ERR(data->reset_gpio)) {
		ret = PTR_ERR(data->reset_gpio);
		dev_err(dev, "failed to request 4G,reset GPIO: %d\n", ret);
		return ret;
	}
	return 0;
}

static int modem_power_on_thread(void *data)
{
	modem_poweron_off(1);
	return 0;
}

static int lte_probe(struct platform_device *pdev)
{
	struct lte_data *pdata;
	struct task_struct *kthread;
	int ret = -1;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	ret = modem_platdata_parse_dt(&pdev->dev, pdata);
	if (ret < 0) {
		LOG("%s: No lte platform data specified\n", __func__);
		goto err;
	}
	gpdata = pdata;
	pdata->dev = &pdev->dev;

	if (pdata->reset_gpio)
		gpiod_direction_output(pdata->reset_gpio, 0);
	kthread = kthread_run(modem_power_on_thread, NULL,
			      "modem_power_on_thread");
	if (IS_ERR(kthread)) {
		LOG("%s: create modem_power_on_thread failed.\n",  __func__);
		ret = PTR_ERR(kthread);
		goto err;
	}
	return 0;
err:
	gpdata = NULL;
	return ret;
}

static int lte_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int lte_resume(struct platform_device *pdev)
{
	return 0;
}

static int lte_remove(struct platform_device *pdev)
{
	struct lte_data *pdata = gpdata;

	if (pdata->power_gpio) {
		msleep(100);
		gpiod_direction_output(pdata->power_gpio, 1);
		msleep(750);
		gpiod_direction_output(pdata->power_gpio, 0);
	}
	if (pdata->vbat_gpio)
		gpiod_direction_output(pdata->vbat_gpio, 0);
	gpdata = NULL;
	return 0;
}

static const struct of_device_id modem_platdata_of_match[] = {
	{ .compatible = "4g-modem-platdata" },
	{ }
};
MODULE_DEVICE_TABLE(of, modem_platdata_of_match);

static struct platform_driver rm310_driver = {
	.probe		= lte_probe,
	.remove		= lte_remove,
	.suspend	= lte_suspend,
	.resume		= lte_resume,
	.driver	= {
		.name	= "4g-modem-platdata",
		.of_match_table = of_match_ptr(modem_platdata_of_match),
	},
};

static int __init rm310_init(void)
{
	int ret;

	modem_class = class_create(THIS_MODULE, "rk_modem");
	ret =  class_create_file(modem_class, &class_attr_modem_status);
	if (ret)
		LOG("Fail to create class modem_status.\n");
	return platform_driver_register(&rm310_driver);
}

static void __exit rm310_exit(void)
{
	platform_driver_unregister(&rm310_driver);
}

late_initcall(rm310_init);
module_exit(rm310_exit);

MODULE_AUTHOR("xuxuehui <xxh@rock-chips.com>");
MODULE_AUTHOR("Alex Zhao <zzc@rock-chips.com>");
MODULE_DESCRIPTION("RM310 lte modem driver");
MODULE_LICENSE("GPL");
