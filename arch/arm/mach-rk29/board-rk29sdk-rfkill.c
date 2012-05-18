/*
 * Copyright (C) 2010 ROCKCHIP, Inc.
 * Author: roger_chen <cz@rock-chips.com>
 *
 * This program is the bluetooth device bcm4329's driver,
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
#include <linux/interrupt.h>
#include <linux/wakelock.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <mach/gpio.h>
#include <asm/irq.h>
#include <mach/iomux.h>
#include <linux/wakelock.h>
#include <linux/timer.h>
#include <mach/board.h>

#if 0
#define DBG(x...)   printk(KERN_INFO x)
#else
#define DBG(x...)
#endif

#ifdef CONFIG_BCM4329
#define WIFI_BT_POWER_TOGGLE	1
#else
#define WIFI_BT_POWER_TOGGLE	0
#endif

#define BT_WAKE_LOCK_TIMEOUT    10 //s

/*
 * IO Configuration for RK29
 */
#ifdef CONFIG_ARCH_RK29

#define BT_WAKE_HOST_SUPPORT    0

/* IO configuration */
// BT power pin
#define BT_GPIO_POWER           RK29_PIN5_PD6
#define IOMUX_BT_GPIO_POWER()     rk29_mux_api_set(GPIO5D6_SDMMC1PWREN_NAME, GPIO5H_GPIO5D6);

// BT reset pin
#define BT_GPIO_RESET           RK29_PIN6_PC4
#define IOMUX_BT_GPIO_RESET()

// BT wakeup pin
#define BT_GPIO_WAKE_UP         RK29_PIN6_PC5
#define IOMUX_BT_GPIO_WAKE_UP()

// BT wakeup host pin
#define BT_GPIO_WAKE_UP_HOST
#define IOMUX_BT_GPIO_WAKE_UP_HOST()

//bt cts paired to uart rts
#define UART_RTS                RK29_PIN2_PA7
#define IOMUX_UART_RTS_GPIO()   rk29_mux_api_set(GPIO2A7_UART2RTSN_NAME, GPIO2L_GPIO2A7)
#define IOMUX_UART_RTS()        rk29_mux_api_set(GPIO2A7_UART2RTSN_NAME, GPIO2L_UART2_RTS_N)

/*
 * IO Configuration for RK30
 */
#elif defined (CONFIG_ARCH_RK30)

#define BT_WAKE_HOST_SUPPORT    1

/* IO configuration */
// BT power pin
#define BT_GPIO_POWER           RK30_PIN3_PC7
#define IOMUX_BT_GPIO_POWER()     rk29_mux_api_set(GPIO3C7_SDMMC1WRITEPRT_NAME, GPIO3C_GPIO3C7);

// BT reset pin
#define BT_GPIO_RESET           RK30_PIN3_PD1
#define IOMUX_BT_GPIO_RESET()     rk29_mux_api_set(GPIO3D1_SDMMC1BACKENDPWR_NAME, GPIO3D_GPIO3D1);

// BT wakeup pin
#define BT_GPIO_WAKE_UP         RK30_PIN3_PC6
#define IOMUX_BT_GPIO_WAKE_UP() rk29_mux_api_set(GPIO3C6_SDMMC1DETECTN_NAME, GPIO3C_GPIO3C6);

// BT wakeup host pin
#define BT_GPIO_WAKE_UP_HOST    RK30_PIN6_PA7
#define IOMUX_BT_GPIO_WAKE_UP_HOST()

//bt cts paired to uart rts
#define UART_RTS                RK30_PIN1_PA3
#define IOMUX_UART_RTS_GPIO()     rk29_mux_api_set(GPIO1A3_UART0RTSN_NAME, GPIO1A_GPIO1A3)
#define IOMUX_UART_RTS()          rk29_mux_api_set(GPIO1A3_UART0RTSN_NAME, GPIO1A_UART0_RTS_N)

#endif

struct bt_ctrl
{
    struct rfkill *bt_rfk;
#if BT_WAKE_HOST_SUPPORT
    struct timer_list tl;
    bool b_HostWake;
    struct wake_lock bt_wakelock;
#endif
};

static const char bt_name[] = 
#if defined(CONFIG_RKWIFI)
    #if defined(CONFIG_RKWIFI_26M)
        "rk903_26M"
    #else
        "rk903"
    #endif
#elif defined(CONFIG_BCM4329)
    "bcm4329"
#elif defined(CONFIG_MV8787)
    "mv8787"
#else
    "bt_default"
#endif
;

#if WIFI_BT_POWER_TOGGLE
extern int rk29sdk_bt_power_state;
extern int rk29sdk_wifi_power_state;
#endif

struct bt_ctrl gBtCtrl;
    
