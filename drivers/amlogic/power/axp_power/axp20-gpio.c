/*
 * axp199-gpio.c  --  gpiolib support for Krosspower &axp PMICs
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
#include <linux/mfd/core.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include "axp-mfd.h"

#include "axp-gpio.h"

struct virtual_gpio_data {
	struct mutex lock;
	int gpio;				//gpio number : 0/1/2/...
	int io;                 //0: input      1: output
	int value;				//0: low        1: high
};

static int axp_io_state = 0;

int axp_gpio_set_io(int gpio, int io_state)
{
    if (!axp) {
        printk("[AXP] driver has not ready now, wait...\n");
		return -ENODEV;
    }
    if(io_state == 1){
        axp_io_state |= (1 << gpio);
        switch (gpio) {
            /*
             * for out put, set pin to high-z state, and remember to a variable
             * in order to prevent unstable state on GPIO.
             */
        case 0: return axp_update(&axp->dev, AXP20_GPIO0_CFG, 0x07, 0x07);
        case 1: return axp_update(&axp->dev, AXP20_GPIO1_CFG, 0x07, 0x07);
        case 2: return axp_update(&axp->dev, AXP20_GPIO2_CFG, 0x07, 0x07);
        case 3: return axp_update(&axp->dev, AXP20_GPIO3_CFG, 0x04, 0x04);
        default: return -ENXIO;
        }
    } else if (io_state == 0) {
        axp_io_state &= ~(1 << gpio);
        switch (gpio) {
        case 0: return axp_update(&axp->dev, AXP20_GPIO0_CFG, 0x02, 0x07);
        case 1: return axp_update(&axp->dev, AXP20_GPIO1_CFG, 0x02, 0x07);
        case 2: return axp_update(&axp->dev, AXP20_GPIO2_CFG, 0x02, 0x07);
        case 3: return axp_update(&axp->dev, AXP20_GPIO3_CFG, 0x04, 0x04);
        default: return -ENXIO;
        }
    }
    return -EINVAL;
}
EXPORT_SYMBOL_GPL(axp_gpio_set_io);

int check_io012(int gpio, int val, int *io_state)
{
    if (gpio > 2 || gpio < 0) {
        return -1;    
    }
    if ((val & 0x07) == 0x07 && axp_io_state & (1 << gpio)) {
        *io_state = 1;    
    } else if (val == 0x02) {
        *io_state = 0;    
    } else {
        return -1;    
    }
    return 0;
}

int axp_gpio_get_io(int gpio, int *io_state)
{
	uint8_t val;

    if (!axp) {
        printk("[AXP] driver has not ready now, wait...\n");
		return -ENODEV;
    }
    switch (gpio) {
    case 0: axp_read(&axp->dev, AXP20_GPIO0_CFG, &val);
            return check_io012(gpio, val & 0x07, io_state);
    case 1: axp_read(&axp->dev, AXP20_GPIO1_CFG, &val);
            return check_io012(gpio, val & 0x07, io_state);
    case 2: axp_read(&axp->dev, AXP20_GPIO2_CFG, &val);
            return check_io012(gpio, val & 0x07, io_state);
    case 3: axp_read(&axp->dev, AXP20_GPIO3_CFG, &val);
            val &= 0x04;
            if (val == 0x0) {
                *io_state = 1;
            } else {
                *io_state = 0;
            }
            break;
    default: return -ENXIO;
    }

    return 0;
}
EXPORT_SYMBOL_GPL(axp_gpio_get_io);


