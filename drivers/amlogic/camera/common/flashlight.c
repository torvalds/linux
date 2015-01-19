/*
 * Flash light char device driver.
 *
 * Author: Heming Lv <heming.lv@amlogic.com>
 *
 * Copyright (c) 2011 Amlogic Inc.
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
#include <linux/platform_device.h>
#include <media/amlogic/flashlight.h>

#define FLASHLIGHT_MODULE_NAME   "flashlight"
#define FLASHLIGHT_DRIVER_NAME "flashlight"
#define FLASHLIGHT_DEVICE_NAME   "flashlight"
#define FLASHLIGHT_CLASS_NAME   "flashlight"

static dev_t flashlight_devno;
static struct cdev *flashlight_cdev=NULL;
static struct device *devp=NULL;

static aml_plat_flashlight_status_t flashlight_flag = FLASHLIGHT_OFF;

static ssize_t flashlight_ctrl(struct class *cla, struct class_attribute *attr, const char *buf, size_t count);
static ssize_t flashlight_getflag(struct class *cla, struct class_attribute *attr, char *buf);
static ssize_t flashlight_setflag(struct class *cla, struct class_attribute *attr, const char *buf, size_t count);
static int  flashlight_open(struct inode *inode,struct file *file);
static int  flashlight_release(struct inode *inode,struct file *file);
static int flashlight_probe(struct platform_device *pdev);
static int flashlight_remove(struct platform_device *pdev);


static struct platform_driver flashlight_driver = {
	 .probe = flashlight_probe,
	 .remove = flashlight_remove,
	 .driver = {
	 .name = FLASHLIGHT_DRIVER_NAME,
	 .owner = THIS_MODULE,
	 },
 };

static const struct file_operations flashlight_fops = {
	.open	= flashlight_open,
	.release	= flashlight_release,
};

static struct class_attribute flashlight_class_attrs[] = {
    __ATTR(flashlightctrl,S_IWUGO,NULL,flashlight_ctrl),
    __ATTR(flashlightflag,S_IRUGO|S_IWUGO,flashlight_getflag,flashlight_setflag),
    __ATTR_NULL
};
static struct class flashlight_class = {
    .name = FLASHLIGHT_CLASS_NAME,
    .class_attrs = flashlight_class_attrs,
    .owner = THIS_MODULE,
};

static int flashlight_device_match(struct device *dev, void *data)
{
	return (!strcmp(dev->kobj.name,(const char*)data));
}

struct device *flashlight_class_to_device(struct class *cla)
{
	struct device		*dev;
	dev = class_find_device(cla, NULL, (void*)cla->name,flashlight_device_match);
	if (!dev)
		printk("%s:%s no matched device found!\n",FLASHLIGHT_MODULE_NAME,__FUNCTION__);
	return dev;
}

static ssize_t flashlight_ctrl(struct class *cla, struct class_attribute *attr, const char *buf, size_t count)
{
	aml_plat_flashlight_data_t *pdata = NULL;
	struct device	*dev = NULL;
	dev = flashlight_class_to_device(cla);
	pdata = (aml_plat_flashlight_data_t *)dev->platform_data;
	if(pdata == NULL){
		printk("%s platform data is required!\n",__FUNCTION__);
		return -1;
	}
	if(!strncmp(buf,"0",1)){
		if(pdata->flashlight_off)
			pdata->flashlight_off();
	}
	else if (!strncmp(buf,"1",1)){
		if(pdata->flashlight_on)
			pdata->flashlight_on();
	}
	else{
		printk( KERN_ERR"%s:%s error!Not support this parameter\n",FLASHLIGHT_MODULE_NAME,__FUNCTION__);
		return -EINVAL;
	}
	return count;
}

static ssize_t flashlight_getflag(struct class *cla, struct class_attribute *attr, char *buf)
{
	sprintf(buf,"%d",(int)flashlight_flag);
	return strlen(buf);
}

static ssize_t flashlight_setflag(struct class *cla, struct class_attribute *attr, const char *buf, size_t count)
{
	if(!strlen(buf)){
		printk("%s parameter is required!\n",__FUNCTION__);
	}
	flashlight_flag = (aml_plat_flashlight_status_t)(buf[0]-'0');
	return count;
}

static int  flashlight_open(struct inode *inode,struct file *file)
{
	return 0;
}

static int  flashlight_release(struct inode *inode,struct file *file)
{
	return 0;
}

static int flashlight_probe(struct platform_device *pdev)
{
	int ret;
	aml_plat_flashlight_data_t *pdata = NULL;

	ret = alloc_chrdev_region(&flashlight_devno, 0, 1, FLASHLIGHT_DRIVER_NAME);
	if (ret < 0) {
		printk(KERN_ERR "%s:%s failed to allocate major number\n",FLASHLIGHT_MODULE_NAME,__FUNCTION__);
		ret = -ENODEV;
		goto out;
	}
	ret = class_register(&flashlight_class);
	if (ret < 0) {
		printk(KERN_ERR "%s:%s  failed to register class\n",FLASHLIGHT_MODULE_NAME,__FUNCTION__);
		goto error1;
	}
	flashlight_cdev = cdev_alloc();
	if ( !flashlight_cdev ) {
		printk(KERN_ERR "%s:%s: failed to allocate memory\n",FLASHLIGHT_MODULE_NAME,__FUNCTION__);
		ret = -ENOMEM;
		goto error2;
	}
	cdev_init(flashlight_cdev, &flashlight_fops);
	flashlight_cdev->owner = THIS_MODULE;
	ret = cdev_add(flashlight_cdev, flashlight_devno, 1);
	if (ret) {
		printk(KERN_ERR "%s:%s: failed to add device\n",FLASHLIGHT_MODULE_NAME,__FUNCTION__);
		goto error3;
	}
	devp = device_create(&flashlight_class, NULL, flashlight_devno, NULL, FLASHLIGHT_DEVICE_NAME);
	if (IS_ERR(devp)) {
		printk(KERN_ERR "%s:%s failed to create device node\n",FLASHLIGHT_MODULE_NAME,__FUNCTION__);
		ret = PTR_ERR(devp);
		goto error3;
	}
	printk(KERN_INFO "%s:%s device %s created\n", FLASHLIGHT_MODULE_NAME,__FUNCTION__,FLASHLIGHT_DEVICE_NAME);
	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "platform data is required!\n");
		ret = -EINVAL;
		goto error4;
	}
	devp->platform_data = pdata;
	return 0;
error4:
	device_destroy(NULL, flashlight_devno);
error3:
	cdev_del(flashlight_cdev);
error2:
	class_unregister(&flashlight_class);
error1:
	unregister_chrdev_region(flashlight_devno, 1);
out:
	return ret;
}

static int flashlight_remove(struct platform_device *pdev)
{
	unregister_chrdev_region(flashlight_devno, 1);
	class_unregister(&flashlight_class);
	device_destroy(NULL, flashlight_devno);
	cdev_del(flashlight_cdev);
	return 0;
}

int set_flashlight(bool mode)
{
	aml_plat_flashlight_data_t *pdata = NULL;
	if(devp&&devp->platform_data){
		pdata = devp->platform_data;
		if(!mode){
			if(pdata->flashlight_off)
				pdata->flashlight_off();
		}
		else {
			if(pdata->flashlight_on)
				pdata->flashlight_on();
		}
	}
}

EXPORT_SYMBOL(set_flashlight);

aml_plat_flashlight_status_t get_flashlightflag(void)
{
	return flashlight_flag;
}

EXPORT_SYMBOL(get_flashlightflag);

static int __init flashlight_init(void)
{
	int ret = -1;
	ret = platform_driver_register(&flashlight_driver);
	if (ret != 0) {
		printk(KERN_ERR "failed to register flashlight driver, error %d\n", ret);
		return -ENODEV;
	}
	return ret;
}

static void __exit flashlight_exit(void)
{
	platform_driver_unregister(&flashlight_driver);
}

module_init(flashlight_init);
module_exit(flashlight_exit);

MODULE_DESCRIPTION("AMLOGIC flashlight driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("amlogic");

