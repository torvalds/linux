//[*]--------------------------------------------------------------------------------------------------[*]
//
//  ODROID Board : ODROID FAN driver
//
//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/pwm.h>

#include <mach/gpio.h>
#include <mach/regs-gpio.h>
#include <plat/gpio-cfg.h>

//[*]--------------------------------------------------------------------------------------------------[*]
#define	DEBUG_PM_MSG
#include	"odroid_fan.h"

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
/*
 * driver private data
 */
struct odroid_fan {
	struct odroid_fan_platform_data *pdata;
	struct pwm_device *pwm;

	struct mutex		mutex;
	unsigned int		pwm_status;

	int period;
	int duty;
	int pwm_id;
};

//[*]------------------------------------------------------------------------------------------------------------------
//
// driver sysfs attribute define
//
//[*]------------------------------------------------------------------------------------------------------------------
static	ssize_t set_pwm_enable	(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static	ssize_t show_pwm_status	(struct device *dev, struct device_attribute *attr, char *buf);
static	DEVICE_ATTR(pwm_enable, S_IRWXUGO, show_pwm_status, set_pwm_enable);

static	ssize_t set_pwm_duty	(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static	ssize_t show_pwm_duty	(struct device *dev, struct device_attribute *attr, char *buf);
static	DEVICE_ATTR(pwm_duty, S_IRWXUGO, show_pwm_duty, set_pwm_duty);

static struct attribute *odroid_fan_sysfs_entries[] = {
	&dev_attr_pwm_enable.attr,
	&dev_attr_pwm_duty.attr,
	NULL
};

static struct attribute_group odroid_fan_attr_group = {
	.name   = NULL,
	.attrs  = odroid_fan_sysfs_entries,
};


//[*]------------------------------------------------------------------------------------------------------------------
//[*]------------------------------------------------------------------------------------------------------------------
static	ssize_t set_pwm_enable	(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct odroid_fan *fan = dev_get_drvdata(dev);
	unsigned int	val;

    if(!(sscanf(buf, "%u\n", &val)))	return	-EINVAL;
	printk("PWM_0 : %s [%d] \n",__FUNCTION__,val);

	mutex_lock(&fan->mutex);
    if(val) {
    	fan->pwm_status = 1;
		pwm_disable(fan->pwm);
		pwm_config(fan->pwm, fan->duty * fan->period / 255, fan->period);
    	pwm_enable(fan->pwm);
    }
    else {
    	pwm_disable(fan->pwm);
		pwm_config(fan->pwm, 0, fan->period);
    	fan->pwm_status = 0;
    }
	mutex_unlock(&fan->mutex);

	return count;
}

static	ssize_t show_pwm_status	(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct odroid_fan *fan = dev_get_drvdata(dev);

	if(fan->pwm_status)	return	sprintf(buf, "PWM_0 : %s\n", "on");
	else					return	sprintf(buf, "PWM_0 : %s\n", "off");
}

//[*]------------------------------------------------------------------------------------------------------------------
//[*]------------------------------------------------------------------------------------------------------------------
static	ssize_t set_pwm_duty	(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct odroid_fan *fan = dev_get_drvdata(dev);
	unsigned int	val;

    if(!(sscanf(buf, "%u\n", &val)))	return	-EINVAL;

	if((val > 256)||(val < 0)){
		printk("PWM_0 : Invalid param. Duty cycle range is 0 to 255 \n");
		return count;
	}

	printk("PWM_0 : %s [%d] \n",__FUNCTION__,val);

	mutex_lock(&fan->mutex);
	fan->duty = val;

    if(fan->pwm_status){
		pwm_disable(fan->pwm);
		pwm_config(fan->pwm, fan->duty * fan->period / 255, fan->period);
    	pwm_enable(fan->pwm);
    }
    else {
		pwm_disable(fan->pwm);
		pwm_config(fan->pwm, 0, fan->period);
    }
	mutex_unlock(&fan->mutex);
	
	return count;
}

static	ssize_t show_pwm_duty	(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct odroid_fan *fan = dev_get_drvdata(dev);

	return	sprintf(buf, "PWM_0 : Duty cycle -> %d (%d) \n", fan->duty,fan->duty*100/255);
}


//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static int	odroid_fan_resume(struct platform_device *dev)
{
	#if	defined(DEBUG_PM_MSG)
		printk("%s\n", __FUNCTION__);
	#endif

    return  0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int	odroid_fan_suspend(struct platform_device *dev, pm_message_t state)
{
	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	int		odroid_fan_probe		(struct platform_device *pdev)	
{
	struct odroid_fan *fan;
	struct device 	*dev = &pdev->dev;
	int ret=0;

	if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "pdata is not available\n");
		return -EINVAL;
	}

	fan = kzalloc (sizeof (struct odroid_fan), GFP_KERNEL);
	if (!fan)
		return -ENOMEM;
	fan->pdata = pdev->dev.platform_data;

	//pwm port pin_func init
	if (gpio_is_valid(fan->pdata->pwm_gpio)) {
		ret = gpio_request(fan->pdata->pwm_gpio, "pwm_gpio");
		if (ret)
			printk(KERN_ERR "failed to get GPIO for PWM0\n");
		s3c_gpio_cfgpin(fan->pdata->pwm_gpio, fan->pdata->pwm_func);
		s5p_gpio_set_drvstr(fan->pdata->pwm_gpio, S5P_GPIO_DRVSTR_LV4);
		gpio_free(fan->pdata->pwm_gpio);
    }

	fan->pwm = pwm_request(fan->pdata->pwm_id, pdev->name);
	fan->period = fan->pdata->pwm_periode_ns;
	fan->duty = fan->pdata->pwm_duty;
	pwm_config(fan->pwm, fan->duty * fan->period / 255, fan->period);
	pwm_enable(fan->pwm);
	fan->pwm_status = 1;
	mutex_init(&fan->mutex);
	
	dev_set_drvdata(dev, fan);

	ret =sysfs_create_group(&dev->kobj, &odroid_fan_attr_group);
	if(ret < 0)	{
		dev_err(&pdev->dev, "failed to create sysfs group !!\n");
	}

	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	int		odroid_fan_remove		(struct platform_device *pdev)	
{
    return	0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static struct platform_driver odroid_fan_driver = {
	.driver = {
		.name = "odroid-fan",
		.owner = THIS_MODULE,
	},
	.probe 		= odroid_fan_probe,
	.remove 	= odroid_fan_remove,
	.suspend	= odroid_fan_suspend,
	.resume		= odroid_fan_resume,
};

//[*]--------------------------------------------------------------------------------------------------[*]
static int __init odroid_fan_init(void)
{
    return platform_driver_register(&odroid_fan_driver);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static void __exit odroid_fan_exit(void)
{
    platform_driver_unregister(&odroid_fan_driver);
}

//[*]--------------------------------------------------------------------------------------------------[*]
module_init(odroid_fan_init);
module_exit(odroid_fan_exit);

//[*]--------------------------------------------------------------------------------------------------[*]
MODULE_DESCRIPTION("FAN driver for odroid-Dev board");
MODULE_AUTHOR("Hard-Kernel");
MODULE_LICENSE("GPL");

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
