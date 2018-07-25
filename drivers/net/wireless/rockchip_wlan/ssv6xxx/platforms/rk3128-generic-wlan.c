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
#include <asm/io.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
#include <linux/printk.h>
#include <linux/err.h>
#else
#include <config/printk.h>
#endif
extern int rockchip_wifi_power(int on);
extern int rockchip_wifi_set_carddetect(int val);
#ifdef ROCKCHIP_WIFI_AUTO_SUPPORT
extern char wifi_chip_type_string[];
#endif
#define GPIO_REG_WRITEL(val,reg) \
 do { \
  __raw_writel(val, CTL_PIN_BASE + (reg)); \
 } while (0)
static int g_wifidev_registered = 0;
extern int ssvdevice_init(void);
extern void ssvdevice_exit(void);
extern int ssv6xxx_get_dev_status(void);
#ifdef CONFIG_SSV_SUPPORT_AES_ASM
extern int aes_init(void);
extern void aes_fini(void);
extern int sha1_mod_init(void);
extern void sha1_mod_fini(void);
#endif
void ssv_wifi_power(void)
{
 rockchip_wifi_set_carddetect(0);
 msleep(150);
 rockchip_wifi_power(0);
 msleep(150);
 rockchip_wifi_power(1);
 msleep(150);
 rockchip_wifi_set_carddetect(1);
 msleep(150);
}
int initWlan(void)
{
 int ret = 0;
 int time = 5;
 ssv_wifi_power();
 msleep(120);
 g_wifidev_registered = 1;
 ret = ssvdevice_init();
 while(time-- > 0){
  msleep(500);
  if(ssv6xxx_get_dev_status() == 1)
   break;
  printk("%s : Retry to carddetect\n",__func__);
  ssv_wifi_power();
 }
#ifdef ROCKCHIP_WIFI_AUTO_SUPPORT
    if (!ret) {
        strcpy(wifi_chip_type_string, "ssv6051");
        printk(KERN_INFO "wifi_chip_type_string : %s\n" ,wifi_chip_type_string);
    }
#endif
 return ret;
}
void exitWlan(void)
{
    if (g_wifidev_registered)
    {
        ssvdevice_exit();
        msleep(50);
#ifndef ROCKCHIP_WIFI_AUTO_SUPPORT
        rockchip_wifi_set_carddetect(0);
#endif
        rockchip_wifi_power(0);
        g_wifidev_registered = 0;
    }
    return;
}
static __init int generic_wifi_init_module(void)
{
 printk("%s\n", __func__);
#ifdef CONFIG_SSV_SUPPORT_AES_ASM
 sha1_mod_init();
 aes_init();
#endif
 return initWlan();
}
static __exit void generic_wifi_exit_module(void)
{
 printk("%s\n", __func__);
#ifdef CONFIG_SSV_SUPPORT_AES_ASM
 aes_fini();
 sha1_mod_fini();
#endif
    msleep(100);
 exitWlan();
}
EXPORT_SYMBOL(generic_wifi_init_module);
EXPORT_SYMBOL(generic_wifi_exit_module);
module_init(generic_wifi_init_module);
module_exit(generic_wifi_exit_module);
MODULE_LICENSE("Dual BSD/GPL");