#if BT_WAKE_HOST_SUPPORT
void resetBtHostSleepTimer(void)
{
    mod_timer(&(gBtCtrl.tl),jiffies + BT_WAKE_LOCK_TIMEOUT*HZ);//再重新设置超时值。    
}

void btWakeupHostLock(void)
{
    if(gBtCtrl.b_HostWake == false){
        DBG("*************************Lock\n");
        wake_lock(&(gBtCtrl.bt_wakelock));
        gBtCtrl.b_HostWake = true;
    }
}

void btWakeupHostUnlock(void)
{
    if(gBtCtrl.b_HostWake == true){        
        DBG("*************************UnLock\n");
        wake_unlock(&(gBtCtrl.bt_wakelock));  //让系统睡眠    
        gBtCtrl.b_HostWake = false;
    }    
}

static void timer_hostSleep(unsigned long arg)
{     
	DBG("%s---b_HostWake=%d\n",__FUNCTION__,gBtCtrl.b_HostWake);
    btWakeupHostUnlock();
}

extern int bcm4325_sleep(int bSleep);

#ifdef CONFIG_PM
static void rfkill_do_wakeup(struct work_struct *work)
{
    DBG("Enable UART_RTS\n");
    gpio_set_value(UART_RTS, GPIO_LOW);
    IOMUX_UART_RTS();
}

static DECLARE_DELAYED_WORK(wakeup_work, rfkill_do_wakeup);

static int bcm4329_rfkill_suspend(struct platform_device *pdev, pm_message_t state)
{
    DBG("%s\n",__FUNCTION__);

#ifdef CONFIG_BT_HCIBCM4325
    bcm4325_sleep(1);
#endif

    DBG("Disable UART_RTS\n");
	//To prevent uart to receive bt data when suspended
	IOMUX_UART_RTS_GPIO();
	gpio_request(UART_RTS, "uart_rts");
	gpio_set_value(UART_RTS, GPIO_HIGH);

#ifdef CONFIG_RFKILL_RESET
    extern void rfkill_set_block(struct rfkill *rfkill, bool blocked);
    printk("rfkill_set_block\n");
    rfkill_set_block(gBtCtrl.bt_rfk, true);
#endif

    return 0;
}

static int bcm4329_rfkill_resume(struct platform_device *pdev)
{  
    DBG("%s\n",__FUNCTION__);     

#ifdef CONFIG_BT_HCIBCM4325
    bcm4325_sleep(0);
#endif

    DBG("delay 1s\n");
    schedule_delayed_work(&wakeup_work, HZ);

    return 0;
}
#else
#define bcm4329_rfkill_suspend NULL
#define bcm4329_rfkill_resume  NULL
#endif

static irqreturn_t bcm4329_wake_host_irq(int irq, void *dev)
{
    DBG("%s\n",__FUNCTION__);

    btWakeupHostLock();
    resetBtHostSleepTimer();
	return IRQ_HANDLED;
}
#endif

int bcm4325_sleep(int bSleep)
{
    DBG("************* bt enter sleep: %d ***************\n", bSleep);
    //low represent bt device may enter sleep
    //high represent bt device must be awake 
    IOMUX_BT_GPIO_WAKE_UP();
    gpio_set_value(BT_GPIO_WAKE_UP, bSleep?GPIO_LOW:GPIO_HIGH);
    return 0;
}
  
static int bcm4329_set_block(void *data, bool blocked)
{
	DBG("%s---blocked :%d\n", __FUNCTION__, blocked);

    IOMUX_BT_GPIO_POWER();
    IOMUX_BT_GPIO_RESET();

	if (false == blocked) { 
   		gpio_set_value(BT_GPIO_POWER, GPIO_HIGH);  /* bt power on */
		mdelay(20);

        gpio_set_value(BT_GPIO_RESET, GPIO_LOW);
        mdelay(20);
		gpio_set_value(BT_GPIO_RESET, GPIO_HIGH);  /* bt reset deactive*/

        mdelay(20);
        bcm4325_sleep(0); // ensure bt is wakeup
        
    	pr_info("bt turn on power\n");
	} else {
#if WIFI_BT_POWER_TOGGLE
		if (!rk29sdk_wifi_power_state) {
#endif
			gpio_set_value(BT_GPIO_POWER, GPIO_LOW);  /* bt power off */
    		mdelay(20);	
    		pr_info("bt shut off power\n");
#if WIFI_BT_POWER_TOGGLE
		}else {
			pr_info("bt shouldn't shut off power, wifi is using it!\n");
		}
#endif

        gpio_set_value(BT_GPIO_RESET, GPIO_LOW);  /* bt reset active*/
        mdelay(20);
	}

#if WIFI_BT_POWER_TOGGLE
	rk29sdk_bt_power_state = !blocked;
#endif
	return 0;
}

