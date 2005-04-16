/* linux/drivers/char/scx200_gpio.c 

   National Semiconductor SCx200 GPIO driver.  Allows a user space
   process to play with the GPIO pins.

   Copyright (c) 2001,2002 Christer Weinigel <wingel@nano-system.com> */

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/scx200_gpio.h>

#define NAME "scx200_gpio"

MODULE_AUTHOR("Christer Weinigel <wingel@nano-system.com>");
MODULE_DESCRIPTION("NatSemi SCx200 GPIO Pin Driver");
MODULE_LICENSE("GPL");

static int major = 0;		/* default to dynamic major */
module_param(major, int, 0);
MODULE_PARM_DESC(major, "Major device number");

static ssize_t scx200_gpio_write(struct file *file, const char __user *data, 
				 size_t len, loff_t *ppos)
{
	unsigned m = iminor(file->f_dentry->d_inode);
	size_t i;

	for (i = 0; i < len; ++i) {
		char c;
		if (get_user(c, data+i))
			return -EFAULT;
		switch (c)
		{
		case '0': 
			scx200_gpio_set(m, 0); 
			break;
		case '1': 
			scx200_gpio_set(m, 1); 
			break;
		case 'O':
			printk(KERN_INFO NAME ": GPIO%d output enabled\n", m);
			scx200_gpio_configure(m, ~1, 1);
			break;
		case 'o':
			printk(KERN_INFO NAME ": GPIO%d output disabled\n", m);
			scx200_gpio_configure(m, ~1, 0);
			break;
		case 'T':
			printk(KERN_INFO NAME ": GPIO%d output is push pull\n", m);
			scx200_gpio_configure(m, ~2, 2);
			break;
		case 't':
			printk(KERN_INFO NAME ": GPIO%d output is open drain\n", m);
			scx200_gpio_configure(m, ~2, 0);
			break;
		case 'P':
			printk(KERN_INFO NAME ": GPIO%d pull up enabled\n", m);
			scx200_gpio_configure(m, ~4, 4);
			break;
		case 'p':
			printk(KERN_INFO NAME ": GPIO%d pull up disabled\n", m);
			scx200_gpio_configure(m, ~4, 0);
			break;
		}
	}

	return len;
}

static ssize_t scx200_gpio_read(struct file *file, char __user *buf,
				size_t len, loff_t *ppos)
{
	unsigned m = iminor(file->f_dentry->d_inode);
	int value;

	value = scx200_gpio_get(m);
	if (put_user(value ? '1' : '0', buf))
		return -EFAULT;
	
	return 1;
}

static int scx200_gpio_open(struct inode *inode, struct file *file)
{
	unsigned m = iminor(inode);
	if (m > 63)
		return -EINVAL;
	return nonseekable_open(inode, file);
}

static int scx200_gpio_release(struct inode *inode, struct file *file)
{
	return 0;
}


static struct file_operations scx200_gpio_fops = {
	.owner   = THIS_MODULE,
	.write   = scx200_gpio_write,
	.read    = scx200_gpio_read,
	.open    = scx200_gpio_open,
	.release = scx200_gpio_release,
};

static int __init scx200_gpio_init(void)
{
	int r;

	printk(KERN_DEBUG NAME ": NatSemi SCx200 GPIO Driver\n");

	if (!scx200_gpio_present()) {
		printk(KERN_ERR NAME ": no SCx200 gpio pins available\n");
		return -ENODEV;
	}

	r = register_chrdev(major, NAME, &scx200_gpio_fops);
	if (r < 0) {
		printk(KERN_ERR NAME ": unable to register character device\n");
		return r;
	}
	if (!major) {
		major = r;
		printk(KERN_DEBUG NAME ": got dynamic major %d\n", major);
	}

	return 0;
}

static void __exit scx200_gpio_cleanup(void)
{
	unregister_chrdev(major, NAME);
}

module_init(scx200_gpio_init);
module_exit(scx200_gpio_cleanup);

/*
    Local variables:
        compile-command: "make -k -C ../.. SUBDIRS=drivers/char modules"
        c-basic-offset: 8
    End:
*/
