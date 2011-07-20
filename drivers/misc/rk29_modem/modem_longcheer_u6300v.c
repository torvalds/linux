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
#include <linux/delay.h>

#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <mach/board.h>

#include <linux/platform_device.h>

#include "rk29_modem.h"

// 确保不出现重复处理wakeup
static int do_wakeup_handle = 0;
static irqreturn_t u6300v_irq_handler(int irq, void *dev_id);
static int __devinit u6300v_resume(struct platform_device *pdev);

static struct rk29_io_t u6300v_io_ap_ready = {
    .io_addr    = RK29_PIN3_PC2,
    .enable     = GPIO_LOW,
    .disable    = GPIO_HIGH,
};

static struct rk29_io_t u6300v_io_power = {
    .io_addr    = RK29_PIN6_PB1,
    .enable     = GPIO_HIGH,
    .disable    = GPIO_LOW,
};

static struct rk29_irq_t u6300v_irq_bp_wakeup_ap= {
    .irq_addr   = RK29_PIN3_PD7,
    .irq_trigger = IRQF_TRIGGER_FALLING, // 下降沿触发
};

static struct platform_driver u6300v_platform_driver = {
	.driver		= {
		.name		= "longcheer_u6300v",
	},
	.suspend    = rk29_modem_suspend,
	.resume     = rk29_modem_resume,
};

static struct rk29_modem_t u6300v_driver = {
    .driver         = &u6300v_platform_driver,
    .modem_power    = &u6300v_io_power,
    .ap_ready       = &u6300v_io_ap_ready,
    .bp_wakeup_ap   = &u6300v_irq_bp_wakeup_ap,
    .status         = MODEM_ENABLE,
    .dev_init       = NULL,
    .dev_uninit     = NULL,
    .irq_handler    = u6300v_irq_handler,
    .suspend        = NULL,
    .resume         = u6300v_resume,
    
    .enable         = NULL,
    .disable        = NULL,
    .sleep          = NULL,
    .wakeup         = NULL,
};

static void do_test1(struct work_struct *work)
{
    printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);
    // 标志AP已就绪，BB可以上报数据给AP
    gpio_direction_output(u6300v_driver.ap_ready->io_addr, u6300v_driver.ap_ready->enable);
}

static DECLARE_DELAYED_WORK(test1, do_test1);

static int __devinit u6300v_resume(struct platform_device *pdev)
{
    printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);

/* cmy: 目前在系统被唤醒后，在这边设置AP_RDY，但由于通信依赖于其它的设备驱动(比如USB或者串口)
        需要延时设置AP_RDY信号
        更好的做法是在它所依赖的设备就绪后(唤醒)，再设置AP_RDY。做法两种:
            1 将设置AP_RDY的函数注入到目标设备的resume函数中
            2 在rk29_modem_resume中，等待目标设备resume之后，再设置AP_RDY
 */
    schedule_delayed_work(&test1, 2*HZ);

    return 0;
}

/*
    u6300v 模组的 IRQ 处理函数，该函数由rk29_modem中的IRQ处理函数调用
 */
static irqreturn_t u6300v_irq_handler(int irq, void *dev_id)
{
    printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);

    if( irq == gpio_to_irq(u6300v_driver.bp_wakeup_ap->irq_addr) )
    {
        if( !do_wakeup_handle )
        {
            do_wakeup_handle = 1;
            // 当接收到 bb wakeup ap 的IRQ后，申请一个8秒的suspend锁，时间到后自动释放
            // 释放时如果没有其它的锁，就将再次挂起.
            wake_lock_timeout(&u6300v_driver.wakelock_bbwakeupap, 8 * HZ);
        } else
            printk("%s: already wakeup\n", __FUNCTION__);
        return IRQ_HANDLED;
    }
    
    return IRQ_NONE;
}

static int __init u6300v_init(void)
{
    printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);

    return rk29_modem_init(&u6300v_driver);
}

static void __exit u6300v_exit(void)
{
    printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);
    rk29_modem_exit();
}

module_init(u6300v_init);
module_exit(u6300v_exit);

MODULE_AUTHOR("lintao lintao@rock-chips.com");
MODULE_DESCRIPTION("ROCKCHIP modem driver");
MODULE_LICENSE("GPL");

#if 0
int test(void)
{
    printk(">>>>>> test \n ");
    int ret = gpio_request(IRQ_BB_WAKEUP_AP, NULL);
    if(ret != 0)
    {
        printk(">>>>>> gpio_request failed! \n ");
        gpio_free(IRQ_BB_WAKEUP_AP);
        return ret;
    }

//    printk(">>>>>> set GPIOPullUp \n ");
//    gpio_pull_updown(IRQ_BB_WAKEUP_AP, GPIOPullUp);
//    printk(">>>>>> set GPIO_HIGH \n ");
//    gpio_direction_output(IRQ_BB_WAKEUP_AP, GPIO_HIGH);

//    printk(">>>>>> set GPIO_LOW \n ");
//    gpio_direction_output(IRQ_BB_WAKEUP_AP, GPIO_LOW);
//    msleep(1000);
    
    gpio_free(IRQ_BB_WAKEUP_AP);

    printk(">>>>>> END \n ");
}
#endif

