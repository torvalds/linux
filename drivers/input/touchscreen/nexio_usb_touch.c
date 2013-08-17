//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  Nexio usb touch driver (charles.park)
//  2011.10.04
// 
//
//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>

#include <mach/irqs.h>
#include <asm/system.h>

#include <asm/gpio.h>
#include <plat/gpio-cfg.h>
#include <mach/regs-gpio.h>
#include <mach/gpio.h>

//[*]--------------------------------------------------------------------------------------------------[*]
#ifdef CONFIG_HAS_EARLYSUSPEND
	#include <linux/wakelock.h>
	#include <linux/earlysuspend.h>
	#include <linux/suspend.h>
#endif

//[*]--------------------------------------------------------------------------------------------------[*]
#include "nexio_usb_touch.h"

//[*]--------------------------------------------------------------------------------------------------[*]
#define	DEBUG_NEXIO_TOUCH_MSG
#define	DEBUG_NEXIO_TOUCH_PM_MSG

//[*]--------------------------------------------------------------------------------------------------[*]
nexio_touch_t	nexio_touch;

//[*]--------------------------------------------------------------------------------------------------[*]
//	USB HID Protocol capture (drivers/hid/usbhid/hid-core.c modified)
//[*]--------------------------------------------------------------------------------------------------[*]
extern	unsigned char	nexio_touch_driver_open;

void 			nexio_touch_report_data		(void *data, unsigned short dsize);

EXPORT_SYMBOL(nexio_touch_report_data);

//[*]--------------------------------------------------------------------------------------------------[*]
static int              nexio_touch_open			(struct input_dev *dev);
static void             nexio_touch_close			(struct input_dev *dev);

static void             nexio_touch_release_device	(struct device *dev);

#ifdef CONFIG_HAS_EARLYSUSPEND
	static void				nexio_touch_early_suspend	(struct early_suspend *h);
	static void				nexio_touch_late_resume	(struct early_suspend *h);
#else
	static int              nexio_touch_resume			(struct platform_device *dev);
	static int              nexio_touch_suspend		(struct platform_device *dev, pm_message_t state);
#endif

static int __devinit    nexio_touch_probe			(struct platform_device *pdev);
static int __devexit    nexio_touch_remove			(struct platform_device *pdev);

static int __init       nexio_touch_init			(void);
static void __exit      nexio_touch_exit			(void);

//[*]--------------------------------------------------------------------------------------------------[*]
static struct platform_driver nexio_touch_platform_device_driver = {
		.probe          = nexio_touch_probe,
		.remove         = nexio_touch_remove,

#ifndef CONFIG_HAS_EARLYSUSPEND
		.suspend        = nexio_touch_suspend,
		.resume         = nexio_touch_resume,
#endif
		.driver		= {
			.owner	= THIS_MODULE,
			.name	= NEXIO_USB_TOUCH_DEVICE_NAME,
		},
};

//[*]--------------------------------------------------------------------------------------------------[*]
static struct platform_device nexio_touch_platform_device = {
        .name           = NEXIO_USB_TOUCH_DEVICE_NAME,
        .id             = -1,
        .num_resources  = 0,
        .dev    = {
                .release	= nexio_touch_release_device,
        },
};

//[*]--------------------------------------------------------------------------------------------------[*]
module_init(nexio_touch_init);
module_exit(nexio_touch_exit);

//[*]--------------------------------------------------------------------------------------------------[*]
MODULE_AUTHOR("HardKernel");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("10.1\" interface for Hardkerenl-Dev Board");

//[*]--------------------------------------------------------------------------------------------------[*]
typedef	struct	touch_usb_data__t	{
	unsigned char	signature;
	unsigned char	status1;
	unsigned char	id1;
	unsigned short	x1;
	unsigned short	y1;
	unsigned char	status2;
	unsigned char	id2;
	unsigned short	x2;
	unsigned short	y2;
	unsigned char	count;
}	__attribute__((packed))	touch_usb_data_t;

