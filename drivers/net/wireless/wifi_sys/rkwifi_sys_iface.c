
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <asm/uaccess.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static ssize_t wifi_chip_read(struct class *cls, struct class_attribute *attr, char *_buf)
#else
static ssize_t wifi_chip_read(struct class *cls, char *_buf)
#endif
{
    int count = 0;

#ifdef CONFIG_BCM4329
    count = sprintf(_buf, "%s", "BCM4329");
    printk("Current WiFi chip is BCM4329.\n");
#endif

#ifdef CONFIG_RTL8192CU
    count = sprintf(_buf, "%s", "RTL8188CU");
    printk("Current WiFi chip is RTL8188CU.\n");
#endif

#ifdef CONFIG_RTL8188EU
    count = sprintf(_buf, "%s", "RTL8188EU");
    printk("Current WiFi chip is RTL8188EU.\n");
#endif

#ifdef CONFIG_RTL8723AU
    count = sprintf(_buf, "%s", "RTL8723AU");
    printk("Current WiFi chip is RTL8723AU.\n");
#endif

#ifdef CONFIG_RTL8189ES
    count = sprintf(_buf, "%s", "RTL8189ES");
    printk("Current WiFi chip is RTL8189ES.\n");
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

#ifdef CONFIG_AP6181
    count = sprintf(_buf, "%s", "RK901");
    printk("Current WiFi chip is AP6181.\n");
#endif

#ifdef CONFIG_AP6210
    count = sprintf(_buf, "%s", "RK901");
    printk("Current WiFi chip is AP6210.\n");
#endif

#ifdef CONFIG_AP6330
    count = sprintf(_buf, "%s", "RK903");
    printk("Current WiFi chip is AP6330.\n");
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

#ifdef CONFIG_MTK_COMBO
	count = sprintf(_buf, "%s", "MT6620");
	printk("Current WiFi chip is MT6620.\n");
#endif

#ifdef CONFIG_RT5370
    count = sprintf(_buf, "%s", "RT5370");
    printk("Current WiFi chip is RT5370.\n");
#endif

#ifdef CONFIG_MT5931
    count = sprintf(_buf, "%s", "MT5931");
    printk("Current WiFi chip is MT5931.\n");
#endif

#ifdef CONFIG_MT5931_MT6622
    count = sprintf(_buf, "%s", "MT5931");
    printk("Current WiFi chip is MT5931.\n");
#endif

#ifdef CONFIG_MT7601
    count = sprintf(_buf, "%s", "MT7601");
    printk("Current WiFi chip is MT7601.\n");
#endif


#ifdef CONFIG_RTL8723AS
    count = sprintf(_buf, "%s", "RTL8723AS");
    printk("Current WiFi chip is RTL8723AS.\n");
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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static ssize_t wifi_p2p_read(struct class *cls, struct class_attribute *attr, char *_buf)
#else
static ssize_t wifi_p2p_read(struct class *cls, char *_buf)
#endif
{
	int count = 0;

#ifdef CONFIG_BCM4329
    count = sprintf(_buf, "%s", "false");
	printk("Current WiFi chip BCM4329 doesn't support direct.(%s)\n", _buf);
#endif

#ifdef CONFIG_RTL8192CU
    count = sprintf(_buf, "%s", "false");
	printk("Current WiFi chip RTL8188 support direct.(%s)\n", _buf);
#endif

#ifdef CONFIG_RK903
    count = sprintf(_buf, "%s", "true");
	    printk("Current WiFi chip RK903 support direct.(%s)\n", _buf);
#endif

#ifdef CONFIG_BCM4330
    count = sprintf(_buf, "%s", "true");
	printk("Current WiFi chip BCM4330 support direct.(%s)\n", _buf);
#endif

	return count;
}

int wifi_pcba_test = 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static ssize_t wifi_pcba_read(struct class *cls, struct class_attribute *attr, char *_buf)
#else
static ssize_t wifi_pcba_read(struct class *cls, char *_buf)
#endif
{
        int count = 0;

        count = sprintf(_buf, "%d", wifi_pcba_test);
        return count;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static ssize_t wifi_pcba_write(struct class *cls, struct class_attribute *attr, char *_buf, size_t _count)
#else
static ssize_t wifi_pcba_write(struct class *cls, char *_buf, size_t _count)
#endif 
{
        wifi_pcba_test = simple_strtol(_buf, NULL, 10);
        if(wifi_pcba_test > 0) {
            wifi_pcba_test = 1;
        }
        return _count;
}

#ifdef CONFIG_AIDC
int check_wifi_type_from_id(int id, char * _buf) {
	int count = 0;

	switch(id) {
		case 0x8179:
			count = sprintf(_buf, "%s", "RTL8188EU");
    		printk("Current WiFi chip is RTL8188EU.\n");
			break;
		case 0x0179:
			count = sprintf(_buf, "%s", "RTL8188EU");
    		printk("Current WiFi chip is RTL8188ETV.\n");
			break;
		case 0x5370:
			count = sprintf(_buf, "%s", "RT5370");
    		printk("Current WiFi chip is RT5370.\n");
			break;
		case 0x0724:
			count = sprintf(_buf, "%s", "RTL8723AU");
    		printk("Current WiFi chip is RTL8723AU.\n");
			break;
		case 0x8176:
			count = sprintf(_buf, "%s", "RTL8188CU");
    		printk("Current WiFi chip is RTL8188CU.\n");
			break;
		case 0x018A:
			count = sprintf(_buf, "%s", "RTL8188CU");
    		printk("Current WiFi chip is RTL8188CTV.\n");
			break;
		default:
    		printk("Unsupported usb wifi.............\n");
	}	
	return count;	
}

extern int rk29sdk_wifi_power(int on);
extern int wifi_activate_usb(void);
extern int wifi_deactivate_usb(void);
#define USB_IDP_SYS_PATCH_1 "/sys/bus/usb/devices/1-1/idProduct"
#define USB_IDP_SYS_PATCH_2 "/sys/bus/usb/devices/2-1/idProduct"
#define USB_IDV_SYS_PATCH_1 "/sys/bus/usb/devices/1-1/idVendor"
#define USB_IDV_SYS_PATCH_2 "/sys/bus/usb/devices/2-1/idVendor"
#define USB_PRODUCT_SYS_PATCH "/sys/bus/usb/devices/1-1/product"
//5370 802.11 n WLAN
//8723 802.11n WLAN Adapter
//8188eu 802.11n NIC
//8188cu 802.11n WLAN Adapter
char aidc_type[20] = {0};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
static ssize_t wifi_aidc_read(struct class *cls, struct class_attribute *attr, char *_buf)
#else
static ssize_t wifi_aidc_read(struct class *cls, char *_buf)
#endif
{
        int count = 0, retry = 10, idP = 0;// idV = 0;
		ssize_t nread;
		loff_t pos = 0;
		char usbid[20] = {0};
		struct file *file = NULL;
		mm_segment_t old_fs;
		 
		sprintf(_buf, "%s", "UNKNOW");
		wifi_activate_usb();
		msleep(2000);
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		while(retry--) {
			file = filp_open(USB_IDP_SYS_PATCH_2, O_RDONLY, 0);
			if (IS_ERR(file)) {
				printk("\nCannot open \"%s\", retry = %d\n", USB_IDP_SYS_PATCH_2, retry);
				file = filp_open(USB_IDP_SYS_PATCH_1, O_RDONLY, 0);
				if (IS_ERR(file)) {
		       		printk("\nCannot open \"%s\", retry = %d\n", USB_IDP_SYS_PATCH_1, retry);
					msleep(500);
					continue;
				}
			}
			break;
    	}
		if(retry <= 0) {
			set_fs(old_fs);
			return count;
		}
		nread = vfs_read(file, (char __user *)usbid, sizeof(usbid), &pos);
		set_fs(old_fs);
		filp_close(file, NULL);
		wifi_deactivate_usb();
		idP = simple_strtol(usbid, NULL, 16);
		printk("Get usb wifi idProduct = 0X%04X\n", idP);
		count = check_wifi_type_from_id(idP, _buf);

        return count;
}
#endif //CONFIG_AIDC

static struct class *rkwifi_class = NULL;
static CLASS_ATTR(chip, 0664, wifi_chip_read, NULL);
static CLASS_ATTR(p2p, 0664, wifi_p2p_read, NULL);
static CLASS_ATTR(pcba, 0664, wifi_pcba_read, wifi_pcba_write);
#ifdef CONFIG_AIDC
static CLASS_ATTR(aidc, 0664, wifi_aidc_read, NULL);
#endif

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
    ret =  class_create_file(rkwifi_class, &class_attr_p2p);
    ret =  class_create_file(rkwifi_class, &class_attr_pcba);
#ifdef CONFIG_AIDC
    ret =  class_create_file(rkwifi_class, &class_attr_aidc);
#endif
    
    return 0;
}

void rkwifi_sysif_exit(void)
{
    // need to remove the sys files and class
    class_remove_file(rkwifi_class, &class_attr_chip);
    class_remove_file(rkwifi_class, &class_attr_p2p);
    class_remove_file(rkwifi_class, &class_attr_pcba);
#ifdef CONFIG_AIDC
    class_remove_file(rkwifi_class, &class_attr_aidc);
#endif
    class_destroy(rkwifi_class);
    
    rkwifi_class = NULL;
}

module_init(rkwifi_sysif_init);
module_exit(rkwifi_sysif_exit);

MODULE_AUTHOR("Yongle Lai");
MODULE_DESCRIPTION("WiFi SYS @ Rockchip");
MODULE_LICENSE("GPL");

