#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <asm/gpio.h>
#include <asm/uaccess.h>
#include <linux/timer.h>
#include <linux/input.h>
#include <linux/adc.h>
#include <linux/delay.h>
#include <linux/string.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <mach/rk29_lightsensor.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend cm3202_early_suspend;
#endif


struct rk29_lsr_platform_data *lightsensor;
static void lsr_report_value(struct input_dev *input_dev, int value)
{
    input_report_abs(input_dev, ABS_MISC/*ABS_X*/, value);
    input_sync(input_dev);
}


static inline void timer_callback(unsigned long data)
{
	int ret;
	unsigned int rate;
	adc_async_read(lightsensor->client);
	mutex_lock(&lightsensor->lsr_mutex);
	rate = lightsensor->rate;
	mutex_unlock(&lightsensor->lsr_mutex);
	if(lightsensor->client->result != lightsensor->oldresult)
		{
			lsr_report_value(lightsensor->input_dev, lightsensor->client->result);
			lightsensor->oldresult = lightsensor->client->result;
		}
	ret = mod_timer( &lightsensor->timer, jiffies + msecs_to_jiffies(RATE(rate)));
	if(ret)
		printk("Error in mod_timer\n");
}
static inline void set_lsr_value(bool state)
{
	if(state)
		lightsensor->lsr_state = 1;
	else
		lightsensor->lsr_state = 0;
	gpio_direction_output(LSR_GPIO, lightsensor->lsr_state);
	gpio_set_value(LSR_GPIO, lightsensor->lsr_state);	
}

static inline unsigned int get_lsr_value(void)
{
	if(0 == lightsensor->lsr_state)
		return 0;
	else
		return 1;
}

static inline unsigned int get_adc_value(void)
{

	return lightsensor->client->result;
}

static inline unsigned int set_lsr_rate(unsigned int value)
{
	mutex_lock(&lightsensor->lsr_mutex);
	if(value <= 0)
		value = 1;
	if(value >= 100)
		value = 100;
	lightsensor->rate = value;
	mutex_unlock(&lightsensor->lsr_mutex);
	return 0;
}
static inline unsigned int set_lsr_timer(unsigned int value)
{
	if(value > 0)
		{
			if(1 != lightsensor->timer_on)
				{
					add_timer(&lightsensor->timer);
					lightsensor->timer_on = 1;
				}
				
		}
	if(value == 0)
		{
			if(0 != lightsensor->timer_on)
				{
					del_timer(&lightsensor->timer);
					lightsensor->timer_on = 0;
				}
				
		}		
	return 0;
}



static ssize_t lsr_store_value(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	unsigned long val;
	if(0 == count)
		return count;
	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;
	if(val)
		set_lsr_value(true);
	else
		set_lsr_value(false);
	return count;
}

static ssize_t lsr_show_value(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "lsr value:%d\nadc value:%d\n", get_lsr_value(),get_adc_value());	
}

static DEVICE_ATTR(value, S_IWUSR|S_IRUGO, lsr_show_value, lsr_store_value );

static struct attribute *lsr_attributes[] = {
	&dev_attr_value.attr,
	NULL
};

static const struct attribute_group lsr_attr_group = {
	.attrs = lsr_attributes,
};
static int rk29_lsr_io_init(struct platform_device *dev)
{
	int err;
	struct platform_device *pdev = dev;
	struct rk29_lsr_platform_data *pdata = pdev->dev.platform_data;

	err = gpio_request(pdata->gpio, pdata->desc ?: "rk29-lsr");
	if (err) {
		gpio_free(pdata->gpio);
		printk("-------request RK29_PIN6_PB1 fail--------\n");
		return -1;
	}

	gpio_direction_output(pdata->gpio, pdata->active_low);
	gpio_set_value(pdata->gpio, pdata->active_low);
	set_lsr_value(STARTUP_LEV_LOW);
	err = sysfs_create_group(&pdev->dev.kobj, &lsr_attr_group);
	return 0;
}
static int rk29_lsr_io_deinit(struct platform_device *dev)
{
	struct platform_device *pdev = dev;
	struct rk29_lsr_platform_data *pdata = pdev->dev.platform_data;

	gpio_direction_output(pdata->gpio, pdata->active_low);
	gpio_set_value(pdata->gpio, pdata->active_low);

	gpio_free(pdata->gpio);
	sysfs_remove_group(&pdev->dev.kobj, &lsr_attr_group);
	return 0;
}
static void callback(struct adc_client *client, void *callback_param, int result)
{
	client->result = result;
}
static int rk29_lsr_adc_init(struct platform_device *dev)
{
	struct rk29_lsr_platform_data *pdata = dev->dev.platform_data;
	pdata->client = adc_register(pdata->adc_chn, callback, "lsr_adc");
	if(!pdata->client)
		return -EINVAL;
	mutex_init(&pdata->lsr_mutex);
	return 0;
}
static void rk29_lsr_adc_deinit(struct platform_device *dev)
{
	struct rk29_lsr_platform_data *pdata = dev->dev.platform_data;
	adc_unregister(pdata->client);
	mutex_destroy(&pdata->lsr_mutex);
}

static void rk29_lsr_timer_init(struct platform_device *dev)
{
	int ret;
	struct rk29_lsr_platform_data *pdata = dev->dev.platform_data;
	setup_timer(&pdata->timer, timer_callback, 0);
	lightsensor->timer_on = 1;
	ret = mod_timer( &pdata->timer, jiffies + msecs_to_jiffies(pdata->delay_time) );
	if(ret)
		printk("Error in mod_timer\n");
	return ;
}
static void rk29_lsr_timer_deinit(struct platform_device *dev)
{
	struct rk29_lsr_platform_data *pdata = dev->dev.platform_data;
	del_timer(&pdata->timer);
	lightsensor->timer_on = 0;
}

