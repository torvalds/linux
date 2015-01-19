/*
 * Driver for the AMLOGIC  GPIO
 *
 * Copyright (c) AMLOGIC CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/consumer.h>
#include <mach/io.h>
#include <plat/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/amlogic/gpio-amlogic.h>
static DEFINE_SPINLOCK(gpio_irqlock);

struct amlogic_set_pullup pullup_ops;
extern struct amlogic_gpio_desc amlogic_pins[];
extern int gpio_amlogic_name_to_num(const char *name);
#define pin_to_name(pin) (amlogic_pins[pin].name)
int gpio_irq;
int gpio_flag;
int gpio_range_check(unsigned int  pin)
{
	if(pin>=GPIO_MAX){
		printk("GPIO:%d out of gpio range!!!\n",pin);
		return -1;
	}
	else
		return 0;
}
static void set_gpio_owner(unsigned int  pin,const char * owner)
{
	amlogic_pins[pin].gpio_owner=owner;	
}

/* amlogic request gpio interface*/

int amlogic_gpio_request(unsigned int  pin,const char *label)
{
	int ret=-1;
	if(gpio_range_check(pin))
		return -1;
	ret=gpio_request(pin, label);
	if(!ret)
	{
		set_gpio_owner(pin,label);
		return ret;
	}
	if (ret==-EBUSY)
	{
		printk("%s is using the pin %s\n",amlogic_pins[pin].gpio_owner,pin_to_name(pin));
		return ret;
	}
	return ret;
}
EXPORT_SYMBOL(amlogic_gpio_request);

int amlogic_gpio_request_one(unsigned pin, unsigned long flags, const char *label)
{
	int ret=-1;
	if(gpio_range_check(pin))
		return -1;
	ret=gpio_request_one(pin,flags, label);
	if(!ret)
	{
		set_gpio_owner(pin,label);
		return ret;
	}
	if (ret==-EBUSY)
	{
		printk("%s is using the pin %s\n",amlogic_pins[pin].gpio_owner,pin_to_name(pin));
		return ret;
	}
	return ret;

}
EXPORT_SYMBOL(amlogic_gpio_request_one);

int amlogic_gpio_request_array(const struct gpio *array, size_t num)
{
	int i, err;
	for (i = 0; i < num; i++, array++) {
		err = amlogic_gpio_request_one(array->gpio, array->flags, array->label);
		if (err)
			goto err_free;
	}
	return 0;

err_free:
	while (i--)
		gpio_free((--array)->gpio);
	return err;
}
EXPORT_SYMBOL(amlogic_gpio_request_array);

int amlogic_gpio_free_array(const struct gpio *array, size_t num)
{
	int ret=0;
	while (num--){
		ret=amlogic_gpio_free(array->gpio,array->label);
		if(ret)
			return ret;
		array++;
	}
	return ret;
}
EXPORT_SYMBOL(amlogic_gpio_free_array);


int amlogic_gpio_direction_input(unsigned int pin,const char *owner)
{
	int ret=-1;
	if(gpio_range_check(pin))
		return -1;
	if( amlogic_pins[pin].gpio_owner && owner)
		if(!strcmp(amlogic_pins[pin].gpio_owner,owner))
			ret=gpio_direction_input(pin);	
	return ret;
}
EXPORT_SYMBOL(amlogic_gpio_direction_input);

int amlogic_gpio_direction_output(unsigned int pin,int value,const char *owner)
{
	int ret=-1;
	if(gpio_range_check(pin))
		return -1;
	if( amlogic_pins[pin].gpio_owner && owner)
		if(!strcmp(amlogic_pins[pin].gpio_owner,owner))
			ret=gpio_direction_output(pin,value);
	return ret;
}
EXPORT_SYMBOL(amlogic_gpio_direction_output);

const char * amlogic_cat_gpio_owner(unsigned int pin)
{
	if(gpio_range_check(pin))
		return NULL;
	return amlogic_pins[pin].gpio_owner;
}
EXPORT_SYMBOL(amlogic_cat_gpio_owner);