//[*]--------------------------------------------------------------------------------------------------[*]
void 			nexio_touch_report_data		(void *data, unsigned short dsize)
{
	touch_usb_data_t	*touch_usb_data = (touch_usb_data_t *)data;
	
	if(touch_usb_data->count)	{

#if defined(NEXIO_USB_TOUCH_LANDSCAPE)
		nexio_touch.x = touch_usb_data->x1;
		nexio_touch.y = touch_usb_data->y1;
#else
		nexio_touch.x = TS_ABS_MAX_Y - touch_usb_data->y1;
		nexio_touch.y = touch_usb_data->x1;
#endif
		
		switch(touch_usb_data->status1)	{
			case	TOUCH_DATA_PRESS	:
			case	TOUCH_DATA_MOVE		:
					input_report_abs(nexio_touch.driver, ABS_MT_TOUCH_MAJOR, 10);   // press               
					input_report_abs(nexio_touch.driver, ABS_MT_WIDTH_MAJOR, 10);
					input_report_abs(nexio_touch.driver, ABS_MT_POSITION_X, nexio_touch.x);
					input_report_abs(nexio_touch.driver, ABS_MT_POSITION_Y, nexio_touch.y);
				break;
			default	:
				break;
		}
		input_mt_sync(nexio_touch.driver);
		
		if(touch_usb_data->count > 1)	{
#if defined(NEXIO_USB_TOUCH_LANDSCAPE)
			nexio_touch.x = touch_usb_data->x2;
			nexio_touch.y = touch_usb_data->y2;
#else
			nexio_touch.x = TS_ABS_MAX_Y - touch_usb_data->y2;
			nexio_touch.y = touch_usb_data->x2;
#endif			

			switch(touch_usb_data->status2)	{
				case	TOUCH_DATA_PRESS	:
				case	TOUCH_DATA_MOVE		:
						input_report_abs(nexio_touch.driver, ABS_MT_TOUCH_MAJOR, 10);   // press               
						input_report_abs(nexio_touch.driver, ABS_MT_WIDTH_MAJOR, 10);
						input_report_abs(nexio_touch.driver, ABS_MT_POSITION_X, nexio_touch.x);
						input_report_abs(nexio_touch.driver, ABS_MT_POSITION_Y, nexio_touch.y);
					break;
				default	:
					break;
			}
			input_mt_sync(nexio_touch.driver);
		}
		input_sync(nexio_touch.driver);
		nexio_touch.status = TOUCH_PRESS;
	}
	else	{
		if(nexio_touch.status)	{
			nexio_touch.status = TOUCH_RELEASE;
			input_report_abs(nexio_touch.driver, ABS_MT_TOUCH_MAJOR, 0);   // press               
			input_report_abs(nexio_touch.driver, ABS_MT_WIDTH_MAJOR, 0);
			input_report_abs(nexio_touch.driver, ABS_MT_POSITION_X, nexio_touch.x);
			input_report_abs(nexio_touch.driver, ABS_MT_POSITION_Y, nexio_touch.y);
			nexio_touch.x = -1;
			nexio_touch.y = -1;
		}
	}
}

