/*
 * linux/drivers/leds-newton-pwm.c
 *
 * simple PWM based LED control
 *
 * Copyright 2009 LMC @ rock-chips (lmc@rock-chips.com)
 *
 * based on leds-gpio.c by Raphael Assenat <raph@8d.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <mach/iomux.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/regulator/rk29-pwm-regulator.h>
#include <linux/ctype.h>
#include <mach/board.h>

#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif

#define pwm_write_reg(id, addr, val)        		__raw_writel(val, addr+(RK29_PWM_BASE+id*0x10)) 
#define pwm_read_reg(id, addr)              		__raw_readl(addr+(RK29_PWM_BASE+id*0x10))
#define LED_PWM_DIV	PWM_DIV1024 /*for led the pwm per div 1024 on the register*/
#define LED_PER_DIV	 		1000	
#define LED_PWM_FULL	255

static struct clk *pwm_clk;

struct led_newton_pwm_data {
	struct led_classdev	cdev;
	unsigned int	pwm_id;
	unsigned 		pwm_gpio;
	char*			pwm_iomux_name;
	unsigned int 	pwm_iomux_pwm;
	unsigned int 	pwm_iomux_gpio;
	unsigned int	freq;/**/
	unsigned int	period;/*1-100*/
};

static void led_newton_pwm_set(struct led_classdev *led_cdev,
	enum led_brightness brightness)
{
	struct led_newton_pwm_data *led_dat =
		container_of(led_cdev, struct led_newton_pwm_data, cdev);
	unsigned int period =  led_dat->period;
	
	DBG("Enter %s, brightness = %d,period =%d,freq =%d\n",__FUNCTION__,brightness,period, led_dat->freq);

	if (brightness == 0||period == 0) {
		 // iomux pwm to gpio
		 rk29_mux_api_set(led_dat->pwm_iomux_name, led_dat->pwm_iomux_gpio);
		 // set gpio to low level	 
		 gpio_set_value(led_dat->pwm_gpio,GPIO_LOW);
	}else if(period == LED_PWM_FULL){
		// iomux pwm to gpio
		 rk29_mux_api_set(led_dat->pwm_iomux_name, led_dat->pwm_iomux_gpio);
		 // set gpio to high level	 
		 gpio_set_value(led_dat->pwm_gpio,GPIO_HIGH);
	} else {
		u32 divh,divTotal;
		int id = led_dat->pwm_id;
    	unsigned long clkrate;

    	clkrate = clk_get_rate(pwm_clk);
		  // iomux pwm
		rk29_mux_api_set(led_dat->pwm_iomux_name, led_dat->pwm_iomux_pwm); 
		pwm_write_reg(id,PWM_REG_CTRL, LED_PWM_DIV|PWM_RESET);
		divh = clkrate >> (1+(LED_PWM_DIV>>9));/*the register pre div*/
		divh =  divh* led_dat->freq/LED_PER_DIV;
		pwm_write_reg(id,PWM_REG_LRC,(divh == 0)?1:divh);

		divTotal =pwm_read_reg(id,PWM_REG_LRC);
		divh = divTotal*period/LED_PWM_FULL;
		pwm_write_reg(id, PWM_REG_HRC, divh?divh:1);
		pwm_write_reg(id,PWM_REG_CNTR,0);
		pwm_write_reg(id, PWM_REG_CTRL,pwm_read_reg(id,PWM_REG_CTRL)|LED_PWM_DIV|PWM_ENABLE|PWM_TimeEN);
	}
}

static ssize_t led_newton_freq_show(struct device *dev, 
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_newton_pwm_data *led_dat =
		container_of(led_cdev, struct led_newton_pwm_data, cdev);

	return sprintf(buf, "%u\n", led_dat->freq);
}

static ssize_t led_newton_freq_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_newton_pwm_data *led_dat =
		container_of(led_cdev, struct led_newton_pwm_data, cdev);
	
	ssize_t ret = -EINVAL;
	char *after;
	unsigned long state = simple_strtoul(buf, &after, 10);
	size_t count = after - buf;

	if (*after && isspace(*after))
		count++;

	if (count == size) {
		ret = count;
		if(state)
			led_dat->freq = state;
		led_newton_pwm_set( led_cdev, led_cdev->brightness);
	}

	return ret;
}

static ssize_t led_newton_period_show(struct device *dev, 
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_newton_pwm_data *led_dat =
		container_of(led_cdev, struct led_newton_pwm_data, cdev);

	return sprintf(buf, "%u\n", led_dat->period);
}

