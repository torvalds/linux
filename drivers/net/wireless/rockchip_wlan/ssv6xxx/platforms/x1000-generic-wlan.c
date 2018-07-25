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
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/pm_runtime.h>
#include <linux/mmc/host.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <linux/regulator/consumer.h>
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
#define GPIO_REG_WRITEL(val,reg) do{__raw_writel(val, CTL_PIN_BASE + (reg));}while(0)
static int g_wifidev_registered = 0;
static struct semaphore wifi_control_sem;
static struct wifi_platform_data *wifi_control_data = NULL;
static struct resource *wifi_irqres = NULL;
static int g_wifi_irq_rc=0;
#define SDIO_ID 2
#define IRQ_RES_NAME "ssv_wlan_irq"
#define WIFI_HOST_WAKE 0xFFFF
extern int ssv_6xxx_wlan_init(void);
extern int ssv_wlan_power_on(int flag);
extern int ssv_wlan_power_off(int flag);
static int wifi_pm_gpio_ctrl(char* name, int level)
{
    (void)name;
    (void)level;
    if (level)
    {
        ssv_wlan_power_on(1);
    }
    else
    {
        ssv_wlan_power_off(1);
    }
    return 0;
}
static int ssv_wifi_power(int on)
{
    printk("ssv pwr on=%d\n",on);
 if(on)
 {
  wifi_pm_gpio_ctrl("wifi_ssv6200_power", 0);
  mdelay(50);
        wifi_pm_gpio_ctrl("wifi_ssv6200_power", 1);
 }
 else
 {
  wifi_pm_gpio_ctrl("wifi_ssv6200_power", 0);
 }
    return 0;
}
static int ssv_wifi_reset(int on)
{
    return 0;
}
int ssv_wifi_set_carddetect(int val)
{
    if (val)
    {
        ssv_wlan_power_on(1);
    }
    else
    {
        ssv_wlan_power_off(1);
    }
    return 0;
}
static struct wifi_platform_data ssv_wifi_control = {
    .set_power = ssv_wifi_power,
    .set_reset = ssv_wifi_reset,
    .set_carddetect = ssv_wifi_set_carddetect,
};
static struct resource resources[] = {
        {
                .start = WIFI_HOST_WAKE,
                .flags = IORESOURCE_IRQ,
                .name = IRQ_RES_NAME,
        },
};
void ssv_wifi_device_release(struct device *dev)
{
    printk(KERN_INFO "ssv_wifi_device_release\n");
}
static struct platform_device ssv_wifi_device = {
        .name = "ssv_wlan",
        .id = 1,
        .num_resources = ARRAY_SIZE(resources),
        .resource = resources,
        .dev = {
                .platform_data = &ssv_wifi_control,
                .release = ssv_wifi_device_release,
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
int wifi_set_reset(int on, unsigned long msec)
{
 if (wifi_control_data && wifi_control_data->set_reset) {
  wifi_control_data->set_reset(on);
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
static irqreturn_t wifi_wakeup_irq_handler(int irq, void *dev){
    printk("sdhci_wakeup_irq_handler\n");
     disable_irq_nosync(irq);
         return IRQ_HANDLED;
}
void setup_wifi_wakeup_BB(struct platform_device *pdev, bool bEnable)
{
    int rc=0,ret=0;
    if (bEnable){
      wifi_irqres = platform_get_resource_byname(pdev, IORESOURCE_IRQ, IRQ_RES_NAME);
       rc = (int)wifi_irqres->start;
        g_wifi_irq_rc = rc;
        ret = request_threaded_irq(rc,
                                    NULL,
                                    (void *)wifi_wakeup_irq_handler,
#if LINUX_VERSION_CODE > KERNEL_VERSION(3,0,0)
                                    IRQ_TYPE_LEVEL_HIGH | IRQF_ONESHOT |IRQF_FORCE_RESUME,
#else
                                    IRQ_TYPE_LEVEL_HIGH | IRQF_ONESHOT,
#endif
                                    "wlan_wakeup_irq", NULL);
        enable_irq_wake(g_wifi_irq_rc);
    }else{
        if(g_wifi_irq_rc){
            free_irq(g_wifi_irq_rc,NULL);
            g_wifi_irq_rc = 0;
        }
    }
}
static int wifi_probe(struct platform_device *pdev)
{
 struct wifi_platform_data *wifi_ctrl =
  (struct wifi_platform_data *)(pdev->dev.platform_data);
 printk(KERN_ALERT "wifi_probe\n");
    wifi_control_data = wifi_ctrl;
    #ifndef CONFIG_SSV6XXX
    ssv_6xxx_wlan_init();
    #endif
    wifi_set_carddetect(1);
    up(&wifi_control_sem);
    return 0;
}
static int wifi_remove(struct platform_device *pdev)
{
 struct wifi_platform_data *wifi_ctrl =
  (struct wifi_platform_data *)(pdev->dev.platform_data);
 wifi_control_data = wifi_ctrl;
 wifi_set_power(0, 0);
 wifi_set_carddetect(0);
 setup_wifi_wakeup_BB(pdev,false);
 return 0;
}
static int wifi_suspend(struct platform_device *pdev, pm_message_t state)
{
    setup_wifi_wakeup_BB(pdev,true);
    return 0;
}
static int wifi_resume(struct platform_device *pdev)
{
    setup_wifi_wakeup_BB(pdev,false);
    return 0;
}
static struct platform_driver wifi_driver = {
 .probe = wifi_probe,
 .remove = wifi_remove,
 .suspend = wifi_suspend,
 .resume = wifi_resume,
 .driver = {
 .name = "ssv_wlan",
 }
};
extern int ssvdevice_init(void);
extern void ssvdevice_exit(void);
#ifdef CONFIG_SSV_SUPPORT_AES_ASM
extern int aes_init(void);
extern void aes_fini(void);
extern int sha1_mod_init(void);
extern void sha1_mod_fini(void);
#endif
int initWlan(void)
{
    int ret=0;
    sema_init(&wifi_control_sem, 0);
#ifdef CONFIG_SSV_SUPPORT_AES_ASM
    sha1_mod_init();
    aes_init();
#endif
    platform_device_register(&ssv_wifi_device);
    platform_driver_register(&wifi_driver);
    g_wifidev_registered = 1;
    if (down_timeout(&wifi_control_sem, msecs_to_jiffies(1000)) != 0) {
        ret = -EINVAL;
        printk(KERN_ALERT "%s: platform_driver_register timeout\n", __FUNCTION__);
    }
    ret = ssvdevice_init();
    return ret;
}
void exitWlan(void)
{
    if (g_wifidev_registered)
    {
        ssvdevice_exit();
#ifdef CONFIG_SSV_SUPPORT_AES_ASM
        aes_fini();
        sha1_mod_fini();
#endif
        platform_driver_unregister(&wifi_driver);
        platform_device_unregister(&ssv_wifi_device);
        g_wifidev_registered = 0;
    }
    return;
}
static int generic_wifi_init_module(void)
{
 return initWlan();
}
static void generic_wifi_exit_module(void)
{
 exitWlan();
}
EXPORT_SYMBOL(generic_wifi_init_module);
EXPORT_SYMBOL(generic_wifi_exit_module);
#ifdef CONFIG_SSV6XXX
late_initcall(generic_wifi_init_module);
#else
module_init(generic_wifi_init_module);
#endif
module_exit(generic_wifi_exit_module);
MODULE_LICENSE("Dual BSD/GPL");
