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
#include <asm/io.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
#include <linux/printk.h>
#include <linux/err.h>
#else
#include <config/printk.h>
#endif
extern void sdio_reinit(void);
extern void extern_wifi_set_enable(int is_on);
extern int wifi_setup_dt(void);
#define GPIO_REG_WRITEL(val,reg) do{__raw_writel(val, CTL_PIN_BASE + (reg));}while(0)
struct semaphore icomm_chipup_sem;
static int g_wifidev_registered = 0;
char ssvcabrio_fw_name[50] = "ssv6051-sw.bin";
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
 if (wifi_setup_dt()) {
  printk("wifi_dt : fail to setup dt\n");
  goto fail;
 }
 extern_wifi_set_enable(0);
 mdelay(200);
 extern_wifi_set_enable(1);
 mdelay(200);
 sdio_reinit();
 mdelay(100);
    g_wifidev_registered = 1;
fail:
 up(&icomm_chipup_sem);
    return ret;
}
void exitWlan(void)
{
    if (g_wifidev_registered)
    {
        ssvdevice_exit();
  extern_wifi_set_enable(0);
        g_wifidev_registered = 0;
    }
    return;
}
static __init int generic_wifi_init_module(void)
{
 int ret;
 printk("%s\n", __func__);
 sema_init(&icomm_chipup_sem, 0);
#ifdef CONFIG_SSV_SUPPORT_AES_ASM
 sha1_mod_init();
 aes_init();
#endif
 ret = initWlan();
 if (down_timeout(&icomm_chipup_sem,
   msecs_to_jiffies(1000)) != 0) {
        ret = -EINVAL;
        printk(KERN_ALERT "%s: platform_driver_register timeout\n", __FUNCTION__);
  goto out;
 }
    ret = ssvdevice_init();
out:
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
