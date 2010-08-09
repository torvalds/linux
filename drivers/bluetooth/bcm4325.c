/*
 * Copyright (C) 2010 ROCKCHIP, Inc.
 * Author: roger_chen <cz@rock-chips.com>
 *
 * This program is the bluetooth device bcm4325's driver,
 *
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/rfkill.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/slab.h>
//#include <asm/gpio.h>
//#include <asm/arch/gpio.h>
//#include <asm/arch/iomux.h>
//#include <asm/arch/gpio.h>
#include <linux/interrupt.h>
#include <linux/wakelock.h>
#include <mach/spi_fpga.h>
#include <linux/fs.h>
#include <asm/uaccess.h>

#define BT_PWR_ON		{spi_gpio_set_pinlevel(SPI_GPIO_P1_06, SPI_GPIO_HIGH);	\
            			    spi_gpio_set_pindirection(SPI_GPIO_P1_06, SPI_GPIO_OUT);}
#define BT_PWR_OFF		{spi_gpio_set_pinlevel(SPI_GPIO_P1_06, SPI_GPIO_LOW);	\
            				spi_gpio_set_pindirection(SPI_GPIO_P1_06, SPI_GPIO_OUT);}

#define BT_RESET_HIGH	{spi_gpio_set_pinlevel(SPI_GPIO_P1_07, SPI_GPIO_HIGH);	\
            				spi_gpio_set_pindirection(SPI_GPIO_P1_07, SPI_GPIO_OUT);}
#define BT_RESET_LOW	{spi_gpio_set_pinlevel(SPI_GPIO_P1_07, SPI_GPIO_LOW);	\
            				spi_gpio_set_pindirection(SPI_GPIO_P1_07, SPI_GPIO_OUT);}

#define BT_SLEEP_GPIO_IOMUX		    
#define BT_SLEEP_GPIO_SET_OUT		spi_gpio_set_pindirection(SPI_GPIO_P1_08, SPI_GPIO_OUT);
#define BT_WAKEUP					//spi_gpio_set_pinlevel(SPI_GPIO_P1_08, SPI_GPIO_HIGH);
#define BT_SLEEP					//spi_gpio_set_pinlevel(SPI_GPIO_P1_08, SPI_GPIO_LOW);

#if 1
#define DBG(x...)   printk(KERN_INFO x)
#else
#define DBG(x...)
#endif

static struct rfkill *bt_rfk;
static const char bt_name[] = "bcm4325";
//extern void rfkill_switch_all(enum rfkill_type type, bool blocked);

  
/*
bSleep:
0: wakeup
1: sleep
*/
int bcm4325_sleep(int bSleep)
{
	if(0 == bSleep)	/*wake up*/
	{
		BT_WAKEUP;
	}
	else		/*sleep*/
	{
		BT_SLEEP;
	}
    
    return 0;
}

static int bcm4325_set_block(void *data, bool blocked)
{
    DBG("%s---blocked :%d\n", __FUNCTION__, blocked);

    if (false == blocked) {          
        BT_SLEEP_GPIO_IOMUX;
        BT_SLEEP_GPIO_SET_OUT;
        BT_PWR_ON;
        mdelay(2);
        BT_RESET_LOW;       
        mdelay(40);
        BT_RESET_HIGH;
        mdelay(10);
        BT_WAKEUP;
        printk("Enter::%s,bluetooth is power on!\n",__FUNCTION__);
    }
    else {
        BT_SLEEP; 
//        BT_PWR_OFF;
        printk("Enter::%s,bluetooth is power off!\n",__FUNCTION__);
    }

    return 0;
}


static const struct rfkill_ops bcm4325_rfk_ops = {
	.set_block = bcm4325_set_block,
};

static int __init bcm4325_rfkill_probe(struct platform_device *pdev)
{
	int rc = 0;
	
	DBG("Enter::%s,line=%d\n",__FUNCTION__,__LINE__);
	
	/* default to bluetooth off */
//	rfkill_switch_all(RFKILL_TYPE_BLUETOOTH, true);
    
    bt_rfk = rfkill_alloc(bt_name, 
                    NULL, 
                    RFKILL_TYPE_BLUETOOTH, 
                    &bcm4325_rfk_ops, 
                    NULL);

	if (!bt_rfk)
	{
		printk("fail to rfkill_allocate************\n");
		return -ENOMEM;
	}

	
	rc = rfkill_register(bt_rfk);
	if (rc)
		rfkill_destroy(bt_rfk);

    printk("rc=0x%x\n", rc);
    
	return rc;
}


static int __devexit bcm4325_rfkill_remove(struct platform_device *pdev)
{
	if (bt_rfk)
		rfkill_unregister(bt_rfk);
	bt_rfk = NULL;

	platform_set_drvdata(pdev, NULL);
	DBG("Enter::%s,line=%d\n",__FUNCTION__,__LINE__);
	return 0;
}

static struct platform_driver bcm4325_rfkill_driver = {
	.probe = bcm4325_rfkill_probe,
	.remove = __devexit_p(bcm4325_rfkill_remove),
	.driver = {
		.name = "rkbt_rfkill",  //"bcm4325_rfkill",
		.owner = THIS_MODULE,
	},
};

/*
 * Module initialization
 */
static int __init bcm4325_mod_init(void)
{
	int ret;
	DBG("Enter::%s,line=%d\n",__FUNCTION__,__LINE__);
	ret = platform_driver_register(&bcm4325_rfkill_driver);
    printk("ret=0x%x\n", ret);
	return ret;
}

static void __exit bcm4325_mod_exit(void)
{
	platform_driver_unregister(&bcm4325_rfkill_driver);
}

module_init(bcm4325_mod_init);
module_exit(bcm4325_mod_exit);
MODULE_DESCRIPTION("bcm4325 Bluetooth driver");
MODULE_AUTHOR("roger_chen cz@rock-chips.com");
MODULE_LICENSE("GPL");

