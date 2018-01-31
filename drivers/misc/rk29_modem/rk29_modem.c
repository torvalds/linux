/* SPDX-License-Identifier: GPL-2.0 */
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

#include "rk29_modem.h"

struct rk29_modem_t *g_rk29_modem;
//static struct wake_lock wakelock_bbwakeupap;
static struct class *rk29_modem_class = NULL;
static void rk29_modem_turnon(struct rk29_io_t *modem_power, int onoff);

int rk29_modem_change_status(struct rk29_modem_t *rk29_modem, int status)
{
    int ret = 0;
    switch(status)
    {
    case MODEM_DISABLE:
        rk29_modem_turnon(rk29_modem->modem_power, 0);
        break;
    case MODEM_ENABLE :
        rk29_modem_turnon(rk29_modem->modem_power, 1);
        break;
    case MODEM_SLEEP:
        ret = -1;
    case MODEM_WAKEUP:
        ret = -1;
        break;
    }
    
    return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static ssize_t modem_status_write(struct class *cls, struct class_attribute *attr, const char *_buf, size_t _count)
#else
static ssize_t modem_status_write(struct class *cls, const char *_buf, size_t _count)
#endif
{
    struct rk29_modem_t *rk29_modem = g_rk29_modem;
    int ret = 0;
    int new_state = simple_strtoul(_buf, NULL, 16);

    if(rk29_modem == NULL){
        printk("!!!! g_rk29_modem is NULL !!!!\n");
        return _count;
    }
    
    printk("[%s] statue change: %d -> %d\n", __func__, rk29_modem->status, new_state);

    if(new_state == rk29_modem->status) return _count;


    switch(new_state)
    {
    case MODEM_DISABLE:
        if(rk29_modem->disable)
            ret = rk29_modem->disable(rk29_modem);
        else
            ret = rk29_modem_change_status(rk29_modem, new_state);
        break;
    case MODEM_ENABLE :
        if(rk29_modem->enable)
            ret = rk29_modem->enable(rk29_modem);
        else
            ret = rk29_modem_change_status(rk29_modem, new_state);
        break;
    case MODEM_SLEEP:
        if(rk29_modem->sleep)
            ret = rk29_modem->sleep(rk29_modem);
        else
            ret = rk29_modem_change_status(rk29_modem, new_state);
        break;
    case MODEM_WAKEUP:
        if(rk29_modem->wakeup)
            ret = rk29_modem->wakeup(rk29_modem);
        else
            ret = rk29_modem_change_status(rk29_modem, new_state);
        break;
    default:
        ret = -1;
        printk("[%s] Invalid new status: %d\n", __func__, new_state);
        break;
    }

    if( !ret ) rk29_modem->status = new_state;
    
    return _count;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static ssize_t modem_status_read(struct class *cls, struct class_attribute *attr, char *_buf)
#else
static ssize_t modem_status_read(struct class *cls, char *_buf)
#endif
{
    struct rk29_modem_t *rk29_modem = g_rk29_modem;

    return sprintf(_buf, "%d\n", rk29_modem->status);
}
static CLASS_ATTR(modem_status, 0666, modem_status_read, modem_status_write);

int __devinit rk29_modem_suspend(struct platform_device *pdev, pm_message_t state)
{
#ifdef CONFIG_PM
    printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);

    if( g_rk29_modem->suspend )
        g_rk29_modem->suspend(pdev, state);
    else
    {
        if(g_rk29_modem->ap_ready) // 标志AP已挂起
            gpio_direction_output(g_rk29_modem->ap_ready->io_addr, g_rk29_modem->ap_ready->disable);
    }
#endif
	return 0;
}

int __devinit rk29_modem_resume(struct platform_device *pdev)
{
#ifdef CONFIG_PM
    printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);
    if( g_rk29_modem->resume )
        g_rk29_modem->resume(pdev);
    else
    {
        if(g_rk29_modem->ap_ready) // 标志AP已恢复
            gpio_direction_output(g_rk29_modem->ap_ready->io_addr, g_rk29_modem->ap_ready->enable);
    }
#endif
	return 0;
}

static irqreturn_t irq_bbwakeupap_handler(int irq, void *dev_id)
{
    irqreturn_t irqret = IRQ_NONE;
    printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);

    if( g_rk29_modem->irq_handler )
    {
        irqret = g_rk29_modem->irq_handler(irq, dev_id);
        if( irqret != IRQ_NONE ) return irqret;
    }

    // 具体设备没有 irq_handler 函数，或者它的irq_handler函数没有处理该 irq，则使用
    // 以下的公共irq处理.

    return IRQ_HANDLED;
}

static void uninstall_irq(struct rk29_irq_t *irq)
{
    gpio_free(irq->irq_addr);
}

