
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/rfkill-wlan.h>

extern int get_wifi_chip_type(void);

static ssize_t wifi_chip_read(struct class *cls, struct class_attribute *attr, char *_buf)
{
    int count = 0;
    int type = get_wifi_chip_type();

if(type == WIFI_RTKWIFI) {
#ifdef CONFIG_RTL8192CU
    count = sprintf(_buf, "%s", "RTL8188CU");
    printk("Current WiFi chip is RTL8188CU.\n");
#endif

#ifdef CONFIG_RTL8192DU
    count = sprintf(_buf, "%s", "RTL8192DU");
    printk("Current WiFi chip is RTL8192DU.\n");
#endif

#ifdef CONFIG_RTL8188EU
    count = sprintf(_buf, "%s", "RTL8188EU");
    printk("Current WiFi chip is RTL8188EU.\n");
#endif

#ifdef CONFIG_RTL8723AU
    count = sprintf(_buf, "%s", "RTL8723AU");
    printk("Current WiFi chip is RTL8723AU.\n");
#endif

#ifdef CONFIG_RTL8723BS
    count = sprintf(_buf, "%s", "RTL8723BS");
    printk("Current WiFi chip is RTL8723BS.\n");
#endif

#ifdef CONFIG_RTL8189ES
    count = sprintf(_buf, "%s", "RTL8189ES");
    printk("Current WiFi chip is RTL8189ES.\n");
#endif
}

if(type == WIFI_BCMWIFI) {
#ifdef CONFIG_BCM4330
    count = sprintf(_buf, "%s", "BCM4330");
    printk("Current WiFi chip is BCM4330.\n");
#endif

#ifdef CONFIG_RK901
    count = sprintf(_buf, "%s", "RK901");
    printk("Current WiFi chip is RK901.\n");
#endif

#ifdef CONFIG_RK903
    count = sprintf(_buf, "%s", "RK903");
    printk("Current WiFi chip is RK903.\n");
#endif

#ifdef CONFIG_AP6181
    count = sprintf(_buf, "%s", "RK901");
    printk("Current WiFi chip is AP6181.\n");
#endif

#ifdef CONFIG_AP6210
    count = sprintf(_buf, "%s", "RK901");
    printk("Current WiFi chip is AP6210.\n");
#endif

#ifdef CONFIG_AP6234
    count = sprintf(_buf, "%s", "AP6234");
    printk("Current WiFi chip is AP6234.\n");
#endif

#ifdef CONFIG_AP6330
    count = sprintf(_buf, "%s", "RK903");
    printk("Current WiFi chip is AP6330.\n");
#endif

#ifdef CONFIG_AP6335
    count = sprintf(_buf, "%s", "AP6335");
    printk("Current WiFi chip is AP6335.\n");
#endif

#ifdef CONFIG_AP6441
    count = sprintf(_buf, "%s", "AP6441");
    printk("Current WiFi chip is AP6441.\n");
#endif

#ifdef CONFIG_AP6476
    count = sprintf(_buf, "%s", "RK901");
    printk("Current WiFi chip is AP6476.\n");
#endif

#ifdef CONFIG_AP6493
    count = sprintf(_buf, "%s", "RK903");
    printk("Current WiFi chip is AP6493.\n");
#endif

#ifdef CONFIG_GB86302I
    count = sprintf(_buf, "%s", "RK903");
    printk("Current WiFi chip is GB86302I.\n");
#endif
}

#ifdef CONFIG_MTK_COMBO
	count = sprintf(_buf, "%s", "MT6620");
	printk("Current WiFi chip is MT6620.\n");
#endif

#ifdef CONFIG_MT5931
    count = sprintf(_buf, "%s", "MT5931");
    printk("Current WiFi chip is MT5931.\n");
#endif

#ifdef CONFIG_MT5931_MT6622
    count = sprintf(_buf, "%s", "MT5931");
    printk("Current WiFi chip is MT5931.\n");
#endif

#ifdef CONFIG_MTK_MT5931
    count = sprintf(_buf, "%s", "MT5931");
    printk("Current WiFi chip is MT5931.\n");
#endif

#ifdef CONFIG_MT7601
    count = sprintf(_buf, "%s", "MT7601");
    printk("Current WiFi chip is MT7601.\n");
#endif

if(type == WIFI_ESP8089) {
#ifdef CONFIG_ESP8089
    count = sprintf(_buf, "%s", "ESP8089");
    printk("Current WiFi chip is ESP8089.\n");
#endif
}

    return count;
}

static ssize_t wifi_power_write(struct class *cls, struct class_attribute *attr, const char *_buf, size_t _count)
{
    int poweren = 0;
    poweren = simple_strtol(_buf, NULL, 10);
    printk("%s: poweren = %d\n", __func__, poweren);
    if(poweren > 0) {
        rockchip_wifi_power(1);
    } else {
        rockchip_wifi_power(0);
    }

return _count;
}

