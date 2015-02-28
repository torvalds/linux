
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

	if(type == WIFI_RK901) {
	    count = sprintf(_buf, "%s", "APRK901");
	    printk("Current WiFi chip is APRK901.\n");
	}

	if(type == WIFI_RK903) {
	    count = sprintf(_buf, "%s", "APRK903");
	    printk("Current WiFi chip is APRK903.\n");
	}
		
	if(type == WIFI_AP6181) {
	    count = sprintf(_buf, "%s", "AP6181");
	    printk("Current WiFi chip is AP6181.\n");
	}

	if(type == WIFI_AP6210) {
	    count = sprintf(_buf, "%s", "AP6210");
	    printk("Current WiFi chip is AP6210.\n");
	}
	
	if(type == WIFI_AP6234) {
	    count = sprintf(_buf, "%s", "AP6234");
	    printk("Current WiFi chip is AP6234.\n");
	}
	
	if(type == WIFI_AP6330) {
	    count = sprintf(_buf, "%s", "AP6330");
	    printk("Current WiFi chip is AP6330.\n");
	}
	
	if(type == WIFI_AP6335) {
	    count = sprintf(_buf, "%s", "AP6335");
	    printk("Current WiFi chip is AP6335.\n");
	}

	if(type == WIFI_AP6441) {
	    count = sprintf(_buf, "%s", "AP6441");
	    printk("Current WiFi chip is AP6441.\n");
	}

	if(type == WIFI_AP6476) {
	    count = sprintf(_buf, "%s", "AP6476");
	    printk("Current WiFi chip is AP6476.\n");
	}

	if(type == WIFI_AP6493) {
	    count = sprintf(_buf, "%s", "AP6493");
	    printk("Current WiFi chip is AP6493.\n");
	}

	if(type == WIFI_RTL8188EU) {
	    count = sprintf(_buf, "%s", "RTL8188EU");
	    printk("Current WiFi chip is RTL8188EU.\n");
	}
	
	if(type == WIFI_RTL8723BS) {
	    count = sprintf(_buf, "%s", "RTL8723BS");
	    printk("Current WiFi chip is RTL8723BS.\n");
	}

	if(type == WIFI_RTL8723AS) {
	    count = sprintf(_buf, "%s", "RTL8723AS");
	    printk("Current WiFi chip is RTL8723AS.\n");
	}	
	
	if(type == WIFI_RTL8723BS_VQ0) {
	    count = sprintf(_buf, "%s", "RTL8723BS_VQ0");
	    printk("Current WiFi chip is RTL8723BS_VQ0.\n");
	}		
	
	if(type == WIFI_RTL8723BU) {
	    count = sprintf(_buf, "%s", "RTL8723BU");
	    printk("Current WiFi chip is RTL8723BU.\n");
	}

	if(type == WIFI_RTL8723AU) {
	    count = sprintf(_buf, "%s", "RTL8723AU");
	    printk("Current WiFi chip is RTL8723AU.\n");
	}							

	if(type == WIFI_RTL8189ES) {
	    count = sprintf(_buf, "%s", "RTL8189ES");
	    printk("Current WiFi chip is RTL8189ES.\n");
	}	

	if(type == WIFI_ESP8089) {
	    count = sprintf(_buf, "%s", "ESP8089");
	    printk("Current WiFi chip is ESP8089.\n");
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
//#ifdef CONFIG_RKWIFI
    if (type < WIFI_AP6XXX_SERIES) {
        if (enable > 0)
            ret = rockchip_wifi_init_module_rkwifi();
        else
            rockchip_wifi_exit_module_rkwifi();
        return ret;
    }
//#endif
//#ifdef CONFIG_RTL_WIRELESS_SOLUTION
    if (type < WIFI_RTL_SERIES) {
        if (enable > 0) 
            ret = rockchip_wifi_init_module_rtkwifi();
        else
            rockchip_wifi_exit_module_rtkwifi();
        return ret;
    }
//#endif
//#ifdef CONFIG_ESP8089
    if (type == WIFI_ESP8089) {
        if (enable > 0)  
            ret = rockchip_wifi_init_module_esp8089();
        else
            rockchip_wifi_exit_module_esp8089();
        return ret;
    }
//#endif

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

