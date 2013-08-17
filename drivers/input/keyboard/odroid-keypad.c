//[*]--------------------------------------------------------------------------------------------------[*]
/*
 *
 * ODROID Dev Board keypad driver (charles.park)
 *
 */
//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>

#include <linux/gpio.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <plat/gpio-cfg.h>
#include <mach/regs-gpio.h>
#include <mach/regs-pmu.h>

// Debug message enable flag
#define	DEBUG_MSG			
#define	DEBUG_PM_MSG

//[*]--------------------------------------------------------------------------------------------------[*]
#include "odroid-keypad.h"

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
static void __exit		odroid_keypad_exit			(void);
static int __init		odroid_keypad_init			(void);
static int __devexit    odroid_keypad_remove		(struct platform_device *pdev);
static int __devinit    odroid_keypad_probe			(struct platform_device *pdev);
static void				odroid_keypad_config		(odroid_keypad_t *keypad);
static int				odroid_keypad_suspend		(struct platform_device *pdev, pm_message_t state);
static int				odroid_keypad_resume		(struct platform_device *pdev);
static void				odroid_keypad_release_device(struct device *dev);
static void				odroid_keypad_close			(struct input_dev *dev);
static int				odroid_keypad_open			(struct input_dev *dev);
static void 			odroid_keypad_control		(odroid_keypad_t *keypad);
static int				odroid_keypad_get_data		(void);
static void				generate_keycode			(odroid_keypad_t *keypad, unsigned short prev_key, unsigned short cur_key, int *key_table);

static enum hrtimer_restart 	odroid_keypad_timer		(struct hrtimer *timer);
static enum hrtimer_restart 	odroid_poweroff_timer	(struct hrtimer *timer);
//[*]--------------------------------------------------------------------------------------------------[*]
static struct platform_driver odroid_platform_device_driver = {
		.probe          = odroid_keypad_probe,
		.remove         = odroid_keypad_remove,
		.suspend        = odroid_keypad_suspend,
		.resume         = odroid_keypad_resume,
		.driver		= {
			.owner	= THIS_MODULE,
			.name	= DEVICE_NAME,
		},
};

//[*]--------------------------------------------------------------------------------------------------[*]
static struct platform_device odroid_platform_device = {
        .name           = DEVICE_NAME,
        .id             = -1,
        .num_resources  = 0,
        .dev    = {
                .release	= odroid_keypad_release_device,
        },
};

//[*]--------------------------------------------------------------------------------------------------[*]
module_init(odroid_keypad_init);
module_exit(odroid_keypad_exit);

//[*]--------------------------------------------------------------------------------------------------[*]
MODULE_AUTHOR("Hard-Kernel");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Keypad interface for ODROID-Dev board");

//[*]--------------------------------------------------------------------------------------------------[*]
//   Control GPIO Define
//[*]--------------------------------------------------------------------------------------------------[*]
// GPIO Index Define
enum	{
	// KEY CONTROL
	KEYPAD_POWER,
	GPIO_INDEX_END
};

static struct {
	int		gpio_index;		// Control Index
	int 	gpio;			// GPIO Number
	char	*name;			// GPIO Name == sysfs attr name (must)
	bool 	output;			// 1 = Output, 0 = Input
	int 	value;			// Default Value(only for output)
	int		pud;			// Pull up/down register setting : S3C_GPIO_PULL_DOWN, UP, NONE
} sControlGpios[] = {
	{	KEYPAD_POWER,   EXYNOS5410_GPX0(3), "KEY POWER"		, 0, 0, S3C_GPIO_PULL_NONE},
};

//[*]--------------------------------------------------------------------------------------------------[*]
#define	MAX_KEYCODE_CNT		1

int Keycode[MAX_KEYCODE_CNT] = {
		KEY_POWER,
};

#if	defined(DEBUG_MSG)
	const char KeyMapStr[MAX_KEYCODE_CNT][20] = {
			"KEY_POWER\n",
	};
#endif	// DEBUG_MSG

