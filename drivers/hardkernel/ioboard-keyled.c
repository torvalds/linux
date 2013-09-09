//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  ODROID IOBOARD Board : IOBOARD KEY and LED driver (charles.park)
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

//[*]--------------------------------------------------------------------------------------------------[*]
unsigned char       BoardTestFlag = 1;
struct hrtimer      BoardTestTimer;		// Board Test Timer
//[*]--------------------------------------------------------------------------------------------------[*]
// IOBOARD KEY & LED GPIO Define
//[*]--------------------------------------------------------------------------------------------------[*]
enum    {
    IOBOARD_SW1,
    IOBOARD_SW2,
    IOBOARD_SW3,
    IOBOARD_SW4,
    IOBOARD_LED1,
    IOBOARD_LED2,
    IOBOARD_LED3,
    IOBOARD_LED4,
    IOBOARD_LED5,
    IOBOARD_MAX
};

//[*]--------------------------------------------------------------------------------------------------[*]
static struct {
	int		gpio_index;		// Control Index
	int 	gpio;			// GPIO Number
	char	*name;			// GPIO Name == sysfs attr name (must)
	bool 	output;			// 1 = Output, 0 = Input
	int 	value;			// Default Value(only for output)
	int		pud;			// Pull up/down register setting : S3C_GPIO_PULL_DOWN, UP, NONE
} sControlGpios[] = {
	{	IOBOARD_SW1,  	EXYNOS5410_GPX2(5),  "sw1",		0,	0,	S3C_GPIO_PULL_NONE	},
	{	IOBOARD_SW2,  	EXYNOS5410_GPX2(6),  "sw2",		0,	0,	S3C_GPIO_PULL_NONE	},
	{	IOBOARD_SW3,  	EXYNOS5410_GPX1(6),  "sw3",		0,	0,	S3C_GPIO_PULL_NONE	},
	{	IOBOARD_SW4,  	EXYNOS5410_GPX1(2),  "sw4",		0,	0,	S3C_GPIO_PULL_NONE	},
	{	IOBOARD_LED1,  	EXYNOS5410_GPX2(7),  "led1",	1,	0,	S3C_GPIO_PULL_NONE	},
	{	IOBOARD_LED2,  	EXYNOS5410_GPX2(4),  "led2",	1,	0,	S3C_GPIO_PULL_NONE	},
	{	IOBOARD_LED3,  	EXYNOS5410_GPX1(3),  "led3",	1,	0,	S3C_GPIO_PULL_NONE	},
	{	IOBOARD_LED4,  	EXYNOS5410_GPX1(0),  "led4",	1,	0,	S3C_GPIO_PULL_NONE	},
	{	IOBOARD_LED5,  	EXYNOS5410_GPX2(0),  "led5",	1,	1,	S3C_GPIO_PULL_NONE	},
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
				if(sControlGpios[i].output)
    				gpio_set_value(sControlGpios[i].gpio, ((val != 0) ? 1 : 0));
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
static 	ssize_t show_test		(struct device *dev, struct device_attribute *attr, char *buf)
{
	return	sprintf(buf, "%d\n", BoardTestFlag);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static 	ssize_t set_test		(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int	val;

    if(!(sscanf(buf, "%d\n", &val))) 	return	-EINVAL;

    val = (val > 0) ? 1 : 0;

    if(BoardTestFlag != val)    {
        BoardTestFlag = val;

        if(BoardTestFlag)
            hrtimer_start(&BoardTestTimer, ktime_set(0, 500000000), HRTIMER_MODE_REL);
    }

    return 	count;
}

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static	DEVICE_ATTR(sw1,    S_IRWXUGO, show_gpio, NULL);
static	DEVICE_ATTR(sw2,    S_IRWXUGO, show_gpio, NULL);
static	DEVICE_ATTR(sw3,    S_IRWXUGO, show_gpio, NULL);
static	DEVICE_ATTR(sw4,    S_IRWXUGO, show_gpio, NULL);

static	DEVICE_ATTR(led1,   S_IRWXUGO, NULL, set_gpio);
static	DEVICE_ATTR(led2,   S_IRWXUGO, NULL, set_gpio);
static	DEVICE_ATTR(led3,   S_IRWXUGO, NULL, set_gpio);
static	DEVICE_ATTR(led4,   S_IRWXUGO, NULL, set_gpio);
static	DEVICE_ATTR(led5,   S_IRWXUGO, NULL, set_gpio);

static	DEVICE_ATTR(board_test,   S_IRWXUGO, show_test, set_test);
//[*]--------------------------------------------------------------------------------------------------[*]
static struct attribute *ioboard_sysfs_entries[] = {
	&dev_attr_sw1.attr,
	&dev_attr_sw2.attr,
	&dev_attr_sw3.attr,
	&dev_attr_sw4.attr,
	&dev_attr_led1.attr,
	&dev_attr_led2.attr,
	&dev_attr_led3.attr,
	&dev_attr_led4.attr,
	&dev_attr_led5.attr,
	&dev_attr_board_test.attr,
	NULL
};

static struct attribute_group ioboard_sysfs_attr_group = {
	.name   = NULL,
	.attrs  = ioboard_sysfs_entries,
};

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static enum hrtimer_restart ioboard_test_timer(struct hrtimer *timer)
{
    if(BoardTestFlag)   {
        gpio_set_value( sControlGpios[IOBOARD_LED1].gpio,
                        gpio_get_value(sControlGpios[IOBOARD_SW1].gpio) ? 0 : 1);
        gpio_set_value( sControlGpios[IOBOARD_LED2].gpio,
                        gpio_get_value(sControlGpios[IOBOARD_SW2].gpio) ? 0 : 1);
        gpio_set_value( sControlGpios[IOBOARD_LED3].gpio,
                        gpio_get_value(sControlGpios[IOBOARD_SW3].gpio) ? 0 : 1);
        gpio_set_value( sControlGpios[IOBOARD_LED4].gpio,
                        gpio_get_value(sControlGpios[IOBOARD_SW4].gpio) ? 0 : 1);

        gpio_set_value( sControlGpios[IOBOARD_LED5].gpio,
                        gpio_get_value(sControlGpios[IOBOARD_LED5].gpio) ? 0 : 1);

        hrtimer_start(&BoardTestTimer, ktime_set(0, 500000000), HRTIMER_MODE_REL);
    }
    else    {
        gpio_set_value(sControlGpios[IOBOARD_LED1].gpio, 0);
        gpio_set_value(sControlGpios[IOBOARD_LED2].gpio, 0);
        gpio_set_value(sControlGpios[IOBOARD_LED3].gpio, 0);
        gpio_set_value(sControlGpios[IOBOARD_LED4].gpio, 0);
        gpio_set_value(sControlGpios[IOBOARD_LED5].gpio, 1);
    }

	return HRTIMER_NORESTART;
}
//[*]--------------------------------------------------------------------------------------------------[*]
static int	ioboard_keyled_resume(struct platform_device *dev)
{
	#if	defined(DEBUG_PM_MSG)
		printk("%s\n", __FUNCTION__);
	#endif

    return  0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int	ioboard_keyled_suspend(struct platform_device *dev, pm_message_t state)
{
	#if	defined(DEBUG_PM_MSG)
		printk("%s\n", __FUNCTION__);
	#endif
	
    return  0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	int		ioboard_keyled_probe		(struct platform_device *pdev)	
{
    int i;
    
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

	hrtimer_init(&BoardTestTimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	BoardTestTimer.function = ioboard_test_timer;
	
	if(BoardTestFlag)
        hrtimer_start(&BoardTestTimer, ktime_set(0, 500000000), HRTIMER_MODE_REL);

    printk("\n=================== %s ===================\n\n", __func__);

	return	sysfs_create_group(&pdev->dev.kobj, &ioboard_sysfs_attr_group);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static	int		ioboard_keyled_remove		(struct platform_device *pdev)	
{
	int	i;
	
	for (i = 0; i < ARRAY_SIZE(sControlGpios); i++) 	{
		if(sControlGpios[i].gpio)	gpio_free(sControlGpios[i].gpio);
	}

    sysfs_remove_group(&pdev->dev.kobj, &ioboard_sysfs_attr_group);
    
    return	0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static struct platform_driver ioboard_keyled_driver = {
	.driver = {
		.name = "ioboard-keyled",
		.owner = THIS_MODULE,
	},
	.probe 		= ioboard_keyled_probe,
	.remove 	= ioboard_keyled_remove,
	.suspend	= ioboard_keyled_suspend,
	.resume		= ioboard_keyled_resume,
};

//[*]--------------------------------------------------------------------------------------------------[*]
static int __init ioboard_keyled_init(void)
{	
    return platform_driver_register(&ioboard_keyled_driver);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static void __exit ioboard_keyled_exit(void)
{
    platform_driver_unregister(&ioboard_keyled_driver);
}

//[*]--------------------------------------------------------------------------------------------------[*]
module_init(ioboard_keyled_init);
module_exit(ioboard_keyled_exit);

//[*]--------------------------------------------------------------------------------------------------[*]
MODULE_DESCRIPTION("IOBOARD driver for ODROIDXU-Dev board");
MODULE_AUTHOR("Hard-Kernel");
MODULE_LICENSE("GPL");

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
