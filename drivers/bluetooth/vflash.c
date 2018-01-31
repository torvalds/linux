/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2010 ROCKCHIP, Inc.
 * Author: roger_chen <cz@rock-chips.com>
 *
 * This program is the virtual flash device 
 * used to store bd_addr or MAC
 *
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#if 0
#define DBG(x...)   printk("vFlash:" x)
#else
#define DBG(x...)
#endif

#define VERSION "0.1"

static int minor = MISC_DYNAMIC_MINOR;

static struct miscdevice vflash_miscdev;

#define READ_BDADDR_FROM_FLASH  0x01

extern char GetSNSectorInfo(char * pbuf);
extern unsigned char wlan_mac_addr[6];

static long vflash_ioctl(struct file *file,
					unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
    
    DBG("%s---cmd=0x%x---arg=0x%x\n", __FUNCTION__, cmd, arg);

    if(NULL == argp)
        return -EFAULT;
        
    switch(cmd)
    {
        case READ_BDADDR_FROM_FLASH:
        {   
#ifdef CONFIG_WIFI_MAC
            unsigned char bd_addr[6] = {0};
            int i;

            printk("vflash: wlan_mac_addr:%X:%X:%X:%x:%X:%x\n", wlan_mac_addr[0],
                                                wlan_mac_addr[1],
                                                wlan_mac_addr[2],
                                                wlan_mac_addr[3],
                                                wlan_mac_addr[4],
                                                wlan_mac_addr[5] );
            for (i=1; i<6; i++) {
                bd_addr[i] = wlan_mac_addr[5-i];
            }

            bd_addr[0] = wlan_mac_addr[5]+1;

            printk("vflash: bd_addr:%X:%X:%X:%x:%X:%x\n", bd_addr[5],
                                                bd_addr[4],
                                                bd_addr[3],
                                                bd_addr[2],
                                                bd_addr[1],
                                                bd_addr[0] );


            if(copy_to_user(argp, bd_addr, 6)) {
                printk("ERROR: copy_to_user---%s\n", __FUNCTION__);
                return -EFAULT;
            }
#else
            char *tempBuf = (char *)kmalloc(512, GFP_KERNEL);
	    char bd_addr[7] = {0};
            int i;

            GetSNSectorInfo(tempBuf);

            for(i=498; i<=504; i++)
            {
                DBG("tempBuf[%d]=%x\n", i, tempBuf[i]);
		bd_addr[504-i] = tempBuf[i];
            }

            
            //printk("%s: ====> get bt addr from flash=[%02x:%02x:%02x:%02x:%02x:%02x]\n", __FUNCTION__,
            //      bd_addr[5], bd_addr[4], bd_addr[3], bd_addr[2], bd_addr[1], bd_addr[0]);
	    if(copy_to_user(argp, bd_addr, 6))
            {
                printk("ERROR: copy_to_user---%s\n", __FUNCTION__);
                kfree(tempBuf);
                return -EFAULT;
            }
            
            kfree(tempBuf);
#endif
        }
        break;
        default:
        break;
    }
    
	return 0;
}

static int vflash_open(struct inode *inode, struct file *file)
{
    DBG("%s\n", __FUNCTION__);
	return 0;
}

static int vflash_release(struct inode *inode, struct file *file)
{
    DBG("%s\n", __FUNCTION__);
	return 0;
}


static const struct file_operations vflash_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= vflash_ioctl,
	.open		= vflash_open,
	.release	= vflash_release,
};

static struct miscdevice vflash_miscdev= {
	.name		= "vflash",
	.fops		= &vflash_fops,
};


static int vflash_init(void)
{
	vflash_miscdev.minor = minor;

	if (misc_register(&vflash_miscdev) < 0) {
		printk(KERN_ERR"Can't register misc device with minor %d", minor);
		return -EIO;
	}
	return 0;
}

static void vflash_exit(void)
{
	if (misc_deregister(&vflash_miscdev) < 0)
		printk(KERN_ERR"Can't unregister misc device with minor %d", minor);
}


module_init(vflash_init);
module_exit(vflash_exit);

module_param(minor, int, 0444);
MODULE_PARM_DESC(minor, "Miscellaneous minor device number");

MODULE_AUTHOR("roger_chen <cz@rock-chips.com>");
MODULE_DESCRIPTION("Bluetooth virtual flash driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");

