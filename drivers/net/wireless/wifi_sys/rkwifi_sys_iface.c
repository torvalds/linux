
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static ssize_t wifi_chip_read(struct class *cls, struct class_attribute *attr, char *_buf)
#else
static ssize_t wifi_chip_read(struct class *cls, char *_buf)
#endif
{
    int count;

#ifdef CONFIG_BCM4329
    count = sprintf(_buf, "%s", "BCM4329");
    printk("Current WiFi chip is BCM4329.\n");
#endif

#ifdef CONFIG_RTL8192CU
    count = sprintf(_buf, "%s", "RTL8188");
    printk("Current WiFi chip is RTL8188.\n");
#endif

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
    
    return count;
}

/*
static ssize_t wifi_channel_write(struct class *cls, const char *_buf, size_t _count)
{
    int ret, channel;
    
    if (wifi_enabled == 0)
    {
        printk("WiFi is disabled.\n");
        return _count;
    }
    
    channel = simple_strtol(_buf, NULL, 10);
    
    ret = wifi_emi_set_channel(channel);
    if (ret != 0)
    {
        //printk("Set channel=%d fail.\n", channel);
    }
    else
    {
        //printk("Set channel=%d successfully.\n", channel);
        wifi_channel = channel;
    }
    
    return _count;
}
*/

static struct class *rkwifi_class = NULL;
static CLASS_ATTR(chip, 0664, wifi_chip_read, NULL);

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
    
    return 0;
}

void rkwifi_sysif_exit(void)
{
    // need to remove the sys files and class
    class_remove_file(rkwifi_class, &class_attr_chip);
    class_destroy(rkwifi_class);
    
    rkwifi_class = NULL;
}

module_init(rkwifi_sysif_init);
module_exit(rkwifi_sysif_exit);

MODULE_AUTHOR("Yongle Lai");
MODULE_DESCRIPTION("WiFi SYS @ Rockchip");
MODULE_LICENSE("GPL");

