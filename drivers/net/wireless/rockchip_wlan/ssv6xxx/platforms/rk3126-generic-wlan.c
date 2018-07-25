/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or 
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/irq.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/gpio.h>
#include <linux/mmc/host.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/semaphore.h>
#include <linux/platform_device.h>
#ifdef CONFIG_HAS_WAKELOCK
#include <linux/wakelock.h>
#endif
#include <asm/io.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
#include <linux/printk.h>
#include <linux/err.h>
#else
#include <config/printk.h>
#endif
#if LINUX_VERSION_CODE > KERNEL_VERSION(3,2,0)
#include <linux/wlan_plat.h>
#else
struct wifi_platform_data {
    int (*set_power)(int val);
    int (*set_reset)(int val);
    int (*set_carddetect)(int val);
    void *(*mem_prealloc)(int section, unsigned long size);
    int (*get_mac_addr)(unsigned char *buf);
    void *(*get_country_code)(char *ccode);
};
#endif
extern int rockchip_wifi_power(int on);
extern int rockchip_wifi_set_carddetect(int val);
extern int ssv6xxx_get_dev_status(void);
static struct wifi_platform_data *wifi_control_data = NULL;
#define GPIO_REG_WRITEL(val,reg) do{__raw_writel(val, CTL_PIN_BASE + (reg));}while(0)
unsigned int oob_irq = 0;
static int g_wifidev_registered = 0;
static struct semaphore wifi_control_sem;
extern int rockchip_wifi_get_oob_irq(void);
extern int ssvdevice_init(void);
extern void ssvdevice_exit(void);
#ifdef CONFIG_SSV_SUPPORT_AES_ASM
extern int aes_init(void);
extern void aes_fini(void);
extern int sha1_mod_init(void);
extern void sha1_mod_fini(void);
#endif
#ifdef CONFIG_HAS_WAKELOCK
struct wake_lock icomm_wake_lock;
#endif
static int icomm_wifi_power(int on)
{
    printk("%s: %d\n", __func__, on);
    rockchip_wifi_power(on);
    return 0;
}
int icomm_wifi_set_carddetect(int val)
{
    rockchip_wifi_set_carddetect(val);
    return 0;
}
static struct wifi_platform_data icomm_wifi_control = {
    .set_power = icomm_wifi_power,
    .set_carddetect = icomm_wifi_set_carddetect,
};
void icomm_wifi_device_release(struct device *dev)
{
    printk("%s\n", __func__);
}
static struct platform_device icomm_wifi_device = {
    .name = "icomm_wlan",
    .id = 1,
    .dev = {
            .platform_data = &icomm_wifi_control,
            .release = icomm_wifi_device_release,
     },
};
int wifi_set_power(int on, unsigned long msec)
{
    if (wifi_control_data && wifi_control_data->set_power) {
        wifi_control_data->set_power(on);
    }
    if (msec)
        msleep(msec);
    return 0;
}
static int wifi_set_carddetect(int on)
{
    if (wifi_control_data && wifi_control_data->set_carddetect) {
        wifi_control_data->set_carddetect(on);
    }
    return 0;
}
static irqreturn_t wifi_wakeup_irq_handler(int irq, void *dev)
{
    printk("%s\n", __func__);
    wake_lock_timeout(&icomm_wake_lock, HZ);
    return IRQ_HANDLED;
}
void setup_wifi_wakeup_BB(void)
{
    int err;
    oob_irq = rockchip_wifi_get_oob_irq();
    if (oob_irq <= 0) {
        printk("%s: oob_irq NULL\n", __func__);
        return;
    }
    err = request_threaded_irq(oob_irq,
                               wifi_wakeup_irq_handler,
                               NULL,
                               IRQF_TRIGGER_FALLING,
                               "wlan_wakeup_irq",
                               NULL);
    printk("%s: set oob_irq:%d %s\n", __func__, oob_irq, (err < 0) ? "NG": "OK");
}
void free_wifi_wakeup_BB(void)
{
    if (oob_irq > 0) {
        free_irq(oob_irq, NULL);
        oob_irq = 0;
    }
}
static int wifi_probe(struct platform_device *pdev)
{
    struct wifi_platform_data *wifi_ctrl =
        (struct wifi_platform_data *)(pdev->dev.platform_data);
    printk("%s\n", __func__);
    wifi_control_data = wifi_ctrl;
    wifi_set_power(0, 50);
    wifi_set_power(1, 50);
    wifi_set_carddetect(1);
    msleep(120);
    up(&wifi_control_sem);
    return 0;
}
static int wifi_remove(struct platform_device *pdev)
{
    struct wifi_platform_data *wifi_ctrl =
        (struct wifi_platform_data *)(pdev->dev.platform_data);
    printk("%s\n", __func__);
    wifi_control_data = wifi_ctrl;
    wifi_set_carddetect(0);
    msleep(120);
    wifi_set_power(0, 50);
#ifdef CONFIG_HAS_WAKELOCK
    wake_lock_destroy(&icomm_wake_lock);
#endif
    return 0;
}
#ifdef CONFIG_PM
static int wifi_suspend(struct platform_device *pdev, pm_message_t state)
{
    printk("%s\n", __func__);
    return 0;
}
static int wifi_resume(struct platform_device *pdev)
{
    printk("%s\n", __func__);
#if 0
    if (wifi_control_data && wifi_control_data->set_carddetect) {
        wifi_control_data->set_carddetect(0);
    }
    msleep(50);
    if (wifi_control_data && wifi_control_data->set_carddetect) {
        wifi_control_data->set_carddetect(1);
    }
#endif
    return 0;
}
#endif
static struct platform_driver wifi_driver = {
    .probe = wifi_probe,
    .remove = wifi_remove,
#ifdef CONFIG_PM
    .suspend = wifi_suspend,
    .resume = wifi_resume,
#endif
    .driver = {
    .name = "icomm_wlan",
    }
};
int initWlan(void)
{
    int ret=0;
#ifdef CONFIG_HAS_WAKELOCK
    wake_lock_init(&icomm_wake_lock, WAKE_LOCK_SUSPEND, "ssv6051");
    wake_lock(&icomm_wake_lock);
#endif
    sema_init(&wifi_control_sem, 0);
    platform_device_register(&icomm_wifi_device);
    platform_driver_register(&wifi_driver);
    g_wifidev_registered = 1;
    if (down_timeout(&wifi_control_sem, msecs_to_jiffies(1000)) != 0) {
        ret = -EINVAL;
        printk(KERN_ALERT "%s: platform_driver_register timeout\n", __FUNCTION__);
    }
    ret = ssvdevice_init();
#ifdef CONFIG_HAS_WAKELOCK
    wake_unlock(&icomm_wake_lock);
#endif
    return ret;
}
void exitWlan(void)
{
    if (g_wifidev_registered)
    {
        ssvdevice_exit();
        platform_driver_unregister(&wifi_driver);
        platform_device_unregister(&icomm_wifi_device);
        g_wifidev_registered = 0;
    }
    return;
}
static __init int generic_wifi_init_module(void)
{
    int ret;
    int time = 5;
    printk("%s\n", __func__);
#ifdef CONFIG_SSV_SUPPORT_AES_ASM
    sha1_mod_init();
    aes_init();
#endif
    ret = initWlan();
 while(time-- > 0)
 {
 msleep(500);
 if(ssv6xxx_get_dev_status() == 1)
  break;
 printk("%s : Retry to carddetect\n",__func__);
 wifi_set_carddetect(0);
    wifi_set_power(0, 50);
 msleep(150);
 wifi_set_power(1, 50);
 wifi_set_carddetect(1);
 }
 return ret;
}
static __exit void generic_wifi_exit_module(void)
{
    printk("%s\n", __func__);
#ifdef CONFIG_SSV_SUPPORT_AES_ASM
    aes_fini();
    sha1_mod_fini();
#endif
    exitWlan();
}
EXPORT_SYMBOL(generic_wifi_init_module);
EXPORT_SYMBOL(generic_wifi_exit_module);
module_init(generic_wifi_init_module);
module_exit(generic_wifi_exit_module);
MODULE_LICENSE("Dual BSD/GPL");
