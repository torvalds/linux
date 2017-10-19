/*
 * gpio detection  driver
 *
 * Copyright (C) 2015 Rockchip Electronics Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fb.h>
#include <linux/gpio_detection.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/wakelock.h>
#include <linux/rk_keys.h>
#include <linux/gpio/consumer.h>

#define WAKE_LOCK_TIMEOUT_MS (5000)

struct gpio_data {
	struct gpio_detection *parent;
	const char *name;
	struct device dev;
	int notify;
	struct gpio_desc *gpio;
	int atv_val;
	int val;
	int irq;
	struct delayed_work work;
	unsigned int debounce_ms;
	int wakeup;
};

struct gpio_detection {
	struct class_attribute cls_attr;
	struct device *dev;
	int num;
	struct gpio_data *data;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	struct notifier_block fb_notifier;
	struct wake_lock wake_lock;
	int mirror;
	int type;
	int info;
};

static struct class *gpio_detection_class;
static BLOCKING_NOTIFIER_HEAD(gpio_det_notifier_list);
static int system_suspend;

#if IS_ENABLED(CONFIG_GPIO_DET)

/*
 * gpio_det_notifier_call_chain - notify clients of gpio_det_events
 *
 */
int gpio_det_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&gpio_det_notifier_list, val, v);
}
EXPORT_SYMBOL_GPL(gpio_det_notifier_call_chain);

/*
 * gpio_det_register_notifier - register a client notifier
 * @nb: notifier block to callback on events
 */
int gpio_det_register_notifier(struct notifier_block *nb)
{
	int ret = blocking_notifier_chain_register(&gpio_det_notifier_list, nb);

	return ret;
}
EXPORT_SYMBOL(gpio_det_register_notifier);

/*
 * gpio_det_unregister_client - unregister a client notifier
 * @nb: notifier block to callback on events
 */
int gpio_det_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&gpio_det_notifier_list, nb);
}
EXPORT_SYMBOL(gpio_det_unregister_notifier);

#endif

static void gpio_det_report_event(struct gpio_data *gpiod)
{
	struct gpio_event event;
	struct gpio_detection *gpio_det = gpiod->parent;
	char *status = NULL;
	char *envp[2];

	event.val = gpiod->val ^ gpiod->atv_val;
	event.name = gpiod->name;
	status = kasprintf(GFP_KERNEL, "GPIO_NAME=%s GPIO_STATE=%s",
			   gpiod->name, event.val ? "over" : "on");
	envp[0] = status;
	envp[1] = NULL;
	wake_lock_timeout(&gpio_det->wake_lock,
			  msecs_to_jiffies(WAKE_LOCK_TIMEOUT_MS));
	kobject_uevent_env(&gpiod->dev.kobj, KOBJ_CHANGE, envp);
	if (gpiod->notify)
		gpio_det_notifier_call_chain(GPIO_EVENT, &event);
	kfree(status);
}

static void gpio_det_work_func(struct work_struct *work)
{
	struct gpio_data *gpiod = container_of(work, struct gpio_data,
					       work.work);
	int val = gpiod_get_value(gpiod->gpio);

	if (gpiod->val != val) {
		gpiod->val = val;
		gpio_det_report_event(gpiod);
		if (system_suspend && gpiod->wakeup) {
			rk_send_power_key(1);
			rk_send_power_key(0);
		}
	}
}

static irqreturn_t gpio_det_interrupt(int irq, void *dev_id)
{
	struct gpio_data *gpiod = dev_id;
	int val = gpiod_get_value(gpiod->gpio);
	unsigned int irqflags = IRQF_ONESHOT;

	if (val)
		irqflags |= IRQ_TYPE_EDGE_FALLING;
	else
		irqflags |= IRQ_TYPE_EDGE_RISING;
	irq_set_irq_type(gpiod->irq, irqflags);

	mod_delayed_work(system_wq, &gpiod->work,
			 msecs_to_jiffies(gpiod->debounce_ms));

	return IRQ_HANDLED;
}

static int gpio_det_init_status_check(struct gpio_detection *gpio_det)
{
	struct gpio_data *gpiod;
	int i;

	for (i = 0; i < gpio_det->num; i++) {
		gpiod = &gpio_det->data[i];
		gpiod->val = gpiod_get_value(gpiod->gpio);
		if (gpiod->atv_val == gpiod->val)
			gpio_det_report_event(gpiod);
	}

	return 0;
}

