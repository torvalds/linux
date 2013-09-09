//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  ODROID Board : ODROID sysfs driver (charles.park)
//  2012.01.17
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

//[*]--------------------------------------------------------------------------------------------------[*]
#define	DEBUG_PM_MSG

//[*]--------------------------------------------------------------------------------------------------[*]
// Sleep disable flage
//[*]--------------------------------------------------------------------------------------------------[*]
#define	SLEEP_DISABLE_FLAG

#if defined(SLEEP_DISABLE_FLAG)
	#ifdef CONFIG_HAS_WAKELOCK
		#include <linux/wakelock.h>
		static struct wake_lock 	sleep_wake_lock;
	#endif
#endif

//[*]--------------------------------------------------------------------------------------------------[*]
//   Control GPIO Define
//[*]--------------------------------------------------------------------------------------------------[*]
enum	{
	// Status LED Display
	STATUS_LED_BLUE,
	STATUS_LED_GREEN,
	STATUS_LED_RED,
	
	GPIO_INDEX_END
};

//[*]--------------------------------------------------------------------------------------------------[*]
//
// GPIOs Defined Header file
//
//[*]--------------------------------------------------------------------------------------------------[*]
#if defined(CONFIG_MACH_ODROIDXU)
	#include "odroidxu-sysfs.h"
#else
    #error "Can't find include file for odroid-sysfs driver!"
#endif

//[*]--------------------------------------------------------------------------------------------------[*]
//
// HDMI PHY Bootargs parsing
//
//[*]--------------------------------------------------------------------------------------------------[*]
static  unsigned char   HdmiBootArgs[5];

// Bootargs parsing
static int __init hdmi_resolution_setup(char *line)
{
    sprintf(HdmiBootArgs, "%s", line);
    return  0;
}
__setup("hdmi_phy_res=", hdmi_resolution_setup);

//[*]--------------------------------------------------------------------------------------------------[*]
//
// HDMI OUTPUT Mode(HDMI/DVI) Bootargs parsing
//
//[*]--------------------------------------------------------------------------------------------------[*]
static  unsigned char   VOutArgs[5];

static int __init vout_mode_setup(char *line)
{
    sprintf(VOutArgs, "%s", line);
    return  0;
}
__setup("v_out=", vout_mode_setup);

//[*]--------------------------------------------------------------------------------------------------[*]
//
// STATUS LED Blink Period(Unit:Sec)
//
//[*]--------------------------------------------------------------------------------------------------[*]
static  unsigned char   LedBlinkBootArgs[5] = "-1";
static  status_led_t    *StatusLed = NULL;

// Bootargs parsing
static int __init led_blink_setup(char *line)
{
    sprintf(LedBlinkBootArgs, "%s", line);
    return  0;
}
__setup("led_blink=", led_blink_setup);

//[*]--------------------------------------------------------------------------------------------------[*]
//
// Power Off Trigger from android app
//
//[*]--------------------------------------------------------------------------------------------------[*]
#if defined(CONFIG_KEYBOARD_ODROID)
    extern	void    odroid_keypad_set_data(unsigned int  ext_keypad_data);
