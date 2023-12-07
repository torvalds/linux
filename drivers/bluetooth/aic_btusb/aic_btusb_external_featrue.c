#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/usb.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/device.h>



#define IOCTL_CHAR_DEVICE_NAME "aic_btusb_ex_dev"

#define SET_APCF_PARAMETER	_IOR('E', 181, int)

static dev_t ioctl_devid; /* bt char device number */
static struct cdev ioctl_char_dev; /* bt character device structure */
static struct class *ioctl_char_class; /* device class for usb char driver */

extern struct file_operations ioctl_chrdev_ops;

extern void btchr_external_write(char* data, int len);

static long ioctl_ioctl(struct file *file_p,unsigned int cmd, unsigned long arg)
{
	char data[1024];
	int ret = 0;

	printk("%s enter\r\n", __func__);
	memset(data, 0, 1024);
    switch(cmd) 
    {
        case SET_APCF_PARAMETER:
			printk("set apcf parameter\r\n");
        	ret = copy_from_user(data, (int __user *)arg, 1024);
			btchr_external_write(&data[1], (int)data[0]);
        break;

        default:
			printk("unknow cmdr\r\n");
			break;
    }
    return 0;
}


#ifdef CONFIG_COMPAT
static long compat_ioctlchr_ioctl (struct file *filp, unsigned int cmd, unsigned long arg)
{
    return ioctl_ioctl(filp, cmd, (unsigned long) compat_ptr(arg));
}
#endif


struct file_operations ioctl_chrdev_ops  = {
    unlocked_ioctl   :   ioctl_ioctl,
#ifdef CONFIG_COMPAT
	compat_ioctl :	compat_ioctlchr_ioctl,
#endif

};

static int __init init_extenal_ioctl(void){
	int res = 0;
	struct device *dev;

	printk("%s enter\r\n", __func__);
	
		ioctl_char_class = class_create(THIS_MODULE, IOCTL_CHAR_DEVICE_NAME);
		if (IS_ERR(ioctl_char_class)) {
			printk("Failed to create ioctl char class");
		}
	
		res = alloc_chrdev_region(&ioctl_devid, 0, 1, IOCTL_CHAR_DEVICE_NAME);
		if (res < 0) {
			printk("Failed to allocate ioctl char device");
			goto err_alloc;
		}
	
		dev = device_create(ioctl_char_class, NULL, ioctl_devid, NULL, IOCTL_CHAR_DEVICE_NAME);
		if (IS_ERR(dev)) {
			printk("Failed to create ioctl char device");
			res = PTR_ERR(dev);
			goto err_create;
		}
	
		cdev_init(&ioctl_char_dev, &ioctl_chrdev_ops);
		res = cdev_add(&ioctl_char_dev, ioctl_devid, 1);
		if (res < 0) {
			printk("Failed to add ioctl char device");
			goto err_add;
		}

		return res;
	
err_add:
		device_destroy(ioctl_char_class, ioctl_devid);
err_create:
		unregister_chrdev_region(ioctl_devid, 1);
err_alloc:
		class_destroy(ioctl_char_class);

		return res;

}
static void __exit deinit_extenal_ioctl(void){
	printk("%s enter\r\n", __func__);
    device_destroy(ioctl_char_class, ioctl_devid);
    cdev_del(&ioctl_char_dev);
    unregister_chrdev_region(ioctl_devid, 1);
    class_destroy(ioctl_char_class);

}

module_init(init_extenal_ioctl);
module_exit(deinit_extenal_ioctl);


MODULE_AUTHOR("AicSemi Corporation");
MODULE_DESCRIPTION("AicSemi Bluetooth USB driver version");
MODULE_LICENSE("GPL");

