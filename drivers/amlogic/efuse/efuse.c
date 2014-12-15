/*
 * E-FUSE char device driver.
 *
 * Author: Bo Yang <bo.yang@amlogic.com>
 *
 * Copyright (c) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the smems of the GNU General Public License as published by
  * the Free Software Foundation; version 2 of the License.
  *
  */

#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <linux/platform_device.h>
#include <mach/am_regs.h>
#include <plat/io.h>

#include <linux/amlogic/efuse.h>
#include "efuse_regs.h"
#include <linux/of.h>
#include <linux/module.h>

#define EFUSE_MODULE_NAME   "efuse"
#define EFUSE_DRIVER_NAME		"efuse"
#define EFUSE_DEVICE_NAME   "efuse"
#define EFUSE_CLASS_NAME    "efuse"

#define EFUSE_READ_ONLY

int efuse_getinfo_byID(unsigned id, efuseinfo_item_t *info);
int check_if_efused(loff_t pos, size_t count);
int efuse_read_item(char *buf, size_t count, loff_t *ppos);
int efuse_write_item(char *buf, size_t count, loff_t *ppos);

void efuse_dump(char *pbuffer);

/* M3 efuse layout: version1
http://wiki-sh.amlogic.com/index.php/How_To_burn_the_info_into_E-Fuse
title				offset			datasize			checksize			totalsize
reserved 		0					0						0						4
usid				4					33					2						35
mac_wifi		39				6						1						7
mac_bt		46				6						1						7
mac				53				6						1						7
licence			60				3						1						4
reserved 		64				0						0						4
hdcp			68				300					10					310
reserved		378				0						0						2
version		380				3						1						4    (version+machid, version=1)
*/

static unsigned long efuse_status;
#define EFUSE_IS_OPEN           (0x01)

#ifdef EFUSE_DEBUG
void __efuse_debug_init(void);
#endif

/*
typedef struct efuse_dev_s {
	struct cdev         cdev;
	unsigned int        flags;
} efuse_dev_t;
*/
static efuse_dev_t *efuse_devp;
//static struct class *efuse_clsp;
static dev_t efuse_devno;

static int efuse_open(struct inode *inode, struct file *file)
{
	int ret = 0;
	efuse_dev_t *devp;

	devp = container_of(inode->i_cdev, efuse_dev_t, cdev);
	file->private_data = devp;

	return ret;
}

static int efuse_release(struct inode *inode, struct file *file)
{
	int ret = 0;
	efuse_dev_t *devp;

	devp = file->private_data;
	efuse_status &= ~EFUSE_IS_OPEN;
	return ret;
}

loff_t efuse_llseek(struct file *filp, loff_t off, int whence)
{
	loff_t newpos;

	switch(whence) {
		case 0: /* SEEK_SET */
			newpos = off;
			break;

		case 1: /* SEEK_CUR */
			newpos = filp->f_pos + off;
			break;

		case 2: /* SEEK_END */
			newpos = EFUSE_BYTES + off;
			break;

		default: /* can't happen */
			return -EINVAL;
	}

	if (newpos < 0)
		return -EINVAL;
	filp->f_pos = newpos;
		return newpos;
}

static long efuse_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg )
{
	switch (cmd)
	{
#ifndef CONFIG_MESON_TRUSTZONE			
		case EFUSE_ENCRYPT_ENABLE:
			aml_set_reg32_bits( P_EFUSE_CNTL4, CNTL4_ENCRYPT_ENABLE_ON,
			CNTL4_ENCRYPT_ENABLE_BIT, CNTL4_ENCRYPT_ENABLE_SIZE);
			break;

		case EFUSE_ENCRYPT_DISABLE:
			aml_set_reg32_bits( P_EFUSE_CNTL4, CNTL4_ENCRYPT_ENABLE_OFF,
			CNTL4_ENCRYPT_ENABLE_BIT, CNTL4_ENCRYPT_ENABLE_SIZE);
			break;

		case EFUSE_ENCRYPT_RESET:
			aml_set_reg32_bits( P_EFUSE_CNTL4, CNTL4_ENCRYPT_RESET_ON,
			CNTL4_ENCRYPT_RESET_BIT, CNTL4_ENCRYPT_RESET_SIZE);
			break;
#endif
		case EFUSE_INFO_GET:
			{
				efuseinfo_item_t *info = (efuseinfo_item_t*)arg;
				if(efuse_getinfo_byID(info->id, info) < 0)
					return  -EFAULT;
			}
			break;

		default:
			return -ENOTTY;
	}
	return 0;
}