static int gpio_det_fb_notifier_callback(struct notifier_block *self,
					 unsigned long event,
					 void *data)
{
	struct gpio_detection *gpio_det;
	struct fb_event *evdata = data;
	int fb_blank;

	if (event != FB_EVENT_BLANK && event != FB_EVENT_CONBLANK)
		return 0;

	gpio_det = container_of(self, struct gpio_detection, fb_notifier);
	fb_blank = *(int *)evdata->data;
	if (fb_blank == FB_BLANK_UNBLANK)
		system_suspend = 0;
	else
		system_suspend = 1;

	return 0;
}

static int gpio_det_fb_notifier_register(struct gpio_detection *gpio)
{
	gpio->fb_notifier.notifier_call = gpio_det_fb_notifier_callback;

	return fb_register_client(&gpio->fb_notifier);
}

static ssize_t gpio_detection_info_show(struct class *class,
					struct class_attribute *attr,
					char *buf)
{
	struct gpio_detection *gpio_det;

	gpio_det = container_of(attr, struct gpio_detection, cls_attr);

	return sprintf(buf, "%d\n", gpio_det->info);
}

static ssize_t status_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct gpio_data *gpiod = container_of(dev, struct gpio_data, dev);
	unsigned int val = gpiod_get_value(gpiod->gpio);

	return sprintf(buf, "%d\n", val == gpiod->atv_val);
}

static ssize_t status_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct gpio_data *gpiod;
	int val;
	int ret;
	struct gpio_event event;

	gpiod = container_of(dev, struct gpio_data, dev);
	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;
	if (val >= 0) {
		event.val = val;
		event.name = gpiod->name;
		gpio_det_notifier_call_chain(GPIO_EVENT, &event);
	} else {
		gpiod->notify = 0;
	}

	return count;
}

static DEVICE_ATTR_RW(status);

static struct attribute *gpio_detection_attrs[] = {
	&dev_attr_status.attr,
	NULL,
};
ATTRIBUTE_GROUPS(gpio_detection);

static int __init gpio_deteciton_class_init(void)
{
	gpio_detection_class = class_create(THIS_MODULE, "gpio-detection");
	if (IS_ERR(gpio_detection_class)) {
		pr_err("create gpio_detection class failed (%ld)\n",
		       PTR_ERR(gpio_detection_class));
		return PTR_ERR(gpio_detection_class);
	}

	return 0;
}

static int gpio_detection_class_register(struct gpio_detection *gpio_det,
					 struct gpio_data *gpiod)
{
	int ret;

	gpiod->dev.class = gpio_detection_class;
	dev_set_name(&gpiod->dev, "%s", gpiod->name);
	dev_set_drvdata(&gpiod->dev, gpio_det);
	ret = device_register(&gpiod->dev);
	ret = sysfs_create_groups(&gpiod->dev.kobj, gpio_detection_groups);

	return ret;
}

static int gpio_det_parse_dt(struct gpio_detection *gpio_det,
			     struct platform_device *pdev)
{
	struct gpio_data *data;
	struct device *dev = &pdev->dev;
	struct device_node *node;
	struct gpio_data *gpiod;
	struct fwnode_handle *child;
	int count = 0;
	int i = 0;
	int num = 0;

