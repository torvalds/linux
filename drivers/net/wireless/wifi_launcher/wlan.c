/*
 * Just a wifi driver hooker.
 *
 * Yongle Lai @ 2009-05-10 @ Rockchip
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("Dual BSD/GPL");

#ifdef CONFIG_MODVERSIONS
#define MODVERSIONS
#include <linux/modversions.h>
#endif 

//#define OLD_WIFI_IFACE

#ifdef OLD_WIFI_IFACE
extern int mv88w8686_if_sdio_init_module(void);
extern void mv88w8686_if_sdio_exit_module(void);
#else
extern int rockchip_wifi_init_module(void);
extern void rockchip_wifi_exit_module(void);
#endif

static int wifi_launcher_init(void) 
{
  int ret;

  printk("=======================================================\n");
  printk("==== Launching Wi-Fi driver! (Powered by Rockchip) ====\n");
  printk("=======================================================\n");

#ifdef OLD_WIFI_IFACE
  ret = mv88w8686_if_sdio_init_module();
  if (ret) /* Try again */
  	ret = mv88w8686_if_sdio_init_module();
#else
  ret = rockchip_wifi_init_module();
  //if (ret) /* Try again */
  //	ret = rockchip_wifi_init_module();
#endif

  return ret;
}

static void wifi_launcher_exit(void) 
{
  printk("=======================================================\n");
  printk("== Dis-launching Wi-Fi driver! (Powered by Rockchip) ==\n");
  printk("=======================================================\n");

#ifdef OLD_WIFI_IFACE
  mv88w8686_if_sdio_exit_module();
#else
  rockchip_wifi_exit_module();
#endif
}

module_init(wifi_launcher_init);
module_exit(wifi_launcher_exit);