static ssize_t efuse_read( struct file *file, char __user *buf, size_t count, loff_t *ppos )
{
	int ret;
	int local_count = 0;
	unsigned char* local_buf = (unsigned char*)kzalloc(sizeof(char)*count, GFP_KERNEL);
	if (!local_buf) {
		printk(KERN_INFO "memory not enough\n");
		return -ENOMEM;
	}

	local_count = efuse_read_item(local_buf, count, ppos);
	if (local_count < 0) {
		ret =  -EFAULT;
		goto error_exit;
	}

	if (copy_to_user((void*)buf, (void*)local_buf, local_count)) {
		ret =  -EFAULT;
		goto error_exit;
	}
	ret = local_count;

error_exit:
	if (local_buf)
		kfree(local_buf);
	return ret;
}

static ssize_t efuse_write( struct file *file, const char __user *buf, size_t count, loff_t *ppos )
{
	unsigned int  pos = (unsigned int)*ppos;
	int ret;
	unsigned char* contents = NULL;

	if (pos >= EFUSE_BYTES)
        return 0;       /* Past EOF */
	if (count > EFUSE_BYTES - pos)
		count = EFUSE_BYTES - pos;
	if (count > EFUSE_BYTES)
		return -EFAULT;

	if ((ret = check_if_efused(pos, count))) {
		printk(KERN_INFO "check if has been efused failed\n");
		if (ret == 1)
			return -EROFS;
		else if (ret < 0)
			return ret;
	}

	contents = (unsigned char*)kzalloc(sizeof(unsigned char)*EFUSE_BYTES, GFP_KERNEL);
	if (!contents) {
		printk(KERN_INFO "memory not enough\n");
		return -ENOMEM;
	}
	memset(contents, 0, sizeof(contents));
	if (copy_from_user(contents, buf, count)){
		if(contents)
			kfree(contents);
		return -EFAULT;
	}

	if(efuse_write_item(contents, count, ppos) < 0){
		if(contents)
			kfree(contents);
		return -EFAULT;
	}

	if (contents)
		kfree(contents);
	return count;
}


static const struct file_operations efuse_fops = {
	.owner      = THIS_MODULE,
	.llseek     = efuse_llseek,
	.open       = efuse_open,
	.release    = efuse_release,
	.read       = efuse_read,
	.write      = efuse_write,
	.unlocked_ioctl      = efuse_unlocked_ioctl,
};

/* Sysfs Files */
static ssize_t mac_show(struct class *cla, struct class_attribute *attr, char *buf)
{
	char dec_mac[6] = {0};
	efuseinfo_item_t info;
	if(efuse_getinfo_byID(EFUSE_MAC_ID, &info) < 0){
		printk(KERN_INFO"ID is not found\n");
		return -EFAULT;
	}

	if (efuse_read_item(dec_mac, info.data_len, (loff_t*)&info.offset) < 0)
		return -EFAULT;

	return sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x\n",
			dec_mac[0],dec_mac[1],dec_mac[2],dec_mac[3],dec_mac[4],dec_mac[5]);
}

static ssize_t mac_wifi_show(struct class *cla, struct class_attribute *attr, char *buf)
{
	char dec_mac[6] = {0};
	efuseinfo_item_t info;

	if(efuse_getinfo_byID(EFUSE_MAC_WIFI_ID, &info) < 0){
		printk(KERN_INFO"ID is not found\n");
		return -EFAULT;
	}

	if (efuse_read_item(dec_mac, info.data_len, (loff_t*)&info.offset) < 0)
		return -EFAULT;

	return sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x\n",
			dec_mac[0],dec_mac[1],dec_mac[2],dec_mac[3],dec_mac[4],dec_mac[5]);
}