#ifdef CONFIG_WIFI_NONE
int rockchip_wifi_init_module(void) {return 0;}
void rockchip_wifi_exit_module(void) {return;}
#else
extern int rockchip_wifi_init_module(void);
extern void rockchip_wifi_exit_module(void);
extern int rockchip_wifi_init_module_rkwifi(void);
extern void rockchip_wifi_exit_module_rkwifi(void);
extern int rockchip_wifi_init_module_rtkwifi(void);
extern void rockchip_wifi_exit_module_rtkwifi(void);
extern int rockchip_wifi_init_module_esp8089(void);
extern void rockchip_wifi_exit_module_esp8089(void);
#endif
static struct semaphore driver_sem;
static int wifi_driver_insmod = 0;

static int wifi_init_exit_module(int enable)
{
    int ret = 0;
    int type = get_wifi_chip_type();
#ifdef CONFIG_RKWIFI
    if (type == WIFI_BCMWIFI) {
        if (enable > 0)
            ret = rockchip_wifi_init_module_rkwifi();
        else
            rockchip_wifi_exit_module_rkwifi();
        return ret;
    }
#endif
#ifdef CONFIG_RTL_WIRELESS_SOLUTION
    if (type == WIFI_RTKWIFI) {
        if (enable > 0) 
            ret = rockchip_wifi_init_module_rtkwifi();
        else
            rockchip_wifi_exit_module_rtkwifi();
        return ret;
    }
#endif
#ifdef CONFIG_ESP8089
    if (type == WIFI_ESP8089) {
        if (enable > 0)  
            ret = rockchip_wifi_init_module_esp8089();
        else
            rockchip_wifi_exit_module_esp8089();
        return ret;
    }
#endif

#if !defined(CONFIG_RKWIFI) && !defined(CONFIG_RTL_WIRELESS_SOLUTION) && !defined(CONFIG_ESP8089)
    if (type >= 0) {
        if (enable > 0)
            ret = rockchip_wifi_init_module();
        else
            rockchip_wifi_exit_module();
    }
#endif

    return ret;
}

static ssize_t wifi_driver_write(struct class *cls, struct class_attribute *attr, const char *_buf, size_t _count)
{
    int enable = 0, ret = 0;
    
    down(&driver_sem);
    enable = simple_strtol(_buf, NULL, 10);
    //printk("%s: enable = %d\n", __func__, enable);
    if (wifi_driver_insmod == enable) {
        printk("%s: wifi driver already %s\n", __func__, enable? "insmod":"rmmod");
    	up(&driver_sem);
        return _count;
    }
    if(enable > 0) {
        ret = wifi_init_exit_module(enable);
        if (ret >= 0)
            wifi_driver_insmod = enable;
    } else {
        wifi_init_exit_module(enable);
        wifi_driver_insmod = enable;
    }   

    up(&driver_sem);
    //printk("%s: ret = %d\n", __func__, ret);
    return _count; 
}

static struct class *rkwifi_class = NULL;
static CLASS_ATTR(chip, 0664, wifi_chip_read, NULL);
static CLASS_ATTR(power, 0660, NULL, wifi_power_write);
static CLASS_ATTR(driver, 0660, NULL, wifi_driver_write);

int rkwifi_sysif_init(void)
{
    int ret;
    
    printk("Rockchip WiFi SYS interface (V1.00) ... \n");
    
    rkwifi_class = NULL;
    
    rkwifi_class = class_create(THIS_MODULE, "rkwifi");
    if (IS_ERR(rkwifi_class)) 
    {   
        printk("Create class rkwifi_class failed.\n");
        return -ENOMEM;
    }
    
    ret =  class_create_file(rkwifi_class, &class_attr_chip);
    ret =  class_create_file(rkwifi_class, &class_attr_power);
    ret =  class_create_file(rkwifi_class, &class_attr_driver);
    sema_init(&driver_sem, 1);
    
    return 0;
}

void rkwifi_sysif_exit(void)
{
    // need to remove the sys files and class
    class_remove_file(rkwifi_class, &class_attr_chip);
    class_remove_file(rkwifi_class, &class_attr_power);
    class_remove_file(rkwifi_class, &class_attr_driver);
    class_destroy(rkwifi_class);
    
    rkwifi_class = NULL;
}

module_init(rkwifi_sysif_init);
module_exit(rkwifi_sysif_exit);

MODULE_AUTHOR("Yongle Lai & gwl");
MODULE_DESCRIPTION("WiFi SYS @ Rockchip");
MODULE_LICENSE("GPL");