#endif
static  int     KeyReleaseTime = -1;

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
//
//   sysfs function prototype define
//
//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_gpio	            (struct device *dev, struct device_attribute *attr, char *buf);
static 	ssize_t set_gpio	            (struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

static	ssize_t show_resolution         (struct device *dev, struct device_attribute *attr, char *buf);

static	ssize_t show_vout_mode			(struct device *dev, struct device_attribute *attr, char *buf);

static	ssize_t show_led_blink			(struct device *dev, struct device_attribute *attr, char *buf);
static 	ssize_t set_led_blink           (struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

static 	ssize_t set_poweroff_trigger    (struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

static	ssize_t show_boot_mode          (struct device *dev, struct device_attribute *attr, char *buf);

static	ssize_t show_inform	            (struct device *dev, struct device_attribute *attr, char *buf);
static 	ssize_t set_inform	            (struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static	DEVICE_ATTR(led_green,			S_IRWXUGO, show_gpio, set_gpio);
static	DEVICE_ATTR(led_blue,			S_IRWXUGO, show_gpio, set_gpio);
static	DEVICE_ATTR(led_red,			S_IRWXUGO, show_gpio, set_gpio);
//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static	DEVICE_ATTR(hdmi_resolution,	S_IRWXUGO, show_resolution, NULL);
static	DEVICE_ATTR(vout_mode,			S_IRWXUGO, show_vout_mode, NULL);
static  DEVICE_ATTR(led_blink,          S_IRWXUGO, show_led_blink, set_led_blink);
static	DEVICE_ATTR(poweroff_trigger,	S_IRWXUGO, NULL, set_poweroff_trigger);
//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static	DEVICE_ATTR(boot_mode,			S_IRWXUGO, show_boot_mode, NULL);
//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
#define INFORM_BASE     0x0800
#define INFORM_OFFSET   0x0004
#define INFORM_REG_CNT  8

static  DEVICE_ATTR(inform0, S_IRWXUGO, show_inform, set_inform);
static  DEVICE_ATTR(inform2, S_IRWXUGO, show_inform, set_inform);
static  DEVICE_ATTR(inform5, S_IRWXUGO, show_inform, set_inform);
static  DEVICE_ATTR(inform6, S_IRWXUGO, show_inform, set_inform);
static  DEVICE_ATTR(inform7, S_IRWXUGO, show_inform, set_inform);
//[*]----------------------- ---------------------------------------------------------------------------[*]
static struct attribute *odroid_sysfs_entries[] = {
	&dev_attr_led_green.attr,
	&dev_attr_led_blue.attr,
	&dev_attr_led_red.attr,

	&dev_attr_hdmi_resolution.attr,
	&dev_attr_vout_mode.attr,
	&dev_attr_led_blink.attr,
	&dev_attr_poweroff_trigger.attr,
	&dev_attr_boot_mode.attr,

    &dev_attr_inform0.attr,     // value clear xreset signal (used bootloader to kernel)
    &dev_attr_inform2.attr,     // value clear xreset signal (used bootloader to kernel)
    &dev_attr_inform5.attr,     // value clear power reset signal (used kernel to bootloader)
    &dev_attr_inform6.attr,     // value clear power reset signal (used kernel to bootloader)
    &dev_attr_inform7.attr,     // value clear power reset signal (used kernel to bootloader)
	NULL
};

static struct attribute_group odroid_sysfs_attr_group = {
	.name   = NULL,
	.attrs  = odroid_sysfs_entries,
};

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t show_gpio		(struct device *dev, struct device_attribute *attr, char *buf)
{
	int	i;

	for (i = 0; i < ARRAY_SIZE(sControlGpios); i++) {
		if(sControlGpios[i].gpio)	{
			if(!strcmp(sControlGpios[i].name, attr->attr.name))
				return	sprintf(buf, "%d\n", (gpio_get_value(sControlGpios[i].gpio) ? 1 : 0));
		}
	}
	
	return	sprintf(buf, "ERROR! : Not found gpio!\n");
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t set_gpio		(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int	val, i;

    if(!(sscanf(buf, "%d\n", &val))) 	return	-EINVAL;

	for (i = 0; i < ARRAY_SIZE(sControlGpios); i++) {
		if(sControlGpios[i].gpio)	{
			if(!strcmp(sControlGpios[i].name, attr->attr.name))	{
				if(sControlGpios[i].output) {
				    // Status LED Blink stop
				    if(StatusLed->gpio == sControlGpios[i].gpio)    StatusLed->blink_off = 1;
				        
    				gpio_set_value(sControlGpios[i].gpio, ((val != 0) ? 1 : 0));
				}
				else
					printk("This GPIO Configuration is INPUT!!\n");
			    return count;
			}
		}
	}

	printk("%s[%d] : undefined gpio!\n", __func__,__LINE__);
    return 	count;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t show_inform		(struct device *dev, struct device_attribute *attr, char *buf)
{
	int	    i;
	char    name[7];

	for (i = 0; i < INFORM_REG_CNT; i++) {
	    
	    memset(name, 0x00, sizeof(name));	    sprintf(name, "inform%d", i);
	    
	    if(!strncmp(attr->attr.name, name, sizeof(name)))    {
	        return  sprintf(buf, "0x%08X\n", readl(EXYNOS_PMUREG(INFORM_BASE + i * INFORM_OFFSET)));
	    }
	}
	
	return	sprintf(buf, "ERROR! : Not found %s reg!\n", attr->attr.name);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t set_inform		(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int	val, i;
	char            name[7];

    if(buf[0] == '0' && ((buf[1] == 'X') || (buf[1] == 'x')))
        val = simple_strtol(buf, NULL, 16);
    else
        val = simple_strtol(buf, NULL, 10);

	for (i = 0; i < INFORM_REG_CNT; i++) {
	    
	    memset(name, 0x00, sizeof(name));	    sprintf(name, "inform%d", i);
	    
	    if(!strncmp(attr->attr.name, name, sizeof(name)))    {
	        writel(val, EXYNOS_PMUREG(INFORM_BASE + i * INFORM_OFFSET)); 
		    return count;
	    }
	}

	printk("ERROR! : Not found %s reg!\n", attr->attr.name);
	return  count;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_resolution (struct device *dev, struct device_attribute *attr, char *buf)
{
	return	sprintf(buf, "%d\n", (0 == strncmp("1080", HdmiBootArgs, 4)) ? 1 : 0);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_vout_mode (struct device *dev, struct device_attribute *attr, char *buf)
{
	return	sprintf(buf, "%d\n", (0 == strncmp("dvi", VOutArgs, 3)) ? 1 : 0);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_boot_mode (struct device *dev, struct device_attribute *attr, char *buf)
{
	return	sprintf(buf, "%d\n", (0x04 == readl(EXYNOS_OM_STAT)) ? 1 : 0);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	ssize_t show_led_blink  (struct device *dev, struct device_attribute *attr, char *buf)
{
	return	sprintf(buf, "%d\n", StatusLed->blink_off);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t set_led_blink   (struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int	val;

    if(!(sscanf(buf, "%d\n", &val))) 	return	-EINVAL;

    // value : 0 ~ 255 (0 : OFF, 1 ~ 255 : Blink period)
    if((val >= 0) && (val < 256))   {
        if(val) StatusLed->blink_off = 0;
        else    StatusLed->blink_off = 1;
        StatusLed->period = val;
    }
    
    return 	count;
}
//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t set_poweroff_trigger    (struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int	val;

    if(!(sscanf(buf, "%d\n", &val))) 	return	-EINVAL;

    //press power off button
    if((val != 0) && (val < 5))     {
        KeyReleaseTime = val;
#if defined(CONFIG_KEYBOARD_ODROID)
        odroid_keypad_set_data(0x01);   // Power Key
#endif
    }
    
    return 	count;
}

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static int	odroid_sysfs_resume(struct platform_device *dev)
{
	#if	defined(DEBUG_PM_MSG)
		printk("%s\n", __FUNCTION__);
	#endif

    return  0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int	odroid_sysfs_suspend(struct platform_device *dev, pm_message_t state)
{
	#if	defined(DEBUG_PM_MSG)
		printk("%s\n", __FUNCTION__);
	#endif
	
    return  0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static enum hrtimer_restart odroid_led_timer(struct hrtimer *timer)
{
    if(!StatusLed->blink_off)    {
        if( StatusLed->hold_time)   StatusLed->hold_time--;
        if(!StatusLed->hold_time)   {
            StatusLed->hold_time    = StatusLed->period;
            StatusLed->on_off       = !StatusLed->on_off;
            
            // LED Port write
			gpio_set_value(StatusLed->gpio, StatusLed->on_off);
        }
    }

    // Power Off Trigger
    if(KeyReleaseTime-- > 0)  {
#if defined(CONFIG_KEYBOARD_ODROID)
        if(!KeyReleaseTime) odroid_keypad_set_data(0x00);   // Power Key Release
#endif
    }

    hrtimer_start(&StatusLed->timer, ktime_set(STATUS_TIMER_PEROID, 0), HRTIMER_MODE_REL);

	return HRTIMER_NORESTART;
}
//[*]--------------------------------------------------------------------------------------------------[*]
static	int		odroid_sysfs_probe		(struct platform_device *pdev)	
{
	int	i;

	if(!(StatusLed = kzalloc(sizeof(status_led_t), GFP_KERNEL)))	{
	    printk("%s : error!! no memory !!\n", __func__);
	    return	-ENOMEM;
	}

	// Control GPIO Init
	for (i = 0; i < ARRAY_SIZE(sControlGpios); i++) {
		if(sControlGpios[i].gpio)	{
			if(gpio_request(sControlGpios[i].gpio, sControlGpios[i].name))	{
				printk("%s : %s gpio reqest err!\n", __FUNCTION__, sControlGpios[i].name);
			}
			else	{
				if(sControlGpios[i].output)		gpio_direction_output	(sControlGpios[i].gpio, sControlGpios[i].value);
				else							gpio_direction_input	(sControlGpios[i].gpio);
	
				s3c_gpio_setpull		(sControlGpios[i].gpio, sControlGpios[i].pud);
			}
		}
	}
	
#if defined(SLEEP_DISABLE_FLAG)
	#ifdef CONFIG_HAS_WAKELOCK
		wake_lock(&sleep_wake_lock);
	#endif
#endif

    StatusLed->period = simple_strtol(LedBlinkBootArgs, NULL, 10);
    // if bootargs not set, blink period 1 sec (default)
    if(StatusLed->period < 0 || StatusLed->period > 255)  StatusLed->period = 1;    
    if(StatusLed->period)   StatusLed->hold_time = StatusLed->period;
    else    {
        StatusLed->blink_off = 1;		gpio_set_value(StatusLed->gpio, 0);
    }
        
	hrtimer_init(&StatusLed->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	StatusLed->timer.function = odroid_led_timer;
	StatusLed->gpio = sControlGpios[STATUS_LED_BLUE].gpio;

    hrtimer_start(&StatusLed->timer, ktime_set(STATUS_TIMER_PEROID, 0), HRTIMER_MODE_REL);

	return	sysfs_create_group(&pdev->dev.kobj, &odroid_sysfs_attr_group);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	int		odroid_sysfs_remove		(struct platform_device *pdev)	
{
	int	i;
	
	for (i = 0; i < ARRAY_SIZE(sControlGpios); i++) 	{
		if(sControlGpios[i].gpio)	gpio_free(sControlGpios[i].gpio);
	}

#if defined(SLEEP_DISABLE_FLAG)
	#ifdef CONFIG_HAS_WAKELOCK
		wake_unlock(&sleep_wake_lock);
	#endif
#endif

    sysfs_remove_group(&pdev->dev.kobj, &odroid_sysfs_attr_group);
    
    return	0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static struct platform_driver odroid_sysfs_driver = {
	.driver = {
		.name = "odroid-sysfs",
		.owner = THIS_MODULE,
	},
	.probe 		= odroid_sysfs_probe,
	.remove 	= odroid_sysfs_remove,
	.suspend	= odroid_sysfs_suspend,
	.resume		= odroid_sysfs_resume,
};

//[*]--------------------------------------------------------------------------------------------------[*]
static int __init odroid_sysfs_init(void)
{	
#if defined(SLEEP_DISABLE_FLAG)
	#ifdef CONFIG_HAS_WAKELOCK
		printk("--------------------------------------------------------\n");
		printk("%s(%d) : Sleep Disable Flag SET!!(Wake_lock_init)\n", __FUNCTION__, __LINE__);
		printk("--------------------------------------------------------\n");

	    wake_lock_init(&sleep_wake_lock, WAKE_LOCK_SUSPEND, "sleep_wake_lock");
	#endif
#else
	printk("--------------------------------------------------------\n");
	printk("%s(%d) : Sleep Enable !! \n", __FUNCTION__, __LINE__);
	printk("--------------------------------------------------------\n");
#endif

    return platform_driver_register(&odroid_sysfs_driver);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static void __exit odroid_sysfs_exit(void)
{
#if defined(SLEEP_DISABLE_FLAG)
	#ifdef CONFIG_HAS_WAKELOCK
	    wake_lock_destroy(&sleep_wake_lock);
	#endif
#endif
    platform_driver_unregister(&odroid_sysfs_driver);
}

//[*]--------------------------------------------------------------------------------------------------[*]
module_init(odroid_sysfs_init);
module_exit(odroid_sysfs_exit);

//[*]--------------------------------------------------------------------------------------------------[*]
MODULE_DESCRIPTION("SYSFS driver for odroid-Dev board");
MODULE_AUTHOR("Hard-Kernel");
MODULE_LICENSE("GPL");

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