/* amlogic free gpio interface*/

int amlogic_gpio_free(unsigned int  pin,const char * owner)
{
	if(gpio_range_check(pin))
		return -1;
	if( amlogic_pins[pin].gpio_owner && owner){
		if(!strcmp(owner,amlogic_pins[pin].gpio_owner))
		{
			gpio_free(pin);
			amlogic_pins[pin].gpio_owner=NULL;
			return 0;
		}else{
			printk("%s try to free gpio %s, but the gpio %s owner is %s",owner,amlogic_pins[pin].name,
						amlogic_pins[pin].name,amlogic_pins[pin].gpio_owner);
			return -1;
		}
	}
	return -1;
}
EXPORT_SYMBOL(amlogic_gpio_free);

/* amlogic  gpio to irq interface*/

int amlogic_request_gpio_to_irq(unsigned int  pin,const char *label,unsigned int flag)
{
	int ret=-1;
	unsigned long flags;
	if(gpio_range_check(pin))
		return -1;
	ret=amlogic_gpio_request(pin, label);
	if(!ret)
	{	
		spin_lock_irqsave(&gpio_irqlock, flags);
		gpio_flag=flag;
		__gpio_to_irq(pin);
		spin_unlock_irqrestore(&gpio_irqlock, flags);
	}
	return ret;
}
EXPORT_SYMBOL(amlogic_request_gpio_to_irq);

int amlogic_gpio_to_irq(unsigned int  pin,const char *owner,unsigned int flag)
{
	int ret=-1;
	unsigned long flags;
	if(gpio_range_check(pin))
		return -1;
	if( amlogic_pins[pin].gpio_owner && owner)
		if(!strcmp(amlogic_pins[pin].gpio_owner,owner))
		{
			spin_lock_irqsave(&gpio_irqlock, flags);
			gpio_flag=flag;
			__gpio_to_irq(pin);
			spin_unlock_irqrestore(&gpio_irqlock, flags);
			return 0;
		}
	return ret;
}
EXPORT_SYMBOL(amlogic_gpio_to_irq);

int amlogic_get_value(unsigned int pin,const char *owner)
{
	int ret=-1;
	if(gpio_range_check(pin))
		return -1;
	if( amlogic_pins[pin].gpio_owner && owner)
		if(!strcmp(amlogic_pins[pin].gpio_owner,owner))
			return gpio_get_value(pin);
	return ret;
}
EXPORT_SYMBOL(amlogic_get_value);

int amlogic_set_value(unsigned int pin,int value,const char *owner)
{
	int ret=-1;
	if(gpio_range_check(pin))
		return -1;
	if( amlogic_pins[pin].gpio_owner && owner)
		if(!strcmp(amlogic_pins[pin].gpio_owner,owner)){
			gpio_set_value(pin,value);
			return 0;
		}
	return ret;
}
EXPORT_SYMBOL(amlogic_set_value);

int amlogic_gpio_name_map_num(const char *name)
{
	return gpio_amlogic_name_to_num(name);
}
EXPORT_SYMBOL(amlogic_gpio_name_map_num);
int amlogic_set_pull_up_down(unsigned int pin,unsigned int val,const char *owner)
{
	int ret=-1;
	if(gpio_range_check(pin))
		return -1;
	if( amlogic_pins[pin].gpio_owner && owner)
		if(pullup_ops.meson_set_pullup){
			pullup_ops.meson_set_pullup(pin,val,1);
			return 0;
		}
	return ret;
}
EXPORT_SYMBOL(amlogic_set_pull_up_down);
int amlogic_disable_pullup(unsigned int pin,const char *owner)
{
	int ret=-1;
	if(gpio_range_check(pin))
		return -1;
	if( amlogic_pins[pin].gpio_owner && owner)
		if(pullup_ops.meson_set_pullup){
			pullup_ops.meson_set_pullup(pin,0xffffffff,0);
			return 0;
		}
	return ret;
}
EXPORT_SYMBOL(amlogic_disable_pullup);