//[*]--------------------------------------------------------------------------------------------------[*]
static enum hrtimer_restart odroid_poweroff_timer(struct hrtimer *timer)
{
	printk(KERN_EMERG "%s : setting GPIO_PDA_PS_HOLD low.\n", __func__);
	(*(unsigned long *)(EXYNOS_PMUREG(0x330C))) = 0x5200;
	return HRTIMER_NORESTART;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static void	generate_keycode(odroid_keypad_t *keypad, unsigned short prev_key, unsigned short cur_key, int *key_table)
{
	unsigned short 	press_key, release_key, i;
	
	press_key	= (cur_key ^ prev_key) & cur_key;
	release_key	= (cur_key ^ prev_key) & prev_key;
	
	i = 0;
	while(press_key)	{
		if(press_key & 0x01)	{
   			input_report_key(keypad->input, key_table[i], KEY_PRESS);
			
			// POWER OFF PRESS
			if(key_table[i] == KEY_POWER)	
					hrtimer_start(&keypad->poweroff_timer, ktime_set(POWEROFF_CHECK_PERIOD, 0), HRTIMER_MODE_REL);
		}
		i++;	press_key >>= 1;
	}
	
	i = 0;
	while(release_key)	{
		if(release_key & 0x01)	{
   			input_report_key(keypad->input, key_table[i], KEY_RELEASE);

			// POWER OFF (RELEASE)		
			if(key_table[i] == KEY_POWER)	
					hrtimer_cancel(&keypad->poweroff_timer);
		}
		i++;	release_key >>= 1;
	}
}
//[*]--------------------------------------------------------------------------------------------------[*]
#if defined(DEBUG_MSG)
	static void debug_keycode_printf(unsigned short prev_key, unsigned short cur_key, const char *key_table)
	{
		unsigned short 	press_key, release_key, i;
		
		press_key	= (cur_key ^ prev_key) & cur_key;
		release_key	= (cur_key ^ prev_key) & prev_key;
		
		i = 0;
		while(press_key)	{
			if(press_key & 0x01)	printk("PRESS KEY : %s", (char *)&key_table[i * 20]);
			i++;					press_key >>= 1;
		}
		
		i = 0;
		while(release_key)	{
			if(release_key & 0x01)	printk("RELEASE KEY : %s", (char *)&key_table[i * 20]);
			i++;					release_key >>= 1;
		}
	}
#endif

//[*]--------------------------------------------------------------------------------------------------[*]
static  int ExternalKeyPadData = 0;

void    odroid_keypad_set_data(int ext_keypad_data)
{
    ExternalKeyPadData = ext_keypad_data;
}

EXPORT_SYMBOL(odroid_keypad_set_data);
       
//[*]--------------------------------------------------------------------------------------------------[*]
static int	odroid_keypad_get_data(void)
{
	int		key_data = 0;

	key_data |= (gpio_get_value(sControlGpios[KEYPAD_POWER].gpio) 		? 0 : 0x01);
    key_data |= ExternalKeyPadData;
    
	return	key_data;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static void odroid_keypad_control(odroid_keypad_t *keypad)
{
	static	unsigned short	prev_keypad_data = 0, cur_keypad_data = 0;

	// key data process
	cur_keypad_data = odroid_keypad_get_data();

	if(prev_keypad_data != cur_keypad_data)	{
		
		generate_keycode(keypad, prev_keypad_data, cur_keypad_data, &Keycode[0]);

		#if defined(DEBUG_MSG)
			debug_keycode_printf(prev_keypad_data, cur_keypad_data, &KeyMapStr[0][0]);
		#endif

		prev_keypad_data = cur_keypad_data;

		input_sync(keypad->input);
	}
}

//[*]--------------------------------------------------------------------------------------------------[*]
static enum hrtimer_restart odroid_keypad_timer(struct hrtimer *timer)
{
	odroid_keypad_t		*keypad = container_of(timer, odroid_keypad_t, timer);

	odroid_keypad_control(keypad);
	hrtimer_start(&keypad->timer, ktime_set(0, KEYPAD_TIMER_PERIOD), HRTIMER_MODE_REL);
	
	return HRTIMER_NORESTART;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int	odroid_keypad_open(struct input_dev *dev)
{
	#if	defined(DEBUG_MSG)
		printk("%s\n", __FUNCTION__);
	#endif
	
	return	0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static void	odroid_keypad_close(struct input_dev *dev)
{
	#if	defined(DEBUG_MSG)
		printk("%s\n", __FUNCTION__);
	#endif
}

//[*]--------------------------------------------------------------------------------------------------[*]
static void	odroid_keypad_release_device(struct device *dev)
{
	#if	defined(DEBUG_MSG)
		printk("%s\n", __FUNCTION__);
	#endif
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int	odroid_keypad_resume(struct platform_device *pdev)
{
	odroid_keypad_t		*keypad = dev_get_drvdata(&pdev->dev);

	#if	defined(DEBUG_PM_MSG)
		printk("%s\n", __FUNCTION__);
	#endif
	
	hrtimer_start(&keypad->timer, ktime_set(0, KEYPAD_TIMER_PERIOD), HRTIMER_MODE_REL);

    return  0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int	odroid_keypad_suspend(struct platform_device *pdev, pm_message_t state)
{
	odroid_keypad_t		*keypad = dev_get_drvdata(&pdev->dev);
	
	#if	defined(DEBUG_PM_MSG)
		printk("%s\n", __FUNCTION__);
	#endif

	hrtimer_cancel(&keypad->timer);
	
    return  0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static void	odroid_keypad_config(odroid_keypad_t *keypad)
{
	int	i;

	// Control GPIO Init
	for (i = 0; i < ARRAY_SIZE(sControlGpios); i++) {
		if(gpio_request(sControlGpios[i].gpio, sControlGpios[i].name))	{
			printk("--------------------------------------------\n");
			printk("%s : %s gpio reqest err!\n", __FUNCTION__, sControlGpios[i].name);
			printk("--------------------------------------------\n");
		}
		else	{
			if(sControlGpios[i].output)		gpio_direction_output	(sControlGpios[i].gpio, sControlGpios[i].value);
			else							gpio_direction_input	(sControlGpios[i].gpio);

			s3c_gpio_setpull(sControlGpios[i].gpio, sControlGpios[i].pud);
		}
	}

	hrtimer_start(&keypad->timer, ktime_set(0, KEYPAD_TIMER_PERIOD), HRTIMER_MODE_REL);
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int __devinit    odroid_keypad_probe(struct platform_device *pdev)
{
    int 				key, code;
	odroid_keypad_t		*keypad;

	if(!(keypad = kzalloc(sizeof(*keypad), GFP_KERNEL)))	return	-ENOMEM;

	dev_set_drvdata(&pdev->dev, keypad);

	if(!(keypad->input 	= input_allocate_device()))		goto err_free_input_mem;

	snprintf(keypad->phys, sizeof(keypad->phys), "%s/input0", DEVICE_NAME);

	keypad->input->name 		= DEVICE_NAME;
	keypad->input->phys 		= keypad->phys;
	keypad->input->id.bustype 	= BUS_HOST;
	keypad->input->id.vendor 	= 0x16B4;
	keypad->input->id.product 	= 0x0701;
	keypad->input->id.version 	= 0x0001;
	keypad->input->keycode 		= Keycode;
	keypad->input->open 		= odroid_keypad_open;
	keypad->input->close 		= odroid_keypad_close;

	set_bit(EV_KEY, keypad->input->evbit);

	for(key = 0; key < MAX_KEYCODE_CNT; key++){
		code = Keycode[key];
		if(code <= 0)	continue;
		set_bit(code & KEY_MAX, keypad->input->keybit);
	}
	if(input_register_device(keypad->input))	{
		printk("--------------------------------------------------------\n");
		printk("%s input register device fail!!\n", DEVICE_NAME);
		printk("--------------------------------------------------------\n");
		goto	err_free_all;
	}

	hrtimer_init(&keypad->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	keypad->timer.function = odroid_keypad_timer;
	
	hrtimer_init(&keypad->poweroff_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	keypad->poweroff_timer.function = odroid_poweroff_timer;

	odroid_keypad_config(keypad);

	printk("--------------------------------------------------------\n");
	printk("%s driver initialized!! Ver 1.0\n", DEVICE_NAME);
	printk("--------------------------------------------------------\n");

    return 	0;
    
err_free_all:
	input_free_device(keypad->input);
err_free_input_mem:
	kfree(keypad);
	return	-ENODEV;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int __devexit    odroid_keypad_remove(struct platform_device *pdev)
{
	int 				i;
	odroid_keypad_t		*keypad = dev_get_drvdata(&pdev->dev);
	
	input_unregister_device(keypad->input);
	
	hrtimer_cancel(&keypad->timer);
	
	dev_set_drvdata(&pdev->dev, NULL);

	for (i = 0; i < ARRAY_SIZE(sControlGpios); i++) 	gpio_free(sControlGpios[i].gpio);

	#if	defined(DEBUG_MSG)
		printk("%s\n", __FUNCTION__);
	#endif
	
	kfree(keypad);
	
	return  0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int __init	odroid_keypad_init(void)
{
	int ret = platform_driver_register(&odroid_platform_device_driver);
	
	#if	defined(DEBUG_MSG)
		printk("%s\n", __FUNCTION__);
	#endif
	
	if(!ret)        {
		ret = platform_device_register(&odroid_platform_device);
		
		#if	defined(DEBUG_MSG)
			printk("platform_driver_register %d \n", ret);
		#endif
		
		if(ret)	platform_driver_unregister(&odroid_platform_device_driver);
	}
	return ret;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static void __exit	odroid_keypad_exit(void)
{
	#if	defined(DEBUG_MSG)
		printk("%s\n",__FUNCTION__);
	#endif
	
	platform_device_unregister(&odroid_platform_device);
	platform_driver_unregister(&odroid_platform_device_driver);
}

//[*]--------------------------------------------------------------------------------------------------[*]
