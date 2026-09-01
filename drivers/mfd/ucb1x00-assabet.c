// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/drivers/mfd/ucb1x00-assabet.c
 *
 *  Copyright (C) 2001-2003 Russell King, All Rights Reserved.
 *
 *  We handle the machine-specific bits of the UCB1x00 driver here.
 */
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/gpio/machine.h>
#include <linux/gpio/property.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/property.h>
#include <linux/slab.h>
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
static DEVICE_ATTR_RO(name)

UCB1X00_ATTR(vbatt, UCB_ADC_INP_AD1);
UCB1X00_ATTR(vcharger, UCB_ADC_INP_AD0);
UCB1X00_ATTR(batt_temp, UCB_ADC_INP_AD2);

static const struct property_entry ucb1x00_gpio_keys_props[] = {
	PROPERTY_ENTRY_STRING("label", "ucb1x00"),
	PROPERTY_ENTRY_U32("poll-interval", 50),
	{ }
};

#define UCB1X00_BTN_PROPS(_idx)						\
struct property_entry ucb1x00_btn##_idx##_props[] = {			\
	PROPERTY_ENTRY_U32("linux,code", BTN_0 + (_idx)),		\
	PROPERTY_ENTRY_GPIO("gpios", &ucb1x00_gpiochip_node,		\
			    _idx, GPIO_ACTIVE_HIGH),			\
	PROPERTY_ENTRY_STRING("label", "btn" #_idx),			\
	PROPERTY_ENTRY_BOOL("linux,can-disable"),			\
	{ }								\
}

static const UCB1X00_BTN_PROPS(0);
static const UCB1X00_BTN_PROPS(1);
static const UCB1X00_BTN_PROPS(2);
static const UCB1X00_BTN_PROPS(3);
static const UCB1X00_BTN_PROPS(4);
static const UCB1X00_BTN_PROPS(5);

static const struct property_entry * const ucb1x00_btn_props[] = {
	ucb1x00_btn0_props,
	ucb1x00_btn1_props,
	ucb1x00_btn2_props,
	ucb1x00_btn3_props,
	ucb1x00_btn4_props,
	ucb1x00_btn5_props,
};

struct ucb1x00_assabet_priv {
	struct platform_device *pdev;
	struct fwnode_handle *keys_node;
	struct fwnode_handle *button_nodes[ARRAY_SIZE(ucb1x00_btn_props)];
};

static void ucb1x00_assabet_remove_nodes(struct ucb1x00_assabet_priv *priv, int n)
{
	while (--n >= 0)
		fwnode_remove_software_node(priv->button_nodes[n]);

	fwnode_remove_software_node(priv->keys_node);
}

static int ucb1x00_assabet_add(struct ucb1x00_dev *dev)
{
	struct ucb1x00 *ucb = dev->ucb;
	struct platform_device_info pdevinfo = {
		.name = "gpio-keys",
		.id = PLATFORM_DEVID_NONE,
		.parent = &ucb->dev,
	};
	int ret;
	int i;

	struct ucb1x00_assabet_priv *priv;

	priv = kzalloc_obj(*priv, GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->keys_node = fwnode_create_software_node(ucb1x00_gpio_keys_props, NULL);
	if (IS_ERR(priv->keys_node)) {
		ret = PTR_ERR(priv->keys_node);
		goto err_free_priv;
	}

	for (i = 0; i < ARRAY_SIZE(ucb1x00_btn_props); i++) {
		priv->button_nodes[i] = fwnode_create_software_node(ucb1x00_btn_props[i],
								    priv->keys_node);
		if (IS_ERR(priv->button_nodes[i])) {
			ret = PTR_ERR(priv->button_nodes[i]);
			goto err_free_buttons;
		}
	}

	pdevinfo.fwnode = priv->keys_node;

	priv->pdev = platform_device_register_full(&pdevinfo);
	ret = PTR_ERR_OR_ZERO(priv->pdev);
	if (ret)
		goto err_free_buttons;

	device_create_file(&ucb->dev, &dev_attr_vbatt);
	device_create_file(&ucb->dev, &dev_attr_vcharger);
	device_create_file(&ucb->dev, &dev_attr_batt_temp);

	dev->priv = priv;
	return 0;

err_free_buttons:
	ucb1x00_assabet_remove_nodes(priv, i);
err_free_priv:
	kfree(priv);
	return ret;
}

static void ucb1x00_assabet_remove(struct ucb1x00_dev *dev)
{
	struct ucb1x00_assabet_priv *priv = dev->priv;

	if (!IS_ERR(priv->pdev))
		platform_device_unregister(priv->pdev);

	ucb1x00_assabet_remove_nodes(priv, ARRAY_SIZE(priv->button_nodes));

	device_remove_file(&dev->ucb->dev, &dev_attr_batt_temp);
	device_remove_file(&dev->ucb->dev, &dev_attr_vcharger);
	device_remove_file(&dev->ucb->dev, &dev_attr_vbatt);

	kfree(priv);
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