static void rk29_lsr_input_init(struct platform_device *dev)
{
	int ret;
	struct rk29_lsr_platform_data *pdata = dev->dev.platform_data;
	pdata->input_dev = input_allocate_device();
	if (!pdata->input_dev) {
		printk(KERN_ERR"rk29_lsr_input_init: Failed to allocate input device\n");
		goto init_input_register_device_failed;
	}
	pdata->input_dev->name = "lsensor";
	pdata->input_dev->dev.parent = &dev->dev;
	pdata->input_dev->evbit[0] = BIT(EV_ABS);
	input_set_abs_params(pdata->input_dev,ABS_MISC/*ABS_X*/,0,9/*0x3ff*/,0,0);
	ret = input_register_device(pdata->input_dev);
	return ;
init_input_register_device_failed:
input_free_device(pdata->input_dev);
}

static void rk29_lsr_input_deinit(struct platform_device *dev)
{
	struct rk29_lsr_platform_data *pdata = dev->dev.platform_data;
	input_unregister_device(pdata->input_dev);
    input_free_device(pdata->input_dev);
}
static int lsr_suspend(struct platform_device *pdev, pm_message_t state)
{
	set_lsr_timer(0);
	set_lsr_value(LSR_OFF);
	return 0;
}

static int lsr_resume(struct platform_device *pdev)
{
	set_lsr_timer(1);
	set_lsr_value(LSR_ON);
	return 0; 
}
static int __devinit lsr_probe(struct platform_device *pdev)
{
	lightsensor = kzalloc(sizeof(struct rk29_lsr_platform_data), GFP_KERNEL);
	if(!lightsensor)
	{
        dev_err(&pdev->dev, "no memory for state\n");
        goto err_kzalloc_lightsensor;
    }
	lightsensor->gpio		= LSR_GPIO;
	lightsensor->desc		= "rk29-lsr";
	lightsensor->adc_chn	= 2;
	lightsensor->delay_time	= 1000;
	lightsensor->rate		= 100;
	lightsensor->oldresult	= 0;
	lightsensor->active_low	= STARTUP_LEV_LOW;
	pdev->dev.platform_data = lightsensor; 
	rk29_lsr_io_init(pdev);
	rk29_lsr_adc_init(pdev);
	rk29_lsr_timer_init(pdev);
	rk29_lsr_input_init(pdev);
#ifdef CONFIG_HAS_EARLYSUSPEND
	cm3202_early_suspend.suspend = lsr_suspend;
	cm3202_early_suspend.resume  = lsr_resume; 
	register_early_suspend(&cm3202_early_suspend);
#endif

	return 0;

err_kzalloc_lightsensor:
	kfree(lightsensor);
	return 0; 

}

static int __devexit lsr_remove(struct platform_device *pdev)
{
	rk29_lsr_io_deinit(pdev);
	rk29_lsr_adc_deinit(pdev);
	rk29_lsr_timer_deinit(pdev);
	rk29_lsr_input_deinit(pdev);
	kfree(lightsensor);
	return 0;
}

static struct platform_driver lsr_device_driver = {
	.probe		= lsr_probe,
	.remove		= __devexit_p(lsr_remove),
//	.suspend	= lsr_suspend,
//	.resume		= lsr_resume,
	.driver		= {
		.name	= LSR_NAME,
		.owner	= THIS_MODULE,
	}
};

static int lsr_adc_open(struct inode * inode, struct file * file)
{
	set_lsr_value(LSR_ON);
	return 0;
}

static int lsr_adc_ioctl(struct tty_struct * tty,struct file * file,unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	switch(cmd)
		{
			case LSR_IOCTL_ENABLE:
				set_lsr_value(arg);
				break;
			case LSR_IOCTL_SETRATE:
				set_lsr_rate(arg);
				break;
			case LSR_IOCTL_DEVNAME:
				ret = copy_to_user((void __user *)arg,lightsensor->input_dev->name,strlen(lightsensor->input_dev->name)+1);
				break;
			case LSR_IOCTL_SWICTH:
				set_lsr_timer(arg);
				break;
			default:
				break;
		}
	return ret;
}

static ssize_t lsr_adc_read(struct file *file, char __user *userbuf, size_t bytes, loff_t *off)
{
	int ret;
	ret = copy_to_user(userbuf,&lightsensor->client->result,bytes);
	return ret;
}

static struct file_operations lsr_adc_fops = {
	.owner		= THIS_MODULE,
	.open		= lsr_adc_open,
	.read		= lsr_adc_read,
	.ioctl		= lsr_adc_ioctl,
};

static struct miscdevice misc_lsr_adc_device = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= LSR_NAME,
	.fops	= &lsr_adc_fops,
};

static int __init lsr_init(void)
{
	platform_driver_register(&lsr_device_driver);
	misc_register(&misc_lsr_adc_device);
	return 0;
}

static void __exit lsr_exit(void)
{
	platform_driver_unregister(&lsr_device_driver);
	misc_deregister(&misc_lsr_adc_device);
}

module_init(lsr_init);
module_exit(lsr_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Seven Huang <sevenxuemin@sina.com>");
MODULE_DESCRIPTION("Light sensor for Backlight");
MODULE_ALIAS("platform:gpio-lightsensor");