//------------------------------------------------------------------------------------------------------------------------
static int	nexio_touch_open	(struct input_dev *dev)
{
	printk("%s\n", __FUNCTION__);
	
	return	0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static void	nexio_touch_close	(struct input_dev *dev)
{
	printk("%s\n", __FUNCTION__);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static void	nexio_touch_release_device	(struct device *dev)
{
	printk("%s\n", __FUNCTION__);
}

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
#ifdef	CONFIG_HAS_EARLYSUSPEND
static void		nexio_touch_late_resume	(struct early_suspend *h)
#else
static 	int		nexio_touch_resume		(struct platform_device *dev)
#endif
{
	#if	defined(DEBUG_NEXIO_TOUCH_PM_MSG)
		printk("%s\n", __FUNCTION__);
	#endif

#ifndef	CONFIG_HAS_EARLYSUSPEND
	return	0;
#endif	
}
//[*]--------------------------------------------------------------------------------------------------[*]
#ifdef	CONFIG_HAS_EARLYSUSPEND
static void		nexio_touch_early_suspend	(struct early_suspend *h)
#else
static 	int		nexio_touch_suspend			(struct platform_device *dev, pm_message_t state)
#endif
{
	#if	defined(DEBUG_NEXIO_TOUCH_PM_MSG)
		printk("%s\n", __FUNCTION__);
	#endif

#ifndef	CONFIG_HAS_EARLYSUSPEND
	return	0;
#endif	
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int __devinit    nexio_touch_probe	(struct platform_device *pdev)
{
	// struct init
	memset(&nexio_touch, 0x00, sizeof(nexio_touch_t));
	
	nexio_touch.driver = input_allocate_device();

    if(!(nexio_touch.driver))	{
		printk("ERROR! : %s cdev_alloc() error!!! no memory!!\n", __FUNCTION__);
		return -ENOMEM;
    }

	nexio_touch.driver->name 	= NEXIO_USB_TOUCH_DEVICE_NAME;
	nexio_touch.driver->phys 	= "nexio_touch/input1";
    nexio_touch.driver->open 	= nexio_touch_open;
    nexio_touch.driver->close	= nexio_touch_close;

	nexio_touch.driver->id.bustype	= BUS_HOST;
	nexio_touch.driver->id.vendor 	= NEXIO_USB_TOUCH_VENDOR;
	nexio_touch.driver->id.product	= NEXIO_USB_TOUCH_PRODUCT;
	nexio_touch.driver->id.version	= 0x0001;

	nexio_touch.driver->evbit[0]  = BIT_MASK(EV_ABS);

	/* multi touch */
	input_set_abs_params(nexio_touch.driver, ABS_MT_POSITION_X, TS_ABS_MIN_X, TS_ABS_MAX_X,	0, 0);
	input_set_abs_params(nexio_touch.driver, ABS_MT_POSITION_Y, TS_ABS_MIN_Y, TS_ABS_MAX_Y,	0, 0);
	input_set_abs_params(nexio_touch.driver, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(nexio_touch.driver, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	
	if(input_register_device(nexio_touch.driver))	{
		printk("NEXIO TOUCH input register device fail!!\n");

		input_free_device(nexio_touch.driver);		return	-ENODEV;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	nexio_touch.power.suspend 	= nexio_touch_early_suspend;
	nexio_touch.power.resume 	= nexio_touch_late_resume;
	nexio_touch.power.level 	= EARLY_SUSPEND_LEVEL_DISABLE_FB-1;
	//if, is in USER_SLEEP status and no active auto expiring wake lock
	//if (has_wake_lock(WAKE_LOCK_SUSPEND) == 0 && get_suspend_state() == PM_SUSPEND_ON)
	register_early_suspend(&nexio_touch.power);
#endif

	printk("--------------------------------------------------------\n");
	printk("HardKernel : Nexio USB Multi touch driver. Ver 1.0\n");
	printk("--------------------------------------------------------\n");

	nexio_touch_driver_open = true;

	return	0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int __devexit    nexio_touch_remove	(struct platform_device *pdev)
{
	#if	defined(DEBUG_NEXIO_TOUCH_MSG)
		printk("%s\n", __FUNCTION__);
	#endif
	
	input_unregister_device(nexio_touch.driver);

	return  0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int __init	nexio_touch_init	(void)
{
	int ret = platform_driver_register(&nexio_touch_platform_device_driver);
	
	#if	defined(DEBUG_NEXIO_TOUCH_MSG)
		printk("%s\n", __FUNCTION__);
	#endif
	
	if(!ret)        {
		ret = platform_device_register(&nexio_touch_platform_device);
		
		#if	defined(DEBUG_NEXIO_TOUCH_MSG)
			printk("platform_driver_register %d \n", ret);
		#endif
		
		if(ret)	platform_driver_unregister(&nexio_touch_platform_device_driver);
	}
	return ret;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static void __exit	nexio_touch_exit	(void)
{
	#if	defined(DEBUG_NEXIO_TOUCH_MSG)
		printk("%s\n",__FUNCTION__);
	#endif
	
	platform_device_unregister(&nexio_touch_platform_device);
	platform_driver_unregister(&nexio_touch_platform_device_driver);
}

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