static ssize_t mac_bt_show(struct class *cla, struct class_attribute *attr, char *buf)
{
	char dec_mac[6] = {0};
	efuseinfo_item_t info;

	if(efuse_getinfo_byID(EFUSE_MAC_BT_ID, &info) < 0){
		printk(KERN_INFO"ID is not found\n");
		return -EFAULT;
	}
	if (efuse_read_item(dec_mac, info.data_len, (loff_t*)&info.offset) < 0)
		return -EFAULT;

	return sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x\n",
			dec_mac[0],dec_mac[1],dec_mac[2],dec_mac[3],dec_mac[4],dec_mac[5]);
}

static int efuse_device_match(struct device *dev, const void *data)
{
	return (!strcmp(dev->kobj.name,(const char*)data));
}

struct device *efuse_class_to_device(struct class *cla)
{
	struct device		*dev;

	dev = class_find_device(cla, NULL, (void*)cla->name,
				efuse_device_match);
	if (!dev)
		printk("%s no matched device found!/n",__FUNCTION__);
	return dev;
}

/*int verify(unsigned char *usid)
{
	int len;

    len = strlen(usid);
    if((len > 8)&&(len<31) )
        return 0;
	else
		return -1;
}*/

static ssize_t userdata_show(struct class *cla, struct class_attribute *attr, char *buf)
{
	char *op;
	bool ret = true;
	int i;
	efuseinfo_item_t info;
	char tmp[5];
	struct efuse_platform_data *data = NULL;
	struct device	*dev = efuse_class_to_device(cla);
	data = dev->platform_data;
	if(!data){
		printk( KERN_ERR "%s error!no platform_data!\n",__FUNCTION__);
		return -1;
	}

	if(efuse_getinfo_byID(EFUSE_USID_ID, &info) < 0){
		printk(KERN_INFO"ID is not found\n");
		return -1;
	}

	op = (char*)kmalloc(sizeof(char)*info.data_len, GFP_KERNEL);
	 if ( !op ) {
		 printk(KERN_ERR "efuse: failed to allocate memory\n");
		 ret = -ENOMEM;
	}

	memset(op, 0, sizeof(op));
	if (efuse_read_item(op, info.data_len, (loff_t*)&info.offset) < 0){
		if(op)
			kfree(op);
		return -1;
	}

	//if(data->data_verify)
	//	ret = data->data_verify(op);
	//ret = verify(op);

	//if(!ret){
	//	printk("%s error!data_verify failed!\n",__FUNCTION__);
	//	return -1;
	//}
	/*return sprintf(buf, "%01c%01c%01c%01c%01c%01c%01c%01c%01c%01c%01c%01c%01c%01c%01c%01c%01c%01c%01c%01c\n",
    			   op[0],op[1],op[2],op[3],op[4],op[5],
    			   op[6],op[7],op[8],op[9],op[10],op[11],
    			   op[12],op[13],op[14],op[15],op[16],op[17],
    			   op[18],op[19]);*/

	for(i = 0; i < info.data_len; i++) {
	    memset(tmp, 0, 5);
	    sprintf(tmp, "%02x:", op[i]);
	    strcat(buf, tmp);
	}
	buf[3*info.data_len - 1] = 0; //delete the last ':'
	return 3*info.data_len - 1;
}

