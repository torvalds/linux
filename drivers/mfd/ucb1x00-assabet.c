/*
 *  linux/drivers/mfd/ucb1x00-assabet.c
 *
 *  Copyright (C) 2001-2003 Russell King, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 *  We handle the machine-specific bits of the UCB1x00 driver here.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/mfd/ucb1x00.h>

#define UCB1X00_ATTR(name,input)\
static ssize_t name##_show(struct device *dev, struct device_attribute *attr, \
			   char *buf)	\
{								\
	struct ucb1x00 *ucb = classdev_to_ucb1x00(dev);		\
	int val;						\
	ucb1x00_adc_enable(ucb);				\
	val = ucb1x00_adc_read(ucb, input, UCB_NOSYNC);		\
	ucb1x00_adc_disable(ucb);				\
	return sprintf(buf, "%d\n", val);			\
}								\
static DEVICE_ATTR(name,0444,name##_show,NULL)

UCB1X00_ATTR(vbatt, UCB_ADC_INP_AD1);
UCB1X00_ATTR(vcharger, UCB_ADC_INP_AD0);
UCB1X00_ATTR(batt_temp, UCB_ADC_INP_AD2);

static int ucb1x00_assabet_add(struct ucb1x00_dev *dev)
{
	struct ucb1x00 *ucb = dev->ucb;
	struct platform_device *pdev;
	struct gpio_keys_platform_data keys;
	static struct gpio_keys_button buttons[6];
	unsigned i;

	memset(buttons, 0, sizeof(buttons));
	memset(&keys, 0, sizeof(keys));

	for (i = 0; i < ARRAY_SIZE(buttons); i++) {
		buttons[i].code = BTN_0 + i;
		buttons[i].gpio = ucb->gpio.base + i;
		buttons[i].type = EV_KEY;
		buttons[i].can_disable = true;
	}

	keys.buttons = buttons;
	keys.nbuttons = ARRAY_SIZE(buttons);
	keys.poll_interval = 50;
	keys.name = "ucb1x00";

	pdev = platform_device_register_data(&ucb->dev, "gpio-keys", -1,
		&keys, sizeof(keys));

	device_create_file(&ucb->dev, &dev_attr_vbatt);
	device_create_file(&ucb->dev, &dev_attr_vcharger);
	device_create_file(&ucb->dev, &dev_attr_batt_temp);

	dev->priv = pdev;
	return 0;
}

static void ucb1x00_assabet_remove(struct ucb1x00_dev *dev)
{
	struct platform_device *pdev = dev->priv;

	if (!IS_ERR(pdev))
		platform_device_unregister(pdev);

	device_remove_file(&dev->ucb->dev, &dev_attr_batt_temp);
	device_remove_file(&dev->ucb->dev, &dev_attr_vcharger);
	device_remove_file(&dev->ucb->dev, &dev_attr_vbatt);
}

static struct ucb1x00_driver ucb1x00_assabet_driver = {
	.add	= ucb1x00_assabet_add,
	.remove	= ucb1x00_assabet_remove,
};

static int __init ucb1x00_assabet_init(void)
{
	return ucb1x00_register_driver(&ucb1x00_assabet_driver);
}

static void __exit ucb1x00_assabet_exit(void)
{
	ucb1x00_unregister_driver(&ucb1x00_assabet_driver);
}

module_init(ucb1x00_assabet_init);
module_exit(ucb1x00_assabet_exit);

MODULE_AUTHOR("Russell King <rmk@arm.linux.org.uk>");
MODULE_DESCRIPTION("Assabet noddy testing only example ADC driver");
MODULE_LICENSE("GPL");