static int install_irq(struct rk29_irq_t *rk29_irq, const char* label)
{
	int ret;
    int irq;
    
    printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);

	irq = gpio_to_irq(rk29_irq->irq_addr);
    printk("%s: %ld ==> %d\n", __func__, rk29_irq->irq_addr, irq);

	ret = gpio_request(rk29_irq->irq_addr, label);
	if (ret < 0) {
		pr_err("%s: gpio_request(%ld) failed\n", __func__, rk29_irq->irq_addr);
		return ret;
	}

	gpio_direction_input(rk29_irq->irq_addr);
    gpio_pull_updown(rk29_irq->irq_addr, 0);
	ret = request_irq(irq, irq_bbwakeupap_handler, rk29_irq->irq_trigger, label, NULL);
	if (ret < 0) {
		pr_err("%s: request_irq(%d) failed\n", __func__, irq);
		gpio_free(rk29_irq->irq_addr);
		return ret;
	}

	enable_irq_wake(irq);
	return 0;
}

// 给modem上电
static void rk29_modem_turnon(struct rk29_io_t *modem_power, int onoff)
{
    if( modem_power )
        gpio_direction_output(modem_power->io_addr, onoff?modem_power->enable:modem_power->disable);
}

/*
    RK29 modem 通用的设备初始化函数
    主要设置各个GPIO以及IRQ的申请等
 */
static int rk29_modem_dev_init(struct rk29_modem_t *rk29_modem)
{
    int ret=0;
    
    printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);

// 申请控制modem电源的GPIO
    if( rk29_modem->modem_power )
    {
        ret = gpio_request(rk29_modem->modem_power->io_addr, "modem_power");
        if(ret != 0)
        {
            gpio_free(rk29_modem->modem_power->io_addr);
            printk(">>>>>> Modem power io request failed!\n");
            return ret;
        }
        
        // 设置初始的modem状态
        rk29_modem_change_status(rk29_modem, rk29_modem->status);
    }

// 申请控制AP就绪状态的GPIO
    if( rk29_modem->ap_ready )
    {
        ret = gpio_request(rk29_modem->ap_ready->io_addr, "ap_ready");
        if(ret != 0)
        {
            gpio_free(rk29_modem->ap_ready->io_addr);
            printk(">>>>>> AP ready io request failed!\n");
            return ret;
        }

        // 默认 AP is ready
        gpio_direction_output(rk29_modem->ap_ready->io_addr, rk29_modem->ap_ready->enable);
    }

// 设置 bb_wakeup_ap 的irq，用于ap挂起之后，由bb来唤醒ap
    if( rk29_modem->bp_wakeup_ap )
    {
        ret = install_irq(rk29_modem->bp_wakeup_ap, "bb_wakeup_ap");
    }
    
    wake_lock_init(&rk29_modem->wakelock_bbwakeupap, WAKE_LOCK_SUSPEND, "bb_wakeup_ap");

    return ret;
}

/*
    modem下电、释放gpio wait_lock 等
 */
static void rk29_modem_dev_uninit(struct rk29_modem_t *rk29_modem)
{
    printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);
    
// 给modem下电
    if( rk29_modem->modem_power )
        rk29_modem_turnon(rk29_modem->modem_power, 0);

// 释放GPIO
    if( rk29_modem->modem_power )
        gpio_free(rk29_modem->modem_power->io_addr);

    if(rk29_modem->ap_ready)
        gpio_free(rk29_modem->ap_ready->io_addr);

    if(rk29_modem->bp_wakeup_ap)
        uninstall_irq(rk29_modem->bp_wakeup_ap);

// 释放 wake lock
    wake_lock_destroy(&rk29_modem->wakelock_bbwakeupap);
}

/*
    驱动加载
 */
int rk29_modem_init(struct rk29_modem_t *rk29_modem)
{
    int retval = 0;
    printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);

    retval = platform_driver_register(rk29_modem->driver);
    if( retval )
    {
        printk("could not register rk29_modem!\n");
        return retval;
    }

	rk29_modem_class = class_create(THIS_MODULE, "rk291x_modem");
    if(rk29_modem_class == NULL){
        printk("create class rk291x_modem failed!\n");
        return -1;
    }
    retval = class_create_file(rk29_modem_class, &class_attr_modem_status);
    if(retval != 0){
        printk("create rk291x_modem class file failed!\n");
        goto failed_create_class;
    }
    
// 对modem初始化
    if( rk29_modem->dev_init )
        retval = rk29_modem->dev_init(rk29_modem);
    else
        retval = rk29_modem_dev_init(rk29_modem);
        
    if( retval )
        goto failed_device_init;

    g_rk29_modem = rk29_modem;

    return 0;

failed_device_init:
    platform_driver_unregister(rk29_modem->driver);

failed_create_class:
    class_destroy(rk29_modem_class);
    
    return retval;
}

/*
    退出驱动时调用
    目前driver是build-in，不存在退出的情况，不会调用到这边
 */
void rk29_modem_exit(void)
{
    printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);
    platform_driver_unregister(g_rk29_modem->driver);

// 对modem反初始化
    if( g_rk29_modem->dev_uninit )
        g_rk29_modem->dev_uninit(g_rk29_modem);
    else
        rk29_modem_dev_uninit(g_rk29_modem);
}