#ifndef EFUSE_READ_ONLY
static ssize_t userdata_write(struct class *cla, struct class_attribute *attr, char *buf,size_t count)
{
	struct efuse_platform_data *data = NULL;
	struct device	*dev = NULL;
	bool ret = true;
	char *op = NULL;
	dev = efuse_class_to_device(cla);
	data = dev->platform_data;
	if(!data){
		printk( KERN_ERR "%s error!no platform_data!\n",__FUNCTION__);
		return -1;
	}
	if(data->data_verify)
		ret = data->data_verify(buf);
	if(!ret){
		printk("%s error!data_verify failed!\n",__FUNCTION__);
		return -1;
	}

	efuseinfo_item_t info;
	int i;
	unsigned local_count = count;
	if(local_count > data->count)
		local_count = data->count;
	if(efuse_getinfo_byID(EFUSE_USID_ID, &info) < 0){
		printk(KERN_INFO, "ID is not found\n");
		return -1;
	}
	op = (char*)kmalloc(sizeof(char)*info.data_len, GFP_KERNEL);
	 if ( !op ) {
		 printk(KERN_ERR "efuse: failed to allocate memory\n");
		 ret = -ENOMEM;
	}
	memset(op, 0, sizeof(op));
	for(i=0; i<local_count; i++)
		op[i] = buf[i];

	if(efuse_write_item(op, info.data_len, (loff_t*)&info.offset) < 0)
		return -1;

	if(op)
		kfree(op);
	return local_count;
}
#endif

static struct class_attribute efuse_class_attrs[] = {

	__ATTR_RO(mac),

	__ATTR_RO(mac_wifi),

	__ATTR_RO(mac_bt),

	#ifndef EFUSE_READ_ONLY		/*make the efuse can not be write through sysfs */
	__ATTR(userdata, S_IRWXU, userdata_show, userdata_write),

	#else
	__ATTR_RO(userdata),

	#endif
	__ATTR_NULL

};

static struct class efuse_class = {

	.name = EFUSE_CLASS_NAME,

	.class_attrs = efuse_class_attrs,

};


