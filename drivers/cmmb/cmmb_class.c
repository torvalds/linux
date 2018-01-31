/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mutex.h>

#include "cmmb_class.h"


#if 1
#define DBGERR(fmt...)    printk(KERN_DEBUG fmt)
#else
#define DBGERR(fmt...)
#endif

#if 0
#define DBG(fmt...)    printk(KERN_DEBUG fmt)
#else
#define DBG(fmt...)
#endif

#define MAX_CMMB_ADAPTER    2
#define MAX_CMMB_MINORS (MAX_CMMB_ADAPTER*4)


struct cmmb_adapter CMMB_adapter;
struct class * cmmb_class;

static struct cdev cmmb_device_cdev;

static int cmmb_device_open(struct inode *inode, struct file *file);

static struct file_operations cmmb_device_fops =
{
	.owner =	THIS_MODULE,
	.open =		cmmb_device_open
};

static struct cmmb_device* cmmb_find_device (int minor)
{
	
    struct cmmb_device *dev;
    DBG("[CMMB HW]:[class]:enter cmmb_find_device\n");
		
	list_for_each_entry(dev, &CMMB_adapter.device_list, list_head)
	if (dev->type == minor)
	return dev;
	
	return NULL;
}

static int cmmb_device_open(struct inode *inode, struct file *file)
{
	struct cmmb_device *cmmbdev;
    
    DBG("[CMMB HW]:[class]:enter cmmb_device_open\n");
    
	cmmbdev = cmmb_find_device (iminor(inode));
    
    DBG("[CMMB HW]:[class]:cmmbdev.type%d\n",cmmbdev->type);
    
	if (cmmbdev && cmmbdev->fops) {
		int err = 0;
		const struct file_operations *old_fops;

		file->private_data = cmmbdev;
		old_fops = file->f_op;
		file->f_op = fops_get(cmmbdev->fops);
		if(file->f_op->open)
			err = file->f_op->open(inode,file);
		if (err) {
			fops_put(file->f_op);
			file->f_op = fops_get(old_fops);
		}
		fops_put(old_fops);
		return err;
	}
	return -ENODEV;
}


static int cmmb_register_adapter(const char *name, struct device *device)
{
    DBG("[CMMB HW]:[class]:cmmb_register_adapter\n");

	memset (&CMMB_adapter, 0, sizeof(struct cmmb_adapter));
    
	INIT_LIST_HEAD (&CMMB_adapter.device_list);

	CMMB_adapter.num = 0;
	CMMB_adapter.name = name;
	CMMB_adapter.device = device;
    
	return 0;
}


static int cmmb_unregister_adapter(struct cmmb_adapter *adap)
{
    DBG("[CMMB HW]:[class]:cmmb_unregister_adapter\n");
    
	memset (&CMMB_adapter, 0, sizeof(struct cmmb_adapter));

	return 0;
}


int cmmb_register_device(struct cmmb_adapter *adap, struct cmmb_device **pcmmbdev,
			 struct file_operations *fops, void *priv, int type,char* name)
{
	struct cmmb_device *cmmbdev;
	struct file_operations *cmmbdevfops;
	struct device *clsdev;

    DBG("[CMMB HW]:[class]:cmmb_register_device\n");
    
	*pcmmbdev = cmmbdev = kmalloc(sizeof(struct cmmb_device), GFP_KERNEL);
    if(!pcmmbdev)
    {
       DBGERR("[CMMB HW]:[class]:[err]: cmmb register device cmmbdev malloc fail!!!\n");
       return -ENOMEM;
    }
    
	cmmbdevfops = kzalloc(sizeof(struct file_operations), GFP_KERNEL);

	if (!cmmbdevfops){
        DBGERR("[CMMB HW]:[class]:[err]: cmmb register device cmmbdevfops malloc fail!!!\n");
		kfree (cmmbdev);
		return -ENOMEM;
	}