int axp_gpio_set_value(int gpio, int value)
{
    if (!axp) {
        printk("[AXP] driver has not ready now, wait...\n");
		return -ENODEV;
    }
    /*
     * ignore preve io state, set gpio to what caller want
     */

    if(value){
        switch (gpio) {
        case 0: return axp_update(&axp->dev, AXP20_GPIO0_CFG, 0x01, 0x07);
        case 1: return axp_update(&axp->dev, AXP20_GPIO1_CFG, 0x01, 0x07);
        case 2: return axp_update(&axp->dev, AXP20_GPIO2_CFG, 0x01, 0x07);     // Need extern pull up resistor 
        case 3: return axp_update(&axp->dev, AXP20_GPIO3_CFG, 0x02, 0x07);     // Need extern pull up resistor
        default: break;
        }
    } else {
        switch (gpio) {
        case 0: return axp_update(&axp->dev, AXP20_GPIO0_CFG, 0x00, 0x07);
        case 1: return axp_update(&axp->dev, AXP20_GPIO1_CFG, 0x00, 0x07);
        case 2: return axp_update(&axp->dev, AXP20_GPIO2_CFG, 0x00, 0x07);
        case 3: return axp_update(&axp->dev, AXP20_GPIO3_CFG, 0x00, 0x07);
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

    if (!axp) {
        printk("[AXP] driver has not ready now, wait...\n");
		return -ENODEV;
    }
    ret = axp_gpio_get_io(gpio,&io_state);
    if(ret)
        return ret;
    if(io_state){
        switch (gpio) {
        case 0:ret = axp_read(&axp->dev, AXP20_GPIO0_CFG, &val);*value = val & 0x01;break;
        case 1:ret = axp_read(&axp->dev, AXP20_GPIO1_CFG, &val);*value = val & 0x01;break;
        case 2:ret = axp_read(&axp->dev, AXP20_GPIO2_CFG, &val);*value = val & 0x01;break; 
        case 3:ret = axp_read(&axp->dev, AXP20_GPIO3_CFG, &val);val &= 0x02;*value = val>>1;break;
        default: return -ENXIO;
        }
    } else {
        switch (gpio) {
        case 0:ret = axp_read(&axp->dev, AXP20_GPIO012_STATE, &val);val &= 0x10;*value = val>>4;break;
        case 1:ret = axp_read(&axp->dev, AXP20_GPIO012_STATE, &val);val &= 0x20;*value = val>>5;break;
        case 2:ret = axp_read(&axp->dev, AXP20_GPIO012_STATE, &val);val &= 0x40;*value = val>>6;break;
        case 3:ret = axp_read(&axp->dev, AXP20_GPIO3_CFG, &val);*value = val & 0x01;break;
        default: return -ENXIO;
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


static DEVICE_ATTR(gpio,0644, show_gpio, set_gpio);
static DEVICE_ATTR(io, 0644, show_io, set_io);
static DEVICE_ATTR(value, 0644, show_value, set_value);

struct device_attribute *attributes[] = {
	&dev_attr_gpio,
	&dev_attr_io,
	&dev_attr_value,
};


static int axp_gpio_probe(struct platform_device *pdev)
{
	//struct axp_mfd_chip *axp_chip = dev_get_drvdata(pdev->dev.parent);
	axp_gpio_cfg_t *init_gpio_cfg = pdev->dev.platform_data;
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
	if(init_gpio_cfg != NULL)
	{
		while(init_gpio_cfg->gpio != AXP_GPIO_NULL)
		{
			if(init_gpio_cfg->dir == AXP_GPIO_OUTPUT)
			{
				axp_gpio_set_io(init_gpio_cfg->gpio, init_gpio_cfg->dir);
				axp_gpio_set_value(init_gpio_cfg->gpio, init_gpio_cfg->level);
			}
			init_gpio_cfg++;
		}
	}

	return 0;

err:
	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		device_remove_file(&pdev->dev, attributes[i]);
	kfree(drvdata);
	return ret;

return 0;
}

static int axp_gpio_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver axp_gpio_driver = {
	.driver.name	= "axp20-gpio",
	.driver.owner	= THIS_MODULE,
	.probe		= axp_gpio_probe,
	.remove		= axp_gpio_remove,
};

static int __init axp_gpio_init(void)
{
	return platform_driver_register(&axp_gpio_driver);
}
arch_initcall(axp_gpio_init);

static void __exit axp_gpio_exit(void)
{
	platform_driver_unregister(&axp_gpio_driver);
}
module_exit(axp_gpio_exit);

MODULE_AUTHOR("Donglu Zhang ");
MODULE_DESCRIPTION("GPIO interface for AXP PMICs");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:axp-gpio");