	num = of_get_child_count(gpio_det->dev->of_node);
	count = device_get_child_node_count(dev);
	if (!count || !num)
		return -ENODEV;
	data = devm_kzalloc(gpio_det->dev, num * sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	of_property_read_u32(gpio_det->dev->of_node, "rockchip,camcap-type",
			     &gpio_det->type);
	of_property_read_u32(gpio_det->dev->of_node, "rockchip,camcap-mirror",
			     &gpio_det->mirror);
	gpio_det->info = (gpio_det->mirror << 4) | gpio_det->type;
	device_for_each_child_node(dev, child) {
		node = to_of_node(child);
		gpiod = &data[i++];
		gpiod->parent = gpio_det;
		gpiod->notify = 1;
		gpiod->atv_val = !OF_GPIO_ACTIVE_LOW;
		gpiod->name = of_get_property(node, "label", NULL);
		gpiod->wakeup = !!of_get_property(node, "gpio,wakeup", NULL);
		of_property_read_u32(node, "linux,debounce-ms",
				     &gpiod->debounce_ms);
		if (!strcmp(gpiod->name, "car-reverse"))
			gpiod->gpio = devm_get_gpiod_from_child(dev,
							"car-reverse", child);
		else
			gpiod->gpio = devm_get_gpiod_from_child(dev,
							"car-acc", child);
	}
	gpio_det->num = num;
	gpio_det->data = data;

	return 0;
}

static int gpio_det_probe(struct platform_device *pdev)
{
	struct gpio_detection *gpio_det;
	struct gpio_data *gpiod;
	unsigned long irqflags = IRQF_ONESHOT;
	int i;
	int ret;

	gpio_det = devm_kzalloc(&pdev->dev, sizeof(*gpio_det), GFP_KERNEL);
	if (!gpio_det)
		return -ENOMEM;
	gpio_det->dev = &pdev->dev;
	gpio_det->cls_attr.attr.name = "info";
	gpio_det->cls_attr.attr.mode = S_IRUGO;
	gpio_det->cls_attr.show = gpio_detection_info_show;
	dev_set_name(gpio_det->dev, "gpio_detection");
	if (!pdev->dev.of_node)
		return -EINVAL;
	gpio_det->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(gpio_det->pinctrl)) {
		dev_err(&pdev->dev, "pinctrl get failed\n");
		return PTR_ERR(gpio_det->pinctrl);
	}
	gpio_det->pins_default = pinctrl_lookup_state(gpio_det->pinctrl,
						      PINCTRL_STATE_DEFAULT);
	if (IS_ERR(gpio_det->pins_default))
		dev_err(gpio_det->dev, "get default pinstate failed\n");
	else
		pinctrl_select_state(gpio_det->pinctrl, gpio_det->pins_default);
	if (gpio_det_parse_dt(gpio_det, pdev))
		return -ENODEV;
	wake_lock_init(&gpio_det->wake_lock, WAKE_LOCK_SUSPEND,
		       "gpio_detection");
	for (i = 0; i < gpio_det->num; i++) {
		gpiod = &gpio_det->data[i];
		gpiod_direction_input(gpiod->gpio);

		gpiod->irq = gpiod_to_irq(gpiod->gpio);
		if (gpiod->irq < 0) {
			dev_err(gpio_det->dev, "failed to get irq number for GPIO %s\n",
				gpiod->name);
			continue;
		}

		ret = gpio_detection_class_register(gpio_det, gpiod);
		if (ret < 0)
			return ret;
		INIT_DELAYED_WORK(&gpiod->work, gpio_det_work_func);
		gpiod->val = gpiod_get_value(gpiod->gpio);
		if  (gpiod->val)
			irqflags |= IRQ_TYPE_EDGE_FALLING;
		else
			irqflags |= IRQ_TYPE_EDGE_RISING;
		ret = devm_request_threaded_irq(gpio_det->dev, gpiod->irq,
						NULL, gpio_det_interrupt,
						irqflags | IRQF_ONESHOT,
						gpiod->name, gpiod);
		if (ret < 0)
			dev_err(gpio_det->dev, "request irq(%s) failed:%d\n",
				gpiod->name, ret);
		else
			if (gpiod->wakeup)
				enable_irq_wake(gpiod->irq);
	}

	if (gpio_det->info) {
		ret = class_create_file(gpio_detection_class,
					&gpio_det->cls_attr);
		if (ret)
			dev_warn(gpio_det->dev, "create class file failed:%d\n",
				 ret);
	}

	gpio_det_fb_notifier_register(gpio_det);
	gpio_det_init_status_check(gpio_det);

	dev_info(gpio_det->dev, "gpio detection driver probe success\n");

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id gpio_det_of_match[] = {
	{
		 .compatible = "gpio-detection"
	},
	{},
};
#endif

static struct platform_driver gpio_det_driver = {
	.driver	= {
		.name = "gpio-detection",
		.of_match_table = of_match_ptr(gpio_det_of_match),
	},
	.probe = gpio_det_probe,
};

#ifdef CONFIG_VIDEO_REVERSE_IMAGE
int gpio_det_init(void)
#else
static int __init gpio_det_init(void)
#endif
{
	if (!gpio_deteciton_class_init())
		return platform_driver_register(&gpio_det_driver);
	else
		return -1;
}

#ifndef CONFIG_VIDEO_REVERSE_IMAGE
fs_initcall_sync(gpio_det_init);
#endif
static void __exit gpio_det_exit(void)
{
	platform_driver_unregister(&gpio_det_driver);
}

module_exit(gpio_det_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gpio-detection");
MODULE_AUTHOR("ROCKCHIP");