static int efuse_probe(struct platform_device *pdev)
{
	 int ret;
	 struct device *devp;
#ifdef CONFIG_OF
struct efuse_platform_data aml_efuse_plat;
struct device_node *np = pdev->dev.of_node;
int usid_min,usid_max;
#endif
	 printk( KERN_INFO "efuse===========================================\n");
	 ret = alloc_chrdev_region(&efuse_devno, 0, 1, EFUSE_DEVICE_NAME);
	 if (ret < 0) {
			 printk(KERN_ERR "efuse: failed to allocate major number\n");
	 ret = -ENODEV;
	 goto out;
	 }

//	   efuse_clsp = class_create(THIS_MODULE, EFUSE_CLASS_NAME);
//	   if (IS_ERR(efuse_clsp)) {
//		   ret = PTR_ERR(efuse_clsp);
//		   goto error1;
//	   }
	 ret = class_register(&efuse_class);
	 if (ret)
		 goto error1;

	 efuse_devp = kmalloc(sizeof(efuse_dev_t), GFP_KERNEL);
	 if ( !efuse_devp ) {
		 printk(KERN_ERR "efuse: failed to allocate memory\n");
		 ret = -ENOMEM;
		 goto error2;
	 }

	 /* connect the file operations with cdev */
	 cdev_init(&efuse_devp->cdev, &efuse_fops);
	 efuse_devp->cdev.owner = THIS_MODULE;
	 /* connect the major/minor number to the cdev */
	 ret = cdev_add(&efuse_devp->cdev, efuse_devno, 1);
	 if (ret) {
		 printk(KERN_ERR "efuse: failed to add device\n");
		 goto error3;
	 }

	 //devp = device_create(efuse_clsp, NULL, efuse_devno, NULL, "efuse");
	 devp = device_create(&efuse_class, NULL, efuse_devno, NULL, "efuse");
	 if (IS_ERR(devp)) {
		 printk(KERN_ERR "efuse: failed to create device node\n");
		 ret = PTR_ERR(devp);
		 goto error4;
	 }
	 printk(KERN_INFO "efuse: device %s created\n", EFUSE_DEVICE_NAME);
#ifdef CONFIG_OF
	if(pdev->dev.of_node){
		of_node_get(np);
		ret = of_property_read_u64(np,"plat-pos",&aml_efuse_plat.pos);
		if(ret){
			printk("%s:%d,please config plat-pos item\n",__func__,__LINE__);
			return -1;
		}
		ret = of_property_read_u32(np,"plat-count",&aml_efuse_plat.count);
		if(ret){
			printk("%s:%d,please config plat-count item\n",__func__,__LINE__);
			return -1;
		}
		ret = of_property_read_u32(np,"usid-min",&usid_min);
		if(ret){
			printk("%s:%d,please config usid-min item\n",__func__,__LINE__);
			return -1;
		}
		ret = of_property_read_u32(np,"usid-max",&usid_max);
		if(ret){
			printk("%s:%d,please config usid-max item\n",__func__,__LINE__);
			return -1;
		}
		//todo reserved for user id <usid-min ~ usid max>
	}
		devp->platform_data = &aml_efuse_plat;
#else


	 if(pdev->dev.platform_data)
		 devp->platform_data = pdev->dev.platform_data;
	 else
	 	devp->platform_data = NULL;
#endif
#ifndef CONFIG_MESON_TRUSTZONE	 	
	 /* disable efuse encryption */
	 aml_set_reg32_bits( P_EFUSE_CNTL4, CNTL1_AUTO_WR_ENABLE_OFF,
		 CNTL4_ENCRYPT_ENABLE_BIT, CNTL4_ENCRYPT_ENABLE_SIZE );
		 // rd off
	aml_set_reg32_bits( P_EFUSE_CNTL1, CNTL1_AUTO_RD_ENABLE_OFF,
		CNTL1_AUTO_RD_ENABLE_BIT, CNTL1_AUTO_RD_ENABLE_SIZE );
		//wr off
		aml_set_reg32_bits( P_EFUSE_CNTL1, CNTL1_AUTO_WR_ENABLE_OFF,
		CNTL1_AUTO_WR_ENABLE_BIT, CNTL1_AUTO_WR_ENABLE_SIZE );
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	// clear power down bit
	aml_set_reg32_bits(P_EFUSE_CNTL1, CNTL1_PD_ENABLE_OFF,
			CNTL1_PD_ENABLE_BIT, CNTL1_PD_ENABLE_SIZE);
#endif		
#endif
	 return 0;

 error4:
	 cdev_del(&efuse_devp->cdev);
 error3:
	 kfree(efuse_devp);
 error2:
	 //class_destroy(efuse_clsp);
	 class_unregister(&efuse_class);
 error1:
	 unregister_chrdev_region(efuse_devno, 1);
 out:
	 return ret;
}

static int efuse_remove(struct platform_device *pdev)
{
	unregister_chrdev_region(efuse_devno, 1);
	//device_destroy(efuse_clsp, efuse_devno);
	device_destroy(&efuse_class, efuse_devno);
	cdev_del(&efuse_devp->cdev);
	kfree(efuse_devp);
	//class_destroy(efuse_clsp);
	class_unregister(&efuse_class);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id amlogic_efuse_dt_match[]={
	{	.compatible = "amlogic,efuse",
	},
	{},
};
#else
#define amlogic_efuse_dt_match NULL
#endif

static struct platform_driver efuse_driver = {
	.probe = efuse_probe,
	.remove = efuse_remove,
	.driver = {
		.name = EFUSE_DEVICE_NAME,
		.of_match_table = amlogic_efuse_dt_match,
	.owner = THIS_MODULE,
	},
};

static int __init efuse_init(void)
{
	int ret = -1;
	ret = platform_driver_register(&efuse_driver);
	if (ret != 0) {
		printk(KERN_ERR "failed to register efuse driver, error %d\n", ret);
		return -ENODEV;
	}
	printk( KERN_INFO "efuse--------------------------------------------\n");

	return ret;
}

static void __exit efuse_exit(void)
{
	platform_driver_unregister(&efuse_driver);
}

module_init(efuse_init);
module_exit(efuse_exit);

MODULE_DESCRIPTION("AMLOGIC eFuse driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bo Yang <bo.yang@amlogic.com>");















