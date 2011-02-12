#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/stat.h>	 /* permission constants */
#include <linux/io.h>
#include <linux/vmalloc.h>
#include <asm/io.h>
#include <asm/sizes.h>
#include <mach/iomux.h>
#include <mach/gpio.h>
//#include <asm/arch/iomux.h>
#include <linux/delay.h>

#include "rk29_modem.h"

/****************************************************************
	 huawei-em660/em660c/em770
	 zte-ad3812/mf210/mu301
	 thinkwill-me800								

*****************************************************************/

static int modem_enable(void){
	printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);
	// PG1: 3G reset
         /*arch/arm/mach-rockchip/iomux.c*/
//	rockchip_mux_api_set(G3_RESET_IOMUX_NAME, G3_RESET_IOMUX_MODE);
	// PB0: 3G poweron
//	rockchip_mux_api_set(G3_POWER_ON_IOMUX_NAME, G3_POWER_ON_IOMUX_MODE);
	// PG0: 3G Radio On/Off
//	rockchip_mux_api_set(G3_RADIO_ON_OFF_IOMUX_NAME, G3_RADIO_ON_OFF_IOMUX_MODE);
//	msleep(10);
	
	/*3G Modem Power On*/
        int ret = gpio_request(G3_POWER_ON, NULL);
        if(ret != 0)
        {
            gpio_free(G3_POWER_ON);
            printk(">>>>>> G3_POWER_ON gpio_request err \n ");
    	    return ret;
        }
        gpio_direction_output(G3_POWER_ON, G3_POWER_ENABLE);
//        gpio_set_value(FB_LCD_CABC_EN_PIN, GPIO_LOW);

//	GPIOSetPinDirection(G3_POWER_ON, GPIO_OUT);
//	GPIOSetPinLevel(G3_POWER_ON, G3_POWER_ENABLE);
	msleep(100);
	gpio_free(G3_POWER_ON);
	
	/*3G Modem Radio On*/
//	GPIOSetPinDirection(G3_RADIO_ON_OFF, GPIO_OUT);
//	GPIOSetPinLevel(G3_RADIO_ON_OFF, G3_RADIO_ENABLE);
//	msleep(100);

	/*3G Modem Reset Controll if needed*/
//	if(G3_RESET){
//		GPIOSetPinDirection(G3_RESET, GPIO_OUT);
//		GPIOSetPinLevel(G3_RESET, G3_RESET_ENABLE);
//		msleep(120);
//		GPIOSetPinLevel(G3_RESET, G3_RESET_DISABLE);
//	}
	return 0;
}

static int modem_disable(void){
	printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);
	// PG1: 3G reset
//	rockchip_mux_api_set(G3_RESET_IOMUX_NAME, G3_RESET_IOMUX_MODE);
	// PB0: 3G poweron
//	rockchip_mux_api_set(G3_POWER_ON_IOMUX_NAME, G3_POWER_ON_IOMUX_MODE);
	// PG0: 3G On/Off
//	rockchip_mux_api_set(G3_RADIO_ON_OFF_IOMUX_NAME, G3_RADIO_ON_OFF_IOMUX_MODE);
//	msleep(10);

#if 1
        int ret = gpio_request(G3_POWER_ON, NULL);
        if(ret != 0)
        {
            gpio_free(G3_POWER_ON);
            printk(">>>>>> G3_POWER_ON gpio_request err \n ");
	    return ret;
        }
        gpio_direction_output(G3_POWER_ON, G3_POWER_DISABLE);
#else	
	/*3G Modem Power off*/
	GPIOSetPinDirection(G3_POWER_ON, GPIO_OUT);
	GPIOSetPinLevel(G3_POWER_ON, G3_POWER_DISABLE);
#endif
	msleep(100);
	gpio_free(G3_POWER_ON);

	/*3G Modem  Radio off*/
//	GPIOSetPinDirection(G3_RADIO_ON_OFF, GPIO_OUT);
//	GPIOSetPinLevel(G3_RADIO_ON_OFF, G3_RADIO_DISABLE);
//	msleep(10);

	/*3G Modem Reset enable if needed*/
//	if(G3_RESET){
//		GPIOSetPinDirection(G3_RESET, GPIO_OUT);
//		GPIOSetPinLevel(G3_RESET, G3_RESET_ENABLE);
//	}
	return 0;
}

static int modem_sleep(void){
	printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);
	
	return 0;
}

static int modem_init(void){
    return modem_disable();
}

struct rk29_modem_t rk29_modem = {
	.name    = "ThinkWill_ME800",
	.enable  = modem_enable,
	.disable = modem_disable,
	.sleep   = modem_sleep,
	.init    = modem_init,
};

static int __init rk29_modem_init(void)
{
	return rk29_modem_register(&rk29_modem);
}

static void __exit rk29_modem_exit(void)
{
	rk29_modem_unregister(&rk29_modem);
}


late_initcall(rk29_modem_init);
//module_init(rk29_modem_init);
module_exit(rk29_modem_exit);

MODULE_AUTHOR("lintao lintao@rock-chips.com");
MODULE_DESCRIPTION("ROCKCHIP modem driver");
MODULE_LICENSE("GPL");


