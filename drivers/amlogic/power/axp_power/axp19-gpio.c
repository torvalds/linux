/*
 * axp199-gpio.c  --  gpiolib support for Krosspower axp19x PMICs
 *
 * Copyright 2011 Krosspower Microelectronics PLC.
 *
 * Author: Donglu Zhang <>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/mfd/core.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/i2c.h>
#include "axp-mfd.h"

#include "axp-gpio.h"

struct virtual_gpio_data {
	struct mutex lock;
	int gpio;				//gpio number : 0/1/2/...
	int io;                 //0: input      1: output
	int value;				//0: low        1: high
};

int axp_gpio_set_io(int gpio, int io_state)
{
	if(io_state == 1){
		switch(gpio)
		{
			case 0: return axp_clr_bits(&axp->dev,AXP19_GPIO0_CFG,0x07);
			case 1: return axp_clr_bits(&axp->dev,AXP19_GPIO1_CFG,0x07);
			case 2: return axp_clr_bits(&axp->dev,AXP19_GPIO2_CFG,0x07);
			case 3: axp_set_bits(&axp->dev,AXP19_GPIO34_CFG,0x81);
					return axp_clr_bits(&axp->dev,AXP19_GPIO34_CFG,0x02);
			case 4: axp_set_bits(&axp->dev,AXP19_GPIO34_CFG,0x84);
					return axp_clr_bits(&axp->dev,AXP19_GPIO34_CFG,0x08);
			case 5: axp_set_bits(&axp->dev,AXP19_GPIO5_CFG,0x80);
					return axp_clr_bits(&axp->dev,AXP19_GPIO5_CFG,0x40);
			case 6: axp_set_bits(&axp->dev,AXP19_GPIO67_CFG0,0x01);
					return axp_clr_bits(&axp->dev,AXP19_GPIO67_CFG1,0x40);
			case 7: axp_set_bits(&axp->dev,AXP19_GPIO67_CFG0,0x01);
					return axp_clr_bits(&axp->dev,AXP19_GPIO67_CFG1,0x04);
			default:return -ENXIO;
		}
	}
	else if(io_state == 0){
		switch(gpio)
		{
			case 0: axp_clr_bits(&axp->dev,AXP19_GPIO0_CFG,0x06);
					return axp_set_bits(&axp->dev,AXP19_GPIO0_CFG,0x01);
			case 1: axp_clr_bits(&axp->dev,AXP19_GPIO1_CFG,0x06);
					return axp_set_bits(&axp->dev,AXP19_GPIO1_CFG,0x01);
			case 2: axp_clr_bits(&axp->dev,AXP19_GPIO2_CFG,0x06);
					return axp_set_bits(&axp->dev,AXP19_GPIO2_CFG,0x01);
			case 3: axp_set_bits(&axp->dev,AXP19_GPIO34_CFG,0x82);
					return axp_clr_bits(&axp->dev,AXP19_GPIO34_CFG,0x01);
			case 4: axp_set_bits(&axp->dev,AXP19_GPIO34_CFG,0x88);
					return axp_clr_bits(&axp->dev,AXP19_GPIO34_CFG,0x04);
			case 5: axp_set_bits(&axp->dev,AXP19_GPIO5_CFG,0xC0);
			case 6: axp_set_bits(&axp->dev,AXP19_GPIO67_CFG0,0x01);
					return axp_set_bits(&axp->dev,AXP19_GPIO67_CFG1,0x40);
			case 7: axp_set_bits(&axp->dev,AXP19_GPIO67_CFG0,0x01);
					return axp_set_bits(&axp->dev,AXP19_GPIO67_CFG1,0x04);
			default:return -ENXIO;
		}
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(axp_gpio_set_io);


int axp_gpio_get_io(int gpio, int *io_state)
{
	uint8_t val;
	switch(gpio)
	{
		case 0: axp_read(&axp->dev,AXP19_GPIO0_CFG,&val);val &= 0x07;
				if(val == 0x00)
					*io_state = 1;
				else if (val == 0x01)
					*io_state = 0;
				else
					return -EIO;
				break;
		case 1: axp_read(&axp->dev,AXP19_GPIO1_CFG,&val);val &= 0x07;
				if(val < 0x00)
					*io_state = 1;
				else if (val == 0x01)
					*io_state = 0;
				else
					return -EIO;
				break;
		case 2: axp_read(&axp->dev,AXP19_GPIO2_CFG,&val);val &= 0x07;
				if(val == 0x0)
					*io_state = 1;
				else if (val == 0x01)
					*io_state = 0;
				else
					return -EIO;
				break;
		case 3: axp_read(&axp->dev,AXP19_GPIO34_CFG,&val);val &= 0x03;
				if(val == 0x1)
					*io_state = 1;
				else if(val == 0x2)
					*io_state = 0;
				else
					return -EIO;
				break;
		case 4: axp_read(&axp->dev,AXP19_GPIO34_CFG,&val);val &= 0x0C;
				if(val == 0x4)
					*io_state = 1;
				else if(val == 0x8)
					*io_state = 0;
				else
					return -EIO;
				break;
		case 5: axp_read(&axp->dev,AXP19_GPIO5_CFG,&val);val &= 0x40;
				*io_state = val >> 6;
				break;
		case 6: axp_read(&axp->dev,AXP19_GPIO67_CFG1,&val);*io_state = (val & 0x40)?0:1;break;
		case 7: axp_read(&axp->dev,AXP19_GPIO67_CFG1,&val);*io_state = (val & 0x04)?0:1;break;
		default:return -ENXIO;
	}

		return 0;
}
EXPORT_SYMBOL_GPL(axp_gpio_get_io);


int axp_gpio_set_value(int gpio, int value)
{
	if(value){
		switch(gpio)
		{
			case 0: return axp_set_bits(&axp->dev,AXP19_GPIO012_STATE,0x01);
			case 1: return axp_set_bits(&axp->dev,AXP19_GPIO012_STATE,0x02);
			case 2: return axp_set_bits(&axp->dev,AXP19_GPIO012_STATE,0x04);
			case 3: return axp_set_bits(&axp->dev,AXP19_GPIO34_STATE,0x01);
			case 4: return axp_set_bits(&axp->dev,AXP19_GPIO34_STATE,0x02);
			case 5: return axp_set_bits(&axp->dev,AXP19_GPIO5_STATE,0x20);
			case 6: return axp_set_bits(&axp->dev,AXP19_GPIO67_STATE,0x20);
			case 7: return axp_set_bits(&axp->dev,AXP19_GPIO67_STATE,0x02);
			default:break;
		}
	}
	else {
		switch(gpio)
		{
			case 0: return axp_clr_bits(&axp->dev,AXP19_GPIO012_STATE,0x01);
			case 1: return axp_clr_bits(&axp->dev,AXP19_GPIO012_STATE,0x02);
			case 2: return axp_clr_bits(&axp->dev,AXP19_GPIO012_STATE,0x04);
			case 3: return axp_clr_bits(&axp->dev,AXP19_GPIO34_STATE,0x01);
			case 4: return axp_clr_bits(&axp->dev,AXP19_GPIO34_STATE,0x02);
			case 5: return axp_clr_bits(&axp->dev,AXP19_GPIO5_STATE,0x20);
			case 6: return axp_clr_bits(&axp->dev,AXP19_GPIO67_STATE,0x20);
			case 7: return axp_clr_bits(&axp->dev,AXP19_GPIO67_STATE,0x02);
			default:break;
		}
	}
	return -ENXIO;
}
EXPORT_SYMBOL_GPL(axp_gpio_set_value);


int axp_gpio_get_value(int gpio, int *value)
{
	int io_state;
	int ret;
	uint8_t val;
	ret = axp_gpio_get_io(gpio,&io_state);
	if(ret)
		return ret;
	if(io_state){
		switch(gpio)
		{
			case 0:ret = axp_read(&axp->dev,AXP19_GPIO012_STATE,&val);*value = val & 0x01;break;
			case 1:ret = axp_read(&axp->dev,AXP19_GPIO012_STATE,&val);*value = (val & 0x02)?1:0;break;
			case 2:ret = axp_read(&axp->dev,AXP19_GPIO012_STATE,&val);*value = (val & 0x04)?1:0;break;
			case 3:ret = axp_read(&axp->dev,AXP19_GPIO34_STATE,&val);*value =val & 0x01;break;
			case 4:ret = axp_read(&axp->dev,AXP19_GPIO34_STATE,&val);*value =(val & 0x02)?1:0;break;
			case 5:ret = axp_read(&axp->dev,AXP19_GPIO5_STATE,&val);*value =(val & 0x20)?1:0;break;
			case 6:ret = axp_read(&axp->dev,AXP19_GPIO67_STATE,&val);*value =(val & 0x20)?1:0;break;
			case 7:ret = axp_read(&axp->dev,AXP19_GPIO67_STATE,&val);*value =(val & 0x02)?1:0;break;
			default:return -ENXIO;
		}
	}
	else{
		switch(gpio)
		{
			case 0:ret = axp_read(&axp->dev,AXP19_GPIO012_STATE,&val); *value = (val & 0x10)?1:0;break;
			case 1:ret = axp_read(&axp->dev,AXP19_GPIO012_STATE,&val); *value = (val & 0x20)?1:0;break;
			case 2:ret = axp_read(&axp->dev,AXP19_GPIO012_STATE,&val); *value = (val & 0x40)?1:0;break;
			case 3:ret = axp_read(&axp->dev,AXP19_GPIO34_STATE,&val); *value = (val & 0x10)?1:0;break;
			case 4:ret = axp_read(&axp->dev,AXP19_GPIO34_STATE,&val); *value = (val & 0x20)?1:0;break;
			case 5:ret = axp_read(&axp->dev,AXP19_GPIO5_STATE,&val);  *value = (val & 0x10);break;
			case 6:ret = axp_read(&axp->dev,AXP19_GPIO67_STATE,&val);*value =(val & 0x10)?1:0;break;
			case 7:ret = axp_read(&axp->dev,AXP19_GPIO67_STATE,&val);*value =(val & 0x01)?1:0;break;
			default:return -ENXIO;
		}
	}
	return ret;
}
EXPORT_SYMBOL_GPL(axp_gpio_get_value);

static ssize_t show_gpio(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct virtual_gpio_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->gpio);
}

static ssize_t set_gpio(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct virtual_gpio_data *data = dev_get_drvdata(dev);
	long val;

	if (strict_strtol(buf, 10, &val) != 0)
		return count;

	data->gpio = val;

	return count;
}

static ssize_t show_io(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct virtual_gpio_data *data = dev_get_drvdata(dev);
	int ret;
	mutex_lock(&data->lock);

	ret = axp_gpio_get_io(data->gpio,&data->io);

	mutex_unlock(&data->lock);

	if(ret)
		return ret;

	return sprintf(buf, "%d\n", data->io);
}

static ssize_t set_io(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct virtual_gpio_data *data = dev_get_drvdata(dev);
	long val;
	int ret;

	if (strict_strtol(buf, 10, &val) != 0)
		return count;

	mutex_lock(&data->lock);

	data->io = val;
	ret = axp_gpio_set_io(data->gpio,data->io);

	mutex_unlock(&data->lock);
	if(ret)
		return ret;
	return count;
}


static ssize_t show_value(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct virtual_gpio_data *data = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&data->lock);

	ret = axp_gpio_get_value(data->gpio,&data->value);

	mutex_unlock(&data->lock);

	if(ret)
		return ret;

	return sprintf(buf, "%d\n", data->value);
}

static ssize_t set_value(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct virtual_gpio_data *data = dev_get_drvdata(dev);
	long val;
	int ret;

	if (strict_strtol(buf, 10, &val) != 0)
		return count;

	mutex_lock(&data->lock);

	data->value = val;
	ret = axp_gpio_set_value(data->gpio,data->value);

	mutex_unlock(&data->lock);

	if(ret){
		return ret;
	}

	return count;
}


static DEVICE_ATTR(gpio,0666, show_gpio, set_gpio);
static DEVICE_ATTR(io, 0666, show_io, set_io);
static DEVICE_ATTR(value, 0666, show_value, set_value);

struct device_attribute *attributes[] = {
	&dev_attr_gpio,
	&dev_attr_io,
	&dev_attr_value,
};


static int __devinit axp_gpio_probe(struct platform_device *pdev)
{
	//struct axp_mfd_chip *axp_chip = dev_get_drvdata(pdev->dev.parent);
	struct virtual_gpio_data *drvdata;
	int ret, i;

	drvdata = kzalloc(sizeof(struct virtual_gpio_data), GFP_KERNEL);
	if (drvdata == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	mutex_init(&drvdata->lock);

	for (i = 0; i < ARRAY_SIZE(attributes); i++) {
		ret = device_create_file(&pdev->dev, attributes[i]);
		if (ret != 0)
			goto err;
	}

	platform_set_drvdata(pdev, drvdata);

	return 0;

err:
	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		device_remove_file(&pdev->dev, attributes[i]);
	kfree(drvdata);
	return ret;

return 0;
}

static int __devexit axp_gpio_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver axp_gpio_driver = {
	.driver.name	= "axp19-gpio",
	.driver.owner	= THIS_MODULE,
	.probe		= axp_gpio_probe,
	.remove		= __devexit_p(axp_gpio_remove),
};

static int __init axp_gpio_init(void)
{
	return platform_driver_register(&axp_gpio_driver);
}
subsys_initcall(axp_gpio_init);

static void __exit axp_gpio_exit(void)
{
	platform_driver_unregister(&axp_gpio_driver);
}
module_exit(axp_gpio_exit);

MODULE_AUTHOR("Donglu Zhang ");
MODULE_DESCRIPTION("GPIO interface for AXP PMICs");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:axp-gpio");