static const struct rfkill_ops bcm4329_rfk_ops = {
	.set_block = bcm4329_set_block,
};

static int __devinit bcm4329_rfkill_probe(struct platform_device *pdev)
{
	int rc = 0;
	bool default_state = true;
	
	DBG("Enter::%s,line=%d\n",__FUNCTION__,__LINE__);
	
	/* default to bluetooth off */
 	bcm4329_set_block(NULL, default_state); /* blocked -> bt off */
	 
	gBtCtrl.bt_rfk = rfkill_alloc(bt_name, 
                NULL, 
                RFKILL_TYPE_BLUETOOTH, 
                &bcm4329_rfk_ops, 
                NULL);

	if (!gBtCtrl.bt_rfk)
	{
		printk("fail to rfkill_allocate************\n");
		return -ENOMEM;
	}
	
	rfkill_set_states(gBtCtrl.bt_rfk, default_state, false);

	rc = rfkill_register(gBtCtrl.bt_rfk);
	if (rc)
	{
		printk("failed to rfkill_register,rc=0x%x\n",rc);
		rfkill_destroy(gBtCtrl.bt_rfk);
	}
	
	gpio_request(BT_GPIO_POWER, NULL);
	gpio_request(BT_GPIO_RESET, NULL);
#ifdef CONFIG_BT_HCIBCM4325
	gpio_request(BT_GPIO_WAKE_UP, NULL);
#endif

#if BT_WAKE_HOST_SUPPORT
    init_timer(&(gBtCtrl.tl));
    gBtCtrl.tl.expires = 0;
    gBtCtrl.tl.function = timer_hostSleep;
    add_timer(&(gBtCtrl.tl));
    gBtCtrl.b_HostWake = false;
    
	wake_lock_init(&(gBtCtrl.bt_wakelock), WAKE_LOCK_SUSPEND, "bt_wake");
	
	rc = gpio_request(BT_GPIO_WAKE_UP_HOST, "bt_wake");
	if (rc) {
		printk("%s:failed to request BT_WAKE_UP_HOST\n",__FUNCTION__);
	}
	
	IOMUX_BT_GPIO_WAKE_UP_HOST();
	gpio_pull_updown(BT_GPIO_WAKE_UP_HOST,GPIOPullUp);
	rc = request_irq(gpio_to_irq(BT_GPIO_WAKE_UP_HOST),bcm4329_wake_host_irq,IRQF_TRIGGER_FALLING,"bt_wake",NULL);
	if(rc)
	{
		printk("%s:failed to request BT_WAKE_UP_HOST irq\n",__FUNCTION__);
		gpio_free(BT_GPIO_WAKE_UP_HOST);
	}
	enable_irq_wake(gpio_to_irq(BT_GPIO_WAKE_UP_HOST)); // so BT_WAKE_UP_HOST can wake up system

	printk(KERN_INFO "bcm4329 module has been initialized,rc=0x%x\n",rc);
 #endif
 
	return rc;
}


static int __devexit bcm4329_rfkill_remove(struct platform_device *pdev)
{
	if (gBtCtrl.bt_rfk)
		rfkill_unregister(gBtCtrl.bt_rfk);
	gBtCtrl.bt_rfk = NULL;
#if BT_WAKE_HOST_SUPPORT	
    del_timer(&(gBtCtrl.tl));//删掉定时器
    btWakeupHostUnlock();
    wake_lock_destroy(&(gBtCtrl.bt_wakelock));
#endif    
	platform_set_drvdata(pdev, NULL);

	DBG("Enter::%s,line=%d\n",__FUNCTION__,__LINE__);
	return 0;
}

static struct platform_driver bcm4329_rfkill_driver = {
	.probe = bcm4329_rfkill_probe,
	.remove = __devexit_p(bcm4329_rfkill_remove),
	.driver = {
		.name = "rk29sdk_rfkill", 
		.owner = THIS_MODULE,
	},	
#if BT_WAKE_HOST_SUPPORT
    .suspend = bcm4329_rfkill_suspend,
    .resume = bcm4329_rfkill_resume,
#endif
};

/*
 * Module initialization
 */
static int __init bcm4329_mod_init(void)
{
	int ret;
	DBG("Enter::%s,line=%d\n",__FUNCTION__,__LINE__);
	ret = platform_driver_register(&bcm4329_rfkill_driver);
    	printk("ret=0x%x\n", ret);
	return ret;
}

static void __exit bcm4329_mod_exit(void)
{
	platform_driver_unregister(&bcm4329_rfkill_driver);
}

module_init(bcm4329_mod_init);
module_exit(bcm4329_mod_exit);
MODULE_DESCRIPTION("bcm4329 Bluetooth driver");
MODULE_AUTHOR("roger_chen cz@rock-chips.com, cmy@rock-chips.com");
MODULE_LICENSE("GPL");