	cmmbdev->type = type;
	cmmbdev->adapter = adap;
	cmmbdev->priv = priv;
	cmmbdev->fops = cmmbdevfops;
    
	init_waitqueue_head (&cmmbdev->wait_queue);

	memcpy(cmmbdev->fops, fops, sizeof(struct file_operations));
	cmmbdev->fops->owner = THIS_MODULE;

	list_add_tail (&cmmbdev->list_head, &adap->device_list);

	clsdev = device_create(cmmb_class, adap->device,MKDEV(CMMB_MAJOR, type),NULL,name);
	if (IS_ERR(clsdev)) {
		DBGERR("[CMMB HW]:[class]:[err]: creat dev fail!!!\n");
		return PTR_ERR(clsdev);
	}
    
	return 0;
}
EXPORT_SYMBOL(cmmb_register_device);


void cmmb_unregister_device(struct cmmb_device *cmmbdev)
{
	if (!cmmbdev)
		return;
    
    DBG("[CMMB HW]:[class]:cmmb_unregister_device\n");

	device_destroy(cmmb_class, MKDEV(CMMB_MAJOR, cmmbdev->type));

	list_del (&cmmbdev->list_head);
	kfree (cmmbdev->fops);
	kfree (cmmbdev);
}
EXPORT_SYMBOL(cmmb_unregister_device);


ssize_t cmmb_class_show_name(struct class * class, char * buf, size_t count, loff_t off)
{
#if defined(CONFIG_IFxxx_CMMB_Chip_Support)
	memcpy(buf,"inno",5);
	return 5;
#else
	memcpy(buf,"siano",6);
	return 6;
#endif
	
}  

static CLASS_ATTR(name, 0660, cmmb_class_show_name, NULL);

static int __init init_cmmbclass(void)
{
    int retval;
    struct cmmb_adapter* cmmbadapter;
    struct cmmb_device * tunerdev;
    dev_t dev = MKDEV(CMMB_MAJOR, 0);
    
    DBG("[CMMB HW]:[class]: init_cmmbclass\n");

	if ((retval = register_chrdev_region(dev, CMMB_MAJOR, "CMMB")) != 0){
		DBGERR("[CMMB HW]:[class]:[err]: register chrdev fail!!!\n");
		return retval;
	}

	cdev_init(&cmmb_device_cdev, &cmmb_device_fops);
	if ((retval = cdev_add(&cmmb_device_cdev, dev, MAX_CMMB_MINORS)) != 0){
		DBGERR("[CMMB HW]:[class]:[err]: cedv add fail!!!\n");
		goto error;
	}

	cmmb_class = class_create(THIS_MODULE, "cmmb");
	if (IS_ERR(cmmb_class)) {
        DBGERR("[CMMB HW]:[class]:[err]: class creat fail!!!\n");
		retval = PTR_ERR(cmmb_class);
		goto error;
	}
        retval = class_create_file(cmmb_class, &class_attr_name);
        if(retval < 0)
        {
            DBGERR("cmmb_class create attribute failed\n");
        } 
         cmmb_register_adapter("cmmb_adapter", NULL);

	return 0;

error:
	cdev_del(&cmmb_device_cdev);
	unregister_chrdev_region(dev, MAX_CMMB_MINORS);
	return retval;
}


static void __exit exit_cmmbclass(void)
{
	DBG("[CMMB HW]:[class]: exit_cmmbclass\n");

	cdev_del(&cmmb_device_cdev);
	cmmb_unregister_adapter(&CMMB_adapter);
	unregister_chrdev_region(MKDEV(CMMB_MAJOR, 0), MAX_CMMB_MINORS);
	class_remove_file(cmmb_class, &class_attr_name);
	class_destroy(cmmb_class);
}


subsys_initcall(init_cmmbclass);
module_exit(exit_cmmbclass);

MODULE_DESCRIPTION("CMMB CORE");
MODULE_AUTHOR("HT,HZB,HH,LW");
MODULE_LICENSE("GPL");

