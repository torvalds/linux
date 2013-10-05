//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  ODROID IOBOARD Board : IOBOARD BH1780 Sensor driver (charles.park)
//  2013.08.28
// 
//
//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>

#include <mach/gpio.h>
#include <mach/regs-gpio.h>
#include <mach/regs-pmu.h>
#include <plat/gpio-cfg.h>
#include <linux/workqueue.h>

#include <plat/adc.h>

//[*]--------------------------------------------------------------------------------------------------[*]
#if defined(CONFIG_ODROIDXU_IOBOARD_DEBUG)
    #define ADC_WORK_PERIOD     msecs_to_jiffies(1000)  // 1000 ms
#else
    #define ADC_WORK_PERIOD     msecs_to_jiffies(100)   // 100 ms
#endif
#define ADC_REF_VOLTAGE     1800                    // 1.8V
#define ADC_CHANNEL         0

//[*]--------------------------------------------------------------------------------------------------[*]
struct adc_data  {

	struct s3c_adc_client   *client;
	
	unsigned int            voltage;
	unsigned int            value;
	bool                    enabled;

    struct delayed_work     work;
};

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t adc_enable_show		(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct adc_data *adc = dev_get_drvdata(dev);
    
	return	sprintf(buf, "%d\n", adc->enabled);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t adc_enable_set      (struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct adc_data     *adc = dev_get_drvdata(dev);
    unsigned int	    val;

    if(!(sscanf(buf, "%d\n", &val))) 	return	-EINVAL;

    val = (val > 0) ? 1 : 0;

    if(adc->enabled != val)    {
        adc->enabled = val;
        if(adc->enabled)    schedule_delayed_work(&adc->work, ADC_WORK_PERIOD);
    }

    return 	count;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t adc_voltage_show    (struct device *dev, struct device_attribute *attr, char *buf)
{
    struct adc_data *adc = dev_get_drvdata(dev);
    
	return	sprintf(buf, "%d\n", adc->voltage);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t adc_value_show      (struct device *dev, struct device_attribute *attr, char *buf)
{
    struct adc_data *adc = dev_get_drvdata(dev);
    
	return	sprintf(buf, "%d\n", adc->value);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static DEVICE_ATTR(value,   S_IRWXUGO, adc_value_show,      NULL);
static DEVICE_ATTR(voltage, S_IRWXUGO, adc_voltage_show,    NULL);
static DEVICE_ATTR(enable,  S_IRWXUGO, adc_enable_show,	    adc_enable_set);

static struct attribute *adc_attributes[] = {
	&dev_attr_value.attr,
	&dev_attr_voltage.attr,
	&dev_attr_enable.attr,
	NULL
};

static struct attribute_group adc_attribute_group = {
	.attrs = adc_attributes
};

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static void ioboard_adc_work(struct work_struct *work)
{
	struct adc_data	*adc = container_of((struct delayed_work *)work, struct adc_data, work);

    adc->value = s3c_adc_read(adc->client, ADC_CHANNEL);
    
    adc->voltage = (ADC_REF_VOLTAGE * adc->value) / 4096;

    #if defined(CONFIG_ODROIDXU_IOBOARD_DEBUG)
        printk("===> %s : %d\n", __func__, adc->voltage);
    #endif

    if(adc->enabled)    schedule_delayed_work(&adc->work, ADC_WORK_PERIOD);
    else    {
        adc->value      = 0;
        adc->voltage    = 0;
    }
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	int		ioboard_adc_probe		(struct platform_device *pdev)	
{
    struct adc_data     *adc;
    int                 err;

	if(!(adc = kzalloc(sizeof(struct adc_data), GFP_KERNEL)))	return	-ENOMEM;

	dev_set_drvdata(&pdev->dev, adc);

    adc->client = s3c_adc_register( pdev, NULL, NULL, 0 );
    
	INIT_DELAYED_WORK(&adc->work, ioboard_adc_work);

    #if defined(CONFIG_ODROIDXU_IOBOARD_DEBUG)
        adc->enabled = 1;
    #endif

    if(adc->enabled)    schedule_delayed_work(&adc->work, ADC_WORK_PERIOD);

	if ((err = sysfs_create_group(&pdev->dev.kobj, &adc_attribute_group)) < 0)		goto error;

    printk("\n=================== %s ===================\n\n", __func__);

	return 0;

error:
    s3c_adc_release(adc->client);
    kfree(adc);
    
    return err;	
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	int		ioboard_adc_remove		(struct platform_device *pdev)	
{
   	struct adc_data *adc = dev_get_drvdata(&pdev->dev);

    if(adc->enabled)    cancel_delayed_work_sync(&adc->work);

    sysfs_remove_group(&pdev->dev.kobj, &adc_attribute_group);
    
    s3c_adc_release(adc->client);

    kfree(adc);
    
    return	0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int	ioboard_adc_suspend(struct platform_device *pdev, pm_message_t state)
{
   	struct adc_data *adc = dev_get_drvdata(&pdev->dev);

    if(adc->enabled)    cancel_delayed_work_sync(&adc->work);
	
    return  0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int	ioboard_adc_resume(struct platform_device *pdev)
{
   	struct adc_data *adc = dev_get_drvdata(&pdev->dev);

    if(adc->enabled)    schedule_delayed_work(&adc->work, ADC_WORK_PERIOD);

    return  0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static struct platform_driver ioboard_adc_driver = {
	.driver = {
		.name   = "ioboard-adc",
		.owner  = THIS_MODULE,
	},
	.probe 		= ioboard_adc_probe,
	.remove 	= ioboard_adc_remove,
	.suspend	= ioboard_adc_suspend,
	.resume		= ioboard_adc_resume,
};

//[*]--------------------------------------------------------------------------------------------------[*]
static int __init ioboard_adc_init(void)
{	
    return platform_driver_register(&ioboard_adc_driver);
}
module_init(ioboard_adc_init);

//[*]--------------------------------------------------------------------------------------------------[*]
static void __exit ioboard_adc_exit(void)
{
    platform_driver_unregister(&ioboard_adc_driver);
}
module_exit(ioboard_adc_exit);

//[*]--------------------------------------------------------------------------------------------------[*]
MODULE_DESCRIPTION("IOBOARD driver for ODROIDXU-Dev board");
MODULE_AUTHOR("Hard-Kernel");
MODULE_LICENSE("GPL");

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