static ssize_t led_newton_period_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_newton_pwm_data *led_dat =
		container_of(led_cdev, struct led_newton_pwm_data, cdev);
	
	ssize_t ret = -EINVAL;
	char *after;
	unsigned long state = simple_strtoul(buf, &after, 10);
	size_t count = after - buf;

	if (*after && isspace(*after))
		count++;

	if (count == size) {
		ret = count;
		led_dat->period = state;
		led_newton_pwm_set( led_cdev, led_cdev->brightness);
	}

	return ret;
}

static DEVICE_ATTR(freq, 0644, led_newton_freq_show, led_newton_freq_store);
static DEVICE_ATTR(period, 0644, led_newton_period_show, led_newton_period_store);


static int led_newton_pwm_probe(struct platform_device *pdev)
{
	struct led_newton_pwm_platform_data *pdata = pdev->dev.platform_data;
	struct led_newton_pwm *cur_led;
	struct led_newton_pwm_data *leds_data, *led_dat;
	int i, ret = 0;

	if (!pdata)
		return -EBUSY;

	leds_data = kzalloc(sizeof(struct led_newton_pwm_data) * pdata->num_leds,
				GFP_KERNEL);
	if (!leds_data)
		return -ENOMEM;

	for (i = 0; i < pdata->num_leds; i++) {
		cur_led = &pdata->leds[i];
		led_dat = &leds_data[i];
		
		 ret = gpio_request(cur_led->pwm_gpio,"pwm");
		
		if (ret) {
			dev_err(&pdev->dev,"failed to request pwm gpio\n");
			goto err;
		}


		if (cur_led->pwm_id >2) {
			dev_err(&pdev->dev, "unable to request PWM %d\n",
					cur_led->pwm_id);
			goto err;
		}

		led_dat->cdev.name = cur_led->name;
		led_dat->pwm_id = cur_led->pwm_id;
		led_dat->pwm_gpio = cur_led->pwm_gpio;
		led_dat->pwm_iomux_name = cur_led->pwm_iomux_name;
		led_dat->pwm_iomux_pwm = cur_led->pwm_iomux_pwm;
		led_dat->pwm_iomux_gpio = cur_led->pwm_iomux_gpio;
		if(cur_led->freq)
			led_dat->freq = cur_led->freq;
		else
			led_dat->freq = 1;
		led_dat->period = cur_led->period;
		led_dat->cdev.brightness_set = led_newton_pwm_set;
		led_dat->cdev.brightness = LED_FULL;
		led_dat->cdev.flags	= 0;

		ret = led_classdev_register(&pdev->dev, &led_dat->cdev);
		if (ret < 0) {
			goto err;
		}

		ret = device_create_file(led_dat->cdev.dev, &dev_attr_freq);
		if (ret)
			goto err;
		
		ret = device_create_file(led_dat->cdev.dev, &dev_attr_period);
		if(ret)
		{
			device_remove_file(led_dat->cdev.dev, &dev_attr_freq);
			goto err;
		}
	}
	
	pwm_clk = clk_get(NULL, "pwm");
	clk_enable(pwm_clk);
	platform_set_drvdata(pdev, leds_data);

	return 0;

err:
	if (i > 0) {
		for (i = i - 1; i >= 0; i--) {
			led_classdev_unregister(&leds_data[i].cdev);
			gpio_free(leds_data[i].pwm_gpio);
		}
	}

	kfree(leds_data);

	return ret;
}

static int __devexit led_newton_pwm_remove(struct platform_device *pdev)
{
	int i;
	struct led_newton_pwm_platform_data *pdata = pdev->dev.platform_data;
	struct led_newton_pwm_data *leds_data;

	leds_data = platform_get_drvdata(pdev);

	for (i = 0; i < pdata->num_leds; i++) {
		led_classdev_unregister(&leds_data[i].cdev);
		gpio_free(leds_data[i].pwm_gpio);
	}

	kfree(leds_data);

	return 0;
}
int led_newton_pwm_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 1;
}

int led_newton_pwm_resume(struct platform_device *pdev)
{
	return 1;
}

static struct platform_driver led_newton_pwm_driver = {
	.probe		= led_newton_pwm_probe,
	.remove		= __devexit_p(led_newton_pwm_remove),
	.driver		= {
		.name	= "leds_newton_pwm",
		.owner	= THIS_MODULE,
	},
};

static int __init led_newton_pwm_init(void)
{
	return platform_driver_register(&led_newton_pwm_driver);
}

static void __exit led_newton_pwm_exit(void)
{
	platform_driver_unregister(&led_newton_pwm_driver);
}

module_init(led_newton_pwm_init);
module_exit(led_newton_pwm_exit);

MODULE_AUTHOR("Luotao Fu <l.fu@pengutronix.de>");
MODULE_DESCRIPTION("PWM LED driver for PXA");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:leds-pwm");

