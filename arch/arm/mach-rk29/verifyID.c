#include <linux/module.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include "verifyID.h"



static int verifyid_open(struct inode *inode, struct file *file)
{
	int ret;
	ret = generic_file_open(inode, file);
	if (unlikely(ret))
		return ret;
	return 0;
}

static int verifyid_release(struct inode *ignored, struct file *file)
{

	return 0;
}

static int GetChipTag(void)
{
    unsigned long i;
    unsigned long value; 
    value = read_XDATA32(RK29_GPIO6_BASE+0x4);
	printk("read gpio6+4 = 0x%x\n",value);
    write_XDATA32((RK29_GPIO6_BASE+0x4), (read_XDATA32(RK29_GPIO6_BASE+0x4)&(~(0x7ul<<28)))); // portD 4:6 input
    value = read_XDATA32(RK29_GPIO6_BASE+0x50); 
	printk("read gpio6+0x50 = 0x%x\n",value);
    value = (value>>28)&0x07; 
	
    return value;
}
static long verifyid_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = -ENOTTY;

	switch (cmd) {
	case VERIFYID_GETID:
		ret=GetChipTag();
		*((unsigned long *)arg) = 1;
		break;
	}

	return ret;
}

static ssize_t verifyid_read(struct file *file, char __user *buf,
			   size_t len, loff_t *pos)
{
	long ret = GetChipTag();
	char *kb = {0x11,0x22,0x33,0x44};
	if(ret>0)
		//*buf = 0xf8;
		copy_to_user(buf,kb,1);
	return ret;
}

static struct file_operations verifyid_fops = {
	.owner = THIS_MODULE,
	.open = verifyid_open,
	.read = verifyid_read,
	.release = verifyid_release,
	.unlocked_ioctl = verifyid_ioctl,
	.compat_ioctl = verifyid_ioctl,
};

static struct miscdevice verifyid_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "verifyid",
	.fops = &verifyid_fops,
};

static int __init verifyid_init(void)
{
	int ret;
	ret = misc_register(&verifyid_misc);
	if (unlikely(ret)) {
		printk(KERN_ERR "verifyid: failed to register misc device!\n");
		return ret;
	}
	printk(KERN_INFO "verifyid: initialized\n");

	return 0;
}

static void __exit verifyid_exit(void)
{
	int ret;

	ret = misc_deregister(&verifyid_misc);
	if (unlikely(ret))
		printk(KERN_ERR "verifyid: failed to unregister misc device!\n");

	printk(KERN_INFO "verifyid: unloaded\n");
}

module_init(verifyid_init);
module_exit(verifyid_exit);

MODULE_LICENSE("GPL");

